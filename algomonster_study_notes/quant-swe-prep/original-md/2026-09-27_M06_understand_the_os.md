# Module 6 — Understand The OS (운영체제를 이해하기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 6 (레슨 6개) · 한국어 개념 정리 노트
> 목표: **"커널은 레이턴시가 숨는 곳."** 페이지 폴트·컨텍스트 스위치·시스템 콜·IPC 복사·malloc slow path가 전부 *원치 않는 경계 횡단*이라는 걸 보고, hot path에서 커널을 빼는 설계를 설명하기.

## 한눈에 보기 (5분 복습용)

| 커널 비용 | 크기 감각 | 언제 터지나 | hot path 처방 |
|---|---|---|---|
| TLB miss | 4단계 page-table walk (의존 메모리 접근 4번) | 흩어진 접근이 많은 페이지를 건드릴 때 | 적은 페이지에 작업 집합, **huge page** |
| Minor page fault | ~1 µs, 페이지당 1회 | malloc/`reserve` 후 **첫 터치** | 시작 시 **pre-fault**, `mlock` |
| Major page fault | **ms** (디스크 I/O) | 스왑/mmap 파일 | 절대 금지 (`mlockall`) |
| Mode switch (syscall) | 함수 호출보다 훨씬 비쌈 | 모든 syscall | hot path syscall 0 |
| Context switch | 직접 1–2 µs + **cold cache/TLB** (진짜 비용) | 블록(락·CV·I/O·sleep), 선점 | 핀닝 + 격리 + RT 정책 + spin |
| IPC 메시지 | syscall 양방향 + 커널 경유 복사 | pipe/socket | **공유 메모리 + SPSC ring** |
| malloc slow path | arena 락 → split → **brk/mmap + fault** | bin이 비었을 때 | 시작 시 pool |
| 네트워크 수신 | 인터럽트 + 스택 + 복사 2번 + `recv` | 매 패킷 | **kernel bypass** (DPDK) |

**최저 레이턴시 스레드의 한 문장:** *격리된 코어에 핀닝 + real-time 정책 + bypass된 NIC를 busy-poll + 시작 시 pre-fault·lock된 메모리 + malloc 대신 pool* → **폴트·스위치·syscall·할당 0.**

---

## 1. Virtual Memory, Pages, TLB

**핵심:** 코드가 만지는 모든 주소(`0x7ffd...`)는 **가상 주소**. 로드가 Module 5의 캐시 계층을 내려가기도 전에 **물리 주소로 번역**돼야 한다.

### 프로세스마다 자기 주소
- 비유: 부서마다 "1호실"이 있음 → "회계부 1호실"로 번역해야 실제 위치.
- 프로세스마다 0부터 시작하는 **private 주소 공간**. 두 프로세스가 `0x1000`을 써도 충돌 없음. OS가 프로세스별 매핑을 세팅, CPU가 매 접근마다 강제 → **격리** + 물리 RAM은 조각나 있어도 프로그램엔 깨끗한 연속 공간.

### 페이지
- 바이트 단위 추적은 메모리만큼 큰 맵 → **고정 크기 페이지** 단위 (x86-64 거의 항상 **4KB**).
- 가상 주소 = **상위 비트: 페이지 번호**(번역됨) + **하위 12비트: 페이지 내 오프셋**(그대로 통과, 2¹²=4096).
- 번역 1회 = 그 페이지 4096바이트 전체 커버.

### Page table + MMU
- 가상 페이지 → 물리 페이지 맵 = **page table** (프로세스당 1개, DRAM에 있음). **MMU**가 매 접근마다 참조.
- 개념: `phys = page_table[vpage] * 4096 + offset`. 실제는 평평한 배열이 아니라 **4단계 트리** (모든 페이지 번호용 배열은 거대).
- **Page-table walk** = 루트부터 4단계 포인터 따라가기 = 실제 로드 전에 **의존적 메모리 접근 4번**.

### TLB (Translation Lookaside Buffer)
- 최근 번역을 캐싱하는 작고 매우 빠른 온칩 캐시. 비유: 최근 방문자를 기억하는 도어맨.
- **hit ≈ 1 cycle**, **miss = page-table walk**.
- 작다: 수십~수천 엔트리, 엔트리당 1페이지 → **reach 제한**. 64 엔트리 × 4KB = **256KB**만 동시 커버. 수천 페이지에 흩어지면 walk 연발.

### 캐시가 두 개다
- 매 로드마다 독립적인 두 질문: ① 데이터가 캐시에 있나(L1/L2/L3) ② **번역이 TLB에 있나**.
- **L1 데이터 hit인데도 느릴 수 있다** — 번역이 먼저, 데이터 캐시가 필요로 하는 물리 주소는 TLB/walk가 만들어야 존재.
- vector 스캔: 연속 페이지 → 4KB 페이지 하나에 32B 업데이트 **128개** → 몇 개 TLB 엔트리로 긴 run 커버. list: 노드가 서로 다른 페이지 → hop마다 **데이터 미스 + TLB 미스**. 같은 locality 교훈을 두 번째 메커니즘이 또 벌함.

- ✅ 체크포인트: 같은 수의 레코드, list가 TLB 미스가 훨씬 많은 이유 → 노드가 많은 페이지에 흩어져 hop마다 새 번역 필요. vector는 몇 엔트리로 긴 run 커버. (TLB는 데이터를 캐싱하지 않음)
- 🎤 "L1 hit인데 느릴 수 있나?" → 번역이 fetch보다 먼저. TLB miss면 page-table walk를 먼저 지불.

---

## 2. Page Faults & Latency

**핵심:** `malloc`은 유효한 **가상 범위**를 즉시 돌려주지만 **물리 메모리는 아직 없다.** 물리 페이지는 **첫 터치** 때 생기고, 첫 터치마다 커널 행.

### Demand paging (의도적 게으름)
- 요청 시 OS는 "이 가상 범위 유효"만 기록. 물리 페이지 찾기·0 채우기·page table 갱신 전부 **첫 읽기/쓰기까지 연기**.
- 비유: 호텔 예약은 즉시(=malloc), 처음 방에 들어갈 때 하우스키핑이 침대 정리(=첫 터치), 그 기다림 = **페이지 폴트**.

### 페이지 폴트가 하는 일
- MMU가 번역 불가 → **fault** → 명령 중단 + 커널 trap. 커널: ① 빈 물리 페이지 찾기 ② **0으로 채우기**(다른 프로세스 잔여 데이터 유출 방지) ③ page table에 매핑 설치 ④ 복귀 후 **폴트난 명령 재실행**.
- **Minor fault:** 필요한 게 이미 메모리에 있음, 연결만. **~1 µs** (일 자체보다 커널 왕복이 지배). **페이지당 딱 1회** — 이후엔 일반 번역 로드.
- **Major fault:** 내용이 디스크에 (스왑 파일, mmap된 파일) → 실제 디스크 I/O 대기 → **ms**, minor의 수천 배. 트레이딩 hot path에서 한 번이면 재앙, minor도 원치 않는 지터.

### 스파이크지 정상 비용이 아니다
- 페이지당 1회 이벤트 → **평균엔 안 보이고 꼬리에 보임.** 새 메모리를 건드리는 특정 iteration만 지불. (Module 1 할당 긴 꼬리와 같은 모양)
- 그래서 벤치마크 워밍업: cold 반복은 폴트 + cold cache + cold 예측기. cold 실행을 재면 **내 코드가 아니라 OS의 페이지 매핑을 측정**.
- 🪤 **함정:** 시작 시 전부 `reserve` → 루프에 할당 없음 → 그런데 첫 수백 업데이트가 스파이크. `reserve`는 **가상 범위만** 확보, 물리 페이지는 여전히 lazy → 각 페이지 첫 쓰기가 폴트. **할당 제거는 필요조건이지 충분조건이 아니다 — 페이지를 실제로 만들어야 함.**

### Hot path에서 폴트 빼기
1. **Pre-fault:** 할당 후 장 시작 전, 모든 페이지를 한 번씩 터치.
2. **상주 유지:** `mlock`/`mlockall` → OS가 페이지를 회수해 다시 폴트나게 하는 일 방지.
3. **Huge page (2MB):** 같은 버퍼에 페이지 **512배 적음** → 폴트 512배 적음 + TLB 엔트리 하나가 2MB 커버 (번역 압박 완화).
```cpp
// 시작 시, 할당 후 hot path 전
for (std::size_t i = 0; i < bytes; i += page_size)
    buffer[i] = 0;   // 쌀 때 모든 페이지를 폴트시켜 둠
```
- ✅ 체크포인트: 전부 reserve했는데 첫 수백 AAPL 업데이트 스파이크 → `reserve`는 가상 버퍼만, demand paging이 물리 페이지를 lazy 매핑 → 페이지별 첫 쓰기 폴트. 시작 시 pre-fault로 제거.
- 🎤 "할당은 빨랐는데 버퍼 첫 터치가 느린 이유?" → demand paging. 비용이 할당에서 첫 사용으로 이동.

---

## 3. Context Switching & Scheduler

**핵심:** Module 4의 "커널에 park", "커널 경유 wake"의 실체. 진짜 비용은 인용된 숫자보다 **크고 교활하다.**

### 스케줄러
- 스레드 수백 개, 코어 몇 개. 준비된 스레드에 **time slice**(수 ms) 할당 → 소진되거나 스레드가 대기하면 코어를 회수해 다른 스레드에. = **context switch**.

### 저장·복원하는 것
- 스레드의 살아 있는 상태 = **레지스터** (범용 레지스터, program counter, stack pointer 등).
- A→B: ① A의 레지스터를 A의 저장 컨텍스트에 ② B의 컨텍스트를 레지스터로 ③ 재개.
- 직접 비용 = 레지스터 저장/복원 + 커널 bookkeeping ≈ **1–2 µs**. 이게 전부면 싸다. **전부가 아니다.**

### 진짜 비용 = cold cache
- 비유: 셰프(코어)가 레시피 카드(레지스터)만 바꾸는 건 빠르지만, 조리대(캐시)엔 이전 레시피 재료가 가득 → 치우고 창고에서 다시 가져와야 함. 되돌아가면 첫 레시피 준비물도 사라짐.
- B가 시작할 때 L1·L2·L3 일부는 A의 데이터, **TLB는 A의 번역** → B는 처음 수천 명령을 cold로: DRAM 미스 + TLB 미스(page walk) 폭탄.
- B의 활동이 A의 데이터를 축출 → A가 돌아와도 cold. **스위치 한 번이 두 스레드 모두 오염.** 간접 비용이 스톱워치로 잰 레지스터 교환을 압도.

### 자발적 vs 비자발적
| | 원인 | 예 |
|---|---|---|
| **Involuntary** | 강제: time slice 만료, 더 높은 우선순위 스레드 준비 → 선점 | 스케줄러 선점 |
| **Voluntary** | 스레드가 스스로 대기 | 못 잡는 mutex, CV wait, sleep, 블로킹 I/O |
- Module 4의 경합 mutex "park" = voluntary switch → park/wake뿐 아니라 **cold restart까지** 지불.
- **Mode switch** (syscall, 스레드 교체 없음): user→kernel→user 보호 경계 횡단. 풀 스위치보단 싸지만 일반 함수 호출보다 훨씬 비쌈. **context switch = mode switch + 레지스터 교환 + 캐시 오염.**

### Hot path는 CPU를 떠나지 않는다
- AAPL 피드를 비우는 스레드는 한 코어에서 hot cache·hot TLB로 풀스피드를 원함 → 스위치가 정확히 그걸 파괴.
- → **전용 코어에 핀닝**(스케줄러가 안 옮김) + **절대 블록 안 함**(스스로 코어를 내주지 않음).
- SPSC ring이 sleep 대신 **spin**하는 이유: spin은 코어를 따뜻하게 유지, CV는 park → 다른 스레드 유입 → 깰 때 cold restart. 코어를 태우는 게 떠나는 비용보다 쌈.
- 🎤 면접 무브: **블로킹은 처리량의 적일 뿐 아니라 tail latency의 적.** 스레드를 재울 수 있는 모든 호출(락, CV, 블로킹 read, 예상 못한 페이지 폴트)은 스케줄러가 따뜻한 코어를 남에게 줄 기회.

- ✅ 체크포인트: 50µs 동안 코어에서 밀려났다 돌아온 스레드가 스위치 완료 후에도 한동안 느림 → 부재 중 다른 스레드가 **캐시 데이터와 TLB 번역을 축출** → cold 재개, 작업 집합 리필 미스 폭탄. (레지스터 복원이 수십 µs 걸리는 게 아님)

---

## 4. Processes, Threads, IPC

**핵심:** **프로세스 = 주소 공간**(자기 page table), **스레드 = 그 안에서 스케줄러가 코어에 올리는 실행 흐름**.
```
한 프로세스, 두 스레드: 주소 공간 1개 (page table 1개, &g가 같음)
  Thread1 ─┐
           ├─► [ heap | globals | code ]
  Thread2 ─┘
두 프로세스: 주소 공간 2개 (기본적으로 공유 없음)
  ProcA ──► [ heap | globals | code ]  page table A
  ProcB ──► [ heap | globals | code ]  page table B, 자기 &g
```
- 스레드가 공유 카운터에서 race할 수 있었던 이유 = 주소 공간 공유 (Module 4의 메커니즘). 별도 프로세스는 race할 공유 공간이 없음.

### 공유 vs 격리
| | 스레드 | 프로세스 |
|---|---|---|
| 생성 비용 | 쌈 (스택 + 스케줄러 엔트리) | 무거움 (새 주소 공간, 전통적으로 `fork` = page table 복제 + **copy-on-write**) |
| 공유 | 가장 빠름, 동기화는 전부 내 책임 | 포인터를 그냥 못 넘김 |
| 장애 격리 | rogue 스레드가 오더북을 덮어쓸 수 있음 | 리스크 프로세스가 크래시해도 피드 핸들러 무사 (**하드 월**) |

### IPC 두 계열
- **공유 메모리:** 두 프로세스가 **같은 물리 페이지**를 각자 주소 공간에 매핑(가상 주소는 달라도 됨). 한쪽 쓰기가 즉시 보임, **데이터 경로에 복사·커널 없음**. 비유: 둘이 같이 서 있는 화이트보드. 단, Module 4 세계로 복귀 → 공유 가변 상태 → **동기화 필요** (공유 영역 안에 atomic 인덱스 / lock-free ring). **공유 메모리는 복사를 없앨 뿐 조정은 없애지 않는다.**
- **메시지 패싱:** pipe, 로컬 소켓, 큐. 비유: 우체국 — 송신자가 커널에 넘기고 커널이 버퍼로 복사 → 수신자에게 복사. 안전하지만 **메시지마다 syscall 양방향 + 커널 경유 복사 최소 1번** (mode switch를 메시지마다 청구).

### Hot path엔 어느 쪽?
- 모든 마켓 업데이트가 지나가는 경로엔 메시지 패싱은 너무 비쌈 → **공유 메모리 영역 안에 Module 4의 SPSC ring**. 생산자 프로세스가 쓰고 소비자 프로세스가 읽음, 동기화는 원자 인덱스 한 쌍 (이제 두 스레드가 아니라 **두 주소 공간**을 조정). 커널 0, 아이템당 복사 0.
- 메시지 패싱은 hot path 밖에서: 시작 핸드셰이크, 설정, 제어 명령, 로깅.
- 다음 모듈로의 다리: **네트워크 = 메시지 패싱의 극한** (같은 커널 경유 복사 + 선과 다른 머신).

- ✅ 체크포인트: 같은 머신 두 프로세스, 공유 메모리 ring vs 로컬 소켓 → 공유 메모리는 같은 물리 페이지를 양쪽 주소 공간에 매핑해 복사·커널 횡단 없음. 소켓은 메시지마다 커널 경유 복사 + 양방향 syscall. (로컬 소켓은 네트워크로 안 나감, 공유 메모리도 동기화는 필요)

---

## 5. malloc Internals & Hot Path

**핵심:** Module 1의 스케치(fast path = free list pop, slow path = 락 or OS 요청)의 블랙박스 세 개를 연다: OS가 실제로 뭘 하나, 왜 락이 있나, "메모리 더 달라"의 비용.

### Chunk = 내 바이트 + bookkeeping
```
malloc(48) 실제 레이아웃:
[ header: size + flags ][ 받은 48바이트 ........ ]
^ chunk 시작            ^ malloc이 돌려준 포인터
free(p)는 p 바로 앞 header를 읽어 크기를 복원
```
- `free(p)`가 크기 인자 없이 동작하는 이유 = header.
- 모든 할당에 요청 안 한 오버헤드: header + 정렬 경계로 반올림. 수백만 개의 작은 할당 = header만으로 실제 메모리 낭비 → 레코드를 한 버퍼에 몰아넣으라는 또 하나의 이유.

### Bin
- 해제된 chunk는 OS에 반환 안 됨 → 크기별 **bin**(free list)에 분류 → 같은 크기 다음 요청에 즉시 응답.
- fast path: 해당 크기 bin의 head pop / `free`는 push. → 해제 직후 같은 크기 재요청 ≈ 공짜 (방금 반납한 그 chunk가 bin 맨 위).

### 스레드 여럿 → arena
- 힙은 프로세스 전체 공유 → 동시 malloc은 bin을 오염 → 직렬화 필요. 전역 락 하나 = 모든 할당이 Module 4의 경합 락 → 확장 불가.
- 해법 = 계산대를 더 열기: 힙을 여러 **arena**(자기 bin + 자기 락을 가진 최상위 힙 영역)로 분할, 대략 스레드당 하나 → 보통 같은 락을 안 만짐.
- 그 위에 **thread cache:** 스레드별 최근 해제 chunk 보관함, **락 불필요** = 반복 같은 크기 할당의 진짜 fast path. glibc **tcache**, jemalloc·tcmalloc은 이 아이디어 중심의 allocator.
- ⚠️ 용어 주의: 여기 "arena" = malloc 내부의 스레드별 힙 영역. Module 3 "arena" = 내가 만들고 소유하는 `monotonic_buffer_resource`. 둘 다 큰 영역에서 작은 할당을 깎아서 이름이 같을 뿐 **별개 메커니즘**.

### Slow path: `brk` / `mmap`
- bin이 비고 충분히 큰 free chunk도 없으면 → 그제야 커널: 일반 힙 확장은 **`brk`**(힙 끝을 바깥으로), 큰 요청은 **`mmap`**(새 영역을 주소 공간에 직접 매핑).
- 비용 = **syscall(mode switch) + 새 페이지 첫 터치마다 page fault** (OS가 lazy 매핑). = Module 1의 "OS에 메모리 요청" 명세서.
- 해제는 거의 되돌리지 않음: 작은 chunk는 재사용 위해 bin에 → 장기 프로세스는 **최대 사용량을 붙잡고 있는 경향**. 큰 `mmap` 블록만 보통 `munmap`으로 반환.

### 비용 사다리 (빠름 → 느림)
| 단계 | 일 | 비용 |
|---|---|---|
| 1. thread cache hit | chunk pop, 락 없음 | 수십 ns |
| 2. arena bin hit | arena 락 + pop | + 락 비용 |
| 3. 큰 chunk split | free chunk를 둘로 + 탐색 | + 탐색 |
| 4. `brk`/`mmap` | 메모리 더 요청 syscall | + **page fault들** (µs) |
- **`new Order()`만 봐서는 어느 단에 떨어질지 모른다.**

### Pool이 존재하는 이유
- Module 3의 pool/arena = 시작 시 큰 블록 하나(`mmap` + 폴트를 워밍업 때 한 번 지불) → 이후 직접 분배 (bump pointer 또는 고정 크기 슬롯 free list). header·arena 락·syscall·폴트 **전부 없음.**
- 범용 allocator = 예측 불가 케이스용 런타임 기계. hot path에선 **절대 느린 단으로 떨어질 수 없는 특수 allocator**로 교체.
- 🎤 면접 본능: "malloc은 **평균은 빠르지만 꼬리는 무한정**" + 이유를 이름으로(arena 락, split, brk/mmap syscall, page fault) + "시작 시 pool을 잡아 재사용해 hot path에서 뺀다".

- ✅ 체크포인트: 같은 크기 free 직후 `malloc(48)` 수백만 번, 대부분 수십 ns인데 가끔 µs 스파이크 → 보통은 thread cache/arena bin에서 커널 없이 pop, 가끔 bin이 비어 arena 락·split·`brk`/`mmap`(syscall + 폴트). (`free`가 매번 OS에 반환하는 게 아님)

---

## 6. Kernel Bypass & Real-Time Scheduling (Intro)

**핵심 질문:** hot path가 **아예 커널에 들어가지 않으면?** 두 절반: ① 데이터 경로에서 커널 제거 ② 스케줄러가 절대 끼어들지 못하게.

### 네트워크 경로에서 커널 빼기
- **일반 경로:** NIC 수신 → **인터럽트** → 커널 네트워크 스택이 헤더 처리 → payload를 소켓 버퍼로 **복사** → 스레드가 `recv` syscall → 커널 횡단 + 내 버퍼로 **복사** → 이제야 업데이트를 봄. 패킷마다 커널 횡단 여러 번 + 복사 최소 2번.
- **Kernel bypass (예: DPDK):** NIC가 수신 패킷을 **DMA로 내 프로세스 메모리의 ring buffer에 직접** 씀 → 스레드가 busy loop로 ring을 읽음. 인터럽트·커널 스택·소켓 버퍼·`recv`·복사 **전부 없음**.
- = Module 4의 SPSC ring + spin-wait, **NIC가 생산자 역할**. 패킷이 userspace에 도착하고 제자리에서 읽음.

### 스케줄러를 코어에서 빼기 (쌓아 올리는 3단)
1. **CPU affinity (핀닝):** 스케줄러가 스레드를 다른 코어로 옮기지 못하게. 단, **다른 일이 같은 코어에 오는 건 못 막음.**
2. **코어 격리 (`isolcpus` 등):** OS의 housekeeping 스레드와 인터럽트를 그 코어에서 완전히 배제 → 내 스레드만 그 코어에서 돔.
3. **Real-time 정책 (`SCHED_FIFO` 등):** 스스로 양보할 때까지 실행, 일반 스레드가 선점 불가.
- + huge page (2MB): 큰 버퍼의 TLB 미스·폴트 수 동시 감소.

### 정직한 단서: 마지막에 꺼내는 도구
| 도구 | 대가 |
|---|---|
| bypass NIC busy-poll | 패킷이 없어도 **코어 하나 영원히 100%** |
| 격리 + RT 스레드 | 일반 스케줄러에 안 보임 → spin 버그가 코어를 구제 불능으로 잠글 수 있음 |
| kernel bypass | 커널 네트워크 스택이 해주던 것 포기 → **프로토콜 처리를 내가 소유**, 특정 하드웨어 + 권한 필요 |
- **커널이 실제 병목임을 측정하기 전엔 하지 마라** (= Module 8).
- 면접 목표는 **설정이 아니라 인식:** bypass가 뭐고 왜 있나(카드가 userspace에 써서 인터럽트·복사·syscall 제거), 핀닝+격리+RT가 합쳐져 선점을 막는다. DPDK를 손으로 설정해봤을 필요는 없고, 이 도구들이 **어떤 비용에서 빠져나오려고 돈을 내는지** 이해하면 됨.

- ✅ 체크포인트: bypass로 `recv` 없애고 폴링 스레드를 핀닝했는데 그 코어에 가끔 context-switch 스파이크 → 핀닝은 **내 스레드를 코어에 묶을 뿐 다른 일을 막지 못함** → 코어 **격리** + **RT 정책** 필요. (busy-poll 자체가 스위치를 유발하는 게 아님)

---

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Virtual address space | 프로세스별 private 주소 범위 |
| Page / offset | 4KB 번역 단위 / 하위 12비트 (그대로 통과) |
| Page table / MMU | 가상→물리 페이지 맵(4단계 트리) / 매 접근 번역 하드웨어 |
| Page-table walk | TLB 미스 시 4단계 의존 메모리 접근 |
| TLB / TLB reach | 번역 캐시 / 엔트리 수 × 페이지 크기 |
| Demand paging | 첫 터치까지 물리 페이지 매핑 연기 |
| Minor / major fault | 메모리 내 연결(~µs) / 디스크 I/O(~ms) |
| Pre-fault | 시작 시 모든 페이지 터치 |
| `mlock` / `mlockall` | 페이지를 상주 고정 (회수 방지) |
| Huge page | 2MB 페이지, 폴트·TLB 미스 감소 |
| Time slice | 스케줄러가 주는 수 ms 코어 사용권 |
| Context switch | 레지스터 저장/복원으로 코어의 스레드 교체 |
| Mode switch | user↔kernel 보호 경계 횡단 (syscall) |
| Voluntary / involuntary | 스스로 블록 / 선점당함 |
| Copy-on-write | fork 후 쓰기 전까지 물리 페이지 공유 |
| IPC | 프로세스 간 통신 (공유 메모리 / 메시지 패싱) |
| Chunk header | malloc이 크기·플래그를 적어두는 앞부분 |
| Bin / tcache | 크기별 free list / glibc 스레드별 캐시 |
| malloc arena | allocator 내부의 스레드별 힙 영역 (자기 락) |
| `brk` / `mmap` | 힙 끝 확장 / 새 영역 매핑 syscall |
| Kernel bypass / DPDK | NIC가 userspace ring에 DMA, 커널 스택 우회 |
| CPU affinity / isolcpus | 스레드 코어 고정 / 코어에서 OS 작업 배제 |
| SCHED_FIFO | 실시간 스케줄링 정책, 일반 스레드에 선점 안 됨 |
