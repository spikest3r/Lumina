#include "helpers.h"

// Splits a full URL like "http://host:port/path" into scheme+host and path.
bool splitUrl(const std::string& url, std::string& hostPart, std::string& pathPart) {
    size_t schemeEnd = url.find("://");
    if (schemeEnd == std::string::npos) return false;

    size_t pathStart = url.find('/', schemeEnd + 3);
    if (pathStart == std::string::npos) {
        hostPart = url;
        pathPart = "/";
    } else {
        hostPart = url.substr(0, pathStart);
        pathPart = url.substr(pathStart);
    }
    return true;
}

// Parses "Key: Value\nKey2: Value2\n..." into httplib::Headers
httplib::Headers parseHeaders(const std::string& headerStr) {
    httplib::Headers headers;
    std::istringstream stream(headerStr);
    std::string line;

    while (std::getline(stream, line)) {
        if (line.empty()) continue;
        // strip trailing \r if present (in case of \r\n input)
        if (!line.empty() && line.back() == '\r') line.pop_back();

        size_t colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = line.substr(0, colon);
        std::string value = line.substr(colon + 1);

        // trim leading space on value
        size_t valStart = value.find_first_not_of(' ');
        if (valStart != std::string::npos) value = value.substr(valStart);

        headers.emplace(key, value);
    }

    return headers;
}

void replaceAll(std::string& str, const std::string& from, const std::string& to) {
    if (from.empty()) return;
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
        str.replace(pos, from.length(), to);
        pos += to.length();
    }
}

bool isPureNumber(const std::string& s) {
    if (s.empty()) return false;
    size_t start = (s[0] == '-') ? 1 : 0;
    if (start == s.size()) return false; // just "-" alone

    bool seenDigit = false;
    bool seenDot = false;
    bool seenExp = false;
    for (size_t i = start; i < s.size(); i++) {
        unsigned char c = s[i];
        if (std::isdigit(c)) {
            seenDigit = true;
        } else if (c == '.' && !seenDot && !seenExp) {
            seenDot = true;
        } else if ((c == 'e' || c == 'E') && !seenExp && seenDigit) {
            seenExp = true;
            // optional sign right after the exponent marker
            if (i + 1 < s.size() && (s[i + 1] == '+' || s[i + 1] == '-')) {
                i++;
            }
        } else {
            return false;
        }
    }
    return seenDigit;
}

int resolveVariableIndex(std::string keyword, CompilerData* data) {
    // strip ref and deref
    replaceAll(keyword, "&", "");
    replaceAll(keyword, "*", "");

    auto it = data->variableMap.find(keyword);

    if (it != data->variableMap.end()) {
        return it->second;
    } else {
        int idx = (int)data->variableMap.size();
        data->variableMap[keyword] = idx;
        return idx;
    }
}

int resolveString(std::string str, CompilerData* data) {
    replaceAll(str, "'", "");

    auto it = data->stringPoolMap.find(str);

    if (it != data->stringPoolMap.end()) {
        return it->second;
    } else {
        int idx = (int)data->stringPool.size();
        data->stringPoolMap[str] = idx;
        data->stringPool.push_back(str);
        return idx;
    }
}

int resolveConst(double constValue, TypeTag type, CompilerData* data) {
    std::pair<int, double> key = {static_cast<int>(type), constValue};

    auto it = data->constPoolMap.find(key);
    if (it != data->constPoolMap.end()) {
        return it->second;
    }

    int index = static_cast<int>(data->constPool.size());
    data->constPool.push_back(constValue);
    data->constPoolMap[key] = index;
    return index;
}

int getOpCodeOffset(int opcode) {
    switch (opcode) {
    case 0x03: // PUSH
        return 3;

    case 0x01: // CALL (16-bit/8-bit relative)
    case 0x02: // POP
    case 0x04: // EXEC
    case 0x05: // JUMP (8-bit)
    case 0xB0: // JEQ
    case 0xB1: // JGR
    case 0xB2: // JLS
    case 0xB3: // JGE
    case 0xB4: // JLE
    case 0xB5: // JNE
        return 2;

    case 0xAA: // JOIN
    case 0xA0: // ADD
    case 0xA1: // SUB
    case 0xA2: // MUL
    case 0xA3: // DIV
    case 0xA4: // POW
    case 0xA5: // MOD
    case 0xFE: // RET
    case 0xFF: // HLT
    case 0xDE:
        return 1;

        // 32-bit offset instructions (1 byte opcode + 4 bytes address/offset)
    case 0x06: // JUMP32
    case 0x07: // CALL32
    case 0xC0: // JEQ32
    case 0xC1: // JGR32
    case 0xC2: // JLS32
    case 0xC3: // JGE32
    case 0xC4: // JLE32
    case 0xC5: // JNE32
        return 5;

    default:
        // Fallback for unknown opcodes or 0x00 padding bytes
        return 1;
    }
}

bool isVar(const std::string &t) {
    if (t.empty()) return false;
    size_t start = (t[0] == '&' || t[0] == '*') ? 1 : 0;
    if (start >= t.size()) return false;
    if (!(std::isalpha(static_cast<unsigned char>(t[start])) || t[start] == '_'))
        return false;
    for (size_t i = start + 1; i < t.size(); i++) {
        if (!(std::isalnum(static_cast<unsigned char>(t[i])) || t[i] == '_'))
            return false;
    }
    return true;
}

std::string variantToString(const Variant& v) {
    switch (v.type) {
        case TAG_INT:
            return std::to_string(std::get<int64_t>(v.data));
        case TAG_FLOAT:
            return std::to_string(std::get<double>(v.data));
        case TAG_STRING:
            return "'" + std::get<std::string>(v.data) + "'";
        default:
            return "<unknown type>";
    }
}

// A literal is a float if it contains a decimal point or an exponent.
// "3" -> int, "3.0" -> float, "3e2" -> float, "-3.5" -> float.
bool isFloatLiteral(const std::string &s) {
    return s.find('.') != std::string::npos ||
           s.find('e') != std::string::npos ||
           s.find('E') != std::string::npos;
}

std::map<int, std::string> disassemblyMap = {
    {0x01, "CALL"},
    {0x02, "POP"},
    {0x03, "PUSH"},
    {0x04, "EXEC"},
    {0x05, "JUMP"},
    {0x06, "JUMP32"},
    {0x07, "CALL32"},

    {0xA0, "ADD"},
    {0xA1, "SUB"},
    {0xA2, "MUL"},
    {0xA3, "DIV"},
    {0xA4, "POW"},
    {0xA5, "MOD"},

    // 8-bit / Standard Branches
    {0xB0, "JEQ"},
    {0xB1, "JGR"},
    {0xB2, "JLS"},
    {0xB3, "JGE"},
    {0xB4, "JLE"},
    {0xB5, "JNE"},

    // 32-bit Branches
    {0xC0, "JEQ32"},
    {0xC1, "JGR32"},
    {0xC2, "JLS32"},
    {0xC3, "JGE32"},
    {0xC4, "JLE32"},
    {0xC5, "JNE32"},

    {0xAA, "JOIN"},
    {0xDE, "DEREF"},
    {0xFE, "RET"},
    {0xFF, "HLT"}
};
