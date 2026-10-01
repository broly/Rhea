export module animation:blend_space;

import std.compat;
import glm;
import assets;

#include "common/type_macros.h"

// Blend spaces: clips placed on a 1D or 2D parameter space, blended by where the input falls
// (UE BlendSpace / BlendSpace1D). JSON written by tools/ue_import/import_als.py:
//
//     { "axes": [ { "name": "Stride Amount", "min": 0, "max": 1, "smoothing_time": 0.2 }, ... ],
//       "samples": [ { "clip": "animations/als/....gltf", "x": 1, "y": 0, "rate_scale": 1 }, ... ],
//       "triangles": [ [1, 0, 3], ... ],    (2D, optional: built from the grid when missing)
//       "loop": true }
//
// 2D weights are barycentric in the triangle containing the input (UE5 triangulation); inputs outside
// every triangle are projected onto the nearest edge. 1D weights are linear between neighbours.
export namespace anim
{
    struct BlendSpace
    {
        DEFAULT_NON_COPYABLE(BlendSpace)

        struct Axis
        {
            std::string name;
            float min = 0.0f;
            float max = 1.0f;
            float smoothing_time = 0.0f;   // input smoothing, s (0: none)
        };
        struct Sample
        {
            AnimationClipHandle clip;
            glm::vec2 position{0.0f};
            float rate_scale = 1.0f;
        };

        std::string path;
        std::vector<Axis> axes;            // 1 or 2
        std::vector<Sample> samples;
        std::vector<std::array<uint32_t, 3>> triangles;
        bool loop = true;
        bool additive = false;             // all samples additive (of the same kind)
        AdditiveKind additive_kind = AdditiveKind::none;

        bool is_1d() const { return axes.size() == 1; }

        // weights of the samples for an input (sum 1), out sized to samples
        void weights(glm::vec2 input, std::vector<float>& out) const;
        glm::vec2 clamp_input(glm::vec2 input) const;
    };

    // cached by path; null when the file is missing
    std::shared_ptr<const BlendSpace> load_blend_space(const std::string& rel_path);
}
