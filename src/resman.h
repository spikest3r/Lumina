#pragma once

#include <engine.h>
#include <scene.h>
#include <unordered_map>
#include <iostream>

struct ResourceArguments {
    bool looping;
    bool three_dim;
};

struct Resource {
    IResource* ptr;
    ResourceType type;
};

class ResourceManager {
public:
    ResourceManager(Engine* enginePtr, bool verbose = false);
    Texture* getTexture(std::string path);
    Mesh* getMesh(std::string path);
    Sound* getSound(std::string path, bool looping, bool three_dim);
private:
    Engine* engine;
    bool isVerbose = false;

    IResource* findResource(std::string path, ResourceType type, ResourceArguments args = {});

    std::unordered_map<std::string, Resource> resources;
};