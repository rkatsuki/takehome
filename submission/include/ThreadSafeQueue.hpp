#pragma once

#include <queue>
#include <mutex>
#include <condition_variable>
#include <optional>

/**
 * @brief A high-performance, contention-aware Thread-Safe Queue.
 * * DESIGN RATIONALE:
 * In High-Frequency Trading (HFT) systems, the cost of a mutex is not just the 
 * lock itself, but the "Cache Line Ping-Ponging" it causes. This queue uses 
 * Hardware Alignment and Batch-Swapping to minimize these effects.
 */
template <typename T>
class ThreadSafeQueue {
private:
    /**
     * @note ALIGNAS(64): False Sharing Protection
     * Standard CPUs fetch data in 64-byte chunks (Cache Lines). 
     * If the Mutex and the Queue data sit on the same 64-byte line, a CPU 
     * core modifying the mutex will invalidate the cache of the CPU core 
     * reading the queue, even if they aren't touching the same variables.
     * We force them onto separate cache lines to allow parallel access.
     */
    alignas(64) mutable std::mutex mutex_;
    alignas(64) std::condition_variable cv_;
    alignas(64) std::queue<T> queue_;
    
    // Flag to indicate the system is shutting down
    bool stopped_ = false;

public:
    ThreadSafeQueue() = default;

    /**
     * @brief Pushes an item into the queue.
     * @details Uses std::move to ensure zero-copy transfer of the data into the 
     * underlying container. Notifies a single waiting thread to wake up.
     */
    void push(T value) {
        {
            // Scope-based lock: we want the mutex held for the shortest time possible.
            std::lock_guard<std::mutex> lock(mutex_);
            queue_.push(std::move(value));
        }
        // Notify outside the lock to prevent the "Wait-Wakeup-Block" race condition
        // where the woken thread immediately hits the still-held mutex.
        cv_.notify_one();
    }

    /**
     * @brief Standard blocking pop for single-item processing.
     * @return std::optional containing the value, or nullopt if the queue is stopped.
     */
    std::optional<T> pop() {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Predicate-based wait: handles spurious wakeups and wait-on-stop logic.
        cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });
        
        // If we woke up because the system stopped and no data is left, return empty.
        if (queue_.empty() && stopped_) return std::nullopt;
        
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    /**
     * @brief THE BATCH-SWAP OPTIMIZATION (Critical for HFT)
     * * @details Standard popping requires 1 Lock/Unlock cycle per message.
     * If the Engine produces 1,000 trades, the Output Thread would lock 1,000 times.
     * * Instead, we use std::swap to steal the entire internal memory of the 
     * queue container and move it to a local variable in the consumer thread.
     * * @performance O(1) complexity. Moving the internal pointers of a std::queue 
     * container is nearly instantaneous and involves zero heap allocations.
     * * @param local_queue A thread-local queue to receive the batch.
     * @return bool True if data was acquired, False if the queue is drained and stopped.
     */
    bool pop_all(std::queue<T>& local_queue) {
        std::unique_lock<std::mutex> lock(mutex_);
        
        // Wait until there is work to do or a stop signal is received.
        cv_.wait(lock, [this] { return !queue_.empty() || stopped_; });

        // If no data and stopped, the consumer should exit its loop.
        if (queue_.empty() && stopped_) return false;

        /**
         * @note PTR-SWAP: This is the high-speed handoff.
         * The internal 'queue_' is now empty, and 'local_queue' contains the batch.
         * The mutex is released immediately after this, allowing the Producer 
         * to start filling the 'queue_' again while we process the 'local_queue'.
         */
        std::swap(local_queue, queue_);
        return true;
    }

    /**
     * @brief Signals all threads to stop and wake up.
     */
    void stop() {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            stopped_ = true;
        }
        // Notify all waiting threads so they can check the stopped_ flag and exit.
        cv_.notify_all();
    }

    /**
    * @brief Non-blocking pop. 
    * @return Value if available, std::nullopt otherwise. 
    * Does NOT wait on condition variable.
    */
    std::optional<T> try_pop() {
        std::lock_guard<std::mutex> lock(mutex_);
        if (queue_.empty()) return std::nullopt;
        
        T value = std::move(queue_.front());
        queue_.pop();
        return value;
    }

    bool empty() const {
       std::lock_guard<std::mutex> lock(mutex_);
    return queue_.empty();
    }
};