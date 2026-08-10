#pragma once

#include <engine.h>
#include "helpers.h"
#include "lumen-inc/vm.h"
#include "lumen-inc/compiler.h"
#include "levelstate.h"

enum VMState {
    RUNNING,
    HALTED,
    SUSPENDED,
    DONE
};

// non-interactive objects/tiles

class Tile : public GameObject {
public:
    std::string type;
    std::string id;
    bool isInteractive;
};

// interactive objects/tiles

class InteractiveObject : public Tile {
public:
    VMProgramData program;
    VMExecutionData vm;

    GoToPosState goToState;
    WaitUntilGroundState waitGroundState;

    std::string sourceCode;

    InteractiveObject();
    void resetVM(); // clears VM state
    bool compileCode(std::string& errBuffer);
    VMState stepVM();
    bool execLock = false;

    bool gravity = false;
    ExecutionType execType;

    std::unordered_map<std::string, Tile*> touching;

    void Start(Engine* engine) override;
    void Destroy(Engine* engine) override;
};