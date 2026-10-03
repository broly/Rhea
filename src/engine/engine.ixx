module;

#include <json/value.h>

export module engine:engine;

import std.compat;
import dependency_collector;
import fixed_string;
import reflect;
import type_id;

import rhobject;
import framework;
import platform;
import assets;
import render;
import render_scene;
import input;
import audio;
import :debug_ui;

#include "object/object_reflection_macro.h"


export class Engine : public RhObject
{
public:
    virtual void engine_init();
    
    void run();
    
    void render_hot_reload();
    
    // Debug UI extension points, called while the UI is visible
    virtual void on_debug_ui_menu() {}             // extra menus of the main menu bar
    virtual void on_debug_ui_render_panel() {}     // top of the "Render" window
    virtual void on_debug_ui_windows_menu() {}     // extra items of the "Windows" menu
    virtual void on_debug_ui_windows() {}          // extra windows, drawn every frame the UI is visible

    platform::window::Window window{};
    std::shared_ptr<Input> input = nullptr;
    RBWindowHandle window_handle;
    std::shared_ptr<World> world = nullptr;
    std::shared_ptr<SceneView> scene_view = nullptr;
    std::shared_ptr<Renderer> renderer = nullptr;
    std::unique_ptr<audio::AudioEngine> audio_engine;
    EngineDebugUI debug_ui;
};
RH_OBJECT(Engine)