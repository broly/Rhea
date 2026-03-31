module vk:render_backend;

import <cassert>;
import <set>;
import <GLFW/glfw3.h>;
import <algorithm>;
import <span>;

#include "vk_macro.h"
#include "common/assertion_macros.h"
#include "logging/log_macro.h"
#include "profiling/profile.h"

import render;

import :enums_to_string;
import :enums_adapters;
import :helpers;
import :log;
import :pipeline;
import :pipeline_raytrace;
import profile;
import reflect;
import :device_extension_api;

DEFINE_LOGGER(LogVkCommands, Display);

void VkRenderBackend::update_uniform_buffer_impl(RBBufferHandle buffer_handle, size_t size, void* data, RBFrameHandle frame)
{
    buffer_manager.update_any_buffer(buffer_handle, size, data, frame);
}

void VkRenderBackend::destroy_render_pass_cache()
{
    render_pass_cache.clear();
}


RBPipelineLayout VkRenderBackend::create_pipeline_layout(const PipelineLayoutDesc& desc)
{    
    return pipeline_manager.create_pipeline_layout(desc);
}


Extent VkRenderBackend::get_swapchain_extent() const
{
    return swapchain.get_extent();
}



void VkRenderBackend::transition_image(RBCommandList cmd, const ImageBarrierParams& params)
{
    image_manager.transition_image(cmd, params);
}


void VkRenderBackend::update_sampled_image(RBDescriptorSet set, uint32_t binding, RBImageHandle image,
                                           ResourceUsage usage, std::optional<RBSampler> sampler, uint32_t layer_index, bool cubemap, uint32_t array_index)
{
    // VkDescriptorSet set = get_descriptor_set(layout, usage);
    auto used_sampler = sampler.has_value() ? VkSampler(*sampler) : sampler_manager.get_default_sampler();
    auto image_view = cubemap ? get_cubemap_image_view(image) : get_image_view(image, layer_index);
    buffer_manager.update_sampled_image(set, binding, image_view, usage, used_sampler, array_index);
    
}

void VkRenderBackend::update_storage_image(RBDescriptorSet set, uint32_t binding, RBImageHandle image, uint32_t array_index)
{
    VkDescriptorImageInfo info{};
    info.imageView = get_image_view(image);
    info.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dstSet = set;
    write.dstBinding = binding;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    write.pImageInfo = &info;
    write.dstArrayElement = array_index;

    vkUpdateDescriptorSets(instance.get_device(), 1, &write, 0, nullptr);
}

void VkRenderBackend::update_tlas(RBDescriptorSet set, uint32_t binding, RBAccelStruct tlas)
{
    VkWriteDescriptorSetAccelerationStructureKHR as_info{};
    as_info.sType =
        VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET_ACCELERATION_STRUCTURE_KHR;

    as_info.accelerationStructureCount = 1;
    as_info.pAccelerationStructures = tlas.ptr<VkAccelerationStructureKHR>();

    VkWriteDescriptorSet write{};
    write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;

    write.dstSet = set;
    write.dstBinding = binding;

    write.descriptorCount = 1;
    write.descriptorType =
        VK_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;

    write.pNext = &as_info;

    vkUpdateDescriptorSets(instance.get_device(), 1, &write, 0, nullptr);
}


void VkRenderBackend::create_depth_resources()
{
    VkFormat depth_format = VK_FORMAT_D32_SFLOAT;
    swapchain.depth_format = depth_format;

    VkImageCreateInfo image_ci{ VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    image_ci.imageType = VK_IMAGE_TYPE_2D;
    image_ci.extent = {
        swapchain.vk_extent.width,
        swapchain.vk_extent.height,
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

void VkRenderBackend::compute(RBCommandList cmd, const ComputeWorkgroups& workgroups)
{
    auto [x, y, z] = workgroups;
    vkCmdDispatch(cmd.as<VkCommandBuffer>(), x, y, z);
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


void VkRenderBackend::update_viewport(const RBCommandList& cmd, Extent extent, bool use_swapchain_extent)
{
    PROFILE("VkRenderBackend::update_viewport");
    
    if (use_swapchain_extent)
        extent = swapchain.get_extent();
    
    static std::optional<Extent> current_viewport_extent = std::nullopt;
    
    if (current_viewport_extent == extent)
        return;
    
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

uint32_t VkRenderBackend::get_num_images_in_flight() const
{
    return vk::MAX_FRAMES_IN_FLIGHT;
}

RBDeviceAddress VkRenderBackend::get_buffer_device_address(RBBufferHandle buffer_handle, RBFrameHandle frame) const
{
    return buffer_manager.get_buffer_device_address(buffer_handle);
}

void VkRenderBackend::create_descriptor_pool()
{
    buffer_manager.create_descriptor_pool();
}

void VkRenderBackend::bind_descriptor_set(RBCommandList cmd_list, int set_index, RBDescriptorSet rb_descriptors, 
    Name debug_name)
{
    return pipeline_manager.bind_descriptor_set(cmd_list, set_index, rb_descriptors, debug_name);
}

RBFrameHandle VkRenderBackend::get_current_frame() const
{
    return swapchain.current_frame;
}

void VkRenderBackend::wait_for_frame(RBFrameHandle frame_handle)
{
    PROFILE("wait_for_frame");
    swapchain.wait_for_frame(frame_handle);
}

void VkRenderBackend::flush_frame_garbage(RBFrameHandle frame)
{
    immediate_command_pool.flush_garbage(frame);
}

void VkRenderBackend::reset_frame_fence(RBFrameHandle frame)
{
    swapchain.reset_frame_fence(frame);
}

void VkRenderBackend::advance_frame()
{
    swapchain.advance_frame();
}

void VkRenderBackend::copy_image_to_buffer(RBImageHandle img, std::vector<float>& buf,
    TextureFormat& format,Extent extent)
{
    image_manager.copy_image_to_buffer(img, buf, format, extent);
}

RBVertexBufferHandle VkRenderBackend::create_vertex_buffer(const VertexBufferDesc& desc)
{
    return vertex_buffer_manager.create_vertex_buffer(desc);
}

void* VkRenderBackend::get_vertex_buffer_ptr(RBVertexBufferHandle handle, RBFrameHandle frame)
{
    return vertex_buffer_manager.get_mapped_pointer(handle, frame);
}

void VkRenderBackend::bind_vertex_buffer(RBCommandList cmd, RBVertexBufferHandle handle, RBFrameHandle frame)
{
    return vertex_buffer_manager.bind_vertex_buffer(cmd, handle, frame);
}

MeshTableInfo VkRenderBackend::get_mesh_table_info() const
{
    return mesh_manager.get_mesh_table_info();
}

ImageReadback VkRenderBackend::readback_image(RBImageHandle img) const
{
    return image_manager.readback(img);
}

GPUMesh VkRenderBackend::get_or_create_mesh_buffers(MeshPrimHandle handle, RTBuildMode rt_build_mode)
{
    PROFILE(__FUNCTION__);
    return mesh_manager.get_or_create_mesh_buffers(handle, rt_build_mode);
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

RBImageView VkRenderBackend::get_image_view(RBImageHandle handle, uint32_t layer_index, uint32_t mip_index)
{
    return image_manager.get_view(handle, layer_index, mip_index);
}

RBImageView VkRenderBackend::get_cubemap_image_view(RBImageHandle handle)
{
    return image_manager.get_cubemap_view(handle);
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
    return swapchain.get_image(frame_handle.value());
}

RBRenderPass VkRenderBackend::get_or_create_render_pass(const FramebufferDesc& fb)
{
    RenderPassDesc desc{};
    desc.name = fb.pass;
    for (auto& attachment : fb.attachments)
    {
        desc.color_attachments.push_back(
            RenderPassAttachmentInfo{
                attachment.image_name,
                get_image_format(attachment.image), 
                attachment.load, 
                attachment.store, 
                attachment.usage,
                attachment.layer,
                attachment.mip_level,
                image_manager.is_swapchain_image(attachment.image),
                image_manager.get_image_layout(attachment.image),
                attachment.depth_attachment
            });
    }

    auto it = render_pass_cache.find(desc);
    if (it != render_pass_cache.end())
        return it->second;
    
    
    LogRBRenderPass.Log("get_or_create_render_pass (CREATE)");
    for (auto attachment : fb.attachments)
    {
        auto vk_img = image_manager.get_image_resource(attachment.image).image;
        LogRBRenderPass.Log(" * %s (image: %s [%p]). Usage: %s, load_op: %s, store_op: %s, layer: %i",
            attachment.depth_attachment ? "depth attachment" : "color attachment",
            debug.get_vk_image_name(vk_img).to_string().c_str(),
            vk_img,
            reflect::enum_name(attachment.usage).to_string().c_str(),
            reflect::enum_name(attachment.load).to_string().c_str(),
            reflect::enum_name(attachment.store).to_string().c_str(),
            attachment.layer);
    }

    std::vector<VkAttachmentDescription> attachments;
    std::vector<VkAttachmentReference> color_refs;

    uint32_t index = 0;
    
    std::optional<VkAttachmentReference> depth_ref;
    
    for (auto& attachment : desc.color_attachments)
    {
        
        attachments.push_back({
            .format = attachment.format,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = vk::vk_convert_attachment_load(attachment.load_op),
            .storeOp = vk::vk_convert_attachment_store(attachment.store_op),
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = vk::to_initial_layout(attachment.load_op, attachment.current_layout),//vk::to_attachment_layout(attachment.usage),
            .finalLayout   = vk::to_final_layout(attachment.usage, attachment.is_swapchain)//vk::to_attachment_layout(attachment.usage)
        });
        if (!attachment.is_depth_attachment)
        {
            color_refs.push_back({
                .attachment = index,
                .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL
            });
        }
        else
        {
            depth_ref = {
                .attachment = index,
                .layout = vk::to_subpass_layout(attachment.usage)
            };
        }
        index++;
    }

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = uint32_t(color_refs.size());
    subpass.pColorAttachments = color_refs.data();
    if (depth_ref.has_value())
        subpass.pDepthStencilAttachment = &depth_ref.value();
    
    VkSubpassDependency deps[2]{};

    // EXTERNAL -> SUBPASS
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL;
    deps[0].dstSubpass = 0;
    deps[0].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

    // SUBPASS -> EXTERNAL
    deps[1].srcSubpass = 0;
    deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
        VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[1].dstStageMask =
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    deps[1].srcAccessMask =
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    deps[1].dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    
    VkRenderPassCreateInfo rpci{ VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
    rpci.attachmentCount = uint32_t(attachments.size());
    rpci.pAttachments = attachments.data();
    rpci.subpassCount = 1;
    rpci.pSubpasses = &subpass;
    rpci.dependencyCount = 2;
    rpci.pDependencies = deps;

    VkRenderPass rp;
    VK_CHECK(vkCreateRenderPass(instance.get_device(), &rpci, nullptr, &rp));
    
    LogRBRenderPass.Log<Display>("Render pass %p created (%s)", rp, fb.pass.to_string().c_str());
    for (int i = 0; auto& attachment : attachments)
    {
        LogRBRenderPass.Log<Display>(" * attachment %i. rp=%p, format=%s, initialLayout=%s, finalLayout=%s, loadOp=%s, storeOp=%s", 
            i, rp, 
            vk::enum_to_string(attachment.format).data(),
            vk::enum_to_string(attachment.initialLayout).data(),
            vk::enum_to_string(attachment.finalLayout).data(),
            vk::enum_to_string(attachment.loadOp).data(),
            vk::enum_to_string(attachment.storeOp).data()
        );
        i++;
    }

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

RBImageHandle VkRenderBackend::create_texture_cubemap(const Cubemap& cubemap, const TextureCreationInfo& texture_creation_info)
{
    return image_manager.create_cubemap(cubemap, texture_creation_info);
}

Extent VkRenderBackend::get_viewport_extent() const
{
    return {swapchain.vk_extent.width, swapchain.vk_extent.height};
}

RenderResource* VkRenderBackend::create_resource(const RenderResourceDesc& desc)
{
    std::unique_ptr<VkRenderResource> res = std::make_unique<VkRenderResource>(desc, buffer_manager, *this);
    
    resources.push_back(std::move(res));
    return resources.back().get();
}

RBAccelStruct VkRenderBackend::build_tlas(RBCommandList cmd, const std::vector<TLASInfo>& objects)
{
    return tlas_manager.build_tlas(cmd, objects);
}

void VkRenderBackend::copy_image(RBCommandList cmd, const CopyImageParams& params)
{
    image_manager.perform_image_copy(cmd, params);
}


void VkRenderBackend::bind_mesh(const RBCommandList& cmd, MeshPrimHandle mesh, RBFrameHandle frame)
{
    PROFILE(__FUNCTION__);
    mesh_manager.bind(cmd, mesh);
}

void VkRenderBackend::push_constants_impl(const RBCommandList& cmd, const void* data, size_t size)
{
    pipeline_manager.push_constants(cmd, data, size);
}

void VkRenderBackend::draw_indexed(const RBCommandList& cmd, uint32_t index_count)
{
    PROFILE("draw_indexed");
    // if (params.update_viewport_extent)
    // {
    //     update_viewport(cmd, params.use_swapchain_extent ? get_swapchain_extent() : params.extent);
    // }
    vkCmdDrawIndexed(cmd, index_count, 1, 0, 0, 0);
}

void VkRenderBackend::draw_fullscreen(RBCommandList cmd)
{
    // if (params.update_viewport_extent)
    // {
    //     update_viewport(cmd, params.use_swapchain_extent ? get_swapchain_extent() : params.extent);
    // }
    
    vkCmdDraw(
        cmd.as<VkCommandBuffer>(),
        3, // fullscreen triangle
        1,
        0,
        0
    );
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

    swapchain.create();
    immediate_command_pool.init();
    
    create_depth_resources();
    create_command_pool();
    // create_pipeline();
    create_frame_sync_objects();
}

RBCommandList VkRenderBackend::begin_commands(RBFrameHandle frame_handle)
{
    LogVkCommands.Log<VeryVerbose>(" ==================== Begin commands %lu ===================", frame_handle);
    
    auto& frame = swapchain.frames[frame_handle];
    
    immediate_command_pool.on_begin_main_commands(frame.cmd);

    VK_CHECK(vkResetCommandBuffer(frame.cmd, 0));

    VkCommandBufferBeginInfo begin{
        VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO
    };
    VK_CHECK(vkBeginCommandBuffer(frame.cmd, &begin));
    
    

    return RBCommandList{frame.cmd};
}

void VkRenderBackend::end_commands(RBCommandList cmd_list)
{
    LogVkCommands.Log<VeryVerbose>("========================= End commands =====================");
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    VK_CHECK(vkEndCommandBuffer(cmd));
    immediate_command_pool.on_end_main_commands(cmd_list);
}

RBFramebufferId VkRenderBackend::get_or_create_framebuffer(const FramebufferDesc& desc)
{
    VkRenderPass rp = get_or_create_render_pass(desc);
    Extent extent = swapchain.get_extent();
    return framebuffer_manager.get_or_create_framebuffer(desc, rp, extent);
}

void VkRenderBackend::begin_render_pass(RBCommandList cmd_list, RBFramebufferId framebuffer_index)
{
    LogRBRenderPass.Log<VeryVerbose>("------------ begin_render_pass %llu -----------", framebuffer_index.as<uint64_t>());
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    const auto& fb = framebuffer_manager.get_framebuffer_resource(framebuffer_index);

    std::vector<VkClearValue> clears;
    clears.resize(fb.desc.attachments.size());
    for (int index = 0; auto& attachment : fb.desc.attachments)
    {
        if (attachment.depth_attachment)
        {
            clears[index].depthStencil = {1.0f, 0};
        } else if (index == 0)
        {
            clears[0].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        }
        else
        {
            clears[index].color = {{0.0f, 0.0f, 0.0f, 0.0f}};
        }
        index++;
    }

    VkRenderPassBeginInfo rpbi{ VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
    rpbi.renderPass  = fb.render_pass;
    rpbi.framebuffer = fb.framebuffer;
    rpbi.renderArea.extent = { fb.extent.width, fb.extent.height };
    rpbi.clearValueCount = clears.size();
    rpbi.pClearValues = clears.data();

    vkCmdBeginRenderPass(cmd, &rpbi, VK_SUBPASS_CONTENTS_INLINE);
    
    current_render_pass = fb.render_pass;
}


void VkRenderBackend::end_render_pass(RBCommandList cmd_list)
{
    LogRBRenderPass.Log<VeryVerbose>("------------- end_render_pass -------------");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdEndRenderPass(cmd);
    current_render_pass = VK_NULL_HANDLE;
    pipeline_manager.invalidate_pipeline_layout();
}


void VkRenderBackend::bind_pipeline(RBCommandList cmd_list, PipelineObject* pipeline_object)
{
    pipeline_manager.bind_pipeline(cmd_list, pipeline_object, current_render_pass);
    
    
}

void VkRenderBackend::draw(RBCommandList cmd_list, uint32_t vertex_count)
{
    LogRB.Log("draw");
    
    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    vkCmdDraw(cmd, vertex_count, 1, 0, 0);
}

void VkRenderBackend::trace_rays(RBCommandList cmd, PipelineObject* pipeline_object, Extent resolution, float depth)
{
    if (resolution.is_zero())
        resolution = swapchain.get_extent();
    
    auto as_vk_rtx_pipeline = (VkPipelineObject_RayTrace*)pipeline_object;
    vk_ext::vkCmdTraceRaysKHR(cmd.as<VkCommandBuffer>(),
        as_vk_rtx_pipeline->get_raygen_sbt(),
        as_vk_rtx_pipeline->get_miss_sbt(),
        as_vk_rtx_pipeline->get_hit_sbt(),
        as_vk_rtx_pipeline->get_callable_sbt(),
        resolution.width, resolution.height, depth);
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


bool VkRenderBackend::submit_frame(RBFrameHandle frame_handle,
                                  RBCommandList cmd_list)
{
    return swapchain.submit_frame(frame_handle, cmd_list);
}


PipelineObject* VkRenderBackend::create_graphics_pipeline(const PipelineCreateDesc_Graphics& desc, RBPipelineLayout pipeline_layout)
{
    return pipeline_manager.create_graphics_pipeline(desc, pipeline_layout);
}

PipelineObject* VkRenderBackend::create_compute_pipeline(const PipelineCreateDesc_Compute& desc, RBPipelineLayout pipeline_layout)
{
    return pipeline_manager.create_compute_pipeline(desc, pipeline_layout);
}

PipelineObject* VkRenderBackend::create_raytrace_pipeline(const PipelineCreateDesc_RayTrace& desc,
    RBPipelineLayout pipeline_layout)
{
    return pipeline_manager.create_raytrace_pipeline(desc, pipeline_layout);
}

vk::DescriptorSetLayoutData VkRenderBackend::get_vk_descriptor_set_layout(RBDescriptorSetLayout descriptor_set_layout)
{
    return buffer_manager.get_vk_descriptor_set_layout(descriptor_set_layout);
}

RBBufferHandle VkRenderBackend::create_uniform_buffer(size_t buffer_size, ResourceUsage usage_type)
{
    return buffer_manager.create_uniform_buffer(buffer_size, usage_type);
}


void VkRenderBackend::create_frame_sync_objects()
{
    swapchain.create_sync_objects();
}

RBSampler VkRenderBackend::create_sampler(const ::SamplerDesc& desc)
{
    return sampler_manager.get_or_create(desc);
}
