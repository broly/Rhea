export module framework:world_script;

import <iostream>;
import <memory>;
import <ostream>;

import :core;

export class WorldScript
{
public:
    virtual ~WorldScript() = default;
    
    void init(std::shared_ptr<World> in_world)
    {
        world = in_world;
        startup();
    }
    
    
    virtual void startup() {}
    virtual void tick(double dt) {}
    
    std::shared_ptr<World> world;
};
