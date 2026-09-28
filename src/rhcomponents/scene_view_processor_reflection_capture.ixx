module;

#include <json/value.h>

export module rhcomponents:scene_view_proxy.reflection_capture;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;
import assets;
import name;
import :reflection_capture;

import render_scene;
import glm;
#include "object/object_reflection_macro.h"

// Render side of a ReflectionCapture. The slot index (RenderId identifier) is the probe's slot in the
// renderer's probe arrays (ReflectionProbeSystem).
export struct RenderObject_ReflectionCapture
{
    bool registered = false;
    bool active = false;
    glm::vec3 position = glm::vec3(0.0f);
    glm::vec3 box_min = glm::vec3(0.0f);
    glm::vec3 box_max = glm::vec3(0.0f);
    float blend_distance = 1.0f;
    float intensity = 1.0f;
    float update_interval = 0.0f;
    bool rebake_on_enter = false;
    // bumped whenever the capture itself must be redone (registered, moved, reactivated)
    uint32_t capture_version = 0;
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

    // indexed by RenderId::identifier
    std::vector<RenderObject_ReflectionCapture> captures;
    std::vector<RenderId> vacated_capture_ids;
};
RH_OBJECT(SceneViewProcessor_ReflectionCapture)
