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
    
    // bakes IBL cubemaps of every RhComp_ReflectionCapture (G)
    void capture_reflection_probes();
};
RH_OBJECT(GameEngine)