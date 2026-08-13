#include "blockeditor.h"
#include <sstream>
#include <iostream> 

std::string getFieldValue(const BlockInfo& block, const std::string& fieldName) {
    for(const auto& field : block.fields) {
        if(field.name != fieldName) continue;
        if(field.plugged) {
            throw std::runtime_error("Not implemented");
        } else {
            return field.text;
        }
    }
    return "";
}

std::string GenerateCode(const std::vector<uint8_t>& blocks) {
    std::stringstream ss; // for output

    BlockEditor::WalkBlockBlob(blocks, [&ss](BlockInfo b) {
        switch (b.type) {
            case BlockType::HeadBlock:
                ss << "# Generated from blocks\n";
                break;
            case BlockType::MoveForward:
                {
                    auto text = getFieldValue(b, "value");
                    auto valueOpt = ToFloat(text);
                    if(valueOpt) {
                        ss << "moveForward " << text << "\n";
                    }
                    break;
                }
            case BlockType::WaitUntilGround:
                {
                    ss << "waitUntilGround\n";
                    break;
                }
            default:
                std::cout << "Unknown\n";
                break;
        }
    });

    std::cout << "Code gen out:\n";
    std::cout << ss.str() << std::endl;

    return ss.str();
}