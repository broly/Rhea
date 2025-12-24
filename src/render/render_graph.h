#pragma once
#include "render_backend.h"
#include "resource_type.h"
#include "rg_types.h"


struct RenderGraphPass
{
    std::string name;

    std::vector<RGTextureHandle> reads;
    std::vector<RGTextureHandle> writes;

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
    
    RGTextureHandle create_texture(const RGTextureDesc& desc);
    RGResourceHandle create_buffer(const RGBufferDesc& desc);

    RGPassId add_pass(RenderGraphPass&& pass);

    void compile(RenderBackend& backend);
    void execute(RenderBackend& backend);
    
    

private:
    struct Resource
    {
        RGResourceType type;
    };

    std::vector<RGResource> resources;
    std::vector<RenderGraphPass> passes;

    std::vector<uint32_t> execution_order;
    
    std::vector<RGTexture> textures;
};
