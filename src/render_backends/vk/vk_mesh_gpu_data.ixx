module;

#include <vulkan/vulkan_core.h>

export module vk:mesh_gpu_data;

import render;

export struct MeshGPUData 
{
    // ranges of the mesh manager's shared blocks (MeshManager::suballocate)
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceSize vertex_offset = 0;
    VkDeviceAddress vertex_address = 0;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceSize index_offset = 0;
    VkDeviceAddress index_address = 0;
    uint32_t index_count = 0;
    uint32_t vertex_count = 0;
    RBDescriptorSet descriptor_set;
    
    // BLAS
    VkAccelerationStructureKHR blas;
    VkDeviceMemory blas_memory;
    VkBuffer blas_buffer;
    VkDeviceAddress blas_address;
    
    
    uint32_t mesh_table_index;
};