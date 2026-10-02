module;

#include <json/value.h>

module navigation;

import :navmesh;
import :agents;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import rhcomponents;
import physics;
import cvar;
import debug_draw;
import fixed_string;
import log;

#include "logging/log_macro.h"
#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"

DEFINE_LOGGER(LogNavigationAgents, Log);

namespace
{
    using namespace nav;

    // nav.rebuild [nocache]: applied by the next update
    std::optional<bool> pending_rebuild;

    cvar::Command rebuild_command("nav.rebuild",
        "Rebuilds every navigation tile (their input is collected again); 'nocache': Recast builds all of them, the cache is ignored",
        [] (cvar::Args args) {
            nav::request_rebuild(!args.empty() && args[0] == "nocache");
        }, "[nocache]");

    NavMeshSettings settings_from_cvars()
    {
        NavMeshSettings s;
        s.cell_size = cv_cell_size.get();
        s.cell_height = cv_cell_height.get();
        s.agent_radius = cv_agent_radius.get();
        s.agent_height = cv_agent_height.get();
        s.agent_max_climb = cv_agent_climb.get();
        s.agent_max_slope = cv_agent_slope.get();
        s.tile_size = std::clamp(cv_tile_size.get(), 16, 512);
        return s;
    }

    CrowdAgentParams params_of(const NavAgent& a)
    {
        return {
            .radius = a.radius,
            .height = a.height,
            .max_speed = a.max_speed,
            .max_acceleration = a.max_acceleration,
            .separation = a.separation,
            .avoidance = a.avoidance,
            .obstacle = a.obstacle,
        };
    }

    // ---------------------------------------------------------------- the mesh

    // The navigation mesh exists while the level has bounds for it
    [[=ecs::on_add]]
    void create_navmesh(ecs::Registry& registry, ecs::Entity, NavMeshBounds&)
    {
        if (!registry.find_resource<NavMesh>())
            registry.set_resource<NavMesh>(settings_from_cvars());
    }

    // once per frame, with the world transforms of the bounds; level bodies the collider sync creates afterwards are
    // seen the next frame
    [[=ecs::system<ecs::Phase::Late>, =ecs::in_set<NavMeshUpdate>, =ecs::after<scene::TransformPropagation>,
      =ecs::before<MeshColliderSync>, =ecs::before<RenderSync>]]
    void update_navmesh(ecs::Query<const NavMeshBounds, const WorldTransform> volumes, ecs::OptResMut<NavMesh> nav,
        ecs::Res<phys::PhysicsScene> physics)
    {
        if (!nav)
            return;

        nav->set_settings(settings_from_cvars());
        if (pending_rebuild)
        {
            nav->rebuild(*pending_rebuild);
            LogNavigationAgents.Log("Navigation mesh: rebuilding every tile%s", *pending_rebuild ? " without the cache" : "");
            pending_rebuild.reset();
        }

        std::vector<phys::Bounds> bounds;
        volumes.each([&] (const NavMeshBounds& volume, const WorldTransform& world) {
            const glm::vec3 center = world.value.position.glm();
            const glm::vec3 half = glm::max(volume.half_extents.glm(), glm::vec3(0.0f));
            bounds.push_back({ center - half, center + half });
        });
        nav->set_bounds(bounds);

        if (!cv_enabled.get())
            return;

        // the log tells when a build round starts and ends
        static bool building = false;
        static std::chrono::steady_clock::time_point build_start;
        static NavMeshStats stats_at_start;
        nav->update(*physics, cv_budget.get());
        const NavMeshStats stats = nav->get_stats();
        if (!building && !nav->is_idle())
        {
            building = true;
            build_start = std::chrono::steady_clock::now();
            stats_at_start = stats;
        }
        else if (building && nav->is_idle())
        {
            building = false;
            const float seconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - build_start).count();
            LogNavigationAgents.Log("Navigation mesh ready in %.1f s: %u of %u tiles walkable, %u polygons; %u tiles built, %u from cache/navmesh",
                seconds, stats.tiles_built, stats.tiles, stats.polygons, stats.builds - stats_at_start.builds,
                stats.cache_hits - stats_at_start.cache_hits);
        }
    }

    // ---------------------------------------------------------------- agents

    void clear_result(NavAgent& a)
    {
        a.status = NavStatus::off_mesh;
        a.desired_velocity = glm::vec3(0.0f);
        a.has_corner = false;
        a.distance_to_goal = std::numeric_limits<float>::infinity();
    }

    [[=ecs::system<ecs::Phase::FixedPre>, =ecs::in_set<NavAgentUpdate>]]
    void update_nav_agents(ecs::Query<NavAgent, const Transform> agents, ecs::OptResMut<NavMesh> nav, ecs::Res<ecs::SimTime> time)
    {
        const float dt = (float)time->dt;
        if (!nav || !cv_enabled.get())
        {
            agents.each([&] (NavAgent& a, const Transform&) { clear_result(a); });
            return;
        }

        // bodies and requests -> crowd
        agents.each([&] (NavAgent& a, const Transform& transform) {
            const glm::vec3 position = a.position.value_or(transform.position.glm());
            if (!a.position)
            {
                // nothing reports how it moves (an obstacle following its Transform): from the last position
                a.velocity = a.sampled_position && dt > 0.0f ? (position - *a.sampled_position) / dt : glm::vec3(0.0f);
                a.sampled_position = position;
            }

            if (a.navmesh_id != nav->get_id())
            {
                // a new mesh (level, settings): a new slot
                a.navmesh_id = nav->get_id();
                a.crowd_index = -1;
            }
            const CrowdAgentParams params = params_of(a);
            if (a.crowd_index < 0)
            {
                a.crowd_index = nav->add_agent(position, params);
                a.applied = params;
                a.applied_target.reset();
                if (a.crowd_index < 0)
                    return;
            }
            else if (!(params == a.applied))
            {
                nav->set_agent_params(a.crowd_index, params);
                a.applied = params;
            }
            nav->set_agent_position(a.crowd_index, position, a.velocity);
            const std::optional<glm::vec3> target = a.obstacle ? std::nullopt : a.target;
            if (target != a.applied_target)
            {
                nav->set_agent_target(a.crowd_index, target);
                a.applied_target = target;
            }
        });

        nav->update_crowd(dt);

        // crowd -> steering for the movers
        agents.each([&] (NavAgent& a, const Transform&) {
            if (a.crowd_index < 0)
            {
                clear_result(a);
                return;
            }
            const CrowdAgentState state = nav->get_agent_state(a.crowd_index);
            a.status = state.status;
            a.desired_velocity = state.desired_velocity;
            a.next_corner = state.next_corner;
            a.has_corner = state.has_corner;
            a.distance_to_goal = state.distance_to_goal;
        });
    }

    [[=ecs::on_remove]]
    void remove_nav_agent(ecs::Registry& registry, ecs::Entity, NavAgent& a)
    {
        NavMesh* nav = registry.find_resource<NavMesh>();
        if (nav && a.crowd_index >= 0 && a.navmesh_id == nav->get_id())
            nav->remove_agent(a.crowd_index);
        a.crowd_index = -1;
    }

    // ---------------------------------------------------------------- debug

    [[=ecs::system<ecs::Phase::Late>, =ecs::after<NavMeshUpdate>, =ecs::before<MeshColliderSync>, =ecs::before<RenderSync>]]
    void draw_navigation(ecs::Query<const Camera, const WorldTransform> cameras, ecs::OptRes<NavMesh> nav)
    {
        if (!nav || !(cv_draw.get() || cv_draw_tiles.get() || cv_draw_agents.get()))
            return;
        std::optional<glm::vec3> camera;
        cameras.each([&] (const Camera& c, const WorldTransform& world) {
            if (!camera && c.active)
                camera = world.value.position.glm();
        });
        if (!camera)
            return;

        std::vector<debug_draw::Line> lines;
        nav->debug_draw({
                .camera = *camera,
                .distance = cv_draw_distance.get(),
                .polygons = cv_draw.get(),
                .tiles = cv_draw_tiles.get(),
                .agents = cv_draw_agents.get(),
            },
            [&lines] (const glm::vec3& a, const glm::vec3& b, const glm::vec4& color) {
                lines.push_back({ a, b, color });
            });
        debug_draw::lines(lines);
    }

    ECS_REGISTER()
    SCENE_REGISTER_COMPONENTS(NavMeshBounds, NavAgent)
}

namespace nav
{
    void request_rebuild(bool ignore_cache)
    {
        pending_rebuild = ignore_cache;
    }
}
