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


