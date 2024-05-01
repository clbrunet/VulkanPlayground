#include "Application.hpp"

#include <iostream>
#include <stdexcept>
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
	for (auto const in_flight_fence : m_in_flight_fences) {
		vkDestroyFence(m_device, in_flight_fence, nullptr);
	}
	for (auto const render_finished_semaphore : m_render_finished_semaphores) {
		vkDestroySemaphore(m_device, render_finished_semaphore, nullptr);
	}
	for (auto const image_available_semaphore : m_image_available_semaphores) {
		vkDestroySemaphore(m_device, image_available_semaphore, nullptr);
	}
	vkDestroyCommandPool(m_device, m_command_pool, nullptr);
	clean_swapchain();
	vkDestroyPipeline(m_device, m_graphics_pipeline, nullptr);
	vkDestroyPipelineLayout(m_device, m_pipeline_layout, nullptr);
	vkDestroyRenderPass(m_device, m_render_pass, nullptr);
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
	vkDeviceWaitIdle(m_device);
}

void Application::set_has_window_been_resized() {
	m_has_window_been_resized = true;
}

void Application::init_window() {
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

	auto constexpr WIDTH = 1280u;
	auto constexpr HEIGHT = 720u;
	m_window = glfwCreateWindow(WIDTH, HEIGHT, "Vulkan Playground", nullptr, nullptr);
	if (!m_window) {
		throw std::runtime_error{ "glfwCreateWindow" };
	}

	glfwSetWindowUserPointer(m_window, this);
	glfwSetFramebufferSizeCallback(m_window, [](GLFWwindow* const window, [[maybe_unused]] int const width, [[maybe_unused]] int const height) noexcept {
		reinterpret_cast<Application*>(glfwGetWindowUserPointer(window))->set_has_window_been_resized();
	});
}

void Application::init_vulkan() {
	create_instance();
	create_surface();
	select_physical_device();
	create_device();
	create_swapchain();
	create_image_views();
	create_render_pass();
	create_graphics_pipeline();
	create_framebuffers();
	create_command_pool();
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

	auto min_image_count = surface_capabilities.minImageCount + 1u;
	if (surface_capabilities.maxImageCount > 0u) {
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

	auto const shader_stages = std::array<VkPipelineShaderStageCreateInfo, 2u>{
		vertex_shader_stage_create_info,
		fragment_shader_stage_create_info
	};

	auto vertex_input_state_create_info = VkPipelineVertexInputStateCreateInfo{};
	vertex_input_state_create_info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
	vertex_input_state_create_info.vertexBindingDescriptionCount = 0u;
	vertex_input_state_create_info.vertexAttributeDescriptionCount = 0u;

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
	rasterization_state_create_info.frontFace = VK_FRONT_FACE_CLOCKWISE;
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
	pipeline_layout_create_info.setLayoutCount = 0u;
	pipeline_layout_create_info.pushConstantRangeCount = 0u;

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

static std::string get_spirv_shader_path(std::string shader) {
	return SPIRV_SHADERS_DIRECTORY + ("/" + std::move(shader) + ".spv");
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

	vkCmdDraw(command_buffer, 3u, 1u, 0u, 0u);

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

	vkDeviceWaitIdle(m_device);

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
