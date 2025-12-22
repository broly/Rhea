#pragma once
#include <cassert>
#include <optional>
#include <vulkan/vulkan_core.h>
#include <GLFW/glfw3.h>
#include "common/type_utils.h"
#include "common/type_macros.h"

template<typename... Ts>
struct RBHandle
{
    using AllowedTypes = TypeList<Ts...>;
    
    uintptr_t handle;
    
    AUTO_SPACESHIP(RBHandle, handle);
#if _DEBUG
    std::optional<const char*> type_id = std::nullopt;
#endif
    
    RBHandle()
    {
        handle = 0;
    }
    
    RBHandle(const RBHandle& Value)
    {
        handle = Value.handle;
    };
    
    template<typename T>
    requires AllowedTypes::template contains<T>
    RBHandle(T Value)
    {
#if _DEBUG
        type_id = typeid(T).name();
#endif
        handle = (uintptr_t)(Value);
    }
    
    template<typename T>
    requires AllowedTypes::template contains<T>
    T as() const
    {
#if _DEBUG
        if (type_id.has_value())
        {
            assert(type_id.value() == typeid(T).name());  // todo: debug compare, remake it to strcmp
        }
#endif
        return reinterpret_cast<T>(handle);
    }
    
    friend bool operator==(RBHandle lhs, RBHandle rhs)
    {
#if _DEBUG
        if (lhs.type_id.has_value() && rhs.type_id.has_value())
        {
            assert(lhs.type_id.value() == rhs.type_id.value());  // todo: debug compare, remake it to strcmp
        }
#endif
        return lhs.handle == rhs.handle;
    }

    friend bool operator!=(RBHandle lhs, RBHandle rhs)
    {
        return !(lhs == rhs);
    }
    
    template<typename T>
    requires AllowedTypes::template contains<T>
    RBHandle& operator=(T rhs)
    {
#if _DEBUG
        if (type_id.has_value())
        {
            assert(type_id.value() == typeid(T).name());  // todo: debug compare, remake it to strcmp
        }
        type_id = typeid(T);
#endif
        handle = rhs;
        return *this;
    }

    template<typename T>
    requires AllowedTypes::template contains<T>
    operator T() const
    {
        return as<T>();
    }
};


// just universal integer handle types
using RBCommandList = RBHandle<VkCommandBuffer>;
using RBPipelineHandle = RBHandle<VkPipeline>;
using RBDescriptorSet = RBHandle<VkDescriptorSet>;
using RBDescriptorSetLayout = RBHandle<uint64_t>;

using RBWindowHandle = RBHandle<GLFWwindow*>;

using RBFramebufferId = uint32_t;
using RBFrameHandle = uint32_t;