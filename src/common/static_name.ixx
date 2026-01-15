export module static_name;

import <stdint.h>;
import <compare>;
import fixed_string;

constexpr char to_lower_ascii(char c)
{
    return (c >= 'A' && c <= 'Z') ? (c + 32) : c;
}

constexpr uint32_t fnv1a_lower(const char* str, size_t len)
{
    uint32_t hash = 2166136261u;
    for (size_t i = 0; i < len; ++i)
    {
        hash ^= uint32_t(to_lower_ascii(str[i]));
        hash *= 16777619u;
    }
    return hash;
}

export struct StaticName
{
    uint32_t value;

    template<size_t N>
    constexpr explicit StaticName(const FixedString<N>& fs)
        : value(fnv1a_lower(fs.Buffer, N))
    {}

    constexpr auto operator<=>(const StaticName& other) const = default;
};

export template<FixedString FS>
constexpr StaticName operator"" _name ()
{
    return FS;
}