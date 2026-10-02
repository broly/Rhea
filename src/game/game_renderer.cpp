module game;

import :renderer;

import std.compat;
import assertions;
import fixed_string;

import render;
import vk;
import glm;
import rhmath;
import profile;
import name;
import rhcomponents;
import :render_graph;
import :generic_render_graph;
import :constants;
import texture_format;
import paths;
import :cubemap_capture_render_graph;
import :brdf_lut_capture_render_graph;
import :debug_view;
import :reflection_probes;

import log;
#include "render_layout.h"
#include "common/assertion_macros.h"
#include "profiling/profile.h"

#include "logging/log_macro.h"

DEFINE_LOGGER(LogGameRenderer, Display);

void GameRenderer::set_engine(const std::shared_ptr<Engine>& in_engine)
{
    engine = in_engine;
}


constexpr bool debug_shadow_pass = false;

void GameRenderer::init(RBWindowHandle in_window)
{
    
    render_backend = RenderBackend::create<VkRenderBackend>(in_window);
    
    
    samplers["default"] = render_backend->create_sampler(samplers::default_surface());
    samplers["surface"] = render_backend->create_sampler(samplers::default_surface());
    samplers["shadow"] = render_backend->create_sampler(samplers::default_shadow());
    samplers["depth"] = render_backend->create_sampler(samplers::default_depth());
    samplers["linear_clamp"] = render_backend->create_sampler(samplers::linear_clamp());
    
    
    Renderer::init(in_window);
    
    // debug view cvars -> renderer int params (read by the render graph every frame)
    set_int_param(DebugViewParams::view_mode, (int)cv_debug_view_mode.get());
    set_int_param(DebugViewParams::show_skeleton, cv_debug_show_skeleton.get() ? 1 : 0);
    // bisection switches for rendering artifacts, editable in Render > Render graph int params
    set_int_param(SyncDebug::param, 0);
    set_int_param(LightingDebug::param, 0);
    set_int_param(GeometryDebug::param, 0);
    cv_debug_view_mode.on_changed([this] (DebugViewMode mode)
    {
        set_int_param(DebugViewParams::view_mode, (int)mode);
    });
    cv_debug_show_skeleton.on_changed([this] (bool show)
    {
        set_int_param(DebugViewParams::show_skeleton, show ? 1 : 0);
    });
}

ReflectionProbeSystem* GameRenderer::get_reflection_probes() const
{
    auto graph = std::dynamic_pointer_cast<GenericRenderGraph>(get_main_render_graph());
    return graph ? graph->reflection_probes.get() : nullptr;
}

void GameRenderer::rebake_reflection_probes()
{
    if (ReflectionProbeSystem* probes = get_reflection_probes())
    {
        probes->request_rebake_all();
        LogGameRenderer.Log("Rebaking reflection probes");
    }
}
