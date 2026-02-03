module vk:render_backend;

import <cassert>;
import <set>;
import <GLFW/glfw3.h>;
import <algorithm>;
import <span>;

#include "vk_macro.h"
#include "profiling/profile.h"

import render;

import :helpers;
import :log;
import :pipeline;
import profile;
import reflect;



void VkRenderBackend::update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame)
{
    resource_manager.update_uniform_buffer(buffer_handle, size, data, frame);
}


RBSwapchainExtent VkRenderBackend::get_swapchain_extent() const
{
    return swapchain.get_extent();
}



void VkRenderBackend::transition_image(
        RBCommandList cmd,
        RBImageHandle image,
        RBImageLayout before,
        RBImageLayout after)
{
    image_manager.transition_image(cmd, image, before, after);
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

void VkRenderBackend::update_sampled_image(RBDescriptorSet set, uint32_t binding, RBImageHandle image,
    ResourceUsageType usage, std::optional<RBSampler> sampler)
{
    // VkDescriptorSet set = get_descriptor_set(layout, usage);


    VkDescriptorImageInfo info{};
    info.imageView   = get_image_view(image);
    info.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    info.sampler     = sampler.has_value() ? VkSampler(*sampler) : sampler_manager.get_default_sampler();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &info;

    vkUpdateDescriptorSets(instance.get_device(), 1, &write, 0, nullptr);
}


void VkRenderBackend::create_depth_resources()
{
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    swapchain.depth_format = depth_format;

    VkImageCreateInfo image_ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.extent = {
        swapchain.extent.width,
        swapchain.extent.height,
        1
    };
    image_ci.mipLevels = 1;
    image_ci.arrayLayers = 1;
    image_ci.format = depth_format;
    image_ci.tiling = VK_IMAGE_TILING_OPTIMAL;
    image_ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    image_ci.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    image_ci.samples = VK_SAMPLE_COUNT_1_BIT;
    image_ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VK_CHECK(vkCreateImage(
        instance.get_device(), &image_ci, nullptr, &swapchain.depth_image
    ));
    

    VkMemoryRequirements mem_req;
    vkGetImageMemoryRequirements(
        instance.get_device(), swapchain.depth_image, &mem_req
    );

    VkMemoryAllocateInfo alloc{ VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    alloc.allocationSize = mem_req.size;
    alloc.memoryTypeIndex =
        vk::find_memory_type(
            instance.get_physical_device(),
            mem_req.memoryTypeBits,
            VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT
        );

    VK_CHECK(vkAllocateMemory(
        instance.get_device(), &alloc, nullptr, &swapchain.depth_memory
    ));

    VK_CHECK(vkBindImageMemory(
        instance.get_device(),
        swapchain.depth_image,
        swapchain.depth_memory,
        0
    ));

    VkImageViewCreateInfo view_ci{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view_ci.image = swapchain.depth_image;
    view_ci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view_ci.format = depth_format;
    view_ci.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    view_ci.subresourceRange.baseMipLevel = 0;
    view_ci.subresourceRange.levelCount = 1;
    view_ci.subresourceRange.baseArrayLayer = 0;
    view_ci.subresourceRange.layerCount = 1;

    VK_CHECK(vkCreateImageView(
        instance.get_device(),
        &view_ci,
        nullptr,
        &swapchain.depth_image_view
    ));
    
    LogVkSamplerManager.Log("Created depth image: %p. View: %p", 
        swapchain.depth_image, swapchain.depth_image_view);
}

VkImageSubresourceRange VkRenderBackend::full_subresource_range(RBImageHandle image)
{
    return image_manager.full_subresource_range(image);
}


void VkRenderBackend::destroy_depth_resources()
{
    VkDevice device = instance.get_device();

    if (swapchain.depth_image_view != VK_NULL_HANDLE)
    {
        vkDestroyImageView(device, swapchain.depth_image_view, nullptr);
        swapchain.depth_image_view = VK_NULL_HANDLE;
    }

    if (swapchain.depth_image != VK_NULL_HANDLE)
    {
        vkDestroyImage(device, swapchain.depth_image, nullptr);
        swapchain.depth_image = VK_NULL_HANDLE;
    }

    if (swapchain.depth_memory != VK_NULL_HANDLE)
    {
        vkFreeMemory(device, swapchain.depth_memory, nullptr);
        swapchain.depth_memory = VK_NULL_HANDLE;
    }
}


void VkRenderBackend::update_viewport_extent(const RBCommandList& cmd)
{
    RBSwapchainExtent extent = swapchain.get_extent();

    VkViewport viewport;
    viewport.x = 0;
    viewport.y = 0;
    viewport.width  = (float)extent.width;
    viewport.height = (float)extent.height;
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor;
    scissor.offset = {0, 0};
    scissor.extent = {extent.width, extent.height};

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor (cmd, 0, 1, &scissor);
}


void VkRenderBackend::update_viewport(const RBCommandList& cmd, RBSwapchainExtent extent)
{
    VkViewport viewport{};
    viewport.x = 0;
    viewport.y = 0;
    viewport.width  = float(extent.width);
    viewport.height = float(extent.height);
    viewport.minDepth = 0.0f;
    viewport.maxDepth = 1.0f;

    VkRect2D scissor{};
    scissor.offset = {0, 0};
    scissor.extent = {extent.width, extent.height};

    vkCmdSetViewport(cmd, 0, 1, &viewport);
    vkCmdSetScissor (cmd, 0, 1, &scissor);
}



RBDescriptorSetLayout VkRenderBackend::create_descriptor_set_layout(const DescriptorSetLayoutDesc& descriptor_set_layout)
{
    return resource_manager.create_descriptor_set_layout(descriptor_set_layout);
}

void VkRenderBackend::create_descriptor_pool()
{
    resource_manager.create_descriptor_pool();
}

void VkRenderBackend::bind_descriptor_set(RBCommandList cmd_list, int set_index, RBDescriptorSet rb_descriptors, RBPipelineHandle pipeline_handle)
{
    auto& pipeline = pipelines[pipeline_handle];
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    VkDescriptorSet vk_set = rb_descriptors.as<VkDescriptorSet>();

    vkCmdBindDescriptorSets(
        cmd,
        VK_PIPELINE_BIND_POINT_GRAPHICS,
        pipeline->get_pipeline_layout(),
        set_index, 
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

void VkRenderBackend::get_or_create_mesh_buffers(MeshPrimHandle handle)
{
    PROFILE(__FUNCTION__);
    mesh_manager.get_or_create_mesh_buffers(handle);
}

TextureFormat VkRenderBackend::get_swapchain_format() const
{
    switch (swapchain.surface_format.format)
    {
    case VK_FORMAT_B8G8R8A8_UNORM:
        return TextureFormat::RGBA8_UNORM;

    case VK_FORMAT_B8G8R8A8_SRGB:
        return TextureFormat::RGBA8_SRGB;

    case VK_FORMAT_R8G8B8A8_UNORM:
        return TextureFormat::RGBA8_UNORM;

    case VK_FORMAT_R8G8B8A8_SRGB:
        return TextureFormat::RGBA8_SRGB;

    default:
        throw std::runtime_error("Unsupported swapchain format");
    }
}

RBImageHandle VkRenderBackend::create_image(const RBImageDesc& desc)
{
    return image_manager.create_image(desc);
}

void VkRenderBackend::destroy_image(RBImageHandle handle, bool wait_fences)
{
    return image_manager.destroy_image(handle, wait_fences);
}

RBImageView VkRenderBackend::get_image_view(RBImageHandle handle)
{
    return image_manager.get_image_view(handle);
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


RBImageHandle VkRenderBackend::get_swapchain_image(std::optional<RBFrameHandle> frame_handle) const
{
    // frame_handle is unused yet
    return swapchain.get_image();
}

RBRenderPass VkRenderBackend::get_or_create_render_pass(const FramebufferDesc& fb)
{
    RenderPassDesc desc{};
    desc.name = fb.pass;
    for (auto& attachment : fb.color_attachments)
    {
        desc.color_attachments.push_back(
            RenderPassAttachmentInfo{
                get_image_format(attachment.image), 
                attachment.load, 
                attachment.store, 
                attachment.usage
            });
    }

    if (fb.depth_attachment)
    {
        auto attachment = *fb.depth_attachment;
        desc.depth_attachment.emplace(RenderPassAttachmentInfo{});
        desc.depth_attachment->format = get_image_format(attachment.image);
        desc.depth_attachment->load_op = fb.depth_attachment->load;
        desc.depth_attachment->store_op = fb.depth_attachment->store;
        desc.depth_attachment->usage = attachment.usage;
    }

    auto it = render_pass_cache.find(desc);
    if (it != render_pass_cache.end())
        return it->second;
    
    
    LogRBRenderPass.Log("get_or_create_render_pass (CREATE)");
    for (auto attachment : fb.color_attachments)
    {
        auto vk_img = image_manager.get_image_resource(attachment.image).image;
        LogRBRenderPass.Log(" * color_attachment (image: %s [%p]). Usage: %s (initial & final layouts), load_op: %s, store_op: %s",
            debug.get_vk_image_name(vk_img).to_string().c_str(),
            vk_img,
            reflect::enum_name(attachment.usage).to_string().c_str(),
            reflect::enum_name(attachment.load).to_string().c_str(),
            reflect::enum_name(attachment.store).to_string().c_str());
    }
    if (fb.depth_attachment)
    {
        auto& attachment = *fb.depth_attachment;
        auto vk_img = image_manager.get_image_resource(attachment.image).image;
        
        LogRBRenderPass.Log(" * depth_attachment (image: %s [%p}). Usage: %s (initial & final layouts), load_op: %s, store_op: %s",
            debug.get_vk_image_name(vk_img).to_string().c_str(),
            vk_img,
            reflect::enum_name(attachment.usage).to_string().c_str(),
            reflect::enum_name(attachment.load).to_string().c_str(),
            reflect::enum_name(attachment.store).to_string().c_str());
    }

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;

    uint32_t index = 0;
    
    
    for (auto& attachment : desc.color_attachments)
    {
        
        attachments.push_back({
            .format = attachment.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = vk::vk_convert_attachment_load(attachment.load_op),
            .storeOp = vk::vk_convert_attachment_store(attachment.store_op),
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = vk::to_attachment_layout(attachment.usage),
            .finalLayout   = vk::to_attachment_layout(attachment.usage),
        });

        color_refs.push_back({
            .attachment = index++,
            .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
        });
    }

    VkAttachmentReference depth_ref{};
    if (desc.depth_attachment.has_value())
    {
        auto& attachment = *desc.depth_attachment;
        
        
        VkImageLayout layout = vk::to_attachment_layout(attachment.usage);
        
        attachments.push_back({
            .format = attachment.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = vk::vk_convert_attachment_load(attachment.load_op),
            .storeOp = vk::vk_convert_attachment_store(attachment.store_op),
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = vk::to_attachment_layout(attachment.usage), // vk::to_attachment_layout(attachment.usage),
            .finalLayout   = vk::to_attachment_layout(attachment.usage) // vk::to_attachment_layout(attachment.usage),
        });


        depth_ref = {
            .attachment = index,
            .layout = layout
        };
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = uint32_t(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    if (desc.depth_attachment.has_value())
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
    return image_manager.get_image_format(handle);
}


RBImageHandle VkRenderBackend::create_texture_2d(const Texture& tex, const TextureCreationInfo& texture_creation_info)
{
    return image_manager.create_texture_2d(tex, texture_creation_info);
}

std::pair<uint32_t, uint32_t> VkRenderBackend::get_viewport_extent() const
{
    return {swapchain.extent.width, swapchain.extent.height};
}

RenderResource* VkRenderBackend::create_resource(const RenderResourceDesc& desc)
{
    std::unique_ptr<VkRenderResource> res = std::make_unique<VkRenderResource>(desc, resource_manager, *this);
    
    resources.push_back(std::move(res));
    return resources.back().get();
}


void VkRenderBackend::bind_mesh(const RBCommandList& cmd, MeshPrimHandle mesh, RBFrameHandle frame)
{
    PROFILE(__FUNCTION__);
    mesh_manager.bind(cmd, mesh);
}

void VkRenderBackend::push_constants_impl(const RBCommandList& cmd, const void* data, size_t size, PipelineObject* pipeline_object)
{
    PROFILE(__FUNCTION__);
    
    auto as_vk_pipeline = static_cast<VkPipelineObject*>(pipeline_object);
    vkCmdPushConstants(
        cmd,
        pipelines[as_vk_pipeline->get_pipeline_handle()]->get_pipeline_layout(),
        VK_SHADER_STAGE_VERTEX_BIT, 
        0, 
        size,
        data
    );
}

void VkRenderBackend::draw_indexed(const RBCommandList& cmd, uint32_t index_count, RBDrawParams params)
{
    PROFILE("draw_indexed");
    if (params.update_viewport_extent)
    {
        update_viewport(cmd, params.use_swapchain_extent ? get_swapchain_extent() : params.extent);
    }
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
    sampler_manager.init();


    create_descriptor_pool();

    swapchain.init();
    immediate_command_pool.init();
    
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

RBFramebufferId VkRenderBackend::get_or_create_framebuffer(const FramebufferDesc& desc)
{
    VkRenderPass rp = get_or_create_render_pass(desc);
    RBSwapchainExtent extent = swapchain.get_extent();
    return framebuffer_manager.get_or_create_framebuffer(desc, rp, extent);
}

void VkRenderBackend::begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index)
{
    LogRB.Log<DisplayFn>("begin_render_pass");
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    const auto& fb = framebuffer_manager.get_framebuffer_resource(framebuffer_index);

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


void VkRenderBackend::end_render_pass(RBCommandList cmd_list)
{
    LogRB.Log("end_render_pass");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdEndRenderPass(cmd);
    current_render_pass = VK_NULL_HANDLE;
}


void VkRenderBackend::bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object)
{
    LogRB.Log<Verbose>("bind_pipeline");
    
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
    const bool result = swapchain.acquire_next_image(frame_handle);
    if (!result)
    {
        framebuffer_manager.destroy_framebuffers();
        destroy_depth_resources();
        create_depth_resources();
    }
    return result;
}


void VkRenderBackend::submit_frame(RBFrameHandle frame_handle,
                                  RBCommandList cmd_list)
{
    swapchain.submit_frame(frame_handle, cmd_list);
}


PipelineObject* VkRenderBackend::create_pipeline(const GraphicsPipelineDesc& desc)
{
    std::unique_ptr<VkPipelineObject> pipeline = std::make_unique<VkPipelineObject>(instance, swapchain, resource_manager, desc);
    PipelineObject* result = pipeline.get();
    pending_pipelines.push_back(std::move(pipeline));
    return result;
}

vk::DescriptorSetLayoutData VkRenderBackend::get_vk_descriptor_set_layout(RBDescriptorSetLayout descriptor_set_layout)
{
    return resource_manager.get_vk_descriptor_set_layout(descriptor_set_layout);
}

RBBufferHandle VkRenderBackend::create_uniform_buffer(size_t buffer_size, ResourceUsageType usage_type)
{
    return resource_manager.create_uniform_buffer(buffer_size, usage_type);
}


void VkRenderBackend::create_frame_sync_objects()
{
    swapchain.create_sync_objects();
}

RBSampler VkRenderBackend::create_sampler(const ::SamplerDesc& desc)
{
    return sampler_manager.get_or_create(desc);
}
