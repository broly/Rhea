#pragma once
#include "rhcomp_transform.h"
#include "framework/actor_component.h"
#include "render/scene_extractor.h"

class RhComp_Renderable : public RhComp_Transform
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
};

REG_REFLECT(RhComp_Renderable, RhComp_Transform);
