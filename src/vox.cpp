#include "vox.hpp"

#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/matrix_integer.hpp>
#include <glm/ext/matrix_integer.hpp>
#include <glm/gtx/io.hpp>

#include <iostream>
#include <fstream>
#include <string>
#include <array>
#include <map>
#include <vector>
#include <algorithm>
#include <charconv>
#include <unordered_map>
#include <ranges>
#include <concepts>

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

static glm::imat3 read_rotation(std::string_view const vox_rotation_str) {
    auto bits = int8_t{};
    if (std::from_chars(std::data(vox_rotation_str), std::data(vox_rotation_str) + std::size(vox_rotation_str), bits).ec != std::errc{}) {
        assert(false);
    }
    auto const x_index = bits & 0b0000011;
    auto const y_index = (bits & 0b0001100) >> 2;
    auto const z_index = 3 - x_index - y_index;
    auto const x_sign = (1 - ((bits & 0b0010000) >> 4)) * 2 - 1;
    auto const y_sign = (1 - ((bits & 0b0100000) >> 5)) * 2 - 1;
    auto const z_sign = (1 - ((bits & 0b1000000) >> 6)) * 2 - 1;

    auto vox_rotation = glm::imat3{ 0 };
    vox_rotation[x_index][0] = x_sign;
    vox_rotation[y_index][1] = y_sign;
    vox_rotation[z_index][2] = z_sign;
    return vox_rotation;
}

struct Node {
    int32_t m_id;
    std::vector<Node> m_children;
    glm::imat4 m_local_transform = glm::identity<glm::imat4>(); // from nTRN
    std::vector<int32_t> m_model_ids; // from nSHP

    Node(int32_t const id) : m_id{ id } {
    }

    static Node* search(int32_t const id, Node& search_root) {
        if (search_root.m_id == id) {
            return &search_root;
        }
        for (auto& child : search_root.m_children) {
            auto const node = search(id, child);
            if (node != nullptr) {
                return node;
            }
        }
        return nullptr;
    }
};

bool import_vox(std::filesystem::path const& path,
    std::function<bool(glm::uvec3 const&)> const& vox_full_size_importer,
    std::function<void(glm::uvec3 const&)> const& voxel_importer) {
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
    auto const for_each_chunks = [&ifstream](std::predicate<std::string_view> auto const chunk_consumer) {
        ifstream.seekg(START_IGNORED_BYTE_COUNT);
        while (ifstream.good() && ifstream.peek() != decltype(ifstream)::traits_type::eof()) {
            auto const chunk_id_letters = read<std::array<char, 4u>>(ifstream);
            auto const chunk_id = std::string_view{ std::data(chunk_id_letters), std::size(chunk_id_letters) };
            auto const chunk_content_size = read<int32_t>(ifstream);
            auto const children_chunks_size = read<int32_t>(ifstream);
            if (!chunk_consumer(chunk_id)) {
                ifstream.ignore(chunk_content_size + children_chunks_size);
            }
        }
    };

    auto model_sizes = std::vector<glm::ivec3>{};
    auto root_node = Node{ 0 };
    for_each_chunks([&](std::string_view const chunk_id) {
        if (chunk_id == "SIZE") {
            auto const size_x = read<int32_t>(ifstream);
            auto const size_y = read<int32_t>(ifstream);
            auto const size_z = read<int32_t>(ifstream);
            model_sizes.emplace_back(glm::ivec3{ size_x, size_z, size_y });
        } else if (chunk_id == "nTRN") {
            auto const node_id = read<int32_t>(ifstream);
            auto const node = Node::search(node_id, root_node);
            assert(node != nullptr);
            auto const node_attributes = read<Dict>(ifstream);
            auto const child_node_id = read<int32_t>(ifstream);
            node->m_children.emplace_back(child_node_id);

            ifstream.ignore(sizeof(int32_t)); // reserved id (must be -1)
            ifstream.ignore(sizeof(int32_t)); // layer id
            auto const frame_count = read<int32_t>(ifstream);
            for (auto i = 0; i < frame_count; ++i) {
                auto const frame_attributes = read<Dict>(ifstream);
                auto const translation_it = frame_attributes.find("_t");
                if (translation_it != std::end(frame_attributes)) {
                    // vox uses a x right, z up and y forward coordinates system
                    std::istringstream{ translation_it->second } >> node->m_local_transform[3].x >> node->m_local_transform[3].z >> node->m_local_transform[3].y;
                }
                auto const rotation_it = frame_attributes.find("_r");
                if (rotation_it != std::end(frame_attributes)) {
                    auto const vox_rotation = read_rotation(rotation_it->second);
                    // vox uses a x right, z up and y forward coordinates system
                    constexpr auto vox_to_z_forward_y_up_matrix = glm::imat3{
                        1, 0, 0,
                        0, 0, 1,
                        0, 1, 0
                    };
                    auto const rotation = glm::transpose(vox_to_z_forward_y_up_matrix) * vox_rotation * vox_to_z_forward_y_up_matrix;
                    node->m_local_transform[0] = glm::ivec4{ rotation[0], 0 };
                    node->m_local_transform[1] = glm::ivec4{ rotation[1], 0 };
                    node->m_local_transform[2] = glm::ivec4{ rotation[2], 0 };
                }
            }
        } else if (chunk_id == "nGRP") {
            auto const node_id = read<int32_t>(ifstream);
            auto const node = Node::search(node_id, root_node);
            assert(node != nullptr);
            auto const node_attributes = read<Dict>(ifstream);
            auto const child_node_count = read<int32_t>(ifstream);
            node->m_children.reserve(static_cast<size_t>(child_node_count));
            for (auto i = 0; i < child_node_count; ++i) {
                auto const child_node_id = read<int32_t>(ifstream);
                node->m_children.emplace_back(child_node_id);
            }
        } else if (chunk_id == "nSHP") {
            auto const node_id = read<int32_t>(ifstream);
            auto const node = Node::search(node_id, root_node);
            assert(node != nullptr);
            auto const node_attributes = read<Dict>(ifstream);
            auto const model_count = read<int32_t>(ifstream);
            node->m_model_ids.reserve(static_cast<size_t>(model_count));
            for (auto i = 0; i < model_count; ++i) {
                auto const model_id = read<int32_t>(ifstream);
                node->m_model_ids.emplace_back(model_id);
                auto const model_attributes = read<Dict>(ifstream);
            }
        } else {
            return false;
        }
        return true;
    });

    auto model_transforms = std::unordered_multimap<int32_t, glm::imat4>{};
    model_transforms.reserve(std::size(model_sizes));
    auto voxel_begin = glm::ivec3{ std::numeric_limits<int32_t>::max() };
    auto voxel_end = glm::ivec3{ std::numeric_limits<int32_t>::lowest() };

    auto const parse_nodes = [&](auto const& self, Node const& node, glm::imat4 const& parent_transform = glm::identity<glm::imat4>()) -> void {
        auto const global_transform = parent_transform * node.m_local_transform;
        for (auto const model_id : node.m_model_ids) {
            auto const& model_size = model_sizes[static_cast<size_t>(model_id)];
            auto const model_transform = global_transform * glm::translate(glm::imat4{ 1 },  model_size / -2);
            auto const model_transform_voxel_end = global_transform * glm::translate(glm::imat4{ 1 }, model_size / 2);

            voxel_begin = glm::min(voxel_begin, glm::ivec3{ model_transform[3] }, glm::ivec3{ model_transform_voxel_end[3] });
            voxel_end = glm::max(voxel_end, glm::ivec3{ model_transform[3] }, glm::ivec3{ model_transform_voxel_end[3] });

            model_transforms.emplace(model_id, model_transform);
        }
        for (auto const& child : node.m_children) {
            self(self, child, global_transform);
        }
    };
    parse_nodes(parse_nodes, root_node);

    if (!vox_full_size_importer(voxel_end - voxel_begin)) {
        return false;
    }

    for (auto& [_, model_transform] : model_transforms) {
        model_transform[3] -= glm::ivec4{ voxel_begin, 0 };
        // substract 1 when a coordinate start from the past-the-end
        model_transform[3][0] -= (model_transform[0][0] + model_transform[1][0] + model_transform[2][0] - 1) / -2;
        model_transform[3][1] -= (model_transform[0][1] + model_transform[1][1] + model_transform[2][1] - 1) / -2;
        model_transform[3][2] -= (model_transform[0][2] + model_transform[1][2] + model_transform[2][2] - 1) / -2;
    }

    auto model_id = int32_t{ 0 };
    for_each_chunks([&](std::string_view const chunk_id) {
        if (chunk_id != "XYZI") {
            return false;
        }
        auto const model_transforms_range = [&]() {
            auto const [begin, end] = model_transforms.equal_range(model_id);
            return std::ranges::subrange{ begin, end };
        }();
        auto const voxel_count = read<int32_t>(ifstream);
        for (auto i = 0; i < voxel_count; ++i) {
            auto const x = read<uint8_t>(ifstream);
            auto const y = read<uint8_t>(ifstream);
            auto const z = read<uint8_t>(ifstream);
            ifstream.ignore(sizeof(uint8_t)); // palette index

            for (auto const& [_, model_transform] : model_transforms_range) {
                auto const voxel = glm::ivec3{ model_transform[3] } + glm::ivec3{
                    model_transform[0][0] * x + model_transform[1][0] * z + model_transform[2][0] * y,
                    model_transform[0][1] * x + model_transform[1][1] * z + model_transform[2][1] * y,
                    model_transform[0][2] * x + model_transform[1][2] * z + model_transform[2][2] * y,
                };
                voxel_importer(glm::uvec3{ voxel });
            }
        }
        model_id += 1u;
        return true;
    });
    return true;
}
