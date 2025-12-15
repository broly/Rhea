#pragma once
#include <vector>
#include <GLFW/glfw3.h>

#include "render/render_backend.h"

class VkRenderBackend final : public RenderBackend
{
public:
    virtual void init(void* window) override;
    virtual void draw_frame() override;
    void create_render_pass(VkSurfaceFormatKHR surfaceFormat);
    void create_framebuffers();
    
    struct
    {
        VkDevice device;
        VkSwapchainKHR swapchain;
        VkQueue graphicsQueue;
        VkQueue presentQueue;
        VkSemaphore imageAvailable;
        VkSemaphore renderFinished;
        VkFence inFlight;
        std::vector<VkCommandBuffer> commandBuffers;
        VkRenderPass renderPass;
        std::vector<VkFramebuffer> framebuffers;
        std::vector<VkImageView> swapchainImageViews;
        VkExtent2D extent;
    } info = {};
};
