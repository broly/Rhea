module;

#include <json/value.h>

export module gameplay:weapons;

import std.compat;
import glm;
import reflect;
import rhobject;
import ecs;
import framework;
import :health;

// Weapons (G4). Definitions in assets/weapons/weapons.json (reloaded when the file changes):
//
//     "loadout": [ "squeezer", "blaster", ... ],          // keys 2, 3, ... select them
//     "weapons": { "<name>": { field: value } }
//
//   mode         "hitscan" | "projectile" | "cone" | "beam" | "charge"
//                  hitscan     one ray per shot
//                  projectile  for now a hitscan (real projectiles: G5)
//                  cone        `pellets` rays spread over `spread` degrees
//                  beam        while the trigger is held: a ray every `interval` s, `damage` per second
//                  charge      hold to charge up to `charge_time` s, release fires: damage x charge, and
//                              area damage within `radius` m of the impact (falls off to the edge)
//   damage, range (m), cooldown (s between shots), auto (hold to keep firing), spread (degrees, half angle),
//   pellets, interval, charge_time, min_charge (share of charge_time that fires), radius,
//   magazine (rounds, 0: no magazine), reload (s), ammo (reserve at start, < 0: infinite),
//   jpeg (hit preset of assets/jpeg/presets.json for the spots), display_name,
//   model (prefab of the model in the hand: MeshRenderer + BoneAttachment; spawn_held_weapon_models),
//   overlay (ALS overlay of the character while the weapon is out: "Rifle", "PistolTwoHanded"; empty: its own)
//
// Systems:
//   read_weapon_input (FixedPre)                    trigger (left mouse), reload (X), slot keys (2..7) and the aim
//                                                   ray of the active camera
//                                                   (the middle of the view) into WeaponInput of the Player
//   fire_weapons      (FixedPost, set WeaponFire)   cooldowns, magazines, rays -> DamageEvent (before
//                                                   DamageResolution and the physics step), WeaponFiredEvent
export
{
    enum class WeaponMode : uint8_t
    {
        Hitscan,
        Projectile,
        Cone,
        Beam,
        Charge,
    };

    struct WeaponDef
    {
        std::string name;
        std::string display_name;
        WeaponMode mode = WeaponMode::Hitscan;
        float damage = 10.0f;
        float range = 200.0f;
        float cooldown = 0.25f;
        bool automatic = false;
        float spread = 0.0f;            // degrees
        int pellets = 1;
        float interval = 0.1f;          // beam
        float charge_time = 1.0f;       // charge
        float min_charge = 0.2f;
        float radius = 0.0f;            // area damage of the charge
        int magazine = 0;
        float reload = 1.0f;
        int ammo = -1;
        std::string jpeg = "default";
        std::string model;
        std::string overlay;
    };

    // System set of the Update systems that show the current weapon (model, overlay of the locomotion animation)
    struct WeaponPresentation {};

    // A weapon model held by its parent (ChildOf the WeaponHolder): shown while that weapon is the current one
    struct [[=scene::runtime_only]] HeldWeaponModel
    {
        [[=rh::edit, =rh::read_only]] std::string weapon;
    };

    // System set of fire_weapons
    struct WeaponFire {};

    // The weapons an entity carries (the player). Runtime state per weapon name in `state`.
    struct WeaponHolder
    {
        struct State
        {
            bool initialized = false;
            int magazine = 0;           // rounds loaded
            int reserve = 0;            // rounds left besides the magazine (< 0: infinite)
            float cooldown = 0.0f;      // s until the next shot
            float reloading = 0.0f;     // s left, 0: not reloading
            float charge = 0.0f;        // s held (charge weapons)
            float beam_time = 0.0f;     // s since the last beam ray
            bool trigger_was_down = false;
        };

        [[=rh::edit]] bool enabled = true;
        [[=rh::edit]] std::string current = "squeezer";
        [[=rh::edit]] bool infinite_ammo = false;
        [[=rh::transient]] std::map<std::string, State> state;
        // the overlay the character had before a weapon set its own (-1: not seen yet)
        [[=rh::transient]] int32_t base_overlay = -1;
    };

    // The command of a tick for the WeaponHolder (filled from the keyboard and mouse for the Player)
    struct [[=scene::runtime_only]] WeaponInput
    {
        [[=rh::edit, =rh::read_only]] bool trigger = false;     // held
        [[=rh::edit, =rh::read_only]] bool reload = false;      // pressed this tick
        int select = -1;                                         // loadout index pressed this tick
        bool has_aim = false;
        glm::vec3 aim_origin{ 0.0f };
        glm::vec3 aim_direction{ 0.0f, 0.0f, -1.0f };
        bool reload_was_down = false;
        int select_was_down = -1;
    };

    struct WeaponFiredEvent
    {
        ecs::Entity shooter;
        std::string weapon;
        glm::vec3 origin{ 0.0f };
        glm::vec3 direction{ 0.0f };
        int hits = 0;
    };

    namespace weapons
    {
        // the models of every weapon with one, as children of holder (hidden but the current one)
        void spawn_held_weapon_models(World& world, ecs::Entity holder);

        const WeaponDef* find(std::string_view name);
        const std::vector<WeaponDef>& get_all();
        const std::vector<std::string>& get_loadout();
        bool reload_definitions();
        std::filesystem::path get_definitions_path();
    }
}
