#include "base/editor/id_picker.h"

#include "base/core/scene_ops.h"

#include <glad/glad.h>
#include <glog/logging.h>

#include <entt/entt.hpp>

IdPicker::IdPicker() : shader_("id_pick.vert.glsl", "id_pick.frag.glsl") {}

IdPicker::~IdPicker()
{
    destroy_targets();
}

void IdPicker::destroy_targets()
{
    if (fbo_ != 0)
        glDeleteFramebuffers(1, &fbo_);
    if (id_tex_ != 0)
        glDeleteTextures(1, &id_tex_);
    if (depth_rb_ != 0)
        glDeleteRenderbuffers(1, &depth_rb_);
    fbo_ = id_tex_ = depth_rb_ = 0;
    width_ = height_ = 0;
}

void IdPicker::ensure_size(int w, int h)
{
    if (w <= 0 || h <= 0)
        return;
    if (w == width_ && h == height_ && fbo_ != 0)
        return;

    destroy_targets();
    width_  = w;
    height_ = h;

    glCreateTextures(GL_TEXTURE_2D, 1, &id_tex_);
    glTextureStorage2D(id_tex_, 1, GL_R32I, width_, height_);
    glTextureParameteri(id_tex_, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTextureParameteri(id_tex_, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    glCreateRenderbuffers(1, &depth_rb_);
    glNamedRenderbufferStorage(depth_rb_, GL_DEPTH_COMPONENT24, width_, height_);

    glCreateFramebuffers(1, &fbo_);
    glNamedFramebufferTexture(fbo_, GL_COLOR_ATTACHMENT0, id_tex_, 0);
    glNamedFramebufferRenderbuffer(fbo_, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, depth_rb_);

    const GLenum draw_buf = GL_COLOR_ATTACHMENT0;
    glNamedFramebufferDrawBuffers(fbo_, 1, &draw_buf);

    const GLenum status = glCheckNamedFramebufferStatus(fbo_, GL_FRAMEBUFFER);
    CHECK(status == GL_FRAMEBUFFER_COMPLETE) << "IdPicker FBO incomplete: " << status;
}

entt::entity IdPicker::pick(const Scene& scene, int mx, int my, int vp_w, int vp_h)
{
    if (vp_w <= 0 || vp_h <= 0 || mx < 0 || my < 0 || mx >= vp_w || my >= vp_h)
        return entt::null;

    ensure_size(vp_w, vp_h);

    GLint prev_fbo = 0;
    glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
    GLint prev_vp[4] = {};
    glGetIntegerv(GL_VIEWPORT, prev_vp);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glViewport(0, 0, width_, height_);

    glDisable(GL_BLEND);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_CULL_FACE);

    const GLint clear_id = 0;
    glClearBufferiv(GL_COLOR, 0, &clear_id);
    glClear(GL_DEPTH_BUFFER_BIT);

    const glm::mat4 view = scene.camera().view();
    const glm::mat4 proj = scene.camera().projection();

    shader_.use();
    const auto& registry = scene.registry();
    for (const entt::entity entity : scene_ops::mesh_entities(registry))
    {
        const glm::mat4 mvp = proj * view * scene_ops::model(registry, entity);
        shader_.set("uMVP", mvp);
        shader_.set("uEntityId", static_cast<int>(entt::to_integral(entity)));
        scene_ops::draw(registry, entity);
    }

    const int read_x = mx;
    const int read_y = vp_h - 1 - my;

    GLint id = 0;
    glBindFramebuffer(GL_READ_FRAMEBUFFER, fbo_);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(read_x, read_y, 1, 1, GL_RED_INTEGER, GL_INT, &id);

    glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
    glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);

    if (id <= 0)
        return entt::null;

    const entt::entity entity{static_cast<entt::id_type>(id)};
    if (!registry.valid(entity) || !scene_ops::has_mesh(registry, entity))
        return entt::null;
    return entity;
}
