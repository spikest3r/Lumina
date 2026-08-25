#pragma once

#include <string>
#include <vector>
#include <memory>
#include <cstdint>
#include <functional>
#include <optional>
#include "imgui.h"
#include "helpers.h"

enum class BlockType {
    HeadBlock,
    MoveForward,
    WaitUntilGround,
    SetVariable,
    SayText,
    Ask,
    RandomRange,
    Wait,
    DestroySelf,
    StopAll,
    IsTouching,
    IsKeyDown,
    IsObstacleAhead,
    GoToPos,
    SetRot,
    Variable,
    If,
    IfElse,
    Forever,
    Repeat,
    While,
    MathAdd,
    MathSub,
    MathMul,
    MathDiv,
    LogicLess,
    LogicGreater,
    LogicEqual,
    LogicNotEqual,
    LogicLessEqual,
    LogicGreaterEqual,
    LogicNot,
    Concat,
    HeadRoutine,
    ExecuteRoutine,
    EndRoutine
};

inline bool IsSlotOnlyBlockType(BlockType type) {
    return type == BlockType::Variable ||
           type == BlockType::MathAdd ||
           type == BlockType::MathSub ||
           type == BlockType::MathMul ||
           type == BlockType::MathDiv ||
           type == BlockType::LogicLess ||
           type == BlockType::LogicGreater ||
           type == BlockType::LogicEqual ||
           type == BlockType::LogicNotEqual ||
           type == BlockType::LogicLessEqual ||
           type == BlockType::LogicGreaterEqual ||
           type == BlockType::LogicNot ||
           type == BlockType::IsKeyDown ||
           type == BlockType::IsTouching ||
           type == BlockType::IsObstacleAhead ||
           type == BlockType::Ask ||
           type == BlockType::RandomRange ||
           type == BlockType::Concat;
}

enum class SlotType {
    Any,            
    Number,         
    Logic,          
    Text,           
    TextOrNumber    
};

struct SlotTemplate {
    std::string name;
    std::string defaultText;
    SlotType allowedType = SlotType::Any;
};

struct Block;

struct Slot {
    std::string name;
    std::string text;
    SlotType allowedType = SlotType::Any;
    Block* plugged = nullptr;
};

struct Block {
    uint64_t id = 0;
    BlockType type;
    std::string label = "Block";
    bool hasError = false;

    ImVec2 pos{0, 0};
    ImVec2 size{220, 50};

    Block* next = nullptr;
    Block* prev = nullptr;

    std::vector<Slot> slots;

    Block* slotParent = nullptr;
    int slotParentIndex = -1;

    std::vector<Block*> subStacks;
    std::vector<std::string> subStackLabels;
    Block* parentSubStack = nullptr;
    int parentSubStackIndex = -1;

    bool IsMainHead() const { return type == BlockType::HeadBlock; }
    bool IsHead() const { return type == BlockType::HeadBlock || type == BlockType::HeadRoutine; }
    bool IsSlotOnly() const { return IsSlotOnlyBlockType(type); }
    bool IsPluggedIn() const { return slotParent != nullptr; }
    bool IsInSubStack() const { return parentSubStack != nullptr; }
};

struct BlockTemplate {
    BlockType type;
    std::string label;
    ImU32 color;
    std::vector<SlotTemplate> slots;
    int subStackCount = 0;
    std::vector<std::string> subStackLabels;
};

struct BlockInfo {
    struct Field {
        std::string name;
        std::string text;
        SlotType allowedType = SlotType::Any;
        std::unique_ptr<BlockInfo> plugged;

        Field() = default;
        ~Field() = default;

        // Move operations
        Field(Field&&) noexcept = default;
        Field& operator=(Field&&) noexcept = default;

        // Explicit Deep Copy operations required due to unique_ptr
        Field(const Field& other)
            : name(other.name)
            , text(other.text)
            , allowedType(other.allowedType)
            , plugged(other.plugged ? std::make_unique<BlockInfo>(*other.plugged) : nullptr) {}

        Field& operator=(const Field& other) {
            if (this != &other) {
                name = other.name;
                text = other.text;
                allowedType = other.allowedType;
                plugged = other.plugged ? std::make_unique<BlockInfo>(*other.plugged) : nullptr;
            }
            return *this;
        }
    };

    uint64_t id = 0;
    BlockType type;
    std::string label;
    std::vector<Field> fields;
    std::vector<std::vector<BlockInfo>> subStacks;

    BlockInfo() = default;
    ~BlockInfo() = default;

    // Move operations
    BlockInfo(BlockInfo&&) noexcept = default;
    BlockInfo& operator=(BlockInfo&&) noexcept = default;

    // Explicit Deep Copy operations
    BlockInfo(const BlockInfo& other)
        : id(other.id)
        , type(other.type)
        , label(other.label)
        , fields(other.fields)
        , subStacks(other.subStacks) {}

    BlockInfo& operator=(const BlockInfo& other) {
        if (this != &other) {
            id = other.id;
            type = other.type;
            label = other.label;
            fields = other.fields;
            subStacks = other.subStacks;
        }
        return *this;
    }
};

class BlockEditor {
public:
    void Setup(std::function<void(std::string)> playSoundCallback);
    void Init();
    void Update();
    void Render();

    void SetErrorBlock(uint64_t blockId);
    void ClearErrors();

    std::vector<uint8_t> ExportBlocks() const;
    bool ImportBlocks(const std::vector<uint8_t>& data);

    static bool WalkBlockBlob(const std::vector<uint8_t>& data,
                               const std::function<void(BlockInfo)>& visit);

    void Cleanup(bool resetCamera = true);

private:
    std::function<void(std::string)> playSound;
    void InitPalette();

    Block* SpawnBlock(BlockType type, const std::string& label, ImVec2 pos);
    void LayoutBlock(Block* block);

    void UpdateLayouts();
    void UpdateBlockLayout(Block* block);
    ImU32 GetBlockColor(BlockType type) const;

    void DrawSidebar();
    void DrawCanvas();
    void DrawBlock(ImDrawList* dl, ImVec2 viewOrigin, Block* block, bool isDragGhost);
    void DrawSlot(ImDrawList* dl, ImVec2 viewOrigin, Block* owner, int slotIndex);

    void UpdateDragExistingBlock();
    void UpdateSnapping();
    void UpdateSlotTargeting();
    void UpdateDeletion();
    
    void CommitState();
    void Undo();
    void Redo();

    void DetachFromChain(Block* block);
    void InsertAfter(Block* anchor, Block* block);
    void DeleteBlock(Block* block);
    void DeleteChain(Block* chainStart);

    void PlugIntoSlot(Block* owner, int slotIndex, Block* block);
    void UnplugFromSlot(Block* block);
    void PlugIntoSubStack(Block* owner, int subStackIndex, Block* block);
    void UnplugFromSubStack(Block* block);
    bool IsDescendantViaChildren(Block* root, Block* candidate) const;

    ImVec2 EffectivePos(const Block* block) const;
    ImVec2 SlotRowOffset(const Block* owner, int slotIndex) const;
    ImVec2 SubStackOffset(const Block* owner, int subStackIndex) const;
    ImVec2 GetChainSlotScreenPos(ImVec2 viewOrigin, Block* anchor) const;
    Block* ChainRoot(Block* block) const;
    bool IsHeadOrDescendantOfHead(Block* block) const;
    bool IsInChainStartingAt(Block* chainStart, Block* candidate) const;

    uint64_t NextId() { return m_nextId++; }

    std::vector<std::unique_ptr<Block>> m_blocks;
    std::vector<BlockTemplate> m_palette;

    Block* m_headBlock = nullptr;

    ImVec2 m_canvasOffset{0, 0};
    float m_canvasZoom = 1.0f;
    ImVec2 m_canvasScreenOrigin{0, 0};

    Block* m_draggingBlock = nullptr;
    ImVec2 m_dragGrabOffset{0, 0};
    bool m_dragJustStarted = false;
    bool m_dragWasPluggedIn = false;
    bool m_dragWasInSubStack = false;

    Block* m_snapTarget = nullptr;
    Block* m_snapSubStackOwner = nullptr;
    int    m_snapSubStackIndex = -1;
    Block* m_slotTargetOwner = nullptr;
    int    m_slotTargetIndex = -1;
    Block* m_hoveredBlock = nullptr;

    uint64_t m_nextId = 1;
    
    std::vector<std::vector<uint8_t>> m_undoStack;
    std::vector<std::vector<uint8_t>> m_redoStack;
    bool m_isUndoing = false;

    static constexpr float kSidebarWidth = 160.0f;
    static constexpr float kSnapDistance = 24.0f;
    static constexpr float kSlotRowHeight = 28.0f;
    static constexpr float kSlotFieldWidth = 100.0f; 
    static constexpr float kBlockHeaderHeight = 30.0f;
    static constexpr float kSubStackMinHeight = 40.0f;
    static constexpr float kSubStackIndent = 20.0f;
    static constexpr float kBottomBarHeight = 20.0f;
};

std::optional<std::string> GenerateCode(const std::vector<uint8_t>& blocks, std::string* outError = nullptr, uint64_t* outErrorBlockId = nullptr);