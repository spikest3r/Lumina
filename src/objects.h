#pragma once

#include <engine.h>
#include "helpers.h"
#include "lumen-inc/vm.h"

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
};

// interactive objects/tiles

class InteractiveObject : public Tile {
public:
    VMProgramData program;
    VMExecutionData vm;
    GoToPosState goToState;

    InteractiveObject();
    void resetVM(); // clears VM state
    VMState stepVM();
};