module rhcomponents:rhcomp_reflection_capture;
import rhmath;
import globals;
import <cassert>;
import render;
import :scene_view_proxy.camera;
import paths;

#include "common/offsetof.h"

void RhComp_ReflectionCapture::on_init()
{
    if (!is_default)
    {
        render_info.scene_proxy_offset = offsetof(RhComp_ReflectionCapture, scene_proxy);
        render_info.is_explicitly_null = false;
        render_info.type_id = reflect::get_default<SceneViewProcessor_Camera>()->index;
    }
}

void RhComp_ReflectionCapture::start()
{
    RhComp_Renderable::start();
    update_scene_proxy();
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.type_id, name);
    
    auto reflection_captures_path = paths::get_cache_path() / "reflection_captures";
    
    //auto render_graph = RhGlobals::engine->renderer->get_render_graph("scene_capture");
}

void RhComp_ReflectionCapture::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.type_id);
}

void RhComp_ReflectionCapture::update_scene_proxy()
{
    scene_proxy.fov = fov;
    scene_proxy.active = active;
    scene_proxy.far_plane = far_plane;
    scene_proxy.near_plane = near_plane;
}
