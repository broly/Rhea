export module physics:scene;

import std.compat;
import glm;

import :types;
import :shape;

export namespace phys
{
    struct PhysicsSettings
    {
        uint32_t max_bodies = 65536;
        uint32_t max_body_pairs = 65536;
        uint32_t max_contact_constraints = 16384;

        glm::vec3 gravity{ 0.0f, -9.81f, 0.0f };

        // step() advances the simulation in fixed steps, catching up at most max_steps_per_frame
        float fixed_timestep = 1.0f / 60.0f;
        uint32_t max_steps_per_frame = 4;

        // 0: hardware threads - 1
        uint32_t worker_threads = 0;

        // cooked triangle meshes (create_mesh_shape with a cache key), empty: no cache
        std::filesystem::path shape_cache_dir;
    };

    struct BodyDesc
    {
        Shape shape;
        glm::vec3 position{ 0.0f };
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };

        Motion motion = Motion::fixed;
        Category category = Category::static_world;
        std::optional<CategoryMask> collides_with;      // default_collision_mask(category) when unset

        float friction = 0.5f;
        float restitution = 0.0f;
        float mass = 0.0f;                              // dynamic bodies, 0: from the shape volume (1000 kg/m3)
        glm::vec3 linear_velocity{ 0.0f };
        glm::vec3 angular_velocity{ 0.0f };

        bool continuous_collision = false;              // fast bodies, no tunneling (projectiles)
        bool start_active = true;

        uint64_t user_data = 0;                         // returned in hits, e.g. the owning component
    };

    // Kinematic character: a capsule moved with collide-and-slide, walks stairs and slopes, sticks to
    // the floor, pushes dynamic bodies. Not simulated: the game computes the velocity every frame.
    struct CharacterDesc
    {
        float radius = 0.3f;
        float height = 1.8f;                            // total, feet to the top of the capsule
        glm::vec3 position{ 0.0f };                     // feet
        glm::quat rotation{ 1.0f, 0.0f, 0.0f, 0.0f };

        float max_slope_degrees = 50.0f;                // steeper ground is not walkable
        float step_height = 0.35f;                      // stairs / curbs climbed without jumping
        float stick_to_floor = 0.5f;                    // snaps down this far when walking down slopes and steps
        float mass = 70.0f;                             // kg, for pushing bodies
        float max_push_force = 500.0f;                  // N

        Category category = Category::character;
        std::optional<CategoryMask> blocked_by;         // default_collision_mask(category) when unset

        uint64_t user_data = 0;                         // also set on the character's body (hit by queries)
    };

    struct CharacterId
    {
        static constexpr uint32_t invalid_value = 0xffffffffu;

        uint32_t value = invalid_value;

        constexpr bool is_valid() const { return value != invalid_value; }
        friend constexpr bool operator==(CharacterId a, CharacterId b) = default;
    };

    enum class GroundState : uint8_t
    {
        on_ground,          // walkable ground
        on_steep_ground,    // standing on a slope steeper than max_slope_degrees: should slide down
        not_supported,      // touching something that does not support the character: falls
        in_air,
    };

    struct CharacterState
    {
        glm::vec3 position{ 0.0f };                     // feet
        glm::vec3 velocity{ 0.0f };                     // after collisions (sliding along walls, landing, ...)
        GroundState ground = GroundState::in_air;
        glm::vec3 ground_normal{ 0.0f, 1.0f, 0.0f };
        glm::vec3 ground_velocity{ 0.0f };              // moving platforms
        BodyId ground_body;
    };

    struct DebugDrawSettings
    {
        glm::vec3 camera_position{ 0.0f };
        float max_distance = 40.0f;                     // bodies whose bounds are farther are skipped

        bool fixed_bodies = false;                      // level geometry, can be a lot of lines
        bool moving_bodies = true;
        bool bounding_boxes = false;
        bool velocities = false;
    };

    struct PhysicsStats
    {
        uint32_t bodies = 0;
        uint32_t active_bodies = 0;
        uint32_t shapes_cooked = 0;                     // triangle meshes built this session
        uint32_t shapes_from_cache = 0;                 // triangle meshes loaded from shape_cache_dir
        uint32_t steps_last_frame = 0;
        float step_ms = 0.0f;                           // CPU time of the last step() call
    };

    class PhysicsBackend;

    // A simulated world: bodies, the fixed step, and spatial queries.
    //
    // Threading:
    //  * shapes may be created from any thread at any time,
    //  * queries and body changes may run from any thread concurrently, except while step() runs,
    //  * step() is called once per frame by the game thread (World::tick).
    class PhysicsScene
    {
    public:
        explicit PhysicsScene(const PhysicsSettings& settings = {});
        ~PhysicsScene();

        PhysicsScene(const PhysicsScene&) = delete;
        PhysicsScene& operator=(const PhysicsScene&) = delete;

        // ---- shapes ----

        Shape create_shape(const ShapeDesc& desc);

        // Triangle mesh (BVH build is slow for big meshes). With a cache key the cooked result is
        // stored in PhysicsSettings::shape_cache_dir; it is rebuilt when the geometry changes.
        Shape create_mesh_shape(const TriangleMeshShape& mesh, std::string_view cache_key = {});

        // ---- bodies ----

        BodyId create_body(const BodyDesc& desc);
        void destroy_body(BodyId body);
        bool is_valid(BodyId body) const;

        BodyTransform get_transform(BodyId body) const;
        void set_transform(BodyId body, const BodyTransform& transform);      // teleport
        void move_kinematic(BodyId body, const BodyTransform& target);        // reached by the next step, pushes bodies

        glm::vec3 get_linear_velocity(BodyId body) const;
        void set_linear_velocity(BodyId body, const glm::vec3& velocity);
        void add_impulse(BodyId body, const glm::vec3& impulse);

        uint64_t get_user_data(BodyId body) const;
        Category get_category(BodyId body) const;

        // ---- characters ----
        // Game thread only, not while step() runs (characters are moved before the step).

        CharacterId create_character(const CharacterDesc& desc);
        void destroy_character(CharacterId character);

        // Moves the character by velocity * dt: collides and slides, climbs steps, snaps to the floor.
        // velocity is the full intended velocity including gravity / jumps.
        CharacterState move_character(CharacterId character, const glm::vec3& velocity, float dt);

        CharacterState get_character_state(CharacterId character) const;
        void set_character_position(CharacterId character, const glm::vec3& position);     // teleport, feet
        void set_character_rotation(CharacterId character, const glm::quat& rotation);

        // the character's rigid body (kinematic), what queries and simulated bodies hit
        BodyId get_character_body(CharacterId character) const;

        // ---- queries ----

        // Closest hit along direction (normalized) within max_distance
        std::optional<Hit> raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance,
                                   const QueryFilter& filter = {}) const;

        // All hits sorted by distance, appended to out
        void raycast_all(const glm::vec3& origin, const glm::vec3& direction, float max_distance,
                         std::vector<Hit>& out, const QueryFilter& filter = {}) const;

        // Moves a shape from transform along direction (normalized), first hit within max_distance
        std::optional<Hit> sweep(const Shape& shape, const BodyTransform& transform,
                                 const glm::vec3& direction, float max_distance,
                                 const QueryFilter& filter = {}) const;

        // Bodies overlapping the shape, appended to out
        void overlap(const Shape& shape, const BodyTransform& transform,
                     std::vector<BodyId>& out, const QueryFilter& filter = {}) const;

        // ---- simulation ----

        void step(float dt);

        // Call after adding a lot of static geometry (level load): rebuilds the broadphase trees
        void optimize();

        void set_gravity(const glm::vec3& gravity);
        glm::vec3 get_gravity() const;

        PhysicsStats get_stats() const;

        // ---- debug ----

        void debug_draw(const DebugDrawSettings& settings,
                        const std::function<void(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color)>& line) const;

    private:
        std::unique_ptr<PhysicsBackend> backend;
    };
}
