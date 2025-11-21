#include <atomic>
#include <cstddef>
#include "define.h"

#pragma warning(push)
#pragma warning(disable: 4324)

// Multi Producer Multi Consumer Lock-Free Queue
// 여러 스레드에서 동시에 push/pop 작업을 수행하는 큐
// CAS(Compare-And-Swap) 연산 사용
template <typename T, size_t Size>
class MPMCQueue
{
public:
    MPMCQueue();
    ~MPMCQueue();

    MPMCQueue(MPMCQueue&&) = delete;
    MPMCQueue(const MPMCQueue&) = delete;
    MPMCQueue& operator=(MPMCQueue&&) = delete;
    MPMCQueue& operator=(const MPMCQueue&) = delete;

    // 여러 스레드에서 안전 호출 가능
    bool Push(const T& _item);
    bool Push(T&& _item);
    bool Pop(T& _item);

    bool IsEmpty() const;
    size_t GetSize() const;
    constexpr size_t GetCapacity() const { return Size; }

private:
    // 각 슬롯의 상태를 관리하는 구조체
    // ABA 문제 해결을 위한 generation 카운터
    struct alignas(lfq::CACHE_LINE_SIZE) Slot
    {
        std::atomic<size_t> _generation;
        T _data;
    };

    Slot m_buffer[Size];

    // Head: 큐의 앞부분 (Pop/읽기)
    // Tail: 큐의 뒷부분 (Push/쓰기)
    alignas(lfq::CACHE_LINE_SIZE) std::atomic<size_t> m_head; // 읽기 인덱스
    alignas(lfq::CACHE_LINE_SIZE) std::atomic<size_t> m_tail; // 쓰기 인덱스
};

// ============================================================
// 구현
template <typename T, size_t Size>
MPMCQueue<T, Size>::MPMCQueue() : m_head(0), m_tail(0)
{
    static_assert(Size > 0, "MPMCQueue - 큐 사이즈가 0보다 커야 함");
    static_assert((Size & (Size - 1)) == 0, "MPMCQueue - 큐 사이즈가 2의 제곱이어야 함");

    // 각 슬롯의 generation 초기화
    for (size_t i = 0; i < Size; ++i)
    {
        m_buffer[i]._generation.store(i, std::memory_order_relaxed);
    }
}

template <typename T, size_t Size>
MPMCQueue<T, Size>::~MPMCQueue()
{
    // 남은 데이터 정리
    T _item;

    while(true == Pop(_item))
    {
        // Pop에서 자동으로 소멸자 호출됨
    }
}

// lvalue 참조 버전 Push 구현 (Tail에 추가)
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::Push(const T& _item)
{
    size_t _tail = m_tail.load(std::memory_order_relaxed); // Write Index

    while (true)
    {
        // 현재 tail 위치의 슬롯 계산
        size_t _idx = _tail & (Size - 1);
        Slot& _slot = m_buffer[_idx];

        // generation 읽기
        size_t _generation = _slot._generation.load(std::memory_order_acquire);

        // 이 슬롯에 쓸 수 있는지 확인
        if (_generation == _tail)
        {
            // tail을 증가시켜 이 슬롯을 예약
            if (m_tail.compare_exchange_weak(_tail, _tail + 1, std::memory_order_relaxed))
            {
                // 데이터 복사
                _slot._data = _item;

                // generation을 증가시켜 Pop이 읽을 수 있게 함
                _slot._generation.store(_tail + 1, std::memory_order_release);
                return true;
            }
        }
        // 아직 Pop이 데이터를 가져가지 않음 (큐가 가득 참)
        else if (_generation < _tail)
        {
            // head(Read Index)와 비교하여 정말 가득 찼는지 확인
            size_t _head = m_head.load(std::memory_order_acquire);

            if (_tail >= _head + Size)
            {
                // 큐가 가득 참
                return false; 
            }

            // 다른 스레드가 Pop을 진행 중일 수 있으므로 재시도
            _tail = m_tail.load(std::memory_order_relaxed);
        }
        // 다른 스레드에서 push 중인 경우 (generation > tail)
        else
        {
            // tail을 다시 읽어서 재시도
            _tail = m_tail.load(std::memory_order_relaxed);
        }
    }
}

// (rvalue 참조 버전) Push 구현 (Tail에 추가)
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::Push(T&& _item)
{
    size_t _tail = m_tail.load(std::memory_order_relaxed); // Write Index

    while (true)
    {
        // 현재 tail 위치의 슬롯 계산
        size_t _idx = _tail & (Size - 1);
        Slot& _slot = m_buffer[_idx];

        // generation 읽기
        size_t _generation = _slot._generation.load(std::memory_order_acquire);

        // 이 슬롯에 쓸 수 있는지 확인
        // generation이 tail과 같으면 쓸 수 있음
        if (_generation == _tail)
        {
            // tail을 증가시켜 이 슬롯을 예약
            if (m_tail.compare_exchange_weak(_tail, _tail + 1, std::memory_order_relaxed))
            {
                // 데이터 이동 (move semantics)
                _slot._data = std::move(_item);

                // generation을 증가시켜 Pop이 읽을 수 있게 함
                _slot._generation.store(_tail + 1, std::memory_order_release);
                return true;
            }
        }
        else if (_generation < _tail)
        {
            // 아직 Pop이 데이터를 가져가지 않음 (큐가 가득 참)
            // head(Read Index)와 비교하여 정말 가득 찼는지 확인
            size_t _head = m_head.load(std::memory_order_acquire);

            if (_tail >= _head + Size)
            {
                return false; // 큐가 가득 참
            }

            // 다른 스레드가 Pop을 진행 중일 수 있으므로 재시도
            _tail = m_tail.load(std::memory_order_relaxed);
        }
        else
        {
            // generation > tail: 다른 스레드가 이미 이 위치에 Push 진행 중
            // tail을 다시 읽어서 재시도
            _tail = m_tail.load(std::memory_order_relaxed);
        }
    }
}

// Pop 구현 (Head에서 제거)
template <typename T, size_t Size>
bool MPMCQueue<T, Size>::Pop(T& _item)
{
    size_t _head = m_head.load(std::memory_order_relaxed); // Read Index

    while (true)
    {
        size_t _idx = _head & (Size - 1);
        Slot& _slot = m_buffer[_idx];

        size_t _generation = _slot._generation.load(std::memory_order_acquire);

        // 현재 head에 해당하는 데이터가 슬롯에 있는지 확인
        if (_generation == _head + 1)
        {
            if (m_head.compare_exchange_weak(_head, _head + 1, std::memory_order_relaxed))
            {
                _item = std::move(_slot._data);
                
                // 다음 Push가 이 슬롯을 사용할 수 있도록 generation 업데이트
                // 다음 Push는 generation == head + Size를 기대함 (해당 바퀴의 새로운 tail)
                // 현재 head가 X일 때, X를 소비함. 다음 번에 이 슬롯이 사용될 때는 인덱스 X + Size가 됨.
                _slot._generation.store(_head + Size, std::memory_order_release);
                return true;
            }
        }
        else if (_generation < _head + 1)
        {
            // 큐가 비었거나 Push가 진행 중임
            size_t _tail = m_tail.load(std::memory_order_acquire); // Write Index
            
            if (_head >= _tail)
            {
                return false; // Empty
            }
            
            // Retry
            _head = m_head.load(std::memory_order_relaxed);
        }
        else
        {
            // 로직이 정확하다면 정상 흐름에서는 발생하지 않아야 하지만, 재시도
            _head = m_head.load(std::memory_order_relaxed);
        }
    }
}

template <typename T, size_t Size>
bool MPMCQueue<T, Size>::IsEmpty() const
{
    size_t _head = m_head.load(std::memory_order_acquire);
    size_t _tail = m_tail.load(std::memory_order_acquire);
    return _tail <= _head;
}

template <typename T, size_t Size>
size_t MPMCQueue<T, Size>::GetSize() const
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
