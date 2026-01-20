export module render:material;

import rhobject;
import <variant>;

import assets;
import linear_color;
import <future>;
import :render_resource;
import :renderer;
#include "object/object_reflection_macro.h"


struct MaterialParameterType
{
    std::variant<float, LinearColor, TextureHandle, Name> data;
};

export void serialize_json_value(MaterialParameterType& target, const Json::Value& value, DependencyCollector* dc);

export class Material : public RhObject
{
public:
    Name model;
    std::map<Name, MaterialParameterType> parameters;
    
    
    std::shared_ptr<class MaterialInstance> create_instance(Renderer* renderer) const;
    
};
REFLECT_OBJECT_FIELDS(Material, RhObject,
    model, parameters);