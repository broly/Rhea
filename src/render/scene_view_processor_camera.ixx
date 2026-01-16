export module render:scene_view_proxy.camera;

import render_scene;
import glm;
import <string>;
import <vector>;
import name;
import rhcomponents;

#include "scene_proxy_boilerplate.h"
#include "object/object_reflection_macro.h"

export struct RenderObject_Camera
{
    glm::vec3 position;
    glm::mat4 view;
    
    
    float fov;
    float near;
    float far;
    bool active;
    Name debug_name;
    
    glm::mat4 get_projection(float aspect) const;
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
REFLECT_OBJECT(SceneViewProcessor_Camera, SceneViewProcessor);