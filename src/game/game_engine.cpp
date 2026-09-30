module;

#include <imgui.h>

module game;

import :engine;

import std.compat;

import :renderer;
import :debug_view;
import :reflection_probes;
import :sky_renderer;
import :generic_render_graph;
import ecs;
import name;
import framework;
import rhcomponents;
import rhmath;
import glm;
import cvar;

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
