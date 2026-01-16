module rhcomponents:rhcomp_camera;
import rhmath;
import globals;
import <cassert>;
#include "common/offsetof.h"

RhComp_Camera::RhComp_Camera()
{
    
    render_info.scene_proxy_offset = offsetof(RhComp_Camera, scene_proxy);
    render_info.is_explicitly_null = false;
    render_info.type_id = 1;  // SceneViewProcessor_Camera
}

void RhComp_Camera::start()
{
    RhComp_Renderable::start();
    update_scene_proxy();
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.type_id, name);
}

void RhComp_Camera::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.type_id);
}

void RhComp_Camera::update_scene_proxy()
{
    scene_proxy.fov = fov;
    scene_proxy.active = active;
    scene_proxy.far_plane = far_plane;
    scene_proxy.near_plane = near_plane;
}
