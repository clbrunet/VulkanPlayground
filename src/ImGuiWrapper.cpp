#include "ImGuiWrapper.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>

namespace vp {

ImGuiWrapper::ImGuiWrapper(Window& window, ImGui_ImplVulkan_InitInfo& init_info) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad
        | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

    ImGui::GetIO().IniFilename = ASSETS_DIRECTORY "/imgui.ini";

    // convert colors to linear for an sRGB swapchain format
    for (auto& color : ImGui::GetStyle().Colors) {
        auto const srgb_to_linear = [](float& component) {
            component = component <= 0.04045f ? component / 12.92f : glm::pow((component + 0.055f) / 1.055f, 2.4f);
        };
        srgb_to_linear(color.x);
        srgb_to_linear(color.y);
        srgb_to_linear(color.z);
    }

    window.init_imgui_for_vulkan();
    ImGui_ImplVulkan_Init(&init_info);

}

ImGuiWrapper::~ImGuiWrapper() {
    ImGui_ImplVulkan_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void ImGuiWrapper::begin_frame() const {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
}

void ImGuiWrapper::render(vk::CommandBuffer const command_buffer) const {
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command_buffer);
}

void ImGuiWrapper::update_windows() const {
    ImGui::UpdatePlatformWindows();
    ImGui::RenderPlatformWindowsDefault();
}

}
