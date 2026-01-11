export module assets:asset_scene;

import :mesh;

struct AssetSceneObject
{
    StaticMesh mesh;
    Transform transform;
    PBRMaterial material;
    std::string material_name;
};

struct AssetSceneHandle
{
    static std::vector<AssetSceneObject> load(const std::filesystem::path& path);
};