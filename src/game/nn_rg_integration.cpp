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

void nn_denoiser::on_pso_built(NNDenoiserState& state)
{
    state.pso = {};
}

bool nn_denoiser::set_coopmat_gemm(NNDenoiserState& state, bool enable)
{
    if (state.use_coopmat_gemm == enable)
        return false;
    state.use_coopmat_gemm = enable;
    // Drop cached PSOs so the convolution passes recompile with the new COOPMAT
    // permutation on the next add_nn_denoiser_passes / execute.
    state.pso = {};
    LogNNDenoiser.Log<Display>("NN denoiser pointwise back-end -> %s",
        enable ? "Coopmat/GEMM (NN_COOPMAT_ON)" : "scalar (NN_COOPMAT_OFF)");
    return true;
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


    // Mirror of denoiser::NNWeightSlot in the shader (std430: 4x int32).
    struct NNWeightSlotCPU
    {
        int32_t base; 
        int32_t width; 
        int32_t height; 
        int32_t depth;
    };

    const uint32_t num_pw   = pipeline->array_sizes.pw_weights;
    const uint32_t num_dw   = pipeline->array_sizes.dw_weights;
    const uint32_t num_bias = pipeline->array_sizes.biases;

    std::vector<glm::vec4> pw_buffer;
    std::vector<glm::vec4> dw_buffer;
    std::vector<glm::vec4> bias_buffer;

    std::vector<NNWeightSlotCPU> pw_slots(num_pw, {0, 0, 0, 0});
    std::vector<NNWeightSlotCPU> dw_slots(num_dw, {0, 0, 0, 0});
    std::vector<NNWeightSlotCPU> bias_slots(num_bias, {0, 0, 0, 0});

    auto append_weight = [&](const NNWeightDesc& desc,
                             std::vector<glm::vec4>& buf,
                             NNWeightSlotCPU& slot)
    {
        const uint32_t base_elem = (uint32_t)buf.size();
        const uint32_t count     = NNWeightsIndex::slot_float4_count(desc);

        std::vector<float> f32 = state.weights_index.load_weight_ssbo_f32(desc);
        checkf(f32.size() == size_t(count) * 4,
            "Weight '%s': element count %zu != expected %u (w=%u h=%u d=%u)",
            desc.file.c_str(), f32.size(), count * 4, desc.width, desc.height, desc.depth);

        buf.resize(base_elem + count);
        std::memcpy(buf.data() + base_elem, f32.data(), count * sizeof(glm::vec4));

        slot.base   = (int32_t)base_elem;
        slot.width  = (int32_t)desc.width;
        slot.height = (int32_t)desc.height;
        slot.depth  = (int32_t)desc.depth;
    };

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

        if (kind == NNWeightKind::pointwise)
        {
            checkf(slot < num_pw, "pw slot %u out of range (%u)", slot, num_pw);
            append_weight(desc, pw_buffer, pw_slots[slot]);
        }
        else if (kind == NNWeightKind::depthwise)
        {
            checkf(slot < num_dw, "dw slot %u out of range (%u)", slot, num_dw);
            append_weight(desc, dw_buffer, dw_slots[slot]);
        }
        else if (kind == NNWeightKind::bias)
        {
            checkf(slot < num_bias, "bias slot %u out of range (%u)", slot, num_bias);
            append_weight(desc, bias_buffer, bias_slots[slot]);
        }
        else
        {
            checkf(false, "Unknown weight kind: %s", reflect::enum_name(kind).to_string().c_str());
        }

        LogNNDenoiser.Log<Display>("Register weight '%s' (%s slot %u), w=%i,h=%i,d=%i",
            wname.c_str(), reflect::enum_name(kind).to_string().c_str(), slot,
            desc.width, desc.height, desc.depth);
    }

    // Combined layout table: [ pw | dw | bias ] — matches the push-constant bases.
    std::vector<NNWeightSlotCPU> layout;
    layout.reserve(num_pw + num_dw + num_bias);
    layout.insert(layout.end(), pw_slots.begin(),   pw_slots.end());
    layout.insert(layout.end(), dw_slots.begin(),   dw_slots.end());
    layout.insert(layout.end(), bias_slots.begin(), bias_slots.end());

    // pw sub-range starts at 0; dw after pw; bias after dw. These reach the
    // shader through NNPassPC so the weight SSBOs can stay single-bound.
    state.layout_dw_base   = int32_t(num_pw);
    state.layout_bias_base = int32_t(num_pw + num_dw);

    // an SSBO needs at least one element or there's nothing to memcpy into
    if (pw_buffer.empty())   pw_buffer.push_back(glm::vec4(0.0f));
    if (dw_buffer.empty())   dw_buffer.push_back(glm::vec4(0.0f));
    if (bias_buffer.empty()) bias_buffer.push_back(glm::vec4(0.0f));
    if (layout.empty())      layout.push_back({0, 0, 0, 0});

    auto& nn_resource = renderer.find_resource_checked(Name("nn_denoiser"));

    // explicit (byte-size, data) overload so element type doesn't matter for the copy
    nn_resource.update_ssbo(Name("u_nn_pw_weights"),
        pw_buffer.size() * sizeof(glm::vec4), (void*)pw_buffer.data());
    nn_resource.update_ssbo(Name("u_nn_dw_weights"),
        dw_buffer.size() * sizeof(glm::vec4), (void*)dw_buffer.data());
    nn_resource.update_ssbo(Name("u_nn_biases"),
        bias_buffer.size() * sizeof(glm::vec4), (void*)bias_buffer.data());
    nn_resource.update_ssbo(Name("u_nn_weight_layout"),
        layout.size() * sizeof(NNWeightSlotCPU), (void*)layout.data());

    nn_resource.update_ssbo(Name("u_nn_pass_indices"), state.pass_ubo_templates);
    
    
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

    std::vector<RBImageHandle> act_images;
    act_images.reserve(state.activation_textures.size());
    for (auto rg_handle : state.activation_textures)
        act_images.push_back(ctx.render_graph.get_image(rg_handle));

    nn_resource->update_image_array(Name("u_nn_activations"), act_images, {});
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

        auto family_it = state.families.find(pass.pipeline_pass);
        checkf(family_it != state.families.end(),
               "No family for pipeline_pass %s",
               pass.pipeline_pass.to_string().c_str());

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
            .execute = [pass_idx, ubo_template, pass_kind, wg_size, out_scale, nn_resource, gbuffer_resource, hdr_color_storage, hdr_color_output, &rg, &state] (RenderGraphContext& ctx)
            {
                const Extent sc = ctx.backend.get_swapchain_extent();
                
                
                Extent size = {
                    (uint32_t)std::max(1, int(sc.width * out_scale)),
                    (uint32_t)std::max(1, int(sc.height * out_scale))
                };

                nn_resource->update_ssbo_element("u_nn_pass_indices", state.pass_ubo_templates[pass_idx], pass_idx);
                
                if (!state.pso.contains(pass_idx))
                {
                    auto& pass = state.pipeline->passes[pass_idx];
                    auto family_it = state.families.find(pass.pipeline_pass);
                    auto family = family_it->second;

                    // Select the pointwise back-end at compile time. Only the passes whose 1x1 conv was ported to the 
                    // GEMM path accept this define (enc/dec/bottleneck/up). NN_USE_COOPMAT is a numeric VARIANT in the 
                    // nn_denoise material (values [0,1]), so it is injected through permutation_constants — the same
                    // channel as IN_CH/OUT_CH — NOT through enums. (enums only index pre-declared enum tokens like ACT;
                    // an unknown enum key is silently dropped, which is why the earlier COOPMAT enum approach never 
                    // reached the shader.)
                    auto variants = pass.permutation_options.variants;
                    const bool coopmat_capable_pass =
                        pass.kind == NNPassKind::enc        ||
                        pass.kind == NNPassKind::dec        ||
                        pass.kind == NNPassKind::bottleneck ||
                        pass.kind == NNPassKind::up;

                    // Shared-mem budget guard. The coopmat path stages
                    // Wsh[OUT*K]*2 + Xsh[K*64]*2 + Ysh[OUT*64]*4 bytes. Consumer NVIDIA caps compute shared mem 
                    // at 48 KB, so the big passes (dec2_a at IN=256/OUT=128 wants 128 KB) don't fit and fall
                    // back to scalar. Dropping Ysh would shrink this a lot, but that's a TODO.
                    auto coopmat_shared_bytes = [](uint32_t in_ch, uint32_t out_ch) -> uint32_t {
                        const uint32_t K = ((in_ch + 3) / 4) * 4;
                        const uint32_t W = out_ch * K * 2;   // fp16
                        const uint32_t X = K * 64 * 2;       // fp16
                        const uint32_t Y = out_ch * 64 * 4;  // fp32
                        return W + X + Y;
                    };
                    const uint32_t kSharedCapBytes = 48u * 1024u;   // consumer NVIDIA hard limit

                    bool use_coopmat_here = state.use_coopmat_gemm && coopmat_capable_pass;
                    if (use_coopmat_here)
                    {
                        uint32_t in_ch =
                            variants.count(Name("IN_CH"))  ? variants[Name("IN_CH")]  : 0u;
                        uint32_t out_ch =
                            variants.count(Name("OUT_CH")) ? variants[Name("OUT_CH")] : 0u;

                        // Coopmat fragment shape is 16x16x16, so the GEMM tiling requires K_PAD and OUT_CH to be 
                        // positive multiples of 16. pass_enc0_a (IN=11 -> K_PAD=12) is the only conv pass that
                        // fails this; force it to scalar. (Also guards OUT_CH<16.)
                        const uint32_t k_pad = ((in_ch + 3u) / 4u) * 4u;
                        const bool tiling_ok =
                            (k_pad >= 16u) && (k_pad % 16u == 0u) &&
                            (out_ch >= 16u) && (out_ch % 16u == 0u);
                        if (!tiling_ok)
                        {
                            use_coopmat_here = false;
                            LogNNDenoiser.Log<Display>(
                                "NN pass '%s' K_PAD=%u OUT=%u not 16-aligned -> scalar",
                                pass.name.to_string().c_str(), k_pad, out_ch);
                        }

                        // Shared-memory budget guard (fast path): dec2_a needs 128 KB.
                        if (use_coopmat_here &&
                            coopmat_shared_bytes(in_ch, out_ch) > kSharedCapBytes)
                        {
                            use_coopmat_here = false;
                            LogNNDenoiser.Log<Display>(
                                "NN pass '%s' exceeds coopmat shared budget (%u B) -> scalar",
                                pass.name.to_string().c_str(),
                                coopmat_shared_bytes(in_ch, out_ch));
                        }
                    }

                    if (coopmat_capable_pass)
                    {
                        variants[Name("NN_USE_COOPMAT")] = use_coopmat_here ? 1u : 0u;
                    }

                    const ShaderKey shader_key = family->make_shader_key(
                        pass.pipeline_pass,
                        nullptr,
                        pass.permutation_options.enums,
                        variants);
                    
                    auto pipeline = family->request_pipeline(shader_key);
                    
                    checkf(pipeline != nullptr, "Failed to create PSO for pass %s",
                           pass.name.to_string().c_str());
                    state.pso[pass_idx] = pipeline;
                }

                ctx.bind_pipeline(state.pso[pass_idx]);

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
                pc.nn_layout_dw_base   = state.layout_dw_base;
                pc.nn_layout_bias_base = state.layout_bias_base;
                
                ctx.push_constants(pc);
                ctx.compute(wg);
            },
            .type = RenderPassType::compute
        });
    }
    
    
    state.initialized = true;
}
