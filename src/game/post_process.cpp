module;

#include <json/reader.h>
#include <json/value.h>
#include <json/writer.h>

module game;

import :post_process;

import std.compat;
import assertions;

import render;
import glm;
import name;
import cvar;
import rhmath;
import paths;
import profile;

import log;
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogPostProcess, Display);

namespace
{
    // local size of bloom_downsample.comp and bloom_upsample.comp
    constexpr uint32_t group = 8;

    ComputeWorkgroups groups_for(Extent extent)
    {
        return { (extent.width + group - 1) / group, (extent.height + group - 1) / group, 1 };
    }

    // row major 3 x 3: the shader dots each row with the color
    struct Mat3
    {
        std::array<glm::vec3, 3> rows;

        static Mat3 make(glm::vec3 r0, glm::vec3 r1, glm::vec3 r2)
        {
            Mat3 m;
            m.rows = { r0, r1, r2 };
            return m;
        }

        static Mat3 identity()
        {
            return make({ 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, { 0.0f, 0.0f, 1.0f });
        }

        static Mat3 diagonal(glm::vec3 d)
        {
            return make({ d.x, 0.0f, 0.0f }, { 0.0f, d.y, 0.0f }, { 0.0f, 0.0f, d.z });
        }

        Mat3 operator*(const Mat3& b) const
        {
            Mat3 result;
            for (int i = 0; i < 3; ++i)
                for (int j = 0; j < 3; ++j)
                    result.rows[i][j] = rows[i][0] * b.rows[0][j] + rows[i][1] * b.rows[1][j] + rows[i][2] * b.rows[2][j];
            return result;
        }
    };

    const glm::vec3 luma_weights(0.2126f, 0.7152f, 0.0722f);

    // color pickers show display values: the HDR math wants them linear
    glm::vec3 to_linear(glm::vec3 c)
    {
        return { std::pow(std::max(c.x, 0.0f), 2.2f), std::pow(std::max(c.y, 0.0f), 2.2f), std::pow(std::max(c.z, 0.0f), 2.2f) };
    }

    // White balance as in Unity (ColorUtils.ColorBalanceToLMSCoeffs): the white point moves along the
    // daylight locus (temperature) and across it (tint), the color is scaled in LMS cone space so that this
    // white point becomes D65. temperature, tint: -100..100
    Mat3 white_balance(float temperature, float tint)
    {
        const float t1 = temperature / 65.0f;
        const float t2 = tint / 65.0f;
        // CIE xy of the white point
        const float x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
        const float standard_illuminant_y = 2.87f * x - 3.0f * x * x - 0.27509507f;
        const float y = standard_illuminant_y + t2 * 0.05f;

        // xy -> XYZ (Y = 1) -> LMS (CIECAT02)
        const float X = x / y;
        const float Y = 1.0f;
        const float Z = (1.0f - x - y) / y;
        const glm::vec3 lms(
            0.7328f * X + 0.4296f * Y - 0.1624f * Z,
            -0.7036f * X + 1.6975f * Y + 0.0061f * Z,
            0.0030f * X + 0.0136f * Y + 0.9834f * Z);
        // D65 in LMS
        const glm::vec3 d65(0.949237f, 1.03542f, 1.08728f);
        const glm::vec3 coefficients = d65 / lms;

        const Mat3 linear_to_lms = Mat3::make(
            { 3.90405e-1f, 5.49941e-1f, 8.92632e-3f },
            { 7.08416e-2f, 9.63172e-1f, 1.35775e-3f },
            { 2.31082e-2f, 1.28021e-1f, 9.36245e-1f });
        const Mat3 lms_to_linear = Mat3::make(
            { 2.85847e+0f, -1.62879e+0f, -2.48910e-2f },
            { -2.10182e-1f, 1.15820e+0f, 3.24281e-4f },
            { -4.18120e-2f, -1.18169e-1f, 1.06867e+0f });
        return lms_to_linear * Mat3::diagonal(coefficients) * linear_to_lms;
    }

    // rotation of the hues around the gray axis (the hue-rotate filter of CSS)
    Mat3 hue_rotation(float degrees)
    {
        const float angle = glm::radians(degrees);
        const float c = std::cos(angle);
        const float s = std::sin(angle);
        return Mat3::make(
            { 0.213f + c * 0.787f - s * 0.213f, 0.715f - c * 0.715f - s * 0.715f, 0.072f - c * 0.072f + s * 0.928f },
            { 0.213f - c * 0.213f + s * 0.143f, 0.715f + c * 0.285f + s * 0.140f, 0.072f - c * 0.072f - s * 0.283f },
            { 0.213f - c * 0.213f - s * 0.787f, 0.715f - c * 0.715f + s * 0.715f, 0.072f + c * 0.928f + s * 0.072f });
    }

    // lerp between the luminance (gray) and the color
    Mat3 saturation(float amount)
    {
        Mat3 m;
        for (int i = 0; i < 3; ++i)
            for (int j = 0; j < 3; ++j)
                m.rows[i][j] = (1.0f - amount) * luma_weights[j] + (i == j ? amount : 0.0f);
        return m;
    }
}

/************************************************************************
 * RENDERER
 ***********************************************************************/

void PostProcessRenderer::init(Renderer& in_renderer, RenderBackend& in_backend)
{
    renderer = &in_renderer;
    backend = &in_backend;

    resource = renderer->find_resource("post_process");
    checkf(resource, "resource 'post_process' is missing (assets/render/resources/post_process.json)");

    auto model = renderer->find_model("post_process");
    checkf(model, "material model 'post_process' is missing (assets/render/schemas/post_process.json)");
    downsample_family = renderer->query_pipeline_family("BloomDownsample", model);
    upsample_family = renderer->query_pipeline_family("BloomUpsample", model);
    exposure_family = renderer->query_pipeline_family("AutoExposure", model);
}

bool PostProcessRenderer::bloom_enabled() const
{
    return cv_post_enabled.get() && cv_post_bloom_intensity.get() > 0.0f;
}

bool PostProcessRenderer::auto_exposure_enabled() const
{
    return cv_post_exposure_mode.get() == ExposureMode::Auto;
}

void PostProcessRenderer::prepare(RenderGraphContext& ctx, const FrameInputs& inputs)
{
    PROFILE("PostProcessRenderer::prepare");

    // pipelines are requested every frame: hot reload drops the PSO cache
    downsample_pipeline = downsample_family->request_pipeline({});
    upsample_pipeline = upsample_family->request_pipeline({});
    exposure_pipeline = exposure_family->request_pipeline({});

    const UpdateImageParams params{ .frame = ctx.frame };
    for (uint32_t level = 0; level < bloom_levels; ++level)
    {
        resource->update_image("u_pp_source", level == 0 ? inputs.scene : inputs.down[level - 1], params, level);
        resource->update_image("u_pp_target", inputs.down[level], params, level);
    }
    for (uint32_t level = 0; level + 1 < bloom_levels; ++level)
    {
        // the smallest up level starts from the smallest down level
        const RBImageHandle low = level + 2 == bloom_levels ? inputs.down[bloom_levels - 1] : inputs.up[level + 1];
        resource->update_image("u_pp_source", low, params, up_instance(level));
        resource->update_image("u_pp_source_add", inputs.down[level], params, up_instance(level));
        resource->update_image("u_pp_target", inputs.up[level], params, up_instance(level));
    }
    resource->update_image("u_pp_source", inputs.down[exposure_level], params, exposure_instance);
    resource->update_image("u_pp_exposure_target", inputs.exposure, params, exposure_instance);

    resource->update_image("u_pp_bloom", inputs.up[0], params, tonemap_instance);
    resource->update_image("u_pp_exposure", inputs.exposure, params, tonemap_instance);

    ++frame;
    delta_seconds = inputs.delta_seconds;
    const bool is_auto = auto_exposure_enabled();
    if (is_auto && !was_auto)
        exposure_reset = true;
    was_auto = is_auto;

    resource->update_uniform_buffer("post_process_ubo", build_ubo(inputs.screen), ctx.frame, tonemap_instance);
}

PostProcessUBO PostProcessRenderer::build_ubo(Extent screen) const
{
    const bool enabled = cv_post_enabled.get();
    const bool is_auto = auto_exposure_enabled();
    const float min_ev = std::min(cv_post_exposure_min_ev.get(), cv_post_exposure_max_ev.get());
    const float max_ev = std::max(cv_post_exposure_min_ev.get(), cv_post_exposure_max_ev.get());

    PostProcessUBO ubo{};
    ubo.exposure = glm::vec4(is_auto ? 1.0f : 0.0f, cv_post_exposure_ev.get(), cv_post_exposure_compensation.get(),
        cv_post_show_histogram.get() && is_auto ? 1.0f : 0.0f);
    ubo.auto_exposure = glm::vec4(min_ev, max_ev, std::log2(std::max(cv_post_exposure_key.get(), 0.001f)), 0.0f);
    ubo.histogram = glm::vec4(histogram_log_min, histogram_log_range, 0.0f, 0.0f);
    ubo.bloom = glm::vec4(to_linear(cv_post_bloom_tint.get()), bloom_enabled() ? std::clamp(cv_post_bloom_intensity.get(), 0.0f, 1.0f) : 0.0f);

    Mat3 balance = Mat3::identity();
    Mat3 grade = Mat3::identity();
    if (enabled)
    {
        balance = Mat3::diagonal(to_linear(cv_post_color_filter.get()))
            * white_balance(std::clamp(cv_post_temperature.get(), -100.0f, 100.0f), std::clamp(cv_post_tint.get(), -100.0f, 100.0f));
        grade = saturation(std::max(cv_post_saturation.get(), 0.0f)) * hue_rotation(cv_post_hue_shift.get());
    }
    ubo.balance_r = glm::vec4(balance.rows[0], 0.0f);
    ubo.balance_g = glm::vec4(balance.rows[1], 0.0f);
    ubo.balance_b = glm::vec4(balance.rows[2], 0.0f);
    ubo.grade_r = glm::vec4(grade.rows[0], 0.0f);
    ubo.grade_g = glm::vec4(grade.rows[1], 0.0f);
    ubo.grade_b = glm::vec4(grade.rows[2], 0.0f);

    ubo.tone = glm::vec4(
        enabled ? std::clamp(cv_post_contrast.get(), 0.1f, 4.0f) : 1.0f,
        (float)cv_post_tonemapper.get(),
        1.0f / std::clamp(cv_post_gamma.get(), 0.5f, 5.0f),
        enabled ? (float)std::clamp(cv_post_posterize.get(), 0, 256) : 0.0f);

    const glm::vec3 midtones = cv_post_midtones.get();
    ubo.lift = glm::vec4(enabled ? cv_post_lift.get() : glm::vec3(0.0f), 0.0f);
    ubo.gamma = enabled
        ? glm::vec4(1.0f / std::max(midtones.x, 0.05f), 1.0f / std::max(midtones.y, 0.05f), 1.0f / std::max(midtones.z, 0.05f), 0.0f)
        : glm::vec4(1.0f);
    ubo.gain = glm::vec4(enabled ? cv_post_gain.get() : glm::vec3(1.0f), 0.0f);

    // 0.5 is the neutral color of the soft light blend
    const float shadows_strength = enabled ? std::clamp(cv_post_split_shadows_strength.get(), 0.0f, 1.0f) : 0.0f;
    const float highlights_strength = enabled ? std::clamp(cv_post_split_highlights_strength.get(), 0.0f, 1.0f) : 0.0f;
    ubo.split_shadows = glm::vec4(glm::mix(glm::vec3(0.5f), cv_post_split_shadows.get(), shadows_strength),
        std::clamp(cv_post_split_balance.get(), -1.0f, 1.0f));
    ubo.split_highlights = glm::vec4(glm::mix(glm::vec3(0.5f), cv_post_split_highlights.get(), highlights_strength), 0.0f);

    // vignette as in Unity: intensity x 3, smoothness x 5
    ubo.vignette = glm::vec4(to_linear(cv_post_vignette_color.get()),
        enabled ? std::clamp(cv_post_vignette_intensity.get(), 0.0f, 1.0f) * 3.0f : 0.0f);
    ubo.lens = glm::vec4(
        std::clamp(cv_post_vignette_smoothness.get(), 0.01f, 1.0f) * 5.0f,
        std::clamp(cv_post_vignette_roundness.get(), 0.0f, 1.0f),
        enabled ? std::clamp(cv_post_chromatic_aberration.get(), 0.0f, 0.05f) : 0.0f,
        enabled ? std::clamp(cv_post_sharpen.get(), 0.0f, 1.0f) : 0.0f);
    ubo.grain = glm::vec4(
        enabled ? std::clamp(cv_post_grain_intensity.get(), 0.0f, 1.0f) : 0.0f,
        std::clamp(cv_post_grain_size.get(), 1.0f, 8.0f),
        std::clamp(cv_post_grain_response.get(), 0.0f, 1.0f),
        (float)(frame % 1024));

    const float width = (float)std::max(screen.width, 1u);
    const float height = (float)std::max(screen.height, 1u);
    ubo.screen = glm::vec4(width, height, 1.0f / width, 1.0f / height);
    return ubo;
}

void PostProcessRenderer::dispatch_downsample(RenderGraphContext& ctx, uint32_t level, Extent extent)
{
    PROFILE("PostProcessRenderer::dispatch_downsample");
    check(level < bloom_levels);

    ctx.bind_pipeline(downsample_pipeline);
    ctx.bind(resource->query_single(level));
    ctx.push_constants(BloomPushConstants{ .params = glm::vec4(level == 0 ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f) });
    ctx.compute(groups_for(extent));
}

void PostProcessRenderer::dispatch_upsample(RenderGraphContext& ctx, uint32_t level, Extent extent)
{
    PROFILE("PostProcessRenderer::dispatch_upsample");
    check(level + 1 < bloom_levels);

    ctx.bind_pipeline(upsample_pipeline);
    ctx.bind(resource->query_single(up_instance(level)));
    ctx.push_constants(BloomPushConstants{
        .params = glm::vec4(0.0f, std::clamp(cv_post_bloom_scatter.get(), 0.0f, 0.95f), 0.0f, 0.0f) });
    ctx.compute(groups_for(extent));
}

void PostProcessRenderer::dispatch_auto_exposure(RenderGraphContext& ctx)
{
    PROFILE("PostProcessRenderer::dispatch_auto_exposure");

    const float min_ev = std::min(cv_post_exposure_min_ev.get(), cv_post_exposure_max_ev.get());
    const float max_ev = std::max(cv_post_exposure_min_ev.get(), cv_post_exposure_max_ev.get());
    const float low = std::clamp(cv_post_exposure_low_percent.get(), 0.0f, 0.98f);
    const float high = std::clamp(cv_post_exposure_high_percent.get(), low + 0.01f, 1.0f);

    ctx.bind_pipeline(exposure_pipeline);
    ctx.bind(resource->query_single(exposure_instance));
    ctx.push_constants(AutoExposurePushConstants{
        .range = glm::vec4(min_ev, max_ev, std::log2(std::max(cv_post_exposure_key.get(), 0.001f)),
            std::clamp(delta_seconds, 0.0f, 0.25f)),
        .speed = glm::vec4(cv_post_exposure_speed_up.get(), cv_post_exposure_speed_down.get(), low, high),
        .histogram = glm::vec4(histogram_log_min, histogram_log_range,
            std::clamp(cv_post_exposure_center_weight.get(), 0.0f, 1.0f), exposure_reset ? 1.0f : 0.0f),
    });
    ctx.compute({ 1, 1, 1 });
    exposure_reset = false;
}

void PostProcessRenderer::bind_tonemap(RenderGraphContext& ctx)
{
    ctx.bind(resource->query_single(tonemap_instance));
}

/************************************************************************
 * PRESETS
 ***********************************************************************/

namespace
{
    std::vector<post_process::Preset> g_presets;
    bool g_presets_loaded = false;

    constexpr std::string_view settings_prefix = "render.post.";

    bool is_switch(std::string_view name)
    {
        return name == cv_post_enabled.get_name() || name == cv_post_preset.get_name()
            || name == cv_post_show_histogram.get_name();
    }

    // a JSON value of a preset as cvar text: numbers, booleans, strings, arrays of numbers ("x y z")
    std::optional<std::string> json_to_text(const Json::Value& value)
    {
        if (value.isBool())
            return value.asBool() ? "true" : "false";
        if (value.isNumeric())
            return std::format("{}", value.asDouble());
        if (value.isString())
            return value.asString();
        if (value.isArray())
        {
            std::string text;
            for (Json::ArrayIndex i = 0; i < value.size(); ++i)
            {
                if (!value[i].isNumeric())
                    return std::nullopt;
                text += std::format("{}{}", i ? " " : "", value[i].asDouble());
            }
            return text;
        }
        return std::nullopt;
    }

    // the cvar text as the JSON value json_to_text reads back
    Json::Value text_to_json(const std::string& text)
    {
        if (text == "true" || text == "false")
            return Json::Value(text == "true");

        std::vector<double> numbers;
        const char* it = text.data();
        const char* end = text.data() + text.size();
        while (it != end)
        {
            while (it != end && *it == ' ')
                ++it;
            if (it == end)
                break;
            double number = 0.0;
            const auto [next, error] = std::from_chars(it, end, number);
            if (error != std::errc{})
                return Json::Value(text);
            numbers.push_back(number);
            it = next;
        }
        if (numbers.size() == 1)
            return Json::Value(numbers[0]);
        if (numbers.size() > 1)
        {
            Json::Value array(Json::arrayValue);
            for (double number : numbers)
                array.append(number);
            return array;
        }
        return Json::Value(text);
    }

    std::optional<Json::Value> read_presets_file()
    {
        std::ifstream file(post_process::get_presets_path());
        if (!file)
            return std::nullopt;
        Json::Value root;
        Json::CharReaderBuilder builder;
        std::string errors;
        if (!Json::parseFromStream(builder, file, &root, &errors) || !root.isObject())
        {
            LogPostProcess.Log("Could not parse %s: %s", post_process::get_presets_path().string().c_str(), errors.c_str());
            return std::nullopt;
        }
        return root;
    }

    std::vector<std::string> preset_names()
    {
        std::vector<std::string> names;
        for (const post_process::Preset& preset : post_process::get_presets())
            names.push_back(preset.name);
        return names;
    }

    std::string join_preset_names()
    {
        std::string names;
        for (const std::string& name : preset_names())
            names += (names.empty() ? "" : ", ") + name;
        return names;
    }

    cvar::Command cmd_preset("post.preset", "Prints the post process presets / applies one (resets the other render.post.* settings)",
        [] (cvar::Args args)
        {
            if (args.empty())
            {
                cvar::print(std::format("preset {} (presets: {})", cv_post_preset.get(), join_preset_names()));
                return;
            }
            if (!post_process::apply_preset(args[0]))
                cvar::print(std::format("No post process preset '{}' (presets: {})", args[0], join_preset_names()), cvar::Output::error);
            else
                cvar::print(std::format("preset {}", args[0]));
        }, "[name]", [] (size_t arg_index) { return arg_index == 0 ? preset_names() : std::vector<std::string>{}; });

    cvar::Command cmd_reload_presets("post.presets.reload", "Reloads assets/render/post_process_presets.json",
        [] (cvar::Args)
        {
            post_process::reload_presets();
            cvar::print(std::format("{} presets: {}", post_process::get_presets().size(), join_preset_names()));
        });

    cvar::Command cmd_save_preset("post.preset.save", "Saves the render.post.* settings that differ from the defaults as a preset (replaces one with the name)",
        [] (cvar::Args args)
        {
            if (args.empty())
            {
                cvar::print("usage: post.preset.save <name> [description]", cvar::Output::error);
                return;
            }
            std::string description;
            for (size_t i = 1; i < args.size(); ++i)
                description += (i > 1 ? " " : "") + args[i];
            if (post_process::save_preset(args[0], description))
                cvar::print(std::format("Saved preset {} to {}", args[0], post_process::get_presets_path().string()));
            else
                cvar::print(std::format("Could not write {}", post_process::get_presets_path().string()), cvar::Output::error);
        }, "<name> [description]", [] (size_t arg_index) { return arg_index == 0 ? preset_names() : std::vector<std::string>{}; });
}

std::filesystem::path post_process::get_presets_path()
{
    return paths::get_assets_path() / "render" / "post_process_presets.json";
}

std::vector<cvar::Entry*> post_process::get_settings()
{
    std::vector<cvar::Entry*> settings;
    for (cvar::Entry* entry : cvar::get_entries())
        if (entry->get_name().starts_with(settings_prefix) && !is_switch(entry->get_name()))
            settings.push_back(entry);
    return settings;
}

void post_process::reload_presets()
{
    g_presets.clear();
    g_presets_loaded = true;

    const std::optional<Json::Value> root = read_presets_file();
    if (!root)
    {
        LogPostProcess.Log("No post process presets (%s)", get_presets_path().string().c_str());
        return;
    }

    for (const Json::Value& item : (*root)["presets"])
    {
        Preset preset;
        preset.name = item["name"].asString();
        preset.description = item["description"].asString();
        if (preset.name.empty())
            continue;
        const Json::Value& values = item["values"];
        for (const std::string& name : values.getMemberNames())
        {
            const std::optional<std::string> text = json_to_text(values[name]);
            if (!text || !cvar::find(name))
            {
                LogPostProcess.Log("Post process preset '%s': skipped '%s' (unknown cvar or value type)", preset.name.c_str(), name.c_str());
                continue;
            }
            preset.values.emplace_back(name, *text);
        }
        g_presets.push_back(std::move(preset));
    }
}

const std::vector<post_process::Preset>& post_process::get_presets()
{
    if (!g_presets_loaded)
        reload_presets();
    return g_presets;
}

const post_process::Preset* post_process::find_preset(std::string_view name)
{
    for (const Preset& preset : get_presets())
        if (preset.name == name)
            return &preset;
    return nullptr;
}

bool post_process::apply_preset(std::string_view name)
{
    const Preset* preset = find_preset(name);
    if (!preset)
        return false;

    for (cvar::Entry* entry : get_settings())
        entry->reset_to_default();
    for (const auto& [cvar_name, text] : preset->values)
    {
        cvar::Entry* entry = cvar::find(cvar_name);
        if (!entry || is_switch(cvar_name) || !entry->from_string(text))
            LogPostProcess.Log("Post process preset '%s': could not set %s = %s", preset->name.c_str(), cvar_name.c_str(), text.c_str());
    }
    cv_post_preset.set(preset->name);
    return true;
}

bool post_process::save_preset(std::string_view name, std::string_view description)
{
    Json::Value root = read_presets_file().value_or(Json::Value(Json::objectValue));
    if (!root["presets"].isArray())
        root["presets"] = Json::Value(Json::arrayValue);

    Json::Value item(Json::objectValue);
    item["name"] = std::string(name);
    item["description"] = std::string(description);
    Json::Value values(Json::objectValue);
    for (cvar::Entry* entry : get_settings())
        if (!entry->is_default())
            values[entry->get_name()] = text_to_json(entry->to_string());
    item["values"] = values;

    // replace the preset with this name, keep its place and its description when none is given
    Json::Value& presets = root["presets"];
    bool replaced = false;
    for (Json::ArrayIndex i = 0; i < presets.size(); ++i)
    {
        if (presets[i]["name"].asString() != name)
            continue;
        if (description.empty())
            item["description"] = presets[i]["description"];
        presets[i] = item;
        replaced = true;
        break;
    }
    if (!replaced)
        presets.append(item);

    std::ofstream file(get_presets_path());
    if (!file)
        return false;
    Json::StreamWriterBuilder builder;
    builder["indentation"] = "\t";
    file << Json::writeString(builder, root) << "\n";
    file.close();

    reload_presets();
    cv_post_preset.set(std::string(name));
    return true;
}
