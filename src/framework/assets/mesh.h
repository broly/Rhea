#pragma once
#include <filesystem>
#include <string>
#include <json/value.h>

#include "asset.h"
#include "common/type_macros.h"
#include "math/aabb.h"
#include "math/vertex.h"


struct Mesh
{
    DEFAULT_NON_COPYABLE(Mesh)
    
    static std::optional<Mesh> create_from_file(const std::filesystem::path path);
    
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
};
static_assert(std::is_move_constructible_v<Mesh>);



struct MeshHandle : AssetHandle<MeshHandle>
{
    
    const Mesh& get() const;
    
};

void serialize_json_value(MeshHandle& target, const Json::Value& value);
