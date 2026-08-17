#pragma once

#include <engine.h>
#include <scene.h>
#include <unordered_map>
#include <unordered_set>

#include "objects.h"
#include "resman.h"
#include "levelstate.h"
#include "blockeditor/blockeditor.h"

enum PropertiesPanelType {
    CLOSED,
    TILE,
    INTERACTIVE
};

struct Dialog {
    std::string title;
    std::string message;
    std::function<void()> onClose;
};

struct InputDialog {
    std::string title;
    std::string message;
    std::string input;
    std::function<void(std::string)> onSubmit;
};

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
public:
    void showDialog(const std::string& title, const std::string& message, std::function<void()> onClose);
    void showInputDialog(const std::string& title, const std::string& message, std::function<void(std::string)> onSubmit);
    void runtimeDestroyInteractive(InteractiveObject* object);
    void stopExecution();
protected:
    ResourceManager* resourceManager;
    LevelState levelState;
    BlockEditor m_blockEditor;

    GameObject* sfxHandler;

    void instantiateLevel(Engine* engine);
    Tile* instantiateObject(Engine* engine, const LevelObject& object);
    
    void InitScene(Engine* engine) override;
    void UpdateScene(Engine* engine) override;
    void DestroyScene(Engine* engine) override;

    Vector3 cameraTarget = {0.0f,0.0f,0.0f};
    float cameraPitch = -25.0f;
    float cameraYaw = 135.0f;
    float cameraDist = 25.0f;

    // TODO: expose sensitivity in settings
    float sensitivity = 0.15f;
    float zoomSensitivity = 0.4f;

    bool extToggleF5 = false;
    bool hotkeyToggle = false;
    bool cameraMode = false;
    bool toolbarActive = true;
    PropertiesPanelType propertiesPanelType = CLOSED;
    Tile* propertiesObject = nullptr;
    Texture* propertiesObjectTexture = nullptr;
    bool textureSelection = false;
    void closePropertiesPanel();
    void openPropertiesPanel(Tile* object);
    void openPropertiesPanel(InteractiveObject* object);
    
    virtual void UICallback(Engine* engine);

    void constructGameObjects(Engine* engine);

    ObjectData getObjectData(std::string objectName);

    PhysicsMaterial* baseMaterial;

    std::unordered_map<std::string, Tile*> sceneObjects; // tiles
    std::unordered_map<std::string, InteractiveObject*> sceneEntities; // objects with code
    
    std::vector<InteractiveObject*> objects; // vector of objects with code
    std::unordered_map<GridPos, std::string, GridPosHash> occupiedCells; // occupied cells by tiles

    InteractiveObject* playerPuppet;

    std::string sourceCode; // for editor and blocks intermediate
    bool running = false;
    
    bool brushMenu = false;
    bool addObjectMenu = false;
    bool activeBrush = false;
    bool eraseBrush = false;
    GameObject* brushObject;
    ObjectData brushObjectData;
    std::string lastBrushName = "";
    Texture* redTexture; // for illegal placements
    bool brushUsesRedTexture = false;
    int brushZ = 0;
    float rot = 0.0f;
    GridPos prevGP;
    Vector3 createPos;

    std::string getUniqueObjectName();

    float toolbarY = 0.0f;
    void setToolbarActive(bool active); // with anim;
    float targetToolbarY = 0.0f;
    float toolbarTime = 0.0f;
    bool toolbarAnimation = false;

    bool settingsPanel = false;

    void createBrush(const std::string& name, Engine* engine);
    void createObject(const std::string& name, const Vector3& pos, Engine* engine);
    void deleteObject(const std::string& id, Engine* engine);

    // toggle between blocks and code in settings
    // real value in LevelState
    int codeModeSwitch = 0;

    int haltedObjects = 0;
    int objectCount = 0;

    std::vector<Dialog> dialogs;
    std::vector<InputDialog> inputDialogs;
    int dialogCount = 0; // cached dialog count
    int inputDialogCount = 0; // cached count

    Engine* engPtr;

    void ExportBlockToObject();
};