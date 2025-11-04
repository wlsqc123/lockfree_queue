# Lock-Free MPMC Queue Performance Benchmark

## 프로젝트 개요

게임 서버의 잡(Job) 시스템이나 로깅 시스템처럼, 다수의 스레드(Producer)가 작업을 생성하고 다수의 스레드(Consumer)가 이를 처리하는 MPMC(Multi-Producer, Multi-Consumer) 구조에서 중앙 큐의 동기화 방식은 전체 성능에 지대한 영향을 미칩니다.

본 프로젝트는 전통적인 `std::mutex` 기반 큐와 C++11 `std::atomic`을 기반으로 직접 구현한 락프리 큐의 성능을 정량적으로 비교 분석합니다.

## 벤치마크 설계

### 구조
- N개의 생산자 스레드와 M개의 소비자 스레드가 단일 중앙 큐를 공유
- 총 K개의 작업을 큐에 enqueue하고, 소비자들이 이를 dequeue하여 처리

### 측정 지표
- **X축**: 총 스레드 수 (N+M) 또는 총 작업 수 (K)
- **Y축**: 총 작업 처리 시간 (ms)
- **목표**: 모든 작업(K개)이 완료될 때까지 걸리는 총 시간 측정

## 비교 대상 아키텍처

### Before: std::mutex 기반 동기화 큐

**구현**
- `std::queue<Job>` + `std::mutex` + `std::condition_variable`

**동작 방식**
- 큐 접근 시 `std::mutex`를 사용하여 락을 획득한 단 하나의 스레드만 push 또는 pop 수행

**병목 현상**
- 스레드 수가 증가하면 락 획득을 위한 경합(Contention) 극심
- OS의 컨텍스트 스위칭(Context Switching) 비용으로 인한 성능 급격 저하

### After: CustomLockFreeMPMCQueue

**구현**
- C++11 `std::atomic`과 링 버퍼(Ring Buffer) 기반 락프리 큐
- CAS(Compare-And-Swap) 연산을 활용한 락 없는 동작

**핵심 설계**

#### 1. 링 버퍼
- `std::array` 기반 링 버퍼로 new/delete 제거
- **SMR(안전한 메모리 회수)**과 **ABA 문제** 회피

#### 2. Head/Tail 인덱스
- `std::atomic<size_t>` 타입의 head(소비자용)와 tail(생산자용) 인덱스
- 여러 스레드가 `fetch_add(1)` 연산으로 동시 인덱스 선점

#### 3. 추월 문제 해결: Slot Version

단순 head/tail 인덱스만으로는 생산자가 소비자를 추월하거나 소비자가 생산자를 추월하는 것을 방지할 수 없습니다(데이터 덮어쓰기 또는 쓰레기 값 읽기 발생).

**해결 방법**: 모든 슬롯마다 `std::atomic<size_t>` 타입의 버전(Version) 카운터 추가

**생산자 (enqueue)**
1. `tail.fetch_add(1)`로 i번 슬롯 선점
2. i번 슬롯의 version이 "소비자가 읽고 감" 상태가 될 때까지 스핀 대기 (acquire)
3. 데이터를 i번 슬롯에 기록
4. version을 "생산자가 씀" 상태로 발행(Publish) (release)

**소비자 (dequeue)**
1. `head.fetch_add(1)`로 j번 슬롯 선점
2. j번 슬롯의 version이 "생산자가 씀" 상태가 될 때까지 스핀 대기 (acquire)
3. 데이터를 j번 슬롯에서 읽기
4. version을 "소비자가 읽고 감" 상태로 발행(Publish) (release)

#### 4. 메모리 오더링

데이터 손상을 방지하기 위해 메모리 오더링 적용:
- 버전 업데이트: `std::memory_order_release`
- 버전 읽기/대기: `std::memory_order_acquire`
- **리오더링** 방지

## 벤치마크 결과 (예상)

**측정 축**
- X축: 총 스레드 수
- Y축: 총 처리 시간 (낮을수록 우수)

**MutexQueue (빨간색)**
- 스레드 1~4개: 준수한 성능
- 스레드 8개, 16개: 락 경합 비용으로 처리 시간 폭발적 증가

**CustomLockFreeMPMCQueue (파란색)**
- 스레드 수 증가 시에도 `std::atomic` 연산 비용만 지불
- 처리 시간이 완만하게 증가하며 뛰어난 확장성(Scalability) 제공

## 기술 스택

- C++11 이상
- `std::atomic`
- `std::thread`
- `std::chrono`

## 빌드 및 실행

```bash
# 빌드
mkdir build && cd build
cmake ..
make

# 실행
./lfqueue_benchmark
```

## 결론

작성 예정