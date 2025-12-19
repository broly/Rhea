#pragma once
#include <cstdint>
#include <vector>

#include "framework/assets/mesh.h"
#include "math/rhea_math.h"

struct RenderId
{
    uint64_t identifier;
};

struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;
};

class RenderExtractor
{
public:
    
    std::vector<RenderObject_Mesh> meshes;
    
};
