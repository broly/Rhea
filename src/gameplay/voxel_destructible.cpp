module;

#include <json/value.h>

module gameplay;

import :voxel_destructible;
import :weapons;
import :health;

import std.compat;
import fixed_string;
import glm;
import rhmath;
import reflect;
import rhobject;
import name;
import ecs;
import framework;
import assets;
import texture_format;
import rhcomponents;
import voxel;
import cvar;
import paths;
import json_utils;
import render;
import engine;
import globals;
import log;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"
#include "logging/log_macro.h"

DEFINE_LOGGER(LogVoxelDestructible, Log);

namespace
{
    // bump when the voxelizer or its inputs change meaning: cached grids are rebuilt
    constexpr uint32_t voxel_copy_version = 2;

    // console commands for the next Late: voxelize everything / back to the meshes
    std::atomic<bool> voxelize_all_requested = false;
    std::atomic<bool> restore_all_requested = false;

    cvar::Command cmd_voxelize("voxel.voxelize", "Turns every VoxelDestructible with a voxel copy into it",
        [] (cvar::Args) { voxelize_all_requested = true; });
    cvar::Command cmd_restore("voxel.restore", "Turns every voxelized VoxelDestructible back into its mesh",
        [] (cvar::Args) { restore_all_requested = true; });

    // What the worker thread needs: copies, the asset manager is not thread safe
    struct BuildInput
    {
        struct Primitive
        {
            std::vector<glm::vec3> positions;
            std::vector<glm::vec2> uvs;
            std::vector<uint32_t> indices;
            uint32_t material = 0;
        };
        struct Material
        {
            std::string base_color;         // texture path under assets/, empty: none
            float factor = 1.0f;
            bool skip = false;
        };
        std::vector<Primitive> primitives;
        std::vector<Material> materials;
        voxel::VoxelizeSettings settings;
        uint32_t texels_per_voxel = 4;
        uint32_t max_atlas = 2048;
        std::string cache_key;
        uint64_t hash = 0;
    };

    // atlases larger than render.textures.max_size are shrunk on upload: they must stay within it
    uint32_t max_atlas_size()
    {
        const uint32_t limit = get_texture_max_size();
        return limit == 0 ? 4096u : std::min(limit, 4096u);
    }

    // The path of a material texture: pending ones carry it, loaded ones are found by their handle
    std::string texture_path(const TextureHandle& handle)
    {
        if (handle.pending_path)
            return *handle.pending_path;
        if (!handle.is_valid())
            return {};
        AssetManager& assets = AssetManager::get();
        std::scoped_lock lock(assets.mutex);
        for (const auto& [path, found] : assets.texture_by_path)
            if (found.id == handle.id && !path.contains('#'))
                return path;
        return {};
    }

    std::optional<BuildInput> gather_input(const MeshRenderer& renderer, const VoxelDestructible& destructible,
                                           std::string& error)
    {
        const StaticMesh& mesh = renderer.mesh.get();
        BuildInput input;
        input.settings = { .voxel_size = destructible.voxel_size, .fill_thickness = destructible.fill_thickness };
        input.texels_per_voxel = destructible.texels_per_voxel;
        input.max_atlas = max_atlas_size();

        uint64_t hash = voxel::hash_value(voxel::hash_seed, voxel_copy_version);
        hash = voxel::hash_value(hash, destructible.voxel_size);
        hash = voxel::hash_value(hash, destructible.fill_thickness);

        for (const std::shared_ptr<Material>& material : renderer.materials)
        {
            BuildInput::Material& out = input.materials.emplace_back();
            if (!material)
                continue;
            if (auto blend = material->parameters.find(Name("blend_mode"));
                blend != material->parameters.end() && blend->second.is<Name>() && blend->second.as<Name>() == Name("translucent"))
                out.skip = true;
            if (auto color = material->parameters.find(Name("base_color"));
                color != material->parameters.end() && color->second.is<TextureHandle>())
                out.base_color = texture_path(color->second.as<TextureHandle>());
            if (auto factor = material->parameters.find(Name("base_color_factor"));
                factor != material->parameters.end() && factor->second.is<float>())
                out.factor = factor->second.as<float>();

            hash = voxel::hash_string(hash, out.base_color);
            hash = voxel::hash_value(hash, out.factor);
            hash = voxel::hash_value(hash, uint8_t(out.skip));
            // a changed texture rebuilds too
            std::error_code ec;
            const std::filesystem::path file = paths::get_assets_path() / out.base_color;
            if (!out.base_color.empty() && std::filesystem::exists(file, ec))
            {
                hash = voxel::hash_value(hash, uint64_t(std::filesystem::file_size(file, ec)));
                hash = voxel::hash_value(hash, int64_t(std::filesystem::last_write_time(file, ec).time_since_epoch().count()));
            }
        }

        for (const Geometry& geometry : mesh.mesh_geometry)
        {
            for (const Primitive& primitive : geometry.primitives)
            {
                BuildInput::Primitive& out = input.primitives.emplace_back();
                out.material = primitive.material_index.value_or(0);
                out.positions.reserve(primitive.vertices.size());
                out.uvs.reserve(primitive.vertices.size());
                for (const Vertex& vertex : primitive.vertices)
                {
                    out.positions.push_back(vertex.position);
                    out.uvs.push_back(vertex.tex_coord);
                }
                out.indices = primitive.indices;
                hash = voxel::hash_bytes(hash, out.positions.data(), out.positions.size() * sizeof(glm::vec3));
                hash = voxel::hash_bytes(hash, out.uvs.data(), out.uvs.size() * sizeof(glm::vec2));
                hash = voxel::hash_bytes(hash, out.indices.data(), out.indices.size() * sizeof(uint32_t));
                hash = voxel::hash_value(hash, out.material);
            }
        }
        if (input.primitives.empty())
        {
            error = "the mesh has no primitives";
            return std::nullopt;
        }

        input.cache_key = std::format("{}_{}cm", mesh.name.empty() ? std::string("mesh") : mesh.name,
                                      std::lround(destructible.voxel_size * 100.0f));
        input.hash = hash;
        return input;
    }

    std::optional<voxel::Image> load_image(const std::string& path)
    {
        std::optional<Texture> texture = Texture::create_from_file(paths::get_assets_path() / path);
        if (!texture || texture->bulk.size() != size_t(texture->extent.width) * texture->extent.height * 4)
            return std::nullopt;
        voxel::Image image{ texture->extent.width, texture->extent.height, {} };
        image.texels.resize(size_t(image.width) * image.height);
        std::memcpy(image.texels.data(), texture->bulk.data(), texture->bulk.size());
        return image;
    }

    // ---- chunks ----

    constexpr int32_t chunk_size = VoxelDestructible::chunk_size;

    int32_t floor_div(int32_t v, int32_t d) { return v >= 0 ? v / d : -((-v + d - 1) / d); }

    using ChunkKey = std::tuple<int32_t, int32_t, int32_t>;
    ChunkKey key_of(glm::ivec3 c) { return { c.x, c.y, c.z }; }

    voxel::GridBounds chunk_range(glm::ivec3 coord)
    {
        return { coord * chunk_size, coord * chunk_size + (chunk_size - 1) };
    }

    // the chunks with solid voxels, in a fixed order
    std::vector<glm::ivec3> solid_chunks(const voxel::VoxelGrid& grid)
    {
        std::set<ChunkKey> keys;
        for (const voxel::Brick& brick : grid.get_bricks())
            if (brick.solid_count > 0)
            {
                const glm::ivec3 first = brick.coord * voxel::brick_size;
                keys.insert({ floor_div(first.x, chunk_size), floor_div(first.y, chunk_size), floor_div(first.z, chunk_size) });
            }
        std::vector<glm::ivec3> result;
        for (const auto& [x, y, z] : keys)
            result.push_back({ x, y, z });
        return result;
    }

    // A chunk's surface mesh: into an atlas of `fixed` size (x > 0: the texture it already has), else the smallest
    // within max_atlas, then grown to about twice the area (while it stays within max_atlas) so the faces the
    // hits add still fit
    std::optional<voxel::SurfaceMesh> mesh_chunk(const voxel::VoxelGrid& grid, glm::ivec3 coord, uint32_t texels,
                                                 uint32_t max_atlas, glm::uvec2 fixed, std::string& error)
    {
        voxel::SurfaceMeshSettings settings{ .texels_per_voxel = texels, .max_atlas_size = max_atlas };
        if (fixed.x > 0 && fixed.y > 0)
        {
            settings.atlas_width = fixed.x;
            settings.atlas_height = fixed.y;
            return voxel::build_surface_mesh(grid, chunk_range(coord), settings, &error);
        }
        std::optional<voxel::SurfaceMesh> mesh = voxel::build_surface_mesh(grid, chunk_range(coord), settings, &error);
        if (!mesh || mesh->empty())
            return mesh;
        uint32_t width = mesh->atlas_width, height = mesh->atlas_height;
        if (height < width && height * 2 <= max_atlas)
            height *= 2;
        else if (width * 2 <= max_atlas)
            width *= 2;
        else if (height * 2 <= max_atlas)
            height *= 2;
        if (width == mesh->atlas_width && height == mesh->atlas_height)
            return mesh;
        settings.texels_per_voxel = mesh->texels_per_voxel;
        settings.atlas_width = width;
        settings.atlas_height = height;
        std::string roomy_error;
        if (std::optional<voxel::SurfaceMesh> roomy = voxel::build_surface_mesh(grid, chunk_range(coord), settings, &roomy_error))
            return roomy;
        return mesh;
    }

    // worker thread
    void build_voxel_copy(const BuildInput& input, VoxelCopyBuild& build)
    {
        const voxel::VoxelCache cache(paths::get_cache_path() / "voxels");
        auto start = std::chrono::steady_clock::now();
        build.grid = cache.load(input.cache_key, input.hash);
        build.cached = build.grid.has_value();
        if (!build.grid)
        {
            std::vector<std::optional<voxel::Image>> images;
            images.reserve(input.materials.size());
            std::vector<voxel::VoxelizeMaterial> materials;
            for (const BuildInput::Material& material : input.materials)
            {
                std::optional<voxel::Image>& image = images.emplace_back();
                if (!material.skip && !material.base_color.empty())
                {
                    image = load_image(material.base_color);
                    if (!image)
                        LogVoxelDestructible.Log<Warning>("Voxel copy '%s': texture '%s' not loaded, flat color",
                            input.cache_key.c_str(), material.base_color.c_str());
                }
                materials.push_back({
                    .base_color = image ? &*image : nullptr,
                    .color = glm::vec4(glm::vec3(std::clamp(material.factor, 0.0f, 1.0f)), 1.0f),
                    .skip = material.skip,
                });
            }
            std::vector<voxel::VoxelizePrimitive> primitives;
            for (const BuildInput::Primitive& primitive : input.primitives)
                primitives.push_back({ primitive.positions, primitive.uvs, primitive.indices, primitive.material });

            voxel::VoxelizeStats stats;
            build.grid = voxel::voxelize(primitives, materials, input.settings, &stats, &build.error);
            if (!build.grid)
                return;
            LogVoxelDestructible.Log("Voxelized '%s': %llu triangles -> %llu surface + %llu filled voxels (%d x %d x %d) in %.0f ms",
                input.cache_key.c_str(), (unsigned long long)stats.triangles, (unsigned long long)stats.surface_voxels,
                (unsigned long long)stats.filled_voxels, stats.size.x, stats.size.y, stats.size.z, stats.milliseconds);
            std::string error;
            if (!cache.store(input.cache_key, input.hash, *build.grid, &error))
                LogVoxelDestructible.Log<Warning>("Voxel copy '%s' not cached: %s", input.cache_key.c_str(), error.c_str());
        }
        build.voxelize_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        start = std::chrono::steady_clock::now();
        for (const glm::ivec3& coord : solid_chunks(*build.grid))
        {
            std::string error;
            std::optional<voxel::SurfaceMesh> mesh = mesh_chunk(*build.grid, coord, input.texels_per_voxel, input.max_atlas,
                                                                glm::uvec2(0), error);
            if (!mesh)
            {
                LogVoxelDestructible.Log<Error>("Voxel copy '%s': chunk %d %d %d not meshed: %s", input.cache_key.c_str(),
                    coord.x, coord.y, coord.z, error.c_str());
                continue;
            }
            if (!mesh->empty())
                build.chunks.push_back({ coord, std::move(*mesh) });
        }
        build.mesh_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    }

    // The surface mesh as a render mesh: clockwise like every render mesh (the glTF importer flips the winding)
    StaticMesh to_static_mesh(const voxel::SurfaceMesh& surface, std::string name)
    {
        Primitive primitive;
        primitive.material_index = 0;
        primitive.vertices.resize(surface.positions.size());
        glm::vec3 lo(std::numeric_limits<float>::max()), hi(std::numeric_limits<float>::lowest());
        for (size_t i = 0; i < surface.positions.size(); ++i)
        {
            Vertex& vertex = primitive.vertices[i];
            vertex.position = surface.positions[i];
            vertex.normal = surface.normals[i];
            vertex.tex_coord = surface.uvs[i];
            vertex.tangent = surface.tangents[i];
            lo = glm::min(lo, surface.positions[i]);
            hi = glm::max(hi, surface.positions[i]);
        }
        primitive.indices = surface.indices;
        for (size_t i = 0; i + 2 < primitive.indices.size(); i += 3)
            std::swap(primitive.indices[i], primitive.indices[i + 1]);

        StaticMesh mesh;
        mesh.name = std::move(name);
        mesh.bounds = AABB(lo, hi);
        mesh.mesh_geometry.push_back(Geometry{ .primitives = {} });
        mesh.mesh_geometry[0].primitives.push_back(std::move(primitive));
        mesh.material_names.push_back("voxels");
        return mesh;
    }

    std::shared_ptr<Material> make_voxel_material(TextureHandle atlas)
    {
        std::shared_ptr<Material> material = new_object<Material>();
        material->model = "pbr";
        material->parameters["blend_mode"] = Name("opaque");
        material->parameters["base_color"] = atlas;
        material->parameters["base_color_factor"] = 1.0f;
        material->parameters["emissive_factor"] = 0.0f;
        material->parameters["roughness_factor"] = 0.85f;
        material->parameters["metallic_factor"] = 0.0f;
        material->parameters["occlusion_factor"] = 1.0f;
        return material;
    }

    uint32_t mix(uint32_t h)
    {
        h ^= h >> 16;
        h *= 0x7feb352du;
        h ^= h >> 15;
        h *= 0x846ca68bu;
        h ^= h >> 16;
        return h;
    }

    // A preset of assets/jpeg/presets.json, section "voxels" (read at every hit: they are rare, edits apply at
    // once). radius: m, 0 = the weapon's.
    struct VoxelPreset
    {
        voxel::VoxelJpegSettings settings;
        float radius = 0.0f;
    };

    std::optional<VoxelPreset> load_voxel_preset(const std::string& name)
    {
        const std::optional<Json::Value> root = json_utils::load_json_asset("jpeg/presets.json");
        if (!root || !(*root)["voxels"].isObject() || !(*root)["voxels"][name].isObject())
            return std::nullopt;
        const Json::Value& json = (*root)["voxels"][name];
        VoxelPreset preset;
        voxel::VoxelJpegSettings& s = preset.settings;
        auto integer = [&] (const char* key, int32_t& value, int32_t lo, int32_t hi)
        {
            if (json[key].isNumeric())
                value = std::clamp(json[key].asInt(), lo, hi);
        };
        integer("quality", s.quality, 1, 100);
        integer("density_quality", s.density_quality, 1, 100);
        integer("density_scale", s.density_scale, 1, 100000);
        integer("density_noise", s.density_noise, 0, 255);
        integer("downscale", s.downscale, 1, 2);
        integer("generations", s.generations, 1, 16);
        integer("sharpen", s.sharpen, 0, 1000);
        integer("key_floor", s.key_floor, 0, 255);
        integer("key_keep", s.key_keep, 0, 255);
        integer("key_appear", s.key_appear, 0, 256);
        integer("holes", s.holes, 0, 100);
        integer("holes_falloff", s.holes_falloff, 1, 8);
        if (json["holes_before"].isBool())
            s.holes_before = json["holes_before"].asBool();
        int32_t erase = s.erase_below;
        integer("erase_below", erase, 0, 255);
        s.erase_below = uint8_t(erase);
        if (json["chroma"].isString())
            s.chroma = json["chroma"].asString() == "4:4:4" ? voxel::JpegChroma::full : voxel::JpegChroma::half;
        if (json["radius"].isNumeric())
            preset.radius = std::max(json["radius"].asFloat(), 0.0f);
        return preset;
    }

    // voxel.hit: world positions queued for the next Late
    std::mutex console_hits_mutex;
    std::vector<std::pair<glm::vec3, std::string>> console_hits;

    cvar::Command cmd_hit("voxel.hit", "Recompresses the voxelized VoxelDestructibles around a world position (3D JPEG)",
        [] (cvar::Args args)
        {
            glm::vec3 p;
            for (size_t i = 0; i < 3; ++i)
                if (args.size() < 3 || std::from_chars(args[i].data(), args[i].data() + args[i].size(), p[int(i)]).ec != std::errc{})
                {
                    cvar::print("voxel.hit <x> <y> <z> [preset]", cvar::Output::error);
                    return;
                }
            std::scoped_lock lock(console_hits_mutex);
            console_hits.emplace_back(p, args.size() > 3 ? std::string(args[3]) : std::string("house_cannon"));
        }, "<x> <y> <z> [preset]");

    // worker thread: the hits in order on a copy of the grid, then the chunks they changed (one voxel around the
    // changed blocks: faces on a chunk border belong to the neighbour), into their atlases' sizes when they fit
    void apply_voxel_hits(voxel::VoxelGrid grid, std::vector<VoxelHit> hits, std::map<ChunkKey, glm::uvec2> atlas_sizes,
                          uint32_t texels, uint32_t max_atlas, VoxelCopyBuild& build)
    {
        auto start = std::chrono::steady_clock::now();
        voxel::GridBounds changed;
        for (const VoxelHit& hit : hits)
        {
            const voxel::VoxelJpegStats stats = voxel::recompress(grid, hit.center, hit.radius, hit.settings, hit.seed);
            build.stats.blocks += stats.blocks;
            build.stats.solid_before += stats.solid_before;
            build.stats.solid_after += stats.solid_after;
            build.stats.holes += stats.holes;
            if (stats.changed.is_empty())
                continue;
            changed = changed.is_empty() ? stats.changed
                : voxel::GridBounds{ glm::min(changed.min, stats.changed.min), glm::max(changed.max, stats.changed.max) };
        }
        build.hits = uint32_t(hits.size());
        build.stats.changed = changed;
        build.voxelize_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();

        start = std::chrono::steady_clock::now();
        if (!changed.is_empty())
        {
            const glm::ivec3 first(floor_div(changed.min.x - 1, chunk_size), floor_div(changed.min.y - 1, chunk_size),
                                   floor_div(changed.min.z - 1, chunk_size));
            const glm::ivec3 last(floor_div(changed.max.x + 1, chunk_size), floor_div(changed.max.y + 1, chunk_size),
                                  floor_div(changed.max.z + 1, chunk_size));
            for (int32_t z = first.z; z <= last.z; ++z)
                for (int32_t y = first.y; y <= last.y; ++y)
                    for (int32_t x = first.x; x <= last.x; ++x)
                    {
                        const glm::ivec3 coord(x, y, z);
                        const auto existing = atlas_sizes.find(key_of(coord));
                        std::string error;
                        std::optional<voxel::SurfaceMesh> mesh;
                        if (existing != atlas_sizes.end())
                            mesh = mesh_chunk(grid, coord, texels, max_atlas, existing->second, error);
                        // a new chunk, or one that outgrew its atlas: a new texture
                        if (!mesh)
                            mesh = mesh_chunk(grid, coord, texels, max_atlas, glm::uvec2(0), error);
                        if (!mesh)
                        {
                            LogVoxelDestructible.Log<Error>("Voxel hits: chunk %d %d %d not meshed: %s", x, y, z, error.c_str());
                            continue;
                        }
                        // empty and never drawn: nothing to tell
                        if (mesh->empty() && existing == atlas_sizes.end())
                            continue;
                        build.chunks.push_back({ coord, std::move(*mesh) });
                    }
        }
        build.mesh_ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        build.grid = std::move(grid);
    }

    TextureHandle register_atlas(const voxel::SurfaceMesh& surface, const std::string& name, ecs::Entity owner, glm::ivec3 coord)
    {
        static uint32_t serial = 0;
        Texture atlas;
        atlas.name = name + "_atlas";
        // not a file: a unique key no load_texture finds
        atlas.transition_path = std::format("{}#voxels{}_{}_{}_{}_{}", atlas.name, owner.bits(), coord.x, coord.y, coord.z, ++serial);
        atlas.extent = Extent(surface.atlas_width, surface.atlas_height);
        atlas.format = TextureFormat::RGBA8;
        atlas.bulk.resize(surface.atlas.size() * sizeof(uint32_t));
        std::memcpy(atlas.bulk.data(), surface.atlas.data(), atlas.bulk.size());
        return AssetManager::get().register_external_texture(std::move(atlas));
    }

    // a chunk's mesh and atlas into its child (spawned when new): the atlas updated in place when the size is the
    // texture's, a new texture otherwise
    void apply_chunk(ecs::Entity e, VoxelDestructible& destructible, const std::string& name, VoxelChunkMesh& built,
                     ecs::Query<MeshRenderer>& renderers, ecs::Commands& commands)
    {
        AssetManager& assets = AssetManager::get();
        const std::string chunk_name = std::format("{}_voxels_{}_{}_{}", name, built.coord.x, built.coord.y, built.coord.z);
        const bool shown = destructible.state == VoxelCopyState::voxelized;
        const voxel::SurfaceMesh& surface = built.mesh;

        auto found = std::ranges::find_if(destructible.chunks, [&] (const VoxelChunk& c) { return c.coord == built.coord; });
        if (found == destructible.chunks.end())
        {
            if (surface.empty())
                return;
            VoxelChunk chunk{ .coord = built.coord, .quads = surface.quads };
            chunk.atlas = register_atlas(surface, chunk_name, e, built.coord);
            chunk.mesh = assets.store_mesh(to_static_mesh(surface, chunk_name));
            MeshRenderer renderer;
            renderer.mesh = chunk.mesh;
            renderer.materials.push_back(make_voxel_material(chunk.atlas));
            renderer.visible = shown;
            chunk.entity = commands.spawn(Name(chunk_name), Transform{}, ChildOf{ e }, std::move(renderer));
            destructible.chunks.push_back(chunk);
            return;
        }

        VoxelChunk& chunk = *found;
        chunk.quads = surface.quads;
        MeshRenderer* renderer = renderers.get(chunk.entity);
        if (!renderer)
            return;
        if (surface.empty())
        {
            renderer->visible = false;
            return;
        }
        Renderer& render = *RhGlobals::engine->renderer;
        Texture& atlas = assets.loaded_textures.at(chunk.atlas);
        if (atlas.extent.width == surface.atlas_width && atlas.extent.height == surface.atlas_height)
        {
            std::memcpy(atlas.bulk.data(), surface.atlas.data(), atlas.bulk.size());
            // waits for the GPU: the frames in flight read the image (as the terrain brushes)
            render.get_backend()->update_texture_2d(render.get_texture(chunk.atlas), atlas,
                TextureRegion{ .x = 0, .y = 0, .width = atlas.extent.width, .height = atlas.extent.height });
        }
        else
        {
            // the old material goes with the primitives that drew it (Renderer::collect_unused_materials)
            render.release_texture(chunk.atlas);
            chunk.atlas = register_atlas(surface, chunk_name, e, built.coord);
            renderer->materials = { make_voxel_material(chunk.atlas) };
        }
        render.release_mesh(chunk.mesh);
        chunk.mesh = assets.store_mesh(to_static_mesh(surface, chunk_name));
        renderer->mesh = chunk.mesh;
        renderer->visible = shown;
    }

    // the chunks' meshes and atlases with the copy (their entities are children: despawned already, or with it)
    [[=ecs::on_remove]]
    void release_voxel_copy(ecs::Registry&, ecs::Entity, VoxelDestructible& destructible)
    {
        if (!RhGlobals::engine || !RhGlobals::engine->renderer)
            return;
        Renderer& render = *RhGlobals::engine->renderer;
        for (const VoxelChunk& chunk : destructible.chunks)
        {
            render.release_mesh(chunk.mesh);
            render.release_texture(chunk.atlas);
        }
        destructible.chunks.clear();
    }

    void count_quads(VoxelDestructible& destructible)
    {
        destructible.quads = 0;
        destructible.chunk_count = 0;
        for (const VoxelChunk& chunk : destructible.chunks)
        {
            destructible.quads += chunk.quads;
            destructible.chunk_count += chunk.quads > 0 ? 1 : 0;
        }
    }

    // builds the voxel copies, adds them when built, recompresses them with the queued hits
    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<VoxelCopies>, =ecs::after<scene::TransformPropagation>,
        =ecs::before<RenderSync>]]
    void update_voxel_copies(ecs::Query<VoxelDestructible, const Name> destructibles, ecs::Query<MeshRenderer> renderers,
        ecs::Commands& commands)
    {
        destructibles.each([&] (ecs::Entity e, VoxelDestructible& destructible, const Name& name)
        {
            // ---- the first build ----
            if (destructible.state == VoxelCopyState::waiting)
            {
                const MeshRenderer* renderer = renderers.get(e);
                if (!renderer || !renderer->mesh.is_valid() || renderer->mesh.is_pending())
                    return;
                std::string error;
                std::optional<BuildInput> input = gather_input(*renderer, destructible, error);
                if (!input)
                {
                    LogVoxelDestructible.Log<Error>("Voxel copy of '%s': %s", name.to_string().c_str(), error.c_str());
                    destructible.state = VoxelCopyState::failed;
                    return;
                }
                auto build = std::make_shared<VoxelCopyBuild>();
                build->done = std::async(std::launch::async, [input = std::move(*input), build = build.get()]
                {
                    build_voxel_copy(input, *build);
                });
                destructible.build = std::move(build);
                destructible.state = VoxelCopyState::building;
                return;
            }

            // ---- hits waiting for a job ----
            if (!destructible.build)
            {
                if (destructible.state != VoxelCopyState::voxelized || destructible.pending_hits.empty() || !destructible.grid)
                    return;
                std::map<ChunkKey, glm::uvec2> atlas_sizes;
                for (const VoxelChunk& chunk : destructible.chunks)
                {
                    const Texture& atlas = AssetManager::get().loaded_textures.at(chunk.atlas);
                    atlas_sizes[key_of(chunk.coord)] = glm::uvec2(atlas.extent.width, atlas.extent.height);
                }
                auto build = std::make_shared<VoxelCopyBuild>();
                build->done = std::async(std::launch::async,
                    [grid = *destructible.grid, hits = std::move(destructible.pending_hits), atlas_sizes = std::move(atlas_sizes),
                     texels = destructible.texels_per_voxel, max_atlas = max_atlas_size(), build = build.get()] () mutable
                    {
                        apply_voxel_hits(std::move(grid), std::move(hits), std::move(atlas_sizes), texels, max_atlas, *build);
                    });
                destructible.pending_hits.clear();
                destructible.build = std::move(build);
                return;
            }

            if (destructible.build->done.wait_for(std::chrono::seconds(0)) != std::future_status::ready)
                return;
            const std::shared_ptr<VoxelCopyBuild> build = std::move(destructible.build);
            build->done.get();
            const std::string entity_name = name.to_string();
            const bool first = destructible.state == VoxelCopyState::building;

            if (!build->grid || (first && build->chunks.empty()))
            {
                LogVoxelDestructible.Log<Error>("Voxel %s of '%s' failed: %s", first ? "copy" : "hits", entity_name.c_str(),
                    build->error.empty() ? "no voxels" : build->error.c_str());
                if (first)
                    destructible.state = VoxelCopyState::failed;
                return;
            }

            destructible.grid = std::make_shared<voxel::VoxelGrid>(std::move(*build->grid));
            destructible.bricks = destructible.grid->get_brick_count();
            destructible.solid_voxels = destructible.grid->get_solid_count();
            if (first)
            {
                destructible.solid_voxels_initial = destructible.solid_voxels;
                destructible.state = VoxelCopyState::ready;
            }
            uint32_t largest_atlas = 0;
            for (VoxelChunkMesh& chunk : build->chunks)
            {
                largest_atlas = std::max({ largest_atlas, chunk.mesh.atlas_width, chunk.mesh.atlas_height });
                apply_chunk(e, destructible, entity_name, chunk, renderers, commands);
            }
            count_quads(destructible);

            if (first)
                LogVoxelDestructible.Log("Voxel copy of '%s': %u bricks, %llu voxels (%s, %.0f ms), %u chunks, %u quads, "
                    "atlases up to %u (%.0f ms)", entity_name.c_str(), destructible.bricks,
                    (unsigned long long)destructible.solid_voxels, build->cached ? "cache" : "voxelized", build->voxelize_ms,
                    destructible.chunk_count, destructible.quads, largest_atlas, build->mesh_ms);
            else
            {
                destructible.hits += build->hits;
                LogVoxelDestructible.Log("Voxel hits on '%s': %u (total %u), %u blocks, solid %llu -> %llu, %llu holes (%.1f ms), "
                    "%zu chunks rebuilt (%.1f ms), now %llu of %llu voxels, %u quads", entity_name.c_str(), build->hits,
                    destructible.hits, build->stats.blocks, (unsigned long long)build->stats.solid_before,
                    (unsigned long long)build->stats.solid_after, (unsigned long long)build->stats.holes, build->voxelize_ms, build->chunks.size(), build->mesh_ms,
                    (unsigned long long)destructible.solid_voxels, (unsigned long long)destructible.solid_voxels_initial,
                    destructible.quads);
            }
        });
    }

    void set_voxelized(ecs::Entity e, VoxelDestructible& destructible, bool voxelized, ecs::Query<MeshRenderer>& renderers)
    {
        if (MeshRenderer* mesh = renderers.get(e))
            mesh->visible = !voxelized;
        for (const VoxelChunk& chunk : destructible.chunks)
            if (MeshRenderer* renderer = renderers.get(chunk.entity))
                renderer->visible = voxelized && chunk.quads > 0;
        destructible.state = voxelized ? VoxelCopyState::voxelized : VoxelCopyState::ready;
    }

    // Late, after the copies are updated and the world transforms (bounds), before the flashes are written into
    // the MeshRenderers and the render sync: the swap shows this frame
    [[=ecs::system<ecs::Phase::Late>, =ecs::after<VoxelCopies>, =ecs::after<scene::TransformPropagation>,
        =ecs::before<HitFlashes>, =ecs::before<RenderSync>]]
    void voxelize_on_impact(ecs::EventReader<WeaponImpactEvent> impacts, ecs::Query<VoxelDestructible> destructibles,
        ecs::Query<MeshRenderer> renderers, ecs::Query<const WorldTransform> transforms,
        ecs::EventWriter<VoxelizedEvent> voxelized, ecs::Commands& commands)
    {
        const bool voxelize_all = voxelize_all_requested.exchange(false);
        const bool restore_all = restore_all_requested.exchange(false);

        auto flash = [&] (ecs::Entity e, const WeaponDef* def)
        {
            const glm::vec3 color = def ? def->hit_flash_color * def->hit_flash : glm::vec3(1.0f, 0.45f, 0.95f) * 16.0f;
            commands.add(e, HitFlash{ .color = color, .duration = def ? std::max(def->hit_flash_time * 2.0f, 0.3f) : 0.7f });
        };
        auto turn = [&] (ecs::Entity e, VoxelDestructible& destructible, glm::vec3 position, const WeaponDef* def)
        {
            set_voxelized(e, destructible, true, renderers);
            flash(e, def);
            voxelized.send({ .entity = e, .position = position, .weapon = def ? def->name : std::string() });
            LogVoxelDestructible.Log("Voxelized entity %llu by %s", (unsigned long long)e.bits(), def ? def->name.c_str() : "console");
        };
        // a hit within the radius (m) of the entity's bounds: queued for the next job
        auto queue_hit = [&] (ecs::Entity e, VoxelDestructible& destructible, glm::vec3 position, const std::string& preset_name,
                              float weapon_radius)
        {
            const WorldTransform* world = transforms.get(e);
            if (!world || !destructible.grid)
                return false;
            const std::optional<VoxelPreset> preset = load_voxel_preset(preset_name);
            if (!preset)
            {
                LogVoxelDestructible.Log<Warning>("No voxel preset '%s' in assets/jpeg/presets.json (section \"voxels\")",
                    preset_name.c_str());
                return false;
            }
            const float radius = preset->radius > 0.0f ? preset->radius : weapon_radius;
            const glm::vec3 scale = world->value.scale.glm();
            const float largest_scale = std::max({ scale.x, scale.y, scale.z, 1e-3f });
            const glm::vec3 local = glm::vec3(glm::inverse(world->value.matrix()) * glm::vec4(position, 1.0f));

            VoxelHit hit;
            hit.center = destructible.grid->voxel_at(local);
            hit.radius = std::max(int32_t(std::lround(radius / (destructible.voxel_size * largest_scale))), 1);
            // the n-th hit of this house at this voxel: the same everywhere for the same hits
            const uint32_t index = destructible.hits_queued++;
            hit.seed = mix(index * 0x9e3779b9u ^ mix(uint32_t(hit.center.x) * 73856093u ^ uint32_t(hit.center.y) * 19349663u ^
                                                    uint32_t(hit.center.z) * 83492791u));
            hit.settings = preset->settings;
            destructible.pending_hits.push_back(hit);
            return true;
        };
        auto within = [&] (ecs::Entity e, glm::vec3 position, float radius)
        {
            const MeshRenderer* mesh = renderers.get(e);
            const WorldTransform* world = transforms.get(e);
            if (!mesh || !world)
                return false;
            const AABB bounds = get_mesh_bounds(*mesh, world->value);
            return glm::length(glm::clamp(position, bounds.min, bounds.max) - position) <= std::max(radius, 0.25f);
        };

        if (voxelize_all || restore_all)
        {
            destructibles.each([&] (ecs::Entity e, VoxelDestructible& destructible)
            {
                if (voxelize_all && destructible.state == VoxelCopyState::ready)
                    turn(e, destructible, glm::vec3(0.0f), nullptr);
                else if (restore_all && destructible.state == VoxelCopyState::voxelized)
                    set_voxelized(e, destructible, false, renderers);
            });
        }

        std::vector<std::pair<glm::vec3, std::string>> console;
        {
            std::scoped_lock lock(console_hits_mutex);
            console.swap(console_hits);
        }
        for (const auto& [position, preset] : console)
            destructibles.each([&] (ecs::Entity e, VoxelDestructible& destructible)
            {
                if (destructible.state == VoxelCopyState::voxelized && within(e, position, 4.0f))
                    queue_hit(e, destructible, position, preset, 4.0f);
            });

        for (const WeaponImpactEvent& impact : impacts.read())
        {
            const WeaponDef* def = weapons::find(impact.weapon);
            if (!def || !def->voxelize)
                continue;
            destructibles.each([&] (ecs::Entity e, VoxelDestructible& destructible)
            {
                if (destructible.state != VoxelCopyState::ready && destructible.state != VoxelCopyState::voxelized)
                    return;
                if (!within(e, impact.position, def->radius))
                    return;
                if (destructible.state == VoxelCopyState::ready)
                    turn(e, destructible, impact.position, def);
                else if (queue_hit(e, destructible, impact.position, def->jpeg, def->radius))
                    flash(e, def);
            });
        }
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(VoxelDestructible)
}
