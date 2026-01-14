export module assets:pbr_material;
import :texture;
import glm;
import rhmath;
#include "common/reflect_macros.h"

export struct PBRMaterial
{    
    TextureHandle base_color = TextureHandle::invalid();
    TextureHandle emissive = TextureHandle::invalid();
    TextureHandle normal = TextureHandle::invalid();
    TextureHandle occlusion_roughness_metallic = TextureHandle::invalid();
    
    float base_color_mult = 1.f;
    float emissive_mult = 1.f;
    float occlusion_mult = 1.f;
    float roughness_mult = 1.f;
    float metallic_mult = 1.f;
};
REFLECT_STRUCT(PBRMaterial,
    base_color, emissive, occlusion_roughness_metallic, normal, 
    base_color_mult, emissive_mult, occlusion_mult, roughness_mult, metallic_mult);

