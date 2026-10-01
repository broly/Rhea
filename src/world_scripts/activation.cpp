module;

#include <json/value.h>

module activation;

import std.compat;
import fixed_string;
import glm;
import rhmath;
import ecs;
import log;
import name;
import framework;
import assets;
import rhcomponents;
import character_controller;
import locomotion;
import sky_controller;
import animation;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogActivation, Log);

namespace
{
    // Characters of both controllers (character_controller and locomotion) switch activators on
    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<Activation>]]
    void update_activators(ecs::Query<Activator, const WorldTransform> activators, ecs::Query<const Name> names,
                           ecs::Query<const CharacterMovement> characters,
                           ecs::Query<const loco::LocomotionCharacter> locomotion_characters, ecs::Res<ecs::FrameTime> time)
    {
        std::vector<glm::vec3> feet;
        characters.each([&] (const CharacterMovement& movement) { feet.push_back(movement.position); });
        locomotion_characters.each([&] (const loco::LocomotionCharacter& character) { feet.push_back(character.position); });

        const float dt = float(time->dt);
        activators.each([&] (ecs::Entity e, Activator& activator, const WorldTransform& world) {
            const bool was_active = activator.active;
            const glm::vec3 origin = world.value.position.glm();
            float nearest_distance = std::numeric_limits<float>::max();
            glm::vec3 nearest{ 0.0f };
            for (const glm::vec3& position : feet)
            {
                const float distance = glm::distance(position, origin);
                if (distance < nearest_distance)
                {
                    nearest_distance = distance;
                    nearest = position;
                }
            }

            if (!activator.active)
            {
                if (nearest_distance <= activator.radius)
                {
                    activator.active = true;
                    activator.away_time = 0.0f;
                    if (activator.time <= 0.0f)
                    {
                        activator.activations++;
                        activator.activated_from = nearest;
                    }
                }
            }
            else if (!activator.stays_on)
            {
                activator.away_time = nearest_distance <= activator.release_radius ? 0.0f : activator.away_time + dt;
                if (activator.away_time >= activator.close_delay)
                    activator.active = false;
            }

            if (activator.active != was_active)
            {
                const Name* name = names.get(e);
                LogActivation.Log("'%s' %s (nearest character %.2f m away)", name ? name->to_string().c_str() : "?",
                    activator.active ? "on" : "off", nearest_distance);
            }

            activator.time = std::clamp(activator.time + (activator.active ? dt : -dt), 0.0f, std::max(activator.duration, 0.0f));
        });
    }

    const Activator* find_activator(ecs::Entity e, const ecs::Query<const Activator>& activators, const ecs::Query<const ChildOf>& parents)
    {
        while (e)
        {
            if (const Activator* activator = activators.get(e))
                return activator;
            const ChildOf* child_of = parents.get(e);
            e = child_of ? child_of->parent : ecs::null_entity;
        }
        return nullptr;
    }

    // At rest (the timeline starts from 0): the turn must not take the middle of the mesh towards the character
    float away_sign(const ActivatedMotion& motion, const Transform& world, const MeshRenderer& renderer, glm::vec3 character)
    {
        const AABB& bounds = renderer.mesh.get().bounds;
        const glm::vec3 middle = 0.5f * (bounds.min + bounds.max);
        const glm::quat turn = glm::angleAxis(glm::radians(motion.angle), glm::normalize(motion.axis.glm()));
        const glm::vec3 move = world.rotation.glm() * ((turn * middle - middle) * world.scale.glm());
        const glm::vec3 to_character = character - glm::vec3(world.matrix() * glm::vec4(middle, 1.0f));
        return glm::dot(move, to_character) > 0.0f ? -1.0f : 1.0f;
    }

    // The sky controller and the character interpolations write Transforms too: of the sun and the characters,
    // never of an activated object; spring bones read those of animated characters
    [[=ecs::system<ecs::Phase::Update>, =ecs::after<Activation>, =ecs::ambiguous_with<SkyControl>,
      =ecs::ambiguous_with<CharacterInterpolation>, =ecs::ambiguous_with<loco::LocomotionPresentation>,
      =ecs::ambiguous_with<anim::PostProcess>]]
    void play_activated_motions(ecs::Query<ActivatedMotion, Transform> motions, ecs::Query<const Activator> activators,
                                ecs::Query<const ChildOf> parents, ecs::Query<const WorldTransform> worlds,
                                ecs::Query<const MeshRenderer> renderers)
    {
        motions.each([&] (ecs::Entity e, ActivatedMotion& motion, Transform& transform) {
            const Activator* activator = find_activator(e, activators, parents);
            if (!activator)
                return;
            if (!motion.rest)
                motion.rest = transform;

            if (motion.activation != activator->activations)
            {
                motion.activation = activator->activations;
                const WorldTransform* world = worlds.get(e);
                const MeshRenderer* renderer = renderers.get(e);
                if (motion.away && motion.angle != 0.0f && world && renderer && renderer->mesh.is_valid())
                    motion.sign = away_sign(motion, world->value, *renderer, activator->activated_from);
            }

            const float t = motion.duration > 0.0f
                ? std::clamp((activator->time - motion.start) / motion.duration, 0.0f, 1.0f)
                : (activator->time >= motion.start ? 1.0f : 0.0f);
            const float progress = t * t * (3.0f - 2.0f * t);
            // untouched while it stands still: the inspector may move it
            if (motion.applied && progress == motion.progress)
                return;
            motion.progress = progress;
            motion.applied = true;

            const Transform& rest = *motion.rest;
            transform.position = rest.position.glm() + motion.translation.glm() * progress;
            transform.rotation = rest.rotation.glm()
                * glm::angleAxis(glm::radians(motion.angle * motion.sign * progress), glm::normalize(motion.axis.glm()));
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(Activator, ActivatedMotion)
}
