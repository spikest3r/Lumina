#include "vm.h"

#include <set>
#include <random>
#include "httplib.h"
#include "objects.h"
#include "helpers.h"
#include "freeplay.h"

std::set<std::string> capabilitySet = {
    "FS", "random", "HTTP"
};

int fileHandleId = 0;
std::unordered_map<int, std::fstream*> fileHandles;

static std::mt19937 rngEngine(std::random_device{}());

std::string toUpper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), ::toupper);
    return s;
}

double getFloat(const Variant& v) {
    if (v.type == TAG_FLOAT) return std::get<double>(v.data);
    if (v.type == TAG_INT) return static_cast<double>(std::get<int64_t>(v.data));
    throw std::runtime_error("getFloat: invalid type");
}

std::unordered_map<int, NativeFn> funcMap = {
    {0x01, [](VMExecutionData& vm) {
        vm.suspended = true;
        auto arg0 = vm.stack.back(); vm.stack.pop_back();
        std::stringstream ss;
        std::visit([&ss](const auto& val) { ss << val; }, arg0.data);
        vm.self->scene->showDialog(vm.self->name + " says", ss.str(), [&vm]() {
            vm.suspended = false;
        });
    }},
    {0x02, [](VMExecutionData& vm) {
        vm.suspended = true;
        auto arg0 = vm.stack.back(); vm.stack.pop_back();
        std::stringstream ss;
        std::visit([&ss](const auto& val) { ss << val; }, arg0.data);
        vm.self->scene->showDialog(vm.self->name + " says", ss.str(), [&vm]() {
            vm.suspended = false;
        });
    }},
    {0x03, [](VMExecutionData& vm) {
        vm.suspended = true;
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        vm.self->scene->showInputDialog(vm.self->name, vm.self->name + " is asking for integer", [&vm, varIndex](std::string input) {
            int64_t result = 0;
            try {
                result = std::stoll(input);
            } catch(...) {
                std::cout << "Invalid value!" << std::endl;
            }
            vm.variables[varIndex].type = TAG_INT;
            vm.variables[varIndex].data = result;
            vm.suspended = false;
        });
    }},
    {0x04, [](VMExecutionData& vm) {
        vm.suspended = true;
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        vm.self->scene->showInputDialog(vm.self->name, vm.self->name + " is asking for string", [&vm, varIndex](std::string input) {
            vm.variables[varIndex].type = TAG_STRING;
            vm.variables[varIndex].data = input;
            vm.suspended = false;
        });
    }},
    {0x05, [](VMExecutionData& vm) {
        // str2int
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        int num = 0;
        std::string str = "0";
        str = std::get<std::string>(value.data);

        num = std::stoi(str);

        vm.variables[varIndex].type = TAG_INT;
        vm.variables[varIndex].data = num;
    }},
    {0x06, [](VMExecutionData& vm) {
        // int2str
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        int num = 0;
        num = getInt(value);

        std::string str = std::to_string(num);
        vm.variables[varIndex].type = TAG_STRING;
        vm.variables[varIndex].data = str;
    }},
    {0x07, [](VMExecutionData& vm) {
        // str2float
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        double num = 0.0;
        std::string str = "0";
        str = std::get<std::string>(value.data);

        try {
            num = std::stod(str);
        } catch (const std::invalid_argument& e) {
            num = 0.0;
        } catch (const std::out_of_range& e) {
            num = 0.0;
        }

        vm.variables[varIndex].type = TAG_FLOAT;
        vm.variables[varIndex].data = num;
    }},
    {0x08, [](VMExecutionData& vm) {
        // float2str
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        double num = 0.0;
        if(value.type == TAG_FLOAT) num = std::get<double>(value.data);
        else if(value.type == TAG_INT) num = static_cast<double>(getInt(value)); // accept int too, same leniency as int2str only handling its own type

        std::string str = std::to_string(num);
        vm.variables[varIndex].type = TAG_STRING;
        vm.variables[varIndex].data = str;
    }},
    // stdlib impl
    {0xA0, [](VMExecutionData& vm) {
        // assertCapability
        auto value = vm.stack.back(); vm.stack.pop_back();
        if(value.type != TAG_STRING) {
            throw std::runtime_error("assertCapability failed: invalid value type");
        }
        auto str = std::get<std::string>(value.data);
        auto it = capabilitySet.find(str);
        if(it == capabilitySet.end()) {
            std::stringstream ss;
            ss << "assertCapability failed: capability " << str << " is not present";
            throw std::runtime_error(ss.str());
        }

        // capability present, proceed with execution
    }},
    {0xA1, [](VMExecutionData& vm) {
        // openFile
        auto handleVarIndex = getInt(vm.stack.back()); vm.stack.pop_back();

        auto value = vm.stack.back(); vm.stack.pop_back();

        auto filename = std::get<std::string>(value.data);

        auto stream = new std::fstream(filename, std::ios::in | std::ios::out | std::ios::trunc);
        if(!stream->is_open()) {
            throw std::runtime_error("openFile failed: unable to open file " + filename);
        }

        fileHandles[fileHandleId] = stream;

        vm.variables[handleVarIndex].type = TAG_INT;
        vm.variables[handleVarIndex].data = fileHandleId++;
    }},
    {0xA2, [](VMExecutionData& vm) {
        // writeFile
        auto handle = getInt(vm.stack.back()); vm.stack.pop_back();

        auto value = vm.stack.back(); vm.stack.pop_back();

        auto valueToWrite = std::get<std::string>(value.data);

        auto it = fileHandles.find(handle);
        if(it != fileHandles.end()) {
            auto f = it->second;
            *f << valueToWrite;
        } else {
            throw std::runtime_error("writeFile failed: invalid file handle");
        }
    }},
    {0xA3, [](VMExecutionData& vm) {
        // readFile
        auto handle = getInt(vm.stack.back()); vm.stack.pop_back();
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();

        auto it = fileHandles.find(handle);
        if(it != fileHandles.end()) {
            auto f = it->second;
            std::string contents((std::istreambuf_iterator<char>(*f)), std::istreambuf_iterator<char>());

            vm.variables[varIndex].type = TAG_STRING;
            vm.variables[varIndex].data = contents;
        } else {
            throw std::runtime_error("readFile failed: invalid file handle");
        }
    }},
    {0xA4, [](VMExecutionData& vm) {
        // closeFile
        auto handle = getInt(vm.stack.back()); vm.stack.pop_back();

        auto it = fileHandles.find(handle);
        if(it != fileHandles.end()) {
            auto f = it->second;
            f->close();
            fileHandles.erase(it);
        } else {
            throw std::runtime_error("writeFile failed: invalid file handle");
        }
    }},
    {0xA5, [](VMExecutionData& vm) {
        // randomSeed
        auto seed = getInt(vm.stack.back()); vm.stack.pop_back();

        rngEngine.seed(seed);
    }},
    {0xA6, [](VMExecutionData& vm) {
        // random
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();

        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        float val = dist(rngEngine);

        vm.variables[varIndex].type = TAG_FLOAT;
        vm.variables[varIndex].data = val;
    }},
    {0xA7, [](VMExecutionData& vm) {
        // randomRange
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto max = getInt(vm.stack.back()); vm.stack.pop_back();
        auto min = getInt(vm.stack.back()); vm.stack.pop_back();

        std::uniform_int_distribution<int64_t> dist(min, max); // inclusive on both ends
        int64_t val = dist(rngEngine);

        vm.variables[varIndex].type = TAG_INT;
        vm.variables[varIndex].data = val;
    }},
    {0xA8, [](VMExecutionData& vm) {
        // httpRequest
        auto outVarIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto statusVarIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto body = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        auto headerStr = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        auto url = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        auto method = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();

        int outStatus;
        std::string outResponse;

        std::string host, path;
        if (!splitUrl(url, host, path)) {
            throw std::runtime_error("httpGet failed: invalid url");
        }

        httplib::Client cli(host);
        cli.set_connection_timeout(5, 0);
        cli.set_read_timeout(10, 0);
        cli.set_follow_location(true);

        httplib::Headers headers = parseHeaders(headerStr);
        std::string m = toUpper(method);

        httplib::Result res;
        if (m == "GET") {
            res = cli.Get(path, headers);
        } else if (m == "POST") {
            res = cli.Post(path, headers, body, "application/octet-stream");
        } else if (m == "PUT") {
            res = cli.Put(path, headers, body, "application/octet-stream");
        } else if (m == "DELETE") {
            res = cli.Delete(path, headers);
        } else {
            throw std::runtime_error("unsupported method: " + method);
        }

        if (res) {
            outStatus = res->status;
            outResponse = res->body;
        } else {
            outStatus = -1;
            outResponse = "request failed: " + httplib::to_string(res.error());
        }

        vm.variables[outVarIndex].type = TAG_STRING;
        vm.variables[outVarIndex].data = outResponse;

        vm.variables[statusVarIndex].type = TAG_INT;
        vm.variables[statusVarIndex].data = outStatus;
    }},
    {0xA9, [](VMExecutionData& vm) {
        // strlen
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        auto str = std::get<std::string>(value.data);

        vm.variables[varIndex].type = TAG_INT;
        vm.variables[varIndex].data = static_cast<int64_t>(str.size());
    }},
    {0xAA, [](VMExecutionData& vm) {
        // substr(s, start, len, &out)
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto lenArg = getInt(vm.stack.back()); vm.stack.pop_back();
        auto startArg = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        auto str = std::get<std::string>(value.data);

        std::string result;
        if (startArg >= 0 && static_cast<size_t>(startArg) < str.size()) {
            result = str.substr(startArg, lenArg);
        }

        vm.variables[varIndex].type = TAG_STRING;
        vm.variables[varIndex].data = result;
    }},
    {0xAB, [](VMExecutionData& vm) {
        // strfind(s, needle, &index)
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto needleVal = vm.stack.back(); vm.stack.pop_back();
        auto strVal = vm.stack.back(); vm.stack.pop_back();

        auto str = std::get<std::string>(strVal.data);
        auto needle = std::get<std::string>(needleVal.data);

        auto pos = str.find(needle);
        int64_t result = (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);

        vm.variables[varIndex].type = TAG_INT;
        vm.variables[varIndex].data = result;
    }},
    {0xAC, [](VMExecutionData& vm) {
        // toUpper(s, &out) / toLower(s, &out) via a flag arg (0=lower, 1=upper)
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto upperFlag = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        auto str = std::get<std::string>(value.data);

        if (upperFlag) {
            std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        } else {
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        }

        vm.variables[varIndex].type = TAG_STRING;
        vm.variables[varIndex].data = str;
    }},
    {0xAD, [](VMExecutionData& vm) {
        // trim(s, &out)
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto value = vm.stack.back(); vm.stack.pop_back();

        auto str = std::get<std::string>(value.data);

        const char* ws = " \t\n\r\f\v";
        size_t start = str.find_first_not_of(ws);
        size_t end = str.find_last_not_of(ws);

        std::string result = (start == std::string::npos) ? "" : str.substr(start, end - start + 1);

        vm.variables[varIndex].type = TAG_STRING;
        vm.variables[varIndex].data = result;
    }},
    // extended opcode set
    {0xB0, [](VMExecutionData& vm) {
        // goToPos x, y
        float y = getFloat(vm.stack.back()); vm.stack.pop_back();
        float x = getFloat(vm.stack.back()); vm.stack.pop_back();

        vm.suspended = true;

        GoToPos(vm.self, vm.self->goToState, {x, y}, [&vm](GoToPosResult result) {
            // vm.stack.push_back(Variant{TAG_INT, (int64_t)(result == GoToPosResult::Reached ? 1 : 0)});
            vm.suspended = false;
        });
    }},
    {0xB1, [](VMExecutionData& vm) {
        // waitUntilGround
        vm.suspended = true;

        WaitUntilGround(vm.self, vm.self->waitGroundState, [&vm]() {
            vm.suspended = false;
        });
    }},
    {0xB2, [](VMExecutionData& vm) {
        // setPos x, y, z
        Vector3* pos = &vm.self->transform.position;
        pos->z = getNumeric(vm.stack.back());
        vm.stack.pop_back();
        pos->y = getNumeric(vm.stack.back());
        vm.stack.pop_back();
        pos->x = getNumeric(vm.stack.back());
        vm.stack.pop_back();
    }},
    {0xB3, [](VMExecutionData& vm) {
        // setRot x, y, z
        constexpr float DEG_TO_RAD = 3.14159265358979323846f / 180.0f;
        // build euler angle
        Vector3 rotation;
        rotation.z = getFloat(vm.stack.back());
        vm.stack.pop_back();
        rotation.y = getFloat(vm.stack.back());
        vm.stack.pop_back();
        rotation.x = getFloat(vm.stack.back());
        vm.stack.pop_back();
        // convert to rad
        rotation.x *= DEG_TO_RAD;
        rotation.y *= DEG_TO_RAD;
        rotation.z *= DEG_TO_RAD;
        // convert to quat
        auto quat = EulerToQuat(rotation);
        vm.self->transform.rotation = quat;
    }},
    {0xB4, [](VMExecutionData& vm) {
        // isTouching 'object', ptr
        auto ptr = getInt(vm.stack.back()); vm.stack.pop_back();
        auto str = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        
        bool colliding = vm.self->isTouching(str);

        vm.variables[ptr].type = TAG_INT;
        vm.variables[ptr].data = (int)colliding;
    }},
    {0xB5, [](VMExecutionData& vm) {
        // askInt 'message', ptr
        vm.suspended = true;
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto message = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        vm.self->scene->showInputDialog(vm.self->name, message, [&vm, varIndex](std::string input) {
            int64_t result = 0;
            try {
                result = std::stoll(input);
            } catch(...) {
                std::cout << "Invalid value!" << std::endl;
            }
            vm.variables[varIndex].type = TAG_INT;
            vm.variables[varIndex].data = result;
            vm.suspended = false;
        });
    }},
    {0xB6, [](VMExecutionData& vm) {
        // askStr 'message', ptr
        vm.suspended = true;
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto message = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        vm.self->scene->showInputDialog(vm.self->name, message, [&vm, varIndex](std::string input) {
            vm.variables[varIndex].type = TAG_STRING;
            vm.variables[varIndex].data = input;
            vm.suspended = false;
        });
    }},
    {0xB7, [](VMExecutionData& vm) {
        // getPos &x, &y, &z
        auto zIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto yIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        auto xIndex = getInt(vm.stack.back()); vm.stack.pop_back();

        vm.variables[zIndex].type = TAG_FLOAT;
        vm.variables[yIndex].type = TAG_FLOAT;
        vm.variables[xIndex].type = TAG_FLOAT;

        vm.variables[zIndex].data = vm.self->transform.position.z;
        vm.variables[yIndex].data = vm.self->transform.position.y;
        vm.variables[xIndex].data = vm.self->transform.position.x;
    }},
    {0xB8, [](VMExecutionData& vm) {
        // isKeyDown 'key', var
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        vm.variables[varIndex].type = TAG_INT;
        auto keyName = std::get<std::string>(vm.stack.back().data); vm.stack.pop_back();
        auto keyCode = KeyCodeFromString(keyName);
        if(keyCode) {
            auto state = vm.self->engPtr->getKey(keyCode.value());
            vm.variables[varIndex].data = (int64_t)(state == PRESS);
        } else {
            vm.variables[varIndex].data = 0;
        }
    }},
    {0xB9, [](VMExecutionData& vm) {
        // setPosXY x, y
        Vector3* pos = &vm.self->transform.position;
        pos->y = getNumeric(vm.stack.back());
        vm.stack.pop_back();
        pos->x = getNumeric(vm.stack.back());
        vm.stack.pop_back();
    }},
    {0xBA, [](VMExecutionData& vm) {
        auto varIndex = getInt(vm.stack.back()); vm.stack.pop_back();
        vm.variables[varIndex].type = TAG_INT;

        Quaternion q = vm.self->transform.rotation;
        Vector3 forward = {
            2.0f * q.z * q.w,
            -(q.w * q.w - q.z * q.z),
            0.0f
        };

        RaycastHit hit = vm.self->engPtr->raycast(
            vm.self->transform.position, forward, 0.5f
        );

        bool blocked = hit.object != nullptr && hit.object != vm.self;
        vm.variables[varIndex].data = blocked ? 1 : 0;
    }},
    {0xBB, [](VMExecutionData& vm) {
        // moveForward distance
        float dist = getNumeric(vm.stack.back()); vm.stack.pop_back();

        Quaternion q = vm.self->transform.rotation;
        Vector3 forward = {
            2.0f * q.z * q.w,
            -(q.w * q.w - q.z * q.z),
            0.0f
        };

        vm.self->transform.position.x += forward.x * dist;
        vm.self->transform.position.y += forward.y * dist;
    }},
    {0xBC, [](VMExecutionData& vm) {
        // waitMs time
        vm.suspended = true;
        auto time = getNumeric(vm.stack.back()); vm.stack.pop_back();
        Wait(vm.self->waitState, time, [&vm]() {
            vm.suspended = false;
        });
    }},
    {0xBD, [](VMExecutionData& vm) {
        // destroySelf
        vm.suspended = true; // will be destroyed anyway
        vm.self->scene->runtimeDestroyInteractive(vm.self);
    }},
    {0xBE, [](VMExecutionData& vm) {
        // TODO: reserved for future use
    }},
    {0xBF, [](VMExecutionData& vm) {
        // stopAll - stops level execution
        vm.suspended = true;
        vm.self->scene->stopExecution();
    }}
};