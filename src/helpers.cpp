#include "helpers.h"
#include <cmath>
#include <iostream>
#include <unordered_map>
#include <string>
#include <optional>
#include <filesystem>
#include <thread>

namespace fs = std::filesystem;

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


void Wait(WaitState& state, const float& time, std::function<void()> callback) {
    state.time = time;
    state.onComplete = callback;
    state.active = true;
}

void UpdateWait(WaitState& state, Engine* engine) {
    if(!state.active) return;
    float dt = engine->getDeltaTime();
    state.time -= dt * 1000;
    if(state.time <= 0.0f) {
        state.time = 0.0f;
        state.active = false;
        if(state.onComplete) {
            state.onComplete();
        }
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

Vector3 GetCameraRelativeDirection(float cameraYawDegrees, CameraMovementAxis axis) {
    constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
    float yaw = cameraYawDegrees * DEG_TO_RAD;

    Vector3 forward = { cosf(yaw), sinf(yaw), 0.0f };
    Vector3 right   = { sinf(yaw), -cosf(yaw), 0.0f };

    switch (axis) {
        case CameraMovementAxis::Forward:
            return forward;
        case CameraMovementAxis::Backward:
            return { -forward.x, -forward.y, 0.0f };
        case CameraMovementAxis::StrafeRight:
            return right;
        case CameraMovementAxis::StrafeLeft:
            return { -right.x, -right.y, 0.0f };
    }
    return { 0.0f, 0.0f, 0.0f };
}

inline const std::unordered_map<std::string, KeyCode>& keyNameMap() {
    static const std::unordered_map<std::string, KeyCode> map = {
        {"Space", KeyCode::Space},
        {"Apostrophe", KeyCode::Apostrophe},
        {"Comma", KeyCode::Comma},
        {"Minus", KeyCode::Minus},
        {"Period", KeyCode::Period},
        {"Slash", KeyCode::Slash},
        {"Key0", KeyCode::Key0}, {"Key1", KeyCode::Key1}, {"Key2", KeyCode::Key2},
        {"Key3", KeyCode::Key3}, {"Key4", KeyCode::Key4}, {"Key5", KeyCode::Key5},
        {"Key6", KeyCode::Key6}, {"Key7", KeyCode::Key7}, {"Key8", KeyCode::Key8},
        {"Key9", KeyCode::Key9},
        {"Semicolon", KeyCode::Semicolon},
        {"Equal", KeyCode::Equal},
        {"A", KeyCode::A}, {"B", KeyCode::B}, {"C", KeyCode::C}, {"D", KeyCode::D},
        {"E", KeyCode::E}, {"F", KeyCode::F}, {"G", KeyCode::G}, {"H", KeyCode::H},
        {"I", KeyCode::I}, {"J", KeyCode::J}, {"K", KeyCode::K}, {"L", KeyCode::L},
        {"M", KeyCode::M}, {"N", KeyCode::N}, {"O", KeyCode::O}, {"P", KeyCode::P},
        {"Q", KeyCode::Q}, {"R", KeyCode::R}, {"S", KeyCode::S}, {"T", KeyCode::T},
        {"U", KeyCode::U}, {"V", KeyCode::V}, {"W", KeyCode::W}, {"X", KeyCode::X},
        {"Y", KeyCode::Y}, {"Z", KeyCode::Z},
        {"LeftBracket", KeyCode::LeftBracket},
        {"Backslash", KeyCode::Backslash},
        {"RightBracket", KeyCode::RightBracket},
        {"GraveAccent", KeyCode::GraveAccent},
        {"World1", KeyCode::World1}, {"World2", KeyCode::World2},

        {"Escape", KeyCode::Escape}, {"Enter", KeyCode::Enter}, {"Tab", KeyCode::Tab},
        {"Backspace", KeyCode::Backspace}, {"Insert", KeyCode::Insert}, {"Delete", KeyCode::Delete},
        {"Right", KeyCode::Right}, {"Left", KeyCode::Left}, {"Down", KeyCode::Down}, {"Up", KeyCode::Up},
        {"PageUp", KeyCode::PageUp}, {"PageDown", KeyCode::PageDown},
        {"Home", KeyCode::Home}, {"End", KeyCode::End},
        {"CapsLock", KeyCode::CapsLock}, {"ScrollLock", KeyCode::ScrollLock},
        {"NumLock", KeyCode::NumLock}, {"PrintScreen", KeyCode::PrintScreen}, {"Pause", KeyCode::Pause},
        {"F1", KeyCode::F1}, {"F2", KeyCode::F2}, {"F3", KeyCode::F3}, {"F4", KeyCode::F4},
        {"F5", KeyCode::F5}, {"F6", KeyCode::F6}, {"F7", KeyCode::F7}, {"F8", KeyCode::F8},
        {"F9", KeyCode::F9}, {"F10", KeyCode::F10}, {"F11", KeyCode::F11}, {"F12", KeyCode::F12},
        {"F13", KeyCode::F13}, {"F14", KeyCode::F14}, {"F15", KeyCode::F15}, {"F16", KeyCode::F16},
        {"F17", KeyCode::F17}, {"F18", KeyCode::F18}, {"F19", KeyCode::F19}, {"F20", KeyCode::F20},
        {"F21", KeyCode::F21}, {"F22", KeyCode::F22}, {"F23", KeyCode::F23}, {"F24", KeyCode::F24},
        {"F25", KeyCode::F25},
        {"KP0", KeyCode::KP0}, {"KP1", KeyCode::KP1}, {"KP2", KeyCode::KP2}, {"KP3", KeyCode::KP3},
        {"KP4", KeyCode::KP4}, {"KP5", KeyCode::KP5}, {"KP6", KeyCode::KP6}, {"KP7", KeyCode::KP7},
        {"KP8", KeyCode::KP8}, {"KP9", KeyCode::KP9},
        {"KPDecimal", KeyCode::KPDecimal}, {"KPDivide", KeyCode::KPDivide},
        {"KPMultiply", KeyCode::KPMultiply}, {"KPSubtract", KeyCode::KPSubtract},
        {"KPAdd", KeyCode::KPAdd}, {"KPEnter", KeyCode::KPEnter}, {"KPEqual", KeyCode::KPEqual},
        {"LeftShift", KeyCode::LeftShift}, {"LeftControl", KeyCode::LeftControl},
        {"LeftAlt", KeyCode::LeftAlt}, {"LeftSuper", KeyCode::LeftSuper},
        {"RightShift", KeyCode::RightShift}, {"RightControl", KeyCode::RightControl},
        {"RightAlt", KeyCode::RightAlt}, {"RightSuper", KeyCode::RightSuper},
        {"Menu", KeyCode::Menu},
    };
    return map;
}

std::optional<KeyCode> KeyCodeFromString(const std::string& name) {
    const auto& map = keyNameMap();
    auto it = map.find(name);
    if (it == map.end()) return std::nullopt;
    return it->second;
}

std::optional<float> ToFloat(const std::string& str) {
    try {
        size_t pos;
        float value = std::stof(str, &pos);

        if (pos != str.size())
            return std::nullopt;

        return value;
    } catch (...) {
        return std::nullopt;
    }
}

void DeleteFileAsync(const std::string& filePath) {
    std::thread([filePath]() {
        std::error_code ec;
        bool removed = fs::remove(filePath, ec);

        if (ec) {
            std::cerr << "Failed to delete file: " << ec.message() << '\n';
        } else if (removed) {
            std::cout << "File deleted successfully.\n";
        } else {
            std::cout << "File did not exist.\n";
        }
    }).detach();
}

fs::path getUserDataDirectory() {
#ifdef _WIN32
    const char* appData = std::getenv("APPDATA");

    if (appData) {
        return fs::path(appData) / "Lumina";
    }

    const char* userProfile = std::getenv("USERPROFILE");

    if (userProfile) {
        return fs::path(userProfile) / "AppData" / "Roaming" / "Lumina";
    }

    return fs::path("Lumina");
#else
    const char* xdgDataHome = std::getenv("XDG_DATA_HOME");

    if (xdgDataHome && *xdgDataHome) {
        return fs::path(xdgDataHome) / "lumina";
    }

    const char* home = std::getenv("HOME");

    if (home && *home) {
        return fs::path(home) / ".local" / "share" / "lumina";
    }

    return fs::path(".lumina");
#endif
}

fs::path getTemporaryProjectPath() {
    return getUserDataDirectory() / "temporary.lumina";
}

fs::path getLastProjectPath() {
    return getUserDataDirectory() / "last";
}

bool readFileToString(const fs::path& path, std::string& output) {
    std::ifstream file(path);

    if (!file.is_open()) {
        return false;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    output = buffer.str();

    return true;
}