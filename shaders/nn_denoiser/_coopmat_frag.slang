// Coopmat fragment typealiases (CMatA/B/C) + `using namespace linalg`.
//
// This is a textual #include, not a module import, on purpose: `using namespace
// linalg` and the typealiases have to land directly in whoever pulls them in, so
// the GEMM code can say CMatA/CMatB/CMatC and call coopMatMulAdd unqualified. A
// module import wouldn't carry the `using namespace` across.
//
// Needs NN::CoopMat::InputType / AccumType in scope (import nn_coopmat_types) and
// the fragment-shape macros NN_CM_M/N/K.

using namespace linalg;

#include "_coopmat_tile.slang"   // NN_CM_TILE_PIXELS (= 64) as a literal macro

#ifndef NN_CM_M
    #define NN_CM_M 16
#endif
#ifndef NN_CM_N
    #define NN_CM_N 16
#endif
#ifndef NN_CM_K
    #define NN_CM_K 16
#endif

typealias CMatA = CoopMat<NN::CoopMat::InputType, MemoryScope.Subgroup, NN_CM_M, NN_CM_K, CoopMatMatrixUse.MatrixA>;
typealias CMatB = CoopMat<NN::CoopMat::InputType, MemoryScope.Subgroup, NN_CM_K, NN_CM_N, CoopMatMatrixUse.MatrixB>;
typealias CMatC = CoopMat<NN::CoopMat::AccumType, MemoryScope.Subgroup, NN_CM_M, NN_CM_N, CoopMatMatrixUse.MatrixAccumulator>;
