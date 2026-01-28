module render:pipeline_family;

import <variant>;
import <map>;
import <set>;
import paths;
import <fstream>;
import <filesystem>;
import file_helpers;
import string_helpers;
import expr;
import assets;
import :renderer;

#include "common/assertion_macros.h"
#include "common/projection.h"

PipelineFamily::PipelineFamily(Name in_pass_name, std::shared_ptr<MaterialModel> model, std::shared_ptr<RenderBackend> in_backend,
                               std::shared_ptr<Renderer> in_renderer)
    : backend(in_backend)
    , renderer(in_renderer)
    , model(model)
{
    pass = model->get_pass_info(in_pass_name);
    checkf(pass, "MaterialModel has no pass '%s'", in_pass_name.to_string().c_str());
    
    for (uint32_t index = 0; auto& vertex_layout : model->vertex_layouts)
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
        layout.vertex_layout.layouts.push_back(std::move(data));
    }
}

ShaderKey PipelineFamily::make_shader_key(std::shared_ptr<Material> material, Name pass_name) const
{
    auto options = material->get_shader_options(pass_name);
    
    uint64_t bits = 0;
    uint8_t bit_index = 0;
    
    std::vector<Name> provided_options;

    std::set<Name> consumed;
    
    expr::Context ctx;
    
    for (const auto& [param_name, param_info] : model->parameters)
    {
        if (param_info.type == MaterialParamType::sampler)
        {
            const bool provided = material->parameters.contains(param_name);
            ctx[param_name.to_string()] = provided;
        } else if (param_info.type == MaterialParamType::definition)
        {
            ctx[param_name.to_string()] = material->parameters[param_name.to_string()].as<Name>().to_string();
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
    if (auto it = pipelines.find(key.key); it != pipelines.end())
        return it->second;

    DefinitionMap defines;
    decode_key_to_defines(key, defines);

    GraphicsPipelineDesc desc;
    desc.pass_name = pass->name;
    desc.permutation_value = key.key;
    desc.depth_test = pass->depth_test;
    desc.depth_write = pass->depth_write;
    desc.is_translucent = pass->translucent;
    desc.cull_mode = pass->cull_mode;
    desc.front_face = pass->front_face;
    desc.compare_op = pass->compare_op;
    desc.depth_bias = pass->depth_bias ? *pass->depth_bias : DepthBiasInfo{};
    desc.layout = layout;
    
    // add attributes support for vertex shader
    int location = 0;
    for (auto& attr_layout : model->vertex_layouts)
    {
        for (auto& attr : attr_layout.attributes)
        {
            defines.insert({attr.definition, location});
            location++;
        }
    }
    
    int set_index = 0;
    for (auto resource_name : pass->resources)
    {
        GraphicsPipelineResourceInfo resource_info;
        auto resource = renderer->find_resource(resource_name);
        checkf(resource, "could not find resource named %s", resource_name.to_string().c_str());
        
        resource_info.resource = resource;
        
        auto& resource_desc = resource->desc;
        
        defines.insert({resource_desc.set, set_index});
        resource_info.set = set_index;
        
        int binding_index = 0;
        for (auto variable : resource_desc.variables)
        {
            defines.insert({variable.binding, binding_index});
            resource_info.resource_variable_bindings.push_back(binding_index);
            binding_index++;
        }
        
        set_index++;
        
        desc.layout.resources.push_back(resource_info);
    }

    if (!model->parameters.empty())
    {
        defines.insert({model->set, set_index});
        int binding_index = 0;
        GraphicsPipelineResourceInfo material_resource_info;
        material_resource_info.set = set_index;
        for (const auto& [name, param] : model->parameters)
        {
            if (param.type == MaterialParamType::sampler || param.type == MaterialParamType::uniform)
            {
                checkf(param.binding, "Binding definition must be set");
                defines.insert({*param.binding, binding_index});
                material_resource_info.resource_variable_bindings.push_back(binding_index);
                binding_index++;
            }
        }
        
        RenderResource* material_resource = renderer->get_or_create_resource_from_model(model, pass->name);
        material_resource_info.resource = material_resource;
    
        desc.layout.resources.push_back(material_resource_info);
    }
    for (const auto& [stage_enum, shader_name] : pass->shaders)
    {
        GraphicsPipelineStage stage;
        stage.stage = stage_enum;
        stage.compiled_shader = request_permutation(shader_name, key, defines).string();
        desc.stages.push_back(stage);
    }

    
    size_t cur_offset = 0;
    desc.layout.push_constants.clear();
    for (auto& push_constant_info : pass->push_constants)
    {
        const reflect::RuntimeReflectionInfo* info = reflect::find_runtime_info(push_constant_info.type);
        PushConstantRange pcr;
        pcr.size = info->size;
        pcr.offset = cur_offset;
        pcr.stages = make_shader_stages_mask(push_constant_info.stages);
        cur_offset += info->size;
        desc.layout.push_constants.push_back(pcr);
    }
    auto pipeline = backend->create_pipeline(desc);
    pipelines[key.key] = pipeline;

    return pipeline;
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
    
    
    
    std::vector<std::string> new_shader_lines;
    auto shader_lines = file_helpers::load_lines_from_file(shader_path);

    auto version_line = shader_lines[0];
    shader_lines.erase(shader_lines.begin());
    
    new_shader_lines.push_back(version_line);
    
    for (auto define : defines)
    {
        auto define_str = define.first.to_string();
        std::transform(define_str.begin(), define_str.end(), define_str.begin(), PROJECTION(std::toupper));
        
        new_shader_lines.push_back(std::string("#undef ") + define_str);
        std::string new_line;
        if (std::holds_alternative<bool>(define.second))
            new_line = std::string("#define ") + define_str + " " + (std::get<bool>(define.second) ? "1" : "0");
        else if (std::holds_alternative<int>(define.second))
            new_line = std::string("#define ") + define_str + " " + std::to_string(std::get<int>(define.second));
        else
        {
            todo("support more types");
        }
        new_shader_lines.push_back(new_line);
    }
    
    new_shader_lines.insert(new_shader_lines.end(), shader_lines.begin(), shader_lines.end());
    
    
    auto permutation_source_file = permutations_dir / hashed_shader_name;
    
    file_helpers::save_lines_to_file(permutation_source_file, new_shader_lines);
    
    std::string cmd;
    cmd += "glslc ";
    cmd += permutation_source_file.string() + " ";
    cmd += std::string("-I ") + shaders_dir.string() + " ";
    cmd += "-o " + compiled_shader_permutation_file.string();
    
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
        checkf(false, "could not compile shader %s", permutation_source_file.string().c_str());
    }
    
    return compiled_shader_permutation_file;
}

