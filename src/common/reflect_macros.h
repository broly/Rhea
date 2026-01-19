#pragma once
#include "common/foreach_macro.h"
import <numeric>;

import fixed_string;
import reflect;
import <string>;
import type_id;

#define __PRIVATE_NAMED_FIELD(x) detail::NamedField<&Type::x, #x>

#define REFLECT_STRUCT(T, ...) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using Type = T; \
        using Fields = detail::NamedFieldList<\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_FIELD,__VA_ARGS__) \
        >;\
        static constexpr bool reflected = true; \
        static constexpr void iter(auto Func) \
        {\
            Fields::iter(Func); \
        }\
    }


#define __PRIVATE_RUNTIME_FIELD(x) \
    FieldRuntimeReflectionInfo( \
        #x, \
        get_type_id<decltype(Type{}.x)>(), \
        offsetof(Type, x)\
    )


#define REFLECT_STRUCT_RUNTIME(T, ...) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using Type = T; \
        using Fields = detail::NamedFieldList<\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_FIELD,__VA_ARGS__) \
        >;\
        static constexpr bool reflected = true; \
        static constexpr void iter(auto Func) \
        {\
            Fields::iter(Func); \
        }\
        static inline auto __dummy = reflect::register_type(\
            get_type_id<T>(), \
            sizeof(T), \
            [] (void* ptr) { new (ptr) T(); },\
            {\
                RHEA_FOR_EACH_COLON(__PRIVATE_RUNTIME_FIELD,__VA_ARGS__) \
            });\
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
        static T enum_name_to_value(Name s)\
        {\
            if (values.contains(s))\
                return (T)values[s];\
            return T{};\
        }\
        static constexpr bool reflected = true; \
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