#pragma once
#include "render/renderer.h"
#include "render/render_graph.h"



class GameRenderer : public Renderer
{
public:
    GameRenderer()
        : camera_buffer(0)
    {
    }

    void init(RBWindowHandle in_window, std::shared_ptr<World> in_world) override;
    void execute() override;
    
    RBBufferHandle camera_buffer;
    RBDescriptorSetLayout camera_layout;
    
    std::shared_ptr<RenderGraph> render_graph;
    std::unique_ptr<RenderBackend> render_backend;
};
