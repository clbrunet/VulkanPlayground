uint exp4(const uint exponent) {
    return 1u << (exponent << 1u);
}

struct Ray {
    float3 position;
    float3 direction;
    float3 direction_inverse;

    __init(const float3 position, const float3 direction) {
        this = Ray(position, direction, 1. / direction);
    }

    __init(const float3 position, const float3 direction, const float3 direction_inverse) {
        this.position = position;
        this.direction = direction;
        this.direction_inverse = direction_inverse;
    }

    struct AabbIntersection {
        float enter_distance;
        float3 enter_distances;
    };

    Optional<AabbIntersection> aabb_intersection(const float3 aabb_min, const float3 aabb_max) {
        let aabb_min_ts = (aabb_min - position) * direction_inverse;
        let aabb_max_ts = (aabb_max - position) * direction_inverse;

        let enter_ts = min(aabb_min_ts, aabb_max_ts);
        let exit_ts = max(aabb_min_ts, aabb_max_ts);

        let enter_t = max3(enter_ts.x, enter_ts.y, enter_ts.z);
        let exit_t = min3(exit_ts.x, exit_ts.y, exit_ts.z);

        if (enter_t > exit_t || exit_t < 0.) {
            return none;
        }
        return AabbIntersection(enter_t, enter_ts);
    }
};

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

    uint child_node_offset(const uint child_bit_index) {
        let before_child_mask = (1ull << child_bit_index) - 1ull;
        return countbits(children_mask & before_child_mask);
    }

    bool has_child_at_bit_index(const uint child_bit_index) {
        return (children_mask & (1ull << child_bit_index)) != 0ull;
    }
};

struct Hit {
    float distance;
    float3 position;
    int3 normal;
};

struct Tree64 {
    static const uint UNUSED_DEPTH = 4u; // TODO: maybe set it as a specialization constant to get the maximum performance out of the rendered trees
    static const uint MAX_DEPTH = 11u - UNUSED_DEPTH;
    Tree64Node* nodes;
    uint depth;

    static uint get_child_bit_index(const float3 position, const uint child_scale_bit_offset, const uint mirror_mask) {
        let child_coords = (asuint(position) >> child_scale_bit_offset) & 3u;
        return (child_coords.x + child_coords.z * 4u + child_coords.y * 16u) ^ mirror_mask;
    }

    Optional<Hit> raycast(const Ray ray_origin, float max_distance) {
        let depth_exp4 = float(exp4(depth));
        var ray = Ray(ray_origin.position / depth_exp4 + 1., ray_origin.direction, ray_origin.direction_inverse);
        let aabb_intersection = ray.aabb_intersection(float3(1.), float3(2.));
        if (!aabb_intersection.hasValue) {
            return none;
        }
        var current_distance = max(aabb_intersection.value.enter_distance, 0.);
        var current_distances = aabb_intersection.value.enter_distances;
        max_distance /= depth_exp4;
        if (current_distance >= max_distance) {
            return none;
        }
        // Mirror the ray and coordinates to optimize the traversal, knowing that the ray moves in the negative direction
        let mirrored_ray_origin = select(ray.direction <= 0., ray.position, 3. - ray.position);
        let mirror_mask = uint(ray.direction.x > 0.) * 0b000011u
            | uint(ray.direction.z > 0.) * 0b001100u | uint(ray.direction.y > 0.) * 0b110000u;
        ray.direction = -abs(ray.direction);
        ray.direction_inverse = 1. / ray.direction;
        ray.position = clamp(mirrored_ray_origin + current_distance * ray.direction, float3(1.), float3(1.99999988079071044921875));

        var node_index_stack: uint[MAX_DEPTH];
        var node_index = 0u;
        var child_scale_bit_offset = 21u;
        while (true) {
            // Descend to current node
            var node = nodes[node_index];
            var child_bit_index = get_child_bit_index(ray.position, child_scale_bit_offset, mirror_mask);
            var has_child_at_child_bit = node.has_child_at_bit_index(child_bit_index);
            while (has_child_at_child_bit && !node.is_leaf) {
                node_index_stack[(child_scale_bit_offset >> 1u) - UNUSED_DEPTH] = node_index;
                node_index = node.first_child_node_index + node.child_node_offset(child_bit_index);
                node = nodes[node_index];

                child_scale_bit_offset -= 2u;
                child_bit_index = get_child_bit_index(ray.position, child_scale_bit_offset, mirror_mask);
                has_child_at_child_bit = node.has_child_at_bit_index(child_bit_index);
            }
            // Check if there is no children in the entire octant to maybe skip unnecessary iterations
            let coarse_child_scale_bit_offset = child_scale_bit_offset
                + uint((node.children_mask & (0b00000000001100110000000000110011ull << (child_bit_index & 0b101010ull))) == 0u);

            let child_min = asfloat(asuint(ray.position) & (~0u << coarse_child_scale_bit_offset));
            if (has_child_at_child_bit) {
                let world_distance = current_distance * depth_exp4;
                let position = ray_origin.position + world_distance * ray_origin.direction;
                let normal = int3(current_distance == current_distances) * -sign(ray_origin.direction);
                return Hit(world_distance, position, normal);
            }
            // Advance to neighbor
            current_distances = (child_min - mirrored_ray_origin) * ray.direction_inverse;
            current_distance = min3(current_distances.x, current_distances.z, current_distances.y);
            if (current_distance >= max_distance) {
                break;
            }
            let child_min_as_uint = asuint(child_min);
            let neighbor_max = asfloat(select(current_distances == current_distance, child_min_as_uint - 1u,
                child_min_as_uint | ((1u << coarse_child_scale_bit_offset) - 1u)));
            ray.position = min(mirrored_ray_origin + current_distance * ray.direction, neighbor_max);

            // Ascend back to higher non-exited node
            let binary_diff = asuint(ray.position) ^ child_min_as_uint;
            let binary_diff_offset = firstbithigh((binary_diff.x | binary_diff.y | binary_diff.z)
                & 0b11111111101010101010101010101010u); // check only for odd offsets (quarter of nodes) and for root exit with the leading 1s
            if (binary_diff_offset > child_scale_bit_offset) {
                if (binary_diff_offset > 21u) {
                    break; // out of root
                }
                child_scale_bit_offset = binary_diff_offset;
                node_index = node_index_stack[(child_scale_bit_offset >> 1u) - UNUSED_DEPTH];
            }
        }
        return none;
    }
};

struct HosekWilkieSkyRenderingParameters {
    float3 config[9];
    float3 luminance;

    private float3 get_sky_radiance(const float cos_theta, const float gamma, const float cos_gamma) {
        let expM = exp(config[4] * gamma);
        let rayM = cos_gamma * cos_gamma;
        let mieM = (1. + rayM) / pow(1. + config[8] * config[8] - 2. * config[8] * cos_gamma, float3(1.5));
        let zenith = sqrt(cos_theta);
        return (1. + config[0] * exp(config[1] / (cos_theta + 0.01)))
            * (config[2] + config[3] * expM + config[5] * rayM + config[6] * mieM + config[7] * zenith);
    }

    float3 get_sky_color(const float3 ray_direction) {
        let cos_theta = clamp(ray_direction.y, 0., 1.);
        let cos_gamma = clamp(dot(ray_direction, pc.to_sun_direction), 0., 1.);
        return get_sky_radiance(cos_theta, acos(cos_gamma), cos_gamma) * luminance;
    }
};

struct PushConstants {
    float aspect_ratio;
    float3 camera_position;
    float3x3 camera_rotation;
    float3 to_sun_direction;
    HosekWilkieSkyRenderingParameters* hosek_wilkie_sky_rendering_parameters;
    Tree64 tree64;
};
[vk::push_constant]
PushConstants pc;

struct Interpolants {
    float4 position : SV_Position;
    float3 ray_direction;
};

[shader("vertex")]
Interpolants main(const uint vertex_id : SV_VertexID) {
    let x = -1. + float((vertex_id & 1u) << 2u);
    let y = -1. + float((vertex_id & 2u) << 1u);
    let position = float4(x, y, 1., 1.);
    let ray_direction = mul(pc.camera_rotation, float3(x * pc.aspect_ratio, -y, 1.));
    return Interpolants(position, ray_direction);
}

[shader("fragment")]
float4 main(const Interpolants input) : SV_Target {
    let ray = Ray(pc.camera_position, normalize(input.ray_direction));
    if (let hit = pc.tree64.raycast(ray, 1.e6)) {
        let half_lambert_diffuse_factor = dot(hit.normal, pc.to_sun_direction) * 0.5 + 0.5;
        var light_multiplier = 0.5;
        if (half_lambert_diffuse_factor > 0.5) {
            let to_sun_hit = pc.tree64.raycast(Ray(hit.position + hit.normal * 0.005, pc.to_sun_direction), 1.e6);
            light_multiplier = to_sun_hit.hasValue ? 0.5 : 1.;
        }
        return float4(float3(light_multiplier * half_lambert_diffuse_factor), 1.);
    }
    return float4(pc.hosek_wilkie_sky_rendering_parameters->get_sky_color(ray.direction), 1.);
}
