export module ecs:entity;

import std.compat;

export namespace ecs
{
    // Generational handle: once the entity is destroyed its slot gets a new generation,
    // so old handles stop resolving instead of dangling. Plain data - copy, hash, send over the network.
    struct Entity
    {
        static constexpr uint32_t invalid_index = ~0u;

        uint32_t index = invalid_index;
        uint32_t generation = 0;

        constexpr bool is_null() const { return index == invalid_index; }
        constexpr explicit operator bool() const { return !is_null(); }
        constexpr uint64_t bits() const { return (uint64_t(generation) << 32) | index; }

        constexpr bool operator==(const Entity&) const = default;
        constexpr auto operator<=>(const Entity&) const = default;
    };

    inline constexpr Entity null_entity{};
}

template<>
struct std::hash<ecs::Entity>
{
    size_t operator()(const ecs::Entity& e) const noexcept { return std::hash<uint64_t>{}(e.bits()); }
};

// std::format("{}", entity) -> "12v3" (index v generation), "null"
template<>
struct std::formatter<ecs::Entity>
{
    constexpr auto parse(std::format_parse_context& ctx) { return ctx.begin(); }

    auto format(const ecs::Entity& e, std::format_context& ctx) const
    {
        if (e.is_null())
            return std::format_to(ctx.out(), "null");
        return std::format_to(ctx.out(), "{}v{}", e.index, e.generation);
    }
};
