#include "mainmenu.h"
#include <engine_tool_ui.h>

#include <filesystem>
#include <iostream>
#include <fstream>
#include <sstream>

namespace fs = std::filesystem;

void MainMenuScene::InitScene(Engine* engine) {
    engine->SetUICallback([this](Engine* engine)
    {
        this->UICallback(engine);
    });

    lastExists = fs::exists("last");

    logoTexture = engine->createTexture("logoTex", "assets/textures/logo.png");
    logoElement = engine->createUIElement(logoTexture, {0,0}, {704,384});
}

void MainMenuScene::DestroyScene(Engine* engine) {
    engine->SetUICallback(nullptr);
}

void MainMenuScene::UpdateScene(Engine* engine) {
    Vector2 extents = engine->getExtents();
    logoElement->position = {extents.x / 2, extents.y / 2 + 75};

    m_fileManager.Update();

    if (m_fileManager.HasResult()) {
        std::string path = m_fileManager.GetResult();
        
        loadFreeplay(path);

        m_fileManager.ClearResult();
    }
}

void MainMenuScene::UICallback(Engine* engine) {
    Vector2 extents = engine->getExtents();
    ToolUI::SetNextWindowSize({300,100});
    ToolUI::SetNextWindowPos({extents.x / 2 - 150, extents.y / 2 + 100});
    ToolUI::Begin("Main menu", false, false);
    if(ToolUI::Button("New project")) {
        loadFreeplay();
    }
    if(ToolUI::Button("Load existing project")) {
        m_fileManager.Init(FileManager::Mode::OPEN, "", {".lumina"});
    }
    if(lastExists) {
        if(ToolUI::Button("Load last project")) {
            std::string text = static_cast<const std::stringstream&>(std::stringstream() << std::ifstream("last").rdbuf()).str();
            loadFreeplay(text);
        }
    }
    ToolUI::End();

    m_fileManager.Render();
}