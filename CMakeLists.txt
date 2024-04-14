cmake_minimum_required(VERSION 3.25)

project(VulkanPlayground VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS On)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

set(GLFW_BUILD_EXAMPLES Off)
set(GLFW_BUILD_TESTS Off)
set(GLFW_BUILD_DOCS Off)
add_subdirectory("./ext/glfw" SYSTEM)

add_subdirectory("./ext/glm" SYSTEM)

if(MSVC)
  set(VULKAN_PLAYGROUND_FLAGS
    "/W4"
    "/wd26439"
    "/external:anglebrackets"
    "/external:W0"
    "/analyze:external-"
  )
else()
  set(VULKAN_PLAYGROUND_FLAGS
    "-Wall"
    "-Wextra"
    "-Wpedantic"
    "-Wshadow"
    "-Wconversion"
    $<$<CONFIG:Debug>:-g3>
  )
endif()

file(GLOB_RECURSE SRCS "src/*.cpp")

add_executable(VulkanPlayground ${SRCS})

target_compile_options(VulkanPlayground PRIVATE ${VULKAN_PLAYGROUND_FLAGS})
target_include_directories(VulkanPlayground PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src" "$ENV{VULKAN_SDK}/include")
target_link_libraries(VulkanPlayground PRIVATE
  glfw
  glm
  "$ENV{VULKAN_SDK}/lib/vulkan-1.lib"
)

set_target_properties(VulkanPlayground PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VulkanPlayground)
