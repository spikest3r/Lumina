#pragma once
#include <string>
#include <unordered_map>
#include "engine_types.h"

struct LevelObject {
    std::string name;
    std::string id;
    std::string type;
    Transform transform;
    bool isInteractive = false; // TODO

    // TODO: Optimize away fields before for non tiles
    bool gravity = false;
    // TODO: Make this switchable between blocks and code
    std::string sourceCode;
};

class LevelState {
public:
    void Save(const std::string& path) const;
    void Load(const std::string& path);

    void AddObject(const std::string& id, const LevelObject& object); // transfer ownership as well
    void DeleteObject(const std::string& id);
    void UpdateObject(const std::string& id, const LevelObject& newObject); // transfers ownership as well
    LevelObject GetObject(const std::string& id);

    std::unordered_map<std::string, LevelObject>& GetLevelObjects() { return levelObjects; }
    const std::unordered_map<std::string, LevelObject>& GetLevelObjects() const { return levelObjects; }

private:
    std::unordered_map<std::string, LevelObject> levelObjects;

    static void WriteString(std::ofstream& out, const std::string& s);
    static void ReadString(std::ifstream& in, std::string& s);
};