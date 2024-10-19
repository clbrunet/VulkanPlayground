#pragma once

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>

class Camera {
public:
	Camera() = delete;
	Camera(glm::vec3 const& position, glm::vec2 euler_angles);

	glm::vec3 const& position() const;
	glm::mat3 const& rotation() const;

	void update(GLFWwindow& window, float time_delta, glm::vec2 cursor_delta);

private:
	static constexpr float DEGREE_PER_INPUT_SENSITIVITY = 0.3f;
	static constexpr float BASE_SPEED = 50.0f;

	glm::vec3 m_position;
	float m_pitch = 0.0f;
	float m_yaw = 0.0f;
	glm::mat3 m_rotation;

	void update_position(GLFWwindow& window, float time_delta);
	void update_rotation(glm::vec2 cursor_delta);
};
