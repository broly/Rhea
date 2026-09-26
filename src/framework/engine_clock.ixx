export module framework:engine_clock;

import std.compat;


export struct EngineClock
{
    void start()
    {
        start_time = std::chrono::steady_clock::now();
        current_frame_time = std::chrono::steady_clock::now();
        num_ticks = 0;
    }
    
    void tick()
    {
        auto frame_time = std::chrono::steady_clock::now();
        // full precision: the fixed-step accumulator sums these every frame
        delta_seconds = std::chrono::duration<double>(frame_time - current_frame_time).count();
        current_frame_time = frame_time;
        num_ticks++;
    }
    
    double get_delta_seconds() const
    {
        return delta_seconds;
    }
    
    double get_total_seconds() const
    {
        return std::chrono::duration<double>(current_frame_time - start_time).count();
    }
    
    double delta_seconds = 0.0;
    std::chrono::time_point<std::chrono::steady_clock> current_frame_time;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    
    unsigned long long num_ticks = 0;
    
};

