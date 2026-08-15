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

struct CodeGenData {
    std::stringstream ss;
    int tempCounter = 0;
    int depth = 0;
};

inline std::string getTempLabel(int& counter) {
    return "temp_" + std::to_string(counter++);
}

std::string getFieldValue(const BlockInfo& block, int fieldID, CodeGenData& data, bool format = true) {
    const auto& field = block.fields[fieldID];
    const std::string idStr = std::to_string(fieldID);
    if(field.plugged) {
        auto getBinaryOp = [&](const std::string& op) {
            auto a = getFieldValue(*field.plugged, 0, data);
            auto b = getFieldValue(*field.plugged, 1, data);
            return a + " " + op + " " + b;
        };
        switch(field.plugged->type) {
            case BlockType::Variable:
                {
                    auto varName = field.plugged->fields[0].text;
                    // TODO: check if starts with digit and error
                    return varName;
                }
            case BlockType::Ask:
                {
                    auto text = field.plugged->fields[0].text;
                    data.ss << "ask \'" << text << "\', &tempAsk" + idStr << "\n";
                    return "tempAsk" + idStr;
                }
            case BlockType::IsKeyDown:
                {
                    data.ss << "isKeyDown \'" << field.plugged->fields[0].text << "\', &flagKeyDown" + idStr << "\n";
                    return "flagKeyDown" + idStr + " == 1";
                }
            case BlockType::IsObstacleAhead:
                {
                    data.ss << "isObstacleAhead " << "&flagObstacleAhead" + idStr << "\n";
                    return "flagObstacleAhead" + idStr + " == 1";
                }
            case BlockType::IsTouching:
                {
                    data.ss << "isTouching \'" << field.plugged->fields[0].text << "\', &flagIsTouching" + idStr << "\n";
                    return "flagIsTouching" + idStr + " == 1";
                }
            case BlockType::RandomRange:
                {
                    auto min = getFieldValue(*field.plugged, 0, data);
                    auto max = getFieldValue(*field.plugged, 1, data);
                    data.ss << "randomRange " << min << ", " << max << ", &tempRandom" + idStr << "\n";
                    return "tempRandom" + idStr;
                }
            case BlockType::MathAdd:  return getBinaryOp("+");
            case BlockType::MathSub:  return getBinaryOp("-");
            case BlockType::MathMul:  return getBinaryOp("*");
            case BlockType::MathDiv:  return getBinaryOp("/");
            case BlockType::LogicLess: return getBinaryOp("<");
            case BlockType::LogicGreater: return getBinaryOp(">");
            case BlockType::LogicEqual: return getBinaryOp("==");
            case BlockType::LogicNotEqual: return getBinaryOp("!=");
            case BlockType::LogicLessEqual: return getBinaryOp("<=");
            case BlockType::LogicGreaterEqual: return getBinaryOp(">=");
            case BlockType::LogicNot:
                {
                    auto condition = getFieldValue(*field.plugged, 0, data);
                    data.ss << "if " << condition << "\n";
                    data.ss << "flagNot" + idStr << " = 0";
                    data.ss << "else\n";
                    data.ss << "flagNot" + idStr << " = 1\n";
                    data.ss << "endif\n";
                    return "flagNot" + idStr + " == 1";
                }
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

void GenerateBlockCode(const BlockInfo& b, CodeGenData& codeGenData) {
    auto& ss = codeGenData.ss;
    auto& tempCounter = codeGenData.tempCounter;
    auto& depth = codeGenData.depth;

    switch (b.type) {
        case BlockType::HeadBlock:
            ss << "# Generated from blocks\n";
            break;
        case BlockType::MoveForward:
            {
                auto text = getFieldValue(b, 0, codeGenData);
                auto valueOpt = ToFloat(text);
                ss << "moveForward " << text << "\n";
                break;
            }
        case BlockType::GoToPos:
            {
                auto x = getFieldValue(b, 0, codeGenData);
                auto y = getFieldValue(b, 1, codeGenData);
                ss << "goToPos " << x << ", " << y << "\n";
                break;
            }
        case BlockType::SetRot:
            {
                auto x = getFieldValue(b, 0, codeGenData);
                auto y = getFieldValue(b, 1, codeGenData);
                auto z = getFieldValue(b, 2, codeGenData);
                ss << "setRot " << x << ", " << y << ", " << z << "\n";
                break;
            }
        case BlockType::WaitUntilGround:
            {
                ss << "waitUntilGround\n";
                break;
            }
        case BlockType::SetVariable:
            {
                auto name = getFieldValue(b, 0, codeGenData, false); // TODO: check if starts with digit and error
                auto value = getFieldValue(b, 1, codeGenData);
                ss << name << " = " << value << "\n";
                break;
            }
        case BlockType::SayText:
            {
                auto text = getFieldValue(b, 0, codeGenData);
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
        case BlockType::Repeat:
            {
                auto counterLabel = "cnt_" + std::to_string(depth++);
                auto myLabel = getTempLabel(tempCounter); // get label for this block
                auto times = getFieldValue(b, 0, codeGenData);
                ss << counterLabel << " = 0\n";
                ss << "label " << myLabel << "\n"; // define label
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                }
                ss << counterLabel << " = " << counterLabel << " + 1\n";
                ss << "if " << counterLabel << " < " << times << "\n";
                ss << "jump " << myLabel << "\n";
                ss << "endif\n";
                depth--;
                break;
            }
        case BlockType::If:
            {
                auto condition = getFieldValue(b, 0, codeGenData);
                ss << "if " << condition << "\n";
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                }
                ss << "endif\n";
                break;
            }
        case BlockType::IfElse:
            {
                auto condition = getFieldValue(b, 0, codeGenData);
                ss << "if " << condition << "\n";
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                }
                ss << "else\n";
                for(const BlockInfo& childInfo : b.subStacks[1]) {
                    GenerateBlockCode(childInfo, codeGenData);
                }
                ss << "endif\n";
                break;
            }
        case BlockType::Wait:
            {
                auto delay = getFieldValue(b, 0, codeGenData);
                ss << "waitMs " << delay << "\n";
                break;
            }
        case BlockType::DestroySelf:
            {
                ss << "destroySelf\n";
                break;
            }
        case BlockType::StopAll:
            {
                ss << "stopAll\n";
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