module;

#include <json/value.h>

export module rhcomponents:scene_view_proxy.sky;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import render_scene;
import :sky;
#include "object/object_reflection_macro.h"

// Render side of a SkyAtmosphere entity (and of its VolumetricClouds)
export struct RenderObject_Sky
{
    bool registered = false;
    SkyAtmosphere atmosphere;
    VolumetricClouds clouds;    // enabled = false: the sky has no clouds
};

export class SceneViewProcessor_Sky : public SceneViewProcessor
{
public:
    SceneViewProcessor_Sky()
    {
        scene_proxy_size = scene_view_proxy_size_v<SceneViewProxy_Sky>;
    }

    RenderId register_proxy() override;
    void unregister_proxy(RenderId render_id) override;
    void process() override;

    // The sky the renderer draws: the first registered one, nullptr when the level has none
    const RenderObject_Sky* get_active_sky() const;

    // indexed by RenderId::identifier
    std::vector<RenderObject_Sky> skies;
    std::vector<RenderId> vacated_sky_ids;
};
RH_OBJECT(SceneViewProcessor_Sky)
