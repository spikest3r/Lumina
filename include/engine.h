#pragma once
#include "3d_loader.h"
#include "engine_types.h"
#include "gameobject.h"
#include "texture.h"
#include "sound.h"
#include "charactercontroller.h"
#include "ui.h"
#include "scene.h"
#include <memory>

class Engine {
public:
	ENGINE_API void init(const int width, const int height, const char* title);

	ENGINE_API void update();
	ENGINE_API void render();
	ENGINE_API bool running();
	ENGINE_API float getDeltaTime();
	ENGINE_API void cleanup();
	ENGINE_API void exit();

	ENGINE_API void getCameraVectors(Vector3& forward, Vector3& right);
	ENGINE_API Vector2 getExtents();

	static ENGINE_API Engine* Create(); // Static factory
	static ENGINE_API void Destroy(Engine* instance);

	// Game Object

	ENGINE_API Texture* createTexture(std::string name, const char* path);
	ENGINE_API Mesh* createMesh(std::string name, const char* path);
	ENGINE_API Mesh* createMesh(std::string name, std::vector<Vertex> vertices, std::vector<uint32_t> indices);
	ENGINE_API Sound* createSound(std::string name, const char* path, bool looping, bool three_dim);

	ENGINE_API Texture* getTexture(std::string name);
	ENGINE_API Mesh* getMesh(std::string name);
	ENGINE_API Sound* getSound(std::string name);
	ENGINE_API Scene* getScene(std::string sceneFile);
	ENGINE_API GameObject* getGameObject(std::string name);

	ENGINE_API KeyState getKey(KeyCode code);
	ENGINE_API KeyState getMouseButton(MouseButton button);

	ENGINE_API Vector2 getMousePos();
	ENGINE_API void getMouseRay(Vector3& origin, Vector3& direction);
	ENGINE_API float getScrollDelta();

	Vector3 cameraPosition = { 0.0f, 0.0f, 5.0f }; // Start 5 units back
	Vector3 cameraRotation = { 0.0f, -90.0f, 0.0f }; // Point toward the scene
	Vector3 cameraOffset = {0.0f,0.0f,0.0f};
	NearFarPlanes planes = { 0.1f, 100.f };

	ENGINE_API void renderPhysXDebug(bool state);
	ENGINE_API void pushRayDebug(RayDebug rd);

	ENGINE_API ICharacterController* createCharacterController(float height, float radius, Vector3 position, PhysicsMaterial* material, bool interactWithActors);
	ENGINE_API PhysicsMaterial* createPhysicsMaterial(float staticFriction, float dynamicFriction, float restitution);

	ENGINE_API Trigger* createBoxTrigger(Vector3 pos, Vector3 size);

	ENGINE_API void setGlobalMute(bool mute);

	ENGINE_API void requestDestroy(IResource* resource);
	ENGINE_API void requestDestroyGameObject(GameObject* object);
	ENGINE_API void requestDestroyTrigger(Trigger* trigger);
	ENGINE_API void requestDestroyCharacterController(ICharacterController* ctrl);
	ENGINE_API void requestDestroyScene(Scene* scene);

	ENGINE_API RaycastHit raycast(Vector3 origin, Vector3 direction, float distance);

	ENGINE_API void setClearColor(Vector3 clearColor);

	ENGINE_API void addTimer(float delay, std::function<void()> cb);

	ENGINE_API void setCursorMode(CursorMode mode);

	ENGINE_API void SetUICallback(std::function<void(Engine* engine)> callback);
	ENGINE_API std::vector<VRAMStats> getVRAMStats();

	ENGINE_API void loadScene(Scene* scene);

	ENGINE_API void unloadActiveScene();
	ENGINE_API Scene* getActiveScene();
	ENGINE_API void updateScene();

	ENGINE_API void setLightPosition(Vector3 pos);

	ENGINE_API void setGroundPlaneActive(bool active);

	ENGINE_API UIElement* createUIElement(Texture* texture, Vector2 pos, Vector2 size);

	template <typename T>
	T* createGameObject(
		Transform spawnTransform,
		Mesh* mesh,
		Texture* texture,
		PhysicsMaterial* material,
		bool isDynamic
	) {
		static_assert(std::is_base_of<GameObject, T>::value,
			"T must inherit from GameObject");

		size_t totalSize = sizeof(ObjectHeader) + alignof(T) + sizeof(T);

		void* raw = requestMemory(totalSize);

		// header
		auto* header = (ObjectHeader*)raw;
		header->destroy = &Engine::destroyImpl<T>;

		// object memory after header
		void* objMem = (char*)raw + sizeof(ObjectHeader);

		// alignment fix
		size_t space = totalSize - sizeof(ObjectHeader);

		void* alignedObjMem = std::align(
			alignof(T),
			sizeof(T),
			objMem,
			space
		);

		if (!alignedObjMem)
			throw std::bad_alloc();

		// construct object
		T* object = new (alignedObjMem) T();

		internal_createGameObject(
			object,
			spawnTransform,
			mesh,
			texture,
			material,
			isDynamic
		);

		object->Start();

		return object;
	}

	template <typename T>
	T* createScene(const char* sceneFile, bool* valid)
	{
		static_assert(std::is_base_of<Scene, T>::value,
			"T must inherit from Scene");

		size_t totalSize = sizeof(ObjectHeader) + alignof(T) + sizeof(T);

		void* raw = requestMemory(totalSize);

		// header sits at start
		auto* header = (ObjectHeader*)raw;
		header->destroy = &destroyImpl<T>;

		// object memory starts after header
		void* objMem = (char*)raw + sizeof(ObjectHeader);

		// IMPORTANT: std::align needs mutable space variable
		size_t space = totalSize - sizeof(ObjectHeader);

		void* alignedObjMem = std::align(
			alignof(T),
			sizeof(T),
			objMem,
			space
		);

		if (!alignedObjMem)
			throw std::bad_alloc();

		// construct Scene in-place
		T* object = new (alignedObjMem) T();

		bool result = loadScene_internal(object, sceneFile);
		if (valid) *valid = result;

		return object;
	}

	template <typename T>
	T* createScene()
	{
		static_assert(std::is_base_of<Scene, T>::value,
			"T must inherit from Scene");

		size_t totalSize = sizeof(ObjectHeader) + alignof(T) + sizeof(T);

		void* raw = requestMemory(totalSize);

		// header sits at start
		auto* header = (ObjectHeader*)raw;
		header->destroy = &destroyImpl<T>;

		// object memory starts after header
		void* objMem = (char*)raw + sizeof(ObjectHeader);

		// IMPORTANT: std::align needs mutable space variable
		size_t space = totalSize - sizeof(ObjectHeader);

		void* alignedObjMem = std::align(
			alignof(T),
			sizeof(T),
			objMem,
			space
		);

		if (!alignedObjMem)
			throw std::bad_alloc();

		// construct Scene in-place
		T* object = new (alignedObjMem) T();

		return object;
	}

	// Memory Allocator
	ENGINE_API static void* requestMemory(size_t size);
	ENGINE_API static void freeMemory(void* ptr);

	ENGINE_API void dualsense_playHaptics(Sound* sound, float volume);
	ENGINE_API void dualsense_setLightbarColor(unsigned char R, unsigned char G, unsigned char B);
	ENGINE_API bool isDualSenseAttached();

	ENGINE_API GamepadState* getGamepad();

	ENGINE_API bool isLastFrame();
private:
	template <typename T>
	static void destroyImpl(void* p) {
		((T*)p)->~T();
		void* base = (char*)p - sizeof(ObjectHeader);
		freeMemory(base);
	}

	ENGINE_API void internal_createGameObject(
		GameObject* ptr,
		Transform spawnTransform,
		Mesh* mesh,
		Texture* texture,
		PhysicsMaterial* material,
		bool isDynamic
	);

	// Scene
	ENGINE_API bool loadScene_internal(Scene* scene, const char* sceneFile);
};
