#include "freeplay.h"
#include <iostream>
#include <engine_tool_ui.h>
#include <shapes.h>

#include "lumen-inc/compiler.h"
#include "lumen-inc/vm.h"
#include "lumen-inc/programfile.h"

#include "helpers.h"
#include "objects.h"
#include "objectresources.h"

#include <typeinfo>

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
    resourceManager = new ResourceManager(engine);

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
    if(ToolUI::Button(running ? "Stop" : "Run", toolbarButtonSize)) {
        if(running) {
            running = false;
        } else {
            CompilerData data;
            int status = compile(sourceCode, &data);
            if(status != 0) {
                std::cout << "Failed to compile\n";
            } else {
                // TODO: compile for each object
                playerPuppet->program.bytecode = std::move(data.bytecode);
                playerPuppet->program.stringPool = std::move(data.stringPool);
                playerPuppet->program.constPool = std::move(data.constPool);
                playerPuppet->program.variableCount = data.variableCount;

                for(auto& object: objects) {
                    object->resetVM();
                }
                
                objectCount = objects.size();
                running = true;

                propertiesPanelType = CLOSED;
                if(textureSelection) {
                    propertiesObject->updateTexture(propertiesObjectTexture);
                    textureSelection = false;
                }
                propertiesObject = nullptr;
                propertiesObjectTexture = nullptr;
            }
        }
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Add object", toolbarButtonSize)) {
        if(!addObjectMenu && running) {
            // TODO: Dialog
            std::cout << "Not Allowed\n";
        } else {
            addObjectMenu = !addObjectMenu;
        }
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Camera", toolbarButtonSize)) {
        cameraMode = true;
        setToolbarActive(false);
    }
    ToolUI::End();
    
    // code editor
    if(codeMode && toolbarActive) {
        ToolUI::SetNextWindowSize({extents.x / 3, extents.y - 70});
        ToolUI::SetNextWindowPos({0, 70});
        ToolUI::Begin("Code editor", false, false);
        ToolUI::InputTextMultiline("##editor", sourceCode);
        ToolUI::End();
    }

    // add object panel
    if(addObjectMenu && toolbarActive && !running) {
        ToolUI::Begin("Add object");
        // Special brushes
        ToolUI::Text("Tools");
        if(ToolUI::Button("Eraser")) {
            createBrush("erase", engine);
        }
        // Simple tiles
        ToolUI::Text("Regular tiles");
        if(ToolUI::Button("Block")) {
            createBrush("block", engine);
        }
        if(ToolUI::Button("Ramp")) {
            createBrush("ramp", engine);
        }
        if(ToolUI::Button("Cylinder")) {
            createBrush("cylinder", engine);
        }
        ToolUI::Text("Special tiles");
        if(ToolUI::Button("Flag")) {
            createBrush("flag", engine);
        }
        // Special tiles
        ToolUI::End();
    }
    
    // properties panel
    if(propertiesPanelType != CLOSED && toolbarActive && propertiesObject) {
        ToolUI::Begin("Object properties");
        ToolUI::TextField("Name", propertiesObject->name, true);
        std::string idText = "Unique ID: " + propertiesObject->id;
        ToolUI::Text(idText.c_str());
        std::string typeText = "Type: " + propertiesObject->type;
        ToolUI::Text(typeText.c_str());
        if(propertiesPanelType == INTERACTIVE) {
            // position
            ToolUI::InputFloat3("Position", propertiesObject->transform.position);
        }
        ToolUI::End();
    }
}

void FreeplayScene::UpdateScene(Engine* engine) {
    float dt = engine->getDeltaTime();

    auto altState = engine->getKey(KeyCode::LeftAlt);
    if(altState == PRESS) hotkeyToggle = true;
    else if(altState == RELEASE) hotkeyToggle = false;

    static bool escHeld = false;
    auto escState = engine->getKey(KeyCode::Escape);
    bool escOnFrame = escState == PRESS && !escHeld;
    if(escState == PRESS && !escHeld) {
        escHeld = true;
    } else if(escState == RELEASE && escHeld) {
        escHeld = false;
    }

    if(toolbarAnimation) {
        toolbarY = lerp(toolbarY, targetToolbarY, toolbarTime);
        toolbarTime += 10.0f * dt;
        if(toolbarTime >= 1.0f) {
            toolbarY = targetToolbarY; // dont overshoot
            toolbarAnimation = false;
        }
    }

    if(!eraseBrush) {
        if(hotkeyToggle && engine->getKey(KeyCode::E) == PRESS) {
            if(!activeBrush) {
                createBrush("erase", engine);
            } else {
                eraseBrush = true;
                brushObject->transform.scale = {0.0f,0.0f,0.0f};
            }
        }
    }

    if(!activeBrush && !lastBrushName.empty()) {
        if(hotkeyToggle && engine->getKey(KeyCode::Q) == PRESS) {
            createBrush(lastBrushName, engine);
        }
    }

    if(!cameraMode && toolbarActive) {
        if(hotkeyToggle && engine->getKey(KeyCode::C) == PRESS) {
            cameraMode = true;
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
                        brushObject->updateTexture(resourceManager->getTexture("red"));
                    }
                } else {
                    if(brushUsesRedTexture) {
                        brushUsesRedTexture = false;
                        if(!eraseBrush) {
                            brushObject->transform.scale = {1.0f, 1.0f, 1.0f};
                            brushObject->updateTexture(brushObjectData.texture);
                        }
                        else
                            brushObject->transform.scale = {0.0f,0.0f,0.0f};
                    }
                }

                auto placeButton = MouseButton::Left;
                auto deleteButton = eraseBrush ? MouseButton::Left : MouseButton::Right;

                bool placeBtnPress = engine->getMouseButton(placeButton) == PRESS;
                bool deleteBtnPress = engine->getMouseButton(deleteButton) == PRESS;

                if(gp != prevGP && (!placeBtnPress && !deleteBtnPress) && (!eraseBrush || occupied)) {
                    Sound* brushMoveSound = resourceManager->getSound("move", false, true);
                    brushObject->playSound(brushMoveSound, 1.0f);
                }
                prevGP = gp;

                if(engine->isLastFrame()) {
                    if(!eraseBrush && !occupied && placeBtnPress) {
                        // TODO: class for block
                        auto objectTrans = baseTransform;
                        objectTrans.position = hit;
                        auto objectMesh = brushObjectData.mesh;
                        auto objectTexture = brushObjectData.texture;
                        auto name = getUniqueObjectName();
                        InteractiveObject* newObject = engine->createGameObject<InteractiveObject>(
                            objectTrans, objectMesh, objectTexture, baseMaterial, false
                        );
                        newObject->name = name;
                        newObject->id = name;
                        newObject->type = lastBrushName;
                        sceneObjects[name] = newObject;
                        occupiedCells[gp] = name;

                        Sound* brushPlaceSound = resourceManager->getSound("place", false, true);
                        brushObject->playSound(brushPlaceSound, 1.0f);
                    } else if(occupied && deleteBtnPress) {
                        auto id = occupiedCells[gp];
                        auto object = sceneObjects[id];

                        // if we delete opened object, deactivate panel
                        if(propertiesObject == object) {
                            propertiesPanelType = CLOSED;
                            propertiesObject = nullptr;
                        }
                        
                        engine->requestDestroyGameObject(object);
                        sceneObjects.erase(id);
                        occupiedCells.erase(gp);

                        auto it = std::find(objects.begin(), objects.end(), object);
                        if(it != objects.end()) objects.erase(it);

                        Sound* brushDeleteSound = resourceManager->getSound("delete", false, true);
                        brushObject->playSound(brushDeleteSound, 1.0f);
                    }
                }
            }
        }

        if(escOnFrame) {
            activeBrush = false;
            eraseBrush = false;
            engine->requestDestroyGameObject(brushObject);
            brushObject = nullptr;
            brushObjectData = {nullptr, nullptr};
            setToolbarActive(true);
        }
    } else if(cameraMode) {
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

        if(escOnFrame) {
            cameraMode = false;
            setToolbarActive(true);
        }
    } else if(!running) {
        // properties panel
        static bool isLeftHeld;
        bool leftPress = engine->getMouseButton(MouseButton::Left) == PRESS;

        if(leftPress && !isLeftHeld) {
            isLeftHeld = true;

            Vector3 origin;
            Vector3 direction;
            engine->getMouseRay(origin, direction);
            RaycastHit hit = engine->raycast(origin, direction, 100.0f);
            if(hit.object) {
                auto object = dynamic_cast<Tile*>(hit.object);
                if(object) {
                    auto data = getObjectData(object->type);
                    propertiesObjectTexture = data.texture;

                    propertiesObject = object;

                    PropertiesPanelType type = TILE;
                    if(dynamic_cast<InteractiveObject*>(object) != nullptr) {
                        type = INTERACTIVE;
                    }

                    propertiesPanelType = type;
                }
            }
        } else if(!leftPress && isLeftHeld) {
            isLeftHeld = false;
        }

        if(escOnFrame && propertiesPanelType != CLOSED) {
            propertiesObject->updateTexture(propertiesObjectTexture);

            propertiesPanelType = CLOSED;
            propertiesObject = nullptr;
            propertiesObjectTexture = nullptr;
            textureSelection = false;
        }
    }

    if(propertiesPanelType != CLOSED) {
        if(toolbarActive && !textureSelection) {
            textureSelection = true;
            propertiesObject->updateTexture(resourceManager->getTexture("yellow"));
        } else if(!toolbarActive && textureSelection) {
            textureSelection = false;
            propertiesObject->updateTexture(propertiesObjectTexture);
        }
    }

    if(running) {
        for(auto& object: objects) {
            if(object->stepVM() == DONE) {
                haltedObjects++;
                continue;
            }
            UpdateGoToPos(object, object->goToState, dt, engine);
        }

        if(haltedObjects == objectCount) {
            // run has finished
            running = false;
            std::cout << "Execution finished\n";
        }
    }

    ApplyFakeGravity(playerPuppet, engine, dt);
}

void FreeplayScene::DestroyScene(Engine* engine) {

}

void FreeplayScene::constructGameObjects(Engine* engine) {
    ObjectData puppetData = getObjectData("puppet");

    // create player puppet
    auto puppetTransform = baseTransform;
    puppetTransform.position.z = 10.0f;
    playerPuppet = engine->createGameObject<InteractiveObject>(
        puppetTransform, puppetData.mesh, puppetData.texture, baseMaterial, false
    );
    playerPuppet->name = "Player";
    playerPuppet->type = "puppet";
    playerPuppet->id = getUniqueObjectName();
    sceneEntities["player"] = playerPuppet;
    objects.push_back(playerPuppet);

    // create 5x5 field
    ObjectData blockData = getObjectData("block");
    for(int dy = -2; dy <= 2; dy++) {
        for(int dx = -2; dx <= 2; dx++) {
            int x = dx * 2;
            int y = dy * 2;
            auto objectTrans = baseTransform;
            objectTrans.position = {static_cast<float>(x), static_cast<float>(y), 0.0f};
            GridPos gp = {x, y, 0};
            Tile* newObject = engine->createGameObject<Tile>(
                objectTrans, blockData.mesh, blockData.texture, baseMaterial, false
            );
            auto name = getUniqueObjectName();
            newObject->name = name;
            newObject->id = name;
            newObject->type = "block";
            sceneObjects[name] = newObject;
            occupiedCells[gp] = name;
        }
    }
}

void FreeplayScene::createBrush(const std::string& name, Engine* engine) {
    eraseBrush = name == "erase";

    ObjectData objData;
    if(!eraseBrush) {
        objData = getObjectData(name);
        lastBrushName = name;
    }

    if(brushObject) {
        // TODO: cleanup and recreate
        std::cout << "Brush already exists. Previous one IS NOT DESTROYED\n";
    }

    Mesh* objectMesh = objData.mesh;
    Texture* objectTexture = objData.texture;

    if(eraseBrush) {
        ObjectData block = getObjectData("block");
        objectMesh = block.mesh;
        objectTexture = resourceManager->getTexture("red");
    }
    
    brushObject = engine->createGameObject<GameObject>(
        baseTransform, objectMesh, objectTexture, baseMaterial, false
    );
    brushObject->tag = "Brush";

    if(eraseBrush) {
        brushObject->transform.scale = {0.0f,0.0f,0.0f};
    }

    activeBrush = true;
    brushObjectData = std::move(objData);

    // hide panels
    setToolbarActive(false);
}

std::string FreeplayScene::getUniqueObjectName() {
    return "Object" + std::to_string(objectIndex++);
}

void FreeplayScene::setToolbarActive(bool active) {
    targetToolbarY = active ? 0.0f : -70.0f;
    toolbarTime = 0.0f;
    toolbarY = active ? -70.0f : 0.0f;
    toolbarAnimation = true;
    toolbarActive = active;
}

ObjectData FreeplayScene::getObjectData(std::string objectName) {
    auto it = objectResources.find(objectName);
    if(it != objectResources.end()) {
        Mesh* mesh = resourceManager->getMesh(it->second.mesh);
        // TODO: select texture variations
        Texture* texture = resourceManager->getTexture(it->second.textures[0]);
        return {mesh, texture};
    } else {
        throw std::runtime_error("Unknown object " + objectName);
    }
    return {nullptr, nullptr};
}