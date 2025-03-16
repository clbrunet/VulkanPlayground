#include "Window.hpp"

#include <cstdlib>
#include <cassert>
#include <algorithm>
#include <iostream>

Window::Window(char const* const title, uint16_t const width, uint16_t const height) {
	glfwSetErrorCallback([](int const error, char const* const description) noexcept {
		std::cerr << "GLFW error " << error << ": " << description << '\n';
	});
	if (!glfwInit()) {
		throw std::runtime_error{ "glfwInit" };
	}
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	m_window = glfwCreateWindow(width, height, title, nullptr, nullptr);
	if (m_window == nullptr) {
		throw std::runtime_error{ "glfwCreateWindow" };
	}

	glfwSetWindowUserPointer(m_window, this);

	glfwSetKeyCallback(m_window, [](GLFWwindow* const window, int const key,
		[[maybe_unused]] int const scancode, int const action, [[maybe_unused]] int const mods) noexcept {
		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
			glfwSetWindowShouldClose(window, true);
		}
	});

	if (glfwRawMouseMotionSupported()) {
		glfwSetInputMode(m_window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
	}
	glfwSetInputMode(m_window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
}

Window::Window(Window&& other) noexcept :
	m_window(other.m_window) {
	other.m_window = nullptr;
}

Window::~Window() {
	if (m_window == nullptr) {
		return;
	}
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

void Window::set_framebuffer_callback(std::function<void(uint16_t, uint16_t)> framebuffer_callback) {
	using NoExceptGLFWframebuffersizefun = void (*)(GLFWwindow* window, int width, int height) noexcept;
	auto callback = NoExceptGLFWframebuffersizefun{ nullptr };
	if (framebuffer_callback) {
		callback = [](GLFWwindow* const window, int const width, int const height) noexcept {
			reinterpret_cast<Window*>(glfwGetWindowUserPointer(window))->m_framebuffer_size_callback(static_cast<uint16_t>(width), static_cast<uint16_t>(height));
		};
	}
	glfwSetFramebufferSizeCallback(m_window, callback);
	m_framebuffer_size_callback = std::move(framebuffer_callback);
}

VkSurfaceKHR Window::create_surface(VkInstance const instance) {
	auto surface = VkSurfaceKHR{};
	if (glfwCreateWindowSurface(instance, m_window, nullptr, &surface) != VK_SUCCESS) {
		throw std::runtime_error{ "glfwCreateWindowSurface" };
	}
	return surface;
}

bool Window::should_close() const {
	return static_cast<bool>(glfwWindowShouldClose(m_window));
}

void Window::poll_events() {
	glfwPollEvents();
	auto const time = this->time();
	m_delta_time = time - m_last_time;
	m_last_time = time;
	auto const cursor_position = this->cursor_position();
	m_cursor_delta = cursor_position - m_last_cursor_position;
	m_last_cursor_position = cursor_position;
}

void Window::wait_for_valid_framebuffer() const {
	auto framebuffer_dimensions = this->framebuffer_dimensions();
	while (framebuffer_dimensions.x == 0 || framebuffer_dimensions.y == 0) {
		glfwWaitEvents();
		framebuffer_dimensions = this->framebuffer_dimensions();
	}
}

glm::ivec2 Window::framebuffer_dimensions() const {
	auto width = 0;
	auto height = 0;
	glfwGetFramebufferSize(m_window, &width, &height);
	return glm::ivec2{ width, height };
}

void Window::prepare_event_loop() {
	m_last_time = time();
	m_last_cursor_position = cursor_position();
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

glm::vec2 Window::cursor_position() const {
	auto x_position = 0.;
	auto y_position = 0.;
	glfwGetCursorPos(m_window, &x_position, &y_position);
	return glm::vec2{ x_position, y_position };
}

void Window::set_cursor_visibility(bool const cursor_visibility) const {
	glfwSetInputMode(m_window, GLFW_CURSOR, cursor_visibility ? GLFW_CURSOR_NORMAL : GLFW_CURSOR_DISABLED);
}

glm::vec2 Window::cursor_delta() const {
	return m_cursor_delta;
}
