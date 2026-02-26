export module vk:device_extension_api;

import <vulkan/vulkan_core.h>;

#define LOAD_VK_EXT_FUNCTION_COMBINE(a,b) a##b

#define LOAD_VK_EXT_FUNCTION_INNER(device, func_name) \
    func_name = (LOAD_VK_EXT_FUNCTION_COMBINE(PFN_, func_name))vkGetDeviceProcAddr(device, #func_name)

#define LOAD_VK_EXT_FUNCTION(device, func_name) \
    LOAD_VK_EXT_FUNCTION_INNER(device, func_name)

namespace vk_ext
{
    PFN_vkCreateAccelerationStructureKHR vkCreateAccelerationStructureKHR = nullptr;
    PFN_vkDestroyAccelerationStructureKHR vkDestroyAccelerationStructureKHR = nullptr;
    PFN_vkGetAccelerationStructureBuildSizesKHR vkGetAccelerationStructureBuildSizesKHR = nullptr;
    PFN_vkCmdBuildAccelerationStructuresKHR vkCmdBuildAccelerationStructuresKHR = nullptr;
    PFN_vkGetAccelerationStructureDeviceAddressKHR vkGetAccelerationStructureDeviceAddressKHR = nullptr;
    
    void load_functions(VkDevice device)
    {
        LOAD_VK_EXT_FUNCTION(device, vkCreateAccelerationStructureKHR);
        LOAD_VK_EXT_FUNCTION(device, vkDestroyAccelerationStructureKHR);
        LOAD_VK_EXT_FUNCTION(device, vkGetAccelerationStructureBuildSizesKHR);
        LOAD_VK_EXT_FUNCTION(device, vkCmdBuildAccelerationStructuresKHR);
        LOAD_VK_EXT_FUNCTION(device, vkGetAccelerationStructureDeviceAddressKHR);
    }
}