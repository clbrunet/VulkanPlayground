#include "Swapchain.hpp"

#include <functional>
#include <algorithm>
#include <iostream>

namespace vp {

Swapchain::Swapchain(VulkanContext const& vk_ctx)
    : m_vk_ctx{ vk_ctx } {
}

Swapchain::~Swapchain() {
    if (*m_vk_ctx.device) {
        m_vk_ctx.device.waitIdle();
    }
}

void Swapchain::recreate(vk::Extent2D const extent, vk::PresentModeKHR const present_mode) {
    m_vk_ctx.device.waitIdle();
    m_render_finished_semaphores.clear();
    m_image_views.clear();
    m_images.clear();
    m_swapchain.clear();

    auto const surface_capabilities = m_vk_ctx.physical_device.getSurfaceCapabilitiesKHR(m_vk_ctx.surface);

    auto const min_image_count = std::invoke([&] {
        if (surface_capabilities.maxImageCount == 0u) {
            return surface_capabilities.minImageCount + 1u;
        }
        return std::min(surface_capabilities.minImageCount + 1u, surface_capabilities.maxImageCount);
    });

    auto const surface_format = std::invoke([&] {
        auto const surface_formats = m_vk_ctx.physical_device.getSurfaceFormatsKHR(m_vk_ctx.surface);
        auto const surface_format_it = std::ranges::find_if(surface_formats, [](vk::SurfaceFormatKHR const device_surface_format) {
            return (device_surface_format.format == vk::Format::eR8G8B8A8Srgb || device_surface_format.format == vk::Format::eB8G8R8A8Srgb)
                && device_surface_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear;
        });
        return (surface_format_it != std::cend(surface_formats)) ? *surface_format_it : surface_formats.front();
    });

    auto const image_extent = std::invoke([&] {
        if (surface_capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max()) {
            return surface_capabilities.currentExtent;
        }
        return vk::Extent2D{
            .width = std::clamp(extent.width,
                surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width),
            .height = std::clamp(extent.height,
                surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height),
        };
    });

    auto const [sharing_mode, queue_family_index_count] = std::invoke([&] {
        if (*m_vk_ctx.graphics_queue != *m_vk_ctx.present_queue) {
            return std::tuple(vk::SharingMode::eConcurrent, 2u);
        }
        return std::tuple(vk::SharingMode::eExclusive, 0u);
    });
    auto const queue_family_indices = std::to_array({
        m_vk_ctx.graphics_queue_family_index,
        m_vk_ctx.present_queue_family_index,
    });

    auto const create_info = vk::SwapchainCreateInfoKHR{
        .surface = m_vk_ctx.surface,
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
    m_swapchain = vk::raii::SwapchainKHR(m_vk_ctx.device, create_info);
    m_format = surface_format.format;
    m_extent = image_extent;

    m_images = m_swapchain.getImages();
    std::ranges::transform(m_images, std::back_inserter(m_image_views), [&](vk::Image const image) {
        return create_image_view(m_vk_ctx.device, image, m_format);
    });

    m_render_finished_semaphores.reserve(std::size(m_images));
    for (auto i = 0u; i < std::size(m_images); ++i) {
        m_render_finished_semaphores.emplace_back(m_vk_ctx.device, vk::SemaphoreCreateInfo{});
    }
}

vk::Format const& Swapchain::format() const {
    return m_format;
}

vk::Extent2D const& Swapchain::extent() const {
    return m_extent;
}

uint32_t Swapchain::image_count() const {
    return static_cast<uint32_t>(std::size(m_images));
}

std::optional<Swapchain::AcquiredImage> Swapchain::acquire_next_image(vk::Semaphore const semaphore) {
    try {
        auto const index = m_swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), semaphore).second;
        return AcquiredImage{
            .index = index,
            .image = m_images[index],
            .view = m_image_views[index],
            .render_finished_semaphore = m_render_finished_semaphores[index],
        };
    } catch (vk::OutOfDateKHRError const&) {
        std::cout << "acquire out of date" << std::endl;
        return std::nullopt;
    }
}

bool Swapchain::queue_present(AcquiredImage const& acquired_image) {
    try {
        auto const present_info = vk::PresentInfoKHR{
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &acquired_image.render_finished_semaphore,
            .swapchainCount = 1u,
            .pSwapchains = &*m_swapchain,
            .pImageIndices = &acquired_image.index,
        };
        auto const result = m_vk_ctx.present_queue.presentKHR(present_info);
        if (result == vk::Result::eSuboptimalKHR) {
            return false;
        }
    } catch (vk::OutOfDateKHRError const&) {
        return false;
    }
    return true;
}

}
