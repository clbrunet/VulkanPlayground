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

struct NewStackElem {//TODO
    vec3 distances;
    uint64_t coords_bit;
    uint node_index;
    vec3 ray_position;
    vec3 new_ray_position;
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

uint64_t get_coords_bit(vec3 ray_position, uint stack_index) {//TODO rename stackindex or overall improve
    uint64_t bit = 1ul;
    uvec3 uint_ray_position = floatBitsToUint(ray_position);
    uint center_bit = 1u << (22u - (stack_index << 1u));
    uint quarter_bit = center_bit >> 1u;
    if ((uint_ray_position.x & center_bit) != 0u) {
        bit <<= 2u;
    }
    if ((uint_ray_position.x & quarter_bit) != 0u) {
        bit <<= 1u;
    }
    if ((uint_ray_position.z & center_bit) != 0u) {
        bit <<= 8u;
    }
    if ((uint_ray_position.z & quarter_bit) != 0u) {
        bit <<= 4u;
    }
    if ((uint_ray_position.y & center_bit) != 0u) {
        bit <<= 32u;
    }
    if ((uint_ray_position.y & quarter_bit) != 0u) {
        bit <<= 16u;
    }
    return bit;
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

uvec3 get_coords_from_coords_bit(uint64_t coords_bit) {
    if (coords_bit > 0x00000000FFFFFFFFul) {
        return coords_lookup_table[32u + findLSB(uint(coords_bit >> 32ul))];
    }
    return coords_lookup_table[findLSB(uint(coords_bit))];
}

void main() {
    //TODO
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

    Ray ray = compute_ray();
    vec3 orig_ray_position = ray.position;
    float t = ray_aabb_intersection(ray, vec3(1.f), vec3(2.f));
    if (t == -1.f) {
        out_color = vec4(0.01f, 0.01f, 0.01f, 1.f);
        return;
    }
    const ivec3 coords_step = ivec3(sign(ray.direction));
    // if (any(equal(coords_step, ivec3(-1)))) { // TODO
    //     out_color = vec4(0.1f, 0.1f, 0.1f, 1.f);
    //     return;
    // }
    const vec3 distances_step = ray.direction_inverse * 0.25f;
    ray.position += t * ray.direction;
    ray.position = clamp(ray.position, vec3(1.f), vec3(1.99999988079071044921875f));
    // uint base_q_size = exp4(u_tree64_depth) >> 2u;
    // vec3 relative_position = ray.position / base_q_size;
    const vec3 to_negative_straight_distances = mod(ray.position, 0.25f) / 0.25;
    //TODO rename straight_distances (its just a fraction or portion because 0,1.f)
    const vec3 straight_distances = mix(to_negative_straight_distances,//TODO faster seems possible
        1.f - to_negative_straight_distances, vec3(coords_step + 1u) * 0.5f);//TODO if i duplicate for each variants of coords_step, the can be specialized (fastest)
    uint stack_index = 0u;
    NewStackElem stack[MAX_TREE64_DEPTH];
    stack[0] = NewStackElem(straight_distances * distances_step, get_coords_bit(ray.position, 0u), 0u, ray.position, ray.position);
    while (true) {
        // if (stack_index >= u_tree64_depth) {
        //     out_color = vec4(1.f, 1.f, 1.f, 1.f);
        //     return;
        // }
        // if (stack[stack_index].node_index > 264399) {
        // // if (stack[stack_index].node_index > 2) {
        //     out_color = vec4(0.5f, 0.5f, 0.5f, 1.f);
        //     return;
        // }
        vec3 distances = stack[stack_index].distances;
#if 0
        //TODO seems slower
        TODO must be modified to retest
        bvec3 mask = lessThanEqual(distances.xyz, min(distances.yzx, distances.zxy));
        stack[stack_index].distances += vec3(mask) * distances_step;
        stack[stack_index].coords += ivec3(mask) * coords_step;
#else
        float distances_step_factor = 1.f / float(1u << (2u * stack_index));
        float min_distance;
        uint64_t coords_bit = stack[stack_index].coords_bit;
        if (distances.x < distances.y) {
            if (distances.x < distances.z) {
                if (coords_step.x == 1) {
                    if ((coords_bit & FAR_RIGHT_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    }
                    else {
                        stack[stack_index].coords_bit <<= 1ul;
                    }
                } else {
                    if ((coords_bit & FAR_LEFT_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    } else {
                        stack[stack_index].coords_bit >>= 1ul;
                    }
                }
                min_distance = distances.x;
                distances.x += distances_step_factor * distances_step.x;
            } else {
                if (coords_step.z == 1) {
                    if ((coords_bit & FAR_BACK_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    }
                    else {
                        stack[stack_index].coords_bit <<= 4ul;
                    }
                } else {
                    if ((coords_bit & FAR_FRONT_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    } else {
                        stack[stack_index].coords_bit >>= 4ul;
                    }
                }
                min_distance = distances.z;
                distances.z += distances_step_factor * distances_step.z;
            }
        } else {
            if (distances.z < distances.y) {
                if (coords_step.z == 1) {
                    if ((coords_bit & FAR_BACK_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    }
                    else {
                        stack[stack_index].coords_bit <<= 4ul;
                    }
                } else {
                    if ((coords_bit & FAR_FRONT_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    } else {
                        stack[stack_index].coords_bit >>= 4ul;
                    }
                }
                min_distance = distances.z;
                distances.z += distances_step_factor * distances_step.z;
            } else {
                if (coords_step.y == 1) {
                    if ((coords_bit & FAR_UP_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    }
                    else {
                        stack[stack_index].coords_bit <<= 16ul;
                    }
                } else {
                    if ((coords_bit & FAR_DOWN_CHILDREN_MASK) != 0ul) {
                        stack[stack_index].coords_bit = 0ul;
                    } else {
                        stack[stack_index].coords_bit >>= 16ul;
                    }
                }
                min_distance = distances.y;
                distances.y += distances_step_factor * distances_step.y;
            }
        }
        stack[stack_index].distances = distances;
#endif
        const Tree64Node node = u_tree64_nodes_device_address.b_tree64_nodes[stack[stack_index].node_index];
        if ((children_mask(node) & coords_bit) != 0ul) {
            if (!is_leaf(node)) {
                const float quarter_size = exp2(-float(4u + 2u * stack_index));
                vec3 new_ray_position = stack[stack_index].new_ray_position;
                stack[stack_index].new_ray_position = stack[stack_index].ray_position + min_distance * ray.direction;
                vec3 ray_position = new_ray_position;
                vec3 to_negative_straight_distances = mod(ray_position, quarter_size) / quarter_size;
                if (to_negative_straight_distances.x < 0.00001f && coords_step.x == -1) {
                    ray_position.x -= quarter_size / 2.f;
                    to_negative_straight_distances.x = 1.f;
                }
                if (to_negative_straight_distances.y < 0.00001f && coords_step.y == -1) {
                    ray_position.y -= quarter_size / 2.f;
                    to_negative_straight_distances.y = 1.f;
                }
                if (to_negative_straight_distances.z < 0.00001f && coords_step.z == -1) {
                    ray_position.z -= quarter_size / 2.f;
                    to_negative_straight_distances.z = 1.f;
                }
                const vec3 straight_distances = mix(to_negative_straight_distances,//TODO faster seems possible
                    1.f - to_negative_straight_distances, vec3(coords_step + 1u) / 2.f);//TODO if I duplicate for each variants for coords_step, the can be specialized (fastest)
                stack_index += 1u;
                stack[stack_index] = NewStackElem(straight_distances * (distances_step_factor * 0.25f) * distances_step, get_coords_bit(ray_position, stack_index),
                    first_child_node_index(node) + child_bit_node_offset(node, coords_bit), new_ray_position, new_ray_position);
                continue;
            } else {
                // TODO if leaf maybe call separate function that zoom through voxels
                // out_color = vec4(compute_color(ray, node_min, node_min + q_size), 1.f);
                out_color = vec4(1.f, vec2(distance(stack[stack_index].new_ray_position, orig_ray_position) / 2.f), 1.f);
                return;
            }
        } else {
            stack[stack_index].new_ray_position = stack[stack_index].ray_position + min_distance * ray.direction;
        }
        bool should_break = false;
        while (stack[stack_index].coords_bit == 0ul) {
            if (stack_index == 0u) {
                should_break = true;
                break;
            }
            stack_index -= 1u;
        }
        if (should_break) {
            break;
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
