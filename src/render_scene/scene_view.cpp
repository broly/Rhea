module render_scene:scene_view;

import <cassert>;
import framework;
import :scene_view_processor;

SceneView::SceneView(std::shared_ptr<class World> in_world, std::shared_ptr<class Renderer> in_renderer)
{
    world = in_world;
    renderer = in_renderer;
    
    for (auto type_info : reflect::get_subtypes<SceneViewProcessor>())
    {
        auto ptr = type_info->instantiate_unique<SceneViewProcessor>();
        processors.push_back(std::move(ptr));
    }
    
    for (const auto& factory : SceneViewProcessor::get_factories())
    {
        if (factory.has_value())
        {
            const auto& func = factory.value();
            processors.push_back(func());
        } else
        {
            processors.push_back(nullptr); // to access via correct index
        }
    }
}


void SceneView::perform_extraction()
{
    for (auto& processor : processors)
        if (processor)
            processor->process();
}

void SceneView::submit_raw(SceneViewProcId svp_id, const void* scene_proxy_ptr)
{
    processors[svp_id]->submit(scene_proxy_ptr);
    if (world.get() != nullptr)
        world_aabb = world->get_world_aabb();  // todo: crutch
}


