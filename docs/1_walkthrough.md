# MPMC 큐 성능 최적화 결과

## 개요
`mpmc_queue`가 `mutex_queue`보다 느린 성능 문제를 해결하기 위해 `Slot` 구조체에 패딩을 추가하여 거짓 공유(False Sharing)를 방지했습니다.

## 변경 사항
### [mpmc_queue.h](file:///f:/lockfree_queue/include/mpmc_queue.h)
- `Pop` 메서드 구현 (벤치마크용)
- `Slot` 구조체에 `alignas(lfq::CACHE_LINE_SIZE)` 추가

```cpp
    // 각 슬롯의 상태를 관리하는 구조체
    // ABA 문제 해결을 위한 generation 카운터
    struct alignas(lfq::CACHE_LINE_SIZE) Slot
    {
        std::atomic<size_t> _generation;
        T _data;
    };
```

## 검증 결과
벤치마크 실행 결과, 최적화 후 `MPMCQueue`가 `MutexQueue`보다 빨라졌습니다.

| 큐 종류 | 소요 시간 (ms) |
| --- | --- |
| MutexQueue | 291ms |
| MPMCQueue (최적화 전) | 421ms |
| **MPMCQueue (최적화 후)** | **251ms** |

### 벤치마크 실행 로그
```
Benchmarking MutexQueue...
Starting MutexQueue...
MutexQueue duration: 291ms
Benchmarking MPMCQueue...
Starting MPMCQueue...
MPMCQueue duration: 251ms
```
