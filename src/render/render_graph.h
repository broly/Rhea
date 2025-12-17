#pragma once
#include "render_backend.h"
#include "framework/world.h"

class RenderGraph
{
public:
    void initialize(RBWindowHandle window_handle);
    
    void draw(const Camera& camera);

    std::unique_ptr<RenderBackend> backend;
};
