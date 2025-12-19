#include "mesh.h"

#include "engine.h"
#include "globals.h"



const Mesh& MeshHandle::get() const
{
    return RhGlobals::engine->asset_manager->get_mesh(*this);
}
