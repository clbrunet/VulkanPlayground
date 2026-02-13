#pragma once

#include <filesystem>
#include <functional>

#include <GLFW/glfw3.h>
#include <nfd.hpp>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

namespace vp {

class Window {
public:
    Window(char const* title, glm::uvec2 dimensions);

    Window(Window const& other) = delete;
    Window(Window&& other) noexcept;

    ~Window();

    Window& operator=(Window const& other) = delete;
    Window& operator=(Window&& other) = delete;

    // callback arguments: int key, int action, int mods
    void set_key_callback(std::function<void(int, int, int)> key_callback);
    // callback arguments: int width, int height
    void set_framebuffer_callback(std::function<void(int, int)> framebuffer_callback);

    std::span<char const* const> get_required_instance_extensions() const;
    VkSurfaceKHR create_surface(VkInstance instance) const;
    void init_imgui_for_vulkan();

    void prepare_event_loop();
    void poll_events();

    [[nodiscard]] bool should_close() const;
    void set_should_close(bool should_close);

    [[nodiscard]] glm::ivec2 framebuffer_dimensions() const;
    glm::uvec2 wait_for_valid_framebuffer() const;

    [[nodiscard]] bool fullscreen_status();
    void set_fullscreen_status(bool fullscreen_status);

    [[nodiscard]] float time() const;
    [[nodiscard]] float delta_time() const;

    [[nodiscard]] bool is_key_pressed(int key) const;

    [[nodiscard]] bool is_mouse_button_pressed(int button) const;
    [[nodiscard]] float scroll_delta() const;

    [[nodiscard]] glm::vec2 cursor_position() const;
    void set_cursor_visibility(bool cursor_visibility) const;
    [[nodiscard]] glm::vec2 cursor_delta() const;

    [[nodiscard]] std::optional<std::filesystem::path> pick_file(std::span<nfdu8filteritem_t const> filters,
        std::filesystem::path const& default_path) const;

    [[nodiscard]] std::optional<std::filesystem::path> pick_saving_path(std::span<nfdu8filteritem_t const> filters,
        std::filesystem::path const& default_path, nfdu8char_t const* default_name = nullptr) const;

private:
    GLFWwindow* m_window = nullptr;
    nfdwindowhandle_t m_native_handle = {};
    std::function<void(int, int, int)> m_key_callback;
    std::function<void(int, int)> m_framebuffer_size_callback;
    float m_last_time = 0.f;
    float m_delta_time = 0.f;
    glm::vec2 m_last_cursor_position = glm::vec2(0.f);
    glm::vec2 m_cursor_delta = glm::vec2(0.f);
    float m_scroll_delta = 0.f;
    glm::ivec2 m_postion_before_fullscreen;
    glm::ivec2 m_size_before_fullscreen;
};

}
