#pragma once

#include "base/gfx/texture.h"

#include <string>

// builtin 纯色示例：
//   builtin:white | builtin:black | builtin:red ...
//   builtin:color/0.75,0.35,0.25
//   builtin:color/0.75,0.35,0.25,1.0
//   builtin:#ff8844 | builtin:#ff8844cc
TextureGPU LoadTexture(const std::string& key);
