export module ecs:events;

import std.compat;

export namespace ecs
{
    // Typed event queue of a registry (Registry::events<T>): systems send with EventWriter<T> and read with
    // EventReader<T>, which remembers per system what it has read, so every reading system sees every event
    // exactly once, in the order they were sent.
    //
    // Lifetime: an event lives until it is at least 2 frames AND 2 fixed ticks old (Registry::update_events,
    // called by Schedule at the start of every frame and every fixed tick). So a reader in Update / Late sees
    // the events of the fixed ticks of its frame and of the previous frame's Update / Late, and a reader in a
    // fixed phase sees the events of the frames since its last tick, however many ticks (0..N) a frame runs.
    // A reader that does not run for longer (another phase, PostLoad, a paused simulation) misses them; events
    // older than max_frame_age frames are dropped regardless.
    template<typename T>
    class Events
    {
    public:
        static constexpr uint64_t max_frame_age = 120;

        void send(T event)
        {
            entries.push_back({ std::move(event), frames, ticks });
        }

        template<typename... Args>
        void emplace(Args&&... args)
        {
            entries.push_back({ T{ std::forward<Args>(args)... }, frames, ticks });
        }

        // ids: every event gets the next one; [oldest_id, end_id) are alive
        uint64_t oldest_id() const { return first_id; }
        uint64_t end_id() const { return first_id + entries.size(); }
        const T& at(uint64_t id) const { return entries[size_t(id - first_id)].event; }
        size_t size() const { return entries.size(); }
        bool empty() const { return entries.empty(); }

        void clear()
        {
            first_id += entries.size();
            entries.clear();
        }

        // start of a frame (tick = false) or of a fixed tick: ages the events and drops the old ones
        void advance(bool tick)
        {
            if (tick)
                ++ticks;
            else
                ++frames;
            while (!entries.empty())
            {
                const Entry& e = entries.front();
                const uint64_t frame_age = frames - e.frame;
                const uint64_t tick_age = ticks - e.tick;
                if (!((frame_age >= 2 && tick_age >= 2) || frame_age > max_frame_age))
                    break;
                entries.pop_front();
                ++first_id;
            }
        }

    private:
        struct Entry
        {
            T event;
            uint64_t frame;
            uint64_t tick;
        };

        std::deque<Entry> entries;
        uint64_t first_id = 0;
        uint64_t frames = 0;
        uint64_t ticks = 0;
    };
}
