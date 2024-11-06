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

void main() {
	const uint AXIS_VOXELS_COUNT = 300u;
	const vec3 ray_direction = normalize(v_ray_direction);
	ivec3 coords = ivec3(round(u_camera_position));
	const ivec3 coords_steps = ivec3(round(ray_direction / abs(ray_direction)));
	const vec3 straight_distances = fract(-coords_steps * u_camera_position + 1.5f);
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
				color_factor = 0.875f;
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
				color_factor = 0.75f;
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
