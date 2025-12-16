#include "vk_render_backend.h"

#include <cassert>
#include <set>
#include <GLFW/glfw3.h>
#include <algorithm>

#include "vk_macro.h"
#include "vk_pipeline.h"


struct SwapchainSupport {
    std::vector<VkPresentModeKHR> present_modes;
    std::vector<VkSurfaceFormatKHR> formats;
    VkSurfaceCapabilitiesKHR caps;
};


constexpr const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};


SwapchainSupport query_swapchain_support(
    VkPhysicalDevice device,
    VkSurfaceKHR surface)
{
    SwapchainSupport s;

    vkGetPhysicalDeviceSurfaceCapabilitiesKHR(
        device, surface, &s.caps);

    uint32_t count = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, surface, &count, nullptr);

    s.formats.resize(count);
    vkGetPhysicalDeviceSurfaceFormatsKHR(
        device, surface, &count, s.formats.data());

    count = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &count, nullptr);

    s.present_modes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &count, s.present_modes.data());

    return s;
}


QueueFamilies find_queue_families(
    VkPhysicalDevice device,
    VkSurfaceKHR surface)
{
    QueueFamilies result;

    uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);

    std::vector<VkQueueFamilyProperties> props(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, props.data());

    for (uint32_t i = 0; i < count; i++) {
        if (props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT)
            result.graphics = i;

        VkBool32 present_support = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            device, i, surface, &present_support);

        if (present_support)
            result.present = i;
    }

    return result;
}

VkSurfaceFormatKHR choose_surface_format(
    const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return formats[0];
}

VkPresentModeKHR choose_present_mode(
    const std::vector<VkPresentModeKHR>& modes)
{
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            return m; // triple buffering
    }
    return VK_PRESENT_MODE_FIFO_KHR; 
}

VkExtent2D choose_extent(
    const VkSurfaceCapabilitiesKHR& caps,
    GLFWwindow* window)
{
    if (caps.currentExtent.width != UINT32_MAX)
        return caps.currentExtent;

    int w, h;
    glfwGetFramebufferSize(window, &w, &h);

    VkExtent2D extent{
        static_cast<uint32_t>(w),
        static_cast<uint32_t>(h)
    };

    extent.width = std::clamp(
        extent.width,
        caps.minImageExtent.width,
        caps.maxImageExtent.width);

    extent.height = std::clamp(
        extent.height,
        caps.minImageExtent.height,
        caps.maxImageExtent.height);

    return extent;
}

void VkRenderBackend::draw_frame()
{
    auto& frame = context.frames[context.current_frame];
    
    vkWaitForFences(context.device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);

    uint32_t image_index;
    VkResult res = vkAcquireNextImageKHR(
        context.device,
        context.swapchain,
        UINT64_MAX,
        frame.image_available,
        VK_NULL_HANDLE,
        &image_index);

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreate_swapchain();
        return;
    }
    else if (res != VK_SUCCESS && res != VK_SUBOPTIMAL_KHR)
    {
        throw std::runtime_error("failed to acquire swapchain image");
    }
    
    if (context.images_in_flight[image_index] != VK_NULL_HANDLE)
    {
        vkWaitForFences(
            context.device, 1,
            &context.images_in_flight[image_index],
            VK_TRUE,
            UINT64_MAX);
    }
    
    context.images_in_flight[image_index] = frame.in_flight;
    
    vkResetFences(context.device, 1, &frame.in_flight);
    

    constexpr VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    
    VkSemaphore render_finished =
        context.render_finished_per_image[image_index];
    
    VkSubmitInfo submit{
        VK_STRUCTURE_TYPE_SUBMIT_INFO
        
    };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers =
        &context.command_buffers[image_index];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &render_finished;

    VK_CHECK(vkQueueSubmit(
        context.graphics_queue, 1, &submit, frame.in_flight));

    VkPresentInfoKHR present{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
    };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &context.swapchain;
    present.pImageIndices = &image_index;
    
    VkResult res2 = vkQueuePresentKHR(context.present_queue, &present);

    if (res2 == VK_ERROR_OUT_OF_DATE_KHR ||
        res2 == VK_SUBOPTIMAL_KHR ||
        framebuffer_resized)
    {
        framebuffer_resized = false;
        recreate_swapchain();
    }
    
    
    context.current_frame = (context.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
}

void VkRenderBackend::create_instance()
{
    
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Rhea";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &appInfo;
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = VALIDATION_LAYERS;


    uint32_t ext_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&ext_count);
    ici.enabledExtensionCount = ext_count;
    ici.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ici, nullptr, &context.instance));
    
}

void VkRenderBackend::create_device()
{
    
    float priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    std::set<uint32_t> unique_families = {
        context.queues.graphics,
        context.queues.present
    };
    
    VkPhysicalDeviceFeatures features{};
    
    for (uint32_t family : unique_families) {
        VkDeviceQueueCreateInfo qi{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queue_infos.push_back(qi);
    }
    
    auto& device = context.device;
    
    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    
    VkDeviceCreateInfo dci{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };
    dci.queueCreateInfoCount =
        static_cast<uint32_t>(queue_infos.size());
    dci.pQueueCreateInfos = queue_infos.data();
    dci.pEnabledFeatures = &features;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = device_extensions;

    VK_CHECK(vkCreateDevice(
        context.physical_device,
        &dci,
        nullptr,
        &device
    ));
}

void VkRenderBackend::recreate_swapchain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0)
    {
        glfwGetFramebufferSize(window, &w, &h);
        glfwWaitEvents();
    }

    vkDeviceWaitIdle(context.device);

    cleanup_swapchain();

    create_swapchain();
    create_render_pass();
    create_framebuffers();
    create_command_pool();
    create_pipeline();
    record_command_buffers();
    
    create_image_sync_objects();
}


void VkRenderBackend::create_render_pass()
{
    VkAttachmentDescription color_attachment{};
    color_attachment.format = context.surface_format.format;
    color_attachment.samples = VK_SAMPLE_COUNT_1_BIT;
    color_attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color_attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color_attachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color_attachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color_attachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color_attachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference color_ref;
    color_ref.attachment = 0;
    color_ref.layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint =
        VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    
    VkSubpassDependency dep{};
    dep.srcSubpass = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass = 0;
    dep.srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo rpci{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
    };
    rpci.attachmentCount = 1;
    rpci.pAttachments = &color_attachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(context.device, &rpci, nullptr, &context.render_pass));
}

void VkRenderBackend::create_framebuffers()
{
    context.framebuffers.resize(context.swapchain_image_views.size());

    for (size_t i = 0; i < context.swapchain_image_views.size(); i++) {
        VkImageView attachments[] = {
            context.swapchain_image_views[i]
        };

        VkFramebufferCreateInfo fbci{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        fbci.renderPass = context.render_pass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = attachments;
        fbci.width = context.extent.width;
        fbci.height = context.extent.height;
        fbci.layers = 1;

        VK_CHECK(vkCreateFramebuffer(
            context.device, &fbci, nullptr,
            &context.framebuffers[i]));
    }
}

void VkRenderBackend::create_swapchain()
{
    SwapchainSupport support =
    query_swapchain_support(context.physical_device, context.surface);

    context.surface_format = choose_surface_format(support.formats);
    

    VkPresentModeKHR present_mode = choose_present_mode(support.present_modes);

    context.extent = choose_extent(support.caps, window);

    uint32_t image_count = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 && image_count > support.caps.maxImageCount)
    {
        image_count = support.caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    sci.surface = context.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = context.surface_format.format;
    sci.imageColorSpace = context.surface_format.colorSpace;
    sci.imageExtent = context.extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    

    if (context.graphics_queue != context.present_queue) {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = context.queues_indices;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    sci.preTransform = support.caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = present_mode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;
    
    
    auto& swapchain = context.swapchain;
    VK_CHECK(vkCreateSwapchainKHR(context.device, &sci, nullptr, &swapchain));
    
    uint32_t sc_image_count = 0;
    vkGetSwapchainImagesKHR(
        context.device, swapchain, &sc_image_count, nullptr);

    std::vector<VkImage> swapchain_images(sc_image_count);
    vkGetSwapchainImagesKHR(
        context.device, swapchain, &sc_image_count,
        swapchain_images.data());
    
    context.images_count = sc_image_count;
    context.images_in_flight.clear();
    context.images_in_flight.resize(sc_image_count, VK_NULL_HANDLE);
    
    

    context.swapchain_image_views.resize(swapchain_images.size());

    for (size_t i = 0; i < swapchain_images.size(); i++) {
        VkImageViewCreateInfo ivci{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        ivci.image = swapchain_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = context.surface_format.format;
        ivci.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(context.device, &ivci, nullptr, &context.swapchain_image_views[i]));
    }
    
}

void VkRenderBackend::create_pipeline()
{
    
    pipeline = std::make_unique<VkPipelineObject>(context);
    

}

void VkRenderBackend::create_command_pool()
{
    vkGetDeviceQueue(
        context.device,
        context.queues.graphics,
        0,
        &context.graphics_queue
    );
    
    vkGetDeviceQueue(
        context.device,
        context.queues.present,
        0,
        &context.present_queue
    );
    

    VkCommandPoolCreateInfo cpci{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    cpci.queueFamilyIndex = context.queues.graphics;
    cpci.flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(context.device, &cpci, nullptr, &context.command_pool));
    
    context.command_buffers.resize(context.swapchain_image_views.size());

    VkCommandBufferAllocateInfo cbai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    cbai.commandPool = context.command_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount =
        static_cast<uint32_t>(context.command_buffers.size());

    VK_CHECK(vkAllocateCommandBuffers(
        context.device, &cbai, context.command_buffers.data()));
    
}

void VkRenderBackend::cleanup_swapchain()
{
    for (auto fb : context.framebuffers)
        vkDestroyFramebuffer(context.device, fb, nullptr);

    vkFreeCommandBuffers(
        context.device,
        context.command_pool,
        context.command_buffers.size(),
        context.command_buffers.data());

    vkDestroyPipeline(context.device, pipeline->pipeline(), nullptr);
    vkDestroyPipelineLayout(context.device, pipeline->layout(), nullptr);

    vkDestroyRenderPass(context.device, context.render_pass, nullptr);

    for (auto iv : context.swapchain_image_views)
        vkDestroyImageView(context.device, iv, nullptr);

    vkDestroySwapchainKHR(context.device, context.swapchain, nullptr);
}

void VkRenderBackend::match_queue_families()
{
    
    
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, nullptr);

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(context.instance, &deviceCount, devices.data());
    

    for (auto device : devices) {
        auto q = find_queue_families(device, context.surface);
        if (q.complete()) {
            context.physical_device = device;
            context.queues = q;
            break;
        }
    }
    
    

    assert(context.physical_device != VK_NULL_HANDLE);
    
    
    context.queues_indices[0] = context.queues.graphics;
    context.queues_indices[1] = context.queues.present;
}

void VkRenderBackend::record_command_buffers()
{
    
    VkClearValue clear{};
    clear.color = { { 0.1f, 0.1f, 0.3f, 1.0f } };
    
    
    for (size_t i = 0; i < context.command_buffers.size(); ++i) {
        VkCommandBuffer cmd = context.command_buffers[i];
        
        VkCommandBufferBeginInfo bi{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };

        VK_CHECK( vkBeginCommandBuffer(cmd, &bi));

        VkRenderPassBeginInfo rpbi{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rpbi.renderPass = context.render_pass;
        rpbi.framebuffer = context.framebuffers[i];
        rpbi.renderArea.extent = context.extent;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(
            cmd,
            VK_PIPELINE_BIND_POINT_GRAPHICS,
            pipeline->pipeline());
        
        vkCmdDraw(cmd, 3, 1, 0, 0);

        vkCmdEndRenderPass(cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));
    }
}

void VkRenderBackend::init(void* in_window)
{
    window = static_cast<GLFWwindow*>(in_window);
    
    glfwSetWindowUserPointer(window, this);

    glfwSetFramebufferSizeCallback(window,
    [](GLFWwindow* win, int, int)
    {
        auto backend = static_cast<VkRenderBackend*>(
            glfwGetWindowUserPointer(win));
        backend->framebuffer_resized = true;
    });
    
    create_instance();
    
    glfwCreateWindowSurface(context.instance, window, nullptr, &context.surface);
    
    
    match_queue_families();
    
    create_device();
    
    
    create_swapchain();
    
    create_render_pass();
    
    create_framebuffers();
    
    create_command_pool();
    
    create_pipeline();
    record_command_buffers();
    create_frame_sync_objects();
    create_image_sync_objects();
    
}



void VkRenderBackend::create_frame_sync_objects()
{
    
    VkSemaphoreCreateInfo sem_ci{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    for (auto& frame : context.frames)
    {
        VK_CHECK(vkCreateSemaphore(context.device, &sem_ci, nullptr, &frame.image_available));

        VkFenceCreateInfo fci{
            VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
        };
        fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

        VK_CHECK(
            vkCreateFence(context.device, &fci, nullptr, &frame.in_flight);
        );
    }
}

void VkRenderBackend::create_image_sync_objects()
{
    VkSemaphoreCreateInfo sem_ci{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };
    
    for (auto sem : context.render_finished_per_image)
        vkDestroySemaphore(context.device, sem, nullptr);

    context.render_finished_per_image.clear();
    context.render_finished_per_image.resize(context.images_count);
    
    
    for (auto& sem : context.render_finished_per_image) {
        VK_CHECK(vkCreateSemaphore(context.device, &sem_ci, nullptr, &sem));
    }
    
}
