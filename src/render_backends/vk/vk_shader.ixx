export module vk:shader;

import <string>;
import <vulkan/vulkan_core.h>;
import <vector>;

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
        code = std::move(other.code);
        shader_name = std::move(other.shader_name);
        other.device_ = nullptr;
        other.shader_module = nullptr;
        other.code.clear();
        other.shader_name.clear();
        
    };

    ~VkShader();

    VkShaderModule get_module() const { return shader_module; }
    
    const std::vector<uint32_t>& get_spirv() const;
    
    std::string get_shader_name() const { return shader_name; }

private:
    std::string shader_name;
    VkDevice device_;
    VkShaderModule shader_module;
    std::vector<uint32_t> code;
};
