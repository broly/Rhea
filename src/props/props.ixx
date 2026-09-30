module;

#include <json/value.h>

export module props;

import std.compat;
import glm;
import rhmath;
import reflect;
import rhobject;
import ecs;
import framework;
import assets;
import rhcomponents;

// Static props of a level: rocks, cliffs, stumps. A prop (assets/props/<name>.json, written by
// tools/props/import_props.py) is a mesh with LODs and its materials plus what it collides as; a placement
// file lists the instances (tools/props/scatter_props.py). Every instance becomes a child entity of the Props
// entity with a MeshRenderer sharing the meshes and materials of its prop, and a MeshCollider whose shape
// is the prop's, cooked once and scaled.
//
//     "Props": { "placement": "levels/sponza_props.json" }
//
//     { "instances": [ { "prop": "props/moss_rock_03.json", "position": { "x": 30, "y": 0, "z": -4 },
//                        "rotation": { "x": 0, "y": 0.38, "z": 0, "w": 0.92 }, "scale": 1.1, "sink": 0.2 } ] }
export
{
    struct Prop
    {
        MeshRenderer renderer;
        // for the tools: bounds of the mesh around its origin, and how far (m) the border of an open shell
        // (a cliff) is extended into the ground behind it, which the bounds leave out
        vec3 bounds_min{ 0.0f, 0.0f, 0.0f };
        vec3 bounds_max{ 0.0f, 0.0f, 0.0f };
        float skirt = 0.0f;
        // cooked from collision_mesh, a low LOD (the mesh of the renderer without one)
        MeshCollision collision = MeshCollision::convex_hull;
        MeshHandle collision_mesh;
    };

    struct PropInstance
    {
        std::string prop;                       // asset path of the Prop
        vec3 position{ 0.0f, 0.0f, 0.0f };      // relative to the Props entity
        quat rotation{ 0.0f, 0.0f, 0.0f, 1.0f };
        float scale = 1.0f;
        // m the origin of the prop is below the terrain (Props::snap_to_terrain)
        float sink = 0.0f;
    };

    struct PropPlacement
    {
        std::vector<PropInstance> instances;
    };

    struct Props
    {
        [[=rh::edit, =rh::read_only]] std::string placement;
        // y of the instances comes from the terrain under their origin (spawned before this entity), if
        // there is one: `sink` below it
        [[=rh::edit, =rh::read_only]] bool snap_to_terrain = true;

        [[=rh::transient]] uint32_t count = 0;
    };
}
