#pragma once
#include <glm/vec4.hpp>

#include "assets/texture.h"

struct PBRMaterial
{
    glm::vec4 base_color;
    
    float metallic;
    float roughness;
    
    TextureHandle normal;
    TextureHandle occlusion;
};
