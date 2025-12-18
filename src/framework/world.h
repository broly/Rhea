#pragma once
#include <chrono>
#include <filesystem>
#include <memory>
#include <vector>

#include "engine_clock.h"
#include "world_script.h"

namespace std::chrono
{
    struct steady_clock;
}

struct RhActor;

class World : public std::enable_shared_from_this<World>
{
public:
    void tick();
    void init();
    
    bool load_bootstrap_level();
    
    bool load_level(std::filesystem::path level_path);
    
    void add_actor(std::shared_ptr<RhActor> actor);
    
    template<typename T>
    void add_script()
    {
        auto script = std::make_unique<T>();
        scripts.push_back(std::move(script));
    }

    double get_time_seconds() const
    {
        return clock->get_total_seconds();
    }
    
    void set_clock(std::shared_ptr<EngineClock> in_clock)
    {
        clock = in_clock;
    }

    std::shared_ptr<Camera> get_camera()
    {
        return camera;
    }
    
    const std::vector<std::shared_ptr<RhActor>>& get_actors()
    {
        return actors;
    }


    std::shared_ptr<Camera> camera;
    std::vector<std::unique_ptr<WorldScript>> scripts;
    std::shared_ptr<EngineClock> clock;
    std::vector<std::shared_ptr<RhActor>> actors;
};
