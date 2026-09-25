module;

#include <GLFW/glfw3.h>

export module ui;

import std.compat;
import properties;
import reflect;
import type_id;


// Debug UI on top of Dear ImGui (docking branch).
//
// The ImGui context, the GLFW platform backend and the generic widgets live here; the render
// backend draws ImGui::GetDrawData() over the swapchain (RenderBackend::render_ui_overlay).
// Modules that draw windows include <imgui.h> in their global module fragment and call ImGui
// directly between ui::begin_frame() and ui::end_frame() (the engine calls them every frame).
export namespace ui
{
    void init(GLFWwindow* window);
    void shutdown();
    bool is_initialized();

    // starts the ImGui frame: platform input, visibility hotkey (`), dock space
    void begin_frame();
    // finishes the frame, ImGui::GetDrawData() is valid after it
    void end_frame();

    // hidden UI draws nothing and never captures input
    bool is_visible();
    void set_visible(bool visible);
    void toggle_visible();

    // ImGui uses the mouse / keyboard this frame (hovered window, active text field)
    bool wants_mouse();
    bool wants_keyboard();

    /************************************************************************
     * PROPERTY EDITOR: reflection driven widgets, see properties.ixx
     ***********************************************************************/

    // Editor of a leaf value of a registered type. label is an ImGui id ("##name").
    using ValueEditor = bool (*)(const char* label, void* value, const reflect::PropertyMeta& meta);
    void register_value_editor(TypeId type, ValueEditor editor);

    template<typename T>
    void register_value_editor(ValueEditor editor)
    {
        register_value_editor(get_type_id<T>(), editor);
    }

    // Two column table (name | value) of all properties of the object.
    // Returns true when something was changed; names of changed top level properties go to *changed.
    bool edit_properties(const reflect::PropertyObject& object, std::vector<std::string_view>* changed = nullptr,
        const char* table_id = "##properties");

    // One property as a table row: must be called inside begin_property_table / end_property_table
    bool edit_property_row(const reflect::PropertyDesc& property, void* value);

    bool begin_property_table(const char* table_id);
    void end_property_table();

    // Value widget alone (no name column), for any property type
    bool edit_value(const reflect::PropertyDesc& property, void* value, const char* label);

    // "(?)" with a tooltip
    void help_marker(const char* text);
}
