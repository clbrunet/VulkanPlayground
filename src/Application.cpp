#include "Application.hpp"

#include <stdexcept>
#include <vector>
#include <iostream>
#include <algorithm>

auto constexpr VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

Application::Application() {
	init_window();
	init_vulkan();
}

Application::~Application() {
	vkDestroyDevice(m_device, nullptr);
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
		throw std::runtime_error{ "glfwCreateWindow" };
	}
}

void Application::init_vulkan() {
	create_instance();
	select_physical_device();
	create_device();
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

	auto create_info = VkInstanceCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	create_info.pApplicationInfo = &application_info;
#if !NDEBUG
	if (has_instance_layer(VALIDATION_LAYER)) {
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif
	create_info.enabledExtensionCount = std::size(extensions);
	create_info.ppEnabledExtensionNames = std::data(extensions);

	if (vkCreateInstance(&create_info, nullptr, &m_instance) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateInstance" };
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

void Application::select_physical_device() {
	uint32_t physical_device_count;
	vkEnumeratePhysicalDevices(m_instance, &physical_device_count, nullptr);
	auto physical_devices = std::vector<VkPhysicalDevice>(physical_device_count);
	vkEnumeratePhysicalDevices(m_instance, &physical_device_count, std::data(physical_devices));

	auto best_score = 0u;
	for (auto const physical_device : physical_devices) {
		int score = get_physical_device_score(physical_device);
		if (score > best_score) {
			best_score = score;
			m_physical_device = physical_device;
		}
	}
	if (best_score == 0u) {
		throw std::runtime_error{ "no suitable GPU found" };
	}
}

uint32_t Application::get_physical_device_score(VkPhysicalDevice physical_device) {
	uint32_t queue_family_property_count;
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_property_count, nullptr);
	auto queue_family_properties = std::vector<VkQueueFamilyProperties>(queue_family_property_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_property_count, std::data(queue_family_properties));

	for (auto const& queue_family_property : queue_family_properties) {
		if (queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			return 1u;
		}
	}
	return 0u;
}

void Application::create_device() {
	auto queue_family_indices = get_queue_family_indices();
	float queue_priority = 1.f;
	auto queue_create_info = VkDeviceQueueCreateInfo{};
	queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
	queue_create_info.queueFamilyIndex = queue_family_indices.graphics;
	queue_create_info.queueCount = 1u;
	queue_create_info.pQueuePriorities = &queue_priority;

	auto features = VkPhysicalDeviceFeatures{};

	auto create_info = VkDeviceCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create_info.queueCreateInfoCount = 1u;
	create_info.pQueueCreateInfos = &queue_create_info;
	create_info.pEnabledFeatures = &features;
#if !NDEBUG
	if (has_device_layer(m_physical_device, VALIDATION_LAYER)) {
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif

	if (vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateDevice" };
	}
	vkGetDeviceQueue(m_device, queue_family_indices.graphics, 0, &m_graphics_queue);
}

Application::QueueFamilyIndices Application::get_queue_family_indices() {
	uint32_t queue_family_property_count;
	vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_property_count, nullptr);
	auto queue_family_properties = std::vector<VkQueueFamilyProperties>(queue_family_property_count);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_property_count, std::data(queue_family_properties));

	auto indices = QueueFamilyIndices{};
	auto index = 0u;
	for (auto const& queue_family_property : queue_family_properties) {
		if (queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphics = index;
		}
		index++;
	}
	return indices;
}

bool Application::has_device_layer(VkPhysicalDevice const physical_device, std::string_view const layer_name) {
	uint32_t property_count;
	vkEnumerateDeviceLayerProperties(physical_device, &property_count, nullptr);
	auto properties = std::vector<VkLayerProperties>(property_count);
	vkEnumerateDeviceLayerProperties(physical_device, &property_count, std::data(properties));

	return std::any_of(std::cbegin(properties), std::cend(properties), [layer_name](VkLayerProperties const& property) {
		return layer_name.compare(property.layerName) == 0;
	});
}
