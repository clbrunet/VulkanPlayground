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

uint child_node_offset(const Tree64Node node, const uint64_t child_bit) {
    const uint64_t mask = child_bit - 1ul;
    const uint low_mask_offset = bitCount(uint(mask) & node.down_children_mask);
    const uint high_mask_offset = bitCount(uint(mask >> 32ul) & node.up_children_mask);
    return low_mask_offset + high_mask_offset;
}

bool is_leaf(Tree64Node node) {
    return (node.is_leaf_and_first_child_node_index & 1u) == 1u;
}

uint first_child_node_index(Tree64Node node) {
    return node.is_leaf_and_first_child_node_index >> 1u;
}

const uint MAX_TREE64_DEPTH = 11u;

layout(scalar, buffer_reference) readonly buffer Tree64NodesBuffer {
    Tree64Node tree64_nodes[];
};

layout(scalar, buffer_reference) readonly buffer HosekWilkieSkyRenderingParametersBuffer {
    vec3 config[9];
    vec3 luminance;
};

layout(scalar, push_constant) uniform PushConstants {
    float u_aspect_ratio;
    vec3 u_camera_position;
    mat3 u_camera_rotation;
    vec3 u_to_sun_direction;
    HosekWilkieSkyRenderingParametersBuffer u_hosek_wilkie_sky_rendering_parameters_device_address;
    Tree64NodesBuffer u_tree64_nodes_device_address;
    uint u_tree64_depth;
};
