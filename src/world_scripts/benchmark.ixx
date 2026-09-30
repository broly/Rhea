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

    std::vector<Frame> frames;
    std::vector<std::string> counter_names;
    std::vector<Scope> scopes;
    std::unordered_map<const prof::ProfileNode*, uint32_t> scope_index;
};
