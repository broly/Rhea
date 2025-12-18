#pragma once

#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <json/value.h>

#include "common/foreach_macro.h"

#define REG_REFLECT_SERIALIZE_MEMBER(member_name) \
    if (!reflect::read_member_from_json<&Class::member_name>(json_object, #member_name, CastedObjPtr, is_loading)) \
        return false;

#define REG_REFLECT(Name, ...) \
    namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                std::nullopt \
            ); \
    }

#define REG_REFLECT_ARGS(Name, ...) \
    namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                    [] (const Json::Value& json_object, RhObject* ObjPtr, bool is_loading) -> bool { \
                        using Class = Name;\
                        auto CastedObjPtr = reinterpret_cast<Name*>(ObjPtr); \
                        RHEA_FOR_EACH(REG_REFLECT_SERIALIZE_MEMBER,##__VA_ARGS__); \
                        return true; \
                    }\
                ); \
    }
    

class RhObject;

inline bool convert_from_string(int& target, std::string value)
{
    if (value.empty())
        target = atoi(value.c_str());
    return true;
}

inline bool convert_from_string(double& target, std::string value)
{
    if (value.empty())
        target = atof(value.c_str());
    return true;
}

inline bool convert_from_string(float& target, std::string value)
{
    if (value.empty())
        target = atof(value.c_str());
    return true;
}

namespace reflect
{
    using ObjectFactoryType = std::function<std::shared_ptr<RhObject>()>;
    using JsonSerializer = std::function<bool(const Json::Value&, RhObject* Ptr, bool is_loading)>;
    
    struct ObjectReflectionInfo
    {
        std::string_view name;
    
        ObjectFactoryType factory;
        std::optional<JsonSerializer> serializer;
    };

    extern void register_object_class_impl(std::string_view name, ObjectFactoryType&& factory, std::optional<JsonSerializer> serializer);
    
    extern const ObjectFactoryType* find_actor_factory(std::string_view name);
    extern const ObjectReflectionInfo* find_object_reflection_info(std::string_view name);
    
    inline std::string jsonValueToString(const Json::Value& value) {
        if (value.isString()) {
            return value.asString();
        } else if (value.isNumeric()) {

            if (value.isInt()) {
                return std::to_string(value.asInt());
            } else if (value.isUInt()) {
                return std::to_string(value.asUInt());
            } else if (value.isInt64()) {
                return std::to_string(value.asInt64());
            } else if (value.isDouble()) {

                std::ostringstream oss;
                oss << value.asDouble();
                return oss.str();
            } else if (value.isBool()) {
                return value.asBool() ? "true" : "false";
            }
        } else if (value.isBool()) {
            return value.asBool() ? "true" : "false";
        } else if (value.isNull()) {
            return "null";
        }
    

        return value.toStyledString();
    }
    
    template<auto PtrToField, typename ObjType>
    inline bool read_member_from_json(const Json::Value& json_value, std::string_view field_name, ObjType* object_ptr, bool is_loading)
    {
        auto* value_ptr = &std::invoke(PtrToField, object_ptr);
        
        std::string field_name_str = std::string(field_name);
        if (auto json_field_value = json_value.find(field_name_str))
        {
            auto string_value = jsonValueToString(*json_field_value);
            convert_from_string(*value_ptr, string_value);
        }
        return true;
    }
    
    
    template <typename T>
    inline bool register_actor_class(std::string_view name, std::optional<JsonSerializer>&& Serializer)
    {
        ObjectFactoryType factory = []() { return std::make_shared<T>(); };
        register_object_class_impl(name, std::move(factory), std::move(Serializer));
        return true;
    }
}


