module game;

import :sky_renderer;

import std.compat;
import assertions;

import render;
import render_scene;
import rhcomponents;
import assets;
import glm;
import rhmath;
import name;
import profile;

import log;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogSky, Display);

namespace
{
    // N slices of N x N stacked along the image height (tools/sky/generate_cloud_noise.py)
    constexpr const char* cloud_shape_path = "textures/noise/cloud_shape_128.png";
    constexpr const char* cloud_detail_path = "textures/noise/cloud_detail_32.png";

    // extinction (1 / m) of a cloud at VolumetricClouds::density 1, full noise density
    constexpr float cloud_extinction = 0.07f;
    // second lobe of the cloud phase function: some light comes back towards the sun
    constexpr float cloud_back_anisotropy = 0.25f;
    constexpr float cloud_back_weight = 0.3f;
    // threshold range one unit of VolumetricClouds::coverage_variation moves the coverage by
    constexpr float weather_amplitude = 0.5f;

    // Noise threshold which leaves the fraction `coverage` of the sky covered. Measured on the shape noise by
    // tools/sky/generate_cloud_noise.py (the table it prints): shaders/sky/clouds.glsl thresholds the
    // normalized base shape times the height profile of the layer.
    float coverage_threshold(float coverage)
    {
        static constexpr std::array<float, 11> table{
            1.0f, 0.883f, 0.827f, 0.782f, 0.741f, 0.702f, 0.663f, 0.619f, 0.567f, 0.492f, 0.0f };
        const float position = std::clamp(coverage, 0.0f, 1.0f) * float(table.size() - 1);
        const size_t index = std::min(size_t(position), table.size() - 2);
        return glm::mix(table[index], table[index + 1], position - float(index));
    }

    RBImageHandle load_volume(RenderBackend& backend, const char* path)
    {
        const TextureHandle handle = AssetManager::get().load_texture(path);
        checkf(handle.is_valid(), "cloud noise volume '%s' is missing (tools/sky/generate_cloud_noise.py writes it)", path);
        const Texture& texture = handle.get();
        checkf(texture.extent.width > 0 && texture.extent.height % texture.extent.width == 0,
            "'%s' is not a stack of square slices", path);
        return backend.create_texture_3d(texture, texture.extent.height / texture.extent.width, true);
    }

    float disc_cos(float angular_diameter_degrees)
    {
        return std::cos(glm::radians(std::max(angular_diameter_degrees, 0.01f) * 0.5f));
    }
}

void SkyRenderer::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    sky_resource = renderer->find_resource("sky");
    buffers_resource = renderer->find_resource("sky_buffers");
    targets_resource = renderer->find_resource("sky_targets");
    checkf(sky_resource && buffers_resource && targets_resource, "sky resources are missing (assets/render/resources)");

    auto sky_model = renderer->find_model("sky");
    checkf(sky_model, "material model 'sky' is missing (assets/render/schemas/sky.json)");
    march_family = renderer->query_pipeline_family("SkyMarch", sky_model);
    sky_family = renderer->query_pipeline_family("Sky", sky_model);
    cloud_shadow_family = renderer->query_pipeline_family("CloudShadow", sky_model);

    cloud_shape = load_volume(*backend, cloud_shape_path);
    cloud_detail = load_volume(*backend, cloud_detail_path);
}

void SkyRenderer::prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs)
{
    PROFILE("SkyRenderer::prepare");

    // pipelines are requested every frame: hot reload drops the PSO cache
    march_pipeline = march_family->request_pipeline({});
    sky_pipeline = sky_family->request_pipeline({});
    cloud_shadow_pipeline = cloud_shadow_family->request_pipeline({});

    // ---- the sky of the level and its directional light ----
    const RenderObject_Sky* active = scene_view.get_processor<SceneViewProcessor_Sky>().get_active_sky();
    const SkyAtmosphere atmosphere = active ? active->atmosphere : SkyAtmosphere{};
    VolumetricClouds clouds = active ? active->clouds : VolumetricClouds{};
    if (!active || !cv_clouds_enabled.get())
        clouds.enabled = false;
    cloud_coverage = clouds.enabled ? std::clamp(clouds.coverage, 0.0f, 1.0f) : 0.0f;

    // the directional light: what reaches the ground
    glm::vec3 light_direction(0.0f, 1.0f, 0.0f);   // towards the light
    glm::vec3 light_color(0.0f);
    bool has_light = false;
    if (const auto light = scene_view.get_processor<SceneViewProcessor_Light>().get_directional_light())
    {
        light_direction = -glm::normalize(light->direction);
        light_color = glm::vec3(light->color);
        has_light = true;
    }

    // a sky controller places the sun and the moon and says which lights the clouds; otherwise the sun is
    // the directional light
    const glm::vec3 sun_direction = atmosphere.driven ? glm::normalize(atmosphere.sun_direction.glm()) : light_direction;
    const glm::vec3 cloud_light_direction = atmosphere.driven
        ? glm::normalize(atmosphere.cloud_light_direction.glm())
        : light_direction;
    const glm::vec3 cloud_light_color = atmosphere.driven ? atmosphere.cloud_light_color.glm() : light_color;
    const glm::vec3 moon_direction = atmosphere.driven && glm::length(atmosphere.moon_direction.glm()) > 0.5f
        ? glm::normalize(atmosphere.moon_direction.glm())
        : glm::vec3(0.0f);
    const glm::vec3 sun_illuminance = atmosphere.sun_color.glm() * std::max(atmosphere.sun_intensity, 0.0f);

    const AtmosphereModel model = AtmosphereModel::from(atmosphere);
    const glm::vec3 night = atmosphere.night_color.glm() * model.intensity;

    // ---- what the frame takes from the sky: cosine weighted average over the upper hemisphere ----
    // (directions relative to the sun's azimuth, none of them at the sun: the estimate is steady while it moves)
    {
        const float sun_azimuth = std::atan2(sun_direction.x, sun_direction.z);
        constexpr int rings = 4;
        constexpr int sectors = 6;
        glm::vec3 sum(0.0f);
        for (int ring = 0; ring < rings; ++ring)
        {
            // cos(theta) = sqrt(u): density proportional to the cosine
            const float cos_theta = std::sqrt((float(ring) + 0.5f) / float(rings));
            const float sin_theta = std::sqrt(1.0f - cos_theta * cos_theta);
            for (int sector = 0; sector < sectors; ++sector)
            {
                const float azimuth = sun_azimuth + (float(sector) + 0.5f) * glm::radians(360.0f / float(sectors));
                const glm::vec3 direction(std::sin(azimuth) * sin_theta, cos_theta, std::cos(azimuth) * sin_theta);
                sum += model.radiance(inputs.camera_position.y, direction, sun_direction, sun_illuminance);
            }
        }
        sky_ambient = sum / float(rings * sectors) + night;
    }

    // the ground below the clouds, lit by the directional light and that sky
    const glm::vec3 ground_radiance = model.ground_albedo
        * (light_color * (std::max(light_direction.y, 0.0f) / std::numbers::pi_v<float>) + sky_ambient);

    // ---- sky UBO ----
    SkyUBO ubo{};
    ubo.sun_direction = glm::vec4(sun_direction, disc_cos(atmosphere.sun_disc_size));
    ubo.sun_illuminance = glm::vec4(sun_illuminance, std::max(atmosphere.sun_disc_intensity, 0.0f));
    ubo.moon_direction = glm::vec4(moon_direction, disc_cos(atmosphere.moon_disc_size));
    ubo.moon_radiance = glm::vec4(atmosphere.moon_color.glm() * std::max(atmosphere.moon_disc_intensity, 0.0f), 0.0f);
    ubo.light_direction = glm::vec4(cloud_light_direction, 0.0f);
    ubo.light_color = glm::vec4(cloud_light_color, 0.0f);
    ubo.rayleigh_scattering = glm::vec4(model.rayleigh_scattering, model.rayleigh_height);
    ubo.mie_scattering = glm::vec4(model.mie_scattering, model.mie_height);
    ubo.mie_extinction = glm::vec4(model.mie_extinction, model.mie_anisotropy);
    ubo.ozone_absorption = glm::vec4(model.ozone_absorption, model.multiple_scattering);
    ubo.planet = glm::vec4(model.planet_radius, model.atmosphere_radius, model.ground_altitude, model.intensity);
    ubo.ground_albedo = glm::vec4(model.ground_albedo, 0.0f);
    ubo.night_radiance = glm::vec4(atmosphere.night_color.glm(), std::max(atmosphere.stars_intensity, 0.0f));
    const glm::vec3 stars_axis = glm::length(atmosphere.stars_axis.glm()) > 1e-3f
        ? glm::normalize(atmosphere.stars_axis.glm())
        : glm::vec3(0.0f, 1.0f, 0.0f);
    ubo.stars_rotation = glm::vec4(stars_axis, atmosphere.stars_rotation);
    // what lights the clouds from above: the blue sky between them, white light from the clouds around them
    // once they close up
    const glm::vec3 overcast_ambient(glm::dot(sky_ambient, glm::vec3(0.2126f, 0.7152f, 0.0722f)));
    ubo.ambient_top = glm::vec4(glm::mix(sky_ambient, overcast_ambient, cloud_coverage * cloud_coverage), 0.0f);
    ubo.ambient_bottom = glm::vec4(ground_radiance, 0.0f);

    const float coverage = cloud_coverage;
    // a solid overcast has no gaps: the weather cells fade out towards full coverage
    const float variation = std::clamp(clouds.coverage_variation, 0.0f, 1.0f) * weather_amplitude
        * (1.0f - glm::smoothstep(0.85f, 1.0f, coverage));
    ubo.cloud_layer = glm::vec4(clouds.bottom, std::max(clouds.thickness, 10.0f), coverage_threshold(coverage),
        std::max(clouds.density, 0.0f) * cloud_extinction);
    ubo.cloud_scale = glm::vec4(1.0f / std::max(clouds.shape_size, 100.0f), 1.0f / std::max(clouds.detail_size, 10.0f),
        1.0f / std::max(clouds.weather_size, 1000.0f), std::clamp(clouds.detail, 0.0f, 0.95f));
    ubo.cloud_offset = glm::vec4(clouds.offset.glm(), variation);
    ubo.cloud_detail_offset = glm::vec4(clouds.detail_offset.glm(), clouds.enabled ? 1.0f : 0.0f);
    ubo.cloud_albedo = glm::vec4(clouds.albedo.glm(), std::max(clouds.ambient, 0.0f));
    ubo.cloud_phase = glm::vec4(std::clamp(clouds.anisotropy, 0.0f, 0.95f), cloud_back_anisotropy, cloud_back_weight,
        std::clamp(clouds.powder, 0.0f, 1.0f));
    ubo.cloud_march = glm::vec4(std::max(clouds.max_distance, 1000.0f), float(inputs.time), 0.0f, 0.0f);

    // ---- shadow of the clouds: a map across the directional light (what it is cast by), around the camera ----
    cloud_shadow = {};
    const float shadow_strength = std::clamp(clouds.shadow, 0.0f, 1.0f);
    if (clouds.enabled && has_light && light_direction.y > 0.0f && shadow_strength > 0.0f && cv_clouds_shadows.get())
    {
        const glm::vec3 up = std::abs(light_direction.y) > 0.99f ? glm::vec3(0.0f, 0.0f, 1.0f) : glm::vec3(0.0f, 1.0f, 0.0f);
        const glm::vec3 axis_u = glm::normalize(glm::cross(up, light_direction));
        const glm::vec3 axis_v = glm::cross(light_direction, axis_u);
        // the center moves in whole texels: the shadows do not crawl when the camera moves
        const float texel = cloud_shadow_size / float(cloud_shadow_texels);
        const float center_u = std::floor(glm::dot(inputs.camera_position, axis_u) / texel) * texel;
        const float center_v = std::floor(glm::dot(inputs.camera_position, axis_v) / texel) * texel;
        cloud_shadow.u = glm::vec4(axis_u / cloud_shadow_size, 0.5f - center_u / cloud_shadow_size);
        cloud_shadow.v = glm::vec4(axis_v / cloud_shadow_size, 0.5f - center_v / cloud_shadow_size);
        cloud_shadow.params = glm::vec4(shadow_strength, cloud_shadow_size,
            glm::dot(inputs.camera_position, light_direction), 0.0f);
    }

    sky_resource->update_uniform_buffer("sky_ubo", ubo, ctx.frame);
    sky_resource->update_image("u_cloud_shape", cloud_shape, { .frame = ctx.frame });
    sky_resource->update_image("u_cloud_detail", cloud_detail, { .frame = ctx.frame });

    buffers_resource->update_image("u_atmosphere", inputs.atmosphere, { .frame = ctx.frame });
    buffers_resource->update_image("u_clouds", inputs.clouds, { .frame = ctx.frame });
    buffers_resource->update_image("u_clouds_history", inputs.clouds_history,
        { .frame = ctx.frame, .layer_index = inputs.history_layer });
    targets_resource->update_image("u_atmosphere_target", inputs.atmosphere, { .frame = ctx.frame });
    targets_resource->update_image("u_clouds_target", inputs.clouds, { .frame = ctx.frame });

    if (inputs.clouds_history != history_image)
    {
        history_image = inputs.clouds_history;
        history_frames = 0;
    }
    downscale = std::max(inputs.downscale, 1u);
    frame_counter++;
}

void SkyRenderer::dispatch_march(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer, Extent extent)
{
    PROFILE("SkyRenderer::dispatch_march");

    ctx.bind_pipeline(march_pipeline);
    ctx.bind(camera, gbuffer, sky_resource, buffers_resource, targets_resource);
    ctx.push_constants(SkyMarchPushConstants{
        .steps = uint32_t(std::clamp(cv_clouds_steps.get(), 8, 256)),
        .light_steps = uint32_t(std::clamp(cv_clouds_light_steps.get(), 1, 16)),
        // new buffers hold no history: two frames fill both layers of it
        .history_weight = history_frames >= 2 ? std::clamp(cv_clouds_history.get(), 0.0f, 0.98f) : 0.0f,
        .frame = frame_counter,
        .downscale = downscale,
    });
    history_frames++;

    // local size of sky_march.comp
    constexpr uint32_t group = 8;
    ctx.compute({ (extent.width + group - 1) / group, (extent.height + group - 1) / group, 1 });
}

void SkyRenderer::draw_sky(RenderGraphContext& ctx, RenderResource* camera, RenderResource* gbuffer)
{
    PROFILE("SkyRenderer::draw_sky");

    if (ctx.bind_pipeline(sky_pipeline))
        ctx.bind(camera, gbuffer, sky_resource, buffers_resource);
    ctx.draw_fullscreen();
}

void SkyRenderer::draw_cloud_shadow(RenderGraphContext& ctx, RenderResource* light)
{
    PROFILE("SkyRenderer::draw_cloud_shadow");

    backend->set_viewport(ctx.cmd, 0, 0, cloud_shadow_texels, cloud_shadow_texels);
    if (ctx.bind_pipeline(cloud_shadow_pipeline))
        ctx.bind(light, sky_resource);
    ctx.push_constants(CloudShadowPushConstants{
        .steps = uint32_t(std::clamp(cv_clouds_shadow_steps.get(), 4, 128)),
    });
    ctx.draw_fullscreen();
}
