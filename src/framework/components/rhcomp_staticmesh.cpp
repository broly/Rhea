#include "rhcomp_staticmesh.h"
#include <fastgltf/core.hpp>
#include <fastgltf/types.hpp>
#include <fastgltf/tools.hpp>

#include "engine.h"
#include "globals.h"
#include "common/paths.h"



void RhComp_StaticMesh::start()
{
    RhComp_Transform::start();
    
    mesh = AssetManager::get().load_mesh(mesh_path);
    if (mesh.is_valid())
    {
        std::cout << mesh.get().name;
    }
}
