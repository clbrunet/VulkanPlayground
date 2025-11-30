#version 450

#include "raytracing_push_contants.glsl"

layout(location = 0) in vec3 v_ray_direction;

layout(location = 0) out vec4 out_color;

struct Ray {
    vec3 position;
    vec3 direction;
    vec3 direction_inverse;
};

uint exp4(const uint exponent) {
    return 1u << (exponent << 1u);
}

Ray compute_ray() {
    Ray ray;
    ray.position = u_camera_position / float(exp4(u_tree64_depth)) + 1.f;
    ray.direction = normalize(v_ray_direction);
    // Avoid division by zero
    const float epsilon = exp2(-23.f);
    if (abs(ray.direction.x) < epsilon) {
        ray.direction.x = ray.direction.x >= 0.f ? epsilon : -epsilon;
    }
    if (abs(ray.direction.y) < epsilon) {
        ray.direction.y = ray.direction.y >= 0.f ? epsilon : -epsilon;
    }
    if (abs(ray.direction.z) < epsilon) {
        ray.direction.z = ray.direction.z >= 0.f ? epsilon : -epsilon;
    }
    ray.direction_inverse = 1.f / ray.direction;
    return ray;
}

float ray_aabb_intersection(const Ray ray, const vec3 aabb_min, const vec3 aabb_max) {

    const vec3 t1 = (aabb_min - ray.position) * ray.direction_inverse;
    const vec3 t2 = (aabb_max - ray.position) * ray.direction_inverse;

    const vec3 t_mins = min(t1, t2);
    const vec3 t_maxs = max(t1, t2);

    const float t_min = max(max(t_mins.x, t_mins.y), t_mins.z);
    const float t_max = min(min(t_maxs.x, t_maxs.y), t_maxs.z);

    if (t_min > t_max || t_max < 0.f) {
        return -1.f;
    }
    return max(t_min, 0.f);
}

uint64_t get_child_bit(const vec3 position, const uint child_scale_bit_offset, const uint mirror_mask) {
    const uvec3 child_coords = (floatBitsToUint(position) >> child_scale_bit_offset) & 3u;
    return 1ul << ((child_coords.x + child_coords.z * 4u + child_coords.y * 16u) ^ mirror_mask);
}

void main() {
    Ray ray = compute_ray();
    const float t = ray_aabb_intersection(ray, vec3(1.f), vec3(2.f));
    if (t == -1.f) {
        out_color = vec4(0.01f, 0.01f, 0.01f, 1.f);
        return;
    }
    // Mirror the ray and coordinates to optimize the traversal, knowing that the ray moves in the negative direction
    const uint mirror_mask = uint(ray.direction.x > 0.f) * 0x03u
        | uint(ray.direction.z > 0.f) * 0x0Cu | uint(ray.direction.y > 0.f) * 0x30u;
    const vec3 ray_origin = mix(ray.position, 3.f - ray.position, greaterThan(ray.direction, vec3(0.f)));
    ray.direction = -abs(ray.direction);
    ray.direction_inverse = 1.f / ray.direction;
    ray.position = clamp(ray_origin + t * ray.direction, vec3(1.f), vec3(1.99999988079071044921875f));

    uint node_index_stack[MAX_TREE64_DEPTH];
    uint node_index = 0u;
    uint child_scale_bit_offset = 21u;
    for (uint i = 0u; i < 2000u; ++i) {
        // Descend to current node
        Tree64Node node = u_tree64_nodes_device_address.b_tree64_nodes[node_index];
        uint64_t child_bit = get_child_bit(ray.position, child_scale_bit_offset, mirror_mask);
        bool has_child_at_child_bit = (children_mask(node) & child_bit) != 0ul;
        while (has_child_at_child_bit && !is_leaf(node)) {
            node_index_stack[child_scale_bit_offset >> 1u] = node_index;

            node_index = first_child_node_index(node) + child_node_offset(node, child_bit);
            node = u_tree64_nodes_device_address.b_tree64_nodes[node_index];

            child_scale_bit_offset -= 2u;
            child_bit = get_child_bit(ray.position, child_scale_bit_offset, mirror_mask);
            has_child_at_child_bit = (children_mask(node) & child_bit) != 0ul;
        }

        const vec3 child_min = uintBitsToFloat(floatBitsToUint(ray.position) & (~0u << child_scale_bit_offset));
        if (has_child_at_child_bit) {
            const float scale = uintBitsToFloat((child_scale_bit_offset + 127u - 23u) << 23u); // exp2(int(child_scale_bit_offset) - 23)
            const vec3 distances = ((child_min + scale) - ray_origin) * ray.direction_inverse;
            const float enter_t = max(max(distances.x, distances.z), distances.y);
            if (enter_t == distances.x) {
                out_color = vec4(1.f, 0.f, 0.f, 1.f);
            } else if (enter_t == distances.z) {
                out_color = vec4(0.f, 0.f, 1.f, 1.f);
            } else {
                out_color = vec4(0.f, 1.f, 0.f, 1.f);
            }
            return;
        }
        // Advance to neighbor
        const vec3 distances = (child_min - ray_origin) * ray.direction_inverse;
        const float exit_t = min(min(distances.x, distances.z), distances.y);
        const vec3 neighbor_max = uintBitsToFloat(mix(
            floatBitsToUint(child_min) | ((1u << child_scale_bit_offset) - 1u),
            floatBitsToUint(child_min) - 1u,
            equal(distances, vec3(exit_t))));
        ray.position = min(ray_origin + exit_t * ray.direction, neighbor_max);

        // Ascend back to higher non-exited node
        const uvec3 binary_diff = floatBitsToUint(ray.position) ^ floatBitsToUint(child_min);
        // & with 0b11111111101010101010101010101010u to check only for odd offsets (quarter of nodes) and for root exit with the leading 1s
        const int binary_diff_offset = findMSB((binary_diff.x | binary_diff.y | binary_diff.z) & 0xFFAAAAAAu);
        if (binary_diff_offset > child_scale_bit_offset) {
            if (binary_diff_offset > 21u) {
                break; // out of root
            }
            child_scale_bit_offset = binary_diff_offset;
            node_index = node_index_stack[child_scale_bit_offset >> 1u];
        }
    }
    out_color = vec4(0.f, 0.f, 0.f, 1.f);
}
