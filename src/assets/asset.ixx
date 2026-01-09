export module assets:asset;
import <cstdint>;
import <limits>;
import <string>;

#include "common/type_macros.h"

using AssetId = uint32_t;

static constexpr auto INVALID_ASSET_ID = std::numeric_limits<AssetId>::max();
static constexpr auto PENDING_ASSET_ID = std::numeric_limits<AssetId>::max() - 1;


export template<typename T>
struct AssetHandle 
{
    AUTO_SPACESHIP(AssetHandle, id);
    
    AssetId id = INVALID_ASSET_ID;
    std::string pending_path;
    
    void start_async_loading(const std::string& path)
    {
        pending_path = path;
        id = PENDING_ASSET_ID;
    }
    
    bool is_valid() const
    {
        return id != 0;
    }
    friend bool operator==(const AssetHandle<T>& lhs, const AssetHandle<T>& rhs)
    {
        return lhs.id == rhs.id;
    }
    
    friend bool operator!=(const AssetHandle<T>& lhs, const AssetHandle<T>& rhs)
    {
        return lhs.id != rhs.id;
    }
    
    static T invalid()
    {
        return {INVALID_ASSET_ID};
    }
};
