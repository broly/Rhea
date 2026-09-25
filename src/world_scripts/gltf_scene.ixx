module;

#include <json/value.h>

export module gltf_scene;

import std.compat;
import rhobject;
import fixed_string;
import reflect;
import type_id;
import dependency_collector;

import framework;
import fastgltf;
import glm;
import render;
import name;
import assets;
#include "common/reflect_macros.h"

#include "object/object_reflection_macro.h"

class RhComp_GltfScene : public RhComponent
{
public:
    void on_serialize(const SerializationContext& context) override;
    std::string asset_path;
    std::string textures_dir;
    
    std::shared_ptr<Material> material;
    
    std::map<Name, std::string> test;
    
    
    
};
REFLECT_OBJECT_FIELDS(RhComp_GltfScene, RhComponent,
    asset_path, textures_dir, material, test);