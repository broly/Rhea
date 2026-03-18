export module render:renderer;

import <memory>;
import <map>;

import :handle_types;
import :render_backend;
import :material_model;
import :pipeline_family;
import :common;
import :rg_params;

#include "common/reflect_macros.h"

export class MaterialInstance;
export class RenderGraph;
export class RenderGraphContext;
export class MaterialManager;

export std::function<void()> NVTX_Start = nullptr;
export std::function<void()> NVTX_Finish = nullptr;

export class Renderer : public std::enable_shared_from_this<Renderer>
{
public:
    virtual ~Renderer() = default;
    
    
    virtual void init(RBWindowHandle in_window);
    virtual void execute();
    
    void execute_graph(std::shared_ptr<RenderGraph>& rg, const RenderGraphParameters& params = {}, RGPostRenderCallback callback = nullptr);
    
    std::shared_ptr<RenderGraph> create_render_graph(
        Name render_graph_name, 
        const std::map<Name, bool>& parameters, 
        std::optional<Name> aux_graph_name = std::nullopt);
    
    void trigger_aux_rg_once(Name aux_rg_name, const RenderGraphParameters& params, RGPostRenderCallback callback);

public:  // public API
    void set_flag(Name name, bool value, bool needs_rebuild = false);
    void toggle_flag(Name name, bool needs_rebuild = false);

    RBImageHandle create_texture_from_asset(TextureHandle handle, bool generate_mips = true, 
        RBImageLayout initial_layout = RBImageLayout::undefined,
        RBImageLayout final_layout = RBImageLayout::undefined);
    RBImageHandle create_cubemap_from_asset(CubemapHandle handle);
    RBImageHandle get_texture(TextureHandle handle);
    RBImageHandle get_cubemap(CubemapHandle handle);
    RenderResource* find_resource(Name resource_name) const;
    RenderResource& find_resource_checked(Name resource_name) const;
    std::shared_ptr<RenderResourceInfo> find_resource_info(Name resource_name) const;
    std::shared_ptr<PipelineFamily> query_pipeline_family(Name pass_name, const std::shared_ptr<MaterialModel>& model_name);
    std::shared_ptr<MaterialInstance> query_material_instance(std::shared_ptr<Material> material, Name pass_name);
    std::shared_ptr<MaterialModel> find_model(Name model_name) const;
    
    RBSampler get_sampler(Name sampler_name) const;

    bool get_render_flag(Name flag) const;

   
    std::shared_ptr<RenderBackend> get_backend() const
    {
        return render_backend;
    }

    std::shared_ptr<MaterialManager> get_material_manager() const
    {
        return material_manager;
    }
protected:  // internal functions
    
    void load_schemas();
    void load_resources();
    
protected:  // internal system accessors
    std::shared_ptr<RenderBackend> render_backend;
    std::shared_ptr<RenderGraph> main_render_graph;
    std::map<Name, std::shared_ptr<RenderGraph>> aux_graphs;
    Name main_render_graph_name;
    
protected: // (semi-)immutable info
    std::map<Name, std::shared_ptr<MaterialModel>> models;
    std::map<Name, std::shared_ptr<RenderResourceInfo>> resources_info;
    std::map<Name, RBSampler> samplers;
    std::map<Name, RenderResource*> resources;
    std::map<std::pair<Name, std::shared_ptr<MaterialModel>>, RenderResource*> material_resources;
    
    std::shared_ptr<MaterialManager> material_manager;
    
    
protected: // materials and resources
    std::map<std::pair<std::shared_ptr<Material>, Name>, std::shared_ptr<MaterialInstance>> material_instances;
    mutable std::map<
        std::pair<Name, std::shared_ptr<MaterialModel>>, 
        std::shared_ptr<PipelineFamily>> material_pipeline_families;

protected:  // textures
    std::map<TextureHandle, RBImageHandle> texture_cache;
    std::map<CubemapHandle, RBImageHandle> cubemap_cache;
    
    struct RenderGraphJob
    {
        Name render_graph_name;
        RenderGraphParameters params;
        RGPostRenderCallback callback;
    };
    
    std::vector<RenderGraphJob> rg_once_jobs;
    
    bool main_render_graph_needs_rebuild = false;
    

    
    std::map<Name, std::shared_ptr<PipelineFamily>> pipeline_families;
};
