#include "Octree.hpp"

#include <array>
#include <algorithm>

Octree::Octree(uint32_t depth) :
	m_depth{ depth } {
	assert(depth <= MAX_DEPTH);
}

void Octree::add_voxel(glm::uvec3 const& voxel) {
	auto half_size = (1u << m_depth) / 2u;
	auto post_center = glm::uvec3{ half_size };
	auto hierarchy_index = 0u;
	auto nodes_hierarchy = std::array<BuildingOctreeNode*, MAX_DEPTH>{ { &m_root_building_node } };
	while (true) {
		auto& node = *nodes_hierarchy[hierarchy_index];
		auto const octant_index = 0u
			| static_cast<uint32_t>(voxel.x >= post_center.x) * 1u
			| static_cast<uint32_t>(voxel.y >= post_center.y) * 4u
			| static_cast<uint32_t>(voxel.z >= post_center.z) * 2u;
		if (half_size == 1u) {
			node.octants_mask |= (1u << octant_index);
			break;
		}
		half_size /= 2u;
		post_center += half_size * (glm::uvec3{ (octant_index & 1u) * 2u, (octant_index & 4u) / 2u, octant_index & 2u } - 1u);
		if (node.is_leaf()) {
			node.octants.resize(8u);
			for (auto i = 0u; i < 8u; ++i) {
				node.octants[i].octants_mask = static_cast<uint8_t>((node.octants_mask & (1u << i)) != 0u) * 0xFFu;
			}
		}
		node.octants_mask |= (1u << octant_index);
		hierarchy_index += 1u;
		nodes_hierarchy[hierarchy_index] = &node.octants[octant_index];
	}
	if (nodes_hierarchy[hierarchy_index]->octants_mask != 0xFFu) {
		return;
	}
	while (hierarchy_index > 0u) {
		hierarchy_index -= 1u;
		auto& parent = *nodes_hierarchy[hierarchy_index];
		auto const can_merge = std::ranges::all_of(parent.octants, [](BuildingOctreeNode const& node) {
			return node.is_leaf() && (node.octants_mask == 0u || node.octants_mask == 0xFFu);
		});
		if (!can_merge) {
			break;
		}
		parent.octants = std::vector<BuildingOctreeNode>{};
	}
}

std::vector<OctreeNode> Octree::build_contiguous_nodes() const {
	auto nodes = std::vector<OctreeNode>{ 1u };
	auto const build = [&](auto const& self, BuildingOctreeNode const& building_node, OctreeNode& node) -> void {
		node.is_leaf = building_node.is_leaf();
		node.octants_mask = building_node.octants_mask;
		auto octant_index = std::size(nodes);
		if (!node.is_leaf) {
			node.first_octant_node_index = std::size(nodes) & 0x7FFFFFu;
			nodes.resize(std::size(nodes) + 8u);
		}
		for (auto const& building_octant : building_node.octants) {
			self(self, building_octant, nodes[octant_index]);
			octant_index += 1u;
		}
	};
	build(build, m_root_building_node, nodes[0]);
	return nodes;
}
