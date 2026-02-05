export module rhcomponents:scene_view_proxy.reflection_capture;
import assets;
import name;
import :rhcomp_reflection_capture;

import render_scene;
#include "object/object_reflection_macro.h"

export struct RenderObject_ReflectionCapture
{
    CubemapHandle cubemap;
    vec3 position;
    Name debug_name;
};

export class SceneViewProcessor_ReflectionCapture : public SceneViewProcessor
{
public:
    SceneViewProcessor_ReflectionCapture()
    {
        scene_proxy_size = scene_view_proxy_size_v<SceneViewProxy_ReflectionCapture>;
    }
    
    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;

    std::vector<RenderObject_ReflectionCapture> cubemaps;
    std::vector<RenderId> vacated_cubemap_ids;
};
REFLECT_OBJECT(SceneViewProcessor_ReflectionCapture, SceneViewProcessor);