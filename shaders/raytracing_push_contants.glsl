#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference : require
#extension GL_EXT_scalar_block_layout : require

struct Tree64Node {
    uint up_children_mask;
    uint down_children_mask;
    uint is_leaf_and_first_child_node_index;
};

uint64_t children_mask(Tree64Node node) {
    return (uint64_t(node.up_children_mask) << 32ul) | node.down_children_mask;
}

uint child_node_offset(Tree64Node node, uint child_index) {
    const uint low_mask_offset = child_index == 0u ? 0u : bitCount(node.down_children_mask << (32u - min(child_index, 32u)));
    const uint high_mask_offset = child_index <= 32u ? 0u : bitCount(node.up_children_mask << (32u - (child_index - 32u)));
    return low_mask_offset + high_mask_offset;
}

bool is_leaf(Tree64Node node) {
    return (node.is_leaf_and_first_child_node_index & 1u) == 1u;
}
uint first_child_node_index(Tree64Node node) {
    return node.is_leaf_and_first_child_node_index >> 1u;
}

const uint MAX_TREE64_DEPTH = 8u;

layout(std430, buffer_reference) readonly buffer Tree64NodesBuffer {
    Tree64Node b_tree64_nodes[];
};

layout(scalar, push_constant) uniform PushConstants {
    vec3 u_camera_position;
    float u_aspect_ratio;
    mat3 u_camera_rotation;
    uint u_tree64_depth;
    Tree64NodesBuffer u_tree64_nodes_device_address;
};
