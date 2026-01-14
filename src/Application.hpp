#pragma once

#include "VulkanContext.hpp"
#include "Window.hpp"
#include "Swapchain.hpp"
#include "vulkan_utils.hpp"
#include "ImGuiWrapper.hpp"
#include "Camera.hpp"
#include "Tree64.hpp"

#include <vulkan/vulkan_raii.hpp>
#include <glm/glm.hpp>

#include <vector>
#include <array>
#include <cstdint>
#include <future>

namespace vp {

class Application {
public:
    Application();
    Application(Application const&) = delete;
    Application(Application&& other) = delete;

    ~Application();

    Application& operator=(Application const&) = delete;
    Application& operator=(Application&& other) = delete;

    void run();

private:
    void start_model_import();

    void init_window();

    void init_vulkan();

    void recreate_swapchain();

    void create_graphics_pipeline();
    vk::PipelineRenderingCreateInfo pipeline_rendering_create_info() const;
    vk::raii::ShaderModule create_shader_module(std::string shader) const;

    void create_command_pool();
    void create_command_buffers();

    void create_sync_objects();

    void create_hosek_wilkie_sky_rendering_parameters_buffer();

    void init_imgui();

    void update_gui();
    void update_tree64_buffer();

    void draw_frame();
    void record_command_buffer(vk::CommandBuffer command_buffer, Swapchain::AcquiredImage const& acquired_image);

    void copy_buffer(vk::Buffer src, vk::Buffer dst, vk::DeviceSize size) const;
    void copy_buffer_to_image(vk::Buffer src, vk::Image dst, uint32_t width, uint32_t height) const;

    void create_tree64_buffer(std::span<Tree64Node const> nodes);
    void save_acceleration_structure(std::filesystem::path const& path);

    void update_hosek_wilkie_sky_rendering_parameters();

private:
    static constexpr std::array<char const*, 1u> DEVICE_REQUIRED_EXTENSIONS = std::to_array({
        vk::KHRSwapchainExtensionName,
    });
    static constexpr auto MAX_FRAMES_IN_FLIGHT = 2u;

    Window m_window = Window("Vulkan Playground", 1280u, 720u);
    bool m_should_recreate_swapchain = false;

    VulkanContext m_vk_ctx = VulkanContext(nullptr);
    bool m_has_immediate_present_mode = false;
    bool m_use_v_sync = true;
    Swapchain m_swapchain = Swapchain(m_vk_ctx);

    vk::raii::PipelineLayout m_pipeline_layout = vk::raii::PipelineLayout(nullptr);
    vk::raii::Pipeline m_graphics_pipeline = vk::raii::Pipeline(nullptr);

    vk::raii::CommandPool m_command_pool = vk::raii::CommandPool(nullptr);
    vk::raii::CommandBuffers m_command_buffers = vk::raii::CommandBuffers(nullptr);

    std::vector<vk::raii::Semaphore> m_image_available_semaphores;
    std::vector<vk::raii::Fence> m_in_flight_fences;

    std::unique_ptr<ImGuiWrapper> m_imgui;

    std::filesystem::path m_model_path_to_import;
    uint32_t m_max_side_voxel_count_to_import = 1024;
    std::future<std::optional<ContiguousTree64>> m_model_import_future;
    uint8_t m_tree64_depth = 0u;
    VmaRaiiBuffer m_tree64_buffer = VmaRaiiBuffer(nullptr);
    vk::DeviceAddress m_tree64_device_address = 0u;

    float m_sun_rotation = glm::radians(0.f);
    float m_sun_elevation = glm::radians(45.f);
    float m_hosek_wilkie_sky_turbidity = 3.f;
    float m_hosek_wilkie_sky_albedo = 0.3f;
    VmaRaiiBuffer m_hosek_wilkie_sky_rendering_parameters_buffer = VmaRaiiBuffer(nullptr);
    vk::DeviceAddress m_hosek_wilkie_sky_rendering_parameters_device_address = 0u;

    uint8_t m_current_in_flight_frame_index = 0u;

    Camera m_camera = Camera(glm::vec3(2000.f, 450.f, 4300.f), glm::radians(glm::vec2(0.f, 90.f)));
};

}
