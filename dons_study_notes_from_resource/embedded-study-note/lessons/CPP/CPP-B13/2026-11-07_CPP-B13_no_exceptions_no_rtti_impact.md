---
id: CPP-B13
time: 50분
updated: 2026-11-07
---
# CPP-B13 -fno-exceptions / -fno-rtti 빌드의 실제 영향

## 학습 목표

- `-fno-exceptions` 가 없애는 것 세 가지 — **정리용 landing pad 코드**, **예외 테이블**(`__gcc_except_tab` / `.ARM.extab`), **런타임 의존**(`__cxa_throw`, personality, unwinder) — 를 실제 숫자로 말할 수 있다.
- `-fno-rtti` 가 없애는 것(`typeinfo` 객체, `__dynamic_cast`)과 **없애지 못하는 것**(vtable 의 typeinfo 슬롯, `throw` 되는 타입의 typeinfo)을 구분한다.
- `throw` / `try` / `dynamic_cast` / `typeid` 가 플래그 아래에서 내는 **실제 컴파일 에러**를 알고, upcast·`dynamic_cast<void*>` 는 여전히 된다는 것까지 설명한다.
- 예외 대신 **에러 코드 / `Result<T,E>` / `std::optional` / C++23 `std::expected`** 로, `dynamic_cast` 대신 **Kind 태그 + `classof` + `static_cast`** (LLVM 방식)로 바꿔 쓸 수 있다.
- Mach-O(`__unwind_info`)와 ELF/ARM(`.ARM.exidx`, `-funwind-tables`)의 차이를 설명한다.

## 1. 문제

펌웨어 C++ 프로젝트의 빌드 플래그 첫 줄에는 거의 항상 `-fno-exceptions -fno-rtti` 가 있다. 면접관은 "왜 끄나요? 끄면 뭐가 줄고 뭐가 깨지나요? 그럼 에러는 어떻게 전달하나요?" 를 꼬리질문으로 이어 묻는다 — 감(感)이 아니라 **숫자와 대안 코드**로 답해야 한다.

두 플래그는 표준 C++의 일부가 아니라 **컴파일러 방언(dialect)** 이다. 표준에는 "예외 없는 C++" 이 없다. 그래서 끄면 언어의 일부 문법이 아예 컴파일 에러가 되고, 표준 라이브러리 중 "던지는" 부분은 `abort` 로 바뀐다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">두 플래그가 바이너리에서 지우는 것</text>
  <rect x="10" y="38" width="350" height="250" rx="8" fill="none" stroke="currentColor"/>
  <rect x="380" y="38" width="350" height="250" rx="8" fill="none" stroke="currentColor"/>
  <text x="24" y="62" fill="currentColor" font-weight="bold">-fno-exceptions</text>
  <text x="394" y="62" fill="currentColor" font-weight="bold">-fno-rtti</text>
  <rect x="24" y="74" width="322" height="44" class="f-acc"/>
  <text x="34" y="92" fill="currentColor">landing pad: 소멸자 호출 + _Unwind_Resume</text>
  <text x="34" y="110" fill="currentColor">       + throw 경로 (__cxa_allocate_exception)</text>
  <rect x="24" y="126" width="322" height="44" class="f-acc"/>
  <text x="34" y="144" fill="currentColor">LSDA 표: __gcc_except_tab / .ARM.extab</text>
  <text x="34" y="162" fill="currentColor">(어느 구간에서 어떤 정리·catch 로 가나)</text>
  <rect x="24" y="178" width="322" height="44" class="f-warn"/>
  <text x="34" y="196" fill="currentColor">언와인드 표: __unwind_info / .ARM.exidx</text>
  <text x="34" y="214" fill="currentColor">(ARM clang 은 CANTUNWIND 엔트리로 남김)</text>
  <rect x="24" y="230" width="322" height="44" class="f-mut"/>
  <text x="34" y="248" fill="currentColor">런타임: __cxa_throw, __gxx_personality_v0,</text>
  <text x="34" y="266" fill="currentColor">_Unwind_* (libsupc++/libgcc 링크 비용)</text>
  <rect x="394" y="74" width="322" height="44" class="f-acc"/>
  <text x="404" y="92" fill="currentColor">typeinfo 객체 _ZTI* + 이름 문자열 _ZTS*</text>
  <text x="404" y="110" fill="currentColor">(다형 클래스마다 1개씩)</text>
  <rect x="394" y="126" width="322" height="44" class="f-mut"/>
  <text x="404" y="144" fill="currentColor">런타임: __dynamic_cast,</text>
  <text x="404" y="162" fill="currentColor">__class_type_info 계열 vtable</text>
  <rect x="394" y="178" width="322" height="44" class="f-ok"/>
  <text x="404" y="196" fill="currentColor">안 지워짐: vtable 의 typeinfo 슬롯 (값만 0)</text>
  <text x="404" y="214" fill="currentColor">— ABI 레이아웃은 고정</text>
  <rect x="394" y="230" width="322" height="44" class="f-ok"/>
  <text x="404" y="248" fill="currentColor">안 지워짐: throw 되는 타입의 typeinfo</text>
  <text x="404" y="266" fill="currentColor">(catch 매칭에 필요 — 예외가 켜져 있으면)</text>
</svg>
```

### Part A — 예외 없는 에러 전달: `Result<T, E>`

C의 "에러 코드 반환 + out 파라미터" 를 타입 하나로 묶는다. `Result<T, E=Err>` 의 `value_or()` 를 채우고, ODR 코드(0..7)를 Hz 로 바꾸는 `odr_from_code()` 를 쓴다 (범위 밖 → `Err::Range`). `value()` 를 에러 상태에서 부르면 던질 수 없으니 **계약 위반 훅**(`on_contract_violation`)을 부른다. `sizeof(Result<uint16_t>) == 4` (레지스터 하나) 를 `static_assert` 로 고정한다.

### Part B — `dynamic_cast` 대체: Kind 태그 + `classof`

`Sensor` ← `Imu` ← {`Bmi270`, `Lsm6dso`}, `Sensor` ← `Bmp390` 계층에서, 생성 시 저장한 `Kind` 로 판별하는 `isa<To>(p)` / `dyn_cast<To>(p)` 를 만든다. 중간 클래스 `Imu` 는 kind 의 **범위**로 판별해야 하고, `nullptr` 와 `const` 포인터도 처리해야 한다. 상속 관계가 없는 타입으로 캐스트하면 **컴파일 에러**(concept 으로 검증).

### Part C — mock I2C IMU 드라이버의 에러 전파

가짜 버스(`MockI2c`: 레지스터 배열 + NACK 주입)로 `imu_probe`(WHO_AM_I 확인), `imu_configure`(ODR 설정 — 검증 실패 시 **버스에 한 번도 쓰지 않아야** 함), `imu_read_x`(DRDY 확인 후 16비트 조립)를 쓴다. 예외였다면 `throw` 한 줄로 끝났을 일을, 각 단계에서 에러를 **명시적으로 위로 넘긴다**.

## 2. Problem (English)

> **Error handling and type queries in a `-fno-exceptions -fno-rtti` firmware build.**
>
> 1. Implement `Result<T, E>` with `ok(T)`, `fail(E)`, `has_value()`, `error()`, `value()` and `value_or(T)`. Calling `value()` on an error must not throw; it calls a contract-violation hook. Then implement `Result<uint16_t> odr_from_code(uint8_t code)` mapping 0..7 → {25, 50, …, 3200} Hz, and `Err::Range` otherwise. `sizeof(Result<uint16_t>)` must be 4.
> 2. Without RTTI, implement `bool isa<To>(const From*)` and `To* dyn_cast<To>(From*)` for the hierarchy `Sensor → Imu → {Bmi270, Lsm6dso}`, `Sensor → Bmp390`, using a `Kind` tag stored in `Sensor` and a static `classof(const Sensor*)` per class. Must handle `nullptr`, `const`, and an intermediate class (`Imu`). Casting to an unrelated type must fail to compile.
> 3. Using a mock I2C bus, write `imu_probe` (WHO_AM_I must be `0x24`, else `BadId`; bus NACK → `Timeout`), `imu_configure(bus, odr_code)` (validate first — on invalid code, zero bus writes; otherwise update only the low nibble of `ACC_CONF`), and `imu_read_x` (`NotReady` unless `STATUS` bit 7 is set; then assemble a little-endian `int16_t`).
>
> Follow-ups: what exactly do the two flags remove from the binary? What still compiles? What happens when library code wants to throw?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 맥 SDK 27.2 링크 문제 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# build: exceptions=off rtti=off ... 6/23 passed — TODO 8개를 채워 23/23 을 만든다
```

관찰용 파일 (이 레슨 폴더):

| 파일 | 하는 일 | 실행 |
|---|---|---|
| `demo_flags.cpp` + `build.sh` | 같은 프로그램(가상 계층 + 던질 수 있는 함수 + `dynamic_cast`/`typeid`)을 4조합으로 빌드해 섹션 표 출력 | `sh build.sh arm` |
| `demo_errors.cpp` | `throw`/`try`/`dynamic_cast`/`typeid` 의 실제 에러 | `c++ -std=c++20 -fno-exceptions -fno-rtti -DSHOW_ERROR=1 -c demo_errors.cpp -o /dev/null` (1~6) |
| `demo_expected.cpp` | C++23 `std::expected` 가 `-fno-exceptions` 에서 되는지, `.value()` 실패 시 무엇이 일어나는지 | `c++ -std=c++23 -O2 -fno-exceptions -fno-rtti demo_expected.cpp -o /tmp/cpp_b13_exp && /tmp/cpp_b13_exp` (`-DSHOW_ABORT=1/2`) |

## 4. 힌트

<details>
<summary>힌트 1 — value_or 와 odr_from_code</summary>

`value_or` 는 삼항 연산자 한 줄. `odr_from_code` 는 **먼저 범위 검사** (`code >= 8` → `fail(Err::Range)`), 통과하면 표 `kOdrHz[code]` 를 `ok()` 로 감싼다. 전부 `constexpr` 이라 컴파일타임에도 돈다.

</details>

<details>
<summary>힌트 2 — Imu::classof 는 "같다" 가 아니라 "범위"</summary>

`Kind` 열거를 **계층 순서대로** 배치해 두었다: `Bmi270, Lsm6dso` (IMU 계열) 다음 `Bmp390`. 중간 클래스는 `first <= kind && kind <= last` 로 판별한다 (LLVM 의 `Value::classof` 와 같은 기법).

</details>

<details>
<summary>힌트 3 — dyn_cast = isa + static_cast</summary>

`isa<To>(p)`: `p != nullptr && To::classof(p)`. `To` 가 `const Imu` 일 수 있으니 `std::remove_cv_t<To>::classof`. `dyn_cast`: 맞으면 `static_cast<To*>(p)` — 다운캐스트 `static_cast` 는 **실제 타입이 맞을 때만** 정의된 동작이다 ([expr.static.cast]). 태그 검사가 그 보증을 대신한다.

</details>

<details>
<summary>힌트 4 — 에러 전파는 "early return"</summary>

`auto r = i2c_read(...); if (!r) return r.error();` (또는 `Result<U>::fail(r.error())`) 를 단계마다 반복한다. configure 는 **ODR 검증을 버스 접근보다 먼저** 해야 `writes == 0` 테스트를 통과한다. ACC_CONF 는 읽고 → 하위 4비트만 바꾸고 → 쓰기 (C-B01 의 field_set 참고).

</details>

## 5. 풀이

### 실험 먼저 — 4조합의 실제 숫자 (`sh build.sh arm`)

`demo_flags.cpp` 는 소스가 하나다. 예외/RTTI 가 꺼졌을 때는 `__cpp_exceptions` / `__cpp_rtti` 기능 테스트 매크로(SD-6)로 대체 경로(`abort`, 태그 + `static_cast`)를 고른다 — ETL 같은 실제 라이브러리가 쓰는 방식이다. 이 맥(Apple clang 21, arm64)에서 받은 출력 그대로:

```text
--- macOS arm64 Mach-O, c++ -std=c++20 -O2 (bytes) ---
combo            __text __stubs  exc_tab unw_info eh_frame  DATA_const TI_def   U_rt file_KB
exc-on/rtti-on      524    120       60      128        0         160      4      9     34
exc-on/rtti-off     444    108       60      120        0          96      1      7     33
exc-off/rtti-on     440     72        0        0        0         144      3      3     33
exc-off/rtti-off    360     60        0        0        0          80      0      0     33

--- Cortex-M4 ELF .o: clang++ --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os (bytes) ---
combo                           .text   .exidx   .extab  .rodata TI_bytes TI_def
exc-on/rtti-on                    288       72       80      133       73      4
exc-on/rtti-off                   274       72       80       82       22      1
exc-off/rtti-on                   216       64        0      111       51      3
exc-off/rtti-off                  204       64        0       60        0      0
exc-off/rtti-off/no-unwind        204       64        0       60        0      0
```

(`exc_tab` = `__TEXT,__gcc_except_tab`, `TI_def` = `nm -U | c++filt` 에서 정의된 `typeinfo for` 개수, `U_rt` = `__cxa_*`/`_Unwind_*`/`__gxx_personality_v0`/`__dynamic_cast`/`__class_type_info` 미정의 참조 개수, `TI_bytes` = ELF `.rodata._ZTI*` + `.rodata._ZTS*`.)

| 관찰 | 숫자 | 왜 |
|---|---|---|
| 예외 끄면 코드 감소 | Mach-O `__text` 524→440 (−16%), ARM `.text` 288→216 (−25%) | landing pad(소멸자 호출 + `_Unwind_Resume`)와 `__cxa_allocate_exception`/`__cxa_throw` 경로가 사라짐. try/catch 대신 `abort` |
| LSDA 표 소멸 | `__gcc_except_tab` 60→0, `.ARM.extab` 80→0 | "이 PC 구간에서 예외가 나면 어느 landing pad / catch 로" 를 적은 표 |
| 언와인드 표 | `__unwind_info` 128→0, `.ARM.exidx` 72→64 | Apple clang 은 `-fno-exceptions` 면 compact unwind 를 안 만든다. ARM clang 은 함수마다 8바이트 **EXIDX_CANTUNWIND**(값 `0x1`) 엔트리를 남긴다 (`-fno-unwind-tables` 를 줘도 64 그대로) |
| RTTI 끄면 typeinfo 소멸 | `TI_def` 4→1, Mach-O `__DATA_CONST,__const` 160→96, ARM typeinfo 73→22 B | `_ZTI6Sensor`, `_ZTI3Imu`, `_ZTI4Baro` 와 이름 문자열 `_ZTS*` 가 빠짐 |
| 그런데 1개가 남는다 | `exc-on/rtti-off` 에도 `typeinfo for SensorError` | **throw 되는 타입의 typeinfo 는 catch 매칭에 필요**하므로 `-fno-rtti` 여도 생성된다 |
| 둘 다 끄면 | `__text` 524→360 (−31%), `__const` 160→80 = vtable 2개 × 40 B 만 | 남은 건 순수 가상 호출 기계(vptr + vtable) 뿐 |
| 파일 크기는 거의 같다 | 34 KB → 33 KB | Mach-O 세그먼트는 16 KB 페이지 정렬 — **파일 크기로 비교하면 안 된다**. 섹션 크기(`size -m`)로 본다. 펌웨어는 `.bin`/`size` 의 text+data |

> 이 숫자는 **내 코드 몫만** 이다. 맥에서는 `__cxa_throw`·unwinder 가 `libc++abi.dylib`(공유 라이브러리)에 있어 바이너리에 안 들어오고, ARM 쪽은 `.o` 까지만 만들었다(링크할 libstdc++ 없음). 실제 Cortex-M 링크에서는 libsupc++/libgcc 의 unwinder·personality·`__cxa_*`·terminate 핸들러가 정적으로 붙는다 — 그 크기(보통 수~수십 KB, 툴체인·newlib-nano 여부에 따라 다름)가 예외를 끄는 진짜 이유인데, **이 환경에서는 측정하지 못했다.** `arm-none-eabi-g++` 가 있으면 `-Wl,--print-map` 으로 `unwind-arm.o`, `eh_personality.o` 등이 끌려오는지 확인해 보라.

### Part A — `throw` 대신 `Result<T, E>`

먼저 예외가 **어디서** 비용을 만드는지 asm 으로 보자. `read_checked()` (지역 `BusLock` 소멸자 + `throw`) 를 `c++ -O2 -S` 로 뽑으면 (맥 arm64, 발췌):

```text
; 예외 ON                                   ; 예외 OFF (-fno-exceptions)
  bl   _puts            ; "lock"            bl   _puts
  blr  x8               ; s.read()          blr  x8
  ... b.eq LBB0_3       ; 던질 조건            ... b.eq LBB0_2
  bl   _puts            ; "unlock"          bl   _puts
  ret                                       ret
LBB0_3:                                   LBB0_2:
  bl   ___cxa_allocate_exception  ; malloc!   bl   _abort
  adrp x1, __ZTI11SensorError@PAGE
  bl   ___cxa_throw
LBB0_5:                ; landing pad
  bl   _puts            ; ~BusLock()
  bl   __Unwind_Resume
```

- **정상 경로(happy path)는 두 빌드가 같은 명령어**다. "zero-cost exceptions" 라는 말은 이 뜻 — 비용은 시간이 아니라 **크기**(landing pad + 표 + 런타임)와, throw 시점의 **비결정적 시간**(표 검색, `__cxa_allocate_exception` 의 힙 할당)이다. 하드 실시간 ISR 경로에서는 둘 다 받아들이기 어렵다.
- `-fno-exceptions` 에서 `throw` 는 쓸 수 없으니, 실패를 **반환값으로** 돌려줘야 한다. C 에서는 `int f(uint16_t *out)` 였다. C++ 에서는 값과 에러를 한 타입으로:

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">Result&lt;uint16_t, Err&gt; = 4바이트 → AAPCS 에서 r0 하나로 반환</text>
  <g stroke="currentColor">
    <rect x="40" y="44" width="160" height="34" class="f-mut"/>
    <rect x="200" y="44" width="160" height="34" class="f-warn"/>
    <rect x="360" y="44" width="320" height="34" class="f-acc"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="120" y="66">has_ [31:24]</text><text x="280" y="66">err_ [23:16]</text><text x="520" y="66">val_ [15:0]</text>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="112">odr_from_code(uint8_t):          ; clang --target=thumbv7em -Os</text>
    <text x="40" y="132">  cmp     r0, #7</text>
    <text x="40" y="152">  itt     hi</text>
    <text x="40" y="172">  movhi.w r0, #196608             ; 0x00030000 = has 0, err Range(3)</text>
    <text x="40" y="192">  bxhi    lr</text>
    <text x="40" y="212">  ldrh.w  r0, [r1, r0, lsl #1]   ; kOdrHz[code]</text>
    <text x="40" y="232">  add.w   r0, r0, #16777216      ; | 0x01000000 = has 1 → bx lr</text>
  </g>
</svg>
```

위 asm 은 `Result` 를 평범한 구조체로 만든 축소판을 Cortex-M4 타깃으로 실제 컴파일한 것이다. 값·에러·플래그가 **레지스터 한 개**에 담겨 돌아온다 — C 의 "에러 코드 + out 포인터" 보다 오히려 메모리 접근이 적다. AAPCS 규칙: 4바이트 이하 복합 타입은 `r0` 로 반환, 그보다 크면 호출자가 잡은 메모리로 반환(숨은 포인터). 그래서 `static_assert(sizeof(Result<uint16_t>) == 4)` 를 박아 두는 것이 의미가 있다 (arm64 AAPCS64 는 16바이트까지 `x0/x1`).

| 방법 | 표준 | `-fno-exceptions` | 비용 / 주의 |
|---|---|---|---|
| `int` 에러 코드 + out 포인터 | C | OK | 무시해도 경고 없음, out 포인터 null 검사 필요 |
| `enum class Err` 반환 | C++11 | OK | 타입 안전. 값은 따로 |
| `Result<T,E>` (직접 구현) | C++11~ | OK | 이 레슨. `[[nodiscard]]` 로 무시 방지 |
| `std::optional<T>` | C++17 | OK (`.value()` 는 abort) | "왜 실패" 정보 없음 — 팩토리에 적합 (연습 3) |
| `std::expected<T,E>` | **C++23** | OK — libc++ 확인함 | `.value()` 실패 시 abort. monadic `and_then`/`transform` |

`std::expected` 확인 (`demo_expected.cpp`, `-std=c++23 -fno-exceptions -fno-rtti`, 실제 출력):

```text
__cpp_lib_expected = 202211, __cpp_exceptions undefined
a: has=1 val=0x6A | b: has=0 err=1 | b.value_or=0xFFFF
c = 0x16A, sizeof(expected<u16,SpiErr>) = 8
# -DSHOW_ABORT=1  (에러 상태에서 b.value())
bad_expected_access was thrown in -fno-exceptions mode
Abort trap: 6        (exit 134)
# -DSHOW_ABORT=2  (std::array::at(7))
Abort trap: 6        (exit 134, 메시지 없음)
```

`-std=c++20` 으로는 `error: no template named 'expected' in namespace 'std'` — C++23 전용이다. 이 레슨의 하네스는 C++20 이라 같은 모양을 직접 만든다. 그리고 **라이브러리의 "던지는" API(`at()`, `value()`)는 `-fno-exceptions` 에서 조용히 `abort`** 가 된다 — 펌웨어에서는 `*opt`, `value_or`, 범위 검사 후 `[]` 를 쓴다. 우리 `Result::value()` 가 abort 대신 훅을 부르게 한 이유도 같다 (펌웨어라면 그 훅에서 `__BKPT()` 나 에러 로그 + 리셋).

### Part B — `dynamic_cast` 대신 Kind 태그 + `classof`

`-fno-rtti` 에서 `dynamic_cast` 가 막히는 이유는 vtable 이 가리키는 **typeinfo 객체**가 없기 때문이다. Itanium C++ ABI (ARM 도 이 ABI 의 변형을 씀)의 vtable 레이아웃은 고정이다:

```svg
<svg viewBox="0 0 740 230" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">vtable for Imu (Itanium ABI) — 실제 .s 출력 기준</text>
  <g stroke="currentColor">
    <rect x="30" y="40" width="200" height="30" class="f-mut"/>
    <rect x="30" y="70" width="200" height="30" class="f-warn"/>
    <rect x="30" y="100" width="200" height="30" class="f-acc"/>
    <rect x="30" y="130" width="200" height="30" class="f-acc"/>
    <rect x="30" y="160" width="200" height="30" class="f-acc"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="40" y="60">offset-to-top = 0</text>
    <text x="40" y="90">typeinfo ptr</text>
    <text x="40" y="120">Imu::~Imu() (D1)</text>
    <text x="40" y="150">Imu::~Imu() (D0)</text>
    <text x="40" y="180">Imu::read()</text>
  </g>
  <path d="M250 115 L300 115" stroke="currentColor"/>
  <text x="308" y="119" fill="currentColor">객체의 vptr 은 여기(+16)를 가리킨다</text>
  <text x="300" y="70" fill="currentColor">RTTI on : .quad __ZTI3Imu → typeinfo 객체</text>
  <text x="300" y="88" fill="currentColor">(부모 __ZTI6Sensor 포인터 포함) — __dynamic_cast 가 이 사슬을 걷는다</text>
  <text x="300" y="150" fill="currentColor">RTTI off: .quad 0  — 슬롯은 남고 값만 0</text>
  <text x="300" y="168" fill="currentColor">(레이아웃이 같아야 섞어 링크해도 가상 호출이 맞는다)</text>
  <text x="30" y="218" fill="currentColor">dynamic_cast&lt;void*&gt; 는 offset-to-top 만 쓰므로 -fno-rtti 에서도 컴파일된다 (clang 확인).</text>
</svg>
```

그래서 "다형 클래스면 RTTI 가 꼭 필요하지 않나?" 의 답은: **vtable 에 typeinfo *슬롯*은 항상 있다(ABI). 하지만 가상 호출은 그 슬롯을 읽지 않으므로, `-fno-rtti` 에선 0 으로 채워져도 문제없다.** 가상 함수만 쓴다면 RTTI 는 필요 없다. (주의: RTTI 켠 TU 와 끈 TU 를 섞으면, vtable 을 내보낸 TU — key function 이 정의된 곳 — 가 `-fno-rtti` 일 때 다른 TU 의 `dynamic_cast` 가 0 슬롯을 따라가 깨진다. 프로젝트 전체를 같은 플래그로.)

대체 방법은 **"타입을 묻는 정보" 를 우리가 직접 저장**하는 것이다:

| 방식 | 판별 비용 | 객체 크기 (64비트 / Cortex-M) | 비고 |
|---|---|---|---|
| `dynamic_cast` (RTTI) | `__dynamic_cast` 호출 — typeinfo 사슬 문자열/포인터 비교 | vptr | 클래스마다 typeinfo |
| 멤버 `Kind` + `classof` (이 레슨) | 바이트 비교 1~2회, 인라인 | vptr + 1 B → 16 / 8 B (패딩) | LLVM 방식, 가장 빠름 |
| `virtual Kind kind()` | 가상 호출 1회 | vptr만 → 8 / 4 B | 연습 2 |
| 가상 함수로 설계 (캐스트 자체를 없앰) | 가상 호출 | vptr | 보통 이게 정답 — "다운캐스트가 필요한 설계인가?" |

`Kind` 를 **계층 순서(전위 순회)대로** 나열하면 중간 클래스는 연속 구간이 된다:

| Kind | `Sensor::classof` | `Imu::classof` [Bmi270, Lsm6dso] | `Bmi270::classof` | `Bmp390::classof` |
|---|---|---|---|---|
| `Bmi270` | ✓ | ✓ | ✓ | |
| `Lsm6dso` | ✓ | ✓ | | |
| `Bmp390` | ✓ | | | ✓ |

`dyn_cast` 의 `requires std::is_base_of_v<From, std::remove_cv_t<To>>` 는 이미 주어진 부분인데, 이게 **함수 시그니처의 일부**라서 `DynCastable<int, Sensor>` 같은 requires-식이 `false` 가 된다. 본문 안의 `static_assert` 로는 안 된다 — requires-식은 선언(즉시 문맥)만 보고 본문은 인스턴스화하지 않기 때문이다 ([temp.constr], [expr.prim.req]).

### Part C — 예외 없는 드라이버: 전파는 명시적으로

```text
imu_configure(bus, code)
  ├─ odr_from_code(code)      실패 → return Range       (버스 접근 0회 ← 테스트가 확인)
  ├─ i2c_read(ACC_CONF)       실패 → return Timeout
  ├─ v = (conf & 0xF0) | code   하위 니블만 교체 (RMW)
  └─ i2c_write(ACC_CONF, v)   → None / Timeout
```

예외 버전이라면 `i2c_read` 안에서 `throw I2cTimeout{}` 하고 호출자는 아무것도 안 해도 됐다. 대신 그 "아무것도 안 함" 이 숨은 landing pad 와 표로 바뀌었다. 에러 코드 버전은 **경로가 소스에 다 보인다** — 리뷰·MISRA 류 정적 분석·WCET(최악 실행 시간) 분석에 유리하다. 대가는 반복되는 `if (!r) return r.error();` 이고, 연습 1 의 `and_then` 이 그 반복을 줄인다.

`imu_read_x` 의 조립 `static_cast<int16_t>(static_cast<uint16_t>(hi << 8 | lo))`: `hi << 8` 은 int 로 승격되어 계산되고, `uint16_t` 로 자른 뒤 `int16_t` 로 바꾼다. C++20 부터 정수 변환은 2의 보수로 **정의**된다 (0xFC18 → −1000). C++17 까지는 구현 정의였다.

### 컴파일 에러 — 실제 출력 (`demo_errors.cpp`, clang 21)

```text
SHOW_ERROR=1  error: cannot use 'throw' with exceptions disabled
SHOW_ERROR=2  error: cannot use 'try' with exceptions disabled
SHOW_ERROR=3  error: use of dynamic_cast requires -frtti
SHOW_ERROR=4  error: use of typeid requires -frtti        (typeid 두 번 → 에러 2개)
SHOW_ERROR=5  error: use of typeid requires -frtti        (typeid(int) 같은 정적 타입도)
SHOW_ERROR=6  (에러 없음)  dynamic_cast<void*>(b)
(공통)        dynamic_cast<Base*>(derived) upcast, noexcept 함수 — 플래그와 무관하게 OK
```

예: `demo_errors.cpp:13:13: error: cannot use 'throw' with exceptions disabled` / `   13 |     if (!b) throw DriverError{-1};`. 같은 파일을 플래그 없이 빌드하면 1~5 모두 통과한다. GCC(`arm-none-eabi-g++`)의 해당 메시지는 대략 `exception handling disabled, use '-fexceptions' to enable`, `'dynamic_cast' not permitted with '-fno-rtti'`, `cannot use 'typeid' with '-fno-rtti'` 이다 — **이 맥에 GCC 가 없어 실행해 확인하지 못했다** (기억 기반).

### ELF / Cortex-M 에서는 (`arm-none-eabi-g++` 기준)

| Mach-O (이 맥) | ELF 일반 (x86-64/AArch64 Linux) | ARM EHABI (Cortex-M) | 역할 |
|---|---|---|---|
| `__TEXT,__unwind_info` (compact) | `.eh_frame` + `.eh_frame_hdr` | `.ARM.exidx` (8 B/함수, 정렬된 인덱스) | "이 PC 에서 스택을 한 칸 되감는 법" |
| `__TEXT,__eh_frame` (compact 로 못 적는 함수만) | `.eh_frame` | — | DWARF CFI |
| `__TEXT,__gcc_except_tab` | `.gcc_except_table` | `.ARM.extab` 안 (personality + LSDA) | landing pad / catch 표 |
| `__DATA_CONST,__const` 의 `__ZTI*` | `.rodata` / `.data.rel.ro` 의 `_ZTI*` | `.rodata._ZTI*` | typeinfo |

- `.ARM.exidx` 엔트리 = (함수 오프셋 prel31, 내용). 내용이 `0x1` 이면 **EXIDX_CANTUNWIND** — "여기서는 되감을 수 없음". 위 `exc-off` .o 를 `objdump -s -j .ARM.exidx` 로 보면 `00000000 01000000 38000000 01000000 ...` — 전부 CANTUNWIND 였다. 예외가 이런 프레임을 만나면 unwinder 가 멈추고 `std::terminate` 로 간다.
- `-funwind-tables`: 예외를 안 써도 언와인드 표를 만든다. 쓰는 곳: **백트레이스**(HardFault 핸들러에서 `_Unwind_Backtrace` 로 콜스택 찍기), C 함수가 C++ 예외 경로 중간에 낄 때(C 쪽도 `-funwind-tables`/`-fexceptions` 필요). 맥에서 측정: `-fno-exceptions -funwind-tables` → `__unwind_info` 104 B 생성, `-fasynchronous-unwind-tables` → `__eh_frame` 192 B 추가.
- 예상(미검증): GCC 는 `-fno-exceptions` 이고 `-funwind-tables` 가 없으면 내 코드에 `.ARM.exidx` 를 만들지 않는다 (clang 은 위처럼 CANTUNWIND 를 남김). 하지만 libgcc 의 일부 객체가 exidx 를 가져오므로 링커 스크립트의 `.ARM.exidx : { *(.ARM.exidx*) }` 와 `__exidx_start/__exidx_end` 는 지우지 않는다.
- 라이브러리 함정: **내 코드를 `-fno-exceptions` 로 빌드해도 미리 빌드된 libstdc++ 는 예외를 쓴다.** `operator new` 실패 → `std::bad_alloc` throw → 잡을 곳이 없어 `std::terminate` → `abort`. 힙을 아예 막는 것은 CPP-B14 참고.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution
# build: exceptions=off rtti=off
# 23/23 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined solution.cpp -o sol_asan && ./sol_asan   # 23/23, 경고 0
```

같은 `solution.cpp` 를 플래그만 바꿔 빌드하면 (`size -m`, 실제 측정): 예외 ON 은 `__unwind_info` 96 B 가 붙을 뿐 `__gcc_except_tab` 은 없다 (try/catch·정리할 소멸자가 없으니까). RTTI ON 은 `__DATA_CONST,__const` 가 40 → 104 B — `-O2` 가 가상 호출을 대부분 풀어 vtable 이 `Lsm6dso` 하나만 남았고(40 B), 그 typeinfo 사슬 `Lsm6dso → Imu → Sensor` 3개(24+24+16 = 64 B)가 붙었다 (`nm -U | c++filt` 로 확인). **에러 코드 스타일로 짠 코드는 예외 플래그에 거의 무관**하다는 것 자체가 결론이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `-fno-exceptions` 니까 `new` 는 `nullptr` 을 돌려줄 거라 믿음 | 일반 `new` 는 여전히 `bad_alloc` 을 throw 하려다 terminate. `nullptr` 검사는 죽은 코드 | `new (std::nothrow) T` 또는 힙 금지 (CPP-B14) |
| `opt.value()`, `arr.at(i)` 를 "안전한" 접근이라 생각 | `-fno-exceptions` 에서 실패 시 `abort` (위 실측: exit 134) | `if (opt) *opt`, `value_or`, 범위 검사 후 `[]` |
| 다운캐스트를 태그 검사 없이 `static_cast<Imu*>(s)` | 실제 타입이 다르면 **UB** ([expr.static.cast]) — 컴파일러는 아무 검사도 안 함 | `dyn_cast<Imu>(s)` (태그 검사 + static_cast) |
| `Kind` 열거를 아무 순서로 나열 | 중간 클래스 `classof` 의 범위 비교가 깨짐 | 계층 전위 순서로 나열, `First/Last` 를 주석·별칭으로 고정 |
| `Result` 반환값 무시 `imu_configure(bus, 4);` | 에러가 조용히 사라짐 — 예외라면 최소한 terminate 였다 | 클래스에 `[[nodiscard]]`, `Err` 를 반환하는 함수에도 `[[nodiscard]]` |
| 실패 검증을 하드웨어 쓰기 **뒤에** | 잘못된 값이 레지스터에 먼저 들어간 뒤 에러 반환 | 검증 → 읽기 → 쓰기 순서 (Part C `writes == 0` 테스트) |
| 일부 파일만 `-fno-rtti` | vtable 을 내보낸 TU 가 RTTI 없이 빌드되면 다른 TU 의 `dynamic_cast`/`typeid` 가 0 슬롯을 따라감 | 전 프로젝트 같은 플래그. 라이브러리 경계 확인 |
| 크기 비교를 `ls -l` 로 | Mach-O 16 KB 페이지 정렬 → 34 KB vs 33 KB 로 차이가 묻힘 | `size -m` (맥) / `arm-none-eabi-size` (text+data+bss) |

## 8. 면접 Q&A

<details>
<summary>Q1. 펌웨어에서 왜 -fno-exceptions 를 쓰나요? "zero-cost exception" 이라던데요?</summary>

zero-cost 는 **정상 경로의 실행 시간**이 0 이라는 뜻이다 (위 asm 에서 happy path 명령어가 같았다). 비용은 ① 크기: landing pad, LSDA(`.ARM.extab`), 언와인드 표(`.ARM.exidx`), 그리고 정적으로 링크되는 unwinder·personality·`__cxa_*` 런타임 ② throw 시의 비결정적 시간: 표 검색 + `__cxa_allocate_exception` 의 **힙 할당** ③ ISR·RTOS 경계를 넘는 언와인드는 정의되지 않음. 실측: 같은 소스에서 예외만 꺼도 `__text` −16%(맥), ARM `.text` −25%, LSDA 0.

</details>

<details>
<summary>Q2. -fno-rtti 인데 가상 함수는 왜 계속 동작하나요? vtable 에 typeinfo 가 있지 않나요?</summary>

Itanium ABI vtable 에는 offset-to-top 과 typeinfo 포인터 슬롯이 **항상** 있다. 가상 호출은 vptr 이 가리키는 함수 슬롯만 읽고 typeinfo 슬롯은 안 읽는다. `-fno-rtti` 면 그 슬롯이 0 으로 채워진다 (실제 `.s`: `.quad 0 / .quad 0 / ImuD1 / ImuD0 / read`). 슬롯을 없애지 않는 이유는 레이아웃이 ABI 로 고정되어야 하기 때문. 꼬리질문 "그럼 RTTI 가 꺼져도 남는 typeinfo 는?" → throw 되는 타입(catch 매칭용). 예외도 꺼야 0 개가 된다.

</details>

<details>
<summary>Q3. -fno-rtti 에서 dynamic_cast 는 전부 에러인가요?</summary>

아니다. 런타임 타입 정보가 필요한 경우만 에러(`use of dynamic_cast requires -frtti`): 다운캐스트, 사이드캐스트. **upcast** 는 컴파일타임에 결정되는 변환이라 OK, `dynamic_cast<void*>` 는 vtable 의 offset-to-top 만 써서 clang 에서 OK (직접 확인). `typeid` 는 `typeid(int)` 같은 정적 타입까지 전부 에러.

</details>

<details>
<summary>Q4. RTTI 없이 안전한 다운캐스트는 어떻게 하나요?</summary>

기반 클래스에 `Kind` 태그를 두고 클래스마다 `static bool classof(const Base*)` 를 정의, `dyn_cast<T>(p) = (p && T::classof(p)) ? static_cast<T*>(p) : nullptr`. LLVM 이 `-fno-rtti -fno-exceptions` 로 빌드되면서 쓰는 방식 그대로다. 중간 클래스는 kind 구간으로. 비용은 바이트 비교 1~2회. 대안: `virtual Kind kind()` (객체 크기 ↓, 가상 호출 1회). 꼬리질문 "애초에 다운캐스트가 필요한가?" → 동작을 가상 함수로 옮기는 게 먼저.

</details>

<details>
<summary>Q5. 예외 없이 생성자의 실패는 어떻게 알리나요?</summary>

생성자는 값을 반환할 수 없다. ① 2단계 초기화: 생성자는 실패하지 않는 일만, `Err init()` 별도 ② 정적 팩토리: `static std::optional<Uart> create(...)` / `Result<Uart>` 를 반환하고 생성자는 private ③ `bool is_valid()` 상태 플래그(덜 권장). 펌웨어 드라이버는 보통 ①: 정적 객체로 만들어 두고(`.bss`), `main` 초기에 `init()` 결과를 검사. 연습 3 참고.

</details>

<details>
<summary>Q6. -fno-exceptions 로 빌드했는데 라이브러리가 예외를 던지면?</summary>

헤더 템플릿 코드(libc++ `array::at`, `optional::value`, `expected::value`)는 **내 플래그로 인스턴스화**되므로 throw 대신 `abort` 로 컴파일된다 (실측: `bad_expected_access was thrown in -fno-exceptions mode` 후 abort). 미리 빌드된 라이브러리(`operator new`, libstdc++ .a)는 예외 켜진 채라 실제로 throw → 내 프레임은 landing pad 가 없고(ARM 은 CANTUNWIND) → `std::terminate`. 그래서 펌웨어는 `std::terminate`/`abort` 를 자체 fault 핸들러로 연결하고, 던질 수 있는 API 를 피한다.

</details>

<details>
<summary>Q7. -funwind-tables 는 언제 켜나요? 예외를 안 쓰는데 왜 필요하죠?</summary>

언와인드 표는 예외만의 것이 아니라 **스택 되감기 정보**다. HardFault 핸들러에서 콜스택을 찍거나(`_Unwind_Backtrace`), 프로파일러가 스택을 걸을 때 필요하다. 비용은 함수당 `.ARM.exidx` 8 B (+ 복잡한 프롤로그면 `.ARM.extab`). 디버그 빌드엔 켜고 릴리스에서 끄거나, 프레임 포인터(`-fno-omit-frame-pointer`)로 대신하기도 한다. 맥 실측: `-fno-exceptions` 단독은 `__unwind_info` 0 B, `-funwind-tables` 추가 시 104 B.

</details>

## 9. 추가 연습

### 연습 1 — `and_then` 으로 에러 전파 줄이기

`Result::and_then(f)` 를 구현해 `period_from_code(code) = odr_from_code(code).and_then(period_us)` 가 되게 하라. 앞 단계가 실패하면 뒤 함수는 호출되지 않고 에러가 그대로 나와야 한다.

<details>
<summary>답 보기</summary>

`has_ ? f(val_) : R::fail(err_)` 에서 `R = std::invoke_result_t<F, T>` — f 의 반환 타입(`Result<uint32_t>`)으로 에러를 옮겨 담는다. 반환 타입을 `decltype(f(val_))` 로 쓰면 trailing return 위치에서 아직 선언 안 된 멤버라 에러가 난다 (직접 겪음). C++23 `std::expected::and_then` 과 같은 의미. `constexpr` 이라 `static_assert(period_from_code(0).value_or(0) == 40'000u)` 도 된다.

</details>

### 연습 2 — 태그를 멤버 대신 `virtual Kind kind()`

`SensorV` 에 `virtual Kind kind() const = 0;` 만 두고 `dyn_cast` 를 다시 만들어라. `sizeof(SensorV)` 와 멤버 태그 버전 `sizeof(SensorT)` 를 비교하라.

<details>
<summary>답 보기</summary>

`sizeof(SensorV) == sizeof(void*)` (vptr 만), `sizeof(SensorT) == 2 * sizeof(void*)` (vptr + 1 B Kind + 정렬 패딩 → 맥 16 B, Cortex-M 8 B). 객체가 수천 개(파티클, 메시지)면 V, 판별이 핫 루프면 T(가상 호출 없이 바이트 비교). C 로 치면 "구조체 첫 필드의 type 바이트" vs "ops 테이블의 get_type 함수 포인터".

</details>

### 연습 3 — 실패할 수 있는 생성: `std::optional` 팩토리

`make_uart(pclk, baud) -> std::optional<UartCfg>`: BRR = round(pclk/baud), 16 ≤ BRR ≤ 0xFFFF 아니면 `nullopt`. 16 MHz/115200 → 139.

<details>
<summary>답 보기</summary>

`(pclk + baud/2) / baud` 로 반올림, `baud == 0` 먼저 거른다. 실패 이유가 하나뿐이거나 중요하지 않으면 `optional` 로 충분하고, 이유를 알려야 하면 `Result`/`expected`. `.value()` 대신 `has_value()` + `->` 또는 `value_or` (`.value()` 는 `-fno-exceptions` 에서 abort — 직접 확인: `bad_optional_access was thrown in -fno-exceptions mode`).

</details>

### 연습 4 — C HAL 경계: `Result` → errno 스타일 `int`

`extern "C" int bmi_read_x(int16_t* out)`: 성공 0, 실패는 `-ETIMEDOUT`/`-ENODEV`/`-EINVAL`/`-EAGAIN` (Linux 커널·Zephyr 관례).

<details>
<summary>답 보기</summary>

`switch` 로 `Err → -errno` 매핑, `out == nullptr` 은 `-EINVAL`. C 코드(벤더 HAL, RTOS 콜백)는 `Result` 템플릿을 모르므로 **경계에서 C 타입으로 낮춘다** (extern "C" 링키지는 CPP-B02 참고). 반대로 C 에서 올라오는 `int` 는 경계에서 `Err` 로 올린다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 15/15 passed
```

## 10. 핵심 정리

- `-fno-exceptions`: landing pad + LSDA(`__gcc_except_tab`/`.ARM.extab`) + 런타임(`__cxa_throw`, personality, unwinder) 제거. 실측 `__text` −16%(맥), ARM `.text` −25%. 정상 경로 명령어는 원래 같다.
- `-fno-rtti`: `_ZTI*`/`_ZTS*` typeinfo 와 `__dynamic_cast` 제거. vtable 의 typeinfo **슬롯은 남고 0**. throw 되는 타입의 typeinfo 는 예외가 켜져 있으면 남는다.
- 에러: `throw`/`try` → "cannot use ... with exceptions disabled", `dynamic_cast`(다운)/`typeid` → "requires -frtti". upcast·`dynamic_cast<void*>`·`noexcept` 는 OK.
- 대안: `Err` / `Result<T,E>`(≤4 B면 r0 반환) / `std::optional`(C++17) / `std::expected`(C++23, libc++ OK). `.value()`·`.at()` 는 abort 가 된다.
- `dynamic_cast` 대신 Kind 태그 + `classof` + `static_cast` (LLVM 방식). 중간 클래스는 kind 구간.
- ELF/ARM: `.ARM.exidx`(8 B/함수, CANTUNWIND=`0x1`), `.ARM.extab`; `-funwind-tables` 는 백트레이스용으로 예외 없이도 켤 수 있다.
- 크기 비교는 파일 크기가 아니라 섹션 크기(`size -m`, `arm-none-eabi-size`)로. 진짜 비용은 링크되는 런타임 쪽.

## 참고자료

- [Itanium C++ ABI — Virtual Table Layout (§2.5), RTTI (§2.9), Exception Handling ABI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html)
- [ARM — Exception Handling ABI for the Arm Architecture (EHABI): .ARM.exidx / .ARM.extab](https://github.com/ARM-software/abi-aa/blob/main/ehabi32/ehabi32.rst)
- [ARM — Procedure Call Standard (AAPCS32): 결과 반환 규칙](https://github.com/ARM-software/abi-aa/blob/main/aapcs32/aapcs32.rst)
- [LLVM Programmer's Manual — isa<>, cast<>, dyn_cast<> 와 classof](https://llvm.org/docs/HowToSetUpLLVMStyleRTTI.html)
- [LLVM Coding Standards — Do not use RTTI or Exceptions](https://llvm.org/docs/CodingStandards.html#do-not-use-rtti-or-exceptions)
- [cppreference — std::expected (C++23)](https://en.cppreference.com/w/cpp/utility/expected), [std::optional](https://en.cppreference.com/w/cpp/utility/optional)
- [SD-6 Feature-test macros (`__cpp_exceptions`, `__cpp_rtti`)](https://isocpp.org/std/standing-documents/sd-6-sg10-feature-test-recommendations)
- [GCC — Code Gen Options (`-funwind-tables`, `-fasynchronous-unwind-tables`, `-fexceptions`)](https://gcc.gnu.org/onlinedocs/gcc/Code-Gen-Options.html)
- C++ 표준 [expr.dynamic.cast], [expr.typeid], [expr.static.cast], [except.throw], [except.handle]
- Herb Sutter, P0709 *Zero-overhead deterministic exceptions* (제안 — 채택 안 됨)
