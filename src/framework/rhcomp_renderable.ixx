export module framework:rhcomp_renderable;

import :rhcomponent;
import :rhcomp_transform;

import rhmath;
import assets;

import :render_id;

#include "object/object_reflection_macro.h"



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

