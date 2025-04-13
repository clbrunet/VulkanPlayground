#version 450

layout(push_constant) uniform PushConstants {
	vec3 u_camera_position;
	float u_aspect_ratio;
	mat3 u_camera_rotation;
	uint u_tree64_depth;
};

layout(location = 0) out vec3 v_ray;

void main() {
	float x = -1.f + float((gl_VertexIndex & 1) << 2);
	float y = -1.f + float((gl_VertexIndex & 2) << 1);
	gl_Position = vec4(x, y, 1.f, 1.f);
	v_ray = u_camera_rotation * vec3(x * u_aspect_ratio, -y, 1.f);
}
