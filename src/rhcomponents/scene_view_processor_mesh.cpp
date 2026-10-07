module rhcomponents;

import :scene_view_proxy.mesh;

import std.compat;
import assertions;

import render_scene;
import framework;
import globals;
import profile;
import render;
#include "common/assertion_macros.h"
#include "profiling/profile.h"


RenderId SceneViewProcessor_Mesh::register_proxy()
{
    RenderId render_id;
    if (!vacated_mesh_ids.empty())
    {
        render_id = vacated_mesh_ids.back();
        render_id.generation++;
        vacated_mesh_ids.pop_back();
    }
    else
    {
        render_id = RenderId(meshes.size(), 0);
        meshes.push_back({});
    }
    RenderObject_Mesh& ro = meshes[render_id.identifier];
    ro.alive = true;
    ro.generation = render_id.generation;

    dirty = true;

    return render_id;
}

void SceneViewProcessor_Mesh::unregister_proxy(RenderId render_id)
{
    RenderObject_Mesh& ro = meshes[render_id.identifier];
    if (!ro.alive || ro.generation != render_id.generation)
        return;

    retire_primitives(ro);
    const uint32_t generation = ro.generation;
    ro = {};
    ro.generation = generation;
    vacated_mesh_ids.push_back(render_id);
    dirty = true;
}

void SceneViewProcessor_Mesh::retire_primitives(RenderObject_Mesh& ro)
{
    if (ro.primitives.empty())
        return;
    // null while the engine shuts down: nothing is released then
    Renderer* renderer = RhGlobals::engine ? RhGlobals::engine->renderer.get() : nullptr;
    for (RenderPrimitiveId prim_index : ro.primitives)
    {
        RenderPrimitive& rp = primitives[prim_index];
        if (rp.skinned && renderer)
            renderer->get_backend()->release_skinned_mesh(rp.skinned->instance_id);
        rp.skinned.reset();
        rp.passes.clear();
        rp.info_by_pass.clear();
        rp.skinning.reset();
        rp.shadow_proxies.clear();
        rp.primitive_material.reset();
        free_primitive_ids.push_back(prim_index);
    }
    ro.primitives.clear();
    if (renderer)
        renderer->request_material_collection();
}

RenderPrimitiveId SceneViewProcessor_Mesh::add_primitive(RenderPrimitive&& rp)
{
    if (free_primitive_ids.empty())
    {
        rp.id = render_primitive_id_counter++;
        checkf(rp.id == primitives.size(), "Render primitive ids out of step with their slots");
        primitives.push_back(std::move(rp));
        return primitives.back().id;
    }
    const RenderPrimitiveId slot = free_primitive_ids.back();
    free_primitive_ids.pop_back();
    rp.id = slot;
    primitives[slot] = std::move(rp);
    return slot;
}



void SceneViewProcessor_Mesh::process()
{
    auto& renderer = *RhGlobals::engine->renderer;

    constexpr RTBuildMode rt_build_mode = render_settings::enable_raytracing ? RTBuildMode::build_blas : RTBuildMode::none;

    // the primitives of new and changed meshes are prepared for their buffers on all cores first (vertex cache
    // optimization: 1.4 s for the level on one core), the loop below creates them one by one
    {
        std::vector<MeshPrimHandle> new_primitives;
        auto add_mesh = [&] (MeshHandle handle)
        {
            const StaticMesh& mesh = handle.get();
            for (uint32_t geom = 0; geom < mesh.mesh_geometry.size(); ++geom)
                for (uint32_t prim_index = 0; prim_index < mesh.mesh_geometry[geom].primitives.size(); ++prim_index)
                    new_primitives.push_back(MeshPrimHandle{ handle, geom, prim_index });
        };
        for (const auto& submitted : peek_submission_buffer<SceneViewProxy_Mesh>())
        {
            if (submitted.skinning || submitted.render_id.identifier >= meshes.size())
                continue;
            const auto& ro = meshes[submitted.render_id.identifier];
            const bool changed = ro.primitives.empty() || ro.mesh != submitted.mesh || ro.lods != submitted.lods
                || ro.shadow_proxies != submitted.shadow_proxies;
            if (!ro.alive || ro.generation != submitted.render_id.generation || !changed)
                continue;
            add_mesh(submitted.mesh);
            for (const MeshLod& lod : submitted.lods)
                add_mesh(lod.mesh);
            for (const ShadowProxy& proxy : submitted.shadow_proxies)
                add_mesh(proxy.mesh);
        }
        if (!new_primitives.empty())
            renderer.get_backend()->prepare_mesh_buffers(new_primitives);
    }

    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Mesh>())
    {
        
        auto& ro = meshes[submitted.render_id.identifier];

        // submitted before its proxy was unregistered (in the same frame), or by a previous owner of the slot
        if (!ro.alive || ro.generation != submitted.render_id.generation)
            continue;

        dirty = true;

        bool is_new = ro.primitives.empty();
        // the materials (set_instance_texture) and the dissolving variant are baked into the primitives too
        const bool dissolving = submitted.tint.a > 0.0f;
        bool mesh_changed = ro.mesh != submitted.mesh || ro.lods != submitted.lods
            || ro.shadow_proxies != submitted.shadow_proxies || ro.materials != submitted.materials
            || ro.dissolving != dissolving;

        glm::mat4 new_world = submitted.transform.matrix();
        bool transform_changed = ro.world != new_world;

        if (is_new || mesh_changed)
        {
            retire_primitives(ro);

            ro.mesh   = submitted.mesh;
            ro.lods   = submitted.lods;
            ro.shadow_proxies = submitted.shadow_proxies;
            ro.materials = submitted.materials;
            ro.dissolving = dissolving;
            ro.world  = new_world;
            ro.bounds = submitted.bounds;
            ro.effect = submitted.effect;
            ro.tint = submitted.tint;
            ro.dissolve_edge = submitted.dissolve_edge;

            checkf(submitted.lods.empty() || !submitted.skinning, "Mesh '%s': skinned meshes have no LODs",
                submitted.debug_name.to_string().c_str());

            // level 0: the mesh itself, level n: lods[n - 1]. One set of primitives per level, the view picks
            // by distance (RenderPrimitive::in_lod_range)
            for (uint32_t level = 0; level <= submitted.lods.size(); ++level)
            {
                const MeshHandle level_mesh = level == 0 ? submitted.mesh : submitted.lods[level - 1].mesh;
                const auto& mesh = level_mesh.get();

                for (uint32_t geom = 0; geom < mesh.mesh_geometry.size(); ++geom)
                {
                    const auto& geometry = mesh.mesh_geometry[geom];

                    for (uint32_t prim_index = 0;
                         prim_index < geometry.primitives.size();
                         ++prim_index)
                    {
                        uint32_t mat_index =
                            geometry.primitives[prim_index]
                                .material_index.value_or(0);
                    
                        checkf(mat_index < submitted.materials.size(),
                            "Mesh '%s': no material for slot %u (%zu provided)",
                            submitted.debug_name.to_string().c_str(), mat_index, submitted.materials.size());
                    
                        auto material = submitted.materials[mat_index];
                    
                        // explicitly hidden material (e.g. fully transparent UE materials)
                        if (auto visible_it = material->parameters.find("visible");
                            visible_it != material->parameters.end() &&
                            visible_it->second.is<float>() && visible_it->second.as<float>() == 0.0f)
                        {
                            continue;
                        }
                    
                        auto blend_mode = material->get_enum_parameter<BlendMode>("blend_mode");

                        auto model =
                            renderer.find_model(material->model);
                        checkf(model, "Material model '%s' not found", material->model.to_string().c_str());
                    
                        const bool masked_in_base_pass = model->supports_masked.value_or(false);

                        RenderPrimitive rp{};
                        rp.mesh = MeshPrimHandle{
                            level_mesh, geom, prim_index };
                        rp.world = &ro.world;
                        rp.bounds = ro.bounds;
                        rp.lod_level = level;
                        rp.masked = blend_mode == BlendMode::masked;

                        std::set<Name> passes;
        
                        if (blend_mode == BlendMode::opaque ||
                            (blend_mode == BlendMode::masked && masked_in_base_pass))
                        {
                            passes.emplace("GeometryBase");
                            // passes.emplace("DepthPrepass");
                        }
                        else if (blend_mode == BlendMode::translucent)
                             passes.emplace("GeometryTranslucent");
                    
                        if (blend_mode != BlendMode::translucent)
                            passes.emplace("ShadowMap");
                    
                        for (Name pass_name : passes)
                        {
                        
                            auto instance =
                                renderer.query_material_instance(
                                    submitted.materials[mat_index], pass_name);
                        
                            auto geom_pipeline_family =
                                renderer.query_pipeline_family(pass_name, model);

                            rp.passes.emplace(pass_name);
                            auto& info = rp.info_by_pass[pass_name];
                            info.pipeline_family = geom_pipeline_family;
                            // INSTANCE_DISSOLVE: the variant that discards (models without it ignore the constant)
                            info.shader_key = geom_pipeline_family->make_shader_key(pass_name, instance->material, {},
                                dissolving ? std::map<Name, uint32_t>{ { Name("INSTANCE_DISSOLVE"), 1u } } : std::map<Name, uint32_t>{});
                            info.pipeline = geom_pipeline_family->request_pipeline(info.shader_key);
                            info.material_instance = instance;
                            if (pass_name != "shadowmap")
                                instance->apply_material_parameters();
                    
                    
                            info.material_index = instance->material_id;
                        }
                    
                        if (submitted.skinning)
                        {
                            // per-instance skinned copy with its own vertices and BLAS
                            const SkeletalMesh& skeletal = submitted.skinning->mesh.get();
                            checkf(geom == 0, "Skeletal meshes have a single geometry");
                        
                            rp.skinned = renderer.get_backend()->create_skinned_mesh(
                                rp.mesh,
                                skeletal.primitive_skins[prim_index],
                                skeletal.skeleton.num_bones(),
                                skeletal.primitive_morphs[prim_index],
                                skeletal.num_morph_targets(),
                                rt_build_mode);
                            rp.skinning = submitted.skinning;
                            rp.mesh_index = rp.skinned->mesh_index;
                        }
                        else
                        {
                            auto result = renderer.get_backend()->get_or_create_mesh_buffers(rp.mesh, rt_build_mode);
                            rp.mesh_index = result.mesh_index;
                        }
                        rp.indices = renderer.get_backend()->get_mesh_index_range((uint32_t)rp.mesh_index);

                        // simplified stand-ins in the shadow maps; kept only when they save triangles
                        if (level == 0 && !submitted.skinning && rp.passes.contains("ShadowMap"))
                        {
                            for (const ShadowProxy& proxy : submitted.shadow_proxies)
                            {
                                const MeshPrimHandle proxy_prim{ proxy.mesh, geom, prim_index };
                                const Primitive& simplified = proxy_prim.get();
                                RenderPrimitive::ShadowProxyDraw draw{ .error = proxy.error };
                                if (!simplified.indices.empty())
                                {
                                    if (simplified.indices.size() * 5 > rp.indices.index_count * 4)
                                        continue;
                                    draw.mesh_index = (uint32_t)renderer.get_backend()->get_or_create_mesh_buffers(
                                        proxy_prim, RTBuildMode::none).mesh_index;
                                    draw.indices = renderer.get_backend()->get_mesh_index_range(draw.mesh_index);
                                }
                                rp.shadow_proxies.push_back(draw);
                            }
                        }
                        
                        // todo: temp. exact pass is bad idea
                        auto mat_instance_TODO_EXACT_PASS =
                                renderer.query_material_instance(
                                    submitted.materials[mat_index], "GeometryBase");
                    
                        // shadow instances are never filled with parameters (see above):
                        // shadow pipelines which alpha test read the base pass material
                        if (auto shadow_it = rp.info_by_pass.find("ShadowMap"); shadow_it != rp.info_by_pass.end())
                            shadow_it->second.material_index = mat_instance_TODO_EXACT_PASS->material_id;
                        
                        rp.primitive_material_id = mat_instance_TODO_EXACT_PASS->material_id;
                        rp.primitive_material = mat_instance_TODO_EXACT_PASS;

                        const RenderPrimitiveId slot = add_primitive(std::move(rp));

                        ro.prev_world = ro.world;
                        write_primitive_info(ro, primitives[slot]);


                        ro.primitives.push_back(slot);
                    }
                }
            }
            update_lod_ranges(ro);

            continue;
        }

        // the per instance effect (hit flash), tint, dissolve share and edge: only the primitive table entries (with
        // the transform below if it moved too)
        if (ro.effect != submitted.effect || ro.tint != submitted.tint || ro.dissolve_edge != submitted.dissolve_edge)
        {
            ro.effect = submitted.effect;
            ro.tint = submitted.tint;
            ro.dissolve_edge = submitted.dissolve_edge;
            if (!transform_changed)
                for (RenderPrimitiveId prim_index : ro.primitives)
                    write_primitive_info(ro, primitives[prim_index]);
        }

        if (transform_changed)
        {
            // moving meshes: primitive table holds the transforms used by every pass
            ro.prev_world = ro.world;
            ro.world = new_world;
            // proxy bounds are computed once at registration: recompute for the new transform
            // (otherwise frustum culling tests the spawn position)
            ro.bounds = submitted.mesh.get().bounds * submitted.transform;
            ro.moved = true;
            update_lod_ranges(ro);
            
            for (RenderPrimitiveId prim_index : ro.primitives)
            {
                RenderPrimitive& rp = primitives[prim_index];
                rp.bounds = ro.bounds;
                write_primitive_info(ro, rp);
            }
            
            moved_this_frame.push_back(submitted.render_id.identifier);
        }

    }
    
    // objects which stopped: previous transform catches up so motion vectors become zero
    for (uint32_t mesh_id : moved_last_frame)
    {
        auto& ro = meshes[mesh_id];
        if (std::find(moved_this_frame.begin(), moved_this_frame.end(), mesh_id) != moved_this_frame.end())
            continue;
        ro.prev_world = ro.world;
        ro.moved = false;
        for (RenderPrimitiveId prim_index : ro.primitives)
            write_primitive_info(ro, primitives[prim_index]);
    }
    moved_last_frame = std::move(moved_this_frame);
    moved_this_frame.clear();
    
    // TLAS instance transforms follow moved meshes
    if (!moved_last_frame.empty() && render_settings::enable_raytracing)
        dirty = true;
}

void SceneViewProcessor_Mesh::update_lod_ranges(RenderObject_Mesh& ro)
{
    if (ro.lods.empty())
        return;

    const float scale = std::max({ glm::length(glm::vec3(ro.world[0])), glm::length(glm::vec3(ro.world[1])),
                                   glm::length(glm::vec3(ro.world[2])) });
    for (RenderPrimitiveId prim_index : ro.primitives)
    {
        RenderPrimitive& rp = primitives[prim_index];
        rp.lod_min = rp.lod_level == 0 ? 0.0f : ro.lods[rp.lod_level - 1].distance * scale;
        rp.lod_max = rp.lod_level < ro.lods.size() ? ro.lods[rp.lod_level].distance * scale
                                                   : std::numeric_limits<float>::max();

        rp.shadow_lod_min = rp.lod_min;
        rp.shadow_lod_max = rp.lod_max;
        if (rp.masked)
        {
            // level n casts over the range of level n - 1, the last level over its own as well
            const bool last = rp.lod_level == ro.lods.size();
            rp.shadow_lod_min = rp.lod_level <= 1 ? 0.0f : ro.lods[rp.lod_level - 2].distance * scale;
            rp.shadow_lod_max = rp.lod_level == 0 ? 0.0f : last ? std::numeric_limits<float>::max() : rp.lod_min;
        }
    }
}

void SceneViewProcessor_Mesh::write_primitive_info(const RenderObject_Mesh& ro, const RenderPrimitive& rp)
{
    if (primitive_table_cpu.size() <= rp.id)
        primitive_table_cpu.resize(rp.id + 1, GPUPrimitiveInfo{ glm::mat4(1.0f), glm::mat4(1.0f), 0, 0 });
    primitive_table_cpu[rp.id] = GPUPrimitiveInfo {
        ro.world,
        ro.prev_world,
        (uint32_t)rp.mesh_index,
        rp.primitive_material_id,
        { 0u, 0u },
        ro.effect,
        ro.tint,
        ro.dissolve_edge
    };
    ++primitive_table_version;
}

void SceneViewProcessor_Mesh::upload_primitive_table(RenderResource& primitive_table, RBFrameHandle frame)
{
    if (primitive_table_uploaded_version.size() <= frame)
        primitive_table_uploaded_version.resize(frame + 1, 0);
    if (primitive_table_uploaded_version[frame] == primitive_table_version || primitive_table_cpu.empty())
        return;

    primitive_table.update_ssbo("u_primitive_table", primitive_table_cpu.size() * sizeof(GPUPrimitiveInfo),
        primitive_table_cpu.data(), frame);
    primitive_table_uploaded_version[frame] = primitive_table_version;
}

static float compute_view_depth(
    const glm::mat4& view,
    const glm::mat4& world)
{
    glm::vec3 world_pos = glm::vec3(world[3]);

    glm::vec4 view_pos = view * glm::vec4(world_pos, 1.0f);

    return -view_pos.z;
}

static uint64_t build_sort_key_opaque(
    uint32_t pipeline_id,
    uint32_t material_id,
    float depth)
{
    // depth bucket (0 .. 65535)
    uint32_t depth_bucket =
        std::min(65535u, uint32_t(depth * 100.0f));

    uint64_t key = 0;

    key |= uint64_t(pipeline_id) << 48;
    key |= uint64_t(material_id) << 32;
    key |= uint64_t(depth_bucket);

    return key;
}

static uint64_t build_sort_key_translucent(float depth)
{
    uint32_t depth_bucket =
        std::min(65535u, uint32_t(depth * 100.0f));

    depth_bucket = 65535u - depth_bucket;

    return uint64_t(depth_bucket);
}



void SceneViewProcessor_Mesh::gather_for_view(const glm::mat4& view_matrix, const Frustum& frustum, Name pass_name,
    std::vector<ViewRenderItem>& out_items)
{
    PROFILE(__FUNCTION__);
    out_items.clear();
    out_items.reserve(primitives.size());

    // the LOD is picked by the distance to the origin of the view
    const glm::vec3 viewer = glm::vec3(glm::inverse(view_matrix)[3]);
    

    for (const auto& prim : primitives)
    {
        // Frustum culling
        if (!prim.in_lod_range(viewer) || !frustum.test_aabb_world(prim.bounds))
            continue;
        
        auto info_it = prim.info_by_pass.find(pass_name);
        
        if (info_it == prim.info_by_pass.end())
            continue;

        auto pipeline_family = info_it->second.pipeline_family;
        auto material_instancce = info_it->second.material_instance;

        float depth =
            compute_view_depth(view_matrix, *prim.world);

        uint64_t key = 0;

        if (pass_name == "GeometryBase" || pass_name == "DepthPrepass")
        {
            key = build_sort_key_opaque(
                pipeline_family->unique_id,
                material_instancce->material_id,
                depth);
        }
        else if (pass_name == "GeometryTranslucent")
        {
            key = build_sort_key_translucent(depth);
        }
        else if (pass_name == "shadowmap")
        {
            key = uint64_t(pipeline_family->unique_id);
        }

        out_items.push_back({ &prim, key });
    }
}

void SceneViewProcessor_Mesh::on_hot_reload()
{
    SceneViewProcessor::on_hot_reload();
    
    for (auto& prim : primitives)
    {
        for (auto& [_, info] : prim.info_by_pass)
        {
            info.pipeline = info.pipeline_family->request_pipeline(info.shader_key);
        }
    }
}
