#pragma once

#include "render/graph/PythonGraph.h"

#include <span>

class ConfigManager;

void drawGraphSettings(ConfigManager& config, std::span<GraphParameterControl> controls);
