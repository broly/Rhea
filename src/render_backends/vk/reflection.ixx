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


struct PipelineReflection
{
    std::unordered_map<std::string, ReflectedInterfaceVariable> input_variables;
};


struct SpirvReflection
{
    SpirvReflection(const std::vector<uint32_t>& in_spirv_binary);
    ~SpirvReflection();
    
    std::unordered_map<std::string, ReflectedInterfaceVariable> get_input_variables() const;

    const std::vector<uint32_t>& spirv_binary;
    SpvReflectShaderModule spirv_module;
};
