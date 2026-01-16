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
import name;
import rhobject;
#include "common/assertion_macros.h"
#include "common/type_macros.h"
#include "object/object_reflection_macro.h"


export class SceneView;


export struct SceneViewProxy
{
    RenderId render_id;
    Name debug_name;
};

export struct SceneViewProxy_Transform : SceneViewProxy
{
    Transform transform;
};

using ProxySize = unsigned short int;

static std::unordered_map<Name, size_t> indices{};

export class SceneViewProcessor : public RhObject
{
    friend SceneView;
public:
    NON_COPYABLE(SceneViewProcessor);
    
    SceneViewProcessor()
        : index(0)
    {}
    
    template<typename T>
    requires std::is_base_of_v<SceneViewProxy_Transform, T>
    static inline size_t scene_view_proxy_size_v = sizeof(T);
    
    void on_init() override
    {
        if (is_default && type_name != reflect::get_object_type_name<SceneViewProcessor>())
        {
            static size_t counter = 0;
            index = counter++;
            indices.insert({type_name, index});
        } else if (!is_default)
        {
            auto info = reflect::find_object_reflection_info(type_name);
            index = static_cast<SceneViewProcessor*>(info->default_object.get())->index;
        }
    }
    
    virtual RenderId register_proxy() { unreachable("todo"); };
    virtual void unregister_proxy(RenderId Id) { unreachable("todo"); };
    virtual void process() { unreachable("todo"); };
    virtual ~SceneViewProcessor() {}

    
protected:
    
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
    using Factory = std::optional<std::function<FactoryResult()>>;
    
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
        
        factories[svp_id] = [] ()
        {
            return std::make_unique<T>();
        };
        return svp_id;
    }
    
    
    size_t index;
private:
    static std::vector<Factory>& get_factories()
    {
        static std::vector<Factory> factories;
        return factories;
    }
};
REFLECT_OBJECT(SceneViewProcessor, RhObject);
