#pragma once

// Registers the [[=ecs::system<...>]], [[=ecs::on_add]] and [[=ecs::on_remove]] functions declared before it
// in the current namespace (usually the anonymous namespace of a .cpp); Schedule::add_auto_systems and
// ecs::add_auto_hooks add them. Needs `import ecs;` in the including file.
#define ECS_REGISTER() \
    struct ecs_register_marker; \
    [[maybe_unused]] const bool ecs_registered = \
        ::ecs::detail::register_systems<ecs_register_marker>(__FILE__);
