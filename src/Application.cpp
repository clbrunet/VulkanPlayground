#include "Application.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <iostream>
#include <set>
#include <algorithm>
#include <limits>
#include <functional>
#include <fstream>
#include <optional>
#include <span>
#include <format>
#include <chrono>

#ifndef NDEBUG
constexpr auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
#endif

struct MvpUniformBufferObject {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projection;
};

struct Vertex {
	glm::vec3 position;
	glm::vec3 color;

	constexpr static VkVertexInputBindingDescription binding_description() {
		auto binding_description = VkVertexInputBindingDescription{};
		binding_description.binding = 0u;
		binding_description.stride = sizeof(Vertex);
		binding_description.inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
		return binding_description;
	}

	constexpr static std::array<VkVertexInputAttributeDescription, 2> attribute_descriptions() {
		auto attribute_descriptions = std::array<VkVertexInputAttributeDescription, 2>{};
		attribute_descriptions[0].location = 0u;
		attribute_descriptions[0].binding = 0u;
		attribute_descriptions[0].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_descriptions[0].offset = offsetof(Vertex, position);

		attribute_descriptions[1].location = 1u;
		attribute_descriptions[1].binding = 0u;
		attribute_descriptions[1].format = VK_FORMAT_R32G32B32_SFLOAT;
		attribute_descriptions[1].offset = offsetof(Vertex, color);
		return attribute_descriptions;
	}
};

constexpr auto VERTICES = std::array<Vertex, 4>{{
	Vertex{ .position = glm::vec3{ -0.5f, -0.5f, 0.f }, .color = glm::vec3{ 1.f, 1.f, 1.f } },
	Vertex{ .position = glm::vec3{ 0.5f, -0.5f, 0.f }, .color = glm::vec3{ 0.f, 1.f, 0.f } },
	Vertex{ .position = glm::vec3{ 0.5f, 0.5f, 0.f }, .color = glm::vec3{ 0.f, 1.f, 0.f } },
	Vertex{ .position = glm::vec3{ -0.5f, 0.5f, 0.f }, .color = glm::vec3{ 1.f, 1.f, 1.f } },
}};

constexpr auto INDICES = std::array<uint16_t, 6>{{ 0, 1, 2, 2, 3, 0 }};

static std::string get_spirv_shader_path(std::string shader) {
	return SPIRV_SHADERS_DIRECTORY + ("/" + std::move(shader) + ".spv");
}

static std::string get_asset_path(std::string asset) {
	return ASSETS_DIRECTORY + ("/" + std::move(asset));
}

Application::Application() {
	init_window();
	init_vulkan();
}

Application::~Application() {
	for (auto const in_flight_fence : m_in_flight_fences) {
		vkDestroyFence(m_device, in_flight_fence, nullptr);
	}
	for (auto const render_finished_semaphore : m_render_finished_semaphores) {
		vkDestroySemaphore(m_device, render_finished_semaphore, nullptr);
	}
	for (auto const image_available_semaphore : m_image_available_semaphores) {
		vkDestroySemaphore(m_device, image_available_semaphore, nullptr);
	}

	vkDestroyDescriptorPool(m_device, m_descriptor_pool, nullptr);
	for (auto const mvp_uniform_buffer : m_mvp_uniform_buffers) {
		vkDestroyBuffer(m_device, mvp_uniform_buffer, nullptr);
	}
	for (auto const mvp_uniform_buffer_memory : m_mvp_uniform_buffer_memories) {
		vkFreeMemory(m_device, mvp_uniform_buffer_memory, nullptr);
	}

	vkDestroyBuffer(m_device, m_index_buffer, nullptr);
	vkFreeMemory(m_device, m_index_buffer_memory, nullptr);
	vkDestroyBuffer(m_device, m_vertex_buffer, nullptr);
	vkFreeMemory(m_device, m_vertex_buffer_memory, nullptr);

	vkDestroyImage(m_device, m_texture_image, nullptr);
	vkFreeMemory(m_device, m_texture_image_memory, nullptr);

	vkDestroyCommandPool(m_device, m_command_pool, nullptr);

	clean_swapchain();

	vkDestroyPipeline(m_device, m_graphics_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
	vkDestroyRenderPass(m_device, m_render_pass, nullptr);
	vkDestroyDescriptorSetLayout(m_device, m_descriptor_set_layout, nullptr);

	vkDestroyDevice(m_device, nullptr);
	vkDestroySurfaceKHR(m_instance, m_surface, nullptr);
	vkDestroyInstance(m_instance, nullptr);
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

void Application::run() {
	while (!glfwWindowShouldClose(m_window)) {
		draw_frame();
		glfwPollEvents();
	}
	if (vkDeviceWaitIdle(m_device) != VK_SUCCESS) {
		throw std::runtime_error{ "vkDeviceWaitIdle" };
	}
}

void Application::set_has_window_been_resized() {
	m_has_window_been_resized = true;
}

void Application::init_window() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	constexpr auto WIDTH = 1280u;
	constexpr auto HEIGHT = 720u;
	m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Playground", nullptr, nullptr);
	if (!m_window) {
		throw std::runtime_error{ "glfwCreateWindow" };
	}

	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* const window,
		[[maybe_unused]] int const width, [[maybe_unused]] int const height) noexcept {
		reinterpret_cast<Application*>(glfwGetWindowUserPointer(window))->set_has_window_been_resized();
	});

	glfwSetKeyCallback(m_window, [](GLFWwindow* const window, int const key,
		[[maybe_unused]] int const scancode, int const action, [[maybe_unused]] int const mods) noexcept {
		if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
			glfwSetWindowShouldClose(window, true);
		}
	});
}

void Application::init_vulkan() {
	create_instance();
	create_surface();
	select_physical_device();
	create_device();

	create_swapchain();
	create_image_views();

	create_descriptor_set_layout();
	create_render_pass();
	create_graphics_pipeline();
	create_framebuffers();

	create_command_pool();

	create_vertex_buffer();
	create_index_buffer();

	create_texture_image();

	create_uniform_buffers();
	create_descriptor_pool();
	create_descriptor_sets();

	create_command_buffers();

	create_sync_objects();
}

void Application::create_instance() {
	auto application_info = VkApplicationInfo{};
	application_info.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
	application_info.pApplicationName = "Vulkan Playground";
	application_info.applicationVersion = VK_MAKE_VERSION(0u, 1u, 0u);
	application_info.pEngineName = "No Engine";
	application_info.engineVersion = VK_MAKE_VERSION(0u, 1u, 0u);
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

	auto properties = VkPhysicalDeviceProperties{};
	vkGetPhysicalDeviceProperties(m_physical_device, &properties);
	std::cout << "Selected GPU : " << properties.deviceName << std::endl;
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
		index += 1u;
	}
	if (!has_graphics_queue || !has_present_support
		|| !has_device_extension(physical_device, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
		return 0u;
	}
	auto score = 1u;

	auto present_mode_count = uint32_t{};
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, nullptr);
	auto present_modes = std::vector<VkPresentModeKHR>{ present_mode_count };
	vkGetPhysicalDeviceSurfacePresentModesKHR(physical_device, m_surface, &present_mode_count, std::data(present_modes));
	auto const mailbox_present_mode_it = std::find_if(std::cbegin(present_modes), std::cend(present_modes), [](VkPresentModeKHR const present_mode) {
		return present_mode == VK_PRESENT_MODE_MAILBOX_KHR;
	});
	if (mailbox_present_mode_it != std::cend(present_modes)) {
		score += 1u;
	}

	return score;
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
	vkGetDeviceQueue(m_device, queue_family_indices.graphics, 0u, &m_graphics_queue);
	vkGetDeviceQueue(m_device, queue_family_indices.graphics, 0u, &m_present_queue);
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
		index += 1u;
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

	auto min_image_count = surface_capabilities.minImageCount + 1u;
	if (surface_capabilities.maxImageCount > 0u) {
		min_image_count = std::min(min_image_count, surface_capabilities.maxImageCount);
	}

	auto surface_format_count = uint32_t{};
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &surface_format_count, nullptr);
	auto surface_formats = std::vector<VkSurfaceFormatKHR>{ surface_format_count };
	vkGetPhysicalDeviceSurfaceFormatsKHR(m_physical_device, m_surface, &surface_format_count, std::data(surface_formats));
	auto surface_format_it = std::find_if(std::cbegin(surface_formats), std::cend(surface_formats), [](VkSurfaceFormatKHR const surface_format) {
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
	auto const queue_family_indices = std::array<uint32_t, 2u>{{
		physical_device_queue_family_indices.graphics,
		physical_device_queue_family_indices.present
	}};
	auto const [sharing_mode, queue_family_index_count] = std::invoke([&]() {
		if (m_graphics_queue != m_present_queue) {
			return std::tuple<VkSharingMode, uint32_t>{
				VK_SHARING_MODE_CONCURRENT, 2u
			};
		}
		return std::tuple<VkSharingMode, uint32_t>{
			VK_SHARING_MODE_EXCLUSIVE, 0u
		};
	});

	auto present_mode_count = uint32_t{};
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &present_mode_count, nullptr);
	auto present_modes = std::vector<VkPresentModeKHR>{ present_mode_count };
	vkGetPhysicalDeviceSurfacePresentModesKHR(m_physical_device, m_surface, &present_mode_count, std::data(present_modes));
	auto const present_mode_it = std::find_if(std::cbegin(present_modes), std::cend(present_modes), [](VkPresentModeKHR const present_mode) {
		return present_mode == VK_PRESENT_MODE_MAILBOX_KHR;
	});
	auto const present_mode = (present_mode_it != std::cend(present_modes)) ? *present_mode_it : VK_PRESENT_MODE_FIFO_KHR;

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
	create_info.presentMode = present_mode;
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
		create_info.subresourceRange.baseMipLevel = 0u;
		create_info.subresourceRange.levelCount = 1u;
		create_info.subresourceRange.baseArrayLayer = 0u;
		create_info.subresourceRange.layerCount = 1u;

		auto image_view = VkImageView{};
		if (vkCreateImageView(m_device, &create_info, nullptr, &image_view) != VK_SUCCESS) {
			throw std::runtime_error{ "vkCreateImageView" };
		}
		return image_view;
	});
}

void Application::create_descriptor_set_layout() {
	auto binding = VkDescriptorSetLayoutBinding{};
	binding.binding = 0u;
	binding.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	binding.descriptorCount = 1u;
	binding.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;

	auto create_info = VkDescriptorSetLayoutCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
	create_info.bindingCount = 1u;
	create_info.pBindings = &binding;

	if (vkCreateDescriptorSetLayout(m_device, &create_info, nullptr, &m_descriptor_set_layout) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateDescriptorSetLayout" };
	}
}

void Application::create_render_pass() {
	auto attachment_description = VkAttachmentDescription{};
	attachment_description.format = m_swapchain_format;
	attachment_description.samples = VK_SAMPLE_COUNT_1_BIT;
	attachment_description.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
	attachment_description.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
	attachment_description.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
	attachment_description.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
	attachment_description.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	attachment_description.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

	auto attachment_reference = VkAttachmentReference{};
	attachment_reference.attachment = 0u;
	attachment_reference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

	auto subpass_description = VkSubpassDescription{};
	subpass_description.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
	subpass_description.colorAttachmentCount = 1u;
	subpass_description.pColorAttachments = &attachment_reference;

	auto subpass_dependency = VkSubpassDependency{};
	subpass_dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
	subpass_dependency.dstSubpass = 0u;
	subpass_dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
	subpass_dependency.srcAccessMask = 0;
	subpass_dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

	auto create_info = VkRenderPassCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
	create_info.attachmentCount = 1u;
	create_info.pAttachments = &attachment_description;
	create_info.subpassCount = 1u;
	create_info.pSubpasses = &subpass_description;
	create_info.dependencyCount = 1u;
	create_info.pDependencies = &subpass_dependency;

	if (vkCreateRenderPass(m_device, &create_info, nullptr, &m_render_pass) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateRenderPass" };
	}
}

void Application::create_graphics_pipeline() {
	auto const vertex_shader_module = create_shader_module("colored.vert");
	auto const fragment_shader_module = create_shader_module("colored.frag");

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

	auto const shader_stages = std::array<VkPipelineShaderStageCreateInfo, 2u>{
		vertex_shader_stage_create_info,
		fragment_shader_stage_create_info
	};

	auto vertex_binding_description = Vertex::binding_description();
	auto vertex_attribute_descriptions = Vertex::attribute_descriptions();
	auto vertex_input_state_create_info = VkPipelineVertexInputStateCreateInfo{};
	vertex_input_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 1u;
	vertex_input_state_create_info.pVertexBindingDescriptions = &vertex_binding_description;
	vertex_input_state_create_info.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(vertex_attribute_descriptions));
	vertex_input_state_create_info.pVertexAttributeDescriptions = std::data(vertex_attribute_descriptions);

	auto input_assembly_state_create_info = VkPipelineInputAssemblyStateCreateInfo{};
	input_assembly_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
	input_assembly_state_create_info.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	input_assembly_state_create_info.primitiveRestartEnable = VK_FALSE;

	auto viewport_state_create_info = VkPipelineViewportStateCreateInfo{};
	viewport_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
	viewport_state_create_info.viewportCount = 1u;
	viewport_state_create_info.scissorCount = 1u;

	auto rasterization_state_create_info = VkPipelineRasterizationStateCreateInfo{};
	rasterization_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
	rasterization_state_create_info.depthClampEnable = VK_FALSE;
	rasterization_state_create_info.rasterizerDiscardEnable = VK_FALSE;
	rasterization_state_create_info.polygonMode = VK_POLYGON_MODE_FILL;
	rasterization_state_create_info.cullMode = VK_CULL_MODE_BACK_BIT;
	rasterization_state_create_info.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
	rasterization_state_create_info.depthBiasEnable = VK_FALSE;
	rasterization_state_create_info.lineWidth = 1.f;

	auto multisample_state_create_info = VkPipelineMultisampleStateCreateInfo{};
	multisample_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
	multisample_state_create_info.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	multisample_state_create_info.sampleShadingEnable = VK_FALSE;

	auto color_blend_attachment_state = VkPipelineColorBlendAttachmentState{};
	color_blend_attachment_state.blendEnable = VK_FALSE;
	color_blend_attachment_state.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;

	auto color_blend_state_create_info = VkPipelineColorBlendStateCreateInfo{};
	color_blend_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
	color_blend_state_create_info.logicOpEnable = VK_FALSE;
	color_blend_state_create_info.attachmentCount = 1u;
	color_blend_state_create_info.pAttachments = &color_blend_attachment_state;

	auto const dynamic_states = std::array<VkDynamicState, 2u>{{
		VK_DYNAMIC_STATE_VIEWPORT,
		VK_DYNAMIC_STATE_SCISSOR
	}};
	auto dynamic_state_create_info = VkPipelineDynamicStateCreateInfo{};
	dynamic_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
	dynamic_state_create_info.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states));
	dynamic_state_create_info.pDynamicStates = std::data(dynamic_states);

	auto pipeline_layout_create_info = VkPipelineLayoutCreateInfo{};
	pipeline_layout_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
	pipeline_layout_create_info.setLayoutCount = 1u;
	pipeline_layout_create_info.pSetLayouts = &m_descriptor_set_layout;

	if (vkCreatePipelineLayout(m_device, &pipeline_layout_create_info, nullptr, &m_pipeline_layout) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreatePipelineLayout" };
	}

	auto create_info = VkGraphicsPipelineCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
	create_info.stageCount = 2u;
	create_info.pStages = std::data(shader_stages);
	create_info.pVertexInputState = &vertex_input_state_create_info;
	create_info.pInputAssemblyState = &input_assembly_state_create_info;
	create_info.pTessellationState = nullptr;
	create_info.pViewportState = &viewport_state_create_info;
	create_info.pRasterizationState = &rasterization_state_create_info;
	create_info.pMultisampleState = &multisample_state_create_info;
	create_info.pDepthStencilState = nullptr;
	create_info.pColorBlendState = &color_blend_state_create_info;
	create_info.pDynamicState = &dynamic_state_create_info;
	create_info.layout = m_pipeline_layout;
	create_info.renderPass = m_render_pass;
	create_info.subpass = 0u;

	if (vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1u, &create_info, nullptr, &m_graphics_pipeline) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateGraphicsPipelines" };
	}

	vkDestroyShaderModule(m_device, vertex_shader_module, nullptr);
	vkDestroyShaderModule(m_device, fragment_shader_module, nullptr);
}

static std::optional<std::vector<uint8_t>> read_binary_file(std::string const& path) {
	auto file = std::ifstream{ path, std::ios::ate | std::ios::binary };
	if (!file) {
		return std::nullopt;
	}

	auto bytes = std::vector<uint8_t>(static_cast<size_t>(file.tellg()));
	file.seekg(0);
	file.read(reinterpret_cast<char*>(std::data(bytes)), static_cast<std::streamsize>(std::size(bytes)));
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

void Application::create_framebuffers() {
	std::transform(std::cbegin(m_image_views), std::cend(m_image_views),
		std::back_inserter(m_framebuffers), [&](VkImageView const image_view) {
		auto create_info = VkFramebufferCreateInfo{};
		create_info.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
		create_info.renderPass = m_render_pass;
		create_info.attachmentCount = 1u;
		create_info.pAttachments = &image_view;
		create_info.width = m_swapchain_extent.width;
		create_info.height = m_swapchain_extent.height;
		create_info.layers = 1u;

		auto framebuffer = VkFramebuffer{};
		if (vkCreateFramebuffer(m_device, &create_info, nullptr, &framebuffer) != VK_SUCCESS) {
			throw std::runtime_error{ "vkCreateFramebuffer" };
		}
		return framebuffer;
	});
}

void Application::create_command_pool() {
	auto create_info = VkCommandPoolCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
	create_info.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
	create_info.queueFamilyIndex = get_queue_family_indices().graphics;

	if (vkCreateCommandPool(m_device, &create_info, nullptr, &m_command_pool) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateCommandPool" };
	}
}

void Application::create_vertex_buffer() {
	auto const buffer_size = std::size(VERTICES) * sizeof(VERTICES[0]);

	auto staging_buffer = VkBuffer{};
	auto staging_buffer_memory = VkDeviceMemory{};
	create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

	void* data;
	vkMapMemory(m_device, staging_buffer_memory, 0u, buffer_size, 0, &data);
	std::memcpy(data, std::data(VERTICES), buffer_size);
	vkUnmapMemory(m_device, staging_buffer_memory);

	create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_vertex_buffer, m_vertex_buffer_memory);

	copy_buffer(staging_buffer, m_vertex_buffer, buffer_size);

	vkDestroyBuffer(m_device, staging_buffer, nullptr);
	vkFreeMemory(m_device, staging_buffer_memory, nullptr);
}

void Application::create_index_buffer() {
	auto const buffer_size = std::size(INDICES) * sizeof(INDICES[0]);

	auto staging_buffer = VkBuffer{};
	auto staging_buffer_memory = VkDeviceMemory{};
	create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, staging_buffer, staging_buffer_memory);

	void* data;
	vkMapMemory(m_device, staging_buffer_memory, 0u, buffer_size, 0, &data);
	std::memcpy(data, std::data(INDICES), buffer_size);
	vkUnmapMemory(m_device, staging_buffer_memory);

	create_buffer(buffer_size, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_index_buffer, m_index_buffer_memory);

	copy_buffer(staging_buffer, m_index_buffer, buffer_size);

	vkDestroyBuffer(m_device, staging_buffer, nullptr);
	vkFreeMemory(m_device, staging_buffer_memory, nullptr);
}

void Application::create_texture_image() {
	auto width = int{};
	auto height = int{};
	auto pixels = stbi_load(get_asset_path("textures/statue.jpg").c_str(),
		&width, &height, nullptr, STBI_rgb_alpha);
	if (!pixels) {
		throw std::runtime_error{ "failed to load texture" };
	}
	auto image_size = static_cast<VkDeviceSize>(width * height * STBI_rgb_alpha);

	VkBuffer staging_buffer;
	VkDeviceMemory staging_buffer_memory;
	create_buffer(image_size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
		VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
		staging_buffer, staging_buffer_memory);

	void* data;
	vkMapMemory(m_device, staging_buffer_memory, 0u, image_size, 0, &data);
	std::memcpy(data, pixels, image_size);
	vkUnmapMemory(m_device, staging_buffer_memory);

	stbi_image_free(pixels);

	create_image(VK_FORMAT_R8G8B8A8_SRGB, static_cast<uint32_t>(width), static_cast<uint32_t>(height),
		VK_IMAGE_TILING_OPTIMAL, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
		VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, m_texture_image, m_texture_image_memory);

	transition_image_layout(m_texture_image, VK_IMAGE_LAYOUT_UNDEFINED,
		VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
	copy_buffer_to_image(staging_buffer, m_texture_image, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	transition_image_layout(m_texture_image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
		VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

	vkDestroyBuffer(m_device, staging_buffer, nullptr);
	vkFreeMemory(m_device, staging_buffer_memory, nullptr);
}

void Application::create_uniform_buffers() {
	auto const buffer_size = sizeof(MvpUniformBufferObject);
	for (auto i = 0u; i < std::size(m_mvp_uniform_buffers); ++i) {
		create_buffer(buffer_size, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
			VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
			m_mvp_uniform_buffers[i], m_mvp_uniform_buffer_memories[i]);
		vkMapMemory(m_device, m_mvp_uniform_buffer_memories[i], 0u, buffer_size, 0, &m_mvp_uniform_buffer_maps[i]);
	}
}

void Application::create_buffer(VkDeviceSize const size, VkBufferUsageFlags const usage,
	VkMemoryPropertyFlags const memory_property_flags, VkBuffer& buffer, VkDeviceMemory& buffer_memory) const {
	auto create_info = VkBufferCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
	create_info.size = size;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateBuffer(m_device, &create_info, nullptr, &buffer) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateBuffer" };
	}

	auto memory_requirements = VkMemoryRequirements{};
	vkGetBufferMemoryRequirements(m_device, buffer, &memory_requirements);

	auto memory_allocate_info = VkMemoryAllocateInfo{};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, memory_property_flags);

	if (vkAllocateMemory(m_device, &memory_allocate_info, nullptr, &buffer_memory) != VK_SUCCESS) {
		throw std::runtime_error{ "vkAllocateMemory" };
	}

	vkBindBufferMemory(m_device, buffer, buffer_memory, 0u);
}

void Application::copy_buffer(VkBuffer const src, VkBuffer const dst, VkDeviceSize size) const {
	one_time_command([=](VkCommandBuffer const command_buffer) {
		auto copy_region = VkBufferCopy{};
		copy_region.size = size;
		vkCmdCopyBuffer(command_buffer, src, dst, 1u, &copy_region);
	});
}

void Application::create_image(VkFormat const format, uint32_t const width, uint32_t const height,
	VkImageTiling const tiling, VkImageUsageFlags const usage, VkMemoryPropertyFlags const memory_property_flags,
	VkImage& image, VkDeviceMemory& image_memory) {
	auto create_info = VkImageCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
	create_info.imageType = VK_IMAGE_TYPE_2D;
	create_info.format = format;
	create_info.extent = VkExtent3D{ width, height, 1u };
	create_info.mipLevels = 1u;
	create_info.arrayLayers = 1u;
	create_info.samples = VK_SAMPLE_COUNT_1_BIT;
	create_info.tiling = tiling;
	create_info.usage = usage;
	create_info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

	if (vkCreateImage(m_device, &create_info, nullptr, &image) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateImage" };
	}

	auto memory_requirements = VkMemoryRequirements{};
	vkGetImageMemoryRequirements(m_device, image, &memory_requirements);

	auto memory_allocate_info = VkMemoryAllocateInfo{};
	memory_allocate_info.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
	memory_allocate_info.allocationSize = memory_requirements.size;
	memory_allocate_info.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, memory_property_flags);

	if (vkAllocateMemory(m_device, &memory_allocate_info, nullptr, &image_memory) != VK_SUCCESS) {
		throw std::runtime_error{ "vkAllocateMemory" };
	}

	vkBindImageMemory(m_device, image, image_memory, 0u);
}

void Application::transition_image_layout(VkImage const image,
	VkImageLayout const old_layout, VkImageLayout const new_layout) {
	one_time_command([=](VkCommandBuffer const command_buffer) {
		auto memory_barrier = VkImageMemoryBarrier{};
		memory_barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
		memory_barrier.oldLayout = old_layout;
		memory_barrier.newLayout = new_layout;
		memory_barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memory_barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		memory_barrier.image = image;
		memory_barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		memory_barrier.subresourceRange.baseMipLevel = 0u;
		memory_barrier.subresourceRange.levelCount = 1u;
		memory_barrier.subresourceRange.baseArrayLayer = 0u;
		memory_barrier.subresourceRange.layerCount = 1u;

		auto src_stage = VkPipelineStageFlags{};
		auto dst_stage = VkPipelineStageFlags{};
		if (old_layout == VK_IMAGE_LAYOUT_UNDEFINED && new_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
			memory_barrier.srcAccessMask = 0;
			memory_barrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			src_stage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
			dst_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
		} else if (old_layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && new_layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
			memory_barrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
			memory_barrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
			src_stage = VK_PIPELINE_STAGE_TRANSFER_BIT;
			dst_stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
		} else {
			assert(false && "unsupported image layout transition");
		}

		vkCmdPipelineBarrier(command_buffer, src_stage, dst_stage, 0, 0u, nullptr, 0u, nullptr, 1u, &memory_barrier);
	});
}

void Application::copy_buffer_to_image(VkBuffer const src, VkImage const dst, uint32_t const width, uint32_t const height) const {
	one_time_command([=](VkCommandBuffer const command_buffer) {
		auto copy_region = VkBufferImageCopy{};
		copy_region.bufferOffset = 0u;
		copy_region.bufferRowLength = 0u;
		copy_region.bufferImageHeight = 0u;
		copy_region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
		copy_region.imageSubresource.mipLevel = 0u;
		copy_region.imageSubresource.baseArrayLayer = 0u;
		copy_region.imageSubresource.layerCount = 1u;
		copy_region.imageOffset = VkOffset3D{ 0u, 0u, 0u };
		copy_region.imageExtent = VkExtent3D{ width, height, 1u };

		vkCmdCopyBufferToImage(command_buffer, src, dst, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1u, &copy_region);
	});
}

uint32_t Application::find_memory_type(uint32_t const type_bits, VkMemoryPropertyFlags const property_flags) const {
	auto memory_properties = VkPhysicalDeviceMemoryProperties{};
	vkGetPhysicalDeviceMemoryProperties(m_physical_device, &memory_properties);

	auto i = 0u;
	auto const memory_types = std::span{ memory_properties.memoryTypes, memory_properties.memoryTypeCount };
	for (auto const memory_type : memory_types) {
		if ((type_bits & (1 << i)) != 0 && (memory_type.propertyFlags & property_flags) == property_flags) {
			return i;
		}
		i += 1u;
	}

	auto const message = std::format(
		"physical device lacks memory type with bits: {:#b} and VkMemoryPropertyFlags: {:#b}",
		type_bits, property_flags);
	throw std::runtime_error{ message };
}

void Application::create_descriptor_pool() {
	auto pool_size = VkDescriptorPoolSize{};
	pool_size.type = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
	pool_size.descriptorCount = static_cast<uint32_t>(std::size(m_descriptor_sets));

	auto create_info = VkDescriptorPoolCreateInfo{};
	create_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
	create_info.maxSets = static_cast<uint32_t>(std::size(m_descriptor_sets));
	create_info.poolSizeCount = 1u;
	create_info.pPoolSizes = &pool_size;

	if (vkCreateDescriptorPool(m_device, &create_info, nullptr, &m_descriptor_pool) != VK_SUCCESS) {
		throw std::runtime_error{ "vkCreateDescriptorPool" };
	}
}

void Application::create_descriptor_sets() {
	auto layouts = std::array<VkDescriptorSetLayout, MAX_FRAMES_IN_FLIGHT>{};
	layouts.fill(m_descriptor_set_layout);
	auto allocate_info = VkDescriptorSetAllocateInfo{};
	allocate_info.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
	allocate_info.descriptorPool = m_descriptor_pool;
	allocate_info.descriptorSetCount = static_cast<uint32_t>(std::size(m_descriptor_sets));
	allocate_info.pSetLayouts = std::data(layouts);

	if (vkAllocateDescriptorSets(m_device, &allocate_info, std::data(m_descriptor_sets)) != VK_SUCCESS) {
		throw std::runtime_error{ "vkAllocateDescriptorSets" };
	}

	auto i = 0u;
	for (auto descriptor_set : m_descriptor_sets) {
		auto buffer_info = VkDescriptorBufferInfo{};
		buffer_info.buffer = m_mvp_uniform_buffers[i];
		buffer_info.offset = 0u;
		buffer_info.range = VK_WHOLE_SIZE;

		auto descriptor_write = VkWriteDescriptorSet{};
		descriptor_write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
		descriptor_write.dstSet = descriptor_set;
		descriptor_write.dstBinding = 0u;
		descriptor_write.dstArrayElement = 0u;
		descriptor_write.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
		descriptor_write.descriptorCount = 1u;
		descriptor_write.pBufferInfo = &buffer_info;

		vkUpdateDescriptorSets(m_device, 1u, &descriptor_write, 0u, nullptr);
		i += 1u;
	}
}

void Application::create_command_buffers() {
	auto allocate_info = VkCommandBufferAllocateInfo{};
	allocate_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
	allocate_info.commandPool = m_command_pool;
	allocate_info.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
	allocate_info.commandBufferCount = static_cast<uint32_t>(std::size(m_command_buffers));

	if (vkAllocateCommandBuffers(m_device, &allocate_info, std::data(m_command_buffers)) != VK_SUCCESS) {
		throw std::runtime_error{ "vkAllocateCommandBuffers" };
	}
}

void Application::create_sync_objects() {
	auto image_available_semaphore_create_info = VkSemaphoreCreateInfo{};
	image_available_semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (auto& image_available_semaphore : m_image_available_semaphores) {
		if (vkCreateSemaphore(m_device, &image_available_semaphore_create_info, nullptr, &image_available_semaphore) != VK_SUCCESS) {
			throw std::runtime_error{ "image available vkCreateSemaphore" };
		}
	}

	auto render_finished_semaphore_create_info = VkSemaphoreCreateInfo{};
	render_finished_semaphore_create_info.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
	for (auto& render_finished_semaphore : m_render_finished_semaphores) {
		if (vkCreateSemaphore(m_device, &render_finished_semaphore_create_info, nullptr, &render_finished_semaphore) != VK_SUCCESS) {
			throw std::runtime_error{ "render finished vkCreateSemaphore" };
		}
	}

	auto in_flight_fence_create_info = VkFenceCreateInfo{};
	in_flight_fence_create_info.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
	in_flight_fence_create_info.flags = VK_FENCE_CREATE_SIGNALED_BIT;
	for (auto& in_flight_fence : m_in_flight_fences) {
		if (vkCreateFence(m_device, &in_flight_fence_create_info, nullptr, &in_flight_fence) != VK_SUCCESS) {
			throw std::runtime_error{ "in flight vkCreateFence" };
		}
	}
}

void Application::draw_frame() {
	auto const& in_flight_fence = m_in_flight_fences[m_current_in_flight_frame_index];
	auto const& image_available_semaphore = m_image_available_semaphores[m_current_in_flight_frame_index];
	auto const& command_buffer = m_command_buffers[m_current_in_flight_frame_index];
	auto const& render_finished_semaphore = m_render_finished_semaphores[m_current_in_flight_frame_index];

	vkWaitForFences(m_device, 1u, &in_flight_fence, VK_TRUE, std::numeric_limits<uint64_t>::max());

	auto image_index = uint32_t{};
	auto result = vkAcquireNextImageKHR(m_device, m_swapchain, std::numeric_limits<uint64_t>::max(), image_available_semaphore, VK_NULL_HANDLE, &image_index);
	if (result == VK_ERROR_OUT_OF_DATE_KHR) {
		recreate_swapchain();
		return;
	} else if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
		throw std::runtime_error{ "vkAcquireNextImageKHR" };
	}
	update_uniform_buffer(m_mvp_uniform_buffer_maps[m_current_in_flight_frame_index]);

	vkResetFences(m_device, 1u, &in_flight_fence);

	vkResetCommandBuffer(command_buffer, 0);
	record_command_buffer(command_buffer, image_index);

	auto wait_stage = VkPipelineStageFlags{ VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT };
	auto submit_info = VkSubmitInfo{};
	submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
	submit_info.waitSemaphoreCount = 1u;
	submit_info.pWaitSemaphores = &image_available_semaphore;
	submit_info.pWaitDstStageMask = &wait_stage;
	submit_info.commandBufferCount = 1u;
	submit_info.pCommandBuffers = &command_buffer;
	submit_info.signalSemaphoreCount = 1u;
	submit_info.pSignalSemaphores = &render_finished_semaphore;

	if (vkQueueSubmit(m_graphics_queue, 1u, &submit_info, in_flight_fence) != VK_SUCCESS) {
		throw std::runtime_error{ "vkQueueSubmit" };
	}

	auto present_info = VkPresentInfoKHR{};
	present_info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
	present_info.waitSemaphoreCount = 1u;
	present_info.pWaitSemaphores = &render_finished_semaphore;
	present_info.swapchainCount = 1u;
	present_info.pSwapchains = &m_swapchain;
	present_info.pImageIndices = &image_index;

	result = vkQueuePresentKHR(m_graphics_queue, &present_info);
	if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || m_has_window_been_resized) {
		recreate_swapchain();
	} else if (result != VK_SUCCESS) {
		throw std::runtime_error{ "vkQueuePresentKHR" };
	}

	m_current_in_flight_frame_index = (m_current_in_flight_frame_index + 1u) % MAX_FRAMES_IN_FLIGHT;
}

void Application::update_uniform_buffer(void* uniform_buffer_map) const {
	static auto const start_time = std::chrono::high_resolution_clock::now();
	auto const current_time = std::chrono::high_resolution_clock::now();
	auto const time = std::chrono::duration<float, std::chrono::seconds::period>{ current_time - start_time }.count();

	auto mvp = MvpUniformBufferObject{};
	mvp.model = glm::rotate(glm::mat4{ 1.f }, time * glm::radians(90.f), glm::vec3{ 0.f, 0.f, 1.f });
	mvp.view = glm::lookAt(glm::vec3{ 0.f, 2.f, 2.f }, glm::vec3{ 0.f }, glm::vec3{ 0.f, 1.f, 0.f });

	auto aspect_ratio = static_cast<float>(m_swapchain_extent.width) / static_cast<float>(m_swapchain_extent.height);
	mvp.projection = glm::perspective(glm::radians(60.f), aspect_ratio, 0.01f, 100.f);
	mvp.projection[1][1] *= -1;

	std::memcpy(uniform_buffer_map, &mvp, sizeof(mvp));
}

void Application::record_command_buffer(VkCommandBuffer const command_buffer, uint32_t const image_index) {
	auto begin_info = VkCommandBufferBeginInfo{};
	begin_info.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

	if(vkBeginCommandBuffer(command_buffer, &begin_info) != VK_SUCCESS) {
		throw std::runtime_error{ "vkBeginCommandBuffer" };
	}

	auto clear_value = VkClearValue{ VkClearColorValue{ { 0.f, 0.f, 0.f, 1.f } } };
	auto render_pass_begin_info = VkRenderPassBeginInfo{};
	render_pass_begin_info.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
	render_pass_begin_info.renderPass = m_render_pass;
	render_pass_begin_info.framebuffer = m_framebuffers[image_index];
	render_pass_begin_info.renderArea.offset = VkOffset2D{ 0, 0 };
	render_pass_begin_info.renderArea.extent = m_swapchain_extent;
	render_pass_begin_info.clearValueCount = 1u;
	render_pass_begin_info.pClearValues = &clear_value;

	vkCmdBeginRenderPass(command_buffer, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);

	vkCmdBindPipeline(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_graphics_pipeline);

	auto viewport = VkViewport{};
	viewport.x = 0.f;
	viewport.y = 0.f;
	viewport.width = static_cast<float>(m_swapchain_extent.width);
	viewport.height = static_cast<float>(m_swapchain_extent.height);
	viewport.minDepth = 0.f;
	viewport.maxDepth = 1.f;
	vkCmdSetViewport(command_buffer, 0u, 1u, &viewport);

	auto scissor = VkRect2D{};
	scissor.offset = VkOffset2D{ 0, 0 };
	scissor.extent = m_swapchain_extent;
	vkCmdSetScissor(command_buffer, 0u, 1u, &scissor);

	auto offset = VkDeviceSize{ 0u };
	vkCmdBindVertexBuffers(command_buffer, 0u, 1u, &m_vertex_buffer, &offset);
	vkCmdBindIndexBuffer(command_buffer, m_index_buffer, 0u, VK_INDEX_TYPE_UINT16);
	vkCmdBindDescriptorSets(command_buffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline_layout, 0u, 1u,
		&m_descriptor_sets[m_current_in_flight_frame_index], 0u, nullptr);
	vkCmdDrawIndexed(command_buffer, static_cast<uint32_t>(std::size(INDICES)), 1u, 0u, 0u, 0u);

	vkCmdEndRenderPass(command_buffer);

	if (vkEndCommandBuffer(command_buffer) != VK_SUCCESS) {
		throw std::runtime_error{ "vkEndCommandBuffer" };
	}
}

void Application::recreate_swapchain() {
	auto width = int{};
	auto height = int{};
	glfwGetFramebufferSize(m_window, &width, &height);
	while (width == 0 || height == 0) {
		glfwWaitEvents();
		glfwGetFramebufferSize(m_window, &width, &height);
	}

	if (vkDeviceWaitIdle(m_device) != VK_SUCCESS) {
		throw std::runtime_error{ "vkDeviceWaitIdle" };
	}

	clean_swapchain();

	create_swapchain();
	create_image_views();
	create_framebuffers();

	m_has_window_been_resized = false;
}

void Application::clean_swapchain() {
	for (auto const framebuffer : m_framebuffers) {
		vkDestroyFramebuffer(m_device, framebuffer, nullptr);
	}
	m_framebuffers.clear();
	for (auto const image_view : m_image_views) {
		vkDestroyImageView(m_device, image_view, nullptr);
	}
	m_image_views.clear();
	m_swapchain_images.clear();
	vkDestroySwapchainKHR(m_device, m_swapchain, nullptr);
}
