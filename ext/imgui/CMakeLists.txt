set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/imgui)

file(GLOB SRCS ${IMGUI_DIR}/*.cpp ${IMGUI_DIR}/misc/cpp/*.cpp)
list(APPEND SRCS
    ${IMGUI_DIR}/backends/imgui_impl_glfw.cpp
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.cpp
)
file(GLOB HEADERS ${IMGUI_DIR}/*.h ${IMGUI_DIR}/misc/cpp/*.h)
list(APPEND HEADERS
    ${IMGUI_DIR}/backends/imgui_impl_glfw.h
    ${IMGUI_DIR}/backends/imgui_impl_vulkan.h
)

add_library(imgui STATIC ${SRCS} ${HEADERS})

target_compile_definitions(imgui PUBLIC
    GLFW_INCLUDE_NONE
    GLFW_INCLUDE_VULKAN
)

target_include_directories(imgui PUBLIC
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC glfw Vulkan::Vulkan)
