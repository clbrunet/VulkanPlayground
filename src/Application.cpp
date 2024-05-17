#include "Application.hpp"

#include <vulkan/vulkan.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <stb_image.h>

#include <iostream>
#include <set>
#include <algorithm>
#include <ranges>
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

struct MvpUniformBuffer {
	alignas(16) glm::mat4 model;
	alignas(16) glm::mat4 view;
	alignas(16) glm::mat4 projection;
};

struct Vertex {
	glm::vec3 position;
	glm::vec2 texture_coords;
	glm::vec3 color;

	constexpr static vk::VertexInputBindingDescription binding_description() {
		return vk::VertexInputBindingDescription{
			.binding = 0u,
			.stride = sizeof(Vertex),
			.inputRate = vk::VertexInputRate::eVertex,
		};
	}

	constexpr static std::array<vk::VertexInputAttributeDescription, 3u> attribute_descriptions() {
		return std::array<vk::VertexInputAttributeDescription, 3u>{
			vk::VertexInputAttributeDescription{
				.location = 0u,
				.binding = 0u,
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = offsetof(Vertex, position),
			},
			vk::VertexInputAttributeDescription{
				.location = 1u,
				.binding = 0u,
				.format = vk::Format::eR32G32Sfloat,
				.offset = offsetof(Vertex, texture_coords),
			},
			vk::VertexInputAttributeDescription{
				.location = 2u,
				.binding = 0u,
				.format = vk::Format::eR32G32B32Sfloat,
				.offset = offsetof(Vertex, color),
			},
		};
	}
};

constexpr auto VERTICES = std::array<Vertex, 4u>{
	Vertex{ .position = glm::vec3{ -0.5f, -0.5f, 0.f }, .texture_coords = glm::vec2{ 0.f, 1.f }, .color = glm::vec3{ 1.f, 0.f, 0.f }, },
	Vertex{ .position = glm::vec3{ 0.5f, -0.5f, 0.f }, .texture_coords = glm::vec2{ 1.f, 1.f }, .color = glm::vec3{ 0.f, 1.f, 0.f }, },
	Vertex{ .position = glm::vec3{ 0.5f, 0.5f, 0.f }, .texture_coords = glm::vec2{ 1.f, 0.f }, .color = glm::vec3{ 0.f, 0.f, 1.f }, },
	Vertex{ .position = glm::vec3{ -0.5f, 0.5f, 0.f }, .texture_coords = glm::vec2{ 0.f, 0.f }, .color = glm::vec3{ 1.f, 1.f, 1.f }, },
};

constexpr auto INDICES = std::array<uint16_t, 6u>{{ 0u, 1u, 2u, 2u, 3u, 0u, }};

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
	glfwDestroyWindow(m_window);
	glfwTerminate();
}

void Application::run() {
	while (!glfwWindowShouldClose(m_window)) {
		draw_frame();
		glfwPollEvents();
	}
	m_device.waitIdle();
}

void Application::set_has_window_been_resized() {
	m_should_recreate_swapchain = true;
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
	create_texture_image_view();
	create_texture_sampler();

	create_uniform_buffers();

	create_descriptor_pool();
	create_descriptor_sets();

	create_command_buffers();

	create_sync_objects();
}

void Application::create_instance() {
	auto const application_info = vk::ApplicationInfo{
		.pApplicationName = "Vulkan Playground",
		.applicationVersion = vk::makeApiVersion(0u, 0u, 1u, 0u),
		.pEngineName = "No Engine",
		.engineVersion = vk::makeApiVersion(0u, 0u, 1u, 0u),
		.apiVersion = vk::ApiVersion11,
	};

	auto glfw_extension_count = uint32_t{};
	auto const glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
	auto extensions = std::vector<char const*>{ glfw_extensions, glfw_extensions + glfw_extension_count };

	auto const has_portabibilty_enumeration_extension = has_instance_extension(m_context, vk::KHRPortabilityEnumerationExtensionName);
	if (has_portabibilty_enumeration_extension) {
		extensions.emplace_back(vk::KHRPortabilityEnumerationExtensionName);
	}

	auto create_info = vk::InstanceCreateInfo{
		.pApplicationInfo = &application_info,
		.enabledExtensionCount = static_cast<uint32_t>(std::size(extensions)),
		.ppEnabledExtensionNames = std::data(extensions),
	};
	if (has_portabibilty_enumeration_extension) {
		create_info.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
	}
#ifndef NDEBUG
	if (has_instance_layer(m_context, VALIDATION_LAYER)) {
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif

	m_instance = vk::raii::Instance{ m_context, create_info };
}

bool Application::has_instance_layer(vk::raii::Context const& context, std::string_view const layer_name) {
	return std::ranges::any_of(context.enumerateInstanceLayerProperties(), [layer_name](vk::LayerProperties const& property) {
		return layer_name.compare(std::data(property.layerName)) == 0;
	});
}

bool Application::has_instance_extension(vk::raii::Context const& context, std::string_view const extension_name) {
	return std::ranges::any_of(context.enumerateInstanceExtensionProperties(), [extension_name](vk::ExtensionProperties const& property) {
		return extension_name.compare(std::data(property.extensionName)) == 0;
	});
}

void Application::create_surface() {
	auto surface = VkSurfaceKHR{};
	if (glfwCreateWindowSurface(*m_instance, m_window, nullptr, &surface) != VK_SUCCESS) {
		throw std::runtime_error{ "glfwCreateWindowSurface" };
	}
	m_surface = vk::raii::SurfaceKHR{ m_instance, surface };
}

void Application::select_physical_device() {
	auto best_score = 0u;
	for (auto const& physical_device : m_instance.enumeratePhysicalDevices()) {
		auto const score = get_physical_device_score(physical_device);
		if (score > best_score) {
			best_score = score;
			m_physical_device = physical_device;
		}
	}
	if (!*m_physical_device) {
		throw std::runtime_error{ "no suitable GPU found" };
	}
	std::cout << "Selected GPU : " << m_physical_device.getProperties().deviceName << std::endl;
}

uint32_t Application::get_physical_device_score(vk::PhysicalDevice const physical_device) const {
	auto const queue_family_properties = physical_device.getQueueFamilyProperties();
	auto has_graphics_queue = false;
	auto has_present_support = false;
	auto queue_family_index = 0u;
	for (auto const& queue_family_property : queue_family_properties) {
		if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
			has_graphics_queue = true;
		}
		if (physical_device.getSurfaceSupportKHR(queue_family_index, m_surface)) {
			has_present_support = true;
		}
		queue_family_index += 1u;
	}
	if (!has_graphics_queue || !has_present_support
		|| !has_device_extension(physical_device, vk::KHRSwapchainExtensionName)) {
		return 0u;
	}
	auto score = 1u;

	auto const present_modes = physical_device.getSurfacePresentModesKHR(m_surface);
	auto const mailbox_present_mode_it = std::ranges::find_if(present_modes, [](vk::PresentModeKHR const present_mode) {
		return present_mode == vk::PresentModeKHR::eMailbox;
	});
	if (mailbox_present_mode_it != std::cend(present_modes)) {
		score += 1u;
	}

	return score;
}

void Application::create_device() {
	auto const queue_family_indices = get_queue_family_indices();
	auto const unique_queue_family_indices = std::set<uint32_t>{
		queue_family_indices.graphics,
		queue_family_indices.present
	};
	auto const queue_priority = 1.f;
	auto queue_create_infos = std::vector<vk::DeviceQueueCreateInfo>{};
	std::ranges::transform(unique_queue_family_indices, std::back_inserter(queue_create_infos),
		[&queue_priority](uint32_t const queue_family_index) {
			return vk::DeviceQueueCreateInfo{
				.queueFamilyIndex = queue_family_index,
				.queueCount = 1u,
				.pQueuePriorities = &queue_priority,
			};
	});

	auto const features = vk::PhysicalDeviceFeatures{
		.depthClamp = vk::True,
	};

	auto const extension = vk::KHRSwapchainExtensionName;

	auto create_info = vk::DeviceCreateInfo{
		.queueCreateInfoCount = static_cast<uint32_t>(std::size(queue_create_infos)),
		.pQueueCreateInfos = std::data(queue_create_infos),
		.enabledExtensionCount = 1u,
		.ppEnabledExtensionNames = &extension,
		.pEnabledFeatures = &features,
	};
#ifndef NDEBUG
	if (has_device_layer(m_physical_device, VALIDATION_LAYER)) {
		create_info.enabledLayerCount = 1u;
		create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
	}
#endif

	m_device = vk::raii::Device{ m_physical_device, create_info };
	m_graphics_queue = m_device.getQueue(queue_family_indices.graphics, 0u);
	m_present_queue = m_device.getQueue(queue_family_indices.present, 0u);
}

Application::QueueFamilyIndices Application::get_queue_family_indices() const {
	auto const queue_family_properties = m_physical_device.getQueueFamilyProperties();
	auto indices = QueueFamilyIndices{};
	auto queue_family_index = 0u;
	for (auto const& queue_family_property : queue_family_properties) {
		if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
			indices.graphics = queue_family_index;
		}
		if (m_physical_device.getSurfaceSupportKHR(queue_family_index, m_surface)) {
			indices.present = queue_family_index;
		}
		queue_family_index += 1u;
	}
	return indices;
}

bool Application::has_device_layer(vk::PhysicalDevice const physical_device, std::string_view const layer_name) {
	return std::ranges::any_of(physical_device.enumerateDeviceLayerProperties(), [layer_name](vk::LayerProperties const& property) {
		return layer_name.compare(std::data(property.layerName)) == 0;
	});
}

bool Application::has_device_extension(vk::PhysicalDevice physical_device, std::string_view const extension_name) {
	return std::ranges::any_of(physical_device.enumerateDeviceExtensionProperties(), [extension_name](vk::ExtensionProperties const& property) {
		return extension_name.compare(std::data(property.extensionName)) == 0;
	});
}

void Application::create_swapchain() {
	auto const surface_capabilities = m_physical_device.getSurfaceCapabilitiesKHR(m_surface);

	auto const min_image_count = [&] {
		if (surface_capabilities.maxImageCount == 0u) {
			return surface_capabilities.minImageCount + 1u;
		}
		return std::min(surface_capabilities.minImageCount + 1u, surface_capabilities.maxImageCount);
	}();

	auto const surface_format = [&] {
		auto const surface_formats = m_physical_device.getSurfaceFormatsKHR(m_surface);
		auto const surface_format_it = std::ranges::find_if(surface_formats, [](vk::SurfaceFormatKHR const device_surface_format) {
			return device_surface_format == vk::SurfaceFormatKHR{ vk::Format::eB8G8R8A8Srgb, vk::ColorSpaceKHR::eSrgbNonlinear };
		});
		return (surface_format_it != std::cend(surface_formats)) ? *surface_format_it : surface_formats.front();
	}();

	auto const image_extent = [&] {
		if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
			return surface_capabilities.currentExtent;
		}
		auto width = int{};
		auto height = int{};
		glfwGetFramebufferSize(m_window, &width, &height);
		return vk::Extent2D{
			.width = std::clamp(static_cast<uint32_t>(width), surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width),
			.height = std::clamp(static_cast<uint32_t>(height), surface_capabilities.minImageExtent.height,
				surface_capabilities.maxImageExtent.height),
		};
	}();

	auto const [sharing_mode, queue_family_index_count] = [&] {
		if (*m_graphics_queue != *m_present_queue) {
			return std::make_tuple(vk::SharingMode::eConcurrent, 2u);
		}
		return std::make_tuple(vk::SharingMode::eExclusive, 0u);
	}();
	auto const physical_device_queue_family_indices = get_queue_family_indices();
	auto const queue_family_indices = std::array<uint32_t, 2u>{{
		physical_device_queue_family_indices.graphics,
		physical_device_queue_family_indices.present,
	}};

	auto const present_modes = m_physical_device.getSurfacePresentModesKHR(m_surface);
	auto const present_mode_it = std::ranges::find_if(present_modes, [](vk::PresentModeKHR const present_mode) {
		return present_mode == vk::PresentModeKHR::eMailbox;
	});
	auto const present_mode = (present_mode_it != std::cend(present_modes)) ? *present_mode_it : vk::PresentModeKHR::eFifo;

	auto const create_info = vk::SwapchainCreateInfoKHR{
		.surface = m_surface,
		.minImageCount = min_image_count,
		.imageFormat = surface_format.format,
		.imageColorSpace = surface_format.colorSpace,
		.imageExtent = image_extent,
		.imageArrayLayers = 1u,
		.imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
		.imageSharingMode = sharing_mode,
		.queueFamilyIndexCount = queue_family_index_count,
		.pQueueFamilyIndices = std::data(queue_family_indices),
		.preTransform = surface_capabilities.currentTransform,
		.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
		.presentMode = present_mode,
		.clipped = vk::True,
		.oldSwapchain = vk::SwapchainKHR{},
	};

	m_swapchain = vk::raii::SwapchainKHR{ m_device, create_info };
	m_swapchain_images = m_swapchain.getImages();
	m_swapchain_format = surface_format.format;
	m_swapchain_extent = image_extent;
}

void Application::create_image_views() {
	std::ranges::transform(m_swapchain_images, std::back_inserter(m_image_views), [&](vk::Image const swapchain_image) {
		return create_image_view(swapchain_image, m_swapchain_format);
	});
}

void Application::create_descriptor_set_layout() {
	auto const mvp_uniform_buffer_binding = vk::DescriptorSetLayoutBinding{
		.binding = 0u,
		.descriptorType = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = 1u,
		.stageFlags = vk::ShaderStageFlagBits::eVertex,
		.pImmutableSamplers = nullptr,
	};

	auto const texture_sampler_binding = vk::DescriptorSetLayoutBinding{
		.binding = 1u,
		.descriptorType = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = 1u,
		.stageFlags = vk::ShaderStageFlagBits::eFragment,
		.pImmutableSamplers = nullptr,
	};

	auto const bindings = std::array<vk::DescriptorSetLayoutBinding, 2u>{ mvp_uniform_buffer_binding, texture_sampler_binding };
	auto const create_info = vk::DescriptorSetLayoutCreateInfo{
		.bindingCount = static_cast<uint32_t>(std::size(bindings)),
		.pBindings = std::data(bindings),
	};

	m_descriptor_set_layout = vk::raii::DescriptorSetLayout{ m_device, create_info };
}

void Application::create_render_pass() {
	auto const attachment_description = vk::AttachmentDescription{
		.format = m_swapchain_format,
		.samples = vk::SampleCountFlagBits::e1,
		.loadOp = vk::AttachmentLoadOp::eClear,
		.storeOp = vk::AttachmentStoreOp::eStore,
		.stencilLoadOp = vk::AttachmentLoadOp::eDontCare,
		.stencilStoreOp = vk::AttachmentStoreOp::eDontCare,
		.initialLayout = vk::ImageLayout::eUndefined,
		.finalLayout = vk::ImageLayout::ePresentSrcKHR,
	};

	auto const attachment_reference = vk::AttachmentReference{
		.attachment = 0u,
		.layout = vk::ImageLayout::eColorAttachmentOptimal,
	};

	auto const subpass_description = vk::SubpassDescription{
		.pipelineBindPoint = vk::PipelineBindPoint::eGraphics,
		.colorAttachmentCount = 1u,
		.pColorAttachments = &attachment_reference,
	};

	auto const subpass_dependency = vk::SubpassDependency{
		.srcSubpass = vk::SubpassExternal,
		.dstSubpass = 0u,
		.srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
		.dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput,
		.srcAccessMask = vk::AccessFlags{},
		.dstAccessMask = vk::AccessFlagBits::eColorAttachmentWrite,
	};

	auto const create_info = vk::RenderPassCreateInfo{
		.attachmentCount = 1u,
		.pAttachments = &attachment_description,
		.subpassCount = 1u,
		.pSubpasses = &subpass_description,
		.dependencyCount = 1u,
		.pDependencies = &subpass_dependency,
	};

	m_render_pass = vk::raii::RenderPass{ m_device, create_info };
}

void Application::create_graphics_pipeline() {
	auto const vertex_shader_module = create_shader_module("colored_textured.vert");
	auto const vertex_shader_stage_create_info = vk::PipelineShaderStageCreateInfo{
		.stage = vk::ShaderStageFlagBits::eVertex,
		.module = vertex_shader_module,
		.pName = "main",
	};

	auto const fragment_shader_module = create_shader_module("colored_textured.frag");
	auto const fragment_shader_stage_create_info = vk::PipelineShaderStageCreateInfo{
		.stage = vk::ShaderStageFlagBits::eFragment,
		.module = fragment_shader_module,
		.pName = "main",
	};

	auto const shader_stages = std::array<vk::PipelineShaderStageCreateInfo, 2u>{
		vertex_shader_stage_create_info, fragment_shader_stage_create_info,
	};

	auto const vertex_binding_description = Vertex::binding_description();
	auto const vertex_attribute_descriptions = Vertex::attribute_descriptions();
	auto const vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo{
		.vertexBindingDescriptionCount = 1u,
		.pVertexBindingDescriptions = &vertex_binding_description,
		.vertexAttributeDescriptionCount = static_cast<uint32_t>(std::size(vertex_attribute_descriptions)),
		.pVertexAttributeDescriptions = std::data(vertex_attribute_descriptions),
	};

	auto const input_assembly_state_create_info = vk::PipelineInputAssemblyStateCreateInfo{
		.topology = vk::PrimitiveTopology::eTriangleList,
		.primitiveRestartEnable = vk::False,
	};

	auto const viewport_state_create_info = vk::PipelineViewportStateCreateInfo{
		.viewportCount = 1u,
		.scissorCount = 1u,
	};

	auto const rasterization_state_create_info = vk::PipelineRasterizationStateCreateInfo{
		.depthClampEnable = vk::True,
		.rasterizerDiscardEnable = vk::False,
		.polygonMode = vk::PolygonMode::eFill,
		.cullMode = vk::CullModeFlagBits::eBack,
		.frontFace = vk::FrontFace::eCounterClockwise,
		.depthBiasEnable = vk::False,
		.lineWidth = 1.f,
	};

	auto const multisample_state_create_info = vk::PipelineMultisampleStateCreateInfo{
		.rasterizationSamples = vk::SampleCountFlagBits::e1,
		.sampleShadingEnable = vk::False,
	};

	auto const color_blend_attachment_state = vk::PipelineColorBlendAttachmentState{
		.blendEnable = vk::False,
		.colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
			| vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
	};

	auto const color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo{
		.logicOpEnable = vk::False,
		.attachmentCount = 1u,
		.pAttachments = &color_blend_attachment_state,
	};

	auto const dynamic_states = std::array<vk::DynamicState, 2u>{{
		vk::DynamicState::eViewport,
		vk::DynamicState::eScissor,
	}};
	auto const dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo{
		.dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states)),
		.pDynamicStates = std::data(dynamic_states),
	};

	auto const pipeline_layout_create_info = vk::PipelineLayoutCreateInfo{
		.setLayoutCount = 1u,
		.pSetLayouts = &*m_descriptor_set_layout,
	};

	m_pipeline_layout = vk::raii::PipelineLayout{ m_device, pipeline_layout_create_info };

	auto const create_info = vk::GraphicsPipelineCreateInfo{
		.stageCount = static_cast<uint32_t>(std::size(shader_stages)),
		.pStages = std::data(shader_stages),
		.pVertexInputState = &vertex_input_state_create_info,
		.pInputAssemblyState = &input_assembly_state_create_info,
		.pTessellationState = nullptr,
		.pViewportState = &viewport_state_create_info,
		.pRasterizationState = &rasterization_state_create_info,
		.pMultisampleState = &multisample_state_create_info,
		.pDepthStencilState = nullptr,
		.pColorBlendState = &color_blend_state_create_info,
		.pDynamicState = &dynamic_state_create_info,
		.layout = m_pipeline_layout,
		.renderPass = m_render_pass,
		.subpass = 0u,
	};

	m_graphics_pipeline = vk::raii::Pipeline{ m_device, { nullptr }, create_info };
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

vk::raii::ShaderModule Application::create_shader_module(std::string shader) const {
	auto const spirv_path = get_spirv_shader_path(std::move(shader));
	auto const code = read_binary_file(spirv_path);
	if (!code) {
		throw std::runtime_error{ "cannot read \"" + spirv_path + '"' };
	}

	auto const create_info = vk::ShaderModuleCreateInfo{
		.codeSize = std::size(*code),
		.pCode = reinterpret_cast<uint32_t const*>(std::data(*code)),
	};

	return vk::raii::ShaderModule{ m_device, create_info };
}

void Application::create_framebuffers() {
	std::ranges::transform(m_image_views, std::back_inserter(m_framebuffers), [&](vk::raii::ImageView const& image_view) {
		auto const create_info = vk::FramebufferCreateInfo{
			.renderPass = m_render_pass,
			.attachmentCount = 1u,
			.pAttachments = &*image_view,
			.width = m_swapchain_extent.width,
			.height = m_swapchain_extent.height,
			.layers = 1u,
		};

		return vk::raii::Framebuffer{ m_device, create_info };
	});
}

void Application::create_command_pool() {
	auto const create_info = vk::CommandPoolCreateInfo{
		.flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
		.queueFamilyIndex = get_queue_family_indices().graphics,
	};

	m_command_pool = vk::raii::CommandPool{ m_device, create_info };
}

void Application::create_vertex_buffer() {
	auto const buffer_size = std::size(VERTICES) * sizeof(VERTICES[0]);

	auto const [staging_buffer, staging_buffer_memory] = create_buffer(buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	auto* const data = staging_buffer_memory.mapMemory(0u, buffer_size);
	std::memcpy(data, std::data(VERTICES), buffer_size);
	staging_buffer_memory.unmapMemory();

	std::tie(m_vertex_buffer, m_vertex_buffer_memory) = create_buffer(buffer_size,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	copy_buffer(staging_buffer, m_vertex_buffer, buffer_size);
}

void Application::create_index_buffer() {
	auto const buffer_size = std::size(INDICES) * sizeof(INDICES[0]);

	auto const [staging_buffer, staging_buffer_memory] = create_buffer(buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	auto* const data = staging_buffer_memory.mapMemory(0u, buffer_size);
	std::memcpy(data, std::data(INDICES), buffer_size);
	staging_buffer_memory.unmapMemory();

	std::tie(m_index_buffer, m_index_buffer_memory) = create_buffer(buffer_size,
		vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eIndexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal);

	copy_buffer(staging_buffer, m_index_buffer, buffer_size);
}

void Application::create_texture_image() {
	auto width = int{};
	auto height = int{};
	auto pixels = stbi_load(get_asset_path("textures/statue.jpg").c_str(),
		&width, &height, nullptr, STBI_rgb_alpha);
	if (!pixels) {
		throw std::runtime_error{ "failed to load texture" };
	}
	auto const image_size = static_cast<vk::DeviceSize>(width * height * STBI_rgb_alpha);

	auto const [staging_buffer, staging_buffer_memory] = create_buffer(image_size, vk::BufferUsageFlagBits::eTransferSrc,
		vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

	auto* const data = staging_buffer_memory.mapMemory(0u, image_size);
	std::memcpy(data, pixels, image_size);
	staging_buffer_memory.unmapMemory();

	stbi_image_free(pixels);

	std::tie(m_texture_image, m_texture_image_memory) = create_image(vk::Format::eR8G8B8A8Srgb, static_cast<uint32_t>(width),
		static_cast<uint32_t>(height), vk::ImageTiling::eOptimal, vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled,
		vk::MemoryPropertyFlagBits::eDeviceLocal);

	transition_image_layout(m_texture_image, vk::ImageLayout::eUndefined, vk::ImageLayout::eTransferDstOptimal);
	copy_buffer_to_image(staging_buffer, m_texture_image, static_cast<uint32_t>(width), static_cast<uint32_t>(height));
	transition_image_layout(m_texture_image, vk::ImageLayout::eTransferDstOptimal, vk::ImageLayout::eShaderReadOnlyOptimal);
}

void Application::create_texture_image_view() {
	m_texture_image_view = create_image_view(m_texture_image, vk::Format::eR8G8B8A8Srgb);
}

void Application::create_texture_sampler() {
	auto const create_info = vk::SamplerCreateInfo{
		.magFilter = vk::Filter::eLinear,
		.minFilter = vk::Filter::eLinear,
		.addressModeU = vk::SamplerAddressMode::eRepeat,
		.addressModeV = vk::SamplerAddressMode::eRepeat,
		.addressModeW = vk::SamplerAddressMode::eRepeat,
		.mipLodBias = 0.f,
		.anisotropyEnable = vk::False,
		.maxAnisotropy = 1.f,
		.compareEnable = vk::False,
		.compareOp = vk::CompareOp::eAlways,
		.minLod = 0.f,
		.maxLod = 0.f,
		.borderColor = vk::BorderColor::eIntOpaqueBlack,
		.unnormalizedCoordinates = vk::False,
	};

	m_texture_sampler = vk::raii::Sampler{ m_device, create_info };
}

void Application::create_uniform_buffers() {
	m_mvp_uniform_buffers.reserve(MAX_FRAMES_IN_FLIGHT);
	m_mvp_uniform_buffer_memories.reserve(MAX_FRAMES_IN_FLIGHT);
	auto const buffer_size = sizeof(MvpUniformBuffer);
	for (auto i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		auto [buffer, buffer_memory] = create_buffer(buffer_size, vk::BufferUsageFlagBits::eUniformBuffer,
			vk::MemoryPropertyFlagBits::eHostVisible | vk::MemoryPropertyFlagBits::eHostCoherent);

		m_mvp_uniform_buffers.emplace_back(std::move(buffer));
		m_mvp_uniform_buffer_memories.emplace_back(std::move(buffer_memory));
		m_mvp_uniform_buffer_maps[i] = m_mvp_uniform_buffer_memories.back().mapMemory(0u, buffer_size);
	}
}

void Application::create_descriptor_pool() {
	auto const mvp_uniform_buffer_pool_size = vk::DescriptorPoolSize{
		.type = vk::DescriptorType::eUniformBuffer,
		.descriptorCount = MAX_FRAMES_IN_FLIGHT,
	};
	auto const texture_sampler_pool_size = vk::DescriptorPoolSize{
		.type = vk::DescriptorType::eCombinedImageSampler,
		.descriptorCount = MAX_FRAMES_IN_FLIGHT,
	};
	auto const pool_sizes = std::array<vk::DescriptorPoolSize, 2u>{ mvp_uniform_buffer_pool_size, texture_sampler_pool_size };

	auto const create_info = vk::DescriptorPoolCreateInfo{
		.flags = vk::DescriptorPoolCreateFlagBits::eFreeDescriptorSet,
		.maxSets = MAX_FRAMES_IN_FLIGHT,
		.poolSizeCount = static_cast<uint32_t>(std::size(pool_sizes)),
		.pPoolSizes = std::data(pool_sizes),
	};

	m_descriptor_pool = vk::raii::DescriptorPool{ m_device, create_info };
}

void Application::create_descriptor_sets() {
	auto layouts = std::array<vk::DescriptorSetLayout, MAX_FRAMES_IN_FLIGHT>{};
	layouts.fill(m_descriptor_set_layout);

	auto const allocate_info = vk::DescriptorSetAllocateInfo{
		.descriptorPool = m_descriptor_pool,
		.descriptorSetCount = static_cast<uint32_t>(std::size(layouts)),
		.pSetLayouts = std::data(layouts),
	};
	m_descriptor_sets = vk::raii::DescriptorSets{ m_device, allocate_info };

	auto i = 0u;
	for (auto const& descriptor_set : m_descriptor_sets) {
		auto const mvp_uniform_buffer_info = vk::DescriptorBufferInfo{
			.buffer = m_mvp_uniform_buffers[i],
			.offset = 0u,
			.range = vk::WholeSize,
		};

		auto const texture_image_info = vk::DescriptorImageInfo{
			.sampler = m_texture_sampler,
			.imageView = m_texture_image_view,
			.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal,
		};

		auto const mvp_uniform_buffer_descriptor_write = vk::WriteDescriptorSet{
			.dstSet = descriptor_set,
			.dstBinding = 0u,
			.dstArrayElement = 0u,
			.descriptorCount = 1u,
			.descriptorType = vk::DescriptorType::eUniformBuffer,
			.pBufferInfo = &mvp_uniform_buffer_info,
		};

		auto const texture_image_descriptor_write = vk::WriteDescriptorSet{
			.dstSet = descriptor_set,
			.dstBinding = 1u,
			.dstArrayElement = 0u,
			.descriptorCount = 1u,
			.descriptorType = vk::DescriptorType::eCombinedImageSampler,
			.pImageInfo = &texture_image_info,
		};

		auto const descriptor_writes = std::array<vk::WriteDescriptorSet, 2u>{
			mvp_uniform_buffer_descriptor_write,
			texture_image_descriptor_write,
		};

		m_device.updateDescriptorSets(descriptor_writes, {});
		i += 1u;
	}
}

void Application::create_command_buffers() {
	auto const allocate_info = vk::CommandBufferAllocateInfo{
		.commandPool = m_command_pool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = MAX_FRAMES_IN_FLIGHT,
	};
	m_command_buffers = vk::raii::CommandBuffers{ m_device, allocate_info };
}

void Application::create_sync_objects() {
	m_image_available_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
	m_render_finished_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
	m_in_flight_fences.reserve(MAX_FRAMES_IN_FLIGHT);
	auto const in_flight_fence_create_info = vk::FenceCreateInfo{
		.flags = vk::FenceCreateFlagBits::eSignaled,
	};
	for (auto i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
		m_image_available_semaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
		m_render_finished_semaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
		m_in_flight_fences.emplace_back(m_device, in_flight_fence_create_info);
	}
}

void Application::draw_frame() {
	auto const& in_flight_fence = m_in_flight_fences[m_current_in_flight_frame_index];
	auto const& image_available_semaphore = m_image_available_semaphores[m_current_in_flight_frame_index];
	auto const& command_buffer = m_command_buffers[m_current_in_flight_frame_index];
	auto const& render_finished_semaphore = m_render_finished_semaphores[m_current_in_flight_frame_index];

	static_cast<void>(m_device.waitForFences(*in_flight_fence, vk::True, std::numeric_limits<uint64_t>::max()));

	auto image_index = uint32_t{};
	try {
		image_index = m_swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), image_available_semaphore).second;
	} catch (vk::OutOfDateKHRError const&) {
		recreate_swapchain();
		return;
	}

	update_uniform_buffer(m_mvp_uniform_buffer_maps[m_current_in_flight_frame_index]);

	m_device.resetFences(*in_flight_fence);

	command_buffer.reset();
	record_command_buffer(command_buffer, image_index);

	auto const wait_stage = vk::PipelineStageFlags{ vk::PipelineStageFlagBits::eColorAttachmentOutput };
	auto const submit_info = vk::SubmitInfo{
		.waitSemaphoreCount = 1u,
		.pWaitSemaphores = &*image_available_semaphore,
		.pWaitDstStageMask = &wait_stage,
		.commandBufferCount = 1u,
		.pCommandBuffers = &*command_buffer,
		.signalSemaphoreCount = 1u,
		.pSignalSemaphores = &*render_finished_semaphore,
	};

	m_graphics_queue.submit(submit_info, in_flight_fence);

	auto const present_info = vk::PresentInfoKHR{
		.waitSemaphoreCount = 1u,
		.pWaitSemaphores = &*render_finished_semaphore,
		.swapchainCount = 1u,
		.pSwapchains = &*m_swapchain,
		.pImageIndices = &image_index,
	};

	try {
		auto const result = m_graphics_queue.presentKHR(present_info);
		if (result == vk::Result::eSuboptimalKHR) {
			m_should_recreate_swapchain = true;
		}
	} catch (vk::OutOfDateKHRError const&) {
		m_should_recreate_swapchain = true;
	}
	if (m_should_recreate_swapchain) {
		recreate_swapchain();
	}

	m_current_in_flight_frame_index = (m_current_in_flight_frame_index + 1u) % MAX_FRAMES_IN_FLIGHT;
}

void Application::update_uniform_buffer(void* uniform_buffer_map) const {
	static auto const start_time = std::chrono::high_resolution_clock::now();
	auto const current_time = std::chrono::high_resolution_clock::now();
	auto const time = std::chrono::duration<float, std::chrono::seconds::period>{ current_time - start_time }.count();
	auto const aspect_ratio = static_cast<float>(m_swapchain_extent.width) / static_cast<float>(m_swapchain_extent.height);

	auto const mvp = MvpUniformBuffer{
		.model = glm::rotate(glm::mat4{ 1.f }, time * glm::radians(90.f), glm::vec3{ 0.f, 0.f, 1.f }),
		.view = glm::lookAt(glm::vec3{ 0.f, 2.f, 2.f }, glm::vec3{ 0.f }, glm::vec3{ 0.f, 1.f, 0.f }),
		.projection = glm::perspective(glm::radians(60.f), aspect_ratio, 0.01f, 100.f),
	};
	std::memcpy(uniform_buffer_map, &mvp, sizeof(mvp));
}

void Application::record_command_buffer(vk::CommandBuffer const command_buffer, uint32_t const image_index) {
	command_buffer.begin(vk::CommandBufferBeginInfo{});

	auto const clear_value = vk::ClearValue{ .color = vk::ClearColorValue{ .float32 = std::array<float, 4u>{{ 0.f, 0.f, 0.f, 1.f }} } };
	auto const render_pass_begin_info = vk::RenderPassBeginInfo{
		.renderPass = m_render_pass,
		.framebuffer = m_framebuffers[image_index],
		.renderArea = vk::Rect2D{
			.offset = vk::Offset2D{ 0, 0, },
			.extent = m_swapchain_extent,
		},
		.clearValueCount = 1u,
		.pClearValues = &clear_value,
	};

	command_buffer.beginRenderPass(render_pass_begin_info, vk::SubpassContents::eInline);

	command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphics_pipeline);

	auto const viewport = vk::Viewport{
		.x = 0.f,
		.y = static_cast<float>(m_swapchain_extent.height),
		.width = static_cast<float>(m_swapchain_extent.width),
		.height = -static_cast<float>(m_swapchain_extent.height),
		.minDepth = 0.f,
		.maxDepth = 1.f,
	};
	command_buffer.setViewport(0u, viewport);

	auto const scissor = vk::Rect2D{
		.offset = vk::Offset2D{ 0, 0, },
		.extent = m_swapchain_extent,
	};
	command_buffer.setScissor(0u, scissor);

	command_buffer.bindVertexBuffers(0u, *m_vertex_buffer, vk::DeviceSize{ 0u });
	command_buffer.bindIndexBuffer(m_index_buffer, 0u, vk::IndexType::eUint16);
	command_buffer.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, m_pipeline_layout, 0u, *m_descriptor_sets[m_current_in_flight_frame_index], {});
	command_buffer.drawIndexed(static_cast<uint32_t>(std::size(INDICES)), 1u, 0u, 0u, 0u);

	command_buffer.endRenderPass();
	command_buffer.end();
}

void Application::recreate_swapchain() {
	auto width = int{};
	auto height = int{};
	glfwGetFramebufferSize(m_window, &width, &height);
	while (width == 0 || height == 0) {
		glfwWaitEvents();
		glfwGetFramebufferSize(m_window, &width, &height);
	}
	m_device.waitIdle();

	clean_swapchain();

	create_swapchain();
	create_image_views();
	create_framebuffers();

	m_should_recreate_swapchain = false;
}

void Application::clean_swapchain() {
	m_framebuffers.clear();
	m_image_views.clear();
	m_swapchain_images.clear();
	m_swapchain.clear();
}

std::tuple<vk::raii::Buffer, vk::raii::DeviceMemory> Application::create_buffer(vk::DeviceSize const size,
	vk::BufferUsageFlags const usage, vk::MemoryPropertyFlags const memory_property_flags) const {
	auto const create_info = vk::BufferCreateInfo{
		.size = size,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive,
	};

	auto buffer = vk::raii::Buffer{ m_device, create_info };

	auto const memory_requirements = buffer.getMemoryRequirements();
	auto const memory_allocate_info = vk::MemoryAllocateInfo{
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, memory_property_flags),
	};

	auto buffer_memory = vk::raii::DeviceMemory{ m_device, memory_allocate_info };
	buffer.bindMemory(buffer_memory, 0u);
	
	return std::make_tuple(std::move(buffer), std::move(buffer_memory));
}

void Application::copy_buffer(vk::Buffer const src, vk::Buffer const dst, vk::DeviceSize size) const {
	one_time_command([=](vk::CommandBuffer const command_buffer) {
		auto const copy_region = vk::BufferCopy{ .size = size };
		command_buffer.copyBuffer(src, dst, copy_region);
	});
}

std::tuple<vk::raii::Image, vk::raii::DeviceMemory> Application::create_image(vk::Format const format, uint32_t const width, uint32_t const height,
	vk::ImageTiling const tiling, vk::ImageUsageFlags const usage, vk::MemoryPropertyFlags const memory_property_flags) const {
	auto const create_info = vk::ImageCreateInfo{
		.imageType = vk::ImageType::e2D,
		.format = format,
		.extent = vk::Extent3D{ width, height, 1u },
		.mipLevels = 1u,
		.arrayLayers = 1u,
		.samples = vk::SampleCountFlagBits::e1,
		.tiling = tiling,
		.usage = usage,
		.sharingMode = vk::SharingMode::eExclusive,
	};

	auto image = vk::raii::Image{ m_device, create_info };

	auto const memory_requirements = image.getMemoryRequirements();
	auto const memory_allocate_info = vk::MemoryAllocateInfo{
		.allocationSize = memory_requirements.size,
		.memoryTypeIndex = find_memory_type(memory_requirements.memoryTypeBits, memory_property_flags),
	};

	auto image_memory = vk::raii::DeviceMemory{ m_device, memory_allocate_info };
	image.bindMemory(image_memory, 0u);

	return std::make_tuple(std::move(image), std::move(image_memory));
}

void Application::transition_image_layout(vk::Image const image, vk::ImageLayout const old_layout, vk::ImageLayout const new_layout) const {
	one_time_command([=](vk::CommandBuffer const command_buffer) {
		auto memory_barrier = vk::ImageMemoryBarrier{
			.oldLayout = old_layout,
			.newLayout = new_layout,
			.srcQueueFamilyIndex = vk::QueueFamilyIgnored,
			.dstQueueFamilyIndex = vk::QueueFamilyIgnored,
			.image = image,
			.subresourceRange = vk::ImageSubresourceRange{
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.baseMipLevel = 0u,
				.levelCount = 1u,
				.baseArrayLayer = 0u,
				.layerCount = 1u,
			},
		};

		auto src_stage = vk::PipelineStageFlags{};
		auto dst_stage = vk::PipelineStageFlags{};
		if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
			memory_barrier.srcAccessMask = vk::AccessFlags{};
			memory_barrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
			src_stage = vk::PipelineStageFlagBits::eTopOfPipe;
			dst_stage = vk::PipelineStageFlagBits::eTransfer;
		} else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
			memory_barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
			memory_barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
			src_stage = vk::PipelineStageFlagBits::eTransfer;
			dst_stage = vk::PipelineStageFlagBits::eFragmentShader;
		} else {
			assert(false && "unsupported image layout transition");
		}

		command_buffer.pipelineBarrier(src_stage, dst_stage, vk::DependencyFlags{}, {}, {}, memory_barrier);
	});
}

void Application::copy_buffer_to_image(vk::Buffer const src, vk::Image const dst, uint32_t const width, uint32_t const height) const {
	one_time_command([=](vk::CommandBuffer const command_buffer) {
		auto const copy_region = vk::BufferImageCopy{
			.bufferOffset = 0u,
			.bufferRowLength = 0u,
			.bufferImageHeight = 0u,
			.imageSubresource = vk::ImageSubresourceLayers{
				.aspectMask = vk::ImageAspectFlagBits::eColor,
				.mipLevel = 0u,
				.baseArrayLayer = 0u,
				.layerCount = 1u,
			},
			.imageOffset = vk::Offset3D{ 0u, 0u, 0u },
			.imageExtent = vk::Extent3D{ width, height, 1u },
		};

		command_buffer.copyBufferToImage(src, dst, vk::ImageLayout::eTransferDstOptimal, copy_region);
	});
}

vk::raii::ImageView Application::create_image_view(vk::Image const image, vk::Format const format) const {
	auto const create_info = vk::ImageViewCreateInfo{
		.image = image,
		.viewType = vk::ImageViewType::e2D,
		.format = format,
		.components = vk::ComponentMapping{
			.r = vk::ComponentSwizzle::eIdentity,
			.g = vk::ComponentSwizzle::eIdentity,
			.b = vk::ComponentSwizzle::eIdentity,
			.a = vk::ComponentSwizzle::eIdentity,
		},
		.subresourceRange = vk::ImageSubresourceRange{
			.aspectMask = vk::ImageAspectFlagBits::eColor,
			.baseMipLevel = 0u,
			.levelCount = 1u,
			.baseArrayLayer = 0u,
			.layerCount = 1u,
		},
	};

	return vk::raii::ImageView{ m_device, create_info };
}

uint32_t Application::find_memory_type(uint32_t const type_bits, vk::MemoryPropertyFlags const property_flags) const {
	auto const memory_properties = m_physical_device.getMemoryProperties();

	auto i = 0u;
	auto const memory_types = std::span{ std::data(memory_properties.memoryTypes), memory_properties.memoryTypeCount };
	for (auto const memory_type : memory_types) {
		if ((type_bits & (1 << i)) != 0 && (memory_type.propertyFlags & property_flags) == property_flags) {
			return i;
		}
		i += 1u;
	}

	auto const message = std::format(
		"physical device lacks memory type with bits: {:#b} and vk::MemoryPropertyFlags: {}",
		type_bits, vk::to_string(property_flags));
	throw std::runtime_error{ message };
}

template<typename Function>
void Application::one_time_command(Function function) const {
	auto const command_buffer_allocate_info = vk::CommandBufferAllocateInfo{
		.commandPool = m_command_pool,
		.level = vk::CommandBufferLevel::ePrimary,
		.commandBufferCount = 1u,
	};

	auto const command_buffer = std::move(vk::raii::CommandBuffers{ m_device, command_buffer_allocate_info }.front());

	auto const command_buffer_begin_info = vk::CommandBufferBeginInfo{
		.flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
	};
	command_buffer.begin(command_buffer_begin_info);

	function(*command_buffer);

	command_buffer.end();

	auto const submit_info = vk::SubmitInfo{
		.commandBufferCount = 1u,
		.pCommandBuffers = &*command_buffer,
	};

	m_graphics_queue.submit(submit_info);
	m_graphics_queue.waitIdle();
}
