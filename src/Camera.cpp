#include "Camera.hpp"

#include <glm/gtx/euler_angles.hpp>

#include <numbers>

Camera::Camera(glm::vec3 const& position, glm::vec2 euler_angles) :
	m_position{ position },
	m_pitch{ euler_angles.x },
	m_yaw{ euler_angles.y },
	m_rotation{ glm::eulerAngleYX(m_yaw, m_pitch) } {
}

glm::vec3 const& Camera::position() const {
	return m_position;
}

glm::mat3 const& Camera::rotation() const {
	return m_rotation;
}

void Camera::update(GLFWwindow& window, float const time_delta, glm::vec2 const cursor_delta) {
	update_position(window, time_delta);
	update_rotation(cursor_delta);
}

void Camera::update_position(GLFWwindow& window, float const time_delta) {
	auto direction = glm::vec3(0.0f);
	if (glfwGetKey(&window, GLFW_KEY_W)) {
		direction.z -= 1.0f;
	}
	if (glfwGetKey(&window, GLFW_KEY_S)) {
		direction.z += 1.0f;
	}
	if (glfwGetKey(&window, GLFW_KEY_A)) {
		direction.x -= 1.0f;
	}
	if (glfwGetKey(&window, GLFW_KEY_D)) {
		direction.x += 1.0f;
	}
	if (glfwGetKey(&window, GLFW_KEY_Q)) {
		direction.y -= 1.0f;
	}
	if (glfwGetKey(&window, GLFW_KEY_E)) {
		direction.y += 1.0f;
	}
	if (direction == glm::vec3(0.0f)) {
		return;
	}
	auto speed_modifier = 1.f;
	if (glfwGetKey(&window, GLFW_KEY_LEFT_SHIFT)) {
		speed_modifier *= 2.f;
	}
	if (glfwGetKey(&window, GLFW_KEY_LEFT_ALT)) {
		speed_modifier /= 2.f;
	}
	m_position += time_delta * BASE_SPEED * speed_modifier * (m_rotation * glm::normalize(direction));
}

void Camera::update_rotation(glm::vec2 const cursor_delta) {
	glm::vec2 change = glm::radians(-DEGREE_PER_INPUT_SENSITIVITY) * cursor_delta;
	m_pitch = glm::clamp(m_pitch + change.y, -std::numbers::pi_v<float> / 2.f, std::numbers::pi_v<float> / 2.f);
	m_yaw += change.x;
	m_rotation = glm::eulerAngleYX(m_yaw, m_pitch);
}
