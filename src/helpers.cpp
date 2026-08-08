#include "helpers.h"
#include <cmath>
#include <iostream>

static float VecLength(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

static float VecLength(const Vector2& v) {
    return std::sqrt(v.x * v.x + v.y * v.y);
}

void GoToPos(GameObject* obj, GoToPosState& state, Vector2 target,
             std::function<void(GoToPosResult)> callback) {
    state.targetPos = target;
    state.moving = true;
    state.onComplete = std::move(callback);
}

Quaternion LookRotationYaw(Vector3 direction) {
    float len = std::sqrt(direction.x * direction.x + direction.y * direction.y);
    if (len < 0.0001f) {
        return { 0.0f, 0.0f, 0.0f, 1.0f };
    }

    float dx = direction.x / len;
    float dy = direction.y / len;

    float yaw = std::atan2(direction.y, direction.x) + (M_PI / 2.0f);

    float halfYaw = yaw * 0.5f;
    Quaternion q;
    q.x = 0.0f;
    q.y = 0.0f;
    q.z = std::sin(halfYaw);
    q.w = std::cos(halfYaw);
    return q;
}

void UpdateGoToPos(GameObject* obj, GoToPosState& state, float dt, Engine* engine,
                   float stopDistance) {
    if (!state.moving) return;

    Vector3 current = obj->transform.position;
    Vector2 currentXY = { current.x, current.y };

    Vector2 toTarget = {
        state.targetPos.x - currentXY.x,
        state.targetPos.y - currentXY.y
    };

    float dist = VecLength(toTarget);
    bool reached = dist < state.arriveThreshold;

    if (!reached) {
        float len = dist;
        if (len > 0.0001f) {
            Vector2 dir = { toTarget.x / len, toTarget.y / len };

            Vector3 rayOrigin = current;
            Vector3 rayDir = { dir.x, dir.y, 0.0f };
            RaycastHit hit = engine->raycast(rayOrigin, rayDir, stopDistance);

            if (hit.object != nullptr && hit.object != obj) {
                // Something's in the way within stopDistance
                state.moving = false;
                auto cb = std::move(state.onComplete);
                state.onComplete = nullptr;
                if (cb) cb(GoToPosResult::Blocked);
                return;
            }

            float step = state.moveSpeed * dt;
            if (step > len) step = len; // don't overshoot the target

            obj->transform.position.x += dir.x * step;
            obj->transform.position.y += dir.y * step;
            obj->transform.rotation = LookRotationYaw(rayDir);
        }
    }

    if (reached) {
        state.moving = false;
        auto cb = std::move(state.onComplete);
        state.onComplete = nullptr;
        if (cb) cb(GoToPosResult::Reached);
    }
}