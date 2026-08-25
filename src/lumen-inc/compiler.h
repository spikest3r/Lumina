#pragma once
#include "includes.h"
#include "helpers.h"
#include "types.h"
#include "tokenizer.h"

int compile(std::string fileName,
    CompilerData* compilerData,
    bool verbose = false, bool debugInfo = false
);

int compileFromFile(std::ifstream& file,
    CompilerData* compilerData,
    bool verbose = false, bool debugInfo = false, std::string fileName = ""
);

int compileFromText(const std::string& text,
    CompilerData* compilerData,
    bool verbose = false, bool debugInfo = false, std::string fileName = ""
);

void compileExpression(
    std::string expr, CompilerData* data, std::vector<uint8_t>& bytecode
);

std::string getCompilerError();