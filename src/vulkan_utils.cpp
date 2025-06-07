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

void VmaRaiiBuffer::copy_memory_to_allocation(void const* src, vk::DeviceSize offset, vk::DeviceSize size) {
	vmaCopyMemoryToAllocation(m_allocator, src, m_allocation, offset, size);
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
