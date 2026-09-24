// Element types for the coopmat path, in their own tiny module so anything that
// needs them can import without dragging in the whole GEMM.

#pragma once

// fp16 in -> fp32 accumulate. That's the coopmat shape NVIDIA supports everywhere
// (16x16x16), and it maps cleanly: weights + activations are fp16 on disk and the
// activation textures are RGBA16F.

namespace NN::CoopMat
{
    public typealias InputType = float16_t;
    public typealias AccumType = float;
}
