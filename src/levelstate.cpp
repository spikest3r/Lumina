#include "levelstate.h"
#include <fstream>
#include <stdexcept>
#include <cstdint>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<Transform>,
    "Transform must stay trivially copyable for raw binary Save/Load to be valid");

static constexpr uint32_t LEVEL_FILE_MAGIC   = 0x4C56454C; // 'LVEL'
static constexpr uint32_t LEVEL_FILE_VERSION = 1;

void LevelState::WriteString(std::ofstream& out, const std::string& s) {
    uint32_t len = static_cast<uint32_t>(s.size());
    out.write(reinterpret_cast<const char*>(&len), sizeof(len));
    if (len > 0) out.write(s.data(), len);
}

void LevelState::ReadString(std::ifstream& in, std::string& s) {
    uint32_t len = 0;
    in.read(reinterpret_cast<char*>(&len), sizeof(len));
    if (!in) throw std::runtime_error("LevelState::Load - truncated file (string length)");

    s.resize(len);
    if (len > 0) {
        in.read(s.data(), len);
        if (!in) throw std::runtime_error("LevelState::Load - truncated file (string data)");
    }
}

void LevelState::Save(const std::string& path) const {
    std::ofstream out(path, std::ios::binary);
    if (!out) throw std::runtime_error("LevelState::Save - failed to open file: " + path);

    LevelStateHeader header{ LEVEL_FILE_MAGIC, LEVEL_FILE_VERSION, objectCounter };
    out.write(reinterpret_cast<const char*>(&header.magic), sizeof(header.magic));
    out.write(reinterpret_cast<const char*>(&header.version), sizeof(header.version));
    out.write(reinterpret_cast<const char*>(&header.objectCounter), sizeof(header.objectCounter));

    uint32_t count = static_cast<uint32_t>(levelObjects.size());
    out.write(reinterpret_cast<const char*>(&count), sizeof(count));

    for (const auto& [key, obj] : levelObjects) {
        WriteString(out, key);            // map key (redundant with obj.id, but kept explicit)
        WriteString(out, obj.name);
        WriteString(out, obj.id);
        WriteString(out, obj.type);
        out.write(reinterpret_cast<const char*>(&obj.transform), sizeof(Transform));
        out.write(reinterpret_cast<const char*>(&obj.isInteractive), sizeof(bool));
        out.write(reinterpret_cast<const char*>(&obj.gravity), sizeof(bool));
        WriteString(out, obj.sourceCode);
        out.write(reinterpret_cast<const char*>(&obj.execType), sizeof(ExecutionType));

        // Serialize blockData
        uint32_t blockDataSize = static_cast<uint32_t>(obj.blockData.size());
        out.write(reinterpret_cast<const char*>(&blockDataSize), sizeof(blockDataSize));
        if (blockDataSize > 0) {
            out.write(reinterpret_cast<const char*>(obj.blockData.data()), blockDataSize);
        }
    }

    if (!out) throw std::runtime_error("LevelState::Save - write failed for: " + path);
}

void LevelState::Load(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("LevelState::Load - failed to open file: " + path);

    LevelStateHeader header{};
    in.read(reinterpret_cast<char*>(&header.magic), sizeof(header.magic));
    in.read(reinterpret_cast<char*>(&header.version), sizeof(header.version));
    in.read(reinterpret_cast<char*>(&header.objectCounter), sizeof(header.objectCounter));
    if (!in) throw std::runtime_error("LevelState::Load - truncated header: " + path);
    if (header.magic != LEVEL_FILE_MAGIC)
        throw std::runtime_error("LevelState::Load - not a valid level file: " + path);
    if (header.version != LEVEL_FILE_VERSION)
        throw std::runtime_error("LevelState::Load - unsupported version (" +
                                  std::to_string(header.version) + "): " + path);

    uint32_t count = 0;
    in.read(reinterpret_cast<char*>(&count), sizeof(count));
    if (!in) throw std::runtime_error("LevelState::Load - truncated file (count)");

    std::unordered_map<std::string, LevelObject> loaded;
    loaded.reserve(count);

    for (uint32_t i = 0; i < count; ++i) {
        std::string key;
        LevelObject obj;

        ReadString(in, key);
        ReadString(in, obj.name);
        ReadString(in, obj.id);
        ReadString(in, obj.type);

        in.read(reinterpret_cast<char*>(&obj.transform), sizeof(Transform));
        if (!in) throw std::runtime_error("LevelState::Load - truncated file (transform)");

        in.read(reinterpret_cast<char*>(&obj.isInteractive), sizeof(bool));
        if (!in) throw std::runtime_error("LevelState::Load - truncated file (isInteractive)");

        in.read(reinterpret_cast<char*>(&obj.gravity), sizeof(bool));
        if (!in) throw std::runtime_error("LevelState::Load - truncated file (gravity)");

        // temporary
        ReadString(in, obj.sourceCode);

        in.read(reinterpret_cast<char*>(&obj.execType), sizeof(ExecutionType));
        if (!in) throw std::runtime_error("LevelState::Load - truncated file (execType)");

        // Deserialize blockData
        uint32_t blockDataSize = 0;
        in.read(reinterpret_cast<char*>(&blockDataSize), sizeof(blockDataSize));
        if (!in) throw std::runtime_error("LevelState::Load - truncated file (blockData length)");

        obj.blockData.resize(blockDataSize);
        if (blockDataSize > 0) {
            in.read(reinterpret_cast<char*>(obj.blockData.data()), blockDataSize);
            if (!in) throw std::runtime_error("LevelState::Load - truncated file (blockData payload)");
        }

        loaded.emplace(std::move(key), std::move(obj));
    }

    levelObjects = std::move(loaded);
    objectCounter = header.objectCounter;
}

void LevelState::AddObject(const std::string& id, LevelObject object) {
    levelObjects[id] = std::move(object);
}

void LevelState::DeleteObject(const std::string& id) {
    levelObjects.erase(id);
}

void LevelState::UpdateObject(const std::string& id, LevelObject newObject) {
    levelObjects[id] = std::move(newObject);
}

LevelObject* LevelState::GetObject(const std::string& id) {
    auto it = levelObjects.find(id);
    if (it != levelObjects.end()) {
        return &it->second;
    }
    throw std::runtime_error("Unknown level object");
}