export module assets:asset_manager;

import std.compat;


import :mesh;
import :skeletal_mesh;
import :animation;
import :texture;
import :cubemap;
import :asset_scene;
#include "common/type_macros.h"

enum AssetManagerInit { asset_mgr_init };

export class AssetManager
{
public:
    NON_COPYABLE(AssetManager);
    
    AssetManager(AssetManagerInit);;
    
    MeshHandle load_mesh(const std::string& rel_path);
    SkeletalMeshHandle load_skeletal_mesh(const std::string& rel_path);
    AnimationClipHandle load_animation(const std::string& rel_path);
    // max_size: halved until both sides fit (Texture::shrink_to_fit), 0 keeps the file's size
    TextureHandle load_texture(const std::string& rel_path, uint32_t max_size = 0);
    AssetSceneInfo load_scene(const std::string& rel_path, const std::string& textures_rel_path);
    CubemapHandle load_cubemap(const std::string& rel_path);
    
    TextureHandle register_external_texture(Texture&& tex);
    
    
    MeshHandle store_mesh(StaticMesh&& mesh);
    CubemapHandle store_cubemap(Cubemap&& mesh);

    // Drop the CPU copy of a runtime asset (Renderer::release_mesh / release_texture free the GPU copies and call
    // these). A texture id is a slot of the bindless texture array: recycled only once no frame reads the slot.
    void forget_mesh(MeshHandle handle);
    void forget_texture(TextureHandle handle);
    void recycle_texture_id(uint32_t id);
    
    std::mutex mutex;
    std::unordered_map<std::string, std::shared_future<TextureHandle>> textures_in_flight;
    std::unordered_map<std::string, std::shared_future<CubemapHandle>> cubemaps_in_flight;
    
    // material textures: fitted to render.textures.max_size on the loading thread (get_texture_max_size)
    std::shared_future<TextureHandle> load_texture_async(const std::string& path);
    std::shared_future<CubemapHandle> load_cubemap_async(const std::string& path);


    static AssetManager& get();
    
    
    const StaticMesh& get_mesh(MeshHandle id);
    const SkeletalMesh& get_skeletal_mesh(SkeletalMeshHandle id);
    const AnimationClip& get_animation(AnimationClipHandle id);
    const Texture& get_texture(TextureHandle texture_handle);
    const Cubemap& get_cubemap(CubemapHandle cubemap_handle);
    
    
    std::map<TextureHandle, Texture> loaded_textures;
    std::map<std::string, TextureHandle> texture_by_path;
    
    std::map<CubemapHandle, Cubemap> loaded_cubemaps;
    std::map<std::string, CubemapHandle> cubemap_by_path;
    
    std::map<MeshHandle, StaticMesh> loaded_meshes;
    std::map<std::string, MeshHandle> mesh_by_path;
    
    std::map<SkeletalMeshHandle, SkeletalMesh> loaded_skeletal_meshes;
    std::map<std::string, SkeletalMeshHandle> skeletal_mesh_by_path;
    
    std::map<AnimationClipHandle, AnimationClip> loaded_animations;
    std::map<std::string, AnimationClipHandle> animation_by_path;
    
    std::vector<StaticMesh> stored_meshes;
    
    std::mutex load_texture_mutex;
    
    uint32_t meshes_counter;
    uint32_t skeletal_meshes_counter = 0;
    uint32_t animations_counter = 0;
    uint32_t textures_counter;
    uint32_t cubemaps_counter;

private:
    // under `mutex`: a recycled id first, the array stays small
    uint32_t allocate_texture_id();
    std::vector<uint32_t> free_texture_ids;
};
