#include "Tree64.hpp"
#include "math.hpp"
#include "filesystem.hpp"
#include "vox.hpp"
#include "voxelizer.hpp"

#include <array>
#include <iostream>
#include <algorithm>

namespace vp {

std::optional<Tree64> Tree64::voxelize_model(std::filesystem::path const& path, uint32_t const max_side_voxel_count) {
    auto const depth = divide_ceil(static_cast<uint8_t>(std::bit_width(max_side_voxel_count - 1u)), uint8_t{ 2u });
    if (depth > MAX_DEPTH) {
        std::cerr << "Exceeded the max voxel size " << 1u << (MAX_DEPTH * 2u) << std::endl;
        return std::nullopt;
    }
    auto tree64 = Tree64{ depth };
    auto const success = ::voxelize_model(path, max_side_voxel_count, [&](glm::uvec3 const& voxel) {
        tree64.add_voxel(voxel);
    });
    if (!success) {
        return std::nullopt;
    }
    return tree64;
}

std::optional<Tree64> Tree64::import_vox(std::filesystem::path const& path) {
    std::optional<Tree64> tree64;
    auto const success = ::import_vox(path, [&](glm::uvec3 const& vox_full_size) {
        auto const max = glm::max(4u, max_component(vox_full_size));
        auto depth = divide_ceil(static_cast<uint8_t>(std::bit_width(max - 1u)), uint8_t{ 2u });
        if (depth > Tree64::MAX_DEPTH) {
            std::cerr << "Vox \"" << string_from(path) << "\" exceeds the max voxel size " << 1u << (MAX_DEPTH * 2u) << std::endl;
            return false;
        }
        tree64 = Tree64{ depth };
        return true;
    }, [&](glm::uvec3 const& voxel) {
        tree64->add_voxel(voxel);
    });
    if (!success) {
        return std::nullopt;
    }
    return tree64;
}

Tree64::Tree64(uint8_t depth) :
    m_depth{ depth } {
    assert(depth <= MAX_DEPTH);
}

uint8_t Tree64::depth() const {
    return m_depth;
}

std::vector<Tree64Node> Tree64::build_contiguous_nodes() const {
    auto nodes = std::vector<Tree64Node>{ 1u };
    auto const build = [&](auto const& self, BuildingTree64Node const& building_node, Tree64Node& node) -> void {
        node.set_is_leaf(building_node.is_leaf());
        node.set_children_mask(building_node.children_mask);
        auto child_index = std::size(nodes);
        if (!node.is_leaf()) {
            node.set_first_child_node_index(static_cast<uint32_t>(child_index));
            nodes.resize(child_index + static_cast<size_t>(std::popcount(building_node.children_mask)));
        }
        for (auto const& building_child : building_node.children) {
            if (building_child.children_mask == 0u) {
                continue;
            }
            self(self, building_child, nodes[child_index]);
            child_index += 1u;
        }
    };
    build(build, m_root_building_node, nodes[0]);
    return nodes;
}

void Tree64::add_voxel(glm::uvec3 const& voxel) {
    auto half_size = (1u << (m_depth * 2u)) / 2u;
    auto post_center = glm::uvec3{ half_size };
    auto hierarchy_index = 0u;
    auto nodes_hierarchy = std::array<BuildingTree64Node*, MAX_DEPTH>{ { &m_root_building_node } };
    while (true) {
        auto& node = *nodes_hierarchy[hierarchy_index];
        auto child_index = 0u
            | static_cast<uint32_t>(voxel.x >= post_center.x) * 2u
            | static_cast<uint32_t>(voxel.y >= post_center.y) * 32u
            | static_cast<uint32_t>(voxel.z >= post_center.z) * 8u;
        half_size /= 2u;
        post_center += half_size * (glm::uvec3{ child_index & 2u, (child_index & 32u) >> 4u, (child_index & 8u) >> 2u } - 1u);
        child_index |= 0u
            | static_cast<uint32_t>(voxel.x >= post_center.x) * 1u
            | static_cast<uint32_t>(voxel.y >= post_center.y) * 16u
            | static_cast<uint32_t>(voxel.z >= post_center.z) * 4u;
        if (half_size == 1u) {
            node.children_mask |= (1_u64 << child_index);
            break;
        }
        half_size /= 2u;
        post_center += half_size * (glm::uvec3{ (child_index & 1u) * 2u, (child_index & 16u) >> 3u, (child_index & 4u) >> 1u } - 1u);
        if (node.is_leaf()) {
            node.children.resize(64u);
            for (auto i = 0_u64; i < 64_u64; ++i) {
                node.children[i].children_mask = (node.children_mask & (1_u64 << i)) != 0_u64 ? ~0_u64 : 0_u64;
            }
        }
        node.children_mask |= (1_u64 << child_index);
        hierarchy_index += 1u;
        nodes_hierarchy[hierarchy_index] = &node.children[child_index];
    }
    if (nodes_hierarchy[hierarchy_index]->children_mask != ~0_u64) {
        return;
    }
    while (hierarchy_index > 0u) {
        hierarchy_index -= 1u;
        auto& parent = *nodes_hierarchy[hierarchy_index];
        auto const can_merge = std::ranges::all_of(parent.children, [](BuildingTree64Node const& node) {
            return node.is_leaf() && (node.children_mask == 0_u64 || node.children_mask == ~0_u64);
        });
        if (!can_merge) {
            break;
        }
        parent.children = std::vector<BuildingTree64Node>{};
    }
}

}
