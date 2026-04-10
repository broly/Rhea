export module vk:swapchain_control;
import render;
import :context;
import :instance;
import :image_mgr;
import assets;
import :sampler_mgr;
import :debug_object_tracker;
import :debug;
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
        SwapchainControl(vk::Instance& in_instance, vk::ImageManager& in_texture_manager, 
            vk::SamplerManager& in_sampler_manager, vk::Debug& in_debug, vk::VkDebugObjectTracker& in_debug_tracker)
            : swapchain(VK_NULL_HANDLE)
            , image_manager(in_texture_manager)
            , vk_extent()
            , surface_format()
            , instance(in_instance)
            , sampler_manager(in_sampler_manager)
            , debug(in_debug)
            , debug_object_tracker(in_debug_tracker)
        {
        }

        void create();
        void init(VkSwapchainKHR old_swapchain);
        void recreate_swapchain();
        Extent get_extent() const;

        RBImageHandle get_image(RBFrameHandle frame_handle) const;
        bool acquire_next_image(RBFrameHandle frame_handle);
        bool submit_frame(RBFrameHandle frame_handle, const RBCommandList& cmd_list);
        void advance_frame();
        void cleanup();
        void create_sync_objects();
        void wait_for_frame(RBFrameHandle frame_handle);
        void reset_frame_fence(uint32_t frame);

        VkSwapchainKHR swapchain = VK_NULL_HANDLE;
        VkExtent2D vk_extent;
    
        VkImage depth_image = VK_NULL_HANDLE;
        VkDeviceMemory depth_memory = VK_NULL_HANDLE;
        VkImageView depth_image_view = VK_NULL_HANDLE;
        VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
        
        
        VkSurfaceFormatKHR surface_format;
        
        vk::VkDebugObjectTracker& debug_object_tracker;
        vk::Instance& instance;
        vk::ImageManager& image_manager;
        vk::SamplerManager& sampler_manager;
        vk::Debug& debug;
        std::vector<RBImageHandle> swapchain_image_handles;
        
        
        uint32_t current_swapchain_index = 0;
        
        
        
        std::array<FrameContext, MAX_FRAMES_IN_FLIGHT> frames;
        
        
        uint32_t current_frame = 0;
        
        bool framebuffer_resized = false;
        std::vector<VkSemaphore> render_finished_per_image;

        std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> render_finished_per_frame;
    };
}
