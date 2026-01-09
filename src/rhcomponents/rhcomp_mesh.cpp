module rhcomponents:rhcomp_mesh;

import globals;
import engine;
#include "common/offsetof.h"
import <cassert>;
import <string>;
import <future>;


RhComp_StaticMesh::RhComp_StaticMesh()
{
    render_info.scene_proxy_offset = offsetof(RhComp_StaticMesh, scene_proxy);
    render_info.is_explicitly_null = false;
    render_info.processor_id = 0;  // SceneViewProcessor_Mesh
}


std::string get_texture_base_name(std::string path)
{
    size_t lastSlash = path.find_last_of('/');
    if (lastSlash != std::string::npos)
    {
        path.erase(lastSlash); 
    }

    const std::string from = "meshes";
    const std::string to   = "textures";

    size_t pos = path.find(from);
    if (pos != std::string::npos)
    {
        path.replace(pos, from.length(), to);
    }

    return path;
}

std::string clean_material_name(std::string name)
{
    size_t dotPos = name.find_last_of('.');
    if (dotPos != std::string::npos)
    {
        bool digitsOnly = true;
        for (size_t i = dotPos + 1; i < name.size(); ++i)
        {
            if (!std::isdigit(static_cast<unsigned char>(name[i])))
            {
                digitsOnly = false;
                break;
            }
        }

        if (digitsOnly)
        {
            name.erase(dotPos);
        }
    }

    const std::string suffix = "_usd";
    if (name.size() >= suffix.size() &&
        name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
    {
        name.erase(name.size() - suffix.size());
    }

    return name;
}


void RhComp_StaticMesh::start()
{
    
    RhComp_Renderable::start();
    update_scene_proxy();
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.processor_id, owner->name);
}

void RhComp_StaticMesh::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.processor_id);
}
void RhComp_StaticMesh::on_serialize(DependencyCollector* dc)
{
    if (!auto_retrieve_materials || !mesh.is_valid())
        return;

    auto& mesh_object = mesh.get();
    std::string texture_base_name = get_texture_base_name(mesh_object.name);

    for (auto material_name : mesh_object.material_names)
    {
        std::string cleaned_material_name = clean_material_name(material_name);

        std::string base_color_path = texture_base_name + "/" + cleaned_material_name + "_BaseColor.png";
        std::string normal_path     = texture_base_name + "/" + cleaned_material_name + "_Normal.png";
        std::string orm_path        = texture_base_name + "/" + cleaned_material_name + "_ORM.png";

        auto base_color_fut = AssetManager::get().load_texture_async(base_color_path);
        auto normal_fut     = AssetManager::get().load_texture_async(normal_path);
        auto orm_fut        = AssetManager::get().load_texture_async(orm_path);

        auto material_task = std::async(
            std::launch::async,
            [this,
             material_name,
             base_color_fut,
             normal_fut,
             orm_fut]()
            {
                PBRMaterial mat;

                if (auto tex = base_color_fut.get(); tex.is_valid())
                    mat.base_color = tex;

                if (auto tex = normal_fut.get(); tex.is_valid())
                    mat.normal = tex;

                if (auto tex = orm_fut.get(); tex.is_valid())
                    mat.occlusion_roughness_metallic = tex;

                mat.emissive = TextureHandle::invalid();

                mat.emissive_mult  = 1.f;
                mat.base_color_mult = 1.f;
                mat.occlusion_mult  = 1.f;
                mat.metallic_mult   = 1.f;
                mat.roughness_mult  = 1.f;

                materials.emplace(material_name, std::move(mat));
            });

        dc->push(std::async(std::launch::async, [base_color_fut]() { base_color_fut.wait(); }));
        dc->push(std::async(std::launch::async, [normal_fut]()     { normal_fut.wait(); }));
        dc->push(std::async(std::launch::async, [orm_fut]()        { orm_fut.wait(); }));

        dc->push(std::move(material_task));
    }
}

void RhComp_StaticMesh::update_scene_proxy()
{
    std::vector<PBRMaterial> mats;
    if (auto_retrieve_materials)
    {
        for (auto mat_name : mesh.get().material_names)
        {
            mats.push_back(materials[mat_name]);
        }
    }
    else
    {
        if (materials.contains("default"))
            mats.push_back(materials["default"]);
    }
    scene_proxy.materials = mats;
    scene_proxy.mesh = mesh;
    scene_proxy.transform = transform;
    scene_proxy.auto_retrieve_materials = auto_retrieve_materials;
    scene_proxy.debug_name = owner->name;
}

