#pragma once
#include <cstdint>
#include <functional>
#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

struct Task {
    uint64_t id;
    std::function<void()> function;
    std::chrono::steady_clock::time_point scheduled_time;
    int priority;
    bool repeating;
    std::chrono::milliseconds interval;
    
    bool operator<(const Task& other) const {
        if (scheduled_time != other.scheduled_time) {
            return scheduled_time > other.scheduled_time; // Min-heap by time
        }
        return priority < other.priority; // Higher priority first
    }
};

struct Thread {
    uint64_t id;
    std::thread handle;
    std::atomic<bool> active{true};
    std::atomic<uint64_t> current_task_id{0};
    std::chrono::steady_clock::time_point last_activity;
    int cpu_affinity;
};

class Scheduler {
public:
    using TickFn = std::function<void()>;
    
    Scheduler();
    ~Scheduler();
    
    // Core scheduling functions
    void initialize(int num_threads = std::thread::hardware_concurrency());
    void shutdown();
    
    // Task management
    uint64_t schedule_task(std::function<void()> task, int priority = 0);
    uint64_t schedule_delayed_task(std::function<void()> task, std::chrono::milliseconds delay, int priority = 0);
    uint64_t schedule_repeating_task(std::function<void()> task, std::chrono::milliseconds interval, int priority = 0);
    bool cancel_task(uint64_t task_id);
    
    // Thread management
    void set_thread_affinity(uint64_t thread_id, int cpu_core);
    void pause_thread(uint64_t thread_id);
    void resume_thread(uint64_t thread_id);
    
    // Legacy compatibility
    void run(TickFn tick);
    
    // Statistics
    size_t get_pending_tasks() const;
    size_t get_active_threads() const;
    double get_cpu_usage() const;

private:
    void worker_thread(uint64_t thread_id);
    void scheduler_thread();
    
    std::vector<std::unique_ptr<Thread>> threads_;
    std::priority_queue<Task> task_queue_;
    std::mutex queue_mutex_;
    std::condition_variable queue_cv_;
    std::condition_variable scheduler_cv_;
    
    std::atomic<bool> running_{false};
    std::atomic<uint64_t> next_task_id_{1};
    std::atomic<uint64_t> next_thread_id_{1};
    
    std::thread scheduler_thread_handle_;
    std::chrono::steady_clock::time_point start_time_;
    std::atomic<uint64_t> completed_tasks_{0};
};
