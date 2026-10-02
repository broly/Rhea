module;

#include <json/value.h>

export module gameplay:player;

import std.compat;
import reflect;
import rhobject;

// The entity a player plays (whatever moves it: locomotion, a vehicle). Game rules and effects find the player
// by it (damage_feedback: the health on screen).
export struct Player
{
    // local player, 0 in a single player game (split screen / coop later)
    [[=rh::edit]] int index = 0;
};
