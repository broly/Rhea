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
    for (RenderPrimitiveId prim_index : ro.primitives)
    {
        RenderPrimitive& rp = primitives[prim_index];
        rp.passes.clear();
        rp.info_by_pass.clear();
        rp.skinning.reset();
    }
    ro.primitives.clear();
}



void SceneViewProcessor_Mesh::process()
{
    auto& renderer = *RhGlobals::engine->renderer;

    constexpr RTBuildMode rt_build_mode = render_settings::enable_raytracing ? RTBuildMode::build_blas : RTBuildMode::none;

    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Mesh>())
    {
        
        auto& ro = meshes[submitted.render_id.identifier];

        // submitted before its proxy was unregistered (in the same frame), or by a previous owner of the slot
        if (!ro.alive || ro.generation != submitted.render_id.generation)
            continue;

        dirty = true;

        bool is_new = ro.primitives.empty();
        bool mesh_changed = ro.mesh != submitted.mesh;

        glm::mat4 new_world = submitted.transform.matrix();
        bool transform_changed = ro.world != new_world;

        if (is_new || mesh_changed)
        {
            retire_primitives(ro);

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
                        submitted.mesh, geom, prim_index };
                    rp.world = &ro.world;
                    rp.bounds = ro.bounds;

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
                        info.shader_key = geom_pipeline_family->make_shader_key(pass_name, instance->material);
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
                        
                    // todo: temp. exact pass is bad idea
                    auto mat_instance_TODO_EXACT_PASS =
                            renderer.query_material_instance(
                                submitted.materials[mat_index], "GeometryBase");
                    
                    // shadow instances are never filled with parameters (see above):
                    // shadow pipelines which alpha test read the base pass material
                    if (auto shadow_it = rp.info_by_pass.find("ShadowMap"); shadow_it != rp.info_by_pass.end())
                        shadow_it->second.material_index = mat_instance_TODO_EXACT_PASS->material_id;
                        
                    rp.primitive_material_id = mat_instance_TODO_EXACT_PASS->material_id;
                        
                    rp.debug_texture_name = 
                    rp.id = render_primitive_id_counter++;
                    primitives.push_back(rp);
                    
                    ro.prev_world = ro.world;
                    write_primitive_info(ro, rp);
                    
                        
                    ro.primitives.push_back(primitives.size() - 1);
                }
            }

            continue;
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

void SceneViewProcessor_Mesh::write_primitive_info(const RenderObject_Mesh& ro, const RenderPrimitive& rp)
{
    if (primitive_table_cpu.size() <= rp.id)
        primitive_table_cpu.resize(rp.id + 1, GPUPrimitiveInfo{ glm::mat4(1.0f), glm::mat4(1.0f), 0, 0 });
    primitive_table_cpu[rp.id] = GPUPrimitiveInfo {
        ro.world,
        ro.prev_world,
        (uint32_t)rp.mesh_index,
        rp.primitive_material_id
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
