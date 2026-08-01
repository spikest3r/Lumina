#pragma once
#include "includes.h"
#include "helpers.h"
#include "types.h"
#include "tokenizer.h"

int compile(const std::string& script, 
    CompilerData* compilerData,
    bool verbose = false, bool debugInfo = false
);

void compileExpression(
    std::string expr, CompilerData* data, std::vector<uint8_t>& bytecode
);
