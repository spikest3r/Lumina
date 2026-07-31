#pragma once

class Engine;
class GameObject;

class Sound : public IResource {
	friend class GameObject;
	friend class Engine;
public:
	Sound();
};