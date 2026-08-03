#include <atomic>
#include <chrono>
#include <cstddef>
#include <iostream>
#include <thread>
#include <vector>
#include <windows.h>

#include "mpmc_queue.h"

namespace
{
    int g_failure_count = 0;

    void Check(bool _condition, const char* _message)
    {
        if (true == _condition)
        {
            return;
        }

        ++g_failure_count;

        std::cerr << "  실패: " << _message << '\n';
    }

    // 단일 스레드에서 빈 큐/가득 찬 큐의 반환값과 FIFO 순서를 확인한다.
    // Pop과 Push로 링버퍼가 한 바퀴 순환한 뒤에도 기존 순서가 유지되는지 함께 검증한다.
    void TestSingleThreadBoundaryAndFifo()
    {
        MPMCQueue<int, 4> _queue;
        int _value = -1;

        Check(_queue.GetCapacity() == 4, "큐 용량이 4가 아님");
        Check(true == _queue.IsEmpty(), "생성된 큐가 비어 있지 않음");
        Check(_queue.GetSize() == 0, "생성된 큐의 크기가 0이 아님");
        Check(false == _queue.Pop(_value), "빈 큐에서 Pop이 성공함");

        int _lvalue = 10;
        Check(true == _queue.Push(_lvalue), "첫 번째 Push 실패");
        Check(true == _queue.Push(20), "두 번째 Push 실패");
        Check(true == _queue.Push(30), "세 번째 Push 실패");
        Check(true == _queue.Push(40), "네 번째 Push 실패");
        Check(false == _queue.Push(50), "가득 찬 큐에서 Push가 성공함");
        Check(_queue.GetSize() == 4, "가득 찬 큐의 크기가 4가 아님");

        Check(true == _queue.Pop(_value), "첫 번째 Pop 실패");
        Check(_value == 10, "첫 번째 값의 FIFO 순서가 틀림");
        Check(true == _queue.Pop(_value), "두 번째 Pop 실패");
        Check(_value == 20, "두 번째 값의 FIFO 순서가 틀림");

        Check(true == _queue.Push(50), "링버퍼 순환 후 첫 번째 Push 실패");
        Check(true == _queue.Push(60), "링버퍼 순환 후 두 번째 Push 실패");
        Check(false == _queue.Push(70), "다시 가득 찬 큐에서 Push가 성공함");

        Check(true == _queue.Pop(_value), "세 번째 Pop 실패");
        Check(_value == 30, "세 번째 값의 FIFO 순서가 틀림");
        Check(true == _queue.Pop(_value), "네 번째 Pop 실패");
        Check(_value == 40, "네 번째 값의 FIFO 순서가 틀림");
        Check(true == _queue.Pop(_value), "다섯 번째 Pop 실패");
        Check(_value == 50, "다섯 번째 값의 FIFO 순서가 틀림");
        Check(true == _queue.Pop(_value), "여섯 번째 Pop 실패");
        Check(_value == 60, "여섯 번째 값의 FIFO 순서가 틀림");

        Check(false == _queue.Pop(_value), "모두 소비한 큐에서 Pop이 성공함");
        Check(true == _queue.IsEmpty(), "모두 소비한 큐가 비어 있지 않음");
        Check(_queue.GetSize() == 0, "모두 소비한 큐의 크기가 0이 아님");
    }

    // 알고리즘이 지원하는 최소 용량인 2에서 같은 슬롯을 10만 번 반복해 재사용한다.
    // 슬롯의 generation 갱신 오류로 이전 값이 노출되거나 FIFO 순서가 깨지는지 확인한다.
    void TestRepeatedSlotReuse()
    {
        constexpr int IterationCount = 100'000;
        MPMCQueue<int, 2> _queue;

        for (int _iteration = 0; _iteration < IterationCount; ++_iteration)
        {
            int _first = -1;
            int _second = -1;

            Check(true == _queue.Push(_iteration * 2), "슬롯 재사용 중 첫 번째 Push 실패");
            Check(true == _queue.Push(_iteration * 2 + 1), "슬롯 재사용 중 두 번째 Push 실패");
            Check(false == _queue.Push(-1), "가득 찬 용량 2 큐에서 Push가 성공함");
            Check(true == _queue.Pop(_first), "슬롯 재사용 중 첫 번째 Pop 실패");
            Check(true == _queue.Pop(_second), "슬롯 재사용 중 두 번째 Pop 실패");
            Check(_first == _iteration * 2, "슬롯 재사용 후 첫 번째 값이 틀림");
            Check(_second == _iteration * 2 + 1, "슬롯 재사용 후 두 번째 값이 틀림");
            Check(true == _queue.IsEmpty(), "반복 종료 후 큐가 비어 있지 않음");
        }
    }

    // 생산자와 소비자 스레드가 동시에 동작할 때 10만 개 값의 전달 순서를 확인한다.
    // 소비자가 0부터 99999까지 빠짐없이 동일한 FIFO 순서로 받는지 검증한다.
    void TestSingleProducerSingleConsumerOrder()
    {
        constexpr int ItemCount = 100'000;
        MPMCQueue<int, 64> _queue;
        std::atomic<bool> _order_mismatch{false};

        std::thread _producer([&_queue, ItemCount]()
        {
            for (int _value = 0; _value < ItemCount; ++_value)
            {
                while (false == _queue.Push(_value))
                {
                }
            }
        });

        std::thread _consumer([&_queue, &_order_mismatch, ItemCount]()
        {
            for (int _expected = 0; _expected < ItemCount; ++_expected)
            {
                int _value = -1;
                while (false == _queue.Pop(_value))
                {
                }

                if (_value != _expected)
                {
                    _order_mismatch.store(true, std::memory_order_relaxed);
                }
            }
        });

        _producer.join();
        _consumer.join();

        Check(false == _order_mismatch.load(std::memory_order_relaxed), "단일 생산자/소비자 FIFO 순서가 틀림");
        Check(true == _queue.IsEmpty(), "단일 생산자/소비자 테스트 후 큐가 비어 있지 않음");
        Check(_queue.GetSize() == 0, "단일 생산자/소비자 테스트 후 큐의 크기가 0이 아님");
    }

    // 지정한 수의 생산자와 소비자를 동시에 실행해 각 값의 소비 횟수를 기록한다.
    // 전체 실행 후 입력/출력 수, 누락, 중복, 범위 밖 값과 큐의 최종 상태를 검증한다.
    void RunMpmcExactlyOnceCase(std::size_t _producer_count, std::size_t _consumer_count, std::size_t _items_per_producer)
    {
        const std::size_t _total_item_count = _producer_count * _items_per_producer;
        MPMCQueue<std::size_t, 64> _queue;

        std::vector<std::atomic<unsigned int>> _seen(_total_item_count);

        std::atomic<std::size_t> _push_count{0};
        std::atomic<std::size_t> _pop_count{0};
        std::atomic<std::size_t> _invalid_count{0};

        for (auto& _count : _seen)
        {
            _count.store(0, std::memory_order_relaxed);
        }

        std::vector<std::thread> _producers;
        std::vector<std::thread> _consumers;
        _producers.reserve(_producer_count);
        _consumers.reserve(_consumer_count);

        for (auto _producer_index = 0; _producer_index < _producer_count; ++_producer_index)
        {
            _producers.emplace_back([&, _producer_index]()
            {
                const std::size_t _first_value = _producer_index * _items_per_producer;
                for (std::size_t _offset = 0; _offset < _items_per_producer; ++_offset)
                {
                    const std::size_t _value = _first_value + _offset;
                    while (false == _queue.Push(_value))
                    {
                    }
                    _push_count.fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto _consumer_index = 0; _consumer_index < _consumer_count; ++_consumer_index)
        {
            _consumers.emplace_back([&]()
            {
                while (_pop_count.load(std::memory_order_acquire) < _total_item_count)
                {
                    std::size_t _value = 0;
                    if (false == _queue.Pop(_value))
                    {
                        continue;
                    }

                    const std::size_t _previous_pop_count = _pop_count.fetch_add(1, std::memory_order_acq_rel);

                    if (_previous_pop_count >= _total_item_count || _value >= _total_item_count)
                    {
                        _invalid_count.fetch_add(1, std::memory_order_relaxed);
                        continue;
                    }

                    _seen[_value].fetch_add(1, std::memory_order_relaxed);
                }
            });
        }

        for (auto& _producer : _producers)
        {
            _producer.join();
        }
        for (auto& _consumer : _consumers)
        {
            _consumer.join();
        }

        std::size_t _missing_count = 0;
        std::size_t _duplicate_count = 0;

        for (const auto& _count : _seen)
        {
            const unsigned int _delivery_count = _count.load(std::memory_order_relaxed);
            if (_delivery_count == 0)
            {
                ++_missing_count;
            }
            else if (_delivery_count > 1)
            {
                ++_duplicate_count;
            }
        }

        const std::size_t _final_push_count = _push_count.load(std::memory_order_relaxed);
        const std::size_t _final_pop_count = _pop_count.load(std::memory_order_relaxed);
        const std::size_t _final_invalid_count = _invalid_count.load(std::memory_order_relaxed);

        Check(_final_push_count == _total_item_count, "전체 Push 수가 예상과 다름");
        Check(_final_pop_count == _total_item_count, "전체 Pop 수가 예상과 다름");
        Check(_missing_count == 0, "소비되지 않은 값이 있음");
        Check(_duplicate_count == 0, "중복으로 소비된 값이 있음");
        Check(_final_invalid_count == 0, "범위를 벗어난 값이 소비됨");
        Check(true == _queue.IsEmpty(), "MPMC 테스트 후 큐가 비어 있지 않음");
        Check(_queue.GetSize() == 0, "MPMC 테스트 후 큐의 크기가 0이 아님");

        std::cout << "       생산자=" << _producer_count
                  << " | 소비자=" << _consumer_count
                  << " | 예상=" << _total_item_count
                  << " | 입력=" << _final_push_count
                  << " | 출력=" << _final_pop_count
                  << " | 누락=" << _missing_count
                  << " | 중복=" << _duplicate_count
                  << " | 비정상=" << _final_invalid_count << '\n';
    }

    // 생산자 또는 소비자 한쪽이 많은 비대칭 상황과 양쪽 모두 많은 경합 상황을 검사한다.
    // 4P/1C, 1P/4C, 4P/4C 구성에서 모든 값이 정확히 한 번 전달되는지 검증한다.
    void TestMpmcExactlyOnceDelivery()
    {
        constexpr std::size_t ItemsPerProducer = 25'000;

        RunMpmcExactlyOnceCase(4, 1, ItemsPerProducer);
        RunMpmcExactlyOnceCase(1, 4, ItemsPerProducer);
        RunMpmcExactlyOnceCase(4, 4, ItemsPerProducer);
        RunMpmcExactlyOnceCase(8, 8, ItemsPerProducer);
    }

    using TestFunction = void (*)();

    bool RunTest(const char* _name, const char* _description, TestFunction _test)
    {
        const int _failure_count_before = g_failure_count;
        const auto _start_time = std::chrono::steady_clock::now();

        std::cout << "\n[테스트] " << _name << '\n';
        std::cout << "       " << _description << '\n';
        _test();

        const auto _elapsed_time = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _start_time);

        if (_failure_count_before == g_failure_count)
        {
            std::cout << "[통과] " << _name << " (" << _elapsed_time.count() << " ms)\n";
            return true;
        }

        std::cout << "[실패] " << _name << " (" << _elapsed_time.count() << " ms)\n";
        return false;
    }
}

int main()
{
    constexpr int TestCount = 4;
    int _passed_test_count = 0;

    SetConsoleOutputCP(CP_UTF8);

    std::cout << "MPMCQueue 정확성 테스트\n";
    std::cout << "============================================================\n";

    _passed_test_count += RunTest("단일 스레드 경계값 및 FIFO", "용량=4 | 빈 큐/가득 찬 큐 처리 | 링버퍼 순환 후 FIFO 순서", TestSingleThreadBoundaryAndFifo);
    _passed_test_count += RunTest("슬롯 반복 재사용", "용량=2 | 재사용=100000회 | 처리 값=200000개", TestRepeatedSlotReuse);
    _passed_test_count += RunTest("단일 생산자/단일 소비자 순서", "생산자=1 | 소비자=1 | 처리 값=100000개 | FIFO 순서", TestSingleProducerSingleConsumerOrder);
    _passed_test_count += RunTest("다중 생산자/다중 소비자 정확히 한 번 전달", "생산자/소비자=4/1, 1/4, 4/4 | 누락/중복/비정상 값 검사", TestMpmcExactlyOnceDelivery);

    std::cout << "\n============================================================\n";

    if (g_failure_count != 0)
    {
        std::cerr << "결과: 실패 | 통과한 테스트=" << _passed_test_count << '/' << TestCount
                  << " | 실패한 검증=" << g_failure_count << '\n';
        return 1;
    }

    std::cout << "결과: 통과 | 통과한 테스트=" << _passed_test_count << '/' << TestCount << '\n';
    std::cout << "데이터 누락, 중복 소비, 비정상 값, FIFO 순서 오류가 발견되지 않았습니다.\n";
    return 0;
}
