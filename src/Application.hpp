#pragma once

#include "Window.hpp"
#include "ImGuiWrapper.hpp"
#include "Camera.hpp"
#include "Tree64.hpp"
#include "vulkan_utils.hpp"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

#include <string_view>
#include <vector>
#include <array>
#include <cstdint>
#include <future>

namespace vp {

class Application {
public:
    Application();
    Application(Application const&) = delete;

    ~Application();

    Application& operator=(Application const&) = delete;

    void run();

private:
    void start_model_import();

    void init_window();

    void init_vulkan();

    void create_instance();
    static bool has_instance_layer(vk::raii::Context const& context, std::string_view layer_name);
    static bool has_instance_extension(vk::raii::Context const& context, std::string_view extension_name);

    void create_debug_messenger();
    static vk::DebugUtilsMessengerCreateInfoEXT get_debug_messenger_create_info();
    static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_utils_messenger_callback(vk::DebugUtilsMessageSeverityFlagBitsEXT message_severity,
        vk::DebugUtilsMessageTypeFlagsEXT message_types, vk::DebugUtilsMessengerCallbackDataEXT const* callback_data, void* user_data);

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

    void create_graphics_pipeline();
    vk::PipelineRenderingCreateInfo pipeline_rendering_create_info() const;
    vk::raii::ShaderModule create_shader_module(std::string shader) const;

    void create_command_pool();
    void create_command_buffers();

    void create_sync_objects();

    void init_imgui();

    void update_gui();
    void update_tree64_buffer();

    void draw_frame();
    void record_command_buffer(vk::CommandBuffer command_buffer, uint32_t image_index);

    void recreate_swapchain();
    void clean_swapchain();

    vk::raii::ImageView create_image_view(vk::Image image, vk::Format format) const;

    void one_time_commands(std::invocable<vk::CommandBuffer> auto const& commands_recorder) const;

    void copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) const;
    void copy_buffer_to_image(vk::Buffer src, vk::Image dst, uint32_t width, uint32_t height) const;

    void create_tree64_buffer(std::vector<Tree64Node> const& nodes);

private:
    static constexpr auto MAX_FRAMES_IN_FLIGHT = 2u;

    Window m_window = Window("Vulkan Playground", 1280u, 720u);
    bool m_should_recreate_swapchain = false;
    vk::raii::Context m_context;
    vk::raii::Instance m_instance = nullptr;
    vk::raii::DebugUtilsMessengerEXT m_debug_messenger = nullptr;
    vk::raii::SurfaceKHR m_surface = nullptr;

    static constexpr std::array<char const*, 1u> DEVICE_REQUIRED_EXTENSIONS = std::to_array({
        vk::KHRSwapchainExtensionName,
    });
    vk::raii::PhysicalDevice m_physical_device = nullptr;
    vk::raii::Device m_device = nullptr;
    vk::raii::Queue m_graphics_queue = nullptr;
    vk::raii::Queue m_present_queue = nullptr;
    VmaRaiiAllocator m_allocator = nullptr;

    vk::raii::SwapchainKHR m_swapchain = nullptr;
    std::vector<vk::Image> m_swapchain_images;
    vk::Format m_swapchain_format = vk::Format::eUndefined;
    vk::Extent2D m_swapchain_extent;
    std::vector<vk::raii::ImageView> m_swapchain_image_views;
    std::vector<vk::raii::Semaphore> m_render_finished_semaphores;

    vk::raii::PipelineLayout m_pipeline_layout = nullptr;
    vk::raii::Pipeline m_graphics_pipeline = nullptr;

    vk::raii::CommandPool m_command_pool = nullptr;
    vk::raii::CommandBuffers m_command_buffers = nullptr;

    std::vector<vk::raii::Semaphore> m_image_available_semaphores;
    std::vector<vk::raii::Fence> m_in_flight_fences;

    std::unique_ptr<ImGuiWrapper> m_imgui;

    std::filesystem::path m_model_path_to_import;
    uint32_t m_max_side_voxel_count_to_import = 512;
    std::future<std::tuple<uint8_t, std::vector<Tree64Node>>> m_model_import_future;
    uint8_t m_tree64_depth = 0u;
    VmaRaiiBuffer m_tree64_buffer = nullptr;
    vk::DeviceAddress m_tree64_device_address = 0u;

    uint8_t m_current_in_flight_frame_index = 0u;

    Camera m_camera = Camera(glm::vec3(15.1f, 3.1f, 17.1f), glm::radians(glm::vec2(-45.f, 45.f)));
};

}
