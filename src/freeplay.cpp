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

#define MAX_WORLD_HEIGHT 4

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

void ComputeOrbitCamera(Vector3 currentPosition,
                        float yawDegrees,
                        float pitchDegrees,
                        float distance,
                        Vector3& outPosition,
                        Vector3& outRotation)
{
    float yaw   = yawDegrees   * (3.14159265358979323846f / 180.0f);
    float pitch = pitchDegrees * (3.14159265358979323846f / 180.0f);

    Vector3 front;
    front.x = cosf(yaw) * cosf(pitch);
    front.y = sinf(yaw) * cosf(pitch);
    front.z = sinf(pitch);

    // Orbit around currentPosition
    outPosition.x = currentPosition.x - front.x * distance;
    outPosition.y = currentPosition.y - front.y * distance;
    outPosition.z = currentPosition.z - front.z * distance;

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

    ComputeOrbitCamera(cameraTarget, cameraYaw, cameraPitch, cameraDist, engine->cameraPosition, engine->cameraRotation);
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
        if(ToolUI::Button("Eraser")) {
            createBrush("erase", engine);
            setToolbarActive(false);
        }
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

    if(!eraseBrush) {
        if(engine->getKey(KeyCode::E) == PRESS) {
            if(!activeBrush) {
                createBrush("erase", engine);
                setToolbarActive(false);
            } else {
                eraseBrush = true;
                brushObject->transform.scale = {0.0f,0.0f,0.0f};
            }
        }
    }

    if(!activeBrush && !lastBrushName.empty()) {
        if(engine->getKey(KeyCode::Q) == PRESS) {
            createBrush(lastBrushName, engine);
            setToolbarActive(false);
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

                int scrollDelta = std::round(engine->getScrollDelta());

                hit.x = std::round(hit.x / grid) * grid;
                hit.y = std::round(hit.y / grid) * grid;

                GridPos gp;
                gp.x = static_cast<int>(hit.x);
                gp.y = static_cast<int>(hit.y);

                if (scrollDelta < 0) // scroll down
                {
                    if (brushZ > 0)
                        brushZ--;
                }
                else if (scrollDelta > 0) // scroll up
                {
                    gp.z = brushZ;
                    bool currentOccupied = occupiedCells.contains(gp);

                    gp.z = brushZ + 1;
                    bool aboveOccupied = (brushZ + 1 <= MAX_WORLD_HEIGHT) && occupiedCells.contains(gp);

                    if ((currentOccupied || aboveOccupied) && brushZ < MAX_WORLD_HEIGHT)
                        brushZ++;
                }

                brushZ = std::clamp(brushZ, 0, MAX_WORLD_HEIGHT);

                hit.z = static_cast<float>(brushZ) * grid;
                gp.z = brushZ;

                brushObject->transform.position = hit;
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
                        if(!eraseBrush)
                            brushObject->transform.scale = {1.0f, 1.0f, 1.0f};
                        else
                            brushObject->transform.scale = {0.0f,0.0f,0.0f};
                        if(brushObjectData)
                            brushObject->updateTexture(brushObjectData->texture);
                    }
                }

                auto placeButton = MouseButton::Left;
                auto deleteButton = eraseBrush ? MouseButton::Left : MouseButton::Right;

                if(engine->isLastFrame()) {
                    if(!eraseBrush && !occupied && engine->getMouseButton(placeButton) == PRESS) {
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
                    } else if(occupied && engine->getMouseButton(deleteButton) == PRESS) {
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
            eraseBrush = false;
            engine->requestDestroyGameObject(brushObject);
            brushObject = nullptr;
            brushObjectData = nullptr;
            setToolbarActive(true);
        }
    } else {
        // rotate camera with mouse buttons
        static bool wasHeldLeft = false;
        static bool wasHeldRight = false;
        static Vector2 lastMousePosLeft;
        static Vector2 lastMousePosRight;

        bool heldLeft = engine->getMouseButton(MouseButton::Left) == PRESS;
        bool heldRight = engine->getMouseButton(MouseButton::Right) == PRESS;

        // orbit
        if (heldLeft && !heldRight)
        {
            Vector2 mousePos = engine->getMousePos();

            if (!wasHeldLeft)
                lastMousePosLeft = mousePos;

            Vector2 delta = mousePos - lastMousePosLeft;

            cameraYaw   += -delta.x * sensitivity;
            cameraPitch -= delta.y * sensitivity;

            cameraPitch = std::clamp(cameraPitch, -89.0f, 89.0f);

            lastMousePosLeft = mousePos;
        }

        // zoom
        float scrollDelta = engine->getScrollDelta();

        if (scrollDelta != 0.0f)
        {
            cameraDist -= scrollDelta * zoomSensitivity;
            cameraDist = std::clamp(cameraDist, 1.0f, 100.0f);
        }

        // pan
        if (heldRight && !heldLeft)
        {
            Vector2 mousePos = engine->getMousePos();

            if (!wasHeldRight)
                lastMousePosRight = mousePos;

            Vector2 delta = mousePos - lastMousePosRight;

            const float sensitivity = 0.01f;

            constexpr Vector3 up = {0.0f,0.0f,1.0f}; // Z+ up
            Vector3 forward, right;
            engine->getCameraVectors(forward, right);

            cameraTarget =
                cameraTarget
                - right * (delta.x * sensitivity)
                + up    * (delta.y * sensitivity);

            lastMousePosRight = mousePos;
        }

        if(heldLeft || heldRight || scrollDelta != 0.0f) {
            ComputeOrbitCamera(cameraTarget, cameraYaw, cameraPitch, cameraDist, engine->cameraPosition, engine->cameraRotation);
        }

        wasHeldLeft = heldLeft;
        wasHeldRight = heldRight;
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
    eraseBrush = name == "erase";

    std::unordered_map<std::string, ObjectData>::iterator it;
    if(!eraseBrush) {
        it = objectPool.find(name);
        if(it == objectPool.end()) {
            throw std::runtime_error("Unknown brush \'" + name + "\'");
        }
        lastBrushName = name;
    }

    if(brushObject) {
        // TODO: cleanup and recreate
        std::cout << "Brush already exists. Previous one IS NOT DESTROYED\n";
    }

    Mesh* objectMesh = nullptr;
    Texture* objectTexture = nullptr;

    if(!eraseBrush) {
        brushObjectData = &objectPool[name];

        objectMesh = it->second.mesh;
        objectTexture = it->second.texture;
    } else {
        objectMesh = objectPool["block"].mesh;
        objectTexture = redTexture;
    }
    
    brushObject = engine->createGameObject<GameObject>(
        baseTransform, objectMesh, objectTexture, baseMaterial, false
    );
    brushObject->tag = "Brush";

    if(eraseBrush) {
        brushObject->transform.scale = {0.0f,0.0f,0.0f};
    }

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