module;

#include <json/value.h>

module terrain;

import std.compat;
import fixed_string;
import texture_format;
import glm;
import rhmath;
import reflect;
import rhobject;
import name;
import ecs;
import framework;
import assets;
import physics;
import render;
import rhcomponents;
import engine;
import globals;
import paths;
import log;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogTerrain, Log);

namespace
{
    float sample(const TerrainData& data, int32_t x, int32_t z)
    {
        const int32_t last = int32_t(data.samples) - 1;
        return data.heights[size_t(std::clamp(z, 0, last)) * data.samples + size_t(std::clamp(x, 0, last))];
    }

    // same triangulation as the collision (Jolt splits quads along (x, z) - (x + 1, z + 1))
    float interpolate(const TerrainData& data, float fx, float fz)
    {
        const int32_t last_quad = int32_t(data.samples) - 2;
        const int32_t x = std::clamp(int32_t(std::floor(fx)), 0, last_quad);
        const int32_t z = std::clamp(int32_t(std::floor(fz)), 0, last_quad);
        const float tx = std::clamp(fx - float(x), 0.0f, 1.0f);
        const float tz = std::clamp(fz - float(z), 0.0f, 1.0f);
        const float a = sample(data, x, z);
        const float b = sample(data, x + 1, z);
        const float c = sample(data, x, z + 1);
        const float d = sample(data, x + 1, z + 1);
        if (tx >= tz)
            return a + (b - a) * tx + (d - b) * tz;
        return a + (c - a) * tz + (d - c) * tx;
    }

    Vertex make_vertex(const TerrainData& data, uint32_t x, uint32_t z)
    {
        const int32_t ix = int32_t(x), iz = int32_t(z);
        const float h = sample(data, ix, iz);
        const float left = sample(data, ix - 1, iz), right = sample(data, ix + 1, iz);
        const float back = sample(data, ix, iz - 1), front = sample(data, ix, iz + 1);
        const float half_size = 0.5f * data.size();

        Vertex v{};
        v.position = glm::vec3(float(x) * data.spacing - half_size, h, float(z) * data.spacing - half_size);
        v.normal = glm::normalize(glm::vec3(left - right, 2.0f * data.spacing, back - front));
        // the splat map spans the terrain: uv 0..1 over the samples (terrain.frag tiles the layers in world space)
        v.tex_coord = glm::vec2(float(x), float(z)) / float(data.samples - 1);
        v.tangent = glm::vec4(glm::normalize(glm::vec3(2.0f * data.spacing, right - left, 0.0f)), 1.0f);
        return v;
    }

    void fill_chunk_vertices(const TerrainData& data, const TerrainChunk& chunk, std::vector<Vertex>& vertices)
    {
        vertices.resize(size_t(chunk.quads_x + 1) * (chunk.quads_z + 1));
        size_t index = 0;
        for (uint32_t z = 0; z <= chunk.quads_z; ++z)
            for (uint32_t x = 0; x <= chunk.quads_x; ++x)
                vertices[index++] = make_vertex(data, chunk.first_x + x, chunk.first_z + z);
    }

    StaticMesh build_chunk_mesh(const TerrainData& data, const TerrainChunk& chunk, std::string name)
    {
        Primitive primitive;
        fill_chunk_vertices(data, chunk, primitive.vertices);
        primitive.material_index = 0;

        // clockwise like every render mesh (the glTF importer flips the winding), split like the collision
        const uint32_t row = chunk.quads_x + 1;
        primitive.indices.reserve(size_t(chunk.quads_x) * chunk.quads_z * 6);
        for (uint32_t z = 0; z < chunk.quads_z; ++z)
        {
            for (uint32_t x = 0; x < chunk.quads_x; ++x)
            {
                const uint32_t a = z * row + x, b = a + 1, c = a + row, d = c + 1;
                primitive.indices.insert(primitive.indices.end(), { a, d, c, a, b, d });
            }
        }

        // heights change while sculpting and the proxy keeps its bounds: the whole height range
        const float half_size = 0.5f * data.size();
        const glm::vec3 min(float(chunk.first_x) * data.spacing - half_size, data.min_height,
                            float(chunk.first_z) * data.spacing - half_size);
        const glm::vec3 max(float(chunk.first_x + chunk.quads_x) * data.spacing - half_size, data.max_height,
                            float(chunk.first_z + chunk.quads_z) * data.spacing - half_size);

        StaticMesh mesh;
        mesh.name = std::move(name);
        mesh.bounds = AABB(min, max);
        mesh.mesh_geometry.push_back(Geometry{ .primitives = {} });
        mesh.mesh_geometry[0].primitives.push_back(std::move(primitive));
        mesh.material_names.push_back("terrain");
        return mesh;
    }

    std::vector<float> load_heights(const Terrain& terrain, bool& loaded)
    {
        const size_t count = size_t(terrain.samples) * terrain.samples;
        std::vector<float> heights(count, std::clamp(0.0f, terrain.min_height, terrain.max_height));
        loaded = false;
        if (terrain.heightmap.empty())
            return heights;

        const std::filesystem::path path = paths::get_assets_path() / terrain.heightmap;
        std::ifstream file(path, std::ios::binary);
        if (!file)
        {
            LogTerrain.Log<Warning>("Heightmap '%s' not found: flat terrain", terrain.heightmap.c_str());
            return heights;
        }
        std::vector<uint16_t> raw(count);
        file.read(reinterpret_cast<char*>(raw.data()), std::streamsize(count * sizeof(uint16_t)));
        if (file.gcount() != std::streamsize(count * sizeof(uint16_t)))
        {
            LogTerrain.Log<Warning>("Heightmap '%s' is not %u x %u uint16 samples: flat terrain",
                terrain.heightmap.c_str(), terrain.samples, terrain.samples);
            return heights;
        }
        const float range = terrain.max_height - terrain.min_height;
        for (size_t i = 0; i < count; ++i)
            heights[i] = terrain.min_height + float(raw[i]) / 65535.0f * range;
        loaded = true;
        return heights;
    }

    Texture load_splat(const Terrain& terrain)
    {
        if (!terrain.splatmap.empty())
        {
            std::optional<Texture> file = Texture::create_from_file(paths::get_assets_path() / terrain.splatmap);
            if (file && file->extent.width == file->extent.height)
                return std::move(*file);
            LogTerrain.Log<Warning>("Splat map '%s' not found or not square: layer 0 everywhere", terrain.splatmap.c_str());
        }
        Texture splat;
        splat.extent = Extent(terrain.splat_resolution, terrain.splat_resolution);
        splat.format = TextureFormat::RGBA8;
        splat.bulk.resize(size_t(terrain.splat_resolution) * terrain.splat_resolution * 4, std::byte{0});
        for (size_t i = 0; i < splat.bulk.size(); i += 4)
            splat.bulk[i] = std::byte{255};
        return splat;
    }

    Texture load_puddles(const Terrain& terrain)
    {
        if (!terrain.puddlemap.empty())
        {
            std::optional<Texture> file = Texture::create_from_file(paths::get_assets_path() / terrain.puddlemap);
            if (file && file->extent.width == file->extent.height)
                return std::move(*file);
        }
        Texture puddles;
        puddles.extent = Extent(terrain.puddle_resolution, terrain.puddle_resolution);
        puddles.format = TextureFormat::RGBA8;
        puddles.bulk.resize(size_t(terrain.puddle_resolution) * terrain.puddle_resolution * 4, std::byte{0});
        for (size_t i = 3; i < puddles.bulk.size(); i += 4)
            puddles.bulk[i] = std::byte{255};
        return puddles;
    }

    Texture load_foliage(const Terrain& terrain)
    {
        if (!terrain.foliagemap.empty())
        {
            std::optional<Texture> file = Texture::create_from_file(paths::get_assets_path() / terrain.foliagemap);
            if (file && file->extent.width == file->extent.height)
                return std::move(*file);
            LogTerrain.Log<Warning>("Foliage map '%s' not found or not square: no ground foliage", terrain.foliagemap.c_str());
        }
        Texture foliage;
        foliage.extent = Extent(terrain.foliage_resolution, terrain.foliage_resolution);
        foliage.format = TextureFormat::RGBA8;
        foliage.bulk.resize(size_t(terrain.foliage_resolution) * terrain.foliage_resolution * 4, std::byte{0});
        return foliage;
    }

    // 1 inside the inner part of the brush, fading to 0 at the radius
    float brush_weight(const TerrainBrush& brush, float distance)
    {
        if (distance >= brush.radius)
            return 0.0f;
        const float inner = brush.radius * (1.0f - std::clamp(brush.falloff, 0.0f, 1.0f));
        if (distance <= inner)
            return 1.0f;
        const float t = (distance - inner) / std::max(brush.radius - inner, 1e-4f);
        return 1.0f - t * t * (3.0f - 2.0f * t);
    }

    // brush -> samples / texels (inclusive), clamped to [0, count)
    TerrainRect brush_rect(glm::vec2 center, float radius, float cell, int32_t count)
    {
        TerrainRect rect;
        rect.min_x = std::max(0, int32_t(std::floor((center.x - radius) / cell)));
        rect.min_z = std::max(0, int32_t(std::floor((center.y - radius) / cell)));
        rect.max_x = std::min(count - 1, int32_t(std::ceil((center.x + radius) / cell)));
        rect.max_z = std::min(count - 1, int32_t(std::ceil((center.y + radius) / cell)));
        return rect;
    }

    void sculpt(TerrainData& data, const TerrainBrush& brush, glm::vec2 local, float dt)
    {
        const TerrainRect rect = brush_rect(local, brush.radius, data.spacing, int32_t(data.samples));
        if (rect.empty())
            return;

        // smoothing reads the heights before this step
        std::vector<float> before;
        if (brush.mode == TerrainBrushMode::smooth)
            before = data.heights;

        const float rate = std::clamp(brush.strength, 0.0f, 1.0f);
        const float flatten_target = brush.flatten_height - data.origin.y;
        for (int32_t z = rect.min_z; z <= rect.max_z; ++z)
        {
            for (int32_t x = rect.min_x; x <= rect.max_x; ++x)
            {
                const float distance = glm::length(glm::vec2(float(x), float(z)) * data.spacing - local);
                const float weight = brush_weight(brush, distance);
                if (weight <= 0.0f)
                    continue;

                float& h = data.heights[size_t(z) * data.samples + size_t(x)];
                switch (brush.mode)
                {
                case TerrainBrushMode::raise:
                    h += rate * 4.0f * weight * dt;         // m/s at full strength
                    break;
                case TerrainBrushMode::lower:
                    h -= rate * 4.0f * weight * dt;
                    break;
                case TerrainBrushMode::smooth:
                {
                    float sum = 0.0f;
                    for (int32_t dz = -1; dz <= 1; ++dz)
                        for (int32_t dx = -1; dx <= 1; ++dx)
                        {
                            const int32_t sx = std::clamp(x + dx, 0, int32_t(data.samples) - 1);
                            const int32_t sz = std::clamp(z + dz, 0, int32_t(data.samples) - 1);
                            sum += before[size_t(sz) * data.samples + size_t(sx)];
                        }
                    h = glm::mix(h, sum / 9.0f, std::clamp(rate * 10.0f * weight * dt, 0.0f, 1.0f));
                    break;
                }
                case TerrainBrushMode::flatten:
                    h = glm::mix(h, flatten_target, std::clamp(rate * 10.0f * weight * dt, 0.0f, 1.0f));
                    break;
                case TerrainBrushMode::paint:
                case TerrainBrushMode::water:
                case TerrainBrushMode::dry:
                case TerrainBrushMode::foliage:
                case TerrainBrushMode::clear_foliage:
                    break;
                }
                h = std::clamp(h, data.min_height, data.max_height);
            }
        }
        data.dirty_heights.add(rect);
        data.collision_dirty = true;
        data.unsaved = true;
    }

    void paint(TerrainData& data, const TerrainBrush& brush, glm::vec2 local, float dt)
    {
        Texture& splat = terrain::get_splat(data);
        const int32_t resolution = int32_t(splat.extent.width);
        const float texel = data.size() / float(resolution);
        // texel centers at (i + 0.5) * texel
        const TerrainRect rect = brush_rect(local - glm::vec2(0.5f * texel), brush.radius, texel, resolution);
        if (rect.empty() || brush.layer > 3)
            return;

        const float rate = std::clamp(brush.strength, 0.0f, 1.0f) * 6.0f * dt;
        auto* texels = reinterpret_cast<uint8_t*>(splat.bulk.data());
        for (int32_t z = rect.min_z; z <= rect.max_z; ++z)
        {
            for (int32_t x = rect.min_x; x <= rect.max_x; ++x)
            {
                const glm::vec2 center = (glm::vec2(float(x), float(z)) + 0.5f) * texel;
                const float weight = brush_weight(brush, glm::length(center - local));
                if (weight <= 0.0f)
                    continue;

                uint8_t* w = texels + (size_t(z) * resolution + size_t(x)) * 4;
                const float amount = std::clamp(rate * weight, 0.0f, 1.0f);
                // the painted layer gains at least one step (8 bit weights, small per frame amounts)
                const int32_t old_layer = w[brush.layer];
                int32_t new_layer = old_layer + int32_t(std::ceil(float(255 - old_layer) * amount));
                new_layer = std::min(new_layer, 255);
                if (new_layer == old_layer)
                    continue;

                // the others keep their proportions in what is left
                const int32_t others_before = std::max(1, (w[0] + w[1] + w[2] + w[3]) - old_layer);
                const int32_t others_after = 255 - new_layer;
                int32_t assigned = new_layer;
                int32_t largest = -1;
                for (uint32_t layer = 0; layer < 4; ++layer)
                {
                    if (layer == brush.layer)
                        continue;
                    const int32_t value = int32_t(float(w[layer]) * float(others_after) / float(others_before));
                    w[layer] = uint8_t(value);
                    assigned += value;
                    if (largest < 0 || w[layer] > w[largest])
                        largest = int32_t(layer);
                }
                w[brush.layer] = uint8_t(new_layer);
                // rounding leftovers: the sum stays 255
                if (assigned < 255)
                {
                    const uint32_t target = largest >= 0 && w[largest] > 0 ? uint32_t(largest) : brush.layer;
                    w[target] = uint8_t(w[target] + (255 - assigned));
                }
            }
        }
        data.dirty_splat.add(rect);
        data.unsaved = true;
    }

    // The water level moves toward the brush's profile (full in the inner part, 0 at the radius): the falloff
    // is the shore of the puddle. Drying mirrors it.
    void flood(TerrainData& data, const TerrainBrush& brush, glm::vec2 local, float dt)
    {
        Texture& puddles = terrain::get_puddles(data);
        const int32_t resolution = int32_t(puddles.extent.width);
        const float texel = data.size() / float(resolution);
        const TerrainRect rect = brush_rect(local - glm::vec2(0.5f * texel), brush.radius, texel, resolution);
        if (rect.empty())
            return;

        const bool fill = brush.mode == TerrainBrushMode::water;
        const float rate = std::clamp(brush.strength, 0.0f, 1.0f) * 6.0f * dt;
        auto* texels = reinterpret_cast<uint8_t*>(puddles.bulk.data());
        for (int32_t z = rect.min_z; z <= rect.max_z; ++z)
        {
            for (int32_t x = rect.min_x; x <= rect.max_x; ++x)
            {
                const glm::vec2 center = (glm::vec2(float(x), float(z)) + 0.5f) * texel;
                const float weight = brush_weight(brush, glm::length(center - local));
                if (weight <= 0.0f)
                    continue;

                uint8_t* level = texels + (size_t(z) * resolution + size_t(x)) * 4;
                const int32_t old_level = level[0];
                const int32_t target = int32_t(std::lround((fill ? weight : 1.0f - weight) * 255.0f));
                const int32_t distance = fill ? target - old_level : old_level - target;
                if (distance <= 0)
                    continue;
                // at least one step (8 bit levels, small per frame amounts)
                const int32_t step = int32_t(std::ceil(float(distance) * std::min(rate, 1.0f)));
                const uint8_t new_level = uint8_t(fill ? old_level + step : old_level - step);
                level[0] = level[1] = level[2] = new_level;
            }
        }
        data.dirty_puddles.add(rect);
        data.unsaved = true;
    }

    // The density of one foliage channel moves toward the brush's profile (full in the inner part, 0 at the
    // radius), clearing mirrors it: like the puddles, the falloff is the edge of the patch
    void grow(TerrainData& data, const TerrainBrush& brush, glm::vec2 local, float dt)
    {
        Texture& foliage = terrain::get_foliage(data);
        const int32_t resolution = int32_t(foliage.extent.width);
        const float texel = data.size() / float(resolution);
        const TerrainRect rect = brush_rect(local - glm::vec2(0.5f * texel), brush.radius, texel, resolution);
        if (rect.empty() || brush.foliage_channel > 3)
            return;

        const bool fill = brush.mode == TerrainBrushMode::foliage;
        const float rate = std::clamp(brush.strength, 0.0f, 1.0f) * 6.0f * dt;
        auto* texels = reinterpret_cast<uint8_t*>(foliage.bulk.data());
        for (int32_t z = rect.min_z; z <= rect.max_z; ++z)
        {
            for (int32_t x = rect.min_x; x <= rect.max_x; ++x)
            {
                const glm::vec2 center = (glm::vec2(float(x), float(z)) + 0.5f) * texel;
                const float weight = brush_weight(brush, glm::length(center - local));
                if (weight <= 0.0f)
                    continue;

                uint8_t& density = texels[(size_t(z) * resolution + size_t(x)) * 4 + brush.foliage_channel];
                const int32_t old_density = density;
                const int32_t target = int32_t(std::lround((fill ? weight : 1.0f - weight) * 255.0f));
                const int32_t distance = fill ? target - old_density : old_density - target;
                if (distance <= 0)
                    continue;
                // at least one step (8 bit densities, small per frame amounts)
                const int32_t step = int32_t(std::ceil(float(distance) * std::min(rate, 1.0f)));
                density = uint8_t(fill ? old_density + step : old_density - step);
            }
        }
        data.dirty_foliage.add(rect);
        data.unsaved = true;
    }

    [[=scene::on_spawned<Terrain>]]
    void spawn_terrain(World& world, ecs::Entity root, const SerializationContext&)
    {
        ecs::Registry& registry = world.registry;
        const Terrain terrain = *registry.get<Terrain>(root);
        if (terrain.samples < 8 || terrain.samples % 4 != 0 || terrain.spacing <= 0.0f || terrain.chunk_quads == 0)
        {
            LogTerrain.Log<Error>("Terrain '%s': samples must be a multiple of 4 (>= 8), spacing > 0",
                scene::get_name(registry, root).c_str());
            return;
        }
        if (!terrain.material || terrain.material->model != Name("terrain"))
        {
            LogTerrain.Log<Error>("Terrain '%s' needs a material of the model \"terrain\"", scene::get_name(registry, root).c_str());
            return;
        }

        const Transform world_transform = scene::get_world_transform(registry, root);
        if (world_transform.rotation.glm() != glm::quat(1.0f, 0.0f, 0.0f, 0.0f) || world_transform.scale.glm() != glm::vec3(1.0f))
            LogTerrain.Log<Warning>("Terrain '%s': rotation and scale are ignored by its collision", scene::get_name(registry, root).c_str());

        auto data = std::make_shared<TerrainData>();
        data->samples = terrain.samples;
        data->spacing = terrain.spacing;
        data->min_height = terrain.min_height;
        data->max_height = terrain.max_height;
        data->origin = world_transform.position.glm() - glm::vec3(0.5f * data->size(), 0.0f, 0.5f * data->size());

        bool heights_loaded = false;
        data->heights = load_heights(terrain, heights_loaded);

        Texture splat = load_splat(terrain);
        splat.name = terrain.splatmap.empty() ? std::string("terrain_splat") : terrain.splatmap;
        // not the path: an AssetManager::load_texture of the same file must not return this editable copy
        splat.transition_path = std::format("{}#terrain{}", splat.name, root.bits());
        data->splat = AssetManager::get().register_external_texture(std::move(splat));
        terrain.material->parameters["splat"] = data->splat;

        Texture puddles = load_puddles(terrain);
        puddles.name = terrain.puddlemap.empty() ? std::string("terrain_puddles") : terrain.puddlemap;
        puddles.transition_path = std::format("{}#terrain{}", puddles.name, root.bits());
        data->puddles = AssetManager::get().register_external_texture(std::move(puddles));
        terrain.material->parameters["puddles"] = data->puddles;

        // read by the ground foliage (module foliage), not by the terrain material
        Texture foliage = load_foliage(terrain);
        foliage.name = terrain.foliagemap.empty() ? std::string("terrain_foliage") : terrain.foliagemap;
        foliage.transition_path = std::format("{}#terrain{}", foliage.name, root.bits());
        data->foliage = AssetManager::get().register_external_texture(std::move(foliage));

        const std::string name = scene::get_name(registry, root);
        const uint32_t quads = terrain.samples - 1;
        for (uint32_t first_z = 0; first_z < quads; first_z += terrain.chunk_quads)
        {
            for (uint32_t first_x = 0; first_x < quads; first_x += terrain.chunk_quads)
            {
                TerrainChunk chunk{
                    .first_x = first_x,
                    .first_z = first_z,
                    .quads_x = std::min(terrain.chunk_quads, quads - first_x),
                    .quads_z = std::min(terrain.chunk_quads, quads - first_z),
                };
                const std::string chunk_name = std::format("{}_chunk_{}_{}", name, first_x / terrain.chunk_quads,
                                                           first_z / terrain.chunk_quads);
                chunk.mesh = AssetManager::get().store_mesh(build_chunk_mesh(*data, chunk, chunk_name));

                chunk.entity = registry.create();
                registry.add<Name>(chunk.entity, chunk_name);
                registry.add<Transform>(chunk.entity, Transform{});
                registry.add<ChildOf>(chunk.entity, root);
                MeshRenderer renderer;
                renderer.mesh = chunk.mesh;
                renderer.materials.push_back(terrain.material);
                registry.add<MeshRenderer>(chunk.entity, std::move(renderer));
                data->chunks.push_back(chunk);
            }
        }

        terrain::commit_collision(*data, world.get_physics(), root);

        LogTerrain.Log("Terrain '%s': %u x %u samples (%s), %.0f m, %zu chunks", name.c_str(), terrain.samples,
            terrain.samples, heights_loaded ? terrain.heightmap.c_str() : "flat", data->size(), data->chunks.size());
        registry.get<Terrain>(root)->data = std::move(data);
    }

    [[=scene::on_spawned<SnapToTerrain>]]
    void snap_to_terrain(World& world, ecs::Entity e, const SerializationContext&)
    {
        ecs::Registry& registry = world.registry;
        const float offset = registry.get<SnapToTerrain>(e)->offset;
        Transform transform = scene::get_world_transform(registry, e);

        std::optional<float> height;
        ecs::Query<const Terrain>(registry).each([&] (const Terrain& terrain) {
            if (terrain.data && !height)
                height = terrain::height_at(*terrain.data, glm::vec2(transform.position.x, transform.position.z));
        });
        if (!height)
        {
            LogTerrain.Log<Warning>("SnapToTerrain of '%s': no terrain under it (the Terrain entity must be spawned first)",
                scene::get_name(registry, e).c_str());
            return;
        }
        transform.position.y = *height + offset;
        scene::set_world_transform(registry, e, transform);
    }

    [[=ecs::on_remove]]
    void destroy_terrain_body(ecs::Registry& registry, ecs::Entity, Terrain& terrain)
    {
        phys::PhysicsScene* physics = registry.find_resource<phys::PhysicsScene>();
        if (terrain.data && terrain.data->body.is_valid() && physics)
            physics->destroy_body(terrain.data->body);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Terrain, SnapToTerrain)
}

namespace terrain
{
    Texture& get_splat(const TerrainData& data)
    {
        return AssetManager::get().loaded_textures.at(data.splat);
    }

    Texture& get_puddles(const TerrainData& data)
    {
        return AssetManager::get().loaded_textures.at(data.puddles);
    }

    Texture& get_foliage(const TerrainData& data)
    {
        return AssetManager::get().loaded_textures.at(data.foliage);
    }

    std::optional<float> height_at(const TerrainData& data, glm::vec2 world_xz)
    {
        const glm::vec2 local = (world_xz - glm::vec2(data.origin.x, data.origin.z)) / data.spacing;
        const float last = float(data.samples - 1);
        if (local.x < 0.0f || local.y < 0.0f || local.x > last || local.y > last)
            return std::nullopt;
        return data.origin.y + interpolate(data, local.x, local.y);
    }

    std::optional<glm::vec3> raycast(const TerrainData& data, const glm::vec3& origin, const glm::vec3& direction,
                                     float max_distance)
    {
        // march in half samples, then bisect the crossing
        const float step = 0.5f * data.spacing;
        auto above = [&] (float t) -> std::optional<bool> {
            const glm::vec3 p = origin + direction * t;
            const std::optional<float> h = height_at(data, glm::vec2(p.x, p.z));
            if (!h)
                return std::nullopt;
            return p.y >= *h;
        };

        float previous_t = 0.0f;
        std::optional<bool> previous = above(0.0f);
        for (float t = step; t <= max_distance; t += step)
        {
            const std::optional<bool> current = above(t);
            if (previous && current && *previous && !*current)
            {
                float lo = previous_t, hi = t;
                for (int i = 0; i < 16; ++i)
                {
                    const float mid = 0.5f * (lo + hi);
                    if (above(mid).value_or(true))
                        lo = mid;
                    else
                        hi = mid;
                }
                return origin + direction * hi;
            }
            previous = current;
            previous_t = t;
        }
        return std::nullopt;
    }

    void apply_brush(TerrainData& data, const TerrainBrush& brush, glm::vec2 world_xz, float dt)
    {
        const glm::vec2 local = world_xz - glm::vec2(data.origin.x, data.origin.z);
        if (brush.mode == TerrainBrushMode::paint)
            paint(data, brush, local, dt);
        else if (brush.mode == TerrainBrushMode::water || brush.mode == TerrainBrushMode::dry)
            flood(data, brush, local, dt);
        else if (brush.mode == TerrainBrushMode::foliage || brush.mode == TerrainBrushMode::clear_foliage)
            grow(data, brush, local, dt);
        else
            sculpt(data, brush, local, dt);
    }

    void commit_render(TerrainData& data)
    {
        Renderer& renderer = *RhGlobals::engine->renderer;
        const std::shared_ptr<RenderBackend> backend = renderer.get_backend();

        if (!data.dirty_heights.empty())
        {
            // normals of the neighbors change too
            TerrainRect changed = data.dirty_heights;
            changed.min_x -= 1;
            changed.min_z -= 1;
            changed.max_x += 1;
            changed.max_z += 1;
            for (const TerrainChunk& chunk : data.chunks)
            {
                const TerrainRect chunk_rect{
                    int32_t(chunk.first_x), int32_t(chunk.first_z),
                    int32_t(chunk.first_x + chunk.quads_x), int32_t(chunk.first_z + chunk.quads_z) };
                if (!changed.intersects(chunk_rect))
                    continue;
                // AssetManager's copy: a chunk drawn for the first time uploads it as is
                std::vector<Vertex>& vertices = AssetManager::get().loaded_meshes.at(chunk.mesh).mesh_geometry[0].primitives[0].vertices;
                fill_chunk_vertices(data, chunk, vertices);
                backend->update_mesh_vertices(MeshPrimHandle{ chunk.mesh, 0, 0 }, vertices);
            }
            data.dirty_heights = {};
            ++data.heights_version;
        }

        auto upload = [&] (TextureHandle handle, TerrainRect& rect) {
            if (rect.empty())
                return;
            backend->update_texture_2d(renderer.get_texture(handle), AssetManager::get().loaded_textures.at(handle), TextureRegion{
                .x = uint32_t(rect.min_x),
                .y = uint32_t(rect.min_z),
                .width = uint32_t(rect.max_x - rect.min_x + 1),
                .height = uint32_t(rect.max_z - rect.min_z + 1),
            });
            rect = {};
        };
        upload(data.splat, data.dirty_splat);
        upload(data.puddles, data.dirty_puddles);
        if (!data.dirty_foliage.empty())
        {
            upload(data.foliage, data.dirty_foliage);
            ++data.foliage_version;
        }
    }

    void commit_collision(TerrainData& data, phys::PhysicsScene& physics, ecs::Entity owner)
    {
        phys::Shape shape = physics.create_shape(phys::HeightFieldShape{
            .sample_count = data.samples,
            .spacing = data.spacing,
            .heights = data.heights,
        });
        if (!shape)
        {
            LogTerrain.Log<Error>("Could not create the terrain height field");
            return;
        }
        if (data.body.is_valid())
            physics.destroy_body(data.body);
        data.shape = std::move(shape);
        data.body = physics.create_body({
            .shape = data.shape,
            .position = data.origin,
            .motion = phys::Motion::fixed,
            .category = phys::Category::static_world,
            .friction = 0.8f,
            .user_data = owner.bits(),
        });
        data.collision_dirty = false;
    }

    bool save(const Terrain& terrain, std::string& error)
    {
        if (!terrain.data)
        {
            error = "the terrain is not spawned";
            return false;
        }
        if (terrain.heightmap.empty() || terrain.splatmap.empty())
        {
            error = "the Terrain has no heightmap / splatmap path";
            return false;
        }
        const TerrainData& data = *terrain.data;

        const std::filesystem::path heightmap_path = paths::get_assets_path() / terrain.heightmap;
        std::filesystem::create_directories(heightmap_path.parent_path());
        std::vector<uint16_t> raw(data.heights.size());
        const float range = std::max(data.max_height - data.min_height, 1e-3f);
        for (size_t i = 0; i < raw.size(); ++i)
            raw[i] = uint16_t(std::lround(std::clamp((data.heights[i] - data.min_height) / range, 0.0f, 1.0f) * 65535.0f));
        {
            std::ofstream file(heightmap_path, std::ios::binary | std::ios::trunc);
            file.write(reinterpret_cast<const char*>(raw.data()), std::streamsize(raw.size() * sizeof(uint16_t)));
            if (!file)
            {
                error = "could not write " + heightmap_path.string();
                return false;
            }
        }

        const std::filesystem::path splat_path = paths::get_assets_path() / terrain.splatmap;
        if (!get_splat(data).save_to_file(splat_path))
        {
            error = "could not write " + splat_path.string();
            return false;
        }

        if (!terrain.puddlemap.empty())
        {
            const std::filesystem::path puddle_path = paths::get_assets_path() / terrain.puddlemap;
            if (!get_puddles(data).save_to_file(puddle_path))
            {
                error = "could not write " + puddle_path.string();
                return false;
            }
        }

        if (!terrain.foliagemap.empty())
        {
            const std::filesystem::path foliage_path = paths::get_assets_path() / terrain.foliagemap;
            if (!get_foliage(data).save_to_file(foliage_path))
            {
                error = "could not write " + foliage_path.string();
                return false;
            }
        }

        terrain.data->unsaved = false;
        LogTerrain.Log("Saved %s and %s", terrain.heightmap.c_str(), terrain.splatmap.c_str());
        return true;
    }
}
