#pragma once
#include <memory>

#include "handle_types.h"

class World;

class Renderer
{
public:
    virtual ~Renderer() = default;
    virtual void init(RBWindowHandle in_window, std::shared_ptr<World> in_world);;
    
    virtual void execute() {};
    
    std::shared_ptr<World> world;
};
