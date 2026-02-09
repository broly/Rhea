module rhcomponents:rhcomp_reflection_capture;
import rhmath;
import globals;
import <cassert>;
import render;
import :scene_view_proxy.reflection_capture;
import paths;

#include "common/offsetof.h"

void RhComp_ReflectionCapture::on_init()
{
    if (!is_default)
    {
        render_info.scene_proxy_offset = offsetof(RhComp_ReflectionCapture, scene_proxy);
        render_info.is_explicitly_null = false;
        render_info.type_id = reflect::get_default<SceneViewProcessor_ReflectionCapture>()->index;
    }
}

void RhComp_ReflectionCapture::start()
{
    RhComp_Renderable::start();
    update_scene_proxy();
    
    auto reflection_captures_path = paths::get_cache_path() / "reflection_captures";
    
    const std::string irradiance_path = std::string("hdr/ibl_") + owner->name.to_string() + "_" + "irradiance" + ".exr";
    irradiance = AssetManager::get().load_cubemap(irradiance_path);
    
    const std::string prefiltered_env_path = std::string("hdr/ibl_") + owner->name.to_string() + "_" + "prefiltered_env" + ".exr";
    prefiltered_env = AssetManager::get().load_cubemap(prefiltered_env_path);
    
    scene_proxy.irradiance = irradiance;
    scene_proxy.prefiltered_env = prefiltered_env;
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.type_id, name);
}

void RhComp_ReflectionCapture::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.type_id);
}

void RhComp_ReflectionCapture::update_scene_proxy()
{
    scene_proxy.active = active;
}
