#include "resman.h"

std::string resolveType(ResourceType type) {
    switch(type) {
        case TEXTURE:
            return "Texture";
        case MESH:
            return "Mesh";
        case SOUND:
            return "Sound";
        default:
            return "Unknown"; 
    }
}

ResourceManager::ResourceManager(Engine* enginePtr, bool verbose) {
    engine = enginePtr;
    isVerbose = verbose;
}

Texture* ResourceManager::getTexture(std::string path) {
    if(isVerbose) std::cout << "Requested texture " << path << "\n";
    path = "assets/textures/" + path + ".png";
    return static_cast<Texture*>(findResource(path, TEXTURE));
}

Mesh* ResourceManager::getMesh(std::string path) {
    if(isVerbose) std::cout << "Requested mesh " << path << "\n";
    path = "assets/models/" + path + ".obj";
    return static_cast<Mesh*>(findResource(path, MESH));
}

Sound* ResourceManager::getSound(std::string path, bool looping, bool three_dim) {
    if(isVerbose) std::cout << "Requested sound " << path << "\n";
    path = "assets/sounds/" + path + ".wav";
    return static_cast<Sound*>(findResource(path, SOUND, {looping, three_dim}));
}

IResource* ResourceManager::findResource(std::string path, ResourceType type, ResourceArguments args) {
    auto it = resources.find(path);
    if(it != resources.end()) {
        // use existing
        if(it->second.type == type) {
            if(isVerbose) std::cout << "Found " << path << "\n";
            return it->second.ptr;
        } else {
            std::cerr << path << "is a " << resolveType(it->second.type) << ", not a " << resolveType(type) << "\n";
            return nullptr;
        }
    } else {
        // load
        if(isVerbose) std::cout << "Loading " << path << "\n";
        IResource* newResource = nullptr;
        switch(type) {
            case TEXTURE:
                newResource = engine->createTexture(path + "_texture", path.c_str());
                break;
            case MESH:
                newResource = engine->createMesh(path + "_mesh", path.c_str());
                break;
            case SOUND:
                newResource = engine->createSound(path + "_sound", path.c_str(), args.looping, args.three_dim);
                break;
            default:
                throw std::runtime_error("Unkown resource type");
        }
        if(newResource == nullptr) {
            std::cerr << "Failed to load resource " << path << "\n";
            return nullptr;
        }
        if(isVerbose) std::cout << "Loaded " << path << " successfully\n";
        resources[path] = {
            newResource,
            type
        };
        return newResource;
    }
}