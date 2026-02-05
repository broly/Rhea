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
        
        mesh_ro.mats.resize(submitted.mats.size());
        mesh_ro.mats_instances.resize(submitted.mats.size());
        
        for (uint32_t index = 0; auto material : submitted.mats)
        {
            mesh_ro.mats[index] = submitted.mats[index];
            index++;
        }
        
        mesh_ro.mesh = mesh_handle;
        mesh_ro.bounds = mesh.bounds;
        mesh_ro.mesh = mesh_handle;
        mesh_ro.world = submitted.transform.matrix();
        mesh_ro.debug_name = submitted.debug_name;
    }
}