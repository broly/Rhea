export module assets:asset;
import <cstdint>;
import <limits>;

#include "common/type_macros.h"

using AssetId = uint32_t;

static constexpr auto INVALID_ASSET_ID = std::numeric_limits<AssetId>::max();

export template<typename T>
struct AssetHandle
{
    AUTO_SPACESHIP(AssetHandle, id)
    
    AssetId id = INVALID_ASSET_ID;
    
    bool is_valid() const
    {
        return id != 0;
    }
    
    static T invalid()
    {
        return {INVALID_ASSET_ID};
    }
};
