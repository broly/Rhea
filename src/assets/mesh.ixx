export module assets:mesh;

import <filesystem>;
import <string>;
import <json/value.h>;

import :asset;
import rhmath;

#include "common/type_macros.h"


export struct Mesh
{
    DEFAULT_NON_COPYABLE(Mesh)
    
    static std::optional<Mesh> create_from_file(const std::filesystem::path path);
    
    std::string name;
    std::vector<Vertex> vertices;
    std::vector<uint32_t> indices;
    AABB bounds;
    
    uint32_t get_index_count() const
    {
        return indices.size();
    }
};
static_assert(std::is_move_constructible_v<Mesh>);



export struct MeshHandle : AssetHandle<MeshHandle>
{
    
    const Mesh& get() const;
    
};

export void serialize_json_value(MeshHandle& target, const Json::Value& value);
