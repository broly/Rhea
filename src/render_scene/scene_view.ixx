export module render_scene:scene_view;
import <vector>;

import <memory>;
import <unordered_map>;

import <any>;

import name;

import :scene_view_processor;

export class SceneView
{
    friend class SceneViewProcessor;
public:
    
    SceneView(std::shared_ptr<class World> in_world, std::shared_ptr<class Renderer> in_renderer);

    void perform_extraction();
    
    template<typename T>
    requires std::is_base_of_v<SceneViewProxy, T>
    void register_scene_view_proxy(T& proxy, SceneViewProcId id, Name name)
    {
        proxy.debug_name = name;
        proxy.render_id = processors[id]->register_proxy();
        submit_raw(id, &proxy);
    }
    
    template<typename T>
    void unregister_scene_view_proxy(const T& proxy, SceneViewProcId svp_id)
    {
        processors[svp_id]->unregister_proxy(proxy.render_id);
    }
    
    template<typename T>
    T& get_processor()
    {
        return *reinterpret_cast<T*>(processors[T::type_id].get());
    }
    
    
    template<typename P, typename T>
    requires std::is_base_of_v<SceneViewProxy, T>
    void push(const T& proxy)
    {
        SceneViewProcessor* procesor_raw = processors[P::type_id].get();
        P* processor_typed = reinterpret_cast<P*>(procesor_raw);
        processor_typed->push(proxy);
    }
    
    void submit_raw(SceneViewProcId svp_id, const void* scene_proxy_ptr)
    {
        processors[svp_id]->submit(scene_proxy_ptr);
    }
    
    
    std::shared_ptr<class World> world;

    std::shared_ptr<Renderer> renderer;
    
    std::vector<std::unique_ptr<SceneViewProcessor>> processors;
};
