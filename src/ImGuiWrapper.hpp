#pragma once

#include "Window.hpp"

#include <imgui_impl_vulkan.h>

namespace vp {

class ImGuiWrapper {
public:
    ImGuiWrapper(Window& window, ImGui_ImplVulkan_InitInfo& init_info);

    ImGuiWrapper(ImGuiWrapper&& other) = default;
    ImGuiWrapper(ImGuiWrapper const& other) = delete;

    ~ImGuiWrapper();

    ImGuiWrapper& operator=(ImGuiWrapper&& other) = default;
    ImGuiWrapper& operator=(ImGuiWrapper const& other) = delete;

    void begin_frame() const;
    void render(vk::CommandBuffer command_buffer) const;
    void update_windows() const;
};

}
