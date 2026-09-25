module;

#include <json/value.h>

export module assets:material;

import std.compat;
import assertions;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import linear_color;
import :material_parameter_type;
import rhobject;
#include "common/assertion_macros.h"
#include "object/object_reflection_macro.h"


export using ShaderOptionValue = std::variant<bool, Name>;

export void serialize_json_value(MaterialParameterType& target, const Json::Value& value, const SerializationContext& context);

export enum class BlendMode
{
    opaque,
    translucent,
    masked,
};


export class Material : public RhObject
{
public:
    [[=rh::serialize]] Name model;
    [[=rh::serialize]] std::map<Name, MaterialParameterType> parameters;
    
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
RH_OBJECT(Material)