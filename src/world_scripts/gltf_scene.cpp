module gltf_scene;


import log;
import glm;
import rhcomponents;
import assets;
import <future>;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogImportGltf, Log);

void RhComp_GltfScene::on_serialize(DependencyCollector* dc)
{
    AssetSceneInfo scene = AssetManager::get().load_scene(asset_path, textures_dir);
    
    std::vector<std::shared_ptr<RhComp_StaticMesh>> pending_mesh_comps;
    for (auto& obj : scene.objects)
    {
        std::vector<std::shared_ptr<Material>> mesh_materials;
        for (auto& mat_name : obj.mesh.material_names)
            mesh_materials.push_back(scene.materials[mat_name]);
            
        auto mesh_name = obj.mesh.name;
        auto mesh_handle = AssetManager::get().store_mesh(std::move(obj.mesh));
        std::shared_ptr<RhComp_StaticMesh> comp = owner->add_component<RhComp_StaticMesh>(true);
        comp->name = mesh_name;
        comp->mesh = mesh_handle;
        comp->mats = mesh_materials;
        comp->transform = obj.transform;
        
        pending_mesh_comps.push_back(comp);  
    }
    
    std::vector<std::shared_future<void>> futures;
    
    for (auto comp : pending_mesh_comps)
    {
        for (auto& mat: comp->mats)
        {
            for (auto& [name, param] : mat->parameters)
            {
                if (param.is<TextureHandle>())
                {
                    if (param.as<TextureHandle>() != TextureHandle::invalid())
                        futures.push_back(param.as<TextureHandle>().resolve_async());
                }
            }
        }
    }
    
    auto materials_task = std::async([this, futures] ()
    {
        for (auto& fut : futures)
        {
            fut.wait();
        }
    });
    for (auto fut : futures)
        dc->push(std::async(std::launch::async, [fut]() { fut.wait(); }));
    
    dc->push(std::move(materials_task));
    // auto base_color_fut = AssetManager::get().load_texture_async(base_color_path);
    // auto normal_fut     = AssetManager::get().load_texture_async(normal_path);
    // auto orm_fut        = AssetManager::get().load_texture_async(orm_path);
    //
    // auto material_task = std::async(
    //     std::launch::async,
    //     [this,
    //      material_name,
    //      base_color_fut,
    //      normal_fut,
    //      orm_fut]()
    //     {
    //         PBRMaterial mat;
    //
    //         if (auto tex = base_color_fut.get(); tex.is_valid())
    //             mat.base_color = tex;
    //
    //         if (auto tex = normal_fut.get(); tex.is_valid())
    //             mat.normal = tex;
    //
    //         if (auto tex = orm_fut.get(); tex.is_valid())
    //             mat.occlusion_roughness_metallic = tex;
    //
    //         mat.emissive = TextureHandle::invalid();
    //
    //         mat.emissive_mult  = 1.f;
    //         mat.base_color_mult = 1.f;
    //         mat.occlusion_mult  = 1.f;
    //         mat.metallic_mult   = 1.f;
    //         mat.roughness_mult  = 1.f;
    //
    //         materials.emplace(material_name, std::move(mat));
    //     });
    //
    // dc->push(std::async(std::launch::async, [base_color_fut]() { base_color_fut.wait(); }));
    // dc->push(std::async(std::launch::async, [normal_fut]()     { normal_fut.wait(); }));
    // dc->push(std::async(std::launch::async, [orm_fut]()        { orm_fut.wait(); }));
    //
    // dc->push(std::move(material_task));
    
}