module framework:rhcomp_mesh;



import :actor;
import globals;
import engine;



void RhComp_StaticMesh::start()
{
    RhComp_Renderable::start();
    render_id = RhGlobals::engine->scene_view->register_scene_view_proxy<SceneViewProcessor_Mesh>();
}

void RhComp_StaticMesh::finish()
{
    RhComp_Renderable::finish();
    RhGlobals::engine->scene_view->unregister_scene_view_proxy<SceneViewProcessor_Mesh>(render_id);
}

static int dummy = SceneViewProcessor::register_scene_view_processor<SceneViewProcessor_Mesh>();