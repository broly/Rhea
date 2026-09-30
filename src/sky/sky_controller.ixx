module;

#include <json/value.h>

export module sky_controller;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;

// Sky controller: one component which runs the sky of a level (in the spirit of UE's Sky Creator).
// It owns the time of day and the weather and drives what draws them:
//
//   SkyAtmosphere     (same entity)  where the sun and the moon are, the stars, the haze
//   VolumetricClouds  (same entity)  coverage, density, altitude, wind
//   VolumetricFog     (same entity)  density of the ground fog (the weather, thicker at the hours the rail says)
//   directional Light (entity `sun`) the sun by day and the moon by night: direction, and the color the
//                                    atmosphere leaves of it at the ground
//
// Time of day: the sun follows its path for `latitude` and `declination` (season); a full day takes
// day_length seconds. The day rail (`day_rail`) is the art direction along that path: keys by hour,
// interpolated around the clock, which tint the light, scale the sky and the speed of time (e.g. short nights).
//
// Weather: named presets (`weather_presets`), the sky blends to the one named by `weather` over
// weather_transition seconds.
//
//     "SkyController": {
//         "time_of_day": 10, "day_length": 600, "sun": "dir_light0", "weather": "overcast",
//         "weather_presets": { "overcast": { "cloud_coverage": 0.75, "sun_light": 0.7 }, "clear": { "cloud_coverage": 0.1 } },
//         "day_rail": [ { "hour": 6.5, "light_tint": { "x": 1, "y": 0.8, "z": 0.6 } }, { "hour": 9 }, { "hour": 22, "time_scale": 4 } ]
//     }
export
{
    // What the sky is like, whatever the hour
    struct Weather
    {
        [[=rh::edit, =rh::range<0.f, 1.f>]] float cloud_coverage = 0.5f;
        [[=rh::edit, =rh::range<0.f, 4.f>]] float cloud_density = 1.0f;
        [[=rh::edit, =rh::speed<10.f>]] float cloud_bottom = 1500.0f;        // m
        [[=rh::edit, =rh::speed<10.f>]] float cloud_thickness = 2200.0f;     // m
        [[=rh::edit, =rh::speed<0.1f>]] float wind_speed = 12.0f;            // m/s
        // SkyAtmosphere::mie: 1 clear air, more for mist
        [[=rh::edit, =rh::range<0.f, 10.f>]] float haze = 1.0f;
        // VolumetricFog::density: the fog in the low ground, optical depth per kilometer
        [[=rh::edit, =rh::speed<0.1f>]] float fog = 6.0f;
        // share of the sun (moon) light the weather leaves at the ground, apart from the shadows of its clouds
        // (those are cast by the renderer, VolumetricClouds::shadow): rain, mist, a veil above the layer
        [[=rh::edit, =rh::range<0.f, 1.f>]] float sun_light = 1.0f;
    };

    // A key of the day rail: the look at an hour of the day
    struct SkyKey
    {
        float hour = 12.0f;                     // 0 .. 24
        vec3 light_tint{ 1.0f, 1.0f, 1.0f };    // of the directional light
        float light_intensity = 1.0f;           // its scale
        float sky_intensity = 1.0f;             // SkyAtmosphere::intensity
        float fog = 1.0f;                       // scale of the ground fog (Weather::fog): the morning mist
        float time_scale = 1.0f;                // speed of the time of day around this hour
    };

    struct SkyController
    {
        // hours, 0 .. 24: 12 is solar noon
        [[=rh::edit, =rh::range<0.f, 24.f>]] float time_of_day = 10.0f;
        // seconds a full day takes, 0: the time stands still
        [[=rh::edit, =rh::speed<1.f>]] float day_length = 600.0f;
        [[=rh::edit]] bool paused = false;

        // path of the sun: degrees north of the equator, and the tilt of the season
        // (23.4 midsummer, 0 equinox, -23.4 midwinter)
        [[=rh::edit, =rh::range<-90.f, 90.f>]] float latitude = 45.0f;
        [[=rh::edit, =rh::range<-23.44f, 23.44f>]] float declination = 12.0f;
        // where north is: degrees from -Z, clockwise seen from above (the sun rises 90 degrees further)
        [[=rh::edit, =rh::range<-180.f, 180.f>]] float north = 0.0f;

        // name of the entity with the directional Light; empty: the first directional light of the level
        [[=rh::edit]] std::string sun;

        // the moon: the light by night
        [[=rh::edit, =rh::color]] vec3 moon_light_color{ 0.55f, 0.68f, 1.0f };
        [[=rh::edit, =rh::speed<0.01f>]] float moon_light_intensity = 0.35f;

        [[=rh::edit]] std::string weather;
        [[=rh::edit, =rh::speed<0.1f>]] float weather_transition = 8.0f;    // seconds
        std::map<std::string, Weather> weather_presets;

        std::vector<SkyKey> day_rail;

        // --- runtime ---
        // the weather right now (between presets during a transition)
        [[=rh::edit, =rh::read_only, =rh::transient]] Weather current_weather;
        [[=rh::transient]] Weather previous_weather;
        [[=rh::transient]] std::string active_weather;
        [[=rh::transient]] float weather_blend = 1.0f;
        [[=rh::transient]] bool started = false;
        // extra tint of the directional light from gameplay (lighting presets, flashes)
        [[=rh::transient]] vec3 light_tint{ 1.0f, 1.0f, 1.0f };
        // elevation of the sun above the horizon, degrees (for display)
        [[=rh::edit, =rh::read_only, =rh::transient]] float sun_elevation = 0.0f;
    };

    // System set of the controller (Update): systems which read what it drives run after it
    struct SkyControl {};

    namespace sky
    {
        // Direction towards the sun at an hour of the controller's day
        glm::vec3 sun_direction(const SkyController& controller, float hour);
        // Direction towards the moon: up while the sun is down
        glm::vec3 moon_direction(const SkyController& controller, float hour);

        // The day rail at an hour: keys interpolated around the clock, a default key when the rail is empty
        SkyKey evaluate_rail(const SkyController& controller, float hour);

        // The controller of the level (the first one) and its entity, nullptr if the level has none
        SkyController* find_controller(ecs::Registry& registry, ecs::Entity* entity = nullptr);

        // "14:30"
        std::string format_time(float hours);
    }
}
