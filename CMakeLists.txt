cmake_minimum_required(VERSION 3.24)
project(ZLRenderer VERSION 0.1.0 LANGUAGES C CXX)

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

option(ZL_BUILD_RENDERER "Build the GLFW/Vulkan renderer and graphics tests" ON)
include(CTest)
find_package(Threads REQUIRED)
add_library(taskSystem STATIC src/taskSystem.cpp)
configureCppTarget(taskSystem)
target_compile_features(taskSystem PUBLIC cxx_std_20)
target_include_directories(taskSystem PUBLIC src)
target_link_libraries(taskSystem PUBLIC Threads::Threads)
if(BUILD_TESTING)
    add_executable(taskSystemTests tests/taskSystemTests.cpp)
    configureCppTarget(taskSystemTests)
    target_link_libraries(taskSystemTests PRIVATE taskSystem)
    foreach(workerCount 0 1 4)
        foreach(taskCase basic waiting batches lifetime failures shutdown stress)
            add_test(NAME task-${taskCase}-${workerCount} COMMAND taskSystemTests ${taskCase} ${workerCount})
            set_tests_properties(task-${taskCase}-${workerCount} PROPERTIES TIMEOUT 60)
        endforeach()
    endforeach()
    # Same implementation, with host-control checkpoints excluded from the normal library.
    add_executable(taskSystemFailureTests tests/taskSystemFailureTests.cpp src/taskSystem.cpp)
    configureCppTarget(taskSystemFailureTests)
    target_include_directories(taskSystemFailureTests PRIVATE tests)
    target_compile_definitions(taskSystemFailureTests PRIVATE ZL_TASK_SYSTEM_TESTING)
    target_link_libraries(taskSystemFailureTests PRIVATE Threads::Threads)
    add_test(NAME task-startup-failure COMMAND taskSystemFailureTests startup 4)
    set_tests_properties(task-startup-failure PROPERTIES TIMEOUT 15)
    foreach(workerCount 0 1 4)
        add_test(NAME task-acceptance-failure-${workerCount} COMMAND taskSystemFailureTests acceptance ${workerCount})
        set_tests_properties(task-acceptance-failure-${workerCount} PROPERTIES TIMEOUT 15)
    endforeach()
    add_test(NAME task-concurrent COMMAND taskSystemTests concurrent 4)
    set_tests_properties(task-concurrent PROPERTIES TIMEOUT 60)
endif()

if(ZL_BUILD_RENDERER)
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

set(rendererSources src/main.cpp src/rhi.cpp src/renderGraph.cpp src/vulkanRenderGraph.cpp src/vulkanPresenter.cpp src/vulkanContext.cpp src/queueSelection.cpp src/swapchainSelection.cpp)
add_executable(zlRenderer ${rendererSources})
configureCppTarget(zlRenderer)
target_link_libraries(zlRenderer PRIVATE glfw Vulkan::Vulkan taskSystem)


if(BUILD_TESTING)
    add_executable(renderGraphTests tests/renderGraphTests.cpp src/renderGraph.cpp)
    configureCppTarget(renderGraphTests)
    add_test(NAME graph-primary-evidence COMMAND ${CMAKE_COMMAND} "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>" "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>/primary-rejections" -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/checkGraphRejects.cmake")
    foreach(testCase declarations references shapes exits raw war waw readers contents)
        add_test(NAME graph-${testCase} COMMAND renderGraphTests ${testCase})
    endforeach()
    add_executable(vulkanRenderGraphTests tests/vulkanRenderGraphTests.cpp src/renderGraph.cpp src/vulkanRenderGraph.cpp)
    configureCppTarget(vulkanRenderGraphTests)
    target_link_libraries(vulkanRenderGraphTests PRIVATE Vulkan::Vulkan)
    foreach(testCase mapping bindings recording)
        add_test(NAME graph-${testCase} COMMAND vulkanRenderGraphTests ${testCase})
    endforeach()
    add_executable(queueSelectionTests tests/queueSelectionTests.cpp src/queueSelection.cpp)
    configureCppTarget(queueSelectionTests)
    add_test(NAME queue-selection COMMAND queueSelectionTests)

    add_executable(swapchainSelectionTests tests/swapchainSelectionTests.cpp src/swapchainSelection.cpp src/queueSelection.cpp)
    configureCppTarget(swapchainSelectionTests)
    target_link_libraries(swapchainSelectionTests PRIVATE Vulkan::Vulkan)
    add_test(NAME swapchain-selection COMMAND swapchainSelectionTests)

    # Fault injection is compiled only into this separate test executable.
    add_executable(vulkanLifecycleTests ${rendererSources} tests/vulkanTestHooks.cpp tests/recordingTestHooks.cpp src/taskSystem.cpp)
    configureCppTarget(vulkanLifecycleTests)
    target_include_directories(vulkanLifecycleTests PRIVATE tests)
    target_compile_definitions(vulkanLifecycleTests PRIVATE ZL_RENDERER_TESTING ZL_TASK_SYSTEM_TESTING)
    target_link_libraries(vulkanLifecycleTests PRIVATE glfw Vulkan::Vulkan Threads::Threads)
    foreach(testCase missing-layer missing-extension instance messenger surface no-device missing-swapchain missing-surface-usage create-device device diagnostic)
        add_test(NAME vulkan-${testCase} COMMAND ${CMAKE_COMMAND}
            "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>"
            "-DtestCase=${testCase}"
            "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verifyVulkanLifecycle.cmake")
        set_tests_properties(vulkan-${testCase} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endforeach()
    foreach(testCase normal fifo alternate invalid-color nonfinite-color frame-resources swapchain image-view framebuffer
            fence-timeout acquire-timeout acquire-out-of-date acquire-suboptimal present-out-of-date present-suboptimal
            record submit rebuild-failure surface-lost present-failure wait-failure
            old-chain rebuild-create rebuild-render-pass rebuild-image-view rebuild-framebuffer rebuild-present-semaphore
            prepare-resize prepare-close prepare-zero prepare-iconified initial-zero initial-iconified initial-zero-close
            surface-zero surface-variable-zero surface-width-zero
            primary-allocation submit-later double-rebuild-failure acquire-not-ready group-begin group-body group-end reset-fence double-rebuild graph-double graph-binding graph-stale graph-missing graph-group graph-unsupported)
        add_test(NAME presentation-${testCase} COMMAND ${CMAKE_COMMAND}
            "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>"
            "-DtestCase=${testCase}"
            "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verifyPresentation.cmake")
        set_tests_properties(presentation-${testCase} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endforeach()
    foreach(workerCount 0 1 4)
        foreach(groupCount 1 2 5)
            add_test(NAME recording-${workerCount}-${groupCount} COMMAND ${CMAKE_COMMAND}
                "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>" -DtestCase=normal
                "-DworkerCount=${workerCount}" "-DgroupCount=${groupCount}" -Dcontrol=none
                "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>/recording-${workerCount}-${groupCount}"
                -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verifyParallelRecording.cmake")
            set_tests_properties(recording-${workerCount}-${groupCount} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
        endforeach()
    endforeach()
    foreach(control overlap reverse startup acceptance group-begin group-body group-end sibling nonstandard multiple gate-timeout)
        add_test(NAME recording-${control} COMMAND ${CMAKE_COMMAND}
            "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>" -DtestCase=normal
            -DworkerCount=4 -DgroupCount=2 "-Dcontrol=${control}"
            "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>/recording-${control}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verifyParallelRecording.cmake")
        set_tests_properties(recording-${control} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endforeach()
    foreach(testCase worker-pool worker-pool-later worker-primary-later double-rebuild double-rebuild-failure submit-later acquire-suboptimal acquire-timeout acquire-not-ready acquire-out-of-date present-suboptimal)
        add_test(NAME recording-${testCase} COMMAND ${CMAKE_COMMAND}
            "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>" "-DtestCase=${testCase}"
            -DworkerCount=4 -DgroupCount=2 -Dcontrol=none
            "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>/recording-${testCase}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/verifyParallelRecording.cmake")
        set_tests_properties(recording-${testCase} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    endforeach()
    add_test(NAME recording-reject-evidence COMMAND ${CMAKE_COMMAND}
        "-DtestExecutable=$<TARGET_FILE:vulkanLifecycleTests>"
        "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>/recording-reject-evidence"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/checkRecordingRejects.cmake")
    set_tests_properties(recording-reject-evidence PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
    add_test(NAME recording-compare-modes COMMAND ${CMAKE_COMMAND}
        "-DlogDirectory=${CMAKE_BINARY_DIR}/test-logs/$<CONFIG>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/checkRecordingModes.cmake")
    set_tests_properties(recording-compare-modes PROPERTIES DEPENDS "recording-0-1;recording-0-2;recording-0-5;recording-1-1;recording-1-2;recording-1-5;recording-4-1;recording-4-2;recording-4-5")
endif()
endif()
