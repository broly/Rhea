export module render:shader_key;

import std.compat;


export struct ShaderKey
{
    uint64_t key;
    
    auto operator<=>(const ShaderKey&) const = default;
};



export template<>
struct std::hash<ShaderKey>
{
    size_t operator()(const ShaderKey& h) const noexcept
    {
        return h.key;
    }
};