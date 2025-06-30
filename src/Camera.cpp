#include "Camera.hpp"

#include <glm/gtx/euler_angles.hpp>
#include <glm/gtx/io.hpp>

#include <numbers>
#include <iostream>

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

void Camera::update(Window const& window) {
    if (!window.is_mouse_button_pressed(GLFW_MOUSE_BUTTON_RIGHT)) {
        if (m_is_free_flying) {
            m_is_free_flying = false;
            window.set_cursor_visibility(true);
        }
        return;
    }
    if (!m_is_free_flying) {
        m_is_free_flying = true;
        window.set_cursor_visibility(false);
        return;
    }
    m_speed *= glm::pow(1.1f, window.scroll_delta());
    update_position(window);
    update_rotation(window);
}

void Camera::update_position(Window const& window) {
    auto const direction = glm::vec3{
        static_cast<float>(window.is_key_pressed(GLFW_KEY_D)) - static_cast<float>(window.is_key_pressed(GLFW_KEY_A)),
        static_cast<float>(window.is_key_pressed(GLFW_KEY_E)) - static_cast<float>(window.is_key_pressed(GLFW_KEY_Q)),
        static_cast<float>(window.is_key_pressed(GLFW_KEY_W)) - static_cast<float>(window.is_key_pressed(GLFW_KEY_S))
    };
    if (direction == glm::vec3{ 0.f }) {
        return;
    }

    auto speed_modifier = 1.f;
    if (window.is_key_pressed(GLFW_KEY_LEFT_SHIFT)) {
        speed_modifier *= 2.f;
    }
    if (window.is_key_pressed(GLFW_KEY_LEFT_ALT)) {
        speed_modifier /= 2.f;
    }
    m_position += window.delta_time() * m_speed * speed_modifier * (m_rotation * glm::normalize(direction));
}

void Camera::update_rotation(Window const& window) {
    auto const change = glm::radians(DEGREE_PER_INPUT_SENSITIVITY) * window.cursor_delta();
    m_pitch = glm::clamp(m_pitch + change.y, -std::numbers::pi_v<float> / 2.f, std::numbers::pi_v<float> / 2.f);
    m_yaw += change.x;
    m_rotation = glm::eulerAngleYX(m_yaw, m_pitch);
}
