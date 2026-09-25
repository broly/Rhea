export module render:pipeline_object;

import std.compat;

import :handle_types;
import name;

export class PipelineObject
{
public:
    virtual ~PipelineObject() = default;
    
    virtual RBPipelineLayout get_layout() const = 0;
    
    Name debug_name;
    uint64_t permutation_value;
};

