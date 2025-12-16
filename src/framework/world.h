#pragma once
#include <chrono>
#include <memory>
#include <vector>

#include "engine_clock.h"
#include "world_script.h"

namespace std::chrono
{
    struct steady_clock;
}

class World : public std::enable_shared_from_this<World>
{
public:
    void tick();
    void init();
    
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


    std::shared_ptr<Camera> camera;
    std::vector<std::unique_ptr<WorldScript>> scripts;
    std::shared_ptr<EngineClock> clock;
};
