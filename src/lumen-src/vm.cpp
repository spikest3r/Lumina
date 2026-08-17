#include "vm.h"

inline uint32_t readU32(const std::vector<uint8_t>& bytecode, size_t& pc)
{
    uint32_t value =
        (uint32_t)bytecode[pc] |
        ((uint32_t)bytecode[pc + 1] << 8) |
        ((uint32_t)bytecode[pc + 2] << 16) |
        ((uint32_t)bytecode[pc + 3] << 24);

    pc += 4;
    return value;
}

int64_t getInt(const Variant& v) {
    return std::get<int64_t>(v.data);
}

double getNumeric(const Variant& v) {
    if (v.type == TAG_FLOAT) return std::get<double>(v.data);
    if (v.type == TAG_INT) return static_cast<double>(std::get<int64_t>(v.data));
    return 0.0;
}

bool isFloatVariant(const Variant& a, const Variant& b) {
    return a.type == TAG_FLOAT || b.type == TAG_FLOAT;
}

int execute(
    VMProgramData* progData,
    VMExecutionData* execData
) {
    auto opcode = progData->bytecode[execData->PC];
    int offset = getOpCodeOffset(opcode);

    switch (opcode) {
    case 0x01:
        // run subroutine (CALL8 legacy)
    {
        auto addr = progData->bytecode[execData->PC + 1];
        execData->pcStack.push_back({
            execData->PC + offset,
            execData->routineBase
        });

        execData->routineBase = addr;

        return addr;
    }
    break;
    case 0xFE:
    {
        if (execData->pcStack.empty()) {
            std::cerr << "Return stack underflow!" << std::endl;
            return -1;
        }

        auto frame = execData->pcStack.back();
        execData->pcStack.pop_back();

        execData->routineBase = frame.routineBase;

        return frame.returnPC;
    }
    break;
    break;
    case 0x02:
    {
        auto varIndex = progData->bytecode[execData->PC + 1];
        auto var = execData->stack.back();
        execData->variables[varIndex].type = var.type;
        switch (var.type) {
        case TAG_INT:
            execData->variables[varIndex].data = std::get<int64_t>(var.data);
            break;
        case TAG_FLOAT:
            execData->variables[varIndex].data = std::get<double>(var.data);
            break;
        case TAG_STRING:
            execData->variables[varIndex].data = std::get<std::string>(var.data);
            break;
        }
        execData->stack.pop_back();
    }
    break;
    case 0x03:
    {
        auto dataType = progData->bytecode[execData->PC + 1];
        auto value = progData->bytecode[execData->PC + 2];
        switch (dataType) {
        case 0x01:
            execData->stack.push_back({ TAG_STRING, progData->stringPool[value] });
            break;
        case 0x02:
            execData->stack.push_back({ TAG_INT, static_cast<int64_t>(progData->constPool[value]) });
            break;
        case 0x03:
        {
            auto variable = execData->variables[value];
            execData->stack.push_back({ variable.type, variable.data });
        }
        break;
        case 0x04:
        {
            // raw uint8_t passed
            execData->stack.push_back({ TAG_INT, static_cast<int64_t>(value) });
        }
        break;
        case 0x05:
            execData->stack.push_back({ TAG_FLOAT, progData->constPool[value] });
            break;
        }
    }
    break;
    case 0x04: {
        auto functionIndex = progData->bytecode[execData->PC + 1];
        auto it = funcMap.find(functionIndex);
        if (it != funcMap.end()) {
            try {
                it->second(*execData);
            } catch(std::exception& s) {
                std::cerr << "Function error\n" << s.what() << std::endl;
                execData->halt = true;
            }
        }
        else {
            std::cerr << "Unknown function index: " << functionIndex << std::endl;
            return -1;
        }
        break;
    }
    case 0x05: // JUMP8 legacy
    {
        auto newPC = progData->bytecode[execData->PC + 1];
        return execData->routineBase + newPC;
    }
    break;
    case 0x06:
    {
        size_t pc = execData->PC + 1;
        uint32_t addr = readU32(progData->bytecode, pc);

        return execData->routineBase + addr;
    }
    break;
    case 0x07:
    {
        size_t pc = execData->PC + 1;
        uint32_t target = readU32(progData->bytecode, pc);

        execData->pcStack.push_back({
            execData->PC + offset,
            execData->routineBase
            });

        execData->routineBase = target;

        return target;
    }
    break;
    case 0xA0: { // ADD
        if(execData->stack.size() < 2) {
            execData->stack.push_back({TAG_INT, 0});
            break;
        }
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();
        Variant result;
        if (isFloatVariant(a, b)) {
            result.type = TAG_FLOAT;
            result.data = getNumeric(a) + getNumeric(b);
        }
        else {
            result.type = TAG_INT;
            result.data = getInt(a) + getInt(b);
        }
        execData->stack.push_back(result);
        break;
    }
    case 0xA1: { // SUB
        if(execData->stack.size() < 2) {
            execData->stack.push_back({TAG_INT, 0});
            break;
        }
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();
        Variant result;
        if (isFloatVariant(a, b)) {
            result.type = TAG_FLOAT;
            result.data = getNumeric(a) - getNumeric(b);
        }
        else {
            result.type = TAG_INT;
            result.data = getInt(a) - getInt(b);
        }
        execData->stack.push_back(result);
        break;
    }
    case 0xA2: { // MUL
        if(execData->stack.size() < 2) {
            execData->stack.push_back({TAG_INT, 0});
            break;
        }
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();
        Variant result;
        if (isFloatVariant(a, b)) {
            result.type = TAG_FLOAT;
            result.data = getNumeric(a) * getNumeric(b);
        }
        else {
            result.type = TAG_INT;
            result.data = getInt(a) * getInt(b);
        }
        execData->stack.push_back(result);
        break;
    }
    case 0xA3: { // DIV
        if(execData->stack.size() < 2) {
            execData->stack.push_back({TAG_INT, 0});
            break;
        }
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();
        Variant result;
        result.type = TAG_FLOAT;
        result.data = getNumeric(a) / getNumeric(b);
        execData->stack.push_back(result);
        break;
    }
    case 0xA4: { // POW
        if(execData->stack.size() < 2) {
            execData->stack.push_back({TAG_INT, 0});
            break;
        }
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();
        Variant result;
        if (isFloatVariant(a, b)) {
            result.type = TAG_FLOAT;
            result.data = std::pow(getNumeric(a), getNumeric(b));
        }
        else {
            result.type = TAG_INT;
            result.data = static_cast<int64_t>(std::pow(getNumeric(a), getNumeric(b)));
        }
        execData->stack.push_back(result);
        break;
    }
    case 0xA5: { // MOD
        if(execData->stack.size() < 2) {
            execData->stack.push_back({TAG_INT, 0});
            break;
        }
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();
        Variant result;
        if (isFloatVariant(a, b)) {
            result.type = TAG_FLOAT;
            result.data = std::fmod(getNumeric(a), getNumeric(b));
        }
        else {
            result.type = TAG_INT;
            result.data = getInt(a) % getInt(b);
        }
        execData->stack.push_back(result);
        break;
    }
    case 0xB0: // ==
    case 0xB1: // >
    case 0xB2: // <
    case 0xB3: // >=
    case 0xB4: // <=
    case 0xB5: // != 
    {
        Variant a, b;
        if(execData->stack.size() < 2) {
            a = {TAG_INT, 0xFEEDFACE};
            b = {TAG_INT, 0xDEADBEEF};
        } else {
            b = execData->stack.back(); execData->stack.pop_back();
            a = execData->stack.back(); execData->stack.pop_back();
        }
        int falseIndex = progData->bytecode[execData->PC + 1];

        bool result = false;
        double av = getNumeric(a);
        double bv = getNumeric(b);

        switch (opcode) {
        case 0xB0: result = av == bv; break;
        case 0xB1: result = av > bv; break;
        case 0xB2: result = av < bv; break;
        case 0xB3: result = av >= bv; break;
        case 0xB4: result = av <= bv; break;
        case 0xB5: result = av != bv; break;
        }

        if (!result) {
            return execData->routineBase + falseIndex;
        }
        break;
    }
    case 0xC0: // ==32
    case 0xC1: // >32
    case 0xC2: // <32
    case 0xC3: // >=32
    case 0xC4: // <=32
    case 0xC5: // !=32
    {
        Variant b = execData->stack.back(); execData->stack.pop_back();
        Variant a = execData->stack.back(); execData->stack.pop_back();

        size_t pc = execData->PC + 1;
        uint32_t falseIndex = readU32(progData->bytecode, pc);

        bool result = false;
        double av = getNumeric(a);
        double bv = getNumeric(b);

        switch (opcode) {
        case 0xC0: result = av == bv; break;
        case 0xC1: result = av > bv; break;
        case 0xC2: result = av < bv; break;
        case 0xC3: result = av >= bv; break;
        case 0xC4: result = av <= bv; break;
        case 0xC5: result = av != bv; break;
        }

        if (!result) {
            return execData->routineBase + falseIndex;
        }

        break;
    }
    case 0xAA: { // join strings
        int strCount = getInt(execData->stack.back()); execData->stack.pop_back();
        std::string result;
        for (int i = 0; i < strCount; i++) {
            Variant strVar = execData->stack.back(); execData->stack.pop_back();
            std::string str = std::get<std::string>(strVar.data);
            result = str + result; // prepend to maintain order
        }
        execData->stack.push_back({ TAG_STRING, result });
        break;
    }
    case 0xDE: { // dereference
        Variant ptrVar = execData->stack.back(); execData->stack.pop_back();
        if(ptrVar.type != TAG_INT) {
            std::cout << "Invalid dereference" << std::endl;
            return -1;
        }
        int ptr = getInt(ptrVar);
        if(ptr >= execData->variables.size() || ptr < 0) {
            std::cout << "Invalid dereference" << std::endl;
            return -1;
        }
        Variant variable = execData->variables[ptr];
        execData->stack.push_back(variable);
        break;
    }
    case 0xFF:
        execData->halt = true;
        break;
    default:
        std::cout << "Invalid opcode: " << (int)opcode << std::endl;
        return -1; // error
    }

    return execData->PC + offset;
}