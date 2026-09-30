module;

#include <json/value.h>

export module gltf_scene;

import std.compat;
import rhobject;
import reflect;

import framework;
import render;
import name;
import assets;
import rhcomponents;

// Imports a glTF scene into the world: every mesh object of the file becomes a child entity
// (Name, Transform, MeshRenderer and, with `collision`, MeshCollider) of the entity with this component.
//
//     "GltfScene": { "asset_path": "gltf/sponza/Sponza.gltf", "textures_dir": "textures/sponza", "collision": true,
//                    "collision_overrides": { "wood_door": "box", "lamps_": "convex_hull", "decals_": "none" },
//                    "hidden_meshes": [ "master_material" ] }
export struct GltfScene
{
    [[=rh::edit, =rh::read_only]] std::string asset_path;
    [[=rh::edit, =rh::read_only]] std::string textures_dir;
    std::shared_ptr<Material> material;
    std::map<Name, std::string> test;

    // static collision for every mesh of the scene
    [[=rh::edit, =rh::read_only]] bool collision = false;
    // of the meshes without an override
    [[=rh::edit, =rh::read_only]] MeshCollision collision_type = MeshCollision::simplified;
    [[=rh::edit, =rh::read_only]] float simplify_error = 0.02f;
    // mesh name prefix -> collision type, the longest matching prefix wins
    std::map<std::string, MeshCollision> collision_overrides;
    // mesh name prefixes not imported at all (authoring leftovers: material swatches, ...)
    std::vector<std::string> hidden_meshes;

    MeshCollisionSettings get_collision_settings(std::string_view mesh_name) const;
};
