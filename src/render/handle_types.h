#pragma once
#include <cstdint>
#include <type_traits>
#include <vulkan/vulkan_core.h>

struct RBCommandList
{
    uintptr_t handle = 0;
    
    template<typename T>
    requires std::is_same_v<VkCommandBuffer, T> // || another backends types...
    T as() const
    {
        return reinterpret_cast<T>(handle);
    }
    
};


struct RBPipelineHandle
{
    uintptr_t handle = 0;
    
    
    template<typename T>
    requires std::is_same_v<VkPipeline, T>  // || another backends types...
    T as() const
    {
        return reinterpret_cast<T>(handle);
    }
};

struct RBDescriptorSet
{
    uintptr_t handle = 0;
    
    template<typename T>
    requires std::is_same_v<VkDescriptorSet, T>  // || another backends types...
    T as() const
    {
        return reinterpret_cast<T>(handle);
    }
};

struct RBWindowHandle
{
    RBWindowHandle(void* ptr) 
        : handle(reinterpret_cast<uintptr_t>(ptr)) {};
    
    template<typename T>
    T* unsafe_as() const
    {
        return reinterpret_cast<T*>(handle);
    }
    
    uintptr_t handle = 0;
};

using RBFramebufferId = uint32_t;
