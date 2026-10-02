module;

#include <json/value.h>

module particles;

import std.compat;
import glm;
import rhmath;
import linear_color;
import reflect;
import rhobject;
import name;
import ecs;
import framework;
import assets;
import render_scene;
import rhcomponents;
import json_utils;
import paths;
import cvar;
import profile;
import log;

#include "logging/log_macro.h"
#include "profiling/profile.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogParticles, Log);

void serialize_json_value(ParticleRange& target, const Json::Value& value, const SerializationContext& context)
{
    if (value.isNumeric())
    {
        target.min = target.max = value.asFloat();
        return;
    }
    reflect::json::visit_serialize(value, target, context);
}

namespace
{
    // a frame this long or longer is simulated as one of max_step: no explosion after a hitch
    constexpr float max_step = 0.1f;
    constexpr float warmup_step = 1.0f / 30.0f;
    constexpr uint32_t max_particles_per_emitter = 16384;
    // s between looks at the effect files for changes
    constexpr double change_check_interval = 0.5;
    constexpr float two_pi = 2.0f * std::numbers::pi_v<float>;

    // ---- effects, shared by the systems playing them ----

    struct CachedEffect
    {
        // nullptr: failed to load (reported once)
        std::shared_ptr<const ParticleEffect> effect;
        std::filesystem::file_time_type written{};
    };
    std::map<std::string, CachedEffect> effect_cache;
    bool reload_requested = false;
    double next_change_check = 0.0;

    std::filesystem::file_time_type write_time(const std::string& path)
    {
        std::error_code error;
        const auto time = std::filesystem::last_write_time(paths::get_assets_path() / path, error);
        return error ? std::filesystem::file_time_type{} : time;
    }

    std::shared_ptr<const ParticleEffect> load_effect(const std::string& path)
    {
        std::error_code error;
        if (!std::filesystem::is_regular_file(paths::get_assets_path() / path, error))
        {
            LogParticles.Log<Error>("Particle effect '%s' not found", path.c_str());
            return nullptr;
        }
        const std::optional<Json::Value> json = json_utils::load_json_asset(path);
        if (!json || !json->isObject())
        {
            LogParticles.Log<Error>("Particle effect '%s' is not a JSON object", path.c_str());
            return nullptr;
        }

        auto effect = std::make_shared<ParticleEffect>();
        SerializationContext context;
        context.current_file = path;
        reflect::json::visit_serialize(*json, *effect, context);

        for (ParticleEmitter& emitter : effect->emitters)
        {
            emitter.max_particles = std::clamp(emitter.max_particles, 1u, max_particles_per_emitter);
            std::ranges::sort(emitter.spawn.bursts, {}, &ParticleBurst::time);
            std::ranges::sort(emitter.over_life.size, {}, &ParticleCurveKey::t);
            std::ranges::sort(emitter.over_life.color, {}, &ParticleColorKey::t);

            ParticleRender& render = emitter.render;
            render.columns = std::clamp(render.columns, 1u, 255u);
            render.rows = std::clamp(render.rows, 1u, 255u);
            // no texture: a soft round dot
            if (!render.texture.empty())
            {
                render.texture_handle = AssetManager::get().load_texture(render.texture);
                if (!render.texture_handle.is_valid())
                    LogParticles.Log<Error>("Particle effect '%s', emitter '%s': texture '%s' not found", path.c_str(),
                        emitter.name.c_str(), render.texture.c_str());
            }
        }
        LogParticles.Log("Particle effect '%s': %zu emitters", path.c_str(), effect->emitters.size());
        return effect;
    }

    std::shared_ptr<const ParticleEffect> get_effect(const std::string& path)
    {
        // a system without an effect yet
        if (path.empty())
            return nullptr;
        auto found = effect_cache.find(path);
        if (found == effect_cache.end())
            found = effect_cache.emplace(path, CachedEffect{ load_effect(path), write_time(path) }).first;
        return found->second.effect;
    }

    // Effects whose file changed (all of them when forced) are loaded again: the systems playing them see
    // another effect and restart. A file that no longer parses keeps the effect it had.
    void reload_changed_effects(bool force)
    {
        for (auto& [path, cached] : effect_cache)
        {
            const std::filesystem::file_time_type written = write_time(path);
            if (!force && written == cached.written)
                continue;
            cached.written = written;
            if (std::shared_ptr<const ParticleEffect> effect = load_effect(path))
                cached.effect = std::move(effect);
        }
    }

    cvar::Command cmd_reload("particles.reload", "Loads the particle effects again from their files and restarts the systems playing them",
        [] (cvar::Args) { reload_requested = true; });

    // ---- random numbers, noise, curves ----

    // xorshift32, [0, 1)
    float random01(uint32_t& state)
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return float(state >> 8) * (1.0f / 16777216.0f);
    }

    float pick(const ParticleRange& range, uint32_t& state)
    {
        return glm::mix(range.min, range.max, random01(state));
    }

    glm::vec3 random_unit_vector(uint32_t& state)
    {
        const float y = random01(state) * 2.0f - 1.0f;
        const float angle = random01(state) * two_pi;
        const float ring = std::sqrt(std::max(1.0f - y * y, 0.0f));
        return glm::vec3(std::cos(angle) * ring, y, std::sin(angle) * ring);
    }

    // [-1, 1]
    float lattice(int x, int y, int z)
    {
        uint32_t h = uint32_t(x) * 0x8da6b343u ^ uint32_t(y) * 0xd8163841u ^ uint32_t(z) * 0xcb1ab31fu;
        h ^= h >> 15;
        h *= 0x2c1b3c6du;
        h ^= h >> 12;
        return float(h & 0xffffffu) * (2.0f / 16777215.0f) - 1.0f;
    }

    // smooth value noise, [-1, 1]
    float noise(glm::vec3 p)
    {
        const glm::vec3 cell(std::floor(p.x), std::floor(p.y), std::floor(p.z));
        glm::vec3 f = p - cell;
        f = f * f * (3.0f - 2.0f * f);
        const int x = int(cell.x), y = int(cell.y), z = int(cell.z);
        const auto along_x = [&] (int dy, int dz) {
            return glm::mix(lattice(x, y + dy, z + dz), lattice(x + 1, y + dy, z + dz), f.x);
        };
        return glm::mix(glm::mix(along_x(0, 0), along_x(1, 0), f.y), glm::mix(along_x(0, 1), along_x(1, 1), f.y), f.z);
    }

    // a push in any direction, smooth in space
    glm::vec3 swirl(glm::vec3 p)
    {
        return glm::vec3(noise(p), noise(p + glm::vec3(31.4f, 17.3f, 5.1f)), noise(p + glm::vec3(-11.7f, 47.2f, 23.9f)));
    }

    float evaluate(const std::vector<ParticleCurveKey>& keys, float t)
    {
        if (keys.empty())
            return 1.0f;
        if (t <= keys.front().t)
            return keys.front().value;
        for (size_t i = 1; i < keys.size(); ++i)
            if (t < keys[i].t)
                return glm::mix(keys[i - 1].value, keys[i].value, (t - keys[i - 1].t) / std::max(keys[i].t - keys[i - 1].t, 1e-6f));
        return keys.back().value;
    }

    glm::vec4 to_vec4(const LinearColor& color)
    {
        return glm::vec4(color.r, color.g, color.b, color.a);
    }

    glm::vec4 evaluate(const std::vector<ParticleColorKey>& keys, float t)
    {
        if (keys.empty())
            return glm::vec4(1.0f);
        if (t <= keys.front().t)
            return to_vec4(keys.front().color);
        for (size_t i = 1; i < keys.size(); ++i)
            if (t < keys[i].t)
                return glm::mix(to_vec4(keys[i - 1].color), to_vec4(keys[i].color),
                    (t - keys[i - 1].t) / std::max(keys[i].t - keys[i - 1].t, 1e-6f));
        return to_vec4(keys.back().color);
    }

    // ---- simulation ----

    // The entity of the system this frame
    struct EmitterFrame
    {
        glm::mat4 to_world{ 1.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
        float scale = 1.0f;         // sizes and speeds of the effect are scaled by it
        double time = 0.0;
        glm::vec3 shape_scale{ 1.0f };  // ParticleSystem::shape_scale
    };

    // What moves the particles of an emitter during a step, in the space they are simulated in
    struct StepForces
    {
        glm::vec3 acceleration;
        glm::vec3 wind;
        float drag;                 // share of the difference to the wind taken per step
        float turbulence;
        float turbulence_frequency;
        glm::vec3 turbulence_offset;
    };

    StepForces step_forces(const ParticleEmitter& emitter, const EmitterFrame& frame, float dt)
    {
        const ParticleForces& forces = emitter.forces;
        // the forces are given in the world
        const bool local = emitter.space == ParticleSpace::local;
        const glm::quat to_simulation = local ? glm::inverse(frame.rotation) : glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const float scale = local ? 1.0f / frame.scale : 1.0f;

        StepForces result;
        result.acceleration = to_simulation * forces.acceleration.glm() * scale;
        result.wind = to_simulation * forces.wind.glm() * scale;
        result.drag = 1.0f - std::exp(-std::max(forces.drag, 0.0f) * dt);
        result.turbulence = forces.turbulence * scale;
        result.turbulence_frequency = 1.0f / (std::max(forces.turbulence_size, 0.01f) * scale);
        // the swirls rise through the particles (and drift sideways a little: they never repeat in place)
        result.turbulence_offset = glm::vec3(0.31f, -1.0f, 0.17f) * float(frame.time * forces.turbulence_speed)
            * result.turbulence_frequency * scale;
        return result;
    }

    void advance(Particle& particle, const StepForces& forces, float dt)
    {
        glm::vec3 acceleration = forces.acceleration;
        if (forces.turbulence != 0.0f)
            acceleration += swirl(particle.position * forces.turbulence_frequency + forces.turbulence_offset) * forces.turbulence;
        particle.velocity += acceleration * dt;
        particle.velocity += (forces.wind - particle.velocity) * forces.drag;
        particle.position += particle.velocity * dt;
        particle.rotation += particle.spin * dt;
    }

    glm::vec3 point_in_shape(const ParticleShape& shape, uint32_t& random)
    {
        glm::vec3 point(0.0f);
        switch (shape.type)
        {
        case ParticleShapeType::point:
            break;
        case ParticleShapeType::sphere:
            point = random_unit_vector(random) * (shape.radius * std::cbrt(random01(random)));
            break;
        case ParticleShapeType::box:
            point = shape.extent.glm() * glm::vec3(random01(random) * 2.0f - 1.0f, random01(random) * 2.0f - 1.0f,
                random01(random) * 2.0f - 1.0f);
            break;
        case ParticleShapeType::disc:
        {
            const float angle = random01(random) * two_pi;
            const float distance = shape.radius * std::sqrt(random01(random));
            point = glm::vec3(std::cos(angle) * distance, 0.0f, std::sin(angle) * distance);
            break;
        }
        }
        return shape.offset.glm() + point;
    }

    // A direction within `spread` degrees of `axis`, uniform over that cap of the sphere
    glm::vec3 direction_in_cone(glm::vec3 axis, float spread, uint32_t& random)
    {
        const float length = glm::length(axis);
        axis = length > 1e-6f ? axis / length : glm::vec3(0.0f, 1.0f, 0.0f);
        const float cos_limit = std::cos(glm::radians(std::clamp(spread, 0.0f, 180.0f)));
        const float cos_angle = glm::mix(1.0f, cos_limit, random01(random));
        const float sin_angle = std::sqrt(std::max(1.0f - cos_angle * cos_angle, 0.0f));
        const float turn = random01(random) * two_pi;

        const glm::vec3 side = glm::normalize(glm::cross(axis, std::abs(axis.y) < 0.99f ? glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f)));
        const glm::vec3 other = glm::cross(axis, side);
        return axis * cos_angle + (side * std::cos(turn) + other * std::sin(turn)) * sin_angle;
    }

    uint32_t frame_count(const ParticleRender& render)
    {
        return render.columns * render.rows;
    }

    Particle spawn_particle(const ParticleEmitter& emitter, const EmitterFrame& frame, uint32_t& random)
    {
        const ParticleStart& start = emitter.start;

        Particle particle;
        particle.position = point_in_shape(emitter.shape, random) * frame.shape_scale;
        particle.velocity = direction_in_cone(start.direction.glm(), start.spread, random) * pick(start.speed, random);
        particle.lifetime = std::max(pick(start.lifetime, random), 0.01f);
        particle.size = pick(start.size, random);
        particle.rotation = glm::radians(pick(start.rotation, random));
        particle.spin = glm::radians(pick(start.spin, random));
        particle.mirrored = emitter.render.mirror && random01(random) < 0.5f;

        const float frames = float(frame_count(emitter.render));
        switch (emitter.render.animation)
        {
        case ParticleAnimation::over_life: particle.frame = 0.0f; break;
        case ParticleAnimation::loop: particle.frame = random01(random) * frames; break;
        case ParticleAnimation::random: particle.frame = std::floor(random01(random) * frames); break;
        }

        // world space: the particle leaves the entity here
        if (emitter.space == ParticleSpace::world)
        {
            particle.position = glm::vec3(frame.to_world * glm::vec4(particle.position, 1.0f));
            particle.velocity = frame.rotation * particle.velocity * frame.scale;
            particle.size *= frame.scale;
        }
        return particle;
    }

    void step_emitter(const ParticleEmitter& emitter, ParticleEmitterState& state, const EmitterFrame& frame, float dt, bool emitting)
    {
        const StepForces forces = step_forces(emitter, frame, dt);

        std::vector<Particle>& particles = state.particles;
        for (size_t i = 0; i < particles.size();)
        {
            Particle& particle = particles[i];
            particle.age += dt;
            if (particle.age >= particle.lifetime)
            {
                particle = particles.back();
                particles.pop_back();
                continue;
            }
            advance(particle, forces, dt);
            ++i;
        }

        const ParticleSpawn& spawn = emitter.spawn;
        const bool ended = spawn.duration > 0.0f && !spawn.loop && state.time >= spawn.duration;
        state.time += dt;
        if (emitting && !ended)
        {
            uint32_t count = 0;
            while (state.next_burst < spawn.bursts.size() && spawn.bursts[state.next_burst].time <= state.time)
                count += spawn.bursts[state.next_burst++].count;

            state.spawn_debt += std::max(spawn.rate, 0.0f) * dt;
            const uint32_t owed = uint32_t(state.spawn_debt);
            state.spawn_debt -= float(owed);
            count += owed;

            for (uint32_t i = 0; i < count && particles.size() < emitter.max_particles; ++i)
            {
                Particle particle = spawn_particle(emitter, frame, state.random);
                // born at some moment of the step, not all at its end: no pulsing at low frame rates
                particle.age = random01(state.random) * dt;
                advance(particle, forces, particle.age);
                particles.push_back(particle);
            }
        }
        else
            state.spawn_debt = 0.0f;

        if (spawn.duration > 0.0f && spawn.loop && state.time >= spawn.duration)
        {
            state.time = std::fmod(state.time, spawn.duration);
            state.next_burst = 0;
        }
    }

    void append_sprites(const ParticleEmitter& emitter, const ParticleEmitterState& state, const EmitterFrame& frame,
        std::vector<GPUParticle>& sprites)
    {
        const ParticleRender& render = emitter.render;
        const bool local = emitter.space == ParticleSpace::local;
        const uint32_t frames = frame_count(render);

        GPUParticle sprite{};
        sprite.stretch = render.facing == ParticleFacing::velocity ? std::max(render.stretch, 0.0f) : 0.0f;
        sprite.soft = std::max(render.soft, 0.0f);
        sprite.additive = render.blend == ParticleBlend::additive ? 1.0f : 0.0f;
        // slot 0 of the texture array is "no texture": the shader draws a soft round dot
        sprite.texture = render.texture_handle.is_valid() ? render.texture_handle.id : 0;
        sprite.grid = render.columns | render.rows << 16;
        sprite.aspect = std::max(render.aspect, 0.01f);
        uint32_t flags = 0;
        if (render.lit)
            flags |= gpu_particle_lit;
        if (render.facing == ParticleFacing::velocity)
            flags |= gpu_particle_velocity_facing;
        if (render.smooth && render.animation != ParticleAnimation::random && frames > 1)
            flags |= gpu_particle_smooth;
        if (render.animation == ParticleAnimation::loop)
            flags |= gpu_particle_loop;

        for (const Particle& particle : state.particles)
        {
            const float t = std::clamp(particle.age / particle.lifetime, 0.0f, 1.0f);
            const glm::vec4 color = evaluate(emitter.over_life.color, t);
            if (color.a <= 0.0f)
                continue;

            sprite.position = local ? glm::vec3(frame.to_world * glm::vec4(particle.position, 1.0f)) : particle.position;
            sprite.velocity = local ? frame.rotation * particle.velocity * frame.scale : particle.velocity;
            sprite.size = particle.size * evaluate(emitter.over_life.size, t) * (local ? frame.scale : 1.0f);
            sprite.color = glm::vec4(glm::vec3(color) * render.intensity, color.a);
            sprite.rotation = particle.rotation;
            sprite.flags = particle.mirrored ? flags | gpu_particle_mirrored : flags;
            switch (render.animation)
            {
            case ParticleAnimation::over_life: sprite.frame = t * float(frames - 1); break;
            case ParticleAnimation::loop: sprite.frame = std::fmod(particle.frame + particle.age * render.fps, float(frames)); break;
            case ParticleAnimation::random: sprite.frame = particle.frame; break;
            }
            sprites.push_back(sprite);
        }
    }

    // Emitter states for the effect the system plays now, warmed up
    void start_system(ParticleSystem& system, ecs::Entity e, const EmitterFrame& frame, SceneViewProcessor_Particles& processor)
    {
        system.emitters.clear();
        if (!system.loaded)
            return;

        system.emitters.resize(system.loaded->emitters.size());
        for (size_t i = 0; i < system.emitters.size(); ++i)
        {
            const ParticleEmitter& emitter = system.loaded->emitters[i];
            ParticleEmitterState& state = system.emitters[i];
            // every system and emitter its own sequence
            state.random = uint32_t(e.bits() * 0x9e3779b9u) ^ uint32_t((i + 1) * 0x85ebca6bu);
            if (state.random == 0)
                state.random = 1;
            state.particles.reserve(std::min(emitter.max_particles, 1024u));

            const TextureHandle texture = emitter.render.texture_handle;
            if (texture.is_valid() && std::ranges::find(processor.textures, texture) == processor.textures.end())
                processor.textures.push_back(texture);

            EmitterFrame warmup_frame = frame;
            const float warmup = std::clamp(emitter.spawn.warmup, 0.0f, 60.0f);
            for (float time = 0.0f; time < warmup; time += warmup_step)
            {
                warmup_frame.time = frame.time - double(warmup - time);
                step_emitter(emitter, state, warmup_frame, warmup_step, system.emitting);
            }
        }
    }

    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<ParticleSimulation>, =ecs::after<scene::TransformPropagation>,
      =ecs::before<RenderSync>, =ecs::ambiguous_with<MeshColliderSync>]]
    void simulate_particles(ecs::Query<ParticleSystem, const WorldTransform> systems, ecs::Res<ecs::FrameTime> time,
        ecs::ResMut<SceneView> scene_view)
    {
        PROFILE("simulate_particles");

        if (reload_requested || time->time >= next_change_check)
        {
            reload_changed_effects(reload_requested);
            reload_requested = false;
            next_change_check = time->time + change_check_interval;
        }

        SceneViewProcessor_Particles& processor = scene_view->get_processor<SceneViewProcessor_Particles>();
        processor.particles.clear();

        systems.each([&] (ecs::Entity e, ParticleSystem& system, const WorldTransform& world) {
            const glm::vec3 scale = world.value.scale.glm();
            const EmitterFrame frame{
                .to_world = world.value.matrix(),
                .rotation = world.value.rotation.glm(),
                .scale = std::max((std::abs(scale.x) + std::abs(scale.y) + std::abs(scale.z)) / 3.0f, 1e-4f),
                .time = time->time,
                .shape_scale = system.shape_scale.glm(),
            };

            const std::shared_ptr<const ParticleEffect> effect = get_effect(system.effect);
            if (system.loaded != effect)
            {
                system.loaded = effect;
                start_system(system, e, frame, processor);
            }
            if (!system.loaded)
                return;

            const float dt = std::min(float(time->dt) * std::max(system.time_scale, 0.0f), max_step);
            system.alive = 0;
            for (size_t i = 0; i < system.emitters.size(); ++i)
            {
                const ParticleEmitter& emitter = system.loaded->emitters[i];
                ParticleEmitterState& state = system.emitters[i];
                if (dt > 0.0f)
                    step_emitter(emitter, state, frame, dt, system.emitting);
                append_sprites(emitter, state, frame, processor.particles);
                system.alive += uint32_t(state.particles.size());
            }
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(ParticleSystem)
}
