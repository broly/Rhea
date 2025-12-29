export module assets:material;
import :texture;
import glm;
import rhmath;
#include "common/reflect_macros.h"

export struct PBRMaterial
{
    vec4 base_color;
    
    float metallic;
    float roughness;
    
    TextureHandle albedo;
    TextureHandle normal;
    TextureHandle occlusion;
};
REFLECT_STRUCT(PBRMaterial,
    base_color, metallic, roughness, normal, occlusion);

