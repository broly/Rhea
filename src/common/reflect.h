#pragma once
#include "common/fixed_string.h"
#include "common/foreach_macro.h"

namespace reflect
{
    namespace detail
    {
        template<auto PtrToMember, FixedString Name>
        struct NamedField
        {            
            static constexpr void iter(auto func)
            {
                func.template operator()<PtrToMember, Name>();
            }
        };


        template<typename... Types>
        struct NamedFieldList
        {
            static constexpr void iter(auto&& func)
            {
                (Types::iter(func), ...);
            }
        };
    }
    
    template<typename>
    struct ReflectionInfo;
    
    template<typename T>
    constexpr void visit(auto func)
    {
        static_assert(requires { ReflectionInfo<T>::reflected; }, "specified type not reflected");
        ReflectionInfo<T>::iter(func);
    }
    
    template<typename T>
    constexpr bool is_reflected_v = requires { ReflectionInfo<T>::reflected; };
}

#define __PRIVATE_NAMED_FIELD(x) detail::NamedField<&Type::x, #x>

#define REFLECT_STRUCT(T, ...) \
    template<> \
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


