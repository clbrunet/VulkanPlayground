#include "vulkan_utils.hpp"

VmaRaiiAllocator::VmaRaiiAllocator(std::nullptr_t) {
}

VmaRaiiAllocator::VmaRaiiAllocator(VmaAllocatorCreateFlags const flags, vk::PhysicalDevice const physical_device,
    vk::Device const device, vk::Instance const instance, uint32_t const vulkan_api_version) {
    auto const allocator_create_info = VmaAllocatorCreateInfo{
        .flags = flags,
        .physicalDevice = physical_device,
        .device = device,
        .instance = instance,
        .vulkanApiVersion = vulkan_api_version,
    };
    vmaCreateAllocator(&allocator_create_info, &m_allocator);
}

VmaRaiiAllocator::VmaRaiiAllocator(VmaRaiiAllocator&& other) noexcept {
    *this = std::move(other);
}

VmaRaiiAllocator::~VmaRaiiAllocator() {
    if (m_allocator) {
        vmaDestroyAllocator(m_allocator);
    }
}

VmaRaiiAllocator& VmaRaiiAllocator::operator=(VmaRaiiAllocator&& other) noexcept {
    std::swap(m_allocator, other.m_allocator);
    return *this;
}

VmaRaiiAllocator::operator VmaAllocator() {
    return m_allocator;
}

VmaRaiiBuffer::VmaRaiiBuffer(std::nullptr_t) {
}

VmaRaiiBuffer::VmaRaiiBuffer(VmaAllocator const allocator, vk::DeviceSize const size, vk::BufferUsageFlags const usage,
    VmaAllocationCreateFlags const allocation_flags, VmaMemoryUsage const memory_usage) :
    m_allocator{ allocator } {
    auto const create_info = vk::BufferCreateInfo{
        .size = size,
        .usage = usage,
    };
    auto const allocation_create_info = VmaAllocationCreateInfo{
        .flags = allocation_flags,
        .usage = memory_usage,
    };
    vmaCreateBuffer(allocator, reinterpret_cast<VkBufferCreateInfo const*>(&create_info),
        &allocation_create_info, reinterpret_cast<VkBuffer*>(&m_buffer), &m_allocation, nullptr);
}

VmaRaiiBuffer::VmaRaiiBuffer(VmaRaiiBuffer&& other) noexcept {
    *this = std::move(other);
}

VmaRaiiBuffer::~VmaRaiiBuffer() {
    if (m_buffer) {
        vmaDestroyBuffer(m_allocator, m_buffer, m_allocation);
    }
}

VmaRaiiBuffer& VmaRaiiBuffer::operator=(VmaRaiiBuffer&& other) noexcept {
    std::swap(m_allocator, other.m_allocator);
    std::swap(m_buffer, other.m_buffer);
    std::swap(m_allocation, other.m_allocation);
    return *this;
}

vk::Buffer VmaRaiiBuffer::operator*() {
    return m_buffer;
}

VmaRaiiBuffer::operator vk::Buffer() {
    return m_buffer;
}

vk::DeviceSize VmaRaiiBuffer::size() const {
    auto allocation_info = VmaAllocationInfo{};
    vmaGetAllocationInfo(m_allocator, m_allocation, &allocation_info);
    return allocation_info.size;
}

void VmaRaiiBuffer::copy_memory_to_allocation(uint8_t const* const src,
    vk::DeviceSize const offset, vk::DeviceSize const size) {
    vmaCopyMemoryToAllocation(m_allocator, src, m_allocation, offset, size);
}

void VmaRaiiBuffer::copy_allocation_to_memory(vk::DeviceSize const offset, std::span<uint8_t> const host_dst) const {
    vmaCopyAllocationToMemory(m_allocator, m_allocation, offset, std::data(host_dst), std::size(host_dst));
}

void VmaRaiiBuffer::destroy() {
    *this = VmaRaiiBuffer(nullptr);
}

void one_time_commands(vk::raii::Device const& device, vk::CommandPool const command_pool, vk::Queue const queue,
    std::function<void(vk::CommandBuffer)> const& commands_recorder) {
    auto const command_buffer_allocate_info = vk::CommandBufferAllocateInfo{
        .commandPool = command_pool,
        .level = vk::CommandBufferLevel::ePrimary,
        .commandBufferCount = 1u,
    };

    auto const command_buffer = std::move(vk::raii::CommandBuffers(device, command_buffer_allocate_info).front());

    auto const command_buffer_begin_info = vk::CommandBufferBeginInfo{
        .flags = vk::CommandBufferUsageFlagBits::eOneTimeSubmit,
    };
    command_buffer.begin(command_buffer_begin_info);

    commands_recorder(*command_buffer);

    command_buffer.end();

    auto const command_buffer_submit_info = vk::CommandBufferSubmitInfo{
        .commandBuffer = command_buffer,
    };
    queue.submit2(vk::SubmitInfo2{
        .commandBufferInfoCount = 1u,
        .pCommandBufferInfos = &command_buffer_submit_info,
    });
    // TODO: a fence should be used
    queue.waitIdle();
}

vk::raii::ImageView create_image_view(vk::raii::Device const& device, vk::Image const image, vk::Format const format) {
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
    return vk::raii::ImageView(device, create_info);
}

void transition_image_layout(vk::CommandBuffer const command_buffer, vk::Image const image,
    vk::ImageLayout const old_layout, vk::ImageLayout const new_layout) {
    auto memory_barrier = vk::ImageMemoryBarrier2{
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = vk::QueueFamilyIgnored,
        .dstQueueFamilyIndex = vk::QueueFamilyIgnored,
        .image = image,
        .subresourceRange = vk::ImageSubresourceRange{
            .aspectMask = vk::ImageAspectFlagBits::eColor,
            .baseMipLevel = 0u,
            .levelCount = vk::RemainingMipLevels,
            .baseArrayLayer = 0u,
            .layerCount = vk::RemainingArrayLayers,
        },
    };
    if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eTransferDstOptimal) {
        memory_barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
        memory_barrier.dstStageMask = vk::PipelineStageFlagBits2::eTransfer;
        memory_barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
        memory_barrier.dstAccessMask = vk::AccessFlagBits2::eTransferWrite;
    } else if (old_layout == vk::ImageLayout::eTransferDstOptimal && new_layout == vk::ImageLayout::eShaderReadOnlyOptimal) {
        memory_barrier.srcStageMask = vk::PipelineStageFlagBits2::eTransfer;
        memory_barrier.dstStageMask = vk::PipelineStageFlagBits2::eFragmentShader;
        memory_barrier.srcAccessMask = vk::AccessFlagBits2::eTransferWrite;
        memory_barrier.dstAccessMask = vk::AccessFlagBits2::eShaderRead;
    } else if (old_layout == vk::ImageLayout::eUndefined && new_layout == vk::ImageLayout::eColorAttachmentOptimal) {
        memory_barrier.srcStageMask = vk::PipelineStageFlagBits2::eNone;
        memory_barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        memory_barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
        memory_barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
    } else if (old_layout == vk::ImageLayout::eColorAttachmentOptimal && new_layout == vk::ImageLayout::ePresentSrcKHR) {
        memory_barrier.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
        memory_barrier.dstStageMask = vk::PipelineStageFlagBits2::eNone;
        memory_barrier.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
        memory_barrier.dstAccessMask = vk::AccessFlagBits2::eNone;
    } else {
        assert(false && "unsupported image layout transition");
    }

    command_buffer.pipelineBarrier2(vk::DependencyInfo{
        .imageMemoryBarrierCount = 1u,
        .pImageMemoryBarriers = &memory_barrier,
    });
}
