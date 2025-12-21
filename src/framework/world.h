#pragma once
#include <chrono>
#include <memory>
#include <vector>

#include "engine_clock.h"
#include "world_script.h"

class RhActor;
class SceneExtractor;

class World : public std::enable_shared_from_this<World>
{
public:
    void tick();
    void init();
    
    bool load_bootstrap_level();
    
    bool load_level(std::string level_path);
    
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
    
    std::shared_ptr<SceneExtractor> get_render_extractor() const
    {
        return render_extractor;
    }


    std::shared_ptr<Camera> camera;
    std::vector<std::unique_ptr<WorldScript>> scripts;
    std::shared_ptr<EngineClock> clock;
    std::vector<std::shared_ptr<RhActor>> actors;
    std::shared_ptr<SceneExtractor> render_extractor;
};
