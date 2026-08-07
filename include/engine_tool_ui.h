#pragma once

#include "engine_types.h"

class ENGINE_API UIFont {

};

class ENGINE_API ToolUI {
public:
	static void Begin(const char* name, bool borderless = false, bool resizable = true);
	static void End();
	static bool Button(const char* text);
	static bool Button(const char* text, Vector2 size);
	static void Text(const char* text); // todo: args
	static void SameLine();
	static void ProgressBar(float value, Vector2 vec);
	static bool TextField(const char* label, char* buffer, size_t size, bool disallowBlank = false);
	static bool InputFloat3(const char* label, Vector3& v, float speed = 0.1f);
	static void SetNextWindowSize(Vector2 size);
	static void SetNextWindowPos(Vector2 pos);
	static void AddFontFromFileTTF(UIFont& font, const char* fontName, float size);
	static void PushFont(UIFont& font);
	static void PopFont();
	static bool InputTextMultiline(const char* label, std::string& str);
};