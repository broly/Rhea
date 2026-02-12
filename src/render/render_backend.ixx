export module render:render_backend;
import <memory>;
import <span>;

import glm;
import :graphics_pipeline_desc;
import framework;
import :handle_types;
import :pipeline_object;
import :rg_types;
import :render_resource;
import :sampler_desc;

import assets;


struct RenderGraphPass;
export class RenderBackend;

export template<typename T>
concept allowed_for_push_constant = 
    std::is_trivially_copyable_v<T> && !std::is_pointer_v<T> && sizeof(T) <= 128;

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
};

export struct ImageReadback
{
    Extent extent;
    uint32_t layers = 1;
    uint32_t mips   = 1;
    TextureFormat format;
    
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

export class RenderBackend 
{
public:
    virtual ~RenderBackend() = default;
    virtual RBFrameHandle get_current_frame() const = 0;
    virtual void wait_for_frame(RBFrameHandle frame) = 0;
    virtual void reset_frame_fence(RBFrameHandle frame) = 0;
    virtual void advance_frame() = 0;
    virtual void copy_image_to_buffer(RBImageHandle img, std::vector<float>& buf, TextureFormat& format, Extent extent) = 0;
    virtual ImageReadback readback_image(RBImageHandle img) const = 0;
    
    
    virtual RBBufferHandle create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type) = 0;
    virtual void update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame) = 0;
    
    template<typename T>
    void update_uniform_buffer(RBBufferHandle buffer_handle, const T& data, RBFrameHandle frame)
    {
        update_uniform_buffer_impl(buffer_handle, sizeof(T), (void*)&data, frame);
    }

    virtual Extent get_swapchain_extent() const = 0;

    virtual RBImageHandle create_texture_2d(const Texture& data, const TextureCreationInfo& texture_creation_info) = 0;
    virtual RBImageHandle create_texture_cubemap(const Cubemap& data, const TextureCreationInfo& texture_creation_info) = 0;
    virtual void update_viewport(const RBCommandList& cmd, Extent extent, bool use_swapchain_extent = false) = 0;

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
    
    virtual void draw(RBCommandList cmd_list, uint32_t vertex_count) = 0;
    
    virtual RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout) = 0;

    virtual bool acquire_next_image(RBFrameHandle frame_handle) = 0;
    virtual void submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list) = 0;
    
    virtual PipelineObject* create_pipeline(const GraphicsPipelineDesc& desc) = 0;


    virtual void bind_descriptor_set(RBCommandList cmd, int set_index, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle) = 0;
    
    void push_constants(
        const RBCommandList& cmd, 
        const allowed_for_push_constant auto& value, 
        PipelineObject* pipeline_object)
    {
        push_constants_impl(cmd, &value, sizeof(value), pipeline_object);
    }
    
    virtual void bind_mesh(const RBCommandList& cmd, MeshPrimHandle mesh, RBFrameHandle frame) = 0;
    virtual void push_constants_impl(const RBCommandList& cmd, const void* data, size_t size, PipelineObject* pipeline_object) = 0;
    virtual void draw_indexed(const RBCommandList& cmd, uint32_t index_countaram) = 0;
    virtual void draw_fullscreen(RBCommandList cmd) = 0;
    virtual void get_or_create_mesh_buffers(MeshPrimHandle handle) = 0;
    virtual TextureFormat get_swapchain_format() const = 0;
    virtual RBImageHandle create_image(const RBImageDesc& desc) = 0;
    virtual void destroy_image(RBImageHandle handle, bool wait_fences) = 0;
    virtual RBImageView get_image_view(RBImageHandle handle, uint32_t array_index = 0, uint32_t mip_index = 0) = 0;
    virtual RBImageView get_cubemap_image_view(RBImageHandle handle) = 0;
    virtual RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc) = 0;
    virtual RBImageHandle get_swapchain_image(std::optional<RBFrameHandle> frame_handle = std::nullopt) const = 0;
    virtual RBRenderPass get_or_create_render_pass(const FramebufferDesc& fb) = 0;
    virtual RBSampler create_sampler(const ::SamplerDesc& desc) = 0;
    
    virtual void transition_image(
        RBCommandList cmd,
        RBImageHandle image,
        RBImageLayout before,
        RBImageLayout after) = 0;
    
    virtual void update_sampled_image(
        RBDescriptorSet set,
        uint32_t binding,
        RBImageHandle image,
        ResourceUsageType usage,
        std::optional<RBSampler> sampler, 
        uint32_t array_index = 0,
        bool cubemap = false) = 0;
    
    virtual Extent get_viewport_extent() const = 0;
    
    virtual RenderResource* create_resource(const RenderResourceDesc& desc) = 0;
    
    
};
