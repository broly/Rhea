module sky_controller;

import std.compat;
import glm;
import rhmath;
import name;
import ecs;
import framework;
import rhcomponents;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

namespace
{
    constexpr float hours_per_day = 24.0f;
    // the moon rises about when the sun sets
    constexpr float moon_lag_hours = 12.6f;

    float wrap_hours(float hours)
    {
        hours = std::fmod(hours, hours_per_day);
        return hours < 0.0f ? hours + hours_per_day : hours;
    }

    // Direction towards a body on the celestial sphere `hour_angle` past its highest point
    glm::vec3 celestial_direction(const SkyController& controller, float hour_angle, float declination)
    {
        const float latitude = glm::radians(controller.latitude);
        const float east = -std::cos(declination) * std::sin(hour_angle);
        const float north = std::cos(latitude) * std::sin(declination) - std::sin(latitude) * std::cos(declination) * std::cos(hour_angle);
        const float up = std::sin(latitude) * std::sin(declination) + std::cos(latitude) * std::cos(declination) * std::cos(hour_angle);

        // north is -Z turned clockwise (seen from above) by `north`, east is to its right
        const float heading = glm::radians(controller.north);
        const glm::vec3 north_axis(std::sin(heading), 0.0f, -std::cos(heading));
        const glm::vec3 east_axis(std::cos(heading), 0.0f, std::sin(heading));
        return glm::normalize(east_axis * east + north_axis * north + glm::vec3(0.0f, up, 0.0f));
    }

    float hour_angle(float hour)
    {
        return (hour - 12.0f) / hours_per_day * glm::radians(360.0f);
    }

    Weather blend(const Weather& a, const Weather& b, float t)
    {
        Weather result;
        result.cloud_coverage = glm::mix(a.cloud_coverage, b.cloud_coverage, t);
        result.cloud_density = glm::mix(a.cloud_density, b.cloud_density, t);
        result.cloud_bottom = glm::mix(a.cloud_bottom, b.cloud_bottom, t);
        result.cloud_thickness = glm::mix(a.cloud_thickness, b.cloud_thickness, t);
        result.wind_speed = glm::mix(a.wind_speed, b.wind_speed, t);
        result.haze = glm::mix(a.haze, b.haze, t);
        result.sun_light = glm::mix(a.sun_light, b.sun_light, t);
        return result;
    }

    // The weather the level was authored with: what the components of the sky say
    Weather weather_of(const SkyAtmosphere& atmosphere, const VolumetricClouds* clouds)
    {
        Weather result;
        result.haze = atmosphere.mie;
        if (clouds)
        {
            result.cloud_coverage = clouds->enabled ? clouds->coverage : 0.0f;
            result.cloud_density = clouds->density;
            result.cloud_bottom = clouds->bottom;
            result.cloud_thickness = clouds->thickness;
            result.wind_speed = clouds->wind_speed;
        }
        else
            result.cloud_coverage = 0.0f;
        return result;
    }

    void advance_time(SkyController& controller, float dt)
    {
        if (controller.paused || controller.day_length <= 0.0f)
        {
            controller.time_of_day = wrap_hours(controller.time_of_day);
            return;
        }
        const float scale = std::max(sky::evaluate_rail(controller, controller.time_of_day).time_scale, 0.0f);
        controller.time_of_day = wrap_hours(controller.time_of_day + dt * hours_per_day / controller.day_length * scale);
    }

    void advance_weather(SkyController& controller, const Weather& authored, float dt)
    {
        const auto preset = controller.weather_presets.find(controller.weather);
        // an unknown name keeps the sky as it was authored
        const Weather& target = preset != controller.weather_presets.end() ? preset->second : authored;

        if (!controller.started)
        {
            controller.started = true;
            controller.active_weather = controller.weather;
            controller.weather_blend = 1.0f;
        }
        else if (controller.weather != controller.active_weather)
        {
            controller.active_weather = controller.weather;
            controller.previous_weather = controller.current_weather;
            controller.weather_blend = 0.0f;
        }

        if (controller.weather_blend < 1.0f)
        {
            controller.weather_blend = controller.weather_transition > 0.0f
                ? std::min(controller.weather_blend + dt / controller.weather_transition, 1.0f)
                : 1.0f;
        }
        // the target itself once the transition is over: edits of the preset show at once
        controller.current_weather = controller.weather_blend < 1.0f
            ? blend(controller.previous_weather, target, glm::smoothstep(0.0f, 1.0f, controller.weather_blend))
            : target;
    }

    // The sky controllers drive their sky, clouds and the directional light. Before the clouds move: they
    // take this frame's wind.
    [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<SkyControl>, =ecs::before<CloudWind>]]
    void update_sky_controllers(ecs::Query<SkyController, SkyAtmosphere> skies, ecs::Query<VolumetricClouds> cloud_layers,
        ecs::Query<const Name, Light, Transform> lights, ecs::Res<ecs::FrameTime> time)
    {
        skies.each([&] (ecs::Entity e, SkyController& controller, SkyAtmosphere& atmosphere) {
            const float dt = float(time->dt);
            VolumetricClouds* clouds = cloud_layers.get(e);

            // ---- time and weather ----
            advance_time(controller, dt);
            if (!controller.started)
                controller.current_weather = weather_of(atmosphere, clouds);
            advance_weather(controller, controller.current_weather, dt);

            const Weather& weather = controller.current_weather;
            const SkyKey key = sky::evaluate_rail(controller, controller.time_of_day);

            // ---- the sky ----
            const glm::vec3 sun = sky::sun_direction(controller, controller.time_of_day);
            const glm::vec3 moon = sky::moon_direction(controller, controller.time_of_day);
            controller.sun_elevation = glm::degrees(std::asin(std::clamp(sun.y, -1.0f, 1.0f)));

            atmosphere.driven = true;
            atmosphere.sun_direction = sun;
            atmosphere.moon_direction = moon;
            atmosphere.mie = weather.haze;
            atmosphere.intensity = key.sky_intensity;
            // the stars turn once a day around the celestial pole
            const float heading = glm::radians(controller.north);
            const float latitude = glm::radians(controller.latitude);
            atmosphere.stars_axis = glm::vec3(std::sin(heading) * std::cos(latitude), std::sin(latitude),
                -std::cos(heading) * std::cos(latitude));
            atmosphere.stars_rotation = hour_angle(controller.time_of_day);

            if (clouds)
            {
                clouds->coverage = weather.cloud_coverage;
                clouds->density = weather.cloud_density;
                clouds->bottom = weather.cloud_bottom;
                clouds->thickness = weather.cloud_thickness;
                clouds->wind_speed = weather.wind_speed;
            }

            // ---- the directional light: the sun, or the moon once the sun is down ----
            const AtmosphereModel model = AtmosphereModel::from(atmosphere);
            const glm::vec3 sun_light = atmosphere.sun_color.glm() * atmosphere.sun_intensity
                * model.transmittance(0.0f, sun) * glm::smoothstep(-0.02f, 0.03f, sun.y);
            const glm::vec3 moon_light = controller.moon_light_color.glm() * controller.moon_light_intensity
                * model.transmittance(0.0f, moon) * glm::smoothstep(0.0f, 0.08f, moon.y)
                * glm::smoothstep(-0.05f, -0.12f, sun.y);
            // both are dark when they swap, in the dusk
            const bool by_moon = std::max({ moon_light.x, moon_light.y, moon_light.z })
                > std::max({ sun_light.x, sun_light.y, sun_light.z });
            const glm::vec3 body = by_moon ? moon : sun;
            // above the clouds, and what is left of it below them
            const glm::vec3 body_light = (by_moon ? moon_light : sun_light) * key.light_tint.glm() * key.light_intensity;
            const glm::vec3 color = body_light * weather.sun_light * controller.light_tint.glm();

            atmosphere.cloud_light_direction = body;
            atmosphere.cloud_light_color = body_light;

            bool found = false;
            lights.each([&] (ecs::Entity, const Name& name, Light& light, Transform& transform) {
                if (found || light.type != LightType::directional)
                    return;
                if (!controller.sun.empty() && name != Name(controller.sun))
                    return;
                found = true;

                light.color = glm::vec4(color, 0.0f);
                // the light shines along its forward axis (-Z): away from the body
                const glm::vec3 up = std::abs(body.y) > 0.999f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
                transform.rotation = glm::quatLookAtRH(-body, up);
            });
        });
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(SkyController)
}

glm::vec3 sky::sun_direction(const SkyController& controller, float hour)
{
    return celestial_direction(controller, hour_angle(hour), glm::radians(controller.declination));
}

glm::vec3 sky::moon_direction(const SkyController& controller, float hour)
{
    // opposite the sun, on the other side of the celestial equator
    return celestial_direction(controller, hour_angle(hour - moon_lag_hours), -glm::radians(controller.declination));
}

SkyKey sky::evaluate_rail(const SkyController& controller, float hour)
{
    const std::vector<SkyKey>& rail = controller.day_rail;
    if (rail.empty())
        return SkyKey{ .hour = hour };
    if (rail.size() == 1)
        return rail.front();

    // the keys around `hour` on the clock (the rail may be authored in any order)
    const SkyKey* before = nullptr;
    const SkyKey* after = nullptr;
    float before_distance = hours_per_day + 1.0f;
    float after_distance = hours_per_day + 1.0f;
    for (const SkyKey& key : rail)
    {
        const float back = wrap_hours(hour - key.hour);       // hours since the key
        const float ahead = wrap_hours(key.hour - hour);      // hours until the key
        if (back < before_distance)
        {
            before_distance = back;
            before = &key;
        }
        if (ahead > 0.0f && ahead < after_distance)
        {
            after_distance = ahead;
            after = &key;
        }
    }
    if (!after || before == after)
        return *before;

    const float span = before_distance + after_distance;
    const float t = span > 0.0f ? before_distance / span : 0.0f;
    SkyKey result;
    result.hour = hour;
    result.light_tint = glm::mix(before->light_tint.glm(), after->light_tint.glm(), t);
    result.light_intensity = glm::mix(before->light_intensity, after->light_intensity, t);
    result.sky_intensity = glm::mix(before->sky_intensity, after->sky_intensity, t);
    result.time_scale = glm::mix(before->time_scale, after->time_scale, t);
    return result;
}

SkyController* sky::find_controller(ecs::Registry& registry, ecs::Entity* entity)
{
    SkyController* result = nullptr;
    ecs::Query<SkyController>(registry).each([&] (ecs::Entity e, SkyController& controller) {
        if (result)
            return;
        result = &controller;
        if (entity)
            *entity = e;
    });
    return result;
}

std::string sky::format_time(float hours)
{
    const int minutes = int(std::floor(wrap_hours(hours) * 60.0f)) % (24 * 60);
    return std::format("{:02}:{:02}", minutes / 60, minutes % 60);
}
