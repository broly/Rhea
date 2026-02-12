export module assets:material;

import <variant>;
import linear_color;
import :material_parameter_type;
import <future>;
import rhobject;
#include "common/assertion_macros.h"
#include "object/object_reflection_macro.h"


export using ShaderOptionValue = std::variant<bool, Name>;

export void serialize_json_value(MaterialParameterType& target, const Json::Value& value, DependencyCollector* dc);

export enum class BlendMode
{
    opaque,
    translucent,
    masked,
};
REFLECT_ENUM(BlendMode,
    opaque, translucent, masked);


export class Material : public RhObject
{
public:
    Name model;
    std::map<Name, MaterialParameterType> parameters;
    
    void ctor() {}
    
    template<typename T>
    void static_set_parameter(std::string_view key, T&& value)
    {
        parameters[key] = MaterialParameterType(value);
    }
    
    Name get_enum_parameter(Name key) const;
    
    template<typename T>
    T get_enum_parameter(Name key) const
    {
        const auto& enum_member_name = get_enum_parameter(key);
        checkf(reflect::is_valid_enum_name<T>(enum_member_name), "No such enum member name %s", 
            enum_member_name.to_string().c_str());
        return reflect::name_to_enum<T>(enum_member_name);
    }


    std::map<Name, ShaderOptionValue> get_shader_options(Name pass_name) const;
    
};
REFLECT_OBJECT_FIELDS(Material, RhObject,
    model, parameters);