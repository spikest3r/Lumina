// Lumen Creator

#include <engine.h>
#include "mainmenu/mainmenu.h"
#include "freeplay.h"

#include <filesystem>
#include <iostream>

namespace fs = std::filesystem;

static FreeplayScene* freeplayScene;
static bool requestLoadFreeplay = false;
static bool isFreeplay = false;
static std::string projectToLoad = "";
static bool justLoad = false;

UIFont fontDefault;

void loadFreeplay() {
    if(isFreeplay || requestLoadFreeplay) return;
    requestLoadFreeplay = true;
}

void loadFreeplay(const std::string& projectFile, bool doNotPersist) {
    if(isFreeplay || requestLoadFreeplay) return;
    projectToLoad = projectFile;
    requestLoadFreeplay = true;
    justLoad = doNotPersist;
}

void unloadFreeplay() {
    if(!isFreeplay || requestLoadFreeplay) return;
    requestLoadFreeplay = true;
}

int main(int argc, char* argv[]) {
    // pre-launch flags
    bool launchLast = false;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--last") {
            if(fs::exists("last")) {
                launchLast = true;
                std::string text = static_cast<const std::stringstream&>(std::stringstream() << std::ifstream("last").rdbuf()).str();
                loadFreeplay(text);
            } else {
                std::cout << "Last project is not present!\n";
            }
        }
    }

    Engine* engine = Engine::Create();
	engine->init(800, 600, "Lumina v1.0");
    engine->setClearColor({0.18f, 0.18f, 0.18f});

    #ifdef _WIN32
        const char* fontPath = "C:\\Windows\\Fonts\\consola.ttf";
    #else
        const char* fontPath = "/usr/share/fonts/TTF/Hack-Regular.ttf";
    #endif

    // ToolUI::AddFontFromFileTTF(fontDefault, fontPath, 14.0f);

    MainMenuScene* mainMenuScene = engine->createScene<MainMenuScene>();

    // check if temporary file exists
    if(!launchLast) {
        if(fs::exists("temporary.lumina")) {
            loadFreeplay("temporary.lumina");
        } else {
            engine->loadScene(mainMenuScene);
        }
    }
    
    while (engine->running()) {
        engine->update();
		engine->updateScene();

		engine->render();

        if(requestLoadFreeplay && engine->isLastFrame()) {
            if(!isFreeplay) {
                isFreeplay = true;
                requestLoadFreeplay = false;

                freeplayScene = engine->createScene<FreeplayScene>();
                if(projectToLoad.size() > 0) {
                    freeplayScene->setProjectFile(projectToLoad, justLoad);
                    projectToLoad.clear();
                    justLoad = false;
                }
                engine->loadScene(freeplayScene);
            } else {
                engine->loadScene(mainMenuScene);
                
                isFreeplay = false;
                requestLoadFreeplay = false;

                engine->requestDestroyScene(freeplayScene);
                freeplayScene = nullptr;
            }
        }
    }

    engine->cleanup();
	Engine::Destroy(engine);
}