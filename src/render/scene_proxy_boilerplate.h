#pragma once

#define SCENE_PROXY_BOILERPLATE(Name, Super, ProxyName, Id) \
    Name() \
        : Super() \
    { \
        scene_proxy_size = sizeof(ProxyName); \
    } \
    static constexpr SceneViewProcId type_id = Id; \
    using SceneViewProcessor::SceneViewProcessor; \

#define REGISTER_SCENE_PROXY_PROCESSOR(Name) \
    namespace \
    { \
        static int dummy = SceneViewProcessor::register_scene_view_processor<Name>(); \
    }