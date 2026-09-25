export module render:common;

import std.compat;

export class RenderGraphContext;

export using RGPostRenderCallback = std::function<void(RenderGraphContext& ctx)>;
