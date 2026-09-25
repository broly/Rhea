export module engine:debug_ui;

import std.compat;
import glm;
import rhmath;
import framework;


export class Engine;

// Engine windows of the debug UI (ui module, Dear ImGui): main menu, world outliner, inspector of the
// selected actor, renderer settings, stats, GPU profiler, cvar catalog, world scripts.
//
// Game code extends it with Engine::on_debug_ui_menu / on_debug_ui_render_panel and
// WorldScript::draw_debug_ui.
export class EngineDebugUI
{
public:
    // Draws all windows, called every frame while the UI is visible
    void draw(Engine& engine);

    void select(const std::shared_ptr<RhActor>& actor);
    std::shared_ptr<RhActor> get_selected() const { return selected.lock(); }

private:
    // camera of the last frame, for picking and 3D overlays
    struct ViewData
    {
        bool valid = false;
        glm::vec3 position{};
        glm::mat4 view_proj{1.0f};
        glm::vec2 viewport{};

        // screen position of a world point, nullopt behind the camera
        std::optional<glm::vec2> project(const glm::vec3& point) const;
    };

    ViewData compute_view(Engine& engine) const;

    void draw_main_menu(Engine& engine);
    void draw_outliner(Engine& engine);
    void draw_inspector(Engine& engine);
    void draw_render_window(Engine& engine);
    void draw_stats_window(Engine& engine);
    void draw_gpu_profiler_window(Engine& engine);
    void draw_cvars_window(Engine& engine);
    void draw_world_scripts_window(Engine& engine);
    void draw_help_window();

    void draw_viewport_overlay(Engine& engine, const ViewData& view);
    void handle_picking(Engine& engine, const ViewData& view);

    std::weak_ptr<RhActor> selected;
    bool scroll_outliner_to_selection = false;
    std::string outliner_filter;
    std::string cvar_filter;

    static constexpr size_t FRAME_TIME_HISTORY = 240;
    std::array<float, FRAME_TIME_HISTORY> frame_times_ms{};
    size_t frame_time_index = 0;
};
