export module texture_format;

export enum class TextureFormat
{
    Undefined, 
    RGB8, 
    RGBA8, 
    RGBA8_UNORM,
    RGBA8_SRGB,

    RGBA16F,
    RGBA32F,

    Depth24Stencil8,
    Depth32F,
    Depth16UNorm,
};
