module vk:render_backend;

import <cassert>;
import <set>;
import <GLFW/glfw3.h>;
import <algorithm>;
import <span>;

#include "vk_macro.h"

import render;

import :helpers;
import :log;
import :pipeline;



void VkRenderBackend::update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data)
{
    auto& buf = get_buffer(buffer_handle, swapchain.current_frame);
    
    memcpy(buf.mapped_ptr, data, size);
    
}

void VkRenderBackend::bind_buffer_to_descriptor(
    RBDescriptorSetLayout layout,
    uint32_t binding,
    RBBufferHandle buffer)
{
    resource_manager.bind_buffer_to_descriptor(layout, binding, buffer);
}

RBSwapchainExtent VkRenderBackend::get_swapchain_extent() const
{
    return swapchain.get_extent();
}


void VkRenderBackend::CRUTCH_transition_image(RBCommandList cmd, RBImageHandle image, 
    RGTextureFormat format, VkImageLayout old_layout,
    VkImageLayout new_layout)
{
    swapchain.CRUTCH_transition_image(cmd, image,  format,  old_layout, new_layout);
    
}


vk::BufferInfo& VkRenderBackend::get_buffer(RBBufferHandle buffer_handle, size_t frame_index)
{
    return resource_manager.get_buffer(buffer_handle, frame_index);
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


void VkRenderBackend::draw_fullscreen(RBCommandList cmd)
{
    vkCmdDraw(
        cmd.as<VkCommandBuffer>(),
        3, // fullscreen triangle
        1,
        0,
        0
    );
}

void VkRenderBackend::update_sampled_image(RBDescriptorSetLayout layout, uint32_t binding, RBImageHandle image,
    ResourceUsageType usage)
{
    VkDescriptorSet set = get_descriptor_set(layout, usage);


    VkDescriptorImageInfo info{};
    info.imageView   = get_image_view(image);
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.sampler     = texture_manager.get_default_sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;

    vkUpdateDescriptorSets(instance.get_device(), 1, &write, 0, nullptr);
}


void VkRenderBackend::recreate_swapchain()
{
    assert(false);
    // int w = 0, h = 0;
    // glfwGetFramebufferSize(window, &w, &h);
    // while (w == 0 || h == 0)
    // {
    //     glfwWaitEvents();
    //     glfwGetFramebufferSize(window, &w, &h);
    // }
    //
    // vkDeviceWaitIdle(instance.get_device());
    // cleanup_swapchain();
    // create_swapchain();
    // create_depth_resources();
}

void VkRenderBackend::create_depth_resources()
{
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    swapchain.depth_format = depth_format;

    VkImageCreateInfo image_ci{
        VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO
    };
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.extent.width  = swapchain.extent.width;
    image_ci.extent.height = swapchain.extent.height;
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
        vkCreateImage(instance.get_device(), &image_ci, nullptr, &swapchain.depth_image)
    );

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(instance.get_device(), swapchain.depth_image, &mem_req);

    VkMemoryAllocateInfo alloc{
        VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO
    };
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex = vk::find_memory_type(instance.get_physical_device(),mem_req.memoryTypeBits,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    VK_CHECK(
        vkAllocateMemory(instance.get_device(), &alloc, nullptr, &swapchain.depth_memory)
    );

    vkBindImageMemory(instance.get_device(), swapchain.depth_image, swapchain.depth_memory,0);

    VkImageViewCreateInfo view_ci{
        VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO
    };
    view_ci.image = swapchain.depth_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = depth_format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(
        instance.get_device(), &view_ci, nullptr,
        &swapchain.depth_image_view));
}


RBDescriptorSetLayout VkRenderBackend::create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout)
{
    return resource_manager.create_descriptor_set_layout(descriptor_set_layout);
}

void VkRenderBackend::create_descriptor_pool()
{
    resource_manager.create_descriptor_pool();
}



void VkRenderBackend::allocate_descriptor_sets_for_layout(
    RBDescriptorSetLayout layout_handle,
    ResourceUsageType usage_type)
{
    return resource_manager.allocate_descriptor_sets_for_layout(layout_handle, usage_type);
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
    return swapchain.current_frame;
}

void VkRenderBackend::wait_for_frame(RBFrameHandle frame_handle)
{
    swapchain.wait_for_frame(frame_handle);
}

void VkRenderBackend::reset_frame_fence(RBFrameHandle frame)
{
    swapchain.reset_frame_fence(frame);
}

void VkRenderBackend::advance_frame()
{
    swapchain.advance_frame();
}

void VkRenderBackend::get_or_create_mesh_buffers(MeshHandle handle)
{
    if (mesh_map.contains(handle))
        return;
    
    MeshGPUData data{};
    auto& mesh = handle.get();

    data.index_count = static_cast<uint32_t>(mesh.indices.size());

    vk::create_buffer(
        instance.get_device(),
        instance.get_physical_device(),
        mesh.vertices.size() * sizeof(Vertex),
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.vertex_buffer,
        data.vertex_memory
    );

    void* mapped_data = nullptr;
    vkMapMemory(
        instance.get_device(),
        data.vertex_memory,
        0,
        mesh.vertices.size() * sizeof(Vertex),
        0,
        &mapped_data
    );
    memcpy(mapped_data, mesh.vertices.data(),
        mesh.vertices.size() * sizeof(Vertex));
    vkUnmapMemory(instance.get_device(), data.vertex_memory);

    vk::create_buffer(
        instance.get_device(),
        instance.get_physical_device(),
        mesh.indices.size() * sizeof(uint32_t),
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        data.index_buffer,
        data.index_memory
    );

    vkMapMemory(
        instance.get_device(),
        data.index_memory,
        0,
        mesh.indices.size() * sizeof(uint32_t),
        0,
        &mapped_data
    );
    memcpy(mapped_data, mesh.indices.data(),
        mesh.indices.size() * sizeof(uint32_t));
    vkUnmapMemory(instance.get_device(), data.index_memory);

    mesh_map[handle] = data;
}

RGTextureFormat VkRenderBackend::get_swapchain_format() const
{
    switch (swapchain.surface_format.format)
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
    return swapchain.create_image(desc);
}

RBImageView VkRenderBackend::get_image_view(RBImageHandle handle)
{
    return swapchain.get_image_view(handle);
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
    return swapchain.resolve_image_view(tex, frame);
}

RBImageView VkRenderBackend::get_swapchain_image_view(RBFrameHandle frame)
{
    return swapchain.get_image_view();
}

RBImageHandle VkRenderBackend::get_swapchain_image() const
{
    return swapchain.get_image();
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
    VK_CHECK(vkCreateRenderPass(instance.get_device(), &rpci, nullptr, &rp));

    render_pass_cache[desc] = rp;
    return rp;
}

VkFormat VkRenderBackend::get_image_format(RBImageHandle handle) const
{
    return swapchain.get_image_format(handle);
}

RBPipelineHandle VkRenderBackend::get_or_create_pipeline(RBPipelineHandle handle, VkRenderPass render_pass)
{
    auto& obj = *pipelines[handle];

    return obj.get_or_create_pipeline(render_pass);
    
}

void VkRenderBackend::update_depth_descriptor(const RBDescriptorSet& rb_handle, RBImageHandle value,
    RGTextureFormat format)
{
    swapchain.update_depth_descriptior(rb_handle, value);
}

RBImageHandle VkRenderBackend::create_texture_2d(const Texture& data)
{
    assert(false);
    return {};
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


void VkRenderBackend::create_command_pool()
{
    if (command_context.command_pool != VK_NULL_HANDLE)
    {
        vkDestroyCommandPool(instance.get_device(), command_context.command_pool, nullptr);
    }
    
    vkGetDeviceQueue(instance.get_device(), instance.queues.graphics, 0, 
        &instance.graphics_queue);

    vkGetDeviceQueue(instance.get_device(), instance.queues.present, 0, 
        &instance.present_queue);

    VkCommandPoolCreateInfo cpci{
        VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO
    };
    cpci.queueFamilyIndex = instance.queues.graphics;
    cpci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

    VK_CHECK(vkCreateCommandPool(
        instance.get_device(),
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
        vkAllocateCommandBuffers(instance.get_device(), &cbai, cmds.data())
    );

    for (uint32_t i = 0; i < vk::MAX_FRAMES_IN_FLIGHT; ++i)
    {
        swapchain.frames[i].cmd = cmds[i];
    }
    
}
void VkRenderBackend::cleanup_swapchain()
{
    swapchain.cleanup();
}


VkRenderBackend::VkRenderBackend()
    : instance()
    , texture_manager(instance)
    , swapchain(instance, texture_manager)
    , resource_manager(instance.get_device(), instance.get_physical_device())
{
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
            backend->swapchain.framebuffer_resized = true;
        });
    
    
    instance.init(window);


    create_descriptor_pool();

    swapchain.init();
    
    create_depth_resources();
    create_command_pool();
    // create_pipeline();
    create_frame_sync_objects();
}

RBCommandList VkRenderBackend::begin_commands(RBFrameHandle frame_handle)
{
    LogRB.Log<DisplayFn>("Begin commands");
    
    auto& frame = swapchain.frames[frame_handle];

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
    LogRB.Log<DisplayFn>("begin_render_pass");
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
    
    uint32_t height = (desc.height > 0) ? desc.height : get_swapchain_extent().height;
    uint32_t width = (desc.width > 0) ? desc.width : get_swapchain_extent().width;
    
    
    assert(height > 0);
    assert(width > 0);

    std::vector<VkImageView> attachments;

    for (RBImageHandle img : desc.color_attachments)
        attachments.push_back(swapchain.image_resources[img.id].view);

    if (desc.depth_attachment)
        attachments.push_back(swapchain.image_resources[desc.depth_attachment->id].view);

    VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = rp;
    info.attachmentCount = uint32_t(attachments.size());
    info.pAttachments = attachments.data();
    info.width  = width;
    info.height = height;
    info.layers = 1;

    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(instance.get_device(), &info, nullptr, &fb));

    uint32_t id = framebuffer_resources.size();
    framebuffer_resources.push_back({
        .framebuffer = fb,
        .render_pass = rp,
        .width = width,
        .height = height
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

bool VkRenderBackend::acquire_next_image(RBFrameHandle frame_handle)
{
    return swapchain.acquire_next_image(frame_handle);
}


void VkRenderBackend::submit_frame(RBFrameHandle frame_handle,
                                  RBCommandList cmd_list)
{
    swapchain.submit_frame(frame_handle, cmd_list);
}


PipelineObject* VkRenderBackend::create_pipeline(GraphicsPipelineDesc desc)
{
    std::unique_ptr<VkPipelineObject> pipeline = std::make_unique<VkPipelineObject>(desc, instance, swapchain, resource_manager);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

vk::DescriptorSetLayoutData VkRenderBackend::get_vk_descriptor_set_layouta(RBDescriptorSetLayout descriptor_set_layout)
{
    return resource_manager.get_vk_descriptor_set_layout(descriptor_set_layout);
}

RBDescriptorSet VkRenderBackend::get_descriptor_set(RBDescriptorSetLayout rb_descriptor_set_layout, ResourceUsageType pool_type)
{
    return resource_manager.get_descriptor_set(rb_descriptor_set_layout, pool_type, swapchain.current_frame);
}

RBBufferHandle VkRenderBackend::create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type)
{
    return resource_manager.create_uniform_buffer(buffer_size, usage_type);
}


void VkRenderBackend::create_frame_sync_objects()
{
    swapchain.create_sync_objects();
}

RBSampler VkRenderBackend::create_sampler(const RBSamplerDesc& desc)
{
    VkSamplerCreateInfo info{ VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO };

    info.magFilter = desc.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
    info.minFilter = desc.linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;

    info.addressModeU =
    info.addressModeV =
    info.addressModeW = desc.clamp_to_edge
        ? VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE
        : VK_SAMPLER_ADDRESS_MODE_REPEAT;

    info.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;
    info.minLod = 0.0f;
    info.maxLod = 1.0f;

    VkSampler sampler;
    VK_CHECK(vkCreateSampler(instance.get_device(), &info, nullptr, &sampler));

    return RBSampler{ sampler };
}

void VkRenderBackend::bind_image_to_descriptor(RBDescriptorSetLayout layout, uint32_t binding, RBImageHandle image,
    RBSampler sampler)
{
    auto& img = swapchain.image_resources[image.id];

    VkDescriptorImageInfo img_info{};
    img_info.imageView = img.view;
    img_info.sampler   = sampler.as<VkSampler>();
    img_info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

    VkWriteDescriptorSet write{ VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
    write.dstSet = get_descriptor_set(layout, ResourceUsageType::Frame).as<VkDescriptorSet>();
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &img_info;

    vkUpdateDescriptorSets(instance.get_device(), 1, &write, 0, nullptr);
}
