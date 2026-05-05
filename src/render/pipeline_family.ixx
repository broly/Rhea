export module render:pipeline_family;

import :pipeline_object;
import :shader_key;
import :render_backend;
import :material_model;
import <map>;
import <variant>;
import <filesystem>;

#include "object/object_reflection_macro.h"

export class Renderer;

using DefinitionValue = std::variant<bool, int>;
using DefinitionMap = std::map<Name, DefinitionValue>;

export class PipelineFamily : public RhObject
{
public:
    void ctor(
        Name in_pass_name,
        std::shared_ptr<MaterialModel> in_model,
        std::shared_ptr<RenderBackend> in_backend,
        std::shared_ptr<Renderer> in_renderer
    );

    ShaderKey make_shader_key(
        std::shared_ptr<const Material> material, 
        Name pass_name,
        const std::map<Name, uint32_t>& permutation_constants = {}) const;

    PipelineObject* request_pipeline(ShaderKey key);
    
    RBPipelineLayout get_pipeline_layout() const;
    
    const PipelineInfo& get_base_pipeline_config() const;
    
    void clear_pso_cache();

private:

    void decode_key_to_defines(ShaderKey key, DefinitionMap& out_defines) const;
    std::filesystem::path request_permutation(
        const std::string& shader_name, 
        ShaderKey key, 
        const DefinitionMap& defines,
        ShaderLanguage language);
    
    void compile_shader_checked(
        const std::filesystem::path source,
        const std::filesystem::path output_spirv,
        const std::filesystem::path timestamp_file_name,
        const std::filesystem::path shaders_dir,
        const DefinitionMap& defines,
        ShaderLanguage lang) const;
    
    
    
    PipelineLayoutDesc layout_desc;

private:
    std::shared_ptr<RenderBackend> backend;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<MaterialModel> model;
    const MatModel_PipelineVariant* pipeline_config = nullptr;
    
    RBPipelineLayout pipeline_layout;
    
    // PSO cache
    std::unordered_map<uint64_t, PipelineObject*> pipelines;
    
};
REFLECT_OBJECT(PipelineFamily, RhObject)