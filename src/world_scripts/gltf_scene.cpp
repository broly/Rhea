module gltf_scene;


import log;
import glm;
import rhcomponents;
import assets;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogImportGltf, Log);

void RhComp_GltfScene::on_serialize(DependencyCollector* dc)
{

    auto scene = AssetManager::get().load_scene(asset_path);

}