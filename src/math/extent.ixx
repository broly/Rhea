export module rhmath:extent;
import <cstdint>;

export struct Extent
{
    uint32_t width, height;
    
    constexpr Extent operator>>(uint32_t shift) const
    {
        return Extent{width >> shift, height >> shift};
    }
    
    constexpr bool operator==(const Extent& other) const
    {
        return width == other.width && height == other.height;
    }
    
    constexpr bool operator!=(const Extent& other) const
    {
        return !(*this == other);
    }
    
    constexpr bool is_zero() const
    {
        return width == 0 || height == 0;
    }
    
    constexpr bool is_not_zero() const
    {
        return !is_zero();
    }
};