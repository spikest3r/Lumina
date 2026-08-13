#include "blockeditor.h"
#include <algorithm>
#include <cstring>
#include <type_traits>
#include <utility>

namespace {

constexpr char kMagic[4] = { 'L', 'B', 'L', 'K' };
constexpr uint32_t kVersion = 1;

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

// Reads sizeof(T) bytes at `offset`, advances `offset`, returns true on
// success. Returns false (leaving `offset`/`out` untouched) if the blob
// doesn't have enough remaining bytes
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
};

bool IsKnownBlockTypeValue(uint32_t v) {
    return v == static_cast<uint32_t>(BlockType::HeadBlock) ||
           v == static_cast<uint32_t>(BlockType::MoveForward) ||
           v == static_cast<uint32_t>(BlockType::WaitUntilGround) ||
           v == static_cast<uint32_t>(BlockType::Variable);
}

bool ParseBlockBlob(const std::vector<uint8_t>& data, std::vector<ParsedBlock>& outBlocks, uint64_t& outHeadId) {
    size_t offset = 0;

    char magic[4];
    if (!ReadPod(data, offset, magic) || std::memcmp(magic, kMagic, sizeof(kMagic)) != 0) {
        return false; // not our format, or truncated
    }

    uint32_t version = 0;
    if (!ReadPod(data, offset, version) || version != kVersion) {
        return false; // unknown/future/old format — refuse rather than misparse
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

        if (pb.id == 0) return false; // 0 is the null sentinel, never a valid id
        if (!IsKnownBlockTypeValue(pb.type)) {
            return false; // unrecognized BlockType value, malformed or from a newer version
        }

        uint32_t slotCount = 0;
        if (!ReadPod(data, offset, slotCount)) return false;
        pb.slots.reserve(slotCount);

        for (uint32_t s = 0; s < slotCount; ++s) {
            ParsedSlot ps;
            if (!ReadString(data, offset, ps.name)) return false;
            if (!ReadString(data, offset, ps.text)) return false;

            uint8_t hasPlugged = 0;
            if (!ReadPod(data, offset, hasPlugged)) return false;
            ps.hasPlugged = (hasPlugged != 0);

            if (!ReadPod(data, offset, ps.pluggedId)) return false;
            if (ps.hasPlugged && ps.pluggedId == 0) return false; // inconsistent: says plugged but no id

            pb.slots.push_back(std::move(ps));
        }

        parsed.push_back(std::move(pb));
    }

    if (headId == 0) return false; // must have designated a head

    // --- Cross-reference validation ---
    auto findBlock = [&](uint64_t id) -> const ParsedBlock* {
        for (const auto& pb : parsed) if (pb.id == id) return &pb;
        return nullptr;
    };
    auto idExists = [&](uint64_t id) {
        if (id == 0) return true; // null is always "valid" as a reference
        return findBlock(id) != nullptr;
    };

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
        // A slot-only type (Variable etc) must never appear in the stack axis.
        if (IsSlotOnlyBlockType(static_cast<BlockType>(pb.type))) {
            if (pb.nextId != 0 || pb.prevId != 0) return false;
        }

        if (pb.nextId != 0) {
            int idx = indexOfId(pb.nextId);
            if (idx < 0) return false; // unreachable given idExists above, but stay safe
            if (claimedByNext[idx] || claimedBySlot[idx]) return false; // double-claimed
            claimedByNext[idx] = 1;
        }

        for (const auto& ps : pb.slots) {
            if (!ps.hasPlugged) continue;
            if (!idExists(ps.pluggedId)) return false;
            int idx = indexOfId(ps.pluggedId);
            if (idx < 0) return false;
            if (claimedByNext[idx] || claimedBySlot[idx]) return false; // double-claimed
            claimedBySlot[idx] = 1;
            const ParsedBlock* child = findBlock(ps.pluggedId);
            if (!child || !IsSlotOnlyBlockType(static_cast<BlockType>(child->type))) return false;
        }
    }
    if (!foundHead) return false;

    {
        std::vector<uint8_t> visited(parsed.size(), 0);
        uint64_t cur = headId;
        while (cur != 0) {
            int idx = indexOfId(cur);
            if (idx < 0) return false;
            if (visited[idx]) return false; // cycle
            visited[idx] = 1;
            cur = parsed[idx].nextId;
        }

        for (size_t i = 0; i < parsed.size(); ++i) {
            std::vector<uint8_t> slotVisited(parsed.size(), 0);
            // local DFS over the plug graph starting at block i
            std::vector<size_t> stack;
            stack.push_back(i);
            while (!stack.empty()) {
                size_t node = stack.back();
                stack.pop_back();
                if (slotVisited[node]) return false; // cycle reachable from i
                slotVisited[node] = 1;
                for (const auto& ps : parsed[node].slots) {
                    if (!ps.hasPlugged) continue;
                    int childIdx = indexOfId(ps.pluggedId);
                    if (childIdx < 0) return false;
                    stack.push_back(static_cast<size_t>(childIdx));
                }
            }
        }
    }

    outBlocks = std::move(parsed);
    outHeadId = headId;
    return true;
}

} // namespace

// ============================================================================
// Init
// ============================================================================

void BlockEditor::Init() {
    Cleanup();
    
    // Spawn the permanent head block. Fixed position; not draggable, not
    // deletable, always exists. Everything in the "main" pile hangs off it.
    m_blocks.push_back(std::make_unique<Block>());
    m_headBlock = m_blocks.back().get();
    m_headBlock->id = NextId();
    m_headBlock->type = BlockType::HeadBlock;
    m_headBlock->label = "On Execute";
    m_headBlock->pos = ImVec2(40, 40);
    LayoutBlock(m_headBlock);
}

void BlockEditor::InitPalette() {
    m_palette.push_back({ BlockType::MoveForward, "Move Forward", IM_COL32(90, 140, 210, 255),
                           { SlotTemplate{ "value", "0" } } });

    m_palette.push_back({ BlockType::WaitUntilGround, "Wait Until Ground", IM_COL32(90, 140, 210, 255),
                           { } });

    m_palette.push_back({ BlockType::Variable, "Variable", IM_COL32(150, 100, 200, 255),
                           { SlotTemplate{ "name", "myVar" } } });

    // (If, Repeat, MoveTo, SetVar, ...).
}

Block* BlockEditor::SpawnBlock(BlockType type, const std::string& label, ImVec2 pos) {
    m_blocks.push_back(std::make_unique<Block>());
    Block* b = m_blocks.back().get();
    b->id = NextId();
    b->type = type;
    b->label = label;
    b->pos = pos;

    // Copy this type's fixed field list from its palette template, if any.
    for (const auto& tmpl : m_palette) {
        if (tmpl.type == type) {
            b->slots.reserve(tmpl.slots.size());
            for (const auto& st : tmpl.slots) {
                Slot s;
                s.name = st.name;
                s.text = st.defaultText;
                b->slots.push_back(std::move(s));
            }
            break;
        }
    }

    LayoutBlock(b);
    return b;
}

void BlockEditor::LayoutBlock(Block* block) {
    if (!block) return;
    float width = 200.0f;
    float height = block->slots.empty()
        ? 50.0f
        : kBlockHeaderHeight + kSlotRowHeight * static_cast<float>(block->slots.size()) + 6.0f;
    block->size = ImVec2(width, height);
}

void BlockEditor::Update() {
    UpdateDragFromPalette();
    UpdateSnapping();
    UpdateSlotTargeting();
    UpdateDragExistingBlock();
    UpdateDeletion();
}

void BlockEditor::UpdateDragFromPalette() {
    if (!m_draggingFromPalette) return;

    if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        const BlockTemplate& tmpl = m_palette[m_paletteDragIndex];
        ImVec2 mouse = ImGui::GetIO().MousePos;

        if (IsSlotOnlyBlockType(tmpl.type)) {
            if (m_slotTargetOwner && m_slotTargetIndex >= 0) {
                Block* spawned = SpawnBlock(tmpl.type, tmpl.label, ImVec2(0, 0));
                PlugIntoSlot(m_slotTargetOwner, m_slotTargetIndex, spawned);
            }
            m_draggingFromPalette = false;
            m_paletteDragIndex = -1;
            m_slotTargetOwner = nullptr;
            m_slotTargetIndex = -1;
            return;
        }

        if (mouse.x > kSidebarWidth) {
            ImVec2 grabOffset = ImVec2(100.0f, 10.0f);
            ImVec2 mouseCanvas = ImVec2(mouse.x - m_canvasScreenOrigin.x, mouse.y - m_canvasScreenOrigin.y);
            ImVec2 dropPos = ImVec2(mouseCanvas.x - grabOffset.x, mouseCanvas.y - grabOffset.y);

            Block* spawned = SpawnBlock(tmpl.type, tmpl.label, dropPos);

            m_draggingBlock = spawned;
            m_dragGrabOffset = grabOffset;
            m_dragJustStarted = false; // freshly spawned
            m_dragWasPluggedIn = false;
        }
        m_draggingFromPalette = false;
        m_paletteDragIndex = -1;
    }
}

void BlockEditor::UpdateDragExistingBlock() {
    if (!m_draggingBlock) return;

    if (ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
        if (m_dragJustStarted) {
            if (m_dragWasPluggedIn) {
                UnplugFromSlot(m_draggingBlock);
            } else {
                DetachFromChain(m_draggingBlock);
            }
            m_dragJustStarted = false;
        }

        ImVec2 mouse = ImGui::GetIO().MousePos;
        ImVec2 mouseCanvas = ImVec2(mouse.x - m_canvasScreenOrigin.x, mouse.y - m_canvasScreenOrigin.y);
        m_draggingBlock->pos = ImVec2(mouseCanvas.x - m_dragGrabOffset.x, mouseCanvas.y - m_dragGrabOffset.y);
    } else {
        if (!m_draggingBlock->IsHead()) {
            if (m_slotTargetOwner && m_slotTargetIndex >= 0) {
                PlugIntoSlot(m_slotTargetOwner, m_slotTargetIndex, m_draggingBlock);
            } else if (m_snapTarget) {
                InsertAfter(m_snapTarget, m_draggingBlock);
            }
        }
        m_snapTarget = nullptr;
        m_slotTargetOwner = nullptr;
        m_slotTargetIndex = -1;
        m_draggingBlock = nullptr;
        m_dragWasPluggedIn = false;
    }
}

void BlockEditor::UpdateSnapping() {
    m_snapTarget = nullptr;
    if (!m_draggingBlock) return;
    if (m_draggingBlock->IsHead()) return; // head never snaps under anything
    if (m_draggingBlock->IsSlotOnly()) return; // slot-only types never join the stack

    // Never allow snapping onto the block currently being dragged, or the
    // head block being treated as anything other than a valid anchor target
    // (head block itself is always a valid snap target since it's the pile root).
    Block* best = nullptr;
    float bestDist = kSnapDistance;

    for (auto& up : m_blocks) {
        Block* candidate = up.get();
        if (candidate == m_draggingBlock) continue;
        if (candidate->IsSlotOnly()) continue; // slot-only types are never stack anchors either
        if (candidate->IsPluggedIn()) continue; // a block sitting in a slot is not a stack tail

        if (IsInChainStartingAt(m_draggingBlock, candidate)) continue;

        if (candidate->next != nullptr) continue;

        ImVec2 candidatePos = EffectivePos(candidate);
        ImVec2 slot = ImVec2(candidatePos.x, candidatePos.y + candidate->size.y);
        ImVec2 dragTop = m_draggingBlock->pos; // dragging block is always a chain root, ->pos is authoritative
        float dist = std::abs(slot.x - dragTop.x) + std::abs(slot.y - dragTop.y);

        if (dist < bestDist) {
            bestDist = dist;
            best = candidate;
        }
    }

    m_snapTarget = best;
}

void BlockEditor::UpdateSlotTargeting() {
    m_slotTargetOwner = nullptr;
    m_slotTargetIndex = -1;

    bool draggingSlotOnlyFromPalette = m_draggingFromPalette &&
        m_paletteDragIndex >= 0 &&
        IsSlotOnlyBlockType(m_palette[m_paletteDragIndex].type);
    bool draggingExistingBlock = (m_draggingBlock != nullptr);

    if (!draggingSlotOnlyFromPalette && !draggingExistingBlock) return;
    if (draggingExistingBlock && m_draggingBlock->IsHead()) return; // head can never be plugged anywhere

    ImVec2 mouse = ImGui::GetIO().MousePos;
    ImVec2 mouseCanvas = ImVec2(mouse.x - m_canvasScreenOrigin.x, mouse.y - m_canvasScreenOrigin.y);

    Block* bestOwner = nullptr;
    int bestIndex = -1;

    for (auto& up : m_blocks) {
        Block* owner = up.get();
        if (draggingExistingBlock) {
            if (owner == m_draggingBlock) continue;
            // Refuse plugging a block into a slot belonging to itself or
            // any of its own descendants (stack or slot) — that would be
            // a cycle. IsDescendantViaSlots covers both axes.
            if (IsDescendantViaSlots(m_draggingBlock, owner)) continue;
        }

        for (size_t i = 0; i < owner->slots.size(); ++i) {
            if (owner->slots[i].plugged != nullptr) continue; // slot already occupied

            ImVec2 ownerPos = EffectivePos(owner);
            ImVec2 rowOffset = SlotRowOffset(owner, static_cast<int>(i));
            ImVec2 fieldMin = ImVec2(ownerPos.x + rowOffset.x, ownerPos.y + rowOffset.y);
            ImVec2 fieldMax = ImVec2(fieldMin.x + kSlotFieldWidth, fieldMin.y + kSlotRowHeight);

            if (mouseCanvas.x >= fieldMin.x && mouseCanvas.x <= fieldMax.x &&
                mouseCanvas.y >= fieldMin.y && mouseCanvas.y <= fieldMax.y) {
                bestOwner = owner;
                bestIndex = static_cast<int>(i);
                break; // mouse can only be over one field at a time
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
        m_slotTargetOwner = nullptr;
        m_slotTargetIndex = -1;
        m_dragJustStarted = false;
        m_dragWasPluggedIn = false;
        DeleteChain(toDelete);
        return;
    }

    if (m_hoveredBlock && !m_hoveredBlock->IsHead()) {
        Block* toDelete = m_hoveredBlock;
        m_hoveredBlock = nullptr;
        DeleteBlock(toDelete);
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
    if (block->IsSlotOnly()) return; // slot-only types never join the stack
    if (block->IsPluggedIn()) return; // must be detached from its slot first (see UnplugFromSlot)

    if (IsInChainStartingAt(block, anchor)) return;

    Block* oldNext = anchor->next;
    anchor->next = block;
    block->prev = anchor;

    // If block already has a tail (dragged a mid-chain segment in), splice
    // the anchor's old tail onto the end of that dragged segment.
    Block* tail = block;
    while (tail->next) tail = tail->next;
    tail->next = oldNext;
    if (oldNext) oldNext->prev = tail;

    block->pos = ImVec2(anchor->pos.x, anchor->pos.y + anchor->size.y);
}

namespace {
template <typename Editor>
void ClearDanglingRefs(Editor* ed, Block* cur, Block*& snapTarget, Block*& hoveredBlock,
                        Block*& draggingBlock, Block*& slotTargetOwner, int& slotTargetIndex) {
    (void)ed;
    if (snapTarget == cur) snapTarget = nullptr;
    if (hoveredBlock == cur) hoveredBlock = nullptr;
    if (draggingBlock == cur) draggingBlock = nullptr;
    if (slotTargetOwner == cur) { slotTargetOwner = nullptr; slotTargetIndex = -1; }
}
} // namespace

void BlockEditor::DeleteBlock(Block* block) {
    if (!block || block->IsHead()) return;

    for (auto& slot : block->slots) {
        if (slot.plugged) {
            Block* child = slot.plugged;
            slot.plugged = nullptr;
            DeleteBlock(child); // child is slot-only, never has a ->next tail to worry about
        }
    }

    // If THIS block was itself plugged into someone else's slot, clear
    // that owner's pointer to it so nothing dangles.
    if (block->slotParent && block->slotParentIndex >= 0 &&
        static_cast<size_t>(block->slotParentIndex) < block->slotParent->slots.size()) {
        block->slotParent->slots[block->slotParentIndex].plugged = nullptr;
    }

    // Reconnect around the single removed block so the rest of its chain
    // (both above and below) stays intact and doesn't get orphaned.
    if (block->prev) block->prev->next = block->next;
    if (block->next) block->next->prev = block->prev;

    ClearDanglingRefs(this, block, m_snapTarget, m_hoveredBlock, m_draggingBlock, m_slotTargetOwner, m_slotTargetIndex);

    m_blocks.erase(
        std::remove_if(m_blocks.begin(), m_blocks.end(),
            [block](const std::unique_ptr<Block>& up) { return up.get() == block; }),
        m_blocks.end());
}

void BlockEditor::DeleteChain(Block* chainStart) {
    if (!chainStart || chainStart->IsHead()) return;

    if (chainStart->prev) chainStart->prev->next = nullptr;

    for (Block* cur = chainStart; cur; ) {
        Block* next = cur->next;

        // Free anything plugged into cur's own slots (recursively — a
        // plugged-in block can itself have further plugged-in blocks).
        for (auto& slot : cur->slots) {
            if (slot.plugged) {
                Block* child = slot.plugged;
                slot.plugged = nullptr;
                DeleteBlock(child);
            }
        }

        ClearDanglingRefs(this, cur, m_snapTarget, m_hoveredBlock, m_draggingBlock, m_slotTargetOwner, m_slotTargetIndex);

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
    if (owner->slots[slotIndex].plugged != nullptr) return; // occupied — caller must unplug/clear first
    if (block->prev != nullptr || block->next != nullptr) return; // must be a detached stack root, not mid-chain
    if (block->IsHead()) return; // head can never be plugged anywhere
    if (block == owner) return;
    if (IsDescendantViaSlots(block, owner)) return; // would create a cycle (owner is reachable from block already)

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
        owner->slots[idx].plugged = nullptr; // reveals owner->slots[idx].text again automatically
    }

    block->slotParent = nullptr;
    block->slotParentIndex = -1;
    block->pos = currentPos;
}

bool BlockEditor::IsDescendantViaSlots(Block* root, Block* candidate) const {
    if (!root || !candidate) return false;
    if (root == candidate) return true;

    // Stack axis: candidate reachable by following ->next from root.
    for (Block* cur = root; cur; cur = cur->next) {
        if (cur == candidate) return true;
        // Slot axis from each stacked block along the way: anything
        // plugged into cur's slots, recursively.
        for (const auto& slot : cur->slots) {
            if (slot.plugged && IsDescendantViaSlots(slot.plugged, candidate)) return true;
        }
    }
    return false;
}

ImVec2 BlockEditor::EffectivePos(const Block* block) const {
    if (!block) return ImVec2(0, 0);

    if (block->slotParent) {
        // Plugged-in child: parent's effective position + that slot's row
        // offset within the parent's body.
        ImVec2 parentPos = EffectivePos(block->slotParent);
        ImVec2 rowOffset = SlotRowOffset(block->slotParent, block->slotParentIndex);
        return ImVec2(parentPos.x + rowOffset.x, parentPos.y + rowOffset.y);
    }

    if (!block->prev) return block->pos; // chain root — ->pos is authoritative

    ImVec2 rootPos = EffectivePos(block->prev);
    return ImVec2(rootPos.x, rootPos.y + block->prev->size.y);
}

ImVec2 BlockEditor::SlotRowOffset(const Block* owner, int slotIndex) const {
    if (!owner || slotIndex < 0) return ImVec2(0, 0);
    float y = kBlockHeaderHeight + kSlotRowHeight * static_cast<float>(slotIndex);
    float x = owner->size.x - kSlotFieldWidth - 10.0f; // right-aligned within the block body, small margin
    return ImVec2(x, y);
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

void BlockEditor::Render() {
    ImGui::Begin("Block Editor");

    DrawSidebar();
    ImGui::SameLine();
    DrawCanvas();

    ImGui::End();
}

void BlockEditor::DrawSidebar() {
    ImGui::BeginChild("palette", ImVec2(kSidebarWidth, 0), true);
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

        if (ImGui::IsItemActivated()) {
            m_draggingFromPalette = true;
            m_paletteDragIndex = i;
        }

        ImGui::PopID();
    }

    ImGui::EndChild();
}

void BlockEditor::DrawCanvas() {
    ImGui::BeginChild("canvas", ImVec2(0, 0), true,
                       ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse);

    ImVec2 canvasOrigin = ImGui::GetCursorScreenPos();
    m_canvasScreenOrigin = canvasOrigin; // cache for Update() to convert mouse screen-pos -> canvas-space
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Reset every frame before re-detecting below. Without this, moving the
    // mouse off every block (e.g. onto empty canvas or the sidebar) would
    // leave last frame's target permanently armed for Delete.
    m_hoveredBlock = nullptr;

    for (auto& up : m_blocks) {
        Block* b = up.get();
        if (b->IsPluggedIn()) continue;
        DrawBlock(dl, canvasOrigin, b, /*isDragGhost=*/false);
    }

    if (m_draggingBlock && m_snapTarget) {
        ImVec2 slotPos = GetChainSlotScreenPos(canvasOrigin, m_snapTarget);
        ImVec2 slotMax = ImVec2(slotPos.x + m_snapTarget->size.x, slotPos.y + 6.0f);
        dl->AddRectFilled(slotPos, slotMax, IM_COL32(255, 220, 80, 200), 3.0f);
    }

    if (m_slotTargetOwner && m_slotTargetIndex >= 0) {
        ImVec2 ownerPos = EffectivePos(m_slotTargetOwner);
        ImVec2 rowOffset = SlotRowOffset(m_slotTargetOwner, m_slotTargetIndex);
        ImVec2 fieldMin = ImVec2(canvasOrigin.x + ownerPos.x + rowOffset.x, canvasOrigin.y + ownerPos.y + rowOffset.y);
        ImVec2 fieldMax = ImVec2(fieldMin.x + kSlotFieldWidth, fieldMin.y + kSlotRowHeight);
        dl->AddRect(fieldMin, fieldMax, IM_COL32(255, 220, 80, 255), 4.0f, 0, 3.0f);
    }

    ImGui::SetCursorScreenPos(canvasOrigin);
    ImGui::InvisibleButton("canvas_bg", ImGui::GetContentRegionAvail());

    ImGui::EndChild();
}

void BlockEditor::DrawBlock(ImDrawList* dl, ImVec2 canvasOrigin, Block* block, bool isDragGhost) {
    ImVec2 effPos = EffectivePos(block);
    ImVec2 blockMin = ImVec2(canvasOrigin.x + effPos.x, canvasOrigin.y + effPos.y);
    ImVec2 blockMax = ImVec2(blockMin.x + block->size.x, blockMin.y + block->size.y);

    ImU32 bodyColor = block->IsHead() ? IM_COL32(200, 90, 90, 255)
                     : block->IsSlotOnly() ? IM_COL32(150, 100, 200, 255)
                     : IM_COL32(90, 140, 210, 255);
    ImU32 borderColor = block->IsHead() ? IM_COL32(120, 40, 40, 255)
                       : block->IsSlotOnly() ? IM_COL32(90, 60, 130, 255)
                       : IM_COL32(40, 70, 110, 255);

    float rounding = block->IsSlotOnly() ? (block->size.y * 0.5f) : 8.0f; // pill shape for slot-only blocks
    dl->AddRectFilled(blockMin, blockMax, bodyColor, rounding);
    dl->AddRect(blockMin, blockMax, borderColor, rounding, 0, 2.0f);
    dl->AddText(ImVec2(blockMin.x + 12, blockMin.y + kBlockHeaderHeight * 0.5f - 8),
                IM_COL32(255, 255, 255, 255), block->label.c_str());

    // Draw each field row: label + either the literal text box or, if
    // something's plugged in, that block drawn inline (recursively).
    for (size_t i = 0; i < block->slots.size(); ++i) {
        DrawSlot(dl, canvasOrigin, block, static_cast<int>(i));
    }

    ImVec2 headerMin = blockMin;
    ImVec2 headerSize = ImVec2(block->size.x, block->slots.empty() ? block->size.y : kBlockHeaderHeight);
    ImGui::SetCursorScreenPos(headerMin);
    ImGui::PushID((int)block->id);
    ImGui::InvisibleButton("block_drag", headerSize);

    if (ImGui::IsItemHovered()) {
        m_hoveredBlock = block;
    }

    if (ImGui::IsItemActivated() && !m_draggingBlock && !m_draggingFromPalette) {
        m_draggingBlock = block;
        m_dragJustStarted = true;
        m_dragWasPluggedIn = block->IsPluggedIn();
        ImVec2 mouse = ImGui::GetIO().MousePos;
        m_dragGrabOffset = ImVec2(mouse.x - blockMin.x, mouse.y - blockMin.y);
    }
    ImGui::PopID();
}

void BlockEditor::DrawSlot(ImDrawList* dl, ImVec2 canvasOrigin, Block* owner, int slotIndex) {
    Slot& slot = owner->slots[slotIndex];
    ImVec2 ownerPos = EffectivePos(owner);
    ImVec2 rowOffset = SlotRowOffset(owner, slotIndex);
    ImVec2 rowMin = ImVec2(canvasOrigin.x + ownerPos.x + rowOffset.x, canvasOrigin.y + ownerPos.y + rowOffset.y);

    // Field name label, to the left of the field area.
    ImVec2 labelPos = ImVec2(canvasOrigin.x + ownerPos.x + 12, rowMin.y + kSlotRowHeight * 0.5f - 8);
    dl->AddText(labelPos, IM_COL32(230, 230, 230, 255), slot.name.c_str());

    if (slot.plugged) {
        // Something is plugged in, draw it inline, recursively. Its own
        // EffectivePos() resolves through this owner+slot automatically.
        DrawBlock(dl, canvasOrigin, slot.plugged, /*isDragGhost=*/false);
        return;
    }

    // Nothing plugged in, draw an editable literal text field.
    ImVec2 fieldMax = ImVec2(rowMin.x + kSlotFieldWidth, rowMin.y + kSlotRowHeight - 4.0f);
    dl->AddRectFilled(rowMin, fieldMax, IM_COL32(255, 255, 255, 235), 4.0f);
    dl->AddRect(rowMin, fieldMax, IM_COL32(60, 60, 60, 255), 4.0f, 0, 1.0f);

    ImGui::SetCursorScreenPos(rowMin);
    ImGui::PushID(static_cast<int>(owner->id));
    ImGui::PushID(slotIndex);
    ImGui::SetNextItemWidth(kSlotFieldWidth);

    char buf[256];
    std::snprintf(buf, sizeof(buf), "%s", slot.text.c_str());
    ImGui::PushStyleColor(ImGuiCol_FrameBg, IM_COL32(0, 0, 0, 0)); // we already painted the field bg above
    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(20, 20, 20, 255));
    if (ImGui::InputText("##field", buf, sizeof(buf))) {
        slot.text = buf;
    }
    ImGui::PopStyleColor(2);

    ImGui::PopID();
    ImGui::PopID();
}

ImVec2 BlockEditor::GetChainSlotScreenPos(ImVec2 canvasOrigin, Block* anchor) const {
    ImVec2 effPos = EffectivePos(anchor);
    return ImVec2(canvasOrigin.x + effPos.x, canvasOrigin.y + effPos.y + anchor->size.y);
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
            uint8_t hasPlugged = slot.plugged ? 1 : 0;
            WritePod<uint8_t>(out, hasPlugged);
            WritePod<uint64_t>(out, slot.plugged ? slot.plugged->id : 0);
        }
    }

    return out;
}

void BlockEditor::Cleanup() {
    m_blocks.clear();
    m_headBlock = nullptr;
    m_draggingBlock = nullptr;
    m_snapTarget = nullptr;
    m_slotTargetOwner = nullptr;
    m_slotTargetIndex = -1;
    m_hoveredBlock = nullptr;
    m_draggingFromPalette = false;
    m_paletteDragIndex = -1;
    m_dragJustStarted = false;
    m_dragWasPluggedIn = false;
}

bool BlockEditor::ImportBlocks(const std::vector<uint8_t>& data) {
    std::vector<ParsedBlock> parsed;
    uint64_t headId = 0;
    if (!ParseBlockBlob(data, parsed, headId)) return false;

    // Everything validated by ParseBlockBlob — now, and only now, replace
    // the live pile. Drag/snap/slot-target/hover state is cleared up front
    // since it holds raw pointers into the pile we're about to destroy.
    Cleanup();

    // Pass 1: construct every Block with its scalar fields and slot list
    // (literal text only for now — plugged pointers are wired in pass 2,
    // once every Block exists), id -> pointer lookup built alongside so
    // later passes can resolve next/prev/plugged links.
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
            // s.plugged wired in pass 2
            b->slots.push_back(std::move(s));
        }

        idToBlock.emplace_back(pb.id, b);
    }

    auto resolve = [&](uint64_t id) -> Block* {
        if (id == 0) return nullptr;
        for (auto& kv : idToBlock) if (kv.first == id) return kv.second;
        return nullptr; // unreachable given ParseBlockBlob's validation, but stay safe
    };

    // Pass 2: wire up next/prev now that every Block exists.
    for (size_t i = 0; i < parsed.size(); ++i) {
        Block* b = idToBlock[i].second;
        b->next = resolve(parsed[i].nextId);
        b->prev = resolve(parsed[i].prevId);
    }

    // Pass 3: wire up slot plug pointers + the plugged block's back-links
    // (slotParent/slotParentIndex). Done after pass 2 so every Block* is
    // valid to reference regardless of declaration order in the blob.
    for (size_t i = 0; i < parsed.size(); ++i) {
        Block* b = idToBlock[i].second;
        for (size_t s = 0; s < parsed[i].slots.size(); ++s) {
            const auto& ps = parsed[i].slots[s];
            if (!ps.hasPlugged) continue;
            Block* child = resolve(ps.pluggedId);
            if (!child) continue; // unreachable given validation, but stay safe
            b->slots[s].plugged = child;
            child->slotParent = b;
            child->slotParentIndex = static_cast<int>(s);
        }
    }

    m_headBlock = resolve(headId);

    // Keep future NextId() calls from ever colliding with an imported id.
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
        if (ps.hasPlugged) {
            const ParsedBlock* child = findById(ps.pluggedId);
            if (child) {
                field.plugged = std::make_unique<BlockInfo>(BuildBlockInfo(*child, findById));
            }
        }
        info.fields.push_back(std::move(field));
    }

    return info;
}
} // namespace

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
    if (!head) return false; // ParseBlockBlob already guarantees this, but stay safe

    for (const ParsedBlock* cur = head; cur; cur = findById(cur->nextId)) {
        visit(BuildBlockInfo(*cur, findById));
    }

    return true;
}