export module assets:asset_manager;

import <cstdint>;
import <map>;
import <string>;

import :mesh;
import :texture;

export class AssetManager
{
public:
    MeshHandle load_mesh(const std::string& rel_path);
    TextureHandle load_texture(const std::string& rel_path);


    static AssetManager& get();
    
    
    const StaticMesh& get_mesh(MeshHandle id);
    const Texture& get_texture(TextureHandle texture_handle);
    
    
    std::map<TextureHandle, Texture> loaded_textures;
    std::map<std::string, TextureHandle> texture_by_path;
    
    std::map<MeshHandle, StaticMesh> loaded_meshes;
    std::map<std::string, MeshHandle> mesh_by_path;
    
    uint32_t meshes_counter;
    uint32_t textures_counter;
};
