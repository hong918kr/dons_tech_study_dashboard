---
id: CPP-A03
time: 110분
updated: 2026-11-11
---
# CPP-A03 C++ 런타임 의존성 최소화: __cxa_* 심볼 추적

## 학습 목표

- 프리스탠딩 Cortex-M4 오브젝트의 `nm -u` 를 읽고, 미정의 심볼 하나하나를 **어느 C++ 구문이 만들었는지, 누가 정의를 주는지(libsupc++ / libgcc / newlib / crt / 나), 어떻게 없애는지** 표 한 장으로 답할 수 있다 — 표의 모든 행은 실제로 돌린 출력이다.
- clang 과 GCC 가 같은 소스에서 **다른 심볼을 요구하는 지점**(`__cxa_atexit` vs `__aeabi_atexit`, 강한 vs 약한 `__cxa_pure_virtual`, `__aeabi_memcpy4` vs `memcpy`, `__aeabi_unwind_cpp_pr0` vs `pr1`)을 실측으로 말한다.
- 스텁 층을 직접 쓴다: `__cxa_guard_acquire/release/abort` 상태 기계(단일 코어 + 인터럽트에서 무엇이 틀릴 수 있나), `__cxa_pure_virtual` = 고장 기록 + trap, 용량 고정 `atexit` 등록부(ARM 인자 순서 포함).
- `-nostdlib -nostartfiles` 맨몸 링크로 "undefined reference" 를 일부러 내고, 스텁 / `-lgcc -lc` 로 하나씩 지워 가며 **`new` 하나가 플래시 14.6 KB** 를 끌고 오는 사슬을 숫자로 보인다.
- 허용 목록 게이트(`check_syms.sh`)를 CI 에 걸어, 새 런타임 의존성이 리뷰 없이 들어오면 빌드가 실패하게 만든다.

## 1. 문제

"부트로더를 C++ 로 옮겼더니 링크에서 `undefined reference to '__cxa_guard_acquire'`, `'__aeabi_atexit'`, `'__dso_handle'` 가 우수수 나옵니다. 아무 거나 빈 함수로 채우면 되나요?" — C++ 펌웨어 시니어 면접의 단골이다. 정답은 "심볼마다 **원인 구문**과 **올바른 의미**가 다르다" 이고, 빈 함수로 채우면 조용히 틀리는 것(guard)과 오히려 바람직한 것(atexit)이 섞여 있다.

이미 배운 것은 다시 하지 않는다: 전역 생성자와 `.init_array`, Meyers singleton 의 guard 비용(CPP-B09), `-fno-exceptions/-fno-rtti` 의 섹션별 크기(CPP-B13), 가상 소멸자 → deleting dtor → `operator delete` → `malloc` 사슬과 vtable 비용(CPP-M06), `operator new` 금지(CPP-B14), `__atomic_*` 를 누가 주나(CPP-A04), 이름 맹글링 규칙과 ODR(MCPP-A05). 이 레슨은 그것들을 **"미정의 심볼" 이라는 하나의 관측 창**으로 묶고, 그 창을 CI 게이트로 만든다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">구문 → 미정의 심볼 → 누가 주나 (thumbv7em, 실측)</text>
  <g fill="currentColor"><text x="20" y="48">C++ 구문</text><text x="250" y="48">nm -u 에 보이는 것</text><text x="560" y="48">정의를 가진 곳</text></g>
  <g stroke="currentColor">
    <rect x="10" y="58" width="200" height="30" rx="4" class="f-mut"/><rect x="230" y="58" width="290" height="30" rx="4" class="f-warn"/><rect x="545" y="58" width="185" height="30" rx="4" class="f-acc"/>
    <rect x="10" y="96" width="200" height="30" rx="4" class="f-mut"/><rect x="230" y="96" width="290" height="30" rx="4" class="f-warn"/><rect x="545" y="96" width="185" height="30" rx="4" class="f-acc"/>
    <rect x="10" y="134" width="200" height="30" rx="4" class="f-mut"/><rect x="230" y="134" width="290" height="30" rx="4" class="f-warn"/><rect x="545" y="134" width="185" height="30" rx="4" class="f-acc"/>
    <rect x="10" y="172" width="200" height="30" rx="4" class="f-mut"/><rect x="230" y="172" width="290" height="30" rx="4" class="f-warn"/><rect x="545" y="172" width="185" height="30" rx="4" class="f-acc"/>
    <rect x="10" y="210" width="200" height="30" rx="4" class="f-mut"/><rect x="230" y="210" width="290" height="30" rx="4" class="f-ok"/><rect x="545" y="210" width="185" height="30" rx="4" class="f-ok"/>
    <rect x="10" y="248" width="200" height="30" rx="4" class="f-mut"/><rect x="230" y="248" width="290" height="30" rx="4" class="f-ok"/><rect x="545" y="248" width="185" height="30" rx="4" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="18" y="78">static Cal c; (함수 안)</text><text x="238" y="78" font-family="monospace" font-size="12">__cxa_guard_acquire / _release</text><text x="553" y="78">libsupc++ guard.o</text>
    <text x="18" y="116">전역 + 비자명 소멸자</text><text x="238" y="116" font-family="monospace" font-size="12">(__cxa|__aeabi)_atexit, __dso_handle</text><text x="553" y="116">newlib libc, crtbegin.o</text>
    <text x="18" y="154">virtual ~Drv();  / new</text><text x="238" y="154" font-family="monospace" font-size="12">_ZdlPvj  /  _Znwj</text><text x="553" y="154">libsupc++ → malloc 사슬</text>
    <text x="18" y="192">= 0 (순수 가상)</text><text x="238" y="192" font-family="monospace" font-size="12">__cxa_pure_virtual (GCC: 약한 w)</text><text x="553" y="192">libsupc++ pure.o / 내 trap</text>
    <text x="18" y="230">a / b (uint64_t)</text><text x="238" y="230" font-family="monospace" font-size="12">__aeabi_uldivmod</text><text x="553" y="230">libgcc (808 B)</text>
    <text x="18" y="268">*d = *s (큰 구조체)</text><text x="238" y="268" font-family="monospace" font-size="12">__aeabi_memcpy4 | memcpy</text><text x="553" y="268">newlib libc</text>
  </g>
  <g fill="currentColor" font-size="12">
    <rect x="10" y="296" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="30" y="308">C++ 런타임: 없애거나 스텁으로 의미를 정한다</text>
    <rect x="370" y="296" width="14" height="14" class="f-ok" stroke="currentColor"/><text x="390" y="308">C 런타임 도우미: 허용 목록에 넣고 비용만 안다</text>
  </g>
</svg>
```

### Part A — nm 을 읽는 코드: 파서와 분류기

- `parse_nm_line(line, out)` — `llvm-nm` / `arm-none-eabi-nm` 의 네 형식(`주소 타입 이름`, `주소 크기 타입 이름`, `   U 이름`, Mach-O `-u` 의 `이름` 만)을 파싱. `U` 와 `w`(약한 미정의)는 미정의.
- `normalize(name, macho)` — Mach-O 는 이름 앞에 `_` 가 하나 더 붙는다(`___cxa_atexit`).
- `classify(sym) → Kind`, `fix_for(Kind) → Fix`, `provider_for(Kind) → Provider` — 5. 풀이의 표를 코드로. `summarize()` 로 붙여 넣은 **실제 nm 출력** 픽스처 11개를 요약.

### Part B — 스텁 층: guard 상태 기계와 pure virtual trap

- `rt::guard_acquire/release/abort` — ARM C++ ABI guard(4바이트, bit0 = 완료) 위에 "초기화 중" 비트를 얹은 상태 기계. 단일 코어 + 인터럽트 모델(PRIMASK, `isr_window()`)에서: ISR 이 검사와 표시 사이에 끼어도 **이중 생성 없음**, 생성자는 **인터럽트를 켠 채** 돌 것, 초기화 중 재진입(ISR 또는 재귀)은 기다릴 수 없으니 **trap 기록**, abort 후엔 재시도.
- `rt::pure_virtual(where)` — 고장 기록(첫 고장 보존) + trap. 손으로 만든 vtable 슬롯으로 호출.

### Part C — atexit 등록부와 허용 목록 게이트

- `AtexitRegistry<N>` — `cxa_atexit(f, obj, dso)`, ARM 판 `aeabi_atexit(obj, f, dso)`(인자 순서가 다르다), 가득 차면 실패 + 카운터, `finalize(dso)` 는 LIFO 로 한 번씩.
- `gate(lines, macho, allow)` — `check_syms.sh` 의 판정을 C++ 로: 허용 종류 밖의 미정의 심볼 수와 첫 위반. 고친 `fw_app.cpp` 는 0, LEGACY 판은 6.

## 2. Problem (English)

> **Track and eliminate C++ runtime dependencies in a freestanding Cortex-M build.**
>
> 1. Write `bool parse_nm_line(std::string_view, NmSym&)` that accepts `llvm-nm`/GNU `nm` output lines (`addr type name`, `addr size type name`, `      U name`, and the bare-name lines Mach-O `llvm-nm -u` prints). Write `Kind classify(std::string_view)` covering guard, pure virtual, atexit/`__aeabi_atexit`, `__dso_handle`, `operator new/delete`, RTTI, exception handling (incl. ARM EHABI personality routines), `__aeabi_*` integer helpers, mem functions, soft-float, atomics; plus `fix_for` and `provider_for`. Summarise real `nm` fixtures (ELF and Mach-O, weak undefined included).
> 2. Implement the runtime stubs a bare-metal single-core build needs: `guard_acquire/release/abort` on an ARM-ABI guard word (bit 0 = initialised; the compiler inlines that test), safe against an interrupt arriving anywhere, without masking interrupts during the constructor, trapping on re-entry while initialisation is in progress; and `pure_virtual` that records the first fault and traps.
> 3. Implement a fixed-capacity `__cxa_atexit` registry with the ARM EABI `__aeabi_atexit(object, destroyer, dso)` adapter and LIFO `finalize(dso)`, and an allow-list gate over `nm -u` output.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap. Interrupts are simulated (`isr_window()`, PRIMASK as a variable).
>
> Follow-ups: Why does clang emit `__cxa_atexit` but GCC `__aeabi_atexit` for the same file? Is an empty `__cxa_guard_acquire` that returns 1 correct? What does `-fno-threadsafe-statics` trade away? Why does `-fexceptions` alone (no `throw`) add `__aeabi_unwind_cpp_pr0`? Why is `__cxa_pure_virtual` weak in GCC's object? What does one `new` cost on a bare-metal link?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, 앞뒤 일부 — FAIL 80줄):

```text
FAIL starter.cpp:346  parse_nm_line("00000000 T _Z8cal_gainv", s) && s.name == "_Z8cal_gainv" && s.type == 'T' && !s.undefined
FAIL starter.cpp:347  parse_nm_line("         U __cxa_guard_acquire", s) && s.name == "__cxa_guard_acquire" && s.undefined
...
FAIL starter.cpp:435  g_cal.ctor_runs == 1
FAIL starter.cpp:436  g_isr_runs == 1 && rt::g_fault.count == 1 && rt::g_fault.first == rt::Fault::GuardRecursion
...
FAIL starter.cpp:511  legacy.checked == 11 && legacy.violations == 6 && legacy.first == "_ZdlPvj"
FAIL starter.cpp:518  gate(kGccRow3, count_of(kGccRow3), false, kFirmwareAllow).first == "__aeabi_atexit"
8/88 passed
```

하네스 설계: 시작 코드의 `guard_acquire` 는 항상 0("이미 초기화됨")을 돌려준다 — 틀렸지만 안전하다(생성자를 안 부를 뿐, 무한 루프나 이중 생성이 없음). 처음부터 PASS 인 8개는 "거부해야 할 입력" 과 "프로젝트 심볼" 쪽이다. 픽스처(`kClangLegacy` 등)는 아래 관찰 파일들을 실제로 돌린 출력을 그대로 붙인 것이다.

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

| 파일 | 무엇 | 실행 |
|---|---|---|
| `demo_constructs.cpp` + `nm_table.sh` | 구문 12개(ROW)를 ROW 마다 오브젝트로 → `nm -u` | `bash nm_table.sh` (맥 clang), `bash nm_table.sh host`, VM 에서 `bash nm_table.sh gcc` |
| `fw_app.cpp` | 작은 펌웨어 TU. 기본 = 고친 판, `-DLEGACY` = 흔한 습관 판 | 아래 두 스크립트가 쓴다 |
| `check_syms.sh` + `allow_syms.txt` | 허용 목록 밖 미정의 심볼이 있으면 exit 1 | `bash check_syms.sh` (→ 0), `bash check_syms.sh -DLEGACY` (→ 1) |
| `fw_start.cpp`, `fw_stubs.cpp`, `link_vm.sh` | VM 에서 `-nostdlib -nostartfiles` 맨몸 링크, 스텁 효과, 크기 | 스크립트 머리 주석의 `tar … | limactl shell elinux …` |
| `demo_pure_host.cpp` | 호스트에서 진짜 `__cxa_pure_virtual` 이 불리는 경로 | `c++ -std=c++20 -O0 -fno-exceptions -fno-rtti demo_pure_host.cpp -o pure && ./pure` |

맥 주의: Apple clang 은 thumbv7em 코드는 만들지만 bare-metal C++ 헤더가 없어서, 스크립트들은 CPP-A04 `asm_targets.sh` 와 같이 VM 의 arm-none-eabi 헤더(libstdc++ 14.2 + newlib)를 `ARM_INC`(기본 `/tmp/arm-none-eabi-include`)로 한 번 복사해 쓴다.

## 4. 힌트

<details>
<summary>힌트 1 — 파서: 토큰 수로 형식을 가른다</summary>

공백으로 나눈 토큰이 1개면 Mach-O `-u` 의 "이름만" 줄(단 `r.o:` 같은 머리 줄은 제외), 2개 이상이면 **마지막이 이름, 그 앞이 타입 한 글자**다. 주소·크기 토큰은 몇 개든 무시하면 된다. 타입이 `U` 또는 `w` 면 미정의.

</details>

<details>
<summary>힌트 2 — 분류기: 순서가 전부다</summary>

`__aeabi_` 로 시작하는 이름만 다섯 종류다: `__aeabi_atexit`(Atexit), `__aeabi_unwind_cpp_pr0`(예외), `__aeabi_memclr8`(mem), `__aeabi_ddiv`(부동소수점 — "div" 가 들어 있지만!), `__aeabi_uldivmod`(정수). 구체적인 이름부터 먼저 걸러라. `_ZTI` 는 typeinfo — `-fno-rtti` 여도 `throw 42` 는 `_ZTIi` 를 요구한다(실측).

</details>

<details>
<summary>힌트 3 — guard: "검사 + 표시" 만 원자적으로</summary>

`irq_save()` → `guard_load()` → 완료/초기화 중/처음 세 갈래 → 처음이면 `guard_store(v | kBusy)` → `irq_restore()`. 생성자는 그 **밖**에서 돈다(호출자가 1 을 받은 뒤). release 는 busy 를 지우고 bit0 만 남긴다 — 컴파일러가 인라인으로 보는 비트가 bit0 이다.

</details>

<details>
<summary>힌트 4 — atexit: ARM 은 인자 둘이 바뀌어 있다</summary>

`__cxa_atexit(f, obj, dso)` 대 `__aeabi_atexit(obj, f, dso)`. 등록부는 하나로 두고 ARM 판은 순서만 바꿔 넘긴다. finalize 는 뒤에서부터, 실행 **전에** 항목을 죽여 두면 같은 항목이 두 번 돌 일이 없다.

</details>

## 5. 풀이

### 0. 실측 표 — `nm_table.sh` (clang, thumbv7em)

`bash nm_table.sh` 실제 출력 (Apple clang 21.0.0, `--target=thumbv7em-none-eabi -mcpu=cortex-m4 -O2 -ffreestanding`):

```text
1   -fno-exceptions -fno-rtti          _ZN3CalC1Ev __cxa_guard_acquire __cxa_guard_release
1   -fno-exceptions -fno-rtti -fno-threadsafe-statics _ZN3CalC1Ev
1   -fexceptions -fno-rtti             _ZN3CalC1Ev __cxa_end_cleanup __cxa_guard_abort __cxa_guard_acquire __cxa_guard_release __gxx_personality_v0
2   -fno-exceptions -fno-rtti          __cxa_pure_virtual
3   -fno-exceptions -fno-rtti          _ZN6LoggerC1Ev _ZN6LoggerD1Ev __cxa_atexit __dso_handle
3   -fno-exceptions -fno-rtti -fno-use-cxa-atexit _ZN6LoggerC1Ev _ZN6LoggerD1Ev atexit
3   -fno-exceptions -fno-rtti -fno-c++-static-destructors _ZN6LoggerC1Ev
4   -fno-exceptions -fno-rtti          _ZN6LoggerC1Ev
12  -fno-exceptions -fno-rtti          _ZN6LoggerC1Ev
5   -fno-exceptions -fno-rtti          _ZdlPvj
5   -fno-exceptions -fno-rtti -fno-sized-deallocation _ZdlPv
6   -fno-exceptions -fno-rtti          _Znwj
7   -fno-exceptions -frtti             _ZTVN10__cxxabiv117__class_type_infoE _ZTVN10__cxxabiv120__si_class_type_infoE __dynamic_cast
7   -fno-exceptions -fno-rtti          컴파일 에러: use of typeid requires -frtti
8   -fexceptions -fno-rtti             _ZTIi __aeabi_unwind_cpp_pr0 __cxa_allocate_exception __cxa_begin_catch __cxa_end_catch __cxa_throw __gxx_personality_v0
8   -fno-exceptions -fno-rtti          컴파일 에러: cannot use 'throw' with exceptions disabled
9   -fno-exceptions -fno-rtti          __aeabi_ldivmod __aeabi_uldivmod
10  -fno-exceptions -fno-rtti          __aeabi_memclr4 __aeabi_memcpy4
10  -fno-exceptions -fno-rtti -fno-builtin __aeabi_memclr4 __aeabi_memcpy4
11  -fno-exceptions -fno-rtti          __aeabi_dadd __aeabi_dmul
```

이걸 정리한 이 레슨의 중심 표. "출처" 는 VM 의 툴체인 아카이브를 `nm` 으로 찾은 결과다(`link_vm.sh` 끝부분, 아래 Part C):

| ROW | 구문 | 미정의 심볼 (clang) | GCC 14 는 | 출처 (arm-none-eabi 14.2) | 없애는 법 |
|---|---|---|---|---|---|
| 1 | 함수 안 `static Cal c;` (비-constexpr 생성자) | `__cxa_guard_acquire/release` | 같음 | libsupc++ `guard.o` | `constinit` + 부팅 때 명시적 init, `-fno-threadsafe-statics`, 또는 스텁 (Part B) |
| 1 | 위 + `-fexceptions` | `+ __cxa_guard_abort, __cxa_end_cleanup, __gxx_personality_v0` | 같음 | libsupc++ | `-fno-exceptions` |
| 2 | 순수 가상 + 키 함수가 이 TU | `__cxa_pure_virtual` (강한 `U`) | **약한 `w`** | libsupc++ `pure.o` | 내 trap 스텁 (없애기 어려움 — vtable 슬롯이니까) |
| 3 | 전역 + 비자명 소멸자 | `__cxa_atexit, __dso_handle` | **`__aeabi_atexit`**, `__dso_handle` | libc `cxa_atexit.o`/`aeabi_atexit.o`, `crtbegin.o` | 4 / 12 행, `-fno-c++-static-destructors`(clang) |
| 3 | 위 + `-fno-use-cxa-atexit` | `atexit` | 등록 없음 → **`.fini_array`** 엔트리 | libc `atexit.o` | (소멸자 코드는 남는다) |
| 4 | `NoDestroy<T>` (정적 버퍼 + placement new) | 생성자만 | 같음 | — | — |
| 12 | `[[clang::no_destroy]]` | 생성자만 | 경고 후 **무시** → `__aeabi_atexit` | — | 이식성 없음 |
| 5 | `virtual ~Drv()` | `_ZdlPvj` (크기 있는 delete) | 같음 | libsupc++ `del_ops.o` → `free` | protected 비가상 소멸자 (CPP-M06), 또는 trap 스텁 |
| 6 | `new Pkt{4}` | `_Znwj` | 같음 | libsupc++ `new_op.o` → `malloc`, `bad_alloc`, 언와인더 | 정적 풀 (CPP-B14) |
| 7 | `typeid` / `dynamic_cast` (`-frtti`) | `__dynamic_cast`, `_ZTVN10__cxxabiv1…` | 같음 | libsupc++ `dyncast.o`, `tinfo.o` | `-fno-rtti` + 자체 태그 |
| 8 | `throw` / `catch` (`-fexceptions`) | `__cxa_throw` 등 + `_ZTIi` + `__aeabi_unwind_cpp_pr0` | `pr1` + `__cxa_end_cleanup` | libsupc++ `eh_*.o`, libgcc `unwind-arm.o` | `-fno-exceptions` + Result (CPP-M11) |
| 9 | `uint64_t / uint64_t`, `/ 1000u` 도 | `__aeabi_uldivmod`, `__aeabi_ldivmod` | 같음 | libgcc (`__udivmoddi4` 760 B) | 허용 (비용만 알기), 32비트로 |
| 10 | 128 B 구조체 복사·0 초기화 | `__aeabi_memcpy4`, `__aeabi_memclr4` | **`memcpy`, `memset`** | newlib libc | 허용 |
| 11 | `double` 연산 | `__aeabi_dadd`, `__aeabi_dmul` | + `__aeabi_fadd/fmul` | libgcc `_arm_muldivdf3.o` | `float` / 고정소수점 |

관찰 몇 가지 (모두 위 출력에서):

- **`-ffreestanding` 은 이것들을 하나도 없애지 않는다.** 그 플래그는 "호스트 라이브러리를 가정하지 말라" 는 뜻이지 "런타임 호출을 만들지 말라" 가 아니다. `-fno-builtin` 을 붙여도 구조체 복사는 여전히 `__aeabi_memcpy4` 였다(ROW 10) — 컴파일러 백엔드가 큰 복사를 라이브러리 호출로 내리는 것은 소스의 `memcpy` 와 무관하다.
- **64비트 상수 나눗셈도 호출이다.** `div_const(a) { return a / 1000u; }` 가 clang·GCC 모두 `mov r2,#1000; movs r3,#0; bl __aeabi_uldivmod` 였다. 32비트 `a / b` 는 M4 의 `udiv` 한 명령. 32비트 상수 나눗셈의 "곱셈+시프트" 변환을 64비트에도 기대하면 틀린다.
- **같은 `-mcpu=cortex-m4` 라도 float ABI 기본값이 다르다.** clang `thumbv7em-none-eabi` 는 `__ARM_FP 0x6` 이 정의되고 `float` 연산이 `vmul.f32` (softfp: FPU 를 쓰고 인자는 정수 레지스터) 였지만, VM 의 GCC 기본 멀티립(`thumb/v7e-m/nofp`)은 `__aeabi_fadd/fmul` 을 불렀다. `double` 은 M4F 가 단정도 FPU 뿐이라 `-mfloat-abi=hard` 여도 libgcc 호출이다.
- **`-fexceptions` 는 `throw` 없이도 의존성을 만든다.** 고친 `fw_app.cpp` 를 `-fexceptions` 로만 바꿔 `check_syms.sh` 를 돌리면 `DENY  __aeabi_unwind_cpp_pr0` 1개가 나왔다 (실측). 언와인드 테이블(`.ARM.exidx`)이 개인성 루틴(personality routine)을 참조하기 때문 — CPP-B13 의 섹션 크기 이야기가 "링크 의존성" 으로도 보인다.
- 호스트(Mach-O, `bash nm_table.sh host`)와 다른 점: 이름 앞 `_` 하나 더(`___cxa_atexit`), `new` 가 `__Znwm`(size_t = unsigned long, 맹글링은 MCPP-A05), 가상 소멸자는 `__ZdlPv`(Apple 기본은 크기 없는 delete), 예외 정리 경로가 `__Unwind_Resume`(ARM 은 `__cxa_end_cleanup`), ROW 9–11 은 미정의 없음(64비트 코어).

### 0-1. guard 는 어떻게 생겼나 — 4바이트, bit0

ROW 1 의 clang 출력 (지시어 제거):

```text
_Z8cal_gainv:                               _Z8cal_gainv.cold.1:
	push	{r4, lr}                            	push	{r4, r5, r7, lr}
	movw	r4, :lower16:.L_MergedGlobals       	ldr	r5, .LCPI1_0
	movt	r4, :upper16:.L_MergedGlobals       	adds	r4, r5, #4          @ &guard
	ldrb	r0, [r4, #4]     @ guard 첫 바이트    	mov	r0, r4
	dmb	sy                                  	bl	__cxa_guard_acquire
	lsls	r0, r0, #31      @ bit0 → 플래그     	cbz	r0, .LBB1_2         @ 0 = 남이 끝냄
	beq	.LBB0_2                             	mov	r0, r5
	ldr	r0, [r4]         @ 빠른 경로          	bl	_ZN3CalC1Ev
	pop	{r4, pc}                            	mov	r0, r4
.LBB0_2:                                    	pop.w	{r4, r5, r7, lr}
	bl	_Z8cal_gainv.cold.1                 	b	__cxa_guard_release
```

GCC 14 는 `ldr r3,[r4]; dmb ish; lsls r3,r3,#31; bpl` — 워드로 읽지만 보는 비트는 똑같이 bit0 이다. `nm -S` 로 본 guard 크기: thumbv7em `_ZGVZ8cal_gainvE1c` = **4 바이트**(clang·GCC), 같은 소스를 `--target=aarch64-none-elf` 로 하면 **8 바이트**. ARM C++ ABI(CPPABI32)는 guard 를 4바이트 워드로 하고 `__cxa_guard_release` 가 최하위 비트를 세운다고 정한다. Itanium ABI §2.8 은 64비트 guard 의 **첫 바이트**가 0 이면 미완료, 1 이면 완료다. 리틀 엔디언 Cortex-M 에선 두 규칙이 같은 비트를 가리키지만, **나머지 비트는 런타임 몫**이라 이 레슨의 스텁은 bit8 을 "초기화 중" 으로 쓴다 (연습 3).

`-fno-threadsafe-statics` 를 주면 (clang 실측):

```text
	ldrb	r0, [r4]        @ guard
	cbz	r0, .LBB0_2     @ 0 이면 느린 경로 → bl _ZN3CalC1Ev; movs r0,#1; strb r0,[r4]
```

배리어도, 런타임 호출도, "초기화 중" 표시도 없다. 단일 스레드 가정 그대로 — ISR 이 생성 도중 같은 함수에 들어오면 **생성자가 두 번 돈다** (연습 1 이 결정적으로 재현한다). 즉 이 플래그는 런타임 의존성을 지우는 대신 **검출 능력**을 지운다.

### 0-2. 전역 소멸자 — clang `__cxa_atexit`, GCC `__aeabi_atexit`

같은 ROW 3 소스, 두 컴파일러 (실측):

```text
@ clang (thumbv7em-none-eabi)                @ GCC 14 (arm-none-eabi)
	bl	_ZN6LoggerC1Ev                        	bl	_ZN6LoggerC1Ev
	movw/movt r0, _ZN6LoggerD1Ev   @ r0=소멸자 	ldr	r2, =__dso_handle
	movw/movt r2, __dso_handle               	ldr	r1, =_ZN6LoggerD1Ev   @ r1=소멸자
	mov	r1, r4                   @ r1=객체  	ldr	r0, =g_log            @ r0=객체
	b	__cxa_atexit                          	b.w	__aeabi_atexit
```

ARM C++ ABI 는 `__aeabi_atexit(void* object, void (*destroyer)(void*), void* dso_handle)` 를 정의하고, 모델 구현은 `__cxa_atexit(destroyer, object, dso_handle)` 로 넘긴다 — **앞 두 인자가 뒤바뀐다**. GCC 는 그 ABI 를 따랐고, clang 은 이 타깃에서도 Itanium 의 `__cxa_atexit` 를 불렀다. 그래서 스텁 층은 **둘 다** 정의해야 하고(`fw_stubs.cpp`), 하나를 다른 하나로 넘길 때 순서를 틀리면 소멸자 자리에 객체 주소가 들어간다(Part C 테스트).

없애는 방법 비교 (전부 실측):

| 방법 | clang | GCC 14 | 비고 |
|---|---|---|---|
| `-fno-use-cxa-atexit` | `__dtor_g_log` 를 만들어 `atexit` 호출 | 등록 대신 `.fini_array` 에 `_GLOBAL__sub_D_g_log` | 소멸자 코드는 플래시에 남음 |
| `-fno-c++-static-destructors` | 등록 사라짐 | `unrecognized command-line option` | clang 전용 |
| `[[clang::no_destroy]]` | 등록 사라짐 | `warning: 'clang::no_destroy' scoped attribute directive ignored` → 등록 남음 | 이식성 없음 |
| `NoDestroy<T>` (정렬된 바이트 버퍼 + placement new, 소멸자 없음) | 생성자만 | 생성자만 | 표준 C++, `static_assert(std::is_trivially_destructible_v<…>)` 로 고정 |
| 타입을 자명 소멸로 (+`constinit`) | 등록도 생성자 호출도 없음 | 같음 | 가장 좋다 (`fw_app.cpp` 고친 판) |

펌웨어는 `main` 에서 돌아오지 않으므로 [basic.start.term] 의 정적 객체 소멸은 일어나지 않는다. 등록은 RAM(등록부)과 플래시(소멸자, 등록 코드)만 쓴다.

### Part A — 파서와 분류기

파서는 형식 넷을 토큰 수로 구분한다. 함정 둘: Mach-O 의 `llvm-nm -u` 는 `U` 없이 **이름만** 찍는다(`__ZdlPv` — 실측, ELF 의 `-u` 는 `U` 를 찍는다). 그리고 여러 파일을 주면 `r.o:` 머리 줄이 낀다.

분류 순서 (solution.cpp):

```cpp
if (s == "__cxa_guard_acquire" || s == "__cxa_guard_release") return Kind::Guard;
if (s == "__cxa_guard_abort") return Kind::GuardAbort;                    // 예외 빌드에서만
... pure_virtual, atexit 3종(+__aeabi_atexit), __dso_handle, _Znw/_Zna, _Zdl/_Zda
if (s == "__dynamic_cast" || starts(s, "_ZTI") || starts(s, "_ZTS") || starts(s, "_ZTVN10__cxxabiv1")) return Kind::Rtti;
if (starts(s, "__gxx_personality") || starts(s, "__aeabi_unwind_cpp_pr") || starts(s, "_Unwind_")) return Kind::Eh;
... __cxa_throw 류 → Eh, mem* → MemFn, __atomic_/__sync_ → Atomic
if (starts(s, "__aeabi_")) {   // 남은 __aeabi_: mem → 부동소수점(d/f/cd/cf 접두, '2' 포함) → 정수(div, l/ul 접두)
```

- `_ZTV3Bus` (내 클래스의 vtable) 는 **프로젝트** 이고, `_ZTVN10__cxxabiv117__class_type_infoE` (typeinfo 객체의 vtable) 는 RTTI 런타임이다. 접두 `_ZTV` 만 보고 묶으면 틀린다.
- 모르는 `__` 이름(`__cxa_bad_cast`, `__aeabi_read_tp`, `__stack_chk_fail`)은 `Unknown` → `Fix::Investigate`. 게이트에서 자동으로 위반이 된다 — 모르는 것은 허용하지 않는 것이 기본값이어야 한다.
- 출처 표(`provider_for`)는 기억이 아니라 `link_vm.sh` 의 아카이브 `nm` 결과다. 의외였던 것: `__cxa_atexit` 는 libsupc++ 가 아니라 **newlib libc** 에 있고, `__aeabi_atexit` 는 **둘 다**(libsupc++ `atexit_arm.o`, libc `libc_a-aeabi_atexit.o`) 있다 (연습 4 — 링크 순서가 고른다).

### Part B — guard 상태 기계 (단일 코어 + 인터럽트)

[stmt.dcl]/4: 블록 범위 정적 변수의 동적 초기화는 처음 선언을 지날 때 하고, **동시에** 들어오면 기다려야 하며, 초기화 중 **재귀로** 다시 들어오면 정의되지 않은 동작이다. Itanium ABI §3.3.3: `__cxa_guard_acquire` 는 "초기화가 아직 안 끝났으면 1, 아니면 0". 단일 코어 bare-metal 에서 "기다린다" 는 선택지가 없다 — ISR 이 main 을 기다리면 main 은 영원히 다시 돌지 못한다. 그래서 세 상태:

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">guard 워드: 상태와 ISR 이 끼는 자리</text>
  <rect x="20" y="45" width="150" height="50" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="32" y="67" fill="currentColor">미초기화</text><text x="32" y="85" fill="currentColor" font-family="monospace" font-size="12">0x000</text>
  <rect x="295" y="45" width="150" height="50" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="307" y="67" fill="currentColor">초기화 중</text><text x="307" y="85" fill="currentColor" font-family="monospace" font-size="12">0x100 (busy)</text>
  <rect x="570" y="45" width="150" height="50" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="582" y="67" fill="currentColor">완료</text><text x="582" y="85" fill="currentColor" font-family="monospace" font-size="12">0x001 (bit0)</text>
  <path d="M170 62 L295 62" stroke="currentColor" marker-end="url(#a3)"/>
  <text x="178" y="56" fill="currentColor" font-size="12">acquire → 1</text>
  <path d="M445 62 L570 62" stroke="currentColor" marker-end="url(#a3)"/>
  <text x="458" y="56" fill="currentColor" font-size="12">release</text>
  <path d="M295 85 L170 85" stroke="currentColor" marker-end="url(#a3)"/>
  <text x="200" y="102" fill="currentColor" font-size="12">abort (생성 실패)</text>
  <path d="M370 95 Q 370 130 400 130 Q 430 130 430 95" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="300" y="148" fill="currentColor" font-size="12">acquire(재진입) → trap, 0</text>
  <defs><marker id="a3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0 0 L10 5 L0 10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="185" fill="currentColor">main: [PRIMASK=1: 읽기 0 → busy 쓰기] PRIMASK 복원 → 생성자 (IRQ 켜짐) → release</text>
  <rect x="20" y="197" width="230" height="22" class="f-acc" stroke="currentColor"/>
  <text x="28" y="213" fill="currentColor" font-size="12">검사+표시: ISR 은 pending 으로</text>
  <rect x="255" y="197" width="250" height="22" class="f-mut" stroke="currentColor"/>
  <text x="263" y="213" fill="currentColor" font-size="12">생성자: 다른 ISR 은 즉시 실행</text>
  <g fill="currentColor" font-size="12">
    <text x="20" y="245">마스크 없이 "읽기 0 → (ISR: 읽기 0, 생성, 완료) → busy 쓰기" 면 main 이 다시 생성 → 이중 생성 (테스트가 잡는다)</text>
    <text x="20" y="265">생성자 내내 마스크하면 정확하지만 IRQ 지연 = 생성자 길이 (OTP 읽기, I2C 프로브 …) → 테스트가 금지한다</text>
    <text x="20" y="285">같은 static 을 ISR 이 생성 도중 쓰면: 기다릴 수 없다 → trap 기록. 진짜 해법은 IRQ 켜기 전에 초기화</text>
  </g>
</svg>
```

```cpp
static int guard_acquire(uint32_t* g) {
    const uint32_t pm = irq_save();                  // mrs pm, primask; cpsid i
    const uint32_t v = guard_load(g);                // (여기서 IRQ 요청 → pending)
    if (v & kDone) { irq_restore(pm); return 0; }
    if (v & kBusy) { irq_restore(pm); trap(Fault::GuardRecursion, 0xA1u); return 0; }
    guard_store(g, v | kBusy);
    irq_restore(pm);                                 // pending ISR 은 여기서: busy 를 보고 trap
    return 1;
}
static void guard_release(uint32_t* g) { guard_store(g, kDone); }   // 타깃: release store
static void guard_abort(uint32_t* g)   { guard_store(g, 0u); }
```

테스트가 확인하는 것 (`LazyStatic::get()` 은 컴파일러가 내는 코드를 손으로 쓴 것 — bit0 빠른 경로 → acquire → 생성 → release):

| 시나리오 | 기대 | 틀린 구현이 내는 것 |
|---|---|---|
| 정상 | 생성 1번, guard == `0x001` (busy 지워짐) | release 가 busy 를 남기면 `0x101` → 5 FAIL |
| acquire 의 검사~표시 사이 IRQ, ISR 이 같은 static | 생성 1번 + `GuardRecursion` 1건 | 마스크 없음 → 생성 2번 (2 FAIL, 주입 실험) |
| 생성자 도중 IRQ, ISR 이 **다른** static | ISR 이 생성 "도중" 실행됨, 고장 0 | 생성자 내내 마스크하면 ISR 이 끝난 뒤로 밀림 |
| 생성자 도중 IRQ, ISR 이 **같은** static / 생성자 안 재귀 | trap, 생성 1번 | — |
| IRQ 켜기 전에 미리 초기화 | ISR 은 빠른 경로, 고장 0 | — |
| acquire → abort → acquire | 다시 1 | — |
| 호출자가 이미 PRIMASK=1 | acquire 뒤에도 1 | `__enable_irq()` 식 복원 (CPP-B06) |

**실제 Cortex-M4 로는?** `fw_stubs.cpp` 의 같은 로직을 GCC 로 컴파일한 결과 (`link_vm.sh`, 실측):

```text
00000000 <__cxa_guard_acquire>:
   0:	push	{r3, lr}
   2:	mrs	r2, PRIMASK
   6:	cpsid	i
   8:	ldr	r3, [r0, #0]
   a:	lsls	r1, r3, #31          @ bit0: 완료?
   c:	bmi.n	20
   e:	lsls	r1, r3, #23          @ bit8: 초기화 중?
  10:	bmi.n	28
  12:	orr.w	r3, r3, #256
  16:	str	r3, [r0, #0]
  18:	msr	PRIMASK, r2
  1c:	movs	r0, #1
  1e:	pop	{r3, pc}
  ...
  28:	msr	PRIMASK, r2
  2c:	ldr	r0, [pc, #4]         @ 0xc0de0002 → fw_trap(code, lr)
```

56 바이트, 인터럽트 차단 구간은 명령 7개. `__cxa_guard_release` 는 `__atomic_store_n(g, kDone, __ATOMIC_RELEASE)` 라 10 바이트(`dmb` + `str`) — 인라인 빠른 경로의 `ldrb; dmb` 와 짝이 맞는다. 단일 코어에서 하드웨어 배리어는 사실 필요 없지만(CPP-A04 Q7), 컴파일러가 빠른 경로에 이미 `dmb` 를 넣으므로 맞춰 둔다.

**이 스텁이 틀리는 곳:** RTOS 태스크 둘이 같은 static 에 동시에 들어오는 경우다. 표준은 "기다려라" 인데 이 스텁은 trap 한다. RTOS 가 있으면 busy 를 본 쪽은 뮤텍스/세마포어로 **블록**해야 하고(ISR 에서는 여전히 trap), 그건 RTOS 포트의 일이다 — newlib 의 retarget lock 이나 libsupc++ 의 gthreads 경로가 그 자리지만, 이 레슨에선 확인하지 않았다(미확인). 단일 코어 bare-metal 의 결론은 단순하다: **ISR 에서 쓰는 lazy static 은 IRQ 켜기 전에 한 번 불러 둔다**, 더 좋게는 `constinit` + 명시적 `init()` (CPP-B09).

#### pure virtual — 기록하고 멈춘다

[class.abstract]/6: 추상 클래스의 생성자·소멸자에서 생성 중인 객체에 대해 순수 가상 함수를 (직접 또는 간접) 가상 호출하면 정의되지 않은 동작이다. Itanium ABI §3.2.6: `__cxa_pure_virtual` 의 동작은 정하지 않지만 "프로그램을 끝낼 것으로 기대한다". 호스트에서 진짜로 불러 보면 (`demo_pure_host.cpp`, 기반 생성자 → 비가상 `start()` → 가상 `sample()`):

```text
$ c++ -std=c++20 -O0 -fno-exceptions -fno-rtti demo_pure_host.cpp -o pure && ./pure; echo "exit=$?"
libc++abi: Pure virtual function called!
exit=134
$ c++ ... -DMY_STUB demo_pure_host.cpp -o pure2 && ./pure2; echo "exit=$?"
fw-style trap: pure virtual called (lr=0x1020ec648)
exit=42
```

`-O2` 로도 같은 메시지였다 (간접 경로라 컴파일러가 경고도 최적화도 못 함). `-DMY_STUB` 는 내 정의가 정적 링크에서 libc++abi 의 것을 이긴 것 — 펌웨어에서 하는 일과 같다. 펌웨어 판(`fw_stubs.cpp`)은 고장 코드와 LR 을 `.noinit` RAM 에 남기고 `bkpt #0` → `for(;;)` (WDT 리셋 후 부트 로그에서 읽는다). 하네스의 `FaultLog` 는 **첫 고장을 보존**한다 — pure virtual 뒤에 이어지는 guard 고장은 대개 결과이지 원인이 아니다.

GCC 의 ROW 2 오브젝트에서 `__cxa_pure_virtual` 은 **약한 미정의(`w`)** 였다. 정의가 없어도 링크되고 슬롯이 0 이 된다 — 호출되면 0번지 분기(CPP-M06 이 덤프로 보였다). clang 은 강한 `U` 라 링크가 실패한다. "GCC 빌드는 링크가 되니까 괜찮다" 가 가장 위험한 결론이다: 게이트는 `w` 도 미정의로 세야 한다(테스트 `gate(kGccRow2, …, bit(Project)).violations == 1`).

### Part C — atexit 등록부, 게이트, 그리고 맨몸 링크

등록부는 Itanium §3.3.6.3 의 계약 그대로다: `__cxa_atexit` 는 성공 0, 실패 0 아님. `finalize(d)` 는 `d` 가 같은 항목을 등록 역순으로 부른다 (이 모델은 `d == nullptr` 이면 전부 — 모델의 선택). 펌웨어 스텁은 더 단순하게 **아무것도 저장하지 않고 0** 을 돌려준다(`fw_stubs.cpp`) — 끝나지 않는 프로그램에서 소멸자 목록은 RAM 낭비다. 반대로 호스트 시뮬레이터나 "펌웨어 종료 후 재진입" 테스트를 한다면 용량 고정 등록부가 필요하고, 넘치면 **조용히 버리지 말고 실패 + 카운터**를 남긴다.

`check_syms.sh` 실제 출력 (맥, clang thumbv7em, `allow_syms.txt` 기준):

```text
$ bash check_syms.sh; echo rc=$?                 $ bash check_syms.sh -DLEGACY; echo rc=$?
ok    __aeabi_memclr4                            DENY  _ZdlPvj
ok    __aeabi_uldivmod                           DENY  _Znwj
ok    hal_cycles64                               ok    __aeabi_memclr8
ok    hal_otp_offset                             ok    __aeabi_uldivmod
ok    hal_uart_write                             DENY  __cxa_atexit
fw_app.cpp : 0 symbol(s) outside allow_syms.txt  DENY  __cxa_guard_acquire
rc=0                                             DENY  __cxa_guard_release
                                                 DENY  __dso_handle
                                                 ok    hal_cycles64 … hal_uart_write
                                                 fw_app.cpp -DLEGACY: 6 symbol(s) outside allow_syms.txt
                                                 rc=1
```

`fw_app.cpp` 의 두 판 차이는 네 가지다: 가상 소멸자 → protected 비가상, 소멸자 있는 `Logger` → 자명 소멸 + `constinit`, lazy `static Cal` → `constinit Cal g_cal` + `cal_init()`, `new Frame` → 정적 풀 4개. `-O0` 으로 돌려도 고친 판은 0 이었다 (실측 — 최적화 수준에 기대는 게이트가 아니다). 허용 목록 한 줄 = "누가 주는지 확인했다" 는 리뷰 서명이다. `__cxa_pure_virtual` 이 들어 있는 이유는 스텁이 주기 때문이고, guard 가 **없는** 이유는 스텁이 있어도 새 lazy static 은 리뷰를 거치게 하려는 정책이다.

이제 VM 에서 진짜 링크 (`link_vm.sh`, arm-none-eabi-g++ 14.2.1, `-nostdlib -nostartfiles --gc-sections`). 1 단계는 실제 출력(중복 줄과 `collect2` 줄 생략), 2 단계부터는 남은 미정의 심볼만 요약했다:

```text
### 1. LEGACY, 라이브러리 없이
fw_app.cpp:(.text._Z3calv+0x14): undefined reference to `__cxa_guard_acquire'
fw_app.cpp:(.text._Z3calv+0x28): undefined reference to `__cxa_guard_release'
fw_app.cpp:(.text._ZN4UartD0Ev[_ZN4UartD5Ev]+0x6): undefined reference to `operator delete(void*, unsigned int)'
fw_app.cpp:(.text.app_main+0x28): undefined reference to `__aeabi_uldivmod'
fw_app.cpp:(.text.app_main+0x32): undefined reference to `operator new(unsigned int)'
fw_app.cpp:(.text.app_main+0xe): undefined reference to `memset'
fw_app.cpp:(.text.startup._GLOBAL__sub_I_g_uart+0x16): undefined reference to `__aeabi_atexit'
fw_app.cpp:(.text.startup._GLOBAL__sub_I_g_uart+0x20): undefined reference to `__dso_handle'
### 2. LEGACY + fw_stubs.o            → __aeabi_uldivmod, operator new, memset 만 남음
### 3. LEGACY + fw_stubs.o + -lgcc -lc → operator new(unsigned int) 만 남음
### 3b. + -lstdc++                     → newlib: undefined reference to `_sbrk' / `_kill' / `_getpid' / `_exit'
### 3c. clang 의 LEGACY 오브젝트 + fw_stubs.o + -lgcc -lc → operator new 만 남음 (__cxa_atexit, __aeabi_memclr8 해결)
### 4. 고친 판, 라이브러리 없이        → memset, __aeabi_uldivmod
### 5. 고친 판 + -lgcc -lc             → rc=0, 끌려온 멤버: _aeabi_uldivmod.o _udivmoddi4.o _dvmd_tls.o libc_a-memset.o
```

읽는 법:

- 1 → 2: 스텁 층(156 B text)이 C++ 런타임 심볼 5종(`__cxa_guard_acquire/release`, `operator delete(void*, unsigned)`, `__aeabi_atexit`, `__dso_handle`)을 모두 지웠다. 남은 셋 중 둘은 C 도우미(libgcc, libc)로 3 에서 해결된다.
- **`new` 는 스텁으로 주지 않았다** — 의도다. 힙을 쓰는 코드는 링크에서 걸려야 한다. 억지로 `-lstdc++` 를 붙이면(3b) `new` → `malloc` → `_sbrk`, 그리고 `bad_alloc` → `abort` → `_kill/_getpid/_exit` 시스템 콜 스텁까지 요구한다.
- 3c: clang 이 만든 오브젝트(`__cxa_atexit`, `__aeabi_memclr8` 요구)도 같은 스텁 + newlib 로 해결됐다 — 스텁이 두 이름을 다 주기 때문.

크기 (`arm-none-eabi-size`, 실측):

| 빌드 | text | data | bss | 무엇 |
|---|---|---|---|---|
| `L_real` LEGACY + 진짜 런타임 (libsupc++ + newlib + `-lnosys`, crt 대역) | 16501 | 1380 | 832 | |
| `L_stub_new` LEGACY + 스텁, `new` 만 libsupc++ | 16173 | 1376 | 440 | 차이 = guard/atexit 의 진짜 구현 |
| `L_stub_all` LEGACY + 스텁 + `new` 도 trap | **1508** | 20 | 24 | |
| `b2` 고친 판 + `-lgcc -lc` | **1292** | 12 | 268 | bss 256 B 는 정적 프레임 풀 |

**`new` 하나 = text 14 665 B + data 1356 B + bss 416 B** (`L_stub_new` − `L_stub_all`). `L_real` 의 큰 심볼이 이유를 보여 준다: `_malloc_r` 1422 B, `__gxx_personality_v0` 1140 B, `__gnu_unwind_execute` 768 B, `__malloc_av_` 1032 B(data). `-fno-exceptions` 로 빌드해도 **libsupc++ 의 `operator new` 는 예외를 켜고 빌드된 라이브러리**라 실패 시 `bad_alloc` 을 던지는 경로가 언와인더를 끌고 온다. 링크 맵이 사슬을 그대로 보여 준다 (`link_vm.sh` 의 "new 하나가 끌고 온 사슬", 경로 줄임, 실측 일부):

```text
libsupc++.a(new_op.o) app_legacy.o (operator new(unsigned int))
libsupc++.a(bad_alloc.o) libsupc++.a(new_op.o) (vtable for std::bad_alloc)
libsupc++.a(eh_alloc.o) libsupc++.a(new_op.o) (__cxa_allocate_exception)
libsupc++.a(eh_terminate.o) libsupc++.a(eh_alloc.o) (std::terminate())
libsupc++.a(eh_throw.o) libsupc++.a(new_op.o) (__cxa_throw)
libsupc++.a(si_class_type_info.o) libsupc++.a(bad_alloc.o) (vtable for __cxxabiv1::__si_class_type_info)
libgcc.a(unwind-arm.o) libsupc++.a(new_op.o) (__aeabi_unwind_cpp_pr0)
libc.a(libc_a-malloc.o) libsupc++.a(new_op.o) (malloc)
libc.a(libc_a-getenv.o) libsupc++.a(eh_alloc.o) (getenv)
```

(스크립트 출력 12줄과 같은 맵의 아래쪽에서 고른 줄.) `-fno-rtti` 로 빌드했는데 `si_class_type_info.o` 가 들어온 것도 같은 이유다 — `bad_alloc` 의 typeinfo. 예외 객체 할당기 `eh_alloc.o` 는 `getenv`, `strtoul` 까지 참조해서 newlib 의 `_impure_ptr`/stdio 구조체가 따라온다(같은 맵). 그리고 고친 판의 1292 B 중 808 B 는 `uptime_ms()` 의 64비트 나눗셈 하나(`__aeabi_uldivmod` 48 + `__udivmoddi4` 760)다 — 허용했지만 공짜는 아니다.

```svg
<svg viewBox="0 0 740 200" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">.text 크기 (바이트, 실측) — 막대 길이 = text / 30</text>
  <g fill="currentColor"><text x="10" y="56">진짜 런타임</text><text x="10" y="89">스텁 + libsupc++ new</text><text x="10" y="122">스텁 + new trap</text><text x="10" y="155">고친 판</text></g>
  <rect x="170" y="40" width="550" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="170" y="73" width="539" height="22" class="f-warn" stroke="currentColor"/>
  <rect x="170" y="106" width="50" height="22" class="f-acc" stroke="currentColor"/>
  <rect x="170" y="139" width="43" height="22" class="f-ok" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12"><text x="660" y="56">16501</text><text x="650" y="89">16173</text><text x="230" y="122">1508</text><text x="225" y="155">1292 (808 = 64비트 나눗셈)</text></g>
  <text x="10" y="188" fill="currentColor" font-size="12">operator new 하나가 malloc + bad_alloc + 언와인더를 끌고 온다 (-fno-exceptions 빌드여도)</text>
</svg>
```

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution     # 88/88 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sol_asan && ./sol_asan   # 88/88, 보고 없음
bash check_syms.sh            # exit 0      bash check_syms.sh -DLEGACY    # exit 1 (6 DENY)
```

하네스가 정말 잡는지 확인하려고 solution 사본에 버그를 넣어 돌렸다 (레슨 폴더 밖에서): acquire 의 PRIMASK 저장/복원 제거 → 2 FAIL (이중 생성), `aeabi_atexit` 가 `dso` 를 버림 → 2 FAIL, finalize 를 FIFO 로 → 1 FAIL, `normalize` 생략 → 4 FAIL, `__aeabi_ddiv` 를 정수로 분류 → 1 FAIL, release 가 busy 비트를 남김 → 5 FAIL, 고장 기록이 마지막 고장으로 덮어씀 → 2 FAIL, `aeabi_atexit` 인자 순서를 뒤바꿈 → 하네스가 **충돌(SIGILL, rc=132)** — 소멸자 자리의 객체 주소로 분기한 것. 실제 보드에서도 같은 모양으로 죽는다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `int __cxa_guard_acquire(…) { return 1; }` | 매번 1 → 생성자가 **매 호출마다** 다시 돈다 (빠른 경로 bit0 을 release 가 세우지 않으면) | 상태 기계 (Part B), 또는 `constinit` 으로 guard 자체를 없앤다 |
| `return 0;` 스텁 | 생성자가 영영 안 불린다 — 0 으로 채워진 객체를 쓴다 | 같음 |
| `-fno-threadsafe-statics` 를 "안전한 최적화" 로 | 락뿐 아니라 "초기화 중" 표시도 사라짐 → ISR 재진입 시 조용한 이중 생성 (연습 1) | IRQ 켜기 전에 초기화, 또는 스텁 유지 |
| `__aeabi_atexit` 를 `__cxa_atexit` 와 같은 인자 순서로 정의 | 객체와 소멸자 주소가 뒤바뀜 → finalize 시 분기 폭주 (주입 실험: SIGILL) | `return __cxa_atexit(dtor, obj, dso);` |
| clang 빌드만 보고 `__cxa_atexit` 만 스텁 | GCC 객체는 `__aeabi_atexit` 를 요구 (실측) | 둘 다 정의 |
| GCC 에서 링크가 되니 `__cxa_pure_virtual` 은 괜찮다 | 약한 미정의 → 슬롯 0 → 0번지 분기 | trap 스텁 정의 + 게이트가 `w` 도 셈 |
| `-ffreestanding` 이면 `memcpy` 가 안 나온다 | 구조체 복사·0 초기화는 백엔드가 `__aeabi_memcpy4`/`memset` 으로 (실측, `-fno-builtin` 도 무관) | libc 를 링크하거나 직접 제공 (자기 구현에서 루프가 다시 `memcpy` 로 바뀌지 않게 주의) |
| `throw` 만 지우고 `-fexceptions` 는 유지 | `__aeabi_unwind_cpp_pr0` + `.ARM.exidx` 가 남는다 (실측) | `-fno-exceptions` |
| 64비트 `/ 1000` 은 곱셈으로 바뀌겠지 | clang·GCC 모두 `bl __aeabi_uldivmod` (실측), 링크 시 808 B | 32비트 범위로 줄이거나 비용을 받아들이고 허용 목록에 |
| `new` 를 막으려고 `operator new` 를 스텁으로 줌 | 링크는 되지만 힙 사용이 보이지 않게 됨 | 스텁 주지 말고 링크 에러로 남긴다 (게이트 DENY) — trap 판을 주더라도 의도를 문서화 |
| 게이트에 `^__` 같은 넓은 패턴 | 새 런타임 의존성이 전부 통과 | 정확한 이름/좁은 정규식, 모르는 것은 DENY |

## 8. 면접 Q&A

<details>
<summary>Q1. 링크 에러에 __cxa_guard_acquire 가 나옵니다. 어디서 왔고, 빈 함수로 채워도 되나요?</summary>

함수 안 `static` 객체의 생성자가 constexpr 이 아닐 때 C++11 이 요구하는 "한 번만, 동시에 오면 기다리게" 초기화를 위해 컴파일러가 넣는 호출이다(ROW 1 실측). 빠른 경로는 인라인으로 guard 의 bit0 을 보고(`ldrb; dmb sy; lsls #31`), 미완료일 때만 `acquire → 생성자 → release`. 빈 함수는 안 된다: `return 1` 이면 release 도 비어 있으니 bit0 이 안 서서 **매 호출 재생성**, `return 0` 이면 **생성자가 영영 안 돈다**. 올바른 스텁은 "완료 → 0, 처음 → busy 표시 후 1, 초기화 중 → (단일 코어) trap" 이고 검사+표시만 인터럽트를 막는다. 꼬리: "아예 없애려면?" → `constinit` 전역 + 부팅 때 명시적 `init()`, 또는 `-fno-threadsafe-statics` (검출 능력을 잃는다는 대가), CPP-B09.

</details>

<details>
<summary>Q2. 같은 파일인데 clang 은 __cxa_atexit, GCC 는 __aeabi_atexit 를 요구합니다. 왜죠? 스텁은 어떻게?</summary>

ARM C++ ABI(CPPABI32)는 `__cxa_atexit` 대신 쓸 `__aeabi_atexit(object, destroyer, dso)` 를 정의한다. GCC 는 그걸 따르고(`r0=&g_log, r1=소멸자` 실측), clang 은 이 타깃에서도 Itanium 의 `__cxa_atexit(destroyer, object, dso)` 를 부른다(`r0=소멸자, r1=객체` 실측). 출처도 다르다: newlib libc 에 둘 다 있고, libsupc++ 에는 `__aeabi_atexit` 만 있다(nm 실측). 스텁은 둘 다 정의하고 하나를 다른 하나로 넘기되 앞 두 인자를 뒤바꿔야 한다. 펌웨어는 끝나지 않으니 실제 내용은 "저장 안 하고 0". 꼬리: "`__dso_handle` 은?" → 공유 객체를 구별하는 주소. 보통 `crtbegin.o` 가 주는데(nm 실측) `-nostartfiles` 면 사라져 링크 에러가 난다 — 정적 펌웨어에선 `void* __dso_handle = &__dso_handle;` 로 충분하다(Itanium: DSO 마다 다른 주소면 된다).

</details>

<details>
<summary>Q3. 전역 객체 소멸자 등록을 없애는 방법들을 비교해 보세요.</summary>

실측 기준: ① 타입을 자명 소멸로 + `constinit` — 등록도 생성자 호출도 없음, 최선. ② `NoDestroy<T>` (정렬된 버퍼 + placement new, 소멸자 미정의) — 표준 C++, 양쪽 컴파일러에서 생성자만 남음. ③ clang `-fno-c++-static-destructors` / `[[clang::no_destroy]]` — clang 에선 사라지지만 GCC 14 는 옵션을 모르고, 속성은 경고 후 무시해서 `__aeabi_atexit` 가 남았다. ④ `-fno-use-cxa-atexit` — clang 은 `atexit` 호출로, GCC 는 `.fini_array` 엔트리로 바뀔 뿐 소멸자 코드는 남는다. 꼬리: "소멸자가 안 불리면 문제 없나?" → [basic.start.term] 의 정적 소멸은 `main` 반환/`exit` 때만이고 펌웨어는 둘 다 안 한다. 소멸자에 "UART flush" 같은 의미 있는 일을 넣었다면 그건 리셋 전 훅으로 옮겨야 한다.

</details>

<details>
<summary>Q4. __cxa_pure_virtual 은 언제 불리고, 스텁은 무엇을 해야 하나요? GCC 는 왜 약한 참조죠?</summary>

추상 클래스의 생성자·소멸자 안에서 생성 중인 객체로 순수 가상 함수를 (간접적으로라도) 부를 때 — [class.abstract]/6 상 UB 다. 그때 vtable 슬롯에 있는 게 `__cxa_pure_virtual` 이다. 호스트 libc++abi 는 "Pure virtual function called!" 후 abort (실측, exit 134). 펌웨어 스텁은 고장 코드 + LR 을 리셋에도 남는 RAM 에 기록하고 `bkpt` → 무한 루프(WDT 리셋). 반환하면 안 된다. GCC 오브젝트에선 `w` (약한 미정의)였다 — 정의가 없으면 링크는 되지만 슬롯이 0 이 되어 호출 시 0번지 분기(CPP-M06). clang 은 강한 `U` 로 링크 실패. 그래서 게이트는 `w` 도 미정의로 센다. 꼬리: "`__cxa_deleted_virtual` 은?" → `= delete` 된 가상 함수 슬롯용, Itanium 은 "반환하지 않는다" 고 한다. 같은 trap 으로.

</details>

<details>
<summary>Q5. -fno-exceptions 로 빌드했는데 new 하나 때문에 바이너리가 16 KB 가 됐습니다. 왜죠?</summary>

`-fno-exceptions` 는 **내 코드**의 컴파일 방식이고, 링크되는 libsupc++ 의 `operator new` 는 예외를 켠 채 빌드된 라이브러리 코드다. 실패 시 `std::bad_alloc` 을 던지는 경로가 `__cxa_throw` → `__gxx_personality_v0` → libgcc 언와인더(`unwind-arm.o`)를 끌고 오고, 본체는 newlib `malloc`(`_malloc_r` 1422 B, `__malloc_av_` 1 KB data)을 쓴다. 실측: 같은 LEGACY 앱에서 `new` 만 libsupc++ 로 두면 text 16173 B, `new` 를 trap 으로 바꾸면 1508 B — **차이 14 665 B**. 해결은 힙을 안 쓰는 것(정적 풀, CPP-B14), 굳이 필요하면 내 `operator new` 를 정의해 라이브러리 것이 링크되지 않게. 꼬리: "nothrow new 는?" → `_ZnwjRKSt9nothrow_t` 도 libsupc++ 구현은 내부에서 던지는 판을 감싸는 경우가 있어 사슬이 같을 수 있다 — 이 레슨에서 확인하지 않았다(미확인). 링크 맵으로 확인할 것.

</details>

<details>
<summary>Q6. -ffreestanding 이면 C 라이브러리 호출이 안 생기는 거 아닌가요?</summary>

아니다. `-ffreestanding` 은 "표준 라이브러리 함수의 의미를 가정하지 말라"(예: `printf` 를 `puts` 로 바꾸지 않음)일 뿐, 컴파일러는 여전히 `memcpy/memset/memmove/memcmp` 가 존재한다고 가정하고 큰 복사·0 초기화를 그 호출로 내린다. 실측: 128 B 구조체 복사가 clang `__aeabi_memcpy4`, GCC `memcpy`, `-fno-builtin` 을 더해도 같았다. 64비트 나눗셈(`__aeabi_uldivmod`)과 soft-float(`__aeabi_dmul`)도 같은 부류 — C++ 런타임이 아니라 컴파일러 지원 루틴이고, 출처는 libgcc/compiler-rt 와 libc 다. 꼬리: "memcpy 를 직접 구현할 때 함정은?" → GCC 가 내 바이트 복사 루프를 다시 `memcpy` 호출로 인식해 무한 재귀를 만들 수 있다(`-fno-tree-loop-distribute-patterns` 가 흔한 대책 — 이 레슨에서 재현하진 않았다, 미확인).

</details>

<details>
<summary>Q7. throw 를 하나도 안 쓰는데 -fexceptions 를 켜 두면 손해가 있나요?</summary>

있다. 고친 `fw_app.cpp` 를 `-fexceptions` 로만 바꿔 게이트를 돌리면 `__aeabi_unwind_cpp_pr0` 이 새로 생겼다(실측). ARM EHABI 는 함수마다 `.ARM.exidx` 언와인드 엔트리를 두고, 그게 개인성 루틴을 참조하기 때문이다 — 링크하면 libgcc 언와인더가 따라온다. 그리고 guard 경로에는 `__cxa_guard_abort` + `__cxa_end_cleanup` + `__gxx_personality_v0` 가 붙는다(ROW 1). ARM 고유 이름에 주의: 호스트는 정리 경로가 `_Unwind_Resume`, ARM 은 `__cxa_end_cleanup`, 개인성 루틴은 clang `pr0` / GCC `pr1` 이었다. 크기 쪽은 CPP-B13 이 다뤘다.

</details>

<details>
<summary>Q8. 이걸 CI 에서 어떻게 지키나요? 무엇을 허용 목록에 넣죠?</summary>

오브젝트(또는 최종 ELF 전 단계)마다 `nm -u` → 허용 목록 대조 → 밖이면 실패 (`check_syms.sh`). 허용 목록은 **출처를 확인한 것만**: 프로젝트 심볼(정확한 접두), libgcc 정수 도우미, libc mem 함수, 내 스텁이 trap 으로 주는 `__cxa_pure_virtual`, 링커 스크립트 심볼. guard/atexit/new/delete/RTTI/EH/double 은 넣지 않는다 — 스텁이 있어도 새 사용처는 리뷰를 거치게. 모르는 `__` 이름은 기본 거부. 보완: 이전/이후 빌드의 미정의 집합 diff 로 "새로 생긴 의존성" 만 PR 에 코멘트(연습 2), 최종 ELF 에서 `nm -C --size-sort` 로 `malloc`/`__gxx_personality_v0` 같은 "있으면 안 되는 정의" 를 거꾸로 검사. 꼬리: "최적화 수준에 따라 결과가 달라지나?" → 이 파일은 `-O0`/`-O2` 모두 같았지만, 추상 기반 vtable 의 방출 여부 같은 것은 인라인에 달릴 수 있으니 출하 빌드 플래그로 검사한다.

</details>

## 9. 추가 연습

### 연습 1 — `-fno-threadsafe-statics` 가 지우는 것

clang 이 `-fno-threadsafe-statics` 로 내는 코드(`ldrb; cbz` → 생성자 → `strb #1`)를 `LazyNts::get()` 으로 옮기고, 생성자 도중 ISR 이 같은 static 을 쓰면 무슨 일이 생기는지 결정적 테스트로 보여라. 대책도 테스트로.

<details>
<summary>답 보기</summary>

```cpp
int* get() {
    if (guard == 0) {        // ldrb; cbz
        ++ctor_runs;         // bl ctor
        isr_window();        // 생성자 도중 IRQ
        value = 7;
        guard = 1;           // movs #1; strb — 생성이 끝난 "뒤" 에 표시
    }
    return &value;
}
```

ISR 이 생성 도중 들어오면 guard 는 아직 0 → ISR 도 생성자를 돈다: `ctor_runs == 2` (테스트는 이 결과를 **확인**한다 — 이게 플래그의 대가다). 스텁판(Part B)이면 같은 상황이 `GuardRecursion` 고장으로 **보인다**. 대책은 IRQ 켜기 전에 한 번 불러 두는 것: 그 뒤 ISR 은 빠른 경로만 탄다(`ctor_runs == 1`). 생성자 자체가 재진입에 안전하면(멱등) 이중 생성이 "틀리지 않을" 수도 있지만, 그건 우연에 기대는 설계다.

</details>

### 연습 2 — 새로 생긴 런타임 의존성만 (PR 코멘트용)

이전/이후 `nm -u` 출력에서, 프로젝트 심볼을 뺀 **새** 미정의 심볼만 순서대로 뽑는 `new_deps(before, after)` 를 써라.

<details>
<summary>답 보기</summary>

after 의 각 미정의 심볼이 before 에 없고 프로젝트 심볼이 아니면 추가 — O(n·m) 이면 충분하다(수십 줄). 실측 픽스처로: 고친 판 → LEGACY 는 7개(`_ZdlPvj, _Znwj, __aeabi_memclr8, __cxa_atexit, __cxa_guard_acquire, __cxa_guard_release, __dso_handle`), LEGACY → 고친 판은 `__aeabi_memclr4` 1개 — 정적 풀 배열로 바꾸면서 정렬이 4바이트가 되어 **이름이 바뀐** 것이다(`memclr8` → `memclr4`). 고친 판 → 고친 판 + `-fexceptions` 는 `__aeabi_unwind_cpp_pr0` 1개. "어떤 PR 이 무엇을 들여왔나" 를 이렇게 한 줄로 남기면 리뷰어가 출처를 묻게 된다.

</details>

### 연습 3 — guard ABI: Itanium vs ARM

`guard_size(Abi)` 와 `guard_is_done(bytes, Abi)` 를 써라. Itanium 은 8바이트·첫 바이트, ARM 은 4바이트·bit0.

<details>
<summary>답 보기</summary>

```cpp
constexpr std::size_t guard_size(Abi a) { return a == Abi::Arm ? 4u : 8u; }
static bool guard_is_done(const unsigned char* g, Abi a) {
    if (a == Abi::Itanium) return g[0] != 0;
    uint32_t w; std::memcpy(&w, g, 4); return (w & 1u) != 0;
}
```

크기는 `nm -S` 실측(thumbv7em 4, aarch64-none-elf 8). 테스트 포인트: ARM 워드 `0x100`(이 레슨 스텁의 busy)은 첫 바이트가 0 이라 미완료, `0x002` 는 Itanium 규칙(첫 바이트 ≠ 0)으로는 "완료" 지만 ARM 규칙(bit0)으로는 아님 — 런타임이 bit0 외의 비트를 쓸 수 있는 이유다. 빅 엔디언 ARM(BE8)에서 bit0 이 어느 바이트에 있는지와 컴파일러의 `ldrb` 오프셋은 확인하지 않았다(미확인).

</details>

### 연습 4 — 누가 주나: 링크 순서가 고른다

`link_vm.sh` 의 아카이브 `nm` 결과를 표로 만들고, 아카이브 검색 순서를 받아 처음 정의한 멤버를 돌려주는 `who_provides(sym, order)` 를 써라.

<details>
<summary>답 보기</summary>

순서대로 아카이브를 돌며 표에서 `(아카이브, 심볼)` 이 맞는 첫 멤버를 반환. 실측 표에서 `__aeabi_atexit` 는 libsupc++ `atexit_arm.o` 와 libc `libc_a-aeabi_atexit.o` 둘 다에 있다 → `-lsupc++ -lgcc -lc` 면 앞의 것, libsupc++ 없이 `-lc` 면 뒤의 것. `__cxa_atexit` 는 libsupc++ 에 없고 libc 에만. `__dso_handle` 은 어떤 아카이브에도 없고 `crtbegin.o` 에만 있다 — 그래서 `-nostartfiles` 링크에서 사라진다. (모델은 단순화다: GNU ld 는 아카이브를 만난 시점의 미정의 심볼로 멤버를 고르고, `--start-group` 없이 되돌아가지 않는다.)

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 16/16 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer practice.cpp -o pr_asan && ./pr_asan   # 16/16, 보고 없음
```

## 10. 핵심 정리

- 프리스탠딩 C++ 오브젝트의 `nm -u` 는 두 부류다: **C++ 런타임**(`__cxa_guard_*`, `__cxa_atexit`/`__aeabi_atexit`, `__dso_handle`, `__cxa_pure_virtual`, `_Znwj`/`_ZdlPvj`, RTTI, EH) — 없애거나 의미를 정한 스텁으로, **C 도우미**(`__aeabi_uldivmod`, `__aeabi_memcpy4`/`memcpy`, `__aeabi_dmul`) — 허용하되 비용(64비트 나눗셈 808 B)을 안다.
- `-ffreestanding`·`-fno-builtin` 은 이 호출들을 없애지 않는다. 64비트 상수 나눗셈도 `bl __aeabi_uldivmod`.
- 컴파일러마다 다르다: clang `__cxa_atexit`/강한 pure_virtual/`__aeabi_memcpy4`/`pr0`, GCC `__aeabi_atexit`(인자 순서 다름)/약한 `w` pure_virtual/`memcpy`/`pr1`. 스텁은 양쪽 이름을 다 준다.
- guard: ARM 은 4바이트 워드의 bit0 (컴파일러가 인라인으로 봄), Itanium 은 8바이트의 첫 바이트. 스텁 = 검사+busy 표시만 IRQ 차단, 생성자는 IRQ 켠 채, 초기화 중 재진입은 trap. `-fno-threadsafe-statics` 는 검출 능력까지 지운다.
- 전역 소멸자: 자명 소멸 + `constinit` 이 최선, 표준적인 대안은 `NoDestroy<T>`, clang 전용 플래그/속성은 GCC 에서 무시된다.
- `__cxa_pure_virtual` = 고장 기록 + trap (반환 금지). 약한 미정의는 링크가 돼도 슬롯 0.
- `new` 하나 = text +14.6 KB (malloc + bad_alloc + 언와인더, `-fno-exceptions` 여도). 스텁으로 주지 말고 링크 에러로 남겨라.
- 게이트: `check_syms.sh` + 좁은 허용 목록, 모르는 `__` 이름은 거부, `w` 도 미정의로.

## 참고자료

- C++ 표준 (N4861 C++20 초안): [stmt.dcl]/4 (블록 범위 static 초기화, 동시 진입 대기, 재귀 진입 UB), [basic.start.dynamic], [basic.start.term] (정적 객체 소멸 시점), [class.abstract]/6 (생성자 안 순수 가상 호출 UB), [basic.life], [expr.new], [dcl.constinit] — <https://isocpp.org/files/papers/N4861.pdf>
- Itanium C++ ABI — §2.8 Initialization Guard Variables, §3.2.6 Pure Virtual Function API, §3.2.7 Deleted Virtual Function API, §3.3.3 One-time Construction API, §3.3.6 DSO Object Destruction API (`__cxa_atexit`, `__cxa_finalize`, `__dso_handle`): <https://itanium-cxx-abi.github.io/cxx-abi/abi.html>
- ARM — C++ ABI for the Arm Architecture (CPPABI32): 4바이트 guard 와 최하위 비트, `__aeabi_atexit` 의 인자 순서와 모델 구현, 생성자가 `this` 를 반환: <https://github.com/ARM-software/abi-aa/blob/main/cppabi32/cppabi32.rst>
- ARM — Exception Handling ABI (EHABI32): `.ARM.exidx`, `__aeabi_unwind_cpp_pr0/1/2`: <https://github.com/ARM-software/abi-aa/blob/main/ehabi32/ehabi32.rst>
- ARM — Run-time ABI (RTABI32): `__aeabi_uldivmod`, `__aeabi_memcpy4/memclr8`, `__aeabi_dadd` 등 도우미 함수 목록: <https://github.com/ARM-software/abi-aa/blob/main/rtabi32/rtabi32.rst>
- GCC 매뉴얼 — C++ Dialect Options (`-fno-threadsafe-statics`, `-fno-use-cxa-atexit`, `-fsized-deallocation`): <https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html>
- Clang 매뉴얼 — `-fno-c++-static-destructors`, `[[clang::no_destroy]]`: <https://clang.llvm.org/docs/AttributeReference.html#no-destroy>
- 관련 레슨: CPP-B09 (정적 초기화·guard 비용), CPP-B13 (예외/RTTI 끄기의 크기), CPP-B14 (operator new 금지), CPP-M06 (가상 소멸자 → delete → malloc 사슬, 약한 pure_virtual 슬롯 0), CPP-M07 (placement new·`std::launder`), CPP-M11 (Result 로 예외 대체), CPP-A04 (`__atomic_*` 출처), MCPP-A05 (맹글링·ODR), CPP-B06 (PRIMASK 저장/복원)
