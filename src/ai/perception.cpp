module;

#include <json/value.h>

module ai;

import :perception;
import :tokens;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;
import physics;
import cvar;
import debug_draw;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    using namespace ai;

    cvar::Var<float> cv_sight_interval("ai.perception.sight_interval", 0.2f,
        "Seconds between two sight checks of a perceiver (a ray per hostile in range); spread over the ticks",
        { .has_range = true, .min = 0.02f, .max = 2.0f });
    cvar::Var<bool> cv_debug("ai.debug.perception", false,
        "Draws what the agents perceive: field of view, lines to what they know (yellow suspected .. red confirmed, "
        "solid while seen), crosses at the last known positions");

    constexpr glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    phys::QueryFilter sight_filter()
    {
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world | phys::Category::voxel;
        filter.hit_back_faces = true;   // open shells (cliffs) block the view from behind too
        return filter;
    }

    float horizontal_distance(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
    }

    KnownTarget& remember(Perception& p, ecs::Entity e)
    {
        if (KnownTarget* k = p.find(e))
            return *k;
        KnownTarget& k = p.known.emplace_back();
        k.entity = e;
        return k;
    }

    // what has been sensed this tick, about one target
    void sensed(KnownTarget& k, const glm::vec3& position, const glm::vec3& velocity, Sense sense, float awareness_gain, double now)
    {
        k.last_position = position;
        k.last_velocity = velocity;
        k.sense = sense;
        k.last_sensed = now;
        k.awareness = std::min(1.0f, k.awareness + awareness_gain);
        if (k.awareness >= 1.0f)
            k.confirmed = true;
    }

    // awareness per s while it sees the other: at once next to it, faster close by, in the middle of the view and
    // when the other runs
    float sight_rate(const Perception& p, float distance, float cos_angle, float speed, float visibility)
    {
        if (distance <= p.near_range)
            return 4.0f * p.awareness_rise;
        const float t = std::clamp((distance - p.near_range) / std::max(p.sight_range - p.near_range, 1e-3f), 0.0f, 1.0f);
        float rate = p.awareness_rise * glm::mix(2.0f, 0.3f, t);
        rate *= cos_angle > std::cos(glm::radians(30.0f)) ? 1.5f : 0.7f;
        if (speed > 3.0f)
            rate *= 1.5f;
        return rate * std::max(visibility, 0.0f);
    }

    // ---------------------------------------------------------------- resources

    [[=ecs::on_add]]
    void create_ai_resources(ecs::Registry& registry, ecs::Entity, Perception&)
    {
        if (!registry.find_resource<AttackTokens>())
            registry.set_resource<AttackTokens>();
    }

    // ---------------------------------------------------------------- measuring

    // where the perceivables are and how fast they go (per frame: the world transforms are computed in Late)
    [[=ecs::system<ecs::Phase::Late>, =ecs::after<scene::TransformPropagation>]]
    void measure_perceivables(ecs::Query<Perceivable, const WorldTransform> perceivables, ecs::Res<ecs::FrameTime> time)
    {
        const float dt = (float)time->dt;
        perceivables.each([&] (Perceivable& p, const WorldTransform& world) {
            const glm::vec3 position = world.value.position.glm();
            if (p.measured && dt > 1e-4f)
            {
                const glm::vec3 velocity = (position - p.position) / dt;
                // teleports and hitches: not a velocity
                if (glm::length(velocity) < 30.0f)
                    p.velocity = glm::mix(p.velocity, velocity, 1.0f - std::exp(-12.0f * dt));
            }
            p.position = position;
            p.measured = true;
        });
    }

    // ---------------------------------------------------------------- senses

    struct Other
    {
        ecs::Entity entity;
        const Perceivable* perceivable;
    };

    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<PerceptionUpdate>]]
    void update_perception(ecs::Query<Perception, const WorldTransform> perceivers, ecs::Query<const Perceivable> perceivables,
        ecs::EventReader<Stimulus> stimuli, ecs::Res<phys::PhysicsScene> physics, ecs::OptResMut<AttackTokens> tokens,
        ecs::Res<ecs::SimTime> time)
    {
        const double now = time->time;
        const float dt = (float)time->dt;
        if (tokens)
            tokens->expire(now);

        std::vector<Other> others;
        perceivables.each([&] (ecs::Entity e, const Perceivable& p) {
            if (p.enabled && p.measured && p.team != Team::neutral)
                others.push_back({ e, &p });
        });
        auto team_of = [&] (ecs::Entity e) {
            const Perceivable* p = perceivables.get(e);
            return p ? p->team : Team::neutral;
        };

        const phys::QueryFilter filter = sight_filter();
        const float interval = std::max(cv_sight_interval.get(), 0.02f);

        perceivers.each([&] (ecs::Entity self, Perception& p, const WorldTransform& world) {
            const Perceivable* own = perceivables.get(self);
            const Team team = own ? own->team : Team::neutral;
            const bool active = !own || own->enabled;   // the dead perceive nothing

            const glm::vec3 feet = world.value.position.glm();
            p.eyes = feet + up * p.eye_height;
            glm::vec3 forward = world.value.rotation.glm() * glm::vec3(0.0f, 0.0f, 1.0f);
            forward.y = 0.0f;
            p.forward = glm::length(forward) > 1e-3f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);

            for (KnownTarget& k : p.known)
                k.distance = horizontal_distance(feet, k.last_position);
            if (!active)
            {
                p.known.clear();
                return;
            }

            // ---- sight: every interval, the first look spread over an interval by the entity
            if (p.next_look < 0.0)
                p.next_look = now + interval * double(self.index % 16) / 16.0;
            const bool look = now >= p.next_look;
            if (look)
            {
                p.next_look = now + interval;
                for (KnownTarget& k : p.known)
                    k.visible = false;
            }
            const float cos_half_fov = std::cos(glm::radians(std::clamp(p.field_of_view, 0.0f, 360.0f) * 0.5f));

            for (const Other& other : others)
            {
                if (other.entity == self || !hostile(team, other.perceivable->team))
                    continue;
                const Perceivable& o = *other.perceivable;
                const float distance = horizontal_distance(feet, o.position);
                const float speed = glm::length(glm::vec2(o.velocity.x, o.velocity.z));

                // footsteps: the faster it goes, the farther they are heard (standing still: silent)
                const float steps_radius = speed * o.footsteps * p.hearing;
                if (speed > 1.0f && distance < steps_radius)
                    sensed(remember(p, other.entity), o.position, o.velocity, Sense::hearing, (1.0f - distance / steps_radius) * dt, now);

                KnownTarget* known = p.find(other.entity);
                if (known && known->visible)
                {
                    // seen at the last look: follows it between the looks
                    sensed(*known, o.position, o.velocity, Sense::sight,
                        sight_rate(p, distance, 1.0f, speed, o.visibility) * dt, now);
                    continue;
                }
                if (!look || distance > p.sight_range)
                    continue;

                const glm::vec3 center = o.position + up * o.center_height;
                glm::vec3 to = center - p.eyes;
                glm::vec3 flat(to.x, 0.0f, to.z);
                const float flat_length = glm::length(flat);
                const float cos_angle = flat_length > 1e-3f ? glm::dot(flat / flat_length, p.forward) : 1.0f;
                if (distance > p.near_range && cos_angle < cos_half_fov)
                    continue;
                const float length = glm::length(to);
                if (length > 1e-3f)
                {
                    if (std::optional<phys::Hit> hit = physics->raycast(p.eyes, to / length, length, filter); hit && hit->distance < length - 0.3f)
                        continue;   // a wall in between
                }
                KnownTarget& k = remember(p, other.entity);
                k.visible = true;
                sensed(k, o.position, o.velocity, Sense::sight, sight_rate(p, distance, cos_angle, speed, o.visibility) * dt, now);
            }

            // ---- fading: what was not sensed this tick
            for (KnownTarget& k : p.known)
                if (k.last_sensed < now)
                    k.awareness -= p.awareness_decay * dt;
            std::erase_if(p.known, [] (const KnownTarget& k) { return k.awareness <= 0.0f; });
        });

        // ---- stimuli: noises, blows, shared knowledge
        for (const Stimulus& s : stimuli.read())
        {
            const Team source_team = team_of(s.source);
            const Perceivable* source = s.source ? perceivables.get(s.source) : nullptr;
            const glm::vec3 velocity = source ? source->velocity : glm::vec3(0.0f);
            perceivers.each([&] (ecs::Entity self, Perception& p, const WorldTransform&) {
                if (self == s.source)
                    return;
                const Perceivable* own = perceivables.get(self);
                if (own && !own->enabled)
                    return;
                const Team team = own ? own->team : Team::neutral;
                switch (s.kind)
                {
                case Stimulus::Kind::noise:
                {
                    const float radius = s.radius * p.hearing;
                    const float distance = glm::length(p.eyes - s.origin);
                    if (!s.source || radius <= 0.0f || distance > radius || !hostile(team, source_team))
                        return;
                    sensed(remember(p, s.source), s.position, velocity, Sense::hearing, s.awareness * (1.0f - distance / radius), now);
                    break;
                }
                case Stimulus::Kind::damage:
                    if (self != s.receiver || !s.source)
                        return;
                    sensed(remember(p, s.source), s.position, velocity, Sense::damage, 1.0f, now);
                    break;
                case Stimulus::Kind::shared:
                {
                    if (team != s.team || !s.source || horizontal_distance(p.eyes, s.origin) > s.radius)
                        return;
                    KnownTarget& k = remember(p, s.source);
                    const float gain = std::max(s.awareness - k.awareness, 0.0f);
                    sensed(k, s.position, velocity, Sense::shared, gain, now);
                    break;
                }
                }
            });
        }
    }

    // ---------------------------------------------------------------- debug

    [[=ecs::system<ecs::Phase::Late>, =ecs::after<scene::TransformPropagation>, =ecs::before<RenderSync>]]
    void draw_perception(ecs::Query<const Perception> perceivers, ecs::Query<const Perceivable> perceivables)
    {
        if (!cv_debug.get())
            return;
        std::vector<debug_draw::Line> lines;
        perceivers.each([&] (const Perception& p) {
            // the field of view: its edges and an arc 3 m out
            const float half = glm::radians(std::clamp(p.field_of_view, 0.0f, 360.0f) * 0.5f);
            const glm::vec4 fov_color{ 0.4f, 0.8f, 1.0f, 0.6f };
            auto direction = [&] (float angle) {
                return glm::angleAxis(angle, up) * p.forward;
            };
            const float arc = 3.0f;
            constexpr int segments = 12;
            glm::vec3 previous = p.eyes + direction(-half) * arc;
            lines.push_back({ p.eyes, previous, fov_color });
            for (int i = 1; i <= segments; ++i)
            {
                const glm::vec3 point = p.eyes + direction(-half + 2.0f * half * float(i) / segments) * arc;
                lines.push_back({ previous, point, fov_color });
                previous = point;
            }
            lines.push_back({ p.eyes, previous, fov_color });

            for (const KnownTarget& k : p.known)
            {
                const float a = k.confirmed ? 1.0f : k.awareness;
                const glm::vec4 color = glm::mix(glm::vec4(1.0f, 1.0f, 0.2f, 0.8f), glm::vec4(1.0f, 0.15f, 0.1f, 1.0f), a);
                glm::vec3 at = k.last_position + up * 0.5f;
                if (const Perceivable* o = perceivables.get(k.entity); o && k.visible)
                    at = o->position + up * o->center_height;
                if (k.visible)
                    lines.push_back({ p.eyes, at, color });
                else
                {
                    // dashed to where it was last sensed
                    const glm::vec3 d = at - p.eyes;
                    const int dashes = std::max(1, int(glm::length(d) / 0.5f));
                    for (int i = 0; i < dashes; i += 2)
                        lines.push_back({ p.eyes + d * (float(i) / dashes), p.eyes + d * (float(i + 1) / dashes), color });
                }
                const float s = 0.25f;
                lines.push_back({ k.last_position + glm::vec3(-s, 0.05f, 0.0f), k.last_position + glm::vec3(s, 0.05f, 0.0f), color });
                lines.push_back({ k.last_position + glm::vec3(0.0f, 0.05f, -s), k.last_position + glm::vec3(0.0f, 0.05f, s), color });
            }
        });
        debug_draw::lines(lines);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Perceivable, Perception)
}
