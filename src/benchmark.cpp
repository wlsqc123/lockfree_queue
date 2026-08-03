#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <memory>
#include <thread>
#include <vector>
#include <windows.h>

#include "mpmc_queue.h"
#include "mutex_queue.h"

namespace
{
    constexpr size_t BenchmarkRepeatCount = 3;

    // 큐 슬롯 하나가 캐시 라인 하나를 사용하도록 데이터 크기를 맞춘다.
    struct TestData
    {
        int value;
        char padding[lfq::CACHE_LINE_SIZE - sizeof(int) - sizeof(std::atomic<size_t>)];
    };

    struct BenchmarkResult
    {
        double duration_ms;
        double messages_per_sec;
        double operations_per_sec;
        double throughput_mb;
        size_t message_count;
        size_t push_retry_count;
        size_t pop_retry_count;
        std::uint64_t checksum;
        std::uint64_t expected_checksum;
    };

    // 정해진 수의 값을 Push하고 큐가 가득 차 발생한 재시도 횟수를 기록한다.
    template <typename QueueType>
    void ProducerThread(QueueType& _queue, size_t _thread_id, std::atomic<size_t>& _retry_count)
    {
        size_t _local_retry_count = 0;

        for (auto _operation_index = 0; _operation_index < lfq::OPERATIONS_PER_THREAD; ++_operation_index)
        {
            TestData _data{static_cast<int>(_thread_id * lfq::OPERATIONS_PER_THREAD + _operation_index)};

            while (false == _queue.Push(_data))
            {
                ++_local_retry_count;
                std::this_thread::yield();
            }
        }

        _retry_count.fetch_add(_local_retry_count, std::memory_order_relaxed);
    }

    // 정해진 수의 값을 Pop하고 재시도 횟수와 전달된 값의 체크섬을 기록한다.
    template <typename QueueType>
    void ConsumerThread(QueueType& _queue, size_t _operation_count, std::atomic<size_t>& _retry_count, std::atomic<std::uint64_t>& _checksum)
    {
        size_t _success_count = 0;
        size_t _local_retry_count = 0;
        std::uint64_t _local_checksum = 0;
        TestData _data;

        while (_success_count < _operation_count)
        {
            if (true == _queue.Pop(_data))
            {
                ++_success_count;
                _local_checksum += static_cast<std::uint64_t>(_data.value);
            }
            else
            {
                ++_local_retry_count;
                std::this_thread::yield();
            }
        }

        _retry_count.fetch_add(_local_retry_count, std::memory_order_relaxed);
        _checksum.fetch_add(_local_checksum, std::memory_order_relaxed);
    }

    // 한 번의 벤치마크를 실행하고 시간, 처리량, 재시도와 체크섬 결과를 반환한다.
    template <typename QueueType>
    BenchmarkResult RunBenchmarkOnce(size_t _producer_count, size_t _consumer_count)
    {
        auto _queue = std::make_unique<QueueType>();
        std::atomic<size_t> _push_retry_count{0};
        std::atomic<size_t> _pop_retry_count{0};
        std::atomic<std::uint64_t> _checksum{0};

        const size_t _total_operation_count = _producer_count * lfq::OPERATIONS_PER_THREAD;
        const size_t _base_operation_count = _total_operation_count / _consumer_count;
        const size_t _remaining_operation_count = _total_operation_count % _consumer_count;

        std::vector<std::thread> _producers;
        std::vector<std::thread> _consumers;
        _producers.reserve(_producer_count);
        _consumers.reserve(_consumer_count);

        const auto _start_time = std::chrono::steady_clock::now();

        for (size_t _producer_index = 0; _producer_index < _producer_count; ++_producer_index)
        {
            _producers.emplace_back(ProducerThread<QueueType>, std::ref(*_queue), _producer_index, std::ref(_push_retry_count));
        }

        for (size_t _consumer_index = 0; _consumer_index < _consumer_count; ++_consumer_index)
        {
            const size_t _operation_count =
                _base_operation_count + (_consumer_index < _remaining_operation_count ? 1 : 0);

            _consumers.emplace_back(ConsumerThread<QueueType>, std::ref(*_queue), _operation_count, std::ref(_pop_retry_count), std::ref(_checksum));
        }

        for (auto& _producer : _producers)
        {
            _producer.join();
        }

        for (auto& _consumer : _consumers)
        {
            _consumer.join();
        }

        const auto _end_time = std::chrono::steady_clock::now();
        const double _duration_sec = std::chrono::duration<double>(_end_time - _start_time).count();
        const double _messages_per_sec = static_cast<double>(_total_operation_count) / _duration_sec;
        const double _operations_per_sec = _messages_per_sec * 2.0;
        const double _throughput_mb = (_operations_per_sec * sizeof(TestData)) / (1024.0 * 1024.0);
        const std::uint64_t _total_operation_count64 = static_cast<std::uint64_t>(_total_operation_count);
        const std::uint64_t _expected_checksum = (_total_operation_count64 * (_total_operation_count64 - 1)) / 2;

        return BenchmarkResult{
            _duration_sec * 1000.0,
            _messages_per_sec,
            _operations_per_sec,
            _throughput_mb,
            _total_operation_count,
            _push_retry_count.load(std::memory_order_relaxed),
            _pop_retry_count.load(std::memory_order_relaxed),
            _checksum.load(std::memory_order_relaxed),
            _expected_checksum};
    }

    // 세 번의 실행 결과를 시간순으로 정렬해 중앙값에 해당하는 결과를 선택한다.
    BenchmarkResult GetMedianResult(std::array<BenchmarkResult, BenchmarkRepeatCount> _results)
    {
        std::sort(_results.begin(), _results.end(), [](const BenchmarkResult& _left, const BenchmarkResult& _right)
        {
            return _left.duration_ms < _right.duration_ms;
        });

        return _results[BenchmarkRepeatCount / 2];
    }

    // 선택된 중앙값 결과를 사람이 확인하기 쉬운 형식으로 출력한다.
    void PrintResult(const char* _queue_name, const BenchmarkResult& _result)
    {
        const bool _checksum_valid = _result.checksum == _result.expected_checksum;

        std::cout << "\n" << _queue_name << " 중앙값\n";
        std::cout << "  실행 시간: " << std::fixed << std::setprecision(2) << _result.duration_ms << " ms\n";
        std::cout << "  처리 메시지: " << _result.message_count << "개\n";
        std::cout << "  메시지 처리량: " << _result.messages_per_sec << " messages/sec\n";
        std::cout << "  큐 연산 처리량: " << _result.operations_per_sec << " ops/sec\n";
        std::cout << "  데이터 처리량(Push+Pop): " << _result.throughput_mb << " MB/s\n";
        std::cout << "  Push 재시도: " << _result.push_retry_count << '\n';
        std::cout << "  Pop 재시도: " << _result.pop_retry_count << '\n';
        std::cout << "  체크섬: " << _result.checksum << " / " << _result.expected_checksum
                  << " (" << (true == _checksum_valid ? "정상" : "오류") << ")\n";
    }

    // 두 큐의 실행 순서를 번갈아 가며 세 번 측정하고 각각의 중앙값을 출력한다.
    template <typename LockFreeQueueType, typename TwoLockQueueType>
    void RunComparison(const char* _case_name, size_t _producer_count, size_t _consumer_count)
    {
        std::array<BenchmarkResult, BenchmarkRepeatCount> _lock_free_results;
        std::array<BenchmarkResult, BenchmarkRepeatCount> _two_lock_results;

        std::cout << "\n============================================================\n";
        std::cout << _case_name << '\n';
        std::cout << "스레드당 작업=" << lfq::OPERATIONS_PER_THREAD << '\n';

        for (size_t _repeat_index = 0; _repeat_index < BenchmarkRepeatCount; ++_repeat_index)
        {
            std::cout << "\n[" << _repeat_index + 1 << '/' << BenchmarkRepeatCount << "] ";

            if ((_repeat_index % 2) == 0)
            {
                std::cout << "Lock-Free → Two-Lock 순서로 측정\n";
                _lock_free_results[_repeat_index] = RunBenchmarkOnce<LockFreeQueueType>(_producer_count, _consumer_count);
                _two_lock_results[_repeat_index] = RunBenchmarkOnce<TwoLockQueueType>(_producer_count, _consumer_count);
            }
            else
            {
                std::cout << "Two-Lock → Lock-Free 순서로 측정\n";
                _two_lock_results[_repeat_index] = RunBenchmarkOnce<TwoLockQueueType>(_producer_count, _consumer_count);
                _lock_free_results[_repeat_index] = RunBenchmarkOnce<LockFreeQueueType>(_producer_count, _consumer_count);
            }

            std::cout << "  Lock-Free: " << std::fixed << std::setprecision(2)
                      << _lock_free_results[_repeat_index].duration_ms << " ms"
                      << " | Two-Lock: " << _two_lock_results[_repeat_index].duration_ms << " ms\n";
        }

        PrintResult("Lock-Free MPMC Queue", GetMedianResult(_lock_free_results));
        PrintResult("Two-Lock Queue", GetMedianResult(_two_lock_results));
    }
}

int main()
{
    SetConsoleOutputCP(CP_UTF8);

    using LockFreeQueue = MPMCQueue<TestData, lfq::QUEUE_SIZE>;
    using TwoLockQueue = MutexQueue<TestData, lfq::QUEUE_SIZE>;

    std::cout << "Lock-Free Queue vs Two-Lock Queue 성능 벤치마크\n";
    std::cout << "큐 크기=" << lfq::QUEUE_SIZE
              << " | 반복=" << BenchmarkRepeatCount << "회 후 중앙값 사용\n";

    RunComparison<LockFreeQueue, TwoLockQueue>("1P / 1C", 1, 1);
    RunComparison<LockFreeQueue, TwoLockQueue>("2P / 2C", 2, 2);
    RunComparison<LockFreeQueue, TwoLockQueue>("4P / 4C", 4, 4);
    RunComparison<LockFreeQueue, TwoLockQueue>("6P / 6C", 6, 6);

    std::cout << "\n모든 벤치마크 완료\n";
    return 0;
}
