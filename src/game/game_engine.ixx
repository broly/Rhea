module;

#include <json/value.h>

export module game:engine;

import std.compat;
import rhobject;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import engine;

#include "object/object_reflection_macro.h"

class GameEngine : public Engine
{
public:
    void engine_init() override;
    
    void on_debug_ui_menu() override;
    void on_debug_ui_render_panel() override;
    void on_debug_ui_windows_menu() override;
    void on_debug_ui_windows() override;
    
    // rebakes every reflection probe (G)
    void rebake_reflection_probes();

private:
    // Windows > Post Process: presets and the render.post.* settings
    void draw_post_process_window();
};
RH_OBJECT(GameEngine)