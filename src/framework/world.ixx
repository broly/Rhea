export module framework:world;

import <memory>;
import <string>;
import :engine_clock;
import :camera;
import :core;
// import :scene_extractor;


export class World : public std::enable_shared_from_this<World>
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

    double get_time_seconds() const;

    void set_clock(std::shared_ptr<EngineClock> in_clock)
    {
        clock = in_clock;
    }
    
    const std::vector<std::shared_ptr<RhActor>>& get_actors()
    {
        return actors;
    }
    
    std::shared_ptr<RhActor> find_actor_by_name(const std::string& name);

    std::vector<std::unique_ptr<WorldScript>> scripts;
    std::shared_ptr<EngineClock> clock;
    std::vector<std::shared_ptr<RhActor>> actors;
};
