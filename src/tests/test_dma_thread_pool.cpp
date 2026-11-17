#include <catch2/catch_test_macros.hpp>

#include "platform/idma_thread_pool.h"
#include "platform/dma_thread_pool.h"
#include "platform/dma_thread_pool_factory.h"
#include "platform/expected.h"
#include "platform/logging.h"

#include <thread>
#include <chrono>
#include <atomic>
#include <vector>

using namespace OmniGhost::Platform;

TEST_CASE("DmaThreadPoolFactory creates instance", "[dma_thread_pool]") {
    DmaThreadPoolFactory factory;
    
    // Test default thread count
    auto result1 = factory.Create();
    REQUIRE(result1);
    REQUIRE(result1.value() != nullptr);
    
    // Test custom thread count
    auto result2 = factory.Create(4);
    REQUIRE(result2);
    REQUIRE(result2.value() != nullptr);
    
    auto pool = std::move(result2.value());
    REQUIRE(pool->ThreadCount() == 4);
}

TEST_CASE("DmaThreadPool basic submit", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    std::atomic<int> counter{0};
    
    // Submit void task
    auto future = pool.SubmitVoid([&counter]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        counter.fetch_add(1, std::memory_order_relaxed);
    });
    
    // Wait for completion
    future.wait();
    
    REQUIRE(counter.load() == 1);
    
    // Test Submit with return value
    auto future2 = pool.Submit([]() -> int {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 42;
    });
    
    int result = future2.get();
    REQUIRE(result == 42);
}

TEST_CASE("DmaThreadPool SubmitDmaRead", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    // Test successful read
    auto future = pool.SubmitDmaRead<int>([]() -> Expected<int> {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Success(42);
    });
    
    auto result = future.get();
    REQUIRE(result);
    REQUIRE(result.value() == 42);
    
    // Test failed read
    auto future2 = pool.SubmitDmaRead<int>([]() -> Expected<int> {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Failure<int>(ErrorCode::DmaError, "Read failed");
    });
    
    auto result2 = future2.get();
    REQUIRE_FALSE(result2);
    REQUIRE(result2.error().code == ErrorCode::DmaError);
}

TEST_CASE("DmaThreadPool SubmitDmaWrite", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    // Test successful write
    auto future = pool.SubmitDmaWrite([]() -> Expected<void> {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Success();
    });
    
    auto result = future.get();
    REQUIRE(result);
    
    // Test failed write
    auto future2 = pool.SubmitDmaWrite([]() -> Expected<void> {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return Failure<void>(ErrorCode::DmaError, "Write failed");
    });
    
    auto result2 = future2.get();
    REQUIRE_FALSE(result2);
    REQUIRE(result2.error().code == ErrorCode::DmaError);
}

TEST_CASE("DmaThreadPool multiple concurrent tasks", "[dma_thread_pool]") {
    DmaThreadPool pool(4);
    
    std::atomic<int> counter{0};
    const int taskCount = 100;
    std::vector<std::future<void>> futures;
    
    for (int i = 0; i < taskCount; ++i) {
        auto future = pool.SubmitVoid([&counter]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            counter.fetch_add(1, std::memory_order_relaxed);
        });
        futures.push_back(std::move(future));
    }
    
    // Wait for all
    for (auto& f : futures) {
        f.wait();
    }
    
    REQUIRE(counter.load() == taskCount);
}

TEST_CASE("DmaThreadPool statistics", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    // Initial stats
    auto stats1 = pool.GetStats();
    REQUIRE(stats1.tasksSubmitted == 0);
    REQUIRE(stats1.tasksCompleted == 0);
    REQUIRE(stats1.tasksFailed == 0);
    
    // Submit some tasks
    pool.SubmitVoid([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    pool.SubmitVoid([]() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); });
    pool.SubmitVoid([]() { throw std::runtime_error("Test error"); }); // This will fail
    
    // Wait a bit for tasks to process
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    
    auto stats2 = pool.GetStats();
    REQUIRE(stats2.tasksSubmitted >= 3);
    REQUIRE(stats2.tasksCompleted >= 2);
    REQUIRE(stats2.tasksFailed >= 1);
    
    // Test QueueSize
    size_t queueSize = pool.QueueSize();
    // Should be 0 after tasks complete
    
    // Test ThreadCount
    REQUIRE(pool.ThreadCount() == 2);
}

TEST_CASE("DmaThreadPool WaitForAll", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    std::atomic<int> completed{0};
    
    pool.SubmitVoid([&completed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        completed.fetch_add(1, std::memory_order_relaxed);
    });
    
    pool.SubmitVoid([&completed]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        completed.fetch_add(1, std::memory_order_relaxed);
    });
    
    // Wait for all
    pool.WaitForAll();
    
    REQUIRE(completed.load() == 2);
}

TEST_CASE("DmaThreadPool Shutdown", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    // Submit some tasks
    pool.SubmitVoid([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
    pool.SubmitVoid([]() { std::this_thread::sleep_for(std::chrono::milliseconds(100)); });
    
    // Shutdown
    pool.Shutdown();
    
    REQUIRE(pool.IsStopped());
    
    // After shutdown, no more tasks can be submitted
    // This should throw or fail gracefully
    try {
        pool.SubmitVoid([]() {});
        // If it doesn't throw, the task should not execute
    } catch (const std::runtime_error&) {
        // Expected
    }
}

TEST_CASE("DmaThreadPool IsStopped", "[dma_thread_pool]") {
    DmaThreadPool pool(2);
    
    REQUIRE_FALSE(pool.IsStopped());
    
    pool.Shutdown();
    
    REQUIRE(pool.IsStopped());
}

TEST_CASE("DmaThreadPool QueueSize", "[dma_thread_pool]") {
    DmaThreadPool pool(1);
    
    REQUIRE(pool.QueueSize() == 0);
    
    // Submit slow task
    auto future = pool.SubmitVoid([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    });
    
    // Give it time to start
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    
    // Queue should be empty (task is running)
    // Note: QueueSize shows pending tasks, not running ones
    
    future.wait();
    
    // After completion
    REQUIRE(pool.QueueSize() == 0);
}

TEST_CASE("DmaThreadPool concurrent submit from multiple threads", "[dma_thread_pool]") {
    DmaThreadPool pool(4);
    
    const int numThreads = 8;
    const int tasksPerThread = 25;
    std::atomic<int> totalTasks{0};
    std::vector<std::thread> threads;
    
    for (int t = 0; t < numThreads; ++t) {
        threads.emplace_back([&]() {
            for (int i = 0; i < 25; ++i) {
                pool.SubmitVoid([&]() {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                });
            }
        });
    }
    
    for (auto& t : threads) {
        t.join();
    }
    
    // Wait for all to complete
    std::this_thread::sleep_for(std::chrono::milliseconds(500));
    
    auto stats = pool.GetStats();
    REQUIRE(stats.tasksSubmitted == numThreads * 25);
}

TEST_CASE("DmaThreadPoolFactory with custom thread count", "[dma_thread_pool]") {
    DmaThreadPoolFactory factory;
    
    // Test various thread counts
    for (size_t count : {1, 2, 4, 8, 16}) {
        auto result = factory.Create(count);
        REQUIRE(result);
        auto pool = std::move(result.value());
        REQUIRE(pool->ThreadCount() == count);
    }
}

} // namespace