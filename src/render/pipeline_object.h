#pragma once
#include "handle_types.h"

class PipelineObject
{
public:
    virtual RBPipelineHandle get_pipeline_handle() const = 0;
};
