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

struct QueueFamilyIndices {
    std::optional<uint32_t> graphics;
    std::optional<uint32_t> present;
    std::optional<uint32_t> compute;
};

static QueueFamilyIndices get_queue_family_indices(vk::PhysicalDevice const physical_device, vk::SurfaceKHR const surface) {
    auto const queue_family_properties = physical_device.getQueueFamilyProperties();
    auto graphics_and_present_indices = std::vector<uint32_t>();
    graphics_and_present_indices.reserve(std::size(queue_family_properties));
    auto compute_indices = std::vector<uint32_t>();
    compute_indices.reserve(std::size(queue_family_properties));
    auto queue_family_index = 0u;
    for (auto const& queue_family_property : physical_device.getQueueFamilyProperties()) {
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics
            && physical_device.getSurfaceSupportKHR(queue_family_index, surface)) {
            graphics_and_present_indices.emplace_back(queue_family_index);
        }
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eCompute) {
            compute_indices.emplace_back(queue_family_index);
        }
        queue_family_index += 1u;
    }
    if (!std::empty(graphics_and_present_indices) && std::empty(compute_indices)) {
        return QueueFamilyIndices{
            .graphics = graphics_and_present_indices[0],
            .present = graphics_and_present_indices[0],
        };
    }
    for (auto const graphics_and_present_index : graphics_and_present_indices) {
        auto const compute_index = std::ranges::find_if(compute_indices, [=](uint32_t const compute_index) {
            return compute_index != graphics_and_present_index;
        });
        if (compute_index != std::end(compute_indices)) {
            // Best case : same index for graphics/present and a separate compute queue
            return QueueFamilyIndices{
                .graphics = graphics_and_present_index,
                .present = graphics_and_present_index,
                .compute = *compute_index,
            };
        }
    }
    if (!std::empty(graphics_and_present_indices)) {
        // Same queue index for all
        return QueueFamilyIndices{
            .graphics = graphics_and_present_indices[0],
            .present = graphics_and_present_indices[0],
            .compute = compute_indices[0],
        };
    }
    auto indices = QueueFamilyIndices{};
    queue_family_index = 0u;
    for (auto const& queue_family_property : physical_device.getQueueFamilyProperties()) {
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics = queue_family_index;
        }
        if (physical_device.getSurfaceSupportKHR(queue_family_index, surface)) {
            indices.present = queue_family_index;
        }
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eCompute) {
            indices.compute = queue_family_index;
        }
        queue_family_index += 1u;
    }
    return indices;
}

template<typename PhysicalDeviceFeatures>
static bool has_device_features(auto const& available_features_chain, auto const& required_features_chain) {
    auto const& available_features = static_cast<PhysicalDeviceFeatures::NativeType const&>(available_features_chain.template get<PhysicalDeviceFeatures>());
    auto const& required_features = static_cast<PhysicalDeviceFeatures::NativeType const&>(required_features_chain.template get<PhysicalDeviceFeatures>());
    auto available_features_bools = std::span<vk::Bool32 const>();
    auto required_features_bools = std::span<vk::Bool32 const>();
    if constexpr (std::is_same_v<PhysicalDeviceFeatures, vk::PhysicalDeviceFeatures2>) {
        available_features_bools = std::span(&available_features.features.robustBufferAccess, 55u);
        required_features_bools = std::span(&required_features.features.robustBufferAccess, 55u);
    } else if constexpr (std::is_same_v<PhysicalDeviceFeatures, vk::PhysicalDeviceVulkan11Features>) {
        available_features_bools = std::span(&available_features.storageBuffer16BitAccess, 12u);
        required_features_bools = std::span(&required_features.storageBuffer16BitAccess, 12u);
    } else if constexpr (std::is_same_v<PhysicalDeviceFeatures, vk::PhysicalDeviceVulkan12Features>) {
        available_features_bools = std::span(&available_features.samplerMirrorClampToEdge, 47u);
        required_features_bools = std::span(&required_features.samplerMirrorClampToEdge, 47u);
    } else if constexpr (std::is_same_v<PhysicalDeviceFeatures, vk::PhysicalDeviceVulkan13Features>) {
        available_features_bools = std::span(&available_features.robustImageAccess, 15u);
        required_features_bools = std::span(&required_features.robustImageAccess, 15u);
    } else {
        static_assert(false);
    }
    for (auto i = 0u; i < std::size(required_features_bools); ++i) {
        if (static_cast<bool>(required_features_bools[i]) && !static_cast<bool>(available_features_bools[i])) {
            return false;
        }
    }
    return true;
}

static uint32_t get_physical_device_score(vk::PhysicalDevice const physical_device, vk::SurfaceKHR const surface,
    std::span<char const* const> const required_extensions, PhysicalDeviceFeaturesChain const& required_features) {
    auto const indices = get_queue_family_indices(physical_device, surface);
    if (!indices.graphics.has_value() || !indices.present.has_value() || !indices.compute.has_value()) {
        return 0u;
    }
    auto const has_required_extensions = std::ranges::all_of(required_extensions, [&](char const* const extension_name) {
        return has_device_extension(physical_device, extension_name);
    });
    if (!has_required_extensions) {
        return 0u;
    }
    auto const features = physical_device.getFeatures2<vk::PhysicalDeviceFeatures2, vk::PhysicalDeviceVulkan11Features,
        vk::PhysicalDeviceVulkan12Features, vk::PhysicalDeviceVulkan13Features>();
    if (!has_device_features<vk::PhysicalDeviceFeatures2>(features, required_features)
        || !has_device_features<vk::PhysicalDeviceVulkan11Features>(features, required_features)
        || !has_device_features<vk::PhysicalDeviceVulkan12Features>(features, required_features)
        || !has_device_features<vk::PhysicalDeviceVulkan13Features>(features, required_features)) {
        return 0u;
    }
    auto score = 1u;
    if (physical_device.getProperties().deviceType == vk::PhysicalDeviceType::eDiscreteGpu) {
        score += 1u;
    }
    return score;
}

static vk::raii::PhysicalDevice select_physical_device(vk::raii::Instance const& instance, vk::SurfaceKHR const surface,
    std::span<char const* const> const required_extensions, PhysicalDeviceFeaturesChain const& required_features) {
    vk::raii::PhysicalDevice selected_physical_device = nullptr;
    auto best_score = 0u;
    for (auto& physical_device : instance.enumeratePhysicalDevices()) {
        auto const score = get_physical_device_score(physical_device, surface, required_extensions, required_features);
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

static vk::raii::Device create_device(vk::raii::PhysicalDevice const& physical_device, QueueFamilyIndices const& queue_family_indices,
    std::span<char const* const> const required_extensions, PhysicalDeviceFeaturesChain const& required_features) {
    auto const unique_queue_family_indices = std::set<uint32_t>({
        queue_family_indices.graphics.value(),
        queue_family_indices.present.value(),
        queue_family_indices.compute.value(),
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

    auto const create_info = vk::StructureChain(
        vk::DeviceCreateInfo{
            .queueCreateInfoCount = static_cast<uint32_t>(std::size(queue_create_infos)),
            .pQueueCreateInfos = std::data(queue_create_infos),
            .enabledExtensionCount = static_cast<uint32_t>(std::size(required_extensions)),
            .ppEnabledExtensionNames = std::data(required_extensions),
        },
        required_features.get<vk::PhysicalDeviceFeatures2>(),
        required_features.get<vk::PhysicalDeviceVulkan11Features>(),
        required_features.get<vk::PhysicalDeviceVulkan12Features>(),
        required_features.get<vk::PhysicalDeviceVulkan13Features>()
    );
    return vk::raii::Device(physical_device, create_info.get());
}

VulkanContext::VulkanContext(std::nullptr_t) {
}

VulkanContext::VulkanContext(Window const& window, std::span<char const* const> required_device_extensions,
    PhysicalDeviceFeaturesChain const& required_features) {
    auto instance_extensions = std::vector<char const*>();
    std::tie(instance, instance_extensions) = create_instance(window, context);

    debug_messenger = vk::raii::DebugUtilsMessengerEXT(nullptr);
    if (std::ranges::find(instance_extensions, vk::EXTDebugUtilsExtensionName) != std::end(instance_extensions)) {
        debug_messenger = vk::raii::DebugUtilsMessengerEXT(instance, get_debug_messenger_create_info());
    }
    surface = vk::raii::SurfaceKHR(instance, window.create_surface(*instance));

    physical_device = select_physical_device(instance, surface, required_device_extensions, required_features);
    auto const queue_family_indices = get_queue_family_indices(physical_device, surface);
    graphics_queue_family_index = queue_family_indices.graphics.value();
    present_queue_family_index = queue_family_indices.present.value();
    compute_queue_family_index = queue_family_indices.compute.value();
    device = create_device(physical_device, queue_family_indices, required_device_extensions, required_features);
    graphics_queue = device.getQueue(graphics_queue_family_index, 0u);
    present_queue = device.getQueue(present_queue_family_index, 0u);
    compute_queue = device.getQueue(compute_queue_family_index, 0u);

    allocator = VmaRaiiAllocator(VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, physical_device, device, instance, VulkanContext::API_VERSION);
}

VulkanContext::~VulkanContext() {
    if (*device) {
        device.waitIdle();
    }
}

}
