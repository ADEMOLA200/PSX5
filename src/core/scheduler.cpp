#include "scheduler.h"
#include <algorithm>
#include <iostream>

Scheduler::Scheduler() : start_time_(std::chrono::steady_clock::now()) {}

Scheduler::~Scheduler() {
    shutdown();
}

void Scheduler::initialize(int num_threads) {
    if (running_.load()) return;
    
    running_.store(true);
    
    // Create worker threads
    threads_.reserve(num_threads);
    for (int i = 0; i < num_threads; ++i) {
        auto thread = std::make_unique<Thread>();
        thread->id = next_thread_id_.fetch_add(1);
        thread->cpu_affinity = i % std::thread::hardware_concurrency();
        thread->last_activity = std::chrono::steady_clock::now();
        
        thread->handle = std::thread(&Scheduler::worker_thread, this, thread->id);
        
        // Set CPU affinity if supported
#ifdef _WIN32
        SetThreadAffinityMask(thread->handle.native_handle(), 1ULL << thread->cpu_affinity);
#elif defined(__linux__)
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(thread->cpu_affinity, &cpuset);
        pthread_setaffinity_np(thread->handle.native_handle(), sizeof(cpu_set_t), &cpuset);
#endif
        
        threads_.push_back(std::move(thread));
    }
    
    // Start scheduler thread
    scheduler_thread_handle_ = std::thread(&Scheduler::scheduler_thread, this);
}

void Scheduler::shutdown() {
    if (!running_.load()) return;
    
    running_.store(false);
    
    // Wake up all threads
    queue_cv_.notify_all();
    scheduler_cv_.notify_all();
    
    // Join scheduler thread
    if (scheduler_thread_handle_.joinable()) {
        scheduler_thread_handle_.join();
    }
    
    // Join worker threads
    for (auto& thread : threads_) {
        thread->active.store(false);
        if (thread->handle.joinable()) {
            thread->handle.join();
        }
    }
    
    threads_.clear();
}

uint64_t Scheduler::schedule_task(std::function<void()> task, int priority) {
    uint64_t task_id = next_task_id_.fetch_add(1);
    
    Task new_task;
    new_task.id = task_id;
    new_task.function = std::move(task);
    new_task.scheduled_time = std::chrono::steady_clock::now();
    new_task.priority = priority;
    new_task.repeating = false;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(new_task);
    }
    
    queue_cv_.notify_one();
    return task_id;
}

uint64_t Scheduler::schedule_delayed_task(std::function<void()> task, std::chrono::milliseconds delay, int priority) {
    uint64_t task_id = next_task_id_.fetch_add(1);
    
    Task new_task;
    new_task.id = task_id;
    new_task.function = std::move(task);
    new_task.scheduled_time = std::chrono::steady_clock::now() + delay;
    new_task.priority = priority;
    new_task.repeating = false;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(new_task);
    }
    
    scheduler_cv_.notify_one();
    return task_id;
}

uint64_t Scheduler::schedule_repeating_task(std::function<void()> task, std::chrono::milliseconds interval, int priority) {
    uint64_t task_id = next_task_id_.fetch_add(1);
    
    Task new_task;
    new_task.id = task_id;
    new_task.function = std::move(task);
    new_task.scheduled_time = std::chrono::steady_clock::now() + interval;
    new_task.priority = priority;
    new_task.repeating = true;
    new_task.interval = interval;
    
    {
        std::lock_guard<std::mutex> lock(queue_mutex_);
        task_queue_.push(new_task);
    }
    
    scheduler_cv_.notify_one();
    return task_id;
}

void Scheduler::worker_thread(uint64_t thread_id) {
    while (running_.load()) {
        Task task;
        bool has_task = false;
        
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            queue_cv_.wait(lock, [this] { 
                return !running_.load() || (!task_queue_.empty() && 
                       task_queue_.top().scheduled_time <= std::chrono::steady_clock::now()); 
            });
            
            if (!running_.load()) break;
            
            if (!task_queue_.empty() && task_queue_.top().scheduled_time <= std::chrono::steady_clock::now()) {
                task = task_queue_.top();
                task_queue_.pop();
                has_task = true;
            }
        }
        
        if (has_task) {
            // Update thread activity
            auto thread_it = std::find_if(threads_.begin(), threads_.end(),
                [thread_id](const std::unique_ptr<Thread>& t) { return t->id == thread_id; });
            
            if (thread_it != threads_.end()) {
                (*thread_it)->current_task_id.store(task.id);
                (*thread_it)->last_activity = std::chrono::steady_clock::now();
            }
            
            // Execute task
            try {
                task.function();
                completed_tasks_.fetch_add(1);
            } catch (const std::exception& e) {
                std::cerr << "Task " << task.id << " failed: " << e.what() << std::endl;
            }
            
            // Reschedule if repeating
            if (task.repeating) {
                task.scheduled_time = std::chrono::steady_clock::now() + task.interval;
                {
                    std::lock_guard<std::mutex> lock(queue_mutex_);
                    task_queue_.push(task);
                }
                scheduler_cv_.notify_one();
            }
            
            // Clear current task
            if (thread_it != threads_.end()) {
                (*thread_it)->current_task_id.store(0);
            }
        }
    }
}

void Scheduler::scheduler_thread() {
    while (running_.load()) {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        
        if (task_queue_.empty()) {
            scheduler_cv_.wait(lock);
            continue;
        }
        
        auto next_time = task_queue_.top().scheduled_time;
        auto now = std::chrono::steady_clock::now();
        
        if (next_time <= now) {
            queue_cv_.notify_one();
            scheduler_cv_.wait_for(lock, std::chrono::milliseconds(1));
        } else {
            scheduler_cv_.wait_until(lock, next_time);
        }
    }
}

void Scheduler::run(TickFn tick) {
    // Legacy compatibility - run single-threaded tick loop
    if (!running_.load()) {
        initialize(1);
    }
    
    while (running_.load()) {
        tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
    }
}

size_t Scheduler::get_pending_tasks() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return task_queue_.size();
}

size_t Scheduler::get_active_threads() const {
    return std::count_if(threads_.begin(), threads_.end(),
        [](const std::unique_ptr<Thread>& t) { return t->active.load(); });
}

double Scheduler::get_cpu_usage() const {
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_time_).count();
    
    if (elapsed == 0) return 0.0;
    
    size_t active_threads = get_active_threads();
    return (double)active_threads / std::thread::hardware_concurrency() * 100.0;
}
