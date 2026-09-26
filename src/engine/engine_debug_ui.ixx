export module engine:debug_ui;

import std.compat;
import glm;
import rhmath;
import framework;
import physics;
import ecs;


export class Engine;

// Engine windows of the debug UI (ui module, Dear ImGui): main menu, world outliner (entities), inspector of
// the selected entity, renderer settings, stats, GPU profiler, cvar catalog, world scripts, physics, ECS.
//
// Game code extends it with Engine::on_debug_ui_menu / on_debug_ui_render_panel and
// WorldScript::draw_debug_ui.
export class EngineDebugUI
{
public:
    // Draws all windows, called every frame while the UI is visible
    void draw(Engine& engine);

    // World space debug drawing (physics shapes, probes), every frame, also while the UI is hidden
    void draw_world_debug(Engine& engine);

    void select(ecs::Entity e);
    // null if nothing is selected or the entity was destroyed
    ecs::Entity get_selected(Engine& engine);

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
    void draw_physics_window(Engine& engine);
    void draw_ecs_window(Engine& engine);

    void draw_viewport_overlay(Engine& engine, const ViewData& view);
    void handle_picking(Engine& engine, const ViewData& view);

    ecs::Entity selected;
    bool scroll_outliner_to_selection = false;
    std::string outliner_filter;
    std::string cvar_filter;

    // physics window: probe from the camera and test bodies
    std::optional<phys::Hit> probe_hit;
    std::string probe_hit_owner;
    std::vector<phys::BodyId> spawned_bodies;
    phys::Shape spawn_sphere_shape;
    phys::Shape spawn_box_shape;
    phys::Shape probe_sphere_shape;
    float probe_sphere_radius = 0.0f;

    static constexpr size_t FRAME_TIME_HISTORY = 240;
    std::array<float, FRAME_TIME_HISTORY> frame_times_ms{};
    size_t frame_time_index = 0;
};
