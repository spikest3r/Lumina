#pragma once
#include "includes.h"
#include "helpers.h"
#include "types.h"

using NativeFn = std::function<void(std::vector<Variant>&, std::vector<Variant>&)>;

int64_t getInt(const Variant& v);
double getNumeric(const Variant& v); // reads int64_t or double as a double, for mixed int/float arithmetic
bool isFloatVariant(const Variant& a, const Variant& b); // true if either operand is TAG_FLOAT

extern std::unordered_map<int, NativeFn> funcMap;

int run(
    VMProgramData* progData
);

int execute(
    VMProgramData* progData,
    VMExecutionData* execData
);