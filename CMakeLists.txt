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
find_package(Vulkan REQUIRED)

function(configureCppTarget targetName)
    target_compile_features(${targetName} PRIVATE cxx_std_20)
    set_target_properties(${targetName} PROPERTIES CXX_EXTENSIONS OFF)
    target_include_directories(${targetName} PRIVATE src)
    if(MSVC)
        target_compile_options(${targetName} PRIVATE /W4 /WX /utf-8)
    else()
        target_compile_options(${targetName} PRIVATE -Wall -Wextra -Wpedantic -Werror)
    endif()
endfunction()

set(rendererSources src/main.cpp src/vulkanContext.cpp src/queueSelection.cpp)
add_executable(zlRenderer ${rendererSources})
configureCppTarget(zlRenderer)
target_link_libraries(zlRenderer PRIVATE glfw Vulkan::Vulkan)

include(CTest)
if(BUILD_TESTING)
    add_executable(queueSelectionTests tests/queueSelectionTests.cpp src/queueSelection.cpp)
    configureCppTarget(queueSelectionTests)
    add_test(NAME queue-selection COMMAND queueSelectionTests)

    # Fault injection is compiled only into this separate test executable.
    add_executable(vulkanLifecycleTests ${rendererSources} tests/vulkanTestHooks.cpp)
    configureCppTarget(vulkanLifecycleTests)
    target_include_directories(vulkanLifecycleTests PRIVATE tests)
    target_compile_definitions(vulkanLifecycleTests PRIVATE ZL_RENDERER_TESTING)
    target_link_libraries(vulkanLifecycleTests PRIVATE glfw Vulkan::Vulkan)
    foreach(testCase missing-layer missing-extension instance messenger surface no-device create-device device diagnostic)
        add_test(NAME vulkan-${testCase} COMMAND ${CMAKE_COMMAND}
            "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>"
            "-DtestCase=${testCase}"
            "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verifyVulkanLifecycle.cmake")
        set_tests_properties(vulkan-${testCase} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endforeach()
endif()
