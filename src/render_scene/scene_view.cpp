module render_scene:scene_view;

import <cassert>;

import :scene_view_processor;

SceneView::SceneView(std::shared_ptr<class World> in_world, std::shared_ptr<class Renderer> in_renderer)
{
    world = in_world;
    renderer = in_renderer;
    
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

