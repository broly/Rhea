export module assets:asset;
import <cstdint>;

#include "common/type_macros.h"

export template<typename T>
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
