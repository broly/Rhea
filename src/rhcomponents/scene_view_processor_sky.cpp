module rhcomponents;

import :scene_view_proxy.sky;

import std.compat;
import render_scene;

RenderId SceneViewProcessor_Sky::register_proxy()
{
    RenderId render_id(0, 0);
    if (!vacated_sky_ids.empty())
    {
        render_id = vacated_sky_ids.back();
        render_id.generation++;
        vacated_sky_ids.pop_back();
    }
    else
    {
        render_id = RenderId(uint32_t(skies.size()), 0);
        skies.push_back({});
    }

    skies[render_id.identifier] = {};
    skies[render_id.identifier].registered = true;
    return render_id;
}

void SceneViewProcessor_Sky::unregister_proxy(RenderId render_id)
{
    skies[render_id.identifier] = {};
    vacated_sky_ids.push_back(render_id);
}

void SceneViewProcessor_Sky::process()
{
    for (const auto& submitted : read_submission_buffer<SceneViewProxy_Sky>())
    {
        RenderObject_Sky& sky = skies[submitted.render_id.identifier];
        if (!sky.registered)
            continue;
        sky.atmosphere = submitted.atmosphere;
        sky.clouds = submitted.clouds;
    }
}

const RenderObject_Sky* SceneViewProcessor_Sky::get_active_sky() const
{
    for (const RenderObject_Sky& sky : skies)
        if (sky.registered)
            return &sky;
    return nullptr;
}
