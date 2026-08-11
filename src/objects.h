#pragma once

#include <engine.h>
#include "helpers.h"
#include "lumen-inc/vm.h"
#include "lumen-inc/compiler.h"
#include "levelstate.h"

class FreeplayScene;

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
    WaitState waitState;

    std::string sourceCode;

    InteractiveObject();
    void resetVM(); // clears VM state
    bool compileCode(std::string& errBuffer);
    VMState stepVM();
    bool execLock = false;

    bool gravity = false;
    ExecutionType execType;

    bool isTouching(std::string name);

    Engine* engPtr;
    FreeplayScene* scene;
    void Start(Engine* engine) override;
};