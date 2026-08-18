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
#include "imgui.h"

#include <typeinfo>

constexpr Vector2 toolbarButtonSize = {110,50};
constexpr Vector2 fileMenuButtonSize = {110,30};
constexpr float grid = 2.0f;
constexpr float DEG2RAD = 3.14159265358979323846f / 180.0f;
constexpr float RAD2DEG = 180.0f / 3.14159265358979323846f;

#define MAX_WORLD_HEIGHT 4

#include "imgui.h"

static GridPos WorldToGrid(const Vector3& pos)
{
    GridPos gp;
    gp.x = static_cast<int>(std::round(pos.x / grid));
    gp.y = static_cast<int>(std::round(pos.y / grid));
    gp.z = static_cast<int>(std::round(pos.z / grid));
    return gp;
}

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
    engPtr = engine;

    engine->SetUICallback([this](Engine* engine)
    {
        this->UICallback(engine);
    });
    engine->setGroundPlaneActive(true);
    //engine->setLightPosition({0.0f,0.0f,3.0f});

    baseMaterial = engine->createPhysicsMaterial(0.5f,0.5f,0.5f);

    sfxHandler = engine->createGameObject<GameObject>(
        baseTransform,
        nullptr,
        nullptr,
        baseMaterial,
        false
    );

    if(loadNew) {
        constructGameObjects(engine);
        chosenFile = false;
    } else {
        std::cout << "Instantiating with existing project file\n";
        levelState.Load(projectFile);
        codeModeSwitch = levelState.codeMode;
        instantiateLevel(engine);
        
        if(projectFile == "temporary.lumina") {
            DeleteFileAsync("temporary.lumina");
            showDialog("Previous session", "Your previous session has been auto-saved.\nDon't forget to save your projects!", nullptr);
            chosenFile = false;
            levelState.SetModified();
        } else {
            chosenFile = true;
        }
    }

    ComputeOrbitCamera(cameraTarget, cameraYaw, cameraPitch, cameraDist, engine->cameraPosition, engine->cameraRotation);

    m_blockEditor.Setup([this](const std::string& name) {
        Sound* snd = resourceManager->getSound(name, false, false);
        sfxHandler->playSound(snd, 1.0f);
    });

    if(!chosenFile) {
        projectFile = "temporary.lumina";
    }

    autosaveTime = autosaveInterval;
}

void FreeplayScene::UICallback(Engine* engine) {
    Vector2 extents = engine->getExtents();

    ToolUI::SetNextWindowPos({0, toolbarY});
    ToolUI::SetNextWindowSize({extents.x, 70});
    ToolUI::Begin("##toolbar", true, false);
    if(ToolUI::Button("File", toolbarButtonSize)) {
        fileMenu = !fileMenu;
    }
    ToolUI::SameLine();
    ToolUI::Separator(VERTICAL);
    ToolUI::SameLine();
    if(ToolUI::Button(running ? "Stop" : "Run", toolbarButtonSize) || extToggleF5) {
        extToggleF5 = false;
        if(running) {
            running = false;
            // clear lingering dialogs after execution stopped
            dialogs.clear();
            inputDialogs.clear();
            // reinstantiate original state
            instantiateLevel(engine);
        } else {
            bool status = true;
            ExportBlockToObject();
            for(auto& object: objects) {
                if(!levelState.codeMode) {
                    // code gen from blocks
                    uint64_t errorBlockId = 0;
                    std::string errorMsg;
                    auto sc = GenerateCode(object->blockData, &errorMsg, &errorBlockId);
                    if(sc) {
                        object->sourceCode = sc.value();
                    } else {
                        m_blockEditor.SetErrorBlock(errorBlockId);
                        showDialog("Error in block", errorMsg, nullptr);
                        status = false;
                        break;
                    }
                }
                std::string message;
                status = object->compileCode(message);
                if(!status) {
                    // TODO: for block mode pinpoint bad block
                    showDialog("Compilation error", message, nullptr);
                    break;
                }
                if(object->execType == ExecutionType::ONCLICK) {
                    object->execLock = true;
                }
                object->resetVM();
            }

            if(status) {
                closePropertiesPanel();
                objectCount = objects.size();
                haltedObjects = 0;
                running = true;
            }
        }
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Brushes", toolbarButtonSize)) {
        if(!brushMenu && running) {
            showDialog("Error", "Not allowed during runtime", nullptr);
        } else {
            brushMenu = !brushMenu;
        }
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Objects", toolbarButtonSize)) {
        if(!addObjectMenu && running) {
            showDialog("Error", "Not allowed during runtime", nullptr);
        } else {
            addObjectMenu = !addObjectMenu;
        }
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Camera", toolbarButtonSize)) {
        cameraMode = true;
        setToolbarActive(false);
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Settings", toolbarButtonSize)) {
        if(running) {
            showDialog("Error", "Not allowed during runtime", nullptr);
        } else {
            settingsPanel = !settingsPanel;
        }
    }
    ToolUI::End();
    
    // code editor
    if(toolbarActive && propertiesObject && propertiesObject->isInteractive) {
        if(levelState.codeMode) {
            InteractiveObject* object = static_cast<InteractiveObject*>(propertiesObject);
            ToolUI::SetNextWindowSize({extents.x / 3, extents.y - 70});
            ToolUI::SetNextWindowPos({0, 70});
            ToolUI::Begin("Code editor", false, false);
            if(ToolUI::InputTextMultiline("##editor", object->sourceCode)) {
                LevelObject* currentObject = levelState.GetObject(object->id);
                currentObject->sourceCode = object->sourceCode;
            }
            ToolUI::End();
        } else {
            m_blockEditor.Render();
        }
    }

    // brush panel
    if(brushMenu && toolbarActive && !running) {
        ToolUI::Begin("Brushes");
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
    
    // object panel
    if(addObjectMenu && toolbarActive && !running) {
        ToolUI::Begin("Objects");
        ToolUI::InputFloat3("Create at", createPos);
        if(ToolUI::Button("Puppet")) {
            // create puppet
            createObject("puppet", createPos, engine);
        }
        ToolUI::End();
    }
    
    // properties panel
    if(propertiesPanelType != CLOSED && toolbarActive && propertiesObject) {
        ToolUI::Begin("Object properties");
        bool close = ToolUI::Button("Close");
        bool deleteObj = false;
        if(propertiesPanelType == INTERACTIVE) {
            ToolUI::SameLine();
            deleteObj = ToolUI::Button("Delete");
        }
        if(close) {
            closePropertiesPanel(); 
        } else if(deleteObj) {
            deleteObject(propertiesObject->id, engine);
        } else {
            bool shouldUpdate = false;
            if(ToolUI::TextField("Name", propertiesObject->name, true)) {
                shouldUpdate = true;
            }
            std::string idText = "Unique ID: " + propertiesObject->id;
            ToolUI::Text(idText.c_str());
            std::string typeText = "Type: " + propertiesObject->type;
            ToolUI::Text(typeText.c_str());
            if(propertiesPanelType == INTERACTIVE) {
                auto interactiveObject = static_cast<InteractiveObject*>(propertiesObject);

                // position
                if(ToolUI::InputFloat3("Position", interactiveObject->transform.position)) {
                    shouldUpdate = true;
                }
                Vector3 rot = QuatToEuler(interactiveObject->transform.rotation);
                rot = { rot.x * RAD2DEG, rot.y * RAD2DEG, rot.z * RAD2DEG };  // show degrees in UI
                if (ToolUI::InputFloat3("Rotation", rot)) {
                    interactiveObject->transform.rotation = EulerToQuat({
                        rot.x * DEG2RAD, rot.y * DEG2RAD, rot.z * DEG2RAD
                    });
                    shouldUpdate = true;
                }

                // gravity
                if(ToolUI::Checkbox("Gravity", &interactiveObject->gravity)) {
                    shouldUpdate = true;
                }

                // execution type
                ToolUI::Text("Execution type");
                int execTypeInt = static_cast<int>(interactiveObject->execType);
                if(ToolUI::RadioButtonInt("Once", &execTypeInt, 0)) shouldUpdate = true;
                if(ToolUI::RadioButtonInt("Repeat", &execTypeInt, 1)) shouldUpdate = true;
                if(ToolUI::RadioButtonInt("On Click", &execTypeInt, 2)) shouldUpdate = true;

                if(shouldUpdate) {
                    LevelObject* currentObject = levelState.GetObject(interactiveObject->id);
                    currentObject->name = interactiveObject->name;
                    currentObject->transform = interactiveObject->transform;
                    currentObject->gravity = interactiveObject->gravity;
                    currentObject->execType = (ExecutionType)execTypeInt;
                    interactiveObject->execType = (ExecutionType)execTypeInt;
                }
            }
        }
        ToolUI::End();
    }

    if(dialogs.size() > 0) {
        std::vector<int> indicesToErase;
        for(int i = 0; i < dialogs.size(); i++) {
            auto& dialog = dialogs[i];
            std::string title = dialog.title + "###dialog_" + std::to_string(i);
            ToolUI::Begin(title.c_str());
            ToolUI::Text(dialog.message.c_str());
            if(ToolUI::Button("OK")) {
                if(dialog.onClose) {
                    dialog.onClose();
                }
                indicesToErase.push_back(i);
            }
            ToolUI::End();
        }
        for(auto& idx: indicesToErase) {
            dialogs[idx] = std::move(dialogs.back());
            dialogs.pop_back();
        }
    }

    if(inputDialogs.size() > 0) {
        std::vector<int> indicesToErase;
        for(int i = 0; i < inputDialogs.size(); i++) {
            auto& dialog = inputDialogs[i];
            std::string title = dialog.title + "###inputDialog_" + std::to_string(i);
            ToolUI::Begin(title.c_str());
            ToolUI::Text(dialog.message.c_str());
            std::string id = "##diag_" + dialog.title;
            ToolUI::TextField(id.c_str(), dialog.input, false);
            if(ToolUI::Button("OK")) {
                if(dialog.onSubmit) {
                    dialog.onSubmit(dialog.input);
                }
                indicesToErase.push_back(i);
            }
            ToolUI::End();
        }
        for(auto& idx: indicesToErase) {
            inputDialogs[idx] = std::move(inputDialogs.back());
            inputDialogs.pop_back();
        }
    }

    if(settingsPanel && toolbarActive && !running) {
        ToolUI::Begin("Settings");
        ToolUI::Text("Coding environment");
        if(ToolUI::RadioButtonInt("Blocks", &codeModeSwitch, 0)) {
            codeModeSwitch = 1;
            convertAlert = true;
        }
        if(ToolUI::RadioButtonInt("Code", &codeModeSwitch, 1)) {
            codeModeSwitch = 0;
            convertAlert = true;
        }
        ToolUI::End();

        if(convertAlert) {
            ToolUI::Begin("Warning!");
            switch(codeModeSwitch) {
                case 0: {
                    // blocks to code
                    ToolUI::Text("Warning: Converting blocks to code is an irreversible action and cannot be undone. Are you sure you want to proceed?");
                    break;
                }
                case 1: {
                    // code to blocks
                    ToolUI::Text("Warning: Switching to blocks will completely wipe all source code, and your code will not be transferred to the blocks. Do you want to continue?");
                    break;
                }
                default: {
                    // undefined behavior
                    ToolUI::Text("Undefined state");
                    break;
                }
            }
            if(ToolUI::Button("Yes", toolbarButtonSize)) {
                convertAlert = false;

                // proceed with conversion
                switch(codeModeSwitch) {
                    case 0: {
                        // blocks to code
                        closePropertiesPanel(); // ensure clear state
                        // non-destructive compilation first to ensure no errors
                        std::unordered_map<std::string, std::string> compiledSource;
                        bool success = true;
                        for(auto& object: objects) {
                            // code gen from blocks
                            uint64_t errorBlockId = 0;
                            std::string errorMsg;
                            auto sc = GenerateCode(object->blockData, &errorMsg, &errorBlockId);
                            if(sc) {
                                compiledSource[object->id] = std::move(*sc);
                            } else {
                                success = false;
                                openPropertiesPanel(object); // open block editor
                                m_blockEditor.SetErrorBlock(errorBlockId);
                                showDialog("Error in block", errorMsg + "\nConversion has been aborted!", nullptr);
                                break;
                            }
                        }
                        if(success) {
                            for(auto& object: objects) {
                                // now apply source code to all objects
                                object->blockData.clear();
                                object->sourceCode = compiledSource[object->id];
                                // and update template
                                auto obj = levelState.GetObject(object->id);
                                obj->blockData.clear();
                                obj->sourceCode = compiledSource[object->id];
                            }
                            // finally, set levelState flag
                            levelState.codeMode = true;
                            codeModeSwitch = 1;
                        }
                        break;
                    }
                    case 1: {
                        // code to blocks
                        closePropertiesPanel(); // ensure clear state
                        // wipe all source code
                        for(auto& object: objects) {
                            object->sourceCode.clear();
                            levelState.GetObject(object->id)->sourceCode.clear();
                        }
                        // finally, set levelState flag
                        levelState.codeMode = false;
                        codeModeSwitch = 0;
                        break;
                    }
                    default: {
                        // undefined behavior, do nothing
                        break;
                    }
                }
            }
            if(ToolUI::Button("No", toolbarButtonSize)) {
                // abort
                convertAlert = false;
            }
            ToolUI::End();
        }
    }

    if(fileMenu && toolbarActive && !running) {
        ToolUI::SetNextWindowPos({0, 70});
        ToolUI::SetNextWindowSize({fileMenuButtonSize.x + 20, fileMenuButtonSize.y * 5 + 50});
        ToolUI::Begin("##filebar", true, false);
        if(ToolUI::Button("New", fileMenuButtonSize)) {
            intent = LoadIntent::NEW;
        }
        ToolUI::Separator(HORIZONTAL);
        if(ToolUI::Button("Save", fileMenuButtonSize)) {
            if(chosenFile) SaveProject();
            else {
                m_fileManager.Init(FileManager::Mode::SAVE, "", {".lumina"});
                intent = LoadIntent::SAVEAS;
            }
        }
        if(ToolUI::Button("Save as", fileMenuButtonSize)) {
            m_fileManager.Init(FileManager::Mode::SAVE, "", {".lumina"});
            intent = LoadIntent::SAVEAS;
        }
        if(ToolUI::Button("Load", fileMenuButtonSize)) {
            intent = LoadIntent::LOAD;
        }
        ToolUI::Separator(HORIZONTAL);
        if(ToolUI::Button("Exit", fileMenuButtonSize)) {
            intent = LoadIntent::EXIT;
        }
        ToolUI::End();
    }

    if(intent != LoadIntent::NO_INTENT) {
        if(levelState.IsModified() && intent != LoadIntent::SAVEAS) {
            ToolUI::Begin("Warning!");
            ToolUI::Text("You have unsaved changes! Do you want to save them before proceeding?");
            bool handle = false;
            if(ToolUI::Button("Yes")) {
                SaveProject(); // sets modified to false
                handle = true;
            }
            ToolUI::SameLine();
            if(ToolUI::Button("No")) {
                levelState.ResetModified();
                handle = true;
            }
            ToolUI::SameLine();
            if(ToolUI::Button("Cancel")) {
                intent = LoadIntent::NO_INTENT;
            }
            ToolUI::End();
            if(handle) {
                switch(intent) {
                    case LoadIntent::LOAD:
                    {
                        m_fileManager.Init(FileManager::Mode::OPEN, "", {".lumina"});
                        break;
                    }
                }
            }
        } else {
            switch(intent) {
                case LoadIntent::LOAD:
                    {
                        if (m_fileManager.HasResult()) {
                            closePropertiesPanel();
                            projectFile = m_fileManager.GetResult();
                            levelState.Load(projectFile);
                            codeModeSwitch = levelState.codeMode;
                            instantiateLevel(engine);
                            chosenFile = true;
                            intent = LoadIntent::NO_INTENT;
                        } else if (!m_fileManager.IsOpen() && !m_fileManager.HasResult()) {
                            intent = LoadIntent::NO_INTENT;
                        }
                        break;
                    }
                case LoadIntent::NEW:
                    {
                        closePropertiesPanel();
                        levelState.Clear();
                        codeModeSwitch = levelState.codeMode;
                        constructGameObjects(engine);
                        intent = LoadIntent::NO_INTENT;
                        chosenFile = false;
                        break;
                    }
                case LoadIntent::SAVEAS:
                    {
                        if (m_fileManager.HasResult()) {
                            projectFile = m_fileManager.GetResult();
                            m_fileManager.ClearResult();
                            intent = LoadIntent::NO_INTENT;
                            SaveProject();
                            chosenFile = true;
                        } else if (!m_fileManager.IsOpen() && !m_fileManager.HasResult()) {
                            intent = LoadIntent::NO_INTENT;
                        }
                        break;
                    }
                case LoadIntent::EXIT:
                    {
                        unloadFreeplay();
                        dropFile = true;
                        break;
                    }
            }
        }
    }

    m_fileManager.Render();
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

    if(!eraseBrush && !running) {
        if(hotkeyToggle && engine->getKey(KeyCode::E) == PRESS) {
            if(!activeBrush) {
                createBrush("erase", engine);
            } else {
                eraseBrush = true;
                brushObject->transform.scale = {0.0f,0.0f,0.0f};
            }
        }
    }

    if(!activeBrush && !lastBrushName.empty() && !running) {
        if(hotkeyToggle && engine->getKey(KeyCode::Q) == PRESS) {
            createBrush(lastBrushName, engine);
        }
    }

    static bool shiftCam = false;
    static Vector3 prevBrushScale = {0.0f,0.0f,0.0f};
    if(!cameraMode) {
        if(hotkeyToggle && engine->getKey(KeyCode::C) == PRESS && !activeBrush) {
            cameraMode = true;
            setToolbarActive(false);
        } else if(hotkeyToggle && engine->getKey(KeyCode::LeftShift) == PRESS) {
            cameraMode = true;
            shiftCam = true;
            setToolbarActive(false);
            if(activeBrush) {
                prevBrushScale = brushObject->transform.scale;
                brushObject->transform.scale = {0.0f,0.0f,0.0f};
            }
        }
    } else if(shiftCam) {
        if(engine->getKey(KeyCode::LeftShift) == RELEASE) {
            cameraMode = false;
            shiftCam = false;
            if(!activeBrush) setToolbarActive(true);
            else {
                brushObject->transform.scale = prevBrushScale;
            }
        }
    }

    if(activeBrush && !cameraMode) {
        Vector3 origin;
        Vector3 direction;

        engine->getMouseRay(origin, direction);

        if (std::abs(direction.z) > 0.0001f)
        {
            float t = -origin.z / direction.z;

            if (t >= 0.0f)
            {
                Vector3 hit = origin + direction * t;

                int scrollDelta = std::round(engine->getScrollDelta());

                hit.x = std::round(hit.x / grid) * grid;
                hit.y = std::round(hit.y / grid) * grid;

                GridPos gp = WorldToGrid(hit);
                static Quaternion rotation;

                if(hotkeyToggle) {
                    if(scrollDelta > 0) // scroll up
                    {
                        rot += 90.0f;
                    } else if(scrollDelta < 0) {
                        rot -= 90.0f;
                    }

                    if(scrollDelta != 0) {
                        rotation = EulerToQuat({0.0f, 0.0f, rot * DEG2RAD});
                        brushObject->transform.rotation = rotation;
                    }
                } else {
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
                }

                hit.z = static_cast<float>(brushZ) * grid + 1;
                gp.z = brushZ + 1;

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
                        auto objectTrans = baseTransform;
                        objectTrans.position = hit;
                        objectTrans.rotation = rotation;
                        auto name = getUniqueObjectName();

                        LevelObject newObject = {name, name, lastBrushName, objectTrans, false, false};
                        levelState.AddObject(name, newObject);
                        instantiateObject(engine, newObject);

                        Sound* brushPlaceSound = resourceManager->getSound("place", false, true);
                        brushObject->playSound(brushPlaceSound, 1.0f);
                    } else if(occupied && deleteBtnPress) {
                        auto id = occupiedCells[gp];
                        auto object = sceneObjects[id];

                        // if we delete opened object, deactivate panel
                        if(propertiesObject == object) {
                            closePropertiesPanel();
                        }
                        
                        engine->requestDestroyGameObject(object);
                        sceneObjects.erase(id);
                        occupiedCells.erase(gp);

                        levelState.DeleteObject(id);

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

        if(hotkeyToggle && leftPress && !isLeftHeld) {
            isLeftHeld = true;

            Vector3 origin;
            Vector3 direction;
            engine->getMouseRay(origin, direction);
            RaycastHit hit = engine->raycast(origin, direction, 100.0f);
            if(hit.object) {
                auto object = dynamic_cast<Tile*>(hit.object);
                if(object) {
                    openPropertiesPanel(object);
                }
            }
        } else if(!leftPress && isLeftHeld) {
            isLeftHeld = false;
        }

        if(escOnFrame && propertiesPanelType != CLOSED) {
            closePropertiesPanel();
        }
    } else if(running) {
        static bool isLeftHeld;
        bool leftPress = engine->getMouseButton(MouseButton::Left) == PRESS;

        if(leftPress && !isLeftHeld) {
            isLeftHeld = true;

            Vector3 origin;
            Vector3 direction;
            engine->getMouseRay(origin, direction);
            RaycastHit hit = engine->raycast(origin, direction, 100.0f);
            if(hit.object) {
                InteractiveObject* object = dynamic_cast<InteractiveObject*>(hit.object);
                if(object) {
                    if(object->execLock && object->execType == ExecutionType::ONCLICK) {
                        object->execLock = false;
                        object->resetVM();
                    }
                }
            }
        } else if(!leftPress && isLeftHeld) {
            isLeftHeld = false;
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

    static bool f5Held = false;
    auto f5State = engine->getKey(KeyCode::F5);
    if(f5State == PRESS && !f5Held) {
        f5Held = true;
        extToggleF5 = !extToggleF5;
    } else if(f5State == RELEASE && f5Held) {
        f5Held = false;
    }

    if (running) {
        constexpr double VM_INSTRUCTIONS_PER_SECOND = 5280.0;

        vmAccumulator += dt * VM_INSTRUCTIONS_PER_SECOND;

        int instructionsToRun = static_cast<int>(vmAccumulator);
        vmAccumulator -= instructionsToRun;

        for (auto& object : objects) {
            bool c = false;

            int instructions = instructionsToRun;

            for (int ipf = 0; ipf < instructions; ipf++) {
                if (!object->execLock) {
                    if (object->stepVM() == DONE) {
                        switch (object->execType) {
                            case ExecutionType::ONCE:
                                haltedObjects++;
                                c = true;
                                break;

                            case ExecutionType::ONCLICK:
                                object->execLock = true;
                                c = true;
                                break;

                            case ExecutionType::REPEAT:
                                object->resetVM();
                                c = true;
                                break;
                        }
                    }
                }

                if (c)
                    break;
            }

            if (c)
                continue;

            UpdateGoToPos(object, object->goToState, dt, engine);
            UpdateWaitUntilGround(object, object->waitGroundState, engine, 0.5f);
            UpdateWait(object->waitState, engine);

            if (object->gravity) {
                ApplyFakeGravity(object, engine, dt);
            }
        }

        if (haltedObjects == objectCount) {
            // run has finished
            // running = false;
            haltedObjects = -1;
            vmAccumulator = 0.0;
            std::cout << "Execution finished\n";
        }
    }

    m_blockEditor.Update();
    m_fileManager.Update();

    // autosave every minute
    // TODO: configurable
    autosaveTime -= dt;
    if(autosaveTime <= 0.0f) {
        autosaveTime = autosaveInterval;
        SaveProject();
    }
}

void FreeplayScene::DestroyScene(Engine* engine) {
    if(chosenFile) {
        std::ofstream("last") << projectFile;
    } else if(!dropFile) {
        SaveProject();
    }
    engine->SetUICallback(nullptr);
}

void FreeplayScene::constructGameObjects(Engine* engine) {
    ObjectData puppetData = getObjectData("puppet");

    // create player puppet
    auto puppetTransform = baseTransform;
    puppetTransform.position.z = 3.0f;
    auto puppetID = getUniqueObjectName();
    LevelObject puppetLO = {"Player", puppetID, "puppet", puppetTransform, true, true};
    levelState.AddObject(puppetID, puppetLO);

    // create 5x5 field
    ObjectData blockData = getObjectData("block");
    for(int dy = -2; dy <= 2; dy++) {
        for(int dx = -2; dx <= 2; dx++) {
            int x = dx * 2;
            int y = dy * 2;
            auto objectTrans = baseTransform;
            objectTrans.position = {static_cast<float>(x), static_cast<float>(y), 1.0f};
            auto name = getUniqueObjectName();
            LevelObject blockLO = {name, name, "block", objectTrans, false, false};
            levelState.AddObject(name, blockLO);
        }
    }

    // instantiate level
    instantiateLevel(engine);
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
    
    auto brushTransform = baseTransform;
    Quaternion rotation = EulerToQuat({0.0f, 0.0f, rot * DEG2RAD});
    brushTransform.rotation = rotation;
    brushObject = engine->createGameObject<GameObject>(
        brushTransform, objectMesh, objectTexture, baseMaterial, false
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

void FreeplayScene::createObject(const std::string& name, const Vector3& pos, Engine* engine) {
    // prepare transform
    auto objectTrans = baseTransform;
    objectTrans.position = createPos;
    // prepare name
    auto objectID = getUniqueObjectName();
    std::string objName = name + "_" + objectID;
    // create and add level object
    LevelObject levelObject = {objName, objectID, name, objectTrans, true, true};
    levelState.AddObject(objectID, levelObject);
    // instantiate object
    InteractiveObject* newObject = static_cast<InteractiveObject*>(instantiateObject(engine, levelObject));
    // active properties panel for new object
    openPropertiesPanel(newObject);
}

void FreeplayScene::deleteObject(const std::string& id, Engine* engine) {
    auto object = sceneEntities[id];
    if(propertiesObject == object) {
        closePropertiesPanel();
    }
    engine->requestDestroyGameObject(object);
    sceneEntities.erase(id);
    auto it = std::find(objects.begin(), objects.end(), object);
    if (it != objects.end()) {
        std::swap(*it, objects.back());
        objects.pop_back();
    }
    levelState.DeleteObject(id);
}

std::string FreeplayScene::getUniqueObjectName() {
    return "Object" + std::to_string(levelState.objectCounter++);
}

void FreeplayScene::setToolbarActive(bool active) {
    if(toolbarActive == active) return;
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

Tile* FreeplayScene::instantiateObject(Engine* engine, const LevelObject& object) {
    auto interactive = object.isInteractive;
    auto objectData = getObjectData(object.type);
    if(interactive) {
        // InteractiveObject
        // instantiate game object
        InteractiveObject* instance = engine->createGameObject<InteractiveObject>(
            object.transform,
            objectData.mesh,
            objectData.texture,
            baseMaterial,
            false
        );
        // fill out info
        instance->name = object.name;
        instance->type = object.type;
        instance->id = object.id;
        instance->gravity = object.gravity;
        instance->isInteractive = true;
        instance->sourceCode = object.sourceCode;
        instance->blockData = object.blockData;
        instance->execType = object.execType;
        instance->scene = this;
        // push to level
        sceneEntities[object.id] = instance;
        objects.push_back(instance);
        return instance;
    } else {
        // Tile
        // instantiate game object
        Tile* instance = engine->createGameObject<Tile>(
            object.transform,
            objectData.mesh,
            objectData.texture,
            baseMaterial,
            false
        );
        // fill out info
        instance->name = object.name;
        instance->id = object.id;
        instance->type = object.type;
        instance->isInteractive = false;
        // fill out GridPos
        GridPos gp = WorldToGrid(object.transform.position);
        // push to level
        sceneObjects[object.id] = instance;
        occupiedCells[gp] = object.name;
        return instance;
    }
}

void FreeplayScene::instantiateLevel(Engine* engine) {
    // destroy old state
    // check entities
    if(!sceneEntities.empty()) {
        for (auto& object : objects) {
            engine->requestDestroyGameObject(object);
        }
    }
    objects.clear(); // clear interactive object vector
    sceneEntities.clear();
    
    // check tiles
    if(!sceneObjects.empty()) {
        for (auto& [id, object] : sceneObjects) {
            engine->requestDestroyGameObject(object);
        }
    }
    sceneObjects.clear();
    occupiedCells.clear(); // clear occupied cell map

    // now reinstantiate the level objects from save state
    const auto& state = levelState.GetLevelObjects();
    for(auto& [id, object] : state) {
        // check type
        instantiateObject(engine, object);
    }
}

void FreeplayScene::showDialog(const std::string& title, const std::string& message, std::function<void()> onClose) {
    dialogs.push_back(
        {title, message, std::move(onClose)}
    );
}

void FreeplayScene::showInputDialog(const std::string& title, const std::string& message, std::function<void(std::string)> onSubmit) {
    inputDialogs.push_back(
        {title, message, "", std::move(onSubmit)}
    );
}

void FreeplayScene::runtimeDestroyInteractive(InteractiveObject* object) {
    // erase from objects vector
    auto it = std::find(objects.begin(), objects.end(), object);
    if (it != objects.end()) {
        std::swap(*it, objects.back());
        objects.pop_back();
    }

    // erase from map
    sceneEntities.erase(object->id);

    // request destroy
    engPtr->requestDestroyGameObject(object);
}

void FreeplayScene::stopExecution() {
    if(!running) return;
    extToggleF5 = true;
}

void FreeplayScene::closePropertiesPanel() {
    if(propertiesPanelType == CLOSED || !propertiesObject) return;

    propertiesObject->updateTexture(propertiesObjectTexture);

    if(propertiesPanelType == INTERACTIVE && !levelState.codeMode) {
        m_blockEditor.ClearErrors();

        ExportBlockToObject();
    }

    propertiesPanelType = CLOSED;
    propertiesObject = nullptr;
    propertiesObjectTexture = nullptr;
    textureSelection = false;
}

void FreeplayScene::openPropertiesPanel(Tile* object) {
    if(propertiesObject != nullptr && propertiesObjectTexture != nullptr) 
    {
        closePropertiesPanel();
    }

    auto data = getObjectData(object->type);
    propertiesObjectTexture = data.texture;

    propertiesObject = object;

    PropertiesPanelType type = TILE;
    if(InteractiveObject* io = dynamic_cast<InteractiveObject*>(object)) {
        type = INTERACTIVE;

        if(!levelState.codeMode) {
            m_blockEditor.Init();
            m_blockEditor.ImportBlocks(io->blockData);
        }
    }

    propertiesPanelType = type;
}

void FreeplayScene::openPropertiesPanel(InteractiveObject* object) {
    if(propertiesObject != nullptr && propertiesObjectTexture != nullptr) 
    {
        closePropertiesPanel();
    }

    auto data = getObjectData(object->type);
    propertiesObjectTexture = data.texture;

    propertiesObject = object;

    propertiesPanelType = INTERACTIVE;
    if(!levelState.codeMode) {
        m_blockEditor.Init();
        InteractiveObject* io = static_cast<InteractiveObject*>(object);
        m_blockEditor.ImportBlocks(io->blockData);
    }
}

void FreeplayScene::ExportBlockToObject() {
    if(propertiesPanelType == CLOSED) return;

    // save to scene object
    InteractiveObject* io = static_cast<InteractiveObject*>(propertiesObject);
    io->blockData = std::move(m_blockEditor.ExportBlocks());

    // save to template
    LevelObject* obj = levelState.GetObject(io->id);
    obj->blockData = io->blockData;
}

void FreeplayScene::SaveProject() {
    if(propertiesPanelType == INTERACTIVE && !levelState.codeMode) {
        // save to scene object
        ExportBlockToObject();
    }

    levelState.Save(projectFile);
}

void FreeplayScene::setProjectFile(const std::string& file) {
    loadNew = false;
    projectFile = file;
}