export module render:scene_view_proxy.light;

import rhcomponents;
import render_scene;

import <array>;
import <vector>;
import glm;
import <algorithm>;
#include "scene_proxy_boilerplate.h"


export struct RenderObject_Light
{
    glm::vec3 position;
    glm::vec4 color;
    float attenuation;
};

export class SceneViewProcessor_Light : public SceneViewProcessor
{
public:
    SCENE_PROXY_BOILERPLATE(SceneViewProcessor_Light, SceneViewProcessor, SceneViewProxy_Light, 2);
    
    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;

    std::vector<RenderObject_Light> lights;
    std::vector<RenderId> vacated_light_ids; 
    
    // unoptimized
    template<size_t NumLights>
    std::tuple<
        std::array<RenderObject_Light, NumLights>,
        size_t
    > query_nearest_lights_limited(glm::vec3 origin)
    {
        std::array<RenderObject_Light, NumLights> result;
        std::vector<RenderObject_Light> sorted_lights = lights;
        
        std::sort(sorted_lights.begin(), sorted_lights.end(),
            [origin](const RenderObject_Light& a, const RenderObject_Light& b) {
                
                return glm::distance(a.position, origin) < glm::distance(b.position, origin);
            });
        
        for (int i = 0; i < NumLights; i++)
        {
            if (i < sorted_lights.size())
                result[i] = sorted_lights[i];
            else
            {
                result[i].position = {};
                result[i].color = {};
            }
        }
        
        return {result, std::min(NumLights, sorted_lights.size())};
    }
};
REGISTER_SCENE_PROXY_PROCESSOR(SceneViewProcessor_Light);
