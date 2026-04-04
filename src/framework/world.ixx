export module framework:world;

import <memory>;
import <string>;
import :engine_clock;
import :core;
import dependency_collector;
import rhmath;
import rhobject;
// import :scene_extractor;


export class World : public std::enable_shared_from_this<World>
{
public:
    void tick();
    void init();
    
    bool load_bootstrap_level();
    
    bool load_level(std::string level_path);
    
    void add_actor(std::shared_ptr<RhActor> actor);
    
    std::shared_ptr<RhActor> spawn(const std::string& name);
    
    AABB get_world_aabb() const;

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
    
    template<typename T>
    std::shared_ptr<T> find_actor_by_name(const std::string& name)
    {
        return std::static_pointer_cast<T>(find_actor_by_name(name));
    }
    
    
    std::shared_ptr<RhActor> find_actor_by_name(const std::string& name);

    std::vector<std::unique_ptr<WorldScript>> scripts;
    std::shared_ptr<EngineClock> clock;
    std::vector<std::shared_ptr<RhActor>> actors;
    DependencyCollector collector;
    SerializationContext world_load_serialization_context;
};
