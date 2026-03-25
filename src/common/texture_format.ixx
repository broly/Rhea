export module texture_format;
import <exception>;

#include "assertion_macros.h"

export enum class TextureFormat
{
    Undefined, 
    RGB8, 
    RGBA8, 
    RGBA8_UNORM,
    RGBA8_SRGB,

    RGBA16F,
    RGBA32F,
    
    RG16F,
    
    R16F,
    
    R8F,
    
    R32F,

    Depth24Stencil8,
    Depth32F,
    Depth16UNorm,
};

export uint32_t bytes_per_pixel(TextureFormat format)
{
    switch (format)
    {
        case TextureFormat::RGBA16F:
            return 4 * sizeof(uint16_t);
        case TextureFormat::RGBA32F:
            return 4 * sizeof(uint32_t);
        default:
            todo();
    }
}
