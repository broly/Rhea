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
