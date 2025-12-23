#pragma once
#include "render_backend.h"
#include "resource_type.h"
#include "rg_types.h"


struct RenderGraphPass
{
    std::string name;

    std::vector<RenderResource> reads;
    std::vector<RenderResource> writes;

    std::function<void(class RenderGraphContext&)> execute;
};

class RenderGraphContext
{
public:
    RenderGraphContext(
        RenderBackend& in_backend,
        RBCommandList in_cmd,
        RBFramebufferId in_fb)
        : backend(in_backend)
        , cmd(in_cmd)
        , framebuffer(in_fb)
    {}

    RenderBackend& backend;
    RBCommandList  cmd;
    RBFramebufferId framebuffer;
};



class RenderGraph
{
public:
    RGResourceHandle create_texture(const RGTextureDesc& desc);
    RGResourceHandle create_buffer(const RGBufferDesc& desc);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile();
    void execute(RenderBackend& backend);
    
    

private:
    struct Resource
    {
        RGResourceType type;
    };

    std::vector<Resource> resources;
    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
};
