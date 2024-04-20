#pragma once

#include <GLFW/glfw3.h>

#include <string_view>

class Application {
public:
	Application();
	~Application();

	void run();

private:
	void init_window();
	void init_vulkan();

	void create_instance();
	bool has_instance_layer(std::string_view layer_name);

	void select_physical_device();
	uint32_t get_physical_device_score(VkPhysicalDevice physical_device);

	struct QueueFamilyIndices {
		uint32_t graphics;
	};
	void create_device();
	QueueFamilyIndices get_queue_family_indices();
	bool has_device_layer(VkPhysicalDevice physical_device, std::string_view const layer_name);

private:
	GLFWwindow* m_window;
	VkInstance m_instance;
	VkPhysicalDevice m_physical_device;
	VkDevice m_device;
	VkQueue m_graphics_queue;
};
