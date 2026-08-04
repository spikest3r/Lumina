#include "freeplay.h"
#include <iostream>
#include <engine_tool_ui.h>
#include <shapes.h>

#include "lumen-inc/compiler.h"
#include "lumen-inc/vm.h"
#include "lumen-inc/programfile.h"

#include "helpers.h"
#include "objects.h"

constexpr Vector2 toolbarButtonSize = {110,50};

static Transform baseTransform = {
    {0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,1.0f},
    {1.0f,1.0f,1.0f}
};

template<typename T>
T lerp(T a, T b, float t)
{
    return a + (b - a) * t;
}

void ComputeOrbitCamera(float yawDegrees, float pitchDegrees, float distance,
                         Vector3& outPosition, Vector3& outRotation) {
    float yaw   = yawDegrees   * (3.14159265358979323846f / 180.0f);
    float pitch = pitchDegrees * (3.14159265358979323846f / 180.0f);

    // Direction the camera is FACING (toward origin)
    Vector3 front;
    front.x = cosf(yaw) * cosf(pitch);
    front.y = sinf(yaw) * cosf(pitch);
    front.z = sinf(pitch);

    // Camera sits on the opposite side of that direction, at `distance`
    outPosition.x = -front.x * distance;
    outPosition.y = -front.y * distance;
    outPosition.z = -front.z * distance;

    outRotation.x = pitchDegrees;
    outRotation.y = yawDegrees;
    outRotation.z = 0.0f;
}

void ApplyFakeGravity(GameObject* obj, Engine* engine, float dt, float fallSpeed = 9.8f, float groundOffset = 0.05f)
{
    Vector3 pos = obj->transform.position;
    Vector3 down = { 0.0f, 0.0f, -1.0f };

    RaycastHit hit = engine->raycast(pos, down, 100.0f);

    if (hit.object != nullptr)
    {
        float groundZ = pos.z - hit.distance;
        float distToGround = pos.z - groundZ;

        if (distToGround > groundOffset)
        {
            float fallStep = fallSpeed * dt;

            if (fallStep > distToGround - groundOffset)
                fallStep = distToGround - groundOffset;

            obj->transform.position.z -= fallStep;
        }
        else
        {
            obj->transform.position.z = groundZ + groundOffset;
        }
    }
    else
    {
        // No ground found — keep falling
        obj->transform.position.z -= fallSpeed * dt;
    }
}

void FreeplayScene::InitScene(Engine* engine) {
    engine->SetUICallback([this](Engine* engine)
    {
        this->UICallback(engine);
    });
    engine->setGroundPlaneActive(true);

    baseMaterial = engine->createPhysicsMaterial(0.5f,0.5f,0.5f);

    constructGameObjects(engine);

    ComputeOrbitCamera(135.0f, -30.0f, 25.0f, engine->cameraPosition, engine->cameraRotation);
}

void FreeplayScene::UICallback(Engine* engine) {
    Vector2 extents = engine->getExtents();

    ToolUI::SetNextWindowPos({0, toolbarY});
    ToolUI::SetNextWindowSize({extents.x, 70});
    ToolUI::Begin("##toolbar", true);
    if(ToolUI::Button("Run", toolbarButtonSize)) {
        CompilerData data;
        int status = compile(sourceCode, &data);
        if(status != 0) {
            std::cout << "Failed to compile\n";
        } else {
            playerPuppet->program.bytecode = std::move(data.bytecode);
            playerPuppet->program.stringPool = std::move(data.stringPool);
            playerPuppet->program.constPool = std::move(data.constPool);
            playerPuppet->program.variableCount = data.variableCount;

            for(auto& object: objects) {
                object->resetVM();
            }
            
            running = true;
        }
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Add object", toolbarButtonSize)) {
        addObjectMenu = !addObjectMenu;
    }
    ToolUI::End();
    
    // code editor
    ToolUI::Begin("Code editor");
    ToolUI::InputTextMultiline("##editor", sourceCode);
    ToolUI::End();

    // add object panel
    if(addObjectMenu && !activeBrush) {
        ToolUI::Begin("Add object");
        if(ToolUI::Button("Block")) {
            createBrush("block", engine);
            setToolbarActive(false);
        }
        ToolUI::End();
    }
}

void FreeplayScene::UpdateScene(Engine* engine) {
    float dt = engine->getDeltaTime();

    if(toolbarAnimation) {
        toolbarY = lerp(toolbarY, targetToolbarY, toolbarTime);
        toolbarTime += 10.0f * dt;
        if(toolbarTime >= 1.0f) {
            toolbarY = targetToolbarY; // dont overshoot
            toolbarAnimation = false;
        }
    }

    if(activeBrush) {
        Vector3 origin;
        Vector3 direction;

        engine->getMouseRay(origin, direction);

        if (std::abs(direction.z) > 0.0001f)
        {
            float t = -origin.z / direction.z;

            if (t >= 0.0f)
            {
                Vector3 hit = origin + direction * t;

                constexpr float grid = 2.0f;

                hit.x = std::round(hit.x / grid) * grid;
                hit.y = std::round(hit.y / grid) * grid;
                hit.z = 0.0f;

                GridPos gp;
                gp.x = static_cast<int>(hit.x);
                gp.y = static_cast<int>(hit.y);
                gp.z = static_cast<int>(hit.z);

                brushObject->transform.position = hit;
                // TODO/FIXME: Implicit convertation not allowed
                bool occupied = occupiedCells.contains(gp);
                if(occupied) {
                    if(!brushUsesRedTexture) {
                        brushUsesRedTexture = true;
                        brushObject->transform.scale = {1.05f, 1.05f, 1.05f};
                        brushObject->updateTexture(redTexture);
                    }
                } else {
                    if(brushUsesRedTexture) {
                        brushUsesRedTexture = false;
                        brushObject->transform.scale = {1.0f, 1.0f, 1.0f};
                        brushObject->updateTexture(brushObjectData->texture);
                    }
                }

                if(engine->isLastFrame()) {
                    if(!occupied && engine->getMouseButton(MouseButton::Left) == PRESS) {
                        // TODO: class for block
                        auto objectTrans = baseTransform;
                        objectTrans.position = hit;
                        auto objectMesh = brushObjectData->mesh;
                        auto objectTexture = brushObjectData->texture;
                        auto name = getUniqueObjectName();
                        InteractiveObject* newObject = engine->createGameObject<InteractiveObject>(
                            objectTrans, objectMesh, objectTexture, baseMaterial, false
                        );
                        newObject->name = name;
                        sceneObjects[name] = newObject;
                        objects.push_back(newObject);
                        occupiedCells[gp] = name;
                    } else if(occupied && engine->getMouseButton(MouseButton::Right) == PRESS) {
                        auto name = occupiedCells[gp];
                        auto object = sceneObjects[name];
                        
                        engine->requestDestroyGameObject(object);
                        sceneObjects.erase(name);
                        occupiedCells.erase(gp);

                        auto it = std::find(objects.begin(), objects.end(), object);
                        if(it != objects.end()) objects.erase(it);
                    }
                }
            }
        }

        if(engine->getKey(KeyCode::Escape) == PRESS) {
            activeBrush = false;
            engine->requestDestroyGameObject(brushObject);
            brushObject = nullptr;
            brushObjectData = nullptr;
            setToolbarActive(true);
        }
    }

    if(running) {
        for(auto& object: objects) {
            object->stepVM();
            UpdateGoToPos(object, object->goToState, dt, engine);
        }
    }

    ApplyFakeGravity(playerPuppet, engine, dt);
}

void FreeplayScene::DestroyScene(Engine* engine) {

}
    // TODO: Move to brush creation section
void FreeplayScene::constructGameObjects(Engine* engine) {
    // load puppet
    Texture* puppetTexture = engine->createTexture("puppetTexture", "assets/textures/puppet.png");
    Mesh* puppetMesh = engine->createMesh("puppetMesh", "assets/models/puppet.obj");
    objectPool["puppet"] = {puppetMesh, puppetTexture};

    // create player puppet
    auto puppetTransform = baseTransform;
    puppetTransform.position.z = 10.0f;
    playerPuppet = engine->createGameObject<Puppet>(
        puppetTransform, puppetMesh, puppetTexture, baseMaterial, false
    );
    sceneObjects["player"] = playerPuppet;
    objects.push_back(playerPuppet);

    redTexture = engine->createTexture("redTexture", "assets/textures/red.png");

    // load other objects
    Texture* blockTextureA = engine->createTexture("blockTexture", "assets/textures/block.png");
    Mesh* blockMesh = engine->createMesh("blockMesh", "assets/models/block.obj");
    objectPool["block"] = {blockMesh, blockTextureA};

    // create 5x5 field
    for(int dy = -2; dy <= 2; dy++) {
        for(int dx = -2; dx <= 2; dx++) {
            int x = dx * 2;
            int y = dy * 2;
            auto objectTrans = baseTransform;
            objectTrans.position = {static_cast<float>(x), static_cast<float>(y), 0.0f};
            GridPos gp = {x, y, 0};
            InteractiveObject* newObject = engine->createGameObject<InteractiveObject>(
                objectTrans, blockMesh, blockTextureA, baseMaterial, false
            );
            auto name = getUniqueObjectName();
            newObject->name = name;
            sceneObjects[name] = newObject;
            objects.push_back(newObject);
            occupiedCells[gp] = name;
        }
    }
}

void FreeplayScene::createBrush(const std::string& name, Engine* engine) {
    auto it = objectPool.find(name);
    if(it == objectPool.end()) {
        throw std::runtime_error("Unknown brush \'" + name + "\'");
    }

    if(brushObject) {
        // TODO: cleanup and recreate
        std::cout << "Brush already exists. Previous one IS NOT DESTROYED\n";
    }

    brushObjectData = &objectPool[name];

    auto objectMesh = it->second.mesh;
    auto objectTexture = it->second.texture;
    
    brushObject = engine->createGameObject<GameObject>(
        baseTransform, objectMesh, objectTexture, baseMaterial, false
    );
    brushObject->tag = "Brush";

    activeBrush = true;
}

std::string FreeplayScene::getUniqueObjectName() {
    return "Object" + std::to_string(objectIndex++);
}

void FreeplayScene::setToolbarActive(bool active) {
    targetToolbarY = active ? 0.0f : -70.0f;
    toolbarTime = 0.0f;
    toolbarY = active ? -70.0f : 0.0f;
    toolbarAnimation = true;
}