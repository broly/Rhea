export module render:render_backend;

import std.compat;

import glm;
import :pipeline_desc;
import framework;
import :handle_types;
import :pipeline_object;
import :rg_types;
import :render_resource;
import :sampler_desc;
import :vertex_buffer;
import :gpu_types;
import :skinning;

import assets;
import rhmath;

// Pending readback handle: opaque readback queue index
export struct PendingReadbackHandle { uint64_t id = 0; };

export struct RenderGraphPass;
export class RenderBackend;

export template<typename T>
concept allowed_for_push_constant = 
    std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && sizeof(T) <= 256;

template<typename T>
concept RenderBackendType = 
    std::is_base_of_v<RenderBackend, T> && 
        requires(T t, RBWindowHandle window_handle) { t.init(window_handle); };

export struct TextureCreationInfo
{
    std::optional<TextureFormat> format_override = std::nullopt;
    bool generate_mips = true;
    bool imported = false;
    RBImageLayout initial_layout = RBImageLayout::undefined;
    RBImageLayout current_layout = RBImageLayout::undefined;
    uint32_t array_layers = 1;
};

// Texels of a 2D texture (mip 0)
export struct TextureRegion
{
    uint32_t x = 0;
    uint32_t y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

export struct CopyImageParams
{
    RBImageHandle source;
    RBImageHandle dest;
    std::optional<RBImageLayout> src_layout = std::nullopt;
    std::optional<RBImageLayout> dst_layout = std::nullopt;
    uint32_t src_layer = 0;
    uint32_t dst_layer = 0;
    uint32_t src_mip = 0;
    uint32_t dst_mip = 0;
    uint32_t num_mips = 1;
    uint32_t num_layers = 1;
};

export struct ImageReadback
{
    Extent extent;
    uint32_t layers = 1;
    uint32_t mips   = 1;
    TextureFormat format;
    uint32_t channels = 0;
    static Extent mip_extent(const Extent& base, uint32_t mip)
    {
        return {
            std::max(1u, base.width  >> mip),
            std::max(1u, base.height >> mip)
        };
    }

    // data[layer][mip][pixel * channels + c]
    std::vector<std::vector<std::vector<float>>> data;
};

export struct ComputeWorkgroups
{
    uint32_t x = 32;
    uint32_t y = 32;
    uint32_t z = 1;
    
    static ComputeWorkgroups from_extent(const Extent& extent, uint32_t z = 1)
    {
        ComputeWorkgroups result;
        result.x = extent.width;
        result.y = extent.height;
        result.z = z;
        return result;
    }
};

export struct TLASInfo
{
    MeshPrimHandle mesh;
    Transform transform;
    uint32_t primitive_id;
    
    // skinned instances own their BLAS (built over the skinned vertices)
    std::optional<uint32_t> skinned_instance = std::nullopt;
};

export class RenderBackend 
{
public:
    virtual ~RenderBackend() = default;
    virtual RBFrameHandle get_current_frame() const = 0;
    virtual void reset_current_frame() = 0;
    virtual void wait_for_frame(RBFrameHandle frame) = 0;
    virtual void flush_frame_garbage(RBFrameHandle frame) = 0;
    virtual void reset_frame_fence(RBFrameHandle frame) = 0;
    virtual void advance_frame() = 0;

    // --- resource release ------------------------------------------------------
    // Runs `release` once the GPU is done with every frame submitted so far, and the one recorded next (resources
    // they may still read: freed between the scene extraction and the frame that no longer draws them)
    virtual void defer_release(std::function<void()> release) = 0;
    // Frees the buffers (and BLAS) of get_or_create_mesh_buffers, deferred; its mesh table index is reused
    virtual void release_mesh_buffers(MeshPrimHandle mesh) = 0;
    // Frees a create_skinned_mesh copy, deferred; its instance id and mesh table index are reused
    virtual void release_skinned_mesh(uint32_t instance_id) = 0;

    // --- Debug UI overlay (Dear ImGui draw data of the current frame) ----------
    // The ImGui context and the platform backend are owned by the ui module; the render
    // backend only uploads and draws ImGui::GetDrawData() over the swapchain image.
    virtual void init_ui_overlay() {}
    virtual void shutdown_ui_overlay() {}
    virtual void ui_overlay_new_frame() {}
    // Called at the end of the main render graph, before the swapchain image goes to Present
    virtual void render_ui_overlay(RBCommandList cmd, RBFrameHandle frame) {}

    // --- GPU timestamp queries (used by the gpu_profile pass profiler) -------
    virtual double get_timestamp_period_ns() const = 0;
    virtual RBQueryPool create_timestamp_pool(uint32_t query_count) = 0;
    virtual void destroy_timestamp_pool(RBQueryPool pool) = 0;
    virtual void cmd_reset_timestamp_pool(RBCommandList cmd, RBQueryPool pool, uint32_t query_count) = 0;
    virtual void cmd_write_timestamp(RBCommandList cmd, RBQueryPool pool, uint32_t query_index, bool bottom_of_pipe) = 0;
    virtual bool read_timestamps(RBQueryPool pool, uint32_t first_query, uint32_t query_count, uint64_t* out_values) = 0;

    // Waits until the GPU has finished all submitted work (debug readbacks, teardown)
    virtual void wait_idle() = 0;

    // Debug: ALL_COMMANDS -> ALL_COMMANDS memory barrier (outside a render pass). Waits for all work
    // submitted earlier to the queue, including the previous frames in flight
    virtual void debug_full_barrier(RBCommandList cmd) = 0;

    // Buffer memory barriers of GPU generated draws (occlusion culling), outside render passes:
    //   compute_to_draw     compute shader writes -> indirect arguments, vertex / fragment / compute shader reads
    //   compute_to_compute  compute shader writes -> compute shader reads and writes (the next dispatch)
    //   draw_to_compute     indirect / shader reads of earlier draws -> compute shader writes (reuse of the buffers)
    enum class BufferBarrier : uint8_t { compute_to_draw, compute_to_compute, draw_to_compute };
    virtual void cmd_buffer_barrier(RBCommandList cmd, BufferBarrier barrier) = 0;

    // --- occlusion queries (samples passing the per-fragment tests; diagnostics) ---
    virtual RBQueryPool create_occlusion_pool(uint32_t query_count) = 0;
    virtual void cmd_begin_query(RBCommandList cmd, RBQueryPool pool, uint32_t query_index) = 0;
    virtual void cmd_end_query(RBCommandList cmd, RBQueryPool pool, uint32_t query_index) = 0;
    // results without waiting; false if not available (never written since the last reset)
    virtual bool read_query_results(RBQueryPool pool, uint32_t first_query, uint32_t query_count, uint64_t* out_values) = 0;
    // host reset (the queries must not be in use by pending command buffers)
    virtual void reset_queries(RBQueryPool pool, uint32_t first_query, uint32_t query_count) = 0;

    virtual void copy_image_to_buffer(RBImageHandle img, std::vector<float>& buf, TextureFormat& format, Extent extent) = 0;
    virtual ImageReadback readback_image(RBImageHandle img) const = 0;
    

    // Add image readback to queue
    virtual PendingReadbackHandle enqueue_image_readback(RBCommandList cmd, RBImageHandle img) = 0;

    // Read data from readback
    virtual ImageReadback finalize_readback(PendingReadbackHandle handle) = 0;
    
    virtual void destroy_pipeline(PipelineObject* pipeline) = 0;
    
    virtual RBVertexBufferHandle create_vertex_buffer(const VertexBufferDesc& desc) = 0;
    virtual void* get_vertex_buffer_ptr(RBVertexBufferHandle handle, RBFrameHandle frame) = 0;
    virtual void bind_vertex_buffer(RBCommandList cmd, RBVertexBufferHandle handle, RBFrameHandle frame) = 0;
    
    virtual MeshTableInfo get_mesh_table_info() const = 0;
    
    
    virtual RBBufferHandle create_uniform_buffer(size_t buffer_size, ResourceUsage usage_type) = 0;
    virtual void update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame) = 0;
    template<typename T>
    void update_uniform_buffer(RBBufferHandle buffer_handle, const T& data, RBFrameHandle frame)
    {
        update_uniform_buffer_impl(buffer_handle, sizeof(T), (void*)&data, frame);
    }
    
    virtual void destroy_render_pass_cache() = 0;
    
    virtual RBPipelineLayout create_pipeline_layout(const PipelineLayoutDesc& desc) = 0;

    virtual Extent get_swapchain_extent() const = 0;

    virtual RBImageHandle create_texture_2d(const Texture& data, const TextureCreationInfo& texture_creation_info) = 0;
    // Volume texture (sampler3D) from an RGBA8 texture holding `depth` slices stacked along its height:
    // slice z is rows [z * slice_height, (z + 1) * slice_height). Mips are box filtered on the GPU.
    virtual RBImageHandle create_texture_3d(const Texture& data, uint32_t depth, bool generate_mips) = 0;
    virtual RBImageHandle create_texture_cubemap(const Cubemap& data, const TextureCreationInfo& texture_creation_info) = 0;

    // Editing tools (terrain painting and sculpting). Both wait until the GPU is idle first: the frames in
    // flight read the resource, and it is one buffer / image for all of them.
    // Replaces `region` of mip 0 of a create_texture_2d RGBA8 texture with the same texels of `data`
    // (same extent) and rebuilds the mips.
    virtual void update_texture_2d(RBImageHandle image, const Texture& data, const TextureRegion& region) = 0;
    // Overwrites the vertices of an uploaded mesh primitive (get_or_create_mesh_buffers), same count;
    // no-op for primitives not uploaded yet
    virtual void update_mesh_vertices(MeshPrimHandle mesh, std::span<const Vertex> vertices) = 0;

    virtual void update_viewport(const RBCommandList& cmd, Extent extent, bool use_swapchain_extent = false) = 0;
    // viewport and scissor of a region of the attachments (shadow atlas tiles)
    virtual void set_viewport(const RBCommandList& cmd, int32_t x, int32_t y, uint32_t width, uint32_t height) = 0;
    // clears a region of the depth attachment of the current render pass (shadow atlas tiles)
    virtual void clear_depth(const RBCommandList& cmd, int32_t x, int32_t y, uint32_t width, uint32_t height, float depth = 1.0f) = 0;

    virtual uint32_t get_num_images_in_flight() const = 0;
    
    virtual RBDeviceAddress get_buffer_device_address(RBBufferHandle buffer_handle, RBFrameHandle frame) const = 0;
    
    template<RenderBackendType T>
    static std::shared_ptr<RenderBackend> create(RBWindowHandle window_handle)
    {
        auto result = std::make_shared<T>();
        result->init(window_handle);
        return result;
    }
    
    // ---- commands section ----
    virtual RBCommandList begin_commands(RBFrameHandle frame_handle) = 0;
    virtual void end_commands(RBCommandList cmd_list) = 0;
    
    // ---- pass section ----
    virtual void begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index) = 0;
    virtual void end_render_pass(RBCommandList cmd_list) = 0;
    
    virtual void bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object) = 0;
    
    // first_instance: gl_InstanceIndex of the (single) instance, mesh draws pass their draw record index
    virtual void draw(RBCommandList cmd_list, uint32_t vertex_count, uint32_t first_vertex = 0, uint32_t first_instance = 0) = 0;

    // draw_count VkDrawIndirectCommands (`stride` bytes apart) at byte `offset` of the storage buffer
    // `buffer` (copy of `frame`)
    virtual void draw_indirect(RBCommandList cmd_list, RBBufferHandle buffer, RBFrameHandle frame, uint64_t offset,
        uint32_t draw_count, uint32_t stride = 16) = 0;
    // the same with VkDrawIndexedIndirectCommands, indices of the bound mesh index block
    virtual void draw_indexed_indirect(RBCommandList cmd_list, RBBufferHandle buffer, RBFrameHandle frame, uint64_t offset,
        uint32_t draw_count, uint32_t stride = 20) = 0;

    // shared indices of the meshes (MeshIndexRange), for indexed draws
    virtual MeshIndexRange get_mesh_index_range(uint32_t mesh_index) const = 0;
    virtual void bind_mesh_index_block(RBCommandList cmd_list, uint32_t block) = 0;
    
    virtual void trace_rays(RBCommandList cmd, PipelineObject* pipeline_object, Extent resolution, float depth) = 0;
    
    virtual bool acquire_next_image(RBFrameHandle frame_handle) = 0;
    virtual bool submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list) = 0;
    
    virtual PipelineObject* create_graphics_pipeline(const PipelineCreateDesc_Graphics& desc, RBPipelineLayout pipeline_layout) = 0;
    virtual PipelineObject* create_compute_pipeline(const PipelineCreateDesc_Compute& desc, RBPipelineLayout pipeline_layout) = 0;
    virtual PipelineObject* create_raytrace_pipeline(const PipelineCreateDesc_RayTrace& desc, RBPipelineLayout pipeline_layout) = 0;


    virtual void bind_descriptor_set(RBCommandList cmd, int set_index, RBDescriptorSet rb_descriptors,Name debug_name) = 0;
    
    void push_constants(
        const RBCommandList& cmd, 
        const allowed_for_push_constant auto& value)
    {
        push_constants_impl(cmd, &value, sizeof(value));
    }
    
    virtual void compute(RBCommandList cmd, const ComputeWorkgroups& workgroups = {}) = 0;
    // a VkDispatchIndirectCommand (3 x uint32 workgroup counts) at byte `offset` of the storage buffer `buffer`
    // (copy of `frame`), written on the GPU: a compute_to_draw buffer barrier must separate it from the writer
    virtual void compute_indirect(RBCommandList cmd, RBBufferHandle buffer, RBFrameHandle frame, uint64_t offset = 0) = 0;
    
    virtual void bind_mesh(const RBCommandList& cmd, MeshPrimHandle mesh, RBFrameHandle frame) = 0;
    virtual void push_constants_impl(const RBCommandList& cmd, const void* data, size_t size) = 0;
    virtual void draw_indexed(const RBCommandList& cmd, uint32_t index_count) = 0;
    virtual void draw_fullscreen(RBCommandList cmd) = 0;
    virtual GPUMesh get_or_create_mesh_buffers(MeshPrimHandle handle, RTBuildMode rt_build_mode) = 0;
    // The CPU part of get_or_create_mesh_buffers (vertex cache optimization) for primitives without buffers yet,
    // on all cores; the next get_or_create_mesh_buffers calls use it. Only a speed-up: the ones left out are
    // optimized when their buffers are created, the ones not created after all are dropped at the next call
    virtual void prepare_mesh_buffers(std::span<const MeshPrimHandle> handles) = 0;

    // ---- skinning section ----
    
    // Creates a per-instance skinned copy of `source` (output vertices + optional BLAS + mesh table entry).
    // Output vertices are initialized with the bind pose.
    virtual SkinnedMeshGPU create_skinned_mesh(MeshPrimHandle source, const std::vector<SkinVertex>& skin, uint32_t bone_count, const PrimitiveMorphs& morphs, uint32_t morph_count, RTBuildMode rt_build_mode) = 0;
    
    // Writes bone matrices into the `frame` slot, returns device address of the slot
    virtual RBDeviceAddress upload_bone_matrices(uint32_t instance_id, RBFrameHandle frame, const std::vector<glm::mat4>& matrices, const std::vector<float>& morph_weights) = 0;
    
    // Before skinning: previous readers of skinned vertices -> compute write
    virtual void cmd_skinning_begin_barrier(RBCommandList cmd) = 0;
    
    // After skinning: compute write -> vertex/fragment/RT reads and BLAS build input
    virtual void cmd_skinning_end_barrier(RBCommandList cmd) = 0;
    
    // Refits skinned BLASes after their vertices were updated (followed by an AS barrier)
    virtual void cmd_refit_skinned_blas(RBCommandList cmd, const std::vector<uint32_t>& instance_ids) = 0;
    virtual TextureFormat get_swapchain_format() const = 0;
    virtual RBImageHandle create_image(const RBImageDesc& desc) = 0;
    virtual void destroy_image(RBImageHandle handle, bool wait_fences) = 0;
    virtual RBImageView get_image_view(RBImageHandle handle, uint32_t layer_index = 0, uint32_t mip_index = 0) = 0;
    virtual RBImageView get_array_image_view(RBImageHandle handle, uint32_t layer_index = 0, uint32_t num_layers = 1) = 0;
    virtual RBImageView get_cubemap_image_view(RBImageHandle handle) = 0;
    virtual RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc) = 0;
    virtual RBImageHandle get_swapchain_image(std::optional<RBFrameHandle> frame_handle = std::nullopt) const = 0;
    virtual RBRenderPass get_or_create_render_pass(const FramebufferDesc& fb) = 0;
    virtual RBSampler create_sampler(const ::SamplerDesc& desc) = 0;
    virtual RBAccelStruct build_tlas(RBCommandList cmd, const std::vector<TLASInfo>& objects) = 0;
    
    virtual void copy_image(RBCommandList cmd, const CopyImageParams& params) = 0;
    
    // Fills mips 1..N-1 of every layer from mip 0 (linear blits). Mip 0 of all layers must be in the same
    // state; afterwards every subresource is TransferSrc (transition the whole image to where it is read next).
    virtual void generate_mips(RBCommandList cmd, RBImageHandle image) = 0;
    
    virtual void transition_image(
        RBCommandList cmd, const ImageBarrierParams& params) = 0;
    
    virtual void update_sampled_image(
        RBDescriptorSet set,
        uint32_t binding,
        RBImageHandle image,
        ResourceUsage usage,
        std::optional<RBSampler> sampler,
        uint32_t layer_index = 0,
        uint32_t layers_num = 1,
        bool cubemap = false,
        uint32_t array_index = 0, 
        bool as_array_2d = false) = 0;
    
    
    virtual void update_storage_image(
        RBDescriptorSet set,
        uint32_t binding,
        RBImageHandle image, 
        uint32_t array_index = 0, 
        bool as_array_2d = false) = 0;
    
    virtual void update_tlas(
        RBDescriptorSet set,
        uint32_t binding,
        RBAccelStruct tlas) = 0;
    
    virtual Extent get_viewport_extent() const = 0;
    
    virtual RenderResource* create_resource(const RenderResourceDesc& desc) = 0;
    
    
};
