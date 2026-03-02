export module render:sampler_desc;


import hash_utils;
import name;
import <type_traits>;
import <string_view>;

export enum class SamplerFilter : uint32_t
{
    Nearest,
    Linear,
    CubicExt,
    CubicImg,
};

export enum class SamplerMipMapMode : uint32_t
{
    Nearest,
    Linear,
};

export enum class SamplerAddressMode : uint32_t
{
    Repeat,
    MirroredRepeat,
    ClampToEdge,
    ClampToBorder,
    MirrorClampToEdge,
};

export enum class SamplerCompareOp : uint32_t
{
    Never,
    Less,
    Equal,
    LessOrEqual,
    Greater,
    NotEqual,
    GreaterOrEqual,
    Always
};

export enum class SamplerBorderColor : uint32_t
{
    float_transparent_black,
    int_transparent_black,
    float_opaque_black,
    int_opaque_black,
    float_opaque_white,
    int_opaque_white,
};

export struct SamplerDesc
{
    constexpr SamplerDesc() {}
    
    std::string_view name;
    
    SamplerFilter min_filter = SamplerFilter::Linear;
    SamplerFilter mag_filter = SamplerFilter::Linear;
    SamplerMipMapMode mipmap_mode = SamplerMipMapMode::Linear;

    SamplerAddressMode address_u = SamplerAddressMode::Repeat;
    SamplerAddressMode address_v = SamplerAddressMode::Repeat;
    SamplerAddressMode address_w = SamplerAddressMode::Repeat;

    float min_lod_bias = 0.0f;
    float min_lod = 0.0f;
    float max_lod = 1000.0F;

    bool anisotropy = false;
    float max_anisotropy = 1.0f;

    bool comparison = false;
    SamplerCompareOp compare_op = SamplerCompareOp::Always;

    SamplerBorderColor border_color = SamplerBorderColor::float_opaque_white;

    bool operator==(const SamplerDesc&) const = default;
};


export struct SamplerDescHash
{
    size_t operator()(const SamplerDesc& v) const noexcept
    {
        size_t h = 0;

        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.min_filter));
        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.mag_filter));
        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.mipmap_mode));

        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.address_u));
        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.address_v));
        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.address_w));

        hash_combine(h, std::hash<float>{}(v.min_lod_bias));
        hash_combine(h, std::hash<float>{}(v.min_lod));
        hash_combine(h, std::hash<float>{}(v.max_lod));

        hash_combine(h, std::hash<bool>{}(v.anisotropy));
        hash_combine(h, std::hash<float>{}(v.max_anisotropy));

        hash_combine(h, std::hash<bool>{}(v.comparison));
        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.compare_op));

        hash_combine(h, std::hash<uint32_t>{}((uint32_t)v.border_color));

        return h;
    }
};

export namespace samplers
{
    constexpr SamplerDesc default_surface()
    {
        SamplerDesc surface_sampler{};
        surface_sampler.name = "surface";
        
        surface_sampler.min_filter   = SamplerFilter::Linear;
        surface_sampler.mag_filter   = SamplerFilter::Linear;
        surface_sampler.mipmap_mode  = SamplerMipMapMode::Linear;

        surface_sampler.address_u = SamplerAddressMode::Repeat;
        surface_sampler.address_v = SamplerAddressMode::Repeat;
        surface_sampler.address_w = SamplerAddressMode::Repeat;

        surface_sampler.min_lod_bias = 0.0f;
        surface_sampler.min_lod      = 0.0f;
        surface_sampler.max_lod      = 1000.0f;

        surface_sampler.anisotropy     = true;
        surface_sampler.max_anisotropy = 16.0f; 

        surface_sampler.comparison = false;

        surface_sampler.border_color = SamplerBorderColor::float_opaque_white;
        
        return surface_sampler;
    }
    
    constexpr SamplerDesc default_shadow()
    {
        SamplerDesc sampler{};
        sampler.name = "shadow";
        sampler.min_filter   = SamplerFilter::Linear;
        sampler.mag_filter   = SamplerFilter::Linear;
        sampler.mipmap_mode  = SamplerMipMapMode::Linear;

        sampler.address_u = SamplerAddressMode::ClampToBorder;
        sampler.address_v = SamplerAddressMode::ClampToBorder;
        sampler.address_w = SamplerAddressMode::ClampToBorder;

        sampler.min_lod_bias = 0.0f;
        sampler.min_lod      = 0.0f;
        sampler.max_lod      = 1000.0f;

        sampler.anisotropy     = true;
        sampler.max_anisotropy = 8.0f; 

        sampler.comparison = true;
        sampler.compare_op = SamplerCompareOp::LessOrEqual;

        sampler.border_color = SamplerBorderColor::float_opaque_white;
        
        return sampler;
    }
    
    
    constexpr SamplerDesc default_depth()
    {
        SamplerDesc depth_sampler{};
        depth_sampler.name = "depth";
        depth_sampler.min_filter  = SamplerFilter::Nearest;
        depth_sampler.mag_filter  = SamplerFilter::Nearest;
        depth_sampler.mipmap_mode = SamplerMipMapMode::Nearest;

        depth_sampler.address_u = SamplerAddressMode::ClampToEdge;
        depth_sampler.address_v = SamplerAddressMode::ClampToEdge;
        depth_sampler.address_w = SamplerAddressMode::ClampToEdge;

        depth_sampler.anisotropy = false;
        depth_sampler.comparison = false;
        
        return depth_sampler;
    }
}