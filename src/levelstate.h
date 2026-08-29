#pragma once
#include <string>
#include <fstream>
#include <cstdint>
#include <unordered_map>
#include "engine_types.h"

enum class ExecutionType : uint8_t {
    ONCE,
    REPEAT,
    ONCLICK
};

struct LevelObject {
    std::string name;
    std::string id;
    int texture;
    std::string type;
    Transform transform;
    bool isInteractive = false; // TODO

    // TODO: Optimize away fields for non-tiles
    bool gravity = false;
    // TODO: Make this switchable between blocks and code
    std::string sourceCode;
    std::vector<uint8_t> blockData;

    ExecutionType execType = ExecutionType::ONCE;
};

struct LevelStateHeader {
    uint32_t magic;
    uint32_t version;
    int objectCounter;
    bool codeMode = false;
    // v2
    bool cameraFollow = false;
    std::string followObject = "Object0";
    bool rotationAllowed = true;
    Vector3 defaultRotation = {-25.0f, 135.0f, 0.0f};
};

class LevelState {
public:
    int objectCounter = 0;
    bool codeMode = false;
    // v2
    bool cameraFollow = false;
    std::string followObject = "Object0"; // for camera follow, unique id only, default is player (its unique id is always Object0)
    bool rotationAllowed = true;
    Vector3 defaultRotation = {-25.0f, 135.0f, 0.0f};

    void Save(const std::string& path);
    void Load(const std::string& path);
    void Clear();

    void AddObject(const std::string& id, LevelObject object);   // moved into map
    void DeleteObject(const std::string& id);
    void UpdateObject(const std::string& id, LevelObject newObject); // moved into map
    LevelObject* GetObject(const std::string& id);

    std::unordered_map<std::string, LevelObject>& GetLevelObjects() { return levelObjects; }
    const std::unordered_map<std::string, LevelObject>& GetLevelObjects() const { return levelObjects; }

    bool IsModified();
    void ResetModified();
    void SetModified();

private:
    std::unordered_map<std::string, LevelObject> levelObjects;

    static void WriteString(std::ofstream& out, const std::string& s);
    static void ReadString(std::ifstream& in, std::string& s);
    
    bool isModified = false;
};