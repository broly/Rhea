module vk:framebuffer_mgr;
import :log;

import <cassert>;

#include "render_backends/vk/vk_macro.h"

void vk::FramebufferManager::destroy_framebuffers()
{
    for (auto& fb : framebuffer_resources)
    {
        vkDestroyFramebuffer(instance.device, fb.framebuffer, nullptr);
    }
    framebuffer_resources.clear();
}

RBFramebufferId vk::FramebufferManager::get_or_create_framebuffer(const FramebufferDesc& desc, VkRenderPass render_pass,
                                                                  Extent extent)
{
    Extent ext = !desc.extent.is_zero() ? desc.extent : extent;
    
    for (uint32_t i = 0; i < framebuffer_resources.size(); ++i)
    {
        if (framebuffer_resources[i].matches(desc, render_pass, ext))
            return RBFramebufferId{(uint64_t)i};
    }
    
    LogVkFramebufferManager.Log("Creating framebuffer for pass '%s'",  desc.pass.to_string().c_str());
    
    
    assert(!ext.is_zero());

    std::vector<VkImageView> attachments;

    for (const auto& attachment : desc.attachments)
    {
    
        LogVkFramebufferManager.Log("Use color attachment %s (image: %p, layer: %i)", 
            attachment.image_name.to_string().c_str(), attachment.image, attachment.layer);
    
        attachments.push_back(image_manager.fetch_image_view_generic(attachment.image, attachment.layer, attachment.mip_level));
    }


    VkFramebufferCreateInfo info{ VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
    info.renderPass = render_pass;
    info.attachmentCount = uint32_t(attachments.size());
    info.pAttachments = attachments.data();
    info.width  = ext.width;
    info.height = ext.height;
    info.layers = 1;

    VkFramebuffer fb;
    VK_CHECK(vkCreateFramebuffer(instance.get_device(), &info, nullptr, &fb));

    uint32_t id = framebuffer_resources.size();
    framebuffer_resources.push_back({
        .desc = desc,
        .framebuffer = fb,
        .render_pass = render_pass,
        .extent = ext,
        .attachments = attachments
    });

    return RBFramebufferId{ (uint64_t)id };
}

const vk::FramebufferResource& vk::FramebufferManager::get_framebuffer_resource(RBFramebufferId framebuffer_id)
{
    return framebuffer_resources[framebuffer_id.as<uint64_t>()];
}

