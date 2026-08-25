#pragma once

#include "base/core/demo.h"
#include "base/io/app_config.h"
#include "base/platform/window.h"
#include "base/editor/id_picker.h"

#include <GLFW/glfw3.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

class AssetCache;

enum class GizmoMode : uint8_t
{
    Translate = 0,
    Rotate,
    Scale,
};

// 轻量级 RenderLab 编辑器：提供 demo 列表/切换、参数面板、相机控制、拾取与 gizmo。
class Editor
{
public:
    Editor(GlfwWindow& window, AssetCache& assets, DemoRegistry& registry,
           const EditorCameraConfig& camera_cfg, const std::string& initial_demo);
    ~Editor();

    Editor(const Editor&)            = delete;
    Editor& operator=(const Editor&) = delete;

    void run();

private:
    void begin_frame();
    void end_frame();
    void update(float dt);
    void draw_viewport(float aspect);
    void draw_ui(float fps);
    void handle_edit_camera(float dt);
    void handle_click();
    glm::vec2 screen_to_world_2d(double screen_x, double screen_y) const;
    void sync_camera_from_fly();
    void sync_fly_from_camera();
    bool switch_demo(int index);

    void handle_key(int key, int scancode, int action, int mods);
    static void GlfwKeyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);

    GlfwWindow&        window_;
    AssetCache&        assets_;
    DemoRegistry&      registry_;
    EditorCameraConfig camera_cfg_;

    std::unique_ptr<IDemo> demo_;
    int                    demo_index_ = 0;
    std::vector<std::string> demo_names_;
    std::string             demo_combo_items_;

    GizmoMode gizmo_mode_ = GizmoMode::Translate;
    int       selected_index_ = -1;

    // free-fly camera state (first person)
    glm::vec3 camera_pos_{0.0f, 2.0f, 5.0f};
    float     yaw_   = 0.0f;    // 绕 Y 轴，0 = 朝向 -Z
    float     pitch_ = -20.0f;  // 俯仰

    // layout
    float panel_width_   = 320.0f;
    float status_height_ = 28.0f;

    // mouse
    bool   rmb_down_     = false;
    bool   mmb_down_     = false;
    double last_mouse_x_ = 0.0;
    double last_mouse_y_ = 0.0;

    std::unique_ptr<IdPicker> id_picker_;

    // 链到 ImGui 的 GLFW key callback；ESC / demo on_key 走这里
    GLFWkeyfun   prev_key_callback_ = nullptr;
    static Editor* s_active_;

#if defined(_WIN32)
    // 启动前的键盘布局 + IME 状态，退出时完整恢复搜狗等输入法
    void*  saved_keyboard_layout_ = nullptr;
    unsigned long saved_ime_conversion_ = 0;
    unsigned long saved_ime_sentence_   = 0;
    bool          saved_ime_valid_      = false;
#endif
};
