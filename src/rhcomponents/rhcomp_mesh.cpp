module rhcomponents:rhcomp_mesh;

import globals;
import engine;
#include "common/offsetof.h"



RhComp_StaticMesh::RhComp_StaticMesh()
{
    render_info.scene_proxy_offset = offsetof(RhComp_StaticMesh, scene_proxy);
    render_info.is_explicitly_null = false;
    render_info.processor_id = 0;  // SceneViewProcessor_Mesh
}

void RhComp_StaticMesh::start()
{
    RhComp_Renderable::start();
    update_scene_proxy();
    RhGlobals::engine->scene_view->register_scene_view_proxy(scene_proxy, render_info.processor_id, name);
}

void RhComp_StaticMesh::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy(scene_proxy, render_info.processor_id);
}

void RhComp_StaticMesh::update_scene_proxy()
{
    scene_proxy.material = material;
    scene_proxy.mesh = mesh;
    scene_proxy.transform = transform;
}

