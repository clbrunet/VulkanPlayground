#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>
#include <span>
#include <filesystem>
#include <optional>

namespace vp {

consteval uint64_t operator""_u64(unsigned long long const value) {
    return static_cast<uint64_t>(value);
}

#pragma pack(push, 1)
struct Tree64Node {
    uint32_t up_children_mask = 0u; //   (1 2 0) -> 0b1, (0 2 1) -> 0b10000, (0 3 0) -> 0b1'00000000'00000000
    uint32_t down_children_mask = 0u; // (1 0 0) -> 0b1, (0 0 1) -> 0b10000, (0 1 0) -> 0b1'00000000'00000000
    uint32_t is_leaf_and_first_child_node_index = 1u; // least significant bit -> is_leaf, 31 other bits -> first_child_node_index

    void set_children_mask(uint64_t const children_mask) {
        up_children_mask = static_cast<uint32_t>(children_mask >> 32_u64);
        down_children_mask = static_cast<uint32_t>(children_mask);
    }

    [[nodiscard]] bool is_leaf() const {
        return (is_leaf_and_first_child_node_index & 1u) == 1u;
    }

    void set_is_leaf(bool const is_leaf) {
        is_leaf_and_first_child_node_index = (is_leaf_and_first_child_node_index & ~1u) | static_cast<uint32_t>(is_leaf);
    }

    [[nodiscard]] uint32_t first_child_node_index() const {
        return is_leaf_and_first_child_node_index >> 1u;
    }

    void set_first_child_node_index(uint32_t const first_child_node_index) {
        is_leaf_and_first_child_node_index = (is_leaf_and_first_child_node_index & 1u) | (first_child_node_index << 1u);
    }
};
#pragma pack(pop)

struct ContiguousTree64 {
    uint8_t depth;
    std::vector<Tree64Node> nodes;
};

struct BuildingTree64Node {
    uint64_t children_mask = 0u; // (1 0 0) -> 0b1, (0 0 1) -> 0b10000, (0 1 0) -> 0b1'00000000'00000000
    std::vector<BuildingTree64Node> children;

    [[nodiscard]] bool is_leaf() const {
        return std::empty(children);
    }
};

class Tree64 {
public:
    static constexpr auto MAX_DEPTH = uint8_t{ 11u };

    [[nodiscard]] static std::optional<Tree64> voxelize_model(std::filesystem::path const& path, uint32_t max_side_voxel_count);
    [[nodiscard]] static std::optional<Tree64> import_vox(std::filesystem::path const& path);

    Tree64(uint8_t depth);

    uint8_t depth() const;
    std::vector<Tree64Node> build_contiguous_nodes() const;

    void add_voxel(glm::uvec3 const& voxel);

private:
    uint8_t m_depth;
    BuildingTree64Node m_root_building_node;
};

}
