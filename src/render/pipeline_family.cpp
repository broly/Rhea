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
    for (const auto& stage_info : config.get_stages())
    {
        for (auto resource_name : stage_info.resources)
        {
            if (processed_resources.contains(resource_name))
            {
                const size_t index = processed_resources.at(resource_name);
                for (ResourceBinding& var : layout_desc.resources[index].resource_variable_bindings)
                {
                    var.stages |= stage_info.stage;
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
                    stage_info.stage
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

ShaderKey PipelineFamily::make_shader_key(
    Name pass_name,
    std::shared_ptr<const Material> material, 
    const std::map<Name, Name>& permutation_enums,
    const std::map<Name, uint32_t>& permutation_constants) const
{
    PROFILE("PipelineFamily::make_shader_key");
    
    
    std::map<Name, ShaderOptionValue> options;
    if (material != nullptr)
    {
        options = material->get_shader_options(pass_name);
    }
    uint64_t bits = 0;
    uint8_t bit_index = 0;
    
    std::vector<Name> provided_options;

    std::set<Name> consumed;
    
    
    expr::Context ctx;
    
    if (model->material_info.has_value())
    {
        for (const auto& [param_name, param_info] : model->material_info->params)
        {
            if (param_info.is_static_parameter())
            {
                check(material != nullptr);
                ctx[param_name.to_string()] = material->parameters.find(param_name.to_string())->second.as<Name>().to_string();
            } else
            {
                check(material != nullptr);
                const bool provided = material->parameters.contains(param_name);
                ctx[param_name.to_string()] = provided;
            }
        }
    }
    
    

    // enum
    if (model->permutations.enums.has_value())
    {
        for (const auto& [enum_name, enum_values] : *model->permutations.enums)
        {
            auto it = options.find(enum_name);
            auto permutation_param_it = permutation_enums.find(enum_name);
        
            const uint8_t bits_needed = std::bit_width(enum_values.size() - 1);
            
            Name value;
        
            if (it != options.end())
            {
                checkf(std::holds_alternative<Name>(it->second), "Enum option must be Name");
                value = std::get<Name>(it->second);
            } else if (permutation_param_it != permutation_enums.end())
            {
                value = permutation_param_it->second;
            }
            else
            {
                bit_index += bits_needed;
                consumed.insert(enum_name);
                continue;
                // checkf(false, "Neither option nor parameter '%s' provided for shader key", enum_name.to_string().c_str());
            }
            
            auto vit = enum_values.find(value);
            checkf(vit != enum_values.end(), "Invalid enum value");

            const uint32_t index = std::distance(enum_values.begin(), vit);

            bits |= (uint64_t(index) << bit_index);
            bit_index += bits_needed;
            consumed.insert(enum_name);
        }
    }

    // flags
    if (model->permutations.flags.has_value())
    {
        for (const auto& [flag_name, expression] : *model->permutations.flags)
        {
            // auto it = options.find(flag_name);
            // bool value = (it != options.end()) && std::get<bool>(it->second);
        

            expr::CompiledExpr compiled = expr::compile(expression);
            bool value = expr::eval_node(compiled.root, compiled.nodes, ctx);

            bits |= (uint64_t(value) << bit_index);
            bit_index += 1;
            consumed.insert(flag_name);
        }
    }
    
    if (model->permutations.variants.has_value())
    {
        for (const auto& [variant_name, variant] : *model->permutations.variants)
        {
            checkf(variant.values.size() > 0, "No any variant values");
        
            auto provided_value_it = permutation_constants.find(variant_name);
        
            const bool constant_provided = provided_value_it != permutation_constants.end();
            
            checkf(constant_provided || variant.default_index.has_value(),
                "Neither parameter nor default_index provided for variant");
            
            uint32_t using_value = constant_provided ?
                provided_value_it->second : variant.values[variant.default_index.value()];


            bool is_valid_value = false;
            uint8_t index = 0;
            for (uint8_t val_index = 0; auto& val : variant.values)
            {
                if (using_value == val)
                {
                    is_valid_value = true;
                    index = val_index;
                    break;
                }
                val_index++;
            }
            checkf(is_valid_value, "Wrong variant value");
        
            const uint8_t bits_needed = std::bit_width(variant.values.size() - 1);
            bits |= (uint64_t(index) << bit_index);
            bit_index += bits_needed;
            consumed.insert(variant_name);
        }
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
                {
                    const Name binding_name = *variable_binding.parameter.binding;
                    const uint16_t binding = *variable_binding.binding_index;
                    defines.insert({binding_name, binding});
                }
                if (variable_binding.parameter.array_size_definition.has_value())
                {
                    checkf(variable_binding.parameter.initial_array_size.has_value(), "array_size_definition is provided, but initial_array_size is not");
                    const Name array_size_define_name = *variable_binding.parameter.array_size_definition;
                    const uint16_t array_size = *variable_binding.parameter.initial_array_size;
                    defines.insert({array_size_define_name, array_size});
                }
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
        
    
        for (const auto& stage_info : config.get_stages())
        {
            PipelineStage stage;
            stage.stage = stage_info.stage;
            stage.shader = stage_info.shader.to_string();
            stage.compiled_shader = request_permutation(stage_info.shader.to_string(), key, defines, stage_info.lang).string();
            desc.stages.push_back(stage);
        }
    
        result = backend->create_graphics_pipeline(desc, pipeline_layout);

    } else if (std::holds_alternative<PipelineInfo_Compute>(*pipeline_config))
    {
        auto& config = std::get<PipelineInfo_Compute>(*pipeline_config);
        
        PipelineCreateDesc_Compute desc;
        
        update_generic_desc_and_defines(config, desc);
        
        PipelineStage stage;
        stage.stage = ShaderStage::compute;
        stage.shader = config.shader_stage.shader.to_string();
        stage.compiled_shader = request_permutation(config.shader_stage.shader.to_string(), key, defines, config.shader_stage.get_lang()).string();
        desc.compute_stage = stage;

        result = backend->create_compute_pipeline(desc, pipeline_layout);
    } else if (std::holds_alternative<PipelineInfo_RayTracing>(*pipeline_config))
    {
        auto& config = std::get<PipelineInfo_RayTracing>(*pipeline_config);

        PipelineCreateDesc_RayTrace desc;

        desc.max_recursion_depth = config.max_recursion_depth;

        update_generic_desc_and_defines(config, desc);

        std::unordered_map<Name, uint32_t> shader_indices;
        std::unordered_map<Name, uint32_t> group_indices;
        
        
        /*
            SHADERS
        */

        for (auto& shader : config.shaders)
        {
            RayTracingShaderStage stage{};

            stage.stage = shader.stage;
            stage.shader_source = shader.shader.to_string();
            stage.lang = shader.get_lang();
            // stage.compiled_shader = request_permutation(shader.shader.to_string(), key, defines).string();

            uint32_t index = (uint32_t)desc.stages.size();

            shader_indices[shader.name] = index;

            desc.stages.push_back(stage);
        }

        /*
            GROUPS
        */

        for (auto& group : config.shader_groups)
        {
            RayTracingShaderGroupDesc g{};

            g.type = group.type;

            if (group.general)
                g.general_shader = shader_indices[*group.general];

            if (group.closest_hit)
                g.closest_hit_shader = shader_indices[*group.closest_hit];

            if (group.any_hit)
                g.any_hit_shader = shader_indices[*group.any_hit];

            if (group.intersection)
                g.intersection_shader = shader_indices[*group.intersection];

            uint32_t index = (uint32_t)desc.groups.size();

            group_indices[group.name] = index;

            desc.groups.push_back(g);
        }

        /*
            SBT
        */

        auto build_sbt = [&](const std::vector<RayTracingSBTLayoutEntry>& src,
            std::vector<uint32_t>& dst)
        {
            for (uint32_t i = 0; auto& entry : src)
            {
                uint32_t group_index = group_indices[entry.group];

                dst.push_back(group_index);

                defines.insert({ entry.define, (int)i });

                i++;
            }
        };

        build_sbt(config.sbt.raygen, desc.sbt_raygen);
        build_sbt(config.sbt.miss, desc.sbt_miss);
        build_sbt(config.sbt.hit, desc.sbt_hit);
        build_sbt(config.sbt.callable, desc.sbt_callable);
        
        for (auto& stage : desc.stages)
        {
            stage.compiled_shader = request_permutation(stage.shader_source, key, defines, stage.lang).string();
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

void PipelineFamily::clear_pso_cache()
{
    for (auto pipeline : pipelines)
    {
        backend->destroy_pipeline(pipeline.second);
    }
    pipelines.clear();
}


void PipelineFamily::decode_key_to_defines(ShaderKey key, DefinitionMap& out_defines) const
{
    uint8_t bit_index = 0;

    if (model->permutations.enums.has_value())
    {
        for (const auto& [enum_name, enum_values] : *model->permutations.enums)
        {
            const uint8_t bits_needed = std::bit_width(enum_values.size() - 1);;

            uint64_t value = (key.key >> bit_index) & ((1ull << bits_needed) - 1);

            uint32_t idx = 0;
            for (const auto& [name, define_name] : enum_values)
            {
                out_defines[define_name] = (idx == value);
                ++idx;
            }

            bit_index += bits_needed;
        }
    }

    if (model->permutations.flags.has_value())
    {
        for (const auto& [flag_name, _] : *model->permutations.flags)
        {
            out_defines[flag_name] = ((key.key >> bit_index) & 1) != 0;
            bit_index += 1;
        }
    }
    
    if (model->permutations.variants.has_value())
    {
        for (const auto& [variant_name, variant_values] : *model->permutations.variants)
        {
            const uint8_t bits_needed = std::bit_width(variant_values.values.size() - 1);

            uint64_t value = (key.key >> bit_index) & ((1ull << bits_needed) - 1);

            out_defines[variant_name] = variant_values.values[value];

            bit_index += bits_needed;
        }
    }
}

std::filesystem::path PipelineFamily::request_permutation(
    const std::string& shader_rel_path, 
    ShaderKey key, 
    const DefinitionMap& defines,
    ShaderLanguage language)
{
    LogPipelineFamily.Log("Requesting permutation for %s. Defines: ", shader_rel_path.c_str());
    for (auto& [key, value] : defines)
    {
        const std::string svalue = std::holds_alternative<bool>(value) ? 
            std::to_string(std::get<bool>(value)) : std::to_string(std::get<int>(value));
        LogPipelineFamily.Log(" * %s=%s", key.to_string().c_str(), svalue.c_str());
    }
    
    const std::filesystem::path shaders_dir = paths::get_project_path() / "shaders";
    const std::filesystem::path shader_path = shaders_dir / shader_rel_path;
    
    const std::string shader_name = string_helpers::substring_after_last(shader_rel_path, '/', shader_rel_path);
    
    const auto [pure_shader_name, shader_extension] = string_helpers::split_by_dot(shader_name);
    
    if (!std::filesystem::exists(shader_path))
    {
        throw std::invalid_argument("shader_path does not exist");
    }

    const std::filesystem::path shaders_cache_dir = paths::get_project_path() / "shaders_cache";
    
    if (!std::filesystem::exists(shaders_cache_dir))
        std::filesystem::create_directories(shaders_cache_dir);
    

    const std::filesystem::path shader_permutations_dir = shaders_cache_dir / shader_rel_path;
    
    if (!std::filesystem::exists(shader_permutations_dir))
        std::filesystem::create_directories(shader_permutations_dir);
    
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
    
    
    compile_shader_checked(shader_path, 
        compiled_shader_permutation_file, timestamp_file_name, shaders_dir, defines, language);
    
    return compiled_shader_permutation_file;
}

void PipelineFamily::compile_shader_checked(
    const std::filesystem::path source, 
    const std::filesystem::path output_spirv,
    const std::filesystem::path timestamp_file_name,
    const std::filesystem::path shaders_dir,
    const DefinitionMap& defines,
    ShaderLanguage lang) const
{
    std::string cmd;

    std::string define_pref;
    
    switch (lang)
    {
        case ShaderLanguage::glsl:
            cmd += "glslc ";
            define_pref = "-D";
            break;

        case ShaderLanguage::hlsl:
            cmd += "glslc ";
            cmd += "-x hlsl ";
            define_pref = "-D";
            break;

        case ShaderLanguage::slang:
            cmd += "slangc ";
            cmd += "-target spirv ";
            define_pref = "-D ";
            break;
    }

    cmd += "\"" + source.string() + "\" ";

    // Defines
    for (auto define : defines)
    {
        auto define_str = define.first.to_string();
        std::transform(define_str.begin(), define_str.end(), define_str.begin(), PROJECTION(std::toupper));

        if (std::holds_alternative<bool>(define.second))
        {
            cmd += define_pref + define_str + "=" + (std::get<bool>(define.second) ? "1" : "0") + " ";
        }
        else if (std::holds_alternative<int>(define.second))
        {
            cmd += define_pref + define_str + "=" + std::to_string(std::get<int>(define.second)) + " ";
        }
        else
        {
            todo("support more types");
        }
    }

    // Include path
    cmd += "-I \"" + shaders_dir.string() + "\" ";

    // Output
    cmd += "-o \"" + output_spirv.string() + "\" ";

    if (lang == ShaderLanguage::glsl || lang == ShaderLanguage::hlsl)
    {
        cmd += "--target-env=vulkan1.3 ";
    }
    else if (lang == ShaderLanguage::slang)
    {
        cmd += "-profile sm_6_0 ";
        cmd += "-entry main ";
    }

    LogPipelineFamily.Log("Compiling shader: %s", cmd.c_str());

    std::system("chcp 65001");
    int result = std::system(cmd.c_str());

    if (result == 0)
    {
        auto filetime = std::filesystem::last_write_time(source);
        std::string timestamp = file_helpers::file_time_to_string(filetime);
        file_helpers::save_text_to_file(timestamp_file_name, timestamp);
    }
    else
    {
        checkf(false, "could not compile shader %s", source.string().c_str());
    }

}
