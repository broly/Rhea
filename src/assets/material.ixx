export module assets:material;

import <variant>;
import linear_color;
import :material_parameter_type;
import <future>;
import rhobject;
#include "object/object_reflection_macro.h"


export using ShaderOptionValue = std::variant<bool, Name>;

export void serialize_json_value(MaterialParameterType& target, const Json::Value& value, DependencyCollector* dc);

export class Material : public RhObject
{
public:
    Name model;
    std::map<Name, MaterialParameterType> parameters;
    
    template<typename T>
    void static_set_parameter(std::string_view key, T&& value)
    {
        parameters[key] = MaterialParameterType(value);
    }
    
    Name get_enum_parameter(Name key);


    std::map<Name, ShaderOptionValue> get_shader_options(Name pass_name) const;
    
};
REFLECT_OBJECT_FIELDS(Material, RhObject,
    model, parameters);