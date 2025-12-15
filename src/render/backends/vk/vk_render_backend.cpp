#include "vk_render_backend.h"

#include <cassert>
#include <set>
#include <GLFW/glfw3.h>
#include <algorithm>

#include "vk_macro.h"

struct QueueFamilies {
    uint32_t graphics = UINT32_MAX;
    uint32_t present  = UINT32_MAX;

    bool complete() const {
        return graphics != UINT32_MAX &&
               present  != UINT32_MAX;
    }
};

struct SwapchainSupport {
    VkSurfaceCapabilitiesKHR caps;
    std::vector<VkSurfaceFormatKHR> formats;
    std::vector<VkPresentModeKHR> presentModes;
};


constexpr const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};


SwapchainSupport querySwapchainSupport(
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

    s.presentModes.resize(count);
    vkGetPhysicalDeviceSurfacePresentModesKHR(
        device, surface, &count, s.presentModes.data());

    return s;
}


QueueFamilies findQueueFamilies(
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

        VkBool32 presentSupport = false;
        vkGetPhysicalDeviceSurfaceSupportKHR(
            device, i, surface, &presentSupport);

        if (presentSupport)
            result.present = i;
    }

    return result;
}

VkSurfaceFormatKHR chooseSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR>& formats)
{
    for (const auto& f : formats) {
        if (f.format == VK_FORMAT_B8G8R8A8_SRGB &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            return f;
    }
    return formats[0];
}

VkPresentModeKHR choosePresentMode(
    const std::vector<VkPresentModeKHR>& modes)
{
    for (auto m : modes) {
        if (m == VK_PRESENT_MODE_MAILBOX_KHR)
            return m; // triple buffering
    }
    return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D chooseExtent(
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
    vkWaitForFences(
        info.device, 1, &info.inFlight, VK_TRUE, UINT64_MAX);
    vkResetFences(info.device, 1, &info.inFlight);

    uint32_t imageIndex;
    vkAcquireNextImageKHR(
        info.device,
        info.swapchain,
        UINT64_MAX,
        info.imageAvailable,
        VK_NULL_HANDLE,
        &imageIndex);

    VkPipelineStageFlags waitStage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{
        VK_STRUCTURE_TYPE_SUBMIT_INFO
    };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &info.imageAvailable;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers =
        &info.commandBuffers[imageIndex];
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &info.renderFinished;

    VK_CHECK(vkQueueSubmit(
        info.graphicsQueue, 1, &submit, info.inFlight));

    VkPresentInfoKHR present{
        VK_STRUCTURE_TYPE_PRESENT_INFO_KHR
    };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &info.renderFinished;
    present.swapchainCount = 1;
    present.pSwapchains = &info.swapchain;
    present.pImageIndices = &imageIndex;

    vkQueuePresentKHR(info.presentQueue, &present);
}


void VkRenderBackend::create_render_pass(VkSurfaceFormatKHR surfaceFormat)
{
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = surfaceFormat.format;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout =
        VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    
    VkAttachmentReference colorRef;
    colorRef.attachment = 0;
    colorRef.layout =
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    
    
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint =
        VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &colorRef;
    
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
    rpci.pAttachments = &colorAttachment;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 1;
    rpci.pDependencies = &dep;

    VK_CHECK(vkCreateRenderPass(info.device, &rpci, nullptr, &info.renderPass));
}

void VkRenderBackend::create_framebuffers()
{
    info.framebuffers.resize(info.swapchainImageViews.size());

    for (size_t i = 0; i < info.swapchainImageViews.size(); i++) {
        VkImageView attachments[] = {
            info.swapchainImageViews[i]
        };

        VkFramebufferCreateInfo fbci{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        fbci.renderPass = info.renderPass;
        fbci.attachmentCount = 1;
        fbci.pAttachments = attachments;
        fbci.width = info.extent.width;
        fbci.height = info.extent.height;
        fbci.layers = 1;

        VK_CHECK(vkCreateFramebuffer(
            info.device, &fbci, nullptr,
            &info.framebuffers[i]));
    }
}

void VkRenderBackend::init(void* in_window)
{
    GLFWwindow* window = static_cast<GLFWwindow*>(in_window);
    VkInstance instance;

    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Rhea";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &appInfo;
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = VALIDATION_LAYERS;


    uint32_t extCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extCount);
    ici.enabledExtensionCount = extCount;
    ici.ppEnabledExtensionNames = extensions;

    VK_CHECK(vkCreateInstance(&ici, nullptr, &instance));
    
    VkSurfaceKHR surface;
    glfwCreateWindowSurface(instance, window, nullptr, &surface);
    
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr);

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data());
    
    
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    QueueFamilies queues;

    for (auto dev : devices) {
        auto q = findQueueFamilies(dev, surface);
        if (q.complete()) {
            physicalDevice = dev;
            queues = q;
            break;
        }
    }

    assert(physicalDevice != VK_NULL_HANDLE);
    
    float priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    std::set<uint32_t> uniqueFamilies = {
        queues.graphics,
        queues.present
    };
    
    VkPhysicalDeviceFeatures features{};
    
    for (uint32_t family : uniqueFamilies) {
        VkDeviceQueueCreateInfo qi{
            VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO
        };
        qi.queueFamilyIndex = family;
        qi.queueCount = 1;
        qi.pQueuePriorities = &priority;
        queueInfos.push_back(qi);
    }
    
    auto& device = info.device;
    
    const char* deviceExtensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };

    
    VkDeviceCreateInfo dci{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };
    dci.queueCreateInfoCount =
        static_cast<uint32_t>(queueInfos.size());
    dci.pQueueCreateInfos = queueInfos.data();
    dci.pEnabledFeatures = &features;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = deviceExtensions;

    VK_CHECK(vkCreateDevice(
        physicalDevice,
        &dci,
        nullptr,
        &device
    ));
    
    SwapchainSupport support =
    querySwapchainSupport(physicalDevice, surface);

    VkSurfaceFormatKHR surfaceFormat = chooseSurfaceFormat(support.formats);

    VkPresentModeKHR presentMode = choosePresentMode(support.presentModes);

    info.extent = chooseExtent(support.caps, window);

    uint32_t imageCount = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 && imageCount > support.caps.maxImageCount)
    {
        imageCount = support.caps.maxImageCount;
    }

    VkSwapchainCreateInfoKHR sci{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    sci.surface = surface;
    sci.minImageCount = imageCount;
    sci.imageFormat = surfaceFormat.format;
    sci.imageColorSpace = surfaceFormat.colorSpace;
    sci.imageExtent = info.extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    
    
    uint32_t queueIndices[] = {
        queues.graphics,
        queues.present
    };

    if (queues.graphics != queues.present) {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = queueIndices;
    } else {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }
    
    sci.preTransform = support.caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode = presentMode;
    sci.clipped = VK_TRUE;
    sci.oldSwapchain = VK_NULL_HANDLE;
    
    
    auto& swapchain = info.swapchain;
    VK_CHECK(vkCreateSwapchainKHR(device, &sci, nullptr, &swapchain));
    
    uint32_t scImageCount = 0;
    vkGetSwapchainImagesKHR(
        device, swapchain, &scImageCount, nullptr);

    std::vector<VkImage> swapchainImages(scImageCount);
    vkGetSwapchainImagesKHR(
        device, swapchain, &scImageCount,
        swapchainImages.data());
    
    

    info.swapchainImageViews.resize(swapchainImages.size());

    for (size_t i = 0; i < swapchainImages.size(); i++) {
        VkImageViewCreateInfo ivci{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        ivci.image = swapchainImages[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = surfaceFormat.format;
        ivci.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(device, &ivci, nullptr, &info.swapchainImageViews[i]));
    }
    
    create_render_pass(surfaceFormat);
    
    create_framebuffers();
    
    auto& graphicsQueue = info.graphicsQueue;
    vkGetDeviceQueue(
        device,
        queues.graphics,
        0,
        &graphicsQueue
    );
    
    auto& presentQueue = info.presentQueue;
    vkGetDeviceQueue(
        device,
        queues.present,
        0,
        &presentQueue
    );
    
    
    VkCommandPool commandPool;

    VkCommandPoolCreateInfo cpci{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    cpci.queueFamilyIndex = queues.graphics;
    cpci.flags =
        VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(device, &cpci, nullptr, &commandPool));
    
    info.commandBuffers.resize(info.swapchainImageViews.size());

    VkCommandBufferAllocateInfo cbai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    cbai.commandPool = commandPool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount =
        static_cast<uint32_t>(info.commandBuffers.size());

    VK_CHECK(vkAllocateCommandBuffers(
        device, &cbai, info.commandBuffers.data()));
    
    VkClearValue clear{};
    clear.color = { { 0.1f, 0.1f, 0.3f, 1.0f } };
    
    for (size_t i = 0; auto& cmd : info.commandBuffers) {
        VkCommandBufferBeginInfo bi{
            VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
        };

       VK_CHECK( vkBeginCommandBuffer(cmd, &bi));

        VkRenderPassBeginInfo rpbi{
            VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
        };
        rpbi.renderPass = info.renderPass;
        rpbi.framebuffer = info.framebuffers[i];
        rpbi.renderArea.extent = info.extent;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;

        vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);



        vkCmdEndRenderPass(cmd);
        VK_CHECK(vkEndCommandBuffer(cmd));
        i++;
    }

    VkSemaphoreCreateInfo sem_ci{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    VkFenceCreateInfo fci{
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };
    fci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    VK_CHECK(vkCreateSemaphore(
        device, &sem_ci, nullptr, &info.imageAvailable));
    VK_CHECK(vkCreateSemaphore(
        device, &sem_ci, nullptr, &info.renderFinished));
    VK_CHECK(vkCreateFence(
        device, &fci, nullptr, &info.inFlight));
    
    
}
