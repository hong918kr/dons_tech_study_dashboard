---
id: OS-A06
time: 110분
updated: 2026-11-15
---
# OS-A06 사용자 공간 페이지 폴트 처리: mprotect + SIGSEGV

## 학습 목표

- `PROT_NONE` 으로 예약한 영역 + `SA_SIGINFO` 핸들러 + `mprotect` 로 **커널의 demand paging 을 사용자 공간에서 흉내** 낸다: 첫 접근 → 페이지 "가져오기"(pagein) + `PROT_READ`, 첫 쓰기 → `PROT_READ|PROT_WRITE`. 폴트 수가 **만진 페이지 수와 정확히 같음** 을 테스트로 보인다.
- 쓰기 보호 = **dirty 비트** 로 써서 dirty 페이지만 저장하는 **증분 체크포인트** 와 `restore(epoch)` 를 만들고, 시드 고정 무작위 작업에서 "저장 집합 == 쓴 집합" 과 바이트 단위 복원을 검증한다.
- 이 맥(Apple M6, macOS 27.2, 페이지 16 KiB)에서 **보호 위반이 SIGBUS 로 온다**, kill 로 보낸 SIGSEGV 도 `si_code=2` 라 출처를 못 가린다, 시스템 콜은 신호 대신 `EFAULT` 를 받는다 — 같은 실측 사실을 근거로 "내 폴트가 아니면 기본 동작으로 되돌리고 다시 보낸다" 를 설계한다.
- arm64 **ESR_ELx** (EC, WnR, CM, DFSC) 를 `uc_mcontext->__es.__esr` 에서 읽어 읽기/쓰기를 한 번에 구분하고, 원자적 RMW 가 `WnR=0` 으로 보고되는 함정과 함께 교차 검증한다.
- 사용자 폴트 1 번의 실제 비용(커널만의 첫 접근 대비 약 6 배)과 증분 vs 전체 복사의 손익분기를 숫자로 말하고, 리눅스의 대안(userfaultfd, `PAGEMAP_SCAN`, soft-dirty)과 Cortex-M MPU + MemManage 로 같은 생각을 옮긴다.

## 1. 문제

"GC 의 쓰기 장벽(write barrier)이나 체크포인트를 페이지 보호로 구현한다면?" — 시스템 면접에서 "페이지 폴트가 정확히 뭐고, 사용자가 가로챌 수 있나?" 로 나오고, 임베디드에서는 MPU 로 "이 영역에 누가 썼나" 를 잡는 것과 똑같은 생각이다. OS-B09 에서 "보호 위반 → 신호" 를, OS-M03 에서 **커널** 이 쓰기 보호로 COW 를 하는 것을 관찰했다. 이번에는 그 일을 **우리가 직접** 한다: 커널이 우리 대신 페이지 테이블을 바꾸는 손이고, 정책(언제 열고, 언제 다시 닫고, 무엇을 저장하나)은 전부 사용자 코드다.

이 기법은 생각보다 흔하다 (출처는 참고자료, 세부 구현은 문서 기준):

| 쓰는 곳 | 보호를 거는 이유 | 폴트에서 하는 일 |
|---|---|---|
| 세대별 GC 의 카드 마킹 대용 (Boehm GC 의 incremental 모드 등) | 오래된 세대에 쓰기 감지 | 페이지를 dirty 로 기록 후 RW |
| 분산 공유 메모리 (Li & Hudak 1989, IVY) | 다른 노드가 가진 페이지 | 네트워크로 받아 와 매핑 |
| JIT / 동적 바이너리 변환 | 코드 페이지가 바뀌면 번역 무효화 | 번역 캐시 비우고 RW |
| VAX/VMS 참조 비트 흉내 (OSTEP 23장 Aside, Babaoglu & Joy 1981) | 하드웨어 참조 비트가 없음 | "참조됨" 표시 후 원래 보호로 |
| 체크포인트·라이브 마이그레이션 (CRIU, QEMU dirty logging) | 지난 체크포인트 뒤 쓴 페이지만 보내기 | dirty 기록 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">페이지 하나의 상태 기계 (상태 = 지금 걸어 둔 보호)</text>
  <rect x="20" y="70" width="150" height="56" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="40" y="94" fill="currentColor" font-weight="bold">NONE</text>
  <text x="40" y="114" fill="currentColor" font-family="monospace">PROT_NONE</text>
  <rect x="295" y="70" width="150" height="56" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="315" y="94" fill="currentColor" font-weight="bold">READ (clean)</text>
  <text x="315" y="114" fill="currentColor" font-family="monospace">PROT_READ</text>
  <rect x="570" y="70" width="150" height="56" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="590" y="94" fill="currentColor" font-weight="bold">RW (dirty)</text>
  <text x="590" y="114" fill="currentColor" font-family="monospace">PROT_READ|WRITE</text>
  <path d="M170 90 L290 90" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M282 84 L292 90 L282 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="180" y="62" fill="currentColor">폴트 #1 (읽기든 쓰기든)</text>
  <text x="180" y="80" fill="currentColor">pagein → n_read++</text>
  <path d="M445 90 L565 90" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M557 84 L567 90 L557 96" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="455" y="62" fill="currentColor">쓰기 폴트</text>
  <text x="455" y="80" fill="currentColor">n_upgrade++</text>
  <path d="M645 126 L645 170 L370 170 L370 130" stroke="currentColor" stroke-width="2" fill="none" stroke-dasharray="6 4"/>
  <path d="M364 138 L370 128 L376 138" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="300" y="190" fill="currentColor">checkpoint(): 복사본 저장 → mprotect(READ)  (Part B)</text>
  <path d="M95 126 L95 230 L645 230 L645 130" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="140" y="250" fill="currentColor">ESR 모드: 첫 접근이 쓰기(WnR=1)면 폴트 1 번에 NONE → RW, n_direct++  (Part C)</text>
  <text x="20" y="285" fill="currentColor">RW 인데 또 폴트 = 내가 못 고치는 폴트 → false → 기본 동작으로 (A3).  내 영역 밖 주소도 같은 길.</text>
</svg>
```

### Part A — 지연 할당: 첫 접근에 페이지를 가져오고 정확히 센다

- `vmr_init(r, npages, pagein, use_esr)` — `mmap(PROT_NONE, MAP_PRIVATE|MAP_ANON)` 으로 **주소만 예약** (실제 메모리 0 — `mincore` 로 확인), 페이지별 상태 배열.
- `vmr_handle_fault(r, f)` — 신호 핸들러 안에서 불린다. `si_addr` 가 영역 밖이면 `false`. 안이면 페이지 번호로 내린 뒤 상태에 따라 `mprotect`. 첫 접근에는 `pagein(idx, dst)` 로 내용을 채운다 ("디스크에서 읽기" 흉내 — 잠깐 RW 로 열어 쓰고 READ 로).
- `pass_to_default(f)` — 내 폴트가 아니면 처리를 `SIG_DFL` 로 되돌리고 같은 신호를 다시 보내서 **원래 신호로 죽게** 한다. fork 한 자식에서 시험한다: 남의 `PROT_NONE` 페이지 접근, `kill(getpid(), SIGSEGV)`.
- 시험: 9 페이지를 읽고(그중 2 개는 쓰기도) 2 페이지는 쓰기만 → `n_read == 9`, `n_upgrade == 4`, `pagein == 9`, 두 번째 접근은 폴트 0, 페이지 경계를 걸친 8 바이트 쓰기는 두 페이지가 각각 폴트.

### Part B — dirty 추적 + 증분 체크포인트 / 복원

- `vmr_checkpoint(r, s)` — 새 epoch 을 열고 RW(=dirty) 페이지만 저장소에 복사, 다시 `PROT_READ` 로 보호해 다음 쓰기를 또 잡는다.
- `vmr_restore(r, s, e)` — e 뒤의 기록을 버리고(역사 잘라내기), 만진 페이지마다 "epoch ≤ e 인 마지막 복사본, 없으면 초기 내용" 을 쓴다.
- 시험: 시드 고정 xorshift 로 8 epoch 동안 무작위 읽기·쓰기. 매 epoch **저장한 페이지 집합 == 테스트가 기록한 쓴 페이지 집합** (중복 저장도 오류), `restore(5)` · `restore(3)` · `restore(0)` 이 각 시점 전체 memcpy 기준 사본과 **바이트 단위 일치**, 복원 뒤 이어서 쓴 epoch 도 정확.

### Part C — 하드웨어가 알려 주는 것: ESR 로 읽기/쓰기 구분

- `esr_decode(esr, &is_write)` — EC(31:26) = 0x24 (하위 EL 데이터 어보트)일 때만 믿는다. DFSC 로 변환/접근 플래그/권한 폴트 구분, `is_write = WnR && !CM` (리눅스 arm64 `is_write_abort` 와 같은 규칙).
- ESR 모드(`use_esr=1`)로 같은 무작위 작업을 돌려 READ-후-업그레이드 방식과 **만진 집합·dirty 집합이 같고 폴트는 적은지** 교차 검증. 원자적 RMW 첫 접근은 어떻게 보고되는지 실측.
- 측정·비교: 폴트 비용(`demo_bench.c`), 신호 표(`demo_fault_table.c`), 리눅스 userfaultfd / `PAGEMAP_SCAN` / soft-dirty (`demo_linux_uffd.c`, Lima VM).

## 2. Problem (English)

> **User-space page-fault handling.** Reserve `npages` with `PROT_NONE`. Install one `SA_SIGINFO` handler for both `SIGSEGV` and `SIGBUS` (macOS reports protection faults as `SIGBUS`). Page size comes from `sysconf(_SC_PAGESIZE)` (16384 on Apple Silicon).
>
> A. `int vmr_init(vmr_t*, size_t npages, pagein_fn, int use_esr)`; `bool vmr_handle_fault(vmr_t*, const fault_t*)` — page-align `si_addr`, reject addresses outside the region, NONE→READ on first touch (call `pagein` to fill the page), READ→RW on the first write; `void pass_to_default(const fault_t*)` — restore `SIG_DFL` and re-raise so a foreign fault kills the process with the original signal. Fault counters must equal the number of distinct pages touched / written, exactly.
>
> B. `long vmr_checkpoint(vmr_t*, snapstore_t*)` saves only dirty (RW) pages for a new epoch and write-protects them again; `int vmr_restore(vmr_t*, snapstore_t*, unsigned epoch)` reconstructs the region byte-exactly as of that epoch and discards later history. A seeded random workload must produce saved-page sets equal to written-page sets for every epoch.
>
> C. `int esr_decode(uint32_t esr, int *is_write)` for arm64 `ESR_ELx` (EC 0x24 only; DFSC classes; WnR unless CM). With `use_esr`, a first-touch write maps the page RW in one fault; cross-check against the portable scheme.
>
> Follow-ups: Is `mprotect` async-signal-safe? What does POSIX say about returning from a `SIGSEGV` handler? What happens when `read(2)` writes into a protected page? How would you do this on Linux without signals? How much does one fault cost, and when is a full copy cheaper than incremental? How does this map to a Cortex-M MPU?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, 0.13 초에 끝남 — 멈추지도 죽지도 않는다, exit 1):

```text
page size 16384
FAIL starter.c:333  vmr_init(&r, NP, pagein_pattern, 0) == 0
FAIL starter.c:334  r.base != NULL && ((uintptr_t)r.base % PG) == 0 && r.len == NP * PG
...
FAIL starter.c:342  ok
FAIL starter.c:343  r.n_read == 9 && r.n_upgrade == 4 && r.n_direct == 0
...
  (C: atomic RMW on fresh page: n_read 0, n_upgrade 0, n_direct 0)
FAIL starter.c:460  okc && q.state && q.state[2] == PS_RW && ...
4/35 passed
```

하네스가 해 주는 일 (이 레슨의 설계 핵심이기도 하다):

- **`guarded(fn, arg)`** — 영역을 만지는 테스트 코드는 전부 이 안에서 돈다. 핸들러가 `false`(내 것 아님)를 돌려주면 `on_fault` 가 `siglongjmp` 로 빠져나와 FAIL 로 센다. 그래서 TODO 가 비어 있어도 **무한 폴트 루프도, 테스트 바이너리 크래시도 없다.**
- **무진전 감지** — 같은 주소에서 폴트가 4 번 넘게 이어지면(핸들러가 `true` 라 해 놓고 보호를 안 바꿈) 역시 빠져나온다. 실제로 이 레슨을 준비하다 탐색용 코드가 `munmap` 된 주소에서 같은 폴트를 2 분 동안 반복한 적이 있다 — 핸들러를 쓸 때 가장 먼저 막아야 할 버그다 (7. 실수 1).
- `guarded()` 밖의 예상 못 한 폴트는 본체 프로세스에서 `FAIL unexpected fault` 를 찍고 `_exit(2)`.
- "남의 폴트는 원래 신호로 죽는다" 는 **fork 한 자식** 에서만 시험한다. 자식의 핸들러는 신호 번호를 파이프로 알리고, 부모는 `wait_child` (1.5 초 뒤 SIGKILL) 로 거둔 뒤 `WTERMSIG == 보고된 신호` 를 본다.

관찰용 파일:

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 demo_fault_table.c -o dft && ./dft     # 접근 종류별 신호·si_code·ESR 표
cc -std=c11 -Wall -Wextra -Werror -O2 demo_bench.c -o db && ./db             # 폴트 비용, 증분 vs 전체 복사
# 리눅스 (Lima VM, 홈 마운트는 읽기 전용이라 /tmp 로 복사):
limactl shell elinux -- bash -lc 'mkdir -p /tmp/OS-A06_work && cp <lesson dir>/demo_linux_uffd.c /tmp/OS-A06_work/ &&
  cd /tmp/OS-A06_work && gcc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_linux_uffd.c -o d && ./d'
```

`demo_linux_uffd.c` 는 첫 줄이 `/* ENV: linux */`, 둘째 줄이 `/* BUILD: -pthread */` 다 (맥 검증에서 건너뜀).

## 4. 힌트

<details><summary>힌트 1 — 핸들러가 받는 것</summary>

`sa_flags = SA_SIGINFO` 면 핸들러는 `(int sig, siginfo_t *si, void *uc)`. `si->si_addr` 는 **폴트 난 바이트 주소** (페이지 시작이 아님). 페이지 번호는 `(addr - base) / pg`, 페이지 시작은 `base + idx * pg`. `mprotect` 의 주소는 반드시 페이지 정렬이어야 한다 (`EINVAL`, macOS `man 2 mprotect`). 같은 핸들러를 SIGSEGV 와 SIGBUS 둘 다에 건다.
</details>

<details><summary>힌트 2 — 핸들러에서 return 하면?</summary>

하드웨어 폴트 신호에서 정상 return 하면 커널이 저장해 둔 문맥으로 돌아가 **폴트 난 명령을 다시 실행** 한다. 보호를 고쳤으면 이번엔 성공, 안 고쳤으면 같은 폴트가 또 난다 — 무한 루프. "고칠 수 없으면 `false`" 를 반드시 돌려주게 짠다. 상태가 RW 인데 폴트가 났다면 그건 내 보호 때문이 아니다.
</details>

<details><summary>힌트 3 — 읽기 폴트인지 쓰기 폴트인지 모를 때</summary>

POSIX `siginfo_t` 에는 "읽기였나 쓰기였나" 가 없다. 그래서 이식성 있는 방법은 "일단 READ 로 연다 → 쓰기였다면 다시 폴트가 난다 → 그때 RW" 다. 쓰기 첫 접근은 폴트 2 번이 들지만 판단이 틀릴 일이 없다. 셀 때도 이 규칙이 그대로 등식이 된다: `n_read == 만진 페이지`, `n_upgrade == 쓴 페이지`.
</details>

<details><summary>힌트 4 — 남의 폴트를 원래대로 죽게 하기</summary>

`sigaction(sig, SIG_DFL)` 만 하고 return 하면 하드웨어 폴트는 다시 일어나 기본 동작(종료)으로 간다. 하지만 `kill` 로 온 SIGSEGV 는 다시 오지 않는다. 그리고 이 맥에서는 `si_code` 로 둘을 가를 수 없다 (5. 풀이 표). 지금 신호는 핸들러가 도는 동안 막혀 있다는 점을 이용하라.
</details>

<details><summary>힌트 5 — 체크포인트의 불변식</summary>

체크포인트 직후 "RW 상태인 페이지 = 0 개" 여야 한다. 그래야 다음 epoch 의 첫 쓰기가 반드시 폴트로 잡힌다. 복사 → `mprotect(READ)` → 상태 READ 순서. 복원은 저장소가 epoch 순서로 쌓였다는 점을 쓰면 "e 이하 앞부분만 남기기" 가 한 번의 훑기다.
</details>

<details><summary>힌트 6 — ESR 의 함정</summary>

`uc_mcontext->__es.__esr` 는 "이 스레드가 마지막으로 받은 예외" 의 syndrome 이다. 데이터 어보트(EC 0x24)가 아닐 수도 있다 — 예를 들어 `kill` 로 온 신호면 마지막 예외는 시스템 콜(SVC)이다. EC 부터 확인하라. 그리고 WnR 이 1 이어도 캐시 유지보수 명령(CM=1)이면 쓰기가 아니다.
</details>

## 5. 풀이

### Part A — 폴트 한 번에 일어나는 일

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">PROT_NONE 페이지 읽기 한 번 (macOS arm64, 관찰 + 문서 기준)</text>
  <rect x="10" y="40" width="160" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="20" y="64" fill="currentColor" font-family="monospace">ldrb w8, [x9]</text>
  <rect x="200" y="40" width="250" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="210" y="58" fill="currentColor">MMU: 변환 폴트 → EL1 로 예외</text>
  <text x="210" y="74" fill="currentColor" font-family="monospace">ESR=0x92000007 FAR=addr</text>
  <rect x="480" y="40" width="250" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="490" y="58" fill="currentColor">XNU VM: 보호 위반 판정</text>
  <text x="490" y="74" fill="currentColor">Mach EXC_BAD_ACCESS → BSD 신호</text>
  <rect x="480" y="110" width="250" height="40" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="490" y="128" fill="currentColor">SIGBUS, si_code=1, si_addr=addr</text>
  <text x="490" y="144" fill="currentColor">사용자 스택에 문맥(ucontext) 저장</text>
  <rect x="200" y="110" width="250" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="210" y="128" fill="currentColor">on_fault → vmr_handle_fault</text>
  <text x="210" y="144" fill="currentColor" font-family="monospace">mprotect(page, READ) = 시스템 콜</text>
  <rect x="10" y="110" width="160" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="20" y="128" fill="currentColor">return → sigreturn</text>
  <text x="20" y="144" fill="currentColor">같은 명령 재실행 OK</text>
  <path d="M170 60 L196 60 M450 60 L476 60 M605 80 L605 106 M480 130 L454 130 M200 130 L174 130" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="10" y="185" fill="currentColor" font-weight="bold">커널 경계를 몇 번 넘나:</text>
  <text x="10" y="205" fill="currentColor">폴트 진입 1 + 신호 전달(사용자로) + mprotect 진입·복귀 1 + sigreturn 1 = 최소 3 번 왕복.</text>
  <text x="10" y="225" fill="currentColor">커널만의 첫 접근 폴트는 1 번 (폴트 → 0 페이지 할당 → 복귀). 실측 비용 차이 ≈ 6 배 (아래 표).</text>
  <text x="10" y="255" fill="currentColor" font-weight="bold">pagein 이 있으면:</text>
  <text x="10" y="275" fill="currentColor">mprotect(RW) → pagein() 으로 채움 → mprotect(READ).  내용이 다 채워지기 전엔 아무도 READ 로 못 본다</text>
  <text x="10" y="295" fill="currentColor">(단일 스레드 기준. 멀티스레드면 다른 스레드가 RW 창에서 반쯤 채운 페이지를 볼 수 있다 → Q6).</text>
  <text x="10" y="320" fill="currentColor">리눅스 arm64 는 같은 ESR 로 SIGSEGV(SEGV_ACCERR) 를 준다 — demo_linux_uffd.c 실측.</text>
</svg>
```

핸들러 본체 (`solution.c` 의 A2, 핵심만):

```c
if (a < b || a >= b + r->len) return false;          /* 남의 주소 → 기본 동작 쪽으로 */
size_t i = (a - b) / r->pg;                          /* si_addr 은 바이트 주소: 페이지로 내린다 */
if (st == PS_NONE) {                                 /* 첫 접근 */
    if (r->pagein) { mprotect(pa, pg, RW); r->pagein(i, pa, pg); }
    mprotect(pa, pg, direct ? RW : PROT_READ);  state = READ (또는 RW);  n_read++ (또는 n_direct++)
} else if (st == PS_READ) {                          /* 읽을 수 있는데 또 폴트 = 쓰기 */
    mprotect(pa, pg, RW);  state = RW;  n_upgrade++;
} else return false;                                 /* RW 인데 폴트: 내가 고칠 수 없다 */
```

**이 맥에서 실제로 오는 신호** — `demo_fault_table.c` 실제 출력 (`si_addr`, FAR 은 페이지 시작 기준 오프셋):

```text
page 16384  SIGBUS=10 SIGSEGV=11  SEGV_MAPERR=1 SEGV_ACCERR=2 BUS_ADRALN=1 BUS_ADRERR=2 SI_USER=65537
access                             signal  code  si_addr      FAR  ESR
read  PROT_NONE (fresh)            SIGBUS     1      100      100  0x92000007  EC=0x24 WnR=0 CM=0 DFSC=0x07
write PROT_NONE (fresh)            SIGBUS     1      100      100  0x92000047  EC=0x24 WnR=1 CM=0 DFSC=0x07
write PROT_READ (fresh, no PTE)    SIGBUS     1      100      100  0x92000047  EC=0x24 WnR=1 CM=0 DFSC=0x07
write PROT_READ (populated)        SIGBUS     1      100      100  0x9200004f  EC=0x24 WnR=1 CM=0 DFSC=0x0f
read  PROT_NONE (populated)        SIGBUS     1      100      100  0x92000007  EC=0x24 WnR=0 CM=0 DFSC=0x07
LDADDAL PROT_NONE (fresh)          SIGBUS     1       64       64  0x92000007  EC=0x24 WnR=0 CM=0 DFSC=0x07
LDADDAL PROT_READ (populated)      SIGBUS     1       64       64  0x9200004f  EC=0x24 WnR=1 CM=0 DFSC=0x0f
memset 0 4 KiB PROT_NONE           SIGBUS     1        0        0  0x92000047  EC=0x24 WnR=1 CM=0 DFSC=0x07
memset 0 4 KiB PROT_READ (pop.)    SIGBUS     1        0        0  0x9200004f  EC=0x24 WnR=1 CM=0 DFSC=0x0f
8 B load across page 0 RW | 1 NONE SIGBUS     1    16384    16384  0x92000007
kill(getpid(), SIGSEGV)            SIGSEGV    2 -4310958080 -4310958080  0x56000080  EC=0x15 WnR=0 CM=0 DFSC=0x00
read after munmap                  SIGSEGV    2      100      100  0x92000007  EC=0x24 WnR=0 CM=0 DFSC=0x07
syscall: read() into PROT_NONE -> -1 (Bad address), into PROT_READ -> -1 (Bad address), signals caught 0
mincore: 256-page reservation resident 0 -> after writing 16 pages resident 16 (4 MiB reserved)
```

읽는 법:

| 관찰 | 의미 | 설계에 주는 영향 |
|---|---|---|
| `PROT_NONE`·`PROT_READ` 위반이 전부 **SIGBUS, si_code=1** (`BUS_ADRALN` 이라는 이름) | macOS 는 보호 위반을 SIGBUS 로 보낸다 (OS-B09 와 같은 관찰). `man 2 mprotect`: "a SIGBUS or SIGSEGV signal" | 두 신호 다 잡는다. si_code 이름을 믿지 않는다 |
| `munmap` 뒤는 **SIGSEGV, si_code=2** (`SEGV_ACCERR`; 리눅스라면 `SEGV_MAPERR`) | 매핑이 없는 주소 | 내 영역 밖 → 기본 동작 |
| `kill(SIGSEGV)` 도 **si_code=2**, `si_addr = NULL`, ESR 은 EC=0x15 (**SVC** — kill 시스템 콜 자체) | 이 맥에선 si_code 로 "누가 보냈나" 를 못 가린다 (`SI_USER` = 65537 이 아님). ESR 은 "마지막 예외" 라 데이터 어보트가 아닐 수 있다 | A3 은 항상 `raise`, C1 은 EC 부터 검사 |
| fresh 페이지의 `PROT_READ` 쓰기는 DFSC 0x07 (**변환** 폴트), 상주 페이지는 0x0F (**권한** 폴트) | XNU 는 아직 PTE 가 없는 페이지를 변환 폴트로 받는다. `PROT_NONE` 은 상주해도 변환 폴트 (PTE 를 무효로 두는 듯 — 추정) | DFSC 로 "NONE 이었나 READ 였나" 를 판단하면 틀린다. 상태는 내 배열로 |
| FAR == si_addr, 경계 걸친 load 는 **실제로 폴트 난 페이지(16384)** 를 보고 | 두 페이지가 각자 폴트 → 핸들러가 페이지 하나씩 연다 | 테스트로 고정 (`r.state[30], [31] == RW`, 폴트 4 번) |
| `read()` 가 보호된 버퍼에 쓰면 **신호 없이 EFAULT** | 커널이 사용자 메모리를 만질 때(copyout)는 사용자 핸들러를 부르지 않는다 | 보호된 영역을 시스템 콜 버퍼로 넘기기 전에 미리 열어야 한다 (7. 실수 5) |
| `mincore`: 예약 256 페이지(4 MiB) 중 상주 0 → 16 페이지 쓰기 후 16 | `PROT_NONE` 예약은 주소 공간만 쓴다 | 큰 영역을 싸게 예약하는 근거 |

**exact count.** 하네스의 고정 작업: 읽기 `{0,3,7,8,20,21,63}`, 쓰기 `{3,9,20,40}` → 만진 페이지 9, 쓴 페이지 4. 실제 출력 `(A: read faults 9, upgrades 4, pageins 9)`. 같은 위치를 다시 만지면 카운터가 그대로다. 등식이 **정확히** 성립하는 이유: 우리 영역은 우리 핸들러 말고 아무도 보호를 바꾸지 않고, 한 페이지는 상태가 NONE→READ→RW 로 한 방향으로만 가므로 페이지마다 각 전이가 최대 1 번이다. (OS-M03 에서 `ru_minflt` 를 "≥ k, ≤ k + slack" 으로 넉넉하게 잰 것과 대조된다 — 그건 커널 폴트라 libc·스택 잡음이 섞였다.)

**남의 폴트.** 실제 출력:

```text
  (foreign PROT_NONE page: child died by signal, reported sig 10, term sig 10)
  (kill(getpid(), SIGSEGV): child died by signal, reported sig 11, term sig 11)
```

`pass_to_default` 는 `sigaction(sig, SIG_DFL)` 후 `raise(sig)`. 이 신호는 핸들러가 도는 동안 막혀 있으므로(`SA_NODEFER` 없음) `raise` 는 보류되었다가 핸들러가 return 하며 마스크가 풀리는 순간 기본 동작(종료 + 코어)으로 전달된다. **돌연변이 시험**: `raise` 를 빼고 "SIG_DFL 후 return" 만 하면 하드웨어 폴트 자식은 여전히 SIGBUS 로 죽지만(같은 명령이 다시 폴트) `kill` 자식은 **살아서 exit 0** — 33/35. 리눅스라면 `si_code == SI_USER` 로 갈라 return 만 할 수도 있지만, 이 맥에서 kill 이 `si_code=2` 를 주는 이상 그 분기는 틀린다 (실측). 참고: 이 자식들이 신호로 죽을 때 macOS 는 `~/Library/Logs/DiagnosticReports/` 에 `.ips` 를 남긴다 (확인함 — 묶음으로 몇 개씩, 매 실행 정확히 2 개는 아님). kill 자식의 리포트는 `EXC_BAD_ACCESS / SIGSEGV / KERN_PROTECTION_FAILURE at <영역 주소>` 로 적혀 있었다 — 리포터가 그 스레드의 **마지막 Mach 예외** (앞서 처리된 내 영역 폴트)를 같이 적은 것이다. 크래시 리포트도 "마지막 예외" 를 맹신하면 안 된다는 예.

**신호 안전성 — 솔직하게.**

- POSIX.1-2024 §2.4.3 의 async-signal-safe 목록에 `mprotect` 는 **없다** (확인: pubs.opengroup.org). macOS `man 2 sigaction` 의 목록에도 없다. `siglongjmp` 도 없다.
- 더 근본적으로 POSIX §2.4.3: "The behavior of a process is undefined after it returns normally from a signal-catching function for a SIGBUS, SIGFPE, SIGILL, or SIGSEGV signal that was not generated by kill(), sigqueue(), or raise()." 이 레슨의 Part A 전체가 **표준 밖** 이다. ISO C11 7.14.1.1p5 도 비동기 신호 핸들러가 `volatile sig_atomic_t`·lock-free atomic 이외의 정적 객체를 만지면 UB 라고 한다.
- 그런데도 쓰는 이유: `mprotect` 는 실제 구현에서 커널로 바로 가는 얇은 시스템 콜 래퍼라 잠금·malloc 이 없고, 폴트는 **동기(synchronous)** 신호라 "아무 곳에서나 끼어드는" 상황이 아니다 — 폴트를 낸 명령에서만 일어난다. Boehm GC, libsigsegv(GNU), 여러 JIT 가 이 가정 위에서 수십 년 동작해 왔다 (문서·소스 기준, 각 구현을 이 레슨에서 확인하지는 않음). 대가: 플랫폼마다 검증해야 하고(이 레슨의 표가 그 검증이다), 핸들러 안에서는 `malloc`·`printf`·락을 쓰지 않는다. 그래서 `pagein` 은 미리 정해진 패턴/저장소만 읽고, 연습 3 의 사본 풀은 **미리 할당** 한다.

### Part B — 증분 체크포인트

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">epoch 마다 dirty 페이지만 저장 → restore(e) 는 "e 이하 마지막 사본"</text>
  <text x="10" y="58" fill="currentColor" font-family="monospace">page 3</text>
  <text x="10" y="98" fill="currentColor" font-family="monospace">page 9</text>
  <text x="10" y="138" fill="currentColor" font-family="monospace">page 20</text>
  <path d="M80 30 L80 150 M240 30 L240 150 M400 30 L400 150 M560 30 L560 150" stroke="currentColor" stroke-dasharray="3 4"/>
  <text x="70" y="168" fill="currentColor">ckpt 0</text>
  <text x="230" y="168" fill="currentColor">ckpt 1</text>
  <text x="390" y="168" fill="currentColor">ckpt 2</text>
  <text x="550" y="168" fill="currentColor">ckpt 3</text>
  <rect x="150" y="42" width="40" height="24" class="f-warn" stroke="currentColor"/><text x="157" y="59" fill="currentColor">w</text>
  <rect x="226" y="42" width="28" height="24" class="f-acc" stroke="currentColor"/><text x="231" y="59" fill="currentColor">S1</text>
  <rect x="470" y="42" width="40" height="24" class="f-warn" stroke="currentColor"/><text x="477" y="59" fill="currentColor">w</text>
  <rect x="546" y="42" width="28" height="24" class="f-acc" stroke="currentColor"/><text x="551" y="59" fill="currentColor">S3</text>
  <rect x="300" y="82" width="40" height="24" class="f-warn" stroke="currentColor"/><text x="307" y="99" fill="currentColor">w</text>
  <rect x="386" y="82" width="28" height="24" class="f-acc" stroke="currentColor"/><text x="391" y="99" fill="currentColor">S2</text>
  <rect x="110" y="122" width="40" height="24" class="f-mut" stroke="currentColor"/><text x="117" y="139" fill="currentColor">r</text>
  <text x="590" y="59" fill="currentColor">restore(2) ← S1</text>
  <text x="590" y="99" fill="currentColor">restore(2) ← S2</text>
  <text x="590" y="139" fill="currentColor">restore(2) ← 초기값</text>
  <text x="10" y="200" fill="currentColor">w = 그 epoch 에 쓴 적 있음 (첫 쓰기 1 번만 폴트) · S = 체크포인트가 저장한 사본 · r = 읽기만 (저장 안 함)</text>
  <text x="10" y="222" fill="currentColor">체크포인트 직후 불변식: RW 페이지 0 개 → 다음 epoch 의 첫 쓰기는 반드시 폴트로 보인다.</text>
  <text x="10" y="244" fill="currentColor">restore(e): e 뒤의 S 는 버린다 (역사 잘라내기). 같은 페이지를 한 epoch 에 100 번 써도 저장은 1 장.</text>
  <text x="10" y="266" fill="currentColor">비용 = (dirty 페이지 수) × (쓰기 폴트 1 번 + 페이지 복사 1 장 + mprotect 1 번).  안 쓴 페이지는 0.</text>
</svg>
```

```c
long vmr_checkpoint(r, s):  e = s->epoch + 1
    for each page i with state RW:  snap_put(s, e, i, page)  →  mprotect(page, READ)  →  state = READ
    s->epoch = e
int vmr_restore(r, s, e):   keep = (epoch <= e 인 앞부분)  →  나머지 free
    for each page i with state != NONE:
        src = 뒤에서부터 찾은 page == i 인 첫 항목 (= e 이하 마지막) / 없으면 pagein 초기 내용
        mprotect(RW) → memcpy → mprotect(READ) → state = READ
```

- **왜 READ 로 되돌리나** (NONE 이 아니라): 내용은 여전히 유효하고 읽기는 자유로워야 한다. 쓰기만 잡으면 된다 — dirty 비트 흉내.
- **안 만진 페이지(NONE)는 복원하지 않는다**: 아직 한 번도 안 만졌으니 초기 내용 그대로다. 첫 접근 때 pagein 이 초기 내용을 줄 것이다.
- **역사 잘라내기**: restore(5) 뒤에 쓰면 그 다음 체크포인트는 epoch 6 이다. 옛 6~8 을 남겨 두면 "e 이하 마지막" 검색이 버려진 미래 사본을 고를 수 있다. 시험 `vmr_restore(&r, &s, 1) == -1` (restore(0) 뒤 1 로는 못 감) 이 이 규칙을 고정한다.

실제 출력 (정답 코드):

```text
  (B: epoch 1 wrote 10 pages, checkpoint saved 10)
  (B: 8 epochs, 133 pages saved = 2128 KiB vs full copies 8 x 1024 KiB)
PASS sets_ok
PASS cnt_ok
PASS vmr_restore(&r, &s, 5) == 0 && region_equals(&r, shadow[5])
PASS g && n == nw && s.epoch == 6 && saved_set_equals(&s, 6, written)
PASS vmr_restore(&r, &s, 3) == 0 && region_equals(&r, shadow[3])
PASS vmr_restore(&r, &s, 0) == 0 && region_equals(&r, shadow[0]) && s.n == 0
```

기준 사본 `shadow[e]` 는 테스트가 매 epoch 전체를 memcpy 한 것 — 증분 방식과 **독립적인 정답** 이다. **돌연변이 시험**: 체크포인트에서 `mprotect(READ)` 한 줄만 빼면 (상태만 READ 로 바꿈) 다음 epoch 의 쓰기가 폴트를 안 내서 저장이 빠진다 → `sets_ok`·restore 3 개 FAIL (32/35). 이게 실무에서 가장 흔한 버그 모양이다: "dirty 비트는 지웠는데 하드웨어 보호는 안 걸었다."

### Part C — ESR 로 한 번에

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">ESR_ELx (데이터 어보트) — 0x9200004F 를 풀어 보면</text>
  <rect x="10" y="40" width="150" height="40" class="f-acc" stroke="currentColor"/>
  <text x="18" y="58" fill="currentColor">EC [31:26]</text><text x="18" y="74" fill="currentColor" font-family="monospace">100100 = 0x24</text>
  <rect x="160" y="40" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="166" y="58" fill="currentColor">IL [25]</text><text x="180" y="74" fill="currentColor" font-family="monospace">1</text>
  <rect x="220" y="40" width="200" height="40" class="f-mut" stroke="currentColor"/>
  <text x="228" y="58" fill="currentColor">ISS [24:9] (ISV=0 등)</text><text x="228" y="74" fill="currentColor" font-family="monospace">0</text>
  <rect x="420" y="40" width="70" height="40" class="f-warn" stroke="currentColor"/>
  <text x="426" y="58" fill="currentColor">CM [8]</text><text x="446" y="74" fill="currentColor" font-family="monospace">0</text>
  <rect x="490" y="40" width="60" height="40" class="f-mut" stroke="currentColor"/>
  <text x="496" y="58" fill="currentColor">[7]</text><text x="510" y="74" fill="currentColor" font-family="monospace">0</text>
  <rect x="550" y="40" width="80" height="40" class="f-ok" stroke="currentColor"/>
  <text x="556" y="58" fill="currentColor">WnR [6]</text><text x="580" y="74" fill="currentColor" font-family="monospace">1</text>
  <rect x="630" y="40" width="100" height="40" class="f-acc" stroke="currentColor"/>
  <text x="636" y="58" fill="currentColor">DFSC [5:0]</text><text x="636" y="74" fill="currentColor" font-family="monospace">001111</text>
  <text x="10" y="110" fill="currentColor">EC 0x24 = 하위 EL(EL0) 데이터 어보트 · 0x20 = 명령 어보트 · 0x25 = 같은 EL · 0x15 = SVC (kill 뒤)</text>
  <text x="10" y="132" fill="currentColor">DFSC 0b0001LL 변환 폴트 · 0b0010LL 접근 플래그 · 0b0011LL 권한 폴트 (LL = 레벨) · 0x10 동기 외부 어보트</text>
  <text x="10" y="154" fill="currentColor">쓰기 판정 = WnR &amp;&amp; !CM  (리눅스 arch/arm64/mm/fault.c:534 is_write_abort 와 같음)</text>
  <text x="10" y="176" fill="currentColor">여기서는 0x0F = 레벨 3 권한 폴트 + 쓰기 → "상주한 READ 페이지에 쓰기"</text>
</svg>
```

필드 위치·DFSC 인코딩은 Arm ARM (DDI 0487) ESR_ELx 설명과 리눅스 `arch/arm64/include/asm/esr.h` (6.18.55: `ESR_ELx_EC_DABT_LOW 0x24` 48 행, `ESR_ELx_WNR_SHIFT 6` 87 행, `ESR_ELx_FSC_PERM 0x0C` 123 행)로 확인했다. LPA2 의 레벨 −1 인코딩 등 드문 값은 `ESR_DABT_OTHER` 로 뭉뚱그렸다.

교차 검증 실제 출력:

```text
  (C: mode READ: touched 47, written 35, first-touch-is-write 29 -> faults 82)
  (C: mode ESR : touched 47, written 35, first-touch-is-write 29 -> faults 53)
  (C: atomic RMW on fresh page: n_read 1, n_upgrade 1, n_direct 0)
```

- 두 모드의 만진 집합·dirty 집합(상태 배열 전체)이 같고, ESR 모드는 첫 접근이 쓰기인 29 페이지만큼 폴트가 적다 (82 → 53 = 82 − 29). 각 모드의 등식도 정확: READ 모드 `n_read == 47, n_upgrade == 35`, ESR 모드 `n_read + n_direct == 47, n_upgrade + n_direct == 35, n_direct == 29`.
- **원자적 RMW (`LDADDAL`) 를 `PROT_NONE` 에** 하면 `WnR=0` 으로 왔다 (위 표, 변환 폴트). 읽기와 쓰기를 둘 다 하는 명령은 "읽기가 먼저 폴트" 로 보고되는 것으로 보인다 (Arm ARM 의 원자 명령 WnR 규칙 — 정확한 문구는 확인 못 함, 관찰만). ESR 모드에서도 결과는 맞다: READ 로 열고 → 재실행 → 권한 폴트 `WnR=1` → RW. **ESR 이 쓰기를 읽기로 잘못 말하는 쪽은 안전**(폴트 1 번 더)하고, 읽기를 쓰기로 말하면 깨끗한 페이지를 dirty 로 저장할 뿐(낭비)이다 — 어느 쪽도 dirty 를 **놓치지** 않는다. 놓치는 유일한 길은 "RW 로 열었는데 기록을 안 함" 이다.
- 하드웨어가 이걸 주는데 왜 POSIX 엔 없나: `siginfo_t` 는 아키텍처 중립이라 접근 종류 필드가 없다. 리눅스 arm64 는 같은 정보를 `ucontext` 의 `esr_context` 레코드(`ESR_MAGIC`)로 준다 — `demo_linux_uffd.c` 가 그걸 읽는다. x86 리눅스는 `uc_mcontext.gregs[REG_ERR]` 의 bit 1 (W/R) 이 같은 역할 (문서 기준, 미검증).

### 비용 — `demo_bench.c` 실측

기계: `Apple M6` (Super 2 + Performance 4 + Efficiency 6), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, macOS 27.2, 페이지 16 KiB, 영역 4096 페이지 = 64 MiB, 각 값은 5 회 중앙값. **부하가 매우 높았다**: 세 번 실행 동안 `uptime` load average 21.9 → 29.6 (다른 세션 작업 동시). 절대값이 아니라 배수만 볼 것.

| 폴트 종류 | 실행 1 | 실행 2 | 실행 3 |
|---|---|---|---|
| 커널만: RW 매핑 첫 쓰기 (0 페이지 할당) | 2.10 µs | 1.38 µs | 1.42 µs |
| NONE→READ (SIGBUS + 핸들러 + mprotect) | 8.51 µs | 11.75 µs | 9.00 µs |
| READ→RW 업그레이드 | 9.33 µs | 17.30 µs | 8.42 µs |
| NONE→RW 한 번에 (ESR) | 9.17 µs | 59.0 µs (이상치) | 8.76 µs |

사용자 공간 폴트 ≈ **8.5~12 µs ≈ 초당 10만 번**, 커널만의 첫 접근 폴트의 **약 6 배**. 차이는 위 그림의 커널 왕복 횟수(신호 프레임 구성 + `mprotect` 시스템 콜 + `sigreturn`) 다. 쓰기 첫 접근은 READ 방식이면 이 비용이 2 번, ESR 이면 1 번.

에포크 비용 (64 MiB 중 dirty 비율 f, "증분" = 쓰기 폴트 + dirty 사본 + 재보호; 실행 1·3 의 coalesced 열, 전체 memcpy 사본은 1720 µs / 1593 µs):

| dirty | 페이지 | 증분 (실행 1) | 증분 (실행 3) | 그중 폴트만 | 전체/증분 |
|---|---|---|---|---|---|
| 1 % | 40 | 521 µs | 519 µs | 366~405 µs | **3.1~3.3 배 이득** |
| 5 % | 204 | 2647 µs | 2419 µs | 1805~2076 µs | 0.6~0.7 (손해) |
| 10 % | 409 | 5096 µs | 4324 µs | 3682~4235 µs | 0.3~0.4 |
| 25 % | 1024 | 22042 µs | 12685 µs | ~1 만 µs | 0.1 |

(실행 2 는 전체 memcpy 가 7255 µs 로 느리게 나와 손익분기가 10 % 부근이었다. 25 % 이상 줄은 실행마다 2~10 배 흔들려 생략 — 부하 탓.) 결론은 흔들리지 않는다: **비용의 70~80 % 가 폴트** 이고, 메모리 대역폭으로 하는 전체 복사(약 40 GB/s)와 비교하면 손익분기는 **dirty 몇 % 수준** 이다. 그래도 증분이 이기는 경우: (1) 영역이 크고 쓰기가 국소적일 때(데이터베이스 버퍼, GC 의 오래된 세대), (2) 사본을 **멈춘 상태로** 만들 수 없을 때 — 전체 복사는 그 시간 동안 쓰기를 멈춰야 일관된다, (3) 사본을 네트워크·플래시로 보내야 해서 **바이트 수** 가 비용일 때 (라이브 마이그레이션). "페이지마다 mprotect" vs "연속 구간 묶기" 차이는 무작위 dirty 패턴에서는 작았다 (인접 dirty 가 드묾). 순차 쓰기라면 묶기가 시스템 콜 수를 크게 줄인다.

### 리눅스라면 — 신호 없이 커널에 맡기기 (`demo_linux_uffd.c`, Lima VM 실측)

VM: `uname -r` = `7.0.0-38-generic` (aarch64, Ubuntu), `gcc (Ubuntu 15.2.0-16ubuntu1) 15.2.0`, 페이지 4096. (소스 인용은 VM 의 `/var/lab/src/linux-6.18.55` — 실행 커널보다 오래된 트리라는 점 주의.) 실제 출력:

```text
page size 4096
1) mprotect + SIGSEGV on linux/arm64
  PROT_NONE read         sig=SIGSEGV si_code=2 (SEGV_ACCERR) off=5 esr=0x92000007 WnR=0 DFSC=0x07
  PROT_NONE write        sig=SIGSEGV si_code=2 (SEGV_ACCERR) off=5 esr=0x92000047 WnR=1 DFSC=0x07
  PROT_READ write (new)  sig=SIGSEGV si_code=2 (SEGV_ACCERR) off=5 esr=0x92000047 WnR=1 DFSC=0x07
  PROT_READ write (pte)  sig=SIGSEGV si_code=2 (SEGV_ACCERR) off=5 esr=0x9200004f WnR=1 DFSC=0x0f
  unmapped read          sig=SIGSEGV si_code=1 (SEGV_MAPERR) off=5 esr=0x92000007 WnR=0 DFSC=0x07
2) userfaultfd MISSING, UFFD_USER_MODE_ONLY (vm.unprivileged_userfaultfd 0 이어도 허용)
  kernel features 0x1ffff (WP_ASYNC yes)
  touched 22 pages -> 22 uffd faults, content from handler OK
3) uffd WP_ASYNC + PAGEMAP_SCAN (CRIU 류의 dirty 추적)
  epoch 1: wrote 13 pages -> PAGEMAP_SCAN 13 regions, 13 written pages (re-protected)
  epoch 2: wrote 7 pages -> PAGEMAP_SCAN 7 regions, 7 written pages (re-protected)
  epoch 3: wrote 5 pages -> PAGEMAP_SCAN 5 regions, 5 written pages (re-protected)
4) soft-dirty
  clear_refs<-4: ok, pagemap read 8, bit55(soft-dirty)=0 after write (arm64: no HAVE_ARCH_SOFT_DIRTY)
```

| 방식 | 하는 일 | 이 레슨 대응 | 근거 |
|---|---|---|---|
| mprotect + SIGSEGV | 같은 ESR, 신호만 SIGSEGV(`SEGV_ACCERR`/`MAPERR` 가 이름대로) | Part A~C 그대로 | 위 출력 |
| **userfaultfd** MISSING | 폴트가 **fd 메시지** 로 오고, 다른 스레드가 `UFFDIO_COPY` 로 페이지를 채워 깨운다. 신호 핸들러 제약 없음 | Part A 의 pagein | `fs/userfaultfd.c:405` — `UFFD_USER_MODE_ONLY` 면 커널 모드 폴트는 처리 안 함 (그래서 비특권 허용; `vm.unprivileged_userfaultfd`=0 인 VM 에서 실측 동작) |
| uffd **WP_ASYNC + PAGEMAP_SCAN** | 쓰기 보호 폴트를 커널이 **알아서** 풀고 "written" 표시, `PAGEMAP_SCAN` 한 번에 목록 + 재보호 | Part B 의 checkpoint (폴트당 신호 없음) | `fs/proc/task_mmu.c:2327` `PM_SCAN_FLAGS`, arm64 `HAVE_ARCH_USERFAULTFD_WP` (`arch/arm64/Kconfig:279`) |
| soft-dirty | `clear_refs` 에 4 → 쓰면 pagemap bit 55 | Part B (조회형) | `task_mmu.c:1592`·`1865`. `mm/Kconfig:1036` `MEM_SOFT_DIRTY` 는 `HAVE_ARCH_SOFT_DIRTY` 필요 — 6.18.55 에서 x86 등은 select, **arm64 는 없음** → 실측 bit55 = 0 |

커널 COW 는 같은 쓰기 보호를 커널 안에서 쓴다 (`mm/memory.c:4063 do_wp_page` — OS-M03 에서 관찰한 것). 이 레슨은 그 **정책을 사용자가 갖는** 버전이다.

### Part C — 펌웨어로 옮기면: Cortex-M MPU + MemManage

MMU 가 없는 MCU 에도 같은 생각이 그대로 있다. MPU 영역 하나를 "읽기 전용" 으로 두면 쓰기가 **MemManage 폴트** 가 되고, `MMFSR.DACCVIOL` + `MMARVALID` 일 때 `MMFAR` 이 폴트 주소다 (ARM-M12 의 CFSR 디코더, ARCH-M15 의 MPU 영역 계산, BM-A08 의 MPU 설정). 대응표:

| 이 레슨 (macOS arm64) | Cortex-M (ARMv7-M/v8-M) |
|---|---|
| `mprotect(page, PROT_READ)` | MPU 영역 AP = 읽기 전용 (영역 수 8~16 개, 크기·정렬 제약) |
| SIGBUS + `si_addr` | MemManage 예외 + `MMFAR` (MMARVALID 일 때만) |
| ESR `WnR` | MMFSR 에는 읽기/쓰기 구분 비트가 없다 — `DACCVIOL` 은 데이터 접근일 뿐. 폴트 난 명령을 스택 프레임 PC 로 디코드해야 함 |
| 핸들러에서 `mprotect` 후 return → 재실행 | 핸들러에서 MPU 영역 변경 → `DSB; ISB` → 예외 복귀 → 재실행 (stacked PC 가 폴트 명령) |
| 페이지 16 KiB 단위 dirty 추적 | 영역 수가 적어 "페이지별" 은 불가 → 섹터 단위 (예: 플래시 에뮬 EEPROM 블록 하나를 RO 로 두고 첫 쓰기 감지) |
| sigaltstack 스택 오버플로 감지 | 스택 아래 MPU 가드 영역 (BM-A08, RTOS-M09 의 가드 페이지 방식) — 폴트 처리 스택은 MSP 로 따로 |

실무에서의 쓰임: (1) 설정 구조체를 RO 로 두고 의도치 않은 쓰기를 잡기(디버그 빌드 "watchpoint 대용"), (2) 이중 뱅크 설정 저장에서 "이번 부팅에 바뀐 블록만 플래시에 쓰기" — Part B 의 증분 체크포인트가 플래시 마모를 줄이는 방식과 같다. 단 MCU 에서는 폴트 처리 시간이 실시간 제약을 깬다는 점 (수 µs 지만 ISR 지연에 더해진다) 을 함께 말해야 한다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_san && ./so_san
```

실제 출력 (PASS 줄 생략, 0.15 초):

```text
page size 16384
  (A: read faults 9, upgrades 4, pageins 9)
  (foreign PROT_NONE page: child died by signal, reported sig 10, term sig 10)
  (kill(getpid(), SIGSEGV): child died by signal, reported sig 11, term sig 11)
  (B: epoch 1 wrote 10 pages, checkpoint saved 10)
  (B: 8 epochs, 133 pages saved = 2128 KiB vs full copies 8 x 1024 KiB)
  (C: mode READ: touched 47, written 35, first-touch-is-write 29 -> faults 82)
  (C: mode ESR : touched 47, written 35, first-touch-is-write 29 -> faults 53)
  (C: atomic RMW on fresh page: n_read 1, n_upgrade 1, n_direct 0)
35/35 passed
```

5 회 연속 + 다른 작업 디렉터리(`/tmp`)에서 모두 `35/35 passed`. ASan+UBSan 빌드도 `35/35 passed`, 보고 없음, exit 0 — 우리 `sigaction` 이 ASan 의 SIGSEGV/SIGBUS 핸들러를 덮어쓰고(OS-B09 와 같은 관찰), `mmap` 영역은 ASan 이 계측하지 않는 메모리라 문제가 되지 않는다. 스레드를 안 쓰므로 TSan 은 해당 없음.

## 7. 자주 하는 실수

1. **고치지 못한 폴트에서 return** — 같은 명령이 다시 폴트 → 영원히 반복 (CPU 100 %, 출력 없음). 실제로 탐색 코드에서 `munmap` 된 영역에 대해 핸들러가 `mprotect`(실패, `ENOMEM`)만 하고 return 해서 2 분간 돌았다. → `mprotect` 반환값 확인, "RW 인데 폴트" 는 `false`, 하네스에 무진전 감지.
2. **SIGSEGV 만 잡기** — macOS 는 보호 위반을 SIGBUS 로 보낸다 (실측 전부). 리눅스에서만 시험하고 맥에서 죽는 전형. → 둘 다 같은 핸들러.
3. **`si_addr` 을 그대로 `mprotect`** — 바이트 주소라 `EINVAL`. → `base + (addr − base) / pg * pg`. 페이지 크기를 4096 으로 하드코딩하면 이 맥(16384)에서 엉뚱한 페이지를 연다. → `sysconf(_SC_PAGESIZE)`.
4. **내 영역 밖 폴트를 삼키기** — NULL 역참조가 조용히 무한 루프나 잘못된 매핑이 된다. → 범위 검사 후 `SIG_DFL` + `raise`. `si_code == SI_USER` 로 출처를 가르려 하지 말 것 (이 맥에서 kill 도 2).
5. **보호된 페이지를 시스템 콜 버퍼로 넘기기** — `read(fd, region, n)` 이 신호 없이 `EFAULT` (실측). 커널이 대신 쓰는 접근은 사용자 핸들러를 거치지 않는다. → 넘기기 전에 해당 페이지를 직접 한 번 만지거나 열어 두기 (dirty 표시 포함). 리눅스 uffd `USER_MODE_ONLY` 도 같은 이유로 커널 모드 폴트를 못 받는다 (`fs/userfaultfd.c:405`).
6. **체크포인트에서 상태만 READ 로 바꾸고 `mprotect` 를 빼먹기** — 다음 쓰기가 폴트를 안 내 dirty 를 놓친다 (돌연변이 시험: 저장 집합 불일치·복원 실패 3 FAIL).
7. **핸들러 안에서 `malloc`/`printf`** — 폴트가 malloc 내부(예: 우리 영역을 malloc 메타데이터가 만질 때)에서 났다면 malloc 락을 쥔 채 재진입 → 교착. → 사본 풀은 미리 할당 (연습 3), 출력은 `write` 로.
8. **ESR 을 EC 확인 없이 믿기** — `kill` 로 온 신호에서 ESR 은 SVC(EC 0x15)였다. WnR 비트만 보면 엉뚱한 판단. 그리고 CM=1(캐시 유지보수)은 쓰기가 아니다.
9. **멀티스레드에서 "RW 로 열고 채운 뒤 READ"** — 그 창 동안 다른 스레드가 반쯤 채운 페이지를 읽거나 쓸 수 있다. → 별도 별칭(alias) 매핑에 채운 뒤 원자적으로 교체하거나 (Q6), uffd `UFFDIO_COPY` 처럼 "채우기 + 매핑" 이 원자적인 API 를 쓴다.

## 8. 면접 Q&A

<details>
<summary>Q1. 페이지 폴트를 사용자 공간에서 처리한다는 게 정확히 무슨 뜻인가요? 커널은 뭘 하고 우리는 뭘 하죠?</summary>

하드웨어 MMU 가 폴트를 내면 커널이 먼저 받는다. 커널은 그 주소의 매핑이 "사용자가 걸어 둔 보호 때문에" 막혔다고 판단하면 (macOS: Mach `EXC_BAD_ACCESS` 를 BSD 신호로 바꿔) SIGBUS/SIGSEGV 를 보낸다. 핸들러가 `mprotect` 로 보호를 바꾸고 return 하면 커널이 문맥을 복원해 같은 명령을 다시 실행한다. 즉 **메커니즘(페이지 테이블 수정·TLB 무효화)은 커널**, **정책(언제 열고 무엇을 할지)은 우리** 다. Appel & Li (ASPLOS 1991) 가 이 원시 연산들 — TRAP, PROT1/PROTN, UNPROT, DIRTY, MAP2 — 을 정리하고 GC·DSM·체크포인트 등 쓰임을 모았다 (논문 기준). 꼬리: "왜 커널 COW 대신 이걸 쓰나?" — 정책을 바꿀 수 있어서다: 무엇을 저장할지, 어디서 페이지를 가져올지(네트워크, 압축 저장소), 어떤 순서로.
</details>

<details>
<summary>Q2. mprotect 는 async-signal-safe 가 아닌데 핸들러에서 불러도 되나요?</summary>

표준상으로는 안 된다: POSIX.1-2024 §2.4.3 목록에 없고, 같은 절은 "kill/sigqueue/raise 가 아닌 SIGSEGV/SIGBUS 핸들러에서 정상 return 한 뒤의 동작은 정의되지 않는다" 고 한다. 실무 근거는 두 가지다: (1) 폴트는 **동기** 신호라 그 명령에서만 일어나며, 우리 영역을 만지는 코드가 libc 내부 락을 쥔 상태가 아니게 설계할 수 있다. (2) `mprotect` 는 실제 구현에서 시스템 콜 래퍼 하나라 재진입 문제가 없다. 그래서 GC·JIT·libsigsegv 가 쓴다. 대신 플랫폼별 검증이 필요하고(이 레슨의 표), 핸들러에서 malloc·printf·락은 쓰지 않는다. 꼬리: "그럼 더 안전한 대안은?" — 리눅스 userfaultfd 는 폴트를 fd 로 받아 **일반 스레드** 에서 처리하므로 신호 안전성 문제가 사라진다 (`demo_linux_uffd.c` 실측 22 폴트 = 22 페이지).
</details>

<details>
<summary>Q3. 읽기 폴트인지 쓰기 폴트인지 어떻게 아나요? 이식성은?</summary>

이식성 있는 방법: 첫 폴트는 무조건 READ 로 열고, 다시 폴트가 나면 쓰기다 (쓰기 첫 접근은 폴트 2 번). arm64 macOS 에선 `uc_mcontext->__es.__esr` 의 WnR(bit 6)으로 한 번에 알 수 있다 — 실측 82 → 53 폴트. 단 EC == 0x24 를 확인해야 하고(kill 뒤엔 SVC 였다), CM=1 은 쓰기가 아니며, 원자 RMW 는 `PROT_NONE` 에서 WnR=0 으로 왔다. 그래도 틀리는 방향이 "쓰기를 읽기로" 라 결과는 안전하다 (폴트 1 번 더). 리눅스 arm64 는 `esr_context` 레코드, x86 은 page-fault error code 의 W 비트 (문서 기준). 꼬리: "어떤 걸 쓰겠나?" — 기본은 이식성 있는 방식, 폴트 비용이 병목일 때만 아키텍처별 빠른 길을 추가하고 **두 방식 교차 검증 테스트를 남긴다** (이 레슨 Part C).
</details>

<details>
<summary>Q4. 내 영역 밖의 폴트는 어떻게 처리해야 하죠? 왜 그냥 return 하면 안 되나요?</summary>

`SIG_DFL` 로 되돌리고 같은 신호를 `raise` 한다. 지금 신호는 핸들러 동안 막혀 있어서 return 순간 기본 동작(종료+코어)으로 전달되므로, 프로세스는 **원래 신호로** 죽고 디버거·크래시 리포트가 올바른 원인을 본다. 그냥 return 하면 하드웨어 폴트는 다시 일어나 (이미 SIG_DFL 이면) 죽지만, `kill` 로 온 신호는 다시 안 와서 **살아남는다** — 돌연변이 시험에서 실제로 exit 0. 이 맥에서는 kill 로 온 SIGSEGV 도 `si_code=2` 라 `SI_USER` 검사로 가를 수 없었다. 더 나은 라이브러리 설계: 설치 시 **이전 핸들러를 저장** 해 두고 내 것이 아니면 그것을 부른다 (연습 5, libsigsegv 방식) — ASan·Crashpad 같은 다른 핸들러와 공존해야 하니까.
</details>

<details>
<summary>Q5. 증분 체크포인트가 전체 복사보다 항상 빠른가요? 숫자로 말해 보세요.</summary>

아니다. 이 맥에서 사용자 폴트 1 번 ≈ 8.5~12 µs (커널만의 첫 접근 폴트 ≈ 1.4~2.1 µs 의 약 6 배), 64 MiB 전체 memcpy ≈ 1.6 ms (부하 load 22~30, 배수만 신뢰). 그래서 dirty 1 % (40 페이지) 는 증분이 3 배 이득, 5 % 에서 이미 손해, 25 % 면 10 배 손해. 비용의 70~80 % 가 폴트다. 그래도 증분을 쓰는 이유: 전체 복사는 **그 시간 동안 쓰기를 멈춰야** 일관된 사본이 되고, 전송·저장 바이트 수가 비용인 곳(라이브 마이그레이션, 플래시)에선 dirty 페이지 수가 곧 비용이다. 꼬리: "폴트 비용을 줄이려면?" — (1) 페이지를 크게(이 맥은 이미 16 KiB → 같은 데이터에 폴트가 4 KiB 페이지의 1/4), (2) 연속 구간 `mprotect` 한 번, (3) ESR 로 폴트 반, (4) 커널이 표시만 하는 방식(uffd WP_ASYNC + `PAGEMAP_SCAN`, soft-dirty) — 폴트가 사용자로 안 올라온다.
</details>

<details>
<summary>Q6. 이 설계가 틀리는 경우는? 멀티스레드라면 어떻게 바꾸겠나?</summary>

깨지는 곳 다섯 군데: (1) **멀티스레드 pagein** — "RW 로 열고 채우고 READ" 사이에 다른 스레드가 반쯤 채운 페이지를 본다. 고침: 같은 물리 메모리를 두 번 매핑(공유 메모리 객체를 RW 별칭 하나 + 보호된 뷰 하나)해 별칭에 채운 뒤 뷰를 READ 로 여는 방식, 또는 uffd `UFFDIO_COPY` 처럼 원자적 API. (2) **체크포인트 중 다른 스레드의 쓰기** — 복사와 재보호 사이에 쓴 내용이 사본에 안 들어가고 dirty 도 안 남는다. 고침: 재보호 **먼저**, 그 다음 복사 (쓰기는 폴트로 기다리거나 COW 사본으로 — 연습 3). (3) 상태 배열·카운터를 핸들러와 다른 스레드가 같이 만짐 → atomic 으로, 전이는 CAS 로 한 번만. (4) **시스템 콜 버퍼** — `EFAULT`. (5) **TLB 무효화 비용** — `mprotect` 로 보호를 줄이면 커널이 모든 CPU 의 TLB 를 비워야 한다(shootdown, OS-M01). 스레드가 많을수록 재보호가 비싸진다 — 리눅스 `PAGEMAP_SCAN` 이 "한 번에 많이" 처리하는 이유.
</details>

<details>
<summary>Q7. macOS 에선 왜 SIGBUS 고 리눅스에선 SIGSEGV 인가요? 코드는 어떻게 써야 하죠?</summary>

POSIX.1-2024 의 `mprotect`·`mmap` 페이지는 보호 위반에 **어떤 신호가 오는지 정하지 않는다** (`mmap` 은 객체 끝 너머 온전한 페이지 = SIGBUS 만 규정 — 확인함). 리눅스 `mmap(2)` man 은 SIGSEGV 로 적고, macOS `man 2 mprotect` 는 "a SIGBUS or SIGSEGV signal" 이라고만 쓴다. 실측: 이 맥은 `PROT_NONE`·`PROT_READ` 위반이 전부 SIGBUS(si_code 1), `munmap` 뒤·스택 가드는 SIGSEGV. 리눅스 arm64 는 같은 ESR 인데 SIGSEGV(`SEGV_ACCERR`). XNU 가 Mach 예외(`EXC_BAD_ACCESS` + kern 코드)를 BSD 신호로 바꾸는 규칙 때문인데, 크래시 리포트에선 `KERN_PROTECTION_FAILURE` 가 SIGBUS 로도 SIGSEGV(스택 가드)로도 적혀 있어 단순한 1:1 규칙은 아니었다 — 정확한 규칙은 XNU 소스를 확인하지 않았다. 코드는 **두 신호 모두 같은 핸들러**, 판단은 `si_code` 이름이 아니라 주소 범위와 내 상태 배열로.
</details>

<details>
<summary>Q8. 스택 오버플로를 잡는 핸들러는 왜 sigaltstack 이 필요하죠?</summary>

스택 오버플로는 스택 아래 가드 영역을 건드려 폴트가 난다. 커널은 핸들러를 부르기 위해 **현재 스택에** 신호 프레임을 쌓아야 하는데 그 스택이 바로 꽉 찬 곳이라 쌓을 수 없다 → 프로세스는 핸들러 없이 죽는다. `sigaltstack` 으로 별도 스택을 등록하고 `SA_ONSTACK` 을 주면 핸들러가 거기서 돈다. 실측 (연습 4): 대체 스택 없음 → 자식이 SIGSEGV 로 죽음(핸들러 메시지 없음), 있음 → 핸들러가 깊이 14 938 (ASan 빌드 11 363) 을 보고하고 exit 42. 이 맥에서 스택 가드는 SIGBUS 가 아니라 SIGSEGV 였다. 대체 스택은 스레드마다 따로 등록해야 한다. 펌웨어 대응: Cortex-M 은 예외 진입 시 MSP 로 쌓을 수 있어 태스크 스택(PSP)이 넘쳐도 폴트 핸들러가 돈다 — 대신 스태킹 자체가 실패하면 `MSTKERR` (ARM-M12).
</details>

<details>
<summary>Q9. 이걸 라이브 마이그레이션이나 컨테이너 체크포인트(CRIU)에 쓴다면 어떻게 설계하겠나?</summary>

pre-copy: 전체를 한 번 보내고 → 쓰기 보호 → 매 라운드 dirty 만 다시 보냄 → dirty 가 대역폭보다 빨리 줄어들면 멈추고 마지막 dirty 를 보낸 뒤 상대에서 재개. 이 레슨의 checkpoint 가 한 라운드다. 수렴 안 하면(쓰기 속도 > 전송 속도) 쓰기를 늦추거나(auto-converge) post-copy 로: 먼저 상대에서 재개하고, 없는 페이지는 폴트 때 가져온다 = 연습 1 의 **지연 복원**, 리눅스에선 uffd MISSING (`demo_linux_uffd.c`). 리눅스 실제 구현은 QEMU 는 KVM dirty log(2 단계 페이지 테이블 쓰기 보호), CRIU 는 soft-dirty 또는 `PAGEMAP_SCAN` (문서 기준; arm64 에선 soft-dirty 가 없어 bit55 = 0 실측). 측정: 라운드별 dirty 페이지 수와 다운타임(마지막 라운드 크기 / 대역폭). 꼬리: "post-copy 의 위험은?" — 원본이 죽으면 두 쪽 다 불완전하다. 그래서 pre-copy 를 기본으로 한다.
</details>

## 9. 추가 연습

### 연습 1 — 지연 복원 (post-copy)

`vmr_restore_lazy(r, s, e)`: 내용을 쓰지 말고 만진 페이지를 전부 `PROT_NONE` 으로 되돌린 뒤, 첫 접근의 pagein 이 저장소(epoch ≤ e 마지막 사본)에서 가져오게 하라. 처음 3 페이지만 만지면 pagein 3 번, 전체 비교는 바이트 일치.

<details>
<summary>답 보기</summary>

역사 잘라내기는 같고, 페이지마다 `mprotect(NONE)` + 상태 NONE, 그리고 `r->pagein = pagein_from_store` (전역 `g_src`, `g_src_epoch`). pagein 은 핸들러 안에서 불리므로 **검색과 memcpy 만** 한다 (할당 없음). 실제 출력: `(ex1: lazy restore to epoch 2, first 3 touches -> 3 pageins, full compare -> 32 pageins)`. 복원 비용이 "만지는 페이지 수" 에 비례한다 — 큰 프로세스를 빨리 재개하는 post-copy 의 핵심이고, 리눅스는 uffd MISSING 이 같은 일을 한다. 복원 뒤 체크포인트도 정확하다 (`n == nw && s.epoch == 3`).
</details>

### 연습 2 — 참조 비트 흉내 (워킹셋 재기)

OSTEP 23장 Aside(Babaoglu & Joy 1981)처럼, 상주 페이지를 모두 `PROT_NONE` 으로 "덫" 을 놓고 한 구간 동안 참조된 페이지 집합을 정확히 재라. 덫이 풀릴 때 원래 보호(READ 또는 RW)로 돌아가야 dirty 정보가 안 깨진다.

<details>
<summary>답 보기</summary>

상태에 `PS_TRAP (0x80)` 플래그: 아래 비트는 원래 상태. `ws_begin` 은 상주 페이지만 `PROT_NONE` + 플래그, 핸들러는 플래그가 있으면 `mprotect(prot_of(원래 상태))`, `ref[i] = 1`. pagein 은 하지 않는다 (내용은 유효). 실제 출력: `(ex2: resident 32 pages, working set in interval = 5 pages)` — 읽은 3 개와 쓴 2 개 정확히. 쓴 페이지가 원래 READ 였다면 덫 해제 → READ → 재실행 → 업그레이드로 RW 가 된다. 이것이 Clock 알고리즘(OS-B11)의 use 비트를 소프트웨어로 만드는 법이다. 덫을 너무 자주 놓으면 폴트 비용(≈ 10 µs)이 커지고, 너무 드물면 모든 페이지가 참조됨이 된다 — OSTEP 이 말하는 균형.
</details>

### 연습 3 — 쓰기 전 사본 스냅숏 (COW 식)

`snap_begin()` 순간의 내용을 멈추지 않고 보존하라: 모든 RW 페이지를 READ 로 내리고, 그 뒤 첫 쓰기 폴트에서 **쓰기 전 내용** 을 미리 할당한 풀로 복사. 스냅숏 읽기 = 사본이 있으면 사본, 없으면 현재 페이지.

<details>
<summary>답 보기</summary>

본문 체크포인트는 "끝에 dirty 를 복사" (redo/post-image), 이건 "쓰기 직전 원본을 복사" (undo/pre-image)다. 장점: 스냅숏 시작이 O(재보호) 로 즉시이고 그 뒤 쓰기는 계속된다 — `fork()` 후 자식이 메모리를 저장하는 Redis BGSAVE 가 커널 COW(OS-M03)로 얻는 효과를 사용자 공간에서 얻는다. 풀은 `snap_begin` 에서 할당(핸들러에서 malloc 금지). 실제 출력: `(ex3: snapshot kept by copying 20 of 32 pages on first write)` + 스냅숏 = 시작 순간 전체 사본과 일치, 사본 수 == 쓴 페이지 수.
</details>

### 연습 4 — sigaltstack 으로 스택 오버플로 잡기

자식 프로세스에서 끝없는 재귀를 돌려라. 대체 스택 없이 핸들러만 있을 때와 `sigaltstack` + `SA_ONSTACK` 일 때를 비교하라.

<details>
<summary>답 보기</summary>

재귀 함수는 `noinline` + 버퍼 주소를 다음 호출에 넘겨 꼬리 호출·누적기 변환을 막고, clang 의 `-Winfinite-recursion` 경고는 사실상 무한인 volatile 한계로 피한다. 실제 출력: `(ex4: altstack=0 -> signal 11, handler saw sig 0 at depth 0)` (핸들러가 못 돌고 SIGSEGV 로 죽음), `(ex4: altstack=1 -> exit 42, handler saw sig 11 at depth 14938)`. 핸들러는 `write` 로 깊이를 알리고 `_exit` — 오버플로 뒤 복구는 하지 않는다 (스택 상태를 믿을 수 없다). 첫 경우의 자식은 신호로 죽으므로 `.ips` 가 남는다.
</details>

### 연습 5 — 이전 핸들러로 넘기기 (chaining)

관리자를 설치하기 전에 다른 라이브러리가 SIGSEGV/SIGBUS 핸들러를 깔아 두었다. 내 영역의 폴트는 내가, 아니면 이전 핸들러가 받게 하라.

<details>
<summary>답 보기</summary>

`sigaction(sig, &new, &old)` 로 이전 것을 저장. 내 것이 아니면 `old` 가 `SA_SIGINFO` 함수면 `old.sa_sigaction(sig, si, uc)`, 일반 함수면 `old.sa_handler(sig)`, `SIG_DFL`/`SIG_IGN` 이면 본문의 기본 동작 경로. 시험: 바깥 핸들러(기록 + `siglongjmp`)를 먼저 깔고 → 관리자 설치 → 남의 `PROT_NONE` 페이지 접근 → 바깥 핸들러 1 번 호출, `si_addr` 그대로; 내 영역 쓰기는 관리자가 처리 (`n_read == 1 && n_upgrade == 1`). 함정: 이전 핸들러가 `SA_ONSTACK`·마스크를 기대할 수 있다 — 완벽한 체이닝은 없다. 크래시 리포터·ASan 과 공존하는 라이브러리는 이걸 꼭 해야 한다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san
```

실제 출력: `17/17 passed` (4 회, 0.15 초, exit 0), ASan+UBSan `17/17 passed` 보고 없음 (오버플로 깊이만 11 363 으로 다름 — ASan 이 프레임을 키운다).

## 10. 핵심 정리

- **예약 = `PROT_NONE`**, 첫 접근 = pagein + READ, 첫 쓰기 = RW. 핸들러는 정책, `mprotect` 는 커널의 손. 폴트 수 == 만진/쓴 페이지 수 (정확).
- **macOS 는 보호 위반이 SIGBUS** (si_code 1), unmapped·스택 가드는 SIGSEGV, kill 로 온 SIGSEGV 도 si_code 2. 두 신호를 같은 핸들러로, 판단은 주소 범위 + 내 상태 배열.
- 내 폴트가 아니면 **SIG_DFL + raise** → 원래 신호로 죽는다. 고치지 못한 폴트에서 return = 무한 루프.
- dirty 추적 = 쓰기 보호. 체크포인트 후 **RW 0 개** 불변식, 저장 집합 == 쓴 집합, restore 는 "e 이하 마지막 사본" + 역사 잘라내기.
- ESR: EC 0x24 확인 → WnR && !CM. 폴트 82 → 53. 원자 RMW 는 WnR=0 으로 올 수 있지만 틀리는 방향이 안전하다.
- 표준 밖이다: `mprotect` 는 async-signal-safe 목록에 없고, SIGSEGV 핸들러에서 return 은 POSIX 상 UB. 시스템 콜 버퍼는 신호 대신 `EFAULT`.
- 비용: 사용자 폴트 ≈ 9~12 µs (커널 폴트의 ~6 배) → 증분은 dirty 몇 % 이하에서만 이득. 리눅스는 uffd / `PAGEMAP_SCAN` 으로 신호 없이 (arm64 엔 soft-dirty 없음).
- 펌웨어: MPU RO 영역 + MemManage(`MMFAR`, MMARVALID) 가 같은 생각. 읽기/쓰기 비트가 없어 명령 디코드가 필요하다.

## 참고자료

- POSIX.1-2024 (IEEE Std 1003.1-2024) §2.4.3 Signal Actions — async-signal-safe 목록, SIGSEGV/SIGBUS 핸들러에서 정상 return 뒤 UB: <https://pubs.opengroup.org/onlinepubs/9799919799/functions/V2_chap02.html> · `sigaction` (SA_ONSTACK): <https://pubs.opengroup.org/onlinepubs/9799919799/functions/sigaction.html> · `mprotect`: <https://pubs.opengroup.org/onlinepubs/9799919799/functions/mprotect.html>
- macOS man pages (이 맥 macOS 27.2): `mprotect(2)`, `sigaction(2)` (async-signal safe 목록), `sigaltstack(2)`, `mincore(2)`; SDK 헤더 `MacOSX26.4.sdk/usr/include/mach/arm/_structs.h` (`__darwin_arm_exception_state64 { __far, __esr, __exception }`), `arm/_mcontext.h` (`__es`)
- ISO C11 (N1570) 7.14.1.1p5 — 신호 핸들러에서 접근 가능한 객체
- OSTEP 21장 (Swapping: Mechanisms — page fault), 22.9 (dirty bit), **23장 VAX/VMS — Aside "Emulating Reference Bits", demand zeroing, copy-on-write** (on-disk: `operating-system-three-easy-pieces/book-md/C23_the_vax_vms_virtual_memory_system.md`)
- Andrew W. Appel, Kai Li, "Virtual Memory Primitives for User Programs", ASPLOS-IV, 1991 (TRAP/PROT1/PROTN/UNPROT/DIRTY/MAP2)
- Ozalp Babaoglu, William Joy, "Converting a Swap-Based System to do Paging in an Architecture Lacking Page-Reference Bits", SOSP 1981 (OSTEP [BJ81])
- Kai Li, Paul Hudak, "Memory Coherence in Shared Virtual Memory Systems", ACM TOCS 7(4), 1989 (IVY DSM)
- GNU libsigsegv — <https://www.gnu.org/software/libsigsegv/> · Boehm-Demers-Weiser GC (incremental/"MPROTECT_VDB") — <https://github.com/ivmai/bdwgc> (구현 세부 미확인)
- Arm Architecture Reference Manual (DDI 0487) — ESR_ELx, EC, ISS for Data Abort (WnR, CM, DFSC): <https://developer.arm.com/documentation/ddi0487/latest/>
- Linux 6.18.55 (VM `/var/lab/src/linux-6.18.55`): `arch/arm64/mm/fault.c:534` `is_write_abort`, `arch/arm64/include/asm/esr.h:48,87,123`, `fs/userfaultfd.c:405` (UFFD_USER_MODE_ONLY), `fs/proc/task_mmu.c:1592,1865,2327` (soft-dirty, PAGEMAP_SCAN), `mm/Kconfig:1036` (MEM_SOFT_DIRTY), `arch/arm64/Kconfig:279` (USERFAULTFD_WP), `mm/memory.c:4063` `do_wp_page`
- Linux man-pages: userfaultfd(2) <https://man7.org/linux/man-pages/man2/userfaultfd.2.html>, ioctl_userfaultfd(2), proc_pid_pagemap(5); kernel docs `Documentation/admin-guide/mm/soft-dirty.rst`, `Documentation/admin-guide/mm/pagemap.rst` (PAGEMAP_SCAN)
- CRIU (memory changes tracking, lazy migration) — <https://criu.org/Memory_changes_tracking> · QEMU migration (dirty page logging) — 문서 기준
- ARMv7-M / ARMv8-M Architecture Reference Manual — MemManage fault, MMFSR, MMFAR
- 관련 레슨: OS-B07 (시그널·async-signal-safe), OS-B09 (mmap, SIGBUS/SIGSEGV 관찰), OS-B10/OS-B11 (페이지 테이블, Clock use 비트), OS-M01 (mprotect 와 TLB 무효화), OS-M03 (커널 COW 와 minflt), ARCH-A01/ARCH-A02 (페이지 테이블·TLB), ARM-M12 (CFSR/MMFAR 디코더), ARCH-M15 (MPU 영역 계산), BM-A08 (MPU 설정·가드), BM-A07 / RTOS-M09 (스택 오버플로 감지), C-M14 (ISR 규칙). 다음 단계: VIRT-M10 (2 단계 페이지 테이블 dirty 로그), VIRT-A02 (라이브 마이그레이션 수렴), STOR-B04 (버퍼 풀 dirty)
