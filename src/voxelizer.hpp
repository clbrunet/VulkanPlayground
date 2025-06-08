#pragma once

#include <glm/ext/vector_uint3.hpp>

#include <filesystem>
#include <functional>

[[nodiscard]] bool voxelize_model(std::filesystem::path const& path, uint32_t const side_voxel_count,
    std::function<void(glm::uvec3 const&)> const& voxel_importer);
