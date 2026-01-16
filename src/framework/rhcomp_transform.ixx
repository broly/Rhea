export module framework:rhcomp_transform;

import :rhcomponent;

import rhmath;
import assets;

#include "object/object_reflection_macro.h"

struct RhCompRenderInfo
{
    ptrdiff_t scene_proxy_offset = -1;
    bool is_explicitly_null = false;
    size_t type_id;
};

export class RhComp_Transform : public RhComponent
{
public:
    
    
    bool has_virtual_transform_callback = false;
    bool renderable = false;
    
    Transform transform;
    
    
    virtual void on_transform_changed();
    void set_transform(const Transform& in_transform);
    Transform get_transform();
    
    void render_submit() const;
    
    void* get_scene_proxy_address() const;
    
    template<typename T>
    T& get_typed_scene_proxy_address() const
    {
        return *(T*)get_scene_proxy_address();
    }
    
    RhCompRenderInfo render_info;
};

REFLECT_OBJECT_FIELDS(RhComp_Transform, RhComponent, 
    transform);