#pragma once

#include <string>
#include <unordered_map>
#include <nlohmann/json.hpp>

// Pass type -> instance name -> property overrides.
using PassOverrides = std::unordered_map<std::string, std::unordered_map<std::string, nlohmann::json>>;
