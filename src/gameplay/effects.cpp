module;

#include <json/value.h>

module gameplay;

import :effects;

import std.compat;
import reflect;
import rhobject;
import ecs;
import framework;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    // despawned (with its children) once its time is up
    [[=ecs::system<ecs::Phase::Update>]]
    void expire_lifetimes(ecs::Query<Lifetime> lifetimes, ecs::Res<ecs::FrameTime> time, ecs::EventWriter<DespawnRequest> despawn)
    {
        lifetimes.each([&] (ecs::Entity e, Lifetime& lifetime) {
            const bool expired = lifetime.age >= lifetime.seconds;
            lifetime.age += float(time->dt);
            if (!expired && lifetime.age >= lifetime.seconds)
                despawn.send({ e });
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Lifetime)
}
