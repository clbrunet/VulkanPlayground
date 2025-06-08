#pragma once

#include <glm/ext/vector_uint3.hpp>

#include <filesystem>
#include <functional>

[[nodiscard]] bool import_vox(std::filesystem::path const& path,
    std::function<bool(glm::uvec3 const&)> const& vox_full_size_importer,
    std::function<void(glm::uvec3 const&)> const& voxel_importer);
