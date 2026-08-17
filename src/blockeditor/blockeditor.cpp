#include "blockeditor.h"
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>
#include <cmath>

namespace {

constexpr char kMagic[4] = { 'L', 'B', 'L', 'K' };
constexpr uint32_t kVersion = 3; 

void WriteBytes(std::vector<uint8_t>& out, const void* data, size_t len) {
    const uint8_t* p = static_cast<const uint8_t*>(data);
    out.insert(out.end(), p, p + len);
}

template <typename T>
void WritePod(std::vector<uint8_t>& out, const T& value) {
    static_assert(std::is_trivially_copyable<T>::value, "WritePod requires a trivially copyable type");
    WriteBytes(out, &value, sizeof(T));
}

void WriteString(std::vector<uint8_t>& out, const std::string& s) {
    WritePod<uint32_t>(out, static_cast<uint32_t>(s.size()));
    WriteBytes(out, s.data(), s.size());
}

template <typename T>
bool ReadPod(const std::vector<uint8_t>& in, size_t& offset, T& out) {
    static_assert(std::is_trivially_copyable<T>::value, "ReadPod requires a trivially copyable type");
    if (offset + sizeof(T) > in.size()) return false;
    std::memcpy(&out, in.data() + offset, sizeof(T));
    offset += sizeof(T);
    return true;
}

bool ReadString(const std::vector<uint8_t>& in, size_t& offset, std::string& out) {
    uint32_t len = 0;
    if (!ReadPod(in, offset, len)) return false;
    if (offset + len > in.size()) return false;
    out.assign(reinterpret_cast<const char*>(in.data() + offset), len);
    offset += len;
    return true;
}

struct ParsedSlot {
    std::string name;
    std::string text;
    uint32_t allowedType = 0;
    bool hasPlugged = false;
    uint64_t pluggedId = 0;
};

struct ParsedBlock {
    uint64_t id = 0;
    uint32_t type = 0;
    std::string label;
    float posX = 0, posY = 0;
    float sizeX = 0, sizeY = 0;
    uint64_t nextId = 0;
    uint64_t prevId = 0;
    std::vector<ParsedSlot> slots;
    std::vector<uint64_t> subStacks;
};

bool IsKnownBlockTypeValue(uint32_t v) {
    return v >= static_cast<uint32_t>(BlockType::HeadBlock) &&
           v <= static_cast<uint32_t>(BlockType::Concat);
}

bool IsSlotCompatible(BlockType blockType, SlotType slotType) {
    if (slotType == SlotType::Any) return true;
    
    bool isNumberSource = (blockType == BlockType::Variable ||
                           blockType == BlockType::Ask ||
                           blockType == BlockType::MathAdd ||
                           blockType == BlockType::MathSub ||
                           blockType == BlockType::MathMul ||
                           blockType == BlockType::MathDiv ||
                           blockType == BlockType::RandomRange);
                           
    bool isTextSource = (blockType == BlockType::Variable ||
                         blockType == BlockType::Ask ||
                         blockType == BlockType::Concat);
                         
    bool isLogicSource = (blockType == BlockType::LogicLess ||
                          blockType == BlockType::LogicGreater ||
                          blockType == BlockType::LogicEqual ||
                          blockType == BlockType::LogicNotEqual ||
                          blockType == BlockType::LogicLessEqual ||
                          blockType == BlockType::LogicGreaterEqual ||
                          blockType == BlockType::LogicNot ||
                          blockType == BlockType::IsKeyDown ||
                          blockType == BlockType::IsTouching ||
                          blockType == BlockType::IsObstacleAhead);

    if (slotType == SlotType::Text) return isTextSource;
    if (slotType == SlotType::Number) return isNumberSource;
    if (slotType == SlotType::TextOrNumber) return isTextSource || isNumberSource;
    if (slotType == SlotType::Logic) return isLogicSource;
    
    return false;
}

bool ParseBlockBlob(const std::vector<uint8_t>& data, std::vector<ParsedBlock>& outBlocks, uint64_t& outHeadId) {
    size_t offset = 0;

    char magic[4];
    if (!ReadPod(data, offset, magic) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        return false;
    }

    uint32_t version = 0;
    if (!ReadPod(data, offset, version) || (version != 1 && version != 2 && version != kVersion)) {
        return false;
    }

    uint64_t headId = 0;
    if (!ReadPod(data, offset, headId)) return false;

    uint32_t blockCount = 0;
    if (!ReadPod(data, offset, blockCount)) return false;

    std::vector<ParsedBlock> parsed;
    parsed.reserve(blockCount);

    for (uint32_t i = 0; i < blockCount; ++i) {
        ParsedBlock pb;

        if (!ReadPod(data, offset, pb.id)) return false;
        if (!ReadPod(data, offset, pb.type)) return false;
        if (!ReadString(data, offset, pb.label)) return false;
        if (!ReadPod(data, offset, pb.posX)) return false;
        if (!ReadPod(data, offset, pb.posY)) return false;
        if (!ReadPod(data, offset, pb.sizeX)) return false;
        if (!ReadPod(data, offset, pb.sizeY)) return false;
        if (!ReadPod(data, offset, pb.nextId)) return false;
        if (!ReadPod(data, offset, pb.prevId)) return false;

        if (pb.id == 0) return false;
        if (!IsKnownBlockTypeValue(pb.type)) return false;

        uint32_t slotCount = 0;
        if (!ReadPod(data, offset, slotCount)) return false;
        pb.slots.reserve(slotCount);

        for (uint32_t s = 0; s < slotCount; ++s) {
            ParsedSlot ps;
            if (!ReadString(data, offset, ps.name)) return false;
            if (!ReadString(data, offset, ps.text)) return false;
            
            if (version >= 3) {
                if (!ReadPod(data, offset, ps.allowedType)) return false;
            } else {
                ps.allowedType = 0; 
            }

            uint8_t hasPlugged = 0;
            if (!ReadPod(data, offset, hasPlugged)) return false;
            ps.hasPlugged = (hasPlugged != 0);

            if (!ReadPod(data, offset, ps.pluggedId)) return false;
            if (ps.hasPlugged && ps.pluggedId == 0) return false;

            pb.slots.push_back(std::move(ps));
        }

        if (version >= 2) {
            uint32_t subStackCount = 0;
            if (!ReadPod(data, offset, subStackCount)) return false;
            pb.subStacks.reserve(subStackCount);
            for (uint32_t s = 0; s < subStackCount; ++s) {
                uint64_t subId = 0;
                if (!ReadPod(data, offset, subId)) return false;
                pb.subStacks.push_back(subId);
            }
        }

        parsed.push_back(std::move(pb));
    }

    if (headId == 0) return false;

    auto findBlock = [&](uint64_t id) -> const ParsedBlock* {
        for (const auto& pb : parsed) if (pb.id == id) return &pb;
        return nullptr;
    };
    auto idExists = [&](uint64_t id) { return id == 0 || findBlock(id) != nullptr; };

    bool foundHead = false;
    std::vector<uint8_t> claimedByNext(parsed.size(), 0);
    std::vector<uint8_t> claimedBySlot(parsed.size(), 0);
    auto indexOfId = [&](uint64_t id) -> int {
        for (size_t k = 0; k < parsed.size(); ++k) if (parsed[k].id == id) return static_cast<int>(k);
        return -1;
    };

    for (const auto& pb : parsed) {
        if (!idExists(pb.nextId)) return false;
        if (!idExists(pb.prevId)) return false;
        if (pb.id == headId) {
            if (pb.type != static_cast<uint32_t>(BlockType::HeadBlock)) return false;
            foundHead = true;
        }
        if (IsSlotOnlyBlockType(static_cast<BlockType>(pb.type))) {
            if (pb.nextId != 0 || pb.prevId != 0 || !pb.subStacks.empty()) return false;
        }

        if (pb.nextId != 0) {
            int idx = indexOfId(pb.nextId);
            if (idx < 0 || claimedByNext[idx] || claimedBySlot[idx]) return false;
            claimedByNext[idx] = 1;
        }

        for (const auto& ps : pb.slots) {
            if (!ps.hasPlugged) continue;
            int idx = indexOfId(ps.pluggedId);
            if (idx < 0 || claimedByNext[idx] || claimedBySlot[idx]) return false;
            claimedBySlot[idx] = 1;
            const ParsedBlock* child = findBlock(ps.pluggedId);
            if (!child || !IsSlotOnlyBlockType(static_cast<BlockType>(child->type))) return false;
        }

        for (uint64_t subId : pb.subStacks) {
            if (subId != 0) {
                int idx = indexOfId(subId);
                if (idx < 0 || claimedByNext[idx] || claimedBySlot[idx]) return false;
                claimedByNext[idx] = 1; 
                const ParsedBlock* child = findBlock(subId);
                if (!child || IsSlotOnlyBlockType(static_cast<BlockType>(child->type))) return false;
            }
        }
    }
    if (!foundHead) return false;

    {
        std::vector<uint8_t> visited(parsed.size(), 0);
        uint64_t cur = headId;
        while (cur != 0) {
            int idx = indexOfId(cur);
            if (idx < 0 || visited[idx]) return false;
            visited[idx] = 1;
            cur = parsed[idx].nextId;
        }

        for (size_t i = 0; i < parsed.size(); ++i) {
            std::vector<uint8_t> nodeVisited(parsed.size(), 0);
            std::vector<size_t> stack;
            stack.push_back(i);
            while (!stack.empty()) {
                size_t node = stack.back();
                stack.pop_back();
                if (nodeVisited[node]) return false;
                nodeVisited[node] = 1;
                
                for (const auto& ps : parsed[node].slots) {
                    if (ps.hasPlugged) {
                        int childIdx = indexOfId(ps.pluggedId);
                        if (childIdx >= 0) stack.push_back(static_cast<size_t>(childIdx));
                    }
                }
                for (uint64_t subId : parsed[node].subStacks) {
                    if (subId != 0) {
                        int childIdx = indexOfId(subId);
                        if (childIdx >= 0) stack.push_back(static_cast<size_t>(childIdx));
                    }
                }
            }
        }
    }

    outBlocks = std::move(parsed);
    outHeadId = headId;
    return true;
}

} 

// ============================================================================
// Init
// ============================================================================

void BlockEditor::Init() {
    Cleanup(true);
    
    m_blocks.push_back(std::make_unique<Block>());
    m_headBlock = m_blocks.back().get();
    m_headBlock->id = NextId();
    m_headBlock->type = BlockType::HeadBlock;
    m_headBlock->label = "On Execute";
    m_headBlock->pos = ImVec2(40, 40);
    LayoutBlock(m_headBlock);

    m_undoStack.clear();
    m_redoStack.clear();
    CommitState();
}

void BlockEditor::Setup(std::function<void(std::string)> playSoundCallback) {
    playSound = std::move(playSoundCallback);
    InitPalette();
}

void BlockEditor::SetErrorBlock(uint64_t blockId) {
    for (auto& up : m_blocks) {
        if (up->id == blockId) {
            up->hasError = true;
        } else {
            up->hasError = false;
        }
    }
}

void BlockEditor::ClearErrors() {
    for (auto& up : m_blocks) {
        up->hasError = false;
    }
}

void BlockEditor::InitPalette() {
    m_palette.push_back({ BlockType::MoveForward, "Move Forward", IM_COL32(90, 140, 210, 255),
                           { SlotTemplate{ "value", "5", SlotType::Number } } });

    m_palette.push_back({ BlockType::GoToPos, "Go to Position", IM_COL32(90, 140, 210, 255),
                           { SlotTemplate{ "X", "0", SlotType::Number }, SlotTemplate{ "Y", "0", SlotType::Number } } });

    m_palette.push_back({ BlockType::SetRot, "Set Rotation", IM_COL32(90, 140, 210, 255),
                           { SlotTemplate{ "X", "0", SlotType::Number }, SlotTemplate{ "Y", "0", SlotType::Number }, SlotTemplate{ "Z", "0", SlotType::Number } } });

    m_palette.push_back({ BlockType::WaitUntilGround, "Wait Until Ground", IM_COL32(90, 140, 210, 255), { } });
    
    m_palette.push_back({ BlockType::Wait, "Wait", IM_COL32(220, 160, 40, 255),
                           { SlotTemplate{"seconds", "1.0", SlotType::Number} } });

    m_palette.push_back({ BlockType::SayText, "Say Text", IM_COL32(210, 90, 140, 255),
                           { SlotTemplate {"text", "Hello, world!", SlotType::Any} } });

    m_palette.push_back({ BlockType::Ask, "Ask", IM_COL32(210, 90, 140, 255),
                           { SlotTemplate {"What?", "Question", SlotType::Text} } });

    m_palette.push_back({ BlockType::SetVariable, "Set Variable", IM_COL32(150, 100, 200, 255),
                           { SlotTemplate{ "name", "myVar", SlotType::Text }, SlotTemplate{ "value", "0", SlotType::TextOrNumber } } });

    m_palette.push_back({ BlockType::Variable, "Variable", IM_COL32(150, 100, 200, 255),
                           { SlotTemplate{ "name", "myVar", SlotType::Text } } });

    m_palette.push_back({ BlockType::DestroySelf, "Destroy Self", IM_COL32(200, 90, 90, 255), { } });
    m_palette.push_back({ BlockType::StopAll, "Stop All", IM_COL32(200, 90, 90, 255), { } });

    m_palette.push_back({ BlockType::If, "If", IM_COL32(220, 160, 40, 255),
                           { SlotTemplate{"condition", "1", SlotType::Logic} }, 1, {""} });

    m_palette.push_back({ BlockType::IfElse, "If Else", IM_COL32(220, 160, 40, 255),
                           { SlotTemplate{"condition", "1", SlotType::Logic} }, 2, {"", "else"} });

    m_palette.push_back({ BlockType::Forever, "Forever", IM_COL32(220, 160, 40, 255),
                           { }, 1, {""} });

    m_palette.push_back({ BlockType::Repeat, "Repeat", IM_COL32(220, 160, 40, 255),
                           { SlotTemplate{"times", "10", SlotType::Number} }, 1, {""} });

    ImU32 sensorColor = IM_COL32(255, 180, 120, 255);
    m_palette.push_back({ BlockType::IsKeyDown, "Is Key Down", sensorColor, 
        { SlotTemplate{"Key", "W", SlotType::Text} } });
    m_palette.push_back({ BlockType::IsObstacleAhead, "Is Obstacle Ahead", sensorColor, {  } });
    m_palette.push_back({ BlockType::IsTouching, "Is Touching", sensorColor, 
        { SlotTemplate{"Object", "name", SlotType::Text} } });

    ImU32 mathColor = IM_COL32(80, 180, 120, 255);
    m_palette.push_back({ BlockType::MathAdd, "A + B", mathColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::MathSub, "A - B", mathColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::MathMul, "A * B", mathColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::MathDiv, "A / B", mathColor, { SlotTemplate{"A", "1", SlotType::Number}, SlotTemplate{"B", "1", SlotType::Number} } });
    m_palette.push_back({ BlockType::RandomRange, "Random Range", mathColor, { SlotTemplate{"min", "0", SlotType::Number}, SlotTemplate{"max", "10", SlotType::Number} } });

    ImU32 logicColor = IM_COL32(100, 180, 200, 255);
    m_palette.push_back({ BlockType::LogicLess, "A < B", logicColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::LogicGreater, "A > B", logicColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::LogicEqual, "A == B", logicColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::LogicNotEqual, "A != B", logicColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::LogicLessEqual, "A <= B", logicColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::LogicGreaterEqual, "A >= B", logicColor, { SlotTemplate{"A", "0", SlotType::Number}, SlotTemplate{"B", "0", SlotType::Number} } });
    m_palette.push_back({ BlockType::LogicNot, "Not", logicColor, { SlotTemplate{"condition", "1", SlotType::Logic} } });

    ImU32 stringColor = IM_COL32(100,150,100,255);
    m_palette.push_back({ BlockType::Concat, "Join Strings", stringColor, { SlotTemplate{"string", "Hello", SlotType::Text}, SlotTemplate{"string", "World", SlotType::Text} } });
}

Block* BlockEditor::SpawnBlock(BlockType type, const std::string& label, ImVec2 pos) {
    m_blocks.push_back(std::make_unique<Block>());
    Block* b = m_blocks.back().get();
    b->id = NextId();
    b->type = type;
    b->label = label;
    b->pos = pos;

    for (const auto& tmpl : m_palette) {
        if (tmpl.type == type) {
            b->slots.reserve(tmpl.slots.size());
            for (const auto& st : tmpl.slots) {
                Slot s;
                s.name = st.name;
                s.text = st.defaultText;
                s.allowedType = st.allowedType;
                b->slots.push_back(std::move(s));
            }
            b->subStacks.resize(tmpl.subStackCount, nullptr);
            b->subStackLabels = tmpl.subStackLabels;
            break;
        }
    }

    LayoutBlock(b);
    return b;
}

void BlockEditor::LayoutBlock(Block* block) {
    UpdateBlockLayout(block);
}

void BlockEditor::UpdateLayouts() {
    for (auto& up : m_blocks) {
        if (!up->IsPluggedIn() && !up->IsInSubStack()) {
            Block* cur = up.get();
            while (cur) {
                UpdateBlockLayout(cur);
                cur = cur->next;
            }
        }
    }
}

void BlockEditor::UpdateBlockLayout(Block* block) {
    if (!block) return;

    float width = block->IsSlotOnly() ? 160.0f : 220.0f; 

    if (block->slots.empty() && block->subStacks.empty()) {
        block->size = ImVec2(width, block->IsSlotOnly() ? 28.0f : 50.0f);
        return;
    }

    float height = kBlockHeaderHeight + 6.0f;
    for (auto& slot : block->slots) {
        float rowHeight = kSlotRowHeight;
        if (slot.plugged) {
            UpdateBlockLayout(slot.plugged);
            rowHeight = std::max(rowHeight, slot.plugged->size.y + 4.0f);
        }
        height += rowHeight;
    }

    for (size_t i = 0; i < block->subStacks.size(); ++i) {
        if (i > 0 && i < block->subStackLabels.size() && !block->subStackLabels[i].empty()) {
            height += 24.0f; 
        }
        float stackHeight = kSubStackMinHeight;
        if (block->subStacks[i]) {
            float chainHeight = 0;
            Block* cur = block->subStacks[i];
            while (cur) {
                UpdateBlockLayout(cur);
                chainHeight += cur->size.y;
                cur = cur->next;
            }
            stackHeight = std::max(stackHeight, chainHeight);
        }
        height += stackHeight;
    }

    if (!block->subStacks.empty()) {
        height += kBottomBarHeight;
    }

    block->size = ImVec2(width, height);
}


void BlockEditor::Update() {
    if (ImGui::GetIO().KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z)) {
            if (ImGui::GetIO().KeyShift) Redo();
            else Undo();
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y)) {
            Redo();
        }
    }

    UpdateSnapping();
    UpdateSlotTargeting();
    UpdateDragExistingBlock();
    UpdateDeletion();
    UpdateLayouts(); 
}

void BlockEditor::UpdateDragExistingBlock() {
    if (!m_draggingBlock) return;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (m_dragJustStarted) {
            if (m_dragWasPluggedIn) {
                UnplugFromSlot(m_draggingBlock);
            } else if (m_dragWasInSubStack) {
                UnplugFromSubStack(m_draggingBlock);
            } else {
                DetachFromChain(m_draggingBlock);
            }
            m_dragJustStarted = false;
        }

        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 mouseCanvas = ImVec2(mouse.x - m_canvasScreenOrigin.x - m_canvasOffset.x, 
                                    mouse.y - m_canvasScreenOrigin.y - m_canvasOffset.y);
        m_draggingBlock->pos = ImVec2(mouseCanvas.x - m_dragGrabOffset.x, mouseCanvas.y - m_dragGrabOffset.y);
    } else {
        bool structureChanged = false;
        if (!m_draggingBlock->IsHead()) {
            if (m_slotTargetOwner && m_slotTargetIndex >= 0) {
                PlugIntoSlot(m_slotTargetOwner, m_slotTargetIndex, m_draggingBlock);
                if (playSound) playSound("snap");
                structureChanged = true;
            } else if (m_snapSubStackOwner && m_snapSubStackIndex >= 0) {
                PlugIntoSubStack(m_snapSubStackOwner, m_snapSubStackIndex, m_draggingBlock);
                if (playSound) playSound("snap");
                structureChanged = true;
            } else if (m_snapTarget) {
                InsertAfter(m_snapTarget, m_draggingBlock);
                if (playSound) playSound("snap");
                structureChanged = true;
            } else {
                structureChanged = true;
            }
        } else {
            structureChanged = true;
        }

        if (structureChanged) CommitState();

        m_snapTarget = nullptr;
        m_snapSubStackOwner = nullptr;
        m_snapSubStackIndex = -1;
        m_slotTargetOwner = nullptr;
        m_slotTargetIndex = -1;
        m_draggingBlock = nullptr;
        m_dragWasPluggedIn = false;
        m_dragWasInSubStack = false;
    }
}

void BlockEditor::UpdateSnapping() {
    m_snapTarget = nullptr;
    m_snapSubStackOwner = nullptr;
    m_snapSubStackIndex = -1;

    if (!m_draggingBlock) return;
    if (m_draggingBlock->IsHead()) return;
    if (m_draggingBlock->IsSlotOnly()) return;

    Block* best = nullptr;
    Block* bestSubStackOwner = nullptr;
    int bestSubStackIdx = -1;
    float bestDist = kSnapDistance;

    ImVec2 dragTop = m_draggingBlock->pos;

    for (auto& up : m_blocks) {
        Block* candidate = up.get();
        if (candidate == m_draggingBlock) continue;
        if (candidate->IsSlotOnly()) continue;
        if (candidate->IsPluggedIn()) continue;

        if (IsInChainStartingAt(m_draggingBlock, candidate)) continue;
        if (IsDescendantViaChildren(m_draggingBlock, candidate)) continue;

        if (candidate->next == nullptr) {
            ImVec2 candidatePos = EffectivePos(candidate);
            ImVec2 slot = ImVec2(candidatePos.x, candidatePos.y + candidate->size.y);
            float dist = std::abs(slot.x - dragTop.x) + std::abs(slot.y - dragTop.y);

            if (dist < bestDist) {
                bestDist = dist;
                best = candidate;
                bestSubStackOwner = nullptr;
                bestSubStackIdx = -1;
            }
        }

        for (size_t i = 0; i < candidate->subStacks.size(); ++i) {
            if (candidate->subStacks[i] == nullptr) {
                ImVec2 candidatePos = EffectivePos(candidate);
                ImVec2 offset = SubStackOffset(candidate, static_cast<int>(i));
                ImVec2 slot = ImVec2(candidatePos.x + offset.x, candidatePos.y + offset.y);
                float dist = std::abs(slot.x - dragTop.x) + std::abs(slot.y - dragTop.y);

                if (dist < bestDist) {
                    bestDist = dist;
                    best = nullptr;
                    bestSubStackOwner = candidate;
                    bestSubStackIdx = static_cast<int>(i);
                }
            }
        }
    }

    m_snapTarget = best;
    m_snapSubStackOwner = bestSubStackOwner;
    m_snapSubStackIndex = bestSubStackIdx;
}

void BlockEditor::UpdateSlotTargeting() {
    m_slotTargetOwner = nullptr;
    m_slotTargetIndex = -1;

    bool draggingExistingBlock = (m_draggingBlock != nullptr && m_draggingBlock->IsSlotOnly());
    if (!draggingExistingBlock) return;
    if (m_draggingBlock->IsHead()) return;

    BlockType draggedType = m_draggingBlock->type;
    ImVec2 mouse = ImGui::GetIO().MousePos;
    ImVec2 mouseCanvas = ImVec2(mouse.x - m_canvasScreenOrigin.x - m_canvasOffset.x, 
                                mouse.y - m_canvasScreenOrigin.y - m_canvasOffset.y);

    Block* bestOwner = nullptr;
    int bestIndex = -1;

    for (auto& up : m_blocks) {
        Block* owner = up.get();
        if (owner == m_draggingBlock) continue;
        if (IsDescendantViaChildren(m_draggingBlock, owner)) continue;

        for (size_t i = 0; i < owner->slots.size(); ++i) {
            if (owner->slots[i].plugged != nullptr) continue;
            if (!IsSlotCompatible(draggedType, owner->slots[i].allowedType)) continue;

            ImVec2 ownerPos = EffectivePos(owner);
            ImVec2 rowOffset = SlotRowOffset(owner, static_cast<int>(i));
            ImVec2 fieldMin = ImVec2(ownerPos.x + rowOffset.x, ownerPos.y + rowOffset.y);
            ImVec2 fieldMax = ImVec2(fieldMin.x + kSlotFieldWidth, fieldMin.y + kSlotRowHeight);

            if (mouseCanvas.x >= fieldMin.x && mouseCanvas.x <= fieldMax.x &&
                mouseCanvas.y >= fieldMin.y && mouseCanvas.y <= fieldMax.y) {
                bestOwner = owner;
                bestIndex = static_cast<int>(i);
                break;
            }
        }
        if (bestOwner) break;
    }

    m_slotTargetOwner = bestOwner;
    m_slotTargetIndex = bestIndex;
}

void BlockEditor::UpdateDeletion() {
    if (!ImGui::IsKeyPressed(ImGuiKey_Delete)) return;

    if (m_draggingBlock) {
        if (m_draggingBlock->IsHead()) return;
        Block* toDelete = m_draggingBlock;
        m_draggingBlock = nullptr;
        m_snapTarget = nullptr;
        m_snapSubStackOwner = nullptr;
        m_snapSubStackIndex = -1;
        m_slotTargetOwner = nullptr;
        m_slotTargetIndex = -1;
        m_dragJustStarted = false;
        m_dragWasPluggedIn = false;
        m_dragWasInSubStack = false;
        DeleteChain(toDelete);
        CommitState();
        return;
    }

    if (m_hoveredBlock && !m_hoveredBlock->IsHead()) {
        Block* toDelete = m_hoveredBlock;
        m_hoveredBlock = nullptr;
        DeleteBlock(toDelete);
        CommitState();
    }
}

void BlockEditor::DetachFromChain(Block* block) {
    if (!block) return;

    ImVec2 currentPos = EffectivePos(block);

    if (block->prev) block->prev->next = nullptr;
    block->prev = nullptr;
    block->pos = currentPos;
}

void BlockEditor::InsertAfter(Block* anchor, Block* block) {
    if (!anchor || !block || anchor == block) return;
    if (block->IsSlotOnly()) return;
    if (block->IsPluggedIn()) return;
    if (block->IsInSubStack()) return; 

    if (IsInChainStartingAt(block, anchor)) return;

    Block* oldNext = anchor->next;
    anchor->next = block;
    block->prev = anchor;

    Block* tail = block;
    while (tail->next) tail = tail->next;
    tail->next = oldNext;
    if (oldNext) oldNext->prev = tail;

    block->pos = ImVec2(anchor->pos.x, anchor->pos.y + anchor->size.y);
}

namespace {
template <typename Editor>
void ClearDanglingRefs(Editor* ed, Block* cur, Block*& snapTarget, Block*& snapSubTarget, int& snapSubIndex,
                        Block*& hoveredBlock, Block*& draggingBlock, Block*& slotTargetOwner, int& slotTargetIndex) {
    (void)ed;
    if (snapTarget == cur) snapTarget = nullptr;
    if (snapSubTarget == cur) { snapSubTarget = nullptr; snapSubIndex = -1; }
    if (hoveredBlock == cur) hoveredBlock = nullptr;
    if (draggingBlock == cur) draggingBlock = nullptr;
    if (slotTargetOwner == cur) { slotTargetOwner = nullptr; slotTargetIndex = -1; }
}
} 

void BlockEditor::DeleteBlock(Block* block) {
    if (!block || block->IsHead()) return;

    for (auto& slot : block->slots) {
        if (slot.plugged) {
            Block* child = slot.plugged;
            slot.plugged = nullptr;
            DeleteBlock(child);
        }
    }

    for (Block* sub : block->subStacks) {
        if (sub) {
            DeleteChain(sub);
        }
    }

    if (block->slotParent && block->slotParentIndex >= 0 &&
        static_cast<size_t>(block->slotParentIndex) < block->slotParent->slots.size()) {
        block->slotParent->slots[block->slotParentIndex].plugged = nullptr;
    }

    if (block->parentSubStack && block->parentSubStackIndex >= 0 &&
        static_cast<size_t>(block->parentSubStackIndex) < block->parentSubStack->subStacks.size()) {
        block->parentSubStack->subStacks[block->parentSubStackIndex] = nullptr;
    }

    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;

    ClearDanglingRefs(this, block, m_snapTarget, m_snapSubStackOwner, m_snapSubStackIndex,
                      m_hoveredBlock, m_draggingBlock, m_slotTargetOwner, m_slotTargetIndex);

    m_blocks.erase(
        std::remove_if(m_blocks.begin(), m_blocks.end(),
            [block](const std::unique_ptr<Block>& up) { return up.get() == block; }),
        m_blocks.end());
}

void BlockEditor::DeleteChain(Block* chainStart) {
    if (!chainStart || chainStart->IsHead()) return;

    if (chainStart->prev) chainStart->prev->next = nullptr;
    if (chainStart->parentSubStack) chainStart->parentSubStack->subStacks[chainStart->parentSubStackIndex] = nullptr;

    for (Block* cur = chainStart; cur; ) {
        Block* next = cur->next;

        for (auto& slot : cur->slots) {
            if (slot.plugged) {
                Block* child = slot.plugged;
                slot.plugged = nullptr;
                DeleteBlock(child);
            }
        }
        
        for (Block* sub : cur->subStacks) {
            if (sub) DeleteChain(sub);
        }

        ClearDanglingRefs(this, cur, m_snapTarget, m_snapSubStackOwner, m_snapSubStackIndex,
                          m_hoveredBlock, m_draggingBlock, m_slotTargetOwner, m_slotTargetIndex);

        m_blocks.erase(
            std::remove_if(m_blocks.begin(), m_blocks.end(),
                [cur](const std::unique_ptr<Block>& up) { return up.get() == cur; }),
            m_blocks.end());

        cur = next;
    }
}

void BlockEditor::PlugIntoSlot(Block* owner, int slotIndex, Block* block) {
    if (!owner || !block) return;
    if (slotIndex < 0 || static_cast<size_t>(slotIndex) >= owner->slots.size()) return;
    if (owner->slots[slotIndex].plugged != nullptr) return;
    if (!IsSlotCompatible(block->type, owner->slots[slotIndex].allowedType)) return;
    if (block->prev != nullptr || block->next != nullptr) return;
    if (block->IsHead()) return;
    if (block == owner) return;
    if (IsDescendantViaChildren(block, owner)) return;

    owner->slots[slotIndex].plugged = block;
    block->slotParent = owner;
    block->slotParentIndex = slotIndex;
}

void BlockEditor::UnplugFromSlot(Block* block) {
    if (!block || !block->slotParent) return;

    Block* owner = block->slotParent;
    int idx = block->slotParentIndex;
    ImVec2 currentPos = EffectivePos(block);

    if (idx >= 0 && static_cast<size_t>(idx) < owner->slots.size() && owner->slots[idx].plugged == block) {
        owner->slots[idx].plugged = nullptr;
    }

    block->slotParent = nullptr;
    block->slotParentIndex = -1;
    block->pos = currentPos;
}

void BlockEditor::PlugIntoSubStack(Block* owner, int subStackIndex, Block* block) {
    if (!owner || !block) return;
    if (subStackIndex < 0 || static_cast<size_t>(subStackIndex) >= owner->subStacks.size()) return;
    if (owner->subStacks[subStackIndex] != nullptr) return;
    if (block->prev != nullptr) return; 
    if (block->IsHead()) return;
    if (block == owner) return;
    if (IsDescendantViaChildren(block, owner)) return;

    owner->subStacks[subStackIndex] = block;
    block->parentSubStack = owner;
    block->parentSubStackIndex = subStackIndex;
}

void BlockEditor::UnplugFromSubStack(Block* block) {
    if (!block || !block->parentSubStack) return;

    Block* owner = block->parentSubStack;
    int idx = block->parentSubStackIndex;
    ImVec2 currentPos = EffectivePos(block);

    if (idx >= 0 && static_cast<size_t>(idx) < owner->subStacks.size() && owner->subStacks[idx] == block) {
        owner->subStacks[idx] = nullptr;
    }

    block->parentSubStack = nullptr;
    block->parentSubStackIndex = -1;
    block->pos = currentPos;
}

bool BlockEditor::IsDescendantViaChildren(Block* root, Block* candidate) const {
    if (!root || !candidate) return false;
    if (root == candidate) return true;

    for (Block* cur = root; cur; cur = cur->next) {
        if (cur == candidate) return true;
        for (const auto& slot : cur->slots) {
            if (slot.plugged && IsDescendantViaChildren(slot.plugged, candidate)) return true;
        }
        for (Block* sub : cur->subStacks) {
            if (sub && IsDescendantViaChildren(sub, candidate)) return true;
        }
    }
    return false;
}

ImVec2 BlockEditor::EffectivePos(const Block* block) const {
    if (!block) return ImVec2(0, 0);

    if (block->slotParent) {
        ImVec2 parentPos = EffectivePos(block->slotParent);
        ImVec2 rowOffset = SlotRowOffset(block->slotParent, block->slotParentIndex);
        return ImVec2(parentPos.x + rowOffset.x, parentPos.y + rowOffset.y);
    }

    if (block->parentSubStack) {
        ImVec2 parentPos = EffectivePos(block->parentSubStack);
        ImVec2 offset = SubStackOffset(block->parentSubStack, block->parentSubStackIndex);
        return ImVec2(parentPos.x + offset.x, parentPos.y + offset.y);
    }

    if (!block->prev) return block->pos;

    ImVec2 rootPos = EffectivePos(block->prev);
    return ImVec2(rootPos.x, rootPos.y + block->prev->size.y);
}

ImVec2 BlockEditor::SlotRowOffset(const Block* owner, int slotIndex) const {
    if (!owner || slotIndex < 0) return ImVec2(0, 0);
    
    float y = kBlockHeaderHeight;
    for (int i = 0; i < slotIndex; ++i) {
        float rowHeight = kSlotRowHeight;
        if (owner->slots[i].plugged) {
            rowHeight = std::max(rowHeight, owner->slots[i].plugged->size.y + 4.0f);
        }
        y += rowHeight;
    }
    
    float x = owner->size.x - kSlotFieldWidth - 10.0f;
    return ImVec2(x, y);
}

ImVec2 BlockEditor::SubStackOffset(const Block* owner, int subStackIndex) const {
    if (!owner || subStackIndex < 0) return ImVec2(0, 0);

    float y = kBlockHeaderHeight;
    for (int i = 0; i < owner->slots.size(); ++i) {
        float rH = kSlotRowHeight;
        if (owner->slots[i].plugged) rH = std::max(rH, owner->slots[i].plugged->size.y + 4.0f);
        y += rH;
    }

    for (int i = 0; i < subStackIndex; ++i) {
        if (i > 0 && i < owner->subStackLabels.size() && !owner->subStackLabels[i].empty()) {
            y += 24.0f;
        }
        float stackHeight = kSubStackMinHeight;
        if (owner->subStacks[i]) {
            float chainH = 0;
            Block* c = owner->subStacks[i];
            while(c) { chainH += c->size.y; c = c->next; }
            stackHeight = std::max(stackHeight, chainH);
        }
        y += stackHeight;
    }

    if (subStackIndex > 0 && subStackIndex < owner->subStackLabels.size() && !owner->subStackLabels[subStackIndex].empty()) {
        y += 24.0f;
    }

    return ImVec2(kSubStackIndent, y);
}

Block* BlockEditor::ChainRoot(Block* block) const {
    Block* cur = block;
    while (cur && cur->prev) cur = cur->prev;
    return cur;
}

bool BlockEditor::IsHeadOrDescendantOfHead(Block* block) const {
    Block* cur = m_headBlock;
    while (cur) {
        if (cur == block) return true;
        cur = cur->next;
    }
    return false;
}

bool BlockEditor::IsInChainStartingAt(Block* chainStart, Block* candidate) const {
    Block* cur = chainStart;
    while (cur) {
        if (cur == candidate) return true;
        cur = cur->next;
    }
    return false;
}

void BlockEditor::CommitState() {
    if (m_isUndoing) return;
    m_undoStack.push_back(ExportBlocks());
    if (m_undoStack.size() > 50) { 
        m_undoStack.erase(m_undoStack.begin());
    }
    m_redoStack.clear();
}

void BlockEditor::Undo() {
    if (m_undoStack.size() > 1) {
        m_redoStack.push_back(m_undoStack.back());
        m_undoStack.pop_back();
        m_isUndoing = true;
        ImportBlocks(m_undoStack.back());
        m_isUndoing = false;
    }
}

void BlockEditor::Redo() {
    if (!m_redoStack.empty()) {
        m_undoStack.push_back(m_redoStack.back());
        m_isUndoing = true;
        ImportBlocks(m_redoStack.back());
        m_isUndoing = false;
        m_redoStack.pop_back();
    }
}

void BlockEditor::Render() {
    ImGui::Begin("Block Editor");

    DrawSidebar();
    ImGui::SameLine();
    DrawCanvas();

    ImGui::End();
}

void BlockEditor::DrawSidebar() {
    ImGui::BeginChild("palette", ImVec2(kSidebarWidth, 0), true);
    
    float btnW = (kSidebarWidth - 24.0f) / 2.0f;
    if (ImGui::Button("Undo", ImVec2(btnW, 24))) Undo();
    ImGui::SameLine();
    if (ImGui::Button("Redo", ImVec2(btnW, 24))) Redo();
    
    ImGui::Separator();
    ImGui::TextDisabled("BLOCKS");
    ImGui::Separator();

    for (int i = 0; i < (int)m_palette.size(); ++i) {
        const BlockTemplate& tmpl = m_palette[i];
        ImGui::PushID(i);

        float h = IsSlotOnlyBlockType(tmpl.type) ? 28.0f : 40.0f;
        ImVec2 buttonSize(kSidebarWidth - 24.0f, h);
        ImGui::PushStyleColor(ImGuiCol_Button, tmpl.color);
        ImGui::Button(tmpl.label.c_str(), buttonSize);
        ImGui::PopStyleColor();

        if (ImGui::IsItemActive() && !m_draggingBlock) {
            if (ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.0f)) {
                ImVec2 mouse = ImGui::GetIO().MousePos;
                ImVec2 mouseCanvas = ImVec2(mouse.x - m_canvasScreenOrigin.x - m_canvasOffset.x,
                                            mouse.y - m_canvasScreenOrigin.y - m_canvasOffset.y);
                ImVec2 grabOffset = ImVec2(15.0f, 15.0f);
                ImVec2 dropPos = ImVec2(mouseCanvas.x - grabOffset.x, mouseCanvas.y - grabOffset.y);
                
                Block* spawned = SpawnBlock(tmpl.type, tmpl.label, dropPos);
                m_draggingBlock = spawned;
                m_dragGrabOffset = grabOffset;
                m_dragJustStarted = false;
                m_dragWasPluggedIn = false;
                m_dragWasInSubStack = false;
            }
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

void BlockEditor::DrawCanvas() {
    ImGui::BeginChild("canvas", ImVec2(0, 0), true,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    m_canvasScreenOrigin = canvasOrigin;
    ImDrawList* dl = ImGui::GetWindowDrawList();

    if (ImGui::IsWindowHovered()) {
        if (ImGui::IsMouseDragging(ImGuiMouseButton_Right, 0.0f) || 
            ImGui::IsMouseDragging(ImGuiMouseButton_Middle, 0.0f)) {
            m_canvasOffset.x += ImGui::GetIO().MouseDelta.x;
            m_canvasOffset.y += ImGui::GetIO().MouseDelta.y;
        }
        
        if (!ImGui::GetIO().KeyCtrl && ImGui::GetIO().MouseWheel != 0.0f) {
            if (ImGui::GetIO().KeyShift) {
                m_canvasOffset.x += ImGui::GetIO().MouseWheel * 30.0f;
            } else {
                m_canvasOffset.y += ImGui::GetIO().MouseWheel * 30.0f;
            }
        }
    }

    ImU32 gridColor = IM_COL32(200, 200, 200, 40);
    float gridSize = 32.0f;
    ImVec2 winSize = ImGui::GetWindowSize();
    
    float fmodx = fmodf(m_canvasOffset.x, gridSize);
    if (fmodx < 0.0f) fmodx += gridSize;
    for (float x = fmodx; x < winSize.x; x += gridSize) {
        dl->AddLine(ImVec2(canvasOrigin.x + x, canvasOrigin.y), ImVec2(canvasOrigin.x + x, canvasOrigin.y + winSize.y), gridColor);
    }
    
    float fmody = fmodf(m_canvasOffset.y, gridSize);
    if (fmody < 0.0f) fmody += gridSize;
    for (float y = fmody; y < winSize.y; y += gridSize) {
        dl->AddLine(ImVec2(canvasOrigin.x, canvasOrigin.y + y), ImVec2(canvasOrigin.x + winSize.x, canvasOrigin.y + y), gridColor);
    }
    
    ImVec2 viewOrigin = ImVec2(canvasOrigin.x + m_canvasOffset.x, canvasOrigin.y + m_canvasOffset.y);

    m_hoveredBlock = nullptr;

    for (auto& up : m_blocks) {
        Block* b = up.get();
        if (b->IsPluggedIn() || b->IsInSubStack()) continue; 
        DrawBlock(dl, viewOrigin, b, false);
    }

    if (m_draggingBlock) {
        if (m_snapTarget) {
            ImVec2 slotPos = GetChainSlotScreenPos(viewOrigin, m_snapTarget);
            ImVec2 slotMax = ImVec2(slotPos.x + m_snapTarget->size.x, slotPos.y + 6.0f);
            dl->AddRectFilled(slotPos, slotMax, IM_COL32(255, 220, 80, 200), 3.0f);
        } else if (m_snapSubStackOwner && m_snapSubStackIndex >= 0) {
            ImVec2 ownerPos = EffectivePos(m_snapSubStackOwner);
            ImVec2 offset = SubStackOffset(m_snapSubStackOwner, m_snapSubStackIndex);
            ImVec2 slotPos = ImVec2(viewOrigin.x + ownerPos.x + offset.x, viewOrigin.y + ownerPos.y + offset.y);
            ImVec2 slotMax = ImVec2(slotPos.x + m_snapSubStackOwner->size.x - offset.x, slotPos.y + 6.0f);
            dl->AddRectFilled(slotPos, slotMax, IM_COL32(255, 220, 80, 200), 3.0f);
        }
    }

    if (m_slotTargetOwner && m_slotTargetIndex >= 0) {
        ImVec2 ownerPos = EffectivePos(m_slotTargetOwner);
        ImVec2 rowOffset = SlotRowOffset(m_slotTargetOwner, m_slotTargetIndex);
        ImVec2 fieldMin = ImVec2(viewOrigin.x + ownerPos.x + rowOffset.x, viewOrigin.y + ownerPos.y + rowOffset.y);
        ImVec2 fieldMax = ImVec2(fieldMin.x + kSlotFieldWidth, fieldMin.y + kSlotRowHeight);
        dl->AddRect(fieldMin, fieldMax, IM_COL32(255, 220, 80, 255), 4.0f, 0, 3.0f);
    }

    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::InvisibleButton("canvas_bg", ImGui::GetContentRegionAvail());

    ImGui::EndChild();
}

ImU32 BlockEditor::GetBlockColor(BlockType type) const {
    if (type == BlockType::HeadBlock) return IM_COL32(200, 90, 90, 255);
    for (const auto& tmpl : m_palette) {
        if (tmpl.type == type) return tmpl.color;
    }
    return IsSlotOnlyBlockType(type) ? IM_COL32(150, 100, 200, 255) : IM_COL32(90, 140, 210, 255);
}

void BlockEditor::DrawBlock(ImDrawList* dl, ImVec2 viewOrigin, Block* block, bool isDragGhost) {
    ImVec2 effPos = EffectivePos(block);
    ImVec2 blockMin = ImVec2(viewOrigin.x + effPos.x, viewOrigin.y + effPos.y);
    ImVec2 blockMax = ImVec2(blockMin.x + block->size.x, blockMin.y + block->size.y);

    float rounding = block->IsSlotOnly() ? std::min(block->size.y * 0.5f, 14.0f) : 8.0f;

    if (block->hasError) {
        dl->AddRectFilled(ImVec2(blockMin.x - 4.0f, blockMin.y - 4.0f), ImVec2(blockMax.x + 4.0f, blockMax.y + 4.0f), IM_COL32(255, 60, 60, 255), rounding + 2.0f);
    }

    ImU32 bodyColor = GetBlockColor(block->type);
    
    ImU8 r = bodyColor & 0xFF;
    ImU8 g = (bodyColor >> 8) & 0xFF;
    ImU8 b = (bodyColor >> 16) & 0xFF;
    ImU8 a = (bodyColor >> 24) & 0xFF;

    ImU32 borderColor;
    float borderThickness;
    if (block->hasError) {
        borderColor = IM_COL32(255, 60, 60, 255);
        borderThickness = 4.0f;
    } else {
        borderColor = IM_COL32(std::max(0, r - 50), std::max(0, g - 50), std::max(0, b - 50), a);
        borderThickness = 2.0f;
    }

    if (block->subStacks.empty()) {
        dl->AddRectFilled(blockMin, blockMax, bodyColor, rounding);
        dl->AddRect(blockMin, blockMax, borderColor, rounding, 0, borderThickness);
    } else {
        float topH = kBlockHeaderHeight + 6.0f;
        for (auto& s : block->slots) {
            float rH = kSlotRowHeight;
            if (s.plugged) rH = std::max(rH, s.plugged->size.y + 4.0f);
            topH += rH;
        }

        dl->AddRectFilled(blockMin, ImVec2(blockMax.x, blockMin.y + topH), bodyColor, rounding, ImDrawFlags_RoundCornersTop);
        dl->AddLine(ImVec2(blockMin.x + rounding, blockMin.y), ImVec2(blockMax.x - rounding, blockMin.y), borderColor, borderThickness);
        
        float curY = blockMin.y + topH;
        for (size_t i = 0; i < block->subStacks.size(); ++i) {
            if (i > 0) {
                float midH = 24.0f;
                dl->AddRectFilled(ImVec2(blockMin.x, curY), ImVec2(blockMax.x, curY + midH), bodyColor, 0);
                if (i < block->subStackLabels.size()) {
                    dl->AddText(ImVec2(blockMin.x + 12, curY + 4), IM_COL32(230, 230, 230, 255), block->subStackLabels[i].c_str());
                }
                curY += midH;
            }
            
            float stackH = kSubStackMinHeight;
            if (block->subStacks[i]) {
                float chainH = 0; Block* c = block->subStacks[i];
                while(c) { chainH += c->size.y; c = c->next; }
                stackH = std::max(stackH, chainH);
            }
            dl->AddRectFilled(ImVec2(blockMin.x, curY), ImVec2(blockMin.x + kSubStackIndent, curY + stackH), bodyColor, 0);
            curY += stackH;
        }
        
        dl->AddRectFilled(ImVec2(blockMin.x, curY), ImVec2(blockMax.x, curY + kBottomBarHeight), bodyColor, rounding, ImDrawFlags_RoundCornersBottom);
        dl->AddLine(ImVec2(blockMin.x + rounding, curY + kBottomBarHeight), ImVec2(blockMax.x - rounding, curY + kBottomBarHeight), borderColor, borderThickness);
    }

    dl->AddText(ImVec2(blockMin.x + 12, blockMin.y + kBlockHeaderHeight * 0.5f - 8),
                IM_COL32(255, 255, 255, 255), block->label.c_str());

    for (size_t i = 0; i < block->slots.size(); ++i) {
        DrawSlot(dl, viewOrigin, block, static_cast<int>(i));
    }

    for (size_t i = 0; i < block->subStacks.size(); ++i) {
        if (block->subStacks[i]) {
            DrawBlock(dl, viewOrigin, block->subStacks[i], false);
        }
    }

    ImVec2 headerMin = blockMin;
    ImVec2 headerSize = ImVec2(block->size.x, block->slots.empty() && block->subStacks.empty() ? block->size.y : kBlockHeaderHeight);
    ImGui::SetCursorScreenPos(headerMin);
    ImGui::PushID((int)block->id);
    ImGui::InvisibleButton("block_drag", headerSize);

    if (ImGui::IsItemHovered()) {
        m_hoveredBlock = block;
    }

    if (ImGui::IsItemActivated() && !m_draggingBlock) {
        m_draggingBlock = block;
        m_dragJustStarted = true;
        m_dragWasPluggedIn = block->IsPluggedIn();
        m_dragWasInSubStack = block->IsInSubStack();
        ImVec2 mouse = ImGui::GetIO().MousePos;
        m_dragGrabOffset = ImVec2(mouse.x - blockMin.x, mouse.y - blockMin.y);
    }
    ImGui::PopID();
}

void BlockEditor::DrawSlot(ImDrawList* dl, ImVec2 viewOrigin, Block* owner, int slotIndex) {
    Slot& slot = owner->slots[slotIndex];
    ImVec2 ownerPos = EffectivePos(owner);
    ImVec2 rowOffset = SlotRowOffset(owner, slotIndex);
    ImVec2 rowMin = ImVec2(viewOrigin.x + ownerPos.x + rowOffset.x, viewOrigin.y + ownerPos.y + rowOffset.y);

    ImVec2 labelPos = ImVec2(viewOrigin.x + ownerPos.x + 12, rowMin.y + kSlotRowHeight * 0.5f - 8);
    dl->AddText(labelPos, IM_COL32(230, 230, 230, 255), slot.name.c_str());

    if (slot.plugged) {
        DrawBlock(dl, viewOrigin, slot.plugged, false);
        return;
    }

    ImVec2 fieldMax = ImVec2(rowMin.x + kSlotFieldWidth, rowMin.y + kSlotRowHeight - 4.0f);
    
    ImU32 fieldColor = (slot.allowedType == SlotType::Text) ? IM_COL32(235, 235, 235, 235) : IM_COL32(255, 255, 255, 235);
    dl->AddRectFilled(rowMin, fieldMax, fieldColor, 4.0f);
    dl->AddRect(rowMin, fieldMax, IM_COL32(60, 60, 60, 255), 4.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(rowMin);
    ImGui::PushID(static_cast<int>(owner->id));
    ImGui::PushID(slotIndex);
    ImGui::SetNextItemWidth(kSlotFieldWidth);

    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", slot.text.c_str());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
    if (ImGui::InputText("##field", buf, sizeof(buf))) {
        slot.text = buf;
    }
    
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        CommitState();
    }
    
    ImGui::PopStyleColor(2);
    ImGui::PopID();
    ImGui::PopID();
}

ImVec2 BlockEditor::GetChainSlotScreenPos(ImVec2 viewOrigin, Block* anchor) const {
    ImVec2 effPos = EffectivePos(anchor);
    return ImVec2(viewOrigin.x + effPos.x, viewOrigin.y + effPos.y + anchor->size.y);
}

// ============================================================================
// Import / Export
// ============================================================================

std::vector<uint8_t> BlockEditor::ExportBlocks() const {
    std::vector<uint8_t> out;

    WriteBytes(out, kMagic, sizeof(kMagic));
    WritePod(out, kVersion);
    WritePod<uint64_t>(out, m_headBlock ? m_headBlock->id : 0);
    WritePod<uint32_t>(out, static_cast<uint32_t>(m_blocks.size()));

    for (const auto& up : m_blocks) {
        const Block* b = up.get();
        WritePod<uint64_t>(out, b->id);
        WritePod<uint32_t>(out, static_cast<uint32_t>(b->type));
        WriteString(out, b->label);
        WritePod<float>(out, b->pos.x);
        WritePod<float>(out, b->pos.y);
        WritePod<float>(out, b->size.x);
        WritePod<float>(out, b->size.y);
        WritePod<uint64_t>(out, b->next ? b->next->id : 0);
        WritePod<uint64_t>(out, b->prev ? b->prev->id : 0);

        WritePod<uint32_t>(out, static_cast<uint32_t>(b->slots.size()));
        for (const auto& slot : b->slots) {
            WriteString(out, slot.name);
            WriteString(out, slot.text);
            WritePod<uint32_t>(out, static_cast<uint32_t>(slot.allowedType));
            uint8_t hasPlugged = slot.plugged ? 1 : 0;
            WritePod<uint8_t>(out, hasPlugged);
            WritePod<uint64_t>(out, slot.plugged ? slot.plugged->id : 0);
        }

        WritePod<uint32_t>(out, static_cast<uint32_t>(b->subStacks.size()));
        for (Block* sub : b->subStacks) {
            WritePod<uint64_t>(out, sub ? sub->id : 0);
        }
    }

    return out;
}

void BlockEditor::Cleanup(bool resetCamera) {
    m_blocks.clear();
    m_headBlock = nullptr;
    m_draggingBlock = nullptr;
    m_snapTarget = nullptr;
    m_snapSubStackOwner = nullptr;
    m_snapSubStackIndex = -1;
    m_slotTargetOwner = nullptr;
    m_slotTargetIndex = -1;
    m_hoveredBlock = nullptr;
    m_dragJustStarted = false;
    m_dragWasPluggedIn = false;
    m_dragWasInSubStack = false;
    
    if (resetCamera) {
        m_canvasOffset = ImVec2(0, 0);
    }
}

bool BlockEditor::ImportBlocks(const std::vector<uint8_t>& data) {
    std::vector<ParsedBlock> parsed;
    uint64_t headId = 0;
    if (!ParseBlockBlob(data, parsed, headId)) return false;

    Cleanup(false);

    std::vector<std::pair<uint64_t, Block*>> idToBlock;
    idToBlock.reserve(parsed.size());

    for (const auto& pb : parsed) {
        m_blocks.push_back(std::make_unique<Block>());
        Block* b = m_blocks.back().get();
        b->id = pb.id;
        b->type = static_cast<BlockType>(pb.type);
        b->label = pb.label;
        b->pos = ImVec2(pb.posX, pb.posY);
        b->size = ImVec2(pb.sizeX, pb.sizeY);

        b->slots.reserve(pb.slots.size());
        for (const auto& ps : pb.slots) {
            Slot s;
            s.name = ps.name;
            s.text = ps.text;
            s.allowedType = static_cast<SlotType>(ps.allowedType);
            b->slots.push_back(std::move(s));
        }

        for (const auto& tmpl : m_palette) {
            if (tmpl.type == b->type) {
                b->subStackLabels = tmpl.subStackLabels;
                break;
            }
        }
        b->subStacks.resize(pb.subStacks.size(), nullptr);

        idToBlock.emplace_back(pb.id, b);
    }

    auto resolve = [&](uint64_t id) -> Block* {
        if (id == 0) return nullptr;
        for (auto& kv : idToBlock) if (kv.first == id) return kv.second;
        return nullptr;
    };

    for (size_t i = 0; i < parsed.size(); ++i) {
        Block* b = idToBlock[i].second;
        b->next = resolve(parsed[i].nextId);
        b->prev = resolve(parsed[i].prevId);
    }

    for (size_t i = 0; i < parsed.size(); ++i) {
        Block* b = idToBlock[i].second;
        for (size_t s = 0; s < parsed[i].slots.size(); ++s) {
            const auto& ps = parsed[i].slots[s];
            if (!ps.hasPlugged) continue;
            Block* child = resolve(ps.pluggedId);
            if (!child) continue;
            b->slots[s].plugged = child;
            child->slotParent = b;
            child->slotParentIndex = static_cast<int>(s);
        }
        for (size_t s = 0; s < parsed[i].subStacks.size(); ++s) {
            uint64_t subId = parsed[i].subStacks[s];
            if (subId == 0) continue;
            Block* child = resolve(subId);
            if (!child) continue;
            b->subStacks[s] = child;
            child->parentSubStack = b;
            child->parentSubStackIndex = static_cast<int>(s);
        }
    }

    m_headBlock = resolve(headId);

    for (const auto& pb : parsed) {
        if (pb.id >= m_nextId) m_nextId = pb.id + 1;
    }

    return true;
}

namespace {
BlockInfo BuildBlockInfo(const ParsedBlock& pb, const std::function<const ParsedBlock*(uint64_t)>& findById) {
    BlockInfo info;
    info.id = pb.id;
    info.type = static_cast<BlockType>(pb.type);
    info.label = pb.label;

    info.fields.reserve(pb.slots.size());
    for (const auto& ps : pb.slots) {
        BlockInfo::Field field;
        field.name = ps.name;
        field.text = ps.text;
        field.allowedType = static_cast<SlotType>(ps.allowedType);
        if (ps.hasPlugged) {
            const ParsedBlock* child = findById(ps.pluggedId);
            if (child) {
                field.plugged = std::make_unique<BlockInfo>(BuildBlockInfo(*child, findById));
            }
        }
        info.fields.push_back(std::move(field));
    }

    info.subStacks.resize(pb.subStacks.size());
    for (size_t i = 0; i < pb.subStacks.size(); ++i) {
        uint64_t curId = pb.subStacks[i];
        while (curId != 0) {
            const ParsedBlock* child = findById(curId);
            if (!child) break;
            info.subStacks[i].push_back(BuildBlockInfo(*child, findById));
            curId = child->nextId;
        }
    }

    return info;
}
} 

bool BlockEditor::WalkBlockBlob(const std::vector<uint8_t>& data,
                                 const std::function<void(BlockInfo)>& visit) {
    std::vector<ParsedBlock> parsed;
    uint64_t headId = 0;
    if (!ParseBlockBlob(data, parsed, headId)) return false;

    std::function<const ParsedBlock*(uint64_t)> findById = [&](uint64_t id) -> const ParsedBlock* {
        if (id == 0) return nullptr;
        for (const auto& pb : parsed) if (pb.id == id) return &pb;
        return nullptr;
    };

    const ParsedBlock* head = findById(headId);
    if (!head) return false;

    for (const ParsedBlock* cur = head; cur; cur = findById(cur->nextId)) {
        visit(BuildBlockInfo(*cur, findById));
    }

    return true;
}