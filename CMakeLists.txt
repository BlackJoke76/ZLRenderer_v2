cmake_minimum_required(VERSION 3.24)
project(ZLRenderer VERSION 0.1.0 LANGUAGES C CXX)

include(FetchContent)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw
    GIT_REPOSITORY https://github.com/glfw/glfw.git
    GIT_TAG a74efa0d5628b74adc0426af4c5710e287fa7c2c # GLFW 3.4
)
FetchContent_MakeAvailable(glfw)

add_executable(zlRenderer src/main.cpp)
target_compile_features(zlRenderer PRIVATE cxx_std_20)
set_target_properties(zlRenderer PROPERTIES CXX_EXTENSIONS OFF)
target_link_libraries(zlRenderer PRIVATE glfw)
if(MSVC)
    target_compile_options(zlRenderer PRIVATE /W4 /WX /utf-8)
else()
    target_compile_options(zlRenderer PRIVATE -Wall -Wextra -Wpedantic -Werror)
endif()
