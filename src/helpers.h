#pragma once
#include <engine.h>
#include <functional>
#include <gameobject.h>
#include <engine_types.h>
#include <optional>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <filesystem>

namespace fs = std::filesystem;

enum class GoToPosResult { Reached, Blocked };

struct GoToPosState {
    Vector2 targetPos;
    bool moving = false;
    float stuckTimer = 0.0f;
    Vector3 lastPos;
    std::function<void(GoToPosResult)> onComplete;

    static constexpr float arriveThreshold = 0.2f;
    static constexpr float stuckVelocityEpsilon = 0.05f;
    static constexpr float stuckTimeout = 0.5f;
    static constexpr float moveSpeed = 2.5f;
};

struct WaitUntilGroundState {
    bool waiting = false;
    std::function<void()> onComplete;
};

struct WaitState {
    bool active = false;
    float time = 0.0f;
    std::function<void()> onComplete;
};

void GoToPos(GameObject* obj, GoToPosState& state, Vector2 target,
             std::function<void(GoToPosResult)> callback);


void UpdateGoToPos(GameObject* obj, GoToPosState& state, float dt, Engine* engine,
                   float stopDistance = 0.5f);

void WaitUntilGround(GameObject* obj, WaitUntilGroundState& state,
                      std::function<void()> callback);

void UpdateWaitUntilGround(GameObject* obj, WaitUntilGroundState& state, Engine* engine,
                             float rayDistance);

void Wait(WaitState& state, const float& time, std::function<void()> callback);

void UpdateWait(WaitState& state, Engine* engine);

Vector3 QuatToEuler(const Quaternion& q);
Quaternion EulerToQuat(const Vector3& e);

enum class CameraMovementAxis {
    Forward,
    Backward,
    StrafeLeft,
    StrafeRight
};

// Computes a normalized horizontal (XY-plane) direction vector for the given
// camera yaw (degrees) and movement axis. Only yaw is considered; camera
// pitch/roll and any puppet rotation have no bearing on the result. This is
// intentionally decoupled from the VM/blocks so input handlers (e.g. a future
// WASD controller) can call it directly every frame.
Vector3 GetCameraRelativeDirection(float cameraYawDegrees, CameraMovementAxis axis);

std::optional<KeyCode> KeyCodeFromString(const std::string& name);

std::optional<float> ToFloat(const std::string& str);

void DeleteFileAsync(const std::string& filePath);

fs::path getUserDataDirectory();
fs::path getTemporaryProjectPath();
fs::path getLastProjectPath();
bool readFileToString(const fs::path& path, std::string& output);