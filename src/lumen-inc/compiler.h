#pragma once
#include "includes.h"
#include "helpers.h"
#include "types.h"
#include "tokenizer.h"

int compile(const std::string& script,
    CompilerData* compilerData,
    std::string& errBuffer,
    bool verbose, bool debugInfo
);

void compileExpression(
    std::string expr, CompilerData* data, std::vector<uint8_t>& bytecode
);
