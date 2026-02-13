#include "Window.hpp"
#include "filesystem.hpp"

#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#elif defined(__APPLE__)
#define GLFW_EXPOSE_NATIVE_COCOA
#endif
#include <nfd_glfw3.h>

#include <imgui_impl_glfw.h>

#include <cassert>
#include <iostream>

namespace vp {

Window::Window(char const* const title, glm::uvec2 const dimensions) {
    glfwSetErrorCallback([](int const error, char const* const description) noexcept {
        std::cerr << "GLFW error " << error << ": " << description << std::endl;
    });
    if (!glfwInit()) {
        throw std::runtime_error("glfwInit");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    m_window = glfwCreateWindow(static_cast<int>(dimensions.x), static_cast<int>(dimensions.y), title, nullptr, nullptr);
    if (m_window == nullptr) {
        throw std::runtime_error("glfwCreateWindow");
    }
    glfwSetWindowUserPointer(m_window, this);
    glfwSetScrollCallback(m_window, [](GLFWwindow* const window, [[maybe_unused]] double const xoffset, double const yoffset) noexcept {
        reinterpret_cast<Window*>(glfwGetWindowUserPointer(window))->m_scroll_delta = static_cast<float>(yoffset);
    });
    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    }

    NFD::Init();
    NFD_GetNativeWindowFromGLFWWindow(m_window, &m_native_handle);

}

Window::Window(Window&& other) noexcept :
    m_window(other.m_window) {
    other.m_window = nullptr;
}

Window::~Window() {
    if (m_window == nullptr) {
        return;
    }
    NFD::Quit();
    glfwDestroyWindow(m_window);
    glfwTerminate();
}

void Window::set_key_callback(std::function<void(int, int, int)> key_callback) {
    using NoExceptGLFWkeyfun = void (*)(GLFWwindow* window, int key, int scancode, int action, int mods) noexcept;
    auto callback = NoExceptGLFWkeyfun{ nullptr };
    if (key_callback) {
        callback = [](GLFWwindow* const window, int const key, [[maybe_unused]] int const scancode,
            int const action, [[maybe_unused]] int const mods) noexcept {
            reinterpret_cast<Window*>(glfwGetWindowUserPointer(window))->m_key_callback(key, action, mods);
        };
    }
    glfwSetKeyCallback(m_window, callback);
    m_key_callback = std::move(key_callback);
}

void Window::set_framebuffer_callback(std::function<void(int, int)> framebuffer_callback) {
    using NoExceptGLFWframebuffersizefun = void (*)(GLFWwindow* window, int width, int height) noexcept;
    auto callback = NoExceptGLFWframebuffersizefun{ nullptr };
    if (framebuffer_callback) {
        callback = [](GLFWwindow* const window, int const width, int const height) noexcept {
            reinterpret_cast<Window*>(glfwGetWindowUserPointer(window))->m_framebuffer_size_callback(width, height);
        };
    }
    glfwSetFramebufferSizeCallback(m_window, callback);
    m_framebuffer_size_callback = std::move(framebuffer_callback);
}

std::span<char const* const> Window::get_required_instance_extensions() const {
    auto extension_count = uint32_t{};
    auto const extensions = glfwGetRequiredInstanceExtensions(&extension_count);
    return std::span<char const* const>(extensions, extension_count);
}

VkSurfaceKHR Window::create_surface(VkInstance const instance) const {
    auto surface = VkSurfaceKHR{};
    if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS) {
        throw std::runtime_error("glfwCreateWindowSurface");
    }
    return surface;
}

void Window::init_imgui_for_vulkan() {
    ImGui_ImplGlfw_InitForVulkan(m_window, true);
}

void Window::prepare_event_loop() {
    poll_events();
    m_delta_time = 0.f;
    m_scroll_delta = 0.f;
    m_cursor_delta = glm::vec2(0.f);
}

void Window::poll_events() {
    m_scroll_delta = 0.f;
    glfwPollEvents();
    auto const time = this->time();
    m_delta_time = time - m_last_time;
    m_last_time = time;
    auto const cursor_position = this->cursor_position();
    m_cursor_delta = cursor_position - m_last_cursor_position;
    m_last_cursor_position = cursor_position;
}

bool Window::should_close() const {
    return static_cast<bool>(glfwWindowShouldClose(m_window));
}

void Window::set_should_close(bool const should_close) {
    glfwSetWindowShouldClose(m_window, should_close);
}

glm::ivec2 Window::framebuffer_dimensions() const {
    auto width = 0;
    auto height = 0;
    glfwGetFramebufferSize(m_window, &width, &height);
    return glm::ivec2(width, height);
}

glm::uvec2 Window::wait_for_valid_framebuffer() const {
    auto framebuffer_dimensions = this->framebuffer_dimensions();
    while (framebuffer_dimensions.x <= 0 || framebuffer_dimensions.y <= 0) {
        glfwWaitEvents();
        framebuffer_dimensions = this->framebuffer_dimensions();
    }
    return framebuffer_dimensions;
}

bool Window::fullscreen_status() {
    return glfwGetWindowMonitor(m_window) != nullptr;
}

void Window::set_fullscreen_status(bool const fullscreen_status) {
    if (fullscreen_status == this->fullscreen_status()) {
        return;
    }
    if (fullscreen_status) {
        glfwGetWindowPos(m_window, &m_postion_before_fullscreen.x, &m_postion_before_fullscreen.y);
        glfwGetWindowSize(m_window, &m_size_before_fullscreen.x, &m_size_before_fullscreen.y);
        auto* const monitor = glfwGetPrimaryMonitor();
        auto const& video_mode = *glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(m_window, monitor, 0, 0, video_mode.width, video_mode.height, GLFW_DONT_CARE);
    } else {
        glfwSetWindowMonitor(m_window, nullptr, m_postion_before_fullscreen.x, m_postion_before_fullscreen.y,
            m_size_before_fullscreen.x, m_size_before_fullscreen.y, GLFW_DONT_CARE);
    }
}

float Window::time() const {
    return static_cast<float>(glfwGetTime());
}

float Window::delta_time() const {
    return m_delta_time;
}

bool Window::is_key_pressed(int const key) const {
    return glfwGetKey(m_window, key) == GLFW_PRESS;
}

bool Window::is_mouse_button_pressed(int const button) const {
    return glfwGetMouseButton(m_window, button) == GLFW_PRESS;
}

float Window::scroll_delta() const {
    return m_scroll_delta;
}

glm::vec2 Window::cursor_position() const {
    auto x_position = 0.;
    auto y_position = 0.;
    glfwGetCursorPos(m_window, &x_position, &y_position);
    return glm::vec2(x_position, y_position);
}

void Window::set_cursor_visibility(bool const cursor_visibility) const {
    glfwSetInputMode(m_window, GLFW_CURSOR, cursor_visibility ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

glm::vec2 Window::cursor_delta() const {
    return m_cursor_delta;
}

std::optional<std::filesystem::path> Window::pick_file(std::span<nfdu8filteritem_t const> filters,
    std::filesystem::path const& default_path) const {
    auto path = NFD::UniquePathU8();
    auto const result = NFD::OpenDialog(path, std::data(filters), static_cast<nfdfiltersize_t>(std::size(filters)),
        string_from(default_path).c_str(), m_native_handle);
    if (result == NFD_OKAY) {
        return path_from(path.get());
    } else if (result != NFD_CANCEL) {
        std::cerr << NFD::GetError() << std::endl;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> Window::pick_saving_path(std::span<nfdu8filteritem_t const> filters,
    std::filesystem::path const& default_path, nfdu8char_t const* default_name) const {
    auto path = NFD::UniquePathU8();
    auto const result = NFD::SaveDialog(path, std::data(filters), static_cast<nfdfiltersize_t>(std::size(filters)),
        string_from(default_path).c_str(), default_name, m_native_handle);
    if (result == NFD_OKAY) {
        return path_from(path.get());
    } else if (result != NFD_CANCEL) {
        std::cerr << NFD::GetError() << std::endl;
    }
    return std::nullopt;
}

}
