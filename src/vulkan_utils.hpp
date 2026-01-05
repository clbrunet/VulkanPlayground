#pragma once

#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>
#include <vk_mem_alloc.h>

#include <functional>

class VmaRaiiAllocator {
public:
    VmaRaiiAllocator(std::nullptr_t);

    VmaRaiiAllocator(VmaAllocatorCreateFlags flags, vk::PhysicalDevice physical_device,
        vk::Device device, vk::Instance instance, uint32_t vulkan_api_version);

    VmaRaiiAllocator(VmaRaiiAllocator const& other) = delete;
    VmaRaiiAllocator(VmaRaiiAllocator&& other) noexcept;

    ~VmaRaiiAllocator();

    VmaRaiiAllocator& operator=(VmaRaiiAllocator const& other) = delete;
    VmaRaiiAllocator& operator=(VmaRaiiAllocator&& other) noexcept;

    operator VmaAllocator();

private:
    VmaAllocator m_allocator = nullptr;
};

class VmaRaiiBuffer {
public:
    VmaRaiiBuffer(std::nullptr_t);

    VmaRaiiBuffer(VmaAllocator allocator, vk::DeviceSize size, vk::BufferUsageFlags usage,
        VmaAllocationCreateFlags allocation_flags, VmaMemoryUsage memory_usage);

    VmaRaiiBuffer(VmaRaiiBuffer const& other) = delete;
    VmaRaiiBuffer(VmaRaiiBuffer&& other) noexcept;

    ~VmaRaiiBuffer();

    VmaRaiiBuffer& operator=(VmaRaiiBuffer const& other) = delete;
    VmaRaiiBuffer& operator=(VmaRaiiBuffer&& other) noexcept;

    vk::Buffer operator*();
    operator vk::Buffer();

    vk::DeviceSize size() const;
    void copy_memory_to_allocation(uint8_t const* src, vk::DeviceSize offset, vk::DeviceSize size);
    void copy_allocation_to_memory(vk::DeviceSize offset, std::span<uint8_t> host_dst) const;
    void destroy();

private:
    VmaAllocator m_allocator = nullptr;
    vk::Buffer m_buffer = vk::Buffer(nullptr);
    VmaAllocation m_allocation = nullptr;
};

void one_time_commands(vk::raii::Device const& device, vk::CommandPool const command_pool, vk::Queue const queue,
    std::function<void(vk::CommandBuffer)> const& commands_recorder);

vk::raii::ImageView create_image_view(vk::raii::Device const& device, vk::Image image, vk::Format format);

void transition_image_layout(vk::CommandBuffer command_buffer, vk::Image image,
    vk::ImageLayout old_layout, vk::ImageLayout new_layout);
