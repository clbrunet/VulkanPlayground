#pragma once

#include <GLFW/glfw3.h>

#include <string_view>
#include <vector>
#include <array>
#include <cstdint>
#include <stdexcept>

class Application {
public:
	Application();
	~Application();

	void run();

	void set_has_window_been_resized();

private:
	void init_window();
	void init_vulkan();

	void create_instance();
	static bool has_instance_layer(std::string_view layer_name);

	void create_surface();

	void select_physical_device();
	uint32_t get_physical_device_score(VkPhysicalDevice physical_device) const;

	struct QueueFamilyIndices {
		uint32_t graphics;
		uint32_t present;
	};
	void create_device();
	QueueFamilyIndices get_queue_family_indices() const;
	static bool has_device_layer(VkPhysicalDevice physical_device, std::string_view const layer_name);
	static bool has_device_extension(VkPhysicalDevice physical_device, std::string_view const extension_name);

	void create_swapchain();
	void create_image_views();

	void create_descriptor_set_layout();
	void create_render_pass();
	void create_graphics_pipeline();
	VkShaderModule create_shader_module(std::string shader) const;

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
	void record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index);

	void recreate_swapchain();
	void clean_swapchain();

	void create_buffer(VkDeviceSize size, VkBufferUsageFlags usage,
		VkMemoryPropertyFlags memory_property_flags, VkBuffer& buffer, VkDeviceMemory& buffer_memory) const;
	void copy_buffer(VkBuffer src, VkBuffer dst, VkDeviceSize size) const;

	void create_image(VkFormat format, uint32_t width, uint32_t height, VkImageTiling tiling, VkImageUsageFlags usage,
		VkMemoryPropertyFlags memory_property_flags, VkImage& image, VkDeviceMemory& image_memory);
	void transition_image_layout(VkImage image, VkImageLayout old_layout, VkImageLayout new_layout);
	void copy_buffer_to_image(VkBuffer src, VkImage dst, uint32_t width, uint32_t height) const;

	VkImageView create_image_view(VkImage image, VkFormat format);

	uint32_t find_memory_type(uint32_t type_bits, VkMemoryPropertyFlags property_flags) const;

	template<typename Function>
	void one_time_command(Function function) const;

private:
	constexpr static auto MAX_FRAMES_IN_FLIGHT = 2u;

	GLFWwindow* m_window;
	bool m_has_window_been_resized = false;
	VkInstance m_instance;
	VkSurfaceKHR m_surface;

	VkPhysicalDevice m_physical_device;
	VkDevice m_device;
	VkQueue m_graphics_queue;
	VkQueue m_present_queue;

	VkSwapchainKHR m_swapchain;
	std::vector<VkImage> m_swapchain_images;
	VkFormat m_swapchain_format;
	VkExtent2D m_swapchain_extent;
	std::vector<VkImageView> m_image_views;
	std::vector<VkFramebuffer> m_framebuffers;

	VkDescriptorSetLayout m_descriptor_set_layout;
	VkRenderPass m_render_pass;
	VkPipelineLayout m_pipeline_layout;
	VkPipeline m_graphics_pipeline;

	VkCommandPool m_command_pool;

	VkBuffer m_vertex_buffer;
	VkDeviceMemory m_vertex_buffer_memory;
	VkBuffer m_index_buffer;
	VkDeviceMemory m_index_buffer_memory;

	VkImage m_texture_image;
	VkDeviceMemory m_texture_image_memory;
	VkImageView m_texture_image_view;
	VkSampler m_texture_sampler;

	std::array<VkBuffer, MAX_FRAMES_IN_FLIGHT> m_mvp_uniform_buffers;
	std::array<VkDeviceMemory, MAX_FRAMES_IN_FLIGHT> m_mvp_uniform_buffer_memories;
	std::array<void*, MAX_FRAMES_IN_FLIGHT> m_mvp_uniform_buffer_maps;

	VkDescriptorPool m_descriptor_pool;
	std::array<VkDescriptorSet, MAX_FRAMES_IN_FLIGHT> m_descriptor_sets;

	std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_command_buffers;

	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_image_available_semaphores;
	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_render_finished_semaphores;
	std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_in_flight_fences;
	uint8_t m_current_in_flight_frame_index = 0u;
};
