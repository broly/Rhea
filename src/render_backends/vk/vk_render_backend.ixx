export module vk:render_backend;
import <map>;
import <span>;
import <vector>;
import <GLFW/glfw3.h>;
import <memory>;
import <unordered_map>;
import glm;

import :instance;
import :swapchain_control;
import :buffer_mgr;
import :context;
import :internal_types;
import :pipeline;
import :mesh_mgr;
import framework;
import render;
import :immediate_commands;
import :framebuffer_mgr;
import :render_resource;
import :debug;
import :vertex_buffer_mgr;
import :pipeline_manager;
import :tlas_mgr;


struct RenderPassAttachmentInfo
{
    Name img_name;
    RBFormat format;
    RBLoadOp load_op;
    RBStoreOp store_op;
    RBImageUsage usage;
    uint32_t layer = 0;
    uint32_t mip_level = 0;
    bool is_swapchain = false;
    VkImageLayout current_layout = VK_IMAGE_LAYOUT_UNDEFINED;
    bool is_depth_attachment = false;
};


struct RenderPassDesc
{
    Name name;
    std::vector<RenderPassAttachmentInfo> color_attachments;
        

    bool operator==(const RenderPassDesc& other) const
    {
        if (name != other.name)
            return false;
        
        if (color_attachments.size() != other.color_attachments.size())
            return false;
        
        for (auto index = 0; index < color_attachments.size(); index++)
            if (color_attachments[index].format != other.color_attachments[index].format)
                return false;
        
        
        return true;
    };
};

struct RenderPassDescHash
{
    size_t operator()(const RenderPassDesc& d) const
    {
        size_t h = d.color_attachments.size();
        for (auto f : d.color_attachments)
            h ^= std::hash<uint32_t>()(f.format + 0x9e3779b9 + (h<<6) + (h>>2));

        return h;
    }
};

export class VkRenderBackend final : public RenderBackend
{
    friend class VkPipelineObject_Graphics;
public:
    VkRenderBackend();
    
    void init(RBWindowHandle window);
    
public:   /// API Section
    virtual RBCommandList begin_commands(RBFrameHandle frame_handle) override;
    virtual void end_commands(RBCommandList cmd_list) override;
    virtual void begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index) override;
    virtual void end_render_pass(RBCommandList cmd_list) override;
    virtual void bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object) override;
    virtual void draw(RBCommandList cmd_list, uint32_t vertex_count) override;
    virtual void trace_rays(RBCommandList cmd, PipelineObject* pipeline_object, Extent resolution, float depth) override;
    virtual bool acquire_next_image(RBFrameHandle frame_handle) override;
    virtual bool submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list) override;
    virtual PipelineObject* create_graphics_pipeline(const PipelineCreateDesc_Graphics& desc, RBPipelineLayout pipeline_layout) override;
    virtual PipelineObject* create_compute_pipeline(const PipelineCreateDesc_Compute& desc, RBPipelineLayout pipeline_layout) override;
    virtual PipelineObject* create_raytrace_pipeline(const PipelineCreateDesc_RayTrace& desc, RBPipelineLayout pipeline_layout) override;
    virtual RBBufferHandle create_uniform_buffer(size_t buffer_size, ResourceUsage usage_type) override;
    virtual void update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame) override;
    void destroy_render_pass_cache() override;
    virtual RBPipelineLayout create_pipeline_layout(const PipelineLayoutDesc& desc) override;
    virtual Extent get_swapchain_extent() const override;
    virtual void transition_image(RBCommandList cmd, const ImageBarrierParams& params) override;
    virtual void update_sampled_image(
        RBDescriptorSet set,
        uint32_t binding,
        RBImageHandle image,
        ResourceUsage usage,
        std::optional<RBSampler> sampler,
        uint32_t layer_index = 0,
        bool cubemap = false, 
        uint32_t array_index = 0) override;
    virtual void update_storage_image(RBDescriptorSet set, uint32_t binding, RBImageHandle image, uint32_t array_index = 0) override;
    virtual void update_tlas(RBDescriptorSet set, uint32_t binding, RBAccelStruct tlas) override;
    virtual void bind_descriptor_set(RBCommandList cmd, int set_index, RBDescriptorSet rb_descriptors, Name debug_name) override;
    virtual RBFrameHandle get_current_frame() const override;
    virtual void wait_for_frame(RBFrameHandle frame_handle) override;
    virtual void flush_frame_garbage(RBFrameHandle frame) override;
    virtual void reset_frame_fence(RBFrameHandle frame) override;
    virtual void advance_frame() override;
    virtual void copy_image_to_buffer(RBImageHandle img, std::vector<float>& buf, TextureFormat& format, Extent extent) override;
    virtual RBVertexBufferHandle create_vertex_buffer(const VertexBufferDesc& desc) override;
    virtual void* get_vertex_buffer_ptr(RBVertexBufferHandle handle, RBFrameHandle frame) override;
    virtual void bind_vertex_buffer(RBCommandList cmd, RBVertexBufferHandle handle, RBFrameHandle frame) override;
    virtual MeshTableInfo get_mesh_table_info() const override;
    virtual ImageReadback readback_image(RBImageHandle img) const override;
    virtual void bind_mesh(const RBCommandList& cmd, MeshPrimHandle mesh, RBFrameHandle frame) override;
    virtual void push_constants_impl(const RBCommandList& cmd, const void* data, size_t size) override;
    virtual void draw_indexed(const RBCommandList& cmd, uint32_t index_count) override;
    virtual void draw_fullscreen(RBCommandList cmd) override;
    virtual GPUMesh get_or_create_mesh_buffers(MeshPrimHandle handle, RTBuildMode rt_build_mode) override;
    virtual TextureFormat get_swapchain_format() const override;
    virtual RBImageHandle create_image(const RBImageDesc& desc) override;
    virtual void destroy_image(RBImageHandle handle, bool wait_fences) override;
    virtual RBImageView get_image_view(RBImageHandle handle, uint32_t layer_index = 0, uint32_t mip_index = 0) override;
    virtual RBImageView get_cubemap_image_view(RBImageHandle handle) override;
    virtual RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc) override;
    virtual RBImageHandle get_swapchain_image(std::optional<RBFrameHandle> frame_handle) const override;
    virtual RBSampler create_sampler(const ::SamplerDesc& desc) override;
    virtual RBRenderPass get_or_create_render_pass(const FramebufferDesc& fb) override;
    virtual RBImageHandle create_texture_2d(const Texture& data, const TextureCreationInfo& texture_creation_info) override;
    virtual RBImageHandle create_texture_cubemap(const Cubemap& cubemap, const TextureCreationInfo& texture_creation_info) override;
    virtual Extent get_viewport_extent() const override;
    virtual RenderResource* create_resource(const RenderResourceDesc& desc) override;
    virtual RBAccelStruct build_tlas(RBCommandList cmd, const std::vector<MeshPrimHandle>& meshes, const std::vector<Transform>& transforms) override;
    
    virtual void copy_image(RBCommandList cmd, const CopyImageParams& params) override;
    
    virtual void update_viewport(const RBCommandList& cmd, Extent extent, bool use_swapchain_extent = false) override;
    virtual uint32_t get_num_images_in_flight() const override;
    RBDeviceAddress get_buffer_device_address(RBBufferHandle buffer_handle, RBFrameHandle frame) const override;
    // Initialization section
    void create_frame_sync_objects();
    void create_descriptor_pool();
    void create_command_pool();
    void cleanup_swapchain();
    void create_depth_resources();
    
    VkImageSubresourceRange full_subresource_range(RBImageHandle image);

private: // internal section
    
    void destroy_depth_resources();

    vk::DescriptorSetLayoutData get_vk_descriptor_set_layout(RBDescriptorSetLayout rb_handle);

    VkFormat get_image_format(RBImageHandle handle) const;

public:   /// Aggregate section. These objects have same lifetime with render backend. use refs
    vk::Instance instance {};
    vk::Debug debug {};
    vk::ImmediateCommandPool immediate_command_pool{instance};
    vk::ImageManager image_manager {instance, immediate_command_pool, debug}; // holds refs
    vk::SamplerManager sampler_manager {instance};
    vk::SwapchainControl swapchain {instance, image_manager, sampler_manager, debug};  // holds refs
    vk::FramebufferManager framebuffer_manager{instance, image_manager};
    vk::BufferManager buffer_manager {instance, instance.device, instance.physical_device, swapchain, immediate_command_pool}; // holds refs
    vk::MeshManager mesh_manager{instance, immediate_command_pool, buffer_manager};
    vk::VertexBufferManager vertex_buffer_manager{instance.device, instance.physical_device};
    vk::PipelineManager pipeline_manager{instance, swapchain, image_manager, buffer_manager};
    vk::TLASManager tlas_manager{instance, immediate_command_pool, buffer_manager, mesh_manager};
    
    std::vector<std::unique_ptr<VkRenderResource>> resources;
    
    
public:   /// cache and state section:
    
    vk::CommandContext command_context = {};
    vk::PipelineContext pipeline_context = {};
    
    
    
    GLFWwindow* window = nullptr;
    
    std::unordered_map<RenderPassDesc, VkRenderPass, RenderPassDescHash> render_pass_cache;
    VkRenderPass current_render_pass = VK_NULL_HANDLE;
};
