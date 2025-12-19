#pragma once
#include <string>

#include "common/type_macros.h"
#include "math/aabb.h"
#include "math/vertex.h"


struct Mesh
{
    DEFAULT_NON_COPYABLE(Mesh)
    
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
};

struct MeshHandle
{
    uint32_t id = 0;
    
    friend auto operator<=>( const MeshHandle& lhs, const MeshHandle& rhs)
    {
        return lhs.id <=> rhs.id;
    }
    
    bool is_valid() const
    {
        return id != 0;
    }
    
    static inline MeshHandle invalid()
    {
        return {};
    }
    
    const Mesh& get() const;
};
