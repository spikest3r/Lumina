// Lumen Creator

#include <engine.h>
#include "mainmenu/mainmenu.h"
#include "freeplay.h"
#include "helpers.h"

#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
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
    if (isFreeplay || requestLoadFreeplay) return;
    requestLoadFreeplay = true;
}

void loadFreeplay(const std::string& projectFile, bool doNotPersist) {
    if (isFreeplay || requestLoadFreeplay) return;
    projectToLoad = projectFile;
    requestLoadFreeplay = true;
    justLoad = doNotPersist;
}

void unloadFreeplay() {
    if (!isFreeplay || requestLoadFreeplay) return;
    requestLoadFreeplay = true;
}

int main(int argc, char* argv[]) {
    const fs::path userDataDirectory = getUserDataDirectory();

    // Ensure Lumina's writable user-data directory exists.
    std::error_code ec;
    fs::create_directories(userDataDirectory, ec);

    if (ec) {
        std::cerr
            << "Warning: failed to create Lumina user data directory: "
            << userDataDirectory << "\n";
    }

    // Pre-launch flags
    bool launchLast = false;

    for (int i = 1; i < argc; ++i) {
        if (std::string(argv[i]) == "--last") {
            const fs::path lastProject = getLastProjectPath();

            if (fs::exists(lastProject)) {
                launchLast = true;

                std::string text;

                if (readFileToString(lastProject, text)) {
                    loadFreeplay(text, false);
                } else {
                    std::cout << "Failed to read last project!\n";
                }
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

    MainMenuScene* mainMenuScene =
        engine->createScene<MainMenuScene>();

    // Check if temporary file exists.
    if (!launchLast) {
        const fs::path temporaryProject = getTemporaryProjectPath();

        if (fs::exists(temporaryProject)) {
            loadFreeplay(temporaryProject.string(), false);
        } else {
            engine->loadScene(mainMenuScene);
        }
    }

    while (engine->running()) {
        engine->update();
        engine->updateScene();

        engine->render();

        if (requestLoadFreeplay && engine->isLastFrame()) {
            if (!isFreeplay) {
                isFreeplay = true;
                requestLoadFreeplay = false;

                freeplayScene =
                    engine->createScene<FreeplayScene>();

                if (!projectToLoad.empty()) {
                    freeplayScene->setProjectFile(
                        projectToLoad,
                        justLoad
                    );

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