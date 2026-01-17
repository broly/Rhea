export module render:pipeline_family;

import :pipeline_object;
import :shader_key;
import :render_backend;
import <map>;
import <variant>;
import <filesystem>;

using ShaderOptionValue = std::variant<bool, Name>;

export class PipelineFamily
{
public:
    PipelineFamily(const GraphicsPipelineDesc& in_desc, std::shared_ptr<RenderBackend> in_backend);
    
    ShaderKey make_shader_key(const std::map<Name, ShaderOptionValue>& in_options) const;
    
    PipelineObject* request_pipeline(ShaderKey key);
    
    std::filesystem::path request_permutation(const std::string& shader_name, ShaderKey key, const std::map<Name, bool>& options);
    
    
    GraphicsPipelineDesc desc;
    std::shared_ptr<RenderBackend> backend;
    
};