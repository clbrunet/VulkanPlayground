#include "Application.hpp"
#include "filesystem.hpp"
#include "t64.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>
#include <glm/ext/scalar_common.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>

#include <iostream>
#include <set>
#include <algorithm>
#include <limits>
#include <functional>
#include <optional>
#include <span>
#include <filesystem>
#include <chrono>

namespace vp {

#ifndef NDEBUG
constexpr auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
#endif

#pragma pack(push, 1)
struct PushConstants {
    glm::vec3 camera_position;
    float aspect_ratio;
    glm::mat3 camera_rotation;
    uint32_t tree64_depth;
    vk::DeviceAddress tree64_device_address;
};
#pragma pack(pop)

constexpr auto VULKAN_API_VERSION = vk::ApiVersion13;

Application::Application() {
    // m_model_path_to_import = get_asset_path("models/sponza.vox");
    // m_model_path_to_import = get_asset_path("models/bistro_exterior.glb");
    m_model_path_to_import = get_asset_path("models/bistro_exterior_8k.t64");
    start_model_import();
    init_window();
    init_vulkan();
    init_imgui();
}

Application::~Application() {
    if (*m_device) {
        m_device.waitIdle();
    }
}

std::optional<ContiguousTree64> model_import(std::filesystem::path const& path, uint32_t const max_side_voxel_count) {
#if 1
    if (path.extension() == ".t64") {
        auto const contiguous_tree64 = import_t64(path);
        if (!contiguous_tree64.has_value()) {
            std::cerr << "Cannot import " << string_from(path) << std::endl;
            return std::nullopt;
        }
        return std::move(contiguous_tree64.value());
    }
    auto const begin_time = std::chrono::high_resolution_clock::now();
    auto tree64 = std::optional<Tree64>();
    if (path.extension() == ".vox") {
        tree64 = Tree64::import_vox(path);
    } else {
        tree64 = Tree64::voxelize_model(path, max_side_voxel_count);
    }
    if (!tree64.has_value()) {
        std::cerr << "Cannot import " << string_from(path) << std::endl;
        return std::nullopt;
    }
#else
    auto const begin_time = std::chrono::high_resolution_clock::now();
    auto tree64 = std::make_optional(Tree64(2u));
    tree64->add_voxel(glm::uvec3(0u, 0u, 0u));
    tree64->add_voxel(glm::uvec3(1u, 1u, 0u));
    tree64->add_voxel(glm::uvec3(0u, 1u, 1u));
    tree64->add_voxel(glm::uvec3(4u, 0u, 0u));
    tree64->add_voxel(glm::uvec3(0u, 0u, 4u));
    tree64->add_voxel(glm::uvec3(8u, 0u, 0u));
    // tree64->add_voxel(glm::uvec3(32u, 0u, 0u));
#endif

    auto const import_done_time = std::chrono::high_resolution_clock::now();
    auto const import_time = import_done_time - begin_time;
    std::cout << "import time " << std::chrono::duration_cast<std::chrono::duration<float>>(import_time) << std::endl;

    auto const nodes = tree64->build_contiguous_nodes();

    auto const build_contiguous_time = std::chrono::high_resolution_clock::now() - import_done_time;
    auto const full_time = std::chrono::high_resolution_clock::now() - begin_time;
    std::cout << "build contiguous time "
        << std::chrono::duration_cast<std::chrono::duration<float>>(build_contiguous_time) << std::endl;
    std::cout << "full time " << std::chrono::duration_cast<std::chrono::duration<float>>(full_time) << std::endl;
    std::cout << "node count " << nodes.size() << std::endl;
    return ContiguousTree64{ .depth = tree64->depth(), .nodes = nodes };
}

void Application::start_model_import() {
    m_model_import_future = std::async(model_import, m_model_path_to_import, m_max_side_voxel_count_to_import);
}

void Application::run() {
    m_window.prepare_event_loop();
    while (!m_window.should_close()) {
        m_imgui->begin_frame();
        update_gui();
        update_tree64_buffer();

        m_camera.update(m_window);

        draw_frame();
        m_window.poll_events();
    }
    m_device.waitIdle();
}

void Application::init_window() {
    m_window.set_framebuffer_callback([&]([[maybe_unused]] uint16_t const width, [[maybe_unused]] uint16_t const height) {
        m_should_recreate_swapchain = true;
    });
}

void Application::init_vulkan() {
    create_instance();
#ifndef NDEBUG
    create_debug_messenger();
#endif
    create_surface();
    select_physical_device();
    create_device();

    create_swapchain();

    create_graphics_pipeline();

    create_command_pool();
    create_command_buffers();

    create_sync_objects();
}

void Application::create_instance() {
    auto const application_info = vk::ApplicationInfo{
        .pApplicationName = "Vulkan Playground",
        .applicationVersion = vk::makeApiVersion(0u, 0u, 1u, 0u),
        .pEngineName = "No Engine",
        .engineVersion = vk::makeApiVersion(0u, 0u, 1u, 0u),
        .apiVersion = VULKAN_API_VERSION,
    };

    auto glfw_extension_count = uint32_t{};
    auto const glfw_extensions = glfwGetRequiredInstanceExtensions(&glfw_extension_count);
    auto extensions = std::vector<char const*>(glfw_extensions, glfw_extensions + glfw_extension_count);

#ifndef NDEBUG
    if (has_instance_extension(m_context, vk::EXTDebugUtilsExtensionName)) {
        extensions.emplace_back(vk::EXTDebugUtilsExtensionName);
    }
#endif

    auto const has_portabibilty_enumeration_extension = has_instance_extension(m_context, vk::KHRPortabilityEnumerationExtensionName);
    if (has_portabibilty_enumeration_extension) {
        extensions.emplace_back(vk::KHRPortabilityEnumerationExtensionName);
    }

    auto create_info = vk::InstanceCreateInfo{
        .pApplicationInfo = &application_info,
        .enabledExtensionCount = static_cast<uint32_t>(std::size(extensions)),
        .ppEnabledExtensionNames = std::data(extensions),
    };
    if (has_portabibilty_enumeration_extension) {
        create_info.flags |= vk::InstanceCreateFlagBits::eEnumeratePortabilityKHR;
    }
#ifndef NDEBUG
    auto const debug_messenger_create_info = get_debug_messenger_create_info();
    create_info.pNext = &debug_messenger_create_info;
    if (has_instance_layer(m_context, VALIDATION_LAYER)) {
        create_info.enabledLayerCount = 1u;
        create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
    }
#endif
    m_instance = vk::raii::Instance(m_context, create_info);
}

bool Application::has_instance_layer(vk::raii::Context const& context, std::string_view const layer_name) {
    return std::ranges::any_of(context.enumerateInstanceLayerProperties(), [layer_name](vk::LayerProperties const& property) {
        return layer_name.compare(std::data(property.layerName)) == 0;
    });
}

bool Application::has_instance_extension(vk::raii::Context const& context, std::string_view const extension_name) {
    return std::ranges::any_of(context.enumerateInstanceExtensionProperties(), [extension_name](vk::ExtensionProperties const& property) {
        return extension_name.compare(std::data(property.extensionName)) == 0;
    });
}

void Application::create_debug_messenger() {
    if (!m_instance.getProcAddr("vkCreateDebugUtilsMessengerEXT") || !m_instance.getProcAddr("vkDestroyDebugUtilsMessengerEXT")) {
        return;
    }
    m_debug_messenger = vk::raii::DebugUtilsMessengerEXT(m_instance, get_debug_messenger_create_info());
}

vk::DebugUtilsMessengerCreateInfoEXT Application::get_debug_messenger_create_info() {
    return vk::DebugUtilsMessengerCreateInfoEXT{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
            | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance,
        .pfnUserCallback = debug_utils_messenger_callback,
    };
}

VKAPI_ATTR vk::Bool32 VKAPI_CALL Application::debug_utils_messenger_callback(
    vk::DebugUtilsMessageSeverityFlagBitsEXT const message_severity, vk::DebugUtilsMessageTypeFlagsEXT const message_types,
    vk::DebugUtilsMessengerCallbackDataEXT const* const callback_data, [[maybe_unused]] void* user_data) {
    std::cerr << "Vulkan message, " << vk::to_string(message_severity)
        << ", " << vk::to_string(message_types) << " : " << callback_data->pMessage << std::endl;
    return vk::False;
}

void Application::create_surface() {
    m_surface = vk::raii::SurfaceKHR(m_instance, m_window.create_surface(*m_instance));
}

void Application::select_physical_device() {
    auto best_score = 0u;
    for (auto const& physical_device : m_instance.enumeratePhysicalDevices()) {
        auto const score = get_physical_device_score(physical_device);
        if (score > best_score) {
            best_score = score;
            m_physical_device = physical_device;
        }
    }
    if (!*m_physical_device) {
        throw std::runtime_error("no suitable GPU found");
    }
    std::cout << "Selected GPU : " << m_physical_device.getProperties().deviceName << std::endl;
}

uint32_t Application::get_physical_device_score(vk::PhysicalDevice const physical_device) const {
    auto const queue_family_properties = physical_device.getQueueFamilyProperties();
    auto has_graphics_queue = false;
    auto has_present_support = false;
    auto queue_family_index = 0u;
    for (auto const& queue_family_property : queue_family_properties) {
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
            has_graphics_queue = true;
        }
        if (physical_device.getSurfaceSupportKHR(queue_family_index, m_surface)) {
            has_present_support = true;
        }
        queue_family_index += 1u;
    }
    auto const has_required_extensions = std::ranges::all_of(DEVICE_REQUIRED_EXTENSIONS, [&](char const* const extension_name) {
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

void Application::create_device() {
    auto const queue_family_indices = get_queue_family_indices();
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
        .enabledExtensionCount = static_cast<uint32_t>(std::size(DEVICE_REQUIRED_EXTENSIONS)),
        .ppEnabledExtensionNames = std::data(DEVICE_REQUIRED_EXTENSIONS),
        .pEnabledFeatures = &features,
    };
#ifndef NDEBUG
    if (has_device_layer(m_physical_device, VALIDATION_LAYER)) {
        create_info.enabledLayerCount = 1u;
        create_info.ppEnabledLayerNames = &VALIDATION_LAYER;
    }
#endif
    m_device = vk::raii::Device(m_physical_device, create_info);
    m_graphics_queue = m_device.getQueue(queue_family_indices.graphics, 0u);
    m_present_queue = m_device.getQueue(queue_family_indices.present, 0u);

    m_allocator = VmaRaiiAllocator(VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, m_physical_device, m_device, m_instance, VULKAN_API_VERSION);
}

Application::QueueFamilyIndices Application::get_queue_family_indices() const {
    auto const queue_family_properties = m_physical_device.getQueueFamilyProperties();
    auto indices = QueueFamilyIndices{};
    auto queue_family_index = 0u;
    for (auto const& queue_family_property : queue_family_properties) {
        if (queue_family_property.queueFlags & vk::QueueFlagBits::eGraphics) {
            indices.graphics = queue_family_index;
        }
        if (m_physical_device.getSurfaceSupportKHR(queue_family_index, m_surface)) {
            indices.present = queue_family_index;
        }
        queue_family_index += 1u;
    }
    return indices;
}

bool Application::has_device_layer(vk::PhysicalDevice const physical_device, std::string_view const layer_name) {
    return std::ranges::any_of(physical_device.enumerateDeviceLayerProperties(), [layer_name](vk::LayerProperties const& property) {
        return layer_name.compare(std::data(property.layerName)) == 0;
    });
}

bool Application::has_device_extension(vk::PhysicalDevice physical_device, std::string_view const extension_name) {
    return std::ranges::any_of(physical_device.enumerateDeviceExtensionProperties(), [extension_name](vk::ExtensionProperties const& property) {
        return extension_name.compare(std::data(property.extensionName)) == 0;
    });
}

void Application::create_swapchain() {
    auto const surface_capabilities = m_physical_device.getSurfaceCapabilitiesKHR(m_surface);

    auto const min_image_count = std::invoke([&] {
        if (surface_capabilities.maxImageCount == 0u) {
            return surface_capabilities.minImageCount + 1u;
        }
        return std::min(surface_capabilities.minImageCount + 1u, surface_capabilities.maxImageCount);
    });

    auto const surface_format = std::invoke([&] {
        auto const surface_formats = m_physical_device.getSurfaceFormatsKHR(m_surface);
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
        auto const framebuffer_dimensions = m_window.framebuffer_dimensions();
        return vk::Extent2D{
            .width = std::clamp(static_cast<uint32_t>(framebuffer_dimensions.x),
                surface_capabilities.minImageExtent.width, surface_capabilities.maxImageExtent.width),
            .height = std::clamp(static_cast<uint32_t>(framebuffer_dimensions.y),
                surface_capabilities.minImageExtent.height, surface_capabilities.maxImageExtent.height),
        };
    });

    auto const [sharing_mode, queue_family_index_count] = std::invoke([&] {
        if (*m_graphics_queue != *m_present_queue) {
            return std::tuple(vk::SharingMode::eConcurrent, 2u);
        }
        return std::tuple(vk::SharingMode::eExclusive, 0u);
    });
    auto const physical_device_queue_family_indices = get_queue_family_indices();
    auto const queue_family_indices = std::to_array({
        physical_device_queue_family_indices.graphics,
        physical_device_queue_family_indices.present,
    });

    auto const create_info = vk::SwapchainCreateInfoKHR{
        .surface = m_surface,
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
        .presentMode = vk::PresentModeKHR::eFifo,
        // .presentMode = vk::PresentModeKHR::eImmediate,
        .clipped = vk::True,
        .oldSwapchain = vk::SwapchainKHR{},
    };
    m_swapchain = vk::raii::SwapchainKHR(m_device, create_info);
    m_swapchain_images = m_swapchain.getImages();
    m_swapchain_format = surface_format.format;
    m_swapchain_extent = image_extent;

    std::ranges::transform(m_swapchain_images, std::back_inserter(m_swapchain_image_views), [&](vk::Image const swapchain_image) {
        return create_image_view(swapchain_image, m_swapchain_format);
    });

    m_render_finished_semaphores.reserve(std::size(m_swapchain_images));
    for (auto i = 0u; i < std::size(m_swapchain_images); ++i) {
        m_render_finished_semaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
    }
}

void Application::create_graphics_pipeline() {
    auto const rendering_create_info = pipeline_rendering_create_info();

    auto const vertex_shader_module = create_shader_module("raytracing.vert");
    auto const vertex_shader_stage_create_info = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eVertex,
        .module = vertex_shader_module,
        .pName = "main",
    };

    auto const fragment_shader_module = create_shader_module("raytracing.frag");
    auto const fragment_shader_stage_create_info = vk::PipelineShaderStageCreateInfo{
        .stage = vk::ShaderStageFlagBits::eFragment,
        .module = fragment_shader_module,
        .pName = "main",
    };

    auto const shader_stages = std::array{ vertex_shader_stage_create_info, fragment_shader_stage_create_info };

    auto const vertex_input_state_create_info = vk::PipelineVertexInputStateCreateInfo{};

    auto const input_assembly_state_create_info = vk::PipelineInputAssemblyStateCreateInfo{
        .topology = vk::PrimitiveTopology::eTriangleList,
        .primitiveRestartEnable = vk::False,
    };

    auto const viewport_state_create_info = vk::PipelineViewportStateCreateInfo{
        .viewportCount = 1u,
        .scissorCount = 1u,
    };

    auto const rasterization_state_create_info = vk::PipelineRasterizationStateCreateInfo{
        .depthClampEnable = vk::True,
        .rasterizerDiscardEnable = vk::False,
        .polygonMode = vk::PolygonMode::eFill,
        .cullMode = vk::CullModeFlagBits::eBack,
        .frontFace = vk::FrontFace::eClockwise,
        .depthBiasEnable = vk::False,
        .lineWidth = 1.f,
    };

    auto const multisample_state_create_info = vk::PipelineMultisampleStateCreateInfo{
        .rasterizationSamples = vk::SampleCountFlagBits::e1,
        .sampleShadingEnable = vk::False,
    };

    auto const color_blend_attachment_state = vk::PipelineColorBlendAttachmentState{
        .blendEnable = vk::False,
        .colorWriteMask = vk::ColorComponentFlagBits::eR | vk::ColorComponentFlagBits::eG
            | vk::ColorComponentFlagBits::eB | vk::ColorComponentFlagBits::eA,
    };

    auto const color_blend_state_create_info = vk::PipelineColorBlendStateCreateInfo{
        .logicOpEnable = vk::False,
        .attachmentCount = 1u,
        .pAttachments = &color_blend_attachment_state,
    };

    auto const dynamic_states = std::to_array({
        vk::DynamicState::eViewport,
        vk::DynamicState::eScissor,
    });
    auto const dynamic_state_create_info = vk::PipelineDynamicStateCreateInfo{
        .dynamicStateCount = static_cast<uint32_t>(std::size(dynamic_states)),
        .pDynamicStates = std::data(dynamic_states),
    };

    auto const push_contant_ranges = std::array{
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .size = sizeof(PushConstants)
        },
    };
    auto const pipeline_layout_create_info = vk::PipelineLayoutCreateInfo{
        .pushConstantRangeCount = static_cast<uint32_t>(std::size(push_contant_ranges)),
        .pPushConstantRanges = std::data(push_contant_ranges),
    };
    m_pipeline_layout = vk::raii::PipelineLayout(m_device, pipeline_layout_create_info);

    auto const create_info = vk::GraphicsPipelineCreateInfo{
        .pNext = &rendering_create_info,
        .stageCount = static_cast<uint32_t>(std::size(shader_stages)),
        .pStages = std::data(shader_stages),
        .pVertexInputState = &vertex_input_state_create_info,
        .pInputAssemblyState = &input_assembly_state_create_info,
        .pTessellationState = nullptr,
        .pViewportState = &viewport_state_create_info,
        .pRasterizationState = &rasterization_state_create_info,
        .pMultisampleState = &multisample_state_create_info,
        .pDepthStencilState = nullptr,
        .pColorBlendState = &color_blend_state_create_info,
        .pDynamicState = &dynamic_state_create_info,
        .layout = m_pipeline_layout,
    };
    m_graphics_pipeline = vk::raii::Pipeline(m_device, nullptr, create_info);
}

vk::PipelineRenderingCreateInfo Application::pipeline_rendering_create_info() const {
    return vk::PipelineRenderingCreateInfo{
        .colorAttachmentCount = 1u,
        .pColorAttachmentFormats = &m_swapchain_format,
    };
}

vk::raii::ShaderModule Application::create_shader_module(std::string shader) const {
    auto const spirv_path = get_spirv_shader_path(std::move(shader));
    auto const code = read_binary_file(spirv_path);
    if (!code) {
        throw std::runtime_error("cannot read \"" + string_from(spirv_path) + '"');
    }

    auto const create_info = vk::ShaderModuleCreateInfo{
        .codeSize = std::size(*code),
        .pCode = reinterpret_cast<uint32_t const*>(std::data(*code)),
    };

    return vk::raii::ShaderModule(m_device, create_info);
}

void Application::create_command_pool() {
    auto const create_info = vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = get_queue_family_indices().graphics,
    };
    m_command_pool = vk::raii::CommandPool(m_device, create_info);
}

void Application::create_command_buffers() {
    auto const allocate_info = vk::CommandBufferAllocateInfo{
        .commandPool = m_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    m_command_buffers = vk::raii::CommandBuffers(m_device, allocate_info);
}

void Application::create_sync_objects() {
    m_image_available_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    m_in_flight_fences.reserve(MAX_FRAMES_IN_FLIGHT);
    auto const in_flight_fence_create_info = vk::FenceCreateInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    for (auto i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_image_available_semaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
        m_in_flight_fences.emplace_back(m_device, in_flight_fence_create_info);
    }
}

void Application::init_imgui() {
    auto init_info = ImGui_ImplVulkan_InitInfo{
        .ApiVersion = VULKAN_API_VERSION,
        .Instance = *m_instance,
        .PhysicalDevice = *m_physical_device,
        .Device = *m_device,
        .QueueFamily = get_queue_family_indices().graphics,
        .Queue = *m_graphics_queue,
        .MinImageCount = static_cast<uint32_t>(std::size(m_swapchain_images)),
        .ImageCount = static_cast<uint32_t>(std::size(m_swapchain_images)),
        .DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_IMAGE_SAMPLER_POOL_SIZE + 1u,
        .UseDynamicRendering = true,
        .PipelineRenderingCreateInfo = static_cast<VkPipelineRenderingCreateInfo>(pipeline_rendering_create_info()),
    };
    m_imgui = std::make_unique<ImGuiWrapper>(m_window, init_info);
}

void Application::update_gui() {
    ImGui::Begin("GUI");
    ImGui::Text("Average frame time : %f ms (%u FPS)", 1000.f / ImGui::GetIO().Framerate,
        static_cast<uint32_t>(ImGui::GetIO().Framerate));

    ImGui::Text("Hold right click to move/rotate the camera");
    ImGui::Text("Speed is adjustable using mouse wheel and Shift/Alt");
    auto position = m_camera.position();
    ImGui::DragFloat3("Camera position", glm::value_ptr(position));
    m_camera.set_position(position);
    auto degrees_euler_angles = glm::degrees(m_camera.euler_angles());
    ImGui::DragFloat2("Camera rotation", glm::value_ptr(degrees_euler_angles));
    m_camera.set_euler_angles(glm::radians(degrees_euler_angles));

    auto model_path_to_import = string_from(m_model_path_to_import);
    ImGui::SetNextItemWidth(-45.f);
    if (ImGui::InputText("##model_path_to_import", &model_path_to_import, ImGuiInputTextFlags_ElideLeft)) {
        m_model_path_to_import = path_from(model_path_to_import);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open")) {
        auto const filters = std::array{ nfdu8filteritem_t{ "Models", "t64,vox,glb,gltf" } };
        auto path = m_window.pick_file(filters, get_asset_path("models"));
        if (path.has_value()) {
            m_model_path_to_import = std::move(path.value());
        }
    }

    if (m_model_path_to_import.extension() != ".t64" && m_model_path_to_import.extension() != ".vox") {
        auto const min = 4u;
        auto const max = 4096u;
        ImGui::DragScalar("Max side voxel count", ImGuiDataType_U32,
            &m_max_side_voxel_count_to_import, 1.f, &min, &max);
    }

    if (m_model_import_future.valid()) {
#ifndef NDEBUG
        ImGui::TextColored(ImVec4(1.f, 1.f, 0.f, 1.f), "Importing is slow with a debug build.");
#endif
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::ProgressBar(-1.f * static_cast<float>(ImGui::GetTime()), ImVec2(0.0f, 0.0f), "Importing...");
    } else if (ImGui::Button("Import")) {
        start_model_import();
    } else if (m_tree64_device_address != 0u && ImGui::Button("Save acceleration structure")) {
        auto const filters = std::array{ nfdu8filteritem_t{ "Tree64", "t64" } };
        auto const path = m_window.pick_saving_path(filters, get_asset_path("models"));
        if (path.has_value()) {
            save_acceleration_structure(path.value());
        }
    }
    ImGui::End();
}

void Application::update_tree64_buffer() {
    if (m_model_import_future.valid()
        && m_model_import_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto contiguous_tree64 = m_model_import_future.get();
        if (contiguous_tree64.has_value()) {
            m_tree64_depth = contiguous_tree64->depth;
            create_tree64_buffer(contiguous_tree64->nodes);
        }
    }
}

void Application::draw_frame() {
    auto const& in_flight_fence = m_in_flight_fences[m_current_in_flight_frame_index];
    static_cast<void>(m_device.waitForFences(*in_flight_fence, vk::True, std::numeric_limits<uint64_t>::max()));

    auto image_index = uint32_t{};
    auto const& image_available_semaphore = m_image_available_semaphores[m_current_in_flight_frame_index];
    try {
        image_index = m_swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), image_available_semaphore).second;
    } catch (vk::OutOfDateKHRError const&) {
        recreate_swapchain();
        return;
    }

    m_device.resetFences(*in_flight_fence);

    auto const& command_buffer = m_command_buffers[m_current_in_flight_frame_index];
    command_buffer.reset();
    record_command_buffer(command_buffer, image_index);

    auto const image_available_semaphore_submit_info = vk::SemaphoreSubmitInfo{
        .semaphore = image_available_semaphore,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    auto const command_buffer_submit_info = vk::CommandBufferSubmitInfo{
        .commandBuffer = command_buffer,
    };
    auto const& render_finished_semaphore = m_render_finished_semaphores[image_index];
    auto const render_finished_semaphore_submit_info = vk::SemaphoreSubmitInfo{
        .semaphore = render_finished_semaphore,
        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
    };
    m_graphics_queue.submit2(vk::SubmitInfo2{
        .waitSemaphoreInfoCount = 1u,
        .pWaitSemaphoreInfos = &image_available_semaphore_submit_info,
        .commandBufferInfoCount = 1u,
        .pCommandBufferInfos = &command_buffer_submit_info,
        .signalSemaphoreInfoCount = 1u,
        .pSignalSemaphoreInfos = &render_finished_semaphore_submit_info,
    }, in_flight_fence);

    m_imgui->update_windows();

    try {
        auto const present_info = vk::PresentInfoKHR{
            .waitSemaphoreCount = 1u,
            .pWaitSemaphores = &*render_finished_semaphore,
            .swapchainCount = 1u,
            .pSwapchains = &*m_swapchain,
            .pImageIndices = &image_index,
        };
        auto const result = m_present_queue.presentKHR(present_info);
        if (result == vk::Result::eSuboptimalKHR) {
            m_should_recreate_swapchain = true;
        }
    } catch (vk::OutOfDateKHRError const&) {
        m_should_recreate_swapchain = true;
    }
    if (m_should_recreate_swapchain) {
        recreate_swapchain();
    }

    m_current_in_flight_frame_index = (m_current_in_flight_frame_index + 1u) % MAX_FRAMES_IN_FLIGHT;
}

void Application::record_command_buffer(vk::CommandBuffer const command_buffer, uint32_t const image_index) {
    command_buffer.begin(vk::CommandBufferBeginInfo{});

    transition_image_layout(command_buffer, m_swapchain_images[image_index], vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);

    auto const color_attachment = vk::RenderingAttachmentInfo{
        .imageView = m_swapchain_image_views[image_index],
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    auto const rendering_info = vk::RenderingInfo{
        .renderArea = vk::Rect2D{
            .offset = vk::Offset2D{ 0, 0 },
            .extent = m_swapchain_extent,
        },
        .layerCount = 1u,
        .colorAttachmentCount = 1u,
        .pColorAttachments = &color_attachment,
    };
    command_buffer.beginRendering(rendering_info);

    command_buffer.bindPipeline(vk::PipelineBindPoint::eGraphics, m_graphics_pipeline);

    auto const viewport = vk::Viewport{
        .x = 0.f,
        .y = 0.f,
        .width = static_cast<float>(m_swapchain_extent.width),
        .height = static_cast<float>(m_swapchain_extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    command_buffer.setViewport(0u, viewport);

    auto const scissor = vk::Rect2D{
        .offset = vk::Offset2D{ 0, 0 },
        .extent = m_swapchain_extent,
    };
    command_buffer.setScissor(0u, scissor);

    if (m_tree64_depth > 0u) {
        auto const push_constants = PushConstants{
            .camera_position = m_camera.position(),
            .aspect_ratio = viewport.width / viewport.height,
            .camera_rotation = m_camera.rotation(),
            .tree64_depth = m_tree64_depth,
            .tree64_device_address = m_tree64_device_address,
        };
        command_buffer.pushConstants(m_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0u, vk::ArrayProxy<PushConstants const>({ push_constants }));
        command_buffer.draw(3u, 1u, 0u, 0u);
    }

    m_imgui->render(command_buffer);

    command_buffer.endRendering();

    transition_image_layout(command_buffer, m_swapchain_images[image_index], vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);

    command_buffer.end();
}

void Application::recreate_swapchain() {
    m_window.wait_for_valid_framebuffer();
    m_device.waitIdle();

    clean_swapchain();
    create_swapchain();
    m_should_recreate_swapchain = false;
}

void Application::clean_swapchain() {
    m_render_finished_semaphores.clear();
    m_swapchain_image_views.clear();
    m_swapchain_images.clear();
    m_swapchain.clear();
}

vk::raii::ImageView Application::create_image_view(vk::Image const image, vk::Format const format) const {
    auto const create_info = vk::ImageViewCreateInfo{
        .image = image,
        .viewType = vk::ImageViewType::e2D,
        .format = format,
        .components = vk::ComponentMapping{
            .r = vk::ComponentSwizzle::eIdentity,
            .g = vk::ComponentSwizzle::eIdentity,
            .b = vk::ComponentSwizzle::eIdentity,
            .a = vk::ComponentSwizzle::eIdentity,
        },
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0u,
            .levelCount = 1u,
            .baseArrayLayer = 0u,
            .layerCount = 1u,
        },
    };
    return vk::raii::ImageView(m_device, create_info);
}

void Application::copy_buffer(vk::Buffer const src, vk::Buffer const dst, vk::DeviceSize size) const {
    one_time_commands(m_device, m_command_pool, m_graphics_queue, [=](vk::CommandBuffer const command_buffer) {
        auto const copy_region = vk::BufferCopy{ .size = size };
        command_buffer.copyBuffer(src, dst, copy_region);
    });
}

void Application::copy_buffer_to_image(vk::Buffer const src, vk::Image const dst, uint32_t const width, uint32_t const height) const {
    one_time_commands(m_device, m_command_pool, m_graphics_queue, [=](vk::CommandBuffer const command_buffer) {
        auto const copy_region = vk::BufferImageCopy{
            .bufferOffset = 0u,
            .bufferRowLength = 0u,
            .bufferImageHeight = 0u,
            .imageSubresource = vk::ImageSubresourceLayers{
                .aspectMask = vk::ImageAspectFlagBits::eColor,
                .mipLevel = 0u,
                .baseArrayLayer = 0u,
                .layerCount = 1u,
            },
            .imageOffset = vk::Offset3D{ 0u, 0u, 0u },
            .imageExtent = vk::Extent3D{ width, height, 1u },
        };
        command_buffer.copyBufferToImage(src, dst, vk::ImageLayout::eTransferDstOptimal, copy_region);
    });
}

void Application::create_tree64_buffer(std::span<Tree64Node const> const nodes) {
    auto const buffer_size = std::size(nodes) * sizeof(nodes[0]);

    auto staging_buffer = VmaRaiiBuffer(m_allocator, buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);
    staging_buffer.copy_memory_to_allocation(reinterpret_cast<uint8_t const*>(std::data(nodes)), 0u, buffer_size);

    m_device.waitIdle();
    m_tree64_buffer.destroy();
    m_tree64_buffer = VmaRaiiBuffer(m_allocator, buffer_size, vk::BufferUsageFlagBits::eTransferDst
        | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    copy_buffer(staging_buffer, m_tree64_buffer, buffer_size);

    m_tree64_device_address = m_device.getBufferAddress(vk::BufferDeviceAddressInfo{
        .buffer = m_tree64_buffer,
    });
}

void Application::save_acceleration_structure(std::filesystem::path const& path) {
    auto const buffer_size = m_tree64_buffer.size();
    auto dst_buffer = VmaRaiiBuffer(m_allocator, buffer_size, vk::BufferUsageFlagBits::eTransferDst,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);
    copy_buffer(m_tree64_buffer, dst_buffer, buffer_size);
    auto nodes = std::vector<Tree64Node>(buffer_size / sizeof(Tree64Node));
    dst_buffer.copy_allocation_to_memory(0u, std::span(reinterpret_cast<uint8_t*>(std::data(nodes)), buffer_size));
    if (!save_t64(path, ContiguousTree64{ .depth = m_tree64_depth, .nodes = std::move(nodes) })) {
        std::cerr << "Cannot save acceleration structure to " << string_from(path) << std::endl;
    }
}

}
