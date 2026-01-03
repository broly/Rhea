export module assets:material;
import :texture;
import glm;
import rhmath;
#include "common/reflect_macros.h"

export struct PBRMaterial
{
    
    TextureHandle base_color;
    TextureHandle emissive;
    TextureHandle normal;
    TextureHandle occlusion_roughness_metallic;
    
    float base_color_mult;
    float emissive_mult;
    float occlusion_mult;
    float roughness_mult;
    float metallic_mult;
};
REFLECT_STRUCT(PBRMaterial,
    base_color, emissive, occlusion_roughness_metallic, normal, 
    base_color_mult, emissive_mult, occlusion_mult, roughness_mult, metallic_mult);

