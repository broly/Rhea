export module enum_helpers;

import <type_traits>;

template<typename T, typename E>
concept enum_compatible = std::is_same_v<E, T> || std::is_integral_v<T>;

export template<typename Enum>
struct Mask
{
    using underlying = std::underlying_type_t<Enum>;
    
    template<enum_compatible<Enum> T>
    constexpr Mask(T value)
        : value(static_cast<Enum>(value))
    {}
    
    Enum value;
    
    constexpr friend Mask operator~(Mask lhs)
    {
        return static_cast<Mask>(~static_cast<underlying>(lhs.value));
    }
    
    template<enum_compatible<Enum> T>
    constexpr friend Mask operator|(Mask lhs, T rhs)
    {
        return static_cast<Mask>(static_cast<underlying>(lhs.value) | static_cast<underlying>(rhs));
    }
    template<enum_compatible<Enum> T>
    constexpr friend Mask operator&(Mask lhs, T rhs)
    {
        return static_cast<Mask>(static_cast<underlying>(lhs.value) & static_cast<underlying>(rhs));
    }
    
    constexpr operator underlying() const
    {
        return static_cast<underlying>(value);
    }
    
    constexpr operator bool() const
    {
        return !!static_cast<underlying>(value);
    }
    
    constexpr bool operator!() const
    {
        return !static_cast<underlying>(value);
    }
    
    constexpr operator Enum() const
    {
        return value;
    }
};
