#pragma once

#include <glm/glm.hpp>

#include <filesystem>
#include <functional>

[[nodiscard]]
bool import_vox(std::filesystem::path const& path, std::function<void(glm::uvec3 const&)> const vox_full_size_importer,
	std::function<void(glm::ivec3 const&)> const voxel_importer);
