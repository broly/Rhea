module;

#include <json/value.h>

export module navigation:agents;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import cvar;
import :navmesh;

// Agents moving on the navigation mesh: NPCs, the companion, enemies. The crowd plans and steers, the entity's own
// movement (a locomotion character, the jackal simulation) moves the body and reports where it got to:
//
//   game code         sets the target (nav::move_to / nav::stop) before NavAgentUpdate
//   mover             writes NavAgent::position / velocity after it moved (any phase); unset: the Transform is read
//   NavAgentUpdate    FixedPre: positions -> crowd, crowd update (paths, corners, avoidance) -> status,
//                     desired_velocity, next_corner, distance_to_goal
//   mover             goes along desired_velocity (or towards next_corner) in its own way
//
// Without a navigation mesh (no NavMeshBounds) or off it the status is off_mesh: movers fall back to going straight.
//
//     "NavAgent": { "radius": 0.3, "height": 0.8, "max_speed": 6.0, "max_acceleration": 8.0 }
//
// obstacle: never steered, only avoided by the others (the player). cvars nav.debug.* draw the mesh and the agents.
export namespace nav
{
    struct NavAgentUpdate {};   // FixedPre: crowd update
    struct NavMeshUpdate {};    // Late (after the transforms): bounds, tile building

    struct NavAgent
    {
        // ---- tuning
        [[=rh::edit, =rh::speed<0.01f>]] float radius = 0.35f;
        [[=rh::edit, =rh::speed<0.01f>]] float height = 1.8f;
        [[=rh::edit, =rh::speed<0.05f>]] float max_speed = 3.5f;          // m/s the crowd steers it at (movers may set it per tick)
        [[=rh::edit, =rh::speed<0.1f>]] float max_acceleration = 10.0f;   // m/s2 the crowd assumes when avoiding
        [[=rh::edit, =rh::speed<0.05f>]] float separation = 2.0f;         // keeps apart from neighbours (0: off)
        [[=rh::edit]] AvoidanceQuality avoidance = AvoidanceQuality::good;
        [[=rh::edit]] bool obstacle = false;                              // not steered: the others go around it

        // ---- request (move_to / stop)
        [[=rh::transient]] std::optional<glm::vec3> target;

        // ---- body, written by the mover
        [[=rh::transient]] std::optional<glm::vec3> position;    // feet
        [[=rh::transient]] glm::vec3 velocity{ 0.0f };
        [[=rh::transient]] std::optional<glm::vec3> sampled_position;   // no mover: the Transform of the last tick

        // ---- result of NavAgentUpdate
        [[=rh::edit, =rh::read_only, =rh::transient]] NavStatus status = NavStatus::off_mesh;
        [[=rh::transient]] glm::vec3 desired_velocity{ 0.0f };
        [[=rh::transient]] glm::vec3 next_corner{ 0.0f };
        [[=rh::transient]] bool has_corner = false;
        [[=rh::edit, =rh::read_only, =rh::transient]] float distance_to_goal = std::numeric_limits<float>::infinity();

        // ---- crowd slot
        [[=rh::transient]] int32_t crowd_index = -1;
        [[=rh::transient]] uint32_t navmesh_id = 0;      // NavMesh::get_id of the crowd the slot is in
        [[=rh::transient]] CrowdAgentParams applied;     // last params given to the crowd
        [[=rh::transient]] std::optional<glm::vec3> applied_target;
    };

    // ---- settings (cvars nav.*; the console, Windows > Navigation)

    cvar::Var<bool> cv_enabled("nav.enabled", true, "Navigation: builds the mesh inside NavMeshBounds and steers the NavAgents");
    cvar::Var<float> cv_budget("nav.build.budget_ms", 4.0f,
        "Game thread time per frame for collecting the geometry of navigation tiles (Recast runs on worker threads), ms",
        { .has_range = true, .min = 0.5f, .max = 50.0f });

    // NavMeshSettings (changes rebuild every tile, from the cache when one with the same input was built before)
    cvar::Var<float> cv_cell_size("nav.build.cell_size", 0.2f, "Navigation mesh voxel size (xz), m", { .has_range = true, .min = 0.05f, .max = 1.0f });
    cvar::Var<float> cv_cell_height("nav.build.cell_height", 0.1f, "Navigation mesh voxel height, m", { .has_range = true, .min = 0.02f, .max = 0.5f });
    cvar::Var<float> cv_agent_radius("nav.build.agent_radius", 0.35f, "Navigation mesh: distance kept from walls and edges, m",
        { .has_range = true, .min = 0.0f, .max = 2.0f });
    cvar::Var<float> cv_agent_height("nav.build.agent_height", 1.8f, "Navigation mesh: passages lower than this are blocked, m",
        { .has_range = true, .min = 0.2f, .max = 5.0f });
    cvar::Var<float> cv_agent_climb("nav.build.agent_max_climb", 0.4f, "Navigation mesh: steps and ledges walked over, m",
        { .has_range = true, .min = 0.0f, .max = 2.0f });
    cvar::Var<float> cv_agent_slope("nav.build.agent_max_slope", 45.0f, "Navigation mesh: steepest walkable slope, degrees",
        { .has_range = true, .min = 0.0f, .max = 89.0f });
    cvar::Var<int> cv_tile_size("nav.build.tile_size", 128, "Navigation mesh: cells per tile side (a tile is rebuilt as a whole)",
        { .has_range = true, .min = 16.0f, .max = 512.0f });

    cvar::Var<bool> cv_draw("nav.debug.draw", false, "Draws the navigation mesh near the camera: borders bright, inner edges faint");
    cvar::Var<bool> cv_draw_tiles("nav.debug.tiles", false,
        "Draws the navigation tiles: green built, grey nothing walkable, yellow waiting, orange building");
    cvar::Var<bool> cv_draw_agents("nav.debug.agents", false,
        "Draws the navigation agents: path corners ahead (orange), target (magenta), desired velocity (yellow), velocity (green)");
    cvar::Var<float> cv_draw_distance("nav.debug.distance", 40.0f, "Navigation tiles farther from the camera are not drawn, m",
        { .has_range = true, .min = 5.0f, .max = 500.0f });

    // every tile again (console nav.rebuild [nocache]); ignore_cache: Recast builds all of them
    void request_rebuild(bool ignore_cache);

    inline void move_to(NavAgent& agent, const glm::vec3& target) { agent.target = target; }
    inline void stop(NavAgent& agent) { agent.target.reset(); }

    // has a path to follow (partial ones too)
    inline bool is_following_path(const NavAgent& agent)
    {
        return agent.status == NavStatus::moving || agent.status == NavStatus::partial;
    }
}
