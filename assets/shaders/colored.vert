#version 450

layout(binding = 0) uniform MvpUniformBufferObject {
	mat4 model;
	mat4 view;
	mat4 projection;
} u_mvp;

layout(location = 0) in vec3 in_position;
layout(location = 1) in vec3 in_color;

layout(location = 0) out vec3 v_color;

void main() {
	gl_Position = u_mvp.projection * u_mvp.view * u_mvp.model * vec4(in_position, 1.f);
	v_color = in_color;
}
