#include "asset_manager.h"

#include <iostream>
#include <fastgltf/core.hpp>
#include <fastgltf/tools.hpp>
#include <glm/ext/vector_common.hpp>

#include "engine.h"
#include "globals.h"
#include "mesh.h"
#include "common/paths.h"



MeshHandle AssetManager::load_mesh(const std::string& rel_path)
{
    if (mesh_by_path.contains(rel_path))
        return mesh_by_path[rel_path];
    
    const std::filesystem::path path = paths::get_assets_path() / rel_path;
    
    std::optional<Mesh> mesh_opt = Mesh::create_from_file(path);
    
    if (!mesh_opt.has_value())
        return MeshHandle::invalid();
    
    Mesh mesh = std::move(*mesh_opt);
    
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
        return texture_by_path[rel_path];
    
    const std::filesystem::path path = paths::get_assets_path() / rel_path;
    
    std::optional<Texture> texture_opt = Texture::create_from_file(path);
    
    if (!texture_opt.has_value())
        return TextureHandle::invalid();
    
    Texture texture = std::move(*texture_opt);
    
    texture.name = rel_path;
    const uint32_t texture_id = ++textures_counter;
    
    const TextureHandle texture_handle {texture_id};
    
    texture_by_path.insert({rel_path, texture_handle});
    loaded_textures.insert({texture_handle, std::move(texture)});

    return texture_handle;
}

AssetManager& AssetManager::get()
{
    return *RhGlobals::engine->asset_manager;
}

const Mesh& AssetManager::get_mesh(MeshHandle id)
{
    return loaded_meshes[id];
}

const Texture& AssetManager::get_texture(TextureHandle texture_handle)
{
    return loaded_textures[texture_handle];
}
