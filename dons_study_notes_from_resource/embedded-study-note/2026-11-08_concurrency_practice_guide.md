# 동시성 연습 가이드 (2026-11-08)

동시성(concurrency)만 따로 모은 트랙은 없다. 대신 **약 70개 아이템이 15개 넘는 트랙에 흩어져 있다.** 이 문서는 그 아이템들을 "어떤 순서로, 무엇을 확인하며" 풀면 되는지 한 장으로 묶은 지도다.

> **쓰는 법**
> - 아래 8개 층을 위에서부터 내려간다. 층마다 **면접 체크**를 말로 설명할 수 있으면 다음 층으로 간다.
> - ⭐ 표시 20개가 **핵심 경로**다. 시간이 없으면 ⭐만 순서대로 푼다.
> - 맨 위 **📋 아이템 현황** 상자에 레슨 준비 수 · done · in progress · **need to fix** · later 가 층별로 실시간 집계된다.
> - 표 첫 칸의 선택 상자로 상태를 바로 바꿀 수 있다 (레슨 페이지 상태 바와 같은 저장소 — 한쪽에서 바꾸면 다른 쪽에도 반영). 아이템 앞 점 색: 파랑 ready · 주황 in progress · 초록 done · 빨강 need to fix · 노랑 later · 회색 준비중.
> - 레슨이 있는 아이템 ID를 누르면 레슨으로, 아직 없는 아이템은 트랙 표의 그 줄로 간다.
> - env가 `linux`인 아이템은 Lima VM `elinux`, `qemu`는 QEMU가 필요하다. 나머지는 Mac에서 바로 된다.

## 한눈에: 8개 층

| 층 | 주제 | 주로 나오는 트랙 | 아이템 수 | ⭐ |
|---|---|---|---|---|
| 1. 기본기 — 레이스를 보고, 막고, 찾기 | pthread · 뮤텍스 · 조건 변수 · TSan | OS · LPERF · TOOL | 6 | 4 |
| 2. 임베디드 — ISR · RTOS 동기화 | ISR 공유 변수 · 세마포어 · 우선순위 역전 · 재진입성 | RTOS · C · BM · RUST | 13 | 4 |
| 3. 원자 연산과 메모리 순서 | volatile vs atomic · acquire/release · 배리어 명령 · exclusive | ARM · C · MCPP · RISCV · CPP · LLAT · OS | 10 | 5 |
| 4. 락을 직접 만들기 | 스핀락 · futex · wait/notify · seqlock · RCU | OS · LLAT · MCPP | 6 | 1 |
| 5. lock-free 자료구조 | SPSC · MPMC · ABA · 메모리 회수 · work-stealing | DS · LLAT · RUST · OS | 9 | 3 |
| 6. 교착 상태 (deadlock) | 4가지 조건 · 락 순서 · 의존 그래프 | STOR · OS · ALGO · SOCX · DCN | 7 | 1 |
| 7. 하드웨어 일관성 — 왜 배리어가 필요한가 | false sharing · MESI · 메모리 모델 · DMA 일관성 | ARCH · MEM · GPGPU · RISCV · SOCX | 10 | 1 |
| 8. 실무 · 검증 응용 | 리눅스 드라이버 · 프로파일링 · 스트레스 테스트 · 분산 | ELX · DIST · LPERF · CAM · SIV · TEST · ROS · OS · QFIN | 13 | 1 |

합계 74개 · ⭐ 20개. 층 안의 순서가 권장 순서다.

## 1. 기본기 — 레이스를 보고, 막고, 찾기

모든 층의 출발점이다. 레이스를 **일부러 만들어서** 깨지는 걸 눈으로 본 뒤에 고친다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-B04](../tracks/OS.html#OS-B04) ⭐ | Basic · mac | 스레드 기초: pthread 생성과 공유 카운터 레이스 | 스레드가 주소 공간을 공유함과 비원자적 증가가 깨지는 이유를 어셈블리 수준에서 보인다 |
| [OS-B05](../tracks/OS.html#OS-B05) ⭐ | Basic · mac | 뮤텍스: 레이스 해결과 경합 비용 측정 | 락 범위·세분성이 정확성과 성능에 주는 영향을 측정한다 |
| [OS-B06](../tracks/OS.html#OS-B06) ⭐ | Basic · mac | 조건 변수: bounded buffer 생산자-소비자 | 조건 변수 사용 규칙(락 보유, while 재검사, signal vs broadcast)을 구현으로 체득한다 |
| [TOOL-M09](../tracks/TOOL.html#TOOL-M09) ⭐ | Mid · mac | ThreadSanitizer로 데이터 레이스 찾기 | TSan 리포트를 읽고 레이스·잘못된 락 순서를 수정한다 |
| [OS-M02](../tracks/OS.html#OS-M02) | Mid · mac | 컨텍스트 스위치 비용 측정: 파이프 핑퐁 | 프로세스·스레드 간 문맥 교환 비용을 측정하고 측정 오차 요인을 통제한다 |
| [LPERF-B10](../tracks/LPERF.html#LPERF-B10) | Basic · linux | 스레드 시간 분해: on-CPU·런큐 대기·sleep | '느리다'를 CPU 실행·CPU 대기·블로킹으로 분해하는 기본 회계를 익힌다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ `counter++`가 왜 원자적이지 않은가? (load–add–store 세 명령)
- ☐ 조건 변수 대기를 `if`가 아니라 `while`로 감싸는 이유 (spurious wakeup, 다른 소비자가 먼저 가져감)
- ☐ `signal` vs `broadcast`는 언제 쓰나?
- ☐ TSan이 "data race"라고 할 때 그 정의는? (적어도 하나가 쓰기, 동기화 없음, 동시 접근)

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


1층 6개 레슨은 모두 써졌고 검증을 통과했다 (시작 코드 FAIL → 정답·추가 연습 PASS, ASan/UBSan·TSan 깨끗). 아래는 작성 중 나온 **환경 한계와 아직 확인 안 한 주장**이다. 읽다가 틀린 곳을 찾으면 그 레슨을 `need to fix`로 표시해 둔다.

| 아이템 | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|
| [OS-B04](../tracks/OS.html#OS-B04) | `-O2`에서는 clang이 루프를 접어 레이스가 **안 보임** → `-O0`/`volatile` 빌드로 재현 | POSIX XBD 4.12 절 번호, FreeRTOS 링크 |
| [OS-B05](../tracks/OS.html#OS-B05) | 벤치 시 load average 7–9 (다른 작업 동시 실행) → 수치는 상대 비교로만 | POSIX 메모리 동기화 인용(절 번호 생략), M0+ `volatile uint32_t` 조언 |
| [OS-B06](../tracks/OS.html#OS-B06) | 맥 `sem_init` = ENOSYS, `pthread_barrier_t` 없음 | FreeRTOS `xQueueReceive` 대기 목록 순서, TSan "size 1" 설명 |
| [TOOL-M09](../tracks/TOOL.html#TOOL-M09) | **맥 TSan은 lock-order-inversion을 보고하지 않음** → 그 리포트는 Lima VM(clang) 출력 | 같은 루프 두 번째 레이스 미보고 원인, `history_size` 동작 |
| [OS-M02](../tracks/OS.html#OS-M02) | 맥은 코어 고정 불가(`THREAD_AFFINITY_POLICY` → 46), `CLOCK_MONOTONIC` 1 µs 단위 | E코어 번호(cpu 0–5) 매핑은 실측 추정, DVFS 해석 |
| [LPERF-B10](../tracks/LPERF.html#LPERF-B10) | VM gcc TSan이 `cond_wait`에서 SIGILL → TSan은 VM에서도 **clang**으로 | `cputime_adjust()` 설명(커널 소스 미확인), VM steal time 회계 |

</details>

## 2. 임베디드 — ISR · RTOS 동기화

펌웨어 면접에서 가장 자주 나오는 층이다. 스레드 대신 **ISR ↔ 메인 루프**, **태스크 ↔ 태스크**가 등장한다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [C-M14](../tracks/C.html#C-M14) ⭐ | Mid · mac | 인터럽트 안전 코드: 공유 변수와 원자성 | ISR과 메인 루프가 공유하는 데이터의 tearing·lost update를 재현하고 막는다 |
| [RTOS-B08](../tracks/RTOS.html#RTOS-B08) | Basic · mac | 바이너리 세마포어: ISR → 태스크 신호 | 이벤트 신호용 바이너리 세마포어의 의미와 이벤트 유실 가능성을 이해한다 |
| [RTOS-B09](../tracks/RTOS.html#RTOS-B09) | Basic · mac | 카운팅 세마포어: 리소스 풀과 이벤트 카운트 | 카운팅 세마포어로 N개 자원 접근 제한과 누적 이벤트 처리를 구현한다 |
| [RTOS-B10](../tracks/RTOS.html#RTOS-B10) ⭐ | Basic · mac | 뮤텍스 vs 세마포어: 소유권과 재귀 뮤텍스 | 뮤텍스가 소유권·우선순위 상속을 갖는 이유와 ISR에서 쓸 수 없는 이유를 안다 |
| [RTOS-M01](../tracks/RTOS.html#RTOS-M01) | Mid · mac | 태스크 알림: 세마포어·이벤트 그룹의 경량 대체 | task notification의 동작(값 덮어쓰기/증가/비트 OR)과 성능 이점·제약(수신자 1명)을 안다 |
| [RTOS-M03](../tracks/RTOS.html#RTOS-M03) ⭐ | Mid · mac | 우선순위 역전 재현: Mars Pathfinder 시나리오 | Low가 자원을 잡고 Medium이 Low를 선점해 High가 무한정 막히는 현상을 재현한다 |
| [RTOS-M04](../tracks/RTOS.html#RTOS-M04) ⭐ | Mid · mac | 우선순위 상속 구현과 연쇄 상속 | 뮤텍스 소유자의 우선순위를 대기자 최고 우선순위로 올리고 해제 시 복원한다 |
| [RTOS-M05](../tracks/RTOS.html#RTOS-M05) | Mid · mac | 우선순위 천장 프로토콜(PCP/IPCP)과 교착 방지 | 천장 우선순위로 우선순위 역전과 교착을 동시에 막는 원리를 구현으로 확인한다 |
| [RTOS-M14](../tracks/RTOS.html#RTOS-M14) | Mid · mac | 재진입성과 스레드 안전: 공유 라이브러리 함수 보호 | strtok·정적 버퍼 함수·newlib malloc/printf처럼 재진입 불가한 코드를 태스크 간에 공유할 때의 버그와 보호 방법을 안다 |
| [C-A04](../tracks/C.html#C-A04) | Adv · mac | 재진입성과 스레드 안전: strtok vs strtok_r | ISR·멀티태스크에서 호출 가능한 함수의 조건을 판별한다 |
| [BM-M11](../tracks/BM.html#BM-M11) | Mid · mac | 저전력 모드: WFI/WFE, Stop/Standby, 슬립 진입 레이스 | 저전력 모드별 웨이크업 소스·복귀 지점 차이와 '조건 확인 후 WFI' 레이스를 이해한다 |
| [RUST-M06](../tracks/RUST.html#RUST-M06) | Mid · mac | ISR 공유 상태: static mut 대신 Mutex<RefCell<..>> | C의 'volatile 전역 + 인터럽트 끄기' 패턴을 critical-section과 atomic으로 안전하게 옮긴다. |
| [RTOS-A08](../tracks/RTOS.html#RTOS-A08) | Adv · mac | SMP RTOS: 코어 친화성, 스핀락, 공유 ready list | 멀티코어 MCU(RP2040, 듀얼 코어 Cortex-M 등)에서 RTOS가 달라지는 지점을 이해한다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ ISR과 메인이 공유하는 32비트/64비트 변수에서 tearing이 일어나는 조건은?
- ☐ 뮤텍스와 바이너리 세마포어의 차이 (소유권, 우선순위 상속, ISR에서 사용 가능 여부)
- ☐ Mars Pathfinder 우선순위 역전을 30초 안에 설명하고, 상속 vs 천장 프로토콜을 비교
- ☐ "조건 확인 후 WFI" 사이에 인터럽트가 오면? (인터럽트를 끈 채로 확인 → WFI)
- ☐ 재진입(reentrant)과 스레드 안전(thread-safe)은 같은 말인가?

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


2층 13개 레슨이 모두 써졌다. C-M14·C-A04는 이전에 썼고, 나머지 11개를 이번에 새로 썼다. 11개 모두 시작 코드 FAIL → 정답·추가 연습 PASS가 확인됐고, ASan/UBSan도 깨끗하다. 스레드를 쓰는 RTOS-A08만 TSan이 해당되며 결과는 깨끗하다. 미니 커널은 레슨마다 들어 있다. OS 스레드 하나로 tick 단위로 돌기 때문에 결과가 매번 같다. M03·M04·M05는 같은 시나리오를 쓴다(H=3/M=2/L=1). 세 레슨의 수치는 일치한다: 일반 세마포어에서 **H 대기 = 3 + C_M**이고, 우선순위 상속(PI)과 천장 프로토콜(IPCP)에서는 C_M과 상관없이 3이다. 진짜 FreeRTOS(V11.3.2, macOS POSIX 포트)는 B08·B10·M01에서 직접 돌렸다. 소스는 `~/.esn_scratch/FreeRTOS-Kernel`에 있다.

| 아이템 | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|
| [RTOS-B08](../tracks/RTOS.html#RTOS-B08) | POSIX 포트엔 진짜 ISR이 없다 → tick hook을 ISR로 썼다(다른 pthread에서 FromISR을 부르는 건 지원 안 됨). 순서·반환값만 비교한다 | ARMv7-M ARM ICSR 절 번호(기억), Zephyr·CMSIS 반환값(문서 기준) |
| [RTOS-B09](../tracks/RTOS.html#RTOS-B09) | 모델의 give는 대기자에게 바로 넘긴다(handoff). FreeRTOS는 깨운 뒤 다시 확인한다(barging, 소스로만 확인) | Zephyr `k_sem_give` handoff(기억), CMSIS 대기 순서 |
| [RTOS-B10](../tracks/RTOS.html#RTOS-B10) | FromISR 호출은 태스크 문맥에서 했다(코드 경로만 보인다). configASSERT를 끄면 상호배제가 깨진다(`in_cs=2`, 실측) | Zephyr `k_mutex_unlock` -EPERM, CMSIS 비재귀 재획득 동작(구현 의존) |
| [RTOS-M01](../tracks/RTOS.html#RTOS-M01) | 호스트 시간은 POSIX 포트 시스템 콜이 지배한다(give+take ≈505 ns로 다 같음). Cortex-M 크기는 clang `-Os` 기준 | 공식 문서의 "45% faster"(MCU에서 미측정), CMSIS thread flags = 알림 구현설 |
| [RTOS-M03](../tracks/RTOS.html#RTOS-M03) | 맥은 실시간 스케줄러가 아니고 멀티코어라 실제 스레드로는 역전을 재현하지 않았다. 맥에서 PRIO_INHERIT/PROTECT는 설정만 통과한다 | Pathfinder 기록 차이(Reeves vs Jones: watchdog 표현, 패치 방식), VxWorks API 이름 |
| [RTOS-M04](../tracks/RTOS.html#RTOS-M04) | FreeRTOS는 소스만 읽었다(연쇄 상속 없음 tasks.c:6805, 모든 뮤텍스를 놓아야 복원) | 🆕 **Zephyr v4.5.0(2026-10-28)부터 연쇄 상속과 정확한 복원을 지원**(commit 1b60add1ac) — 이전 메모의 "Zephyr도 transitive PI 없음"은 v4.4까지만 맞다. time slicing 시 H 대기 ~1 tick 추론 |
| [RTOS-M05](../tracks/RTOS.html#RTOS-M05) | `PTHREAD_PRIO_PROTECT`는 Lima VM(root, SCHED_FIFO)에서 상승을 실측했다(20→40→30→20). **macOS는 무시한다**. 모델은 tick 경계에서만 release하므로 IPCP 최대 대기 3 < 상한 B_H=4 | Zephyr `CONFIG_PRIORITY_CEILING` = 상속 상한(문서 기준), OSEK 절 번호, SRL 1990 정리 번호 |
| [RTOS-M14](../tracks/RTOS.html#RTOS-M14) | ucontext 미니 커널(맥 deprecated 경고는 그 파일에서만 끔). 맥에 arm newlib가 없어 `sizeof(struct _reent)`는 미측정 | SMP에서 전역 `_impure_ptr` 덮어쓰기(소스만 읽음), 툴체인별 retargetable locking 여부 |
| [RTOS-A08](../tracks/RTOS.html#RTOS-A08) | 맥은 코어 고정이 안 된다 → '코어'는 스케줄러 안의 규칙일 뿐이다. 호스트 IPI 지연(µs)은 MCU와 무관하다 | Linux `spin_lock_irqsave` 스핀 중 재활성화, pico-sdk 펜스, RP2040 SIO 의미(데이터시트 기준) |
| [BM-M11](../tracks/BM.html#BM-M11) | 실제 MCU와 전류 측정이 없다 → 전력 수치는 모델값이다(STM32L4 정도의 규모). 레이스는 시뮬레이터와 sigsuspend 유사 실험으로 봤다(pause 손실 ≈30%) | PRIMASK=1에서 이미 pending이면 WFI가 바로 빠져나옴(B1.5.19 해석, 실리콘 미검증), STM32 HAL·레지스터 이름(기억) |
| [RUST-M06](../tracks/RUST.html#RUST-M06) | stable만 설치했다 → Rust sanitizer/Miri는 미실행(크레이트에 unsafe 없음). 실행 전 `export PATH=/opt/homebrew/opt/rustup/bin:$PATH`와 SDKROOT가 필요하다 | 칩 바이너리 링크 에러 문구(README 인용), M4에서 portable-atomic `AtomicU64`의 ISR 안전성 |

</details>

## 3. 원자 연산과 메모리 순서

같은 개념을 C11 → C++ → ARM 명령 → RISC-V 명령 순으로 한 층씩 내려가며 본다. 트랙은 다르지만 **언어/ISA가 달라서 중복이 아니다** (중복 검토 문서 C 분류).

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [C-A02](../tracks/C.html#C-A02) ⭐ | Adv · mac | 컴파일러 배리어와 메모리 순서: volatile만으론 부족한 이유 | 컴파일러 재배치와 CPU 재배치를 구분하고 적절한 배리어를 고른다 |
| [C-A03](../tracks/C.html#C-A03) ⭐ | Adv · mac | C11 Atomics: 플래그·카운터·acquire/release | stdatomic의 memory_order를 실제 생산자/소비자 코드에 적용한다 |
| [OS-M08](../tracks/OS.html#OS-M08) | Mid · mac | C11 atomics 동기화 패턴: 메시지 패싱·DCLP | 리트머스 결과를 실제 코드 패턴(플래그 전달, 지연 초기화)에 적용한다 |
| [CPP-A04](../tracks/CPP.html#CPP-A04) ⭐ | Adv · mac | std::atomic과 Cortex-M: lock-free 여부와 ISR 공유 | ISR-메인 공유 변수에서 volatile과 atomic의 차이, 아키텍처별 lock-free 가능 여부를 안다 |
| [MCPP-M13](../tracks/MCPP.html#MCPP-M13) | Mid · mac | 메모리 모델 규칙: fence·release sequence | happens-before를 만드는 표준 규칙(fence 동기화, RMW release sequence)을 코드로 설명한다. |
| [ARM-M04](../tracks/ARM.html#ARM-M04) | Mid · mac | LDXR/STXR로 원자 연산 구현 (AArch64) | exclusive monitor 기반 CAS·fetch_add와 LSE 원자 명령을 비교한다 |
| [ARM-M05](../tracks/ARM.html#ARM-M05) ⭐ | Mid · mac | 메모리 배리어 명령: DMB, DSB, ISB, LDAR/STLR | 배리어 종류별 의미와 acquire/release 명령의 사용처를 구분한다 |
| [ARM-M11](../tracks/ARM.html#ARM-M11) ⭐ | Mid · qemu | Cortex-M LDREX/STREX와 Exclusive Monitor | M-profile 원자 연산과 예외 시 CLREX/모니터 클리어 동작을 이해한다 |
| [RISCV-M04](../tracks/RISCV.html#RISCV-M04) | Mid · mac | A 확장: LR/SC 예약과 AMO로 락·CAS 구현 | LR/SC 예약 집합, SC 실패 조건, 제약된 LR/SC 루프(16명령 이내) 규칙과 AMOSWAP/AMOADD 기반 동기화를 구현한다. |
| [LLAT-B06](../tracks/LLAT.html#LLAT-B06) | Basic · mac | atomic 연산 비용: relaxed·acq_rel·seq_cst·CAS 경합 | 원자 연산 종류와 메모리 순서, 경합이 지연에 주는 비용을 수치로 안다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ `volatile`만으로 부족한 이유 두 가지 (원자성 없음, CPU 재배치 막지 못함)
- ☐ release store와 acquire load가 만드는 happens-before를 그림으로
- ☐ DMB vs DSB vs ISB를 언제 쓰나? (메모리 순서 / 완료 대기 / 파이프라인 flush)
- ☐ LDREX/STREX 사이에 예외가 나면 어떻게 되나? (모니터 클리어 → STREX 실패 → 재시도)
- ☐ Cortex-M0에서는 `fetch_add`조차, M3/M4에서는 64비트 atomic이 lock-free가 아닌 이유 (v6-M엔 exclusive 명령 없음, v7-M엔 LDREXD 없음)

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


3층 10개 레슨이 모두 있다 (C-A02 · C-A03은 C Advanced 때 작성, 나머지 8개는 이번에 작성). 8개 모두 독립 검증 통과: 시작 코드 FAIL → 정답·추가 연습 PASS, ASan/UBSan·TSan 깨끗. 기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6 = 12코어), Apple clang 21. 아래는 **환경 한계와 아직 확인 안 한 주장**이다. 틀린 곳을 찾으면 그 레슨을 `need to fix`로 표시해 둔다.

| 아이템 | 레슨에서 실측한 것 (한 줄) | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|---|
| [OS-M08](../tracks/OS.html#OS-M08) | relaxed 플래그 메시지 패싱이 이 칩에서 **실제로 깨짐** (최대 ~1%), release/acquire·`pthread_once`는 0 | 맥에 `<threads.h>`/`call_once` 없음, 코어 고정 불가 → 비율은 실행마다 다름. 독립 `atomic_thread_fence`는 TSan 오탐 → practice는 TSan 빌드에서만 store/load 형태로 바꿈 | 같은 캐시 라인 재배치의 원인 설명(추정), 16바이트 `str q0`의 단일 관측성, Arm ARM 절 번호(B2.3) |
| [CPP-A04](../tracks/CPP.html#CPP-A04) | M0+는 32비트 load까지 libcall, M3/M4는 32비트 `ldrex/strex`·64비트 libcall, `isr_shared<T>` 정책 헤더 | 맥 clang엔 bare-metal C++ 헤더 없음 → VM의 arm-none-eabi libstdc++ 헤더를 복사해 씀, 링크 데모는 VM에서만 | compiler-rt `atomic.c` 빌드 제외 설명, Zephyr/pico-sdk가 `__atomic_*` 제공, RP2040 SIO 스핀락 세부, DDI 0403 절 번호 |
| [MCPP-M13](../tracks/MCPP.html#MCPP-M13) | SC 인터리빙 전수 탐색기 vs TSan: fence 3형태 = TSan 오탐, C++20 release sequence 레이스 2개 = TSan 놓침 | thumbv7em용 libc++ `<atomic>` 컴파일 실패 → Cortex-M fence asm은 C-A02 결과 인용 | TSan이 놓치는 이유(런타임 소스 미확인), P0982R1·P0019R8·P0668R5 번호, C 쪽 `atomic_ref` 상호운용 |
| [ARM-M04](../tracks/ARM.html#ARM-M04) | 이 맥에서 **"LSE가 항상 빠르다"는 거짓**: `ldaddal`은 빠르지만 `casal` CAS 루프·`swpa` 락은 LL/SC보다 2–4배 느림 | `CTR_EL0` 유저 모드 읽기 SIGILL → ERG 측정 불가, 코어 고정 불가 | 예외 복귀 시 모니터 클리어, Linux `atomic_ll_sc.h`의 `dmb ish`, gdb가 LDXR…STXR을 한 번에 넘김, casal 느린 원인(far atomics 가설) |
| [ARM-M05](../tracks/ARM.html#ARM-M05) | MP 리트머스에서 재배치 실측 (plain 6.25M/20M), 금지 조합 6개는 0. SB에서 `stlr;ldapr`만 재배치 → RCsc vs RCpc 차이 | 코어 고정 불가(QoS 힌트만), `CTR_EL0` 못 읽음 → IDC/DIC는 `sys_icache_invalidate` 역어셈으로 추정 | 동시 수정 허용 명령 목록, CMSIS `ARM_MPU_Enable`의 배리어, i.MX8M/STM32MP1 공유 도메인, reader 쪽 `dmb ishst`가 더 나쁜 이유 |
| [ARM-M11](../tracks/ARM.html#ARM-M11) | gdb로 LDREX–STREX 사이에 인터럽트 강제 → STREX 실패 → 재시도 → 정확. **QEMU STREX는 값 비교**라 디버거 쓰기도 감지(실리콘과 다름) | 실보드 없음 (QEMU mps2-an385 + gdb, Lima VM). 기본 QEMU 모드는 TB 경계에서만 인터럽트 → 재시도 0, `one-insn-per-tb=on`에서 재현 | ARMv7-M ARM A3.4.4 인용은 2차 출처, 실리콘에서 디버거/DMA 쓰기 미감지, 같은 코어 일반 스토어·granule 크기 |
| [RISCV-M04](../tracks/RISCV.html#RISCV-M04) | 레슨 안에 2-hart RV32IA ISS 내장, 인코딩 259개 clang 21과 일치. qemu-riscv64에서 A-B-A 후에도 SC 성공(스펙상 실패해야 함) | ISS는 SC(순차 일관) → aq/rl/fence 효과는 관찰 불가(약한 메모리는 RISCV-A04). 맥 clang엔 RISC-V 백엔드 없음 → VM에서 교차 확인 | 스펙 장 번호(20240411 Ch.14), 칩 ISA 문자열(ESP32-C3에 A 없음 등), Zalasr 비준 상태, 64B 예약 단위는 대표값 |
| [LLAT-B06](../tracks/LLAT.html#LLAT-B06) | 비경합 시 `ldadd`≈plain add≈1.4 ns, 8스레드 경합 CAS 루프 24 Mops/s vs 스레드별 슬롯 6786. read-before-write ×85–88 | 다른 에이전트 벤치마크와 동시 실행(load 5–11) → **배수만** 보기, 클럭(≈4.4 GHz)은 추정 | `ldadda`가 `ldaddal`보다 1.74배 느린 이유, self-pointer 체인 1사이클 = 값 예측, seq_cst 경합 시 +20% 해석 |

</details>

## 4. 락을 직접 만들기

3층의 원자 연산으로 실제 동기화 프리미티브를 짠다. 성능 측정이 같이 붙어 있어서 "왜 이 설계인가"를 숫자로 답할 수 있다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-M07](../tracks/OS.html#OS-M07) ⭐ | Mid · mac | 스핀락 구현: TAS, TTAS, ticket lock | 원자 명령으로 락을 만들고 캐시 일관성 트래픽이 성능에 주는 영향을 측정한다 |
| [OS-A01](../tracks/OS.html#OS-A01) | Adv · linux | futex 기반 뮤텍스 구현 (Drepper 방식) | 커널 개입을 경합 시로만 제한하는 빠른 사용자 공간 락의 설계와 미묘한 버그를 이해한다 |
| [MCPP-A03](../tracks/MCPP.html#MCPP-A03) | Adv · mac | atomic wait/notify로 동기화 프리미티브 구현 | C++20 atomic wait/notify로 세마포어·래치를 만든다. |
| [LLAT-M02](../tracks/LLAT.html#LLAT-M02) | Mid · mac | Seqlock: 단일 writer 스냅샷 공유 | reader가 writer를 막지 않는 일관 스냅샷 공유를 구현한다. |
| [OS-A03](../tracks/OS.html#OS-A03) | Adv · mac | RCU 개념 구현: epoch 기반 메모리 회수 | 읽기 측 무락 접근과 grace period 후 해제라는 RCU 핵심 아이디어를 사용자 공간에서 구현한다 |
| [LLAT-M05](../tracks/LLAT.html#LLAT-M05) | Mid · mac | 스레드 간 핸드오프 지연 (ping-pong) | 코어 간 통신 지연과 코어 배치의 영향을 측정한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ TAS → TTAS → ticket lock으로 갈수록 무엇이 좋아지나? (캐시 라인 핑퐁, 공정성)
- ☐ futex가 빠른 이유 (경합 없을 때 커널 진입 0번)
- ☐ seqlock reader가 재시도해야 하는 조건과 writer 굶주림
- ☐ RCU의 grace period란?

## 5. lock-free 자료구조

면접 고급 문제의 단골이다. **DS-A01 SPSC 큐**는 임베디드(ISR 생산자 → 메인 소비자)에서 실제로 가장 많이 쓰는 형태다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [DS-A01](../tracks/DS.html#DS-A01) ⭐ | Adv · mac | Lock-free SPSC 큐: C11 atomics | 단일 생산자/소비자 큐를 acquire/release만으로 정확하고 빠르게 만든다 |
| [DS-A03](../tracks/DS.html#DS-A03) ⭐ | Adv · mac | Bounded MPMC 큐: Vyukov 시퀀스 방식 | 다중 생산자/소비자 lock-free 큐를 슬롯 시퀀스 번호로 구현한다 |
| [DS-A04](../tracks/DS.html#DS-A04) ⭐ | Adv · mac | Lock-free 스택과 ABA 문제 | Treiber 스택의 ABA 버그를 재현하고 태그 포인터로 해결한다 |
| [LLAT-M01](../tracks/LLAT.html#LLAT-M01) | Mid · mac | SPSC 큐 지연 튜닝: 캐시된 인덱스·패딩·배치 | 기본 lock-free SPSC 큐를 지연 관점에서 단계별로 최적화하고 효과를 분리 측정한다. |
| [LLAT-M03](../tracks/LLAT.html#LLAT-M03) | Mid · mac | lock-free 메모리 회수: Hazard Pointer | lock-free 구조에서 노드를 안전하게 해제하는 방법과 지연 영향을 이해한다. |
| [RUST-M07](../tracks/RUST.html#RUST-M07) | Mid · mac | Lock-free SPSC 큐: ISR 생산자·main 소비자 split | Acquire/Release 순서로 ISR↔main SPSC 큐를 구현하고 split으로 단일 생산자/소비자를 타입으로 보장한다. |
| [OS-A05](../tracks/OS.html#OS-A05) | Adv · mac | work-stealing 스레드 풀: Chase-Lev deque | 로드 밸런싱을 위한 작업 훔치기 스케줄러와 lock-free deque의 메모리 오더링을 구현한다(false sharing 측정은 see:MEM) |
| [LLAT-A06](../tracks/LLAT.html#LLAT-A06) | Adv · mac | 저지연 비동기 로거 (NanoLog 스타일) | hot path에서 ns 단위로 로그를 남기는 구조를 구현한다. |
| [DS-M10](../tracks/DS.html#DS-M10) | Mid · mac | 가변 길이 레코드 로그 링 버퍼 | 오래된 레코드를 덮어쓰는 가변 길이 트레이스 버퍼를 설계한다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ SPSC 링 버퍼에서 head/tail에 각각 필요한 memory_order는?
- ☐ ABA 문제를 재현하는 최소 시나리오와 해결책 (태그 포인터, hazard pointer, epoch)
- ☐ MPMC에서 Vyukov 방식이 슬롯마다 시퀀스 번호를 두는 이유
- ☐ lock-free와 wait-free의 차이

## 6. 교착 상태 (deadlock)

같은 "순환 대기"가 소프트웨어 락(OS), DB(STOR), 칩 인터커넥트(SOCX), 네트워크(DCN)에 모두 나온다. OS-M10·OS-M11을 먼저 풀고, 나머지는 지원 직무에 맞춰 고른다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [OS-M10](../tracks/OS.html#OS-M10) ⭐ | Mid · mac | 교착 상태: 식사하는 철학자와 4가지 조건 | 교착의 필요 조건을 이해하고 각 조건을 깨는 해법을 구현으로 비교한다 |
| [OS-M11](../tracks/OS.html#OS-M11) | Mid · mac | 교착 탐지기: 락 순서 그래프 (mini lockdep) | 실행 중 락 획득 순서를 기록해 잠재적 교착을 실제로 일어나기 전에 탐지한다 |
| [ALGO-A10](../tracks/ALGO.html#ALGO-A10) | Adv · mac | 강한 연결 요소: 락 의존 그래프 데드락 검출 | Tarjan/Kosaraju SCC로 순환 대기를 찾는다 |
| [STOR-M08](../tracks/STOR.html#STOR-M08) | Mid · mac | 동시성 제어: 2PL & 데드락 감지 | 잠금 기반 직렬화 가능성 보장을 구현한다. |
| [STOR-M10](../tracks/STOR.html#STOR-M10) | Mid · mac | B+tree 동시성: latch crabbing & optimistic | 인덱스 동시 접근의 래치 프로토콜을 구현한다. |
| [SOCX-M05](../tracks/SOCX.html#SOCX-M05) | Mid · mac | NoC 라우팅 데드락: 채널 의존 그래프 | Dally–Seitz 채널 의존 그래프(CDG)로 라우팅 함수의 데드락 자유성을 증명·반증한다. |
| [DCN-M05](../tracks/DCN.html#DCN-M05) | Mid · mac | PFC 데드락: 순환 버퍼 의존성 탐지 | PFC가 루프성 경로에서 만드는 순환 버퍼 의존(CBD)과 데드락을 재현하고 방지한다. |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ 교착의 4가지 필요 조건과 각각을 깨는 방법
- ☐ 락 순서 규칙(lock ordering)을 런타임에 검사하는 방법 (lockdep)
- ☐ 의존 그래프에서 사이클 찾기 → SCC (Tarjan/Kosaraju)

## 7. 하드웨어 일관성 — 왜 배리어가 필요한가

3·4·5층에서 "왜?"라고 물으면 여기서 답이 나온다. 실리콘 검증과 AI 칩 쪽 면접이라면 이 층의 비중이 커진다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [ARCH-M08](../tracks/ARCH.html#ARCH-M08) ⭐ | Mid · mac | False Sharing 측정 | 같은 캐시 라인을 공유하는 독립 변수의 성능 붕괴를 재현한다 |
| [MEM-M04](../tracks/MEM.html#MEM-M04) | Mid · mac | 일관성 트래픽 실측: 공유 캐시라인 경합과 코어 수 | 캐시 일관성 프로토콜 비용이 실제 하드웨어에서 어떻게 나타나는지 측정한다 |
| [ARCH-M12](../tracks/ARCH.html#ARCH-M12) | Mid · mac | DMA와 캐시 일관성: Clean/Invalidate 시나리오 | non-coherent DMA에서 stale 데이터 버그와 올바른 캐시 유지보수 순서를 이해한다 |
| [ARCH-A04](../tracks/ARCH.html#ARCH-A04) | Adv · mac | MESI 캐시 일관성 프로토콜 시뮬레이터 | 스누핑 기반 MESI 상태 전이와 버스 트랜잭션을 구현한다 |
| [ARCH-A05](../tracks/ARCH.html#ARCH-A05) | Adv · mac | 메모리 순서 리트머스 테스트 on Apple M2 | 약한 메모리 모델에서 Store Buffering·Message Passing 재배치를 실제로 관찰한다 |
| [MEM-M07](../tracks/MEM.html#MEM-M07) | Mid · mac | 메모리 일관성 모델 시뮬레이터: SC·TSO·ARM 허용 결과 열거 | 형식적 모델로 어떤 재정렬 결과가 허용되는지 직접 계산한다 |
| [RISCV-A04](../tracks/RISCV.html#RISCV-A04) | Adv · mac | RVWMO 메모리 모델: FENCE 종류·.aq/.rl과 C11 매핑 | RVWMO의 허용 재배치, fence pred/succ 집합(rw,rw / r,rw / rw,w / tso), AMO .aq/.rl 의미와 C11 원자 연산 매핑 표를 근거와 함께 설명한다. |
| [SOCX-B03](../tracks/SOCX.html#SOCX-B03) | Basic · mac | AXI 응답과 배타적 접근: EXOKAY와 Global Exclusive Monitor | OKAY/EXOKAY/SLVERR/DECERR 의미와 인터커넥트 수준 exclusive monitor 동작을 설명한다. |
| [GPGPU-B09](../tracks/GPGPU.html#GPGPU-B09) | Basic · mac | Atomic과 Barrier: 레이스와 동기화 | threadgroup barrier와 atomic 연산의 의미, 경쟁 조건 재현과 해결을 익힌다 |
| [GPGPU-M03](../tracks/GPGPU.html#GPGPU-M03) | Mid · mac | Histogram: Privatization과 Atomic 경합 | atomic 경합을 threadgroup 지역 히스토그램으로 줄이는 privatization 기법을 익힌다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ false sharing을 재현하고 고치는 방법 (padding / `alignas(64)`)
- ☐ MESI 상태 전이를 쓰기 하나로 따라가 보기 (S → I, RFO)
- ☐ Apple M2(ARM)에서는 보이고 x86(TSO)에서는 안 보이는 재배치는?
- ☐ non-coherent DMA 전송 전후로 clean/invalidate를 어떤 순서로?

## 8. 실무 · 검증 응용

앞 층에서 익힌 것을 실제 시스템에서 찾고 검증하는 단계다. 직무에 맞는 것만 고르면 된다.

| 아이템 | 레벨 · env | 제목 | 확인할 것 (아이템 목표) |
|---|---|---|---|
| [ELX-M04](../tracks/ELX.html#ELX-M04) | Mid · linux | 드라이버 동기화: spinlock vs mutex vs atomic | 컨텍스트(프로세스/IRQ)에 맞는 락 선택과 race 재현·수정 능력을 기른다. |
| [ELX-M05](../tracks/ELX.html#ELX-M05) | Mid · linux | 인터럽트 처리: hard IRQ와 threaded IRQ | top-half 최소화와 request_threaded_irq/IRQF_ONESHOT 사용법을 익힌다. |
| [ELX-M06](../tracks/ELX.html#ELX-M06) | Mid · linux | Bottom half 비교: tasklet vs workqueue vs threaded IRQ | 지연 처리 메커니즘별 실행 컨텍스트·sleep 가능 여부·지연을 비교한다. |
| [LPERF-M07](../tracks/LPERF.html#LPERF-M07) | Mid · linux | 락 경합 프로파일링: futex 대기와 holder 찾기 | 사용자 공간 락 경합을 대기 시간·호출 스택·락 주소별로 정량화한다. |
| [LPERF-A03](../tracks/LPERF.html#LPERF-A03) | Adv · linux | perf c2c로 false sharing 찾기 | 캐시 라인 경합(HITM)을 데이터 주소·코드 위치까지 귀속한다. |
| [SIV-M09](../tracks/SIV.html#SIV-M09) ⭐ | Mid · mac | 동시성 스트레스 테스트 설계 | CPU·DMA·메모리 트래픽을 동시에 걸어 단독 테스트에서 안 보이는 버그를 노출한다 |
| [TEST-A05](../tracks/TEST.html#TEST-A05) | Adv · mac | 결정론적 시뮬레이션 테스트: 가상 시간과 재현 가능한 seed | 타이밍 의존 버그를 seed 하나로 재현 가능하게 만든다. |
| [CAM-M11](../tracks/CAM.html#CAM-M11) | Mid · mac | 캡처 버퍼 큐와 DMA: qbuf/dqbuf 소유권 모델 | 드라이버·하드웨어·애플리케이션 사이 버퍼 소유권 이동과 버퍼 부족 시 drop 정책을 설계한다 |
| [ROS-M03](../tracks/ROS.html#ROS-M03) | Mid · linux | Executor & callback group | 콜백 실행 모델과 동시성·데드락 원인을 이해한다. |
| [DIST-B03](../tracks/DIST.html#DIST-B03) | Basic · mac | Vector Clock과 동시성 판정 | 두 이벤트가 인과 관계인지 동시인지 판별한다 |
| [DIST-A05](../tracks/DIST.html#DIST-A05) | Adv · mac | 선형성(Linearizability) 검사기 | Jepsen 스타일로 분산 저장소의 일관성 위반을 자동으로 찾는다 |
| [QFIN-M07](../tracks/QFIN.html#QFIN-M07) | Mid · mac | OMS: ExecutionReport 처리와 Cancel/Replace 경쟁 | 비동기 거래소 응답 속에서 주문 상태를 일관되게 추적한다. |
| [OS-A11](../tracks/OS.html#OS-A11) | Adv · mac | 종합: 멀티스레드 서비스 데몬 | 프로세스·스레드·I/O·메모리·시그널 개념을 하나의 견고한 서비스로 통합한다 |

**면접 체크 — 말로 설명할 수 있으면 통과**

- ☐ IRQ 컨텍스트에서 mutex를 쓰면 안 되는 이유와 `spin_lock_irqsave`
- ☐ hard IRQ / threaded IRQ / workqueue 중 무엇을 고르나 (sleep 가능 여부, 지연)
- ☐ 타이밍 의존 버그를 재현 가능하게 만드는 방법 (결정론적 시뮬레이션, seed)
- ☐ 동시성 스트레스 테스트에서 무엇을 동시에 거나 (CPU · DMA · 메모리 트래픽)

## ⭐ 핵심 경로 20개

펌웨어 · 실리콘 검증 면접 기준. 하루 2개면 2주, 하루 3개면 약 7일.

| # | 아이템 | 제목 | 층 |
|---|---|---|---|
| 1 | [OS-B04](../tracks/OS.html#OS-B04) | 스레드 기초: pthread 생성과 공유 카운터 레이스 | 1층 |
| 2 | [OS-B05](../tracks/OS.html#OS-B05) | 뮤텍스: 레이스 해결과 경합 비용 측정 | 1층 |
| 3 | [OS-B06](../tracks/OS.html#OS-B06) | 조건 변수: bounded buffer 생산자-소비자 | 1층 |
| 4 | [TOOL-M09](../tracks/TOOL.html#TOOL-M09) | ThreadSanitizer로 데이터 레이스 찾기 | 1층 |
| 5 | [C-M14](../tracks/C.html#C-M14) | 인터럽트 안전 코드: 공유 변수와 원자성 | 2층 |
| 6 | [RTOS-B10](../tracks/RTOS.html#RTOS-B10) | 뮤텍스 vs 세마포어: 소유권과 재귀 뮤텍스 | 2층 |
| 7 | [RTOS-M03](../tracks/RTOS.html#RTOS-M03) | 우선순위 역전 재현: Mars Pathfinder 시나리오 | 2층 |
| 8 | [RTOS-M04](../tracks/RTOS.html#RTOS-M04) | 우선순위 상속 구현과 연쇄 상속 | 2층 |
| 9 | [C-A02](../tracks/C.html#C-A02) | 컴파일러 배리어와 메모리 순서: volatile만으론 부족한 이유 | 3층 |
| 10 | [C-A03](../tracks/C.html#C-A03) | C11 Atomics: 플래그·카운터·acquire/release | 3층 |
| 11 | [ARM-M05](../tracks/ARM.html#ARM-M05) | 메모리 배리어 명령: DMB, DSB, ISB, LDAR/STLR | 3층 |
| 12 | [ARM-M11](../tracks/ARM.html#ARM-M11) | Cortex-M LDREX/STREX와 Exclusive Monitor | 3층 |
| 13 | [CPP-A04](../tracks/CPP.html#CPP-A04) | std::atomic과 Cortex-M: lock-free 여부와 ISR 공유 | 3층 |
| 14 | [OS-M07](../tracks/OS.html#OS-M07) | 스핀락 구현: TAS, TTAS, ticket lock | 4층 |
| 15 | [DS-A01](../tracks/DS.html#DS-A01) | Lock-free SPSC 큐: C11 atomics | 5층 |
| 16 | [DS-A03](../tracks/DS.html#DS-A03) | Bounded MPMC 큐: Vyukov 시퀀스 방식 | 5층 |
| 17 | [DS-A04](../tracks/DS.html#DS-A04) | Lock-free 스택과 ABA 문제 | 5층 |
| 18 | [OS-M10](../tracks/OS.html#OS-M10) | 교착 상태: 식사하는 철학자와 4가지 조건 | 6층 |
| 19 | [ARCH-M08](../tracks/ARCH.html#ARCH-M08) | False Sharing 측정 | 7층 |
| 20 | [SIV-M09](../tracks/SIV.html#SIV-M09) | 동시성 스트레스 테스트 설계 | 8층 |

## 직무별로 더할 것

| 목표 직무 | 핵심 경로에 더할 아이템 | 이유 |
|---|---|---|
| 펌웨어 (MCU · RTOS) | [RTOS-B08](../tracks/RTOS.html#RTOS-B08) · [RTOS-M05](../tracks/RTOS.html#RTOS-M05) · [RTOS-M14](../tracks/RTOS.html#RTOS-M14) · [BM-M11](../tracks/BM.html#BM-M11) · [RTOS-A08](../tracks/RTOS.html#RTOS-A08) · [RUST-M06](../tracks/RUST.html#RUST-M06) | ISR·태스크 동기화와 저전력 진입 레이스가 실제 버그의 대부분 |
| 실리콘 검증 · SoC | [ARCH-M12](../tracks/ARCH.html#ARCH-M12) · [SOCX-B03](../tracks/SOCX.html#SOCX-B03) · [ARCH-A04](../tracks/ARCH.html#ARCH-A04) · [MEM-M07](../tracks/MEM.html#MEM-M07) · [SOCX-M05](../tracks/SOCX.html#SOCX-M05) | DMA 일관성, exclusive monitor, 일관성 프로토콜을 테스트로 검증 |
| 임베디드 리눅스 · 드라이버 | [ELX-M04](../tracks/ELX.html#ELX-M04) · [ELX-M05](../tracks/ELX.html#ELX-M05) · [ELX-M06](../tracks/ELX.html#ELX-M06) · [LPERF-M07](../tracks/LPERF.html#LPERF-M07) · [OS-A01](../tracks/OS.html#OS-A01) | 커널 락 선택과 bottom half, 사용자 공간 락 경합 분석 |
| 저지연 · 퀀트 | [LLAT-B06](../tracks/LLAT.html#LLAT-B06) · [LLAT-M01](../tracks/LLAT.html#LLAT-M01) · [LLAT-M02](../tracks/LLAT.html#LLAT-M02) · [LLAT-M03](../tracks/LLAT.html#LLAT-M03) · [LLAT-A06](../tracks/LLAT.html#LLAT-A06) · [QFIN-M07](../tracks/QFIN.html#QFIN-M07) | SPSC 튜닝, seqlock, 메모리 회수를 ns 단위로 측정 |
| AI 칩 · GPU | [GPGPU-B09](../tracks/GPGPU.html#GPGPU-B09) · [GPGPU-M03](../tracks/GPGPU.html#GPGPU-M03) · [MEM-M04](../tracks/MEM.html#MEM-M04) · [ARCH-A05](../tracks/ARCH.html#ARCH-A05) | GPU atomic 경합과 하드웨어 메모리 모델 |

## 같은 개념이 여러 트랙에 나오는 이유

중복처럼 보이지만 언어·ISA·도메인이 달라서 일부러 둔 것들이다 (중복 검토 문서의 C 분류와 같은 기준). 한 줄씩 비교하며 풀면 오히려 기억에 오래 남는다.

| 개념 | 아이템 | 무엇이 다른가 |
|---|---|---|
| acquire/release | [C-A03](../tracks/C.html#C-A03) · [CPP-A04](../tracks/CPP.html#CPP-A04) · [MCPP-M13](../tracks/MCPP.html#MCPP-M13) · [RUST-M07](../tracks/RUST.html#RUST-M07) | C11 → C++ (Cortex-M) → C++ 표준 규칙 → Rust 타입 시스템 |
| 원자 RMW 명령 | [ARM-M04](../tracks/ARM.html#ARM-M04) · [ARM-M11](../tracks/ARM.html#ARM-M11) · [RISCV-M04](../tracks/RISCV.html#RISCV-M04) · [SOCX-B03](../tracks/SOCX.html#SOCX-B03) | AArch64 → Cortex-M → RISC-V → 인터커넥트(AXI) 수준 |
| 메모리 모델 | [ARCH-A05](../tracks/ARCH.html#ARCH-A05) · [MEM-M07](../tracks/MEM.html#MEM-M07) · [RISCV-A04](../tracks/RISCV.html#RISCV-A04) | 실측 리트머스 → 형식 모델 열거 → RVWMO 규칙 |
| SPSC 큐 | [DS-A01](../tracks/DS.html#DS-A01) · [LLAT-M01](../tracks/LLAT.html#LLAT-M01) · [RUST-M07](../tracks/RUST.html#RUST-M07) | 정확성 → 지연 튜닝 → ISR 생산자 + 타입으로 보장 |
| 재진입성 | [C-A04](../tracks/C.html#C-A04) · [RTOS-M14](../tracks/RTOS.html#RTOS-M14) | 함수 단위 판별 → RTOS에서 라이브러리 보호 |
| 교착 탐지 | [OS-M11](../tracks/OS.html#OS-M11) · [ALGO-A10](../tracks/ALGO.html#ALGO-A10) · [STOR-M08](../tracks/STOR.html#STOR-M08) | 런타임 락 순서 → 그래프 알고리즘 → DB 트랜잭션 |
| false sharing | [ARCH-M08](../tracks/ARCH.html#ARCH-M08) · [MEM-M04](../tracks/MEM.html#MEM-M04) · [LPERF-A03](../tracks/LPERF.html#LPERF-A03) | 재현 → 코어 수별 실측 → perf c2c로 리눅스에서 찾기 |

## 참고자료

- Paul E. McKenney, *Is Parallel Programming Hard, And, If So, What Can You Do About It?* — 락·RCU·메모리 순서 전반
- Mara Bos, *Rust Atomics and Locks* (2023) — 언어와 무관하게 atomics·락 구현을 가장 쉽게 설명
- Herlihy & Shavit, *The Art of Multiprocessor Programming* — lock-free 자료구조와 선형성
- ARM, *Learn the architecture: Memory Systems, Ordering, and Barriers* — DMB/DSB/ISB, LDAR/STLR
- cppreference, `std::memory_order` — https://en.cppreference.com/w/cpp/atomic/memory_order
- Jeff Preshing 블로그 (preshing.com) — acquire/release, 리트머스 테스트 그림 설명

