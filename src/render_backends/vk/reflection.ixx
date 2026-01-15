export module vk:reflection;
import <string>;
import <numeric>;
import render;
import spirv_reflect;
import <unordered_map>;
import <optional>;
import <vulkan/vulkan_core.h>;
import name;


struct ReflectedInterfaceVariable
{
    Name name;
    uint32_t location;
    VkFormat format;
    ShaderStage stage;
};

struct ReflectedBinding
{
    Name name;
    uint32_t set;
    uint32_t binding;
    uint32_t count;
    VkDescriptorType descriptor_type;
};


struct PipelineReflection
{
    Name shader_name;
    std::unordered_map<Name, ReflectedInterfaceVariable> input_variables;
    std::unordered_map<Name, ReflectedBinding> bindings;
};


struct SpirvReflection
{
    SpirvReflection(const std::vector<uint32_t>& in_spirv_binary);
    ~SpirvReflection();
    
    std::unordered_map<Name, ReflectedInterfaceVariable> get_input_variables() const;
    std::unordered_map<Name, ReflectedBinding> get_bindings() const;

    const std::vector<uint32_t>& spirv_binary;
    SpvReflectShaderModule spirv_module;
};
