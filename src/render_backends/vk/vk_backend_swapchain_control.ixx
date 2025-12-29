export module vk:swapchain_control;
import render;
import :context;
import :instance;
import :texture_mgr;
import <vulkan/vulkan_core.h>;

class VkRenderBackend;

namespace vk
{
    struct FrameContext {
        VkCommandBuffer cmd = VK_NULL_HANDLE;

        VkSemaphore image_available = VK_NULL_HANDLE;
        VkFence in_flight = VK_NULL_HANDLE;
    };
    
    class SwapchainControl
    {
    public:
        SwapchainControl(vk::Instance& in_instance, vk::TextureManager& in_texture_manager)
            : swapchain(VK_NULL_HANDLE)
            , texture_manager(in_texture_manager)
            , extent()
            , surface_format()
            , instance(in_instance)
        {
        }

        void init();
        RBSwapchainExtent get_extent() const;
        
        void CRUTCH_transition_image(const RBCommandList& cmd, RBImageHandle image, RGTextureFormat format, VkImageLayout old_layout, VkImageLayout new_layout);
        RBImageHandle create_image(RBImageDesc desc);
        RBImageView get_image_view(RBImageHandle handle) const;
        RBImageView get_image_view() const;
        RBImageView resolve_image_view(const RGTexture& tex, uint32_t frame);
        RBImageHandle get_image() const;
        void update_depth_descriptior(const RBDescriptorSet& rb_handle, RBImageHandle value);
        VkFormat get_image_format(RBImageHandle handle) const;
        bool acquire_next_image(RBFrameHandle frame_handle);
        void submit_frame(RBFrameHandle frame_handle, const RBCommandList& cmd_list);
        void advance_frame();
        void cleanup();
        void create_sync_objects();
        void wait_for_frame(RBFrameHandle frame_handle);
        void reset_frame_fence(uint32_t frame);

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkExtent2D extent;

        std::vector<VkImageView> image_views;
    
        VkImage depth_image = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory = VK_NULL_HANDLE;
        VkImageView depth_image_view = VK_NULL_HANDLE;
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
        
        
        VkSurfaceFormatKHR surface_format;
        
        vk::Instance& instance;
        vk::TextureManager& texture_manager;
        std::vector<RBImageHandle> swapchain_image_handles;
        std::vector<vk::ImageResource> image_resources;
        
        
        uint32_t swapchain_image_index = 0;
        
        
        
        std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames;
        std::vector<VkSemaphore> render_finished_per_image;
        
        uint32_t current_frame = 0;
        
        bool framebuffer_resized = false;
    };
}
