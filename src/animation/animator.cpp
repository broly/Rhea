module;

#include <imgui.h>

module animation;

import std.compat;
import glm;
import assets;
import ecs;
import framework;
import rhcomponents;
import cvar;
import debug_draw;
import profile;
import rhmath;

#include "ecs/ecs_macros.h"
#include "framework/scene_macros.h"
#include "profiling/profile.h"

namespace anim
{
    namespace
    {
        cvar::Var<bool> cv_draw_skeleton("anim.debug.skeleton", false, "Draws the bones of animated entities");
    }

    void Animator::init(std::shared_ptr<Rig> in_rig)
    {
        rig = std::move(in_rig);
        pool.init(rig->num_bones(), rig->curves.size());
        output.bones = rig->bind_pose;
        output.curves.assign(rig->curves.size(), 0.0f);
        sync = {};
        events.clear();
        frame = 0;
    }

    Graph Animator::begin_update(float dt)
    {
        dt = paused ? 0.0f : dt * time_scale;
        ++frame;
        events.clear();
        debug.clear();

        std::vector<std::string> montage_events;
        slots.update(dt, montage_events);
        for (std::string& name : montage_events)
            events.push_back({ std::move(name), nullptr, 1.0f });

        return Graph(*rig, pool, sync, slots, events, dt, frame);
    }

    void Animator::end_update(Graph&, PoseRef pose)
    {
        output.bones = pose->bones;
        output.curves = pose->curves;
    }

    bool Animator::has_event(std::string_view name) const
    {
        for (const Event& e : events)
            if (e.name == name)
                return true;
        return false;
    }

    void draw_animator_debug(Animator& animator)
    {
        if (!animator.valid())
        {
            ImGui::TextDisabled("Animator without a rig");
            return;
        }
        ImGui::Text("Frame %llu, poses %u allocated", (unsigned long long)animator.frame, animator.pool.allocated());
        ImGui::DragFloat("Time scale", &animator.time_scale, 0.01f, 0.0f, 4.0f);
        ImGui::Checkbox("Paused", &animator.paused);

        if (!animator.debug.empty() && ImGui::CollapsingHeader("Controller", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (ImGui::BeginTable("controller", 2, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
            {
                for (const auto& [name, value] : animator.debug)
                {
                    ImGui::TableNextRow();
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(name.c_str());
                    ImGui::TableNextColumn();
                    ImGui::TextUnformatted(value.c_str());
                }
                ImGui::EndTable();
            }
        }

        if (ImGui::CollapsingHeader("Curves"))
        {
            static bool nonzero_only = true;
            ImGui::Checkbox("Non-zero only", &nonzero_only);
            const CurveSet& curves = animator.rig->curves;
            for (uint32_t i = 0; i < curves.size(); ++i)
            {
                const float v = animator.curve(i);
                if (nonzero_only && std::abs(v) < 1e-4f)
                    continue;
                ImGui::ProgressBar(std::clamp(std::abs(v), 0.0f, 1.0f), ImVec2(120.0f, 0.0f), "");
                ImGui::SameLine();
                ImGui::Text("%-28s %.3f", curves.names[i].c_str(), v);
            }
        }

        if (ImGui::CollapsingHeader("Montages", ImGuiTreeNodeFlags_DefaultOpen))
        {
            if (animator.slots.instances().empty())
                ImGui::TextDisabled("none");
            for (const MontageInstance& m : animator.slots.instances())
            {
                const char* phase = m.phase == MontageInstance::Phase::blending_in ? "in"
                    : m.phase == MontageInstance::Phase::playing ? "playing"
                    : m.phase == MontageInstance::Phase::blending_out ? "out" : "done";
                const std::string name = !m.montage->path.empty() ? m.montage->path
                    : (!m.montage->segments.empty() ? m.montage->segments[0].clip.get().name : std::string("?"));
                ImGui::Text("[%s] %s  %.2f / %.2f s  w %.2f  %s", m.montage->slot.c_str(), name.c_str(), m.time, m.montage->length(), m.weight, phase);
            }
        }

        if (ImGui::CollapsingHeader("Sync groups"))
        {
            for (const auto& [name, g] : animator.sync.groups)
                ImGui::Text("%-12s %s  %.2f  marker %s %.2f  leader %s (w %.2f)", name.c_str(), g.valid ? "on " : "off",
                    g.normalized, g.marker.empty() ? "-" : g.marker.c_str(), g.marker_fraction,
                    g.leader.clip ? g.leader.clip->name.c_str() : "-", g.leader.weight);
        }

        if (ImGui::CollapsingHeader("Events"))
        {
            // events live one frame: keep a short history here
            static std::deque<std::string> history;
            for (const Event& e : animator.events)
            {
                char line[160];
                std::snprintf(line, sizeof(line), "%llu  %s  (w %.2f)", (unsigned long long)animator.frame, e.name.c_str(), e.weight);
                history.emplace_front(line);
            }
            while (history.size() > 24)
                history.pop_back();
            for (const std::string& line : history)
                ImGui::TextUnformatted(line.c_str());
        }
    }

    namespace
    {
        // output -> skinned mesh
        [[=ecs::system<ecs::Phase::Update>, =ecs::in_set<Apply>, =ecs::after<Evaluate>, =ecs::after<PostProcess>]]
        void apply_animators(ecs::Query<const Animator, SkinnedMesh> animated)
        {
            PROFILE("anim apply");
            animated.each([&](const Animator& animator, SkinnedMesh& mesh) {
                if (!animator.valid() || !animator.apply_to_mesh)
                    return;
                mesh.set_local_pose(pose_to_local_matrices(animator.output.bones));
            });
        }

        // debug lines only: before the exclusive Registry& systems of Late (their order does not matter)
        [[=ecs::system<ecs::Phase::Late>, =ecs::after<scene::TransformPropagation>, =ecs::before<MeshColliderSync>, =ecs::before<RenderSync>]]
        void draw_animator_skeletons(ecs::Query<const Animator, const WorldTransform> animated)
        {
            if (!cv_draw_skeleton.get())
                return;
            thread_local BonePose model;
            animated.each([&](const Animator& animator, const WorldTransform& world) {
                if (!animator.valid())
                    return;
                const Rig& rig = *animator.rig;
                rig.to_model(animator.output.bones, model);
                const glm::mat4 root = world.value.matrix() * rig.skeleton->root_transform;
                for (uint32_t bone = 0; bone < rig.num_bones(); ++bone)
                {
                    const int32_t parent = rig.parents[bone];
                    if (parent < 0)
                        continue;
                    const glm::vec3 a = glm::vec3(root * glm::vec4(model[parent].translation, 1.0f));
                    const glm::vec3 b = glm::vec3(root * glm::vec4(model[bone].translation, 1.0f));
                    debug_draw::line(a, b, glm::vec4(0.2f, 1.0f, 0.4f, 1.0f), { .depth_test = false });
                }
            });
        }

        ECS_REGISTER()
        SCENE_REGISTER_COMPONENTS(Animator)
    }
}
