---
id: RISCV-A04
time: 110분
updated: 2026-11-08
---
# RISCV-A04 RVWMO 메모리 모델: FENCE 종류·.aq/.rl과 C11 매핑

## 학습 목표

- C11 `atomic_load/store/RMW/CAS/thread_fence` 를 memory_order 별로 riscv64 로 컴파일한 **실측 매핑 표**(clang 21 · gcc 15.2, `rv64gc` 와 `rv64gc_ztso`)를 만들고, RISC-V 스펙 부록 A 의 표와 한 칸씩 비교해 **다른 칸 하나(seq_cst store 의 뒤 fence)** 가 왜 생겼는지 설명한다.
- RVWMO 의 보존 프로그램 순서(preserved program order, ppo) 13개 규칙을 네 묶음(같은 주소 · 명시적 동기화 · 문법적 의존 · 파이프라인 의존)으로 말하고, `fence rw,rw` / `fence r,rw` / `fence rw,w` / `fence.tso` / `fence w,w` 가 각각 **어떤 (앞, 뒤) 종류 쌍**을 막는지 표로 그린다.
- `.aq` / `.rl` / `.aqrl` 와 RCpc / RCsc 의 차이를 SB 리트머스 하나로 보여 준다 (`sw.rl; lw.aq` 는 막히고 `sw.rl; lw.aqpc` 는 안 막힘 — ARM-M05 의 `stlr;ldar` vs `stlr;ldapr` 실측과 같은 이야기).
- 직접 짠 axiomatic 열거기(후보 실행 + 비순환성)로 MP · SB · LB 의 허용 결과를 **SC · TSO(Ztso) · RVWMO · ARMv8-lite** 에서 계산하고, 금지된 결과는 cycle 로 "왜 금지인지" 출력한다.
- 매핑을 섞을 때(다른 컴파일러 / ABI 로 만든 라이브러리 링크), Ztso 바이너리를 RVWMO 칩에서 돌릴 때 무엇이 깨지는지 열거기로 증명한다.

## 1. 문제

"RISC-V 에서 `atomic_load(seq_cst)` 는 무슨 명령이 되나요? `fence.tso` 는 `fence rw,rw` 와 뭐가 다르죠? `.aq` 만 붙인 AMO 뒤의 load 는 앞으로 넘어올 수 있나요?" — RISC-V 코어를 쓰는 칩 회사(CPU IP, 가속기의 관리 코어, 자동차 SoC)의 펌웨어 · 컴파일러 · **실리콘 검증** 면접에서 나오는 질문이다. 멀티 hart RISC-V SoC 에서 메일박스 · 링 버퍼 · 락을 짤 때, 그리고 "이 코어가 메모리 모델을 지키는가"를 리트머스로 검증할 때 바로 쓰인다.

이미 본 것 (다시 가르치지 않는다): C11 release/acquire 는 C-A03, fence · release sequence 는 MCPP-M13, ARM 배리어 실측은 ARM-M05, LR/SC · AMO 인코딩과 순차 일관(SC) ISS 는 **RISCV-M04** (그 ISS 는 SC 모델이라 aq/rl/fence 효과가 안 보였다 — 이 레슨이 그 빈칸이다). 같은 층에서 병렬로 쓰이는 레슨: **MEM-M07** (SC/TSO/ARM 열거기 — 이 레슨은 자기 열거기를 따로 내장한다), **ARCH-A05** (M6 실하드웨어 리트머스 + Rosetta TSO).

RVWMO(RISC-V Weak Memory Ordering)는 **다중 복사 원자적(multi-copy atomic)** 이고 ARMv8 과 비슷하게 약한 모델이다: 다른 주소로 가는 load/store 는 fence · 주석(annotation) · 의존성이 없으면 서로 순서를 바꿔 보일 수 있다. 그래서 순서를 붙이는 도구가 두 가지다 — **FENCE** (앞 집합 pred → 뒤 집합 succ) 와 **AMO/LR/SC 의 aq·rl 비트**. 아래 그림이 이 레슨의 핵심 표다 (열거기로 하나씩 검증한다).

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">FENCE pred,succ 가 지키는 (po 앞 → po 뒤) 쌍 — ■ = 순서 보존</text>
  <text x="200" y="48" fill="currentColor">R→R</text><text x="270" y="48" fill="currentColor">R→W</text>
  <text x="340" y="48" fill="currentColor">W→R</text><text x="410" y="48" fill="currentColor">W→W</text>
  <text x="480" y="48" fill="currentColor">쓰는 곳 (실측 매핑 / 최소 처방)</text>
  <g font-family="monospace">
    <text x="20" y="80" fill="currentColor">fence rw,rw</text>
    <text x="20" y="114" fill="currentColor">fence.tso</text>
    <text x="20" y="148" fill="currentColor">fence r,rw</text>
    <text x="20" y="182" fill="currentColor">fence rw,w</text>
    <text x="20" y="216" fill="currentColor">fence w,w</text>
    <text x="20" y="250" fill="currentColor">fence r,r</text>
    <text x="20" y="284" fill="currentColor">fence w,r</text>
    <text x="20" y="318" fill="currentColor">fence r,w</text>
  </g>
  <rect x="196" y="64" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="266" y="64" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="336" y="64" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="406" y="64" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="196" y="98" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="266" y="98" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="336" y="98" width="50" height="24" class="f-warn" stroke="currentColor"/><rect x="406" y="98" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="196" y="132" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="266" y="132" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="336" y="132" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="406" y="132" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="196" y="166" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="266" y="166" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="336" y="166" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="406" y="166" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="196" y="200" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="266" y="200" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="336" y="200" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="406" y="200" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="196" y="234" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="266" y="234" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="336" y="234" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="406" y="234" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="196" y="268" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="266" y="268" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="336" y="268" width="50" height="24" class="f-ok" stroke="currentColor"/><rect x="406" y="268" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <rect x="196" y="302" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="266" y="302" width="50" height="24" class="f-ok" stroke="currentColor"/>
  <rect x="336" y="302" width="50" height="24" class="f-mut" stroke="currentColor"/><rect x="406" y="302" width="50" height="24" class="f-mut" stroke="currentColor"/>
  <text x="480" y="81" fill="currentColor">seq_cst fence, seq_cst load 앞</text>
  <text x="480" y="115" fill="currentColor">acq_rel fence (W→R 만 풀림 ▲)</text>
  <text x="480" y="149" fill="currentColor">acquire: load 뒤 · acquire fence</text>
  <text x="480" y="183" fill="currentColor">release: store 앞 · release fence</text>
  <text x="480" y="217" fill="currentColor">MP writer 최소 · smp_wmb</text>
  <text x="480" y="251" fill="currentColor">MP reader 최소 · smp_rmb</text>
  <text x="480" y="285" fill="currentColor">SB 최소 (C11 매핑엔 안 나옴)</text>
  <text x="480" y="319" fill="currentColor">LB 최소</text>
  <text x="20" y="350" fill="currentColor">▲ fence.tso = TSO 가 이미 지키는 순서만 → SB 못 막음 (rv32.adoc fm 표, riscv-defs.cat)</text>
</svg>
```

### Part A — C11 → RISC-V 매핑 표 (실측 + 스펙 비교)

`demo_map.c` 의 함수 26개(load 3 · store 3 · fetch_add 5 + 64비트 1 · exchange 4 · CAS 5 · fence 4 · signal fence 1)를 Lima VM 에서 **clang `rv64gc` / clang `rv64gc_ztso` / gcc `rv64gc` / gcc `rv64gc_ztso`** 로 `-O2 -S` 해서 매핑 표를 만든다. `solution.py` 에서는 (1) 스펙 부록 A 의 C/C++ 매핑 표를 `lower_spec(op, order)` 로 코드화하고 (TODO), (2) 실측 표(`OBSERVED`)와 비교해 **다른 칸이 정확히 seq_cst store 하나**임을 CHECK 한다. Ztso 표에는 "PW 와 SR 이 둘 다 있는 fence(= `fence rw,rw`)만 남는다" 는 스펙 노트를 CHECK 한다.

### Part B — RVWMO 규칙 부분집합으로 MP · SB · LB 열거

열거기의 ppo 함수에 RVWMO 규칙 r1–r7(같은 주소 r1–r3, FENCE r4, 주석 r5–r7)을 넣는다. TODO 두 개: `fence_orders(f, a, b)` (pred/succ 와 fence.tso 정의), `annot_rule(a, b, ann)` (r5 acquire · r6 release · r7 RCsc). 변형 22개 × 모델 4개의 "weird 결과 허용/금지" 가 문헌 · 스펙의 알려진 결과와 같아야 하고, Ztso(스펙 정의 = RVWMO + 암묵 주석)는 **독립적으로 정의한 x86-TSO** 와 결과 집합이 같아야 한다. 금지된 결과는 cycle 로 설명한다.

### Part C — C11 리트머스를 매핑으로 내려서 끝까지 확인 + 매핑 섞기

C11 MP(release/acquire) · SB(seq_cst) 를 Part A 매핑으로 RISC-V 로 내리고 RVWMO 에서 C11 이 금지하는 결과가 정말 금지되는지 확인한다. 그다음 펌웨어 현실: **스펙 표(A.6)로 만든 store + 가상의 load-acquire 표(Zalasr)로 만든 load 를 섞으면 SB 가 깨진다** — 컴파일러가 seq_cst store 뒤에 `fence rw,rw` 를 하나 더 붙이는 이유다. Ztso 로 컴파일한 바이너리가 RVWMO 칩에서 왜 거부돼야 하는지(ELF `TSO` 플래그)도 본다.

## 2. Problem (English)

> Build an axiomatic litmus-test enumerator for RISC-V and use it to justify the C11 → RISC-V mapping.
>
> 1. `lower_spec(op, order) -> list[str]`: encode the RISC-V ISA manual's "Mappings from C/C++ primitives to RISC-V primitives" (load/store/RMW-AMO/CAS-LRSC/fence × memory_order). Compare it with real `clang`/`gcc` `-O2 -S` output for `rv64gc` and `rv64gc_ztso`; exactly one entry may differ — explain it.
> 2. `fence_orders(f, a, b) -> bool`: does FENCE `f` (pred/succ ⊆ {r,w}, or `fence.tso`) order po-earlier access `a` before po-later access `b`? AMOs are both reads and writes.
> 3. `annot_rule(a, b, ann) -> str|None`: RVWMO PPO rules 5–7 (acquire, release, both RCsc).
> 4. With PPO r1–r7, the enumerator must reproduce: MP/SB/LB weird outcomes allowed under RVWMO with no fences; `fence rw,rw` forbids all three; `fence.tso` forbids MP and LB but **not** SB; Ztso ≡ x86-TSO on annotation-free tests.
>
> Constraints: Python 3.9 stdlib, deterministic output, each run < 1 s.
> Follow-ups: Why does clang put a trailing `fence rw,rw` after a seq_cst store although the manual's table doesn't? What does Ztso change in the mapping, and why can't a Ztso binary run on an RVWMO core? Why is `.aq` on its own not enough for SB? What does QEMU do with `fence.tso`?

## 3. 시작 코드

<!-- code: starter.py -->

```bash
cd lessons/RISCV/RISCV-A04
python3 -I -W error starter.py ; echo "exit=$?"       # 12/45 passed, exit 1
python3 -I -W error solution.py --table               # 45/45 + 비교 표 · cycle · 매핑 표
```

처음 실행 결과 (실제 출력, 긴 줄은 자르고 가운데 생략):

```text
FAIL A clang21 rv64gc vs spec table: only seq_cst store differs: [('cas', 'acq_rel'), ('cas', 'acquire'), ('ca
FAIL A clang21 rv64gc seq_cst store = spec + trailing fence rw,rw: ['fence rw,w', 's', 'fence rw,rw']
…
FAIL A spec: acq_rel fence = fence.tso
FAIL B MP                 SC/TSO/RVWMO/ARMv8 = FFAA: got FAAA
FAIL B MP+fence rw,rw     SC/TSO/RVWMO/ARMv8 = FFFF: got FAAF
…
FAIL B Ztso == x86-TSO on all 16 annotation-free variants: ['LB', 'LB+fence r,w', 'LB+fence rw,rw', 'LB+fence.
FAIL B SB+fence rw,rw forbidden by cycle r4-fr-r4-fr: []
…
FAIL C compiler map: xchg seq_cst as SB store -> forbidden on RVWMO
12/45 passed
```

`got FAAA` 를 읽는 법: 네 글자가 SC / TSO / RVWMO / ARMv8 순서의 "weird 결과 허용(A)/금지(F)". 시작 코드에선 fence 가 아무것도 안 막고 Ztso 의 암묵 주석도 효과가 없어서 TSO 열까지 A 가 된다. ARMv8 열은 자체 DMB 표를 쓰므로 fence 가 있는 변형에서 이미 맞다(F).

미니 문법 (스레드 = 문자열 리스트): `lw[.aq|.aqpc] r0 x`, `sw[.rl] x 1`, `amoswap|amoadd[.aq|.rl|.aqrl] r0 x 1`, `fence P,S`, `fence.tso`. `.aqpc` 는 acquire-**RCpc** (RISC-V 명령엔 없음 — Ztso 의 암묵 주석과 ARM `LDAPR` 을 표현하려고 둔 것), `lw.aq` / `sw.rl` 은 Zalasr(load-acquire/store-release 확장, clang 21 에서 실험적 0.1)의 RCsc 형태다.

데모 (Lima VM `elinux`, 맥 clang 엔 RISC-V 백엔드 없음 — VM 홈 마운트가 읽기 전용이라 `/tmp/RISCV-A04_work/` 로 복사해서):

<!-- code: demo_map.c -->

<!-- code: demo_sb_qemu.c -->

## 4. 힌트

<details><summary>힌트 1 — 매핑 표는 "load 는 뒤에, store 는 앞에"</summary>

acquire 는 "이 load 뒤의 접근이 앞으로 못 온다" → load **뒤에** `fence r,rw`. release 는 "이 store 앞의 접근이 뒤로 못 간다" → store **앞에** `fence rw,w`. seq_cst load 는 앞의 seq_cst store 와의 W→R 까지 막아야 하므로 앞에 `fence rw,rw` 가 하나 더. AMO 는 비트로: acquire `.aq`, release `.rl`, acq_rel · seq_cst `.aqrl`. CAS 는 LR/SC 루프에서 LR 에 aq, SC 에 rl (seq_cst 는 LR 에 `.aqrl`).
</details>

<details><summary>힌트 2 — fence_orders 는 집합 두 개의 곱</summary>

`fence P,S` 는 "a 가 P 에 속하고 b 가 S 에 속하면" 순서를 지킨다. a 가 load 면 `"r" in P`, store 면 `"w" in P` — AMO 는 둘 다라서 어느 쪽이든 걸리면 된다. `fence.tso` 는 특별: 스펙 문장 그대로 "앞의 **load** → 뒤의 모든 것, 앞의 **store** → 뒤의 **store**".
</details>

<details><summary>힌트 3 — r5/r6/r7 은 RCpc 와 RCsc 를 구분하는 곳</summary>

r5 "a 에 acquire 주석" 과 r6 "b 에 release 주석" 은 RCpc 든 RCsc 든 상관없다. r7 은 "a 와 b **둘 다** RCsc" 일 때만 — release store → acquire load 의 W→R 을 막는 유일한 주석 규칙이다. `ann(e)` 가 `(acq, rel)` 을 돌려주고 값은 `None` / `"pc"` / `"sc"`.
</details>

<details><summary>힌트 4 — Ztso 가 공짜로 맞는 이유</summary>

`ann_ztso` 는 이미 짜여 있다: 모든 load = acquire-RCpc, 모든 store = release-RCpc, AMO = RCsc 둘 다. r5/r6 이 맞으면 R→M 과 M→W 가 전부 보존되고, W→R 은 r7 이 RCpc 를 안 쳐 주므로 풀린다 = TSO. 그래서 r5–r7 만 고치면 "Ztso == x86-TSO" CHECK 가 통과한다.
</details>

<details><summary>힌트 5 — cycle 이 안 찍히면</summary>

금지 = "weird 결과를 내는 **모든** 후보 실행에서 관계 합집합에 cycle 이 있다". SB 의 weird 실행에선 두 load 가 init(0)을 읽으므로 `fr` 이 load → 상대 store 로 간다. 같은 스레드 안에서 store → load 를 잇는 ppo 간선(r4 또는 r7)이 있어야 cycle 이 닫힌다.
</details>

## 5. 풀이

### Part A — C11 → RISC-V: 컴파일러가 실제로 낸 것

명령 (VM 안, `/tmp/RISCV-A04_work/`):

```bash
clang --target=riscv64-linux-gnu -march=rv64gc      -O2 -S demo_map.c -o map_clang.s
clang --target=riscv64-linux-gnu -march=rv64gc_ztso -O2 -S demo_map.c -o map_clang_ztso.s
riscv64-linux-gnu-gcc            -march=rv64gc      -O2 -S demo_map.c -o map_gcc.s
riscv64-linux-gnu-gcc            -march=rv64gc_ztso -O2 -S demo_map.c -o map_gcc_ztso.s
# Ubuntu clang 21.1.8, riscv64-linux-gnu-gcc 15.2.0
```

실제 출력 일부 (함수 본문에서 지시어 제거, 한 줄로 합침):

```text
== ld_seq_cst
  map_clang        fence rw, rw ; auipc a0, %pcrel_hi(g) ; lw a0, %pcrel_lo(.Lpcrel_hi2)(a0) ; fence r, rw ; ret
  map_clang_ztso   fence rw, rw ; auipc a0, %pcrel_hi(g) ; lw a0, %pcrel_lo(.Lpcrel_hi2)(a0) ; ret
  map_gcc          lla a5,.LANCHOR0 ; fence rw,rw ; lw a0,0(a5) ; fence r,rw ; sext.w a0,a0 ; ret
  map_gcc_ztso     lla a5,.LANCHOR0 ; fence rw,rw ; lw a0,0(a5) ; sext.w a0,a0 ; ret
== st_seq_cst
  map_clang        fence rw, w ; auipc a1, %pcrel_hi(g) ; sw a0, %pcrel_lo(.Lpcrel_hi5)(a1) ; fence rw, rw ; ret
  map_clang_ztso   auipc a1, %pcrel_hi(g) ; sw a0, %pcrel_lo(.Lpcrel_hi5)(a1) ; fence rw, rw ; ret
  map_gcc          lla a5,.LANCHOR0 ; fence rw,w ; sw a0,0(a5) ; fence rw,rw ; ret
  map_gcc_ztso     lla a5,.LANCHOR0 ; sw a0,0(a5) ; fence rw,rw ; ret
== fadd_acq_rel
  map_clang        auipc a1, %pcrel_hi(g) ; addi a1, a1, %pcrel_lo(.Lpcrel_hi9) ; amoadd.w.aqrl a0, a0, (a1) ; ret
  map_clang_ztso   auipc a1, %pcrel_hi(g) ; addi a1, a1, %pcrel_lo(.Lpcrel_hi9) ; amoadd.w a0, a0, (a1) ; ret
== cas_seq_cst
  map_clang        lw a3, 0(a0) ; … ; lr.w.aqrl a2, (a4) ; bne a2, a3, .LBB20_5 ; sc.w.rl a5, a1, (a4) ; bnez a5, .LBB20_3 ; …
  map_clang_ztso   lw a3, 0(a0) ; … ; lr.w.aqrl a2, (a4) ; bne a2, a3, .LBB20_5 ; sc.w.rl a5, a1, (a4) ; bnez a5, .LBB20_3 ; …
== fn_acq_rel
  map_clang        fence.tso ; ret
  map_clang_ztso   ret
```

전체를 정규화한 표 (`solution.py --table` 의 실제 출력 그대로; `l`/`s` = load/store, `amo` = amoadd/amoswap, CAS 앞뒤의 `expected` 포인터 읽기/쓰기 `lw`/`sw` 는 원자 연산이 아니라서 뺐다 — 스크립트로 4개 `.s` 와 `OBSERVED` 를 대조해 그 외 불일치 0 확인):

```text
cas.acq_rel        spec=lr.aq; sc.rl                       cc=lr.aq; sc.rl                       ztso=lr; sc
cas.acquire        spec=lr.aq; sc                          cc=lr.aq; sc                          ztso=lr; sc
cas.relaxed        spec=lr; sc                             cc=lr; sc                             ztso=lr; sc
cas.release        spec=lr; sc.rl                          cc=lr; sc.rl                          ztso=lr; sc
cas.seq_cst        spec=lr.aqrl; sc.rl                     cc=lr.aqrl; sc.rl                     ztso=lr.aqrl; sc.rl
fence.acq_rel      spec=fence.tso                          cc=fence.tso                          ztso=(없음)
fence.acquire      spec=fence r,rw                         cc=fence r,rw                         ztso=(없음)
fence.release      spec=fence rw,w                         cc=fence rw,w                         ztso=(없음)
fence.seq_cst      spec=fence rw,rw                        cc=fence rw,rw                        ztso=fence rw,rw
load.acquire       spec=l; fence r,rw                      cc=l; fence r,rw                      ztso=l
load.relaxed       spec=l                                  cc=l                                  ztso=l
load.seq_cst       spec=fence rw,rw; l; fence r,rw         cc=fence rw,rw; l; fence r,rw         ztso=fence rw,rw; l
rmw.acq_rel        spec=amo.aqrl                           cc=amo.aqrl                           ztso=amo
rmw.acquire        spec=amo.aq                             cc=amo.aq                             ztso=amo
rmw.relaxed        spec=amo                                cc=amo                                ztso=amo
rmw.release        spec=amo.rl                             cc=amo.rl                             ztso=amo
rmw.seq_cst        spec=amo.aqrl                           cc=amo.aqrl                           ztso=amo
store.relaxed      spec=s                                  cc=s                                  ztso=s
store.release      spec=fence rw,w; s                      cc=fence rw,w; s                      ztso=s
store.seq_cst      spec=fence rw,w; s                      cc=fence rw,w; s; fence rw,rw         ztso=s; fence rw,rw
```

관찰 정리:

| 질문 | 답 (실측) |
|---|---|
| clang 과 gcc 가 다른가? | **아니오.** 원자 연산 부분은 4개 조합 모두 clang == gcc (레지스터 · 주소 만들기 `auipc`/`lla` · `sext.w` 만 다름) |
| 스펙 표를 따르나? | **한 칸 빼고 예.** `atomic_store(seq_cst)` 만 스펙 `fence rw,w; s` 뒤에 `fence rw,rw` 가 붙음 |
| 64비트? | `fadd64_seq_cst` → `amoadd.d.aqrl` (같은 규칙, `.d`) |
| `atomic_signal_fence` | 명령 0개 (`ret` 만) — 컴파일러 배리어일 뿐 (C-A02) |
| Ztso 에서 사라진 것 | acquire/release 용 fence 전부, AMO 의 aq/rl 비트 전부, CAS(relaxed~acq_rel) 의 aq/rl |
| Ztso 에서 남은 것 | `fence rw,rw` 3곳 (seq_cst load 앞, seq_cst store 뒤, seq_cst fence) + seq_cst CAS 의 `lr.w.aqrl / sc.w.rl` |

**왜 뒤 fence 가 붙나 — 스펙 본문이 직접 경고한다.** 부록 A 의 표 바로 위 문장 (riscv-isa-manual main, `src/unpriv/mm-explanatory.adoc`, 2026-11-08 확인):

> "If load and store opcodes with _aq_ and _rl_ modifiers are introduced, then the mappings in [hypothetical table] will suffice. … Even more importantly, a [Table A.6] sequentially consistent store, followed by a [hypothetical] sequentially consistent load can be reordered unless the [Table A.6] mapping of stores is strengthened by either adding a second fence or mapping the store to `amoswap.rl` instead."

가상 표에서 seq_cst load 는 `l.aq` 하나다(앞 fence 없음). 스펙 표로 만든 seq_cst store(`fence rw,w; s`) 뒤에 그런 load 가 오면 W→R 을 막을 것이 없다. 그래서 clang 21 · gcc 15 는 **미래의 Zalasr 코드와 섞여도 안전하도록** store 쪽을 강화한 것으로 보인다. LLVM 쪽 증거:

```bash
llc -mtriple=riscv64 -mattr=help | grep -i trailing
#   no-trailing-seq-cst-fence        - Disable trailing fence for seq-cst store..
clang --target=riscv64-linux-gnu -march=rv64gc -O2 -S demo_map.c -o - -Xclang -target-feature -Xclang +no-trailing-seq-cst-fence
# st_seq_cst:  fence rw, w ; auipc ; sw ; ret        ← 스펙 표 그대로로 돌아감
clang --target=riscv64-linux-gnu -march=rv64gc_zalasr0p1 -menable-experimental-extensions -O2 -S demo_map.c
# ld_acquire: lw.aq   ld_seq_cst: lw.aq   st_release: sw.rl   st_seq_cst: sw.rl   ← 가상 표와 일치
```

(이 선택이 RISC-V psABI 문서의 어느 절에 적혀 있는지는 확인하지 못했다 — "확인 필요".) Part C 에서 이 문장을 열거기로 증명한다.

**Ztso 에서 seq_cst CAS 만 aq/rl 이 남는 이유 (해석):** Ztso 는 AMO 를 RCsc 로 만들어 주지만 LR 은 그냥 load(acquire-**RCpc**), SC 는 store(release-RCpc) 취급이다. seq_cst RMW 는 RCsc 가 필요하니 `.aqrl` / `.rl` 을 남겨야 한다. acq_rel 까지는 RCpc 로 충분해서 비트를 지웠다. 반대로 `fence rw,rw` 는 Ztso 노트 "PW 와 SR 이 둘 다 없는 non-I/O fence 는 중복" 에 해당하지 않아서 남는다 (Part A CHECK). seq_cst load 앞 · store 뒤에 둘 다 남는 건 같은 "섞어도 안전" 논리로 보인다 (미검증).

**ELF 플래그** — Ztso 로 만든 오브젝트는 표시가 붙고, 링크하면 전염된다 (실측):

```text
$ riscv64-linux-gnu-readelf -h m.o      | grep Flags   →  0x5, RVC, double-float ABI
$ riscv64-linux-gnu-readelf -h m_ztso.o | grep Flags   →  0x15, RVC, TSO, double-float ABI   (gcc 도 0x15)
$ riscv64-linux-gnu-ld -r a.o(ztso) b.o(rvwmo) → ab.o  →  0x15, RVC, TSO, double-float ABI   (ld.lld -r 도 같음)
```

스펙 Ztso 절: "Binaries compiled to run only under Ztso should indicate as such via a flag in the binary, so that platforms which do not implement Ztso can simply refuse to run them." RVWMO 코드는 TSO 칩에서도 맞으니 합친 결과가 TSO 로 표시되는 건 맞는 방향이다.

### Part B — RVWMO 의 규칙과 열거기

**모델의 뼈대** (스펙 `rvwmo.adoc` "Definition of the RVWMO Memory Model" + 형식 모델 `mm-formal.adoc` 의 herd 판 `riscv.cat`): 실행 하나에는 모든 hart 의 메모리 연산을 한 줄로 세운 **전역 메모리 순서(global memory order)** 가 있어야 하고, 그 순서는 ppo 를 지켜야 하며, load 값 공리(자기 store buffer 에서 먼저 읽을 수 있음 = forwarding) · 원자성 공리 · 진행 공리를 만족해야 한다. herd 의 동치 형태가 열거기에 그대로 들어간다:

```text
acyclic co|rf|fr|po-loc as Coherence     (같은 주소끼리는 SC)
acyclic co|rfe|fr|ppo  as Model          (rfi 는 빠짐 = store forwarding 허용)
```

**ppo 13개 규칙** (a 가 po 에서 b 앞, 둘 다 주 메모리일 때 — 스펙 원문 목록 그대로 묶음):

| 묶음 | 규칙 | 뜻 (짧게) | 열거기 |
|---|---|---|---|
| 같은 주소 | 1 | b 가 store 이고 a, b 주소가 겹침 | 넣음 (`r1`) |
| | 2 | 둘 다 load, 같은 바이트, 사이에 그 주소 store 없음, **서로 다른 store 의 값을 읽음** | 넣음 (`r2`) |
| | 3 | a 가 AMO/SC, b 가 load 이고 b 가 a 가 쓴 값을 읽음 (AMO→load forwarding 금지) | 넣음 (`r3`) |
| 명시적 동기화 | 4 | 둘 사이 FENCE 가 a→b 를 순서 지음 | **TODO** (`fence_orders`) |
| | 5 | a 에 acquire 주석 | **TODO** (`annot_rule`) |
| | 6 | b 에 release 주석 | **TODO** |
| | 7 | a, b 둘 다 RCsc 주석 | **TODO** |
| | 8 | a 와 b 가 짝지은 LR/SC | 뺌 (LR/SC 없음 — RISCV-M04) |
| 문법적 의존 | 9 | b 가 a 에 주소 의존 | 뺌 |
| | 10 | b 가 a 에 데이터 의존 | 뺌 |
| | 11 | b 가 store 이고 a 에 제어 의존 | 뺌 |
| 파이프라인 의존 | 12 | b 는 load, 사이의 store m 이 a 에 주소/데이터 의존하고 b 가 m 의 값을 읽음 | 뺌 |
| | 13 | b 는 store, 사이의 m 이 a 에 주소 의존 | 뺌 |

MP/SB/LB/IRIW/2+2W 는 의존성이 없는 형태라 9–13 을 빼도 결과가 같다 (LB 의 "의존성 있으면 금지" 변형은 이 열거기로 못 본다 — 정직하게). 재미있는 부수 발견: `r1` 을 지운 돌연변이는 45개 CHECK 를 **모두 통과**했다. 이 partial-order 형태에서는 같은 주소 W→W 는 Coherence 가 강제한 `co`, R→W 는 `fr` 이 이미 Model 에 들어가서 r1 이 중복이기 때문이다 (적어도 이 테스트 범위에서는; GMO 형태 정의에선 r1 이 필요).

**주석:** AMO/LR 의 `.aq` = acquire-**RCsc**, AMO/SC 의 `.rl` = release-RCsc, `.aqrl` = 둘 다 (스펙 원문). RCpc 주석은 "현재 Ztso 의 암묵 주석으로만 쓰인다". 그래서 r7 이 핵심이다 — release store → acquire load(W→R)는 **둘 다 RCsc 일 때만** 막힌다.

**FENCE** (`rv32.adoc`): "A FENCE (with fm=0000) orders all memory operations in its predecessor set before all memory operations in its successor set." `fence.tso` = fm=1000, pred=succ=RW: "orders all load operations in its predecessor set before all memory operations in its successor set, and all store operations in its predecessor set before all store operations in its successor set. This leaves non-AMO store operations … unordered with non-AMO loads in its successor set." 그리고 "it is correct to ignore the fm field and implement FENCE.TSO as FENCE RW,RW" — 이 문장이 아래 QEMU 결과의 열쇠다. 스펙 해설은 pred/succ 4비트 조합 중 "nine non-trivial combinations … plus FENCE.TSO", 실무에서 흔한 것 6개(`rw,rw`, `.tso`, `rw,w`, `r,rw`, `r,r`, `w,w`)를 꼽는다.

**열거기 설계** (`solution.py`): 위치마다 store 들의 co 순서를 전부(순열), load 마다 rf 후보를 전부 고르고, AMO 는 원자성 공리대로 "co 바로 앞 store 를 읽는다"로 고정. 각 후보 실행에서 모델의 관계 합집합에 cycle 이 있으면 버린다. 4개 모델:

| 모델 | 정의 (열거기) |
|---|---|
| SC | acyclic(po ∪ rf ∪ co ∪ fr) |
| RVWMO | Coherence ∧ acyclic(co ∪ rfe ∪ fr ∪ ppo[r1–r7]) |
| TSO(Ztso) | RVWMO 와 같은 식, `ann_ztso` 로 load=acq-RCpc / store=rel-RCpc / AMO=RCsc (스펙 Ztso 절 그대로) |
| ARMv8-lite | Coherence ∧ acyclic(rfe ∪ coe ∪ fre ∪ bob), bob = DMB · [A\|Q];po · po;[L] · [L];po;[A] (aarch64.cat 단순화, dob/aob 뺌) |
| (검증용) x86-TSO | Coherence ∧ acyclic(co ∪ rfe ∪ fr ∪ (po − W→R) ∪ mfence) — Sewell et al. / herding cats 식, Ztso 와 독립 |

ARMv8 열의 RISC-V fence 해석은 "그 순서를 덮는 가장 싼 DMB": `r,*` → `DMB ISHLD`, `w,w` → `DMB ISHST`, `fence.tso` → `ISHLD + ISHST`, W→R 이 끼면 `DMB ISH`. 그래서 `fence rw,w` 는 ARM 에서 `DMB ISH` 가 되어 SB 까지 막는다 — 표의 `SB+fence rw,w = FAAF` 가 그것 (ARM 에선 보통 `STLR` 로 옮긴다).

**결과 표** (`python3 -I -W error solution.py --table` 실제 출력):

```text
variant              SC         TSO(Ztso)  RVWMO      ARMv8-lite
MP                   F(3)       F(3)       A(4)       A(4)
MP+fence rw,rw       F(3)       F(3)       F(3)       F(3)
MP+fence w,w/r,r     F(3)       F(3)       F(3)       F(3)
MP+fence.tso         F(3)       F(3)       F(3)       F(3)
MP+w,w only          F(3)       F(3)       A(4)       A(4)
MP+r,r only          F(3)       F(3)       A(4)       A(4)
MP+amo.rl/amo.aq     F(3)       F(3)       F(3)       F(3)
MP+amo.rl only       F(3)       F(3)       A(4)       A(4)
SB                   F(3)       A(4)       A(4)       A(4)
SB+fence rw,rw       F(3)       F(3)       F(3)       F(3)
SB+fence w,r         F(3)       F(3)       F(3)       F(3)
SB+fence.tso         F(3)       A(4)       A(4)       A(4)
SB+fence r,rw        F(3)       A(4)       A(4)       A(4)
SB+fence rw,w        F(3)       A(4)       A(4)       F(3)
SB+amoswap.rl        F(3)       F(3)       A(4)       A(4)
SB+amoswap.aqrl      F(3)       F(3)       F(3)       F(3)
SB+sw.rl/lw.aq       F(3)       F(3)       F(3)       F(3)
SB+sw.rl/lw.aqpc     F(3)       A(4)       A(4)       A(4)
LB                   F(3)       F(3)       A(4)       A(4)
LB+fence rw,rw       F(3)       F(3)       F(3)       F(3)
LB+fence r,w         F(3)       F(3)       F(3)       F(3)
LB+fence.tso         F(3)       F(3)       F(3)       F(3)
```

괄호는 허용 결과 개수 (두 load 의 값 조합 4개 중). 읽는 법 — 면접 답이 그대로 들어 있다:

- **RVWMO 는 fence 없으면 MP · SB · LB 셋 다 허용**, `fence rw,rw` 는 셋 다 금지. TSO 는 SB 만 허용 (store buffer).
- **`fence.tso` 는 MP · LB 는 막고 SB 는 못 막는다** — 정의가 W→R 을 비워 두니까. SB 의 최소 처방은 `fence w,r` (연습 2 에서 전수 탐색으로 확인).
- `.rl` 만 붙인 AMO 는 **앞**만 막는다: `MP+amo.rl only`(reader 가 plain) 허용, `SB+amoswap.rl` 허용. `.aqrl` 이면 AMO 뒤 load 도 못 넘어와 SB 금지. Ztso 열에서 `SB+amoswap.rl` 이 F 인 이유 = Ztso 에서 AMO 는 무조건 RCsc 양방향 ("no memory operation will be reordered past an AMO in either direction").
- **RCsc vs RCpc:** `sw.rl; lw.aq` (둘 다 RCsc, r7) 금지, `sw.rl; lw.aqpc` 허용. ARM-M05 가 M6 에서 실측한 "SB 에서 `stlr;ldapr` 만 재배치, `stlr;ldar` 는 0" 과 같은 구분이다.
- RVWMO 와 ARMv8-lite 는 `fence rw,w` (ARM 에 같은 배리어가 없음) 한 줄 빼고 같다 — 의존성 없는 테스트에선 두 모델이 매우 가깝다.

**금지 이유 = cycle** (실제 출력):

```text
MP+fence w,w/r,r   T0:sw x 1 -r4 fence w,w-> T0:sw y 1 -rfe-> T1:lw r0 y -r4 fence r,r-> T1:lw r1 x -fr-> T0:sw x 1
SB+fence rw,rw     T0:sw x 1 -r4 fence rw,rw-> T0:lw r0 y -fr-> T1:sw y 1 -r4 fence rw,rw-> T1:lw r1 x -fr-> T0:sw x 1
SB+sw.rl/lw.aq     T0:sw.rl x 1 -r7 RCsc-> T0:lw.aq r0 y -fr-> T1:sw.rl y 1 -r7 RCsc-> T1:lw.aq r1 x -fr-> T0:sw.rl x 1
LB+fence.tso       T1:sw x 1 -rfe-> T0:lw r0 x -r4 fence.tso-> T0:sw y 1 -rfe-> T1:lw r1 y -r4 fence.tso-> T1:sw x 1

== 허용 이유 (RVWMO): SB+fence.tso 의 weird 실행 하나 — cycle 이 없다
  ppo = (없음 — fence.tso 는 store→load 를 잡지 않는다)
  fr  = T0:lw r0 y->T1:sw y 1, T1:lw r1 x->T0:sw x 1
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="10" y="20" fill="currentColor" font-weight="bold">SB 의 weird 실행 (r0 = r1 = 0): fence rw,rw 면 cycle, fence.tso 면 cycle 없음</text>
  <text x="60" y="50" fill="currentColor">T0</text><text x="290" y="50" fill="currentColor">T1</text>
  <rect x="20" y="62" width="150" height="32" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="34" y="83" fill="currentColor" font-family="monospace">(a) sw x 1</text>
  <rect x="20" y="182" width="150" height="32" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="34" y="203" fill="currentColor" font-family="monospace">(b) lw r0 y = 0</text>
  <rect x="250" y="62" width="150" height="32" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="264" y="83" fill="currentColor" font-family="monospace">(c) sw y 1</text>
  <rect x="250" y="182" width="150" height="32" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="264" y="203" fill="currentColor" font-family="monospace">(d) lw r1 x = 0</text>
  <line x1="95" y1="94" x2="95" y2="180" stroke="currentColor" stroke-width="2" marker-end="url(#ah4)"/>
  <line x1="325" y1="94" x2="325" y2="180" stroke="currentColor" stroke-width="2" marker-end="url(#ah4)"/>
  <text x="102" y="142" fill="currentColor">r4 (fence)</text><text x="332" y="142" fill="currentColor">r4 (fence)</text>
  <line x1="170" y1="190" x2="250" y2="90" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah4)"/>
  <line x1="250" y1="190" x2="170" y2="90" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah4)"/>
  <text x="186" y="128" fill="currentColor">fr</text><text x="222" y="128" fill="currentColor">fr</text>
  <rect x="440" y="62" width="290" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="452" y="84" fill="currentColor">fence rw,rw: a→b, c→d 가 ppo</text>
  <text x="452" y="104" fill="currentColor">a →ppo b →fr c →ppo d →fr a = cycle</text>
  <text x="452" y="124" fill="currentColor">→ 어떤 전역 순서도 불가 → 금지</text>
  <rect x="440" y="150" width="290" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="452" y="172" fill="currentColor">fence.tso: W→R 은 안 묶음</text>
  <text x="452" y="192" fill="currentColor">세로 화살표가 사라짐 → fr 두 개뿐</text>
  <text x="452" y="212" fill="currentColor">전역 순서 b, d, a, c 가능 → 허용</text>
  <text x="20" y="250" fill="currentColor">하드웨어 그림: a 와 c 가 각자 store buffer 에 머무는 동안 b, d 가 메모리의 옛 값 0 을 읽는다.</text>
  <text x="20" y="272" fill="currentColor">같은 그림이 x86 · Ztso · ARMv8 · RVWMO 모두에서 허용 — SB 는 "W→R 을 막는 것" 만 고친다.</text>
</svg>
```

**Ztso == x86-TSO**: 주석 없는 16개 변형 전부에서 Ztso(RVWMO + 암묵 주석)와 독립 x86-TSO 의 **허용 결과 집합 전체**가 같았다 (CHECK). 스펙의 "Ztso = RVWMO 의 델타" 정의가 x86-TSO 와 같은 것을 만든다는 걸 열거로 확인한 셈이다.

### Part C — C11 → RVWMO 끝까지, 그리고 매핑 섞기

C11 리트머스를 `lower()` 로 RISC-V 미니 어셈블리로 내린다 (스레드마다 다른 매핑 함수를 줄 수 있다 = 다른 컴파일러/ABI 로 만든 코드를 링크한 상황). 결과 (CHECK, 전부 PASS):

| C11 리트머스 | 매핑 | 모델 | weird | C11 기대 |
|---|---|---|---|---|
| MP (flag release / acquire) | 스펙 | RVWMO | 금지 | 금지 ✔ |
| MP (relaxed / relaxed) | 스펙 | RVWMO | 허용 | 허용 ✔ |
| SB (seq_cst) | 스펙 | RVWMO | 금지 | 금지 ✔ |
| SB (release / acquire) | 스펙 | RVWMO | 허용 | 허용 ✔ (C11 도 SB 를 acq/rel 로는 못 막음) |
| SB (xchg seq_cst 를 store 로) | 컴파일러 | RVWMO | 금지 | 금지 ✔ |
| **SB seq_cst: 스펙 store + Zalasr load** | 섞음 | RVWMO | **허용** | 금지 ✘ — **매핑 섞기 버그** |
| SB seq_cst: 컴파일러 store(뒤 fence) + Zalasr load | 섞음 | RVWMO | 금지 | 금지 ✔ |
| SB seq_cst: Zalasr 만 (`sw.rl` / `lw.aq`) | 가상 표 | RVWMO | 금지 | 금지 ✔ (r7 RCsc) |
| MP rel/acq, SB seq_cst | Ztso 컴파일러 | Ztso | 금지 | 금지 ✔ |

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">매핑을 섞으면: 같은 hart 에서 seq_cst store(라이브러리 A) 다음 seq_cst load(라이브러리 B)</text>
  <rect x="20" y="40" width="330" height="90" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="32" y="62" fill="currentColor">A.6 표로 컴파일된 TU:  fence rw,w ; sw x</text>
  <text x="32" y="84" fill="currentColor">Zalasr 표로 컴파일된 TU: lw.aq y</text>
  <text x="32" y="106" fill="currentColor">sw → lw.aq 사이 W→R 을 막는 것 없음</text>
  <text x="32" y="124" fill="currentColor">(sw 는 주석 없음 → r7 아님)  → SB weird 허용</text>
  <rect x="390" y="40" width="330" height="90" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="402" y="62" fill="currentColor">clang 21 / gcc 15 실제 출력:</text>
  <text x="402" y="84" fill="currentColor">fence rw,w ; sw x ; fence rw,rw</text>
  <text x="402" y="106" fill="currentColor">뒤 fence 가 W→R 을 막음 (r4)</text>
  <text x="402" y="124" fill="currentColor">→ 누구와 링크해도 SB 금지</text>
  <rect x="20" y="150" width="700" height="80" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="172" fill="currentColor">펌웨어 현실: BSP/벤더 정적 라이브러리(.a), RTOS 커널, 앱이 서로 다른 툴체인 버전으로 빌드된다.</text>
  <text x="32" y="194" fill="currentColor">"각자 맞는 매핑" 이어도 섞이면 틀릴 수 있다 → 매핑 규칙은 ABI 의 일부다.</text>
  <text x="32" y="216" fill="currentColor">Ztso 바이너리는 ELF 플래그(TSO)로 표시 · 링크 시 전염 → RVWMO 칩은 실행 거부해야 한다 (연습 4).</text>
</svg>
```

펌웨어 · 검증 연결:

- **MMIO 와 DMA**: 위 열거기는 주 메모리(R/W)만 다룬다. FENCE 에는 I/O 비트(`i`/`o`)도 있다. 스펙 Linux 매핑 표: `mb()` = `fence iorw,iorw`, `rmb()` = `fence ri,ri`, `wmb()` = `fence wo,wo`, `dma_rmb()`/`dma_wmb()` = `fence r,r` / `fence w,w` — "RISC-V Unix Platform requires coherent DMA" 라서. 비일관(non-coherent) DMA 플랫폼이면 `fence ri,ri` / `fence wo,wo` + 장치별 캐시 flush/invalidate 가 필요하다 (스펙 문장; 캐시 관리 순서는 ARCH-M12). DMA 디스크립터를 쓰고 도어벨 MMIO 를 칠 때 `fence w,o` 류가 필요한 이유가 이것이다.
- **단일 hart MCU** (ESP32-C3, GD32VF103 등): 자기 자신에게는 program order 가 보이므로(load 값 공리의 forwarding 포함) 일반 메모리끼리의 재배치는 ISR ↔ main 사이에서도 문제가 안 된다 — 문제는 DMA 마스터 · 다른 hart · 장치 레지스터 쪽이다 (C-M14).
- **실리콘 검증**: 스펙은 "a suite of more than seven thousand litmus tests is available at github.com/litmus-tests/litmus-tests-riscv" 라고 하고, herd 모델 `riscv.cat` 이 기준 답을 낸다 (herd7/litmus7 은 이 맥에 미설치 — 인용만). 이 레슨의 열거기는 그 축소판: 테스트마다 "허용 집합" 을 만들고 실칩 관찰이 그 **부분집합**인지 본다. 관찰이 허용 집합 밖이면 칩 버그, 허용인데 한 번도 안 보이면 "구현이 더 강할 뿐" (아래 QEMU).

### 선택: QEMU 에서 SB 를 실제로 돌리면 — `demo_sb_qemu.c`

```bash
# VM 안 (aarch64 호스트, qemu-riscv64 10.2.1 user mode — 게스트 스레드 = 호스트 스레드)
riscv64-linux-gnu-gcc -std=c11 -Wall -Wextra -Werror -O2 -static -pthread demo_sb_qemu.c -o sb_rv
riscv64-linux-gnu-objdump -d sb_rv | grep fence   # 8330000f fence.tso · 0230000f fence r,rw · 0120000f fence w,r · 0330000f fence rw,rw 확인
qemu-riscv64 ./sb_rv 200000
```

실제 출력 (3회 중 1·3회째, VM load 0.12 / 맥 load 6.8 — 다른 세션 벤치마크와 동시 실행, **비율만** 볼 것):

```text
plain sw;lw  N=200000  (r0,r1)=(0,0):75140  (0,1):70061  (1,0):54771  (1,1):28
fence.tso    N=200000  (r0,r1)=(0,0):0  (0,1):85415  (1,0):66173  (1,1):48412
fence r,rw   N=200000  (r0,r1)=(0,0):0  (0,1):95171  (1,0):90641  (1,1):14188
fence w,r    N=200000  (r0,r1)=(0,0):0  (0,1):91794  (1,0):88879  (1,1):19327
fence rw,rw  N=200000  (r0,r1)=(0,0):0  (0,1):84786  (1,0):78129  (1,1):37085

plain sw;lw  N=200000  (r0,r1)=(0,0):108639  (0,1):58392  (1,0):32898  (1,1):71
fence.tso    N=200000  (r0,r1)=(0,0):0  (0,1):87417  (1,0):80661  (1,1):31922
…(fence r,rw / w,r / rw,rw 모두 (0,0):0)
```

- plain 은 weird (0,0) 이 **35–54%** (세 번 실행 75140 · 69264 · 108639 / 200000; 맨 처음 4변형판 실행은 7%: 14515) — 게스트 `sw`/`lw` 가 arm64 호스트의 일반 str/ldr 로 번역되고, 호스트(arm64)가 SB 를 허용하니까 그대로 드러난다.
- `fence.tso` 와 `fence r,rw` 는 **스펙상 SB 를 허용**하는데 QEMU 에선 0. 이유를 QEMU v10.2.0 소스에서 확인했다 (`target/riscv/insn_trans/trans_rvi.c.inc`):

```c
static bool trans_fence(DisasContext *ctx, arg_fence *a)
{
    /* FENCE is a full memory barrier. */
    tcg_gen_mb(TCG_MO_ALL | TCG_BAR_SC);
    return true;
}
```

  pred/succ · fm 을 무시하고 모든 FENCE 를 전체 배리어로 — 스펙이 허락한 "더 강한 구현" 이다. 교훈: **QEMU 로 fence 가 충분한지 검증하면 안 된다** (약한 fence 로도 통과해 버림). RISCV-M04 가 본 "QEMU SC 는 값 비교" 와 같은 종류의 함정이다. 비교: ARCH-A05 (M6 실하드웨어 + Rosetta TSO), OS-A03 의 demo_sb (relaxed 7–12만 / acq-rel 2/12M / seq_cst 0).

## 6. 정답 코드

<!-- code: solution.py diff=starter.py -->

```bash
python3 -I -W error solution.py ; echo "exit=$?"     # 45/45 passed, exit=0
python3 -I -W error -X dev solution.py               # 경고 0
python3 -I solution.py --table | shasum              # 두 번 실행 → 같은 해시 (결정적)
```

바뀐 곳은 세 함수뿐이다: `fence_orders` (pred/succ 곱 + fence.tso), `annot_rule` (r5/r6/r7), `lower_spec` (스펙 표). 실행 시간 0.13 s (Python 3.9.6).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 것 |
|---|---|---|
| "`fence.tso` = 가벼운 full fence" 로 SB(Dekker, 피터슨 락) 고치기 | 정의상 store→load 를 안 묶는다. 표: `SB+fence.tso` RVWMO **A** | `fence rw,rw` (또는 최소 `fence w,r`) |
| acquire 를 load **앞** fence 로 (`fence r,rw; lw`) | acquire 는 이 load 와 그 뒤의 순서다. 앞 fence 는 이전 load 와의 순서만 만든다 | `lw; fence r,rw` |
| release 를 store **뒤** fence 로 (`sw; fence rw,w`) | 앞의 데이터 store 가 flag 뒤로 밀릴 수 있다 | `fence rw,w; sw` |
| `amoswap.rl` 로 lock 잡기 | `.rl` 은 앞만 막는다 → 임계 구역 load 가 lock 획득 전으로 샘 (`MP+amo.rl only` = 허용) | 획득 `.aq`, 해제 `.rl` (RISCV-M04) |
| `.aq` 와 `.rl` 이 있으니 SB 도 막힌다고 생각 | W.rl → R.aq 는 **둘 다 RCsc** 일 때만 (r7). RCpc 면 허용 (`SB+sw.rl/lw.aqpc`) | seq_cst 는 `.aqrl` 또는 fence rw,rw |
| QEMU 에서 0 이니 fence 충분 | QEMU 는 모든 FENCE 를 full barrier 로 번역 (소스 확인) | 열거기 / herd 로 허용 집합 계산, 실칩에서 측정 |
| 툴체인이 달라도 atomics 는 섞어 링크해도 된다 | 매핑은 ABI. A.6 store + Zalasr load = SB 깨짐 | 컴파일러의 seq_cst store 뒤 `fence rw,rw` 유지, 같은 ABI 로 빌드 |
| `-march=..._ztso` 로 빌드해 RVWMO 보드에 올림 | fence 가 사라진 코드 → MP 깨짐 (연습 4) | ELF TSO 플래그 확인 (`readelf -h`), 로더가 거부 |
| `atomic_signal_fence` 로 코어 간 순서 확보 | 명령 0개 (실측) — 컴파일러만 막음 | `atomic_thread_fence` (C-A02) |

## 8. 면접 Q&A

<details><summary>Q1. C11 seq_cst load/store 는 RISC-V 에서 무슨 명령이 되나요? 컴파일러마다 다른가요?</summary>

스펙 부록 A 표: load = `fence rw,rw; l; fence r,rw`, store = `fence rw,w; s`. 실측(clang 21.1.8 · gcc 15.2.0, `-O2`): load 는 표 그대로, store 는 둘 다 `fence rw,w; sw; fence rw,rw` — **뒤 fence 가 하나 더**. 두 컴파일러는 원자 연산 부분이 한 글자도 안 달랐다. 꼬리: "그 뒤 fence 는 왜?" → 스펙 본문이 경고하듯, 나중에 load-acquire 명령(Zalasr)으로 seq_cst load 를 `l.aq` 하나로 만드는 코드와 섞이면 A.6 store 와 그 load 사이 W→R 이 풀린다. 뒤 fence 가 있으면 어떤 매핑과 섞여도 안전. LLVM 엔 끄는 옵션 `no-trailing-seq-cst-fence` 가 있고, 켜면 표 그대로로 돌아간다(실측). 내 열거기로 "섞으면 SB 허용, 뒤 fence 있으면 금지" 를 증명했다.
</details>

<details><summary>Q2. fence.tso 와 fence rw,rw 의 차이는? 언제 fence.tso 를 쓰나요?</summary>

`fence.tso` 는 R→R, R→W, W→W 를 묶고 **W→R 만 푼다** = TSO 가 원래 지키는 것. 그래서 MP · LB 는 막지만 SB 는 못 막는다 (열거 결과). 쓰는 곳: C11 `atomic_thread_fence(acq_rel)` (실측 `fence.tso`), Linux `smp_store_release()` = `fence.tso; s` (스펙 Linux 표 — `fence rw,w` 보다 R→R 이 더 있어서 임계 구역 load 가 다음 임계 구역의 load 보다 먼저 보이게 한다, 연습 5), x86 코드 포팅. 꼬리: "fence.tso 를 모르는 옛 코어는?" → 인코딩이 fm=1000 + pred=succ=RW 라서 fm 을 무시하면 `fence rw,rw` 로 동작 — 스펙이 그렇게 구현해도 된다고 명시, QEMU 가 실제로 그렇게 한다 (소스 확인).
</details>

<details><summary>Q3. RVWMO 의 preserved program order 규칙을 요약해 보세요.</summary>

po 의 a→b 가 전역 순서에서도 보존되는 경우 13가지, 네 묶음: ① 같은 주소 (b 가 겹치는 store / 같은 바이트 load 둘이 다른 store 값을 읽음 / AMO·SC 가 쓴 값을 뒤 load 가 읽음), ② 명시적 동기화 (FENCE, a 가 acquire, b 가 release, 둘 다 RCsc, LR/SC 짝), ③ 문법적 의존 (주소, 데이터, store 로의 제어 의존), ④ 파이프라인 의존 (중간 store 를 통한 forwarding, 중간 접근의 주소 의존). 공리: 이 ppo 를 지키는 전역 순서 + load 값 공리(자기 store 는 먼저 볼 수 있음) + 원자성 + 진행. 꼬리: "rule 3 은 왜?" → AMO/SC 의 값을 뒤 load 가 store buffer 에서 forwarding 받지 못하게 — 스펙 TSO 매핑 해설은 그 때문에 store 를 `amoswap.rl` 로 옮기면 성능이 나빠질 수 있다고 말한다.
</details>

<details><summary>Q4. .aq / .rl / .aqrl 은 정확히 무엇을 막나요? RCpc 와 RCsc 는?</summary>

`.aq`: 이 AMO/LR 뒤의 모든 접근이 이것보다 먼저 보이지 않음 (r5). `.rl`: 앞의 모든 접근이 이것보다 먼저 보임 (r6). AMO/LR/SC 의 주석은 RCsc 라서 `.rl` 연산 → `.aq` 연산도 순서가 선다 (r7) — 이게 RCpc(release→acquire 사이 W→R 허용)와의 차이다. 열거: `sw.rl; lw.aq` SB 금지, `sw.rl; lw.aqpc` 허용. ARM 과 같다: `STLR;LDAR` (RCsc) vs `STLR;LDAPR` (RCpc) — ARM-M05 가 M6 에서 후자만 재배치되는 걸 실측. 꼬리: "SC 에 aq 만, LR 에 rl 만?" → 스펙은 그런 조합이 더 강한 순서를 보장하지 않을 수 있다고 한다 (RISCV-M04 Q6). seq_cst CAS 가 `lr.aqrl + sc.rl` 인 이유는 표 주석: 가상 매핑과 섞일 때 LR 에 rl 까지 있어야 한다.
</details>

<details><summary>Q5. Ztso 를 켜면 컴파일 결과가 어떻게 바뀌고, 그 바이너리를 일반 RISC-V 보드에서 돌리면?</summary>

실측: acquire/release 용 fence 전부 사라지고 AMO 의 aq/rl 비트도 사라진다. 남는 건 `fence rw,rw` (seq_cst load 앞, seq_cst store 뒤, seq_cst fence)와 seq_cst CAS 의 `lr.aqrl/sc.rl`. Ztso = 모든 load acquire-RCpc · store release-RCpc · AMO RCsc 라서, PW 와 SR 이 다 있는 fence 만 의미가 있다 (스펙 노트, CHECK). 이 바이너리를 RVWMO 칩에 올리면 MP 가 깨진다 (연습 4: Ztso 컴파일 MP rel/acq → RVWMO 에서 weird 허용). 그래서 ELF 헤더에 TSO 플래그(실측 0x15 = RVC | TSO | double-float)가 붙고, RVWMO 오브젝트와 링크해도 결과가 TSO 로 표시된다 (ld.bfd · ld.lld 실측) → 플랫폼이 실행을 거부해야 한다.
</details>

<details><summary>Q6. 메모리 모델 구현을 실리콘에서 어떻게 검증하나요?</summary>

(1) 리트머스: litmus-tests-riscv 의 수천 개 테스트를 하드웨어에서 수백만 번 돌리고(litmus7 방식, ARCH-A05), 관찰 결과 집합이 모델(herd7 + riscv.cat)의 허용 집합 **안**인지 확인 — 밖이면 버그. (2) 허용인데 안 보이는 결과는 버그가 아니다 (구현이 더 강할 뿐) — 이 레슨의 QEMU 처럼 모든 fence 를 full 로 만드는 구현도 합법. 커버리지가 걱정이면 스트레스(캐시 축출, 인터럽트, 다른 마스터 트래픽)로 약한 동작을 끌어낸다. (3) 시뮬레이션 단계에선 RTL 에 SVA 를 걸거나 운영적 모델(스펙에 있는 operational model)과 트레이스를 비교. 꼬리: "내 열거기와 herd 의 차이?" → 의존성(r9–r13), LR/SC 짝(r8), 혼합 크기, I/O 를 뺐다 — 그 테스트들에선 답이 틀릴 수 있다.
</details>

<details><summary>Q7. RVWMO 는 ARMv8 과 같은가요? x86 코드 포팅 시 주의점은?</summary>

둘 다 다중 복사 원자적이고, 이 레슨의 의존성 없는 테스트에선 결과가 같았다 (ARM 에 `fence rw,w` 같은 배리어가 없어 DMB ISH 로 올린 한 줄 빼고). 차이는 배리어 종류(RISC-V 는 pred/succ 조합 + fence.tso, ARM 은 DMB ISH/ISHLD/ISHST + LDAR/STLR/LDAPR 명령)와 의존성 규칙의 세부(확인 필요). x86 포팅: 스펙 TSO 매핑 = load 마다 `fence r,rw`, store 마다 `fence rw,w`, LOCK RMW = `amo.aqrl`, MFENCE = `fence rw,rw` — 연습 3 에서 이 매핑이 x86-TSO 의 허용 집합을 정확히 재현함을 확인. 칩에 Ztso 가 있으면 fence 없이 그대로.
</details>

## 9. 추가 연습

<details><summary>연습 1 — IRIW 와 2+2W: 다중 복사 원자성</summary>

IRIW: T0 `sw x 1`, T1 `sw y 1`, T2 `lw x; lw y`, T3 `lw y; lw x`. 두 reader 가 두 store 를 반대 순서로 보는 결과(1,0,1,0)는 RVWMO 에서 fence 없으면 허용(load 끼리 재배치), reader 에 `fence r,r` 을 넣으면 **금지** — 전역 순서 하나가 있는 모델(MCA)이라서 (`FFFF`). 2+2W (`sw x 1; sw y 2` ‖ `sw y 1; sw x 2`, 최종 x=1 y=1): fence 없으면 RVWMO 허용, `fence w,w` 로 금지, cycle = r4, co, r4, co (실제 출력 PASS). TSO 는 둘 다 처음부터 금지.
</details>

<details><summary>연습 2 — 최소 fence 처방 찾기 (전수 탐색)</summary>

fence 옵션 11개(없음 + fence.tso + pred/succ 9조합) × 2 스레드 = 121 조합을 MP/SB/LB 에 넣어 RVWMO 에서 weird 를 막는 조합을 모으고, "순서 쌍 집합이 더 작은 처방이 없는 것" 만 남긴다. 실측: MP → `fence w,w` / `fence r,r` (막는 조합 25개 중 최소 1개), SB → `fence w,r` / `fence w,r` (16개 중), LB → `fence r,w` / `fence r,w` (25개 중). 그리고 10개 non-trivial fence 의 순서 쌍 집합이 모두 다르고, `fence.tso` = {RR, RW, WW} 임을 CHECK. `fence w,r` 은 합법이지만 C11 매핑에는 안 나온다 — SB 를 고치는 데 쓰는 seq_cst 매핑이 더 많은 걸 막아야 하기 때문.
</details>

<details><summary>연습 3 — 스펙의 TSO → RVWMO 매핑 표가 x86-TSO 를 정확히 재현하나</summary>

`lower_tso`: load → `lw; fence r,rw`, store → `fence rw,w; sw`, MFENCE → `fence rw,rw`. MP · SB · SB+mfence · LB · 2+2W · IRIW 각각에서 RVWMO(변환본)의 허용 집합 == x86-TSO(원본) 허용 집합 (3, 4, 3, 3, 3, 15 개) — 6개 모두 PASS. 부족하지도(약하게 만들지도) 넘치지도(SB 를 막아 버리지도) 않는다.
</details>

<details><summary>연습 4 — Ztso 로 컴파일한 MP 를 RVWMO 칩에서</summary>

C11 MP(release/acquire)를 실측 Ztso 매핑으로 내리면 fence 가 하나도 없는 `sw; sw` / `lw; lw` 가 된다. Ztso 모델에선 금지, RVWMO 모델에선 **허용** → 바이너리에 TSO 플래그가 필요한 이유. 반면 SB seq_cst 는 Ztso 매핑에도 `fence rw,rw` 가 남아 RVWMO 에서도 금지 — "어떤 건 우연히 맞는다" 는 게 더 위험하다 (테스트가 통과해 버림).
</details>

<details><summary>연습 5 — Linux smp_store_release = fence.tso; s</summary>

MP 를 writer `fence.tso; sw flag`, reader `lw flag; fence r,rw` (smp_load_acquire)로 짜면 RVWMO 금지. `fence.tso` − `fence rw,w` = {RR}: 앞의 load 를 뒤의 load 보다도 먼저 보이게 한다. 스펙의 Linux 예(임계 구역의 load (a) → unlock → 다음 lock → load (e))에서 (a)→(e) 순서가 필요할 수 있어서 release 매핑을 강화했다는 설명과 일치. ppo_rule 로 직접: `lw; fence rw,w; lw` 는 순서 없음, `lw; fence.tso; lw` 는 `r4 fence.tso`.
</details>

### 한 번에 검증

<!-- code: practice.py -->

```bash
python3 -I -W error practice.py ; echo "exit=$?"     # 24/24 passed, exit=0
python3 -I -W error -X dev practice.py               # 경고 0
```

실제 출력 (일부):

```text
PASS ex1 IRIW            SC/TSO/RVWMO/ARMv8 = FFAA
PASS ex1 IRIW+fence r,r  = FFFF (RVWMO is multi-copy atomic)
PASS ex2 MP minimal fix = fence w,w / fence r,r (of 25 fixing pairs)
PASS ex2 SB minimal fix = fence w,r / fence w,r (of 16 fixing pairs)
PASS ex2 LB minimal fix = fence r,w / fence r,w (of 25 fixing pairs)
PASS ex3 IRIW      RVWMO(tso-mapped) == x86-TSO (15 outcomes)
PASS ex4 Ztso-compiled MP rel/acq: ALLOWED on RVWMO (must refuse to run)
PASS ex5 fence.tso - fence rw,w = {RR} (earlier loads before later loads)
24/24 passed
```

## 10. 핵심 정리

- RVWMO = 전역 메모리 순서 + ppo 13규칙(같은 주소 · fence/aq/rl/RCsc/LR-SC · 의존성 · 파이프라인) + load 값/원자성/진행 공리. herd: `acyclic co|rf|fr|po-loc`, `acyclic co|rfe|fr|ppo`.
- 의존성·fence 없으면 MP · SB · LB 전부 허용. `fence rw,rw` 는 셋 다 금지. 최소 처방: MP `w,w`+`r,r`, SB `w,r`, LB `r,w`.
- `fence.tso` = R→R, R→W, W→W (W→R 만 풂) → MP · LB 막고 **SB 못 막음**. fm 무시하고 `fence rw,rw` 로 구현해도 합법 (QEMU 가 그렇게 함 — 모든 FENCE = full).
- `.aq` 뒤를, `.rl` 앞을 막는다. AMO/LR/SC 주석은 RCsc → `.rl` → `.aq` 도 막힘. RCpc 는 Ztso 암묵 주석에만.
- C11 매핑(스펙 = clang 21 = gcc 15): acquire = `l; fence r,rw`, release = `fence rw,w; s`, seq_cst load = `fence rw,rw; l; fence r,rw`, RMW = `amo.{aq,rl,aqrl}`, CAS = `lr.aq(rl)/sc.rl`, acq_rel fence = `fence.tso`.
- 단 하나 다른 칸: seq_cst store 뒤 `fence rw,rw` — 미래 Zalasr(`l.aq`) 코드와 섞여도 SB 가 안 깨지게. 매핑은 ABI 다.
- Ztso: aq/rl·약한 fence 사라지고 `fence rw,rw` 만 남음, ELF TSO 플래그(링크 시 전염) → RVWMO 칩은 거부.
- 허용 ≠ 관찰: QEMU SB plain 은 7–54% weird, `fence.tso` 는 0 (스펙상 허용). 검증 = "관찰 ⊆ 허용" 확인.

## 참고자료

- RISC-V Unprivileged ISA — riscv-isa-manual GitHub main (마지막 커밋 c92615e735, 2026-10-02; 2026-11-08 에 받아 읽음): `src/unpriv/rvwmo.adoc` (RVWMO 정의, Preserved Program Order 13규칙, 공리), `src/unpriv/mm-explanatory.adoc` (부록 A "RVWMO Explanatory Material": Litmus Tests, "Mappings from C/C++ primitives to RISC-V primitives" 표와 가상(load-acquire/store-release) 표, TSO/ARM/Linux 매핑 표), `src/unpriv/mm-formal.adoc` (herd `riscv-defs.cat` / `riscv.cat`), `src/unpriv/ztso.adoc` (Ztso), `src/unpriv/rv32.adoc` (FENCE, fm 표, FENCE.TSO). 비준판 20240411 의 장 번호는 확인 필요. https://github.com/riscv/riscv-isa-manual
- RISC-V litmus 테스트 모음 — https://github.com/litmus-tests/litmus-tests-riscv (스펙이 인용, 미실행)
- J. Alglave, L. Maranget, M. Tautschnig, "Herding Cats: Modelling, Simulation, Testing, and Data Mining for Weak Memory", TOPLAS 2014 — 후보 실행 + acyclicity 방식, x86-TSO/ARM cat 모델
- C. Pulte, S. Flur, W. Deacon, J. French, S. Sarkar, P. Sewell, "Simplifying ARM Concurrency: Multicopy-atomic Axiomatic and Operational Models for ARMv8", POPL 2018 — ARMv8-lite 의 obs/bob 출처
- P. Sewell, S. Sarkar, S. Owens, F. Zappa Nardelli, M. Myreen, "x86-TSO: A Rigorous and Usable Programmer's Model for x86 Multiprocessors", CACM 2010
- QEMU v10.2.0 `target/riscv/insn_trans/trans_rvi.c.inc` `trans_fence` (gitlab.com/qemu-project/qemu, 2026-11-08 확인)
- LLVM 21 `llc -mattr=help` (`no-trailing-seq-cst-fence`, `ztso`), clang 21 `-print-supported-extensions` (ztso 1.0, zalasr 0.1 실험적, zacas 1.0, zabha 1.0)
- 관련 레슨: RISCV-M04 (LR/SC · AMO 인코딩 · SC ISS · rv32ia 매핑 일부), MEM-M07 (SC/TSO/ARM 열거기), ARCH-A05 (M6 리트머스 + Rosetta TSO), ARM-M05 (DMB/LDAR/STLR, RCsc vs RCpc 실측), ARM-M04, C-A02 · C-A03 (C11 메모리 순서), MCPP-M13 (fence 규칙), OS-A03 (demo_sb), ARCH-M12 (non-coherent DMA), C-M14 (ISR 공유 변수)
