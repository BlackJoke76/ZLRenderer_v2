#include "taskSystem.h"
#include "taskSystemTestHooks.h"
#include <atomic>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

namespace {
std::size_t failureWorkerIndex = std::numeric_limits<std::size_t>::max();
bool rejectBatch = false;
std::atomic<std::size_t> startedWorkers = 0;
std::atomic<std::size_t> stoppedWorkers = 0;
void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
}

void beforeTaskWorkerCreation(std::size_t workerIndex) {
    if (workerIndex == failureWorkerIndex) { throw std::runtime_error("controlled worker startup failure"); }
}
void beforeTaskBatchAcceptance() {
    if (rejectBatch) { throw std::runtime_error("controlled batch acceptance failure"); }
}
void taskWorkerStarted() { ++startedWorkers; }
void taskWorkerStopped() { ++stoppedWorkers; }

int main(int argumentCount, char** arguments) {
    try {
        if (argumentCount != 3) { throw std::runtime_error("Expected case and worker count"); }
        const std::string testCase = arguments[1];
        const auto workerCount = static_cast<std::size_t>(std::stoul(arguments[2]));
        if (testCase == "startup") {
            for (const std::size_t failureIndex : {0u, 1u, 3u}) {
                failureWorkerIndex = failureIndex;
                startedWorkers = 0;
                stoppedWorkers = 0;
                bool rejected = false;
                try { TaskSystem executor(workerCount); }
                catch (const std::runtime_error& error) {
                    rejected = std::string(error.what()) == "controlled worker startup failure";
                }
                require(rejected && startedWorkers == failureIndex && stoppedWorkers == failureIndex,
                        "Partial startup did not join every real started worker");
                std::cout << "Controlled failure before worker=" << failureIndex
                          << " real started/joined=" << startedWorkers << '/' << stoppedWorkers << '\n';
            }
        } else if (testCase == "acceptance") {
            std::atomic<unsigned> executions = 0;
            TaskSystem executor(workerCount);
            executor.submitBatch({[&](std::size_t) { ++executions; }}).wait();
            rejectBatch = true;
            bool rejected = false;
            try { executor.submitBatch({[&](std::size_t) { executions += 100; }}); }
            catch (const std::runtime_error& error) {
                rejected = std::string(error.what()) == "controlled batch acceptance failure";
            }
            rejectBatch = false;
            executor.submitBatch({[&](std::size_t) { ++executions; }}).wait();
            executor.shutdown();
            require(rejected && executions == 2, "Failed acceptance executed a partial batch or poisoned the executor");
            require(startedWorkers == workerCount && stoppedWorkers == workerCount, "Shutdown missed real workers");
        } else { throw std::runtime_error("Unknown control test"); }
        std::cout << "PASS task " << testCase << " workers=" << workerCount << " (host control; not natural OS/allocation failure)\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
