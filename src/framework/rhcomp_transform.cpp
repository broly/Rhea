module framework:rhcomp_transform;
import rhmath;
import globals;
import <cassert>;
import render_scene;

void RhComp_Transform::on_transform_changed()
{
    
}

void RhComp_Transform::set_transform(const Transform& in_transform)
{
    transform = in_transform;
    
    if (has_virtual_transform_callback)
        on_transform_changed();
    
    if (renderable)
    {
        // non-virtual submit if transform changed
        SceneViewProxy_Transform& proxy = get_typed_scene_proxy_address<SceneViewProxy_Transform>();
        proxy.transform = in_transform;
        render_submit();
    }
}

Transform RhComp_Transform::get_transform()
{
    return transform;
}

void RhComp_Transform::render_submit() const
{
    assert(render_info.is_explicitly_null || render_info.scene_proxy_offset >= sizeof(RhComp_Transform));
    const void* ptr = get_scene_proxy_address();
    RhGlobals::engine->scene_view->submit_raw(render_info.type_id, ptr);
}

void* RhComp_Transform::get_scene_proxy_address() const
{
    return const_cast<char*>(reinterpret_cast<const char*>(this)) + render_info.scene_proxy_offset;
}
