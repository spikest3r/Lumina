#include "blockeditor.h"
#include <sstream>
#include <iostream> 

enum class ValueType {
    TAG_STRING,
    TAG_NUMERIC
};

ValueType detectType(const std::string& s) {
    if (s.empty()) return ValueType::TAG_STRING;

    size_t i = 0;
    bool neg = (s[0] == '-' || s[0] == '+');
    if (neg) i = 1;

    if (i >= s.size()) return ValueType::TAG_STRING;

    bool hasDigits = false;
    bool hasDot = false;

    for (; i < s.size(); ++i) {
        char c = s[i];
        if (std::isdigit(static_cast<unsigned char>(c))) {
            hasDigits = true;
        } else if (c == '.' && !hasDot) {
            hasDot = true;
        } else {
            return ValueType::TAG_STRING;
        }
    }

    return hasDigits ? ValueType::TAG_NUMERIC : ValueType::TAG_STRING;
}

bool looksLikeNumber(const std::string& s) {
    if (s.empty()) return false;

    size_t i = 0;
    if (s[0] == '-' || s[0] == '+') i = 1;

    return i < s.size() && std::isdigit(static_cast<unsigned char>(s[i]));
}

std::string getFieldValue(const BlockInfo& block, int fieldID, bool format = true) {
    const auto& field = block.fields[fieldID];
    if(field.plugged) {
        switch(field.plugged->type) {
            case BlockType::Variable:
                auto varName = field.plugged->fields[0].text;
                // TODO: check if starts with digit and error
                return varName;
        }
    } else {
        if(format) {
            auto type = detectType(field.text);
            switch(type) {
                case ValueType::TAG_STRING:
                    return "'" + field.text + "'";
                case ValueType::TAG_NUMERIC:
                    return field.text;
                default:
                    return "";
            }
        } else {
            return field.text;
        }
    }
    return "";
}

struct CodeGenData {
    std::stringstream ss;
    int tempCounter = 0;
};

inline std::string getTempLabel(int& counter) {
    return "temp_" + std::to_string(counter++);
}

void GenerateBlockCode(const BlockInfo& b, CodeGenData& codeGenData) {
    auto& ss = codeGenData.ss;
    auto& tempCounter = codeGenData.tempCounter;

    switch (b.type) {
        case BlockType::HeadBlock:
            ss << "# Generated from blocks\n";
            break;
        case BlockType::MoveForward:
            {
                auto text = getFieldValue(b, 0);
                auto valueOpt = ToFloat(text);
                if(valueOpt) {
                    ss << "moveForward " << text << "\n";
                }
                break;
            }
        case BlockType::GoToPos:
            {
                auto x = getFieldValue(b, 0);
                auto y = getFieldValue(b, 1);
                ss << "goToPos " << x << ", " << y << "\n";
                break;
            }
        case BlockType::WaitUntilGround:
            {
                ss << "waitUntilGround\n";
                break;
            }
        case BlockType::SetVariable:
            {
                auto name = getFieldValue(b, 0, false); // TODO: check if starts with digit and error
                auto value = getFieldValue(b, 1);
                ss << name << " = " << value << "\n";
                break;
            }
        case BlockType::SayText:
            {
                auto text = getFieldValue(b, 0);
                ss << "println " << text << "\n";
                break;
            }
        case BlockType::Forever:
            {
                auto myLabel = getTempLabel(tempCounter); // get label for this block
                ss << "label " << myLabel << "\n"; // define label
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                }
                ss << "jump " << myLabel << "\n"; // jump to defined label
                break;
            }
        default:
            std::cout << "Unknown\n";
            break;
    }
}

std::string GenerateCode(const std::vector<uint8_t>& blocks) {
    CodeGenData codeGenData;

    BlockEditor::WalkBlockBlob(blocks, [&codeGenData](BlockInfo b) {
        GenerateBlockCode(b, codeGenData);
    });

    auto sc = codeGenData.ss.str();

    std::cout << "Code gen out:\n";
    std::cout << sc << std::endl;

    return sc;
}