module;

#include <imgui.h>

module game;

import :engine;

import std.compat;

import :renderer;
import :debug_view;
import ecs;
import name;
import framework;
import rhcomponents;
import rhmath;
import glm;

void GameEngine::engine_init()
{
    auto game_renderer = std::make_shared<GameRenderer>();
    game_renderer->set_engine(std::static_pointer_cast<GameEngine>(shared_from_this()));
    renderer = game_renderer;
    Engine::engine_init();
}

void GameEngine::capture_reflection_probes()
{
    auto game_renderer = std::static_pointer_cast<GameRenderer>(renderer);
    ecs::Registry& registry = world->registry;
    ecs::Query<const ReflectionCapture, const Name>(registry).each([&] (ecs::Entity e, const ReflectionCapture&, const Name& name) {
        game_renderer->capture_ibl(scene::get_world_transform(registry, e).position.glm(), name);
    });
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

    if (ImGui::Button("Capture reflection probes"))
        capture_reflection_probes();
    ImGui::SetItemTooltip("Bakes IBL cubemaps of every reflection capture into cache/hdr (G)");
}
