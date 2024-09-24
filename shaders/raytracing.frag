#version 450

layout(location = 0) in vec3 v_ray_direction;

layout(location = 0) out vec4 out_color;

const vec3 ray_origin = vec3(0.f);

float get_ray_sphere_intersection_coef(vec3 sphere_center, float sphere_radius) {
	// Quadratic equation coefficients
	float a = dot(v_ray_direction, v_ray_direction);
	float b = dot(2.f * ray_origin * v_ray_direction - 2.f * v_ray_direction * sphere_center, vec3(1.f));
	float c = dot(ray_origin * ray_origin - 2.f * ray_origin * sphere_center + sphere_center * sphere_center, vec3(1.f)) - pow(sphere_radius, 2.f);

	float discriminant = pow(b, 2.f) - 4.f * a * c;
	if (discriminant < 0.f) {
		return 0.f;
	}
	float sqrt_discriminant = sqrt(discriminant);
	float t1 = (-b - sqrt_discriminant) / (2.f * a);
	float t2 = (-b + sqrt_discriminant) / (2.f * a);
	return t1 <= 0.f ? t2 : t1;
}

void main() {
	vec3 sphere_centers[] = {
		vec3(0.f, 0.f, -4.f),
		vec3(2.f, -1.f, -7.f),
	};
	vec3 sphere_colors[] = {
		vec3(1.f, 0.f, 0.f),
		vec3(0.f, 1.f, 0.f),
	};

	const float BACKGROUND_COEF = 99999.f;
	float closest_coef = BACKGROUND_COEF;
	int index = -1;
	for (int i = 0; i < 2; ++i) {
		float t = get_ray_sphere_intersection_coef(sphere_centers[i], 1.2f);
		if (t > 0.f && t < closest_coef) {
			closest_coef = t;
			index = i;
		}
	}

	if (closest_coef == BACKGROUND_COEF) {
		out_color = vec4(0.f, 0.f, 0.f, 1.f);
		return;
	}

	vec3 point = ray_origin + closest_coef * v_ray_direction;
	vec3 light_direction = normalize(vec3(-1.f, -1.f, -1.f));
	float diffuse_coef = max(dot(normalize(sphere_centers[index] - point), light_direction), 0.2f);
	out_color = vec4(diffuse_coef * sphere_colors[index], 1.f);
}
