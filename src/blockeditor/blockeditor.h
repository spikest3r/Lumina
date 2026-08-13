#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include "imgui.h"
#include "helpers.h"

enum class BlockType {
    HeadBlock,
    MoveForward,
    WaitUntilGround,
    Variable,
};

inline bool IsSlotOnlyBlockType(BlockType type) {
    return type == BlockType::Variable;
}

struct SlotTemplate {
    std::string name;
    std::string defaultText;
};

struct Block;

struct Slot {
    std::string name;
    std::string text;
    Block* plugged = nullptr;
};

struct Block {
    uint64_t id = 0;
    BlockType type;
    std::string label = "Block";

    ImVec2 pos{0, 0};
    ImVec2 size{200, 50};

    Block* next = nullptr;
    Block* prev = nullptr;

    std::vector<Slot> slots;

    Block* slotParent = nullptr;
    int slotParentIndex = -1;

    bool IsHead() const { return type == BlockType::HeadBlock; }
    bool IsSlotOnly() const { return IsSlotOnlyBlockType(type); }
    bool IsPluggedIn() const { return slotParent != nullptr; }
};

struct BlockTemplate {
    BlockType type;
    std::string label;
    ImU32 color;
    std::vector<SlotTemplate> slots;
};

struct BlockInfo {
    struct Field {
        std::string name;
        std::string text;
        std::unique_ptr<BlockInfo> plugged;
    };

    uint64_t id = 0;
    BlockType type;
    std::string label;
    std::vector<Field> fields;
};

class BlockEditor {
public:
    void InitPalette();
    void Init();
    void Update();
    void Render();

    std::vector<uint8_t> ExportBlocks() const;
    bool ImportBlocks(const std::vector<uint8_t>& data);

    static bool WalkBlockBlob(const std::vector<uint8_t>& data,
                               const std::function<void(BlockInfo)>& visit);

    void Cleanup();

private:
    Block* SpawnBlock(BlockType type, const std::string& label, ImVec2 pos);
    void LayoutBlock(Block* block);

    void DrawSidebar();
    void DrawCanvas();
    void DrawBlock(ImDrawList* dl, ImVec2 canvasOrigin, Block* block, bool isDragGhost);
    void DrawSlot(ImDrawList* dl, ImVec2 canvasOrigin, Block* owner, int slotIndex);

    void UpdateDragFromPalette();
    void UpdateDragExistingBlock();
    void UpdateSnapping();
    void UpdateSlotTargeting();
    void UpdateDeletion();

    void DetachFromChain(Block* block);
    void InsertAfter(Block* anchor, Block* block);
    void DeleteBlock(Block* block);
    void DeleteChain(Block* chainStart);

    void PlugIntoSlot(Block* owner, int slotIndex, Block* block);
    void UnplugFromSlot(Block* block);
    bool IsDescendantViaSlots(Block* root, Block* candidate) const;

    ImVec2 EffectivePos(const Block* block) const;
    ImVec2 SlotRowOffset(const Block* owner, int slotIndex) const;
    ImVec2 GetChainSlotScreenPos(ImVec2 canvasOrigin, Block* anchor) const;
    Block* ChainRoot(Block* block) const;
    bool IsHeadOrDescendantOfHead(Block* block) const;
    bool IsInChainStartingAt(Block* chainStart, Block* candidate) const;

    uint64_t NextId() { return m_nextId++; }

private:
    std::vector<std::unique_ptr<Block>> m_blocks;
    std::vector<BlockTemplate> m_palette;

    Block* m_headBlock = nullptr;

    ImVec2 m_canvasOffset{0, 0};
    float m_canvasZoom = 1.0f;
    ImVec2 m_canvasScreenOrigin{0, 0};

    bool m_draggingFromPalette = false;
    int  m_paletteDragIndex = -1;

    Block* m_draggingBlock = nullptr;
    ImVec2 m_dragGrabOffset{0, 0};
    bool m_dragJustStarted = false;
    bool m_dragWasPluggedIn = false;

    Block* m_snapTarget = nullptr;
    Block* m_slotTargetOwner = nullptr;
    int    m_slotTargetIndex = -1;
    Block* m_hoveredBlock = nullptr;

    uint64_t m_nextId = 1;

    static constexpr float kSidebarWidth = 160.0f;
    static constexpr float kSnapDistance = 24.0f;
    static constexpr float kSlotRowHeight = 28.0f;
    static constexpr float kSlotFieldWidth = 90.0f;
    static constexpr float kBlockHeaderHeight = 30.0f;
};

// codegen part

std::string GenerateCode(const std::vector<uint8_t>& blocks);