export module render:renderer;

import <memory>;
import <map>;

import :handle_types;
import :render_backend;
import :material_model;
import :pipeline_family;

#include "common/reflect_macros.h"

export class MaterialInstance;
export class RenderGraph;

export class Renderer : public std::enable_shared_from_this<Renderer>
{
public:
    virtual ~Renderer() = default;
    
    
    virtual void init(RBWindowHandle in_window);
    virtual void execute();

public:  // public API
    void set_flag(Name name, bool value, bool needs_rebuild = false);
    void toggle_flag(Name name, bool needs_rebuild = false);

    RBImageHandle create_texture_from_asset(TextureHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    RenderResource* find_resource(Name resource_name) const;
    std::shared_ptr<PipelineFamily> query_pipeline_family(Name pass_name, const std::shared_ptr<MaterialModel>& model_name);
    RenderResource* query_resource(std::shared_ptr<MaterialModel> model, Name pass_name);
    std::shared_ptr<MaterialInstance> query_material_instance(std::shared_ptr<Material> material, Name pass_name);
    std::shared_ptr<MaterialModel> find_model(Name model_name) const;
    
    bool get_render_flag(Name flag) const;

protected:  // internal functions
    
    void load_schemas();
    void load_resources();
   
    
protected:  // internal system accessors
    std::shared_ptr<RenderBackend> render_backend;
    std::shared_ptr<RenderGraph> main_render_graph;
    Name main_render_graph_name;
    
protected: // (semi-)immutable info
    std::map<Name, std::shared_ptr<MaterialModel>> models;
    std::map<Name, std::shared_ptr<RenderResourceInfo>> resources_info;
    std::map<Name, RBSampler> samplers;
    std::map<Name, RenderResource*> resources;
    std::map<std::pair<Name, std::shared_ptr<MaterialModel>>, RenderResource*> material_resources;
    
    
protected: // materials and resources
    std::map<std::pair<std::shared_ptr<Material>, Name>, std::shared_ptr<MaterialInstance>> material_instances;
    mutable std::map<
        std::pair<Name, std::shared_ptr<MaterialModel>>, 
        std::shared_ptr<PipelineFamily>> material_pipeline_families;

protected:  // textures
    std::map<TextureHandle, RBImageHandle> texture_cache;
    
    
    bool main_render_graph_needs_rebuild = false;
    

    
};
