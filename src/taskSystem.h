#pragma once
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>
#include <vector>

struct TaskBatchResult {
    // Input task index identifies the slot; a null exception means success.
    std::vector<std::exception_ptr> taskExceptions;
    std::size_t failedTaskCount = 0;
};

struct TaskBatchState;

class TaskBatchHandle {
public:
    TaskBatchHandle() = default;
    // External threads may wait concurrently. Keep a handle alive while using the result.
    const TaskBatchResult& wait() const;
private:
    friend class TaskSystem;
    explicit TaskBatchHandle(std::shared_ptr<TaskBatchState> state);
    std::shared_ptr<TaskBatchState> batchState;
};

// One coordinator owns submission, shutdown and destruction. Callbacks must not
// reenter these operations or wait on this executor's batches. They must terminate.
class TaskSystem {
public:
    using Task = std::function<void(std::size_t workerIndex)>;
    explicit TaskSystem(std::size_t workerCount);
    ~TaskSystem() noexcept;
    TaskSystem(const TaskSystem&) = delete;
    TaskSystem& operator=(const TaskSystem&) = delete;
    TaskSystem(TaskSystem&&) = delete;
    TaskSystem& operator=(TaskSystem&&) = delete;

    // Owns the callbacks. Zero workers execute inline before this call returns.
    TaskBatchHandle submitBatch(std::vector<Task> tasks);
    // Drains all accepted work, joins workers, and rejects subsequent submissions.
    void shutdown();
private:
    static void executeTask(const std::shared_ptr<TaskBatchState>& batch, std::size_t taskIndex,
                            std::size_t workerIndex);
    void workerLoop(std::size_t workerIndex);

    std::mutex queueMutex;
    std::condition_variable workAvailable;
    std::deque<std::shared_ptr<TaskBatchState>> pendingBatches;
    bool closing = false;
    std::vector<std::jthread> workers;
};
