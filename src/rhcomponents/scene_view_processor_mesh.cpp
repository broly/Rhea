module rhcomponents:scene_view_proxy.mesh;

import render_scene;
import framework;
import globals;
import profile;
import render;
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
    
    dirty = true;
    
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

    RenderResource& primitive_table_resource = renderer.find_resource_checked("primitive_table");

    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Mesh>())
    {
        
        dirty = true;
        
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
            ro.bounds = submitted.bounds;

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


                    RenderPrimitive rp{};
                    rp.mesh = MeshPrimHandle{
                        submitted.mesh, geom, prim_index };
                    rp.world = &ro.world;
                    rp.bounds = ro.bounds;

                    std::set<Name> passes;
        
                    if (blend_mode == BlendMode::opaque)
                    {
                        passes.emplace("GeometryBase");
                        // passes.emplace("DepthPrepass");
                    }
                    else if (blend_mode == BlendMode::translucent)
                         passes.emplace("GeometryTranslucent");
                    
                    if (blend_mode != BlendMode::translucent)
                        passes.emplace("ShadowMap");

                    auto model =
                        renderer.find_model(material->model);
                    
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
                        info.shader_key = geom_pipeline_family->make_shader_key(instance->material, pass_name);
                        info.pipeline = geom_pipeline_family->request_pipeline(info.shader_key);
                        info.material_instance = instance;
                        if (pass_name != "shadowmap")
                            instance->apply_material_parameters();
                    
                    
                        info.material_index = instance->material_id;
                    }
                    
                    auto result = renderer.get_backend()->get_or_create_mesh_buffers(rp.mesh, RTBuildMode::build_blas);
                        
                    rp.mesh_index = result.mesh_index;
                        
                    rp.debug_texture_name = 
                    rp.id = render_primitive_id_counter++;
                    primitives.push_back(rp);
                    
                    // todo: temp. exact pass is bad idea
                    auto mat_instance_TODO_EXACT_PASS =
                            renderer.query_material_instance(
                                submitted.materials[mat_index], "GeometryBase");
                    
                    const GPUPrimitiveInfo primitive_info {
                        ro.world,
                        ro.world, // todo: make previous frame support
                        (uint32_t)rp.mesh_index,
                        mat_instance_TODO_EXACT_PASS->material_id
                        
                    };
                    primitive_table_resource.update_ssbo_element("u_primitive_table", sizeof(GPUPrimitiveInfo), rp.id, &primitive_info);
                    
                        
                    ro.primitives.push_back(primitives.size() - 1);
                }
            }

            continue;
        }

        if (transform_changed)
            ro.world = new_world;
    }
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
    

    for (const auto& prim : primitives)
    {
        // Frustum culling
        if (!frustum.test_aabb_world(prim.bounds))
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
