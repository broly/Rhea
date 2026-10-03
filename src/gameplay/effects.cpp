module;

#include <json/value.h>

module gameplay;

import :effects;
import :health;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    // the g-buffer keeps 4 levels of the body JPEG (shaders/character/shading_models.glsl)
    int jpeg_step(float level)
    {
        return int(std::clamp(level, 0.0f, 1.0f) * 3.0f + 0.5f);
    }

    float random_range(std::minstd_rand& rng, float a, float b)
    {
        return std::uniform_real_distribution<float>(std::min(a, b), std::max(a, b))(rng);
    }

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

    // The pulses of the infected and their crushed bodies. The MeshRenderers are written only when the step changes
    // (a render proxy update) or while a HitFlash rewrites effect.a every frame (then the stronger level wins).
    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<JpegInfections>, =ecs::after<HitFlashes>, =ecs::before<RenderSync>,
        =ecs::before<MeshColliderSync>]]
    void pulse_jpeg_infections(ecs::Query<JpegInfection> infections, ecs::Query<const Dead> dead, ecs::Query<const HitFlash> flashes,
        ecs::Query<MeshRenderer> renderers, ecs::Query<const ChildOf> parents, ecs::Res<ecs::FrameTime> time,
        ecs::EventWriter<JpegBodyEvent> bodies)
    {
        if (infections.empty())
            return;
        const float dt = std::clamp(float(time->dt), 0.0f, 0.25f);
        std::unordered_multimap<uint64_t, ecs::Entity> children;
        bool children_ready = false;
        std::vector<std::string> presets;

        infections.each([&] (ecs::Entity e, JpegInfection& infection) {
            if (!infection.initialized)
            {
                infection.initialized = true;
                infection.rng.seed(uint32_t(e.bits() ^ (e.bits() >> 32)) | 1u);
                // a pack spawned together does not pulse in step
                infection.next_pulse = random_range(infection.rng, 0.0f, infection.interval_max);
            }

            if (const Dead* death = dead.get(e))
                infection.level = infection.dead_strength * std::clamp(death->time / std::max(infection.dead_fade_in, 1e-3f), 0.0f, 1.0f);
            else
            {
                if (infection.pulse_age < 0.0f)
                {
                    infection.next_pulse -= dt;
                    if (infection.next_pulse <= 0.0f)
                        infection.pulse_age = 0.0f;
                }
                else if ((infection.pulse_age += dt) >= infection.pulse_time)
                {
                    infection.pulse_age = -1.0f;
                    infection.next_pulse = random_range(infection.rng, infection.interval_min, infection.interval_max);
                }
                infection.level = infection.pulse_age >= 0.0f
                    ? infection.pulse_strength * std::sin(std::numbers::pi_v<float> * infection.pulse_age / std::max(infection.pulse_time, 1e-3f))
                    : 0.0f;
            }

            const int step = jpeg_step(infection.level);
            if (step > 0 && std::ranges::find(presets, infection.preset) == presets.end())
                presets.push_back(infection.preset);
            const bool flashing = flashes.get(e) != nullptr;
            if (step == infection.applied_step && !flashing)
                return;
            infection.applied_step = step;

            if (!children_ready)
            {
                parents.each([&] (ecs::Entity child, const ChildOf& child_of) { children.emplace(child_of.parent.bits(), child); });
                children_ready = true;
            }
            const float value = float(step) / 3.0f;
            std::vector<ecs::Entity> stack{ e };
            for (int guard = 0; !stack.empty() && guard < 4096; ++guard)
            {
                const ecs::Entity node = stack.back();
                stack.pop_back();
                if (MeshRenderer* renderer = renderers.get(node))
                    renderer->effect.a = flashing ? std::max(renderer->effect.a, value) : value;
                auto [first, last] = children.equal_range(node.bits());
                for (auto it = first; it != last; ++it)
                    stack.push_back(it->second);
            }
        });

        // below a blow's body JPEG (strength 1) while that one is fresh
        for (std::string& preset : presets)
            bodies.send({ .preset = std::move(preset), .lifetime = 0.25f, .strength = 0.5f });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Lifetime, JpegInfection)
}
