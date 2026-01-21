export module assets:types;
import :material;
import :mesh;


export struct AssetSceneObject
{
    StaticMesh mesh;
    Transform transform;
};

export struct AssetSceneInfo
{
    std::vector<AssetSceneObject> objects;
    std::map<std::string, std::shared_ptr<Material>> materials;
    
};