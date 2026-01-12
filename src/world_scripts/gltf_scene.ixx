export module gltf_scene;

import <string>;
import framework;
#include "common/reflect_macros.h"

#include "object/object_reflection_macro.h"
import fastgltf;
import glm;

class RhComp_GltfScene : public RhComponent
{
public:
    void on_serialize(DependencyCollector* dc) override;
    std::string asset_path;
    std::string textures_dir;
    
    
    
};
REFLECT_OBJECT_FIELDS(RhComp_GltfScene, RhComponent,
    asset_path, textures_dir);