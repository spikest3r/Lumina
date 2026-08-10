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

    goToState = {};
}

VMState InteractiveObject::stepVM() {
    if(vm.suspended) return SUSPENDED;
    if(vm.halt) return HALTED;

    auto opcode = program.bytecode[vm.PC];
    int offset = getOpCodeOffset(opcode);
    int result = 0;

    // TODO: get rid of this
    try {
        result = execute(&program, &vm);
    } catch(const std::exception& e) {
        std::cerr << "Something went wrong\n";
        std::cerr << e.what() << std::endl;
        return DONE;
    }

    if(vm.halt || result == -1) {
        return DONE;
    }

    vm.PC = result;

    return RUNNING;
}

bool InteractiveObject::compileCode(std::string& errBuffer) {
    // compile code
    CompilerData data;
    int status = compile(sourceCode, &data, errBuffer, false, false); // TODO: rid of flags
    if(status != 0) {
        // compilation failed
        std::cout << "Failed to compile\n";
        return false;
    } else {
        // proceed
        program.bytecode = std::move(data.bytecode);
        program.stringPool = std::move(data.stringPool);
        program.constPool = std::move(data.constPool);
        program.variableCount = data.variableCount;
    }
    return true;
}

void InteractiveObject::Start(Engine* engine) {

}

void InteractiveObject::Destroy(Engine* engine) {

}

// user objects