export module reflect;
#include "assertion_macros.h"
#include "common/foreach_macro.h"

import fixed_string;
import <string_view>;
import <functional>;
import name;
import type_id;

export namespace reflect
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
        
        template<typename A, typename B>
        struct ConcatNamedFieldList;
        
        template<typename... TypesA, typename... TypesB>
        struct ConcatNamedFieldList<NamedFieldList<TypesA...>, NamedFieldList<TypesB...>>
        {
            using type = NamedFieldList<TypesA..., TypesB...>;
        };
        
        enum reflection_tag {};
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
    
    
    using type_initializer = std::function<void(void* value_ptr)>;
    
    struct FieldRuntimeReflectionInfo
    {
        Name name;
        TypeId id;
        ptrdiff_t offset;
        
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
    
    template<typename E>
    Name enum_name(E value)
    {
        static_assert(is_reflected_v<E>, "enum not reflected");
        return reflect::ReflectionInfo<E>::enum_value_to_name(value);
    }
    
    template<typename E>
    std::vector<Name> enum_names()
    {
        static_assert(is_reflected_v<E>, "enum not reflected");
        return reflect::ReflectionInfo<E>::get_names();
    }
    
    template<typename E>
    E name_to_enum(Name name)
    {
        static_assert(is_reflected_v<E>, "enum not reflected");
        return reflect::ReflectionInfo<E>::enum_name_to_value(name);
    }
    
    template<typename E>
    bool is_valid_enum_name(Name name)
    {
        static_assert(is_reflected_v<E>, "enum not reflected");
        return reflect::ReflectionInfo<E>::is_valid_enum_name(name);
    }
    
    template<typename T>
    Name get_name()
    {
        static_assert(is_reflected_v<T>, "enum not reflected");
        return reflect::ReflectionInfo<T>::name;
    }
    
    template<typename T, typename U>
    constexpr size_t get_offset(U T::* member_ptr) 
    {
        return (size_t)&(((T*)nullptr)->*member_ptr);
    }
    
    template<typename T, auto FieldPtr>
    using field_type_t = decltype(T{}.*FieldPtr);
    
    template<typename T>
    bool register_type_runtime_info(bool opaque)
    {
        std::vector<FieldRuntimeReflectionInfo> fields;
        
        if (!opaque)
        {
            visit<T>([&fields] <auto PtrToField, FixedString name> () {
                FieldRuntimeReflectionInfo field( 
                    (const char*)name, 
                    get_type_id<field_type_t<T, PtrToField>>(), 
                    get_offset(PtrToField)
                );
                fields.push_back(field);
            });
        }
        
        return reflect::register_type(
            get_type_id<T>(),
            sizeof(T),
            [] (void* ptr) { new (ptr) T(); },
            fields
            );
    }
}



