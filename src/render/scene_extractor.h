#pragma once
#include <cstdint>
#include <vector>

#include "render_id.h"
#include "framework/material.h"
#include "framework/assets/mesh.h"
#include "math/rhea_math.h"


struct RenderObject_Mesh
{
    MeshHandle mesh;
    glm::mat4 world;
    AABB bounds;
    PBRMaterial material;
    std::string debug_name;
};

class SceneExtractor
{
public:
    
    SceneExtractor(std::shared_ptr<class World> in_world);

    void perform_extraction();
    
    RenderId register_ro_mesh();
    void unregister_ro_mesh(RenderId render_id);
    
    std::vector<RenderObject_Mesh> meshes;
    
    std::vector<RenderId> vacated_mesh_ids; 
    
    std::shared_ptr<class World> world;
    
};
