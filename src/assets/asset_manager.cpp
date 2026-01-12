module assets:asset_manager;




import globals;
import paths;
import log;
#include "logging/log_macro.h"


DEFINE_LOGGER(LogAssets, Log);

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
    
    texture.name = rel_path;
    const uint32_t texture_id = ++textures_counter;
    
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

MeshHandle AssetManager::store_mesh(StaticMesh&& mesh)
{
    const uint32_t mesh_id = ++meshes_counter;
    
    const MeshHandle mesh_handle {mesh_id};
    
    loaded_meshes.insert({mesh_handle, std::move(mesh)});

    return mesh_handle;
}

std::shared_future<TextureHandle> AssetManager::load_texture_async(const std::string& path)
{
    std::lock_guard lock(mutex);

    if (auto it = in_flight.find(path); it != in_flight.end())
    {
        return it->second;
    }

    std::packaged_task<TextureHandle()> task([path]() {
        return RhGlobals::engine->asset_manager->load_texture(path);
    });

    auto future = task.get_future().share();

    in_flight[path] = future;

    std::thread([task = std::move(task)]() mutable {
        task();
    }).detach();

    return future;
}


AssetManager& AssetManager::get()
{
    return *RhGlobals::engine->asset_manager;
}

const StaticMesh& AssetManager::get_mesh(MeshHandle id)
{
    return loaded_meshes[id];
}

const Texture& AssetManager::get_texture(TextureHandle texture_handle)
{
    return loaded_textures[texture_handle];
}
