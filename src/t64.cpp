#include "t64.hpp"
#include "BinaryFstream.hpp"

namespace vp {

#pragma pack(push, 1)
struct Version {
    uint8_t major;
    uint8_t minor;
    uint16_t patch;
};

struct Header {
    std::array<uint8_t, 3u> signature;
    Version version;
    uint8_t depth;
};

#pragma pack(pop)

}

template<>
struct BinaryFstreamIO<vp::Header> {
    static vp::Header read(BinaryFstream& bf) {
        vp::Header value;
        value.signature = bf.read_array<uint8_t, 3u>();
        value.version.major = bf.read<uint8_t>();
        value.version.minor = bf.read<uint8_t>();
        value.version.patch = bf.read<uint16_t>();
        value.depth = bf.read<uint8_t>();
        return value;
    }

    static void write(BinaryFstream& bf, vp::Header const& value) {
        bf.write_array(value.signature);
        bf.write(value.version.major);
        bf.write(value.version.minor);
        bf.write(value.version.patch);
        bf.write(value.depth);
    }
};

template<>
struct BinaryFstreamIO<vp::Tree64Node> {
    static vp::Tree64Node read(BinaryFstream& bf) {
        vp::Tree64Node value;
        value.up_children_mask = bf.read<uint32_t>();
        value.down_children_mask = bf.read<uint32_t>();
        value.is_leaf_and_first_child_node_index = bf.read<uint32_t>();
        return value;
    }

    static void write(BinaryFstream& bf, vp::Tree64Node const& value) {
        bf.write(value.up_children_mask);
        bf.write(value.down_children_mask);
        bf.write(value.is_leaf_and_first_child_node_index);
    }
};

namespace vp {

constexpr auto FILE_SIGNATURE = std::array<uint8_t, 3u>{{ 'T', '6', '4' }};

std::optional<ContiguousTree64> import_t64(std::filesystem::path const& path) {
    auto bf = BinaryFstream(path, std::ios::ate);
    if (bf.bad()) {
        return std::nullopt;
    }
    auto const file_size = static_cast<uint64_t>(bf.tellg());
    bf.seekg(0);
    auto const header = bf.read<Header>();
    if (header.signature != FILE_SIGNATURE) {
        return std::nullopt;
    }
    auto const node_count = (file_size - sizeof(Header)) / sizeof(Tree64Node);
    auto nodes = bf.read_vector<Tree64Node>(node_count);
    return ContiguousTree64{ .depth = header.depth, .nodes = std::move(nodes) };
}

bool save_t64(std::filesystem::path const& path, ContiguousTree64 const& contiguous_tree64) {
    auto bf = BinaryFstream(path, std::ios::trunc);
    auto const header = Header{
        .signature = FILE_SIGNATURE,
        .version = Version{ .major = 0u, .minor = 1u, .patch = 0u },
        .depth = contiguous_tree64.depth,
    };
    bf.write(header);
    bf.write_range(contiguous_tree64.nodes);
    return static_cast<bool>(bf);
}

}
