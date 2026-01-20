#include "Application.hpp"
#include "filesystem.hpp"
#include "t64.hpp"
#include "math.hpp"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtc/integer.hpp>
#include <glm/ext/scalar_common.hpp>
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <ArHosekSkyModel.h>

#include <iostream>
#include <set>
#include <limits>
#include <functional>
#include <optional>
#include <span>
#include <filesystem>
#include <chrono>

namespace vp {

#pragma pack(push, 1)
struct GpuTree64 {
    vk::DeviceAddress tree64_nodes_device_address;
    uint32_t depth;
};
struct PushConstants {
    float aspect_ratio;
    glm::vec3 camera_position;
    glm::mat3 camera_rotation;
    glm::vec3 to_sun_direction;
    vk::DeviceAddress hosek_wilkie_sky_rendering_parameters_device_address;
    GpuTree64 tree64;
};

struct HosekWilkieSkyRenderingParameters {
    std::array<glm::vec3, 9u> config;
    glm::vec3 luminance;
};
#pragma pack(pop)

Application::Application() {
    // m_model_path_to_import = get_asset_path("models/sponza.vox");
    // m_model_path_to_import = get_asset_path("models/bistro_exterior.glb");
    m_model_path_to_import = get_asset_path("models/bistro_exterior_8k.t64");
    // m_camera.set_position(glm::vec3(15.5f, 120.f, 17.5f));
    // m_camera.set_euler_angles(glm::radians(glm::vec2(0.f, 45.f)));
    // m_model_path_to_import = get_asset_path("models/sponza.t64");
    start_model_import();
    init_window();
    init_vulkan();
    init_imgui();
}

Application::~Application() {
    if (*m_vk_ctx.device) {
        m_vk_ctx.device.waitIdle();
    }
}

static std::optional<ContiguousTree64> model_import(std::filesystem::path const& path, uint32_t const max_side_voxel_count) {
#if 1
    if (path.extension() == ".t64") {
        auto contiguous_tree64 = import_t64(path);
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
    auto tree64 = std::make_optional(Tree64(1u));
    tree64->add_voxel(glm::uvec3(0u, 0u, 0u));
    tree64->add_voxel(glm::uvec3(1u, 1u, 0u));
    tree64->add_voxel(glm::uvec3(0u, 1u, 1u));
    // tree64->add_voxel(glm::uvec3(4u, 0u, 0u));
    // tree64->add_voxel(glm::uvec3(0u, 0u, 4u));
    // tree64->add_voxel(glm::uvec3(8u, 0u, 0u));
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
        if (m_should_recreate_swapchain) {
            recreate_swapchain();
        }
    }
    m_vk_ctx.device.waitIdle();
}

void Application::init_window() {
    m_window.set_framebuffer_callback([&]([[maybe_unused]] uint16_t const width, [[maybe_unused]] uint16_t const height) {
        m_should_recreate_swapchain = true;
    });
}

void Application::init_vulkan() {
    m_vk_ctx = VulkanContext(m_window, DEVICE_REQUIRED_EXTENSIONS);
    std::cout << "Selected GPU : " << m_vk_ctx.physical_device.getProperties().deviceName << std::endl;
    auto const present_modes = m_vk_ctx.physical_device.getSurfacePresentModesKHR(m_vk_ctx.surface);
    m_has_immediate_present_mode = std::ranges::find(present_modes, vk::PresentModeKHR::eImmediate) != std::end(present_modes);

    recreate_swapchain();

    create_graphics_pipeline();

    create_command_pool();
    create_command_buffers();

    create_sync_objects();

    create_hosek_wilkie_sky_rendering_parameters_buffer();
}

void Application::recreate_swapchain() {
    auto const framebuffer_dimensions = m_window.wait_for_valid_framebuffer();
    m_swapchain.recreate(vk::Extent2D{
        .width = static_cast<uint32_t>(framebuffer_dimensions.x),
        .height = static_cast<uint32_t>(framebuffer_dimensions.y),
    }, m_use_v_sync ? vk::PresentModeKHR::eFifo : vk::PresentModeKHR::eImmediate);
    m_should_recreate_swapchain = false;
}

void Application::create_graphics_pipeline() {
    auto const rendering_create_info = pipeline_rendering_create_info();

    auto const shader_module = create_shader_module("raytracing.spv");
    auto const shader_stages = std::array{
        vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eVertex,
            .module = shader_module,
            .pName = "main",
        }, vk::PipelineShaderStageCreateInfo{
            .stage = vk::ShaderStageFlagBits::eFragment,
            .module = shader_module,
            .pName = "main",
        },
    };

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

    auto const push_constants_ranges = std::array{
        vk::PushConstantRange{
            .stageFlags = vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            .size = sizeof(PushConstants)
        },
    };
    auto const pipeline_layout_create_info = vk::PipelineLayoutCreateInfo{
        .pushConstantRangeCount = static_cast<uint32_t>(std::size(push_constants_ranges)),
        .pPushConstantRanges = std::data(push_constants_ranges),
    };
    m_pipeline_layout = vk::raii::PipelineLayout(m_vk_ctx.device, pipeline_layout_create_info);

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
    m_graphics_pipeline = vk::raii::Pipeline(m_vk_ctx.device, nullptr, create_info);
}

vk::PipelineRenderingCreateInfo Application::pipeline_rendering_create_info() const {
    return vk::PipelineRenderingCreateInfo{
        .colorAttachmentCount = 1u,
        .pColorAttachmentFormats = &m_swapchain.format(),
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

    return vk::raii::ShaderModule(m_vk_ctx.device, create_info);
}

void Application::create_command_pool() {
    auto const create_info = vk::CommandPoolCreateInfo{
        .flags = vk::CommandPoolCreateFlagBits::eResetCommandBuffer,
        .queueFamilyIndex = m_vk_ctx.graphics_queue_family_index,
    };
    m_command_pool = vk::raii::CommandPool(m_vk_ctx.device, create_info);
}

void Application::create_command_buffers() {
    auto const allocate_info = vk::CommandBufferAllocateInfo{
        .commandPool = m_command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = MAX_FRAMES_IN_FLIGHT,
    };
    m_command_buffers = vk::raii::CommandBuffers(m_vk_ctx.device, allocate_info);
}

void Application::create_sync_objects() {
    m_image_available_semaphores.reserve(MAX_FRAMES_IN_FLIGHT);
    m_in_flight_fences.reserve(MAX_FRAMES_IN_FLIGHT);
    auto const in_flight_fence_create_info = vk::FenceCreateInfo{
        .flags = vk::FenceCreateFlagBits::eSignaled,
    };
    for (auto i = 0u; i < MAX_FRAMES_IN_FLIGHT; ++i) {
        m_image_available_semaphores.emplace_back(m_vk_ctx.device, vk::SemaphoreCreateInfo{});
        m_in_flight_fences.emplace_back(m_vk_ctx.device, in_flight_fence_create_info);
    }
}

void Application::create_hosek_wilkie_sky_rendering_parameters_buffer() {
    m_hosek_wilkie_sky_rendering_parameters_buffer = VmaRaiiBuffer(m_vk_ctx.allocator, sizeof(HosekWilkieSkyRenderingParameters),
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eShaderDeviceAddress, 0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    m_hosek_wilkie_sky_rendering_parameters_device_address = m_vk_ctx.device.getBufferAddress(vk::BufferDeviceAddressInfo{
        .buffer = m_hosek_wilkie_sky_rendering_parameters_buffer,
    });
    update_hosek_wilkie_sky_rendering_parameters();
}

void Application::init_imgui() {
    auto init_info = ImGui_ImplVulkan_InitInfo{
        .ApiVersion = VulkanContext::API_VERSION,
        .Instance = *m_vk_ctx.instance,
        .PhysicalDevice = *m_vk_ctx.physical_device,
        .Device = *m_vk_ctx.device,
        .QueueFamily = m_vk_ctx.graphics_queue_family_index,
        .Queue = *m_vk_ctx.graphics_queue,
        .MinImageCount = m_swapchain.image_count(),
        .ImageCount = m_swapchain.image_count(),
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
    auto fullscreen_status = m_window.fullscreen_status();
    if (ImGui::Checkbox("Fullscreen", &fullscreen_status)) {
        m_window.set_fullscreen_status(fullscreen_status);
    }
    if (m_has_immediate_present_mode) {
        ImGui::SameLine();
        if (ImGui::Checkbox("V-Sync", &m_use_v_sync)) {
            m_should_recreate_swapchain = true;
        }
    }

    ImGui::Text("Hold right click to move/rotate the camera");
    ImGui::Text("Speed is adjustable using mouse wheel and Shift/Alt");
    auto position = m_camera.position();
    ImGui::DragFloat3("Camera position", glm::value_ptr(position));
    m_camera.set_position(position);
    auto degrees_euler_angles = glm::degrees(m_camera.euler_angles());
    ImGui::DragFloat2("Camera rotation", glm::value_ptr(degrees_euler_angles));
    m_camera.set_euler_angles(glm::radians(degrees_euler_angles));

    ImGui::SeparatorText("Importing");
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
    } else if (m_tree64_nodes_device_address != 0u && ImGui::Button("Save displayed acceleration structure")) {
        auto const filters = std::array{ nfdu8filteritem_t{ "Tree64", "t64" } };
        auto const path = m_window.pick_saving_path(filters, get_asset_path("models"));
        if (path.has_value()) {
            save_acceleration_structure(path.value());
        }
    }
    ImGui::SeparatorText("Sky");
    auto sky_changed = false;
    sky_changed |= ImGui::SliderAngle("Sun elevation", &m_sun_elevation, 0.f, 90.f);
    sky_changed |= ImGui::SliderAngle("Sun rotation", &m_sun_rotation, -180.f, 180.f);
    sky_changed |= ImGui::SliderFloat("Turbidity", &m_hosek_wilkie_sky_turbidity, 1.f, 10.f);
    sky_changed |= ImGui::SliderFloat("Albedo", &m_hosek_wilkie_sky_albedo, 0.f, 1.f);
    if (sky_changed) {
        update_hosek_wilkie_sky_rendering_parameters();
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
    static_cast<void>(m_vk_ctx.device.waitForFences(*in_flight_fence, vk::True, std::numeric_limits<uint64_t>::max()));

    auto const& image_available_semaphore = m_image_available_semaphores[m_current_in_flight_frame_index];
    auto acquired_image_opt = m_swapchain.acquire_next_image(image_available_semaphore);
    if (!acquired_image_opt.has_value()) {
        m_should_recreate_swapchain = true;
        return;
    }
    auto const& acquired_image = acquired_image_opt.value();

    m_vk_ctx.device.resetFences(*in_flight_fence);

    auto const& command_buffer = m_command_buffers[m_current_in_flight_frame_index];
    command_buffer.reset();
    record_command_buffer(command_buffer, acquired_image);

    auto const image_available_semaphore_submit_info = vk::SemaphoreSubmitInfo{
        .semaphore = image_available_semaphore,
        .stageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput,
    };
    auto const command_buffer_submit_info = vk::CommandBufferSubmitInfo{
        .commandBuffer = command_buffer,
    };
    auto const render_finished_semaphore_submit_info = vk::SemaphoreSubmitInfo{
        .semaphore = acquired_image.render_finished_semaphore,
        .stageMask = vk::PipelineStageFlagBits2::eAllCommands,
    };
    m_vk_ctx.graphics_queue.submit2(vk::SubmitInfo2{
        .waitSemaphoreInfoCount = 1u,
        .pWaitSemaphoreInfos = &image_available_semaphore_submit_info,
        .commandBufferInfoCount = 1u,
        .pCommandBufferInfos = &command_buffer_submit_info,
        .signalSemaphoreInfoCount = 1u,
        .pSignalSemaphoreInfos = &render_finished_semaphore_submit_info,
    }, in_flight_fence);

    m_imgui->update_windows();

    if (!m_swapchain.queue_present(acquired_image)) {
        m_should_recreate_swapchain = true;
    }

    m_current_in_flight_frame_index = (m_current_in_flight_frame_index + 1u) % MAX_FRAMES_IN_FLIGHT;
}

void Application::record_command_buffer(vk::CommandBuffer const command_buffer, Swapchain::AcquiredImage const& acquired_image) {
    command_buffer.begin(vk::CommandBufferBeginInfo{});

    transition_image_layout(command_buffer, acquired_image.image, vk::ImageLayout::eUndefined, vk::ImageLayout::eColorAttachmentOptimal);

    auto const color_attachment = vk::RenderingAttachmentInfo{
        .imageView = acquired_image.view,
        .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
        .loadOp = vk::AttachmentLoadOp::eDontCare,
        .storeOp = vk::AttachmentStoreOp::eStore,
    };
    auto const rendering_info = vk::RenderingInfo{
        .renderArea = vk::Rect2D{
            .offset = vk::Offset2D{ 0, 0 },
            .extent = m_swapchain.extent(),
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
        .width = static_cast<float>(rendering_info.renderArea.extent.width),
        .height = static_cast<float>(rendering_info.renderArea.extent.height),
        .minDepth = 0.f,
        .maxDepth = 1.f,
    };
    command_buffer.setViewport(0u, viewport);
    command_buffer.setScissor(0u, rendering_info.renderArea);

    if (m_tree64_depth > 0u) {
        auto const push_constants = PushConstants{
            .aspect_ratio = viewport.width / viewport.height,
            .camera_position = m_camera.position(),
            .camera_rotation = m_camera.rotation(),
            .to_sun_direction = cartesian_direction_from_spherical(m_sun_elevation, m_sun_rotation),
            .hosek_wilkie_sky_rendering_parameters_device_address = m_hosek_wilkie_sky_rendering_parameters_device_address,
            .tree64 {
                .tree64_nodes_device_address = m_tree64_nodes_device_address,
                .depth = m_tree64_depth,
            },
        };
        command_buffer.pushConstants(m_pipeline_layout, vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment,
            0u, vk::ArrayProxy<PushConstants const>({ push_constants }));
        command_buffer.draw(3u, 1u, 0u, 0u);
    }

    m_imgui->render(command_buffer);

    command_buffer.endRendering();

    transition_image_layout(command_buffer, acquired_image.image, vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR);

    command_buffer.end();
}

void Application::copy_buffer(vk::Buffer const src, vk::Buffer const dst, vk::DeviceSize size) const {
    one_time_commands(m_vk_ctx.device, m_command_pool, m_vk_ctx.graphics_queue, [=](vk::CommandBuffer const command_buffer) {
        auto const copy_region = vk::BufferCopy{ .size = size };
        command_buffer.copyBuffer(src, dst, copy_region);
    });
}

void Application::copy_buffer_to_image(vk::Buffer const src, vk::Image const dst, uint32_t const width, uint32_t const height) const {
    one_time_commands(m_vk_ctx.device, m_command_pool, m_vk_ctx.graphics_queue, [=](vk::CommandBuffer const command_buffer) {
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

    auto staging_buffer = VmaRaiiBuffer(m_vk_ctx.allocator, buffer_size, vk::BufferUsageFlagBits::eTransferSrc,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);
    staging_buffer.copy_memory_to_allocation(reinterpret_cast<uint8_t const*>(std::data(nodes)), 0u, buffer_size);

    m_vk_ctx.device.waitIdle();
    m_tree64_nodes_buffer.destroy();
    m_tree64_nodes_buffer = VmaRaiiBuffer(m_vk_ctx.allocator, buffer_size, vk::BufferUsageFlagBits::eTransferDst
        | vk::BufferUsageFlagBits::eShaderDeviceAddress | vk::BufferUsageFlagBits::eTransferSrc,
        0, VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE);
    copy_buffer(staging_buffer, m_tree64_nodes_buffer, buffer_size);

    m_tree64_nodes_device_address = m_vk_ctx.device.getBufferAddress(vk::BufferDeviceAddressInfo{
        .buffer = m_tree64_nodes_buffer,
    });
}

void Application::save_acceleration_structure(std::filesystem::path const& path) {
    auto const buffer_size = m_tree64_nodes_buffer.size();
    auto dst_buffer = VmaRaiiBuffer(m_vk_ctx.allocator, buffer_size, vk::BufferUsageFlagBits::eTransferDst,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);
    copy_buffer(m_tree64_nodes_buffer, dst_buffer, buffer_size);
    auto nodes = std::vector<Tree64Node>(buffer_size / sizeof(Tree64Node));
    dst_buffer.copy_allocation_to_memory(0u, std::span(reinterpret_cast<uint8_t*>(std::data(nodes)), buffer_size));
    if (!save_t64(path, ContiguousTree64{ .depth = m_tree64_depth, .nodes = std::move(nodes) })) {
        std::cerr << "Cannot save acceleration structure to " << string_from(path) << std::endl;
    }
}

void Application::update_hosek_wilkie_sky_rendering_parameters() {
    auto* const sky_model = arhosek_rgb_skymodelstate_alloc_init(m_hosek_wilkie_sky_turbidity, m_hosek_wilkie_sky_albedo, m_sun_elevation);
    auto rendering_params = HosekWilkieSkyRenderingParameters{};
    for (auto i = 0u; i < std::size(rendering_params.config); ++i) {
        rendering_params.config[i] = glm::vec3(sky_model->configs[0][i], sky_model->configs[1][i], sky_model->configs[2][i]);
    }
    rendering_params.luminance = glm::vec3(sky_model->radiances[0], sky_model->radiances[1], sky_model->radiances[2])
        * (2.f * glm::pi<float>() / 683.f), // convert from radiance to luminance
    arhosekskymodelstate_free(sky_model);

    auto staging_buffer = VmaRaiiBuffer(m_vk_ctx.allocator, sizeof(HosekWilkieSkyRenderingParameters), vk::BufferUsageFlagBits::eTransferSrc,
        VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT, VMA_MEMORY_USAGE_AUTO);
    staging_buffer.copy_memory_to_allocation(reinterpret_cast<uint8_t const*>(&rendering_params), 0u, sizeof(HosekWilkieSkyRenderingParameters));

    m_vk_ctx.device.waitIdle();
    copy_buffer(staging_buffer, m_hosek_wilkie_sky_rendering_parameters_buffer, sizeof(HosekWilkieSkyRenderingParameters));
}

}
