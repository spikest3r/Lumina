#pragma once

#include <engine.h>
#include <scene.h>

class FreeplayScene : public Scene {
protected:
    void InitScene(Engine* engine) override;
    void UpdateScene(Engine* engine) override;
    void DestroyScene(Engine* engine) override;
    
    virtual void UICallback(Engine* engine);

    void requestRun();

    std::string sourceCode; // for editor and blocks intermediate
    bool shouldRun = false;
};