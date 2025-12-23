#pragma once
#include "render/renderer.h"
#include "render/render_graph.h"

class GameRenderer : public Renderer
{
public:
    void init(RBWindowHandle in_window, std::shared_ptr<World> in_world) override;
    void execute() override;
    
    std::shared_ptr<RenderGraph> render_graph;
    std::shared_ptr<RenderBackend> render_backend;
};
