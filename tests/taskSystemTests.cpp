#include "taskSystem.h"
#include <array>
#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>

namespace {
using namespace std::chrono_literals;

class TaskGate {
public:
    void arriveAndWait() {
        std::unique_lock lock(mutex);
        ++arrivals;
        changed.notify_all();
        changed.wait(lock, [&] { return released; });
    }
    bool waitForArrivals(std::size_t count) {
        std::unique_lock lock(mutex);
        return changed.wait_for(lock, 3s, [&] { return arrivals >= count; });
    }
    void release() {
        { std::lock_guard lock(mutex); released = true; }
        changed.notify_all();
    }
private:
    std::mutex mutex;
    std::condition_variable changed;
    std::size_t arrivals = 0;
    bool released = false;
};

// Construct after the executor and any joining helper threads. On assertion
// failure, open the gate before those objects drain/join; the gate itself lives longer.
struct ReleaseTaskGate {
    TaskGate& gate;
    ~ReleaseTaskGate() { gate.release(); }
};

void require(bool condition, const char* message) {
    if (!condition) { throw std::runtime_error(message); }
}
template<class Operation>
void expectFailure(Operation operation, const char* messagePart) {
    try { operation(); }
    catch (const std::exception& error) {
        require(std::string(error.what()).find(messagePart) != std::string::npos, error.what());
        return;
    }
    throw std::runtime_error("Expected task API rejection did not occur.");
}

void basic(std::size_t workerCount) {
    const auto coordinator = std::this_thread::get_id();
    TaskSystem executor(workerCount);
    const auto empty = executor.submitBatch({});
    require(empty.wait().taskExceptions.empty() && empty.wait().failedTaskCount == 0, "Empty batch result");
    std::vector<std::size_t> inlineOrder;
    std::array<std::atomic<unsigned>, 64> executions{};
    std::array<std::thread::id, 64> threadIds{};
    std::array<std::size_t, 64> workerIndices{};
    std::vector<TaskSystem::Task> tasks;
    for (std::size_t taskIndex = 0; taskIndex < executions.size(); ++taskIndex) {
        tasks.emplace_back([&, taskIndex](std::size_t workerIndex) {
            ++executions[taskIndex];
            threadIds[taskIndex] = std::this_thread::get_id();
            workerIndices[taskIndex] = workerIndex;
            if (workerCount == 0) { inlineOrder.push_back(taskIndex); }
        });
    }
    auto handle = executor.submitBatch(std::move(tasks));
    if (workerCount == 0) { require(inlineOrder.size() == executions.size(), "Inline submit must finish before returning"); }
    const auto& result = handle.wait();
    require(result.taskExceptions.size() == executions.size() && result.failedTaskCount == 0, "Successful batch result");
    require(&result == &handle.wait(), "Repeated wait changed result identity");
    std::map<std::thread::id, std::size_t> workerIdentity;
    for (std::size_t taskIndex = 0; taskIndex < executions.size(); ++taskIndex) {
        require(executions[taskIndex] == 1 && !result.taskExceptions[taskIndex], "Task must run exactly once");
        require(workerIndices[taskIndex] < (workerCount == 0 ? 1 : workerCount), "Worker index outside configured range");
        if (workerCount == 0) {
            require(threadIds[taskIndex] == coordinator && inlineOrder[taskIndex] == taskIndex, "Inline thread/order mismatch");
        } else { require(threadIds[taskIndex] != coordinator, "Coordinator executed a worker task"); }
        const auto [entry, inserted] = workerIdentity.emplace(threadIds[taskIndex], workerIndices[taskIndex]);
        require(inserted || entry->second == workerIndices[taskIndex], "Thread changed worker index");
    }
    if (workerCount == 1) { require(workerIdentity.size() == 1, "Single worker used multiple threads"); }
    unsigned singleExecutions = 0;
    executor.submitBatch({[&](std::size_t) { ++singleExecutions; }}).wait();
    require(singleExecutions == 1, "Single task was not completed");
    unsigned rejectedExecutions = 0;
    expectFailure([&] { executor.submitBatch({[&](std::size_t) { ++rejectedExecutions; }, {}}); }, "empty callback");
    require(rejectedExecutions == 0, "Invalid batch was partially accepted");
    expectFailure([] { TaskBatchHandle{}.wait(); }, "invalid handle");
    auto movedHandle = std::move(handle);
    expectFailure([&] { handle.wait(); }, "invalid handle");
    require(&movedHandle.wait() == &result, "Moved handle lost result");
    executor.shutdown();
    executor.shutdown();
    expectFailure([&] { executor.submitBatch({}); }, "closed");
    expectFailure([&] { executor.submitBatch({[](std::size_t) {}}); }, "closed");
}

void concurrent(std::size_t workerCount) {
    if (workerCount < 2) { throw std::runtime_error("Concurrency test needs two workers"); }
    TaskGate gate;
    std::vector<std::thread::id> threadIds(workerCount);
    std::vector<std::size_t> workerIndices(workerCount);
    TaskSystem executor(workerCount);
    ReleaseTaskGate release{gate};
    std::vector<TaskSystem::Task> tasks;
    for (std::size_t taskIndex = 0; taskIndex < workerCount; ++taskIndex) {
        tasks.emplace_back([&, taskIndex](std::size_t workerIndex) {
            threadIds[taskIndex] = std::this_thread::get_id();
            workerIndices[taskIndex] = workerIndex;
            gate.arriveAndWait();
        });
    }
    auto batch = executor.submitBatch(std::move(tasks));
    require(gate.waitForArrivals(workerCount), "Configured workers never overlapped");
    std::map<std::thread::id, std::size_t> identities;
    std::vector<bool> seenIndices(workerCount);
    for (std::size_t taskIndex = 0; taskIndex < workerCount; ++taskIndex) {
        require(workerIndices[taskIndex] < workerCount, "Invalid concurrent worker index");
        require(identities.emplace(threadIds[taskIndex], workerIndices[taskIndex]).second && !seenIndices[workerIndices[taskIndex]],
                "Worker identities are not exclusive");
        seenIndices[workerIndices[taskIndex]] = true;
    }
    gate.release();
    require(batch.wait().failedTaskCount == 0, "Concurrent task failed");
    std::vector<TaskSystem::Task> repeatedTasks(128, [&](std::size_t workerIndex) {
        require(identities.at(std::this_thread::get_id()) == workerIndex, "Worker changed identity between batches");
    });
    require(executor.submitBatch(std::move(repeatedTasks)).wait().failedTaskCount == 0, "Worker identity was not stable");
}

void waiting(std::size_t workerCount) {
    if (workerCount == 0) { basic(0); return; } // Inline execution cannot be externally released after submit returns.
    TaskGate gate;
    std::array<std::promise<void>, 2> waitingStarted;
    std::array<std::promise<const TaskBatchResult*>, 2> waitingFinished;
    std::array<std::future<const TaskBatchResult*>, 2> results{
        waitingFinished[0].get_future(), waitingFinished[1].get_future()};
    int output = 0;
    auto capturedOwner = std::make_shared<int>(21);
    const std::weak_ptr<int> capturedWeak = capturedOwner;
    TaskSystem executor(workerCount);
    std::vector<std::jthread> waiters;
    ReleaseTaskGate release{gate};
    auto batch = executor.submitBatch({[](std::size_t) { throw std::runtime_error("failure before wait"); },
        [&, owner = std::move(capturedOwner)](std::size_t) {
        gate.arriveAndWait();
        output = *owner * 2;
    }});
    require(gate.waitForArrivals(1), "Worker did not enter waiting test");
    for (std::size_t waiterIndex = 0; waiterIndex < 2; ++waiterIndex) {
        waiters.emplace_back([&, waiterIndex, handle = batch] {
            waitingStarted[waiterIndex].set_value();
            waitingFinished[waiterIndex].set_value(&handle.wait());
        });
        waitingStarted[waiterIndex].get_future().wait();
        require(results[waiterIndex].wait_for(2ms) == std::future_status::timeout, "Wait returned before callback ended");
    }
    gate.release();
    const auto* firstResult = results[0].get();
    require(results[1].get() == firstResult && firstResult == &batch.wait(), "Waiters observed different results");
    require(firstResult->failedTaskCount == 1 && firstResult->taskExceptions[0] && !firstResult->taskExceptions[1],
            "Wait did not retain the earlier failure");
    require(output == 42 && capturedWeak.expired(), "Completion did not publish output and release captures");
}

void batches(std::size_t workerCount) {
    TaskGate gate;
    std::atomic<unsigned> executions = 0;
    std::promise<void> independentFinished;
    auto independentResult = independentFinished.get_future();
    TaskSystem executor(workerCount);
    std::vector<std::jthread> waiters;
    ReleaseTaskGate release{gate};
    std::vector<TaskBatchHandle> handles;
    for (unsigned batchIndex = 0; batchIndex < 8; ++batchIndex) {
        std::vector<TaskSystem::Task> tasks;
        for (unsigned taskIndex = 0; taskIndex < 17; ++taskIndex) {
            tasks.emplace_back([&](std::size_t) { ++executions; });
        }
        handles.push_back(executor.submitBatch(std::move(tasks)));
    }
    for (const auto& handle : handles) { require(handle.wait().taskExceptions.size() == 17, "Batch result mixed with another batch"); }
    require(executions == 8 * 17, "Multiple batches lost or duplicated tasks");
    if (workerCount >= 2) {
        auto blocked = executor.submitBatch({[&](std::size_t) { gate.arriveAndWait(); }});
        require(gate.waitForArrivals(1), "Independent batch did not enter");
        const auto independent = executor.submitBatch({[](std::size_t) {}});
        waiters.emplace_back([&, independent] { independent.wait(); independentFinished.set_value(); });
        const auto independentReady = independentResult.wait_for(3s) == std::future_status::ready;
        gate.release();
        require(independentReady, "Batch wait incorrectly included unrelated blocked work");
        require(independent.wait().failedTaskCount == 0, "Independent batch failed");
        blocked.wait();
    }
}

void lifetime(std::size_t workerCount) {
    TaskGate gate;
    std::atomic<unsigned> executions = 0;
    TaskBatchHandle retained;
    std::weak_ptr<int> droppedCapture;
    {
        TaskSystem executor(workerCount);
        ReleaseTaskGate release{gate};
        if (workerCount != 0) {
            executor.submitBatch(std::vector<TaskSystem::Task>(workerCount, [&](std::size_t) { gate.arriveAndWait(); }));
            require(gate.waitForArrivals(workerCount), "Drop test did not occupy workers");
        }
        auto capturedOwner = std::make_shared<int>(7);
        droppedCapture = capturedOwner;
        // Dropping the returned handle must not drop accepted callbacks.
        executor.submitBatch({[&, owner = std::move(capturedOwner)](std::size_t) { executions += static_cast<unsigned>(*owner); }});
        retained = executor.submitBatch({[&](std::size_t) { ++executions; },
            [](std::size_t) { throw std::runtime_error("destructor drain failure"); }});
        if (workerCount != 0) { require(!droppedCapture.expired(), "Dropping a handle cancelled queued callbacks"); }
        gate.release();
    }
    require(executions == 8 && droppedCapture.expired(), "Destructor lost tasks or retained captures");
    require(retained.wait().failedTaskCount == 1 && retained.wait().taskExceptions.size() == 2,
            "Handle did not survive executor destruction");
}

void failures(std::size_t workerCount) {
    TaskGate gate;
    std::promise<void> laterTaskEnded;
    auto laterEnded = laterTaskEnded.get_future();
    std::array<std::atomic<unsigned>, 6> executions{};
    std::array<unsigned, 6> endOrder{};
    std::atomic<unsigned> endSequence = 0;
    TaskSystem executor(workerCount);
    ReleaseTaskGate release{gate};
    std::vector<TaskSystem::Task> tasks;
    for (std::size_t taskIndex = 0; taskIndex < executions.size(); ++taskIndex) {
        tasks.emplace_back([&, taskIndex](std::size_t) {
            ++executions[taskIndex];
            if (taskIndex == 1 && workerCount >= 2) { gate.arriveAndWait(); }
            endOrder[taskIndex] = ++endSequence;
            if (taskIndex == 1) { throw std::runtime_error("first task error"); }
            if (taskIndex == 3) {
                laterTaskEnded.set_value();
                throw 37; // Non-standard exception must survive aggregation.
            }
        });
    }
    auto batch = executor.submitBatch(std::move(tasks));
    require(laterEnded.wait_for(3s) == std::future_status::ready, "Later throwing task never ran");
    gate.release();
    const auto& result = batch.wait();
    require(result.taskExceptions.size() == 6 && result.failedTaskCount == 2, "Failure summary count mismatch");
    for (std::size_t taskIndex = 0; taskIndex < executions.size(); ++taskIndex) {
        require(executions[taskIndex] == 1, "Failure abandoned or repeated another task");
        require(static_cast<bool>(result.taskExceptions[taskIndex]) == (taskIndex == 1 || taskIndex == 3), "Wrong failure slot");
    }
    if (workerCount >= 2) { require(endOrder[3] < endOrder[1], "Failure test did not finish out of input order"); }
    expectFailure([&] { std::rethrow_exception(result.taskExceptions[1]); }, "first task error");
    try { std::rethrow_exception(result.taskExceptions[3]); }
    catch (int errorCode) { require(errorCode == 37, "Non-standard exception payload lost"); }
    const auto next = executor.submitBatch({[](std::size_t) {}});
    require(next.wait().failedTaskCount == 0 && batch.wait().failedTaskCount == 2, "A failed batch poisoned later work");
}

void shutdown(std::size_t workerCount) {
    {
        TaskSystem idle(workerCount);
        idle.shutdown();
        idle.shutdown();
        expectFailure([&] { idle.submitBatch({}); }, "closed");
    }
    if (workerCount == 0) { basic(0); failures(0); lifetime(0); return; }
    TaskGate gate;
    std::atomic<unsigned> executions = 0;
    std::promise<void> shutdownEntered;
    std::promise<void> shutdownFinished;
    auto entered = shutdownEntered.get_future();
    auto finished = shutdownFinished.get_future();
    std::exception_ptr coordinatorFailure;
    std::jthread coordinator([&] {
        try {
            TaskSystem executor(workerCount);
            ReleaseTaskGate release{gate};
            std::vector<TaskSystem::Task> tasks;
            for (std::size_t taskIndex = 0; taskIndex < workerCount + 32; ++taskIndex) {
                tasks.emplace_back([&, taskIndex](std::size_t) {
                    if (taskIndex < workerCount) { gate.arriveAndWait(); }
                    ++executions;
                    if (taskIndex == workerCount + 5) { throw std::runtime_error("draining failure"); }
                });
            }
            const auto batch = executor.submitBatch(std::move(tasks));
            const auto queued = executor.submitBatch(std::vector<TaskSystem::Task>(3, [&](std::size_t) { ++executions; }));
            require(gate.waitForArrivals(workerCount), "Shutdown test workers did not enter");
            shutdownEntered.set_value();
            executor.shutdown();
            shutdownFinished.set_value();
            require(batch.wait().failedTaskCount == 1 && queued.wait().failedTaskCount == 0, "Drain lost failure or queued batch");
            require(executions == workerCount + 35, "Shutdown did not drain every task");
            executor.shutdown();
            expectFailure([&] { executor.submitBatch({}); }, "closed");
        } catch (...) { coordinatorFailure = std::current_exception(); }
    });
    ReleaseTaskGate release{gate};
    require(entered.wait_for(3s) == std::future_status::ready, "Shutdown coordinator never reached close");
    require(finished.wait_for(2ms) == std::future_status::timeout, "Shutdown returned while callbacks were gated");
    gate.release();
    coordinator.join();
    if (coordinatorFailure) { std::rethrow_exception(coordinatorFailure); }
    require(finished.wait_for(0s) == std::future_status::ready, "Shutdown did not finish");
}
}

int main(int argumentCount, char** arguments) {
    try {
        if (argumentCount != 3) { throw std::runtime_error("Expected test case and worker count"); }
        const std::string testCase = arguments[1];
        const auto workerCount = static_cast<std::size_t>(std::stoul(arguments[2]));
        if (testCase == "basic") { basic(workerCount); }
        else if (testCase == "concurrent") { concurrent(workerCount); }
        else if (testCase == "waiting") { waiting(workerCount); }
        else if (testCase == "batches") { batches(workerCount); }
        else if (testCase == "lifetime") { lifetime(workerCount); }
        else if (testCase == "failures") { failures(workerCount); }
        else if (testCase == "shutdown") { shutdown(workerCount); }
        else if (testCase == "stress") {
            for (unsigned iteration = 0; iteration < 100; ++iteration) {
                waiting(workerCount);
                batches(workerCount);
                lifetime(workerCount);
                failures(workerCount);
                shutdown(workerCount);
            }
            std::cout << "Completed 100 rounds of wait/batches/lifetime/failure/shutdown\n";
        }
        else { throw std::runtime_error("Unknown task case"); }
        std::cout << "PASS task " << testCase << " workers=" << workerCount << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
