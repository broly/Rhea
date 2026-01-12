export module assets:asset_scene;

import :mesh;

export struct AssetSceneObject
{
    StaticMesh mesh;
    Transform transform;
};

export struct AssetSceneInfo
{
    std::vector<AssetSceneObject> objects;
    std::map<std::string, PBRMaterial> materials;
    
};


struct AssetSceneHandle
{
    static AssetSceneInfo load(const std::filesystem::path& path, const std::string& rel_textures_path);
};