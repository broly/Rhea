module;

#include <GLFW/glfw3.h>
#include <imgui.h>
#include <imgui_impl_glfw.h>

module ui;

import std.compat;
import paths;
import cvar;


namespace
{
    struct UIState
    {
        bool initialized = false;
        GLFWwindow* window = nullptr;
        std::string ini_path;   // ImGuiIO keeps the pointer
    };

    UIState& get_state()
    {
        static UIState state;
        return state;
    }

    cvar::Var<bool> cv_visible("ui.visible", true, "Debug UI is shown (toggle: Shift+`)");
    cvar::Var<float> cv_scale("ui.scale", 1.0f, "Debug UI scale",
        {.has_range = true, .min = 0.5f, .max = 3.0f});

    void apply_style()
    {
        ImGui::StyleColorsDark();
        ImGuiStyle& style = ImGui::GetStyle();
        style.WindowRounding = 4.0f;
        style.FrameRounding = 3.0f;
        style.GrabRounding = 3.0f;
        style.TabRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.WindowBorderSize = 1.0f;
        style.FramePadding = ImVec2(6.0f, 3.0f);
        style.ItemSpacing = ImVec2(8.0f, 4.0f);
        style.Colors[ImGuiCol_WindowBg].w = 0.92f;
        style.Colors[ImGuiCol_DockingEmptyBg].w = 0.0f;
    }

    void load_fonts()
    {
        ImGuiIO& io = ImGui::GetIO();
        // Segoe UI has Cyrillic; glyphs are rasterized on demand (dynamic fonts, ImGui 1.92+)
        const char* font_path = "C:/Windows/Fonts/segoeui.ttf";
        if (std::filesystem::exists(font_path))
            io.Fonts->AddFontFromFileTTF(font_path, 16.0f);
        else
            io.Fonts->AddFontDefault();
    }
}

void ui::init(GLFWwindow* window)
{
    UIState& state = get_state();
    if (state.initialized)
        return;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    // keyboard navigation is off on purpose: it keeps the keyboard captured while a window is focused
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    state.ini_path = (paths::get_cache_path() / "imgui.ini").string();
    std::filesystem::create_directories(paths::get_cache_path());
    io.IniFilename = state.ini_path.c_str();

    apply_style();
    load_fonts();

    // chains the engine's GLFW callbacks (installed by platform::window_create)
    ImGui_ImplGlfw_InitForVulkan(window, true);

    detail::init_console();

    state.window = window;
    state.initialized = true;
}

void ui::shutdown()
{
    UIState& state = get_state();
    if (!state.initialized)
        return;

    detail::shutdown_console();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
    state = {};
}

bool ui::is_initialized()
{
    return get_state().initialized;
}

void ui::begin_frame()
{
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    // ` also works while typing in the console (the console filters the character out)
    const ImGuiIO& io = ImGui::GetIO();
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent, false) && (!io.WantTextInput || detail::console_has_input_focus()))
    {
        if (io.KeyShift)
            toggle_visible();
        else
            toggle_console();
    }

    ImGui::GetStyle().FontScaleMain = cv_scale.get();

    if (is_visible())
        ImGui::DockSpaceOverViewport(0, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
}

void ui::end_frame()
{
    if (is_console_open())
        detail::draw_console();
    ImGui::Render();
}

bool ui::is_visible()
{
    return cv_visible.get();
}

void ui::set_visible(bool visible)
{
    cv_visible.set(visible);
}

void ui::toggle_visible()
{
    set_visible(!is_visible());
}

bool ui::wants_mouse()
{
    return is_initialized() && (is_visible() || is_console_open()) && ImGui::GetIO().WantCaptureMouse;
}

bool ui::wants_keyboard()
{
    // the open console keeps the keyboard even when its input line lost focus
    return is_initialized() && (is_console_open() || (is_visible() && ImGui::GetIO().WantCaptureKeyboard));
}

void ui::help_marker(const char* text)
{
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::BeginItemTooltip())
    {
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(text);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}
