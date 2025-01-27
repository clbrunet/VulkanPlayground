#include "vox.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/io.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <map>
#include <vector>
#include <algorithm>
#include <charconv>

using Dict = std::map<std::string, std::string>;

template<typename Type>
static Type read(std::istream& istream) {
	Type value;
	istream.read(reinterpret_cast<char*>(&value), sizeof(value));
	return value;
}

template<>
std::string read(std::istream& istream) {
	auto const length = read<int32_t>(istream);
	auto string = std::string(static_cast<std::string::size_type>(length), '\0');
	istream.read(std::data(string), length);
	return string;
}

template<>
Dict read(std::istream& istream) {
	auto const length = read<int32_t>(istream);
	auto dict = Dict{};
	for (auto i = 0; i < length; ++i) {
		auto key = read<std::string>(istream);
		auto value = read<std::string>(istream);
		dict.emplace(std::move(key), std::move(value));
	}
	return dict;
}

// CBTODO rename vox_full_size
bool import_vox(std::filesystem::path const& path, std::function<void(glm::ivec3 const&)> const vox_full_size, std::function<void(glm::ivec3 const&)> const import_voxel) {
	auto ifstream = std::ifstream{ path, std::ios::binary };
	if (!ifstream) {
		return false;
	}
	// format : https://github.com/ephtracy/voxel-model/tree/master
	constexpr auto START_IGNORED_BYTE_COUNT = sizeof(std::array<char, 4u>) // "VOX "
		+ sizeof(int32_t) // version
		+ sizeof(std::array<char, 4u>) // MAIN chunk id
		+ sizeof(int32_t) // chunk content size (0 for MAIN)
		+ sizeof(int32_t); // children chunks size
	auto for_each_chunks = [&ifstream](auto function) {//CBTODO changer auto (std::func ?)
		ifstream.seekg(START_IGNORED_BYTE_COUNT);
		while (ifstream.good() && ifstream.peek() != decltype(ifstream)::traits_type::eof()) {
			auto const chunk_id_letters = read<std::array<char, 4u>>(ifstream);
			auto const chunk_id = std::string_view{ std::data(chunk_id_letters), std::size(chunk_id_letters) };
			auto const chunk_content_size = read<int32_t>(ifstream);
			auto const children_chunks_size = read<int32_t>(ifstream);
			if (!function(chunk_id)) {
				ifstream.ignore(chunk_content_size + children_chunks_size);
			}
		}
	};

	struct Node {
		int32_t m_id;
		std::vector<Node> m_children;
		glm::mat4 m_local_transform = glm::identity<glm::mat4>(); // from nTRN//CBTODO tester remplacer glm::mat4 par glm::imat4 
		std::vector<int32_t> m_model_ids; // from nSHP

		Node(int32_t const id) : m_id{ id } {
		}

		static Node* search(int32_t const id, Node& search_root) {
			if (search_root.m_id == id) {
				return &search_root;
			}
			for (auto& child : search_root.m_children) {
				auto node = search(id, child);
				if (node != nullptr) {
					return node;
				}
			}
			return nullptr;
		}
	};

	auto model_sizes = std::vector<glm::ivec3>{};
	auto root_node = Node{ 0 };

	for_each_chunks([&](std::string_view const chunk_id) {
		// CBTODO print seulement si un truc inconnu
		if (chunk_id == "SIZE") {
			auto const size_x = read<int32_t>(ifstream);
			auto const size_y = read<int32_t>(ifstream);
			auto const size_z = read<int32_t>(ifstream);
			model_sizes.emplace_back(glm::ivec3{ size_x, size_z, size_y });
		} else if (chunk_id == "nTRN") {
			auto should_print = true;//CBTODO
			auto const node_id = read<int32_t>(ifstream);
			auto const node = Node::search(node_id, root_node);
			assert(node != nullptr);

			auto const node_attributes = read<Dict>(ifstream);
			for (auto const& elem : node_attributes) {
				if (elem.first != "_name") {
					should_print = true;
				}
			}

			auto const child_node_id = read<int32_t>(ifstream);
			node->m_children.emplace_back(child_node_id);

			ifstream.ignore(sizeof(int32_t)); // reserved id (must be -1)
			auto const layer_id = read<int32_t>(ifstream);
			auto const frame_count = read<int32_t>(ifstream);
			auto print_frames_attributes = std::vector<Dict>{};//CBTODO
			for (auto i = 0; i < frame_count; ++i) {
				auto const frame_attributes = read<Dict>(ifstream);
				print_frames_attributes.push_back(frame_attributes);
				// CBTODO
				// (_r : int8)    ROTATION, see(c)
				// (_t : int32x3) translation
				// (_f : int32)   frame index, start from 0
				// CBTODO gerer _r
				auto const translation_it = frame_attributes.find("_t");
				if (translation_it != std::end(frame_attributes)) {
					auto istringstream = std::istringstream{ translation_it->second };
					istringstream >> node->m_local_transform[3].x >> node->m_local_transform[3].z >> node->m_local_transform[3].y;
				}
				auto const rotation_it = frame_attributes.find("_r");
				if (rotation_it != std::end(frame_attributes)) {
					auto bits = int8_t{};
					if (std::from_chars(std::data(rotation_it->second), std::data(rotation_it->second) + std::size(rotation_it->second), bits).ec != std::errc{}) {
						assert(false);
					}
					node->m_local_transform[0] = glm::vec4{ 0 };
					node->m_local_transform[1] = glm::vec4{ 0 };
					node->m_local_transform[2] = glm::vec4{ 0 };
					auto x_index = bits & 0b0000011;
					auto y_index = (bits & 0b0001100) >> 2;
					auto z_index = 3 - x_index - y_index;
					auto x_sign = (1 - ((bits & 0b0010000) >> 4)) * 2 - 1;
					auto y_sign = (1 - ((bits & 0b0100000) >> 5)) * 2 - 1;
					auto z_sign = (1 - ((bits & 0b1000000) >> 6)) * 2 - 1;
					auto vox_rot = glm::mat3{ 0 };
					vox_rot[x_index][0] = x_sign;
					vox_rot[y_index][1] = y_sign;
					vox_rot[z_index][2] = z_sign;

					glm::mat3 M = glm::mat3(
						1, 0, 0,
						0, 0, -1,
						0, 1, 0
					);

					// Inverse of the change of basis matrix
					glm::mat3 M_inv = glm::inverse(M);

					// Apply the change of basis
					glm::mat3 R_new = M * vox_rot * M_inv;

					// CBTODO surement reorga y et z
					node->m_local_transform[x_index][0] = static_cast<float>(x_sign);
					node->m_local_transform[z_index][1] = static_cast<float>(-z_sign);
					node->m_local_transform[y_index][2] = static_cast<float>(y_sign);

					node->m_local_transform[0] = glm::vec4{ R_new[0], 0.f };
					node->m_local_transform[1] = glm::vec4{ R_new[1], 0.f };
					node->m_local_transform[2] = glm::vec4{ R_new[2], 0.f };
				}
				for (auto const& elem : frame_attributes) {
					if (elem.first != "_t") {
						should_print = true;
					}
				}
			}

			if (should_print) {
				std::cout << "----- " << chunk_id << std::endl;
				std::cout << "node_id : " << node_id << std::endl;

				std::cout << "node_attributes :" << std::endl;
				for (auto const& elem : node_attributes) {
					std::cout << "  " << elem.first << " : " << elem.second << std::endl;
				}

				std::cout << "child_node_id : " << child_node_id << std::endl;
				std::cout << "layer_id : " << layer_id << std::endl;
				std::cout << "frame_count : " << frame_count << std::endl;
				for (auto i = 0; i < frame_count; ++i) {
					std::cout << "  frame_attributes :" << std::endl;
					for (auto const& elem : print_frames_attributes[i]) {
						std::cout << "    " << elem.first << " : " << elem.second << std::endl;
					}
					std::cout << node->m_local_transform << std::endl;
				}
			}
		} else if (chunk_id == "nGRP") {
			auto should_print = true;//CBTODO
			auto const node_id = read<int32_t>(ifstream);
			auto const node = Node::search(node_id, root_node);
			assert(node != nullptr);

			auto const node_attributes = read<Dict>(ifstream);
			for (auto const& elem : node_attributes) {
				should_print = true;
			}

			auto const child_node_count = read<int32_t>(ifstream);
			node->m_children.reserve(static_cast<size_t>(child_node_count));
			for (auto i = 0; i < child_node_count; ++i) {
				auto const child_node_id = read<int32_t>(ifstream);
				node->m_children.emplace_back(child_node_id);
			}
			if (should_print) {
				std::cout << "----- " << chunk_id << std::endl;
				std::cout << "node_id : " << node_id << std::endl;

				std::cout << "node_attributes :" << std::endl;
				for (auto const& elem : node_attributes) {
					std::cout << "  " << elem.first << " : " << elem.second << std::endl;
				}

				std::cout << "child_node_count : " << child_node_count << std::endl;
				for (auto i = std::size(node->m_children) - child_node_count; i < std::size(node->m_children); ++i) {
					std::cout << "  child_node_id : " << node->m_children[i].m_id << std::endl;
				}
			}
		} else if (chunk_id == "nSHP") {
			auto should_print = true;//CBTODO
			auto const node_id = read<int32_t>(ifstream);
			auto const node = Node::search(node_id, root_node);
			assert(node != nullptr);

			auto const node_attributes = read<Dict>(ifstream);
			for (auto const& elem : node_attributes) {
				should_print = true;
			}

			auto const model_count = read<int32_t>(ifstream);
			node->m_model_ids.reserve(static_cast<size_t>(model_count));
			auto print_models_attributes = std::vector<Dict>{};
			for (auto i = 0; i < model_count; ++i) {
				auto const model_id = read<int32_t>(ifstream);
				node->m_model_ids.emplace_back(model_id);

				auto const model_attributes = read<Dict>(ifstream);
				print_models_attributes.push_back(model_attributes);
				for (auto const& elem : model_attributes) {
					should_print = true;
				}
			}
			if (should_print) {
				std::cout << "----- " << chunk_id << std::endl;
				std::cout << "node_id : " << node_id << std::endl;

				std::cout << "node_attributes :" << std::endl;
				for (auto const& elem : node_attributes) {
					std::cout << "  " << elem.first << " : " << elem.second << std::endl;
				}

				std::cout << "model_count : " << model_count << std::endl;
				auto j = 0u;
				for (auto i = std::size(node->m_model_ids) - model_count; i < std::size(node->m_model_ids); ++i) {
					std::cout << "  model_id : " << node->m_model_ids[i] << std::endl;
					std::cout << "  model_attributes :" << std::endl;
					for (auto const& elem : print_models_attributes[j]) {
						std::cout << "    " << elem.first << " : " << elem.second << std::endl;
					}
					++j;
				}
			}
		} else {
			return false;
		}
		return true;
	});

	auto model_transforms = std::vector<glm::mat4>(std::size(model_sizes));
	auto min = glm::ivec3{ std::numeric_limits<int32_t>::max() };
	auto max = glm::ivec3{ std::numeric_limits<int32_t>::min() };
	auto const minmax = [&](auto const& self, Node const& node, glm::mat4 const& parent_transform = glm::identity<glm::mat4>()) -> void {
		auto const global_transform = parent_transform * node.m_local_transform;
		std::cout << "current node : " << node.m_id << ", global transform : " << global_transform << std::endl;
		std::cout << "current node : " << node.m_id << ", local transform : " << node.m_local_transform << std::endl;
		for (auto const model_id : node.m_model_ids) {
			auto const& model_size = model_sizes[static_cast<size_t>(model_id)];
			std::cout << "model_size " << model_id << " :" << model_size << std::endl;
			auto model_transform = glm::translate(glm::mat4{ 1 }, glm::vec3{ model_size / -2 }) * global_transform;
			std::cout << "minmax model transform (au niveau du voxel 0 0 0) " << model_id << " :" << model_transform << std::endl;
			min = glm::min(min, glm::ivec3{ model_transform[3] });
			max = glm::max(max, glm::ivec3{ model_transform[3] } + model_size);

			model_transforms[static_cast<size_t>(model_id)] = model_transform;//CBTODO rename minmax
		}
		for (auto const& child : node.m_children) {
			self(self, child, global_transform);
		}
	};
	minmax(minmax, root_node);

	auto const full_size = max - min;
	vox_full_size(full_size);
	std::cout << "                              FULL SIZE " << full_size << std::endl;

	auto model_id = size_t{ 0 };
	for_each_chunks([&](std::string_view const chunk_id) {
		if (chunk_id == "XYZI") {
			auto const& model_size = model_sizes[model_id];
			auto const& model_transform = model_transforms[model_id];
			auto const position = glm::ivec3{ model_transform[3] } - min;
			auto const voxel_count = read<int32_t>(ifstream);
			std::cout << "XYZI position " << model_id << " :" << position << std::endl;
			for (auto i = 0; i < voxel_count; ++i) {
				auto const x = read<uint8_t>(ifstream); //CBTODO mettre dans le bonne ordre pour les read pour reorga apres, deplacer le commentaire a cette reorga
				auto const z = read<uint8_t>(ifstream); // vox uses a x right, z up and y forward coordinates system
				auto const y = read<uint8_t>(ifstream);
				//std::cout << std::format("x, y, z : {}, {}, {}", x, y, z) << std::endl;
				ifstream.ignore(sizeof(uint8_t)); // palette index
				auto voxel = glm::ivec3{ x, z, y };
				auto x_neg = false;
				auto y_neg = false;
				auto z_neg = false;
				if (model_transform[0][0] == -1.f || model_transform[1][0] == -1.f || model_transform[2][0] == -1.f) {
					x_neg = true;
				}
				if (model_transform[0][0] != 0.f) {
					voxel.x = x_neg ? model_size.x - 1 - x : x;
				} else if (model_transform[1][0] != 0.f) {
					voxel.x = x_neg ? model_size.x - 1 - y : y;
				} else {
					voxel.x = x_neg ? model_size.x - 1 - z : z;
				}
				if (model_transform[0][1] == -1.f || model_transform[1][1] == -1.f || model_transform[2][1] == -1.f) {
					y_neg = true;
				}
				if (model_transform[0][1] != 0.f) {
					voxel.y = y_neg ? model_size.y - 1 - x : x;
				} else if (model_transform[1][1] != 0.f) {
					voxel.y = y_neg ? model_size.y - 1 - y : y;
				} else {
					voxel.y = y_neg ? model_size.y - 1 - z : z;
				}
				if (model_transform[0][2] == -1.f || model_transform[1][2] == -1.f || model_transform[2][2] == -1.f) {
					z_neg = true;
				}
				if (model_transform[0][2] != 0.f) {
					voxel.z = z_neg ? model_size.z - 1 - x : x;
				} else if (model_transform[1][2] != 0.f) {
					voxel.z = z_neg ? model_size.z - 1 - y : y;
				} else {
					voxel.z = z_neg ? model_size.z - 1 - z : z;
				}
				import_voxel(position + voxel);
			}
			model_id += 1u;
		} else {
			return false;
		}
		return true;
	});
	return true;
}
