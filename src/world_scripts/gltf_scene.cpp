module;

#include <json/value.h>

module gltf_scene;

import std.compat;
import fixed_string;

import log;
import glm;
import ecs;
import rhmath;
import rhcomponents;
import assets;
import physics;

#include "logging/log_macro.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogImportGltf, Log);

MeshCollisionSettings GltfScene::get_collision_settings(std::string_view mesh_name) const
{
    MeshCollisionSettings settings{ .type = collision_type, .simplify_error = simplify_error };
    size_t matched_length = 0;
    for (const auto& [prefix, type] : collision_overrides)
    {
        if (mesh_name.starts_with(prefix) && prefix.size() >= matched_length)
        {
            settings.type = type;
            matched_length = prefix.size();
        }
    }
    return settings;
}

namespace
{
    [[=scene::on_spawned<GltfScene>]]
    void import_scene(World& world, ecs::Entity root, const SerializationContext& context)
    {
        ecs::Registry& registry = world.registry;
        const GltfScene scene_desc = *registry.get<GltfScene>(root);
        AssetSceneInfo scene = AssetManager::get().load_scene(scene_desc.asset_path, scene_desc.textures_dir);

        std::vector<std::shared_future<void>> texture_loads;
        for (uint32_t index = 0; index < scene.objects.size(); ++index)
        {
            AssetSceneObject& object = scene.objects[index];
            if (std::ranges::any_of(scene_desc.hidden_meshes, [&] (const std::string& prefix) { return object.mesh.name.starts_with(prefix); }))
                continue;

            MeshRenderer renderer;
            for (const auto& material_name : object.mesh.material_names)
                renderer.materials.push_back(scene.materials[material_name]);
            const std::string mesh_name = object.mesh.name;
            renderer.mesh = AssetManager::get().store_mesh(std::move(object.mesh));

            for (const auto& material : renderer.materials)
                for (auto& [name, param] : material->parameters)
                    if (param.is<TextureHandle>() && param.as<TextureHandle>() != TextureHandle::invalid())
                        texture_loads.push_back(param.as<TextureHandle>().resolve_async());

            const ecs::Entity child = registry.create();
            registry.add<Name>(child, mesh_name);
            registry.add<Transform>(child, object.transform);
            registry.add<ChildOf>(child, root);

            const MeshCollisionSettings collision = scene_desc.get_collision_settings(mesh_name);
            if (scene_desc.collision && collision.type != MeshCollision::none)
            {
                // cooked in parallel with the texture loads (or loaded from the cache); the body is created in PostLoad
                MeshCollider collider{ .type = collision.type, .simplify_error = collision.simplify_error };
                collider.pending = std::make_shared<phys::Shape>();
                phys::PhysicsScene& physics = world.get_physics();
                const StaticMesh* mesh_data = &renderer.mesh.get();
                const glm::vec3 scale = object.transform.scale.glm();
                std::string cache_key = std::format("{}__{}_{}", scene_desc.asset_path, index, mesh_name);
                context.dc->push(std::async(std::launch::async,
                    [shape = collider.pending, mesh_data, scale, collision, &physics, cache_key = std::move(cache_key)]
                    {
                        *shape = cook_mesh_collision(physics, *mesh_data, scale, collision, cache_key);
                    }).share());
                registry.add<MeshCollider>(child, std::move(collider));
            }

            // simplified shadow casters, also on a loading thread (stored as meshes in PostLoad)
            size_t triangles = 0;
            for (const Geometry& geometry : renderer.mesh.get().mesh_geometry)
                for (const Primitive& primitive : geometry.primitives)
                    triangles += primitive.indices.size() / 3;
            if (!scene_desc.shadow_proxy_errors.empty() && triangles >= scene_desc.shadow_proxy_min_triangles)
            {
                renderer.pending_shadow_proxies = std::make_shared<std::vector<std::pair<StaticMesh, float>>>();
                const glm::vec3 scale = glm::abs(object.transform.scale.glm());
                const float mesh_scale = std::max({ scale.x, scale.y, scale.z, 1e-6f });
                context.dc->push(std::async(std::launch::async,
                    [proxies = renderer.pending_shadow_proxies, mesh_data = &renderer.mesh.get(), mesh_scale,
                     errors = scene_desc.shadow_proxy_errors]
                    {
                        for (float error : errors)
                            proxies->emplace_back(build_shadow_proxy(*mesh_data, error / mesh_scale), error);
                    }).share());
            }
            registry.add<MeshRenderer>(child, std::move(renderer));
        }

        for (auto& load : texture_loads)
            context.dc->push(std::async(std::launch::async, [load] { load.wait(); }).share());

        LogImportGltf.Log("Scene '%s': %zu meshes", scene_desc.asset_path.c_str(), scene.objects.size());
    }

    SCENE_REGISTER_COMPONENTS(GltfScene)
}
