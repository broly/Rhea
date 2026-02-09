module rhcomponents:scene_view_proxy.reflection_capture;
import globals;
import <algorithm>;

RenderId SceneViewProcessor_ReflectionCapture::register_proxy()
{
    if (!vacated_cubemap_ids.empty())
    {
        RenderId reuse_id = vacated_cubemap_ids.back();
        reuse_id.generation++;
        vacated_cubemap_ids.pop_back();
        return reuse_id;
    }
    uint32_t identifier = cubemaps.size();
    cubemaps.push_back({});
    const RenderId render_id(identifier, 0);
    
    return render_id;
}

void SceneViewProcessor_ReflectionCapture::unregister_proxy(RenderId render_id)
{
    cubemaps[render_id.identifier] = {}; 
    vacated_cubemap_ids.push_back(render_id);
}



void SceneViewProcessor_ReflectionCapture::process()
{
    for (const auto& submitted : read_submission_buffer<SceneViewProxy_ReflectionCapture>())
    {
        auto& cubemap_ro = cubemaps[submitted.render_id.identifier];
        
        auto renderer = RhGlobals::engine->renderer;  // crutch
        
        const CubemapHandle irradiance = submitted.irradiance;
        const CubemapHandle prefiltered_env = submitted.prefiltered_env;
        irradiance.get();
        prefiltered_env.get();
        
        cubemap_ro.debug_name = submitted.debug_name;
        cubemap_ro.irradiance = irradiance;
        cubemap_ro.prefiltered_env = prefiltered_env;
    }
}

std::optional<RenderObject_ReflectionCapture> SceneViewProcessor_ReflectionCapture::query_nearest(glm::vec3 origin) const
{
    std::vector<RenderObject_ReflectionCapture> all = cubemaps;
        
    std::sort(all.begin(), all.end(),
        [origin](const RenderObject_ReflectionCapture& a, const RenderObject_ReflectionCapture& b) {
                
            return glm::distance(a.position, origin) <
                   glm::distance(b.position, origin);
        });
    
    if (all.empty())
        return std::nullopt;
    return all[0];
}
