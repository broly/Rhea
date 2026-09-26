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

// Imports a glTF scene into the world: every mesh object of the file becomes a child entity
// (Name, Transform, MeshRenderer and, with `collision`, MeshCollider) of the entity with this component.
//
//     "GltfScene": { "asset_path": "gltf/sponza/Sponza.gltf", "textures_dir": "textures/sponza", "collision": true }
export struct GltfScene
{
    [[=rh::edit, =rh::read_only]] std::string asset_path;
    [[=rh::edit, =rh::read_only]] std::string textures_dir;
    std::shared_ptr<Material> material;
    std::map<Name, std::string> test;

    // static triangle mesh collision for every mesh of the scene
    [[=rh::edit, =rh::read_only]] bool collision = false;
};

// Component type and its import on spawn
export void install_gltf_scene(World& world);
