export module benchmark;

import std.compat;

import glm;
import ecs;
import framework;
import profile;

// Scripted performance run (console `bench.run <route> [name] [laps=<n>] [quit]`): the player character runs a route of
// waypoints while the camera sweeps left and right, and every frame is recorded:
//   cache/bench/<name>_frames.csv   frame time, position, camera, frame counters (prof::count), CPU scopes
//   cache/bench/<name>_gpu.csv      GPU time of every render graph pass of every frame (gpuprof)
//   cache/bench/<name>_meta.json    resolution, route, teleports (the character got stuck)
// tools/bench/run_bench.py starts the engine with it and reports the heaviest stretches.
//
// Route (an asset, assets/bench/*.json): the character is put on the first waypoint and runs the loop
// `laps` times.
//   {
//       "waypoints": [[x, z], ...],
//       "laps": 3,
//       "arrive_distance": 1.5,
//       "warmup_seconds": 3,
//       "camera": { "sweep_degrees": 100, "sweep_period": 8, "pitch_degrees": -12,
//                   "pitch_sweep_degrees": 10, "pitch_period": 13 }
//   }
//
// Playtest (a "playtest" section in the route): instead of the loop the character plays for `seconds` in phases:
//   search  runs to the closest jackal within hunt_range (else to random spots of the navigation mesh around the
//           waypoints = roam points), looks around (a full turn every scan_interval s); a jackal in sight: fight
//   fight   aims at the closest jackal in sight within engage_range and shoots it (weapons::set_scripted_trigger),
//           chases the closest one; ends after fight_kills kills, fight_seconds, or 5 s without a jackal: roam
//   roam    runs over the fields between roam points for roam_seconds, shoots only what comes within defend_range;
//           then search again
// Boxes in "avoid" ([min x, min z, max x, max z]: the Sponza building) are never a destination nor on a path, the
// jackals inside are not hunted. The weapon of the loadout changes every weapon_seconds, the player is healed below
// heal_below x max health. `console` lines run at the start, `console_after` ones at the end. The lap column of the
// records is the minute of the run; phase / jackals / engaged / firing / kills / health / weapon are recorded per frame.
//   "playtest": { "seconds": 300, "roam_radius": 20, "hunt_range": 60, "engage_range": 40, "defend_range": 12,
//                 "fight_kills": 3, "fight_seconds": 30, "roam_seconds": 25, "search_seconds": 45, "scan_interval": 8,
//                 "turn_degrees": 400, "aim_tolerance_degrees": 2, "weapon_seconds": 40, "heal_below": 0.4,
//                 "avoid": [[-19, -12.5, 23, 13.5]], "weapons": ["blaster", ...], "console": [...], "console_after": [...] }
export class Benchmark
{
public:
    // laps_override 0: the laps of the route
    bool start(World& world, const std::string& route_asset_path, const std::string& output_name, uint32_t laps_override,
        bool quit_when_done);
    bool is_active() const { return state != State::idle; }

    // Every frame while active, before the camera is placed: steers the character and turns the camera
    void tick(World& world, ecs::Entity character, float& yaw, float& pitch);

private:
    enum class State { idle, warmup, running, draining };

    struct Frame
    {
        double time = 0.0;         // world seconds since the run started
        double wall = 0.0;         // real seconds since the run started
        float frame_ms = 0.0f;     // real time until the next frame
        glm::vec3 position{0.0f};
        float yaw = 0.0f;
        float pitch = 0.0f;
        uint32_t lap = 0;
        uint32_t waypoint = 0;
        uint64_t gpu_frame = 0;    // gpuprof frame id
        // playtest
        uint32_t phase = 0;        // Phase
        uint32_t jackals = 0;      // alive
        bool engaged = false;      // a jackal in the sights
        bool firing = false;
        uint32_t kills = 0;        // jackals dead so far (anyone's shots)
        float health = 0.0f;
        uint32_t weapon = 0;       // index in playtest_weapons
        float target_distance = 0.0f;
        std::vector<uint64_t> counters;   // by index in counter_names
        std::vector<float> scopes_ms;     // by index in scopes
    };

    struct Scope
    {
        std::string path;          // "Renderer::execute/execute_graph/..."
        int64_t last_ns = 0;
        double total_ms = 0.0;
    };

    void begin_run(World& world);
    void steer(World& world, ecs::Entity character, float& yaw, float& pitch);
    void steer_playtest(World& world, ecs::Entity character, float& yaw, float& pitch);
    void pick_destination(World& world, const glm::vec3& feet);
    bool avoided(const glm::vec3& point) const;
    bool path_avoided(const glm::vec3& from, const std::vector<glm::vec3>& corners) const;
    void set_phase(uint32_t phase, double now);
    void open_frame(World& world, ecs::Entity character, float yaw, float pitch);
    void close_frame();
    void snapshot_scopes(const prof::ProfileNode& node, int32_t parent, int depth, std::vector<float>* out);
    void finish();
    void write_results() const;

    State state = State::idle;
    std::string route_path;
    std::string name;
    bool quit_when_done = false;

    // route
    std::vector<glm::vec2> waypoints;
    uint32_t laps = 1;
    float arrive_distance = 1.5f;
    float warmup_seconds = 3.0f;
    float sweep = 0.0f;
    float sweep_period = 8.0f;
    float base_pitch = -0.2f;
    float pitch_sweep = 0.0f;
    float pitch_period = 13.0f;

    // run
    double start_time = 0.0;       // world seconds: warmup, then the run
    std::chrono::steady_clock::time_point start_wall;
    std::chrono::steady_clock::time_point frame_wall;
    int64_t start_unix_ms = 0;     // to line the frames up with external logs (nvidia-smi)
    uint32_t next_waypoint = 0;
    uint32_t lap = 0;
    float travel_yaw = 0.0f;
    // stuck detection: the closest the character got to the waypoint, and when
    float best_distance = 0.0f;
    double best_distance_time = 0.0;
    uint32_t teleports = 0;
    uint32_t drain_frames = 0;
    bool frame_open = false;

    // playtest
    enum Phase : uint32_t { search, fight, roam };
    bool playtest = false;
    uint32_t phase = Phase::search;
    double phase_start = 0.0;
    uint32_t phase_kills = 0;              // kills when the phase started
    double last_contact = 0.0;             // a jackal last in the sights or hunted
    double scan_start = -100.0;            // the last look around (search)
    float defend_range = 12.0f;
    uint32_t fight_kills = 3;
    float fight_seconds = 30.0f;
    float roam_seconds = 25.0f;
    float search_seconds = 45.0f;
    float scan_interval = 8.0f;
    std::vector<glm::vec4> avoid;          // min x, min z, max x, max z
    float playtest_seconds = 300.0f;
    float roam_radius = 20.0f;
    float hunt_range = 35.0f;
    float engage_range = 40.0f;
    float aim_turn_rate = 7.0f;            // rad/s
    float aim_tolerance = 0.035f;          // rad
    float weapon_seconds = 40.0f;
    float heal_below = 0.4f;
    std::vector<std::string> playtest_weapons;
    std::vector<std::string> console_after;
    std::minstd_rand rng;
    std::optional<glm::vec3> destination;
    bool hunting = false;                  // the destination is a jackal
    std::vector<glm::vec3> path;           // corners to the destination
    size_t path_corner = 0;
    double next_repath = 0.0;
    uint32_t stuck_count = 0;
    glm::vec3 progress_position{ 0.0f };   // where it last moved a meter from, and when
    double progress_time = 0.0;
    ecs::Entity target;
    double next_target_search = 0.0;
    float target_distance = 0.0f;
    bool engaged = false;
    bool trigger = false;
    double trigger_time = 0.0;             // when the trigger last changed
    uint32_t weapon_index = 0;
    double next_weapon_time = 0.0;
    std::vector<ecs::Entity> live_jackals;
    uint32_t jackals_alive = 0;
    uint32_t kills = 0;
    uint32_t heals = 0;
    uint32_t destinations = 0;
    std::optional<bool> saved_infinite_ammo;

    std::vector<Frame> frames;
    std::vector<std::string> counter_names;
    std::vector<Scope> scopes;
    std::unordered_map<const prof::ProfileNode*, uint32_t> scope_index;
};
