module;

#include <json/value.h>

module gameplay;

import :jackal;
import :health;
import :player;
import :npc;
import :effects;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;
import physics;
import navigation;
import ai;
import cvar;
import debug_draw;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

// The jackal's brain and the dens of the packs (AI9); see gameplay:jackal for the whole picture.

// What the actions work with: the jackal, what it knows about its target, its home and pack, the world services
struct JackalContext
{
    ecs::Entity self;
    Jackal& body;
    JackalBrain& brain;
    ai::Perception& perception;
    ai::BrainDebug& debug;
    nav::NavAgent* agent = nullptr;
    const nav::NavMesh* nav = nullptr;
    ai::AttackTokens* tokens = nullptr;
    const ecs::EventWriter<DamageEvent>& damage;
    const ecs::EventWriter<ai::Stimulus>& stimuli;
    const ecs::EventWriter<JpegSpotEvent>& spots;
    const ecs::EventWriter<SpawnPrefabRequest>& spawns;
    double now = 0.0;
    float health = 1.0f;                       // share of the max

    // home: the den of its pack, else where it appeared
    glm::vec3 home{ 0.0f };
    float territory = 10.0f;
    float leash = 60.0f;
    bool pack_broken = false;
    std::optional<glm::vec3> leader;           // the pack's leader, when it is another jackal

    // the brain's target, when it knows of it
    const ai::KnownTarget* known = nullptr;
    glm::vec3 target_position{ 0.0f };         // live while seen, else where it was last sensed
    glm::vec3 target_velocity{ 0.0f };
    glm::vec3 target_forward{ 0.0f, 0.0f, 1.0f };
    float target_distance = std::numeric_limits<float>::infinity();
    bool target_is_player = false;

    float random() { return std::uniform_real_distribution<float>(0.0f, 1.0f)(brain.rng); }
    float random(float a, float b) { return glm::mix(a, b, random()); }
};

namespace
{
    cvar::Var<bool> cv_ai("game.jackal.ai", true, "Jackal brains (off: they stand where they are)");
    cvar::Var<int> cv_attackers("game.jackal.attackers", 2, "Jackals attacking the player at once (its attack tokens); any other target: 1",
        { .has_range = true, .min = 1.0f, .max = 8.0f });
    cvar::Var<bool> cv_debug("game.jackal.debug", false,
        "Draws the jackals' fights: ring slots around the targets (cyan: taken), the way to the slot, the leaps (red)");

    constexpr glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    constexpr float pi = std::numbers::pi_v<float>;

    float horizontal_distance(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
    }

    glm::vec3 forward_of(float heading)
    {
        return { std::sin(heading), 0.0f, std::cos(heading) };
    }

    glm::vec3 flat(const glm::vec3& v)
    {
        return { v.x, 0.0f, v.z };
    }

    const char* mode_name(JackalMode mode)
    {
        switch (mode)
        {
        case JackalMode::idle: return "idle";
        case JackalMode::alert: return "alert";
        case JackalMode::combat: return "combat";
        case JackalMode::flee: return "flee";
        case JackalMode::return_home: return "return";
        case JackalMode::dead: return "dead";
        }
        return "?";
    }

    const char* sense_verb(ai::Sense sense)
    {
        switch (sense)
        {
        case ai::Sense::sight: return "saw";
        case ai::Sense::hearing: return "heard";
        case ai::Sense::damage: return "hit by";
        case ai::Sense::shared: return "told of";
        }
        return "knows";
    }

    std::string target_name(const JackalContext& c)
    {
        if (!c.brain.target)
            return "nothing";
        return c.target_is_player ? std::string("the player") : std::format("#{}", c.brain.target.index);
    }

    void set_mode(JackalContext& c, JackalMode mode, std::string_view reason)
    {
        if (c.brain.mode == mode)
            return;
        c.brain.mode = mode;
        c.brain.mode_since = c.now;
        c.debug.log.add(c.now, std::format("{}: {}", mode_name(mode), reason));
    }

    // the mesh is walkable in a straight line between the two (or one of them is off the mesh: can't tell)
    bool open_ground(const nav::NavMesh* nav, const glm::vec3& from, const glm::vec3& to)
    {
        if (!nav)
            return true;
        glm::vec3 hit(std::numeric_limits<float>::quiet_NaN());
        return nav->raycast(from, to, &hit) || std::isnan(hit.x);
    }
}

// ---------------------------------------------------------------- actions

ai::ActionStatus JackalRest::tick(JackalContext& c, float dt)
{
    time_left -= dt;
    c.body.move.face = face;
    return time_left > 0.0f ? ai::ActionStatus::running : ai::ActionStatus::succeeded;
}

ai::ActionStatus JackalGoTo::tick(JackalContext& c, float dt)
{
    time_left -= dt;
    if (horizontal_distance(c.body.position, destination) <= arrive_distance + 0.1f)
        return ai::ActionStatus::succeeded;
    if (time_left <= 0.0f || (c.agent && c.agent->status == nav::NavStatus::unreachable))
        return ai::ActionStatus::failed;
    // the closest it gets
    if (c.agent && c.agent->status == nav::NavStatus::partial && c.agent->distance_to_goal < arrive_distance + 0.2f)
        return ai::ActionStatus::succeeded;
    JackalMove& m = c.body.move;
    m.destination = destination;
    m.speed = speed;
    m.arrive_distance = arrive_distance;
    return ai::ActionStatus::running;
}

void JackalHowl::start(JackalContext& c)
{
    if (!c.known)
        return;
    c.stimuli.send({
        .kind = ai::Stimulus::Kind::shared,
        .source = c.brain.target,
        .position = c.target_position,
        .origin = c.body.position,
        .radius = c.brain.howl_radius,
        .awareness = 1.0f,
        .team = ai::Team::jackals,
    });
    c.debug.log.add(c.now, std::format("howls: {} is here", target_name(c)));
}

ai::ActionStatus JackalHowl::tick(JackalContext& c, float dt)
{
    time_left -= dt;
    if (c.known)
        c.body.move.face = c.target_position;
    return time_left > 0.0f ? ai::ActionStatus::running : ai::ActionStatus::succeeded;
}

ai::ActionStatus JackalCircle::tick(JackalContext& c, float)
{
    if (!c.known)
        return ai::ActionStatus::failed;
    const JackalBrain& b = c.brain;
    JackalMove& m = c.body.move;
    const glm::vec3 position = c.body.position;
    m.face = c.target_position;

    if (b.slot && c.known->visible && c.target_distance < b.ring_radius + 6.0f)
    {
        // on the ring: to its slot, slower the closer
        const float d = horizontal_distance(position, *b.slot);
        m.destination = *b.slot;
        m.arrive_distance = 0.4f;
        m.speed = d > 6.0f ? b.run_speed : d > 1.5f ? b.trot_speed : b.walk_speed;
        return ai::ActionStatus::running;
    }

    // catching up: for the target, stopping on the ring (where it was last sensed when it is out of sight)
    m.destination = c.target_position;
    m.arrive_distance = c.known->visible ? b.ring_radius : 1.0f;
    m.speed = c.target_distance > b.ring_radius + 10.0f ? b.run_speed : b.trot_speed;
    const glm::vec3 to_target = flat(c.target_position - position);
    const float length = glm::length(to_target);
    const bool running_away = length > 1e-3f && glm::dot(flat(c.target_velocity), to_target / length) > 2.0f;
    if (running_away && c.target_distance > b.ring_radius + 3.0f && b.stamina_left > 0.0f)
        m.speed = b.sprint_speed;
    return ai::ActionStatus::running;
}

void JackalFeint::start(JackalContext& c)
{
    const glm::vec3 to = flat(c.target_position - c.body.position);
    const float length = glm::length(to);
    const glm::vec3 direction = length > 1e-3f ? to / length : forward_of(c.body.heading);
    point = c.body.position + direction * std::clamp(length - 2.5f, 0.5f, 2.0f);
}

ai::ActionStatus JackalFeint::tick(JackalContext& c, float dt)
{
    time += dt;
    JackalMove& m = c.body.move;
    if (c.known)
        m.face = c.target_position;
    if (time < 0.45f)
    {
        // the dash
        m.destination = point;
        m.direct = true;
        m.speed = c.brain.run_speed;
        m.acceleration = 25.0f;
        m.deceleration = 25.0f;
        m.arrive_distance = 0.2f;
        return ai::ActionStatus::running;
    }
    // the stop: stands its ground a moment
    return time < 0.9f ? ai::ActionStatus::running : ai::ActionStatus::succeeded;
}

void JackalLunge::start(JackalContext& c)
{
    target = c.brain.target;
    // seen coming or not: from outside the target's view it stares longer (time to hear it, to turn)
    const glm::vec3 from_target = flat(c.body.position - c.target_position);
    const float length = glm::length(from_target);
    const bool in_view = length < 1e-3f || glm::dot(c.target_forward, from_target / length) > std::cos(glm::radians(60.0f));
    telegraph = in_view ? c.brain.telegraph : c.brain.telegraph_behind;
    c.debug.log.add(c.now, std::format("lunge at {} from {:.1f} m ({})", target_name(c), c.target_distance, in_view ? "in view" : "from behind"));
}

ai::ActionStatus JackalLunge::tick(JackalContext& c, float dt)
{
    time += dt;
    const JackalBrain& b = c.brain;
    JackalMove& m = c.body.move;
    const glm::vec3 position = c.body.position;
    const bool same_target = c.known && c.brain.target == target;

    switch (phase)
    {
    case Phase::telegraph:
    {
        if (!same_target)
            return ai::ActionStatus::failed;
        m.face = c.target_position;
        if (time < telegraph)
            return ai::ActionStatus::running;

        // where the target will be when it lands - half the lead, so a step aside makes it miss
        const float flight = c.target_distance / std::max(b.lunge_speed, 1.0f);
        const glm::vec3 predicted = c.target_position + flat(c.target_velocity) * std::min(flight, 0.6f) * 0.5f;
        const glm::vec3 to = flat(predicted - position);
        const float length = glm::length(to);
        if (length < 1e-3f)
            return ai::ActionStatus::failed;
        direction = to / length;
        // lands where its body meets the target's (the bite reaches from there)
        aim = position + direction * std::max(length - b.contact_distance, 0.5f);
        aim.y = predicted.y;
        // not through a wall
        if (c.nav)
        {
            glm::vec3 hit(std::numeric_limits<float>::quiet_NaN());
            if (!c.nav->raycast(position, aim, &hit) && !std::isnan(hit.x))
                aim = hit - direction * 0.2f;
        }
        if (c.tokens)
            c.tokens->acquire(target, c.self, std::numeric_limits<int>::max(), c.now, 3.0);   // holds it through the leap
        phase = Phase::leap;
        time = 0.0f;
        return ai::ActionStatus::running;
    }
    case Phase::leap:
    {
        m.destination = aim;
        m.direct = true;
        m.speed = b.lunge_speed;
        m.acceleration = 40.0f;
        m.deceleration = 30.0f;
        m.arrive_distance = 0.15f;
        m.turn_scale = 0.2f;

        // the bite: the target within reach of the muzzle, in front of it
        if (!bitten && same_target && c.target_distance <= b.bite_range)
        {
            const glm::vec3 to_target = flat(c.target_position - position);
            const float length = glm::length(to_target);
            if (length < 1e-3f || glm::dot(forward_of(c.body.heading), to_target / length) > 0.3f)
            {
                bitten = true;
                c.damage.send(DamageEvent{
                    .target = target,
                    .source = c.self,
                    .amount = b.bite_damage,
                    .type = DamageType::Generic,
                    .point = c.target_position + up * 0.9f,
                    .direction = direction,
                });
                c.debug.log.add(c.now, std::format("bites {}", target_name(c)));

                // the flash at the muzzle: particles and a short JPEG spot
                const glm::vec3 forward = forward_of(c.body.heading);
                const glm::vec3 muzzle = position + forward * 0.55f + up * 0.42f;
                if (!b.bite_jpeg.empty())
                    c.spots.send({ .position = muzzle, .preset = b.bite_jpeg });
                if (!b.bite_effect.empty())
                {
                    Transform placement;
                    placement.position = muzzle;
                    placement.rotation = glm::angleAxis(c.body.heading, up);
                    c.spawns.send({ .prefab = b.bite_effect, .placement = placement });
                }
                phase = Phase::recover;
                time = 0.0f;
                return ai::ActionStatus::running;
            }
        }
        // landed, or stopped by something
        if (horizontal_distance(position, aim) < 0.4f || time > 1.2f || (time > 0.3f && c.body.speed < 1.0f))
        {
            if (!bitten)
                c.debug.log.add(c.now, "missed");
            phase = Phase::recover;
            time = 0.0f;
        }
        return ai::ActionStatus::running;
    }
    case Phase::recover:
        // stops short: from the leap's speed it would slide on for metres
        m.deceleration = 30.0f;
        if (same_target)
            m.face = c.target_position;
        return time < 0.4f ? ai::ActionStatus::running : ai::ActionStatus::succeeded;
    }
    return ai::ActionStatus::failed;
}

void JackalLunge::stop(JackalContext& c)
{
    if (c.tokens)
        c.tokens->release(target, c.self);
    c.brain.next_lunge = c.now + c.random(c.brain.lunge_cooldown_min, c.brain.lunge_cooldown_max);
}

bool JackalLunge::interruptible(const JackalContext&) const
{
    return phase == Phase::telegraph;
}

// ---------------------------------------------------------------- brain

namespace
{
    // confirmed targets within the leash: the nearest, keeping its own, the player first, seen ones first
    ecs::Entity choose_target(const JackalContext& c, const ecs::Query<const Player>& players)
    {
        ecs::Entity best;
        float best_score = std::numeric_limits<float>::infinity();
        for (const ai::KnownTarget& k : c.perception.known)
        {
            if (!k.confirmed || horizontal_distance(k.last_position, c.home) > c.leash)
                continue;
            float score = k.distance;
            if (k.entity == c.brain.target)
                score -= 4.0f;
            if (players.get(k.entity))
                score -= 2.0f;
            if (!k.visible)
                score += 3.0f;
            if (score < best_score)
            {
                best_score = score;
                best = k.entity;
            }
        }
        return best;
    }

    // something it is not sure of yet, within the leash
    const ai::KnownTarget* find_suspect(const JackalContext& c)
    {
        const ai::KnownTarget* result = nullptr;
        for (const ai::KnownTarget& k : c.perception.known)
            if (!k.confirmed && k.awareness >= 0.1f && horizontal_distance(k.last_position, c.home) <= c.leash
                && (!result || k.awareness > result->awareness))
                result = &k;
        return result;
    }

    void start_flee(JackalContext& c)
    {
        const JackalBrain& b = c.brain;
        glm::vec3 to = c.home;
        // the target stands between it and home (or at home): away from the target instead
        if (c.known && horizontal_distance(c.home, c.target_position) < c.target_distance + 4.0f)
        {
            glm::vec3 away = flat(c.body.position - c.target_position);
            away = glm::length(away) > 1e-3f ? glm::normalize(away) : forward_of(c.body.heading + pi);
            to = c.body.position + away * 15.0f;
            if (c.nav)
                if (std::optional<glm::vec3> p = c.nav->project(to, { 3.0f, 4.0f, 3.0f }))
                    to = *p;
        }
        c.brain.action.start(c, JackalGoTo{ .destination = to, .speed = b.run_speed, .arrive_distance = 2.0f, .time_left = 15.0f });
    }

    void start_idle_activity(JackalContext& c)
    {
        const JackalBrain& b = c.brain;
        // keeps up with the leader
        if (c.leader && horizontal_distance(c.body.position, *c.leader) > 7.0f)
        {
            const float angle = c.random(0.0f, 2.0f * pi);
            const glm::vec3 near_leader = *c.leader + glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * 2.0f;
            c.brain.action.start(c, JackalGoTo{ .destination = near_leader, .speed = b.trot_speed, .arrive_distance = 1.5f, .time_left = 15.0f });
            return;
        }
        if (c.random() < 0.5f)
        {
            c.brain.action.start(c, JackalRest{ .time_left = c.random(3.0f, 8.0f) });
            return;
        }
        // a stroll in the territory
        std::optional<glm::vec3> point;
        if (c.nav)
            point = c.nav->random_point_around(c.home, c.territory, uint32_t(c.brain.rng()));
        if (!point)
        {
            c.brain.action.start(c, JackalRest{ .time_left = c.random(2.0f, 5.0f) });
            return;
        }
        c.brain.action.start(c, JackalGoTo{ .destination = *point, .speed = b.walk_speed, .arrive_distance = 0.6f, .time_left = 25.0f });
    }

    void engage(JackalContext& c)
    {
        JackalBrain& b = c.brain;
        set_mode(c, JackalMode::combat, std::format("{} {} at {:.0f} m", sense_verb(c.known->sense), target_name(c), c.target_distance));
        b.slot.reset();
        b.slot_index = -1;
        // the one that found it calls the pack (the ones it told don't call again)
        if (c.known->sense != ai::Sense::shared && c.now - b.last_howl > 15.0)
        {
            b.last_howl = c.now;
            b.action.start(c, JackalHowl{});
        }
        else
            b.action.start(c, JackalCircle{});
    }

    void fight(JackalContext& c)
    {
        JackalBrain& b = c.brain;
        if (b.action.is<JackalLunge>() || b.action.is<JackalHowl>() || b.action.is<JackalFeint>())
            return;   // they finish

        const int capacity = c.target_is_player ? cv_attackers.get() : 1;
        const bool ready = c.now >= b.next_lunge && c.known->visible && c.target_distance <= b.lunge_range
            && c.target_distance > b.bite_range;
        if (ready && c.tokens && c.tokens->acquire(b.target, c.self, capacity, c.now, 4.0))
        {
            b.action.start(c, JackalLunge{});
            return;
        }
        if (!b.action.is<JackalCircle>())
        {
            b.action.start(c, JackalCircle{});
            return;
        }
        // a threat now and then: close to the target without a token
        if (c.known->visible && c.target_distance < b.ring_radius + 1.5f && c.random() < b.feint_chance * b.think.interval)
            b.action.start(c, JackalFeint{});
    }

    // the decisions: a few times per second
    template<typename Resolve>
    void think(JackalContext& c, const ecs::Query<const Player>& players, Resolve&& resolve)
    {
        JackalBrain& b = c.brain;
        if (!b.action.interruptible(c))
            return;   // in the air

        // a target that left the territory is given up (and forgotten: it does not come back to it at once)
        if (b.target && c.known && horizontal_distance(c.known->last_position, c.home) > c.leash)
        {
            const std::string name = target_name(c);
            c.perception.forget(b.target);
            b.target = {};
            resolve(c);
            if (c.tokens)
                c.tokens->release_all(c.self);
            set_mode(c, JackalMode::return_home, std::format("{} left the territory", name));
            b.action.start(c, JackalGoTo{ .destination = c.home, .speed = b.trot_speed, .arrive_distance = 2.0f, .time_left = 60.0f });
            return;
        }

        const ecs::Entity chosen = choose_target(c, players);
        if (chosen != b.target)
        {
            if (c.tokens)
                c.tokens->release_all(c.self);
            b.target = chosen;
            resolve(c);
        }
        const ai::KnownTarget* suspect = find_suspect(c);

        // ---- fear first
        if (c.health < b.flee_health || c.pack_broken)
        {
            if (b.mode != JackalMode::flee)
            {
                set_mode(c, JackalMode::flee, c.pack_broken ? "the pack is broken" : std::format("wounded ({:.0f}%)", c.health * 100.0f));
                if (c.tokens)
                    c.tokens->release_all(c.self);
                start_flee(c);
            }
            else if (b.action.idle())
            {
                // keeps away from what hurt it; cowers when it is far enough
                if (c.known && c.target_distance < 12.0f)
                    start_flee(c);
                else
                    b.action.start(c, JackalRest{ .time_left = c.random(2.0f, 4.0f), .face = c.known ? std::optional(c.target_position) : std::nullopt });
            }
            return;
        }

        switch (b.mode)
        {
        case JackalMode::dead:
            return;

        case JackalMode::flee:
            set_mode(c, JackalMode::idle, "calmed down");
            b.action.stop(c);
            return;

        case JackalMode::combat:
            if (!c.known)
            {
                if (c.tokens)
                    c.tokens->release_all(c.self);
                b.slot.reset();
                if (horizontal_distance(c.body.position, c.home) > c.territory)
                {
                    set_mode(c, JackalMode::return_home, "lost the target");
                    b.action.start(c, JackalGoTo{ .destination = c.home, .speed = b.trot_speed, .arrive_distance = 2.0f, .time_left = 60.0f });
                }
                else
                {
                    set_mode(c, JackalMode::idle, "lost the target");
                    b.action.stop(c);
                }
                return;
            }
            fight(c);
            return;

        case JackalMode::idle:
        case JackalMode::alert:
        case JackalMode::return_home:
            if (c.known)
            {
                engage(c);
                return;
            }
            if (suspect)
            {
                // watches it a moment, then goes to look
                if (b.mode != JackalMode::alert)
                {
                    set_mode(c, JackalMode::alert, std::format("noticed something at {:.0f} m", suspect->distance));
                    b.action.start(c, JackalRest{ .time_left = b.alert_time, .face = suspect->last_position });
                }
                else if (b.action.idle())
                    b.action.start(c, JackalGoTo{ .destination = suspect->last_position, .speed = b.trot_speed, .arrive_distance = 2.0f,
                        .time_left = 12.0f });
                return;
            }
            if (b.mode == JackalMode::alert)
            {
                set_mode(c, JackalMode::idle, "nothing there");
                b.action.stop(c);
            }
            if (b.mode == JackalMode::return_home)
            {
                if (horizontal_distance(c.body.position, c.home) < c.territory * 0.6f || b.action.idle())
                    set_mode(c, JackalMode::idle, "home");
                else
                    return;
            }
            if (b.action.idle())
                start_idle_activity(c);
            return;
        }
    }

    // ---------------------------------------------------------------- ring slots

    // Places on a ring around each target for the jackals fighting it: more slots than jackals, beside and behind
    // the target preferred (a jackal in front of it costs as much as 6 m of running), the ring turning slowly so the
    // pack circles; a slot must be on the mesh with open ground to the target. Greedy by cost, keeping old slots.
    void assign_slots(const ecs::Query<JackalBrain, Jackal, ai::Perception>& jackals, const ecs::Query<const ai::Perceivable>& perceivables,
        const ecs::Query<const WorldTransform>& transforms, const nav::NavMesh* nav, double now)
    {
        struct Seeker
        {
            JackalBrain* brain;
            glm::vec3 position;
        };
        std::map<ecs::Entity, std::vector<Seeker>> by_target;
        jackals.each([&] (JackalBrain& b, Jackal& j, ai::Perception&) {
            if (b.mode != JackalMode::combat || !b.target)
            {
                b.slot.reset();
                b.slot_index = -1;
                return;
            }
            by_target[b.target].push_back({ &b, j.position });
        });

        for (auto& [target, seekers] : by_target)
        {
            const ai::Perceivable* p = perceivables.get(target);
            const WorldTransform* world = transforms.get(target);
            if (!(p && p->measured) && !world)
            {
                for (Seeker& s : seekers)
                    s.brain->slot.reset();
                continue;
            }
            const glm::vec3 center = p && p->measured ? p->position : world->value.position.glm();
            glm::vec3 forward = world ? flat(world->value.rotation.glm() * glm::vec3(0.0f, 0.0f, 1.0f)) : glm::vec3(0.0f, 0.0f, 1.0f);
            forward = glm::length(forward) > 1e-3f ? glm::normalize(forward) : glm::vec3(0.0f, 0.0f, 1.0f);

            const JackalBrain& tuning = *seekers.front().brain;
            const int count = std::max(int(seekers.size()) + 2, 6);
            const float base = glm::radians(tuning.orbit_speed) * float(std::fmod(now, 3600.0));

            struct Slot
            {
                glm::vec3 position{ 0.0f };
                float front = 0.0f;
                bool valid = false;
            };
            std::vector<Slot> slots(static_cast<size_t>(count));
            for (int k = 0; k < count; ++k)
            {
                const float angle = base + 2.0f * pi * float(k) / float(count);
                const glm::vec3 direction(std::sin(angle), 0.0f, std::cos(angle));
                Slot& slot = slots[size_t(k)];
                slot.front = std::max(glm::dot(direction, forward), 0.0f);
                // on the ring, else closer in (a wall, the edge of the mesh)
                for (float scale : { 1.0f, 0.65f })
                {
                    glm::vec3 point = center + direction * (tuning.ring_radius * scale);
                    if (nav)
                    {
                        std::optional<glm::vec3> projected = nav->project(point, { 1.0f, 3.0f, 1.0f });
                        if (!projected || !open_ground(nav, center, *projected))
                            continue;
                        point = *projected;
                    }
                    slot.position = point;
                    slot.valid = true;
                    break;
                }
            }

            struct Pair
            {
                float cost;
                size_t seeker;
                int slot;
            };
            std::vector<Pair> pairs;
            for (size_t i = 0; i < seekers.size(); ++i)
                for (int k = 0; k < count; ++k)
                {
                    const Slot& slot = slots[size_t(k)];
                    if (!slot.valid)
                        continue;
                    float cost = horizontal_distance(seekers[i].position, slot.position) + slot.front * 6.0f;
                    if (seekers[i].brain->slot_index == k)
                        cost -= 2.0f;
                    pairs.push_back({ cost, i, k });
                }
            std::ranges::sort(pairs, {}, &Pair::cost);
            std::vector<bool> seeker_done(seekers.size(), false);
            std::vector<bool> slot_taken(size_t(count), false);
            for (const Pair& pair : pairs)
            {
                if (seeker_done[pair.seeker] || slot_taken[size_t(pair.slot)])
                    continue;
                seeker_done[pair.seeker] = true;
                slot_taken[size_t(pair.slot)] = true;
                seekers[pair.seeker].brain->slot = slots[size_t(pair.slot)].position;
                seekers[pair.seeker].brain->slot_index = pair.slot;
            }
            for (size_t i = 0; i < seekers.size(); ++i)
                if (!seeker_done[i])
                {
                    seekers[i].brain->slot.reset();
                    seekers[i].brain->slot_index = -1;
                }
        }
    }

    float g_slot_timer = 0.0f;

    // After the senses, before the crowd plans the ways: decisions (ThinkTimer), the running action, the nav target
    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<JackalThink>, =ecs::after<ai::PerceptionUpdate>, =ecs::after<NpcPlanning>,
      =ecs::before<nav::NavAgentUpdate>]]
    void think_jackals(ecs::Query<JackalBrain, Jackal, ai::Perception> jackals, ecs::Query<ai::BrainDebug> debugs,
        ecs::Query<nav::NavAgent> agents, ecs::Query<const PackMember> members, ecs::Query<const JackalDen> dens,
        ecs::Query<const Health> healths, ecs::Query<const Dead> dead, ecs::Query<const ai::Perceivable> perceivables,
        ecs::Query<const WorldTransform> transforms, ecs::Query<const Player> players, ecs::OptResMut<ai::AttackTokens> tokens,
        ecs::OptRes<nav::NavMesh> navmesh, ecs::EventWriter<DamageEvent> damage, ecs::EventWriter<ai::Stimulus> stimuli,
        ecs::EventWriter<JpegSpotEvent> spots, ecs::EventWriter<SpawnPrefabRequest> spawns, ecs::Res<ecs::SimTime> time)
    {
        const double now = time->time;
        const float dt = (float)time->dt;
        const nav::NavMesh* nav = navmesh ? navmesh.get() : nullptr;
        ai::AttackTokens* attack_tokens = tokens ? tokens.get() : nullptr;

        std::unordered_map<ecs::Entity, glm::vec3> positions;
        jackals.each([&] (ecs::Entity e, JackalBrain&, Jackal& j, ai::Perception&) {
            positions[e] = j.position;
        });

        g_slot_timer -= dt;
        if (g_slot_timer <= 0.0f)
        {
            g_slot_timer = 0.25f;
            assign_slots(jackals, perceivables, transforms, nav, now);
        }

        const bool enabled = cv_ai.get();
        jackals.each([&] (ecs::Entity e, JackalBrain& brain, Jackal& body, ai::Perception& perception) {
            if (!body.initialized)
                return;
            ai::BrainDebug fallback;
            ai::BrainDebug* debug = debugs.get(e);
            nav::NavAgent* agent = agents.get(e);
            JackalContext c{ .self = e, .body = body, .brain = brain, .perception = perception, .debug = debug ? *debug : fallback,
                .agent = agent, .nav = nav, .tokens = attack_tokens, .damage = damage, .stimuli = stimuli, .spots = spots,
                .spawns = spawns, .now = now };

            if (!brain.initialized)
            {
                brain.initialized = true;
                brain.rng.seed(uint32_t(e.bits() * 2654435761u) | 1u);
                brain.stamina_left = brain.stamina;
                brain.next_lunge = now + c.random(1.0f, 3.0f);
                brain.home = body.position;
            }

            // home and pack
            c.home = brain.home.value_or(body.position);
            c.territory = brain.territory_radius;
            c.leash = brain.leash_radius;
            if (const PackMember* member = members.get(e))
                if (const JackalDen* den = dens.get(member->den))
                {
                    if (den->home)
                        c.home = *den->home;
                    else if (const WorldTransform* w = transforms.get(member->den))
                        c.home = w->value.position.glm();
                    c.territory = den->territory_radius;
                    c.leash = den->leash_radius;
                    c.pack_broken = den->broken(now);
                    if (den->leader && den->leader != e)
                        if (auto it = positions.find(den->leader); it != positions.end())
                            c.leader = it->second;
                }
            if (const Health* health = healths.get(e))
                c.health = health->fraction();

            auto resolve = [&] (JackalContext& ctx) {
                ctx.known = ctx.brain.target ? ctx.perception.find(ctx.brain.target) : nullptr;
                ctx.target_is_player = ctx.brain.target && players.get(ctx.brain.target) != nullptr;
                if (!ctx.known)
                {
                    ctx.target_distance = std::numeric_limits<float>::infinity();
                    return;
                }
                ctx.target_position = ctx.known->last_position;
                ctx.target_velocity = ctx.known->last_velocity;
                if (const ai::Perceivable* p = perceivables.get(ctx.brain.target); p && ctx.known->visible)
                {
                    ctx.target_position = p->position;
                    ctx.target_velocity = p->velocity;
                }
                if (const WorldTransform* w = transforms.get(ctx.brain.target))
                {
                    const glm::vec3 f = flat(w->value.rotation.glm() * glm::vec3(0.0f, 0.0f, 1.0f));
                    if (glm::length(f) > 1e-3f)
                        ctx.target_forward = glm::normalize(f);
                }
                ctx.target_distance = horizontal_distance(ctx.body.position, ctx.target_position);
            };

            body.move = {};
            if (dead.get(e))
            {
                if (brain.mode != JackalMode::dead)
                {
                    brain.action.stop(c);
                    if (attack_tokens)
                        attack_tokens->release_all(e);
                    set_mode(c, JackalMode::dead, "died");
                }
                if (agent)
                    nav::stop(*agent);
                return;
            }
            if (!enabled)
            {
                brain.action.stop(c);
                if (agent)
                    nav::stop(*agent);
                c.debug.mode = "off (game.jackal.ai)";
                return;
            }

            resolve(c);
            if (brain.think.due(dt, float(e.index % 10) / 10.0f))
            {
                think(c, players, resolve);
                resolve(c);
            }
            brain.action.tick(c, dt);
            // whatever it does in a fight, it never walks into its target's body
            if (c.known && brain.mode == JackalMode::combat && !body.move.keep_away_from)
            {
                body.move.keep_away_from = c.target_position;
                body.move.keep_away_distance = brain.contact_distance;
            }

            // stamina: spent sprinting (the leap aside), back otherwise
            if (body.move.speed > brain.run_speed + 0.05f && !brain.action.is<JackalLunge>())
                brain.stamina_left = std::max(brain.stamina_left - dt, 0.0f);
            else
                brain.stamina_left = std::min(brain.stamina_left + brain.stamina_recovery * dt, brain.stamina);

            // the crowd plans the way (the leap goes straight)
            if (agent)
            {
                if (body.move.destination && !body.move.direct)
                    nav::move_to(*agent, *body.move.destination);
                else
                    nav::stop(*agent);
                agent->max_speed = std::max(body.move.speed, 0.5f);
            }
            body.target_distance = c.known ? c.target_distance : 0.0f;

            c.debug.mode = mode_name(brain.mode);
            c.debug.action = brain.action.name();
            c.debug.target = c.known ? std::format("{} {:.1f} m{}, awareness {:.2f}", target_name(c), c.target_distance,
                c.known->visible ? " (seen)" : "", c.known->awareness) : std::string("-");
            c.debug.detail = std::format("health {:.0f}%, stamina {:.1f} s, slot {}{}", c.health * 100.0f, brain.stamina_left,
                brain.slot_index, c.pack_broken ? ", pack broken" : "");
        });
    }

    // ---------------------------------------------------------------- dens

    std::mt19937 g_den_rng{ 0xde75u };

    std::optional<glm::vec3> find_ground(const phys::PhysicsScene& physics, const glm::vec3& p)
    {
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world;
        std::optional<phys::Hit> hit = physics.raycast(p + up * 4.0f, -up, 12.0f, filter);
        if (!hit || hit->normal.y < 0.6f)
            return std::nullopt;
        return hit->position;
    }

    // After the blows of the tick (the dead are counted), before the physics step: losses, the leader, respawns
    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::after<DamageResolution>, =ecs::before<scene::PhysicsStep>]]
    void keep_jackal_dens(ecs::Query<JackalDen, const Transform> dens, ecs::Query<const PackMember> members, ecs::Query<const Dead> dead,
        ecs::Query<const WorldTransform, ecs::With<Player>> players, ecs::EventReader<DeathEvent> deaths, ecs::Res<phys::PhysicsScene> physics,
        ecs::Res<ecs::SimTime> time, ecs::EventWriter<SpawnPrefabRequest> spawns)
    {
        const double now = time->time;
        const float dt = (float)time->dt;

        for (const DeathEvent& death : deaths.read())
            if (const PackMember* member = members.get(death.entity))
                if (JackalDen* den = dens.get<JackalDen>(member->den))
                    den->deaths.push_back(now);

        std::vector<glm::vec3> player_positions;
        players.each([&] (const WorldTransform& world) { player_positions.push_back(world.value.position.glm()); });

        dens.each([&] (ecs::Entity e, JackalDen& den, const Transform& transform) {
            const glm::vec3 center = transform.position.glm();
            if (!den.home)
                den.home = find_ground(*physics, center);
            const glm::vec3 home = den.home.value_or(center);

            // the pack: alive members, a leader among them
            int alive = 0;
            bool leader_alive = false;
            ecs::Entity first;
            members.each([&] (ecs::Entity m, const PackMember& member) {
                if (member.den != e || dead.get(m))
                    return;
                ++alive;
                leader_alive = leader_alive || m == den.leader;
                if (!first)
                    first = m;
            });
            den.alive = alive;
            if (!leader_alive)
                den.leader = first;

            // morale: too many losses in a short time break the pack for a while
            std::erase_if(den.deaths, [&] (double t) { return now - t > den.morale_window; });
            const int breaking_losses = std::max(1, int(std::ceil(float(den.count) * den.morale_losses)));
            if (int(den.deaths.size()) >= breaking_losses && !den.broken(now))
            {
                den.broken_until = now + den.broken_time;
                den.deaths.clear();
            }

            // the pack at once, then one at a time while nobody looks
            int wanted = 0;
            if (!den.populated)
            {
                den.populated = true;
                wanted = den.count;
                den.respawn_timer = den.respawn_interval;
            }
            else if (alive < den.count)
            {
                den.respawn_timer -= dt;
                const bool watched = std::ranges::any_of(player_positions, [&] (const glm::vec3& p) {
                    return horizontal_distance(p, home) < den.respawn_distance;
                });
                if (den.respawn_timer <= 0.0f && !watched)
                {
                    wanted = 1;
                    den.respawn_timer = den.respawn_interval;
                }
            }
            else
                den.respawn_timer = den.respawn_interval;

            std::uniform_real_distribution<float> unit(0.0f, 1.0f);
            for (int i = 0; i < wanted; ++i)
            {
                const float angle = unit(g_den_rng) * 2.0f * pi;
                const float r = std::sqrt(unit(g_den_rng)) * den.spawn_radius;
                const glm::vec3 spot = home + glm::vec3(std::sin(angle), 0.0f, std::cos(angle)) * r;
                Transform placement;
                placement.position = find_ground(*physics, spot).value_or(spot);
                placement.rotation = glm::angleAxis(unit(g_den_rng) * 2.0f * pi, up);
                spawns.send({
                    .prefab = den.prefab,
                    .placement = placement,
                    .on_spawned = [den_entity = e] (ecs::Registry& registry, ecs::Entity spawned) {
                        registry.add<PackMember>(spawned, PackMember{ den_entity });
                    },
                });
            }
        });
    }

    // ---------------------------------------------------------------- debug

    [[=ecs::system<ecs::Phase::Late>, =ecs::before<RenderSync>]]
    void draw_jackal_brains(ecs::Query<const JackalBrain, const Jackal> jackals, ecs::Query<const JackalDen> dens, ecs::Res<ecs::SimTime> time)
    {
        if (!cv_debug.get())
            return;
        const double now = time->time;
        std::vector<debug_draw::Line> lines;
        auto circle = [&] (const glm::vec3& center, float radius, const glm::vec4& color) {
            constexpr int segments = 32;
            for (int i = 0; i < segments; ++i)
            {
                const float a0 = 2.0f * pi * float(i) / segments, a1 = 2.0f * pi * float(i + 1) / segments;
                lines.push_back({ center + glm::vec3(std::sin(a0), 0.1f, std::cos(a0)) * radius,
                    center + glm::vec3(std::sin(a1), 0.1f, std::cos(a1)) * radius, color });
            }
        };
        dens.each([&] (const JackalDen& den) {
            if (!den.home)
                return;
            circle(*den.home, den.territory_radius, den.broken(now) ? glm::vec4(1.0f, 0.3f, 0.3f, 0.6f) : glm::vec4(0.4f, 1.0f, 0.4f, 0.5f));
            circle(*den.home, den.leash_radius, glm::vec4(0.4f, 1.0f, 0.4f, 0.2f));
        });
        jackals.each([&] (const JackalBrain& b, const Jackal& j) {
            const glm::vec3 body = j.position + up * 0.5f;
            if (b.slot)
            {
                const glm::vec4 cyan{ 0.2f, 0.9f, 1.0f, 0.9f };
                lines.push_back({ body, *b.slot + up * 0.1f, glm::vec4(cyan.r, cyan.g, cyan.b, 0.35f) });
                const float s = 0.3f;
                lines.push_back({ *b.slot + glm::vec3(-s, 0.1f, -s), *b.slot + glm::vec3(s, 0.1f, s), cyan });
                lines.push_back({ *b.slot + glm::vec3(-s, 0.1f, s), *b.slot + glm::vec3(s, 0.1f, -s), cyan });
            }
            if (const JackalLunge* lunge = b.action.get<JackalLunge>(); lunge && lunge->phase != JackalLunge::Phase::telegraph)
                lines.push_back({ body, lunge->aim + up * 0.5f, glm::vec4(1.0f, 0.2f, 0.1f, 1.0f) });
            if (j.move.destination && !b.slot)
                lines.push_back({ body, *j.move.destination + up * 0.1f, glm::vec4(1.0f, 1.0f, 1.0f, 0.3f) });
        });
        debug_draw::lines(lines);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(JackalBrain, JackalDen, PackMember)
}
