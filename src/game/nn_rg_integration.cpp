module game:nn_denoiser_passes;

import :render_graph;
import :renderer;
import paths;
import log;
#include "logging/log_macro.h"

#include "common/assertion_macros.h"

DEFINE_LOGGER(LogNNDenoiser, Log);

NNPassIndicesSSBO nn_denoiser::make_ubo_from_pass_indices(const NNPassIndicesData& pi)
{
    NNPassIndicesSSBO ubo{};
    ubo.uPwIdx   = pi.uPwIdx;
    ubo.uDwIdx   = pi.uDwIdx;
    ubo.uBiasIdx = pi.uBiasIdx;
    ubo.uAlbedoFlatCh = pi.uAlbedoFlatCh;
    ubo.uBaselineFlatCh = pi.uBaselineFlatCh;

    auto fill = [](glm::ivec4* dst, const auto& src) {
        for (int i = 0; i < 64; ++i) {
            const int v = (i < int(src.size())) ? src[i] : -1;
            dst[i >> 2][i & 3] = v;
        }
    };
    fill(ubo.uActIn,  pi.uActIn);
    fill(ubo.uActOut, pi.uActOut);
    fill(ubo.uActRes, pi.uActRes);
    return ubo;
}

void nn_denoiser::load_nn_denoiser_schemas(NNDenoiserState& state)
{
    SerializationContext ctx;
    ctx.strict_checking_enabled = true;

    auto nn_dir = paths::get_assets_path() / "nn";
    
    auto pipeline_path = nn_dir / "pipeline.json";
    auto pipeline_loaded = json_object::load_object<NNPipeline>(pipeline_path, ctx);
    checkf(pipeline_loaded, "Could not load %s", pipeline_path.string().c_str());
    state.pipeline = pipeline_loaded;

    auto weights_path = nn_dir / "weights_index.json";
    auto weights_loaded = json_object::load_object<NNWeightsIndex>(weights_path, ctx);
    checkf(weights_loaded, "Could not load %s", weights_path.string().c_str());
    state.weights_index = std::move(*weights_loaded);

    // Pre-compute UBO templates
    state.pass_ubo_templates.reserve(state.pipeline->passes.size());
    for (const auto& pass : state.pipeline->passes)
        state.pass_ubo_templates.push_back(make_ubo_from_pass_indices(pass.pass_indices));
}

void nn_denoiser::allocate_nn_gpu_resources(NNDenoiserState& state, RenderGraph& rg, Renderer& renderer,
    RenderBackend& backend, const Extent& swapchain_extent)
{
    const auto& pipeline = state.pipeline;

    state.activation_textures.resize(pipeline->array_sizes.activations);

    for (const auto& tensor : pipeline->tensors)
    {
        const glm::ivec2 size = {
            std::max(1, int(swapchain_extent.width * tensor.scale)),
            std::max(1, int(swapchain_extent.height * tensor.scale))
        };

        for (size_t s = 0; s < tensor.activation_slots.size(); ++s)
        {
            const uint32_t slot = tensor.activation_slots[s];
            const std::string name = "nn_act_" + tensor.name.to_string()
                + "_s" + std::to_string(s);

            RGTextureDesc desc{};
            desc.name = Name(name);
            desc.extent = size;
            desc.format = TextureFormat::RGBA16F;
            desc.usage = RenderTextureUsage::Storage | RenderTextureUsage::Sampled;
            desc.external = false;

            state.activation_textures[slot] = rg.create_texture(desc);
        }
    }

    // === 2. Weight textures (RBImageHandle, persistent) ===
    state.pw_weight_textures.resize(pipeline->array_sizes.pw_weights);
    state.dw_weight_textures.resize(pipeline->array_sizes.dw_weights);
    state.bias_textures.resize(pipeline->array_sizes.biases);

    std::map<std::string, std::pair<NNWeightKind, uint32_t>> seen;  // weight_name -> (kind, slot)
    for (const auto& pass : pipeline->passes)
    {
        for (const auto& wref : pass.weight_refs)
        {
            if (seen.contains(wref.weight_name)) continue;
            seen[wref.weight_name] = {wref.kind, wref.slot};
        }
    }

    for (const auto& [wname, kind_slot] : seen)
    {
        const auto& [kind, slot] = kind_slot;
        const NNWeightDesc& desc = state.weights_index.find(wname);

        RBImageHandle img = state.weights_index.create_gpu_texture(backend, desc);
        RGTextureDesc tex_desc;
        tex_desc.name = "nn_weights_" + wname + "_" + reflect::enum_name(kind_slot.first).to_string() + "_" + std::to_string(kind_slot.second);
        tex_desc.dimension = TextureDimension::Tex2D;
        tex_desc.extent = {desc.width, desc.height};
        tex_desc.format = TextureFormat::RGBA16F;
        tex_desc.imported = true;
        tex_desc.external = false;
        tex_desc.num_frames = 1;
        tex_desc.num_mip_levels = 1;
        tex_desc.num_layers = desc.depth;
        tex_desc.swapchain_image = false;
        tex_desc.optional_image = img;
        
        LogNNDenoiser.Log<Display>("Register weight texture '%s', w=%i,h=%i,d=%i",
            tex_desc.name.to_string().c_str(), desc.width, desc.height, desc.depth);
        
        RGTextureHandle tex = rg.create_texture(tex_desc);
        

        if (kind == NNWeightKind::pointwise)
            state.pw_weight_textures[slot] = tex;
        else if (kind == NNWeightKind::depthwise)
            state.dw_weight_textures[slot] = tex;
        else if (kind == NNWeightKind::bias)    
            state.bias_textures[slot] = tex;
        else 
            checkf(false, "Unknown weight kind: %s", reflect::enum_name(kind).to_string().c_str());
    }

    auto& nn_resource = renderer.find_resource_checked(Name("nn_denoiser"));

    nn_resource.update_image_array(Name("u_nn_pw_weights"), rg.get_image_array(state.pw_weight_textures));
    nn_resource.update_image_array(Name("u_nn_biases"),     rg.get_image_array(state.bias_textures));

    {
        UpdateImageParams dw_params{};
        dw_params.as_array_2d = true;
        nn_resource.update_image_array(
            Name("u_nn_dw_weights"),
            rg.get_image_array(state.dw_weight_textures),
            dw_params);
    }
    
    nn_resource.update_ssbo(Name("u_nn_pass_indices"), state.pass_ubo_templates);
    
    
    // === 4. Pre-fetch pipeline families ===
    auto nn_model = renderer.find_model(Name("nn_denoise"));
    checkf(nn_model, "MaterialModel 'nn_denoise' not found — load_schemas() didn't pick it up");

    for (const auto& pass : pipeline->passes)
    {
        if (state.families.contains(pass.pipeline_pass)) 
            continue;
        state.families[pass.pipeline_pass] =
            renderer.query_pipeline_family(pass.pipeline_pass, nn_model);
    }
}

void nn_denoiser::prepare_resources(NNDenoiserState& state, RenderGraphContext& ctx)
{
    RenderResource* nn_resource = ctx.render_graph.renderer->find_resource(Name("nn_denoiser"));
    
    for (size_t pass_idx = 0; pass_idx < state.pipeline->passes.size(); ++pass_idx)
    {
        auto& pass = state.pipeline->passes[pass_idx];
        const Name output_tensor_name = pass.output_tensor;
        const NNPassIndicesSSBO ubo_template = state.pass_ubo_templates[pass_idx];
        
        NNPassIndicesSSBO ubo = ubo_template;
        
        
        float out_scale = 1.0f;
        for (const auto& t : state.pipeline->tensors)
        {
            if (t.name == output_tensor_name) { out_scale = t.scale; break; }
        }

        const Extent sc = ctx.backend.get_swapchain_extent();

        
        std::vector<RBImageHandle> act_images;
        act_images.reserve(state.activation_textures.size());
        for (auto rg_handle : state.activation_textures)
            act_images.push_back(ctx.render_graph.get_image(rg_handle));
        
        nn_resource->update_image_array(Name("u_nn_activations"), act_images, {});
    }
    
    

}

void nn_denoiser::add_nn_denoiser_passes(NNDenoiserState& state, RenderGraph& rg, Renderer& renderer)
{
    RenderResource* nn_resource = renderer.find_resource(Name("nn_denoiser"));
    RenderResource* gbuffer_resource = renderer.find_resource(Name("gbuffer"));
    RenderResource* hdr_color_output = renderer.find_resource(Name("hdr_color_output"));
    RenderResource* hdr_color_storage = renderer.find_resource(Name("hdr_color_storage"));
    state.resource = nn_resource;

    for (size_t pass_idx = 0; pass_idx < state.pipeline->passes.size(); ++pass_idx)
    {
        const auto& pass = state.pipeline->passes[pass_idx];
        
        if (pass.is_disabled())
            continue;


        // === Build shader key ===
        auto family_it = state.families.find(pass.pipeline_pass);
        checkf(family_it != state.families.end(),
               "No family for pipeline_pass %s",
               pass.pipeline_pass.to_string().c_str());
        auto& family = family_it->second;

        const ShaderKey shader_key = family->make_shader_key(
            pass.pipeline_pass,
            nullptr,
            pass.permutation_options.enums,
            pass.permutation_options.variants);

        PipelineObject* pso = family->request_pipeline(shader_key);
        checkf(pso != nullptr, "Failed to create PSO for pass %s",
               pass.name.to_string().c_str());

        const NNPassIndicesSSBO ubo_template = state.pass_ubo_templates[pass_idx];
        const NNPassKind pass_kind = pass.kind;
        const Name pass_name = pass.name;
        const std::vector<int32_t> wg_size = pass.workgroup_size;
        const Name output_tensor_name = pass.output_tensor;

        float out_scale = 1.0f;
        for (const auto& t : state.pipeline->tensors)
        {
            if (t.name == output_tensor_name) { out_scale = t.scale; break; }
        }
        
        

        std::set<int> read_slots, write_slots;
        for (auto idx : pass.pass_indices.uActIn)  
            if (idx >= 0) 
                read_slots.insert(idx);
        for (auto idx : pass.pass_indices.uActRes) 
            if (idx >= 0) 
                read_slots.insert(idx);
        for (auto idx : pass.pass_indices.uActOut) 
            if (idx >= 0) 
                write_slots.insert(idx);

        std::vector<RBImageUsage> reads, writes;
        for (int slot : read_slots)
        {
            if (slot < int(state.activation_textures.size()))
                reads.push_back({state.activation_textures[slot], RBImageUsageType::StorageImage});
        }
        for (int slot : write_slots)
        {
            if (slot < int(state.activation_textures.size()))
                writes.push_back({state.activation_textures[slot], RBImageUsageType::StorageImage});
        }
        
        rg.add_pass({
            .name = pass_name,
            .reads = std::move(reads),
            .writes = std::move(writes),
            .execute = [pass_idx, ubo_template, pass_kind, wg_size, out_scale, pso, nn_resource, gbuffer_resource, hdr_color_storage, hdr_color_output, &rg, &state] (RenderGraphContext& ctx)
            {
                const Extent sc = ctx.backend.get_swapchain_extent();
                
                
                Extent size = {
                    (uint32_t)std::max(1, int(sc.width * out_scale)),
                    (uint32_t)std::max(1, int(sc.height * out_scale))
                };

                nn_resource->update_ssbo_element("u_nn_pass_indices", state.pass_ubo_templates[pass_idx], pass_idx);

                ctx.bind_pipeline(pso);

                if (pass_kind == NNPassKind::input)
                {
                    checkf(gbuffer_resource && hdr_color_output,
                           "input pass needs gbuffer + hdr_color_output");
                    ctx.bind(nn_resource, gbuffer_resource, hdr_color_output);
                }
                else if (pass_kind == NNPassKind::head)
                {
                    checkf(hdr_color_output, "head pass needs hdr_color_output");
                    ctx.bind(gbuffer_resource, nn_resource, hdr_color_output, hdr_color_storage);
                }
                else
                {
                    ctx.bind(nn_resource);
                }

                ComputeWorkgroups wg;
                wg.x = (size.width + wg_size[0] - 1) / wg_size[0];
                wg.y = (size.height + wg_size[1] - 1) / wg_size[1];
                wg.z = wg_size[2];
                
                NNPassPC pc;
                pc.out_size = size;
                pc.pass_idx = pass_idx;
                
                ctx.push_constants(pc);
                ctx.compute(wg);
            },
            .type = RenderPassType::compute
        });
    }
    
    
    state.initialized = true;
}