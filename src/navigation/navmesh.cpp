module;

// Recast / Detour are only included here and in no BMI (like Jolt in physics_jolt.cpp)
#include <Recast.h>
#include <DetourAlloc.h>
#include <DetourCommon.h>
#include <DetourNavMesh.h>
#include <DetourNavMeshBuilder.h>
#include <DetourNavMeshQuery.h>
#include <DetourCrowd.h>

module navigation;

import :navmesh;

import std.compat;
import glm;
import physics;
import paths;
import fixed_string;
import log;

#include "logging/log_macro.h"

DEFINE_LOGGER(LogNavigation, Log);

namespace
{
    using namespace nav;

    // bump when the build pipeline changes: cached tiles of an older pipeline are rebuilt
    constexpr uint32_t tile_build_version = 1;
    constexpr uint32_t tile_cache_magic = 0x564e4852;   // "RHNV"

    constexpr int max_verts_per_poly = 6;
    constexpr unsigned short poly_flag_walk = 0x01;

    constexpr int max_tiles = 1 << 14;
    constexpr int max_agents = 256;
    constexpr float max_agent_radius = 1.5f;
    constexpr int query_nodes = 4096;
    constexpr int max_path_polys = 512;

    // the level geometry agents walk on and collide with
    constexpr phys::CategoryMask walk_categories = phys::Category::static_world | phys::Category::voxel;

    // a tile is collected once its geometry has been quiet this long (loading spawns bodies over many frames)
    constexpr double settle_seconds = 0.4;
    // jobs waiting for a worker at most (their triangles are held in memory)
    constexpr size_t max_queued_jobs = 32;

    // the body moved this far from where the crowd had it: placed anew (teleport, pushed off the path)
    constexpr float replace_distance = 1.5f;
    // a moving target closer than this to the last planned one only moves the end of the path
    constexpr float retarget_distance = 1.0f;
    // search box around targets for the closest point on the mesh (half size)
    constexpr float target_extents[3] = { 2.0f, 4.0f, 2.0f };

    uint64_t tile_key(int x, int z)
    {
        return (uint64_t(uint32_t(x)) << 32) | uint32_t(z);
    }

    float tile_world_size(const NavMeshSettings& s)
    {
        return float(s.tile_size) * s.cell_size;
    }

    // voxels around a tile that are rasterized too, so the erosion at its borders sees the neighbours
    int border_cells(const NavMeshSettings& s)
    {
        return int(std::ceil(s.agent_radius / s.cell_size)) + 3;
    }

    glm::vec3 to_glm(const float* v) { return { v[0], v[1], v[2] }; }

    struct DetourDeleter
    {
        void operator()(dtNavMesh* p) const { dtFreeNavMesh(p); }
        void operator()(dtNavMeshQuery* p) const { dtFreeNavMeshQuery(p); }
        void operator()(dtCrowd* p) const { dtFreeCrowd(p); }
        void operator()(rcHeightfield* p) const { rcFreeHeightField(p); }
        void operator()(rcCompactHeightfield* p) const { rcFreeCompactHeightfield(p); }
        void operator()(rcContourSet* p) const { rcFreeContourSet(p); }
        void operator()(rcPolyMesh* p) const { rcFreePolyMesh(p); }
        void operator()(rcPolyMeshDetail* p) const { rcFreePolyMeshDetail(p); }
    };
    template<typename T>
    using DetourPtr = std::unique_ptr<T, DetourDeleter>;


    // ---------------------------------------------------------------- tile builds

    struct TileJob
    {
        int x = 0;
        int z = 0;
        uint64_t hash = 0;
        uint32_t generation = 0;
        bool ignore_cache = false;
        phys::Bounds box;                    // the tile and its border, the y range of the bounds
        NavMeshSettings settings;
        std::vector<glm::vec3> triangles;    // 3 vertices each
    };

    struct TileResult
    {
        int x = 0;
        int z = 0;
        uint64_t hash = 0;
        uint32_t generation = 0;
        unsigned char* data = nullptr;       // dtAlloc'ed tile, null: nothing walkable
        int size = 0;
        bool from_cache = false;
        float build_ms = 0.0f;
        std::string errors;
    };

    class BuildContext final : public rcContext
    {
    public:
        BuildContext() : rcContext(true) {}
        std::string errors;

    protected:
        void doLog(const rcLogCategory category, const char* message, const int length) override
        {
            if (category != RC_LOG_ERROR && category != RC_LOG_WARNING)
                return;
            errors.append(message, size_t(std::max(length, 0)));
            errors += "; ";
        }
    };

    std::filesystem::path tile_cache_path(int x, int z)
    {
        return get_navmesh_cache_dir() / std::format("tile_{}_{}.bin", x, z);
    }

    struct TileCacheHeader
    {
        uint32_t magic = tile_cache_magic;
        uint32_t version = tile_build_version;
        uint64_t hash = 0;
        int32_t size = 0;      // 0: nothing walkable in the tile
        int32_t padding = 0;
    };

    // true: the cache has the tile of this input (result.data may stay null: an empty tile)
    bool load_cached_tile(TileResult& result)
    {
        std::ifstream file(tile_cache_path(result.x, result.z), std::ios::binary);
        if (!file)
            return false;
        TileCacheHeader header;
        if (!file.read(reinterpret_cast<char*>(&header), sizeof(header)) || header.magic != tile_cache_magic
            || header.version != tile_build_version || header.hash != result.hash || header.size < 0)
            return false;
        if (header.size == 0)
            return true;
        auto* data = static_cast<unsigned char*>(dtAlloc(size_t(header.size), DT_ALLOC_PERM));
        if (!data)
            return false;
        if (!file.read(reinterpret_cast<char*>(data), header.size))
        {
            dtFree(data);
            return false;
        }
        result.data = data;
        result.size = header.size;
        return true;
    }

    void store_cached_tile(const TileResult& result)
    {
        std::error_code error;
        std::filesystem::create_directories(get_navmesh_cache_dir(), error);
        // written next to it and renamed: a reader never sees half a file
        const std::filesystem::path path = tile_cache_path(result.x, result.z);
        std::filesystem::path temporary = path;
        temporary += std::format(".{}.tmp", std::hash<std::thread::id>{}(std::this_thread::get_id()));
        {
            std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
            if (!file)
                return;
            TileCacheHeader header;
            header.hash = result.hash;
            header.size = result.data ? result.size : 0;
            file.write(reinterpret_cast<const char*>(&header), sizeof(header));
            if (result.data)
                file.write(reinterpret_cast<const char*>(result.data), result.size);
            if (!file)
                return;
        }
        std::filesystem::rename(temporary, path, error);
        if (error)
            std::filesystem::remove(temporary, error);
    }

    // Recast: triangles -> voxels -> walkable regions -> polygons -> Detour tile (Sample_TileMesh::buildTileMesh)
    void build_tile(const TileJob& job, TileResult& result)
    {
        const NavMeshSettings& s = job.settings;
        const int triangle_count = int(job.triangles.size() / 3);
        if (triangle_count == 0)
            return;

        rcConfig cfg{};
        cfg.cs = s.cell_size;
        cfg.ch = s.cell_height;
        cfg.walkableSlopeAngle = s.agent_max_slope;
        cfg.walkableHeight = int(std::ceil(s.agent_height / cfg.ch));
        cfg.walkableClimb = int(std::floor(s.agent_max_climb / cfg.ch));
        cfg.walkableRadius = int(std::ceil(s.agent_radius / cfg.cs));
        cfg.maxEdgeLen = int(s.edge_max_length / cfg.cs);
        cfg.maxSimplificationError = s.edge_max_error;
        cfg.minRegionArea = int(rcSqr(s.region_min_size));
        cfg.mergeRegionArea = int(rcSqr(s.region_merge_size));
        cfg.maxVertsPerPoly = max_verts_per_poly;
        cfg.tileSize = s.tile_size;
        cfg.borderSize = border_cells(s);
        cfg.width = cfg.tileSize + cfg.borderSize * 2;
        cfg.height = cfg.width;
        cfg.detailSampleDist = s.detail_sample_distance < 0.9f ? 0.0f : cfg.cs * s.detail_sample_distance;
        cfg.detailSampleMaxError = cfg.ch * s.detail_sample_max_error;
        cfg.bmin[0] = job.box.min.x;
        cfg.bmin[1] = job.box.min.y;
        cfg.bmin[2] = job.box.min.z;
        cfg.bmax[0] = job.box.max.x;
        cfg.bmax[1] = job.box.max.y;
        cfg.bmax[2] = job.box.max.z;

        BuildContext ctx;
        auto fail = [&] (const char* step) {
            result.errors = std::format("{} failed. {}", step, ctx.errors);
        };

        DetourPtr<rcHeightfield> heightfield(rcAllocHeightfield());
        if (!heightfield || !rcCreateHeightfield(&ctx, *heightfield, cfg.width, cfg.height, cfg.bmin, cfg.bmax, cfg.cs, cfg.ch))
            return fail("rcCreateHeightfield");

        // a triangle soup: vertex i of triangle t is triangles[3 t + i]
        static_assert(sizeof(glm::vec3) == 3 * sizeof(float));
        const float* vertices = &job.triangles[0].x;
        std::vector<int> indices(size_t(triangle_count) * 3);
        std::iota(indices.begin(), indices.end(), 0);
        std::vector<unsigned char> areas(size_t(triangle_count), RC_NULL_AREA);
        rcMarkWalkableTriangles(&ctx, cfg.walkableSlopeAngle, vertices, int(job.triangles.size()), indices.data(), triangle_count,
            areas.data());
        if (!rcRasterizeTriangles(&ctx, vertices, int(job.triangles.size()), indices.data(), areas.data(), triangle_count, *heightfield,
                cfg.walkableClimb))
            return fail("rcRasterizeTriangles");

        // walkable: may step onto low obstacles, not off ledges, needs the head room
        rcFilterLowHangingWalkableObstacles(&ctx, cfg.walkableClimb, *heightfield);
        rcFilterLedgeSpans(&ctx, cfg.walkableHeight, cfg.walkableClimb, *heightfield);
        rcFilterWalkableLowHeightSpans(&ctx, cfg.walkableHeight, *heightfield);

        DetourPtr<rcCompactHeightfield> compact(rcAllocCompactHeightfield());
        if (!compact || !rcBuildCompactHeightfield(&ctx, cfg.walkableHeight, cfg.walkableClimb, *heightfield, *compact))
            return fail("rcBuildCompactHeightfield");
        heightfield.reset();

        if (!rcErodeWalkableArea(&ctx, cfg.walkableRadius, *compact))
            return fail("rcErodeWalkableArea");

        // watershed regions: the best polygons for open ground, the cost is fine per tile
        if (!rcBuildDistanceField(&ctx, *compact))
            return fail("rcBuildDistanceField");
        if (!rcBuildRegions(&ctx, *compact, cfg.borderSize, cfg.minRegionArea, cfg.mergeRegionArea))
            return fail("rcBuildRegions");

        DetourPtr<rcContourSet> contours(rcAllocContourSet());
        if (!contours || !rcBuildContours(&ctx, *compact, cfg.maxSimplificationError, cfg.maxEdgeLen, *contours))
            return fail("rcBuildContours");
        if (contours->nconts == 0)
            return;   // nothing walkable

        DetourPtr<rcPolyMesh> polygons(rcAllocPolyMesh());
        if (!polygons || !rcBuildPolyMesh(&ctx, *contours, cfg.maxVertsPerPoly, *polygons))
            return fail("rcBuildPolyMesh");
        DetourPtr<rcPolyMeshDetail> detail(rcAllocPolyMeshDetail());
        if (!detail || !rcBuildPolyMeshDetail(&ctx, *polygons, *compact, cfg.detailSampleDist, cfg.detailSampleMaxError, *detail))
            return fail("rcBuildPolyMeshDetail");
        compact.reset();
        contours.reset();

        if (polygons->npolys == 0)
            return;
        if (polygons->nverts >= 0xffff)
            return fail("too many vertices in the tile (smaller tile_size)");

        for (int i = 0; i < polygons->npolys; ++i)
            polygons->flags[i] = polygons->areas[i] != RC_NULL_AREA ? poly_flag_walk : 0;

        dtNavMeshCreateParams params{};
        params.verts = polygons->verts;
        params.vertCount = polygons->nverts;
        params.polys = polygons->polys;
        params.polyAreas = polygons->areas;
        params.polyFlags = polygons->flags;
        params.polyCount = polygons->npolys;
        params.nvp = polygons->nvp;
        params.detailMeshes = detail->meshes;
        params.detailVerts = detail->verts;
        params.detailVertsCount = detail->nverts;
        params.detailTris = detail->tris;
        params.detailTriCount = detail->ntris;
        params.walkableHeight = s.agent_height;
        params.walkableRadius = s.agent_radius;
        params.walkableClimb = s.agent_max_climb;
        params.tileX = job.x;
        params.tileY = job.z;
        params.tileLayer = 0;
        rcVcopy(params.bmin, polygons->bmin);
        rcVcopy(params.bmax, polygons->bmax);
        params.cs = cfg.cs;
        params.ch = cfg.ch;
        params.buildBvTree = true;

        unsigned char* data = nullptr;
        int size = 0;
        if (!dtCreateNavMeshData(&params, &data, &size))
            return fail("dtCreateNavMeshData");
        result.data = data;
        result.size = size;
    }

    // Worker threads for tile builds. Results are taken by the game thread (NavMesh::update).
    class TileBuilder
    {
    public:
        explicit TileBuilder(uint32_t thread_count)
        {
            for (uint32_t i = 0; i < thread_count; ++i)
                threads.emplace_back([this] (std::stop_token stop) { run(stop); });
        }

        ~TileBuilder()
        {
            for (std::jthread& thread : threads)
                thread.request_stop();
            wake.notify_all();
            threads.clear();   // joins
            for (TileResult& result : results)
                dtFree(result.data);
        }

        void push(TileJob job)
        {
            {
                std::lock_guard lock(mutex);
                jobs.push_back(std::move(job));
            }
            wake.notify_one();
        }

        void take_results(std::vector<TileResult>& out)
        {
            std::lock_guard lock(mutex);
            for (TileResult& result : results)
                out.push_back(std::move(result));
            results.clear();
        }

        size_t queued() const
        {
            std::lock_guard lock(mutex);
            return jobs.size();
        }

        uint32_t get_thread_count() const { return uint32_t(threads.size()); }

    private:
        void run(std::stop_token stop)
        {
            for (;;)
            {
                TileJob job;
                {
                    std::unique_lock lock(mutex);
                    if (!wake.wait(lock, stop, [this] { return !jobs.empty(); }))
                        return;
                    job = std::move(jobs.front());
                    jobs.pop_front();
                }

                TileResult result{ .x = job.x, .z = job.z, .hash = job.hash, .generation = job.generation };
                const auto start = std::chrono::steady_clock::now();
                if (!job.ignore_cache && load_cached_tile(result))
                    result.from_cache = true;
                else
                {
                    build_tile(job, result);
                    if (result.errors.empty())
                        store_cached_tile(result);
                }
                result.build_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();

                std::lock_guard lock(mutex);
                results.push_back(std::move(result));
            }
        }

        mutable std::mutex mutex;
        std::condition_variable_any wake;
        std::deque<TileJob> jobs;
        std::vector<TileResult> results;
        std::vector<std::jthread> threads;   // last: joined before the queues go
    };

    struct TileState
    {
        int x = 0;
        int z = 0;
        float min_y = 0.0f;           // of the bounds over the tile
        float max_y = 0.0f;
        bool dirty = true;            // the geometry changed since it was collected
        double changed_at = -1e9;     // seconds, the last change touching it
        bool building = false;
        bool ignore_cache = false;
        uint64_t built_hash = 0;      // input of the tile in the mesh, 0: not built
        bool has_polygons = false;
        uint32_t polygons = 0;
    };

    uint32_t next_navmesh_id = 1;
}

namespace nav
{
    std::filesystem::path get_navmesh_cache_dir()
    {
        return paths::get_cache_path() / "navmesh";
    }

    class NavMeshData
    {
    public:
        explicit NavMeshData(const NavMeshSettings& in_settings)
            : settings(in_settings)
            , builder(std::max(1u, std::thread::hardware_concurrency() / 2))
        {
            init_detour();
        }

        bool init_detour()
        {
            id = next_navmesh_id++;
            crowd.reset();
            query.reset();
            mesh.reset();

            mesh.reset(dtAllocNavMesh());
            dtNavMeshParams params{};
            // tiles at fixed world coordinates (origin 0): cached tiles stay valid when the bounds change
            params.tileWidth = tile_world_size(settings);
            params.tileHeight = params.tileWidth;
            params.maxTiles = max_tiles;
            params.maxPolys = 1 << 16;
            if (!mesh || dtStatusFailed(mesh->init(&params)))
            {
                LogNavigation.Log<Error>("dtNavMesh::init failed");
                mesh.reset();
                return false;
            }
            query.reset(dtAllocNavMeshQuery());
            if (!query || dtStatusFailed(query->init(mesh.get(), query_nodes)))
            {
                LogNavigation.Log<Error>("dtNavMeshQuery::init failed");
                return false;
            }
            crowd.reset(dtAllocCrowd());
            if (!crowd || !crowd->init(max_agents, max_agent_radius, mesh.get()))
            {
                LogNavigation.Log<Error>("dtCrowd::init failed");
                crowd.reset();
                return false;
            }
            // velocity sampling presets of the Recast demo (AvoidanceQuality)
            dtObstacleAvoidanceParams avoidance = *crowd->getObstacleAvoidanceParams(0);
            const unsigned char divisions[] = { 5, 5, 7, 7 };
            const unsigned char rings[] = { 2, 2, 2, 3 };
            const unsigned char depth[] = { 1, 2, 3, 3 };
            for (int i = 0; i < 4; ++i)
            {
                avoidance.velBias = 0.5f;
                avoidance.adaptiveDivs = divisions[i];
                avoidance.adaptiveRings = rings[i];
                avoidance.adaptiveDepth = depth[i];
                crowd->setObstacleAvoidanceParams(i, &avoidance);
            }
            filter.setIncludeFlags(poly_flag_walk);
            filter.setExcludeFlags(0);
            crowd->getEditableFilter(0)->setIncludeFlags(poly_flag_walk);
            agents.assign(max_agents, {});

            for (auto& [key, tile] : tiles)
            {
                tile.built_hash = 0;
                tile.has_polygons = false;
                tile.polygons = 0;
            }
            return true;
        }

        double now() const
        {
            return std::chrono::duration<double>(std::chrono::steady_clock::now() - start_time).count();
        }

        // tiles whose build box (tile + border) overlaps the box
        template<typename F>
        void for_tiles_overlapping(const phys::Bounds& box, F&& f)
        {
            const double size = tile_world_size(settings);
            const double border = double(border_cells(settings)) * settings.cell_size;
            const double x0 = std::floor((box.min.x - border) / size), x1 = std::floor((box.max.x + border) / size);
            const double z0 = std::floor((box.min.z - border) / size), z1 = std::floor((box.max.z + border) / size);
            if (!((x1 - x0 + 1.0) * (z1 - z0 + 1.0) <= double(tiles.size())))
            {
                // a box over many tiles (the terrain): through the tiles there are
                for (auto& [key, tile] : tiles)
                    if (tile.x >= x0 && tile.x <= x1 && tile.z >= z0 && tile.z <= z1)
                        f(tile);
                return;
            }
            for (int z = int(z0); z <= int(z1); ++z)
                for (int x = int(x0); x <= int(x1); ++x)
                    if (auto it = tiles.find(tile_key(x, z)); it != tiles.end())
                        f(it->second);
        }

        void mark_dirty(const phys::Bounds& box)
        {
            const double t = now();
            for_tiles_overlapping(box, [&] (TileState& tile) {
                if (box.max.y < tile.min_y || box.min.y > tile.max_y)
                    return;
                tile.dirty = true;
                tile.changed_at = t;
            });
        }

        void mark_all_dirty(bool ignore_cache)
        {
            const double t = now();
            for (auto& [key, tile] : tiles)
            {
                tile.dirty = true;
                tile.changed_at = t;
                tile.ignore_cache = tile.ignore_cache || ignore_cache;
            }
        }

        phys::Bounds build_box(const TileState& tile) const
        {
            const float size = tile_world_size(settings);
            const float border = float(border_cells(settings)) * settings.cell_size;
            return {
                { float(tile.x) * size - border, tile.min_y, float(tile.z) * size - border },
                { float(tile.x + 1) * size + border, tile.max_y, float(tile.z + 1) * size + border },
            };
        }

        void remove_tile(int x, int z)
        {
            if (const dtTileRef ref = mesh ? mesh->getTileRefAt(x, z, 0) : 0)
                mesh->removeTile(ref, nullptr, nullptr);
        }

        void apply_results()
        {
            finished.clear();
            builder.take_results(finished);
            for (TileResult& result : finished)
            {
                auto it = tiles.find(tile_key(result.x, result.z));
                if (it == tiles.end() || result.generation != generation || !mesh)
                {
                    dtFree(result.data);
                    continue;
                }
                TileState& tile = it->second;
                tile.building = false;
                if (!result.errors.empty())
                    LogNavigation.Log<Warning>("Navigation tile %d %d: %s", result.x, result.z, result.errors.c_str());

                remove_tile(result.x, result.z);
                tile.built_hash = result.hash;
                tile.has_polygons = false;
                tile.polygons = 0;
                if (result.data)
                {
                    // the mesh owns the data from now on
                    dtTileRef ref = 0;
                    if (dtStatusFailed(mesh->addTile(result.data, result.size, DT_TILE_FREE_DATA, 0, &ref)))
                    {
                        LogNavigation.Log<Warning>("Navigation tile %d %d: addTile failed", result.x, result.z);
                        dtFree(result.data);
                    }
                    else
                    {
                        const dtMeshTile* added = mesh->getTileByRef(ref);
                        tile.polygons = added && added->header ? uint32_t(added->header->polyCount) : 0;
                        tile.has_polygons = tile.polygons > 0;
                    }
                }
                if (result.from_cache)
                    ++stats.cache_hits;
                else
                {
                    ++stats.builds;
                    stats.last_build_ms = result.build_ms;
                }
            }
        }

        void collect(const phys::PhysicsScene& physics, float budget_ms)
        {
            const auto start = std::chrono::steady_clock::now();
            const double t = now();

            // the longest waiting first
            candidates.clear();
            for (auto& [key, tile] : tiles)
                if (tile.dirty && !tile.building && t - tile.changed_at >= settle_seconds)
                    candidates.push_back(&tile);
            std::ranges::sort(candidates, {}, &TileState::changed_at);

            for (TileState* tile : candidates)
            {
                if (builder.queued() >= max_queued_jobs)
                    break;
                if (std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count() > budget_ms)
                    break;

                TileJob job{ .x = tile->x, .z = tile->z, .generation = generation, .ignore_cache = tile->ignore_cache,
                    .box = build_box(*tile), .settings = settings };
                physics.collect_static_triangles(job.box, walk_categories, job.triangles);

                uint64_t hash = phys::shape_hash_seed;
                hash = phys::hash_value(hash, tile_build_version);
                hash = phys::hash_value(hash, settings);
                hash = phys::hash_value(hash, job.box);
                hash = phys::hash_bytes(hash, job.triangles.data(), job.triangles.size() * sizeof(glm::vec3));
                hash = std::max<uint64_t>(hash, 1);   // 0 means not built
                job.hash = hash;

                tile->dirty = false;
                if (hash == tile->built_hash && !tile->ignore_cache)
                    continue;   // same input as the tile in the mesh
                tile->ignore_cache = false;
                tile->building = true;
                builder.push(std::move(job));
            }
            stats.collect_ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - start).count();
        }

        void update_stats()
        {
            stats.tiles = uint32_t(tiles.size());
            stats.tiles_built = stats.tiles_dirty = stats.tiles_building = stats.polygons = 0;
            for (const auto& [key, tile] : tiles)
            {
                stats.tiles_built += tile.has_polygons ? 1 : 0;
                stats.tiles_dirty += tile.dirty ? 1 : 0;
                stats.tiles_building += tile.building ? 1 : 0;
                stats.polygons += tile.polygons;
            }
            stats.agents = 0;
            for (const AgentSlot& slot : agents)
                stats.agents += slot.used ? 1 : 0;
        }

        // ---- crowd

        struct AgentSlot
        {
            bool used = false;
            CrowdAgentParams params;
            std::optional<glm::vec3> target;
            glm::vec3 planned_target{ 0.0f };   // of the last full request
            bool unreachable = false;           // no mesh near the target
        };

        dtCrowdAgent* find_agent(int32_t index) const
        {
            if (!crowd || index < 0 || index >= int32_t(agents.size()) || !agents[size_t(index)].used)
                return nullptr;
            dtCrowdAgent* agent = crowd->getEditableAgent(index);
            return agent && agent->active ? agent : nullptr;
        }

        static dtCrowdAgentParams to_detour(const CrowdAgentParams& p)
        {
            dtCrowdAgentParams d{};
            d.radius = std::clamp(p.radius, 0.05f, max_agent_radius);
            d.height = std::max(p.height, 0.1f);
            d.maxAcceleration = std::max(p.max_acceleration, 0.0f);
            d.maxSpeed = std::max(p.max_speed, 0.0f);
            d.collisionQueryRange = std::max(d.radius * 12.0f, 2.0f);
            d.pathOptimizationRange = d.radius * 30.0f;
            d.separationWeight = std::max(p.separation, 0.0f);
            d.updateFlags = 0;
            if (!p.obstacle)
            {
                d.updateFlags = DT_CROWD_ANTICIPATE_TURNS | DT_CROWD_OPTIMIZE_VIS | DT_CROWD_OPTIMIZE_TOPO | DT_CROWD_OBSTACLE_AVOIDANCE;
                if (p.separation > 0.0f)
                    d.updateFlags |= DT_CROWD_SEPARATION;
            }
            d.obstacleAvoidanceType = (unsigned char)std::min<uint32_t>(uint32_t(p.avoidance), 3);
            d.queryFilterType = 0;
            return d;
        }

        // a full path request (planned in slices by the crowd's path queue)
        void request_target(int32_t index, const glm::vec3& target)
        {
            AgentSlot& slot = agents[size_t(index)];
            slot.planned_target = target;
            dtPolyRef ref = 0;
            float nearest[3];
            const float p[3] = { target.x, target.y, target.z };
            query->findNearestPoly(p, target_extents, &filter, &ref, nearest);
            slot.unreachable = ref == 0;
            if (!ref)
            {
                crowd->resetMoveTarget(index);
                return;
            }
            crowd->requestMoveTarget(index, ref, nearest);
        }

        NavMeshSettings settings;
        uint32_t id = 0;
        uint32_t generation = 1;    // of the settings / rebuilds: older results are dropped

        DetourPtr<dtNavMesh> mesh;
        DetourPtr<dtNavMeshQuery> query;
        DetourPtr<dtCrowd> crowd;
        dtQueryFilter filter;
        std::vector<AgentSlot> agents;

        std::vector<phys::Bounds> bounds;
        std::unordered_map<uint64_t, TileState> tiles;
        uint64_t physics_cursor = 0;
        std::vector<phys::StaticChange> changes;
        std::vector<TileState*> candidates;
        std::vector<TileResult> finished;
        NavMeshStats stats;
        std::chrono::steady_clock::time_point start_time = std::chrono::steady_clock::now();

        TileBuilder builder;   // last: its threads stop before the rest goes
    };


    // ---------------------------------------------------------------- NavMesh

    NavMesh::NavMesh(const NavMeshSettings& settings)
        : data(std::make_unique<NavMeshData>(settings))
    {
        LogNavigation.Log("Navigation mesh: cell %.2f x %.2f m, tiles of %.1f m, agent radius %.2f height %.2f climb %.2f slope %.0f, %u build threads",
            settings.cell_size, settings.cell_height, tile_world_size(settings), settings.agent_radius, settings.agent_height,
            settings.agent_max_climb, settings.agent_max_slope, data->builder.get_thread_count());
    }

    NavMesh::~NavMesh() = default;

    uint32_t NavMesh::get_id() const
    {
        return data->id;
    }

    const NavMeshSettings& NavMesh::get_settings() const
    {
        return data->settings;
    }

    void NavMesh::set_bounds(std::span<const phys::Bounds> bounds)
    {
        NavMeshData& d = *data;
        const bool same = bounds.size() == d.bounds.size() && std::ranges::equal(bounds, d.bounds, [] (const phys::Bounds& a, const phys::Bounds& b) {
            return a.min == b.min && a.max == b.max;
        });
        if (same)
            return;
        d.bounds.assign(bounds.begin(), bounds.end());

        // the tiles over the boxes, the y range of the boxes over each
        std::unordered_map<uint64_t, TileState> next;
        const float size = tile_world_size(d.settings);
        for (const phys::Bounds& box : d.bounds)
        {
            const int x0 = int(std::floor(box.min.x / size)), x1 = int(std::ceil(box.max.x / size)) - 1;
            const int z0 = int(std::floor(box.min.z / size)), z1 = int(std::ceil(box.max.z / size)) - 1;
            if (int64_t(x1 - x0 + 1) * int64_t(z1 - z0 + 1) > max_tiles)
            {
                LogNavigation.Log<Error>("NavMeshBounds of %.0f x %.0f m: more than %d tiles, skipped", box.max.x - box.min.x,
                    box.max.z - box.min.z, max_tiles);
                continue;
            }
            for (int z = z0; z <= z1; ++z)
                for (int x = x0; x <= x1; ++x)
                {
                    const uint64_t key = tile_key(x, z);
                    auto [it, inserted] = next.try_emplace(key);
                    TileState& tile = it->second;
                    if (inserted)
                    {
                        // kept as it is if it existed: same bounds, nothing to do
                        if (auto old = d.tiles.find(key); old != d.tiles.end())
                            tile = old->second;
                        else
                        {
                            tile.x = x;
                            tile.z = z;
                            tile.min_y = box.min.y;
                            tile.max_y = box.max.y;
                            tile.changed_at = d.now();
                            continue;
                        }
                        tile.min_y = box.min.y;
                        tile.max_y = box.max.y;
                    }
                    else
                    {
                        tile.min_y = std::min(tile.min_y, box.min.y);
                        tile.max_y = std::max(tile.max_y, box.max.y);
                    }
                }
        }
        // a different y range is a different input
        for (auto& [key, tile] : next)
            if (auto old = d.tiles.find(key); old != d.tiles.end() && (old->second.min_y != tile.min_y || old->second.max_y != tile.max_y))
            {
                tile.dirty = true;
                tile.changed_at = d.now();
            }
        for (const auto& [key, tile] : d.tiles)
            if (!next.contains(key))
                d.remove_tile(tile.x, tile.z);
        d.tiles = std::move(next);
        LogNavigation.Log("Navigation mesh bounds: %zu boxes, %zu tiles", d.bounds.size(), d.tiles.size());
    }

    void NavMesh::set_settings(const NavMeshSettings& settings)
    {
        NavMeshData& d = *data;
        if (std::memcmp(&settings, &d.settings, sizeof(NavMeshSettings)) == 0)
            return;
        d.settings = settings;
        ++d.generation;
        d.init_detour();
        // other tile size: other tiles
        std::vector<phys::Bounds> bounds = std::move(d.bounds);
        d.bounds.clear();
        d.tiles.clear();
        set_bounds(bounds);
        d.mark_all_dirty(false);
        LogNavigation.Log("Navigation mesh settings changed: rebuilding");
    }

    void NavMesh::rebuild(bool ignore_cache)
    {
        NavMeshData& d = *data;
        ++d.generation;
        for (auto& [key, tile] : d.tiles)
        {
            tile.building = false;
            tile.built_hash = 0;
        }
        d.mark_all_dirty(ignore_cache);
    }

    void NavMesh::update(const phys::PhysicsScene& physics, float collect_budget_ms)
    {
        NavMeshData& d = *data;
        if (!d.mesh)
            return;

        d.changes.clear();
        if (!physics.read_static_changes(d.physics_cursor, d.changes))
            d.mark_all_dirty(false);   // changes were dropped: anything may have changed
        for (const phys::StaticChange& change : d.changes)
            if (walk_categories.contains(change.category))
                d.mark_dirty(change.bounds);

        d.apply_results();
        d.collect(physics, collect_budget_ms);
        d.update_stats();
    }

    NavMeshStats NavMesh::get_stats() const
    {
        return data->stats;
    }

    bool NavMesh::is_idle() const
    {
        return data->stats.tiles_dirty == 0 && data->stats.tiles_building == 0;
    }

    // ---------------------------------------------------------------- queries

    std::optional<glm::vec3> NavMesh::project(const glm::vec3& p, const glm::vec3& extents) const
    {
        NavMeshData& d = *data;
        if (!d.query)
            return std::nullopt;
        const float center[3] = { p.x, p.y, p.z };
        const float half[3] = { extents.x, extents.y, extents.z };
        dtPolyRef ref = 0;
        float nearest[3];
        if (dtStatusFailed(d.query->findNearestPoly(center, half, &d.filter, &ref, nearest)) || !ref)
            return std::nullopt;
        return to_glm(nearest);
    }

    bool NavMesh::find_path(const glm::vec3& from, const glm::vec3& to, std::vector<glm::vec3>& corners, bool* partial) const
    {
        NavMeshData& d = *data;
        corners.clear();
        if (partial)
            *partial = false;
        if (!d.query)
            return false;

        const float start[3] = { from.x, from.y, from.z };
        const float end[3] = { to.x, to.y, to.z };
        dtPolyRef start_ref = 0, end_ref = 0;
        float start_point[3], end_point[3];
        d.query->findNearestPoly(start, target_extents, &d.filter, &start_ref, start_point);
        d.query->findNearestPoly(end, target_extents, &d.filter, &end_ref, end_point);
        if (!start_ref || !end_ref)
            return false;

        std::array<dtPolyRef, max_path_polys> path;
        int path_count = 0;
        d.query->findPath(start_ref, end_ref, start_point, end_point, &d.filter, path.data(), &path_count, max_path_polys);
        if (path_count == 0)
            return false;
        if (path[size_t(path_count - 1)] != end_ref)
        {
            // as close as it gets
            if (partial)
                *partial = true;
            d.query->closestPointOnPoly(path[size_t(path_count - 1)], end, end_point, nullptr);
        }

        constexpr int max_corners = 256;
        std::array<float, max_corners * 3> points;
        int point_count = 0;
        d.query->findStraightPath(start_point, end_point, path.data(), path_count, points.data(), nullptr, nullptr, &point_count,
            max_corners);
        for (int i = 0; i < point_count; ++i)
            corners.push_back(to_glm(&points[size_t(i) * 3]));
        return !corners.empty();
    }

    // ---------------------------------------------------------------- crowd

    int32_t NavMesh::add_agent(const glm::vec3& position, const CrowdAgentParams& params)
    {
        NavMeshData& d = *data;
        if (!d.crowd)
            return -1;
        const float p[3] = { position.x, position.y, position.z };
        const dtCrowdAgentParams detour = NavMeshData::to_detour(params);
        const int index = d.crowd->addAgent(p, &detour);
        if (index < 0)
        {
            LogNavigation.Log<Warning>("Crowd full (%d agents)", max_agents);
            return -1;
        }
        d.agents[size_t(index)] = { .used = true, .params = params };
        return index;
    }

    void NavMesh::remove_agent(int32_t agent)
    {
        NavMeshData& d = *data;
        if (!d.find_agent(agent))
            return;
        d.crowd->removeAgent(agent);
        d.agents[size_t(agent)] = {};
    }

    void NavMesh::set_agent_params(int32_t agent, const CrowdAgentParams& params)
    {
        NavMeshData& d = *data;
        if (!d.find_agent(agent))
            return;
        const dtCrowdAgentParams detour = NavMeshData::to_detour(params);
        d.crowd->updateAgentParameters(agent, &detour);
        d.agents[size_t(agent)].params = params;
    }

    void NavMesh::set_agent_position(int32_t agent, const glm::vec3& position, const glm::vec3& velocity)
    {
        NavMeshData& d = *data;
        dtCrowdAgent* a = d.find_agent(agent);
        if (!a)
            return;
        const float p[3] = { position.x, position.y, position.z };
        const dtQueryFilter* filter = d.crowd->getFilter(0);

        if (a->state == DT_CROWDAGENT_STATE_WALKING && dtVdist2DSqr(p, a->npos) <= replace_distance * replace_distance
            && std::abs(p[1] - a->npos[1]) <= d.settings.agent_height)
        {
            // along the mesh from where the crowd had it: the path corridor follows
            a->corridor.movePosition(p, d.query.get(), filter);
            dtVcopy(a->npos, a->corridor.getPos());
        }
        else
        {
            // placed anew: the mesh appeared under it, or it was teleported / pushed far
            dtPolyRef ref = 0;
            float nearest[3];
            d.query->findNearestPoly(p, d.crowd->getQueryHalfExtents(), filter, &ref, nearest);
            if (!ref)
            {
                a->state = DT_CROWDAGENT_STATE_INVALID;
                dtVcopy(a->npos, p);
                dtVset(a->vel, 0.0f, 0.0f, 0.0f);
                dtVset(a->nvel, 0.0f, 0.0f, 0.0f);
                dtVset(a->dvel, 0.0f, 0.0f, 0.0f);
                a->ncorners = 0;
                return;
            }
            a->corridor.reset(ref, nearest);
            a->boundary.reset();
            a->partial = false;
            a->topologyOptTime = 0.0f;
            a->targetReplanTime = 0.0f;
            a->nneis = 0;
            a->ncorners = 0;
            dtVcopy(a->npos, nearest);
            a->state = DT_CROWDAGENT_STATE_WALKING;
            // the old path started somewhere else
            if (const std::optional<glm::vec3>& target = d.agents[size_t(agent)].target)
                d.request_target(agent, *target);
        }
        dtVset(a->vel, velocity.x, velocity.y, velocity.z);
    }

    void NavMesh::set_agent_target(int32_t agent, const std::optional<glm::vec3>& target)
    {
        NavMeshData& d = *data;
        dtCrowdAgent* a = d.find_agent(agent);
        if (!a)
            return;
        NavMeshData::AgentSlot& slot = d.agents[size_t(agent)];
        const bool had_target = slot.target.has_value();
        slot.target = target;
        if (!target)
        {
            slot.unreachable = false;
            d.crowd->resetMoveTarget(agent);
            return;
        }
        if (a->state != DT_CROWDAGENT_STATE_WALKING)
            return;   // requested once it is on the mesh (set_agent_position)

        const float moved = glm::length(*target - slot.planned_target);
        const bool planning = a->targetState == DT_CROWDAGENT_TARGET_REQUESTING || a->targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_QUEUE
            || a->targetState == DT_CROWDAGENT_TARGET_WAITING_FOR_PATH;
        if (had_target && !slot.unreachable && moved < retarget_distance)
        {
            if (a->targetState == DT_CROWDAGENT_TARGET_VALID && a->corridor.getPathCount() > 0)
            {
                // a target moving a little drags the end of the path along the mesh
                const float p[3] = { target->x, target->y, target->z };
                if (a->corridor.moveTargetPosition(p, d.query.get(), d.crowd->getFilter(0)))
                {
                    dtVcopy(a->targetPos, a->corridor.getTarget());
                    a->targetRef = a->corridor.getLastPoly();
                }
                return;
            }
            if (planning)
                return;   // the path being planned ends close enough
        }
        d.request_target(agent, *target);
    }

    void NavMesh::update_crowd(float dt)
    {
        NavMeshData& d = *data;
        if (d.crowd && dt > 0.0f)
            d.crowd->update(dt, nullptr);
    }

    CrowdAgentState NavMesh::get_agent_state(int32_t agent) const
    {
        NavMeshData& d = *data;
        CrowdAgentState state;
        const dtCrowdAgent* a = d.find_agent(agent);
        if (!a)
            return state;
        const NavMeshData::AgentSlot& slot = d.agents[size_t(agent)];
        state.position = to_glm(a->npos);
        if (a->state != DT_CROWDAGENT_STATE_WALKING)
            return state;   // off_mesh

        if (!slot.target)
            state.status = NavStatus::idle;
        else if (slot.unreachable)
            state.status = NavStatus::unreachable;
        else
        {
            switch (a->targetState)
            {
            case DT_CROWDAGENT_TARGET_NONE: state.status = NavStatus::idle; break;
            case DT_CROWDAGENT_TARGET_FAILED: state.status = NavStatus::unreachable; break;
            case DT_CROWDAGENT_TARGET_VALID: state.status = a->partial ? NavStatus::partial : NavStatus::moving; break;
            case DT_CROWDAGENT_TARGET_VELOCITY: state.status = NavStatus::moving; break;
            default: state.status = NavStatus::planning; break;   // a quick partial path is followed meanwhile
            }
        }
        if (state.status == NavStatus::idle || state.status == NavStatus::unreachable)
            return state;

        state.desired_velocity = to_glm(a->nvel);
        state.corner_count = uint32_t(std::min<int>(a->ncorners, int(state.corners.size())));
        for (uint32_t i = 0; i < state.corner_count; ++i)
            state.corners[i] = to_glm(&a->cornerVerts[i * 3]);
        if (a->ncorners > 0)
        {
            state.has_corner = true;
            state.next_corner = state.corners[0];
            // the length is known when the corners reach the end of the path
            if (a->cornerFlags[a->ncorners - 1] & DT_STRAIGHTPATH_END)
            {
                float distance = 0.0f;
                const float* from = a->npos;
                for (int i = 0; i < a->ncorners; ++i)
                {
                    distance += dtVdist2D(from, &a->cornerVerts[i * 3]);
                    from = &a->cornerVerts[i * 3];
                }
                state.distance_to_goal = distance;
            }
        }
        else if (a->targetState == DT_CROWDAGENT_TARGET_VALID)
            state.distance_to_goal = 0.0f;   // there
        return state;
    }

    // ---------------------------------------------------------------- debug

    void NavMesh::debug_draw(const NavDebugDraw& settings,
        const std::function<void(const glm::vec3&, const glm::vec3&, const glm::vec4&)>& line) const
    {
        NavMeshData& d = *data;
        if (!d.mesh)
            return;
        const dtNavMesh& mesh = *d.mesh;
        const glm::vec3 lift(0.0f, 0.06f, 0.0f);
        const float distance_sq = settings.distance * settings.distance;

        auto distance_sq_to_box = [&] (const glm::vec3& min, const glm::vec3& max) {
            const glm::vec3 closest = glm::clamp(settings.camera, min, max);
            const glm::vec3 delta = closest - settings.camera;
            return glm::dot(delta, delta);
        };

        if (settings.polygons)
        {
            const glm::vec4 border_color(0.1f, 0.95f, 1.0f, 1.0f);
            const glm::vec4 portal_color(0.1f, 0.6f, 0.9f, 0.6f);
            const glm::vec4 inner_color(0.0f, 0.55f, 0.75f, 0.3f);
            for (int t = 0; t < mesh.getMaxTiles(); ++t)
            {
                const dtMeshTile* tile = mesh.getTile(t);
                if (!tile || !tile->header)
                    continue;
                if (distance_sq_to_box(to_glm(tile->header->bmin), to_glm(tile->header->bmax)) > distance_sq)
                    continue;
                for (int i = 0; i < tile->header->polyCount; ++i)
                {
                    const dtPoly& poly = tile->polys[i];
                    if (poly.getType() == DT_POLYTYPE_OFFMESH_CONNECTION)
                        continue;
                    const dtPolyDetail& detail = tile->detailMeshes[i];
                    for (int j = 0; j < poly.vertCount; ++j)
                    {
                        // the edge: a border (nothing beyond), a portal to another tile, or shared inside the tile (drawn once)
                        glm::vec4 color = inner_color;
                        if (poly.neis[j] & DT_EXT_LINK)
                        {
                            bool connected = false;
                            for (unsigned int k = poly.firstLink; k != DT_NULL_LINK; k = tile->links[k].next)
                                connected = connected || tile->links[k].edge == j;
                            color = connected ? portal_color : border_color;
                        }
                        else if (poly.neis[j] == 0)
                            color = border_color;
                        else if (int(poly.neis[j]) - 1 < i)
                            continue;

                        const float* v0 = &tile->verts[poly.verts[j] * 3];
                        const float* v1 = &tile->verts[poly.verts[(j + 1) % poly.vertCount] * 3];
                        // along the surface: the edges of the detail triangles on this polygon edge
                        for (int k = 0; k < detail.triCount; ++k)
                        {
                            const unsigned char* tri = &tile->detailTris[(detail.triBase + k) * 4];
                            const float* tv[3];
                            for (int m = 0; m < 3; ++m)
                                tv[m] = tri[m] < poly.vertCount ? &tile->verts[poly.verts[tri[m]] * 3]
                                                                 : &tile->detailVerts[(detail.vertBase + (tri[m] - poly.vertCount)) * 3];
                            for (int m = 0; m < 3; ++m)
                            {
                                if ((dtGetDetailTriEdgeFlags(tri[3], m) & DT_DETAIL_EDGE_BOUNDARY) == 0)
                                    continue;
                                const float* a = tv[m];
                                const float* b = tv[(m + 1) % 3];
                                float along;
                                constexpr float on_edge = 0.01f * 0.01f;
                                if (dtDistancePtSegSqr2D(a, v0, v1, along) < on_edge && dtDistancePtSegSqr2D(b, v0, v1, along) < on_edge)
                                    line(to_glm(a) + lift, to_glm(b) + lift, color);
                            }
                        }
                    }
                }
            }
        }

        if (settings.tiles)
        {
            const float size = tile_world_size(d.settings);
            for (const auto& [key, tile] : d.tiles)
            {
                const glm::vec3 min(float(tile.x) * size, tile.min_y, float(tile.z) * size);
                const glm::vec3 max(float(tile.x + 1) * size, tile.max_y, float(tile.z + 1) * size);
                if (distance_sq_to_box(min, max) > distance_sq)
                    continue;
                const glm::vec4 color = tile.building ? glm::vec4(1.0f, 0.5f, 0.1f, 1.0f)
                    : tile.dirty ? glm::vec4(1.0f, 0.9f, 0.1f, 1.0f)
                    : tile.has_polygons ? glm::vec4(0.2f, 0.9f, 0.3f, 0.6f) : glm::vec4(0.5f, 0.5f, 0.5f, 0.4f);
                // the square at the camera's height (the boxes are tall)
                const float y = std::clamp(settings.camera.y - 1.5f, min.y, max.y);
                const glm::vec3 c[4] = { { min.x, y, min.z }, { max.x, y, min.z }, { max.x, y, max.z }, { min.x, y, max.z } };
                for (int i = 0; i < 4; ++i)
                    line(c[i], c[(i + 1) % 4], color);
            }
        }

        if (settings.agents && d.crowd)
        {
            for (int32_t i = 0; i < int32_t(d.agents.size()); ++i)
            {
                const dtCrowdAgent* a = d.find_agent(i);
                if (!a)
                    continue;
                const glm::vec3 p = to_glm(a->npos) + lift;
                const NavMeshData::AgentSlot& slot = d.agents[size_t(i)];
                // body circle: white walking, red off the mesh, grey obstacle
                const glm::vec4 body = a->state != DT_CROWDAGENT_STATE_WALKING ? glm::vec4(1.0f, 0.2f, 0.2f, 1.0f)
                    : slot.params.obstacle ? glm::vec4(0.6f, 0.6f, 0.6f, 1.0f) : glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
                constexpr int segments = 16;
                for (int s = 0; s < segments; ++s)
                {
                    constexpr float turn = 2.0f * std::numbers::pi_v<float>;
                    const float a0 = turn * float(s) / segments, a1 = turn * float(s + 1) / segments;
                    line(p + glm::vec3(std::cos(a0), 0.0f, std::sin(a0)) * a->params.radius,
                        p + glm::vec3(std::cos(a1), 0.0f, std::sin(a1)) * a->params.radius, body);
                }
                if (a->state != DT_CROWDAGENT_STATE_WALKING)
                    continue;
                // corners ahead (orange), the target (magenta), desired velocity (yellow), actual (green)
                glm::vec3 from = p;
                for (int c = 0; c < a->ncorners; ++c)
                {
                    const glm::vec3 corner = to_glm(&a->cornerVerts[c * 3]) + lift;
                    line(from, corner, { 1.0f, 0.55f, 0.1f, 1.0f });
                    from = corner;
                }
                if (slot.target)
                {
                    const glm::vec3 t = to_glm(a->targetPos) + lift;
                    line(t - glm::vec3(0.2f, 0.0f, 0.0f), t + glm::vec3(0.2f, 0.0f, 0.0f), { 1.0f, 0.2f, 1.0f, 1.0f });
                    line(t - glm::vec3(0.0f, 0.0f, 0.2f), t + glm::vec3(0.0f, 0.0f, 0.2f), { 1.0f, 0.2f, 1.0f, 1.0f });
                    line(t, t + glm::vec3(0.0f, 0.5f, 0.0f), { 1.0f, 0.2f, 1.0f, 1.0f });
                }
                const glm::vec3 up(0.0f, 0.1f, 0.0f);
                line(p + up, p + up + to_glm(a->nvel) * 0.5f, { 1.0f, 0.9f, 0.1f, 1.0f });
                line(p + up * 2.0f, p + up * 2.0f + to_glm(a->vel) * 0.5f, { 0.2f, 1.0f, 0.3f, 1.0f });
            }
        }
    }
}
