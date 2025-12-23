#pragma once

#include <cassert>
#include <functional>
#include <memory>
#include <optional>
#include <string_view>
#include <json/value.h>
#include <set>

#include "common/reflect.h"


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

#define REFLECT_OBJECT(Name, Base, ...) \
    namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                std::nullopt \
            ); \
        REFL_OBJECT_TRAITS(Name, Base) \
    } \
    
inline void serialize_json_value(Json::Int& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt();
}
inline void serialize_json_value(Json::UInt& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt();
}
inline void serialize_json_value(Json::Int64& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt64();
}
inline void serialize_json_value(Json::UInt64& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt64();
}
inline void serialize_json_value(std::string& target, const Json::Value& value)
{
    assert(value.isString());
    target = value.asString();
}






inline void serialize_json_value(float& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asFloat();
}
inline void serialize_json_value(double& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asDouble();
}
inline void serialize_json_value(bool& target, const Json::Value& value)
{
    assert(value.isBool());
    target = value.asBool();
}
    
namespace reflect::json
{
    
    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, bool is_loading);

    template<typename T>
    void do_serialize_json_value(T& target, const Json::Value& value, bool is_loading)
    {
        if constexpr (reflect::is_reflected_v<T>)
        {
            assert(value.isObject());
            visit_serialize(value, target, is_loading);
        } else
        {
            static_assert(requires { serialize_json_value(target, value); });
            serialize_json_value(target, value);
        }
    }

    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, bool is_loading)
    {
        reflect::visit<T>([&] <auto PtrToField, FixedString Name> ()
        {
            const std::string name = std::string(Name); // todo: inefficient
            if (Json::Value const* json_value = json_object.find(std::string(name)))
            {
                auto& field_ref = struct_ref.*PtrToField;
                do_serialize_json_value(field_ref, *json_value, is_loading);
            }
        });
    }
}



#define REFLECT_OBJECT_FIELDS(Name, Base, ...) \
    REFLECT_STRUCT(Name, __VA_ARGS__); \
    namespace reflect_inner { \
        static_assert(std::is_base_of<RhObject, Name>::value, "Can't reflect non-RhObject types");\
        const bool Name##_registered = \
            reflect::register_actor_class<Name>(\
                #Name, \
                    [] (const Json::Value& json_object, RhObject* ObjPtr, bool is_loading) -> bool { \
                        using Class = Name;\
                        auto CastedObjPtr = reinterpret_cast<Name*>(ObjPtr); \
                        reflect::json::visit_serialize(json_object, *CastedObjPtr, is_loading); \
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
        
        template<typename T>
        std::shared_ptr<T> instantiate() const;
    };

    extern void register_object_class_impl(
        std::string_view name, 
        ObjectFactoryType&& factory, 
        std::set<std::string_view>&& bases,
        std::optional<JsonSerializer> serializer);
    
    extern const ObjectReflectionInfo* find_object_reflection_info(std::string_view name);
        
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
    
    
    template <typename T>
    std::shared_ptr<T> ObjectReflectionInfo::instantiate() const
    {
        auto type_id = reflect::get_object_type_id<T>();
        assert(bases.contains(type_id));
        std::string obj_name = std::string(name) + "_inst";
        ObjectInitData init_data;
        init_data.name = obj_name;
        auto object = (factory)(init_data);
        return std::static_pointer_cast<T>(object);
    }

    
}


