#pragma once

#include <engine.h>
#include <scene.h>
#include <unordered_map>
#include <unordered_set>
#include "objects.h"

struct ObjectData {
    Mesh* mesh;
    Texture* texture;
};

struct GridPos
{
    int x;
    int y;
    int z;

    bool operator==(const GridPos&) const = default;
};

struct GridPosHash
{
    std::size_t operator()(const GridPos& p) const
    {
        std::size_t h1 = std::hash<int>{}(p.x);
        std::size_t h2 = std::hash<int>{}(p.y);
        std::size_t h3 = std::hash<int>{}(p.z);

        return h1 ^ (h2 << 1) ^ (h3 << 2);
    }
};

class FreeplayScene : public Scene {
protected:
    void InitScene(Engine* engine) override;
    void UpdateScene(Engine* engine) override;
    void DestroyScene(Engine* engine) override;
    
    virtual void UICallback(Engine* engine);

    void constructGameObjects(Engine* engine);

    PhysicsMaterial* baseMaterial;

    std::unordered_map<std::string, ObjectData> objectPool;
    std::unordered_map<std::string, InteractiveObject*> sceneObjects;
    
    std::vector<InteractiveObject*> objects;
    std::unordered_map<GridPos, std::string, GridPosHash> occupiedCells;

    Puppet* playerPuppet;

    std::string sourceCode; // for editor and blocks intermediate
    bool running = false;
    
    bool addObjectMenu = false;
    bool activeBrush = false;
    GameObject* brushObject;
    ObjectData* brushObjectData;
    Texture* redTexture; // for illegal placements
    bool brushUsesRedTexture = false;

    int objectIndex = 0;
    std::string getUniqueObjectName();

    float toolbarY = 0.0f;
    void setToolbarActive(bool active); // with anim;
    float targetToolbarY = 0.0f;
    float toolbarTime = 0.0f;
    bool toolbarAnimation = false;

    void createBrush(const std::string& name, Engine* engine);
};