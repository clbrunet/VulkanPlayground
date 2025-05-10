cmake_minimum_required(VERSION 3.25)

project(VulkanPlayground VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Vulkan REQUIRED COMPONENTS glslc)

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(ext/glfw SYSTEM)

add_subdirectory(ext/glm SYSTEM)

add_subdirectory(ext/stb SYSTEM)

set(BUILD_SHARED_LIBS OFF)
set(ASSIMP_NO_EXPORT ON)
set(ASSIMP_BUILD_ALL_EXPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_ALL_IMPORTERS_BY_DEFAULT OFF)
set(ASSIMP_BUILD_TESTS OFF)
set(ASSIMP_INSTALL OFF)
set(ASSIMP_WARNINGS_AS_ERRORS OFF)
set(ASSIMP_BUILD_GLTF_IMPORTER ON)
add_subdirectory("./ext/assimp" SYSTEM)

set(SHADERS_DIRECTORY ${CMAKE_SOURCE_DIR}/shaders)
set(SPIRV_SHADERS_DIRECTORY ${CMAKE_BINARY_DIR}/shaders)

file(GLOB_RECURSE SRCS src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.hpp)
file(GLOB_RECURSE SHADERS ${SHADERS_DIRECTORY}/*.vert ${SHADERS_DIRECTORY}/*.frag)

if(MSVC)
  set(VULKAN_PLAYGROUND_FLAGS
    /Wall
    /wd4505 # 'function' : unreferenced function with internal linkage has been removed
    /wd4514 # 'function' : unreferenced inline function has been removed
    /wd4710 # 'function' : function not inlined
    /wd4711 # function 'function' selected for inline expansion
    /wd4820 # 'bytes' bytes padding added after construct 'member_name'
    /wd4868 # compiler may not enforce left-to-right evaluation order in braced initializer list
    /wd5045 # Compiler will insert Spectre mitigation for memory load if /Qspectre switch specified
    /wd6001 # disabled due to false positives # Using uninitialized memory 'variable'.
    /wd28020 # disabled due to false positives # The expression 'expr' is not true at this call.
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
  VULKAN_HPP_NO_CONSTRUCTORS
  GLM_FORCE_DEPTH_ZERO_TO_ONE
  GLM_ENABLE_EXPERIMENTAL
  SPIRV_SHADERS_DIRECTORY=\"${SPIRV_SHADERS_DIRECTORY}\"
  ASSETS_DIRECTORY=\"${CMAKE_SOURCE_DIR}/assets\"
)
target_compile_options(VulkanPlayground PRIVATE ${VULKAN_PLAYGROUND_FLAGS})
target_include_directories(VulkanPlayground PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VulkanPlayground PRIVATE
  glfw
  Vulkan::Vulkan
  glm
  stb
  assimp
)

set_target_properties(VulkanPlayground PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY ${CMAKE_SOURCE_DIR})

if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  set(SHADERS_COMPILE_FLAGS -g -O0)
else()
  set(SHADERS_COMPILE_FLAGS -O)
endif()

foreach(SHADER ${SHADERS})
  file(RELATIVE_PATH RELATIVE_SHADER ${SHADERS_DIRECTORY} ${SHADER})
  set(SPIRV_SHADER ${SPIRV_SHADERS_DIRECTORY}/${RELATIVE_SHADER}.spv)
  get_filename_component(SPIRV_SHADER_DIRECTORY ${SPIRV_SHADER} DIRECTORY)

  add_custom_command(
    OUTPUT ${SPIRV_SHADER}
    COMMAND ${CMAKE_COMMAND} -E make_directory ${SPIRV_SHADER_DIRECTORY}
    COMMAND ${Vulkan_GLSLC_EXECUTABLE} ${SHADERS_COMPILE_FLAGS} ${SHADER} -o ${SPIRV_SHADER}
    MAIN_DEPENDENCY ${SHADER}
  )
  list(APPEND SPIRV_SHADERS ${SPIRV_SHADER})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_SHADERS})

add_dependencies(VulkanPlayground Shaders)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VulkanPlayground)
