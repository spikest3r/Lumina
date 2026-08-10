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

void WaitUntilGround(GameObject* obj, WaitUntilGroundState& state,
                      std::function<void()> callback) {
    state.waiting = true;
    state.onComplete = std::move(callback);
}

void UpdateWaitUntilGround(GameObject* obj, WaitUntilGroundState& state, Engine* engine,
                             float rayDistance) {
    if (!state.waiting) return;

    Vector3 origin = obj->transform.position;
    Vector3 down = { 0.0f, 0.0f, -1.0f };

    RaycastHit hit = engine->raycast(origin, down, rayDistance);

    bool grounded = (hit.object != nullptr && hit.object != obj);

    if (grounded) {
        state.waiting = false;
        auto cb = std::move(state.onComplete);
        state.onComplete = nullptr;
        if (cb) cb();
    }
}

// Euler (radians, XYZ order) -> Quaternion
Quaternion EulerToQuat(const Vector3& e) {
    float cx = cosf(e.x * 0.5f), sx = sinf(e.x * 0.5f);
    float cy = cosf(e.y * 0.5f), sy = sinf(e.y * 0.5f);
    float cz = cosf(e.z * 0.5f), sz = sinf(e.z * 0.5f);

    return Quaternion{
        sx * cy * cz - cx * sy * sz, // x
        cx * sy * cz + sx * cy * sz, // y
        cx * cy * sz - sx * sy * cz, // z
        cx * cy * cz + sx * sy * sz  // w
    };
}

// Quaternion -> Euler (radians, XYZ order)
Vector3 QuatToEuler(const Quaternion& q) {
    Vector3 e;

    float sinr_cosp = 2.0f * (q.w * q.x + q.y * q.z);
    float cosr_cosp = 1.0f - 2.0f * (q.x * q.x + q.y * q.y);
    e.x = atan2f(sinr_cosp, cosr_cosp);

    float sinp = 2.0f * (q.w * q.y - q.z * q.x);
    e.y = (fabsf(sinp) >= 1.0f) ? copysignf(1.5707963f, sinp) : asinf(sinp);

    float siny_cosp = 2.0f * (q.w * q.z + q.x * q.y);
    float cosy_cosp = 1.0f - 2.0f * (q.y * q.y + q.z * q.z);
    e.z = atan2f(siny_cosp, cosy_cosp);

    return e;
}