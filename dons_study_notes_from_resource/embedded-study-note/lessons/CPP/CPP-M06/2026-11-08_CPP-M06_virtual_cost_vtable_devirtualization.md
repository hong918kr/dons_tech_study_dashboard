---
id: CPP-M06
time: 80분
updated: 2026-11-08
---
# CPP-M06 virtual의 실제 비용: vtable·간접호출·devirtualization

## 학습 목표

- 가상 함수의 비용을 **RAM(객체마다 vptr) / 플래시(클래스마다 vtable, typeinfo) / 사이클(간접 호출, 인라인 불가)** 세 칸으로 나눠, Itanium ABI 규칙으로 **바이트 단위로 예측**하고 실제 ELF 의 `nm -S` 와 맞춰 본다.
- 드라이버 3종을 multi-TU 로 빌드해 `-frtti`/`-fno-rtti`, `final`, `-flto`, `--gc-sections` 조합의 결과를 `size`/`nm`/`objdump` 로 측정한다 (arm-none-eabi-g++ 14.2.1, Cortex-M4).
- **간접 호출이 언제 직접 호출로 바뀌는가**를 호출 지점별로 어셈블리에서 확인한다. `final` 이 못 푸는 호출, LTO 가 푸는 호출, 아무도 못 푸는 호출을 구분한다.
- "vtable 이 호출 안 되는 가상 함수까지 잡아 둔다", "virtual 소멸자가 `operator delete` → `malloc` 을 끌고 온다" 같은 **링크 수준 비용**을 재현하고 끊는 방법을 안다.
- `protected` 비가상 소멸자 인터페이스, 손으로 만든 ops 테이블, guarded devirtualization 을 구현한다.

## 1. 문제

펌웨어 면접에서 "C++ virtual 쓰면 느리고 커지지 않나요?"는 단골 질문이고, "vtable 때문에 조금요"라는 답은 탈락감이다. 이 레슨은 **실제 ELF 로 숫자를 재고**, 어디가 진짜 비용인지 말할 수 있게 만든다. 스포일러: 가장 큰 비용은 vtable 도 간접 호출도 아니었다.

vtable 의 슬롯 구조와 `-fno-rtti` 가 슬롯을 0으로 채운다는 사실은 CPP-B13, 클래스 레이아웃은 CPP-B08, CRTP 로 virtual 을 없애는 방법은 CPP-M05 에서 다룬다. 여기서는 **회계와 측정**만 한다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">가상 호출의 세 가지 비용 — nortti_gc.elf 실측 주소</text>
  <text x="20" y="50" fill="currentColor">RAM (.data)</text>
  <rect x="20" y="58" width="200" height="30" class="f-warn" stroke="currentColor"/>
  <rect x="20" y="88" width="200" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="20" y="118" width="200" height="30" class="f-mut" stroke="currentColor"/>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="28" y="78">0x18264 vptr=0x00008208</text>
    <text x="28" y="108">0x18268 base_=0x40004400</text>
    <text x="28" y="138">0x1826C hz_=0</text>
  </g>
  <text x="20" y="168" fill="currentColor">g_uart 12 B 중 vptr 4 B</text>
  <text x="20" y="186" fill="currentColor">객체 수만큼 곱해진다</text>
  <text x="300" y="50" fill="currentColor">플래시 (.rodata) vtable for UartDrv, 24 B</text>
  <g stroke="currentColor">
    <rect x="300" y="58" width="240" height="26" class="f-mut"/>
    <rect x="300" y="84" width="240" height="26" class="f-mut"/>
    <rect x="300" y="110" width="240" height="26" class="f-ok"/>
    <rect x="300" y="136" width="240" height="26" class="f-ok"/>
    <rect x="300" y="162" width="240" height="26" class="f-acc"/>
    <rect x="300" y="188" width="240" height="26" class="f-ok"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="308" y="76">0x8200 offset_to_top = 0</text>
    <text x="308" y="102">0x8204 typeinfo = 0 (nortti)</text>
    <text x="308" y="128">0x8208 init     0x80d5</text>
    <text x="308" y="154">0x820c write    0x80ed</text>
    <text x="308" y="180">0x8210 status   0x8001</text>
    <text x="308" y="206">0x8214 selftest 0x8115</text>
  </g>
  <path d="M222 73 L296 122" stroke="currentColor" fill="none"/>
  <text x="552" y="128" fill="currentColor">클래스당 1벌</text>
  <text x="552" y="146" fill="currentColor">객체 수와 무관</text>
  <text x="552" y="180" fill="currentColor">via_base 는 +8 을</text>
  <text x="552" y="198" fill="currentColor">읽고 분기한다</text>
  <text x="20" y="240" fill="currentColor" font-weight="bold">사이클 (명령어 수준, Cortex-M4 실측 아님):</text>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="20" y="262">ldr r3,[r0,#0]   ; vptr 읽기 (데이터 버스)</text>
    <text x="20" y="282">ldr r3,[r3,#8]   ; 슬롯 읽기 (플래시, wait state 영향)</text>
    <text x="20" y="302">bx  r3 / blx r3  ; 간접 분기 (파이프라인 재충전)</text>
  </g>
  <text x="20" y="324" fill="currentColor">그리고 가장 큰 항목: 호출 대상을 모르니 인라인 불가.</text>
</svg>
```

측정 대상은 Part A의 드라이버 3종을 표준 헤더 없이 옮긴 `demo_bus.hpp` + TU 5개다. `demo_app.cpp` 는 호출 방식마다 `noinline` 함수를 하나씩 둔다 (`via_base`, `via_ref`, `via_object`, `via_console`, `fanout`). LTO 는 **호출이 TU 를 넘을 때만** 의미가 있어서 single-TU 로는 이 실험이 안 된다.

### Part A — 드라이버 3종 + Itanium ABI 회계 모델

- `ByteBus` 인터페이스(`init`/`write`/`status`, 소멸자는 **protected 비가상**)를 구현하는 `UartDrv`, `SpiDrv`, `I2cDrv` (모두 `final`)의 TODO 를 채운다. mock 레지스터 로그로 순서를 검증한다.
- `vtable_bytes(Abi, Shape)`: `offset_to_top + typeinfo + (dtor 슬롯 2개) + 가상 함수` × 포인터 크기.
- `typeinfo_bytes(Abi, Shape)`: 루트 2포인터 / 단일 상속 3포인터 + 이름 문자열(NUL 포함). RTTI off 면 0.
- `object_bytes(Abi, field_bytes, field_align)`: vptr + 패딩 + 멤버, 전체 정렬로 올림.
- 하네스의 기대값은 **ELF 실측값**이다. 호스트 `sizeof` 와도 맞춰 본다.

### Part B — 손으로 만드는 vtable 과 guarded devirtualization

- `make_ops<D>()`: 캡처 없는 람다로 `Ops` 함수 포인터 표를 만들되, 안에서 `p->D::write(s)` **한정 호출**(qualified call)로 vtable 을 우회한다.
- `guarded_write`: `ops == &kUartOps` 면 직접 호출, 아니면 함수 포인터. 고정 시드 xorshift32 혼합 부하 1000회로 카운터를 검증한다.
- 생성자/소멸자 안의 가상 호출이 무엇을 보는지 **예측값 3개**를 채운다.

### Part C — 보드 레지스트리와 예산

- `constinit` 전역 드라이버 + `ByteBus* const g_buses[]` 표, `fanout()` 은 `status()==0` 인 버스를 건너뛴다.
- `board_budget(Abi, rows)`: 클래스별 vtable/typeinfo 는 1벌, 객체와 vptr 은 인스턴스 수만큼. 결과를 ELF 의 `.data` 40 B, vtable 72 B 와 대조한다.

## 2. Problem (English)

> **Measure what `virtual` really costs on a Cortex-M4.**
>
> 1. Implement three `final` drivers behind an abstract `ByteBus` interface whose destructor is protected and non-virtual. `init(0)` must fail without touching registers; `write` before `init` returns 0.
> 2. Write constexpr functions predicting, for an Itanium-ABI target, the vtable size, the typeinfo size and the object size of a class. Validate them against `nm -S` output from a real `arm-none-eabi-g++` link and against `sizeof` on the host.
> 3. Implement a hand-written ops table whose entries use qualified (non-virtual) calls, and a `guarded_write` that takes a direct path for one known driver and an indirect path otherwise.
> 4. Without running anything: what does a virtual call made from a base-class constructor dispatch to, and why?
> 5. Build the drivers across multiple translation units. Report, per call site, whether `final` and `-flto` turn the indirect call into a direct one. Explain any call that stays indirect.
>
> Example: for a class with 4 virtual functions and no virtual destructor on ILP32, the vtable is `(2 + 4) * 4 = 24` bytes. With RTTI, `UartDrv`'s typeinfo is `12 + 9 = 21` bytes (12-byte `__si_class_type_info` plus the string `"7UartDrv"` and its NUL).

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti starter.cpp -o starter && ./starter
# 처음엔 "28/82 passed", exit 1 — TODO 를 채워 82/82 를 만든다
```

측정 실험은 Lima VM 에서 돌린다 (맥에는 `ld.lld` 도 arm-none-eabi 링커도 없다):

```bash
limactl shell elinux -- bash -c 'mkdir -p /tmp/CPP-M06 && cp <레슨 디렉터리>/* /tmp/CPP-M06/ && cd /tmp/CPP-M06 && bash measure.sh'
# 이어서: bash measure.sh dis nortti | gc | vfe | pure
```

## 4. 힌트

<details>
<summary>힌트 1 — 슬롯 개수부터 세라</summary>

앞의 2칸(`offset_to_top`, typeinfo)은 RTTI 를 꺼도 사라지지 않는다 (CPP-B13 참고). virtual 소멸자는 슬롯을 **2개** 먹는다: complete(D1)와 deleting(D0).

</details>

<details>
<summary>힌트 2 — object_bytes 는 두 번 올림한다</summary>

먼저 멤버 시작 위치를 `field_align` 의 배수로 올리고(vptr 뒤 패딩), 끝을 `max(ptr, field_align)` 의 배수로 올린다(꼬리 패딩). ILP32 에서 `double` 멤버면 vptr 뒤에 패딩 4가 생긴다.

</details>

<details>
<summary>힌트 3 — 왜 D::write 로 쓰나</summary>

`static_cast<D*>(p)->write(s)` 는 가상 호출 그대로다 (`D` 가 `final` 이면 컴파일러가 풀어 주겠지만, 그건 최적화일 뿐이다). `p->D::write(s)` 는 **언어 규칙상** 비가상 호출이다 ([class.virtual]).

</details>

<details>
<summary>힌트 4 — 생성자 안의 kind()</summary>

`ProbeBase()` 가 실행되는 동안 객체의 동적 타입은 무엇인가? 파생 부분은 아직 없다. [class.cdtor] 를 떠올려라.

</details>

<details>
<summary>힌트 5 — board_budget 에서 instances == 0</summary>

객체가 없는 클래스는 vtable 도 플래시에 안 남는다는 가정이다 (gc-sections). 그 행은 건너뛴다.

</details>

## 5. 풀이

### Part A — 회계 모델

표준은 vtable 을 언급하지 않는다. 슬롯 배치는 **Itanium C++ ABI** 약속이고, Cortex-M 은 그 변형인 ARM C++ ABI 를 쓴다. 측정값을 먼저 보자 (`measure.sh`, `-O2 -fno-exceptions -mcpu=cortex-m4 -mthumb`, `nm -C -S`):

```text
00008230 00000018 R vtable for I2cDrv
00008218 00000018 R vtable for SpiDrv
00008200 00000018 R vtable for UartDrv
```

0x18 = 24 = (2 + 4) × 4. `ByteBus` 의 vtable 은 **없다** — 객체가 전부 상수 초기화되어(아래 `.data` 덤프) 기반 vtable 을 참조하는 코드가 애초에 생기지 않았다.

RTTI 를 켜면 (`rtti_gc.elf`):

```text
0000a104 00000008 V typeinfo for ByteBus
0000a0f8 00000009 V typeinfo name for ByteBus
0000a118 0000000c R typeinfo for UartDrv
0000a10c 00000009 R typeinfo name for UartDrv
0000a144 0000000c R typeinfo for SpiDrv
0000a13c 00000008 R typeinfo name for SpiDrv
0000a170 0000000c R typeinfo for I2cDrv
0000a168 00000008 R typeinfo name for I2cDrv
```

| 항목 | 모델 | 실측 |
|---|---|---|
| vtable, 4 virtual, dtor 없음 | (2+4)×4 = 24 | `0x18` ×3 |
| vtable, virtual dtor | (2+2+4)×4 = 32 | `vtables=3 (96B)` |
| typeinfo UartDrv | 3×4 + 8+1 = 21 | 0xc + 9 |
| typeinfo SpiDrv | 3×4 + 7+1 = 20 | 0xc + 8 |
| typeinfo ByteBus (루트) | 2×4 + 8+1 = 17 | 8 + 9 |
| g_uart / g_spi | 4 + 8 = 12 | `.data` 덤프 |
| g_i2c | 4 + 9 → 16 | `.data` 덤프 |

세 객체 합 12+12+16 = 40 = `size` 의 `data=40`. **모델과 실측이 일치한다.** 내 드라이버 몫 typeinfo 는 61 B, `ByteBus` 까지 78 B 다. `size` 의 `text` 는 `.text` + `.rodata` 를 합친 값이라 vtable 도 거기 들어 있다.

`.data` 실물 (`objdump -s -j .data`):

```text
 18248 38820000 00540040 00000000 50000000  8....T.@....P...
 18258 20820000 00300140 00000000 08820000   ....0.@........
 18268 00440040 00000000                    .D.@....
```

`g_uart` 는 `0x18264`: vptr `0x00008208` = `vtable for UartDrv`(0x8200) **+ 8**. vptr 은 vtable 시작이 아니라 **첫 함수 슬롯**(address point)을 가리킨다. 그리고 여기서 하나 더 얻는다: `.init_array` 가 없고 객체가 `.data` 에 들어갔다 — `constexpr` 생성자 덕에 vptr 까지 **상수 초기화**됐다 (CPP-B09 참고). vptr 이 있어도 동적 초기화가 필수는 아니다.

호스트 쪽 교차 확인: `vptr_of(a) == vptr_of(b)`(같은 클래스), `sizeof(UartDrv) == 16`(LP64). `vptr_of` 는 ABI 구현 세부에 기댄 **진단 코드**이고 표준이 보장하지 않는다.

### 전체 측정표 — 여기가 이 레슨의 본론

`measure.sh` 실제 출력 (`vtables`/`typeinfo_syms` 는 `nm -C -S` 합계, libstdc++ 내부 것 포함):

```text
nortti                 text=596     data=40    bss=8     vtables=3 (72B) typeinfo_syms=0  (0B)
rtti                   text=11744   data=1392  bss=384   vtables=6 (192B) typeinfo_syms=14 (194B)
nortti_gc              text=584     data=40    bss=4     vtables=3 (72B) typeinfo_syms=0  (0B)
rtti_gc                text=9060    data=1376  bss=380   vtables=5 (160B) typeinfo_syms=14 (194B)
nortti_final           text=580     data=40    bss=8     vtables=3 (72B) typeinfo_syms=0  (0B)
nortti_lto             text=528     data=40    bss=4     vtables=3 (72B) typeinfo_syms=0  (0B)
nortti_final_lto       text=528     data=40    bss=4     vtables=3 (72B) typeinfo_syms=0  (0B)
nortti_lto_gc          text=532     data=40    bss=4     vtables=3 (72B) typeinfo_syms=0  (0B)
vdtor                  text=5208    data=1400  bss=788   vtables=3 (96B) typeinfo_syms=0  (0B)
vdtor_gc               text=3192    data=1384  bss=788   vtables=3 (96B) typeinfo_syms=0  (0B)
vdtor_outline          text=5260    data=1400  bss=788   vtables=4 (128B) typeinfo_syms=0  (0B)
vdtor_outline_gc       text=3192    data=1384  bss=788   vtables=3 (96B) typeinfo_syms=0  (0B)
only_uart_lto          text=228     data=12    bss=4     vtables=1 (24B) typeinfo_syms=0  (0B)
vdtor_gc_stubdel       text=940     data=48    bss=412   vtables=3 (96B) typeinfo_syms=0  (0B)
vdtor_gc_nocxa         text=2932    data=1376  bss=380   vtables=3 (96B) typeinfo_syms=0  (0B)
vdtor_gc_both          text=672     data=40    bss=4     vtables=3 (96B) typeinfo_syms=0  (0B)
rtti_gc_stubdel        text=6812    data=40    bss=4     vtables=5 (160B) typeinfo_syms=14 (194B)
```

읽는 법 (전부 `nortti_gc` 584/40/4 기준):

| 바꾼 것 | text | data | bss | 해석 |
|---|---|---|---|---|
| `-frtti` | **+8476** | +1336 | +376 | 내 typeinfo 는 78 B 뿐. 나머지는 libstdc++ 체인 |
| `-frtti` + delete 스텁 | +6228 | 0 | 0 | malloc 은 끊겼지만 언와인더가 남음 |
| virtual 소멸자 | **+2608** | +1344 | +784 | 슬롯 증가는 24 B 뿐. 나머지는 체인 |
| + `operator delete` 스텁 | +356 | +8 | +408 | `__aeabi_atexit` 경로(`__atexit0` 400 B)가 남음 |
| + `-fno-use-cxa-atexit` 만 | +2348 | +1336 | +376 | atexit 은 끊겼지만 malloc 이 남음 |
| 둘 다 | **+88** | 0 | 0 | 소멸자 슬롯과 본체만 남음 |
| `final` (gc 없음 기준 596→580) | −16 | 0 | 0 | 소폭 |
| `-flto` (596→528) | −68 | 0 | −4 | |

핵심 관찰 셋:

1. **`-fno-rtti` 의 이득은 typeinfo 바이트가 아니다.** 내 타입의 typeinfo 는 78 B 인데 text 는 8.5 KB 줄었다. typeinfo 객체가 참조하는 `vtable for __cxxabiv1::__si_class_type_info` 가 libstdc++ 의 `class_type_info.o` 를 끌어온다. 그 오브젝트의 deleting dtor 가 `operator delete` → `free` → `_malloc_r` 를, 또 그 오브젝트가 참조하는 언와인더(`__aeabi_unwind_cpp_*`)가 `abort` → `raise` 와 stdio 를 끌어온다. 링크 맵이 사슬을 그대로 보여 준다 (`-Wl,-Map`, 일부):

```text
libstdc++.a(class_type_info.o)   /tmp/...o (vtable for __cxxabiv1::__class_type_info)
libstdc++.a(del_ops.o)           libstdc++.a(class_type_info.o) (operator delete...
libgcc.a(unwind-arm.o)           libstdc++.a(class_type_info.o) (__aeabi_un...
libc.a(libc_a-malloc.o)          libstdc++.a(del_op.o) (free)
libc.a(libc_a-abort.o)           libgcc.a(pr-support.o) (abort)
```

2. **virtual 소멸자의 슬롯 비용은 24 B 지만 링크 비용은 2.6 KB + RAM 2 KB 다.** (아래 Part C 그림.) `-fno-exceptions` 여도 일어난다.
3. **`final` 과 `-flto` 의 크기 이득은 수십 바이트다.** 이들의 진짜 효과는 크기가 아니라 호출 지점의 인라인이다 (다음 절).

`nortti_lto_gc`(532)가 `nortti_lto`(528)보다 4 B 큰 이유는 추적하지 않았다 (미확인). 함수별 섹션의 정렬 패딩으로 추정한다.

### 호출 지점별 — 간접이 직접으로 바뀌나

`bash measure.sh dis <빌드>`로 본 실제 결과를 정리하면:

```svg
<svg viewBox="0 0 740 262" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">호출 지점 × 빌드 옵션 (arm-none-eabi-g++ 14.2.1 -O2, 실측)</text>
  <g fill="currentColor" font-weight="bold">
    <text x="16" y="50">호출 지점</text>
    <text x="250" y="50">기본</text>
    <text x="370" y="50">final</text>
    <text x="490" y="50">-flto</text>
    <text x="610" y="50">final+lto</text>
  </g>
  <g stroke="currentColor">
    <rect x="240" y="60" width="110" height="30" class="f-warn"/><rect x="360" y="60" width="110" height="30" class="f-warn"/><rect x="480" y="60" width="110" height="30" class="f-warn"/><rect x="600" y="60" width="130" height="30" class="f-warn"/>
    <rect x="240" y="94" width="110" height="30" class="f-acc"/><rect x="360" y="94" width="110" height="30" class="f-ok"/><rect x="480" y="94" width="110" height="30" class="f-warn"/><rect x="600" y="94" width="130" height="30" class="f-ok"/>
    <rect x="240" y="128" width="110" height="30" class="f-ok"/><rect x="360" y="128" width="110" height="30" class="f-ok"/><rect x="480" y="128" width="110" height="30" class="f-ok"/><rect x="600" y="128" width="130" height="30" class="f-ok"/>
    <rect x="240" y="162" width="110" height="30" class="f-warn"/><rect x="360" y="162" width="110" height="30" class="f-warn"/><rect x="480" y="162" width="110" height="30" class="f-acc"/><rect x="600" y="162" width="130" height="30" class="f-acc"/>
    <rect x="240" y="196" width="110" height="30" class="f-warn"/><rect x="360" y="196" width="110" height="30" class="f-warn"/><rect x="480" y="196" width="110" height="30" class="f-warn"/><rect x="600" y="196" width="130" height="30" class="f-warn"/>
  </g>
  <g fill="currentColor" font-family="monospace" font-size="12">
    <text x="16" y="80">via_base(ByteBus&amp;)</text>
    <text x="16" y="114">via_ref(UartDrv&amp;)</text>
    <text x="16" y="148">via_object() g_uart</text>
    <text x="16" y="182">via_console()</text>
    <text x="16" y="216">fanout() 표 순회</text>
    <text x="252" y="80">간접</text><text x="372" y="80">간접</text><text x="492" y="80">간접</text><text x="612" y="80">간접</text>
    <text x="252" y="114">가드+인라인</text><text x="372" y="114">인라인</text><text x="492" y="114">간접</text><text x="612" y="114">인라인</text>
    <text x="252" y="148">인라인</text><text x="372" y="148">인라인</text><text x="492" y="148">인라인</text><text x="612" y="148">인라인</text>
    <text x="252" y="182">간접</text><text x="372" y="182">간접</text><text x="492" y="182">직접 b</text><text x="612" y="182">직접 b</text>
    <text x="252" y="216">간접</text><text x="372" y="216">간접</text><text x="492" y="216">간접</text><text x="612" y="216">간접</text>
  </g>
  <text x="10" y="248" fill="currentColor">가드 = 슬롯 값을 비교해 맞으면 인라인, 틀리면 간접. 직접 b = 인라인은 안 됐지만 tail call 이 직접 분기.</text>
</svg>
```

칸마다 실제 어셈블리 근거:

**`via_base` — 기본/final/LTO 전부 간접.** 정적 타입이 `ByteBus&` 이고 `noinline` 이니 구현을 고를 근거가 없다.

```text
00008000 <via_base>:
    8000:	ldr	r3, [r0, #0]
    8002:	ldr	r3, [r3, #8]
    8004:	bx	r3
```

`final` 은 **`UartDrv` 를 통한 호출**에만 작용하므로 여기선 무력하다. `-DONLY_UART -flto` 로 구현을 하나만 남겨도, 거기에 `-fwhole-program` 을 더해 손으로 빌드해도 `via_base` 는 그대로 간접이었다. "구현이 하나뿐이면 LTO 가 알아서 푼다"는 내 예상은 이 설정에서 **틀렸다**. (이유는 추적하지 않았다, 미확인.)

**`via_ref` (UartDrv&, 비-final) — 기본 빌드에서 가장 재밌다.** 파생 클래스가 있을지도 모르니 확정은 못 하는데, GCC 는 **추측 devirtualization** 을 넣었다:

```text
00008008 <via_ref>:
    8008:	ldr	r2, [r0, #0]              ; vptr
    800a:	ldr	r1, [pc, #20]             ; =0x80b9 (UartDrv::status | thumb)
    800c:	ldr	r2, [r2, #8]              ; 슬롯
    800e:	cmp	r2, r1
    8010:	bne.n	801c
    8012:	ldr	r0, [r0, #8]              ; 맞으면 인라인된 본체: hz_ 읽기
    ...
    801c:	bx	r2                        ; 틀리면 간접
```

`-fno-devirtualize-speculatively` 를 주면 이 가드가 사라지고 `ldr/ldr/bx` 만 남는 것을 직접 확인했다. GCC 문서에 따르면 이 옵션은 `-O2` 에서 켜진다. **`final` 을 붙이면** 가드 없이 인라인 5줄이 된다:

```text
00008008 <via_ref>:
    8008:	ldr	r0, [r0, #8]
    800a:	subs	r0, #0
    800c:	it	ne
    800e:	movne	r0, #1
    8010:	bx	lr
```

그런데 **`-flto` 만 켜면 오히려 `ldr/ldr/bx` 간접으로 돌아갔다** (`final` 없는 경우). LTO 에서 가드가 왜 사라졌는지는 추적하지 않았다 (미확인). "LTO 는 항상 더 잘 푼다"가 사실이 아니라는 증거로만 기록한다.

**`via_object` — 전부 인라인.** `g_uart.status()` 는 참조가 아니라 **객체 표현식**이고, 객체의 동적 타입은 곧 정적 타입이다. `final` 도 LTO 도 필요 없다.

**`via_console` — LTO 의 진짜 승리.** `console()` 이 다른 TU 에 있으니 기본 빌드는 반환값의 동적 타입을 모른다 (`bl console()` 후 간접). LTO 가 `console()` 을 인라인해 `&g_uart` 를 보면 풀린다:

```text
00008044 <via_console>:
    8044:	ldr	r0, [pc, #0]              ; =&g_uart
    8046:	b.n	8000 <UartDrv::status() const>
```

`final` 은 여기서 **도움이 안 됐다** — `console()` 의 반환 타입이 `ByteBus&` 이기 때문이다. 다만 직접 분기로 바뀌었을 뿐 **인라인까지 되지는 않았다.**

**`fanout` — 영원히 간접.** 표에 세 타입이 섞여 있고 인덱스가 런타임 값이다:

```text
    804e:	ldr.w	r0, [r4], #4
    8052:	ldr	r3, [r0, #0]
    8054:	ldr	r3, [r3, #8]
    8056:	blx	r3
```

이것이 "virtual 이 진짜로 필요한 곳"의 모양이고, 비용을 정직하게 치르는 곳이다.

**사이클 — 명령어 수준 추론이며 실측이 아니다.** 맥에는 DWT 사이클 카운터가 없고, QEMU 는 Cortex-M 파이프라인을 모델링하지 않는다. 간접 호출이 직접 호출보다 더 하는 일은 **`ldr` 2개**(vptr, 슬롯)다. 두 `ldr` 은 첫 결과가 둘째의 주소가 되는 연쇄라, Cortex-M4 TRM §3.3.3 의 "LDR 페어링 시 1사이클 절감" 조건(첫 LDR 의 목적지를 다음 주소 계산에 쓰지 않을 것)에 해당하지 않는다 ([TRM 발췌 재인용](https://www.cse.scu.edu/~dlewis/book3/docs/Cortex-M4%20Instruction%20Timing.pdf)). 슬롯 읽기는 플래시 접근이라 wait state 의 영향도 받는다. BL/BLX 의 정확한 사이클(TRM Table 3-1)은 원문을 확보하지 못해 **미확인**이다 — 수치를 면접에서 단정하지 말고, "`ldr` 두 번 + 분기, 그리고 인라인 불가" 로 말하라. 실제 값이 필요하면 보드에서 `DWT->CYCCNT` 로 재야 한다.

사이클보다 큰 것은 **인라인 불가**다. `status()` 는 인라인되면 `ldr`+조건 처리 몇 개로 끝나지만, 간접이면 호출·복귀 비용에 더해, 호출을 넘어 값을 유지하려고 callee-saved 레지스터를 써야 한다. 위 `fanout` 의 `push {r4,r5,r6,lr}` 이 그 흔적이다 (이건 비인라인 호출 공통의 비용이다).

### Part B — 손 vtable 과 가드

`make_ops<D>()` 는 C 드라이버의 `struct uart_ops` 그대로다. 컴파일러가 해 주는 일을 손으로 하는 셈이고, 차이는 **객체에 vptr 대신 `Dev{ops, self}` 16 B(LP64) 짝**을 들고 다닌다는 것이다. `p->D::write(s)` 한정 호출은 언어 규칙상 비가상이다.

`guarded_write` 는 위 `via_ref` 에서 GCC 가 만든 가드를 손으로 쓴 것이다. 호출 대상의 분포를 안다면(대부분 UART) 효과가 있다. 호출 분포를 모르면 비교 비용만 는다.

**생성/소멸 중 가상 호출.** `ProbeBase()` 안의 `kind()` 는 `'B'`, 완료 후엔 `'D'`, `~ProbeBase()` 안에선 다시 `'B'` 다 ([class.cdtor]). 구현 관점에선 **각 단계의 생성자가 자기 vtable 을 vptr 에 쓴다**. 이번 측정의 객체는 상수 초기화되어 그 저장 코드 자체가 없다 (그래서 `ByteBus` vtable 도 없다).

`ProbeBase()` 에서 순수 가상 함수를 호출하면 UB 다 ([class.abstract]). 다음 함정과 이어진다.

### Part C — virtual 소멸자가 끌고 오는 것

```svg
<svg viewBox="0 0 740 280" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">virtual ~ByteBus() 의 링크 사슬 (vdtor_gc 실측)</text>
  <rect x="20" y="40" width="210" height="44" class="f-acc" stroke="currentColor"/>
  <text x="32" y="58" fill="currentColor" font-family="monospace" font-size="12">vtable 슬롯 D0</text>
  <text x="32" y="76" fill="currentColor" font-family="monospace" font-size="12">UartDrv deleting dtor</text>
  <rect x="280" y="40" width="210" height="44" class="f-warn" stroke="currentColor"/>
  <text x="292" y="58" fill="currentColor" font-family="monospace" font-size="12">operator delete(void*,</text>
  <text x="292" y="76" fill="currentColor" font-family="monospace" font-size="12">unsigned int)</text>
  <rect x="530" y="40" width="200" height="44" class="f-warn" stroke="currentColor"/>
  <text x="542" y="58" fill="currentColor" font-family="monospace" font-size="12">free / _free_r</text>
  <text x="542" y="76" fill="currentColor" font-family="monospace" font-size="12">_malloc_r  0x58e B</text>
  <path d="M232 62 L278 62" stroke="currentColor" fill="none"/><path d="M492 62 L528 62" stroke="currentColor" fill="none"/>
  <rect x="20" y="120" width="210" height="44" class="f-acc" stroke="currentColor"/>
  <text x="32" y="138" fill="currentColor" font-family="monospace" font-size="12">전역 객체의 비자명 소멸자</text>
  <text x="32" y="156" fill="currentColor" font-family="monospace" font-size="12">_GLOBAL__sub_I_g_uart</text>
  <rect x="280" y="120" width="210" height="44" class="f-warn" stroke="currentColor"/>
  <text x="292" y="138" fill="currentColor" font-family="monospace" font-size="12">__aeabi_atexit</text>
  <text x="292" y="156" fill="currentColor" font-family="monospace" font-size="12">__register_exitproc</text>
  <rect x="530" y="120" width="200" height="44" class="f-warn" stroke="currentColor"/>
  <text x="542" y="138" fill="currentColor" font-family="monospace" font-size="12">__atexit0</text>
  <text x="542" y="156" fill="currentColor" font-family="monospace" font-size="12">.bss 0x190 B</text>
  <path d="M232 142 L278 142" stroke="currentColor" fill="none"/><path d="M492 142 L528 142" stroke="currentColor" fill="none"/>
  <text x="20" y="196" fill="currentColor">.data 는 __malloc_av_ 0x408 B, _impure_data 0x120 B 가 주범 (nm -S).</text>
  <text x="20" y="218" fill="currentColor">끊기 1: operator delete 를 무한루프로 대체 → malloc 사슬 소멸</text>
  <text x="20" y="240" fill="currentColor">끊기 2: -fno-use-cxa-atexit → __atexit0 소멸</text>
  <text x="20" y="262" fill="currentColor">가장 좋은 답: virtual 소멸자를 안 쓴다 (protected 비가상, 이 레슨의 ByteBus)</text>
</svg>
```

호출하지도 않는 `delete` 때문에 `malloc` 이 들어온다. vtable 의 D0 슬롯은 **누군가 `delete` 할 가능성에 대비해** 항상 채워지고, gc-sections 는 슬롯이 참조하는 함수를 살려 둔다. 게다가 전역 객체의 소멸자가 비자명해지니 `_GLOBAL__sub_I_g_uart` 가 `__aeabi_atexit` 를 3번 호출한다 — 돌아오지 않는 펌웨어에서 쓸모없는 일이다.

처음엔 **링크조차 실패**했다. `-nostartfiles` 맨몸 링크에서 `undefined reference to '__dso_handle'` 가 나서 `demo_stubs.cpp` 에 `__dso_handle`, `_init`, `_fini` 를 줬다 (실제 프로젝트는 startup 이 제공하거나, 이 에러로 문제를 처음 알게 된다). 위 "virtual 소멸자" 행은 그 스텁을 넣은 수치다.

**`protected` 비가상 소멸자**는 이 사슬을 언어 수준에서 끊는다: 기반 포인터로 `delete` 가 **컴파일되지 않고** (`Deletable<ByteBus>` 가 false), 드라이버는 자명 소멸이라 `atexit` 등록도 없다. `final` 드라이버를 기반 포인터로 소유하지 않는다는 펌웨어 현실과 맞다. 실제 에러 (`demo_errors.cpp`, Apple clang 21):

```text
demo_errors.cpp:20:31: warning: delete called on 'ByteBus' that is abstract but has non-virtual destructor [-Wdelete-abstract-non-virtual-dtor]
demo_errors.cpp:20:38: error: calling a protected destructor of class 'ByteBus'
demo_errors.cpp:8:5: note: declared protected here
```

나머지 블록 (`-DSHOW_ERROR=2..5`):

```text
demo_errors.cpp:22:14: error: base 'UartDrv' is marked 'final'
demo_errors.cpp:24:31: error: declaration of 'status' overrides a 'final' function
demo_errors.cpp:26:42: error: non-virtual member function marked 'override' hides virtual member function
demo_errors.cpp:5:22: note: hidden overloaded virtual function 'ByteBus::status' declared here: different qualifiers ('const' vs unqualified)
demo_errors.cpp:28:9: error: return type 'ByteBus' is an abstract class
```

**vtable 이 gc-sections 를 막는다.** `UartDrv::selftest` 는 가상, `UartDrv::debug_dump` 는 비가상이고 둘 다 아무도 부르지 않는다 (`measure.sh gc`):

```text
nortti         selftest(virtual)=1 debug_dump(non-virtual)=1
nortti_gc      selftest(virtual)=1 debug_dump(non-virtual)=0
nortti_lto     selftest(virtual)=1 debug_dump(non-virtual)=0
nortti_lto_gc  selftest(virtual)=1 debug_dump(non-virtual)=0
```

비가상은 gc 되고 가상은 살아남는다. vtable 이 함수 주소를 들고 있으니 링커 입장에선 "참조됨"이다. 쓸모없는 가상 함수 하나마다 **그 함수가 호출하는 코드까지** 플래시에 남는다. GCC LTO 도 못 지웠다. clang 은 전용 기능이 있다 (`measure.sh vfe`, 실측, clang 21 + ld.lld, `-flto -fwhole-program-vtables -fvirtual-function-elimination -fvisibility=hidden -Wl,--lto-whole-program-visibility`):

```text
c0:    1390	     40	      4	 selftest=3
c1:    1362	     40	      4	 selftest=0
```

3개 드라이버의 `selftest` 가 전부 사라졌다. 전제 조건이 많다는 점(전체 프로그램이 LTO, vtable 의 가시성 제한)을 기억하라.

**순수 가상 슬롯 — 툴체인마다 다르다.** `ByteBus` 소멸자를 비인라인으로 정의하면(`-DOUTLINE_DTOR`) 그 TU 에 기반 vtable 이 방출된다 (`measure.sh pure`):

```text
gcc  :          w __cxa_pure_virtual
clang:          U __cxa_pure_virtual
 93f8 00000000 00000000 00000000 00000000  ................
 9408 00000000 00000000 00000000 00000000  ................
```

이 빌드의 GCC 는 **약한 참조**(`w`)라 정의가 없어도 링크되고 슬롯이 **0** 이 된다(위 덤프). 호출되면 0번지 분기다 — ARMv7-M 에선 대상 주소 bit0 이 0 인 BLX 는 INVSTATE UsageFault 가 정의된 동작이지만, 이 경로를 보드에서 실행해 보지는 않았다. clang 은 강한 참조(`U`)여서 `__cxa_pure_virtual` 을 정의하지 않으면 `ld.lld: error: undefined symbol: __cxa_pure_virtual` 로 링크가 실패한다 (실측). `extern "C" void __cxa_pure_virtual() { for (;;) {} }` 를 주면 해결된다.

`__cxa_pure_virtual` 이 terminate 체인을 끌고 온다는 이야기를 흔히 하는데, 위 GCC 빌드에서는 약한 참조라 **끌려오지 않았다** (링크된 ELF 의 `nm` 에 `__cxa_pure_virtual`, `terminate` 없음). 툴체인과 libstdc++ 빌드 설정에 달린 문제이니 자기 ELF 로 확인하라.

### 언제 무엇을 쓰나

| 상황 | 선택 | 이유 |
|---|---|---|
| 컴파일 시점에 타입이 하나로 정해짐 | 템플릿/CRTP (CPP-M05) | vtable·vptr 없음 |
| 닫힌 집합, 런타임 선택 | `enum` + `switch` (연습 3) | vptr 대신 태그, 분기가 인라인 가능 |
| 열린 집합, 런타임 선택, 객체 몇 개 | `virtual` + `final` + protected 소멸자 | 표가 RAM·플래시에 고정, 추가 코드는 슬롯뿐 |
| 객체가 수천 개 (RAM 민감) | 클래스당 ops 포인터 1개 (연습 4) | vptr 를 인스턴스 수만큼 곱하지 않음 |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti solution.cpp -o solution && ./solution   # 82/82 passed
c++ -std=c++20 -g -O1 -fno-exceptions -fno-rtti -fsanitize=address,undefined -fno-omit-frame-pointer solution.cpp -o sa && ./sa   # 82/82, 리포트 없음
```

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `virtual ~ByteBus() = default;` 를 습관적으로 | D0 슬롯 → `operator delete` → malloc. 이 측정에서 text +2608, RAM +2 KB | 힙으로 소유하지 않으면 `protected: ~ByteBus() = default;` |
| "`final` 붙였으니 가상 호출 없음" | `final` 은 **그 타입을 통한** 호출만 푼다. `via_base`(`ByteBus&`)는 그대로 간접 | 호출부가 구체 타입을 보게 하거나 템플릿 인자로 |
| "`-flto` 면 다 풀린다" | `fanout` 은 절대 안 풀리고 `via_base` 도 그대로. `via_ref` 는 이 측정에서 LTO 로 오히려 간접이 됐다 | 어셈블리로 확인. 필요하면 `final` 도 |
| RTTI 비용을 "typeinfo 크기"로 계산 | libstdc++ 체인 포함 +8.5 KB | `-fno-rtti`, `nm` 으로 체인 확인 |
| 안 부르는 가상 함수는 gc 된다고 가정 | vtable 이 주소를 참조 → 살아남음 | 비가상으로 빼거나 빌드에서 제외 |
| 생성자에서 `init()` 가상 호출 | 동적 타입이 아직 기반이라 기반 구현으로 간다 | 2단계 초기화 또는 팩토리 (생성 후 호출) |
| `-Wl,--gc-sections` 없이 `-ffunction-sections` 만 | 섹션만 쪼개지고 제거는 안 된다. 둘은 짝이다 | 둘 다 지정 |
| vptr 을 `memcpy` 로 복사/초기화 | 다형 타입은 trivially copyable 이 아니다. `memcpy` 로 객체를 만들면 UB ([basic.life]) | 생성자 호출, placement new (CPP-M07 참고) |

## 8. 면접 Q&A

<details>
<summary>Q1. vtable 과 vptr 은 각각 어디에 있고 얼마나 먹나요?</summary>

vptr 은 **객체마다** 포인터 하나로 RAM 에 있다(객체가 `.data`/스택에 있으면 거기). vtable 은 **클래스당 1벌**이고 이번 빌드에선 `.rodata`, 즉 플래시다. 이번 측정: 가상 4개면 vtable 24 B, 객체당 vptr 4 B. 꼬리질문 "vptr 이 vtable 시작을 가리키나?" → 아니다, address point(첫 함수 슬롯)를 가리킨다. 실측 `0x8208 = 0x8200 + 8`.

</details>

<details>
<summary>Q2. 가상 호출이 직접 호출보다 얼마나 느린가요?</summary>

명령어로는 `ldr` 2개 더 (vptr, 슬롯) + 레지스터 간접 분기다. 사이클 수는 메모리에 달렸다 — 슬롯 읽기는 플래시 접근이라 wait state 를 탄다. 직접 측정은 보드의 `DWT->CYCCNT` 로 해야 한다. 그러나 진짜 비용은 **인라인을 못 한다**는 것이다: `status()` 는 인라인되면 `ldr` 하나에 조건 처리인데, 간접이면 호출 규약 비용이 전부 붙는다.

</details>

<details>
<summary>Q3. final 은 정확히 언제 도움이 되나요?</summary>

호출의 정적 타입이 `final` 클래스(또는 `final` 함수)일 때다. 이번 측정에서 `via_ref(UartDrv&)` 가 가드 없는 인라인으로 바뀌었다. `via_base(ByteBus&)` 와 `console()` 반환값(`ByteBus&`)에는 효과 없음. 꼬리질문 "그럼 객체로 부르면?" → `g_uart.status()` 는 `final` 없이도 인라인된다. 객체의 동적 타입은 정적 타입이니까.

</details>

<details>
<summary>Q4. LTO 가 devirtualization 을 어떻게 돕나요? 한계는?</summary>

TU 경계를 없애 **객체의 정체가 드러나게** 한다. `console()` 을 인라인해 `&g_uart` 를 보면 `b UartDrv::status` 로 풀린다. 한계: 인덱스가 런타임 값인 표(`fanout`)는 불가능하고, 이번 측정에선 구현이 하나뿐이어도 `via_base` 는 간접이었다. 또 인라인 없이 직접 분기까지만 된 경우도 있다. 결과를 가정하지 말고 어셈블리를 보라.

</details>

<details>
<summary>Q5. -fno-rtti 로 얼마나 줄어드나요? typeinfo 크기만큼인가요?</summary>

아니다. 이번 측정에서 내 타입들의 typeinfo 는 78 B 인데 text 는 8.5 KB, RAM 1.7 KB 가 줄었다. typeinfo 가 `__si_class_type_info` 의 vtable 을 참조하면서 libstdc++, `operator delete`, 언와인더, `abort` 가 줄줄이 링크되기 때문이다. `-fno-exceptions` 만으론 막히지 않는다. 숫자는 라이브러리 빌드에 따라 달라지므로 자기 `-Wl,-Map` 으로 확인한다.

</details>

<details>
<summary>Q6. 임베디드 인터페이스의 소멸자는 어떻게 설계해야 하나요?</summary>

힙으로 소유하지 않는다면 `protected` 비가상이다. 기반 포인터로 `delete` 가 컴파일 에러가 되므로 "virtual 소멸자가 없어서 UB" 문제가 사라지고, D0 슬롯과 `operator delete` 의존도 없어진다. 드라이버는 자명 소멸이라 전역 객체의 `atexit` 등록도 없다. 꼬리질문 "`final` 없이 파생되면?" → 파생 클래스 소멸자도 자동 생성되어 잘 동작한다. 금지되는 건 기반 포인터로의 `delete` 뿐이다.

</details>

<details>
<summary>Q7. --gc-sections 를 켰는데 안 쓰는 가상 함수가 남아 있습니다. 왜죠?</summary>

vtable 이 그 함수의 주소를 들고 있고, vtable 은 생성자(또는 상수 초기화된 객체의 vptr)가 참조한다. 링커는 "주소가 참조됨"까지만 안다. 이번 측정에서 `selftest` 는 남고 비가상 `debug_dump` 는 지워졌다. 객체가 전혀 없어 vtable 이 참조되지 않을 때만 통째로 사라진다 (`vdtor_outline` 의 `ByteBus` vtable 이 gc 빌드에서 사라진 것이 그 예). clang 의 `-fvirtual-function-elimination` 은 이를 푸는데, 전체 프로그램 LTO 와 vtable 가시성 제한이 필요하다 (실측으로 `selftest` 3개 제거 확인).

</details>

<details>
<summary>Q8. 생성자에서 가상 함수를 호출하면요?</summary>

그 시점의 동적 타입은 지금 실행 중인 생성자의 클래스이므로 그 클래스 버전이 불린다 ([class.cdtor]). 파생 오버라이드는 불리지 않는다. 구현상 각 생성자가 자기 vtable 을 vptr 에 쓰기 때문이다. 순수 가상을 호출하면 UB. 펌웨어 관점의 함정: "베이스 생성자에서 `init()` 을 부르게 하자"는 설계가 조용히 잘못된 구현을 호출한다.

</details>

## 9. 추가 연습

### 연습 1 — 다중 상속의 vptr 개수

`TxIf`, `RxIf` 를 모두 구현한 `Combo` 의 크기를 모델링하라.

<details>
<summary>답 보기</summary>

기반마다 부분 객체가 있어 vptr 이 **기반 수만큼** 생긴다. LP64 에서 2×8 + 4 + 패딩 4 = 24. `RxIf&` 로 부르면 `this` 를 보정하는 thunk 를 거친다 (`&t != &r` 로 확인).

</details>

### 연습 2 — GCC 의 가드를 그대로

`d->status == uart_status` 비교로 빠른 경로를 만들어라 (위 `via_ref` 실측과 같은 형태).

<details>
<summary>답 보기</summary>

슬롯(함수 포인터)을 비교하고 맞으면 인라인된 본체를 실행한다. 빠른 경로는 간접 분기를 피하지만 비교와 슬롯 읽기는 여전히 든다. 분포가 치우쳐 있을 때만 이득이다.

</details>

### 연습 3 — 닫힌 집합은 switch

`enum class Kind` + `switch` 로 `status` 를 구현하라.

<details>
<summary>답 보기</summary>

vptr 도 vtable 도 없고 `constexpr` 도 된다. 대신 **타입을 추가하면 모든 switch 를 고쳐야** 한다 (닫힌 집합). `sizeof(Tagged) == 8`, `is_polymorphic_v` 는 false.

</details>

### 연습 4 — vptr vs 클래스당 포인터

같은 타입 객체 N 개의 vptr RAM 과, 클래스당 정적 ops 포인터 1개의 RAM 을 비교하고 예산 판정을 구현하라.

<details>
<summary>답 보기</summary>

`ram_saved(ptr, N) = ptr*N - ptr`. N=1 이면 0, N=8(ILP32)이면 28 B. 객체가 많을 때만 의미가 있다. 드라이버 3개짜리 보드에선 고민할 가치가 없다 — vptr 12 B 다.

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti practice.cpp -o practice && ./practice   # 23/23 passed
```

## 10. 핵심 정리

- RAM = 객체당 vptr, 플래시 = 클래스당 vtable (`(2 + dtor슬롯 + 가상함수) × ptr`) + RTTI 시 typeinfo. 이번 ELF 와 바이트 단위로 일치.
- 가장 큰 비용은 **링크 사슬**: `-frtti` +8.5 KB, virtual 소멸자 +2.6 KB·RAM 2 KB. 슬롯 자체는 수십 바이트.
- 간접 호출은 `ldr`×2 + 간접 분기 + **인라인 불가**. 정확한 사이클은 DWT 로 잰다 (여기서는 명령어 수준 추론만).
- `final` 은 그 타입을 통한 호출만, 객체 표현식은 항상 직접, LTO 는 TU 를 넘어 객체를 볼 때만. 런타임 표(`fanout`)는 영원히 간접.
- GCC `-O2` 는 비-`final` 참조에 가드+인라인을 넣는다 (speculative). LTO 가 항상 더 낫지는 않았다 — 어셈블리로 확인.
- vtable 은 호출 안 되는 가상 함수를 gc 에서 지킨다. clang VFE 만 예외.
- 인터페이스 소멸자는 `protected` 비가상, 구현은 `final`.
- `__cxa_pure_virtual` 은 GCC 빌드에서 약한 참조(슬롯 0), clang 은 강한 참조 — 툴체인마다 다르다.

## 참고자료

- [Itanium C++ ABI — Virtual Table Layout, RTTI](https://itanium-cxx-abi.github.io/cxx-abi/abi.html#vtable) · [ARM C++ ABI (IHI 0041)](https://github.com/ARM-software/abi-aa/blob/main/cppabi32/cppabi32.rst)
- C++20 표준: [class.virtual], [class.abstract], [class.cdtor], [class.dtor], [basic.life] · [cppreference — final](https://en.cppreference.com/w/cpp/language/final) · [override](https://en.cppreference.com/w/cpp/language/override)
- [GCC — Optimize Options (-fdevirtualize, -fdevirtualize-speculatively)](https://gcc.gnu.org/onlinedocs/gcc/Optimize-Options.html) · [GCC — -fuse-cxa-atexit](https://gcc.gnu.org/onlinedocs/gcc/C_002b_002b-Dialect-Options.html)
- [Clang — Command line reference (-fvirtual-function-elimination, -fwhole-program-vtables)](https://clang.llvm.org/docs/ClangCommandLineReference.html) · [LTO visibility](https://clang.llvm.org/docs/LTOVisibility.html)
- [Cortex-M4 TRM (DDI 0439) §3.3 — 발췌, Dan Lewis](https://www.cse.scu.edu/~dlewis/book3/docs/Cortex-M4%20Instruction%20Timing.pdf) (LDR 페어링. BL/BLX 사이클은 원문 미확인)
- ARMv7-M Architecture Reference Manual (DDI 0403) — BLX, INVSTATE UsageFault
