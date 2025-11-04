#include <atomic>
#include <cstddef>
#include "define.h"

// Multi Producer Multi Consumer Lock-Free Queue
// 여러 스레드에서 동시에 push/pop 작업을 수행하는 큐
// CAS(Compare-And-Swap) 연산 사용
template <typename T, size_t Size>
class MPMCQueue
{
public:
    MPMCQueue();
    ~MPMCQueue();

    MPMCQueue(MPMCQueue &&) = delete;
    MPMCQueue(const MPMCQueue &) = delete;
    MPMCQueue &operator=(MPMCQueue &&) = delete;
    MPMCQueue &operator=(const MPMCQueue &) = delete;

    // 여러 스레드에서 안전 호출 가능
    bool Push(const T &item);
    bool Push(T &&item);
    bool Pop(T &item);

    bool IsEmpty() const;
    size_t GetSize() const;
    constexpr size_t GetCapacity() const { return Size; }

private:
    static constexpr size_t CACHE_LINE_SIZE = 64;

    // 각 슬롯의 상태를 관리하는 구조체
    // ABA 문제 해결을 위한 generation 카운터
    struct Slot
    {
        std::atomic<size_t> generation;
        T data;
    };

    alignas(CACHE_LINE_SIZE) Slot p_buffer_[Size];
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> p_head;
    alignas(CACHE_LINE_SIZE) std::atomic<size_t> p_tail;
};

// ============================================================
// 구현
template <typename T, size_t Size>
MPMCQueue<T, Size>::MPMCQueue() : p_head(0), p_tail(0)
{
    static_assert(Size > 0, "Queue size must be greater than 0");
    static_assert((Size & (Size - 1)) == 0, "Queue size must be power of 2");

    // 각 슬롯의 generation 초기화
    for (size_t i = 0; i < Size; ++i)
    {
        p_buffer_[i].generation.store(i, std::memory_order_relaxed);
    }
}

template <typename T, size_t Size>
MPMCQueue<T, Size>::~MPMCQueue()
{
    // 남은 데이터 정리
    T _item;

    while(true == Pop(OUT _item))
    {
        // Pop에서 자동으로 소멸자 호출됨
    }
}

// TODO: push 구현
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::Push(const T &item)
{
    return true;
}

// TODO: push 구현
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::Push(T &&item)
{
    return true;
}

// TODO: pop 구현
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::Pop(T &item)
{
    return true;
}

// TODO: IsEmpty 구현
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::IsEmpty() const
{
    return true;
}

template <typename T, size_t Size>
size_t MPMCQueue<T, Size>::GetSize() const
{
    size_t _head = p_head.load(std::memory_order_acquire);
    size_t _tail = p_tail.load(std::memory_order_acquire);

    if (_head >= _tail)
    {
        return _head - _tail;
    }
    else
    {
        // 언더플로우 발생한 경우 처리
        return 0;
    }
}
