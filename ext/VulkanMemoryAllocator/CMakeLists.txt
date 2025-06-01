find_package(Vulkan REQUIRED)

add_library(VulkanMemoryAllocator STATIC src/vk_mem_alloc.cpp)

target_include_directories(VulkanMemoryAllocator PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

target_link_libraries(VulkanMemoryAllocator PUBLIC Vulkan::Vulkan)
