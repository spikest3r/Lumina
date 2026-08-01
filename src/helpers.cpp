#include "helpers.h"
#include <cmath>

static float VecLength(const Vector3& v) {
    return std::sqrt(v.x * v.x + v.y * v.y + v.z * v.z);
}

void GoToPos(GameObject* obj, GoToPosState& state, Vector3 target,
             std::function<void(GoToPosResult)> callback) {
    state.targetPos = target;
    state.moving = true;
    state.onComplete = std::move(callback);
}

void UpdateGoToPos(GameObject* obj, GoToPosState& state, float dt) {
    if (!state.moving) return;

    Vector3 current = obj->transform.position;
    Vector3 toTarget = state.targetPos - current;
    float dist = VecLength(toTarget);

    bool reached = dist < state.arriveThreshold;

    if (!reached) {
        float len = dist;
        if (len > 0.0001f) {
            Vector3 dir = { toTarget.x / len, toTarget.y / len, toTarget.z / len };
            float step = state.moveSpeed * dt;
            if (step > len) step = len; // don't overshoot the target

            obj->transform.position.x += dir.x * step;
            obj->transform.position.y += dir.y * step;
            obj->transform.position.z += dir.z * step;
        }
    }

    if (reached) {
        state.moving = false;
        auto cb = std::move(state.onComplete);
        state.onComplete = nullptr;
        if (cb) cb(GoToPosResult::Reached);
    }
}