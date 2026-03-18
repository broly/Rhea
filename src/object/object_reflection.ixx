export module rhobject:reflection;

import <cassert>;
import <functional>;
import <memory>;
import <optional>;
import <string_view>;
import <json/value.h>;
import <set>;
import reflect;
import fixed_string;
import enum_helpers;

import :object;
import dependency_collector;
import name;
import static_name;
import container_traits;
import type_utils;
import <variant>;
import <set>;
#include "common/assertion_macros.h"
#include "common/reflect_macros.h"



export struct SerializationContext
{
    DependencyCollector* dc = nullptr;
    bool is_loading = true;
    bool strict_checking_enabled = false;
    std::optional<std::string> current_file = std::nullopt;
};
    
export inline void serialize_json_value(Json::Int& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt();
}
export inline void serialize_json_value(Name& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isString());
    target = value.asString();
}

export inline void serialize_json_value(uint16_t& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt();
}
export inline void serialize_json_value(Json::UInt& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt();
}
export inline void serialize_json_value(Json::Int64& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt64();
}
export inline void serialize_json_value(Json::UInt64& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt64();
}
export inline void serialize_json_value(std::string& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isString());
    target = value.asString();
}


export inline void serialize_json_value(float& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asFloat();
}
export inline void serialize_json_value(double& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asDouble();
}
export inline void serialize_json_value(bool& target, const Json::Value& value, const SerializationContext& context)
{
    assert(value.isBool());
    target = value.asBool();
}


export namespace reflect_inner
{
    template<typename T>
    struct RhObjectTraits;
    
    
    template<typename Class, typename Base>
    constexpr std::set<std::string_view> get_bases_impl(std::string_view ClassName)
    {
        std::set<std::string_view> current_bases;
        if constexpr (!std::is_same_v<Class, RhObject> && !std::is_same_v<Base, void>) { 
            current_bases.merge(RhObjectTraits<Base>::get_bases()); 
        }
        current_bases.emplace(ClassName); 
        return current_bases; 
    }
}

export namespace reflect
{
    using ObjectFactoryType = std::function<std::shared_ptr<RhObject>(const ObjectInitData& init_data)>;
    using UniqueObjectFactoryType = std::function<std::unique_ptr<RhObject>(const ObjectInitData& init_data)>;
    using JsonSerializer = std::function<bool(const Json::Value&, RhObject* Ptr, const SerializationContext& context)>;
    
    struct ObjectReflectionInfo
    {
        std::string_view name;
        std::set<std::string_view> bases;
    
        ObjectFactoryType factory;
        UniqueObjectFactoryType unique_factory;
        std::optional<JsonSerializer> serializer;
        bool is_abstract;
        
        template<typename T>
        std::shared_ptr<T> instantiate() const;
        
        template<typename T>
        std::unique_ptr<T> instantiate_unique() const;
        
        void instantiate_default();
        
        std::unique_ptr<RhObject> default_object = nullptr;
    };
    template<typename T>
    std::string_view get_object_type_name()
    {
        return reflect_inner::RhObjectTraits<T>::type_name;
    }
    
    extern const ObjectReflectionInfo* find_object_reflection_info(Name name);
}

    
export namespace reflect::json
{
    
    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, const SerializationContext& context);

    template<typename T>
    void do_serialize_json_value(T& target, const Json::Value& value, const SerializationContext& context)
    {
        if constexpr (std::is_enum_v<T> && reflect::is_reflected_v<T>)
        {
            std::string value_str = value.asString();
            checkf(reflect::is_valid_enum_name<T>(value_str), "Wrong member name '%s' for enum '%s'",
                            value_str.c_str(), reflect::get_name<T>().to_string().c_str());
            
            target = reflect::ReflectionInfo<T>::template enum_name_to_value(value.asString());
        } else if constexpr (is_shared_ptr_v<T>)
        {
            auto type_id = reflect::get_object_type_name<typename T::element_type>();
            auto info = reflect::find_object_reflection_info(type_id);
            target = info->template instantiate<typename T::element_type>();
            do_serialize_json_value(*target, value, context);
        } else if constexpr (is_variant_v<T>)
        {
            checkf(value.isObject(), "Variant supports only JSON objects");

            Json::Value const* type_name_ptr = value.find("__type__");
            checkf(type_name_ptr != nullptr, "__type__ should be provided for variant fields");
            checkf(type_name_ptr->isString(), "__type__ must be string");
            
            const Name type_name = type_name_ptr->asString();
            
            visit_variadic_types(target, [&] <typename U> () {
                if (type_name == reflect::get_name<U>())
                {
                    auto& variant_target_value = target.template emplace<U>(U{});
                    do_serialize_json_value(variant_target_value, value, context);
                }
            });
        }
        else if constexpr (reflect::is_reflected_v<T>)
        {
            assert(value.isObject());
            auto names = value.getMemberNames();
            visit_serialize(value, target, context);
        } else if constexpr (is_vector_v<std::decay_t<T>>)
        {
            assert(value.isArray());
            target.clear();
            for (auto& json_item : value)
            {
                typename T::value_type array_item;
                // serialize_json_value(array_item, json_item);
                target.push_back(array_item);
                do_serialize_json_value(target.back(), json_item, context);
            }
        } else if constexpr (is_set_v<std::decay_t<T>>)
        {
            assert(value.isArray());
            target.clear();
            for (auto& json_item : value)
            {
                using value_type = typename T::value_type;
                value_type set_item;
                if constexpr (std::is_enum_v<value_type> && is_reflected_v<value_type>)
                {
                    // todo: crutch
                    set_item = reflect::ReflectionInfo<value_type>::template enum_name_to_value(json_item.asString());
                } else
                {
                    serialize_json_value(set_item, json_item, context);
                }
                target.emplace(set_item);
            }
        } else if constexpr (is_mask_v<std::decay_t<T>>)
        {
            assert(value.isArray());
            target = 0;
            for (auto& json_item : value)
            {
                using value_type = typename T::enum_type;
                value_type enum_value;
                static_assert(std::is_enum_v<value_type> && is_reflected_v<value_type>);
                enum_value = reflect::ReflectionInfo<value_type>::template enum_name_to_value(json_item.asString());
                target |= enum_value;
            }
        }else if constexpr (is_optional_v<std::decay_t<T>>)
        {
            if (value.isNull())
            {
                target.reset();
            }
            else
            {
                typename T::value_type item;
                target.emplace(item);
                do_serialize_json_value(target.value(), value, context);
            }
        } else if constexpr (is_map_v<std::decay_t<T>>)
        {
            assert(value.isObject());
            auto member_names = value.getMemberNames();
            
            using KEY = T::key_type;
            
            static_assert(
                std::is_same_v<KEY, std::string> || 
                std::is_same_v<KEY, Name> ||
                (std::is_enum_v<KEY> && reflect::is_reflected_v<KEY>));
            
            for (auto& member_name : member_names)
            {
                typename T::mapped_type map_value;
                
                auto from_string = [] (const std::string& name) -> KEY
                {
                    if constexpr (std::is_enum_v<KEY>)
                    {
                        checkf(reflect::is_valid_enum_name<KEY>(name), "Wrong member name '%s' for enum '%s'",
                            name.c_str(), reflect::get_name<KEY>().to_string().c_str());
                        return reflect::name_to_enum<KEY>(name);
                    }
                    else
                    {
                        return std::string(name);
                    }
                };
            
                KEY key = from_string(member_name);
            
                auto [it, inserted] = target.try_emplace(key, map_value);
            
                auto& json_value = value[member_name];
            
                do_serialize_json_value(it->second, json_value, context);
            }
        } else if constexpr (requires { serialize_json_value(target, value, context); })
        {
            // visit_serialize(value, target, is_loading);
            // if constexpr (!requires { serialize_json_value(target, value); })
            // {
            //     ReflectionInfo<T>::reflected;
            // }
            // static_assert(requires { serialize_json_value(target, value); });
            serialize_json_value(target, value, context);
        }
    }

    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, const SerializationContext& context)
    {
        reflect::visit<T>([&] <auto PtrToField, FixedString Name> ()
        {
            const std::string name = std::string(Name); // todo: inefficient
            Json::Value const* json_value = json_object.find(std::string(name));
            
            if (is_optional_v<std::decay_t<decltype(struct_ref.*PtrToField)>> && !json_value)
                return;
            
            if (context.strict_checking_enabled)
            {
                checkf(context.current_file.has_value(), "Strict checking mode not supported for non-file serialization");
                checkf(json_value, "During parsing '%s', required field '%s' is missing", 
                    context.current_file->c_str(), 
                    name.c_str());
            }
            if (!json_value)
                return;
            auto& field_ref = struct_ref.*PtrToField;
            do_serialize_json_value(field_ref, *json_value, context);
            
        });
    }
}

export inline bool convert_from_string(int& target, std::string value)
{
    if (value.empty())
        target = atoi(value.c_str());
    return true;
}

export inline bool convert_from_string(std::string& target, std::string value)
{
    target = value;
    return true;
}

export inline bool convert_from_string(double& target, std::string value)
{
    if (value.empty())
        target = atof(value.c_str());
    return true;
}

export inline bool convert_from_string(float& target, std::string value)
{
    if (value.empty())
        target = atof(value.c_str());
    return true;
}

export namespace reflect
{

    extern void register_object_class_impl(
        std::string_view name, 
        ObjectFactoryType&& factory, 
        UniqueObjectFactoryType&& unique_factory, 
        std::set<std::string_view>&& bases,
        std::optional<JsonSerializer> serializer,
        bool is_abstract);
        
    template <typename T>
    inline bool register_object_class(std::string_view name, std::optional<JsonSerializer>&& Serializer)
    {
        ObjectFactoryType factory = [name](const ObjectInitData& init_data) -> std::shared_ptr<T>
        {
            if constexpr (!std::is_abstract_v<T>)
            {
                auto object = std::make_shared<T>();
                object->init(name, init_data);
                return object;
            }
            unreachable("Could not create object from abstract class");
        };
        UniqueObjectFactoryType unique_factory = [name](const ObjectInitData& init_data) -> std::unique_ptr<T>
        {
            if constexpr (!std::is_abstract_v<T>)
            {
                auto object = std::make_unique<T>();
                object->init(name, init_data);
                return object;
            }
            unreachable("Could not create object from abstract class");
        };
        std::set<std::string_view> class_bases = reflect_inner::RhObjectTraits<T>::get_bases();
        register_object_class_impl(
            name, 
            std::move(factory), 
            std::move(unique_factory), 
            std::move(class_bases), 
            std::move(Serializer), 
            std::is_abstract_v<T>);
        return true;
    }
    
    template<typename T>
    const std::set<Name>& get_object_bases()
    {
        return reflect_inner::RhObjectTraits<T>::bases;
    }
    
    
    template <typename T>
    std::shared_ptr<T> ObjectReflectionInfo::instantiate() const
    {
        auto type_id = reflect::get_object_type_name<T>();
        assert(bases.contains(type_id));
        std::string obj_name = std::string(name) + "_inst";
        ObjectInitData init_data;
        init_data.name = obj_name;
        auto object = (factory)(init_data);
        return std::static_pointer_cast<T>(object);
    }
    
    template <typename T>
    std::unique_ptr<T> ObjectReflectionInfo::instantiate_unique() const
    {
        auto type_id = reflect::get_object_type_name<T>();
        assert(bases.contains(type_id));
        std::string obj_name = std::string(name) + "_inst";
        ObjectInitData init_data;
        init_data.name = obj_name;
        auto object = (unique_factory)(init_data);
        return std::unique_ptr<T>{static_cast<T*>(object.release())};
    }
    
    
    std::vector<const ObjectReflectionInfo*> get_subtypes(std::string_view type_name, bool include_parent = false);
    
    template<typename T>
    std::vector<const ObjectReflectionInfo*> get_subtypes(bool include_parent = false)
    {
        std::string_view type_name = get_object_type_name<T>();
        return get_subtypes(type_name);
    }
    
    template<typename T>
    const ObjectReflectionInfo* find_info()
    {
        auto type_name = get_object_type_name<T>();
        return find_object_reflection_info(type_name);
    }
    
    template<typename T>
    const T* get_default()
    {
        auto info = find_info<T>();
        return (const T*)info->default_object.get();
    }
    
    void create_defaults();
    
}

export template<typename T = RhObject, typename... Ts>
std::shared_ptr<T> new_object(Ts&&... vs)
{
    auto info = reflect::find_info<T>();
    auto obj = info->template instantiate<T>();
    obj->ctor(std::forward<Ts>(vs)...);
    
    return obj;
}
