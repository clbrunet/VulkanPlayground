#version 450
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require

struct OctreeNode {//CBTODO names
	uint bits[3];
};

bool is_leaf(OctreeNode node) {
	return (node.bits[2] & 1u) == 1u;
}

uint64_t octants_mask(OctreeNode node) {
	return (uint64_t(node.bits[1]) << 32ul) | node.bits[0];//CBTODO faire gaffe a lendianness pour ca
}

uint first_octant_node_index(OctreeNode node) {
	return node.bits[2] >> 1u;
}

const uint MAX_OCTREE_DEPTH = 8u;

struct StackElem {
	uint64_t octants_intesection_mask;
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
	ray.position = u_camera_position / float(exp2(u_octree_depth * 2u)) + 1.f;
	ray.direction = normalize(v_ray_direction);
	// avoid division by zero
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
		first_octant_index |= 3u;
	}
	if (ray_direction.y < 0.f) {
		first_octant_index |= 48u;
	}
	if (ray_direction.z < 0.f) {
		first_octant_index |= 12u;
	}
	return first_octant_index;
}

//CBTODO refaire ca pour les 9 planes
uint64_t compute_octants_intesection_mask(const Ray ray, const vec3 node_min, const vec3 node_max) {
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

	//CBTODO rename (everywhere) "octant" (chercher "oct" partout)
	// check intersections with the planes dividing the node into octants
	const uint64_t FAR_LEFT_OCTANTS_MASK = 0x1111111111111111ul; //     0b0001000100010001000100010001000100010001000100010001000100010001ul
	const uint64_t CENTER_LEFT_OCTANTS_MASK = 0x2222222222222222ul; //  0b0010001000100010001000100010001000100010001000100010001000100010ul
	const uint64_t CENTER_RIGHT_OCTANTS_MASK = 0x4444444444444444ul; // 0b0100010001000100010001000100010001000100010001000100010001000100ul
	const uint64_t FAR_RIGHT_OCTANTS_MASK = 0x8888888888888888ul;//     0b1000100010001000100010001000100010001000100010001000100010001000ul
	const uint64_t FAR_FRONT_OCTANTS_MASK = 0x000F000F000F000Ful; //    0b0000000000001111000000000000111100000000000011110000000000001111ul
	const uint64_t CENTER_FRONT_OCTANTS_MASK = 0x00F000F000F000F0ul; // 0b0000000011110000000000001111000000000000111100000000000011110000ul
	const uint64_t CENTER_BACK_OCTANTS_MASK = 0x0F000F000F000F00ul; //  0b0000111100000000000011110000000000001111000000000000111100000000ul
	const uint64_t FAR_BACK_OCTANTS_MASK = 0xF000F000F000F000ul; //     0b1111000000000000111100000000000011110000000000001111000000000000ul
	const uint64_t FAR_DOWN_OCTANTS_MASK = 0x000000000000FFFFul; //     0b0000000000000000000000000000000000000000000000001111111111111111ul
	const uint64_t CENTER_DOWN_OCTANTS_MASK = 0x00000000FFFF0000ul; //  0b0000000000000000000000000000000011111111111111110000000000000000ul
	const uint64_t CENTER_UP_OCTANTS_MASK = 0x0000FFFF00000000ul; //    0b0000000000000000111111111111111100000000000000000000000000000000ul
	const uint64_t FAR_UP_OCTANTS_MASK = 0xFFFF000000000000ul; //       0b1111111111111111000000000000000000000000000000000000000000000000ul

	// q1 separator
	{//CBTODO cleaner tout ca (tabs, var names etc)
	const vec3 q1_x_separator_point = ray.position + q1_separator_ts.x * ray.direction;
	const uint64_t a = q1_x_separator_point.y < node_q1.y ? FAR_DOWN_OCTANTS_MASK
		: q1_x_separator_point.y < node_center.y ? CENTER_DOWN_OCTANTS_MASK
		: q1_x_separator_point.y < node_q3.y ? CENTER_UP_OCTANTS_MASK : FAR_UP_OCTANTS_MASK;
	const uint64_t b = q1_x_separator_point.z < node_q1.z ? FAR_FRONT_OCTANTS_MASK
		: q1_x_separator_point.z < node_center.z ? CENTER_FRONT_OCTANTS_MASK
		: q1_x_separator_point.z < node_q3.z ? CENTER_BACK_OCTANTS_MASK : FAR_BACK_OCTANTS_MASK;
	intersection_mask |= (q1_separator_ts.x < t_min || t_max < q1_separator_ts.x) ? 0ul : (FAR_LEFT_OCTANTS_MASK | CENTER_LEFT_OCTANTS_MASK) & a & b;

	const vec3 q1_z_separator_point = ray.position + q1_separator_ts.z * ray.direction;
	const uint64_t c = q1_z_separator_point.x < node_q1.x ? FAR_LEFT_OCTANTS_MASK
		: q1_z_separator_point.x < node_center.x ? CENTER_LEFT_OCTANTS_MASK
		: q1_z_separator_point.x < node_q3.x ? CENTER_RIGHT_OCTANTS_MASK : FAR_RIGHT_OCTANTS_MASK;
	const uint64_t d = q1_z_separator_point.y < node_q1.y ? FAR_DOWN_OCTANTS_MASK
		: q1_z_separator_point.y < node_center.y ? CENTER_DOWN_OCTANTS_MASK
		: q1_z_separator_point.y < node_q3.y ? CENTER_UP_OCTANTS_MASK : FAR_UP_OCTANTS_MASK;
	intersection_mask |= (q1_separator_ts.z < t_min || t_max < q1_separator_ts.z) ? 0ul : (FAR_FRONT_OCTANTS_MASK | CENTER_FRONT_OCTANTS_MASK) & c & d;

	const vec3 q1_y_separator_point = ray.position + q1_separator_ts.y * ray.direction;
	const uint64_t e = q1_y_separator_point.x < node_q1.x ? FAR_LEFT_OCTANTS_MASK
		: q1_y_separator_point.x < node_center.x ? CENTER_LEFT_OCTANTS_MASK
		: q1_y_separator_point.x < node_q3.x ? CENTER_RIGHT_OCTANTS_MASK : FAR_RIGHT_OCTANTS_MASK;
	const uint64_t f = q1_y_separator_point.z < node_q1.z ? FAR_FRONT_OCTANTS_MASK
		: q1_y_separator_point.z < node_center.z ? CENTER_FRONT_OCTANTS_MASK
		: q1_y_separator_point.z < node_q3.z ? CENTER_BACK_OCTANTS_MASK : FAR_BACK_OCTANTS_MASK;
	intersection_mask |= (q1_separator_ts.y < t_min || t_max < q1_separator_ts.y) ? 0ul : (FAR_DOWN_OCTANTS_MASK | CENTER_DOWN_OCTANTS_MASK) & e & f;
	}

	// center separator
	{
	const vec3 center_x_separator_point = ray.position + center_separator_ts.x * ray.direction;
	const uint64_t a = center_x_separator_point.y < node_q1.y ? FAR_DOWN_OCTANTS_MASK
		: center_x_separator_point.y < node_center.y ? CENTER_DOWN_OCTANTS_MASK
		: center_x_separator_point.y < node_q3.y ? CENTER_UP_OCTANTS_MASK : FAR_UP_OCTANTS_MASK;
	const uint64_t b = center_x_separator_point.z < node_q1.z ? FAR_FRONT_OCTANTS_MASK
		: center_x_separator_point.z < node_center.z ? CENTER_FRONT_OCTANTS_MASK
		: center_x_separator_point.z < node_q3.z ? CENTER_BACK_OCTANTS_MASK : FAR_BACK_OCTANTS_MASK;
	intersection_mask |= (center_separator_ts.x < t_min || t_max < center_separator_ts.x) ? 0ul : (CENTER_LEFT_OCTANTS_MASK | CENTER_RIGHT_OCTANTS_MASK) & a & b;

	const vec3 center_z_separator_point = ray.position + center_separator_ts.z * ray.direction;
	const uint64_t c = center_z_separator_point.x < node_q1.x ? FAR_LEFT_OCTANTS_MASK
		: center_z_separator_point.x < node_center.x ? CENTER_LEFT_OCTANTS_MASK
		: center_z_separator_point.x < node_q3.x ? CENTER_RIGHT_OCTANTS_MASK : FAR_RIGHT_OCTANTS_MASK;
	const uint64_t d = center_z_separator_point.y < node_q1.y ? FAR_DOWN_OCTANTS_MASK
		: center_z_separator_point.y < node_center.y ? CENTER_DOWN_OCTANTS_MASK
		: center_z_separator_point.y < node_q3.y ? CENTER_UP_OCTANTS_MASK : FAR_UP_OCTANTS_MASK;
	intersection_mask |= (center_separator_ts.z < t_min || t_max < center_separator_ts.z) ? 0ul : (CENTER_FRONT_OCTANTS_MASK | CENTER_BACK_OCTANTS_MASK) & c & d;

	const vec3 center_y_separator_point = ray.position + center_separator_ts.y * ray.direction;
	const uint64_t e = center_y_separator_point.x < node_q1.x ? FAR_LEFT_OCTANTS_MASK
		: center_y_separator_point.x < node_center.x ? CENTER_LEFT_OCTANTS_MASK
		: center_y_separator_point.x < node_q3.x ? CENTER_RIGHT_OCTANTS_MASK : FAR_RIGHT_OCTANTS_MASK;
	const uint64_t f = center_y_separator_point.z < node_q1.z ? FAR_FRONT_OCTANTS_MASK
		: center_y_separator_point.z < node_center.z ? CENTER_FRONT_OCTANTS_MASK
		: center_y_separator_point.z < node_q3.z ? CENTER_BACK_OCTANTS_MASK : FAR_BACK_OCTANTS_MASK;
	intersection_mask |= (center_separator_ts.y < t_min || t_max < center_separator_ts.y) ? 0ul : (CENTER_DOWN_OCTANTS_MASK | CENTER_UP_OCTANTS_MASK) & e & f;
	}

	// q3 separator
	{
	const vec3 q3_x_separator_point = ray.position + q3_separator_ts.x * ray.direction;
	const uint64_t a = q3_x_separator_point.y < node_q1.y ? FAR_DOWN_OCTANTS_MASK
		: q3_x_separator_point.y < node_center.y ? CENTER_DOWN_OCTANTS_MASK
		: q3_x_separator_point.y < node_q3.y ? CENTER_UP_OCTANTS_MASK : FAR_UP_OCTANTS_MASK;
	const uint64_t b = q3_x_separator_point.z < node_q1.z ? FAR_FRONT_OCTANTS_MASK
		: q3_x_separator_point.z < node_center.z ? CENTER_FRONT_OCTANTS_MASK
		: q3_x_separator_point.z < node_q3.z ? CENTER_BACK_OCTANTS_MASK : FAR_BACK_OCTANTS_MASK;
	intersection_mask |= (q3_separator_ts.x < t_min || t_max < q3_separator_ts.x) ? 0ul : (CENTER_RIGHT_OCTANTS_MASK | FAR_RIGHT_OCTANTS_MASK) & a & b;

	const vec3 q3_z_separator_point = ray.position + q3_separator_ts.z * ray.direction;
	const uint64_t c = q3_z_separator_point.x < node_q1.x ? FAR_LEFT_OCTANTS_MASK
		: q3_z_separator_point.x < node_center.x ? CENTER_LEFT_OCTANTS_MASK
		: q3_z_separator_point.x < node_q3.x ? CENTER_RIGHT_OCTANTS_MASK : FAR_RIGHT_OCTANTS_MASK;
	const uint64_t d = q3_z_separator_point.y < node_q1.y ? FAR_DOWN_OCTANTS_MASK
		: q3_z_separator_point.y < node_center.y ? CENTER_DOWN_OCTANTS_MASK
		: q3_z_separator_point.y < node_q3.y ? CENTER_UP_OCTANTS_MASK : FAR_UP_OCTANTS_MASK;
	intersection_mask |= (q3_separator_ts.z < t_min || t_max < q3_separator_ts.z) ? 0ul : (CENTER_BACK_OCTANTS_MASK | FAR_BACK_OCTANTS_MASK) & c & d;

	const vec3 q3_y_separator_point = ray.position + q3_separator_ts.y * ray.direction;
	const uint64_t e = q3_y_separator_point.x < node_q1.x ? FAR_LEFT_OCTANTS_MASK
		: q3_y_separator_point.x < node_center.x ? CENTER_LEFT_OCTANTS_MASK
		: q3_y_separator_point.x < node_q3.x ? CENTER_RIGHT_OCTANTS_MASK : FAR_RIGHT_OCTANTS_MASK;
	const uint64_t f = q3_y_separator_point.z < node_q1.z ? FAR_FRONT_OCTANTS_MASK
		: q3_y_separator_point.z < node_center.z ? CENTER_FRONT_OCTANTS_MASK
		: q3_y_separator_point.z < node_q3.z ? CENTER_BACK_OCTANTS_MASK : FAR_BACK_OCTANTS_MASK;
	intersection_mask |= (q3_separator_ts.y < t_min || t_max < q3_separator_ts.y) ? 0ul : (CENTER_UP_OCTANTS_MASK | FAR_UP_OCTANTS_MASK) & e & f;
	}

	// handle the case when the ray starts and ends inside a single octant
	const vec3 inside_point = ray.position + (t_min + t_max) / 2.f * ray.direction;
	uint64_t inside_point_octant_bit = 1ul;
	inside_point_octant_bit <<= inside_point.x < node_q1.x ? 0ul
		: inside_point.x < node_center.x ? 1ul
		: inside_point.x < node_q3.x ? 2ul : 3ul;
	inside_point_octant_bit <<= inside_point.y < node_q1.y ? 0ul
		: inside_point.y < node_center.y ? 16ul
		: inside_point.y < node_q3.y ? 32ul : 48ul;
	inside_point_octant_bit <<= inside_point.z < node_q1.z ? 0ul
		: inside_point.z < node_center.z ? 4ul
		: inside_point.z < node_q3.z ? 8ul : 12ul;
	intersection_mask |= (t_min > t_max) ? 0ul : inside_point_octant_bit;

	return intersection_mask;
}

uint compute_current_octant_index(const OctreeNode node, inout StackElem stack_elem, const uint first_octant_index) {
	const uint64_t octants_mask = octants_mask(node);
	while (stack_elem.checked_octant_count < 64u) {
		const uint64_t current_octant_index = stack_elem.checked_octant_count ^ first_octant_index;
		if ((octants_mask & (1ul << current_octant_index)) != 0ul && (stack_elem.octants_intesection_mask & (1ul << current_octant_index)) != 0ul) {
			return uint(current_octant_index);
		}
		stack_elem.checked_octant_count += 1u;
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
		if (current_octant_index == 64u) {
			if (stack_index == 0u) {
				if (stack[0].octants_intesection_mask == 0ul) {//CBTODO virer ce if
					out_color = vec4(1.f, 1.f, 1.f, 1.f);
					return;
				}
				//out_color = vec4(vec3(float(iter) / 300.f), 1.f); return; // CBTODO
				break;
			}
			stack_index -= 1u;
			stack[stack_index].checked_octant_count += 1u;
			continue;
		}
		vec3 octant_min = stack[stack_index].node_min;
		float octant_size = exp2(-float(2u * stack_index + 1u));
		octant_min += octant_size * vec3(float((current_octant_index & 2u) >> 1u), float((current_octant_index & 32u) >> 5u), float((current_octant_index & 8u) >> 3u));
		octant_size /= 2.f;
		octant_min += octant_size * vec3(float(current_octant_index & 1u), float((current_octant_index & 16u) >> 4u), float((current_octant_index & 4u) >> 2u));
		if (is_leaf(node)) {
			out_color = vec4(compute_color(ray, octant_min, octant_min + octant_size), 1.f);
			//out_color = vec4(vec3(float(iter) / 300.f), 1.f); // CBTODO
			return;
		}
		stack_index += 1u;
		//CBTODO endianness //CBTODO mettre dans une fonction pour ne jamais acceder a bits
		const uint low_mask_offset = current_octant_index == 0u ? 0u : bitCount(node.bits[0] << (32u - min(current_octant_index, 32u)));
		const uint high_mask_offset = current_octant_index <= 32u ? 0u : bitCount(node.bits[1] << (32u - (current_octant_index - 32u)));
		const uint octant_node_offset = low_mask_offset + high_mask_offset;
		stack[stack_index] = StackElem(compute_octants_intesection_mask(ray, octant_min, octant_min + octant_size),
			0u, first_octant_node_index(node) + octant_node_offset, octant_min);
	}
	out_color = vec4(0.f, 0.f, 0.f, 1.f);
}
