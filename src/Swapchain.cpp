#include "Swapchain.hpp"

#include <functional>
#include <algorithm>
#include <iostream>

namespace vp {

Swapchain::Swapchain(std::nullptr_t) {
}

Swapchain::~Swapchain() {
    if (m_device) {
        m_device->waitIdle();
    }
}

void Swapchain::recreate(VulkanContext const& vk_ctx, vk::Extent2D extent, vk::PresentModeKHR present_mode) {
    m_device = &vk_ctx.device;
    m_queue = vk_ctx.general_queue;
    m_device->waitIdle();
    m_render_finished_semaphores.clear();
    m_image_views.clear();
    m_images.clear();
    m_swapchain.clear();

    auto const surface_capabilities = vk_ctx.physical_device.getSurfaceCapabilitiesKHR(vk_ctx.surface);

    auto const min_image_count = std::invoke([&] {
        if (surface_capabilities.maxImageCount == 0u) {
            return surface_capabilities.minImageCount + 1u;
        }
        return std::min(surface_capabilities.minImageCount + 1u, surface_capabilities.maxImageCount);
    });

    auto const surface_format = std::invoke([&] {
        auto const surface_formats = vk_ctx.physical_device.getSurfaceFormatsKHR(vk_ctx.surface);
        auto const surface_format_it = std::ranges::find_if(surface_formats, [](vk::SurfaceFormatKHR const device_surface_format) {
            // return (device_surface_format.format == vk::Format::eR8G8B8A8Unorm || device_surface_format.format == vk::Format::eB8G8R8A8Unorm)
            //     && device_surface_format.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear; // Linear format useful for debugging
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

    auto const create_info = vk::SwapchainCreateInfoKHR{
        .surface = vk_ctx.surface,
        .minImageCount = min_image_count,
        .imageFormat = surface_format.format,
        .imageColorSpace = surface_format.colorSpace,
        .imageExtent = image_extent,
        .imageArrayLayers = 1u,
        .imageUsage = vk::ImageUsageFlagBits::eColorAttachment,
        .imageSharingMode = vk::SharingMode::eExclusive,
        .preTransform = surface_capabilities.currentTransform,
        .compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque,
        .presentMode = present_mode,
        .clipped = vk::True,
        .oldSwapchain = vk::SwapchainKHR{},
    };
    m_swapchain = vk::raii::SwapchainKHR(*m_device, create_info);
    m_format = surface_format.format;
    m_extent = image_extent;

    m_images = m_swapchain.getImages();
    std::ranges::transform(m_images, std::back_inserter(m_image_views), [&](vk::Image const image) {
        return create_image_view(*m_device, image, m_format);
    });

    m_render_finished_semaphores.reserve(std::size(m_images));
    for (auto i = 0u; i < std::size(m_images); ++i) {
        m_render_finished_semaphores.emplace_back(*m_device, vk::SemaphoreCreateInfo{});
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
    assert(**m_device);
    assert(m_queue);
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
    assert(**m_device);
    assert(m_queue);
    try {
        auto const present_info = vk::PresentInfoKHR{
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &acquired_image.render_finished_semaphore,
            .swapchainCount = 1u,
            .pSwapchains = &*m_swapchain,
            .pImageIndices = &acquired_image.index,
        };
        auto const result = m_queue.presentKHR(present_info);
        if (result == vk::Result::eSuboptimalKHR) {
            return false;
        }
    } catch (vk::OutOfDateKHRError const&) {
        return false;
    }
    return true;
}

}
