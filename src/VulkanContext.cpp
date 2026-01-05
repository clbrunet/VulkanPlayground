#include "VulkanContext.hpp"

#include <iostream>
#include <set>

namespace vp {

static bool has_instance_layer(vk::raii::Context const& context, std::string_view const layer_name) {
    return std::ranges::any_of(context.enumerateInstanceLayerProperties(), [layer_name](vk::LayerProperties const& property) {
        return layer_name.compare(std::data(property.layerName)) == 0;
    });
}

static bool has_instance_extension(vk::raii::Context const& context, std::string_view const extension_name) {
    return std::ranges::any_of(context.enumerateInstanceExtensionProperties(), [extension_name](vk::ExtensionProperties const& property) {
        return extension_name.compare(std::data(property.extensionName)) == 0;
    });
}

static VKAPI_ATTR vk::Bool32 VKAPI_CALL debug_utils_messenger_callback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT const message_severity, vk::DebugUtilsMessageTypeFlagsEXT const message_types,
    vk::DebugUtilsMessengerCallbackDataEXT const* const callback_data, [[maybe_unused]] void* user_data) {
    std::cerr << "Vulkan message, " << vk::to_string(message_severity)
        << ", " << vk::to_string(message_types) << " : " << callback_data->pMessage << std::endl;
    return vk::False;
}

static vk::DebugUtilsMessengerCreateInfoEXT get_debug_messenger_create_info() {
    return vk::DebugUtilsMessengerCreateInfoEXT{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
            | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debug_utils_messenger_callback,
    };
}

static std::tuple<vk::raii::Instance, std::vector<char const*>> create_instance(Window const& window, vk::raii::Context const& context) {
    auto const application_info = vk::ApplicationInfo{
        .pApplicationName = "Vulkan Playground",
        .applicationVersion = vk::makeApiVersion(0u, 0u, 1u, 0u),
        .pEngineName = "No Engine",
        .engineVersion = vk::makeApiVersion(0u, 0u, 1u, 0u),
        .apiVersion = VulkanContext::API_VERSION,
    };

    auto const required_instance_extensions = window.get_required_instance_extensions();
    auto instance_extensions = std::vector<char const*>(std::begin(required_instance_extensions), std::end(required_instance_extensions));
#ifndef NDEBUG
    if (has_instance_extension(context, vk::EXTDebugUtilsExtensionName)) {
        instance_extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
    }
#endif
    auto const has_portabibilty_enumeration_extension = has_instance_extension(context, vk::KHRPortabilityEnumerationExtensionName);
    if (has_portabibilty_enumeration_extension) {
        instance_extensions.emplace_back(vk::KHRPortabilityEnumerationExtensionName);
    }

    auto create_info = vk::InstanceCreateInfo{
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = static_cast<uint32_t>(std::size(instance_extensions)),
        .ppEnabledExtensionNames = std::data(instance_extensions),
    };
    if (has_portabibilty_enumeration_extension) {
        create_info.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
#ifndef NDEBUG
    auto const debug_messenger_create_info = get_debug_messenger_create_info();
    create_info.pNext = &debug_messenger_create_info;
    constexpr auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
    if (has_instance_layer(context, VALIDATION_LAYER)) {
        create_info.enabledLayerCount = 1u;
        create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
    } else {
        std::cerr << "Warning : The Vulkan validation layers should be installed when using a debug build" << std::endl;
    }
#endif
    return std::tuple(vk::raii::Instance(context, create_info), instance_extensions);
}

static bool has_device_extension(vk::PhysicalDevice physical_device, std::string_view const extension_name) {
    return std::ranges::any_of(physical_device.enumerateDeviceExtensionProperties(), [extension_name](vk::ExtensionProperties const& property) {
        return extension_name.compare(std::data(property.extensionName)) == 0;
    });
}

static uint32_t get_physical_device_score(vk::PhysicalDevice const physical_device, vk::SurfaceKHR const surface, std::span<char const* const> const required_extensions) {
    auto has_graphics_queue = false;
    auto has_present_support = false;
    auto queue_family_index = 0u;
    for (auto const& queue_family_property : physical_device.getQueueFamilyProperties()) {
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
            has_graphics_queue = true;
        }
        if (physical_device.getSurfaceSupportKHR(queue_family_index, surface)) {
            has_present_support = true;
        }
        queue_family_index += 1u;
    }
    auto const has_required_extensions = std::ranges::all_of(required_extensions, [&](char const* const extension_name) {
        return has_device_extension(physical_device, extension_name);
    });
    if (!has_graphics_queue || !has_present_support || !has_required_extensions) {
        return 0u;
    }
    auto score = 1u;

    if (physical_device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 1u;
    }

    return score;
}

static vk::raii::PhysicalDevice select_physical_device(vk::raii::Instance const& instance, vk::SurfaceKHR const surface, std::span<char const* const> const required_extensions) {
    vk::raii::PhysicalDevice selected_physical_device = nullptr;
    auto best_score = 0u;
    for (auto physical_device : instance.enumeratePhysicalDevices()) {
        auto const score = get_physical_device_score(physical_device, surface, required_extensions);
        if (score > best_score) {
            best_score = score;
            selected_physical_device = std::move(physical_device);
        }
    }
    if (!*selected_physical_device) {
        throw std::runtime_error("no suitable GPU found");
    }
    return selected_physical_device;
}

struct QueueFamilyIndices {
    uint32_t graphics;
    uint32_t present;
};

static QueueFamilyIndices get_queue_family_indices(vk::PhysicalDevice const physical_device, vk::SurfaceKHR const surface) {
    auto indices = QueueFamilyIndices{};
    auto queue_family_index = 0u;
    for (auto const& queue_family_property : physical_device.getQueueFamilyProperties()) {
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics = queue_family_index;
        }
        if (physical_device.getSurfaceSupportKHR(queue_family_index, surface)) {
            indices.present = queue_family_index;
        }
        queue_family_index += 1u;
    }
    return indices;
}

static vk::raii::Device create_device(vk::raii::PhysicalDevice const& physical_device, QueueFamilyIndices const& queue_family_indices, std::span<char const* const> const required_extensions) {
    auto const unique_queue_family_indices = std::set<uint32_t>({
        queue_family_indices.graphics,
        queue_family_indices.present,
    });
    auto const queue_priority = 1.f;
    auto queue_create_infos = std::vector<vk::DeviceQueueCreateInfo>();
    std::ranges::transform(unique_queue_family_indices, std::back_inserter(queue_create_infos),
        [&queue_priority](uint32_t const queue_family_index) {
            return vk::DeviceQueueCreateInfo{
                .queueFamilyIndex = queue_family_index,
                .queueCount = 1u,
                .pQueuePriorities = &queue_priority,
            };
        }
    );

    auto vulkan13_features = vk::PhysicalDeviceVulkan13Features{
        .synchronization2 = vk::True,
        .dynamicRendering = vk::True,
    };
    auto const vulkan12_features = vk::PhysicalDeviceVulkan12Features{
        .pNext = &vulkan13_features,
        .scalarBlockLayout = vk::True,
        .bufferDeviceAddress = vk::True,
    };
    auto const features = vk::PhysicalDeviceFeatures{
        .depthClamp = vk::True,
        .shaderInt64 = vk::True,
    };

    auto create_info = vk::DeviceCreateInfo{
        .pNext = &vulkan12_features,
        .queueCreateInfoCount = static_cast<uint32_t>(std::size(queue_create_infos)),
        .pQueueCreateInfos = std::data(queue_create_infos),
        .enabledExtensionCount = static_cast<uint32_t>(std::size(required_extensions)),
        .ppEnabledExtensionNames = std::data(required_extensions),
        .pEnabledFeatures = &features,
    };
    return vk::raii::Device(physical_device, create_info);
}

VulkanContext::VulkanContext(std::nullptr_t) {
}
VulkanContext::VulkanContext(Window const& window, std::span<char const* const> required_device_extensions) {
    auto instance_extensions = std::vector<char const*>();
    std::tie(instance, instance_extensions) = create_instance(window, context);

    debug_messenger = vk::raii::DebugUtilsMessengerEXT(nullptr);
    if (std::ranges::find(instance_extensions, vk::EXTDebugUtilsExtensionName) != std::end(instance_extensions)) {
        debug_messenger = vk::raii::DebugUtilsMessengerEXT(instance, get_debug_messenger_create_info());
    }
    surface = vk::raii::SurfaceKHR(instance, window.create_surface(*instance));

    physical_device = select_physical_device(instance, surface, required_device_extensions);
    auto const queue_family_indices = get_queue_family_indices(physical_device, surface);
    graphics_queue_family_index = queue_family_indices.graphics;
    present_queue_family_index = queue_family_indices.present;
    device = create_device(physical_device, queue_family_indices, required_device_extensions);
    graphics_queue = device.getQueue(graphics_queue_family_index, 0u);
    present_queue = device.getQueue(present_queue_family_index, 0u);

    allocator = VmaRaiiAllocator(VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, physical_device, device, instance, VulkanContext::API_VERSION);
}

VulkanContext::~VulkanContext() {
    if (*device) {
        device.waitIdle();
    }
}

}
