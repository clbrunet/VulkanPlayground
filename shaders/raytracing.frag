#version 450

layout(std430, binding = 0) readonly buffer Voxels {
	bool b_voxels[];
};

layout(push_constant) uniform PushConstants {
	vec3 u_camera_position;
	float u_aspect_ratio;
	mat3 u_camera_rotation;
};

layout(location = 0) in vec3 v_ray_direction;

layout(location = 0) out vec4 out_color;

float ray_aabb_intersection(vec3 ray_position, vec3 ray_direction) {
	const vec3 aabb_min = vec3(0.f, 0.f, 0.f);
	const vec3 aabb_max = vec3(256.f, 256.f, 256.f);

	const vec3 t1 = (aabb_min - ray_position) / ray_direction;
	const vec3 t2 = (aabb_max - ray_position) / ray_direction;

	const vec3 mins = min(t1, t2);
	const vec3 maxs = max(t1, t2);

	const float tmin = max(max(mins.x, mins.y), mins.z);
	const float tmax = min(min(maxs.x, maxs.y), maxs.z);

	if (tmin > tmax || tmax < 0.f) {
		return -1.f;
	}
	return max(tmin, 0.f);
}

void main() {
	const uint AXIS_VOXELS_COUNT = 300u;
	const vec3 ray_direction = normalize(v_ray_direction);
	float t = ray_aabb_intersection(u_camera_position, ray_direction);
	if (t == -1.f) {
		out_color = vec4(0.f, 0.f, 0.f, 1.f);
		return;
	}
	vec3 ray_position = u_camera_position;
	if (t != 0.0f) {
		ray_position += (t - 0.5f) * ray_direction;
	}
	ivec3 coords = ivec3(floor(ray_position));
	const ivec3 coords_steps = ivec3(sign(ray_direction));

	const vec3 ray_position_fract = fract(ray_position);
	const vec3 straight_distances = mix(ray_position_fract, 1.f - ray_position_fract, vec3(coords_steps + 1) / 2.f);
	vec3 distances = vec3(
		length((straight_distances.x / ray_direction.x) * ray_direction),
		length((straight_distances.y / ray_direction.y) * ray_direction),
		length((straight_distances.z / ray_direction.z) * ray_direction)
	);
	const vec3 distances_steps = vec3(
		length((1.f / ray_direction.x) * ray_direction),
		length((1.f / ray_direction.y) * ray_direction),
		length((1.f / ray_direction.z) * ray_direction)
	);
	for (uint i = 0u; i < 500u; ++i) {
		float color_factor = 1.f;
		if (distances.x < distances.y) {
			if (distances.z < distances.x) {
				coords.z += coords_steps.z;
				distances.z += distances_steps.z;
			}
			else {
				coords.x += coords_steps.x;
				distances.x += distances_steps.x;
				color_factor = 0.8f;
			}
		}
		else {
			if (distances.z < distances.y) {
				coords.z += coords_steps.z;
				distances.z += distances_steps.z;
			}
			else {
				coords.y += coords_steps.y;
				distances.y += distances_steps.y;
				color_factor = 0.6f;
			}
		}
		if (coords.x >= 0 && coords.x < AXIS_VOXELS_COUNT && coords.y >= 0 && coords.y < AXIS_VOXELS_COUNT && coords.z >= 0 && coords.z < AXIS_VOXELS_COUNT
			&& b_voxels[coords.y * AXIS_VOXELS_COUNT * AXIS_VOXELS_COUNT + coords.z * AXIS_VOXELS_COUNT + coords.x]) {
			out_color = color_factor * vec4(1.f, 0.f, 0.f, 1.f);
			return;
		}
	}
	out_color = vec4(0.f, 0.f, 0.f, 1.f);
}
