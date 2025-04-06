#pragma once

#include "Window.hpp"
#include "Camera.hpp"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

#include <string_view>
#include <vector>
#include <array>
#include <cstdint>

class Application {
public:
	Application();
	Application(Application const&) = delete;

	Application& operator=(Application const&) = delete;

	void run();

private:
	void init_window();
	void init_vulkan();

	void create_instance();
	static bool has_instance_layer(vk::raii::Context const& context, std::string_view layer_name);
	static bool has_instance_extension(vk::raii::Context const& context, std::string_view extension_name);

	void create_debug_messenger();
	static vk::DebugUtilsMessengerCreateInfoEXT get_debug_messenger_create_info();
	static VKAPI_ATTR VkBool32 VKAPI_CALL debug_utils_messenger_callback(VkDebugUtilsMessageSeverityFlagBitsEXT message_severity,
		VkDebugUtilsMessageTypeFlagsEXT message_types, VkDebugUtilsMessengerCallbackDataEXT const* callback_data, void* user_data);

	void create_surface();

	void select_physical_device();
	uint32_t get_physical_device_score(vk::PhysicalDevice physical_device) const;

	struct QueueFamilyIndices {
		uint32_t graphics;
		uint32_t present;
	};
	void create_device();
	QueueFamilyIndices get_queue_family_indices() const;
	static bool has_device_layer(vk::PhysicalDevice physical_device, std::string_view layer_name);
	static bool has_device_extension(vk::PhysicalDevice physical_device, std::string_view extension_name);

	void create_swapchain();
	void create_image_views();

	void create_descriptor_set_layout();
	void create_render_pass();
	void create_graphics_pipeline();
	vk::raii::ShaderModule create_shader_module(std::string_view shader) const;

	void create_framebuffers();

	void create_command_pool();
	void create_command_buffers();

	void create_voxels_shader_storage_buffer();
	void create_descriptor_pool();
	void create_descriptor_sets();

	void create_sync_objects();

	void draw_frame();
	void record_command_buffer(vk::CommandBuffer command_buffer, uint32_t image_index);

	void recreate_swapchain();
	void clean_swapchain();

	std::tuple<vk::raii::Buffer, vk::raii::DeviceMemory> create_buffer(vk::DeviceSize size,
		vk::BufferUsageFlags usage, vk::MemoryPropertyFlags memory_property_flags) const;
	void copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) const;

	std::tuple<vk::raii::Image, vk::raii::DeviceMemory> create_image(vk::Format format, uint32_t width, uint32_t height,
		vk::ImageTiling tiling, vk::ImageUsageFlags usage, vk::MemoryPropertyFlags memory_property_flags) const;
	void transition_image_layout(vk::Image image, vk::ImageLayout old_layout, vk::ImageLayout new_layout) const;
	void copy_buffer_to_image(vk::Buffer src, vk::Image dst, uint32_t width, uint32_t height) const;

	vk::raii::ImageView create_image_view(vk::Image image, vk::Format format) const;

	uint32_t find_memory_type(uint32_t type_bits, vk::MemoryPropertyFlags property_flags) const;

	void one_time_commands(std::invocable<vk::CommandBuffer> auto commands_recorder) const;

private:
	constexpr static auto MAX_FRAMES_IN_FLIGHT = 2u;

	Window m_window = Window{ "Vulkan Playground", 1280u, 720u };
	bool m_should_recreate_swapchain = false;
	vk::raii::Context m_context;
	vk::raii::Instance m_instance = { nullptr };
	vk::raii::DebugUtilsMessengerEXT m_debug_messenger = { nullptr };
	vk::raii::SurfaceKHR m_surface = { nullptr };

	constexpr static std::array<char const*, 1u> DEVICE_REQUIRED_EXTENSIONS = std::to_array({
		vk::KHRSwapchainExtensionName,
	});
	vk::raii::PhysicalDevice m_physical_device = { nullptr };
	vk::raii::Device m_device = { nullptr };
	vk::raii::Queue m_graphics_queue = { nullptr };
	vk::raii::Queue m_present_queue = { nullptr };

	vk::raii::SwapchainKHR m_swapchain = { nullptr };
	std::vector<vk::Image> m_swapchain_images;
	vk::Format m_swapchain_format;
	vk::Extent2D m_swapchain_extent;
	std::vector<vk::raii::ImageView> m_image_views;
	std::vector<vk::raii::Framebuffer> m_framebuffers;

	vk::raii::DescriptorSetLayout m_descriptor_set_layout = { nullptr };
	vk::raii::RenderPass m_render_pass = { nullptr };
	vk::raii::PipelineLayout m_pipeline_layout = { nullptr };
	vk::raii::Pipeline m_graphics_pipeline = { nullptr };

	vk::raii::CommandPool m_command_pool = { nullptr };
	vk::raii::CommandBuffers m_command_buffers = { nullptr };

	vk::raii::Buffer m_voxels_storage_buffer = { nullptr };
	vk::raii::DeviceMemory m_voxels_storage_buffer_memory = { nullptr };

	vk::raii::DescriptorPool m_descriptor_pool = { nullptr };
	vk::raii::DescriptorSets m_descriptor_sets = { nullptr };

	std::vector<vk::raii::Semaphore> m_image_available_semaphores;
	std::vector<vk::raii::Semaphore> m_render_finished_semaphores;
	std::vector<vk::raii::Fence> m_in_flight_fences;
	uint8_t m_current_in_flight_frame_index = 0u;

	Camera m_camera = Camera{ glm::vec3{ 900.21f, 601.345f, 800.01f }, glm::vec2{ glm::radians(89.5898f), 0.f } };
	uint32_t m_octree_depth;
};
