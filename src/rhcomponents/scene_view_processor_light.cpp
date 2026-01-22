module rhcomponents:scene_view_proxy.light;
import render_scene;


RenderId SceneViewProcessor_Light::register_proxy()
{
    if (!vacated_light_ids.empty())
    {
        RenderId reuse_id = vacated_light_ids.back();
        reuse_id.generation++;
        vacated_light_ids.pop_back();
        return reuse_id;
    }
    uint32_t identifier = lights.size();
    lights.push_back({});
    const RenderId render_id(identifier, 0);
    
    return render_id;
}

void SceneViewProcessor_Light::unregister_proxy(RenderId render_id)
{
    lights[render_id.identifier] = {}; 
    vacated_light_ids.push_back(render_id);
}



void SceneViewProcessor_Light::process()
{
    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Light>())
    {
        auto& light_ro = lights[submitted.render_id.identifier];
        
        
        light_ro.position = submitted.transform.position;
        light_ro.color = submitted.color;
        light_ro.direction = glm::normalize(submitted.transform.forward());
        light_ro.type = submitted.light_type;
        // light_ro.attenuation = submitted.attenuation;
    }
}


