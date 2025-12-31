export module engine:scene_view_processor;

import <functional>;
import <optional>;
import <cassert>;
import <any>;
import <memory>;

import framework;
#include "common/type_macros.h"


export class SceneView;

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
    virtual void extract() = 0;
    virtual ~SceneViewProcessor() {}
    
protected:
    SceneView& view;
    
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