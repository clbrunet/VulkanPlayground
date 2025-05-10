#include "Tree64.hpp"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/config.h>
#include <assimp/scene.h>
#include <glm/ext/scalar_common.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtx/io.hpp>

#include <iostream>
#include <array>
#include <algorithm>
#include <bit>

//CBTODO DRY
template<typename T>
inline constexpr T divide_ceil(T const& a, T const& b) {
	return T((a + b - T(1)) / b);
}

//CBTODO bouger tout ca ?
template<glm::length_t L, typename T, glm::qualifier Q>
inline constexpr T min_component(glm::vec<L, T, Q> const& vec) {
	auto min = vec[0];
	for (auto i = 1u; i < L; ++i) {
		if (vec[i] < min) {
			min = vec[i];
		}
	}
	return min;
}

template<glm::length_t L, typename T, glm::qualifier Q>
inline constexpr T max_component(glm::vec<L, T, Q> const& vec) {
	auto max = vec[0];
	for (auto i = 1; i < L; ++i) {
		if (vec[i] > max) {
			max = vec[i];
		}
	}
	return max;
}

//CBTODO
inline constexpr glm::vec3 vec3(aiVector3D const& vec3)
{
	return glm::vec3{ vec3.x, vec3.y, vec3.z };
}

Tree64 Tree64::voxelize_model(std::filesystem::path const& path, uint32_t const voxel_size) {
	auto const depth = divide_ceil(static_cast<uint8_t>(std::bit_width(voxel_size - 1u)), uint8_t{ 2u });
	std::cerr << "CBTODO : " << (int)depth << '\n';
	if (depth > Tree64::MAX_DEPTH) {
		std::cerr << "CBTODO too large : " << voxel_size << '\n';
		// CBTODO return std::nullopt;
	}
	auto tree64 = Tree64{ depth };

	//CBTODO move to a voxelize_model module
	auto importer = Assimp::Importer{};
	importer.SetPropertyInteger(AI_CONFIG_PP_RVC_FLAGS, aiComponent_NORMALS | aiComponent_TANGENTS_AND_BITANGENTS
		| aiComponent_COLORS | aiComponent_TEXCOORDS | aiComponent_BONEWEIGHTS | aiComponent_ANIMATIONS
		| aiComponent_TEXTURES | aiComponent_LIGHTS | aiComponent_CAMERAS | aiComponent_MATERIALS);
	importer.SetPropertyInteger(AI_CONFIG_PP_SBP_REMOVE, aiPrimitiveType_POINT | aiPrimitiveType_LINE);
	auto const* const scene = importer.ReadFile(path.string().c_str(), aiProcess_JoinIdenticalVertices
		| aiProcess_MakeLeftHanded | aiProcess_Triangulate | aiProcess_RemoveComponent | aiProcess_PreTransformVertices
		| aiProcess_SortByPType | aiProcess_DropNormals | static_cast<unsigned int>(aiProcess_GenBoundingBoxes));
	if (scene == nullptr) {
		std::cerr << "Model loading error : " << importer.GetErrorString() << '\n';
		return tree64;
		// CBTODO return std::nullopt;
	}
	auto const& root_node = *scene->mRootNode;

	auto const for_each_node = [&](std::invocable<aiNode&> auto const& fn) {
		fn(root_node);
		for (auto const* const child : std::span{ root_node.mChildren, root_node.mNumChildren }) {
			fn(*child);
		}
	};

	auto min = glm::vec3{ std::numeric_limits<float>::max()};
	auto max = glm::vec3{ std::numeric_limits<float>::lowest() };
	for_each_node([&](aiNode const& node) {
		for (auto const mesh_index : std::span{ node.mMeshes, node.mNumMeshes }) {
			auto const& mesh = *scene->mMeshes[mesh_index];
			min = glm::min(min, vec3(mesh.mAABB.mMin));
			max = glm::max(max, vec3(mesh.mAABB.mMax));
		}
	});
	auto const model_size = max - min;
	auto const scale = static_cast<float>(voxel_size) / max_component(model_size);
	for_each_node([&](aiNode const& node) {
		for (auto const mesh_index : std::span{ node.mMeshes, node.mNumMeshes }) {
			auto const& mesh = *scene->mMeshes[mesh_index];
			for (auto const& ai_face : std::span{ mesh.mFaces, mesh.mNumFaces }) {
				auto const dda = [&](glm::vec3 const& a, glm::vec3 const& b, std::invocable<glm::ivec3 const&> auto const& fn) {
					//auto const a_to_b = b - a;
					//auto const a_to_b_scaled = a_to_b / max_component(glm::abs(a_to_b));
					//auto a_to_b_count = max_component(glm::abs(glm::ivec3{ b } - glm::ivec3{ a }));
					//for (auto pos = a; a_to_b_count >= 0; --a_to_b_count, pos += a_to_b_scaled) {
					//	fn(pos);
					//}
					//return;

					auto direction = glm::normalize(b - a);
					constexpr auto EPSILON = std::numeric_limits<float>::min();
					// Avoid division by zero
					direction.x = std::copysign(glm::max(glm::abs(direction.x), EPSILON), direction.x);
					direction.y = std::copysign(glm::max(glm::abs(direction.y), EPSILON), direction.y);
					direction.z = std::copysign(glm::max(glm::abs(direction.z), EPSILON), direction.z);
					auto const coords_steps = glm::ivec3{ glm::sign(direction) };
					auto coords = glm::ivec3{ a };

					auto const fract = glm::fract(a);
					auto const straight_distances = glm::mix(fract, 1.f - fract, glm::vec3{ coords_steps + 1 } / 2.f);
					auto distances = glm::vec3{
						glm::length((straight_distances.x / direction.x) * direction),
						glm::length((straight_distances.y / direction.y) * direction),
						glm::length((straight_distances.z / direction.z) * direction)
					};
					auto distances_steps = glm::vec3{
						glm::length((1.f / direction.x) * direction),
						glm::length((1.f / direction.y) * direction),
						glm::length((1.f / direction.z) * direction)
					};

					fn(coords);
					auto const last_coords = glm::ivec3{ b };
					while (coords != last_coords) {
						if (distances.x < distances.y) {
							if (distances.z < distances.x) {
								coords.z += coords_steps.z;
								distances.z += distances_steps.z;
							} else {
								coords.x += coords_steps.x;
								distances.x += distances_steps.x;
							}
						} else {
							if (distances.z < distances.y) {
								coords.z += coords_steps.z;
								distances.z += distances_steps.z;
							} else {
								coords.y += coords_steps.y;
								distances.y += distances_steps.y;
							}
						}
						fn(coords);
					}
				};
				auto const a = scale * (vec3(mesh.mVertices[ai_face.mIndices[0]]) - min);
				auto const b = scale * (vec3(mesh.mVertices[ai_face.mIndices[1]]) - min);
				auto const c = scale * (vec3(mesh.mVertices[ai_face.mIndices[2]]) - min);

				auto added_voxels = std::vector<glm::ivec3>{};
				auto const c_coords = glm::ivec3{ c };
				dda(b, c, [&](glm::ivec3 const& pos) {
					auto index = 0u;
					auto const dest = added_voxels.empty() ? b
						: pos == c_coords ? c
						: glm::vec3{ pos } + 0.5f;
					dda(a, dest, [&](glm::ivec3 const& voxel) {
						if (index >= added_voxels.size()) {
							added_voxels.emplace_back(-1);
						}
						if (voxel != added_voxels[index]) {
							tree64.add_voxel(glm::uvec3{ voxel });
							added_voxels[index] = voxel;
						}
						index += 1u;
					});
				});
			}
		}
	});
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
