#pragma once
#include "includes.h"
#include "types.h"
#include "httplib.h"

// Hash for (TypeTag as int, double value) pairs, used as the constPoolMap key.
struct ConstPoolKeyHash {
    size_t operator()(const std::pair<int, double>& key) const {
        size_t h1 = std::hash<int>{}(key.first);
        size_t h2 = std::hash<double>{}(key.second);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

struct CompilerData {
    std::vector<uint8_t> bytecode;

    std::vector<std::string> stringPool;
    std::vector<double> constPool;
    int variableCount = 0;

    std::unordered_map<std::string, int> variableMap;
    std::unordered_map<std::string, int> stringPoolMap;
    std::unordered_map<std::pair<int, double>, int, ConstPoolKeyHash> constPoolMap;
};

bool splitUrl(const std::string& url, std::string& hostPart, std::string& pathPart);
httplib::Headers parseHeaders(const std::string& headerStr);
void replaceAll(std::string& str, const std::string& from, const std::string& to);
bool isPureNumber(const std::string& s);
int resolveVariableIndex(std::string keyword, CompilerData* data);
int resolveString(std::string str, CompilerData* data);
int resolveConst(double constValue, TypeTag type, CompilerData* data);
int getOpCodeOffset(int opcode);
bool isVar(const std::string &s);
std::string variantToString(const Variant& v);
bool isFloatLiteral(const std::string &s);

extern std::map<int, std::string> disassemblyMap;