export module render_scene:scene_view_processor;

import <functional>;
import <optional>;
import <cassert>;
import <any>;
import <memory>;
import guard_value;
import generator;
import :render_id;
import <string>;
import rhmath;
#include "common/type_macros.h"


export class SceneView;


export struct SceneViewProxy
{
    RenderId render_id;
    std::string debug_name;
};

export struct SceneViewProxy_Transform : SceneViewProxy
{
    Transform transform;
};

using ProxySize = unsigned short int;

export class SceneViewProcessor
{
    friend SceneView;
public:
    NON_COPYABLE(SceneViewProcessor);
    
    SceneViewProcessor(SceneView& in_view)
        : view(in_view)
    {}
    
    virtual RenderId register_proxy() = 0;
    virtual void unregister_proxy(RenderId Id) = 0;
    virtual void process() = 0;
    virtual ~SceneViewProcessor() {}

    
protected:
    SceneView& view;
    
    static constexpr ProxySize invalid_proxy_size = std::numeric_limits<ProxySize>::max();
    ProxySize scene_proxy_size = invalid_proxy_size;
    
    template<typename T>
    __declspec(noinline) Generator<T> read_submission_buffer()
    {
        const Guard _(reading, true);
        
        assert(scene_proxy_size != 0 && scene_proxy_size != invalid_proxy_size);
        
        for (size_t proxy_position = 0; 
            proxy_position < submission_buffer.size(); proxy_position += scene_proxy_size)
        {
            std::byte* ptr = (submission_buffer.data() + proxy_position);
            co_yield *reinterpret_cast<T*>(ptr);
        }
        
        submission_buffer.erase(submission_buffer.begin(), submission_buffer.end());
    }
    
    void submit(const void* raw_proxy)
    {
        
        assert(scene_proxy_size != invalid_proxy_size);
        size_t current_buffer_size = submission_buffer.size();
        submission_buffer.resize(current_buffer_size + scene_proxy_size, std::byte{});
        memcpy(submission_buffer.data() + current_buffer_size, raw_proxy, scene_proxy_size);
    }
    
    bool reading = false;
    std::vector<std::byte> submission_buffer;
    
    using FactoryResult = std::unique_ptr<SceneViewProcessor>;
    using Factory = std::optional<std::function<FactoryResult(SceneView& scene_view)>>;
    
public:
    template<typename T>
    static SceneViewProcId register_scene_view_processor()
    {
        SceneViewProcId svp_id = T::type_id;
        
        auto& factories = get_factories();
        
        if (svp_id >= factories.size())
            factories.resize(svp_id + 1, std::nullopt);
        
        // factory could be registered once!
        assert(factories[svp_id] == std::nullopt);
        
        factories[svp_id] = [] (SceneView& scene_view)
        {
            return std::make_unique<T>(scene_view);
        };
        return svp_id;
    }
    
private:
    static std::vector<Factory>& get_factories()
    {
        static std::vector<Factory> factories;
        return factories;
    }
};

