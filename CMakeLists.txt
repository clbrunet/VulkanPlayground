cmake_minimum_required(VERSION 3.25)

project(VulkanPlayground VERSION 0.1.0)

get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(IS_MULTI_CONFIG)
    if(NOT CMAKE_CONFIGURATION_TYPES)
        set(CMAKE_CONFIGURATION_TYPES "Debug;Release" CACHE STRING "Multi-config build types" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/$<CONFIG>)
else()
    if(NOT CMAKE_BUILD_TYPE)
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Single-config build type" FORCE)
    endif()
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GLFW_BUILD_EXAMPLES OFF)
set(GLFW_BUILD_TESTS OFF)
set(GLFW_BUILD_DOCS OFF)
add_subdirectory(ext/glfw SYSTEM)

if(UNIX AND NOT APPLE)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(GTK3 gtk+-3.0)
    if(NOT GTK3_FOUND)
        message(FATAL_ERROR
            " GTK 3 development package not found.\n"
            " Please install it on your system.\n"
            " Debian / Ubuntu : sudo apt install libgtk-3-dev\n"
        )
    endif()
endif()
add_subdirectory(ext/nativefiledialog-extended SYSTEM)

find_package(Vulkan REQUIRED COMPONENTS glslc)

add_subdirectory(ext/VulkanMemoryAllocator SYSTEM)

add_subdirectory(ext/imgui SYSTEM)

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
add_subdirectory(ext/assimp SYSTEM)

add_subdirectory(ext/HosekWilkie_SkylightModel_C_Source SYSTEM)

file(GLOB_RECURSE SRCS src/*.cpp)
file(GLOB_RECURSE HEADERS src/*.hpp)
file(GLOB_RECURSE SHADERS shaders/*.vert shaders/*.frag)

if(MSVC)
    set(VULKAN_PLAYGROUND_FLAGS
        /utf-8
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
        -Wno-missing-field-initializers
        $<$<CONFIG:Debug>:-g3>
    )
endif()

add_executable(VulkanPlayground ${SRCS} ${HEADERS})

target_compile_definitions(VulkanPlayground PRIVATE
    NOMINMAX
    GLFW_INCLUDE_NONE
    GLFW_INCLUDE_VULKAN
    VULKAN_HPP_NO_CONSTRUCTORS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_ENABLE_EXPERIMENTAL
    SPIRV_SHADERS_DIRECTORY=\"${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/shaders\"
    ASSETS_DIRECTORY=\"${CMAKE_SOURCE_DIR}/assets\"
)
target_compile_options(VulkanPlayground PRIVATE ${VULKAN_PLAYGROUND_FLAGS})
target_include_directories(VulkanPlayground PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VulkanPlayground PRIVATE
    glfw
    nfd
    Vulkan::Vulkan
    VulkanMemoryAllocator
    imgui
    glm
    stb
    assimp
    HosekWilkieSkyLightModel
)

foreach(SHADER ${SHADERS})
    file(RELATIVE_PATH RELATIVE_SHADER ${CMAKE_SOURCE_DIR} ${SHADER})
    set(SPIRV_SHADER ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/${RELATIVE_SHADER}.spv)
    file(RELATIVE_PATH RELATIVE_SPIRV_SHADER ${CMAKE_SOURCE_DIR} ${SPIRV_SHADER})
    get_filename_component(RELATIVE_SPIRV_SHADER_DIRECTORY ${RELATIVE_SPIRV_SHADER} DIRECTORY)

    add_custom_command(
        OUTPUT ${SPIRV_SHADER}
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
        COMMAND ${CMAKE_COMMAND} -E make_directory ${RELATIVE_SPIRV_SHADER_DIRECTORY}
        COMMAND ${Vulkan_GLSLC_EXECUTABLE}
            --target-env=vulkan1.3 $<IF:$<CONFIG:Debug>,-O0,-O> $<$<CONFIG:Debug>:-g>
            ${RELATIVE_SHADER} -o ${RELATIVE_SPIRV_SHADER}
        MAIN_DEPENDENCY ${SHADER}
    )
    list(APPEND SPIRV_SHADERS ${SPIRV_SHADER})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_SHADERS})

add_dependencies(VulkanPlayground Shaders)

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY VS_STARTUP_PROJECT VulkanPlayground)
