#pragma once

#include <string>
#include <vector>
#include <unordered_map>

struct ObjectResources {
    std::string mesh;
    std::vector<std::string> textures;
};

static std::unordered_map<std::string, ObjectResources> objectResources = {
    {"puppet", {"puppet", {"puppet"}}},
    {"block", {"block", {"block"}}},
    {"ramp", {"ramp", {"block"}}},
    {"cylinder", {"cylinder", {"block"}}},
    {"flag", {"flag", {"flag-red"}}}
};