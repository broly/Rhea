#include "vk_render_backend.h"

#include <cassert>
#include <set>
#include <GLFW/glfw3.h>
#include <algorithm>
#include <span>

#include "vk_helpers.h"
#include "vk_macro.h"
#include "vk_pipeline.h"
#include "logging/log.h"

DEFINE_LOGGER(LogRB, Log);

constexpr const char* VALIDATION_LAYERS[] = {
    "VK_LAYER_KHRONOS_validation"
};


void VkRenderBackend::update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data)
{
    auto& buf = get_buffer(buffer_handle, frame_schedule_context.current_frame);
    
    memcpy(buf.mapped_ptr, data, size);
    
}

void VkRenderBackend::bind_buffer_to_descriptor(
    RBDescriptorSetLayout layout,
    uint32_t binding,
    RBBufferHandle buffer)
{
    auto usage = buffer.get_usage_type();

    if (usage == ResourceUsageType::Frame)
    {
        for (uint32_t frame = 0; frame < vk::MAX_FRAMES_IN_FLIGHT; ++frame)
        {
            auto& buf = frame_schedule_context.ubos
                .at(buffer.get_identifier())[frame];

            VkDescriptorBufferInfo info{
                .buffer = buf.buffer,
                .offset = 0,
                .range  = VK_WHOLE_SIZE
            };

            VkWriteDescriptorSet write{
                .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
                .dstSet = frame_schedule_context.frames[frame]
                    .descriptors.at(layout),
                .dstBinding = binding,
                .descriptorCount = 1,
                .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                .pBufferInfo = &info
            };

            vkUpdateDescriptorSets(instance_context.device, 1, &write, 0, nullptr);
        }
    }
    else // Persistent
    {
        auto& buf = swapchain_context.ubos
            .at(buffer.get_identifier());

        VkDescriptorBufferInfo info{
            .buffer = buf.buffer,
            .offset = 0,
            .range  = VK_WHOLE_SIZE
        };

        VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = persistent_descriptors.at(layout),
            .dstBinding = binding,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
            .pBufferInfo = &info
        };

        vkUpdateDescriptorSets(instance_context.device, 1, &write, 0, nullptr);
    }
}

RBSwapchainExtent VkRenderBackend::get_swapchain_extent() const
{
    return RBSwapchainExtent{swapchain_context.extent.width, swapchain_context.extent.height};
}


vk::BufferInfo& VkRenderBackend::get_buffer(RBBufferHandle buffer_handle, size_t frame_index)
{
    if (buffer_handle.get_usage_type() == ResourceUsageType::Frame)
    {
        size_t index = buffer_handle.get_identifier();
        return frame_schedule_context.ubos.at(index).at(frame_index);
    }
    return swapchain_context.ubos[buffer_handle.get_identifier()];
}

RenderPassDesc VkRenderBackend::make_render_pass_desc(const FramebufferDesc& fb) const
{
    RenderPassDesc rp{};

    for (auto img : fb.color_attachments)
    {
        VkFormat format = get_image_format(img);
        rp.color_formats.push_back(format);
    }

    if (fb.depth_attachment)
    {
        auto format = get_image_format(*fb.depth_attachment);
        rp.depth_format = format;
    }

    return rp;
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
    ResourceUsageType usage_type)
{
    auto& layout_data = descriptor_set_layouts.at(layout_handle);
    
    if (usage_type == ResourceUsageType::Frame)
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

void VkRenderBackend::get_or_create_mesh_buffers(MeshHandle handle)
{
    if (mesh_map.contains(handle))
        return;
    
    MeshGPUData data{};
    auto& mesh = handle.get();

    data.index_count = static_cast<uint32_t>(mesh.indices.size());

    vk::create_buffer(
        instance_context.device,
        instance_context.physical_device,
        mesh.vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.vertex_buffer,
        data.vertex_memory
    );

    void* mapped_data = nullptr;
    vkMapMemory(
        instance_context.device,
        data.vertex_memory,
        0,
        mesh.vertices.size() * sizeof(Vertex),
        0,
        &mapped_data
    );
    memcpy(mapped_data, mesh.vertices.data(),
        mesh.vertices.size() * sizeof(Vertex));
    vkUnmapMemory(instance_context.device, data.vertex_memory);

    vk::create_buffer(
        instance_context.device,
        instance_context.physical_device,
        mesh.indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.index_buffer,
        data.index_memory
    );

    vkMapMemory(
        instance_context.device,
        data.index_memory,
        0,
        mesh.indices.size() * sizeof(uint32_t),
        0,
        &mapped_data
    );
    memcpy(mapped_data, mesh.indices.data(),
        mesh.indices.size() * sizeof(uint32_t));
    vkUnmapMemory(instance_context.device, data.index_memory);

    mesh_map[handle] = data;
}

VkFormat get_vk_format(RGTextureFormat format)
{
    switch (format)
    {
    case RGTextureFormat::RGBA8_UNORM:
        return VK_FORMAT_B8G8R8A8_UNORM;
    case RGTextureFormat::RGBA8_SRGB:
        return VK_FORMAT_B8G8R8A8_SRGB;
    case RGTextureFormat::Undefined:
    case RGTextureFormat::RGBA16F:
    case RGTextureFormat::RGBA32F:
    case RGTextureFormat::Depth24Stencil8:
    case RGTextureFormat::Depth32F:
    default:
        
        throw std::runtime_error("Unsupported texture format");
    }
}

RGTextureFormat VkRenderBackend::get_swapchain_format() const
{
    switch (swapchain_context.surface_format.format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return RGTextureFormat::RGBA8_UNORM;

    case VK_FORMAT_B8G8R8A8_SRGB:
        return RGTextureFormat::RGBA8_SRGB;

    case VK_FORMAT_R8G8B8A8_UNORM:
        return RGTextureFormat::RGBA8_UNORM;

    case VK_FORMAT_R8G8B8A8_SRGB:
        return RGTextureFormat::RGBA8_SRGB;

    default:
        throw std::runtime_error("Unsupported swapchain format");
    }
}

RBImageHandle VkRenderBackend::create_image(const RBImageDesc& desc)
{
    vk::ImageResource res{};
    res.width  = desc.width;
    res.height = desc.height;
    res.format = get_vk_format(desc.format);

    VkImageUsageFlags vk_usage = 0;

    if (desc.usage & RenderTextureUsage::ColorAttachment)
        vk_usage |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

    if (desc.usage & RenderTextureUsage::DepthStencil)
        vk_usage |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;

    if (desc.usage & RenderTextureUsage::Sampled)
        vk_usage |= VK_IMAGE_USAGE_SAMPLED_BIT;

    VkImageCreateInfo image_info{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_info.imageType = VK_IMAGE_TYPE_2D;
    image_info.extent = { desc.width, desc.height, 1 };
    image_info.mipLevels = 1;
    image_info.arrayLayers = 1;
    image_info.format = get_vk_format(desc.format);
    image_info.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_info.usage = vk_usage;
    image_info.samples = VK_SAMPLE_COUNT_1_BIT;
    image_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(instance_context.device, &image_info, nullptr, &res.image));

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(instance_context.device, res.image, &mem_req);

    VkMemoryAllocateInfo alloc_info{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc_info.allocationSize = mem_req.size;
    alloc_info.memoryTypeIndex =
        vk::find_memory_type(
            instance_context.physical_device,
            mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(vkAllocateMemory(instance_context.device, &alloc_info, nullptr, &res.memory));
    VK_CHECK(vkBindImageMemory(instance_context.device, res.image, res.memory, 0));

    // ---- Image View ----
    VkImageAspectFlags aspect =
        (desc.usage & RenderTextureUsage::DepthStencil)
            ? VK_IMAGE_ASPECT_DEPTH_BIT
            : VK_IMAGE_ASPECT_COLOR_BIT;

    VkImageViewCreateInfo view_info{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_info.image = res.image;
    view_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_info.format = get_vk_format(desc.format);
    view_info.subresourceRange = {
        aspect, 0, 1, 0, 1
    };

    VK_CHECK(vkCreateImageView(instance_context.device, &view_info, nullptr, &res.view));

    uint32_t id = static_cast<uint32_t>(image_resources.size());
    image_resources.push_back(res);

    return RBImageHandle{ id };
}

RBImageView VkRenderBackend::get_image_view(RBImageHandle handle, RBFrameHandle frame_handle)
{
    return image_resources[handle.id].view;
}

size_t hash_framebuffer(
    const std::vector<VkImageView>& attachments,
    uint32_t width,
    uint32_t height,
    VkRenderPass render_pass)
{
    size_t h = std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(render_pass));
    h ^= std::hash<uint32_t>{}(width)  + 0x9e3779b9 + (h << 6) + (h >> 2);
    h ^= std::hash<uint32_t>{}(height) + 0x9e3779b9 + (h << 6) + (h >> 2);

    for (auto view : attachments)
    {
        size_t v = std::hash<uint64_t>{}(reinterpret_cast<uint64_t>(view));
        h ^= v + 0x9e3779b9 + (h << 6) + (h >> 2);
    }

    return h;
}


RBImageView VkRenderBackend::resolve_image_view(const RGTexture& tex, RBFrameHandle frame)
{
    if (tex.desc.external)
    {
        // swapchain
        return swapchain_context.image_views[current_image_index];
    }

    return image_resources[tex.image.value().id].view;
}

RBImageView VkRenderBackend::get_swapchain_image_view(RBFrameHandle frame)
{
    return swapchain_context.image_views[current_image_index];
}

RBImageHandle VkRenderBackend::get_swapchain_image(RBFrameHandle frame) const
{
    return swapchain_image_handles[current_image_index];
}

RBRenderPass VkRenderBackend::get_or_create_render_pass(const FramebufferDesc& fb)
{
    RenderPassDesc desc{};

    for (RBImageHandle image : fb.color_attachments)
    {
        desc.color_formats.push_back(get_image_format(image));
    }

    if (fb.depth_attachment)
    {
        desc.has_depth = true;
        desc.depth_format = get_image_format(*fb.depth_attachment);
    }

    auto it = render_pass_cache.find(desc);
    if (it != render_pass_cache.end())
        return it->second;

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;

    uint32_t index = 0;
    for (VkFormat fmt : desc.color_formats)
    {
        attachments.push_back({
            .format = fmt,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        });

        color_refs.push_back({
            .attachment = index++,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        });
    }

    VkAttachmentReference depth_ref{};
    if (desc.has_depth)
    {
        attachments.push_back({
            .format = desc.depth_format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
            .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        });

        depth_ref = {
            .attachment = index,
            .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL
        };
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = uint32_t(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    if (desc.has_depth)
        subpass.pDepthStencilAttachment = &depth_ref;

    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = uint32_t(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;

    VkRenderPass rp;
    VK_CHECK(vkCreateRenderPass(instance_context.device, &rpci, nullptr, &rp));

    render_pass_cache[desc] = rp;
    return rp;
}

VkFormat VkRenderBackend::get_image_format(RBImageHandle handle) const
{
    return image_resources[handle.id].format;
}

RBPipelineHandle VkRenderBackend::get_or_create_pipeline(RBPipelineHandle handle, VkRenderPass render_pass)
{
    auto& obj = *pipelines[handle];

    return obj.get_or_create_pipeline(render_pass);
    
}


void VkRenderBackend::bind_mesh(const RBCommandList& cmd, MeshHandle mesh)
{
    VkBuffer vertex_buffers = mesh_map[mesh].vertex_buffer;
    VkBuffer index_buffer = mesh_map[mesh].index_buffer;
    VkDeviceSize offsets[] = { 0 };
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertex_buffers, offsets);
    vkCmdBindIndexBuffer(cmd, index_buffer, 0, VK_INDEX_TYPE_UINT32);
}

void VkRenderBackend::push_constants(const RBCommandList& cmd, glm::mat4 matrix, RBPipelineHandle pipeline_handle)
{
    vkCmdPushConstants(
        cmd,
        pipelines[pipeline_handle]->get_pipeline_layout(),
        VK_SHADER_STAGE_VERTEX_BIT, 
        0, 
        sizeof(glm::mat4),
        &matrix
    );
}

void VkRenderBackend::draw_indexed(const RBCommandList& cmd, uint32_t index_count)
{
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
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
    
    swapchain_image_handles.clear();
    swapchain_image_handles.resize(sc_image_count);
    

    for (uint32_t i = 0; i < sc_image_count; ++i)
    {
        VkImageView view;

        VkImageViewCreateInfo ivci{
            VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
        };
        ivci.image = swapchain_images[i];
        ivci.viewType = VK_IMAGE_VIEW_TYPE_2D;
        ivci.format = swapchain_context.surface_format.format;
        ivci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        ivci.subresourceRange.levelCount = 1;
        ivci.subresourceRange.layerCount = 1;

        VK_CHECK(vkCreateImageView(
            instance_context.device,
            &ivci,
            nullptr,
            &view));

        vk::ImageResource res{};
        res.image  = swapchain_images[i];                 // VkImage
        res.view   = view;                                // VkImageView
        res.format = swapchain_context.surface_format.format;
        res.width  = swapchain_context.extent.width;
        res.height = swapchain_context.extent.height;

        uint32_t id = static_cast<uint32_t>(image_resources.size());
        image_resources.push_back(res);

        swapchain_image_handles[i] = RBImageHandle{ id };
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
    // for (uint32_t i = 0; i < vk::MAX_FRAMES_IN_FLIGHT; ++i)
    // {
    //     allocate_descriptor_sets(camera_layout,
    //                              DescriptorPoolType::Frame,
    //                              i);
    // }
    create_swapchain();
    create_depth_resources();
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
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    const auto& fb = framebuffer_resources[framebuffer_index.as<uint64_t>()];

    VkClearValue clears[2]{};
    clears[0].color = {{0.1f, 0.1f, 0.3f, 1.0f}};
    clears[1].depthStencil = {1.0f, 0};

    VkRenderPassBeginInfo rpbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass  = fb.render_pass;
    rpbi.framebuffer = fb.framebuffer;
    rpbi.renderArea.extent = { fb.width, fb.height };
    rpbi.clearValueCount = 2;
    rpbi.pClearValues = clears;

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    
    current_render_pass = fb.render_pass;
}

RBFramebufferId VkRenderBackend::get_or_create_framebuffer(const FramebufferDesc& desc)
{
    VkRenderPass rp = get_or_create_render_pass(desc);

    std::vector<VkImageView> attachments;

    for (RBImageHandle img : desc.color_attachments)
        attachments.push_back(image_resources[img.id].view);

    if (desc.depth_attachment)
        attachments.push_back(image_resources[desc.depth_attachment->id].view);

    VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = rp;
    info.attachmentCount = uint32_t(attachments.size());
    info.pAttachments = attachments.data();
    info.width  = desc.width;
    info.height = desc.height;
    info.layers = 1;

    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(instance_context.device, &info, nullptr, &fb));

    uint32_t id = framebuffer_resources.size();
    framebuffer_resources.push_back({
        .framebuffer = fb,
        .render_pass = rp,
        .width = desc.width,
        .height = desc.height
    });

    return RBFramebufferId{ (uint64_t)id };
}


void VkRenderBackend::end_render_pass(RBCommandList cmd_list)
{
    LogRB.Log("end_render_pass");
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdEndRenderPass(cmd);
    current_render_pass = VK_NULL_HANDLE;
}


void VkRenderBackend::bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object)
{
    LogRB.Log("bind_pipeline");
    
    auto vk_pipeline_object = static_cast<VkPipelineObject*>(pipeline_object);
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    assert(current_render_pass != VK_NULL_HANDLE);
    VkPipeline pipeline_vk = vk_pipeline_object->get_or_create_pipeline(current_render_pass);
    RBPipelineHandle handle = pipeline_vk;
    
    for (auto it = pending_pipelines.begin(); it != pending_pipelines.end(); ++it)
    {
        if (it->get() == pipeline_object)
        {
            std::unique_ptr<VkPipelineObject> pipeline_ptr = std::move(*it);
            pending_pipelines.erase(it);
            pipelines.insert({handle, std::move(pipeline_ptr)});
            break;
        }
    }

    assert(pipeline_vk != VK_NULL_HANDLE);
    
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_vk);
}

void VkRenderBackend::draw(RBCommandList cmd_list, uint32_t vertex_count)
{
    LogRB.Log("draw");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
}

void VkRenderBackend::acquire_next_image(RBFrameHandle frame_handle)
{
    auto& frame = frame_schedule_context.frames[frame_handle];

    vkWaitForFences(
        instance_context.device,
        1,
        &frame.in_flight,
        VK_TRUE,
        UINT64_MAX
    );

    vkResetFences(
        instance_context.device,
        1,
        &frame.in_flight
    );

    VkResult res = vkAcquireNextImageKHR(
        instance_context.device,
        swapchain_context.swapchain,
        UINT64_MAX,
        frame.image_available,
        VK_NULL_HANDLE,
        &current_image_index
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR)
    {
        recreate_swapchain();
        return;
    }

    VK_CHECK(res);
}


void VkRenderBackend::submit_frame(RBFrameHandle frame_handle,
                                  RBCommandList cmd_list)
{
    auto& frame = frame_schedule_context.frames[frame_handle];

    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();

    VkPipelineStageFlags wait_stage =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSubmitInfo submit{ VK_STRUCTURE_TYPE_SUBMIT_INFO };
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &frame.image_available;
    submit.pWaitDstStageMask = &wait_stage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &cmd;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &frame.render_finished;

    VK_CHECK(vkQueueSubmit(
        instance_context.graphics_queue,
        1,
        &submit,
        frame.in_flight
    ));

    VkPresentInfoKHR present{ VK_STRUCTURE_TYPE_PRESENT_INFO_KHR };
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &frame.render_finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_context.swapchain;
    present.pImageIndices = &current_image_index;

    VkResult res = vkQueuePresentKHR(
        instance_context.present_queue,
        &present
    );

    if (res == VK_ERROR_OUT_OF_DATE_KHR ||
        res == VK_SUBOPTIMAL_KHR ||
        framebuffer_resized)
    {
        framebuffer_resized = false;
        recreate_swapchain();
    }
    else
    {
        VK_CHECK(res);
    }
}


PipelineObject* VkRenderBackend::create_pipeline(GraphicsPipelineDesc desc)
{
    std::unique_ptr<VkPipelineObject> pipeline = std::make_unique<VkPipelineObject>(desc, *this);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

DescriptorSetLayoutData VkRenderBackend::get_vk_descriptor_set_layout(const RBDescriptorSetLayout& rb_handle)
{
    return descriptor_set_layouts[rb_handle];
}

RBDescriptorSet VkRenderBackend::get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, ResourceUsageType pool_type)
{
    if (pool_type == ResourceUsageType::Frame)
        return frame_schedule_context.frames[frame_schedule_context.current_frame].descriptors[rb_descriptor_set_layout];
    return persistent_descriptors[rb_descriptor_set_layout];
}

RBBufferHandle VkRenderBackend::create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type)
{
    if (usage_type == ResourceUsageType::Frame)
    {
        frame_schedule_context.ubos_counter++;
        
        const RBBufferHandle handle {frame_schedule_context.ubos_counter, ResourceUsageType::Frame};
        std::array<vk::BufferInfo, vk::MAX_FRAMES_IN_FLIGHT> buffers;
        for (int32_t index = 0; auto& frame : frame_schedule_context.frames)
        {
            vk::create_buffer(
                instance_context.device,
                instance_context.physical_device,
                buffer_size,
                VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
                VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                buffers[index].buffer,
                buffers[index].memory);
            vkMapMemory(instance_context.device, buffers[index].memory, 0, VK_WHOLE_SIZE, 0, &buffers[index].mapped_ptr);
            index++;
        }
        frame_schedule_context.ubos.emplace(handle.get_identifier(), buffers);
        
        return handle;
    } else
    {
        swapchain_context.ubo_counter++;
        const RBBufferHandle handle {swapchain_context.ubo_counter, ResourceUsageType::Persistent};
        
        vk::BufferInfo buffer_info;
        
        
        vk::create_buffer(
            instance_context.device,
            instance_context.physical_device,
            buffer_size,
            VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            buffer_info.buffer,
            buffer_info.memory);
        vkMapMemory(instance_context.device, buffer_info.memory, 0, VK_WHOLE_SIZE, 0, &buffer_info.mapped_ptr);
        
        swapchain_context.ubos[handle.get_identifier()] = buffer_info;
        return handle;
    }
}


void VkRenderBackend::create_frame_sync_objects()
{
    VkSemaphoreCreateInfo sem_ci{
        VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO
    };

    VkFenceCreateInfo fence_ci{
        VK_STRUCTURE_TYPE_FENCE_CREATE_INFO
    };
    fence_ci.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (auto& frame : frame_schedule_context.frames)
    {
        VK_CHECK(vkCreateSemaphore(
            instance_context.device,
            &sem_ci,
            nullptr,
            &frame.image_available
        ));

        VK_CHECK(vkCreateSemaphore(
            instance_context.device,
            &sem_ci,
            nullptr,
            &frame.render_finished
        ));

        VK_CHECK(vkCreateFence(
            instance_context.device,
            &fence_ci,
            nullptr,
            &frame.in_flight
        ));
    }
}