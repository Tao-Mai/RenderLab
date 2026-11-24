#pragma once

#include "editor/EditorCameraConfig.h"

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
    [[nodiscard]] EditorCameraConfig&       config() { return data; }
    [[nodiscard]] const EditorCameraConfig& config() const { return data; }

private:
    EditorCameraConfig& data;
    bool                inputEnabled   = true;
    bool                looking        = false;
    bool                freeMovement   = false;
    double              previousMouseX = 0.0;
    double              previousMouseY = 0.0;
};
