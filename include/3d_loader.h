#pragma once
#include <string>
#include <vector>
#include "vertex.h"
#include "engine_types.h"

class Mesh : public IResource {
public:
	Mesh(std::vector<Vertex> vertices, std::vector<uint32_t> indices);
};

class Model {
public:
	Model(const char* path) {
		loadModel(path);
	}
	std::vector<Mesh> meshes;
	void loadModel(const std::string& path);
};