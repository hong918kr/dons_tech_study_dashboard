---
id: CPP-A04
time: 110분
updated: 2026-11-08
---
# CPP-A04 std::atomic과 Cortex-M: lock-free 여부와 ISR 공유

## 학습 목표

- 같은 `std::atomic<uint32_t>` / `std::atomic<uint64_t>` 의 load · store · `fetch_add` · `compare_exchange` 가 Cortex-M0+ (ARMv6-M), M3 (ARMv7-M), M4 (ARMv7E-M), 호스트 arm64 에서 **실제로 어떤 명령이 되는지** 말할 수 있다: 인라인 `LDREX/STREX` 루프 vs `bl __atomic_fetch_add_4/_8` 라이브러리 호출 vs `ldr/str` + `dmb` vs `ldaddal/casal` 한 개.
- `is_always_lock_free`, `is_lock_free()`, `ATOMIC_INT_LOCK_FREE` (0/1/2) 의 차이를 알고, 타깃별 값을 **컴파일러에게 직접 물어서** (`static_assert` 를 일부러 실패시키는 probe, `-dM -E`) 확인한다.
- ISR ↔ main 공유 변수 정책 `isr_shared<T>` 를 만든다: lock-free 가 아니면 `static_assert` 로 빌드를 멈추거나, 명시적으로 허용한 경우에만 PRIMASK 임계 구역으로 바꾼다. 멀티코어에서는 그 폴백이 틀렸다는 것까지 타입으로 막는다.
- `volatile` 이 ISR 공유에 주지 **않는** 세 가지(찢어짐 방지, RMW 원자성, 순서)를 실측 asm 과 결정적 테스트로 보인다.
- M0+ 에서 `__atomic_*` 를 **누가 구현하는지** 답한다: arm-none-eabi 의 libgcc·newlib 에는 없다(링크 에러 실측) → 보통 펌웨어 쪽에서 IRQ 차단으로 직접 쓴다.

## 1. 문제

"`std::atomic<uint64_t>` 로 바꿨더니 M0+ 보드에서 `undefined reference to __atomic_fetch_add_8` 가 나요. M4 에서는 빌드는 되는데 ISR 안에서 함수 호출이 보이고요. 왜죠? 어떻게 막죠?" — C++ 펌웨어 면접에서 C-M14 (ISR 공유 데이터)와 C-A03 (C11 atomics) 다음으로 나오는 질문이다. "atomic 이면 원자적이다" 는 반만 맞다. **원자적(atomic)** 은 언어가 약속하지만, **lock-free (인터럽트를 끄거나 락을 잡지 않고 명령으로 처리)** 인지는 코어가 정한다. ISR 에서 쓰려면 lock-free 여야 하고(아니면 교착이나 숨은 인터럽트 차단), 그건 빌드할 때 확인할 수 있다.

이미 배운 것은 다시 하지 않는다: C11 `stdatomic` 와 memory_order 고르기(C-A03), Cortex-M4 의 C11 asm (C-A03 `demo_asm.c`), 64비트 tearing 과 PRIMASK 저장/복원(C-M14), `CriticalSection` RAII 가드(CPP-B06), 컴파일러 배리어와 DMA 순서(C-A02). 이 레슨은 그 위에서 **C++ `std::atomic` 하나를 네 타깃으로 내려 보고**, 그 결과를 **타입 정책** 으로 굳힌다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">같은 std::atomic, 코어마다 다른 명령 (Apple clang 21, -O2, seq_cst, 실측)</text>
  <g fill="currentColor">
    <text x="170" y="52">load/store 32</text>
    <text x="310" y="52">RMW 32 (fetch_add, CAS)</text>
    <text x="470" y="52">load/store 64</text>
    <text x="610" y="52">RMW 64</text>
    <text x="10" y="86">Cortex-M0+</text><text x="10" y="102" font-size="12">ARMv6-M</text>
    <text x="10" y="141">Cortex-M3 / M4</text><text x="10" y="157" font-size="12">ARMv7-M / 7E-M</text>
    <text x="10" y="196">호스트 arm64</text><text x="10" y="212" font-size="12">Apple M6 (LSE)</text>
  </g>
  <rect x="160" y="68" width="140" height="42" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="168" y="94" fill="currentColor" font-family="monospace" font-size="12">bl __atomic_load_4</text>
  <rect x="305" y="68" width="140" height="42" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="311" y="94" fill="currentColor" font-family="monospace" font-size="12">bl __atomic_..._4</text>
  <rect x="450" y="68" width="140" height="42" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="456" y="94" fill="currentColor" font-family="monospace" font-size="12">bl __atomic_load_8</text>
  <rect x="595" y="68" width="140" height="42" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="601" y="94" fill="currentColor" font-family="monospace" font-size="12">bl __atomic_..._8</text>
  <rect x="160" y="123" width="140" height="42" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="168" y="149" fill="currentColor" font-family="monospace" font-size="12">ldr/str + dmb sy</text>
  <rect x="305" y="123" width="140" height="42" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="311" y="149" fill="currentColor" font-family="monospace" font-size="12">ldrex/strex 루프</text>
  <rect x="450" y="123" width="140" height="42" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="456" y="149" fill="currentColor" font-family="monospace" font-size="12">bl __atomic_load_8</text>
  <rect x="595" y="123" width="140" height="42" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="601" y="149" fill="currentColor" font-family="monospace" font-size="12">bl __atomic_..._8</text>
  <rect x="160" y="178" width="140" height="42" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="168" y="204" fill="currentColor" font-family="monospace" font-size="12">ldar / stlr</text>
  <rect x="305" y="178" width="140" height="42" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="311" y="204" fill="currentColor" font-family="monospace" font-size="12">ldaddal / casal</text>
  <rect x="450" y="178" width="140" height="42" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="456" y="204" fill="currentColor" font-family="monospace" font-size="12">ldar x / stlr x</text>
  <rect x="595" y="178" width="140" height="42" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="601" y="204" fill="currentColor" font-family="monospace" font-size="12">ldaddal x / casal x</text>
  <g fill="currentColor" font-size="12">
    <rect x="10" y="236" width="14" height="14" class="f-warn" stroke="currentColor"/><text x="30" y="248">lock-free 아님: 라이브러리 호출 (누가 구현?)</text>
    <rect x="320" y="236" width="14" height="14" class="f-acc" stroke="currentColor"/><text x="340" y="248">LL/SC 루프</text>
    <rect x="430" y="236" width="14" height="14" class="f-mut" stroke="currentColor"/><text x="450" y="248">일반 접근+배리어</text>
    <rect x="590" y="236" width="14" height="14" class="f-ok" stroke="currentColor"/><text x="610" y="248">명령 1개</text>
  </g>
</svg>
```

빨간 칸(라이브러리 호출)이 이 레슨의 주인공이다. 그 칸의 변수를 ISR 과 공유하면 ① 링크가 안 되거나 ② 누군가 구현한 함수가 몰래 인터럽트를 끄거나 ③ 락 기반 구현이면 ISR 이 그 락을 기다리며 영원히 돈다. 그래서 **빌드할 때** 걸러야 한다.

### Part A — 실측 표를 코드로, 그리고 lock-free 정책

- `max_lock_free_bytes(Core)` 와 `lowering(Core, Op, bytes)` — 5. 풀이의 실측 asm 을 `constexpr` 표로 옮긴다 (M0+ = 0바이트, M3/M4 = 4, 호스트 = 16).
- `policy_ok<T, Target, Policy>()` — T 가 부호 없는 정수이고, 타깃에서 lock-free 이거나 (폴백 허용 **그리고** 단일 코어) 일 때만 true. `isr_shared<T, Target, Policy>` 가 이걸 `static_assert` 로 건다.
- lock-free 경로 (`std::atomic<T>`) 의 `fetch_max` 를 CAS 루프로. 호스트 4 스레드 × 10만 번 스트레스로 정확한 합과 최대값.

### Part B — volatile 반례와 PRIMASK 폴백

- 주어진 반례: `volatile` 카운터의 `t = c; c = t + 1;` 사이 ISR → 증가 1개 유실, 64비트 값의 워드 2개 읽기 사이 캐리 → `0x1_FFFFFFFF` (존재한 적 없는 값). 둘 다 **항상** 재현되는 결정적 테스트다 (끼어드는 지점을 `isr_window()` 로 지정).
- `isr_shared` 의 폴백 경로 6개 (`load/store/fetch_add/exchange/compare_exchange/fetch_max`) 를 `CriticalSection` 안에서 구현한다. ISR 은 pending 으로 밀렸다가 복원 순간 실행돼야 하고, 이미 마스크된 상태에서 불리면 마스크를 유지해야 한다.

### Part C — UART RX 텔레메트리 (hot ISR)

- `uart_rx_isr(t, len, lat)` — 프레임 수(32비트, M3 에선 LDREX/STREX), 바이트 수(64비트, M3 에선 폴백), 최악 지연 워터마크.
- `snapshot_and_clear(t)` — main 이 1초마다 세 값을 가져가고 0 으로. 주어진 `snapshot_naive` 는 변수마다 원자적이지만 **세 개 묶음은 원자적이 아니라서** "프레임 1개에 128바이트" 같은 스냅샷이 나온다. 묶음을 원자적으로 만들어라.

## 2. Problem (English)

> **ISR-shared variables with `std::atomic` on Cortex-M: know when it is lock-free, and enforce it.**
>
> 1. Compile `std::atomic<uint32_t>` and `std::atomic<uint64_t>` load/store/`fetch_add`/`compare_exchange_strong` for `thumbv6m-none-eabi -mcpu=cortex-m0plus`, `thumbv7m-none-eabi -mcpu=cortex-m3`, `thumbv7em-none-eabihf -mcpu=cortex-m4` and the arm64 host. Encode what you observe as `constexpr unsigned max_lock_free_bytes(Core)` and `constexpr Lowering lowering(Core, Op, unsigned bytes)` where `Lowering ∈ {Libcall, PlainBarrier, LlscLoop, SingleInsn}`.
> 2. Write `template<class T, class Target = NativeTarget, class Policy = RequireLockFree> class isr_shared` with `load, store, fetch_add, exchange, compare_exchange (strong), fetch_max`. If `T` is always lock-free on `Target`, wrap `std::atomic<T>`. Otherwise fail the build with a readable `static_assert`, unless `Policy` is `AllowIrqFallback` **and** the target is single-core, in which case every operation runs inside a save/restore PRIMASK critical section. Only unsigned integral `T`.
> 3. UART RX telemetry: the ISR updates a 32-bit frame count, a 64-bit byte count and a latency watermark; `Snapshot snapshot_and_clear(UartTelemetry&)` must return a consistent triple (`frames * 64 == bytes` when every frame is 64 bytes) even if the RX interrupt fires in the middle.
>
> Constraints: `-std=c++20 -fno-exceptions -fno-rtti`, no heap. The interrupt is simulated: `isr_window()` marks where it may fire; a masked interrupt stays pending until PRIMASK is restored.
>
> Follow-ups: Why is `volatile` not enough? What does `ATOMIC_INT_LOCK_FREE == 1` mean? Who provides `__atomic_fetch_add_4` on a Cortex-M0+? Is an IRQ-masking `__atomic_*` usable from an ISR? From the second core of an RP2040? Is `std::atomic_flag` really lock-free on M0+?

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -pthread starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력, PASS 줄 생략):

```text
FAIL starter.cpp:268  max_lock_free_bytes(Core::M0plus) == 0 && max_lock_free_bytes(Core::M3) == 4
FAIL starter.cpp:269  max_lock_free_bytes(Core::M4) == 4 && max_lock_free_bytes(Core::HostArm64) == 16
FAIL starter.cpp:272  lowering(Core::M3, Op::Load, 4) == Lowering::PlainBarrier
FAIL starter.cpp:273  lowering(Core::M3, Op::Store, 4) == Lowering::PlainBarrier
FAIL starter.cpp:274  lowering(Core::M3, Op::FetchAdd, 4) == Lowering::LlscLoop
FAIL starter.cpp:275  lowering(Core::M4, Op::Cas, 4) == Lowering::LlscLoop && lowering(Core::M3, Op::FetchAdd, 2) == Lowering::LlscLoop
FAIL starter.cpp:278  lowering(Core::HostArm64, Op::FetchAdd, 8) == Lowering::SingleInsn
FAIL starter.cpp:279  lowering(Core::HostArm64, Op::Cas, 4) == Lowering::SingleInsn
FAIL starter.cpp:286  (!policy_ok<uint64_t, M3, RequireLockFree>())
FAIL starter.cpp:288  (!policy_ok<uint32_t, M0, RequireLockFree>())
FAIL starter.cpp:290  (!policy_ok<uint32_t, RP2040, AllowIrqFallback>())
FAIL starter.cpp:293  (isr_shared<uint32_t, M3, AllowIrqFallback>::uses_atomic)
FAIL starter.cpp:305  mx.fetch_max(7) == 0 && mx.fetch_max(4) == 7 && mx.load() == 7
FAIL starter.cpp:314  mx.load() == 99999u * 4u + 3u
FAIL starter.cpp:332  g_ts64.load() == 0xFFFFFFFFull
FAIL starter.cpp:333  g_isr_runs == 1 && g_ts64.load() == 0x100000000ull && !irq_masked()
FAIL starter.cpp:336  g_ts64.fetch_add(0x10) == 0x100000000ull
FAIL starter.cpp:337  g_ts64.load() == 0x100000011ull && g_isr_runs == 1
FAIL starter.cpp:341  g_isr_seen == 0x2'0000'0005ull
FAIL starter.cpp:344  g_ts64.exchange(0) == 0x2'0000'0005ull && g_ts64.load() == 1u
FAIL starter.cpp:348  !g_ts64.compare_exchange(e, 50) && e == 1u
FAIL starter.cpp:350  g_ts64.compare_exchange(e, 50) && g_ts64.load() == 51u
FAIL starter.cpp:351  g_ts64.fetch_max(40) == 51u && g_ts64.fetch_max(0x1'0000'0000ull) == 51u
FAIL starter.cpp:352  g_ts64.load() == 0x1'0000'0000ull && !irq_masked()
FAIL starter.cpp:360  g_isr_runs == 1 && g_ts64.load() == 0x1'0000'0002ull
FAIL starter.cpp:364  g_m0_cnt.fetch_add(5) == 10u && g_m0_cnt.load() == 16u
FAIL starter.cpp:367  g_m3_cnt.fetch_add(5) == 0u && g_m3_cnt.load() == 5u && g_armed_isr != nullptr
FAIL starter.cpp:375  s.frames == 3u && s.bytes == 192u && s.max_lat_us == 140u
FAIL starter.cpp:383  n.frames == 1u && n.bytes == 128u
FAIL starter.cpp:393  s1.frames == 2u && s1.bytes == 128u && s1.max_lat_us == 70u
FAIL starter.cpp:396  s2.frames == 1u && s2.bytes == 64u && s2.max_lat_us == 300u
FAIL starter.cpp:414  rel_ok && frames == 1000u + 143u && bytes == frames * 64u
22/54 passed
```

하네스 설계: 시작 코드의 `policy_ok` 는 일부러 `true` 를 돌려준다 — `false` 면 `isr_shared` 안의 `static_assert` 가 빌드를 멈춰서 FAIL 목록조차 볼 수 없기 때문이다. 스레드 스트레스는 lock-free 경로만 쓰고, 스텁은 루프가 없어서 멈추지 않는다. volatile 반례 2개는 처음부터 PASS 다 (그 반례가 "항상 깨진다" 는 것을 확인하는 CHECK 다).

관찰용 파일 (하네스와 별개, 결과는 5. 풀이에 실제 출력으로):

```bash
bash asm_targets.sh asm      # demo_targets.cpp → M0+/M3/M4/arm64 asm (지시어 제거)
bash asm_targets.sh probe    # demo_probe.cpp → is_always_lock_free, ATOMIC_*_LOCK_FREE, -dM -E
bash asm_targets.sh policy   # demo_policy_targets.cpp + isr_shared.hpp → 타깃별 static_assert, M3 asm
# Lima VM elinux (arm-none-eabi-g++ 14.2): 링크 실험 — 누가 __atomic_* 를 구현하나
limactl shell elinux -- bash -lc 'mkdir -p /tmp/a04 && cp <lesson dir>/{app_m0.cpp,demo_libatomic_m0.c,link_m0.sh} /tmp/a04 && cd /tmp/a04 && bash link_m0.sh'
```

맥 주의: Apple clang 은 `--target=thumbv6m-none-eabi` 코드를 만들지만 bare-metal 용 C++ 헤더가 없다 (`fatal error: 'atomic' file not found` — 실측). 맥 SDK 의 libc++ 헤더를 억지로 넣으면 `"No thread API"` 등으로 깨진다 (실측). 그래서 `asm_targets.sh` 는 Lima VM `elinux` 의 arm-none-eabi **libstdc++ 14.2 + newlib 헤더**를 `/tmp/arm-none-eabi-include` 로 한 번 복사해 쓴다. 즉 맥 clang 이 GCC 의 `<atomic>` 을 컴파일한다 — libstdc++ 의 `fetch_add` 는 `__atomic_fetch_add(&_M_i, __i, int(__m))` 내장 함수 한 줄이라(`bits/atomic_base.h` 631행) 명령 선택은 **컴파일러 백엔드** 가 한다.

## 4. 힌트

<details>
<summary>힌트 1 — 표는 "크기 → 코어 → 연산" 순서로 거른다</summary>

먼저 크기가 lock-free 한도를 넘거나 2의 거듭제곱이 아니면 무조건 라이브러리 호출이다. 그다음 코어: 호스트는 전부 명령 하나. M3/M4 는 load/store 와 RMW 가 다르다 — 정렬된 32비트 이하 `LDR/STR` 은 그 자체로 원자적이라 배리어만 붙이면 되지만, RMW 는 읽기와 쓰기 사이를 지켜야 하므로 LDREX/STREX.

</details>

<details>
<summary>힌트 2 — 정책은 두 질문의 AND/OR</summary>

"타깃에서 lock-free 인가?" 가 참이면 끝. 거짓이면 "폴백을 허용했나?" 와 "단일 코어인가?" 가 둘 다 참이어야 한다. PRIMASK 는 **자기 코어** 의 인터럽트만 막는다. 그리고 부호 있는 T 는 폴백의 `old + d` 가 오버플로 UB 가 되므로 거부한다 (`std::atomic<int>::fetch_add` 는 C++20 에서 2의 보수로 감싸지만, 일반 `int` 덧셈은 아니다).

</details>

<details>
<summary>힌트 3 — 폴백은 "CriticalSection 하나 + 읽기 + 쓰기"</summary>

`load_words` / `store_words` 가 32비트 코어의 워드 단위 접근과 그 사이의 틈(`isr_window()`)을 이미 모델링한다. 폴백 함수는 `CriticalSection cs;` 를 먼저 만들고 그 둘을 부르면 된다. ISR 은 틈에서 pending 이 되고, `cs` 소멸자가 PRIMASK 를 **원래 값으로** 되돌리는 순간 실행된다.

</details>

<details>
<summary>힌트 4 — 원자 변수 세 개 ≠ 원자 묶음</summary>

`exchange(0)` 세 번은 각각 원자적이다. 하지만 첫 번째와 두 번째 사이에 ISR 이 들어오면 그 프레임은 frames 쪽엔 다음 스냅샷, bytes 쪽엔 이번 스냅샷에 들어간다. 단일 코어에서 "ISR 에 대해 원자적인 묶음" 을 만드는 도구는 하나다.

</details>

## 5. 풀이

### 0. 실측: 같은 std::atomic, 네 타깃 — `demo_targets.cpp`

`bash asm_targets.sh asm` 의 실제 출력에서 함수별 핵심 명령만 모았다 (Apple clang 21.0.0, `-O2 -fno-exceptions -fno-rtti`):

| 연산 (`demo_targets.cpp`) | M0+ (`thumbv6m`) | M3 (`thumbv7m`) | M4 (`thumbv7em`) | 호스트 arm64 |
|---|---|---|---|---|
| `load(relaxed)` 32 | `bl __atomic_load_4` (r1=0) | `ldr` | `ldr` | `ldr w0` |
| `load(acquire)` 32 | `bl __atomic_load_4` (r1=2) | `ldr; dmb sy` | 같음 | `ldapr` |
| `load()` seq_cst 32 | `bl __atomic_load_4` (r1=5) | `ldr; dmb sy` | 같음 | `ldar` |
| `store(release)` 32 | `bl __atomic_store_4` | `dmb sy; str` | 같음 | `stlr` |
| `store()` seq_cst 32 | `bl __atomic_store_4` | `dmb sy; str; dmb sy` | 같음 | `stlr` |
| `fetch_add(relaxed)` 32 | `bl __atomic_fetch_add_4` (r2=0) | `ldrex/adds/strex/cmp/bne` | 같음 | `ldadd` |
| `fetch_add()` seq_cst 32 | `bl __atomic_fetch_add_4` (r2=5) | `dmb sy` + 루프 + `dmb sy` | 같음 | `ldaddal` |
| `compare_exchange_strong` 32 | `bl __atomic_compare_exchange_4` | `ldrex/cmp/bne`, `dmb`, `strex/cbz`, 실패 경로 `clrex` | 같음 | `casal` |
| `load / store` 64 | `bl __atomic_load_8 / _store_8` | `bl __atomic_load_8 / _store_8` | 같음 | `ldar x0` / `stlr x0` |
| `fetch_add / CAS` 64 | `bl __atomic_fetch_add_8 / _compare_exchange_8` | 같음 (libcall) | 같음 | `ldaddal x` / `casal x` |
| volatile `v32 = v32 + 1` | `ldr; adds; str` | `ldr; adds; str` | 같음 | `ldr; add; str` |
| volatile `uint64_t` 읽기 | `ldr r1,[r0,#4]; ldr r0,[r0]` | `ldm r0, {r0, r1}` | `ldrd r0, r1, [r0]` | `ldr x0` |

M3 와 M4 의 차이는 출력 전체에서 **한 줄**뿐이었다 (`diff`: volatile 64비트 읽기의 `ldm` vs `ldrd`). 원자 연산 쪽은 완전히 같다 — M4 의 DSP/FPU 확장은 원자성과 무관하다.

M0+ 와 M3 의 `fetch_add()` (seq_cst) 실제 조각:

```text
@ M0+ (thumbv6m)                       @ M3 (thumbv7m)
add32_seqcst:                          add32_seqcst:
	push	{r7, lr}                       	movw	r1, :lower16:a32
	ldr	r0, .LCPI7_0   @ &a32           	movt	r1, :upper16:a32
	movs	r1, #1         @ 더할 값        	dmb	sy
	movs	r2, #5         @ seq_cst        .LBB7_1:
	bl	__atomic_fetch_add_4             	ldrex	r0, [r1]
	pop	{r7, pc}                       	adds	r2, r0, #1
                                       	strex	r3, r2, [r1]
                                       	cmp	r3, #0
                                       	bne	.LBB7_1
                                       	dmb	sy
                                       	bx	lr
```

읽는 법:

- **M0+ 는 32비트 load 까지 라이브러리 호출이다 (clang).** ARMv6-M 에는 LDREX/STREX 가 없고(DDI 0419 의 명령 목록에 exclusive 명령 없음), clang 은 이 타깃의 인라인 원자 폭을 0 으로 본다 (C-M14 실측 경고 `exceeds the max lock-free size (0 bytes)`). 마지막 인자 `movs r2, #5` 는 memory_order 번호다 (`__ATOMIC_SEQ_CST` = 5, relaxed = 0, acquire = 2, release = 3).
- **GCC 14 는 다르다.** VM 의 `arm-none-eabi-g++ 14.2.1 -mcpu=cortex-m0plus` 는 32비트 load/store 를 인라인으로 냈다: `ld32_acquire` = `ldr; dmb ish`, `ld32_seqcst` = `dmb ish; ldr; dmb ish`, `st32_release` = `dmb ish; str`. RMW 와 CAS 는 똑같이 `bl __atomic_fetch_add_4` / `bl __atomic_compare_exchange_4`. 정렬된 32비트 `LDR/STR` 은 v6-M 에서도 단일 복사 원자적(single-copy atomic)이므로 GCC 방식도 맞다. **같은 소스라도 컴파일러에 따라 필요한 라이브러리 함수 목록이 달라진다** (clang 객체는 `__atomic_load_4` 도 요구) — 연습 2.
- **M3/M4 의 배리어는 `dmb sy`(clang) / `dmb ish`(GCC).** 둘 다 C++ 매핑으로 맞다. release = 앞에 dmb, acquire = 뒤에 dmb, seq_cst store = 앞뒤. relaxed 는 배리어 0개. 의미는 C-A03 §5 에서 다뤘다.
- **64비트는 M3/M4 도 라이브러리 호출이다.** ARMv7-M 에 `LDREXD/STREXD` 가 없기 때문이다 (ARMv7-A/R 에만 있음). volatile 64비트 읽기가 `ldm` / `ldrd` 한 명령으로 보여도 원자적이지 않다: ARMv7-M 의 단일 복사 원자성은 워드 단위까지이고, `LDM`/`LDRD` 는 워드별 접근이며 예외로 중단 후 재시작/계속될 수 있다 (DDI 0403, A3.5.3 atomicity 절 — 절 번호는 판에 따라 다를 수 있음).
- **CAS 실패 경로의 `clrex`**: 비교가 틀려 `strex` 를 안 하고 나갈 때 배타 모니터를 정리한다. ISR 이 `ldrex`~`strex` 사이에 끼면 예외 진입/복귀가 로컬 모니터를 지워 `strex` 가 1 을 돌려주고 루프가 다시 돈다 — 인터럽트를 끄지 않는 원자 RMW 의 원리 (ARM-M11 이 QEMU+gdb 로 보여 준다).

### 0-1. lock-free 값을 컴파일러에게 묻기 — `demo_probe.cpp`

정의 없는 템플릿 `Show<...>` 에 값을 넣어 인스턴스화하면 에러 메시지에 값이 찍힌다 (`static_assert(false)` 를 일부러 실패시키는 것과 같은 요령 — 값을 출력해 준다). `bash asm_targets.sh probe` 실제 출력:

```text
== m0
Show<false, false, false, false, 1, 1, 1>
#define __CLANG_ATOMIC_INT_LOCK_FREE 1
#define __CLANG_ATOMIC_LLONG_LOCK_FREE 1
== m3
Show<true, true, true, false, 2, 1, 2>
#define __CLANG_ATOMIC_INT_LOCK_FREE 2
#define __CLANG_ATOMIC_LLONG_LOCK_FREE 1
== m4
Show<true, true, true, false, 2, 1, 2>
== host
Show<true, true, true, true, 2, 2, 2>
```

(`Show` 인자 순서: `is_always_lock_free` u8, u16, u32, u64, `ATOMIC_INT_LOCK_FREE`, `ATOMIC_LLONG_LOCK_FREE`, `ATOMIC_POINTER_LOCK_FREE`. VM 의 `arm-none-eabi-g++` 14.2 로 같은 probe 를 돌려도 세 코어 모두 같은 `Show<...>` 가 나왔다. 호스트는 16바이트 구조체·`unsigned __int128` 도 `is_always_lock_free == true` 였다.)

| 이름 | 종류 | 뜻 | 쓰는 곳 |
|---|---|---|---|
| `std::atomic<T>::is_always_lock_free` | `static constexpr bool` (C++17) | 이 타깃의 **모든** 객체가 lock-free | `static_assert` — 이 레슨의 정책 |
| `a.is_lock_free()` | 런타임 함수 | **이 객체** 가 lock-free (정렬 등에 따라) | 로그·자가 진단 |
| `ATOMIC_INT_LOCK_FREE` 등 | 매크로 0/1/2 | 0 = 절대 아님, 1 = 때때로, 2 = 항상 | `#if` (C 와 공용) |

M0+ 의 `ATOMIC_INT_LOCK_FREE == 1` ("때때로") 은 오해하기 쉽다. 실제 asm 에는 lock-free 인 경우가 없었다 — 1 은 "컴파일 시점에 '항상' 이라 말할 수 없다" 는 뜻일 뿐이다. 정책에는 1 이 아니라 `is_always_lock_free` (= 2 와 같음) 를 쓴다. 참고로 clang 은 M3 에 `__GCC_HAVE_SYNC_COMPARE_AND_SWAP_8 1` 도 정의했다 (`-dM -E` 실측) — 그런데 64비트 CAS 는 libcall 이었다. 이 매크로로 판단하지 말 것.

### Part A — 표를 constexpr 로, 정책을 static_assert 로

```cpp
constexpr Lowering lowering(Core c, Op op, unsigned bytes) {
    if (bytes == 0 || (bytes & (bytes - 1)) != 0 || bytes > max_lock_free_bytes(c)) return Lowering::Libcall;
    if (c == Core::HostArm64) return Lowering::SingleInsn;
    return (op == Op::Load || op == Op::Store) ? Lowering::PlainBarrier : Lowering::LlscLoop;
}
```

표를 코드로 만드는 이유: 면접에서 "M4 에서 `atomic<uint16_t>` fetch_add 는?" 같은 변형이 나와도 같은 세 질문(크기 한도 → 코어 → 연산)으로 답할 수 있다. 실제로 `std::atomic<uint16_t>::fetch_add` 를 M3 로 컴파일하면 `ldrexh/strexh`, `std::atomic<uint8_t>::exchange` 는 `ldrexb/strexb` 였다 (실측) — `lowering(M3, FetchAdd, 2) == LlscLoop` 테스트의 근거.

정책 클래스의 뼈대:

```cpp
template <class T, class Target = NativeTarget, class Policy = RequireLockFree>
class isr_shared {
    static_assert(policy_ok<T, Target, Policy>(), "isr_shared<T>: T is not always lock-free on this target. ...");
public:
    static constexpr bool uses_atomic = Target::template lock_free<T>;
    T fetch_add(T d) {
        if constexpr (uses_atomic) return v_.fetch_add(d);
        else { CriticalSection cs; const T old = load_words(v_); store_words(v_, static_cast<T>(old + d)); return old; }
    }
    ...
private:
    std::conditional_t<uses_atomic, std::atomic<T>, T> v_{};
};
```

설계 판단:

| 결정 | 이유 |
|---|---|
| 기본 정책 = `RequireLockFree` (빌드 실패) | 폴백은 인터럽트 지연을 늘린다. **쓰는 사람이 이름으로 동의** 해야 한다 (`AllowIrqFallback`) |
| `Target` 을 템플릿 인자로 | 펌웨어 빌드는 `NativeTarget` (= 컴파일러의 진짜 `is_always_lock_free`), 호스트 테스트는 `ModelTarget<Core::M3>` 로 **M3 에서 어느 경로를 타는지** 를 맥에서 검증 |
| `multicore` 면 폴백 금지 | PRIMASK 는 다른 코어를 못 막는다. RP2040(듀얼 M0+)에서 조용히 틀리느니 빌드 실패 (연습 4: HW 스핀락) |
| 부호 없는 정수만 | 폴백의 `old + d` 가 감싸기(wrap)로 정의되게. `bool`/구조체는 `fetch_add` 의미가 없다 |
| `if constexpr` + `conditional_t` | 안 쓰는 경로는 인스턴스화되지 않는다 → lock-free 타깃에서 폴백 코드 0바이트 |

`fetch_max` (lock-free 경로) 는 C-A03 의 CAS 워터마크를 C++ 로 옮긴 것이다. `compare_exchange_weak` 가 실패하면 `cur` 에 현재 값이 들어오므로 다시 load 하지 않는다. 호스트 4 스레드 × 10만에서 `cnt == 400000`, `mx == 99999*4+3` 이 매번 성립한다.

실제 펌웨어 판 `isr_shared.hpp` 를 타깃별로 빌드한 결과 (`bash asm_targets.sh policy`, 실제 출력):

```text
== host
== m4
== m3
== m0
./isr_shared.hpp:59:19: error: static assertion failed due to requirement 'policy_ok<unsigned int, NativeTarget, RequireLockFree>()': isr_shared<T>: T is not always lock-free on this target. Use AllowIrqFallback (single core only), a narrower T, or split the value (hi/lo, seqlock).
demo_policy_targets.cpp:7:22: note: in instantiation of template class 'isr_shared<unsigned int>' requested here
== m3 -DSHOW_ERROR
./isr_shared.hpp:59:19: error: static assertion failed due to requirement 'policy_ok<unsigned long long, NativeTarget, RequireLockFree>()': isr_shared<T>: T is not always lock-free on this target. ...
demo_policy_targets.cpp:10:22: note: in instantiation of template class 'isr_shared<unsigned long long>' requested here
```

호스트·M4·M3 는 에러 없음 (빈 줄), M0+ 는 `isr_shared<uint32_t>` 부터 거부, M3 에서 `isr_shared<uint64_t>` (strict) 거부. 같은 소스가 타깃에 따라 **빌드 단계에서** 갈린다 — 이게 이 레슨의 납품물이다.

### Part B — volatile 이 주지 않는 세 가지, 그리고 폴백

| ISR 공유에 필요한 것 | `volatile` | `std::atomic<T>` (lock-free) | 이 레슨의 증거 |
|---|---|---|---|
| 접근이 사라지지 않음 (폴링 루프) | O | O | (C-B09) |
| 찢어지지 않음 (64비트) | **X** — M0+ `ldr` 2개, M3 `ldm`, M4 `ldrd` | lock-free 면 O, 아니면 libcall | 표, `load_words(g_vol_ts) == 0x1FFFFFFFF` |
| RMW 원자성 (`++`) | **X** — `ldr; adds; str` | O (`ldrex/strex`, `ldaddal`) | `g_vol_count == 1` (2 여야 함) |
| 다른 변수와의 순서 | **X** — 비-volatile 접근은 컴파일러가 옮길 수 있고 CPU 배리어도 없음 | memory_order 대로 (`dmb`) | C-A02 / C-A03 |

C++20 은 `volatile` 객체의 `++` 를 **폐기 예정(deprecated)** 으로 만들었다 (P1152). Apple clang 21 `-std=c++20 -Wall` 에서 `volatile unsigned x; x++;` 는 `warning: increment of object of volatile-qualified type 'volatile unsigned int' is deprecated [-Wdeprecated-volatile]` 를 냈다 (실측; 같은 파일의 `x += 2` 는 경고가 없었다). `-Werror` 빌드에서는 그 줄이 에러가 된다 — `demo_targets.cpp` 가 `v32 = v32 + 1` 로 쓴 이유이고, 경고는 "이건 원자적이지 않다" 는 언어의 신호다.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="22" fill="currentColor" font-weight="bold">main 의 fetch_add 도중 RX 인터럽트: volatile vs PRIMASK 폴백</text>
  <text x="10" y="58" fill="currentColor">volatile</text>
  <rect x="100" y="40" width="120" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="110" y="60" fill="currentColor" font-family="monospace">t = c (=0)</text>
  <rect x="230" y="40" width="150" height="30" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="240" y="60" fill="currentColor" font-family="monospace">ISR: c = c+1 (=1)</text>
  <rect x="390" y="40" width="130" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="398" y="60" fill="currentColor" font-family="monospace">c = t+1 (=1)</text>
  <text x="535" y="60" fill="currentColor">→ 2 가 아니라 1 (유실)</text>
  <text x="10" y="128" fill="currentColor">폴백</text>
  <rect x="100" y="110" width="110" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="108" y="130" fill="currentColor" font-family="monospace">pm=PRIMASK</text>
  <rect x="215" y="110" width="80" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="222" y="130" fill="currentColor" font-family="monospace">old = c</text>
  <rect x="300" y="110" width="110" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="307" y="130" fill="currentColor" font-family="monospace">c = old + d</text>
  <rect x="415" y="110" width="120" height="30" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="422" y="130" fill="currentColor" font-family="monospace">PRIMASK=pm</text>
  <rect x="540" y="110" width="150" height="30" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="548" y="130" fill="currentColor" font-family="monospace">ISR: c += 1</text>
  <path d="M297 155 L297 145" stroke="currentColor" stroke-width="2"/>
  <text x="200" y="172" fill="currentColor">IRQ 요청 → PRIMASK=1 이라 pending</text>
  <path d="M540 172 Q 500 190 470 145" stroke="currentColor" fill="none" stroke-dasharray="4 3"/>
  <text x="480" y="192" fill="currentColor">복원 순간 실행 → 두 증가 모두 남음</text>
  <g fill="currentColor">
    <text x="10" y="228">중첩: 호출자가 이미 PRIMASK=1 이면 1 을 저장·복원 → ISR 은 바깥 구역 끝까지 pending.</text>
    <text x="10" y="250">64비트는 "old = c" 자체가 LDR 2개라 그 사이에도 틈이 있다. 폴백은 두 틈을 다 덮는다.</text>
    <text x="10" y="272">비용: 인터럽트 지연 = 구역 길이. NMI·HardFault 는 PRIMASK 로 안 막힌다.</text>
  </g>
</svg>
```

폴백 구현 (solution.cpp, 6개 모두 같은 모양):

```cpp
T fetch_add(T d) {
    if constexpr (uses_atomic) { return v_.fetch_add(d); }
    else {
        CriticalSection cs;                       // mrs pm, primask; cpsid i
        const T old = load_words(v_);             // 64비트면 워드 2개 (틈 포함)
        store_words(v_, static_cast<T>(old + d));
        return old;
    }                                             // ~cs: msr primask, pm → pending ISR 실행
}
```

테스트가 확인하는 것: ① 64비트 `load` 도중 캐리 ISR → `0xFFFFFFFF` (ISR 이전 값, 찢어지지 않음) 후 `0x1_00000000` ② `fetch_add` 와 ISR 의 증가가 둘 다 남음 ③ `store` 도중 ISR 이 읽으면 반쯤 쓴 값이 아니라 완성된 값 ④ CAS 실패 시 `expected` 갱신 ⑤ 이미 마스크된 상태에서 호출하면 마스크 유지 (CPP-B06 의 중첩 규칙) ⑥ M3 의 32비트는 atomic 경로라 `isr_window()` 가 아예 불리지 않음 (`g_armed_isr != nullptr` 그대로).

실제 타깃에서는 이 폴백이 어떻게 나오나 — `isr_shared.hpp` 를 M3 로 `-S` (실제 출력, `on_rx`):

```text
on_rx:
	movw	r2, :lower16:g_bytes
	mrs	r12, primask          @ 64비트 g_bytes: 폴백
	cpsid i
	movt	r2, :upper16:g_bytes
	ldr	r3, [r2]
	ldr	r1, [r2, #4]
	adds	r0, r0, r3
	adc	r1, r1, #0
	str	r0, [r2]
	str	r1, [r2, #4]
	movw	r1, :lower16:g_events
	msr	primask, r12
	movt	r1, :upper16:g_events
	dmb	sy                    @ 32비트 g_events: lock-free 경로
.LBB0_1:
	ldrex	r0, [r1]
	adds	r2, r0, #1
	strex	r3, r2, [r1]
	cmp	r3, #0
	bne	.LBB0_1
	dmb	sy
	bx	lr
```

(`@APP/@NO_APP` 줄 생략.) 64비트 덧셈이 `ldr ×2 / adds / adc / str ×2` 로 `cpsid i` ~ `msr primask` 안에 갇혀 있다. 컴파일러가 `movt` (주소 계산, 레지스터만) 는 asm 경계 너머로 옮겼지만 메모리 접근은 옮기지 않았다 — `"memory"` clobber 의 효과다.

### Part B — 그럼 M0+ 의 `__atomic_fetch_add_4` 는 누가 구현하나

`link_m0.sh` 실제 출력 (Lima VM, `arm-none-eabi-g++ 14.2.1`, `--specs=nosys.specs`, newlib 경고 줄 제외):

```text
### libgcc 에 있는 atomic/sync 심볼 (v6-m)
00000000 T __sync_synchronize_none
00000000 T __sync_synchronize_dmb
00000000 T __sync_synchronize
### M0+ : 라이브러리 없이
app_m0.cpp:(.text+0xc): undefined reference to `__atomic_fetch_add_4'
app_m0.cpp:(.text+0x1c): undefined reference to `__atomic_fetch_add_8'
app_m0.cpp:(.text.startup+0xc): undefined reference to `__atomic_exchange_4'
app_m0.cpp:(.text.startup+0x20): undefined reference to `__atomic_compare_exchange_4'
app_m0.cpp:(.text.startup+0x2a): undefined reference to `__atomic_load_8'
collect2: error: ld returned 1 exit status
### M3  : 라이브러리 없이
app_m0.cpp:(.text+0x1e): undefined reference to `__atomic_fetch_add_8'
app_m0.cpp:(.text.startup+0x38): undefined reference to `__atomic_load_8'
collect2: error: ld returned 1 exit status
### M0+ : demo_libatomic_m0.c 와 함께
link rc=0
000081ac <__atomic_fetch_add_4>:
    81ac:	0003      	movs	r3, r0
    81ae:	f3ef 8210 	mrs	r2, PRIMASK
    81b2:	b672      	cpsid	i
    81b4:	6800      	ldr	r0, [r0, #0]
    81b6:	1841      	adds	r1, r0, r1
    81b8:	6019      	str	r1, [r3, #0]
    81ba:	f382 8810 	msr	PRIMASK, r2
    81be:	4770      	bx	lr
```

| 후보 | 상태 (확인한 것 / 확인 못 한 것) |
|---|---|
| libgcc (arm-none-eabi 14.2, v6-m / v7-m) | `__sync_synchronize*` 만 있다 (nm 실측). `__atomic_*_N` 없음 |
| newlib libc.a (v6-m) | `__atomic_` / `__sync` 심볼 없음 (nm 실측) |
| GCC libatomic | arm-none-eabi 패키지에 `libatomic*` 파일 자체가 없다 (VM 에서 `find` 실측) |
| Apple clang 의 `macho_embedded/libclang_rt.soft_static.a` | armv6m 조각: `__atomic_*` 정의 없음. 오히려 그 안의 `atomic_flag_test_and_set` 이 `U ___atomic_exchange_1` 을 **요구** 한다. armv7m 조각: `__sync_fetch_and_add_4` 등 `__sync_*_4` 만 (nm 실측). Mach-O 용이라 ELF 펌웨어와는 별개, 참고만 |
| LLVM compiler-rt `lib/builtins/atomic.c` | 소스 기준: 주소 해시로 고른 **스핀락** 으로 `__atomic_*` 를 구현하고, 기본 빌드에서는 빠진다 (`COMPILER_RT_EXCLUDE_ATOMIC_BUILTIN`) — 미검증. 스핀락 방식은 main 이 락을 쥔 순간 같은 락을 ISR 이 잡으면 **영원히 돈다** → ISR 공유에는 부적합 |
| RTOS/SDK | Zephyr `CONFIG_ATOMIC_OPERATIONS_C` (irq_lock 기반), pico-sdk 2.x `pico_atomic` (스핀락 기반) 가 이 역할을 한다고 문서에 있음 — 미검증 |

결론: **bare-metal 에서는 보통 당신이 쓴다.** `demo_libatomic_m0.c` 가 그 모양이다 — `mrs/cpsid/…/msr` 로 감싼 일반 접근. 단일 코어에서는 인터럽트를 끈 구간이 seq_cst 보다 강하므로 memorder 인자는 무시해도 된다. ABI 는 호출 쪽 asm 에서 읽었다: `fetch_add_4(ptr=r0, val=r1, order=r2)`, `fetch_add_8(ptr=r0, val=r2:r3, order=[sp])`, `compare_exchange_4(ptr=r0, expected*=r1, desired=r2, success=r3, failure=[sp])`.

함정 하나 (실측): CAS 라이브러리 함수에는 **weak 인자가 없다** (위 레지스터 배치). 그런데 GCC 는 같은 이름의 **내장 함수** 를 weak 포함 6인자로 알고 있어서, 5인자로 정의하면

```text
demo_libatomic_m0.c:50:6: error: conflicting types for built-in function '__atomic_compare_exchange_4'; expected '_Bool(volatile void *, void *, unsigned int,  _Bool,  int,  int)' [-Werror=builtin-declaration-mismatch]
```

가 났다. 6인자로 맞추면 경고는 사라지지만 레지스터 배치가 틀린다(인자를 다 무시하니 "우연히" 동작). 그래서 `demo_libatomic_m0.c` 는 다른 C 이름(`m0_cas_4`)으로 정의하고 asm 라벨 `__asm__("__atomic_compare_exchange_4")` 로 심볼 이름만 맞췄다 — GCC·clang 모두 `-Werror` 로 통과, 두 `nm` 모두 `T __atomic_compare_exchange_4`.

### Part C — 텔레메트리: 원자 변수 셋 ≠ 원자 스냅샷

```cpp
static void uart_rx_isr(UartTelemetry& t, uint32_t len, uint32_t lat_us) {
    t.frames.fetch_add(1);          // M3: dmb + ldrex/strex + dmb
    t.bytes.fetch_add(len);         // M3: PRIMASK 폴백 (64비트)
    t.max_lat_us.fetch_max(lat_us); // M3: CAS 루프
}
static Snapshot snapshot_and_clear(UartTelemetry& t) {
    Snapshot s{};
    CriticalSection cs;             // 세 exchange 를 ISR 기준으로 하나의 원자 단위로
    s.frames = t.frames.exchange(0);
    isr_window();
    s.bytes = t.bytes.exchange(0);
    s.max_lat_us = t.max_lat_us.exchange(0);
    return s;
}
```

| 시나리오 (테스트) | naive | 고친 판 |
|---|---|---|
| 프레임 1개 쌓인 뒤, frames 교환 직후 RX ISR | `{frames 1, bytes 128}` — 관계 깨짐 | ISR 은 스냅샷 뒤로: `{2, 128, 70}` 다음 `{1, 64, 300}` |
| 1000 프레임 + 도중 143번 끼어듦, 5번마다 스냅샷 | — | 모든 스냅샷 `frames*64 == bytes`, 합계 1143 프레임 |

생각할 점:

- ISR 안의 `fetch_add` 들은 같은 우선순위의 다른 ISR 에 대해서는 이미 원자적이다 (Cortex-M 은 같은 우선순위끼리 선점하지 않음). 묶음 문제는 **읽는 쪽(main)** 에 있다. 그래서 임계 구역은 main 쪽 스냅샷에만 필요하다.
- 묶음이 lock-free 여야 한다면(임계 구역 금지): 프레임과 바이트를 **같은 워드에** 담거나(예: 상위 8비트 프레임 + 하위 24비트 바이트 — 넘침 관리 필요), seqlock (LLAT-M02) 을 쓴다. 정확한 관계보다 "대략 맞으면 됨" 이면 naive 도 실무에서 흔하다 — 대신 대시보드에 그렇게 적는다.
- 임계 구역 안의 `exchange` 는 M3 에서 여전히 `dmb + ldrex/strex` 다. 이미 인터럽트를 껐으니 낭비지만, 몇 사이클이고 정확하다. 줄이고 싶으면 relaxed 를 쓴다 — 단일 코어 ISR ↔ main 은 하드웨어 배리어가 필요 없다 (Q7).
- 듀얼 코어 (RP2040, STM32H7 의 M7+M4 공유 카운터): 두 번째 코어도 RX 를 처리한다면 PRIMASK 로는 부족하다 → `IrqSpinGuard` (IRQ 차단 + 하드웨어 스핀락, 연습 4). M0+ 듀얼은 LDREX 도 없어서 HW 스핀락이 유일한 선택이다.

### 정리: 무엇을 언제

| 공유 대상 | M0+ | M3 / M4 | 이 레슨 |
|---|---|---|---|
| 32비트 이하 카운터/플래그 | `isr_shared<T, …, AllowIrqFallback>` (실제로는 IRQ 차단) | `isr_shared<T>` (LDREX/STREX) | Part A |
| 64비트 카운터/타임스탬프 | 폴백 or hi/lo 분리 (C-M14) | 폴백 or hi/lo 분리 | Part B |
| 여러 필드의 일관된 묶음 | 임계 구역 | 임계 구역 or seqlock | Part C |
| MMIO 레지스터 | `volatile` (atomic 금지 — LDREX/libcall 이 장치 레지스터에 나가면 안 됨) | 같음 | C-M01 |
| 다른 코어와 공유 | HW 스핀락 + IRQ 차단 | LDREX/STREX (global monitor 필요) 또는 HW 세마포어 | 연습 4, C-A03 Q8 |

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -pthread solution.cpp -o solution && ./solution        # 54/54 passed
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -fno-exceptions -fno-rtti -pthread solution.cpp -o sol_asan && ./sol_asan   # 54/54, 보고 없음
c++ -std=c++20 -g -O1 -fsanitize=thread -fno-exceptions -fno-rtti -pthread solution.cpp -o sol_tsan && ./sol_tsan       # 54/54, 보고 없음
```

실제 출력 마지막 줄은 세 빌드 모두 `54/54 passed` 였고, strict 빌드를 6번 연속 돌려도 같았다. TSan 이 보는 것은 lock-free 경로의 4 스레드 스트레스뿐이다 — mock PRIMASK 폴백은 단일 스레드("단일 코어")에서만 돌린다. 그게 폴백의 전제 그대로다: 폴백 경로를 두 스레드에서 돌리는 것은 설계상 틀린 사용이고(PRIMASK 는 다른 코어를 못 막는다), 그 경우는 정책이 `multicore` 로 막는다 (그 잘못된 실행을 TSan 으로 돌려 보지는 않았다).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| ISR 카운터를 `volatile uint32_t` + `++` | `ldr; adds; str` — 사이에 ISR 이 오면 유실. C++20 은 `++` 자체를 deprecated 경고 | lock-free `std::atomic<uint32_t>` / `isr_shared<uint32_t>` |
| `volatile uint64_t` 타임스탬프 | M0+ `ldr` 2개, M3 `ldm`, M4 `ldrd` — 어느 것도 64비트 원자 아님 | 폴백 (임계 구역) 또는 hi-lo-hi (C-M14) |
| "atomic 이니까 ISR 에서 안전" | lock-free 가 아니면 libcall: 링크 실패, 숨은 IRQ 차단, 또는 스핀락 교착 | `static_assert(std::atomic<T>::is_always_lock_free)` |
| `ATOMIC_INT_LOCK_FREE >= 1` 로 판정 | 1 = "때때로" — M0+ 는 1 이지만 실제론 항상 libcall | `== 2` 또는 `is_always_lock_free` |
| `is_lock_free()` (런타임) 로 판정 | 빌드는 통과하고 현장에서 로그로만 안다 | constexpr `is_always_lock_free` |
| M0+ 용 `__atomic_*` 를 IRQ 차단으로 쓰고 RP2040 에서도 사용 | PRIMASK 는 자기 코어만 막는다 | HW 스핀락 + IRQ 차단 (`IrqSpinGuard`), 정책에서 `multicore` 면 폴백 금지 |
| CAS 라이브러리 함수를 GCC 내장 시그니처(weak 포함 6인자)로 정의 | 실제 호출은 5인자 — 레지스터 배치가 다르다 | 5인자 + asm 라벨 (`demo_libatomic_m0.c`) |
| 폴백에서 `__disable_irq(); … __enable_irq();` | 이미 꺼져 있던 호출자의 보호를 풀어 버린다 (CPP-B06) | PRIMASK 저장/복원 |
| 카운터 셋을 각각 `exchange(0)` 해서 "스냅샷" | 각각 원자적이어도 묶음은 아니다 (Part C naive) | 임계 구역 하나로 묶기, 한 워드로 합치기, seqlock |
| MMIO 레지스터를 `std::atomic<uint32_t>&` 로 | 장치 레지스터에 LDREX/STREX 나 libcall 이 나간다. 접근 폭·횟수 보장 없음 | `volatile` (C-M01), 원자성은 하드웨어 기능(BSRR, SET/CLR 레지스터)으로 |
| NMI/HardFault 핸들러에서 폴백 변수 접근 | PRIMASK 로 막히지 않는 예외 → 임계 구역이 보호 못 함 | 그 핸들러에선 lock-free 변수만, 또는 FAULTMASK 고려 |

## 8. 면접 Q&A

<details>
<summary>Q1. ISR 과 main 이 공유하는 변수에 volatile 이면 충분하지 않나요?</summary>

volatile 은 "접근을 지우거나 합치지 말라" 는 컴파일러 지시라 폴링 루프가 사라지지 않게 할 뿐이다. 세 가지가 없다: ① 찢어짐 방지 — 32비트 코어에서 64비트는 워드 2개 접근 (M0+ `ldr` 2개, M3 `ldm`, M4 `ldrd`, 실측) ② RMW 원자성 — `++` 가 `ldr; adds; str` (실측), 사이에 ISR 이 오면 유실 ③ 순서 — 비-volatile 데이터 접근은 volatile 플래그 앞뒤로 옮겨질 수 있고, 다른 코어/DMA 에 대한 배리어는 전혀 없다 (C-A02). 32비트 플래그를 ISR 이 쓰고 main 이 읽기만 한다면 volatile 로도 "동작" 하지만, 같은 일을 lock-free `std::atomic` 이 비용 거의 없이(relaxed 면 `ldr`/`str` 그대로) 언어 수준 보장과 함께 한다. 꼬리: "둘 다 붙이면?" → `volatile std::atomic` 은 장치 메모리나 시그널 핸들러와 공유하는 드문 경우뿐. MMIO 레지스터에는 atomic 을 쓰지 않는다.

</details>

<details>
<summary>Q2. is_always_lock_free, is_lock_free(), ATOMIC_INT_LOCK_FREE 는 어떻게 다르죠? M0+ 에서 값은?</summary>

`is_always_lock_free` 는 C++17 의 constexpr 멤버 — 이 타입의 모든 객체가 lock-free 인지 컴파일 시점에. `is_lock_free()` 는 이 객체 하나에 대한 런타임 답 (정렬이 나쁘면 false 일 수 있음). 매크로 `ATOMIC_xxx_LOCK_FREE` 는 0(절대)/1(때때로)/2(항상). 실측: M0+ 는 `is_always_lock_free` 가 u8~u64 모두 false, `ATOMIC_INT_LOCK_FREE` = 1. M3/M4 는 u8/u16/u32 true, u64 false, INT 2, LLONG 1. arm64 는 전부 true/2. 정책에는 `is_always_lock_free` 를 static_assert 로. 꼬리: "1 인데 실제 lock-free 인 경우가 있나?" → 이 레슨의 asm 에선 M0+ 에 하나도 없었다. 1 은 "항상이라고 약속 못 함" 일 뿐이다.

</details>

<details>
<summary>Q3. 왜 M0+ 에서는 32비트 fetch_add 조차, M3/M4 에서는 64비트가 lock-free 가 아닌가요? M33 이나 Cortex-A 는?</summary>

RMW 를 인터럽트를 끄지 않고 원자적으로 하려면 LL/SC (`LDREX/STREX`) 같은 배타 접근이 필요하다. ARMv6-M (M0/M0+/M1) 에는 배타 명령이 없다. ARMv7-M (M3/M4/M7) 에는 `LDREX/LDREXB/LDREXH` 는 있지만 64비트 `LDREXD/STREXD` 가 없다 — 그건 ARMv7-A/R 에만 있다. 그래서 M3/M4 의 `atomic<uint64_t>` 는 `__atomic_*_8` 호출이 된다 (실측). ARMv8-M Baseline (M23) 은 `LDREX/STREX` 와 `LDA/STL` 계열을 갖고, Mainline (M33/M55) 도 마찬가지지만 64비트 배타 명령은 없다 (DDI 0553 기준, 이 레슨에서 컴파일로 확인하진 않음). Cortex-A (ARMv7-A `LDREXD`, AArch64 `LDXP/STXP`·LSE `CASP`) 는 64비트도 lock-free — 호스트 arm64 는 16바이트까지 `is_always_lock_free` 였다. 꼬리: "M0+ 에서 32비트 load/store 는 원자적인가?" → 정렬된 `LDR/STR` 은 단일 복사 원자적이다. GCC 는 그래서 인라인(`ldr; dmb ish`)으로 내고, clang 은 그래도 libcall 로 냈다 (실측).

</details>

<details>
<summary>Q4. M0+ 에서 std::atomic 을 쓰면 링크 에러가 납니다. __atomic_fetch_add_4 는 누가 제공하나요?</summary>

arm-none-eabi 툴체인 기본 라이브러리에는 없다: libgcc 에는 `__sync_synchronize*` 만, newlib 에는 아무것도, libatomic 은 패키지에 없음 (전부 nm/find 실측) → `undefined reference to '__atomic_fetch_add_4'` (실측). 보통 펌웨어가 직접 쓴다: PRIMASK 저장 → `cpsid i` → 일반 접근 → 복원 (`demo_libatomic_m0.c`, 링크 후 `__atomic_fetch_add_4` 가 `mrs/cpsid/ldr/adds/str/msr` 8명령, 실측). RTOS/SDK 가 주기도 한다 (Zephyr, pico-sdk — 문서 기준). compiler-rt 의 범용 `atomic.c` 는 스핀락 기반이라 ISR 과 쓰면 교착 위험이 있고 기본 빌드에서 빠진다 (소스 기준, 미검증). 꼬리: "그 구현을 직접 쓸 때 함정은?" → CAS 의 라이브러리 ABI 는 5인자인데 GCC 내장 선언은 6인자라 `-Werror=builtin-declaration-mismatch` (실측) — asm 라벨로 이름만 맞춘다. 그리고 단일 코어 전용임을 주석과 정책으로 못박는다.

</details>

<details>
<summary>Q5. 인터럽트를 끄는 __atomic_* 는 "lock-free" 인가요? ISR 에서 불러도 되나요?</summary>

표준 의미로는 lock-free 가 아니고(`is_always_lock_free == false`), 그래서 정책이 기본으로 거부한다. 하지만 **단일 코어** 에서는 ISR 에서 불러도 안전하다: 기다릴 락이 없으니 교착이 없고, ISR 안에서 다시 PRIMASK 를 저장/복원할 뿐이다 (테스트의 중첩 케이스). 대가는 인터럽트 지연(구역 길이만큼, 수십 사이클)과 NMI/HardFault 는 막지 못한다는 점. 깨지는 곳: 다른 코어(RP2040 듀얼 M0+, 같은 SRAM 을 쓰는 M7+M4) — PRIMASK 는 자기 코어만 막는다. 그래서 `isr_shared` 는 `Target::multicore` 면 폴백을 컴파일 에러로 만든다. 꼬리: "스핀락 기반 구현이면?" → main 이 락을 쥔 채 ISR 이 같은 락을 잡으면 ISR 이 영원히 돈다(main 은 ISR 이 끝나야 진행). C11 7.14.1.1 / C++ [support.signal] 이 시그널 핸들러에 lock-free 원자만 허용하는 것과 같은 이유다.

</details>

<details>
<summary>Q6. std::atomic_flag 는 표준이 lock-free 를 보장하잖아요. M0+ 에서는요?</summary>

[atomics.flag] 는 `atomic_flag` 연산이 lock-free 여야 한다고 한다. 그런데 M0+ 로 컴파일하면 clang 은 `test_and_set` → `bl __atomic_exchange_1`, `clear` → `bl __atomic_store_1`, GCC 14 는 `bl __atomic_test_and_set` 을 냈다 (실측). 즉 "보장" 은 결국 당신이 제공하는 함수가 지킨다 — IRQ 차단 구현이면 단일 코어 기준으로는 "막히지 않음" 이 성립하지만 듀얼 코어에서는 아니다. Apple 의 armv6m compiler-rt 조각 안의 `atomic_flag_test_and_set` 도 `__atomic_exchange_1` 을 정의 없이 참조하고 있었다 (nm 실측). 꼬리: "그럼 M0+ 에서 스핀락은?" → 단일 코어엔 스핀락이 의미 없다(IRQ 차단), 듀얼 M0+ 는 SIO 하드웨어 스핀락 (연습 4).

</details>

<details>
<summary>Q7. 단일 코어 ISR ↔ main 공유에 seq_cst 가 필요한가요? M3 에서 비용은?</summary>

필요 없다. 같은 코어의 ISR 은 그 코어의 프로그램 순서대로 메모리를 본다. 막아야 하는 것은 **컴파일러** 재배치뿐이고, 그건 `std::atomic_signal_fence` 나 relaxed 원자 연산 + 그 펜스로 된다 (C-A02, C-A03 Q9). M3 에서 seq_cst `fetch_add` 는 `dmb sy` 2개 + 루프, seq_cst store 는 `dmb sy; str; dmb sy`, relaxed 는 배리어 0개 (실측). 카운터·통계는 relaxed 로 충분하다. 다만 DMA 나 다른 코어가 같은 메모리를 볼 때는 `dmb` 가 다시 필요하다 — 그때는 release/acquire. `isr_shared` 가 기본 seq_cst 인 건 "틀리지 않는 기본값" 이고, 핫 ISR 에서 줄이고 싶으면 memory_order 인자를 받는 판을 만들면 된다. 꼬리: "Cortex-M7 은?" → 슈퍼스칼라·캐시·AXI 라 다른 버스 마스터와 공유할 때 배리어가 실제로 의미 있다 (AN321).

</details>

<details>
<summary>Q8. 텔레메트리 카운터 세 개를 각각 atomic 으로 했는데 대시보드 숫자가 가끔 안 맞습니다. 왜죠?</summary>

각 변수는 원자적이지만 **묶음** 은 아니다. main 이 frames 를 교환하고 bytes 를 교환하기 전에 RX ISR 이 오면, 그 프레임은 이번 스냅샷의 bytes 와 다음 스냅샷의 frames 에 나뉘어 들어간다 (테스트: `{frames 1, bytes 128}`). 단일 코어면 스냅샷을 임계 구역 하나로 묶는다 — 몇 사이클 지연이고 ISR 은 끝나자마자 실행된다. 임계 구역을 못 쓰면 한 워드로 합치거나 seqlock (LLAT-M02). 꼬리: "ISR 쪽 갱신 세 줄도 묶어야 하나?" → 같은 우선순위 ISR 끼리는 선점하지 않으니 ISR 안의 세 줄은 main 에 대해 이미 한 덩어리다. 더 높은 우선순위 ISR 도 같은 카운터를 쓴다면 그쪽도 고려해야 한다.

</details>

<details>
<summary>Q9. M3 의 CAS 출력에 clrex 가 있던데 왜 필요하죠? LDREX 와 STREX 사이에 인터럽트가 오면?</summary>

`LDREX` 는 로컬 배타 모니터를 "이 주소 독점" 상태로 만든다. 비교가 틀려 `STREX` 를 하지 않고 나가면 모니터가 열린 채 남는데, 그 상태로 다른 코드(예: 다른 변수의 `STREX`)가 엉뚱하게 성공할 수 있어 `clrex` 로 닫는다 (실측: 실패 경로 `.LBB8_4: clrex`). ARMv7-M 은 예외 진입/복귀 시 로컬 모니터를 지우므로, ISR 이 사이에 끼면 돌아온 뒤의 `STREX` 는 실패(1)하고 루프가 다시 돈다 — 인터럽트를 끄지 않고도 RMW 가 원자적인 이유. ISR 이 같은 변수를 바꿨다면 재시도가 새 값을 읽는다. 이 동작을 QEMU + gdb 로 직접 끼워 넣어 보는 건 ARM-M11, AArch64 판은 ARM-M04.

</details>

## 9. 추가 연습

### 연습 1 — M0+ 용 `__atomic_*` 를 ABI 그대로 (호스트 모델)

`m0_atomic_fetch_add_4(volatile void*, uint32_t, int)`, `m0_atomic_compare_exchange_4(volatile void*, void* expected, uint32_t desired, int success, int failure)`, `m0_atomic_load_8`, `m0_atomic_store_8` 를 PRIMASK 저장/복원으로 구현하라. 읽기와 쓰기 사이(64비트는 워드 사이)에 `isr_window()` 를 두고, ISR 이 그 틈에서 같은 함수를 불러도 결과가 정확해야 한다.

<details>
<summary>답 보기</summary>

```cpp
static uint32_t m0_atomic_fetch_add_4(volatile void* p, uint32_t v, int memorder) {
    (void)memorder;                            // 인터럽트 차단 구간 = 단일 코어에서 seq_cst 이상
    const uint32_t pm = irq_save();
    volatile uint32_t* q = static_cast<volatile uint32_t*>(p);
    const uint32_t old = *q;
    isr_window();
    *q = old + v;
    irq_restore(pm);
    return old;
}
```

CAS 는 실패 시 `*expected = cur` 를 잊지 말 것 (std::atomic 과 같은 계약). memorder 인자를 무시해도 되는 이유: 단일 코어에서 인터럽트를 끈 구간 안의 접근은 그 코어의 프로그램 순서 그대로 보이고, asm 의 `"memory"` clobber 가 컴파일러 이동을 막는다. 테스트: `41 + main 1 + ISR 1 == 43`, CAS 실패 → `expected == 43`, 64비트 load 도중 캐리 → `0xFFFFFFFF` 그다음 `0x100000000`, 호출자가 이미 마스크했으면 유지. 실제 타깃 판은 `demo_libatomic_m0.c` (위 링크 실험).

</details>

### 연습 2 — GCC 14 는 무엇이 다른가: `lowering_gcc`

5. 풀이의 GCC 실측(M0+ 32비트 load/store 인라인)을 반영한 `lowering_gcc(Core, Op, bytes)` 를 써라. 그리고 "clang 으로 빌드한 M0+ 펌웨어가 GCC 로 빌드한 것보다 더 많이 요구하는 libcall" 을 말해 보라.

<details>
<summary>답 보기</summary>

```cpp
if (bytes > 4 || (bytes & (bytes - 1)) != 0) return Lowering::Libcall;
if (c == Core::M0plus) return ldst ? Lowering::PlainBarrier : Lowering::Libcall;
return ldst ? Lowering::PlainBarrier : Lowering::LlscLoop;
```

clang 객체는 `__atomic_load_4` / `__atomic_store_4` 도 요구한다 (GCC 객체는 `ldr; dmb ish` 인라인). 그래서 라이브러리 쪽 구현은 "내 컴파일러가 지금 부르는 것" 이 아니라 **크기별 전체 세트** (load/store/exchange/CAS/fetch_* × 1/2/4/8) 를 갖추는 게 안전하다 — 링크 에러는 어떤 연산을 처음 쓰는 날 터진다. 두 컴파일러 모두 M3 의 64비트는 libcall.

</details>

### 연습 3 — 구조체 단위 정책: `all_lock_free<Target, Ts...>`

모터 ISR 이 갱신하는 필드 타입 목록을 받아 모두 lock-free 인지 검사하는 `all_lock_free`, 그리고 에러 메시지·로그용으로 **첫 위반 필드 번호** 를 돌려주는 `first_not_lock_free` 를 fold expression 으로 써라.

<details>
<summary>답 보기</summary>

```cpp
template <class Target, class... Ts> constexpr bool all_lock_free() { return (Target::template lock_free<Ts> && ...); }
template <class Target, class... Ts> constexpr int first_not_lock_free() {
    int idx = 0, found = -1;
    ((found < 0 && !Target::template lock_free<Ts> ? (found = idx, 0) : 0, ++idx), ...);
    return found;
}
static_assert(first_not_lock_free<M3, uint32_t, uint16_t, uint64_t, uint8_t>() == 2);
```

쉼표 fold 는 왼쪽부터 평가되므로 `idx` 가 필드 순서대로 증가한다. M3 에서 `{u32, u16, u8}` 은 통과, `{u32, u64}` 는 실패(1번), M0+ 는 `u8` 부터 실패(0번). 실무에서는 이걸 ISR 공유 구조체 정의 바로 아래에 `static_assert` 로 둔다 — 누가 필드를 `uint64_t` 로 넓히는 순간 M3 빌드가 멈춘다.

</details>

### 연습 4 — 듀얼 코어 M0+ (RP2040 류): IRQ 차단 + 하드웨어 스핀락

RP2040 SIO 의 SPINLOCKn 레지스터는 읽으면 "비어 있었으면 잡고 nonzero, 이미 잡혀 있으면 0", 쓰면 해제다 (데이터시트 기준). 이 모델(`SioSpinlock`)로 `IrqSpinGuard` 를 만들어라: 자기 코어의 ISR 과 다른 코어를 **둘 다** 막아야 한다. 스레드 2개 = 코어 2개 (PRIMASK 는 `thread_local`).

<details>
<summary>답 보기</summary>

```cpp
explicit IrqSpinGuard(SioSpinlock& s) : s_(s), pm_(irq_save()) {
    while (s_.read_claim() == 0u) { }
}
~IrqSpinGuard() { s_.write_release(); irq_restore(pm_); }
```

순서가 핵심이다: **IRQ 먼저 끄고 → 스핀락**. 반대로 하면 락을 쥔 채 같은 코어의 ISR 이 들어와 같은 락을 잡으려고 영원히 돈다 (Linux `spin_lock_irqsave` 가 같은 구조). 해제는 역순. 모델의 `read_claim` 은 `exchange(acquire)`, 해제는 `store(release)` 라 일반 변수 `g_shared_bytes` 에 happens-before 가 생기고 TSan 이 깨끗하다. 테스트: 가드 안에서 내 코어 ISR → pending, 복원 후 실행 (`1 + 1000`); 두 "코어" × 10만 번 → `300000` 정확. pico-sdk 의 `spin_lock_blocking()` 도 IRQ 저장·차단 후 스핀하는 구조다 (문서 기준).

</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -fno-exceptions -fno-rtti -pthread practice.cpp -o practice && ./practice   # 21/21 passed
c++ -std=c++20 -g -O1 -fsanitize=thread -fno-exceptions -fno-rtti -pthread practice.cpp -o practice_t && ./practice_t   # 21/21, 보고 없음
```

## 10. 핵심 정리

- 원자적(atomic)은 언어가, lock-free 는 코어가 정한다. ISR 공유에는 **lock-free** 가 필요하고, 그건 `static_assert(std::atomic<T>::is_always_lock_free)` 로 빌드 때 확인한다 (`ATOMIC_INT_LOCK_FREE == 1` 은 "아님" 으로 읽는다).
- 실측 (clang 21): M0+ = 모든 원자 연산이 `bl __atomic_*_N` (GCC 는 32비트 load/store 만 인라인). M3/M4 = 32비트 load/store 는 `ldr/str + dmb sy`, RMW 는 `ldrex/strex` 루프, 64비트는 전부 libcall. arm64 = `ldar/stlr/ldaddal/casal` 한 개.
- 이유: ARMv6-M 에는 배타 명령이 없고, ARMv7-M 에는 `LDREXD` 가 없다.
- arm-none-eabi 의 libgcc·newlib 에는 `__atomic_*_N` 이 없다 → 링크 에러. 단일 코어면 PRIMASK 저장/복원으로 직접 구현 (CAS 는 5인자 ABI, GCC 내장 선언과 충돌 주의). 듀얼 코어면 HW 스핀락.
- `volatile` 은 찢어짐 방지·RMW 원자성·순서 셋 다 주지 않는다 (`ldr; adds; str`, `ldm`/`ldrd`). C++20 은 volatile `++` 를 deprecated.
- 정책 `isr_shared<T, Target, Policy>`: lock-free 면 `std::atomic`, 아니면 기본은 빌드 실패, `AllowIrqFallback` + 단일 코어일 때만 임계 구역. 멀티코어 폴백은 타입으로 금지.
- 원자 변수 여러 개 ≠ 원자 묶음. 일관된 스냅샷은 임계 구역 하나 (또는 한 워드, seqlock).

## 참고자료

- C++ 표준 (N4861 C++20 초안): [atomics.lockfree] (`ATOMIC_xxx_LOCK_FREE`, `is_always_lock_free`), [atomics.types.operations] (`is_lock_free`, `compare_exchange_*` 의 expected 갱신·가짜 실패), [atomics.types.int] (정수 fetch_add 의 감싸기), [atomics.flag] (lock-free 보장), [atomics.order], [atomics.fences] (`atomic_signal_fence`), [intro.races] (데이터 레이스), [support.signal] (시그널 핸들러에서 허용되는 원자 연산), [depr.volatile.type] / P1152R4 (volatile 폐기 예정) — <https://isocpp.org/files/papers/N4861.pdf>
- cppreference — `std::atomic<T>::is_always_lock_free`: <https://en.cppreference.com/w/cpp/atomic/atomic/is_always_lock_free>
- ARMv6-M Architecture Reference Manual (DDI 0419) — 명령 목록(배타 명령 없음), 메모리 모델: <https://developer.arm.com/documentation/ddi0419/latest/>
- ARMv7-M Architecture Reference Manual (DDI 0403) — A3.4 Synchronization and semaphores (LDREX/STREX, CLREX, 예외 시 로컬 모니터 클리어), A3.5 메모리 접근 원자성, A3.7 배리어: <https://developer.arm.com/documentation/ddi0403/latest/>
- ARMv8-M Architecture Reference Manual (DDI 0553) — Baseline/Mainline 의 배타·acquire/release 명령: <https://developer.arm.com/documentation/ddi0553/latest/>
- ARM Application Note 321 — Cortex-M 메모리 배리어 사용 지침: <https://developer.arm.com/documentation/dai0321/latest/>
- GCC wiki — Atomic library ABI (`__atomic_*_N` 크기별 함수): <https://gcc.gnu.org/wiki/Atomic/GCCMM/LIbrary>
- LLVM — Atomic Instructions and Concurrency Guide (libcall 으로 내려가는 조건): <https://llvm.org/docs/Atomics.html>
- LLVM compiler-rt `lib/builtins/atomic.c` (락 기반 범용 구현): <https://github.com/llvm/llvm-project/blob/main/compiler-rt/lib/builtins/atomic.c>
- C/C++11 mappings to processors (Cambridge): <https://www.cl.cam.ac.uk/~pes20/cpp/cpp0xmappings.html>
- RP2040 Datasheet — 2.3.1 SIO 의 Hardware Spinlocks 절: <https://datasheets.raspberrypi.com/rp2040/rp2040-datasheet.pdf>
- 관련 레슨: C-M14 (ISR 공유·64비트 tearing·PRIMASK), C-A02 (컴파일러 배리어·signal fence·DMA), C-A03 (C11 atomics·M4 asm), CPP-B06 (CriticalSection RAII), C-B09 / C-M01 (volatile·MMIO), OS-M08 (메시지 패싱·DCLP), MCPP-M13 (fence·release sequence), ARM-M04 (LDXR/STXR·LSE), ARM-M05 (DMB/DSB/ISB), ARM-M11 (Cortex-M LDREX/STREX 와 예외), RISCV-M04 (LR/SC), LLAT-B06 (원자 연산 비용), LLAT-M02 (seqlock), RTOS-A08 (SMP RTOS)
