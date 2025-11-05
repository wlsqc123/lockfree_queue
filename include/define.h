#pragma once

#include <cstddef>

// 공통 상수
namespace lfq
{
    // CPU 캐시 라인 크기
    constexpr size_t CACHE_LINE_SIZE = 64;

    // 벤치마크 설정
    constexpr size_t QUEUE_SIZE = 1024;
    constexpr size_t OPERATIONS_PER_THREAD = 10'000'000;
}