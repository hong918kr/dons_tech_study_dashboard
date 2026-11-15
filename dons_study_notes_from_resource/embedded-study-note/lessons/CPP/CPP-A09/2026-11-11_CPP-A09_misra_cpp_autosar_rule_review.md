---
id: CPP-A09
time: 120분
updated: 2026-11-11
---
# CPP-A09 MISRA C++/AUTOSAR 규칙 적용 리뷰

## 학습 목표

- `clang-tidy`(cppcoreguidelines/cert/bugprone/misc/readability/modernize)를 "C with classes" 레거시 펌웨어에 돌린 결과 184개를 **진짜 결함 / 안전 규칙 위반 / 스타일·소음**으로 나누고, 각 항목에 MISRA C++:2023 규칙 번호(공개 목록으로 확인한 것만)와 근거를 붙일 수 있다.
- C++ 고유 위반(암시적 축소, C 캐스트·`reinterpret_cast`, union 퍼닝, `new[]`/`delete` 불일치, 비가상 소멸자, rule of 3/5, 미초기화 멤버, 예외·RTTI, goto, 재귀, 가변 인자, 전역)을 고친 코드를 쓰고, 고친 결과가 Cortex-M 에서 **공짜인지 실측**(thumbv7em/thumbv6m asm, `llvm-nm`)으로 말할 수 있다.
- 리팩터의 계약을 **동등성 도메인 + 의도된 차이 D1..D9**로 나누고, 전수·xorshift 차등 테스트로 증명한다 (C-A10 패턴의 C++판).
- clang-tidy 가 **못 잡는** 것(배열 `new[]` 를 `delete` 로 해제, unsigned 로의 축소, 한 식 안의 두 레지스터 읽기 순서, 생성자 안의 가상 호출, `uint16_t` 곱의 int 오버플로, 로직 버그)을 데모로 재현하고 어떤 그물로 메울지 고른다.
- NOLINT 정책(체크 이름 + 이탈 번호 DEV-n, 글롭 금지)과 "MMIO 는 `reinterpret_cast` 이탈 1곳 또는 링커가 놓는 구조체" 같은 실무 결론을 설명할 수 있다.

## 1. 문제

자동차·방산·의료 펌웨어 팀의 시니어 면접에서 "MISRA 아세요?" 다음 질문은 항상 같다: **"이 C++ 코드 리뷰해 보세요. 도구는 뭘 잡고, 뭘 놓치고, 고친 게 동작을 안 바꿨다는 건 어떻게 보이죠?"** C-A10 이 C 레거시로 이 흐름(도구 → 분류 → 특성화 테스트 → 리팩터 → 증명)을 했다. 이 레슨은 같은 흐름을 C++ 에 적용한다. C++ 에서는 문제의 종류가 다르다 — 캐스트 다섯 종류, 소유권과 수명, 가상 소멸자, 예외/RTTI, 암시적 변환이 생성자·연산자 오버로드에 숨어 있다.

`legacy_mc100.cpp` 는 가상의 "MC100 BLDC 모터 컨트롤러" 펌웨어(193줄)다. 팀 빌드 옵션(예외·RTTI 켬, `-Wall -Wextra -Werror`)으로 **경고 0개**, 호스트 selftest 도 통과한다:

```text
$ bash tidy.sh build     # c++ -std=c++20 -Wall -Wextra -Werror -O2 legacy_mc100.cpp demo_legacy_main.cpp
duty(50)=127 duty(100)=255
decode: type=1 id=0x1234 value=-100 crc=0xDEAD
rpm_to_q3(1000)=8000  rpm_index(1000)=4
motor_init(3)=2 faults=0
cmd_set_speed(0, 6000)=0 CCR0=500
mean=15
selftest ok
```

같은 파일에 과제의 체크 세트를 걸면 184개가 나온다. 그리고 selftest 가 쓰지 않은 입력(필드에서 실제로 들어온 값)을 넣으면 `int16_t` 함수가 **40000** 을 돌려주고, 로그 버퍼가 넘친다 (§5 실측). 이 레슨의 그림 — 그물은 여러 겹이고, 겹마다 잡는 게 다르다:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">레거시 C++ 리뷰: 다섯 겹의 그물 (legacy_mc100.cpp 의 진짜 결함 10개 기준, §5 실측)</text>
  <rect x="10" y="34" width="150" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">① clang-tidy</text>
  <text x="20" y="70" fill="currentColor" font-size="12">184개 중 결함은 일부</text>
  <rect x="170" y="34" width="560" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="180" y="54" fill="currentColor" font-size="12">잡음: 미초기화 멤버, 비가상 소멸자, rule of 3/5, 재귀, goto, 캐스트·union (규칙으로서)</text>
  <text x="180" y="70" fill="currentColor" font-size="12">놓침: delete/new[] 불일치, unsigned 축소, 음수 rpm, 로그 넘침, 예외·RTTI 사용 자체</text>
  <rect x="10" y="86" width="150" height="44" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="106" fill="currentColor">② 컴파일러</text>
  <text x="20" y="122" fill="currentColor" font-size="12">-Wconversion 계열</text>
  <rect x="170" y="86" width="560" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="180" y="106" fill="currentColor" font-size="12">-Wimplicit-int-conversion(duty 101→1), -Wcast-align, -Wcast-qual, -Wsign-conversion</text>
  <text x="180" y="122" fill="currentColor" font-size="12">-fno-exceptions -fno-rtti 빌드 = throw/try/dynamic_cast 를 에러로 (가장 확실한 "규칙 검사기")</text>
  <rect x="10" y="138" width="150" height="44" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="20" y="158" fill="currentColor">③ sanitizer</text>
  <text x="20" y="174" fill="currentColor" font-size="12">입력이 있어야 잡는다</text>
  <rect x="170" y="138" width="560" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="180" y="158" fill="currentColor" font-size="12">UBSan: 정렬 안 맞는 읽기(selftest 에서!), float→short 범위 밖. ASan: 전역 버퍼 넘침,</text>
  <text x="180" y="174" fill="currentColor" font-size="12">new[]/delete 불일치 (맥에선 ASAN_OPTIONS=alloc_dealloc_mismatch=1 일 때만)</text>
  <rect x="10" y="190" width="150" height="44" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="210" fill="currentColor">④ 동등성 테스트</text>
  <text x="20" y="226" fill="currentColor" font-size="12">old == new + D 목록</text>
  <rect x="170" y="190" width="560" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="180" y="210" fill="currentColor" font-size="12">보정(clear) 후 낡은 칸 평균(D5), 얕은 복사가 원본을 바꿈(D4) — 로직 버그는 테스트만 본다</text>
  <text x="180" y="226" fill="currentColor" font-size="12">"경고 수정" 이 비트를 바꾸는지 (0.9 → 0.9F: 2만 스텝 중 10670 스텝 다름)</text>
  <rect x="10" y="242" width="150" height="44" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="262" fill="currentColor">⑤ 리뷰 체크리스트</text>
  <text x="20" y="278" fill="currentColor" font-size="12">도구 밖의 규칙</text>
  <rect x="170" y="242" width="560" height="44" rx="6" fill="none" stroke="currentColor"/>
  <text x="180" y="262" fill="currentColor" font-size="12">한 식 안 두 레지스터 읽기 순서(4.6.1), 생성자 안 가상 호출(15.1.1), uint16×uint16 승격(7.0.5),</text>
  <text x="180" y="278" fill="currentColor" font-size="12">가변 인자 타입(8.2.11) — demo_tool_gaps.cpp 에서 tidy 가 조용했던 것들</text>
</svg>
```

`starter.cpp` 의 `namespace legacy` 에는 레거시 코드가 **글자 그대로** 복사되어 있다 (고치지 않는다 — 동작의 기준). 예외·RTTI 를 쓰는 `set_speed`/`calibrate_all` 은 `-fno-exceptions -fno-rtti` 로 컴파일되지 않으므로 하네스에서 뺐다 (그게 이미 하나의 발견이다). `namespace mc` 에 같은 일을 하는 새 코드를 쓴다. 완성본은 `fixed_mc100.cpp`(레거시와 같은 순서라 `diff -u` 로 비교 가능)와 같은 코드다.

### Part A — 변환 · 캐스트 · union

- `mc::duty_from_pct(int32_t)` — 0..100 % → 0..255. 0..100 **전수** 에서 legacy 와 같고, **D1**: 범위 밖은 포화(legacy 는 101 % → 1).
- `mc::decode(std::span<const uint8_t, 7>)` — 리틀 엔디언 프레임 `[type][id:2][value:2][crc:2]`. legacy 는 `*(const uint16_t*)(f + 1)` 등 포인터 캐스트 3개. **D2**: 새 코드는 어떤 정렬의 버퍼에서도 정의된 동작. 2만 프레임 동등.
- `mc::float_bits(float)` — union 없이. 10만 비트 패턴(NaN 포함)에서 동등 + 왕복.
- `mc::rpm_to_q3(float)` — rpm → Q12.3. 표현 가능한 범위에서 legacy 와 같은 0 쪽 버림, **D3**: 범위 밖 포화, NaN → 0 (legacy 는 [conv.fpint] UB).

### Part B — 클래스 · 수명 · 소유권

- `mc::SampleWindow<N>` — rule of 0 (`std::array` 멤버). legacy `SampleBuf`(생성자에서 `new int16_t[n]`, 소멸자에서 `delete`, 암시적 얕은 복사, `explicit` 없음)와 2000개 무작위 수열에서 매 push 평균이 같다. **D4**: 복사는 독립. **D5**: `clear()` 뒤 첫 샘플의 평균은 그 샘플. 힙 할당 0회.
- `mc::Pid` — 비정적 멤버 초기화(NSDMI)로 미초기화 제거, `explicit`, `PidGains` 구조체로 바꿔 넣기 쉬운 float 3개 제거. legacy(`reset()` 뒤)와 2만 스텝 **비트 단위** 동등.
- `mc::Sensor` 인터페이스 — 기반 포인터 `delete` 불가(protected 비가상 소멸자), 복사·이동 금지, 구현은 `final`. `dynamic_cast<HallSensor*>` 대신 가상 `calibrate()`. 요구사항은 하네스의 트레이트 CHECK 다.

### Part C — 제어 흐름: 재귀 · goto · 가변 인자 · 전역

- `mc::lut_find(std::span<const uint16_t>, uint16_t)` — 재귀 이분 탐색 → 루프. 65536 전수 + 무작위 표 3000개.
- `mc::Log<N>::put` — `vsnprintf` 가변 인자 로거 → 타입 안전 고정 버퍼. `"%d"` 와 2만 개 동등(INT32_MIN 포함), **D8**: 가득 차면 버리고 표시.
- `mc::motor_init(probe, retries, log, faults)` — 뒤로 가는 `goto` 와 전역 `g_fault_count`/`g_log` 제거. 13×13 조합에서 반환값·probe 호출 수·고장 수·**로그 글자** 까지 같다.

## 2. Problem (English)

> **Review a legacy C++ firmware file against MISRA C++:2023-style rules and refactor it with proof of equivalence.**
>
> `legacy_mc100.cpp` builds warning-free with `-Wall -Wextra -Werror` (exceptions and RTTI on) and its self-test passes. Run `clang-tidy --checks='-*,cppcoreguidelines-*,cert-*,hicpp-*,bugprone-*,misc-*,readability-*,modernize-*'`, classify every finding as *real defect / safety-rule violation / style noise*, and map it to a MISRA C++:2023 rule. Then rewrite, under `-fno-exceptions -fno-rtti`:
>
> 1. `duty_from_pct` (saturating), `decode` (no pointer casts, any alignment, explicit little-endian), `float_bits` (no union), `rpm_to_q3` (no out-of-range float→int conversion).
> 2. `SampleWindow<N>` (rule of zero, no heap), `Pid` (no indeterminate members, bit-identical output), a `Sensor` interface that cannot be deleted through a base pointer or sliced, with no `dynamic_cast`.
> 3. `lut_find` without recursion, a vararg-free bounded logger, `motor_init` without `goto` or globals.
>
> The harness compares legacy vs new on the equivalence domain (exhaustive where possible, xorshift32 otherwise) and checks each intended difference D1–D9 explicitly. Report what clang-tidy missed and how you covered it, your suppression policy, and the Cortex-M cost of each fix.
>
> Follow-ups: How do you do MMIO when `reinterpret_cast` is banned? Public-virtual or protected-non-virtual destructor? Are exceptions banned by MISRA? Is `0.9 → 0.9F` a fix? What does a justified `NOLINT` look like?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "9/37 passed" (legacy 를 고정하는 테스트와 트레이트 일부만) — TODO 를 채워 37/37
```

분석 대상과 관찰용 파일 (하네스와 별개, 전부 `tidy.sh` 가 빌드한다 — 끝나면 exit 0):

```bash
bash tidy.sh counts   # 과제 체크 세트로 legacy vs fixed 경고 수 (184 / 83)
bash tidy.sh policy   # 소음을 끈 프로젝트 정책으로 (127 / 0) + fixed 의 NOLINT 목록
bash tidy.sh build    # legacy selftest, legacy 를 -fno-exceptions -fno-rtti 로 (에러 3개), fixed 를 펌웨어 플래그로
bash tidy.sh san      # legacy 를 ASan+UBSan 으로, FIELD_CASES 입력까지
bash tidy.sh gaps     # demo_tool_gaps.cpp: 도구가 못 잡는 것
bash tidy.sh asm      # demo_m4_asm.cpp → Cortex-M4F / M0+ asm, llvm-nm -u, vtable 재배치
bash tidy.sh vm       # demo_linker_regs.cpp 를 Lima VM 의 arm-none-eabi-g++ 14.2.1 로 링크 (링커가 놓는 레지스터)
```

`clang-tidy` 는 `~/.venvs/clangtidy/bin/clang-tidy` (LLVM 23.1.4). `--` 뒤는 컴파일 옵션이고 맥에선 `-isysroot $SDKROOT` 가 있어야 표준 헤더를 찾는다.

## 4. 힌트

<details>
<summary>힌트 1 — 분류 기준: "필드에서 틀린 값이 나오는가 / 규칙이 금지하는가 / 둘 다 아닌가"</summary>

경고마다 ① 지금 어떤 입력에서 틀린 값·UB 가 나오는가(진짜 결함 → D 목록, old 값도 테스트로 고정) ② 규칙이 금지하지만 지금은 동작이 맞는가(안전 규칙 → 동작 보존 수정) ③ 둘 다 아닌가(소음 → 정책에서 끈다, 이유를 적는다). 같은 줄에 체크 둘이 붙기도 한다 — `modernize-avoid-c-style-cast` 와 `cppcoreguidelines-pro-type-cstyle-cast` 는 같은 캐스트에 각각 경고를 낸다. 개수가 아니라 **위치(결함)** 단위로 센다.

</details>

<details>
<summary>힌트 2 — Part A: 포인터 캐스트 대신 바이트 조립, union 대신 std::bit_cast</summary>

`f.subspan<1, 2>()` 는 길이까지 타입에 든 `std::span<const uint8_t, 2>` 다. `b[0] | (b[1] << 8)` 를 **unsigned 로 넓힌 뒤** 계산한다 (`uint8_t` 는 int 로 승격되어 부호 있는 비트 연산이 된다). 값 필드는 `std::bit_cast<std::int16_t>(u16)`. 컴파일러가 바이트 조립을 `ldrh` 한 개로 합치는지는 §5 에서 실측한다. `rpm_to_q3` 는 **변환 전에** 범위를 검사한다: 잘라서 int16 에 들어가는 q 는 `-32769 < q < 32768` 이다.

</details>

<details>
<summary>힌트 3 — Part B: legacy 객체를 하네스에서 어떻게 관찰하나</summary>

legacy `SampleBuf` 의 소멸자는 `new[]` 를 `delete` 로 해제하고(UB), 복사본 둘이 같은 버퍼를 해제한다(이중 해제). 그래서 하네스는 legacy 객체를 `NoDestroy<T>`(placement new 로 만들고 소멸자를 **부르지 않는** 저장소)에 담는다. 소멸자를 안 부르는 것은 합법이다 — 프로그램이 그 부수 효과에 기대지 않으면 된다 ([basic.life]). 덕분에 "복사본에 push 하면 원본 평균이 바뀐다" 는 버그를 UB 없이 테스트로 고정할 수 있다.

</details>

<details>
<summary>힌트 4 — Part B: Pid 의 비트 동등과 Sensor 의 소멸자</summary>

`d_filt = 0.9 * d_filt + 0.1 * (err - prev);` 는 **double** 로 계산한 뒤 float 로 줄인다. 비트까지 같게 하려면 double 상수를 유지하고 변환을 `static_cast` 로 드러낸다. `0.9F` 로 바꾸는 건 연습 1 에서 숫자로 본다. Sensor: CppCoreGuidelines C.35 — 기반 클래스 소멸자는 "public 이고 virtual" 이거나 "protected 이고 비가상". 펌웨어에서 어느 쪽이 싼지는 vtable 을 보면 안다.

</details>

<details>
<summary>힌트 5 — Part C: goto 의 "다시" 를 루프로, 로그는 글자 단위로 같게</summary>

legacy `motor_init` 은 `retries <= 0` 이어도 probe 를 **한 번은** 부른다. `while (!ok) { ok = probe() == 0; if (!ok) { ++tries; if (tries >= retries) break; } }` 가 같은 횟수를 만든다. 로그는 `"init ok "` + 정수 + `"\n"` — 정수 출력은 `INT32_MIN` 에서 `-v` 를 하면 UB 이므로 unsigned 로 바꾼 뒤 `0U - mag` 로 부정한다.

</details>

## 5. 풀이

### 0. 도구를 돌린다 — 실측

**과제 체크 세트, legacy 184개 (LLVM 23.1.4):**

```text
$ bash tidy.sh counts
hicpp module: No checks enabled.
== legacy_mc100.cpp : 184 warnings
  25 [misc-use-internal-linkage]
  16 [readability-identifier-length]
  14 [modernize-use-trailing-return-type]
  14 [cppcoreguidelines-avoid-magic-numbers,readability-magic-numbers]
   9 [readability-braces-around-statements]
   8 [cppcoreguidelines-pro-type-cstyle-cast]
   8 [cppcoreguidelines-pro-bounds-pointer-arithmetic]
   7 [readability-math-missing-parentheses]  7 [cppcoreguidelines-macro-usage]  7 [...macro-to-enum]
   5 [misc-non-private-member-variables-in-classes]  5 [misc-const-correctness]
   5 [cppcoreguidelines-avoid-non-const-global-variables]
   4 [modernize-avoid-c-style-cast]  4 [cppcoreguidelines-pro-type-vararg]
   3 [cppcoreguidelines-virtual-class-destructor]  3 [cppcoreguidelines-special-member-functions]
   3 [cppcoreguidelines-pro-type-member-init]  3 [cppcoreguidelines-owning-memory]
   3 [...avoid-c-arrays]  3 [bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions]
   2 [...pro-type-union-access]  2 [...pro-bounds-array-to-pointer-decay]  2 [...use-override]
   1 [misc-no-recursion]  1 [cppcoreguidelines-avoid-goto]  1 [...pro-type-reinterpret-cast]
   1 [cert-dcl50-cpp,modernize-avoid-variadic-functions]  1 [...explicit-constructor]  ... (1개짜리 13종 생략)
```

(한 줄에 여러 개 붙인 것은 지면 때문. 원본 출력은 `tidy.sh counts`.) 먼저 눈에 띄는 것 세 가지:

1. **`hicpp-*` 는 0개다 — 체크가 아예 없다.** LLVM 23.1.4 의 `--list-checks` 에 hicpp 모듈이 없다(실측). LLVM 업스트림에는 "Remove hicpp modules" 커밋(#196870, 체크는 다른 모듈로 옮김)과 라이선스 문제로 23 릴리스에서 뺀다는 토론이 있다. 정확히 어느 릴리스부터인지는 릴리스 노트로 확인하지 못했다(미확인). 옛 `hicpp-signed-bitwise` 같은 것은 `bugprone-signed-bitwise` 로 남아 있다. 면접 포인트: **체크 세트 이름은 도구 버전마다 바뀐다** → `.clang-tidy` 와 도구 버전을 같이 고정한다.
2. **`cert-*` 는 1개뿐**(`cert-dcl50-cpp` = C 스타일 가변 함수 정의). 나머지 cert 체크는 대부분 다른 체크의 별칭이라 같은 줄에 이름이 합쳐져 나온다.
3. 같은 결함에 경고가 둘: 46·48 줄 C 캐스트는 `modernize-avoid-c-style-cast` 와 `cppcoreguidelines-pro-type-cstyle-cast` 가 **각각** 경고한다. 그래서 "184개" 는 결함 수가 아니다.

**컴파일러 `-Weverything` 은 tidy 와 다른 것을 본다** (`bash tidy.sh build` 마지막 부분 발췌):

```text
legacy_mc100.cpp:35:27: warning: implicit conversion loses integer precision: 'int' to 'uint8_t' (aka 'unsigned char') [-Wimplicit-int-conversion]
legacy_mc100.cpp:46:13: warning: cast from 'const uint8_t *' ... to 'const uint16_t *' ... increases required alignment from 1 to 2 [-Wcast-align]
legacy_mc100.cpp:48:25: warning: cast from 'const unsigned char *' to 'unsigned short *' drops const qualifier [-Wcast-qual]
legacy_mc100.cpp:97:31: warning: implicit conversion loses floating-point precision: 'double' to 'float' [-Wimplicit-float-conversion]
legacy_mc100.cpp:109:5: warning: 'Sensor' has virtual functions but non-virtual destructor [-Wnon-virtual-dtor]
legacy_mc100.cpp:144:32: warning: implicit conversion changes signedness: 'int' to 'uint32_t' (aka 'unsigned int') [-Wsign-conversion]
```

35 줄(`uint8_t d = pct * 255 / 100;`)은 **tidy 가 조용했다**: `bugprone-narrowing-conversions` 는 축소 3개(63 `float`→`int16_t`, 82 `int`→`int16_t`, 97 `double`→`float`)만 보고했고, unsigned 정수로의 축소는 보고하지 않았다 (결과만 실측 — 체크 옵션 중 무엇이 이걸 가르는지는 확인 못 함, 미확인). 그런데 이게 이 파일의 1번 결함이다(101 % → 듀티 1).

**예외·RTTI 는 tidy 의 어떤 체크도 경고하지 않았다.** 대신 펌웨어 플래그가 잡는다:

```text
$ bash tidy.sh build   (legacy 를 -std=c++20 -fno-exceptions -fno-rtti 로)
legacy_mc100.cpp:136:25: error: use of dynamic_cast requires -frtti
legacy_mc100.cpp:143:24: error: cannot use 'throw' with exceptions disabled
legacy_mc100.cpp:149:5: error: cannot use 'try' with exceptions disabled
```

**sanitizer — 입력이 있어야 보인다.** selftest 자체에서 이미 UBSan 이 터진다 (DMA 버퍼 `rx` 는 4바이트 정렬이므로 `rx + 1` 의 16비트 필드는 전부 홀수 주소):

```text
$ bash tidy.sh san
legacy_mc100.cpp:46:12: runtime error: load of misaligned address 0x00016d3a2a21 for type 'const uint16_t' (aka 'const unsigned short'), which requires 2 byte alignment
legacy_mc100.cpp:47:15: runtime error: load of misaligned address 0x00016d3a2a23 for type 'const int16_t' ...
legacy_mc100.cpp:48:13: runtime error: load of misaligned address 0x00016d3a2a25 for type 'uint16_t' ...
selftest ok
== selftest, ASAN_OPTIONS=alloc_dealloc_mismatch=1
==43967==ERROR: AddressSanitizer: alloc-dealloc-mismatch (operator new [] vs operator delete) on 0x6020000009d0
```

맥 ASan 은 `new[]`/`delete` 불일치를 **기본으로 보고하지 않았다** (첫 실행은 조용, 옵션을 켜야 보고). 필드 입력(`-DFIELD_CASES`)을 넣으면:

```text
== FIELD_CASES, plain -O2
duty(101)=1  rpm_to_q3(5000)=40000
log[21]: init ok 2
m0 duty=0%
log_len=65 (sizeof g_log = 64)
selftest ok
== FIELD_CASES under ASan+UBSan
legacy_mc100.cpp:63:17: runtime error: 40000 is outside the range of representable values of type 'short'
legacy_mc100.cpp:28:34: runtime error: index 65 out of bounds for type 'char[64]'
==43981==ERROR: AddressSanitizer: global-buffer-overflow on address 0x0001043510c1 ...
```

`int16_t` 를 반환하는 함수가 **40000** 을 돌려줬다 — UB 의 실제 모습이다. arm64 `-O2` 의 함수 본문 전체가 `fcvtzs w0, s0, #3` (×8 을 고정소수점 변환에 접어 넣은 float→**32비트** 정수 변환) + `ret` 였다. 범위 밖은 UB 이므로 컴파일러는 결과가 int16 에 들어간다고 가정하고 16비트로 자르거나 부호 확장하는 명령을 넣지 않았다. `report()` 는 `float` 를 `%d` 로 넘겨 `duty=0%` 를 찍었다(이 ABI 에서 가변 인자 double 50.0 슬롯의 하위 32비트가 0 — UB 라서 값은 우연). 그리고 로그가 64바이트를 넘기면 `vsnprintf` 가 "썼을 길이" 를 돌려주므로 `g_log_len` 이 65 가 되고, 다음 호출의 크기 인자 `sizeof(g_log) - g_log_len` 은 `size_t` 로 **거의 2^64** 가 된다. plain 빌드는 조용히 전역 메모리를 덮고 `selftest ok` 를 찍었다.

### 분류 — 발견 14개 + 로직 1개를 규칙과 판정으로

규칙 번호 "M:x.y.z" 는 MISRA C++:2023 이다. 규칙 문장은 유료라 인용하지 않고, 번호와 제목은 공개된 Polyspace 규칙 목록으로 확인한 것만 적었다. AUTOSAR C++14 규칙 번호는 PDF 를 열지 못해 쓰지 않는다(미확인).

| # | legacy (줄) | tidy | 다른 그물 | MISRA C++:2023 | 판정 |
|---|---|---|---|---|---|
| 1 | `uint8_t d = pct*255/100` (35) | **없음** | `-Wimplicit-int-conversion` | 7.0.6 수치 대입 | **결함**: 101 % → 1 (D1) |
| 2 | `*(const uint16_t*)(f+1)` (46–48) | cstyle/reinterpret (규칙으로) | `-Wcast-align`, UBSan | 8.2.2, 8.2.5, 4.1.3 | **결함**: 정렬·aliasing UB, M0+ HardFault (D2) |
| 3 | `(uint16_t*)(f+5)` const 제거 (48) | cstyle | `-Wcast-qual` | 8.2.3 | 규칙 (쓰기는 안 함) |
| 4 | union 퍼닝 (52–59) | union-access 2 | — | 12.3.1 | 규칙 — C++ 에선 비활성 멤버 읽기 UB, GCC/Clang 은 확장으로 허용 |
| 5 | `int16_t q = rpm*8.0f` (63) | narrowing ✔ | UBSan(필드) | 7.0.6, 4.1.3 | **결함**: 4096 rpm 이상 UB (D3). MAX_RPM 은 12000! |
| 6 | `new int16_t[n]` … `delete data` (70–71) | **없음** | ASan(옵션) | 21.6.1, 21.6.2 | **결함**: 매 소멸마다 UB |
| 7 | SampleBuf 암시적 복사 (68) | special-member-functions | — | 15.0.1 | **결함**: 얕은 복사 → 이중 해제, 원본 오염 (D4) |
| 8 | `SampleBuf(int n)` (70) | explicit-constructor | — | 15.1.3 | 규칙: `SampleBuf b = 8;` 이 컴파일된다 |
| 9 | Pid `integ, d_filt, prev` 미초기화 (90) | pro-type-member-init ✔ | (MSan 없음) | 15.1.4, 11.6.2 | **결함**: `reset()` 안 부른 경로에서 쓰레기 적분 (D4) |
| 10 | 가상 함수 + public 비가상 소멸자 (106–123) | virtual-class-destructor ✔ | `-Wnon-virtual-dtor` | (목록에서 전용 규칙 못 찾음) | 잠재: 지금은 아무도 delete 안 함 |
| 11 | `throw`/`try`/`dynamic_cast` (136–149) | **없음** | `-fno-exceptions -fno-rtti` | 18.x 는 예외를 *규제*, 8.2.9 typeid | 프로젝트 정책 위반 (CPP-B13) |
| 12 | `REG(PWM_CCR + motor*4)`, 음수 rpm (144) | cstyle(규칙으로) | `-Wsign-conversion` | 6.7.2, 8.2.6, 7.0.5 | **결함**: motor 미검사, rpm=−1 → 거의 최대 듀티 (D6, D7) |
| 13 | `vsnprintf` 길이 누적 (28) | vararg/decay (규칙으로) | ASan(필드) | 21.10.1 | **결함**: 64B 넘으면 전역 넘침 (D8) |
| 14 | `float` 를 `%d` 로 (192) | vararg (규칙으로) | format 속성 + `-Wformat` | 8.2.11 | **결함**: UB 출력 (D9) |
| — | `lut_find` 재귀 (162), `goto again` (180) | no-recursion, avoid-goto ✔ | — | 8.2.10, 9.6.1/9.6.3 | 규칙 (재귀 깊이 ≤ 5 지만 정적 상한 불가) |
| — | 전역 4개 (16–19), C 배열, 포인터 연산 | ✔ | — | 6.7.2 | 규칙 |
| — | 이름 길이 16, trailing return 14, internal-linkage 25 | ✔ | — | 없음 | **소음** — 정책에서 끔 |

그리고 **어떤 도구도 못 본 결함 하나**: legacy `calibrate_all` 은 `h->hist->count = 0` 만 한다. `mean()` 은 `data[0..count)` 를 더하는데 `head` 는 그대로라, 보정 뒤 첫 샘플은 `data[head]` 에 들어가고 평균은 낡은 `data[0]` 이다 (하네스가 `mean == 10` 으로 고정, D5). 공개 멤버 변수(`misc-non-private-member-variables-in-classes`)가 이 버그를 가능하게 했다 — 규칙 위반이 결함의 **원인** 인 좋은 예다.

### MISRA C++:2023, AUTOSAR C++14, 그리고 clang-tidy 의 관계

- MISRA C++:2023 은 MISRA C++:2008 을 갱신하면서 AUTOSAR C++14 지침을 흡수했다 (두 단체가 2019 년에 C++ 지침 통합을 발표). 다만 AUTOSAR 규칙이 전부 그대로 들어간 것은 아니라는 설명이 도구 회사 자료에 있다. 대상 언어는 **C++17** 이다 — 규칙 4.1.1 이 "ISO/IEC 14882:2017 을 따른다" 이다.
- 그래서 이 레슨 코드(트랙 규칙상 C++20: `std::span`, `std::bit_cast`, 지정 초기화)는 엄밀히 말해 **4.1.1 에서 이탈** 이다. C++17 프로젝트라면 `bit_cast` 대신 `memcpy` 가 보통인데, 24.5.2 가 `<cstring>` 의 `memcpy` 사용을 금지하므로 그것도 문서화된 이탈 1곳이 된다. 면접에서 "MISRA C++ 에서 C++20 써도 되나요?" 에 대한 정확한 답이다.
- clang-tidy 에는 MISRA 모듈이 없다. `cppcoreguidelines-*` 는 C++ Core Guidelines(MISRA 가 아님), `cert-*` 는 SEI CERT C++ 이다. 겹치는 부분(캐스트, union, 가변 인자, goto, 재귀, 전역)이 많아 실무 1차 그물로 쓰지만, 준수 주장은 Helix QAC, Polyspace, Parasoft, PC-lint Plus, LDRA, Coverity 같은 인증된 검사기 + deviation 기록으로 한다 (C-A10 Q2).
- 확인한 목록의 표현: "shall" 과 "should" 가 섞여 있다(예: 21.6.1 *should not*, 8.2.5 *shall not*). 각 규칙의 Mandatory/Required/Advisory 분류 자체는 원문으로 확인하지 못했다(미확인).

### 도구가 못 잡은 것 — `demo_tool_gaps.cpp` 실측

```text
$ bash tidy.sh gaps
read16 = 0x1234 (HI first -> 0x1234, LO first -> 0x12AA), reads=2
ctor sees: Base
after ctor: Uart
area = 4294836225
duty=0%
duty=0%
== -DSHOW_FORMAT (format attribute on log_checked)
demo_tool_gaps.cpp:65:32: warning: format specifies type 'int' but the argument has type 'float' [-Wformat]
== UBSan
demo_tool_gaps.cpp:35:57: runtime error: signed integer overflow: 65535 * 65535 cannot be represented in type 'int'
== clang-tidy (brief set) minus naming/linkage/trailing-return noise
demo_tool_gaps.cpp:18:57: warning: use of a signed integer operand with a binary bitwise operator [bugprone-signed-bitwise]
demo_tool_gaps.cpp:22:14: warning: do not call c-style vararg functions [cppcoreguidelines-pro-type-vararg]
demo_tool_gaps.cpp:38:13: warning: do not define a C-style variadic function; ... [cert-dcl50-cpp,modernize-avoid-variadic-functions]
... (나머지는 전부 va_list / 가변 인자 호출에 대한 같은 종류)
```

| 결함 | 실행 결과 | tidy | 메우는 그물 |
|---|---|---|---|
| `(rd(H) << 8) \| rd(L)` | 이 빌드는 H 먼저 (0x1234) | 18 줄은 `signed-bitwise` 만 — **순서는 말 안 함** | 리뷰 (M:4.6.1 취지). C++17 은 `<<` 의 피연산자를 왼쪽부터 순서화했지만([expr.shift]) `\|` 는 아니다 → 두 호출 순서 미지정 (UB 는 아님). 문장 둘로 나눈다 |
| 생성자 안 가상 호출 | `ctor sees: Base` (Uart 아님) | 22 줄은 printf 가변 인자 경고뿐 | 리뷰 (M:15.1.1). 정적 분석기 `optin.cplusplus.VirtualCall` 은 이 체크 세트 밖 |
| `uint16_t w * h` | `-O2` 는 우연히 0xFFFE0001, UBSan 은 오버플로 보고 | **없음** | UBSan + 리뷰 (M:7.0.5). 연습 3 |
| `float` 를 `%d` 로 | `duty=0%` | 호출마다 "가변 인자 쓰지 마라" 만 | `__attribute__((format(printf,1,2)))` → `-Wformat` 이 타입을 본다. 근본 해결은 가변 인자 제거 (Log) |
| `new[]` → `delete` (legacy 71 줄) | 맥 ASan 기본은 조용 | **없음** (`owning-memory` 도 이 delete 는 안 짚음) | ASan + `alloc_dealloc_mismatch=1`, 근본 해결은 rule of 0 |
| unsigned 축소 (legacy 35 줄) | 101 → 1 | **없음** | `-Wconversion` 계열 + 경계값 테스트 |
| 로직: clear 뒤 낡은 평균 | 10 (100 이어야) | **없음** | 동등성/명세 테스트 (D5) |

### 수정 정책 — 무엇을 같게, 무엇을 다르게

| ID | 대상 | legacy (테스트로 고정한 값) | new | 근거 |
|---|---|---|---|---|
| D1 | duty | 101 → 1, −1 → 254, 200 → 254 | 255 / 0 / 255 | 7.0.6. 범위 검사는 **곱하기 전에** (INT32_MAX 도 안전) |
| D2 | decode | 홀수 주소 필드 = UB (UBSan 보고) | 어떤 정렬이든 같은 값 | 8.2.5, 4.1.3 |
| D3 | rpm_to_q3 | ≥ 4096 rpm UB (40000 관측) | 포화, NaN → 0 | [conv.fpint] |
| D4 | SampleBuf/Pid | 복사본 push 가 원본 평균을 10 → 20 으로 / 미초기화 | 독립 복사 / 새 객체는 0 상태 | 15.0.1, 15.1.4 |
| D5 | clear | 보정 뒤 첫 평균 = 낡은 `data[0]` (10) | 100 | 로직 버그 |
| D6 | set_speed | motor ≥ 2 → 다른 레지스터에 씀 | `Status::BadMotor`, 쓰기 없음 | 연습 2 |
| D7 | set_speed | rpm = −1 → CCR 357913 | `Status::BadRpm` | 7.0.5 |
| D8 | 로그 | 64B 넘으면 전역 넘침 | 버리고 `truncated()` | 21.10.1 |
| D9 | report | float→`%d` UB | 정수 % (`ccr*100/arr`, arr=0 → 0) | 8.2.11 |

**보존** 한 것 (동등성 도메인): duty 의 내림(50 % → 127), Q12.3 의 0 쪽 버림, Pid 의 **double 계산 결과 비트**, `lut_find` 의 lower-bound 의미, `retries <= 0` 이어도 probe 1회, 로그 글자. C-A10 과 같은 원칙 — 리팩터 커밋은 동작을 바꾸지 않고, 결함 수정은 D 목록으로 리뷰어가 승인한다.

### Part A — 변환 · 캐스트 · union: "고친 게 공짜인가" 를 Cortex-M 에서

```text
$ bash tidy.sh asm      # demo_m4_asm.cpp, clang++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -O2
_Z10decode_oldPKh:            _Z10decode_newPKh:          (legacy 캐스트 vs 바이트 조립)
	ldrh.w	r0, [r0, #1]      	ldrh.w	r0, [r0, #1]
	bx	lr                    	bx	lr
_Z10bits_unionf:              _Z9bits_castf:              (union vs __builtin_bit_cast)
	vmov	r0, s0                	vmov	r0, s0
	bx	lr                    	bx	lr
=================== cortex-m0+ (thumbv6m): decode only
_Z10decode_oldPKh:            _Z10decode_newPKh:
	movs	r1, #1                	ldrb	r1, [r0, #1]
	ldrh	r0, [r0, r1]          	ldrb	r0, [r0, #2]
	bx	lr                    	lsls	r0, r0, #8
	                              	adds	r0, r0, r1
	                              	uxth	r0, r0
	                              	bx	lr
```

(실제 출력은 함수가 위아래로 나온다 — 비교하려고 옆에 놓았다.) 읽는 법:

- **M4 에선 바이트 조립이 공짜다.** ARMv7-M 은 `LDRH` 의 비정렬 접근을 허용하므로(CCR.UNALIGN_TRP=0 일 때) 컴파일러가 두 바이트 읽기를 `ldrh.w` 한 개로 합쳤다 — 같은 명령이다. 다만 비정렬이 허용되는 건 `LDR/LDRH/STR/STRH` 류뿐이고 `LDRD/LDM` 은 비정렬이면 항상 폴트한다. 캐스트 코드는 "컴파일러가 언제 `ldrd` 로 합치느냐" 에 운을 건다.
- **M0+ 에선 legacy 가 HardFault 다.** ARMv6-M 은 모든 비정렬 접근이 폴트다. legacy 는 `ldrh r0, [r0, r1]` 로 홀수 주소를 읽는다 — 이 레거시를 M0+ 제품군으로 포팅하는 순간 첫 프레임에서 멈춘다. 새 코드는 컴파일러가 정렬을 모르니 `ldrb` 두 번 + 조립 (5 명령). 정의된 동작의 값이다.
- `std::bit_cast` 는 union 과 같은 `vmov` 한 개. C++ 에서 union 의 비활성 멤버 읽기는 UB 이고(GCC 문서는 union 을 통한 type-punning 을 확장으로 허용한다고 적는다), `bit_cast` 는 `constexpr` 이기까지 하다 (하네스가 `constexpr mc::Telemetry t = mc::decode(fr);` 로 확인).
- tidy 메시지의 함정: `narrowing conversion from 'int' to signed type 'int16_t' is implementation-defined` 는 `-std=c++20` 에서도 나왔다. C++20 부터 정수 → 정수 변환은 **모듈러로 정의** 되어 있다([conv.integral], P1236). 메시지는 C++17 기준이다 — MISRA C++:2023 이 C++17 대상이라 규칙 자체는 여전히 유효하지만, "구현 정의라서 위험" 이라는 설명은 C++20 에선 틀리다.

`rpm_to_q3` 는 변환 **전에** 범위를 거른다. 경계를 정확히: 0 쪽 버림 뒤 int16 에 들어가는 q 는 `(-32769, 32768)` 열린 구간이다. 그래서 `4095.875 → 32767`, `-4096 → -32768` 은 legacy 와 같고(하네스가 둘 다 확인), `4096.0` 부터 포화한다.

### Part B — 소유권과 소멸자: rule of 0, 그리고 protected 소멸자의 값

`SampleBuf` 의 결함 셋(불일치 delete, 얕은 복사, 암시적 변환 생성자)은 **한 번에** 사라진다 — 자원을 소유하는 멤버가 `std::array` 면 특수 멤버 함수를 하나도 안 쓰면 된다(rule of 0, C.20). 하네스가 확인하는 것: `std::is_trivially_copyable_v<SampleWindow<8>>`, `!std::is_convertible_v<int, SampleWindow<8>>`, 2000 수열 동안 힙 할당이 정확히 2000회(전부 legacy, 새 코드 0회 — 전역 `operator new` 교체로 계수, CPP-B14).

`Sensor` 의 소멸자 정책은 vtable 로 비교한다:

```text
=================== undefined symbols (M4 object)
         U _ZdlPvj
         U __aeabi_d2f  U __aeabi_dadd  U __aeabi_dmul  U __aeabi_f2d      (이건 Pid 필터 — 연습 1)
=================== vtable relocations (M4 object): HallA (virtual dtor) vs HallB (protected dtor)
RELOCATION RECORDS FOR [.rodata._ZTV5HallA]:
00000008 R_ARM_ABS32              _ZN5HallA4readEv
0000000c R_ARM_ABS32              _ZN16IfaceVirtualDtorD2Ev
00000010 R_ARM_ABS32              _ZN5HallAD0Ev
RELOCATION RECORDS FOR [.rodata._ZTV5HallB]:
00000008 R_ARM_ABS32              _ZN5HallB4readEv
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 HallSensor, 소멸자 정책만 다르다 (thumbv7em -O2 실측)</text>
  <text x="20" y="46" fill="currentColor">public virtual ~Iface()</text>
  <rect x="20" y="56" width="300" height="26" class="f-mut" stroke="currentColor"/>
  <text x="30" y="74" fill="currentColor" font-family="monospace" font-size="12">+0x00 offset-to-top / +0x04 typeinfo</text>
  <rect x="20" y="84" width="300" height="26" class="f-ok" stroke="currentColor"/>
  <text x="30" y="102" fill="currentColor" font-family="monospace" font-size="12">+0x08 HallA::read</text>
  <rect x="20" y="112" width="300" height="26" class="f-warn" stroke="currentColor"/>
  <text x="30" y="130" fill="currentColor" font-family="monospace" font-size="12">+0x0C 소멸자 (D1 자리 → Iface D2)</text>
  <rect x="20" y="140" width="300" height="26" class="f-warn" stroke="currentColor"/>
  <text x="30" y="158" fill="currentColor" font-family="monospace" font-size="12">+0x10 HallA D0 (삭제 소멸자)</text>
  <path d="M320 153 L380 153" stroke="currentColor"/>
  <path d="M372 147 L380 153 L372 159" stroke="currentColor" fill="none"/>
  <rect x="384" y="140" width="150" height="26" class="f-warn" stroke="currentColor"/>
  <text x="392" y="158" fill="currentColor" font-family="monospace" font-size="12">b _ZdlPvj</text>
  <text x="384" y="184" fill="currentColor" font-size="12">operator delete(void*, unsigned)</text>
  <text x="384" y="200" fill="currentColor" font-size="12">힙이 없는 펌웨어에도 링크 요구</text>
  <text x="560" y="46" fill="currentColor">protected ~Iface()</text>
  <rect x="560" y="56" width="170" height="26" class="f-mut" stroke="currentColor"/>
  <text x="568" y="74" fill="currentColor" font-family="monospace" font-size="12">+0x00 / +0x04</text>
  <rect x="560" y="84" width="170" height="26" class="f-ok" stroke="currentColor"/>
  <text x="568" y="102" fill="currentColor" font-family="monospace" font-size="12">+0x08 HallB::read</text>
  <text x="560" y="130" fill="currentColor" font-size="12">끝. delete 경로 없음</text>
  <text x="560" y="148" fill="currentColor" font-size="12">Iface* 로 delete 는</text>
  <text x="560" y="164" fill="currentColor" font-size="12">컴파일 에러 (접근 불가)</text>
  <rect x="10" y="214" width="720" height="30" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="22" y="234" fill="currentColor" font-size="12">절대 delete 되지 않는 펌웨어 객체 → protected 비가상 (C.35). vtable 2칸, _ZdlPvj 의존이 없다.</text>
</svg>
```

가상 소멸자는 vtable 에 완료 객체 소멸자 자리(D1 — 하는 일이 없어 `IfaceVirtualDtor` 의 D2 를 그대로 가리킨다)와 **삭제 소멸자(D0)** 를 넣고, D0 는 `operator delete` 를 부른다 → 힙을 안 쓰는 펌웨어에도 `_ZdlPvj` 가 정의되어 있어야 링크된다 (런타임 심볼 추적은 CPP-A03, vtable 회계는 CPP-M06 이 주인). protected 비가상이면 엔트리가 `read` 하나뿐이고, 기반 포인터 `delete` 는 **컴파일 에러** 가 된다 — 하네스가 `requires(T* p) { delete p; }` 로 `can_delete<legacy::Sensor>` 는 참, `can_delete<mc::Sensor>` 는 거짓임을 확인한다. 기반 클래스의 복사·이동은 `= delete` (C.67, slicing 방지) — 그래서 `HallSensor` 도 복사 불가가 되고, 그것도 트레이트로 확인한다.

`dynamic_cast<HallSensor*>` 의 대체는 다운캐스트를 다른 방식으로 하는 게 아니라 **안 하는 것** 이다: 가상 `calibrate()` (기본 no-op). "타입을 물어보고 분기" 는 대개 인터페이스에 동작이 빠졌다는 신호다.

**Pid** 는 NSDMI 로 미초기화를 없애고(새 객체는 0 상태 — legacy 는 `reset()` 을 잊은 경로에서 쓰레기 적분), 필터 줄은 double 을 **명시적으로** 유지했다. 2만 스텝 비트 동등이 그 증거다. `0.9F` 로 바꾸면 M4F 에서 소프트 double 호출 6개가 FPU 명령 5개로 줄지만 결과 비트가 바뀐다 → D 목록에 올려야 할 계약 변경 (연습 1).

### Part C — 재귀 · goto · 가변 인자 · 전역

- 재귀 `lut_find` → 같은 `mid` 순서의 루프. 표가 16칸이라 깊이는 ≤ 5 지만, M:8.2.10 은 "지금 얕다" 가 아니라 **스택 상한을 도구가 합산할 수 있는가** 의 문제다 (숫자는 C-A10 Part C 의 `-fstack-usage` 실측). `(lo + hi) / 2` 대신 `lo + (hi - lo) / 2` 는 같은 값이지만 큰 표에서 넘치지 않는다. 표준 대안은 `std::lower_bound` (헤더 전용, 힙 없음).
- `goto again` → `while (!ok)` + `break`. 같은 probe 횟수를 13×13 조합으로 확인. 전역 `g_fault_count`/`g_log` → 인자로 주입 (M:6.7.2). probe 는 템플릿 매개변수로 받아 테스트에서 람다를 넣는다 (테스트 심 설계는 CPP-A08).
- `vsnprintf` 로거 → `Log<N>`: 문자열과 `int32_t` 오버로드만. 타입이 맞지 않는 인자는 **컴파일이 안 된다** (가변 인자의 8.2.11 문제가 사라짐). `INT32_MIN` 처리: `-v` 는 UB 이므로 `static_cast<uint32_t>(v)` 후 `0U - mag`. 이 함정은 동등성 테스트가 **못 잡는다** — 아래 "뮤턴트" 참고.

### NOLINT 정책과 리팩터 후 재측정

| 정책에서 끈 체크 | 이유 |
|---|---|
| `readability-identifier-length` | `i`, `v`, `lo/hi` 는 관례. 소음 |
| `modernize-use-trailing-return-type` | 스타일 취향 |
| `misc-use-internal-linkage` | 단일 .cpp 레슨의 인공물. 같은 코드를 헤더(`mc100.hpp`)로 옮겨 include 하면 0개 (실측) |
| `cppcoreguidelines-pro-bounds-avoid-unchecked-container-access`, `-constant-array-index` | `gsl::at`/`.at()` 을 요구하는데 `-fno-exceptions` 에서 libc++ `.at()` 의 범위 초과는 `abort()`(실측 exit 134). 대신 인덱스를 구조로 보장하고, 테스트 빌드는 `-D_LIBCPP_HARDENING_MODE=_LIBCPP_HARDENING_MODE_FAST` 로 `operator[]` 범위 초과를 `brk #1` 트랩으로 (실측 exit 133) |
| `readability-trailing-comma` | clang-format 이 할 일 |

```text
$ bash tidy.sh policy
== legacy_mc100.cpp : 127 warnings
== fixed_mc100.cpp : 0 warnings
== NOLINT lines in fixed_mc100.cpp
42:    // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) DEV-1
291:// NOLINTNEXTLINE(bugprone-easily-swappable-parameters) DEV-2: unsigned index vs signed rpm, both range-checked
```

과제 체크 세트 그대로면 fixed 는 83개인데, 전부 위 표의 6종이다 (`tidy.sh counts`: internal-linkage 23, identifier-length 21, trailing-return 21, unchecked-container-access 11, constant-array-index 5, trailing-comma 2). 억제 규칙: ① 체크 이름을 적는다(글롭·빈 괄호 금지) ② 이탈 번호 `DEV-n` 을 적고 deviation 기록에 근거·승인자를 남긴다 ③ `NOLINTBEGIN/END` 범위 억제 금지 ④ 억제 수를 CI 가 센다. 연습 4 가 이 규칙을 코드로 검사한다.

**DEV-1 의 대안 — 링커가 놓는 레지스터 구조체.** `reinterpret_cast` 이탈을 아예 없애는 방법: C++ 에는 `extern PwmRegs PWM1;` 선언만 두고, 주소는 링커 스크립트(여기선 `--defsym=PWM1=0x40010000`)가 준다. Lima VM 의 arm-none-eabi-g++ 14.2.1 실측:

```text
$ bash tidy.sh vm
40010000 A PWM1
00008000 <_Z11ccr0_linkerm>:                 0000800c <_Z9ccr0_castm>:
    8000:	ldr	r3, [pc, #4]                     800c:	ldr	r3, [pc, #4]
    8002:	str	r0, [r3, #52]	@ 0x34           800e:	str	r0, [r3, #52]	@ 0x34
    8004:	bx	lr                               8010:	bx	lr
    8008:	.word	0x40010000                      8014:	.word	0x40010000
```

(옆으로 놓은 것만 편집.) **같은 명령**이다. 대가: 레지스터 맵이 링커 스크립트와 C++ 두 곳에 나뉘고, 호스트 테스트에선 그 심볼을 정의한 객체를 링크해야 한다. CMSIS 식 `#define PWM1 ((PwmRegs*)0x40010000UL)` 은 C 캐스트 매크로라 8.2.2 + 8.2.5 를 동시에 어긴다. 어느 쪽이든 원칙은 **이탈을 한 곳에 모으는 것** — 레지스터 접근 타입 설계는 CPP-M01 이 주인이다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 37/37 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_san && ./sol_san   # 37/37, 보고 없음
```

**하네스가 정말 잡는가 — 뮤턴트 5개를 사본에 심어 본 결과:**

| 심은 버그 | 결과 |
|---|---|
| `lut_find` 의 `<` → `<=` (upper bound) | FAIL 2 (전수 + 무작위 표) |
| Pid 의 `kAlpha` 를 `0.9F` float 로 | FAIL 1 (비트 동등) |
| `SampleWindow::clear` 가 legacy 처럼 `count_` 만 0 | FAIL 2 (D5, 센서 보정) |
| duty 의 음수 검사 제거 | FAIL 1 (D1) |
| `Log::put` 에서 `static_cast<uint32_t>(-v)` (INT32_MIN 에서 UB) | **37/37 통과** — UBSan 빌드에서만 `negation of -2147483648 cannot be represented` |

마지막 줄이 이 레슨의 축소판이다: 동등성 테스트는 **결과 값** 을 보고, UB 는 "우연히 맞는 값" 을 낼 수 있다. 그래서 sanitizer 빌드가 하네스와 짝이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `reinterpret_cast` 경고를 C 캐스트로 바꿔 "조용히" | C 캐스트는 `reinterpret_cast` + `const_cast` 를 한 번에 할 수 있다 (48 줄이 const 를 지웠다). 경고만 다른 이름으로 바뀐다 | 바이트 조립 / `bit_cast`, MMIO 는 DEV 한 곳 |
| `std::memcpy` 로 퍼닝을 고치고 끝 | C++20 에선 `bit_cast` 가 더 낫고(constexpr), MISRA C++:2023 은 24.5.2 로 `memcpy` 자체를 금지한다 | `std::bit_cast`, C++17 이면 이탈 1곳 |
| 비가상 소멸자 경고에 `virtual ~Base()` 를 기계적으로 | 힙 없는 펌웨어에 삭제 소멸자 → `_ZdlPvj` 의존이 생긴다 (실측) | `protected: ~Sensor() = default;` + 복사 `= delete` |
| `SampleBuf` 에 복사 생성자를 손으로 써서 rule of 3 만족 | 여전히 `new[]`, 실패 경로, 단편화. 규칙은 만족했는데 21.6.1 은 그대로 | rule of 0: `std::array` 멤버 |
| 미초기화 경고에 생성자 초기화 목록만 추가 | 생성자를 추가할 때마다 또 빠뜨린다 | NSDMI `float integ_{0.0F};` (C.48) |
| `0.9` → `0.9F` 를 "double-promotion 경고 수정" 으로 리팩터 커밋에 | 결과 비트가 바뀐다 (연습 1: 2만 중 10670) | 리팩터에선 double 명시 유지, float 화는 D 항목으로 따로 |
| `try/catch` 를 지우고 `throw` 자리를 `abort()` 로 | 오류가 "프로그램 종료" 로 바뀐다. 호출자는 실패를 처리할 기회를 잃는다 | `[[nodiscard]] Status` (CPP-M11) |
| `// NOLINT` 를 줄 끝에 붙여 CI 통과 | 무엇을 왜 억제했는지 기록이 없다 → 감사 불가 | `NOLINTNEXTLINE(<check>) DEV-n` + deviation 기록 |
| tidy 0 개 = MISRA 준수라고 보고 | clang-tidy 에는 MISRA 모듈이 없고, 결함 10개 중 6개를 결함으로 못 봤다(§5) | 인증 검사기 + sanitizer + 동등성 테스트 + 리뷰 |

## 8. 면접 Q&A

<details>
<summary>Q1. MISRA C++:2023, AUTOSAR C++14, C++ Core Guidelines, clang-tidy 는 어떤 관계인가요? clang-tidy 를 통과하면 MISRA 준수인가요?</summary>

MISRA C++:2023 은 MISRA C++:2008 의 후속이고 AUTOSAR C++14 지침을 흡수했다(전부는 아님). 대상은 C++17 이다(규칙 4.1.1). C++ Core Guidelines 는 별개의 공개 지침이고, clang-tidy 의 `cppcoreguidelines-*` 는 그것을, `cert-*` 는 SEI CERT 를 구현한다. MISRA 모듈은 없다. 그래서 clang-tidy 는 "겹치는 부분의 1차 그물" 이지 준수 증명이 아니다. 이 레슨에서 진짜 결함 10개 중 6개(unsigned 축소, `new[]`/`delete`, motor 미검사·음수 rpm, 로그 넘침, float 를 `%d` 로, clear 로직)는 tidy 가 결함으로 보지 못했다 — 해당 줄에 경고가 아예 없거나 "가변 인자 쓰지 마라" 같은 일반 규칙 경고뿐이었다. 예외·RTTI 사용 자체에도 경고가 0개였다. 준수 주장은 인증 검사기 결과 + deviation 기록 + 재분류 계획으로 한다. 꼬리: "그럼 tidy 는 왜 돌리나?" → 빠르고 무료라 모든 커밋에 돌릴 수 있고, 캐스트·union·재귀·goto 처럼 규칙과 일대일인 것은 확실히 잡는다.

</details>

<details>
<summary>Q2. reinterpret_cast 가 금지인데 메모리 맵 레지스터는 어떻게 접근하나요?</summary>

세 가지. ① 이탈 1곳: `mmio_block()` 한 함수에만 `reinterpret_cast` + `NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast,performance-no-int-to-ptr) DEV-1`, 나머지 코드는 주입된 `RegBlock` 만 쓴다 — 호스트 테스트가 모의 배열을 넣을 수 있는 덤도 있다. ② 링커가 놓는 객체: `extern PwmRegs PWM1;` + 링커 스크립트에 주소. 캐스트가 없고, arm-none-eabi-g++ 14.2.1 에서 캐스트 버전과 **같은 3 명령**(`ldr r3,=0x40010000; str r0,[r3,#52]; bx lr`)을 실측했다. ③ 벤더 헤더(CMSIS)를 시스템 헤더로 취급해 검사에서 빼고 프로젝트 차원의 deviation 으로 처리. `reinterpret_cast` 는 상수 표현식에서 쓸 수 없으므로 주소를 정수 NTTP 로 들고 접근 시점에 바꾸는 설계(CPP-M01)도 있다. 꼬리: "구조체 레이아웃은 누가 보장하나?" → `static_assert(offsetof(PwmRegs, ccr) == 0x34)` (연습 2).

</details>

<details>
<summary>Q3. union 으로 float 비트를 꺼내는 코드, 뭐가 문제고 무엇으로 바꾸나요? C++17 프로젝트라면요?</summary>

C++ 에서 union 은 한 번에 한 멤버만 활성이고, 다른 멤버를 읽는 것은 수명 밖 객체 접근이라 UB 다. GCC 문서는 union 을 통한 type-punning 을 확장으로 허용한다고 적지만, 그건 컴파일러 약속이지 언어 약속이 아니다. MISRA C++:2023 은 12.3.1 로 `union` 키워드 자체를 금지한다. C++20 이면 `std::bit_cast<std::uint32_t>(x)` — Cortex-M4F 에서 union 버전과 같은 `vmov r0, s0` 한 개(실측), 게다가 constexpr. C++17 이면 `std::memcpy` 가 정석이지만 24.5.2 가 `memcpy` 를 금지하므로 결국 **이탈 1곳**(래퍼 함수 하나)이 된다. 꼬리: "variant 를 쓰라는 tidy 메시지는?" → `pro-type-union-access` 는 "태그 있는 합" 용도를 말하는 것이고, 비트 재해석에는 맞지 않다.

</details>

<details>
<summary>Q4. 다형 기반 클래스의 소멸자는 public virtual 인가요, protected 비가상인가요?</summary>

C.35: 둘 중 하나. 기반 포인터로 `delete` 할 일이 있으면 public virtual, 없으면 protected 비가상. 펌웨어에서 객체는 보통 정적 저장소에 살고 지워지지 않으므로 protected 비가상이 낫다: vtable 엔트리가 2개 줄고 삭제 소멸자(D0)가 없어 `operator delete(void*, unsigned)` 가 링크에 필요 없다 — thumbv7em 오브젝트에서 virtual 쪽만 `U _ZdlPvj` 가 나왔다(실측). 그리고 잘못 쓰면 런타임 UB 가 아니라 **컴파일 에러** 다. 여기에 기반의 복사·이동을 `= delete` 해서 slicing 을 막고(C.67), 구현은 `final` 로 — 컴파일러가 탈가상화(devirtualize)할 여지도 생긴다. 꼬리: "legacy 는 왜 지금은 안 터지나?" → 아무도 `delete` 하지 않기 때문. 결함이 아니라 **잠재 위험** 으로 분류한 이유다.

</details>

<details>
<summary>Q5. MISRA C++ 는 예외와 RTTI 를 금지하나요?</summary>

확인한 MISRA C++:2023 목록에는 예외를 금지하는 규칙이 없고 18.x 규칙들이 **어떻게 쓰는지** 를 규제한다(포인터 예외 객체 금지, 참조로 잡기, noexcept 함수에서 전파 금지 등). RTTI 도 8.2.9(다형 타입 식에 `typeid` 금지) 정도다. 펌웨어가 `-fno-exceptions -fno-rtti` 를 쓰는 건 MISRA 가 아니라 **프로젝트 정책** 이다: 언와인드 테이블·런타임 크기, 최악 실행 시간 분석, 힙 의존(CPP-B13, CPP-A03). 이 정책의 가장 확실한 검사기는 tidy 가 아니라 컴파일러 플래그다 — legacy 를 그 플래그로 빌드하자 `throw`, `try`, `dynamic_cast` 가 각각 에러가 됐다(실측). tidy 는 이 셋에 아무 경고도 안 냈다. 대체: 오류는 `[[nodiscard]]` 값(CPP-M11), 타입 분기는 가상 함수로.

</details>

<details>
<summary>Q6. double-promotion 경고를 없애려고 0.9 를 0.9F 로 바꿨습니다. 리팩터인가요?</summary>

아니다, 계약 변경이다. `0.9 * d` 는 double 로 계산 후 float 로 반올림하고, `0.9F * d` 는 float 로 계산한다 — 반올림 단계가 다르다. 연습 1 에서 같은 상태·같은 입력으로 한 스텝씩 비교하면 2만 스텝 중 10670 스텝의 결과 비트가 달랐다(최대 차 3.8e-6). 대신 Cortex-M4F(단정밀도 FPU)에서 double 버전은 `__aeabi_f2d` ×2, `__aeabi_dmul` ×2, `__aeabi_dadd`, `__aeabi_d2f` 의 소프트웨어 호출 6개이고, float 버전은 `vmul.f32` ×2 + `vadd.f32` 등 5 명령이다(실측). 보통은 바꾸는 게 맞지만 그건 D 항목으로 따로 리뷰하고, 제어 루프의 안정성·필드 데이터와의 비교는 제어 담당이 승인한다. 리팩터 커밋에서는 double 을 `static_cast` 로 **명시** 해서 경고와 동작을 둘 다 지킨다. 꼬리: "호스트에서 비트 동등이면 타깃에서도?" → IEEE 기본 연산은 정확 반올림이라 같아야 하지만, 컴파일러의 FMA 축약(`-ffp-contract`) 같은 차이가 끼어들 수 있으니 타깃 빌드에서 골든 벡터로 한 번 더 확인한다.

</details>

<details>
<summary>Q7. 정적 분석 억제(NOLINT)는 어떻게 관리하나요?</summary>

세 층. ① 설정: `.clang-tidy` 에 끈 체크와 이유(이 레슨: 이름 길이·trailing return·internal-linkage·unchecked access·trailing comma — 각각 근거가 있다). 도구 버전도 고정한다(hicpp 처럼 체크가 사라진다). ② 줄 단위: `NOLINTNEXTLINE(<정확한 체크>) DEV-n` 만 허용 — 글롭, 빈 괄호, 이유 없는 NOLINT, `NOLINTBEGIN/END` 범위 억제는 CI 가 거부(연습 4 의 `nolint_ok`). ③ 기록: DEV-n 마다 규칙 번호, 이유, 위험, 대체 수단(테스트), 승인자 — MISRA Compliance 의 deviation 기록 형식. fixed 파일의 억제는 2개(DEV-1 MMIO 캐스트, DEV-2 바꿔 넣기 쉬운 매개변수)다. 꼬리: "억제가 늘어나면?" → 수를 지표로 추적하고, 같은 체크가 반복되면 설계(래퍼 타입)로 없앤다.

</details>

<details>
<summary>Q8. clang-tidy 가 못 잡은 것은 무엇이고 어떻게 메우나요?</summary>

이 레슨 실측: `new[]` 를 `delete` 로 해제(tidy·컴파일러 모두 조용, 맥 ASan 은 `alloc_dealloc_mismatch=1` 이어야 보고), unsigned 로의 축소(`-Wimplicit-int-conversion` 만), 한 식 안의 두 레지스터 읽기(C++17 은 `<<` 만 순서화했고 `|` 는 아니다), 생성자 안 가상 호출(`Base` 가 호출됨), `uint16_t*uint16_t` 의 int 오버플로(UBSan 만), float 를 `%d` 로(format 속성이 있어야 `-Wformat`), 보정 뒤 낡은 평균(로직). 메우는 법: 경고 세트(`-Wconversion -Wcast-align -Wcast-qual -Wnon-virtual-dtor`), sanitizer 빌드의 단위 테스트(옵션 포함), old/new 동등성 + 경계값, 그리고 리뷰 체크리스트(레지스터 읽기는 문장마다 하나, 생성자에서 가상 호출 금지, 곱하기 전에 넓히기). 꼬리: "Cppcheck 나 상용 도구는?" → 경로 기반 분석기는 일부를 더 잡지만, 로직 버그(D5)와 "고친 게 비트를 바꿨나" 는 결국 테스트의 몫이다.

</details>

## 9. 추가 연습

`legacy_mc100.cpp` 의 나머지와 이 레슨의 결론을 코드로. 답은 모두 `practice.cpp` 에 있다.

### 연습 1 — 0.9 → 0.9F 는 몇 비트를 바꾸나

같은 상태에서 한 스텝씩 double 상수 필터와 float 상수 필터를 비교해, 다른 스텝 수와 최대 차이를 구하라. 리팩터로 허용할 수 있나?

<details>
<summary>답 보기</summary>

`FiltD::step` (fixed 와 같은 double 명시) vs `FiltF::step` (`0.9F * d + 0.1F * (err - prev)`), 매 스텝 `b.d = a.d` 로 상태를 맞춘다. 실측: `ex1: 10670 / 20000 steps differ, worst |diff| = 3.8147e-06`. 절반 넘게 다르다 → 비트 동등이 아니므로 리팩터가 아니라 D 항목. 크기는 float 몇 ulp 수준이라 제어상 무해할 가능성이 크고, M4F 에서 소프트 double 호출 6개를 FPU 명령 5개로 줄이므로(§5 asm) 바꿀 가치가 있다 — 다만 별도 커밋, 별도 승인, 골든 벡터 갱신과 함께.

</details>

### 연습 2 — set_speed: throw 와 REG 매크로 없이

레지스터 블록을 `PwmRegs` 구조체(오프셋은 `static_assert(offsetof(...))` 로 고정)로 두고 `[[nodiscard]] Status set_speed(PwmRegs&, size_t motor, int32_t rpm)` 를 써라. 유효 도메인(motor 0..1, rpm 0..12000)에서 legacy 식과 같은 CCR, D6/D7 은 쓰기 없이 오류.

<details>
<summary>답 보기</summary>

`motor >= pwm.ccr.size()` → `BadMotor`, `rpm < 0 || rpm > 12000` → `BadRpm`, 아니면 `uint64_t` 중간값으로 `rpm * arr / 12000`. 24002 조합에서 legacy 식(`rpm * arr / 12000`, int×uint32 → uint32)과 같다. legacy 는 `rpm = -1, arr = 1000` 에서 **357913** 을 썼다 — 음수 rpm 이 일반 산술 변환으로 4294967295 가 된 결과(M:7.0.5). `arr = 0xFFFFFFFF` 에선 legacy 의 32비트 곱이 감싸지만 새 코드는 정확하다(이것도 차이지만 16비트 타이머에선 일어나지 않는 도메인 — 문서에 적는다). 예외 대신 값을 돌려주므로 호출자가 `g_fault_count++` 를 할지 정한다.

</details>

### 연습 3 — uint16_t 곱의 승격

`area(uint16_t w, uint16_t h)` 를 UB 없이 써라. `0xFFFF * 0xFFFF` 는?

<details>
<summary>답 보기</summary>

`uint16_t` 는 int 로 승격되므로 `w * h` 는 **int 곱** 이고 65535×65535 는 int 를 넘는다 → UB (UBSan: `signed integer overflow: 65535 * 65535`). `-O2` 결과가 우연히 0xFFFE0001 이어도 보장이 아니다. 답: `static_cast<uint32_t>(w) * static_cast<uint32_t>(h)`. 하네스는 10만 쌍을 uint64 기준값과 비교하고, `static_assert(area(0xFFFF, 0xFFFF) == 0xFFFE0001u)` 로 상수 평가에서도 확인한다 — 상수 평가 중의 UB 는 컴파일 에러라서 **constexpr 테스트는 UB 검출기** 이기도 하다 ([expr.const]).

</details>

### 연습 4 — NOLINT 위생 검사기

`constexpr bool nolint_ok(std::string_view line)`: NOLINT 가 없으면 참. 있으면 괄호 안 체크 이름(글롭 `*` 금지) + 뒤에 `DEV-<숫자>` 가 있어야 참, `NOLINTBEGIN/END` 는 거짓. `fixed_mc100.cpp` 의 실제 두 줄로 시험하라.

<details>
<summary>답 보기</summary>

`find("NOLINT")` → 접미사 `NEXTLINE` 건너뛰기 → `(` … `)` 사이가 비어 있지 않고 `*` 없음 → 닫는 괄호 뒤에 `DEV-` + 숫자. 실제 두 줄(DEV-1, DEV-2)은 통과, `// NOLINT`, 이유 없는 `NOLINT(check)`, `NOLINTNEXTLINE(cppcoreguidelines-*)`, `NOLINTBEGIN`, `NOLINT()` 는 거부. CI 에서는 `grep -n NOLINT` 결과를 이 함수(또는 같은 정규식)로 걸러 실패시키고, DEV 번호가 deviation 기록 파일에 있는지도 확인한다. 이 파일 자체에도 `NOLINT(bugprone-use-after-move) DEV-9` 가 하나 있다 — moved-from 상태를 **일부러** 검사하는 테스트 줄이다.

</details>

### 연습 5 — rule of 5 가 맞는 경우: 이동 전용 DMA 채널

8채널 비트맵에서 채널을 잡는 `DmaChannel` 핸들: 복사 금지, `noexcept` 이동, moved-from 은 무효, 소멸자가 정확히 한 번 반납, **자기 이동 대입** 에서도 자원을 잃지 않을 것.

<details>
<summary>답 보기</summary>

rule of 0 은 "자원을 직접 소유하지 않을 때" 의 규칙이고, 하드웨어 자원 핸들처럼 **직접** 소유하면 다섯 개를 모두 정한다(C.21): 복사 둘은 `= delete`, 이동 생성은 `std::exchange(o.ch_, kNone)`, 이동 대입은 `if (this != &o) { release(); ch_ = std::exchange(o.ch_, kNone); }`, 소멸자는 `release()`. 자기 대입 검사가 없으면 `release()` 가 자기 채널을 반납한 뒤 `kNone` 을 가져와 핸들이 무효가 된다 — MISRA C++:2023 Dir 15.8.1(사용자 정의 복사·이동 대입은 자기 대입을 처리)이 정확히 이것이다. 하네스: 트레이트 4개, 반납 횟수 2, 8개 다 잡은 뒤 `claim()` 은 무효. 소멸자에서 `noexcept` 가 기본이고 이동이 `noexcept` 라야 컨테이너가 이동을 쓴다(CPP-M09).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 24/24 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o pr_san && ./pr_san   # 24/24, 보고 없음
```

## 10. 핵심 정리

- clang-tidy 184개 ≠ 결함 184개. 같은 캐스트에 체크 둘, 소음 57개(이름 길이·trailing return·internal linkage·…). **진짜 결함 10개** 중 6개는 tidy 가 결함으로 보지 못했다 → 컴파일러 경고(`-Wconversion` 계열, `-Wcast-align`), sanitizer(옵션 포함), 동등성 테스트, 리뷰가 각각 나눠 잡는다.
- MISRA C++:2023 = C++:2008 갱신 + AUTOSAR C++14 흡수, 대상 C++17(4.1.1). clang-tidy 에 MISRA 모듈은 없고, LLVM 23 에는 hicpp 모듈도 없다(실측). C++20 기능을 쓰면 4.1.1 이탈이다.
- 고친 코드의 비용은 실측으로: M4 에서 바이트 조립 = `ldrh` 1개(공짜), M0+ 에선 legacy 캐스트가 HardFault. `bit_cast` = union 과 같은 `vmov`. protected 비가상 소멸자 = vtable 2칸 + `_ZdlPvj` 의존 제거.
- 소유권은 rule of 0 (`std::array` 멤버)이 기본, 하드웨어 자원 핸들만 rule of 5 + 자기 대입 처리. 다형 기반은 복사·이동 `= delete`, 구현은 `final`, 다운캐스트 대신 가상 함수.
- 예외·RTTI 는 MISRA 가 금지한 게 아니라 프로젝트 정책 — 검사기는 `-fno-exceptions -fno-rtti` 플래그 자체.
- 리팩터 = 동등성 도메인에서 old == new (전수/고정 시드) + D 목록. "경고 수정" 도 비트를 바꾼다(0.9 → 0.9F). UB 는 우연히 맞는 값을 내므로 sanitizer 빌드가 하네스와 짝이다.
- 억제는 `NOLINTNEXTLINE(<check>) DEV-n` 만. `reinterpret_cast` 는 MMIO 한 곳(또는 링커가 놓는 구조체 — 같은 3 명령).

## 참고자료

- MISRA C++:2023 규칙 번호·제목 (공개 목록): MathWorks Polyspace "MISRA C++:2023 Rules and Directives" — <https://www.mathworks.com/help/bugfinder/misra-cpp-2023-rules-and-directives.html> · MISRA — <https://misra.org.uk/> (규칙 원문은 유료, 이 레슨은 번호·제목만)
- MISRA C++:2023 과 AUTOSAR C++14 통합 배경: Perforce "MISRA C++:2023 intro" — <https://www.perforce.com/blog/qac/misra-cpp-2023-intro>, Embedded Computing Design "The AUTOSAR C++ and MISRA C++ Integration" — <https://embeddedcomputing.com/home-page/the-autosar-c-and-misra-c-integration-and-adaptive-development-so-good-or-so-what>
- C++ Core Guidelines: C.20/C.21 (rule of 0/5), C.35 (기반 소멸자), C.48 (멤버 초기화), C.67 (slicing), ES.75 — <https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines>
- clang-tidy 체크 목록 — <https://clang.llvm.org/extra/clang-tidy/checks/list.html> · hicpp 모듈 제거 커밋 (#196870) — <https://llvm.googlesource.com/llvm-project/+/d1b0c854354cb9bc0033b126fa1f71b2a6c721b2%5E%21/> · 관련 메일링 리스트 (PR #194377) — <https://lists.llvm.org/pipermail/llvm-branch-commits/2026-April/091754.html>
- C++ 표준 (N4861 등): [conv.integral], [conv.fpint], [expr.shift], [intro.execution], [basic.lval], [basic.life], [class.union], [class.dtor], [expr.const] — <https://eel.is/c++draft/>
- libc++ Hardening Modes — <https://libcxx.llvm.org/Hardening.html> · UBSan / ASan (`alloc_dealloc_mismatch`) — <https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html>, <https://github.com/google/sanitizers/wiki/AddressSanitizerFlags>
- Arm, *ARMv7-M Architecture Reference Manual* (비정렬 접근, A3.2) · *ARMv6-M Architecture Reference Manual* — <https://developer.arm.com/documentation>
- SEI CERT C++ (DCL50-CPP 가변 함수, EXP 등) — <https://wiki.sei.cmu.edu/confluence/display/cplusplus>
- 관련 레슨: C-A10 (C 판 레거시 리팩터·동등성 패턴, `-fstack-usage`), CPP-M01 (Reg 접근 권한), CPP-M06 (vtable 회계), CPP-A03 (`__cxa_*`/`_ZdlPv` 런타임 의존), CPP-B13 (-fno-exceptions/-fno-rtti), CPP-B14 (operator new 금지), CPP-M11 (Result), CPP-M09 (noexcept 이동), CPP-A08 (테스트 심)
