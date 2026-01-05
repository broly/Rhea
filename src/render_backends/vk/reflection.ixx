export module vk:reflection;
import <string>;
import <numeric>;
import render;
import spirv_reflect;
import <unordered_map>;
import <optional>;
import <vulkan/vulkan_core.h>;


struct ReflectedInterfaceVariable
{
    std::string name;
    uint32_t location;
    VkFormat format;
    ShaderStage stage;
};

struct ReflectedBinding
{
    std::string name;
    uint32_t set;
    uint32_t binding;
    uint32_t count;
    VkDescriptorType descriptor_type;
};


struct PipelineReflection
{
    std::string shader_name;
    std::unordered_map<std::string, ReflectedInterfaceVariable> input_variables;
    std::unordered_map<std::string, ReflectedBinding> bindings;
};


struct SpirvReflection
{
    SpirvReflection(const std::vector<uint32_t>& in_spirv_binary);
    ~SpirvReflection();
    
    std::unordered_map<std::string, ReflectedInterfaceVariable> get_input_variables() const;
    std::unordered_map<std::string, ReflectedBinding> get_bindings() const;

    const std::vector<uint32_t>& spirv_binary;
    SpvReflectShaderModule spirv_module;
};
