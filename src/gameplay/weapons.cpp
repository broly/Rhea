module;

#include <json/reader.h>
#include <json/value.h>

module gameplay;

import :weapons;
import :health;
import :player;
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
import input;
import cvar;
import paths;
import locomotion;
import particles;
import name;
import fixed_string;

import log;
#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogWeapons, Display);

namespace
{
    /************************************************************************
     * DEFINITIONS
     ***********************************************************************/

    std::vector<WeaponDef> g_weapons;
    std::vector<std::string> g_loadout;
    bool g_loaded = false;
    std::filesystem::file_time_type g_file_time{};
    float g_reload_check = 0.0f;

    // console requests, taken by the systems
    std::optional<std::string> g_select_request;
    bool g_refill_request = false;
    // automation (weapons::set_scripted_trigger): replaces the left mouse button of the players
    std::optional<bool> g_scripted_trigger;

    // the combat stance of locomotion characters (hold_combat_stance, fire_weapons)
    cvar::Var<float> cv_combat_time("game.weapons.combat_time", 1.5f,
        "Seconds a locomotion character stays aiming after the trigger is let go (then the weapon is ready, lowered after 3 s)");
    cvar::Var<float> cv_raise_time("game.weapons.raise_time", 0.25f,
        "Seconds of aiming before a locomotion character fires (the weapon raised, the spine turned to the view)");
    cvar::Var<float> cv_facing_tolerance("game.weapons.facing_tolerance", 10.0f,
        "Degrees over the aiming yaw limit (the spine's reach) the body may still be turned away from the view and fire");

    std::optional<WeaponMode> parse_mode(std::string_view text)
    {
        if (text == "hitscan") return WeaponMode::Hitscan;
        if (text == "projectile") return WeaponMode::Projectile;
        if (text == "cone") return WeaponMode::Cone;
        if (text == "beam") return WeaponMode::Beam;
        if (text == "charge") return WeaponMode::Charge;
        return std::nullopt;
    }

    std::optional<glm::vec3> parse_vec3(const Json::Value& v)
    {
        if (!v.isArray() || v.size() != 3 || !v[0].isNumeric() || !v[1].isNumeric() || !v[2].isNumeric())
            return std::nullopt;
        return glm::vec3(v[0].asFloat(), v[1].asFloat(), v[2].asFloat());
    }

    // a path or a list of paths
    std::optional<std::vector<std::string>> parse_paths(const Json::Value& v)
    {
        if (v.isString())
            return std::vector<std::string>{ v.asString() };
        if (!v.isArray())
            return std::nullopt;
        std::vector<std::string> paths;
        for (const Json::Value& item : v)
        {
            if (!item.isString())
                return std::nullopt;
            paths.push_back(item.asString());
        }
        return paths;
    }

    WeaponSounds parse_sounds(const std::string& weapon, const Json::Value& value)
    {
        WeaponSounds sounds;
        const std::pair<const char*, std::vector<std::string> WeaponSounds::*> lists[] = {
            { "fire", &WeaponSounds::fire }, { "impact", &WeaponSounds::impact },
            { "impact_creature", &WeaponSounds::impact_creature }, { "reload", &WeaponSounds::reload },
            { "dry_fire", &WeaponSounds::dry_fire }, { "cycle", &WeaponSounds::cycle },
            { "beam_start", &WeaponSounds::beam_start }, { "beam_loop", &WeaponSounds::beam_loop },
            { "beam_stop", &WeaponSounds::beam_stop },
        };
        for (const std::string& key : value.getMemberNames())
        {
            const Json::Value& v = value[key];
            const auto list = std::ranges::find(lists, std::string_view(key), [] (const auto& l) { return std::string_view(l.first); });
            bool ok = true;
            if (list != std::end(lists) && parse_paths(v)) sounds.*list->second = *parse_paths(v);
            else if (key == "cycle_delay" && v.isNumeric()) sounds.cycle_delay = std::max(v.asFloat(), 0.0f);
            else if (key == "volume" && v.isNumeric()) sounds.volume = std::max(v.asFloat(), 0.0f);
            else if (key == "impact_volume" && v.isNumeric()) sounds.impact_volume = std::max(v.asFloat(), 0.0f);
            else ok = false;
            if (!ok)
                LogWeapons.Log("Weapon %s: unknown sound or bad value '%s'", weapon.c_str(), key.c_str());
        }
        return sounds;
    }

    WeaponDef parse_weapon(const std::string& name, const Json::Value& value)
    {
        WeaponDef def;
        def.name = name;
        def.display_name = name;
        for (const std::string& key : value.getMemberNames())
        {
            const Json::Value& v = value[key];
            bool ok = true;
            if (key == "display_name" && v.isString()) def.display_name = v.asString();
            else if (key == "mode" && v.isString() && parse_mode(v.asString())) def.mode = *parse_mode(v.asString());
            else if (key == "damage" && v.isNumeric()) def.damage = v.asFloat();
            else if (key == "range" && v.isNumeric()) def.range = std::max(v.asFloat(), 0.1f);
            else if (key == "cooldown" && v.isNumeric()) def.cooldown = std::max(v.asFloat(), 0.0f);
            else if (key == "auto" && v.isBool()) def.automatic = v.asBool();
            else if (key == "spread" && v.isNumeric()) def.spread = std::clamp(v.asFloat(), 0.0f, 89.0f);
            else if (key == "pellets" && v.isNumeric()) def.pellets = std::clamp(v.asInt(), 1, 64);
            else if (key == "interval" && v.isNumeric()) def.interval = std::max(v.asFloat(), 1.0f / 120.0f);
            else if (key == "charge_time" && v.isNumeric()) def.charge_time = std::max(v.asFloat(), 0.01f);
            else if (key == "min_charge" && v.isNumeric()) def.min_charge = std::clamp(v.asFloat(), 0.0f, 1.0f);
            else if (key == "radius" && v.isNumeric()) def.radius = std::max(v.asFloat(), 0.0f);
            else if (key == "magazine" && v.isNumeric()) def.magazine = std::max(v.asInt(), 0);
            else if (key == "reload" && v.isNumeric()) def.reload = std::max(v.asFloat(), 0.0f);
            else if (key == "ammo" && v.isNumeric()) def.ammo = v.asInt();
            else if (key == "jpeg" && v.isString()) def.jpeg = v.asString();
            else if (key == "model" && v.isString()) def.model = v.asString();
            else if (key == "overlay" && v.isString()) def.overlay = v.asString();
            else if (key == "muzzle" && parse_vec3(v)) def.muzzle = *parse_vec3(v);
            else if (key == "muzzle_forward" && parse_vec3(v) && glm::length(*parse_vec3(v)) > 1e-3f)
                def.muzzle_forward = glm::normalize(*parse_vec3(v));
            else if (key == "muzzle_effect" && v.isString()) def.muzzle_effect = v.asString();
            else if (key == "tracer" && v.isString()) def.tracer = v.asString();
            else if (key == "impact_effect" && v.isString()) def.impact_effect = v.asString();
            else if (key == "projectile" && v.isString()) def.projectile = v.asString();
            else if (key == "projectile_speed" && v.isNumeric()) def.projectile_speed = std::max(v.asFloat(), 0.1f);
            else if (key == "projectile_gravity" && v.isNumeric()) def.projectile_gravity = v.asFloat();
            else if (key == "hit_flash" && v.isNumeric()) def.hit_flash = std::max(v.asFloat(), 0.0f);
            else if (key == "hit_flash_color" && parse_vec3(v)) def.hit_flash_color = *parse_vec3(v);
            else if (key == "hit_flash_time" && v.isNumeric()) def.hit_flash_time = std::max(v.asFloat(), 0.0f);
            else if (key == "body_jpeg_time" && v.isNumeric()) def.body_jpeg_time = std::max(v.asFloat(), 0.0f);
            else if (key == "voxelize" && v.isBool()) def.voxelize = v.asBool();
            else if (key == "sounds" && v.isObject()) def.sounds = parse_sounds(name, v);
            else ok = false;
            if (!ok)
                LogWeapons.Log("Weapon %s: unknown field or bad value '%s'", name.c_str(), key.c_str());
        }
        return def;
    }

    /************************************************************************
     * FIRING
     ***********************************************************************/

    constexpr std::array<Key, 6> slot_keys = { Key::_2, Key::_3, Key::_4, Key::_5, Key::_6, Key::_7 };

    ecs::Entity entity_of(const phys::Hit& hit)
    {
        // framework convention: BodyDesc::user_data is the owning entity (ecs::Entity::bits), or 0
        return hit.user_data != 0 ? ecs::Entity{ uint32_t(hit.user_data), uint32_t(hit.user_data >> 32) } : ecs::null_entity;
    }

    // a direction within half_angle (radians) of `direction`, uniform over the cone's cap
    glm::vec3 spread_direction(const glm::vec3& direction, float half_angle, std::mt19937& rng)
    {
        if (half_angle <= 0.0f)
            return direction;
        std::uniform_real_distribution<float> unit(0.0f, 1.0f);
        const float cos_theta = glm::mix(1.0f, std::cos(half_angle), unit(rng));
        const float sin_theta = std::sqrt(std::max(1.0f - cos_theta * cos_theta, 0.0f));
        const float phi = unit(rng) * 6.28318531f;
        const glm::vec3 up = std::abs(direction.y) < 0.99f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
        const glm::vec3 right = glm::normalize(glm::cross(direction, up));
        const glm::vec3 side = glm::cross(right, direction);
        return glm::normalize(direction * cos_theta + (right * std::cos(phi) + side * std::sin(phi)) * sin_theta);
    }

    // what shots hit: the world and the creatures (hitboxes), not the characters' capsules
    phys::QueryFilter shot_filter()
    {
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world | phys::Category::dynamic | phys::Category::hitbox
            | phys::Category::voxel;
        return filter;
    }

    // the rotation turning +z (the axis effects are authored along) to `direction`
    glm::quat rotation_to(const glm::vec3& direction)
    {
        const float length = glm::length(direction);
        if (length < 1e-6f)
            return glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
        const glm::vec3 to = direction / length;
        const float d = to.z;
        if (d < -0.9999f)
            return glm::angleAxis(3.14159265f, glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::vec3 c = glm::cross(glm::vec3(0.0f, 0.0f, 1.0f), to);
        return glm::normalize(glm::quat(1.0f + d, c.x, c.y, c.z));
    }

    // the end of the barrel of the model in the shooter's hand, world
    struct Muzzle
    {
        ecs::Entity model;
        glm::vec3 position;
        glm::vec3 forward;
    };

    struct ShotContext
    {
        ecs::Entity shooter;
        const WeaponDef& def;
        const phys::PhysicsScene& physics;
        const ecs::EventWriter<DamageEvent>& damage;
        const ecs::EventWriter<SpawnPrefabRequest>& spawns;
        const ecs::EventWriter<WeaponImpactEvent>& impacts;
        const ecs::Query<const Health, const WorldTransform>& targets;
        DamageType type;
        glm::vec3 camera;               // where the aim rays start (the crosshair)
        std::optional<Muzzle> muzzle;
    };

    glm::vec3 flash_of(const WeaponDef& def, float scale)
    {
        return def.hit_flash_color * def.hit_flash * std::max(scale, 0.0f);
    }

    // a one shot effect in the world, its +z along `direction`; length > 0: its spawn shapes stretched to it
    void spawn_effect(const ShotContext& shot, const std::string& prefab, const glm::vec3& position, const glm::vec3& direction,
        float length = 0.0f)
    {
        if (prefab.empty())
            return;
        Transform placement;
        placement.position = position;
        placement.rotation = rotation_to(direction);
        std::function<void(ecs::Registry&, ecs::Entity)> stretch;
        if (length > 0.0f)
            stretch = [length] (ecs::Registry& registry, ecs::Entity e) {
                if (ParticleSystem* particles = registry.get<ParticleSystem>(e))
                    particles->shape_scale = vec3(1.0f, 1.0f, length);
            };
        shot.spawns.send({ .prefab = prefab, .placement = placement, .on_spawned = std::move(stretch) });
    }

    // the flash at the muzzle: a child of the model, it moves with the weapon
    void spawn_muzzle_effect(const ShotContext& shot)
    {
        if (!shot.muzzle || shot.def.muzzle_effect.empty())
            return;
        Transform placement;
        placement.position = shot.def.muzzle;
        placement.rotation = rotation_to(shot.def.muzzle_forward);
        shot.spawns.send({ .prefab = shot.def.muzzle_effect, .placement = placement, .parent = shot.muzzle->model });
    }

    // damage around `point` falling off to the edge (`spared`: the one hit directly, the shooter)
    void area_damage(const ShotContext& shot, const glm::vec3& point, float amount, ecs::Entity spared, float scale)
    {
        const float radius = shot.def.radius;
        if (radius <= 0.0f)
            return;
        shot.targets.each([&] (ecs::Entity e, const Health&, const WorldTransform& transform) {
            if (e == shot.shooter || e == spared)
                return;
            const glm::vec3 position = transform.value.position.glm();
            const float distance = glm::distance(position, point);
            if (distance >= radius)
                return;
            const float falloff = 1.0f - distance / radius;
            shot.damage.send({
                .target = e,
                .source = shot.shooter,
                .amount = amount * falloff,
                .type = DamageType::Explosion,
                .point = position,
                .direction = distance > 1e-4f ? (position - point) / distance : glm::vec3(0.0f, 1.0f, 0.0f),
                .flash = flash_of(shot.def, scale * falloff),
                .flash_time = shot.def.hit_flash_time,
            });
        });
    }

    // a blow where a ray or a projectile meets a surface: damage (walls too: the JPEG spot), the flash of the
    // entity, the impact effect, the area damage
    void hit_at(const ShotContext& shot, const phys::Hit& hit, const glm::vec3& direction, float amount, float scale)
    {
        const ecs::Entity target = entity_of(hit);
        shot.damage.send({
            .target = target,
            .source = shot.shooter,
            .amount = amount,
            .type = shot.type,
            .point = hit.position,
            .direction = direction,
            .jpeg_preset = shot.def.jpeg,
            .flash = flash_of(shot.def, scale),
            .flash_time = shot.def.hit_flash_time,
            .body_jpeg_time = shot.def.body_jpeg_time,
        });
        spawn_effect(shot, shot.def.impact_effect, hit.position + hit.normal * 0.02f, hit.normal);
        shot.impacts.send({ .shooter = shot.shooter, .weapon = shot.def.name, .position = hit.position, .normal = hit.normal,
            .creature = hit.category == phys::Category::hitbox });
        area_damage(shot, hit.position, amount, target, scale);
    }

    // one shot along `direction` from the camera (under the crosshair): a ray hitting at once (with a tracer from
    // the muzzle), or a projectile flying from the muzzle to the point the ray finds. true: the ray hit
    bool discharge(const ShotContext& shot, const glm::vec3& direction, float amount, float scale)
    {
        const std::optional<phys::Hit> hit = shot.physics.raycast(shot.camera, direction, shot.def.range, shot_filter());
        const glm::vec3 aim_point = hit ? hit->position : shot.camera + direction * shot.def.range;
        const glm::vec3 start = shot.muzzle ? shot.muzzle->position : shot.camera;
        const glm::vec3 to_aim = aim_point - start;
        const float distance = glm::length(to_aim);
        const glm::vec3 from_muzzle = distance > 1e-3f ? to_aim / distance : direction;

        if (!shot.def.projectile.empty())
        {
            Transform placement;
            placement.position = start;
            placement.rotation = rotation_to(from_muzzle);
            const Projectile projectile{
                .shooter = shot.shooter,
                .weapon = shot.def.name,
                .velocity = from_muzzle * shot.def.projectile_speed,
                .damage = amount,
                .range_left = shot.def.range,
                .charge = scale,
            };
            shot.spawns.send({
                .prefab = shot.def.projectile,
                .placement = placement,
                .on_spawned = [projectile] (ecs::Registry& registry, ecs::Entity e) { registry.add<Projectile>(e, projectile); },
            });
            return false;
        }

        if (shot.muzzle && distance > 0.05f)
            spawn_effect(shot, shot.def.tracer, start, from_muzzle, distance);
        if (!hit)
            return false;
        hit_at(shot, *hit, direction, amount, scale);
        return true;
    }

    DamageType damage_type(WeaponMode mode)
    {
        switch (mode)
        {
            case WeaponMode::Projectile: return DamageType::Projectile;
            case WeaponMode::Beam: return DamageType::Beam;
            case WeaponMode::Charge: return DamageType::Explosion;
            default: return DamageType::Hitscan;
        }
    }

    void init_state(WeaponHolder::State& state, const WeaponDef& def)
    {
        if (state.initialized)
            return;
        state.initialized = true;
        if (def.magazine > 0)
        {
            const int loaded = def.ammo < 0 ? def.magazine : std::min(def.magazine, def.ammo);
            state.magazine = loaded;
            state.reserve = def.ammo < 0 ? -1 : def.ammo - loaded;
        }
        else
        {
            state.reserve = def.ammo;
        }
    }

    bool has_round(const WeaponHolder& holder, const WeaponHolder::State& state, const WeaponDef& def)
    {
        if (holder.infinite_ammo)
            return true;
        return def.magazine > 0 ? state.magazine > 0 : state.reserve != 0;
    }

    void consume_round(const WeaponHolder& holder, WeaponHolder::State& state, const WeaponDef& def)
    {
        if (holder.infinite_ammo)
            return;
        if (def.magazine > 0)
            state.magazine = std::max(state.magazine - 1, 0);
        else if (state.reserve > 0)
            --state.reserve;
    }

    std::string ammo_text(const WeaponHolder::State& state, const WeaponDef& def)
    {
        const std::string reserve = state.reserve < 0 ? "inf" : std::to_string(state.reserve);
        return def.magazine > 0 ? std::format("{}/{} + {}", state.magazine, def.magazine, reserve) : reserve;
    }

    /************************************************************************
     * SYSTEMS
     ***********************************************************************/

    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<WeaponInputSet>]]
    void read_weapon_input(ecs::Query<WeaponInput, const Player> players, ecs::Res<Input> input,
        ecs::Query<const Camera, const WorldTransform> cameras, ecs::Res<ecs::SimTime> time)
    {
        // hot reload of the definitions
        g_reload_check += float(time->dt);
        if (g_reload_check >= 0.5f)
        {
            g_reload_check = 0.0f;
            std::error_code error;
            const auto file_time = std::filesystem::last_write_time(weapons::get_definitions_path(), error);
            if (!error && g_loaded && file_time != g_file_time)
                weapons::reload_definitions();
        }

        // the aim: the middle of the active camera's view
        bool has_aim = false;
        glm::vec3 origin{ 0.0f };
        glm::vec3 direction{ 0.0f, 0.0f, -1.0f };
        cameras.each([&] (const Camera& camera, const WorldTransform& transform) {
            if (!camera.active || has_aim)
                return;
            has_aim = true;
            origin = transform.value.position.glm();
            direction = glm::normalize(transform.value.rotation.glm() * glm::vec3(0.0f, 0.0f, -1.0f));
        });

        players.each([&] (WeaponInput& command, const Player&) {
            command.trigger = g_scripted_trigger ? *g_scripted_trigger : input->is_key_down(Key::MouseLeft);

            // X: R is the shader hot reload
            const bool reload_down = input->is_key_down(Key::X);
            command.reload = reload_down && !command.reload_was_down;
            command.reload_was_down = reload_down;

            int slot_down = -1;
            for (size_t i = 0; i < slot_keys.size(); ++i)
                if (input->is_key_down(slot_keys[i]))
                    slot_down = int(i);
            command.select = slot_down != command.select_was_down ? slot_down : -1;
            command.select_was_down = slot_down;

            command.has_aim = has_aim;
            command.aim_origin = origin;
            command.aim_direction = direction;
        });
    }

    // A shot from the hip would leave the muzzle wherever the relaxed pose points it: the trigger raises the weapon
    // first. A locomotion character with the trigger down (and combat_time s after) aims like with the right mouse
    // button: ALS turns the body to the view and raises the weapon; sprinting stops. The camera does not zoom (that
    // stays the right mouse button).
    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::after<loco::LocomotionInputSet>, =ecs::after<WeaponInputSet>]]
    void hold_combat_stance(ecs::Query<WeaponHolder, const WeaponInput, loco::LocomotionInput> holders, ecs::Res<ecs::SimTime> time)
    {
        const float dt = float(time->dt);
        holders.each([&] (WeaponHolder& holder, const WeaponInput& command, loco::LocomotionInput& input) {
            if (holder.enabled && command.trigger && command.has_aim && weapons::find(holder.current))
                holder.combat = cv_combat_time.get();
            else
                holder.combat = std::max(holder.combat - dt, 0.0f);
            if (holder.combat <= 0.0f && holder.queued_press <= 0.0f)
                return;
            input.aim = true;
            input.sprint = false;
        });
    }

    // the weapon of a locomotion character points where the shots go: aiming for raise_time s, the body turned to the
    // view as far as the spine reaches, no roll or mantle
    bool is_weapon_raised(WeaponHolder& holder, const loco::LocomotionCharacter* c, float dt)
    {
        if (!c)
            return true;
        const bool aiming = c->rotation_mode == loco::RotationMode::aiming && c->action == loco::LocomotionAction::none;
        holder.raised = aiming ? holder.raised + dt : 0.0f;
        const float limit = c->settings ? c->settings->aiming_yaw_angle_limit : 70.0f;
        const bool facing = std::abs(loco::unwind_degrees(c->view.yaw - c->yaw)) <= limit + cv_facing_tolerance.get();
        return aiming && facing && holder.raised >= cv_raise_time.get();
    }

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<WeaponFire>, =ecs::before<DamageResolution>,
        =ecs::before<scene::PhysicsStep>]]
    void fire_weapons(ecs::Query<WeaponHolder, const WeaponInput> holders, ecs::Query<const Health, const WorldTransform> targets,
        ecs::Query<const loco::LocomotionCharacter> characters,
        ecs::Query<const HeldWeaponModel, const ChildOf, const WorldTransform> models,
        ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time, ecs::EventWriter<DamageEvent> damage,
        ecs::EventWriter<WeaponFiredEvent> fired, ecs::EventWriter<SpawnPrefabRequest> spawns,
        ecs::EventWriter<WeaponImpactEvent> impacts, ecs::EventWriter<WeaponActionEvent> actions)
    {
        const float dt = float(time->dt);
        const std::vector<std::string>& loadout = weapons::get_loadout();
        const std::optional<std::string> select_request = std::exchange(g_select_request, std::nullopt);
        const bool refill = std::exchange(g_refill_request, false);

        holders.each([&] (ecs::Entity shooter, WeaponHolder& holder, const WeaponInput& command) {
            for (auto& [name, state] : holder.state)
            {
                state.cooldown = std::max(state.cooldown - dt, 0.0f);
                if (refill)
                    state.initialized = false;
            }

            // selection: slot keys, the console
            std::optional<std::string> selected = select_request;
            if (command.select >= 0 && size_t(command.select) < loadout.size())
                selected = loadout[size_t(command.select)];
            if (selected && *selected != holder.current && weapons::find(*selected))
            {
                if (auto it = holder.state.find(holder.current); it != holder.state.end())
                {
                    it->second.charge = 0.0f;
                    it->second.reloading = 0.0f;
                }
                holder.current = *selected;
                holder.queued_press = 0.0f;
                const WeaponDef& def = *weapons::find(holder.current);
                WeaponHolder::State& state = holder.state[def.name];
                init_state(state, def);
                LogWeapons.Log("Weapon: %s (%s)", def.display_name.c_str(), ammo_text(state, def).c_str());
            }

            const WeaponDef* def_ptr = weapons::find(holder.current);
            if (!def_ptr)
                return;
            const WeaponDef& def = *def_ptr;
            WeaponHolder::State& state = holder.state[def.name];
            init_state(state, def);

            const bool trigger = holder.enabled && command.trigger && command.has_aim;
            const bool pressed = trigger && !state.trigger_was_down;
            const bool released = !trigger && state.trigger_was_down;
            state.trigger_was_down = trigger;

            // reload: X, or the trigger on an empty magazine
            if (state.reloading > 0.0f)
            {
                state.reloading -= dt;
                if (state.reloading <= 0.0f)
                {
                    state.reloading = 0.0f;
                    const int wanted = def.magazine - state.magazine;
                    const int taken = state.reserve < 0 ? wanted : std::min(wanted, state.reserve);
                    state.magazine += taken;
                    if (state.reserve >= 0)
                        state.reserve -= taken;
                    LogWeapons.Log("Reloaded %s (%s)", def.display_name.c_str(), ammo_text(state, def).c_str());
                }
            }
            else if (def.magazine > 0 && state.reserve != 0 && !holder.infinite_ammo
                && ((command.reload && state.magazine < def.magazine) || (pressed && state.magazine == 0)))
            {
                state.reloading = def.reload;
                state.charge = 0.0f;
                actions.send({ .shooter = shooter, .weapon = def.name, .action = WeaponAction::Reload });
            }
            else if (pressed && !has_round(holder, state, def))
                actions.send({ .shooter = shooter, .weapon = def.name, .action = WeaponAction::DryFire });

            // the weapon raised first: a click shorter than the raise fires once it is up
            const bool raised = is_weapon_raised(holder, characters.get(shooter), dt);
            bool fire_press = false;
            if (raised)
            {
                fire_press = pressed || holder.queued_press > 0.0f;
                holder.queued_press = 0.0f;
            }
            else if (pressed && state.reloading <= 0.0f && has_round(holder, state, def))
                holder.queued_press = cv_combat_time.get();
            else
                holder.queued_press = std::max(holder.queued_press - dt, 0.0f);

            const bool ready = state.reloading <= 0.0f && state.cooldown <= 0.0f && has_round(holder, state, def);
            // shots of one tick spread the same way on every machine
            std::mt19937 rng(uint32_t(time->tick * 2654435761u) ^ shooter.index);
            std::optional<Muzzle> muzzle;
            models.each([&] (ecs::Entity model, const HeldWeaponModel& held, const ChildOf& parent, const WorldTransform& world) {
                if (parent.parent != shooter || held.weapon != def.name)
                    return;
                const glm::mat4 matrix = world.value.matrix();
                muzzle = Muzzle{ model, glm::vec3(matrix * glm::vec4(def.muzzle, 1.0f)),
                    glm::normalize(glm::mat3(matrix) * def.muzzle_forward) };
            });
            const ShotContext shot{ shooter, def, *physics, damage, spawns, impacts, targets, damage_type(def.mode), command.aim_origin, muzzle };
            const glm::vec3 origin = command.aim_origin;
            const glm::vec3 aim = command.aim_direction;
            const float half_angle = glm::radians(def.spread);

            auto report = [&] (int hits) {
                fired.send({ .shooter = shooter, .weapon = def.name, .origin = origin, .direction = aim, .hits = hits });
            };

            switch (def.mode)
            {
                case WeaponMode::Hitscan:
                case WeaponMode::Projectile:
                case WeaponMode::Cone:
                {
                    if (!ready || !raised || !(fire_press || (def.automatic && trigger)))
                        break;
                    const int pellets = def.mode == WeaponMode::Cone ? def.pellets : 1;
                    int hits = 0;
                    spawn_muzzle_effect(shot);
                    for (int i = 0; i < pellets; ++i)
                        hits += discharge(shot, spread_direction(aim, half_angle, rng), def.damage, 1.0f) ? 1 : 0;
                    consume_round(holder, state, def);
                    state.cooldown = def.cooldown;
                    report(hits);
                    break;
                }
                case WeaponMode::Beam:
                {
                    if (!trigger || !raised || state.reloading > 0.0f || !has_round(holder, state, def))
                    {
                        // the next press (or the raised weapon) fires at once
                        state.beam_time = def.interval;
                        break;
                    }
                    state.beam_time += dt;
                    while (state.beam_time >= def.interval && has_round(holder, state, def))
                    {
                        state.beam_time -= def.interval;
                        spawn_muzzle_effect(shot);
                        const bool hit = discharge(shot, spread_direction(aim, half_angle, rng), def.damage * def.interval, 1.0f);
                        consume_round(holder, state, def);
                        report(hit ? 1 : 0);
                    }
                    break;
                }
                case WeaponMode::Charge:
                {
                    if (trigger && ready)
                        state.charge = std::min(state.charge + dt, def.charge_time);
                    if (!released)
                        break;
                    const float fraction = state.charge / def.charge_time;
                    state.charge = 0.0f;
                    // charged while the weapon was raised; let go before it is up: nothing
                    if (!ready || !raised || fraction < def.min_charge)
                        break;

                    // the area damage (radius) happens in hit_at, where the ray or the projectile lands
                    spawn_muzzle_effect(shot);
                    const bool hit = discharge(shot, aim, def.damage * fraction, fraction);
                    consume_round(holder, state, def);
                    state.cooldown = def.cooldown;
                    report(hit ? 1 : 0);
                    break;
                }
            }
        });
    }

    // shots in flight: a ray over the step of the tick, the blow of their weapon where it meets a surface
    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<WeaponFire>, =ecs::before<DamageResolution>,
        =ecs::before<scene::PhysicsStep>]]
    void move_projectiles(ecs::Query<Projectile, Transform> projectiles, ecs::Query<const Health, const WorldTransform> targets,
        ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time, ecs::EventWriter<DamageEvent> damage,
        ecs::EventWriter<SpawnPrefabRequest> spawns, ecs::EventWriter<DespawnRequest> despawns,
        ecs::EventWriter<WeaponImpactEvent> impacts)
    {
        const float dt = float(time->dt);
        projectiles.each([&] (ecs::Entity e, Projectile& projectile, Transform& transform) {
            if (projectile.range_left <= 0.0f)
                return;     // despawning
            const WeaponDef* def = weapons::find(projectile.weapon);
            if (!def)
            {
                projectile.range_left = 0.0f;
                despawns.send({ e });
                return;
            }
            projectile.velocity.y -= def->projectile_gravity * dt;
            const glm::vec3 position = transform.position.glm();
            const glm::vec3 step = projectile.velocity * dt;
            const float length = glm::length(step);
            if (length > 1e-5f)
            {
                const glm::vec3 direction = step / length;
                if (std::optional<phys::Hit> hit = physics->raycast(position, direction, length, shot_filter()))
                {
                    const ShotContext shot{ projectile.shooter, *def, *physics, damage, spawns, impacts, targets,
                        damage_type(def->mode), position, std::nullopt };
                    hit_at(shot, *hit, direction, projectile.damage, projectile.charge);
                    projectile.range_left = 0.0f;
                    despawns.send({ e });
                    return;
                }
                transform.rotation = rotation_to(direction);
            }
            transform.position = position + step;
            projectile.range_left -= length;
            if (projectile.range_left <= 0.0f)
                despawns.send({ e });
        });
    }

    // the ALS overlay of the current weapon (Rifle, PistolTwoHanded), the character's own one without
    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<WeaponPresentation>, =ecs::before<loco::LocomotionPresentation>]]
    void select_weapon_overlay(ecs::Query<loco::LocomotionAnimation, WeaponHolder> characters)
    {
        characters.each([&] (loco::LocomotionAnimation& animation, WeaponHolder& holder) {
            if (holder.base_overlay < 0)
                holder.base_overlay = animation.overlay;
            int32_t overlay = holder.base_overlay;
            if (const WeaponDef* def = weapons::find(holder.current); def && !def->overlay.empty())
            {
                const int32_t index = loco::find_overlay(animation, def->overlay);
                if (index >= 0)
                    overlay = index;
            }
            animation.overlay = overlay;
        });
    }

    // the current weapon's model is visible, the others hidden: in Late, before the render proxies are synced (in
    // Update it would race the systems that read MeshRenderer there)
    [[=ecs::system<ecs::Phase::Late>, =ecs::after<HitFlashes>, =ecs::after<JpegInfections>, =ecs::before<RenderSync>,
        =ecs::before<MeshColliderSync>]]
    void show_held_weapon(ecs::Query<MeshRenderer, const HeldWeaponModel, const ChildOf> models,
        ecs::Query<const WeaponHolder> holders)
    {
        models.each([&] (MeshRenderer& renderer, const HeldWeaponModel& model, const ChildOf& parent) {
            const WeaponHolder* holder = holders.get(parent.parent);
            renderer.visible = holder && holder->current == model.weapon;
        });
    }

    /************************************************************************
     * CONSOLE
     ***********************************************************************/

    std::vector<std::string> weapon_names()
    {
        std::vector<std::string> names;
        for (const WeaponDef& def : weapons::get_all())
            names.push_back(def.name);
        return names;
    }

    cvar::Command cmd_select("weapons.select", "Gives the player a weapon of assets/weapons/weapons.json",
        [] (cvar::Args args)
        {
            if (args.empty() || !weapons::find(args[0]))
            {
                cvar::print("weapons.select <name>: one of the weapons.list", cvar::Output::error);
                return;
            }
            g_select_request = args[0];
        },
        "<weapon>",
        [] (size_t arg_index) { return arg_index == 0 ? weapon_names() : std::vector<std::string>{}; });

    cvar::Command cmd_refill("weapons.refill", "Refills the magazines and the ammo of every weapon",
        [] (cvar::Args) { g_refill_request = true; });

    cvar::Command cmd_list("weapons.list", "Prints the weapons (assets/weapons/weapons.json)",
        [] (cvar::Args)
        {
            const auto& loadout = weapons::get_loadout();
            for (const WeaponDef& def : weapons::get_all())
            {
                const auto slot = std::ranges::find(loadout, def.name);
                cvar::print(std::format("{}{}: {} damage {}, cooldown {} s, range {} m, magazine {}, ammo {}, jpeg {}",
                    slot != loadout.end() ? std::format("[{}] ", 2 + (slot - loadout.begin())) : std::string(),
                    def.name, def.display_name, def.damage, def.cooldown, def.range, def.magazine, def.ammo, def.jpeg));
            }
        });

    cvar::Command cmd_trigger("weapons.trigger", "Holds the players' trigger down (on), up (off) or gives it back to the mouse (auto)",
        [] (cvar::Args args)
        {
            if (args.empty() || (args[0] != "on" && args[0] != "off" && args[0] != "auto"))
            {
                cvar::print("weapons.trigger on|off|auto", cvar::Output::error);
                return;
            }
            weapons::set_scripted_trigger(args[0] == "auto" ? std::nullopt : std::optional<bool>(args[0] == "on"));
        },
        "on|off|auto");

    cvar::Command cmd_reload_defs("weapons.reload_definitions", "Reloads assets/weapons/weapons.json",
        [] (cvar::Args) { weapons::reload_definitions(); });

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(WeaponHolder, WeaponInput, HeldWeaponModel, Projectile)
}

/************************************************************************
 * DEFINITIONS
 ***********************************************************************/

void weapons::spawn_held_weapon_models(World& world, ecs::Entity holder)
{
    for (const WeaponDef& def : get_all())
    {
        if (def.model.empty())
            continue;
        const ecs::Entity model = world.spawn_prefab(def.model, Name("held_weapon_" + def.name));
        if (!model)
        {
            LogWeapons.Log("Weapon %s: could not spawn its model %s", def.name.c_str(), def.model.c_str());
            continue;
        }
        world.registry.add<ChildOf>(model, ChildOf{ holder });
        world.registry.add<HeldWeaponModel>(model, HeldWeaponModel{ def.name });
    }
}

std::filesystem::path weapons::get_definitions_path()
{
    return paths::get_assets_path() / "weapons" / "weapons.json";
}

bool weapons::reload_definitions()
{
    g_loaded = true;
    const std::filesystem::path path = get_definitions_path();
    std::error_code error;
    g_file_time = std::filesystem::last_write_time(path, error);

    std::ifstream file(path);
    Json::Value root;
    Json::CharReaderBuilder builder;
    std::string errors;
    if (!file || !Json::parseFromStream(builder, file, &root, &errors) || !root.isObject())
    {
        LogWeapons.Log("Could not read %s: %s (the weapons stay as they were)", path.string().c_str(), errors.c_str());
        return false;
    }

    std::vector<WeaponDef> definitions;
    const Json::Value& list = root["weapons"];
    if (list.isObject())
        for (const std::string& name : list.getMemberNames())
            definitions.push_back(parse_weapon(name, list[name]));

    std::vector<std::string> loadout;
    for (const Json::Value& name : root["loadout"])
    {
        if (name.isString() && std::ranges::any_of(definitions, [&] (const WeaponDef& d) { return d.name == name.asString(); }))
            loadout.push_back(name.asString());
        else
            LogWeapons.Log("Loadout: no weapon '%s'", name.isString() ? name.asString().c_str() : "?");
    }

    g_weapons = std::move(definitions);
    g_loadout = std::move(loadout);
    LogWeapons.Log("Loaded %zu weapons from %s", g_weapons.size(), path.string().c_str());
    return true;
}

const std::vector<WeaponDef>& weapons::get_all()
{
    if (!g_loaded)
        reload_definitions();
    return g_weapons;
}

const std::vector<std::string>& weapons::get_loadout()
{
    if (!g_loaded)
        reload_definitions();
    return g_loadout;
}

const WeaponDef* weapons::find(std::string_view name)
{
    for (const WeaponDef& def : get_all())
        if (def.name == name)
            return &def;
    return nullptr;
}

void weapons::set_scripted_trigger(std::optional<bool> trigger)
{
    g_scripted_trigger = trigger;
}
