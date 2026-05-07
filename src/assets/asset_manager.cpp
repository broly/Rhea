module assets:asset_manager;

import paths;
import log;
import <cassert>;
#include "logging/log_macro.h"


DEFINE_LOGGER(LogAssets, Log);

AssetManager::AssetManager(AssetManagerInit) 
    : meshes_counter(0)
    , textures_counter(0)
{
}

MeshHandle AssetManager::load_mesh(const std::string& rel_path)
{
    if (mesh_by_path.contains(rel_path))
        return mesh_by_path[rel_path];
    LogAssets.Log("Loading mesh: %s", rel_path.c_str());
    
    const std::filesystem::path path = paths::get_assets_path() / rel_path;
    
    std::optional<StaticMesh> mesh_opt = StaticMesh::create_from_file(path);
    
    if (!mesh_opt.has_value())
        return MeshHandle::invalid();
    
    StaticMesh mesh = std::move(*mesh_opt);
    
    mesh.name = rel_path;
    const uint32_t mesh_id = ++meshes_counter;
    
    const MeshHandle mesh_handle {mesh_id};
    
    mesh_by_path.insert({rel_path, mesh_handle});
    loaded_meshes.insert({mesh_handle, std::move(mesh)});

    return mesh_handle;
}

TextureHandle AssetManager::load_texture(const std::string& rel_path)
{
    if (texture_by_path.contains(rel_path))
    {
        LogAssets.Log("Texture %s already loaded", rel_path.c_str());
        return texture_by_path[rel_path];
    }
    LogAssets.Log("Loading texture: %s", rel_path.c_str());
    
    const std::filesystem::path path = paths::get_assets_path() / rel_path;
    
    std::optional<Texture> texture_opt = Texture::create_from_file(path);
    
    if (!texture_opt.has_value())
    {
        LogAssets.Log("Loading texture failed: %s", rel_path.c_str());
        return TextureHandle::invalid();
    }
    
    Texture texture = std::move(*texture_opt);
    assert(texture.extent.height > 0);
    
    std::scoped_lock<std::mutex> lock(mutex);
    
    texture.name = rel_path;
    const uint32_t texture_id = ++textures_counter;
    texture.id = texture_id;
    
    const TextureHandle texture_handle {texture_id};
    
    texture_by_path.insert({rel_path, texture_handle});
    loaded_textures.insert({texture_handle, std::move(texture)});

    return texture_handle;
}

AssetSceneInfo AssetManager::load_scene(const std::string& rel_path, const std::string& textures_rel_path)
{
    
    const std::filesystem::path path = paths::get_assets_path() / rel_path;
    
    return AssetSceneHandle::load(path, textures_rel_path);
    
}

CubemapHandle AssetManager::load_cubemap(const std::string& rel_path)
{
    if (cubemap_by_path.contains(rel_path))
    {
        LogAssets.Log("Texture %s already loaded", rel_path.c_str());
        return cubemap_by_path[rel_path];
    }
    LogAssets.Log("Loading texture: %s", rel_path.c_str());
    
    const std::filesystem::path path = paths::get_cache_path() / rel_path;
    
    std::optional<Cubemap> cubemap_opt = Cubemap::load(path);
    
    if (!cubemap_opt.has_value())
        return CubemapHandle::invalid();
    
    Cubemap cubemap = std::move(*cubemap_opt);
    
    std::scoped_lock<std::mutex> lock(mutex);
    
    cubemap.name = rel_path;
    const uint32_t cubemap_id = ++cubemaps_counter;
    cubemap.id = cubemap_id;
    
    const CubemapHandle cubemap_handle {cubemap_id};
    
    cubemap_by_path.insert({rel_path, cubemap_handle});
    loaded_cubemaps.insert({cubemap_handle, std::move(cubemap)});

    return cubemap_handle;
}

TextureHandle AssetManager::register_external_texture(Texture&& tex)
{
    const uint32_t texture_id = ++textures_counter;
    tex.id = texture_id;
    
    TextureHandle texture_handle {texture_id};
    texture_by_path[tex.transition_path] = texture_handle;
    loaded_textures[texture_handle] = std::move(tex);
    return texture_handle;
}

MeshHandle AssetManager::store_mesh(StaticMesh&& mesh)
{
    const uint32_t mesh_id = ++meshes_counter;
    
    const MeshHandle mesh_handle {mesh_id};
    
    loaded_meshes.insert({mesh_handle, std::move(mesh)});

    return mesh_handle;
}

CubemapHandle AssetManager::store_cubemap(Cubemap&& cubemap)
{
    const uint32_t cubemap_id = ++cubemaps_counter;
    
    const CubemapHandle cubemap_handle {cubemap_id};
    
    loaded_cubemaps.insert({cubemap_handle, std::move(cubemap)});
    
    
    return cubemap_handle;
}

std::shared_future<TextureHandle> AssetManager::load_texture_async(const std::string& path)
{
    std::lock_guard lock(mutex);

    if (auto it = textures_in_flight.find(path); it != textures_in_flight.end())
    {
        return it->second;
    }

    std::packaged_task<TextureHandle()> task([path]() {
        return get().load_texture(path);
    });

    auto future = task.get_future().share();

    textures_in_flight[path] = future;

    std::thread([task = std::move(task)]() mutable {
        task();
    }).detach();

    return future;
}

std::shared_future<CubemapHandle> AssetManager::load_cubemap_async(const std::string& path)
{
    std::lock_guard lock(mutex);

    if (auto it = cubemaps_in_flight.find(path); it != cubemaps_in_flight.end())
    {
        return it->second;
    }

    std::packaged_task<CubemapHandle()> task([path]() {
        return get().load_cubemap(path);
    });

    auto future = task.get_future().share();

    cubemaps_in_flight[path] = future;

    std::thread([task = std::move(task)]() mutable {
        task();
    }).detach();

    return future;
}


AssetManager& AssetManager::get()
{
    static AssetManager instance{asset_mgr_init};
    
    return instance;
}

const StaticMesh& AssetManager::get_mesh(MeshHandle id)
{
    return loaded_meshes[id];
}

const Texture& AssetManager::get_texture(TextureHandle texture_handle)
{
    return loaded_textures[texture_handle];
}

const Cubemap& AssetManager::get_cubemap(CubemapHandle cubemap_handle)
{
    return loaded_cubemaps[cubemap_handle];
}
