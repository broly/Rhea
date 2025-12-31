module engine:scene_view_proxy.mesh;
import :scene_view;

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



void SceneViewProcessor_Mesh::extract()
{
    for (const auto& actor : view.world->actors)
    {
        auto mesh_comp = actor->find_component<RhComp_StaticMesh>();
        
        auto render_id = mesh_comp->get_render_id();
        
        if (mesh_comp->is_render_state_dirty())
        {
            auto& mesh_ro = meshes[render_id.identifier];
            MeshHandle mesh_handle = mesh_comp->mesh;
            auto& mesh_resource = mesh_handle.get();
            mesh_ro.material = view.get_or_create_material(MaterialKey::make_key(mesh_comp->material));
            mesh_ro.bounds = mesh_resource.bounds;
            mesh_ro.mesh = mesh_handle;
            mesh_ro.world = mesh_comp->get_transform().matrix();
            mesh_ro.debug_name = mesh_comp->name;
            mesh_comp->mark_render_state_clear();
        }
    }
}
