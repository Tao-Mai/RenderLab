#pragma once

#include "base/core/scene.h"
#include "base/gfx/shader.h"

// GPU object-id picking: render entity ids to an offscreen R32I buffer, then read back.
class IdPicker
{
public:
    IdPicker();
    ~IdPicker();

    IdPicker(const IdPicker&)            = delete;
    IdPicker& operator=(const IdPicker&) = delete;

    int pick(const Scene& scene, int mx, int my, int vp_w, int vp_h);

private:
    void ensure_size(int w, int h);
    void destroy_targets();

    Shader shader_;
    GLuint fbo_      = 0;
    GLuint id_tex_   = 0;
    GLuint depth_rb_ = 0;
    int    width_    = 0;
    int    height_   = 0;
};
