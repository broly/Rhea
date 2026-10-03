module rhcomponents;

import :camera;
import :render_sync;

import std.compat;
import ecs;
import rhmath;
import framework;
import audio;
import glm;

#include "ecs/ecs_macros.h"

namespace
{
    // The ears are where the active camera is (Late: its WorldTransform of this frame)
    [[=ecs::system<ecs::Phase::Late>, =ecs::after<scene::TransformPropagation>, =ecs::after<RenderSync>,
      =ecs::after<MeshColliderSync>]]
    void sync_audio_listener(ecs::Query<const Camera, const WorldTransform> cameras, ecs::ResMut<audio::AudioEngine> audio,
        ecs::Res<ecs::FrameTime> time)
    {
        cameras.each([&] (const Camera& camera, const WorldTransform& world) {
            if (!camera.active)
                return;
            const audio::Listener previous = audio->get_listener();
            audio::Listener listener;
            listener.position = world.value.position.glm();
            listener.forward = world.value.forward();
            listener.up = world.value.up();
            // for the doppler shift of the voices that use it
            if (time->dt > 0.0)
                listener.velocity = (listener.position - previous.position) / float(time->dt);
            audio->set_listener(listener);
        });
    }

    ECS_REGISTER()
}
