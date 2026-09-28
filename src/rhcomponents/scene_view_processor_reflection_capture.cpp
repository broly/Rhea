module rhcomponents;

import :scene_view_proxy.reflection_capture;

import std.compat;
import glm;

RenderId SceneViewProcessor_ReflectionCapture::register_proxy()
{
    RenderId render_id(0, 0);
    if (!vacated_capture_ids.empty())
    {
        render_id = vacated_capture_ids.back();
        render_id.generation++;
        vacated_capture_ids.pop_back();
    }
    else
    {
        render_id = RenderId(uint32_t(captures.size()), 0);
        captures.push_back({});
    }
    
    RenderObject_ReflectionCapture& capture = captures[render_id.identifier];
    const uint32_t version = capture.capture_version;
    capture = {};
    capture.registered = true;
    // a reused slot holds the previous owner's capture
    capture.capture_version = version + 1;
    return render_id;
}

void SceneViewProcessor_ReflectionCapture::unregister_proxy(RenderId render_id)
{
    RenderObject_ReflectionCapture& capture = captures[render_id.identifier];
    const uint32_t version = capture.capture_version;
    capture = {};
    capture.capture_version = version + 1;
    vacated_capture_ids.push_back(render_id);
}

void SceneViewProcessor_ReflectionCapture::process()
{
    for (const auto& submitted : read_submission_buffer<SceneViewProxy_ReflectionCapture>())
    {
        RenderObject_ReflectionCapture& capture = captures[submitted.render_id.identifier];
        if (!capture.registered)
            continue;
        
        const glm::vec3 position = submitted.transform.position.glm();
        if (position != capture.position || submitted.active != capture.active)
            capture.capture_version++;
        
        const glm::vec3 center = position + submitted.box_offset.glm();
        const glm::vec3 extent = glm::max(glm::abs(submitted.box_extent.glm()), glm::vec3(0.01f));
        
        capture.active = submitted.active;
        capture.position = position;
        capture.box_min = center - extent;
        capture.box_max = center + extent;
        capture.blend_distance = std::max(submitted.blend_distance, 0.001f);
        capture.intensity = std::max(submitted.intensity, 0.0f);
        capture.update_interval = std::max(submitted.update_interval, 0.0f);
        capture.rebake_on_enter = submitted.rebake_on_enter;
        capture.debug_name = submitted.debug_name;
    }
}
