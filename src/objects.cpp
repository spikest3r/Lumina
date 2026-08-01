#include "objects.h"

InteractiveObject::InteractiveObject() {
    vm.self = this;
}

void InteractiveObject::resetVM() {
    vm.variables.clear();
    vm.variables.resize(program.variableCount);
    vm.stack.clear();
    vm.pcStack.clear();

    vm.PC = 0;
    vm.routineBase = 0;
    vm.halt = false;
    vm.suspended = false;
}

void InteractiveObject::stepVM() {
    if(vm.suspended || vm.halt) return;

    auto opcode = program.bytecode[vm.PC];
    int offset = getOpCodeOffset(opcode);
    int result = 0;

    // TODO: get rid of this
    try {
        result = execute(&program, &vm);
    } catch(const std::exception& e) {
        std::cerr << "Something went wrong\n";
        std::cerr << e.what() << std::endl;
    }

    if(vm.halt || result == -1) {
        return;
    }

    vm.PC = result;
}