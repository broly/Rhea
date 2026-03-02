#pragma once
#include "common/foreach_macro.h"
import <numeric>;

import fixed_string;
import reflect;
import <string>;
import type_id;

#define __PRIVATE_NAMED_FIELD(x) detail::NamedField<&Type::x, #x>

#define __PRIVATE_IS_REFLECTED(x) static_assert(reflect::is_reflected_v<decltype(Type{}.x)>, #x" is not reflected")

#define REFLECT_STRUCT(T, ...) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using Type = T; \
        using Fields = detail::NamedFieldList<\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_FIELD,__VA_ARGS__) \
        >;\
        static constexpr std::string_view name = #T; \
        static constexpr detail::reflection_tag reflected {}; \
        static constexpr void iter(auto Func) \
        {\
            Fields::iter(Func); \
        }\
    }

#define REFLECT_STRUCT_RUNTIME(T, ...) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using Type = T; \
        using Fields = detail::NamedFieldList<\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_FIELD,__VA_ARGS__) \
        >;\
        static constexpr detail::reflection_tag reflected {}; \
        static constexpr std::string_view name = #T; \
        static constexpr void iter(auto Func) \
        {\
            Fields::iter(Func); \
        }\
        static inline auto __dummy = reflect::register_type_runtime_info<T>(false); \
    }

#define REFLECT_STRUCT_RUNTIME_OPAQUE(T) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using Type = T; \
        using Fields = detail::NamedFieldList<>;\
        static constexpr detail::reflection_tag reflected {}; \
        static constexpr std::string_view name = #T; \
        static constexpr void iter(auto Func) \
        {\
        }\
        static inline auto __dummy = reflect::register_type_runtime_info<T>(true); \
    }

#define __PRIVATE_NAMED_ENUM_MEMBER_VALUE_TO_NAME(x) {underlying(type::x), #x}
#define __PRIVATE_NAMED_ENUM_MEMBER_NAME_TO_VALUE(x) {#x, underlying(type::x)}

#define REFLECT_ENUM(T, ...) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using type = T; \
        using underlying = std::underlying_type_t<T>; \
        inline static std::map<underlying, Name> names = {\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_ENUM_MEMBER_VALUE_TO_NAME,__VA_ARGS__)\
        }; \
        inline static std::map<Name, underlying> values = {\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_ENUM_MEMBER_NAME_TO_VALUE,__VA_ARGS__)\
        }; \
        static constexpr std::string_view name = #T; \
        static T enum_name_to_value(Name s)\
        {\
            if (values.contains(s))\
                return (T)values[s];\
            return T{};\
        }\
        static bool is_valid_enum_name(Name s)\
        {\
            return values.contains(s);\
        }\
        static Name enum_value_to_name(T value) \
        {\
            return names.at((int)value); \
        }\
        static constexpr detail::reflection_tag reflected {}; \
    };

#define __REGISTER_TYPE_COMBINE_INNER(A,B) A##B
#define __REGISTER_TYPE_COMBINE(A,B) __REGISTER_TYPE_COMBINE_INNER(A,B)



#define REGISTER_BASIC_TYPE(T) \
    namespace { \
        auto __REGISTER_TYPE_COMBINE(_dummy_type_, __LINE__) = ::reflect::register_basic_type(get_type_id<T>(), sizeof(T), [] (void* ptr) { new (ptr) T(); } ); \
    }

REGISTER_BASIC_TYPE(uint32_t);
REGISTER_BASIC_TYPE(uint64_t);
REGISTER_BASIC_TYPE(std::string);
REGISTER_BASIC_TYPE(float);
REGISTER_BASIC_TYPE(double);