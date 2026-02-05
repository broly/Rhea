export module assets:asset_manager;

import <cstdint>;
import <map>;
import <string>;

import :mesh;
import :texture;
import :cubemap;
import <future>;
import :asset_scene;
#include "common/type_macros.h"

enum AssetManagerInit { asset_mgr_init };

export class AssetManager
{
public:
    NON_COPYABLE(AssetManager);
    
    AssetManager(AssetManagerInit);;
    
    MeshHandle load_mesh(const std::string& rel_path);
    TextureHandle load_texture(const std::string& rel_path);
    AssetSceneInfo load_scene(const std::string& rel_path, const std::string& textures_rel_path);
    CubemapHandle load_cubemap(const std::string& rel_path);
    
    
    MeshHandle store_mesh(StaticMesh&& mesh);
    CubemapHandle store_cubemap(Cubemap&& mesh);
    
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_future<TextureHandle>> textures_in_flight;
    std::unordered_map<std::string, std::shared_future<CubemapHandle>> cubemaps_in_flight;
    
    std::shared_future<TextureHandle> load_texture_async(const std::string& path);
    std::shared_future<CubemapHandle> load_cubemap_async(const std::string& path);


    static AssetManager& get();
    
    
    const StaticMesh& get_mesh(MeshHandle id);
    const Texture& get_texture(TextureHandle texture_handle);
    const Cubemap& get_cubemap(CubemapHandle cubemap_handle);
    
    
    std::map<TextureHandle, Texture> loaded_textures;
    std::map<std::string, TextureHandle> texture_by_path;
    
    std::map<CubemapHandle, Cubemap> loaded_cubemaps;
    std::map<std::string, CubemapHandle> cubemap_by_path;
    
    std::map<MeshHandle, StaticMesh> loaded_meshes;
    std::map<std::string, MeshHandle> mesh_by_path;
    
    std::vector<StaticMesh> stored_meshes;
    
    std::mutex load_texture_mutex;
    
    uint32_t meshes_counter;
    uint32_t textures_counter;
    uint32_t cubemaps_counter;
};
