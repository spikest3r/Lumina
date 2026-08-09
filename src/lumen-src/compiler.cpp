#include "compiler.h"

struct Function {
    uint8_t opcode;
    uint8_t argCount;
};

inline void emitUint32(std::vector<uint8_t>& bytecode, uint32_t value) {
    bytecode.push_back(value & 0xFF);
    bytecode.push_back((value >> 8) & 0xFF);
    bytecode.push_back((value >> 16) & 0xFF);
    bytecode.push_back((value >> 24) & 0xFF);
}

inline void patchUint32(std::vector<uint8_t>& bytecode, int location, uint32_t value) {
    bytecode[location] = value & 0xFF;
    bytecode[location + 1] = (value >> 8) & 0xFF;
    bytecode[location + 2] = (value >> 16) & 0xFF;
    bytecode[location + 3] = (value >> 24) & 0xFF;
}

struct UnresolvedJump {
    std::string keyword;
    int location;
    int line;
    int routineIndex; // -1 for main program, >= 0 for subroutines
};

static const std::unordered_map<std::string, ConditionOp> condOpMap = {
    {"==", EQUALS},
    {">",  GREATER},
    {"<",  LESSER},
    {">=", GREATER_OR_EQ},
    {"<=", LESSER_OR_EQ},
    {"!=", NOT_EQUALS}
};

static const std::unordered_map<ConditionOp, uint8_t> condOpcodeMap = {
    {EQUALS,        0xC0},
    {GREATER,       0xC1},
    {LESSER,        0xC2},
    {GREATER_OR_EQ, 0xC3},
    {LESSER_OR_EQ,  0xC4},
    {NOT_EQUALS,    0xC5}
};

std::unordered_map<std::string, Function> funcList = {
    {"println",   {0x01,1}},
    {"print",     {0x02,1}},
    {"inputInt",  {0x03,1}},
    {"inputStr",  {0x04,1}},
    {"str2int",   {0x05,2}},
    {"int2str",   {0x06,2}},
    {"str2float", {0x07,2}},
    {"float2str", {0x08,2}},
    {"assertCapability", {0xA0,1}},
    {"openFile", {0xA1,2}},
    {"writeFile", {0xA2, 2}},
    {"readFile", {0xA3, 2}},
    {"closeFile", {0xA4, 1}},
    {"randomSeed", {0xA5, 1}},
    {"random", {0xA6, 1}},
    {"randomRange", {0xA7, 3}},
    {"httpRequest", {0xA8, 6}},
    {"strlen", {0xA9, 2}},
    {"substr", {0xAA, 4}},
    {"strfind", {0xAB, 3}},
    {"strcase", {0xAC, 3}},
    {"trim", {0xAD, 2}},
    // extended
    {"goToPos", {0xB0, 2}}
};

void printError(std::string error, int line, std::string& errorBuffer) {
    std::stringstream ss;
    ss << "Error on line " << line << std::endl << "    >>> " << error << std::endl;
    errorBuffer += ss.str();
}

void pushToStack(std::string token, CompilerData* data, std::vector<uint8_t>& bytecode) {
    bytecode.push_back(0x03); // PUSH opcode

    if (token.starts_with("'")) {
        auto strIndex = resolveString(token, data);
        bytecode.push_back(0x01); // operand type: string
        bytecode.push_back(static_cast<uint8_t>(strIndex));
    }
    else if (isPureNumber(token)) {
        bool isFloat = isFloatLiteral(token);
        TypeTag tag = isFloat ? TAG_FLOAT : TAG_INT;
        uint8_t dataType = isFloat ? 0x05 : 0x02;
        double x = std::stod(token);
        int idx = resolveConst(x, tag, data);
        bytecode.push_back(dataType);
        bytecode.push_back(static_cast<uint8_t>(idx));
    }
    else {
        bool ref = token.starts_with("&");
        auto index = resolveVariableIndex(token, data);
        bytecode.push_back(ref ? 0x04 : 0x03);
        bytecode.push_back(static_cast<uint8_t>(index));
    }
}

int compile(const std::string& script,
    CompilerData* compilerData,
    std::string& errBuffer,
    bool verbose, bool debugInfo
) {
    errBuffer.clear();

    std::istringstream file(script);

    std::string line;

    std::unordered_map<std::string, int> globalLabels;
    std::unordered_map<int, std::unordered_map<std::string, int>> routineLabels;

    std::unordered_map<int, std::vector<uint8_t>> subroutineBytecode;
    std::unordered_map<std::string, int> subroutineIndexMap;
    std::vector<UnresolvedJump> unresolvedRoutineCalls;
    std::vector<UnresolvedJump> unresolvedJumps;
    std::vector<int> condJumpStack;
    std::vector<int> elseJumpStack;

    std::string functionArgument = "";

    int blockDepth = 0;
    std::vector<bool> elseDefined;

    int lineIndex = 1; // for user messages
    bool inRoutine = false;
    int routineIndex = -1;
    int routineCount = 0;

    int funcArgs = 0;
    int requiredFuncArgs = 0;

    while (std::getline(file, line)) {
        if (verbose) std::cout << line << std::endl;
        auto tokens = tokenizeFormula(line);
        if (verbose) {
            for (const auto& token : tokens) {
                std::cout << "[" << token << "] " << token.size() << " ";
            }
            std::cout << std::endl;
        }
        std::string keyword = "";
        Operation op = NONE;
        int funcIndex = 0;
        int conditionArgs = 0;
        int varIndex_assign = 0;
        ConditionOp condOp = COP_NONE;
        std::vector<uint8_t>& bytecode = inRoutine ? subroutineBytecode[routineIndex] : compilerData->bytecode;

        for (const auto& token : tokens) {
            if (token == "=") {
                std::string formula;
                std::vector<std::string> strs;
                
                for (size_t i = 2; i < tokens.size(); i++) {
                    formula += tokens[i];
                }

                try {
                    compileExpression(
                        formula, compilerData, bytecode
                    ); // result in stack
                } catch (const std::exception& e) {
                    printError(e.what(), lineIndex, errBuffer);
                    return -1;
                }
                
                bytecode.push_back(0x02);
                keyword = tokens[0];
                auto var_index = resolveVariableIndex(keyword, compilerData);
                varIndex_assign = var_index;
                bytecode.push_back(var_index);
                break;
            }
            else if (token == "label") {
                if (op != NONE) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                op = LABEL;
                continue;
            }
            else if (token == "jump") {
                if (op != NONE) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                op = JUMP;
                continue;
            }
            else if (token == "if") {
                if (op != NONE) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                op = IF;
                blockDepth++;
                elseDefined.push_back(false);
                condJumpStack.push_back(-1);
                elseJumpStack.push_back(-1);
                continue;
            }
            else if (token == "endif") {
                if (blockDepth == 0) {
                    printError("Unexpected 'endif' (no matching 'if')", lineIndex, errBuffer);
                    return -1;
                }
                if (elseDefined[blockDepth - 1]) {
                    int loc = elseJumpStack.back();
                    patchUint32(bytecode, loc, static_cast<uint32_t>(bytecode.size()));
                }
                else {
                    int loc = condJumpStack.back();
                    patchUint32(bytecode, loc, static_cast<uint32_t>(bytecode.size()));
                }
                condJumpStack.pop_back();
                elseJumpStack.pop_back();
                blockDepth--;
                elseDefined.pop_back();
                continue;
            }
            else if (token == "else") {
                if (blockDepth == 0) {
                    printError("Unexpected 'else' (no matching 'if')", lineIndex, errBuffer);
                    return -1;
                }
                elseDefined[blockDepth - 1] = true;
                int loc = condJumpStack.back();

                // Patch false-jump location to skip the upcoming 5-byte 'JUMP32 target' instruction (1 byte opcode + 4 bytes uint32)
                patchUint32(bytecode, loc, static_cast<uint32_t>(bytecode.size() + 5));

                bytecode.push_back(0x06); // JUMP32
                emitUint32(bytecode, 0x00000000);
                elseJumpStack.back() = static_cast<int>(bytecode.size() - 4); // Track location of jump target
                continue;
            }
            else if (token == "halt") {
                if (op != NONE) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                bytecode.push_back(0xFF);
                continue;
            }
            else if (token == "routine") {
                if (op != NONE) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                op = SUBROUTINE;
                if (inRoutine) {
                    printError("Nested routines are not allowed", lineIndex, errBuffer);
                    return -1;
                }
                inRoutine = true;
                continue;
            }
            else if (token == "endroutine") {
                if (op != NONE) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                if (!inRoutine) {
                    printError("Unexpected 'endroutine' (no matching 'routine')", lineIndex, errBuffer);
                    return -1;
                }
                bytecode.push_back(0xFE); // RET
                inRoutine = false;
                routineIndex = -1;
                continue;
            }
            else if (token == "call") {
                if (tokens.size() < 2) {
                    printError("Syntax error: expected subroutine name after 'call'", lineIndex, errBuffer);
                    return -1;
                }
                std::string routineName = tokens[1];
                bytecode.push_back(0x07); // CALL32
                int loc = static_cast<int>(bytecode.size());
                emitUint32(bytecode, 0x00000000); // 4-byte placeholder

                int currRoutine = inRoutine ? routineIndex : -1;
                unresolvedRoutineCalls.push_back({ routineName, loc, lineIndex, currRoutine });
                break;
            }
            else {
                if (op == NONE) {
                    auto it = funcList.find(token);
                    if (it != funcList.end()) {
                        op = FUNC_CALL;
                        funcIndex = it->second.opcode;
                        funcArgs = 0;
                        requiredFuncArgs = it->second.argCount;
                    }
                    continue;
                }
            }

            keyword = token;

            switch (op) {
            case SUBROUTINE:
            {
                routineIndex = routineCount++;
                subroutineIndexMap[token] = routineIndex;
                subroutineBytecode[routineIndex] = std::vector<uint8_t>();
            }
            break;
            case FUNC_CALL:
            //case PUSH_STACK:
            {
                if (token == ",") {
                    try {
                        compileExpression(
                            functionArgument, compilerData, bytecode
                        ); // result in stack
                    } catch (const std::exception& e) {
                        printError(e.what(), lineIndex, errBuffer);
                        return -1;
                    }
                    funcArgs++;
                    functionArgument.clear();
                    break;
                }
                functionArgument += token;
            }
            break;
            case LABEL:
            {
                if (inRoutine) {
                    routineLabels[routineIndex][keyword] = static_cast<int>(bytecode.size());
                }
                else {
                    globalLabels[keyword] = static_cast<int>(bytecode.size());
                }
                op = NONE;
            }
            break;
            case JUMP:
            {
                int currentCtx = inRoutine ? routineIndex : -1;
                bool found = false;
                int targetOffset = 0;

                if (inRoutine) {
                    auto& rMap = routineLabels[routineIndex];
                    auto it = rMap.find(keyword);
                    if (it != rMap.end()) {
                        found = true;
                        targetOffset = it->second;
                    }
                }
                else {
                    auto it = globalLabels.find(keyword);
                    if (it != globalLabels.end()) {
                        found = true;
                        targetOffset = it->second;
                    }
                }

                bytecode.push_back(0x06); // JUMP32
                int loc = static_cast<int>(bytecode.size());

                if (found) {
                    emitUint32(bytecode, static_cast<uint32_t>(targetOffset));
                }
                else {
                    emitUint32(bytecode, 0x00000000);
                    unresolvedJumps.push_back({ keyword, loc, lineIndex, currentCtx });
                }
                op = NONE;
            }
            break;
            case IF:
            {
                auto it = condOpMap.find(keyword);
                if (it != condOpMap.end()) {
                    if (conditionArgs != 1) {
                        printError("Syntax error", lineIndex, errBuffer);
                        return -1;
                    }
                    condOp = it->second;
                }
                else {
                    if (conditionArgs > 1 && condOp != COP_NONE) {
                        printError("Syntax error", lineIndex, errBuffer);
                        return -1;
                    }
                    pushToStack(token, compilerData, bytecode);
                    conditionArgs++;
                }
            }
            break;
            default:
                break;
            }
        }

        switch (op) {
        case FUNC_CALL:
            try {
                compileExpression(
                    functionArgument, compilerData, bytecode
                ); // result in stack
            } catch (const std::exception& e) {
                printError(e.what(), lineIndex, errBuffer);
                return -1;
            }
            funcArgs++;
            functionArgument.clear();

            if(funcArgs != requiredFuncArgs) {
                auto it = std::find_if(funcList.begin(), funcList.end(),
                    [&](const auto& p) { return p.second.opcode == funcIndex; });
                std::string key = (it != funcList.end()) ? it->first : "<unknown>";
                std::stringstream ss;
                ss << "argument count mismatch for '" << key << "': expected " 
                    << requiredFuncArgs << ", got " << funcArgs << "\n";
                printError(ss.str(), lineIndex, errBuffer);
                return -1;
            }
            bytecode.push_back(0x04); // call function
            bytecode.push_back(funcIndex);
            break;
        case IF:
        {
            auto it = condOpcodeMap.find(condOp);
            if (it != condOpcodeMap.end()) {
                if (conditionArgs != 2) {
                    printError("Syntax error", lineIndex, errBuffer);
                    return -1;
                }
                bytecode.push_back(it->second); // IF32 opcode
                int loc = static_cast<int>(bytecode.size());
                emitUint32(bytecode, 0x00000000);
                condJumpStack.back() = loc;
            }
            else {
                printError("Syntax error", lineIndex, errBuffer);
                return -1;
            }
        }
        break;
        }
        op = NONE;

        lineIndex++;
    }

    // Appending HALT instruction to main bytecode
    compilerData->bytecode.push_back(0xFF); // HALT

    // Resolve Main Program Jumps
    for (const auto& it : unresolvedJumps) {
        if (it.routineIndex == -1) {
            auto it2 = globalLabels.find(it.keyword);
            if (it2 != globalLabels.end()) {
                patchUint32(compilerData->bytecode, it.location, static_cast<uint32_t>(it2->second));
            }
            else {
                printError("Label '" + it.keyword + "' is not defined in global scope", it.line, errBuffer);
                return -1;
            }
        }
    }

    // Append Subroutine Bytecodes and Calculate Final Absolute Offsets
    std::unordered_map<int, int> routineOffsets;

    for (const auto& it : subroutineBytecode) {
        auto idx = it.first;
        auto& routineBc = it.second;
        routineOffsets[idx] = static_cast<int>(compilerData->bytecode.size()); // Final offset where this routine starts

        // Resolve unresolved jumps inside this routine using scoped routine labels
        for (auto& jump : unresolvedJumps) {
            if (jump.routineIndex == idx) {
                auto& rMap = routineLabels[idx];
                auto labelIt = rMap.find(jump.keyword);
                if (labelIt != rMap.end()) {
                    // relative offset within the routine — VM adds routineBase at runtime
                    uint32_t relativeTarget = static_cast<uint32_t>(labelIt->second);
                    patchUint32(subroutineBytecode[idx], jump.location, relativeTarget);
                }
                else {
                    printError("Label '" + jump.keyword + "' is not defined in routine scope", jump.line, errBuffer);
                    return -1;
                }
            }
        }
        // Merge routine bytecode into main global bytecode vector
        compilerData->bytecode.insert(compilerData->bytecode.end(), routineBc.begin(), routineBc.end());
    }

    // Patch Subroutine Calls (CALL32)
    for (const auto& it : unresolvedRoutineCalls) {
        auto keyword = it.keyword;
        auto location = it.location;
        auto line = it.line;

        auto it2 = subroutineIndexMap.find(keyword);

        if (it2 != subroutineIndexMap.end()) {
            int rIdx = it2->second;
            uint32_t absAddress = static_cast<uint32_t>(routineOffsets[rIdx]);

            if (it.routineIndex == -1) {
                patchUint32(compilerData->bytecode, location, absAddress);
            }
            else {
                // Adjust for target routine in subroutineBytecode chunk before merging
                int absoluteLocationInGlobalBytecode = routineOffsets[it.routineIndex] + location;
                patchUint32(compilerData->bytecode, absoluteLocationInGlobalBytecode, absAddress);
            }
        }
        else {
            printError("Subroutine '" + keyword + "' is not defined", line, errBuffer);
            return -1;
        }
    }

    // if (debugInfo) {
    //     std::ofstream debugFile(fileName + ".bin.dbg");
    //     if (debugFile.is_open()) {
    //         // Write variable names and their indices
    //         debugFile << "variables" << std::endl;
    //         for (const auto& var : compilerData->variableMap) {
    //             debugFile << var.first << " " << var.second << std::endl;
    //         }
    //         // Write subroutine names, their bytecode offsets and bytecode length
    //         debugFile << "routines" << std::endl;
    //         for (const auto& sub : subroutineIndexMap) {
    //             debugFile << sub.first << std::endl;
    //             debugFile << routineOffsets[sub.second] << std::endl;
    //             debugFile << subroutineBytecode[sub.second].size() << std::endl;
    //         }
    //         // Write exec functions
    //         debugFile << "exec" << std::endl;
    //         for (const auto& func : funcList) {
    //             debugFile << func.first << " " << func.second.opcode << std::endl;
    //         }
    //     }
    // }

    compilerData->variableCount = static_cast<int>(compilerData->variableMap.size());

    return 0;
}