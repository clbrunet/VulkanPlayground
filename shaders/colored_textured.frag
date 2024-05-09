#version 450

layout(binding = 1) uniform sampler2D u_texture;

layout(location = 0) in vec2 v_texture_coords;
layout(location = 1) in vec3 v_color;

layout(location = 0) out vec4 out_color;

void main() {
	out_color = mix(texture(u_texture, v_texture_coords), vec4(v_color, 1.0), 0.4);
}
