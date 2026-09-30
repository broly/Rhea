module;

#include <json/value.h>

export module trees;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import physics;
import rhcomponents;

// Trees of a level. A species (assets/trees/<name>.json, written by tools/trees/import_trees.py) is a mesh
// with LODs and "foliage" materials (bark, leaves) plus the capsule of its trunk; a placement file lists the
// instances (tools/trees/scatter_trees.py). Every instance becomes a child entity of the Trees entity with a
// MeshRenderer sharing the meshes and materials of its species, and a TreeCollider.
//
//     "Trees": { "placement": "levels/sponza_trees.json" }
//
//     { "instances": [ { "species": "trees/cypress.json", "position": { "x": 30, "y": 0, "z": -4 },
//                        "yaw": 120, "scale": 1.1 } ] }
export
{
    struct TreeSpecies
    {
        MeshRenderer renderer;
        float height = 0.0f;                    // m
        // collision of the trunk: a capsule standing on the ground at trunk_offset; radius 0: none
        float trunk_radius = 0.0f;
        float trunk_height = 0.0f;
        vec3 trunk_offset{ 0.0f, 0.0f, 0.0f };
    };

    struct TreeInstance
    {
        std::string species;                    // asset path of the TreeSpecies
        vec3 position{ 0.0f, 0.0f, 0.0f };      // relative to the Trees entity
        float yaw = 0.0f;                       // degrees
        float scale = 1.0f;
    };

    struct TreePlacement
    {
        std::vector<TreeInstance> instances;
    };

    struct Trees
    {
        [[=rh::edit, =rh::read_only]] std::string placement;
        // y of the instances comes from the terrain under them (spawned before this entity), if there is one
        [[=rh::edit, =rh::read_only]] bool snap_to_terrain = true;
        // m the trees stand below the surface: the root flare hides the contact on slopes
        [[=rh::edit, =rh::read_only]] float sink = 0.08f;

        [[=rh::transient]] uint32_t count = 0;
    };

    // Static capsule of a tree's trunk
    struct [[=scene::runtime_only]] TreeCollider
    {
        phys::BodyId body;
    };
}
