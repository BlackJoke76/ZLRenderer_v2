#pragma once
#include "rhi.h"
#include <cstddef>
#include <cstdint>
void configureRecordingTest(RhiConfig& config);
std::size_t getRecordingTestGroupCount();
void beginRecordingTestBatch(std::uint64_t submittedFrames, std::size_t groupCount, std::size_t workerCount);
void finishRecordingTestBatch();
void runRecordingGroupCheckpoint(const char* stage, std::size_t groupIndex);
void beforeRecordingTestEnd(std::size_t groupIndex);
void verifyRecordingWorkersStopped();
class RecordingTestScope {
public:
    explicit RecordingTestScope(std::size_t groupIndex);
    ~RecordingTestScope() noexcept;
    void complete();
private:
    std::size_t groupIndex;
    bool completed = false;
};
