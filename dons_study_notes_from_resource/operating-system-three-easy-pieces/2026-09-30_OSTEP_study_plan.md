# OSTEP 공부 계획 · 진도표

> 📚 교재: *Operating Systems: Three Easy Pieces* (Arpaci-Dusseau, v0.91, 675쪽) · 시작일 2026-09-30 · [허브로 돌아가기](site/index.html)

## 0. 목표

- **면접에서 OS 질문을 "그림 그려가며" 설명할 수 있는 수준**: 프로세스/컨텍스트 스위치, 스케줄링, 가상 메모리(페이징 · TLB · 페이지 폴트), 락 · 조건변수 · 세마포어, 데드락, I/O · 인터럽트 · DMA, 파일시스템 · 저널링 · 크래시 일관성.
- 펌웨어 경험(SSD FTL, NVMe, 인터럽트, 전원 손실 복구)을 **OS 용어로 다시 말할 수 있게** — "제가 만든 FTL 매핑 테이블은 OS로 치면 페이지 테이블이고, GC는 LFS 클리너입니다" 같은 문장.
- 다음 단계(AI 가속기 · 로보틱스 시스템)의 바탕: 디바이스 메모리 관리, IOMMU, 커맨드 큐, 실시간 스케줄링을 이해할 토대.

## 1. 공부 루프 (챕터 하나 = 이 6단계)

| 단계 | 할 일 | 시간 |
|---|---|---|
| ① 예습 | 한국어 노트의 **0. 한눈에 보기 + 1. 5분 복습표**만 먼저 읽기 | 5분 |
| ② 원문 | 📖 원문(또는 PDF)을 처음부터 끝까지. 그림은 PDF 링크로 | 30~60분 |
| ③ 노트 | 한국어 노트 본문 — 원문에서 헷갈린 부분 위주로 | 20~30분 |
| ④ 손 | "직접 해보기"의 C 코드 컴파일·실행, 시뮬레이터 명령 따라 치기, 값 바꿔서 한 번 더 | 20~40분 |
| ⑤ 말 | "면접 질문"을 **답 보기 전에 소리 내어** 답해보고, 그다음 답 열기 | 10분 |
| ⑥ 기록 | 아래 진도표에 날짜 · 한 줄 메모, 허브에서 "읽음 표시" | 2분 |

> **TIP — 시뮬레이터는 `-c` 없이 먼저.** OSTEP 숙제 시뮬레이터는 `-c` 를 붙이면 정답을 보여준다. 먼저 손으로 풀고 `-c` 로 채점하는 게 핵심.

실습 환경 (이미 받아둠):

```text
cd .tools/ostep-homework/cpu-intro          # 챕터별 시뮬레이터 (python3)
python3 process-run.py -l 5:100,5:100       # 먼저 손으로 풀고
python3 process-run.py -l 5:100,5:100 -c    # 채점
cc -Wall -Wextra -O0 -pthread code/C05_fork.c -o .work/bin/C05_fork && .work/bin/C05_fork
```

## 2. 로드맵 (주 5일 · 하루 60~90분 기준, 9주)

| 주차 | 챕터 | 손으로 할 것 | 이번 주가 끝나면 설명할 수 있어야 하는 것 |
|---|---|---|---|
| W1 | [02](notes/2026-09-30_C02_intro.md) · [04](notes/2026-09-30_C04_process.md) · [05](notes/2026-09-30_C05_process_api.md) · [06](notes/2026-09-30_C06_limited_direct_execution.md) | process-run.py, fork/exec/pipe 코드, syscall 비용 측정 | 시스템 콜이 유저→커널로 넘어가는 정확한 과정, 타이머 인터럽트와 컨텍스트 스위치 |
| W2 | [07](notes/2026-09-30_C07_scheduling_intro.md) · [08](notes/2026-09-30_C08_mlfq.md) · [09](notes/2026-09-30_C09_proportional_share.md) · [10](notes/2026-09-30_C10_multiprocessor_scheduling.md) | scheduler.py, mlfq.py, lottery.py, multi.py | SJF/STCF/RR 트레이드오프, MLFQ 5규칙, CFS, 캐시 친화성 |
| W3 | [13](notes/2026-09-30_C13_address_spaces.md) · [14](notes/2026-09-30_C14_memory_api.md) · [15](notes/2026-09-30_C15_address_translation.md) · [16](notes/2026-09-30_C16_segmentation.md) · [17](notes/2026-09-30_C17_free_space_management.md) | ASan 버그 잡기, relocation.py, segmentation.py, malloc.py | 가상주소→물리주소 변환을 손으로, 단편화 종류, 할당자 설계 |
| W4 | [18](notes/2026-09-30_C18_paging_intro.md) · [19](notes/2026-09-30_C19_tlb.md) · [20](notes/2026-09-30_C20_smaller_page_tables.md) | 페이지 테이블 크기 계산, TLB 측정 프로그램, multi-level 변환 | **VPN/offset 비트 계산, TLB miss 처리 흐름, 다단계 페이지 테이블** (면접 단골) |
| W5 | [21](notes/2026-09-30_C21_swapping_mechanisms.md) · [22](notes/2026-09-30_C22_swapping_policies.md) · [23](notes/2026-09-30_C23_vax_vms.md) · [B](notes/2026-09-30_C0B_virtual_machine_monitors.md) | paging-policy.py, LRU/Clock 구현 | 페이지 폴트 전체 경로, LRU 근사(Clock), COW, 스래싱 |
| W6 | [26](notes/2026-09-30_C26_concurrency_intro.md) · [27](notes/2026-09-30_C27_thread_api.md) · [28](notes/2026-09-30_C28_locks.md) · [29](notes/2026-09-30_C29_concurrent_data_structures.md) | race 재현, spin/ticket lock 구현 + 벤치 | TAS/CAS/LL-SC, 스핀 vs 슬립, futex, 확장 가능한 카운터 |
| W7 | [30](notes/2026-09-30_C30_condition_variables.md) · [31](notes/2026-09-30_C31_semaphores.md) · [32](notes/2026-09-30_C32_concurrency_bugs.md) · [33](notes/2026-09-30_C33_event_based_concurrency.md) · [D](notes/2026-09-30_C0D_monitors.md) | bounded buffer, 세마포어 직접 구현, 데드락 재현, kqueue | **생산자-소비자를 화이트보드에 정확히**, while 루프 이유, 데드락 4조건 |
| W8 | [36](notes/2026-09-30_C36_io_devices.md) · [37](notes/2026-09-30_C37_hard_disk_drives.md) · [38](notes/2026-09-30_C38_raid.md) · [39](notes/2026-09-30_C39_files_and_directories.md) · [I](notes/2026-09-30_C0I_flash_ssd.md) | disk.py, raid.py, 파일 syscall, 장난감 FTL | 인터럽트 vs 폴링, DMA, RAID-5 small write 비용, FTL과 GC (홈그라운드!) |
| W9 | [40](notes/2026-09-30_C40_file_system_implementation.md) · [41](notes/2026-09-30_C41_ffs.md) · [42](notes/2026-09-30_C42_crash_consistency_journaling.md) · [43](notes/2026-09-30_C43_lfs.md) · [44](notes/2026-09-30_C44_data_integrity.md) | vsfs.py, WAL 구현 + 크래시 재생, 체크섬 비교 | inode/블록 계산, **저널링 순서와 크래시 시나리오**, LFS ↔ FTL |
| (선택) | [47](notes/2026-09-30_C47_distributed_systems.md) · [48](notes/2026-09-30_C48_nfs.md) · [49](notes/2026-09-30_C49_afs.md) | UDP 재전송 코드 | 멱등성, stateless 프로토콜, 캐시 일관성 |

### 면접이 가까울 때 — 빠른 코스 (2주)

06 → 07 → 08 → 18 → 19 → 20 → 21 → 26 → 28 → 30 → 31 → 32 → 36 → 42 → I. 각 챕터의 **1. 5분 복습표 + 면접 질문**만 돌아도 핵심은 커버된다.

## 3. 진도표

| 날짜 | 챕터 | 상태 | 한 줄 메모 (헷갈린 것 · 깨달은 것) |
|---|---|---|---|
| 2026-09-30 | 세팅 | ✅ | PDF → 원문 md 59개 + 한국어 노트 + 시뮬레이터 준비 |
| 2026-09-30 | 02 소개 | ▶️ 시작 | |
| | 04 프로세스 | ⬜ | |
| | 05 프로세스 API | ⬜ | |
| | 06 LDE | ⬜ | |

(이후 행은 공부하면서 추가 — "Ch.NN 끝냈어"라고 말하면 Claude가 여기와 허브를 갱신)

## 4. Day 1 — 오늘 할 것

1. [Ch.02 OS 소개 노트](notes/2026-09-30_C02_intro.md)의 0~1절(5분) → [원문 02](book-md/C02_introduction_to_operating_systems.md) 읽기. 책 전체 지도(가상화 · 동시성 · 영속성)를 머리에 넣는 챕터.
2. 원문 02의 `cpu.c` · `mem.c` · `threads.c` · `io.c` 예제를 직접 돌려보기: `.tools/ostep-code/intro/` (Makefile 있음). 특히 `threads.c` 를 `loops=100000` 으로 여러 번 돌려 **결과가 매번 달라지는 것**을 눈으로 확인 — 이게 Part II 동시성의 출발점.
3. [Ch.04 프로세스](notes/2026-09-30_C04_process.md) → `process-run.py` 숙제 1~4번을 손으로 풀고 `-c` 로 채점.
4. 면접 질문 소리 내어 답하기 → 진도표에 한 줄.

## 5. 이 책을 읽을 때 Don이 특히 볼 것

- **Mechanism vs Policy** 구분이 책 전체를 관통한다. FW에서도 "GC를 *어떻게* 하나(mechanism)"와 "*언제 · 어떤 블록*을 GC하나(policy)"는 따로 설계했다 — 같은 틀로 읽으면 빠르다.
- 가상 메모리 파트(13~23)는 IOMMU · GPU/NPU 페이지 테이블 · 유니파이드 메모리를 이해하는 데 그대로 쓰인다 — AI 가속기 면접에서 가장 많이 연결되는 부분.
- 동시성 파트(26~33)는 펌웨어의 ISR ↔ 태스크 공유 데이터, 멀티코어 컨트롤러의 락 설계와 1:1로 대응한다. 직접 겪은 레이스/데드락 버그를 하나씩 OS 용어로 다시 정리해 두면 행동 면접 스토리가 된다.
- 영속성 파트(36~44 + 부록 I)는 홈그라운드. 대신 **호스트(OS) 쪽 시점**에서 SSD를 보는 연습: 왜 fsync가 비싼가, 저널링 FS 위의 FTL은 쓰기를 몇 번 증폭하는가(log-on-log).
