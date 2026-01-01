export module render:scene_view_proxy.camera;

import render_scene;
import glm;
import <string>;
import <vector>;
import rhcomponents;

#include "scene_proxy_boilerplate.h"

export struct RenderObject_Camera
{
    // glm::mat4 world;
    glm::mat4 view;
    
    
    float fov;
    float near;
    float far;
    bool active;
    std::string debug_name;
    
    glm::mat4 get_projection(float aspect) const
    {
        auto p = glm::perspective(fov, aspect, near, far);
        p[1][1] *= -1; // Vulkan NDC
        return p;
    }
};


export class SceneViewProcessor_Camera : public SceneViewProcessor
{
public:
    SCENE_PROXY_BOILERPLATE(SceneViewProcessor_Camera, SceneViewProcessor, SceneViewProxy_Camera, 1);
    
    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;
    
    std::vector<RenderObject_Camera> cameras;
    std::vector<RenderId> vacated_camera_ids; 
    
    const RenderObject_Camera* get_active_camera() const
    {
        if (cameras.size() > active_camera_id.identifier)
        {
            return &cameras[active_camera_id.identifier];
        }
        return nullptr;
    }
    
    RenderId active_camera_id {0};
};
REGISTER_SCENE_PROXY_PROCESSOR(SceneViewProcessor_Camera);