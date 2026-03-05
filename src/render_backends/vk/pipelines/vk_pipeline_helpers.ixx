export module vk:pipeline_helpers;

import <vulkan/vulkan_core.h>;
import render;
#include "common/assertion_macros.h"
import <array>;

#define PROVIDE_VK_SHADER_STAGE(stage, vk_stage) \
   stages_vk_bits[ShaderStage_index(ShaderStage::stage)] = vk_stage;
    

export namespace vk_conf_converters
{

    constexpr std::array<VkShaderStageFlagBits, MAX_STAGES> get_vk_stages()
    {
        std::array<VkShaderStageFlagBits, MAX_STAGES> stages_vk_bits;
        PROVIDE_VK_SHADER_STAGE(vertex, VK_SHADER_STAGE_VERTEX_BIT);
        PROVIDE_VK_SHADER_STAGE(fragment, VK_SHADER_STAGE_FRAGMENT_BIT);
        PROVIDE_VK_SHADER_STAGE(geometry, VK_SHADER_STAGE_GEOMETRY_BIT);
        PROVIDE_VK_SHADER_STAGE(tesselation_control, VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT);
        PROVIDE_VK_SHADER_STAGE(tesselation_evaluation, VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT);
        PROVIDE_VK_SHADER_STAGE(compute, VK_SHADER_STAGE_COMPUTE_BIT);
        
        PROVIDE_VK_SHADER_STAGE(rtx_raygen, VK_SHADER_STAGE_RAYGEN_BIT_KHR);
        PROVIDE_VK_SHADER_STAGE(rtx_any_hit, VK_SHADER_STAGE_ANY_HIT_BIT_KHR);
        PROVIDE_VK_SHADER_STAGE(rtx_closest_hit, VK_SHADER_STAGE_CLOSEST_HIT_BIT_KHR);
        PROVIDE_VK_SHADER_STAGE(rtx_miss, VK_SHADER_STAGE_MISS_BIT_KHR);
        PROVIDE_VK_SHADER_STAGE(rtx_intersection, VK_SHADER_STAGE_INTERSECTION_BIT_KHR);
        PROVIDE_VK_SHADER_STAGE(rtx_callable, VK_SHADER_STAGE_CALLABLE_BIT_KHR);
        return stages_vk_bits;
    }

    constexpr std::array<VkShaderStageFlagBits, MAX_STAGES> STAGES_VK_BITS = get_vk_stages();
    
    constexpr VkShaderStageFlagBits conv_shader_stage(ShaderStage stage)
    {
        return STAGES_VK_BITS[ShaderStage_index(stage)];
    }
    
    VkCullModeFlags conv_cull_mode(CullMode cull_mode)
    {
        switch (cull_mode) {
        case CullMode::none:
            return VK_CULL_MODE_NONE;
        case CullMode::front:
            return VK_CULL_MODE_FRONT_BIT;
        case CullMode::back:
            return VK_CULL_MODE_BACK_BIT;
        case CullMode::both:
            return VK_CULL_MODE_FRONT_AND_BACK;
        }
        unreachable("error");
    }


    VkCompareOp conv_compare_op(CompareOp compare_op)
    {
        switch (compare_op) {
        case CompareOp::never:
            return VK_COMPARE_OP_NEVER;
        case CompareOp::less:
            return VK_COMPARE_OP_LESS;
        case CompareOp::equal:
            return VK_COMPARE_OP_EQUAL;
        case CompareOp::less_or_equal:
            return VK_COMPARE_OP_LESS_OR_EQUAL;
        case CompareOp::greater:
            return VK_COMPARE_OP_GREATER;
        case CompareOp::not_equal:
            return VK_COMPARE_OP_NOT_EQUAL;
        case CompareOp::greater_or_equal:
            return VK_COMPARE_OP_GREATER_OR_EQUAL;
        case CompareOp::always:
            return VK_COMPARE_OP_ALWAYS;
        }
        unreachable("error");
    }

    VkFrontFace conv_front_face(FrontFace front_face)
    {
        switch (front_face) {
        case FrontFace::CW:
            return VK_FRONT_FACE_CLOCKWISE;
        case FrontFace::CCW:
            return VK_FRONT_FACE_COUNTER_CLOCKWISE;
        }
        unreachable("error");
    }
}
