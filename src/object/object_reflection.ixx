module;

#include <cassert>
#include <json/value.h>

export module rhobject:reflection;

import std.compat;
import assertions;
import type_id;

import reflect;
import fixed_string;
import enum_helpers;

import :object;
import dependency_collector;
import name;
import static_name;
import container_traits;
import type_utils;
import string_utils;
#include "common/assertion_macros.h"

#define DEBUG_SERIALIZATION_PATH 1

#if DEBUG_SERIALIZATION_PATH
    #define DEBUG_SERIALIZATION_SCOPE(context, string) \
        SerializationContext::__Scope _(context, string);
#else
    #define DEBUG_SERIALZATION_SCOPE(...)
#endif

export struct SerializationContext
{
    DependencyCollector* dc = nullptr;
    bool is_loading = true;
    bool strict_checking_enabled = false;
    std::optional<std::string> current_file = std::nullopt;
#if DEBUG_SERIALIZATION_PATH
    mutable std::vector<std::string> path;
#endif
    
    std::string get_debug_path() const
    {
#if DEBUG_SERIALIZATION_PATH
        std::string result;
        for (uint32_t index = 0; const auto& e : path)
        {
            result.append(e);
            if (index != path.size() - 1)
            {
                result += ".";
            }
            index++;
        }
        return result;
#else
        return "!NOT SUPPORTED!";
#endif
    }
    
#if DEBUG_SERIALIZATION_PATH
    struct __Scope
    {
        const SerializationContext& ctx;
        __Scope(const SerializationContext& in_ctx, std::string s)
            : ctx(in_ctx)
        {
            ctx.path.emplace_back(s);
        }
        
        ~__Scope()
        {
            ctx.path.pop_back();
        }
    };
#endif
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
    constexpr std::string_view get_object_type_name()
    {
        return type_name_of<T>();
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
        if constexpr (requires { serialize_json_value(target, value, context); })
        {
            serialize_json_value(target, value, context);
        }
        else if constexpr (std::is_enum_v<T>)
        {
            std::string value_str = value.asString();
            checkf(reflect::is_valid_enum_name<T>(value_str), "Wrong member name '%s' for enum '%s'. Path: %s",
                            value_str.c_str(), reflect::get_name<T>().to_string().c_str(),
                            context.get_debug_path().c_str());

            target = reflect::name_to_enum<T>(value_str);
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
            checkf(type_name_ptr != nullptr, "__type__ should be provided for variant fields. Path: %s",
                            context.get_debug_path().c_str());
            checkf(type_name_ptr->isString(), "__type__ must be string. Path: %s",
                            context.get_debug_path().c_str());

            const Name type_name = type_name_ptr->asString();

            visit_variadic_types(target, [&] <typename U> () {
                if (type_name == reflect::get_name<U>())
                {
                    auto& variant_target_value = target.template emplace<U>(U{});
                    DEBUG_SERIALIZATION_SCOPE(context, format("<%s>", type_name.to_string().c_str()));
                    do_serialize_json_value(variant_target_value, value, context);
                }
            });
        } else if constexpr (is_vector_v<std::decay_t<T>>)
        {
            checkf(value.isArray(), "Provided JSON value is not an array (meant as array). Path: %s",
                            context.get_debug_path().c_str());
            target.clear();
            for (uint32_t index = 0; auto& json_item : value)
            {
                typename T::value_type array_item;
                target.push_back(array_item);
                DEBUG_SERIALIZATION_SCOPE(context, format("[%i]", index));
                do_serialize_json_value(target.back(), json_item, context);
                index++;
            }
        } else if constexpr (is_set_v<std::decay_t<T>>)
        {
            checkf(value.isArray(), "Provided JSON value is not an array (meant as set). Path: %s",
                            context.get_debug_path().c_str());
            target.clear();
            for (uint32_t index = 0; auto& json_item : value)
            {
                typename T::value_type set_item;
                DEBUG_SERIALIZATION_SCOPE(context, format("[%i]", index));
                do_serialize_json_value(set_item, json_item, context);
                target.emplace(set_item);
                index++;
            }
        } else if constexpr (is_mask_v<std::decay_t<T>>)
        {
            checkf(value.isArray(), "Provided JSON value is not an array (meant as mask). Path: %s",
                            context.get_debug_path().c_str());
            target = 0;
            for (auto& json_item : value)
            {
                using value_type = typename T::enum_type;
                static_assert(std::is_enum_v<value_type>);
                value_type enum_value;
                do_serialize_json_value(enum_value, json_item, context);
                target |= enum_value;
            }
        } else if constexpr (is_optional_v<std::decay_t<T>>)
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
            checkf(value.isObject(), "Provided JSON value is not an object (meant as map). Path: %s",
                            context.get_debug_path().c_str());
            auto member_names = value.getMemberNames();

            using KEY = T::key_type;

            static_assert(
                std::is_same_v<KEY, std::string> ||
                std::is_same_v<KEY, Name> ||
                std::is_enum_v<KEY>);

            for (auto& member_name : member_names)
            {
                typename T::mapped_type map_value;

                auto from_string = [&context] (const std::string& name) -> KEY
                {
                    if constexpr (std::is_enum_v<KEY>)
                    {
                        checkf(reflect::is_valid_enum_name<KEY>(name), "Wrong member name '%s' for enum '%s'. Path: %s",
                            name.c_str(), reflect::get_name<KEY>().to_string().c_str(),
                            context.get_debug_path().c_str());
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

                DEBUG_SERIALIZATION_SCOPE(context, format("[%s]", member_name.c_str()));
                do_serialize_json_value(it->second, json_value, context);
            }
        } else if constexpr (std::is_class_v<T>)
        {
            checkf(value.isObject(), "Provided JSON value is not an object (meant as '%s'). Path: %s",
                            reflect::get_name<T>().to_string().c_str(), context.get_debug_path().c_str());
            visit_serialize(value, target, context);
        } else
        {
            static_assert(false, "No JSON serialization for this type: add serialize_json_value() overload");
        }
    }

    // Serializes reflected fields of T (see reflect::fields_of): for RhObject descendants these are
    // [[=rh::serialize]] fields, for other classes all public fields except [[=rh::transient]] ones.
    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, const SerializationContext& context)
    {
        reflect::for_each_field<T>([&] <typename Field> () {
            constexpr std::string_view name = Field::name;
            Json::Value const* json_value = json_object.find(name.data(), name.data() + name.size());

            if (!json_value && context.strict_checking_enabled && !is_optional_v<std::remove_cv_t<typename Field::type>>)
            {
                checkf(context.current_file.has_value(), "Strict checking mode not supported for non-file serialization");
                checkf(false, "During parsing '%s', required field '%s' is missing. Path: %s",
                    context.current_file->c_str(),
                    std::string(name).c_str(),
                    context.get_debug_path().c_str());
            }
            if (json_value)
            {
                DEBUG_SERIALIZATION_SCOPE(context, std::string(name));
                do_serialize_json_value(struct_ref.[:Field::info:], *json_value, context);
            }
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
        
    namespace detail
    {
        // T and all its RhObject ancestors
        consteval void collect_object_classes(std::meta::info type, std::vector<std::meta::info>& classes)
        {
            classes.push_back(type);
            for (std::meta::info base : std::meta::bases_of(type, std::meta::access_context::unchecked()))
            {
                std::meta::info base_type = std::meta::type_of(base);
                if (base_type == ^^RhObject || std::meta::is_base_of_type(^^RhObject, base_type))
                    collect_object_classes(base_type, classes);
            }
        }

        template<typename T>
        std::set<std::string_view> get_object_classes()
        {
            constexpr auto class_names = [] consteval {
                std::vector<std::meta::info> classes;
                collect_object_classes(^^T, classes);
                std::vector<const char*> names;
                for (std::meta::info cls : classes)
                    names.push_back(std::define_static_string(std::meta::identifier_of(cls)));
                return std::define_static_array(names);
            }();
            return std::set<std::string_view>(class_names.begin(), class_names.end());
        }
    }

    // Use RH_OBJECT(T) (object/object_reflection_macro.h) instead of calling it directly
    template <typename T>
    bool register_object_class()
    {
        static_assert(std::is_base_of_v<RhObject, T>, "Can't register non-RhObject types");

        constexpr std::string_view name = get_object_type_name<T>();

        JsonSerializer serializer = [] (const Json::Value& json_object, RhObject* object_ptr, const SerializationContext& context) -> bool
        {
            T* object = static_cast<T*>(object_ptr);
            json::visit_serialize(json_object, *object, context);
            object->on_serialize(context);
            return true;
        };

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
        register_object_class_impl(
            name,
            std::move(factory),
            std::move(unique_factory),
            detail::get_object_classes<T>(),
            std::move(serializer),
            std::is_abstract_v<T>);
        return true;
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
