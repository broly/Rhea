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

import :object;

#include "common/reflect_macros.h"

export namespace reflect_inner
{
    template<typename T>
    struct RhObjectTraits;
    
    
    template<typename Class, typename Base>
    std::set<std::string_view> get_bases_impl(std::string_view ClassName)
    {
        std::set<std::string_view> current_bases;
        if constexpr (!std::is_same_v<Class, RhObject> && !std::is_same_v<Base, void>) { 
            current_bases.merge(RhObjectTraits<Base>::get_bases()); 
        }
        current_bases.emplace(ClassName); 
        return current_bases; 
    }
}

    
export inline void serialize_json_value(Json::Int& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt();
}
export inline void serialize_json_value(Json::UInt& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt();
}
export inline void serialize_json_value(Json::Int64& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt64();
}
export inline void serialize_json_value(Json::UInt64& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt64();
}
export inline void serialize_json_value(std::string& target, const Json::Value& value)
{
    assert(value.isString());
    target = value.asString();
}




// export template<typename T>
// requires requires(T& t, const Json::Value& v) 
// {
//     serialize_json_value(t, v);
// }
// inline void serialize_json_value(std::vector<T>& target, const Json::Value& value)
// {
//     assert(value.isArray());
//     target.clear();
//     for (auto& json_item : value)
//     {
//         T array_item;
//         serialize_json_value<T>(array_item, json_item);
//         target.push_back(array_item);
//     }
// }


export inline void serialize_json_value(float& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asFloat();
}
export inline void serialize_json_value(double& target, const Json::Value& value)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asDouble();
}
export inline void serialize_json_value(bool& target, const Json::Value& value)
{
    assert(value.isBool());
    target = value.asBool();
}

template<typename T>
struct is_vector
{
    static bool constexpr value = false;
};

template<typename T>
struct is_vector<std::vector<T> > 
{
    static bool constexpr value = true;
};

template<typename T>
constexpr bool is_vector_v = is_vector<T>::value;


template<typename>
struct is_map 
{
    static bool constexpr value = false;
};

template<typename K, typename V>
struct is_map<std::map<K, V> > 
{
    static bool constexpr value = true;
};

template<typename T>
constexpr bool is_map_v = is_map<T>::value;
    
export namespace reflect::json
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
        } else if constexpr (is_vector_v<std::decay_t<T>>)
        {
            assert(value.isArray());
            target.clear();
            for (auto& json_item : value)
            {
                typename T::value_type array_item;
                // serialize_json_value(array_item, json_item);
                visit_serialize(json_item, array_item, is_loading);
                target.push_back(array_item);
            }
        } else if constexpr (is_map_v<std::decay_t<T>>)
        {
            assert(value.isObject());
            auto member_names = value.getMemberNames();
            
            static_assert(std::is_same_v<typename T::key_type, std::string>);
            
            for (auto& member_name : member_names)
            {
                typename T::mapped_type map_value;
                
                auto& json_value = value[member_name];
                
                visit_serialize(json_value, map_value, is_loading);
                target.try_emplace(member_name, map_value);
            }
        } else if constexpr (requires { serialize_json_value(target, value); })
        {
            // visit_serialize(value, target, is_loading);
            // if constexpr (!requires { serialize_json_value(target, value); })
            // {
            //     ReflectionInfo<T>::reflected;
            // }
            // static_assert(requires { serialize_json_value(target, value); });
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

export struct ObjectInitData
{
    std::string name;
};

export namespace reflect
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


