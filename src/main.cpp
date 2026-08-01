// Lumen Creator

#include <engine.h>
#include "freeplay.h"

int main() {
    Engine* engine = Engine::Create();
	engine->init(800, 600, "Lumen Creator");
    engine->setClearColor({0.18f, 0.18f, 0.18f});

    FreeplayScene* freeplayScene = engine->createScene<FreeplayScene>();
    engine->loadScene(freeplayScene);
    
    while (engine->running()) {
		engine->updateScene();
		engine->update();

		engine->render();
    }

    engine->cleanup();
	Engine::Destroy(engine);
}