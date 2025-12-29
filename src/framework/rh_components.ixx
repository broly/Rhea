export module framework:rhcomponents;
import :rhcomponent;

import rhmath;
import assets;

#include "object/object_reflection_macro.h"

export class RhComp_Transform : public RhComponent
{
public:
    
    Transform transform;
    
    virtual void set_transform(const Transform& in_transform);
    Transform get_transform();
};

REFLECT_OBJECT_FIELDS(RhComp_Transform, RhComponent, 
    transform);



export struct RenderId
{
    std::uint64_t identifier;
    uint32_t generation;
};



export class RhComp_Renderable : public RhComp_Transform
{
public:
    void start() override;
    void finish() override;
    
    void set_transform(const Transform& in_transform) override;
    
    inline void mark_render_state_dirty()
    {
        render_state_dirty = true;
    }
    inline void mark_render_state_clear()
    {
        render_state_dirty = false;
    }
    inline bool is_render_state_dirty() const
    {
        return render_state_dirty;
    }
    
    bool render_state_dirty;
    
    RenderId render_id;
    
    RenderId get_render_id() const
    {
        return render_id;
    }
};

REFLECT_OBJECT(RhComp_Renderable, RhComp_Transform);



export class RhComp_StaticMesh : public RhComp_Renderable
{
public:    
    MeshHandle mesh;
    TextureHandle texture;
    PBRMaterial material;
};

REFLECT_OBJECT_FIELDS(RhComp_StaticMesh, RhComp_Renderable, 
    transform, mesh, texture, material);
