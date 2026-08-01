#include "vm.h"

#include <set>
#include <random>
#include "httplib.h"

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

std::unordered_map<int, NativeFn> funcMap = {
    {0x01, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        auto arg0 = stack.back(); stack.pop_back();
        std::visit([](const auto& val) { std::cout << val; }, arg0.data);
        std::cout << std::endl;
    }},
    {0x02, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        auto arg0 = stack.back(); stack.pop_back();
        std::visit([](const auto& val) { std::cout << val; }, arg0.data);
    }},
    {0x03, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        auto varIndex = getInt(stack.back()); stack.pop_back();
        std::string input;
        std::cin >> input;
        int64_t result = 0;
        try {
            result = std::stoll(input);
        } catch(...) {
            std::cout << "Invalid value!" << std::endl;
        }
        variables[varIndex].type = TAG_INT;
        variables[varIndex].data = result;
    }},
    {0x04, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        auto varIndex = getInt(stack.back()); stack.pop_back();
        std::string input;
        std::cin >> input;
        variables[varIndex].type = TAG_STRING;
        variables[varIndex].data = input;
    }},
    {0x05, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // str2int
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        int num = 0;
        std::string str = "0";
        str = std::get<std::string>(value.data);

        num = std::stoi(str);

        variables[varIndex].type = TAG_INT;
        variables[varIndex].data = num;
    }},
    {0x06, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // int2str
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        int num = 0;
        num = getInt(value);

        std::string str = std::to_string(num);
        variables[varIndex].type = TAG_STRING;
        variables[varIndex].data = str;
    }},
    {0x07, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // str2float
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

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

        variables[varIndex].type = TAG_FLOAT;
        variables[varIndex].data = num;
    }},
    {0x08, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // float2str
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        double num = 0.0;
        if(value.type == TAG_FLOAT) num = std::get<double>(value.data);
        else if(value.type == TAG_INT) num = static_cast<double>(getInt(value)); // accept int too, same leniency as int2str only handling its own type

        std::string str = std::to_string(num);
        variables[varIndex].type = TAG_STRING;
        variables[varIndex].data = str;
    }},
    // stdlib impl
    {0xA0, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // assertCapability
        auto value = stack.back(); stack.pop_back();
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
    {0xA1, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // openFile
        auto handleVarIndex = getInt(stack.back()); stack.pop_back();

        auto value = stack.back(); stack.pop_back();

        auto filename = std::get<std::string>(value.data);

        auto stream = new std::fstream(filename, std::ios::in | std::ios::out | std::ios::trunc);
        if(!stream->is_open()) {
            throw std::runtime_error("openFile failed: unable to open file " + filename);
        }

        fileHandles[fileHandleId] = stream;

        variables[handleVarIndex].type = TAG_INT;
        variables[handleVarIndex].data = fileHandleId++;
    }},
    {0xA2, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // writeFile
        auto handle = getInt(stack.back()); stack.pop_back();

        auto value = stack.back(); stack.pop_back();

        auto valueToWrite = std::get<std::string>(value.data);

        auto it = fileHandles.find(handle);
        if(it != fileHandles.end()) {
            auto f = it->second;
            *f << valueToWrite;
        } else {
            throw std::runtime_error("writeFile failed: invalid file handle");
        }
    }},
    {0xA3, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // readFile
        auto handle = getInt(stack.back()); stack.pop_back();
        auto varIndex = getInt(stack.back()); stack.pop_back();

        auto it = fileHandles.find(handle);
        if(it != fileHandles.end()) {
            auto f = it->second;
            std::string contents((std::istreambuf_iterator<char>(*f)), std::istreambuf_iterator<char>());

            variables[varIndex].type = TAG_STRING;
            variables[varIndex].data = contents;
        } else {
            throw std::runtime_error("readFile failed: invalid file handle");
        }
    }},
    {0xA4, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // closeFile
        auto handle = getInt(stack.back()); stack.pop_back();

        auto it = fileHandles.find(handle);
        if(it != fileHandles.end()) {
            auto f = it->second;
            f->close();
            fileHandles.erase(it);
        } else {
            throw std::runtime_error("writeFile failed: invalid file handle");
        }
    }},
    {0xA5, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // randomSeed
        auto seed = getInt(stack.back()); stack.pop_back();

        rngEngine.seed(seed);
    }},
    {0xA6, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // random
        auto varIndex = getInt(stack.back()); stack.pop_back();

        static std::uniform_real_distribution<double> dist(0.0, 1.0);
        float val = dist(rngEngine);

        variables[varIndex].type = TAG_FLOAT;
        variables[varIndex].data = val;
    }},
    {0xA7, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // randomRange
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto max = getInt(stack.back()); stack.pop_back();
        auto min = getInt(stack.back()); stack.pop_back();

        std::uniform_int_distribution<int64_t> dist(min, max); // inclusive on both ends
        int64_t val = dist(rngEngine);

        variables[varIndex].type = TAG_INT;
        variables[varIndex].data = val;
    }},
    {0xA8, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // httpRequest
        auto outVarIndex = getInt(stack.back()); stack.pop_back();
        auto statusVarIndex = getInt(stack.back()); stack.pop_back();
        auto body = std::get<std::string>(stack.back().data); stack.pop_back();
        auto headerStr = std::get<std::string>(stack.back().data); stack.pop_back();
        auto url = std::get<std::string>(stack.back().data); stack.pop_back();
        auto method = std::get<std::string>(stack.back().data); stack.pop_back();

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

        variables[outVarIndex].type = TAG_STRING;
        variables[outVarIndex].data = outResponse;

        variables[statusVarIndex].type = TAG_INT;
        variables[statusVarIndex].data = outStatus;
    }},
    {0xA9, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // strlen
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        auto str = std::get<std::string>(value.data);

        variables[varIndex].type = TAG_INT;
        variables[varIndex].data = static_cast<int64_t>(str.size());
    }},
    {0xAA, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // substr(s, start, len, &out)
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto lenArg = getInt(stack.back()); stack.pop_back();
        auto startArg = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        auto str = std::get<std::string>(value.data);

        std::string result;
        if (startArg >= 0 && static_cast<size_t>(startArg) < str.size()) {
            result = str.substr(startArg, lenArg);
        }

        variables[varIndex].type = TAG_STRING;
        variables[varIndex].data = result;
    }},
    {0xAB, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // strfind(s, needle, &index)
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto needleVal = stack.back(); stack.pop_back();
        auto strVal = stack.back(); stack.pop_back();

        auto str = std::get<std::string>(strVal.data);
        auto needle = std::get<std::string>(needleVal.data);

        auto pos = str.find(needle);
        int64_t result = (pos == std::string::npos) ? -1 : static_cast<int64_t>(pos);

        variables[varIndex].type = TAG_INT;
        variables[varIndex].data = result;
    }},
    {0xAC, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // toUpper(s, &out) / toLower(s, &out) via a flag arg (0=lower, 1=upper)
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto upperFlag = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        auto str = std::get<std::string>(value.data);

        if (upperFlag) {
            std::transform(str.begin(), str.end(), str.begin(), ::toupper);
        } else {
            std::transform(str.begin(), str.end(), str.begin(), ::tolower);
        }

        variables[varIndex].type = TAG_STRING;
        variables[varIndex].data = str;
    }},
    {0xAD, [](std::vector<Variant>& stack, std::vector<Variant>& variables) {
        // trim(s, &out)
        auto varIndex = getInt(stack.back()); stack.pop_back();
        auto value = stack.back(); stack.pop_back();

        auto str = std::get<std::string>(value.data);

        const char* ws = " \t\n\r\f\v";
        size_t start = str.find_first_not_of(ws);
        size_t end = str.find_last_not_of(ws);

        std::string result = (start == std::string::npos) ? "" : str.substr(start, end - start + 1);

        variables[varIndex].type = TAG_STRING;
        variables[varIndex].data = result;
    }},
};