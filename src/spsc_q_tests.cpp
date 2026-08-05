#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <string_view>
#include <thread>

#include "spsc_queue.h"

namespace
{
    int g_failure_count = 0;

    void Check(bool _condition, std::string_view _message)
    {
        if (_condition)
        {
            return;
        }

        ++g_failure_count;
        std::cerr << "  FAIL: " << _message << '\n';
    }

    template <typename TestFunction>
    void RunTest(std::string_view _name, TestFunction&& _test)
    {
        const int _failure_count_before = g_failure_count;
        const auto _started_at = std::chrono::steady_clock::now();

        std::cout << "[TEST] " << _name << '\n';
        _test();

        const auto _elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - _started_at);

        if (_failure_count_before == g_failure_count)
        {
            std::cout << "[PASS] " << _name << " (" << _elapsed.count() << " ms)\n";
        }
        else
        {
            std::cout << "[FAIL] " << _name << " (" << _elapsed.count() << " ms)\n";
        }
    }

    void TestCapacityValidation()
    {
        bool _zero_rejected = false;
        bool _fifty_rejected = false;

        try
        {
            SPSC_Q _queue(0);
        }
        catch (const std::invalid_argument&)
        {
            _zero_rejected = true;
        }

        try
        {
            SPSC_Q _queue(50);
        }
        catch (const std::invalid_argument&)
        {
            _fifty_rejected = true;
        }

        Check(_zero_rejected, "capacity 0 must be rejected");
        Check(_fifty_rejected, "capacity 50 must be rejected");

        try
        {
            SPSC_Q _minimum_queue(1);
            SPSC_Q _maximum_queue(49);
        }
        catch (const std::exception&)
        {
            Check(false, "capacities 1 and 49 must be accepted");
        }
    }

    void TestBoundaryAndFifo()
    {
        SPSC_Q _queue(3);

        Check(!_queue.pop().has_value(), "a newly created queue must be empty");
        Check(_queue.push(std::numeric_limits<std::int64_t>::min()), "first push failed");
        Check(_queue.push(0), "second push failed");
        Check(_queue.push(std::numeric_limits<std::int64_t>::max()), "third push failed");
        Check(!_queue.push(123), "push must fail when N elements are present");

        const auto _first = _queue.pop();
        const auto _second = _queue.pop();
        const auto _third = _queue.pop();

        Check(_first == std::numeric_limits<std::int64_t>::min(), "first FIFO value mismatch");
        Check(_second == 0, "second FIFO value mismatch");
        Check(_third == std::numeric_limits<std::int64_t>::max(), "third FIFO value mismatch");
        Check(!_queue.pop().has_value(), "queue must be empty after all elements are popped");
    }

    void TestCapacityOneAndWrapAround()
    {
        SPSC_Q _queue(1);

        constexpr std::int64_t ITERATION_COUNT = 200'000;
        for (std::int64_t _expected = 0; _expected < ITERATION_COUNT; ++_expected)
        {
            Check(_queue.push(_expected), "capacity-one push failed");
            Check(!_queue.push(-1), "capacity-one queue accepted a second element");

            const auto _value = _queue.pop();
            Check(_value == _expected, "capacity-one wrap-around value mismatch");
            Check(!_queue.pop().has_value(), "capacity-one queue was not empty after pop");
        }
    }

    void TestNonPowerOfTwoWrapAround()
    {
        SPSC_Q _queue(7);

        constexpr std::int64_t ITERATION_COUNT = 300'000;
        for (std::int64_t _base = 0; _base < ITERATION_COUNT; _base += 7)
        {
            for (std::int64_t _offset = 0; _offset < 7; ++_offset)
            {
                Check(_queue.push(_base + _offset), "push failed during non-power-of-two wrap-around");
            }

            Check(!_queue.push(-1), "full non-power-of-two queue accepted another element");

            for (std::int64_t _offset = 0; _offset < 7; ++_offset)
            {
                const auto _value = _queue.pop();
                Check(_value == _base + _offset, "FIFO mismatch after non-power-of-two wrap-around");
            }
        }
    }

    void TestConcurrentFifoDelivery()
    {
        constexpr std::int64_t ITEM_COUNT = 1'000'000;
        SPSC_Q _queue(7);

        std::atomic<bool> _start{false};
        std::atomic<bool> _mismatch{false};

        std::thread _writer([&_queue, &_start, ITEM_COUNT]()
        {
            while (!_start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for (std::int64_t _value = 0; _value < ITEM_COUNT; ++_value)
            {
                while (!_queue.push(_value))
                {
                    std::this_thread::yield();
                }
            }
        });

        std::thread _reader([&_queue, &_start, &_mismatch, ITEM_COUNT]()
        {
            while (!_start.load(std::memory_order_acquire))
            {
                std::this_thread::yield();
            }

            for (std::int64_t _expected = 0; _expected < ITEM_COUNT; ++_expected)
            {
                std::optional<std::int64_t> _value;
                while (!(_value = _queue.pop()).has_value())
                {
                    std::this_thread::yield();
                }

                if (*_value != _expected)
                {
                    _mismatch.store(true, std::memory_order_relaxed);
                }
            }
        });

        _start.store(true, std::memory_order_release);
        _writer.join();
        _reader.join();

        Check(!_mismatch.load(std::memory_order_relaxed), "concurrent FIFO order mismatch");
        Check(!_queue.pop().has_value(), "queue must be empty after concurrent delivery");
    }
}

int main()
{
    RunTest("capacity validation", TestCapacityValidation);
    RunTest("empty/full boundaries and FIFO", TestBoundaryAndFifo);
    RunTest("capacity one and repeated wrap-around", TestCapacityOneAndWrapAround);
    RunTest("non-power-of-two wrap-around", TestNonPowerOfTwoWrapAround);
    RunTest("one-million-element concurrent FIFO delivery", TestConcurrentFifoDelivery);

    std::cout << "----------------------------------------\n";
    if (g_failure_count != 0)
    {
        std::cerr << "RESULT: FAIL (assertions=" << g_failure_count << ")\n";
        return 1;
    }

    std::cout << "RESULT: PASS\n";
    return 0;
}
