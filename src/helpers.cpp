#include "helpers.h"
#include <cmath>
#include <iostream>

static float VecLength(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void GoToPos(GameObject* obj, GoToPosState& state, Vector3 target,
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
 
    Vector3 toTarget = {
        state.targetPos.x - current.x,
        state.targetPos.y - current.y,
        0.0f
    };
    float dist = VecLength(toTarget);
 
    bool reached = dist < state.arriveThreshold;
 
    if (!reached) {
        float len = dist;
        if (len > 0.0001f) {
            Vector3 dir = { toTarget.x / len, toTarget.y / len, 0.0f };
 
            Vector3 rayOrigin = current;
            RaycastHit hit = engine->raycast(rayOrigin, dir, stopDistance);
 
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
 
            obj->transform.rotation = LookRotationYaw(dir);
        }
    }
 
    if (reached) {
        state.moving = false;
        auto cb = std::move(state.onComplete);
        state.onComplete = nullptr;
        if (cb) cb(GoToPosResult::Reached);
    }
}