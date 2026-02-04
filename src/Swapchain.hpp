#pragma once

#include "VulkanContext.hpp"

#include <vulkan/vulkan_raii.hpp>

namespace vp {

class Swapchain {
public:
    Swapchain(std::nullptr_t);
    Swapchain(Swapchain const& other) = delete;
    Swapchain(Swapchain&& other) = default;

    ~Swapchain();

    Swapchain& operator=(Swapchain const& other) = delete;
    Swapchain& operator=(Swapchain&& other) = default;

    void recreate(VulkanContext const& vk_ctx, vk::Extent2D extent, vk::PresentModeKHR present_mode);

    [[nodiscard]] vk::Format const& format() const;
    [[nodiscard]] vk::Extent2D const& extent() const;
    [[nodiscard]] uint32_t image_count() const;

    struct AcquiredImage {
        uint32_t index;
        vk::Image image;
        vk::ImageView view;
        vk::Semaphore render_finished_semaphore;
    };

    [[nodiscard]] std::optional<AcquiredImage> acquire_next_image(vk::Semaphore semaphore);
    [[nodiscard]] bool queue_present(AcquiredImage const& acquired_image);

private:
    vk::raii::Device const* m_device = nullptr;
    vk::Queue m_queue;
    vk::raii::SwapchainKHR m_swapchain = vk::raii::SwapchainKHR(nullptr);
    vk::Format m_format;
    vk::Extent2D m_extent;
    std::vector<vk::Image> m_images;
    std::vector<vk::raii::ImageView> m_image_views;
    std::vector<vk::raii::Semaphore> m_render_finished_semaphores;
};

}
