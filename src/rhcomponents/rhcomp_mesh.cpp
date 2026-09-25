module rhcomponents;

import :rhcomp_mesh;

import std.compat;

import globals;
import engine;
import render;
import :scene_view_proxy.mesh;
import physics;
import glm;
#include "common/offsetof.h"


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
    create_collision_body();
}

void RhComp_StaticMesh::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.type_id);
    destroy_collision_body();
}

void RhComp_StaticMesh::cook_collision(phys::PhysicsScene& physics, const StaticMesh& static_mesh, std::string_view cache_key)
{
    // scale is baked into the vertices, the body carries position and rotation
    const glm::vec3 scale = transform.scale.glm();

    phys::TriangleMeshShape triangles;
    for (const Geometry& geometry : static_mesh.mesh_geometry)
    {
        for (const Primitive& primitive : geometry.primitives)
        {
            const uint32_t base = (uint32_t)triangles.vertices.size();
            for (const Vertex& vertex : primitive.vertices)
                triangles.vertices.push_back(vertex.position * scale);
            for (uint32_t index : primitive.indices)
                triangles.indices.push_back(base + index);
        }
    }

    collision_shape = physics.create_mesh_shape(triangles, cache_key);
}

void RhComp_StaticMesh::create_collision_body()
{
    if (!collision || collision_body.is_valid())
        return;

    phys::PhysicsScene& physics = owner->get_world()->get_physics();
    if (!collision_shape && mesh.is_valid())
        cook_collision(physics, mesh.get(), owner->name.to_string() + "__" + name.to_string());
    if (!collision_shape)
        return;

    collision_body = physics.create_body({
        .shape = collision_shape,
        .position = transform.position.glm(),
        .rotation = transform.rotation.glm(),
        .motion = phys::Motion::fixed,
        .category = phys::Category::static_world,
        .user_data = reinterpret_cast<uint64_t>(this),
    });
}

void RhComp_StaticMesh::destroy_collision_body()
{
    if (!collision_body.is_valid())
        return;
    owner->get_world()->get_physics().destroy_body(collision_body);
    collision_body = {};
}
void RhComp_StaticMesh::on_serialize(const SerializationContext& context)
{

}

AABB RhComp_StaticMesh::get_aabb() const
{
    return mesh.get().bounds * transform;
}


void RhComp_StaticMesh::on_property_changed(std::string_view property)
{
    update_scene_proxy();
    RhComp_Renderable::on_property_changed(property);
}

void RhComp_StaticMesh::update_scene_proxy()
{
    scene_proxy.materials = mats;
    scene_proxy.mesh = mesh;
    scene_proxy.bounds = get_aabb();
    scene_proxy.transform = transform;
    scene_proxy.debug_name = owner->name;
}

