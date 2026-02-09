#pragma once

#include <glm/vec3.hpp>
#include <glm/vec4.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/common.hpp>
#include <glm/trigonometric.hpp>
#include <assimp/vector3.h>
#include <imgui.h>

template<typename T1, typename T2>
inline constexpr T1 divide_ceil(T1 const& a, T2 const& b) {
    return T1((a + b - 1u) / b);
}

template<typename T>
inline constexpr T normalized_angle(T radians_angle) {
    return radians_angle - glm::two_pi<T>()
        * glm::floor((radians_angle + glm::pi<T>()) / glm::two_pi<T>());
}

inline constexpr glm::vec3 cartesian_direction_from_spherical(float const polar_angle, float const azimuthal_angle) {
    return glm::vec3(
        glm::cos(polar_angle) * glm::sin(azimuthal_angle),
        glm::sin(polar_angle),
        glm::cos(polar_angle) * glm::cos(azimuthal_angle)
    );
}

inline constexpr glm::vec3 vec3_from(aiVector3D const& vec3) {
    return glm::vec3(vec3.x, vec3.y, vec3.z);
}

inline constexpr glm::vec4 vec4_from(ImVec4 const& vec4) {
    return glm::vec4(vec4.x, vec4.y, vec4.z, vec4.w);
}

inline constexpr ImVec4 imvec4_from(glm::vec4 const& vec4) {
    return ImVec4(vec4.x, vec4.y, vec4.z, vec4.w);
}
