#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct ObjectResources {
    std::string mesh;

    // Default, Red, Green, Blue, Yellow
    std::vector<std::string> textures;

    float zOffset = 0.0f;
};

static std::unordered_map<std::string, ObjectResources> objectResources = {
    // objects
    {"puppet", {"puppet", {"puppet"}, 0.0f}},
    {"apple", {"apple", {"apple"}, 0.5f}},
    {"spider", {"spider", {"gray"}, 0.0f}},
    {"gecko", {"gecko", {"gecko"}, 0.2f}},
    {"ghost", {"ghost", {"ghost"}, 0.2f}},
    {"hedgehog", {"hedgehog", {"hedgehog"},0.3f}},
    // tiles
    {"block", {"block", {"block", "block-red", "block-green", "block-blue", "block-yellow"}, 0.0f}},
    {"ramp", {"ramp", {"block", "block-red", "block-green", "block-blue", "block-yellow"}, 0.0f}},
    {"cylinder", {"cylinder", {"block", "block-red", "block-green", "block-blue", "block-yellow"}, 0.0f}},
    {"flag", {"flag", {"flag-red", "flag-red", "flag-green", "flag-blue", "flag-yellow"}, 0.0f}}
};

float getZoffset(const std::string& id);