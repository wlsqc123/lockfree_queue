cmake_minimum_required(VERSION 3.15)
project(lockfree_queue VERSION 1.0.0 LANGUAGES CXX)

# C++17 표준 사용
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 빌드 타입이 설정되지 않은 경우 Release로 기본 설정
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 컴파일 옵션
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)

    # Release 빌드 최적화
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(/O2 /Oi /GL)
        add_link_options(/LTCG)
    endif()
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8)

    # Release 빌드 최적화
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# 인클루드 디렉토리
include_directories(${PROJECT_SOURCE_DIR}/include)

# 벤치마크 실행 파일
add_executable(benchmark
    src/benchmark.cpp
    include/define.h
    include/mpmc_queue.h
    include/mutex_queue.h)

# 빌드 정보 출력
message(STATUS "Lockfree Queue Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")