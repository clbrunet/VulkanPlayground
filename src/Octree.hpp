#pragma once

#include <glm/glm.hpp>

#include <vector>
#include <span>

struct OctreeNode {
	uint32_t is_leaf : 1 = 1u;
	uint32_t octants_mask : 8 = 0u; // 1 0 0 -> 0b1, 0 0 1 -> 0b10, 0 1 0 -> 0b1000
	uint32_t first_octant_node_index : 23 = 0u;
};

struct BuildingOctreeNode {
	uint8_t octants_mask = 0u; // 1 0 0 -> 0b1, 0 0 1 -> 0b10, 0 1 0 -> 0b1000
	std::vector<BuildingOctreeNode> octants;

	[[nodiscard]] bool is_leaf() const {
		return std::empty(octants);
	}
};

class Octree {
public:
	static constexpr auto MAX_DEPTH = 15u;

	Octree(uint32_t depth);

	void add_voxel(glm::uvec3 const& voxel);
	
	std::vector<OctreeNode> build_contiguous_nodes() const;

private:
	uint32_t m_depth;
	BuildingOctreeNode m_root_building_node;
};
