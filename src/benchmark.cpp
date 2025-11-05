#include <atomic>
#include <chrono>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <windows.h>
#include "../include/mpmc_queue.h"
#include "../include/mutex_queue.h"

// 테스트용 데이터 구조체
struct TestData
{
    int value; // 4byte
    char padding[lfq::CACHE_LINE_SIZE - sizeof(int) - sizeof(std::atomic<size_t>)]; // 52byte

    // 총 56byte, 
};

template <typename QueueType>
void ProducerThread(QueueType &_queue, size_t _thread_id, std::atomic<size_t> &_success_count)
{
    size_t _local_success = 0;

    for (size_t i = 0; i < lfq::OPERATIONS_PER_THREAD; ++i)
    {
        TestData _data{static_cast<int>(_thread_id * lfq::OPERATIONS_PER_THREAD + i)};

        while (!_queue.Push(_data))
        {
            // 큐가 가득 찬 경우 재시도
            std::this_thread::yield();
        }

        ++_local_success;
    }

    _success_count.fetch_add(_local_success, std::memory_order_relaxed);
}

template <typename QueueType>
void ConsumerThread(QueueType &_queue, size_t _operations, std::atomic<size_t> &_success_count)
{
    size_t _local_success = 0;
    TestData _data;

    while (_local_success < _operations)
    {
        if (_queue.Pop(_data))
        {
            ++_local_success;
        }
        else
        {
            // 큐가 비어있는 경우 재시도
            std::this_thread::yield();
        }
    }

    _success_count.fetch_add(_local_success, std::memory_order_relaxed);
}

// 벤치마크 실행 함수
template <typename QueueType>
void RunBenchmark(const std::string &_queue_name, size_t _num_producers, size_t _num_consumers)
{
    std::cout << "\n========================================" << std::endl;
    std::cout << _queue_name << " 테스트" << std::endl;
    std::cout << "프로듀서: " << _num_producers << ", 컨슈머: " << _num_consumers << std::endl;
    std::cout << "스레드당 작업 수: " << lfq::OPERATIONS_PER_THREAD << std::endl;

    // 큐를 힙에 할당하여 스택 오버플로우 방지
    auto _queue = std::make_unique<QueueType>();
    std::atomic<size_t> _push_count{0};
    std::atomic<size_t> _pop_count{0};

    // 총 작업 수 계산
    size_t _total_operations = _num_producers * lfq::OPERATIONS_PER_THREAD;
    size_t _operations_per_consumer = _total_operations / _num_consumers;

    std::vector<std::thread> _producers;
    std::vector<std::thread> _consumers;

    // 시작 시간 측정
    auto _start_time = std::chrono::high_resolution_clock::now();

    // 프로듀서 스레드 시작
    for (size_t i = 0; i < _num_producers; ++i)
    {
        _producers.emplace_back(ProducerThread<QueueType>, std::ref(*_queue), i, std::ref(_push_count));
    }

    // 컨슈머 스레드 시작
    for (size_t i = 0; i < _num_consumers; ++i)
    {
        _consumers.emplace_back(ConsumerThread<QueueType>, std::ref(*_queue), _operations_per_consumer,
                                std::ref(_pop_count));
    }

    // 모든 스레드 종료 대기
    for (auto &t : _producers)
    {
        t.join();
    }

    for (auto &t : _consumers)
    {
        t.join();
    }

    // 종료 시간 측정
    auto _end_time = std::chrono::high_resolution_clock::now();
    auto _duration = std::chrono::duration_cast<std::chrono::milliseconds>(_end_time - _start_time);

    // 결과 출력
    double _ops_per_sec = (_total_operations * 2.0 * 1000.0) / _duration.count(); // push + pop
    double _throughput_mb = (_ops_per_sec * sizeof(TestData)) / (1024.0 * 1024.0);

    std::cout << "========================================" << std::endl;
    std::cout << "실행 시간: " << _duration.count() << " ms" << std::endl;
    std::cout << "Push 성공: " << _push_count << " / " << _total_operations << std::endl;
    std::cout << "Pop 성공: " << _pop_count << " / " << _total_operations << std::endl;
    std::cout << "처리량: " << std::fixed << std::setprecision(2) << _ops_per_sec << " ops/sec" << std::endl;
    std::cout << "데이터 처리량: " << std::fixed << std::setprecision(2) << _throughput_mb << " MB/s" << std::endl;
    std::cout << "========================================" << std::endl;
}

int main()
{
    // Windows 콘솔 UTF-8 출력 설정
    SetConsoleOutputCP(CP_UTF8);

    std::cout << "Lock-Free Queue vs Mutex Queue 성능 벤치마크" << std::endl;
    std::cout << "큐 크기: " << lfq::QUEUE_SIZE << std::endl;

    // 1p, 1c 테스트
    RunBenchmark<MPMCQueue<TestData, lfq::QUEUE_SIZE>>("Lock-Free MPMC Queue (1P/1C)", 1, 1);
    RunBenchmark<MutexQueue<TestData, lfq::QUEUE_SIZE>>("Mutex Queue (1P/1C)", 1, 1);

    // 2p, 2c 테스트
    RunBenchmark<MPMCQueue<TestData, lfq::QUEUE_SIZE>>("Lock-Free MPMC Queue (2P/2C)", 2, 2);
    RunBenchmark<MutexQueue<TestData, lfq::QUEUE_SIZE>>("Mutex Queue (2P/2C)", 2, 2);

    // 4p, 4c 테스트
    RunBenchmark<MPMCQueue<TestData, lfq::QUEUE_SIZE>>("Lock-Free MPMC Queue (4P/4C)", 4, 4);
    RunBenchmark<MutexQueue<TestData, lfq::QUEUE_SIZE>>("Mutex Queue (4P/4C)", 4, 4);

    // 6p, 6c 테스트 -> 테스트 CPU가 12스레드라 여기까지만 함
    RunBenchmark<MPMCQueue<TestData, lfq::QUEUE_SIZE>>("Lock-Free MPMC Queue (6P/6C)", 6, 6);
    RunBenchmark<MutexQueue<TestData, lfq::QUEUE_SIZE>>("Mutex Queue (6P/6C)", 6, 6);

    std::cout << "\n모든 벤치마크 완료" << std::endl;

    return 0;
}
