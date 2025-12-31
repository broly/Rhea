export module engine:scene_view_proxy.mesh;


import :scene_view_processor;
import render;
#include "common/type_macros.h"


export class SceneViewProcessor_Mesh : public SceneViewProcessor
{
public:
    static constexpr SceneViewProcId type_id = 0;
    
    using SceneViewProcessor::SceneViewProcessor;
    
    RenderId register_proxy() override;

    void unregister_proxy(RenderId render_id) override;

    void extract() override;

    std::vector<RenderObject_Mesh> meshes;
    
    std::vector<RenderId> vacated_mesh_ids; 
};

