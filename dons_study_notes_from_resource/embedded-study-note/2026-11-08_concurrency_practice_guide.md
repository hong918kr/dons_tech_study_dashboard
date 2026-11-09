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

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


4층 6개 레슨이 모두 있다. 6개 모두 독립 검증 통과: 시작 코드 FAIL → 정답·추가 연습 PASS, ASan/UBSan·TSan 깨끗 (OS-A01은 Lima VM에서 gcc·clang 둘 다). 기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), Apple clang 21. 6개 레슨이 **동시에 벤치마크를 돌렸다** (load 3–10) → 절대 ns 말고 **배수**만 믿는다. 아래는 환경 한계와 아직 확인 안 한 주장이다. 틀린 곳을 찾으면 그 레슨을 `need to fix`로 표시해 둔다.

| 아이템 | 레슨에서 실측한 것 (한 줄) | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|---|
| [OS-M07](../tracks/OS.html#OS-M07) | Apple clang 기본 = LSE(`swpa`/`casa`/`ldadd`), `-mno-outline-atomics`·`+nolse`로는 안 바뀌고 `-mcpu=apple-m1+nolse`여야 LL/SC. 12스레드 TAS: LSE 3.1 vs LL/SC 19 Mops/s (~6×). ticket = 공정(Jain 0.999)·FIFO 20/20이지만 24스레드 과구독에서 170× 붕괴(convoy). `yield`는 0.22 ns(사실상 no-op), `isb` 7.8 ns | 코어 고정 불가(6+스레드는 E 코어로), 공정성 CoV가 실행마다 0.2–0.7로 흔들림, LL/SC 내부 `stxr` 재시도는 카운트 안 됨. 아이템 task의 "M2"는 M6로 측정 | LSE `swpa` TAS가 느린 원인(가설), Rust `spin_loop()`=`isb`, qspinlock 도입 버전(x86 ~4.2 / arm64 ~4.19), arm64 `smp_cond_load_acquire`의 `wfe`, RP2040 SIO 스핀락 세부(§2.3.1.3)·RP2350 E2 erratum, macOS `sched_yield` 동작. MCS/CLH는 전용 아이템 없음 → DS-A01/A03/A04 참조만 |
| [OS-A01](../tracks/OS.html#OS-A01) | mutex2 비경합 = syscall 0번 2.7 ns vs 항상 wake 버전 152 ns (57×, strace 100001 vs 1). naiveq lost wakeup은 강제 스케줄에서 매번, round 모드에서 자연 발생 353/353. **glibc 2.43이 실제로 0/1/2 + xchg** (`__lock` 값 0→1→2→0, `FUTEX_WAIT_PRIVATE, 2`, objdump). 타임아웃 있는 wait는 SA_RESTART여도 EINTR | 숫자는 VM vCPU 기준(물리 코어 아님), 다른 프로세스 `/proc/<pid>/syscall` EPERM, 맥·VM 숫자는 서로 비교 불가 | 커널 waitwake.c의 `smp_mb` 위치·"Futex Ordering Guarantees" 주석(소스 미확인), ERESTART_RESTARTBLOCK 설명, glibc 주소↔함수 매핑·`__libc_single_threaded` 분기, os_unfair_lock 값=mach thread port, Zephyr `sys_sem`→`k_futex`, FreeRTOS-MPU SVC, `mutex_spin_count` tunable. Drepper v1.6(2011-11-05)은 PDF로 확인함 |
| [MCPP-A03](../tracks/MCPP.html#MCPP-A03) | 이 맥 libc++ `wait` = 64회 폴링 → 4 µs 시계 스핀 → `__ulock_wait` (`os_sync_wait_on_address`는 안 씀). **int64만 진짜 `notify_one`**, uint32/ptrdiff_t(=std 세마포어·래치)는 전역 해시 테이블 + 전부 깨움 (1/4 vs 4/4 깨어남). notify 생략 이득 &lt; 1 ns. std::counting_semaphore의 +13 ns = `acquire` 안의 시계 읽기. lost wakeup 3패턴 재현 | 동시 벤치마크(load 3–5), B2 중앙값은 41.67 ns 타이머 틱으로 양자화, 코어 고정 불가, MCU 실행 없음 | `UL_COMPARE_AND_WAIT64`/`ULF_WAKE_ALL` 이름(값 5/0x105만 관측), macOS pthread_mutex 핸드오프가 드문 이유, B3의 ~69 µs 모드 원인, B1' uint32 느림=전역 테이블 경합, freestanding의 wait/notify 포함 여부, timed wait 제안 상태, [atomics.wait]/4 문단 번호, FreeRTOS event group 8/24비트 |
| [LLAT-M02](../tracks/LLAT.html#LLAT-M02) | 4 reader에서 seqlock 704 vs rwlock 9.1 Mreads/s. 1M/s writer: seqlock 300k 다 씀 vs rwlock 42k. **fence 빼면 이 칩에서 실제로 찢어짐** (reader쪽 0.035%, writer쪽 0.30%, odd 검사 없음 4.4–5.1%, writer 2개 13.5%). writer 풀가동 시 reader 굶주림(33%가 64+ 재시도). Apple clang acquire load = `ldapr` | 코어 고정 불가, 타이머 틱 41.7 ns → 64회 배치 평균, 동시 벤치마크 | **TSan이 memcpy 경로 레이스를 못 잡음** (원인 미조사), memcpy(UB) 변형 0 torn 이유, writer쪽 tear가 10× 많은 이유, macOS rwlock 내부(writer 선호), Linux `seqcount_LOCKNAME_t` PREEMPT_RT·KCSAN, P1478 상태, `ldsetl` reader 확장성, v6-M seqlock(M0+ 미빌드) |
| [OS-A03](../tracks/OS.html#OS-A03) | 즉시 free 버전: ASan 없이는 **"v77, checksum OK"** (재사용된 블록을 정상처럼 읽음) → ASan UAF 보고. `+2`→`+1` off-by-one 돌연변이는 강제 스케줄 테스트가 매번 잡고 스트레스만으론 2–18건. reader 진입 SB 리트머스: seq_cst 0 / acq-rel 2/12M / relaxed 7–12만. 10 reader에서 EBR 7709 vs rwlock 10.5 Mops/s. reader 200 ms 정지 시 cap 없는 EBR 6 MB 고정 | demo_sb 하네스는 두 스레드 겹침이 작아 **0 vs 0 아님**만 보기, 동시 벤치마크 | macOS rwlock 내부(측정으로 추정), 캐시 라인 전송 비용 "수십~수백 ns"는 대표값, 커널 RCU 세부(PREEMPT_RCU·Tree RCU·stall 경고), liburcu flavour 이름/기본값, 단일 코어 펌웨어 ISR reader 주장(추론), Fraser 2004·Hart 2007·Desnoyers 2012 인용, 슬롯 패딩 제거 효과 미측정 |
| [LLAT-M05](../tracks/LLAT.html#LLAT-M05) | 왕복 p50: P–P 74–90 ns, P↔E 200–400 ns, 잠자는 핸드오프 2.5–3.5 µs → 비율 spin : P↔E : sleep ≈ 1 : 3–4 : 30–40. QoS는 힌트일 뿐 (BACKGROUND는 100% E, USER_INTERACTIVE도 부하 시 44–78% E). 병렬 카운터는 E 코어가 끼면 **128 B**에서야 false sharing 사라짐 → 128 B 패딩. relaxed mailbox는 컴파일러가 store를 루프 밖으로 빼서 2M/2M 오답 | 맥은 코어 고정 불가(QoS + cpu 번호 로깅이 대안), VM 숫자는 vCPU(평평한 토폴로지, 가상 IPI로 교차 vCPU 깨움 부풀려짐), 배치 측정이 p99를 평탄화 | cpu 번호↔코어 종류 매핑(0–5 E, 6–7 Super, 8–11 P)은 타이밍 추정, Super↔P가 Super–Super보다 빠른 이유, E–E 편차=DVFS 추정, 64 B vs 128 B 경계 설명(가설), WFE 1.3 µs 자동 깨어남 원인, `__cmpwait` 세부, RP2040 SIO FIFO, STM32H7 M7↔M4 캐시 관리 |

</details>

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

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


5층 9개 레슨이 모두 있다 (DS-M10은 DS Medium 때 작성, 나머지 8개는 이번에 작성). 8개 모두 독립 검증 통과: 시작 코드 FAIL → 정답·추가 연습 PASS, ASan/UBSan·TSan 깨끗 (RUST-M07은 `cargo test` + loom + clippy, Miri·sanitizer는 nightly 전용이라 미실행). 기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), Apple clang 21, rustc 1.99.1. 8개 레슨과 4층 세션이 **동시에 벤치마크를 돌렸다** (load 4–16) → 절대 ns 말고 **배수**만 믿는다. 이 층의 공통 결론: **lock-free가 항상 빠르지는 않다** — 일이 없는 경합에서는 뮤텍스가 이긴 경우가 여럿이다 (DS-A03 W=0, DS-A04 8스레드, RUST-M07). lock-free의 진짜 이점은 "막히지 않음"(ISR·실시간)과 일이 섞인 실제 부하에서 나온다. 틀린 곳을 찾으면 그 레슨을 `need to fix`로 표시해 둔다.

| 아이템 | 레슨에서 실측한 것 (한 줄) | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|---|
| [DS-A01](../tracks/DS.html#DS-A01) | 전부 relaxed면 이 칩에서 **실제로 깨짐** (stale ≈0.1%, 2e7당 13k–24k). head 쪽만 relaxed는 6천만 개에서 0이지만 TSan이 잡음. 캐시된 인덱스 ≈13배 (cap 1024: 18.8 → 244 Mops/s). arm64 acquire = `ldapr`, M4 = `ldr; dmb sy` | load 5–8에서 측정, 코어 고정 불가 → 배수만 보기. cap 65536은 실행마다 크게 흔들림. Mac에 arm-none-eabi-gcc 없음 → M0 출력은 clang 것만 (relaxed도 `__atomic_load_4` 호출) | torn 0인 이유 = LSE2 16B 단일 복사 원자성(미확인), cap 65536 흔들림 = 클러스터 배치(추정), DDI 0403 절 번호, Linux kfifo 세부, STM32 USART ORE 동작 |
| [DS-A03](../tracks/DS.html#DS-A03) | 생산자가 CAS와 seq store 사이에 멈추면 **모든 소비자가 막힘** (1000회·10만회 시도 모두 빈손) → 엄밀한 lock-free 아님을 강제 재현. seq를 relaxed로 하면 이 칩에서 **실제로 깨짐** (4M당 8–19개 유실+중복). 일이 없으면(W=0) 뮤텍스 큐가 이김 (4×4: 18.1 vs 10.9 Mops/s), 메시지당 ≈50 ns 일이 있으면 Vyukov ≈3배. LSE면 weak·strong CAS 모두 `cas` 한 개 | load 4.3–9.3, 코어 고정 불가 → 1×1은 ±50%. TSan 리포트에 `vq_enq` 프레임 보려면 `-fno-inline` 필요 | 1024cores 원문 "not lock-free" 문구, 뮤텍스가 이기는 이유(잠든 대기자 = 배치 효과, 추정)·≈11 Mops 포화 이유(공유 pos 라인 경합, 추정), DPDK `rte_ring`·rigtorp·crossbeam `ArrayQueue` 세부, ESP32 `S32C1I`, C11 7.17.3·5.1.2.4 문단 |
| [DS-A04](../tracks/DS.html#DS-A04) | 강제 스케줄에서 ABA 재현: T1 CAS가 첫 시도에 성공 → B가 스택에도 있고 main도 들고 있음 → B→B 순환, C·D 유실. 강제 없이 8스레드 경합만으로도 순진한 스택은 5/5 깨짐, 64비트 태그는 0/20. 손으로 짠 LDAXR…STLXR은 ABA에서 STLXR 1000/1000 실패. 16B CAS = `caspa/caspl` (lock-free). 8스레드에서 lock-free 스택이 **뮤텍스보다 ≈8배 느림** (LL/SC 빌드는 LSE보다 빠름). 32비트 태그는 ≈20 s면 한 바퀴 | load 9–14, 코어 고정 불가 → 2·4스레드 열은 노이즈 큼. Cortex-M은 asm만 (실리콘 없음) | 경합 시 뮤텍스가 이기는 이유(추정), relaxed push 깨짐 원리(추론), sleep 시 STLXR 실패 = 문맥 교환이 모니터 클리어(추정), x86 libatomic 내부 락, FreeRTOS 풀 critical section, Herlihy & Shavit 장 번호, TSan의 16B atomic 계측 |
| [LLAT-M01](../tracks/LLAT.html#LLAT-M01) | **패딩만 하면 오히려 느려짐** (v0 40–58 → v1 18–30 Mops/s, 4/4회). 캐시된 인덱스 114–160, 배치 B=32 1070–1510 (v0의 26–30배). 왕복 p50 v0 230 ns → v2 86–96 ns (≈LLAT-M05 핑퐁 바닥). 메시지가 띄엄띄엄이면 배치가 지연을 망침 (p50 83 ns → 7–8 µs, 2 µs 상한 두면 ≈1.1 µs). `hardware_destructive_interference_size` = **256** (`hw.cachelinesize`는 128) | load 4.8–13, 코어 고정 불가 → 배수만. 일부 실행에서 꼬리가 ms까지 (선점으로 해석). macOS에서 PMU 카운터 못 읽음 → 미스 수는 세지 못함 | v1이 v0보다 느린 이유·v4 왕복이 v2보다 나쁜 이유 = 캐시 라인 수 논증(카운터 미측정), 256 vs 128 패딩 차이 노이즈 안, ui/bg 처리량 136↔233 흔들림 설명 못 함, rigtorp/FastForward/MCRingBuffer/LMAX 인용(기억), RP2040 SIO FIFO 깊이 |
| [LLAT-M03](../tracks/LLAT.html#LLAT-M03) | 바로 해제하는 버전 → 강제 스케줄에서 **진짜 ASan heap-use-after-free** (ASan 없으면 malloc이 주소를 재사용해 우연히 맞는 답). 읽기 경로 비용 HP 1.44× / EBR 1.04× (plain 대비). 한 스레드가 200 ms 멈추면 **EBR 미회수 255k 노드(≈6 MB) vs HP 최대 32** (상한 N·R=100). 올바른 publish/re-check = `stlr; ldar` | load 4.5–12, 코어 고정 불가. 큐 처리량은 HP·EBR·뮤텍스 모두 5–7 Mops/s로 순위가 실행마다 바뀜 → 처리량으로는 비교 못 함. EBR은 데모용 최소 버전 (전체는 OS-A03). release/acquire로 약화해도 2M×10회 UAF 0 (증거 아님) | C++26 `<hazard_pointer>`/`<rcu>` API 이름·P2530/P2545 채택 연도(libc++ `<version>`의 202306L에서 추론, 헤더는 없음), folly hazptr API, 2024 Linux HP RFC, P2414, 상한 N·R 유도(직접), 듀얼코어 MCU CAS 지원 여부 |
| [RUST-M07](../tracks/RUST.html#RUST-M07) | loom은 슬롯이 `UnsafeCell`일 때 오더링 하나만 약화해도 **전부 잡음** (Causality violation + 줄 번호). 슬롯이 atomic이고 head만 약하면 **못 잡음** (load buffering, loom이 모델링 못 함). 단순 SPSC ≈20 Mops/s ≈ `Mutex<VecDeque>`, std `sync_channel`이 4–6배, 캐시된 인덱스 2.5–3배. thumbv6m에서도 빌드됨 (load/store만 씀) | Miri·sanitizer는 nightly 전용 → 미실행 (loom + 스트레스·drop 카운트로 대체). 호스트 "ISR" = 진짜 병렬 스레드라 실제 ISR보다 가혹. 벤치 ±30% (load 6.3–6.6) | std mpsc = crossbeam 기반(1.67~, 문서 기준), ARMv8이 이 LB 형태를 제어 의존성으로 금지(절 번호 미확인), 느린 원인 = 캐시 라인 핑퐁(가설), rustc가 `ldapr` 내는 이유 = 기본 apple-m1 CPU(추정) |
| [OS-A05](../tracks/OS.html#OS-A05) | `take`에서 fence를 빼면 이 칩에서 **실제로 태스크 중복** (10M 라운드당 11·40·23회), 논문 fence(`dmb ish`)·`stlr/ldar`는 0. 잘게 쪼갠 fib(34) 12워커: Chase-Lev 7.0×, 워커별 뮤텍스 deque 4.6×, **전역 뮤텍스 큐 0.07×**, Chase-Lev + 전역 pending 카운터 0.54× (카운터 하나가 직렬보다 느리게 만듦) | load 7–16, 코어 고정 불가. 12워커에서 일부 워커는 태스크 0개 (늦은 시작/E코어). false sharing 비용은 미측정 (see:MEM). TSan은 fence 누락 버그를 못 잡음 (relaxed bottom은 잡음) | push를 논문의 release fence 대신 release store로 씀 → C11 같은 스레드 release sequence 규칙(N1570 5.1.2.4p10)에 기대는데 C++20은 이 규칙 삭제(P0982R1). 고정 배열에서 steal의 top acquire 필요성(미증명), push의 top acquire LB 논증(관측 안 됨), TBB·Rayon·Go·Tokio·ForkJoinPool 내부, ROS 2 `MultiThreadedExecutor`가 work-stealing 아님(기억) |
| [LLAT-A06](../tracks/LLAT.html#LLAT-A06) | 호출당 nlog 9–12 ns vs snprintf ≈146, fprintf ≈200, spdlog 165–245 ns (**14–23배**), p99 42 ns vs ≈2.8 µs. 버스트에서는 소비자 속도가 상한 (텍스트 ≈4 vs 바이너리 ≈48 M msg/s) → DROP이면 95% 유실, BLOCK이면 p99.9 ≈65 µs. `const char*`만 저장하는 로거는 **진짜 ASan heap-use-after-free** | `cntvct_el0`가 ≈42 ns마다만 바뀜 → 단일 호출 백분위는 42 ns 격자. load 6–9, 코어 고정 불가. spdlog v1.17.0은 /tmp 클론 header-only로만 씀 (설치 안 함). 타깃 newlib printf 크기 미측정 | NanoLog 논문 수치(≈8 ns, 80 M msg/s)·가득 참 시 block·`%s` 복사, Trice/RTT/ITM/defmt/Zephyr 세부, spdlog µs p99 원인(condvar notify), Linux initial-exec TLS = `mrs tpidr_el0`, 코어 간 `cntvct_el0` 일관성, newlib printf 플래시 크기 |

</details>

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

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


6층 7개 레슨이 모두 써졌다. 모두 독립 검증을 통과했다: 시작 코드 FAIL(exit 1, 멈추지 않음) → 정답·추가 연습 PASS. C 5개는 엄격 빌드, ASan/UBSan, TSan이 모두 깨끗하고 정답을 5회 반복 실행해도 같다. Python 2개(SOCX-M05·DCN-M05)는 `-W error`로 경고가 없고 결정적이라 기계 부하와 무관하게 결과가 같다. 레슨마다 "같은 순환 대기, 다른 층" 표가 있어 서로 id로 이어진다. 벤치마크는 load 6–13에서 돌렸으므로 **비율만 믿는다**. 기계: Apple M6 (2+4+6코어), Apple clang 21. 틀린 곳을 찾으면 그 레슨을 `need to fix`로 표시해 둔다.

| 아이템 | 레슨에서 실측한 것 (한 줄) | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|---|
| [OS-M10](../tracks/OS.html#OS-M10) | 강제 스케줄로 매번 `P0 holds F0 waits F1 … P4 holds F4 waits F0` 교착을 재현한다. n=12에서 trylock+랜덤 백오프가 가장 빠르고 고르다(0.7–0.8M meals/s, max/min 1.2–1.4). 전역 락 순서는 교착은 없지만 심하게 불공평하다(max/min 최대 7.5만). | 맥에는 `pthread_mutex_timedlock`·`pthread_barrier_t`·`sem_init`이 없어 모두 mutex+cond로 만들었다. 고정 백오프 livelock은 자유 실행에서는 안 나오고 lockstep 강제 스케줄에서만 보인다. | 락 순서 방식의 기아 원인 해석(대기 사슬+barging), seated n−1이 가장 공평한 이유, Linux `double_lock`의 주소 순서, Chandy–Misra 세부(TOPLAS 1984), FreeRTOS "락 순서 검사 없음"(V11.3.2 일부만 읽음) |
| [OS-M11](../tracks/OS.html#OS-M11) | ABBA를 **순차 실행해도** 두 간선의 최초 호출 스택과 함께 보고한다. 동시 실행에서는 4/4 실제 교착. 비용: 락 하나당 plain 11 ns, mini lockdep 34 ns. 8스레드에서 알려진 간선 캐시가 없으면 29배 느리다. | **맥 TSan은 `detect_deadlocks=1`이어도 lock-order-inversion을 보고하지 않는다** → TSan·helgrind 출력은 Lima VM에서 얻었다(둘 다 클래스가 아니라 객체 주소 단위). VM 커널에 `CONFIG_PROVE_LOCKING`이 꺼져 있어 진짜 lockdep은 못 돌렸다. Linux에선 `-rdynamic` 없으면 backtrace에 이름이 없다. | lockdep DFS→BFS(`__bfs`) 전환 시기, `check_noncircular`/`lookup_chain_cache` 이름, trylock 간선 처리, gate-lock 미필터, 보고 헤더 문자열, `lock_class_key` 규칙, Zephyr `CONFIG_SPIN_VALIDATE` 범위 |
| [ALGO-A10](../tracks/ALGO.html#ALGO-A10) | 반복형 Tarjan과 Kosaraju가 무작위 그래프 500개에서 전이 폐쇄 기준 답과 일치(불일치 0)하고 10^6 정점 사슬도 처리한다. 재귀 Tarjan은 -O2에서 1e5 정점이면 SIGSEGV(8 MB 스택). 펌웨어 로그에서 3-락 사이클과 gate된(무해) 사이클을 구분해 보고한다. | 맥 ASan은 LeakSanitizer(`detect_leaks=1`)를 지원하지 않는다 → 누수 검사는 Linux에서. 재귀 붕괴 지점은 빌드에 따라 다르다(-O2 프레임 112 B, -O0 48 B). 스레드가 없어 TSan은 해당 없다. | Linux lockdep이 직전 보유 락에서만 간선을 추가하는지, Sharir 1981 / Havelund 2000 / Bensalem–Havelund 2005 서지, SystemView·Tracealyzer 뮤텍스 이벤트 기록 여부 |
| [STOR-M08](../tracks/STOR.html#STOR-M08) | ABBA 스크립트에서 희생자는 정확히 1개(가장 젊은 T2), 같은 ts로 재시도. 8스레드 은행 이체: 총액이 매번 정확하고, abort 수 = 찾은 사이클 수. "읽고 나서 X로 승격"은 교착이 끊임없이 나서 abort가 commit보다 많다. 정렬 순서로 잠그면 교착 0, 처리량 7배 이상. | 맥에 `pthread_condattr_setclock`이 없어 대기 마감을 `CLOCK_REALTIME`으로 잡았다. 탐지 자체의 비용은 두 실행에서 1.29× / 0.91×로 갈려 결론을 내지 않았다. | PostgreSQL `deadlock_timeout` 1 s와 hard/soft edge, InnoDB `innodb_deadlock_detect`·가중치 희생자·50 s 타임아웃, SQL Server lock monitor 5 s, Spanner wound-wait, 1976–78 논문 서지 |
| [STOR-M10](../tracks/STOR.html#STOR-M10) | 제자리 R→W 승격을 두 스레드로 강제하면 `LATCH DEADLOCK (upgrade)`를 보고한 뒤 복구한다. 8스레드 read-heavy에서 optimistic 3.39 > crabbing 1.37 ≈ 전역 rwlock 1.33 Mops/s. 부모를 먼저 놓는 버그는 TSan에 안 잡히고(데이터 레이스 아님) 강제 스케줄로만 결정적으로 보인다. | **맥 pthread rwlock은 R을 쥔 채 `wrlock`하면 단일 스레드에서도 영원히 멈춘다**(EDEADLK 없음). `pthread_rwlock_timedwrlock`이 없다. 코어 고정이 안 돼 T=1 측정이 흔들린다. crabbing 조회가 스레드 수와 함께 늘지 않는다(6.6→6.4). | macOS `rdlock`이 락 워드에 atomic RMW를 한다는 설명(소스 미확인), 경합 W 대기자가 커널(psynch)에서 잔다는 추론, PostgreSQL nbtree의 Lehman–Yao 기반 여부, OLC 논문(Leis et al., DaMoN 2016) 서지, SSD L2P 일반론 |
| [SOCX-M05](../tracks/SOCX.html#SOCX-M05) | 4×4 메시 XY의 CDG는 간선 68개로 비순환이고 손 계산 공식과 일치한다. torus DOR은 순환이고 dateline VC 2개를 쓰면 비순환이다. 1-VC 링은 cycle 3에 교착(16개 중 0개 전달)하고, dateline이면 cycle 18에 16/16. 버퍼 깊이를 8로 늘려도 교착한다(쥐는 자원은 버퍼 칸이 아니라 VC 소유권). | 시뮬레이터 단순화: VC는 tail이 떠날 때만 해제, credit 지연 1 cycle, ejection 무제한. wait-for 탐지는 결정적 라우팅에서만 유효하다(적응형은 knot 탐지 필요, 설명만). | Dally–Seitz "⇒" 방향의 정확한 진술과 Schwiebert–Jayasimha 후속, Glass & Ni "16개 중 12개"(도구로 재현은 됨), Chiu 2000 odd-even 의사코드, Disha·bubble flow control, AMBA CHI 채널 구분 |
| [DCN-M05](../tracks/DCN.html#DCN-M05) | 링크 2개 장애 후 bounce 경로가 버퍼 4개짜리 사이클을 만든다. t=82에 교착, 소스를 멈춰도 64패킷이 갇히고 drop 0(혼잡과 구분되는 증상). 대책별 결과: up-down 1218 전달, 우선순위 상승 632, watchdog 74 전달·108 drop(교착이 반복됨). XOFF를 늘리면 교착이 늦어질 뿐이다(t=82→562). | 모델 단순화: 입력 FIFO(HOL 블로킹), 링크·PAUSE 지연 1 cycle, 공유 버퍼 풀과 PAUSE quanta 없음. headroom은 1패킷(실제 headroom 계산은 DCN-M04). | Guo et al. SIGCOMM 2016 내용(MAC flooding 원인), Hu et al. HotNets 2016, Tagger(CoNEXT 2017) 태그 규칙, 벤더/SONiC PFC watchdog 기본값·동작, Autonet up*/down* 서지 |

</details>

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

<details>
<summary>작성 메모 — 2026-11-08 레슨 작성 후 확인할 것</summary>


7층 10개 레슨이 모두 써졌다. 모두 독립 검증을 통과했다: 시작 코드 FAIL(exit 1, 멈추지 않음) → 정답·추가 연습 PASS. C 6개는 엄격 빌드와 ASan/UBSan이 깨끗하고, 스레드를 쓰는 3개(ARCH-M08·MEM-M04·ARCH-A05)는 TSan도 깨끗하다. Python 2개(MEM-M07·RISCV-A04)는 `-W error`로 경고가 없고 출력이 결정적이다. GPGPU 2개는 호스트 코드만 ASan/UBSan을 돌렸다 (GPU 커널은 계측되지 않는다). 기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), Apple clang 21. 10개 레슨과 5·6층 세션이 **동시에 벤치마크를 돌렸다** (load 4–14) → 절대 숫자 말고 **배수**만 믿는다. 위 면접 체크의 `alignas(64)`는 x86 기준이고, 이 맥에서는 ARCH-M08 실측대로 **128 B**가 안전하다.

**need to fix 후보:** [ARM-M05](../tracks/ARM.html#ARM-M05) 본문은 `stlr;ldapr` SB를 "13회 중 9회 관찰"이라고 쓰는데(3곳), 같은 레슨의 표 숫자로는 7회다 (MEM-M07 작성 중 발견). 아직 고치지 않았다.

| 아이템 | 레슨에서 실측한 것 (한 줄) | 환경 한계 (고칠 필요 없음, 알고 보기) | 확인 안 된 주장 (검토 후보) |
|---|---|---|---|
| [ARCH-M08](../tracks/ARCH.html#ARCH-M08) | 같은 라인 `ldadd`는 약 8배 느리다 (11.1–11.5 vs 1.40 ns). 같은 클러스터(E–E, S–S, S–P, P–P)는 64 B면 깨끗하고, **E↔S/P는 128 B여야 깨끗하다** → 128 B로 패딩. 12스레드 stride 4 B는 71 ns. `pq_t` 재배치로 약 12배. volatile `str`만 쓰면 1.6–2.2배뿐. clang `__GCC_DESTRUCTIVE_SIZE` 256 / CONSTRUCTIVE 64 | 코어 고정 불가. bg QoS도 Super에 자주 올라가서(E 0–55%) 코어 종류별로 묶어 분류했다. E–E 표본이 적다. cpu 번호↔코어 종류 매핑은 타이밍 추정. load 6–7 | 64/128 경계가 갈리는 이유(클러스터별 소유권 단위 가설), store-buffer 병합 설명, Super·P의 L2 공유 여부, Linux `____cacheline_aligned_in_smp`·arm64 `L1_CACHE_SHIFT 6`, M7 32 B 라인(TRM), STM32H7 M4 D-cache 없음, x86 인접 라인 프리페처, Drepper §6.4.1, C11 조항 번호 |
| [MEM-M04](../tracks/MEM.html#MEM-M04) | 공유 `fetch_add` 720 → 161–185 Mops/s (1→2스레드 4.2배 하락 후 12까지 평평). CAS 루프는 `ldadd`의 1/3.5–1/5.7. 스레드별 라인 9.3배, 읽기 전용 10.3배 (12스레드). BACKGROUND QoS면 100% E, E 217 vs P 650 Mops/s. **모델은 곡선 모양은 맞히지만 이동 횟수는 틀린다** (실측 연산당 0.3 vs 모델 1.0: 코어가 약 3.2연산씩 라인을 붙든다) | 코어 고정 불가(E% 과소 집계), cpu→코어 매핑은 LLAT-M05 추정, 절대값 10–40% 변동. 모델에 캐시 용량·축출·타이밍·거리·O/F 상태 없음, clean 라인은 항상 메모리에서 | 라인을 연속으로 붙드는 이유, 라인 이동 약 15 ns 추정, `ldadd`가 far atomic(CHI)인지, 클러스터별 디렉터리/스눕 필터(`cpusperl2`에서 추론), 실패한 Arm CAS의 소유권 획득, x86 CMPXCHG 항상 쓰기, C11 조항 번호, Sorin·Culler 장 번호, PMU 이벤트 이름, STM32H7·i.MX8M 일관성 |
| [ARCH-M12](../tracks/ARCH.html#ARCH-M12) | 버그 4개를 결정적으로 재현 (TX clean 누락, RX invalidate 누락, DMA 중 dirty 축출, DMA 중 추측 refill). 라인을 공유하는 40 B 버퍼: invalidate는 이웃 4 B를 잃고 clean+invalidate는 DMA 데이터 8 B를 망가뜨린다 → `alignas(32)` + 크기 올림만 해결. **옛 CMSIS `core_cm7.h` V5.00은 비정렬 주소에서 마지막 라인을 건너뛴다** (16 B stale, `cachel1_armv7.h` V1.0.3은 수정됨). 돌연변이는 재사용 시 유지보수 생략 하나만 안 잡힌다 (이유는 레슨에) | 보드 없음: 숫자는 시간이 아니라 횟수. 축출 쓰기는 바로 RAM으로, 도어벨은 디스크립터를 즉시 읽는 모델(그래서 DSB 필요). M7 asm은 `-S`만, 실행 안 함 | M7의 Normal 메모리 추측 읽기(TRM/AN4839 미확인), DDI 0487 "언제든 할당 가능" 조항, arm64 Linux가 RX 전 clean으로 바꾼 이유, `__dma_from_device_group_begin` 도입 시기, `dma_alloc_coherent` 리맵, STM32H7 ETH 디스크립터 MPU NC 배치, M7 4-way, Device 도어벨 앞 DMB 충분 여부 |
| [ARCH-A04](../tracks/ARCH.html#ARCH-A04) | 프로토콜 돌연변이 8개가 예상한 불변식·사이클에서 잡히고 랜덤 트레이스도 시드 1에서 전부 검출. 정상 프로토콜은 48만 연산에 위반 0. E: private `BusUpgr` 4→0. O: 생산자-소비자 메모리 write-back 8→0. 패턴 a·b'(false sharing)는 연산당 전송 약 1, b·c는 → 0. 8코어 디렉터리면 브로드캐스트 스눕의 14%(a)·1.8%(c)만 필요 | 단일 스레드 + 원자적 버스 → transient 상태·store buffer 없음. 16 B 라인으로 축소, 기본 4코어(최대 8). 공유 데이터는 S/E 보유자가 아니라 메모리가 공급 | 약 25 Mops/s 추정이 LLAT-B06 24와 맞는 것(우연일 수 있음), Primer 장 번호·`SM^AD` 표기, CHI 상태·DCT, AMD APM MOESI 절, Cortex-A53 L1 MOESI, MESIF(구현 안 함), Apple 프로토콜 미공개 |
| [ARCH-A05](../tracks/ARCH.html#ARCH-A05) | 100만 회당 이상 결과 — M6: SB relaxed 약 34.5만, SB acq/rel 약 500, MP 약 3.4만, IRIW 약 1500, 2+2W 약 8만, **LB 1600만 중 0**. **Rosetta(x86_64): SB 77–99%, MP·LB·IRIW·2+2W 전부 0 = TSO**. C11 금지 칸은 양쪽 다 항상 0. 하네스가 숫자를 크게 바꾼다 (SB relaxed: 반복별 스핀 배리어 170 vs 공통 타임베이스 345,663). x86 seq_cst fence는 `mfence`가 아니라 `lock orl` | 코어 고정·QoS 없음, 약 10개 세션 동시 벤치마크. Rosetta는 진짜 x86이 아니다 (TSO 모델 확인용). TSan 빌드는 허용 칸도 0 → 횟수는 비-sanitizer 빌드로만. `mach_absolute_time` 사용(맥 전용) | Rosetta 하드웨어 TSO 메커니즘, `CELL=4` store-buffer 가설, LB가 드문 이유(store 은퇴 대기), C11 2+2W·IRIW 허용 칸(손 추론), litmus7 옵션, DDI 0487·SDM 절 번호, ARMv8 MCA 개정 시기, Cortex-A9 정오표, STM32H7·nRF5340·RP2040 버스 순서 |
| [MEM-M07](../tracks/MEM.html#MEM-M07) | 리트머스 23개 × SC/TSO/ARMv8이 문헌 결과와 일치하고, 공리적 SC/TSO가 연산적 기계와 전부 같다. M6 실측 19줄(+ARCH-A05 대조표)은 ARMv8 위반 0, TSO로 보면 MP 줄이 위반 → M6는 TSO가 아니다. SB 최소 수정 = 양쪽 `stlr`+`ldar` (= C11 seq_cst 매핑). **plain IRIW는 ARMv8에서 허용** (IRIW+F+F·+acq만 금지). SC⊆TSO⊆ARMv8은 SB+rel+acq 하나만 예외 | herd7 없음(손으로 만든 기대 집합과 비교). 의존성(dob)·RMW(aob)·Device·혼합 크기 없음 → MP+addr은 잘못 허용으로 나온다. M6 숫자는 부하 중 측정이라 0 vs 0 아님만 | 현재 `aarch64.cat` 형태(lob, po;[L];coi), ARMv8 OMCA 시기, Pulte 2018과 Flat의 동치, DDI 0487·SDM 절 번호, Herding cats의 Cortex-A9 CoRR, POWER IRIW+lwsync, x86 C11 매핑(이 레슨에서 컴파일 안 함) |
| [RISCV-A04](../tracks/RISCV.html#RISCV-A04) | clang 21과 gcc 15의 C11 매핑이 rv64gc·ztso 모두 동일하고, 스펙 부록 표와 같다. 차이는 seq_cst store 뒤의 `fence rw,rw` 하나 (Zalasr 매핑과 섞일 때 SB가 깨지는 것을 막음, 열거기로 확인). RVWMO는 MP/SB/LB 이상 결과 허용, `fence rw,rw`는 셋 다 금지, `fence.tso`는 MP·LB만 금지. Ztso = x86-TSO (16변형). **QEMU v10.2.0은 모든 FENCE를 전체 배리어로 바꾼다** → `fence.tso`도 SB 0, QEMU 통과는 약한 fence의 정확성을 증명하지 못한다 (plain SB는 35–54%) | 규칙 r8–r13(LR/SC 짝, 의존성)·I/O·혼합 크기 없음. herd7 없음. RISC-V 실기 없음(arm64 VM 위 QEMU뿐). ARMv8 열은 fence를 가장 싼 DMB로 번역 | 뒤따르는 fence의 psABI 절, Ztso에서 seq_cst fence를 유지하는 이유(추측), Ztso CAS aq/rl 해석, 스펙은 GitHub main c92615e735 기준(20240411판 장 번호 미확인), Zalasr 상태·`lw.aq` RCsc, r1 중복은 이 테스트 집합 안에서만, 단일 hart ISR 주장(추론) |
| [SOCX-B03](../tracks/SOCX.html#SOCX-B03) | 정상 모니터는 모든 인터리빙에서 위반 0. **스핀락 시나리오는 고장 모니터 3개 중 1개만 잡는다** (NO_CLEAR_OTHER 16/56; SHARED_RESV는 카운터 시나리오 2/25에서만, NO_ADDR_CHECK는 끼어드는 master 시나리오 32/132에서만). OKAY를 성공으로 보는 드라이버는 정상 모니터에서도 상호배제가 깨진다 (16/56). 슬롯 1개 모니터는 안전하지만 응답을 정확히 비교하는 테스트는 실패 (합법 구현을 거부) | **IHI 0022 페이지가 안 열려** 규칙은 Arm Community의 AXI3 인용과 VLSI Verify 튜토리얼 기준. 한 번에 트랜잭션 1개, single-beat ≤8 B, master 2개, 재시도 ≤3. RTL 없음 | IHI 0022 절 번호(A3.4.4/A7.2), exclusive 미지원 slave에 쓴 exclusive write의 동작, 자기 일반 쓰기가 자기 예약을 지우는지, 실패한 exclusive write가 예약을 끝내는지(모델 선택), 1–128 B·16 beat 제한, ID에 포트 번호 추가, AHB5 `HEXCL`, MCU SRAM에 global monitor 없음, Cortex-A 캐시 영역 LDXR, AXI5 `AWATOP`, CHI `Excl`, Q1의 버스트 중 오류 |
| [GPGPU-B09](../tracks/GPGPU.html#GPGPU-B09) | plain `c[0]++` 2^20 스레드 = 매번 **1** (atomic은 정확). 배리어 없음: T=32는 0, T≥64는 약 45–50% 틀림. atomic: 1주소 20 vs 8주소 이상 85 Gop/s (한 SIMD 명령에서 같은 주소를 치는 레인 수에 비례). **128 B 간격은 3.2배 느리다** (CPU 습관과 반대). MSL 4.0 device atomic은 relaxed만. `MTL_SHADER_VALIDATION=1`은 `_REPORT_TO_STDERR=1`까지 있어야 출력되고, 범위 초과만 잡고 레이스·배리어 누락은 못 잡는다 | offline `metal` 컴파일러 없음(GPU asm·AIR 없음, 런타임 컴파일). SDK 26.4에 Metal 4.1 enum 없음. GPU를 M03 작성과 공유. sanitizer는 호스트만. load 10–14 | 한 dispatch 안에서 스레드그룹 간 plain store가 안 보이는 이유(가설), atomic 실행 위치, 128 B 간격이 4·16주소에서 들쭉날쭉한 이유, `mem_none`이 맞은 이유, `coherent(device)` 필요 여부, MSL 4.1 acquire/release(컴파일만), simdgroup = 연속 32스레드(관찰), CUDA 주장, MSL 4.1 스펙 자기모순(§6.16.4.5 vs §1.3) |
| [GPGPU-M03](../tracks/GPGPU.html#GPGPU-M03) | uniform에서는 privatization 효과 없음 (둘 다 읽기 상한 140–150 GB/s). zipf 1.47배, 한 bin 몰림 1.48배, SIMD 집계를 더하면 **7.2배**. 비용은 bin 수가 아니라 같은 주소를 치는 레인에서 나온다 (32 bin lane32는 빠름). 작은 입력(수 KiB 이하)에서는 v2가 4–7% 느리고, 16384 스레드그룹이면 uniform에서 진다. 64 KB 커널은 파이프라인 생성 실패 (한도 32 KB). 버그 데모는 no-zero-init만 빼고 20/20 틀림 | GPU 상태가 둘로 갈린다 (v1 uniform 0.378 ms vs 약 0.11 ms, 2초 예열로 빠른 상태 사용; 느린 상태면 v2가 1.42배). `bench size`의 1–4 MiB 줄과 일부 `ntg` 칸은 상태 혼합·GPU 경합. load 4.3–4.5 | 두 상태 = 클럭/전력 상태(추론), 같은 주소 레인 직렬화 메커니즘(비공개), 스레드그룹 메모리 0 초기화 주체와 스펙 보장, M6 메모리 대역폭, PMPP 장 번호, NVIDIA 블로그 출처, ISP 통계 블록 설명, CPU 몰림 느림의 원인(추론), 배리어 앞 `return`의 실제 동작(실행 안 함) |

</details>


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

