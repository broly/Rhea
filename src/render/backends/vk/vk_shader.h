#pragma once
#include <string>
#include <vulkan/vulkan_core.h>

class VkShader
{
public:
    VkShader(VkDevice device,
             const std::string& path);
    VkShader() = delete;
    VkShader(const VkShader&) = delete;
    VkShader(VkShader&& other) noexcept
    {
        device_ = other.device_;
        shader_module = other.shader_module;
        other.device_ = nullptr;
        other.shader_module = nullptr;
    };

    ~VkShader();

    VkShaderModule get_module() const { return shader_module; }

private:
    VkDevice device_;
    VkShaderModule shader_module;
};
