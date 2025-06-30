#pragma once

#include "Window.hpp"

#include <glm/glm.hpp>

class Camera {
public:
    Camera() = delete;
    Camera(glm::vec3 const& position, glm::vec2 euler_angles);

    [[nodiscard]] glm::vec3 const& position() const;
    [[nodiscard]] glm::mat3 const& rotation() const;

    void update(Window const& window);

private:
    static constexpr float DEGREE_PER_INPUT_SENSITIVITY = 0.25f;

    bool m_is_free_flying = false;
    glm::vec3 m_position = glm::vec3(0.f);
    float m_pitch = 0.f;
    float m_yaw = 0.f;
    glm::mat3 m_rotation;
    float m_speed = 80.f;

    void update_position(Window const& window);
    void update_rotation(Window const& window);
};
