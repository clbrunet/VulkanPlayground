add_library(SimplexNoise STATIC src/SimplexNoise.cpp)

target_include_directories(SimplexNoise PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
