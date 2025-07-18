#include "Application.hpp"
#include "filesystem.hpp"

#include <glm/ext/scalar_common.hpp>
#include <glm/gtc/integer.hpp>
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
#include <concepts>
#include <chrono>

namespace vp {

#ifndef NDEBUG
constexpr auto VALIDATION_LAYER = "VK_LAYER_KHRONOS_validation";
#endif

struct PushConstants {
    glm::vec3 camera_position;
    float aspect_ratio;
    glm::mat3 camera_rotation;
    uint32_t tree64_depth;
    vk::DeviceAddress tree64_device_address;
};

constexpr auto VULKAN_API_VERSION = vk::ApiVersion13;

Application::Application() {
    m_model_path_to_import = get_asset_path("models/bistro_exterior.glb");
    // m_model_path_to_import = get_asset_path("models/sponza.vox");
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

void Application::start_model_import() {
    m_model_import_future = std::async([] (std::filesystem::path const& path, uint32_t const max_side_voxel_count) {
        auto const begin_time = std::chrono::high_resolution_clock::now();

        auto tree64 = std::optional<Tree64>();
        if (path.extension() == ".vox") {
            tree64 = Tree64::import_vox(path);
        } else {
            tree64 = Tree64::voxelize_model(path, max_side_voxel_count);
        }
        if (!tree64.has_value()) {
            std::cerr << "Cannot import " << string_from(path) << std::endl;
            return std::tuple(uint8_t{ 0u }, std::vector<Tree64Node>());
        }

        auto const import_done_time = std::chrono::high_resolution_clock::now();
        auto const import_time = std::chrono::duration_cast<std::chrono::duration<float>>(import_done_time - begin_time);
        std::cout << "import time " << import_time << std::endl;

        auto const nodes = tree64->build_contiguous_nodes();

        std::cout << "build contiguous time " << std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::high_resolution_clock::now() - import_done_time) << std::endl;
        std::cout << "full time " << std::chrono::duration_cast<std::chrono::duration<float>>(std::chrono::high_resolution_clock::now() - begin_time) << std::endl;
        std::cout << "node count " << nodes.size() << std::endl;
        return std::tuple(tree64->depth(), nodes);
    }, m_model_path_to_import, m_max_side_voxel_count_to_import);
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
    create_image_views();

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
    auto extensions = std::vector<char const*>{ glfw_extensions, glfw_extensions + glfw_extension_count };

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
    m_instance = vk::raii::Instance{ m_context, create_info };
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
    m_debug_messenger = vk::raii::DebugUtilsMessengerEXT{ m_instance, get_debug_messenger_create_info() };
}

vk::DebugUtilsMessengerCreateInfoEXT Application::get_debug_messenger_create_info() {
    return vk::DebugUtilsMessengerCreateInfoEXT{
        .messageSeverity = vk::DebugUtilsMessageSeverityFlagBitsEXT::eVerbose
            | vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning | vk::DebugUtilsMessageSeverityFlagBitsEXT::eError,
        .messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation
            | vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance | vk::DebugUtilsMessageTypeFlagBitsEXT::eDeviceAddressBinding,
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
    m_surface = vk::raii::SurfaceKHR{ m_instance, m_window.create_surface(*m_instance) };
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
        throw std::runtime_error{ "no suitable GPU found" };
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
    auto const unique_queue_family_indices = std::set<uint32_t>{
        queue_family_indices.graphics,
        queue_family_indices.present,
    };
    auto const queue_priority = 1.f;
    auto queue_create_infos = std::vector<vk::DeviceQueueCreateInfo>{};
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
    m_device = vk::raii::Device{ m_physical_device, create_info };
    m_graphics_queue = m_device.getQueue(queue_family_indices.graphics, 0u);
    m_present_queue = m_device.getQueue(queue_family_indices.present, 0u);

    m_allocator = VmaRaiiAllocator{ VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT, m_physical_device, m_device, m_instance, VULKAN_API_VERSION };
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
            return std::tuple{ vk::SharingMode::eConcurrent, 2u };
        }
        return std::tuple{ vk::SharingMode::eExclusive, 0u };
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
        .clipped = vk::True,
        .oldSwapchain = vk::SwapchainKHR{},
    };
    m_swapchain = vk::raii::SwapchainKHR{ m_device, create_info };
    m_swapchain_images = m_swapchain.getImages();
    m_swapchain_format = surface_format.format;
    m_swapchain_extent = image_extent;
}

void Application::create_image_views() {
    std::ranges::transform(m_swapchain_images, std::back_inserter(m_swapchain_image_views), [&](vk::Image const swapchain_image) {
        return create_image_view(swapchain_image, m_swapchain_format);
    });
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
    m_pipeline_layout = vk::raii::PipelineLayout{ m_device, pipeline_layout_create_info };

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
    m_graphics_pipeline = vk::raii::Pipeline{ m_device, nullptr, create_info };
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
        throw std::runtime_error{ "cannot read \"" + string_from(spirv_path) + '"'};
    }

    auto const create_info = vk::ShaderModuleCreateInfo{
        .codeSize = std::size(*code),
        .pCode = reinterpret_cast<uint32_t const*>(std::data(*code)),
    };

    return vk::raii::ShaderModule{ m_device, create_info };
}

void Application::create_command_pool() {
    auto const create_info = vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = get_queue_family_indices().graphics,
    };
    m_command_pool = vk::raii::CommandPool{ m_device, create_info };
}

void Application::create_command_buffers() {
    auto const allocate_info = vk::CommandBufferAllocateInfo{
        .commandPool = m_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    m_command_buffers = vk::raii::CommandBuffers{ m_device, allocate_info };
}

void Application::create_sync_objects() {
    m_image_available_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    m_render_finished_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    m_in_flight_fences.reserve(MAX_FRAMES_IN_FLIGHT);
    auto const in_flight_fence_create_info = vk::FenceCreateInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    for (auto i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_image_available_semaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
        m_render_finished_semaphores.emplace_back(m_device, vk::SemaphoreCreateInfo{});
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
    ImGui::Text("Hold right click to move the camera");

    auto model_path_to_import = string_from(m_model_path_to_import);
    ImGui::SetNextItemWidth(-45.f);
    if (ImGui::InputText("##model_path_to_import", &model_path_to_import, ImGuiInputTextFlags_ElideLeft)) {
        m_model_path_to_import = path_from(model_path_to_import);
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Open")) {
        auto const filters = std::array{ nfdu8filteritem_t{ "Models", "vox,glb,gltf" } };
        auto path = m_window.pick_file(filters, get_asset_path("models"));
        if (path.has_value()) {
            m_model_path_to_import = std::move(path.value());
        }
    }

    if (m_model_path_to_import.extension() != ".vox") {
        auto const min = 4u;
        auto const max = 4096u;
        ImGui::DragScalar("Max side voxel count", ImGuiDataType_U32,
            &m_max_side_voxel_count_to_import, 1.f, &min, &max);
    }

    if (m_model_import_future.valid()) {
        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::ProgressBar(-1.f * static_cast<float>(ImGui::GetTime()), ImVec2(0.0f, 0.0f), "Importing...");
    } else if (ImGui::Button("Import")) {
        start_model_import();
    }
    ImGui::End();
}

void Application::update_tree64_buffer() {
    if (m_model_import_future.valid()
        && m_model_import_future.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto [depth, nodes] = m_model_import_future.get();
        if (depth > 0u) {
            m_tree64_depth = depth;
            create_tree64_buffer(nodes);
        }
    }
}

void Application::draw_frame() {
    auto const& in_flight_fence = m_in_flight_fences[m_current_in_flight_frame_index];
    auto const& image_available_semaphore = m_image_available_semaphores[m_current_in_flight_frame_index];
    auto const& command_buffer = m_command_buffers[m_current_in_flight_frame_index];
    auto const& render_finished_semaphore = m_render_finished_semaphores[m_current_in_flight_frame_index];

    static_cast<void>(m_device.waitForFences(*in_flight_fence, vk::True, std::numeric_limits<uint64_t>::max()));

    auto image_index = uint32_t{};
    try {
        image_index = m_swapchain.acquireNextImage(std::numeric_limits<uint64_t>::max(), image_available_semaphore).second;
    } catch (vk::OutOfDateKHRError const&) {
        recreate_swapchain();
        return;
    }

    m_device.resetFences(*in_flight_fence);

    command_buffer.reset();
    record_command_buffer(command_buffer, image_index);

    auto const wait_semaphore_submit_info = vk::SemaphoreSubmitInfo{
        .semaphore = image_available_semaphore,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    auto const command_buffer_submit_info = vk::CommandBufferSubmitInfo{
        .commandBuffer = command_buffer,
    };
    auto const render_finished_semaphore_submit_info = vk::SemaphoreSubmitInfo{
        .semaphore = render_finished_semaphore,
        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
    };
    m_graphics_queue.submit2(vk::SubmitInfo2{
        .waitSemaphoreInfoCount = 1u,
        .pWaitSemaphoreInfos = &wait_semaphore_submit_info,
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
        auto const result = m_graphics_queue.presentKHR(present_info);
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
            .offset = vk::Offset2D{ 0, 0, },
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
            0u, vk::ArrayProxy<PushConstants const>{ push_constants });
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
    create_image_views();

    m_should_recreate_swapchain = false;
}

void Application::clean_swapchain() {
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

    return vk::raii::ImageView{ m_device, create_info };
}

void Application::one_time_commands(std::invocable<vk::CommandBuffer> auto const& commands_recorder) const {
    auto const command_buffer_allocate_info = vk::CommandBufferAllocateInfo{
        .commandPool = m_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1u,
    };

    auto const command_buffer = std::move(vk::raii::CommandBuffers{ m_device, command_buffer_allocate_info }.front());

    auto const command_buffer_begin_info = vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    command_buffer.begin(command_buffer_begin_info);

    commands_recorder(*command_buffer);

    command_buffer.end();

    auto const command_buffer_submit_info = vk::CommandBufferSubmitInfo{
        .commandBuffer = command_buffer,
    };
    m_graphics_queue.submit2(vk::SubmitInfo2{
        .commandBufferInfoCount = 1u,
        .pCommandBufferInfos = &command_buffer_submit_info,
    });
    m_graphics_queue.waitIdle();
}

void Application::copy_buffer(vk::Buffer const src, vk::Buffer const dst, vk::DeviceSize size) const {
    one_time_commands([=](vk::CommandBuffer const command_buffer) {
        auto const copy_region = vk::BufferCopy{ .size = size };
        command_buffer.copyBuffer(src, dst, copy_region);
    });
}

void Application::copy_buffer_to_image(vk::Buffer const src, vk::Image const dst, uint32_t const width, uint32_t const height) const {
    one_time_commands([=](vk::CommandBuffer const command_buffer) {
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

void Application::create_tree64_buffer(std::vector<Tree64Node> const& nodes) {
    auto const buffer_size = std::size(nodes) * sizeof(nodes[0]);

    auto staging_buffer = VmaRaiiBuffer{ m_allocator, buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO };
    staging_buffer.copy_memory_to_allocation(reinterpret_cast<void const*>(nodes.data()), 0u, buffer_size);

    m_device.waitIdle();
    m_tree64_buffer.destroy();
    m_tree64_buffer = VmaRaiiBuffer{ m_allocator, buffer_size, vk::BufferUsageFlagBits::eTransferDst
        | vk::BufferUsageFlagBits::eShaderDeviceAddress, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE };
    copy_buffer(staging_buffer, m_tree64_buffer, buffer_size);

    m_tree64_device_address = m_device.getBufferAddress(vk::BufferDeviceAddressInfo{
        .buffer = m_tree64_buffer,
    });
}

}
