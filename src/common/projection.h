#pragma once

#define PROJECTION(func) \
    [] (auto v) { return func(v); }