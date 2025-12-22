#include "vk_render_backend.h"

#include <cassert>
#include <set>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <span>

#include "vk_camera_ubo.h"
#include "vk_helpers.h"
#include "vk_macro.h"
#include "vk_pipeline.h"
#include "logging/log.h"

DEFINE_LOGGER(LogRB, Log);

constexpr const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};


void VkRenderBackend::create_instance()
{
    VkApplicationInfo appInfo{ VK_STRUCTURE_TYPE_APPLICATION_INFO };
    appInfo.pApplicationName = "Rhea";
    appInfo.apiVersion = VK_API_VERSION_1_3;

    VkInstanceCreateInfo ici{ VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
    ici.pApplicationInfo = &appInfo;
    ici.enabledLayerCount = 1;
    ici.ppEnabledLayerNames = VALIDATION_LAYERS;

    // extensions (by GLFW)
    uint32_t ext_count = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&ext_count);
    ici.enabledExtensionCount = ext_count;
    ici.ppEnabledExtensionNames = extensions;

    VK_CHECK(
        vkCreateInstance(&ici, nullptr, &instance_context.instance)
    );
}

void VkRenderBackend::create_device()
{
    float priority = 1.0f;

    std::vector<VkDeviceQueueCreateInfo> queue_infos;
    std::set<uint32_t> unique_families = {
        instance_context.queues.graphics,
        instance_context.queues.present
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
    
    auto& device = instance_context.device;
    
    const char* device_extensions[] = {
        VK_KHR_SWAPCHAIN_EXTENSION_NAME
    };
    
    VkDeviceCreateInfo dci{
        VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO
    };
    dci.queueCreateInfoCount = static_cast<uint32_t>(queue_infos.size());
    dci.pQueueCreateInfos = queue_infos.data();
    dci.pEnabledFeatures = &features;
    dci.enabledExtensionCount = 1;
    dci.ppEnabledExtensionNames = device_extensions;

    VK_CHECK(
        vkCreateDevice(instance_context.physical_device, &dci, nullptr, &device)
    );
}
void VkRenderBackend::recreate_swapchain()
{
    int w = 0, h = 0;
    glfwGetFramebufferSize(window, &w, &h);
    while (w == 0 || h == 0)
    {
        glfwWaitEvents();
        glfwGetFramebufferSize(window, &w, &h);
    }

    vkDeviceWaitIdle(instance_context.device);
    cleanup_swapchain();
    create_swapchain();
    create_depth_resources();
    create_render_pass();
    create_framebuffers();
    create_images_context();
}

void VkRenderBackend::create_images_context()
{
    images.clear();
    images.resize(swapchain_context.image_views.size());

    VkSemaphoreCreateInfo sem_ci{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    for (size_t i = 0; i < images.size(); i++)
    {
        vk::ImageContext& img = images[i];

        img.image_view  = swapchain_context.image_views[i];
        img.framebuffer = swapchain_context.framebuffers[i];
        img.in_flight   = VK_NULL_HANDLE;

        VK_CHECK(
            vkCreateSemaphore(instance_context.device, &sem_ci, nullptr, &img.render_finished)
        );
    }
}

void VkRenderBackend::create_depth_resources()
{
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    swapchain_context.depth_format = depth_format;

    VkImageCreateInfo image_ci{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
    };
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.extent.width  = swapchain_context.extent.width;
    image_ci.extent.height = swapchain_context.extent.height;
    image_ci.extent.depth  = 1;
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.format = depth_format;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(
        vkCreateImage(instance_context.device, &image_ci, nullptr, &swapchain_context.depth_image)
    );

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(instance_context.device, swapchain_context.depth_image, &mem_req);

    VkMemoryAllocateInfo alloc{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex = vk::find_memory_type(instance_context.physical_device,mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(
        vkAllocateMemory(instance_context.device, &alloc, nullptr, &swapchain_context.depth_memory)
    );

    vkBindImageMemory(instance_context.device, swapchain_context.depth_image, swapchain_context.depth_memory,0);

    VkImageViewCreateInfo view_ci{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
    };
    view_ci.image = swapchain_context.depth_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = depth_format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(
        instance_context.device, &view_ci, nullptr,
        &swapchain_context.depth_image_view));
}

void VkRenderBackend::update_camera_ubo(
    vk::FrameContext& frame,
    const Camera& camera)
{
    CameraUBO ubo;

    const float aspect =
        static_cast<float>(swapchain_context.extent.width) /
        static_cast<float>(swapchain_context.extent.height);

    ubo.mvp =
        camera.projection(aspect) *
        camera.view() *
        Transform{}.matrix();

    void* data = nullptr;
    VK_CHECK(
        vkMapMemory(instance_context.device, frame.camera_memory, 0, sizeof(CameraUBO), 0, &data)
    );

    std::memcpy(data, &ubo, sizeof(CameraUBO));

    vkUnmapMemory(
        instance_context.device,
        frame.camera_memory);
}

RBDescriptorSetLayout VkRenderBackend::allocate_descriptor_layout_handle()
{
    auto result = ++descriptor_set_counter;
    RBDescriptorSetLayout layout((uintptr_t)result);    
    return layout;
}


RBDescriptorSetLayout VkRenderBackend::create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout)
{
    std::vector<VkDescriptorSetLayoutBinding> vk_bindings;
    vk_bindings.reserve(descriptor_set_layout.bindings.size());

    for (const DescriptorBinding& b : descriptor_set_layout.bindings)
    {
        VkDescriptorSetLayoutBinding vk{};
        vk.binding = b.binding;
        vk.descriptorType = vk::to_vk_descriptor_type(b.type);
        vk.descriptorCount = b.count;
        vk.stageFlags = vk::to_vk_shader_stage_flags(b.stages);
        vk.pImmutableSamplers = nullptr;

        vk_bindings.push_back(vk);
    }

    VkDescriptorSetLayoutCreateInfo ci{
        VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO
    };
    ci.bindingCount = uint32_t(vk_bindings.size());
    ci.pBindings = vk_bindings.data();

    VkDescriptorSetLayout layout = VK_NULL_HANDLE;
    VK_CHECK(vkCreateDescriptorSetLayout(
        instance_context.device,
        &ci,
        nullptr,
        &layout
    ));
    DescriptorSetLayoutData layout_data {
        layout, 
        descriptor_set_layout.set_index
    };
    RBDescriptorSetLayout handle = allocate_descriptor_layout_handle();
    descriptor_set_layouts[handle] = layout_data;

    return handle;
}

void VkRenderBackend::create_descriptor_pool()
{
    {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = vk::MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        };
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = vk::MAX_FRAMES_IN_FLIGHT;
        pool_info.flags = 0; // without FREE_DESCRIPTOR_SET

        VK_CHECK(
            vkCreateDescriptorPool(instance_context.device, &pool_info, nullptr, &descriptor_context.persistent_pool)
        );
    }
    {
        VkDescriptorPoolSize pool_size{};
        pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        pool_size.descriptorCount = vk::MAX_FRAMES_IN_FLIGHT;

        VkDescriptorPoolCreateInfo pool_info{
            VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO
        };
        pool_info.poolSizeCount = 1;
        pool_info.pPoolSizes = &pool_size;
        pool_info.maxSets = vk::MAX_FRAMES_IN_FLIGHT;
        pool_info.flags = 0; // without FREE_DESCRIPTOR_SET

        VK_CHECK(
            vkCreateDescriptorPool(instance_context.device, &pool_info, nullptr, &descriptor_context.frame_pool)
        );
    }
}


void VkRenderBackend::allocate_descriptor_sets_for_layout(
    RBDescriptorSetLayout layout_handle,
    DescriptorPoolType pool_type)
{
    auto& layout_data = descriptor_set_layouts.at(layout_handle);
    
    if (pool_type == DescriptorPoolType::Frame)
    {
        for (size_t frame_index = 0; frame_index < vk::MAX_FRAMES_IN_FLIGHT; frame_index++)
        {
            VkDescriptorSetAllocateInfo alloc{
                VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
            };
            alloc.descriptorPool = descriptor_context.frame_pool;
            alloc.descriptorSetCount = 1;
            alloc.pSetLayouts = &layout_data.vk_layout;

            VkDescriptorSet set = VK_NULL_HANDLE;
            VK_CHECK(vkAllocateDescriptorSets(
                instance_context.device,
                &alloc,
                &set
            ));
            frame_schedule_context.frames[frame_index].descriptors.insert({layout_handle, set});
        }
    } else
    {
        VkDescriptorSetAllocateInfo alloc{
            VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO
        };
        alloc.descriptorPool = descriptor_context.persistent_pool;
        alloc.descriptorSetCount = 1;
        alloc.pSetLayouts = &layout_data.vk_layout;

        VkDescriptorSet set = VK_NULL_HANDLE;
        VK_CHECK(vkAllocateDescriptorSets(
            instance_context.device,
            &alloc,
            &set
        ));
        
        persistent_descriptors.insert({layout_handle, set});
    }

}

void VkRenderBackend::bind_descriptor_set(RBCommandList cmd_list, int i, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle)
{
    auto& pipeline = pipelines[pipeline_handle];
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    VkDescriptorSet vk_set = rb_descriptors.as<VkDescriptorSet>();

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->get_pipeline_layout(),
        0, 
        1, 
        &vk_set,
        0, nullptr
    );
}

RBFrameHandle VkRenderBackend::get_current_frame() const
{
    return frame_schedule_context.current_frame;
}

void VkRenderBackend::wait_for_frame(RBFrameHandle frame_handle)
{
    auto& frame = frame_schedule_context.frames[frame_handle];
    if (frame.in_flight != VK_NULL_HANDLE) {
        vkWaitForFences(instance_context.device, 1, &frame.in_flight, VK_TRUE, UINT64_MAX);
        vkResetFences(instance_context.device, 1, &frame.in_flight);
    }
}

void VkRenderBackend::advance_frame()
{
    frame_schedule_context.current_frame =
        (frame_schedule_context.current_frame + 1) % vk::MAX_FRAMES_IN_FLIGHT;
}

void VkRenderBackend::bind_mesh(const RBCommandList& cmd, MeshHandle mesh)
{
    // TODO
}

void VkRenderBackend::push_constants(const RBCommandList& cmd, glm::mat4 matrix)
{
    // TODO
    vkCmdPushConstants(
        cmd,
        pipelines.begin()->second->get_pipeline_layout(), 
        VK_SHADER_STAGE_VERTEX_BIT, 
        0, 
        sizeof(glm::mat4),
        &matrix
    );
}

void VkRenderBackend::draw_indexed(const RBCommandList& cmd, uint32_t index_count)
{
    // TODO
    vkCmdDraw(cmd, index_count, 1, 0, 0);
}


void VkRenderBackend::create_render_pass()
{
    VkAttachmentDescription color{};
    color.format = swapchain_context.surface_format.format;
    color.samples = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    color.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    color.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentDescription depth{};
    depth.format = swapchain_context.depth_format;
    depth.samples = VK_SAMPLE_COUNT_1_BIT;
    depth.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    depth.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    depth.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    depth.finalLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

    VkAttachmentReference color_ref{
        0,
        VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
    };

    VkAttachmentReference depth_ref{
        1,
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
    };

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint =
        VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color_ref;
    subpass.pDepthStencilAttachment = &depth_ref;

    VkAttachmentDescription attachments[2] = {
        color, depth
    };

    VkRenderPassCreateInfo rpci{
        VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO
    };
    rpci.attachmentCount = 2;
    rpci.pAttachments = attachments;
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;

    VK_CHECK(vkCreateRenderPass(
        instance_context.device, &rpci, nullptr,
        &swapchain_context.render_pass));
}

void VkRenderBackend::create_framebuffers()
{
    swapchain_context.framebuffers.resize(
        swapchain_context.image_views.size());

    for (size_t i = 0;
         i < swapchain_context.image_views.size(); i++)
    {
        VkImageView attachments[] = {
            swapchain_context.image_views[i],
            swapchain_context.depth_image_view
        };

        VkFramebufferCreateInfo fbci{
            VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO
        };
        fbci.renderPass = swapchain_context.render_pass;
        fbci.attachmentCount = 2;
        fbci.pAttachments = attachments;
        fbci.width  = swapchain_context.extent.width;
        fbci.height = swapchain_context.extent.height;
        fbci.layers = 1;

        VK_CHECK(vkCreateFramebuffer(
            instance_context.device, &fbci, nullptr,
            &swapchain_context.framebuffers[i]));
    }
}

void VkRenderBackend::create_frame_resources()
{
    constexpr VkDeviceSize size = sizeof(CameraUBO);

    for (auto& frame : frame_schedule_context.frames)
    {
        vk::create_buffer(
            instance_context.device,
            instance_context.physical_device,
            size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            frame.camera_buffer,
            frame.camera_memory);
    }
}
void VkRenderBackend::create_swapchain()
{
    vk::SwapchainSupport support = vk::query_swapchain_support(
        instance_context.physical_device, instance_context.surface);

    // --- Surface format ---
    swapchain_context.surface_format = vk::choose_surface_format(support.formats);

    VkPresentModeKHR present_mode = vk::choose_present_mode(support.present_modes);

    swapchain_context.extent = vk::choose_extent(support.caps, window);

    // --- Image count ---
    uint32_t image_count = support.caps.minImageCount + 1;
    if (support.caps.maxImageCount > 0 &&
        image_count > support.caps.maxImageCount)
    {
        image_count = support.caps.maxImageCount;
    }

    // --- Swapchain create info ---
    VkSwapchainCreateInfoKHR sci{
        VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR
    };
    sci.surface = instance_context.surface;
    sci.minImageCount = image_count;
    sci.imageFormat = swapchain_context.surface_format.format;
    sci.imageColorSpace = swapchain_context.surface_format.colorSpace;
    sci.imageExtent = swapchain_context.extent;
    sci.imageArrayLayers = 1;
    sci.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (instance_context.queues.graphics !=
        instance_context.queues.present)
    {
        sci.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
        sci.queueFamilyIndexCount = 2;
        sci.pQueueFamilyIndices = instance_context.queues_indices;
    }
    else
    {
        sci.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    }

    sci.preTransform   = support.caps.currentTransform;
    sci.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    sci.presentMode    = present_mode;
    sci.clipped        = VK_TRUE;
    sci.oldSwapchain   = VK_NULL_HANDLE;

    VK_CHECK(
        vkCreateSwapchainKHR(instance_context.device, &sci, nullptr, &swapchain_context.swapchain)
    );

    // --- Get swapchain images ---
    uint32_t sc_image_count = 0;
    vkGetSwapchainImagesKHR(instance_context.device, swapchain_context.swapchain, 
        &sc_image_count, nullptr);

    std::vector<VkImage> swapchain_images(sc_image_count);
    vkGetSwapchainImagesKHR(instance_context.device, swapchain_context.swapchain, 
        &sc_image_count, swapchain_images.data());

    // --- Resize contexts ---
    images.clear();
    images.resize(sc_image_count);

    swapchain_context.image_views.resize(sc_image_count);

    // --- Create image views + init ImageContext ---
    for (uint32_t i = 0; i < sc_image_count; ++i)
    {
        images[i].image = swapchain_images[i];
        images[i].in_flight = VK_NULL_HANDLE;

        VkImageViewCreateInfo ivci{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        ivci.image = swapchain_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = swapchain_context.surface_format.format;
        ivci.subresourceRange.aspectMask =
            VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(
            instance_context.device,
            &ivci,
            nullptr,
            &swapchain_context.image_views[i]));

        images[i].image_view = swapchain_context.image_views[i];
    }
}

void VkRenderBackend::create_command_pool()
{
    if (command_context.command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(instance_context.device, command_context.command_pool, nullptr);
    }
    
    vkGetDeviceQueue(instance_context.device, instance_context.queues.graphics, 0, 
        &instance_context.graphics_queue);

    vkGetDeviceQueue(instance_context.device, instance_context.queues.present, 0, 
        &instance_context.present_queue);

    VkCommandPoolCreateInfo cpci{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    cpci.queueFamilyIndex = instance_context.queues.graphics;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(
        instance_context.device,
        &cpci,
        nullptr,
        &command_context.command_pool));

    // --- Allocate per-frame command buffers ---
    VkCommandBufferAllocateInfo cbai{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO
    };
    cbai.commandPool = command_context.command_pool;
    cbai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cbai.commandBufferCount = vk::MAX_FRAMES_IN_FLIGHT;

    std::vector<VkCommandBuffer> cmds(vk::MAX_FRAMES_IN_FLIGHT);

    VK_CHECK(
        vkAllocateCommandBuffers(instance_context.device, &cbai, cmds.data())
    );

    for (uint32_t i = 0; i < vk::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        frame_schedule_context.frames[i].cmd = cmds[i];
    }
    
}
void VkRenderBackend::cleanup_swapchain()
{
    // --- Framebuffers ---
    for (auto fb : swapchain_context.framebuffers)
    {
        if (fb != VK_NULL_HANDLE)
        {
            vkDestroyFramebuffer(instance_context.device, fb, nullptr);
        }
    }
    swapchain_context.framebuffers.clear();

    // --- Render pass ---
    if (swapchain_context.render_pass != VK_NULL_HANDLE)
    {
        vkDestroyRenderPass(instance_context.device, swapchain_context.render_pass, nullptr);
        swapchain_context.render_pass = VK_NULL_HANDLE;
    }

    // --- Image views ---
    for (auto iv : swapchain_context.image_views)
    {
        if (iv != VK_NULL_HANDLE)
        {
            vkDestroyImageView(instance_context.device, iv, nullptr);
        }
    }
    swapchain_context.image_views.clear();

    // --- Depth resources ---
    if (swapchain_context.depth_image_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(instance_context.device, swapchain_context.depth_image_view, nullptr);
    }

    if (swapchain_context.depth_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(instance_context.device, swapchain_context.depth_image, nullptr);
    }

    if (swapchain_context.depth_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(instance_context.device, swapchain_context.depth_memory, nullptr);
    }

    swapchain_context.depth_image = VK_NULL_HANDLE;
    swapchain_context.depth_image_view = VK_NULL_HANDLE;
    swapchain_context.depth_memory = VK_NULL_HANDLE;

    // --- Swapchain ---
    if (swapchain_context.swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(instance_context.device, swapchain_context.swapchain, nullptr);
        swapchain_context.swapchain = VK_NULL_HANDLE;
    }

    // --- Per-image sync (ImageContext) ---
    for (auto& img : images)
    {
        if (img.render_finished != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(instance_context.device, img.render_finished, nullptr);
            img.render_finished = VK_NULL_HANDLE;
        }

        img.in_flight = VK_NULL_HANDLE;
        img.image = VK_NULL_HANDLE;
        img.image_view = VK_NULL_HANDLE;
        img.framebuffer = VK_NULL_HANDLE;
    }

    images.clear();
}

void VkRenderBackend::match_queue_families()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(instance_context.instance, &deviceCount, nullptr);

    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(instance_context.instance, &deviceCount, devices.data());
    
    for (auto device : devices) {
        auto q = vk::find_queue_families(device, instance_context.surface);
        if (q.complete()) {
            instance_context.physical_device = device;
            instance_context.queues = q;
            break;
        }
    }
    
    assert(instance_context.physical_device != VK_NULL_HANDLE);
    
    instance_context.queues_indices[0] = instance_context.queues.graphics;
    instance_context.queues_indices[1] = instance_context.queues.present;
}


void VkRenderBackend::create_render_finished_semaphores()
{
    VkSemaphoreCreateInfo sem_ci{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    for (auto& img : images)
    {
        // in case of recreate_swapchain
        if (img.render_finished != VK_NULL_HANDLE)
        {
            vkDestroySemaphore(instance_context.device, img.render_finished, nullptr);
        }

        VK_CHECK(
            vkCreateSemaphore(instance_context.device, &sem_ci, nullptr, &img.render_finished)
        );
    }
}

void VkRenderBackend::init(RBWindowHandle in_window)
{
    window = in_window.as<GLFWwindow*>();
    
    glfwSetWindowUserPointer(window, this);

    glfwSetFramebufferSizeCallback(window,
        [](GLFWwindow* win, int, int)
        {
            auto backend = static_cast<VkRenderBackend*>(
                glfwGetWindowUserPointer(win));
            backend->framebuffer_resized = true;
        });
    
    create_instance();
    
    glfwCreateWindowSurface(instance_context.instance, window, nullptr, &instance_context.surface);
    
    match_queue_families();
    create_device();
    create_descriptor_pool();
    create_frame_resources();          // <-- create camera_buffer + memory
    // for (uint32_t i = 0; i < vk::MAX_FRAMES_IN_FLIGHT; ++i)
    // {
    //     allocate_descriptor_sets(camera_layout,
    //                              DescriptorPoolType::Frame,
    //                              i);
    // }
    create_swapchain();
    create_render_finished_semaphores();
    create_depth_resources();
    create_render_pass();
    create_framebuffers();
    create_command_pool();
    // create_pipeline();
    create_frame_sync_objects();
}

RBCommandList VkRenderBackend::begin_commands(RBFrameHandle frame_handle)
{
    LogRB.Log<DisplayFn>("Begin commands");
    
    auto& frame = frame_schedule_context.frames[frame_handle];

    VK_CHECK(vkResetCommandBuffer(frame.cmd, 0));

    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    VK_CHECK(vkBeginCommandBuffer(frame.cmd, &begin));

    return RBCommandList{frame.cmd};
}

void VkRenderBackend::end_commands(RBCommandList cmd_list)
{
    LogRB.Log<DisplayFn>("End commands");
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    VK_CHECK(vkEndCommandBuffer(cmd));
}

void VkRenderBackend::begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index)
{
    LogRB.Log("begin_render_pass");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();

    VkClearValue clears[2]{};
    clears[0].color = {{0.1f, 0.1f, 0.3f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{
        VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO
    };
    rpbi.renderPass = swapchain_context.render_pass;
    rpbi.framebuffer = swapchain_context.framebuffers[framebuffer_index];
    rpbi.renderArea.extent = swapchain_context.extent;
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
}

void VkRenderBackend::end_render_pass(RBCommandList cmd_list)
{
    LogRB.Log("end_render_pass");
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdEndRenderPass(cmd);
}


void VkRenderBackend::bind_pipeline(RBCommandList cmd_list, RBPipelineHandle pipeline_handle)
{
    LogRB.Log("bind_pipeline");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    VkPipeline pipeline_vk = pipeline_handle.as<VkPipeline>();

    assert(pipeline_vk != VK_NULL_HANDLE);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_vk);
}

void VkRenderBackend::draw(RBCommandList cmd_list, uint32_t vertex_count)
{
    LogRB.Log("draw");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
}

void VkRenderBackend::update_camera_ubo(RBFrameHandle frame_handle, const Camera& camera)
{
    auto& frame = frame_schedule_context.frames[frame_handle];
    update_camera_ubo(frame, camera);
}

RBFramebufferId VkRenderBackend::acquire_next_image(RBFrameHandle frame_handle)
{
    LogRB.Log("acquire_next_image");
    auto& frame = frame_schedule_context.frames[frame_handle];

    uint32_t image_index = 0;
    VkResult res = vkAcquireNextImageKHR(
        instance_context.device,
        swapchain_context.swapchain,
        UINT64_MAX,
        frame.image_available,
        VK_NULL_HANDLE,
        &image_index
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreate_swapchain();
        return RBFramebufferId{ 0 };
    }
    VK_CHECK(res);

    current_image_index = image_index;

    auto& image = images[image_index];

    if (image.in_flight != VK_NULL_HANDLE) {
        vkWaitForFences(instance_context.device, 1, &image.in_flight, VK_TRUE, UINT64_MAX);
    }

    image.in_flight = frame.in_flight;

    return RBFramebufferId{ image_index };
}

void VkRenderBackend::submit_frame(RBFrameHandle frame_handle, RBCommandList cmd_list, RBFramebufferId framebuffer_id)
{
    LogRB.Log("submit_frame");
    
    auto& frame = frame_schedule_context.frames[frame_handle];
    auto& image = images[current_image_index];

    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();

    VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &image.render_finished;

    VK_CHECK(vkQueueSubmit(instance_context.graphics_queue, 1, &submit, frame.in_flight));

    // --- Present ---
    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &image.render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_context.swapchain;
    present.pImageIndices = &current_image_index;

    VkResult res = vkQueuePresentKHR(instance_context.present_queue, &present);
    if (res == VK_ERROR_OUT_OF_DATE_KHR || res == VK_SUBOPTIMAL_KHR || framebuffer_resized)
    {
        framebuffer_resized = false;
        recreate_swapchain();
    }
    else
    {
        VK_CHECK(res);
    }
}

RBPipelineHandle VkRenderBackend::create_pipeline(GraphicsPipelineDesc desc)
{
    std::unique_ptr<VkPipelineObject> pipeline = std::make_unique<VkPipelineObject>(
        instance_context, swapchain_context, desc, *this);
    RBPipelineHandle handle = pipeline->get_handle();
    
    pipelines.insert({handle, std::move(pipeline)});
    
    return handle;
}

DescriptorSetLayoutData VkRenderBackend::get_vk_descriptor_set_layout(const RBDescriptorSetLayout& rb_handle)
{
    return descriptor_set_layouts[rb_handle];
}

RBDescriptorSet VkRenderBackend::get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, DescriptorPoolType pool_type)
{
    if (pool_type == DescriptorPoolType::Frame)
        return frame_schedule_context.frames[frame_schedule_context.current_frame].descriptors[rb_descriptor_set_layout];
    return persistent_descriptors[rb_descriptor_set_layout];
}

void VkRenderBackend::update_descriptor_set_data_impl(RBDescriptorSetLayout layout, void* buffer, size_t buffer_size)
{
    auto& frame = frame_schedule_context.frames[frame_schedule_context.current_frame];
    auto it = frame.descriptors.find(layout);
    if (it == frame.descriptors.end()) {
        throw std::runtime_error("Descriptor set for layout not allocated");
    }

    VkDescriptorSet set = it->second.as<VkDescriptorSet>();

    // TODO: for now only uniform buffers
    VkDescriptorBufferInfo buffer_info{};
    buffer_info.buffer = frame.camera_buffer; // TODO hardcoded, different ubos
    buffer_info.offset = 0;
    buffer_info.range = buffer_size;

    void* mapped = nullptr;
    VK_CHECK(vkMapMemory(instance_context.device, frame.camera_memory, 0, buffer_size, 0, &mapped));
    std::memcpy(mapped, buffer, buffer_size);
    vkUnmapMemory(instance_context.device, frame.camera_memory);

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = set;
    write.dstBinding = 0; // todo: hardcode
    write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    write.descriptorCount = 1;
    write.pBufferInfo = &buffer_info;

    vkUpdateDescriptorSets(instance_context.device, 1, &write, 0, nullptr);
}


void VkRenderBackend::create_frame_sync_objects()
{
    LogRB.Log("submit_frame");
    
    VkSemaphoreCreateInfo sem_ci{ VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO };
    VkFenceCreateInfo fence_ci{ VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : frame_schedule_context.frames)
    {
        VK_CHECK(vkCreateSemaphore(instance_context.device, &sem_ci, nullptr, &frame.image_available));
        VK_CHECK(vkCreateFence(instance_context.device, &fence_ci, nullptr, &frame.in_flight));
    }
}

