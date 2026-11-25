#pragma once

#include "core/utils/NonCopyable.h"
#include "scene/Camera.h"

class EditorCamera : private NonCopyable
{
public:
    explicit EditorCamera(Camera& config) : data(config) {}

    EditorCamera(EditorCamera&&) = delete;

    void tick(float deltaTime);
    void resetNavigation();
    void requestFreeMovementToggle() { freeMovementToggleRequested = true; }
    void addScrollOffset(double offset) { scrollOffset += offset; }
    void setInputEnabled(bool enabled) { inputEnabled = enabled; }

    [[nodiscard]] bool                      isNavigationActive() const { return looking; }

private:
    void setLooking(bool enabled);

    Camera& data;
    bool                inputEnabled   = true;
    bool                looking        = false;
    bool                freeMovement   = false;
    bool                freeMovementToggleRequested = false;
    double              scrollOffset = 0.0;
    double              previousMouseX = 0.0;
    double              previousMouseY = 0.0;
};
