#pragma once

#include "editor/EditorCameraConfig.h"

#include <glm/mat4x4.hpp>

struct RenderView;
struct ViewportRect;

class EditorCamera
{
public:
    explicit EditorCamera(EditorCameraConfig& config) : data(config) {}

    EditorCamera(const EditorCamera&)            = delete;
    EditorCamera& operator=(const EditorCamera&) = delete;

    void tick(float deltaTime);
    void resetNavigation();
    void setInputEnabled(bool enabled) { inputEnabled = enabled; }

    [[nodiscard]] bool                      isNavigationActive() const { return looking; }
    [[nodiscard]] bool                      isFreeMovementActive() const { return freeMovement; }
    [[nodiscard]] const glm::vec3&          worldPosition() const { return data.position; }
    [[nodiscard]] EditorCameraConfig&       config() { return data; }
    [[nodiscard]] const EditorCameraConfig& config() const { return data; }
    [[nodiscard]] glm::vec3                 forward() const;
    [[nodiscard]] glm::vec3                 right() const;
    [[nodiscard]] glm::vec3                 up() const;
    [[nodiscard]] glm::mat4                 viewMatrix() const;
    [[nodiscard]] glm::mat4                 projectionMatrix(float aspectRatio) const;
    [[nodiscard]] RenderView                renderView(ViewportRect viewport) const;

private:
    EditorCameraConfig& data;
    bool                inputEnabled   = true;
    bool                looking        = false;
    bool                freeMovement   = false;
    double              previousMouseX = 0.0;
    double              previousMouseY = 0.0;
};
