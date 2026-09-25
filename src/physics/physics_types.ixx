export module physics:types;

import std.compat;
import glm;

// Engine-side vocabulary of the physics module. Nothing here depends on the backend (Jolt), so
// gameplay code can include the physics API without the physics library.
export namespace phys
{
    // What a body is. Every body has exactly one category.
    //
    // Two separate questions replace Unreal's channels + response matrix:
    //  * simulation: which categories push each other, BodyDesc::collides_with (a mask, symmetric:
    //    both bodies must accept the other one),
    //  * queries: what a trace hits, QueryFilter::categories, given per call.
    enum class Category : uint16_t
    {
        static_world = 1 << 0,  // level geometry
        dynamic      = 1 << 1,  // simulated props
        character    = 1 << 2,  // character capsules
        hitbox       = 1 << 3,  // damage volumes of characters, only hit by queries
        projectile   = 1 << 4,  // simulated projectiles (grenades, arrows); bullets are sweeps
        voxel        = 1 << 5,  // destructible voxel terrain
        trigger      = 1 << 6,  // sensors: report overlaps, never block
        debris       = 1 << 7,  // small simulated pieces, ignored by characters
    };

    inline constexpr uint32_t category_count = 8;

    struct CategoryMask
    {
        uint16_t bits = 0;

        constexpr CategoryMask() = default;
        constexpr CategoryMask(Category category) : bits(static_cast<uint16_t>(category)) {}
        static constexpr CategoryMask from_bits(uint16_t bits) { CategoryMask m; m.bits = bits; return m; }

        static constexpr CategoryMask none() { return {}; }
        static constexpr CategoryMask all() { return from_bits((1u << category_count) - 1); }

        constexpr bool contains(Category category) const { return (bits & static_cast<uint16_t>(category)) != 0; }
        constexpr bool intersects(CategoryMask other) const { return (bits & other.bits) != 0; }
        constexpr bool empty() const { return bits == 0; }

        friend constexpr CategoryMask operator|(CategoryMask a, CategoryMask b) { return from_bits(a.bits | b.bits); }
        friend constexpr CategoryMask operator&(CategoryMask a, CategoryMask b) { return from_bits(a.bits & b.bits); }
        friend constexpr CategoryMask operator-(CategoryMask a, CategoryMask b) { return from_bits(a.bits & ~b.bits); }
        friend constexpr bool operator==(CategoryMask a, CategoryMask b) = default;
    };

    constexpr CategoryMask operator|(Category a, Category b) { return CategoryMask(a) | CategoryMask(b); }

    // Everything a query hits by default: all but triggers
    constexpr CategoryMask blocking_categories()
    {
        return CategoryMask::all() - Category::trigger;
    }

    // Simulation mask used when BodyDesc::collides_with is not set
    constexpr CategoryMask default_collision_mask(Category category)
    {
        using enum Category;
        switch (category)
        {
        case static_world: return dynamic | character | projectile | debris;
        case dynamic:      return static_world | dynamic | character | projectile | voxel | debris;
        case character:    return static_world | dynamic | character | voxel;
        case hitbox:       return CategoryMask::none();
        case projectile:   return static_world | dynamic | voxel;
        case voxel:        return dynamic | character | projectile | debris;
        case trigger:      return dynamic | character;
        case debris:       return static_world | dynamic | voxel;
        }
        return CategoryMask::none();
    }

    enum class Motion : uint8_t
    {
        fixed,      // never moves (level geometry)
        kinematic,  // moved by code (move_kinematic), pushes dynamic bodies
        dynamic,    // simulated
    };

    // Handle of a body in a PhysicsScene. Stale handles are detected (generation counter).
    struct BodyId
    {
        static constexpr uint32_t invalid_value = 0xffffffffu;

        uint32_t value = invalid_value;

        constexpr bool is_valid() const { return value != invalid_value; }
        friend constexpr bool operator==(BodyId a, BodyId b) = default;
        friend constexpr auto operator<=>(BodyId a, BodyId b) = default;
    };

    struct BodyTransform
    {
        glm::vec3 position{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };
    };

    // Result of a raycast or a shape sweep
    struct Hit
    {
        BodyId body;
        uint64_t user_data = 0;             // BodyDesc::user_data of the hit body
        Category category = Category::static_world;
        glm::vec3 position{ 0.0f };         // contact point in world space
        glm::vec3 normal{ 0.0f };           // surface normal, facing the query
        float distance = 0.0f;              // along the query direction
        uint32_t sub_shape = 0;             // part of a compound (e.g. a hitbox bone), backend specific
        bool start_penetrating = false;     // sweeps: the shape already overlapped at the start
    };

    struct QueryFilter
    {
        CategoryMask categories = blocking_categories();
        std::span<const BodyId> ignore_bodies{};

        // rare custom rules (team, material, ...), called for candidates that passed the checks above
        std::function<bool(BodyId body, uint64_t user_data)> accept{};

        // raycasts: also hit triangles from behind (back faces are skipped by default)
        bool hit_back_faces = false;
    };
}
