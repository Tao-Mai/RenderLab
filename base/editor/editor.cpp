#include "base/editor/editor.h"

#include "base/io/scene_loader.h"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <glad/glad.h>
#include <GLFW/glfw3.h>
#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glog/logging.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>

#if defined(_WIN32)
#    ifndef WIN32_LEAN_AND_MEAN
#        define WIN32_LEAN_AND_MEAN
#    endif
#    ifndef NOMINMAX
#        define NOMINMAX
#    endif
#    include <windows.h>
#    include <imm.h>
#    define GLFW_EXPOSE_NATIVE_WIN32
#    include <GLFW/glfw3native.h>
#endif

namespace
{
// 加载支持中文的系统字体；失败则回退 ImGui 默认字体（无中文）。
void load_imgui_chinese_font(ImGuiIO& io)
{
    const char* candidates[] = {
        "C:/Windows/Fonts/msyh.ttc",   // 微软雅黑
        "C:/Windows/Fonts/msyh.ttf",
        "C:/Windows/Fonts/simhei.ttf", // 黑体
        "C:/Windows/Fonts/simsun.ttc", // 宋体
    };

    ImFontConfig cfg;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;
    cfg.PixelSnapH  = true;

    const ImWchar* ranges = io.Fonts->GetGlyphRangesChineseSimplifiedCommon();
    for (const char* path : candidates)
    {
        if (!std::filesystem::exists(path))
            continue;
        if (io.Fonts->AddFontFromFileTTF(path, 18.0f, &cfg, ranges))
        {
            LOG(INFO) << "ImGui font: " << path;
            return;
        }
    }
    LOG(WARNING) << "No Chinese font found, ImGui will not display CJK glyphs";
}

#if defined(_WIN32)
struct SavedInputState
{
    HKL   hkl         = nullptr;
    DWORD conversion  = 0;
    DWORD sentence    = 0;
    bool  has_ime     = false;
};

// 保存当前布局与 IME 状态，再切到美式英文，避免中文输入法抢走 WASD。
SavedInputState push_english_input(GLFWwindow* glfw_win)
{
    SavedInputState saved;
    saved.hkl = GetKeyboardLayout(0);

    HWND hwnd = glfwGetWin32Window(glfw_win);
    if (hwnd)
    {
        HIMC himc = ImmGetContext(hwnd);
        if (himc)
        {
            ImmGetConversionStatus(himc, &saved.conversion, &saved.sentence);
            saved.has_ime = true;
            ImmSetConversionStatus(himc, IME_CMODE_ALPHANUMERIC, saved.sentence);
            ImmReleaseContext(hwnd, himc);
        }
    }

    if (HKL en = LoadKeyboardLayoutA("00000409", KLF_ACTIVATE))
        ActivateKeyboardLayout(en, KLF_SETFORPROCESS);

    return saved;
}

// 退出时完整恢复：键盘布局 + 窗口 IME 中/英状态（搜狗等才能回来）。
void restore_input_layout(GLFWwindow* glfw_win, const SavedInputState& saved)
{
    if (!saved.hkl)
        return;

    // 1) 进程内切回原布局
    ActivateKeyboardLayout(saved.hkl, KLF_SETFORPROCESS);

    // 2) 写回用户默认输入语言，避免进程退出后任务栏仍停在英文
    HKL hkl = saved.hkl;
    SystemParametersInfoW(SPI_SETDEFAULTINPUTLANG, 0, &hkl, SPIF_SENDCHANGE);

    HWND hwnd = glfw_win ? glfwGetWin32Window(glfw_win) : nullptr;
    if (hwnd)
    {
        // 3) 同步通知窗口切换输入语言（SendMessage，退出前必须完成）
        SendMessageW(hwnd, WM_INPUTLANGCHANGEREQUEST, TRUE, reinterpret_cast<LPARAM>(saved.hkl));

        // 4) 恢复中/英转换状态（搜狗中文模式）
        if (saved.has_ime)
        {
            HIMC himc = ImmGetContext(hwnd);
            if (himc)
            {
                ImmSetConversionStatus(himc, saved.conversion, saved.sentence);
                ImmReleaseContext(hwnd, himc);
            }
        }
    }
}
#endif
}  // namespace

Editor::Editor(GlfwWindow& window, AssetCache& assets, DemoRegistry& registry,
               const EditorCameraConfig& camera_cfg, const std::string& initial_demo) :
    window_(window), assets_(assets), registry_(registry), camera_cfg_(camera_cfg)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;

    load_imgui_chinese_font(io);

    ImGui::StyleColorsDark();
    ImGui_ImplGlfw_InitForOpenGL(window_.GetNativeWindow(), true);
    ImGui_ImplOpenGL3_Init("#version 450");

#if defined(_WIN32)
    {
        const SavedInputState saved = push_english_input(window_.GetNativeWindow());
        saved_keyboard_layout_      = saved.hkl;
        saved_ime_conversion_       = saved.conversion;
        saved_ime_sentence_         = saved.sentence;
        saved_ime_valid_            = saved.has_ime;
    }
#endif

    id_picker_ = std::make_unique<IdPicker>();

    // 收集 demo 名
    demo_names_.reserve(registry_.size());
    for (size_t i = 0; i < registry_.size(); ++i)
        demo_names_.push_back(registry_.name_at(i));

    int start = registry_.index_of(initial_demo);
    if (start < 0)
        start = 0;
    switch_demo(start);
}

Editor::~Editor()
{
#if defined(_WIN32)
    SavedInputState saved;
    saved.hkl        = static_cast<HKL>(saved_keyboard_layout_);
    saved.conversion = saved_ime_conversion_;
    saved.sentence   = saved_ime_sentence_;
    saved.has_ime    = saved_ime_valid_;
    restore_input_layout(window_.GetNativeWindow(), saved);
    saved_keyboard_layout_ = nullptr;
    saved_ime_valid_       = false;
#endif
    ImGuizmo::Enable(false);
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

bool Editor::switch_demo(int index)
{
    if (index < 0 || index >= static_cast<int>(registry_.size()))
        return false;

    auto d = registry_.create(registry_.name_at(static_cast<size_t>(index)), assets_);
    if (!d)
    {
        LOG(ERROR) << "Failed to create demo: " << registry_.name_at(static_cast<size_t>(index));
        return false;
    }

    demo_           = std::move(d);
    demo_index_     = index;
    selected_index_ = -1;
    sync_fly_from_camera();
    LOG(INFO) << "Switched to demo '" << demo_->name() << "'";
    return true;
}

void Editor::sync_fly_from_camera()
{
    if (!demo_)
        return;
    const auto& cam  = demo_->scene().camera();
    camera_pos_      = cam.position;
    const glm::vec3 fwd = glm::normalize(cam.front);
    yaw_               = glm::degrees(std::atan2(fwd.x, -fwd.z));
    pitch_             = glm::degrees(std::asin(std::clamp(fwd.y, -1.0f, 1.0f)));
}

void Editor::sync_camera_from_fly()
{
    if (!demo_)
        return;
    const float yaw   = glm::radians(yaw_);
    const float pitch = glm::radians(pitch_);
    const glm::vec3 front{
        std::sin(yaw) * std::cos(pitch),
        std::sin(pitch),
        -std::cos(yaw) * std::cos(pitch),
    };
    demo_->scene().camera().look_at(camera_pos_, camera_pos_ + front);
}

void Editor::handle_edit_camera(float dt)
{
    if (!demo_ || demo_->scene().is_2d())
        return;

    GLFWwindow* win = window_.GetNativeWindow();

    const bool rmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    const bool mmb = glfwGetMouseButton(win, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;

    // 拖拽期间光标不可见，避免 ImGui 光标闪烁
    window_.SetCursorVisible(!rmb && !mmb);

    // 鼠标悬停在 ImGui 窗口上时不启动相机操作（已开始的拖拽继续）
    if (ImGui::GetIO().WantCaptureMouse && !rmb_down_ && !mmb_down_)
        return;

    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(win, &mx, &my);

    const bool camera_drag_start = (rmb && !rmb_down_) || (mmb && !mmb_down_);
    if (camera_drag_start)
    {
        last_mouse_x_ = mx;
        last_mouse_y_ = my;
    }

    const float dx = static_cast<float>(mx - last_mouse_x_);
    const float dy = static_cast<float>(my - last_mouse_y_);
    last_mouse_x_  = mx;
    last_mouse_y_  = my;

    // RMB 拖拽：旋转视角（鼠标向右 → 视野向右转，和游戏引擎一致）
    if (rmb && rmb_down_)
    {
        yaw_ += dx * camera_cfg_.orbit_speed;
        pitch_ = std::clamp(pitch_ - dy * camera_cfg_.orbit_speed, -89.0f, 89.0f);
    }

    sync_camera_from_fly();

    // MMB 拖拽：沿相机平面平移（right/up）
    if (mmb && mmb_down_)
    {
        const auto&     cam   = demo_->scene().camera();
        const glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.up));
        const float     pan_speed =
            std::max(0.5f, glm::length(cam.position - camera_pos_)) *
            camera_cfg_.pan_speed * 0.05f;
        camera_pos_ += (right * -dx + cam.up * dy) * pan_speed;
    }

    rmb_down_ = rmb;
    mmb_down_ = mmb;

    // WASD/QE 飞行：仅在按住 RMB 时生效，沿相机朝向移动（游戏引擎 FPS 模式）
    if (rmb)
    {
        const auto&     cam   = demo_->scene().camera();
        const glm::vec3 right = glm::normalize(glm::cross(cam.front, cam.up));
        const float     speed = camera_cfg_.fly_speed * dt;
        // 避免 S 往后倒时 A/D 反向：用标准 FPS 规则
        if (glfwGetKey(win, GLFW_KEY_W) == GLFW_PRESS)
            camera_pos_ += cam.front * speed;
        if (glfwGetKey(win, GLFW_KEY_S) == GLFW_PRESS)
            camera_pos_ -= cam.front * speed;
        if (glfwGetKey(win, GLFW_KEY_A) == GLFW_PRESS)
            camera_pos_ -= right * speed;
        if (glfwGetKey(win, GLFW_KEY_D) == GLFW_PRESS)
            camera_pos_ += right * speed;
        if (glfwGetKey(win, GLFW_KEY_Q) == GLFW_PRESS)
            camera_pos_ -= cam.up * speed;
        if (glfwGetKey(win, GLFW_KEY_E) == GLFW_PRESS)
            camera_pos_ += cam.up * speed;
    }

    sync_camera_from_fly();
}

glm::vec2 Editor::screen_to_world_2d(double screen_x, double screen_y) const
{
    const float win_w  = static_cast<float>(window_.Width());
    const float win_h  = static_cast<float>(window_.Height());
    const float vp_w   = win_w - panel_width_;
    const float vp_h   = win_h - status_height_;

    const float ndc_x = static_cast<float>(screen_x) / vp_w * 2.0f - 1.0f;
    const float ndc_y = 1.0f - static_cast<float>(screen_y) / vp_h * 2.0f;

    const glm::mat4 inv = glm::inverse(demo_->scene().camera().get_projection_matrix() *
                                       demo_->scene().camera().get_view_matrix());
    const glm::vec4 world = inv * glm::vec4(ndc_x, ndc_y, 0.0f, 1.0f);
    return {world.x / world.w, world.y / world.w};
}

void Editor::handle_click()
{
    if (!demo_)
        return;
    if (ImGui::GetIO().WantCaptureMouse || ImGuizmo::IsUsing() || ImGuizmo::IsOver())
        return;
    if (!ImGui::IsMouseClicked(ImGuiMouseButton_Left))
        return;

    GLFWwindow* win = window_.GetNativeWindow();

    int fbw = 0, fbh = 0, win_w = 0, win_h = 0;
    glfwGetFramebufferSize(win, &fbw, &fbh);
    glfwGetWindowSize(win, &win_w, &win_h);
    if (fbw <= 0 || fbh <= 0 || win_w <= 0 || win_h <= 0)
        return;

    const float sx = static_cast<float>(fbw) / static_cast<float>(win_w);
    const float sy = static_cast<float>(fbh) / static_cast<float>(win_h);
    const int   vp_w_fb = static_cast<int>(static_cast<float>(fbw) - panel_width_ * sx);
    const int   vp_h_fb = static_cast<int>(static_cast<float>(fbh) - status_height_ * sy);
    if (vp_w_fb <= 1 || vp_h_fb <= 1)
        return;

    double mx = 0.0, my = 0.0;
    glfwGetCursorPos(win, &mx, &my);
    const double mx_fb = mx * sx;
    const double my_fb = my * sy;
    if (mx_fb < 0.0 || my_fb < 0.0 || mx_fb >= vp_w_fb || my_fb >= vp_h_fb)
        return;

    Scene& scene = demo_->scene();

    // 3D：拾取物体 + gizmo；2D：拾取物体，未命中交给 demo 的 on_click
    if (id_picker_ && !scene.entities().empty())
    {
        const int hit = id_picker_->pick(scene, static_cast<int>(mx_fb),
                                         static_cast<int>(my_fb), vp_w_fb, vp_h_fb);
        if (hit >= 0)
        {
            selected_index_ = hit;
            return;
        }
    }

    selected_index_ = -1;
    if (scene.is_2d())
        demo_->on_click(screen_to_world_2d(mx, my));
}

void Editor::begin_frame()
{
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();
}

void Editor::end_frame()
{
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void Editor::draw_viewport(float aspect)
{
    if (!demo_)
        return;

    // aspect 收进相机，渲染器和拾取都直接用 camera.aspect
    demo_->scene().camera().aspect = aspect;

    if (!demo_->scene().is_2d())
        sync_camera_from_fly();

    demo_->draw();

    Scene& scene = demo_->scene();
    if (selected_index_ < 0 || selected_index_ >= static_cast<int>(scene.entities().size()))
        return;

    auto& entities = scene.entities();
    const float win_w = static_cast<float>(window_.Width());
    const float win_h = static_cast<float>(window_.Height());
    const float vp_w  = win_w - panel_width_;
    const float vp_h  = win_h - status_height_;
    const bool  is_2d = scene.is_2d();

    ImGuizmo::SetOrthographic(is_2d);
    ImGuizmo::SetDrawlist(ImGui::GetForegroundDrawList());
    ImGuizmo::SetRect(0.0f, 0.0f, vp_w, vp_h);

    auto&       transform = entities[selected_index_].transform;
    glm::mat4   model     = transform.to_model();
    const auto  view      = scene.camera().view();
    const auto  proj      = scene.camera().projection();

    ImGuizmo::OPERATION op    = ImGuizmo::TRANSLATE;
    ImGuizmo::MODE      space = ImGuizmo::WORLD;
    switch (gizmo_mode_)
    {
    case GizmoMode::Translate:
        op    = is_2d ? (ImGuizmo::TRANSLATE_X | ImGuizmo::TRANSLATE_Y) : ImGuizmo::TRANSLATE;
        space = ImGuizmo::WORLD;
        break;
    case GizmoMode::Rotate:
        op    = is_2d ? ImGuizmo::ROTATE_Z : ImGuizmo::ROTATE;
        space = ImGuizmo::LOCAL;
        break;
    case GizmoMode::Scale:
        op    = is_2d ? (ImGuizmo::SCALE_X | ImGuizmo::SCALE_Y) : ImGuizmo::SCALE;
        space = ImGuizmo::LOCAL;
        break;
    }

    if (ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(proj), op, space,
                             glm::value_ptr(model)))
    {
        glm::vec3 pos, rot_deg, scale;
        ImGuizmo::DecomposeMatrixToComponents(glm::value_ptr(model), glm::value_ptr(pos),
                                              glm::value_ptr(rot_deg), glm::value_ptr(scale));
        transform.position = pos;
        transform.scale    = scale;
        transform.rotation = glm::quat(glm::radians(rot_deg));
    }
}

void Editor::draw_ui(float fps)
{
    const float win_w = static_cast<float>(window_.Width());
    const float win_h = static_cast<float>(window_.Height());

    // ----- right inspector -----
    ImGui::SetNextWindowPos(ImVec2(win_w - panel_width_, 0.0f), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(panel_width_, win_h - status_height_), ImGuiCond_Always);
    ImGui::Begin("Editor", nullptr,
                 ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    if (demo_)
        ImGui::Text("Demo: %s  [%s]", demo_->name(), demo_->scene().is_2d() ? "2D" : "3D");
    ImGui::Separator();
    ImGui::TextUnformatted("Switch Demo");

    demo_combo_items_.clear();
    for (const auto& n : demo_names_)
    {
        demo_combo_items_ += n;
        demo_combo_items_.push_back('\0');
    }
    demo_combo_items_.push_back('\0');

    int pending = demo_index_;
    ImGui::SetNextItemWidth(-1);
    if (ImGui::Combo("##demo_combo", &pending, demo_combo_items_.c_str()))
    {
        if (pending != demo_index_)
            switch_demo(pending);
    }

    ImGui::Separator();
    if (demo_ && ImGui::Button("Reset Demo", ImVec2(-1, 0)))
        demo_->reset();

    if (demo_ && !demo_->scene_config_path().empty())
    {
        if (ImGui::Button("Save Scene", ImVec2(-1, 0)))
        {
            if (SaveScene(demo_->scene_config_path(), demo_->scene()))
                LOG(INFO) << "Scene saved: " << demo_->scene_config_path();
            else
                LOG(ERROR) << "Failed to save scene: " << demo_->scene_config_path();
        }
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Gizmo");
    if (ImGui::RadioButton("Translate", gizmo_mode_ == GizmoMode::Translate))
        gizmo_mode_ = GizmoMode::Translate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Rotate", gizmo_mode_ == GizmoMode::Rotate))
        gizmo_mode_ = GizmoMode::Rotate;
    ImGui::SameLine();
    if (ImGui::RadioButton("Scale", gizmo_mode_ == GizmoMode::Scale))
        gizmo_mode_ = GizmoMode::Scale;

    ImGui::Separator();
    ImGui::TextUnformatted("Inspector");
    if (demo_ && selected_index_ >= 0 &&
        selected_index_ < static_cast<int>(demo_->scene().entities().size()))
    {
        auto& entry = demo_->scene().entities()[selected_index_];
        auto& t     = entry.transform;
        ImGui::Text("Name: %s", entry.name.c_str());

        if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
        {
            ImGui::DragFloat3("Position", glm::value_ptr(t.position), 0.01f);
            glm::vec3 euler = glm::degrees(glm::eulerAngles(t.rotation));
            if (ImGui::DragFloat3("Rotation", glm::value_ptr(euler), 0.5f))
                t.rotation = glm::quat(glm::radians(euler));
            ImGui::DragFloat3("Scale", glm::value_ptr(t.scale), 0.01f, 0.001f, 1000.0f);
        }
    }
    else
    {
        ImGui::TextDisabled("Click an object to select");
    }

    ImGui::Separator();
    ImGui::TextUnformatted("Camera Speed");
    ImGui::DragFloat("Pan", &camera_cfg_.pan_speed, 0.01f, 0.01f, 20.0f, "%.2f");
    ImGui::DragFloat("Mouse", &camera_cfg_.orbit_speed, 0.1f, 0.01f, 5.0f, "%.2f");
    ImGui::DragFloat("Fly", &camera_cfg_.fly_speed, 0.01f, 0.01f, 20.0f, "%.2f");
    ImGui::DragFloat("Zoom", &camera_cfg_.zoom_speed, 0.01f, 0.01f, 1.0f, "%.2f");
    ImGui::Separator();
    ImGui::TextWrapped("Camera: RMB orbit (hold) + WASD/QE fly, MMB pan, Wheel zoom");

    // ----- demo 专属 UI（右侧面板底部）-----
    if (demo_)
    {
        ImGui::Separator();
        ImGui::TextUnformatted("Demo");
        demo_->draw_ui();
    }

    ImGui::End();

    // ----- status bar -----
    ImGui::SetNextWindowPos(ImVec2(0.0f, win_h - status_height_), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2(win_w, status_height_), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 4));
    ImGui::Begin("##statusbar", nullptr,
                 ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove |
                     ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoCollapse);
    if (demo_)
    {
        ImGui::Text("Demo: %s | FPS: %.1f | Meshes: %d", demo_->name(), fps,
                    demo_->scene().mesh_count());
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

void Editor::update(float dt)
{
    if (!demo_)
        return;

    // scroll zoom（3D）：沿相机朝向推进/后退（FPS 式缩放）
    if (!demo_->scene().is_2d() && !ImGui::GetIO().WantCaptureMouse)
    {
        const float wheel = ImGui::GetIO().MouseWheel;
        if (wheel != 0.0f)
        {
            const auto& cam = demo_->scene().camera();
            camera_pos_ += cam.front * wheel * (camera_cfg_.zoom_speed * 2.0f);
            sync_camera_from_fly();
        }
    }

    handle_edit_camera(dt);
    handle_click();
    demo_->update(dt);
}

void Editor::run()
{
    auto  last      = std::chrono::high_resolution_clock::now();
    float fps       = 0.0f;
    float fps_timer = 0.0f;
    int   fps_frames = 0;

    while (!window_.ShouldClose())
    {
        window_.PollEvents();

        const auto  now = std::chrono::high_resolution_clock::now();
        const float dt  = std::chrono::duration<float>(now - last).count();
        last            = now;

        fps_timer += dt;
        ++fps_frames;
        if (fps_timer >= 0.5f)
        {
            fps        = static_cast<float>(fps_frames) / fps_timer;
            fps_timer  = 0.0f;
            fps_frames = 0;
        }

        begin_frame();
        update(dt);

        const float win_w  = static_cast<float>(window_.Width());
        const float win_h  = static_cast<float>(window_.Height());
        const int   vp_w   = std::max(1, static_cast<int>(win_w - panel_width_));
        const int   vp_h   = std::max(1, static_cast<int>(win_h - status_height_));
        const float aspect = static_cast<float>(vp_w) / static_cast<float>(vp_h);

        glViewport(0, static_cast<int>(status_height_), vp_w, vp_h);
        draw_viewport(aspect);

        glViewport(0, 0, window_.Width(), window_.Height());
        draw_ui(fps);
        end_frame();

        window_.SwapBuffer();
    }

#if defined(_WIN32)
    // 主循环结束、窗口仍在时立刻恢复输入法（比只靠析构更稳）
    if (saved_keyboard_layout_)
    {
        SavedInputState saved;
        saved.hkl        = static_cast<HKL>(saved_keyboard_layout_);
        saved.conversion = saved_ime_conversion_;
        saved.sentence   = saved_ime_sentence_;
        saved.has_ime    = saved_ime_valid_;
        restore_input_layout(window_.GetNativeWindow(), saved);
        saved_keyboard_layout_ = nullptr;
        saved_ime_valid_       = false;
    }
#endif
}
