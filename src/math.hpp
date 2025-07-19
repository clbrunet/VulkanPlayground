#pragma once

#include <glm/vec3.hpp>
#include <assimp/vector3.h>

template<typename T>
inline constexpr T divide_ceil(T const& a, T const& b) {
    return T((a + b - T(1)) / b);
}

template<glm::length_t L, typename T, glm::qualifier Q>
inline constexpr T min_component(glm::vec<L, T, Q> const& vec) {
    auto min = vec[0];
    for (auto i = 1u; i < L; ++i) {
        if (vec[i] < min) {
            min = vec[i];
        }
    }
    return min;
}

template<glm::length_t L, typename T, glm::qualifier Q>
inline constexpr T max_component(glm::vec<L, T, Q> const& vec) {
    auto max = vec[0];
    for (auto i = 1; i < L; ++i) {
        if (vec[i] > max) {
            max = vec[i];
        }
    }
    return max;
}

inline constexpr glm::vec3 vec3_from_aivec3(aiVector3D const& vec3) {
    return glm::vec3(vec3.x, vec3.y, vec3.z);
}
