#pragma once
#include "includes.h"

typedef enum {
    TAG_INT = 2,
    TAG_FLOAT = 3,
    TAG_STRING = 1
} TypeTag;

typedef struct {
    TypeTag type;
    std::variant<int64_t, double, std::string> data;
} Variant;

struct VMProgramData {
    std::vector<uint8_t> bytecode;
    std::vector<std::string> stringPool;
    std::vector<double> constPool;
    int variableCount = 0;
};

struct CallFrame {
    int returnPC;
    int routineBase;
};

struct VMExecutionData {
    std::vector<Variant> variables;
    std::vector<Variant> stack;
    std::vector<CallFrame> pcStack;

    int PC = 0;
    int routineBase = 0;
    bool halt = false;
};

typedef enum {
    NONE,
    ASSIGN,
    FUNC_CALL,
    PUSH_STACK,
    LABEL,
    JUMP,
    IF, ELSE,
    SUBROUTINE
} Operation;

typedef enum {
    COP_NONE,
    EQUALS,
    GREATER,
    LESSER,
    GREATER_OR_EQ,
    LESSER_OR_EQ,
    NOT_EQUALS
} ConditionOp;