export module spirv_reflect;

#include <spirv-reflect/spirv_reflect.h>;

export 
{
    using ::SpvReflectDescriptorType;
    
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLER;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_IMAGE;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_TEXEL_BUFFER;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_TEXEL_BUFFER;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_INPUT_ATTACHMENT;
    using ::SPV_REFLECT_DESCRIPTOR_TYPE_ACCELERATION_STRUCTURE_KHR;
    
    using ::SpvReflectShaderModule;
    using ::spvReflectCreateShaderModule;
    using ::spvReflectEnumerateDescriptorBindings;
    using ::SpvReflectDescriptorBinding;
    using ::spvReflectEnumerateDescriptorBindings;
    using ::spvReflectEnumeratePushConstantBlocks;
    using ::SpvReflectBlockVariable;
    using ::spvReflectEnumeratePushConstantBlocks;
    using ::spvReflectDestroyShaderModule;
    
    using ::SpvReflectResult;
}