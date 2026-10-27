#include "recordingTestHooks.h"
#include "vulkanTestHooks.h"
#include "taskSystemTestHooks.h"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
std::string readControl(const char* name) {
#ifdef _MSC_VER
    char value[128]{};
    std::size_t requiredSize = 0;
    if (getenv_s(&requiredSize, value, sizeof(value), name) != 0) { throw std::runtime_error("invalid recording control"); }
    return value;
#else
    const auto value = std::getenv(name);
    return value ? value : "";
#endif
}
// Coordinator initializes these before starting workers; batch state uses gateMutex.
std::string recordingCase;
std::string lifecycleCase;
std::size_t requestedGroups = 0;
std::atomic<int> liveWorkers = 0;
std::mutex gateMutex;
std::condition_variable gateChanged;
std::size_t activeCount = 0;
std::size_t maximumActive = 0;
std::size_t arrivals = 0;
std::size_t batchGroups = 0;
std::size_t batchWorkers = 0;
bool secondFinished = false;
bool firstFinished = false;
bool failedGroupExited = false;
bool timedOut = false;
bool controlActive = false;
std::vector<std::size_t> completionOrder;
struct RecordingEvent {
    std::size_t groupIndex;
    bool entering;
    std::size_t activeCount;
};
std::vector<RecordingEvent> recordingEvents;
template<class Predicate> void waitForGate(std::unique_lock<std::mutex>& lock, Predicate predicate) {
    if (!gateChanged.wait_for(lock, std::chrono::seconds(3), [&] { return timedOut || predicate(); })) {
        timedOut = true;
        gateChanged.notify_all();
    }
    if (timedOut) { throw std::runtime_error("recording gate timeout"); }
}
}

void configureRecordingTest(RhiConfig& config) {
    recordingCase = readControl("ZL_RECORDING_TEST_CASE");
    lifecycleCase = readControl("ZL_VULKAN_TEST_CASE");
    const auto workers = readControl("ZL_RECORDING_WORKERS");
    const auto groups = readControl("ZL_RECORDING_GROUPS");
    if (!workers.empty()) { config.recordingWorkerCount = std::stoull(workers); }
    if (!groups.empty()) { requestedGroups = std::stoull(groups); }
}
std::size_t getRecordingTestGroupCount() { return requestedGroups; }
void beforeTaskWorkerCreation(std::size_t workerIndex) {
    if (recordingCase == "startup" && workerIndex == 1) { throw std::runtime_error("recording startup failure"); }
}
void beforeTaskBatchAcceptance() {
    if (controlActive && recordingCase == "acceptance") { throw std::runtime_error("recording acceptance failure"); }
}
void taskWorkerStarted() { ++liveWorkers; }
void taskWorkerStopped() { --liveWorkers; }
void beginRecordingTestBatch(std::uint64_t submittedFrames, std::size_t groupCount, std::size_t workerCount) {
    std::lock_guard lock(gateMutex);
    controlActive = submittedFrames == 3;
    batchGroups = groupCount;
    batchWorkers = workerCount;
    activeCount = maximumActive = arrivals = 0;
    firstFinished = secondFinished = failedGroupExited = timedOut = false;
    completionOrder.clear();
    completionOrder.reserve(groupCount);
    recordingEvents.clear();
    recordingEvents.reserve(groupCount * 2);
}
void finishRecordingTestBatch() {
    std::lock_guard lock(gateMutex);
    if (activeCount != 0) { throw std::runtime_error("recording batch returned with live callbacks"); }
    if (controlActive && !recordingCase.empty()) {
        std::printf("Recording batch drained active=0 maximum=%zu timeout=%d order=", maximumActive, timedOut ? 1 : 0);
        for (const auto groupIndex : completionOrder) { std::printf("%zu,", groupIndex); }
        std::puts("");
        for (std::size_t eventIndex = 0; eventIndex < recordingEvents.size(); ++eventIndex) {
            const auto& event = recordingEvents[eventIndex];
            std::printf("Recording event sequence=%zu group=%zu action=%s active=%zu\n", eventIndex,
                        event.groupIndex, event.entering ? "enter" : "exit", event.activeCount);
        }
    }
}
RecordingTestScope::RecordingTestScope(std::size_t requestedGroupIndex) : groupIndex(requestedGroupIndex) {
    std::unique_lock lock(gateMutex);
    // Register arrival while still inside a begun primary. RAII starts after the throwing gate wait.
    if (controlActive && (recordingCase == "overlap" || recordingCase == "reverse" ||
        recordingCase == "sibling" || recordingCase == "nonstandard" || recordingCase == "multiple" || recordingCase == "gate-timeout")) {
        ++arrivals;
        gateChanged.notify_all();
        waitForGate(lock, [&] { return arrivals >= (recordingCase == "gate-timeout" ? batchGroups + 1 : std::min(batchGroups, batchWorkers)); });
    }
    ++activeCount;
    maximumActive = std::max(maximumActive, activeCount);
    recordingEvents.push_back({groupIndex, true, activeCount});
    // Hold registered scopes until all participating workers have entered.
    if (controlActive && recordingCase == "overlap") {
        gateChanged.notify_all();
        // No throw after registering: constructor failure must undo its registration.
        try { waitForGate(lock, [&] { return maximumActive >= std::min(batchGroups, batchWorkers); }); }
        catch (...) {
            --activeCount;
            recordingEvents.push_back({groupIndex, false, activeCount});
            throw;
        }
    }
}
RecordingTestScope::~RecordingTestScope() noexcept {
    std::lock_guard lock(gateMutex);
    --activeCount;
    recordingEvents.push_back({groupIndex, false, activeCount});
    if (!completed) { failedGroupExited = true; }
    gateChanged.notify_all();
}
void RecordingTestScope::complete() {
    std::lock_guard lock(gateMutex);
    completionOrder.push_back(groupIndex);
    if (groupIndex == 0) { firstFinished = true; }
    if (groupIndex == 1) { secondFinished = true; }
    completed = true;
    gateChanged.notify_all();
}
void beforeRecordingTestEnd(std::size_t groupIndex) {
    if (!controlActive) { return; }
    std::unique_lock lock(gateMutex);
    if (recordingCase == "reverse" && groupIndex == 0) { waitForGate(lock, [] { return secondFinished; }); }
    if ((recordingCase == "sibling" || recordingCase == "nonstandard") && groupIndex == 0) {
        waitForGate(lock, [] { return failedGroupExited; });
    }
}
void runRecordingGroupCheckpoint(const char* stage, std::size_t groupIndex) {
    const std::string requestedStage(stage);
    const auto legacyMatch = (groupIndex == 1 && lifecycleCase == requestedStage) ||
        (groupIndex == 0 && lifecycleCase == "graph-group" && requestedStage == "graph-group");
    const auto lateMatch = controlActive && groupIndex == 1 && recordingCase == requestedStage;
    if (lateMatch) {
        std::unique_lock lock(gateMutex);
        waitForGate(lock, [] { return firstFinished; });
    }
    if (legacyMatch || lateMatch) { throw std::runtime_error(requestedStage + ": injected recording failure"); }
    if (controlActive && requestedStage == "group-body") {
        if (groupIndex == 1 && recordingCase == "nonstandard") { throw 7; }
        if ((groupIndex == 1 && recordingCase == "sibling") || recordingCase == "multiple") {
            throw std::runtime_error("recording sibling failure");
        }
    }
}
void verifyRecordingWorkersStopped() {
    std::printf("Recording live workers=%d\n", liveWorkers.load());
    if (liveWorkers != 0) { throw std::runtime_error("recording workers leaked"); }
}
