#version 450

struct OctreeNode {
	uint bits;
};

bool is_leaf(OctreeNode node) {
	return (node.bits & 1u) == 1u;
}

uint octants_mask(OctreeNode node) {
	return (node.bits << 23u) >> 24u;
}

uint first_octant_node_index(OctreeNode node) {
	return node.bits >> 9u;
}

const uint MAX_OCTREE_DEPTH = 15u;

struct StackElem {
	uint octants_intesection_mask;
	uint checked_octant_count;
	uint node_index;
	vec3 node_min;
};

struct Ray {
	vec3 position;
	vec3 direction;
	vec3 direction_inverse;
};

layout(std430, binding = 0) readonly buffer OctreeNodesBuffer {
	OctreeNode b_octree_nodes[];
};

layout(push_constant) uniform PushConstants {
	vec3 u_camera_position;
	float u_aspect_ratio;
	mat3 u_camera_rotation;
	uint u_octree_depth;
};

layout(location = 0) in vec3 v_ray_direction;

layout(location = 0) out vec4 out_color;

Ray compute_ray() {
	Ray ray;
	ray.position = u_camera_position / float(exp2(u_octree_depth)) + 1.f;
	ray.direction = normalize(v_ray_direction);
	// Get rid of small ray direction components to avoid division by zero.
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

uint compute_first_octant_index(const vec3 ray_direction) {
	uint first_octant_index = 0u;
	if (ray_direction.x < 0.f) {
		first_octant_index |= 1u;
	}
	if (ray_direction.y < 0.f) {
		first_octant_index |= 4u;
	}
	if (ray_direction.z < 0.f) {
		first_octant_index |= 2u;
	}
	return first_octant_index;
}

uint compute_octants_intesection_mask(const Ray ray, const vec3 node_min, const vec3 node_max) {
	const float half_extent = (node_max.x - node_min.x) / 2.f;
	const vec3 node_center = node_min + half_extent;

	const vec3 octant_separator_ts = (node_center - ray.position) * ray.direction_inverse;

	const vec3 half_extent_ray_lengths = half_extent * abs(ray.direction_inverse);
	const vec3 t_mins = octant_separator_ts - half_extent_ray_lengths;
	const vec3 t_maxs = octant_separator_ts + half_extent_ray_lengths;

	const float t_min = max(max(max(t_mins.x, t_mins.y), t_mins.z), 0.f);
	const float t_max = min(min(t_maxs.x, t_maxs.y), t_maxs.z);

	uint intersection_mask = 0u;

	// check intersections with the planes dividing the node into octants
	const uint LEFT_OCTANTS_BITS = 0x55; // 0b01010101
	const uint RIGHT_OCTANTS_BITS = 0xAA; // 0b10101010
	const uint FRONT_OCTANTS_BITS = 0x33; // 0b00110011
	const uint BACK_OCTANTS_BITS = 0xCC; // 0b11001100
	const uint DOWN_OCTANTS_BITS = 0x0F; // 0b00001111
	const uint UP_OCTANTS_BITS = 0xF0; // 0b11110000

	const vec3 x_separator_point = ray.position + octant_separator_ts.x * ray.direction;
	const uint a = x_separator_point.y < node_center.y ? DOWN_OCTANTS_BITS : UP_OCTANTS_BITS;
	const uint b = x_separator_point.z < node_center.z ? FRONT_OCTANTS_BITS : BACK_OCTANTS_BITS;
	intersection_mask |= (octant_separator_ts.x < t_min || t_max < octant_separator_ts.x) ? 0u : a & b;

	const vec3 y_separator_point = ray.position + octant_separator_ts.y * ray.direction;
	const uint c = y_separator_point.x < node_center.x ? LEFT_OCTANTS_BITS : RIGHT_OCTANTS_BITS;
	const uint d = y_separator_point.z < node_center.z ? FRONT_OCTANTS_BITS : BACK_OCTANTS_BITS;
	intersection_mask |= (octant_separator_ts.y < t_min || t_max < octant_separator_ts.y) ? 0u : c & d;

	const vec3 z_separator_point = ray.position + octant_separator_ts.z * ray.direction;
	const uint e = z_separator_point.x < node_center.x ? LEFT_OCTANTS_BITS : RIGHT_OCTANTS_BITS;
	const uint f = z_separator_point.y < node_center.y ? DOWN_OCTANTS_BITS : UP_OCTANTS_BITS;
	intersection_mask |= (octant_separator_ts.z < t_min || t_max < octant_separator_ts.z) ? 0u : e & f;

	// handle the case when the ray starts and ends inside a single octant
	const vec3 inside_point = ray.position + (t_min + t_max) / 2.f * ray.direction;
	uint inside_point_octant_bit = 1u;
	inside_point_octant_bit <<= inside_point.x < node_center.x ? 0u : 1u;
	inside_point_octant_bit <<= inside_point.y < node_center.y ? 0u : 4u;
	inside_point_octant_bit <<= inside_point.z < node_center.z ? 0u : 2u;
	intersection_mask |= (t_min > t_max) ? 0u : inside_point_octant_bit;

	return intersection_mask;
}

uint compute_current_octant_index(const OctreeNode node, inout StackElem stack_elem, const uint first_octant_index) {
	const uint octants_mask = octants_mask(node);
	while (stack_elem.checked_octant_count < 8u) {
		const uint current_octant_index = stack_elem.checked_octant_count ^ first_octant_index;
		if ((octants_mask & (1u << current_octant_index)) != 0u && (stack_elem.octants_intesection_mask & (1u << current_octant_index)) != 0u) {
			return current_octant_index;
		}
		stack_elem.checked_octant_count += 1u;
	}
	return 8u;
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

void main() {
	Ray ray = compute_ray();
	const uint first_octant_index = compute_first_octant_index(ray.direction);
	StackElem stack[MAX_OCTREE_DEPTH];
	stack[0] = StackElem(compute_octants_intesection_mask(ray, vec3(1.f), vec3(2.f)), 0u, 0u, vec3(1.f));
	uint stack_index = 0u;

	uint iter = 0u;
	while (iter < 1000u) {
		iter += 1u;

		const OctreeNode node = b_octree_nodes[stack[stack_index].node_index];
		const uint current_octant_index = compute_current_octant_index(node, stack[stack_index], first_octant_index);
		if (current_octant_index == 8u) {
			if (stack_index == 0u) {
				break;
			}
			stack_index -= 1u;
			stack[stack_index].checked_octant_count += 1u;
			continue;
		}
		const float octant_size = exp2(-float(stack_index + 1u));
		const vec3 octant_min = stack[stack_index].node_min + octant_size
			* vec3(float(current_octant_index & 1u), float((current_octant_index & 4u) >> 2u), float((current_octant_index & 2u) >> 1u));
		if (is_leaf(node)) {
			out_color = vec4(compute_color(ray, octant_min, octant_min + octant_size), 1.f);
			return;
		}
		stack_index += 1u;
		stack[stack_index] = StackElem(compute_octants_intesection_mask(ray, octant_min, octant_min + octant_size),
			0u, first_octant_node_index(node) + current_octant_index, octant_min);
	}
	out_color = vec4(0.f, 0.f, 0.f, 1.f);
}
