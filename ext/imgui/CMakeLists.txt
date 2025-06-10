file(GLOB SRCS imgui/*.cpp misc/cpp/*.cpp)
list(APPEND SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.cpp
)
file(GLOB HEADERS src/*.h misc/cpp/*.h)
list(APPEND HEADERS
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_glfw.h
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends/imgui_impl_vulkan.h
)

add_library(imgui STATIC ${SRCS} ${HEADERS})

target_compile_definitions(imgui PUBLIC
    GLFW_INCLUDE_NONE
    GLFW_INCLUDE_VULKAN
)

target_include_directories(imgui PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui
    ${CMAKE_CURRENT_SOURCE_DIR}/imgui/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
