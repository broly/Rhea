// _coopmat_tile.slang — tile size as a MACRO (not a module const) so it expands
// to the same literal token everywhere, avoiding Slang treating
// 'K * NN_CM_TILE_PIXELS' (const) and 'K * 64' (literal) as different array types.
// #include this (do NOT import it); macros don't cross module 'import'.
#pragma once
#ifndef NN_CM_TILE_X
    #define NN_CM_TILE_X 8
#endif
#ifndef NN_CM_TILE_Y
    #define NN_CM_TILE_Y 8
#endif
#ifndef NN_CM_TILE_PIXELS
    #define NN_CM_TILE_PIXELS 64
#endif
