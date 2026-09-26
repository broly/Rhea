module;

#include <json/value.h>

export module rhcomponents:scene_view_proxy.mesh;

import std.compat;
import rhobject;
import assertions;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render_scene;
import glm;
import assets;
import :mesh;

import render;
import name;
import rhmath;
import render;

#include "common/assertion_macros.h"
#include "object/object_reflection_macro.h"


export using RenderPrimitiveId = uint32_t;

export struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;

    std::vector<RenderPrimitiveId> primitives;
    bool dirty;
    
    // world matrix of the previous frame (motion vectors)
    glm::mat4 prev_world = glm::mat4(1.0f);
    // moved this frame: prev_world must be synced once the object stops
    bool moved = false;

    // registered and the owner of this slot (render ids are reused with a new generation):
    // submissions of an unregistered / older owner are ignored
    bool alive = false;
    uint32_t generation = 0;
};

export struct RenderPrimitivePassInfo
{
    std::shared_ptr<MaterialInstance> material_instance;
    std::shared_ptr<PipelineFamily> pipeline_family;
    ShaderKey shader_key = {};
    PipelineObject* pipeline;
    uint32_t material_index;
};

export struct RenderPrimitive
{
    RenderPrimitiveId id;

    MeshPrimHandle mesh;
    const glm::mat4* world;
    AABB bounds;
    
    uint64_t mesh_index;
    // material written into the primitive table
    uint32_t primitive_material_id = 0;
    uint32_t debug_texture_id;
    std::string debug_texture_name;
    
    // ---- skinning (SkinnedMesh) ----
    std::shared_ptr<SkinningPose> skinning;
    std::optional<SkinnedMeshGPU> skinned;
    // pose version last written to the skinned vertices
    uint64_t skinned_pose_version = 0;
    
    bool is_skinned() const { return skinned.has_value(); }

    
    std::set<Name> passes;
    std::map<Name, RenderPrimitivePassInfo> info_by_pass;
    
    RBPipelineLayout get_pipeline_layout_by_pass(Name pass_name)
    {
        return info_by_pass.at(pass_name).pipeline_family->get_pipeline_layout();
    }
    
    
    const RenderPrimitivePassInfo* get_pass_info(Name pass_name) const
    {
        auto it = info_by_pass.find(pass_name);
        if (it != info_by_pass.end())
            return &it->second;
        return nullptr;
    }

    uint8_t dirty;
};

export struct ViewRenderItem
{
    const RenderPrimitive* primitive = nullptr;
    uint64_t sort_key = 0;
    
    auto get_sort_key() const
    {
        return sort_key;
    }
};

export class SceneViewProcessor_Mesh : public SceneViewProcessor
{
public:
    SceneViewProcessor_Mesh()
    {
        scene_proxy_size = scene_view_proxy_size_v<SceneViewProxy_Mesh>;
    }
    
    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;
    
    void gather_for_view(
        const glm::mat4& view_matrix,
        const Frustum& frustum,
        Name pass_name,
        std::vector<ViewRenderItem>& out_items);
    
    virtual void on_hot_reload() override;

    // deque: RenderPrimitive::world points into it, registering more meshes must not move them
    std::deque<RenderObject_Mesh> meshes;
    std::vector<RenderId> vacated_mesh_ids;

    // Stops drawing the object's primitives (their slots stay allocated: ids index the primitive table)
    void retire_primitives(RenderObject_Mesh& ro);
    
    std::vector<RenderPrimitive> primitives;
    
    std::vector<RenderPrimitiveId> free_primitive_ids;
    
    bool dirty = false;
    
    // Primitive table (u_primitive_table: transforms, mesh / material ids by RenderPrimitive::id) is kept on the
    // CPU and copied into the buffer of the frame being recorded (the resource is per frame in flight): writing
    // one shared buffer would change data a frame still in flight on the GPU is reading.
    void write_primitive_info(const RenderObject_Mesh& ro, const RenderPrimitive& rp);
    // Uploads the table into `frame`'s buffer if it changed since that buffer was written
    void upload_primitive_table(RenderResource& primitive_table, RBFrameHandle frame);

    std::vector<GPUPrimitiveInfo> primitive_table_cpu;
    uint64_t primitive_table_version = 1;
    std::vector<uint64_t> primitive_table_uploaded_version;   // per frame in flight
    
    bool is_dirty() const { return dirty; }
    
    void set_dirty(bool in_dirty)
    {
        dirty = in_dirty;
    }
    
    RenderPrimitiveId render_primitive_id_counter = 0;
    
    std::vector<uint32_t> moved_this_frame;
    std::vector<uint32_t> moved_last_frame;
};
RH_OBJECT(SceneViewProcessor_Mesh)