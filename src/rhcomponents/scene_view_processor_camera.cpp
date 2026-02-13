module rhcomponents:scene_view_proxy.camera;
import render_scene;


glm::mat4 RenderObject_Camera::get_projection(float aspect) const
{
    auto p = glm::perspectiveRH_ZO(fov, aspect, near, far);
    p[1][1] *= -1; // Vulkan NDC
    return p;
}

RenderId SceneViewProcessor_Camera::register_proxy()
{
    if (!vacated_camera_ids.empty())
    {
        RenderId reuse_id = vacated_camera_ids.back();
        reuse_id.generation++;
        vacated_camera_ids.pop_back();
        return reuse_id;
    }
    uint32_t identifier = cameras.size();
    cameras.push_back({});
    const RenderId render_id(identifier, 0);
    
    return render_id;
}

void SceneViewProcessor_Camera::unregister_proxy(RenderId render_id)
{
    cameras[render_id.identifier] = {}; 
    vacated_camera_ids.push_back(render_id);
}

void SceneViewProcessor_Camera::process()
{
    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Camera>())
    {
        auto& camera_ro = cameras[submitted.render_id.identifier];
        
        camera_ro.debug_name = submitted.debug_name;
        camera_ro.active = submitted.active;
        camera_ro.far = submitted.far_plane;
        camera_ro.near = submitted.near_plane;
        camera_ro.fov = submitted.fov;
        camera_ro.view = submitted.transform.get_view();
        camera_ro.position = submitted.transform.position;
        camera_ro.forward = submitted.transform.forward();
        
        if (camera_ro.active)
            active_camera_id = submitted.render_id;
    }
}
