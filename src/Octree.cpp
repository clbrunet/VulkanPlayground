#include "Octree.hpp"

#include <algorithm>

Octree::Octree(uint32_t depth) :
	m_depth{ depth } {
	assert(depth <= MAX_DEPTH);
}

void Octree::add_voxel(glm::uvec3 const& voxel) {
	auto half_size = (1u << m_depth) / 2u;
	auto post_center = glm::uvec3{ half_size };
	auto node_it = std::begin(m_nodes);
	while (true) {
		auto octant_index = 0u;
		if (voxel.x >= post_center.x) {
			octant_index |= 1u;
		}
		if (voxel.y >= post_center.y) {
			octant_index |= 4u;
		}
		if (voxel.z >= post_center.z) {
			octant_index |= 2u;
		}
		node_it->octants_mask |= (1u << octant_index);
		if (half_size == 1u) {
			node_it->is_leaf = true;
		}
		if (node_it->is_leaf) {
			break;
		}
		half_size /= 2u;
		post_center += half_size * (glm::uvec3{ (octant_index & 1u) * 2u, (octant_index & 4u) / 2u, octant_index & 2u } - 1u);
		octant_index += node_it->first_octant_node_index;
		if (node_it->first_octant_node_index == 0u) {
			node_it->first_octant_node_index = std::size(m_nodes) & 0x7FFFFFu;
			octant_index += node_it->first_octant_node_index;
			m_nodes.resize(std::size(m_nodes) + 8u);
		}
		node_it = std::begin(m_nodes) + octant_index;
	}
}

void Octree::shrink() {
	auto indices_to_erase = std::vector<uint32_t>{};
	auto const merge = [&](auto const& self, OctreeNode& node) {
		if (node.is_leaf || node.octants_mask == 0u) {
			return;
		}
		auto const octants = std::span{ std::begin(m_nodes) + node.first_octant_node_index, 8u };
		auto is_full = true;
		for (auto& octant : octants) {
			self(self, octant);
			is_full &= octant.is_leaf && octant.octants_mask == 0xFFu;
		}
		if (is_full) {
			indices_to_erase.emplace_back(static_cast<uint32_t>(node.first_octant_node_index));
			node.is_leaf = true;
			node.first_octant_node_index = 0u;
		}
	};
	merge(merge, m_nodes[0]);
	if (std::empty(indices_to_erase)) {
		return;
	}
	std::ranges::sort(indices_to_erase);
	auto index_to_erase_it = std::cbegin(indices_to_erase);
	auto index = 0u;
	std::erase_if(m_nodes, [&](OctreeNode& node) {
		if (index_to_erase_it != std::cend(indices_to_erase) && index >= *index_to_erase_it) {
			if (index - *index_to_erase_it == 8u - 1u) {
				index_to_erase_it += 1;
			}
			index += 1u;
			return true;
		}
		if (node.first_octant_node_index != 0u) {
			auto const distance = std::distance(std::begin(indices_to_erase), std::ranges::upper_bound(indices_to_erase, node.first_octant_node_index));
			node.first_octant_node_index -= 8u * static_cast<uint32_t>(distance);
		}
		index += 1u;
		return false;
	});
}

std::span<const OctreeNode> Octree::nodes() const {
	return std::span{ m_nodes };
}
