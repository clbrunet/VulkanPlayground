#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_raii.hpp>

#include <string_view>
#include <vector>
#include <array>
#include <cstdint>
#include <stdexcept>

class Application {
public:
	Application();
	Application(Application const&) = delete;
	~Application();
	
	Application& operator=(Application const&) = delete;

	void run();

	void set_has_window_been_resized();

private:
	void init_window();
	void init_vulkan();

	void create_instance();
	static bool has_instance_layer(vk::raii::Context const& context, std::string_view layer_name);
	static bool has_instance_extension(vk::raii::Context const& context, std::string_view extension_name);

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
	vk::raii::ShaderModule create_shader_module(std::string shader) const;

	void create_framebuffers();

	void create_command_pool();

	void create_vertex_buffer();
	void create_index_buffer();

	void create_texture_image();
	void create_texture_image_view();
	void create_texture_sampler();

	void create_uniform_buffers();

	void create_descriptor_pool();
	void create_descriptor_sets();

	void create_command_buffers();

	void create_sync_objects();

	void draw_frame();
	void update_uniform_buffer(void* uniform_buffer_map) const;
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

	template<typename Function>
	void one_time_command(Function function) const;

private:
	constexpr static auto MAX_FRAMES_IN_FLIGHT = 2u;

	GLFWwindow* m_window;
	bool m_should_recreate_swapchain = false;
	vk::raii::Context m_context;
	vk::raii::Instance m_instance = { nullptr };
	vk::raii::SurfaceKHR m_surface = { nullptr };

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

	vk::raii::Buffer m_vertex_buffer = { nullptr };
	vk::raii::DeviceMemory m_vertex_buffer_memory = { nullptr };
	vk::raii::Buffer m_index_buffer = { nullptr };
	vk::raii::DeviceMemory m_index_buffer_memory = { nullptr };

	vk::raii::Image m_texture_image = { nullptr };
	vk::raii::DeviceMemory m_texture_image_memory = { nullptr };
	vk::raii::ImageView m_texture_image_view = { nullptr };
	vk::raii::Sampler m_texture_sampler = { nullptr };

	std::vector<vk::raii::Buffer> m_mvp_uniform_buffers;
	std::vector<vk::raii::DeviceMemory> m_mvp_uniform_buffer_memories;
	std::array<void*, MAX_FRAMES_IN_FLIGHT> m_mvp_uniform_buffer_maps;

	vk::raii::DescriptorPool m_descriptor_pool = { nullptr };
	std::vector<vk::raii::DescriptorSet> m_descriptor_sets;

	std::vector<vk::raii::CommandBuffer> m_command_buffers;

	std::vector<vk::raii::Semaphore> m_image_available_semaphores;
	std::vector<vk::raii::Semaphore> m_render_finished_semaphores;
	std::vector<vk::raii::Fence> m_in_flight_fences;
	uint8_t m_current_in_flight_frame_index = 0u;
};
