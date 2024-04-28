#include "Application.hpp"

#include <iostream>
#include <stdexcept>
#include <array>
#include <vector>
#include <set>
#include <algorithm>
#include <limits>
#include <functional>
#include <fstream>
#include <optional>

auto constexpr VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";

Application::Application() {
	init_window();
	init_vulkan();
}

Application::~Application() {
	for (auto const image_view : m_image_views) {\
		vkDestroyImageView(m_device, image_view, nullptr);
	}
	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
	vkDestroyDevice(m_device, nullptr);
	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
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
	create_surface();
	select_physical_device();
	create_device();
	create_swapchain();
	create_image_views();
	create_graphics_pipeline();
}

void Application::create_instance() {
	auto application_info = VkApplicationInfo{};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "Vulkan Playground";
	application_info.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
	application_info.pEngineName = "No Engine";
	application_info.engineVersion = VK_MAKE_VERSION(0, 1, 0);
	application_info.apiVersion = VK_API_VERSION_1_0;

	auto glfw_extension_count = uint32_t{};
	auto const glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
	auto extensions = std::vector<char const*>{ glfw_extensions, glfw_extensions + glfw_extension_count };
	extensions.emplace_back(VK_KHR_PORTABILITY_ENUMERATION_EXTENSION_NAME);

	auto create_info = VkInstanceCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
	create_info.flags = VK_INSTANCE_CREATE_ENUMERATE_PORTABILITY_BIT_KHR;
	create_info.pApplicationInfo = &application_info;
#ifndef NDEBUG
	if (has_instance_layer(VALIDATION_LAYER)) {
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif
	create_info.enabledExtensionCount = static_cast<uint32_t>(std::size(extensions));
	create_info.ppEnabledExtensionNames = std::data(extensions);

	if (vkCreateInstance(&create_info, nullptr, &m_instance) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateInstance" };
	}
}

bool Application::has_instance_layer(std::string_view const layer_name) {
	auto property_count = uint32_t{};
	vkEnumerateInstanceLayerProperties(&property_count, nullptr);
	auto properties = std::vector<VkLayerProperties>(property_count);
	vkEnumerateInstanceLayerProperties(&property_count, std::data(properties));

	return std::any_of(std::cbegin(properties), std::cend(properties), [layer_name](VkLayerProperties const& property) {
		return layer_name.compare(property.layerName) == 0;
	});
}

void Application::create_surface() {
	if (glfwCreateWindowSurface(m_instance, m_window, nullptr, &m_surface) != VK_SUCCESS) {
		throw std::runtime_error{ "glfwCreateWindowSurface" };
	}
}

void Application::select_physical_device() {
	auto physical_device_count = uint32_t{};
	vkEnumeratePhysicalDevices(m_instance, &physical_device_count, nullptr);
	auto physical_devices = std::vector<VkPhysicalDevice>(physical_device_count);
	vkEnumeratePhysicalDevices(m_instance, &physical_device_count, std::data(physical_devices));

	auto best_score = 0u;
	for (auto const physical_device : physical_devices) {
		auto const score = get_physical_device_score(physical_device);
		if (score > best_score) {
			best_score = score;
			m_physical_device = physical_device;
		}
	}
	if (best_score == 0u) {
		throw std::runtime_error{ "no suitable GPU found" };
	}
}

uint32_t Application::get_physical_device_score(VkPhysicalDevice physical_device) const {
	auto queue_family_property_count = uint32_t{};
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_property_count, nullptr);
	auto queue_family_properties = std::vector<VkQueueFamilyProperties>(queue_family_property_count);
	vkGetPhysicalDeviceQueueFamilyProperties(physical_device, &queue_family_property_count, std::data(queue_family_properties));

	bool has_graphics_queue = false;
	bool has_present_support = false;
	auto index = 0u;
	for (auto const& queue_family_property : queue_family_properties) {
		if (queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			has_graphics_queue = true;
		}
		VkBool32 family_present_support = false;
		vkGetPhysicalDeviceSurfaceSupportKHR(physical_device, index, m_surface, &family_present_support);
		if (family_present_support) {
			has_present_support = true;
		}
		index++;
	}
	if (!has_graphics_queue || !has_present_support
		|| !has_device_extension(physical_device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
		return 0u;
	}
	return 1u;
}

void Application::create_device() {
	auto queue_family_indices = get_queue_family_indices();
	auto const unique_queue_family_indices = std::set<uint32_t>{
		queue_family_indices.graphics,
		queue_family_indices.present
	};
	auto const queue_priority = 1.f;
	auto queue_create_infos = std::vector<VkDeviceQueueCreateInfo>{};
	std::transform(std::cbegin(unique_queue_family_indices), std::cend(unique_queue_family_indices),
		std::back_inserter(queue_create_infos), [&queue_priority](uint32_t const queue_family_index) {
		auto queue_create_info = VkDeviceQueueCreateInfo{};
		queue_create_info.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
		queue_create_info.queueFamilyIndex = queue_family_index;
		queue_create_info.queueCount = 1u;
		queue_create_info.pQueuePriorities = &queue_priority;
		return queue_create_info;
	});

	auto features = VkPhysicalDeviceFeatures{};

	auto extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;

	auto create_info = VkDeviceCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
	create_info.queueCreateInfoCount = static_cast<uint32_t>(std::size(queue_create_infos));
	create_info.pQueueCreateInfos = std::data(queue_create_infos);
	create_info.pEnabledFeatures = &features;
#ifndef NDEBUG
	if (has_device_layer(m_physical_device, VALIDATION_LAYER)) {
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif
	create_info.enabledExtensionCount = 1u;
	create_info.ppEnabledExtensionNames = &extension;

	if (vkCreateDevice(m_physical_device, &create_info, nullptr, &m_device) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateDevice" };
	}
	vkGetDeviceQueue(m_device, queue_family_indices.graphics, 0, &m_graphics_queue);
	vkGetDeviceQueue(m_device, queue_family_indices.graphics, 0, &m_present_queue);
}

Application::QueueFamilyIndices Application::get_queue_family_indices() const {
	auto queue_family_property_count = uint32_t{};
	vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_property_count, nullptr);
	auto queue_family_properties = std::vector<VkQueueFamilyProperties>(queue_family_property_count);
	vkGetPhysicalDeviceQueueFamilyProperties(m_physical_device, &queue_family_property_count, std::data(queue_family_properties));

	auto indices = QueueFamilyIndices{};
	auto index = 0u;
	for (auto const& queue_family_property : queue_family_properties) {
		if (queue_family_property.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
			indices.graphics = index;
		}
		auto family_present_support = VkBool32{};
		vkGetPhysicalDeviceSurfaceSupportKHR(m_physical_device, index, m_surface, &family_present_support);
		if (family_present_support) {
			indices.present = index;
		}
		index++;
	}
	return indices;
}

bool Application::has_device_layer(VkPhysicalDevice const physical_device, std::string_view const layer_name) {
	auto property_count = uint32_t{};
	vkEnumerateDeviceLayerProperties(physical_device, &property_count, nullptr);
	auto properties = std::vector<VkLayerProperties>(property_count);
	vkEnumerateDeviceLayerProperties(physical_device, &property_count, std::data(properties));

	return std::any_of(std::cbegin(properties), std::cend(properties), [layer_name](VkLayerProperties const& property) {
		return layer_name.compare(property.layerName) == 0;
	});
}

bool Application::has_device_extension(VkPhysicalDevice physical_device, std::string_view const extension_name) {
	auto property_count = uint32_t{};
	vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &property_count, nullptr);
	auto properties = std::vector<VkExtensionProperties>(property_count);
	vkEnumerateDeviceExtensionProperties(physical_device, nullptr, &property_count, std::data(properties));

	return std::any_of(std::cbegin(properties), std::cend(properties), [extension_name](VkExtensionProperties const& property) {
		return extension_name.compare(property.extensionName) == 0;
	});
}

void Application::create_swapchain() {
	auto surface_capabilities = VkSurfaceCapabilitiesKHR{};
	vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physical_device, m_surface, &surface_capabilities);

	auto min_image_count = surface_capabilities.minImageCount + 1;
	if (surface_capabilities.maxImageCount > 0) {
		min_image_count = std::min(min_image_count, surface_capabilities.maxImageCount);
	}

	auto surface_format_count = uint32_t{};
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &surface_format_count, nullptr);
	auto surface_formats = std::vector<VkSurfaceFormatKHR>{ surface_format_count };
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &surface_format_count, std::data(surface_formats));
	auto surface_format_it = std::find_if(std::cbegin(surface_formats), std::cend(surface_formats), [](VkSurfaceFormatKHR surface_format) {
		return surface_format.format == VK_FORMAT_B8G8R8A8_SRGB && surface_format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
	});
	if (surface_format_it == std::cend(surface_formats)) {
		surface_format_it = std::cbegin(surface_formats);
	}

	auto const image_extent = std::invoke([&]() {
		if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return surface_capabilities.currentExtent;
		}
		auto width = int{};
		auto height = int{};
		glfwGetFramebufferSize(m_window, &width, &height);
		return VkExtent2D{
			.width = std::clamp(static_cast<uint32_t>(width), surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width),
			.height = std::clamp(static_cast<uint32_t>(height), surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height),
		};
	});

	auto const physical_device_queue_family_indices = get_queue_family_indices();
	auto const queue_family_indices = std::array<uint32_t, 2>{{
			physical_device_queue_family_indices.graphics,
				physical_device_queue_family_indices.present
		}};
	auto const [sharing_mode, queue_family_index_count] = std::invoke([&]() {
		if (m_graphics_queue != m_present_queue) {
			return std::tuple<VkSharingMode, uint32_t>{
				VK_SHARING_MODE_CONCURRENT, 2
			};
		}
		return std::tuple<VkSharingMode, uint32_t>{
			VK_SHARING_MODE_EXCLUSIVE, 0
		};
	});

	auto create_info = VkSwapchainCreateInfoKHR{};
	create_info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
	create_info.surface = m_surface;
	create_info.minImageCount = min_image_count;
	create_info.imageFormat = surface_format_it->format;
	create_info.imageColorSpace = surface_format_it->colorSpace;
	create_info.imageExtent = image_extent;
	create_info.imageArrayLayers = 1u;
	create_info.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
	create_info.imageSharingMode = sharing_mode;
	create_info.queueFamilyIndexCount = queue_family_index_count;
	create_info.pQueueFamilyIndices = std::data(queue_family_indices);
	create_info.preTransform = surface_capabilities.currentTransform;
	create_info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
	create_info.presentMode = VK_PRESENT_MODE_FIFO_KHR;
	create_info.clipped = VK_TRUE;
	create_info.oldSwapchain = VK_NULL_HANDLE;
	if (vkCreateSwapchainKHR(m_device, &create_info, nullptr, &m_swapchain) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateSwapchainKHR" };
	}
	auto image_count = uint32_t{};
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, nullptr);
	m_swapchain_images.resize(image_count);
	vkGetSwapchainImagesKHR(m_device, m_swapchain, &image_count, std::data(m_swapchain_images));
	m_swapchain_format = surface_format_it->format;
	m_swapchain_extent = image_extent;
}

void Application::create_image_views() {
	std::transform(std::cbegin(m_swapchain_images), std::cend(m_swapchain_images),
		std::back_inserter(m_image_views), [&](VkImage const swapchain_image) {
		auto create_info = VkImageViewCreateInfo{};
		create_info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
		create_info.image = swapchain_image;
		create_info.viewType = VK_IMAGE_VIEW_TYPE_2D;
		create_info.format = m_swapchain_format;
		create_info.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
		create_info.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		create_info.subresourceRange.baseMipLevel = 0;
		create_info.subresourceRange.levelCount = 1;
		create_info.subresourceRange.baseArrayLayer = 0;
		create_info.subresourceRange.layerCount = 1;
		auto image_view = VkImageView{};
		if (vkCreateImageView(m_device, &create_info, nullptr, &image_view) != VK_SUCCESS) {
			throw std::runtime_error{ "vkCreateImageView" };
		}
		return image_view;
	});
}

void Application::create_graphics_pipeline() {
	auto const vertex_shader_module = create_shader_module("triangle.vert");
	auto const fragment_shader_module = create_shader_module("triangle.frag");

	auto vertex_shader_stage_create_info = VkPipelineShaderStageCreateInfo{};
	vertex_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	vertex_shader_stage_create_info.stage = VK_SHADER_STAGE_VERTEX_BIT;
	vertex_shader_stage_create_info.module = vertex_shader_module;
	vertex_shader_stage_create_info.pName = "main";

	auto fragment_shader_stage_create_info = VkPipelineShaderStageCreateInfo{};
	fragment_shader_stage_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
	fragment_shader_stage_create_info.stage = VK_SHADER_STAGE_FRAGMENT_BIT;
	fragment_shader_stage_create_info.module = fragment_shader_module;
	fragment_shader_stage_create_info.pName = "main";

	auto shader_stages = std::array<VkPipelineShaderStageCreateInfo, 2>{
		vertex_shader_stage_create_info,
		fragment_shader_stage_create_info
	};

	vkDestroyShaderModule(m_device, vertex_shader_module, nullptr);
	vkDestroyShaderModule(m_device, fragment_shader_module, nullptr);
}

static std::string get_spirv_shader_path(std::string shader) {
	return SPIRV_SHADERS_DIRECTORY + ("/" + std::move(shader) + ".spv");
}

static std::optional<std::vector<uint8_t>> read_binary_file(std::string const& path) {
	auto file = std::ifstream{ path, std::ios::ate | std::ios::binary };
	if (!file) {
		return std::nullopt;
	}
	auto bytes = std::vector<uint8_t>(file.tellg());
	file.seekg(0);
	file.read(reinterpret_cast<char*>(std::data(bytes)), std::size(bytes));
	return std::make_optional(std::move(bytes));
}

VkShaderModule Application::create_shader_module(std::string shader) const {
	auto const spirv_path = get_spirv_shader_path(std::move(shader));
	auto const code = read_binary_file(spirv_path);
	if (!code) {
		throw std::runtime_error{ "cannot read \"" + spirv_path + '"' };
	}
	auto create_info = VkShaderModuleCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
	create_info.codeSize = std::size(*code);
	create_info.pCode = reinterpret_cast<uint32_t const*>(std::data(*code));
	auto shader_module = VkShaderModule{};
	if (vkCreateShaderModule(m_device, &create_info, nullptr, &shader_module) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateShaderModule for \"" + spirv_path + '"' };
	}
	return shader_module;
}
