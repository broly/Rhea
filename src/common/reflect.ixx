export module reflect;

import std.compat;
import assertions;
import name;
import type_id;
import container_traits;
#include "assertion_macros.h"


// Annotations (C++26, P3394) that control what reflection sees:
//
//     struct CameraUBO
//     {
//         glm::vec3 camera_pos;
//         [[=rh::padding]] float pad0;
//     };
//
//     class RhComp_Light : public RhComp_Renderable
//     {
//         [[=rh::serialize]] float intensity;
//         SceneViewProxy_Light scene_proxy;   // not reflected: RhObject fields are opt-in
//     };
export namespace rh
{
    // Field of a class with explicit fields (every RhObject) that is reflected / serialized.
    struct Serialize {};
    inline constexpr Serialize serialize;

    // Field or enumerator that reflection ignores: runtime-only state, enum helper values (COUNT, masks).
    struct Transient {};
    inline constexpr Transient transient;

    // Explicit padding of a GPU struct: not a field of the shader-side struct.
    struct Padding {};
    inline constexpr Padding padding;

    // On a class: only [[=rh::serialize]] fields of the class and its descendants are reflected.
    // Without it every public non-static data member (including ones of public bases) is reflected.
    struct ExplicitFields {};
    inline constexpr ExplicitFields explicit_fields;
}


export namespace reflect
{
    enum class TypeKind
    {
        value,
        array,
        map,
        set,
        shared_ptr,
        unknown,
    };

    template<typename Annotation>
    consteval bool has_annotation(std::meta::info entity)
    {
        return !std::meta::annotations_of(entity, ^^Annotation).empty();
    }

    namespace detail
    {
        consteval bool has_explicit_fields(std::meta::info type)
        {
            if (has_annotation<rh::ExplicitFields>(type))
                return true;
            for (std::meta::info base : std::meta::bases_of(type, std::meta::access_context::unchecked()))
                if (has_explicit_fields(std::meta::type_of(base)))
                    return true;
            return false;
        }

        consteval bool is_reflected_field(std::meta::info member, bool explicit_fields)
        {
            if (!std::meta::has_identifier(member))
                return false;
            if (has_annotation<rh::Transient>(member) || has_annotation<rh::Padding>(member))
                return false;
            return !explicit_fields || has_annotation<rh::Serialize>(member);
        }

        consteval void collect_fields(std::meta::info type, bool explicit_fields, std::vector<std::meta::info>& fields)
        {
            const auto ctx = std::meta::access_context::unprivileged();
            for (std::meta::info base : std::meta::bases_of(type, ctx))
                collect_fields(std::meta::type_of(base), explicit_fields, fields);
            for (std::meta::info member : std::meta::nonstatic_data_members_of(type, ctx))
                if (is_reflected_field(member, explicit_fields))
                    fields.push_back(member);
        }

        consteval size_t field_offset(std::meta::info type, std::meta::info field)
        {
            if (std::meta::parent_of(field) == type)
                return std::meta::offset_of(field).bytes;
            for (std::meta::info base : std::meta::bases_of(type, std::meta::access_context::unchecked()))
            {
                std::meta::info base_type = std::meta::type_of(base);
                if (base_type == std::meta::parent_of(field) ||
                    std::meta::is_base_of_type(std::meta::parent_of(field), base_type))
                    return std::meta::offset_of(base).bytes + field_offset(base_type, field);
            }
            (std::unreachable)();
        }
    }

    // Reflected fields of T in declaration order, fields of bases first
    template<typename T>
    consteval std::span<const std::meta::info> fields_of()
    {
        constexpr std::meta::info type = std::meta::dealias(^^T);
        std::vector<std::meta::info> fields;
        detail::collect_fields(type, detail::has_explicit_fields(type), fields);
        return std::define_static_array(fields);
    }

    // Offset of a field (possibly declared in a base) inside T.
    template<typename T>
    consteval size_t field_offset(std::meta::info field)
    {
        return detail::field_offset(std::meta::dealias(^^T), field);
    }

    // I-th reflected field of T, passed to for_each_field callbacks
    template<typename T, size_t I>
    struct Field
    {
        static constexpr std::meta::info info = fields_of<T>()[I];
        static constexpr std::string_view name = std::meta::identifier_of(info);
        static constexpr size_t offset = field_offset<T>(info);
        using type = typename [:std::meta::type_of(info):];
    };

    // Calls func.template operator()<Field<T, I>>() for every reflected field of T:
    //     reflect::for_each_field<T>([&] <typename F> () { use(F::name, obj.[:F::info:]); });
    //
    // Why not `template for` / a std::meta::info template parameter (both are simpler):
    //  - clang-p2996 serializes expansion statements of module interface templates into gigabyte-sized BMIs;
    //  - clang's MSVC-ABI mangling gives the same name to specializations differing only in an info
    //    template argument, so the linker merges them. Field<T, I> is mangled by T and I.
    template<typename T, typename Func>
    void for_each_field(Func&& func)
    {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (func.template operator()<Field<T, I>>(), ...);
        }(std::make_index_sequence<fields_of<T>().size()>());
    }

    template<typename T>
    consteval std::string_view type_name_of()
    {
        constexpr std::meta::info type = std::meta::dealias(^^T);
        if constexpr (std::meta::has_identifier(type))
            return std::meta::identifier_of(type);
        else
            return std::meta::display_string_of(type);
    }

    template<typename T>
    Name get_name()
    {
        static const Name name = type_name_of<T>();
        return name;
    }


    /************************************************************************
     * ENUMS
     ***********************************************************************/

    template<typename E>
    consteval std::span<const std::meta::info> enumerators_of()
    {
        static_assert(std::is_enum_v<E>);
        std::vector<std::meta::info> result;
        for (std::meta::info enumerator : std::meta::enumerators_of(^^E))
            if (!has_annotation<rh::Transient>(enumerator))
                result.push_back(enumerator);
        return std::define_static_array(result);
    }

    // I-th reflected enumerator of E, passed to for_each_enumerator callbacks
    template<typename E, size_t I>
    struct Enumerator
    {
        static constexpr std::meta::info info = enumerators_of<E>()[I];
        static constexpr std::string_view name = std::meta::identifier_of(info);
        static constexpr E value = [:info:];
    };

    // reflect::for_each_enumerator<E>([&] <typename En> () { use(En::name, En::value); });
    // (see for_each_field about the wrapper type)
    template<typename E, typename Func>
    void for_each_enumerator(Func&& func)
    {
        [&]<size_t... I>(std::index_sequence<I...>) {
            (func.template operator()<Enumerator<E, I>>(), ...);
        }(std::make_index_sequence<enumerators_of<E>().size()>());
    }

    namespace detail
    {
        template<typename E>
        struct EnumTable
        {
            using underlying = std::underlying_type_t<E>;

            // Aliases (several enumerators with one value) map to the first declared name
            std::map<underlying, Name> names;
            std::map<Name, underlying> values;
        };

        template<typename E>
        const EnumTable<E>& enum_table()
        {
            static const EnumTable<E> table = [] {
                EnumTable<E> result;
                for_each_enumerator<E>([&] <typename En> () {
                    const auto value = static_cast<std::underlying_type_t<E>>(En::value);
                    const Name name = En::name;
                    result.names.try_emplace(value, name);
                    result.values.try_emplace(name, value);
                });
                return result;
            }();
            return table;
        }
    }

    template<typename E>
    Name enum_name(E value)
    {
        const auto& names = detail::enum_table<E>().names;
        auto it = names.find(static_cast<std::underlying_type_t<E>>(value));
        
        checkf(it != names.end(), "Value %lld is not an enumerator of '%s'",
            (long long)value, get_name<E>().to_string().c_str());
        return it->second;
    }

    // Names of all enumerators, ordered by value
    template<typename E>
    std::vector<Name> enum_names()
    {
        std::vector<Name> result;
        for (const auto& [value, name] : detail::enum_table<E>().names)
            result.push_back(name);
        return result;
    }

    template<typename E>
    bool is_valid_enum_name(Name name)
    {
        return detail::enum_table<E>().values.contains(name);
    }

    template<typename E>
    E name_to_enum(Name name)
    {
        const auto& values = detail::enum_table<E>().values;
        auto it = values.find(name);
        return it != values.end() ? static_cast<E>(it->second) : E{};
    }


    /************************************************************************
     * RUNTIME TYPE INFO (lookup by type name: UBOs, push constants, vertices)
     ***********************************************************************/

    using type_initializer = std::function<void(void* value_ptr)>;

    struct FieldRuntimeReflectionInfo
    {
        Name name;
        TypeId id;
        ptrdiff_t offset;
        TypeKind kind = TypeKind::unknown;

        void* get_value_ptr(void* struct_ptr) const
        {
            return (uint8_t*)struct_ptr + offset;
        }
    };

    struct RuntimeReflectionInfo
    {
        TypeId id;
        size_t size;
        type_initializer initializer;
        bool is_basic;
        std::vector<FieldRuntimeReflectionInfo> fields;

        const FieldRuntimeReflectionInfo* find_field(Name field_name) const
        {
            for (const auto& field : fields)
                if (field.name == field_name)
                    return &field;
            return nullptr;
        }

        const FieldRuntimeReflectionInfo& find_field_checked(Name field_name) const
        {
            auto result = find_field(field_name);
            checkf(result != nullptr, "find_field_checked failed");
            return *result;
        }
    };

    bool register_type(TypeId type_id, size_t size, type_initializer initializer,
        const std::vector<FieldRuntimeReflectionInfo>& fields);
    bool register_basic_type(TypeId type_id, size_t size, type_initializer initializer);


    const RuntimeReflectionInfo* find_runtime_info(TypeId type_id);


    const RuntimeReflectionInfo* find_runtime_info(Name type_name)
    {
        return find_runtime_info(TypeId(type_name));
    }
    const RuntimeReflectionInfo& find_runtime_info_checked(Name type_name)
    {
        auto result = find_runtime_info(TypeId(type_name));
        checkf(result != nullptr, "find_runtime_info failed");
        return *result;
    }

    template<typename T>
    consteval TypeKind type_kind_of()
    {
        using Type = std::decay_t<T>;
        if (std::is_array_v<Type>)
            return TypeKind::array;
        if (is_map_v<Type>)
            return TypeKind::map;
        if (is_set_v<Type>)
            return TypeKind::set;
        if (std::is_arithmetic_v<Type> || std::is_same_v<Type, std::string> || std::is_same_v<Type, Name>)
            return TypeKind::value;
        return TypeKind::unknown;
    }

    template<typename T>
    constexpr TypeKind type_kind_v = type_kind_of<T>();

    // Use RH_REGISTER_TYPE(T) (common/reflect_macros.h) instead of calling it directly
    template<typename T>
    bool register_type_runtime_info()
    {
        std::vector<FieldRuntimeReflectionInfo> fields;

        for_each_field<T>([&] <typename F> () {
            fields.push_back(FieldRuntimeReflectionInfo{
                .name = F::name,
                .id = get_type_id<typename F::type>(),
                .offset = static_cast<ptrdiff_t>(F::offset),
                .kind = type_kind_v<typename F::type>
            });
        });

        return reflect::register_type(
            get_type_id<T>(),
            sizeof(T),
            [] (void* ptr) { new (ptr) T(); },
            fields
            );
    }
}
