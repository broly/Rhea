export module rhmath:extent;
import <cstdint>;

export struct Extent
{
    uint32_t width, height;
    
    bool operator==(const Extent& other) const
    {
        return width == other.width && height == other.height;
    }
    
    bool operator!=(const Extent& other) const
    {
        return !(*this == other);
    }
    
    bool is_zero() const
    {
        return width == 0 || height == 0;
    }
    
    bool is_not_zero() const
    {
        return !is_zero();
    }
};