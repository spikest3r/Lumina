#pragma once

#include <engine.h>
#include "helpers.h"
#include "lumen-inc/vm.h"

class InteractiveObject : public GameObject {
public:
    VMProgramData program;
    VMExecutionData vm;
    GoToPosState goToState;

    InteractiveObject();
    void resetVM(); // clears VM state
    void stepVM();
};

// user objects

class Puppet : public InteractiveObject {
public:
    
};