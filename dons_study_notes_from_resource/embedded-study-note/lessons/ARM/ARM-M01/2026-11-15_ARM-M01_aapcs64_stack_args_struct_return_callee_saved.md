---
id: ARM-M01
time: 80분
updated: 2026-11-15
---
# ARM-M01 AAPCS64 심화: 많은 인자·구조체 반환·callee-saved

## 학습 목표

- 인자 배정 알고리즘(NGRN · NSRN · NSAA)을 손으로 돌려서, 인자 10개짜리·int/char/short 가 섞인 시그니처의 **스택 오프셋을 바이트 단위로** 맞힌다. Apple(자연 정렬로 압축)과 표준 AAPCS64(8 B 슬롯)의 차이도 말할 수 있다.
- 구조체 반환의 세 경로를 구분한다. 16 B 이하면 `x0/x1`, HFA면 `s0–s3`, 그보다 크면 **x8 간접 결과**(호출자가 버퍼를 잡는다)다. 20 B 짜리 float 5개 인자는 복사본 포인터로 넘어간다는 것도 안다.
- callee-saved 집합(x19–x28, x29, sp, **d8–d15 의 하위 64비트만**)을 기계적으로 검증하는 **ABI 적합성 검사기**를 asm 으로 짠다. 일부러 규약을 어긴 함수 7개에서 정확한 비트마스크를 받아 낸다.
- 같은 `.S` 를 Mac(Mach-O, Apple ABI)과 Linux(ELF, 표준 ABI)에서 빌드하고, 다른 곳은 스택 인자 오프셋 4개뿐이라는 것을 실측으로 확인한다.

## 1. 문제

"struct 리턴은 어디로 가나요?", "9번째 인자는 sp 몇에 있죠?", "이 asm 함수가 d9 를 건드렸는데 문제가 되나요?" 같은 질문은 C 와 asm 을 섞는 펌웨어 면접에서 반드시 나오는 꼬리질문이다. 벤더 DSP·암호 라이브러리를 붙일 때나, RTOS 문맥 전환 코드를 읽을 때나, 크래시 덤프에서 인자를 복원할 때도 이 규칙을 모르면 손을 못 댄다.

ARM-B01 에서는 인자 8개 이하의 leaf 함수를, ARM-B08 에서는 프레임과 x19–x28 저장을, ARM-B11 에서는 FP 인자와 d8–d15 를 따로따로 봤다. 이 레슨은 그 셋을 **한 함수 호출 규약(AAPCS64)** 의 전체 그림으로 묶는다. 레지스터가 모자랄 때(스택 인자), 반환값이 레지스터에 안 들어갈 때(x8), 부동소수점 구조체(HFA)일 때, 그리고 "피호출자가 지켜야 할 것" 을 기계로 검사하는 법까지 다룬다.

```svg
<svg viewBox="0 0 750 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">인자 배정 = 카운터 3개 (Stage A: NGRN=0, NSRN=0, NSAA=sp)</text>
  <g stroke="currentColor">
    <rect x="10" y="34" width="730" height="26" class="f-mut"/>
    <rect x="10" y="60" width="730" height="26"/>
    <rect x="10" y="86" width="730" height="26"/>
    <rect x="10" y="112" width="730" height="26" class="f-warn"/>
    <rect x="10" y="138" width="730" height="26" class="f-warn"/>
    <rect x="10" y="164" width="730" height="26" class="f-warn"/>
    <rect x="10" y="190" width="730" height="26" class="f-warn"/>
    <rect x="10" y="216" width="730" height="26" class="f-ok"/>
    <rect x="10" y="242" width="730" height="26" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="52" font-weight="bold">인자</text><text x="190" y="52" font-weight="bold">규칙</text>
    <text x="420" y="52" font-weight="bold">Apple arm64</text><text x="590" y="52" font-weight="bold">표준 AAPCS64</text>
    <text x="18" y="78" font-family="monospace">long a0..a7</text><text x="190" y="78">C.9: NGRN 0→8</text>
    <text x="420" y="78" font-family="monospace">x0..x7</text><text x="590" y="78" font-family="monospace">x0..x7</text>
    <text x="18" y="104" font-family="monospace">(NGRN = 8)</text><text x="190" y="104">C.13: 이제부터 스택</text>
    <text x="420" y="104">NSAA = sp</text><text x="590" y="104">NSAA = sp</text>
    <text x="18" y="130" font-family="monospace">signed char c</text><text x="190" y="130">C.16: 8 B 미만 → 8 B</text>
    <text x="420" y="130" font-family="monospace">[sp+0]  1 B</text><text x="590" y="130" font-family="monospace">[sp+0]  8 B 슬롯</text>
    <text x="18" y="156" font-family="monospace">short s</text><text x="190" y="156">C.14: 정렬 max(8, 2)</text>
    <text x="420" y="156" font-family="monospace">[sp+2]  2 B</text><text x="590" y="156" font-family="monospace">[sp+8]</text>
    <text x="18" y="182" font-family="monospace">int i</text><text x="190" y="182">C.14 + C.16</text>
    <text x="420" y="182" font-family="monospace">[sp+4]  4 B</text><text x="590" y="182" font-family="monospace">[sp+16]</text>
    <text x="18" y="208" font-family="monospace">long l</text><text x="190" y="208">C.17: NSAA 에 복사</text>
    <text x="420" y="208" font-family="monospace">[sp+8]  8 B</text><text x="590" y="208" font-family="monospace">[sp+24]</text>
    <text x="18" y="234" font-family="monospace">quat a, quat b</text><text x="190" y="234">C.2: HFA, NSRN+4 ≤ 8</text>
    <text x="420" y="234" font-family="monospace">s0–s3, s4–s7</text><text x="590" y="234" font-family="monospace">s0–s3, s4–s7</text>
    <text x="18" y="260" font-family="monospace">결과 &gt; 16 B</text><text x="190" y="260">Result return</text>
    <text x="420" y="260" font-family="monospace">x8 = 버퍼 주소</text><text x="590" y="260" font-family="monospace">x8 = 버퍼 주소</text>
  </g>
  <text x="10" y="292" fill="currentColor" font-size="12">노란 줄 = mixed_stack(long a0..a7, signed char c, short s, int i, long l) 의 실제 오프셋 (clang -S 실측, 5장).</text>
  <text x="10" y="312" fill="currentColor" font-size="12">Apple 은 C.16 의 "8 B 로 키우기" 를 하지 않는다 → 스택 인자 16 B, 표준은 32 B. 두 ABI 가 다른 곳은 여기뿐.</text>
</svg>
```

### Part A — 인자와 반환값의 모든 경로 (asm 6개)

- `long sum10(long a0, …, long a9)`: `a8 = [sp]`, `a9 = [sp+8]` (진입 순간 sp). leaf 라 프레임이 없다.
- `long mixed_stack(long a0..a7, signed char c, short s, int i, long l)` → `a7 + c + s + i + l`. Apple 오프셋 0/2/4/8 에서 `ldrsb/ldrsh/ldrsw/ldr` 로 읽는다. 같은 파일이 Linux 에선 0/8/16/24 를 쓰도록 `#ifdef __APPLE__` 매크로 4개로 처리한다.
- `qr16 divmod_pair(int64_t a, int64_t b)`: 16 B 구조체 `{q, r}` 를 `x0, x1` 로 반환한다. `SDIV` + `MSUB` 를 쓴다(ARM-B07).
- `imu_sample make_imu(uint64_t ts, int32_t ax, ay, az)`: 32 B 구조체다. **호출자가 준 x8** 에 직접 쓴다.
- `quat quat_mul(quat a, quat b)`: `struct {float w,x,y,z;}` 는 HFA 다. a 는 `s0–s3`, b 는 `s4–s7` 로 들어오고 결과는 `s0–s3` 로 나간다(해밀턴 곱, 로봇 자세 계산).
- `float f5_sum(f5 v)`: float 5개는 HFA 가 아니고(멤버 4개 초과) 20 B > 16 이다. 그래서 **호출자가 만든 복사본의 포인터가 x0** 으로 온다.

### Part B — ABI 적합성 검사기 `abi_call_check` + 주입된 위반

- `uint32_t abi_call_check(const void *fn, const abi_args *a, abi_ret *r)`: x19–x28 과 d8–d15 에 센티널을 심는다. `a` 로 x0–x8, d0–d7, 스택 인자 16 B 를 채워 `fn` 을 부르고, 돌아오면 바뀐 레지스터를 마스크로 돌려준다(bit 0–9 x19–x28, 10–17 d8–d15, 18 sp, 19 x29).
- 주어진 시험 대상 `shared_violators.S` 로 검사한다. **위반이 아닌 것**은 0 이 나와야 한다: caller-saved 를 전부 망가뜨린 leaf, **v8 상위 64비트만** 바꾼 함수, 제대로 저장하는 함수, clang 이 만든 `c_pressure`. **위반**은 정확한 마스크가 나와야 한다: x19 덮기, d9 덮기, `s12` 만 쓰기(→ d12 도 바뀜), x28+d15, 저장 후 **복원 순서 뒤바꿈**, sp 를 16 내리고 복귀, x29 망가뜨리기.
- 검사기로 검사기 자신을 검사한다(중첩 호출 → 바깥 마스크 0, 안쪽 결과 = x19 비트).
- Apple 압축 배치를 모르고 표준 8 B 슬롯으로 짠 `vio_mixed_aapcs` 는 보존 규칙은 다 지키지만 **틀린 값**을 낸다. 검사기로는 못 잡고 값 테스트로만 잡힌다.

### Part C — 브링업 감사(audit) 게이트

- 펌웨어 팀이 벤더 asm(센서 퓨전 `quat_mul`, IMU 패킷 `make_imu` 등)을 받았을 때 통합 전에 돌리는 CI 게이트를 만든다. Part A 의 모든 시그니처(스택 인자, x8, HFA, 포인터 인자)를 **같은 검사기 하나로** 부르고 결과 표를 찍는다. RTOS 문맥 전환이 callee-saved 만 저장해도 되는 근거가 바로 이 규칙이라는 것도 연결한다.

## 2. Problem (English)

> **AAPCS64 deep dive (AArch64, Apple arm64 + standard ELF).**
>
> Implement in assembly (`.S`, called from C):
>
> 1. `long sum10(long a0, …, long a9)` — args 9–10 arrive on the stack at entry `[sp]`, `[sp+8]`.
> 2. `long mixed_stack(long a0..a7, signed char c, short s, int i, long l)` returning `a7 + c + s + i + l`. On Apple platforms stack arguments are packed by natural size/alignment (`c@0, s@2, i@4, l@8`); the standard ABI gives each an 8-byte slot (`0, 8, 16, 24`). One source file must work on both.
> 3. `qr16 divmod_pair(int64_t a, int64_t b)` returning `{a/b, a%b}` in `x0`, `x1` (b == 0 and INT64_MIN/−1: ISA-defined results, no trap).
> 4. `imu_sample make_imu(uint64_t ts, int32_t ax, int32_t ay, int32_t az)` — a 32-byte struct returned through the caller-provided buffer in `x8`.
> 5. `quat quat_mul(quat a, quat b)` with `typedef struct {float w,x,y,z;} quat;` (an HFA: `s0–s3`, `s4–s7` → `s0–s3`).
> 6. `float f5_sum(f5 v)` with `typedef struct {float v[5];} f5;` (not an HFA; passed by reference to a caller-made copy).
> 7. `uint32_t abi_call_check(const void *fn, const abi_args *a, abi_ret *r)`: load sentinels into x19–x28 and d8–d15, load `x0–x8`, `d0–d7` and 16 bytes of stack args from `*a`, call `fn`, store `x0, x1, d0–d3` into `*r`, and return a bitmask of callee-saved registers that changed. Compare only the low 64 bits of v8–v15. Detect and recover from a callee that returns with a wrong `sp` *or* a wrong `x29`.
>
> Constraints: don't touch x18; keep `sp` 16-byte aligned; the checker itself must be ABI-conformant (it gets checked by itself).
>
> Example: `abi_call_check(vio_x28_d15, …) == (1u<<9) | (1u<<17)`; a function that only writes `v8.d[1]` → `0`; `fmov s12, #1.0` → bit for d12.
>
> Follow-ups: Who allocates the x8 buffer and must the callee return its address? What happens to a 5-float struct? Why are only the low 64 bits of v8–v15 preserved? How does Apple's variadic convention differ?

## 3. 시작 코드

하네스 (starter / solution 공용, Mac 에서 테스트 41개, Linux 에선 Apple 전용 2개를 빼고 39개):

<!-- code: harness.c -->

주어진 시험 대상 (TODO 아님). 착한 함수 3개, 위반 7개, 표준 배치로 짠 `vio_mixed_aapcs`:

<!-- code: shared_violators.S -->

여기를 채운다:

<!-- code: starter.S -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c starter.S shared_violators.S -o starter && ./starter
```

실제 출력(줄임). 스텁은 레지스터만 0 으로 만들고 바로 `ret` 하거나, x8 버퍼를 0 으로 채우거나, `0xFFFFFFFF` 를 돌려준다. 그래서 멈추거나 메모리를 깨지 않는다.

```text
FAIL harness.c:104  sum10(1, 2, 3, 4, 5, 6, 7, 8, 9, 10) == 55
FAIL harness.c:105  sum10(0, 0, 0, 0, 0, 0, 0, 0, 1L << 40, 0) == 1L << 40
… (Part A 19줄 더)
FAIL harness.c:171  abi_call_check((const void *)vio_good_leaf, &A, &R) == 0 && R.x0 == 42
… (Part B 11줄 더)
  sum10              mask=0xffffffff expect=0x00000 MISMATCH
… (감사 표 나머지 15줄 전부 MISMATCH + 그 사이 FAIL 6줄 + info 1줄)
FAIL harness.c:250  g_audit_bad == 0
1/41 passed
```

통과한 1개는 `good != bad` 다(스텁 mixed_stack 의 0 ≠ vio_mixed_aapcs 의 쓰레기 값). `.S` 는 C 전처리기를 거치므로 주석은 `//` 와 `/* */` 만 쓴다. `SYM(x)` 매크로는 Mach-O 에선 `_x`, ELF 에선 `x` + `.type`/`.size` 로 펼쳐진다(ARM-B01).

## 4. 힌트

<details>
<summary>힌트 1 — 스택 인자는 "진입 순간의 sp" 기준</summary>

leaf 함수가 sp 를 안 건드리면 `[sp]` 가 바로 NSAA 의 시작이다. `ldp x9, x10, [sp]` 한 줄로 a8, a9 를 읽는다. 프레임을 먼저 만들었다면 오프셋에 프레임 크기를 더해야 한다(`[x29, #16]` 부터 시작하는 식). clang 이 만든 호출자는 `stp x9, x8, [sp]` 로 9, 10 을 쓴다(5장).

</details>

<details>
<summary>힌트 2 — 1·2·4 B 스택 인자는 "그 크기만큼만" 써 있다</summary>

Apple 호출자는 `strb w10, [sp]` / `strh w9, [sp, #2]` / `str w8, [sp, #4]` 로 저장한다(실측). 나머지 바이트는 이전 호출이 남긴 쓰레기다. `ldr x` 로 8 B 를 읽으면 안 되고, `ldrsb x9, [sp]` 처럼 **부호 확장 로드**를 써야 한다. 오프셋은 `#define MS_C 0 …` 로 빼 두면 Linux 판은 매크로만 바꾸면 된다.

</details>

<details>
<summary>힌트 3 — x8 은 "주소가 들어 있는 인자" 일 뿐</summary>

`make_imu` 는 `str x0, [x8]`, `stp w1, w2, [x8, #8]` … 처럼 쓰기만 하고 `ret` 한다. x0 에 주소를 돌려줄 의무는 없다(AAPCS64: "there is no requirement for the callee to preserve the value stored in x8"). int32 인자의 x 레지스터 윗 32비트는 unspecified 다. 그래서 체크섬은 `add x10, x0, w1, sxtw` 로 확장하며 더한다.

</details>

<details>
<summary>힌트 4 — HFA 는 멤버마다 레지스터 하나</summary>

`quat a` 는 `s0,s1,s2,s3`, `quat b` 는 `s4..s7` 이다. 임시는 **v16–v31** 을 쓰면 저장할 게 없다. v8–v15 를 쓰면 하위 64비트를 저장해야 한다. `fmsub s16, s1, s5, s16` = `s16 − s1·s5` 다. `f5` 는 HFA 가 아니라서 `x0` 이 복사본 주소다.

</details>

<details>
<summary>힌트 5 — 검사기 뼈대</summary>

프레임 176 B 의 구성은 다음과 같다. 레코드 16, x19–x28 80, d8–d15 64, `r` 포인터와 "자기 주소 표지" 16. 그 아래에 `sub sp, sp, #16` 으로 나가는 인자 영역을 둔다. fn 주소와 a 는 x16/x17 에 둔다. 인자를 채울 때는 x0/x1 을 **마지막에** 로드한다(a 를 읽는 동안 덮지 않도록). 돌아오면 `sp + 16 == x29` 인지부터 본다. 다르면 표지(`[base+168] == base`)를 보고 sp 와 x29 중 맞는 쪽을 찾아 다른 쪽을 복구한다. d 비교는 `fmov x13, d8` 로 하위 64비트만 꺼낸다.

</details>

## 5. 풀이

### Part A — 스택 인자: 실제 바이트로 확인

`demo_stack.c` 는 `sum10`, `mixed_stack`, `make_imu` 를 한 번씩 부르는 최소 프로그램이다. lldb 로 각 함수의 첫 명령에서 멈춘다.

```bash
cc -std=c11 -g -O1 demo_stack.c solution.S -o /tmp/ARM-M01_stack
lldb -b -s demo_stack.lldb /tmp/ARM-M01_stack        # 전체 기록: demo_stack_lldb.txt
```

```text
(lldb) register read sp x0 x7                       ← sum10 진입
      sp = 0x000000016fdfea00
      x0 = 0x0000000000000001
      x7 = 0x0000000000000008
(lldb) memory read -fx -s8 -c2 $sp
0x16fdfea00: 0x0000000000000099 0x0000000000001010
(lldb) register read sp x7                          ← mixed_stack 진입
      sp = 0x000000016fdfea00
      x7 = 0x0000000000000008
(lldb) memory read -fx -s1 -c16 $sp
0x16fdfea00: 0xfd 0x00 0x18 0xfc 0x60 0x79 0xfe 0xff
0x16fdfea08: 0x89 0x67 0x45 0x23 0x01 0x00 0x00 0x00
```

`sum10(…, 0x99, 0x1010)` 은 예상대로 `[sp] = 0x99`, `[sp+8] = 0x1010` 이다. `mixed_stack(…, -3, -1000, -100000, 0x123456789)` 의 16 바이트는 리틀 엔디언으로 이렇게 읽힌다.

```svg
<svg viewBox="0 0 750 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mixed_stack 진입 순간 [sp..] — Apple (lldb 실측) vs 표준 AAPCS64 (Linux clang -S)</text>
  <text x="10" y="50" fill="currentColor">Apple</text>
  <g stroke="currentColor">
    <rect x="70" y="34" width="40" height="28" class="f-acc"/>
    <rect x="110" y="34" width="40" height="28" class="f-mut"/>
    <rect x="150" y="34" width="80" height="28" class="f-ok"/>
    <rect x="230" y="34" width="160" height="28" class="f-warn"/>
    <rect x="390" y="34" width="320" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="78" y="53">fd</text><text x="118" y="53">00</text><text x="158" y="53">18 fc</text>
    <text x="238" y="53">60 79 fe ff</text><text x="398" y="53">89 67 45 23 01 00 00 00</text>
    <text x="70" y="78">+0</text><text x="110" y="78">+1</text><text x="150" y="78">+2</text>
    <text x="230" y="78">+4</text><text x="390" y="78">+8</text><text x="700" y="78">+16</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="70" y="96">c=-3</text><text x="110" y="96">패딩</text><text x="150" y="96">s=-1000</text>
    <text x="230" y="96">i=-100000</text><text x="390" y="96">l=0x123456789</text>
  </g>
  <text x="10" y="140" fill="currentColor">표준</text>
  <g stroke="currentColor">
    <rect x="70" y="124" width="160" height="28" class="f-acc"/>
    <rect x="230" y="124" width="160" height="28" class="f-ok"/>
    <rect x="390" y="124" width="160" height="28" class="f-warn"/>
    <rect x="550" y="124" width="160" height="28" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="78" y="143">strb c, [sp]</text><text x="238" y="143">strh s, [sp,#8]</text>
    <text x="398" y="143">str w, [sp,#16]</text><text x="558" y="143">str x, [sp,#24]</text>
    <text x="70" y="168">+0</text><text x="230" y="168">+8</text><text x="390" y="168">+16</text><text x="550" y="168">+24</text>
    <text x="700" y="168">+32</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="10" y="200">Apple: 스택 인자 16 B (호출자 sub sp, #32 = 인자 16 + 레코드 16). 표준: 32 B (sub sp, #48).</text>
    <text x="10" y="222">+1 의 00 은 "unspecified" 패딩 — 여기선 직전 sum10 호출이 남긴 0x99 의 둘째 바이트다.</text>
    <text x="10" y="244">표준 배치로 짠 asm(vio_mixed_aapcs)을 Apple 에서 부르면 [sp+8] 에서 s 를 읽는다 → l 의 하위 16비트 0x6789.</text>
    <text x="10" y="266">[sp+16], [sp+24] 는 호출자 프레임(저장된 fp/lr 등)이라 i, l 은 실행마다 다른 쓰레기가 된다.</text>
  </g>
</svg>
```

이 배치는 clang 이 직접 만든 호출자 코드로 증명된다(`bash demo_abi.sh` → `demo_abi_apple_vs_linux.txt`, `call_mixed` 발췌).

```text
// --target=arm64-apple-macos14            // --target=aarch64-linux-gnu
sub   sp, sp, #32                          sub   sp, sp, #48
str   x8, [sp, #8]      // l               str   x8, [sp, #24]     // l
str   w8, [sp, #4]      // i               str   w9, [sp, #16]     // i
strh  w9, [sp, #2]      // s               strh  w8, [sp, #8]      // s
strb  w10, [sp]         // c               strb  w10, [sp]         // c
bl    _mixed_stack                         bl    mixed_stack
```

피호출자도 마찬가지다. 같은 파일의 C 판 `callee_mixed` 를 Apple 로 컴파일하면 `ldrsb w10, [sp]` / `ldrsh w11, [sp, #2]` / `ldr w9, [sp, #4]` / `ldr x8, [sp, #8]` 이 나온다. Linux 로는 `[sp]`, `[sp, #8]`, `[sp, #16]`, `[sp, #24]` 다. 표준 쪽은 C.16 이 "8 B 미만이면 크기를 8 B 로" 라고 하므로 슬롯이 8 B 다. Apple 문서는 이렇게 쓴다. *"Function arguments may consume slots on the stack that are not multiples of 8 bytes. If the total number of bytes for stack-based arguments is not a multiple of 8 bytes, insert padding on the stack to maintain the 8-byte alignment requirements."*

부호 확장 주의: 스택의 char 는 1 B 만 써 있다. Apple 에서 "32비트 미만 인자는 호출자가 확장한다" 는 규칙(ARM-B01)은 **레지스터** 인자 이야기다. 스택 슬롯에는 확장된 4 B 가 없으므로 피호출자가 `ldrsb` 해야 한다. 실제로 clang 피호출자도 그렇게 한다. 테스트 `c=-128, s=-32768, i=INT32_MIN` 이 `ldrb`(0 확장) 실수를 잡는다.

`sum10` 추적(호출자가 `stp x9, x8, [sp]` 로 9, 10 을 썼다):

| # | 명령 | x0 | x2 | x4 | x9 | x10 |
|---|---|---|---|---|---|---|
| 1 | `add x0, x0, x1` | 3 | 3 | 5 | | |
| 2 | `add x2, x2, x3` / `add x4, x4, x5` / `add x6, x6, x7` | 3 | 7 | 11 | | |
| 3 | `ldp x9, x10, [sp]` | | | | 9 | 10 |
| 4 | `add x0, x0, x2` / `add x4, x4, x6` / `add x9, x9, x10` | 10 | | 26 | 19 | |
| 5 | `add x0, x0, x4` / `add x0, x0, x9` | **55** | | | | |

(x6 = 7 + 8 = 15, 그래서 x4 = 11 + 15 = 26.) 덧셈을 트리 모양으로 묶은 것은 의존 사슬을 짧게 하려는 습관이다. 정확성과는 상관없다.

### Part A — 구조체 반환: 세 갈래

AAPCS64 "Result return" 의 규칙은 한 줄이다. **"`void func(T arg)` 로 넘길 때 레지스터에 들어가면 같은 레지스터로 반환, 아니면 x8"**. 그래서 인자 규칙을 알면 반환 규칙도 따라 나온다.

| 타입 | 크기 | 인자로는 | 반환은 | 이 레슨 |
|---|---|---|---|---|
| `struct {int64 q, r;}` | 16 | C.12: x0, x1 | x0, x1 | `divmod_pair` |
| `struct {int32 r, g, b;}` | 12 → 16 (B.5) | x0 = g:r, x1 = b | 같음 | 연습 1 `make_rgb` |
| `struct {float w,x,y,z;}` HFA | 16 | C.2: s0–s3 | s0–s3 | `quat_mul` |
| `struct {float v[5];}` | 20 | B.4: 복사본 포인터 → x0 | x8 버퍼 | `f5_sum`, 연습 2 |
| `imu_sample` | 32 | B.4: 포인터 | x8 버퍼 | `make_imu` |

clang 실측(Apple, `demo_abi_apple_vs_linux.txt`):

```text
_call_big:                         // big32 b = make_big(5);  (32 B)
    sub   sp, sp, #48
    ...
    mov   x8, sp                   // 결과 버퍼 = 호출자 프레임
    mov   w0, #5
    bl    _make_big
    ldr   x8, [sp]                 // 버퍼에서 읽는다 — x0 은 안 본다
    ldr   x9, [sp, #24]
_call_f5make:                      // f5 r = f5_make(1.5f);  (20 B, 정렬 4)
    add   x8, sp, #12              // 버퍼 주소가 16 정렬이 아니다 (타입 정렬 4 만 보장)
    fmov  s0, #1.50000000
    bl    _f5_make
    ldr   s0, [sp, #28]            // r.v[4] = 버퍼 + 16
_call_f5:                          // f5_sum(*p)  (20 B 인자)
    ldr   q0, [x0]                 // 16 B 복사
    str   q0, [sp]
    ldr   w8, [x0, #16]            // 나머지 4 B
    str   w8, [sp, #16]
    mov   x0, sp                   // 인자 = 복사본 주소
    bl    _f5_sum
```

세 가지를 읽어 낼 수 있다. (1) 호출자는 `bl` 뒤에 x0 을 보지 않고 자기 버퍼를 읽는다. 그래서 `make_imu` 는 x0 에 아무것도 안 돌려줘도 된다. x86-64 SysV 는 sret 주소를 rax 로 돌려줘야 하는 것과 다른 점이다. (2) x8 버퍼는 **타입 정렬만** 보장된다(`add x8, sp, #12`). 피호출자가 "16 정렬이겠지" 하고 정렬을 요구하는 명령을 쓰면 안 된다. 일반 메모리에서 AArch64 의 `stp/ldp` 는 비정렬도 허용되지만, Device 메모리나 MMU 가 꺼진 부트 코드에선 다르다(ARM-M13). (3) 20 B 인자는 **호출자가** 복사한다. 피호출자는 그 복사본을 마음대로 바꿔도 원본에 영향이 없다.

lldb 로 본 x8 (같은 기록 파일):

```text
(lldb) register read x8 sp x0 w1                   ← make_imu 진입
      x8 = 0x000000016fdfea20
      sp = 0x000000016fdfea00
      x0 = 0x000000000000004d
      w1 = 0xffffffff
(lldb) finish
(lldb) memory read -fx -s4 -c8 &m
0x16fdfea20: 0x0000004d 0x00000000 0xffffffff 0x00000002
0x16fdfea30: 0xfffffffd 0x00000005 0x0000004b 0x00000000
```

```svg
<svg viewBox="0 0 750 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">x8 간접 결과: 호출자 main 의 지역 변수 m 이 곧 결과 버퍼 (lldb 실측)</text>
  <g stroke="currentColor">
    <rect x="20" y="40" width="300" height="30" class="f-mut"/>
    <rect x="20" y="70" width="150" height="30" class="f-acc"/><rect x="170" y="70" width="75" height="30" class="f-acc"/><rect x="245" y="70" width="75" height="30" class="f-acc"/>
    <rect x="20" y="100" width="75" height="30" class="f-acc"/><rect x="95" y="100" width="75" height="30" class="f-ok"/><rect x="170" y="100" width="150" height="30" class="f-acc"/>
    <rect x="20" y="130" width="300" height="30" class="f-mut"/>
    <rect x="20" y="160" width="150" height="30" class="f-warn"/><rect x="170" y="160" width="150" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="59">main 의 저장 레지스터 / 레코드</text>
    <text x="28" y="89">ts = 0x4d</text><text x="178" y="89">ax=-1</text><text x="253" y="89">ay=2</text>
    <text x="28" y="119">az=-3</text><text x="103" y="119">flags=5</text><text x="178" y="119">sum = 0x4b</text>
    <text x="28" y="149">(printf 용 공간)</text>
    <text x="28" y="179">스택 인자 슬롯 [sp]</text><text x="178" y="179">[sp+8]</text>
    <text x="330" y="89">0x16fdfea20 ← x8</text>
    <text x="330" y="119">0x16fdfea30</text>
    <text x="330" y="179">0x16fdfea00 ← sp</text>
  </g>
  <g stroke="currentColor" fill="none">
    <path d="M560 85 C 600 85, 600 140, 560 150"/><path d="M567 144 L560 150 L569 153"/>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="470" y="60">clang: add x8, sp, #0x20</text>
    <text x="470" y="170">make_imu 는 [x8+0..31] 에 쓰기만,</text>
    <text x="470" y="188">x0 은 반환하지 않는다</text>
    <text x="10" y="222">copy elision: main 은 m 자체를 버퍼로 넘겼다(&amp;m == x8). 그래서 반환 후 복사가 없다.</text>
    <text x="10" y="244">flags = 5 = ax&lt;0 (bit0) | az&lt;0 (bit2). sum = 77 - 1 + 2 - 3 = 75 = 0x4b.</text>
    <text x="10" y="266">w1 = 0xffffffff 이지만 x1 윗 32비트는 unspecified → 체크섬은 add x10, x0, w1, sxtw 로 확장.</text>
  </g>
</svg>
```

HFA `quat_mul` 은 레지스터만 쓴다. a 의 네 멤버가 `s0..s3`, b 가 `s4..s7` 이고, NSRN 이 0 → 4 → 8 로 정확히 다 찬다(clang 호출자: `fmov s0, #1.0 … fmov s7, #8.0`). 계산은 `fmul` 하나와 `fmadd/fmsub` 셋을 성분 4개에 반복한다. 테스트 값은 −8..8 정수라서 곱과 합이 float 에서 정확하다. 그래서 fused(한 번 반올림)든 아니든 C 기준 모델과 비트까지 같다. 실제로 clang 은 Apple 에서 C 의 `h + f * g` 를 `fmadd` 로 합쳤다(`c_pressure` asm, `-ffp-contract=on` 기본값으로 보임 — 확인 권장). 일반 실수값이었다면 오차 허용치가 필요했다.

### Part B — 검사기: 센티널을 심고, 부르고, 비교한다

ARM-B08 의 `call_preserve_check`(x19–x28)와 ARM-B11 의 `cs_check_call`(d8–d15)을 하나로 합치고, 인자 묶음 `abi_args` 를 받아 **어떤 시그니처든** 부를 수 있게 한 판이다.

```svg
<svg viewBox="0 0 750 340" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">abi_call_check 프레임 (176 B + 나가는 인자 16 B), 위가 높은 주소</text>
  <g stroke="currentColor">
    <rect x="20" y="36" width="260" height="28" class="f-mut"/>
    <rect x="20" y="64" width="260" height="28" class="f-acc"/>
    <rect x="20" y="92" width="260" height="56" class="f-ok"/>
    <rect x="20" y="148" width="260" height="56" class="f-ok"/>
    <rect x="20" y="204" width="130" height="28" class="f-acc"/><rect x="150" y="204" width="130" height="28" class="f-warn"/>
    <rect x="20" y="232" width="260" height="28" class="f-mut"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="54">호출자(C) 프레임</text>
    <text x="28" y="82">+160 r 포인터 | +168 표지</text>
    <text x="28" y="116">+96..+159 호출자 d8..d15</text><text x="28" y="134">(하위 64비트만 = stp d)</text>
    <text x="28" y="172">+16..+95 호출자 x19..x28</text><text x="28" y="190">(10 × 8 B)</text>
    <text x="28" y="222">+0 x29</text><text x="158" y="222">+8 x30</text>
    <text x="28" y="250">[sp] [sp+8] = a-&gt;stk[0..1]</text>
    <text x="290" y="222">← x29</text>
    <text x="290" y="250">← sp (= x29-16)</text>
  </g>
  <g fill="currentColor" font-size="12">
    <text x="420" y="60" font-weight="bold">호출 직전 레지스터</text>
    <text x="420" y="80" font-family="monospace">x19..x28 = 0xC5C5_0000_0000_00nn</text>
    <text x="420" y="98" font-family="monospace">d8..d15  = 0x5EED_0000_0000_00nn</text>
    <text x="420" y="116" font-family="monospace">x0..x7, x8, d0..d7 ← *a</text>
    <text x="420" y="134" font-family="monospace">x16 = fn, blr x16</text>
    <text x="420" y="166" font-weight="bold">돌아온 뒤</text>
    <text x="420" y="186">sp+16 == x29 ? → 정상</text>
    <text x="420" y="204">[sp+16+168] == sp+16 → x29 깨짐 (bit19)</text>
    <text x="420" y="222">[x29+168] == x29 → sp 깨짐 (bit18)</text>
    <text x="420" y="240">둘 다 아님 → brk (복구 불가)</text>
  </g>
  <text x="10" y="284" fill="currentColor" font-size="12">표지 = [x29+168] 에 저장한 x29 자신의 값. 마스크: bit 0..9 = x19..x28, 10..17 = d8..d15, 18 = sp, 19 = x29.</text>
  <text x="10" y="306" fill="currentColor" font-size="12">센티널 하위 바이트 = 레지스터 번호 → "x20 값이 x19 로 갔다" 같은 뒤섞임(vio_swap_restore)도 두 비트로 드러난다.</text>
</svg>
```

설계에서 고민할 점 다섯 가지:

1. **검사기 자신도 피호출자다.** C 하네스의 x19–x28, d8–d15 를 덮어쓰므로 먼저 저장해야 한다. 이때 `stp d8, d9` 는 64비트씩만 저장하는데, 이것도 규약이 허용하는 범위다. 검사기를 검사기로 부르는 중첩 테스트가 이걸 증명한다(바깥 마스크 0).
2. **인자 로드 순서.** `a` 포인터(x17)로 x0–x7 을 채우는데, 원래 a 가 x1 에 있었으므로 미리 x17 로 옮겨 둔다. fn 은 x16 에 둔다. x16/x17(IP0/IP1)은 링커 veneer 가 쓰는 caller-saved 다. 우리 `blr x16` 사이에는 veneer 가 없으니 안전하다.
3. **d 는 하위 64비트만 비교.** `fmov x13, d8` 은 v8[63:0] 만 꺼낸다. 상위 64비트까지 비교하는 검사기를 만들면 `vio_v8_upper` 와 `vio_good_leaf` 가 오탐이 된다. 반대로 `fmov s12, #1.0` 은 **레지스터 전체를 다시 쓴다**(스칼라 FP 쓰기는 나머지 비트를 0 으로). 그래서 d12 의 상위 32비트도 바뀌고, 위반으로 잡혀야 맞다. `demo_szero.S` 실측: v16 을 `0xAAAA0000BBBB0000`(두 반쪽 모두)으로 채운 뒤 `fmov s16, #1.0` 하면 `d16=0x000000003f800000 upper=0x0000000000000000` 이 나온다.
4. **sp/x29 복구.** 둘 중 하나만 깨졌다면 프레임 안의 "자기 주소 표지"(`[x29+168] = x29`)로 어느 쪽이 맞는지 가릴 수 있다. `vio_fp` 는 `x29 = 0x1230` 이므로 x29 로는 메모리를 읽으면 안 된다. 그래서 sp 쪽부터 검사한다. `vio_sp` 는 sp 를 16 내렸으므로 `[sp+16+168]` 은 저장된 d15 칸을 가리키고, 그 값은 표지와 다르다. 그다음 `[x29+168]` 을 보면 일치한다.
5. **못 잡는 것.** 레지스터를 바꿨다가 돌아오기 전에 되돌리는 것은 합법이므로 잡을 필요가 없다. 특정 입력에서만 생기는 위반은 그 경로를 타는 인자를 줘야 보인다(커버리지 문제). 스택 인자 영역 너머(호출자 프레임)에 쓰는 버그, x18 사용, 비동기 신호 중 동작도 이 방법으로는 보이지 않는다.

실측 결과(solution 실행 출력의 감사 표):

```text
ABI audit (bit0-9 x19-x28, bit10-17 d8-d15, bit18 sp, bit19 fp):
  sum10              mask=0x00000 expect=0x00000 ok
  mixed_stack        mask=0x00000 expect=0x00000 ok
  info: mixed_stack=4886617350  vio_mixed_aapcs (8 B 슬롯 가정)=4377776337
  divmod_pair        mask=0x00000 expect=0x00000 ok
  make_imu (x8)      mask=0x00000 expect=0x00000 ok
  quat_mul (HFA)     mask=0x00000 expect=0x00000 ok
  f5_sum (ptr)       mask=0x00000 expect=0x00000 ok
  vio_good_leaf      mask=0x00000 expect=0x00000 ok
  vio_v8_upper       mask=0x00000 expect=0x00000 ok
  vio_good_saver     mask=0x00000 expect=0x00000 ok
  vio_x19            mask=0x00001 expect=0x00001 ok
  vio_d9             mask=0x00800 expect=0x00800 ok
  vio_s12            mask=0x04000 expect=0x04000 ok
  vio_x28_d15        mask=0x20200 expect=0x20200 ok
  vio_swap_restore   mask=0x00003 expect=0x00003 ok
  vio_sp             mask=0x40000 expect=0x40000 ok
  vio_fp             mask=0x80000 expect=0x80000 ok
41/41 passed
```

`vio_mixed_aapcs` 값은 실행마다 다르다. 세 번 돌렸을 때 `4340109521`, `4302508241`, `4376039633` 이 나왔다. `[sp+16]`, `[sp+24]` 가 호출자 프레임(ASLR 주소가 든 저장 fp/lr 등)이기 때문이다. 이 함수는 **보존 규칙은 완벽**하다(검사기 마스크 0). 그래도 값이 틀린다. "ABI 적합" 에는 인자 위치와 레지스터 보존 두 축이 있고, 검사기는 두 번째 축만 본다. 그래서 하네스는 값 테스트와 검사기를 둘 다 쓴다.

clang 이 만든 `c_pressure` 는 실제로 x19–x26 과 d8–d11 을 썼고 마스크 0 이 나왔다. 프롤로그를 보면 이렇다(`clang -S -O2 harness.c` 발췌).

```text
_c_pressure:
    stp   d11, d10, [sp, #-112]!   // d 만 저장 = 하위 64비트
    stp   d9, d8, [sp, #16]
    stp   x26, x25, [sp, #32]
    ...
    stp   x29, x30, [sp, #96]      // 프레임 레코드는 프레임 "위쪽" (ARM-B08 과 같은 관찰)
    add   x29, sp, #96
```

### Part C — 감사 게이트와 RTOS 연결

하네스의 Part C 는 위 표를 만드는 코드다. 시그니처마다 `abi_args` 를 다르게 채운다. sum10 은 `stk[0..1] = 9, 10`, mixed_stack 은 `pack_mixed()` 로 `c | s<<16 | i<<32` 를 한 워드에 넣는다(Apple 배치를 C 로 재현). make_imu 는 `x8 = &buf`, quat_mul 은 `d[k] = float 비트`(s 뷰 = d 의 하위 32비트), f5_sum 은 `x[0] = &copy` 다. 이것이 바로 펌웨어 브링업에서 벤더 바이너리를 받았을 때 "우리 컴파일러와 규약이 맞나?" 를 기계로 확인하는 방법이다. 소스가 없는 `.a` 에도 쓸 수 있다.

RTOS 와의 연결: 협조적 문맥 전환(함수 호출처럼 들어가는 `switch_to`)은 **callee-saved 만 저장하면 충분**하다. caller-saved 는 이미 호출자가 "깨져도 된다" 고 가정하고 있기 때문이다. Linux arm64 `cpu_switch_to` 가 x19–x28, fp, sp, lr 만 저장하는 이유가 이것이다(FP/SIMD 상태는 따로 lazy 하게 처리, 커널 소스 확인 권장). 그래서 asm 루틴 하나가 d9 를 몰래 깨면 **다른 태스크의 값**이 깨진 채로 돌아가는 디버깅 지옥이 된다. 비동기 인터럽트 진입은 반대로 caller-saved 까지 전부 저장해야 한다(ARM-M09, ARM-A02).

#### 같은 `.S` 를 Linux(ELF, 표준 ABI)에서

```bash
limactl shell elinux -- bash -lc 'mkdir -p /tmp/ARM-M01_work && cd /tmp/ARM-M01_work && \
  cp <lesson>/{harness.c,solution.S,shared_violators.S} . && \
  gcc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_violators.S -o t_gcc && ./t_gcc'
```

실측: gcc 15.2 → `39/39 passed`, clang 21.1 → `39/39 passed`, gcc ASan/UBSan → `39/39 passed`. Apple 전용 테스트 2개(`pack_mixed`, `vio_mixed_aapcs`)는 `#ifdef __APPLE__` 로 빠진다. 두 플랫폼에서 다른 것은 `MS_C/MS_S/MS_I/MS_L` 네 숫자와 심볼 매크로뿐이다.

## 6. 정답 코드

<!-- code: solution.S diff=starter.S -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 harness.c solution.S shared_violators.S -o solution && ./solution   # 41/41 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   harness.c solution.S shared_violators.S -o sol_asan && ./sol_asan                                    # 41/41, 보고 없음
bash demo_abi.sh > demo_abi_apple_vs_linux.txt                                                            # 호출자/피호출자 asm 비교
cc -std=c11 -g -O1 demo_stack.c solution.S -o /tmp/ARM-M01_stack && lldb -b -s demo_stack.lldb /tmp/ARM-M01_stack
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| Apple 에서 스택 인자를 8 B 슬롯으로 읽음 | Apple 은 자연 크기로 압축한다 → 실측 `vio_mixed_aapcs` 가 매번 다른 틀린 값 | `#ifdef __APPLE__` 오프셋 0/2/4/8, 표준 0/8/16/24 |
| 스택의 char/short 를 `ldr w`/`ldrb` 로 읽음 | 나머지 바이트는 쓰레기이고, 0 확장은 음수를 틀리게 만든다 | `ldrsb x9, [sp]`, `ldrsh x10, [sp, #2]` |
| 프레임을 만든 뒤 `[sp]` 에서 9번째 인자를 읽음 | sp 가 내려갔다 → 지역 변수를 읽음 | `[x29, #16]` 또는 프레임 크기만큼 더한 오프셋 |
| x8 버퍼 주소를 x0 으로 돌려줘야 한다고 생각 / 반대로 호출자가 x0 을 믿음 | AAPCS64 엔 그런 의무가 없다(x86-64 SysV 의 rax 와 다름). clang 호출자는 자기 버퍼를 읽는다 | 쓰기만 하고 `ret` |
| x8 버퍼를 16 정렬로 가정 | 실측 `add x8, sp, #12` — 타입 정렬(4)만 보장 | 타입 정렬에 맞는 접근만 |
| int32 인자를 `x1` 로 더함 | 윗 32비트 unspecified → 검사기 경유(`x1 = 0xffffffff`)에서 체크섬이 2^32 만큼 틀림 | `add x10, x0, w1, sxtw` |
| HFA 계산 임시로 s8–s15 사용, 저장 안 함 | d8–d15 위반 (`vio_d9` 꼴) | v16–v31 사용, 또는 `stp d8, d9` 저장 |
| `fmov s12, …` 는 32비트만 바꾸니 d12 는 괜찮다 | 스칼라 FP 쓰기는 레지스터 나머지 비트를 0 으로 만든다 → d12 변경 (`vio_s12` 마스크 0x04000) | 같음 — 저장 대상 |
| 검사기가 q8 전체(128비트) 비교 | 상위 64비트는 caller 책임 → `vio_v8_upper` 오탐 | `fmov x13, d8` 로 하위만 |
| 저장/복원 쌍 순서 뒤바꿈 (`stp x19, x20` / `ldp x20, x19`) | 둘 다 "저장·복원" 하는 것처럼 보여 리뷰에서 통과 → 검사기 마스크 0x00003 | 같은 순서로 |
| 검사기에서 인자 로드 중 `a` 포인터(x1)를 덮음 | `ldp x0, x1, [x1]` 을 먼저 하면 그 뒤 로드가 망가짐 | a 를 x17 로 옮기고 x0/x1 을 마지막에 |
| 20 B 구조체를 x0..x2 로 넘긴다고 생각 | 16 B 초과 비-HFA 는 B.4 로 복사본 포인터 | `ldp s0, s1, [x0]` |

## 8. 면접 Q&A

<details>
<summary>Q1. 인자 10개짜리 함수에서 9번째, 10번째 인자는 어디 있나요?</summary>

정수 인자는 x0–x7 까지 쓴다(NGRN 0→8). 그다음은 C.13 으로 NGRN = 8 이 되고, 남은 인자는 NSAA, 곧 **호출 순간의 sp** 부터 쌓인다. long 이면 `[sp]`, `[sp+8]` 이다. 피호출자가 프레임을 만들었다면 그만큼 오프셋이 커진다(`stp x29, x30, [sp, #-16]!` 뒤라면 `[x29+16]`). 꼬리질문 "char 하나가 스택으로 가면 몇 바이트를 차지하나요?" → 표준은 C.16 에 따라 8 B 슬롯이다. Apple 은 1 B 를 쓰고 다음 인자는 자기 정렬로 붙는다. 스택 인자 전체 크기만 8 의 배수로 패딩한다. 실측은 char@0, short@2, int@4, long@8 이었다.

</details>

<details>
<summary>Q2. 32바이트 구조체를 반환하면 어떻게 되나요? 피호출자가 주소를 돌려줘야 하나요?</summary>

호출자가 결과 크기·정렬에 맞는 메모리를 잡고 그 주소를 **x8**(XR, indirect result location register)로 넘긴다. 피호출자는 그 메모리에 쓴다. AAPCS64 원문은 *"there is no requirement for the callee to preserve the value stored in x8"* 이고, 주소를 x0 으로 돌려줄 의무도 없다. 실측 clang 호출자는 `bl` 뒤에 자기 `[sp]` 를 읽고 x0 은 보지 않았다. x86-64 SysV 는 hidden 첫 인자(rdi)로 받고 **rax 로 돌려줘야** 하는 점이 다르다(확인 권장). 꼬리질문 "그 버퍼가 호출자의 지역 변수 그 자체일 수 있나요?" → 그렇다. 실측에서 `&m == x8` 이었다(copy elision). 그래서 피호출자가 다른 인자를 통해 같은 객체를 읽는 경우의 앨리어싱은 언어 규칙이 책임진다.

</details>

<details>
<summary>Q3. HFA 가 뭐고, float 5개짜리 구조체나 HFA 가 레지스터에 다 안 들어가면 어떻게 되나요?</summary>

HFA(Homogeneous Floating-point Aggregate)는 같은 FP 타입 멤버가 **1~4개**인 구조체·배열이다(HVA 는 같은 짧은 벡터 타입). C.2 에 따라 NSRN + 멤버 수 ≤ 8 이면 멤버 하나에 v 레지스터 하나씩 들어간다. 그래서 `quat` 는 `s0–s3` 로 오고 같은 레지스터로 반환된다. 메모리를 거치지 않으니 빠르다. 멤버가 5개면 HFA 가 아니다. 20 B > 16 이므로 B.4 에 따라 복사본 포인터로 넘어가고, 반환은 x8 이다. HFA 인데 레지스터가 모자라면 C.3 에 따라 NSRN = 8 로 막고 **통째로 스택**에 놓는다. 일부만 레지스터에 넣는 분할은 없고, 정수 레지스터로도 가지 않는다. 연습 3 의 `quat_add3(a, b, c)` 실측에서 c 가 `[sp..sp+15]` 에 있었고, 두 ABI 모두 같았다.

</details>

<details>
<summary>Q4. 왜 v8–v15 는 하위 64비트만 callee-saved 인가요? s 레지스터만 써도 저장해야 하나요?</summary>

비용 배분 때문이다. 스칼라 double 코드(제어 루프, 필터)는 호출을 건너 살아야 하는 값이 많다. 그래서 d8–d15 를 callee-saved 로 두면 이득이 있다. 반면 128비트 NEON 값을 호출 너머로 들고 있는 일은 드물다. 그런 드문 경우는 **호출자**가 저장하게 하고, 모든 피호출자에게 128비트 저장을 강요하지 않는다(AAPCS64: "it is the responsibility of the caller to preserve larger values"). s 레지스터 쓰기도 저장해야 한다. 스칼라 FP 쓰기는 v 레지스터 나머지 비트를 0 으로 만들기 때문이다. 실측 `vio_s12` 의 마스크가 d12 비트로 나왔다. 꼬리질문 "d8 을 `stp d8, d9` 로 저장·복원하면 v8 상위 64비트는?" → `ldp d8` 이 상위를 0 으로 만든다. 그래도 합법이다(ARM-B11 실측).

</details>

<details>
<summary>Q5. caller-saved 와 callee-saved, 누가 비용을 내나요? RTOS 문맥 전환과 무슨 관계가 있죠?</summary>

caller-saved(x0–x17, v0–v7, v16–v31, v8–v15 상위)는 호출 뒤에도 값이 필요할 때만 **호출자**가 저장한다. leaf 피호출자는 이것만 쓰면 저장 비용이 0 이다. callee-saved(x19–x28, x29, sp, d8–d15)는 **쓰는 피호출자**만 저장한다. 호출을 많이 건너는 값은 여기에 둔다. 협조적 문맥 전환(`switch_to(prev, next)` 를 함수처럼 부름)은 호출자 입장에서 평범한 함수 호출이다. 그래서 callee-saved + sp + lr 만 저장하면 충분하다. Linux arm64 `cpu_switch_to` 가 그렇게 한다(확인 권장). 인터럽트·선점 진입은 아무 명령 사이에서나 끼어들 수 있다. 그래서 caller-saved 까지 전부 저장해야 하고, Cortex-M 은 하드웨어가 r0–r3, r12, lr, pc, xPSR 를 자동으로 쌓는다(ARM-M09).

</details>

<details>
<summary>Q6. Apple 에서 C 가 부르는 asm 을 짤 때 표준 AAPCS64 문서만 보고 짜면 어디서 깨지나요?</summary>

네 곳이다. (1) **스택 인자 압축**이다. 8개 넘는 인자 중 8 B 미만이 있으면 오프셋이 다르다. 실측 `vio_mixed_aapcs` 가 틀린 값을 냈다. (2) **가변 인자는 전부 스택**에 8 B 슬롯으로 놓이고, `va_list` 는 `char *` 다. 표준은 x1.. 레지스터부터 쓴다. 실측 `call_var` 에서 Apple 은 `stp x9, x8, [sp]`, Linux 는 `mov w1, #1; mov w2, #2` 였다. (3) 32비트 미만 레지스터 인자는 **호출자가** 확장한다(표준은 피호출자, ARM-B01). (4) 16 B 정렬 인자(`__int128`)가 홀수 x 레지스터에서 시작할 수 있다(표준은 짝수로 올림, C.10). 거기에 x18 금지와 x29 항상 유효가 더해진다. 실무에서는 한 `.S` 에 `#ifdef __APPLE__` 오프셋을 두고, 양쪽에서 테스트를 돌린다.

</details>

<details>
<summary>Q7. 짠 asm 함수가 ABI 를 지키는지 어떻게 자동으로 검사하나요? 한계는요?</summary>

래퍼(트램펄린)를 쓴다. callee-saved 전부에 서로 다른 센티널(하위 바이트에 레지스터 번호)을 심고, 인자를 채워 부르고, 돌아오면 비교해서 비트마스크를 만든다. sp/x29 는 프레임 안의 자기 주소 표지로 어느 쪽이 깨졌는지 판정하고 복구한다. 둘 다 깨지면 `brk` 로 멈춘다. 한계는 이렇다. 실행된 경로만 보인다(입력 커버리지). 인자 위치 오류는 못 본다(값 테스트로 보완). 호출자 프레임에 쓰는 버그는 못 본다(스택 카나리로 보완). 잠깐 바꿨다 되돌리는 것은 합법이라 잡을 필요가 없다. 비동기 신호나 IRQ 중 상태도 이 방법의 범위 밖이다. 실무 대안은 컴파일러의 `-fsanitize` 류가 아니라 유닛 테스트용 트램펄린이고, valgrind 류 동적 계측이나 정적 분석(callee-saved 를 쓰기 전에 저장했는지)도 쓸 수 있다.

</details>

<details>
<summary>Q8. 크래시 덤프에서 함수 인자를 복원하려면 어디를 보나요?</summary>

함수 진입 직후에 죽었다면 x0–x7, v0–v7 이 인자이고 9번째부터는 `[sp]` 다. x8 은 큰 구조체 반환 버퍼다. 함수 중간이라면 인자 레지스터는 이미 덮였을 수 있다. callee-saved 로 옮겨졌다면(`mov x19, x0`) 그 레지스터나, 더 깊은 프레임이 저장한 슬롯(`[fp+16]` 등)에서 찾는다. 그래서 디버거의 "optimized out" 이 생긴다. 프레임 체인 걷기는 ARM-B08, 덤프 분류 연습은 ARM-M14 다.

</details>

## 9. 추가 연습

### 연습 1 — 12 B 구조체 반환: `make_rgb`

`typedef struct {int32_t r, g, b;} rgb12; rgb12 make_rgb(int32_t r, int32_t g, int32_t b);`. 12 B 비-HFA 다. 어느 레지스터에 어떤 비트로 넣어야 하나?

<details>
<summary>답</summary>

B.5 로 크기를 16 으로 올리면 C.12 가 적용된다. "더블워드 정렬 주소에서 LDR 두 번 한 것처럼" 담긴다. 그래서 x0 = 메모리 +0..+7 = `g:r`(r 이 하위), x1 = +8..+11 = b 이고, x1 윗 32비트는 unspecified 다. `mov w9, w0` / `orr x0, x9, x1, lsl #32` / `mov w1, w2`. clang 의 `callee_rgb` 실측도 같은 3줄이다(`demo_abi_apple_vs_linux.txt`). 테스트는 랜덤 r, g, b 1000개로 한다.

</details>

### 연습 2 — 인자도 포인터, 결과도 포인터: `f5_scale`

`f5 f5_scale(f5 v, float k)`. v, k, 결과는 각각 어디 있나?

<details>
<summary>답</summary>

v → 복사본 포인터 **x0**(B.4), k → **s0**(NSRN 은 따로 세므로 0번), 결과 → **x8** 버퍼다. 실측 Apple 호출자는 `mov x0, sp` / `sub x8, x29, #20` / `fmov s0, #3.0` 였다. 루프 `ldr s1, [x0, x9, lsl #2]` → `fmul` → `str s1, [x8, x9, lsl #2]` 를 5회 돈다. `v = f5_scale(v, 2)` 처럼 같은 변수를 넘겨도 호출자가 복사본을 만들기 때문에 겹치지 않는다(테스트가 확인).

</details>

### 연습 3 — HFA 가 넘칠 때: `quat_add3`

`quat quat_add3(quat a, quat b, quat c)` (= a + b + c, 성분별). c 는 어디로 오나?

<details>
<summary>답</summary>

a = s0–s3, b = s4–s7 로 NSRN = 8 이 된다. c 는 C.3 에 따라 레지스터 대신 **통째로 `[sp..sp+15]`** 에 온다(분할 없음). 실측 호출자는 `stp x9, x8, [sp]` 로 float 쌍 두 개(9.0/10.0, 11.0/12.0)를 썼고, Apple 과 Linux 가 같았다. 피호출자는 `ldp s16, s17, [sp]` / `ldp s18, s19, [sp, #8]` 로 읽는다. C 와 같은 `(a+b)+c` 순서로 더한다.

</details>

### 연습 4 — 카운터 두 개: `mixed_iv`

`long mixed_iv(long a0..a7, double d, long a8)` → `a0 + a8 + (long)d`. d 는 스택인가?

<details>
<summary>답</summary>

아니다. 정수 레지스터가 다 찼어도 NSRN 은 0 이므로 d 는 **d0** 이다. a8 만 `[sp]` 다. 실측 호출자는 `str x8, [sp]` (10) + `fmov d0, #9.5` 였다. `fcvtzs x10, d0` 는 0 쪽으로 자른다(−2.75 → −2, 테스트 포함).

</details>

### 연습 5 — 가변 인자를 asm 으로 받기: `vsum`

`long vsum(long n, ...)` (가변 인자는 전부 long). Apple 과 표준 ABI 모두에서 동작하게 하라.

<details>
<summary>답</summary>

Apple 에서는 가변 인자가 처음부터 전부 스택 8 B 슬롯에 있다. 그래서 `p = sp` 로 두고 `ldr x11, [p], #8` 을 n 번 하면 된다. 표준에서는 가변 인자도 x1–x7 에 먼저 들어간다. `stp x0, x1, [sp, #-64]!` … 로 x0–x7 을 들어온 스택 인자 **바로 아래** 64 B 에 저장하면 `[sp+8]` 부터 x1, x2, …, x7, 그리고 원래 스택 인자가 한 줄로 이어진다. 이것이 표준 `va_list` 의 GR 저장 영역 아이디어다. `vsum(10, 1..9, 1000) == 1045` 가 레지스터 7개 + 스택 3개 경로를 확인한다(Linux 실측 통과). 실측 Linux `call_vsum` 은 `mov w1..w3` 후 `b vsum`(꼬리 호출), Apple 은 스택에 `stp`/`str` 이었다.

</details>

### 한 번에 검증

<!-- code: practice.S -->

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c practice.S shared_violators.S -o practice && ./practice   # 11/11 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined \
   practice.c practice.S shared_violators.S -o pr_asan && ./pr_asan                                       # 11/11, 보고 없음
# Linux VM (gcc 15.2, 표준 ABI): 11/11 passed
```

## 10. 핵심 정리

- 인자 배정은 카운터 3개다. 정수 NGRN(x0–x7), FP/HFA NSRN(v0–v7), 스택 NSAA(= 호출 순간 sp). 정수와 FP 는 **따로** 센다.
- 스택 인자: 표준은 8 B 미만도 8 B 슬롯(C.16)이고, **Apple 은 자연 크기·정렬로 압축**한다(char@0 short@2 int@4 long@8 실측). 스택의 작은 정수는 `ldrsb/ldrsh/ldrsw` 로 읽는다.
- 반환 = "인자로 레지스터에 들어가면 같은 레지스터" 다. ≤16 B 는 x0/x1, HFA 는 s0–s3/d0–d3, 그 밖은 **x8 버퍼**(호출자가 할당, 타입 정렬만 보장, x0 으로 돌려줄 의무 없음).
- HFA = 같은 FP 타입 1~4개다. 레지스터가 모자라면 통째로 스택에 간다. 16 B 넘는 비-HFA 인자는 호출자가 만든 복사본의 포인터로 간다.
- callee-saved = x19–x28, x29, sp, **d8–d15 하위 64비트**다. s 레지스터 쓰기도 d 를 바꾼다. v8 상위 64비트는 caller 몫이다.
- Apple 가변 인자는 전부 스택 8 B 슬롯이고 `va_list = char *` 다. 표준은 레지스터부터 쓴다.
- 검사기 = 센티널 심기 → 호출 → 비교 → 마스크다. sp/x29 는 표지로 판정·복구한다. 인자 위치 오류는 검사기가 못 보니 값 테스트와 함께 쓴다.

## 참고자료

- Arm, *Procedure Call Standard for the Arm 64-bit Architecture (AAPCS64)* 2026Q3 — "Parameter passing rules" Stage A/B/C (B.4 큰 composite → 포인터, C.2/C.3 HFA, C.13–C.17 스택), "Result return"(x8), "General-purpose Registers"/"SIMD and Floating-Point Registers"(r19–r29, v8–v15 하위 64비트), HFA 정의(5.10.5.1): <https://github.com/ARM-software/abi-aa/blob/main/aapcs64/aapcs64.rst>
- Apple, *Writing ARM64 code for Apple platforms* — 스택 인자 슬롯, 16 B 정렬 인자 홀수 레지스터, 32비트 미만 인자 호출자 확장, 가변 인자 8 B 스택 슬롯 / `va_list = char*`, x18, x29: <https://developer.apple.com/documentation/xcode/writing-arm64-code-for-apple-platforms>
- Arm Architecture Reference Manual A-profile (DDI 0487) — FMADD/FMSUB, LDRSB/LDRSH/LDRSW, 스칼라 FP 쓰기 시 상위 비트 0: <https://developer.arm.com/documentation/ddi0487/latest/>
- System V AMD64 ABI (x86-64 sret 과 rax 반환 비교용): <https://gitlab.com/x86-psABIs/x86-64-ABI>
- Linux kernel `arch/arm64/kernel/entry.S` `cpu_switch_to` (callee-saved 만 저장하는 문맥 전환): <https://elixir.bootlin.com/linux/latest/source/arch/arm64/kernel/entry.S>
- 레슨 파일: `demo_abi.c` + `demo_abi.sh` (+ 출력 `demo_abi_apple_vs_linux.txt`), `demo_stack.c` + `demo_stack.lldb` (+ 기록 `demo_stack_lldb.txt`)
- 관련 레슨: ARM-B01 (첫 .S, Apple vs AAPCS64 표), ARM-B07 (SDIV/MSUB), ARM-B08 (프레임, x19–x28 검사기), ARM-B11 (FP 인자, d8–d15 검사기), ARM-M02 (inline asm 제약), ARM-M09 (예외 진입 자동 저장), ARM-M14 (크래시 덤프), ARM-A01 (PendSV 문맥 전환)
