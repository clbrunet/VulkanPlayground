cmake_minimum_required(VERSION 3.25)

project(VulkanPlayground VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS On)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release")
endif()

if(MSVC)
  set(VULKAN_PLAYGROUND_FLAGS
    "/W4"
    "/wd26439"
    "/external:anglebrackets"
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
target_include_directories(VulkanPlayground PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/src")

set_target_properties(VulkanPlayground PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VulkanPlayground)
