// =============================================================================
// Shadow Mapping Demo
// -----------------------------------------------------------------------------
// 点光源阴影（本 demo 用「单张 2D Shadow Map + 透视投影」）：
//   - 点光源位置 → lookAt 场景中心，再用 perspective 当「光源相机」
//   - 这在效果上等价于朝向场景的聚光灯阴影，不是全向点光
//   - 真正的全向点光需要 6 张面的 Cubemap Shadow Map（后续可扩展）
//
//   Pass 1 — 从光源视角把场景深度写入 Shadow Map（FBO + 深度纹理）
//   Pass 2 — 主相机正常着色，把片元变换到光空间，与 Shadow Map 比较深浅
//
// 场景物体 / 灯 / 相机来自 config/scenes/shadow.yaml（不再硬编码）。
// =============================================================================

#include "base/core/demo.h"
#include "base/gfx/asset_cache.h"
#include "base/gfx/shader.h"
#include "base/io/app_config.h"
#include "base/io/scene_loader.h"
#include "base/platform/paths.h"

#include <glad/glad.h>
#include <imgui.h>

#include <glm/gtc/matrix_transform.hpp>

#include <filesystem>

namespace
{
class ShadowMappingDemo : public IDemo
{
public:
    explicit ShadowMappingDemo(AssetCache& assets)
    {
        scene_path_ = paths::scene_config_dir() / "shadow.yaml";

        const AppConfig app = LoadAppConfig(paths::config_dir() / "app.yaml");
        LoadScene(scene_path_, scene_, assets, app.scene_camera);

        for (const auto& e : scene_.entities())
        {
            if (e.light)
            {
                light_pos_ = e.light->position;
                break;
            }
        }

        const auto dir = paths::project_root() / "demos" / "shadow";
        depth_shader_  = Shader(dir / "depth.vert.glsl", dir / "depth.frag.glsl");
        scene_shader_  = Shader(dir / "vert.glsl", dir / "frag.glsl");

        init_shadow_map();
    }

    ~ShadowMappingDemo() override { destroy_shadow_map(); }

    const char* name() const override { return "shadow"; }
    Scene&      scene() override { return scene_; }

    [[nodiscard]] std::filesystem::path scene_config_path() const override { return scene_path_; }

    void update(float) override {}

    void draw() override
    {
        const glm::mat4 light_view =
            glm::lookAt(light_pos_, glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        constexpr float     light_fov  = glm::radians(90.0f);
        constexpr float     light_near = 0.5f;
        constexpr float     light_far  = 25.0f;
        const glm::mat4 light_proj =
            glm::perspective(light_fov, 1.0f, light_near, light_far);
        const glm::mat4 light_vp = light_proj * light_view;

        GLint prev_vp[4] = {};

        glGetIntegerv(GL_VIEWPORT, prev_vp);

        glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
        glViewport(0, 0, shadow_size_, shadow_size_);
        glEnable(GL_DEPTH_TEST);
        glClear(GL_DEPTH_BUFFER_BIT);

        glCullFace(GL_FRONT);

        depth_shader_.use();
        for (const auto& e : scene_.entities())
        {
            if (!e.has_mesh())
                continue;
            depth_shader_.set("uLightMVP", light_vp * e.model());
            e.draw();
        }
        glCullFace(GL_BACK);

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(prev_vp[0], prev_vp[1], prev_vp[2], prev_vp[3]);
        glClearColor(scene_.clear_color_.r, scene_.clear_color_.g, scene_.clear_color_.b, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

        scene_shader_.use();
        scene_shader_.set("view", scene_.camera().view());
        scene_shader_.set("projection", scene_.camera().projection());
        scene_shader_.set("uLightVP", light_vp);
        scene_shader_.set("uAmbient", scene_.ambient_);
        scene_shader_.set("uLightPos", light_pos_);
        scene_shader_.set("uLightColor", glm::vec3(1.0f));
        scene_shader_.set("uLightIntensity", 1.0f);
        scene_shader_.set("uShadowBias", bias_);
        scene_shader_.set("uShadowMap", 0);

        glBindTextureUnit(0, depth_tex_);

        for (const auto& e : scene_.entities())
        {
            if (!e.has_mesh())
                continue;
            scene_shader_.set("model", e.model());
            scene_shader_.set("uAlbedo", e.mesh_renderer->material.albedo);
            e.draw();
        }
    }

    void draw_ui() override
    {
        ImGui::TextWrapped(
            "点光源阴影：透视投影 + 单张 2D Shadow Map（等价朝向场景的 spot）。\n"
            "场景物体来自 config/scenes/shadow.yaml；Editor 中点「保存场景」写回位置。\n"
            "Bias 过大阴影会漂，过小会有 acne。");
        ImGui::DragFloat("Bias", &bias_, 0.0001f, 0.0f, 0.02f, "%.4f");
        if (ImGui::DragFloat3("Light", &light_pos_.x, 0.05f))
        {
            for (auto& e : scene_.entities())
            {
                if (!e.light)
                    continue;
                e.light->position    = light_pos_;
                e.transform.position = light_pos_;
                break;
            }
        }
    }

private:
    void init_shadow_map()
    {
        glCreateTextures(GL_TEXTURE_2D, 1, &depth_tex_);
        glTextureStorage2D(depth_tex_, 1, GL_DEPTH_COMPONENT24, shadow_size_, shadow_size_);
        glTextureParameteri(depth_tex_, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTextureParameteri(depth_tex_, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTextureParameteri(depth_tex_, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
        glTextureParameteri(depth_tex_, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
        const float border[] = {1.0f, 1.0f, 1.0f, 1.0f};
        glTextureParameterfv(depth_tex_, GL_TEXTURE_BORDER_COLOR, border);

        glTextureParameteri(depth_tex_, GL_TEXTURE_COMPARE_MODE, GL_COMPARE_REF_TO_TEXTURE);
        glTextureParameteri(depth_tex_, GL_TEXTURE_COMPARE_FUNC, GL_LEQUAL);

        glCreateFramebuffers(1, &fbo_);
        glNamedFramebufferTexture(fbo_, GL_DEPTH_ATTACHMENT, depth_tex_, 0);
        glNamedFramebufferDrawBuffer(fbo_, GL_NONE);
        glNamedFramebufferReadBuffer(fbo_, GL_NONE);
    }

    void destroy_shadow_map()
    {
        if (fbo_ != 0)
            glDeleteFramebuffers(1, &fbo_);
        if (depth_tex_ != 0)
            glDeleteTextures(1, &depth_tex_);
        fbo_ = depth_tex_ = 0;

    }

    Scene                 scene_;
    std::filesystem::path scene_path_;
    Shader                depth_shader_;
    Shader                scene_shader_;

    GLuint depth_tex_   = 0;
    GLuint fbo_         = 0;
    int    shadow_size_ = 2048;

    glm::vec3 light_pos_{-3.0f, 5.0f, -2.0f};
    float     bias_ = 0.005f;
};

REGISTER_DEMO(ShadowMappingDemo, "shadow");
}  // namespace
