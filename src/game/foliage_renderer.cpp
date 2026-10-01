module game;

import :foliage_renderer;

import std.compat;
import assertions;

import render;
import render_scene;
import foliage;
import terrain;
import assets;
import glm;
import rhmath;
import name;
import profile;

import log;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogFoliageRenderer, Display);

namespace
{
    constexpr uint32_t instance_size = 32;         // FoliageInstance
    constexpr uint32_t draw_size = 20;             // VkDrawIndexedIndirectCommand
    constexpr uint32_t group = 8;                  // local size of foliage_cull.comp (x and y)
    constexpr uint32_t max_cells = 1024;           // per side of a type's grid

    size_t buffer_size(const RenderResource& resource, Name variable)
    {
        return resource.find_var_checked(variable).parameter.initial_buffer_size.value_or(0);
    }

    glm::vec4 color(const vec3& value, float w)
    {
        return glm::vec4(value.x, value.y, value.z, w);
    }

    // planes of a Vulkan (depth 0..1) view projection, inside: dot(xyz, p) + w >= 0
    std::array<glm::vec4, 6> frustum_planes(const glm::mat4& m)
    {
        const glm::vec4 row0(m[0][0], m[1][0], m[2][0], m[3][0]);
        const glm::vec4 row1(m[0][1], m[1][1], m[2][1], m[3][1]);
        const glm::vec4 row2(m[0][2], m[1][2], m[2][2], m[3][2]);
        const glm::vec4 row3(m[0][3], m[1][3], m[2][3], m[3][3]);
        std::array<glm::vec4, 6> planes{ row3 + row0, row3 - row0, row3 + row1, row3 - row1, row2, row3 - row2 };
        for (glm::vec4& plane : planes)
            plane /= std::max(glm::length(glm::vec3(plane)), 1e-6f);
        return planes;
    }
}

void FoliageRenderer::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    foliage_resource = renderer->find_resource("foliage");
    instances_resource = renderer->find_resource("foliage_instances");
    checkf(foliage_resource && instances_resource, "ground foliage resources are missing (assets/render/resources)");

    auto model = renderer->find_model("ground_foliage");
    checkf(model, "material model 'ground_foliage' is missing (assets/render/schemas/ground_foliage.json)");
    cull_family = renderer->query_pipeline_family("FoliageCull", model);
    draw_family = renderer->query_pipeline_family("GroundFoliage", model);

    instance_capacity = uint32_t(buffer_size(*instances_resource, "u_foliage_instances") / instance_size);
    bucket_capacity = uint32_t(std::min(buffer_size(*instances_resource, "u_foliage_draws") / draw_size,
                                        buffer_size(*foliage_resource, "u_foliage_buckets") / sizeof(GPUFoliageBucket)));
    heights_capacity = uint32_t(buffer_size(*foliage_resource, "u_foliage_heights") / sizeof(float));
    table_capacity = uint32_t((buffer_size(*foliage_resource, "u_foliage") - sizeof(GPUFoliageGlobals)) / sizeof(GPUFoliageType));
    checkf(table_capacity >= max_types, "the foliage table holds too few types (assets/render/resources/foliage.json)");
}

void FoliageRenderer::prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs)
{
    PROFILE("FoliageRenderer::prepare");

    active = false;
    dispatches.clear();
    bucket_blocks.clear();
    bucket_count = 0;
    // pipelines are requested every frame: hot reload drops the PSO cache
    cull_pipeline = cull_family->request_pipeline({});
    draw_pipeline = draw_family->request_pipeline({});

    const SceneViewProcessor_Foliage& processor = scene_view.get_processor<SceneViewProcessor_Foliage>();
    if (!cv_foliage_enabled.get() || !processor.valid || !processor.terrain || processor.types.empty())
        return;
    const TerrainData& terrain = *processor.terrain;
    if (terrain.samples * terrain.samples > heights_capacity)
    {
        if (!size_reported)
            LogFoliageRenderer.Log<Error>("Terrain of %u^2 samples: the foliage heights buffer holds %u (assets/render/resources/foliage.json)",
                terrain.samples, heights_capacity);
        size_reported = true;
        return;
    }

    // ---- heights: this frame's copy, when the terrain or its heights changed ----
    HeightsCopy& copy = heights_copies[ctx.frame % heights_copies.size()];
    if (copy.terrain != &terrain || copy.version != terrain.heights_version)
    {
        PROFILE("FoliageRenderer: heights");
        const std::span<std::byte> memory = foliage_resource->query_single(0)->map_ssbo("u_foliage_heights", ctx.frame);
        std::memcpy(memory.data(), terrain.heights.data(), terrain.heights.size() * sizeof(float));
        copy.terrain = &terrain;
        copy.version = terrain.heights_version;
    }
    foliage_resource->update_image("u_foliage_mask", renderer->get_texture(terrain.foliage), { .frame = ctx.frame });

    // ---- types and their buckets ----
    const float distance_scale = std::clamp(cv_foliage_distance.get(), 0.1f, 4.0f);
    const float density_scale = std::clamp(cv_foliage_density.get(), 0.05f, 4.0f);
    const uint32_t type_count = uint32_t(std::min<size_t>(processor.types.size(), max_types));

    const std::span<std::byte> table_memory = foliage_resource->query_single(0)->map_ssbo("u_foliage", ctx.frame);
    auto* globals = reinterpret_cast<GPUFoliageGlobals*>(table_memory.data());
    auto* types = reinterpret_cast<GPUFoliageType*>(table_memory.data() + sizeof(GPUFoliageGlobals));
    const std::span<std::byte> bucket_memory = foliage_resource->query_single(0)->map_ssbo("u_foliage_buckets", ctx.frame);
    auto* buckets = reinterpret_cast<GPUFoliageBucket*>(bucket_memory.data());

    // what each bucket could hold at most (the plants of its ring around the camera): the instances are shared
    // out in proportion
    std::vector<double> weights;
    double total_weight = 0.0;
    const auto setup_start = std::chrono::steady_clock::now();
    const size_t meshes_before = meshes.size(), materials_before = materials.size();
    const glm::vec2 camera_xz(inputs.camera_position.x, inputs.camera_position.z);
    for (uint32_t index = 0; index < type_count; ++index)
    {
        const FoliageType& type = processor.types[index];
        const FoliageAsset& asset = *type.loaded;
        const uint32_t variant_count = uint32_t(type.variant_indices.size());
        if (bucket_count + variant_count * lods > bucket_capacity)
        {
            LogFoliageRenderer.Log<Warning>("Foliage type '%s' and the ones after it: more than %u buckets", type.name.c_str(), bucket_capacity);
            break;
        }

        const float density = std::max(type.density, 0.0f) * density_scale;
        const float fade_end = std::max(type.fade_end, 0.1f) * distance_scale;
        const float fade_start = std::clamp(type.fade_start * distance_scale, 0.0f, fade_end);
        const float lod1 = std::clamp(type.lod1_distance * distance_scale, 0.0f, fade_end);
        const float lod2 = std::clamp(type.lod2_distance * distance_scale, lod1, fade_end);
        const float spacing = density > 0.0f ? 1.0f / std::sqrt(density) : 1.0f;

        // the material, registered once (its textures enter the texture array)
        auto material = materials.find(asset.material.get());
        if (material == materials.end())
        {
            std::shared_ptr<MaterialInstance> instance = renderer->query_material_instance(asset.material, "GeometryBase");
            instance->apply_material_parameters();
            material = materials.emplace(asset.material.get(), std::move(instance)).first;
        }
        const uint32_t material_id = material->second->material_id;

        float radius = 0.0f, height = 0.0f;
        const uint32_t first_bucket = bucket_count;
        for (const uint32_t variant_index : type.variant_indices)
        {
            const FoliageVariant& variant = asset.variants[variant_index];
            radius = std::max(radius, variant.radius);
            height = std::max(height, variant.height);
            for (uint32_t lod = 0; lod < lods; ++lod)
            {
                const MeshHandle handle = variant.lods[lod];
                auto uploaded = meshes.find(handle.id);
                if (uploaded == meshes.end())
                {
                    const uint32_t mesh_index = uint32_t(backend->get_or_create_mesh_buffers(MeshPrimHandle{ handle, 0, 0 },
                        RTBuildMode::none).mesh_index);
                    uploaded = meshes.emplace(handle.id, UploadedMesh{ mesh_index, backend->get_mesh_index_range(mesh_index) }).first;
                }
                GPUFoliageBucket& bucket = buckets[bucket_count++];
                bucket.draw = glm::uvec4(uploaded->second.indices.index_count, uploaded->second.indices.first_index, 0, 0);
                bucket.mesh = glm::uvec4(uploaded->second.mesh_index, material_id, index, lod);
                bucket.size = glm::vec4(variant.height, variant.radius, 0.0f, 0.0f);
                bucket_blocks.push_back(uploaded->second.indices.block);
            }
        }

        GPUFoliageType& gpu = types[index];
        gpu.placement = glm::vec4(float(variant_count), float(std::min(type.channel, 3u)), density, spacing);
        gpu.fade = glm::vec4(fade_start, fade_end, lod1, lod2);
        gpu.mask = glm::vec4(std::clamp(type.mask_threshold, 0.0f, 0.99f), type.patch_scale,
            std::clamp(type.patch_coverage, 0.0f, 1.0f), float((type.seed + index * 7919u) % 65536u));
        gpu.size = glm::vec4(type.scale_min, std::max(type.scale_max, type.scale_min), type.sink, std::clamp(type.ground_align, 0.0f, 1.0f));
        gpu.bounds = glm::vec4(radius, height, type.wind, type.transmission);
        gpu.tint = color(type.tint, std::clamp(type.tint_variation, 0.0f, 1.0f));
        gpu.dry = color(type.dry_color, std::clamp(type.dry_fraction, 0.0f, 1.0f));
        gpu.color = glm::vec4(type.color_noise, 0.0f, 0.0f, 0.0f);
        gpu.buckets = glm::uvec4(first_bucket, variant_count, 0, 0);

        // the rings of the LODs, the thinning part (fade_start .. fade_end) counted half, shared by the variants
        const float edges[4] = { 0.0f, lod1, lod2, fade_end };
        for (uint32_t variant = 0; variant < variant_count; ++variant)
        {
            for (uint32_t lod = 0; lod < lods; ++lod)
            {
                auto area = [] (float inner, float outer) { return std::numbers::pi * (double(outer) * outer - double(inner) * inner); };
                const float inner = edges[lod], outer = edges[lod + 1];
                const double full = area(inner, std::clamp(fade_start, inner, outer));
                const double thinning = area(std::clamp(fade_start, inner, outer), outer) * 0.5;
                const double weight = density > 0.0f ? (full + thinning) * density / variant_count : 0.0;
                weights.push_back(weight);
                total_weight += weight;
            }
        }

        if (density <= 0.0f || variant_count == 0)
            continue;
        // the candidate grid: every cell within fade_end of the camera
        const float grid_radius = std::min(fade_end, 0.5f * spacing * float(max_cells - 2));
        const glm::ivec2 first(int32_t(std::floor((camera_xz.x - grid_radius) / spacing)), int32_t(std::floor((camera_xz.y - grid_radius) / spacing)));
        const glm::ivec2 last(int32_t(std::floor((camera_xz.x + grid_radius) / spacing)), int32_t(std::floor((camera_xz.y + grid_radius) / spacing)));
        dispatches.push_back({ .type = index, .first_cell = first, .cells = uint32_t(std::max(last.x - first.x, last.y - first.y) + 1) });
    }

    if (meshes.size() != meshes_before || materials.size() != materials_before)
        LogFoliageRenderer.Log("Uploaded %zu foliage meshes and %zu materials in %.0f ms", meshes.size() - meshes_before,
            materials.size() - materials_before,
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - setup_start).count());

    // the instances in proportion to the weights
    uint32_t next = 0;
    for (uint32_t bucket = 0; bucket < bucket_count; ++bucket)
    {
        const double share = total_weight > 0.0 ? weights[bucket] / total_weight : 0.0;
        const uint32_t capacity = std::min(uint32_t(share * double(instance_capacity)), instance_capacity - next);
        buckets[bucket].draw.z = next;
        buckets[bucket].draw.w = capacity;
        next += capacity;
    }

    // ---- globals ----
    const float wind_heading = processor.wind_heading;
    *globals = {};
    globals->terrain = glm::vec4(terrain.origin.x, terrain.origin.z, terrain.spacing, float(terrain.samples));
    const float time = float(std::fmod(processor.time, 3600.0));
    globals->terrain_info = glm::vec4(terrain.origin.y, terrain.size(), time,
        previous_time >= 0.0f && previous_time <= time ? previous_time : time);
    previous_time = time;
    const std::array<glm::vec4, 6> planes = frustum_planes(inputs.view_proj);
    for (size_t i = 0; i < planes.size(); ++i)
        globals->frustum[i] = planes[i];
    globals->camera = glm::vec4(inputs.camera_position, 0.0f);
    globals->wind = glm::vec4(std::cos(wind_heading), std::sin(wind_heading), processor.wind_strength, processor.gust_strength);
    globals->gust = glm::vec4(processor.gust_speed, 1.0f / processor.gust_scale, 0.0f, 0.0f);
    const bool hzb = inputs.hzb_valid && cv_foliage_occlusion.get() && inputs.hzb_levels > 0;
    globals->hzb = glm::uvec4(inputs.hzb_width, inputs.hzb_height, inputs.hzb_levels, hzb ? 1u : 0u);
    // the characters nearest to the camera
    std::vector<FoliageInteractor> interactors = processor.interactors;
    std::ranges::sort(interactors, {}, [&] (const FoliageInteractor& i) { return glm::distance(i.position, inputs.camera_position); });
    const uint32_t interactor_count = uint32_t(std::min<size_t>(interactors.size(), 4));
    for (uint32_t i = 0; i < interactor_count; ++i)
        globals->interactors[i] = glm::vec4(interactors[i].position, interactors[i].radius);
    globals->counts = glm::uvec4(type_count, interactor_count, bucket_count, 0);

    active = !dispatches.empty();
}

void FoliageRenderer::cull(RenderGraphContext& ctx, RenderResource* camera, RenderResource* occlusion)
{
    PROFILE("FoliageRenderer::cull");

    // the draws of the last frame read the buffers
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::draw_to_compute);
    ctx.bind_pipeline(cull_pipeline);
    ctx.bind(camera, foliage_resource, instances_resource, occlusion);

    ctx.push_constants(FoliageCullPushConstants{ .mode = 0, .bucket_count = bucket_count });
    ctx.compute({ (bucket_count + group - 1) / group, 1, 1 });
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_compute);

    for (const Dispatch& dispatch : dispatches)
    {
        ctx.push_constants(FoliageCullPushConstants{
            .mode = 1,
            .type = dispatch.type,
            .first_cell_x = dispatch.first_cell.x,
            .first_cell_z = dispatch.first_cell.y,
            .cells = dispatch.cells,
            .bucket_count = bucket_count,
        });
        const uint32_t groups = (dispatch.cells + group - 1) / group;
        ctx.compute({ groups, groups, 1 });
    }
    backend->cmd_buffer_barrier(ctx.cmd, RenderBackend::BufferBarrier::compute_to_draw);
}

void FoliageRenderer::draw(RenderGraphContext& ctx, const DrawResources& resources)
{
    PROFILE("FoliageRenderer::draw");

    if (ctx.bind_pipeline(draw_pipeline))
        ctx.bind(resources.camera, resources.mesh_table, foliage_resource, instances_resource, resources.material_table,
            resources.textures);

    const RBBufferHandle draws = instances_resource->query_single(0)->get_ssbo_handle("u_foliage_draws", std::nullopt);
    // runs of buckets in one index block: one multi draw each
    for (uint32_t first = 0; first < bucket_count;)
    {
        uint32_t end = first + 1;
        while (end < bucket_count && bucket_blocks[end] == bucket_blocks[first])
            ++end;
        backend->bind_mesh_index_block(ctx.cmd, bucket_blocks[first]);
        backend->draw_indexed_indirect(ctx.cmd, draws, ctx.frame, uint64_t(first) * draw_size, end - first, draw_size);
        first = end;
    }
}
