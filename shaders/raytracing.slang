struct Tree64Node {
    uint2 children_mask_uint2;
    uint is_leaf_and_first_child_node_index;

    property uint64_t children_mask {
        get {
            return reinterpret<uint64_t>(children_mask_uint2);
        }
    }

    property bool is_leaf {
        get {
            return (is_leaf_and_first_child_node_index & 1u) == 1u;
        }
    }

    property uint first_child_node_index {
        get {
            return is_leaf_and_first_child_node_index >> 1u;
        }
    }

    uint child_node_offset(const uint64_t child_bit) {
        const uint64_t mask = child_bit - 1ull;
        return countbits(mask & children_mask);
    }
};

static const uint MAX_TREE64_DEPTH = 11u;

struct HosekWilkieSkyRenderingParameters {
    float3 config[9];
    float3 luminance;

    private float3 get_sky_radiance(const float cos_theta, const float gamma, const float cos_gamma) {
        const float3 expM = exp(config[4] * gamma);
        const float rayM = cos_gamma * cos_gamma;
        const float3 mieM = (1. + rayM) / pow(1. + config[8] - 2. * config[8] * cos_gamma, float3(1.5));
        const float zenith = sqrt(cos_theta);
        return (1. + config[0] * exp(config[1] / (cos_theta + 0.01)))
            * (config[2] + config[3] * expM + config[5] * rayM + config[6] * mieM + config[7] * zenith);
    }

    float3 get_sky_color(const float3 ray_direction) {
        const float cos_theta = clamp(ray_direction.y, 0., 1.);
        const float cos_gamma = clamp(dot(ray_direction, pc.to_sun_direction), 0., 1.);
        return get_sky_radiance(cos_theta, acos(cos_gamma), cos_gamma) * luminance;
    }
};

struct PushConstants {
    float aspect_ratio;
    float3 camera_position;
    float3x3 camera_rotation;
    float3 to_sun_direction;
    HosekWilkieSkyRenderingParameters* hosek_wilkie_sky_rendering_parameters;
    Tree64Node* tree64_nodes;
    uint tree64_depth;
};
[vk::push_constant]
PushConstants pc;

struct Interpolants {
    float4 position : SV_Position;
    float3 ray_direction;
};

[shader("vertex")]
Interpolants main(const uint vertex_id : SV_VertexID) {
    float x = -1. + float((vertex_id & 1u) << 2u);
    float y = -1. + float((vertex_id & 2u) << 1u);
    Interpolants output;
    output.position = float4(x, y, 1., 1.);
    output.ray_direction = mul(pc.camera_rotation, float3(x * pc.aspect_ratio, -y, 1.));
    return output;
}

struct Ray {
    float3 position;
    float3 direction;
    float3 direction_inverse;
};

uint exp4(const uint exponent) {
    return 1u << (exponent << 1u);
}

Ray compute_ray(const float3 direction) {
    Ray ray;
    ray.position = pc.camera_position / float(exp4(pc.tree64_depth)) + 1.;
    ray.direction = direction;
    // Avoid division by zero
    const float epsilon = exp2(-23.);
    if (abs(ray.direction.x) < epsilon) {
        ray.direction.x = copysign(epsilon,ray.direction.x);
    }
    if (abs(ray.direction.y) < epsilon) {
        ray.direction.y = copysign(epsilon, ray.direction.y);
    }
    if (abs(ray.direction.z) < epsilon) {
        ray.direction.z = copysign(epsilon, ray.direction.z);
    }
    ray.direction_inverse = 1. / ray.direction;
    return ray;
}

float ray_aabb_intersection(const Ray ray, const float3 aabb_min, const float3 aabb_max) {
    const float3 t1 = (aabb_min - ray.position) * ray.direction_inverse;
    const float3 t2 = (aabb_max - ray.position) * ray.direction_inverse;

    const float3 t_mins = min(t1, t2);
    const float3 t_maxs = max(t1, t2);

    const float t_min = max3(t_mins.x, t_mins.y, t_mins.z);
    const float t_max = min3(t_maxs.x, t_maxs.y, t_maxs.z);

    if (t_min > t_max || t_max < 0.) {
        return -1.;
    }
    return max(t_min, 0.);
}

uint64_t get_child_bit(const float3 position, const uint child_scale_bit_offset, const uint mirror_mask) {
    const uint3 child_coords = (asuint(position) >> child_scale_bit_offset) & 3u;
    return 1ull << ((child_coords.x + child_coords.z * 4u + child_coords.y * 16u) ^ mirror_mask);
}

[shader("fragment")]
float4 main(const Interpolants input) : SV_Target {
    const float3 real_ray_direction = normalize(input.ray_direction);
    Ray ray = compute_ray(real_ray_direction);
    const float t = ray_aabb_intersection(ray, float3(1.), float3(2.));
    if (t == -1.) {
        return float4(0.01, 0.01, 0.01, 1.);
    }
    const float3 ray_origin = select(ray.direction <= 0., ray.position, 3. - ray.position);
    // Mirror the ray and coordinates to optimize the traversal, knowing that the ray moves in the negative direction
    const uint mirror_mask = uint(ray.direction.x > 0.) * 0x03u
        | uint(ray.direction.z > 0.) * 0x0Cu | uint(ray.direction.y > 0.) * 0x30u;
    ray.direction = -abs(ray.direction);
    ray.direction_inverse = 1. / ray.direction;
    ray.position = clamp(ray_origin + t * ray.direction, float3(1.), float3(1.99999988079071044921875));

    uint node_index_stack[MAX_TREE64_DEPTH];
    uint node_index = 0u;
    uint child_scale_bit_offset = 21u;
    for (uint i = 0u; i < 2000u; ++i) {
        // Descend to current node
        Tree64Node node = pc.tree64_nodes[node_index];
        uint64_t child_bit = get_child_bit(ray.position, child_scale_bit_offset, mirror_mask);
        bool has_child_at_child_bit = (node.children_mask & child_bit) != 0ull;
        while (has_child_at_child_bit && !node.is_leaf) {
            node_index_stack[child_scale_bit_offset >> 1u] = node_index;
            node_index = node.first_child_node_index + node.child_node_offset(child_bit);
            node = pc.tree64_nodes[node_index];

            child_scale_bit_offset -= 2u;
            child_bit = get_child_bit(ray.position, child_scale_bit_offset, mirror_mask);
            has_child_at_child_bit = (node.children_mask & child_bit) != 0ull;
        }

        const float3 child_min = asfloat(asuint(ray.position) & (~0u << child_scale_bit_offset));
        if (has_child_at_child_bit) {
            const float scale = asfloat((child_scale_bit_offset + 127u - 23u) << 23u); // exp2(int(child_scale_bit_offset) - 23)
            const float3 distances = ((child_min + scale) - ray_origin) * ray.direction_inverse;
            const float enter_t = max3(distances.x, distances.z, distances.y);
            const int3 normal = int3(enter_t == distances) * -sign(real_ray_direction);
            return float4(float3(normal / 2. + int3(enter_t == distances)), 1.);
        }
        // Advance to neighbor
        const float3 distances = (child_min - ray_origin) * ray.direction_inverse;
        const float exit_t = min3(distances.x, distances.z, distances.y);
        const uint3 child_min_as_uint = asuint(child_min);
        const float3 neighbor_max = asfloat(select(distances == exit_t, child_min_as_uint - 1u,
            child_min_as_uint | ((1u << child_scale_bit_offset) - 1u)));
        ray.position = min(ray_origin + exit_t * ray.direction, neighbor_max);

        // Ascend back to higher non-exited node
        const uint3 binary_diff = asuint(ray.position) ^ child_min_as_uint;
        // & with 0b11111111101010101010101010101010u to check only for odd offsets (quarter of nodes) and for root exit with the leading 1s
        const uint binary_diff_offset = firstbithigh((binary_diff.x | binary_diff.y | binary_diff.z) & 0xFFAAAAAAu);
        if (binary_diff_offset > child_scale_bit_offset) {
            if (binary_diff_offset > 21u) {
                break; // out of root
            }
            child_scale_bit_offset = binary_diff_offset;
            node_index = node_index_stack[child_scale_bit_offset >> 1u];
        }
    }
    return float4(pc.hosek_wilkie_sky_rendering_parameters->get_sky_color(real_ray_direction), 1.);
}
