#pragma once
#include "engine.h"

class UIElement {
	friend class Engine;
public:
	Vector2 position;
	Vector2 size;
};