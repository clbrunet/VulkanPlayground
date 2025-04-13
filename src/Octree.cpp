#include "Octree.hpp"

#include <array>
#include <algorithm>
#include <bit>

constexpr uint64_t operator""_u64(unsigned long long value) {
	return static_cast<uint64_t>(value);
}

Octree::Octree(uint8_t depth) :
	m_depth{ depth } {
	assert(depth <= MAX_DEPTH);
}

//CBTODO test in linux
#pragma warning(push)
#pragma warning(disable : 28020)
void Octree::add_voxel(glm::uvec3 const& voxel) {
	auto half_size = (1u << (m_depth * 2u)) / 2u;
	auto post_center = glm::uvec3{ half_size };
	auto hierarchy_index = 0u;
	auto nodes_hierarchy = std::array<BuildingOctreeNode*, MAX_DEPTH>{ { &m_root_building_node } };
	while (true) {
		auto& node = *nodes_hierarchy[hierarchy_index];
		auto octant_index = 0u
			| static_cast<uint32_t>(voxel.x >= post_center.x) * 2u
			| static_cast<uint32_t>(voxel.y >= post_center.y) * 32u
			| static_cast<uint32_t>(voxel.z >= post_center.z) * 8u;
		half_size /= 2u;
		post_center += half_size * (glm::uvec3{ octant_index & 2u, (octant_index & 32u) >> 4u, (octant_index & 8u) >> 2u } - 1u);
		octant_index |= 0u
			| static_cast<uint32_t>(voxel.x >= post_center.x) * 1u
			| static_cast<uint32_t>(voxel.y >= post_center.y) * 16u
			| static_cast<uint32_t>(voxel.z >= post_center.z) * 4u;
		if (half_size == 1u) {
			node.octants_mask |= (1_u64 << octant_index);
			break;
		}
		half_size /= 2u;
		post_center += half_size * (glm::uvec3{ (octant_index & 1u) * 2u, (octant_index & 16u) >> 3u, (octant_index & 4u) >> 1u } - 1u);
		if (node.is_leaf()) {
			node.octants.resize(64u);
			//CBTODO if node.octants_mask != 0 ?
			for (auto i = 0_u64; i < 64_u64; ++i) {
				node.octants[i].octants_mask = static_cast<uint64_t>((node.octants_mask & (1_u64 << i)) != 0_u64) * ~0_u64;
			}
		}
		node.octants_mask |= (1_u64 << octant_index);
		hierarchy_index += 1u;
		nodes_hierarchy[hierarchy_index] = &node.octants[octant_index];
	}
	if (nodes_hierarchy[hierarchy_index]->octants_mask != ~0_u64) {
		return;
	}
	while (hierarchy_index > 0u) {
		hierarchy_index -= 1u;
		auto& parent = *nodes_hierarchy[hierarchy_index];
		auto const can_merge = std::ranges::all_of(parent.octants, [](BuildingOctreeNode const& node) {
			return node.is_leaf() && (node.octants_mask == 0_u64 || node.octants_mask == ~0_u64);
		});
		if (!can_merge) {
			break;
		}
		parent.octants = std::vector<BuildingOctreeNode>{};
	}
}
#pragma warning(pop)

std::vector<OctreeNode> Octree::build_contiguous_nodes() const {
	auto nodes = std::vector<OctreeNode>{ 1u };
	auto const build = [&](auto const& self, BuildingOctreeNode const& building_node, OctreeNode& node) -> void {
		node.set_is_leaf(building_node.is_leaf());
		node.octants_mask = building_node.octants_mask;
		auto octant_index = std::size(nodes);
		if (!node.is_leaf()) {
			node.set_first_octant_node_index(octant_index & ((1u << 31u) - 1u));
			nodes.resize(octant_index + std::popcount(node.octants_mask));
		}
		for (auto const& building_octant : building_node.octants) {
			if (building_octant.octants_mask == 0u) {
				continue;
			}
			self(self, building_octant, nodes[octant_index]);
			octant_index += 1u;
		}
	};
	build(build, m_root_building_node, nodes[0]);
	return nodes;
}
