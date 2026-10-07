module render;

import :graph_extension;

import std.compat;
import name;

namespace
{
    // a function local: extensions register during static initialization, in any order of the units
    std::vector<RenderGraphExtension*>& registry()
    {
        static std::vector<RenderGraphExtension*> extensions;
        return extensions;
    }
}

RenderGraphExtension::RenderGraphExtension(Name in_name, RenderStage in_stage, int32_t in_order)
    : name(in_name)
    , stage(in_stage)
    , order(in_order)
{
    registry().push_back(this);
}

RenderGraphExtension::~RenderGraphExtension()
{
    std::erase(registry(), this);
}

std::vector<RenderGraphExtension*> RenderGraphExtension::of_stage(RenderStage stage)
{
    std::vector<RenderGraphExtension*> result;
    for (RenderGraphExtension* extension : registry())
        if (extension->stage == stage)
            result.push_back(extension);
    std::ranges::sort(result, [] (const RenderGraphExtension* a, const RenderGraphExtension* b)
    {
        if (a->order != b->order)
            return a->order < b->order;
        return a->name.to_string() < b->name.to_string();
    });
    return result;
}
