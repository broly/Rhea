#pragma once

// Registers the listed component types (loadable from JSON by name unless [[=scene::runtime_only]]) and the
// [[=scene::on_spawned<T>]] functions declared before it in the current namespace (usually the anonymous
// namespace of a .cpp); scene::register_auto_components does the registration. Needs `import framework;`.
//
//     SCENE_REGISTER_COMPONENTS(Rail)
//     SCENE_REGISTER_COMPONENTS()          // only on_spawned functions
#define SCENE_REGISTER_COMPONENTS(...) \
    struct scene_register_components_marker; \
    [[maybe_unused]] const bool scene_components_registered = \
        ::scene::detail::register_components<scene_register_components_marker __VA_OPT__(,) __VA_ARGS__>();
