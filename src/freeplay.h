#pragma once

#include <engine.h>
#include <scene.h>
#include <unordered_map>
#include "objects.h"

struct ObjectData {
    Mesh* mesh;
    Texture* texture;
};

class FreeplayScene : public Scene {
public:
    // VM commands
    void goToPos(std::string objectName, Vector3 location);
protected:
    void InitScene(Engine* engine) override;
    void UpdateScene(Engine* engine) override;
    void DestroyScene(Engine* engine) override;
    
    virtual void UICallback(Engine* engine);

    void constructGameObjects(Engine* engine);

    PhysicsMaterial* baseMaterial;
    GameObject* plane;

    std::unordered_map<std::string, ObjectData> objectPool;
    std::unordered_map<std::string, InteractiveObject*> sceneObjects;
    
    std::vector<InteractiveObject*> objects;

    Puppet* playerPuppet;

    std::string sourceCode; // for editor and blocks intermediate
    bool running = false;
};