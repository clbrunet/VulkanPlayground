#include "ImGuiWrapper.hpp"
#include "math.hpp"

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <glm/gtc/color_space.hpp>

namespace vp {

ImGuiWrapper::ImGuiWrapper(Window& window, ImGui_ImplVulkan_InitInfo& init_info) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();

    auto& io = ImGui::GetIO();

    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NavEnableGamepad
        | ImGuiConfigFlags_DockingEnable | ImGuiConfigFlags_ViewportsEnable;

    io.IniFilename = ASSETS_DIRECTORY "/imgui.ini";

    io.Fonts->AddFontFromFileTTF(ASSETS_DIRECTORY "/fonts/NotoSans/NotoSans-VariableFont_wdth,wght.ttf");

    // convert colors to linear for an sRGB swapchain format
    for (auto& color : ImGui::GetStyle().Colors) {
        color = imvec4_from(glm::convertSRGBToLinear(vec4_from(color)));
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
