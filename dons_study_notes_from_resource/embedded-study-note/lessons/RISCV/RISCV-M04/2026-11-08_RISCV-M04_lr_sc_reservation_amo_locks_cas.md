---
id: RISCV-M04
time: 95분
updated: 2026-11-08
---
# RISCV-M04 A 확장: LR/SC 예약과 AMO로 락·CAS 구현

## 학습 목표

- RISC-V A 확장(= Zalrsc + Zaamo)의 **AMO 포맷**(funct5 | aq | rl | rs2 | rs1 | 010 | rd | 0101111)을 손으로 인코딩하고, 내가 만든 인코더가 낸 259개 워드를 **진짜 어셈블러(clang 21 + llvm-objdump)** 와 한 비트도 안 틀리게 맞춘다.
- **예약 집합(reservation set)** 이 무엇이고 언제 깨지는지(다른 hart 의 store / AMO / 성공한 SC, 같은 값을 다시 써도), SC 가 실패하는 조건(예약 없음, 다른 SC 가 끼어듦, 가짜 실패)을 ISS 로 구현하고 규칙마다 테스트로 보인다.
- AMOSWAP 스핀락 · LR/SC CAS · AMOADD ticket lock 을 **기계어로** 짜서 2-hart ISS 에서 라운드로빈 + 시드 300개 랜덤 스케줄로 돌려 상호 배제와 정확한 카운트를 확인하고, 깨진 락은 같은 스트레스가 반드시 잡아내는 것도 보인다.
- **제약된 LR/SC 루프(constrained LR/SC loop)** 규칙(16 명령 이내, base I 만, load/store·뒤 방향 분기·JALR·FENCE·SYSTEM 금지)을 스펙 원문으로 인용하고, 정적 검사기 + "LR/SC 안의 load" 위반이 실제로 livelock 하는 구현을 만들어 본다.
- C11 atomics 가 RISC-V 명령으로 내려가는 모습(실측 asm), QEMU 가 SC 를 "값 비교 cmpxchg" 로 흉내 낸다는 것(소스 + 실측), ARM LDXR/STXR · LSE 와의 차이를 표로 설명한다.

## 1. 문제

"RISC-V 에서 CAS 는 어떻게 만드나요? LR 과 SC 사이에 다른 코어가 **같은 값**을 다시 쓰면 SC 는 성공하나요? LR/SC 루프 안에 함수 호출을 넣어도 되나요?" — RISC-V 코어를 쓰는 칩 회사(SiFive, Tenstorrent, Rivos, 가속기 팀의 관리 코어) 면접에서 실제로 나오는 비트 수준 질문이다. 펌웨어 쪽에서는 멀티 hart SoC(예: Kendryte K210 2코어, ESP32-P4 HP 2코어 — 데이터시트 기준, 미검증)에서 코어 간 메일박스·통계·락을 짤 때, 그리고 **단일 코어 MCU 에서도** ISR ↔ main 공유 카운터를 인터럽트 마스킹 없이 `amoadd.w` 한 명령으로 처리할 때 이 지식이 바로 쓰인다 (C-M14 의 PRIMASK 방식과 비교).

C-A03 에서 C11 `atomic_fetch_add` / `compare_exchange` 가 arm64 에선 `ldadd`/`cas`, Cortex-M4 에선 `LDREX/STREX` 로 내려가는 걸 봤다. 이 레슨은 같은 개념을 **RISC-V 명령 한 층 아래**에서 본다. RISC-V 에는 원자 명령이 두 종류 있다:

| 종류 | 명령 (RV32, .w) | 하는 일 | 2024 스펙 이름 |
|---|---|---|---|
| LR/SC | `lr.w rd,(rs1)` / `sc.w rd,rs2,(rs1)` | LR: 읽고 **예약 집합** 등록. SC: 예약이 살아 있으면 저장하고 rd=0, 아니면 저장 안 하고 rd≠0 | Zalrsc |
| AMO | `amoswap/amoadd/amoand/amoor/amoxor/amomin[u]/amomax[u].w rd,rs2,(rs1)` | 읽기-연산-쓰기를 **명령 하나로** 원자적으로, 옛 값을 rd 로 | Zaamo |

두 종류 모두 `.aq` / `.rl` / `.aqrl` 순서 비트가 붙는다. 그리고 "A" = Zalrsc + Zaamo 다 (20240411 판에서 둘로 쪼개짐). 칩마다 무엇이 있는지 꼭 확인해야 한다 — 예: GD32VF103 = RV32IMAC(A 있음), ESP32-C3 = RV32IMC(**A 없음**), ESP32-C6 = RV32IMAC (모두 데이터시트 기준, 미검증). A 가 없는 칩에서 C11 atomics 를 쓰면 clang 은 `__atomic_fetch_add_4` 같은 **라이브러리 호출**을 낸다(5. 풀이에서 실측) — Cortex-M0 와 같은 상황이다 (CPP-A04 참고).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">예약 집합: hart0 의 LR 이 64B 블록을 "찜", hart1 의 스토어가 그 블록에 닿으면 찜이 풀린다</text>
  <text x="10" y="62" fill="currentColor">hart0</text>
  <text x="10" y="172" fill="currentColor">hart1</text>
  <line x1="60" y1="58" x2="730" y2="58" stroke="currentColor"/>
  <line x1="60" y1="168" x2="730" y2="168" stroke="currentColor"/>
  <rect x="70" y="40" width="130" height="34" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="78" y="62" fill="currentColor" font-family="monospace">lr.w t0,(a0)</text>
  <rect x="210" y="40" width="120" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="218" y="62" fill="currentColor" font-family="monospace">addi t1,t0,1</text>
  <rect x="420" y="40" width="150" height="34" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="428" y="62" fill="currentColor" font-family="monospace">sc.w t2,t1,(a0)</text>
  <rect x="580" y="40" width="150" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="588" y="62" fill="currentColor" font-family="monospace">bnez t2, 다시 LR</text>
  <rect x="300" y="150" width="140" height="34" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="308" y="172" fill="currentColor" font-family="monospace">sw x,4(a0)</text>
  <rect x="70" y="86" width="430" height="22" rx="3" class="f-ok" stroke="currentColor"/>
  <text x="80" y="102" fill="currentColor">예약 유효: [a0 &amp; ~63, +64)  (이 ISS 기본값 = 64B 블록)</text>
  <path d="M370 150 L370 112" stroke="currentColor" stroke-width="2" fill="none"/>
  <path d="M364 122 L370 112 L376 122" stroke="currentColor" stroke-width="2" fill="none"/>
  <text x="380" y="138" fill="currentColor">이웃 워드(a0+4)라도 같은 블록 → 예약 무효</text>
  <text x="430" y="96" fill="currentColor">✗</text>
  <text x="380" y="230" fill="currentColor">SC: 예약 없음 → 메모리 안 씀, t2 = 1 (실패)</text>
  <text x="10" y="258" fill="currentColor">• 깨는 것: 다른 hart 의 store · AMO · 성공한 SC (같은 값을 써도)</text>
  <text x="10" y="276" fill="currentColor">• 안 깨는 것: 다른 hart 의 load · LR · 실패한 SC</text>
  <text x="10" y="294" fill="currentColor">• SC 는 성공이든 실패든 내 예약을 끝낸다. 예약은 hart 당 하나 (새 LR 이 대체)</text>
  <text x="10" y="312" fill="currentColor">• 입도를 워드(4B)로 바꾸면 위의 sw 는 예약을 깨지 않는다 (연습 3: SC 실패 1337 → 0)</text>
</svg>
```

이 레슨의 ISS 모델 (RISCV-B04 ISS 가 아직 없으므로 레슨 안에 필요한 만큼만 넣었다):

| 항목 | 이 ISS 의 선택 | 스펙이 허용하는 범위 |
|---|---|---|
| 명령 | RV32I 일부 + `lr.w sc.w amoswap.w amoadd.w` (+ 연습: AMO 9종), `fence`, `ebreak`(= hart 정지) | — |
| 실행 모델 | 한 스텝 = 한 hart 의 명령 1개, 원자적·즉시 전역 가시 (**순차 일관 인터리빙**) | RVWMO 는 훨씬 약하다 → `aq/rl/fence` 효과는 여기서 안 보임 (RISCV-A04) |
| 예약 집합 | 자연 정렬 `2^res_shift` B 블록, 기본 64B (캐시 라인 흉내), 옵션 4B | "접근 바이트를 포함하는 임의 크기" — 구현 정의, 플랫폼이 알려야 함 |
| 예약 무효화 | 다른 hart 의 sw / AMO / 성공한 SC 가 그 블록에 닿을 때, 내 SC 실행 시 | 내 store 가 내 예약을 깨는지는 규정 없음 → 깨지 않게 둠 |
| SC 실패 | 예약 없음 / 다른 블록 주소 / 가짜 실패 주입(`spurious_pct`) / 제약 위반 거부(`fail_unconstrained`) | "가끔 아무 이유로나 실패 가능 (진행 보장만 지키면)" |
| 스케줄러 | 라운드로빈(quantum 1,2,3,5,8) + 시드 고정 랜덤(hart 와 burst 1..6 을 xorshift 로) | — |
| 트랩 | 핸들러 없음: 트랩 = 그 hart 정지 + `cause`(mcause 번호) | 실제로는 mtvec 로 점프 (RISCV-M06) |

### Part A — AMO 인코더와 A 확장 실행

- `enc_amo(f5, aqrl, rs2, rs1, rd)` — AMO 포맷 인코더. 기대값은 VM 의 clang 21 이 낸 워드 (`lr.w.aq t0,(a0)` = `0x140522af` 등 7개).
- `exec_amo(m, hart, insn)` — `lr.w` (예약 등록), `sc.w` (예약 확인 → 저장/실패, 예약 종료), `amoswap.w`, `amoadd.w`. 정렬 안 된 주소는 트랩(LR = load 예외 4, SC/AMO = store/AMO 예외 6). 모든 저장은 `store32()` 로 → 다른 hart 의 예약을 깬다.
- 규칙마다 단위 테스트 15개: 예약 없이 SC → 실패, SC 두 번 → 둘째 실패, 남의 **같은 값** 스토어 → 실패, 이웃 워드는 64B 입도에서만 실패, 남의 load·LR·실패한 SC 는 영향 없음, `amoadd.w.aqrl t0,t0,(a1)` 처럼 rd == rs2 여도 옛 값 반환…

### Part B — 기계어로 락 3종, 랜덤 스케줄 스트레스

두 hart 가 같은 코드(0x0000)를 실행하고 `a0 = hartid` 로 시작한다 (OpenSBI 가 커널에 hartid 를 a0 로 넘기는 관례와 같다). 임계 구역은 공유 카운터를 **일반** `lw/addi/sw` 로 올리고, 점유 표시 워드로 동시 진입을 센다.

- (주어짐) `build_spinlock` — 스펙 예제 그대로의 test-and-test-and-set: `lw/bnez` 로 기다리고 `amoswap.w.aq` 로 잡고 `amoswap.w.rl zero,zero` 로 푼다.
- `build_cas_counter` — 스펙 예제 `cas` 서브루틴(`lr.w / bne / sc.w / bnez`)을 `jal ra` 로 불러 락 없이 카운터 +1. 실패하면 다시 읽기.
- `build_ticket_lock` — `amoadd.w` 로 번호표, `now_serving` 이 내 번호가 될 때까지 `lw`, `fence r,rw` → 임계 구역 → `fence rw,w` → `sw my+1`.
- 스트레스: RR 5가지 + 랜덤 시드 300개 = 305 스케줄 × 2 hart × 200 반복. 전부 `count == 400`, 동시 진입 0. 음성 대조군 2개(검사·설정 사이 틈이 있는 락, SC 결과를 안 보는 fetch_add)는 **반드시** 틀려야 한다.

### Part C — 듀얼 hart 드라이버 리뷰: 제약된 LR/SC 루프

드라이버 통계 `count += cfg.step` 을 LR/SC 로 짠 코드 리뷰. 작성자가 `cfg.step` 을 **LR 과 SC 사이에서** `lw` 했다.

- `lrsc_check(code, n, lr_idx, &info)` — 정적 검사기: LR→SC 사이 명령 종류, 주소 레지스터 보존, SC 의 rs1 동일, 재시도 분기 찾기, 루프 길이 ≤ 16. 위반 종류와 위치(`bad_idx`)를 돌려준다. 합성 사례 5개(17 명령, 주소 레지스터 변경, 뒤 방향 분기, `jalr`, 재시도 없음)도 판정.
- 실행: ISS 를 "제약 안 된 시퀀스의 SC 는 항상 실패" 구현으로 바꾸면(스펙이 허용) 나쁜 판은 **livelock**(스텝 한도), `lw` 를 루프 밖으로 올린 판은 정확히 끝난다. 관대한 구현에선 나쁜 판도 끝난다 — "우리 칩에선 되던데요" 의 함정.

## 2. Problem (English)

> **RISC-V "A" extension on a 2-hart instruction-set simulator.**
>
> You are given a small RV32I ISS (sequentially consistent interleaving: one step = one instruction of one hart) with a round-robin and a seeded random scheduler, a mini assembler, and a test harness.
>
> 1. Implement `uint32_t enc_amo(uint32_t funct5, int aqrl, int rs2, int rs1, int rd)` (AMO format, funct3 = 010) and `void exec_amo(machine_t *m, int hart, uint32_t insn)` for `lr.w`, `sc.w`, `amoswap.w`, `amoadd.w`. Reservation set = naturally aligned `2^res_shift`-byte block. It is invalidated by any store, AMO or successful SC **from another hart** to that block, and by any SC of the owning hart. `sc.w` writes 0 to `rd` on success, 1 on failure (no memory write). Misaligned addresses trap.
> 2. In machine code, write a lock-free counter using the LR/SC `cas` routine from the spec, and a ticket lock using `amoadd.w`. Both harts run the same code with `a0 = hartid`. Over 305 schedules the final count must be exact and the critical section never shared.
> 3. Write `int lrsc_check(const uint32_t *code, int n, int lr_idx, lrsc_info_t *out)` that decides whether the LR at `lr_idx` starts a *constrained LR/SC loop* as defined by the unprivileged spec, reporting the first violation.
>
> Constraints: C11, no dynamic allocation, deterministic (fixed seeds), every encoding must match a real RISC-V assembler.
>
> Follow-ups: Why can LR/SC be ABA-free while CAS is not? What breaks a reservation? What does an OS do with a reservation on a context switch? Why is `lw` inside an LR/SC loop a problem if it "works on my board"? How do `lr.w.aq`/`sc.w.rl` compare to `ldaxr`/`stlxr`, and AMOs to ARMv8.1 LSE?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter   # 경고 0, exit 1
```

처음 실행 결과 (실제 출력, 가운데 생략):

```text
FAIL starter.c:511  enc_amo(F5_LR, 0, ZERO, A0, T0) == 0x100522AFu
FAIL starter.c:512  enc_amo(F5_LR, AQ, ZERO, A0, T0) == 0x140522AFu
…(enc_amo 5줄, exec_amo 단위 테스트 15줄 생략)…
FAIL starter.c:603  r.st == RUN_OK && r.count == want && r.viol == 0
spinlock   : 305 bad / 305 schedules
FAIL starter.c:606  bad == 0
…
broken lock: 304 bad / 305 schedules (expected > 0)
sc no-retry: count 0 of 400
…
bad LR/SC loop, strict impl: trap, count 0, SC fails 0
FAIL starter.c:676  r.st == RUN_LIMIT && r.count == 0
FAIL starter.c:678  r.st == RUN_OK && r.count == want
1/43 passed
```

하네스 설계:

- **starter 가 멈추지 않는 이유**: `enc_amo` 스텁은 `0x00000000` 을 돌려준다. RISC-V 는 모든 비트가 0 인 명령을 **항상 illegal** 로 정해 두었으므로, 그 명령을 만난 hart 는 트랩으로 정지한다. 그리고 `run()` 은 `max_steps` 한도가 있다 (livelock 도 `RUN_LIMIT` 으로 끝남).
- 유일하게 통과하는 1개는 "깨진 락은 스트레스가 잡아낸다" — AMO 를 안 쓰는 대조군이라 starter 에서도 성립한다.
- 모든 CHECK 는 결정적이다: ISS 자체가 단일 스레드 C 코드이고 스케줄은 시드로 고정. "몇 번 실패했나" 같은 수는 출력만 하고, CHECK 는 "정확한 카운트, 동시 진입 0, (음성 대조군은) 반드시 틀림" 만 본다.

관찰용 파일 (하네스와 별개):

```bash
# Mac: 인코더 덤프 (solution.c 를 #include) → VM 의 clang 이 다시 어셈블해서 비교
cc -std=c11 -Wall -Wextra -Werror -O2 demo_enc.c -o demo_enc && ./demo_enc S > all.S && ./demo_enc hex > ours.txt
# Lima VM elinux (/tmp/RISCV-M04_work — 마운트가 읽기 전용이라 복사해서)
clang --target=riscv32 -march=rv32ia -mno-relax -c all.S -o all.o && llvm-objdump -d all.o > all.dis
awk '/^ *[0-9a-f]+: [0-9a-f]{8} /{print $2}' all.dis > clang.txt && diff ours.txt clang.txt && echo MATCH
clang --target=riscv32 -march=rv32ia -O2 -S demo_c11_map.c -o -     # C11 → RISC-V (A 있음)
clang --target=riscv32 -march=rv32i  -O2 -S demo_c11_map.c -o -     # A 없음 → libcall
riscv64-linux-gnu-gcc -std=c11 -O2 -Wall -Wextra -Werror -static -pthread demo_qemu_rv64.c -o dq && qemu-riscv64 ./dq
```

## 4. 힌트

<details>
<summary>힌트 1 — AMO 포맷은 R 포맷의 funct7 자리를 쪼갠 것</summary>

R 포맷 `funct7 | rs2 | rs1 | funct3 | rd | opcode` 에서 funct7 의 위 5비트가 funct5(연산 종류: LR=00010, SC=00011, AMOSWAP=00001, AMOADD=00000), 아래 2비트가 aq(bit 26)·rl(bit 25). funct3 = 010 이 ".w", opcode = 0101111 (0x2F). LR 은 rs2 자리가 0 이어야 한다. 검산: `lr.w t0,(a0)` → `0x100522af`.

</details>

<details>
<summary>힌트 2 — 예약은 (유효 비트, 블록 주소) 두 개면 충분</summary>

LR: `res_valid = true; res_base = 주소 & ~(블록크기-1)`. SC: `ok = res_valid && res_base == 블록(주소)` (+ `sc_forced_fail()`), 그 다음 **무조건** `res_valid = false`. 성공이면 `store32()` (이게 남의 예약을 깬다), rd = 0. 실패면 메모리 안 쓰고 rd = 1. AMO: 읽고, 계산하고, `store32()`, rd = 옛 값. rs2 값은 rd 를 쓰기 전에 읽어 둬야 한다.

</details>

<details>
<summary>힌트 3 — CAS 서브루틴은 스펙 예제 8줄</summary>

`cas: lr.w t0,(a0); bne t0,a1,fail; sc.w t0,a2,(a0); bnez t0,cas; li a0,0; ret; fail: li a0,1; ret`. 본체: `a0 = &count; a1 = lw count; a2 = a1+1; jal ra,cas; beqz a0,ok; 재시도++; j top; ok: …`. 앞 방향 분기는 `BNE(p, …, -1)` 로 자리만 잡고 나중에 `fix(p, at)`.

</details>

<details>
<summary>힌트 4 — ticket lock: 번호표만 원자, 반납은 주인만</summary>

`t1 = amoadd.w(next, 1)` (옛 값 = 내 번호). `wait: lw t2, serving; bne t2,t1,wait`. 들어가기 전 `fence r,rw`(acquire), 나오기 전 `fence rw,w`(release). `serving` 은 락을 가진 hart 만 쓰므로 `sw t1+1` 이면 된다 — 원자 RMW 가 필요 없다.

</details>

<details>
<summary>힌트 5 — 검사기: SC 를 찾고, 그 뒤에서 "LR 이전으로 가는" 분기를 찾는다</summary>

LR 다음부터 SC 전까지 각 명령: LUI/AUIPC/OP-IMM/OP(f7 0 또는 0x20)/앞 방향 BRANCH·JAL 만 허용, 그리고 LR 의 rs1 을 쓰면 안 된다. SC 의 rs1 도 같아야 한다. SC 뒤로 가며 `target <= lr_idx` 인 뒤 방향 분기를 찾으면 그게 재시도 분기 — 루프 = [target, 분기], 길이 ≤ 16. 그 전에 허용 안 되는 명령이 나오면 "루프 아님".

</details>

## 5. 풀이

### 0. A 확장 인코딩 한 장

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">AMO 포맷 (R 포맷 변형) — 예: lr.w.aq t0,(a0) = 0x140522af</text>
  <rect x="20" y="40" width="150" height="40" class="f-acc" stroke="currentColor"/>
  <rect x="170" y="40" width="30" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="200" y="40" width="30" height="40" class="f-warn" stroke="currentColor"/>
  <rect x="230" y="40" width="110" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="340" y="40" width="110" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="450" y="40" width="80" height="40" class="f-ok" stroke="currentColor"/>
  <rect x="530" y="40" width="100" height="40" class="f-mut" stroke="currentColor"/>
  <rect x="630" y="40" width="100" height="40" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="60" y="65">funct5</text><text x="176" y="65">aq</text><text x="208" y="65">rl</text>
    <text x="270" y="65">rs2</text><text x="380" y="65">rs1</text><text x="468" y="65">funct3</text>
    <text x="568" y="65">rd</text><text x="650" y="65">opcode</text>
    <text x="20" y="98" font-family="monospace">31     27</text><text x="172" y="98" font-family="monospace">26</text>
    <text x="202" y="98" font-family="monospace">25</text><text x="232" y="98" font-family="monospace">24   20</text>
    <text x="342" y="98" font-family="monospace">19   15</text><text x="452" y="98" font-family="monospace">14 12</text>
    <text x="532" y="98" font-family="monospace">11    7</text><text x="632" y="98" font-family="monospace">6     0</text>
    <text x="20" y="126" font-family="monospace">00010 (LR)</text><text x="176" y="126" font-family="monospace">1</text>
    <text x="208" y="126" font-family="monospace">0</text><text x="232" y="126" font-family="monospace">00000 (x0)</text>
    <text x="342" y="126" font-family="monospace">01010 (a0)</text><text x="460" y="126" font-family="monospace">010</text>
    <text x="532" y="126" font-family="monospace">00101 (t0)</text><text x="632" y="126" font-family="monospace">0101111</text>
    <text x="20" y="160">funct5: AMOADD 00000 · AMOSWAP 00001 · LR 00010 · SC 00011 · AMOXOR 00100</text>
    <text x="20" y="180">        AMOOR 01000 · AMOAND 01100 · AMOMIN 10000 · AMOMAX 10100</text>
    <text x="20" y="200">        AMOMINU 11000 · AMOMAXU 11100   (funct3 010 = .w, 011 = .d 는 RV64 전용)</text>
    <text x="20" y="226">aq rl: 00 순서 없음 · 10 .aq(acquire) · 01 .rl(release) · 11 .aqrl(순차 일관)</text>
    <text x="20" y="252">이진: 00010 1 0 00000 01010 010 00101 0101111</text>
    <text x="20" y="272">    = 0001 0100 0000 0101 0010 0010 1010 1111 = 0x140522af</text>
  </g>
</svg>
```

정답 인코더는 한 줄이다: `f5 << 27 | aqrl << 25 | rs2 << 20 | rs1 << 15 | 2 << 12 | rd << 7 | 0x2F`. (B/J 포맷의 뒤섞인 즉치값 비트는 주어진 `enc_b` / `enc_j` 에 있다 — RISCV-B01 주제.)

#### 인코딩 교차 검증 — `demo_enc.c` (실측)

`demo_enc.c` 는 `solution.c` 를 `#include` 해서 **레슨의 모든 프로그램**(스핀락, 깨진 락, CAS, ticket, SC 미확인, step 좋은/나쁜 판) + 즉치값 경계 모음(`addi -2048/2047`, `lui 0xfffff`, `lw -2048(s0)`, 40 명령 건너뛰는 `jal`, 6가지 분기의 뒤 방향 먼 분기, `srai`, `sub`, `fence iorw,iorw` …) + AMO 4종 × 순서 비트 4가지를 덤프한다. 텍스트는 ISS 와 같은 디코드 함수로 만든 디스어셈블리라, 인코더·디코더가 **같이** 틀리면 텍스트는 멀쩡해 보이고 clang 이 낸 워드와 달라져 잡힌다.

<!-- code: demo_enc.c -->

```text
$ (VM) clang --version | head -1
Ubuntu clang version 21.1.8 (6ubuntu1)
$ (VM) clang --target=riscv32 -march=rv32ia -mno-relax -c all.S -o all.o && llvm-readelf -r all.o
There are no relocations in this file.
$ (VM) diff ours.txt clang.txt && echo "ALL $(wc -l < ours.txt) ENCODINGS MATCH"
ALL 259 ENCODINGS MATCH
```

`-mno-relax` 는 링커 완화(relaxation)용 재배치를 막아 분기 오프셋이 `.o` 에 바로 박히게 한다 (재배치 0개 확인). 같은 objdump 의 CAS 프로그램 부분:

```text
      e4: 08090513     	addi	a0, s2, 0x80
      e8: 08092583     	lw	a1, 0x80(s2)
      ec: 00158613     	addi	a2, a1, 0x1
      f0: 02c000ef     	jal	0x11c <cas_19>
      f4: 00050663     	beqz	a0, 0x100 <cas_12>
      f8: 001b0b13     	addi	s6, s6, 0x1
      fc: fe9ff06f     	j	0xe4 <cas_5>
     100: fffa0a13     	addi	s4, s4, -0x1
     104: fe0a10e3     	bnez	s4, 0xe4 <cas_5>
 …
     11c: 100522af     	lr.w	t0, (a0)
     120: 00b29a63     	bne	t0, a1, 0x134 <cas_25>
     124: 18c522af     	sc.w	t0, a2, (a0)
     128: fe029ae3     	bnez	t0, 0x11c <cas_19>
     12c: 00000513     	li	a0, 0x0
     130: 00008067     	ret
     134: 00100513     	li	a0, 0x1
     138: 00008067     	ret
```

objdump 는 `addi a0,zero,0` 을 `li`, `jalr zero,0(ra)` 를 `ret`, `bne x,zero` 를 `bnez` 로 **별칭(pseudo)** 출력한다 — 워드는 같다.

### Part A — 예약 집합과 SC 실패 조건

스펙 원문 (Unprivileged ISA 20240411, 14장 "A" Extension v2.1, §14.2 Zalrsc — 장 번호는 소스 include 순서로 셈; 20191213 판은 8장 §8.2 에 거의 같은 문장):

> "An implementation can register an arbitrarily large reservation set on each LR, provided the reservation set includes all bytes of the addressed data word or doubleword. An SC can only pair with the most recent LR in program order. An SC may succeed only if no store from another hart to the reservation set can be observed to have occurred between the LR and the SC, and if there is no other SC between the LR and itself in program order."
>
> "Regardless of success or failure, executing an SC.W instruction invalidates any reservation held by this hart."
>
> "The failure code with value 1 encodes an unspecified failure. … Portable software should only assume the failure code will be non-zero."

규칙 → 코드 → 테스트 대응:

| 스펙 규칙 | `exec_amo` / `store32` 코드 | 단위 테스트 (solution.c) |
|---|---|---|
| LR 은 예약 등록, 새 LR 이 옛 예약 대체 | `res_valid = true; res_base = res_block(m, a)` | LR→SC 성공, rd=0, 메모리 42 |
| 예약 없이 SC → 실패, 메모리 안 씀 | `ok = res_valid && …` | SC 단독 → rd≠0, 값 7 그대로 |
| SC 는 성공·실패 무관하게 예약 종료 | `h->res_valid = false` (무조건) | SC 두 번 → 둘째 실패 |
| 다른 hart 의 store 가 예약 집합에 닿으면 실패 | `notify_store()` 가 다른 hart 의 `res_valid` 를 끔 | 남이 **같은 값 8** 을 써도 실패 |
| 예약 집합은 워드보다 클 수 있다 | `res_shift` 6 (64B) / 2 (4B) | 이웃 워드 스토어: 64B 실패, 4B 성공 |
| load·LR 은 남의 진행을 막지 않는다 | load 는 `notify_store` 를 안 부름 | 다른 블록 sw + 같은 워드 lw + LR → 성공 |
| AMO·성공한 SC 도 스토어다 | AMO 와 SC 성공 경로 모두 `store32()` | 남의 amoadd → 실패, 남이 먼저 SC 성공 → 실패 |
| 실패한 SC 는 쓰지 않으니 남을 방해 안 함 | 실패 경로는 `store32` 없음 | 남의 실패 SC → 내 SC 성공 |
| 가끔 아무 이유로 실패 가능 | `sc_forced_fail()` | 가짜 실패 100% → 실패 |
| 자연 정렬 필수 | LR → 트랩 4, SC/AMO → 트랩 6 | `lr.w` / `amoswap.w` 에 0x1002 |

왜 이렇게 생겼나:

1. **ABA 면역**: 예약은 "값" 이 아니라 "누가 그 블록에 썼는가" 를 본다. 그래서 LR 후 남이 5 → 7 → 5 로 바꿔 놓아도 SC 는 실패한다. 값만 비교하는 CAS (x86 `cmpxchg`, ARM LSE `cas`) 는 이걸 못 잡는다 — 스펙 노트가 LR/SC 를 고른 첫째 이유로 든 점이다. (단, 아래 QEMU 는 이 성질을 흉내 내지 않는다.)
2. **입도**: 실제 코어는 보통 L1 캐시 라인 단위로 예약을 추적한다 (그래서 기본 64B 로 둠 — 대표값, 칩마다 다름). 그러면 **같은 라인의 다른 변수** 에 대한 스토어도 SC 를 깬다 = 예약판 false sharing. 락 워드와 자주 쓰는 데이터를 같은 라인에 두지 말라는 이유다 (연습 3: SC 실패 0 → 1337). 이 ISS 의 데이터는 변수마다 다른 64B 블록에 있다 (`O_LOCK 0x000`, `O_SERVE 0x040`, `O_COUNT 0x080` …).
3. **rd == rs2**: `amoadd.w.aqrl t0,t0,(a1)` 은 "t0 를 더하고 옛 값을 t0 로". 구현에서 rd 를 먼저 쓰면 더할 값이 사라진다 → `src = h->x[rs2]` 를 맨 앞에서 읽는다. 하드웨어도 레지스터 읽기 단계가 쓰기보다 먼저다.

AMOSWAP / AMOADD 는 ISS 의 한 스텝 안에서 읽기-계산-쓰기를 끝내므로 사이에 아무것도 끼지 못한다. 실제 하드웨어는 이것을 캐시 라인 독점(코어 안에서) 또는 **메모리 컨트롤러/인터커넥트 쪽 원자 연산**(far atomic, 대표 예: AMBA AXI5 의 atomic transaction)으로 구현할 수 있다 — 스펙 노트도 "AMO 를 메모리 컨트롤러에서 구현할 수 있다" 고 말한다. 그래서 AMO 는 많은 코어가 한 카운터를 두드릴 때 LR/SC 보다 확장성이 좋다 (같은 노트: "scale to highly parallel systems better than LR/SC or CAS").

### Part B — 락 3종과 스트레스

#### 스핀락: 스펙의 test-and-test-and-set

```text
        li           t0, 1        # 스펙 예제 (Zaamo 절)
again:  lw           t1, (a0)     # 잡혀 있으면 읽기만 (캐시 라인 공유 상태 유지 → 버스 조용)
        bnez         t1, again
        amoswap.w.aq t1, t0, (a0) # 옛 값 0 이면 내가 잡음. aq: 임계 구역 접근이 이보다 앞서 보이지 않게
        bnez         t1, again
        # critical section
        amoswap.w.rl x0, x0, (a0) # 해제. rl: 임계 구역 접근이 이보다 먼저 보이게
```

스펙은 해제도 `sw` 대신 `amoswap.w.rl` 을 권한다(투기적 락 생략, speculative lock elision 구현을 단순하게). `fence rw,w; sw x0` 도 release store 로 맞다 — 아래 C11 매핑에서 clang 이 `store_release` 에 낸 것이 바로 그것이다.

#### CAS: 4 명령 루프, 앞 방향 탈출

`cas` 서브루틴의 루프는 `lr.w / bne / sc.w / bnez` **4 명령**이다. `bne t0,a1,fail` 은 루프 밖으로 나가는 **앞 방향** 분기라 제약 규칙 안이다. 실패에는 두 종류가 있다:

| 실패 | 어디서 | 처리 | 305 스케줄 합계 (실측) |
|---|---|---|---|
| 값 불일치 (남이 이미 올림) | `bne t0,a1,fail` → a0 = 1 | 본체가 count 를 다시 읽음 (`s6++`) | CAS retries 45287 |
| 예약 깨짐 (LR~SC 사이 남의 스토어) | `sc.w` → t0 = 1 | `bnez t0,cas` 로 LR 부터 바로 다시 | SC fails 17624 |

LR/SC 판 CAS 는 "값이 같으면" 이 아니라 "값이 같고 **그 사이 아무도 안 썼으면**" 이다 — 값 비교 CAS 보다 강하다.

#### ticket lock: AMOADD + fence

```text
        amoadd.w t1, t0, (a1)     # my = next++  (번호 발급만 원자 — relaxed 로 충분)
wait:   lw       t2, 0x40(s2)     # now_serving
        bne      t2, t1, wait
        fence    r, rw            # acquire: serving 을 읽은 뒤에야 임계 구역 접근
        ...임계 구역...
        fence    rw, w            # release: 임계 구역 접근이 serving 쓰기보다 먼저
        addi     t1, t1, 1
        sw       t1, 0x40(s2)     # 주인만 쓰는 변수 → 일반 sw
```

번호표가 FIFO 순서를 정하므로 **공정** 하다 (스핀락은 운 좋은 hart 가 계속 잡을 수 있음). 대가: 대기자 전원이 `now_serving` 한 라인을 읽으며 기다리고, 앞사람이 선점되면 뒷사람 전원이 기다린다 (OS-M07 이 성능 측정). 위의 `fence r,rw` / `fence rw,w` 워드(`0x0230000f` / `0x0310000f`)도 objdump 로 확인했다.

#### 스트레스 결과 (solution 실제 출력)

```text
spinlock   : 0 bad / 305 schedules
cas counter: 0 bad, CAS retries 45287, SC fails 17624 (over 300 seeds)
cas counter: 0 bad with 20% spurious SC failures (SC fails 56794)
ticket lock: 0 bad / 305 schedules
broken lock: 304 bad / 305 schedules (expected > 0)
sc no-retry: count 331 of 400
```

- 깨진 락(`lw lock; bnez; sw 1`)은 305 중 304 스케줄에서 동시 진입 또는 카운트 손실. 통과한 1개는 운 좋게 겹치지 않은 스케줄이다 — **스트레스를 시드 하나로 끝내면 안 되는 이유**. 실패 시나리오: hart0 `lw`(0) → hart1 `lw`(0) → hart0 `bnez` 통과, `sw 1` → hart1 `bnez` 통과(이미 읽은 0), `sw 1` → 둘 다 임계 구역.
- SC 결과를 안 보는 fetch_add 는 400 중 69 를 잃었다 (실패한 SC 는 아무것도 안 쓰므로 그 증가가 사라짐).
- SC 가짜 실패 20% 를 넣어도 CAS 카운터는 정확하다 — 재시도 루프가 있는 한 가짜 실패는 성능 문제일 뿐 정확성 문제가 아니다.
- 모든 체크는 SC 인터리빙 모델 위에서다. 약한 메모리에서 `aq/rl/fence` 를 빼면 무엇이 깨지는지는 이 ISS 로 볼 수 없다 → RISCV-A04 (RVWMO 리트머스), ARM-M05 (arm64 실측).

### Part C — 제약된 LR/SC 루프 (constrained LR/SC loop)

스펙 원문 (Unprivileged ISA 20240411 §14.3 "Eventual Success of Store-Conditional Instructions"; 20191213 판 §8.3 은 첫 줄이 "The standard A extension defines…" 이고 금지 목록에 FENCE.I 가 더 적혀 있다):

> "The Zalrsc extension defines *constrained LR/SC loops*, which have the following properties:
> - The loop comprises only an LR/SC sequence and code to retry the sequence in the case of failure, and must comprise at most 16 instructions placed sequentially in memory.
> - An LR/SC sequence begins with an LR instruction and ends with an SC instruction. The dynamic code executed between the LR and SC instructions can only contain instructions from the base ''I'' instruction set, excluding loads, stores, backward jumps, taken backward branches, JALR, FENCE, and SYSTEM instructions. If the ''C'' extension is supported, then compressed forms of the aforementioned ''I'' instructions are also permitted.
> - The code to retry a failing LR/SC sequence can contain backwards jumps and/or branches to repeat the LR/SC sequence, but otherwise has the same constraint as the code between the LR and SC.
> - The LR and SC addresses must lie within a memory region with the *LR/SC eventuality* property. …
> - The SC must be to the same effective address and of the same data size as the latest LR executed by the same hart.
>
> LR/SC sequences that do not lie within constrained LR/SC loops are *unconstrained*. Unconstrained LR/SC sequences might succeed on some attempts on some implementations, but might never succeed on other implementations."

그리고 보장: 제약 루프에 들어간 hart 는 결국 (자기나 남의 SC 성공 / 남의 store·AMO / 루프 탈출 / 트랩) 중 하나가 일어난다 — **livelock-free** (예전 판의 starvation-free 보다 약함). 노트에 이유도 있다: 16 명령 = 64 바이트로 I-cache·TLB 제약을 줄이고, 루프 안 load/store 금지는 "예약을 자기 private 캐시에서 추적하는 단순한 구현" 의 D-cache 연관도 제약을 피하려는 것. 그리고 "Implementations are permitted to unconditionally fail any unconstrained LR/SC sequence."

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C 코드 리뷰: 같은 일, 한쪽만 제약 루프</text>
  <text x="20" y="48" fill="currentColor">나쁜 판 (bad_idx = LR+1)</text>
  <rect x="20" y="58" width="320" height="30" class="f-acc" stroke="currentColor"/>
  <text x="30" y="78" fill="currentColor" font-family="monospace">retry: lr.w t0,(a1)</text>
  <rect x="20" y="88" width="320" height="30" class="f-warn" stroke="currentColor"/>
  <text x="30" y="108" fill="currentColor" font-family="monospace">       lw   t5,0x180(s2)  ✗ load</text>
  <rect x="20" y="118" width="320" height="30" class="f-mut" stroke="currentColor"/>
  <text x="30" y="138" fill="currentColor" font-family="monospace">       add  t0,t0,t5</text>
  <rect x="20" y="148" width="320" height="30" class="f-acc" stroke="currentColor"/>
  <text x="30" y="168" fill="currentColor" font-family="monospace">       sc.w t1,t0,(a1)</text>
  <rect x="20" y="178" width="320" height="30" class="f-mut" stroke="currentColor"/>
  <text x="30" y="198" fill="currentColor" font-family="monospace">       bnez t1,retry</text>
  <text x="400" y="48" fill="currentColor">좋은 판 (loop_len = 4)</text>
  <rect x="400" y="58" width="300" height="30" class="f-ok" stroke="currentColor"/>
  <text x="410" y="78" fill="currentColor" font-family="monospace">       lw   t5,0x180(s2)  ← 루프 밖</text>
  <rect x="400" y="88" width="300" height="30" class="f-acc" stroke="currentColor"/>
  <text x="410" y="108" fill="currentColor" font-family="monospace">retry: lr.w t0,(a1)</text>
  <rect x="400" y="118" width="300" height="30" class="f-mut" stroke="currentColor"/>
  <text x="410" y="138" fill="currentColor" font-family="monospace">       add  t0,t0,t5</text>
  <rect x="400" y="148" width="300" height="30" class="f-acc" stroke="currentColor"/>
  <text x="410" y="168" fill="currentColor" font-family="monospace">       sc.w t1,t0,(a1)</text>
  <rect x="400" y="178" width="300" height="30" class="f-mut" stroke="currentColor"/>
  <text x="410" y="198" fill="currentColor" font-family="monospace">       bnez t1,retry</text>
  <path d="M345 193 Q370 130 345 73" stroke="currentColor" fill="none" stroke-width="2"/>
  <path d="M705 193 Q735 145 705 103" stroke="currentColor" fill="none" stroke-width="2"/>
  <text x="20" y="236" fill="currentColor">엄격한 구현(fail_unconstrained): 나쁜 판 → SC 79997번 실패, 스텝 한도 (livelock)</text>
  <text x="20" y="254" fill="currentColor">                                  좋은 판 → 1200 정확</text>
  <text x="20" y="272" fill="currentColor">관대한 구현: 둘 다 1200 — "우리 보드에선 됐어요" 가 이식성을 보장하지 않는 이유</text>
  <text x="20" y="290" fill="currentColor">(count += cfg.step, step = 3, 2 hart × 200 → 기대값 1200)</text>
</svg>
```

#### 검사기 알고리즘 (`lrsc_check`)

| 단계 | 하는 일 | 위반 코드 |
|---|---|---|
| 0 | `code[lr_idx]` 가 LR 인가 | `LRSC_NOT_LR` |
| 1 | LR 다음 ~ SC 전: LUI/AUIPC/OP-IMM/OP(f7 0·0x20)/앞 방향 BRANCH·JAL 만 | `LRSC_BAD_INSN` (load·store·jalr·fence·system·AMO·MUL) / `LRSC_BACKWARD` |
| 1' | 그 사이 명령이 LR 의 rs1 을 덮어쓰면 | `LRSC_ADDR_CHANGED` |
| 2 | SC 가 없음 / SC 의 rs1 ≠ LR 의 rs1 | `LRSC_NO_SC` / `LRSC_ADDR_CHANGED` |
| 3 | SC 뒤: `target ≤ lr_idx` 인 뒤 방향 분기까지 같은 제약 (LR 앞쪽 재시도 코드도) | `LRSC_NO_LOOP` |
| 4 | 루프 [target, 분기] 길이 ≤ 16 | `LRSC_TOO_LONG` |

한계(정직하게): 정적 검사는 "동적으로 실행되는" 경로를 근사한다 — 앞 방향 분기로 건너뛴 명령도 검사하므로 보수적이다. "같은 주소" 는 레지스터 이름 비교일 뿐 값 추적은 안 한다. 그래서 ISS 쪽에도 **동적 감시**(`step()` 의 `track` 블록: LR 이후 실행된 명령이 금지 종류이거나, 뒤로 가거나, 16개를 넘으면 `lrsc_bad`)를 두었다. 실제 컴파일러 출력(C11 CAS → `lr.w.aqrl / bne / sc.w.rl / bnez`, 아래)이 이 규칙을 지키는 것도 확인할 수 있다.

### C11 → RISC-V: 컴파일러가 실제로 내는 것 — `demo_c11_map.c`

<!-- code: demo_c11_map.c -->

VM, `clang --target=riscv32 -march=rv32ia -O2 -S` (실측, 주소 계산 `lui/addi %hi/%lo` 생략):

| C11 | rv32ia 출력 | 메모 |
|---|---|---|
| `fetch_add(relaxed)` | `amoadd.w a0, a1, (a0)` | 순서 비트 없음 |
| `fetch_add(acq_rel)` / `seq_cst` | `amoadd.w.aqrl a0, a1, (a0)` | 둘 다 `.aqrl` |
| `exchange(acquire)` | `amoswap.w.aq a0, a0, (a1)` | rd == rs2 (힌트 2) |
| `load(acquire)` | `lw` ; `fence r, rw` | |
| `load(seq_cst)` | `fence rw, rw` ; `lw` ; `fence r, rw` | |
| `store(release)` | `fence rw, w` ; `sw` | 스핀락 해제의 다른 형태 |
| `store(seq_cst)` | `fence rw, w` ; `sw` ; `fence rw, rw` | 뒤 fence 까지 붙음 (이유: 다른 매핑과 섞여도 안전한 보수적 선택으로 보임 — 미검증) |
| `compare_exchange_strong(seq_cst)` | `lr.w.aqrl a2,(a4)` ; `bne a2,a3,.LBB8_5` ; `sc.w.rl a5,a1,(a4)` ; `bnez a5,.LBB8_3` | 4 명령 제약 루프, 스펙 노트의 seq_cst 조합 (LR.aq + SC.rl) |
| `compare_exchange_weak` 루프 (relaxed max) | `lr.w a3,(a2)` ; `bne a3,a1,…` ; `sc.w a4,a0,(a2)` ; `bnez a4,…` | |

`-march=rv32i` (A 없음): 모든 연산이 `call __atomic_fetch_add_4` / `__atomic_exchange_4` / `__atomic_load_4` / `__atomic_store_4` / `__atomic_compare_exchange_4` 가 되고 마지막 인자로 memorder 상수(`li a2, 0` relaxed, `2` acquire, `3` release, `4` acq_rel, `5` seq_cst)를 넘긴다. clang 경고도 나온다: `the access size (4 bytes) exceeds the max lock-free size (0 bytes) [-Watomic-alignment]`. 흥미롭게도 `-march=rv32i_zaamo` (AMO 만, LR/SC 없음)도 clang 21 은 **전부 libcall** 로 냈다(실측: `call __atomic_fetch_add_4` 3회 등) — CAS 를 인라인으로 못 만드니 "lock-free 크기 0" 으로 본 것으로 보인다. 이 libcall 을 누가 구현하나? 단일 코어 MCU 면 보통 SDK 가 인터럽트를 끄고 하는 구현을 제공한다 (CPP-A04 의 Cortex-M0 논의와 같은 구조, 플랫폼마다 다름 — 미검증).

### QEMU 는 SC 를 어떻게 흉내 내나 — 소스 + `demo_qemu_rv64.c` 실측

QEMU 10.2.0 `target/riscv/insn_trans/trans_rva.c.inc` 의 `gen_sc` (발췌, 한국어 주석만 추가):

```c
    tcg_gen_brcond_tl(TCG_COND_NE, load_res, src1, l1);   /* LR 주소와 다르면 실패 */
    ...
    tcg_gen_atomic_cmpxchg_tl(dest, load_res, load_val, src2,
                              ctx->mem_idx, mop);
    tcg_gen_setcond_tl(TCG_COND_NE, dest, dest, load_val);
```

즉 **LR 때 읽은 값(load_val)과 지금 값이 같으면 cmpxchg 로 성공**. 다른 hart 의 스토어가 "예약을 깨는" 개념이 없다 (이 파일 안에서 `load_res` 를 -1 로 지우는 곳은 자기 SC 뿐 — 다른 경로(트랩 등)는 확인 안 함). 같은 asm 을 qemu-riscv64 사용자 모드(게스트 스레드 = 호스트 스레드, 병렬)에서 돌린 실제 출력 (3회 모두 동일):

<!-- code: demo_qemu_rv64.c -->

```text
Part 1 (4 threads x 100000):
  spinlock  plain_a = 400000  OK
  cas       g_cas   = 400000  OK
  ticket    plain_b = 400000  OK (serve=400000 next=400000)
Part 2 (x=5, A: lr.w; B stores; A: sc.w 99). Spec: any store by another hart -> SC must fail
  B stores 5 (same value)            SC rd=0 (success), x=99
  B stores 7 then 5 (A-B-A)          SC rd=0 (success), x=99
  B stores 7 (value changed)         SC rd=1 (fail), x=7
```

(VM: riscv64-linux-gnu-gcc 15.2.0, qemu-riscv64 10.2.1. 바이너리 안의 명령은 `riscv64-linux-gnu-objdump -d` 로 `amoswap.w.aq`, `amoswap.w.rl`, `lr.w`, `sc.w`, `amoadd.w`, `fence r,rw`, `fence rw,w` 확인.) Part 2 의 A 시퀀스는 LR 과 SC 사이에 store·load·뒤 방향 분기가 있는 **제약 안 된** 시퀀스라 실제 칩에선 어떤 결과든 "허용" 되지만, 남의 스토어가 관찰됐으므로 스펙상 SC 는 **반드시** 실패해야 한다 ("The SC must fail if a store to the reservation set from another hart can be observed to occur between the LR and SC"). QEMU 는 앞의 두 경우에 성공시켰다. 결론: **QEMU 로 LR/SC 의 ABA 면역이나 예약 입도를 검증하면 안 된다** (Spike 등 다른 모델이나 실리콘으로). 연습 4 가 ISS 에 같은 "QEMU 모드" 를 넣어 결과를 재현한다.

### ARM 과 비교 (see: ARM-M04, ARM-M11)

| | RISC-V LR/SC | ARM LDXR/STXR | RISC-V AMO | ARMv8.1 LSE |
|---|---|---|---|---|
| 명령 | `lr.w/d`, `sc.w/d` | `ldxr/stxr` (+`ldaxr/stlxr`, 쌍 `ldxp`) | `amoswap/add/and/or/xor/min/max/minu/maxu` | `swp`, `ldadd`, `ldclr`, `ldeor`, `ldset`, `ld{s,u}{max,min}`, `cas`, `casp` |
| 추적 단위 | 예약 집합 (구현 정의, ≥ 접근 크기, 플랫폼이 알려야) | Exclusives Reservation Granule (구현 정의, CTR_EL0.ERG 로 알림 — 문서 기준) | — | — |
| 성공 표시 | rd = 0 성공, ≠0 실패 (1 = unspecified) | Ws = 0 성공, 1 실패 | 항상 완료 | 항상 완료 |
| 순서 | LR `.aq`, SC `.rl` (+`.aqrl`) | `ldaxr` acquire, `stlxr` release | `.aq/.rl/.aqrl` | 접미사 `A/L/AL` (`ldaddal`, `casal`) |
| 예약 해제 | 내 SC (성공·실패 무관); 컨텍스트 스위치 땐 scratch 워드에 SC (스펙 노트) | `clrex`; 예외 진입/복귀 시 동작은 프로파일별 (ARM-M11) | — | — |
| 진행 보장 | 제약 루프면 livelock-free (≤16 명령, base I, …) | 비슷한 사용 제약 있음 (LDXR~STXR 사이 명시적 메모리 접근 등 — DDI 0487 "Load-Exclusive and Store-Exclusive instruction usage restrictions", 문서 기준) | 하드웨어가 완료 보장 | 하드웨어가 완료 보장 |
| CAS | LR/SC 로 (ABA 면역) / Zacas `amocas.w/d/q` (값 비교) | LL/SC 루프 | — | `cas` (값 비교 → ABA 가능) |
| 비트 지우기 | `amoand` 에 `~mask` | — | `amoand` | `ldclr` (mask 그대로) |
| 컴파일러 기본 (실측) | CAS 에 사용 | arm64 macOS 는 LSE 기본 (ARM-M04) | `fetch_add` 등에 사용 | `fetch_add` → `ldadd*` (C-A03) |

정리: **RISC-V LR/SC ≈ ARM LDXR/STXR, RISC-V AMO ≈ ARM LSE** (단 RISC-V 에는 LSE `cas` 같은 값 비교 CAS 가 기본 A 에 없고 Zacas 로 따로 들어왔다). Cortex-M3/M4 에는 LDREX/STREX 만 있고 LSE 같은 단일 명령 RMW 가 없다 — 그래서 단일 코어 RISC-V MCU 의 `amoadd.w` 는 ISR 이 끼어들 수 없는 진짜 "한 명령" 이라는 점이 장점이다 (C-M14, ARM-M11).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution        # 43/43, 5회 반복 모두 exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_asan && ./sol_asan   # 43/43, 보고 없음
cc -std=c11 -g -O1 -fsanitize=thread solution.c -o sol_tsan && ./sol_tsan         # 43/43 (스레드 없음 — 참고용)
```

실제 출력:

```text
spinlock   : 0 bad / 305 schedules
cas counter: 0 bad, CAS retries 45287, SC fails 17624 (over 300 seeds)
cas counter: 0 bad with 20% spurious SC failures (SC fails 56794)
ticket lock: 0 bad / 305 schedules
broken lock: 304 bad / 305 schedules (expected > 0)
sc no-retry: count 331 of 400
bad LR/SC loop, strict impl: step limit (livelock), count 0, SC fails 79997
43/43 passed
```

(Mac: `sysctl -n machdep.cpu.brand_string` = Apple M6, `cc --version` = Apple clang version 21.0.0, 실행 0.11 s user. 결과는 시드로 고정이라 기계와 무관하게 같다.) 이 레슨의 "동시성" 은 모두 ISS 안의 결정적 인터리빙이라 호스트 스레드가 없다 → TSan 은 할 일이 없다. `demo_qemu_rv64.c` 의 손으로 쓴 asm 원자 연산은 어차피 TSan 이 계측하지 못한다 (컴파일러가 만든 `__tsan_atomic*` 호출이 없음) — 그쪽 정확성은 정확한 최종 카운트로만 확인했다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `sc.w` 결과(rd) 를 안 본다 | 실패한 SC 는 아무것도 안 쓴다 → 증가 유실 (실측 400 중 69) | `sc.w t1,…; bnez t1, retry` |
| SC 실패 시 LR 없이 SC 만 다시 | SC 가 예약을 이미 끝냈다 → 영원히 실패 | 재시도는 **LR 부터** |
| LR/SC 사이에서 설정값 `lw`, 함수 호출(`jal`/`jalr`), `fence`, `ecall` | 제약 루프가 아님 → 어떤 구현에선 영원히 실패 (Part C livelock) | 필요한 값은 LR 전에 읽고, 사이엔 base I 산술만 |
| LR/SC 루프를 16 명령보다 길게 / 사이에서 뒤로 분기 | 진행 보장 밖 | 계산을 루프 밖으로, 루프는 짧게 |
| LR 과 SC 의 주소 레지스터를 사이에서 바꿈 / `lr.w` 후 `sc.d` | "같은 유효 주소, 같은 크기" 규칙 위반 | 주소 레지스터는 건드리지 않는다 |
| 락 워드와 자주 쓰는 데이터를 같은 캐시 라인에 | 라인 단위 예약이면 그 데이터 스토어가 SC 를 깬다 (연습 3: 0 → 1337) | 락/카운터를 라인 정렬·분리 |
| AMO 를 캐시 안 되는 MMIO/특정 메모리 영역에 그냥 씀 | 그 영역이 AMO/LR-SC 를 지원하는지는 PMA(물리 메모리 속성)에 달림 → 접근 오류 가능 | 플랫폼 문서의 PMA 확인 (privileged spec) |
| 스핀락 획득에 `.aq`, 해제에 `.rl` 를 빠뜨림 | RVWMO 에서 임계 구역 접근이 락 밖으로 샌다 (이 ISS 로는 안 보임) | `amoswap.w.aq` / `amoswap.w.rl` 또는 `fence` 쌍 |
| ISS 구현에서 rd 를 먼저 쓰고 rs2 를 읽음 | `amoadd.w t0,t0,(a1)` 이 틀린 값을 더함 | `src = x[rs2]` 를 맨 앞에서 |
| QEMU 에서 "ABA 안전" 을 검증 | QEMU SC = 값 비교 cmpxchg (소스·실측) | Spike / 실리콘 / 이 레슨의 ISS |
| 스트레스를 시드 하나로 | 깨진 락도 305 중 1 스케줄은 통과 | 시드 수백 개 + RR 여러 quantum |
| "A 확장 있음" 을 칩 이름만 보고 가정 | ESP32-C3 처럼 RV32IMC 인 칩 → libcall | `-march` 와 데이터시트의 ISA 문자열 확인 |

## 8. 면접 Q&A

<details>
<summary>Q1. LR/SC 로 CAS 를 짜 보세요. 값 비교 CAS 와 뭐가 다르죠?</summary>

`cas: lr.w t0,(a0); bne t0,a1,fail; sc.w t0,a2,(a0); bnez t0,cas; li a0,0; ret; fail: li a0,1; ret` (스펙 예제). 다른 점: 값 비교 CAS 는 "지금 값 == 기대값" 만 보지만 LR/SC 는 "LR 이후 **아무도** 그 예약 집합에 쓰지 않았는가" 를 본다 → A→B→A 로 돌아온 경우도 잡는다(ABA 면역). 스펙이 CAS 대신 LR/SC 를 고른 이유 1번이 이것이고, 2~4번은 CAS 는 소스 레지스터 3개가 필요한 새 포맷, ABA 회피용 DW-CAS 는 더 복잡, LR/SC 는 load 한 번으로 된다는 것. 꼬리: "그럼 RISC-V 에 CAS 명령은 없나요?" → Zacas 확장의 `amocas.w/d/q` (값 비교, 20240411 매뉴얼에 포함). 꼬리 2: "QEMU 에서 ABA 테스트하면?" → QEMU 는 SC 를 값 비교 cmpxchg 로 구현해서 ABA 를 성공시킨다 (실측).

</details>

<details>
<summary>Q2. SC 는 언제 실패하나요? 다 대 보세요.</summary>

반드시 실패: (1) 유효한 예약이 없을 때 (LR 안 함, 이미 SC 를 실행함), (2) 주소가 가장 최근 LR 의 예약 집합 밖일 때, (3) LR 이후 다른 hart 의 store/AMO/성공 SC 가 예약 집합에 관찰될 때 (같은 값이어도), (4) 다른 장치(DMA 등)가 LR 이 접근한 바이트에 쓸 때, (5) LR 과 SC 사이에 다른 SC 가 있을 때. 실패할 수도 있음: 구현 사정에 의한 가짜 실패(캐시 축출, 인터럽트, 트랩 …) — 제약 루프의 진행 보장만 지키면 허용. 그리고 제약 안 된 시퀀스는 "항상 실패" 도 허용. 실패 시 rd ≠ 0 (현재 1 = unspecified), 메모리는 안 씀. 꼬리: "다른 hart 가 그 주소를 **읽기만** 하면?" → 실패 원인이 아니다 ("Loads and load-reserved instructions do not by themselves impede the progress of other harts' LR/SC sequences").

</details>

<details>
<summary>Q3. constrained LR/SC loop 규칙을 말해 보세요. 왜 그런 규칙이 있죠?</summary>

루프 전체(LR/SC + 재시도 코드) 16 명령 이내, 메모리에 연속 배치. LR~SC 사이 동적으로 실행되는 명령은 base I(와 그 압축형)만, 단 load·store·뒤 방향 점프·택한 뒤 방향 분기·JALR·FENCE·SYSTEM 제외. 재시도 코드는 뒤 방향 분기 허용, 나머지는 같은 제약. SC 는 최근 LR 과 같은 주소·같은 크기. 메모리 영역이 LR/SC eventuality 속성을 가져야 함. 이유 (스펙 노트): 64 B 안에 넣어 I-cache·TLB 연관도 요구를 줄이고, 루프 안 load/store 를 막아 "예약을 private 캐시 라인으로 추적하는" 단순 구현에서 그 라인이 축출되지 않게, 분기 제한으로 시퀀스 시간을 짧게, FP·곱셈 금지로 OS 에뮬레이션을 단순하게. 대가로 구현은 livelock-free 진행을 보장해야 한다. 꼬리: "MUL 넣으면?" → M 은 base I 가 아님 → 위반 (검사기 `LRSC_BAD_INSN`).

</details>

<details>
<summary>Q4. LR/SC 사이에 인터럽트나 컨텍스트 스위치가 나면?</summary>

트랩은 진행 보장 목록의 한 항목("H traps")이라 SC 가 실패해도 된다 (구현이 트랩 때 예약을 지우는 게 보통 — 구현 정의). 문제는 OS 가 다른 태스크로 바꿨는데 예약이 남아 있는 경우: 태스크 B 의 SC 가 태스크 A 의 LR 예약으로 성공해 버릴 수 있다. 그래서 스펙 노트: "A store-conditional instruction to a scratch word of memory should be used to forcibly invalidate any existing load reservation during a preemptive context switch". Linux `arch/riscv/kernel/entry.S` (master, 확인함) 의 예외 복귀 경로가 정확히 이것이다: `REG_L a2, PT_EPC(sp)` → `REG_SC x0, a2, PT_EPC(sp)` (같은 값을 다시 쓰는 SC — 성공해도 무해). 주석이 이유를 둘 든다: LR/SC 도중의 트랩, 그리고 "a taken forward branch around an SC -- which is how we implement CAS" — 값이 달라 `bne` 로 빠진 CAS 도 예약을 남긴다 (연습 2 의 `done:` 경로). ARM 의 `clrex` 와 같은 역할이고, Cortex-M 은 예외 진입/복귀에서 로컬 모니터가 지워진다 (ARM-M11). 펌웨어 RTOS 포팅 때 컨텍스트 스위치 코드에 이 한 줄이 있는지 확인하라.

</details>

<details>
<summary>Q5. AMO 가 있는데 LR/SC 는 왜 필요하고, 반대로 LR/SC 가 있는데 AMO 는 왜 필요하죠?</summary>

AMO 는 정해진 9가지 연산(swap, add, and, or, xor, min/max signed/unsigned)만 한다 — 조건부 갱신(CAS, "값이 X 면 Y 로"), 포화 증가, 두 필드 동시 갱신은 LR/SC 루프가 필요하다. 반대로 AMO 는 항상 완료되고(재시도 없음), 메모리 컨트롤러/인터커넥트에서 실행할 수 있어(far atomic) 많은 코어가 한 카운터를 두드릴 때 확장성이 좋고, `rd = x0` 이면 옛 값 가져오기도 생략 가능하다. 그리고 캐시 없는 MCU 는 LR/SC 예약 추적이 어렵다 — 그래서 Zaamo 만 따로 떼어 MCU 가 AMO 만 구현할 수 있게 했다 (스펙 노트). 다만 clang 21 은 Zaamo 만 있으면 C11 atomics 를 전부 libcall 로 냈다(실측) — AMO 를 쓰려면 asm/intrinsic 이 필요할 수 있다.

</details>

<details>
<summary>Q6. 스핀락에 .aq / .rl 은 왜 붙이죠? fence 로 대신할 수 있나요?</summary>

획득 AMO 의 `.aq` = "이 hart 의 이후 메모리 연산이 이 AMO 보다 먼저 보이지 않는다" → 임계 구역 접근이 락 획득 전으로 새지 않음. 해제 AMO 의 `.rl` = "이전 메모리 연산이 이 AMO 보다 먼저 보인다" → 임계 구역 쓰기가 해제 후로 밀리지 않음 (§14.1 원문 요지). `fence r,rw` 는 acquire, `fence rw,w` 는 release 로 대신 쓸 수 있지만 스펙 노트 말대로 "불필요한 순서까지 강제" 한다 (fence 는 앞뒤 **모든** 접근에 걸림). 그래서 clang 도 `exchange(acquire)` 는 `amoswap.w.aq`, `store(release)` 는 `fence rw,w; sw` 로 냈다(실측) — 일반 store 에는 rl 비트가 없으니까 (load-acquire/store-release 명령은 Zalasr 확장 — 현재 main 브랜치 스펙에 있음, 비준 상태 미확인). 꼬리: "SC 에 `.aq` 를 붙이면?" → 스펙: rl 없이 aq 만 단 SC, aq 없이 rl 만 단 LR 은 더 강한 순서를 보장하지 않을 수 있다 — 쓰지 말 것.

</details>

<details>
<summary>Q7. ARM 의 LDXR/STXR, LSE 와 비교해 보세요.</summary>

LR/SC ↔ LDXR/STXR 는 거의 1:1: 둘 다 "예약(독점 모니터) 걸고 읽기 → 조건부 쓰기, 상태 0 = 성공", 예약 단위는 구현 정의 (RISC-V 예약 집합 / ARM ERG), 순서는 RISC-V 는 `.aq/.rl` 비트, ARM 은 `ldaxr/stlxr` 별도 명령. 해제는 RISC-V 에 `clrex` 가 없어 scratch SC 로. AMO ↔ LSE: 단일 명령 RMW, 항상 완료. 차이: LSE 에는 값 비교 `cas`/`casp` 와 `ldclr`(비트 지우기)이 있고, RISC-V 기본 A 에는 CAS 가 없으며(Zacas 로 추가) 비트 지우기는 `amoand ~mask`. 컴파일러: arm64 macOS 는 LSE 를 기본으로 써서 `fetch_add` 가 `ldadd*` 한 명령(ARM-M04, C-A03), RISC-V rv32ia 는 `amoadd.w(.aqrl)` 한 명령, CAS 는 arm64 가 `cas*` 한 명령, RISC-V rv32ia 는 `lr.w.aqrl/sc.w.rl` 루프 (demo_c11_map.c 실측). Cortex-M3/M4 는 LDREX/STREX 만 (ARM-M11).

</details>

<details>
<summary>Q8. 예약 집합이 64B 라면 무엇을 조심해야 하죠? 4B 가 더 좋은 거 아닌가요?</summary>

같은 라인의 이웃 변수에 대한 스토어가 SC 를 깬다 — 예약판 false sharing. 연습 3 실측: 이웃 워드를 계속 쓰는 hart 가 있으면 64B 입도에서 SC 실패 1337번(100 시드), 4B 입도에서 0번. 결과(카운트)는 둘 다 정확하지만 재시도가 성능을 먹고, 최악엔 이웃이 쉬지 않고 쓰면 진행 보장도 약해진다 ("if other harts or devices continue to write to that reservation set, it is not guaranteed that any hart will exit"). 4B 가 정확하지만 하드웨어 비용이 크다: 캐시 일관성 프로토콜이 이미 라인 단위로 "누가 썼나" 를 알려 주므로 라인 단위 예약이 거의 공짜다. 그래서 대처는 소프트웨어 쪽: 락·원자 카운터를 캐시 라인 정렬·패딩 (C의 `_Alignas(64)`, ARCH-M08 false sharing).

</details>

## 9. 추가 연습

`practice.c` 는 solution.c 의 ISS 를 복사하고 `exec_amo` 를 AMO 9종 + "QEMU 식 SC" 옵션으로 넓힌 것이다. 새 인코딩 4개(`amoor.w.aqrl a0,a1,(a2)` = `0x46b6252f`, `amoor.w t1,t0,(s2)` = `0x4059232f`, `amoand.w.rl t1,t0,(s2)` = `0x6259232f`, `amomaxu.w a0,a1,(a2)` = `0xe0b6252f`)도 VM clang 출력과 CHECK 한다.

### 연습 1 — 이벤트 플래그 (FreeRTOS EventGroup 의 하드웨어판)

hart h 가 비트 16h..16h+15 를 하나씩 세우고, 마지막에 자기 절반의 짝수 비트를 지운다. 최종값은 항상 `0xAAAAAAAA` 여야 한다. AMO 판과 `lw/or/sw` 판을 시드 100개로 비교하라.

<details>
<summary>답 보기</summary>

세우기 `amoor.w zero, t0, (a1)` (옛 값 필요 없음 → rd = x0), 지우기 `amoand.w.rl zero, ~mask, (a1)` — RISC-V 에는 LSE `ldclr` 같은 "비트 지우기" 가 없어서 `xori t0,t0,-1` 로 마스크를 뒤집어 AND 한다. `0x5555` 는 `lui t0,0x5; addi t0,t0,0x555` 로 만든다 (addi 즉치값은 12비트 부호 있음). 실측: `ex1 flags: amo 0/100 wrong, plain lw/or/sw 100/100 wrong` — 일반 RMW 는 100개 시드 전부에서 비트를 잃었다 (상대가 읽은 뒤 쓴 값을 덮음). 펌웨어 의미: 단일 코어 MCU 라도 ISR 과 main 이 같은 플래그 워드를 `lw/or/sw` 로 고치면 같은 문제다 (C-M14). A 가 있는 칩이면 `amoor` 한 명령으로 인터럽트를 끌 필요가 없다.

</details>

### 연습 2 — 원자 최대값: LR/SC 루프 vs amomaxu.w

두 hart 가 xorshift32 값(시드 hartid+1) 100개씩을 공유 "최대값" 에 제출한다. LR/SC 판과 `amomaxu.w` 판을 둘 다 짜고 같은 결과인지 보라. LR/SC 판이 제약 루프인지도 따져 보라.

<details>
<summary>답 보기</summary>

```text
retry: lr.w  t0, (a1)
       bgeu  t0, s5, done     # 이미 크거나 같으면 끝 (앞 방향 → 루프 탈출, 허용)
       sc.w  t1, s5, (a1)
       bnez  t1, retry
done:
```

4 명령 제약 루프다. `done` 으로 빠지면 예약이 남아 있지만 다음 LR 이 대체하므로 문제없다 (단, 그 사이 다른 코드의 SC 가 이 예약을 "먹을" 일이 없게 SC 는 항상 LR 과 짝지어 쓴다). `amomaxu.w zero, s5, (a1)` 는 한 명령. 실측: `ex2 max (LR/SC loop): 0 bad, 18 instructions in program` / `ex2 max (amomaxu.w): 0 bad, 15 instructions in program`. C11 에는 `atomic_fetch_max` 가 없어서 컴파일러는 CAS 루프(`lr.w / bne / sc.w / bnez`)를 냈다 (demo_c11_map.c 의 `fmax_relaxed`). 워터마크(최대 큐 깊이, 최악 지연)는 펌웨어 통계의 단골이다 (C-A03 의 `stat_max`).

</details>

### 연습 3 — 예약 집합 입도: 이웃 워드가 SC 를 깨는가

hart0 은 X 를 LR/SC 로 100번 증가, hart1 은 같은 64B 블록의 Y = X+4 에 100번 `sw`. `res_shift` = 2 와 6 에서 hart0 의 SC 실패 수를 세라. 결과값은?

<details>
<summary>답 보기</summary>

실측: `ex3 neighbor stores: SC fails word-granule 0, 64B-granule 1337 (100 seeds)`. 두 경우 모두 X == 100, Y == 1 로 정확하다 — 정확성은 그대로, 비용(재시도)만 다르다. 실제 칩에서 이 수는 캐시 라인 크기와 구현에 달렸고, 플랫폼이 "예약 집합 크기와 모양" 을 알려 줘야 한다는 게 스펙 노트다. 대책: 원자 변수는 라인 단위로 떼어 놓기 (이 레슨 ISS 의 데이터 배치처럼 0x40 간격).

</details>

### 연습 4 — ABA: 스펙 SC vs QEMU 식 SC

x = 5. hart0 `lr.w`, hart1 `sw 7` 후 `sw 5`, hart0 `sc.w 99`. ISS 를 스펙 모드와 "QEMU 모드"(LR 주소가 같고 현재 값 == LR 때 값이면 성공, 남의 스토어로 예약이 안 지워짐)로 각각 돌려라.

<details>
<summary>답 보기</summary>

실측: `ex4 ABA, spec SC: rd=1 x=5` / `ex4 ABA, QEMU-style SC: rd=0 x=99`. QEMU 모드 구현은 LR 때 `res_addr`, `res_val` 을 저장하고, SC 에서 `res_own && res_addr == a && ld32(a) == res_val` 로 판정한다 (`res_own` 은 자기 SC 만 지움 — QEMU 의 `load_res = -1`). 이것이 `demo_qemu_rv64.c` 의 실제 QEMU 결과(같은 값 → 성공, A-B-A → 성공, 값 바뀜 → 실패)와 정확히 같다. 교훈: 에뮬레이터는 "기능적으로 맞는" 프로그램을 돌리는 데는 충분하지만, 동기화 프리미티브의 **실패 경로** 검증에는 부적합할 수 있다. ABA 가 실제 문제가 되는 곳은 lock-free 스택 pop (DS-A04).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice        # 13/13 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_asan && ./pr_asan   # 13/13, 보고 없음
```

실제 출력:

```text
ex1 flags: amo 0/100 wrong, plain lw/or/sw 100/100 wrong
ex2 max (LR/SC loop): 0 bad, 18 instructions in program
ex2 max (amomaxu.w): 0 bad, 15 instructions in program
ex3 neighbor stores: SC fails word-granule 0, 64B-granule 1337 (100 seeds)
ex4 ABA, spec SC: rd=1 x=5
ex4 ABA, QEMU-style SC: rd=0 x=99
13/13 passed
```

## 10. 핵심 정리

- A = **Zalrsc**(LR/SC) + **Zaamo**(AMO 9종). AMO 포맷 = `funct5 | aq | rl | rs2 | rs1 | 010 | rd | 0101111`. 내 인코더 259 워드 = clang 21 출력.
- LR 은 **예약 집합**(구현 정의, ≥ 접근 바이트, 보통 캐시 라인)을 건다. 다른 hart 의 store·AMO·성공 SC 가 닿으면 깨진다 — **같은 값이어도** (ABA 면역). load·LR·실패 SC 는 안 깬다.
- SC: 성공 rd = 0, 실패 rd ≠ 0 & 메모리 안 씀. 실행하면 성공·실패 무관하게 내 예약 종료 → 재시도는 LR 부터. 가짜 실패 허용 → 항상 루프.
- **제약 루프**: ≤ 16 명령, LR~SC 사이 base I 산술·앞 방향 분기만 (load/store/뒤 방향/JALR/FENCE/SYSTEM 금지), 같은 주소·크기. 지키면 livelock-free, 어기면 "어떤 구현에선 영원히 실패".
- 스핀락 = `lw` 대기 + `amoswap.w.aq` / `amoswap.w.rl`. ticket lock = `amoadd.w` 번호표 + `fence r,rw` / `fence rw,w`. CAS = `lr.w / bne / sc.w / bnez` 4 명령.
- C11 → rv32ia: `fetch_add` → `amoadd.w(.aqrl)`, CAS → `lr.w.aqrl/sc.w.rl` 루프, acquire load → `lw; fence r,rw`, release store → `fence rw,w; sw`. A 없음(그리고 Zaamo 만) → `__atomic_*_4` libcall.
- QEMU 의 SC 는 값 비교 cmpxchg → ABA·예약 입도 검증엔 못 쓴다. LR/SC ≈ LDXR/STXR, AMO ≈ LSE, 컨텍스트 스위치의 scratch SC ≈ `clrex`.

## 참고자료

- RISC-V Unprivileged ISA, 20240411 (ratified) — 14장 "A" Extension for Atomic Instructions v2.1: §14.1 Specifying Ordering of Atomic Instructions, §14.2 Zalrsc, §14.3 Eventual Success of Store-Conditional Instructions, §14.4 Zaamo (장 번호는 소스 include 순서로 셈). 소스: <https://github.com/riscv/riscv-isa-manual/blob/20240411/src/a-st-ext.adoc>
- 같은 내용 20191213 판 — 8장 "A" Standard Extension v2.1, §8.3: <https://github.com/riscv/riscv-isa-manual/blob/Ratified-IMAFDQC/src/a.tex> (현재 main 은 za/zalrsc/zaamo.adoc 로 재구성 중: <https://github.com/riscv/riscv-isa-manual/tree/main/src/unpriv>)
- RVWMO 와 C/C++ 매핑 (Unprivileged ISA, RVWMO 장 + 부록 A 해설) — RISCV-A04 에서 자세히
- RISC-V Privileged ISA — mcause 예외 코드(4 load address misaligned, 6 store/AMO address misaligned), PMA 의 AMO/LR-SC 지원 속성: <https://github.com/riscv/riscv-isa-manual>
- QEMU `target/riscv/insn_trans/trans_rva.c.inc` (v10.2.0) — `gen_lr` / `gen_sc`: <https://github.com/qemu/qemu/blob/v10.2.0/target/riscv/insn_trans/trans_rva.c.inc>
- Linux `arch/riscv/kernel/entry.S` — 예외 복귀 시 scratch SC 로 예약 지우기 (주석 포함): <https://github.com/torvalds/linux/blob/master/arch/riscv/kernel/entry.S>
- Spike (riscv-isa-sim) — 참조 ISS: <https://github.com/riscv-software-src/riscv-isa-sim>
- Arm Architecture Reference Manual for A-profile (DDI 0487) — Load-Exclusive/Store-Exclusive, Exclusives Reservation Granule(CTR_EL0.ERG), FEAT_LSE 원자 명령: <https://developer.arm.com/documentation/ddi0487/latest/>
- Patterson & Waterman, *The RISC-V Reader* — 10장 RV32A
- 관련 레슨: C-A03 (C11 atomics, arm64/M4 asm), C-A02 (컴파일러 vs CPU 재배치), C-M14 (ISR 공유, PRIMASK), CPP-A04 (lock-free 여부·libatomic), OS-M07 (TAS/TTAS/ticket 성능), ARM-M04 (LDAXR/STLXR·LSE, see), ARM-M05 (배리어 실측), ARM-M11 (Cortex-M LDREX/STREX·모니터 클리어), RISCV-A04 (RVWMO), DS-A04 (ABA), ARCH-M08 (false sharing)
