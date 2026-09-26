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

DEFINE_LOGGER(LogImportGltf, Log);

namespace
{
    void import_scene(World& world, ecs::Entity root, const SerializationContext& context)
    {
        ecs::Registry& registry = world.registry;
        const GltfScene scene_desc = *registry.get<GltfScene>(root);
        AssetSceneInfo scene = AssetManager::get().load_scene(scene_desc.asset_path, scene_desc.textures_dir);

        std::vector<std::shared_future<void>> texture_loads;
        for (uint32_t index = 0; index < scene.objects.size(); ++index)
        {
            AssetSceneObject& object = scene.objects[index];

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

            if (scene_desc.collision)
            {
                // cooked in parallel with the texture loads (or loaded from the cache); the body is created in PostLoad
                MeshCollider collider;
                collider.pending = std::make_shared<phys::Shape>();
                phys::PhysicsScene& physics = world.get_physics();
                const StaticMesh* mesh_data = &renderer.mesh.get();
                const glm::vec3 scale = object.transform.scale.glm();
                std::string cache_key = std::format("{}__{}_{}", scene_desc.asset_path, index, mesh_name);
                context.dc->push(std::async(std::launch::async,
                    [shape = collider.pending, mesh_data, scale, &physics, cache_key = std::move(cache_key)]
                    {
                        *shape = cook_mesh_collision(physics, *mesh_data, scale, cache_key);
                    }).share());
                registry.add<MeshCollider>(child, std::move(collider));
            }
            registry.add<MeshRenderer>(child, std::move(renderer));
        }

        for (auto& load : texture_loads)
            context.dc->push(std::async(std::launch::async, [load] { load.wait(); }).share());

        LogImportGltf.Log("Scene '%s': %zu meshes", scene_desc.asset_path.c_str(), scene.objects.size());
    }
}

void install_gltf_scene(World& world)
{
    scene::register_component_type<GltfScene>(import_scene);
}
