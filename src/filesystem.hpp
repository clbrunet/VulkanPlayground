#pragma once

#include <filesystem>
#include <string_view>
#include <fstream>
#include <vector>
#include <optional>

inline std::filesystem::path path_from(char const* const str_path) {
    return std::filesystem::path(reinterpret_cast<char8_t const*>(str_path));
}

inline std::filesystem::path path_from(std::string_view const string_view_path) {
    auto const u8string_view = std::u8string_view(
        reinterpret_cast<char8_t const*>(std::data(string_view_path)), std::size(string_view_path));
    return std::filesystem::path(u8string_view);
}

inline std::filesystem::path path_from(std::string const& string_path) {
    auto const u8string = std::u8string(std::cbegin(string_path), std::cend(string_path));
    return std::filesystem::path(u8string);
}

inline std::string string_from(std::filesystem::path const& path) {
    auto const u8string = path.u8string();
    return std::string(reinterpret_cast<char const*>(std::data(u8string)), std::size(u8string));
}

inline std::filesystem::path get_spirv_shader_path(std::string shader) {
    return path_from(SPIRV_SHADERS_DIRECTORY "/" + std::move(shader) + ".spv");
}

inline std::filesystem::path get_asset_path(std::string asset) {
    return path_from(ASSETS_DIRECTORY "/" + std::move(asset));
}

inline std::optional<std::vector<uint8_t>> read_binary_file(std::filesystem::path const& path) {
    auto ifstream = std::ifstream{ path, std::ios::ate | std::ios::binary };
    if (!ifstream) {
        return std::nullopt;
    }
    auto bytes = std::vector<uint8_t>(static_cast<size_t>(ifstream.tellg()));
    ifstream.seekg(0);
    ifstream.read(reinterpret_cast<char*>(std::data(bytes)), static_cast<std::streamsize>(std::size(bytes)));
    return std::make_optional(std::move(bytes));
}
