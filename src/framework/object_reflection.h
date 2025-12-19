#pragma once

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string_view>
#include <json/value.h>
#include <set>
#include "common/foreach_macro.h"

#define REG_REFLECT_SERIALIZE_MEMBER(member_name) \
    if (!reflect::read_member_from_json<&Class::member_name>(json_object, #member_name, CastedObjPtr, is_loading)) \
        return false;

class RhObject;

namespace reflect_inner
{
    template<typename T>
    struct RhObjectTraits;
    
    
    template<typename Class, typename Base>
    std::set<std::string_view> get_bases_impl(std::string_view ClassName)
    {
        std::set<std::string_view> current_bases;
        if constexpr (!std::is_same_v<Class, ::RhObject> && !std::is_same_v<Base, void>) { 
            current_bases.merge(RhObjectTraits<Base>::get_bases()); 
        }
        current_bases.emplace(ClassName); 
        return current_bases; 
    }
}


#define REFL_OBJECT_TRAITS(Name, Base) \
        template<> \
        struct RhObjectTraits<Name> {\
            static constexpr std::string_view type_id = #Name; \
            static std::set<std::string_view> get_bases() {\
                return get_bases_impl<Name, Base>(#Name); \
            }\
            inline static std::set<std::string_view> bases = RhObjectTraits<Name>::get_bases(); \
        }; \

#define REG_REFLECT(Name, Base, ...) \
    namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                std::nullopt \
            ); \
        REFL_OBJECT_TRAITS(Name, Base) \
    } \

#define REG_REFLECT_ARGS(Name, Base, ...) \
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
        REFL_OBJECT_TRAITS(Name, Base) \
    } \


inline bool convert_from_string(int& target, std::string value)
{
    if (value.empty())
        target = atoi(value.c_str());
    return true;
}

inline bool convert_from_string(std::string& target, std::string value)
{
    target = value;
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

struct ObjectInitData
{
    std::string name;
};

namespace reflect
{
    using ObjectFactoryType = std::function<std::shared_ptr<RhObject>(const ObjectInitData& init_data)>;
    using JsonSerializer = std::function<bool(const Json::Value&, RhObject* Ptr, bool is_loading)>;
    
    struct ObjectReflectionInfo
    {
        std::string_view name;
        std::set<std::string_view> bases;
    
        ObjectFactoryType factory;
        std::optional<JsonSerializer> serializer;
    };

    extern void register_object_class_impl(
        std::string_view name, 
        ObjectFactoryType&& factory, 
        std::set<std::string_view>&& bases,
        std::optional<JsonSerializer> serializer);
    
    extern const ObjectReflectionInfo* find_object_reflection_info(std::string_view name);
    
    inline std::string json_val_to_str(const Json::Value& value) {
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
            auto string_value = json_val_to_str(*json_field_value);
            convert_from_string(*value_ptr, string_value);
        }
        return true;
    }
    
    
    template <typename T>
    inline bool register_actor_class(std::string_view name, std::optional<JsonSerializer>&& Serializer)
    {
        ObjectFactoryType factory = [name](const ObjectInitData& init_data)
        {
            auto object = std::make_shared<T>();
            object->set_type_id(name);
            object->set_name(init_data.name);
            return object;
        };
        std::set<std::string_view> class_bases = reflect_inner::RhObjectTraits<T>::get_bases();
        register_object_class_impl(name, std::move(factory), std::move(class_bases), std::move(Serializer));
        return true;
    }
    
    template<typename T>
    std::string_view get_object_type_id()
    {
        return reflect_inner::RhObjectTraits<T>::type_id;
    }
    
    template<typename T>
    const std::set<std::string_view>& get_object_bases()
    {
        return reflect_inner::RhObjectTraits<T>::bases;
    }
    
}


