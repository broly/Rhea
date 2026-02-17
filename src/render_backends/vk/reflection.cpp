module vk:reflection;

import <map>;
import <iostream>;
import <vulkan/vulkan_core.h>;
import reflect;

#define MAKE_SPV_RESULT_PAIR(x) {x, #x}
#include "common/assertion_macros.h"

const char* get_readable_spv_result(SpvReflectResult result)
{
    static std::map<SpvReflectResult, const char*> result_map = {
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_SUCCESS),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_NOT_READY),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_PARSE_FAILED),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_ALLOC_FAILED),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_RANGE_EXCEEDED),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_NULL_POINTER),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_INTERNAL_ERROR),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_COUNT_MISMATCH),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_ELEMENT_NOT_FOUND),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_CODE_SIZE),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_MAGIC_NUMBER),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_EOF),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ID_REFERENCE),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_SET_NUMBER_OVERFLOW),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_STORAGE_CLASS),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_RECURSION),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_INSTRUCTION),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_UNEXPECTED_BLOCK_DATA),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_BLOCK_MEMBER_REFERENCE),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_ENTRY_POINT),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_INVALID_EXECUTION_MODE),
        MAKE_SPV_RESULT_PAIR(SPV_REFLECT_RESULT_ERROR_SPIRV_MAX_RECURSIVE_EXCEEDED),
    };
    return result_map.at(result);
}

static VkDescriptorType convert_spv_to_vk_descriptor_type(SpvReflectDescriptorType type)
{
    static std::map<SpvReflectDescriptorType, VkDescriptorType> types_map = {
        {SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER, VK_DESCRIPTOR_TYPE_SAMPLER},
        {SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER},
        {SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE, VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE},
        {SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE},
        {SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER},
        {SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER},
        {SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER},
        {SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER},
        {SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC},
        {SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC},
        {SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT, VK_DESCRIPTOR_TYPE_INPUT_ATTACHMENT},
    };
    return types_map[type];
}

static VkFormat convert_spv_to_vk_format(SpvReflectFormat format)
{
    static std::map<SpvReflectFormat, VkFormat> format_map = {
        {SPV_REFLECT_FORMAT_UNDEFINED, VK_FORMAT_UNDEFINED},
        {SPV_REFLECT_FORMAT_R16_UINT, VK_FORMAT_R16_UINT},
        {SPV_REFLECT_FORMAT_R16_SINT, VK_FORMAT_R16_SINT},
        {SPV_REFLECT_FORMAT_R16_SFLOAT, VK_FORMAT_R16_SFLOAT},
        {SPV_REFLECT_FORMAT_R16G16_UINT, VK_FORMAT_R16G16_UINT},
        {SPV_REFLECT_FORMAT_R16G16_SINT, VK_FORMAT_R16G16_SINT},
        {SPV_REFLECT_FORMAT_R16G16_SFLOAT, VK_FORMAT_R16G16_SFLOAT},
        {SPV_REFLECT_FORMAT_R16G16B16_UINT, VK_FORMAT_R16G16B16_UINT},
        {SPV_REFLECT_FORMAT_R16G16B16_SINT, VK_FORMAT_R16G16B16_SINT},
        {SPV_REFLECT_FORMAT_R16G16B16_SFLOAT, VK_FORMAT_R16G16B16_SFLOAT},
        {SPV_REFLECT_FORMAT_R16G16B16A16_UINT, VK_FORMAT_R16G16B16A16_UINT},
        {SPV_REFLECT_FORMAT_R16G16B16A16_SINT, VK_FORMAT_R16G16B16A16_SINT},
        {SPV_REFLECT_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_R16G16B16A16_SFLOAT},
        {SPV_REFLECT_FORMAT_R32_UINT, VK_FORMAT_R32_UINT},
        {SPV_REFLECT_FORMAT_R32_SINT, VK_FORMAT_R32_SINT},
        {SPV_REFLECT_FORMAT_R32_SFLOAT, VK_FORMAT_R32_SFLOAT},
        {SPV_REFLECT_FORMAT_R32G32_UINT, VK_FORMAT_R32G32_UINT},
        {SPV_REFLECT_FORMAT_R32G32_SINT, VK_FORMAT_R32G32_SINT},
        {SPV_REFLECT_FORMAT_R32G32_SFLOAT, VK_FORMAT_R32G32_SFLOAT},
        {SPV_REFLECT_FORMAT_R32G32B32_UINT, VK_FORMAT_R32G32B32_UINT},
        {SPV_REFLECT_FORMAT_R32G32B32_SINT, VK_FORMAT_R32G32B32_SINT},
        {SPV_REFLECT_FORMAT_R32G32B32_SFLOAT, VK_FORMAT_R32G32B32_SFLOAT},
        {SPV_REFLECT_FORMAT_R32G32B32A32_UINT, VK_FORMAT_R32G32B32A32_UINT},
        {SPV_REFLECT_FORMAT_R32G32B32A32_SINT, VK_FORMAT_R32G32B32A32_SINT},
        {SPV_REFLECT_FORMAT_R32G32B32A32_SFLOAT, VK_FORMAT_R32G32B32A32_SFLOAT},
        {SPV_REFLECT_FORMAT_R64_UINT, VK_FORMAT_R64_UINT},
        {SPV_REFLECT_FORMAT_R64_SINT, VK_FORMAT_R64_SINT},
        {SPV_REFLECT_FORMAT_R64_SFLOAT, VK_FORMAT_R64_SFLOAT},
        {SPV_REFLECT_FORMAT_R64G64_UINT, VK_FORMAT_R64G64_UINT},
        {SPV_REFLECT_FORMAT_R64G64_SINT, VK_FORMAT_R64G64_SINT},
        {SPV_REFLECT_FORMAT_R64G64_SFLOAT, VK_FORMAT_R64G64_SFLOAT},
        {SPV_REFLECT_FORMAT_R64G64B64_UINT, VK_FORMAT_R64G64B64_UINT},
        {SPV_REFLECT_FORMAT_R64G64B64_SINT, VK_FORMAT_R64G64B64_SINT},
        {SPV_REFLECT_FORMAT_R64G64B64_SFLOAT, VK_FORMAT_R64G64B64_SFLOAT},
        {SPV_REFLECT_FORMAT_R64G64B64A64_UINT, VK_FORMAT_R64G64B64A64_UINT},
        {SPV_REFLECT_FORMAT_R64G64B64A64_SINT, VK_FORMAT_R64G64B64A64_SINT},
        {SPV_REFLECT_FORMAT_R64G64B64A64_SFLOAT, VK_FORMAT_R64G64B64A64_SFLOAT},
    };
    return format_map[format];
}

#define SPV_CHECK(spv_result) \
    do                                                              \
	{                                                               \
		SpvReflectResult r = spv_result;                                           \
		if (r != SPV_REFLECT_RESULT_SUCCESS)                                                    \
		{                                                           \
			std::cout <<"Detected SPIR-V reflection error: " << get_readable_spv_result(r) << std::endl; \
			abort();                                                \
		}                                                           \
	} while (0)


SpirvReflection::SpirvReflection(const std::vector<uint32_t>& in_spirv_binary)
    : spirv_binary(in_spirv_binary)
{
    SpvReflectResult result = spvReflectCreateShaderModule(in_spirv_binary.size() * sizeof(uint32_t),
                                                           in_spirv_binary.data(),
                                                           &spirv_module);
    
}

SpirvReflection::~SpirvReflection()
{
    spvReflectDestroyShaderModule(&spirv_module);
}

std::unordered_map<Name, ReflectedInterfaceVariable> SpirvReflection::get_input_variables() const
{
    std::unordered_map<Name, ReflectedInterfaceVariable> result;
    
    uint32_t count;
    
    SPV_CHECK(
        spvReflectEnumerateInputVariables(&spirv_module, &count, nullptr)
    );
    std::vector<SpvReflectInterfaceVariable*> variables(count);
    SPV_CHECK(
        spvReflectEnumerateInputVariables(&spirv_module, &count, variables.data())
    );
    for (auto* v : variables)
    {
        if (v->storage_class == SpvStorageClass::SpvStorageClassInput)
        {
            result.insert({v->name, 
               ReflectedInterfaceVariable{
                   .name = v->name, 
                   .location = v->location,
                   .format = convert_spv_to_vk_format(v->format),
               }});
        }
    }
    return result;
}

std::unordered_map<Name, ReflectedBinding> SpirvReflection::get_bindings() const
{
    std::unordered_map<Name, ReflectedBinding> result;
    
    uint32_t count;
    
    SPV_CHECK(
        spvReflectEnumerateDescriptorBindings(&spirv_module, &count, nullptr)
    );
    std::vector<SpvReflectDescriptorBinding*> descriptors(count);
    SPV_CHECK(
        spvReflectEnumerateDescriptorBindings(&spirv_module, &count, descriptors.data())
    );
    for (auto* v : descriptors)
    {
        result.insert({v->name, 
            ReflectedBinding{
                .name = v->name, 
                .set = v->set,
                .binding = v->binding,
                .count = v->count,
                .descriptor_type = convert_spv_to_vk_descriptor_type(v->descriptor_type)
            }});
        
        
        // VALIDATION
        if (auto type_desc = v->type_description)
        {
            if (type_desc->type_name)
            {
                if (auto runtime_reflection = reflect::find_runtime_info(type_desc->type_name))
                {
                    checkf(type_desc->member_count == runtime_reflection->fields.size(),
                        "fields number mismatched");
                    for (int i = 0; i < type_desc->member_count; i++)
                    {
                        SpvReflectBlockVariable& block_var = v->block.members[i];
                        const reflect::FieldRuntimeReflectionInfo& cpp_field = runtime_reflection->fields[i];
                        checkf(cpp_field.name == block_var.name,
                            "Different member names detected in %s", type_desc->type_name);
                        checkf(cpp_field.offset == block_var.offset, 
                            "'%s::%s' variable offset differs with shader and C++ struct (possible layout violation)",
                            type_desc->type_name, block_var.name);
                    }
                }
            }
        }
    }
    return result;
}
