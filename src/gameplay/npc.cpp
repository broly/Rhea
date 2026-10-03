module;

#include <json/value.h>

module gameplay;

import :npc;
import :player;
import :health;
import :weapons;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;
import locomotion;
import navigation;
import ai;
import fixed_string;
import log;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogNpc, Log);

namespace
{
    // the capsules of locomotion characters are the bodies of their nav agents
    struct LocomotionAgentSync {};

    [[=scene::on_spawned<Npc>]]
    void init_npc(World& world, ecs::Entity e, const SerializationContext&)
    {
        ecs::Registry& registry = world.registry;
        const std::string name = scene::get_name(registry, e);
        const SkinnedMesh* skinned = registry.get<SkinnedMesh>(e);
        if (!skinned || !skinned->skeletal_mesh.is_valid())
        {
            LogNpc.Log<Warning>("NPC '%s' has no skinned mesh: not a character", name.c_str());
            return;
        }
        if (!skinned->pose)   // the SkinnedMesh may be spawned after us
            init_skinned_mesh(registry, e);

        const std::string settings = registry.get<Npc>(e)->locomotion;
        if (!loco::init_locomotion_character(world, e, settings) || !loco::init_locomotion_animation(world, e))
        {
            LogNpc.Log<Warning>("NPC '%s': no locomotion character (settings '%s')", name.c_str(), settings.c_str());
            return;
        }
        const loco::LocomotionCharacter& c = *registry.get<loco::LocomotionCharacter>(e);
        if (!registry.has<nav::NavAgent>(e))
            registry.add<nav::NavAgent>(e, nav::NavAgent{
                .radius = c.scaled(c.settings->capsule_radius),
                .height = c.capsule_height,
                .max_speed = c.gait_settings.run_forward_speed,
            });
        // armed (NpcCombat): a weapon command per tick from its brain (the models in the hand: npc_combat)
        if (registry.has<WeaponHolder>(e) && !registry.has<WeaponInput>(e))
            registry.add<WeaponInput>(e);
        if (registry.has<NpcCombat>(e) && !registry.has<ai::BrainDebug>(e))
            registry.add<ai::BrainDebug>(e);   // Windows > AI
        registry.get<Npc>(e)->initialized = true;
    }

    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<LocomotionAgentSync>, =ecs::after<loco::LocomotionSimulation>,
      =ecs::before<nav::NavAgentUpdate>]]
    void sync_locomotion_agents(ecs::Query<nav::NavAgent, const loco::LocomotionCharacter> agents)
    {
        agents.each([] (nav::NavAgent& agent, const loco::LocomotionCharacter& c) {
            agent.position = c.position;
            agent.velocity = c.velocity;
        });
    }

    // where to go and how fast
    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<NpcPlanning>, =ecs::after<LocomotionAgentSync>, =ecs::before<nav::NavAgentUpdate>]]
    void plan_npcs(ecs::Query<Npc, nav::NavAgent, const loco::LocomotionCharacter> npcs,
        ecs::Query<const loco::LocomotionCharacter, ecs::With<Player>> players, ecs::Query<const Dead> dead)
    {
        std::optional<glm::vec3> player;
        players.each([&] (const loco::LocomotionCharacter& c) {
            if (!player)
                player = c.position;
        });

        npcs.each([&] (ecs::Entity e, Npc& npc, nav::NavAgent& agent, const loco::LocomotionCharacter& c) {
            if (!npc.initialized)
                return;
            if (!npc.follow_player || !player || dead.get(e))
            {
                npc.following = false;
                npc.face_yaw.reset();
                nav::stop(agent);
                return;
            }

            glm::vec3 to_player = *player - c.position;
            to_player.y = 0.0f;
            const float distance = glm::length(to_player);
            npc.player_distance = distance;
            npc.face_yaw = distance > 0.1f ? std::optional<float>(loco::direction_to_yaw(to_player)) : std::nullopt;

            // sets off when the player is catch_up_distance away, stops at follow_distance
            if (!npc.following && distance > npc.catch_up_distance)
                npc.following = true;
            else if (npc.following && distance <= npc.follow_distance)
                npc.following = false;
            if (npc.following)
                nav::move_to(agent, *player);
            else
                nav::stop(agent);

            // gait by the distance, a few meters of hysteresis so it does not flicker
            uint8_t gait = npc.gait;
            if (distance > npc.sprint_distance)
                gait = 2;
            else if (gait == 2 && distance < npc.sprint_distance - 3.0f)
                gait = 1;
            if (gait < 2)
            {
                if (distance > npc.run_distance)
                    gait = 1;
                else if (gait == 1 && distance < npc.run_distance - 2.0f)
                    gait = 0;
            }
            npc.gait = gait;
            // the crowd plans with the speed the character will go at
            const loco::GaitSettings& speeds = c.gait_settings;
            agent.max_speed = gait == 2 ? speeds.sprint_speed : gait == 1 ? speeds.run_forward_speed : speeds.walk_forward_speed;
        });
    }

    // the crowd's steering as the input of the locomotion character; aiming at its target it strafes
    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::after<nav::NavAgentUpdate>, =ecs::after<loco::LocomotionSimulation>,
      =ecs::after<NpcCombatSet>, =ecs::before<loco::LocomotionInputSet>]]
    void drive_npcs(ecs::Query<const Npc, const nav::NavAgent, loco::LocomotionInput, loco::LocomotionCharacter> npcs,
        ecs::Query<const NpcCombat> combats)
    {
        npcs.each([&] (ecs::Entity e, const Npc& npc, const nav::NavAgent& agent, loco::LocomotionInput& input, loco::LocomotionCharacter& c) {
            if (!npc.initialized)
                return;
            const NpcCombat* combat = combats.get(e);
            const std::optional<float> aim_yaw = combat ? combat->aim_yaw : std::nullopt;
            const float view_yaw = input.view_yaw;
            input = {};
            input.view_yaw = view_yaw;
            input.set_rotation_mode = true;

            glm::vec3 move(0.0f);
            if (npc.following)
            {
                if (nav::is_following_path(agent) || agent.status == nav::NavStatus::planning)
                {
                    move = glm::vec3(agent.desired_velocity.x, 0.0f, agent.desired_velocity.z);
                    if (glm::length(move) < 0.05f && agent.has_corner)
                        move = glm::vec3(agent.next_corner.x - c.position.x, 0.0f, agent.next_corner.z - c.position.z);
                }
                else if ((agent.status == nav::NavStatus::off_mesh || agent.status == nav::NavStatus::unreachable) && npc.face_yaw)
                    move = loco::yaw_to_direction(*npc.face_yaw);   // no mesh: straight at the player
            }

            if (aim_yaw)
            {
                // aims at its target (ALS aiming: faces the view), the way to go relative to it
                input.aim = true;
                input.view_yaw = *aim_yaw;
                input.view_pitch = combat->aim_pitch;
                input.rotation_mode = loco::RotationMode::view_direction;
                if (glm::length(move) > 0.05f)
                {
                    const glm::vec3 direction = glm::normalize(move);
                    const glm::vec3 forward = loco::yaw_to_direction(*aim_yaw);
                    const glm::vec3 right = loco::yaw_to_direction(*aim_yaw + 90.0f);
                    input.move_axis = { glm::dot(direction, right), glm::dot(direction, forward) };
                }
            }
            else if (glm::length(move) > 0.05f)
            {
                // runs where it goes
                input.view_yaw = loco::direction_to_yaw(move);
                input.move_axis = { 0.0f, 1.0f };
                input.rotation_mode = loco::RotationMode::velocity_direction;
                input.sprint = npc.gait == 2;
            }
            else
            {
                // stands facing the player: turns in place
                if (npc.face_yaw)
                    input.view_yaw = *npc.face_yaw;
                input.rotation_mode = loco::RotationMode::view_direction;
            }
            // sprinting comes from the held input (the locomotion character turns it back to running without it)
            c.desired_gait = npc.gait == 0 ? loco::Gait::walking : loco::Gait::running;
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Npc)
}
