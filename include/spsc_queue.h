#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>

// 검증 환경:
// - 운영체제: Debian GNU/Linux 13 (trixie), Linux 커널 6.12.13, x86_64
// - 컴파일러: g++ 14.2.0
// - 언어 모드: C++17
//
// SPSC_Q는 크기가 제한된 단일 provider/단일 consumer FIFO 큐다.
// provider 스레드 하나만 push()를 호출하고 consumer 스레드 하나만 pop()을
// 호출해야 한다. 객체는 두 스레드보다 오래 유지되어야 한다.
class SPSC_Q
{
public:
    // 외부에 노출되는 용량은 N이며, 0 < N < 50을 만족해야 한다.
    explicit SPSC_Q(std::size_t _capacity)
        : m_ring_size(ValidateCapacityAndGetRingSize(_capacity)),
          m_buffer(std::make_unique<std::int64_t[]>(m_ring_size))
    {
    }

    // provider 스레드에서만 호출하는 함수
    bool push(std::int64_t _elem) noexcept
    {
        // tail은 provider만 갱신하므로 provider 자신의 현재 값은
        // relaxed 순서로 읽어도 된다.
        const std::size_t _tail = m_tail.load(std::memory_order_relaxed);
        const std::size_t _next_tail = NextIndex(_tail);

        // consumer는 소비를 마친 슬롯을 head에 release 저장하여 알린다.
        // acquire는 consumer의 앞선 데이터 읽기가 끝나기 전에 provider가
        // 해당 슬롯을 재사용하지 못하게 한다.
        if (_next_tail == m_head.load(std::memory_order_acquire))
        {
            // 원소가 이미 N개 있으면 false를 반환한다.
            return false;
        }

        m_buffer[_tail] = _elem;

        // 초기화한 원소를 consumer에게 공개한다. 이 tail을 acquire로 읽은
        // consumer는 위의 m_buffer 쓰기 결과를 볼 수 있음이 보장된다.
        m_tail.store(_next_tail, std::memory_order_release);
        return true;
    }

    // consumer 스레드에서 호출하는 연산이다.
    // 원소가 없으면 std::nullopt를 반환한다.
    std::optional<std::int64_t> pop() noexcept
    {
        // head는 consumer만 갱신하므로 consumer 자신의 현재 값은
        // relaxed 순서로 읽어도 된다.
        const std::size_t _head = m_head.load(std::memory_order_relaxed);

        // provider는 초기화한 슬롯을 tail에 release 저장하여 공개한다.
        // acquire는 그에 대응하는 버퍼 쓰기 결과를 보이게 한다.
        if (_head == m_tail.load(std::memory_order_acquire))
        {
            return std::nullopt;
        }

        const std::int64_t _elem = m_buffer[_head];
        const std::size_t _next_head = NextIndex(_head);

        // consumer가 이 슬롯을 더 이상 읽지 않음을 알린다. provider는 이 값을
        // acquire로 확인한 뒤에만 해당 슬롯을 재사용할 수 있다.
        m_head.store(_next_head, std::memory_order_release);
        return _elem;
    }

private:
    static constexpr std::size_t CACHE_LINE_SIZE = 64;
    static constexpr std::size_t MAX_CAPACITY_EXCLUSIVE = 50;

    static std::size_t ValidateCapacityAndGetRingSize(std::size_t _capacity)
    {
        if (_capacity == 0 || _capacity >= MAX_CAPACITY_EXCLUSIVE)
        {
            throw std::invalid_argument("SPSC_Q capacity must satisfy 0 < N < 50");
        }

        // 내부 슬롯 하나를 의도적으로 추가 확보한다. 공유 원소 개수나 슬롯별
        // 세대 번호 없이 head == tail은 비어 있음, next(tail) == head는
        // 가득 참을 나타낼 수 있다.
        return _capacity + 1;
    }

    std::size_t NextIndex(std::size_t _index) const noexcept
    {
        ++_index;
        return (_index == m_ring_size) ? 0 : _index;
    }

    const std::size_t m_ring_size;
    std::unique_ptr<std::int64_t[]> m_buffer;

    // provider 소유 tail과 consumer 소유 head를 서로 다른 캐시 라인에 두어
    // 거짓 공유를 줄인다. 각 스레드는 자신이 소유한 인덱스만 쓴다.
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> m_tail{0};
    alignas(CACHE_LINE_SIZE) std::atomic<std::size_t> m_head{0};
};
