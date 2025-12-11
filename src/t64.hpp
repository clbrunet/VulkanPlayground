#pragma once

#include "Tree64.hpp"

#include <filesystem>
#include <optional>

namespace vp {

[[nodiscard]] std::optional<ContiguousTree64> import_t64(std::filesystem::path const& path);
[[nodiscard]] bool save_t64(std::filesystem::path const& path, ContiguousTree64 const& contiguous_tree64);

}
