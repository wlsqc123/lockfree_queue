#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include "mpmc_queue.h"
#include "mutex_queue.h"

const int NUM_PRODUCERS = 4;
const int NUM_CONSUMERS = 4;
const int ITEMS_PER_THREAD = 1000000;
const int QUEUE_SIZE = 65536;

template<typename QueueType>
void producer(QueueType& q, int /*id*/) {
    for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
        while (!q.Push(i)) {
            std::this_thread::yield();
        }
    }
}

template<typename QueueType>
void consumer(QueueType& q, int /*id*/) {
    int item;
    for (int i = 0; i < ITEMS_PER_THREAD; ++i) {
        while (!q.Pop(item)) {
            std::this_thread::yield();
        }
    }
}

template<typename QueueType>
long long run_benchmark(const std::string& name) {
    std::cout << "Starting " << name << "..." << std::endl;
    auto q = std::make_unique<QueueType>();
    std::vector<std::thread> threads;
    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        threads.emplace_back(producer<QueueType>, std::ref(*q), i);
    }
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        threads.emplace_back(consumer<QueueType>, std::ref(*q), i);
    }

    for (auto& t : threads) {
        t.join();
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();
    std::cout << name << " duration: " << duration << "ms" << std::endl;
    return duration;
}

int main() {
    std::cout << "Benchmarking MutexQueue..." << std::endl;
    run_benchmark<MutexQueue<int, QUEUE_SIZE>>("MutexQueue");

    std::cout << "Benchmarking MPMCQueue..." << std::endl;
    // Note: MPMCQueue Pop is currently empty, so this might hang or behave unexpectedly if not fixed.
    // We run it to demonstrate the issue.
    run_benchmark<MPMCQueue<int, QUEUE_SIZE>>("MPMCQueue");

    return 0;
}
