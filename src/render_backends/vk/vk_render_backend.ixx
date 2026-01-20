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




export class VkRenderBackend final : public RenderBackend
{
    friend class VkPipelineObject;
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
    virtual bool acquire_next_image(RBFrameHandle frame_handle) override;
    virtual void submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list) override;
    virtual PipelineObject* create_pipeline(const GraphicsPipelineDesc& desc) override;
    virtual RBBufferHandle create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type) override;
    virtual void update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame) override;
    virtual RBSwapchainExtent get_swapchain_extent() const override;
    virtual void transition_image(
        RBCommandList cmd,
        RBImageHandle image,
        RBImageUsage before,
        RBImageUsage after) override;
    virtual void update_sampled_image(
        RBDescriptorSet set,
        uint32_t binding,
        RBImageHandle image,
        ResourceUsageType usage,
        std::optional<RBSampler> sampler) override;
    virtual RBDescriptorSetLayout create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout) override;
    virtual void bind_descriptor_set(RBCommandList cmd, int set_index, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle) override;
    virtual RBFrameHandle get_current_frame() const override;
    virtual void wait_for_frame(RBFrameHandle frame_handle) override;
    virtual void reset_frame_fence(RBFrameHandle frame) override;
    virtual void advance_frame() override;
    virtual void bind_mesh(const RBCommandList& cmd, MeshPrimHandle mesh, RBFrameHandle frame) override;
    virtual void push_constants(const RBCommandList& cmd, glm::mat4 matrix, PipelineObject* pipeline_object) override;
    virtual void draw_indexed(const RBCommandList& cmd, uint32_t index_count) override;
    virtual void get_or_create_mesh_buffers(MeshPrimHandle handle) override;
    virtual TextureFormat get_swapchain_format() const override;
    virtual RBImageHandle create_image(const RBImageDesc& desc) override;
    virtual RBImageView get_image_view(RBImageHandle handle) override;
    virtual RBFramebufferId get_or_create_framebuffer(const FramebufferDesc& desc) override;
    virtual RBImageView get_swapchain_image_view(RBFrameHandle frame) override;
    virtual RBImageHandle get_swapchain_image(std::optional<RBFrameHandle> frame_handle) const override;
    virtual RBSampler create_sampler(const ::SamplerDesc& desc) override;
    virtual RBRenderPass get_or_create_render_pass(const FramebufferDesc& fb) override;
    virtual void draw_fullscreen(RBCommandList cmd) override;
    virtual RBImageHandle create_texture_2d(const Texture& data, std::optional<TextureFormat> format_override = std::nullopt) override;
    std::pair<uint32_t, uint32_t> get_viewport_extent() const override;
    virtual RenderResource* create_resource(const RenderResourceDesc& desc) override;
    
    
// Initialization section
    void create_frame_sync_objects();
    void create_descriptor_pool();
    void create_command_pool();
    void cleanup_swapchain();
    void create_depth_resources();
    
    VkImageSubresourceRange full_subresource_range(RBImageHandle image);

private: // internal section
    
    void destroy_depth_resources();
    
    void update_viewport_extent(const RBCommandList& cmd);
    
    vk::DescriptorSetLayoutData get_vk_descriptor_set_layout(RBDescriptorSetLayout rb_handle);
    
    RenderPassDesc make_render_pass_desc(const FramebufferDesc& fb) const;
    
    VkFormat get_image_format(RBImageHandle handle) const;
    
    RBPipelineHandle get_or_create_pipeline(
        RBPipelineHandle handle,
        VkRenderPass render_pass);

public:   /// Aggregate section. These objects have same lifetime with render backend. use refs
    vk::Instance instance {};
    vk::ImmediateCommandPool immediate_command_pool{instance};
    vk::ImageManager image_manager {instance, immediate_command_pool}; // holds refs
    vk::SamplerManager sampler_manager {instance};
    vk::SwapchainControl swapchain {instance, image_manager, sampler_manager};  // holds refs
    vk::FramebufferManager framebuffer_manager{instance, image_manager};
    vk::BufferManager resource_manager {instance.device, instance.physical_device, swapchain}; // holds refs
    vk::MeshManager mesh_manager{instance};
    
    std::vector<std::unique_ptr<VkRenderResource>> resources;
    
    
public:   /// cache and state section:
    
    vk::CommandContext command_context = {};
    vk::PipelineContext pipeline_context = {};
    
    // currently working pipelines (moving from pending_pipelines)
    std::map<RBPipelineHandle, std::unique_ptr<VkPipelineObject>> pipelines;
    
    // pipelines objects that only have layouts, but not real pipelines yet
    std::vector<std::unique_ptr<VkPipelineObject>> pending_pipelines;
    
    
    GLFWwindow* window = nullptr;
    
    std::unordered_map<RenderPassDesc, VkRenderPass, RenderPassDescHash> render_pass_cache;
    VkRenderPass current_render_pass = VK_NULL_HANDLE;
};
