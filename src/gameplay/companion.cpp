module;

#include <json/value.h>

module gameplay;

import :npc;
import :player;
import :health;
import :weapons;
import :jackal;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import locomotion;
import ai;
import cvar;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

// The NPCs' fighting (NpcCombat, AI6): see gameplay:npc
namespace
{
    cvar::Var<bool> cv_fire("game.companion.fire", true, "The companion shoots the creatures (off: it only follows)");

    constexpr glm::vec3 up{ 0.0f, 1.0f, 0.0f };

    float horizontal_distance(const glm::vec3& a, const glm::vec3& b)
    {
        return glm::length(glm::vec2(a.x - b.x, a.z - b.z));
    }

    // a direction within half_angle (radians) of `direction`
    glm::vec3 scatter(const glm::vec3& direction, float half_angle, std::minstd_rand& rng)
    {
        if (half_angle <= 0.0f)
            return direction;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const float cos_theta = glm::mix(1.0f, std::cos(half_angle), unit(rng));
        const float sin_theta = std::sqrt(std::max(1.0f - cos_theta * cos_theta, 0.0f));
        const float phi = unit(rng) * 6.28318531f;
        const glm::vec3 side_axis = std::abs(direction.y) < 0.99f ? up : glm::vec3(1.0f, 0.0f, 0.0f);
        const glm::vec3 right = glm::normalize(glm::cross(direction, side_axis));
        const glm::vec3 side = glm::cross(right, direction);
        return glm::normalize(direction * cos_theta + (right * std::cos(phi) + side * std::sin(phi)) * sin_theta);
    }

    // After the senses, the player's weapon input and the jackals' decisions (who leaps at the player this tick),
    // before the NPCs are driven (the aim turns them)
    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<NpcCombatSet>, =ecs::after<ai::PerceptionUpdate>, =ecs::after<WeaponInputSet>,
      =ecs::after<loco::LocomotionSimulation>, =ecs::after<JackalThink>]]
    void npc_combat(ecs::Query<NpcCombat, WeaponInput, const ai::Perception, const loco::LocomotionCharacter> fighters,
        ecs::Query<const WeaponHolder> holders, ecs::Query<ai::BrainDebug> debugs, ecs::Query<const ai::Perceivable> perceivables,
        ecs::Query<const loco::LocomotionCharacter, ecs::With<Player>> players, ecs::Query<const Dead> dead,
        ecs::OptRes<ai::AttackTokens> tokens, ecs::Res<ecs::SimTime> time, ecs::EventWriter<SpawnPrefabRequest> spawns)
    {
        const double now = time->time;

        ecs::Entity player;
        std::optional<glm::vec3> player_position;
        players.each([&] (ecs::Entity e, const loco::LocomotionCharacter& c) {
            if (!player)
            {
                player = e;
                player_position = c.position;
            }
        });

        fighters.each([&] (ecs::Entity self, NpcCombat& combat, WeaponInput& input, const ai::Perception& perception,
            const loco::LocomotionCharacter& body) {
            if (!combat.initialized)
            {
                combat.initialized = true;
                combat.rng.seed(uint32_t(self.bits() * 2654435761u) | 1u);
            }
            ai::BrainDebug* debug = debugs.get(self);
            input.trigger = false;
            input.reload = false;
            input.select = -1;
            input.has_aim = false;

            const WeaponHolder* holder = holders.get(self);
            const WeaponDef* def = holder ? weapons::find(holder->current) : nullptr;

            // the weapon models in its hand, like the player's (shown while current: show_held_weapon)
            if (holder && !combat.armed)
            {
                combat.armed = true;
                for (const WeaponDef& weapon : weapons::get_all())
                    if (!weapon.model.empty())
                        spawns.send({
                            .prefab = weapon.model,
                            .parent = self,
                            .on_spawned = [name = weapon.name] (ecs::Registry& registry, ecs::Entity model) {
                                registry.add<HeldWeaponModel>(model, HeldWeaponModel{ name });
                            },
                        });
            }
            if (!def || dead.get(self) || !cv_fire.get())
            {
                combat.target = {};
                combat.aim_yaw.reset();
                combat.trigger_was_down = false;
                if (debug)
                {
                    debug->mode = !def ? "unarmed" : "off";
                    debug->action = "follow";
                    debug->target = "-";
                }
                return;
            }

            // ---- what to shoot: seen hostiles in reach, the player's attackers first
            ecs::Entity best;
            float best_score = std::numeric_limits<float>::infinity();
            for (const ai::KnownTarget& k : perception.known)
            {
                if (!k.confirmed || !k.visible)
                    continue;
                const ai::Perceivable* p = perceivables.get(k.entity);
                if (!p || !p->enabled || dead.get(k.entity))
                    continue;
                const float to_self = horizontal_distance(body.position, p->position);
                const float to_player = player_position ? horizontal_distance(*player_position, p->position)
                    : std::numeric_limits<float>::infinity();
                const bool in_reach = to_self <= combat.engage_range
                    || (to_player <= combat.protect_range && to_self <= combat.engage_range * 1.5f);
                if (!in_reach)
                    continue;
                float score = to_self + 0.7f * std::min(to_player, 100.0f);
                if (tokens && player && tokens->holds(player, k.entity))
                    score -= 8.0f;   // going for the player right now
                if (k.entity == combat.target)
                    score -= 3.0f;   // keeps its target
                if (score < best_score)
                {
                    best_score = score;
                    best = k.entity;
                }
            }

            if (best != combat.target)
            {
                combat.target = best;
                combat.target_since = now;
                combat.shots = 0;
                combat.burst_end = 0.0;
                if (debug)
                    debug->log.add(now, best ? std::format("targets #{}", best.index) : std::string("no target"));
            }
            if (!combat.target)
            {
                combat.aim_yaw.reset();
                combat.trigger_was_down = false;
                if (debug)
                {
                    debug->mode = "follow";
                    debug->action = "follow";
                    debug->target = "-";
                }
                return;
            }

            // ---- aim: at the middle of its body, from the eyes
            const ai::Perceivable& target = *perceivables.get(combat.target);
            const glm::vec3 eyes = body.position + up * combat.eye_height;
            const glm::vec3 aim_point = target.position + up * target.center_height;
            const glm::vec3 to = aim_point - eyes;
            const float distance = glm::length(to);
            if (distance < 1e-3f)
                return;
            const glm::vec3 direction = to / distance;
            combat.aim_yaw = loco::direction_to_yaw(glm::vec3(direction.x, 0.0f, direction.z));
            combat.aim_pitch = glm::degrees(std::asin(std::clamp(direction.y, -1.0f, 1.0f)));

            // never through the player
            bool blocked = false;
            if (player_position)
            {
                const glm::vec3 center = *player_position + up * 0.9f;
                const float along = glm::dot(center - eyes, direction);
                blocked = along > 0.0f && along < distance && glm::length(center - (eyes + direction * along)) < 0.6f;
            }

            const bool reacted = now - combat.target_since >= combat.reaction_time;
            std::string action = blocked ? "hold fire (the player in the way)" : reacted ? "shoot" : "aim";
            if (reacted && !blocked)
            {
                // the spread narrows over the first shots at a target
                const float settle = std::clamp(float(combat.shots) / float(std::max(combat.settle_shots, 1)), 0.0f, 1.0f);
                const float error = glm::radians(glm::mix(combat.aim_error, combat.aim_error_min, settle));
                input.has_aim = true;
                input.aim_origin = eyes;
                input.aim_direction = scatter(direction, error, combat.rng);

                if (def->automatic || def->mode == WeaponMode::Beam)
                {
                    // bursts
                    if (now >= combat.burst_end && now >= combat.next_shot)
                    {
                        combat.burst_end = now + combat.burst_time;
                        combat.next_shot = combat.burst_end + combat.burst_pause;
                        ++combat.shots;
                    }
                    input.trigger = now < combat.burst_end;
                }
                else if (now >= combat.next_shot && !combat.trigger_was_down)
                {
                    // one press per shot (charge weapons need a held trigger: not for NPCs yet)
                    input.trigger = true;
                    const float jitter = std::uniform_real_distribution<float>(0.0f, 0.15f)(combat.rng);
                    combat.next_shot = now + std::max(def->cooldown, 0.1f) * combat.cadence + jitter;
                    ++combat.shots;
                }
            }
            else
                combat.burst_end = 0.0;
            combat.trigger_was_down = input.trigger;

            if (debug)
            {
                debug->mode = "fight";
                debug->action = action;
                debug->target = std::format("#{} {:.1f} m", combat.target.index, distance);
                debug->detail = std::format("{}, shots {}, aim {:.0f} / {:.0f} deg", def->name, combat.shots, *combat.aim_yaw,
                    combat.aim_pitch);
            }
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(NpcCombat)
}
