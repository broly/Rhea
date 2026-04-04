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
    
    constexpr Extent operator/(const uint32_t d) const
    {
        return Extent{width / d, height / d};
    }
    
    constexpr Extent operator/(const float d) const
    {
        return Extent{(uint32_t)(width / d), (uint32_t)(height / d)};
    }
    
    constexpr Extent operator*(const float m) const
    {
        return Extent{(uint32_t)(width * m), (uint32_t)(height * m)};
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