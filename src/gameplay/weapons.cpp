module;

#include <json/reader.h>
#include <json/value.h>

module gameplay;

import :weapons;
import :health;
import :player;

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

    std::optional<WeaponMode> parse_mode(std::string_view text)
    {
        if (text == "hitscan") return WeaponMode::Hitscan;
        if (text == "projectile") return WeaponMode::Projectile;
        if (text == "cone") return WeaponMode::Cone;
        if (text == "beam") return WeaponMode::Beam;
        if (text == "charge") return WeaponMode::Charge;
        return std::nullopt;
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

    struct ShotContext
    {
        ecs::Entity shooter;
        const WeaponDef& def;
        const phys::PhysicsScene& physics;
        const ecs::EventWriter<DamageEvent>& damage;
        DamageType type;
    };

    std::optional<phys::Hit> shoot_ray(const ShotContext& shot, const glm::vec3& origin, const glm::vec3& direction,
        float amount)
    {
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world | phys::Category::dynamic | phys::Category::hitbox
            | phys::Category::voxel;
        std::optional<phys::Hit> hit = shot.physics.raycast(origin, direction, shot.def.range, filter);
        if (!hit)
            return std::nullopt;
        // walls too: no Health, but a JPEG spot
        shot.damage.send({
            .target = entity_of(*hit),
            .source = shot.shooter,
            .amount = amount,
            .type = shot.type,
            .point = hit->position,
            .direction = direction,
            .jpeg_preset = shot.def.jpeg,
        });
        return hit;
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

    [[=ecs::system<ecs::Phase::FixedPre>]]
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
            command.trigger = input->is_key_down(Key::MouseLeft);

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

    [[=ecs::system<ecs::Phase::FixedPost>, =ecs::in_set<WeaponFire>, =ecs::before<DamageResolution>,
        =ecs::before<scene::PhysicsStep>]]
    void fire_weapons(ecs::Query<WeaponHolder, const WeaponInput> holders, ecs::Query<const Health, const WorldTransform> targets,
        ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::SimTime> time, ecs::EventWriter<DamageEvent> damage,
        ecs::EventWriter<WeaponFiredEvent> fired)
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
            }

            const bool ready = state.reloading <= 0.0f && state.cooldown <= 0.0f && has_round(holder, state, def);
            // shots of one tick spread the same way on every machine
            std::mt19937 rng(uint32_t(time->tick * 2654435761u) ^ shooter.index);
            const ShotContext shot{ shooter, def, *physics, damage, damage_type(def.mode) };
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
                    if (!ready || !(def.automatic ? trigger : pressed))
                        break;
                    const int pellets = def.mode == WeaponMode::Cone ? def.pellets : 1;
                    int hits = 0;
                    for (int i = 0; i < pellets; ++i)
                        hits += shoot_ray(shot, origin, spread_direction(aim, half_angle, rng), def.damage) ? 1 : 0;
                    consume_round(holder, state, def);
                    state.cooldown = def.cooldown;
                    report(hits);
                    break;
                }
                case WeaponMode::Beam:
                {
                    if (!trigger || state.reloading > 0.0f || !has_round(holder, state, def))
                    {
                        // the next press fires at once
                        state.beam_time = def.interval;
                        break;
                    }
                    state.beam_time += dt;
                    while (state.beam_time >= def.interval && has_round(holder, state, def))
                    {
                        state.beam_time -= def.interval;
                        const bool hit = shoot_ray(shot, origin, spread_direction(aim, half_angle, rng),
                            def.damage * def.interval).has_value();
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
                    if (!ready || fraction < def.min_charge)
                        break;

                    std::optional<phys::Hit> hit = shoot_ray(shot, origin, aim, def.damage * fraction);
                    consume_round(holder, state, def);
                    state.cooldown = def.cooldown;
                    report(hit ? 1 : 0);
                    if (!hit || def.radius <= 0.0f)
                        break;

                    // area damage around the impact, falling off to the edge (the entity hit directly is spared)
                    const ecs::Entity direct = entity_of(*hit);
                    targets.each([&] (ecs::Entity e, const Health&, const WorldTransform& transform) {
                        if (e == shooter || e == direct)
                            return;
                        const glm::vec3 position = transform.value.position.glm();
                        const float distance = glm::distance(position, hit->position);
                        if (distance >= def.radius)
                            return;
                        damage.send({
                            .target = e,
                            .source = shooter,
                            .amount = def.damage * fraction * (1.0f - distance / def.radius),
                            .type = DamageType::Explosion,
                            .point = position,
                            .direction = distance > 1e-4f ? (position - hit->position) / distance : aim,
                        });
                    });
                    break;
                }
            }
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

    // the current weapon's model is visible, the others hidden
    [[=ecs::system<ecs::Phase::Update>, =ecs::after<WeaponPresentation>]]
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

    cvar::Command cmd_reload_defs("weapons.reload_definitions", "Reloads assets/weapons/weapons.json",
        [] (cvar::Args) { weapons::reload_definitions(); });

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(WeaponHolder, WeaponInput, HeldWeaponModel)
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
