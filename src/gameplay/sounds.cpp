module gameplay;

import :sounds;
import :weapons;
import :player;

import std.compat;
import glm;
import rhmath;
import ecs;
import framework;
import physics;
import rhcomponents;
import animation;
import locomotion;
import terrain;
import sky_controller;
import audio;
import cvar;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    cvar::Var<float> cv_footsteps_volume("audio.footsteps.volume", 0.7f, "Volume of the footsteps",
        {.has_range = true, .min = 0.0f, .max = 2.0f});
    cvar::Var<bool> cv_footsteps_debug("audio.footsteps.debug", false, "Prints every footstep (surface, type, weight)");

    // sets: the three systems below write the AudioEngine, this is their order
    struct WeaponAudio {};
    struct FootstepAudio {};
    struct AmbienceAudio {};

    std::minstd_rand g_rng(0x50d5);

    float random_range(float min, float max)
    {
        return std::uniform_real_distribution<float>(min, std::max(min, max))(g_rng);
    }

    const std::string* pick(const std::vector<std::string>& list)
    {
        if (list.empty())
            return nullptr;
        return &list[std::uniform_int_distribution<size_t>(0, list.size() - 1)(g_rng)];
    }

    audio::Spatial world_at(const glm::vec3& position, float min_distance, float max_distance)
    {
        audio::Spatial spatial;
        spatial.position = position;
        spatial.min_distance = min_distance;
        spatial.max_distance = max_distance;
        return spatial;
    }

    // a sound in the world as the listener hears it through the level: the volume and the low-pass of its play
    audio::PlayParams muffled(const audio::AudioEngine& audio, const phys::PhysicsScene& physics, audio::PlayParams params)
    {
        if (!params.spatial)
            return params;
        const sound_occlusion::Muffle muffle = sound_occlusion::muffle(
            sound_occlusion::trace(physics, audio.get_listener().position, params.spatial->position));
        params.volume *= muffle.volume;
        params.lowpass = muffle.lowpass;
        return params;
    }

    ecs::Entity entity_of(const phys::Hit& hit)
    {
        // framework convention: BodyDesc::user_data is the owning entity (ecs::Entity::bits), or 0
        return hit.user_data != 0 ? ecs::Entity{ uint32_t(hit.user_data), uint32_t(hit.user_data >> 32) } : ecs::null_entity;
    }

    /************************************************************************
     * WEAPONS
     ***********************************************************************/

    // a beam firing: its loop plays until no ray came for a while
    struct BeamSound
    {
        std::string weapon;
        audio::VoiceId loop;
        double last_ray = 0.0;
        std::optional<audio::Spatial> spatial;
    };
    std::unordered_map<uint64_t, BeamSound> g_beams;      // by shooter (ecs::Entity::bits)

    constexpr int max_impacts_per_frame = 3;             // per weapon: the pellets of a cone hit together

    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<WeaponAudio>]]
    void play_weapon_sounds(ecs::EventReader<WeaponFiredEvent> shots, ecs::EventReader<WeaponImpactEvent> impacts,
        ecs::EventReader<WeaponActionEvent> actions, ecs::Query<const Player> players, ecs::ResMut<audio::AudioEngine> audio,
        ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::FrameTime> time)
    {
        // the player's own weapon is right at the ears: flat; anyone else's is in the world
        auto placement = [&] (ecs::Entity shooter, const glm::vec3& position) -> std::optional<audio::Spatial> {
            if (players.get(shooter))
                return std::nullopt;
            return world_at(position, 3.0f, 120.0f);
        };
        auto play = [&] (const std::vector<std::string>& list, float volume, std::optional<audio::Spatial> spatial,
            float delay = 0.0f) {
            if (const std::string* sound = pick(list))
                audio->play(*sound, muffled(*audio, *physics,
                    { .volume = volume, .pitch = random_range(0.97f, 1.03f), .spatial = spatial, .delay = delay }));
        };

        for (const WeaponFiredEvent& shot : shots.read())
        {
            const WeaponDef* def = weapons::find(shot.weapon);
            if (!def)
                continue;
            const WeaponSounds& sounds = def->sounds;
            const std::optional<audio::Spatial> spatial = placement(shot.shooter, shot.origin);
            play(sounds.fire, sounds.volume, spatial);
            play(sounds.cycle, sounds.volume, spatial, sounds.cycle_delay);

            if (def->mode != WeaponMode::Beam || sounds.beam_loop.empty())
                continue;
            BeamSound& beam = g_beams[shot.shooter.bits()];
            beam.last_ray = time->time;
            beam.spatial = spatial;
            if (beam.loop.is_valid() && audio->is_alive(beam.loop) && beam.weapon == def->name)
            {
                if (spatial)
                    audio->set_spatial(beam.loop, *spatial);
                continue;
            }
            if (beam.loop.is_valid())
                audio->stop(beam.loop, 0.05f);
            beam.weapon = def->name;
            play(sounds.beam_start, sounds.volume, spatial);
            if (const std::string* loop = pick(sounds.beam_loop))
                beam.loop = audio->play(*loop, muffled(*audio, *physics,
                    { .volume = sounds.volume, .looping = true, .spatial = spatial, .fade_in = 0.03f }));
        }

        // the beams no ray came from for two intervals: their end
        for (auto it = g_beams.begin(); it != g_beams.end();)
        {
            BeamSound& beam = it->second;
            const WeaponDef* def = weapons::find(beam.weapon);
            const double silence = def ? 2.0 * def->interval + 0.05 : 0.0;
            if (time->time - beam.last_ray <= silence)
            {
                ++it;
                continue;
            }
            audio->stop(beam.loop, 0.08f);
            if (def)
                play(def->sounds.beam_stop, def->sounds.volume, beam.spatial);
            it = g_beams.erase(it);
        }

        std::unordered_map<std::string_view, int> impacts_of_frame;
        for (const WeaponImpactEvent& impact : impacts.read())
        {
            const WeaponDef* def = weapons::find(impact.weapon);
            if (!def || ++impacts_of_frame[def->name] > max_impacts_per_frame)
                continue;
            const WeaponSounds& sounds = def->sounds;
            const auto& list = impact.creature && !sounds.impact_creature.empty() ? sounds.impact_creature : sounds.impact;
            play(list, sounds.volume * sounds.impact_volume, world_at(impact.position, 2.0f, 80.0f));
        }

        for (const WeaponActionEvent& action : actions.read())
        {
            const WeaponDef* def = weapons::find(action.weapon);
            if (!def)
                continue;
            const auto& list = action.action == WeaponAction::Reload ? def->sounds.reload : def->sounds.dry_fire;
            // the holder's position is not in the event: actions of others are rare, they play flat too
            play(list, def->sounds.volume, std::nullopt);
        }
    }

    /************************************************************************
     * FOOTSTEPS
     ***********************************************************************/

    enum class StepType : uint8_t { step, walk_run, land };

    // the footstep files by surface, found once among the sound assets by their names
    const std::vector<std::string>& footstep_sounds(audio::AudioEngine& audio, std::string_view surface)
    {
        static std::map<std::string, std::vector<std::string>, std::less<>> by_surface;
        if (by_surface.empty())
            for (const std::string& path : audio.find_sound_assets())
                for (std::string_view name : { "grass", "stone" })
                    if (path.starts_with(std::format("audio/environment/foley/footstep_{}_", name)))
                        by_surface[std::string(name)].push_back(path);
        static const std::vector<std::string> none;
        const auto it = by_surface.find(surface);
        return it != by_surface.end() ? it->second : none;
    }

    // the ground under the feet: the terrain is grass, anything else (Sponza, rocks, voxels) stone
    std::string_view surface_under(const phys::PhysicsScene& physics, const glm::vec3& feet, const ecs::Query<const Terrain>& terrains)
    {
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world | phys::Category::dynamic | phys::Category::voxel;
        const std::optional<phys::Hit> hit = physics.raycast(feet + glm::vec3(0.0f, 0.5f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f), 1.5f, filter);
        if (!hit)
            return "grass";
        const ecs::Entity e = entity_of(*hit);
        return e && terrains.get(e) ? "grass" : "stone";
    }

    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<FootstepAudio>, =ecs::after<WeaponAudio>, =ecs::after<anim::Evaluate>,
      =ecs::after<anim::PostProcess>]]
    void play_footsteps(ecs::Query<const anim::Animator, const loco::LocomotionCharacter> characters,
        ecs::Query<const Terrain> terrains, ecs::Query<const Player> players, ecs::Res<phys::PhysicsScene> physics,
        ecs::ResMut<audio::AudioEngine> audio)
    {
        characters.each([&] (ecs::Entity e, const anim::Animator& animator, const loco::LocomotionCharacter& character) {
            if (!animator.valid() || animator.events.empty())
                return;
            // ALS: the animations that must stay silent say so with this curve
            if (const std::optional<uint32_t> block = animator.rig->curves.find("FootstepSoundBlock"); block && animator.curve(*block) >= 0.5f)
                return;

            // blended clips cross the same notify together: the strongest one per foot
            struct Step { float weight = 0.0f; StepType type = StepType::step; };
            std::array<Step, 2> feet{};
            for (const anim::Event& event : animator.events)
            {
                if (!event.notify || event.notify->type != "AlsAnimNotify_FootstepEffects")
                    continue;
                const size_t foot = event.notify->field("foot_bone").find("RIGHT") != std::string_view::npos ? 1 : 0;
                const std::string_view type = event.notify->field("sound_type");
                const StepType step = type.find("LAND") != std::string_view::npos ? StepType::land
                    : type.find("WALK_RUN") != std::string_view::npos ? StepType::walk_run : StepType::step;
                if (event.weight > feet[foot].weight)
                    feet[foot] = { event.weight, step };
            }

            for (const Step& step : feet)
            {
                if (step.weight < 0.25f || (character.locomotion_mode == loco::LocomotionMode::in_air && step.type != StepType::land))
                    continue;
                const std::string_view surface = surface_under(*physics, character.position, terrains);
                const std::string* sound = pick(footstep_sounds(*audio, surface));
                if (!sound)
                    continue;
                const float loudness = step.type == StepType::land ? 1.0f : step.type == StepType::walk_run ? 0.75f : 0.45f;
                const float pitch = (step.type == StepType::land ? 0.9f : 1.0f) * random_range(0.94f, 1.06f);
                const audio::PlayParams params{ .volume = cv_footsteps_volume.get() * loudness * std::min(step.weight * 1.5f, 1.0f),
                    .pitch = pitch, .spatial = world_at(character.position, 2.5f, 30.0f) };
                // the player's own steps are always heard (the camera may look over a wall at it)
                audio->play(*sound, players.get(e) ? params : muffled(*audio, *physics, params));
                if (cv_footsteps_debug.get())
                    cvar::print(std::format("footstep {} {}: {} weight {:.2f}", e.index, surface,
                        step.type == StepType::land ? "land" : step.type == StepType::walk_run ? "walk_run" : "step", step.weight));
            }
        });
    }

    /************************************************************************
     * AMBIENCE
     ***********************************************************************/

    // a looping 2D stream at `volume` (low-passed at lowpass Hz, 0: not): started when it becomes audible,
    // stopped when it fades away
    void keep_bed(audio::AudioEngine& audio, audio::VoiceId& voice, const std::string& sound, float volume, float lowpass)
    {
        const bool alive = voice.is_valid() && audio.is_alive(voice);
        if (sound.empty() || volume <= 0.001f)
        {
            if (alive)
                audio.stop(voice, 1.5f);
            voice = {};
            return;
        }
        if (alive)
        {
            audio.set_volume(voice, volume);
            audio.set_lowpass(voice, lowpass);
            return;
        }
        const audio::SoundId id = audio.get_sound(sound, { .stream = true });
        voice = audio.play(id, { .volume = volume, .looping = true, .fade_in = 2.0f, .lowpass = lowpass });
    }

    // How open the listener's place is, 0 (a room) .. 1 (the open sky): the share of rays up and around that
    // leave without meeting the level within reach, the steep ones weigh more (a ceiling matters most). A
    // courtyard or an arcade comes out in between.
    float openness(const phys::PhysicsScene& physics, const glm::vec3& listener)
    {
        constexpr float reach = 30.0f;
        struct Ray { glm::vec3 direction; float weight; };
        static const std::vector<Ray> rays = [] {
            std::vector<Ray> r{ { glm::vec3(0.0f, 1.0f, 0.0f), 4.0f } };
            for (int i = 0; i < 8; ++i)
            {
                const float yaw = float(i) * 0.785398f;
                for (const auto [elevation, weight] : { std::pair{ 0.785398f, 1.0f }, std::pair{ 0.261799f, 0.5f } })
                    r.push_back({ glm::vec3(std::cos(yaw) * std::cos(elevation), std::sin(elevation), std::sin(yaw) * std::cos(elevation)),
                        weight });
            }
            return r;
        }();
        phys::QueryFilter filter;
        filter.categories = phys::Category::static_world | phys::Category::voxel;
        float open = 0.0f;
        float total = 0.0f;
        for (const Ray& ray : rays)
        {
            total += ray.weight;
            if (!physics.raycast(listener, ray.direction, reach, filter))
                open += ray.weight;
        }
        return open / total;
    }

    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<AmbienceAudio>, =ecs::after<FootstepAudio>, =ecs::after<SkyControl>]]
    void play_ambience(ecs::Query<OutdoorAmbience> ambiences, ecs::Query<const SkyController> skies,
        ecs::ResMut<audio::AudioEngine> audio, ecs::Res<phys::PhysicsScene> physics, ecs::Res<ecs::FrameTime> time)
    {
        std::optional<float> elevation;
        skies.each([&] (const SkyController& sky) {
            if (!elevation)
                elevation = sky.sun_elevation;
        });

        ambiences.each([&] (OutdoorAmbience& ambience) {
            const float day = elevation
                ? glm::smoothstep(ambience.night_elevation, std::max(ambience.day_elevation, ambience.night_elevation + 0.01f), *elevation)
                : 1.0f;
            const float night = 1.0f - day;

            // indoors: quieter and duller, over half a second (through a door)
            const float target = openness(*physics, audio->get_listener().position);
            ambience.outdoor += (target - ambience.outdoor) * (1.0f - std::exp(-float(time->dt) / 0.5f));
            const float muffle = glm::mix(ambience.indoor_volume, 1.0f, ambience.outdoor);
            const float lowpass = ambience.outdoor > 0.97f ? 0.0f
                : std::exp(glm::mix(std::log(std::max(ambience.indoor_lowpass, 20.0f)), std::log(18000.0f), ambience.outdoor));

            keep_bed(*audio, ambience.day_voice, ambience.day, ambience.volume * muffle * day, lowpass);
            keep_bed(*audio, ambience.night_voice, ambience.night, ambience.volume * muffle * night, lowpass);
            keep_bed(*audio, ambience.night_layer_voice, ambience.night_layer,
                ambience.volume * ambience.night_layer_volume * muffle * night, lowpass);

            // night calls: somewhere around the listener, 25 .. 60 m away and up a bit (a tree)
            if (night < 0.5f || ambience.night_call.empty())
            {
                ambience.next_call = -1.0f;
                return;
            }
            if (ambience.next_call < 0.0f)
                ambience.next_call = random_range(ambience.night_call_interval_min, ambience.night_call_interval_max);
            ambience.next_call -= float(time->dt);
            if (ambience.next_call > 0.0f)
                return;
            ambience.next_call = random_range(ambience.night_call_interval_min, ambience.night_call_interval_max);
            const float angle = random_range(0.0f, 6.2831853f);
            const float distance = random_range(25.0f, 60.0f);
            const glm::vec3 position = audio->get_listener().position
                + glm::vec3(std::cos(angle) * distance, random_range(4.0f, 12.0f), std::sin(angle) * distance);
            audio->play(ambience.night_call, muffled(*audio, *physics, { .volume = ambience.night_call_volume * night,
                .pitch = random_range(0.95f, 1.05f), .spatial = world_at(position, 8.0f, 150.0f) }));
        });
    }

    [[=ecs::on_remove]]
    void stop_ambience(ecs::Registry& registry, ecs::Entity, OutdoorAmbience& ambience)
    {
        audio::AudioEngine* audio = registry.find_resource<audio::AudioEngine>();
        for (audio::VoiceId* voice : { &ambience.day_voice, &ambience.night_voice, &ambience.night_layer_voice })
        {
            if (audio && voice->is_valid())
                audio->stop(*voice, 1.0f);
            *voice = {};
        }
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(OutdoorAmbience)
}
