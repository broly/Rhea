#pragma once
#include <memory>

#include "handle_types.h"
#include "render_objects.h"

class RenderBackend;
class World;

struct MaterialUBO
{
    glm::vec4 base_color;

    float metallic;
    float roughness;

    int has_normal;
    int has_occlusion;
};


class Renderer
{
public:
    virtual ~Renderer() = default;
    virtual void init(RBWindowHandle in_window, std::shared_ptr<World> in_world);
    void update_material_descriptor(const RenderMaterial& rm, const MaterialKey& key);
    
    virtual void execute() {}

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    
    std::shared_ptr<World> world;
    std::shared_ptr<RenderBackend> render_backend;
    
    std::map<TextureHandle, RBImageHandle> texture_cache;
    
};
