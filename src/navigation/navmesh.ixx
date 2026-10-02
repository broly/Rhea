module;

#include <json/value.h>

export module navigation:navmesh;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import physics;

// Navigation mesh of a level, built with Recast and queried with Detour (recastnavigation main; Unreal uses an
// old fork of the same library). Tiled: the level is cut into square tiles of NavMeshSettings::tile_size cells,
// each built alone from the triangles of the static physics world (terrain heightfield, level meshes, props,
// tree trunks), so a change of the level geometry rebuilds only the tiles it touches:
//
//   NavMeshBounds    (JSON) box around its entity (rotation ignored): the tiles to build. The navigation mesh
//                    exists while at least one is in the level, as the resource nav::NavMesh
//   NavMesh::update  per frame: fixed bodies created / destroyed / moved (phys::PhysicsScene::read_static_changes)
//                    mark tiles dirty; once a tile has been quiet for a moment its triangles are collected on
//                    the game thread (a few ms per frame at most), Recast runs on worker threads, finished tiles
//                    replace the old ones. A tile whose input (triangles + settings) did not change is not rebuilt
//                    at all: built tiles are cached in cache/navmesh/ by the hash of their input.
//   crowd            Detour crowd of the agents (navigation:agents): paths, path following, local avoidance.
//
// One mesh for every agent: eroded by agent_radius, passages lower than agent_height left out. Agents may be a
// bit thinner (the jackals) - not thicker.
export namespace nav
{
    // Recast parameters, meters / degrees. A change rebuilds every tile (console nav.rebuild); cvars nav.build.*
    struct NavMeshSettings
    {
        float cell_size = 0.2f;              // xz resolution of the voxels
        float cell_height = 0.1f;            // y resolution
        float agent_radius = 0.35f;          // walls and edges are kept this far away
        float agent_height = 1.8f;           // lower passages are blocked
        float agent_max_climb = 0.4f;        // steps / ledges walked up and down
        float agent_max_slope = 45.0f;       // degrees
        int tile_size = 128;                 // cells per tile side (25.6 m)
        float region_min_size = 8.0f;        // cells (side of a square): smaller islands are dropped
        float region_merge_size = 20.0f;     // cells: smaller regions merge into bigger ones
        float edge_max_length = 12.0f;       // m, long border edges are split
        float edge_max_error = 1.3f;         // cells, how far simplified borders may stray from the voxels
        float detail_sample_distance = 6.0f; // cells, height detail sampling (0: polygons only)
        float detail_sample_max_error = 1.0f;// cell heights
    };

    // Tiles to build: the box of half_extents around the entity's world position
    struct NavMeshBounds
    {
        [[=rh::edit, =rh::speed<0.5f>]] vec3 half_extents{ 50.0f, 20.0f, 50.0f };
    };

    // State of an agent's move request (NavAgent::status)
    enum class NavStatus : uint8_t
    {
        idle,         // no target
        planning,     // the path is being searched
        moving,       // following a path to the target
        partial,      // following a path that ends as close as it gets: the target is not reachable
        unreachable,  // no path (the target is far from the mesh)
        off_mesh,     // the agent is not on the navigation mesh (not built yet, or it stands outside)
    };

    // Avoidance of other agents: velocity samples per tick (cost)
    enum class AvoidanceQuality : uint8_t { low, medium, good, high };

    struct CrowdAgentParams
    {
        float radius = 0.35f;
        float height = 1.8f;
        float max_speed = 3.5f;
        float max_acceleration = 10.0f;
        float separation = 2.0f;              // pushes away from the neighbours (0: off)
        AvoidanceQuality avoidance = AvoidanceQuality::good;
        bool obstacle = false;                // never steered: only avoided by the others

        friend bool operator==(const CrowdAgentParams&, const CrowdAgentParams&) = default;
    };

    struct CrowdAgentState
    {
        NavStatus status = NavStatus::off_mesh;
        glm::vec3 position{ 0.0f };           // on the mesh
        glm::vec3 desired_velocity{ 0.0f };   // where to go now, other agents avoided, m/s
        glm::vec3 next_corner{ 0.0f };        // the next turn of the path (the target when it is in sight)
        bool has_corner = false;
        // along the path to the target; +inf: farther than the corners the crowd looks ahead
        float distance_to_goal = std::numeric_limits<float>::infinity();
        std::array<glm::vec3, 4> corners{};   // the path ahead (debug)
        uint32_t corner_count = 0;
    };

    struct NavMeshStats
    {
        uint32_t tiles = 0;            // in the bounds
        uint32_t tiles_built = 0;      // with polygons
        uint32_t tiles_dirty = 0;      // waiting for their geometry to settle / to be collected
        uint32_t tiles_building = 0;   // on the worker threads
        uint32_t polygons = 0;
        uint32_t builds = 0;           // tiles built by Recast since the start
        uint32_t cache_hits = 0;       // tiles loaded from cache/navmesh
        float last_build_ms = 0.0f;    // Recast time of the last built tile (one worker)
        float collect_ms = 0.0f;       // game thread time of the last update (triangle collection)
        uint32_t agents = 0;
    };

    struct NavDebugDraw
    {
        glm::vec3 camera{ 0.0f };
        float distance = 40.0f;        // tiles farther from the camera are skipped
        bool polygons = true;          // polygon edges (borders brighter)
        bool tiles = false;            // tile bounds: green built, yellow dirty, orange building, grey empty
        bool agents = true;            // corners ahead, desired velocity
    };

    class NavMeshData;

    // The navigation mesh + crowd of a level (an ECS resource). Game thread only, except the tile builds it runs.
    class NavMesh
    {
    public:
        explicit NavMesh(const NavMeshSettings& settings = {});
        ~NavMesh();

        NavMesh(const NavMesh&) = delete;
        NavMesh& operator=(const NavMesh&) = delete;

        // unique per NavMesh (agents notice a new one)
        uint32_t get_id() const;
        const NavMeshSettings& get_settings() const;

        // ---- building

        // the boxes to build tiles in (NavMeshBounds); tiles outside all of them are dropped
        void set_bounds(std::span<const phys::Bounds> bounds);
        // new settings, every tile rebuilt (from the cache when its input matches)
        void set_settings(const NavMeshSettings& settings);
        // every tile again; ignore_cache: Recast runs for all of them
        void rebuild(bool ignore_cache);
        // Level changes, triangle collection within the time budget, finished tiles. Once per frame.
        void update(const phys::PhysicsScene& physics, float collect_budget_ms);

        NavMeshStats get_stats() const;
        // no dirty or building tile
        bool is_idle() const;

        // ---- queries

        // The closest point of the mesh within extents (half size of the search box) of p
        std::optional<glm::vec3> project(const glm::vec3& p, const glm::vec3& extents = { 1.0f, 2.0f, 1.0f }) const;
        // Corners of the path from -> to (both projected onto the mesh). False: none of the mesh is near either;
        // a target that can't be reached gives the path to the closest point and partial = true.
        bool find_path(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& corners, bool* partial = nullptr) const;

        // ---- crowd (navigation:agents keeps it in sync with the NavAgent components)

        // -1: crowd full
        int32_t add_agent(const glm::vec3& position, const CrowdAgentParams& params);
        void remove_agent(int32_t agent);
        void set_agent_params(int32_t agent, const CrowdAgentParams& params);
        // The body's position (the mover is the physics: the crowd only steers) and velocity, before update_crowd
        void set_agent_position(int32_t agent, const glm::vec3& position, const glm::vec3& velocity);
        // nullopt: stop. A target that moved a little keeps the path (the end of the path moves with it).
        void set_agent_target(int32_t agent, const std::optional<glm::vec3>& target);
        // paths, steering and avoidance of all agents
        void update_crowd(float dt);
        CrowdAgentState get_agent_state(int32_t agent) const;

        // ---- debug

        void debug_draw(const NavDebugDraw& settings,
            const std::function<void(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color)>& line) const;

    private:
        std::unique_ptr<NavMeshData> data;
    };

    // Built tiles are cached here (by the hash of their input)
    std::filesystem::path get_navmesh_cache_dir();
}
