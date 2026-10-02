module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_stdlib.h>

module engine;

import :engine;
import :debug_ui;

import std.compat;
import glm;
import rhmath;
import name;
import reflect;
import properties;
import rhobject;
import framework;
import render;
import render_scene;
import rhcomponents;
import gpu_profile;
import profile;
import input;
import ui;
import cvar;
import physics;
import debug_draw;
import ecs;
import terrain;
import sky_controller;
import animation;


namespace
{
    // open windows, saved with the other cvars
    cvar::Var<bool> cv_window_outliner("ui.windows.outliner", true, "World outliner window");
    cvar::Var<bool> cv_window_inspector("ui.windows.inspector", true, "Inspector of the selected entity");
    cvar::Var<bool> cv_window_render("ui.windows.render", true, "Renderer settings window");
    cvar::Var<bool> cv_window_stats("ui.windows.stats", true, "Frame stats window");
    cvar::Var<bool> cv_window_gpu_profiler("ui.windows.gpu_profiler", false, "GPU pass timings window");
    cvar::Var<bool> cv_window_cvars("ui.windows.cvars", false, "Console variables catalog");
    cvar::Var<bool> cv_window_world_scripts("ui.windows.world_scripts", true, "World scripts window");
    cvar::Var<bool> cv_window_help("ui.windows.help", false, "Hotkeys window");
    cvar::Var<bool> cv_window_imgui_demo("ui.windows.imgui_demo", false, "Dear ImGui demo window (widget reference)");
    cvar::Var<bool> cv_window_physics("ui.windows.physics", false, "Physics stats, debug drawing and probes");
    cvar::Var<bool> cv_window_ecs("ui.windows.ecs", false, "ECS: simulation tick, systems, archetypes");
    cvar::Var<bool> cv_window_animation("ui.windows.animation", false, "Animator of the selected entity: curves, montages, sync groups");
    cvar::Var<bool> cv_window_terrain("ui.windows.terrain", false, "Terrain editor: paint layers, sculpt, save");
    cvar::Var<bool> cv_window_sky("ui.windows.sky", false, "Sky: time of day, weather");
    cvar::Var<bool> cv_terrain_brush("terrain.brush", false,
        "Terrain window: the left mouse button paints / sculpts in the viewport (the camera turns with the right one)");

    cvar::Var<bool> cv_physics_draw("physics.debug.draw", false, "Draw collision shapes near the camera");
    cvar::Var<bool> cv_physics_draw_fixed("physics.debug.draw_fixed", false,
        "Also draw fixed bodies (level geometry, many lines)");
    cvar::Var<bool> cv_physics_draw_bounds("physics.debug.draw_bounds", false, "Draw body bounding boxes");
    cvar::Var<bool> cv_physics_draw_velocity("physics.debug.draw_velocity", false, "Draw body velocities");
    cvar::Var<bool> cv_physics_draw_character("physics.debug.draw_character", false,
        "Draw the character controller's stair steps (white: sweeps, red: rejected, cyan: forward test), "
        "floor snapping (orange) and corner probes (green: free, red: blocked)");
    cvar::Var<float> cv_physics_draw_distance("physics.debug.draw_distance", 25.0f,
        "Level geometry farther from the camera is not drawn (moving bodies always are), m", {.has_range = true, .min = 1.0f, .max = 500.0f});
    cvar::Var<bool> cv_physics_probe("physics.debug.probe", false,
        "Trace from the camera along its view direction every frame");

    cvar::Var<bool> cv_viewport_picking("ui.viewport.picking", true,
        "Click in the viewport selects the entity under the cursor");
    cvar::Var<bool> cv_viewport_selection_bounds("ui.viewport.selection_bounds", true,
        "Bounding box of the selected entity");
    cvar::Var<bool> cv_viewport_icons("ui.viewport.icons", true,
        "Icons of entities without geometry (lights, cameras, probes)");

    constexpr ImU32 selection_color = IM_COL32(255, 170, 40, 255);
    constexpr float icon_pick_radius = 12.0f;

    // Begin() for a window whose open state is a cvar. true: draw the content and call ImGui::End()
    bool begin_window(const char* title, cvar::Var<bool>& open, ImVec2 default_pos, ImVec2 default_size)
    {
        if (!open.get())
            return false;

        ImGui::SetNextWindowPos(default_pos, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize(default_size, ImGuiCond_FirstUseEver);

        bool keep_open = true;
        const bool expanded = ImGui::Begin(title, &keep_open);
        if (!keep_open)
            open.set(false);
        if (!expanded)
        {
            ImGui::End();
            return false;
        }
        return true;
    }

    // default window placement, relative to the main viewport
    ImVec2 viewport_point(float x, float y)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        return ImVec2(viewport->WorkPos.x + x * viewport->WorkSize.x, viewport->WorkPos.y + y * viewport->WorkSize.y);
    }

    ImVec2 viewport_size(float x, float y)
    {
        const ImGuiViewport* viewport = ImGui::GetMainViewport();
        return ImVec2(x * viewport->WorkSize.x, y * viewport->WorkSize.y);
    }

    bool contains_case_insensitive(std::string_view text, std::string_view pattern)
    {
        if (pattern.empty())
            return true;
        auto it = std::ranges::search(text, pattern, [] (char a, char b) {
            return std::tolower((unsigned char)a) == std::tolower((unsigned char)b);
        });
        return !it.empty();
    }

    // Position of an entity for icons / labels: its WorldTransform
    std::optional<glm::vec3> get_entity_position(const ecs::Registry& registry, ecs::Entity e)
    {
        if (const WorldTransform* world = registry.get<WorldTransform>(e))
            return world->value.position.glm();
        return std::nullopt;
    }

    std::string get_entity_label(const ecs::Registry& registry, ecs::Entity e)
    {
        std::string name = scene::get_name(registry, e);
        return name.empty() ? std::format("entity {}", e) : name;
    }

    std::string get_component_names(const ecs::Registry& registry, ecs::Entity e)
    {
        std::string result;
        for (ecs::ComponentId id : registry.get_component_ids(e))
        {
            if (!result.empty())
                result += ", ";
            result += ecs::component_info(id).name;
        }
        return result;
    }

    bool is_active_camera(const ecs::Registry& registry, ecs::Entity e)
    {
        const Camera* camera = registry.get<Camera>(e);
        return camera && camera->active;
    }

    // World transform and settings of the active camera
    std::optional<std::pair<Transform, Camera>> find_active_camera(ecs::Registry& registry)
    {
        std::optional<std::pair<Transform, Camera>> result;
        ecs::Query<const Camera, const WorldTransform>(registry).each([&] (const Camera& camera, const WorldTransform& world) {
            if (!result && camera.active)
                result.emplace(world.value, camera);
        });
        return result;
    }

    bool is_empty_aabb(const AABB& aabb)
    {
        const glm::vec3 size = aabb.max - aabb.min;
        return size.x <= 0.0f && size.y <= 0.0f && size.z <= 0.0f;
    }

    // Ray / box slab test: distance to the entry point, nullopt when missed or the origin is inside
    std::optional<float> intersect_ray_aabb(const glm::vec3& origin, const glm::vec3& direction, const AABB& aabb)
    {
        float t_near = -std::numeric_limits<float>::max();
        float t_far = std::numeric_limits<float>::max();
        for (int axis = 0; axis < 3; axis++)
        {
            if (std::abs(direction[axis]) < 1e-8f)
            {
                if (origin[axis] < aabb.min[axis] || origin[axis] > aabb.max[axis])
                    return std::nullopt;
                continue;
            }
            float t0 = (aabb.min[axis] - origin[axis]) / direction[axis];
            float t1 = (aabb.max[axis] - origin[axis]) / direction[axis];
            if (t0 > t1)
                std::swap(t0, t1);
            t_near = std::max(t_near, t0);
            t_far = std::min(t_far, t1);
            if (t_near > t_far)
                return std::nullopt;
        }
        if (t_near <= 0.0f)
            return std::nullopt;
        return t_near;
    }

    // Selectable-like row in a property table: name | value | reset button
    void draw_cvar_row(cvar::Entry& entry, std::string_view label)
    {
        ImGui::TableNextRow();
        ImGui::PushID(&entry);

        ImGui::TableSetColumnIndex(0);
        ImGui::AlignTextToFramePadding();
        const std::string label_text(label);
        ImGui::TreeNodeEx(label_text.c_str(),
            ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen | ImGuiTreeNodeFlags_SpanFullWidth);
        if (ImGui::BeginItemTooltip())
        {
            ImGui::TextUnformatted(entry.get_name().c_str());
            if (!entry.get_description().empty())
                ImGui::TextDisabled("%s", entry.get_description().c_str());
            if (!(entry.get_flags() & cvar::persistent))
                ImGui::TextDisabled("(not saved)");
            ImGui::EndTooltip();
        }

        ImGui::TableSetColumnIndex(1);
        ImGui::SetNextItemWidth(-FLT_MIN);
        if (ui::edit_value(entry.get_property(), entry.value_ptr(), "##value"))
            entry.notify_changed();

        ImGui::TableSetColumnIndex(2);
        if (!entry.is_default())
        {
            if (ImGui::SmallButton("Reset"))
                entry.reset_to_default();
        }

        ImGui::PopID();
    }

    struct CVarTreeNode
    {
        std::map<std::string, CVarTreeNode> children;
        std::vector<std::pair<std::string, cvar::Entry*>> entries;   // leaf name, entry
    };

    void draw_cvar_tree(const CVarTreeNode& node)
    {
        for (const auto& [category, child] : node.children)
        {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(category.c_str());
            const bool open = ImGui::TreeNodeEx(category.c_str(),
                ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_DefaultOpen);
            if (open)
            {
                draw_cvar_tree(child);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        for (const auto& [name, entry] : node.entries)
            draw_cvar_row(*entry, name);
    }
}


/************************************************************************
 * VIEW
 ***********************************************************************/

std::optional<glm::vec2> EngineDebugUI::ViewData::project(const glm::vec3& point) const
{
    const glm::vec4 clip = view_proj * glm::vec4(point, 1.0f);
    if (clip.w <= 1e-4f)
        return std::nullopt;
    const glm::vec2 ndc = glm::vec2(clip.x, clip.y) / clip.w;
    // the projection already flips Y for Vulkan: ndc.y = -1 is the top of the screen
    return glm::vec2((ndc.x * 0.5f + 0.5f) * viewport.x, (ndc.y * 0.5f + 0.5f) * viewport.y);
}

EngineDebugUI::ViewData EngineDebugUI::compute_view(Engine& engine) const
{
    ViewData view;
    const ImGuiIO& io = ImGui::GetIO();
    if (!engine.world || io.DisplaySize.x <= 0.0f || io.DisplaySize.y <= 0.0f)
        return view;

    if (const auto active = find_active_camera(engine.world->registry))
    {
        const auto& [transform, camera] = *active;

        // same as RenderObject_Camera::get_projection
        glm::mat4 proj = glm::perspectiveRH_ZO(camera.fov, io.DisplaySize.x / io.DisplaySize.y,
            camera.near_plane, camera.far_plane);
        proj[1][1] *= -1;

        view.valid = true;
        view.position = transform.position.glm();
        view.view_proj = proj * transform.get_view();
        view.viewport = glm::vec2(io.DisplaySize.x, io.DisplaySize.y);
    }
    return view;
}


/************************************************************************
 * FRAME
 ***********************************************************************/

void EngineDebugUI::select(ecs::Entity e)
{
    selected = e;
    scroll_outliner_to_selection = !e.is_null();
}

void EngineDebugUI::register_console_commands(Engine& engine)
{
    auto add = [this] (std::string_view name, std::string_view description, cvar::Command::Handler handler,
        std::string_view usage = {}, cvar::Command::Completer completer = {}) {
        console_commands.push_back(std::make_unique<cvar::Command>(name, description, std::move(handler), usage,
            std::move(completer)));
    };
    auto bool_values = [] { return std::vector<std::string>{"true", "false"}; };

    add("quit", "Closes the engine", [&engine] (cvar::Args) {
        glfwSetWindowShouldClose(engine.window.handle, GLFW_TRUE);
    });

    add("after", "Runs a console line after a delay (automation, with RHEA_EXEC)",
        [this, &engine] (cvar::Args args) {
            float seconds = 0.0f;
            if (args.size() < 2 || std::from_chars(args[0].data(), args[0].data() + args[0].size(), seconds).ec != std::errc{})
            {
                cvar::print("after <seconds> <command> [arguments]", cvar::Output::error);
                return;
            }
            std::string line;
            for (size_t i = 1; i < args.size(); ++i)
                line += (i > 1 ? " " : "") + args[i];
            delayed_commands.push_back({ engine.world->get_time_seconds() + seconds, std::move(line) });
        }, "<seconds> <command> [arguments]");

    add("render.reload_shaders", "Rebuilds all pipelines from the shader sources (R)", [&engine] (cvar::Args) {
        engine.render_hot_reload();
    });

    add("render.reset_accumulation", "Resets temporal accumulation for one frame (B)", [&engine] (cvar::Args) {
        engine.renderer->set_flag("reset_temporal_accum", true, false, true);
    });

    add("render.dump_draws", "Writes what every mesh pass draws in the next frames, by mesh and LOD, to cache/bench/draws.csv",
        [&engine] (cvar::Args) {
            engine.renderer->set_flag("dump_draws", true, false, true);
        });

    auto flag_names = [&engine] {
        std::vector<std::string> names;
        if (const auto graph = engine.renderer->get_main_render_graph())
            for (const auto& [name, value] : graph->get_render_flags())
                names.push_back(name.to_string());
        return names;
    };
    add("render.dump_frame", "Dumps the intermediate buffers of the next frame (F9) to cache/debug_dump/<buffer>/frame_<N>.exr|png",
        [&engine] (cvar::Args) { engine.renderer->set_flag("debug_dump_frame", true, false, true); });
    add("render.flag", "Prints / sets a render graph flag, toggles it without a value",
        [&engine] (cvar::Args args) {
            const auto graph = engine.renderer->get_main_render_graph();
            if (args.empty() || !graph)
            {
                cvar::print("render.flag <name> [true|false]", cvar::Output::error);
                return;
            }
            const Name name(args[0]);
            const auto flags = graph->get_render_flags();
            const auto it = flags.find(name);
            const bool current = it != flags.end() && it->second;
            bool value = !current;
            if (args.size() > 1)
            {
                if (args[1] == "true" || args[1] == "1" || args[1] == "on")
                    value = true;
                else if (args[1] == "false" || args[1] == "0" || args[1] == "off")
                    value = false;
                else
                {
                    cvar::print(std::format("'{}' is not true / false", args[1]), cvar::Output::error);
                    return;
                }
            }
            if (it == flags.end())
                engine.renderer->set_flag(name, value, true);
            else if (value != current)
                engine.renderer->toggle_flag(name, true);
            cvar::print(std::format("{} = {}", args[0], value));
        },
        "<name> [true|false]",
        [flag_names, bool_values] (size_t arg_index) { return arg_index == 0 ? flag_names() : bool_values(); });

    add("render.param", "Prints / sets a render graph int parameter",
        [&engine] (cvar::Args args) {
            if (args.empty())
            {
                for (const auto& [name, value] : engine.renderer->get_int_params())
                    cvar::print(std::format("{} = {}", name.to_string(), value));
                return;
            }
            const Name name(args[0]);
            if (args.size() > 1)
            {
                int value = 0;
                if (std::from_chars(args[1].data(), args[1].data() + args[1].size(), value).ec != std::errc{})
                {
                    cvar::print(std::format("'{}' is not an integer", args[1]), cvar::Output::error);
                    return;
                }
                engine.renderer->set_int_param(name, value);
            }
            const auto& params = engine.renderer->get_int_params();
            const auto it = params.find(name);
            if (it == params.end())
                cvar::print(std::format("{} is not set", args[0]), cvar::Output::error);
            else
                cvar::print(std::format("{} = {}", args[0], it->second));
        },
        "[name] [value]",
        [&engine] (size_t arg_index) {
            std::vector<std::string> names;
            if (arg_index == 0)
                for (const auto& [name, value] : engine.renderer->get_int_params())
                    names.push_back(name.to_string());
            return names;
        });

    add("prof.start", "Starts the CPU profiler (PROFILE scopes, accumulated until prof.stop)", [] (cvar::Args) {
        prof::set_is_profiling(true);
    });
    add("prof.stop", "Stops the CPU profiler and writes profiling_dump.txt", [] (cvar::Args) {
        prof::set_is_profiling(false);
        prof::dump();
    });

    add("gpuprof.start", "Starts the GPU pass profiler (P)", [] (cvar::Args) {
        gpuprof::clear_results();
        gpuprof::set_enabled(true);
        cv_window_gpu_profiler.set(true);
    });
    add("gpuprof.stop", "Dumps the GPU profile and stops the profiler (O)", [] (cvar::Args args) {
        gpuprof::dump_json(args.empty() ? std::string_view() : std::string_view(args[0]));
        gpuprof::set_enabled(false);
    }, "[name: gpu_profiling_dump_<name>.json]");
    add("gpuprof.dump", "Dumps the GPU profile to JSON", [] (cvar::Args) {
        gpuprof::dump_json();
    });

    auto entity_labels = [&engine] {
        std::vector<std::string> labels;
        const ecs::Registry& registry = engine.world->registry;
        for (const auto& archetype : registry.get_archetypes())
            for (ecs::Entity e : archetype->get_entities())
                labels.push_back(get_entity_label(registry, e));
        return labels;
    };
    add("select", "Selects an entity by name (exact, else the first containing the text) and shows the inspector",
        [this, &engine] (cvar::Args args) {
            if (args.empty())
            {
                cvar::print("select <entity name>", cvar::Output::error);
                return;
            }
            const ecs::Registry& registry = engine.world->registry;
            ecs::Entity exact;
            ecs::Entity partial;
            for (const auto& archetype : registry.get_archetypes())
                for (ecs::Entity e : archetype->get_entities())
                {
                    const std::string label = get_entity_label(registry, e);
                    if (!exact && label == args[0])
                        exact = e;
                    if (!partial && contains_case_insensitive(label, args[0]))
                        partial = e;
                }
            const ecs::Entity found = exact ? exact : partial;
            if (!found)
            {
                cvar::print(std::format("No entity '{}'", args[0]), cvar::Output::error);
                return;
            }
            select(found);
            cv_window_inspector.set(true);
            cvar::print(std::format("Selected {}{}", get_entity_label(registry, found),
                ui::is_visible() ? "" : "   (Shift+` shows the UI)"));
        },
        "<entity name>", [entity_labels] (size_t arg_index) {
            return arg_index == 0 ? entity_labels() : std::vector<std::string>{};
        });

    register_terrain_commands(engine);
    register_sky_commands(engine);
}

ecs::Entity EngineDebugUI::get_selected(Engine& engine)
{
    if (selected && !engine.world->registry.alive(selected))
        selected = ecs::null_entity;
    return selected;
}

void EngineDebugUI::draw(Engine& engine)
{
    frame_times_ms[frame_time_index] = ImGui::GetIO().DeltaTime * 1000.0f;
    frame_time_index = (frame_time_index + 1) % FRAME_TIME_HISTORY;

    const ViewData view = compute_view(engine);

    draw_main_menu(engine);
    draw_outliner(engine);
    draw_inspector(engine);
    draw_render_window(engine);
    draw_stats_window(engine);
    draw_gpu_profiler_window(engine);
    draw_cvars_window(engine);
    draw_world_scripts_window(engine);
    draw_help_window();
    draw_physics_window(engine);
    draw_terrain_window(engine);
    draw_sky_window(engine);
    draw_ecs_window(engine);
    draw_animation_window(engine);
    engine.on_debug_ui_windows();
    if (cv_window_imgui_demo.get())
    {
        bool open = true;
        ImGui::ShowDemoWindow(&open);
        if (!open)
            cv_window_imgui_demo.set(false);
    }

    draw_viewport_overlay(engine, view);
    update_terrain_brush(engine, view);
    handle_picking(engine, view);
}


/************************************************************************
 * MAIN MENU
 ***********************************************************************/

void EngineDebugUI::draw_main_menu(Engine& engine)
{
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("Rhea"))
    {
        if (ImGui::MenuItem("Console", "`", ui::is_console_open()))
            ui::toggle_console();
        if (ImGui::MenuItem("Hide UI", "Shift+`"))
            ui::set_visible(false);
        ImGui::Separator();
        if (ImGui::MenuItem("Save settings"))
            cvar::save();
        if (ImGui::MenuItem("Load settings"))
            cvar::load();
        ImGui::SetItemTooltip("cache/cvars.json, also saved on exit");
        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            glfwSetWindowShouldClose(engine.window.handle, GLFW_TRUE);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Windows"))
    {
        auto item = [] (const char* label, cvar::Var<bool>& open) {
            if (ImGui::MenuItem(label, nullptr, open.get()))
                open.set(!open.get());
        };
        item("Outliner", cv_window_outliner);
        item("Inspector", cv_window_inspector);
        item("Render", cv_window_render);
        item("World Scripts", cv_window_world_scripts);
        item("Stats", cv_window_stats);
        item("GPU Profiler", cv_window_gpu_profiler);
        item("Console Variables", cv_window_cvars);
        item("Physics", cv_window_physics);
        item("Terrain", cv_window_terrain);
        item("Sky", cv_window_sky);
        item("ECS", cv_window_ecs);
        item("Animation", cv_window_animation);
        engine.on_debug_ui_windows_menu();
        ImGui::Separator();
        item("Hotkeys", cv_window_help);
        item("ImGui Demo", cv_window_imgui_demo);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Viewport"))
    {
        auto item = [] (const char* label, cvar::Var<bool>& value) {
            if (ImGui::MenuItem(label, nullptr, value.get()))
                value.set(!value.get());
        };
        item("Click to select", cv_viewport_picking);
        item("Selection bounds", cv_viewport_selection_bounds);
        item("Entity icons", cv_viewport_icons);
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools"))
    {
        if (ImGui::MenuItem("Reload shaders", "R"))
            engine.render_hot_reload();
        if (ImGui::MenuItem("Reset temporal accumulation", "B"))
            engine.renderer->set_flag("reset_temporal_accum", true, false, true);
        ImGui::Separator();
        const bool profiling = gpuprof::is_enabled();
        if (ImGui::MenuItem(profiling ? "Stop GPU profiler" : "Start GPU profiler", profiling ? "O" : "P"))
        {
            if (!profiling)
                gpuprof::clear_results();
            gpuprof::set_enabled(!profiling);
            cv_window_gpu_profiler.set(true);
        }
        if (ImGui::MenuItem("Dump GPU profile"))
            gpuprof::dump_json();
        ImGui::EndMenu();
    }

    engine.on_debug_ui_menu();

    // right aligned frame time
    char fps_text[64];
    const float framerate = ImGui::GetIO().Framerate;
    std::snprintf(fps_text, sizeof(fps_text), "%.1f FPS  %.2f ms", framerate, framerate > 0.0f ? 1000.0f / framerate : 0.0f);
    const float text_width = ImGui::CalcTextSize(fps_text).x;
    ImGui::SameLine(ImGui::GetWindowWidth() - text_width - ImGui::GetStyle().ItemSpacing.x * 2.0f);
    ImGui::TextDisabled("%s", fps_text);

    ImGui::EndMainMenuBar();
}


/************************************************************************
 * OUTLINER / INSPECTOR
 ***********************************************************************/

void EngineDebugUI::draw_outliner(Engine& engine)
{
    if (!begin_window("Outliner", cv_window_outliner, viewport_point(0.0f, 0.0f), viewport_size(0.2f, 0.45f)))
        return;

    ecs::Registry& registry = engine.world->registry;
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter by name or component", &outliner_filter);
    ImGui::TextDisabled("%zu entities", registry.entity_count());

    const ecs::Entity current = get_selected(engine);

    // hierarchy: roots, children by parent
    std::vector<ecs::Entity> roots;
    std::unordered_map<ecs::Entity, std::vector<ecs::Entity>> children;
    for (const auto& archetype : registry.get_archetypes())
        for (ecs::Entity e : archetype->get_entities())
        {
            const ChildOf* child_of = registry.get<ChildOf>(e);
            if (child_of && registry.alive(child_of->parent))
                children[child_of->parent].push_back(e);
            else
                roots.push_back(e);
        }
    // spawn order
    auto by_index = [] (ecs::Entity a, ecs::Entity b) { return a.index < b.index; };
    std::ranges::sort(roots, by_index);
    for (auto& [parent, list] : children)
        std::ranges::sort(list, by_index);

    // with a filter: a flat list of matching entities
    const bool filtering = !outliner_filter.empty();

    auto draw_entity = [&] (this auto&& self, ecs::Entity e) -> void
    {
        const std::string label = get_entity_label(registry, e);
        const std::string components = get_component_names(registry, e);
        auto child_it = children.find(e);
        const bool has_children = !filtering && child_it != children.end();

        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (e == current)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (!has_children)
            flags |= ImGuiTreeNodeFlags_Leaf;

        const bool open = ImGui::TreeNodeEx((void*)(uintptr_t)e.bits(), flags, "%s", label.c_str());
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            select(e);
        if (e == current && scroll_outliner_to_selection)
        {
            ImGui::SetScrollHereY();
            scroll_outliner_to_selection = false;
        }
        ImGui::SetItemTooltip("%s", components.c_str());
        if (has_children)
        {
            ImGui::SameLine();
            ImGui::TextDisabled("(%zu)", child_it->second.size());
        }

        if (open)
        {
            if (has_children)
                for (ecs::Entity child : child_it->second)
                    self(child);
            ImGui::TreePop();
        }
    };

    if (ImGui::BeginChild("##entities", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        if (filtering)
        {
            std::vector<ecs::Entity> all;
            for (const auto& archetype : registry.get_archetypes())
                for (ecs::Entity e : archetype->get_entities())
                    if (contains_case_insensitive(get_entity_label(registry, e), outliner_filter)
                        || contains_case_insensitive(get_component_names(registry, e), outliner_filter))
                        all.push_back(e);
            std::ranges::sort(all, by_index);
            for (ecs::Entity e : all)
                draw_entity(e);
        }
        else
        {
            for (ecs::Entity e : roots)
                draw_entity(e);
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void EngineDebugUI::draw_inspector(Engine& engine)
{
    if (!begin_window("Inspector", cv_window_inspector, viewport_point(0.0f, 0.45f), viewport_size(0.2f, 0.55f)))
        return;

    ecs::Registry& registry = engine.world->registry;
    const ecs::Entity e = get_selected(engine);
    if (!e)
    {
        ImGui::TextWrapped("Nothing selected.");
        ImGui::TextDisabled("Click an object in the viewport or an entity in the Outliner.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s", get_entity_label(registry, e).c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s", std::format("{}", e).c_str());
    ImGui::SameLine();
    const float buttons_width = ImGui::CalcTextSize("DeselectDestroy").x + ImGui::GetStyle().FramePadding.x * 4.0f
        + ImGui::GetStyle().ItemSpacing.x;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - buttons_width));
    if (ImGui::SmallButton("Deselect"))
    {
        select(ecs::null_entity);
        ImGui::End();
        return;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Destroy"))
    {
        engine.world->destroy_recursive(e);
        select(ecs::null_entity);
        ImGui::End();
        return;
    }
    if (const ChildOf* child_of = registry.get<ChildOf>(e); child_of && registry.alive(child_of->parent))
    {
        ImGui::TextDisabled("child of");
        ImGui::SameLine();
        if (ImGui::SmallButton(get_entity_label(registry, child_of->parent).c_str()))
            select(child_of->parent);
    }

    ImGui::SeparatorText("Components");
    // copy: editing may not change the component set, but keep it safe against hooks
    const std::vector<ecs::ComponentId> ids(registry.get_component_ids(e).begin(), registry.get_component_ids(e).end());
    std::vector<ecs::ComponentId> runtime_ids;
    for (ecs::ComponentId id : ids)
    {
        const ComponentType* type = scene::find_component_type(id);
        if (!type)
        {
            runtime_ids.push_back(id);
            continue;
        }
        ImGui::PushID((int)id);
        const std::string header(type->name);
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            const reflect::PropertyObject properties = type->properties ? type->properties(registry.get_raw(e, id)) : reflect::PropertyObject{};
            if (properties.empty())
                ImGui::TextDisabled("No editable fields");
            else
                ui::edit_properties(properties, nullptr, "##component_properties");
        }
        ImGui::PopID();
    }
    // not registered with scene::register_component_type: engine state (WorldTransform, render proxies...)
    if (!runtime_ids.empty())
    {
        ImGui::SeparatorText("Runtime");
        for (ecs::ComponentId id : runtime_ids)
            ImGui::BulletText("%s", std::string(ecs::component_info(id).name).c_str());
    }

    ImGui::End();
}


/************************************************************************
 * RENDER / STATS / PROFILER
 ***********************************************************************/

void EngineDebugUI::draw_render_window(Engine& engine)
{
    if (!begin_window("Render", cv_window_render, viewport_point(0.78f, 0.0f), viewport_size(0.22f, 0.5f)))
        return;

    Renderer& renderer = *engine.renderer;

    engine.on_debug_ui_render_panel();

    ImGui::SeparatorText("Actions");
    if (ImGui::Button("Reload shaders"))
        engine.render_hot_reload();
    ImGui::SetItemTooltip("Rebuilds all pipelines from the shader sources (R)");
    ImGui::SameLine();
    if (ImGui::Button("Reset accumulation"))
        renderer.set_flag("reset_temporal_accum", true, false, true);
    ImGui::SetItemTooltip("Resets temporal accumulation for one frame (B)");

    if (ImGui::CollapsingHeader("Render graph flags", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto graph = renderer.get_main_render_graph();
        const std::map<Name, bool> flags = graph ? graph->get_render_flags() : std::map<Name, bool>{};
        if (flags.empty())
            ImGui::TextDisabled("No flags set yet");
        for (const auto& [name, value] : flags)
        {
            bool enabled = value;
            if (ImGui::Checkbox(name.to_string().c_str(), &enabled))
                renderer.toggle_flag(name, true);
        }
    }

    if (ImGui::CollapsingHeader("Render graph int params", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const std::map<Name, int> params = renderer.get_int_params();
        if (params.empty())
            ImGui::TextDisabled("No params set yet");
        for (const auto& [name, value] : params)
        {
            int edited = value;
            ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
            if (ImGui::InputInt(name.to_string().c_str(), &edited))
                renderer.set_int_param(name, edited);
        }
    }

    if (ImGui::CollapsingHeader("Frame"))
    {
        const Extent extent = renderer.get_backend()->get_swapchain_extent();
        ImGui::Text("Swapchain: %u x %u", extent.width, extent.height);
        ImGui::Text("Frame: %u", renderer.get_frame_id());
        int runs = renderer.get_num_runs_per_frame();
        ImGui::SetNextItemWidth(ImGui::GetFontSize() * 8.0f);
        if (ImGui::SliderInt("Graph runs per frame", &runs, 1, 64))
            renderer.set_num_runs_per_frame((uint8_t)runs);
    }

    ImGui::End();
}

void EngineDebugUI::draw_stats_window(Engine& engine)
{
    if (!begin_window("Stats", cv_window_stats, viewport_point(0.22f, 0.0f), viewport_size(0.2f, 0.18f)))
        return;

    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.1f FPS (%.2f ms)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

    float max_ms = 0.0f;
    for (float ms : frame_times_ms)
        max_ms = std::max(max_ms, ms);
    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "max %.1f ms", max_ms);
    ImGui::PlotLines("##frame_times", frame_times_ms.data(), (int)frame_times_ms.size(), (int)frame_time_index,
        overlay, 0.0f, std::max(max_ms * 1.1f, 1.0f), ImVec2(-FLT_MIN, ImGui::GetFontSize() * 4.0f));

    ImGui::Text("World time: %.1f s", engine.world->get_time_seconds());
    ImGui::Text("Entities: %zu", engine.world->registry.entity_count());
    ImGui::End();
}

void EngineDebugUI::draw_gpu_profiler_window(Engine& engine)
{
    if (!begin_window("GPU Profiler", cv_window_gpu_profiler, viewport_point(0.22f, 0.6f), viewport_size(0.35f, 0.4f)))
        return;

    bool enabled = gpuprof::is_enabled();
    if (ImGui::Checkbox("Record", &enabled))
        gpuprof::set_enabled(enabled);
    ImGui::SameLine();
    if (ImGui::Button("Clear"))
        gpuprof::clear_results();
    ImGui::SameLine();
    if (ImGui::Button("Dump txt"))
        gpuprof::dump();
    ImGui::SameLine();
    if (ImGui::Button("Dump json"))
        gpuprof::dump_json();

    // CPU frame rate (ImGui's running average) next to the GPU pass times
    const ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("%.1f FPS (%.2f ms/frame)", io.Framerate, io.Framerate > 0.0f ? 1000.0f / io.Framerate : 0.0f);

    std::vector<const gpuprof::PassResult*> results;
    double total_last_ms = 0.0;
    for (const auto& [name, result] : gpuprof::ctx().results)
    {
        results.push_back(&result);
        total_last_ms += result.last_ms;
    }

    if (results.empty())
    {
        ImGui::TextDisabled(enabled ? "Waiting for results..." : "Enable recording to measure render graph passes (P / O)");
        ImGui::End();
        return;
    }
    ImGui::Text("Passes total (last frame): %.3f ms", total_last_ms);

    const ImGuiTableFlags flags = ImGuiTableFlags_Sortable | ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV |
        ImGuiTableFlags_Resizable | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("##passes", 5, flags))
    {
        ImGui::TableSetupScrollFreeze(0, 1);
        ImGui::TableSetupColumn("Pass", ImGuiTableColumnFlags_WidthStretch, 3.0f);
        ImGui::TableSetupColumn("Last, ms", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Avg, ms", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Min, ms", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableSetupColumn("Max, ms", ImGuiTableColumnFlags_PreferSortDescending);
        ImGui::TableHeadersRow();

        auto avg = [] (const gpuprof::PassResult* r) {
            return r->sample_count ? r->total_ms / (double)r->sample_count : 0.0;
        };
        if (const ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs(); sort && sort->SpecsCount > 0)
        {
            const ImGuiTableColumnSortSpecs& spec = sort->Specs[0];
            auto less = [&] (const gpuprof::PassResult* a, const gpuprof::PassResult* b) {
                switch (spec.ColumnIndex)
                {
                    case 0: return a->name < b->name;
                    case 1: return a->last_ms < b->last_ms;
                    case 2: return avg(a) < avg(b);
                    case 3: return a->min_ms < b->min_ms;
                    default: return a->max_ms < b->max_ms;
                }
            };
            const bool ascending = spec.SortDirection == ImGuiSortDirection_Ascending;
            std::ranges::sort(results, [&] (const gpuprof::PassResult* a, const gpuprof::PassResult* b) {
                return ascending ? less(a, b) : less(b, a);
            });
        }

        for (const gpuprof::PassResult* result : results)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(result->name.c_str());
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", result->last_ms);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", avg(result));
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", result->sample_count ? result->min_ms : 0.0);
            ImGui::TableNextColumn();
            ImGui::Text("%.3f", result->max_ms);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}


/************************************************************************
 * CVARS / WORLD SCRIPTS / HELP
 ***********************************************************************/

void EngineDebugUI::draw_cvars_window(Engine& engine)
{
    if (!begin_window("Console Variables", cv_window_cvars, viewport_point(0.58f, 0.5f), viewport_size(0.3f, 0.45f)))
        return;

    if (ImGui::Button("Save"))
        cvar::save();
    ImGui::SameLine();
    if (ImGui::Button("Load"))
        cvar::load();
    ImGui::SameLine();
    if (ImGui::Button("Reset all"))
        for (cvar::Entry* entry : cvar::get_entries())
            entry->reset_to_default();
    ui::help_marker("Saved to cache/cvars.json (also on exit). Hover a name for its description.");

    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter", &cvar_filter);

    const ImGuiTableFlags flags = ImGuiTableFlags_Resizable | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_RowBg |
        ImGuiTableFlags_ScrollY;
    if (ImGui::BeginTable("##cvars", 3, flags))
    {
        ImGui::TableSetupColumn("Name", ImGuiTableColumnFlags_WidthStretch, 0.45f);
        ImGui::TableSetupColumn("Value", ImGuiTableColumnFlags_WidthStretch, 0.55f);
        ImGui::TableSetupColumn("##reset", ImGuiTableColumnFlags_WidthFixed, ImGui::CalcTextSize("Reset").x + 8.0f);

        const std::vector<cvar::Entry*> entries = cvar::get_entries();
        if (!cvar_filter.empty())
        {
            // flat list of matches
            for (cvar::Entry* entry : entries)
                if (contains_case_insensitive(entry->get_name(), cvar_filter) ||
                    contains_case_insensitive(entry->get_description(), cvar_filter))
                    draw_cvar_row(*entry, entry->get_name());
        }
        else
        {
            // tree by the dotted name
            CVarTreeNode root;
            for (cvar::Entry* entry : entries)
            {
                CVarTreeNode* node = &root;
                std::string_view name = entry->get_name();
                size_t dot;
                while ((dot = name.find('.')) != std::string_view::npos)
                {
                    node = &node->children[std::string(name.substr(0, dot))];
                    name.remove_prefix(dot + 1);
                }
                node->entries.emplace_back(std::string(name), entry);
            }
            draw_cvar_tree(root);
        }
        ImGui::EndTable();
    }
    ImGui::End();
}

void EngineDebugUI::draw_world_scripts_window(Engine& engine)
{
    if (!begin_window("World Scripts", cv_window_world_scripts, viewport_point(0.78f, 0.5f), viewport_size(0.22f, 0.5f)))
        return;

    if (engine.world->scripts.empty())
        ImGui::TextDisabled("No world scripts");

    for (const auto& script : engine.world->scripts)
    {
        ImGui::PushID(script.get());
        const std::string title(script->get_debug_name());
        if (ImGui::CollapsingHeader(title.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
        {
            const reflect::PropertyObject properties = script->get_properties();
            if (!properties.empty())
                ui::edit_properties(properties);
            script->draw_debug_ui();
        }
        ImGui::PopID();
    }
    ImGui::End();
}

namespace
{
    std::string component_names(std::span<const ecs::ComponentId> ids)
    {
        std::string result;
        for (ecs::ComponentId id : ids)
        {
            if (!result.empty())
                result += ", ";
            result += ecs::component_info(id).name;
        }
        return result;
    }
}

void EngineDebugUI::draw_ecs_window(Engine& engine)
{
    if (!begin_window("ECS", cv_window_ecs, viewport_point(0.3f, 0.1f), viewport_size(0.4f, 0.6f)))
        return;

    ecs::Registry& registry = engine.world->registry;
    ecs::Schedule& schedule = engine.world->schedule;

    const ecs::SimTime* sim = registry.find_resource<ecs::SimTime>();
    const ecs::FrameTime* frame = registry.find_resource<ecs::FrameTime>();
    ImGui::Text("tick %llu  (%.3f s)   alpha %.2f", sim ? (unsigned long long)sim->tick : 0ull,
        sim ? sim->time : 0.0, frame ? frame->alpha : 0.0);
    int tick_rate = int(std::round(1.0 / schedule.fixed_dt));
    if (ImGui::SliderInt("tick rate, Hz", &tick_rate, 10, 240))
        schedule.fixed_dt = 1.0 / tick_rate;
    ImGui::Text("%zu entities, %zu archetypes, %zu component types", registry.entity_count(),
        registry.get_archetypes().size(), ecs::registered_component_count());

    if (ImGui::CollapsingHeader("Systems", ImGuiTreeNodeFlags_DefaultOpen))
    {
        if (schedule.get_systems().empty())
            ImGui::TextDisabled("No systems");

        constexpr ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable;
        if (!schedule.get_systems().empty() && ImGui::BeginTable("##systems", 4, flags))
        {
            ImGui::TableSetupColumn("Phase", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("System");
            ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed);
            ImGui::TableSetupColumn("Access");
            ImGui::TableHeadersRow();
            for (int phase = 0; phase < int(ecs::Phase::Count); ++phase)
            {
                for (const ecs::System* system_ptr : schedule.get_phase_systems(ecs::Phase(phase)))
                {
                    const ecs::System& system = *system_ptr;
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(ecs::phase_name(system.phase).data());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(system.name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", system.last_ms);
                    ImGui::TableNextColumn();
                    if (system.access.exclusive)
                        ImGui::TextUnformatted("exclusive");
                    else
                        ImGui::Text("W: %s  R: %s", component_names(system.access.writes).c_str(),
                            component_names(system.access.reads).c_str());
                }
            }
            ImGui::EndTable();
        }

        for (const ecs::Ambiguity& a : schedule.find_ambiguities())
            ImGui::TextColored(ImVec4(1.f, 0.7f, 0.2f, 1.f), "unordered: %s / %s (%s): %s", a.first->name.c_str(),
                a.second->name.c_str(), ecs::phase_name(a.first->phase).data(), a.conflict.c_str());
    }

    if (ImGui::CollapsingHeader("Archetypes", ImGuiTreeNodeFlags_DefaultOpen))
    {
        for (const auto& archetype : registry.get_archetypes())
        {
            if (archetype->size() == 0)
                continue;
            ImGui::PushID(archetype.get());
            const std::string types = archetype->get_types().empty() ? "<empty>" : component_names(archetype->get_types());
            if (ImGui::TreeNode("##archetype", "%zu  |  %s", archetype->size(), types.c_str()))
            {
                constexpr size_t max_listed = 200;
                const auto entities = archetype->get_entities();
                for (size_t i = 0; i < std::min(entities.size(), max_listed); ++i)
                    ImGui::BulletText("%s", std::format("{}", entities[i]).c_str());
                if (entities.size() > max_listed)
                    ImGui::TextDisabled("... %zu more", entities.size() - max_listed);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
    }
    ImGui::End();
}

void EngineDebugUI::draw_help_window()
{
    if (!begin_window("Hotkeys", cv_window_help, viewport_point(0.35f, 0.25f), viewport_size(0.3f, 0.5f)))
        return;

    static constexpr std::pair<const char*, const char*> hotkeys[] = {
        {"`", "Console: commands and variables, completes names while typing"},
        {"Shift+`", "Show / hide the debug UI"},
        {"Click", "Select the entity under the cursor"},
        {"LMB drag", "Rotate the camera"},
        {"WASD / Q E", "Move (free camera) or walk (character)"},
        {"C", "Character / free camera"},
        {"Mouse wheel", "Field of view (free camera)"},
        {"F / V", "Frame the character / face close-up"},
        {"T / N / 0", "Next pose / expression, back to locomotion"},
        {"L / M", "Next lighting preset (character.light_rig on) / freeze the sun"},
        {"F1 F2 F3 F4", "Lit / wireframe / gbuffer channels / skeletons"},
        {"R", "Reload shaders"},
        {"B", "Reset temporal accumulation"},
        {"G", "Capture reflection probes"},
        {"H", "Shadow map debug"},
        {"P / O", "Start / stop + dump the GPU profiler"},
    };

    if (ImGui::BeginTable("##hotkeys", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerV))
    {
        for (const auto& [key, action] : hotkeys)
        {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(key);
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(action);
        }
        ImGui::EndTable();
    }
    ImGui::TextDisabled("Keys are ignored while the UI uses the keyboard / mouse.");
    ImGui::End();
}


/************************************************************************
 * VIEWPORT: selection, icons, picking
 ***********************************************************************/

void EngineDebugUI::draw_viewport_overlay(Engine& engine, const ViewData& view)
{
    if (!view.valid)
        return;

    ecs::Registry& registry = engine.world->registry;
    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    const ecs::Entity current = get_selected(engine);

    auto to_imvec = [] (glm::vec2 p) { return ImVec2(p.x, p.y); };

    if (cv_viewport_icons.get())
    {
        ecs::Query<const WorldTransform>(registry).each([&] (ecs::Entity e, const WorldTransform& world) {
            // entities without geometry: lights, cameras, probes, groups
            if (get_entity_bounds(registry, e) || is_active_camera(registry, e))   // the active camera is the view itself
                return;
            if (!registry.has<Light>(e) && !registry.has<Camera>(e) && !registry.has<ReflectionCapture>(e))
                return;
            const auto screen = view.project(world.value.position.glm());
            if (!screen)
                return;

            ImU32 color = IM_COL32(200, 200, 200, 200);
            if (const Light* light = registry.get<Light>(e))
            {
                const glm::vec3 c = glm::clamp(glm::vec3(light->color.x, light->color.y, light->color.z), glm::vec3(0.0f), glm::vec3(1.0f));
                color = IM_COL32(int(c.x * 255), int(c.y * 255), int(c.z * 255), 230);
            }
            draw_list->AddCircleFilled(to_imvec(*screen), 5.0f, color);
            draw_list->AddCircle(to_imvec(*screen), 6.0f, IM_COL32(0, 0, 0, 180), 0, 1.5f);
            draw_list->AddText(ImVec2(screen->x + 9.0f, screen->y - 8.0f), IM_COL32(230, 230, 230, 180),
                get_entity_label(registry, e).c_str());
        });
    }

    if (current && cv_viewport_selection_bounds.get())
    {
        std::optional<glm::vec2> label_pos;
        if (const std::optional<AABB> bounds = get_entity_bounds(registry, current); bounds && !is_empty_aabb(*bounds))
        {
            const AABB& aabb = *bounds;
            glm::vec3 corners[8];
            for (int i = 0; i < 8; i++)
                corners[i] = glm::vec3(i & 1 ? aabb.max.x : aabb.min.x, i & 2 ? aabb.max.y : aabb.min.y, i & 4 ? aabb.max.z : aabb.min.z);
            std::optional<glm::vec2> screen[8];
            for (int i = 0; i < 8; i++)
                screen[i] = view.project(corners[i]);

            for (int a = 0; a < 8; a++)
                for (int axis = 1; axis < 8; axis <<= 1)
                {
                    const int b = a | axis;
                    if (b == a || !screen[a] || !screen[b])
                        continue;
                    draw_list->AddLine(to_imvec(*screen[a]), to_imvec(*screen[b]), selection_color, 2.0f);
                }
            label_pos = view.project(glm::vec3(aabb.min.x, aabb.max.y, aabb.min.z));
        }
        else if (const auto position = get_entity_position(registry, current))
        {
            label_pos = view.project(*position);
            if (label_pos)
                draw_list->AddCircle(to_imvec(*label_pos), 10.0f, selection_color, 0, 2.5f);
        }

        if (label_pos)
            draw_list->AddText(ImVec2(label_pos->x + 12.0f, label_pos->y - 18.0f), selection_color,
                get_entity_label(registry, current).c_str());
    }
}

void EngineDebugUI::handle_picking(Engine& engine, const ViewData& view)
{
    if (!view.valid || !cv_viewport_picking.get())
        return;
    // the left button belongs to the terrain brush
    if (cv_window_terrain.get() && cv_terrain_brush.get())
        return;

    const ImGuiIO& io = ImGui::GetIO();
    // a click (not a camera drag) on the viewport, not on a window
    if (io.WantCaptureMouse || !ImGui::IsMouseReleased(ImGuiMouseButton_Left) || io.MouseDragMaxDistanceSqr[0] > 16.0f)
        return;

    const glm::vec2 mouse(io.MousePos.x, io.MousePos.y);
    const glm::vec2 ndc = mouse / view.viewport * 2.0f - 1.0f;
    const glm::mat4 inverse_view_proj = glm::inverse(view.view_proj);
    glm::vec4 near_point = inverse_view_proj * glm::vec4(ndc, 0.0f, 1.0f);
    glm::vec4 far_point = inverse_view_proj * glm::vec4(ndc, 1.0f, 1.0f);
    near_point /= near_point.w;
    far_point /= far_point.w;
    const glm::vec3 origin = view.position;
    const glm::vec3 direction = glm::normalize(glm::vec3(far_point) - glm::vec3(near_point));

    ecs::Registry& registry = engine.world->registry;
    ecs::Entity best_icon;
    float best_icon_distance = icon_pick_radius;
    ecs::Entity best_box;
    float best_box_t = std::numeric_limits<float>::max();

    ecs::Query<const WorldTransform>(registry).each([&] (ecs::Entity e, const WorldTransform& world) {
        if (const std::optional<AABB> bounds = get_entity_bounds(registry, e))
        {
            if (const auto t = intersect_ray_aabb(origin, direction, *bounds); t && *t < best_box_t)
            {
                best_box_t = *t;
                best_box = e;
            }
            return;
        }
        // entities without geometry are picked by their icon
        if (!cv_viewport_icons.get() || is_active_camera(registry, e))
            return;
        if (!registry.has<Light>(e) && !registry.has<Camera>(e) && !registry.has<ReflectionCapture>(e))
            return;
        if (const auto screen = view.project(world.value.position.glm()))
        {
            const float distance = glm::length(*screen - mouse);
            if (distance < best_icon_distance)
            {
                best_icon_distance = distance;
                best_icon = e;
            }
        }
    });

    select(best_icon ? best_icon : best_box);
}


/************************************************************************
 * PHYSICS
 ***********************************************************************/

namespace
{
    const char* category_name(phys::Category category)
    {
        switch (category)
        {
        case phys::Category::static_world: return "static_world";
        case phys::Category::dynamic:      return "dynamic";
        case phys::Category::character:    return "character";
        case phys::Category::hitbox:       return "hitbox";
        case phys::Category::projectile:   return "projectile";
        case phys::Category::voxel:        return "voxel";
        case phys::Category::trigger:      return "trigger";
        case phys::Category::debris:       return "debris";
        }
        return "?";
    }
}

void EngineDebugUI::draw_world_debug(Engine& engine)
{
    // the terrain brush takes the left button again in draw() while it is on (the UI may be hidden now)
    engine.input->set_tool_capture(Key::MouseLeft, false);

    // RHEA_EXEC="line; line": console lines once the level runs (automation, screenshots), see `after`
    if (!startup_commands_run)
    {
        startup_commands_run = true;
        if (const char* exec = std::getenv("RHEA_EXEC"))
        {
            std::string_view lines = exec;
            while (!lines.empty())
            {
                const size_t end = std::min(lines.find(';'), lines.size());
                if (std::string_view line = lines.substr(0, end); line.find_first_not_of(' ') != std::string_view::npos)
                    cvar::execute(line);
                lines.remove_prefix(std::min(end + 1, lines.size()));
            }
        }
    }
    const double now = engine.world->get_time_seconds();
    for (size_t i = 0; i < delayed_commands.size();)
    {
        if (delayed_commands[i].first > now)
        {
            ++i;
            continue;
        }
        const std::string line = std::move(delayed_commands[i].second);
        delayed_commands.erase(delayed_commands.begin() + i);
        cvar::execute(line);
    }

    probe_hit.reset();
    probe_hit_owner.clear();

    if (!engine.world || !engine.world->physics)
        return;

    phys::PhysicsScene& physics = engine.world->get_physics();
    const auto camera = find_active_camera(engine.world->registry);
    if (!camera)
        return;

    const glm::vec3 camera_position = camera->first.position.glm();
    const glm::vec3 camera_forward = camera->first.forward();

    physics.set_character_debug(cv_physics_draw.get() && cv_physics_draw_character.get());
    if (cv_physics_draw.get())
    {
        std::vector<debug_draw::Line> lines;
        physics.debug_draw({
                .camera_position = camera_position,
                .max_distance = cv_physics_draw_distance.get(),
                .fixed_bodies = cv_physics_draw_fixed.get(),
                .bounding_boxes = cv_physics_draw_bounds.get(),
                .velocities = cv_physics_draw_velocity.get(),
            },
            [&lines] (const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
                lines.push_back({ a, b, color });
            });
        debug_draw::lines(lines);
    }

    if (!cv_physics_probe.get())
        return;

    constexpr float probe_distance = 100.0f;
    // start in front of the near plane, or the line is invisible
    const glm::vec3 origin = camera_position + camera_forward * 0.5f;

    if (probe_sphere_radius > 0.0f && probe_sphere_shape)
    {
        probe_hit = physics.sweep(probe_sphere_shape, { origin }, camera_forward, probe_distance);
        if (probe_hit)
            debug_draw::sphere(origin + camera_forward * probe_hit->distance, probe_sphere_radius, { 1.0f, 0.8f, 0.2f, 1.0f });
    }
    else
    {
        probe_hit = physics.raycast(origin, camera_forward, probe_distance);
    }

    if (!probe_hit)
        return;

    debug_draw::cross(probe_hit->position, 0.15f, { 1.0f, 0.2f, 0.2f, 1.0f }, { .depth_test = false });
    debug_draw::arrow(probe_hit->position, probe_hit->position + probe_hit->normal * 0.5f,
                      { 0.2f, 1.0f, 0.3f, 1.0f }, { .depth_test = false });

    // framework convention: BodyDesc::user_data is the owning entity (ecs::Entity::bits), or 0
    if (probe_hit->user_data != 0)
    {
        const ecs::Entity owner{ uint32_t(probe_hit->user_data), uint32_t(probe_hit->user_data >> 32) };
        probe_hit_owner = engine.world->registry.alive(owner) ? get_entity_label(engine.world->registry, owner) : "<destroyed>";
    }
}

void EngineDebugUI::draw_animation_window(Engine& engine)
{
    if (!begin_window("Animation", cv_window_animation, viewport_point(0.02f, 0.40f), viewport_size(0.26f, 0.5f)))
        return;
    if (!engine.world)
    {
        ImGui::TextDisabled("No world");
        ImGui::End();
        return;
    }

    // the selected entity, else the first animated one
    ecs::Registry& registry = engine.world->registry;
    ecs::Entity entity = get_selected(engine);
    if (!entity || !registry.get<anim::Animator>(entity))
    {
        entity = ecs::null_entity;
        ecs::Query<anim::Animator>(registry).each([&](ecs::Entity e, anim::Animator&) {
            if (!entity)
                entity = e;
        });
    }
    if (!entity)
    {
        ImGui::TextDisabled("No animated entity");
        ImGui::End();
        return;
    }
    ImGui::Text("%s", scene::get_name(registry, entity).c_str());
    anim::draw_animator_debug(*registry.get<anim::Animator>(entity));
    ImGui::End();
}

void EngineDebugUI::draw_physics_window(Engine& engine)
{
    if (!begin_window("Physics", cv_window_physics, viewport_point(0.70f, 0.40f), viewport_size(0.24f, 0.45f)))
        return;

    if (!engine.world || !engine.world->physics)
    {
        ImGui::TextDisabled("No physics scene");
        ImGui::End();
        return;
    }

    phys::PhysicsScene& physics = engine.world->get_physics();
    const phys::PhysicsStats stats = physics.get_stats();

    ImGui::Text("Bodies: %u (%u active)", stats.bodies, stats.active_bodies);
    ImGui::Text("Step: %.2f ms, %u substeps", stats.step_ms, stats.steps_last_frame);
    ImGui::Text("Characters: %.2f ms, %u contacts", stats.character_ms, stats.character_contacts);
    ImGui::Text("Cooked shapes: %u built, %u from cache", stats.shapes_cooked, stats.shapes_from_cache);

    auto checkbox = [] (const char* label, cvar::Var<bool>& value) {
        bool v = value.get();
        if (ImGui::Checkbox(label, &v))
            value.set(v);
    };

    if (ImGui::CollapsingHeader("Debug draw", ImGuiTreeNodeFlags_DefaultOpen))
    {
        checkbox("Shapes", cv_physics_draw);
        ImGui::BeginDisabled(!cv_physics_draw.get());
        checkbox("Level geometry", cv_physics_draw_fixed);
        checkbox("Bounding boxes", cv_physics_draw_bounds);
        checkbox("Velocities", cv_physics_draw_velocity);
        checkbox("Character steps / probes", cv_physics_draw_character);
        float distance = cv_physics_draw_distance.get();
        if (ImGui::SliderFloat("Distance", &distance, 1.0f, 500.0f, "%.0f m", ImGuiSliderFlags_Logarithmic))
            cv_physics_draw_distance.set(distance);
        ImGui::EndDisabled();
    }

    if (ImGui::CollapsingHeader("Probe", ImGuiTreeNodeFlags_DefaultOpen))
    {
        checkbox("Trace from camera", cv_physics_probe);

        float radius = probe_sphere_radius;
        if (ImGui::SliderFloat("Sphere radius", &radius, 0.0f, 1.0f, radius > 0.0f ? "%.2f m" : "ray"))
        {
            probe_sphere_radius = radius;
            if (radius > 0.0f)
                probe_sphere_shape = physics.create_shape(phys::SphereShape{ radius });
        }

        if (cv_physics_probe.get() && probe_hit)
        {
            const phys::Hit& hit = *probe_hit;
            ImGui::Text("Distance: %.3f m%s", hit.distance, hit.start_penetrating ? " (start penetrating)" : "");
            ImGui::Text("Position: %.2f %.2f %.2f", hit.position.x, hit.position.y, hit.position.z);
            ImGui::Text("Normal:   %.2f %.2f %.2f", hit.normal.x, hit.normal.y, hit.normal.z);
            ImGui::Text("Body: %08x  %s", hit.body.value, category_name(hit.category));
            if (!probe_hit_owner.empty())
                ImGui::TextWrapped("Owner: %s", probe_hit_owner.c_str());
        }
        else if (cv_physics_probe.get())
        {
            ImGui::TextDisabled("No hit");
        }
    }

    if (ImGui::CollapsingHeader("Test bodies", ImGuiTreeNodeFlags_DefaultOpen))
    {
        const auto camera = find_active_camera(engine.world->registry);

        if (!spawn_sphere_shape)
            spawn_sphere_shape = physics.create_shape(phys::SphereShape{ 0.2f });
        if (!spawn_box_shape)
            spawn_box_shape = physics.create_shape(phys::BoxShape{ glm::vec3(0.25f) });

        auto spawn = [&] (const phys::Shape& shape, float speed)
        {
            if (!camera)
                return;
            const glm::vec3 forward = camera->first.forward();
            const phys::BodyId body = physics.create_body({
                .shape = shape,
                .position = camera->first.position.glm() + forward * 1.5f,
                .motion = phys::Motion::dynamic,
                .category = phys::Category::dynamic,
                .linear_velocity = forward * speed,
                .continuous_collision = speed > 10.0f,
            });
            if (body.is_valid())
                spawned_bodies.push_back(body);
        };

        ImGui::BeginDisabled(!camera);
        if (ImGui::Button("Drop sphere"))
            spawn(spawn_sphere_shape, 0.0f);
        ImGui::SameLine();
        if (ImGui::Button("Drop box"))
            spawn(spawn_box_shape, 0.0f);
        ImGui::SameLine();
        if (ImGui::Button("Throw sphere"))
            spawn(spawn_sphere_shape, 25.0f);
        ImGui::EndDisabled();

        ImGui::BeginDisabled(spawned_bodies.empty());
        if (ImGui::Button(std::format("Remove {} test bodies", spawned_bodies.size()).c_str()))
        {
            for (phys::BodyId body : spawned_bodies)
                physics.destroy_body(body);
            spawned_bodies.clear();
        }
        ImGui::EndDisabled();
        ImGui::TextDisabled("Test bodies are visible with debug draw");
    }

    ImGui::End();
}


/************************************************************************
 * TERRAIN
 ***********************************************************************/

namespace
{
    struct TerrainEditor
    {
        TerrainBrush brush;
        bool flatten_pick = true;           // flatten to the height under the cursor when the stroke starts
        bool stroke = false;
        std::optional<glm::vec3> hit;       // under the cursor
        std::string status;
    };
    TerrainEditor terrain_editor;

    const char* brush_mode_name(TerrainBrushMode mode)
    {
        switch (mode)
        {
        case TerrainBrushMode::paint:   return "Paint";
        case TerrainBrushMode::raise:   return "Raise";
        case TerrainBrushMode::lower:   return "Lower";
        case TerrainBrushMode::smooth:  return "Smooth";
        case TerrainBrushMode::flatten: return "Flatten";
        case TerrainBrushMode::water:   return "Water";
        case TerrainBrushMode::dry:     return "Dry";
        }
        return "?";
    }

    // the selected entity when it is a terrain, else the first one
    ecs::Entity find_terrain(ecs::Registry& registry, ecs::Entity selected)
    {
        if (selected && registry.alive(selected) && registry.has<Terrain>(selected))
            return selected;
        ecs::Entity found;
        ecs::Query<const Terrain>(registry).each([&] (ecs::Entity e, const Terrain&) {
            if (!found)
                found = e;
        });
        return found;
    }

    void draw_brush_ring(const TerrainData& data, glm::vec2 center, float radius, const glm::vec4& color)
    {
        constexpr int segments = 64;
        glm::vec3 previous{};
        for (int i = 0; i <= segments; ++i)
        {
            const float angle = float(i) / float(segments) * 6.2831853f;
            const glm::vec2 xz = center + radius * glm::vec2(std::cos(angle), std::sin(angle));
            const glm::vec3 point(xz.x, terrain::height_at(data, xz).value_or(data.origin.y) + 0.05f, xz.y);
            if (i > 0)
                debug_draw::line(previous, point, color, { .depth_test = false });
            previous = point;
        }
    }

    void teleport_player(glm::vec3 feet)
    {
        cvar::execute(std::format("player.teleport {} {} {}", feet.x, feet.y, feet.z));
    }
}

void EngineDebugUI::update_terrain_brush(Engine& engine, const ViewData& view)
{
    TerrainEditor& editor = terrain_editor;
    editor.hit.reset();

    ecs::Registry& registry = engine.world->registry;
    const ecs::Entity terrain_entity = find_terrain(registry, get_selected(engine));
    Terrain* terrain = terrain_entity ? registry.get<Terrain>(terrain_entity) : nullptr;
    TerrainData* data = terrain ? terrain->data.get() : nullptr;

    const ImGuiIO& io = ImGui::GetIO();
    const bool enabled = cv_window_terrain.get() && cv_terrain_brush.get() && view.valid && data;
    const bool button_down = ImGui::IsMouseDown(ImGuiMouseButton_Left);

    // stroke end: the collision catches up with the sculpted heights
    if (editor.stroke && (!enabled || !button_down))
    {
        editor.stroke = false;
        if (data && data->collision_dirty)
            terrain::commit_collision(*data, engine.world->get_physics(), terrain_entity);
    }
    if (!enabled)
        return;

    // the game does not turn the camera with the left button (see draw_world_debug)
    engine.input->set_tool_capture(Key::MouseLeft, true);

    if (!io.WantCaptureMouse)
    {
        const glm::vec2 ndc = glm::vec2(io.MousePos.x, io.MousePos.y) / view.viewport * 2.0f - 1.0f;
        const glm::mat4 inverse_view_proj = glm::inverse(view.view_proj);
        glm::vec4 near_point = inverse_view_proj * glm::vec4(ndc, 0.0f, 1.0f);
        glm::vec4 far_point = inverse_view_proj * glm::vec4(ndc, 1.0f, 1.0f);
        near_point /= near_point.w;
        far_point /= far_point.w;
        editor.hit = terrain::raycast(*data, view.position, glm::normalize(glm::vec3(far_point) - glm::vec3(near_point)));
    }

    TerrainBrush& brush = editor.brush;
    if (editor.hit)
    {
        const glm::vec2 center(editor.hit->x, editor.hit->z);
        const glm::vec4 color = brush.mode == TerrainBrushMode::paint ? glm::vec4(0.3f, 0.9f, 1.0f, 1.0f)
                                                                       : glm::vec4(1.0f, 0.75f, 0.2f, 1.0f);
        draw_brush_ring(*data, center, brush.radius, color);
        const float inner = brush.radius * (1.0f - brush.falloff);
        if (inner > 0.05f)
            draw_brush_ring(*data, center, inner, color * glm::vec4(1.0f, 1.0f, 1.0f, 0.5f));
    }

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle) && editor.hit)
        teleport_player(*editor.hit + glm::vec3(0.0f, 0.05f, 0.0f));

    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) && editor.hit)
    {
        editor.stroke = true;
        if (brush.mode == TerrainBrushMode::flatten && editor.flatten_pick)
            brush.flatten_height = editor.hit->y;
    }
    if (!editor.stroke || !editor.hit)
        return;

    TerrainBrush step = brush;
    // Shift inverts raise / lower
    if (io.KeyShift && step.mode == TerrainBrushMode::raise)
        step.mode = TerrainBrushMode::lower;
    else if (io.KeyShift && step.mode == TerrainBrushMode::lower)
        step.mode = TerrainBrushMode::raise;
    else if (io.KeyShift && step.mode == TerrainBrushMode::water)
        step.mode = TerrainBrushMode::dry;
    else if (io.KeyShift && step.mode == TerrainBrushMode::dry)
        step.mode = TerrainBrushMode::water;
    terrain::apply_brush(*data, step, glm::vec2(editor.hit->x, editor.hit->z), std::min(io.DeltaTime, 0.1f));
    terrain::commit_render(*data);
}

void EngineDebugUI::register_terrain_commands(Engine& engine)
{
    auto add = [this] (std::string_view name, std::string_view description, cvar::Command::Handler handler,
        std::string_view usage = {}, cvar::Command::Completer completer = {}) {
        console_commands.push_back(std::make_unique<cvar::Command>(name, description, std::move(handler), usage,
            std::move(completer)));
    };
    auto terrain_of = [this, &engine] () -> std::pair<ecs::Entity, Terrain*> {
        ecs::Registry& registry = engine.world->registry;
        const ecs::Entity e = find_terrain(registry, get_selected(engine));
        Terrain* terrain = e ? registry.get<Terrain>(e) : nullptr;
        if (!terrain || !terrain->data)
        {
            cvar::print("No terrain in the level", cvar::Output::error);
            return { e, nullptr };
        }
        return { e, terrain };
    };

    // scripted edits (automation, precise values): the same brush as the Terrain window
    add("terrain.stroke", "Applies the terrain brush at a world position: paint <layer>, raise, lower, smooth, flatten <height>",
        [this, &engine, terrain_of] (cvar::Args args) {
            auto number = [] (const std::string& text, float& value) {
                return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
            };
            TerrainBrush brush = terrain_editor.brush;
            float x = 0.0f, z = 0.0f, value = 0.0f, seconds = 0.5f;
            static constexpr std::array<std::pair<std::string_view, TerrainBrushMode>, 7> modes{ {
                { "paint", TerrainBrushMode::paint }, { "raise", TerrainBrushMode::raise }, { "lower", TerrainBrushMode::lower },
                { "smooth", TerrainBrushMode::smooth }, { "flatten", TerrainBrushMode::flatten },
                { "water", TerrainBrushMode::water }, { "dry", TerrainBrushMode::dry } } };
            const auto mode = args.empty() ? modes.end()
                : std::ranges::find_if(modes, [&] (const auto& m) { return m.first == args[0]; });
            const bool needs_value = mode != modes.end() && (mode->second == TerrainBrushMode::paint || mode->second == TerrainBrushMode::flatten);
            const size_t first_optional = needs_value ? 5 : 4;
            if (mode == modes.end() || args.size() < first_optional || !number(args[1], x) || !number(args[2], z)
                || !number(args[3], brush.radius) || (needs_value && !number(args[4], value))
                || (args.size() > first_optional && !number(args[first_optional], brush.strength))
                || (args.size() > first_optional + 1 && !number(args[first_optional + 1], seconds)))
            {
                cvar::print("terrain.stroke <paint|raise|lower|smooth|flatten|water|dry> <x> <z> <radius> [layer|height] [strength] [seconds]",
                    cvar::Output::error);
                return;
            }
            const auto [e, terrain] = terrain_of();
            if (!terrain)
                return;
            brush.mode = mode->second;
            if (brush.mode == TerrainBrushMode::paint)
                brush.layer = uint32_t(std::clamp(value, 0.0f, 3.0f));
            if (brush.mode == TerrainBrushMode::flatten)
                brush.flatten_height = value;
            TerrainData& data = *terrain->data;
            // in steps of a frame at 60 FPS, like a held mouse button
            for (float t = 0.0f; t < seconds; t += 1.0f / 60.0f)
                terrain::apply_brush(data, brush, glm::vec2(x, z), 1.0f / 60.0f);
            terrain::commit_render(data);
            if (data.collision_dirty)
                terrain::commit_collision(data, engine.world->get_physics(), e);
        },
        "<paint|raise|lower|smooth|flatten|water|dry> <x> <z> <radius> [layer|height] [strength] [seconds]");

    add("terrain.save", "Writes the terrain's heightmap and splat map (the paths of its Terrain component)",
        [terrain_of] (cvar::Args) {
            const auto [e, terrain] = terrain_of();
            if (!terrain)
                return;
            std::string error;
            if (terrain::save(*terrain, error))
                cvar::print(std::format("Saved {}, {}", terrain->heightmap, terrain->splatmap));
            else
                cvar::print("Save failed: " + error, cvar::Output::error);
        });
}

void EngineDebugUI::draw_terrain_window(Engine& engine)
{
    if (!begin_window("Terrain", cv_window_terrain, viewport_point(0.70f, 0.30f), viewport_size(0.24f, 0.5f)))
        return;

    TerrainEditor& editor = terrain_editor;
    ecs::Registry& registry = engine.world->registry;
    const ecs::Entity terrain_entity = find_terrain(registry, get_selected(engine));
    Terrain* terrain = terrain_entity ? registry.get<Terrain>(terrain_entity) : nullptr;
    if (!terrain || !terrain->data)
    {
        ImGui::TextDisabled("No terrain in the level");
        ImGui::End();
        return;
    }
    TerrainData& data = *terrain->data;

    ImGui::Text("%s: %u x %u samples, %.0f m, %zu chunks%s", get_entity_label(registry, terrain_entity).c_str(),
        data.samples, data.samples, data.size(), data.chunks.size(), data.unsaved ? " (modified)" : "");

    // ---- brush ----
    ImGui::SeparatorText("Brush");
    bool brush_on = cv_terrain_brush.get();
    if (ImGui::Checkbox("Edit with the left mouse button", &brush_on))
        cv_terrain_brush.set(brush_on);
    ImGui::SetItemTooltip("The camera turns with the right mouse button meanwhile");

    TerrainBrush& brush = editor.brush;
    // painting and puddles on the first row, sculpting on the second
    for (TerrainBrushMode mode : { TerrainBrushMode::paint, TerrainBrushMode::water, TerrainBrushMode::dry,
                                   TerrainBrushMode::raise, TerrainBrushMode::lower,
                                   TerrainBrushMode::smooth, TerrainBrushMode::flatten })
    {
        if (mode != TerrainBrushMode::paint && mode != TerrainBrushMode::raise)
            ImGui::SameLine();
        if (ImGui::RadioButton(brush_mode_name(mode), brush.mode == mode))
            brush.mode = mode;
    }

    if (brush.mode == TerrainBrushMode::paint)
    {
        for (uint32_t layer = 0; layer < 4; ++layer)
        {
            const std::string label = layer < terrain->layer_names.size() ? terrain->layer_names[layer]
                                                                          : std::format("layer {}", layer);
            if (layer > 0)
                ImGui::SameLine();
            if (ImGui::RadioButton(label.c_str(), brush.layer == layer))
                brush.layer = layer;
        }
    }
    if (brush.mode == TerrainBrushMode::flatten)
    {
        ImGui::Checkbox("Height under the cursor", &editor.flatten_pick);
        ImGui::SetItemTooltip("Takes the height where the stroke starts");
        ImGui::BeginDisabled(editor.flatten_pick);
        ImGui::DragFloat("Height", &brush.flatten_height, 0.05f, data.origin.y + data.min_height,
            data.origin.y + data.max_height, "%.2f m");
        ImGui::EndDisabled();
    }
    ImGui::SliderFloat("Radius", &brush.radius, 0.5f, 60.0f, "%.1f m", ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Strength", &brush.strength, 0.01f, 1.0f, "%.2f");
    ImGui::SliderFloat("Falloff", &brush.falloff, 0.0f, 1.0f, "%.2f");
    if (brush.mode == TerrainBrushMode::raise || brush.mode == TerrainBrushMode::lower
        || brush.mode == TerrainBrushMode::water || brush.mode == TerrainBrushMode::dry)
        ImGui::TextDisabled("Shift: the opposite direction");

    if (editor.hit)
    {
        const Texture& splat = terrain::get_splat(data);
        const glm::vec2 uv = (glm::vec2(editor.hit->x, editor.hit->z) - glm::vec2(data.origin.x, data.origin.z)) / data.size();
        const uint32_t tx = std::min(uint32_t(std::max(uv.x, 0.0f) * float(splat.extent.width)), splat.extent.width - 1);
        const uint32_t tz = std::min(uint32_t(std::max(uv.y, 0.0f) * float(splat.extent.height)), splat.extent.height - 1);
        const auto* w = reinterpret_cast<const uint8_t*>(splat.bulk.data()) + (size_t(tz) * splat.extent.width + tx) * 4;
        ImGui::Text("Cursor %.1f, %.2f, %.1f  weights %d %d %d %d", editor.hit->x, editor.hit->y, editor.hit->z,
            w[0], w[1], w[2], w[3]);
    }

    // ---- file ----
    ImGui::SeparatorText("File");
    if (ImGui::Button("Save"))
    {
        std::string error;
        editor.status = terrain::save(*terrain, error) ? std::format("Saved {}, {}", terrain->heightmap, terrain->splatmap)
                                                       : "Save failed: " + error;
    }
    ImGui::SetItemTooltip("%s\n%s", terrain->heightmap.c_str(), terrain->splatmap.c_str());
    if (!editor.status.empty())
        ImGui::TextWrapped("%s", editor.status.c_str());

    // ---- player ----
    ImGui::SeparatorText("Player");
    const glm::vec2 spot(terrain->teleport_spot.x, terrain->teleport_spot.z);
    if (ImGui::Button("Teleport player"))
    {
        if (const std::optional<float> height = terrain::height_at(data, spot))
            teleport_player(glm::vec3(spot.x, *height + 0.05f, spot.y));
    }
    ImGui::SetItemTooltip("To the Terrain's teleport_spot (%.1f, %.1f): outside of Sponza", spot.x, spot.y);
    if (cv_terrain_brush.get())
    {
        ImGui::SameLine();
        ImGui::TextDisabled("middle click: to the cursor");
    }

    ImGui::End();
}


/************************************************************************
 * SKY
 ***********************************************************************/

void EngineDebugUI::register_sky_commands(Engine& engine)
{
    auto add = [this] (std::string_view name, std::string_view description, cvar::Command::Handler handler,
        std::string_view usage = {}, cvar::Command::Completer completer = {}) {
        console_commands.push_back(std::make_unique<cvar::Command>(name, description, std::move(handler), usage,
            std::move(completer)));
    };
    auto controller_of = [&engine] () -> SkyController* {
        SkyController* controller = sky::find_controller(engine.world->registry);
        if (!controller)
            cvar::print("No SkyController in the level", cvar::Output::error);
        return controller;
    };
    auto parse = [] (const std::string& text, float& value) {
        return std::from_chars(text.data(), text.data() + text.size(), value).ec == std::errc{};
    };

    add("sky.time", "Prints / sets the time of day, hours (12 = noon)",
        [controller_of, parse] (cvar::Args args) {
            SkyController* controller = controller_of();
            if (!controller)
                return;
            float hours = 0.0f;
            if (!args.empty() && !parse(args[0], hours))
            {
                cvar::print("sky.time [hours]", cvar::Output::error);
                return;
            }
            if (!args.empty())
                controller->time_of_day = hours;
            cvar::print(std::format("time of day {} (sun {:.1f} degrees above the horizon)",
                sky::format_time(controller->time_of_day), controller->sun_elevation));
        }, "[hours]");

    add("sky.day_length", "Prints / sets the seconds a full day takes (0: the time stands still)",
        [controller_of, parse] (cvar::Args args) {
            SkyController* controller = controller_of();
            if (!controller)
                return;
            float seconds = 0.0f;
            if (!args.empty() && !parse(args[0], seconds))
            {
                cvar::print("sky.day_length [seconds]", cvar::Output::error);
                return;
            }
            if (!args.empty())
                controller->day_length = std::max(seconds, 0.0f);
            cvar::print(std::format("day length {} s", controller->day_length));
        }, "[seconds]");

    add("sky.pause", "Freezes / resumes the time of day, toggles without a value",
        [controller_of] (cvar::Args args) {
            SkyController* controller = controller_of();
            if (!controller)
                return;
            controller->paused = args.empty() ? !controller->paused : (args[0] == "true" || args[0] == "1" || args[0] == "on");
            cvar::print(std::format("time of day {}", controller->paused ? "paused" : "running"));
        }, "[true|false]", [] (size_t) { return std::vector<std::string>{ "true", "false" }; });

    add("sky.weather", "Prints / sets the weather preset of the sky controller",
        [controller_of] (cvar::Args args) {
            SkyController* controller = controller_of();
            if (!controller)
                return;
            if (!args.empty())
            {
                if (!controller->weather_presets.contains(args[0]))
                {
                    std::string names;
                    for (const auto& [name, preset] : controller->weather_presets)
                        names += (names.empty() ? "" : ", ") + name;
                    cvar::print(std::format("No weather preset '{}' (presets: {})", args[0], names), cvar::Output::error);
                    return;
                }
                controller->weather = args[0];
            }
            cvar::print(std::format("weather {}", controller->weather));
        }, "[preset]", [&engine] (size_t arg_index) {
            std::vector<std::string> names;
            if (const SkyController* controller = arg_index == 0 ? sky::find_controller(engine.world->registry) : nullptr)
                for (const auto& [name, preset] : controller->weather_presets)
                    names.push_back(name);
            return names;
        });
}

void EngineDebugUI::draw_sky_window(Engine& engine)
{
    if (!begin_window("Sky", cv_window_sky, viewport_point(0.70f, 0.08f), viewport_size(0.24f, 0.5f)))
        return;

    ecs::Entity entity;
    SkyController* controller = sky::find_controller(engine.world->registry, &entity);
    if (!controller)
    {
        ImGui::TextDisabled("No SkyController in the level");
        ImGui::End();
        return;
    }

    ImGui::SeparatorText("Time of day");
    ImGui::SliderFloat("##time", &controller->time_of_day, 0.0f, 24.0f, sky::format_time(controller->time_of_day).c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Paused", &controller->paused);
    ImGui::DragFloat("Day length, s", &controller->day_length, 1.0f, 0.0f, 86400.0f, "%.0f");
    ImGui::SetItemTooltip("Seconds a full day takes (0: the time stands still). The day rail scales it by the hour");
    ImGui::TextDisabled("Sun %.1f degrees above the horizon, time x%.1f", controller->sun_elevation,
        sky::evaluate_rail(*controller, controller->time_of_day).time_scale);

    ImGui::SeparatorText("Weather");
    if (ImGui::BeginCombo("Preset", controller->weather.c_str()))
    {
        for (const auto& [name, preset] : controller->weather_presets)
            if (ImGui::Selectable(name.c_str(), name == controller->weather))
                controller->weather = name;
        ImGui::EndCombo();
    }
    ImGui::DragFloat("Transition, s", &controller->weather_transition, 0.1f, 0.0f, 600.0f, "%.1f");
    if (controller->weather_blend < 1.0f)
        ImGui::ProgressBar(controller->weather_blend, ImVec2(-1.0f, 0.0f), "changing");

    // the preset itself: the sky follows its edits
    if (const auto preset = controller->weather_presets.find(controller->weather); preset != controller->weather_presets.end())
        ui::edit_properties(reflect::make_property_object(preset->second), nullptr, "##weather_preset");
    else
        ImGui::TextDisabled("No preset '%s': the sky keeps its authored look", controller->weather.c_str());

    ImGui::Separator();
    if (ImGui::Button("Select the sky entity"))
    {
        select(entity);
        cv_window_inspector.set(true);
    }
    ImGui::SetItemTooltip("Atmosphere and cloud settings the controller does not drive are in the inspector");

    ImGui::End();
}
