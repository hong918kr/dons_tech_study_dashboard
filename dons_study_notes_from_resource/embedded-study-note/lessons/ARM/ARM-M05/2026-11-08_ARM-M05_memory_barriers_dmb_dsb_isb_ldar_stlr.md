---
id: ARM-M05
time: 95분
updated: 2026-11-08
---
# ARM-M05 메모리 배리어 명령: DMB, DSB, ISB, LDAR/STLR

## 학습 목표

- `DMB`(순서) · `DSB`(완료 대기) · `ISB`(명령 재인출) 와 `LDAR`/`STLR`/`LDAPR`(한 방향 배리어가 붙은 load/store) 가 **각각 무엇을 막는지** 를 명령 단위로 말하고, `ish`/`ishst`/`ishld`/`osh`/`sy` 같은 옵션까지 고를 수 있다.
- Message Passing(MP) · Store Buffering(SB) 리트머스를 **GNU inline asm 으로 직접** 짜서 Apple M6 에서 돌리고, 버전별 "재배치 관찰 횟수" 를 읽고 해석할 수 있다 (관찰 = 허용의 증거, 0 = 보장 아님).
- ARMv8 의 순서 규칙(배리어, acquire/release, RCsc vs RCpc, 주소 의존성, 제어 의존성+ISB)을 **판정기 코드** 로 옮기고, 그 판정기를 내가 쓴 함수의 **실제 기계어** 에 적용한다.
- DSB/ISB 가 꼭 필요한 상황(시스템 레지스터 변경, TLB/페이지 테이블, 코드 수정·JIT, WFI 전, Cortex-M 의 MPU·CONTROL·CPACR)을 표로 정리하고, macOS JIT(`MAP_JIT` + `sys_icache_invalidate`)로 "I-cache 동기화를 빼면 옛 명령이 돈다" 를 직접 본다.

## 1. 문제

Cortex-A + Cortex-M 이 붙은 SoC(i.MX8M, STM32MP1)의 메일박스, Linux arm64 드라이버, 부트로더의 MMU/MPU 설정, 실리콘 검증 코드는 모두 배리어 **명령** 을 손으로 고르는 자리다. 면접은 "DMB, DSB, ISB 차이는?" 다음에 곧바로 **"STLR/LDAR 로 DMB 를 대신할 수 있나? LDAPR 은 뭐가 다른가? 실제 칩에서 재배치가 보이나?"** 를 묻는다. 이 레슨은 그 질문에 **실측 숫자와 기계어** 로 답하게 만든다.

층 나누기: C 코드에서 어떤 fence 를 쓸지는 C-A02, C11 atomics 가 어떤 명령이 되는지는 C-A03 이 다뤘다. 여기서는 한 층 아래 — **명령을 직접 쓰고, 그 명령이 만드는 순서를 아키텍처 규칙으로 판정하고, 하드웨어에서 센다.**

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">두 리트머스: 무엇을 막아야 "이상한 결과" 가 사라지나</text>
  <rect x="10" y="34" width="350" height="190" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor" font-weight="bold">MP (message passing)</text>
  <text x="20" y="76" fill="currentColor">writer (코어 0)</text>
  <text x="190" y="76" fill="currentColor">reader (코어 1)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="20" y="98">str data, i</text>
    <text x="20" y="140">str flag, i</text>
    <text x="190" y="98">ldr f, flag</text>
    <text x="190" y="140">ldr d, data</text>
  </g>
  <rect x="20" y="106" width="140" height="22" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="26" y="121" fill="currentColor" font-size="12">① st→st 순서</text>
  <rect x="190" y="106" width="150" height="22" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="196" y="121" fill="currentColor" font-size="12">② ld→ld 순서</text>
  <text x="20" y="168" fill="currentColor" font-size="12">이상한 결과: d &lt; f (flag 는 새것, data 는 옛것)</text>
  <text x="20" y="188" fill="currentColor" font-size="12">①: dmb ish / dmb ishst / stlr flag</text>
  <text x="20" y="208" fill="currentColor" font-size="12">②: dmb ish/ishld, ldar·ldapr, 주소 의존</text>
  <rect x="380" y="34" width="350" height="190" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="390" y="54" fill="currentColor" font-weight="bold">SB (store buffering)</text>
  <text x="390" y="76" fill="currentColor">A (코어 0)</text>
  <text x="560" y="76" fill="currentColor">B (코어 1)</text>
  <g font-family="monospace" font-size="12" fill="currentColor">
    <text x="390" y="98">str x, i</text>
    <text x="390" y="140">ldr r1, y</text>
    <text x="560" y="98">str y, i</text>
    <text x="560" y="140">ldr r2, x</text>
  </g>
  <rect x="390" y="106" width="150" height="22" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="396" y="121" fill="currentColor" font-size="12">③ st→ld 순서</text>
  <rect x="560" y="106" width="150" height="22" rx="3" class="f-warn" stroke="currentColor"/>
  <text x="566" y="121" fill="currentColor" font-size="12">③ st→ld 순서</text>
  <text x="390" y="168" fill="currentColor" font-size="12">이상한 결과: r1 &lt; i 이고 r2 &lt; i (둘 다 옛값)</text>
  <text x="390" y="188" fill="currentColor" font-size="12">③: dmb ish(전부) / stlr 다음 ldar (RCsc)</text>
  <text x="390" y="208" fill="currentColor" font-size="12">안 됨: dmb ishst, dmb ishld, stlr 다음 ldapr</text>
  <text x="10" y="252" fill="currentColor">MP 는 양쪽(①과 ②)이 모두 막아야 금지된다. 한쪽만 막으면 여전히 관찰될 수 있다.</text>
  <text x="10" y="274" fill="currentColor">SB 는 store 버퍼 때문에 생긴다 — release/acquire 짝은 이것을 막지 않는다 (ldapr). ldar 는 막는다.</text>
</svg>
```

### Part A — 리트머스 커널을 inline asm 으로

`starter.c` 에 배리어 없는 예시(`w_plain`, `r_plain`, `s_plain`)가 있다. 같은 모양으로 9개를 채운다. 모두 `NOINLINE` 함수라서 기계어가 그대로 남는다.

| 함수 | 명령열 |
|---|---|
| `w_dmb(data, flag, v)` | `str v,[data]` · `dmb ish` · `str v,[flag]` |
| `w_stlr` | `str v,[data]` · `stlr v,[flag]` |
| `r_dmb(flag, data)` → `(f<<32)\|d` | `ldr f,[flag]` · `dmb ish` · `ldr d,[data]` |
| `r_ldar` / `r_ldapr` | `ldar f,[flag]` / `ldapr f,[flag]` · `ldr d,[data]` |
| `r_addr` | `ldr f,[flag]` · `eor t,f,f` · `ldr d,[data, t]` (배리어 없이 주소 의존성) |
| `s_dmb(mine, other, v)` | `str v,[mine]` · `dmb ish` · `ldr r,[other]` |
| `s_rcsc` / `s_rcpc` | `stlr v,[mine]` · `ldar r,[other]` / `ldapr r,[other]` |

테스트는 값(단일 스레드), 그리고 **함수의 기계어를 읽어** "첫 메모리 접근부터 둘째까지" 가 정확히 `STR DMB.ISH STR` 같은 모양인지 본다. 그 다음 "금지" 조합(dmb/dmb, stlr/ldapr, stlr/addr)을 두 스레드로 200만 번씩 돌려 **이상한 결과 0** 을 확인한다. 실제 관찰 횟수 비교는 `demo_litmus_asm.c` (13개 MP 조합 + 5개 SB 조합)가 한다.

### Part B — ARMv8 순서 판정기

주어진 디코더가 32비트 명령어를 `{LDR, STR, LDAR, LDAPR, STLR, DMB(CRm), DSB(CRm), ISB, ALU, CBZ, RET}` 로 바꾼다. 그 목록을 받아 세 함수를 쓴다:

- `bool st_st_ordered(ops, n)` — 첫 두 접근이 store, store 일 때 다른 코어에게 순서가 보장되나
- `bool ld_ld_ordered(ops, n)` — load, load (acquire / LD 배리어 / 주소 의존 / 제어 의존+ISB)
- `bool st_ld_ordered(ops, n)` — store, load (전부 배리어 / `stlr`→`ldar`)

주어진 `mp_forbidden = st_st && ld_ld`, `sb_forbidden = st_ld(A) && st_ld(B)` 로 27개 손 고른 명령열 + **Part A 커널의 실제 기계어** 12조합을 판정한다.

### Part C — 펌웨어: 코어 간 메일박스, 그리고 JIT

1. **메일박스**: 앱 코어 → 실시간 코어 1칸 메일박스 (`seq`, `ack`, payload 4워드). `mbox_send` 는 `ack == seq-1` 을 acquire 로 기다리고, payload 를 쓰고, `seq` 를 `stlr` 로 게시한다. `mbox_poll` 은 `seq` 를 `ldapr` 로 보고 payload 를 복사한 뒤 `ack` 를 `stlr` 로 돌려준다. 10만 메시지 스트레스에서 손상 0.
2. **JIT**: `MAP_JIT` 페이지에 기계어를 넣는 `jit_load` — W^X 스위치 → 복사 → 되돌림 → `sys_icache_invalidate`. 같은 주소에 `mov w0,#42`, `add`, `sub` 를 차례로 넣고 실행, 2000번 "바꾸고 바로 실행" 이 모두 새 명령이어야 한다. 그리고 DSB/ISB 가 필요한 상황 표를 만든다 (연습 3).

## 2. Problem (English)

> **Barrier instructions on AArch64: write them, decode them, measure them.**
>
> A. Implement, with GNU extended inline asm, message-passing writers `void w_dmb/w_stlr(uint32_t *data, uint32_t *flag, uint32_t v)` (store `v` to `data`, then to `flag`), readers `uint64_t r_dmb/r_ldar/r_ldapr/r_addr(const uint32_t *flag, const uint32_t *data)` returning `(flag << 32) | data`, and store-buffering kernels `uint32_t s_dmb/s_rcsc/s_rcpc(uint32_t *mine, const uint32_t *other, uint32_t v)`. `r_addr` must order its two loads with an artificial address dependency (`eor t, f, f`) instead of a barrier. Each function's machine code is read back at run time and must match the expected instruction shape.
>
> B. Given a decoder for the relevant A64 encodings, implement `st_st_ordered`, `ld_ld_ordered` and `st_ld_ordered`, deciding whether the first two memory accesses of an instruction sequence are ordered as observed by another core under the Armv8 memory model (DMB/DSB with type and shareability, Store-Release, Load-Acquire (RCsc) vs Load-AcquirePC (RCpc), address dependency, control dependency + ISB). Apply it to your own kernels.
>
> C. (1) Implement a one-slot inter-core mailbox `mbox_send`/`mbox_poll` with `stlr`/`ldapr` on `seq`/`ack`; 100k messages must arrive intact. (2) Implement `int jit_load(jit_t *j, const uint32_t *code, size_t n)` on a `MAP_JIT` page so the next call executes the new code.
>
> Follow-ups: DMB vs DSB vs ISB? Is `stlr`+`ldar` a full barrier? Why does clang use `ldapr` for `memory_order_acquire`? You ran MP 20M times and saw 0 — is the code correct? What is the exact sequence for self-modifying code, and what does `sys_icache_invalidate` do on this Mac? Which barriers does a Cortex-M need after enabling the MPU?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread starter.c -o starter && ./starter
# 처음엔 "27/77 passed" (exit 1). 스트레스 2묶음은 단위 테스트가 실패하면 SKIP — 멈추지 않는다. TODO 14개를 채워 77/77
```

asm 은 `-std=c11` 에서 `asm` 키워드가 없으므로 `__asm__ __volatile__` 철자를 쓴다 (C-A02 §3 참고). 관찰용 파일 (하네스와 별개):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_litmus_asm.c -o demo_litmus_asm
./demo_litmus_asm 20 all          # 반복 2000만 회 × (MP 13조합 + SB 5조합), 이 기계에서 약 5초
./demo_litmus_asm 20 sb           # SB 만
cc -std=c11 -Wall -Wextra -Werror -O2 demo_jit.c -o demo_jit && ./demo_jit
clang -std=c11 --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm_barrier.c -o -
xcrun llvm-objdump -d --no-show-raw-insn solution_binary | less      # 내 커널의 실제 기계어
```

실행 환경 (실측): `sysctl -n machdep.cpu.brand_string hw.perflevel0.physicalcpu hw.perflevel1.physicalcpu` → `Apple M6`, `2`, `4` — 단, 이 칩은 성능 단계가 3개다 (`hw.nperflevels` = 3: perflevel0 Super 2 + perflevel1 Performance 4 + perflevel2 Efficiency 6 = 12 코어). 위 명령은 앞의 두 단계만 보여 준다. `cc --version` → `Apple clang version 21.0.0 (clang-2100.0.123.102)`. `sysctl hw.optional.arm.FEAT_LRCPC` → `1` (ldapr 있음), `FEAT_LRCPC2` → `1`. 과제 문장은 "M2" 지만 이 기계는 M6 이다.

## 4. 힌트

<details><summary>힌트 1 — inline asm 피연산자</summary>

예시 `w_plain` 을 그대로 복사하고 가운데 줄만 바꾼다. `%w[v]` 는 32비트 W 레지스터 이름, `[%[d]]` 는 주소(64비트 X). 출력은 `"=&r"`(early clobber — 입력 레지스터와 겹치지 않게)이고, 순서가 중요하므로 `"memory"` 클로버를 꼭 둔다. 조건 플래그를 안 바꾸니 `"cc"` 는 필요 없다.
</details>

<details><summary>힌트 2 — 주소 의존성 만들기</summary>

`eor %x[t], %x[rf], %x[rf]` 는 항상 0 이지만 **CPU 입장에서는 f 의 값을 기다려야 계산되는 값** 이다. 그 t 를 둘째 load 의 주소에 넣는다: `ldr %w[rd], [%[d], %x[t]]`. t 를 위한 출력 피연산자(`[t] "=&r"(t)`)가 하나 더 필요하다.
</details>

<details><summary>힌트 3 — DMB 옵션 비트</summary>

디코더의 `opt` 는 CRm 4비트다. 아래 2비트 = 종류 (01 LD, 10 ST, 11 전부), 위 2비트 = 도메인 (00 OSH, 01 NSH, 10 ISH, 11 SY). 주어진 `bar_covers(o, need)` 가 "NSH 가 아니고, 종류가 전부이거나 need 와 같다" 를 판정한다. st→st 는 `NEED_ST`, ld→ld 는 `NEED_LD`, st→ld 는 `NEED_ALL`.
</details>

<details><summary>힌트 4 — ld_ld_ordered 의 의존성 추적</summary>

"첫 load 의 목적 레지스터" 에서 시작하는 비트마스크(taint)를 두고, 사이의 ALU 명령이 오염된 레지스터를 읽으면 목적 레지스터도 오염시킨다 (아니면 지운다). 둘째 load 의 `rn`(베이스) 또는 `rm`(오프셋)이 오염됐으면 주소 의존. `CBZ` 가 오염된 레지스터를 보면 제어 의존이고, 그 뒤에 `ISB` 가 오면 순서가 생긴다.
</details>

<details><summary>힌트 5 — 메일박스의 ack 는 왜 acquire 인가 / JIT 순서</summary>

보내는 쪽이 `ack` 를 보고 payload 를 **덮어쓴다**. 받는 쪽의 payload 읽기가 그 덮어쓰기보다 먼저 끝나야 한다 → 받는 쪽 `ack` 는 release, 보내는 쪽 `ack` 읽기는 acquire. JIT: `pthread_jit_write_protect_np(0)` → `memcpy` → `pthread_jit_write_protect_np(1)` → `sys_icache_invalidate(page, n*4)`.
</details>

## 5. 풀이

### 0. 배리어 지도 — 무엇을 기다리고, 무엇을 막나

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">세 배리어가 "기다리는 것" 이 다르다</text>
  <rect x="10" y="36" width="720" height="66" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="56" fill="currentColor" font-weight="bold">DMB — 순서 (Data Memory Barrier)</text>
  <text x="20" y="76" fill="currentColor" font-size="12">앞의 메모리 접근이 뒤의 메모리 접근보다 "먼저 관찰" 되게. 뒤의 비메모리 명령(add, 분기)은 계속 실행된다.</text>
  <text x="20" y="94" fill="currentColor" font-size="12">옵션: 종류 LD / ST / 전부 × 도메인 NSH / ISH / OSH / SY.  예: 디스크립터 → OWN, data → flag</text>
  <rect x="10" y="112" width="720" height="66" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="132" fill="currentColor" font-weight="bold">DSB — 완료 (Data Synchronization Barrier)</text>
  <text x="20" y="152" fill="currentColor" font-size="12">앞의 메모리 접근 + 캐시/TLB/분기예측 유지보수가 "끝날 때까지" 뒤의 어떤 명령도 실행하지 않는다.</text>
  <text x="20" y="170" fill="currentColor" font-size="12">예: TLBI 뒤, dc/ic 뒤, WFI 앞, MPU/주변장치 설정이 실제로 반영됐음을 보장할 때</text>
  <rect x="10" y="188" width="720" height="66" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="208" fill="currentColor" font-weight="bold">ISB — 재인출 (Instruction Synchronization Barrier, context synchronization)</text>
  <text x="20" y="228" fill="currentColor" font-size="12">파이프라인을 비우고 뒤의 명령을 "새 설정으로" 다시 가져온다. 메모리 순서 배리어가 아니다.</text>
  <text x="20" y="246" fill="currentColor" font-size="12">예: 시스템 레지스터(SCTLR, TTBR, CPACR, CONTROL) 쓴 뒤, 새 코드 실행 전 (ic ivau; dsb; isb)</text>
  <text x="10" y="274" fill="currentColor">LDAR/LDAPR: 뒤의 접근은 이 load 뒤로. STLR: 앞의 접근은 이 store 앞으로 (한 방향 배리어).</text>
</svg>
```

| 명령 | 인코딩 (objdump 실측) | 막는 것 | C11 에서 이 명령이 나오는 곳 (C-A02/C-A03 실측) |
|---|---|---|---|
| `dmb ish` | `d5033bbf` | ISH 도메인에서 앞의 모든 접근 → 뒤의 모든 접근 | `atomic_thread_fence(release/seq_cst)` |
| `dmb ishld` | `d50339bf` | 앞의 **load** → 뒤의 load·store | `atomic_thread_fence(acquire)` |
| `dmb ishst` | `d5033abf` | 앞의 store → 뒤의 store 만 | (Linux `smp_wmb`) |
| `dmb sy` / `dsb sy` | `d5033fbf` / `d5033f9f` | 전체 시스템 / + 완료 대기 | Cortex-M 의 모든 fence 는 `dmb sy` |
| `isb` | `d5033fdf` | (메모리 아님) 명령 재인출 | 없음 |
| `stlr w2,[x1]` | `889ffc22` | 앞의 모든 접근 → 이 store | `atomic_store(release/seq_cst)` |
| `ldar w3,[x0]` | `88dffc03` | 이 load → 뒤의 모든 접근, **앞선 stlr 도 못 앞지름** (RCsc) | `atomic_load(seq_cst)` |
| `ldapr w3,[x0]` | `b8bfc003` | 이 load → 뒤의 모든 접근 (앞선 stlr 는 앞지를 수 있음, RCpc) | `atomic_load(acquire)` (FEAT_LRCPC) |

인코딩은 `cc -c enc.s && xcrun llvm-objdump -d enc.o` 로 확인한 값이고, 디코더·연습 2 인코더의 기대값도 모두 이 출력에서 왔다.

### Part A — inline asm 커널과 실제 기계어

정답 커널 하나:

```c
NOINLINE static uint64_t r_addr(const uint32_t *flag, const uint32_t *data) {
    uint64_t f, d, t;
    __asm__ __volatile__("ldr %w[rf], [%[f]]\n\t"
                         "eor %x[t], %x[rf], %x[rf]\n\t"
                         "ldr %w[rd], [%[d], %x[t]]"
                         : [rf] "=&r"(f), [rd] "=&r"(d), [t] "=&r"(t)
                         : [f] "r"(flag), [d] "r"(data) : "memory");
    return f << 32 | d;
}
```

- 출력 변수를 `uint64_t` 로 두고 `%w` 로 32비트 load 를 한다 (W 레지스터에 쓰면 위 32비트는 0). 32비트 변수에 `%x` 를 붙이는 피연산자 폭 불일치를 피한다.
- `"=&r"` 이 없으면 컴파일러가 `rf` 와 `d`(입력)를 같은 레지스터에 줄 수 있다 — 첫 load 가 둘째 load 의 주소를 덮어쓴다.
- `"memory"` 는 **컴파일러 배리어** 역할도 한다: 메일박스에서 relaxed payload store 가 `stlr` 너머로 옮겨지지 않는 이유.

`cc -O2` 로 빌드한 바이너리의 실제 기계어 (`xcrun llvm-objdump -d --no-show-raw-insn so`, 일부):

```text
<_w_dmb>:   str w2, [x0] / dmb ish / str w2, [x1] / ret
<_w_stlr>:  str w2, [x0] / stlr w2, [x1] / ret
<_r_ldapr>: ldapr w8, [x0] / ldr w9, [x1] / orr x0, x9, x8, lsl #32 / ret
<_r_addr>:  ldr w8, [x0] / eor x10, x8, x8 / ldr w9, [x1, x10] / orr x0, x9, x8, lsl #32 / ret
<_s_rcsc>:  stlr w2, [x0] / ldar w8, [x1] / mov x0, x8 / ret
<_s_rcpc>:  stlr w2, [x0] / ldapr w8, [x1] / mov x0, x8 / ret
```

```text
1000020ec: b9400008    ldr  w8, [x0]
1000020f0: ca08010a    eor  x10, x8, x8
1000020f4: b86a6829    ldr  w9, [x1, x10]
```

레지스터는 컴파일러가 골랐지만(`w8`, `x10`) 명령과 순서는 asm 문자열 그대로다. 판정기가 레지스터 번호로 의존성을 추적하므로 "eor 가 정말 x8 을 읽고, 둘째 ldr 가 정말 x10 을 쓰는지" 까지 확인된다. 하네스의 `sig()` 는 이 목록에서 첫 메모리 접근부터 둘째까지 이름만 뽑는다 — `r_addr` 는 `"LDR ALU LDR"`. ASan/UBSan(-O1) 과 TSan 빌드에서도 같은 모양이었다 (계측 코드는 asm 블록 밖에 붙는다).

### Part A — 실측: MP 13조합 × 5회 (Apple M6)

`demo_litmus_asm.c` 는 "연속" MP 다: writer 가 `data=i; flag=i` 를 i=1..2000만 반복하고, reader 가 같은 시간 동안 `f=flag; d=data` 를 반복하며 `d < f` 를 센다. 순서가 지켜지면 flag 가 i 일 때 data 는 이미 i 이상이어야 한다. data 와 flag 는 서로 다른 128 B 줄에 둔다. 공유 변수는 asm 으로만 건드린다.

<!-- code: demo_litmus_asm.c -->

`./demo_litmus_asm 20` 5회 실제 출력의 요약 (반복 2000만 회씩):

| writer + reader | 판정 | run1 | run2 | run3 | run4 | run5 |
|---|---|---|---|---|---|---|
| plain + plain | 허용 | 4306529 | 3667459 | 6307666 | 6631989 | 6250475 |
| plain + ldapr | 허용 | 2501204 | 2843087 | 2665414 | 2893109 | 2781827 |
| plain + dmb ish | 허용 | 1460303 | 1165517 | 1028456 | 1418176 | 1012482 |
| dmb ish + dmb ishst (reader 쪽 잘못) | 허용 | 125241 | 164223 | 82354 | 38636 | 152908 |
| dmb ish + plain | 허용 | 3560 | 309 | 583 | 450 | 940 |
| stlr + plain | 허용 | 28 | 849 | 695 | 110 | 1004 |
| stlr + ctrl (cbz 만) | 허용 | 197 | 19 | 238 | 48 | 948 |
| dmb ish + dmb ish | 금지 | 0 | 0 | 0 | 0 | 0 |
| dmb ishst + dmb ishld | 금지 | 0 | 0 | 0 | 0 | 0 |
| stlr + ldar | 금지 | 0 | 0 | 0 | 0 | 0 |
| stlr + ldapr | 금지 | 0 | 0 | 0 | 0 | 0 |
| stlr + addr-dep | 금지 | 0 | 0 | 0 | 0 | 0 |
| stlr + ctrl + isb | 금지 | 0 | 0 | 0 | 0 | 0 |

같은 출력의 첫 줄: `thread_policy_set(THREAD_AFFINITY_POLICY) = 46 (KERN_NOT_SUPPORTED)` — macOS 에는 `sched_setaffinity` 가 없고, Mach affinity 태그도 이 기계에서 거부된다. 두 스레드가 어느 코어(P/E)에 앉는지는 고를 수 없어서 `pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE)` 로 P 코어 쪽 **힌트** 만 줬다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">MP "d &lt; f" 관찰 횟수 — 5회 중앙값, 로그 눈금 (2000만 회당, Apple M6 실측)</text>
  <g stroke="currentColor" stroke-dasharray="2 3">
    <path d="M250 30 V266"/><path d="M370 30 V266"/><path d="M490 30 V266"/><path d="M610 30 V266"/>
  </g>
  <g fill="currentColor">
    <text x="246" y="282">1</text><text x="360" y="282">100</text><text x="476" y="282">10⁴</text><text x="596" y="282">10⁶</text>
    <text x="10" y="46">plain + plain</text>
    <text x="10" y="64">plain + ldapr</text>
    <text x="10" y="82">plain + dmb ish</text>
    <text x="10" y="100">dmb ish + dmb ishst</text>
    <text x="10" y="118">stlr + plain</text>
    <text x="10" y="136">dmb ish + plain</text>
    <text x="10" y="154">stlr + ctrl(cbz)</text>
    <text x="10" y="172">dmb ish + dmb ish</text>
    <text x="10" y="190">dmb ishst + dmb ishld</text>
    <text x="10" y="208">stlr + ldar</text>
    <text x="10" y="226">stlr + ldapr</text>
    <text x="10" y="244">stlr + addr-dep</text>
    <text x="10" y="262">stlr + ctrl + isb</text>
  </g>
  <rect x="250" y="35" width="408" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="53" width="387" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="71" width="364" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="89" width="306" height="13" class="f-warn" stroke="currentColor"/>
  <rect x="250" y="107" width="171" height="13" class="f-acc" stroke="currentColor"/>
  <rect x="250" y="125" width="166" height="13" class="f-acc" stroke="currentColor"/>
  <rect x="250" y="143" width="138" height="13" class="f-acc" stroke="currentColor"/>
  <g fill="currentColor">
    <text x="662" y="46">6250475</text><text x="641" y="64">2781827</text><text x="618" y="82">1165517</text>
    <text x="560" y="100">125241</text><text x="425" y="118">695</text><text x="420" y="136">583</text><text x="392" y="154">197</text>
  </g>
  <g class="f-ok" stroke="currentColor">
    <rect x="250" y="161" width="4" height="13"/><rect x="250" y="179" width="4" height="13"/><rect x="250" y="197" width="4" height="13"/>
    <rect x="250" y="215" width="4" height="13"/><rect x="250" y="233" width="4" height="13"/><rect x="250" y="251" width="4" height="13"/>
  </g>
  <g fill="currentColor">
    <text x="262" y="172">0 ×5 (금지)</text><text x="262" y="190">0 ×5 (금지)</text><text x="262" y="208">0 ×5 (금지)</text>
    <text x="262" y="226">0 ×5 (금지)</text><text x="262" y="244">0 ×5 (금지)</text><text x="262" y="262">0 ×5 (금지)</text>
  </g>
  <text x="10" y="304" fill="currentColor">주황: writer 쪽이 순서를 안 지킴(수십만~수백만). 파랑: writer 는 지키고 reader 만 약함(수백~수천).</text>
  <text x="10" y="322" fill="currentColor">초록: 판정기가 "금지" 라고 한 6조합 — 5회 × 2000만 번 모두 0.</text>
</svg>
```

읽는 법:

1. **이 M6 에서는 store→store 재배치가 압도적으로 흔하다.** reader 를 `dmb ish` 로 묶어도(reader 쪽은 아키텍처상 확실히 순서가 지켜짐) writer 가 plain 이면 100만 회 이상 나온다. 이건 writer 의 두 store 가 **다른 순서로 다른 코어에게 보였다** 는 직접 증거다.
2. **load→load 재배치도 실제로 있다.** writer 를 `dmb ish`/`stlr` 로 묶고 reader 만 plain 이면 수백~수천 회. 그래서 MP 는 **양쪽 모두** 막아야 한다.
3. **제어 의존성만으로는 load→load 가 안 막힌다** — `cbz` 를 넣은 reader 가 19~948 회 깨졌다 (분기 예측으로 둘째 load 를 미리 실행). `cbz` 뒤에 `isb` 를 넣으면 0.
4. **reader 쪽 `dmb ishst` 는 load 를 막지 않는다** — 오히려 plain 보다 많이(3.8만~16만) 나왔다. 배리어가 타이밍을 바꿔 창을 넓힌 것으로 보인다(추정). "배리어를 넣었다" 가 아니라 "맞는 종류를 넣었다" 가 중요하다.
5. 빈도는 실행마다 10배 이상 흔들린다 (dmb + plain: 309 ~ 3560). C-A02 의 C 수준 MP(배열 버전)는 같은 기계에서 3 ~ 5911 이었다 — 측정 방식(연속 vs 배열)에 따라서도 크게 달라진다.

### Part A — 실측: SB, 그리고 RCsc vs RCpc

`./demo_litmus_asm 20 all` 5회 + `./demo_litmus_asm 20 sb` 8회, 총 13회 실제 값 (반복 2000만 회):

| A·B 명령열 | 판정 | 13회 관찰 횟수 |
|---|---|---|
| `str ; ldr` | 허용 | 646, 1506, 337, 1717, 1, 0, 1, 0, 37, 1, 1591, 1, 63 |
| `str ; dmb ishst ; ldr` | 허용 | 2014, 789, 651, 487, 58, 363, 762, 465, 3034, 11, 596, 628, 680 |
| `stlr ; ldapr` | **허용** | 0, 0, 0, 0, 13, 1, 106, 112, 92, 0, 751, 24, 0 |
| `str ; dmb ish ; ldr` | 금지 | 0 ×13 |
| `stlr ; ldar` | 금지 | 0 ×13 |

`stlr` 다음 `ldar` 는 13회 모두 0, `stlr` 다음 `ldapr` 는 13회 중 9회에서 관찰(최대 751)됐다. 이게 이름의 뜻이다:

- **RCsc** (release consistency, sequentially consistent): Arm ARM 의 순서 규칙에 "Store-Release 는 프로그램 순서상 뒤에 오는 **Load-Acquire** 보다 먼저 관찰된다" 가 들어 있다. 그래서 `stlr x; ldar y` 는 SB 에서 full barrier 처럼 동작하고, clang 이 seq_cst load 에 `ldar` 를 쓴다 (C-A03 실측 `-DMO=2`).
- **RCpc** (processor consistent): `ldapr`(Load-AcquirePC, ARMv8.3 FEAT_LRCPC)은 그 규칙에서 빠진다. "뒤의 접근을 앞지르게 하지 않는다" 만 남는다. C11 acquire 는 그것만 요구하므로 clang 은 acquire load 에 `ldapr` 를 쓴다 (C-A03 실측 `-DMO=1`). 그래서 C11 release/acquire 만으로 Peterson/Dekker 를 짜면 깨진다.
- `dmb ishst` 는 store→store 만, `dmb ishld` 는 앞의 load 만 묶는다 — SB 의 store→load 를 막는 건 "전부" 종류(`dmb ish`, `dmb sy`, DSB)뿐이다.

### Part A — "0 번 관찰" 은 증명이 아니다

- 관찰 1회 = **그 결과가 허용된다는 증명** 이다 (하드웨어가 실제로 했다).
- 관찰 0회 = "이번 타이밍에서는 안 나왔다" 일 뿐이다. 이 기계에서도 plain SB 는 13회 중 2회 0 이었다. 같은 코드를 다른 칩(Cortex-A72, Neoverse, 다른 Apple 세대)이나 다른 부하에서 돌리면 달라진다. Apple 코어처럼 "아키텍처는 약하지만 특정 재배치를 거의 안 보이는" 구현도 있다 — 이 레슨의 숫자는 **M6 에서는 MP 와 SB 둘 다 보였다** 는 기록일 뿐이다.
- 그래서 정확성은 **아키텍처 규칙** (Part B 판정기, herd7 같은 공리 모델)으로 판단하고, 하드웨어 리트머스는 "규칙이 실제로 쓰이는지" 확인용으로만 쓴다. 체크 테스트가 "금지 조합 = 0" 만 CHECK 하고 "허용 조합 > 0" 은 info 로만 찍는 이유다.

### Part B — 판정기: 규칙을 코드로

```c
static bool ld_ld_ordered(const op_t *o, int n) {
    int a, b;
    if (!two_mem(o, n, &a, &b) || !is_ld(o[a].k) || !is_ld(o[b].k)) return false;
    if (o[a].k == K_LDAR || o[a].k == K_LDAPR) return true;          /* acquire */
    uint32_t taint = 1u << o[a].rt;
    bool ctrl = false;
    for (int i = a + 1; i < b; i++) {
        if (bar_covers(&o[i], NEED_LD)) return true;                  /* dmb ld/ish/sy, dsb */
        if (o[i].k == K_ALU) { ... 입력이 오염 → rd 오염, 아니면 rd 지움 }
        if (o[i].k == K_CBZ && (taint >> o[i].rt & 1u)) ctrl = true;  /* 제어 의존 */
        if (o[i].k == K_ISB && ctrl) return true;                     /* + ISB */
    }
    return (taint >> o[b].rn & 1u) || (o[b].rm < 32 && (taint >> o[b].rm & 1u));   /* 주소 의존 */
}
```

| 규칙 | 판정기에서 | DDI 0487 의 개념 (B2 메모리 모델 절, 이름 기준) |
|---|---|---|
| DMB/DSB 종류·도메인 | `bar_covers` — NSH 제외, 종류 = 전부 또는 need | Barrier-ordered-before |
| Store-Release | 둘째 store 가 `STLR` → 앞의 모든 접근이 먼저 | Barrier-ordered-before (Store-Release) |
| Load-Acquire / AcquirePC | 첫 load 가 `LDAR`/`LDAPR` → 뒤의 접근이 나중 | Barrier-ordered-before (Load-Acquire) |
| `STLR` → `LDAR` (RCsc) | `st_ld_ordered` 의 특례, `LDAPR` 은 제외 | Barrier-ordered-before (Store-Release → Load-Acquire) |
| 주소 의존 | 첫 load 결과 → ALU 사슬 → 둘째 load 의 Rn/Rm | Dependency-ordered-before (address) |
| 제어 의존 + ISB | 첫 load 결과로 CBZ → 그 뒤 ISB → 둘째 load | Dependency-ordered-before (control + context synchronization) |
| 제어 의존만 | 순서 없음 (load→load) | — (load→**store** 는 제어 의존만으로 순서가 생긴다 — 투기적 store 는 보이지 않으므로) |

MP 금지 = writer st→st **그리고** reader ld→ld. ARMv8 은 other-multi-copy-atomic(한 코어의 store 가 다른 코어 하나에게 보이면 모두에게 보임)이라서 2 스레드 MP/SB 판정은 이 "각 스레드의 순서" 만으로 충분하다. (3 스레드 이상의 WRC/IRIW 는 이 판정기 범위 밖 — MEM-M07, ARCH-A05 참고.)

판정 vs 관찰: 판정기가 금지라고 한 MP 6조합, SB 2조합은 실측도 전부 0 이었고, 허용이라고 한 조합은 실측에서 모두 1번 이상 나왔다 (stlr+ldapr SB 는 13회 중 9회). 판정기는 하드웨어를 흉내 내는 게 아니라 **아키텍처가 허락하는 범위** 를 계산한다 — 하드웨어 관찰이 그 범위 안에 있는지 확인하는 것이 리트머스의 쓰임새다.

또 하나: `decode_fn` 은 함수 포인터를 데이터 포인터로 바꿔 읽는다. C11 에서 이 변환은 정의되지 않은 확장(공통 확장 J.5.7)이고, macOS arm64(PAC 없는 arm64 ABI)에서 텍스트가 읽기 가능해서 동작한다. 펌웨어에서는 같은 기법으로 부트 ROM 패치 확인, 플래시 이미지 검증 같은 "내 코드의 기계어 검사" 를 한다.

### Part C — 메일박스: Cortex-A ↔ Cortex-M 공유 SRAM

```c
static int mbox_send(mbox_t *m, uint32_t seq, const uint32_t *payload, uint32_t spins) {
    for (uint32_t s = 0; ld_acq(&m->ack) != seq - 1u; s++) {   /* 이전 메시지를 다 읽었나 (acquire) */
        if (s >= spins) return -1;
        cpu_relax();
    }
    for (int i = 0; i < MB_WORDS; i++) atomic_store_explicit(&m->w[i], payload[i], memory_order_relaxed);
    st_rel(&m->seq, seq);                                       /* 게시 (stlr) */
    return 0;
}
```

| 순서 | 무엇이 깨지나 (막는 명령) |
|---|---|
| payload → seq | 받는 쪽이 새 seq 와 옛 payload 를 본다 = MP (`stlr seq`) |
| seq 확인 → payload 읽기 | 같은 MP 의 reader 쪽 (`ldapr seq`) |
| payload 읽기 → ack | 보내는 쪽이 아직 읽는 중인 payload 를 덮어쓴다 = load→store (`stlr ack`) |
| ack 확인 → payload 쓰기 | 같은 문제의 반대편 (`ldapr ack`) |

결과 (실측): `Part C mailbox: received=100000 bad=0`, 5회 연속 77/77. **TSan 주의:** `seq`/`ack` 의 asm 접근은 TSan 이 보지 못한다. payload 를 relaxed atomic 으로 둔 덕에 TSan 이 데이터 레이스로 보고하지 않을 뿐, TSan 이 이 프로토콜의 순서를 **검증해 준 것은 아니다**. 이 부분은 스트레스 CHECK 과 판정기가 책임진다.

실제 칩으로 옮기면:

- **i.MX8M (Cortex-A53 + Cortex-M4)**, **STM32MP1 (Cortex-A7 + Cortex-M4)**: 공유 SRAM + 하드웨어 메일박스(i.MX MU, STM32 IPCC)의 인터럽트. Cortex-A7 은 ARMv7-A 라 `stlr/ldar` 가 없다 → `dmb`. Cortex-M4(ARMv7-M)도 없다 → `__DMB()`. ARMv8-M(M33/M55)에는 `LDA`/`STL` 이 있다 (DDI 0553 기준, 이 레슨에서 미검증).
- **도메인**: M 코어는 A 클러스터의 inner shareable 도메인 밖에 있는 경우가 많다. 그러면 A 쪽 배리어는 `dmb ish` 가 아니라 `dmb osh`/`dmb sy`(Linux 의 `dma_wmb()` = `dmb oshst`, C-A02 참고)가 맞고, 공유 SRAM 은 보통 non-cacheable 로 매핑한다. 이 레슨의 `ish` 는 "두 스레드가 같은 클러스터의 코어" 라는 호스트 모델에서만 충분하다.
- doorbell(MU/IPCC 레지스터 쓰기)은 Device 메모리다. payload/seq → doorbell 순서도 DMB 가 필요하다 (Linux `writel()` 이 앞에 넣는다).

### Part C — DSB/ISB 가 필요한 자리, 그리고 JIT 실측

| 상황 | 순서 (대표) | 왜 |
|---|---|---|
| 시스템 레지스터 쓰기 → 그 설정에 의존하는 명령 (CPACR_EL1.FPEN 켜고 FP 명령) | `msr ; isb` | 시스템 레지스터 변경은 context synchronization 이후에야 뒤의 명령에 보장된다 |
| 페이지 테이블 수정 → TLB 무효화 | `dsb ishst ; tlbi ... ; dsb ish ; isb` | 테이블 store 가 walker 에게 보인 뒤 무효화, 무효화 **완료** 후 재인출 |
| MMU 켜기 / TTBR 바꾸기 | `msr sctlr_el1(ttbr0_el1) ; isb` | 다음 명령 인출부터 새 변환 |
| 코드를 쓰고 실행 (일반형) | `dc cvau ; dsb ish ; ic ivau ; dsb ish ; isb` | D-cache → PoU 로 밀고(clean), I-cache 무효화, 둘 다 완료, 재인출 |
| WFI/WFE 로 잠들기 전 | `dsb ; wfi` | 진행 중 store(예: 주변장치 클리어)가 끝난 뒤 잠든다 (C-B09) |
| 캐시 clean 후 DMA 시작 | `dc cvac ; dsb ; doorbell` | 유지보수 연산의 완료는 DSB 만 기다린다 (C-A02 Part C) |
| Cortex-M: MPU 설정 후 | `DSB ; ISB` | CMSIS `ARM_MPU_Enable()` 이 끝에 `__DSB(); __ISB();` |
| Cortex-M: VTOR 변경 | `DSB` (+ 필요시 `ISB`) | 다음 예외가 새 테이블을 쓰도록 쓰기 완료 |
| Cortex-M: CONTROL(SPSEL/nPRIV) 쓰기 | `MSR ; ISB` | 뒤의 명령이 새 스택 포인터·권한으로 실행되게 (ARMv7-M ARM MSR 설명) |
| Cortex-M: CPACR 로 FPU 켜기 | `DSB ; ISB` | 첫 FP 명령이 NOCP UsageFault 를 내지 않게 (Cortex-M4 Generic User Guide 예제) |

Cortex-M 쪽 실제 asm (`clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cm_barrier.c -o -`, 지시어 제외):

<!-- code: demo_cm_barrier.c -->

```text
mpu_setup:  movw r2,#60820 / movt r2,#57344 / movs r3,#0 / dmb sy / str r3,[r2] / str r3,[r2,#4]
            str r0,[r2,#8] / movs r0,#5 / str r1,[r2,#12] / str r0,[r2] / dsb sy / isb sy / bx lr
vtor_move:  movw r1,#60680 / movt r1,#57344 / str r0,[r1] / dsb sy / bx lr
use_psp:    mrs r0,control / orr r0,r0,#2 / msr control,r0 / isb sy / bx lr
fpu_enable: movw r0,#60808 / movt r0,#57344 / ldr r1,[r0] / orr r1,r1,#15728640 / str r1,[r0] / dsb sy / isb sy / bx lr
```

`movs r3,#0` 은 `dmb sy` 위로 올라갔지만 레지스터 연산이라 상관없고, MPU 레지스터 store 들은 전부 `dmb` 뒤·`dsb` 앞에 그대로 있다 ("memory" 클로버 + volatile). Cortex-M0+(`thumbv6m`)도 같은 `dmb sy`/`dsb sy`/`isb sy` 를 냈다 — ARMv6-M 에도 세 배리어가 있다 (DDI 0419). M0+ 에 없는 것은 LDREX/STREX 와 acquire/release 명령이다 (ARM-M11, CPP-A04).

**JIT 실측** (`demo_jit.c`, 4회 실행, 첫 실행의 실제 출력):

<!-- code: demo_jit.c -->

```text
pthread_jit_write_protect_supported_np() = 1
run 1  mov w0,#42 ; ret     -> 42
run 2  add w0,w0,w1 ; ret   -> 7
run 3  sub w0,w0,w1 ; ret   -> -1
no-invalidate experiment x100000: old code ran 99994, new code ran 6, other 0
```

나머지 3회는 같은 줄이 `old code ran 100000, new code ran 0, other 0` 이었다.

같은 주소에 서로 다른 명령을 넣어 세 번 실행했고 전부 새 명령이 돌았다. 그런데 `sys_icache_invalidate` 를 **빼고** 덮어쓰면, 10만 번 중 99994 ~ 100000 번 **옛 명령이 실행됐다**. D-side 에 쓴 새 명령을 I-side 가 모르는 것이다 — 그래서 코드 수정에는 I-cache 동기화가 필수다.

`sys_icache_invalidate` 는 무엇을 하나? 함수 포인터로 자기 기계어를 읽어 `.inst` 로 다시 조립한 뒤 `llvm-objdump` 로 풀었다 (실제 출력, 앞부분):

```text
 0: cbz   x1, 0x54             ; 길이 0 이면 끝
 4: and   x9, x0, #0xffffffffffffffc0   ; 64 B 줄 정렬
 ...
1c: dsb   ish
28: ic    ivau, x9             ; 64 B 마다 반복 (20줄마다 CPU 종류 확인 후 dsb ish 를 끼우는 경로가 있음)
2c: add   x9, x9, #0x40
 ...
4c: dsb   ish
50: isb
54: ret
```

- 이 Mac 의 구현에는 **`dc cvau` 가 없다**. 일반형 순서의 앞 절반(D-cache clean)을 건너뛴다는 뜻이고, 하드웨어가 D→I 일관성에 clean 을 요구하지 않는다는 표시(CTR_EL0.IDC=1)로 **추정** 한다. 확인하려고 `mrs x0, ctr_el0` 를 사용자 공간에서 실행했더니 SIGILL(exit 132)로 죽었다 — macOS 는 EL0 의 CTR_EL0 읽기를 허용하지 않는다 (Linux arm64 는 허용/에뮬레이션). 그래서 IDC/DIC 값 자체는 **미검증**.
- `ic ivau` 루프 + `dsb ish` + `isb` 는 있다. "invalidate 없이 옛 명령이 돈다" 실측과 맞는다 (DIC=0 로 추정).
- `pthread_jit_write_protect_np()` 는 MAP_JIT 영역의 RW/RX 를 **스레드별로** 바꾼다 (Apple 문서 기준). W^X 스위치는 권한 문제만 풀 뿐, I-cache 와는 무관하다는 것이 위 실험이 보여 준 것이다.
- 펌웨어의 같은 상황: 부트로더가 RAM 에 코드를 복사하고 점프, 플래시 패치, Cortex-M7 의 I-cache 가 켜진 상태에서 코드 복사 후 `SCB_InvalidateICache()` (내부 DSB/ISB). Cortex-M3/M4 는 I-cache 가 없지만 점프 전 `DSB; ISB` 를 넣는 것이 권장이다 (AN321 기준).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread solution.c -o solution && ./solution
```

실제 출력:

```text
MP stress 2000000 iters: plain/plain=751477 (info, not checked)  dmb/dmb=0 stlr/ldapr=0 stlr/addr=0
Part C mailbox: received=100000 bad=0
77/77 passed
```

5회 연속 77/77. ASan+UBSan(`-g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread`)과 TSan(`-g -O1 -fsanitize=thread -pthread`) 빌드도 각각 77/77, 보고 없음 (TSan 은 asm 접근을 못 본다는 점은 위 Part C 참고). plain/plain 값은 ASan 빌드 550973, TSan 빌드 407592 처럼 실행마다 다르다 — CHECK 하지 않는 이유.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고침 |
|---|---|---|
| writer 에만 `dmb` 를 넣는다 | MP 는 reader 쪽 load→load 도 재배치된다 — 실측 309 ~ 3560 회 | reader 에 `dmb ishld` 또는 `ldar`/`ldapr` |
| reader 에 `dmb ishst` | ST 종류는 store 끼리만 — 실측 3.8만~16만 회 | `dmb ishld` / `dmb ish` |
| "if (flag) 안에서 읽으니 순서가 있다" | 제어 의존은 load→load 를 막지 않는다 — 실측 19 ~ 948 회 | `cbz` 뒤 `isb`, 또는 acquire |
| `stlr`/`ldapr` 로 Dekker/Peterson | RCpc 는 store→load 를 막지 않는다 — SB 13회 중 9회 관찰 | 그 자리만 `ldar`(seq_cst) 또는 `dmb ish` |
| `isb` 를 메모리 배리어로 씀 | ISB 는 명령 재인출뿐 — 판정기 연습 5 | DMB/DSB |
| 시스템 레지스터 쓰고 바로 사용 | 파이프라인에 이미 들어온 명령은 옛 설정 | `msr ; isb` |
| JIT/코드 복사 후 바로 점프 | I-side 는 옛 명령 — 실측 10만 번 중 99994 ~ 100000 | `sys_icache_invalidate` / `dc cvau; dsb ish; ic ivau; dsb ish; isb` |
| asm 출력에 `"=r"` (early clobber 없음) | 출력이 아직 안 쓴 입력과 같은 레지스터 → 주소 덮어씀 | `"=&r"` |
| `"memory"` 클로버 생략 | 컴파일러가 주변 C 접근을 asm 너머로 옮김 (CPU 배리어를 써도 소용없음) | 순서 asm 에는 항상 `"memory"` |
| 서로 다른 클러스터/장치와 `dmb ish` | 상대가 inner shareable 도메인 밖이면 부족 | `dmb osh`/`dmb sy` (DMA 는 `dma_wmb`) |

## 8. 면접 Q&A

<details><summary>Q1. DMB, DSB, ISB 를 한 문장씩, 그리고 각각 실제 예 하나씩.</summary>

DMB: 앞의 메모리 접근이 뒤의 메모리 접근보다 먼저 **관찰** 되게 한다 (순서만, 뒤의 비메모리 명령은 계속 실행) — 디스크립터 필드 → OWN, payload → seq. DSB: 앞의 메모리 접근과 캐시·TLB 유지보수가 **완료** 될 때까지 뒤의 명령을 실행하지 않는다 — `tlbi` 뒤, `ic ivau` 뒤, `wfi` 앞. ISB: 파이프라인을 비우고 뒤의 명령을 다시 가져와 **새 컨텍스트** 로 실행 — `msr sctlr_el1` 뒤, Cortex-M 의 `MSR CONTROL` 뒤. 꼬리: "MPU 켜고 왜 둘 다?" → DSB 로 MPU 레지스터 쓰기 완료, ISB 로 이미 가져온 명령을 새 권한으로 다시 가져옴.
</details>

<details><summary>Q2. STLR/LDAR 로 DMB 를 대신할 수 있나요? 장단점은?</summary>

한 방향이면 된다. `stlr` 는 "앞의 모든 접근 → 이 store", `ldar`/`ldapr` 는 "이 load → 뒤의 모든 접근". MP 는 writer `stlr flag` + reader `ldapr flag` 로 충분하다 (실측 0 ×5). 장점: 필요한 방향만 막아 더 싸고, 순서가 특정 변수에 묶여 의도가 코드에 드러난다. 단점: 주소가 레지스터 하나인 형식만 있다 (오프셋 없음, `[x1]` 만 — ARMv8.4 LRCPC2 에 `ldapur`/`stlur` 오프셋형이 추가됨). 여러 store 를 한 번에 정리하거나 store→load 를 막으려면 `dmb ish` 가 필요하다. 꼬리: "stlr + ldar 는 full barrier 인가?" → 그 두 접근 사이(store→load)는 순서가 생기지만(RCsc), `stlr` 앞의 store 와 `ldar` 뒤의 접근만 묶일 뿐 모든 쌍을 묶는 `dmb ish` 와 같지는 않다.
</details>

<details><summary>Q3. clang 은 acquire load 에 LDAPR 을, seq_cst load 에 LDAR 을 냅니다. 왜 다르죠?</summary>

C11 acquire 는 "이 load 뒤의 접근이 앞으로 오지 않는다" 만 요구한다 — `ldapr`(RCpc)이 정확히 그것이고 앞선 `stlr` 를 앞지를 수 있어서 더 싸다. seq_cst 는 모든 seq_cst 연산의 단일 전체 순서가 필요하고, 그중 "seq_cst store 다음 seq_cst load" 의 순서를 `ldar` 의 RCsc 규칙(Store-Release → Load-Acquire)이 만들어 준다. 그래서 seq_cst store=`stlr`, load=`ldar` 만으로 fence 없이 구현된다. 실측: SB 에서 `stlr;ldar` 0 ×13, `stlr;ldapr` 13회 중 9회 관찰(최대 751). FEAT_LRCPC 가 없는 코어(ARMv8.0~8.2)에서는 acquire 도 `ldar` 가 된다.
</details>

<details><summary>Q4. MP 를 2000만 번 돌려 0 이 나왔습니다. 코드가 맞다는 뜻인가요?</summary>

아니다. 관찰은 **허용의 증명** 일 뿐이고, 0 은 "이 칩·이 부하·이 타이밍에서 안 나왔다" 다. 같은 기계에서 plain SB 도 13회 중 2회는 0 이었다. Apple 코어처럼 아키텍처는 약하지만 특정 재배치를 드물게만 보이는 구현이 있고, 다음 세대 칩이나 다른 벤더(Cortex-A, Neoverse)에서는 다르다. 정확성은 아키텍처 규칙으로 판단한다: 공리 모델(herd7 `aarch64.cat`), 이 레슨의 판정기 같은 규칙 검사, C 수준이면 TSan/모델 검사기. 하드웨어 리트머스는 "규칙이 실제로 쓰인다" 는 확인과 실리콘 검증(새 칩이 규칙을 **어기지 않는지** — 금지 결과가 한 번이라도 나오면 버그)용이다.
</details>

<details><summary>Q5. 주소 의존성으로 순서를 만드는 건 무슨 쓸모가 있나요?</summary>

배리어 없이 load→load 순서를 얻는 가장 싼 방법이다. 포인터를 읽고 그 포인터로 읽는 경우(`p = head; v = p->x`)는 자연히 주소 의존이라 ARM 에서는 배리어가 필요 없다 — Linux RCU 의 `rcu_dereference()` 가 이걸 쓴다. C11 의 `memory_order_consume` 이 이걸 표현하려던 것이지만 컴파일러가 의존성을 보존하기 어려워 clang/gcc 는 consume 을 acquire 로 승격한다. 그래서 C 에서 이 트릭을 쓰려면 이 레슨처럼 asm 이 필요하다 — C 로 `eor t,f,f` 를 쓰면 컴파일러가 `t = 0` 으로 지워 버린다. 실측: `stlr + addr-dep` 0 ×5. 꼬리: "Alpha 는?" → Alpha 는 주소 의존 load 도 재배치할 수 있어서 Linux 가 `smp_read_barrier_depends()` 를 따로 뒀었다.
</details>

<details><summary>Q6. if (flag) { use(data); } 에서 분기가 있는데 왜 순서가 없죠? store 면요?</summary>

CPU 는 분기 결과를 예측하고 `data` 를 **투기적으로 미리 읽는다**. 예측이 맞으면 그 값이 그대로 쓰이므로 load→load 순서가 없다 (실측 `stlr + ctrl` 19 ~ 948 회). 분기 뒤에 `isb` 를 넣으면 분기가 확정될 때까지 뒤의 명령을 가져오지 않으므로 순서가 생긴다 (0 ×5). 반면 분기 뒤의 **store** 는 투기적으로 다른 코어에게 보일 수 없으므로 제어 의존만으로 load→store 순서가 생긴다 (Arm ARM Dependency-ordered-before). 실무에서는 `isb` 트릭보다 `ldapr`/`ldar` 를 쓴다 — 더 싸고 읽기 쉽다.
</details>

<details><summary>Q7. 자기 수정 코드(JIT, 부트로더 코드 복사)의 정확한 순서와, 각 단계가 왜 필요한가요?</summary>

`str`(새 명령) → `dc cvau`(D-cache 줄을 PoU 까지 clean) → `dsb ish`(clean 완료) → `ic ivau`(I-cache 줄 무효화) → `dsb ish`(무효화 완료) → `isb`(이 코어의 파이프라인 재인출). 다른 코어가 그 코드를 실행할 거면 그 코어도 `isb`(또는 예외 진입/복귀 같은 context synchronization)가 필요하다. CTR_EL0.IDC=1 이면 `dc cvau`, DIC=1 이면 `ic ivau` 를 생략할 수 있다 (Linux `caches_clean_inval_pou` 가 이 비트를 본다). 이 Mac 의 `sys_icache_invalidate` 는 `dsb ish ; ic ivau 루프 ; dsb ish ; isb` 였고 `dc cvau` 는 없었다. invalidate 를 빼면 10만 번 중 99994 ~ 100000 번 옛 명령이 돌았다. macOS 에서는 그 전에 `pthread_jit_write_protect_np(0/1)` 로 W^X 를 전환해야 쓰기가 가능하다.
</details>

<details><summary>Q8. dmb ish, dmb osh, dmb sy 는 어떻게 고르나요? Cortex-M 은요?</summary>

도메인 = "이 순서를 누가 봐야 하나". 같은 클러스터의 코어끼리(SMP 커널, 스레드)는 inner shareable(`ish`). 장치·DMA·다른 클러스터의 코어처럼 outer shareable 에 있는 관찰자는 `osh` (Linux `dma_wmb()` = `dmb oshst`), 모든 관찰자는 `sy`. `nsh` 는 이 PE 하나 — 다른 코어와의 순서에는 소용없다 (판정기도 거부). 종류는 필요한 방향만: store→store `st`, 앞의 load 기준 `ld`, store→load 가 끼면 전부. Cortex-M(ARMv6-M/v7-M)은 실질적으로 `SY` 만 쓰고 CMSIS `__DMB()` 가 `dmb 0xF`(= sy)다 — 이 레슨 demo 의 M4/M0+ 출력도 `dmb sy`/`dsb sy`/`isb sy` 였다.
</details>

<details><summary>Q9. 우리 Cortex-M4 부트로더는 MPU 를 켜고 DSB/ISB 없이도 잘 돌았습니다. 꼭 필요합니까?</summary>

M4 는 짧은 순서 파이프라인이라 대개 문제가 드러나지 않는다. 그러나 ① 아키텍처(ARMv7-M)는 MPU 설정이 뒤의 명령에 보장되려면 DSB/ISB 가 필요하다고 정하고, CMSIS `ARM_MPU_Enable()` 도 끝에 `__DSB(); __ISB();` 를 넣는다. ② M7(쓰기 버퍼, 캐시, 이중 발행)이나 M33/M55 로 옮기면 MPU 쓰기가 끝나기 전에 뒤의 접근이 옛 권한으로 실행될 수 있다 — 보호하려던 접근이 통과하거나, 반대로 새 영역 접근이 fault. ③ `__DSB/__ISB` 는 "memory" 클로버가 있어 컴파일러 재배치도 막는다. 비용은 부트 때 몇 사이클. 같은 논리로 `MSR CONTROL` 뒤 `ISB`, CPACR 로 FPU 켠 뒤 `DSB; ISB`.
</details>

<details><summary>Q10. x86 에서 같은 리트머스를 돌리면?</summary>

x86 은 TSO 라 store→store, load→load 재배치가 없다 → MP 는 배리어 없이도 0 이 정상(컴파일러 배리어는 여전히 필요). SB(store→load)는 store buffer 때문에 x86 에서도 관찰된다 → `mfence` 또는 `lock` 접두 명령(`xchg`)이 필요하고, 그래서 x86 의 seq_cst store 가 `xchg` 다. ARM 은 MP·SB 둘 다 보인다 (이 레슨 실측). 그래서 x86 에서 "잘 돌던" lock-free 코드가 ARM 서버/Apple Silicon 으로 옮기면 깨진다 — ARCH-A05 에서 더 다룬다.
</details>

## 9. 추가 연습

### 연습 1 — 한쪽 배리어를 필요한 만큼만

writer 는 `dmb ishst`, reader 는 `dmb ishld` 로 바꿔도 MP 가 금지되는지, 제어 의존(`cbz`)만 있는 reader 와 `cbz ; isb` reader 는 어떤지 커널을 쓰고 판정·스트레스로 확인하라.

<details><summary>답</summary>

`w_dmbst` = `str ; dmb ishst ; str`, `r_dmbld` = `ldr ; dmb ishld ; ldr`, `r_ctrl` = `ldr ; cbz f,1f ; 1: ldr`, `r_ctrlisb` = `... 1: isb ; ldr`. 판정: (dmbst, dmbld) 금지, (stlr, ctrl) 허용, (stlr, ctrl+isb) 금지, (plain, dmbld) 허용. 스트레스 100만 회: 금지 두 조합 0 (CHECK), `stlr/ctrl` 은 info 로 1, 439, 45, 42 (일반 빌드 4회), ASan·TSan 빌드에서는 0 이 나왔다. ishst/ishld 는 `ish` 보다 약하지만 MP 에는 충분하다 — Linux `smp_wmb()`/`smp_rmb()` 가 이 둘이다.
</details>

### 연습 2 — 배리어 명령 인코더

`enc_dmb(crm)`, `enc_dsb(crm)`, `enc_isb()`, `enc_ldar_w/stlr_w/ldapr_w(rt, rn)`, `enc_ldr_w/str_w(rt, rn, off)` 를 쓰고 objdump 값과 맞춰라. 디코더로 왕복해 kind/opt/rt/rn 이 보존되는지도 확인.

<details><summary>답</summary>

DMB = `0xD50330BF | CRm<<8`, DSB = `0xD503309F | CRm<<8`, ISB = `0xD5033FDF`. LDAR W = `0x88DFFC00 | Rn<<5 | Rt`, STLR W = `0x889FFC00 | ...`, LDAPR W = `0xB8BFC000 | ...`, LDR W imm = `0xB9400000 | (off/4)<<10 | Rn<<5 | Rt`, STR W = `0xB9000000 | ...`. 확인값: `dmb ld` = `d5033dbf`, `dmb st` = `d5033ebf`, `dmb oshld` = `d50331bf`, `stlr w7,[x9]` = `889ffd27`, `ldapr w7,[x9]` = `b8bfc127`, `ldr w7,[x9,#8]` = `b9400927` (모두 llvm-objdump 실측).
</details>

### 연습 3 — 상황별 배리어 레시피

`recipe(sit)` 가 다음 7개 상황의 명령 순서를 문자열로 돌려주게 하라: A-profile 시스템 레지스터 변경, TLB(페이지 테이블 수정), 코드 수정, WFI, Cortex-M MPU, CONTROL, FPU(CPACR).

<details><summary>답</summary>

`"MSR ; ISB"`, `"DSB ISHST ; TLBI ; DSB ISH ; ISB"`, `"DC CVAU ; DSB ISH ; IC IVAU ; DSB ISH ; ISB"`, `"DSB ; WFI"`, `"DSB ; ISB"`, `"MSR ; ISB"`, `"DSB ; ISB"`. 공통 규칙: **"메모리/유지보수가 끝나야 한다" → DSB, "뒤의 명령이 새 설정으로 실행돼야 한다" → ISB**, 둘 다면 DSB 다음 ISB. 순서만 필요하면 DMB (C-A02 연습 4 의 DMA 칸).
</details>

### 연습 4 — JIT 한 단어 패치

`jit_patch(j, idx, word)` 로 한 단어만 바꾸고 그 4바이트만 동기화하라. `ldapr w0,[x0] ; ret` 를 JIT 해 변수를 읽고, 0번 단어를 `ldar w0,[x0]`, `ldr w0,[x0,#4]` 로 바꿔 가며 실행하라.

<details><summary>답</summary>

```c
static int jit_patch(jit_t *j, size_t idx, uint32_t word) {
    if (idx >= j->cap) return -1;
    pthread_jit_write_protect_np(0);
    j->page[idx] = word;
    pthread_jit_write_protect_np(1);
    sys_icache_invalidate(&j->page[idx], 4);
    return 0;
}
```

결과 0x1111 → 0x1111 → 0x2222, 그리고 1000번 번갈아 패치해도 매번 새 명령. 실제 핫 패치(라이브 패치, 트레이싱의 `nop`↔`b`)는 다른 코어가 **동시에 실행 중** 일 수 있어 더 엄격하다: 아키텍처가 동시 수정을 허용하는 명령은 `B`, `BL`, `NOP`, `BRK`, `SVC`, `HVC`, `SMC`, `ISB` 같은 일부뿐이다 (DDI 0487 "Concurrent modification and execution of instructions", 목록은 문서 기준, 미검증).
</details>

### 연습 5 — 판정기 확장 케이스

`dmb osh`, `dmb oshst`, `dmb st`, `dmb ld`, `dmb nsh`, `dsb ishld`, `dsb sy`, `isb` 단독, `stlr ; dmb ishld ; ldapr` 를 판정하라. 두 스레드 SB 가 `dmb sy` 하나씩으로 금지되는지, 한쪽이 `isb` 면 어떤지도.

<details><summary>답</summary>

st→st: osh·oshst·st 모두 순서 있음 (도메인이 ISH 보다 넓어도 된다). ld→ld: `dmb ld` 있음, `dmb nsh` 없음, `dsb ishld` 있음. st→ld: `dsb sy` 있음, `isb` 없음 (메모리 배리어가 아님), `dmb ld` 없음 (앞의 **store** 는 안 묶음), `stlr ; dmb ishld ; ldapr` 없음. SB: 두 스레드 모두 `dmb sy` 면 금지, 한쪽만이면 허용 — MP·SB 는 "양쪽 다" 가 규칙이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread practice.c -o practice && ./practice
```

실제 출력:

```text
ex1 MP 1M: dmbst/dmbld=0 stlr/ctrl+isb=0  stlr/ctrl=439 (info, not checked)
46/46 passed
```

## 10. 핵심 정리

- DMB = 순서(관찰 순서), DSB = 완료(유지보수·설정까지 끝날 때까지 정지), ISB = 재인출(새 컨텍스트). "끝나야 한다" → DSB, "새 설정으로 실행" → ISB.
- DMB 옵션 = 종류(LD/ST/전부) × 도메인(NSH/ISH/OSH/SY). 코어끼리 `ish`, 장치·다른 클러스터 `osh`/`sy`, Cortex-M 은 사실상 `sy`.
- `stlr` = 앞 → 이 store, `ldar`/`ldapr` = 이 load → 뒤. `stlr`→`ldar` 는 순서 있음(RCsc), `stlr`→`ldapr` 는 없음(RCpc) — SB 실측 0 ×13 vs 13회 중 9회.
- MP 는 writer 와 reader **둘 다** 막아야 한다. Apple M6 실측: writer 만 약하면 수십만~수백만, reader 만 약하면 수백~수천, 양쪽 막으면 0.
- 주소 의존은 배리어 없이 load→load 순서를 준다(RCU). 제어 의존만으로는 안 되고 `+ isb` 면 된다 (실측 19~948 → 0).
- 관찰 1회 = 허용의 증명, 관찰 0회 = 증명 아님. 정확성은 아키텍처 규칙(판정기, herd7)으로.
- 코드 수정: `dc cvau ; dsb ish ; ic ivau ; dsb ish ; isb`. macOS: W^X 전환 + `sys_icache_invalidate` (이 Mac 구현은 `dc cvau` 없이 `ic ivau` + `dsb ish` + `isb`). 빼면 옛 명령이 돈다 (실측 99994~100000/100000).
- Cortex-M: MPU/CPACR 뒤 `DSB; ISB`, CONTROL 뒤 `ISB`, VTOR 뒤 `DSB`. "M4 에서 됐다" 는 M7 에서 깨진다.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — B2 "The AArch64 Application Level Memory Model" 의 메모리 모델 정의 (Barrier-ordered-before, Dependency-ordered-before, Ordered-before, other-multi-copy atomicity), "Concurrent modification and execution of instructions", 캐시 유지보수와 CTR_EL0(IDC/DIC), C6 명령 설명(DMB, DSB, ISB, LDAR, LDAPR, STLR). 절 번호는 판본마다 달라 이름으로 적었다 — <https://developer.arm.com/documentation/ddi0487/latest/>
- ARMv7-M Architecture Reference Manual (DDI 0403) — Memory barriers(DMB/DSB/ISB), MSR(CONTROL) 뒤 ISB, System Control Space 쓰기와 동기화 — <https://developer.arm.com/documentation/ddi0403/latest/>
- ARMv6-M Architecture Reference Manual (DDI 0419) — v6-M 의 DMB/DSB/ISB — <https://developer.arm.com/documentation/ddi0419/latest/>
- Arm AN321 "ARM Cortex-M Programming Guide to Memory Barrier Instructions" — MPU, VTOR, CONTROL, 슬립, 코드 수정 시 배리어 — <https://developer.arm.com/documentation/dai0321/latest/>
- Cortex-M4 Devices Generic User Guide (DUI 0553) — FPU 켜기 예제(CPACR → DSB → ISB) — <https://developer.arm.com/documentation/dui0553/latest/>
- CMSIS-Core `mpu_armv7.h` `ARM_MPU_Enable/Disable`, `__DMB/__DSB/__ISB` — <https://arm-software.github.io/CMSIS_5/Core/html/group__mpu__functions.html>
- Apple, "Porting just-in-time compilers to Apple silicon" (`MAP_JIT`, `pthread_jit_write_protect_np`, `sys_icache_invalidate`) — <https://developer.apple.com/documentation/apple-silicon/porting-just-in-time-compilers-to-apple-silicon>
- Linux `Documentation/memory-barriers.txt`, `arch/arm64/include/asm/barrier.h`, `arch/arm64/mm/cache.S` (`caches_clean_inval_pou`) — <https://github.com/torvalds/linux>
- herd7 / litmus7 (diy 도구) — AArch64 공리 모델로 리트머스 판정, 실제 하드웨어 실행 — <https://diy.inria.fr/>
- Pulte et al., "Simplifying ARM Concurrency: Multicopy-atomic Axiomatic and Operational Models for ARMv8" (POPL 2018) — <https://www.cl.cam.ac.uk/~pes20/armv8-mca/>
- 관련 레슨: C-A02 (C 수준 fence 선택, DMA 디스크립터), C-A03 (C11 atomics → `stlr`/`ldapr`/`ldar`, SB), C-B09 (WFI 전 DSB), C-M14 (ISR 공유), OS-M08 (C11 패턴), CPP-A04 (std::atomic 과 Cortex-M), MCPP-M13 (fence 규칙), ARM-M04 (LDXR/STXR·LSE), ARM-M11 (Cortex-M LDREX/STREX), RISCV-M04 (LR/SC), LLAT-B06 (비용 실측), ARCH-A05 (리트머스), MEM-M07 (모델 시뮬레이터)
