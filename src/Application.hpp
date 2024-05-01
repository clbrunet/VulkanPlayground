#pragma once

#include <GLFW/glfw3.h>

#include <string_view>
#include <vector>
#include <array>
#include <cstdint>

class Application {
public:
	Application();
	~Application();

	void run();

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

	void create_render_pass();
	void create_graphics_pipeline();
	VkShaderModule create_shader_module(std::string shader) const;

	void create_framebuffers();

	void create_command_pool();
	void create_command_buffers();

	void create_sync_objects();

	void draw_frame();

	void record_command_buffer(VkCommandBuffer command_buffer, uint32_t image_index);

private:
	static constexpr auto MAX_FRAMES_IN_FLIGHT = 2u;

	GLFWwindow* m_window;
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
	VkRenderPass m_render_pass;
	VkPipelineLayout m_pipeline_layout;
	VkPipeline m_graphics_pipeline;
	std::vector<VkFramebuffer> m_framebuffers;
	VkCommandPool m_command_pool;
	std::array<VkCommandBuffer, MAX_FRAMES_IN_FLIGHT> m_command_buffers;
	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_image_available_semaphores;
	std::array<VkSemaphore, MAX_FRAMES_IN_FLIGHT> m_render_finished_semaphores;
	std::array<VkFence, MAX_FRAMES_IN_FLIGHT> m_in_flight_fences;
	uint8_t m_current_in_flight_frame_index = 0u;
};
