#include "freeplay.h"
#include <iostream>
#include <engine_tool_ui.h>

#include "lumen-inc/compiler.h"
#include "lumen-inc/vm.h"
#include "lumen-inc/programfile.h"

constexpr Vector2 toolbarButtonSize = {110,50};

void FreeplayScene::InitScene(Engine* engine) {
    engine->SetUICallback([this](Engine* engine)
    {
        this->UICallback(engine);
    });

    std::cout << "Freeplay scene loaded\n";
}

void FreeplayScene::UICallback(Engine* engine) {
    Vector2 extents = engine->getExtents();

    ToolUI::SetNextWindowPos({0, 0});
    ToolUI::SetNextWindowSize({extents.x, 70});
    ToolUI::Begin("##toolbar", true);
    if(ToolUI::Button("Run", toolbarButtonSize)) {
        requestRun();
    }
    ToolUI::SameLine();
    if(ToolUI::Button("Button2", toolbarButtonSize)) {

    }
    ToolUI::End();
    
    ToolUI::Begin("Code editor");
    ToolUI::InputTextMultiline("##editor", sourceCode);
    ToolUI::End();
}

void FreeplayScene::UpdateScene(Engine* engine) {
    if(shouldRun) {
        shouldRun = false;

        CompilerData data;
        int status = compile(sourceCode, &data);
        if(status != 0) {
            std::cout << "Failed to compile\n";
        } else {
            VMProgramData progData;
            progData.bytecode = std::move(data.bytecode);
            progData.stringPool = std::move(data.stringPool);
            progData.constPool = std::move(data.constPool);
            progData.variableCount = data.variableCount;
            int status2 = run(&progData);
            if(status2 != 0) {
                std::cout << "Execution error has occured!\n";
            } else {
                std::cout << "Executed\n";
            }
        }
    }
}

void FreeplayScene::DestroyScene(Engine* engine) {

}

void FreeplayScene::requestRun() {
    if(!shouldRun) shouldRun = true;
    else {
        std::cout << "already running\n";
    }
}