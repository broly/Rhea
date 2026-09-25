export module properties;

import std.compat;
import reflect;
import type_id;
import name;


// Type-erased description of [[=rh::edit]] fields (see reflect.ixx), built from C++26 reflection.
// Lets UI code (debug inspector, cvar catalog) edit fields of any type without knowing the type:
//
//     reflect::PropertyObject props = reflect::make_property_object(my_struct);
//     for (const reflect::PropertyDesc& p : *props.properties)
//         edit(p.type, p.value_ptr(props.object));
//
// Leaf values are identified by TypeId (editors are registered per type by the UI), enums carry their
// enumerators, other structs carry their own property list (their [[=rh::edit]] fields when they have any,
// otherwise all public fields).
export namespace reflect
{
    struct EnumInfo
    {
        std::vector<std::string_view> names;
        std::vector<int64_t> values;
        int64_t (*get)(const void* value) = nullptr;
        void (*set)(void* value, int64_t new_value) = nullptr;

        // index in names / values, -1 when the value is not an enumerator
        int32_t find_index(int64_t value) const
        {
            for (size_t index = 0; index < values.size(); index++)
                if (values[index] == value)
                    return (int32_t)index;
            return -1;
        }
    };

    struct PropertyDesc;
    using PropertyList = std::vector<PropertyDesc>;

    struct PropertyDesc
    {
        std::string_view name;
        TypeId type = nullptr;
        size_t offset = 0;
        PropertyMeta meta;
        const EnumInfo* enum_info = nullptr;
        const PropertyList* fields = nullptr;     // members of a struct type

        void* value_ptr(void* object) const
        {
            return static_cast<std::byte*>(object) + offset;
        }
    };

    // An object together with its editable properties
    struct PropertyObject
    {
        void* object = nullptr;
        const PropertyList* properties = nullptr;
        std::string_view type_name;

        bool empty() const
        {
            return !object || !properties || properties->empty();
        }
    };

    namespace detail
    {
        consteval bool is_std_type(std::meta::info type)
        {
            for (std::meta::info scope = std::meta::parent_of(type); scope != ^^::; scope = std::meta::parent_of(scope))
                if (scope == ^^std)
                    return true;
            return false;
        }

        // structs whose fields are listed (std types and Name are leaves: edited as a whole or not at all)
        template<typename T>
        constexpr bool has_nested_properties =
            std::is_class_v<T> && !std::is_same_v<T, Name> && !is_std_type(std::meta::dealias(^^T));
    }

    template<typename E>
    const EnumInfo& enum_info_of()
    {
        static const EnumInfo info = [] {
            using U = std::underlying_type_t<E>;
            EnumInfo result;
            for_each_enumerator<E>([&] <typename En> () {
                result.names.push_back(En::name);
                result.values.push_back((int64_t)(U)En::value);
            });
            result.get = [] (const void* value) -> int64_t { return (int64_t)(U)*static_cast<const E*>(value); };
            result.set = [] (void* value, int64_t new_value) { *static_cast<E*>(value) = (E)(U)new_value; };
            return result;
        }();
        return info;
    }

    template<typename T>
    const PropertyList& nested_properties_of();

    template<typename V>
    PropertyDesc make_property(std::string_view name, size_t offset, const PropertyMeta& meta)
    {
        PropertyDesc desc{
            .name = name,
            .type = get_type_id<V>(),
            .offset = offset,
            .meta = meta,
        };
        if constexpr (std::is_enum_v<V>)
            desc.enum_info = &enum_info_of<V>();
        else if constexpr (detail::has_nested_properties<V>)
            desc.fields = &nested_properties_of<V>();
        return desc;
    }

    // [[=rh::edit]] fields of T (and of its bases)
    template<typename T>
    const PropertyList& editable_properties_of()
    {
        static const PropertyList properties = [] {
            PropertyList result;
            for_each_editable_field<T>([&] <typename F> () {
                constexpr PropertyMeta meta = F::meta;
                result.push_back(make_property<std::remove_cv_t<typename F::type>>(F::name, F::offset, meta));
            });
            return result;
        }();
        return properties;
    }

    // Fields of a struct used as a property: its [[=rh::edit]] fields, or all its public fields
    template<typename T>
    const PropertyList& nested_properties_of()
    {
        if constexpr (!editable_fields_of<T>().empty())
        {
            return editable_properties_of<T>();
        }
        else
        {
            static const PropertyList properties = [] {
                PropertyList result;
                for_each_field<T>([&] <typename F> () {
                    using V = std::remove_cv_t<typename F::type>;
                    if constexpr (!std::meta::is_bit_field(F::info) && !std::is_reference_v<typename F::type>)
                    {
                        constexpr PropertyMeta meta = F::meta;
                        result.push_back(make_property<V>(F::name, F::offset, meta));
                    }
                });
                return result;
            }();
            return properties;
        }
    }

    template<typename T>
    PropertyObject make_property_object(T& object)
    {
        return PropertyObject{
            .object = &object,
            .properties = &editable_properties_of<T>(),
            .type_name = type_name_of<T>(),
        };
    }
}
