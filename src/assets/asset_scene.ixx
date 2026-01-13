export module assets:asset_scene;

import :mesh;
import :helpers;


struct AssetSceneHandle
{
    static AssetSceneInfo load(const std::filesystem::path& path, const std::string& rel_textures_path);
};