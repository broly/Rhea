export module render:rg_params;

import <map>;
import name;
import glm;

export struct RenderGraphParameters
{
    uint32_t render_iter_id = 0;
    uint32_t num_runs = 0;
    uint32_t output_frame_id = 0;
    std::map<Name, int> int_params;
    std::map<Name, bool> bool_params;
    std::map<Name, glm::vec3> vec3_params;
    
    int get_int(Name param, int default_value = 0) const
    {
        auto it = int_params.find(param);
        if (it != int_params.end())
            return it->second;
        return default_value;
    }
};
