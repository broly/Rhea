export module rhcomponents:scene_view_proxy.mesh;

import render_scene;
import <cassert>;
import glm;
import <string>;
import assets;
import :rhcomp_mesh;

import render;
import name;
import rhmath;
import render;
import <unordered_map>;

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
};

export struct RenderPrimitivePassInfo
{
    std::shared_ptr<MaterialInstance> material_instance;
    std::shared_ptr<PipelineFamily> pipeline_family;
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
    uint32_t debug_texture_id;
    std::string debug_texture_name;

    
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

    std::vector<RenderObject_Mesh> meshes;
    std::vector<RenderId> vacated_mesh_ids;
    
    std::vector<RenderPrimitive> primitives;
    
    std::vector<RenderPrimitiveId> free_primitive_ids;
    
    bool dirty = false;
    
    bool is_dirty() const { return dirty; }
    
    void set_dirty(bool in_dirty)
    {
        dirty = in_dirty;
    }
    
    RenderPrimitiveId render_primitive_id_counter = 0;
};
REFLECT_OBJECT(SceneViewProcessor_Mesh, SceneViewProcessor);