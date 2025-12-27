#pragma once
#include <glm/vec4.hpp>

#include "assets/texture.h"
#include "common/reflect.h"

struct PBRMaterial
{
    glm::vec4 base_color;
    
    float metallic;
    float roughness;
    
    TextureHandle albedo;
    TextureHandle normal;
    TextureHandle occlusion;
};
REFLECT_STRUCT(PBRMaterial,
    base_color, metallic, roughness, normal, occlusion);

