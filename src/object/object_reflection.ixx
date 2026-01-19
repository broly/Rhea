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
import dependency_collector;
import name;
import static_name;

#include "common/reflect_macros.h"


    
export inline void serialize_json_value(Json::Int& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt();
}
export inline void serialize_json_value(Name& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isString());
    target = value.asString();
}
export inline void serialize_json_value(Json::UInt& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt();
}
export inline void serialize_json_value(Json::Int64& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asInt64();
}
export inline void serialize_json_value(Json::UInt64& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isNumeric() || value.isIntegral());
    target = value.asUInt64();
}
export inline void serialize_json_value(std::string& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isString());
    target = value.asString();
}


export inline void serialize_json_value(float& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asFloat();
}
export inline void serialize_json_value(double& target, const Json::Value& value, DependencyCollector* dc)
{
    assert(value.isNumeric() || value.isIntegral() || value.isDouble());
    target = value.asDouble();
}
export inline void serialize_json_value(bool& target, const Json::Value& value, DependencyCollector* dc)
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
    using JsonSerializer = std::function<bool(const Json::Value&, RhObject* Ptr, bool is_loading, DependencyCollector* collector)>;
    
    struct ObjectReflectionInfo
    {
        std::string_view name;
        std::set<std::string_view> bases;
    
        ObjectFactoryType factory;
        UniqueObjectFactoryType unique_factory;
        std::optional<JsonSerializer> serializer;
        
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




template<typename>
struct is_shared_ptr 
{
    static bool constexpr value = false;
};

template<typename T>
struct is_shared_ptr<std::shared_ptr<T> > 
{
    static bool constexpr value = true;
};

template<typename T>
constexpr bool is_shared_ptr_v = is_shared_ptr<T>::value;

    
export namespace reflect::json
{
    
    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, bool is_loading, DependencyCollector* dc);

    template<typename T>
    void do_serialize_json_value(T& target, const Json::Value& value, bool is_loading, DependencyCollector* dc)
    {
        if constexpr (std::is_enum_v<T> && reflect::is_reflected_v<T>)
        {
            target = reflect::ReflectionInfo<T>::template enum_name_to_value(value.asString());
        } else if constexpr (is_shared_ptr_v<T>)
        {
            auto type_id = reflect::get_object_type_name<typename T::element_type>();
            auto info = reflect::find_object_reflection_info(type_id);
            target = info->template instantiate<typename T::element_type>();
            do_serialize_json_value(*target, value, is_loading, dc);
        }
        else if constexpr (reflect::is_reflected_v<T>)
        {
            assert(value.isObject());
            visit_serialize(value, target, is_loading, dc);
        } else if constexpr (is_vector_v<std::decay_t<T>>)
        {
            assert(value.isArray());
            target.clear();
            for (auto& json_item : value)
            {
                typename T::value_type array_item;
                // serialize_json_value(array_item, json_item);
                target.push_back(array_item);
                do_serialize_json_value(target.back(), json_item, is_loading, dc);
            }
        } else if constexpr (is_map_v<std::decay_t<T>>)
        {
            assert(value.isObject());
            auto member_names = value.getMemberNames();
            
            using KEY = typename T::key_type;
            
            static_assert(std::is_same_v<typename T::key_type, std::string> || std::is_same_v<typename T::key_type, Name>);
            
            for (auto& member_name : member_names)
            {
                typename T::mapped_type map_value;
                
                KEY key = std::string(member_name);
                
                auto [it, inserted] = target.try_emplace(key, map_value);
                
                auto& json_value = value[member_name];
                
                do_serialize_json_value(it->second, json_value, is_loading, dc);
            }
        } else if constexpr (requires { serialize_json_value(target, value, dc); })
        {
            // visit_serialize(value, target, is_loading);
            // if constexpr (!requires { serialize_json_value(target, value); })
            // {
            //     ReflectionInfo<T>::reflected;
            // }
            // static_assert(requires { serialize_json_value(target, value); });
            serialize_json_value(target, value, dc);
        }
    }

    template<typename T>
    void visit_serialize(const Json::Value& json_object, T& struct_ref, bool is_loading, DependencyCollector* dc)
    {
        reflect::visit<T>([&] <auto PtrToField, FixedString Name> ()
        {
            const std::string name = std::string(Name); // todo: inefficient
            if (Json::Value const* json_value = json_object.find(std::string(name)))
            {
                auto& field_ref = struct_ref.*PtrToField;
                do_serialize_json_value(field_ref, *json_value, is_loading, dc);
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
        std::optional<JsonSerializer> serializer);
        
    template <typename T>
    inline bool register_object_class(std::string_view name, std::optional<JsonSerializer>&& Serializer)
    {
        ObjectFactoryType factory = [name](const ObjectInitData& init_data)
        {
            auto object = std::make_shared<T>();
            object->init(name, init_data);
            return object;
        };
        UniqueObjectFactoryType unique_factory = [name](const ObjectInitData& init_data)
        {
            auto object = std::make_unique<T>();
            object->init(name, init_data);
            return object;
        };
        std::set<std::string_view> class_bases = reflect_inner::RhObjectTraits<T>::get_bases();
        register_object_class_impl(name, std::move(factory), std::move(unique_factory), std::move(class_bases), std::move(Serializer));
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
    
    
    std::vector<const ObjectReflectionInfo*> get_subtypes(Name type_name, bool include_parent = false);
    
    template<typename T>
    std::vector<const ObjectReflectionInfo*> get_subtypes(bool include_parent = false)
    {
        Name type_name = get_object_type_name<T>();
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


