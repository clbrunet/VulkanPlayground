#pragma once
//CBTODO renaming across all project (code and filenames) octree to 64 tree
//CBTODO renaming across all project octant to orthant ?

#include <glm/glm.hpp>

#include <vector>
#include <span>

#pragma pack(push, 1)//CBTODO linux ?
struct OctreeNode {//CBTODO renaming a lot
	uint64_t octants_mask = 0u; // 1 0 0 -> 0b1, 0 0 1 -> 0b10, 0 1 0 -> 0b1000 CBTODO
	uint32_t bits = 1u;

	bool is_leaf() const {
		return (bits & 1u) == 1u;
	}

	void set_is_leaf(bool is_leaf) {
		bits = (bits & ~1u) | static_cast<uint32_t>(is_leaf);
	}

	uint32_t first_octant_node_index() const {
		return bits >> 1u;
	}

	void set_first_octant_node_index(uint32_t first_octant_node_index) {
		bits = (bits & 1u) | (first_octant_node_index << 1u);
	}
};
#pragma pack(pop)
static_assert(sizeof(OctreeNode) == 12);

struct BuildingOctreeNode {
	uint64_t octants_mask = 0u; // 1 0 0 -> 0b1, 0 0 1 -> 0b10, 0 1 0 -> 0b1000 CBTODO
	std::vector<BuildingOctreeNode> octants;

	[[nodiscard]] bool is_leaf() const {
		return std::empty(octants);
	}
};

class Octree {
public:
	static constexpr auto MAX_DEPTH = uint8_t{ 8u };//CBTODO

	Octree(uint8_t depth);

	void add_voxel(glm::uvec3 const& voxel);
	
	std::vector<OctreeNode> build_contiguous_nodes() const;

private:
	uint8_t m_depth;
	BuildingOctreeNode m_root_building_node;
};
