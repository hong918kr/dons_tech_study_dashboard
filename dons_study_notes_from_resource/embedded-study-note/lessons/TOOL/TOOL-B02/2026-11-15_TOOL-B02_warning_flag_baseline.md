---
id: TOOL-B02
time: 60분
updated: 2026-11-15
---
# TOOL-B02 경고 플래그 기준선: -Wall -Wextra -Werror 외

## 학습 목표

- 의도적 버그 12개가 든 `bugs12.c` 에 플래그를 **하나씩** 붙여 보며, 어떤 경고(warning)가 어떤 버그를 잡는지 표로 만들 수 있다 (Mac clang 21 + VM gcc 15.2 실측).
- `-Wall` 이 "전부" 가 아니라는 것, 그리고 clang 과 GCC 가 **기본값 · 그룹 구성 · 경고 위치 · 최적화 의존성**에서 서로 다르다는 것을 실측 예로 설명할 수 있다.
- `-Wconversion` 의 잡음(noise)이 호스트(LP64)와 Cortex-M(ILP32)에서 다르게 나오는 이유, 벤더 헤더 경고를 `-isystem` 으로 처리하는 방법을 안다.
- 프로젝트 경고 기준선(baseline)을 확정하고, `-Werror` 를 "고정된 툴체인의 CI 빌드" 에서만 켜는 정책과 GCC 전용 플래그 분기를 Makefile 로 구현할 수 있다.

## 1. 문제

"우리 펌웨어 빌드는 경고 플래그를 뭘 쓰나요? `-Wall` 이면 충분한가요?" — 펌웨어 면접에서 자주 나오는 질문이고, 실제 팀에서는 새 프로젝트를 시작하거나 SSD 컨트롤러 펌웨어 같은 큰 코드베이스의 CI 를 정리할 때 반드시 정해야 하는 것이다. 디버거도 printf 도 쓰기 어려운 타깃에서 **컴파일러 경고는 가장 싼 버그 탐지기**다. 컴파일 한 번에 공짜로 나온다.

이 레슨의 대상은 작은 UART/센서 모듈 `bugs12.c` 다. 줄마다 `// BUGnn` 태그가 붙은 버그 12개가 있고, 이 파일은 **고치지 않는다**. 대신 플래그 파일(`<variant>.flags`)을 고쳐서 12개가 전부 경고로 드러나게 하면서, 같은 모듈을 올바르게 쓴 `good.c` 는 `-Werror` 로도 조용히 빌드되게 만든다 — "모든 버그는 잡고, 정상 코드에는 잡음이 0".

| 태그 | 줄 | 버그 | 펌웨어에서 일어나는 일 |
|---|---|---|---|
| BUG01 | 29/34 | `adc_average`: `n <= 0` 이면 `sum` 미초기화 | 빈 샘플 버퍼에서 스택 쓰레기로 평균 → 엉뚱한 과열 판정 |
| BUG02 | 41 | `int16_t` 값과 `unsigned limit` 비교 | `-5` 가 `4294967291` 로 바뀌어 "limit 초과" 로 셈 (C-B02 승격 규칙) |
| BUG03 | 50 | 안쪽 블록의 `size_t len` 이 매개변수 `len` 을 가림(shadowing) | 그 블록 안에선 버퍼 길이 검사가 헤더 값 검사로 바뀜 |
| BUG04 | 58 | `printf("%u", uint64_t)` | 형식 불일치 = 정의되지 않은 동작(UB). 32비트 타깃에선 뒤 인자까지 어긋날 수 있다 |
| BUG05 | 63 | `int` 결과를 `uint8_t` 에 대입 | `pwm_duty(2000)` → 510 → **254** 로 잘림 |
| BUG06 | 70 | `int32_t` 음수를 `uint32_t` 로 반환 | `temp_offset(100)` = −300 → 4294966996 |
| BUG07 | 80 | non-void 함수 끝에서 `return` 누락 | UB. 보통은 r0/x0 에 남은 값이 "CRC OK" 결과가 된다 |
| BUG08 | 89 | `warn_unused_result` 인 `flash_erase()` 결과 무시 | 지우기 실패를 모른 채 다음 프로그램(write) → 데이터 깨짐 |
| BUG09 | 97/98 | `case 9600:` 끝에 `break` 없음 | 9600 bps 가 115200 의 분주값 139 로 설정됨 |
| BUG10 | 110 | `if (retries = 0)` | 항상 거짓 + 인자를 0 으로 덮음 → 링크 체크가 항상 성공 |
| BUG11 | 117 | 배열 매개변수에 `sizeof` | 64 가 아니라 포인터 크기(Mac 8, Cortex-M 4) |
| BUG12 | 122 | `default:` 있는 enum switch 에서 `ST_CRC` 누락 | 상태를 추가해도 아무 경고 없이 `"?"` — 현장 로그만 이상해짐 |

아래 그림은 이 레슨의 결론을 미리 보여 준다 — 플래그를 한 단계씩 더했을 때 12개 중 몇 개가 잡히는지(`demo_matrix.sh` 실측).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">플래그를 하나씩 더할 때 bugs12.c 에서 잡히는 버그 수 (12개 중, -O2, 실측)</text>
  <rect x="430" y="30" width="14" height="12" class="f-acc" stroke="currentColor"/>
  <text x="450" y="41" fill="currentColor" font-size="12">Mac clang 21</text>
  <rect x="550" y="30" width="14" height="12" class="f-ok" stroke="currentColor"/>
  <text x="570" y="41" fill="currentColor" font-size="12">VM gcc 15.2 (괄호: -O0)</text>
  <line x1="230" y1="50" x2="230" y2="292" stroke="currentColor"/>
  <line x1="590" y1="50" x2="590" y2="292" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="572" y="306" fill="currentColor" font-size="12">12</text>
  <text x="225" y="306" fill="currentColor" font-size="12">0</text>
  <text x="10" y="72" fill="currentColor" font-family="monospace" font-size="12">1  -Wall</text>
  <rect x="230" y="58" width="180" height="14" class="f-acc" stroke="currentColor"/>
  <text x="416" y="70" fill="currentColor" font-size="12">6</text>
  <rect x="230" y="74" width="180" height="14" class="f-ok" stroke="currentColor"/>
  <text x="416" y="86" fill="currentColor" font-size="12">6 (5)</text>
  <text x="10" y="112" fill="currentColor" font-family="monospace" font-size="12">2  + -Wextra</text>
  <rect x="230" y="98" width="210" height="14" class="f-acc" stroke="currentColor"/>
  <text x="446" y="110" fill="currentColor" font-size="12">7</text>
  <rect x="230" y="114" width="240" height="14" class="f-ok" stroke="currentColor"/>
  <text x="476" y="126" fill="currentColor" font-size="12">8 (7)</text>
  <text x="10" y="152" fill="currentColor" font-family="monospace" font-size="12">3  + -Wshadow</text>
  <rect x="230" y="138" width="240" height="14" class="f-acc" stroke="currentColor"/>
  <text x="476" y="150" fill="currentColor" font-size="12">8</text>
  <rect x="230" y="154" width="270" height="14" class="f-ok" stroke="currentColor"/>
  <text x="506" y="166" fill="currentColor" font-size="12">9 (8)</text>
  <text x="10" y="192" fill="currentColor" font-family="monospace" font-size="12">4  + -Wconversion</text>
  <rect x="230" y="178" width="300" height="14" class="f-acc" stroke="currentColor"/>
  <text x="536" y="190" fill="currentColor" font-size="12">10</text>
  <rect x="230" y="194" width="330" height="14" class="f-ok" stroke="currentColor"/>
  <text x="566" y="206" fill="currentColor" font-size="12">11 (10)</text>
  <text x="10" y="232" fill="currentColor" font-family="monospace" font-size="12">5  + -Wimplicit-fallthrough</text>
  <rect x="230" y="218" width="330" height="14" class="f-acc" stroke="currentColor"/>
  <text x="566" y="230" fill="currentColor" font-size="12">11</text>
  <rect x="230" y="234" width="330" height="14" class="f-ok" stroke="currentColor"/>
  <text x="566" y="246" fill="currentColor" font-size="12">11 (10)</text>
  <text x="10" y="272" fill="currentColor" font-family="monospace" font-size="12">6  + -Wswitch-enum</text>
  <rect x="230" y="258" width="360" height="14" class="f-acc" stroke="currentColor"/>
  <text x="596" y="270" fill="currentColor" font-size="12">12</text>
  <rect x="230" y="274" width="360" height="14" class="f-ok" stroke="currentColor"/>
  <text x="596" y="286" fill="currentColor" font-size="12">12 (11)</text>
  <text x="10" y="322" fill="currentColor" font-size="12">gcc -O0 는 끝까지 11 — BUG01(미초기화)은 GCC 에서 최적화를 켜야 보인다 (5. 풀이 Part B).</text>
</svg>
```

### Part A — 12개 버그 × 플래그 표

- `starter.flags`(= 지금 팀 CFLAGS 의 경고 부분: `-Wall`)로 `bash check.sh starter` 를 돌려 어떤 BUG 가 빠져나가는지 본다.
- `demo_matrix.sh` 로 플래그를 **하나씩 단독으로** 붙여 본 표(clang, gcc)와, **누적으로** 더해 가는 표를 실제로 만든다. 표에서 "이 버그는 어떤 플래그가, 어떤 경고 이름으로 잡나" 를 읽는다.
- 12개를 모두 잡는 **최소 집합**을 찾는다.

### Part B — 함정: 컴파일러·최적화·타깃마다 다르다

- 같은 버그를 clang 과 GCC 가 **다른 줄**에서 보고하는 경우(BUG01, BUG09)를 찾는다.
- clang 은 기본으로 켜고 GCC 는 `-Wall` 이 필요한 경고, GCC 는 `-Wextra` 에 넣고 clang 은 안 넣은 경고를 구분한다.
- `demo_uninit.c` 로 미초기화 경고가 GCC 에선 `-O0`/`-O2` 에 따라 달라지고 clang 에선 같다는 것, 그리고 **어떤 경고도 못 잡는** 경우를 확인한다.
- `demo_noise.c` 로 `-Wconversion` 이 Mac(LP64)과 Cortex-M(ILP32)에서 다른 줄을 잡는 것을 본다.

### Part C — 펌웨어 프로젝트 기준선 확정

- `-Wconversion` 을 켜는 순간 `good.c` 가 깨진다 — 원인은 우리 코드가 아니라 `third_party/vendor_hal.h`(고칠 수 없는 벤더 헤더). 플래그 파일만으로 해결한다.
- `solution.flags` 를 최종 기준선으로 확정하고, clang / gcc / arm-none-eabi-gcc 에서 모두 통하는지 확인한다.
- `-Werror` 정책: 개발자 빌드 vs CI 빌드(`demo_ci.mk`), GCC 전용 플래그(`-Wlogical-op` 등) 분기.

## 2. Problem (English)

> **Pick the warning baseline for a firmware project.**
>
> `bugs12.c` (a UART/sensor module, do not edit it) contains 12 planted bugs, each on a line tagged `// BUGnn`: an uninitialized read, a signed/unsigned comparison, a shadowed parameter, a printf format mismatch, an implicit narrowing to `uint8_t`, an implicit sign change, a missing `return`, an ignored `warn_unused_result` value, an unannotated switch fall-through, `if (x = 0)`, `sizeof` on an array parameter, and an enum switch with a `default:` that misses one enumerator.
>
> Edit only the flags file (one flag per line). Requirements:
>
> 1. Compiling `bugs12.c` must produce a diagnostic on **every** tagged line, and **no** diagnostic on any untagged line.
> 2. `good.c` (the same module written correctly, which includes the vendor header `third_party/vendor_hal.h`) must build with your flags **plus `-Werror`** with zero diagnostics and print `good.c: 14/14 self-checks OK`.
> 3. Do not edit `bugs12.c`, `good.c` or `third_party/`. No `-w`, `-Weverything`, or `-Wno-error`. Every flag must also be accepted by GCC / `arm-none-eabi-gcc`.
>
> Example (starter, `-Wall` only):
> ```
> FAIL bugs12.c BUG02 line(s) 41: no diagnostic with these flags
> ...
> 11/17 passed
> ```
> Follow-ups: Why isn't `-Wall` "all"? Why does GCC only report the uninitialized read at `-O2`? Should `-Werror` be on for every developer build? How do you keep vendor/CMSIS headers from breaking `-Werror`? What would you add for a Cortex-M4 with a single-precision FPU?

## 3. 시작 코드

검사 대상 파일과 시작 플래그:

<!-- code: starter.flags -->

<!-- code: bugs12.c -->

정상 버전 `good.c` 와 벤더 헤더(둘 다 고치지 않는다):

<!-- code: good.c -->

<!-- code: third_party/vendor_hal.h -->

검사기 — `<variant>.flags` 를 읽어 Mac clang 으로 컴파일하고, 진단(diagnostic) 줄 번호를 `// BUGnn` 태그 줄과 맞춰 본다:

<!-- code: check.sh -->

```bash
bash check.sh starter      # 일부 FAIL, exit 1
bash check.sh solution     # 전부 PASS, exit 0
bash check.sh practice     # 9. 추가 연습, exit 0
bash demo_matrix.sh > matrix.txt   # 버그 x 플래그 표 (VM gcc 포함, 약 20초)
bash demo_extra.sh  > extra.txt    # 5. 풀이의 곁가지 실험 전부
```

`bash check.sh starter; echo exit=$?` 실제 출력:

```text
flags (starter.flags): -Wall -I third_party 
PASS rules: no -w / -Weverything / -Wno-error
PASS rules: third_party/vendor_hal.h untouched
PASS bugs12.c BUG01 line 29 [-Wsometimes-uninitialized]
FAIL bugs12.c BUG02 line(s) 41: no diagnostic with these flags
FAIL bugs12.c BUG03 line(s) 50: no diagnostic with these flags
PASS bugs12.c BUG04 line 58 [-Wformat]
FAIL bugs12.c BUG05 line(s) 63: no diagnostic with these flags
FAIL bugs12.c BUG06 line(s) 70: no diagnostic with these flags
PASS bugs12.c BUG07 line 80 [-Wreturn-type]
PASS bugs12.c BUG08 line 89 [-Wunused-result]
FAIL bugs12.c BUG09 line(s) 97 98: no diagnostic with these flags
PASS bugs12.c BUG10 line 110 [-Wparentheses]
PASS bugs12.c BUG11 line 117 [-Wsizeof-array-argument]
FAIL bugs12.c BUG12 line(s) 122: no diagnostic with these flags
PASS bugs12.c no warnings on untagged lines
PASS good.c builds with -Werror and 0 diagnostics
PASS good.c runs: 14/14 self-checks OK
11/17 passed
exit=1
```

검사기 설계:

- **줄 번호 매칭**: `bugs12.c:LINE:COL: warning:` (또는 `error:`) 줄만 센다. `note:` 는 세지 않는다. 한 버그에 태그가 두 줄이면(BUG01, BUG09) 둘 중 하나에 진단이 있으면 통과 — clang 과 GCC 가 가리키는 줄이 달라서다(Part B).
- **잡음 검사**: 태그 없는 줄의 경고가 하나라도 있으면 FAIL. "경고가 많을수록 좋다" 가 아니라 "버그에만 경고" 가 목표다.
- **good.c**: 플래그 + `-Werror` 로 빌드 → stderr 가 비어야 하고, 실행 결과가 정확해야 한다(빈 파일로 꼼수 불가). 벤더 헤더는 sha256 으로 "안 고쳤음" 을 확인한다.
- 모든 산출물은 `/tmp/TOOL-B02_work/<variant>/` 에만 쓴다. Mac 에서 1초 남짓.

## 4. 힌트

<details>
<summary>힌트 1 — 경고 이름은 진단 줄 끝 대괄호 안에 있다</summary>

`[-Wsometimes-uninitialized]` 처럼 clang/GCC 는 그 경고를 켠 옵션 이름을 줄 끝에 찍는다. 아직 안 잡힌 버그는 "이런 종류의 경고 이름이 뭘까" 를 생각해 보고, 그 이름을 단독으로 붙여서(`cc -std=c11 -O2 -Wxxx -c bugs12.c`) 해당 줄이 뜨는지 본다. 남은 6개는 각각 "부호 비교", "가림", "변환(정밀도 손실)", "부호 변환", "case 넘어감", "enum 의 switch" 다.

</details>

<details>
<summary>힌트 2 — -Wall 다음은 -Wextra, 그다음은 "이름이 버그 종류인" 플래그</summary>

`-Wextra` 가 BUG02 하나를 더 잡는다. 나머지 넷은 어떤 그룹에도 들어 있지 않은(clang 기준) 개별 플래그다: `-Wshadow`, `-Wconversion`, `-Wimplicit-fallthrough`, `-Wswitch-enum`. `-Wswitch`(이미 `-Wall` 에 있음)는 `default:` 가 있는 switch 에는 아무 말도 안 한다.

</details>

<details>
<summary>힌트 3 — good.c 가 갑자기 깨지면 경고가 어느 파일에서 났는지 보라</summary>

`-Wconversion` 을 넣으면 `third_party/vendor_hal.h:12:34: error: ...` 가 뜬다. 우리 파일이 아니다. 헤더를 고치거나 `-Wconversion` 을 빼는 것은 금지. 같은 디렉터리를 **시스템 헤더 디렉터리**로 알려 주는 옵션이 있다(`-I` 의 형제).

</details>

<details>
<summary>힌트 4 — 기준선은 "12개를 잡는 최소 집합" + 몇 개의 보강</summary>

최소 6개(`-Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum`)면 12/12. 여기에 bugs12.c 에는 없지만 실무에서 값싼 것(`-Wformat=2`, `-Wstrict-prototypes`, `-Wmissing-prototypes`, `-Wundef`)을 더하고, good.c 가 여전히 조용한지 확인한다. GCC 전용 플래그를 넣으면 clang 이 `unknown warning option` 을 낸다 — 이 파일은 두 컴파일러 공용이다.

</details>

## 5. 풀이

### Part A — 플래그를 하나씩: 실측 표

`demo_matrix.sh` 가 `bugs12.c` 를 `-O2 -c` 로, 플래그 **하나만** 붙여 컴파일하고 진단 줄을 태그와 맞춘 결과다(`matrix.txt` 원본). ● = 이 플래그가 잡음(플래그 없이도 잡히는 건 아님), ○ = 잡히지만 플래그 없이도 이미 잡힘, · = 못 잡음.

**Mac clang (Apple clang 21.0.0)**

| clang flag | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 | new |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `(no -W flag)` | · | · | · | ● | · | · | ● | ● | · | ● | ● | · | 5 |
| `-Wall` | ● | · | · | ○ | · | · | ○ | ○ | · | ○ | ○ | · | 1 |
| `-Wextra` | · | ● | · | ○ | · | · | ○ | ○ | · | ○ | ○ | · | 1 |
| `-Wshadow` | · | · | ● | ○ | · | · | ○ | ○ | · | ○ | ○ | · | 1 |
| `-Wconversion` | · | · | · | ○ | ● | ● | ○ | ○ | · | ○ | ○ | · | 2 |
| `-Wsign-conversion` | · | · | · | ○ | · | ● | ○ | ○ | · | ○ | ○ | · | 1 |
| `-Wimplicit-fallthrough` | · | · | · | ○ | · | · | ○ | ○ | ● | ○ | ○ | · | 1 |
| `-Wswitch` | · | · | · | ○ | · | · | ○ | ○ | · | ○ | ○ | · | 0 |
| `-Wswitch-enum` | · | · | · | ○ | · | · | ○ | ○ | · | ○ | ○ | ● | 1 |

`-Wformat=2 -Wcast-align -Wpedantic -Wstrict-prototypes -Wmissing-prototypes -Wnull-dereference -Wvla -Wdouble-promotion -Wundef -Wconditional-uninitialized` 는 이 파일에선 전부 new = 0 이었다(각자 다른 종류의 버그를 잡는다 — Part C, 9. 추가 연습).

**Lima VM gcc 15.2**

| gcc flag | 01 | 02 | 03 | 04 | 05 | 06 | 07 | 08 | 09 | 10 | 11 | 12 | new |
|---|---|---|---|---|---|---|---|---|---|---|---|---|---|
| `(no -W flag)` | · | · | · | ● | · | · | · | ● | · | · | ● | · | 3 |
| `-Wall` | ● | · | · | ○ | · | · | ● | ○ | · | ● | ○ | · | 3 |
| `-Wextra` | ● | ● | · | ○ | · | · | · | ○ | ● | · | ○ | · | 3 |
| `-Wshadow` | · | · | ● | ○ | · | · | · | ○ | · | · | ○ | · | 1 |
| `-Wconversion` | · | · | · | ○ | ● | ● | · | ○ | · | · | ○ | · | 2 |
| `-Wsign-conversion` | · | · | · | ○ | · | ● | · | ○ | · | · | ○ | · | 1 |
| `-Wimplicit-fallthrough` | · | · | · | ○ | · | · | · | ○ | ● | · | ○ | · | 1 |
| `-Wswitch-enum` | · | · | · | ○ | · | · | · | ○ | · | · | ○ | ● | 1 |

(`-Wformat=2 -Wlogical-op -Wduplicated-cond -Wduplicated-branches -Wjump-misses-init` 도 이 파일에선 0 — Part C 의 `demo_gcc_only.c` 에서 쓰인다.)

**누적 — 한 단계씩 더하기** (bugs12.c 에서 잡힌 수 / good.c 의 진단 수):

| step | flags added | clang -O2 | gcc -O2 | gcc -O0 | good.c clang `-I` | good.c clang `-isystem` | good.c gcc `-isystem -Werror` |
|---|---|---|---|---|---|---|---|
| 1 | `-Wall` | 6/12 | 6/12 | 5/12 | 0 | 0 | 0 |
| 2 | `-Wextra` | 7/12 | 8/12 | 7/12 | 0 | 0 | 0 |
| 3 | `-Wshadow` | 8/12 | 9/12 | 8/12 | 0 | 0 | 0 |
| 4 | `-Wconversion` | 10/12 | 11/12 | 10/12 | **2** | 0 | 0 |
| 5 | `-Wimplicit-fallthrough` | 11/12 | 11/12 | 10/12 | 2 | 0 | 0 |
| 6 | `-Wswitch-enum` | **12/12** | **12/12** | 11/12 | 2 | 0 | 0 |
| 7 | `-Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Wundef` | 12/12 | 12/12 | 11/12 | 2 | 0 | 0 |

읽는 법:

- **플래그 없이도 clang 은 5개, GCC 는 3개**를 잡는다. 형식 불일치(`-Wformat`), `warn_unused_result`, 배열 매개변수 `sizeof` 는 둘 다 기본으로 켜져 있다. `-Wreturn-type` 과 `-Wparentheses` 는 clang 만 기본이고 GCC 는 `-Wall` 이 필요하다.
- `-Wall` 은 이름과 달리 "전부" 가 아니다. 이 파일에서 clang `-Wall` 이 새로 잡은 건 BUG01 하나다. `-Wall -Wextra` 까지 해도 7/12(clang).
- **12개를 잡는 최소 집합**은 두 컴파일러 모두 `-Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum`. GCC 에선 `-Wextra` 가 fall-through 까지 포함하지만 clang 에선 아니므로 `-Wimplicit-fallthrough` 를 명시해야 공용 목록이 된다.
- 4단계에서 `good.c` 가 `-I` 일 때 진단 2개 — 벤더 헤더다(Part C).

**버그별 경고 이름** (기준선 전체, `matrix.txt` 마지막 표):

| bug | clang | gcc | good.c 의 고친 방법 |
|---|---|---|---|
| BUG01 | 29 `-Wsometimes-uninitialized` | 34 `-Wmaybe-uninitialized` | `int sum = 0;` + `n > 0 ? sum / n : 0` |
| BUG02 | 41 `-Wsign-compare` | 41 `-Wsign-compare` | `limit` 를 `int` 로 — 비교 양쪽을 같은 부호 타입으로 |
| BUG03 | 50 `-Wshadow` | 50 `-Wshadow` | 안쪽 변수를 없애고 `payload = buf[1]` |
| BUG04 | 58 `-Wformat` | 58 `-Wformat=` | `printf("%llu", (unsigned long long)x)` |
| BUG05 | 63 `-Wimplicit-int-conversion` | 63 `-Wconversion` | 0..1000 으로 범위 제한(clamp) **후** `(uint8_t)` 캐스트 |
| BUG06 | 70 `-Wsign-conversion` | 70 `-Wsign-conversion` | 반환 타입을 `int32_t` 로 — 음수가 정상값이므로 |
| BUG07 | 80 `-Wreturn-type` | 80 `-Wreturn-type` | `return c == 0;` 한 줄 |
| BUG08 | 89 `-Wunused-result` | 89 `-Wunused-result` | `return flash_erase(0);` — 호출자에게 전달 |
| BUG09 | 98 `-Wimplicit-fallthrough` | 97 `-Wimplicit-fallthrough=` | 의도한 곳만 `FALLTHROUGH;` (= `__attribute__((fallthrough))`) |
| BUG10 | 110 `-Wparentheses` | 110 `-Wparentheses` | `==` |
| BUG11 | 117 `-Wsizeof-array-argument` | 117 `-Wsizeof-array-argument` | `16 * sizeof(uint32_t)` (또는 길이를 인자로) |
| BUG12 | 122 `-Wswitch-enum` | 122 `-Wswitch-enum` | `default:` 를 빼고 모든 enumerator 를 나열, 범위 밖 값은 switch 뒤에서 처리 |

경고를 끄려고 캐스트만 붙이는 것은 고친 게 아니다. BUG05 를 `(uint8_t)(percent_x10 * 255 / 1000)` 로만 바꾸면 경고는 사라지지만 2000 → 254 는 그대로다. good.c 는 **먼저 범위를 확정**하고 캐스트한다 — 캐스트는 "이 변환은 안전하다는 것을 내가 확인했다" 는 서명이다.

### Part B — 함정 1: 같은 버그, 다른 줄

| 버그 | clang 이 가리키는 줄 | gcc 가 가리키는 줄 | 이유 |
|---|---|---|---|
| BUG01 | 29 `if (n > 0) {` — 원인이 된 **조건** | 34 `return sum / ...` — 미초기화 값을 **쓰는** 곳 | clang 은 "조건이 거짓인 경로" 를, gcc 는 사용 지점을 보고 |
| BUG09 | 98 `case 115200:` — 도착하는 **레이블** | 97 `div = 1666;` — 넘어가는 **문장** | 같은 사건을 양 끝에서 본다 |

그래서 bugs12.c 는 두 버그에 태그를 두 줄씩 달았고, check.sh 는 "태그 줄 중 하나" 로 판정한다. 경고 줄 번호로 자동 판정하는 도구(CI 경고 대시보드, 경고 수 상한 검사)는 컴파일러마다 기준 줄이 다를 수 있음을 감안해야 한다. clang 의 실제 메시지는 조건 줄을 가리키고, 쓰는 줄은 `note:` 로 붙는다(`demo_extra.sh` 의 같은 모양 함수 `u2_branch`):

```text
demo_uninit.c:28:9: warning: variable 'v' is used uninitialized whenever 'if' condition is false [-Wsometimes-uninitialized]
demo_uninit.c:30:12: note: uninitialized use occurs here
demo_uninit.c:28:5: note: remove the 'if' if its condition is always true
demo_uninit.c:27:10: note: initialize the variable 'v' to silence this warning
```

### Part B — 함정 2: 기본값과 그룹 구성이 컴파일러마다 다르다

위 두 표에서 바로 읽히는 차이(전부 실측):

| 경고 | clang 21 | gcc 15.2 |
|---|---|---|
| `-Wreturn-type`, `-Wparentheses` | 기본 켜짐 | `-Wall` 필요 |
| `-Wsign-compare` (C) | `-Wextra` | `-Wextra` |
| `-Wimplicit-fallthrough` | 어떤 그룹에도 없음 → 명시 | `-Wextra` 에 포함 |
| 미초기화 | `-Wall` (`-Wuninitialized`, `-Wsometimes-uninitialized`) | `-Wall` 또는 `-Wextra` (`-Wuninitialized`, `-Wmaybe-uninitialized`) — 최적화 의존 |
| `-Wconversion` | `-Wimplicit-int-conversion`, `-Wsign-conversion`, `-Wshorten-64-to-32`, `-Wimplicit-float-conversion` 등을 묶은 그룹 | C 에선 `-Wsign-conversion` 도 켠다(BUG06 ●). C++ 에선 아니라고 GCC 문서에 있음 — 확인 권장 |
| fall-through 주석 `/* fall through */` | 무시 (경고 남음) | `-Wimplicit-fallthrough`(=3) 에서 인정 → 경고 없음. `=5` 면 다시 경고 |

마지막 줄 실측 (`demo_fallthrough.c`):

```text
mac clang -Wimplicit-fallthrough -Wpedantic:
demo_fallthrough.c:18:16: warning: binary integer literals are a C23 extension [-Wc23-extensions]
demo_fallthrough.c:12:5: warning: unannotated fall-through between switch labels [-Wimplicit-fallthrough]
demo_fallthrough.c:12:5: note: insert '__attribute__((fallthrough));' to silence this warning
vm gcc -Wimplicit-fallthrough -Wpedantic:
demo_fallthrough.c:18:16: warning: binary constants are a C23 feature or GCC extension [-Wpedantic]
vm gcc -Wimplicit-fallthrough=5:
demo_fallthrough.c:10:11: warning: this statement may fall through [-Wimplicit-fallthrough=]
```

그래서 공용 코드에서는 주석 대신 `good.c` 의 `FALLTHROUGH` 매크로(`__attribute__((fallthrough))`, C23 이면 `[[fallthrough]]`)를 쓴다. 덤으로: `0b101` 같은 이진 리터럴은 C11 에선 확장이라 `-Wpedantic` 이 잡는다 — 레지스터 비트를 0b 로 쓰는 펌웨어가 많아서 `-Wpedantic` 을 기준선에서 뺀 이유 중 하나다.

### Part B — 함정 3: GCC 의 미초기화 경고는 최적화에 달려 있다

`demo_uninit.c` 의 6가지 미초기화. 각 칸 = 경고가 나왔나 (`demo_extra.sh` 실측, 모두 `-Wall`):

| 경우 | 코드 모양 | clang -O0 | clang -O2 | gcc -O0 | gcc -O1/-O2/-Os |
|---|---|---|---|---|---|
| U1 | `int x; return x + 1;` | ✔ `-Wuninitialized` | ✔ | ✔ `-Wuninitialized` | ✔ |
| U2 | `if (n > 0) v = ...; return v;` | ✔ `-Wsometimes-uninitialized` | ✔ | ✘ | ✔ `-Wmaybe-uninitialized` |
| U3 | `sensor_read(&v); return v;` (다른 파일 함수) | ✘ | ✘ | ✘ | ✘ |
| U4 | `int a[4];` 중 `a[3]` 미기록 | ✘ | ✘ | ✘ | ✔ `‘a’ is used uninitialized` |
| U5 | `for (...) last = i; return last;` | ✘ | ✘ | ✘ | ✔ `-Wmaybe-uninitialized` |
| U6 | 구조체 멤버 `c.parity` 미기록 | ✘ | ✘ | ✔ | ✔ (같은 경고가 두 번 찍힘) |

- **clang** 은 프런트엔드(소스 분석 단계)에서 지역 스칼라 변수만 흐름 분석한다 → `-O` 와 무관, 배열·구조체·루프는 못 본다. U5 는 `-Wconditional-uninitialized`(clang 전용, `-Wall` 밖)를 켜면 `variable 'last' may be uninitialized when used here` 가 나온다.
- **GCC** 는 최적화 패스(SSA)가 만든 정보로 판단한다 → `-O0` 에서는 U2/U4/U5 가 안 보인다. 이것이 `bugs12.c` 의 BUG01 이 gcc -O0 에서 11/12 로 남는 이유다.
- **U3 은 누구도 못 잡는다** — 컴파일러는 다른 번역 단위(TU)의 함수가 `*out` 을 쓰는지 모른다. 런타임 도구(MSan/valgrind — TOOL-M10)나 정적 분석기(TOOL-M12)의 몫이고, 펌웨어 관행은 "출력 인자는 호출 전에 초기화" + 반환값 확인이다. `-ftrivial-auto-var-init=pattern`(clang — 이 Mac 에서 컴파일 rc=0 실측)은 증상을 결정적으로 만들 뿐 경고를 주지 않는다.

**실전 결과**: 같은 코드가 디버그 빌드에선 `-Werror` 통과, 릴리스 빌드에선 실패한다 (`-DONLY_MAYBE` = U1/U6 제외):

```text
=== vm: demo_uninit.c -DONLY_MAYBE gcc -Wall -Werror -O0 (debug build) ===
rc=0

=== vm: demo_uninit.c -DONLY_MAYBE gcc -Wall -Werror -O2 (release build) ===
demo_uninit.c:30:12: error: ‘v’ may be used uninitialized [-Werror=maybe-uninitialized]
demo_uninit.c:45:13: error: ‘a’ is used uninitialized [-Werror=uninitialized]
demo_uninit.c:53:12: error: ‘last’ may be used uninitialized [-Werror=maybe-uninitialized]
rc=1
```

(note 줄은 생략.) 개발자가 `-O0` 로만 빌드하면 CI 의 `-O2` 빌드에서 처음 터진다 — CI 는 **배포하는 최적화 레벨**로 경고를 본다.

### Part B — 함정 4: -Wconversion 의 결과는 타깃에 따라 다르다

`demo_noise.c` 는 **문법적으로 멀쩡한** 변환 7개다. 같은 파일, 같은 `-Wconversion`:

| 줄 | 코드 | Mac clang (arm64, LP64) | clang `--target=thumbv7em` (ILP32) | gcc aarch64 | arm-none-eabi-gcc |
|---|---|---|---|---|---|
| N1 19 | `uint8_t + uint8_t` → `uint8_t` | — | — | — | — |
| N2 24 | `reg \|= 1u << bit` (`uint16_t`) | — | — | — | — |
| N3 30 | `x <<= 1` (`uint8_t`) | — | — | — | — |
| N4 37 | `size_t` → `int` | `-Wshorten-64-to-32` | `-Wsign-conversion` | `-Wconversion` | `-Wsign-conversion` |
| N5 42 | `float * 0.5` → `float` | `-Wimplicit-float-conversion` | 같음 | `-Wfloat-conversion` | `-Wfloat-conversion` |
| N6 47 | `int` → `uint32_t` | `-Wsign-conversion` | 같음 | 같음 | 같음 |
| N7 52 | `uint32_t` → `long` | — | `-Wsign-conversion` | — | `-Wsign-conversion` |

- N4/N7: `size_t`, `long` 의 폭이 LP64(64비트 long)와 ILP32(32비트)에서 다르다. N7 은 호스트에선 값이 다 들어가므로 조용하고, Cortex-M 에선 `long` 도 32비트라 부호가 문제다. **호스트 단위 테스트 빌드에서 깨끗해도 타깃 빌드에서 새 경고가 나온다** → CI 에 타깃 빌드가 반드시 있어야 하는 이유.
- N1–N3: 두 컴파일러 모두 "좁은 타입끼리의 연산 결과를 같은 좁은 타입에 다시 넣는" 패턴은 일부러 경고하지 않았다(실측). 예전 컴파일러에선 이런 줄까지 경고해서 "-Wconversion 은 잡음투성이" 라는 평판이 생겼다고 알려져 있다 — 버전별 확인 권장. 반대로 말하면 **N1 의 200 + 100 → 44 같은 wrap 은 -Wconversion 도 못 잡는다**.
- N5: Cortex-M4 의 FPU 는 단정밀도만 있다. `0.5` 는 `double` 이라 곱셈이 소프트웨어 double 루틴으로 간다. 이건 `-Wdouble-promotion` 이 잡는다(arm-none-eabi-gcc: `implicit conversion from 'float' to 'double' to match other operand of binary expression`) — 9. 추가 연습의 펌웨어 세트에 넣는다.

### Part B — 함정 5: printf 형식은 타깃마다 맞고 틀린다

```c
printf("ticks=%u\n", ticks);   /* ticks 는 uint32_t */
```

Mac clang `-Wall`: 경고 0 (`uint32_t` = `unsigned int`). VM `arm-none-eabi-gcc -Wall`:

```text
demo_u32fmt.c:9:20: warning: format '%u' expects argument of type 'unsigned int', but argument 2 has type 'uint32_t' {aka 'long unsigned int'} [-Wformat=]
```

newlib + arm-none-eabi-gcc 에서는 `uint32_t` 가 `long unsigned int` 다(같은 Cortex-M 이라도 clang `--target=thumbv7em-none-eabi` 는 `unsigned int` 로 찍었다 — `demo_noise.c` 출력). 크기는 같아 실제로는 대개 맞게 찍히지만 형식상 틀렸다. 이식 가능한 답은 `PRIu32` 또는 `(unsigned long)` 캐스트 + `%lu`. 참고로 이 VM 의 Ubuntu 패키지 newlib 에서는 `-std=c11` 로 `<inttypes.h>` 를 넣어도 **`PRIu64` 가 정의되지 않았다**(`PRIu32` 는 됨, 실측 — 원인 미확인). 그래서 good.c 는 64비트 값을 `%llu` + `(unsigned long long)` 캐스트로 찍는다.

`-Wformat=2` 가 `-Wall` 의 `-Wformat` 위에 더하는 것 (`demo_format2.c`):

```text
mac clang -Wall:            demo_format2.c:10:12: warning: format string is not a string literal (potentially insecure) [-Wformat-security]
mac clang -Wall -Wformat=2: + demo_format2.c:17:13: warning: format string is not a string literal [-Wformat-nonliteral]
vm gcc -Wall -Wformat=2:    demo_format2.c:10:5: warning: format not a string literal and no format arguments [-Wformat-security]
vm gcc ... -Wsuggest-attribute=format:
                            demo_format2.c:17:5: warning: function ‘log_printf’ might be a candidate for ‘gnu_printf’ format attribute [-Wsuggest-attribute=format]
```

10행은 `printf(msg)` — UART 로 받은 문자열에 `%n` 이 섞이면 메모리 쓰기 공격이 된다. 17행은 `vprintf(fmt, ap)` 를 감싼 로그 함수인데 `format(printf, 1, 2)` 속성이 없어 호출자의 형식 검사가 사라진다. clang 은 이것을 nonliteral 로 경고하고, GCC 는 `va_list` 를 받는 경우는 nonliteral 에서 빼고 대신 `-Wsuggest-attribute=format` 이 알려 준다(실측). 속성을 붙이는 로그 매크로 설계는 TOOL-B13.

### Part C — 벤더 헤더: `-I` 대신 `-isystem`

4단계에서 `-Wconversion` 을 넣으면 good.c 가 이렇게 깨진다 (Mac, `-I third_party -Werror`):

```text
third_party/vendor_hal.h:12:34: error: implicit conversion changes signedness: 'int' to 'uint32_t' (aka 'unsigned int') [-Werror,-Wsign-conversion]
third_party/vendor_hal.h:17:17: error: implicit conversion loses integer precision: 'int' to 'uint8_t' (aka 'unsigned char') [-Werror,-Wimplicit-int-conversion]
rc=1
```

check.sh 로 보면 `solution.flags` 에 `-I third_party` 를 남긴 채 돌렸을 때 실제로 `15/17 passed`, `FAIL good.c builds with -Werror and 0 diagnostics: 2 diagnostic(s)` 였다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 헤더, 다른 include 방식 → 경고 대상이냐 아니냐 (good.c, 기준선 + -Werror)</text>
  <rect x="10" y="90" width="150" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="114" fill="currentColor" font-family="monospace" font-size="12">good.c</text>
  <text x="22" y="134" fill="currentColor" font-family="monospace" font-size="12">#include "vendor_hal.h"</text>
  <rect x="230" y="40" width="230" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="240" y="62" fill="currentColor" font-family="monospace" font-size="12">-I third_party</text>
  <text x="240" y="82" fill="currentColor" font-size="12">사용자 헤더 = 우리 코드 취급</text>
  <rect x="230" y="140" width="230" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="240" y="162" fill="currentColor" font-family="monospace" font-size="12">-isystem third_party</text>
  <text x="240" y="182" fill="currentColor" font-size="12">시스템 헤더 = 그 안의 경고 억제</text>
  <line x1="160" y1="110" x2="230" y2="72" stroke="currentColor" stroke-width="2"/>
  <line x1="160" y1="130" x2="230" y2="170" stroke="currentColor" stroke-width="2"/>
  <rect x="520" y="40" width="210" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="530" y="62" fill="currentColor" font-size="12">vendor_hal.h:12, :17 에서</text>
  <text x="530" y="82" fill="currentColor" font-size="12">진단 2개 → -Werror 실패</text>
  <rect x="520" y="140" width="210" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="530" y="162" fill="currentColor" font-size="12">진단 0개 → 빌드·실행 OK</text>
  <text x="530" y="182" fill="currentColor" font-size="12">clang · gcc · arm gcc 모두</text>
  <line x1="460" y1="70" x2="520" y2="70" stroke="currentColor" stroke-width="2"/>
  <line x1="460" y1="170" x2="520" y2="170" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="230" fill="currentColor" font-size="12">우리 코드의 버그는 그대로 다 잡힌다 — 억제 범위는 "그 디렉터리 안의 파일에서 난 진단" 뿐 (bugs12.c 는 12/12 유지).</text>
</svg>
```

- 벤더 헤더를 고치면 다음 HAL 업데이트 때 덮어써지고, `-Wconversion` 을 빼면 우리 코드의 BUG05/06 을 놓친다. `-isystem` 은 "이 디렉터리는 내 책임이 아니다" 를 정확히 표현한다. 실무 대상: CMSIS(`Drivers/CMSIS/Include`), 벤더 HAL, FreeRTOS 커널 헤더, 생성된 레지스터 헤더.
- 주의(확인 권장): 시스템 헤더의 **매크로가 우리 파일에서 펼쳐질 때**의 경고 처리는 컴파일러·경고 종류마다 달라 일부는 여전히 보일 수 있다. 그리고 `-isystem` 은 include 탐색 순서도 바꾼다(`-I` 디렉터리들 뒤, 표준 시스템 디렉터리 앞 — GCC 문서 기준).
- 벤더 **소스(.c)** 는 헤더처럼 숨길 수 없다 → 그 파일들만 경고 레벨을 낮춘 별도 규칙으로 빌드하거나, 9. 추가 연습 2처럼 범위를 좁힌 `#pragma` 를 쓴다.

### Part C — 최종 기준선

<!-- code: solution.flags -->

| 묶음 | 플래그 | 왜 |
|---|---|---|
| 12개를 잡는 최소 집합 | `-Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum` | 위 표. 두 컴파일러에서 12/12 |
| 명시 | `-Wsign-conversion` | C 에선 `-Wconversion` 에 포함되지만 같은 목록을 C++ 파일과 공유하면 필요(GCC) |
| 싼 보강 | `-Wformat=2` | 로그 래퍼의 형식 검사 구멍(위) |
|  | `-Wstrict-prototypes` | C11 에서 `void isr_init()` 은 인자 검사가 없는 비-프로토타입 선언 |
|  | `-Wmissing-prototypes` | 헤더 선언 없는 전역 함수 = `static` 을 잊었거나 선언·정의 불일치 |
|  | `-Wundef` | `#if HW_REV >= 2` 에서 `HW_REV` 오타/미정의가 조용히 0 (TOOL-B07) |
| 우리 책임 아님 | `-isystem third_party` | 벤더 헤더 |
| 일부러 뺌 | `-Wpedantic` | `0b` 리터럴, `##__VA_ARGS__` 등 펌웨어가 흔히 쓰는 GNU 확장까지 경고 |
|  | `-Weverything` | clang 전용, 서로 모순되는 경고까지 켬 — 새 경고 탐색용으로만 |
|  | `-Wcast-align`, `-Wdouble-promotion`, `-Wvla`, … | 타깃 의존 → 펌웨어 세트(9. 추가 연습 1) |

이 목록이 세 컴파일러 모두에서 통하는지 (`demo_extra.sh` 실측): Mac clang `-isystem third_party -Werror` → 빌드 rc=0, 실행 `good.c: 14/14 self-checks OK`; VM gcc 같은 명령 → 같은 출력, rc=0; VM `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb ... -Werror -c good.c` → rc=0.

`bash check.sh solution; echo exit=$?` 실제 출력 (BUG02–BUG11 줄은 Part A "버그별 경고 이름" 표와 같아 생략):

```text
flags (solution.flags): -Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum -Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Wundef -isystem third_party 
PASS rules: no -w / -Weverything / -Wno-error
PASS rules: third_party/vendor_hal.h untouched
PASS bugs12.c BUG01 line 29 [-Wsometimes-uninitialized]
...
PASS bugs12.c BUG12 line 122 [-Wswitch-enum]
PASS bugs12.c no warnings on untagged lines
PASS good.c builds with -Werror and 0 diagnostics
PASS good.c runs: 14/14 self-checks OK
17/17 passed
exit=0
```

### Part C — GCC 전용 플래그와 "모르는 옵션" 처리

`demo_gcc_only.c` 의 버그 4개는 clang `-Wall -Wextra` 도, gcc `-Wall -Wextra` 도 조용했다. GCC 전용 4개를 켜면 (VM):

```text
demo_gcc_only.c:17:32: warning: logical ‘and’ of equal expressions [-Wlogical-op]
demo_gcc_only.c:26:19: warning: duplicated ‘if’ condition [-Wduplicated-cond]
demo_gcc_only.c:34:8: warning: this condition has identical branches [-Wduplicated-branches]
demo_gcc_only.c:44:9: warning: jump skips variable initialization [-Wjump-misses-init]
```

17행 `(sr & UART_SR_RXNE) && (sr & UART_SR_RXNE)` 는 `ORE` 를 쓰려던 복사-붙여넣기 버그 — 레지스터 비트 검사에서 정말 흔하다. 그런데 같은 플래그를 Mac clang 에 주면:

```text
warning: unknown warning option '-Wlogical-op'; did you mean '-Wlong-long'? [-Wunknown-warning-option]
warning: unknown warning option '-Wduplicated-cond' [-Wunknown-warning-option]
warning: unknown warning option '-Wduplicated-branches' [-Wunknown-warning-option]
demo_gcc_only.c:44:9: warning: jump from this goto statement to its label is incompatible with C++ [-Wjump-misses-init]
```

`-Werror` 가 붙으면 이 `unknown warning option` 들이 **error** 가 되어 빌드가 멈춘다(rc=1). 반대 방향도 있다: GCC 는 모르는 `-Wfoo-unknown` 을 `gcc: error: unrecognized command-line option` 으로 즉시 실패시키고(rc=1), 모르는 `-Wno-…` 는 조용히 넘어가다가 **다른 경고가 날 때만** `cc1: note: unrecognized command-line option ‘-Wno-unknown-warning-option’ may have been intended to silence earlier diagnostics` 를 덧붙인다(실측). 그래서 공용 목록에는 양쪽이 아는 것만 두고, 전용 플래그는 컴파일러를 보고 붙인다:

<!-- code: demo_ci.mk -->

실측 (`ci.txt` 발췌 — `...` 는 생략, 긴 명령줄 일부 축약): Mac(Make 3.81, clang `make` / `make WERROR=1 bugs`)과 VM(Make 4.4.1, `make CC=gcc WERROR=1` / `... bugs`):

```text
CC=cc IS_CLANG=1
CFLAGS=-std=c11 -O2 -Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum -Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Wundef -isystem third_party
...
good.c: 14/14 self-checks OK
12 errors generated.
make: *** [bugs] Error 1
---- VM ----
CC=gcc IS_CLANG=0
CFLAGS=... -isystem third_party -Wlogical-op -Wduplicated-cond -Wduplicated-branches
gcc ... -Wduplicated-branches -Werror good.c -o /tmp/TOOL-B02_work/civm/out/good
good.c: 14/14 self-checks OK
bugs12.c:97:13: error: this statement may fall through [-Werror=implicit-fallthrough=]
bugs12.c:34:16: error: ‘sum’ may be used uninitialized [-Werror=maybe-uninitialized]
cc1: all warnings being treated as errors
make: *** [demo_ci.mk:28: bugs] Error 1
```

### Part C — -Werror 정책

| 빌드 | 경고 | 이유 |
|---|---|---|
| 개발자 로컬 (`make`) | 전부 **보이게**, 빌드는 계속 | 컴파일러 버전·`-O` 레벨이 제각각 → 남의 머신에서만 나는 경고로 작업이 막히면 안 됨. 위 실측처럼 `-O0` 와 `-O2` 결과가 다르다 |
| CI (`make WERROR=1`) | **고정된(pinned) 툴체인 버전**으로, 배포하는 `-O` 레벨과 **타깃 빌드**(arm-none-eabi)까지 `-Werror` | 새 경고가 main 에 들어오는 것을 막는 유일한 문. 툴체인을 올릴 땐 경고를 먼저 정리하는 별도 작업으로 |
| 항상 error | `-Werror=return-type`, `-Werror=implicit-function-declaration` 같은 "버그 확정" 경고 | 버전이 바뀌어도 오탐이 거의 없다. 실측: `-Werror=return-type` 만 주면 bugs12.c 에서 BUG07 하나만 `error:`, 나머지는 `warning:` 으로 남는다 |
| 서드파티 | `-isystem` / 별도 규칙 | 우리가 못 고치는 코드가 우리 빌드를 막지 않게 |

(GCC 14 부터 `implicit-function-declaration`, `int-conversion` 등 일부는 C 에서 기본 error 가 되었다 — 버전 의존, 확인 권장.)

## 6. 정답 코드

검사기가 읽는 변경은 플래그 파일 하나다. 시작 플래그 대비 바뀐 줄이 강조된다:

<!-- code: solution.flags diff=starter.flags -->

핵심 두 가지: (1) 12개를 잡는 6개 + 싼 보강 5개, (2) `-I third_party` → `-isystem third_party`. `bash check.sh solution` → `17/17 passed`, exit 0 (위 Part C 출력).

## 7. 자주 하는 실수

1. **`-Wall` 이면 다 켜졌다고 믿기** → 실측 clang `-Wall` 6/12, `-Wall -Wextra` 7/12. 고친 것: 버그 종류별 플래그(`-Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum`)를 명시.
2. **벤더 헤더 경고 때문에 `-Wconversion` 을 끄거나 헤더를 고치기** → 우리 코드의 BUG05/06 을 놓치거나, HAL 업데이트 때 수정이 사라진다. 고친 것: `-isystem third_party`.
3. **캐스트로 경고만 지우기** → `(uint8_t)(percent_x10 * 255 / 1000)` 은 2000 → 254 버그를 그대로 둔 채 경고만 없앤다. 고친 것: 범위 확인(clamp) 후 캐스트, 또는 타입 자체를 바르게(BUG06 은 반환형을 `int32_t` 로).
4. **GCC 전용 플래그를 공용 목록에 넣기** → clang `-Werror` 빌드가 `unknown warning option` 으로 실패. clang 전용(`-Wconditional-uninitialized`, `-Weverything`)을 넣으면 GCC 가 `unrecognized command-line option` 으로 실패. 고친 것: `demo_ci.mk` 처럼 컴파일러별 분기.
5. **`#pragma GCC diagnostic ignored` 를 push/pop 없이 쓰기** → 그 줄부터 파일 끝까지 꺼진다. 실측: practice_pragma.c 에서 `pop` 을 지우면 뒤쪽 PROBE 줄의 `-Wsign-conversion` 경고가 **0개**(사라짐). 고친 것: `push` → `ignored` → 해당 함수 → `pop`.
6. **개발자 빌드에 무조건 `-Werror`** → 툴체인이 고정되지 않으면 "내 머신에선 되는데" 가 생기고, 사람들이 `-w` 나 `-Wno-error` 로 우회한다. 고친 것: `WERROR ?= 0`, CI 만 1.
7. **clang 에서 `/* fall through */` 주석으로 넘어가기를 표시** → clang 은 주석을 안 읽는다(실측 경고 남음). 고친 것: `__attribute__((fallthrough));` 매크로.
8. **호스트 빌드만 경고 0 이면 끝이라고 생각** → `demo_noise.c` N7 처럼 ILP32 타깃에서만 나는 경고, arm-none-eabi-gcc 에서만 나는 `%u`/`uint32_t` 경고가 있다. 고친 것: CI 에 타깃 툴체인 빌드 포함.

## 8. 면접 Q&A

<details>
<summary>Q1. -Wall 은 왜 "all" 이 아닌가요? -Wextra, -Weverything 과의 차이는?</summary>

`-Wall` 은 "오탐이 적고 거의 모든 코드에 켜도 되는" 경고 묶음이라는 역사적 이름이다. 오탐 가능성이 있는 것은 `-Wextra` 로, 코드 스타일·이식성에 따라 호불호가 갈리는 것(`-Wshadow`, `-Wconversion`, `-Wswitch-enum`)은 어디에도 넣지 않았다. 실측: clang `-Wall` 이 bugs12.c 에서 새로 잡은 것은 1개(BUG01), `-Wextra` 는 1개(BUG02). `-Weverything` 은 clang 전용으로 정말 전부(서로 모순되는 것 포함)를 켠다 — 새 경고를 찾아볼 때 한 번 돌려 보는 용도이지 기준선이 아니다(GCC 는 아예 없음).

꼬리질문 — "그럼 몇 개 더 켜야 하나요?" → "버그 종류별로 필요한 것을 실측해서 고른다. 우리 경우 6개로 12/12, 거기에 싼 보강 몇 개."

</details>

<details>
<summary>Q2. -Werror 는 어디서 켜야 하나요?</summary>

경고는 컴파일러 **버전**, **최적화 레벨**, **타깃**에 따라 바뀐다. 실측: 같은 `demo_uninit.c` 가 gcc `-O0 -Werror` 는 통과, `-O2 -Werror` 는 3개 error. `uint32_t` 를 `%u` 로 찍는 코드는 Mac 에선 조용하고 arm-none-eabi-gcc 에선 경고. 그래서 `-Werror` 는 **툴체인 버전이 고정된 CI** 에서, 배포하는 `-O` 레벨과 타깃 빌드로 켠다. 개발자 로컬은 경고를 보여 주되 막지 않는다. 확실한 버그만 `-Werror=return-type` 처럼 항상 error 로. 툴체인 업그레이드는 "새 경고 정리" 작업과 함께 한다.

</details>

<details>
<summary>Q3. -Wconversion 은 잡음이 많다는데 기존 코드베이스에 어떻게 도입하나요?</summary>

1) 먼저 경고 수를 센다(모듈별). 2) 새 파일/수정된 파일부터 `-Wconversion` 을 켜고, 오래된 디렉터리는 잠시 별도 규칙. 3) 고칠 때는 캐스트보다 **타입을 바르게**(음수가 정상이면 signed 로 반환), 캐스트가 필요하면 범위 확인 뒤에. 4) 호스트와 타깃 둘 다 빌드 — LP64 와 ILP32 에서 결과가 다르다(N4: 호스트 `-Wshorten-64-to-32`, Cortex-M `-Wsign-conversion`; N7 은 Cortex-M 에서만). 그리고 최근 컴파일러는 `uint8_t + uint8_t` 같은 고전적 잡음을 이미 내지 않는다(실측) — 반대로 그 wrap 은 이 경고로 못 잡는다는 것도 알아야 한다.

</details>

<details>
<summary>Q4. CMSIS 나 벤더 HAL 헤더가 경고를 내면?</summary>

헤더를 고치지 않는다(업데이트 때 덮어써짐). `-I` 대신 `-isystem` 으로 넣으면 그 디렉터리 파일 안에서 난 경고는 억제되고, 우리 코드의 경고는 그대로다. 실측: 같은 good.c 가 `-I` 로는 진단 2개(-Werror 실패), `-isystem` 으로는 0개 — clang, gcc, arm-none-eabi-gcc 모두. 벤더 .c 소스는 별도 규칙(경고 낮춤)이나 범위를 좁힌 `#pragma GCC diagnostic push/ignored/pop`.

</details>

<details>
<summary>Q5. 미초기화 변수는 컴파일러가 왜 다 못 잡나요? clang 과 GCC 는 어떻게 다른가요?</summary>

clang 은 프런트엔드에서 지역 스칼라의 흐름만 본다 → `-O` 와 무관하지만 배열·구조체·루프 경로(U4/U5/U6)를 놓친다. GCC 는 최적화 후 SSA 정보로 본다 → `-O1` 이상에서 더 많이 잡지만(U2, U4, U5) `-O0` 에선 놓친다. 다른 TU 의 함수가 출력 인자를 쓰는지(U3)는 둘 다 모른다. 보완: `-Wconditional-uninitialized`(clang), 정적 분석(TOOL-M12), 런타임 도구(MSan/valgrind, TOOL-M10), `-ftrivial-auto-var-init=pattern` 으로 증상을 결정적으로.

</details>

<details>
<summary>Q6. switch 에 default 를 넣어야 하나요? -Wswitch 와 -Wswitch-enum 차이는?</summary>

`-Wswitch`(`-Wall`)는 `default:` 가 **없을 때만** 빠진 enumerator 를 알려 준다. `-Wswitch-enum` 은 `default:` 가 있어도 알려 준다 — 실측 BUG12 는 `-Wswitch` 0, `-Wswitch-enum` 1. 상태 머신에서 권장 패턴: enum switch 에는 `default:` 를 두지 않고 모든 상태를 나열(새 상태 추가 시 컴파일러가 모든 switch 를 찾아 줌), 메모리 깨짐 등으로 범위 밖 값이 올 경우는 switch **뒤**에서 처리(good.c `state_name`). `-Wswitch-enum` 을 켜면 일부러 몇 개만 다루는 switch 에도 경고가 나므로, 그런 곳은 의도를 드러내게 나머지를 명시적으로 나열한다.

</details>

<details>
<summary>Q7. 펌웨어(Cortex-M4F)라면 무엇을 더 켜겠습니까?</summary>

`-Wdouble-promotion`(단정밀도 FPU 에서 `double` 연산은 소프트웨어 에뮬레이션 — 느리고 코드가 큼), `-Wvla`(런타임 크기 스택 배열 = 스택 오버플로 예측 불가), `-Wcast-align`(바이트 버퍼 → `uint32_t*`; M4 는 일반 LDR 은 비정렬 허용이지만 LDRD/LDM 은 fault — ARMv7-M 아키텍처 매뉴얼 기준, 확인 권장 — GCC 의 이 경고는 strict-alignment 타깃에서만 나오고, aarch64 호스트 gcc 에선 `-Wcast-align=strict` 가 필요했다. clang 은 `=strict` 를 모른다, 실측), `-Wcast-qual`(Flash 의 const 테이블에 쓰기 → bus fault), `-Wpointer-arith`, `-Wwrite-strings`(문자열 리터럴은 Flash). 9. 추가 연습 1에서 6개 모두 실측.

</details>

<details>
<summary>Q8. 같은 경고 목록을 clang 과 gcc 가 공유할 때 주의점은?</summary>

(1) 모르는 옵션 처리 차이: clang 은 `unknown warning option` **경고**(→ -Werror 면 error), GCC 는 모르는 `-W…` 를 **즉시 error**, 모르는 `-Wno-…` 는 다른 경고가 있을 때만 note. (2) 그룹 구성 차이: fall-through 는 GCC `-Wextra` 에만, `-Wreturn-type` 은 clang 만 기본. (3) 경고 위치 차이(BUG01, BUG09). (4) 공용 목록 + 컴파일러별 추가(`IS_CLANG` 분기)로 관리하고 CI 에서 둘 다 빌드한다.

</details>

## 9. 추가 연습

### 연습 1 — 펌웨어 strict 세트 (`practice.flags` + `fw_bugs.c`)

기준선은 `fw_bugs.c` 의 `// FW01`–`// FW06` 을 하나도 잡지 않는다. 기준선을 유지한 채 6개를 더해 모두 잡되, good.c 는 여전히 `-Werror` 로 조용해야 한다.

<!-- code: fw_bugs.c -->

<details>
<summary>답 — 6개 플래그와 실측 경고</summary>

| 태그 | 버그 | 플래그 | Mac clang | arm-none-eabi-gcc |
|---|---|---|---|---|
| FW01 | `temp_c > 85.0` (float vs double) | `-Wdouble-promotion` | `implicit conversion increases floating-point precision` | `implicit conversion from 'float' to 'double' to match other operand` |
| FW02 | `uint8_t tmp[n];` | `-Wvla` | `variable length array used` | `ISO C90 forbids variable length array 'tmp'` |
| FW03 | `*(const uint32_t *)(rx + 1)` | `-Wcast-align` | `increases required alignment from 1 to 4` | `cast increases required alignment of target type` (aarch64 gcc 는 `=strict` 일 때만) |
| FW04 | `(uint8_t *)k_gamma` 로 const 제거 | `-Wcast-qual` | `drops const qualifier` | `cast discards 'const' qualifier` |
| FW05 | `void *` 산술 | `-Wpointer-arith` | `[-Wgnu-pointer-arith]` | `pointer of type 'void *' used in arithmetic` |
| FW06 | `char *name = "uart1";` 후 쓰기 | `-Wwrite-strings` | `[-Wincompatible-pointer-types-discards-qualifiers]` | `[-Wdiscarded-qualifiers]` |

FW06 은 `-Wwrite-strings` 가 리터럴의 타입을 `const char[]` 로 바꾸기 때문에 "qualifier 를 버린다" 는 다른 이름의 경고로 나온다. 고친 코드: FW01 `85.0f`, FW02 고정 최대 크기 + 길이 검사, FW03 `memcpy` 로 읽기(C-B 트랙의 비정렬 접근), FW04 테이블을 RAM 사본으로, FW05 `const uint8_t *` 로, FW06 `const char *` 또는 `char name[] = "uart1";`.

<!-- code: practice.flags diff=solution.flags -->

</details>

### 연습 2 — 범위를 좁힌 `#pragma` (`practice_pragma.c`)

필드에서 검증된 레거시 CRC 함수는 이번 릴리스에서 고치지 않기로 했다. 빌드 플래그는 건드리지 않고 **그 함수에서만** 변환 경고를 끄되, 함수 뒤의 코드(`-DSCOPE_PROBE` 의 `// PROBE` 줄)는 여전히 경고가 나야 한다.

<details>
<summary>답 — push / ignored / pop</summary>

`#pragma GCC diagnostic push` → `ignored "-Wsign-conversion"`, `ignored "-Wconversion"` → 함수 → `pop`. clang 도 `#pragma GCC diagnostic` 을 이해한다(실측 두 컴파일러 모두 통과). pragma 없이 빌드하면 clang 은 3개(`int`→`size_t` 부호 1, `int`→`uint16_t` 정밀도 2), gcc 는 1개(`int`→`size_t`)를 낸다 — 컴파일러마다 끄는 대상 목록이 다를 수 있어 둘 다 적었다. `pop` 을 빼면 PROBE 경고가 사라지는 것을 check.sh 가 잡는다.

<!-- code: practice_pragma.c -->

</details>

### 연습 3 — GCC 전용 플래그 분기

`demo_ci.mk` 를 읽고, `make -f demo_ci.mk show` 를 Mac 과 VM(`CC=gcc`)에서 돌려 `CFLAGS` 가 어떻게 달라지는지 확인하라. `demo_gcc_only.c` 의 버그 4개를 각각 고쳐 보고, 고친 뒤 GCC 4개 플래그로 경고 0 인지 확인.

<details>
<summary>답</summary>

`IS_CLANG := $(shell $(CC) --version | grep -c clang)` 가 0 이면 `-Wlogical-op -Wduplicated-cond -Wduplicated-branches` 를 붙인다(실측 출력은 5. 풀이 Part C). G1 `UART_SR_ORE` 로, G2 세 번째 조건 `code == 3`, G3 `raw * 2`, G4 `int hdr` 선언을 `goto` 앞으로(C 에선 합법이지만 C++ 에선 에러인 패턴). `-Wjump-misses-init` 은 clang 도 알지만 메시지가 "incompatible with C++" 다.

</details>

### 한 번에 검증

<!-- code: practice.flags -->

```bash
bash check.sh practice
```

실제 출력:

```text
flags (practice.flags): -Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum -Wsign-conversion -Wformat=2 -Wstrict-prototypes -Wmissing-prototypes -Wundef -Wdouble-promotion -Wvla -Wcast-align -Wcast-qual -Wpointer-arith -Wwrite-strings -isystem third_party 
PASS rules: no -w / -Weverything / -Wno-error
PASS rules: third_party/vendor_hal.h untouched
PASS bugs12.c BUG01 line 29 [-Wsometimes-uninitialized]
... (BUG02–BUG12 PASS, solution 과 같음)
PASS bugs12.c no warnings on untagged lines
PASS good.c builds with -Werror and 0 diagnostics
PASS good.c runs: 14/14 self-checks OK
PASS fw_bugs.c FW01 line 18 [-Wdouble-promotion]
PASS fw_bugs.c FW02 line 23 [-Wvla]
PASS fw_bugs.c FW03 line 34 [-Wcast-align]
PASS fw_bugs.c FW04 line 39 [-Wcast-qual]
PASS fw_bugs.c FW05 line 45 [-Wgnu-pointer-arith]
PASS fw_bugs.c FW06 line 50 [-Wincompatible-pointer-types-discards-qualifiers]
PASS fw_bugs.c no warnings on untagged lines
PASS practice_pragma.c: legacy block silenced, -Werror build clean
PASS practice_pragma.c PROBE line 34 [-Wsign-conversion]
PASS practice_pragma.c: push/pop balanced (1/1)
27/27 passed
exit=0
```

## 10. 핵심 정리

- `-Wall` ≠ 전부. 실측 clang `-Wall` 6/12 → 12/12 에는 `-Wall -Wextra -Wshadow -Wconversion -Wimplicit-fallthrough -Wswitch-enum` 6개가 필요(clang·gcc 동일).
- 기본값·그룹이 컴파일러마다 다르다: `-Wreturn-type`/`-Wparentheses` 는 clang 기본·GCC `-Wall`, fall-through 는 GCC `-Wextra`·clang 명시.
- GCC 미초기화 경고는 최적화 의존(`-O0` 에서 놓침), clang 은 `-O` 무관하지만 배열·구조체·루프는 못 봄, 다른 TU 의 출력 인자는 아무도 못 봄.
- `-Wconversion` 결과는 LP64 호스트와 ILP32 Cortex-M 에서 다르다 → CI 에 타깃 빌드. 경고는 캐스트가 아니라 범위·타입으로 고친다.
- 벤더 헤더는 `-isystem`. 레거시 함수는 `#pragma GCC diagnostic push/ignored/pop` 으로 범위를 좁혀서.
- `-Werror` 는 고정 툴체인 CI 에서(`WERROR=1`), 확정 버그류는 `-Werror=return-type` 처럼 항상. GCC 전용 플래그는 컴파일러 분기.
- 펌웨어 추가: `-Wdouble-promotion -Wvla -Wcast-align -Wcast-qual -Wpointer-arith -Wwrite-strings`.

## 참고자료

- GCC Manual — Warning Options: https://gcc.gnu.org/onlinedocs/gcc/Warning-Options.html (`-Wall`/`-Wextra` 포함 목록, `-Wimplicit-fallthrough=n`, `-Wmaybe-uninitialized` 와 최적화)
- GCC Manual — Diagnostic Pragmas: https://gcc.gnu.org/onlinedocs/gcc/Diagnostic-Pragmas.html
- GCC Manual — Directory Options (`-isystem` 탐색 순서): https://gcc.gnu.org/onlinedocs/gcc/Directory-Options.html
- Clang Diagnostics Reference (그룹별 포함 관계): https://clang.llvm.org/docs/DiagnosticsReference.html
- Clang User's Manual — Controlling Diagnostics, System Headers: https://clang.llvm.org/docs/UsersManual.html#controlling-diagnostics-in-system-headers
- ISO/IEC 9899:2011 (C11) 7.21.6.1p9 — 형식 지정자와 인자 타입 불일치는 UB
- 이 레슨의 실측 원본: `matrix.txt`(`demo_matrix.sh`), `extra.txt`(`demo_extra.sh`), `ci.txt`(`demo_ci.mk`)
- 관련 레슨: C-B02(정수 승격), TOOL-B07(전처리기·`-Wundef`), TOOL-B12(UBSan), TOOL-B13(로그 매크로 format 속성), TOOL-M10(leaks/valgrind), TOOL-M12(정적 분석)
