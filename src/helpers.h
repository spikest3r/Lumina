#pragma once
#include <engine.h>
#include <functional>
#include <gameobject.h>

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

void GoToPos(GameObject* obj, GoToPosState& state, Vector2 target,
             std::function<void(GoToPosResult)> callback);


void UpdateGoToPos(GameObject* obj, GoToPosState& state, float dt, Engine* engine,
                   float stopDistance = 0.5f);

void WaitUntilGround(GameObject* obj, WaitUntilGroundState& state,
                      std::function<void()> callback);

void UpdateWaitUntilGround(GameObject* obj, WaitUntilGroundState& state, Engine* engine,
                             float rayDistance);

Vector3 QuatToEuler(const Quaternion& q);
Quaternion EulerToQuat(const Vector3& e);