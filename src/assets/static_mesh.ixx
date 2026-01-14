export module assets:mesh;

import <filesystem>;
import <string>;
import <json/value.h>;
import <type_traits>;
import :asset;
import :pbr_material;
import rhmath;
import hash_utils;

#include "common/type_macros.h"

export struct Primitive
{
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    std::optional<uint32_t> material_index;
    
};

export struct Geometry
{
    std::vector<Primitive> primitives;
};

export struct MeshNode
{
    uint32_t index;
    Transform transform;
};


export struct StaticMesh
{
    
    
    DEFAULT_NON_COPYABLE(StaticMesh)
    
    static std::optional<StaticMesh> create_from_file(const std::filesystem::path path);
    
    std::string name;
    AABB bounds;
    std::vector<Geometry> mesh_geometry;
    std::vector<MeshNode> nodes;
    std::vector<std::string> material_names;
};
static_assert(std::is_move_constructible_v<StaticMesh>);



export struct MeshHandle : AssetHandle<MeshHandle>
{
    
    const StaticMesh& get() const;
    
};

export struct MeshPrimHandle
{
    MeshHandle mesh;
    uint32_t geom_index;
    uint32_t prim_index;
    
    const Primitive& get() const;
    
    
    bool operator<(const MeshPrimHandle& other) const
    {
        if (mesh != other.mesh) return mesh < other.mesh;
        if (geom_index != other.geom_index) return geom_index < other.geom_index;
        return prim_index < other.prim_index;
    }
    
    bool operator==(const MeshPrimHandle& other) const
    {
        return mesh == other.mesh &&
               geom_index == other.geom_index &&
               prim_index == other.prim_index;
    }
};


export template<>
struct std::hash<MeshPrimHandle>
{
    size_t operator()(const MeshPrimHandle& h) const noexcept
    {
        size_t seed = 0;
        hash_combine(seed, h.mesh.id);
        hash_combine(seed, h.geom_index);
        hash_combine(seed, h.prim_index);
        return seed;
    }
};

export void serialize_json_value(MeshHandle& target, const Json::Value& value, DependencyCollector* dc);
