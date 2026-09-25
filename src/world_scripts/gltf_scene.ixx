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
    [[=rh::serialize]] std::string asset_path;
    [[=rh::serialize]] std::string textures_dir;
    
    [[=rh::serialize]] std::shared_ptr<Material> material;
    
    [[=rh::serialize]] std::map<Name, std::string> test;

    // static triangle mesh collision for every mesh of the scene
    [[=rh::serialize]] bool collision = false;
    
    
    
};
RH_OBJECT(RhComp_GltfScene)