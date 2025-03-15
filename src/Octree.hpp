#pragma once

#include <glm/glm.hpp>

#include <vector>
#include <span>

struct OctreeNode {
	uint32_t is_leaf : 1 = 0u;
	uint32_t octants_mask : 8 = 0u; // 1 0 0 -> 0b1, 0 0 1 -> 0b10, 0 1 0 -> 0b1000
	uint32_t first_octant_node_index : 23 = 0u;
};

class Octree {
public:
	static constexpr auto MAX_DEPTH = 15u;

	Octree(uint32_t depth);

	void add_voxel(glm::uvec3 const& voxel);
	void shrink();
	
	std::span<const OctreeNode> nodes() const;

private:
	uint32_t m_depth;
	std::vector<OctreeNode> m_nodes = std::vector<OctreeNode>{ 1u };
};
