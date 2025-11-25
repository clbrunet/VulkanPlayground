#version 450

#include "raytracing_push_contants.glsl"

layout(location = 0) in vec3 v_ray_direction;

layout(location = 0) out vec4 out_color;

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

vec3 copysign(vec3 magnitude, vec3 sign_part)
{
    return uintBitsToFloat(
        (floatBitsToUint(magnitude) & 0x7FFFFFFFu) | (floatBitsToUint(sign_part) & 0x80000000u)
    );
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

uint64_t get_child_bit(vec3 position, uint child_scale_bit_offset) {
    const uvec3 child_coords = (floatBitsToUint(position) >> child_scale_bit_offset) & 3u;
    return 1ul << (child_coords.x + child_coords.z * 4u + child_coords.y * 16u);
}

void main() {
    Ray ray = compute_ray();
    const float t = ray_aabb_intersection(ray, vec3(1.f), vec3(2.f));
    if (t == -1.f) {
        out_color = vec4(0.01f, 0.01f, 0.01f, 1.f);
        return;
    }
    const vec3 ray_origin = ray.position;
    ray.position = clamp(ray_origin + t * ray.direction, vec3(1.f), vec3(1.99999988079071044921875f));
    while (true) {
        uint node_index = 0u;
        Tree64Node node = u_tree64_nodes_device_address.b_tree64_nodes[node_index];
        uint child_scale_bit_offset = 21u;
        uint64_t child_bit = get_child_bit(ray.position, child_scale_bit_offset);
        bool has_child_at_child_bit = (children_mask(node) & child_bit) != 0ul;
        while (has_child_at_child_bit && !is_leaf(node)) {
            node_index = first_child_node_index(node) + child_node_offset(node, child_bit);
            node = u_tree64_nodes_device_address.b_tree64_nodes[node_index];
            child_scale_bit_offset -= 2u;
            child_bit = get_child_bit(ray.position, child_scale_bit_offset);
            has_child_at_child_bit = (children_mask(node) & child_bit) != 0ul;
        }

        const vec3 child_min = uintBitsToFloat(floatBitsToUint(ray.position) & (~0u << child_scale_bit_offset));
        const float scale = uintBitsToFloat((child_scale_bit_offset + 127u - 23u) << 23u); // exp2(int(child_scale_bit_offset) - 23)
        if (has_child_at_child_bit) {
            out_color = vec4(compute_color(ray, child_min, child_min + scale), 1.f);
            return;
        }
        // Advance to neighbor
        const vec3 exit_planes = child_min + step(0.f, ray.direction) * scale;
        const vec3 distances = (exit_planes - ray_origin) * ray.direction_inverse;
        const float exit_t = min(min(distances.x, distances.y), distances.z);
        const vec3 neighbor_min = mix(child_min, child_min + copysign(vec3(scale), ray.direction), equal(distances, vec3(exit_t)));
        const vec3 neighbor_max = uintBitsToFloat(floatBitsToUint(neighbor_min) | ((1u << child_scale_bit_offset) - 1u));
        ray.position = clamp(ray_origin + exit_t * ray.direction, neighbor_min, neighbor_max);

        if (ray.position.x <= 1.f || ray.position.y <= 1.f || ray.position.z <= 1.f
            || 2.f <= ray.position.x || 2.f <= ray.position.y || 2.f <= ray.position.z) {
            break;
        }
    }
    out_color = vec4(0.f, 0.f, 0.f, 1.f);
}
