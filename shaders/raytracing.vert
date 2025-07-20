#version 450

#include "raytracing_push_contants.glsl"

layout(location = 0) out vec3 v_ray_direction;

void main() {
    float x = -1.f + float((gl_VertexIndex & 1) << 2);
    float y = -1.f + float((gl_VertexIndex & 2) << 1);
    gl_Position = vec4(x, y, 1.f, 1.f);
    v_ray_direction = u_camera_rotation * vec3(x * u_aspect_ratio, -y, 1.f);
}
