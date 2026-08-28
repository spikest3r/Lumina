#include "blockeditor.h"
#include <sstream>
#include <iostream>
#include <unordered_map>
#include <cctype>
#include <algorithm>
#include <optional>

enum class ValueType {
    TAG_STRING,
    TAG_NUMERIC
};

enum class ExprType {
    Unknown,
    Numeric,
    String,
    Boolean
};

struct ExprResult {
    std::string code;
    ExprType type;
};

struct CodeGenData {
    std::stringstream ss;
    int tempCounter = 0;
    int depth = 0;
    bool hasError = false;
    std::string errorMessage;
    uint64_t errorBlockId = 0;
    std::unordered_map<std::string, ExprType> varTypes;
};

void SetError(CodeGenData& data, uint64_t blockId, const std::string& msg) {
    if (!data.hasError) {
        data.hasError = true;
        data.errorMessage = msg;
        data.errorBlockId = blockId;
    }
}

bool IsValidVariableName(const std::string& name) {
    if (name.empty()) return false;
    if (std::isdigit(static_cast<unsigned char>(name[0]))) return false;
    for (char c : name) {
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '_') return false;
    }
    return true;
}

std::string EscapeString(const std::string& str) {
    std::string res;
    for (char c : str) {
        if (c == '\'') res += "\\'";
        else if (c == '\\') res += "\\\\";
        else res += c;
    }
    return res;
}

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

inline std::string getTempLabel(int& counter) {
    return "temp_" + std::to_string(counter++);
}

ExprResult getFieldValue(const BlockInfo& parentBlock, int fieldID, CodeGenData& data, std::stringstream* altStream = nullptr) {
    if (data.hasError) return {"", ExprType::Unknown};
    const auto& field = parentBlock.fields[fieldID];
    const std::string idStr = std::to_string(fieldID) + "_" + std::to_string(parentBlock.id);

    std::stringstream& stream = altStream ? *altStream : data.ss;

    if (field.plugged) {
        auto& pb = *field.plugged;
        switch (pb.type) {
            case BlockType::Variable: {
                auto varName = pb.fields[0].text;
                if (!IsValidVariableName(varName)) {
                    SetError(data, pb.id, "Invalid variable name: '" + varName + "'");
                    return {"", ExprType::Unknown};
                }
                ExprType vType = ExprType::Unknown;
                if (data.varTypes.count(varName)) vType = data.varTypes[varName];
                return {varName, vType};
            }
            case BlockType::GetGlobal: {
                auto varName = pb.fields[0].text;
                if (!IsValidVariableName(varName)) {
                    SetError(data, pb.id, "Invalid global name: '" + varName + "'");
                    return {"", ExprType::Unknown};
                }
                auto str = "tempGlobalRead" + idStr;
                stream << "readGlobal \'" << varName << "\', &" << str << "\n";
                ExprType vType = ExprType::Unknown;
                if (data.varTypes.count("g_" + varName)) vType = data.varTypes["g_" + varName];
                return {str, vType};
            }
            case BlockType::Ask: {
                auto textRes = getFieldValue(pb, 0, data);
                if (data.hasError) return {"", ExprType::Unknown};
                stream << "ask " << textRes.code << ", &tempAsk" << idStr << "\n";
                return {"tempAsk" + idStr, ExprType::Unknown};
            }
            case BlockType::IsKeyDown: {
                auto keyRes = getFieldValue(pb, 0, data);
                if (data.hasError) return {"", ExprType::Unknown};
                stream << "isKeyDown " << keyRes.code << ", &flagKeyDown" << idStr << "\n";
                return {"flagKeyDown" + idStr + " == 1", ExprType::Boolean};
            }
            case BlockType::IsObstacleAhead: {
                stream << "isObstacleAhead &flagObstacleAhead" << idStr << "\n";
                return {"flagObstacleAhead" + idStr + " == 1", ExprType::Boolean};
            }
            case BlockType::IsTouching: {
                auto objRes = getFieldValue(pb, 0, data);
                if (data.hasError) return {"", ExprType::Unknown};
                stream << "isTouching " << objRes.code << ", &flagIsTouching" << idStr << "\n";
                return {"flagIsTouching" + idStr + " == 1", ExprType::Boolean};
            }
            case BlockType::RandomRange: {
                auto minRes = getFieldValue(pb, 0, data);
                auto maxRes = getFieldValue(pb, 1, data);
                if (data.hasError) return {"", ExprType::Unknown};
                if (minRes.type == ExprType::String || maxRes.type == ExprType::String) {
                    SetError(data, pb.id, "RandomRange requires numbers, got strings.");
                    return {"", ExprType::Unknown};
                }
                stream << "randomRange " << minRes.code << ", " << maxRes.code << ", &tempRandom" << idStr << "\n";
                return {"tempRandom" + idStr, ExprType::Numeric};
            }
            case BlockType::MathAdd:
            case BlockType::MathSub:
            case BlockType::MathMul:
            case BlockType::MathDiv: {
                auto a = getFieldValue(pb, 0, data);
                auto b = getFieldValue(pb, 1, data);
                if (data.hasError) return {"", ExprType::Unknown};
                if (a.type == ExprType::String || b.type == ExprType::String) {
                    SetError(data, pb.id, "Math operations require numbers.");
                    return {"", ExprType::Unknown};
                }
                if (pb.type == BlockType::MathDiv && b.code == "0") {
                    SetError(data, pb.id, "Division by zero.");
                    return {"", ExprType::Unknown};
                }
                std::string op = (pb.type == BlockType::MathAdd) ? "+" :
                                 (pb.type == BlockType::MathSub) ? "-" :
                                 (pb.type == BlockType::MathMul) ? "*" : "/";
                return {a.code + " " + op + " " + b.code, ExprType::Numeric};
            }
            case BlockType::LogicLess:
            case BlockType::LogicGreater:
            case BlockType::LogicEqual:
            case BlockType::LogicNotEqual:
            case BlockType::LogicLessEqual:
            case BlockType::LogicGreaterEqual: {
                auto a = getFieldValue(pb, 0, data);
                auto b = getFieldValue(pb, 1, data);
                if (data.hasError) return {"", ExprType::Unknown};
                if ((a.type == ExprType::String && b.type == ExprType::Numeric) ||
                    (a.type == ExprType::Numeric && b.type == ExprType::String)) {
                    SetError(data, pb.id, "Cannot mix string and number in comparison.");
                    return {"", ExprType::Unknown};
                }
                std::string op = (pb.type == BlockType::LogicLess) ? "<" :
                                 (pb.type == BlockType::LogicGreater) ? ">" :
                                 (pb.type == BlockType::LogicEqual) ? "==" :
                                 (pb.type == BlockType::LogicNotEqual) ? "!=" :
                                 (pb.type == BlockType::LogicLessEqual) ? "<=" : ">=";
                return {a.code + " " + op + " " + b.code, ExprType::Boolean};
            }
            case BlockType::LogicNot: {
                auto cond = getFieldValue(pb, 0, data);
                if (data.hasError) return {"", ExprType::Unknown};
                stream << "if " << cond.code << "\n";
                stream << "flagNot" << idStr << " = 0\n";
                stream << "else\n";
                stream << "flagNot" << idStr << " = 1\n";
                stream << "endif\n";
                return {"flagNot" + idStr + " == 1", ExprType::Boolean};
            }
            case BlockType::Concat: {
                auto str1 = getFieldValue(pb, 0, data);
                auto str2 = getFieldValue(pb, 1, data);
                if (data.hasError) return {"", ExprType::Unknown};
                stream << "tempConcat" << idStr << " = " << str1.code << " .. " << str2.code << "\n";
                return {"tempConcat" + idStr, ExprType::String};
            }
            default:
                return {"", ExprType::Unknown};
        }
    } else {
        auto detected = detectType(field.text);
        
        if (field.allowedType == SlotType::Number && detected != ValueType::TAG_NUMERIC) {
            SetError(data, parentBlock.id, "Expected number in field '" + field.name + "'.");
            return {"", ExprType::Unknown};
        }
        
        if (detected == ValueType::TAG_NUMERIC) {
            return {field.text, ExprType::Numeric};
        } else {
            return {"'" + EscapeString(field.text) + "'", ExprType::String};
        }
    }
}

void GenerateBlockCode(const BlockInfo& b, CodeGenData& codeGenData) {
    if (codeGenData.hasError) return;
    auto& ss = codeGenData.ss;
    auto& tempCounter = codeGenData.tempCounter;
    auto& depth = codeGenData.depth;

    switch (b.type) {
        case BlockType::HeadBlock:
            ss << "# Main entry point\n";
            break;
        case BlockType::HeadRoutine:
            {
                auto name = b.fields[0].text;
                if (!IsValidVariableName(name)) {
                    SetError(codeGenData, b.id, "Invalid routine name: '" + name + "'");
                    return;
                }
                ss << "# Routine " << name << "\n";
                ss << "routine " << name << "\n";
                break;
            }
        case BlockType::ExecuteRoutine:
            {
                auto name = b.fields[0].text;
                if (!IsValidVariableName(name)) {
                    SetError(codeGenData, b.id, "Invalid routine name: '" + name + "'");
                    return;
                }
                ss << "call " << name << "\n";
                break;
            }
        case BlockType::EndRoutine:
            {
                ss << "endroutine\n";
                break;
            }
        case BlockType::MoveForward:
            {
                auto res = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                if (res.type == ExprType::String) {
                    SetError(codeGenData, b.id, "MoveForward requires a numeric value");
                    return;
                }
                ss << "moveForward " << res.code << "\n";
                break;
            }
        case BlockType::UpwardForce:
            {
                auto res = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                if (res.type == ExprType::String) {
                    SetError(codeGenData, b.id, "UpwardForce requires a numeric value");
                    return;
                }
                ss << "upwardForce " << res.code << "\n";
                break;
            }
        case BlockType::GoToPos:
            {
                auto x = getFieldValue(b, 0, codeGenData);
                auto y = getFieldValue(b, 1, codeGenData);
                if (codeGenData.hasError) return;
                if (x.type == ExprType::String || y.type == ExprType::String) {
                    SetError(codeGenData, b.id, "GoToPos requires numeric coordinates");
                    return;
                }
                ss << "goToPos " << x.code << ", " << y.code << "\n";
                break;
            }
        case BlockType::SetPos:
            {
                // setting own position
                auto x = getFieldValue(b, 0, codeGenData);
                auto y = getFieldValue(b, 1, codeGenData);
                auto z = getFieldValue(b, 2, codeGenData);
                if (codeGenData.hasError) return;
                if (x.type == ExprType::String || y.type == ExprType::String || z.type == ExprType::String) {
                    SetError(codeGenData, b.id, "SetPos requires numeric coordinates");
                    return;
                }
                ss << "setPos " << x.code << ", " << y.code << ", " << z.code << "\n";
                break;
            }
        case BlockType::SetOtherPos:
            {
                // setting other object position
                auto x = getFieldValue(b, 0, codeGenData);
                auto y = getFieldValue(b, 1, codeGenData);
                auto z = getFieldValue(b, 2, codeGenData);
                auto object = getFieldValue(b, 3, codeGenData);
                if (codeGenData.hasError) return;
                if (x.type == ExprType::String || y.type == ExprType::String || z.type == ExprType::String) {
                    SetError(codeGenData, b.id, "SetPos requires numeric coordinates");
                    return;
                }
                ss << "setOtherPos " << x.code << ", " << y.code << ", " << z.code << ", " << object.code << "\n";
                break;
            }
        case BlockType::SetRot:
            {
                auto x = getFieldValue(b, 0, codeGenData);
                auto y = getFieldValue(b, 1, codeGenData);
                auto z = getFieldValue(b, 2, codeGenData);
                if (codeGenData.hasError) return;
                if (x.type == ExprType::String || y.type == ExprType::String || z.type == ExprType::String) {
                    SetError(codeGenData, b.id, "SetRot requires numeric angles");
                    return;
                }
                ss << "setRot " << x.code << ", " << y.code << ", " << z.code << "\n";
                break;
            }
        case BlockType::WaitUntilGround:
            {
                ss << "waitUntilGround\n";
                break;
            }
        case BlockType::SetVariable:
            {
                auto name = b.fields[0].text; 
                if (!IsValidVariableName(name)) {
                    SetError(codeGenData, b.id, "Invalid variable name: '" + name + "'");
                    return;
                }
                auto valRes = getFieldValue(b, 1, codeGenData);
                if (codeGenData.hasError) return;
                codeGenData.varTypes[name] = valRes.type;
                ss << name << " = " << valRes.code << "\n";
                break;
            }
        case BlockType::SetGlobal:
            {
                auto name = b.fields[0].text; 
                if (!IsValidVariableName(name)) {
                    SetError(codeGenData, b.id, "Invalid global name: '" + name + "'");
                    return;
                }
                auto valRes = getFieldValue(b, 1, codeGenData);
                if (codeGenData.hasError) return;
                codeGenData.varTypes["g_" + name] = valRes.type;
                ss << "writeGlobal \'" << name << "\', " << valRes.code << "\n";
                break;
            }
        case BlockType::HideGlobal:
            {
                auto name = b.fields[0].text; 
                if (!IsValidVariableName(name)) {
                    SetError(codeGenData, b.id, "Invalid global name: '" + name + "'");
                    return;
                }
                if (codeGenData.hasError) return;
                ss << "hideGlobal \'" << name << "\'\n";
                break;
            }
        case BlockType::ShowGlobal:
            {
                auto name = b.fields[0].text; 
                if (!IsValidVariableName(name)) {
                    SetError(codeGenData, b.id, "Invalid global name: '" + name + "'");
                    return;
                }
                if (codeGenData.hasError) return;
                ss << "hideGlobal \'" << name << "\'\n";
                break;
            }
        case BlockType::SayText:
            {
                auto textRes = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                ss << "println " << textRes.code << "\n";
                break;
            }
        case BlockType::Forever:
            {
                auto myLabel = getTempLabel(tempCounter); 
                ss << "label " << myLabel << "\n"; 
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                    if (codeGenData.hasError) return;
                }
                ss << "jump " << myLabel << "\n"; 
                break;
            }
        case BlockType::Repeat:
            {
                auto timesRes = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                if (timesRes.type == ExprType::String) {
                    SetError(codeGenData, b.id, "Repeat requires a numeric count");
                    return;
                }
                ss << "repeat " << timesRes.code << "\n";
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                    if (codeGenData.hasError) return;
                }
                ss << "endrepeat\n";
                depth--;
                break;
            }
        case BlockType::While:
            {
                std::stringstream altStream;
                auto condRes = getFieldValue(b, 0, codeGenData, &altStream);
                if (codeGenData.hasError) return;
                ss << "# prefetch\n" << altStream.str();
                ss << "while " << condRes.code << "\n";
                ss << altStream.str(); // FIX: duplicated condition fetch below while itself to avoid stale value
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                    if (codeGenData.hasError) return;
                }
                ss << "endwhile\n";
                break;
            }
        case BlockType::If:
            {
                auto condRes = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                ss << "if " << condRes.code << "\n";
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                    if (codeGenData.hasError) return;
                }
                ss << "endif\n";
                break;
            }
        case BlockType::IfElse:
            {
                auto condRes = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                ss << "if " << condRes.code << "\n";
                for(const BlockInfo& childInfo : b.subStacks[0]) {
                    GenerateBlockCode(childInfo, codeGenData);
                    if (codeGenData.hasError) return;
                }
                ss << "else\n";
                for(const BlockInfo& childInfo : b.subStacks[1]) {
                    GenerateBlockCode(childInfo, codeGenData);
                    if (codeGenData.hasError) return;
                }
                ss << "endif\n";
                break;
            }
        case BlockType::Wait:
            {
                auto delayRes = getFieldValue(b, 0, codeGenData);
                if (codeGenData.hasError) return;
                if (delayRes.type == ExprType::String) {
                    SetError(codeGenData, b.id, "Wait requires a numeric duration");
                    return;
                }
                ss << "waitMs " << delayRes.code << " * 1000\n";
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

std::optional<std::string> GenerateCode(const std::vector<uint8_t>& blocks, std::string* outError, uint64_t* outErrorBlockId) {
    CodeGenData codeGenData;

    codeGenData.ss << "# Generated from blocks\n";

    if(blocks.size() > 0) {
        if (!BlockEditor::WalkBlockBlob(blocks, [&codeGenData](BlockInfo b) {
            if (!codeGenData.hasError) {
                GenerateBlockCode(b, codeGenData);
            }
        })) {
            if (outError) *outError = "Failed to parse block data.";
            return std::nullopt;
        }

        if (codeGenData.hasError) {
            if (outError) *outError = codeGenData.errorMessage;
            if (outErrorBlockId) *outErrorBlockId = codeGenData.errorBlockId;
            return std::nullopt;
        }
    } else {
        std::cout << "Empty program\n";
    }

    auto sc = codeGenData.ss.str();

    std::cout << "Code gen out:\n";
    std::cout << sc << std::endl;

    return sc;
}