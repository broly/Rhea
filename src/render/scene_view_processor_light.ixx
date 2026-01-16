export module render:scene_view_proxy.light;

import rhcomponents;
import render_scene;

import <array>;
import <vector>;
import glm;
import <algorithm>;
#include "object/object_reflection_macro.h"


export struct RenderObject_Light
{
    glm::vec3 position;
    glm::vec4 color;
    float attenuation;
};

export class SceneViewProcessor_Light : public SceneViewProcessor
{
public:
    SceneViewProcessor_Light()
    {
        scene_proxy_size = scene_view_proxy_size_v<SceneViewProxy_Light>;
    }
    
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
REFLECT_OBJECT(SceneViewProcessor_Light, SceneViewProcessor);
