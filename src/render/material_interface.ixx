export module render:material_interface;

import rhobject;
#include "object/object_reflection_macro.h"

export enum class MaterialDomain
{
    Opaque = 0,
    Masked = 1,
    Transparent = 2,
};
REFLECT_ENUM(MaterialDomain,
    Opaque, Masked, Transparent);

export class MaterialInterface : public RhObject
{
public:
    MaterialDomain domain = MaterialDomain::Opaque;
};

REFLECT_OBJECT_FIELDS(MaterialInterface, RhObject,
    domain);