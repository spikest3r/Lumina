#pragma once

#include "engine_types.h"
#include "3d_loader.h"
#include "texture.h"

class Scene {
	friend class Engine;
private:
	// custom user logic

	// GameObjects and resources are not loaded
	virtual void EarlyInitScene(Engine* engine);

	// GameObjects and resources are loaded
	virtual void InitScene(Engine* engine);

	virtual void UpdateScene(Engine* engine);
	virtual void DestroyScene(Engine* engine);
	virtual GameObject* CreateGameObject(Engine* engine, const char* objectType, const char* tag, const char* name, Transform transform, Mesh* mesh, Texture* texture, bool dynamic);
	virtual void ResourceLoaded(std::string name, const char* path, ResourceType type);
	char padding[104];
};