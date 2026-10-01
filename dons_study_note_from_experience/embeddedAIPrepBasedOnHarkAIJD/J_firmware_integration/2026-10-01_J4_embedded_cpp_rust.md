# J4. 임베디드 C++와 Rust — 예외/RTTI 없는 C++, constexpr·템플릿, Rust ownership·no_std·C FFI

> **이 노트를 다 읽으면**: 펌웨어에서 C++의 어떤 기능을 왜 끄는지(`-fno-exceptions -fno-rtti`, 힙, iostream, 정적 초기화 순서)를 실제 object 크기와 미해결 심볼로 설명할 수 있다 · RAII·`constexpr` LUT·`RingBuffer<T, N>`·타입 안전 레지스터·`std::span`·placement new로 힙 없는 C++ 추론 코드를 짤 수 있다 · Rust의 ownership/borrowing/lifetime을 C의 버퍼 소유권 규칙으로 번역하고 `no_std` int8 dense 커널을 C 레퍼런스와 bit-exact로 테스트할 수 있다 · Cortex-M4용 `no_std` Rust 바이너리를 빌드해 섹션 크기와 inner loop 어셈블리를 확인하고, C↔Rust↔C++ 경계를 C ABI로 설계할 수 있다
> **JD 연결**: "Integrate ML inference into embedded firmware written in **C, C++, or Rust**" — study_prep_list **J4**: C++: 예외/RTTI 없이, 템플릿, constexpr / Rust: ownership, `no_std`, embassy, C FFI. (P2지만 JD에 언어 이름이 그대로 나오는 키워드다.)
> **Don 기준 난이도**: C, 포인터, 링커 섹션, 정적 메모리 풀, 링버퍼, SSD FW의 "Embedded C/C++"는 이미 손에 익은 것 / C++17·20의 컴파일 타임 기능(`constexpr`, `static_assert`, 템플릿 파라미터로 크기 고정, `std::span`)을 펌웨어 관점으로 정리하는 것과 **Rust 전체**(소유권, borrow checker, `no_std`, cargo, FFI)는 새로 배움
> **선행 노트**: C1 (int8 requantization — 7절 커널을 Rust로 포팅한다), F2 (TFLM — C++ API: `MicroMutableOpResolver<N>`, `MicroInterpreter`, tensor arena), F3 (llama.cpp의 C API 경계), F7 (크로스 빌드·링커 스크립트·섹션 배치), E8 (SPSC 링버퍼), J1 (추론 통합 패턴 — 정적 arena, ISR → task)

---

## 0. 큰 그림 — 이게 왜 필요한가

edge ML 엔지니어가 "추론을 펌웨어에 통합한다"고 할 때 실제로 만지는 코드는 언어가 섞여 있다. 예를 들어 Hark 같은 웨어러블(추정)이라면 이런 모양이 흔하다.

- 맨 아래 HAL·드라이버·RTOS·vendor NPU SDK는 거의 **C**다 (CMSIS, 벤더 HAL, Zephyr/FreeRTOS, Qualcomm·Arm NPU 드라이버 — 대부분 C 또는 C++ API를 제공한다. 제품마다 확인할 것).
- 추론 런타임은 **C++**인 경우가 많다. TFLM(F2)이 C++이고, ONNX Runtime·LiteRT도 C++ 구현 위에 C API를 얹는다.
- 그 사이의 전처리(MFCC, 필터), 후처리(smoothing, threshold), 링버퍼, 상태 머신은 팀이 고른 언어 — C, C++, 최근에는 **Rust**도 후보가 된다.

그래서 JD가 "C, C++, or Rust"라고 쓴다. 세 언어를 다 깊게 알 필요는 없지만, (1) C++을 펌웨어답게 "작고 결정적으로" 쓰는 규칙, (2) Rust가 무엇을 보장하고 무엇을 대가로 요구하는지, (3) 언어 경계를 어떻게 안전하게 넘는지는 설명할 수 있어야 한다.

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="20" width="640" height="40" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="340" y="45" font-size="13" text-anchor="middle">앱 로직 · 상태 머신 (C / C++ / Rust — 팀 선택)</text>
<line x1="20" y1="78" x2="660" y2="78" stroke="currentColor" stroke-dasharray="6 4"/>
<text x="655" y="73" font-size="12" text-anchor="end">C ABI 경계 (extern "C")</text>
<rect x="20" y="92" width="200" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="120" y="120" font-size="13" text-anchor="middle">전처리</text>
<text x="120" y="140" font-size="12" text-anchor="middle">MFCC · 필터 · 링버퍼 (C/Rust)</text>
<rect x="240" y="92" width="200" height="66" rx="6" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="340" y="120" font-size="13" text-anchor="middle">추론 런타임</text>
<text x="340" y="140" font-size="12" text-anchor="middle">TFLM (C++) 또는 직접 짠 커널</text>
<rect x="460" y="92" width="200" height="66" rx="6" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="560" y="120" font-size="13" text-anchor="middle">후처리</text>
<text x="560" y="140" font-size="12" text-anchor="middle">smoothing · threshold (C/Rust)</text>
<line x1="220" y1="125" x2="240" y2="125" stroke="currentColor"/>
<line x1="440" y1="125" x2="460" y2="125" stroke="currentColor"/>
<line x1="20" y1="176" x2="660" y2="176" stroke="currentColor" stroke-dasharray="6 4"/>
<text x="655" y="171" font-size="12" text-anchor="end">C ABI 경계 (vendor SDK 헤더)</text>
<rect x="20" y="190" width="640" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/>
<text x="340" y="217" font-size="13" text-anchor="middle">HAL · 드라이버 · RTOS · vendor NPU/DSP SDK (대부분 C, 일부 C++)</text>
<rect x="20" y="250" width="640" height="40" rx="6" fill="#888" fill-opacity="0.15" stroke="#888"/>
<text x="340" y="275" font-size="13" text-anchor="middle">하드웨어: Cortex-M MCU · DSP · NPU · 센서 (IMU, 마이크)</text>
</svg>
```

그림 1 — 추론 펌웨어의 언어 지도(예시). 파랑 = C가 지배하는 층, 주황 = C++ 런타임, 초록 = 팀이 언어를 고를 수 있는 층. 언어가 바뀌는 곳(점선)은 거의 항상 **C ABI**로 넘는다 — C++ 클래스나 Rust 타입을 경계 너머로 직접 보내지 않는다.

Don의 경험으로 번역하면: SSD 펌웨어에서 "Embedded C/C++"라고 할 때 C++은 보통 **클래스 = 구조체 + 함수, 예외·RTTI·STL 컨테이너 금지** 수준의 부분집합이었을 것이다. 이 노트는 그 부분집합을 "왜 그런지"를 숫자로 확인하고, C++17·20이 펌웨어에 주는 진짜 이득(컴파일 타임 계산과 검사)을 추가한다. Rust는 "그 규칙들을 사람이 코드 리뷰로 지키는 대신 컴파일러가 강제하는 언어"로 보면 이해가 빠르다.

| 절 | 내용 | 실행 예제 |
|---|---|---|
| 1 | 펌웨어 C++ 부분집합: 예외·RTTI·힙·iostream·정적 초기화·virtual | 크기 측정, 미해결 심볼, 초기화 순서 버그 |
| 2–5 | RAII, `constexpr` LUT, 템플릿(링버퍼·레지스터), `std::span`·placement new, TFLM 연결 | 6개 |
| 6 | Rust 기초: ownership·borrowing·lifetime·Option/Result·slice·unsafe | 실제 `rustc` 에러 4개 + 실행 2개 |
| 7–8 | `no_std` int8 dense 커널, C 레퍼런스와 bit-exact `cargo test`, Cortex-M4 바이너리 | 테스트, 섹션 크기, 디스어셈블 |
| 9 | 언어 경계: C→Rust, Rust→C→C++ (TFLM shim 모양) | 2개 |
| 10–11 | 임베디드 Rust 생태계, 언어 선택 표 | — |

모든 C++ 예제는 Apple clang 21(`c++`)로, Rust 예제는 rustc/cargo 1.99.0으로 실제 컴파일·실행했다. Cortex-M4 결과는 `--target=thumbv7em-none-eabihf -mcpu=cortex-m4`로 크로스 컴파일한 object/ELF를 `xcrun llvm-size`, `llvm-nm`, `llvm-objdump`로 본 것이다 (보드에서 돌린 것은 아니다).

---

## 1. 펌웨어용 C++ — 무엇을 쓰고 무엇을 피하나

### 1.1 직관: "비용이 보이는 기능"과 "비용이 숨는 기능"

C++ 기능은 두 종류로 나뉜다.

- **zero-overhead**: 쓰지 않으면 비용이 0이고, 쓰면 손으로 C를 짠 것과 같은 비용. 클래스, 생성자/소멸자(RAII), 템플릿, `constexpr`, `enum class`, 참조, 네임스페이스, `std::array`, `std::span`. 이것들은 컴파일러가 다 풀어 버려서 기계어에 흔적이 거의 없다.
- **런타임이 딸려 오는 기능**: 예외(unwinder + 테이블 + 예외 객체용 힙), RTTI(`typeid`, `dynamic_cast` — 타입 정보 테이블), `new`/`delete`(힙), iostream(locale·버퍼), 동적 초기화가 필요한 전역 객체(시작 코드가 생성자 목록을 돌린다). 비용이 소스에 안 보이고 **링크할 때** 드러난다.

펌웨어 C++ 규칙은 결국 "두 번째 그룹을 끄고, 첫 번째 그룹을 적극적으로 쓴다"다. Bjarne Stroustrup의 표현으로 zero-overhead 원칙은 "쓰지 않는 것에 비용을 내지 않고, 쓰는 것은 손으로 더 잘 짤 수 없다"이다.

### 1.2 기능별 판정표

| 기능 | 비용(펌웨어 관점) | 판정 | 대안 |
|---|---|---|---|
| 예외 (`throw`/`try`) | unwind 테이블(`.ARM.exidx/.ARM.extab`), unwinder 라이브러리, 예외 객체 할당(`__cxa_allocate_exception`), 던질 때 시간 비결정적 | 끈다 (`-fno-exceptions`) | 에러 코드, `enum class Status`, C++23 `std::expected` 류 |
| RTTI (`dynamic_cast`, `typeid`) | 클래스마다 type_info와 이름 문자열(flash), `__dynamic_cast` 런타임 | 끈다 (`-fno-rtti`) | enum 태그, virtual 함수로 질의 |
| 힙 (`new`, `std::vector`, `std::string`) | 단편화, 실패 처리, 비결정적 시간, 메모리 예산 증명 어려움 | 초기화 이후 금지가 일반적 | 정적 배열, arena + placement new, 고정 용량 컨테이너 |
| iostream (`std::cout`) | locale·스트림 객체·정적 초기화 — 수십 KB 이상 커지는 일이 흔하다 | 금지 | `printf` 계열 경량 구현, 로그 ID(defmt류) |
| 동적 초기화 전역 객체 | 시작 코드가 생성자 실행, TU 간 순서 미정(1.5절) | 피한다 | `constexpr`/`constinit` 생성자, 명시적 `init()` |
| 함수 내부 `static` 객체 | 스레드 안전 guard(`__cxa_guard_acquire`) | 주의 | `-fno-threadsafe-statics` 또는 전역 `constexpr` |
| virtual 함수 | 객체당 vptr 4 B, vtable(flash), 간접 호출 → 인라인 불가 | 필요한 곳만 | 템플릿/CRTP로 정적 다형성 |
| 템플릿 | 인스턴스마다 코드 복제 → flash 증가 가능 | 쓴다, 크기 감시 | 크기 무관 부분을 비템플릿 base로 분리 |
| `constexpr`, `static_assert` | 런타임 비용 0, 오히려 코드 감소 | 적극 사용 | — |

### 1.3 예제 1 — 예외·RTTI를 끄면 무엇이 사라지나 (Cortex-M4 object)

무엇을 확인하는 코드인지: virtual 함수 + `dynamic_cast` + `throw`가 들어간 작은 센서 클래스 계층을 Cortex-M4용으로 세 가지 플래그 조합으로 컴파일하고, 섹션 크기와 **링커가 채워야 할 미해결 심볼**을 비교한다.

```cpp
// shapes.cpp — 작은 클래스 계층: virtual + dynamic_cast + throw
struct Sensor {
    virtual ~Sensor() = default;
    virtual int read() = 0;
};
struct Imu : Sensor { int read() override { return 42; } };
struct Mic : Sensor { int read() override { return 7; } };
struct ReadError { int code; };

int poll(Sensor &s) {
#if defined(__cpp_exceptions)
    int v = s.read();
    if (v < 0) throw ReadError{v};          // 예외로 에러 전달
    return v;
#else
    int v = s.read();
    return v < 0 ? -1 : v;                  // 에러 코드로 전달
#endif
}
int is_imu([[maybe_unused]] Sensor &s) {
#if defined(__cpp_rtti)
    return dynamic_cast<Imu *>(&s) != nullptr;   // RTTI 필요
#else
    return 0;
#endif
}
Imu g_imu;
Mic g_mic;
int app() {
#if defined(__cpp_exceptions)
    try { return poll(g_imu) + poll(g_mic) + is_imu(g_mic); }
    catch (const ReadError &e) { return e.code; }
#else
    return poll(g_imu) + poll(g_mic) + is_imu(g_mic);
#endif
}
```

```sh
T="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -std=c++17 -Os -ffreestanding -Wall -Wextra"
c++ $T -c shapes.cpp -o full.o
c++ $T -fno-exceptions -c shapes.cpp -o noexc.o
c++ $T -fno-exceptions -fno-rtti -c shapes.cpp -o noexc_nortti.o
xcrun llvm-size full.o noexc.o noexc_nortti.o
for f in full noexc noexc_nortti; do echo "== $f: undefined symbols"; xcrun llvm-nm -u -C $f.o; done
```

```text
   text	   data	    bss	    dec	    hex	filename
    451	     12	      0	    463	    1cf	full.o
    360	     12	      0	    372	    174	noexc.o
    250	     12	      0	    262	    106	noexc_nortti.o
== full: undefined symbols
         U vtable for __cxxabiv1::__class_type_info
         U vtable for __cxxabiv1::__si_class_type_info
         U operator delete(void*, unsigned int)
         U __aeabi_unwind_cpp_pr0
         U __cxa_allocate_exception
         U __cxa_atexit
         U __cxa_begin_catch
         U __cxa_end_catch
         U __cxa_throw
         U __dso_handle
         U __dynamic_cast
         U __gxx_personality_v0
== noexc: undefined symbols
         U vtable for __cxxabiv1::__class_type_info
         U vtable for __cxxabiv1::__si_class_type_info
         U operator delete(void*, unsigned int)
         U __cxa_atexit
         U __dso_handle
         U __dynamic_cast
== noexc_nortti: undefined symbols
         U operator delete(void*, unsigned int)
         U __cxa_atexit
         U __dso_handle
```

출력에서 볼 것: object 자체는 451 → 360 → 250 바이트로 줄어든다(`full.o`의 451 중 unwind 테이블 `.ARM.exidx/.ARM.extab`이 108 B, `_ZTI`/`_ZTS` RTTI 테이블이 69 B — `llvm-size -A`로 섹션별 합산). 하지만 **진짜 비용은 아래 심볼 목록**이다. `U`(undefined)는 "링커가 라이브러리에서 끌어와야 하는 것"이다. `__cxa_throw`, `__gxx_personality_v0`, `__aeabi_unwind_cpp_pr0`이 보이면 링커는 libsupc++/libgcc(또는 libc++abi/libunwind)의 unwinder 전체를 끌어온다. 이 노트 환경엔 ARM용 C++ 런타임 라이브러리가 없어서 최종 링크 크기는 재지 못했지만, Arm GNU 툴체인에서 예외를 쓰는 프로그램은 이 런타임 때문에 **수십 KB 단위**로 커지는 것이 흔히 보고된다 (툴체인·newlib-nano 여부·`--gc-sections`에 따라 크게 다르니 자기 프로젝트의 map 파일로 확인할 것).

흥미로운 점이 두 개 더 있다. 예외·RTTI를 다 꺼도 남는 `operator delete`와 `__cxa_atexit`이다.

- `operator delete(void*, unsigned int)`: **virtual 소멸자** 때문이다. 컴파일러는 virtual 소멸자가 있으면 "deleting destructor"(`D0`)를 vtable에 넣고, 그 안에서 `operator delete`를 부른다. 힙을 전혀 안 써도 심볼이 필요해진다.
- `__cxa_atexit` / `__dso_handle`: 소멸자가 trivial하지 않은 **전역 객체**(`g_imu`, `g_mic`)의 소멸자를 프로그램 종료 시 부르려고 등록하는 코드다. 펌웨어는 `main`에서 돌아오지 않으니 쓸모없는 코드다.

### 1.4 예제 2 — 펌웨어식으로 고쳐 쓰기: 미해결 심볼 0개

무엇을 확인하는 코드인지: 소멸자를 `protected` non-virtual로, 전역 객체를 `constexpr` 생성자로, `dynamic_cast` 대신 enum 태그로 바꾸면 런타임 의존성이 완전히 사라지는지.

```cpp
// fw.cpp — 펌웨어식: virtual 소멸자 없음(정적 객체만), 에러 코드, RTTI 대신 enum 태그
struct Sensor {
    enum class Kind : unsigned char { Imu, Mic };
    const Kind kind;
    virtual int read() = 0;
protected:
    explicit constexpr Sensor(Kind k) : kind(k) {}
    ~Sensor() = default;                    // non-virtual, protected → delete via base 불가
};
struct Imu final : Sensor { constexpr Imu() : Sensor(Kind::Imu) {} int read() override { return 42; } };
struct Mic final : Sensor { constexpr Mic() : Sensor(Kind::Mic) {} int read() override { return 7; } };

int poll(Sensor &s) { int v = s.read(); return v < 0 ? -1 : v; }
int is_imu(const Sensor &s) { return s.kind == Sensor::Kind::Imu; }

Imu g_imu;   // trivially destructible + constexpr ctor → 정적 초기화(.data), atexit 없음
Mic g_mic;
int app() { return poll(g_imu) + poll(g_mic) + is_imu(g_mic); }
```

```sh
c++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -std=c++17 -Os -ffreestanding \
    -Wall -Wextra -fno-exceptions -fno-rtti -c fw.cpp -o fw.o
xcrun llvm-size fw.o && echo "== undefined:" && xcrun llvm-nm -u -C fw.o; echo "(end)"
```

```text
   text	   data	    bss	    dec	    hex	filename
    148	     16	      0	    164	     a4	fw.o
== undefined:
(end)
```

출력에서 볼 것: text가 148 B, 미해결 심볼 **0개**. 이 object는 C 런타임조차 필요 없다. `data` 16 B는 두 객체(각각 vptr 4 B + `kind` 1 B → 정렬해서 8 B)다. 생성자가 `constexpr`이고 소멸자가 trivial이라 컴파일러가 객체를 **이미 초기화된 상태로 `.data`에 구워 넣었다** — 시작 코드가 생성자를 돌릴 필요가 없다. `final`은 "더 이상 파생 없음"을 알려 줘서, 정적 타입이 `Imu&`인 호출은 간접 호출 대신 직접 호출(심지어 인라인)이 가능해진다.

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">Cortex-M4 object .text 크기 (bytes, -Os) 와 미해결 런타임 심볼 수</text>
<line x1="200" y1="35" x2="200" y2="235" stroke="currentColor"/>
<text x="190" y="62" font-size="12" text-anchor="end">예외 + RTTI</text>
<rect x="200" y="45" width="360.8" height="28" fill="#d0564a"/>
<text x="568" y="64" font-size="12">451 B · 심볼 12개</text>
<text x="190" y="112" font-size="12" text-anchor="end">-fno-exceptions</text>
<rect x="200" y="95" width="288.0" height="28" fill="#e08a3c"/>
<text x="496" y="114" font-size="12">360 B · 심볼 6개</text>
<text x="190" y="162" font-size="12" text-anchor="end">+ -fno-rtti</text>
<rect x="200" y="145" width="200.0" height="28" fill="#4a7bd0"/>
<text x="408" y="164" font-size="12">250 B · 심볼 3개</text>
<text x="190" y="212" font-size="12" text-anchor="end">펌웨어식 재작성</text>
<rect x="200" y="195" width="118.4" height="28" fill="#3f9a6b"/>
<text x="326" y="214" font-size="12">148 B · 심볼 0개</text>
<text x="200" y="252" font-size="12">막대 = object 크기. 최종 크기를 좌우하는 것은 심볼이 끌어오는 런타임 라이브러리다.</text>
</svg>
```

그림 2 — 같은 기능을 하는 코드의 object 크기와 런타임 의존성. 막대 길이는 실제 측정값에 비례(0.8 px/B). object 차이는 수백 바이트지만, 심볼 12개 쪽은 링크 시 unwinder·RTTI 런타임을 통째로 끌어온다.

### 1.5 virtual 호출의 비용 — 얼마나 비싼가

virtual 호출 `s.read()`는 기계어로 대략 "객체에서 vptr 로드 → vtable에서 함수 주소 로드 → 간접 분기(`blx rN`)"다. Cortex-M4에서 로드 두 번과 간접 분기는 몇 사이클이고, 그 자체는 크지 않다. 진짜 비용은 **인라인이 막힌다**는 것이다. 센서 드라이버 `read()`를 샘플마다 부르는 정도는 문제없지만, 추론 커널의 inner loop(MAC 한 번마다)에서 virtual 함수를 부르면 루프 벡터화·레지스터 할당이 전부 깨진다.

규칙: **다형성은 바깥(초기화·드라이버 선택)에서, inner loop는 템플릿이나 함수 포인터 한 번 선택 후 직접 루프로.** TFLM도 같은 구조다 — op마다 커널 함수 포인터(`TFLMRegistration`의 `invoke`)를 한 번 부르고, 커널 안은 평범한 루프다.

### 1.6 예제 3 — 정적 초기화 순서 문제 (static initialization order fiasco)

무엇을 확인하는 코드인지: 서로 다른 소스 파일(TU)의 전역 객체가 서로를 참조할 때, **링크 순서만 바꿔도** 결과가 달라지는지.

```cpp
// cfg.cpp
int read_board_rev() { volatile int r = 3; return r; }  // 런타임에 레지스터를 읽는다고 가정
int g_board_rev = read_board_rev();         // TU 1의 전역 (동적 초기화)
constexpr int kSampleRateHz = 16000;        // 상수 초기화 (순서 문제 없음)
int g_rate = kSampleRateHz;
```

```cpp
// model.cpp
#include <cstdio>
extern int g_board_rev, g_rate;
struct ModelConfig {
    int rev, rate;
    ModelConfig() : rev(g_board_rev), rate(g_rate) {}   // 다른 TU의 전역을 읽는다
};
ModelConfig g_cfg;                          // TU 2의 전역 (동적 초기화)
int main() { std::printf("g_cfg.rev=%d g_cfg.rate=%d\n", g_cfg.rev, g_cfg.rate); }
```

```sh
F="-std=c++17 -Wall -Wextra -O2 -fno-exceptions -fno-rtti"
c++ $F cfg.cpp model.cpp -o order1 && ./order1
c++ $F model.cpp cfg.cpp -o order2 && ./order2
```

```text
g_cfg.rev=3 g_cfg.rate=16000
g_cfg.rev=0 g_cfg.rate=16000
```

출력에서 볼 것: 같은 코드인데 링크 순서를 바꾸자 `rev`가 3에서 **0**이 됐다. `g_cfg`의 생성자가 `g_board_rev`의 초기화보다 먼저 돌았고, 그때 `g_board_rev`는 아직 `.bss`의 0이었다. 반면 `g_rate`는 두 경우 모두 16000이다 — 상수식으로 초기화되는 전역은 컴파일 타임에 `.data`에 구워지므로 순서 문제가 없다.

펌웨어에서 이게 무서운 이유: 표준은 TU 간 동적 초기화 순서를 정하지 않으므로, Makefile에 파일 하나를 추가하거나 링커 버전이 바뀌면 조용히 바뀐다. 게다가 동적 초기화는 `main` 전에 시작 코드(`__libc_init_array` 등)가 돌리므로, 아직 클럭·SDRAM·캐시 설정이 끝나지 않은 상태일 수도 있다. 해결책:

- 전역 객체는 `constexpr` 생성자로 **상수 초기화**만 되게 한다. C++20 `constinit`을 붙이면 "동적 초기화가 필요하면 컴파일 에러"로 강제할 수 있다.
- 하드웨어를 읽어야 하는 초기화는 `main`에서 명시적 `init()` 호출 순서로 한다 — C 펌웨어에서 하던 그대로.

### 1.7 함정

- `-fno-exceptions`에서 `new`가 실패하면? 예외를 던질 수 없으니 구현에 따라 abort된다. 힙을 쓰지 않는 게 답이고, 꼭 써야 하면 `new (std::nothrow)`로 nullptr을 검사한다.
- `-fno-rtti`로 빌드한 코드와 RTTI가 필요한 라이브러리를 섞으면 링크 에러나 `dynamic_cast` 실패가 난다. vendor SDK가 C++이면 그 SDK의 빌드 플래그를 먼저 확인한다.
- 함수 안의 `static Foo foo;`는 스레드 안전 초기화 guard(`__cxa_guard_acquire/release`)를 부른다. RTOS 없는 bare-metal이면 `-fno-threadsafe-statics`로 끄는 게 흔하다.
- 템플릿은 인스턴스마다 코드가 복제된다. `RingBuffer<int16_t, 64>`와 `RingBuffer<int16_t, 128>`은 별개 함수다. map 파일에서 템플릿 심볼이 많아지면 크기와 무관한 부분을 비템플릿 base로 뺀다.

---

## 2. RAII — 힙 없이 "스코프 = 자원 수명"

### 2.1 직관

RAII(Resource Acquisition Is Initialization)는 이름이 거창하지만 뜻은 단순하다. **생성자에서 자원을 잡고 소멸자에서 놓는다.** 객체가 스코프를 벗어나면 컴파일러가 소멸자 호출을 모든 출구(정상 return, early return, `break`)에 자동으로 넣어 준다. 힙은 필요 없다 — 스택 객체로 충분하다.

C 펌웨어에서 `irq_save()` 후 함수 중간의 early return에서 `irq_restore()`를 빼먹어 인터럽트가 영영 꺼진 버그 — Don도 한 번쯤 봤을 것이다. RAII는 그 버그를 구조적으로 없앤다. 펌웨어에서 RAII로 감쌀 만한 것: 인터럽트 마스크(critical section), mutex, 주변장치 클럭 게이트, chip-select 핀, DMA 채널 예약, 전력 도메인 on 요청, 프로파일링 타이머.

### 2.2 예제 4 — critical section과 클럭 게이트 (호스트 시뮬레이션)

무엇을 확인하는 코드인지: early return이 있어도 PRIMASK가 복원되는지, 스코프를 벗어나면 클럭 비트가 꺼지는지, 그리고 RAII 객체의 크기.

```cpp
#include <cstdint>
#include <cstdio>
// 호스트 시뮬레이션: PRIMASK와 클럭 게이트 레지스터를 변수로 흉내 낸다
static uint32_t PRIMASK = 0, RCC_APB2ENR = 0;
static uint32_t get_primask() { return PRIMASK; }
static void set_primask(uint32_t v) { PRIMASK = v; }

class CriticalSection {                  // 생성 = 인터럽트 끔, 소멸 = 이전 상태 복원
    uint32_t saved_;
public:
    CriticalSection() : saved_(get_primask()) { set_primask(1); }
    ~CriticalSection() { set_primask(saved_); }
    CriticalSection(const CriticalSection &) = delete;            // 복사 금지
    CriticalSection &operator=(const CriticalSection &) = delete;
};
class ClockGate {                        // 주변장치 클럭 on/off를 스코프에 묶는다
    uint32_t bit_;
public:
    explicit ClockGate(uint32_t bit) : bit_(bit) { RCC_APB2ENR |= bit_; }
    ~ClockGate() { RCC_APB2ENR &= ~bit_; }
    ClockGate(const ClockGate &) = delete;
    ClockGate &operator=(const ClockGate &) = delete;
};
static int counter = 0;
static int bump(int n) {
    CriticalSection cs;                  // 함수 어느 return으로 나가도 복원된다
    if (n < 0) return -1;                // early return
    counter += n;
    std::printf("  inside: PRIMASK=%u\n", (unsigned)PRIMASK);
    return counter;
}
int main() {
    std::printf("bump(5)=%d, PRIMASK after=%u\n", bump(5), (unsigned)PRIMASK);
    std::printf("bump(-1)=%d, PRIMASK after=%u\n", bump(-1), (unsigned)PRIMASK);
    {
        ClockGate spi1(1u << 12);
        std::printf("in scope: RCC_APB2ENR=0x%04x\n", (unsigned)RCC_APB2ENR);
    }
    std::printf("after scope: RCC_APB2ENR=0x%04x, sizeof(CriticalSection)=%zu\n",
                (unsigned)RCC_APB2ENR, sizeof(CriticalSection));
}
```

```sh
c++ -std=c++17 -Wall -Wextra -O2 -fno-exceptions -fno-rtti raii.cpp -o raii && ./raii
```

```text
  inside: PRIMASK=1
bump(5)=5, PRIMASK after=0
bump(-1)=-1, PRIMASK after=0
in scope: RCC_APB2ENR=0x1000
after scope: RCC_APB2ENR=0x0000, sizeof(CriticalSection)=4
```

출력에서 볼 것: early return(`bump(-1)`)에서도 PRIMASK가 0으로 돌아왔다. 객체 크기는 저장한 PRIMASK 값 4 B뿐 — vptr도 숨은 필드도 없다. 복사 생성자를 `= delete`로 막은 이유: critical section 객체가 복사되면 소멸자가 두 번 돌아 PRIMASK를 두 번 복원하는 버그가 생긴다. "자원을 가진 객체는 복사 금지, 필요하면 move만"이 RAII의 짝 규칙이다 (Rust는 이걸 언어 기본값으로 만들었다 — 6절).

### 2.3 Cortex-M4에서 실제로 무엇이 나오나

같은 클래스를 진짜 `mrs/cpsid/msr` 인라인 어셈블리로 바꿔 Cortex-M4용 `-S`로 컴파일하면(`@APP` 주석 줄은 생략):

```cpp
static inline uint32_t get_primask() { uint32_t r; __asm volatile("mrs %0, primask" : "=r"(r)); return r; }
static inline void set_primask(uint32_t v) { __asm volatile("msr primask, %0" :: "r"(v) : "memory"); }
static inline void disable_irq() { __asm volatile("cpsid i" ::: "memory"); }
// class CriticalSection { ... 위와 같음, 생성자에서 disable_irq() ... };
volatile uint32_t g_count;
void bump(uint32_t n) { CriticalSection cs; g_count = g_count + n; }
```

```text
_Z4bumpj:                               @ @_Z4bumpj
	movw	r2, :lower16:g_count
	mrs	r1, primask
	cpsid i
	movt	r2, :upper16:g_count
	ldr	r3, [r2]
	add	r0, r3
	str	r0, [r2]
	msr	primask, r1
	bx	lr
```

출력에서 볼 것: "객체"는 레지스터 `r1` 하나로 사라졌다. C로 `uint32_t s = __get_PRIMASK(); __disable_irq(); ...; __set_PRIMASK(s);`를 손으로 쓴 것과 같은 명령열이다. 이것이 zero-overhead의 실물이다. (`"memory"` clobber가 있어서 컴파일러가 `g_count` 접근을 critical section 밖으로 옮기지 못한다 — 이걸 빼면 RAII든 C든 틀린 코드가 된다.)

---

## 3. `constexpr` — 테이블을 컴파일 타임에 만든다

### 3.1 왜 constexpr 테이블인가

펌웨어에는 테이블이 많다: 활성화 함수 LUT(sigmoid/tanh int8), mel filterbank 가중치, 창 함수(Hann), CRC 테이블, 고정소수점 역수 표. 지금까지의 방법은 둘이었다.

1. **부팅 시 계산**: `init()`에서 `expf`로 채운다 → RAM을 차지하고, 부팅이 느려지고, float 라이브러리가 링크된다.
2. **Python 스크립트로 생성한 `.h`**: flash에 들어가지만, 스크립트와 C 코드가 따로 놀아 파라미터(scale, 크기)가 어긋나도 아무도 모른다.

`constexpr` 함수로 테이블을 만들면 **컴파일러가 빌드 중에 계산해서 `.rodata`(flash)에 상수로 넣는다.** 파라미터는 같은 소스에 있고, `static_assert`로 핵심 값을 빌드 때 검사한다. 런타임 비용 0, RAM 0, float 라이브러리 링크 0.

### 3.2 int8 sigmoid LUT의 정의 (손계산)

TFLite의 int8 LOGISTIC 출력은 scale = 1/256, zero-point = −128로 고정하는 관례가 있다(C1 참고). 입력 scale을 0.1(zero-point 0)이라 하면 입력 `q_in ∈ [−128, 127]`은 실수 `x = 0.1·q_in`이다.

```
q_out = clamp( round( σ(0.1·q_in) · 256 ) − 128, −128, 127 )
σ(x) = 1 / (1 + e^(−x))
```

말로 하면: 입력 정수를 실수로 펴고, sigmoid를 구하고, 출력 눈금(1/256)으로 다시 접은 뒤 zero-point를 더한다. 가능한 입력이 256개뿐이므로 전부 미리 계산해 표로 둔다.

손계산 세 점:

- `q_in = 0` → σ(0) = 0.5 → 0.5·256 = 128 → 128 − 128 = **0**
- `q_in = 10` → σ(1.0) = 0.73106 → 187.15 → 187 − 128 = **59**
- `q_in = 20` → σ(2.0) = 0.88080 → 225.48 → 225 − 128 = **97**

### 3.3 예제 5 — constexpr LUT, static_assert, Python 대조

무엇을 확인하는 코드인지: (1) `constexpr`로 exp까지 직접 구현해 256칸 LUT를 컴파일 타임에 만들 수 있는지, (2) 틀린 기대값을 `static_assert`가 빌드에서 잡는지, (3) 표 전체가 Python(numpy)과 같은지.

```cpp
#include <array>
#include <cstdint>
#include <cstdio>
// int8 sigmoid LUT: 입력 q_in (scale 0.1, zp 0) → 출력 q_out (scale 1/256, zp -128, TFLite logistic 관례)
constexpr double cexp(double x) {                 // constexpr exp: x = k·ln2 + r, |r| ≤ ln2/2
    constexpr double LN2 = 0.6931471805599453;
    int k = static_cast<int>(x / LN2 + (x >= 0 ? 0.5 : -0.5));
    double r = x - k * LN2, term = 1.0, sum = 1.0;
    for (int n = 1; n < 25; ++n) { term *= r / n; sum += term; }   // Taylor
    for (; k > 0; --k) sum *= 2.0;
    for (; k < 0; ++k) sum *= 0.5;
    return sum;
}
constexpr std::array<int8_t, 256> make_sigmoid_lut(double in_scale) {
    std::array<int8_t, 256> t{};
    for (int i = 0; i < 256; ++i) {
        int q_in = i - 128;                                   // 인덱스 0 ↔ q_in = -128
        double y = 1.0 / (1.0 + cexp(-q_in * in_scale));
        int q = static_cast<int>(y * 256.0 + 0.5) - 128;      // y ≥ 0 이라 +0.5 후 절삭 = 반올림
        t[i] = static_cast<int8_t>(q > 127 ? 127 : (q < -128 ? -128 : q));
    }
    return t;
}
constexpr auto kSigmoid = make_sigmoid_lut(0.1);      // 컴파일 타임에 256바이트 완성

static_assert(kSigmoid[128] == 0, "sigmoid(0)=0.5 -> 128-128 = 0");
static_assert(kSigmoid[0] == -128 && kSigmoid[255] == 127, "saturates at both ends");
static_assert(kSigmoid[138] == 64, "sigmoid(1.0)=0.731 -> round(187.1)-128 = 59?");

int8_t sigmoid_s8(int8_t q_in) { return kSigmoid[static_cast<uint8_t>(q_in + 128)]; }

int main() {
    uint32_t h = 2166136261u;                                 // FNV-1a 체크섬
    for (int8_t v : kSigmoid) { h ^= static_cast<uint8_t>(v); h *= 16777619u; }
    std::printf("q_in=-20,-10,0,10,20 -> %d %d %d %d %d\n", sigmoid_s8(-20), sigmoid_s8(-10),
                sigmoid_s8(0), sigmoid_s8(10), sigmoid_s8(20));
    std::printf("fnv1a=0x%08x\n", h);
}
```

먼저 일부러 틀린 기대값(64)을 넣고 빌드하면:

```text
lut.cpp:28:15: error: static assertion failed due to requirement 'kSigmoid[138] == 64': sigmoid(1.0)=0.731 -> round(187.1)-128 = 59?
   28 | static_assert(kSigmoid[138] == 64, "sigmoid(1.0)=0.731 -> round(187.1)-128 = 59?");
      |               ^~~~~~~~~~~~~~~~~~~
lut.cpp:28:29: note: expression evaluates to '59 == 64'
   28 | static_assert(kSigmoid[138] == 64, "sigmoid(1.0)=0.731 -> round(187.1)-128 = 59?");
      |               ~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~~~~
1 error generated.
```

컴파일러가 표를 실제로 계산해서 "59 == 64"가 거짓이라고 알려 준다. 기대값을 손계산대로 59로 고치고 실행, 같은 계산을 Python으로도 한다.

```python
import numpy as np
q_in = np.arange(-128, 128)
y = 1.0 / (1.0 + np.exp(-q_in * 0.1))
q = np.clip(np.floor(y * 256.0 + 0.5).astype(np.int64) - 128, -128, 127).astype(np.int8)
h = 2166136261
for v in q.view(np.uint8):
    h = ((h ^ int(v)) * 16777619) & 0xFFFFFFFF
print("q_in=-20,-10,0,10,20 ->", *[int(q[i + 128]) for i in (-20, -10, 0, 10, 20)])
print(f"fnv1a=0x{h:08x}")
```

```text
$ c++ -std=c++17 -Wall -Wextra -O2 -fno-exceptions -fno-rtti lut.cpp -o lut && ./lut
q_in=-20,-10,0,10,20 -> -97 -59 0 59 97
fnv1a=0xde1c978c
$ .venv/bin/python lut.py
q_in=-20,-10,0,10,20 -> -97 -59 0 59 97
fnv1a=0xde1c978c
```

출력에서 볼 것: 손계산(0, 59, 97)과 일치하고, 256칸 전체의 체크섬 `0xde1c978c`가 C++ 컴파일 타임 계산과 numpy 사이에 같다. 즉 constexpr `cexp`(Taylor 24항 + 2의 거듭제곱 범위 축소)가 이 반올림 해상도에서는 libm `exp`와 구별되지 않는다. 경계(정확히 .5인 값) 근처에서 반올림 방식이 다르면 1 LSB 차이가 날 수 있으니, 실제 프로젝트에서는 이런 체크섬/전수 비교를 CI에 넣는다.

### 3.4 Cortex-M4에서: 표는 `.rodata`, 코드는 3명령

이 크로스 환경에는 ARM용 libc++ 헤더가 없어서 `std::array`를 같은 모양의 최소 구조체로 바꿔 넣고 `main`을 뺀 뒤 컴파일했다 (Arm GNU 툴체인에는 `<array>`가 있으므로 실제 프로젝트에선 그대로 쓰면 된다).

```text
$ xcrun llvm-size -A lut_m4.o
section             size   addr
.text                 18      0
.ARM.exidx             8      0
.rodata              256      0
...
$ xcrun llvm-objdump -d --no-show-raw-insn -C lut_m4.o
00000000 <sigmoid_s8(signed char)>:
       0:      	uxtb	r0, r0
       2:      	movw	r1, #0x0
       6:      	eor	r0, r0, #0x80
       a:      	movt	r1, #0x0
       e:      	ldrsb	r0, [r1, r0]
      10:      	bx	lr
```

출력에서 볼 것: `.rodata` 정확히 256 B가 표다. `cexp`와 `make_sigmoid_lut`는 기계어에 **존재하지 않고**, double 연산 라이브러리(`__aeabi_dmul` 등) 참조도 없다(`xcrun llvm-nm -u lut_m4.o`의 출력이 비어 있다 — 미해결 심볼 0). 런타임 함수는 "+128 해서 인덱스(`eor #0x80`) → 바이트 로드" 뿐이다. `movw/movt #0x0`은 링커가 채울 표 주소 자리다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="250" x2="630" y2="250" stroke="currentColor"/>
<line x1="70" y1="30" x2="70" y2="250" stroke="currentColor"/>
<line x1="70" y1="139.6" x2="630" y2="139.6" stroke="#888" stroke-dasharray="3 3"/>
<line x1="351.1" y1="30" x2="351.1" y2="250" stroke="#888" stroke-dasharray="3 3"/>
<line x1="212.7" y1="30" x2="212.7" y2="250" stroke="#d0564a" stroke-dasharray="5 4"/>
<line x1="465.3" y1="30" x2="465.3" y2="250" stroke="#d0564a" stroke-dasharray="5 4"/>
<polyline fill="none" stroke="#4a7bd0" stroke-width="2.5" points="70.0,250.0 78.8,250.0 87.6,250.0 96.4,250.0 105.1,250.0 113.9,250.0 122.7,250.0 131.5,250.0 140.3,250.0 149.1,250.0 157.8,250.0 166.6,250.0 175.4,250.0 184.2,250.0 193.0,250.0 201.8,250.0 210.5,250.0 219.3,249.1 228.1,249.1 236.9,249.1 245.7,248.3 254.5,247.4 263.3,245.7 272.0,244.0 280.8,241.4 289.6,237.1 298.4,231.9 307.2,223.3 316.0,212.9 324.7,199.1 333.5,181.8 342.3,161.1 351.1,139.6 359.9,118.0 368.7,97.3 377.5,80.0 386.2,66.2 395.0,55.9 403.8,47.3 412.6,42.1 421.4,37.8 430.2,35.2 438.9,33.5 447.7,31.7 456.5,30.9 465.3,30.0 474.1,30.0 482.9,30.0 491.6,30.0 500.4,30.0 509.2,30.0 518.0,30.0 526.8,30.0 535.6,30.0 544.4,30.0 553.1,30.0 561.9,30.0 570.7,30.0 579.5,30.0 588.3,30.0 597.1,30.0 605.8,30.0 614.6,30.0 623.4,30.0 630.0,30.0"/>
<text x="70" y="268" font-size="12" text-anchor="middle">-128</text>
<text x="210.5" y="268" font-size="12" text-anchor="middle">-64</text>
<text x="351.1" y="268" font-size="12" text-anchor="middle">0</text>
<text x="491.6" y="268" font-size="12" text-anchor="middle">64</text>
<text x="630" y="268" font-size="12" text-anchor="middle">127</text>
<text x="350" y="290" font-size="13" text-anchor="middle">q_in (x = 0.1·q_in)</text>
<text x="62" y="254" font-size="12" text-anchor="end">-128</text>
<text x="62" y="144" font-size="12" text-anchor="end">0</text>
<text x="62" y="35" font-size="12" text-anchor="end">127</text>
<text x="20" y="20" font-size="13">q_out</text>
<text x="140" y="230" font-size="12" text-anchor="middle">-128 포화: 66칸</text>
<text x="550" y="55" font-size="12" text-anchor="middle">127 포화: 76칸</text>
</svg>
```

그림 3 — 컴파일 타임에 만든 int8 sigmoid LUT 256칸(4칸 간격으로 그린 실제 값). 입력 scale 0.1로는 x ∈ [−12.8, 12.7]을 덮지만 sigmoid는 |x| > 약 6에서 이미 포화되어, 256칸 중 142칸이 양 끝 값이고 서로 다른 출력값은 84개뿐이다. 실제로는 변환기가 calibration으로 입력 scale을 정하므로 이렇게 낭비되지 않는다 — 표의 모양은 scale에 따라 달라지고, 그래서 표를 scale과 같은 소스에서 만드는 것이 안전하다.

### 3.5 같은 방식으로 만들 수 있는 것들

- **mel filterbank**: 삼각 필터 n_mels × (n_fft/2+1) 가중치. `constexpr`로 Hz↔mel 변환(log10이 필요 — `cexp`처럼 직접 구현하거나 급수)을 해서 Q15 표를 만든다. 대부분 0이므로 실제로는 (시작 bin, 길이, 가중치들) 형태의 희소 표로 만든다.
- **Hann 창**, **CRC32 표**, **비트 역순 인덱스(FFT)**: 정수만 쓰면 constexpr이 특히 쉽다.
- 한계: C++20까지 `<cmath>` 함수는 constexpr이 아니다(C++23/26에서 일부 constexpr화가 진행 중이지만 컴파일러 지원을 확인해야 한다). 복잡한 표(학습된 weight)는 여전히 Python에서 생성하되, **생성된 표에 대한 불변식**(합이 1, 단조 증가, 크기)을 `static_assert`로 검사하는 조합이 실용적이다.
- constexpr 계산이 너무 길면 컴파일러가 단계 수 한도(`-fconstexpr-steps`, GCC는 `-fconstexpr-ops-limit`)에서 멈춘다. 수천 항목 표는 괜찮지만 수십만 항목이면 생성 스크립트가 낫다.

---

## 4. 템플릿 — 크기를 타입에 넣는다

### 4.1 직관

C에서 고정 크기 링버퍼를 여러 개 쓰려면 매크로로 찍어 내거나, `struct ring { uint8_t *buf; uint32_t size; ... }`처럼 크기를 런타임 변수로 둔다. 후자는 `idx % size`가 진짜 나눗셈이 되고, 크기가 2의 거듭제곱인지 아무도 검사하지 않는다.

템플릿 파라미터로 크기를 넘기면(`RingBuffer<T, N>`) **N이 타입의 일부**가 된다. 그러면 (1) `N - 1` 마스크가 컴파일 타임 상수가 되어 `and` 한 명령이 되고, (2) `static_assert`로 "N은 2의 거듭제곱"을 빌드에서 강제하고, (3) 배열이 객체 안에 들어가 `.bss`에 정적으로 놓인다 — 힙이 없다. C++의 "템플릿 = 타입 안전한 매크로"라는 설명이 펌웨어에선 정확하다.

### 4.2 예제 6 — `RingBuffer<T, N>` (SPSC, ISR → task)

무엇을 확인하는 코드인지: 용량이 꽉 찼을 때 push가 거절되는지, 32비트 인덱스가 2³² 경계를 넘어도 개수 계산이 맞는지, 2의 거듭제곱이 아닌 N을 빌드가 거부하는지. E8에서 본 SPSC 링버퍼를 C++ 템플릿으로 옮긴 것이다.

`ring.hpp`:

```cpp
#pragma once
#include <atomic>
#include <cstddef>
#include <cstdint>
// SPSC 링버퍼: 생산자 1(ISR), 소비자 1(task). 힙 없음, 크기는 컴파일 타임 상수.
template <typename T, std::size_t N>
class RingBuffer {
    static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
    static_assert(std::atomic<uint32_t>::is_always_lock_free, "need lock-free 32-bit atomics");
    T buf_[N];
    std::atomic<uint32_t> head_{0};   // 생산자만 쓴다
    std::atomic<uint32_t> tail_{0};   // 소비자만 쓴다
    static constexpr uint32_t kMask = N - 1;
public:
    bool push(const T &v) {                                   // ISR 쪽
        uint32_t h = head_.load(std::memory_order_relaxed);
        if (h - tail_.load(std::memory_order_acquire) == N) return false;   // full
        buf_[h & kMask] = v;
        head_.store(h + 1, std::memory_order_release);        // 데이터 쓴 "뒤"에 공개
        return true;
    }
    bool pop(T &out) {                                        // task 쪽
        uint32_t t = tail_.load(std::memory_order_relaxed);
        if (head_.load(std::memory_order_acquire) == t) return false;       // empty
        out = buf_[t & kMask];
        tail_.store(t + 1, std::memory_order_release);
        return true;
    }
    std::size_t size() const { return head_.load() - tail_.load(); }
    static constexpr std::size_t capacity() { return N; }
};
```

`ring_test.cpp`:

```cpp
#include "ring.hpp"
#include <cstdio>
struct ImuSample { int16_t ax, ay, az; uint32_t ts; };
static RingBuffer<ImuSample, 8> g_q;            // .bss에 정적으로 놓인다
int main() {
    int pushed = 0;
    for (int i = 0; i < 10; ++i)                 // 8개까지만 들어간다
        pushed += g_q.push({int16_t(i), int16_t(-i), 1000, uint32_t(i * 10)});
    ImuSample s; int popped = 0; long sum = 0;
    while (g_q.pop(s)) { ++popped; sum += s.ax; }
    std::printf("pushed=%d popped=%d sum_ax=%ld sizeof(queue)=%zu\n",
                pushed, popped, sum, sizeof(g_q));
    // 32비트 인덱스 wrap-around: 2^32 경계를 넘어도 h - t가 맞는지
    uint32_t h = 2, t = 0xFFFFFFFEu;
    std::printf("wrap: head=%u tail=%u -> count=%u\n", h, t, h - t);
}
```

```text
$ c++ -std=c++17 -Wall -Wextra -O2 -fno-exceptions -fno-rtti ring_test.cpp -o ring_test && ./ring_test
pushed=8 popped=8 sum_ax=28 sizeof(queue)=104
wrap: head=2 tail=4294967294 -> count=4
```

`RingBuffer<int, 100>`을 선언하면:

```text
In file included from ring_bad.cpp:1:
./ring.hpp:8:29: error: static assertion failed due to requirement '(100UL & (100UL - 1)) == 0': N must be a power of two
    8 |     static_assert(N >= 2 && (N & (N - 1)) == 0, "N must be a power of two");
      |                             ^~~~~~~~~~~~~~~~~~
ring_bad.cpp:2:22: note: in instantiation of template class 'RingBuffer<int, 100>' requested here
    2 | RingBuffer<int, 100> bad;
      |                      ^
./ring.hpp:8:43: note: expression evaluates to '96 == 0'
```

출력에서 볼 것: 10개를 넣으려 했지만 8개만 들어갔고(꽉 차면 `false`, 덮어쓰지 않음), 0+1+…+7 = 28이 정확히 나왔다. 크기 104 B = 샘플 12 B × 8 + 인덱스 4 B × 2 — 숨은 필드가 없다. wrap 줄: head = 2, tail = 2³² − 2일 때 `head − tail`을 unsigned로 빼면 4다(−2, −1, 0, 1 네 칸). 인덱스를 0..N−1로 접지 않고 **계속 증가시키고 쓸 때만 `& mask`** 하는 이 방식은 "full과 empty 구분용 빈 칸"이 필요 없어서 N칸을 전부 쓴다 — 단 N이 2의 거듭제곱이어야 2³² wrap과 맞물린다. 그래서 static_assert가 필요하다.

```svg
<svg viewBox="0 0 680 230" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">RingBuffer&lt;T, 8&gt; — head = 11, tail = 6 → count = head − tail = 5, slot = index &amp; 7</text>
<rect x="100" y="90" width="60" height="50" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/>
<rect x="160" y="90" width="60" height="50" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/>
<rect x="220" y="90" width="60" height="50" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/>
<rect x="280" y="90" width="60" height="50" fill="none" stroke="currentColor"/>
<rect x="340" y="90" width="60" height="50" fill="none" stroke="currentColor"/>
<rect x="400" y="90" width="60" height="50" fill="none" stroke="currentColor"/>
<rect x="460" y="90" width="60" height="50" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/>
<rect x="520" y="90" width="60" height="50" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/>
<text x="130" y="120" font-size="12" text-anchor="middle">idx 8</text>
<text x="190" y="120" font-size="12" text-anchor="middle">idx 9</text>
<text x="250" y="120" font-size="12" text-anchor="middle">idx 10</text>
<text x="490" y="120" font-size="12" text-anchor="middle">idx 6</text>
<text x="550" y="120" font-size="12" text-anchor="middle">idx 7</text>
<text x="130" y="158" font-size="12" text-anchor="middle">0</text>
<text x="190" y="158" font-size="12" text-anchor="middle">1</text>
<text x="250" y="158" font-size="12" text-anchor="middle">2</text>
<text x="310" y="158" font-size="12" text-anchor="middle">3</text>
<text x="370" y="158" font-size="12" text-anchor="middle">4</text>
<text x="430" y="158" font-size="12" text-anchor="middle">5</text>
<text x="490" y="158" font-size="12" text-anchor="middle">6</text>
<text x="550" y="158" font-size="12" text-anchor="middle">7</text>
<text x="60" y="158" font-size="12" text-anchor="end">slot</text>
<line x1="490" y1="55" x2="490" y2="86" stroke="#3f9a6b" stroke-width="2"/>
<polygon points="484,80 496,80 490,89" fill="#3f9a6b"/>
<text x="490" y="48" font-size="12" text-anchor="middle">tail = 6 (task가 pop)</text>
<line x1="310" y1="200" x2="310" y2="146" stroke="#e08a3c" stroke-width="2"/>
<polygon points="304,152 316,152 310,143" fill="#e08a3c"/>
<text x="310" y="216" font-size="12" text-anchor="middle">head = 11 → 다음 쓰기 slot 11 &amp; 7 = 3 (ISR이 push)</text>
<path d="M 580 115 Q 620 115 620 180 Q 620 190 100 190 Q 80 190 80 140" fill="none" stroke="#888" stroke-dasharray="4 3"/>
<text x="640" y="175" font-size="12" text-anchor="end">wrap</text>
</svg>
```

그림 4 — 인덱스는 계속 증가하고 배열 위치는 `& (N−1)`로 접는다. 파란 칸 5개가 차 있는 데이터(idx 6..10). 생산자(ISR)는 head만, 소비자(task)는 tail만 쓰므로 lock 없이 안전하다(single-core Cortex-M에서 정렬된 32비트 load/store는 원자적이다). release/acquire 순서는 "데이터를 쓴 뒤에 head를 공개"를 컴파일러·CPU에 강제한다.

함정: Cortex-M4는 single-core라 `memory_order`가 대부분 컴파일러 재배치 방지로만 작동하지만, Cortex-M7(캐시·쓰기 버퍼)이나 멀티코어 SoC, DMA가 개입하면 barrier(`dmb`)와 캐시 유지보수가 별도로 필요하다(E7, E8 참고).

### 4.3 예제 7 — 타입 안전 레지스터 접근

무엇을 확인하는 코드인지: 레지스터 비트 필드를 "타입"으로 정의하면 (1) 생성되는 코드가 C 매크로와 같은지, (2) 읽기 전용 필드에 쓰기를 **컴파일 에러**로 막을 수 있는지.

C 펌웨어에서 흔한 버그: `SPI1->SR |= BIT7;` — 상태 레지스터(읽기 전용, 혹은 write-1-to-clear)에 실수로 read-modify-write. 템플릿으로 필드마다 접근 권한을 타입에 새기면 이 버그가 빌드에서 걸린다.

`reg.hpp`:

```cpp
#pragma once
#include <stdint.h>
enum class Access { RO, WO, RW };
// 레지스터 하나의 비트 필드를 "타입"으로 표현한다. 런타임 객체는 없다.
template <uintptr_t Addr, unsigned Off, unsigned Width, Access A>
struct Field {
    static_assert(Off + Width <= 32, "field exceeds 32-bit register");
    static constexpr uint32_t mask = ((Width == 32) ? 0xFFFFFFFFu : ((1u << Width) - 1u)) << Off;
    static volatile uint32_t &reg() { return *reinterpret_cast<volatile uint32_t *>(Addr); }
    static uint32_t read() {
        static_assert(A != Access::WO, "field is write-only");
        return (reg() & mask) >> Off;
    }
    static void write(uint32_t v) {
        static_assert(A != Access::RO, "field is read-only");
        reg() = (reg() & ~mask) | ((v << Off) & mask);       // read-modify-write
    }
};
// 가상의 IMU용 SPI 주변장치 (주소·비트 배치는 예시)
namespace spi1 {
constexpr uintptr_t CR1 = 0x40013000, SR = 0x40013008;
using BaudDiv = Field<CR1, 3, 3, Access::RW>;   // CR1[5:3]
using Enable  = Field<CR1, 6, 1, Access::RW>;   // CR1[6]
using Busy    = Field<SR, 7, 1, Access::RO>;    // SR[7]
}
```

`reg_use.cpp`:

```cpp
#include "reg.hpp"
void spi_setup() {
    spi1::BaudDiv::write(5);       // fPCLK/64
    spi1::Enable::write(1);
}
bool spi_busy() { return spi1::Busy::read(); }
#ifdef BAD
void oops() { spi1::Busy::write(0); }   // 읽기 전용 필드에 쓰기 시도
#endif
```

```text
$ c++ --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -std=c++17 -O2 -ffreestanding \
      -fno-exceptions -fno-rtti -Wall -Wextra -c reg_use.cpp -o reg_use.o
$ xcrun llvm-objdump -d --no-show-raw-insn -C reg_use.o
00000000 <spi_setup()>:
       0:      	movw	r0, #0x3000
       4:      	movt	r0, #0x4001
       8:      	ldr	r1, [r0]
       a:      	movs	r2, #0x5
       c:      	bfi	r1, r2, #3, #3
      10:      	str	r1, [r0]
      12:      	ldr	r1, [r0]
      14:      	orr	r1, r1, #0x40
      18:      	str	r1, [r0]
      1a:      	bx	lr

0000001c <spi_busy()>:
      1c:      	movw	r0, #0x3008
      20:      	movt	r0, #0x4001
      24:      	ldr	r0, [r0]
      26:      	uxtb	r0, r0
      28:      	lsrs	r0, r0, #0x7
      2a:      	bx	lr
$ c++ ... -DBAD -fsyntax-only reg_use.cpp
./reg.hpp:15:23: error: static assertion failed due to requirement '(Access)0 != Access::RO': field is read-only
   15 |         static_assert(A != Access::RO, "field is read-only");
      |                       ^~~~~~~~~~~~~~~
reg_use.cpp:8:27: note: in instantiation of member function 'Field<1073819656, 7, 1, Access::RO>::write' requested here
    8 | void oops() { spi1::Busy::write(0); }   // 읽기 전용 필드에 쓰기 시도
      |                           ^
```

출력에서 볼 것: `BaudDiv::write(5)`가 Cortex-M4의 비트 필드 삽입 명령 **`bfi r1, r2, #3, #3`** 한 개로 내려갔다 — 손으로 쓴 `(reg & ~0x38) | (5 << 3)`보다 오히려 짧다. volatile이라 두 필드 쓰기가 각각 별도의 `ldr/str`로 남은 것도 정확하다(volatile 접근은 합치거나 생략하면 안 된다). 그리고 `Busy::write`는 빌드 에러다. 이 아이디어를 크게 키운 것이 Rust의 PAC(svd2rust가 SVD 파일에서 생성, 10절)와 C++의 여러 레지스터 라이브러리다.

---

## 5. `std::array`, `std::span`, placement new — 그리고 TFLM이 쓰는 방식

### 5.1 세 가지 도구

| 도구 | C에서의 대응 | 펌웨어 이득 |
|---|---|---|
| `std::array<T, N>` | `T a[N]` | 값으로 복사·비교 가능, `size()`가 타입에 있음, 포인터로 decay하지 않음. 힙 없음. |
| `std::span<T>` (C++20) | `(T *p, size_t n)` 짝 | 포인터+길이를 **한 객체**로. 함수 인자에서 길이를 잃어버리는 버그 제거. 크기 = 포인터 2개. |
| placement new `new (p) T(...)` | 메모리 풀에서 꺼낸 블록에 `init_xxx(p)` | 이미 있는 메모리(정적 arena)에 생성자를 실행. 힙 사용 0. |

말로 하면: `std::span`은 C에서 늘 쓰던 "버퍼 포인터와 길이"를 한 덩어리로 묶은 것이고, placement new는 "메모리는 내가 준비했으니 생성자만 돌려 달라"는 문법이다.

### 5.2 예제 8 — 정적 arena + placement new + span으로 만든 미니 dense 층

무엇을 확인하는 코드인지: TFLM의 tensor arena처럼 정적 바이트 배열 하나에서 객체와 버퍼를 정렬 맞춰 잘라 쓰고, 커널이 span만 받아 계산하는지. 공간이 모자라면 예외 대신 nullptr.

```cpp
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <new>       // placement new
#include <span>
// TFLM의 tensor arena를 흉내 낸 bump allocator: 해제 없음, 정렬 보장, 실패 시 nullptr
class Arena {
    std::span<std::byte> mem_;
    std::size_t used_ = 0;
public:
    explicit Arena(std::span<std::byte> m) : mem_(m) {}
    void *alloc(std::size_t n, std::size_t align) {
        std::size_t off = (used_ + align - 1) & ~(align - 1);
        if (off + n > mem_.size()) return nullptr;      // 예외 대신 nullptr
        used_ = off + n;
        return mem_.data() + off;
    }
    template <class T, class... Args> T *make(Args &&...args) {
        void *p = alloc(sizeof(T), alignof(T));
        return p ? new (p) T(static_cast<Args &&>(args)...) : nullptr;   // placement new
    }
    std::size_t used() const { return used_; }
};
struct DenseLayer {                   // 커널은 길이 정보가 붙은 span만 받는다
    std::span<const int8_t> w; int n_in, n_out;
    DenseLayer(std::span<const int8_t> w_, int ni, int no) : w(w_), n_in(ni), n_out(no) {}
    void run(std::span<const int8_t> x, std::span<int32_t> acc) const {
        for (int o = 0; o < n_out; ++o) {
            auto row = w.subspan(o * n_in, n_in);
            int32_t s = 0;
            for (std::size_t i = 0; i < row.size(); ++i) s += int32_t(row[i]) * x[i];
            acc[o] = s;
        }
    }
};
alignas(16) static std::byte g_arena[256];
static constexpr std::array<int8_t, 6> kW = {127, -76, 30, -56, 127, 32};   // 2×3, flash(.rodata)
int main() {
    Arena arena{g_arena};
    auto *layer = arena.make<DenseLayer>(std::span<const int8_t>(kW), 3, 2);
    auto *x = static_cast<int8_t *>(arena.alloc(3, 1));
    auto *acc = static_cast<int32_t *>(arena.alloc(2 * sizeof(int32_t), alignof(int32_t)));
    x[0] = 64; x[1] = 96; x[2] = -32;
    layer->run({x, 3}, {acc, 2});
    std::printf("acc = [%d, %d], arena used = %zu / %zu bytes\n", acc[0], acc[1],
                arena.used(), sizeof g_arena);
    std::printf("big alloc -> %p\n", arena.alloc(1000, 4));
    static_assert(sizeof(std::span<const int8_t>) == 2 * sizeof(void *), "span = ptr + len");
}
```

```text
$ c++ -std=c++20 -Wall -Wextra -O2 -fno-exceptions -fno-rtti arena.cpp -o arena && ./arena
acc = [-128, 7584], arena used = 36 / 256 bytes
big alloc -> 0x0
```

출력에서 볼 것: 손계산 — 첫 행 127·64 + (−76)·96 + 30·(−32) = 8128 − 7296 − 960 = **−128**, 둘째 행 −56·64 + 127·96 + 32·(−32) = −3584 + 12192 − 1024 = **7584**. arena 36 B의 내역: `DenseLayer` 24 B(span 16 B + int 2개 8 B, 8 B 정렬) → x 3 B(오프셋 24..26) → acc는 4 B 정렬이라 오프셋 28로 올라가서 8 B → 36. 정렬 패딩 1 B가 보인다. 1000 B 요청은 nullptr — 예외 없이 실패를 돌려준다. 이 노트의 호스트는 64비트라 span이 16 B지만 Cortex-M에선 8 B다.

### 5.3 TFLM은 이 도구들을 이렇게 쓴다 (F2 연결)

F2에서 본 TFLM 애플리케이션 골격을 이 노트의 어휘로 다시 읽으면:

| TFLM 코드 | 이 노트의 개념 |
|---|---|
| `alignas(16) uint8_t tensor_arena[kArenaSize];` | 정적 arena (예제 8의 `g_arena`) |
| `tflite::MicroMutableOpResolver<4> resolver;` | **템플릿 파라미터로 용량 고정** — 등록 가능한 op 수 N이 타입에 들어가 내부 배열 크기가 정해진다. 넘치면 `AddXxx()`가 에러 상태를 반환(예외 아님) |
| `resolver.AddFullyConnected();` 등의 반환값 `TfLiteStatus` | 예외 대신 상태 코드 |
| `tflite::MicroInterpreter interpreter(model, resolver, tensor_arena, kArenaSize);` | arena를 빌려 쓰는 객체. 내부 할당자는 arena 안에 객체를 **placement new**로 만든다(소스에서 이 패턴이 보인다 — 버전마다 세부는 다름) |
| `interpreter.AllocateTensors()` | 한 번만 하는 계획·할당. 이후 `Invoke()`는 할당 없음 |
| `MicroPrintf(...)` | iostream 대신 경량 로그 |
| 빌드 플래그 | TFLM Makefile 기본 플래그에 `-fno-rtti -fno-exceptions`가 들어 있다(F2에서 본 저장소 기준, 버전마다 확인) |
| op 커널 = 등록 구조체의 함수 포인터 | 다형성은 op 단위 간접 호출 한 번, 커널 안은 평범한 루프(1.5절) |

정리하면 TFLM은 "펌웨어 C++ 부분집합"의 교과서 같은 예다. 반대로 **API는 C++**이라 C나 Rust에서 부르려면 C shim이 필요하다 — 9절에서 그 모양을 직접 만든다.

---

## 6. Rust 기초 — C 프로그래머를 위한 번역

### 6.1 직관: 버퍼 소유권 규칙을 컴파일러가 검사한다

SSD 펌웨어에서 버퍼 descriptor에는 보통 "지금 이 버퍼는 누구 것인가"(FW / DMA 엔진 / NAND 컨트롤러)를 나타내는 상태가 있었을 것이다. 규칙은 이렇다: 소유자만 버퍼를 만질 수 있고, 넘겨주면(submit) 넘긴 쪽은 더 이상 만지지 않으며, 마지막 소유자가 반납(free)한다. 이 규칙을 어기면 use-after-free, double free, DMA가 쓰는 중에 CPU가 덮어쓰기 같은 버그가 나고, 그걸 잡는 건 코드 리뷰와 오랜 디버깅이었다.

Rust는 이 규칙을 **언어에 넣고 컴파일 타임에 검사**한다. 그게 전부다. 런타임 비용(GC, 참조 카운트)은 없다.

### 6.2 세 가지 규칙

1. **소유권(ownership)**: 모든 값에는 소유자(변수)가 정확히 하나 있다. 소유자가 스코프를 벗어나면 값이 drop된다(C++ RAII 소멸자와 같다).
2. **이동(move)**: 대입하거나 함수에 값으로 넘기면 소유권이 이동한다. 이전 변수는 더 이상 쓸 수 없다. (정수 같은 작은 `Copy` 타입은 복사된다.)
3. **빌림(borrowing)**: 소유권을 넘기지 않고 참조를 빌려줄 수 있다. 단 동시에 **여러 개의 공유 참조 `&T`(읽기 전용)** 또는 **딱 하나의 배타 참조 `&mut T`(쓰기 가능)** 중 하나만 허용된다. 이를 흔히 "aliasing XOR mutability"(별칭과 변경은 동시에 안 된다)라고 부른다.

말로 하면: 읽는 사람은 여럿 있어도 되고, 쓰는 사람은 혼자여야 한다 — 펌웨어의 reader/writer 규칙과 같다. 그리고 참조는 원본보다 오래 살 수 없다(lifetime).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="113" y="22" font-size="13" text-anchor="middle">move (소유권 이동)</text>
<text x="340" y="22" font-size="13" text-anchor="middle">&amp;T 공유 빌림 (여럿, 읽기)</text>
<text x="567" y="22" font-size="13" text-anchor="middle">&amp;mut T 배타 빌림 (하나, 쓰기)</text>
<rect x="63" y="100" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="113" y="127" font-size="12" text-anchor="middle">Vec&lt;i8&gt; 버퍼</text>
<rect x="20" y="40" width="80" height="30" rx="4" fill="none" stroke="#888" stroke-dasharray="4 3"/>
<text x="60" y="60" font-size="12" text-anchor="middle">frame ✗</text>
<rect x="126" y="40" width="80" height="30" rx="4" fill="none" stroke="#3f9a6b"/>
<text x="166" y="60" font-size="12" text-anchor="middle">buf (새 주인)</text>
<line x1="166" y1="70" x2="140" y2="98" stroke="#3f9a6b" stroke-width="2"/>
<line x1="100" y1="55" x2="124" y2="55" stroke="currentColor"/>
<polygon points="118,50 126,55 118,60" fill="currentColor"/>
<text x="113" y="175" font-size="12" text-anchor="middle">이전 변수 사용 = E0382</text>
<rect x="290" y="100" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="340" y="127" font-size="12" text-anchor="middle">owner</text>
<rect x="240" y="40" width="60" height="30" rx="4" fill="none" stroke="#3f9a6b"/>
<text x="270" y="60" font-size="12" text-anchor="middle">&amp;r1</text>
<rect x="310" y="40" width="60" height="30" rx="4" fill="none" stroke="#3f9a6b"/>
<text x="340" y="60" font-size="12" text-anchor="middle">&amp;r2</text>
<rect x="380" y="40" width="60" height="30" rx="4" fill="none" stroke="#3f9a6b"/>
<text x="410" y="60" font-size="12" text-anchor="middle">&amp;r3</text>
<line x1="270" y1="70" x2="315" y2="98" stroke="#3f9a6b"/>
<line x1="340" y1="70" x2="340" y2="98" stroke="#3f9a6b"/>
<line x1="410" y1="70" x2="365" y2="98" stroke="#3f9a6b"/>
<text x="340" y="175" font-size="12" text-anchor="middle">빌린 동안 owner도 읽기만</text>
<rect x="517" y="100" width="100" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.25" stroke="#4a7bd0"/>
<text x="567" y="127" font-size="12" text-anchor="middle">owner</text>
<rect x="470" y="40" width="70" height="30" rx="4" fill="none" stroke="#e08a3c" stroke-width="2"/>
<text x="505" y="60" font-size="12" text-anchor="middle">&amp;mut w</text>
<rect x="590" y="40" width="70" height="30" rx="4" fill="none" stroke="#d0564a" stroke-dasharray="4 3"/>
<text x="625" y="60" font-size="12" text-anchor="middle">&amp;mut ✗</text>
<line x1="505" y1="70" x2="545" y2="98" stroke="#e08a3c" stroke-width="2"/>
<text x="567" y="175" font-size="12" text-anchor="middle">두 번째 &amp;mut = E0499</text>
<text x="340" y="215" font-size="13" text-anchor="middle">규칙: 읽는 쪽은 여럿 OR 쓰는 쪽은 하나 — 동시에 둘 다는 안 된다</text>
<text x="340" y="237" font-size="12" text-anchor="middle">참조는 owner보다 오래 살 수 없다 (lifetime) — 위반 = E0515/E0597 등</text>
</svg>
```

그림 5 — ownership과 borrowing을 그림으로. 파랑 = 데이터(owner가 가진 버퍼), 초록 = 허용된 참조, 주황 = 유일한 쓰기 참조, 빨강 점선 = 컴파일러가 거부하는 것. 이 검사는 전부 컴파일 타임이고, 생성 코드에는 포인터만 남는다.

### 6.3 예제 9 — use-after-move: 실제 `rustc` 에러

무엇을 확인하는 코드인지: 소유권을 넘긴 뒤 원래 변수를 쓰면(C라면 free된 버퍼 접근) 컴파일러가 어떻게 말하는지.

```rust
fn consume(buf: Vec<i8>) -> usize {
    buf.len() // 함수가 끝나면 buf가 drop(해제)된다
}
fn main() {
    let frame = vec![1i8, 2, 3];
    let n = consume(frame); // 소유권이 consume으로 이동(move)
    println!("{n} {}", frame[0]); // C라면 use-after-free
}
```

```text
$ rustc --edition 2021 moved.rs
error[E0382]: borrow of moved value: `frame`
 --> moved.rs:7:24
  |
5 |     let frame = vec![1i8, 2, 3];
  |         ----- move occurs because `frame` has type `Vec<i8>`, which does not implement the `Copy` trait
6 |     let n = consume(frame); // 소유권이 consume으로 이동(move)
  |                     ----- value moved here
7 |     println!("{n} {}", frame[0]); // C라면 use-after-free
  |                        ^^^^^ value borrowed here after move
  |
(note/help 12줄 생략: "consume이 빌리도록 시그니처를 바꾸라", "frame.clone()을 넘기라")

error: aborting due to 1 previous error
```

출력에서 볼 것: 에러가 "어디서 이동했고(6행) 어디서 다시 썼는지(7행)"를 짚고, 생략한 note/help에서 고치는 법 두 가지(빌림으로 바꾸기, 복제)를 제안한다. 펌웨어 답은 거의 항상 첫 번째 — 함수가 버퍼를 소유할 필요가 없으면 `&[i8]`(slice 빌림)을 받는다.

### 6.4 예제 10 — data race 시도: 두 스레드가 같은 변수를 쓰기

무엇을 확인하는 코드인지: 두 실행 흐름(ISR과 task, 또는 두 스레드)이 같은 변수를 동시에 수정하는 코드를 Rust가 받아 주는지.

```rust
use std::thread;
fn main() {
    let mut count = 0u32;
    thread::scope(|s| {
        s.spawn(|| count += 1); // 스레드 A가 &mut count를 빌림
        s.spawn(|| count += 1); // 스레드 B도 &mut count를 빌리려 함
    });
    println!("{count}");
}
```

```text
$ rustc --edition 2021 race.rs
error[E0499]: cannot borrow `count` as mutable more than once at a time
 --> race.rs:6:17
  |
4 |     thread::scope(|s| {
  |                    - has type `&'1 Scope<'1, '_>`
5 |         s.spawn(|| count += 1); // 스레드 A가 &mut count를 빌림
  |         ----------------------
  |         |       |  |
  |         |       |  first borrow occurs due to use of `count` in closure
  |         |       first mutable borrow occurs here
  |         argument requires that `count` is borrowed for `'1`
6 |         s.spawn(|| count += 1); // 스레드 B도 &mut count를 빌리려 함
  |                 ^^ ----- second borrow occurs due to use of `count` in closure
  |                 |
  |                 second mutable borrow occurs here
```

출력에서 볼 것: data race가 "배타 참조 `&mut`가 두 개"라는 **타입 에러**로 잡혔다. C였다면 컴파일·실행이 되고, 가끔 카운트가 1이 되는 버그가 생긴다. 고치려면 공유하면서 변경 가능한 타입 — `AtomicU32`, `Mutex`, 임베디드에선 `critical_section::Mutex` — 을 써야 한다(예제 12). 펌웨어의 "ISR과 main이 공유하는 전역 변수"가 Rust에서 `static mut`으로는 `unsafe` 없이 접근이 안 되는 것도 같은 이유다.

### 6.5 Lifetime — "참조는 원본보다 오래 살 수 없다"

lifetime은 새 개념이 아니라 C 버그 하나의 이름이다: 스택 배열의 주소를 반환하는 dangling pointer.

```rust
fn latest_frame() -> &'static [i8] {
    let frame = [1i8, 2, 3]; // 스택 위 지역 배열
    &frame                   // C라면 dangling pointer 반환
}
fn main() { println!("{:?}", latest_frame()); }
```

```text
$ rustc --edition 2021 dangle.rs
error[E0515]: cannot return reference to local variable `frame`
 --> dangle.rs:3:5
  |
3 |     &frame                   // C라면 dangling pointer 반환
  |     ^^^^^^ returns a reference to data owned by the current function
```

`'static`, `'a` 같은 표기가 lifetime 이름이다. 대부분은 컴파일러가 추론하고, 사람이 쓰는 경우는 "반환하는 참조가 어느 입력에서 왔는가"를 알려 줄 때다. 예를 들어 `fn window<'a>(buf: &'a [i8], start: usize) -> &'a [i8]`는 "반환된 창(window)은 `buf`가 살아 있는 동안만 유효하다"는 계약이다. 9절에서 "모델 객체는 arena보다 오래 살 수 없다"를 lifetime으로 표현해 본다. 펌웨어에서 `'static`은 "프로그램 끝까지 산다" = 전역/flash 데이터라는 뜻이다.

### 6.6 예제 11 — 고친 버전: 빌림, Atomic, Option/Result, slice

무엇을 확인하는 코드인지: 위 에러들을 고친 정상 코드와, C의 "에러 코드 + out 포인터" 관례를 `Result`/`Option`으로 바꾼 모습.

```rust
use std::sync::atomic::{AtomicU32, Ordering};
use std::thread;

fn peek(buf: &[i8]) -> usize { buf.len() } // 빌리기만 한다(&), 소유권은 그대로

#[derive(Debug)]
enum SensorError { NotReady, BadWhoAmI(u8) }

// C의 "int read_whoami(uint8_t *out)" + 에러 코드 대신 Result
fn read_whoami(reg: Option<u8>) -> Result<u8, SensorError> {
    let v = reg.ok_or(SensorError::NotReady)?; // None이면 즉시 Err 반환 (? 연산자)
    if v != 0x6C { return Err(SensorError::BadWhoAmI(v)); }
    Ok(v)
}

fn main() {
    let frame = vec![1i8, 2, 3];
    let n = peek(&frame);
    println!("len={n}, frame[0]={} (still usable)", frame[0]);

    let count = AtomicU32::new(0); // 공유 + 변경은 atomic(또는 Mutex)으로만
    thread::scope(|s| {
        s.spawn(|| count.fetch_add(1, Ordering::Relaxed));
        s.spawn(|| count.fetch_add(1, Ordering::Relaxed));
    });
    println!("count={}", count.load(Ordering::Relaxed));

    for r in [Some(0x6C), Some(0x11), None] {
        match read_whoami(r) {
            Ok(id) => println!("WHO_AM_I ok: 0x{id:02X}"),
            Err(SensorError::BadWhoAmI(v)) => println!("wrong chip: 0x{v:02X}"),
            Err(e) => println!("error: {e:?}"),
        }
    }
    let win = &frame[1..]; // slice = 포인터 + 길이, 범위는 런타임 검사
    println!("win={win:?}, get(5)={:?}", frame.get(5));
}
```

```text
$ rustc --edition 2021 -O fixed.rs && ./fixed
len=3, frame[0]=1 (still usable)
count=2
WHO_AM_I ok: 0x6C
wrong chip: 0x11
error: NotReady
win=[2, 3], get(5)=None
```

출력에서 볼 것:

- `Result<u8, SensorError>`는 "성공이면 값, 실패면 에러 종류"를 **한 타입**에 담는다. C의 `int` 리턴 코드와 달리 호출자가 에러를 무시하면 경고가 나고, `match`가 모든 경우를 다루지 않으면 컴파일 에러다. `?`는 "에러면 바로 위로 return" — C의 `if (rc) return rc;` 매크로와 같다.
- `Option<u8>`은 "값이 있을 수도, 없을 수도"다. NULL 포인터 대신 쓴다. `Option<&T>`는 메모리상 그냥 포인터 하나(None = 0)로 최적화된다.
- slice `&frame[1..]`는 C++ `std::span`과 같은 (포인터, 길이) 짝이다. `frame.get(5)`는 범위 밖이면 `None`, `frame[5]`는 **패닉**(런타임 검사)이다. 경계 검사 비용은 8절에서 실제 어셈블리로 확인한다 — 반복자를 쓰면 루프 안에서 사라진다.

### 6.7 예제 12 — 정수 오버플로: Rust는 의도를 쓰게 한다

무엇을 확인하는 코드인지: C에서 signed overflow는 undefined behavior다. Rust는 어떻게 처리하는지 — 양자화 커널에서 특히 중요하다.

```rust
use std::hint::black_box;
fn main() {
    let acc: i8 = black_box(100);
    println!("wrapping_add  : {}", acc.wrapping_add(50));   // C처럼 wrap (명시)
    println!("saturating_add: {}", acc.saturating_add(50)); // SSAT처럼 포화
    println!("checked_add   : {:?}", acc.checked_add(50));  // 넘치면 None
    let buf = [1i8, 2, 3];
    let idx = black_box(5);
    println!("buf.get(idx)  : {:?}", buf.get(idx));
    println!("plain +       : {}", acc + 50);              // debug 빌드: 패닉
}
```

```text
$ rustc --edition 2021 arith.rs -o arith_dbg && ./arith_dbg; echo "exit=$?"
wrapping_add  : -106
saturating_add: 127
checked_add   : None
buf.get(idx)  : None

thread 'main' (3834847) panicked at arith.rs:10:36:
attempt to add with overflow
note: run with `RUST_BACKTRACE=1` environment variable to display a backtrace
exit=101
$ rustc --edition 2021 -O arith.rs -o arith_rel && ./arith_rel | tail -1
plain +       : -106
```

출력에서 볼 것: 100 + 50 = 150은 i8 범위(127)를 넘는다. `wrapping_add`는 150 − 256 = −106, `saturating_add`는 127(Cortex-M의 `SSAT`/`QADD` 명령과 같은 의미), `checked_add`는 `None`. 그냥 `+`는 **debug 빌드에선 패닉, release 빌드에선 wrap**이다(기본 설정; `overflow-checks`로 바꿀 수 있다). UB가 아니라 둘 중 하나로 정의되어 있다는 점이 C와 다르다. 양자화 커널에서는 "여기서는 포화가 맞다"를 `saturating_*`/`clamp`로 코드에 직접 쓰게 된다.

### 6.8 `unsafe` — 언제 필요한가

`unsafe`는 "검사를 끄는 스위치"가 아니라 **컴파일러가 증명할 수 없는 약속을 사람이 대신 하는 블록**이다. `unsafe` 안에서만 할 수 있는 것은 Rust book이 꼽는 "다섯 가지"가 핵심이다: raw pointer(`*const T`, `*mut T`) 역참조, `unsafe fn` 호출(FFI 함수 포함), `static mut` 접근, `unsafe trait` 구현, union 필드 읽기. borrow checker는 `unsafe` 안에서도 그대로 동작한다.

| 상황 | 왜 unsafe인가 | 관례 |
|---|---|---|
| MMIO 레지스터 | 주소 0x4001_3000이 유효한 레지스터인지 컴파일러가 모름 | PAC가 `unsafe`를 한 곳에 가두고 안전한 API 제공 |
| C 함수 호출 (FFI) | C 쪽 시그니처·포인터 유효성을 검사할 수 없음 | 얇은 safe wrapper 하나에만 `unsafe` (9절) |
| `static mut` (ISR 공유 전역) | data race 가능성 | `critical_section::Mutex`, atomic, RTIC/Embassy의 공유 자원 |
| DMA 버퍼 | 하드웨어가 메모리를 쓰는 동안 CPU 접근 금지를 컴파일러가 모름 | 버퍼 소유권을 DMA 전송 객체로 move했다가 완료 시 돌려받는 API |
| SIMD intrinsics, 특정 명령 | 대상 CPU 기능 보장 필요 | `core::arch`의 intrinsic을 safe 함수로 감싼다 |

펌웨어 관점의 요점: 전체 코드 중 `unsafe`는 드라이버 바닥과 FFI 경계의 수 % 이내로 모이고, 리뷰·감사 대상이 그 블록들로 좁혀진다. "C의 모든 줄이 unsafe"인 것과의 차이가 이것이다.

---

## 7. Rust로 ML 커널 — `no_std` int8 dense 층

### 7.1 `core`, `alloc`, `std`

| crate | 들어 있는 것 | 필요한 것 | MCU |
|---|---|---|---|
| `core` | 정수·float 연산, slice, `Option/Result`, 반복자, `atomic`, `fmt`, `ptr`, `mem` | 아무것도 없음 | 항상 사용 |
| `alloc` | `Vec`, `Box`, `String`, `Rc` | 전역 할당자(`#[global_allocator]`) | 선택 (힙을 쓰기로 했을 때) |
| `std` | 파일, 스레드, 네트워크, `println!`, `HashMap` | OS | 사용 불가 (Linux급 Cortex-A면 가능) |

`#![no_std]`는 "`std` 대신 `core`만 링크한다"는 선언이다. 그러면 `Vec`과 `println!`이 없어지고, 대신 이 코드는 OS 없는 어떤 타깃에서도 돈다. 고정 용량 컨테이너가 필요하면 `heapless` crate(`heapless::Vec<T, N>`, `spsc::Queue`)를 쓴다 — C++ 4절의 `RingBuffer<T, N>`과 같은 발상이다(const generics로 N이 타입에 들어간다).

### 7.2 커널 — C1 7.6절을 그대로 포팅

무엇을 확인하는 코드인지: C1에서 C로 짠 int8 dense + gemmlowp식 requantize(SRDHM + RDBPOT)를 `no_std` Rust 라이브러리로 옮긴 것. 같은 정수 산술이므로 결과가 C와 비트 단위로 같아야 한다.

```rust
// qk/src/lib.rs
#![no_std] // core만 쓴다: 힙·OS·println 없음 → MCU와 호스트 양쪽에서 같은 코드

/// round(a·b / 2^31), int32 포화 (gemmlowp SaturatingRoundingDoublingHighMul)
#[inline(always)]
pub fn srdhm(a: i32, b: i32) -> i32 {
    if a == i32::MIN && b == i32::MIN { return i32::MAX; }
    let ab = a as i64 * b as i64;
    let nudge = if ab >= 0 { 1i64 << 30 } else { 1 - (1i64 << 30) };
    ((ab + nudge) / (1i64 << 31)) as i32 // Rust의 / 도 0 쪽으로 자른다 (C와 같음)
}

/// x / 2^e, half away from zero (gemmlowp RoundingDivideByPOT)
#[inline(always)]
pub fn rdbpot(x: i32, e: u32) -> i32 {
    let mask = ((1i64 << e) - 1) as i32;
    let rem = x & mask;
    let thr = (mask >> 1) + if x < 0 { 1 } else { 0 };
    (x >> e) + if rem > thr { 1 } else { 0 } // i32의 >> 는 언어 정의상 산술 시프트
}

#[inline(always)]
pub fn requant(acc: i32, m0: i32, shift: i32) -> i32 { // shift <= 0
    rdbpot(srdhm(acc, m0), (-shift) as u32)
}

#[derive(Debug, PartialEq, Eq)]
pub enum DenseError { BadLength }

/// int8 dense: y[o] = clamp(z_y + requant(bias_eff[o] + Σ w[o][i]·x[i]))
pub fn dense_s8(x: &[i8], w: &[i8], bias_eff: &[i32], m0: &[i32], shift: &[i32],
                z_y: i32, act_min: i32, act_max: i32, y: &mut [i8]) -> Result<(), DenseError> {
    let (n_in, n_out) = (x.len(), y.len());
    if w.len() != n_in * n_out || bias_eff.len() != n_out || m0.len() != n_out || shift.len() != n_out {
        return Err(DenseError::BadLength); // 길이 검사는 한 번, 루프 밖에서
    }
    for (o, out) in y.iter_mut().enumerate() {
        let row = &w[o * n_in..(o + 1) * n_in];
        let mut acc = bias_eff[o];
        for (&wi, &xi) in row.iter().zip(x) { // zip: 인덱스 없음 → 경계 검사 없음
            acc += wi as i32 * xi as i32;
        }
        let v = requant(acc, m0[o], shift[o]) + z_y;
        *out = v.clamp(act_min, act_max) as i8;
    }
    Ok(())
}

/// 오프라인 단계: bias_eff = q_b - z_x · Σ_i w[o][i]
pub fn fold_bias(w: &[i8], q_b: &[i32], z_x: i32, bias_eff: &mut [i32]) {
    let n_in = w.len() / q_b.len();
    for (o, be) in bias_eff.iter_mut().enumerate() {
        let sw: i32 = w[o * n_in..(o + 1) * n_in].iter().map(|&v| v as i32).sum();
        *be = q_b[o] - z_x * sw;
    }
}
```

C와 비교해서 달라진 점:

- 포인터 + `n_in, n_out` 대신 **slice**를 받는다. 길이는 slice가 들고 있으니 인자 개수가 줄고, 길이 불일치는 `Err(BadLength)`로 돌려준다(C 버전은 조용히 범위를 넘어 읽는다).
- `x >> e`의 음수 동작: C11에서는 implementation-defined라 C1에서 "산술 시프트라고 가정"이라 적었지만, Rust의 부호 있는 정수 `>>`는 **언어 정의상 산술 시프트**다. 정수 나눗셈 `/`도 C와 같이 0 쪽으로 자른다고 정의되어 있다.
- `as` 캐스트는 C의 명시적 캐스트와 같다(넓히기는 부호 확장, 좁히기는 비트 자르기). 출력의 `as i8`은 `clamp` 뒤라 안전하다.

### 7.3 C 레퍼런스를 같이 빌드 — `build.rs` + `cc` crate

C1의 C 커널(`c/dense_s8.c`, `static`만 빼고 함수 이름을 `c_dense_s8`로)을 Rust 테스트에서 직접 부른다. `build.rs`는 cargo가 빌드 전에 실행하는 스크립트이고, `cc` crate는 "이 C 파일을 시스템 C 컴파일러로 컴파일해서 정적 라이브러리로 링크하라"를 해 준다.

```rust
// qk/build.rs — C 레퍼런스는 "호스트에서 테스트할 때"만 필요하다. MCU 타깃(target_os = none)에선 건너뛴다.
fn main() {
    println!("cargo:rerun-if-changed=c/dense_s8.c");
    if std::env::var("CARGO_CFG_TARGET_OS").unwrap() != "none" {
        cc::Build::new().file("c/dense_s8.c").flag("-std=c11").flag("-Wall").flag("-Wextra")
            .compile("dense_ref"); // → libdense_ref.a, 자동으로 링크 지시
    }
}
```

`qk/Cargo.toml`에는 `[build-dependencies] cc = "1.5.1"`과 테스트용 `[dev-dependencies] heapless = "0.9"`만 있다 — 라이브러리 본체는 의존성 0개다.

### 7.4 예제 13 — `cargo test`: 손계산, 에러 경로, C와 bit-exact

무엇을 확인하는 코드인지: (1) C1의 손계산 예제(`bias_eff = [6803, 4568]`, `q_y = [−26, 3]`)가 나오는지, (2) 길이가 틀리면 `Err`인지, (3) 무작위 층 3종 × 입력 200개에서 Rust와 C 출력이 **완전히 같은지**, (4) `heapless` 컨테이너의 용량 동작.

```rust
// qk/tests/vs_c.rs
use core::ffi::c_int;
use qk::{dense_s8, fold_bias, srdhm, DenseError};

unsafe extern "C" { // C 함수 선언: 컴파일러는 이 시그니처를 "믿는다" → unsafe
    fn c_dense_s8(x: *const i8, w: *const i8, bias_eff: *const i32, m0: *const i32,
                  shift: *const c_int, n_in: c_int, n_out: c_int,
                  z_y: i32, act_min: i32, act_max: i32, y: *mut i8);
}

struct Lcg(u32); // 외부 crate 없이 재현 가능한 난수
impl Lcg {
    fn next(&mut self) -> u32 { self.0 = self.0.wrapping_mul(1664525).wrapping_add(1013904223); self.0 }
    fn i8(&mut self) -> i8 { (self.next() >> 24) as u8 as i8 }
}

#[test]
fn hand_example_from_c1() { // C1 7.5/7.6절 손계산 예제
    let w = [127i8, -76, 30, -56, 127, 32];
    let mut be = [0i32; 2];
    fold_bias(&w, &[1619, -2024], -64, &mut be);
    let mut y = [0i8; 2];
    dense_s8(&[0, 32, -96], &w, &be, &[1082196484, 1731514374], &[-7, -7], -32, -128, 127, &mut y).unwrap();
    assert_eq!(be, [6803, 4568]);
    assert_eq!(y, [-26, 3]);
}

#[test]
fn srdhm_saturates() { assert_eq!(srdhm(i32::MIN, i32::MIN), i32::MAX); }

#[test]
fn length_mismatch_is_an_error() {
    let mut y = [0i8; 2];
    let r = dense_s8(&[1, 2, 3], &[0; 5], &[0; 2], &[1 << 30; 2], &[-1; 2], 0, -128, 127, &mut y);
    assert_eq!(r, Err(DenseError::BadLength));
}

#[test]
fn bit_exact_vs_c_random_layers() {
    let mut rng = Lcg(2026);
    let (mut vectors, mut outputs) = (0, 0);
    for &(n_in, n_out) in &[(3usize, 2usize), (64, 32), (250, 10)] {
        let w: Vec<i8> = (0..n_in * n_out).map(|_| rng.i8()).collect();
        let be: Vec<i32> = (0..n_out).map(|_| (rng.next() % 20001) as i32 - 10000).collect();
        let m0: Vec<i32> = (0..n_out).map(|_| (1 << 30) + (rng.next() >> 2) as i32).collect();
        let sh: Vec<i32> = (0..n_out).map(|_| -((rng.next() % 6) as i32 + 6)).collect();
        for _ in 0..200 {
            let x: Vec<i8> = (0..n_in).map(|_| rng.i8()).collect();
            let (mut y_rs, mut y_c) = (vec![0i8; n_out], vec![0i8; n_out]);
            dense_s8(&x, &w, &be, &m0, &sh, -5, -128, 127, &mut y_rs).unwrap();
            unsafe { // 포인터·길이 짝은 여기서 우리가 보증한다
                c_dense_s8(x.as_ptr(), w.as_ptr(), be.as_ptr(), m0.as_ptr(), sh.as_ptr(),
                           n_in as c_int, n_out as c_int, -5, -128, 127, y_c.as_mut_ptr());
            }
            assert_eq!(y_rs, y_c, "mismatch n_in={n_in} n_out={n_out}");
            vectors += 1; outputs += n_out;
        }
    }
    println!("bit-exact: {vectors} vectors, {outputs} outputs");
}

#[test]
fn heapless_containers() {
    let mut v: heapless::Vec<i8, 4> = heapless::Vec::new();
    for i in 0..6 { let _ = v.push(i); } // 5번째부터 Err(값) 반환, 패닉 없음
    let mut q: heapless::spsc::Queue<u16, 4> = heapless::spsc::Queue::new();
    let accepted = (0..6).filter(|&i| q.enqueue(i).is_ok()).count();
    println!("heapless::Vec len={} cap={}, spsc::Queue<_,4> accepted={accepted}", v.len(), v.capacity());
}
```

```text
$ cargo test --test vs_c -- --nocapture --test-threads=1
    Finished `test` profile [unoptimized + debuginfo] target(s) in 0.01s
     Running tests/vs_c.rs (target/debug/deps/vs_c-7875640ca7ebd4a1)

running 5 tests
test bit_exact_vs_c_random_layers ... bit-exact: 600 vectors, 8800 outputs
ok
test hand_example_from_c1 ... ok
test heapless_containers ... heapless::Vec len=4 cap=4, spsc::Queue<_,4> accepted=3
ok
test length_mismatch_is_an_error ... ok
test srdhm_saturates ... ok

test result: ok. 5 passed; 0 failed; 0 ignored; 0 measured; 0 filtered out; finished in 0.01s
```

출력에서 볼 것:

- 손계산 예제가 C1과 같은 `[6803, 4568]`, `[−26, 3]`을 냈다.
- **600개 입력, 8800개 출력이 C 레퍼런스와 비트 단위로 일치**한다. 정수 연산만 쓰므로 언어가 달라도 bit-exact가 정상이고, 하나라도 다르면 포팅 버그다(J6의 golden-vector 테스트와 같은 원리).
- `heapless::Vec<i8, 4>`는 4개에서 멈추고 `push`가 `Err`를 돌려준다(패닉 아님). `heapless::spsc::Queue<u16, 4>`는 **3개**만 받았다 — 이 버전(0.9.3)의 spsc Queue는 full/empty 구분용으로 한 칸을 비워 두어 실제 용량이 N−1이다. 4.2절 C++ 버전(N칸 전부 사용)과 설계가 다르다. 라이브러리 컨테이너의 정확한 용량 규칙은 버전 문서로 확인해야 한다는 좋은 예다.
- 테스트는 debug 빌드라 오버플로 검사가 켜져 있다. int32 누산이 넘쳤다면 테스트가 패닉으로 알려 줬을 것이다(6.7절) — 커널 검증에 덤으로 얻는 안전망이다.

같은 커널 코드가 호스트 `cargo test`와 MCU 빌드(8절)에 **수정 없이** 쓰인다. C에서 호스트 단위 테스트 하네스(Unity, CppUTest)를 따로 세팅하던 것과 비교하면 이 점이 Rust 도입의 실질적인 이득 중 하나다.

---

## 8. Cortex-M4용 `no_std` 바이너리 빌드

### 8.1 프로젝트 구성

`cortex-m-rt` crate가 벡터 테이블, 리셋 핸들러(`.data` 복사·`.bss` 0 채우기 후 `main` 호출), 링커 스크립트 `link.x`를 제공한다. 사용자는 메모리 맵 `memory.x`만 쓴다 — F7에서 손으로 쓴 링커 스크립트·startup 코드를 crate가 대신 해 주는 셈이다. `panic-halt`는 "패닉 = 무한 루프에서 정지"인 패닉 핸들러다(`no_std`에서는 패닉 핸들러를 반드시 하나 제공해야 한다).

```toml
# fw/.cargo/config.toml
[build]
target = "thumbv7em-none-eabihf"          # Cortex-M4F/M7F, hard-float ABI

[target.thumbv7em-none-eabihf]
rustflags = ["-C", "link-arg=-Tlink.x"]   # cortex-m-rt가 제공하는 링커 스크립트 (memory.x를 include)
```

```text
/* fw/memory.x — 예시 MCU: STM32F4급 1 MB flash, 128 KB SRAM */
MEMORY
{
  FLASH : ORIGIN = 0x08000000, LENGTH = 1024K
  RAM   : ORIGIN = 0x20000000, LENGTH = 128K
}
```

```toml
# fw/Cargo.toml
[dependencies]
cortex-m-rt = "0.7.7"
heapless = "0.9"
panic-halt = "1.0.0"
qk = { version = "0.1.0", path = "../qk" }

[profile.release]
opt-level = "s"     # 크기 우선 (-Os)
lto = true
codegen-units = 1
debug = true        # 디버그 정보는 ELF에만, flash에 안 올라간다
panic = "abort"
```

```rust
// fw/src/main.rs
#![no_std]
#![no_main]

use core::ptr::{addr_of, addr_of_mut, read_volatile, write_volatile};
use cortex_m_rt::entry;
use heapless::Vec;
use panic_halt as _; // 패닉 = 무한 루프에서 멈춤 (#[panic_handler] 제공)

const N_IN: usize = 64;
const N_OUT: usize = 32;

const fn make_weights() -> [i8; N_IN * N_OUT] { // C++ constexpr에 해당: 컴파일 타임 생성
    let mut w = [0i8; N_IN * N_OUT];
    let (mut s, mut i) = (2026u32, 0);
    while i < w.len() {
        s = s.wrapping_mul(1664525).wrapping_add(1013904223);
        w[i] = (s >> 24) as u8 as i8;
        i += 1;
    }
    w
}
static W: [i8; N_IN * N_OUT] = make_weights(); // → .rodata (flash)
static BIAS: [i32; N_OUT] = [100; N_OUT];
static M0: [i32; N_OUT] = [1_518_500_250; N_OUT]; // ≈ 0.7071·2^31
static SHIFT: [i32; N_OUT] = [-8; N_OUT];

static mut INPUT: [i8; N_IN] = [0; N_IN];  // DMA/ISR가 채운다고 가정 (.bss)
static mut OUTPUT: [i8; N_OUT] = [0; N_OUT];

#[entry]
fn main() -> ! {
    let mut window: Vec<i8, N_IN> = Vec::new(); // 힙 없는 고정 용량 벡터 (스택)
    loop {
        window.clear();
        for i in 0..N_IN { // 입력 버퍼를 volatile로 읽어 상수 접힘 방지
            let v = unsafe { read_volatile(addr_of!(INPUT).cast::<i8>().add(i)) };
            let _ = window.push(v);
        }
        let mut y = [0i8; N_OUT];
        if qk::dense_s8(&window, &W, &BIAS, &M0, &SHIFT, -128, -128, 127, &mut y).is_ok() {
            for (i, v) in y.iter().enumerate() {
                unsafe { write_volatile(addr_of_mut!(OUTPUT).cast::<i8>().add(i), *v) };
            }
        }
    }
}
```

`main() -> !`의 `!`는 "절대 반환하지 않는다"는 타입이다 — 펌웨어 main 루프를 타입으로 표현한 것. `const fn`은 C++ `constexpr` 함수에 해당하고, 64×32 weight 2048개가 빌드 중에 계산되어 flash에 들어간다. `unsafe`는 `static mut` 입력/출력 버퍼에 접근하는 두 줄뿐이다.

### 8.2 예제 14 — 빌드, 섹션 크기, 심볼

```text
$ cargo build --release 2>&1 | tail -1
    Finished `release` profile [optimized + debuginfo] target(s) in 3.56s
$ file target/thumbv7em-none-eabihf/release/fw
target/thumbv7em-none-eabihf/release/fw: ELF 32-bit LSB executable, ARM, EABI5 version 1 (SYSV), statically linked, with debug_info, not stripped
$ xcrun llvm-size target/thumbv7em-none-eabihf/release/fw
   text	   data	    bss	    dec	    hex	filename
   3668	      0	     96	   3764	    eb4	target/thumbv7em-none-eabihf/release/fw
$ xcrun llvm-size -A -x target/thumbv7em-none-eabihf/release/fw     (디버그 섹션 제외)
section               size         addr
.vector_table        0x400    0x8000000
.text                0x254    0x8000400
.rodata              0x800    0x8000654
.data                    0   0x20000000
.gnu.sgstubs             0    0x8000e60
.bss                  0x60   0x20000000
.uninit                  0   0x20000060
$ xcrun llvm-nm -S --size-sort -C target/thumbv7em-none-eabihf/release/fw | tail -12
08000584 00000006 t __rustc::rust_begin_unwind
08000568 00000008 t core::panicking::panic_fmt
08000570 00000008 t core::slice::index::slice_index_fail
08000560 00000008 T main
20000040 00000020 b fw::OUTPUT
08000008 00000038 R __EXCEPTIONS
08000400 0000003e T Reset
20000000 00000040 b fw::INPUT
0800058a 000000c4 t __aeabi_memclr4
08000458 00000108 t fw::__cortex_m_rt_main
08000040 000003c0 R __INTERRUPTS
08000654 00000800 r fw::W
```

출력에서 볼 것:

- 의존성은 `cortex-m-rt`(+ 매크로용 `proc-macro2/quote/syn`), `heapless`(+ `hash32`, `byteorder`, `stable_deref_trait`), `panic-halt`, `qk`(+ 호스트 빌드 스크립트용 `cc`)뿐이고 클린 빌드가 3.6초 걸렸다.
- `rust-size`(cargo-binutils)는 설치하지 않았지만, Apple의 `llvm-size`/`llvm-nm`/`llvm-objdump`가 ARM ELF를 그대로 읽는다. `file`도 ELF 32-bit ARM으로 확인.
- flash 사용 = 3668 B = 벡터 테이블 1024 B + 코드 596 B + 상수 2048 B. **코드는 596 B뿐**이고, 그중 실제 로직 `__cortex_m_rt_main`이 264 B(0x108)다. 벡터 테이블이 큰 이유: 디바이스 crate 없이 `cortex-m-rt`만 쓰면 인터럽트 벡터 240개(`__INTERRUPTS` 0x3c0 = 960 B)를 기본으로 잡는다. 실제 칩의 PAC를 쓰면 그 칩의 IRQ 수만큼만 생긴다.
- `.rodata` 0x800 = 2048 B가 정확히 `fw::W`다. `BIAS/M0/SHIFT`는 모든 원소가 같은 상수라 LTO가 코드 안의 즉시값으로 접어 버려서 테이블로 남지 않았다.
- RAM은 `.bss` 96 B = `INPUT` 64 + `OUTPUT` 32. `.data`는 0 B — 초기값 있는 전역 변수가 없다. 스택은 `memory.x`의 RAM 끝(0x2002_0000)에서 아래로 자란다.
- 패닉 관련 심볼은 `panic_fmt` 8 B, `rust_begin_unwind` 6 B, `slice_index_fail` 8 B로 아주 작다. `panic = "abort"` + `panic-halt` 조합에서는 포매팅 기계가 거의 링크되지 않는다. (패닉 메시지를 출력하는 핸들러를 쓰면 `core::fmt`가 끌려와 수 KB가 늘어나는 경우가 흔하다 — 그래서 defmt 같은 도구가 나왔다, 10절.)

```svg
<svg viewBox="0 0 680 240" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">fw ELF 메모리 배치 (thumbv7em-none-eabihf, opt-level="s", LTO)</text>
<text x="20" y="60" font-size="12">FLASH</text>
<rect x="80" y="44" width="156.3" height="30" fill="#888" fill-opacity="0.5" stroke="currentColor"/>
<rect x="236.3" y="44" width="91.0" height="30" fill="#4a7bd0" stroke="currentColor"/>
<rect x="327.3" y="44" width="312.7" height="30" fill="#e08a3c" stroke="currentColor"/>
<text x="158" y="93" font-size="12" text-anchor="middle">.vector_table 1024 B</text>
<text x="282" y="110" font-size="12" text-anchor="middle">.text 596 B</text>
<text x="484" y="93" font-size="12" text-anchor="middle">.rodata 2048 B (weights W)</text>
<text x="80" y="40" font-size="12">0x0800_0000</text>
<text x="640" y="40" font-size="12" text-anchor="end">0x0800_0E54 (합 3668 B)</text>
<text x="20" y="160" font-size="12">RAM</text>
<rect x="80" y="144" width="60" height="30" fill="#3f9a6b" stroke="currentColor"/>
<rect x="140" y="144" width="30" height="30" fill="#3f9a6b" fill-opacity="0.5" stroke="currentColor"/>
<rect x="170" y="144" width="370" height="30" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<rect x="540" y="144" width="100" height="30" fill="#d0564a" fill-opacity="0.35" stroke="currentColor"/>
<text x="110" y="193" font-size="12" text-anchor="middle">INPUT 64</text>
<text x="160" y="210" font-size="12" text-anchor="middle">OUTPUT 32</text>
<text x="355" y="164" font-size="12" text-anchor="middle">빈 공간 (128 KB 중 대부분)</text>
<text x="590" y="193" font-size="12" text-anchor="middle">스택 ↓ (window, y)</text>
<text x="80" y="140" font-size="12">0x2000_0000</text>
<text x="640" y="140" font-size="12" text-anchor="end">0x2002_0000</text>
<text x="20" y="232" font-size="12">FLASH 막대는 실제 크기 비례 (0.153 px/B). RAM 막대는 비례 아님.</text>
</svg>
```

그림 6 — 빌드된 `no_std` 펌웨어의 실제 섹션 배치. 회색 = 벡터 테이블, 파랑 = 코드, 주황 = weight 상수(flash), 초록 = `.bss`, 빨강 = 스택. F7에서 배운 "상수는 flash, 가변은 RAM" 배치가 링커 스크립트 한 줄 없이 그대로 나온다.

### 8.3 예제 15 — inner loop 디스어셈블: Rust vs C

무엇을 확인하는 코드인지: slice와 반복자를 쓴 Rust 커널이 C와 같은 기계어로 내려가는지, 경계 검사가 루프 안에 남는지.

```text
$ xcrun llvm-objdump -d --no-show-raw-insn -C --triple=thumbv7em-none-eabihf --mcpu=cortex-m4 \
      target/thumbv7em-none-eabihf/release/fw     (fw::__cortex_m_rt_main 중 발췌)
 80004b0:      	add.w	lr, r12, #0x1
 80004b4:      	mul	r0, r12, r9
 80004b8:      	mul	r1, lr, r9
 80004bc:      	cmp	r1, r0
 80004be:      	blo	0x800055c <fw::__cortex_m_rt_main+0x104> @ imm = #0x9a
 80004c0:      	cmp.w	r1, #0x800
 80004c4:      	bhi	0x800055c <fw::__cortex_m_rt_main+0x104> @ imm = #0x94
 80004c6:      	movs	r0, #0x64
 80004c8:      	mov	r1, r9
 80004ca:      	mov	r2, r4
 80004cc:      	mov	r5, r10
 80004ce:      	ldrsb	r6, [r2], #1
 80004d2:      	subs	r1, #0x1
 80004d4:      	ldrsb	r3, [r5], #1
 80004d8:      	smlabb	r0, r3, r6, r0
 80004dc:      	bne	0x80004ce <fw::__cortex_m_rt_main+0x76> @ imm = #-0x12
 80004de:      	movs	r1, #0x1
 80004e0:      	movw	r3, #0x799a
 80004e4:      	movt	r1, #0xc000
 ...
 80004fa:      	smlal	r1, r2, r0, r3
 ...
 8000530:      	ssat	r0, #0x8, r0
 8000534:      	strb.w	r0, [r11, r12]
 ...
 800055c:      	bl	0x8000570 <core::slice::index::slice_index_fail> @ imm = #0x10
```

같은 커널의 C 버전(C1의 `dense_s8.c`)을 clang으로 Cortex-M4 `-Os` 컴파일한 inner loop:

```text
$ cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfloat-abi=hard -std=c11 -Os -ffreestanding -c c/dense_s8.c
      2c:      	ldrsb	r12, [r4], #1
      30:      	ldrsb	r6, [r5], #1
      34:      	subs	r7, #0x1
      36:      	smlabb	lr, r6, r12, lr
      3a:      	bne	0x2c <c_dense_s8+0x2c>  @ imm = #-0x12
```

출력에서 볼 것:

- MAC inner loop(`0x80004ce`~`0x80004dc`)는 **5명령: `ldrsb`, `subs`, `ldrsb`, `smlabb`, `bne`** — C 버전과 명령 하나하나가 같다(레지스터 이름만 다름). `smlabb`는 Cortex-M4 DSP 확장의 16×16+32 MAC이다(E2).
- 경계 검사는 루프 **밖**, 행마다 한 번(`cmp r1, #0x800` → `bhi slice_index_fail`)만 남았다. 행 slice `&w[o·n_in..(o+1)·n_in]`가 2048 안에 있는지 검사하는 것이고, 안쪽 `zip`에는 인덱스가 없어서 검사가 생기지 않는다. 만약 `row[i] * x[i]`로 인덱싱했다면 컴파일러가 증명하지 못한 경우 루프 안에 비교·분기가 남을 수 있다 — Rust 성능 팁 "인덱스 대신 반복자/slice"의 이유다.
- requantize는 `smlal`(64비트 누산 곱셈) 하나로 SRDHM의 nudge 덧셈까지 합쳐졌고(`#0xc0000001`/`#0x40000000`이 nudge, `0x5a82799a`가 M0), clamp는 `ssat r0, #8`(8비트 포화) 한 명령이다. 둘 다 C1의 C 코드를 clang이 컴파일한 결과와 같은 패턴이다.
- 두 언어 모두 LLVM 백엔드를 쓰므로 같은 수준의 코드가 나오는 것이 자연스럽다. "Rust는 느리다/빠르다"보다 "같은 백엔드, 다른 프런트엔드 보장"으로 이해하면 된다. 본격적인 SIMD(`SMLAD`로 2 MAC/명령)는 C에서처럼 intrinsic이나 CMSIS-NN 수준의 손 최적화가 필요하다(J3).

---

## 9. 언어 경계 — C ABI로 넘는다

### 9.1 원칙

C++ 클래스, 템플릿, Rust의 `Vec`·`Result`·trait은 각 언어 안에서만 의미가 있다. 레이아웃·호출 규약·이름 맹글링이 언어(심지어 컴파일러 버전)마다 다르기 때문이다. 공통 언어는 **C ABI** 하나다 — AAPCS(Arm Procedure Call Standard)가 정한 레지스터 사용, 구조체 레이아웃, 맹글링 없는 심볼 이름. 그래서 경계 설계 규칙은 언어와 무관하게 같다.

1. **경계에는 C 타입만**: 정수, 포인터 + 길이, `#[repr(C)]`/POD 구조체, opaque handle(내용을 숨긴 구조체 포인터).
2. **에러는 정수 상태 코드**: 예외도 Rust 패닉도 경계를 넘으면 안 된다. C++은 `extern "C"` 함수 안에서 모든 실패를 코드로 바꾸고(`-fno-exceptions`면 애초에 없다), Rust는 `panic = "abort"`로 unwind 자체를 없앤다. (최근 Rust(1.81~)는 `extern "C"` 함수에서 패닉이 빠져나가려 하면 abort하도록 정의되어 있다.)
3. **메모리 소유권을 문서로 정한다**: "누가 할당하고 누가 해제하는가". 가장 안전한 펌웨어 패턴은 **호출자가 버퍼(arena)를 소유하고 빌려주는 것** — 경계 너머에서 `malloc/free`가 짝이 안 맞는 버그가 원천 차단된다.
4. **얇은 안전 래퍼**: Rust 쪽은 `unsafe` FFI 선언을 safe 타입 하나로 감싸고, 나머지 코드는 그 타입만 쓴다.

도구: **bindgen**은 C 헤더를 읽어 Rust `extern` 선언을 자동 생성하고(libclang 사용), **cbindgen**은 반대로 Rust의 `extern "C"` 함수에서 C 헤더를 생성한다. 아래 예제는 작아서 손으로 썼다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<rect x="20" y="40" width="190" height="150" rx="8" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/>
<text x="115" y="62" font-size="13" text-anchor="middle">Rust (safe)</text>
<text x="115" y="88" font-size="12" text-anchor="middle">app: model.invoke(&amp;x)?</text>
<text x="115" y="112" font-size="12" text-anchor="middle">Model&lt;'a&gt; 래퍼</text>
<text x="115" y="132" font-size="12" text-anchor="middle">unsafe는 래퍼 안에만</text>
<text x="115" y="156" font-size="12" text-anchor="middle">static ARENA 소유</text>
<text x="115" y="176" font-size="12" text-anchor="middle">panic = "abort"</text>
<rect x="245" y="40" width="190" height="150" rx="8" fill="none" stroke="currentColor" stroke-dasharray="6 4"/>
<text x="340" y="62" font-size="13" text-anchor="middle">C ABI (model_shim.h)</text>
<text x="340" y="88" font-size="12" text-anchor="middle">opaque ml_model 포인터</text>
<text x="340" y="112" font-size="12" text-anchor="middle">const int8_t 포인터 + size_t</text>
<text x="340" y="136" font-size="12" text-anchor="middle">int 상태 코드 (0 = OK)</text>
<text x="340" y="160" font-size="12" text-anchor="middle">NULL = 생성 실패</text>
<rect x="470" y="40" width="190" height="150" rx="8" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/>
<text x="565" y="62" font-size="13" text-anchor="middle">C++ (-fno-exceptions)</text>
<text x="565" y="88" font-size="12" text-anchor="middle">extern "C" 함수들</text>
<text x="565" y="112" font-size="12" text-anchor="middle">placement new → arena</text>
<text x="565" y="136" font-size="12" text-anchor="middle">TinyModel / 실제로는</text>
<text x="565" y="156" font-size="12" text-anchor="middle">tflite::MicroInterpreter</text>
<line x1="210" y1="100" x2="243" y2="100" stroke="currentColor"/>
<polygon points="237,95 245,100 237,105" fill="currentColor"/>
<line x1="435" y1="100" x2="468" y2="100" stroke="currentColor"/>
<polygon points="462,95 470,100 462,105" fill="currentColor"/>
<line x1="468" y1="140" x2="437" y2="140" stroke="currentColor"/>
<polygon points="443,135 435,140 443,145" fill="currentColor"/>
<line x1="243" y1="140" x2="212" y2="140" stroke="currentColor"/>
<polygon points="218,135 210,140 218,145" fill="currentColor"/>
<path d="M 115 190 Q 115 240 340 240 Q 565 240 565 192" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="5 4"/>
<polygon points="560,198 565,190 570,198" fill="#4a7bd0"/>
<text x="340" y="232" font-size="12" text-anchor="middle">arena 메모리: Rust가 소유, C++은 빌려 쓴다 (해제 없음)</text>
<text x="340" y="262" font-size="12" text-anchor="middle">위 화살표 = 호출, 아래 화살표 = 상태 코드 반환</text>
</svg>
```

그림 7 — Rust 앱에서 C++ 추론 런타임을 부르는 구조. 가운데 C ABI 층에는 C 타입만 지나간다. 메모리는 Rust가 소유한 arena를 빌려주고(파랑 점선), 에러는 정수 코드로 돌아온다.

### 9.2 예제 16 — C에서 Rust 커널 부르기 (`#[no_mangle] extern "C"`)

무엇을 확인하는 코드인지: 기존 C 펌웨어에 Rust로 짠 커널 하나만 넣는 경우 — Rust 함수를 C 심볼로 내보내고 C `main`에서 링크해 부른다. 7절의 `qk`를 감싼 별도 crate `qk_capi`(crate-type = `staticlib`)다.

```rust
// qk_capi/src/lib.rs   (Cargo.toml: [lib] crate-type = ["staticlib"], [profile.release] panic = "abort")
use core::slice;

/// C에서 부르는 진입점. 반환: 0 = OK, -1 = 인자 오류 (C 관례의 에러 코드로 번역)
/// # Safety: 포인터는 각각 n_in, n_in·n_out, n_out 개 원소를 가리켜야 한다.
#[unsafe(no_mangle)]
pub unsafe extern "C" fn qk_dense_s8(
    x: *const i8, w: *const i8, bias_eff: *const i32, m0: *const i32, shift: *const i32,
    n_in: usize, n_out: usize, z_y: i32, act_min: i32, act_max: i32, y: *mut i8,
) -> i32 {
    if x.is_null() || w.is_null() || bias_eff.is_null() || m0.is_null() || shift.is_null() || y.is_null() {
        return -1;
    }
    // 경계에서 딱 한 번 raw pointer → slice로 바꾸고, 안쪽은 안전한 Rust
    let (x, w) = unsafe { (slice::from_raw_parts(x, n_in), slice::from_raw_parts(w, n_in * n_out)) };
    let (be, m0, sh) = unsafe {
        (slice::from_raw_parts(bias_eff, n_out), slice::from_raw_parts(m0, n_out),
         slice::from_raw_parts(shift, n_out))
    };
    let y = unsafe { slice::from_raw_parts_mut(y, n_out) };
    match qk::dense_s8(x, w, be, m0, sh, z_y, act_min, act_max, y) {
        Ok(()) => 0,
        Err(_) => -1,
    }
}
```

```c
/* qk.h — cbindgen이 만들어 줄 헤더를 손으로 쓴 것 */
#include <stddef.h>
#include <stdint.h>
int32_t qk_dense_s8(const int8_t *x, const int8_t *w, const int32_t *bias_eff,
                    const int32_t *m0, const int32_t *shift, size_t n_in, size_t n_out,
                    int32_t z_y, int32_t act_min, int32_t act_max, int8_t *y);
```

```c
/* main.c */
#include <stdio.h>
#include "qk.h"
int main(void) {
    const int8_t w[6] = {127, -76, 30, -56, 127, 32}, x[3] = {0, 32, -96};
    const int32_t be[2] = {6803, 4568}, m0[2] = {1082196484, 1731514374}, sh[2] = {-7, -7};
    int8_t y[2];
    int rc = qk_dense_s8(x, w, be, m0, sh, 3, 2, -32, -128, 127, y);
    printf("rc=%d y=[%d, %d]\n", rc, y[0], y[1]);
    printf("rc(null w)=%d\n", qk_dense_s8(x, NULL, be, m0, sh, 3, 2, -32, -128, 127, y));
    return 0;
}
```

```text
$ cargo build --release
    Finished `release` profile [optimized] target(s) in 1.87s
$ cc -std=c11 -Wall -Wextra -O2 main.c target/release/libqk_capi.a -o c_calls_rust && ./c_calls_rust
rc=0 y=[-26, 3]
rc(null w)=-1
$ xcrun nm -gU target/release/libqk_capi.a | grep qk_dense
0000000000000000 T _qk_dense_s8
```

출력에서 볼 것: C 컴파일러 입장에서 Rust 함수는 그냥 `qk_dense_s8`이라는 심볼이다(맥의 Mach-O는 앞에 `_`를 붙인다). 결과는 C1 손계산과 같은 `[−26, 3]`, NULL은 −1. 주의: 호스트용 `libqk_capi.a`는 **18.7 MB**였다 — `qk_capi`가 `std`를 쓰는 crate라 표준 라이브러리 전체가 아카이브에 들어간다. 링커가 쓰는 것만 끌어오므로 실행 파일이 그만큼 커지진 않지만, MCU에서는 `qk_capi`도 `#![no_std]` + 패닉 핸들러를 가진 staticlib으로 만들어 C 펌웨어 링크에 넣는다. 이때 C 쪽 링커 스크립트, Rust의 target(`thumbv7em-none-eabihf`)과 C 컴파일러의 `-mfloat-abi`가 **같은 ABI**인지 확인해야 한다(hard-float와 soft-float를 섞으면 링크 에러 또는 잘못된 float 인자).

### 9.3 예제 17 — Rust에서 C shim을 거쳐 C++ 런타임 부르기 (TFLM 패턴)

무엇을 확인하는 코드인지: TFLM 같은 C++ 런타임을 Rust에서 쓰는 표준적인 모양 — C++ 쪽에 `extern "C"` shim을 만들고, Rust는 opaque handle을 safe 래퍼로 감싼다. 여기서는 TFLM 대신 3→2 dense 하나짜리 `TinyModel`을 쓰지만 구조는 같다.

```c
/* cpp/model_shim.h — C ABI 경계: C++ 타입은 하나도 노출하지 않는다 */
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct ml_model ml_model;                  /* 내용 비공개 */
ml_model *ml_model_create(void *arena, size_t arena_size);   /* 실패 시 NULL */
int ml_model_invoke(ml_model *m, const int8_t *in, size_t n_in, int8_t *out, size_t n_out);
size_t ml_model_arena_used(const ml_model *m);
#ifdef __cplusplus
}
#endif
```

```cpp
// cpp/model_shim.cpp — build.rs에서 cc::Build::new().cpp(true) + -fno-exceptions -fno-rtti로 컴파일
#include "model_shim.h"
#include <new>
namespace {
class TinyModel {                                    // 실제라면 tflite::MicroInterpreter
    static constexpr int8_t kW[2][3] = {{127, -76, 30}, {-56, 127, 32}};
public:
    int Invoke(const int8_t *in, int8_t *out) const {
        for (int o = 0; o < 2; ++o) {
            int32_t acc = 0;
            for (int i = 0; i < 3; ++i) acc += kW[o][i] * in[i];
            int32_t v = acc >> 7;                     // 단순화한 requantize
            out[o] = static_cast<int8_t>(v > 127 ? 127 : (v < -128 ? -128 : v));
        }
        return 0;
    }
};
}  // namespace
struct ml_model { TinyModel impl; size_t used; };
extern "C" ml_model *ml_model_create(void *arena, size_t size) {
    if (!arena || size < sizeof(ml_model) || reinterpret_cast<uintptr_t>(arena) % alignof(ml_model))
        return nullptr;
    return new (arena) ml_model{TinyModel{}, sizeof(ml_model)};   // placement new, 힙 없음
}
extern "C" int ml_model_invoke(ml_model *m, const int8_t *in, size_t n_in, int8_t *out, size_t n_out) {
    if (!m || n_in != 3 || n_out != 2) return -1;    // 예외 대신 상태 코드
    return m->impl.Invoke(in, out);
}
extern "C" size_t ml_model_arena_used(const ml_model *m) { return m ? m->used : 0; }
```

```rust
use core::ffi::c_void;
use core::marker::PhantomData;

#[repr(C)] struct MlModel { _private: [u8; 0] } // opaque 타입
unsafe extern "C" {
    fn ml_model_create(arena: *mut c_void, size: usize) -> *mut MlModel;
    fn ml_model_invoke(m: *mut MlModel, i: *const i8, ni: usize, o: *mut i8, no: usize) -> i32;
    fn ml_model_arena_used(m: *const MlModel) -> usize;
}

/// 안전한 래퍼: 'a = arena를 빌린 기간. Model은 arena보다 오래 살 수 없다.
pub struct Model<'a> { raw: *mut MlModel, _arena: PhantomData<&'a mut [u8]> }

#[derive(Debug)] pub enum MlError { CreateFailed, InvokeFailed(i32) }

impl<'a> Model<'a> {
    pub fn new(arena: &'a mut [u64]) -> Result<Self, MlError> { // u64 → 8바이트 정렬 보장
        let raw = unsafe { ml_model_create(arena.as_mut_ptr().cast(), arena.len() * 8) };
        if raw.is_null() { Err(MlError::CreateFailed) } else { Ok(Model { raw, _arena: PhantomData }) }
    }
    pub fn invoke(&mut self, input: &[i8; 3]) -> Result<[i8; 2], MlError> {
        let mut out = [0i8; 2];
        let rc = unsafe { ml_model_invoke(self.raw, input.as_ptr(), 3, out.as_mut_ptr(), 2) };
        if rc == 0 { Ok(out) } else { Err(MlError::InvokeFailed(rc)) }
    }
    pub fn arena_used(&self) -> usize { unsafe { ml_model_arena_used(self.raw) } }
}

fn main() {
    let mut arena = [0u64; 8]; // 64바이트 arena (스택; 실제 펌웨어라면 static)
    let mut model = Model::new(&mut arena).expect("create");
    println!("invoke -> {:?}, arena used = {} B", model.invoke(&[64, 96, -32]), model.arena_used());
    let mut tiny = [0u64; 0];
    println!("too-small arena -> {:?}", Model::new(&mut tiny).err());
    #[cfg(feature = "bad")] { arena[0] = 1; let _ = model.invoke(&[0, 0, 0]); } // model이 arena를 빌린 동안 arena 수정
}
```

```text
$ cargo run --release
invoke -> Ok([-1, 59]), arena used = 16 B
too-small arena -> Some(CreateFailed)
$ cargo build --release --features bad
error[E0506]: cannot assign to `arena[_]` because it is borrowed
  --> src/main.rs:35:31
   |
31 |     let mut model = Model::new(&mut arena).expect("create");
   |                                ---------- `arena[_]` is borrowed here
...
35 |     #[cfg(feature = "bad")] { arena[0] = 1; let _ = model.invoke(&[0, 0, 0]); } // model이 arena를 빌린 동안 arena 수정
   |                               ^^^^^^^^^^^^          ----- borrow later used here
   |                               |
   |                               `arena[_]` is assigned to here but it was already borrowed
```

출력에서 볼 것:

- 결과 `[−1, 59]`: 누산 −128, 7584를 `>> 7` 하면 −1(산술 시프트는 −∞ 쪽), 59. arena 16 B = `ml_model` 크기(빈 클래스 1 B + 패딩 + `size_t` 8 B).
- 너무 작은 arena는 C++ 쪽이 NULL을 돌려주고, Rust 래퍼가 그것을 `Err(CreateFailed)`로 바꿨다. 예외도 패닉도 경계를 넘지 않았다.
- **E0506**: 모델이 arena를 쓰고 있는 동안 arena를 덮어쓰려 하자 컴파일 에러. C/C++ 쪽은 arena를 raw pointer로 받을 뿐이지만, Rust 래퍼의 `PhantomData<&'a mut [u8]>`가 "이 Model은 arena를 `'a` 동안 배타적으로 빌렸다"를 타입에 새겼다. TFLM에서 "interpreter가 살아 있는 동안 tensor arena를 다른 용도로 재사용"하는 버그를 Rust 래퍼가 빌드 타임에 막는 셈이다.
- 이 래퍼에는 아직 구멍이 있다: `Model`에 `Drop`이 없고(C++ 소멸자를 부르지 않음 — TinyModel은 trivially destructible이라 괜찮지만 실제 런타임이면 `ml_model_destroy`를 `Drop`에서 불러야 한다), 같은 arena로 Model을 두 번 만드는 것은 `&mut` 빌림 규칙이 이미 막는다. 래퍼 설계 = "C API의 문서화된 규칙을 Rust 타입 규칙으로 옮기는 작업"이다.

---

## 10. 임베디드 Rust 생태계 (2026년 기준 개요 — 버전은 반드시 확인)

| 구성 요소 | 무엇인가 | C 펌웨어의 대응 |
|---|---|---|
| `cortex-m`, `cortex-m-rt` | 코어 레지스터·intrinsic 접근, 벡터 테이블·리셋·링커 스크립트 | CMSIS-Core, startup.s, 링커 스크립트 |
| PAC (Peripheral Access Crate) | SVD 파일에서 `svd2rust`로 생성한 레지스터 API (4.3절 아이디어의 완성판) | 벤더 디바이스 헤더(`stm32f4xx.h`) |
| HAL crate | PAC 위의 드라이버(`stm32f4xx-hal`, `nrf-hal`, `rp2040-hal` 등) | 벤더 HAL |
| `embedded-hal` (1.0, 2024년 1월 릴리스) | I2C·SPI·GPIO·delay의 **trait**(인터페이스). 센서 드라이버를 칩 독립적으로 작성 | 직접 만든 HAL 추상화 계층 |
| RTIC (v2) | 인터럽트 우선순위 기반 동시성 프레임워크. 공유 자원 잠금을 컴파일 타임에 계산(SRP 기반) | 우선순위 + critical section을 손으로 |
| Embassy | MCU용 async/await executor + HAL(`embassy-stm32`, `embassy-nrf`, `embassy-rp`) + 네트워크·BLE·USB 스택 | RTOS 태스크 + 이벤트 플래그 |
| probe-rs | 플래싱·디버깅 도구 (CMSIS-DAP, ST-Link, J-Link 등 지원). `probe-rs run`, `cargo embed` | OpenOCD, J-Link Commander |
| defmt | 포맷 문자열을 ELF에 두고 인덱스+인자만 전송하는 초경량 로그 (RTT 위에서) | 로그 ID 테이블 방식 트레이스 |
| `heapless`, `critical-section` | 고정 용량 컨테이너, 플랫폼 독립 critical section | 직접 만든 링버퍼, irq save/restore |
| Ferrocene | 안전 규격(ISO 26262, IEC 61508 등) 인증을 목표로 한 qualified Rust 컴파일러 배포판 — 인증 범위는 공식 문서 확인 | 인증된 C 컴파일러(IAR, Green Hills 등) |

Embassy를 펌웨어 엔지니어 말로 하면: "RTOS 태스크 대신 async 함수를 쓰고, 컴파일러가 각 태스크를 **상태 머신**으로 변환해 스택 하나로 돌린다". 태스크마다 스택을 따로 잡지 않으니 RAM이 줄고, `await` 지점에서만 전환되니 협력형 스케줄러다. 예를 들어 "IMU FIFO 인터럽트를 await → 샘플을 링버퍼로 → 창이 차면 추론" 같은 흐름이 순차 코드처럼 써진다.

ML 관련 Rust crate (MCU 적합성은 신중하게):

- **tract** (Sonos): ONNX·NNEF(·일부 TF) 추론 엔진. `std` 기반 — 임베디드 Linux/Cortex-A급에서 쓰이고, bare-metal MCU용은 아니다.
- **candle** (Hugging Face): PyTorch 스타일 텐서 라이브러리, LLM 추론 예제 다수. `std` 기반 — 서버/PC/모바일급.
- **burn**: 학습·추론 프레임워크, 여러 backend. 문서상 일부 backend(ndarray)로 `no_std` 추론을 지원한다고 밝히지만, MCU에서의 메모리 사용·성능은 직접 검증해야 한다.
- **MicroFlow**: Rust로 작성된 TinyML 추론 엔진을 다룬 연구 프로젝트/논문이 있다(컴파일러 방식으로 MCU를 겨냥). 연구 단계로 보고 실무 채택 전 상태를 확인할 것.
- 현실적인 MCU 경로: **TFLM이나 벤더 런타임(C/C++)을 9.3절처럼 C shim으로 감싸 Rust에서 부르거나**, 작은 모델이면 7절처럼 커널을 직접 쓴다. 대부분의 vendor NPU SDK(예: Arm Ethos-U 드라이버, Qualcomm QNN, ST Edge AI 생성 코드)는 C/C++ API를 제공하므로 경계 설계 능력이 더 중요하다(제품마다 확인).

---

## 11. ML 컴포넌트의 언어 고르기

| 기준 | C | C++ (펌웨어 부분집합) | Rust |
|---|---|---|---|
| 팀 숙련도·채용 | 가장 넓음 | 넓음, 단 "어떤 C++인가"의 합의 필요 | 아직 좁음, 학습 곡선 큼 |
| 메모리 안전 | 리뷰·정적 분석(MISRA, 커버리티)에 의존 | RAII·span으로 개선, 근본은 C와 같음 | 컴파일러가 강제 (unsafe 블록만 리뷰) |
| vendor SDK·NPU 런타임 연동 | 직접 (대부분 C API) | 직접 (TFLM 등 C++ API 그대로) | C shim/bindgen 필요 |
| ML 런타임 선택지 | CMSIS-NN, 벤더 생성 코드 | TFLM, 벤더 런타임 | 소수, MCU용은 미성숙 → C/C++ 런타임 호출이 현실적 |
| 코드 크기·성능 | 기준선 | 부분집합이면 C와 동등 (1, 2절) | 동등 (8절: 같은 inner loop), 패닉·fmt 크기 주의 |
| 컴파일 타임 계산·검사 | 매크로, 외부 스크립트 | `constexpr`, `static_assert`, 템플릿 | `const fn`, const generics, 타입 시스템 |
| 호스트 테스트 | 하네스 별도 구성 | 하네스 별도 구성 | `cargo test` 내장 (7.4절) |
| 안전 인증 | 성숙 (MISRA C, 인증 컴파일러) | MISRA C++, AUTOSAR C++14 | Ferrocene 등 진행 중, 산업별 확인 |
| 디버깅·툴 | 모든 IDE·트레이스 도구 | 거의 동일 | probe-rs, defmt — 벤더 IDE 통합은 상대적으로 약함 |

Don이 면접에서 쓸 수 있는 결론: "추론 런타임과 NPU 경계는 SDK가 정한 C/C++로, 그 위의 신호 처리·상태 머신·프로토콜 파서처럼 **버그가 비싸고 입력이 외부에서 오는 코드**는 팀이 준비되어 있다면 Rust를 고려한다. 어느 경우든 경계는 C ABI, 메모리는 정적 arena, 에러는 코드."

---

## 12. 임베디드 관점에서 다시 보기 — 체크리스트

C++이든 Rust든 추론 코드를 펌웨어에 넣을 때 확인할 것:

1. **플래그**: C++은 `-fno-exceptions -fno-rtti`(+ bare-metal이면 `-fno-threadsafe-statics`), Rust는 `panic = "abort"`, `opt-level = "s"` 또는 `"z"`, `lto = true`, `codegen-units = 1`.
2. **미해결 심볼 검사**: object/라이브러리에 `llvm-nm -u`를 돌려 `__cxa_*`, `__gxx_personality_v0`, `malloc`, `operator new`가 없는지 CI에서 확인(예제 1·2). Rust는 map 파일에서 `core::fmt` 크기를 본다.
3. **정적 메모리만**: arena·링버퍼·모델 버퍼 크기는 템플릿 파라미터/const generic/`static` 배열로 컴파일 타임에 고정. 링커가 RAM 초과를 빌드에서 잡게 한다.
4. **전역 초기화**: C++ 전역은 `constexpr`/`constinit`, 하드웨어 의존 초기화는 `main`의 명시적 순서. Rust `static`은 언어상 상수 초기화만 허용되므로 이 문제가 없다(런타임 초기화가 필요하면 `static` 안에 `Option`/once-cell류를 명시적으로).
5. **inner loop 확인**: 디스어셈블해서 MAC 루프에 경계 검사·간접 호출·함수 호출이 없는지(예제 15). 이건 언어와 무관한 습관이다.
6. **경계**: C ABI만, 상태 코드, 호출자 소유 버퍼, float ABI(hard/soft) 일치, 정렬(arena `alignas(16)`, Rust는 `#[repr(align(16))]` 래퍼 타입).
7. **bit-exact 테스트**: 언어를 바꿔 포팅한 커널은 레퍼런스와 정수 출력이 완전히 같아야 한다(예제 13). J6의 golden vector 체계에 그대로 넣는다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| C++ 라이브러리 하나를 예외 켠 채 빌드 | flash가 갑자기 수십 KB 증가, 링크에 `__cxa_throw` 등장 | 한 TU라도 예외를 쓰면 unwinder 전체가 링크 | 모든 TU에 같은 플래그, CI에서 `nm -u` 검사 |
| virtual 소멸자만 있고 힙은 안 씀 | "undefined reference to operator delete" 링크 에러 | deleting destructor가 `operator delete`를 참조 | protected non-virtual 소멸자, 또는 빈 `operator delete` 정의 |
| 다른 TU 전역을 생성자에서 읽음 | 파일 추가·링크 순서 변경 후 값이 0 (예제 3) | 동적 초기화 순서 미정 | `constexpr`/`constinit`, 명시적 `init()` |
| 링버퍼 크기를 100으로 | 인덱스 wrap 시 데이터 꼬임 | `& (N−1)` 마스크가 2의 거듭제곱 가정 | `static_assert` (예제 6) |
| Rust 커널에서 `x[i]` 인덱싱 루프 | 기대보다 느림, 디스어셈블에 루프 안 `cmp/bhi` | 경계 검사 제거 실패 | 반복자·`zip`·미리 자른 slice |
| `no_std` 바이너리에 포매팅 패닉 핸들러 | flash 수 KB 증가 | `core::fmt` 링크 | `panic-halt`/`panic-abort`, defmt |
| Rust 패닉이 C 콜스택을 넘어감 | 정의되지 않은 동작 또는 즉시 abort | unwind 경계 위반 | `panic = "abort"`, FFI 함수에서 `Result` → 코드 변환 |
| hard-float Rust + soft-float C 링크 | 링크 에러(ABI 속성 불일치) 또는 float 인자 쓰레기 | `eabihf` vs `-mfloat-abi=soft` | 양쪽 ABI 통일 |
| FFI 래퍼가 arena 수명을 표현 안 함 | interpreter 사용 중 arena 재사용 → 무작위 출력 | raw pointer만 저장 | lifetime 있는 래퍼(`PhantomData`, 예제 17) |
| `heapless::spsc::Queue<T, N>`에 N개 넣으려 함 | 마지막 하나가 거절됨 | (0.9 기준) 실제 용량 N−1 | 문서 확인, N을 하나 크게 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "What C++ features do you avoid in firmware, and why?"

**A.** 기준은 "비용이 숨은 기능"이다. 예외는 unwind 테이블과 런타임 라이브러리, 예외 객체 할당, 비결정적 시간을 가져오고, RTTI는 타입 정보 테이블과 `__dynamic_cast` 런타임을 가져온다. 그래서 `-fno-exceptions -fno-rtti`로 끄고, 실제로 object의 미해결 심볼을 보면 12개에서 0개로 줄어든다. 힙은 초기화 이후 금지, iostream 금지, 동적 초기화 전역 객체는 순서 문제 때문에 피한다. 대신 RAII, 템플릿, `constexpr`, `std::array/span`처럼 zero-overhead 기능은 적극 쓴다.

> "I avoid features whose cost is hidden at link time: exceptions bring unwind tables, an unwinder runtime and heap-allocated exception objects with non-deterministic timing; RTTI brings type-info tables and the dynamic_cast runtime. So I build with -fno-exceptions and -fno-rtti and check the undefined symbols of every object in CI. No heap after init, no iostreams, and no dynamically initialized globals because of the static initialization order problem. What I do use heavily are the zero-overhead parts: RAII for locks and peripherals, templates with sizes as parameters, constexpr tables and static_assert, std::array and std::span."

**Q.** "How would you write a fixed-size ring buffer in C++ or Rust?"

**A.** 크기 N을 템플릿 파라미터(Rust는 const generic)로 받아 배열을 객체 안에 두고, `static_assert`로 N이 2의 거듭제곱인지 강제한다. 인덱스는 접지 않고 계속 증가시키다가 쓸 때만 `& (N−1)` — 그러면 N칸을 다 쓰고 unsigned 뺄셈이 2³² wrap에도 맞는다. SPSC면 생산자는 head만, 소비자는 tail만 쓰고, 데이터를 쓴 뒤 release로 head를 공개, 반대쪽은 acquire로 읽는다. Rust면 `heapless::spsc::Queue`가 있지만 용량 규칙(N−1)을 문서로 확인한다.

> "I make the capacity a compile-time parameter — a template argument in C++, a const generic in Rust — so the storage lives inside the object and the queue can be a static with no heap. A static_assert enforces a power of two, so wrapping is a single AND with N minus one. I let head and tail run freely as 32-bit counters and mask only on access, which uses all N slots and makes the unsigned difference correct across wrap-around. For a single producer such as an ISR and a single consumer task, the producer writes the slot and then publishes head with release semantics, and the consumer reads head with acquire. On a Cortex-M7 or with DMA I'd also think about barriers and cache maintenance."

**Q.** "Explain Rust ownership to a C developer."

**A.** DMA 버퍼 소유권 규칙을 컴파일러가 검사한다고 생각하면 된다. 모든 값에는 주인이 하나 있고, 넘기면(move) 넘긴 쪽은 못 쓴다 — use-after-free가 컴파일 에러다. 빌려줄 때는 읽기 참조 여러 개 또는 쓰기 참조 하나만 — data race가 타입 에러가 된다. 참조는 원본보다 오래 못 산다 — dangling pointer가 컴파일 에러다. 전부 컴파일 타임 검사라 생성 코드는 C와 같은 포인터다.

> "Think of the buffer-ownership rules we enforce by code review in firmware — who owns a DMA buffer, who may write it, who frees it — and make the compiler check them. Every value has exactly one owner; passing it by value moves ownership, so using the old variable is a compile error instead of a use-after-free. You can lend it either to many readers or to exactly one writer, never both, which turns data races into type errors. And a reference can't outlive what it points to, which eliminates dangling pointers. It's all checked at compile time; the generated code is the same pointers and loops you'd write in C."

**Q.** "How do you call a C (or C++) inference library from Rust?"

**A.** C++ 런타임이면 먼저 C++ 쪽에 `extern "C"` shim을 만든다 — opaque handle, 포인터+길이, 정수 상태 코드만 노출. Rust 쪽은 bindgen이나 손으로 `extern "C"` 선언을 쓰고, `build.rs`에서 `cc` crate로 C/C++을 같은 플래그로 빌드한다. 그 위에 safe 래퍼 타입을 하나 두고 `unsafe`는 거기에만. arena는 Rust가 소유해 빌려주고, 래퍼의 lifetime으로 "모델은 arena보다 오래 못 산다"를 표현한다. 패닉은 abort로, 에러는 `Result`로 변환한다.

> "For a C++ runtime like TFLM I first write a thin extern C shim that exposes only an opaque handle, pointer-and-length buffers and integer status codes, compiled with -fno-exceptions. On the Rust side I declare those functions — by hand or with bindgen — and build the C++ with the cc crate from build.rs. Then I wrap the handle in one safe Rust type, so unsafe lives only there. The tensor arena is owned by Rust and lent to the runtime, and the wrapper carries a lifetime tied to the arena, so the borrow checker prevents reusing the arena while the model is alive. Panics are set to abort and status codes become Result values."

**Q.** "Why constexpr tables instead of computing at boot or generating headers?"

**A.** 부팅 시 계산은 RAM과 부팅 시간, float 라이브러리를 쓴다. 외부 스크립트로 만든 헤더는 flash에 들어가지만 파라미터(scale 등)와 코드가 따로 놀아 어긋나도 모른다. `constexpr`이면 컴파일러가 빌드 중에 계산해 `.rodata`에 넣고, 파라미터가 같은 소스에 있으며, `static_assert`로 핵심 값을 빌드 때 검사한다. 실제로 sigmoid int8 LUT를 이렇게 만들면 flash 256 B, 런타임 함수는 로드 한 번이고, 표 전체가 numpy 결과와 체크섬까지 같았다.

> "A constexpr table is computed by the compiler and lands in flash as read-only data, so it costs no RAM, no boot time and doesn't drag in the floating-point library. Unlike a header generated by a separate script, the parameters such as the input scale live in the same source as the code that uses them, so they can't silently drift, and static_assert lets me check key entries at build time. For example, an int8 sigmoid LUT built this way is 256 bytes of rodata, the lookup is a single load, and I verified the whole table against numpy with a checksum."

**Q.** "Would you use Rust for the ML part of a wearable's firmware?"

**A.** 부분적으로. 추론 런타임과 NPU 경계는 vendor SDK가 C/C++이라 그대로 두고, 그 주변의 신호 처리·상태 머신·프로토콜 파서처럼 외부 입력을 다루고 버그가 비싼 코드에서 Rust가 이득이 크다. 같은 백엔드라 성능은 같다(같은 inner loop를 확인했다). 결정 요인은 팀 숙련도, 툴체인·디버거 지원, 인증 요구다. 도입한다면 C ABI 경계, 정적 arena, bit-exact 테스트로 기존 C 코드와 나란히 검증하면서 점진적으로.

> "Selectively. The inference runtime and NPU interface would stay in C or C++ because that's what the vendor SDKs ship. Rust pays off most in code that parses external input and is expensive to get wrong — signal-processing glue, state machines, protocol parsers. Performance isn't the issue: it's the same LLVM backend, and I've checked that an int8 dot-product loop compiles to identical Cortex-M4 instructions. The real decision factors are team skills, debugger and vendor tool support, and certification requirements. I'd introduce it incrementally behind a C ABI, with static arenas and bit-exact tests against the existing C reference."

---

## 15. 직접 해보기

1. 예제 1의 `shapes.cpp`에 `static Imu local;`을 가진 함수를 추가하고 `-fno-threadsafe-statics` 유무로 `nm -u`를 비교하라. 정답: 플래그가 없으면 `__cxa_guard_acquire/__cxa_guard_release`가 나타나고, 켜면 사라진다(소멸자가 non-trivial이면 `__cxa_atexit`은 여전히 남는다).
2. 손계산: `RingBuffer<T, 16>`에서 head = 3, tail = 0xFFFFFFF9일 때 개수와, 다음 pop이 읽을 slot은? 정답: 3 − (2³² − 7) = 10개(mod 2³²), slot = 0xFFFFFFF9 & 15 = 9.
3. 예제 5를 tanh LUT로 바꿔라. 출력 scale 1/128, zero-point 0, 입력 scale 0.05. `tanh(x) = 2σ(2x) − 1`을 이용해 `cexp`를 재사용하고, q_in = 0 → 0, q_in = 20 → round(tanh(1.0)·128) = round(97.48) = 97을 `static_assert`로 확인하라. 정답: Python으로 같은 표를 만들어 체크섬 비교.
4. Rust: 7.2절 `dense_s8`의 inner loop를 `for i in 0..n_in { acc += row[i] as i32 * x[i] as i32; }`로 바꾸고 8.3절처럼 디스어셈블하라. 힌트: 루프 안에 경계 검사가 남는지는 컴파일러가 `row.len() == x.len()`을 증명할 수 있는지에 달렸다 — 결과를 직접 보고 설명하라.
5. Rust: `qk`에 `fn argmax(y: &[i8]) -> Option<usize>`를 추가하고, 빈 slice → `None`, 동점이면 첫 인덱스를 돌려주는 `#[test]`를 써라. 정답: `y.iter().enumerate().max_by_key(...)`는 동점에서 **마지막**을 고르므로, 첫 인덱스가 필요하면 `fold`로 직접 쓰거나 `rev()`를 조합한다.
6. 예제 17의 `Model`에 `Drop`을 구현해 C 쪽 `ml_model_destroy`(새로 추가)를 부르게 하고, `ml_model_destroy`가 placement new로 만든 객체의 소멸자를 명시적으로 호출(`m->~ml_model()`)하게 하라. 힌트: placement new로 만든 객체는 `delete`가 아니라 소멸자 직접 호출로 정리한다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| zero-overhead | 쓰지 않으면 비용 0 | 템플릿·RAII·constexpr처럼 손으로 쓴 C와 같은 비용의 기능 |
| unwinder | 스택 되감기 런타임 | 예외가 던져질 때 프레임을 거슬러 올라가며 소멸자 호출 |
| RTTI | Run-Time Type Information | `typeid`, `dynamic_cast`용 타입 정보 테이블 |
| RAII | 자원 획득 = 초기화 | 생성자에서 잡고 소멸자에서 놓아 모든 출구에서 해제 보장 |
| static initialization order fiasco | 정적 초기화 순서 문제 | TU 간 전역 객체 동적 초기화 순서가 정해지지 않음 |
| `constexpr` / `constinit` | 컴파일 타임 계산 / 상수 초기화 강제 | 표를 `.rodata`에 굽고, 동적 초기화를 금지 |
| placement new | 주어진 주소에 객체 생성 | 힙 없이 arena에 생성자 실행 |
| `std::span` | 포인터 + 길이 뷰 (C++20) | 버퍼 인자의 길이 손실 방지 |
| ownership / move | 값의 유일한 주인 / 주인 이전 | 이동 후 사용은 컴파일 에러 |
| borrow (`&T`, `&mut T`) | 빌림 (공유 / 배타) | 읽기 여럿 또는 쓰기 하나 |
| lifetime | 참조가 유효한 범위 | 참조는 원본보다 오래 살 수 없다 |
| `no_std` | `std` 없이 `core`만 | OS 없는 MCU용 Rust |
| `unsafe` | 컴파일러가 증명 못 하는 약속 | raw pointer, FFI, `static mut` 등에만 |
| panic / `panic = "abort"` | 복구 불가 오류 / unwind 없이 중단 | 펌웨어·FFI 경계에서는 abort 설정 |
| C ABI | C 호출 규약·레이아웃 | 언어 경계의 공통어 (AAPCS on Arm) |
| opaque handle | 내용을 숨긴 포인터 | C API가 C++ 객체를 노출하지 않는 방법 |
| bindgen / cbindgen | C→Rust / Rust→C 바인딩 생성기 | FFI 선언·헤더 자동 생성 |
| PAC / HAL | 레지스터 crate / 드라이버 crate | SVD에서 생성한 레지스터 API와 그 위 드라이버 |
| embedded-hal | 주변장치 trait 모음 | 칩 독립 센서 드라이버 작성 기반 |
| Embassy / RTIC | async executor / 우선순위 기반 동시성 | RTOS 태스크의 Rust식 대안 |
| probe-rs / defmt | 플래싱·디버거 / 초경량 로그 | J-Link·OpenOCD / 로그 ID 방식에 대응 |

---

## 17. 요약 & 체크리스트

펌웨어 C++의 핵심은 "비용이 숨은 기능(예외, RTTI, 힙, iostream, 동적 초기화)은 끄고, 비용이 0인 기능(RAII, 템플릿, `constexpr`, `std::array/span`, placement new)은 적극 쓴다"이다. 예외·RTTI를 끄면 object 크기보다 **링크가 끌어올 런타임 심볼**(12개 → 0개)이 사라지는 것이 핵심이고, `constexpr` LUT는 flash에 구워지며 `static_assert`로 빌드 때 검증된다. TFLM은 이 부분집합의 실제 사례다(`MicroMutableOpResolver<N>`, 정적 arena, 상태 코드). Rust는 C 펌웨어에서 사람이 지키던 버퍼 소유권·reader/writer·수명 규칙을 컴파일러가 강제하는 언어이고, `no_std`로 같은 커널을 호스트 `cargo test`와 Cortex-M4 바이너리에 그대로 쓸 수 있으며, inner loop는 C와 같은 기계어로 내려간다. 언어가 섞이는 곳은 C ABI·정적 arena·상태 코드·`panic = "abort"`로 설계한다.

- [ ] `-fno-exceptions -fno-rtti`가 무엇을 없애는지 `nm -u` 출력으로 설명할 수 있다
- [ ] virtual 소멸자가 `operator delete`를, 전역 객체가 `__cxa_atexit`을 끌어오는 이유를 말할 수 있다
- [ ] 정적 초기화 순서 문제를 재현하고 `constexpr`/`constinit`/명시적 `init()`으로 고칠 수 있다
- [ ] RAII critical section을 짜고 early return에서도 복원됨을 보일 수 있다
- [ ] `constexpr` int8 LUT를 만들고 `static_assert`와 Python 체크섬으로 검증할 수 있다
- [ ] `RingBuffer<T, N>`을 2의 거듭제곱 static_assert와 free-running 인덱스로 짤 수 있다
- [ ] Rust의 move/borrow/lifetime 에러(E0382, E0499, E0515)를 C 버그와 짝지어 설명할 수 있다
- [ ] `no_std` Rust int8 커널을 C 레퍼런스와 bit-exact로 `cargo test`할 수 있다
- [ ] `thumbv7em-none-eabihf` 바이너리를 빌드해 섹션 크기와 inner loop를 확인할 수 있다
- [ ] C++ 런타임을 C shim + Rust safe 래퍼로 감싸는 구조를 그리고 설명할 수 있다

---

## 참고 자료

- Bjarne Stroustrup, "The C++ Programming Language" (4th ed.) — zero-overhead 원칙, RAII
- cppreference.com — [constexpr](https://en.cppreference.com/w/cpp/language/constexpr), [std::span](https://en.cppreference.com/w/cpp/container/span), [static initialization order fiasco](https://en.cppreference.com/w/cpp/language/siof), [placement new](https://en.cppreference.com/w/cpp/language/new)
- The Rust Programming Language ("the book") — [doc.rust-lang.org/book](https://doc.rust-lang.org/book/) — 4장 Ownership, 9장 Error Handling, 20장 Unsafe Rust
- The Embedded Rust Book — [docs.rust-embedded.org/book](https://docs.rust-embedded.org/book/) — `no_std`, 메모리 맵, 인터럽트, C interop
- The Rustonomicon — [doc.rust-lang.org/nomicon](https://doc.rust-lang.org/nomicon/) — FFI, unsafe의 규칙
- Rust Embedded WG 저장소: `rust-embedded/cortex-m`, `rust-embedded/embedded-hal`, `rust-embedded/svd2rust`
- Embassy — [embassy.dev](https://embassy.dev/), RTIC — [rtic.rs](https://rtic.rs/), probe-rs — [probe.rs](https://probe.rs/), defmt — `knurling-rs/defmt`
- crates: `heapless`, `cortex-m-rt`, `panic-halt`, `cc`, `bindgen`, `cbindgen` (docs.rs에서 버전별 문서)
- gemmlowp — `google/gemmlowp` (SaturatingRoundingDoublingHighMul, RoundingDivideByPOT의 원본)
- TensorFlow Lite Micro — `tensorflow/tflite-micro` (F2 참고)
- Arm, "Procedure Call Standard for the Arm Architecture (AAPCS)" — C ABI의 정의
