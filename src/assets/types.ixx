export module assets:types;

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