#pragma once
#include "includes.h"
#include "types.h"

class BinaryProgram {
public:
    std::vector<uint8_t> bytecode;
    std::vector<std::string> stringPool;
    std::vector<double> constPool;
    int variableCount;
    bool save(const std::string& path);
    bool load(const std::string& path);
};

void constructProgData(VMProgramData* progData, BinaryProgram* inProg);