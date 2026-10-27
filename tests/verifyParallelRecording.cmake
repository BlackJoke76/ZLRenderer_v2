cmake_minimum_required(VERSION 3.24)
set(recordingFailure FALSE)
if(control MATCHES "^(startup|acceptance|group-begin|group-body|group-end|sibling|nonstandard|multiple|gate-timeout)$" OR testCase MATCHES "^(worker-pool|worker-pool-later|worker-primary-later|double-rebuild-failure|submit-later)$")
    set(recordingFailure TRUE)
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "ZL_VULKAN_TEST_CASE=${testCase}"
    "ZL_RECORDING_WORKERS=${workerCount}" "ZL_RECORDING_GROUPS=${groupCount}" "ZL_RECORDING_TEST_CASE=${control}"
    VK_LAYER_VALIDATE_SYNC=1 "${testExecutable}"
    RESULT_VARIABLE exitCode OUTPUT_VARIABLE standardOutput ERROR_VARIABLE standardError TIMEOUT 20)
set(expectedGraphGroups ${groupCount})
include("${CMAKE_CURRENT_LIST_DIR}/verifyPresentation.cmake")
include("${CMAKE_CURRENT_LIST_DIR}/checkParallelEvidence.cmake")
