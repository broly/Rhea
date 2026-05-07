export module rhmath:extent;
import <cstdint>;
import glm;

export struct Extent
{
    uint32_t width, height;
    
    constexpr Extent() = default;
    constexpr Extent(const uint32_t w, const uint32_t h)
        : width{w}, height{h}
    {}
    
    constexpr Extent(const glm::uvec2& vec)
        : width{vec.x}, height{vec.y}
    {}
    
    
    constexpr Extent(const glm::ivec2& vec)
        : width{(uint32_t)vec.x}, height{(uint32_t)vec.y}
    {}
    
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