#include "mainmenu.h"
#include "helpers.h"
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

    lastExists = fs::exists(getLastProjectPath());

    logoTexture = engine->createTexture("logoTex", "assets/textures/logo.png");
    logoElement = engine->createUIElement(logoTexture, {0,0}, {704,384});

    examples = false;
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
    ToolUI::SetNextWindowSize({500,150});
    ToolUI::SetNextWindowPos({extents.x / 2 - 250, extents.y / 2 + 100});
    if(examples) renderExamples();
    else if(about) renderAbout();
    else renderMainMenu();

    m_fileManager.Render();
}

void MainMenuScene::renderMainMenu() {
    ToolUI::Begin("Main menu", false, false);
    if(ToolUI::Button("New project")) {
        loadFreeplay();
    }
    if(ToolUI::Button("Examples & Templates")) {
        examples = true;
    }
    if(ToolUI::Button("Load existing project")) {
        m_fileManager.Init(FileManager::Mode::OPEN, "", {".lumina"});
    }
    if(lastExists) {
        if(ToolUI::Button("Load last project")) {
            std::string text = static_cast<const std::stringstream&>(std::stringstream() << std::ifstream(getLastProjectPath()).rdbuf()).str();
            loadFreeplay(text);
        }
    }
    if(ToolUI::Button("About")) {
        about = true;
    }
    ToolUI::End();
}

void MainMenuScene::renderExamples() {
    ToolUI::Begin("Examples & Templates", false, false);
    if(ToolUI::Button("< Back")) {
        examples = false;
    }
    ToolUI::Text("Examples");
    if(ToolUI::Button("Collect & Avoid")) {
        loadFreeplay("examples/collector.lumina", true);
    }
    if(ToolUI::Button("Simple Parkour")) {
        loadFreeplay("examples/parkour.lumina", true);
    }
    ToolUI::Separator(HORIZONTAL);
    ToolUI::Text("Templates");
    if(ToolUI::Button("Basic Movement")) {
        loadFreeplay("examples/template_movement.lumina", true);
    }
    if(ToolUI::Button("Camera-Relative Movement")) {
        loadFreeplay("examples/template_camera_movement.lumina", true);
    }
    ToolUI::End();
}

void OpenURL(const std::string& url) {
#ifdef _WIN32
	ShellExecuteA(NULL, "open", url.c_str(), NULL, NULL, SW_SHOWNORMAL);
#else
	system(("xdg-open " + url).c_str());
#endif
}

inline void Link(const char* linkTitle, const char* link, bool sameline = true) {
	ToolUI::Text(linkTitle);
	if(sameline) ToolUI::SameLine();
	if (ToolUI::Button(link)) {
		OpenURL(link);
	}
}

void MainMenuScene::renderAbout() {
    ToolUI::Begin("About");

    if(ToolUI::Button("< Back")) {
        about = false;
    }

    ToolUI::Text("Lumina");
    ToolUI::Text("Visual programming environment");
    ToolUI::Text("A game creation environment for everyone.");

    ToolUI::Separator(HORIZONTAL);

    ToolUI::Text("Version 1.0.0");
    ToolUI::Text("Created by spikest3r");

    Link("GitHub", "https://github.com/spikest3r/Lumina");

    ToolUI::Separator(HORIZONTAL);

    ToolUI::Text("LumenLang v3.2");
    Link("Github", "https://github.com/spikest3r/LumenLang");

    ToolUI::Separator(HORIZONTAL);

    ToolUI::Text("Proudly powered by VulkanEngine");
    Link("Github", "https://github.com/spikest3r/VulkanEngine");

    ToolUI::Separator(HORIZONTAL);

    Link("Learn more about me", "https://olehsheremeta.com/");

    ToolUI::End();
}