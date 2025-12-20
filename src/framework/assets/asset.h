#pragma once
#include <cstdint>

#include "common/type_macros.h"

template<typename T>
struct AssetHandle
{
    AUTO_SPACESHIP(AssetHandle, id)
    
    uint32_t id;
    
    bool is_valid() const
    {
        return id != 0;
    }
    
    static T invalid()
    {
        return {};
    }
};
