#pragma once
#include <cstddef>

// Compiled only into the separate host-control test target.
void beforeTaskWorkerCreation(std::size_t workerIndex);
void beforeTaskBatchAcceptance();
void taskWorkerStarted();
void taskWorkerStopped();
