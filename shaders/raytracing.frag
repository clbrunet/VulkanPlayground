#version 450

#include "raytracing_push_contants.glsl"

layout(location = 0) in vec3 v_ray_direction;

layout(location = 0) out vec4 out_color;

struct StackElem {
    uint64_t children_intesection_mask;
    uint checked_child_count;
    uint node_index;
    vec3 node_min;
};

struct Ray {
    vec3 position;
    vec3 direction;
    vec3 direction_inverse;
};

uint exp4(uint exponent) {
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

uint compute_first_child_index(const vec3 ray_direction) {
    uint first_child_index = 0u;
    if (ray_direction.x < 0.f) {
        first_child_index |= 3u;
    }
    if (ray_direction.y < 0.f) {
        first_child_index |= 48u;
    }
    if (ray_direction.z < 0.f) {
        first_child_index |= 12u;
    }
    return first_child_index;
}

const uint64_t FAR_LEFT_CHILDREN_MASK = 0x1111111111111111ul; //     0b00010001'00010001'00010001'00010001'00010001'00010001'00010001'00010001ul
const uint64_t CENTER_LEFT_CHILDREN_MASK = 0x2222222222222222ul; //  0b00100010'00100010'00100010'00100010'00100010'00100010'00100010'00100010ul
const uint64_t CENTER_RIGHT_CHILDREN_MASK = 0x4444444444444444ul; // 0b01000100'01000100'01000100'01000100'01000100'01000100'01000100'01000100ul
const uint64_t FAR_RIGHT_CHILDREN_MASK = 0x8888888888888888ul;//     0b10001000'10001000'10001000'10001000'10001000'10001000'10001000'10001000ul
const uint64_t FAR_FRONT_CHILDREN_MASK = 0x000F000F000F000Ful; //    0b00000000'00001111'00000000'00001111'00000000'00001111'00000000'00001111ul
const uint64_t CENTER_FRONT_CHILDREN_MASK = 0x00F000F000F000F0ul; // 0b00000000'11110000'00000000'11110000'00000000'11110000'00000000'11110000ul
const uint64_t CENTER_BACK_CHILDREN_MASK = 0x0F000F000F000F00ul; //  0b00001111'00000000'00001111'00000000'00001111'00000000'00001111'00000000ul
const uint64_t FAR_BACK_CHILDREN_MASK = 0xF000F000F000F000ul; //     0b11110000'00000000'11110000'00000000'11110000'00000000'11110000'00000000ul
const uint64_t FAR_DOWN_CHILDREN_MASK = 0x000000000000FFFFul; //     0b00000000'00000000'00000000'00000000'00000000'00000000'11111111'11111111ul
const uint64_t CENTER_DOWN_CHILDREN_MASK = 0x00000000FFFF0000ul; //  0b00000000'00000000'00000000'00000000'11111111'11111111'00000000'00000000ul
const uint64_t CENTER_UP_CHILDREN_MASK = 0x0000FFFF00000000ul; //    0b00000000'00000000'11111111'11111111'00000000'00000000'00000000'00000000ul
const uint64_t FAR_UP_CHILDREN_MASK = 0xFFFF000000000000ul; //       0b11111111'11111111'00000000'00000000'00000000'00000000'00000000'00000000ul

uint64_t compute_separator_intesection_mask(const Ray ray, const vec3 separator_ts,
    const float t_min, const float t_max, const vec3 node_q1, const vec3 node_center, const vec3 node_q3,
    const uint64_t x_separator_mask, const uint64_t y_separator_mask, const uint64_t z_separator_mask) {
    uint64_t intersection_mask = 0ul;

    const vec3 x_separator_point = ray.position + separator_ts.x * ray.direction;
    const uint64_t x_separator_point_y_mask = x_separator_point.y < node_q1.y ? FAR_DOWN_CHILDREN_MASK
        : x_separator_point.y < node_center.y ? CENTER_DOWN_CHILDREN_MASK
        : x_separator_point.y < node_q3.y ? CENTER_UP_CHILDREN_MASK : FAR_UP_CHILDREN_MASK;
    const uint64_t x_separator_point_z_mask = x_separator_point.z < node_q1.z ? FAR_FRONT_CHILDREN_MASK
        : x_separator_point.z < node_center.z ? CENTER_FRONT_CHILDREN_MASK
        : x_separator_point.z < node_q3.z ? CENTER_BACK_CHILDREN_MASK : FAR_BACK_CHILDREN_MASK;
    intersection_mask |= (separator_ts.x < t_min || t_max < separator_ts.x) ? 0ul
        : x_separator_mask & x_separator_point_y_mask & x_separator_point_z_mask;

    const vec3 y_separator_point = ray.position + separator_ts.y * ray.direction;
    const uint64_t y_separator_point_x_mask = y_separator_point.x < node_q1.x ? FAR_LEFT_CHILDREN_MASK
        : y_separator_point.x < node_center.x ? CENTER_LEFT_CHILDREN_MASK
        : y_separator_point.x < node_q3.x ? CENTER_RIGHT_CHILDREN_MASK : FAR_RIGHT_CHILDREN_MASK;
    const uint64_t y_separator_point_z_mask = y_separator_point.z < node_q1.z ? FAR_FRONT_CHILDREN_MASK
        : y_separator_point.z < node_center.z ? CENTER_FRONT_CHILDREN_MASK
        : y_separator_point.z < node_q3.z ? CENTER_BACK_CHILDREN_MASK : FAR_BACK_CHILDREN_MASK;
    intersection_mask |= (separator_ts.y < t_min || t_max < separator_ts.y) ? 0ul
        : y_separator_mask & y_separator_point_x_mask & y_separator_point_z_mask;

    const vec3 z_separator_point = ray.position + separator_ts.z * ray.direction;
    const uint64_t z_separator_point_x_mask = z_separator_point.x < node_q1.x ? FAR_LEFT_CHILDREN_MASK
        : z_separator_point.x < node_center.x ? CENTER_LEFT_CHILDREN_MASK
        : z_separator_point.x < node_q3.x ? CENTER_RIGHT_CHILDREN_MASK : FAR_RIGHT_CHILDREN_MASK;
    const uint64_t z_separator_point_y_mask = z_separator_point.y < node_q1.y ? FAR_DOWN_CHILDREN_MASK
        : z_separator_point.y < node_center.y ? CENTER_DOWN_CHILDREN_MASK
        : z_separator_point.y < node_q3.y ? CENTER_UP_CHILDREN_MASK : FAR_UP_CHILDREN_MASK;
    intersection_mask |= (separator_ts.z < t_min || t_max < separator_ts.z) ? 0ul
        : z_separator_mask & z_separator_point_x_mask & z_separator_point_y_mask;

    return intersection_mask;
}

uint64_t compute_children_intesection_mask(const Ray ray, const vec3 node_min, const vec3 node_max) {
    const float half_extent = (node_max.x - node_min.x) / 2.f;
    const vec3 node_q1 = node_min + half_extent * 0.5f;
    const vec3 node_center = node_min + half_extent;
    const vec3 node_q3 = node_min + half_extent * 1.5f;

    const vec3 q1_separator_ts = (node_q1 - ray.position) * ray.direction_inverse;
    const vec3 center_separator_ts = (node_center - ray.position) * ray.direction_inverse;
    const vec3 q3_separator_ts = (node_q3 - ray.position) * ray.direction_inverse;

    const vec3 half_extent_ray_lengths = half_extent * abs(ray.direction_inverse);
    const vec3 t_mins = center_separator_ts - half_extent_ray_lengths;
    const vec3 t_maxs = center_separator_ts + half_extent_ray_lengths;

    const float t_min = max(max(max(t_mins.x, t_mins.y), t_mins.z), 0.f);
    const float t_max = min(min(t_maxs.x, t_maxs.y), t_maxs.z);

    uint64_t intersection_mask = 0ul;

    // check intersections with the planes dividing the node into children
    intersection_mask |= compute_separator_intesection_mask(ray, q1_separator_ts, t_min, t_max, node_q1, node_center, node_q3,
        FAR_LEFT_CHILDREN_MASK | CENTER_LEFT_CHILDREN_MASK, FAR_DOWN_CHILDREN_MASK | CENTER_DOWN_CHILDREN_MASK, FAR_FRONT_CHILDREN_MASK | CENTER_FRONT_CHILDREN_MASK);

    intersection_mask |= compute_separator_intesection_mask(ray, center_separator_ts, t_min, t_max, node_q1, node_center, node_q3,
        CENTER_LEFT_CHILDREN_MASK | CENTER_RIGHT_CHILDREN_MASK, CENTER_DOWN_CHILDREN_MASK | CENTER_UP_CHILDREN_MASK, CENTER_FRONT_CHILDREN_MASK | CENTER_BACK_CHILDREN_MASK);

    intersection_mask |= compute_separator_intesection_mask(ray, q3_separator_ts, t_min, t_max, node_q1, node_center, node_q3,
        CENTER_RIGHT_CHILDREN_MASK | FAR_RIGHT_CHILDREN_MASK, CENTER_UP_CHILDREN_MASK | FAR_UP_CHILDREN_MASK, CENTER_BACK_CHILDREN_MASK | FAR_BACK_CHILDREN_MASK);

    // handle the case when the ray starts and ends inside a single child
    const vec3 inside_point = ray.position + (t_min + t_max) / 2.f * ray.direction;
    uint64_t inside_point_child_bit = 1ul;
    inside_point_child_bit <<= inside_point.x < node_q1.x ? 0ul
        : inside_point.x < node_center.x ? 1ul
        : inside_point.x < node_q3.x ? 2ul : 3ul;
    inside_point_child_bit <<= inside_point.y < node_q1.y ? 0ul
        : inside_point.y < node_center.y ? 16ul
        : inside_point.y < node_q3.y ? 32ul : 48ul;
    inside_point_child_bit <<= inside_point.z < node_q1.z ? 0ul
        : inside_point.z < node_center.z ? 4ul
        : inside_point.z < node_q3.z ? 8ul : 12ul;
    intersection_mask |= (t_min > t_max) ? 0ul : inside_point_child_bit;

    return intersection_mask;
}

uint compute_current_child_index(const Tree64Node node, inout StackElem stack_elem, const uint first_child_index) {
    const uint64_t children_mask = children_mask(node);
    while (stack_elem.checked_child_count < 64u) {
        const uint64_t current_child_index = stack_elem.checked_child_count ^ first_child_index;
        if ((children_mask & (1ul << current_child_index)) != 0ul && (stack_elem.children_intesection_mask & (1ul << current_child_index)) != 0ul) {
            return uint(current_child_index);
        }
        stack_elem.checked_child_count += 1u;
    }
    return 64u;
}

vec3 compute_color(const Ray ray, const vec3 aabb_min, const vec3 aabb_max) {
    const vec3 t1 = (aabb_min - ray.position) * ray.direction_inverse;
    const vec3 t2 = (aabb_max - ray.position) * ray.direction_inverse;
    const vec3 t_mins = min(t1, t2);
    const float t_min = max(max(t_mins.x, t_mins.y), t_mins.z);

    if (t_min == t1.x) {
        return vec3(1.f, 0.f, 0.f);
    } else if (t_min == t1.y) {
        return vec3(0.f, 1.f, 0.f);
    } else if (t_min == t1.z) {
        return vec3(0.f, 0.f, 1.f);
    } else if (t_min == t2.x) {
        return vec3(1.f, 1.f, 0.f);
    } else if (t_min == t2.y) {
        return vec3(0.f, 1.f, 1.f);
    } else {
        return vec3(1.f, 0.f, 1.f);
    }
}

struct NewStackElem {
    uint node_index;
    vec3 distances;
    uvec3 coords_start;
    vec3 first_distances;
    uvec3 node_min;
};

float ray_aabb_intersection(const Ray ray, const vec3 aabb_min, const vec3 aabb_max) {

    const vec3 t1 = (aabb_min - ray.position) * ray.direction_inverse;
    const vec3 t2 = (aabb_max - ray.position) * ray.direction_inverse;

    const vec3 mins = min(t1, t2);
    const vec3 maxs = max(t1, t2);

    const float tmin = max(max(mins.x, mins.y), mins.z);
    const float tmax = min(min(maxs.x, maxs.y), maxs.z);

    if (tmin > tmax || tmax < 0.f) {
        return -1.f;
    }
    return max(tmin, 0.f);
}

float unchecked_ray_aabb_intersection(const Ray ray, const vec3 aabb_min, const vec3 aabb_max) {

    const vec3 t1 = (aabb_min - ray.position) * ray.direction_inverse;
    const vec3 t2 = (aabb_max - ray.position) * ray.direction_inverse;
    const vec3 mins = min(t1, t2);
    const float tmin = max(max(mins.x, mins.y), mins.z);
    return max(tmin, 0.f);
}

uint64_t get_coords_bit(const uint base_center_bit, const uint stack_index, const uvec3 coords) {
    uint64_t coords_bit = 1ul;
    const uint center_bit = base_center_bit >> (stack_index << 1u);
    const uint quarter_bit = center_bit >> 1u;
    if ((coords.x & center_bit) != 0u) {
        coords_bit <<= 2ul;
    }
    if ((coords.x & quarter_bit) != 0u) {
        coords_bit <<= 1ul;
    }
    if ((coords.z & center_bit) != 0u) {
        coords_bit <<= 8ul;
    }
    if ((coords.z & quarter_bit) != 0u) {
        coords_bit <<= 4ul;
    }
    if ((coords.y & center_bit) != 0u) {
        coords_bit <<= 32ul;
    }
    if ((coords.y & quarter_bit) != 0u) {
        coords_bit <<= 16ul;
    }
    return coords_bit;
}

uvec3 coords_lookup_table[64] = {
    uvec3(0u, 0u, 0u), uvec3(1u, 0u, 0u), uvec3(2u, 0u, 0u), uvec3(3u, 0u, 0u),
    uvec3(0u, 0u, 1u), uvec3(1u, 0u, 1u), uvec3(2u, 0u, 1u), uvec3(3u, 0u, 1u),
    uvec3(0u, 0u, 2u), uvec3(1u, 0u, 2u), uvec3(2u, 0u, 2u), uvec3(3u, 0u, 2u),
    uvec3(0u, 0u, 3u), uvec3(1u, 0u, 3u), uvec3(2u, 0u, 3u), uvec3(3u, 0u, 3u),
    uvec3(0u, 1u, 0u), uvec3(1u, 1u, 0u), uvec3(2u, 1u, 0u), uvec3(3u, 1u, 0u),
    uvec3(0u, 1u, 1u), uvec3(1u, 1u, 1u), uvec3(2u, 1u, 1u), uvec3(3u, 1u, 1u),
    uvec3(0u, 1u, 2u), uvec3(1u, 1u, 2u), uvec3(2u, 1u, 2u), uvec3(3u, 1u, 2u),
    uvec3(0u, 1u, 3u), uvec3(1u, 1u, 3u), uvec3(2u, 1u, 3u), uvec3(3u, 1u, 3u),
    uvec3(0u, 2u, 0u), uvec3(1u, 2u, 0u), uvec3(2u, 2u, 0u), uvec3(3u, 2u, 0u),
    uvec3(0u, 2u, 1u), uvec3(1u, 2u, 1u), uvec3(2u, 2u, 1u), uvec3(3u, 2u, 1u),
    uvec3(0u, 2u, 2u), uvec3(1u, 2u, 2u), uvec3(2u, 2u, 2u), uvec3(3u, 2u, 2u),
    uvec3(0u, 2u, 3u), uvec3(1u, 2u, 3u), uvec3(2u, 2u, 3u), uvec3(3u, 2u, 3u),
    uvec3(0u, 3u, 0u), uvec3(1u, 3u, 0u), uvec3(2u, 3u, 0u), uvec3(3u, 3u, 0u),
    uvec3(0u, 3u, 1u), uvec3(1u, 3u, 1u), uvec3(2u, 3u, 1u), uvec3(3u, 3u, 1u),
    uvec3(0u, 3u, 2u), uvec3(1u, 3u, 2u), uvec3(2u, 3u, 2u), uvec3(3u, 3u, 2u),
    uvec3(0u, 3u, 3u), uvec3(1u, 3u, 3u), uvec3(2u, 3u, 3u), uvec3(3u, 3u, 3u),
};

uvec3 get_coords_from_coords_bit(const uint64_t coords_bit) {
    if (coords_bit > 0x00000000FFFFFFFFul) {
        return coords_lookup_table[32u + findLSB(uint(coords_bit >> 32ul))];
    }
    return coords_lookup_table[findLSB(uint(coords_bit))];
}

vec3 previous_float_unchecked(const vec3 x)
{
    uvec3 u = floatBitsToUint(x);
    u -= 1u;
    return uintBitsToFloat(u);
}

void main() {
    Ray ray = compute_ray();
    ray.position = u_camera_position;
    const vec3 orig_ray_position = ray.position;
    const float t = ray_aabb_intersection(ray, vec3(0.f), vec3(float(exp4(u_tree64_depth))));
    if (t == -1.f) {
        out_color = vec4(0.01f, 0.01f, 0.01f, 1.f);
        return;
    }
    const ivec3 coords_step = ivec3(sign(ray.direction));
    const vec3 distances_step = abs(ray.direction_inverse);
    ray.position += t * ray.direction;
    ray.position = clamp(ray.position, vec3(0.f), previous_float_unchecked(vec3(exp4(u_tree64_depth))));
    uvec3 coords = uvec3(ray.position);

    const vec3 to_negative_straight_distances = fract(ray.position);
    const vec3 straight_distances = mix(to_negative_straight_distances,
        1.f - to_negative_straight_distances, equal(coords_step, uvec3(1u)));
    const uint base_multiplier = exp4(u_tree64_depth) >> 2u;
    const uvec3 child_steps_to_negative_next_coords = coords % base_multiplier;
    const uvec3 child_steps_to_next_coords = mix(child_steps_to_negative_next_coords, base_multiplier
        - (child_steps_to_negative_next_coords + 1u), equal(coords_step, uvec3(1u)));
    NewStackElem stack[MAX_TREE64_DEPTH];
    stack[0] = NewStackElem(0u, straight_distances * distances_step + child_steps_to_next_coords * distances_step,
        coords, straight_distances * distances_step, uvec3(0u));

    float last_distance = 0.f;
    const uint base_center_bit = exp4(u_tree64_depth) >> 1u;
    uint stack_index = 0u;
    while (true) {
        const Tree64Node node = u_tree64_nodes_device_address.b_tree64_nodes[stack[stack_index].node_index];
        uint64_t coords_bit = get_coords_bit(base_center_bit, stack_index, coords);
        uint multiplier = base_multiplier >> (stack_index << 1u);
        if ((children_mask(node) & coords_bit) != 0ul) {
            // TODO if child is leaf maybe call separate function that zoom through voxels instead of the else
            if (!is_leaf(node)) {
                const vec3 distance_from_start = last_distance - stack[stack_index].first_distances;
                if (distance_from_start.x >= 0.f) {
                    coords.x = stack[stack_index].coords_start.x + coords_step.x * (1u
                        + uint(distance_from_start.x / distances_step.x + 0.0001f));
                }
                if (distance_from_start.z >= 0.f) {
                    coords.z = stack[stack_index].coords_start.z + coords_step.z * (1u
                        + uint(distance_from_start.z / distances_step.z + 0.0001f));
                }
                if (distance_from_start.y >= 0.f) {
                    coords.y = stack[stack_index].coords_start.y + coords_step.y * (1u
                        + uint(distance_from_start.y / distances_step.y + 0.0001f));
                }

                const uvec3 node_min = coords - coords % multiplier;
                const uvec3 steps_to_remove_for_negative_next_coords = coords % multiplier;
                const vec3 first_distances = stack[stack_index].distances - mix(steps_to_remove_for_negative_next_coords,
                    multiplier - 1u - steps_to_remove_for_negative_next_coords, equal(coords_step, uvec3(1u))) * distances_step;
                multiplier >>= 2u;
                const uvec3 child_steps_to_remove_for_negative_next_child = coords / multiplier % 4u;
                const vec3 distances = stack[stack_index].distances - mix(child_steps_to_remove_for_negative_next_child,
                    3u - child_steps_to_remove_for_negative_next_child, equal(coords_step, uvec3(1u))) * distances_step * multiplier;
                stack_index += 1u;
                stack[stack_index] = NewStackElem(first_child_node_index(node) + child_bit_node_offset(node, coords_bit),
                    distances, coords, first_distances, node_min);
                continue;
            } else {
                uvec3 cell_min = stack[stack_index].node_min + get_coords_from_coords_bit(coords_bit);
                out_color = vec4(compute_color(ray, cell_min, cell_min + 1u), 1.f);
                return;
            }
        }

        vec3 distances = stack[stack_index].distances;
        const float min_distance = min(min(distances.x, distances.z), distances.y);
        if (distances.x == min_distance) {
            coords.x += mix(-int(coords.x % multiplier + 1u), int(multiplier - coords.x % multiplier), coords_step.x == 1u);
            last_distance = distances.x;
            distances.x += multiplier * distances_step.x;
        } else if (distances.z == min_distance) {
            coords.z += mix(-int(coords.z % multiplier + 1u), int(multiplier - coords.z % multiplier), coords_step.z == 1u);
            last_distance = distances.z;
            distances.z += multiplier * distances_step.z;
        } else {
            coords.y += mix(-int(coords.y % multiplier + 1u), int(multiplier - coords.y % multiplier), coords_step.y == 1u);
            last_distance = distances.y;
            distances.y += multiplier * distances_step.y;
        }
        stack[stack_index].distances = distances;

        bool has_exited_node_tree = false;
        bool has_exited_node = false;
        while (any(lessThan(coords, stack[stack_index].node_min)) || any(lessThanEqual(stack[stack_index].node_min + 4u * multiplier, coords))) {
            has_exited_node = true;
            if (stack_index == 0u) {
                has_exited_node_tree = true;
                break;
            }
            stack_index -= 1u;
            multiplier <<= 2u;
        }
        if (has_exited_node_tree) {
            break;
        }
        if (has_exited_node) {
            vec3 distances = stack[stack_index].distances;
            const float min_distance = min(min(distances.x, distances.z), distances.y);
            if (distances.x == min_distance) {
                last_distance = distances.x;
                distances.x += multiplier * distances_step.x;
            } else if (distances.z == min_distance) {
                last_distance = distances.z;
                distances.z += multiplier * distances_step.z;
            } else {
                last_distance = distances.y;
                distances.y += multiplier * distances_step.y;
            }
            stack[stack_index].distances = distances;
        }
    }
    out_color = vec4(0.f, 0.f, 0.f, 1.f);
}

void amain() {
    Ray ray = compute_ray();
    const uint first_child_index = compute_first_child_index(ray.direction);
    StackElem stack[MAX_TREE64_DEPTH];
    stack[0] = StackElem(compute_children_intesection_mask(ray, vec3(1.f), vec3(2.f)), 0u, 0u, vec3(1.f));
    uint stack_index = 0u;

    while (true) {
        const Tree64Node node = u_tree64_nodes_device_address.b_tree64_nodes[stack[stack_index].node_index];
        const uint current_child_index = compute_current_child_index(node, stack[stack_index], first_child_index);
        if (current_child_index == 64u) {
            if (stack_index == 0u) {
                //out_color = vec4(vec3(float(iter) / 300.f), 1.f); return;
                if (stack[0].children_intesection_mask == 0ul) {
                    out_color = vec4(1.f, 1.f, 1.f, 1.f);
                    return;
                }
                break;
            }
            stack_index -= 1u;
            stack[stack_index].checked_child_count += 1u;
            continue;
        }
        vec3 child_min = stack[stack_index].node_min;
        float child_size = exp2(-float(2u * stack_index + 1u));
        child_min += child_size * vec3(float((current_child_index & 2u) >> 1u), float((current_child_index & 32u) >> 5u), float((current_child_index & 8u) >> 3u));
        child_size /= 2.f;
        child_min += child_size * vec3(float(current_child_index & 1u), float((current_child_index & 16u) >> 4u), float((current_child_index & 4u) >> 2u));
        if (is_leaf(node)) {
            out_color = vec4(compute_color(ray, child_min, child_min + child_size), 1.f);
            //out_color = vec4(vec3(float(iter) / 300.f), 1.f);
            return;
        }
        stack_index += 1u;
        stack[stack_index] = StackElem(compute_children_intesection_mask(ray, child_min, child_min + child_size),
            0u, first_child_node_index(node) + child_node_offset(node, current_child_index), child_min);
    }
    out_color = vec4(0.f, 0.f, 0.f, 1.f);
}
