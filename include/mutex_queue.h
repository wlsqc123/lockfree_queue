#pragma once
#include <mutex>
#include <atomic>
#include <cstddef>
#include "define.h"

#pragma warning(push)
#pragma warning(disable: 4324) // 구조체가 alignas로 패딩됨

// Two-Lock Multi Producer Multi Consumer Queue
// 성능 비교를 위한 two-lock 기반 큐
// head와 tail에 각각 별도의 mutex 사용
template <typename T, size_t Size>
class MutexQueue
{
public:
    MutexQueue();
    ~MutexQueue();

    MutexQueue(MutexQueue&&) = delete;
    MutexQueue(const MutexQueue&) = delete;
    MutexQueue& operator=(MutexQueue&&) = delete;
    MutexQueue& operator=(const MutexQueue&) = delete;

    // 여러 스레드에서 안전 호출 가능
    bool Push(const T& _item);
    bool Push(T&& _item);
    bool Pop(T& _item);

    bool IsEmpty() const;
    size_t GetSize() const;
    constexpr size_t GetCapacity() const { return Size; }

private:
    // MPMCQueue와 동일한 메모리 레이아웃을 위한 슬롯 구조체
    struct Slot
    {
        size_t _dummy_generation;  // 메모리 레이아웃 맞추기용 (사용 안함)
        T _data;
    };

    Slot m_buffer[Size];

    alignas(lfq::CACHE_LINE_SIZE) std::mutex m_tail_mutex;
    std::atomic<size_t> m_tail;

    alignas(lfq::CACHE_LINE_SIZE) std::mutex m_head_mutex;
    std::atomic<size_t> m_head;
};

// ============================================================
// 구현
template <typename T, size_t Size>
MutexQueue<T, Size>::MutexQueue() : m_tail(0), m_head(0)
{
    static_assert(Size > 0, "큐 크기는 0보다 커야 합니다");
    static_assert((Size & (Size - 1)) == 0, "큐 크기는 2의 제곱이어야 합니다");
}

template <typename T, size_t Size>
MutexQueue<T, Size>::~MutexQueue()
{
    // 남은 데이터 정리
    T _item;
    while (Pop(_item))
    {
        // Pop에서 자동으로 소멸자 호출됨
    }
}

template <typename T, size_t Size>
bool MutexQueue<T, Size>::Push(const T& item)
{
    std::lock_guard<std::mutex> _lock(m_tail_mutex);

    size_t _tail = m_tail.load(std::memory_order_relaxed);
    size_t _head = m_head.load(std::memory_order_acquire);

    if (_tail - _head >= Size)
    {
        return false;
    }

    size_t _index = _tail & (Size - 1);
    m_buffer[_index]._data = item;

    m_tail.store(_tail + 1, std::memory_order_release);

    return true;
}

template <typename T, size_t Size>
bool MutexQueue<T, Size>::Push(T&& item)
{
    std::lock_guard<std::mutex> _lock(m_tail_mutex);

    size_t _tail = m_tail.load(std::memory_order_relaxed);
    size_t _head = m_head.load(std::memory_order_acquire);

    if (_tail - _head >= Size)
    {
        return false;
    }

    size_t _index = _tail & (Size - 1);
    m_buffer[_index]._data = std::move(item);

    m_tail.store(_tail + 1, std::memory_order_release);

    return true;
}

template <typename T, size_t Size>
bool MutexQueue<T, Size>::Pop(T& item)
{
    std::lock_guard<std::mutex> _lock(m_head_mutex);

    size_t _head = m_head.load(std::memory_order_relaxed);
    size_t _tail = m_tail.load(std::memory_order_acquire);

    if (_head == _tail)
    {
        return false;
    }

    size_t _index = _head & (Size - 1);
    item = std::move(m_buffer[_index]._data);

    m_head.store(_head + 1, std::memory_order_release);

    return true;
}

template <typename T, size_t Size>
bool MutexQueue<T, Size>::IsEmpty() const
{
    size_t _head = m_head.load(std::memory_order_acquire);
    size_t _tail = m_tail.load(std::memory_order_acquire);
    return _head == _tail;
}

template <typename T, size_t Size>
size_t MutexQueue<T, Size>::GetSize() const
{
    size_t _head = m_head.load(std::memory_order_acquire);
    size_t _tail = m_tail.load(std::memory_order_acquire);

    if (_tail >= _head)
    {
        return _tail - _head;
    }
    else
    {
        return 0;
    }
}

#pragma warning(pop)
