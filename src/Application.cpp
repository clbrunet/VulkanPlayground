#include "Application.hpp"

#include <stdexcept>
#include <vector>
#include <iostream>
#include <algorithm>

Application::Application() {
	init_window();
	init_vulkan();
}

Application::~Application() {
	vkDestroyInstance(m_instance, nullptr);
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

void Application::run() {
	while (!glfwWindowShouldClose(m_window)) {
		glfwPollEvents();
	}
}

void Application::init_window() {
	glfwInit();

	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);

	auto constexpr WIDTH = 1280u;
	auto constexpr HEIGHT = 720u;
	m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Playground", nullptr, nullptr);
	if (!m_window) {
		throw std::runtime_error("glfwCreateWindow");
	}
}

void Application::init_vulkan() {
	create_instance();
}

void Application::create_instance() {
	auto application_info = VkApplicationInfo{};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "Vulkan Playground";
	application_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	application_info.pEngineName = "No Engine";
	application_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
	application_info.apiVersion = VK_API_VERSION_1_0;

	uint32_t glfw_extension_count;
	auto const glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
	auto extensions = std::vector<char const*>{ glfw_extensions, glfw_extensions + glfw_extension_count };
	extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

	auto instance_create_info = VkInstanceCreateInfo{};
	instance_create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	instance_create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	instance_create_info.pApplicationInfo = &application_info;
#if !NDEBUG
	auto constexpr VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
	if (has_instance_layer(VALIDATION_LAYER)) {
		instance_create_info.enabledLayerCount = 1;
		instance_create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif
	instance_create_info.enabledExtensionCount = std::size(extensions);
	instance_create_info.ppEnabledExtensionNames = std::data(extensions);

	if (vkCreateInstance(&instance_create_info, nullptr, &m_instance) != VK_SUCCESS) {
		throw std::runtime_error("vkCreateInstance");
	}
}

bool Application::has_instance_layer(std::string_view const layer_name) {
	uint32_t property_count;
	vkEnumerateInstanceLayerProperties(&property_count, nullptr);
	auto properties = std::vector<VkLayerProperties>(property_count);
	vkEnumerateInstanceLayerProperties(&property_count, std::data(properties));

	return std::any_of(std::cbegin(properties), std::cend(properties), [layer_name](VkLayerProperties const& property) {
		return layer_name.compare(property.layerName) == 0;
	});
}
