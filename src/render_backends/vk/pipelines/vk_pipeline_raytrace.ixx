export module vk:pipeline_raytrace;

import <vulkan/vulkan_core.h>;

import :context;
import :shader;
import :instance;
import :swapchain_control;
import :buffer_mgr;
import render;
import <unordered_map>;
import <optional>;
import <cassert>;
import :render_resource;
import :render_resource_instance;
import :reflection;
import :pipeline;




class VkPipelineObject_RayTrace : public VkPipelineObject
{
public:
    VkPipelineObject_RayTrace(
        vk::Instance& in_instance,
        vk::SwapchainControl& in_swapchain,
        vk::BufferManager& in_buffer_manager,
        vk::VkDebugObjectTracker& in_debug_object_tracker,
        RBPipelineLayout pipeline_layout,
        const PipelineCreateDesc_RayTrace& desc);


    virtual VkPipeline create_pipeline(VkRenderPass render_pass) override;
    
    void create_sbt();

    
    PipelineCreateDesc_RayTrace pipeline_desc;
    
    VkBuffer sbt_buffer = VK_NULL_HANDLE;
    VkDeviceMemory sbt_memory = VK_NULL_HANDLE;

    VkStridedDeviceAddressRegionKHR raygen_region{};
    VkStridedDeviceAddressRegionKHR miss_region{};
    VkStridedDeviceAddressRegionKHR hit_region{};
    VkStridedDeviceAddressRegionKHR callable_region{};
    
    
    virtual VkPipelineBindPoint get_bind_point() override
    {
        return VK_PIPELINE_BIND_POINT_RAY_TRACING_KHR;
    }
    
    
    const VkStridedDeviceAddressRegionKHR* get_raygen_sbt() const 
    {
        return &raygen_region;
    }
    const VkStridedDeviceAddressRegionKHR* get_miss_sbt() const 
    {
        return &miss_region;
    }
    const VkStridedDeviceAddressRegionKHR* get_hit_sbt() const 
    {
        return &hit_region;
    }
    const VkStridedDeviceAddressRegionKHR* get_callable_sbt() const 
    {
        return &callable_region;
    }
};
