#pragma once

#include "vulkan_utils.hpp"
#include "Window.hpp"

#include <vulkan/vulkan_raii.hpp>

#include <span>

namespace vp {

using PhysicalDeviceFeaturesChain = vk::StructureChain<vk::PhysicalDeviceFeatures2,
    vk::PhysicalDeviceVulkan11Features, vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>;

struct VulkanContext {
    static constexpr auto API_VERSION = vk::ApiVersion13;

    VulkanContext(std::nullptr_t);
    VulkanContext(Window const& window, std::span<char const* const> required_device_extensions,
        PhysicalDeviceFeaturesChain const& required_features);
    VulkanContext(VulkanContext const& other) = delete;
    VulkanContext(VulkanContext&& other) = default;

    ~VulkanContext();

    VulkanContext& operator=(VulkanContext const& other) = delete;
    VulkanContext& operator=(VulkanContext&& other) = default;

    vk::raii::Context context;
    vk::raii::Instance instance = vk::raii::Instance(nullptr);
    vk::raii::DebugUtilsMessengerEXT debug_messenger = vk::raii::DebugUtilsMessengerEXT(nullptr);
    vk::raii::SurfaceKHR surface = vk::raii::SurfaceKHR(nullptr);

    vk::raii::PhysicalDevice physical_device = vk::raii::PhysicalDevice(nullptr);
    vk::raii::Device device = vk::raii::Device(nullptr);
    uint32_t general_queue_family_index = ~0u;
    vk::raii::Queue general_queue = vk::raii::Queue(nullptr);

    VmaRaiiAllocator allocator = VmaRaiiAllocator(nullptr);
};

}
