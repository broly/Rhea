export module assets:material_parameter_type;


import <variant>;
import linear_color;
import :texture;
import name;
#include "common/assertion_macros.h"

export struct MaterialParameterType
{
    std::variant<float, LinearColor, TextureHandle, Name> data;
    
    template<typename T>
    auto& operator=(T&& val)
    {
        data = val;
        return *this;
    }
    
    template<typename T>
    bool is() const
    {
        return std::holds_alternative<T>(data);
    }
    
    const void* get_raw() const
    {
        checkf(!std::holds_alternative<Name>(data) && !std::holds_alternative<Name>(data), "Can read only float or LinearColor in memory");
        return &data;
    }
    template<typename T>
    T& as()
    {
        return std::get<T>(data);
    }
};
