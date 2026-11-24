#pragma once

#include <slang-rhi.h>
#include <cstdint>
#include <functional>
#include <glm/vec2.hpp>
#include <optional>

inline constexpr char kEditorPickingPassType[] = "EditorPickingPass";

struct EditorPickRequest
{
    glm::vec2                    viewportUv;
    std::function<void(uint32_t)> complete;
};

struct EditorFrameInput
{
    std::optional<EditorPickRequest>                      pick;
    std::function<void(rhi::IRenderPassEncoder&, uint32_t)> recordUi;
};
