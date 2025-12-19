#pragma once
#include <cstdint>
#include <map>
#include <string>

#include "mesh.h"

class AssetManager
{
public:
    MeshHandle load_mesh(const std::string& rel_path);
    

    static AssetManager& get();
    
    
    const Mesh& get_mesh(MeshHandle id);
    
    std::map<MeshHandle, Mesh> loaded_meshes;
    std::map<std::string, MeshHandle> mesh_by_path;
    
    uint32_t meshes_counter;
};
