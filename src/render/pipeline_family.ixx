export module render:pipeline_family;

import :pipeline_object;
import :shader_key;
import :render_backend;
import :material_model;
import <map>;
import <variant>;
import <filesystem>;

export class Renderer;

export class PipelineFamily
{
public:
    PipelineFamily(
        Name in_pass_name,
        std::shared_ptr<MaterialModel> in_model,
        std::shared_ptr<RenderBackend> in_backend,
        std::shared_ptr<Renderer> in_renderer
    );

    ShaderKey make_shader_key(std::shared_ptr<Material> material, Name pass_name) const;

    PipelineObject* request_pipeline(ShaderKey key, const PipelineLayoutDesc& layout);

private:

    void decode_key_to_defines(ShaderKey key, std::map<Name, bool>& out_defines) const;
    std::filesystem::path request_permutation(const std::string& shader_name, ShaderKey key, const std::map<Name, bool>& defines);
private:
    std::shared_ptr<RenderBackend> backend;
    std::shared_ptr<Renderer> renderer;
    std::shared_ptr<MaterialModel> model;
    const MatModel_Pass* pass = nullptr;


    std::unordered_map<uint64_t, PipelineObject*> pipelines;
    
};