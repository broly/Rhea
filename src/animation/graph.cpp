module animation;

import std.compat;
import glm;
import assets;

namespace anim
{
    namespace
    {
        float wrap(float time, float duration, bool loop, bool* looped = nullptr)
        {
            if (duration <= 0.0f)
                return 0.0f;
            if (!loop)
                return std::clamp(time, 0.0f, duration);
            if (time >= duration || time < 0.0f)
            {
                if (looped)
                    *looped = true;
                time = std::fmod(time, duration);
                if (time < 0.0f)
                    time += duration;
            }
            return time;
        }

        glm::vec3 rotation_vector(const glm::quat& q)
        {
            glm::quat r = q.w < 0.0f ? -q : q;
            const glm::vec3 v(r.x, r.y, r.z);
            const float s = glm::length(v);
            if (s < 1e-6f)
                return v * 2.0f;
            const float angle = 2.0f * std::atan2(s, r.w);
            return v * (angle / s);
        }

        glm::quat from_rotation_vector(const glm::vec3& v)
        {
            const float angle = glm::length(v);
            if (angle < 1e-6f)
                return glm::normalize(glm::quat(1.0f, v.x * 0.5f, v.y * 0.5f, v.z * 0.5f));
            return glm::angleAxis(angle, v / angle);
        }
    }


    // ---------------------------------------------------------------- inputs

    float ScaleBiasClamp::update(float input, float dt)
    {
        float result = input;
        if (map_range)
        {
            const float range = in_max - in_min;
            const float t = range != 0.0f ? (result - in_min) / range : 0.0f;
            result = out_min + (out_max - out_min) * t;
        }
        result = result * scale + bias;
        if (clamp)
            result = std::clamp(result, clamp_min, clamp_max);
        if (interpolate)
        {
            if (initialized)
            {
                const float speed = result >= value ? speed_increasing : speed_decreasing;
                if (speed > 0.0f)
                {
                    // UE FMath::FInterpTo
                    const float delta = result - value;
                    result = value + delta * std::clamp(dt * speed, 0.0f, 1.0f);
                }
            }
            value = result;
        }
        else
            value = result;
        initialized = true;
        return result;
    }


    // ---------------------------------------------------------------- sync

    void set_sync_position(SyncGroup& group, const AnimationClip& clip, float time)
    {
        group.normalized = clip.duration > 0.0f ? std::clamp(time / clip.duration, 0.0f, 1.0f) : 0.0f;
        const auto& markers = clip.sync_markers;
        if (markers.empty() || clip.duration <= 0.0f)
        {
            group.marker.clear();
            group.marker_fraction = 0.0f;
            return;
        }
        // last marker at or before `time`, cyclic
        size_t prev = markers.size() - 1;
        float prev_time = markers.back().time - clip.duration;
        for (size_t i = 0; i < markers.size(); ++i)
            if (markers[i].time <= time)
            {
                prev = i;
                prev_time = markers[i].time;
            }
        const size_t next = (prev + 1) % markers.size();
        float next_time = markers[next].time;
        if (next_time <= prev_time)
            next_time += clip.duration;
        group.marker = markers[prev].name;
        group.marker_fraction = next_time > prev_time ? std::clamp((time - prev_time) / (next_time - prev_time), 0.0f, 1.0f) : 0.0f;
    }

    float time_from_sync_position(const SyncGroup& group, const AnimationClip& clip)
    {
        const auto& markers = clip.sync_markers;
        if (!group.marker.empty() && !markers.empty() && clip.duration > 0.0f)
        {
            for (size_t i = 0; i < markers.size(); ++i)
            {
                if (markers[i].name != group.marker)
                    continue;
                const size_t next = (i + 1) % markers.size();
                float next_time = markers[next].time;
                if (next_time <= markers[i].time)
                    next_time += clip.duration;
                float t = markers[i].time + (next_time - markers[i].time) * group.marker_fraction;
                if (t >= clip.duration)
                    t -= clip.duration;
                return t;
            }
        }
        return group.normalized * clip.duration;
    }


    // ---------------------------------------------------------------- graph

    Graph::Graph(const Rig& rig, PosePool& pool, SyncGroups& sync, Slots& slots, std::vector<Event>& events,
        float dt, uint64_t frame)
        : rig(rig), pool(pool), dt(dt), frame(frame), slots(slots), sync(sync), events(events)
    {
    }

    PoseRef Graph::bind_pose()
    {
        PoseRef pose = new_pose();
        set_bind(rig, *pose);
        return pose;
    }

    PoseRef Graph::additive_identity()
    {
        PoseRef pose = new_pose();
        set_additive_identity(*pose);
        return pose;
    }

    PoseRef Graph::copy(const PoseRef& pose)
    {
        PoseRef out = new_pose();
        out->bones = pose->bones;
        out->curves = pose->curves;
        return out;
    }

    bool Graph::touch(NodeState& node)
    {
        const bool reset = node.last_frame == 0 || reset_depth > 0;
        node.last_frame = frame;
        return reset;
    }

    bool Graph::becoming_relevant(NodeState& node)
    {
        const bool relevant_before = node.last_frame + 1 == frame || node.last_frame == frame;
        node.last_frame = frame;
        return !relevant_before || reset_depth > 0;
    }

    SyncGroup& Graph::sync_group(std::string_view name)
    {
        auto it = sync.groups.find(name);
        if (it == sync.groups.end())
            it = sync.groups.emplace(std::string(name), SyncGroup{}).first;
        SyncGroup& g = it->second;

        if (g.advanced_frame != frame)
        {
            // the leader of the previous frame moves the group on; a group unused last frame starts over
            g.last_leader = g.advanced_frame + 1 == frame ? g.leader : SyncGroup::Leader{};
            g.leader = {};
            g.valid = false;
            if (g.last_leader.clip)
            {
                const SyncGroup::Leader& l = g.last_leader;
                const float t = wrap(l.time + dt * l.rate, l.clip->duration, l.loop);
                set_sync_position(g, *l.clip, t);
                g.valid = true;
            }
            g.advanced_frame = frame;
        }
        return g;
    }

    void Graph::raise_notifies(const AnimationClip& clip, float from, float to, bool looped)
    {
        for (const AnimNotify& n : clip.notifies)
        {
            const bool crossed = looped
                ? (n.time > from || n.time <= to)
                : (n.time > from && n.time <= to);
            if (crossed)
                events.push_back({ n.name, &n, current_weight });
        }
    }

    float Graph::advance_player_time(SequencePlayer& node, const AnimationClip& clip, const PlayParams& params, bool reset)
    {
        const float rate = params.rate * clip.rate_scale;
        const float previous = node.time;
        bool looped = false;

        if (reset)
            node.time = wrap(params.start, clip.duration, params.loop);

        if (!params.sync.empty())
        {
            SyncGroup& group = sync_group(params.sync);
            if (group.valid)
            {
                const float t = time_from_sync_position(group, clip);
                looped = params.loop && !reset && rate >= 0.0f && t < previous;
                node.time = params.loop ? t : std::clamp(t, 0.0f, clip.duration);
            }
            else
            {
                if (!reset)
                    node.time = wrap(node.time + dt * rate, clip.duration, params.loop, &looped);
                set_sync_position(group, clip, node.time);
                group.valid = true;
            }
            if (!params.sync_follower && current_weight > group.leader.weight)
                group.leader = { &clip, node.time, rate, current_weight, params.loop };
        }
        else if (!reset)
            node.time = wrap(node.time + dt * rate, clip.duration, params.loop, &looped);

        if (params.notifies && !reset)
            raise_notifies(clip, previous, node.time, looped);
        node.previous_time = previous;
        return node.time;
    }

    PoseRef Graph::play(SequencePlayer& node, const AnimationClip& clip, const PlayParams& params)
    {
        const bool already_updated = node.last_frame == frame && node.clip == &clip;
        const bool reset = touch(node) || node.clip != &clip;
        node.clip = &clip;
        node.weight = current_weight;
        if (!already_updated)
            advance_player_time(node, clip, params, reset);

        PoseRef pose = new_pose();
        sample(rig, clip, node.time, params.loop, *pose);
        return pose;
    }

    PoseRef Graph::evaluate(const AnimationClip& clip, float time, bool loop)
    {
        PoseRef pose = new_pose();
        sample(rig, clip, time, loop, *pose);
        return pose;
    }

    PoseRef Graph::evaluate_frame(const AnimationClipHandle& clip, int32_t frame_index)
    {
        const AnimationClip& c = clip.get();
        const float time = c.key_count > 1 ? c.key_interval * float(frame_index)
            : (c.duration > 0.0f ? std::min(float(frame_index) / 30.0f, c.duration) : 0.0f);
        return evaluate(c, time, false);
    }

    PoseRef Graph::play(BlendSpacePlayer& node, const BlendSpace& space, glm::vec2 input, const PlayParams& params)
    {
        const bool already_updated = node.last_frame == frame;
        const bool reset = touch(node);
        node.weight = current_weight;

        input = space.clamp_input(input);
        if (!already_updated)
        {
            for (size_t i = 0; i < space.axes.size() && i < 2; ++i)
            {
                const float time = space.axes[i].smoothing_time;
                if (reset || time <= 0.0f)
                    node.input[(int)i] = input[(int)i];
                else
                    node.input[(int)i] += (input[(int)i] - node.input[(int)i]) * (1.0f - std::exp(-dt * 4.0f / time));
            }
        }
        space.weights(node.input, node.sample_weights);
        if (space.samples.empty())
            return bind_pose();

        const size_t leader = (size_t)std::distance(node.sample_weights.begin(), std::ranges::max_element(node.sample_weights));
        const AnimationClip& leader_clip = space.samples[leader].clip.get();
        const float rate = params.rate * space.samples[leader].rate_scale * leader_clip.rate_scale;

        // cycle position: the blend space's own, or the sync group's
        if (!already_updated)
        {
            if (params.sync.empty())
            {
                if (reset || !node.position.valid)
                {
                    set_sync_position(node.position, leader_clip, params.start);
                    node.position.valid = true;
                }
                else
                {
                    const float t = wrap(time_from_sync_position(node.position, leader_clip) + dt * rate, leader_clip.duration, space.loop);
                    set_sync_position(node.position, leader_clip, t);
                }
            }
            else
            {
                SyncGroup& group = sync_group(params.sync);
                if (!group.valid)
                {
                    const float t = reset || !node.position.valid ? params.start
                        : wrap(time_from_sync_position(node.position, leader_clip) + dt * rate, leader_clip.duration, space.loop);
                    set_sync_position(group, leader_clip, t);
                    group.valid = true;
                }
                node.position.valid = true;
                node.position.normalized = group.normalized;
                node.position.marker = group.marker;
                node.position.marker_fraction = group.marker_fraction;
                if (!params.sync_follower && current_weight > group.leader.weight)
                    group.leader = { &leader_clip, time_from_sync_position(group, leader_clip), rate, current_weight, space.loop };
            }
        }

        // samples
        std::array<PoseRef, max_blend_children> poses;
        std::array<const Pose*, max_blend_children> pointers{};
        std::array<float, max_blend_children> weights{};
        uint32_t count = 0;
        for (size_t i = 0; i < space.samples.size() && count < max_blend_children; ++i)
        {
            const float w = node.sample_weights[i];
            if (w <= weight_epsilon)
                continue;
            const AnimationClip& clip = space.samples[i].clip.get();
            poses[count] = new_pose();
            sample(rig, clip, time_from_sync_position(node.position, clip), space.loop, *poses[count]);
            pointers[count] = poses[count].operator->();
            weights[count] = w;
            ++count;
        }
        if (count == 1)
            return std::move(poses[0]);
        blend_weighted(std::span(pointers.data(), count), std::span(weights.data(), count), *poses[0]);
        return std::move(poses[0]);
    }

    PoseRef Graph::evaluate(BlendSpacePlayer& node, const BlendSpace& space, glm::vec2 input, float normalized_time)
    {
        const bool reset = touch(node);
        input = space.clamp_input(input);
        for (size_t i = 0; i < space.axes.size() && i < 2; ++i)
        {
            const float time = space.axes[i].smoothing_time;
            if (reset || time <= 0.0f)
                node.input[(int)i] = input[(int)i];
            else
                node.input[(int)i] += (input[(int)i] - node.input[(int)i]) * (1.0f - std::exp(-dt * 4.0f / time));
        }
        space.weights(node.input, node.sample_weights);
        if (space.samples.empty())
            return bind_pose();

        std::array<PoseRef, max_blend_children> poses;
        std::array<const Pose*, max_blend_children> pointers{};
        std::array<float, max_blend_children> weights{};
        uint32_t count = 0;
        for (size_t i = 0; i < space.samples.size() && count < max_blend_children; ++i)
        {
            const float w = node.sample_weights[i];
            if (w <= weight_epsilon)
                continue;
            const AnimationClip& clip = space.samples[i].clip.get();
            poses[count] = new_pose();
            sample(rig, clip, normalized_time * clip.duration, false, *poses[count]);
            pointers[count] = poses[count].operator->();
            weights[count] = w;
            ++count;
        }
        if (count == 1)
            return std::move(poses[0]);
        blend_weighted(std::span(pointers.data(), count), std::span(weights.data(), count), *poses[0]);
        return std::move(poses[0]);
    }

    PoseRef Graph::blend(PoseRef a, PoseRef b, float alpha)
    {
        anim::blend(*a, *b, std::clamp(alpha, 0.0f, 1.0f), *a);
        return a;
    }

    void Graph::update_blend_list(BlendList& node, int32_t active, uint32_t count, float blend_time, const BlendShape& curve)
    {
        const bool reset = touch(node);
        if (node.weights.size() != count)
        {
            node.weights.assign(count, 0.0f);
            node.from.assign(count, 0.0f);
            node.active = -1;
        }
        node.activated_frame_child = -1;
        active = std::clamp(active, 0, (int32_t)count - 1);

        if (reset || node.active < 0)
        {
            std::ranges::fill(node.weights, 0.0f);
            std::ranges::fill(node.from, 0.0f);
            node.weights[active] = node.from[active] = 1.0f;
            node.active = active;
            node.elapsed = node.duration = 0.0f;
            if (node.reset_on_activation)
                node.activated_frame_child = active;
            return;
        }
        if (active != node.active)
        {
            if (node.reset_on_activation && node.weights[active] <= weight_epsilon)
                node.activated_frame_child = active;
            node.from = node.weights;
            node.active = active;
            node.elapsed = 0.0f;
            node.duration = blend_time;
            node.curve = curve;
        }
        node.elapsed += dt;
        const float a = node.curve(node.duration > 0.0f ? node.elapsed / node.duration : 1.0f);
        for (uint32_t i = 0; i < count; ++i)
            node.weights[i] = (int32_t)i == node.active ? node.from[i] + (1.0f - node.from[i]) * a : node.from[i] * (1.0f - a);
    }


    // ---------------------------------------------------------------- state machines

    void StateMachineBase::start_transition(int32_t to, const Transition& t)
    {
        float start = 0.0f;
        if (t.inertialize || t.duration <= 0.0f)
        {
            fading.clear();
            inertialization_pending = t.inertialize;
        }
        else
        {
            // every state active now fades out with the weight it has now
            const float a = blending() ? transition_info.curve(transition_alpha()) : 1.0f;
            std::vector<Fading> next;
            next.push_back({ current, a });
            for (const Fading& f : fading)
                next.push_back({ f.state, f.weight * (1.0f - a) });

            // a state that is still blending out comes back from its weight (not reset, not counted twice)
            std::vector<Fading> merged;
            for (const Fading& f : next)
            {
                if (f.state == to)
                {
                    start += f.weight;
                    continue;
                }
                auto it = std::ranges::find(merged, f.state, &Fading::state);
                if (it != merged.end())
                    it->weight += f.weight;
                else if (f.weight > 1e-4f)
                    merged.push_back(f);
            }
            fading = std::move(merged);
            inertialization_pending = false;
        }

        const bool was_active = start > 0.0f;
        transition_info = t;
        transition_elapsed = 0.0f;
        start_alpha = std::clamp(start, 0.0f, 1.0f);
        previous = current;
        current = to;
        state_time = 0.0f;
        entered_frame = last_frame + 1;
        reset_current = !was_active;
        pending_notify = std::string(t.notify);
    }

    float StateMachineBase::current_weight() const
    {
        return blending() ? transition_info.curve(transition_alpha()) : 1.0f;
    }

    float StateMachineBase::transition_alpha() const
    {
        if (!blending() || transition_info.duration <= 0.0f)
            return 1.0f;
        const float t = std::clamp(transition_elapsed / (transition_info.duration * (1.0f - start_alpha) + 1e-6f), 0.0f, 1.0f);
        return start_alpha + (1.0f - start_alpha) * t;
    }

    void Graph::begin_state_machine(StateMachineBase& sm)
    {
        const bool reset = touch(sm);
        if (reset)
        {
            sm.reset_current = true;
            sm.fading.clear();
        }
        if (sm.entered_frame != frame)
            sm.state_time += dt;
        if (sm.blending())
        {
            sm.transition_elapsed += dt;
            if (sm.transition_alpha() >= 1.0f)
                sm.fading.clear();
        }
        if (sm.inertialization_pending)
        {
            request_inertialization(sm.transition_info.duration, sm.transition_info.profile);
            sm.inertialization_pending = false;
        }
        if (!sm.pending_notify.empty())
        {
            raise(sm.pending_notify);
            sm.pending_notify.clear();
        }
    }

    void Graph::blend_transition(StateMachineBase& sm, Pose& from_inout, const Pose& to, float alpha)
    {
        const BlendProfile* profile = sm.transition_info.profile;
        if (!profile)
        {
            anim::blend(from_inout, to, alpha, from_inout);
            return;
        }
        thread_local std::vector<float> weights;
        weights.resize(from_inout.bones.size());
        const float linear = sm.transition_alpha();
        for (uint32_t i = 0; i < weights.size(); ++i)
            weights[i] = profile->bone_weight(i, linear, sm.transition_info.curve);
        blend_per_bone(from_inout, to, weights, alpha, from_inout);
    }


    // ---------------------------------------------------------------- inertialization

    void Graph::request_inertialization(float duration, const BlendProfile* profile)
    {
        if (inertialization_stack.empty())
            return;
        Inertialization& node = *inertialization_stack.back();
        if (duration > node.requested)
        {
            node.requested = duration;
            node.requested_profile = profile;
        }
    }

    PoseRef Graph::finish_inertialization(Inertialization& node, PoseRef pose)
    {
        const bool becoming_relevant = !node.updated_last_frame(frame) && node.last_frame != frame;
        touch(node);
        if (becoming_relevant && node.reset_on_becoming_relevant)
        {
            node.active = false;
            node.has_last = false;
        }

        const size_t n = pose->bones.size();
        const size_t nc = pose->curves.size();

        if (node.requested >= 0.0f)
        {
            if (node.has_last && node.requested > 0.0f)
            {
                node.source = node.last;
                node.source_linear = node.linear_velocity;
                node.source_angular = node.angular_velocity;
                node.source_curve = node.curve_velocity;
                node.active = true;
                node.elapsed = 0.0f;
                node.duration = node.requested;
                node.bone_durations.clear();
                if (const BlendProfile* profile = node.requested_profile)
                {
                    node.bone_durations.resize(n);
                    for (size_t i = 0; i < n; ++i)
                    {
                        const float scale = i < profile->scales.size() ? profile->scales[i] : 1.0f;
                        node.bone_durations[i] = profile->mode == BlendProfile::Mode::weight_factor
                            ? node.duration / std::max(scale, 1e-3f)
                            : node.duration * std::clamp(scale, 0.0f, 1.0f);
                    }
                }
            }
            node.requested = -1.0f;
            node.requested_profile = nullptr;
        }

        if (node.active)
        {
            node.elapsed += dt;
            const float decay = std::exp(-dt * 0.69314718f / std::max(node.velocity_halflife, 1e-3f));
            bool any = false;
            for (size_t i = 0; i < n; ++i)
            {
                BoneTransform& src = node.source.bones[i];
                src.translation += node.source_linear[i] * dt;
                src.rotation = glm::normalize(from_rotation_vector(node.source_angular[i] * dt) * src.rotation);
                node.source_linear[i] *= decay;
                node.source_angular[i] *= decay;

                const float d = node.bone_durations.empty() ? node.duration : node.bone_durations[i];
                const float t = d > 0.0f ? std::clamp(node.elapsed / d, 0.0f, 1.0f) : 1.0f;
                if (t < 1.0f)
                    any = true;
                const float a = t * t * (3.0f - 2.0f * t);
                BoneTransform& out = pose->bones[i];
                out.translation = glm::mix(src.translation, out.translation, a);
                out.rotation = nlerp(src.rotation, out.rotation, a);
                out.scale = glm::mix(src.scale, out.scale, a);
            }
            const float tc = node.duration > 0.0f ? std::clamp(node.elapsed / node.duration, 0.0f, 1.0f) : 1.0f;
            const float ac = tc * tc * (3.0f - 2.0f * tc);
            for (size_t c = 0; c < nc; ++c)
            {
                node.source.curves[c] += node.source_curve[c] * dt;
                node.source_curve[c] *= decay;
                if (std::ranges::find(node.filtered_curves, (uint32_t)c) != node.filtered_curves.end())
                    continue;
                pose->curves[c] = node.source.curves[c] + (pose->curves[c] - node.source.curves[c]) * ac;
            }
            if (!any && tc >= 1.0f)
                node.active = false;
        }

        // velocity of the output, for the next request
        node.linear_velocity.resize(n, glm::vec3(0.0f));
        node.angular_velocity.resize(n, glm::vec3(0.0f));
        node.curve_velocity.resize(nc, 0.0f);
        if (node.has_last && dt > 0.0f)
        {
            const float inv_dt = 1.0f / dt;
            for (size_t i = 0; i < n; ++i)
            {
                node.linear_velocity[i] = (pose->bones[i].translation - node.last.bones[i].translation) * inv_dt;
                node.angular_velocity[i] = rotation_vector(pose->bones[i].rotation * glm::inverse(node.last.bones[i].rotation)) * inv_dt;
            }
            for (size_t c = 0; c < nc; ++c)
                node.curve_velocity[c] = (pose->curves[c] - node.last.curves[c]) * inv_dt;
        }
        else
        {
            std::ranges::fill(node.linear_velocity, glm::vec3(0.0f));
            std::ranges::fill(node.angular_velocity, glm::vec3(0.0f));
            std::ranges::fill(node.curve_velocity, 0.0f);
        }
        node.last.bones = pose->bones;
        node.last.curves = pose->curves;
        node.has_last = true;
        return pose;
    }


    // ---------------------------------------------------------------- montages

    PoseRef Graph::slot(std::string_view slot_name, PoseRef source)
    {
        if (const float duration = slots.take_inertialization(slot_name); duration > 0.0f)
            request_inertialization(duration);
        for (const MontageInstance& m : slots.instances())
        {
            if (!m.active() || m.weight <= weight_epsilon || m.montage->slot != slot_name)
                continue;
            float clip_time = 0.0f;
            const Montage::Segment* segment = m.montage->segment_at(m.time, clip_time);
            if (!segment)
                continue;
            const AnimationClip& clip = segment->clip.get();
            PoseRef pose = new_pose();
            sample(rig, clip, clip_time, false, *pose);
            if (clip.is_additive())
                apply_additive(rig, *source, *pose, clip.additive, m.weight);
            else
                anim::blend(*source, *pose, m.weight, *source);
        }
        return source;
    }


    // ---------------------------------------------------------------- root motion

    BoneTransform root_transform(const Rig& rig, const Montage& montage, float time)
    {
        float clip_time = 0.0f;
        const Montage::Segment* segment = montage.segment_at(time, clip_time);
        if (!segment || !segment->clip.is_valid())
            return rig.bind_pose[rig.root];
        return root_transform(rig, segment->clip.get(), clip_time);
    }
}
