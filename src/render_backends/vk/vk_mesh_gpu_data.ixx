export module vk:mesh_gpu_data;

import <vulkan/vulkan_core.h>;
import render;

export struct MeshGPUData 
{
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    uint32_t index_count = 0;
    RBDescriptorSet descriptor_set;
    
    // BLAS
    VkAccelerationStructureKHR blas;
    VkDeviceMemory blas_memory;
    VkBuffer blas_buffer;
    VkDeviceAddress blas_address;
};