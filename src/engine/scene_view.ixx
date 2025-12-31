export module engine:scene_view;
import <vector>;

import render;
import framework;
import <memory>;
import render;
import <unordered_map>;

import <any>;

import :scene_view_processor;
import framework;

export class SceneView
{
    friend class SceneViewProcessor;
public:
    
    SceneView(std::shared_ptr<class World> in_world, std::shared_ptr<class Renderer> in_renderer);

    void perform_extraction();
    
    template<typename T>
    RenderId register_scene_view_proxy()
    {
        SceneViewProcId id = T::type_id;
        return processors[id]->register_proxy();
    }
    
    template<typename T>
    void unregister_scene_view_proxy(RenderId rid)
    {
        SceneViewProcId svp_id = T::type_id;
        processors[svp_id]->unregister_proxy(rid);
    }
    
    template<typename T>
    T& get_processor()
    {
        return *reinterpret_cast<T*>(processors[T::type_id].get());
    }
    
    void register_camera(std::shared_ptr<Camera> in_camera);
    
    
    std::shared_ptr<class World> world;
    
    RenderMaterial get_or_create_material(const MaterialKey& material_key);
    
    std::unordered_map<MaterialKey, RenderMaterial, MaterialKeyHash> material_cache;

    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<Camera> camera;
    
    std::vector<std::unique_ptr<SceneViewProcessor>> processors;
};
