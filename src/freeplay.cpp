#include "freeplay.h"
#include <iostream>
#include <engine_tool_ui.h>
#include <shapes.h>

#include "lumen-inc/compiler.h"
#include "lumen-inc/vm.h"
#include "lumen-inc/programfile.h"

#include "helpers.h"
#include "objects.h"

constexpr Vector2 toolbarButtonSize = {110,50};

static Transform baseTransform = {
    {0.0f,0.0f,0.0f},
    {0.0f,0.0f,0.0f,1.0f},
    {1.0f,1.0f,1.0f}
};

void ComputeOrbitCamera(float yawDegrees, float pitchDegrees, float distance,
                         Vector3& outPosition, Vector3& outRotation) {
    float yaw   = yawDegrees   * (3.14159265358979323846f / 180.0f);
    float pitch = pitchDegrees * (3.14159265358979323846f / 180.0f);

    // Direction the camera is FACING (toward origin)
    Vector3 front;
    front.x = cosf(yaw) * cosf(pitch);
    front.y = sinf(yaw) * cosf(pitch);
    front.z = sinf(pitch);

    // Camera sits on the opposite side of that direction, at `distance`
    outPosition.x = -front.x * distance;
    outPosition.y = -front.y * distance;
    outPosition.z = -front.z * distance;

    outRotation.x = pitchDegrees;
    outRotation.y = yawDegrees;
    outRotation.z = 0.0f;
}

void ApplyFakeGravity(GameObject* obj, Engine* engine, float dt, float fallSpeed = 9.8f, float groundOffset = 0.05f)
{
    Vector3 pos = obj->transform.position;
    Vector3 down = { 0.0f, 0.0f, -1.0f };

    RaycastHit hit = engine->raycast(pos, down, 100.0f);

    if (hit.object != nullptr)
    {
        float groundZ = pos.z - hit.distance;
        float distToGround = pos.z - groundZ;

        if (distToGround > groundOffset)
        {
            float fallStep = fallSpeed * dt;

            if (fallStep > distToGround - groundOffset)
                fallStep = distToGround - groundOffset;

            obj->transform.position.z -= fallStep;
        }
        else
        {
            obj->transform.position.z = groundZ + groundOffset;
        }
    }
    else
    {
        // No ground found — keep falling
        obj->transform.position.z -= fallSpeed * dt;
    }
}

void FreeplayScene::InitScene(Engine* engine) {
    engine->SetUICallback([this](Engine* engine)
    {
        this->UICallback(engine);
    });

    baseMaterial = engine->createPhysicsMaterial(0.5f,0.5f,0.5f);

    constructGameObjects(engine);

    ComputeOrbitCamera(135.0f, -30.0f, 25.0f, engine->cameraPosition, engine->cameraRotation);

    std::cout << "Freeplay scene loaded\n";
}

void FreeplayScene::UICallback(Engine* engine) {
    Vector2 extents = engine->getExtents();

    ToolUI::SetNextWindowPos({0, 0});
    ToolUI::SetNextWindowSize({extents.x, 70});
    ToolUI::Begin("##toolbar", true);
    if(ToolUI::Button("Run", toolbarButtonSize)) {
        CompilerData data;
        int status = compile(sourceCode, &data);
        if(status != 0) {
            std::cout << "Failed to compile\n";
        } else {
            playerPuppet->program.bytecode = std::move(data.bytecode);
            playerPuppet->program.stringPool = std::move(data.stringPool);
            playerPuppet->program.constPool = std::move(data.constPool);
            playerPuppet->program.variableCount = data.variableCount;

            for(auto& object: objects) {
                object->resetVM();
            }
            
            running = true;
        }
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
    float dt = engine->getDeltaTime();

    // TODO: Into scene graph
    if(running) {
        for(auto& object: objects) {
            object->stepVM();
            UpdateGoToPos(object, object->goToState, dt, engine);
        }
    }

    ApplyFakeGravity(playerPuppet, engine, dt);
}

void FreeplayScene::DestroyScene(Engine* engine) {

}

void FreeplayScene::constructGameObjects(Engine* engine) {
    Texture* texture = engine->createTexture("white", "assets/textures/white.png");
    Mesh* planeMesh = engine->createMesh("plane", "assets/models/floor.obj");
    auto planeTrans = baseTransform;
    plane = engine->createGameObject<GameObject>(
        planeTrans, planeMesh, texture, baseMaterial, false
    );

    // load puppet
    Texture* puppetTexture = engine->createTexture("puppetTexture", "assets/textures/puppet.png");
    Mesh* puppetMesh = engine->createMesh("puppetMesh", "assets/models/puppet.obj");
    objectPool["puppet"] = {puppetMesh, puppetTexture};

    // create player puppet
    auto puppetTransform = baseTransform;
    puppetTransform.position.z = 10.0f;
    playerPuppet = engine->createGameObject<Puppet>(
        puppetTransform, puppetMesh, puppetTexture, baseMaterial, false
    );
    sceneObjects["player"] = playerPuppet;
    objects.push_back(playerPuppet);
}