module rhcomponents:rhcomp_mesh;

import globals;
import engine;
#include "common/offsetof.h"
import <cassert>;
import <string>;
import <future>;
import render;
import :scene_view_proxy.mesh;


void RhComp_StaticMesh::on_init()
{
    if (!is_default)
    {
        render_info.scene_proxy_offset = offsetof(RhComp_StaticMesh, scene_proxy);
        render_info.is_explicitly_null = false;
        render_info.type_id = reflect::get_default<SceneViewProcessor_Mesh>()->index;
    }
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
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.type_id, owner->name);
}

void RhComp_StaticMesh::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.type_id);
}
void RhComp_StaticMesh::on_serialize(DependencyCollector* dc)
{

}

AABB RhComp_StaticMesh::get_aabb() const
{
    return mesh.get().bounds * transform;
}

void RhComp_StaticMesh::update_scene_proxy()
{
    scene_proxy.materials = materials;
    scene_proxy.mats = mats;
    scene_proxy.mesh = mesh;
    scene_proxy.transform = transform;
    scene_proxy.debug_name = owner->name;
}

