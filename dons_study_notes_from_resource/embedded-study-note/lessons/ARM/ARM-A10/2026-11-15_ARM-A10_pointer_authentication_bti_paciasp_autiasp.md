---
id: ARM-A10
time: 120분
updated: 2026-11-15
---
# ARM-A10 포인터 인증과 BTI: PACIASP, AUTIASP

## 학습 목표

- `-mbranch-protection=standard` / `pac-ret+leaf` / `pac-ret+b-key` / `pac-ret+pc` 와 `-arch arm64e` 로 같은 C 함수를 컴파일하고, **PACIASP·AUTIASP·RETAA·BTI c 가 어느 함수의 어디에 들어가는지**(리프·비리프·축소 포장된 빠른 경로·꼬리 호출·간접 호출) 실제 디스어셈블리로 설명한다.
- 자기 프레임의 저장된 LR 을 바꾸는 장난감 프로그램으로 **PAC 이 위조 반환 주소를 막는 것**(Linux VM: SIGILL, Mac arm64e: SIGBUS)과 **막지 못하는 것**(같은 SP 에서의 재사용)을 실제 하드웨어에서 재현한다.
- BTI 착지 패드(`bti`/`bti c`/`bti j`/`bti jc`/`paciasp`) × 간접 분기 종류(BLR / BR x16 / BR x9) 18칸 표를 실제 하드웨어로 채우고, 그 표가 BTYPE 규칙과 일치함을 C 로 검증한다. GNU property 노트 하나가 빠지면 BTI 가 통째로 꺼지는 것도 본다.
- 원시 명령 워드만 보고 "모든 비리프 함수가 LR 에 서명하고 모든 반환이 인증되는가" 를 판정하는 **분기 보호 린터**(CFG 데이터 흐름)를 짜서 실제 컴파일러 출력 45개 함수와 주입한 버그에 돌린다.
- 장난감 PAC 모델로 PAC 필드 폭(실측 7 / 8 / 16 비트), 키·modifier(SP) 의 역할, 무차별 대입 비용을 숫자로 말한다.

## 1. 문제

"PAC 이 ROP 를 어떻게 막아요? 그럼 뚫리는 경우는요?" — Apple(arm64e), Android/Linux(arm64 PAC-RET+BTI), 자동차·방산 SoC 보안 리뷰에서 나오는 스태프급 질문이다. 펌웨어 쪽에서도 Cortex-M85(Armv8.1-M PACBTI)부터 같은 개념이 들어왔다. 말로만 알면 꼬리질문("SP modifier 의 약점은?", "BTI 는 왜 바이너리 전체가 켜져야 해?", "fork 서버면 왜 위험해?")에서 막힌다. 이 레슨은 **전부 실측**으로 답한다.

이 레슨에서 실제로 관찰한 것 (모두 자기 프로세스 안 장난감 프로그램, fork 한 자식에서만 위반):

| 어디서 | 빌드 | 저장 LR 을 `&landing` 생 주소로 덮음 | 같은 SP 에서 남의 서명 LR 재사용 | 다른 SP 에서 재사용 |
|---|---|---|---|---|
| Mac (M6, macOS 27.2) | `cc -O2` (arm64) | `landing()` 도착, exit 42 | 성공, exit 43 | 성공, exit 43 |
| Mac | `-mbranch-protection=standard` (arm64) | **도착 (막지 못함)** — arm64 프로세스는 PAC 키가 꺼져 `paciasp` 가 NOP | 성공 | 성공 |
| Mac | `-arch arm64e` | **SIGBUS** (PAC 실패) | **성공** | SIGBUS |
| Linux VM (Lima, CPU 가 `paca pacg bti` 노출) | `gcc -mbranch-protection=none` | 도착, exit 42 | 성공 | 성공 |
| Linux VM | `gcc -mbranch-protection=standard` | **SIGILL** (FEAT_FPAC: `autiasp` 자체가 예외) — 1000번 중 8번은 우연 통과 | **성공** | SIGILL — 1000번 중 12번은 우연 통과 |

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">PAC-RET: 들어올 때 LR 에 서명, 나갈 때 검사 — modifier 는 SP</text>
<rect x="20" y="40" width="200" height="34" class="f-mut" stroke="currentColor"/>
<text x="30" y="62" fill="currentColor">caller 프레임 …</text>
<rect x="20" y="74" width="200" height="34" class="f-acc" stroke="currentColor"/>
<text x="30" y="96" fill="currentColor" font-family="monospace">[x29+8] 저장 LR (서명됨)</text>
<rect x="20" y="108" width="200" height="34" class="f-mut" stroke="currentColor"/>
<text x="30" y="130" fill="currentColor" font-family="monospace">[x29]   저장 x29</text>
<rect x="20" y="142" width="200" height="34" class="f-mut" stroke="currentColor"/>
<text x="30" y="164" fill="currentColor">지역 변수 (버퍼)</text>
<text x="230" y="130" fill="currentColor" font-family="monospace">← x29 = 프레임 레코드</text>
<text x="20" y="196" fill="currentColor" font-size="12">낮은 주소 ↓ (스택은 아래로 자람)</text>
<rect x="410" y="40" width="310" height="60" class="f-ok" stroke="currentColor"/>
<text x="420" y="60" fill="currentColor" font-family="monospace">paciasp  ; LR ← PAC(LR, SP, 키 IA)</text>
<text x="420" y="80" fill="currentColor" font-family="monospace">stp x29, x30, [sp, #-16]!</text>
<rect x="410" y="120" width="310" height="60" class="f-ok" stroke="currentColor"/>
<text x="420" y="140" fill="currentColor" font-family="monospace">ldp x29, x30, [sp], #16</text>
<text x="420" y="160" fill="currentColor" font-family="monospace">retaa  (= autiasp ; ret)</text>
<line x1="220" y1="91" x2="410" y2="70" stroke="currentColor"/>
<line x1="220" y1="91" x2="410" y2="150" stroke="currentColor"/>
<rect x="410" y="200" width="310" height="110" class="f-warn" stroke="currentColor"/>
<text x="420" y="220" fill="currentColor">공격자가 [x29+8] 을 바꾸면:</text>
<text x="420" y="242" fill="currentColor">① 생 주소 → PAC 불일치 → 잡힘</text>
<text x="420" y="264" fill="currentColor">② 다른 SP 의 서명값 → 잡힘</text>
<text x="420" y="286" fill="currentColor">③ 같은 SP 의 서명값 → 통과!</text>
<text x="420" y="304" fill="currentColor" font-size="12">키는 커널만 안다 · PAC 은 비트 7~16개</text>
<line x1="220" y1="91" x2="410" y2="255" stroke="currentColor" stroke-dasharray="4 3"/>
</svg>
```

### Part A — 명령 워드 디코더와 착지 패드 규칙

- `decode(uint32_t w)` — HINT 공간(`paciasp` = `hint #25`, `autiasp` = `#29`, `bti c` = `#34` …), 분기 레지스터 그룹(RET/RETAA/RETAB/BR/BLR 와 인증 변형 BRAA·BLRAAZ…), 직접 분기 오프셋(B/BL/B.cond/CBZ/TBZ), x30 저장·복원(STP/LDP/STR/LDR) 을 분류한다. 기대값은 `shared_ref.S` 를 **진짜 어셈블러**가 만든 워드로 검증한다.
- `btype_of(w, guarded)` 와 `bti_accepts(pad, btype, bt_strict)` — 간접 분기가 남기는 PSTATE.BTYPE 과 착지 패드 호환 규칙. Linux VM 에서 실측한 18칸 표와 같아야 한다.

### Part B — 분기 보호 린터 (주입한 버그를 잡는다)

- `lint_function(words, n, policy, &report)` — 0번 명령에서 출발해 함수 안 분기를 따라가며 **LR 상태(PLAIN / A 키 서명 / B 키 서명)** 를 전파한다. 위반: 패드 없음, 서명 안 된 LR 저장, 인증 없는 RET, 서명 안 된 LR 인증, 키 불일치, 이중 서명, 서명된 채 꼬리 호출, 두 경로의 상태 충돌.
- 입력은 **실제 컴파일러 출력 45개 함수**(Mac clang 6가지 + VM gcc/clang 3가지 × 5 함수) — 축소 포장(shrink-wrapping)된 `early_exit` 의 맨 `ret` 을 오탐하면 안 된다. 그 워드를 한 개씩 바꾼 변이(`retaa → ret`, `autiasp → nop`, `paciasp → pacibsp`, 분기 목적지 변경 …)는 정확한 위반 비트로 잡아야 한다.

### Part C — PAC 모델과 CI 게이트

- 장난감 PAC 모델 `pac_field_mask / pac_sign / pac_auth / frame_return` (QARMA 아님, 성질만 흉내) — 필드 마스크가 실측(Mac arm64e I 키 `0xff7f800000000000`, D 키 `0x007f800000000000`, VM `0x007f000000000000`)과 같아야 하고, 위의 실측 표(위조 → 잡힘, 같은 SP 재사용 → 통과, 다른 SP → 잡힘)를 모델로 재현해야 한다.
- `lint_image()` — 펌웨어/앱 이미지의 모든 함수에 린터를 돌리는 CI 게이트. 손으로 쓴 `.S` 몇 개가 섞이면 정확히 그 함수들이 걸려야 한다.

## 2. Problem (English)

> **Branch protection: verify PAC-RET and BTI from machine code.**
>
> 1. `insn_t decode(uint32_t w)`: classify an A64 word as PACIASP/PACIBSP/AUTIASP/AUTIBSP, BTI/BTI c/j/jc, RET Xn/RETAA/RETAB, BR/BLR (incl. authenticated BRAA/BLRAAZ…), B/BL/B.cond/CBZ/TBZ with byte offset, or an STP/STR/LDP/LDR that stores/loads x30.
> 2. `int btype_of(uint32_t branch, int guarded)` and `int bti_accepts(kind_t pad, int btype, int bt_strict)`: BLR → 0b10; BR x16/x17 → 0b01; other BR → 0b11 in a guarded page (0b01 otherwise). BTI c accepts 01/10, BTI j 01/11, BTI jc all, plain BTI none, PACIxSP 01/10 (and 11 only if SCTLR_EL1.BT0 = 0).
> 3. `int lint_function(const uint32_t *w, int n, unsigned policy, lint_report_t *r)`: forward data-flow over the function's CFG starting at word 0 with LR unsigned. Report missing landing pad, unsigned LR spill, RET with signed LR, AUT of an unsigned LR, key mismatch, double signing, tail call with signed LR, and state conflicts at join points.
> 4. A toy PAC model: `pac_field_mask({va_bits, tbi})` = bits [54:va_bits] plus [63:56] when TBI is off; `pac_sign`, `pac_auth` (failure yields a non-canonical pointer), and `frame_return(slot, sp, pac_on, legit, key, cfg)` → NORMAL / REDIRECT / CAUGHT.
>
> Example: clang `-mbranch-protection=standard` emits `bti c; tbnz …; paciasp; stp x29, x30, …; …; retaa; mov w0, #-1; ret` for an early-exit function — lint result 0 (the bare `ret` is reached only on the unsigned path).
>
> Follow-ups: Why does a forged LR still work on a plain arm64 macOS binary built with `-mbranch-protection=standard`? Why is reuse at the same SP not caught? Why must every object carry the GNU property note for BTI?

## 3. 시작 코드

| 파일 | 내용 |
|---|---|
| `starter.c` / `solution.c` | 본문 문제 (TODO 9개) + 하네스. 소스 = `<v>.c` + `shared_ref.S` |
| `shared_ref.S` | 디코더 기대값용 실제 어셈블 워드 39개 (실행 안 함, 데이터로 읽음) |
| `demo_gen.c` + `demo_gen_main.c` | 관찰용 함수 5개 (leaf / nonleaf / early_exit / tail_call / call_ptr) |
| `gen.sh` → `gen_mac.txt`, `mac_forge.txt` | Mac: 옵션 6가지 + Cortex-M85 디스어셈블리, Mac 에서 위조·BTI 실행 결과 |
| `run_vm.sh` (`pac` / `gen` / `qemu` / `stat`) → `vm_pac.txt`, `gen_vm.txt`, `pac_collision_stats.txt` | VM: 실제 PAC/BTI 하드웨어에서 위조·재사용·BTI 표 (17개 검사), gcc/clang 코드 생성, 1000회 우연 일치 통계 |
| `demo_forge.c` | 자기 프레임의 저장 LR 을 바꾸는 장난감 (forge / replay / replay2) |
| `demo_bti.c` + `demo_bti_pads.S` | 착지 패드 6종 × 간접 분기 3종 |
| `demo_probe_pac.c` → `probe.txt` | PACIA/PACDA/PACGA 직접 실행: 키 켜짐 여부, PAC 필드 비트, ID 레지스터 |
| `demo_fork_keys.c` → `fork_keys.txt` | 키가 fork / exec 에서 유지되나 |
| `make_golden.py` | `gen_*.txt` → 하네스의 `golden[]` 표 (45 함수) |

```bash
cd lessons/ARM/ARM-A10
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
bash gen.sh                                                         # (선택) Mac 쪽 관찰 다시 만들기
limactl shell elinux -- bash -lc "bash $PWD/run_vm.sh pac"           # VM 실측 17개 검사, 끝줄 17/17 passed
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c shared_ref.S -o /tmp/ARM-A10_starter && /tmp/ARM-A10_starter
```

<!-- code: starter.c -->

starter 실제 출력 (요약 줄만, FAIL 53줄 생략) — exit 1:

```text
Part A (decode, BTYPE, landing pads): 27 checks, FAIL
Part B (lint_function on 45 real functions + mutants): 18 checks, FAIL
  toy model: accidental pass with wrong SP 4000/4000, wrong key 4000/4000 (7비트 VM 필드가 대부분)
Part C (toy PAC model, CI gate): 18 checks, FAIL
10/63 passed
```

## 4. 힌트

<details><summary>힌트 1 — HINT 공간부터</summary>

`HINT #imm` = `0xD503201F | imm7 << 5`. PAC/BTI 의 "SP 판" 명령은 전부 여기 있다: 25 PACIASP, 27 PACIBSP, 29 AUTIASP, 31 AUTIBSP, 32 BTI, 34 BTI c, 36 BTI j, 38 BTI jc (39 = PACM). HINT 라서 ARMv8.0 코어에서는 NOP — 그래서 한 바이너리가 옛 코어와 새 코어에서 다 돈다. `RETAA` (`0xD65F0BFF`)는 HINT 가 **아니다**.
</details>

<details><summary>힌트 2 — 분기 레지스터 그룹은 한 틀</summary>

비트 31:25 = `1101011`, 24 = Z, 23 = 0, 22:21 = op (00 BR, 01 BLR, 10 RET), 20:16 = `11111`, 15:12 = 0, 11 = A(인증), 10 = M(키 B), 9:5 = Rn, 4:0 = Rm. A=0 이면 Z·M·Rm 이 0, A=1·Z=0 이면 Rm = 31 (BRAAZ, BLRAAZ, RETAA). `ret` = `0xD65F03C0` 으로 손 확인해 보자.
</details>

<details><summary>힌트 3 — 왜 선형 스캔이 아니라 CFG 인가</summary>

`early_exit` 는 `bti c; tbnz w0, #31, L; paciasp; … retaa; L: mov w0, #-1; ret`. 줄 순서로 읽으면 마지막 `ret` 은 "서명 뒤의 인증 없는 ret" 처럼 보인다. 하지만 그 `ret` 은 `tbnz` 에서만 오고, 그 경로는 `paciasp` 를 지나지 않았다. 상태는 **경로**에 붙는다 → 워크리스트로 후속 명령에 상태를 밀어 넣고, 이미 다른 상태가 있으면 충돌.
</details>

<details><summary>힌트 4 — 꼬리 호출과 함수 밖</summary>

`b _sink` 의 목적지가 `[0, n)` 밖이면 함수를 떠나는 것이다. 그 순간 LR 은 원래 값(PLAIN)이어야 한다 — 받는 함수가 다시 서명하고 자기 `retaa` 로 **우리의 호출자**에게 돌아가기 때문이다. `paciasp … autiasp; b _sink` (pac-ret+leaf 의 tail_call) 는 정상.
</details>

<details><summary>힌트 5 — PAC 필드 마스크</summary>

필드 = 비트 [54 : VA 비트 수] (비트 55 는 상위/하위 주소 범위 선택이라 남김) + TBI 가 꺼진 포인터면 [63:56] 도. Mac arm64e 코드 포인터는 VA 47비트 · TBI 꺼짐 → 8 + 8 = 16비트, 데이터 포인터는 TBI 켜짐 → 8비트. VM 은 48비트 · 둘 다 7비트.
</details>

## 5. 풀이

### 5.1 컴파일러는 어디에 무엇을 넣나 (실측, `gen_mac.txt` · `gen_vm.txt`)

Mac Apple clang 21, `cc -O2 -mbranch-protection=standard` 링크 결과 (발췌):

```text
<_nonleaf>:
100000384: d503233f    	paciasp                  ← 비리프: 첫 명령이 서명 (= 암묵적 BTI c 패드)
100000388: a9bf7bfd    	stp	x29, x30, [sp, #-0x10]!
100000390: 94000023    	bl	0x10000041c <_sink>
100000398: a8c17bfd    	ldp	x29, x30, [sp], #0x10
10000039c: d65f0bff    	retaa                    ← 인증 + 반환 한 명령 (ARMv8.3 전용 인코딩)
<_early_exit>:
1000003a0: d503245f    	bti	c                    ← 첫 명령이 서명이 아니니 따로 패드
1000003a4: 37f80100    	tbnz	w0, #0x1f, 0x1000003c4   ← 음수면 프롤로그 없이 빠른 경로
1000003a8: d503233f    	paciasp                  ← 축소 포장: 서명이 분기 뒤로 밀림
 …
1000003c0: d65f0bff    	retaa
1000003c4: 12800000    	mov	w0, #-0x1
1000003c8: d65f03c0    	ret                      ← LR 을 저장한 적 없는 경로 → 맨 ret 이 맞다
```

같은 소스를 VM gcc 15.2 `-mbranch-protection=standard` 로:

```text
<nonleaf>:
 88c:	d503233f 	paciasp
 890:	a9bf7bfd 	stp	x29, x30, [sp, #-16]!
 898:	9400002a 	bl	940 <sink>
 89c:	a8c17bfd 	ldp	x29, x30, [sp], #16
 8a0:	d50323bf 	autiasp                     ← gcc 는 HINT 인 autiasp + ret (v8.0 에서도 실행 가능)
 8a4:	11000400 	add	w0, w0, #0x1
 8a8:	d65f03c0 	ret
```

| 변형 (실측) | leaf_add | nonleaf / call_ptr | early_exit | tail_call | 간접 호출 |
|---|---|---|---|---|---|
| Mac `none`, VM gcc `none` | 없음 | 없음 | 없음 | 없음 | `blr x20` |
| Mac `standard` | `bti c` | `paciasp` … `retaa` | `bti c`, 분기 뒤 `paciasp` … `retaa` | `bti c` | `blr` |
| VM gcc / clang `standard` | `bti c` | `paciasp` … `autiasp; ret` | 같은 모양 | `bti c` | `blr` |
| Mac `pac-ret+leaf` | `paciasp` … `retaa` | 같음 | **분기 앞** `paciasp` | `paciasp; add; autiasp; b _sink` | `blr` |
| Mac `pac-ret+b-key` | 없음 | `pacibsp` … `retab` | 분기 뒤 `pacibsp` | 없음 | `blr` |
| Mac `pac-ret+pc` | 없음 | `pacm; paciasp` … `adrp/add x16, <paciasp 주소>; pacm; retaa` | 같음 | 없음 | `blr` |
| Mac `-arch arm64e` | 없음 | `pacibsp` … `retab` | `pacibsp` … **`autibsp; ret`** | 없음 | **`blraaz x20`** (함수 포인터도 서명) |

관찰 포인트:

1. **리프는 기본으로 서명하지 않는다.** LR 을 메모리에 두지 않으니 덮일 수 없다. `+leaf` 는 그래도 서명한다 (리프에서도 LR 이 레지스터째 노출될 수 있는 환경용).
2. **PACIASP 는 착지 패드를 겸한다.** 첫 명령이 `paciasp` 면 `bti c` 를 따로 넣지 않는다 (Mac/gcc/clang 모두). 첫 명령이 서명이 아닐 때만 `bti c`. `bti+pac-ret+pc` 로 컴파일하면 첫 명령이 `pacm` 이라 실제로 `bti c; hint #39; paciasp` 가 나왔다 (clang -S 실측).
3. **arm64e 는 BTI 를 쓰지 않는다.** 대신 함수 포인터 자체가 서명돼 있어 `blraaz x20` 이 호출 직전에 인증한다 (전방 간선 보호를 BTI 대신 PAC 으로). 반환 주소는 B 키(`pacibsp/retab`). `early_exit` 에서만 `autibsp; ret` 로 쪼개 냈다 — 이유는 확인하지 못했다.
4. **`pac-ret+pc` (FEAT_PAuth_LR)**: `adrp/add x16` 으로 `paciasp` 의 주소를 x16 에 다시 만들고 `pacm` 이 다음 명령에 "PC 도 modifier 로 쓰라" 고 알린다. SP 만 쓸 때의 재사용 약점(5.3)을 노린 확장이다.
5. **Cortex-M85** (`--target=thumbv8.1m.main-none-eabi -mcpu=cortex-m85 -mbranch-protection=standard`): `pacbti r12, lr, sp` (`f3af 800d`) → `push.w {r7, r12, lr}` 로 PAC 을 r12 째 스택에 저장 → `pop.w` → `aut r12, lr, sp` (`f3af 802d`) → `bx lr`. 리프는 `bti` (`f3af 800f`). M-프로파일은 포인터 상위 비트가 남지 않으니 PAC 을 **별도 레지스터(r12)** 에 둔다 (연습 4).

`gen.sh` 가 각 실행 파일을 실제로 돌려 본 종료 코드는 모두 0 (`run exit=0`) — 이 맥에서 arm64e 실행 파일도 돌았다. 이 맥은 `ReleaseType: Internal` 빌드다. 일반 macOS 에서는 서드파티 arm64e 실행이 부트 인자(`-arm64e_preview_abi`) 없이는 막힐 수 있다 (문서 확인 권장).

### 5.2 키와 비트 — `demo_probe_pac.c` 실측 (`probe.txt`)

```text
[Mac arm64] cc -O2 demo_probe_pac.c
pacia(p, m)      = 0x0000000100004000        ← 그대로! 키가 꺼져 있다
pacga(p, m)      = 0x2cabc7ca00000000        ← 범용 키 PACGA 는 동작
4096 modifiers: I-key PAC bits = 000000000000000000 (0 bits), D-key PAC bits = 000000000000000000 (0 bits)
[Mac arm64e] cc -O2 -arch arm64e
pacia(p, m)      = 0x9e1a800100004000
pacia(p, m+16)   = 0xd532000100004000        ← modifier(SP 자리)만 바뀌어도 PAC 이 다르다
pacib(p, m)      = 0xb262800100004000
pacda(p, m)      = 0x007d000100004000
4096 modifiers: I-key PAC bits = 0xff7f800000000000 (16 bits), D-key PAC bits = 0x007f800000000000 (8 bits)
[VM Linux 7.0] gcc -O2
pacia(p, m)      = 0x007e000100004000
pacda(p, m)      = 0x0000000100004000        ← PAC 이 우연히 0 (7비트면 1/128 확률)
4096 modifiers: I-key PAC bits = 0x007f000000000000 (7 bits), D-key PAC bits = 0x007f000000000000 (7 bits)
ID_AA64ISAR1_EL1 = 0x0010201110211602  APA=0 API=6 GPA=0 GPI=1
ID_AA64PFR1_EL1  = 0x0000000002000001  BT=1
```

- **Mac arm64 (arm64e 아님) 프로세스에서는 IA/IB/DA 키가 꺼져 있다** — `pacia` 가 포인터를 그대로 돌려준다. 그래서 `-mbranch-protection=standard` 로 빌드한 arm64 바이너리의 `paciasp`/`retaa` 는 아무것도 막지 못한다 (1. 의 표, ARM-M14 의 "pac-ret 을 줘도 증상이 같았다" 관찰과 일치). 커널이 프로세스마다 SCTLR_EL1.EnIA 같은 활성 비트를 끄는 것으로 보인다 (관찰로부터의 추정). PACGA 는 활성 비트가 없는 명령이라 돈다 (Arm ARM 기준, 확인 권장).
- **필드 폭이 환경마다 다르다**: Mac arm64e 코드 포인터 16비트(63:56 + 54:47), 데이터 포인터 8비트(54:47), VM 은 7비트(54:48) — VM 커널은 코드 포인터에도 TBI 를 적용하는 설정으로 보인다 (상위 바이트가 PAC 에 쓰이지 않음, 관찰).
- **VM 의 ID 레지스터** (Linux 가 EL0 의 MRS 를 에뮬레이트): API = 6 → 구현 정의 알고리즘으로 FEAT_PAuth2 · FEAT_FPAC 이상(값 6 은 FEAT_PAuth_LR 수준으로 알려짐 — 문서 확인 권장). APA/APA3 = 0 → QARMA5/QARMA3 아님. BT = 1 → FEAT_BTI.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">64비트 포인터에서 PAC 이 차지하는 비트 (실측)</text>
<g fill="currentColor"><text x="120" y="40">63</text><text x="212" y="40">56</text><text x="236" y="40">55</text><text x="262" y="40">54</text><text x="400" y="40">47/48</text><text x="700" y="40">0</text></g>
<text x="10" y="70" fill="currentColor">Mac arm64e I 키</text>
<rect x="120" y="54" width="110" height="24" class="f-acc" stroke="currentColor"/>
<rect x="230" y="54" width="24" height="24" class="f-mut" stroke="currentColor"/>
<rect x="254" y="54" width="170" height="24" class="f-acc" stroke="currentColor"/>
<rect x="424" y="54" width="290" height="24" class="f-ok" stroke="currentColor"/>
<text x="140" y="71" fill="currentColor">PAC 8</text><text x="300" y="71" fill="currentColor">PAC 8 (54:47)</text><text x="520" y="71" fill="currentColor">주소 47비트</text>
<text x="10" y="110" fill="currentColor">Mac arm64e D 키</text>
<rect x="120" y="94" width="110" height="24" class="f-mut" stroke="currentColor"/>
<rect x="230" y="94" width="24" height="24" class="f-mut" stroke="currentColor"/>
<rect x="254" y="94" width="170" height="24" class="f-acc" stroke="currentColor"/>
<rect x="424" y="94" width="290" height="24" class="f-ok" stroke="currentColor"/>
<text x="140" y="111" fill="currentColor">TBI 태그</text><text x="300" y="111" fill="currentColor">PAC 8 (54:47)</text><text x="520" y="111" fill="currentColor">주소 47비트</text>
<text x="10" y="150" fill="currentColor">VM Linux I·D 키</text>
<rect x="120" y="134" width="110" height="24" class="f-mut" stroke="currentColor"/>
<rect x="230" y="134" width="24" height="24" class="f-mut" stroke="currentColor"/>
<rect x="254" y="134" width="150" height="24" class="f-acc" stroke="currentColor"/>
<rect x="404" y="134" width="310" height="24" class="f-ok" stroke="currentColor"/>
<text x="140" y="151" fill="currentColor">TBI</text><text x="290" y="151" fill="currentColor">PAC 7 (54:48)</text><text x="520" y="151" fill="currentColor">주소 48비트</text>
<text x="236" y="180" fill="currentColor">↑ 비트 55: 상/하위 VA 범위 선택 (PAC 아님)</text>
<text x="10" y="208" fill="currentColor">마스크: 0xff7f800000000000 · 0x007f800000000000 · 0x007f000000000000 — 4096개 modifier 로 서명해 바뀐 비트를 OR 한 값</text>
<text x="10" y="230" fill="currentColor">무작위로 맞힐 확률: 1/65536 · 1/256 · 1/128 — "PAC 은 짧은 MAC" 이라는 사실이 설계의 한계를 정한다</text>
</svg>
```

### 5.3 실제로 죽이기 — `demo_forge.c`

`victim_forge()` 는 `__builtin_frame_address(0)` (= x29, 프레임 레코드)에서 `[x29+8]` 저장 LR 슬롯을 찾아 `&landing` 의 **생 주소**로 덮고 정상 반환한다. 오버플로 없이 결정적으로 "ROP 의 첫 걸음" 만 재현한다. arm64e 에서 `&landing` 은 서명된 함수 포인터라 `ptrauth_strip` 으로 생 주소를 만든다.

`run_vm.sh pac` 실제 출력 (`vm_pac.txt` 발췌):

```text
--- forge_std forge
  child: saved LR slot = 0x005dc1c983ea0c54 -> overwrite with raw &landing = 0x0000c1c983ea0e00
RESULT forge: signal 4 (Illegal instruction)
--- forge_std replay
  child: record_lr saved LR = 0x0069c875afea0c0c
  child: victim_replay own LR = 0x0072c875afea0c6c -> replace with recorded
  child: REPLAYED — came back to record_lr's return site (pass 2)
RESULT replay: exit 43
--- forge_std replay2
RESULT replay2: signal 4 (Illegal instruction)
  (FEAT_FPAC: autiasp 자체가 예외 → SIGILL)
PASS std: forged raw LR → killed by PAC (FPAC → SIGILL): signal 4 (Illegal instruction)
PASS std: replay same SP → STILL redirected (SP modifier weakness): exit 43
PASS std: replay other SP → killed by PAC: signal 4 (Illegal instruction)
 …
17/17 passed
```

- 저장 LR `0x005dc1c983ea0c54` — 비트 54:48 에 PAC `0x5d`. 덮은 값 `0x0000…0e00` 은 PAC 자리가 0 이라 인증 실패.
- **SIGILL 인 이유**: 이 CPU 는 FEAT_FPAC 이 있어 `autiasp` 가 실패하는 순간 예외를 낸다(Linux 는 SIGILL 로 전달). FPAC 이 없는 ARMv8.3 코어라면 `autiasp` 는 조용히 **비정규 포인터**를 만들고, 다음 `ret` 이 거기로 가다 번역 fault → SIGSEGV 가 된다 (스크립트는 둘 다 구분해 출력하도록 짰다).
- **Mac arm64e** (`mac_forge.txt`): forge → `signal 10 (Bus error: 10)`, replay → exit 43, replay2 → SIGBUS. lldb 로 보면 `EXC_BAD_ACCESS (code=261 = EXC_ARM_PAC_FAIL)`, pc 는 `retab` 자체 (ARM-M14 의 `crash_stack_arm64e.txt`).
- **같은 SP 재사용이 통과하는 이유**: `record_lr` 과 `victim_replay` 는 둘 다 `run_child` 에서 바로 불려 진입 SP 가 같다. PAC = f(LR, SP, 키) 이니 `record_lr` 의 서명된 LR 은 `victim_replay` 프레임에서도 유효한 서명이다. 공격자가 "같은 깊이의 다른 유효한 반환 주소" 를 하나 훔쳐 쓰면 통과 — Apple 의 arm64e 도 실측으로 같았다. `replay2` 는 한 프레임 더 깊어 SP 가 달라 잡혔다.
- **"잡힘" 은 확률이다.** PAC 필드가 7비트라 아무 값이나 넣어도 1/128 확률로 인증이 통과한다. `run_vm.sh stat` 으로 1000번씩 돌린 실측 (`pac_collision_stats.txt`): forge → `exit 42`(landing 도착) **8번** / SIGILL 992번, replay2 → `exit 43` **12번** / SIGILL 988번 (기대 ≈ 7.8번). 실제로 체커 루프 11번 중 1번이 이것 때문에 16/17 로 떨어졌다. 그래서 `run_vm.sh pac` 은 PAC 이 잡아야 하는 두 검사를 `result_pac` 으로 최대 4번 실행한다 — 실행(exec)마다 키가 새로 뽑히므로(`fork_keys.txt`) 시도들은 독립이고, 기대 결과(SIGILL)가 한 번이라도 나오면 통과, 그 전의 빗나간 시도는 "7비트 PAC 우연 일치" 로 출력한다. 4번 모두 빗나갈 확률은 (1/128)^4 ≈ 4×10^-9.
- `pac-ret+pc` 로 빌드한 VM 바이너리(clang)도 replay 가 **여전히 통과**했다 (`exit 43`, qemu-aarch64 -cpu max 에서도 같음). `pacm` 이 이 환경에서 효과가 없었다 — 커널/하이퍼바이저가 FEAT_PAuth_LR 을 EL0 에 켜 주지 않는 것으로 추정 (확인 못 함).

### 5.4 BTI 표 — 실제 하드웨어 (`run_vm.sh pac`, `bti_note`)

```text
pad\via   | blr (10)     | br x16 (01)  | br x9 (11)  
none      | SIGILL       | SIGILL       | SIGILL      
bti       | SIGILL       | SIGILL       | SIGILL      
bti c     | ok(12)       | ok(12)       | SIGILL      
bti j     | SIGILL       | ok(13)       | ok(13)      
bti jc    | ok(14)       | ok(14)       | ok(14)      
paciasp   | ok(15)       | ok(15)       | SIGILL      
```

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">간접 분기 → PSTATE.BTYPE → 목적지 첫 명령이 받아 주나</text>
<rect x="10" y="40" width="190" height="32" class="f-mut" stroke="currentColor"/><text x="20" y="61" fill="currentColor" font-family="monospace">blr xN / blraa…</text>
<rect x="10" y="90" width="190" height="32" class="f-mut" stroke="currentColor"/><text x="20" y="111" fill="currentColor" font-family="monospace">br x16 / br x17</text>
<rect x="10" y="140" width="190" height="32" class="f-mut" stroke="currentColor"/><text x="20" y="161" fill="currentColor" font-family="monospace">br 그 밖의 xN</text>
<rect x="10" y="190" width="190" height="32" class="f-mut" stroke="currentColor"/><text x="20" y="211" fill="currentColor" font-family="monospace">ret / b / bl</text>
<rect x="250" y="40" width="90" height="32" class="f-acc" stroke="currentColor"/><text x="262" y="61" fill="currentColor">BTYPE 10</text>
<rect x="250" y="90" width="90" height="32" class="f-acc" stroke="currentColor"/><text x="262" y="111" fill="currentColor">BTYPE 01</text>
<rect x="250" y="140" width="90" height="32" class="f-acc" stroke="currentColor"/><text x="262" y="161" fill="currentColor">BTYPE 11</text>
<rect x="250" y="190" width="90" height="32" class="f-ok" stroke="currentColor"/><text x="262" y="211" fill="currentColor">BTYPE 00</text>
<g stroke="currentColor"><line x1="200" y1="56" x2="250" y2="56"/><line x1="200" y1="106" x2="250" y2="106"/><line x1="200" y1="156" x2="250" y2="156"/><line x1="200" y1="206" x2="250" y2="206"/></g>
<text x="380" y="61" fill="currentColor">받는 패드: bti c · bti jc · paciasp</text>
<text x="380" y="111" fill="currentColor">받는 패드: bti c · bti j · bti jc · paciasp</text>
<text x="380" y="161" fill="currentColor">받는 패드: bti j · bti jc (paciasp ✗, BT0=1)</text>
<text x="380" y="211" fill="currentColor">검사 없음 (반환·직접 분기)</text>
<text x="10" y="248" fill="currentColor" font-size="12">x16/x17 이 특별한 이유: PLT·베니어가 br x16/x17 로 함수 시작(bti c)에 점프하므로 "호출" 로 취급</text>
</svg>
```

- 행 하나하나가 BTYPE 규칙 그대로다. 특히 `paciasp` 가 `br x9`(BTYPE 11)에서 SIGILL — Linux 는 SCTLR_EL1.BT0 = 1 로 켜서 "PACIxSP 는 x16/x17 이 아닌 BR 의 패드가 될 수 없다" 로 만든다 (커널 BTI 패치 설명과 QEMU `target/arm: Enable SCTLR_EL1.BT0 for aarch64-linux-user` 커밋 메시지 기준). 하네스의 `bti_accepts(..., bt_strict = 1)` 이 이 표를 18칸 모두 맞춰야 PASS.
- **BTI 는 바이너리 전체가 켜져야 걸린다.** `.S` 에 GNU property 노트를 빼고 링크한 `bti_nonote` 는 18칸 전부 `ok` — 링커가 모든 입력 오브젝트의 `GNU_PROPERTY_AARCH64_FEATURE_1_AND` 를 AND 하고, 하나라도 없으면 결과 실행 파일에 BTI 비트가 없다 → 로더(glibc)가 페이지를 `PROT_BTI` 로 매핑하지 않는다. `-Wl,-z,force-bti` 는 실제로 경고를 내며 강제로 켠다:
  `ld: <demo_bti_pads.o>: warning: BTI is required by -z force-bti, but this input object file lacks the necessary property note.`
- gcc 15.2 `standard` 결과물의 노트는 `BTI, PAC, GCS` (GCS = Guarded Control Stack, 하드웨어 섀도 스택 — 이 VM CPU 의 `/proc/cpuinfo` 에는 `gcs` 가 없어 실제로 켜지지는 않는다).
- **Mac arm64** 에서 같은 표를 돌리면 18칸 모두 `ok` (`mac_forge.txt`) — macOS 는 서드파티 arm64 프로세스에 BTI 를 강제하지 않았다 (관찰). Apple 은 arm64e 에서 서명된 함수 포인터(`blraaz`)로 전방 간선을 보호한다.
- `qemu-aarch64 -cpu max` (사용자 모드)로 같은 바이너리를 돌려도 같은 18칸이 나왔다 (`run_vm.sh qemu`).

### 5.5 Part A — 디코더

| 명령 | 워드 (shared_ref.S 실측) | 판별식 |
|---|---|---|
| `paciasp` / `autiasp` / `bti c` | `d503233f` / `d50323bf` / `d503245f` | `(w & 0xFFFFF01F) == 0xD503201F`, imm7 = 25 / 29 / 34 |
| `retaa` / `retab` / `ret` | `d65f0bff` / `d65f0fff` / `d65f03c0` | 분기 레지스터 틀, op = 10, A·M |
| `braa x16, x17` / `blraaz x20` | `d71f0a11` / `d63f0a9f` | Z = 1 → Rm 이 modifier / Z = 0 → Rm = 31 |
| `bl` / `b` | `94000004` / `14000003` | `(w & 0x7C000000) == 0x14000000`, bit31 = 링크 |
| `b.ne` / `cbz` / `tbnz` | `54000041` / `b4000020` / `37f80040` | imm19 / imm19 / imm14, ×4 |
| `stp x29, x30, [sp, #-16]!` | `a9bf7bfd` | `(w & 0xFE000000) == 0xA8000000`, 24:23 ≠ 00, Rt 또는 Rt2 = 30 |
| `str x30, [sp, #-16]!` / `ldr x30, [sp], #16` | `f81f0ffe` / `f84107fe` | `(w & 0xFFA00400) == 0xF8000400`, Rt = 30 |

`ret` = `0xD65F03C0` 을 틀에 대 보면: 비트 31:25 = `1101011`, 22:21 = `10`(RET), 20:16 = `11111`, A = 0, Rn = `11110` = 30. `retaa` 는 같은 틀에 A = 1, Rn = Rm = 31. M14 의 arm64e 크래시 `address=0xd65f0fff` 는 바로 `retab` 의 인코딩이었다 (FPAC 예외가 명령 워드를 보고한 것으로 보인다).

테스트: 실제 어셈블 워드 39개, HINT 공간 128개 전수, BR/BLR/RET × 레지스터 32개 전수, 할당 안 된 `0xD65F0BFE` 는 OTHER, 무작위 워드 20000개(범위 밖 종류 없음), B 의 ±128 MB 끝값.

### 5.6 Part B — CFG 데이터 흐름 린터

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="12">
<text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">early_exit (Mac standard) — 명령마다 LR 상태</text>
<g font-family="monospace" fill="currentColor">
<rect x="20" y="30" width="250" height="24" class="f-mut" stroke="currentColor"/><text x="28" y="47">0 bti c</text><text x="200" y="47">PLAIN</text>
<rect x="20" y="58" width="250" height="24" class="f-mut" stroke="currentColor"/><text x="28" y="75">1 tbnz w0,#31 → 9</text><text x="200" y="75">PLAIN</text>
<rect x="20" y="86" width="250" height="24" class="f-acc" stroke="currentColor"/><text x="28" y="103">2 paciasp</text><text x="200" y="103">→ A</text>
<rect x="20" y="114" width="250" height="24" class="f-acc" stroke="currentColor"/><text x="28" y="131">3 stp x29,x30 (spill)</text><text x="200" y="131">A</text>
<rect x="20" y="142" width="250" height="24" class="f-acc" stroke="currentColor"/><text x="28" y="159">4-6 mov · bl · lsl</text><text x="200" y="159">A</text>
<rect x="20" y="170" width="250" height="24" class="f-acc" stroke="currentColor"/><text x="28" y="187">7 ldp x29,x30</text><text x="200" y="187">A</text>
<rect x="20" y="198" width="250" height="24" class="f-ok" stroke="currentColor"/><text x="28" y="215">8 retaa</text><text x="200" y="215">A → 끝</text>
<rect x="20" y="236" width="250" height="24" class="f-mut" stroke="currentColor"/><text x="28" y="253">9 mov w0,#-1</text><text x="200" y="253">PLAIN</text>
<rect x="20" y="264" width="250" height="24" class="f-ok" stroke="currentColor"/><text x="28" y="281">10 ret</text><text x="200" y="281">PLAIN ✓</text>
</g>
<path d="M270 70 C330 70 330 248 270 248" stroke="currentColor" fill="none"/>
<text x="335" y="160" fill="currentColor">tbnz 경로: 서명 안 지남</text>
<rect x="470" y="40" width="260" height="110" class="f-warn" stroke="currentColor"/>
<text x="480" y="60" fill="currentColor">줄 순서 스캔 ("ret 뒤엔 다시</text>
<text x="480" y="78" fill="currentColor">서명 상태") 이면 10 번 ret 을</text>
<text x="480" y="96" fill="currentColor">"인증 없는 ret" 으로 오탐한다.</text>
<text x="480" y="118" fill="currentColor">상태는 경로에 붙는다 →</text>
<text x="480" y="136" fill="currentColor">워크리스트로 전파</text>
<rect x="470" y="170" width="260" height="90" class="f-mut" stroke="currentColor"/>
<text x="480" y="190" fill="currentColor">변이: tbnz 목적지를 7 (ldp) 로</text>
<text x="480" y="210" fill="currentColor">→ 7 에 PLAIN 과 A 가 같이 도착</text>
<text x="480" y="230" fill="currentColor">→ V_JOIN_CONFLICT</text>
<text x="480" y="250" fill="currentColor">(축소 포장을 잘못한 컴파일러)</text>
</svg>
```

전이 규칙 (상태 = PLAIN / A / B):

| 명령 | 조건 | 다음 상태 / 위반 |
|---|---|---|
| `paciasp` / `pacibsp` | PLAIN 이 아니면 `V_DOUBLE_SIGN` | A / B |
| `autiasp` / `autibsp`, `retaa` / `retab` | PLAIN → `V_AUTH_UNSIGNED`(실행하면 항상 실패), 키 다름 → `V_KEY_MISMATCH` | PLAIN (RETAx 는 끝) |
| x30 저장 (STP/STR) | POL_PAC 이고 PLAIN → `V_SPILL_UNSIGNED` | 그대로 |
| `ret` (x30) | 서명 상태 → `V_RET_UNAUTH` | 끝 |
| `br`, 함수 밖으로 가는 `b`/`b.cond` | 서명 상태 → `V_TAIL_UNAUTH` | 끝 (b.cond 는 계속) |
| 후속 명령에 이미 다른 상태 | — | `V_JOIN_CONFLICT` |

`bl` 은 x30 을 덮지만 서명된 원래 LR 은 이미 스택에 있으니 "함수의 반환 주소 상태" 는 그대로 둔다. 진입 패드 검사(POL_BTI)는 첫 명령이 `bti c` / `bti jc` / `paciasp` / `pacibsp` 인가.

실제 45개 함수에 돌린 결과 (POL_PAC|POL_BTI, 하네스의 `expect_full`):

| 변형 | 결과 |
|---|---|
| Mac `none`, VM gcc `none` | 5개 전부 `V_NO_PAD`, 비리프 3개는 `+ V_SPILL_UNSIGNED` |
| Mac `standard`, `pac-ret+leaf`, VM gcc/clang `standard` | 0 (깨끗) — `+leaf` 는 `bti` 플래그 없이도 첫 명령 `paciasp` 가 패드라 통과 |
| Mac `pac-ret+b-key`, `arm64e` | 리프·꼬리 호출·`early_exit`(첫 명령 `tbnz`)만 `V_NO_PAD`, PAC 은 0 |
| Mac `pac-ret+pc` | 5개 모두 `V_NO_PAD` (첫 명령이 `pacm`), PAC 은 0 |

POL_PAC 만 걸면 위 결과에서 `V_NO_PAD` 를 뺀 값이 나와야 한다 (같이 검사). 주입 변이 8가지는 정확한 비트로 잡혔다 (6. 정답 코드의 Part B 테스트).

### 5.7 Part C — 장난감 PAC 모델과 CI 게이트

```c
uint64_t m = ((1ull << 55) - 1) & ~((1ull << c.va_bits) - 1);   /* [54:va_bits] */
if (!c.tbi) m |= 0xFF00000000000000ull;                          /* [63:56] */
```

`pac_sign` 은 필드를 지운 포인터에 `toy_pac_hash(포인터, modifier, 키)` 의 필드 비트를 끼운다. **이 해시는 QARMA 가 아니다** — splitmix64 두 번. 모델이 보여 주려는 것은 세 가지뿐: (1) 키 없이 맞는 PAC 을 만들 수 없다, (2) modifier(SP)가 다르면 PAC 이 다르다, (3) 필드가 w 비트면 우연히 맞을 확률이 2^-w 다. 실제 하네스 출력: 다른 SP 로 인증 시 우연 통과 7/4000, 다른 키 14/4000 (절반이 7비트 VM 설정이라 기대 ≈ 2000/128 ≈ 16).

`frame_return` 으로 5.3 의 실측 6칸(PAC 꺼짐 3칸 REDIRECT, 켜짐: CAUGHT / REDIRECT / CAUGHT)을 그대로 재현한다. 모델과 하드웨어가 같은 답을 내야 "왜" 를 설명할 수 있다.

CI 게이트 실측 시나리오 (하네스): Mac `standard` 5함수 → 통과 (서명 함수 3). 여기에 `demo_bti_pads.S` 의 손 asm 7개를 섞으면 **정확히 4개**(`pad_none`, `pad_bti`, `pad_j` 패드 문제, `via_blr` 은 `bti c` 는 있지만 LR 을 서명 없이 저장)가 걸린다. 컴파일러는 C 에 PAC/BTI 를 넣어 주지만 **손으로 쓴 어셈블리는 아무도 안 넣어 준다** — 실제 프로젝트(부트로더, 컨텍스트 스위치, memcpy)에서 빠지는 곳이 여기다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c shared_ref.S -o /tmp/ARM-A10_solution && /tmp/ARM-A10_solution   # 63/63, exit 0
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c shared_ref.S -o /tmp/ARM-A10_asan && /tmp/ARM-A10_asan   # 63/63, 깨끗
```

실제 출력:

```text
Part A (decode, BTYPE, landing pads): 27 checks, ok
Part B (lint_function on 45 real functions + mutants): 18 checks, ok
  toy model: accidental pass with wrong SP 7/4000, wrong key 14/4000 (7비트 VM 필드가 대부분)
Part C (toy PAC model, CI gate): 18 checks, ok
63/63 passed
```

하네스가 정말 버그를 잡는지: solution 의 사본(레슨 디렉터리 밖)에 변이 8개를 넣어 돌렸고 모두 FAIL 이었다 — STP 에서 Rt2 만 보기(`stp x30, x19` 놓침 → 참조 워드 검사), PACIASP 가 BTYPE 11 도 받기(VM 표 불일치), 키 비교 생략(키 불일치 변이 2개), `br x17` 을 BTYPE 11 로(`btype_of(br x17)`), 필드 마스크에 비트 55 포함(마스크 4개), 인증에서 modifier 무시(재사용 시나리오), 꼬리 호출 검사 생략, 합류 충돌 검사 생략.

VM 에서 gcc 15.2 로도 같은 파일이 빌드·통과했다 (`gcc -std=c11 … solution.c shared_ref.S` → 63/63).

## 7. 자주 하는 실수

**1. "`-mbranch-protection=standard` 로 빌드했으니 Mac 에서 보호된다" 고 믿는다.** 이 맥의 arm64 프로세스에서는 `pacia` 가 포인터를 그대로 돌려줬고(`probe.txt`), 위조 LR 로 `landing()` 에 그대로 도착했다 (`mac_forge.txt`, exit 42). 실제 보호를 보려면 `-arch arm64e` 또는 키가 켜진 Linux. 고친 확인 절차: 서명 명령의 *존재*가 아니라 *효과*를 본다.

```bash
cc -O2 -arch arm64e demo_probe_pac.c -o /tmp/pe && /tmp/pe | head -1   # PAC 비트가 붙어야 키가 켜진 것
```

**2. 착지 패드를 C 에서만 생각한다.** 손으로 쓴 `.S` 는 `bti c` 도 GNU property 노트도 자동으로 안 붙는다. 노트 하나가 빠지면 **링크된 실행 파일 전체**의 BTI 가 꺼진다 (`bti_nonote`: 18칸 모두 ok). 고친 `.S` 끝부분 (`demo_bti_pads.S`):

```asm
        .section .note.gnu.property, "a"
        .p2align 3
        .word   4, 16, 5                // namesz, descsz, NT_GNU_PROPERTY_TYPE_0
        .asciz  "GNU"
        .word   0xc0000000, 4, 3, 0     // FEATURE_1_AND, 4바이트, BTI|PAC, 패딩
```

**3. 줄 순서로 린트한다.** 축소 포장된 `early_exit` 의 빠른 경로 `ret` 을 "인증 없는 ret" 으로 오탐하거나, 반대로 "ret 뒤는 PLAIN" 으로 리셋해 진짜 버그를 놓친다. 고친 방법: 분기를 따라가는 워크리스트 (`lint_function`).

**4. `retaa` 와 `autiasp; ret` 를 같은 것으로 본다.** 의미는 같지만 `retaa` 는 HINT 공간이 아니라 ARMv8.3 이전 코어에서 **정의되지 않은 명령**이다. 배포판(gcc, 일반 `armv8-a` 타깃 clang)은 `autiasp; ret` 를 써서 옛 코어에서도 NOP 로 돈다. Apple clang(M 시리즈 타깃)은 `retaa` 를 썼다. 디코더/린터는 둘 다 인증으로 쳐야 한다.

**5. PAC 실패 = SIGSEGV 라고 단정한다.** FEAT_FPAC 이 있으면 `autiasp` 에서 바로 예외(VM: SIGILL, Mac arm64e: SIGBUS / `EXC_ARM_PAC_FAIL`), 없으면 비정규 포인터로 `ret` 한 뒤 SIGSEGV. 크래시 분류 도구는 셋 다 "PAC 실패 가능성" 으로 봐야 한다 (ARM-M14).

**6. PAC 테스트를 한 번 돌려 결정적이라고 믿는다.** 7비트 PAC 이면 "위조가 잡힌다" 는 검사가 1/128 확률로 뒤집힌다 (실측 forge 8/1000, replay2 12/1000). 한 번만 돌리는 검사를 CI 에 넣으면 검사 하나당 128번에 한 번 빨갛게 뜬다 (Mac arm64e 의 16비트 코드 PAC 이면 1/65536). 고친 코드 (`run_vm.sh`): 새 키로 다시 실행하되, 빗나간 시도를 숨기지 않고 이유와 함께 찍는다.

```bash
result_pac() {   # result_pac <bin> <mode> <기대>
    local r k
    for k in 1 2 3 4; do
        r=$(result "$1" "$2")
        [ "$r" = "$3" ] && break
        echo "  ($1 $2 attempt $k: [$r] — 7비트 PAC 우연 일치(1/128)로 인증 통과, 새 키로 다시 실행)" >&2
    done
    echo "$r"
}
```

반대로 이 숫자 자체가 면접 답이다: 공격자도 같은 1/128 을 얻는다 (Q2).

**7. SP 가 modifier 니까 재사용은 불가능하다고 말한다.** 같은 SP 에서 불린 다른 함수의 서명 LR 은 그대로 통한다 (VM·arm64e 모두 exit 43 실측). 대책은 PC 까지 섞는 FEAT_PAuth_LR(`+pc`), 섀도 스택(GCS), 또는 CFI 조합이다.

## 8. 면접 Q&A

<details><summary>Q1. PAC 이 ROP 를 어떻게 막나요? 한 문장으로, 그리고 명령으로.</summary>

반환 주소가 메모리에 머무는 동안 위조되면 쓸 수 없게, 들어올 때 `paciasp` 로 LR 상위 미사용 비트에 MAC(키 IA, modifier = SP)을 넣고, 나갈 때 `autiasp`/`retaa` 로 검사한다. 키는 커널만 알기 때문에 공격자는 임의 주소에 맞는 PAC 을 만들 수 없다. 이번 VM 실측: 생 주소로 덮은 LR → `autiasp` 에서 SIGILL.
</details>

<details><summary>Q2. 꼬리: PAC 은 몇 비트고, 무차별 대입하면요?</summary>

VA 크기와 TBI 에 달렸다. 실측: Linux VM 7비트, Mac arm64e 코드 포인터 16비트, 데이터 포인터 8비트. 7비트면 50% 성공까지 89번(연습 3) — 이 VM 에서 위조 LR 을 1000번 넣어 보니 실제로 8번 통과했다 (`pac_collision_stats.txt`). 그래서 **키가 다시 뽑히지 않는 상태에서 재시도할 수 있는가**가 핵심이다: VM·Mac 모두 fork 자식은 부모와 같은 키였다(`fork_keys.txt`). fork 해서 요청을 처리하는 서버가 크래시 후 같은 키로 다시 fork 하면 오라클이 된다. exec 하면 Linux 는 키가 바뀌었다. 대책: 크래시율 감시/재시작 지연, exec 기반 워커, VA 를 줄여 PAC 비트 늘리기(주소 공간과 맞바꿈).
</details>

<details><summary>Q3. SP 를 modifier 로 쓰는 설계가 틀리는 경우는?</summary>

같은 SP 에서 서명된 다른 반환 주소로 바꿔치기(재사용)하면 통과한다 — 이번에 같은 호출자에서 연달아 불린 두 함수로 실제로 재현했다 (VM gcc, Mac arm64e 모두 exit 43). 또 SP 가 같은 재귀 깊이·스레드 스택 재사용에서도 생긴다. 바꾼다면: PC 까지 modifier 에 넣는 FEAT_PAuth_LR (`-mbranch-protection=pac-ret+pc`, 코드 생성은 `pacm; paciasp` 와 x16 = 함수 시작 주소), 하드웨어 섀도 스택 GCS(반환 주소를 별도 보호 스택과 비교), 혹은 Apple 처럼 함수 포인터·vtable 까지 서명하는 ABI 전체 설계. 단 이 VM 에서는 `+pc` 빌드도 재사용이 통과했다 — 기능이 EL0 에 켜졌는지까지 확인해야 한다.
</details>

<details><summary>Q4. BTI 는 무엇을 막고, PAC 과 어떻게 나뉘나요?</summary>

PAC-RET 은 **역방향 간선**(반환)을, BTI 는 **전방 간선**(간접 분기 `br`/`blr`)을 막는다. BTI 가 켜진(GP 비트) 페이지에서 간접 분기의 목적지는 착지 패드여야 하고, 아니면 Branch Target 예외 → Linux SIGILL. JOP 가젯(함수 중간)으로 점프하는 것을 막는다. 실측 18칸 표: `bti c` 는 BLR·`br x16` 만, `bti j` 는 BR 만, `paciasp` 는 BLR·`br x16` 만(Linux 는 SCTLR_EL1.BT0 = 1). Apple arm64e 는 BTI 대신 함수 포인터 서명(`blraaz`)으로 전방 간선을 지킨다.
</details>

<details><summary>Q5. 왜 x16/x17 은 BR 인데도 "호출" 로 취급되나요?</summary>

링커가 만드는 PLT 스텁과 범위 밖 분기용 베니어가 `br x16`/`br x17` 로 **함수 시작**에 점프한다. 함수 시작에는 `bti c` 가 있으므로, 이 레지스터로의 BR 은 BTYPE 01 (c 와 j 모두 허용)로 만들어 PLT 경유 호출이 깨지지 않게 했다. 그 밖의 레지스터로의 BR 은 BTYPE 11 → `bti j`/`bti jc` 만 허용 (switch 점프 테이블 목적지가 `bti j`). 실측: `bti c` 함수에 `br x16` ok, `br x9` SIGILL.
</details>

<details><summary>Q6. 바이너리에 PAC/BTI 가 "켜져 있는지" 어떻게 검사하나요? CI 에 넣는다면?</summary>

두 층: (1) ELF `readelf -n` 의 `AArch64 feature: BTI, PAC` — 하나라도 노트 없는 오브젝트가 있으면 링커 AND 로 탈락하니 CI 는 `-z force-bti` 경고나 노트 부재를 실패로 처리한다 (`-z bti-report=error` 같은 옵션도 있다고 알려짐, 확인 권장). (2) 명령 수준: 이 레슨의 린터처럼 함수별로 진입 패드·서명/인증 짝을 CFG 로 검사 — 손 asm 이 섞인 이미지에서 정확히 4개 함수가 걸렸다. 런타임 확인은 `/proc/cpuinfo` 의 `paca pacg bti` 와 실제 PACIA 실행으로 키가 켜졌는지까지.
</details>

<details><summary>Q7. 꼬리: `-mbranch-protection=standard` 바이너리를 PAC 이 없는 Cortex-A53 에서 돌리면?</summary>

`paciasp`/`autiasp`/`bti c` 는 HINT 공간이라 NOP 로 실행된다 — 그래서 배포판이 기본으로 켤 수 있다. 하지만 컴파일러가 `retaa` 를 냈다면 v8.3 이전 코어에선 정의되지 않은 명령 예외다. 이번 실측에서 gcc/일반 clang 은 `autiasp; ret`, Apple clang(M 시리즈 CPU 타깃)은 `retaa` 를 냈다. 옛 코어까지 지원하는 이미지는 `-march=armv8-a` 계열 타깃으로 빌드하고 린터로 `retaa` 부재를 확인한다.
</details>

<details><summary>Q8. 펌웨어(Cortex-M)에서는 어떻게 되나요?</summary>

Armv8.1-M PACBTI 확장(Cortex-M85 등). 32비트 주소에는 남는 비트가 없으니 PAC 을 **r12** 에 계산해 LR 과 함께 스택에 push 한다: `pacbti r12, lr, sp` → `push {…, r12, lr}` … `pop {…, r12, lr}` → `aut r12, lr, sp` → `bx lr` (Apple clang 21 `-mcpu=cortex-m85 -mbranch-protection=standard` 실측). 리프는 `bti`. 키는 특권 레지스터(PAC_KEY_P/U)에 부팅 코드가 넣어야 하고, 예외 진입/RTOS 컨텍스트 스위치에서 r12·키·CONTROL 의 PAC_EN/BTI_EN 을 다뤄야 한다 (문서 확인 권장). 즉 RTOS 포트(ARM-A01 의 PendSV)와 부트로더가 같이 바뀌어야 한다.
</details>

<details><summary>Q9. 이 설계를 당신이 바꾼다면? (스태프급)</summary>

(1) 반환 주소: PAC-RET + PC 모디파이어(PAuth_LR) 또는 하드웨어 섀도 스택(GCS) — PAC 비트가 7개뿐인 48비트 VA 시스템에서는 GCS 가 무차별 대입에 강하다. (2) 전방 간선: BTI 는 거친(coarse) CFI — `bti c` 가 있는 *아무* 함수로나 갈 수 있다. 함수 포인터 서명(arm64e 식) 또는 타입 기반 CFI(kCFI)를 겹친다. (3) 운영: fork 서버의 키 공유를 피하고, PAC 실패를 크래시 리포트에서 별도 분류해 공격 징후로 감시한다. (4) 툴체인: 손 asm·서드파티 오브젝트까지 노트 검사를 CI 에. 틀리는 경우: JIT 코드(서명 규칙을 JIT 가 지켜야 함, ARM-A04), 서명 키가 같은 다른 프로세스와 포인터를 공유하는 경우(Mac 은 exec 후 IA 키가 유지됐다 — 관찰).
</details>

## 9. 추가 연습

### 연습 1 — GNU property 노트 파서와 링커 AND

`parse_gnu_property(sec, len, &feat)` — `.note.gnu.property` 섹션 바이트에서 `GNU_PROPERTY_AARCH64_FEATURE_1_AND` 값을 꺼낸다 (ELF64 노트: namesz/descsz/type, 이름 4바이트 정렬, property 는 8바이트 정렬). `link_features()` 는 노트 없는 오브젝트를 0 으로 쳐서 AND, `force_bti` 면 BTI 를 OR.

<details><summary>답</summary>

VM 의 실제 바이트 (`readelf -x .note.gnu.property forge_std`): `04000000 10000000 05000000 474e5500 | 000000c0 04000000 07000000 00000000` → feature 7 = BTI|PAC|GCS. `bti_note` 는 3. 링크 규칙을 run_vm.sh 결과와 맞춰 본다: 7 & 3 = 3 ("BTI, PAC"), 노트 없는 `.S` → 0 (노트 섹션 자체가 없음), `-z force-bti` → 1 ("BTI" + 경고). 잘린 섹션·이름 불일치·크기 초과는 0.
</details>

### 연습 2 — 실측값으로 PAC 필드 추정

`infer_pac_field(raw, sig[], n, &va_bits, &tbi)` — 같은 원본을 여러 modifier 로 서명한 값들의 XOR 를 OR 해서 최하위 비트 = VA 비트 수, 상위 바이트가 비면 TBI.

<details><summary>답</summary>

`probe.txt` 의 Mac arm64e pacia/pacib 9개 → va_bits 47, TBI 꺼짐. VM 6개 → 48, TBI 켜짐. Mac arm64 → 차이 0 → "키 꺼짐". 함정: Mac pacda 3개는 우연히 비트 47 이 모두 0 이라 **48 로 과소추정** — 표본이 적으면 필드가 좁아 보인다. 4096개 modifier 로 잰 마스크 `0x007f800000000000` 가 정답(47). 실측 기반 결론에는 표본 수를 같이 말해야 한다.
</details>

### 연습 3 — 무차별 대입 비용과 fork

`tries_for(w, p)` = ⌈log(1−p) / log(1−2^−w)⌉, `mean_tries_same_key(w, N)` 장난감 실험.

<details><summary>답</summary>

| PAC 비트 | 50% 까지 시도 | 평균 |
|---|---|---|
| 7 (VM) | 89 | 128 (실험 128.0) |
| 16 (Mac arm64e 코드) | 45426 | 65536 |
| 24 (39비트 VA, TBI 꺼짐) | 11629080 | 2^24 |

`fork_keys.txt` 실측: VM 은 fork 자식이 부모와 같은 pacia/pacib, exec 후 둘 다 다름. Mac arm64e 는 fork 같음, exec 후 **IB 만** 바뀌고 IA 는 같았다 (셸에서 새로 띄운 프로세스끼리는 IA 도 달랐다). 반환 주소(IB, `pacibsp`)는 exec 마다 새 키라는 점이 중요하다.
</details>

### 연습 4 — Cortex-M85 PACBTI 린터

`thumb_len(hw1)` (ARM-B12 규칙) + `m_lint(halfwords, n)` — 첫 명령이 `bti`(`f3af 800f`) 또는 `pacbti r12, lr, sp`(`f3af 800d`), PACBTI 로 시작했으면 모든 `bx lr`(`4770`) 앞에 `aut r12, lr, sp`(`f3af 802d`).

<details><summary>답</summary>

`gen_mac.txt` 의 cortex_m85 5함수는 모두 0. 변이: `aut` 를 `nop nop` 으로 → `MV_UNAUTH_RET`, 리프의 `bti` 제거 → `MV_NO_PAD`, `pacbti` 를 `bti` 로 → `aut` 가 서명 없이 실행 (`MV_UNSIGNED_AUT`). 선형 검사로 충분했던 이유: clang 이 M85 에서 낸 코드는 에필로그가 하나(`early_exit` 도 `b` 로 합류)였다 — 여러 에필로그가 생기면 Part B 처럼 CFG 가 필요하다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cd lessons/ARM/ARM-A10
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c shared_ref.S -o /tmp/ARM-A10_practice && /tmp/ARM-A10_practice   # 23/23 passed
```

## 10. 핵심 정리

- PAC-RET: `paciasp`(LR ← MAC(LR, SP, IA 키)) … `autiasp; ret` 또는 `retaa`. 비리프만(기본), 리프는 `+leaf`. arm64e 는 B 키(`pacibsp/retab`) + 함수 포인터 서명(`blraaz`).
- BTI: 간접 분기 목적지 = 착지 패드. BLR → BTYPE 10, BR x16/x17 → 01, 그 밖의 BR → 11. `bti c` = 01·10, `bti j` = 01·11, `paciasp` = 01·10 (Linux BT0=1). 실측 18칸.
- 실측: VM(FPAC) 위조 → SIGILL, Mac arm64e → SIGBUS. **같은 SP 재사용은 둘 다 통과.** Mac 의 arm64(비 e) 프로세스는 키가 꺼져 `standard` 빌드도 무방비.
- PAC 은 짧다: VM 7비트, Mac arm64e 코드 16 / 데이터 8비트. fork 는 키를 물려받는다 → 재시도 오라클 주의.
- BTI 는 링크된 **모든** 오브젝트에 GNU property 노트가 있어야 켜진다. 손 asm 이 구멍 (CI 게이트에서 4개).
- 린터는 줄 순서가 아니라 CFG 데이터 흐름: 축소 포장된 빠른 경로 `ret` 은 정상, 합류 상태 충돌은 버그.
- Cortex-M85: `pacbti r12, lr, sp` / `aut r12, lr, sp` — PAC 은 r12 에 담아 스택으로.

## 참고자료

- Arm Architecture Reference Manual for A-profile (DDI 0487) — FEAT_PAuth/PAuth2/FPAC/PAuth_LR, FEAT_BTI (BTYPE, SCTLR_ELx.BT), HINT 인코딩: https://developer.arm.com/documentation/ddi0487/latest
- Arm A64 ISA: PACIA/PACIASP, AUTIA, RETAA, BRAA, BTI: https://developer.arm.com/documentation/ddi0602/latest/Base-Instructions
- Linux kernel `Documentation/arch/arm64/pointer-authentication.rst`, `elf_hwcaps.rst` (paca/pacg/bti)
- Linux BTI 패치 (SCTLR_EL1.BTx 로 PACIxSP 의 BR 착지 제한): https://lkml.iu.edu/hypermail/linux/kernel/1910.1/03429.html
- QEMU `target/arm: Enable SCTLR_EL1.BT0 for aarch64-linux-user`: https://patchew.org/QEMU/20220505091147.2657652-1-peter.maydell@linaro.org/20220505091147.2657652-2-peter.maydell@linaro.org
- GCC `-mbranch-protection` (AArch64 Options), Clang `-mbranch-protection`; ELF `GNU_PROPERTY_AARCH64_FEATURE_1_AND` (AArch64 ELF ABI, sysvabi64): https://github.com/ARM-software/abi-aa
- Apple: Preparing your app to work with pointer authentication (arm64e): https://developer.apple.com/documentation/security/preparing-your-app-to-work-with-pointer-authentication
- Arm Cortex-M85 / Armv8.1-M PACBTI Extension: https://developer.arm.com/documentation/107656/latest (PACBTI 개요, 문서 확인 권장)
- 이 트랙: ARM-B08 (프레임 레코드, x29/x30), ARM-M14 (arm64e PAC 실패 크래시 덤프), ARM-B12 (Thumb 명령 길이), ARM-A04 (JIT·코드 패치, 같은 Advanced 묶음), 다음 단계: SEC 트랙 (CFI·섀도 스택)
