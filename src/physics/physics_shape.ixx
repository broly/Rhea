export module physics:shape;

import std.compat;
import glm;

export namespace phys
{
    struct SphereShape
    {
        float radius = 0.5f;
    };

    // Along Y, centered at the origin: total height = 2 * (half_height + radius)
    struct CapsuleShape
    {
        float half_height = 0.5f;
        float radius = 0.3f;
    };

    struct BoxShape
    {
        glm::vec3 half_extents{ 0.5f };
        glm::vec3 center{ 0.0f };           // in body space
    };

    struct ConvexHullShape
    {
        std::vector<glm::vec3> points;
    };

    // Static geometry only (fixed or kinematic bodies)
    struct TriangleMeshShape
    {
        std::vector<glm::vec3> vertices;
        std::vector<uint32_t> indices;      // 3 per triangle
    };

    using ShapeDesc = std::variant<SphereShape, CapsuleShape, BoxShape, ConvexHullShape, TriangleMeshShape>;

    // Content hash for the cooked shape cache (PhysicsScene::load_cached_shape): chain the source data
    // and every cooking parameter, so a change of either rebuilds the shape
    constexpr uint64_t shape_hash_seed = 0xcbf29ce484222325ull;

    inline uint64_t hash_bytes(uint64_t h, const void* data, size_t size)
    {
        const auto* bytes = static_cast<const uint8_t*>(data);
        size_t i = 0;
        for (; i + 8 <= size; i += 8)
        {
            uint64_t word;
            std::memcpy(&word, bytes + i, 8);
            h = (h ^ word) * 0x9e3779b97f4a7c15ull;
            h ^= h >> 29;
        }
        for (; i < size; ++i)
            h = (h ^ bytes[i]) * 0x100000001b3ull;
        return h;
    }

    template<typename T> requires std::is_trivially_copyable_v<T>
    uint64_t hash_value(uint64_t h, const T& value)
    {
        return hash_bytes(h, &value, sizeof(T));
    }

    inline uint64_t hash_mesh(uint64_t h, const TriangleMeshShape& mesh)
    {
        h = hash_bytes(h, mesh.vertices.data(), mesh.vertices.size() * sizeof(glm::vec3));
        h = hash_bytes(h, mesh.indices.data(), mesh.indices.size() * sizeof(uint32_t));
        return h;
    }

    // Backend representation, defined by the backend
    class ShapeData;

    // Immutable, reference counted collision shape, shared between bodies. Created from any thread
    // (PhysicsScene::create_shape) and released when the last body / handle using it is gone.
    class Shape
    {
    public:
        Shape() = default;
        explicit Shape(std::shared_ptr<const ShapeData> in_data) : data(std::move(in_data)) {}

        bool is_valid() const { return data != nullptr; }
        explicit operator bool() const { return is_valid(); }

        const ShapeData* get_data() const { return data.get(); }

    private:
        std::shared_ptr<const ShapeData> data;
    };
}
