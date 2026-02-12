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
    auto& renderer = *RhGlobals::engine->renderer;

    static std::map<BlendMode, Name> pass_by_blend_mode = {
        {BlendMode::opaque, "GeometryBase"},
        {BlendMode::translucent, "GeometryTranslucent"}
    };
    for (const auto& submitted :
         read_submission_buffer<SceneViewProxy_Mesh>())
    {
        auto& ro = meshes[submitted.render_id.identifier];

        bool is_new = ro.primitives.empty();
        bool mesh_changed = ro.mesh != submitted.mesh;

        glm::mat4 new_world = submitted.transform.matrix();
        bool transform_changed = ro.world != new_world;

        if (is_new || mesh_changed)
        {
            ro.primitives.clear();

            ro.mesh   = submitted.mesh;
            ro.world  = new_world;
            ro.bounds = submitted.mesh.get().bounds;

            const auto& mesh = submitted.mesh.get();

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
                    
                    auto material = submitted.materials[mat_index];
                    
                    auto blend_mode = material->get_enum_parameter<BlendMode>("blend_mode");

                    auto instance =
                        renderer.query_material_instance(
                            submitted.materials[mat_index], pass_by_blend_mode[blend_mode]);

                    RenderPrimitive rp{};
                    rp.mesh = MeshPrimHandle{
                        submitted.mesh, geom, prim_index };
                    rp.world = &ro.world;
                    rp.bounds = ro.bounds;
                    rp.material_instance = instance;

                    Name pass_name;
        
                    if (blend_mode == BlendMode::opaque)
                        pass_name = "GeometryBase";
                    else if (blend_mode == BlendMode::translucent)
                        pass_name = "GeometryTranslucent";

                    auto model =
                        renderer.find_model(instance->material->model);

                    auto geom_pipeline_family =
                        renderer.query_pipeline_family(pass_name, model);

                    auto pipeline =
                        geom_pipeline_family->request_pipeline(
                            geom_pipeline_family->make_shader_key(
                                instance->material,
                                pass_name));
                    
                    
                    rp.pipeline = pipeline;
                    rp.pipeline_family = geom_pipeline_family;
                    rp.pass_name = pass_name;
                    
                    
                    auto shadow_model =
                        renderer.find_model("shadow");
                    auto shadow_pipeline_family = renderer.query_pipeline_family("shadowmap", shadow_model);
                    rp.shadow_pipeline_family = shadow_pipeline_family;
                    rp.shadow_pipeline = shadow_pipeline_family->request_pipeline({});
                    
                    primitives.push_back(rp);
                    ro.primitives.push_back(primitives.size() - 1);
                    
                    renderer.get_backend()->get_or_create_mesh_buffers(rp.mesh);
                }
            }

            continue;
        }

        if (transform_changed)
            ro.world = new_world;
    }
}
