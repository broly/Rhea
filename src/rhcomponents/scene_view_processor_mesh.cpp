module rhcomponents:scene_view_proxy.mesh;

import render_scene;
import framework;
import globals;
import profile;

#include "common/assertion_macros.h"
#include "profiling/profile.h"


RenderId SceneViewProcessor_Mesh::register_proxy()
{
    if (!vacated_mesh_ids.empty())
    {
        RenderId reuse_id = vacated_mesh_ids.back();
        reuse_id.generation++;
        vacated_mesh_ids.pop_back();
        return reuse_id;
    }
    uint32_t identifier = meshes.size();
    meshes.push_back({});
    const RenderId render_id(identifier, 0);
    
    return render_id;
}

void SceneViewProcessor_Mesh::unregister_proxy(RenderId render_id)
{
    meshes[render_id.identifier] = {}; 
    vacated_mesh_ids.push_back(render_id);
}



void SceneViewProcessor_Mesh::process()
{
    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Mesh>())
    {
        auto& mesh_ro = meshes[submitted.render_id.identifier];
        
        auto renderer = RhGlobals::engine->renderer;  // crutch
        
        const MeshHandle mesh_handle = submitted.mesh;
        auto& mesh = mesh_handle.get();
        
        if (mesh_ro.material_keys.size() > 0 && mesh_ro.material_keys.size() < submitted.materials.size())
        {
            checkf(false, "unsupported materials changing behaviour");
        }
        
        mesh_ro.material_keys.resize(submitted.materials.size());
        mesh_ro.material_instances.resize(submitted.materials.size());
        mesh_ro.mat_instances.resize(submitted.mats.size());
        
        for (uint32_t index = 0; auto material : submitted.materials)
        {
            mesh_ro.material_keys[index] = MaterialKey::make_key(material);
            mesh_ro.material_instances[index] = get_or_create_material_resource(renderer->get_material_resource(), mesh_ro.material_keys[index]);
            index++;
        }
        
        for (uint32_t index = 0; auto material : submitted.mats)
        {
            // material->create_resource();
            // mesh_ro.mat_instances[index] = material->create_instance(renderer.get());
            // mesh_ro.mat_instances[index]->c
        }
        mesh_ro.mesh = mesh_handle;
        mesh_ro.bounds = mesh.bounds;
        mesh_ro.mesh = mesh_handle;
        mesh_ro.world = submitted.transform.matrix();
        mesh_ro.debug_name = submitted.debug_name;
    }
}


RenderResourceInstance* SceneViewProcessor_Mesh::get_or_create_material_resource(RenderResource* resource, const MaterialKey& key, RBFrameHandle frame_handle)
{
    PROFILE("SceneViewProcessor_Mesh::get_or_create_material_resource");
    auto it = material_cache.find(key);
    if (it != material_cache.end())
        return it->second;
    
    auto resource_instance = resource->create_instance();
    
    
    auto renderer = RhGlobals::engine->renderer;  // crutch
    renderer->update_material_resource(resource_instance, key, frame_handle);
    
    auto [new_it, _] = material_cache.emplace(key, resource_instance);
    return new_it->second;
}
