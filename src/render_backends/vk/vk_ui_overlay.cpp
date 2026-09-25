module;

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
#include <imgui.h>
#include <imgui_impl_vulkan.h>

module vk;

import :render_backend;

import std.compat;

import render;
#include "vk_macro.h"


// Dear ImGui over the swapchain image. The overlay is drawn with dynamic rendering, so it
// needs neither a render pass nor framebuffers (and survives swapchain recreation as is).

static void check_imgui_vk_result(VkResult result)
{
    VK_CHECK(result);
}

void VkRenderBackend::init_ui_overlay()
{
    if (ui_overlay_initialized)
        return;

    ui_overlay_format = swapchain.surface_format.format;

    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_3;
    info.Instance = instance.instance;
    info.PhysicalDevice = instance.physical_device;
    info.Device = instance.device;
    info.QueueFamily = instance.queues.graphics;
    info.Queue = instance.graphics_queue;
    info.DescriptorPoolSize = 64;   // own pool: font atlas + user textures
    info.MinImageCount = 2;
    info.ImageCount = std::max<uint32_t>(vk::MAX_FRAMES_IN_FLIGHT, (uint32_t)swapchain.swapchain_image_handles.size());
    info.UseDynamicRendering = true;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.PipelineInfoMain.PipelineRenderingCreateInfo = {VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
    info.PipelineInfoMain.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineInfoMain.PipelineRenderingCreateInfo.pColorAttachmentFormats = &ui_overlay_format;
    info.CheckVkResultFn = &check_imgui_vk_result;

    ImGui_ImplVulkan_Init(&info);
    ui_overlay_initialized = true;
}

void VkRenderBackend::shutdown_ui_overlay()
{
    if (!ui_overlay_initialized)
        return;

    vkDeviceWaitIdle(instance.device);
    ImGui_ImplVulkan_Shutdown();
    ui_overlay_initialized = false;
}

void VkRenderBackend::ui_overlay_new_frame()
{
    if (ui_overlay_initialized)
        ImGui_ImplVulkan_NewFrame();
}

void VkRenderBackend::render_ui_overlay(RBCommandList cmd_list, RBFrameHandle frame)
{
    if (!ui_overlay_initialized)
        return;

    ImDrawData* draw_data = ImGui::GetDrawData();
    if (!draw_data || draw_data->CmdLists.Size == 0 || draw_data->DisplaySize.x <= 0.f || draw_data->DisplaySize.y <= 0.f)
        return;

    VkCommandBuffer cmd = cmd_list.as<VkCommandBuffer>();
    const RBImageHandle target = swapchain.get_image(frame);

    // after the last render graph pass the image is a color attachment (or presentable when
    // nothing has written it); the render graph moves it to Present right after the overlay
    ImageBarrierParams barrier{};
    barrier.debug_pass_name = Name("UIOverlay");
    barrier.image = target;
    barrier.dst_usage = RBImageUsageType::ColorAttachment;
    barrier.pass_type = RenderPassType::graphics;
    barrier.layer_count = 1;
    barrier.mip_count = 1;
    transition_image(cmd_list, barrier);

    const Extent extent = swapchain.get_extent();

    VkRenderingAttachmentInfo color{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};
    color.imageView = image_manager.get_view(target);
    color.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    color.loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
    color.storeOp = VK_ATTACHMENT_STORE_OP_STORE;

    VkRenderingInfo rendering{VK_STRUCTURE_TYPE_RENDERING_INFO};
    rendering.renderArea.extent = {extent.width, extent.height};
    rendering.layerCount = 1;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &color;

    vkCmdBeginRendering(cmd, &rendering);
    ImGui_ImplVulkan_RenderDrawData(draw_data, cmd);
    vkCmdEndRendering(cmd);

    // ImGui binds its own pipeline / layout / viewport
    pipeline_manager.invalidate_pipeline_layout();
}
