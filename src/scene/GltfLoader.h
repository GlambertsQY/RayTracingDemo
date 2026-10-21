#pragma once

#include "scene/Scene.h"
#include <string>

bool loadGltfScene(const std::string& filename, Scene& out, std::string& error);
