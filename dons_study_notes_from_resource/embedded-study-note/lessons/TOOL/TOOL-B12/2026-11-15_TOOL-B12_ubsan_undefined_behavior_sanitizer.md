---
id: TOOL-B12
time: 60분
updated: 2026-11-15
---
# TOOL-B12 UBSan으로 정의되지 않은 동작 잡기

## 학습 목표

- `-fsanitize=undefined` 메시지 한 줄을 보고 **어느 체크(check)** 가 잡았는지(signed-integer-overflow, shift-base / shift-exponent, alignment, bool, enum, null, bounds, integer-divide-by-zero, float-cast-overflow), 그리고 UB-free 로 어떻게 고칠지 바로 말한다.
- 기본값이 **recover(보고하고 계속 실행, exit 0)** 라는 함정을 알고, CI 에서는 `-fno-sanitize-recover=all` 로 멈추게 한다. 이 맥은 abort(rc 134), Linux 는 exit 1 — 실측.
- **트랩 모드(trap mode)** `-fsanitize-trap=undefined` 를 설명한다: 런타임 라이브러리 없이 체크마다 `brk #0x55NN`(arm64) / `udf`(Thumb) 한 개. lldb 가 트랩 이유를 그대로 보여 주는 것과 Cortex-M 에서 UsageFault/HardFault 로 이어지는 흐름을 안다.
- Cortex-M4 오브젝트에서 모드별 **코드 크기 비용**(없음 / 트랩 / 최소 런타임 / 전체 런타임)을 직접 재고, 펌웨어에 어떤 조합을 넣을지 근거를 댄다.
- UBSan 이 **못 잡는 것**(C 의 enum, 범위 안의 잘못된 값, 논리 버그, 실행 안 된 경로)과 UB 가 아닌데 위험한 것(unsigned wrap, 암묵 축소 변환)을 구분한다.

## 1. 문제

"이 코드, 디버그 빌드에선 되는데 릴리스에선 값이 이상해요" — 펌웨어에서 가장 비싼 버그가 정의되지 않은 동작(undefined behavior, UB)이고, 면접에서는 "UBSan 써 봤어요? 펌웨어에선 어떻게 써요?" 로 나온다. UB 의 정의와 목록은 C 트랙(C-M12 UB 카탈로그, C-B03 시프트, C-B02 정수 승격, C-M04 비정렬 접근, C-M13 strict aliasing)에서 다뤘다. 이 레슨은 **도구** 쪽이다: UB 를 실행 중에 잡는 UndefinedBehaviorSanitizer(UBSan)를 호스트 테스트와 CI 에 넣고, 같은 코드를 Cortex-M 빌드에 트랩 모드로 넣었을 때 몇 바이트가 드는지 잰다.

UBSan 은 컴파일러가 **위험한 연산 바로 앞에 검사 코드**를 끼워 넣는 방식이다. ASan(TOOL-B11)처럼 shadow 메모리가 필요 없어서 가볍고, 그래서 런타임 라이브러리를 아예 빼고 **트랩 한 개**로 바꾸면 MCU 에도 들어간다. 아래가 이 레슨의 `adc_scale` 한 줄 `raw * num / den` 이 모드에 따라 바뀌는 모습이다 (명령은 이 맥에서 실제로 본 것).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 줄 raw * num 에 붙는 검사: 네 가지 모드</text>
  <rect x="10" y="40" width="200" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="64" fill="currentColor" font-family="monospace">raw * num / den</text>
  <text x="22" y="86" fill="currentColor" font-size="12">smull → 64비트 결과 비교</text>
  <path d="M210 70 L250 70" stroke="currentColor" fill="none"/>
  <path d="M250 70 L250 296" stroke="currentColor" fill="none"/>
  <g stroke="currentColor" fill="none">
    <path d="M250 70 L270 70"/><path d="M250 136 L270 136"/><path d="M250 202 L270 202"/><path d="M250 268 L270 268"/>
  </g>
  <rect x="270" y="46" width="460" height="52" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="282" y="66" fill="currentColor">기본 -fsanitize=undefined (recover)</text>
  <text x="282" y="88" fill="currentColor" font-family="monospace" font-size="12">bl __ubsan_handle_mul_overflow → 메시지, 계속 실행 → exit 0</text>
  <rect x="270" y="112" width="460" height="52" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="282" y="132" fill="currentColor">+ -fno-sanitize-recover=all (CI)</text>
  <text x="282" y="154" fill="currentColor" font-family="monospace" font-size="12">메시지 후 중단: 맥 abort rc 134 / Linux exit 1</text>
  <rect x="270" y="178" width="460" height="52" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="282" y="198" fill="currentColor">-fsanitize-trap=undefined (펌웨어)</text>
  <text x="282" y="220" fill="currentColor" font-family="monospace" font-size="12">b.ne → brk #0x550c (arm64) / udf (Thumb): 런타임 0 B</text>
  <rect x="270" y="244" width="460" height="52" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="282" y="264" fill="currentColor">-fsanitize-minimal-runtime</text>
  <text x="282" y="286" fill="currentColor" font-family="monospace" font-size="12">bl __ubsan_handle_mul_overflow_minimal → 직접 구현 가능</text>
  <text x="10" y="320" fill="currentColor" font-size="12">메시지 = 파일:줄:열 + 값. 전체 런타임만 값과 위치를 출력한다 (위치·타입 정보가 .data 에 들어가는 이유).</text>
</svg>
```

### Part A — 산술: signed overflow 와 시프트

`starter.c` 의 다섯 함수는 벤치에서는 늘 맞는 값을 내던 코드다. 각각 합법적인 입력 하나에서 UB 가 난다.

- `adc_scale(raw, num, den)` — `raw * num` 곱셈 오버플로, `den == 0`, `INT32_MIN / -1`. 계약: 정확히 계산해서 int32 로 **포화(saturate)**, `den == 0` 이면 0.
- `accum_add` — 적분기 한 스텝. 포화 덧셈.
- `sample_mag` — `-INT32_MIN` 은 int32 에 없다. 결과는 uint32 로 정확히.
- `reg_mask(lsb, width)` — `(1 << width) - 1` 이 `width = 31` 에서 부호 비트로 시프트, `32` 에서 폭 이상 시프트.
- `sample_to_u16(raw, adc_bits)` — 18비트 ADC 를 넣으면 `16 - adc_bits` 가 음수.

### Part B — 메모리와 표현: 정렬, bool, enum, NULL, 범위

- `pkt_parse` — 무선/UART 패킷 헤더 `[type][seq LE 4B][ack][len LE 2B]` 를 `*(const uint32_t *)(buf + 1)` 로 읽는 **비정렬 load**, 와이어 바이트를 `*(const bool *)` 로 읽는 **잘못된 bool 값**(2). 계약: ack 바이트가 0/1 이 아니면 -2.
- `pwr_mode_name(wire)` — 바이트를 `enum pwr_mode` 로 캐스트해 이름 표를 인덱싱. C 에서는 enum 체크가 안 걸리고 **bounds** 체크가 잡는다(직접 확인).
- `cal_apply(c, x)` — 보정값이 아직 로드 안 됐을 때 `c == NULL` → **null** 체크.
- `therm_lookup(adc12)` — 글리치 값 0x1000 이 `k_therm[16]` 을 읽음 → **bounds**.

### Part C — 펌웨어 형태: float→int, 트랩 모드, 크기 비용

- `temp_to_centi(400.0f)` → 40000 은 int16_t 범위 밖, `NAN` → **float-cast-overflow**. `duty_to_ccr(-0.25f, 999)` 도 같은 UB, `duty_to_ccr(1.5f, 999)` 는 UB 가 아닌 **논리 버그**(UBSan 은 침묵 — 값 체크로만 잡힘).
- `check.sh` 는 같은 소스를 ① UBSan 런타임 + no-recover 로 케이스마다 실행, ② `-O2 -fsanitize-trap=undefined` 로 다시 실행(트랩 하나라도 나면 FAIL), ③ `thumbv7em` Cortex-M4 오브젝트로 `-Werror` 컴파일하고 트랩 모드 크기를 출력한다.
- 크기 실측: `demo_size.sh` (맥 clang → Cortex-M4 ELF 오브젝트) 와 `demo_vm.sh` (VM 의 arm-none-eabi-gcc 14.2.1).

## 2. Problem (English)

> **Make a firmware helper module UBSan-clean.**
>
> `starter.c` implements ADC scaling, a saturating integrator, register-mask generation, ADC sample alignment, a packet-header parser, a power-mode name lookup, calibration, a thermistor table and two float→integer conversions. All of them pass the "normal" tests, but each has undefined behaviour for some legal input. The test driver runs every case in its own process, built with
> `cc -std=c11 -g -O1 -fsanitize=undefined -fno-sanitize-recover=all driver.c starter.c`.
>
> 1. For each failing case, read the UBSan report (`file:line:col: runtime error: ...`), name the check that fired and the C rule that was broken.
> 2. Rewrite only the function bodies so that every case returns the value required by `fwub.h` with **no** `runtime error:` — e.g. `adc_scale(INT32_MIN, 1, -1) == INT32_MAX`, `reg_mask(0, 32) == 0xFFFFFFFF`, `sample_to_u16(0x3FFFF, 18) == 0xFFFF`, `pkt_parse` of `{0x10, EF BE AD DE, 01, 34 12}` at an odd address gives `seq 0xDEADBEEF`, ack byte `2` returns `-2`, `pwr_mode_name(7) == "?"`, `cal_apply(NULL, 100) == 100`, `temp_to_centi(400.0f) == 32767`, `temp_to_centi(NAN) == 0`, `duty_to_ccr(1.5f, 999) == 999`.
> 3. Constraints: the module stays freestanding (only `<stdint.h>`, `<stddef.h>`, `<stdbool.h>` — it must also compile for `--target=thumbv7em-none-eabi` with `-Werror`), no compiler builtins, signatures unchanged.
> 4. Rebuild with `-O2 -fsanitize=undefined -fsanitize-trap=undefined`: no case may trap (shell exit code 133).
>
> Follow-ups: why does the default build exit 0 after reporting UB? What does trap mode cost on a Cortex-M4, and what happens on the MCU when a trap fires? What can UBSan *not* see?

## 3. 시작 코드

검사기 (이 레슨의 채점 계약 — 어느 디렉터리에서 실행해도 됨, 작업은 `/tmp/TOOL-B12_work/<variant>` 에서만):

<!-- code: check.sh -->

모듈 헤더 (계약) 와 시작 코드:

<!-- code: fwub.h -->

<!-- code: starter.c -->

테스트 드라이버 (TODO 아님 — 케이스 이름 하나를 받아 `got .. want ..` 를 찍고, 같으면 0, 다르면 2 로 끝남):

<!-- code: driver.c -->

```bash
bash check.sh starter      # PASS/FAIL 줄 + "N/M passed", 실패 있으면 exit 1
bash check.sh solution     # 전부 PASS, exit 0
bash check.sh practice     # 9. 추가 연습
```

`bash check.sh starter` 실제 출력 (2초 정도):

```text
PASS canary: planted INT_MAX + 1 reported, process stopped (rc 134)
PASS adc_ok
FAIL adc_mul_ovf  (rc 134) starter.c:13:16 signed integer overflow: 30000 * 100000 cannot be represented in type 'int32_t' (aka 'int')
FAIL adc_div0  (rc 134) starter.c:13:22 division by zero
FAIL adc_min_div  (rc 134) starter.c:13:22 division of -2147483648 by -1 cannot be represented in type 'int32_t' (aka 'int')
PASS accum_ok
FAIL accum_sat  (rc 134) starter.c:22:16 signed integer overflow: 2147483000 + 1000 cannot be represented in type 'int32_t' (aka 'int')
FAIL accum_sat_lo  (rc 134) starter.c:22:16 signed integer overflow: -2147483648 + -1 cannot be represented in type 'int32_t' (aka 'int')
FAIL mag_min  (rc 134) starter.c:29:31 negation of -2147483648 cannot be represented in type 'int32_t' (aka 'int'); cast to an unsigned type to negate this value to itself
PASS mask_ok
FAIL mask_top  (rc 134) starter.c:35:42 left shift of 15 by 28 places cannot be represented in type 'int'
FAIL mask_31  (rc 134) starter.c:35:27 left shift of 1 by 31 places cannot be represented in type 'int'
FAIL mask_32  (rc 134) starter.c:35:27 shift exponent 32 is too large for 32-bit type 'int'
PASS align12
FAIL align18  (rc 134) starter.c:41:27 shift exponent -2 is negative
PASS pkt_short
FAIL pkt_seq  (rc 134) starter.c:51:16 load of misaligned address 0x000102cbc371 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
FAIL pkt_ack  (rc 134) starter.c:52:16 load of value 2, which is not a valid value for type 'const _Bool'
PASS mode_ok
FAIL mode_bad  (rc 134) starter.c:64:12 index 7 out of bounds for type 'const char *const[3]'
PASS cal_ok
FAIL cal_null  (rc 134) starter.c:70:19 member access within null pointer of type 'const struct cal'
PASS therm_ok
FAIL therm_glitch  (rc 134) starter.c:81:12 index 16 out of bounds for type 'const uint16_t[16]' (aka 'const unsigned short[16]')
PASS temp_ok
FAIL temp_hot  (rc 134) starter.c:89:12 40000 is outside the range of representable values of type 'short'
FAIL temp_nan  (rc 134) starter.c:89:12 nan is outside the range of representable values of type 'short'
PASS duty_ok
FAIL duty_neg  (rc 134) starter.c:98:12 -249.75 is outside the range of representable values of type 'unsigned short'
FAIL duty_over  (rc 2) duty_to_ccr(1.5,999): got 1498 want 999
FAIL trap build: SIGTRAP (rc 133) in: adc_mul_ovf adc_div0 adc_min_div accum_sat accum_sat_lo mag_min mask_top mask_31 mask_32 align18 pkt_seq pkt_ack mode_bad cal_null therm_glitch temp_hot temp_nan duty_neg | other rc in: duty_over(rc 2)
PASS Cortex-M4 object (thumbv7em -Os -Werror): text 309 B, with UBSan traps 635 B
12/32 passed
```

검사기 설계:

- **canary** — `INT_MAX - 1 + 1 + 1` 을 넣은 3줄짜리 프로그램이 정말 보고되고 멈추는지 먼저 본다. 툴체인이 바뀌어 sanitizer 가 조용히 꺼지면 모든 케이스가 "PASS" 가 되는 사고를 막는다.
- 케이스마다 **별도 프로세스**: no-recover 라 첫 보고에서 멈추고, NULL 같은 케이스가 다른 케이스를 죽이지 않는다. FAIL 줄에 첫 `runtime error:` 의 위치와 내용을 그대로 붙인다.
- 트랩 빌드는 `-O2` 로 한다 — 최적화가 체크를 지우거나 옮겨도 여전히 깨끗해야 실제 펌웨어 빌드와 같다.

관찰용 데모 (전부 이 디렉터리, 결과는 `*.txt` 로 저장해 둠):

```bash
bash demo_catalog.sh > ubsan_starter.txt   # starter 전체를 recover 모드로 + UBSAN_OPTIONS + UBSan 없는 -O2
bash demo_groups.sh  > ubsan_groups.txt    # -fsanitize=undefined 가 펼쳐지는 목록, C vs C++ (demo_lang.c)
bash demo_trap.sh    > ubsan_trap.txt      # 트랩 모드: rc 133, brk 코드, lldb (demo_trap.lldb)
bash demo_size.sh    > size_m4.txt         # Cortex-M4 오브젝트 크기, 7개 모드
bash demo_vm.sh      > ubsan_vm.txt        # Lima VM: GCC 15.2 메시지, 최소 런타임, addr2line, arm-none-eabi-gcc 크기
cc -std=c11 -g -O1 -fsanitize=undefined[,unsigned-integer-overflow,implicit-conversion] demo_strict.c   # UB 아닌 버그
cc -std=c11 -O1 -fsanitize=undefined -fsanitize-minimal-runtime -fno-sanitize-link-runtime demo_minrt.c starter.c
```

## 4. 힌트

<details>
<summary>힌트 1 — 메시지의 형태가 체크 이름을 말해 준다</summary>

`signed integer overflow: A op B` → signed-integer-overflow. `left shift of X by N places` → shift-base(값 쪽), `shift exponent N is too large / is negative` → shift-exponent(개수 쪽). `load of misaligned address` → alignment, `load of value V, which is not a valid value for type` → bool / enum, `member access within null pointer` → null, `index I out of bounds for type T[N]` → bounds, `division by zero` / `division of A by -1` → integer-divide-by-zero / signed-integer-overflow, `V is outside the range of representable values` → float-cast-overflow. `UBSAN_OPTIONS=report_error_type=1` 로 돌리면 SUMMARY 줄에 이름이 직접 찍힌다.

</details>

<details>
<summary>힌트 2 — 산술: 넓은 타입에서 계산하고, 범위를 먼저 본 다음 좁힌다</summary>

int32 × int32 는 int64 에 항상 들어간다(최대 2^62). 나눗셈도 int64 에서 하면 `INT32_MIN / -1` 은 그냥 2147483648 이고, 그 다음 포화. 부호 반전은 `0u - (uint32_t)x` — unsigned 는 2^32 로 감싸는 것이 정의된 동작이다. 마스크는 처음부터 `1u` 로, `width == 32` 는 따로.

</details>

<details>
<summary>힌트 3 — 와이어 바이트는 바이트로 읽고, 검사한 다음 타입을 입힌다</summary>

비정렬 정수는 바이트 4개를 시프트/OR 로 조립(엔디안도 명시됨, C-M05). 컴파일러에 맡기려면 `memcpy` 도 되지만 이 모듈은 freestanding 이라 libc 가 없다. bool·enum·인덱스는 **원시 바이트 값으로 범위를 검사한 뒤** 변환한다 — 일단 `bool` 로 읽으면 이미 늦다.

</details>

<details>
<summary>힌트 4 — float→int 는 캐스트 "전에" 범위와 NaN 을 본다</summary>

C11 6.3.1.4: 정수 부분이 대상 타입에 안 들어가면 UB. `(int16_t)v` 뒤에 검사하면 이미 UB 가 일어난 후다. NaN 은 모든 비교가 거짓이라 `v != v` 로 잡히고, `!(duty > 0.0f)` 한 줄이 음수·0·NaN 을 같이 거른다. `duty > 1` 은 UB 가 아니라 계약 위반 — UBSan 이 아니라 테스트가 잡는다.

</details>

## 5. 풀이

### 0. 검사는 어떻게 생겼나 — 기본 모드가 exit 0 이라는 함정

`demo_catalog.sh` 는 starter 를 **기본 옵션**(`-fsanitize=undefined` 만)으로 빌드해 전 케이스를 돌린다. 실제 출력에서 골라 보면 (`ubsan_starter.txt`):

```text
=== mag_min
starter.c:29:31: runtime error: negation of -2147483648 cannot be represented in type 'int32_t' (aka 'int'); cast to an unsigned type to negate this value to itself
SUMMARY: UndefinedBehaviorSanitizer: undefined-behavior starter.c:29:31
sample_mag(INT32_MIN): got 2147483648 want 2147483648
[exit 0]
=== mask_31
starter.c:35:27: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
starter.c:35:37: runtime error: signed integer overflow: -2147483648 - 1 cannot be represented in type 'int'
starter.c:35:42: runtime error: left shift of 2147483647 by 1 places cannot be represented in type 'int'
reg_mask(1,31): got 0xFFFFFFFE want 0xFFFFFFFE
[exit 0]
```

- 기본은 **recover**: 보고하고 계속 간다. 프로세스는 exit 0 — CI 가 종료 코드만 보면 **통과로 기록된다**. `-fno-sanitize-recover=all`(빌드) 또는 `UBSAN_OPTIONS=halt_on_error=1`(실행) 이 필요하다. 이 맥에서는 둘 다 `[exit 134]` (abort), VM 의 GCC/Clang 은 `[exit 1]` 이었다.
- 위 두 케이스는 **값이 맞았다**. 그래도 UB 다. `adc_div0`(arm64 의 `sdiv` 는 0 으로 나누면 0 을 돌려준다), `mask_top`, `pkt_seq`, `temp_nan` 도 recover 모드에서 값이 맞았다. "테스트 통과 = UB 없음" 이 아니다.
- recover 모드에서 한 줄에 보고가 여러 개 나올 수 있다: `mask_31` 은 시프트(35:27) → 그 결과 `INT_MIN - 1`(35:37) → 다시 시프트(35:42). 열 번호가 각 연산자 위치다. **첫 줄만 원인**이고 나머지는 연쇄 효과.
- `cal_null`·`mode_bad` 는 보고 후 계속 실행하다 `UndefinedBehaviorSanitizer:DEADLYSIGNAL ... SEGV on unknown address` 로 죽었다 (exit 134). recover 는 "안전하게 계속" 이 아니다.

검사가 실제로 무엇인지는 트랩 빌드의 기계어가 제일 잘 보여 준다 (`demo_trap.sh`, arm64, `-O2`):

```text
0000000100001170 <_adc_scale>:
100001170:     	smull	x8, w0, w1                        ; 64비트 곱
100001174:     	cmp	x8, w8, sxtw                      ; 하위 32비트를 부호 확장한 값과 같은가?
100001178:     	b.ne	0x10000119c <_adc_scale+0x2c>     ; 다르면 곱셈 오버플로
10000117c:     	cmn	w2, #0x1                          ; den == -1 이고
100001180:     	mov	w9, #-0x80000000        ; =-2147483648
100001184:     	ccmp	w8, w9, #0x0, eq                  ; 곱 == INT_MIN 인가?
100001188:     	cset	w9, ne
10000118c:     	cbz	w2, 0x1000011a0 <_adc_scale+0x30> ; den == 0
100001190:     	tbz	w9, #0x0, 0x1000011a0 <_adc_scale+0x30>
100001194:     	sdiv	w0, w8, w2
100001198:     	ret
10000119c:     	brk	#0x550c                           ; mul overflow
1000011a0:     	brk	#0x5503                           ; divrem overflow (0 나눗셈 포함)

00000001000011a4 <_accum_add>:
1000011a4:     	adds	w0, w0, w1
1000011a8:     	b.vs	0x1000011b0 <_accum_add+0xc>      ; V 플래그 = signed overflow
1000011ac:     	ret
1000011b0:     	brk	#0x5500                           ; add overflow
```

검사 비용은 **비교 + 분기 하나**다. 정상 경로는 거의 그대로이고, 실패 경로만 함수 끝에 붙는다.

### Part A — 산술

| 케이스 | 실제 메시지 (요지) | 체크 | 깨진 규칙 | 고친 방법 |
|---|---|---|---|---|
| adc_mul_ovf | `signed integer overflow: 30000 * 100000` | signed-integer-overflow | C11 6.5p5 결과가 타입 범위 밖 | `(int64_t)raw * num` |
| adc_div0 | `division by zero` | integer-divide-by-zero | 6.5.5p5 | `den == 0` 먼저 |
| adc_min_div | `division of -2147483648 by -1` | signed-integer-overflow | 6.5.5p6 몫이 표현 불가 | int64 에서 나눈 뒤 포화 |
| accum_sat(_lo) | `2147483000 + 1000`, `-2147483648 + -1` | signed-integer-overflow | 6.5p5 | int64 합 → 포화 |
| mag_min | `negation of -2147483648` | signed-integer-overflow | 6.5p5 (`-x`) | `0u - (uint32_t)x` |
| mask_top | `left shift of 15 by 28 places cannot be represented in type 'int'` | shift-base | 6.5.7p4 signed E1×2^E2 표현 불가 | `1u` 기반 unsigned |
| mask_31 | `left shift of 1 by 31 places` | shift-base | 같은 조항 | 같음 |
| mask_32 | `shift exponent 32 is too large for 32-bit type 'int'` | shift-exponent | 6.5.7p3 개수 ≥ 폭 | `width >= 32` 분기 |
| align18 | `shift exponent -2 is negative` | shift-exponent | 6.5.7p3 음수 개수 | 방향을 나눔 |

이것들이 **UBSan 없이 `-O2`** 에서 실제로 낸 값 (`demo_catalog.sh` 끝부분): `adc_scale(30000,100000,3)` → `-431655765`, `sample_to_u16(0x3FFFF,18)` → `0x0`, `reg_mask(0,32)` → `0x0`. 마지막 것의 기계어가 재미있다:

```text
0000000000000020 <_reg_mask>:      (starter.o, arm64, -O2, UBSan 없음)
      20:      	mov	w8, #-0x1               ; =-1
      24:      	lsl	w8, w8, w1              ; -1 << width  ← (1 << w) - 1 을 ~(-1 << w) 로 바꿈
      28:      	mvn	w8, w8                  ; ~
      2c:      	lsl	w0, w8, w0              ; << lsb
      30:      	ret
```

arm64 `lsl`(레지스터)은 개수의 하위 5비트만 쓴다 → `-1 << 32` 가 `-1 << 0` = −1 → `~` 하면 **0**. 같은 starter 를 `thumbv7em -O2` 로 컴파일하면 똑같은 모양(`mov.w r2,#0xffffffff; lsl.w r1,r2,r1; mvns r1,r1`)이 나오는데, Arm ARM 의 Thumb LSL(register)는 하위 **8비트**를 써서 32 면 0 → `~0` = **0xFFFFFFFF**, 즉 Cortex-M 에서는 "우연히 정답" 이 될 것으로 예상된다(디스어셈블리는 실측, M4 실행은 못 함 — 확인 권장). **같은 소스가 호스트 테스트에서만 틀리거나, 타깃에서만 틀릴 수 있다** — UB 를 "플랫폼 동작" 으로 믿으면 안 되는 이유다.

고친 코드 요점 (전체는 6. 정답 코드의 diff):

- `adc_scale`: `|raw × num| ≤ 2^62` 라 int64 곱은 절대 넘치지 않는다. 나눗셈도 int64 라 `INT32_MIN / -1 = 2147483648` 은 평범한 값이고, 포화 비교에서 `INT32_MAX` 로 간다. Cortex-M4 에는 64비트 나눗셈 명령이 없어 `__aeabi_ldivmod` 호출이 생긴다(`demo_size.sh` 의 `llvm-nm -u` 에 실제로 보임) — ISR 핫패스라면 `den` 을 고정 시프트로 바꾸는 설계를 고려.
- `sample_mag`: signed → unsigned 변환은 정의됨(6.3.1.3p2, 2^32 를 더함), unsigned 뺄셈도 정의됨. 메시지 자체가 "cast to an unsigned type to negate this value to itself" 라고 힌트를 준다.
- `reg_mask`: `(1u << width) - 1u` 는 `width ≤ 31` 에서만. 32 는 `0xFFFFFFFFu` 상수. `ones << lsb` 는 계약상 `lsb ≤ 31`.

### Part B — 메모리와 표현

**비정렬 load** (`pkt_seq`):

```text
starter.c:51:16: runtime error: load of misaligned address 0x000102cbc371 for type 'const uint32_t' (aka 'const unsigned int'), which requires 4 byte alignment
0x000102cbc371: note: pointer points here
 00 00 00  10 ef be ad de 01 34 12  00 00 00 00 00 00 00 00  00 00 00 00 00 00 00 00  00 00 00 00 00
              ^
```

- 주소 끝자리 `...371` 은 4의 배수가 아니다. 아래 hex 덤프와 `^` 가 포인터 위치 — `ef be ad de` 가 seq 바이트다.
- arm64 맥과 Cortex-M4 의 일반 `LDR` 는 비정렬을 하드웨어가 처리하니 **값은 맞았다**. 그러나 C 에서는 정렬 안 된 포인터를 만드는 순간 UB(6.3.2.3p7)이고, `uint8_t` 버퍼를 `uint32_t` 로 읽는 것은 strict aliasing 위반(C-M13)이기도 하다 — UBSan 은 **aliasing 은 검사하지 않는다**. 실제 위험: Cortex-M0/M0+ 는 비정렬 LDR 이 HardFault, M3/M4 도 `LDRD`/`LDM` 은 비정렬이면 fault (C-M04).
- 고침: 바이트 조립 `(uint32_t)buf[1] | (uint32_t)buf[2] << 8 | ...`. **`(uint32_t)` 캐스트를 빼면** `buf[4] << 24` 가 int 승격(C-B02) 후 부호 비트로 시프트 → 이번 테스트 값 `0xDE` 에서 shift-base 가 잡는다(7. 자주 하는 실수).

**잘못된 bool** (`pkt_ack`, 패킷을 `frame + 3` 에 둬서 seq 는 정렬된 상태):

```text
starter.c:52:16: runtime error: load of value 2, which is not a valid value for type 'const _Bool'
```

UBSan 없이 `-O2` 에서는 `pkt_parse(frame+3, n=8): rc 0 seq 0x00000001 ack 2 len 0x0008` — 드라이버는 `h.ack ? 1 : 0` 을 찍었는데 **2 가 나왔다**. 컴파일러는 bool 이 0 아니면 1 이라고 믿고 `? 1 : 0` 을 바이트 그대로 쓰는 코드로 바꿨다. 고침: `if (buf[5] > 1u) return -2;` 를 먼저, 그 다음 `out->ack = buf[5] != 0u;`.

**enum 은 C 에서 안 잡힌다** — `demo_lang.c` 를 C / C++ 로 컴파일한 실제 결과 (`ubsan_groups.txt`):

```text
--- dl_c                      (C11)
demo_lang.c:18:17: runtime error: left shift of 1 by 31 places cannot be represented in type 'int'
is_stop=0 bit=-2147483648
--- dl_17                     (C++17)
demo_lang.c:17:20: runtime error: load of value 7, which is not a valid value for type 'enum pwr_mode'
is_stop=0 bit=-2147483648
--- dl_17 with wire=3 (not an enumerator, but inside the 2-bit value range 0..3)
is_stop=0 bit=2
```

| 같은 소스 | enum 에 7 로드 | `1 << 31` |
|---|---|---|
| C11 (clang 21 맥, gcc 15.2 VM) | 보고 없음 | **보고** (shift-base) |
| C++17 / C++20 (clang) | **보고** | 보고 없음 |
| C++20, `n = 32` | 보고 | `shift exponent 32 is too large` (개수 ≥ 폭은 여전히 UB) |

- C 에서 enum 은 구현 정의 정수 타입과 호환되고(C11 6.7.2.2p4) 그 타입의 모든 값이 합법이라 "범위 밖 enum" 이라는 UB 자체가 없다. `-###` 로 보면 C 에서도 `enum` 이 목록에 들어 있지만 C 코드에는 효과가 없다. C++ 는 고정 기반 타입이 없는 enum 의 값 범위를 열거자를 담는 최소 비트폭으로 정한다 → 0..3. 그래서 **3 은 열거자가 아닌데도 보고되지 않았다**.
- `1 << 31` 은 C++14 부터 결과가 unsigned 에 들어가면 허용, C++20 은 2의 보수로 정의 — clang 이 C++17 에서도 침묵한 이유 (조항 번호는 확인 권장).
- 그래서 starter 의 `pwr_mode_name(7)` 은 enum 체크가 아니라 **bounds** 체크(`index 7 out of bounds for type 'const char *const[3]'`)가 잡았다. 표 인덱스가 없었다면 C 에서 UBSan 은 아무 말도 안 했을 것이다. 고침은 원시 바이트로 범위 검사: `if (wire > PWR_STOP) return "?";`.

**NULL 과 bounds**:

```text
starter.c:70:19: runtime error: member access within null pointer of type 'const struct cal'
starter.c:81:12: runtime error: index 16 out of bounds for type 'const uint16_t[16]' (aka 'const unsigned short[16]')
```

- null 체크는 접근 **전에** 보고한다. no-recover 면 SEGV 전에 멈추고 정확한 줄을 준다. 많은 Cortex-M 보드(예: Flash 가 0 번지에 alias 되는 STM32)에서는 0 번지가 벡터 테이블이라 NULL 읽기가 **fault 없이 초기 SP 값을 읽어 온다**(MPU 로 막지 않는 한) — 호스트 UBSan 이 더 값진 이유.
- bounds 는 **배열 타입을 컴파일러가 아는** 경우만: `k_therm[i]` 처럼 배열 이름으로 인덱싱할 때. 포인터 인자 `p[i]` 는 크기를 몰라 검사 못 한다 → ASan(TOOL-B11)의 몫.
- 고침: `therm_lookup` 은 `i > 15` 를 15 로 클램프, `cal_apply` 는 NULL 이면 항등, 아니면 포화 덧셈(`accum_add` 재사용 — `x + offset` 도 넘칠 수 있다).

### Part C — float→int, 트랩 모드, 크기

**float-cast-overflow** 메시지와 UBSan 없는 `-O2` 의 실제 동작:

```text
starter.c:89:12: runtime error: 40000 is outside the range of representable values of type 'short'
starter.c:89:12: runtime error: nan is outside the range of representable values of type 'short'
starter.c:98:12: runtime error: -249.75 is outside the range of representable values of type 'unsigned short'

(no sanitizer, -O2; 디스어셈블리는 명령만 한 줄로 요약)
temp_to_centi(400.0): got 40000 want 32767          ← int16_t 함수가 40000 을 "반환"
duty_to_ccr(-0.25,999): got 4294967047 want 0       ← uint16_t 함수가 0xFFFFFF07 을 "반환"
<_temp_to_centi>:  fmul s0, s0, s1 ; fcvtzs w0, s0 ; ret     ← 16비트로 자르는 명령이 없음
<_duty_to_ccr>:    ucvtf s1, w0 ; fmul s0, s0, s1 ; fcvtzs w0, s0 ; ret
```

컴파일러는 "변환 결과는 범위 안이다" 를 가정해도 되므로 32비트 변환 결과를 그대로 반환했고, 호출자는 상위 비트가 이미 정리돼 있다고 믿었다(애플 arm64 ABI 의 좁은 반환값 확장 규칙 — 확인 권장). 결과: `int16_t` 변수가 40000 을 갖는, 소스만 봐서는 불가능한 상태. 반면 `-0.5f` 처럼 **정수 부분이 0** 이면 `unsigned` 변환도 합법(0)이다 — UB 조건은 "잘라낸 정수 부분이 범위 밖" 이다.

고침: 곱한 다음 NaN → 상한 → 하한 순서로 보고, 그 다음 캐스트. `duty_to_ccr` 의 `!(duty > 0.0f)` 는 NaN 까지 한 번에 거른다. `duty_over`(1.5 → 1498)는 **UB 가 아니라서 UBSan 은 침묵**했고 드라이버의 값 비교(`rc 2`)만 잡았다 — 테스트가 계약을 검사해야 하는 이유.

**GCC 는 float-cast-overflow 를 기본 묶음에 넣지 않는다** — VM 실측 (`ubsan_vm.txt`):

```text
### 1. GCC 15.2.0 -fsanitize=undefined (recover) on starter cases
=== temp_hot
temp_to_centi(400.0): got -25536 want 32767                 ← 보고 없음
### 2. GCC: float-cast-overflow is NOT in -fsanitize=undefined; add it explicitly
=== temp_hot
starter.c:89:28: runtime error: 40000 is outside the range of representable values of type 'short int'
```

clang 21 의 `-fsanitize=undefined` 는 `-###` 로 보면 이렇게 펼쳐진다 (실제 출력): `alignment, array-bounds, bool, builtin, enum, float-cast-overflow, integer-divide-by-zero, nonnull-attribute, null, pointer-overflow, return, returns-nonnull-attribute, shift-base, shift-exponent, signed-integer-overflow, unreachable, vla-bound` — 그리고 `--target=thumbv7em-none-eabi` 에서는 여기에 **`function`** 이 추가된다. 같은 GCC 메시지는 타입을 `'int'` 로(clang 은 `'int32_t' (aka 'int')`), 열 번호를 다르게 준다(`64:24` vs `64:12`). GCC 의 `mode_bad` 에는 `load of address ... with insufficient space for an object of type 'const char *'` 가 하나 더 붙었다 (GCC 가 묶음에 넣는 object-size 계열 검사로 보임, 확인 권장).

**트랩 모드** — 런타임이 없으니 메시지도 없다. 대신:

```text
adc_ok       exit 0   adc_scale(1000,3300,4095): got 805 want 805
adc_mul_ovf  exit 133
mask_32      exit 133
pkt_seq      exit 133
duty_over    exit 2   duty_to_ccr(1.5,999): got 1498 want 999
### is the runtime linked?  otool -L trap | grep -c ubsan
0
### all distinct brk immediates in the binary   (uniq -c 출력을 가로로 정리)
   2 brk #0x5500    1 brk #0x5503    2 brk #0x5505    2 brk #0x550a    1 brk #0x550c   1 brk #0x550d
   2 brk #0x5512    4 brk #0x5513    2 brk #0x5514    1 brk #0x5515    3 brk #0x5516
```

arm64 에서는 `brk` 의 하위 바이트가 **검사 종류**다. 실측 대응: 0x00 add, 0x03 divrem, 0x0c mul, 0x0d negate, 0x14 shift (위 `adc_scale`/`accum_add`/`reg_mask`/`sample_mag` 디스어셈블리에서 확인). 나머지(0x05 float-cast, 0x0a load-invalid-value, 0x12 out-of-bounds, 0x13 pointer-overflow, 0x15 sub, 0x16 type-mismatch)는 LLVM 의 SanitizerHandler 순서와 맞는다(소스 기준, 확인 권장). 그리고 lldb 는 디버그 정보에 들어간 트랩 이유를 그대로 보여 준다 (`demo_trap.lldb`, 실제 출력, 소스 줄 일부 생략):

```text
(lldb) run
* thread #1, queue = 'com.apple.main-thread', stop reason = Undefined Behavior Sanitizer: signed integer multiplication overflow in 'raw * num'
    frame #1: 0x000000010000119c trap`adc_scale(raw=30000, num=100000, den=3) at starter.c:13:16 [opt]
-> 13  	    return raw * num / den;
(lldb) bt
    frame #0: 0x000000010000119c trap`__clang_trap_msg$Undefined Behavior Sanitizer$signed integer multiplication overflow in 'raw * num' at starter.c:0 [opt] [inlined]
  * frame #1: 0x000000010000119c trap`adc_scale(raw=30000, num=100000, den=3) at starter.c:13:16 [opt]
    frame #2: 0x0000000100000a4c trap`run(c="adc_mul_ovf") at driver.c:56:80 [opt] [inlined]
(lldb) disassemble -p -c 1
->  0x10000119c <+44>: brk    #0x550c
```

`__clang_trap_msg$...` 는 가짜 인라인 프레임이다 — 런타임 0 바이트로 메시지를 디버그 정보(DWARF)에만 넣었다. Flash 에는 안 들어가고 `.elf` 에만 남으니, 펌웨어에 디버거를 붙여도 같은 프레임 이름이 보일 것이다 (lldb 는 실측, gdb/OpenOCD 쪽 표시는 확인 권장 — TOOL-A03).

Thumb(Cortex-M4)에서는 종류 번호가 없다: clang 은 `defe` (`udf #254`, objdump 는 `trap` 으로 표시), arm-none-eabi-gcc 는 `udf #255` 를 쓴다 (실측). 그리고 한 함수의 모든 체크가 **트랩 하나로 합쳐진다**(`-fsanitize-merge` 기본) — starter 의 `adc_scale` 은 곱셈·0 나눗셈·`INT_MIN/-1` 세 분기가 모두 `2a: defe` 로 간다. `-fno-sanitize-merge` 면 곱셈은 `2a: defe`, 나눗셈 두 검사는 `2c: defe` 로 갈라져 PC 만으로 종류를 구분할 수 있다(실측, `size_m4.txt`).

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">펌웨어 트랩 경로 (Cortex-M): udf → fault → 크래시 로그 → 호스트에서 해석</text>
  <rect x="10" y="40" width="160" height="70" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="22" y="62" fill="currentColor" font-family="monospace">b.cond → udf</text>
  <text x="22" y="82" fill="currentColor" font-size="12">-fsanitize-trap=</text>
  <text x="22" y="98" fill="currentColor" font-size="12">undefined</text>
  <rect x="200" y="40" width="170" height="70" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="212" y="62" fill="currentColor">UsageFault</text>
  <text x="212" y="82" fill="currentColor" font-size="12">UNDEFINSTR (켜져 있으면)</text>
  <text x="212" y="98" fill="currentColor" font-size="12">아니면 HardFault 로 승격</text>
  <rect x="400" y="40" width="160" height="70" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="412" y="62" fill="currentColor">fault 핸들러</text>
  <text x="412" y="82" fill="currentColor" font-size="12">스택 프레임의 PC, LR,</text>
  <text x="412" y="98" fill="currentColor" font-size="12">xPSR → noinit RAM</text>
  <rect x="590" y="40" width="140" height="70" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="602" y="62" fill="currentColor">리셋 후 업로드</text>
  <text x="602" y="82" fill="currentColor" font-size="12">텔레메트리 /</text>
  <text x="602" y="98" fill="currentColor" font-size="12">디버그 UART</text>
  <g stroke="currentColor" fill="none">
    <path d="M170 75 L200 75"/><path d="M192 70 L200 75 L192 80"/>
    <path d="M370 75 L400 75"/><path d="M392 70 L400 75 L392 80"/>
    <path d="M560 75 L590 75"/><path d="M582 70 L590 75 L582 80"/>
    <path d="M660 110 L660 150 L560 150"/><path d="M568 145 L560 150 L568 155"/>
  </g>
  <rect x="300" y="130" width="260" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="312" y="150" fill="currentColor" font-family="monospace" font-size="12">addr2line -e fw.elf 0x0800_1234</text>
  <text x="312" y="166" fill="currentColor" font-size="12">→ 파일:줄 (TOOL-M14)</text>
  <text x="10" y="200" fill="currentColor" font-size="12">대안: -fsanitize-minimal-runtime + 직접 만든 __ubsan_handle_*_minimal() 이 PC 와 종류를 기록하고 계속 실행 (demo_minrt.c).</text>
  <text x="10" y="222" fill="currentColor" font-size="12">-fno-sanitize-merge 면 트랩 PC 마다 체크 하나 → PC 만으로 종류 구분. merge(기본)면 함수당 트랩 하나 → 더 작다.</text>
  <text x="10" y="242" fill="currentColor" font-size="12">UsageFault 활성화 = SCB-&gt;SHCSR 의 USGFAULTENA 비트 (아키텍처 문서로 확인 권장).</text>
</svg>
```

**최소 런타임을 직접 구현** (`demo_minrt.c`): `-fsanitize-minimal-runtime` 은 체크마다 인자 없는 `__ubsan_handle_<kind>_minimal()` 을 부른다. 그 함수들을 펌웨어가 정의하면 된다 — 호출 PC(`__builtin_return_address(0)`)와 종류를 RAM 로그에 남기고 계속. VM 에서 `-no-pie` 로 빌드해 addr2line 까지 돌린 실제 출력:

```text
--- ub log: 4 distinct site(s)
  #0 mul_overflow         pc=0x400e94
  #1 add_overflow         pc=0x400ee0
  #2 shift_out_of_bounds  pc=0x400f88
  #3 out_of_bounds        pc=0x401418
(return address - 4 = the bl that called the handler)
0x400e94 -> adc_scale /tmp/TOOL-B12_work/vm/starter.c:13
0x400ee0 -> accum_add /tmp/TOOL-B12_work/vm/starter.c:22
0x400f88 -> reg_mask /tmp/TOOL-B12_work/vm/starter.c:35
0x401418 -> therm_lookup /tmp/TOOL-B12_work/vm/starter.c:81
```

같은 사이트를 두 번 부른 `adc_scale` 은 한 번만 기록됐다(로그 중복 제거). VM 의 clang 이 제공하는 진짜 최소 런타임(`libclang_rt.ubsan_minimal`)은 `ubsan: mul-overflow by 0x0000b1ddbca430d8` 한 줄만 찍는다. 이 맥의 Apple clang 은 `-fsanitize-minimal-runtime` 에 `clang: warning: the clang compiler does not support '-fsanitize-minimal-runtime'` 을 내고, 링크하면 `libclang_rt.ubsan_minimal_osx_dynamic.dylib` 이 없어 실패했다 — 그래서 데모는 `-fno-sanitize-link-runtime` 으로 런타임을 빼고 직접 만든 핸들러를 링크한다(경고는 그대로 나옴).

**크기 비용** — `demo_size.sh`, 맥 clang 21 → `thumbv7em-none-eabi -mcpu=cortex-m4 -mfloat-abi=hard -Os`, `llvm-size` Berkeley (`text` 는 .text + .rodata + .ARM.exidx):

| 모드 (solution.c) | 플래그 | text | data | 합계 | 트랩 / 핸들러 호출 |
|---|---|---:|---:|---:|---|
| none | — | 525 | 0 | 525 | 0 / 0 |
| trap-subset | `-fsanitize=signed-integer-overflow,shift,integer-divide-by-zero,bounds,float-cast-overflow -fsanitize-trap=all` | 617 | 0 | 617 (+18%) | 4 / 0 |
| trap-nofunc | trap + `-fno-sanitize=function` | 681 | 0 | 681 (+30%) | 8 / 0 |
| trap | `-fsanitize=undefined -fsanitize-trap=undefined` | 777 | 0 | 777 (+48%) | 8 / 0 |
| trap-nomerge | trap + `-fno-sanitize-merge` | 817 | 0 | 817 (+56%) | 19 / 0 |
| minimal-rt | `-fsanitize=undefined -fsanitize-minimal-runtime` | 1069 | 0 | 1069 (+104%) | 0 / 27 |
| full-rt | `-fsanitize=undefined` | 1898 | 596 | 2494 (+375%) | 0 / 27 |

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">solution.c 를 Cortex-M4 (-Os) 로: UBSan 모드별 바이트 (막대 1 px = 5 B)</text>
  <g fill="currentColor" font-size="12">
    <text x="10" y="52">none</text><text x="10" y="80">trap-subset</text><text x="10" y="108">trap-nofunc</text>
    <text x="10" y="136">trap</text><text x="10" y="164">trap-nomerge</text><text x="10" y="192">minimal-rt</text>
    <text x="10" y="220">full-rt</text>
  </g>
  <rect x="120" y="40" width="105" height="16" class="f-ok" stroke="currentColor"/>
  <rect x="120" y="68" width="123" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="120" y="96" width="136" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="120" y="124" width="155" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="120" y="152" width="163" height="16" class="f-acc" stroke="currentColor"/>
  <rect x="120" y="180" width="214" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="120" y="208" width="380" height="16" class="f-mut" stroke="currentColor"/>
  <rect x="500" y="208" width="119" height="16" class="f-warn" stroke="currentColor"/>
  <g fill="currentColor" font-size="12" font-family="monospace">
    <text x="232" y="52">525</text><text x="250" y="80">617</text><text x="263" y="108">681</text>
    <text x="282" y="136">777</text><text x="290" y="164">817</text><text x="341" y="192">1069</text>
    <text x="626" y="220">1898+596</text>
  </g>
  <text x="10" y="248" fill="currentColor" font-size="12">full-rt 의 596 B 는 .data: 체크마다 파일명·줄·열·타입 기술자. 그리고 링크 시 런타임 라이브러리가 별도로 필요하다.</text>
</svg>
```

읽는 법:

- **trap(+48%)** 은 런타임 없이 링크된다. 이 중 96 B(777 → 681)는 `function` 체크의 몫이다 — Thumb 타깃에서만 묶음에 들어가고, 함수마다 앞에 서명 워드(`0xc105cafe` + 타입 해시)를 붙여 함수 포인터 간접 호출의 타입을 검사한다. 간접 호출이 거의 없는 모듈이면 `-fno-sanitize=function` 이 거의 공짜 절약이다.
- **trap-subset(+18%)** — 산술·시프트·나눗셈·bounds·float-cast 만. 펌웨어에서 "가장 흔한 UB 만 상시 트랩" 으로 쓰기 좋은 절충. alignment / null 은 MCU 에선 하드웨어 fault 와 겹치거나(M0 비정렬) 오탐이 적은 대신 효용도 낮아 빼는 경우가 많다(프로젝트 판단).
- **minimal-rt(+104%)** 는 트랩보다 크다: 체크마다 `bl` 호출 + recover 를 위한 레지스터 보존. 대신 **멈추지 않고 기록**할 수 있다.
- **full-rt** 는 MCU 용이 아니다: `.data` 596 B (Flash 와 RAM 둘 다 차지, TOOL-B06) + 링크 시 printf 기반 런타임. 그리고 `arm-none-eabi` 에는 libubsan 이 없다 (`arm-none-eabi-nm -u solution-full.o` → `U __ubsan_handle_out_of_bounds` 등 미해결).

VM 의 arm-none-eabi-gcc 14.2.1 로 같은 측정 (실제 출력):

```text
--- solution.c   (arm-none-eabi-size, Berkeley)
   text	   data	    bss	    dec	    hex	filename
    453	      0	      0	    453	    1c5	solution-none.o
    633	      0	      0	    633	    279	solution-trap.o      ← -fsanitize-undefined-trap-on-error
    633	      0	      0	    633	    279	solution-trap2.o     ← -fsanitize-trap=undefined (GCC 도 받음, 같은 결과)
   1044	    697	      0	   1741	    6cd	solution-full.o
    traps (udf) in solution-trap.o: 6
--- starter.c
    219 → 431 (trap), 909 + 809 (full)
```

GCC 는 +40%(453 → 633). 두 컴파일러 모두 **트랩 모드 ≈ 코드의 +40~50%** 가 이 모듈의 실측이다. 체크 밀도에 따라 다르므로(이 모듈은 산술·캐스트 위주라 밀도가 높다) 자기 펌웨어 전체로 다시 재야 한다 — 맵 파일로 어디가 커졌는지 보는 법은 TOOL-M02.

**펌웨어에서 실제로 쓰는 형태** (정리):

| 어디서 | 무엇을 | 왜 |
|---|---|---|
| 호스트 단위 테스트 / CI | `-fsanitize=undefined -fno-sanitize-recover=all` (+ ASan, TOOL-B11) | 위치·값이 담긴 메시지. CI 는 종료 코드로 판정 |
| HIL / 개발 보드 빌드 | `-fsanitize=undefined -fsanitize-trap=undefined` (+ `-fno-sanitize-merge` 선택) | 런타임 0, 디버거가 붙으면 lldb/gdb 가 트랩 위치 표시 |
| 양산 빌드 (선택) | 좁힌 부분집합 + trap, 또는 최소 런타임 + 직접 핸들러 | Flash 예산 안에서 "조용한 오답" 대신 크래시 로그 |
| 하지 않는 것 | 전체 런타임을 MCU 에 | 런타임·문자열·.data 비용, arm-none-eabi 에 런타임 없음 |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

`bash check.sh solution` 실제 출력:

```text
PASS canary: planted INT_MAX + 1 reported, process stopped (rc 134)
PASS adc_ok
PASS adc_mul_ovf
… (adc_div0 ~ duty_over: 27줄 모두 PASS, 생략)
PASS trap build (-O2 -fsanitize-trap=undefined): all 29 cases rc 0
PASS Cortex-M4 object (thumbv7em -Os -Werror): text 525 B, with UBSan traps 777 B
32/32 passed
```

UB-free 로 고치니 기본 크기가 309 → 525 B 로 늘었다 (포화 비교, int64 연산, 범위 검사). 정확함의 비용이고, 트랩 모드 추가분(+252 B)과는 별개다.

## 7. 자주 하는 실수

**1. 기본 빌드로 CI 를 돌린다.** 메시지가 로그에 묻히고 exit 0.

```bash
cc -fsanitize=undefined ...                              # 보고 후 계속, exit 0
cc -fsanitize=undefined -fno-sanitize-recover=all ...    # 첫 보고에서 중단 (맥 134, Linux 1)
UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1 ./test  # 이미 빌드된 바이너리라면
```

**2. 바이트 조립에서 캐스트를 빠뜨린다.** `buf[4] << 24` 는 `uint8_t` → `int` 승격 후 시프트 — `buf[4] >= 0x80` 이면 부호 비트로 시프트(UB, C-B02, C-B03). 이 레슨의 seq 값 `0xDEADBEEF` 의 최상위 바이트 0xDE 가 정확히 그 경우다.

```c
out->seq = buf[1] | buf[2] << 8 | buf[3] << 16 | buf[4] << 24;          /* UB when buf[4] >= 0x80 */
out->seq = (uint32_t)buf[1] | (uint32_t)buf[2] << 8 |
           (uint32_t)buf[3] << 16 | (uint32_t)buf[4] << 24;             /* defined */
```

solution.c 에서 이 캐스트만 지운 사본을 같은 검사기 플래그로 돌린 실제 결과 (`pkt_seq`):

```text
nocast.c:52:38: runtime error: left shift of 222 by 24 places cannot be represented in type 'int'
```

222 = 0xDE. 비정렬 load 를 고치다가 새 UB 를 만드는 전형적인 경로다.

**3. 캐스트한 다음 검사한다.** `bool`, `enum`, float→int 모두 "변환 순간" 이 UB 다.

```c
int16_t c = (int16_t)(deg * 100.0f); if (c > 30000) ...   /* 이미 늦음 */
float v = deg * 100.0f; if (v >= 32767.0f) return INT16_MAX; ... return (int16_t)v;
```

**4. "UBSan 이 조용하니 맞다".** UBSan 은 **실행된 경로**의 **UB** 만 본다. `duty_over`(논리 버그), C 의 범위 밖 enum, 범위 안의 잘못된 enum(C++ 의 3), strict aliasing, 데이터 레이스(TOOL-M09), 포인터 인자 인덱싱(ASan 의 몫)은 모두 침묵. 커버리지와 계약 테스트가 같이 있어야 한다.

**5. 오버플로 검사를 오버플로로 한다.**

```c
if (a + b < a) ...                 /* signed: 이미 UB, b > 0 이 알려지면 컴파일러가 false 로 지울 수 있음 (TOOL-B03) */
int64_t s = (int64_t)a + b;        /* 넓혀서 */
if (s > INT32_MAX) ...             /* 그 다음 비교 */
```

**6. unsigned wrap 을 끄려고 sanitizer 를 통째로 끈다.** 의도된 wrap(틱 카운터, 해시)은 `__attribute__((no_sanitize("unsigned-integer-overflow")))` 로 **그 함수만** 표시 (9. 추가 연습).

## 8. 면접 Q&A

<details>
<summary>Q1. UBSan 은 어떻게 동작하나요? ASan 과 뭐가 다른가요?</summary>

컴파일 시 계측이다. 위험한 연산(산술, 시프트, 캐스트, 역참조, 인덱싱) 바로 앞에 조건 검사와 분기를 넣고, 실패하면 런타임 핸들러를 부르거나(기본) 트랩 명령을 실행한다. shadow 메모리나 할당자 교체가 없어 메모리 오버헤드가 거의 없고, ASan 과 함께(`-fsanitize=address,undefined`) 쓰는 게 보통이다. ASan 은 **메모리 접근의 유효성**(힙/스택 범위, 해제 후 사용)을 shadow 로 보고, UBSan 은 **연산 자체의 언어 규칙**을 본다. 그래서 포인터 인자의 범위 밖 접근은 ASan, `INT_MAX + 1` 은 UBSan. 펌웨어로 가져갈 수 있는 건 UBSan(트랩 모드) 쪽이다.

</details>

<details>
<summary>Q2. 꼬리: 그럼 기본 빌드에서 UB 가 나면 테스트가 실패하나요?</summary>

아니다. 기본은 recover — 메시지를 stderr 에 찍고 계속 실행해서 exit 0 이 가능하다(이 레슨에서 `mag_min` 등 실측). CI 에서는 `-fno-sanitize-recover=all` 로 빌드하거나 `UBSAN_OPTIONS=halt_on_error=1` 로 실행한다. 이 맥은 abort(셸 134), Linux 는 exit 1 이었다. 반대로 개발 중 "한 번에 다 보고 싶다" 면 recover 가 편하다 — 단 첫 보고 이후의 보고는 연쇄 효과일 수 있다(`mask_31` 의 세 줄).

</details>

<details>
<summary>Q3. 펌웨어(Cortex-M)에서 UBSan 을 어떻게 쓰나요?</summary>

`-fsanitize=undefined -fsanitize-trap=undefined` 로 런타임 없이 빌드한다. 체크 실패 = `udf` 명령 → UsageFault(UNDEFINSTR), 활성화 안 됐으면 HardFault. fault 핸들러가 스택 프레임의 PC/LR/xPSR 을 noinit RAM 에 저장하고 리셋, 다음 부팅에 텔레메트리로 올리면 호스트에서 `addr2line` 으로 줄을 찾는다. 디버거가 붙어 있으면 lldb 가 `stop reason = Undefined Behavior Sanitizer: signed integer multiplication overflow in 'raw * num'` 처럼 이유까지 보여 준다(이 맥에서 실측 — DWARF 에 넣은 `__clang_trap_msg` 덕분). 크래시 대신 기록하고 계속 가고 싶으면 `-fsanitize-minimal-runtime` + 직접 만든 `__ubsan_handle_*_minimal`.

</details>

<details>
<summary>Q4. 꼬리: 비용은 얼마나 들어요? 양산에도 넣나요?</summary>

이 레슨의 모듈 실측(Cortex-M4 `-Os`): clang 525 → 777 B(+48%), GCC 453 → 633 B(+40%). 그중 clang 의 `function` 체크 96 B 는 `-fno-sanitize=function` 으로 뺄 수 있고, 산술·시프트·bounds·float-cast 만 남기면 +18%. 실행 시간은 정상 경로에 비교+분기 하나씩이라 보통 작지만 핫 루프에서는 벡터화나 루프 최적화를 막을 수 있어 직접 재야 한다. 양산 적용은 팀 결정: Flash 여유와 "조용한 오답 vs 리셋" 중 무엇이 더 위험한지(안전 관련이면 보통 fault 가 낫다). 최소한 HIL 과 개발 빌드에는 넣는 걸 권한다.

</details>

<details>
<summary>Q5. `-fsanitize=undefined` 에 무엇이 들어 있고 무엇이 빠져 있나요?</summary>

clang 21(`-###` 실측): alignment, array-bounds, bool, builtin, enum, float-cast-overflow, integer-divide-by-zero, nonnull-attribute, null, pointer-overflow, return, returns-nonnull-attribute, shift-base, shift-exponent, signed-integer-overflow, unreachable, vla-bound (+ Thumb 타깃에서는 function). 빠진 것: **UB 가 아닌** unsigned-integer-overflow, implicit-conversion(축소·부호 변경), 그리고 local-bounds 등. GCC 15 는 float-cast-overflow 가 기본 묶음에 없다(VM 실측). 언어 차이: C 에서는 enum 체크가 효과 없고(C 의 enum 은 정수 타입과 호환), C++17 부터 `1 << 31` 은 보고되지 않는다.

</details>

<details>
<summary>Q6. 비정렬 접근이 arm64 와 Cortex-M4 에선 그냥 되는데 왜 고쳐야 하나요?</summary>

C 에서는 하드웨어와 무관하게 UB 다(정렬 안 된 포인터로의 변환 6.3.2.3p7, 그리고 `uint8_t[]` 를 `uint32_t` 로 읽는 건 strict aliasing 위반). 컴파일러는 정렬을 가정해 `LDRD`/`LDM`/벡터 load 로 합칠 수 있고, 이것들은 M3/M4 에서도 비정렬이면 fault 다. M0/M0+ 는 일반 LDR 도 fault. 같은 코드가 칩을 바꾸거나 최적화 레벨을 바꾸는 순간 터진다. 해법은 바이트 조립(엔디안도 명시) 또는 `memcpy`(컴파일러가 단일 LDR 로 바꿔 줌, 정의된 동작). packed 구조체는 컴파일러가 비정렬을 안다는 점에서 낫지만 멤버 주소를 포인터로 넘기면 같은 문제(C-M04).

</details>

<details>
<summary>Q7. UBSan 이 조용하면 UB 가 없는 건가요?</summary>

아니다. (1) 실행된 경로만 본다 — 커버리지가 낮으면 의미가 없다. (2) 계측 안 된 코드(미리 빌드된 라이브러리, 어셈블리)는 못 본다. (3) 검사 종류 밖의 UB: strict aliasing, 데이터 레이스(TSan), 수명 끝난 객체(ASan), 시퀀스 포인트 위반(`i = i++`, 경고가 잡음), C 의 범위 밖 enum. (4) 최적화가 UB 를 근거로 코드를 먼저 지워 버린 경우 검사도 같이 사라질 수 있어 `-O1` 정도로 돌리는 게 보통이다. 그래서 컴파일 경고(TOOL-B02), 정적 분석(TOOL-M12), ASan/TSan 과 겹쳐서 쓴다.

</details>

<details>
<summary>Q8. 의도된 unsigned wrap(타이머 틱, 해시)은 어떻게 다루나요?</summary>

unsigned 산술의 wrap 은 C 에서 **정의된 동작**이라 `-fsanitize=undefined` 는 아무 말도 안 한다. 하지만 버그일 때도 많아(크기 계산 `off + len`) 더 엄격한 CI 에서는 `-fsanitize=unsigned-integer-overflow` 를 켜고, 의도된 곳만 `__attribute__((no_sanitize("unsigned-integer-overflow")))` 로 표시한다. 틱 비교는 `(int32_t)(a - b) > 0` 같은 리눅스 `time_after` 관용구보다 순수 unsigned `(a - b) - 1u < 0x7FFFFFFFu` 가 구현 정의 변환 없이 이식성 있다 (9. 추가 연습 P1).

</details>

## 9. 추가 연습

`practice.c` 의 함수들은 **더 엄격한** 집합 `-fsanitize=undefined,unsigned-integer-overflow,implicit-conversion -fno-sanitize-recover=all` 에서 깨끗해야 한다. 아래 순진한 버전들이 실제로 무엇을 내는지는 `demo_strict.c` 로 확인했다:

```text
$ cc -std=c11 -g -O1 -fsanitize=undefined demo_strict.c && ./a.out          # UB 가 아니므로 침묵
q15_mul_naive(-1.0,-1.0) = -32768 (want 32767)
span_ok_naive(64, 8, SIZE_MAX) = 1 (want 0)
put_le16_naive -> 34 12
$ cc ... -fsanitize=undefined,unsigned-integer-overflow,implicit-conversion demo_strict.c && ./a.out
demo_strict.c:12:12: runtime error: implicit conversion from type 'int' of value 32768 (32-bit, signed) to type 'int16_t' (aka 'short') changed the value to -32768 (16-bit, signed)
demo_strict.c:17:16: runtime error: unsigned integer overflow: 8 + 18446744073709551615 cannot be represented in type 'size_t' (aka 'unsigned long')
demo_strict.c:22:12: runtime error: implicit conversion from type 'uint16_t' (aka 'unsigned short') of value 4660 (16-bit, unsigned) to type 'uint8_t' (aka 'unsigned char') changed the value to 52 (8-bit, unsigned)
```

세 번째(`p[0] = v` 로 하위 바이트만 쓰는 것)는 **의도된** 축소다 — implicit-conversion 은 이런 "오탐성" 보고를 내므로 명시적 캐스트 `(uint8_t)v` 로 의도를 적는다.

**P1. 틱 카운터.** 32비트 free-running 틱에서 `ticks_elapsed(now, then)` 과 `tick_after(a, b)` 를 wrap 에 안전하게. unsigned-integer-overflow 체크가 켜져 있다.

<details>
<summary>답</summary>

`now - then` 은 2^32 모듈로 정답이다(5 − 0xFFFFFFF0 = 21). 의도된 wrap 이므로 함수에 `no_sanitize("unsigned-integer-overflow")` 를 붙인다(`WRAP_OK` 매크로). `tick_after` 는 `(a - b) mod 2^32` 가 1..2^31−1 이면 a 가 뒤 → `(uint32_t)(a - b) - 1u < 0x7FFFFFFFu`. a == b 면 0 − 1 이 0xFFFFFFFF 가 되어 false. 검사기의 negative control 은 `-DWRAP_OK=` 로 표시를 지우면 `practice.c:23:16: runtime error: unsigned integer overflow: 5 - 4294967280` 이 나는지 확인한다(실측).

</details>

**P2. Q15 곱셈 포화.** `q15_mul(-32768, -32768)` (−1.0 × −1.0) 은 32767 이어야 한다. 음수의 `>>` 는 구현 정의이니 피한다 (C-A09).

<details>
<summary>답</summary>

`int32_t p = (int32_t)a * b;` (|p| ≤ 2^30), `p /= 32768;` (0 방향 버림, 시프트 아님), `p > INT16_MAX` 면 포화. 순진한 `return (a * b) >> 15;` 는 UB 는 아니지만(int 에서 계산, 축소는 구현 정의) −32768 을 돌려준다 — implicit-conversion 만 잡는다(위 출력). 버림 방향이 바뀌므로 `q15_mul(-3, 16384) == -1` 로 테스트에 고정해 두었다(`>> 15` 였다면 −2).

</details>

**P3. 넘치지 않는 범위 검사.** `span_ok(cap, off, len)`: `[off, off+len)` 이 `[0, cap)` 안인가. `len = SIZE_MAX` 가 들어와도 맞아야 한다.

<details>
<summary>답</summary>

`return off <= cap && len <= cap - off;` — `off + len` 을 아예 계산하지 않는다. `off + len <= cap` 은 size_t 가 wrap(정의된 동작)해서 `span_ok(64, 8, SIZE_MAX)` 가 참이 된다(위 실측) — 패킷 길이 필드를 믿는 파서의 고전적 버퍼 오버런. 포인터로 `p + len <= end` 라고 쓰면 그쪽은 진짜 UB(pointer-overflow 체크 대상)다.

</details>

**P4. 아무 주소에서나 쓰고 읽기.** `put_le32(p, v)` 와 `get_be16(p)` 를 홀수 주소에서도 정의된 동작으로.

<details>
<summary>답</summary>

바이트 단위, 명시적 캐스트: `p[0] = (uint8_t)v; ... p[3] = (uint8_t)(v >> 24);`, `return (uint16_t)((unsigned)p[0] << 8 | p[1]);`. `(unsigned)` 로 승격을 unsigned 로 고정하면 `<< 8` 이 부호 비트 근처에 가도 안전하다(16비트라 int 로도 안전하지만 습관). 엄격 집합에서 implicit-conversion 이 조용하려면 축소를 모두 명시해야 한다.

</details>

**P5. FNV-1a 해시.** 곱셈 wrap 이 알고리즘의 일부다. `fnv1a("a") == 0xE40C292C`, `fnv1a("abc") == 0x1A47E90B`.

<details>
<summary>답</summary>

`h = 2166136261u; for each byte: h ^= b; h *= 16777619u;` — 함수에 `WRAP_OK`. 전역으로 `-fno-sanitize=unsigned-integer-overflow` 를 주면 P3 같은 진짜 버그도 같이 놓친다. 표시를 지운 빌드에서는 `practice.c:65:11: runtime error: unsigned integer overflow: 2166136228 * 16777619` 가 실제로 나왔다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
bash check.sh practice
```

실제 출력:

```text
PASS canary: planted INT_MAX + 1 reported, process stopped (rc 134)
PASS practice.c: ticks_elapsed(100u, 40u) == 60u
PASS practice.c: ticks_elapsed(5u, 0xFFFFFFF0u) == 21u
… (P1 ~ P5 의 나머지 CHECK 16줄 모두 PASS, 생략)
PASS practice.c strict sanitizer run: rc 0, no runtime error
PASS practice.c trap build (-O2): rc 0
PASS negative control -DWRAP_OK= : practice.c:23:16 unsigned wrap reported (rc 134)
22/22 passed
```

## 10. 핵심 정리

- UBSan = 위험한 연산 앞에 **비교 + 분기**를 넣는 컴파일 시 계측. 메시지 `file:line:col: runtime error: ...` 의 문구가 체크 이름(signed-integer-overflow, shift-base/exponent, alignment, bool, null, bounds, float-cast-overflow …)을 알려 준다.
- 기본은 **recover → exit 0**. CI 는 `-fno-sanitize-recover=all` (맥 abort 134 / Linux exit 1). 값이 맞아도 UB 다(`mag_min`, `pkt_seq` 실측).
- 고치는 공식: **넓혀서 계산 → 범위 검사 → 좁히기**, 부호 반전·마스크는 **unsigned**, 와이어 바이트는 **바이트로 읽고 검사한 뒤** 타입을 입힌다, float→int 는 **캐스트 전에** NaN·범위.
- 못 잡는 것: 실행 안 된 경로, 논리 버그, C 의 enum, strict aliasing, 포인터 인자 인덱싱(ASan), 레이스(TSan). UB 아닌 위험은 opt-in: unsigned-integer-overflow, implicit-conversion + 함수 단위 `no_sanitize`.
- 펌웨어 = **트랩 모드** `-fsanitize-trap=undefined`: 런타임 0, arm64 `brk #0x55NN`(종류 번호) / Thumb `udf` → UsageFault/HardFault → 크래시 로그 → addr2line. 대안: 최소 런타임 + 직접 만든 핸들러.
- 실측 비용(Cortex-M4 -Os): clang +48% (function 체크 제외 +30%, 부분집합 +18%), GCC +40%, 전체 런타임은 +375% 에 .data 까지 — MCU 엔 안 넣는다.
- GCC 는 float-cast-overflow 가 기본 묶음에 없다. Thumb 의 clang 묶음에는 function 이 추가된다. 항상 `-###` 으로 확인.

## 참고자료

- Clang UndefinedBehaviorSanitizer 문서 — 체크 목록, 트랩 모드, 최소 런타임, `no_sanitize`: https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html
- GCC Instrumentation Options (`-fsanitize=undefined`, `-fsanitize-undefined-trap-on-error`, `float-cast-overflow`): https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
- ISO/IEC 9899:2011 (C11) 6.3.1.3, 6.3.1.4, 6.3.2.3p7, 6.5p5, 6.5.5, 6.5.7, 6.7.2.2 — N1570 초안: https://www.open-std.org/jtc1/sc22/wg14/www/docs/n1570.pdf
- John Regehr, "A Guide to Undefined Behavior in C and C++": https://blog.regehr.org/archives/213
- Arm, Cortex-M4 Devices Generic User Guide — UsageFault, HardFault, SHCSR: https://developer.arm.com/documentation/dui0553/latest/
- LLVM 소스 `clang/lib/CodeGen/SanitizerHandler.h` (트랩 종류 번호 순서): https://github.com/llvm/llvm-project
- 관련 레슨: C-M12 (UB 카탈로그), C-B02, C-B03, C-M04, C-M05, C-M13, C-A09, TOOL-B02 (경고), TOOL-B03 (최적화와 UB), TOOL-B06 (size), TOOL-B11 (ASan), TOOL-M09 (TSan), TOOL-M12 (정적 분석), TOOL-M14 (addr2line), TOOL-A05 (크래시 덤프 포맷)
