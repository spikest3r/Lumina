#pragma once
#include <engine.h>
#include <scene.h>
#include "filemanager.h"
#include <ui.h>

void loadFreeplay();
void loadFreeplay(const std::string& projectFile, bool doNotPersist = false);
void unloadFreeplay();

class MainMenuScene : public Scene {
protected:
    void InitScene(Engine* engine) override;
    void DestroyScene(Engine* engine) override;
    void UpdateScene(Engine* engine) override;
    virtual void UICallback(Engine* engine);
    FileManager m_fileManager;
    bool lastExists = false;

    // logo
    Texture* logoTexture;
    UIElement* logoElement;

    void renderMainMenu();
    void renderExamples();
    void renderAbout();
    
    bool examples = false;
    bool about = false;
};