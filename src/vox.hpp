#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <functional>

[[nodiscard]]
bool import_vox(std::filesystem::path const& path, std::function<void(glm::ivec3 const&)> const vox_full_size,
	std::function<void(glm::ivec3 const&)> const import_voxel);
