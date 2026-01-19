export module reflect;
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
    
    // bool register_struct(std::string_view struct_name, size_t size);
    
    
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
    };
    
    bool register_type(TypeId type_id, size_t size, type_initializer initializer, 
        const std::vector<FieldRuntimeReflectionInfo>& fields);
    bool register_basic_type(TypeId type_id, size_t size, type_initializer initializer);
    
    RuntimeReflectionInfo* find_runtime_info(TypeId type_id);
    
    
    RuntimeReflectionInfo* find_runtime_info(Name type_name)
    {
        return find_runtime_info(TypeId(type_name));
    }
}



