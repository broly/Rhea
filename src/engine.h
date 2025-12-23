#pragma once
#include <memory>

#include "framework/object.h"
#include "framework/world.h"
#include "framework/assets/asset_manager.h"
#include "platform/window.h"
#include "render/renderer.h"
#include "render/render_backend.h"
#include "render/render_graph.h"


class Engine : public RhObject
{
public:
    virtual void init();
    
    void run();

    void init_render_graph(RenderBackend& render_backend, RenderGraph& render_graph);
    
    std::shared_ptr<AssetManager> asset_manager = nullptr;
    platform::window::Window window = nullptr;
    RBWindowHandle window_handle;
    std::shared_ptr<World> world = nullptr;
    std::shared_ptr<Renderer> renderer = nullptr;
};
REFLECT_OBJECT(Engine, RhObject);