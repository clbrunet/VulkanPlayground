#pragma once

#include <functional>
#include <optional>

#include <GLFW/glfw3.h>
#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>

class Window {
public:
	Window(char const* title, uint16_t width, uint16_t height);
	Window(Window&& other) noexcept;
	Window(Window const& other) = delete;
	Window& operator=(Window const& other) = delete;
	~Window();

	void set_framebuffer_callback(std::function<void(uint16_t, uint16_t)> framebuffer_callback);

	VkSurfaceKHR create_surface(VkInstance instance);

	void prepare_event_loop();
	void poll_events();
	void wait_for_valid_framebuffer() const;

	[[nodiscard]] bool should_close() const;
	[[nodiscard]] glm::ivec2 framebuffer_dimensions() const;

	[[nodiscard]] float time() const;
	[[nodiscard]] float delta_time() const;

	[[nodiscard]] bool is_key_pressed(int key) const;

	[[nodiscard]] bool is_mouse_button_pressed(int button) const;

	[[nodiscard]] glm::vec2 cursor_position() const;
	void set_cursor_visibility(bool cursor_visibility) const;
	[[nodiscard]] glm::vec2 cursor_delta() const;

private:
	GLFWwindow* m_window = nullptr;
	std::function<void(uint16_t, uint16_t)> m_framebuffer_size_callback;
	float m_last_time = 0.f;
	float m_delta_time = 0.f;
	glm::vec2 m_last_cursor_position = glm::vec2{ 0.f };
	glm::vec2 m_cursor_delta = glm::vec2{ 0.f };
};
