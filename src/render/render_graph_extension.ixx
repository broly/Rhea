export module render:graph_extension;

import std.compat;
import name;

import :render_graph;

// Places of the frame where game modules add their own passes to the main render graph (the graph of the game:
// graphs of captures and bakes run none). The graph names its textures, an extension finds them with
// RenderGraph::texture: "g_depth", "g_linear_depth", "g_normal", "g_world_normal", "g_albedo", "g_position",
// "g_motion_vectors", "g_geometry_normal", "g_emissive" (the g-buffer), "hdr_color_present[BASE]" (the lit HDR
// frame), "ldr_color" (the tone mapped frame, display encoded RGBA8), "swapchain".
export enum class RenderStage : uint8_t
{
    after_gbuffer,      // depth and g-buffer of the opaque scene are done; GI, AO and lighting not yet
    after_lighting,     // the lit HDR frame with its fog and particles, before TAA and the post process
    after_tonemap,      // the tone mapped ldr_color, before Present copies it to the swapchain (with the HUD)
    overlay,            // the swapchain after Present: over everything
};

// A static object of a game module:
//
//     class MyPasses final : public RenderGraphExtension
//     {
//     public:
//         MyPasses() : RenderGraphExtension("MyPasses", RenderStage::after_tonemap) {}
//         void build(RenderGraph& graph, const std::function<bool()>& active) override { ... graph.add_pass ... }
//         void prepare(RenderGraphContext& ctx, RenderGraph& graph) override { ... descriptors ... }
//     };
//     MyPasses my_passes;
//
// Extensions are registered before the renderer builds its graphs (static initialization) and live as long as
// the program. One graph runs them: their state can live in the object.
export class RenderGraphExtension
{
public:
    RenderGraphExtension(Name name, RenderStage stage, int32_t order = 0);
    virtual ~RenderGraphExtension();
    RenderGraphExtension(const RenderGraphExtension&) = delete;
    RenderGraphExtension& operator=(const RenderGraphExtension&) = delete;

    // Once, when the graph adds the passes of the stage (build_passes): textures (graph.create_texture) and
    // passes (graph.add_pass), in the order they run. `active` is false while the graph shows a debug view
    // instead of the frame: conditions of the passes should include it.
    virtual void build(RenderGraph& graph, const std::function<bool()>& active) = 0;
    // Every frame before its commands are recorded (prepare_resources): descriptors, per frame state
    virtual void prepare(RenderGraphContext& ctx, RenderGraph& graph) {}

    Name get_name() const { return name; }
    RenderStage get_stage() const { return stage; }
    int32_t get_order() const { return order; }

    // the extensions of a stage, by order (then name)
    static std::vector<RenderGraphExtension*> of_stage(RenderStage stage);

private:
    Name name;
    RenderStage stage;
    int32_t order;
};
