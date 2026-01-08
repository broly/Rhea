export module engine:engine;
import <memory>;

import rhobject;
import framework;
import platform;
import assets;
import render;
import render_scene;
import input;

#include "object/object_reflection_macro.h"



export class Engine : public RhObject
{
public:
    virtual void init();
    
    void run();

    void init_render_graph(RenderBackend& render_backend, RenderGraph& render_graph);
    
    std::shared_ptr<AssetManager> asset_manager = nullptr;
    platform::window::Window window = nullptr;
    std::shared_ptr<Input> input = nullptr;
    RBWindowHandle window_handle;
    std::shared_ptr<World> world = nullptr;
    std::shared_ptr<SceneView> scene_view = nullptr;
    std::shared_ptr<Renderer> renderer = nullptr;
};
REFLECT_OBJECT(Engine, RhObject);