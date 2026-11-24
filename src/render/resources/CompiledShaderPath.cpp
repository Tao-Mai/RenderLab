#include "render/resources/CompiledShaderPath.h"
#include "core/RuntimeDirectory.h"
std::filesystem::path compiledShaderPath(const std::filesystem::path& path) { return runtimeDirectory() / path; }
