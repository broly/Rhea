export module framework:rhcomp_renderable;

import :rhcomp_transform;
import rhmath;
import assets;


#include "object/object_reflection_macro.h"

export class RhComp_Renderable : public RhComp_Transform
{
public:
    RhComp_Renderable();
    
    void start() override;
    void finish() override;
    
    virtual AABB get_aabb() const;
    
    
};

REFLECT_OBJECT(RhComp_Renderable, RhComp_Transform);

