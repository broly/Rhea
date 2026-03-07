module render:pipeline_family;

import <variant>;
import <map>;
import <set>;
import paths;
import <fstream>;
import <filesystem>;
import <variant>;
import file_helpers;
import string_helpers;
import expr;
import assets;
import :renderer;
import :material_model;

import log;
#include "logging/log_macro.h"

#include "common/assertion_macros.h"
#include "common/projection.h"
#include "profiling/profile.h"

DEFINE_LOGGER(LogPipelineFamily, Display);


void PipelineFamily::ctor(Name in_pass_name, std::shared_ptr<MaterialModel> model, std::shared_ptr<RenderBackend> in_backend,
                               std::shared_ptr<Renderer> in_renderer)
{
    backend = in_backend;
    renderer = in_renderer;
    this->model = model;
    pipeline_config = model->get_pipeline_config_by_pass(in_pass_name);
    checkf(pipeline_config, "MaterialModel has no pipeline config that relates to pass '%s'", in_pass_name.to_string().c_str());

    const PipelineInfo& config = get_base_pipeline_config();
    
    
    layout_desc.pass = in_pass_name;
    
    
    std::map<Name, uint32_t> processed_resources;
    for (const auto& [stage, stage_info] : config.stages)
    {
        for (auto resource_name : stage_info.resources)
        {
            if (processed_resources.contains(resource_name))
            {
                const size_t index = processed_resources.at(resource_name);
                for (ResourceBinding& var : layout_desc.resources[index].resource_variable_bindings)
                {
                    var.stages |= stage;
                }
                continue;
            }
            
            PipelineResourceInfo resource_info;
            auto resource = renderer->find_resource(resource_name);
            checkf(resource, "could not find resource named %s", resource_name.to_string().c_str());
        
            resource_info.resource = resource;
        
            auto& resource_desc = resource->desc;
            resource_info.name = resource_name;
        
            uint32_t binding_index = 0;
            for (auto variable : resource_desc.variables)
            {
                std::optional<RBSampler> sampler;
                if (variable.parameter.sampler.has_value())
                    sampler.emplace(renderer->get_sampler(*variable.parameter.sampler));
                resource_info.resource_variable_bindings.push_back(ResourceBinding{
                    variable.parameter, 
                    binding_index,
                    sampler,
                    stage
                });
                binding_index++;
            }
            
            
                        
            layout_desc.resources.push_back(resource_info);
            const size_t index = layout_desc.resources.size() - 1;
            processed_resources.insert({resource_name, index});
            
        }
    }
    
    
    size_t cur_offset = 0;
    layout_desc.push_constants.clear();
    for (auto& push_constant_info : config.push_constants)
    {
        size_t pc_size = 0;
        if (push_constant_info.type == "uint32_t")
        {
            pc_size = sizeof(uint32_t);
        } else
        {
            const reflect::RuntimeReflectionInfo* info = reflect::find_runtime_info(push_constant_info.type);
            pc_size = info->size;
        }
        PushConstantRange pcr;
        pcr.size = pc_size;
        pcr.offset = cur_offset;
        pcr.stages = push_constant_info.stages;
        cur_offset += pc_size;
        layout_desc.push_constants.push_back(pcr);
    }
    pipeline_layout = backend->create_pipeline_layout(layout_desc);
    
}

ShaderKey PipelineFamily::make_shader_key(std::shared_ptr<const Material> material, Name pass_name) const
{
    PROFILE("PipelineFamily::make_shader_key");
    auto options = material->get_shader_options(pass_name);
    
    uint64_t bits = 0;
    uint8_t bit_index = 0;
    
    checkf(model->material_resource.has_value(), "Model should provide material resource");
    auto resource_info = renderer->find_resource_info(*model->material_resource);
    
    std::vector<Name> provided_options;

    std::set<Name> consumed;
    
    
    expr::Context ctx;
    
    for (const auto& [param_name, param_info] : resource_info->resource.parameters)
    {
        if (param_info.type == MaterialParamType::sampler || param_info.type == MaterialParamType::image || param_info.type == MaterialParamType::tlas)
        {
            const bool provided = material->parameters.contains(param_name);
            ctx[param_name.to_string()] = provided;
        } else if (param_info.type == MaterialParamType::definition)
        {
            ctx[param_name.to_string()] = material->parameters.find(param_name.to_string())->second.as<Name>().to_string();
        }
    }
    
    

    // enum
    for (const auto& [enum_name, enum_values] : model->permutations.enums)
    {
        auto it = options.find(enum_name);
        checkf(it != options.end(), "Missing enum option %s", enum_name.to_string().c_str());
        checkf(std::holds_alternative<Name>(it->second), "Enum option must be Name");

        const Name value = std::get<Name>(it->second);
        auto vit = enum_values.find(value);
        checkf(vit != enum_values.end(), "Invalid enum value");

        const uint32_t index = std::distance(enum_values.begin(), vit);
        const uint8_t bits_needed = uint8_t(std::ceil(std::log2(enum_values.size())));

        bits |= (uint64_t(index) << bit_index);
        bit_index += bits_needed;
        consumed.insert(enum_name);
    }

    // flags
    for (const auto& [flag_name, expression] : model->permutations.flags)
    {
        // auto it = options.find(flag_name);
        // bool value = (it != options.end()) && std::get<bool>(it->second);
        

        expr::CompiledExpr compiled = expr::compile(expression);
        bool value = expr::eval_node(compiled.root, compiled.nodes, ctx);

        bits |= (uint64_t(value) << bit_index);
        bit_index += 1;
        consumed.insert(flag_name);
    }

    checkf(bit_index <= 64, "ShaderKey overflow");

    return ShaderKey(bits);
}


PipelineObject* PipelineFamily::request_pipeline(ShaderKey key)
{
    PROFILE("PipelineFamily::request_pipeline");
    if (auto it = pipelines.find(key.key); it != pipelines.end())
        return it->second;
    
    PipelineObject* result = nullptr;
    
    DefinitionMap defines;
    decode_key_to_defines(key, defines);
    
    auto update_generic_desc_and_defines = [&] (const PipelineInfo& config, PipelineCreateDesc_Base& desc)
    {
        desc.pass_name = config.pass;
        desc.permutation_value = key.key;
        desc.layout = layout_desc;
    
        for (auto& resource : layout_desc.resources)
        {
            const Name resource_set_name = resource.resource->desc.set;
            const uint16_t resource_set = resource.resource->desc.set_index;
            defines.insert({resource_set_name, resource_set});
        
            for (auto& variable_binding : resource.resource_variable_bindings)
            {
                const Name binding_name = *variable_binding.parameter.binding;
                const uint16_t binding = *variable_binding.binding_index;
                defines.insert({binding_name, binding});
            }
        }
    };

    if (std::holds_alternative<PipelineInfo_Graphics>(*pipeline_config))
    {
        auto& config = std::get<PipelineInfo_Graphics>(*pipeline_config);

        PipelineCreateDesc_Graphics desc;
        desc.depth_test = config.depth_test;
        desc.depth_write = config.depth_write;
        desc.color_attachments = config.color_attachments;
        desc.cull_mode = config.cull_mode;
        desc.front_face = config.front_face;
        desc.no_color_attachments = config.no_color_attachments.has_value() ? *config.no_color_attachments : false;
        desc.compare_op = config.compare_op;
        desc.depth_bias = config.depth_bias ? *config.depth_bias : DepthBiasInfo{};
        desc.topology = config.topology;
        desc.layout = layout_desc;
        
        for (uint32_t index = 0; auto& vertex_layout : config.vertex_layouts)
        {
            const Name vertex_type_name = vertex_layout.vertex_type;
            auto runtime_info = reflect::find_runtime_info(vertex_type_name);
        
            VertexLayoutData data {
                .binding_index = index++, // todo
                .stride = runtime_info->size,
                .attributes = {}
            };
        
            for (uint16_t loc = 0; auto& attribute_info : vertex_layout.attributes)
            {
                auto field_info = runtime_info->find_field(attribute_info.name);
                checkf(field_info != nullptr, "could not find field %s", attribute_info.name.to_string().c_str());
            
                data.attributes.push_back(VertexAttributeInfo{
                    .variable_name = attribute_info.variable,
                    .location = loc++,
                    .offset = (uint32_t)field_info->offset,
                });
            }
            desc.vertex_layout.layouts.push_back(std::move(data));
        }
    
    
        // add attributes support for vertex shader
        int location = 0;
        for (auto& attr_layout : config.vertex_layouts)
        {
            for (auto& attr : attr_layout.attributes)
            {
                defines.insert({attr.definition, location});
                location++;
            }
        }
        
        update_generic_desc_and_defines(config, desc);
        
    
        for (const auto& [shader_stage, stage_info] : config.stages)
        {
            GraphicsPipelineStage stage;
            stage.stage = shader_stage;
            stage.compiled_shader = request_permutation(stage_info.shader.to_string(), key, defines).string();
            desc.stages.push_back(stage);
        }
    
        result = backend->create_graphics_pipeline(desc, pipeline_layout);

    } else if (std::holds_alternative<PipelineInfo_Compute>(*pipeline_config))
    {
        auto& config = std::get<PipelineInfo_Compute>(*pipeline_config);
        
        PipelineCreateDesc_Compute desc;
        
        update_generic_desc_and_defines(config, desc);
            
        for (const auto& [shader_stage, stage_info] : config.stages)
        {
            GraphicsPipelineStage stage;
            stage.stage = shader_stage;
            stage.compiled_shader = request_permutation(stage_info.shader.to_string(), key, defines).string();
            desc.stages.push_back(stage);
        }
        
        result = backend->create_compute_pipeline(desc, pipeline_layout);
    } else if (std::holds_alternative<PipelineInfo_RayTracing>(*pipeline_config))
    {
        auto& config = std::get<PipelineInfo_RayTracing>(*pipeline_config);

        PipelineCreateDesc_RayTrace desc;

        desc.max_recursion_depth = config.max_recursion_depth;

        std::vector<RayTracingShaderStage> rt_stages;
        std::map<ShaderStage, uint32_t> stage_indices;

        update_generic_desc_and_defines(config, desc);
        
        for (const auto& [shader_stage, stage_info] : config.stages)
        {
            if (!is_rtx_stage(shader_stage))
                continue;

            RayTracingShaderStage stage;
            stage.stage = shader_stage;
            stage.compiled_shader = request_permutation(stage_info.shader.to_string(), key, defines).string();
            
            uint32_t index = (uint32_t)rt_stages.size();
            stage_indices[shader_stage] = index;
            rt_stages.push_back(stage);
        }

        desc.stages = std::move(rt_stages);

        for (auto& group : config.shader_groups)
        {
            RayTracingShaderGroupDesc g{};
            g.type = group.type;

            if (group.general)
                g.general_shader = stage_indices[*group.general];

            if (group.closest_hit)
                g.closest_hit_shader = stage_indices[*group.closest_hit];

            if (group.any_hit)
                g.any_hit_shader = stage_indices[*group.any_hit];

            if (group.intersection)
                g.intersection_shader = stage_indices[*group.intersection];

            desc.groups.push_back(g);
        }


        result = backend->create_raytrace_pipeline(desc, pipeline_layout);
        
    }
    pipelines[key.key] = result;
    
    checkf(result != nullptr, "Could not create pipeline");
    return result;
}

RBPipelineLayout PipelineFamily::get_pipeline_layout() const
{
    return pipeline_layout;
}

const PipelineInfo& PipelineFamily::get_base_pipeline_config() const
{
    checkf(pipeline_config != nullptr, "Unable to retrieve config");
    
    const PipelineInfo* info = nullptr;
    std::visit([&info] (const auto& v)
    {
        info = &v;
    }, *pipeline_config);
    
    checkf(info != nullptr, "invalid config");
    
    return *info;
}


void PipelineFamily::decode_key_to_defines(ShaderKey key, DefinitionMap& out_defines) const
{
    uint8_t bit_index = 0;

    for (const auto& [enum_name, enum_values] : model->permutations.enums)
    {
        const uint8_t bits_needed =
            uint8_t(std::ceil(std::log2(enum_values.size())));

        uint64_t value = (key.key >> bit_index) & ((1ull << bits_needed) - 1);

        uint32_t idx = 0;
        for (const auto& [name, define_name] : enum_values)
        {
            out_defines[define_name] = (idx == value);
            ++idx;
        }

        bit_index += bits_needed;
    }

    for (const auto& [flag_name, _] : model->permutations.flags)
    {
        out_defines[flag_name] = ((key.key >> bit_index) & 1) != 0;
        bit_index += 1;
    }
}

std::filesystem::path PipelineFamily::request_permutation(
    const std::string& shader_name, 
    ShaderKey key, 
    const DefinitionMap& defines)
{
    const std::filesystem::path shaders_dir = paths::get_project_path() / "shaders";
    const std::filesystem::path shader_path = shaders_dir / shader_name;
    
    const auto [pure_shader_name, shader_extension] = string_helpers::split_by_dot(shader_name);
    
    if (!std::filesystem::exists(shader_path))
    {
        throw std::invalid_argument("shader_path does not exist");
    }

    const std::filesystem::path shaders_cache_dir = paths::get_project_path() / "shaders_cache";
    
    if (!std::filesystem::exists(shaders_cache_dir))
        std::filesystem::create_directory(shaders_cache_dir);

    const std::filesystem::path shader_permutations_dir = shaders_cache_dir / shader_name;
    
    if (!std::filesystem::exists(shader_permutations_dir))
        std::filesystem::create_directory(shader_permutations_dir);
    
    std::string shader_hash = std::to_string(key.key);
    
    const std::string hashed_shader_name = pure_shader_name + "_" + shader_hash + "." + shader_extension;
    
    const std::string compiled_permutation_filename = hashed_shader_name + ".spv";

    std::filesystem::path compiled_shader_permutation_file = shader_permutations_dir / compiled_permutation_filename;
    
    
    auto intermediate_dir =  paths::get_project_path() / "intermediate";
    file_helpers::create_dir_if_not_exists(intermediate_dir);
    
    auto permutations_dir = intermediate_dir / "permutations";
    file_helpers::create_dir_if_not_exists(permutations_dir);
    
    auto timestamp_file_name = permutations_dir / (hashed_shader_name + ".timestamp");

    
    if (std::filesystem::exists(compiled_shader_permutation_file))
    {
        
        std::filesystem::file_time_type filetime = std::filesystem::last_write_time(shader_path);
        std::string actual_timestamp = file_helpers::file_time_to_string(filetime);
        std::string saved_timestamp = string_helpers::trim_and_remove_newlines(file_helpers::load_text_from_file(timestamp_file_name));
        if (actual_timestamp == saved_timestamp)
        {
            return compiled_shader_permutation_file;
        }
    }
    
    std::string cmd;
    cmd += "glslc ";
    cmd += shader_path.string() + " ";
    for (auto define : defines)
    {
        auto define_str = define.first.to_string();
        std::transform(define_str.begin(), define_str.end(), define_str.begin(), PROJECTION(std::toupper));
        if (std::holds_alternative<bool>(define.second))
            cmd += std::string("-D") + define_str + "=" + (std::get<bool>(define.second) ? "1" : "0") + " ";
        else if (std::holds_alternative<int>(define.second))
            cmd += std::string("-D") + define_str + "=" + std::to_string(std::get<int>(define.second)) + " ";
        else
        {
            todo("support more types");
        }
    }
    cmd += std::string("-I ") + shaders_dir.string() + " ";
    cmd += "-o " + compiled_shader_permutation_file.string() + " ";
    cmd += "--target-env=vulkan1.3";
    
    LogPipelineFamily.Log("Compiling shader: %s", cmd.c_str());
    
    std::system("chcp 65001");
    int result = std::system(cmd.c_str());
    
    
    
    if (result == 0)
    {
        std::filesystem::file_time_type filetime = std::filesystem::last_write_time(shader_path);
        std::string timestamp = file_helpers::file_time_to_string(filetime);
        file_helpers::save_text_to_file(timestamp_file_name, timestamp);
    }
    else
    {
        checkf(false, "could not compile shader %s", shader_path.string().c_str());
    }
    
    return compiled_shader_permutation_file;
}

