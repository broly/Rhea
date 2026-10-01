module;

// Jolt Physics backend of the physics module. Jolt is only included here (global module fragment),
// so no Jolt type ever reaches a BMI: switching backends means replacing this file.
#include <Jolt/Jolt.h>

#include <Jolt/RegisterTypes.h>
#include <Jolt/Core/Factory.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Core/JobSystemThreadPool.h>
#include <Jolt/Core/StreamWrapper.h>
#include <Jolt/Physics/PhysicsSettings.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/ObjectLayerPairFilterMask.h>
#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayerInterfaceMask.h>
#include <Jolt/Physics/Collision/BroadPhase/ObjectVsBroadPhaseLayerFilterMask.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/Collision/CollideShape.h>
#include <Jolt/Physics/Collision/CollisionCollectorImpl.h>
#include <Jolt/Physics/Collision/Shape/SphereShape.h>
#include <Jolt/Physics/Collision/Shape/CapsuleShape.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/ConvexHullShape.h>
#include <Jolt/Physics/Collision/Shape/MeshShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Character/CharacterVirtual.h>
#ifdef JPH_DEBUG_RENDERER
#include <Jolt/Renderer/DebugRendererSimple.h>
#endif

#include <cstdarg>

module physics;

import std.compat;
import glm;
import log;
import fixed_string;

import :types;
import :shape;
import :scene;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogPhysics, Log);


// ------------------------------------------------------------------------------------------------
// Shapes
// ------------------------------------------------------------------------------------------------

namespace phys
{
    class ShapeData
    {
    public:
        JPH::RefConst<JPH::Shape> shape;
    };
}

namespace
{
    using namespace phys;

    JPH::Vec3 to_jolt(const glm::vec3& v) { return JPH::Vec3(v.x, v.y, v.z); }
    JPH::Quat to_jolt(const glm::quat& q) { return JPH::Quat(q.x, q.y, q.z, q.w).Normalized(); }
    glm::vec3 to_glm(JPH::Vec3Arg v) { return glm::vec3(v.GetX(), v.GetY(), v.GetZ()); }
    glm::quat to_glm(JPH::QuatArg q) { return glm::quat(q.GetW(), q.GetX(), q.GetY(), q.GetZ()); }
    glm::vec4 to_glm(JPH::ColorArg c) { return glm::vec4(c.r, c.g, c.b, c.a) / 255.0f; }

    JPH::BodyID to_jolt(BodyId id) { return JPH::BodyID(id.value); }
    BodyId to_phys(const JPH::BodyID& id) { return BodyId{ id.GetIndexAndSequenceNumber() }; }

    const JPH::Shape* get_jolt_shape(const Shape& shape)
    {
        return shape.is_valid() ? shape.get_data()->shape.GetPtr() : nullptr;
    }

    Shape make_shape(JPH::RefConst<JPH::Shape> jolt_shape)
    {
        auto data = std::make_shared<ShapeData>();
        data->shape = std::move(jolt_shape);
        return Shape(std::move(data));
    }

    Shape make_shape(const JPH::ShapeSettings::ShapeResult& result, const char* what)
    {
        if (result.HasError())
        {
            LogPhysics.Log<Error>("Failed to create %s: %s", what, result.GetError().c_str());
            return {};
        }
        return make_shape(result.Get());
    }

    std::string sanitize_file_name(std::string_view key)
    {
        std::string name(key);
        for (char& c : name)
            if (!std::isalnum((unsigned char)c) && c != '_' && c != '-' && c != '.')
                c = '_';
        return name;
    }

    // Cooked mesh file: magic, version, content hash, then the Jolt shape (SaveWithChildren)
    constexpr uint32_t cooked_shape_magic = 0x50534852;     // "RHSP"
    constexpr uint32_t cooked_shape_version = 1;
}


// ------------------------------------------------------------------------------------------------
// Layers: category (16 bits) | collision mask (16 bits) = JPH::ObjectLayer
// ------------------------------------------------------------------------------------------------

namespace
{
    // Broadphase trees. Each category lives in the first layer that includes it.
    enum BroadPhaseLayers : uint8_t
    {
        bp_non_moving,      // static_world, voxel
        bp_moving,          // dynamic, character, projectile, debris
        bp_query_only,      // hitbox, trigger (last layer: may be tested against anything)
        bp_count
    };

    constexpr CategoryMask broadphase_categories[bp_count] = {
        Category::static_world | Category::voxel,
        Category::dynamic | Category::character | Category::projectile | Category::debris,
        Category::hitbox | Category::trigger,
    };

    JPH::ObjectLayer make_object_layer(Category category, CategoryMask collides_with)
    {
        return JPH::ObjectLayerPairFilterMask::sGetObjectLayer(static_cast<uint16_t>(category), collides_with.bits);
    }

    Category get_layer_category(JPH::ObjectLayer layer)
    {
        return static_cast<Category>(JPH::ObjectLayerPairFilterMask::sGetGroup(layer));
    }

    // Queries: the category of a body must be in QueryFilter::categories
    class QueryBroadPhaseLayerFilter final : public JPH::BroadPhaseLayerFilter
    {
    public:
        explicit QueryBroadPhaseLayerFilter(CategoryMask in_categories) : categories(in_categories) {}

        bool ShouldCollide(JPH::BroadPhaseLayer layer) const override
        {
            const auto index = (JPH::BroadPhaseLayer::Type)layer;
            return index < bp_count && broadphase_categories[index].intersects(categories);
        }

    private:
        CategoryMask categories;
    };

    class QueryObjectLayerFilter final : public JPH::ObjectLayerFilter
    {
    public:
        explicit QueryObjectLayerFilter(CategoryMask in_categories) : categories(in_categories) {}

        bool ShouldCollide(JPH::ObjectLayer layer) const override
        {
            return categories.contains(get_layer_category(layer));
        }

    private:
        CategoryMask categories;
    };

    class QueryBodyFilter final : public JPH::BodyFilter
    {
    public:
        explicit QueryBodyFilter(const QueryFilter& in_filter) : filter(in_filter) {}

        bool ShouldCollide(const JPH::BodyID& body) const override
        {
            const BodyId id = to_phys(body);
            return std::ranges::find(filter.ignore_bodies, id) == filter.ignore_bodies.end();
        }

        bool ShouldCollideLocked(const JPH::Body& body) const override
        {
            return !filter.accept || filter.accept(to_phys(body.GetID()), body.GetUserData());
        }

    private:
        const QueryFilter& filter;
    };

    // all three filters of a query
    struct QueryFilters
    {
        explicit QueryFilters(const QueryFilter& filter)
            : broadphase(filter.categories), object_layer(filter.categories), body(filter)
        {}

        QueryBroadPhaseLayerFilter broadphase;
        QueryObjectLayerFilter object_layer;
        QueryBodyFilter body;
    };

    struct DebugLine
    {
        glm::vec3 from;
        glm::vec3 to;
        glm::vec4 color;
    };

#ifdef JPH_DEBUG_RENDERER
    // Jolt's debug renderer (a singleton): lines go to `line` while debug_draw runs, otherwise to
    // `recording` (character moves)
    class LineDebugRenderer final : public JPH::DebugRendererSimple
    {
    public:
        using LineFn = std::function<void(const glm::vec3&, const glm::vec3&, const glm::vec4&)>;

        const LineFn* line = nullptr;
        std::vector<DebugLine>* recording = nullptr;

        void DrawLine(JPH::RVec3Arg from, JPH::RVec3Arg to, JPH::ColorArg color) override
        {
            if (line)
                (*line)(to_glm(from), to_glm(to), to_glm(color));
            else if (recording)
                recording->push_back({ to_glm(from), to_glm(to), to_glm(color) });
        }

        void DrawText3D(JPH::RVec3Arg, const std::string_view&, JPH::ColorArg, float) override {}
    };

    class DebugDrawFilter final : public JPH::BodyDrawFilter
    {
    public:
        explicit DebugDrawFilter(const DebugDrawSettings& in_settings) : settings(in_settings) {}

        bool ShouldDraw(const JPH::Body& body) const override
        {
            if (body.IsStatic() ? !settings.fixed_bodies : !settings.moving_bodies)
                return false;
            if (!body.IsStatic())
                return true;        // few, and the character is often farther than max_distance (third person camera)
            const float max_distance_sq = settings.max_distance * settings.max_distance;
            return body.GetWorldSpaceBounds().GetSqDistanceTo(to_jolt(settings.camera_position)) <= max_distance_sq;
        }

    private:
        const DebugDrawSettings& settings;
    };
#endif

    void jolt_trace(const char* format, ...)
    {
        char buffer[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(buffer, sizeof(buffer), format, args);
        va_end(args);
        LogPhysics.Log<Warning>("Jolt: %s", buffer);
    }

#ifdef JPH_ENABLE_ASSERTS
    bool jolt_assert_failed(const char* expression, const char* message, const char* file, JPH::uint line)
    {
        LogPhysics.Log<Error>("Jolt assert %s:%u: (%s) %s", file, line, expression, message ? message : "");
        return true;    // break into the debugger
    }
#endif

    void init_jolt_once()
    {
        static std::once_flag once;
        std::call_once(once, []
        {
            JPH::RegisterDefaultAllocator();
            JPH::Trace = jolt_trace;
            JPH_IF_ENABLE_ASSERTS(JPH::AssertFailed = jolt_assert_failed;)
            JPH::Factory::sInstance = new JPH::Factory();
            JPH::RegisterTypes();
        });
    }
}


// ------------------------------------------------------------------------------------------------
// Backend
// ------------------------------------------------------------------------------------------------

namespace phys
{
    class PhysicsBackend
    {
    public:
        explicit PhysicsBackend(const PhysicsSettings& in_settings)
            : settings(in_settings)
            , broadphase_layers(bp_count)
            , object_vs_broadphase(broadphase_layers)
        {
            for (uint32_t layer = 0; layer < bp_count; ++layer)
                broadphase_layers.ConfigureLayer(JPH::BroadPhaseLayer((JPH::BroadPhaseLayer::Type)layer),
                                                 broadphase_categories[layer].bits, 0);

            temp_allocator = std::make_unique<JPH::TempAllocatorImpl>(32 * 1024 * 1024);

            const uint32_t hardware_threads = std::max(2u, std::thread::hardware_concurrency());
            const int threads = settings.worker_threads > 0 ? (int)settings.worker_threads : (int)hardware_threads - 1;
            job_system = std::make_unique<JPH::JobSystemThreadPool>(
                JPH::cMaxPhysicsJobs, JPH::cMaxPhysicsBarriers, threads);

            system.Init(settings.max_bodies, 0, settings.max_body_pairs, settings.max_contact_constraints,
                        broadphase_layers, object_vs_broadphase, object_pair_filter);
            system.SetGravity(to_jolt(settings.gravity));

            LogPhysics.Log("Jolt Physics: %d worker threads, max %u bodies", threads, settings.max_bodies);
        }

        ~PhysicsBackend()
        {
            characters.clear();

            JPH::BodyInterface& bodies = system.GetBodyInterface();
            JPH::BodyIDVector ids;
            system.GetBodies(ids);
            if (!ids.empty())
            {
                bodies.RemoveBodies(ids.data(), (int)ids.size());
                bodies.DestroyBodies(ids.data(), (int)ids.size());
            }
        }

        JPH::BodyInterface& body_interface() { return system.GetBodyInterface(); }
        const JPH::BodyInterface& body_interface() const { return system.GetBodyInterface(); }
        const JPH::NarrowPhaseQuery& narrow_phase() const { return system.GetNarrowPhaseQuery(); }

        void check_not_stepping() const
        {
            JPH_ASSERT(!stepping.load(std::memory_order_relaxed), "physics accessed while step() runs");
        }

        PhysicsSettings settings;

        JPH::BroadPhaseLayerInterfaceMask broadphase_layers;
        JPH::ObjectVsBroadPhaseLayerFilterMask object_vs_broadphase;
        JPH::ObjectLayerPairFilterMask object_pair_filter;

        std::unique_ptr<JPH::TempAllocatorImpl> temp_allocator;
        std::unique_ptr<JPH::JobSystemThreadPool> job_system;
        JPH::PhysicsSystem system;

        // step
        float accumulator = 0.0f;
        std::atomic<bool> stepping = false;
        uint32_t steps_last_frame = 0;
        float step_ms = 0.0f;

        // move_kinematic targets, applied by the next step()
        std::mutex kinematic_mutex;
        std::unordered_map<uint32_t, BodyTransform> kinematic_targets;
        std::vector<JPH::BodyID> kinematic_moved;

        std::atomic<uint32_t> shapes_cooked = 0;
        std::atomic<uint32_t> shapes_from_cache = 0;

        // characters (game thread)
        struct CharacterSlot
        {
            JPH::Ref<JPH::CharacterVirtual> character;
            JPH::CharacterVirtual::ExtendedUpdateSettings update_settings;
            CategoryMask blocked_by;
            uint32_t generation = 0;

            float radius = 0.0f;
            float step_height = 0.0f;
            float corner_slide = 0.0f;
            bool blocked = false;       // the last move got less than half of the way
        };
        std::vector<CharacterSlot> characters;
        std::vector<uint32_t> free_characters;

        // move_character cost, accumulated until the next step()
        float character_ms = 0.0f;
        uint32_t character_contacts = 0;
        float character_ms_last_frame = 0.0f;
        uint32_t character_contacts_last_frame = 0;

        CharacterSlot* find_character(CharacterId id);

#ifdef JPH_DEBUG_RENDERER
        mutable std::unique_ptr<LineDebugRenderer> debug_renderer;

        LineDebugRenderer& get_debug_renderer() const
        {
            if (!debug_renderer)
                debug_renderer = std::make_unique<LineDebugRenderer>();
            return *debug_renderer;
        }
#endif

        // set_character_debug: lines of the move_character calls since the last step(), and of the
        // calls before it (what debug_draw shows)
        bool character_debug = false;
        std::vector<DebugLine> character_lines;
        std::vector<DebugLine> character_lines_shown;
    };


    PhysicsScene::PhysicsScene(const PhysicsSettings& settings)
    {
        // before any Jolt object: the backend's members already allocate through Jolt's allocator
        init_jolt_once();
        backend = std::make_unique<PhysicsBackend>(settings);
    }

    PhysicsScene::~PhysicsScene() = default;


    // ---- shapes ----

    Shape PhysicsScene::create_shape(const ShapeDesc& desc)
    {
        return std::visit([this] <typename T> (const T& d) -> Shape
        {
            if constexpr (std::is_same_v<T, SphereShape>)
            {
                return make_shape(new JPH::SphereShape(d.radius));
            }
            else if constexpr (std::is_same_v<T, CapsuleShape>)
            {
                return make_shape(new JPH::CapsuleShape(d.half_height, d.radius));
            }
            else if constexpr (std::is_same_v<T, BoxShape>)
            {
                const float min_half_extent = std::min({ d.half_extents.x, d.half_extents.y, d.half_extents.z });
                const float convex_radius = std::min(JPH::cDefaultConvexRadius, 0.5f * min_half_extent);
                JPH::RefConst<JPH::Shape> box = new JPH::BoxShape(to_jolt(d.half_extents), convex_radius);
                if (d.center == glm::vec3(0.0f))
                    return make_shape(box);
                return make_shape(JPH::RotatedTranslatedShapeSettings(to_jolt(d.center), JPH::Quat::sIdentity(), box).Create(), "offset box");
            }
            else if constexpr (std::is_same_v<T, ConvexHullShape>)
            {
                JPH::Array<JPH::Vec3> points;
                points.reserve(d.points.size());
                for (const glm::vec3& p : d.points)
                    points.push_back(to_jolt(p));
                return make_shape(JPH::ConvexHullShapeSettings(points).Create(), "convex hull");
            }
            else if constexpr (std::is_same_v<T, HeightFieldShape>)
            {
                if (d.sample_count < 8 || d.heights.size() != size_t(d.sample_count) * d.sample_count)
                    return {};
                JPH::HeightFieldShapeSettings settings(d.heights.data(), to_jolt(d.offset),
                    JPH::Vec3(d.spacing, 1.0f, d.spacing), d.sample_count);
                settings.mBlockSize = 4;
                settings.mBitsPerSample = settings.CalculateBitsPerSampleForError(d.max_error);
                return make_shape(settings.Create(), "height field");
            }
            else
            {
                return create_mesh_shape(d);
            }
        }, desc);
    }

    Shape PhysicsScene::create_mesh_shape(const TriangleMeshShape& mesh, std::string_view cache_key)
    {
        if (mesh.indices.size() < 3 || mesh.vertices.empty())
            return {};

        const uint64_t content_hash = cache_key.empty() ? 0 : hash_mesh(shape_hash_seed, mesh);
        if (Shape cached = load_cached_shape(cache_key, content_hash))
            return cached;

        JPH::VertexList vertices;
        vertices.reserve(mesh.vertices.size());
        for (const glm::vec3& v : mesh.vertices)
            vertices.push_back(JPH::Float3(v.x, v.y, v.z));

        JPH::IndexedTriangleList triangles;
        triangles.reserve(mesh.indices.size() / 3);
        for (size_t i = 0; i + 2 < mesh.indices.size(); i += 3)
            triangles.push_back(JPH::IndexedTriangle(mesh.indices[i], mesh.indices[i + 1], mesh.indices[i + 2]));

        JPH::MeshShapeSettings mesh_settings(std::move(vertices), std::move(triangles));
        Shape shape = make_shape(mesh_settings.Create(), "triangle mesh");
        if (shape && !cache_key.empty())
            store_cached_shape(cache_key, content_hash, shape);
        return shape;
    }

    Shape PhysicsScene::create_scaled_shape(const Shape& shape, glm::vec3 scale)
    {
        const JPH::Shape* jolt_shape = get_jolt_shape(shape);
        if (!jolt_shape)
            return {};
        if (scale == glm::vec3(1.0f))
            return shape;
        return make_shape(jolt_shape->ScaleShape(to_jolt(scale)), "scaled shape");
    }

    Shape PhysicsScene::load_cached_shape(std::string_view cache_key, uint64_t hash) const
    {
        if (cache_key.empty() || backend->settings.shape_cache_dir.empty())
            return {};

        const std::filesystem::path cache_file = backend->settings.shape_cache_dir / (sanitize_file_name(cache_key) + ".jshape");
        std::ifstream in(cache_file, std::ios::binary);
        if (!in)
            return {};

        uint32_t magic = 0, version = 0;
        uint64_t stored_hash = 0;
        in.read(reinterpret_cast<char*>(&magic), sizeof(magic));
        in.read(reinterpret_cast<char*>(&version), sizeof(version));
        in.read(reinterpret_cast<char*>(&stored_hash), sizeof(stored_hash));
        if (!in || magic != cooked_shape_magic || version != cooked_shape_version || stored_hash != hash)
            return {};

        JPH::StreamInWrapper stream(in);
        JPH::Shape::IDToShapeMap shape_map;
        JPH::Shape::IDToMaterialMap material_map;
        JPH::Shape::ShapeResult result = JPH::Shape::sRestoreWithChildren(stream, shape_map, material_map);
        if (!result.IsValid())
            return {};

        backend->shapes_from_cache++;
        return make_shape(result.Get());
    }

    void PhysicsScene::store_cached_shape(std::string_view cache_key, uint64_t hash, const Shape& shape) const
    {
        backend->shapes_cooked++;
        if (cache_key.empty() || backend->settings.shape_cache_dir.empty() || !shape)
            return;

        const std::filesystem::path cache_file = backend->settings.shape_cache_dir / (sanitize_file_name(cache_key) + ".jshape");
        std::error_code ec;
        std::filesystem::create_directories(cache_file.parent_path(), ec);

        // write next to the target and rename: a concurrent reader never sees a partial file
        std::filesystem::path temp_file = cache_file;
        temp_file += std::format(".{}.tmp", std::hash<std::thread::id>{}(std::this_thread::get_id()));
        {
            std::ofstream out(temp_file, std::ios::binary | std::ios::trunc);
            out.write(reinterpret_cast<const char*>(&cooked_shape_magic), sizeof(cooked_shape_magic));
            out.write(reinterpret_cast<const char*>(&cooked_shape_version), sizeof(cooked_shape_version));
            out.write(reinterpret_cast<const char*>(&hash), sizeof(hash));
            JPH::StreamOutWrapper stream(out);
            JPH::Shape::ShapeToIDMap shape_map;
            JPH::Shape::MaterialToIDMap material_map;
            get_jolt_shape(shape)->SaveWithChildren(stream, shape_map, material_map);
        }
        std::filesystem::rename(temp_file, cache_file, ec);
        if (ec)
        {
            LogPhysics.Log<Warning>("Can't write cooked shape '%s': %s", cache_file.string().c_str(), ec.message().c_str());
            std::filesystem::remove(temp_file, ec);
        }
    }


    // ---- bodies ----

    BodyId PhysicsScene::create_body(const BodyDesc& desc)
    {
        backend->check_not_stepping();

        const JPH::Shape* shape = get_jolt_shape(desc.shape);
        if (!shape)
        {
            LogPhysics.Log<Error>("create_body: no shape");
            return {};
        }

        static constexpr JPH::EMotionType motion_types[] = {
            JPH::EMotionType::Static, JPH::EMotionType::Kinematic, JPH::EMotionType::Dynamic };

        const CategoryMask collides_with = desc.collides_with.value_or(default_collision_mask(desc.category));

        JPH::BodyCreationSettings body_settings(
            shape, to_jolt(desc.position), to_jolt(desc.rotation),
            motion_types[(size_t)desc.motion], make_object_layer(desc.category, collides_with));

        body_settings.mFriction = desc.friction;
        body_settings.mRestitution = desc.restitution;
        body_settings.mLinearVelocity = to_jolt(desc.linear_velocity);
        body_settings.mAngularVelocity = to_jolt(desc.angular_velocity);
        body_settings.mUserData = desc.user_data;
        body_settings.mIsSensor = desc.category == Category::trigger;
        body_settings.mMotionQuality = desc.continuous_collision ? JPH::EMotionQuality::LinearCast : JPH::EMotionQuality::Discrete;
        // static bodies may later become kinematic (e.g. level pieces moved by scripts)
        body_settings.mAllowDynamicOrKinematic = desc.motion == Motion::fixed && desc.category != Category::static_world;

        if (desc.motion == Motion::dynamic && desc.mass > 0.0f)
        {
            body_settings.mOverrideMassProperties = JPH::EOverrideMassProperties::CalculateInertia;
            body_settings.mMassPropertiesOverride.mMass = desc.mass;
        }

        const JPH::EActivation activation = desc.start_active && desc.motion != Motion::fixed
            ? JPH::EActivation::Activate : JPH::EActivation::DontActivate;

        const JPH::BodyID id = backend->body_interface().CreateAndAddBody(body_settings, activation);
        if (id.IsInvalid())
        {
            LogPhysics.Log<Error>("create_body: out of bodies (max %u)", backend->settings.max_bodies);
            return {};
        }
        return to_phys(id);
    }

    void PhysicsScene::destroy_body(BodyId body)
    {
        backend->check_not_stepping();
        if (!is_valid(body))
            return;

        JPH::BodyInterface& bodies = backend->body_interface();
        bodies.RemoveBody(to_jolt(body));
        bodies.DestroyBody(to_jolt(body));

        std::lock_guard lock(backend->kinematic_mutex);
        backend->kinematic_targets.erase(body.value);
    }

    bool PhysicsScene::is_valid(BodyId body) const
    {
        if (!body.is_valid())
            return false;
        JPH::BodyLockRead lock(backend->system.GetBodyLockInterface(), to_jolt(body));
        return lock.Succeeded();
    }

    BodyTransform PhysicsScene::get_transform(BodyId body) const
    {
        JPH::RVec3 position;
        JPH::Quat rotation;
        backend->body_interface().GetPositionAndRotation(to_jolt(body), position, rotation);
        return { to_glm(position), to_glm(rotation) };
    }

    void PhysicsScene::set_transform(BodyId body, const BodyTransform& transform)
    {
        backend->check_not_stepping();
        backend->body_interface().SetPositionAndRotation(
            to_jolt(body), to_jolt(transform.position), to_jolt(transform.rotation), JPH::EActivation::Activate);
    }

    void PhysicsScene::move_kinematic(BodyId body, const BodyTransform& target)
    {
        std::lock_guard lock(backend->kinematic_mutex);
        backend->kinematic_targets[body.value] = target;
    }

    glm::vec3 PhysicsScene::get_linear_velocity(BodyId body) const
    {
        return to_glm(backend->body_interface().GetLinearVelocity(to_jolt(body)));
    }

    void PhysicsScene::set_linear_velocity(BodyId body, const glm::vec3& velocity)
    {
        backend->check_not_stepping();
        backend->body_interface().SetLinearVelocity(to_jolt(body), to_jolt(velocity));
    }

    void PhysicsScene::add_impulse(BodyId body, const glm::vec3& impulse)
    {
        backend->check_not_stepping();
        backend->body_interface().AddImpulse(to_jolt(body), to_jolt(impulse));
    }

    uint64_t PhysicsScene::get_user_data(BodyId body) const
    {
        return backend->body_interface().GetUserData(to_jolt(body));
    }

    Category PhysicsScene::get_category(BodyId body) const
    {
        return get_layer_category(backend->body_interface().GetObjectLayer(to_jolt(body)));
    }


    // ---- characters ----

    namespace
    {
        // CharacterId: slot index (low bits) + generation of the slot (high bits), detects stale ids
        constexpr uint32_t character_index_bits = 20;
        constexpr uint32_t character_index_mask = (1u << character_index_bits) - 1;

        CharacterId make_character_id(uint32_t index, uint32_t generation)
        {
            return CharacterId{ index | (generation << character_index_bits) };
        }

        GroundState to_ground_state(JPH::CharacterBase::EGroundState state)
        {
            switch (state)
            {
            case JPH::CharacterBase::EGroundState::OnGround:      return GroundState::on_ground;
            case JPH::CharacterBase::EGroundState::OnSteepGround: return GroundState::on_steep_ground;
            case JPH::CharacterBase::EGroundState::NotSupported:  return GroundState::not_supported;
            case JPH::CharacterBase::EGroundState::InAir:         return GroundState::in_air;
            }
            return GroundState::in_air;
        }

        CharacterState make_character_state(const JPH::CharacterVirtual& character)
        {
            CharacterState state;
            state.position = to_glm(character.GetPosition());
            state.velocity = to_glm(character.GetLinearVelocity());
            state.ground = to_ground_state(character.GetGroundState());
            state.ground_normal = to_glm(character.GetGroundNormal());
            state.ground_velocity = to_glm(character.GetGroundVelocity());
            state.ground_body = to_phys(character.GetGroundBodyID());
            state.contacts = (uint32_t)character.GetActiveContacts().size();
            return state;
        }

        // The last move was blocked by something taller than a step on one side only (a door jamb, the edge
        // of a column) whose face is square to the movement, so collide-and-slide has nothing to slide along:
        // turns the horizontal velocity 45 degrees to the free side. Probes: the capsule raised by the step
        // height (steps don't count) swept forward by its radius, from its position and shifted sideways.
        glm::vec3 steer_around_corners(const PhysicsBackend& b, const PhysicsBackend::CharacterSlot& slot, const glm::vec3& velocity)
        {
            const JPH::CharacterVirtual& c = *slot.character;
            const JPH::Vec3 up = c.GetUp();
            const JPH::Vec3 desired = to_jolt(velocity);
            const JPH::Vec3 horizontal = desired - desired.Dot(up) * up;
            const float speed = horizontal.Length();
            if (speed < 1.0e-3f)
                return velocity;
            const JPH::Vec3 forward = horizontal / speed;
            const JPH::Vec3 side = up.Cross(forward).Normalized();

            const QueryBroadPhaseLayerFilter broadphase_filter(slot.blocked_by);
            const QueryObjectLayerFilter object_layer_filter(slot.blocked_by);
            const JPH::IgnoreSingleBodyFilter body_filter(c.GetInnerBodyID());
            const JPH::RMat44 raised = c.GetCenterOfMassTransform().PostTranslated(up * slot.step_height);

            auto is_free = [&] (float shift)
            {
                const JPH::RMat44 from = raised.PostTranslated(side * shift);
                const JPH::RShapeCast cast(c.GetShape(), JPH::Vec3::sOne(), from, forward * slot.radius);
                JPH::ShapeCastSettings settings;
                settings.mBackFaceModeTriangles = JPH::EBackFaceMode::CollideWithBackFaces;
                JPH::AnyHitCollisionCollector<JPH::CastShapeCollector> collector;
                b.narrow_phase().CastShape(cast, settings, JPH::RVec3::sZero(), collector,
                                           broadphase_filter, object_layer_filter, body_filter);
                const bool free = !collector.HadHit();
#ifdef JPH_DEBUG_RENDERER
                if (b.character_debug)
                    b.get_debug_renderer().DrawArrow(from.GetTranslation(), from.GetTranslation() + forward * slot.radius,
                                                     free ? JPH::Color::sGreen : JPH::Color::sRed, 0.03f);
#endif
                return free;
            };

            if (is_free(0.0f))
                return velocity;        // nothing or a step ahead
            const float shift = slot.corner_slide * slot.radius;
            const bool free_positive = is_free(shift);
            const bool free_negative = is_free(-shift);
            if (free_positive == free_negative)
                return velocity;        // a wall across the way, or an inside corner
            const JPH::Vec3 steered = (forward + (free_positive ? side : -side)).Normalized() * speed;
            return to_glm(steered + desired.Dot(up) * up);
        }
    }

    PhysicsBackend::CharacterSlot* PhysicsBackend::find_character(CharacterId id)
    {
        if (!id.is_valid())
            return nullptr;
        const uint32_t index = id.value & character_index_mask;
        if (index >= characters.size())
            return nullptr;
        CharacterSlot& slot = characters[index];
        if (!slot.character || make_character_id(index, slot.generation) != id)
            return nullptr;
        return &slot;
    }

    CharacterId PhysicsScene::create_character(const CharacterDesc& desc)
    {
        backend->check_not_stepping();

        const float radius = std::max(desc.radius, 0.01f);
        const float half_height = std::max(0.5f * desc.height - radius, 0.01f);     // of the cylinder part
        const CategoryMask blocked_by = desc.blocked_by.value_or(default_collision_mask(desc.category));

        JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(half_height, radius);

        JPH::CharacterVirtualSettings settings;
        settings.mShape = capsule;
        // the character position is at the feet
        settings.mShapeOffset = JPH::Vec3(0.0f, half_height + radius, 0.0f);
        // contacts below the center of the lower sphere support the character
        settings.mSupportingVolume = JPH::Plane(JPH::Vec3::sAxisY(), -radius);
        settings.mMaxSlopeAngle = JPH::DegreesToRadians(desc.max_slope_degrees);
        settings.mMass = desc.mass;
        settings.mMaxStrength = desc.max_push_force;
        settings.mEnhancedInternalEdgeRemoval = true;     // no bumps on triangle edges of level meshes
        // rigid body for queries and simulated bodies, slightly smaller so it doesn't touch what the character stands on
        settings.mInnerBodyShape = new JPH::CapsuleShape(half_height, std::max(radius - 0.02f, 0.01f));
        settings.mInnerBodyLayer = make_object_layer(desc.category, blocked_by);

        PhysicsBackend::CharacterSlot slot;
        slot.character = new JPH::CharacterVirtual(&settings, to_jolt(desc.position), to_jolt(desc.rotation),
                                                   desc.user_data, &backend->system);
        slot.blocked_by = blocked_by;
        slot.radius = radius;
        slot.step_height = desc.step_height;
        slot.corner_slide = desc.corner_slide;
        slot.update_settings.mWalkStairsStepUp = JPH::Vec3(0.0f, desc.step_height, 0.0f);
        slot.update_settings.mStickToFloorStepDown = JPH::Vec3(0.0f, -desc.stick_to_floor, 0.0f);

        uint32_t index;
        if (!backend->free_characters.empty())
        {
            index = backend->free_characters.back();
            backend->free_characters.pop_back();
            slot.generation = backend->characters[index].generation;
            backend->characters[index] = std::move(slot);
        }
        else
        {
            index = (uint32_t)backend->characters.size();
            backend->characters.push_back(std::move(slot));
        }
        return make_character_id(index, backend->characters[index].generation);
    }

    void PhysicsScene::destroy_character(CharacterId character)
    {
        backend->check_not_stepping();

        PhysicsBackend::CharacterSlot* slot = backend->find_character(character);
        if (!slot)
            return;
        slot->character = nullptr;      // removes the inner body
        slot->generation = (slot->generation + 1) & (0xffffffffu >> character_index_bits);
        backend->free_characters.push_back(character.value & character_index_mask);
    }

    CharacterState PhysicsScene::move_character(CharacterId character, const glm::vec3& velocity, float dt)
    {
        backend->check_not_stepping();

        PhysicsBackend::CharacterSlot* slot = backend->find_character(character);
        if (!slot)
            return {};

        const auto start_time = std::chrono::steady_clock::now();
        JPH::CharacterVirtual& c = *slot->character;
        if (dt > 0.0f)
        {
#ifdef JPH_DEBUG_RENDERER
            // Jolt draws its stair walking / floor sticking sweeps through the debug renderer
            if (backend->character_debug)
                backend->get_debug_renderer().recording = &backend->character_lines;
            JPH::CharacterVirtual::sDrawWalkStairs = backend->character_debug;
            JPH::CharacterVirtual::sDrawStickToFloor = backend->character_debug;
#endif

            const glm::vec3 steered = slot->blocked && slot->corner_slide > 0.0f
                ? steer_around_corners(*backend, *slot, velocity) : velocity;
            c.SetLinearVelocity(to_jolt(steered));

            const JPH::RVec3 old_position = c.GetPosition();
            const QueryBroadPhaseLayerFilter broadphase_filter(slot->blocked_by);
            const QueryObjectLayerFilter object_layer_filter(slot->blocked_by);
            c.ExtendedUpdate(dt, backend->system.GetGravity(), slot->update_settings,
                             broadphase_filter, object_layer_filter, {}, {}, *backend->temp_allocator);

            // blocked: moved less than half of the way (along the horizontal movement)
            const JPH::Vec3 up = c.GetUp();
            JPH::Vec3 desired = to_jolt(steered) * dt;
            desired -= desired.Dot(up) * up;
            const float desired_length = desired.Length();
            const float achieved = JPH::Vec3(c.GetPosition() - old_position).Dot(desired) / std::max(desired_length, 1.0e-6f);
            slot->blocked = desired_length > 1.0e-4f && achieved < 0.5f * desired_length;

#ifdef JPH_DEBUG_RENDERER
            JPH::CharacterVirtual::sDrawWalkStairs = false;
            JPH::CharacterVirtual::sDrawStickToFloor = false;
            if (backend->debug_renderer)
                backend->debug_renderer->recording = nullptr;
#endif
        }

        // ground velocity for the next frame's velocity (moving platforms)
        c.UpdateGroundVelocity();
        const CharacterState state = make_character_state(c);

        backend->character_ms += std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start_time).count();
        backend->character_contacts = std::max(backend->character_contacts, state.contacts);
        return state;
    }

    bool PhysicsScene::set_character_height(CharacterId character, float height)
    {
        backend->check_not_stepping();
        PhysicsBackend::CharacterSlot* slot = backend->find_character(character);
        if (!slot)
            return false;

        JPH::CharacterVirtual& c = *slot->character;
        const float radius = slot->radius;
        const float half_height = std::max(0.5f * height - radius, 0.01f);
        JPH::RefConst<JPH::Shape> capsule = new JPH::CapsuleShape(half_height, radius);

        // feet stay where they are: the shape moves with its new half height
        const JPH::Vec3 old_offset = c.GetShapeOffset();
        c.SetShapeOffset(JPH::Vec3(0.0f, half_height + radius, 0.0f));
        const QueryBroadPhaseLayerFilter broadphase_filter(slot->blocked_by);
        const QueryObjectLayerFilter object_layer_filter(slot->blocked_by);
        const float max_penetration = 1.5f * backend->system.GetPhysicsSettings().mPenetrationSlop;
        if (!c.SetShape(capsule, max_penetration, broadphase_filter, object_layer_filter, {}, {}, *backend->temp_allocator))
        {
            c.SetShapeOffset(old_offset);
            return false;
        }
        c.SetInnerBodyShape(new JPH::CapsuleShape(half_height, std::max(radius - 0.02f, 0.01f)));
        return true;
    }

    CharacterState PhysicsScene::get_character_state(CharacterId character) const
    {
        PhysicsBackend::CharacterSlot* slot = backend->find_character(character);
        return slot ? make_character_state(*slot->character) : CharacterState{};
    }

    void PhysicsScene::set_character_position(CharacterId character, const glm::vec3& position)
    {
        backend->check_not_stepping();
        if (PhysicsBackend::CharacterSlot* slot = backend->find_character(character))
        {
            slot->character->SetPosition(to_jolt(position));
            slot->character->SetLinearVelocity(JPH::Vec3::sZero());
        }
    }

    void PhysicsScene::set_character_rotation(CharacterId character, const glm::quat& rotation)
    {
        backend->check_not_stepping();
        if (PhysicsBackend::CharacterSlot* slot = backend->find_character(character))
            slot->character->SetRotation(to_jolt(rotation));
    }

    BodyId PhysicsScene::get_character_body(CharacterId character) const
    {
        PhysicsBackend::CharacterSlot* slot = backend->find_character(character);
        return slot ? to_phys(slot->character->GetInnerBodyID()) : BodyId{};
    }


    // ---- queries ----

    namespace
    {
        // hit from a ray result, the normal needs the body
        Hit make_ray_hit(const PhysicsBackend& backend, const JPH::RRayCast& ray, const JPH::RayCastResult& result,
                         float max_distance)
        {
            Hit hit;
            hit.body = to_phys(result.mBodyID);
            hit.distance = result.mFraction * max_distance;
            hit.sub_shape = result.mSubShapeID2.GetValue();

            const JPH::RVec3 point = ray.GetPointOnRay(result.mFraction);
            hit.position = to_glm(point);

            JPH::BodyLockRead lock(backend.system.GetBodyLockInterface(), result.mBodyID);
            if (lock.Succeeded())
            {
                const JPH::Body& body = lock.GetBody();
                hit.user_data = body.GetUserData();
                hit.category = get_layer_category(body.GetObjectLayer());
                JPH::Vec3 normal = body.GetWorldSpaceSurfaceNormal(result.mSubShapeID2, point);
                // back face hits: face the ray
                if (normal.Dot(ray.mDirection) > 0.0f)
                    normal = -normal;
                hit.normal = to_glm(normal);
            }
            return hit;
        }

        JPH::RayCastSettings make_ray_settings(const QueryFilter& filter)
        {
            JPH::RayCastSettings ray_settings;
            ray_settings.SetBackFaceMode(filter.hit_back_faces ? JPH::EBackFaceMode::CollideWithBackFaces
                                                               : JPH::EBackFaceMode::IgnoreBackFaces);
            ray_settings.mTreatConvexAsSolid = true;
            return ray_settings;
        }

        JPH::RMat44 make_center_of_mass_transform(const JPH::Shape* shape, const BodyTransform& transform)
        {
            return JPH::RMat44::sRotationTranslation(to_jolt(transform.rotation), to_jolt(transform.position))
                * JPH::Mat44::sTranslation(shape->GetCenterOfMass());
        }
    }

    std::optional<Hit> PhysicsScene::raycast(const glm::vec3& origin, const glm::vec3& direction, float max_distance,
                                             const QueryFilter& filter) const
    {
        backend->check_not_stepping();

        const JPH::RRayCast ray(to_jolt(origin), to_jolt(direction * max_distance));
        const QueryFilters filters(filter);

        JPH::ClosestHitCollisionCollector<JPH::CastRayCollector> collector;
        backend->narrow_phase().CastRay(ray, make_ray_settings(filter), collector,
                                        filters.broadphase, filters.object_layer, filters.body);
        if (!collector.HadHit())
            return std::nullopt;
        return make_ray_hit(*backend, ray, collector.mHit, max_distance);
    }

    void PhysicsScene::raycast_all(const glm::vec3& origin, const glm::vec3& direction, float max_distance,
                                   std::vector<Hit>& out, const QueryFilter& filter) const
    {
        backend->check_not_stepping();

        const JPH::RRayCast ray(to_jolt(origin), to_jolt(direction * max_distance));
        const QueryFilters filters(filter);

        JPH::AllHitCollisionCollector<JPH::CastRayCollector> collector;
        backend->narrow_phase().CastRay(ray, make_ray_settings(filter), collector,
                                        filters.broadphase, filters.object_layer, filters.body);
        collector.Sort();
        for (const JPH::RayCastResult& result : collector.mHits)
            out.push_back(make_ray_hit(*backend, ray, result, max_distance));
    }

    std::optional<Hit> PhysicsScene::sweep(const Shape& shape, const BodyTransform& transform,
                                           const glm::vec3& direction, float max_distance,
                                           const QueryFilter& filter) const
    {
        backend->check_not_stepping();

        const JPH::Shape* jolt_shape = get_jolt_shape(shape);
        if (!jolt_shape)
            return std::nullopt;

        const JPH::RShapeCast shape_cast(jolt_shape, JPH::Vec3::sOne(),
                                         make_center_of_mass_transform(jolt_shape, transform),
                                         to_jolt(direction * max_distance));

        JPH::ShapeCastSettings cast_settings;
        cast_settings.mReturnDeepestPoint = true;
        cast_settings.mBackFaceModeTriangles = filter.hit_back_faces ? JPH::EBackFaceMode::CollideWithBackFaces
                                                                     : JPH::EBackFaceMode::IgnoreBackFaces;

        const QueryFilters filters(filter);
        JPH::ClosestHitCollisionCollector<JPH::CastShapeCollector> collector;
        backend->narrow_phase().CastShape(shape_cast, cast_settings, JPH::RVec3::sZero(), collector,
                                          filters.broadphase, filters.object_layer, filters.body);
        if (!collector.HadHit())
            return std::nullopt;

        const JPH::ShapeCastResult& result = collector.mHit;

        Hit hit;
        hit.body = to_phys(result.mBodyID2);
        hit.distance = result.mFraction * max_distance;
        hit.sub_shape = result.mSubShapeID2.GetValue();
        hit.position = to_glm(result.mContactPointOn2);
        hit.start_penetrating = result.mFraction <= 0.0f && result.mPenetrationDepth > 0.0f;

        // penetration axis points from the swept shape into the hit body
        const JPH::Vec3 axis = result.mPenetrationAxis;
        hit.normal = axis.LengthSq() > 1e-12f ? to_glm(-axis.Normalized()) : -direction;

        JPH::BodyLockRead lock(backend->system.GetBodyLockInterface(), result.mBodyID2);
        if (lock.Succeeded())
        {
            hit.user_data = lock.GetBody().GetUserData();
            hit.category = get_layer_category(lock.GetBody().GetObjectLayer());
        }
        return hit;
    }

    void PhysicsScene::overlap(const Shape& shape, const BodyTransform& transform,
                               std::vector<BodyId>& out, const QueryFilter& filter) const
    {
        backend->check_not_stepping();

        const JPH::Shape* jolt_shape = get_jolt_shape(shape);
        if (!jolt_shape)
            return;

        JPH::CollideShapeSettings collide_settings;
        collide_settings.mBackFaceMode = JPH::EBackFaceMode::CollideWithBackFaces;

        const QueryFilters filters(filter);
        JPH::AllHitCollisionCollector<JPH::CollideShapeCollector> collector;
        backend->narrow_phase().CollideShape(jolt_shape, JPH::Vec3::sOne(),
                                             make_center_of_mass_transform(jolt_shape, transform),
                                             collide_settings, JPH::RVec3::sZero(), collector,
                                             filters.broadphase, filters.object_layer, filters.body);

        const size_t first = out.size();
        for (const JPH::CollideShapeResult& result : collector.mHits)
        {
            const BodyId id = to_phys(result.mBodyID2);
            if (std::find(out.begin() + (ptrdiff_t)first, out.end(), id) == out.end())
                out.push_back(id);
        }
    }


    // ---- simulation ----

    void PhysicsScene::step(float dt)
    {
        const auto start_time = std::chrono::steady_clock::now();
        PhysicsBackend& b = *backend;

        b.character_ms_last_frame = std::exchange(b.character_ms, 0.0f);
        b.character_contacts_last_frame = std::exchange(b.character_contacts, 0u);
        if (!b.character_lines.empty())
        {
            b.character_lines_shown.swap(b.character_lines);
            b.character_lines.clear();
        }

        const float fixed_dt = b.settings.fixed_timestep;
        b.accumulator = std::min(b.accumulator + dt, fixed_dt * (float)b.settings.max_steps_per_frame);

        uint32_t steps = 0;
        while (b.accumulator >= fixed_dt)
        {
            b.accumulator -= fixed_dt;
            steps++;
        }
        b.steps_last_frame = steps;

        if (steps > 0)
        {
            JPH::BodyInterface& bodies = b.body_interface();

            // kinematic bodies reach their targets at the end of this frame's steps
            {
                std::lock_guard lock(b.kinematic_mutex);
                for (const auto& [id, target] : b.kinematic_targets)
                {
                    const JPH::BodyID body(id);
                    bodies.MoveKinematic(body, to_jolt(target.position), to_jolt(target.rotation), fixed_dt * (float)steps);
                    b.kinematic_moved.push_back(body);
                }
                b.kinematic_targets.clear();
            }

            b.stepping.store(true, std::memory_order_relaxed);
            for (uint32_t i = 0; i < steps; ++i)
            {
                const JPH::EPhysicsUpdateError error = b.system.Update(fixed_dt, 1, b.temp_allocator.get(), b.job_system.get());
                if (error != JPH::EPhysicsUpdateError::None)
                    LogPhysics.Log<Warning>("Physics update error %u (raise max_body_pairs / max_contact_constraints)", (uint32_t)error);
            }
            b.stepping.store(false, std::memory_order_relaxed);

            // no new target next frame: the body stops instead of drifting on
            for (const JPH::BodyID& body : b.kinematic_moved)
            {
                if (!bodies.IsAdded(body))
                    continue;
                bodies.SetLinearAndAngularVelocity(body, JPH::Vec3::sZero(), JPH::Vec3::sZero());
            }
            b.kinematic_moved.clear();
        }

        b.step_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start_time).count();
    }

    void PhysicsScene::optimize()
    {
        backend->check_not_stepping();
        backend->system.OptimizeBroadPhase();
    }

    void PhysicsScene::set_gravity(const glm::vec3& gravity)
    {
        backend->system.SetGravity(to_jolt(gravity));
    }

    glm::vec3 PhysicsScene::get_gravity() const
    {
        return to_glm(backend->system.GetGravity());
    }

    PhysicsStats PhysicsScene::get_stats() const
    {
        PhysicsStats stats;
        stats.bodies = backend->system.GetNumBodies();
        stats.active_bodies = backend->system.GetNumActiveBodies(JPH::EBodyType::RigidBody);
        stats.shapes_cooked = backend->shapes_cooked.load();
        stats.shapes_from_cache = backend->shapes_from_cache.load();
        stats.steps_last_frame = backend->steps_last_frame;
        stats.step_ms = backend->step_ms;
        stats.character_ms = backend->character_ms_last_frame;
        stats.character_contacts = backend->character_contacts_last_frame;
        return stats;
    }


    // ---- debug ----

    void PhysicsScene::debug_draw(const DebugDrawSettings& settings,
                                  const std::function<void(const glm::vec3&, const glm::vec3&, const glm::vec4&)>& line) const
    {
#ifdef JPH_DEBUG_RENDERER
        backend->check_not_stepping();

        LineDebugRenderer& renderer = backend->get_debug_renderer();
        renderer.line = &line;
        renderer.SetCameraPos(to_jolt(settings.camera_position));

        JPH::BodyManager::DrawSettings draw_settings;
        draw_settings.mDrawShape = true;
        draw_settings.mDrawShapeWireframe = true;
        draw_settings.mDrawBoundingBox = settings.bounding_boxes;
        draw_settings.mDrawVelocity = settings.velocities;

        const DebugDrawFilter filter(settings);
        backend->system.DrawBodies(draw_settings, &renderer, &filter);

        renderer.NextFrame();
        renderer.line = nullptr;
#endif

        for (const DebugLine& recorded : backend->character_lines_shown)
            line(recorded.from, recorded.to, recorded.color);
    }

    void PhysicsScene::set_character_debug(bool enabled)
    {
        backend->character_debug = enabled;
        if (!enabled)
        {
            backend->character_lines.clear();
            backend->character_lines_shown.clear();
        }
    }
}
