module game;

import :reflection_probes;

import std.compat;
import assertions;

import render;
import render_scene;
import rhcomponents;
import glm;
import rhmath;
import name;
import profile;
import debug_draw;
import :names;

import log;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogReflectionProbes, Display);

namespace
{
    // sun change that makes the probes dirty (auto_rebake)
    constexpr float sun_direction_threshold_cos = 0.99985f;   // ~1 degree
    constexpr float sun_color_threshold = 0.02f;              // relative
    // sky change that makes them dirty: its average radiance (relative), the cloud coverage
    constexpr float sky_ambient_threshold = 0.05f;
    constexpr float cloud_coverage_threshold = 0.05f;

    float distance_to_box(glm::vec3 point, glm::vec3 box_min, glm::vec3 box_max)
    {
        const glm::vec3 d = glm::max(glm::max(box_min - point, point - box_max), glm::vec3(0.0f));
        return glm::length(d);
    }

    bool inside_box(glm::vec3 point, glm::vec3 box_min, glm::vec3 box_max)
    {
        return point.x >= box_min.x && point.y >= box_min.y && point.z >= box_min.z
            && point.x <= box_max.x && point.y <= box_max.y && point.z <= box_max.z;
    }

    bool sun_changed(const glm::vec3& baked_direction, const glm::vec3& baked_color,
        const glm::vec3& direction, const glm::vec3& color)
    {
        if (glm::dot(baked_direction, direction) < sun_direction_threshold_cos)
            return true;
        const float scale = std::max(glm::length(baked_color), 1e-3f);
        return glm::length(color - baked_color) / scale > sun_color_threshold;
    }

    bool sky_changed(const glm::vec4& baked, const glm::vec4& sky)
    {
        const float scale = std::max(glm::length(glm::vec3(baked)), 1e-3f);
        return glm::length(glm::vec3(sky) - glm::vec3(baked)) / scale > sky_ambient_threshold
            || std::abs(sky.w - baked.w) > cloud_coverage_threshold;
    }
}

void ReflectionProbeSystem::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    reflection_resource = renderer->find_resource("reflection");
    probe_capture_resource = renderer->find_resource("probe_capture");
    sky_resource = renderer->find_resource("sky");
    light_resource = renderer->find_resource("light");
    shadow_resource = renderer->find_resource("shadow");
    mesh_table_resource = renderer->find_resource("mesh_table");
    primitive_table_resource = renderer->find_resource("primitive_table");
    pbr_material_table_resource = renderer->find_resource("pbr_material_table");
    textures_resource = renderer->find_resource("textures");
    checkf(reflection_resource && probe_capture_resource && sky_resource, "reflection probe resources are missing (assets/render/resources)");

    auto probe_model = renderer->find_model("reflection_probe");
    checkf(probe_model, "material model 'reflection_probe' is missing (assets/render/schemas/reflection_probe.json)");
    capture_family = renderer->query_pipeline_family("ProbeCapture", probe_model);
    sky_family = renderer->query_pipeline_family("ProbeSky", probe_model);
    prefilter_family = renderer->query_pipeline_family("ProbePrefilter", probe_model);
    irradiance_family = renderer->query_pipeline_family("ProbeIrradiance", probe_model);
    blend_family = renderer->query_pipeline_family("ProbeBlend", probe_model);
    pbr_base_family = renderer->query_pipeline_family(Names::pass_geometry_base, renderer->find_model("pbr"));

    for (uint32_t index = 0; index < capture_colors.size(); ++index)
    {
        capture_colors[index] = backend->create_image({
            .name = "probe_capture_color[" + std::to_string(index) + "]",
            .extent = { capture_size, capture_size },
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled |
                RenderTextureUsage::TransferSrc | RenderTextureUsage::TransferDst,
            .mip_levels = specular_mips,
            .num_layers = 6,
            .is_cubemap = true,
        });
    }
    sky_capture = backend->create_image({
        .name = "probe_capture_sky",
        .extent = { capture_size, capture_size },
        .format = TextureFormat::RGBA16F,
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled |
            RenderTextureUsage::TransferSrc | RenderTextureUsage::TransferDst,
        .mip_levels = specular_mips,
        .num_layers = 6,
        .is_cubemap = true,
    });
    capture_depth = backend->create_image({
        .name = "probe_capture_depth",
        .extent = { capture_size, capture_size },
        .format = TextureFormat::Depth32F,
        .usage = RenderTextureUsage::DepthStencil,
    });

    scratch_specular = backend->create_image({
        .name = "probe_scratch_specular",
        .extent = { specular_size, specular_size },
        .format = TextureFormat::RGBA16F,
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .mip_levels = specular_mips,
        .num_layers = 6,
        .is_cubemap = true,
    });
    scratch_irradiance = backend->create_image({
        .name = "probe_scratch_irradiance",
        .extent = { irradiance_size, irradiance_size },
        .format = TextureFormat::RGBA16F,
        .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
        .mip_levels = 1,
        .num_layers = 6,
        .is_cubemap = true,
    });

    for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
    {
        specular_images[slot] = backend->create_image({
            .name = "probe_specular[" + std::to_string(slot) + "]",
            .extent = { specular_size, specular_size },
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
            .mip_levels = specular_mips,
            .num_layers = 6,
            .is_cubemap = true,
        });
        irradiance_images[slot] = backend->create_image({
            .name = "probe_irradiance[" + std::to_string(slot) + "]",
            .extent = { irradiance_size, irradiance_size },
            .format = TextureFormat::RGBA16F,
            .usage = RenderTextureUsage::ColorAttachment | RenderTextureUsage::Sampled,
            .mip_levels = 1,
            .num_layers = 6,
            .is_cubemap = true,
        });
    }
}

glm::mat4 ReflectionProbeSystem::face_view_proj(uint32_t face, glm::vec3 position)
{
    // basis of each face: texel (u, v) of the face sees forward + u' * right + v' * down (u', v' in [-1, 1],
    // v down), the Vulkan cubemap convention. Must match probe_face_uv_to_dir (resources/probe_capture.glsl).
    static constexpr glm::vec3 forward[6] = { {1, 0, 0}, {-1, 0, 0}, {0, 1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1} };
    static constexpr glm::vec3 right[6] = { {0, 0, -1}, {0, 0, 1}, {1, 0, 0}, {1, 0, 0}, {1, 0, 0}, {-1, 0, 0} };
    static constexpr glm::vec3 down[6] = { {0, -1, 0}, {0, -1, 0}, {0, 0, 1}, {0, 0, -1}, {0, -1, 0}, {0, -1, 0} };

    const glm::vec3 F = forward[face];
    const glm::vec3 R = right[face];
    const glm::vec3 D = down[face];

    // 90 degree perspective, Vulkan clip space: x right, y down, depth 0..1 from near to far
    const float a = capture_far / (capture_far - capture_near);
    const float b = -capture_far * capture_near / (capture_far - capture_near);

    glm::mat4 m(0.0f);   // m[column][row]
    for (int c = 0; c < 3; ++c)
    {
        m[c][0] = R[c];
        m[c][1] = D[c];
        m[c][2] = a * F[c];
        m[c][3] = F[c];
    }
    m[3][0] = -glm::dot(R, position);
    m[3][1] = -glm::dot(D, position);
    m[3][2] = -a * glm::dot(F, position) + b;
    m[3][3] = -glm::dot(F, position);
    return m;
}

void ReflectionProbeSystem::prepare(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs)
{
    PROFILE("ReflectionProbeSystem::prepare");

    current_time = inputs.time;
    frame_work = {};

    // pipelines are requested every frame: hot reload drops the PSO cache
    capture_pipeline = capture_family->request_pipeline({});
    sky_pipeline = sky_family->request_pipeline({});
    prefilter_pipeline = prefilter_family->request_pipeline({});
    irradiance_pipeline = irradiance_family->request_pipeline({});
    blend_pipeline = blend_family->request_pipeline({});

    gather_lights(scene_view);
    update_slots(scene_view, inputs);
    if (cv_probes_enabled.get())
    {
        plan_work(scene_view, inputs);
    }
    else
    {
        job.reset();
        fade.reset();
    }
    write_descriptors(ctx, scene_view, inputs);

    if (cv_probes_show_volumes.get())
        draw_debug_volumes(scene_view);
}

void ReflectionProbeSystem::gather_lights(SceneView& scene_view)
{
    probe_lights.sun_visible = false;
    probe_lights.sun_direction = glm::vec3(0.0f, 1.0f, 0.0f);
    probe_lights.sun_color = glm::vec3(0.0f);
    probe_lights.points.clear();

    uint64_t hash = 0xcbf29ce484222325ull;
    auto hash_float = [&hash] (float value)
    {
        uint32_t bits;
        std::memcpy(&bits, &value, sizeof(bits));
        hash = (hash ^ bits) * 0x100000001b3ull;
    };

    for (const RenderObject_Light& light : scene_view.get_processor<SceneViewProcessor_Light>().lights)
    {
        if (!light.visible_in_reflection_probes)
            continue;
        const glm::vec3 color = glm::vec3(light.color);
        if (light.type == LightType::directional)
        {
            // the main view's sun (light_ubo.dir_light, the one with the shadow map)
            if (!probe_lights.sun_visible)
            {
                probe_lights.sun_visible = true;
                probe_lights.sun_direction = -glm::normalize(light.direction);
                probe_lights.sun_color = color;
            }
            continue;
        }
        if (color.x <= 0.0f && color.y <= 0.0f && color.z <= 0.0f)
            continue;
        probe_lights.points.push_back({ light.position, color });
        for (int c = 0; c < 3; ++c)
        {
            hash_float(light.position[c]);
            hash_float(color[c]);
        }
    }
    probe_lights.points_hash = hash;
}

void ReflectionProbeSystem::update_slots(SceneView& scene_view, const FrameInputs& inputs)
{
    const auto& captures = scene_view.get_processor<SceneViewProcessor_ReflectionCapture>().captures;

    if (captures.size() > sky_slot && !warned_slot_overflow)
    {
        warned_slot_overflow = true;
        LogReflectionProbes.Log("%zu reflection captures, only the first %u slots are used",
            captures.size(), sky_slot);
    }

    const bool rebake_all = rebake_all_requested;
    rebake_all_requested = false;

    for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
    {
        SlotState& state = slots[slot];
        if (slot == sky_slot)
        {
            static const Name sky_name("sky");
            state.active = cv_probes_sky.get();
            state.name = sky_name;
            state.force = state.force || rebake_all;
            const bool auto_rebake = cv_probes_auto_rebake.get();
            const bool sun_differs = auto_rebake &&
                sun_changed(state.baked_sun_direction, state.baked_sun_color, probe_lights.sun_direction, probe_lights.sun_color);
            const bool sky_differs = auto_rebake && sky_changed(state.baked_sky, glm::vec4(inputs.sky_ambient, inputs.cloud_coverage));
            state.valid = state.valid && state.active;
            state.dirty = state.active && (!state.valid || state.force || sun_differs || sky_differs);
            state.dirty_reason = !state.valid ? "first bake" : state.force ? "requested" : sun_differs ? "sun changed" : "sky changed";
            continue;
        }
        const RenderObject_ReflectionCapture* capture = slot < captures.size() ? &captures[slot] : nullptr;

        state.active = capture && capture->registered && capture->active;
        if (!state.active)
        {
            state.valid = false;
            state.dirty = false;
            state.force = false;
            state.camera_inside = false;
            if (job && job->slot == slot)
                job.reset();
            if (fade && fade->slot == slot)
                fade.reset();
            continue;
        }

        state.name = capture->debug_name;

        if (rebake_all)
            state.force = true;

        const bool inside = inside_box(inputs.camera_position, capture->box_min, capture->box_max);
        if (inside && !state.camera_inside && capture->rebake_on_enter && state.valid)
            state.force = true;
        state.camera_inside = inside;

        const bool moved = state.baked_version != capture->capture_version;
        const bool interval_elapsed = capture->update_interval > 0.0f &&
            current_time - state.bake_time >= capture->update_interval;
        const bool auto_rebake = cv_probes_auto_rebake.get();
        const bool sun_differs = auto_rebake &&
            sun_changed(state.baked_sun_direction, state.baked_sun_color, probe_lights.sun_direction, probe_lights.sun_color);
        const bool lights_differ = auto_rebake && state.baked_lights_hash != probe_lights.points_hash;
        const bool sky_differs = auto_rebake && sky_changed(state.baked_sky, glm::vec4(inputs.sky_ambient, inputs.cloud_coverage));

        state.dirty = !state.valid || moved || state.force || interval_elapsed || sun_differs || lights_differ || sky_differs;
        state.dirty_reason = !state.valid ? "first bake"
            : moved ? "moved"
            : state.force ? "requested"
            : sun_differs ? "sun changed"
            : lights_differ ? "lights changed"
            : sky_differs ? "sky changed"
            : interval_elapsed ? "update interval"
            : "";
    }
}

bool ReflectionProbeSystem::plan_sky(const FrameInputs& inputs)
{
    if (sky_cooldown > 0)
        sky_cooldown--;

    SlotState& state = slots[sky_slot];
    // a sky nobody has seen yet does not wait
    if (!state.active || !state.dirty || (state.valid && sky_cooldown > 0))
        return false;

    // lighting driven updates run all the time under an animated sun: not logged
    if (!state.valid || state.force)
        LogReflectionProbes.Log("Baking reflection probe 'sky' (slot %u): %s", sky_slot, state.dirty_reason);

    frame_work.sky = true;

    state.valid = true;
    state.baked_position = inputs.camera_position;
    state.baked_sun_direction = probe_lights.sun_direction;
    state.baked_sun_color = probe_lights.sun_color;
    state.baked_sky = glm::vec4(inputs.sky_ambient, inputs.cloud_coverage);
    state.bake_time = current_time;
    state.bake_count++;
    state.dirty = false;
    state.force = false;

    sky_cooldown = (uint32_t)std::clamp(cv_probes_sky_interval.get(), 2, 60);
    return true;
}

void ReflectionProbeSystem::plan_work(SceneView& scene_view, const FrameInputs& inputs)
{
    // the capture UBO and the filter source are one per frame: the probes of the scene wait for the sky
    if (plan_sky(inputs))
        return;

    const auto& captures = scene_view.get_processor<SceneViewProcessor_ReflectionCapture>().captures;

    // the probe moved while it was being captured: start over from its new position
    if (job && captures[job->slot].capture_version != job->capture_version)
        job.reset();

    // ---- a finished capture starts filtering into its slot (one filter at a time: the source descriptor) ----
    if (job && job->next_face == 6 && !fade)
    {
        SlotState& state = slots[job->slot];
        const uint32_t blend_frames = (uint32_t)std::clamp(cv_probes_blend_frames.get(), 1, 600);
        const uint32_t updates = std::max(blend_frames / fade_update_interval, 1u);
        fade = Fade{
            .slot = job->slot,
            .capture_index = job->capture_index,
            // a slot which showed nothing yet is written at once
            .blend = state.valid && updates > 1,
            .steps = updates,
        };

        // the slot shows (the start of) the new capture from this frame on
        state.valid = true;
        state.baked_version = job->capture_version;
        state.baked_position = job->position;
        state.baked_sun_direction = job->sun_direction;
        state.baked_sun_color = job->sun_color;
        state.baked_sky = job->sky;
        state.baked_lights_hash = job->lights_hash;
        state.bake_time = current_time;
        state.bake_count++;
        state.dirty = false;
        job.reset();
    }

    // ---- this frame's filter / crossfade update ----
    if (fade && !fade->filtered)
    {
        frame_work.filter = true;
        frame_work.filter_to_scratch = fade->blend;
        frame_work.filter_slot = fade->slot;
        frame_work.filter_capture_index = fade->capture_index;
        fade->filtered = true;
        fade->cooldown = fade_update_interval - 1;
        if (!fade->blend)
            fade.reset();
    }
    else if (fade && fade->cooldown > 0)
    {
        fade->cooldown--;
    }
    else if (fade)
    {
        // weight 1 / (updates left) moves the slot linearly from the old capture to the new one
        frame_work.blend = true;
        frame_work.blend_slot = fade->slot;
        frame_work.blend_alpha = 1.0f / float(fade->steps - fade->step);
        fade->cooldown = fade_update_interval - 1;
        if (++fade->step >= fade->steps)
            fade.reset();
    }

    // ---- next capture ----
    if (!job)
    {
        // unbaked probes first (they show the sky ambient meanwhile), nearest first. Then the dirty probes by
        // staleness over distance: near probes refresh more often, far ones still get their turn while the
        // lighting keeps changing (animated sun)
        std::optional<uint32_t> best;
        float best_score = -1.0f;
        for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
        {
            // the sky probe has its own path (plan_sky)
            if (slot == sky_slot)
                continue;
            const SlotState& state = slots[slot];
            if (!state.active || !state.dirty)
                continue;
            // still fading in: its capture cube is in use
            if (fade && fade->slot == slot)
                continue;

            const float distance = distance_to_box(inputs.camera_position, captures[slot].box_min, captures[slot].box_max);
            const float score = !state.valid
                ? 1.0e6f / (1.0f + distance)
                : (float(current_time - state.bake_time) + 0.1f) / (1.0f + distance * 0.2f);
            if (score > best_score)
            {
                best = slot;
                best_score = score;
            }
        }

        if (!best)
            return;

        job = Job{
            .slot = *best,
            .capture_version = captures[*best].capture_version,
            .next_face = 0,
            .position = captures[*best].position,
            .sun_direction = probe_lights.sun_direction,
            .sun_color = probe_lights.sun_color,
            .sky = glm::vec4(inputs.sky_ambient, inputs.cloud_coverage),
            .lights_hash = probe_lights.points_hash,
            // not the cube a pending filter still reads
            .capture_index = fade ? 1u - fade->capture_index : 0u,
        };
        // lighting driven rebakes run all the time under an animated sun: not logged
        const SlotState& picked = slots[*best];
        if (!picked.valid || picked.baked_version != job->capture_version || picked.force)
        {
            LogReflectionProbes.Log("Baking reflection probe '%s' (slot %u): %s",
                picked.name.to_string().c_str(), *best, picked.dirty_reason);
        }
        // the request is taken: changes from now on make the probe dirty again
        slots[*best].force = false;
    }

    if (job->next_face < 6)
    {
        // a probe nobody has seen yet is captured at once, rebakes of shown probes are spread over frames
        const uint32_t budget = slots[job->slot].valid
            ? (uint32_t)std::clamp(cv_probes_faces_per_frame.get(), 1, 6)
            : 6u;

        frame_work.slot = job->slot;
        frame_work.capture_index = job->capture_index;
        frame_work.position = job->position;
        frame_work.first_face = job->next_face;
        frame_work.face_count = std::min(budget, 6u - job->next_face);
        job->next_face += frame_work.face_count;
    }
}

void ReflectionProbeSystem::write_descriptors(RenderGraphContext& ctx, SceneView& scene_view, const FrameInputs& inputs)
{
    const auto& captures = scene_view.get_processor<SceneViewProcessor_ReflectionCapture>().captures;
    const bool enabled = cv_probes_enabled.get();

    // ---- probes read by the lighting ----
    ReflectionProbesUBO probes_ubo{};
    uint32_t count = 0;

    for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
    {
        const SlotState& state = slots[slot];
        if (!enabled || !state.active || !state.valid || slot == sky_slot)
            continue;

        const RenderObject_ReflectionCapture& capture = captures[slot];
        GPUReflectionProbe& probe = probes_ubo.probes[slot];
        // parallax is relative to where the content was captured (the probe may have moved since)
        probe.capture_position = glm::vec4(state.baked_position, 1.0f);
        probe.box_min = glm::vec4(capture.box_min, capture.blend_distance);
        probe.box_max = glm::vec4(capture.box_max, capture.intensity);
        count = slot + 1;
    }

    const bool sky_baked = enabled && slots[sky_slot].active && slots[sky_slot].valid;
    probes_ubo.info = glm::uvec4(count, sky_baked ? sky_slot + 1 : 0u, specular_mips, cv_probes_parallax.get() ? 1u : 0u);
    probes_ubo.sky_ambient = glm::vec4(inputs.sky_ambient, std::max(cv_probes_sky_distance.get(), 0.1f));
    probes_ubo.intensity = glm::vec4(
        cv_probes_diffuse.get() ? cv_probes_diffuse_intensity.get() : 0.0f,
        cv_probes_specular.get() ? cv_probes_specular_intensity.get() : 0.0f,
        0.0f, 0.0f);
    probes_ubo.ssr_params = glm::vec4(cv_ssr_enabled.get() ? cv_ssr_intensity.get() : 0.0f, 0.0f, 0.0f, 0.0f);

    reflection_resource->update_uniform_buffer("reflection_probes_ubo", probes_ubo, ctx.frame);
    for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
    {
        reflection_resource->update_image("u_probe_irradiance", irradiance_images[slot],
            { .frame = ctx.frame, .cubemap = true, .array_index = slot });
        reflection_resource->update_image("u_probe_specular", specular_images[slot],
            { .frame = ctx.frame, .cubemap = true, .array_index = slot });
    }
    reflection_resource->update_image("u_brdf_lut", inputs.brdf_lut, { .frame = ctx.frame });

    // ---- capture / filter passes ----
    ProbeCaptureUBO capture_ubo{};
    const glm::vec3 capture_position = frame_work.face_count > 0 ? frame_work.position : inputs.camera_position;
    for (uint32_t face = 0; face < 6; ++face)
        capture_ubo.face_view_proj[face] = face_view_proj(face, capture_position);
    capture_ubo.capture_position = glm::vec4(capture_position, 1.0f);
    capture_ubo.filter_params = glm::vec4(float(capture_size), float(specular_mips), 0.0f, 0.0f);

    // static point lights nearest to the capture point
    std::vector<std::pair<glm::vec3, glm::vec3>> points = probe_lights.points;
    const size_t num_points = std::min<size_t>(points.size(), std::size(capture_ubo.light_position));
    std::partial_sort(points.begin(), points.begin() + num_points, points.end(),
        [&] (const auto& a, const auto& b)
        {
            return glm::length(a.first - capture_position) < glm::length(b.first - capture_position);
        });
    for (size_t index = 0; index < num_points; ++index)
    {
        capture_ubo.light_position[index] = glm::vec4(points[index].first, 1.0f);
        capture_ubo.light_color[index] = glm::vec4(points[index].second, 0.0f);
    }
    // no sun lobe in the sky probe: it is what mirrors show, the sun's highlight comes from the light itself
    capture_ubo.light_info = glm::uvec4((uint32_t)num_points, probe_lights.sun_visible && !frame_work.sky ? 1u : 0u, 0u, 0u);

    probe_capture_resource->update_uniform_buffer("probe_capture_ubo", capture_ubo, ctx.frame);
    // the filter reads the finished capture, the capture pass draws into the other one (the sky probe: its own
    // cube, rendered earlier in the same pass)
    probe_capture_resource->update_image("u_probe_source",
        frame_work.sky ? sky_capture : capture_colors[frame_work.filter_capture_index], { .frame = ctx.frame, .cubemap = true });
    probe_capture_resource->update_image("u_probe_scratch_specular", scratch_specular, { .frame = ctx.frame, .cubemap = true });
    probe_capture_resource->update_image("u_probe_scratch_irradiance", scratch_irradiance, { .frame = ctx.frame, .cubemap = true });
}

void ReflectionProbeSystem::draw_debug_volumes(SceneView& scene_view) const
{
    const auto& captures = scene_view.get_processor<SceneViewProcessor_ReflectionCapture>().captures;
    for (uint32_t slot = 0; slot < std::min<size_t>(captures.size(), kMaxReflectionProbes); ++slot)
    {
        const SlotState& state = slots[slot];
        if (!state.active)
            continue;
        const RenderObject_ReflectionCapture& capture = captures[slot];

        // green: baked, yellow: waiting for a rebake, red: being captured / fading in / never baked
        const bool baking = (job && job->slot == slot) || (fade && fade->slot == slot);
        const glm::vec4 color = baking || !state.valid ? glm::vec4(1.0f, 0.25f, 0.2f, 1.0f)
            : state.dirty ? glm::vec4(1.0f, 0.85f, 0.2f, 1.0f)
            : glm::vec4(0.3f, 1.0f, 0.45f, 1.0f);

        debug_draw::box(capture.box_min, capture.box_max, color);
        // end of the influence fade
        debug_draw::box(capture.box_min - glm::vec3(capture.blend_distance), capture.box_max + glm::vec3(capture.blend_distance),
            color * glm::vec4(1.0f, 1.0f, 1.0f, 0.35f));
        debug_draw::sphere(capture.position, 0.15f, color, { .depth_test = false });
    }
}

std::vector<ReflectionProbeSystem::ProbeStatus> ReflectionProbeSystem::get_status() const
{
    std::vector<ProbeStatus> result;
    for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
    {
        const SlotState& state = slots[slot];
        if (!state.active)
            continue;
        result.push_back({
            .name = state.name,
            .slot = slot,
            .active = state.active,
            .baked = state.valid,
            .dirty = state.dirty,
            .baking = (job && job->slot == slot) || (fade && fade->slot == slot),
            .bake_count = state.bake_count,
            .age_seconds = state.valid ? current_time - state.bake_time : 0.0,
        });
    }
    return result;
}

void ReflectionProbeSystem::transition(RenderGraphContext& ctx, RBImageHandle image, RBImageUsageType usage,
    uint32_t base_layer, uint32_t layer_count, uint32_t base_mip, uint32_t mip_count) const
{
    ImageBarrierParams params{};
    params.debug_pass_name = ctx.pass_name;
    params.image = image;
    params.dst_usage = usage;
    params.pass_type = RenderPassType::graphics;
    params.base_layer = base_layer;
    params.layer_count = layer_count;   // 0: all
    params.base_mip = base_mip;
    params.mip_count = mip_count;       // 0: all
    backend->transition_image(ctx.cmd, params);
}

void ReflectionProbeSystem::initialize_images(RenderGraphContext& ctx)
{
    // every slot is bound to the lighting from the first frame: it must be in a readable layout
    // (unbaked slots are never sampled, the UBO marks them invalid)
    for (RBImageHandle capture : capture_colors)
        transition(ctx, capture, RBImageUsageType::SampledFragment);
    transition(ctx, sky_capture, RBImageUsageType::SampledFragment);
    transition(ctx, scratch_specular, RBImageUsageType::SampledFragment);
    transition(ctx, scratch_irradiance, RBImageUsageType::SampledFragment);
    for (uint32_t slot = 0; slot < kMaxReflectionProbes; ++slot)
    {
        transition(ctx, specular_images[slot], RBImageUsageType::SampledFragment);
        transition(ctx, irradiance_images[slot], RBImageUsageType::SampledFragment);
    }
    images_initialized = true;
}

void ReflectionProbeSystem::execute(RenderGraphContext& ctx, SceneView& scene_view, DrawList& draw_list)
{
    PROFILE("ReflectionProbeSystem::execute");

    if (!images_initialized)
        initialize_images(ctx);

    if (frame_work.face_count == 0 && !frame_work.filter && !frame_work.blend && !frame_work.sky)
        return;

    // the sky probe: the whole update at once, no crossfade (the lighting follows the sky like it follows the sun)
    if (frame_work.sky)
    {
        for (uint32_t face = 0; face < 6; ++face)
            render_face(ctx, scene_view, draw_list, sky_capture, face, true);
        filter_capture(ctx, sky_capture, specular_images[sky_slot], irradiance_images[sky_slot]);
    }

    // the finished capture first: the faces below go to the other cube
    if (frame_work.filter)
    {
        const uint32_t slot = frame_work.filter_slot;
        const RBImageHandle source = capture_colors[frame_work.filter_capture_index];
        if (frame_work.filter_to_scratch)
            filter_capture(ctx, source, scratch_specular, scratch_irradiance);
        else
            filter_capture(ctx, source, specular_images[slot], irradiance_images[slot]);
    }
    if (frame_work.blend)
        blend_into_slot(ctx, frame_work.blend_slot, frame_work.blend_alpha);

    for (uint32_t face = frame_work.first_face; face < frame_work.first_face + frame_work.face_count; ++face)
        render_face(ctx, scene_view, draw_list, capture_colors[frame_work.capture_index], face, false);

    // later passes set their viewport only when it differs from the swapchain
    backend->update_viewport(ctx.cmd, {}, true);
}

void ReflectionProbeSystem::render_face(RenderGraphContext& ctx, SceneView& scene_view, DrawList& draw_list,
    RBImageHandle capture_color, uint32_t face, bool sky_only)
{
    PROFILE("ReflectionProbeSystem::render_face");

    transition(ctx, capture_color, RBImageUsageType::ColorAttachment, face, 1, 0, 1);
    transition(ctx, capture_depth, RBImageUsageType::DepthStencilAttachment);

    FramebufferDesc fb_desc{};
    fb_desc.pass = "ProbeCapture";
    fb_desc.attachments = {
        {
            .image_name = "probe_capture_color",
            .image = capture_color,
            .load = RBLoadOp::Clear,
            .store = RBStoreOp::Store,
            .usage = RBImageUsageType::ColorAttachment,
            .layer = face,
            .mip_level = 0,
        },
        {
            .image_name = "probe_capture_depth",
            .image = capture_depth,
            .load = RBLoadOp::Clear,
            .store = RBStoreOp::DontCare,
            .usage = RBImageUsageType::DepthStencilAttachment,
            .depth_attachment = true,
        },
    };
    fb_desc.update_extent({ capture_size, capture_size });

    backend->begin_render_pass(ctx.cmd, backend->get_or_create_framebuffer(fb_desc));
    backend->update_viewport(ctx.cmd, { capture_size, capture_size });
    // a new render pass: the pipeline must be bound again (RenderGraphContext::bind_pipeline)
    ctx.current_pipeline = nullptr;

    // ---- sky (the SkyRenderer wrote the "sky" resource of the frame) ----
    ctx.bind_pipeline(sky_pipeline);
    ctx.bind(probe_capture_resource, sky_resource);
    ctx.push_constants(ProbeFacePushConstants{ face, 0, 0.0f, 0, 1.0f });
    ctx.draw_fullscreen();

    if (sky_only)
    {
        backend->end_render_pass(ctx.cmd);
        transition(ctx, capture_color, RBImageUsageType::SampledFragment, face, 1, 0, 1);
        faces_rendered++;
        return;
    }

    // ---- static opaque geometry of the pbr model ----
    const glm::mat4 view_proj = face_view_proj(face, frame_work.position);
    const Frustum frustum = Frustum::from_view_projection(view_proj);

    // one indirect draw per index block of the meshes
    std::map<uint32_t, std::vector<const RenderPrimitive*>> by_block;
    for (const RenderPrimitive& prim : scene_view.get_processor<SceneViewProcessor_Mesh>().primitives)
    {
        // skinned meshes move: a probe keeps only what stays
        if (prim.is_skinned())
            continue;
        const RenderPrimitivePassInfo* info = prim.get_pass_info(Names::pass_geometry_base);
        if (!info || info->pipeline_family != pbr_base_family)
            continue;
        if (!frustum.test_aabb_world(prim.bounds))
            continue;
        by_block[prim.indices.block].push_back(&prim);
    }

    ctx.bind_pipeline(capture_pipeline);
    ctx.bind(probe_capture_resource, mesh_table_resource, primitive_table_resource, light_resource,
        shadow_resource, reflection_resource, pbr_material_table_resource, textures_resource);
    draw_list.bind(ctx);
    for (const auto& [block, primitives] : by_block)
    {
        const uint32_t first_command = draw_list.command_count();
        for (const RenderPrimitive* prim : primitives)
        {
            draw_list.add_draw({
                .mesh_id = (uint32_t)prim->mesh_index,
                .primitive_id = prim->id,
                .material_id = prim->get_pass_info(Names::pass_geometry_base)->material_index,
                .user = face,
            }, prim->indices);
        }
        draw_list.draw_indirect(ctx, block, first_command, draw_list.command_count() - first_command);
    }

    backend->end_render_pass(ctx.cmd);

    transition(ctx, capture_color, RBImageUsageType::SampledFragment, face, 1, 0, 1);
    faces_rendered++;
}

void ReflectionProbeSystem::draw_filter_face(RenderGraphContext& ctx, Name pass, RBLoadOp load, RBImageHandle target,
    Name target_name, uint32_t size, uint32_t face, uint32_t mip, PipelineObject* pipeline, const ProbeFacePushConstants& pc)
{
    FramebufferDesc fb_desc{};
    // render passes are cached by name: one name per load op
    fb_desc.pass = pass;
    fb_desc.attachments = {
        {
            .image_name = target_name,
            .image = target,
            .load = load,
            .store = RBStoreOp::Store,
            .usage = RBImageUsageType::ColorAttachment,
            .layer = face,
            .mip_level = mip,
        },
    };
    const Extent extent = { std::max(size >> mip, 1u), std::max(size >> mip, 1u) };
    fb_desc.update_extent(extent);

    backend->begin_render_pass(ctx.cmd, backend->get_or_create_framebuffer(fb_desc));
    backend->update_viewport(ctx.cmd, extent);
    // a new render pass: the pipeline must be bound again (RenderGraphContext::bind_pipeline)
    ctx.current_pipeline = nullptr;
    ctx.bind_pipeline(pipeline);
    ctx.bind(probe_capture_resource);
    ctx.push_constants(pc);
    ctx.draw_fullscreen();
    backend->end_render_pass(ctx.cmd);
}

void ReflectionProbeSystem::filter_capture(RenderGraphContext& ctx, RBImageHandle source, RBImageHandle specular,
    RBImageHandle irradiance)
{
    PROFILE("ReflectionProbeSystem::filter_capture");

    // source mips for filtered importance sampling
    backend->generate_mips(ctx.cmd, source);
    transition(ctx, source, RBImageUsageType::SampledFragment);

    transition(ctx, specular, RBImageUsageType::ColorAttachment);
    transition(ctx, irradiance, RBImageUsageType::ColorAttachment);

    for (uint32_t mip = 0; mip < specular_mips; ++mip)
    {
        const float roughness = float(mip) / float(specular_mips - 1);
        for (uint32_t face = 0; face < 6; ++face)
        {
            draw_filter_face(ctx, "ProbeFilter", RBLoadOp::DontCare, specular, "probe_specular", specular_size, face, mip,
                prefilter_pipeline, ProbeFacePushConstants{ face, mip, roughness, prefilter_samples, 1.0f });
        }
    }
    for (uint32_t face = 0; face < 6; ++face)
    {
        draw_filter_face(ctx, "ProbeFilter", RBLoadOp::DontCare, irradiance, "probe_irradiance", irradiance_size, face, 0,
            irradiance_pipeline, ProbeFacePushConstants{ face, 0, 1.0f, irradiance_samples, 1.0f });
    }

    transition(ctx, specular, RBImageUsageType::SampledFragment);
    transition(ctx, irradiance, RBImageUsageType::SampledFragment);
}

void ReflectionProbeSystem::blend_into_slot(RenderGraphContext& ctx, uint32_t slot, float alpha)
{
    PROFILE("ReflectionProbeSystem::blend_into_slot");

    // the slot keeps its content (load) and the filtered new capture (scratch) is alpha blended over it
    transition(ctx, specular_images[slot], RBImageUsageType::ColorAttachment);
    transition(ctx, irradiance_images[slot], RBImageUsageType::ColorAttachment);

    // probe_blend.frag: sample_count selects the scratch cube (0: specular at `mip`, 1: irradiance)
    for (uint32_t mip = 0; mip < specular_mips; ++mip)
    {
        for (uint32_t face = 0; face < 6; ++face)
        {
            draw_filter_face(ctx, "ProbeBlend", RBLoadOp::Load, specular_images[slot], "probe_specular", specular_size,
                face, mip, blend_pipeline, ProbeFacePushConstants{ face, mip, 0.0f, 0, alpha });
        }
    }
    for (uint32_t face = 0; face < 6; ++face)
    {
        draw_filter_face(ctx, "ProbeBlend", RBLoadOp::Load, irradiance_images[slot], "probe_irradiance", irradiance_size,
            face, 0, blend_pipeline, ProbeFacePushConstants{ face, 0, 0.0f, 1, alpha });
    }

    transition(ctx, specular_images[slot], RBImageUsageType::SampledFragment);
    transition(ctx, irradiance_images[slot], RBImageUsageType::SampledFragment);
}
