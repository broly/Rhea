#pragma once
#include <chrono>

struct EngineClock
{
    void start()
    {
        start_time = std::chrono::steady_clock::now();
        current_frame_time = std::chrono::steady_clock::now();
    }
    
    void tick()
    {
        auto frame_time = std::chrono::steady_clock::now();
        auto frame_duration = std::chrono::duration_cast<std::chrono::milliseconds>(frame_time - current_frame_time);
        delta_seconds = (double)frame_duration.count() / 1000.f;
        current_frame_time = frame_time;
    }
    
    double get_delta_seconds() const
    {
        return delta_seconds;
    }
    
    double get_total_seconds() const
    {
        auto frame_time = std::chrono::steady_clock::now();
        auto frame_duration = std::chrono::duration_cast<std::chrono::milliseconds>(current_frame_time - start_time);
        return (double)frame_duration.count() / 1000.f;
    }
    
    double delta_seconds;
    std::chrono::time_point<std::chrono::steady_clock> current_frame_time;
    std::chrono::time_point<std::chrono::steady_clock> start_time;
    
};

