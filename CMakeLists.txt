cmake_minimum_required(VERSION 3.15)
project(lockfree_queue VERSION 1.0.0 LANGUAGES CXX)

# C++17 표준 사용
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# 컴파일 옵션
if(MSVC)
    add_compile_options(/W4 /WX /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -finput-charset=UTF-8 -fexec-charset=UTF-8)
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -march=native)
    endif()
endif()

# 인클루드 디렉토리
include_directories(${PROJECT_SOURCE_DIR}/include)

# 메인 실행 파일
add_executable(main
    src/main.cpp
    include/define.h)

# 벤치마크 실행 파일
add_executable(benchmark
    src/benchmark.cpp
    include/define.h
    include/mpmc_queue.h
    include/mutex_queue.h)

# 테스트 옵션
option(BUILD_TESTS "Build test programs" ON)

if(BUILD_TESTS)
    add_executable(test_mpmc
        tests/test_mpmc.cpp
        include/define.h
        include/mpmc_queue.h)

    enable_testing()
    add_test(NAME MPMCTest COMMAND test_mpmc)
endif()

# 빌드 정보 출력
message(STATUS "Lockfree Queue Configuration:")
message(STATUS "  C++ Standard: ${CMAKE_CXX_STANDARD}")
message(STATUS "  Build Type: ${CMAKE_BUILD_TYPE}")
message(STATUS "  Build Tests: ${BUILD_TESTS}")