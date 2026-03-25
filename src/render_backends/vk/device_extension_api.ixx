export module vk:device_extension_api;

import <vulkan/vulkan_core.h>;

#define LOAD_VK_EXT_FUNCTION_COMBINE(a,b) a##b

#define LOAD_VK_EXT_DEVICE_FUNCTION_INNER(device, func_name) \
    func_name = (LOAD_VK_EXT_FUNCTION_COMBINE(PFN_, func_name))vkGetDeviceProcAddr(device, #func_name)

#define LOAD_VK_EXT_DEVICE_FUNCTION(device, func_name) \
    LOAD_VK_EXT_DEVICE_FUNCTION_INNER(device, func_name)

#define LOAD_VK_EXT_INSTANCE_FUNCTION_INNER(instance, func_name) \
    func_name = (LOAD_VK_EXT_FUNCTION_COMBINE(PFN_, func_name))vkGetInstanceProcAddr(instance, #func_name)

#define LOAD_VK_EXT_INSTANCE_FUNCTION(instance, func_name) \
    LOAD_VK_EXT_INSTANCE_FUNCTION_INNER(instance, func_name)

namespace vk_ext
{
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupHandlesKHR vkGetRayTracingShaderGroupHandlesKHR = nullptr;
    
    PFN_vkCmdTraceRaysKHR vkCmdTraceRaysKHR = nullptr;
    PFN_vkCreateRayTracingPipelinesKHR vkCreateRayTracingPipelinesKHR = nullptr;
    PFN_vkGetRayTracingCaptureReplayShaderGroupHandlesKHR vkGetRayTracingCaptureReplayShaderGroupHandlesKHR = nullptr;
    PFN_vkCmdTraceRaysIndirectKHR vkCmdTraceRaysIndirectKHR = nullptr;
    PFN_vkGetRayTracingShaderGroupStackSizeKHR vkGetRayTracingShaderGroupStackSizeKHR = nullptr;
    PFN_vkCmdSetRayTracingPipelineStackSizeKHR vkCmdSetRayTracingPipelineStackSizeKHR = nullptr;
    PFN_vkCreateDebugUtilsMessengerEXT vkCreateDebugUtilsMessengerEXT = nullptr;
    
    void load_device_functions(VkDevice device)
    {
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkCreateAccelerationStructureKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkDestroyAccelerationStructureKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkGetAccelerationStructureBuildSizesKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkCmdBuildAccelerationStructuresKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkGetAccelerationStructureDeviceAddressKHR);
        
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkCmdTraceRaysKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkCreateRayTracingPipelinesKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkGetRayTracingCaptureReplayShaderGroupHandlesKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkCmdTraceRaysIndirectKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkGetRayTracingShaderGroupStackSizeKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkCmdSetRayTracingPipelineStackSizeKHR);
        LOAD_VK_EXT_DEVICE_FUNCTION(device, vkGetRayTracingShaderGroupHandlesKHR);
        
    }
    
    void load_instance_functions(VkInstance instance)
    {
        LOAD_VK_EXT_INSTANCE_FUNCTION(instance, vkCreateDebugUtilsMessengerEXT);
    }
}