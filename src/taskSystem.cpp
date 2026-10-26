#include "taskSystem.h"
#include <stdexcept>
#include <utility>
#ifdef ZL_TASK_SYSTEM_TESTING
#include "taskSystemTestHooks.h"
#endif

struct TaskBatchState {
    explicit TaskBatchState(std::vector<TaskSystem::Task> acceptedTasks)
        : tasks(std::move(acceptedTasks)), result{std::vector<std::exception_ptr>(tasks.size()), 0},
          remainingCount(tasks.size()) {}

    std::vector<TaskSystem::Task> tasks;
    TaskBatchResult result;
    std::size_t nextTaskIndex = 0; // Protected by the executor queue mutex.
    std::mutex completionMutex;
    std::condition_variable completed;
    std::size_t remainingCount; // Protected by completionMutex, together with result.
};

TaskBatchHandle::TaskBatchHandle(std::shared_ptr<TaskBatchState> state) : batchState(std::move(state)) {}

const TaskBatchResult& TaskBatchHandle::wait() const {
    if (!batchState) { throw std::logic_error("task batch wait: invalid handle."); }
    std::unique_lock lock(batchState->completionMutex);
    batchState->completed.wait(lock, [&] { return batchState->remainingCount == 0; });
    return batchState->result;
}

TaskSystem::TaskSystem(std::size_t workerCount) {
    workers.reserve(workerCount);
    try {
        for (std::size_t workerIndex = 0; workerIndex < workerCount; ++workerIndex) {
#ifdef ZL_TASK_SYSTEM_TESTING
            beforeTaskWorkerCreation(workerIndex);
#endif
            workers.emplace_back([this, workerIndex] { workerLoop(workerIndex); });
        }
    } catch (...) {
        shutdown(); // Wake already started workers before jthread members are destroyed.
        throw;
    }
}

TaskSystem::~TaskSystem() noexcept { shutdown(); }

TaskBatchHandle TaskSystem::submitBatch(std::vector<Task> tasks) {
    // Control calls are serialized by the coordinator, including the inline path.
    {
        std::lock_guard lock(queueMutex);
        if (closing) { throw std::logic_error("task batch submit: executor is closed."); }
    }
    for (const auto& taskCallback : tasks) {
        if (!taskCallback) { throw std::invalid_argument("task batch submit: empty callback."); }
    }
    auto batch = std::make_shared<TaskBatchState>(std::move(tasks));
    TaskBatchHandle handle(batch);
#ifdef ZL_TASK_SYSTEM_TESTING
    beforeTaskBatchAcceptance();
#endif
    if (workers.empty()) {
        for (std::size_t taskIndex = 0; taskIndex < batch->tasks.size(); ++taskIndex) {
            executeTask(batch, taskIndex, 0);
        }
    } else if (!batch->tasks.empty()) {
        {
            std::lock_guard lock(queueMutex);
            pendingBatches.push_back(batch); // Single all-or-nothing acceptance point.
        }
        workAvailable.notify_all();
    }
    return handle;
}

void TaskSystem::executeTask(const std::shared_ptr<TaskBatchState>& batch, std::size_t taskIndex,
                             std::size_t workerIndex) {
    auto taskCallback = std::exchange(batch->tasks[taskIndex], Task{});
    std::exception_ptr failure;
    try { taskCallback(workerIndex); }
    catch (...) { failure = std::current_exception(); }
    taskCallback = {}; // Release captures before announcing completion, with no locks held.
    bool finished;
    {
        std::lock_guard lock(batch->completionMutex);
        batch->result.taskExceptions[taskIndex] = std::move(failure);
        if (batch->result.taskExceptions[taskIndex]) { ++batch->result.failedTaskCount; }
        finished = --batch->remainingCount == 0;
    }
    if (finished) { batch->completed.notify_all(); }
}

void TaskSystem::workerLoop(std::size_t workerIndex) {
#ifdef ZL_TASK_SYSTEM_TESTING
    taskWorkerStarted();
#endif
    for (;;) {
        std::shared_ptr<TaskBatchState> batch;
        std::size_t taskIndex;
        {
            std::unique_lock lock(queueMutex);
            workAvailable.wait(lock, [&] { return closing || !pendingBatches.empty(); });
            if (pendingBatches.empty()) { break; } // closing, including constructor rollback.
            batch = pendingBatches.front();
            taskIndex = batch->nextTaskIndex++;
            if (batch->nextTaskIndex == batch->tasks.size()) { pendingBatches.pop_front(); }
        }
        executeTask(batch, taskIndex, workerIndex);
    }
#ifdef ZL_TASK_SYSTEM_TESTING
    taskWorkerStopped();
#endif
}

void TaskSystem::shutdown() {
    {
        std::lock_guard lock(queueMutex);
        closing = true;
    }
    workAvailable.notify_all();
    for (auto& worker : workers) {
        if (worker.joinable()) { worker.join(); }
    }
}
