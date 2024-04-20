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

private:
	GLFWwindow* m_window;
	VkInstance m_instance;
};
