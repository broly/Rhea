#pragma once
#include "common/foreach_macro.h"

import fixed_string;
import reflect;

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

#define __PRIVATE_NAMED_ENUM_MEMBER_VALUE_TO_NAME(x) {underlying(type::x), #x}
#define __PRIVATE_NAMED_ENUM_MEMBER_NAME_TO_VALUE(x) {#x, underlying(type::x)}

#define REFLECT_ENUM(T, ...) \
    export template<> \
    struct reflect::ReflectionInfo<T> \
    { \
        using type = T; \
        using underlying = std::underlying_type_t<T>; \
        inline static std::map<underlying, std::string> names = {\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_ENUM_MEMBER_VALUE_TO_NAME,__VA_ARGS__)\
        }; \
        inline static std::map<std::string, underlying> values = {\
            RHEA_FOR_EACH_COLON(__PRIVATE_NAMED_ENUM_MEMBER_NAME_TO_VALUE,__VA_ARGS__)\
        }; \
        static T enum_name_to_value(const std::string& s)\
        {\
            if (values.contains(s))\
                return (T)values[s];\
            return T{};\
        }\
        static constexpr bool reflected = true; \
    };
