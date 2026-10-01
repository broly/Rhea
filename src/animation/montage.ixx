export module animation:montage;

import std.compat;
import glm;
import assets;

#include "common/type_macros.h"

// Montages: one-shot animations played on top of a graph in a named slot (UE AnimMontage + Slot node).
// The graph decides where a slot sits (Graph::slot); gameplay plays montages by slot name.
//
// JSON written by tools/ue_import/import_als.py:
//     { "slot": "PostLocomotion", "segments": [ { "clip": "...gltf", "start": 0, "end": 1.5, "play_rate": 1 } ],
//       "blend_in": 0.2, "blend_out": 0.2, "blend_out_trigger_time": 0,
//       "notifies": [ { "name", "time", "duration", "class", "fields": { ... } } ] }
export namespace anim
{
    struct Montage
    {
        DEFAULT_NON_COPYABLE(Montage)

        struct Segment
        {
            AnimationClipHandle clip;
            float start = 0.0f;       // in the clip
            float end = 0.0f;
            float play_rate = 1.0f;
        };
        struct Notify
        {
            std::string name;
            std::string type;          // UE class, e.g. AlsAnimNotifyState_EarlyBlendOut
            float time = 0.0f;
            float duration = 0.0f;
            std::vector<std::pair<std::string, std::string>> fields;
            std::string_view field(std::string_view key) const;
        };

        std::string path;
        std::string slot;
        std::vector<Segment> segments;
        float blend_in = 0.25f;
        float blend_out = 0.25f;
        float blend_out_trigger_time = -1.0f;   // < 0: blend out to end exactly at the end
        bool auto_blend_out = true;
        std::vector<Notify> notifies;

        float length() const;
        // segment containing montage time `t` and the clip time there
        const Segment* segment_at(float t, float& clip_time) const;

        // a montage of one clip (UE PlaySlotAnimationAsDynamicMontage)
        // blend_out_trigger_time < 0: the blend out ends with the clip, >= 0: it starts that long before the end
        static std::shared_ptr<const Montage> from_clip(const AnimationClipHandle& clip, std::string_view slot,
            float blend_in, float blend_out, float blend_out_trigger_time = -1.0f);
    };

    // cached by path; null when the file is missing
    std::shared_ptr<const Montage> load_montage(const std::string& rel_path);

    struct MontagePlay
    {
        float rate = 1.0f;
        float start_time = 0.0f;
        float blend_in = -1.0f;         // < 0: the montage's
        float blend_out = -1.0f;
        // blending out switches off at once and smooths the switch with the inertialization above the slot
        // (Graph::slot requests it), so the montage's curves stop instantly (UE EMontageBlendMode::Inertialization)
        bool inertial_blend_out = false;
    };

    struct MontageInstance
    {
        uint32_t id = 0;
        std::shared_ptr<const Montage> montage;
        float time = 0.0f;
        float previous_time = 0.0f;
        float rate = 1.0f;
        float weight = 0.0f;
        float blend_in = 0.0f;
        float blend_out = 0.0f;

        enum class Phase : uint8_t { blending_in, playing, blending_out, done };
        Phase phase = Phase::blending_in;
        float phase_time = 0.0f;
        float blend_out_start_weight = 1.0f;
        float blend_out_duration = 0.0f;
        bool inertial_blend_out = false;

        bool active() const { return phase != Phase::done; }
        bool blending_out() const { return phase == Phase::blending_out; }
    };

    // Montages of one animator, by slot.
    class Slots
    {
    public:
        // returns the instance id (0: not played). Another montage in the same slot blends out.
        uint32_t play(std::shared_ptr<const Montage> montage, const MontagePlay& params = {});
        void stop(uint32_t id, float blend_out = -1.0f);
        void stop_slot(std::string_view slot, float blend_out = -1.0f);
        void stop_all(float blend_out = -1.0f);

        bool is_playing(std::string_view slot) const;
        bool is_playing(uint32_t id) const;
        const MontageInstance* find(uint32_t id) const;
        MontageInstance* find(uint32_t id);
        // the montage of `montage` playing (not blending out), null when none
        const MontageInstance* find_playing(const Montage& montage) const;
        // jumps to a montage time (UE Montage_SetPosition); notifies in between are not raised
        void set_position(uint32_t id, float time);
        // weight of the slot (sum of its montages)
        float slot_weight(std::string_view slot) const;
        // weight left to the pose below the slot: 1 - its full body (not additive) montages
        float source_weight(std::string_view slot) const;

        // a notify state of type `type` (UE class name) currently inside its range in an active montage
        const Montage::Notify* active_notify_state(std::string_view type) const;

        // advances times and blend weights, raises notifies crossed (names) into `events`: the montage's and those
        // of the clips it plays
        void update(float dt, std::vector<std::string>& notify_events);

        // inertialization requested by montages of `slot` that stopped (inertial_blend_out); 0 when none
        float take_inertialization(std::string_view slot);

        std::span<const MontageInstance> instances() const { return active; }

    private:
        void begin_blend_out(MontageInstance& m, float duration);

        std::vector<MontageInstance> active;
        std::vector<std::pair<std::string, float>> inertialization_requests;
        uint32_t next_id = 1;
    };
}
