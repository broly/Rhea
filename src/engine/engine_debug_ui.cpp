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
import input;
import ui;
import cvar;


namespace
{
    // open windows, saved with the other cvars
    cvar::Var<bool> cv_window_outliner("ui.windows.outliner", true, "World outliner window");
    cvar::Var<bool> cv_window_inspector("ui.windows.inspector", true, "Inspector of the selected actor");
    cvar::Var<bool> cv_window_render("ui.windows.render", true, "Renderer settings window");
    cvar::Var<bool> cv_window_stats("ui.windows.stats", true, "Frame stats window");
    cvar::Var<bool> cv_window_gpu_profiler("ui.windows.gpu_profiler", false, "GPU pass timings window");
    cvar::Var<bool> cv_window_cvars("ui.windows.cvars", false, "Console variables catalog");
    cvar::Var<bool> cv_window_world_scripts("ui.windows.world_scripts", true, "World scripts window");
    cvar::Var<bool> cv_window_help("ui.windows.help", false, "Hotkeys window");
    cvar::Var<bool> cv_window_imgui_demo("ui.windows.imgui_demo", false, "Dear ImGui demo window (widget reference)");

    cvar::Var<bool> cv_viewport_picking("ui.viewport.picking", true,
        "Click in the viewport selects the actor under the cursor");
    cvar::Var<bool> cv_viewport_selection_bounds("ui.viewport.selection_bounds", true,
        "Bounding box of the selected actor");
    cvar::Var<bool> cv_viewport_icons("ui.viewport.icons", true,
        "Icons of actors without geometry (lights, cameras, probes)");

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

    std::shared_ptr<RhComp_Transform> find_transform(const RhActor& actor)
    {
        return actor.find_component<RhComp_Transform>();
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

    for (const auto& actor : engine.world->get_actors())
    {
        auto camera = actor->find_component<RhComp_Camera>();
        if (!camera || !camera->active)
            continue;

        // same as RenderObject_Camera::get_projection
        glm::mat4 proj = glm::perspectiveRH_ZO(camera->fov, io.DisplaySize.x / io.DisplaySize.y,
            camera->near_plane, camera->far_plane);
        proj[1][1] *= -1;

        view.valid = true;
        view.position = camera->transform.position.glm();
        view.view_proj = proj * camera->transform.get_view();
        view.viewport = glm::vec2(io.DisplaySize.x, io.DisplaySize.y);
        break;
    }
    return view;
}


/************************************************************************
 * FRAME
 ***********************************************************************/

void EngineDebugUI::select(const std::shared_ptr<RhActor>& actor)
{
    selected = actor;
    scroll_outliner_to_selection = actor != nullptr;
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
    if (cv_window_imgui_demo.get())
    {
        bool open = true;
        ImGui::ShowDemoWindow(&open);
        if (!open)
            cv_window_imgui_demo.set(false);
    }

    draw_viewport_overlay(engine, view);
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
        if (ImGui::MenuItem("Hide UI", "`"))
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
        item("Actor icons", cv_viewport_icons);
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

    const auto& actors = engine.world->get_actors();
    ImGui::SetNextItemWidth(-FLT_MIN);
    ImGui::InputTextWithHint("##filter", "Filter by name or class", &outliner_filter);
    ImGui::TextDisabled("%zu actors", actors.size());

    const std::shared_ptr<RhActor> current = selected.lock();

    if (ImGui::BeginChild("##actors", ImVec2(0, 0), ImGuiChildFlags_Borders))
    {
        for (const auto& actor : actors)
        {
            const std::string& name = actor->name.to_string();
            const std::string& type = actor->get_type_name().to_string();
            if (!contains_case_insensitive(name, outliner_filter) && !contains_case_insensitive(type, outliner_filter))
                continue;

            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
            if (actor == current)
                flags |= ImGuiTreeNodeFlags_Selected;
            if (actor->instanced_components.empty())
                flags |= ImGuiTreeNodeFlags_Leaf;

            const bool open = ImGui::TreeNodeEx(actor.get(), flags, "%s", name.c_str());
            if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                select(actor);
            if (actor == current && scroll_outliner_to_selection)
            {
                ImGui::SetScrollHereY();
                scroll_outliner_to_selection = false;
            }
            ImGui::SameLine();
            ImGui::TextDisabled("%s", type.c_str());

            if (open)
            {
                for (const auto& component : actor->instanced_components)
                    ImGui::BulletText("%s  (%s)", component->name.to_string().c_str(),
                        component->get_type_name().to_string().c_str());
                ImGui::TreePop();
            }
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void EngineDebugUI::draw_inspector(Engine& engine)
{
    if (!begin_window("Inspector", cv_window_inspector, viewport_point(0.0f, 0.45f), viewport_size(0.2f, 0.55f)))
        return;

    const std::shared_ptr<RhActor> actor = selected.lock();
    if (!actor)
    {
        ImGui::TextWrapped("Nothing selected.");
        ImGui::TextDisabled("Click an object in the viewport or an actor in the Outliner.");
        ImGui::End();
        return;
    }

    ImGui::Text("%s", actor->name.to_string().c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("%s  #%u", actor->get_type_name().to_string().c_str(), actor->unique_id);
    ImGui::SameLine();
    const float button_width = ImGui::CalcTextSize("Deselect").x + ImGui::GetStyle().FramePadding.x * 2.0f;
    ImGui::SetCursorPosX(ImGui::GetCursorPosX() + std::max(0.0f, ImGui::GetContentRegionAvail().x - button_width));
    if (ImGui::SmallButton("Deselect"))
    {
        select(nullptr);
        ImGui::End();
        return;
    }

    // [[=rh::edit]] fields of an object, notifies it about changes
    auto edit_object = [] (RhObject& object, const char* table_id) {
        const reflect::ObjectReflectionInfo* info = reflect::find_object_reflection_info(object.get_type_name());
        const reflect::PropertyObject properties = info && info->get_properties
            ? info->get_properties(&object)
            : reflect::PropertyObject{};
        if (properties.empty())
        {
            ImGui::TextDisabled("No [[=rh::edit]] fields");
            return;
        }
        std::vector<std::string_view> changed;
        if (ui::edit_properties(properties, &changed, table_id))
            for (std::string_view property : changed)
                object.on_property_changed(property);
    };

    const reflect::ObjectReflectionInfo* actor_info = reflect::find_object_reflection_info(actor->get_type_name());
    if (actor_info && actor_info->get_properties && !actor_info->get_properties(actor.get()).empty())
    {
        ImGui::SeparatorText("Actor");
        edit_object(*actor, "##actor_properties");
    }

    ImGui::SeparatorText("Components");
    for (const auto& component : actor->instanced_components)
    {
        ImGui::PushID(component.get());
        const std::string header = component->name.to_string() + "  (" + component->get_type_name().to_string() + ")";
        if (ImGui::CollapsingHeader(header.c_str(), ImGuiTreeNodeFlags_DefaultOpen))
            edit_object(*component, "##component_properties");
        ImGui::PopID();
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
    ImGui::Text("Actors: %zu", engine.world->get_actors().size());
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

void EngineDebugUI::draw_help_window()
{
    if (!begin_window("Hotkeys", cv_window_help, viewport_point(0.35f, 0.25f), viewport_size(0.3f, 0.5f)))
        return;

    static constexpr std::pair<const char*, const char*> hotkeys[] = {
        {"`", "Show / hide the debug UI"},
        {"Click", "Select the actor under the cursor"},
        {"LMB drag", "Rotate the camera"},
        {"WASD / Q E", "Move (free camera) or walk (character)"},
        {"C", "Character / free camera"},
        {"Mouse wheel", "Field of view (free camera)"},
        {"F / V", "Frame the character / face close-up"},
        {"T / N / 0", "Next pose / expression, back to locomotion"},
        {"L / M", "Next lighting preset / freeze the sun"},
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

    ImDrawList* draw_list = ImGui::GetBackgroundDrawList();
    const std::shared_ptr<RhActor> current = selected.lock();

    auto to_imvec = [] (glm::vec2 p) { return ImVec2(p.x, p.y); };

    if (cv_viewport_icons.get())
    {
        for (const auto& actor : engine.world->get_actors())
        {
            if (!is_empty_aabb(actor->get_aabb()))
                continue;
            const auto transform = find_transform(*actor);
            if (!transform)
                continue;
            // the active camera is the view itself
            if (auto camera = actor->find_component<RhComp_Camera>(); camera && camera->active)
                continue;
            const auto screen = view.project(transform->transform.position.glm());
            if (!screen)
                continue;

            ImU32 color = IM_COL32(200, 200, 200, 200);
            if (auto light = actor->find_component<RhComp_Light>())
            {
                const glm::vec3 c = glm::clamp(glm::vec3(light->color.x, light->color.y, light->color.z), glm::vec3(0.0f), glm::vec3(1.0f));
                color = IM_COL32(int(c.x * 255), int(c.y * 255), int(c.z * 255), 230);
            }
            draw_list->AddCircleFilled(to_imvec(*screen), 5.0f, color);
            draw_list->AddCircle(to_imvec(*screen), 6.0f, IM_COL32(0, 0, 0, 180), 0, 1.5f);
            draw_list->AddText(ImVec2(screen->x + 9.0f, screen->y - 8.0f), IM_COL32(230, 230, 230, 180),
                actor->name.to_string().c_str());
        }
    }

    if (current && cv_viewport_selection_bounds.get())
    {
        const AABB aabb = current->get_aabb();
        std::optional<glm::vec2> label_pos;
        if (!is_empty_aabb(aabb))
        {
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
        else if (const auto transform = find_transform(*current))
        {
            label_pos = view.project(transform->transform.position.glm());
            if (label_pos)
                draw_list->AddCircle(to_imvec(*label_pos), 10.0f, selection_color, 0, 2.5f);
        }

        if (label_pos)
            draw_list->AddText(ImVec2(label_pos->x + 12.0f, label_pos->y - 18.0f), selection_color,
                current->name.to_string().c_str());
    }
}

void EngineDebugUI::handle_picking(Engine& engine, const ViewData& view)
{
    if (!view.valid || !cv_viewport_picking.get())
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

    std::shared_ptr<RhActor> best_icon;
    float best_icon_distance = icon_pick_radius;
    std::shared_ptr<RhActor> best_box;
    float best_box_t = std::numeric_limits<float>::max();

    for (const auto& actor : engine.world->get_actors())
    {
        const AABB aabb = actor->get_aabb();
        if (is_empty_aabb(aabb))
        {
            // actors without geometry are picked by their icon
            const auto transform = find_transform(*actor);
            if (!transform || !cv_viewport_icons.get())
                continue;
            if (auto camera = actor->find_component<RhComp_Camera>(); camera && camera->active)
                continue;
            if (const auto screen = view.project(transform->transform.position.glm()))
            {
                const float distance = glm::length(*screen - mouse);
                if (distance < best_icon_distance)
                {
                    best_icon_distance = distance;
                    best_icon = actor;
                }
            }
        }
        else if (const auto t = intersect_ray_aabb(origin, direction, aabb); t && *t < best_box_t)
        {
            best_box_t = *t;
            best_box = actor;
        }
    }

    select(best_icon ? best_icon : best_box);
}
