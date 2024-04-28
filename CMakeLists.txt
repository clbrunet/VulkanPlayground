cmake_minimum_required(VERSION 3.25)

project(VulkanPlayground VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS On)

find_package(Vulkan REQUIRED COMPONENTS glslc)

set(GLFW_BUILD_EXAMPLES Off)
set(GLFW_BUILD_TESTS Off)
set(GLFW_BUILD_DOCS Off)
add_subdirectory(./ext/glfw SYSTEM)

add_subdirectory(./ext/glm SYSTEM)

set(SHADERS_DIRECTORY ${CMAKE_SOURCE_DIR}/assets/shaders)
set(SPIRV_SHADERS_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

file(GLOB_RECURSE SRCS src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.hpp)
file(GLOB_RECURSE SHADERS ${SHADERS_DIRECTORY}/*.vert ${SHADERS_DIRECTORY}/*.frag)

if(MSVC)
  set(VULKAN_PLAYGROUND_FLAGS
    /Wall
    /wd4820
    /external:anglebrackets
    /external:W0
    /analyze:external-
  )
else()
  set(VULKAN_PLAYGROUND_FLAGS
    -Wall
    -Wextra
    -Wpedantic
    -Wshadow
    -Wconversion
    $<$<CONFIG:Debug>:-g3>
  )
endif()

add_executable(VulkanPlayground ${SRCS} ${HEADERS})

target_compile_definitions(VulkanPlayground PRIVATE
  GLFW_INCLUDE_VULKAN
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  SPIRV_SHADERS_DIRECTORY=\"${SPIRV_SHADERS_DIRECTORY}\"
)
target_compile_options(VulkanPlayground PRIVATE ${VULKAN_PLAYGROUND_FLAGS})
target_include_directories(VulkanPlayground PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VulkanPlayground PRIVATE
  glfw
  glm
  Vulkan::Vulkan
)

set_target_properties(VulkanPlayground PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

foreach(SHADER ${SHADERS})
  file(RELATIVE_PATH RELATIVE_SHADER ${SHADERS_DIRECTORY} ${SHADER})
  set(SPIRV_SHADER ${SPIRV_SHADERS_DIRECTORY}/${RELATIVE_SHADER}.spv)
  get_filename_component(SPIRV_SHADER_DIRECTORY ${SPIRV_SHADER} DIRECTORY)

  add_custom_command(
    OUTPUT ${SPIRV_SHADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIRV_SHADER_DIRECTORY}
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADER} -o ${SPIRV_SHADER}
    MAIN_DEPENDENCY ${SHADER}
  )
  list(APPEND SPIRV_SHADERS ${SPIRV_SHADER})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_SHADERS})

add_dependencies(VulkanPlayground Shaders)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VulkanPlayground)
