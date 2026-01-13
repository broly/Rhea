export module render:sampler_desc;


import hash_utils;
import <type_traits>;

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