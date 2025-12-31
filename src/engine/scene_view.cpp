module engine:scene_view;

import framework;
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
            processors.push_back(func(*this));
        } else
        {
            processors.push_back(nullptr); // to access via correct index
        }
    }
}

void SceneView::register_camera(std::shared_ptr<Camera> in_camera)
{
    camera = in_camera;
}

void SceneView::perform_extraction()
{
    for (auto& processor : processors)
        if (processor)
            processor->extract();
}



RenderMaterial SceneView::get_or_create_material(const MaterialKey& key)
{
    auto it = material_cache.find(key);
    if (it != material_cache.end())
        return it->second;
    
    RenderMaterial rm{};
    rm.key = key;
    
    rm.layout = renderer->material_layout;
    rm.descriptor = renderer->allocate_material_descriptor();
    rm.material_ubo = renderer->create_material_ubo();
    renderer->bind_material_ubo(rm);
    
    renderer->update_material_descriptor(rm, key);
    
    auto [new_it, _] = material_cache.emplace(key, rm);
    return new_it->second;
}
