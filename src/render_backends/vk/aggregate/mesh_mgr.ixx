export module vk:mesh_mgr;

import :instance;
import assets;
import <unordered_map>;
import <vulkan/vulkan_core.h>;

export struct MeshGPUData {
    VkBuffer vertex_buffer = VK_NULL_HANDLE;
    VkDeviceMemory vertex_memory = VK_NULL_HANDLE;
    VkBuffer index_buffer = VK_NULL_HANDLE;
    VkDeviceMemory index_memory = VK_NULL_HANDLE;
    uint32_t index_count = 0;
    RBDescriptorSet descriptor_set;
};


namespace vk
{
    class MeshManager
    {
    public:
        MeshManager(vk::Instance& in_instance)
            : instance(in_instance)
        {}
        vk::Instance& instance;
        
        
        void get_or_create_mesh_buffers(MeshPrimHandle handle);
        
        void bind(const RBCommandList& cmd, MeshPrimHandle mesh);


        std::unordered_map<MeshPrimHandle, MeshGPUData> mesh_map;
    };
}
