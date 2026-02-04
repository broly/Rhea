export module render:common;
import <functional>;

export class RenderGraphContext;

export using RGPostRenderCallback = std::function<void(RenderGraphContext& ctx)>;
