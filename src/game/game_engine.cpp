module;

#include <imgui.h>
#include <imgui_stdlib.h>

module game;

import :engine;

import std.compat;

import :renderer;
import :debug_view;
import :reflection_probes;
import :sky_renderer;
import :gtao_renderer;
import :generic_render_graph;
import :post_process;
import ecs;
import name;
import framework;
import rhcomponents;
import rhmath;
import glm;
import cvar;
import reflect;
import properties;

namespace
{
    cvar::Var<bool> cv_window_post_process("ui.windows.post_process", false, "Post processing: presets, exposure, bloom, color grading, lens effects");

    // post process presets (assets/render/post_process_presets.json), the description as tooltip
    void post_process_preset_combo()
    {
        const std::string current = cv_post_preset.get();
        if (ImGui::BeginCombo("Preset", current.c_str()))
        {
            for (const post_process::Preset& preset : post_process::get_presets())
            {
                if (ImGui::Selectable(preset.name.c_str(), preset.name == current))
                    post_process::apply_preset(preset.name);
                if (!preset.description.empty())
                    ImGui::SetItemTooltip("%s", preset.description.c_str());
            }
            ImGui::EndCombo();
        }
        ImGui::SetItemTooltip("Sets every post process setting (the ones a preset does not list go back to their defaults).\nConsole: post.preset <name>");
    }
}

void GameEngine::engine_init()
{
    auto game_renderer = std::make_shared<GameRenderer>();
    game_renderer->set_engine(std::static_pointer_cast<GameEngine>(shared_from_this()));
    renderer = game_renderer;
    Engine::engine_init();
}

void GameEngine::rebake_reflection_probes()
{
    std::static_pointer_cast<GameRenderer>(renderer)->rebake_reflection_probes();
}

void GameEngine::on_debug_ui_menu()
{
    if (!ImGui::BeginMenu("View"))
        return;

    // F1-F3 cycle the same modes
    const DebugViewMode current = cv_debug_view_mode.get();
    for (uint32_t index = 0; index < (uint32_t)DebugViewMode::COUNT; index++)
    {
        const DebugViewMode mode = (DebugViewMode)index;
        if (mode == first_gbuffer_view_mode)
            ImGui::Separator();
        const char* shortcut = mode == DebugViewMode::LIT ? "F1"
            : mode == DebugViewMode::WIREFRAME ? "F2"
            : mode == first_gbuffer_view_mode ? "F3"
            : nullptr;
        if (ImGui::MenuItem(get_debug_view_mode_name(mode), shortcut, mode == current))
            cv_debug_view_mode.set(mode);
    }
    ImGui::Separator();
    if (ImGui::MenuItem("Skeletons", "F4", cv_debug_show_skeleton.get()))
        cv_debug_show_skeleton.set(!cv_debug_show_skeleton.get());

    ImGui::EndMenu();
}

void GameEngine::on_debug_ui_render_panel()
{
    ImGui::SeparatorText("Debug view");

    const DebugViewMode current = cv_debug_view_mode.get();
    if (ImGui::BeginCombo("View mode", get_debug_view_mode_name(current)))
    {
        for (uint32_t index = 0; index < (uint32_t)DebugViewMode::COUNT; index++)
        {
            const DebugViewMode mode = (DebugViewMode)index;
            if (ImGui::Selectable(get_debug_view_mode_name(mode), mode == current))
                cv_debug_view_mode.set(mode);
        }
        ImGui::EndCombo();
    }

    bool show_skeleton = cv_debug_show_skeleton.get();
    if (ImGui::Checkbox("Skeletons", &show_skeleton))
        cv_debug_show_skeleton.set(show_skeleton);

    // ---- post processing: the look of the frame (its settings: Windows > Post Process) ----
    ImGui::SeparatorText("Post process");
    ImGui::PushID("post");
    post_process_preset_combo();
    ImGui::SameLine();
    if (ImGui::SmallButton("Settings"))
        cv_window_post_process.set(true);
    ImGui::PopID();

    auto checkbox = [] (const char* label, cvar::Var<bool>& var, const char* tooltip)
    {
        bool value = var.get();
        if (ImGui::Checkbox(label, &value))
            var.set(value);
        ImGui::SetItemTooltip("%s", tooltip);
    };
    auto slider = [] (const char* label, cvar::Var<float>& var, float min, float max, const char* tooltip)
    {
        float value = var.get();
        if (ImGui::SliderFloat(label, &value, min, max))
            var.set(value);
        ImGui::SetItemTooltip("%s", tooltip);
    };
    // one bit of a renderer int param (debug switches read by the render graph every frame)
    auto int_param_bit = [this] (const char* label, const char* param, uint32_t bit, const char* tooltip)
    {
        const auto& params = renderer->get_int_params();
        const auto it = params.find(param);
        const uint32_t value = it != params.end() ? (uint32_t)it->second : 0u;
        bool on = (value & bit) != 0;
        if (ImGui::Checkbox(label, &on))
            renderer->set_int_param(param, (int)(on ? value | bit : value & ~bit));
        ImGui::SetItemTooltip("%s", tooltip);
    };

    // ---- image based specular: SSR where the screen space ray hits, probe cubemaps elsewhere ----
    ImGui::SeparatorText("Reflections");

    ImGui::PushID("ssr");
    checkbox("SSR", cv_ssr_enabled, "Screen space reflections: replace the probe reflections where the ray hits something on screen");
    ImGui::SameLine(160.0f);
    ImGui::BeginDisabled(!cv_ssr_enabled.get());
    slider("Intensity", cv_ssr_intensity, 0.0f, 8.0f, "Scale of the screen space reflections (1: physically based)");
    ImGui::EndDisabled();
    ImGui::PopID();

    ImGui::PushID("probe_specular");
    checkbox("Probe cubemaps", cv_probes_specular, "Specular reflections from the reflection probe cubemaps (parallax corrected, blended by the boxes)");
    ImGui::SameLine(160.0f);
    ImGui::BeginDisabled(!cv_probes_specular.get());
    slider("Intensity", cv_probes_specular_intensity, 0.0f, 8.0f, "Scale of the probe reflections (1: physically based)");
    ImGui::EndDisabled();
    ImGui::PopID();

    ImGui::PushID("probe_diffuse");
    checkbox("Probe diffuse", cv_probes_diffuse, "Indirect diffuse light from the probes' irradiance cubemaps");
    ImGui::SameLine(160.0f);
    ImGui::BeginDisabled(!cv_probes_diffuse.get());
    slider("Intensity", cv_probes_diffuse_intensity, 0.0f, 4.0f, "Scale of the probe irradiance");
    ImGui::EndDisabled();
    ImGui::PopID();

    int_param_bit("Glossy level", GeometryDebug::param, GeometryDebug::glossy,
        "Debug: roughness x 0.25 on every pbr material. The level (New Sponza) is rough stone (roughness ~0.7): physically it barely reflects");
    ImGui::SameLine();
    int_param_bit("Probe mirror", LightingDebug::param, LightingDebug::probe_mirror,
        "Debug: every surface shows the probes' mirror reflection (roughness 0, parallax corrected) - what the probes captured and how their boxes line up");

    // ---- directional light shadow: cascades 0 and 1 every frame, the far ones time sliced ----
    ImGui::SeparatorText("Shadows");
    for (auto [label, var] : { std::pair{ "Cascade 2 interval", &cv_shadow_interval_2 },
                               std::pair{ "Cascade 3 interval", &cv_shadow_interval_3 } })
    {
        int interval = var->get();
        if (ImGui::SliderInt(label, &interval, 1, 8))
            var->set(interval);
        ImGui::SetItemTooltip("%s", var->get_description().c_str());
    }
    if (const auto graph = std::dynamic_pointer_cast<GenericRenderGraph>(renderer->get_main_render_graph()))
    {
        const auto& cascades = graph->get_shadow_cascades();
        std::string rendered;
        for (uint32_t i = 0; i < cascades.size(); ++i)
            rendered += std::format("{}{}: {:.0f} m{}", i ? "   " : "", i, cascades[i].radius,
                cascades[i].rendered_this_frame ? " *" : "");
        ImGui::TextDisabled("%s", rendered.c_str());
        ImGui::SetItemTooltip("Radius of each cascade; * rendered this frame (the others keep their tile)");
    }

    // ---- sky: its cost is the texels of its buffers (downscale), for the clouds x steps per ray x samples
    // towards the light ----
    ImGui::SeparatorText("Sky");
    checkbox("Volumetric clouds", cv_clouds_enabled, "Clouds of the VolumetricClouds of the level (their look: the sky entity, Windows > Sky)");
    ImGui::SameLine();
    checkbox("Cloud shadows", cv_clouds_shadows, cv_clouds_shadows.get_description().c_str());
    for (auto [label, var, min, max] : { std::tuple{ "Downscale", &cv_sky_downscale, 1, 8 },
                                         std::tuple{ "Cloud steps", &cv_clouds_steps, 8, 256 },
                                         std::tuple{ "Cloud light steps", &cv_clouds_light_steps, 1, 16 },
                                         std::tuple{ "Cloud shadow steps", &cv_clouds_shadow_steps, 4, 128 } })
    {
        int value = var->get();
        if (ImGui::SliderInt(label, &value, min, max))
            var->set(value);
        ImGui::SetItemTooltip("%s", var->get_description().c_str());
    }
    slider("Cloud history", cv_clouds_history, 0.0f, 0.98f, cv_clouds_history.get_description().c_str());

    // ---- fog: texels of its buffer (downscale) x samples per ray ----
    ImGui::SeparatorText("Fog");
    checkbox("Volumetric fog", cv_fog_enabled, "Fog and light shafts of the VolumetricFog of the level (their look: the sky entity, Windows > Sky)");
    ImGui::PushID("fog");
    for (auto [label, var, min, max] : { std::tuple{ "Downscale", &cv_fog_downscale, 1, 8 },
                                         std::tuple{ "Steps", &cv_fog_steps, 8, 128 } })
    {
        int value = var->get();
        if (ImGui::SliderInt(label, &value, min, max))
            var->set(value);
        ImGui::SetItemTooltip("%s", var->get_description().c_str());
    }
    slider("History", cv_fog_history, 0.0f, 0.98f, cv_fog_history.get_description().c_str());
    ImGui::PopID();

    // ---- ambient occlusion: texels (downscale) x slices x 2 sides x steps depth samples ----
    ImGui::SeparatorText("Ambient occlusion");
    checkbox("GTAO", cv_gtao_enabled, cv_gtao_enabled.get_description().c_str());
    ImGui::SameLine();
    checkbox("Denoise", cv_gtao_denoise, cv_gtao_denoise.get_description().c_str());
    ImGui::PushID("gtao");
    for (auto [label, var, min, max] : { std::tuple{ "Downscale", &cv_gtao_downscale, 1, 2 },
                                         std::tuple{ "Slices", &cv_gtao_slices, 1, 8 },
                                         std::tuple{ "Steps", &cv_gtao_steps, 1, 8 } })
    {
        int value = var->get();
        if (ImGui::SliderInt(label, &value, min, max))
            var->set(value);
        ImGui::SetItemTooltip("%s", var->get_description().c_str());
    }
    slider("Radius", cv_gtao_radius, 0.05f, 5.0f, cv_gtao_radius.get_description().c_str());
    slider("Falloff", cv_gtao_falloff, 0.05f, 1.0f, cv_gtao_falloff.get_description().c_str());
    slider("Power", cv_gtao_power, 0.5f, 5.0f, cv_gtao_power.get_description().c_str());
    slider("Thin occluders", cv_gtao_thin_compensation, 0.0f, 0.7f, cv_gtao_thin_compensation.get_description().c_str());
    slider("Blur beta", cv_gtao_blur_beta, 0.1f, 10.0f, cv_gtao_blur_beta.get_description().c_str());
    slider("Foliage", cv_gtao_foliage, 0.0f, 1.0f, cv_gtao_foliage.get_description().c_str());
    ImGui::PopID();

    // ---- particles: sprites of the ParticleSystems of the level ----
    ImGui::SeparatorText("Particles");
    checkbox("Particles", cv_particles_enabled, cv_particles_enabled.get_description().c_str());
    if (const auto graph = std::dynamic_pointer_cast<GenericRenderGraph>(renderer->get_main_render_graph());
        graph && graph->particles)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("%u sprites", graph->particles->get_count());
        ImGui::SetItemTooltip("Drawn this frame. Effects (assets/particles) reload when their file is saved");
    }

    ImGui::SeparatorText("Reflection probes");

    ReflectionProbeSystem* probes = std::static_pointer_cast<GameRenderer>(renderer)->get_reflection_probes();
    if (!probes)
    {
        ImGui::TextDisabled("The render graph has no reflection probes");
        return;
    }

    if (ImGui::Button("Rebake all"))
        rebake_reflection_probes();
    ImGui::SetItemTooltip("Every probe is captured again, nearest to the camera first (G)");

    ImGui::SameLine();
    checkbox("Show volumes", cv_probes_show_volumes, "Boxes (outer box: end of the influence fade) and capture points. Green: baked, yellow: waiting for a rebake, red: capturing");
    checkbox("Enabled", cv_probes_enabled, "Off: no probes, the lighting uses the sky ambient only");
    ImGui::SameLine();
    checkbox("Parallax", cv_probes_parallax, "Box projected reflections");
    ImGui::SameLine();
    checkbox("Rebake on sun change", cv_probes_auto_rebake, "A sun direction / color change makes every probe dirty");

    int faces = cv_probes_faces_per_frame.get();
    if (ImGui::SliderInt("Faces per frame", &faces, 1, 6))
        cv_probes_faces_per_frame.set(faces);
    ImGui::SetItemTooltip("Cube faces a rebaking probe renders per frame: 1 spreads a rebake over 6 frames");
    int blend_frames = cv_probes_blend_frames.get();
    if (ImGui::SliderInt("Blend frames", &blend_frames, 1, 120))
        cv_probes_blend_frames.set(blend_frames);
    ImGui::SetItemTooltip("Frames a rebaked probe crossfades from its old capture to the new one (1: switch at once)");
    int sky_interval = cv_probes_sky_interval.get();
    if (ImGui::SliderInt("Sky interval", &sky_interval, 2, 60))
        cv_probes_sky_interval.set(sky_interval);
    ImGui::SetItemTooltip("Least frames between two updates of the sky probe: it follows the sun and the weather at once, without a crossfade");

    const auto status = probes->get_status();
    if (status.empty())
    {
        ImGui::TextDisabled("No active ReflectionCapture entities");
        return;
    }
    if (ImGui::BeginTable("reflection_probes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_SizingStretchProp))
    {
        ImGui::TableSetupColumn("Probe");
        ImGui::TableSetupColumn("State");
        ImGui::TableSetupColumn("Bakes");
        ImGui::TableSetupColumn("Age");
        ImGui::TableHeadersRow();
        for (const auto& probe : status)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(probe.name.to_string().c_str());
            ImGui::TableNextColumn();
            if (probe.baking)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "capturing");
            else if (!probe.baked)
                ImGui::TextColored(ImVec4(1.0f, 0.4f, 0.3f, 1.0f), "not baked");
            else if (probe.dirty)
                ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "dirty");
            else
                ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.5f, 1.0f), "baked");
            ImGui::TableNextColumn();
            ImGui::Text("%u", probe.bake_count);
            ImGui::TableNextColumn();
            if (probe.baked)
                ImGui::Text("%.1f s", probe.age_seconds);
        }
        ImGui::EndTable();
    }
    ImGui::Text("Faces rendered: %llu", (unsigned long long)probes->get_faces_rendered());
}


/************************************************************************
 * POST PROCESS WINDOW
 ***********************************************************************/

namespace
{
    // tooltip with the description and the default; right click resets the setting
    void setting_tooltip(cvar::Entry& entry)
    {
        ImGui::SetItemTooltip("%s\nRight click: default (%s)", entry.get_description().c_str(), entry.default_to_string().c_str());
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            entry.reset_to_default();
    }

    void setting(const char* label, cvar::Var<float>& var, const char* format = "%.3f")
    {
        const reflect::PropertyMeta& meta = var.get_property().meta;
        float value = var.get();
        if (ImGui::SliderFloat(label, &value, meta.min, meta.max, format))
            var.set(value);
        setting_tooltip(var);
    }

    void setting(const char* label, cvar::Var<int>& var)
    {
        const reflect::PropertyMeta& meta = var.get_property().meta;
        int value = var.get();
        if (ImGui::SliderInt(label, &value, (int)meta.min, (int)meta.max))
            var.set(value);
        setting_tooltip(var);
    }

    void setting(const char* label, cvar::Var<bool>& var)
    {
        bool value = var.get();
        if (ImGui::Checkbox(label, &value))
            var.set(value);
        setting_tooltip(var);
    }

    void color_setting(const char* label, cvar::Var<glm::vec3>& var)
    {
        glm::vec3 value = var.get();
        if (ImGui::ColorEdit3(label, &value.x, ImGuiColorEditFlags_Float))
            var.set(value);
        setting_tooltip(var);
    }

    void vector_setting(const char* label, cvar::Var<glm::vec3>& var, float min, float max)
    {
        glm::vec3 value = var.get();
        if (ImGui::DragFloat3(label, &value.x, 0.002f, min, max, "%.3f"))
            var.set(value);
        setting_tooltip(var);
    }

    template<typename E>
    void enum_setting(const char* label, cvar::Var<E>& var)
    {
        const E current = var.get();
        if (ImGui::BeginCombo(label, reflect::enum_name(current).to_string().c_str()))
        {
            const reflect::EnumInfo& info = *var.get_property().enum_info;
            for (size_t i = 0; i < info.names.size(); ++i)
            {
                const E value = (E)info.values[i];
                if (ImGui::Selectable(std::string(info.names[i]).c_str(), value == current))
                    var.set(value);
            }
            ImGui::EndCombo();
        }
        setting_tooltip(var);
    }
}

void GameEngine::on_debug_ui_windows_menu()
{
    if (ImGui::MenuItem("Post Process", nullptr, cv_window_post_process.get()))
        cv_window_post_process.set(!cv_window_post_process.get());
}

void GameEngine::on_debug_ui_windows()
{
    draw_post_process_window();
}

void GameEngine::draw_post_process_window()
{
    if (!cv_window_post_process.get())
        return;

    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(ImVec2(viewport->WorkPos.x + viewport->WorkSize.x * 0.55f, viewport->WorkPos.y + viewport->WorkSize.y * 0.05f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(viewport->WorkSize.x * 0.22f, viewport->WorkSize.y * 0.8f), ImGuiCond_FirstUseEver);
    bool keep_open = true;
    const bool expanded = ImGui::Begin("Post Process", &keep_open);
    if (!keep_open)
        cv_window_post_process.set(false);
    if (!expanded)
    {
        ImGui::End();
        return;
    }

    // ---- presets: every render.post.* setting below; applying one resets the others to their defaults ----
    setting("Enabled", cv_post_enabled);
    ImGui::SameLine();
    setting("Histogram", cv_post_show_histogram);

    post_process_preset_combo();
    const std::string current = cv_post_preset.get();

    if (ImGui::Button("Reapply"))
        post_process::apply_preset(current);
    ImGui::SetItemTooltip("Throws away the changes made since the preset was applied");
    ImGui::SameLine();
    if (ImGui::Button("Reload"))
        post_process::reload_presets();
    ImGui::SetItemTooltip("Reloads %s", post_process::get_presets_path().string().c_str());
    ImGui::SameLine();
    if (ImGui::Button("Save as..."))
        ImGui::OpenPopup("save_post_preset");
    ImGui::SetItemTooltip("Saves the settings that differ from the defaults as a preset");

    if (ImGui::BeginPopup("save_post_preset"))
    {
        static std::string name;
        static std::string description;
        if (ImGui::IsWindowAppearing())
        {
            name = current;
            const post_process::Preset* preset = post_process::find_preset(current);
            description = preset ? preset->description : std::string();
        }
        ImGui::InputText("Name", &name);
        ImGui::InputText("Description", &description);
        const bool exists = post_process::find_preset(name) != nullptr;
        ImGui::BeginDisabled(name.empty());
        if (ImGui::Button(exists ? "Replace" : "Save"))
        {
            post_process::save_preset(name, description);
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }

    // ---- exposure ----
    if (ImGui::CollapsingHeader("Exposure", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("exposure");
        enum_setting("Mode", cv_post_exposure_mode);
        if (cv_post_exposure_mode.get() == ExposureMode::Manual)
            setting("EV", cv_post_exposure_ev, "%.2f");
        setting("Compensation", cv_post_exposure_compensation, "%.2f");
        if (cv_post_exposure_mode.get() == ExposureMode::Auto)
        {
            setting("Min EV", cv_post_exposure_min_ev, "%.2f");
            setting("Max EV", cv_post_exposure_max_ev, "%.2f");
            setting("Key", cv_post_exposure_key);
            setting("Speed up", cv_post_exposure_speed_up, "%.2f");
            setting("Speed down", cv_post_exposure_speed_down, "%.2f");
            setting("Low percent", cv_post_exposure_low_percent, "%.2f");
            setting("High percent", cv_post_exposure_high_percent, "%.2f");
            setting("Center weight", cv_post_exposure_center_weight, "%.2f");
        }
        ImGui::PopID();
    }

    // ---- bloom ----
    if (ImGui::CollapsingHeader("Bloom", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("bloom");
        setting("Intensity", cv_post_bloom_intensity);
        setting("Scatter", cv_post_bloom_scatter, "%.2f");
        color_setting("Tint", cv_post_bloom_tint);
        ImGui::PopID();
    }

    // ---- tone curve ----
    if (ImGui::CollapsingHeader("Tone curve", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("tone");
        enum_setting("Tonemapper", cv_post_tonemapper);
        setting("Display gamma", cv_post_gamma, "%.2f");
        ImGui::PopID();
    }

    // ---- color grading ----
    if (ImGui::CollapsingHeader("Color grading", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("color");
        ImGui::SeparatorText("White balance");
        setting("Temperature", cv_post_temperature, "%.1f");
        setting("Tint", cv_post_tint, "%.1f");
        color_setting("Color filter", cv_post_color_filter);

        ImGui::SeparatorText("Tone");
        setting("Contrast", cv_post_contrast, "%.2f");
        setting("Saturation", cv_post_saturation, "%.2f");
        setting("Hue shift", cv_post_hue_shift, "%.1f");

        ImGui::SeparatorText("Lift / gamma / gain");
        vector_setting("Lift", cv_post_lift, -0.25f, 0.25f);
        vector_setting("Gamma", cv_post_midtones, 0.2f, 3.0f);
        vector_setting("Gain", cv_post_gain, 0.0f, 3.0f);

        ImGui::SeparatorText("Split toning");
        color_setting("Shadows", cv_post_split_shadows);
        setting("Shadows strength", cv_post_split_shadows_strength, "%.2f");
        color_setting("Highlights", cv_post_split_highlights);
        setting("Highlights strength", cv_post_split_highlights_strength, "%.2f");
        setting("Balance", cv_post_split_balance, "%.2f");

        ImGui::SeparatorText("Stylize");
        setting("Posterize", cv_post_posterize);
        ImGui::PopID();
    }

    // ---- lens ----
    if (ImGui::CollapsingHeader("Lens", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImGui::PushID("lens");
        ImGui::SeparatorText("Vignette");
        setting("Intensity", cv_post_vignette_intensity, "%.2f");
        setting("Smoothness", cv_post_vignette_smoothness, "%.2f");
        setting("Roundness", cv_post_vignette_roundness, "%.2f");
        color_setting("Color", cv_post_vignette_color);

        ImGui::SeparatorText("Image");
        setting("Chromatic aberration", cv_post_chromatic_aberration, "%.4f");
        setting("Sharpen", cv_post_sharpen, "%.2f");

        ImGui::SeparatorText("Film grain");
        ImGui::PushID("grain");
        setting("Intensity", cv_post_grain_intensity, "%.2f");
        setting("Size", cv_post_grain_size, "%.2f");
        setting("Response", cv_post_grain_response, "%.2f");
        ImGui::PopID();
        ImGui::PopID();
    }

    ImGui::End();
}
