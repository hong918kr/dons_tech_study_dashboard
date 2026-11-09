---
id: LLAT-A06
time: 110분
updated: 2026-11-08
---
# LLAT-A06 저지연 비동기 로거 (NanoLog 스타일)

## 학습 목표

- hot path 에서 **포맷팅을 아예 하지 않는** 로거를 직접 짠다: 호출 지점마다 한 번만 등록하는 사이트 id(함수 내부 `static`), 원시 타임스탬프(`cntvct_el0`), 원시 인자 바이트를 스레드 전용 SPSC 바이트 링에 복사, 백그라운드 스레드가 포맷팅.
- 가변 길이 레코드 링의 `reserve → 쓰기 → commit` 과 패드 레코드를 짜고, 각 load/store 의 memory_order 를 "누가 누구의 쓰기를 봐야 하나" 로 설명한다 (이 맥에서 `ldr` / `ldapr` / `stlr` 로 나오는 것까지).
- 버퍼가 가득 찼을 때의 두 정책 — **DROP**(정확한 drop 수 + 빠진 자리에 마커) 과 **BLOCK**(손실 0, 생산자 정지, 상한 시간) — 을 강제 상태로 결정적으로 검사하고, 버스트에서 각각이 지연 꼬리와 손실을 어떻게 바꾸는지 숫자로 말한다.
- 문자열 인자를 포인터로 저장하면 왜 틀리는지(수명) 를 재현하고(ASan heap-use-after-free), 바이트 복사 비용과 맞바꾼다.
- 같은 구조가 펌웨어의 Trice / SEGGER RTT / ITM / defmt 식 "id 만 보내고 호스트에서 포맷" 이라는 걸 바이너리 프레임 + 사전 + 오프라인 디코더로 구현하고, 펌웨어판 hot path 의 Cortex-M4 명령(`dmb sy`)을 본다.

## 1. 문제

로그 한 줄의 비용은 생각보다 크다. `fprintf` 한 번은 이 맥에서 평균 ~200 ns 이고, stdio 버퍼가 찰 때마다 `write(2)` 가 끼어 p99 가 ~3 µs 로 튄다 (아래 측정). 모터 제어 루프·트레이딩 피드 핸들러·센서 ISR 처럼 µs 예산으로 도는 코드에서 "디버깅하려고 넣은 로그가 타이밍을 바꿔서 버그가 사라지는" 일이 실제로 생긴다. 그래서 저지연 면접(HFT)과 펌웨어 면접 모두 "hot path 에서 로그를 어떻게 남기나?" 를 묻고, 정답의 뼈대는 같다: **정적인 것(포맷 문자열·파일·줄·레벨·인자 타입)은 한 번만, 동적인 것(시각·인자 값)만 원시 바이트로, 문자열 만들기는 나중에 다른 곳에서.** Stanford 의 NanoLog (Yang, Park, Ousterhout, USENIX ATC 2018) 가 이걸 끝까지 밀어붙인 설계이고, 펌웨어 세계의 Trice / defmt / Zephyr 사전 로깅도 같은 생각이다.

이미 아는 것 (다시 가르치지 않는다): release/acquire 로 데이터 공개 (C-A03), ISR↔main 공유 변수 (C-M14), 가변 길이 레코드를 바이트 링에 넣는 법과 split vs 패드 선택 (DS-M10), 원자 연산 비용 (LLAT-B06), SPSC 링의 정확성 논증 (DS-A01 — 이 층에서 같이 쓰는 중), 캐시된 상대 인덱스·패딩·배치의 지연 효과 측정 (LLAT-M01). 여기서는 그것들을 **로거라는 제품** 으로 조립하고, 로거에만 있는 문제(포맷 레지스트리, 인자 직렬화, 가득 참 정책, 문자열 수명, 디코더)를 푼다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">NanoLog 구조: hot path 는 복사만, 포맷팅은 백그라운드</text>
<rect x="10" y="36" width="200" height="74" rx="6" class="f-acc" stroke="currentColor"/>
<text x="20" y="56" fill="currentColor">호출 지점 (스레드 T1)</text>
<text x="20" y="76" fill="currentColor" font-size="12" font-family="monospace">NLOG(lg, INF, "id=%d",</text>
<text x="20" y="94" fill="currentColor" font-size="12" font-family="monospace">     id);</text>
<rect x="10" y="130" width="200" height="74" rx="6" class="f-acc" stroke="currentColor"/>
<text x="20" y="150" fill="currentColor">호출 지점 (스레드 T2)</text>
<text x="20" y="170" fill="currentColor" font-size="12" font-family="monospace">NLOG(lg, WRN, "state %s",</text>
<text x="20" y="188" fill="currentColor" font-size="12" font-family="monospace">     name);</text>
<rect x="250" y="36" width="210" height="74" rx="6" class="f-ok" stroke="currentColor"/>
<text x="260" y="56" fill="currentColor">T1 전용 SPSC 바이트 링</text>
<text x="260" y="76" fill="currentColor" font-size="12">[16B 헤더: size site argbytes ts]</text>
<text x="260" y="94" fill="currentColor" font-size="12">[int 8B][double 8B][패딩]</text>
<rect x="250" y="130" width="210" height="74" rx="6" class="f-ok" stroke="currentColor"/>
<text x="260" y="150" fill="currentColor">T2 전용 SPSC 바이트 링</text>
<text x="260" y="170" fill="currentColor" font-size="12">[헤더][u16 길이 + 문자 바이트]</text>
<text x="260" y="188" fill="currentColor" font-size="12">문자열은 복사 (포인터 아님)</text>
<path d="M210 73 L250 73 M210 167 L250 167" stroke="currentColor" fill="none"/>
<rect x="500" y="80" width="230" height="80" rx="6" class="f-warn" stroke="currentColor"/>
<text x="510" y="100" fill="currentColor">백그라운드 소비자 1개</text>
<text x="510" y="120" fill="currentColor" font-size="12">링마다 acquire → 레코드 해석 →</text>
<text x="510" y="138" fill="currentColor" font-size="12">조각별 snprintf → 파일 (또는</text>
<text x="510" y="154" fill="currentColor" font-size="12">바이너리 그대로 + 오프라인 디코더)</text>
<path d="M460 73 L500 105 M460 167 L500 135" stroke="currentColor" fill="none"/>
<rect x="10" y="224" width="450" height="58" rx="6" class="f-mut" stroke="currentColor"/>
<text x="20" y="244" fill="currentColor">사이트 표 (한 번만 등록): id → 포맷 조각, 파일:줄, 레벨, 인자 종류</text>
<text x="20" y="264" fill="currentColor" font-size="12">첫 호출 때만 register_site(), 이후엔 guard 확인 한 번</text>
<path d="M500 160 L470 240" stroke="currentColor" stroke-dasharray="4 3" fill="none"/>
<text x="476" y="214" fill="currentColor" font-size="12">id 로 찾아봄</text>
<text x="10" y="306" fill="currentColor" font-size="12">hot path = 시계 + thread_local 확인 + memcpy + release store 1번 (이 맥 평균 ~9–14 ns)</text>
<text x="10" y="324" fill="currentColor" font-size="12">대가: 소비자가 포맷팅을 따라가지 못하면 링이 찬다 → DROP 또는 BLOCK 을 골라야 한다</text>
</svg>
```

체크 테스트(`starter.cpp` → `solution.cpp`)는 **매 실행 항상 성립하는 것만** 본다: 가짜 시계(1, 2, 3, …)로 만든 정확한 텍스트, 정확한 바이트 수, 강제로 만든 '가득 참' 상태에서의 정확한 drop 수, 스트레스에서 모든 메시지 정확히 한 번 + 스레드별 순서. 지연·처리량은 `demo_bench.cpp` 의 출력이다.

### Part A — 사이트 레지스트리 + 레코드 + SPSC 바이트 링 + 포맷팅

- 주어짐: `parse_format` (printf 부분집합을 "리터럴 + 명세 1개" 조각으로 쪼갬), `register_site` (조각 수·인자 종류 검사, 틀리면 `BAD_SITE`), `NLOG` 매크로, `Logger` 골격, 스레드별 링 찾기.
- TODO A1 `StagingRing::reserve(n)` / `commit()` — 연속 n 바이트 예약, 끝에 안 맞으면 남은 칸을 **패드 레코드** 로 채우고 0 부터. 자리 없으면 `nullptr`. 공개는 `commit` 한 번.
- TODO A2 `arg_bytes` / `put_arg` — 스칼라는 8 B 그대로, 문자열은 `u16 길이 + 바이트` (최대 200 B, 넘으면 자름).
- TODO A3 `format_message` — 조각 순서대로 인자를 꺼내 명세마다 `snprintf`.
- TODO A4 `StagingRing::consume` — 공개된 레코드를 읽고(패드는 건너뜀) `head` 를 돌려준다.
- 검사: ints/doubles/strings/`%%`/`%c`/`%zu`/`PRId64`/`%hhu`/`%p` 섞인 6 줄이 **글자 하나까지** 기대값과 같다, 레코드 크기 40 B, 256 B 링에서 50 개를 3 개마다 비우며 패드 정확히 3 번, 레벨 필터는 인자 평가도 안 한다.

### Part B — 가득 참 정책, 문자열 수명, 백그라운드 스레드

- TODO B1 `Logger::acquire_space` — **DROP**: 실패하면 `dropped++`, 다음에 자리가 나면 **먼저** `DROP_SITE` 마커 레코드(개수 8 B)를 쓴다 → 출력에 "빠진 자리" 가 정확히 표시된다. **BLOCK**: `stalls++` 후 잠깐 돌고(spin) → 양보(yield), `block_timeout` 이 지나면 drop 으로 바꾼다 (로거가 시스템을 영원히 멈추게 하지 않는다).
- 검사 (DROP): 1024 B 링 + 24 B 레코드 → 정확히 42 개 수락, 58 개 drop, 비운 뒤 다음 줄 앞에 `nlog: dropped 58 messages`.
- 검사 (문자열 수명): 주어진 `PtrLogger`(포인터만 저장)는 호출 뒤 바뀐 버퍼 내용 `SECOND` 를 찍고, 이 로거는 호출 시점 `first` 를 찍는다.
- 검사 (BLOCK, 강제): 소비자 없이 생산자 스레드가 100 개 → `stalls ≥ 1` 이 될 때 `tail == 42 × 24` (정확히 42 개에서 멈춤) → main 이 소비자가 되면 100 개 전부 순서대로, drop 0. 상한 20 ms 로는 43 번째가 timeout → drop 1.
- 검사 (스트레스): 백그라운드 소비자 + 생산자 4 개 × 20000 (4 KB 링, BLOCK) → 80000 줄, 각 (t, i) 정확히 한 번, 스레드별 i 증가, drop 0. 로거 두 개에 번갈아 써도 섞이지 않는다.

### Part C — 펌웨어 모델: 바이너리 프레임 + 사전 + 오프라인 디코더

- TODO C1 `BinarySink::on_record` — 링 레코드를 `[u16 site][u64 ts][인자 바이트]` 프레임으로 (길이 필드 없음).
- TODO C2 `decode_stream(dict, bytes, out)` — 헤더 `"NLG1" + 사전 해시` 확인 → 프레임마다 **사전으로** 인자 길이를 계산(경계 검사) → 텍스트. 사전이 다르면 -1, 잘리거나 깨진 프레임이면 -2 (그 앞까지는 살림).
- 검사: 디코드 결과 == Part A 텍스트 경로 결과, 스트림 정확히 180 B, 다른 빌드의 사전 거절, 잘린 스트림 → 5 줄 + -2, 문자열 길이 0xFFFF 로 깨진 프레임 → 경계 밖을 읽지 않고 -2 (ASan 깨끗).

## 2. Problem (English)

Design a low-latency asynchronous logger in the style of NanoLog for a C++20 service whose hot threads must not spend more than tens of nanoseconds per log call.

**Part A.** A call site `NLOG(logger, level, "printf-format", args...)` must (1) be type-checked at compile time against the format, (2) register its static metadata (format, file, line, level, argument kinds) exactly once, thread-safely, and get a 16-bit id, (3) on every call copy only `{id, raw timestamp, raw argument bytes}` into a per-thread single-producer/single-consumer byte ring. Records are variable length: `struct RecHdr { u32 size; u16 site; u16 argbytes; u64 ts; }` followed by arguments (8 bytes per scalar, `u16 len + bytes` per string, max 200 bytes), padded to 8. Implement `unsigned char *StagingRing::reserve(uint32_t n)` (contiguous space; if the tail end is too short, emit a pad record and wrap), `void commit()`, `size_t consume(F fn, size_t max)` and `size_t format_message(...)`. Justify every memory order.

**Part B.** Implement two full-buffer policies: `Drop` (count drops exactly, and the next successful call first emits a marker record "dropped N messages" so the gap is visible in the output) and `Block` (no loss; the producer spins then yields; after `block_timeout` it gives up and counts a drop). Explain why storing `const char*` arguments instead of copying them is a lifetime bug. A background thread drains all rings; with 4 producers every message must appear exactly once and in per-thread order.

**Part C.** Firmware mode: instead of formatting, emit binary frames `[u16 site][u64 ts][args]` behind a header `"NLG1" + hash(dictionary)`, and write an offline decoder that rebuilds the exact text using the dictionary, rejects a mismatched dictionary, and never reads past the end of a truncated or corrupted stream.

Follow-up: measure per-call latency (median / p99 / p99.9) against `snprintf` into a stack buffer, `fprintf` to a file and spdlog (sync and async), plus throughput, tail latency and drop counts under a 1 M message burst.

## 3. 시작 코드

<!-- code: starter.cpp -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread starter.cpp -o starter && ./starter
```

처음 실행 결과 (실제 출력 일부, exit 1, 0.09초에 끝남 — `reserve` 스텁은 `nullptr`, `consume` 스텁은 0 을 돌려주므로 아무것도 기다리지 않는다. 스레드를 쓰는 검사 6 개는 Part A 단위 검사가 다 통과해야 돈다):

```text
-- Part A: 사이트 등록 + 인코딩 + SPSC 바이트 링 + 포맷팅 --
FAIL starter.cpp:542  arg_bytes(42) == 8u && arg_bytes(2.5) == 8u && arg_bytes("abc") == 5u && ...
FAIL starter.cpp:551  p == r.buf.get() && r.tail.load() == 0u
...
FAIL starter.cpp:598  sink.text == mixed_expected(1000)
...
-- Part B: 가득 참 정책(drop/block) + 문자열 수명 + 백그라운드 스레드 --
FAIL starter.cpp:659  accepted == 42 && lg.my_ring().dropped.load() == 58u
...
SKIP 6 threaded checks (Part B block/stress): unit checks failed
-- Part C: 바이너리 프레임 + 사전 + 오프라인 디코더 --
FAIL starter.cpp:785  bin.frames == 6u
...
8/40 passed
```

처음부터 통과하는 8 개는 주어진 코드의 검사(레지스트리 4 개, `PtrLogger` 1 개)와 스텁이 우연히 맞히는 3 개(commit 전 `consume == 0`, 빈 링 `head == tail`, 사전 불일치 `-1`)다. 관찰용 파일 (하네스와 별개, 숫자는 CHECK 하지 않는다):

```bash
c++ -std=c++20 -O2 -pthread demo_bench.cpp -o bench && ./bench          # 호출당 지연 + 버스트 (약 1.5초)
#   spdlog 포함: git clone --depth 1 --branch v1.17.0 https://github.com/gabime/spdlog /tmp/LLAT-A06_work/spdlog
#   c++ -std=c++20 -O2 -pthread -DHAVE_SPDLOG -I/tmp/LLAT-A06_work/spdlog/include demo_bench.cpp -o bench
c++ -std=c++20 -g -O1 -fsanitize=address -fno-omit-frame-pointer -pthread demo_lifetime.cpp -o life && ./life ptr   # UAF
c++ -std=c++20 -O2 -c demo_asm.cpp -o asm.o && xcrun llvm-objdump -d --no-show-raw-insn asm.o | c++filt   # hot path 명령
clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S demo_cortexm.c -o -                      # 펌웨어판
```

데모 세 개(`demo_bench` / `demo_lifetime` / `demo_asm`)는 `#define NLOG_NO_MAIN` 후 `solution.cpp` 를 include 해서 로거만 쓴다.

## 4. 힌트

<details>
<summary>힌트 1 — reserve: 연속 자리와 패드</summary>

레코드를 링 끝에서 쪼개면(DS-M10 의 split) 생산자가 `memcpy` 를 두 번 해야 하고, 무엇보다 **헤더 구조체를 그 자리에 바로 쓸 수 없다**. 연속 자리를 보장하면 hot path 는 "포인터 받아서 그냥 쓴다" 가 된다. `off = tail & mask` 에서 `off + n > cap` 이면 `pad = cap - off`, 필요한 양은 `pad + n`. 레코드 크기를 전부 8 의 배수로 맞추면 남은 칸도 항상 8 의 배수 ≥ 8 이라 패드 헤더(`size` 4 B + `site` 2 B)가 늘 들어간다. 그리고 `n ≤ cap/2` 이면 빈 링에는 항상 들어간다 (그렇지 않으면 BLOCK 이 영원히 기다릴 수 있다).
</details>

<details>
<summary>힌트 2 — 어떤 load 가 acquire 여야 하나</summary>

방향이 두 개다. (1) 생산자의 레코드 쓰기 → 소비자의 읽기: `tail` 을 release 로 쓰고 acquire 로 읽는다. (2) 소비자의 읽기 끝 → 생산자의 **덮어쓰기**: `head` 를 release 로 쓰고 생산자가 acquire 로 읽는다. (2) 를 relaxed 로 하면, 약한 메모리에서 생산자의 새 쓰기가 소비자의 아직 끝나지 않은 읽기보다 먼저 보일 수 있다 (읽는 중인 바이트를 덮어씀). 자기 변수(`tail` 을 생산자가, `head` 를 소비자가 읽을 때)는 relaxed.
</details>

<details>
<summary>힌트 3 — 캐시된 head</summary>

`head` 는 소비자의 캐시 라인에 있다. 매번 읽으면 메시지마다 라인이 생산자 ↔ 소비자 사이를 왕복한다. 생산자는 마지막으로 본 값 `head_cache` 를 들고 있다가 "그 값으로 계산해서 모자랄 때만" 진짜 `head` 를 읽는다. 옛 head 는 실제보다 **덜 비어 있다고** 말할 뿐이라 안전하다. (이 최적화의 지연 효과 측정은 LLAT-M01 의 몫.)
</details>

<details>
<summary>힌트 4 — format_message: 조각 하나에 snprintf 하나</summary>

`parse_format` 이 이미 `"a=%d b=%.2f"` 를 `{lit "a=", spec "%d"}`, `{lit " b=", spec "%.2f"}` 로 쪼개 두었다. 조각마다 `out += lit` 하고, `spec` 이 있으면 종류에 맞는 값을 꺼내 `snprintf(buf, spec, value)`. 정수는 저장은 8 B 지만 printf 에는 **명세의 길이 수식어에 맞는 타입** 으로 넘겨야 한다 (`%d` 에 `long long` 을 넘기면 UB) — 그래서 주어진 `fmt_int` 가 `len` 으로 갈라 캐스트한다. 문자열은 링에 NUL 이 없으니 임시 버퍼에 복사하고 NUL 을 붙인다.
</details>

<details>
<summary>힌트 5 — DROP 마커는 어디에?</summary>

drop 은 "자리가 없을 때" 생기니 그 순간엔 마커도 못 쓴다. 생산자 전용 `pending_drops` 에 쌓아 두고, **다음 호출에서 레코드보다 먼저** 마커를 예약한다. 마커조차 안 들어가면 이번 레코드도 drop (pending 증가). 이러면 출력 순서상 정확히 빠진 자리에 "dropped N" 이 찍힌다.
</details>

<details>
<summary>힌트 6 — 디코더는 입력을 믿지 않는다</summary>

프레임에 길이가 없으니 경계를 사전으로 계산한다: 스칼라 8 B, 문자열은 `u16` 을 먼저 읽고 그만큼. 매번 "남은 바이트 ≥ 필요한 바이트" 를 확인한 **뒤에** 읽는다. 문자열 길이가 200 을 넘으면 손상으로 본다. 해시는 사전 내용(포맷·레벨·종류)으로 만든다 — 다른 빌드의 사전으로 디코드하면 쓰레기가 아니라 에러가 나와야 한다.
</details>

## 5. 풀이

### Part A — 레코드 형식과 링

**레코드 형식.** 시작은 항상 8 B 정렬, 크기는 8 의 배수.

| 바이트 | 필드 | 설명 |
|---|---|---|
| 0–3 | `size` | 헤더 + 인자 + 패딩. 소비자는 이것만 보고 다음 레코드로 간다 |
| 4–5 | `site` | 사이트 id, 또는 `PAD_SITE` 0xFFFF / `DROP_SITE` 0xFFFD |
| 6–7 | `argbytes` | 패딩 뺀 인자 바이트 (바이너리 프레임에 필요) |
| 8–15 | `ts` | 호출 시각 (원시 카운터). 패드 레코드에는 없음 → 패드는 8 B 만 쓰고 읽는다 |
| 16– | 인자 | I64/U64/F64/PTR = 8 B, 문자열 = u16 길이 + 바이트 |

`NLOG(lg, INF, "%d %f %s", 1, 2.0, "abc")` = 16 + 8 + 8 + (2 + 3) = 37 → **40 B** (검사함). 정적인 것(포맷 25 B 남짓, 파일 이름, 줄, 레벨, 인자 종류 3 개)은 한 바이트도 링에 들어가지 않는다 — 이게 NanoLog 의 핵심이다.

**함수 내부 `static` = 한 번만 등록.** `NLOG` 는 이렇게 펼쳐진다:

```cpp
if (false) std::printf(fmt, args...);                 // 실행 안 됨. 컴파일러의 -Wformat 검사만 받는다
if ((lvl) >= (lg).min_level) {                        // 레벨이 낮으면 인자 평가도 안 한다
    static const std::uint16_t nlog_site_ =           // [stmt.dcl]: 첫 통과 때 한 번, 스레드 안전 초기화
        register_site(fmt, __FILE__, __LINE__, lvl, decltype(kinds_of(args...)){});   // decltype = 평가 안 함
    (lg).log(nlog_site_, args...);
}
```

`if (false) printf` 덕분에 포맷/인자 불일치는 **컴파일 에러** 다 (`-Werror`). 실제로 넣어 본 결과:

```text
fmtchk.cpp:4:44: error: format specifies type 'int' but the argument has type 'double' [-Werror,-Wformat]
fmtchk.cpp:4:72: error: cannot pass non-trivial object of type 'std::string' (aka 'basic_string<char>') to variadic function; expected type from format string was 'char *' [-Wnon-pod-varargs]
```

두 번째 줄이 이 설계의 제약이다: `NLOG` 에는 `std::string` 을 못 넘긴다 (`.c_str()` 필요). NanoLog 도 C 문자열만 받는다. `register_site` 의 런타임 검사(개수·종류, '*' 폭 거절)는 디코더가 바이트 경계를 잘못 계산하지 않게 하는 두 번째 방어선이다.

**hot path 가 실제로 무슨 명령인가** (`demo_asm.cpp`, `xcrun llvm-objdump -d`, Apple clang 21, arm64 — 실제 출력에서 골라 옮김):

| 하는 일 | 본 명령 | 의미 |
|---|---|---|
| 레벨 비교 | `ldrb w8, [x0]` · `cmp w8, #0x1` · `b.hi` | `min_level` 바이트 하나 |
| 함수 내부 static 가드 | `ldaprb w8, [x8]` · `tbz w8, #0x0, …` | 가드 바이트 **acquire** load (RCpc `ldapr`) — 등록한 스레드의 사이트 쓰기가 보이게 |
| 시계 | `ldr x8, [x20, #0x30]` · `blr x8` → `mrs x0, CNTVCT_EL0` | 설정의 함수 포인터를 부른다 (템플릿 인자로 바꾸면 인라인 가능) |
| `thread_local` 캐시 | `ldr x0, [x0]` · `ldr x8, [x0]` · `blr x8` (이 함수에 3 번) | Mach-O TLV: 디스크립터의 접근 함수를 **간접 호출** |
| 레코드 쓰기 | `str w23, [x0]` · `strh w24, [x0, #0x4]` · `stp x21, x8, [x0, #0x8]` · `str d8, [x0, #0x18]` | `memcpy` 가 다 보통 store 로 펴졌다 |
| commit | `stlr x8, [x22]` | `tail` release store |
| reserve 안: 내 tail | `ldr x8, [x19]` | relaxed = 보통 load |
| reserve 안: 진짜 head | `ldapr x12, [x12]` | acquire = `ldapr` (clang 이 이 타깃(FEAT_LRCPC)에선 `ldar` 대신 RCpc load 를 낸다; ARM-M05 의 RCsc/RCpc) |

`thread_local` 이 함수 호출이라는 건 맥 특유다 (Linux ELF 의 initial-exec TLS 는 `mrs tpidr_el0` + 오프셋이 보통 — 이 맥에서 확인한 것은 아님). 그래서 "링 포인터를 thread_local 에 캐시" 하는 것조차 이 맥에선 간접 호출 몇 번이다 (따로 재지는 않았다 — 전체 평균 9–12 ns 안에 들어 있다). 줄이려면 thread_local 접근을 함수당 한 번으로 묶거나, 호출자가 링 핸들을 들고 다니게 한다.

**memory_order 표.**

| 접근 | 누가 | order | 왜 |
|---|---|---|---|
| `tail.load` (reserve) | 생산자 | relaxed | 내가 쓴 변수 |
| `head_cache` 로 판단 | 생산자 | (보통 변수) | 옛 값은 "덜 비었다" 고만 말한다 → 안전한 쪽 |
| `head.load` (모자랄 때만) | 생산자 | **acquire** | 소비자가 그 칸을 **다 읽은 뒤에** 덮어쓴다 |
| 레코드·패드 바이트 쓰기 | 생산자 | 보통 store | 아래 release 앞에 있으면 된다 |
| `tail.store` (commit) | 생산자 | **release** | 바이트 → 그 다음 공개 |
| `tail.load` (consume) | 소비자 | **acquire** | 공개된 바이트가 보인다 |
| `head.load` (consume) | 소비자 | relaxed | 내가 쓴 변수 |
| `head.store` (consume 끝) | 소비자 | **release** | 읽기 끝 → 칸 반환 |
| `dropped`/`stalls`/`pads` | 생산자 쓰기, 아무나 읽기 | relaxed | 통계. 다른 데이터를 공개하지 않는다 |

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">happens-before 두 갈래: 쓰기 → 읽기 (tail), 읽기 → 덮어쓰기 (head)</text>
<text x="40" y="48" fill="currentColor">생산자</text>
<text x="520" y="48" fill="currentColor">소비자</text>
<rect x="20" y="60" width="250" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="30" y="80" fill="currentColor">레코드 N 바이트 쓰기 (보통 store)</text>
<rect x="20" y="104" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="30" y="124" fill="currentColor" font-family="monospace">tail.store(t+n, release) stlr</text>
<rect x="470" y="104" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="480" y="124" fill="currentColor" font-family="monospace">tail.load(acquire) ldapr</text>
<rect x="470" y="148" width="250" height="30" rx="4" class="f-ok" stroke="currentColor"/><text x="480" y="168" fill="currentColor">레코드 N 읽고 포맷팅</text>
<rect x="470" y="192" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="480" y="212" fill="currentColor" font-family="monospace">head.store(h, release) stlr</text>
<rect x="20" y="192" width="250" height="30" rx="4" class="f-acc" stroke="currentColor"/><text x="30" y="212" fill="currentColor" font-family="monospace">head.load(acquire) ldapr</text>
<rect x="20" y="236" width="250" height="30" rx="4" class="f-warn" stroke="currentColor"/><text x="30" y="256" fill="currentColor">같은 칸에 레코드 N+k 덮어쓰기</text>
<path d="M270 119 L470 119" stroke="currentColor" fill="none"/><path d="M462 114 L470 119 L462 124" stroke="currentColor" fill="none"/>
<text x="300" y="112" fill="currentColor" font-size="12">synchronizes-with ①</text>
<path d="M470 207 L270 207" stroke="currentColor" fill="none"/><path d="M278 202 L270 207 L278 212" stroke="currentColor" fill="none"/>
<text x="300" y="200" fill="currentColor" font-size="12">synchronizes-with ②</text>
<path d="M145 90 L145 104 M595 134 L595 148 M595 178 L595 192 M145 222 L145 236" stroke="currentColor" fill="none"/>
<text x="10" y="290" fill="currentColor" font-size="12">① 없으면: 반쯤 쓴 레코드를 읽는다. ② 없으면: 아직 읽는 바이트를 덮어쓴다 (WAR).</text>
</svg>
```

**포맷팅 = 조각 + snprintf.** 소비자는 `"[ts] L "` 를 붙인 뒤 조각을 돈다. 정수는 8 B 로 저장했지만 printf 의 가변 인자는 명세가 말하는 타입이어야 한다 (C11 7.21.6.1/9: 타입이 맞지 않으면 UB). 그래서 `%d`/`%hhu` → `int`/`unsigned`, `%ld` → `long`, `%lld`(`PRId64` 는 이 맥에서 `"lld"`) → `long long`, `%zu` → `size_t` 로 캐스트한다. `%hhu` 에 300 을 주면 printf 가 `unsigned char` 로 바꿔 `44` 를 찍는다 — 기대값에 넣어 두었다. `%p` 는 구현 정의 출력이라 기대값을 같은 `snprintf("%p")` 로 만든다.

**감싸기.** 256 B 링, 32 B × 7 = 224 다음에 64 B: `off 224 + 64 > 256` → 32 B 패드 + 0 에 64 B, `tail = 224 + 32 + 64`. 50 개 혼합(32 B/40 B) 시나리오에서는 패드가 정확히 3 번 생긴다 (결정적이라 `== 3` 으로 검사).

### Part B — 가득 찼을 때

**DROP 의 정확한 수.** 1024 B, 레코드 24 B: 42 × 24 = 1008. 43 번째는 `off 1008 + 24 > 1024` → 패드 16 + 24 = 40 이 필요한데 남은 건 16 → 실패. 소비자가 없으니 이후 57 개도 전부 실패 → **58**. 비운 다음 첫 호출은 마커(24 B)를 먼저 쓰고 자기 레코드를 쓴다. 실제 출력 꼬리:

```text
[42] I n=41
[101] W nlog: dropped 58 messages
[101] I after 1
[102] I after 2
```

`reserve(24)` 를 단계별로 따라가면 (cap 1024, 소비자 없음 → head 0):

| 호출 | t (tail) | off = t & 1023 | pad | need | t + need − head_cache | 결과 |
|---|---|---|---|---|---|---|
| 1 번째 | 0 | 0 | 0 | 24 | 24 | 성공, pending 24 |
| 42 번째 | 984 | 984 | 0 | 24 | 1008 | 성공 → tail 1008 |
| 43 번째 | 1008 | 1008 | 16 | 40 | 1048 > 1024 → head 재확인 = 0 → 1048 | 실패 → `pending_drops` 1 |
| 100 번째 | 1008 | 1008 | 16 | 40 | 1048 (head_cache 0) → 재확인 0 | 실패 → 58 |
| poll 뒤 101 번째 (마커) | 1008 | 1008 | 16 | 40 | 1048 > 1024 → 재확인 = 1008 → 40 | 성공: 패드 16 + 마커 at 0 |
| 101 번째 (레코드) | 1048 | 24 | 0 | 24 | 1072 − 1008 = 64 | 성공 |

head 를 다시 읽는 건 "캐시로 보니 모자랄 때" 뿐이다 — 정상 상태에선 링 한 바퀴에 한 번 정도.

마커의 ts 가 101 인 이유: 시계는 **호출 시작** 에 읽는다 (drop 된 호출도 시계를 읽었으니 1..100 이 지나갔다). 막혀 있던 시간이 ts 에 섞이지 않게 하려는 선택이다.

**BLOCK 과 상한.** NanoLog 는 버퍼가 차면 생산자를 기다리게 한다 (논문/코드 기준, 미확인). spdlog 비동기 로거는 `async_overflow_policy::{block, overrun_oldest, discard_new}` 를 고르게 한다 (v1.17.0 `async_logger.h` 에서 확인). 이 레슨은 BLOCK 에 `block_timeout` 을 붙였다: 소비자 스레드가 죽거나 멈추면 로거가 시스템 전체를 멈추는 것보다 로그를 잃는 게 낫다 (watchdog 이 있는 펌웨어라면 더욱). 강제 검사에서 소비자 없이 생산자를 돌리면 `stalls == 1` 인 순간 `tail == 1008` — 정확히 42 개 뒤에 멈춰 있다. 이건 타이밍이 아니라 **불변식** 이라 매번 성립한다 (올바른 BLOCK 은 소비자가 움직이기 전엔 절대 더 못 쓴다).

**문자열 수명.** "포인터만 저장하면 더 빠르지 않나?" — 소비자는 µs~ms 뒤에 그 주소를 읽는다. 호출자의 스택 버퍼는 이미 다른 값이고(검사: `SECOND`), 힙 문자열은 이미 해제됐을 수 있다. `demo_lifetime.cpp` 실제 ASan 출력 (일부):

```text
==19868==ERROR: AddressSanitizer: heap-use-after-free on address 0x604000000410 at pc 0x000104ccee08 ...
READ of size 2 at 0x604000000410 thread T0
    #2 0x000104ccfb5c in snprintf+0x44 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x1fb5c)
    #3 0x0001045757e0 in PtrLogger::drain() solution.cpp:512
    #4 0x0001045750bc in main demo_lifetime.cpp:19
freed by thread T0 here:
    #1 0x0001045750a0 in main demo_lifetime.cpp:18
previously allocated by thread T0 here:
    #3 0x0001045754e0 in make_name(int) demo_lifetime.cpp:9
SUMMARY: AddressSanitizer: heap-use-after-free solution.cpp:512 in PtrLogger::drain()
```

<!-- code: demo_lifetime.cpp -->

`./life ptr` 은 exit 134 (ASan abort), `./life copy` (바이트 복사판)은 `[…] I name=sensor-7-with-a-long-name` 을 찍고 깨끗하다. 포인터를 저장해도 되는 건 **문자열 리터럴** 뿐인데, 함수 인자 `const char*` 만 보고는 리터럴인지 알 수 없다 (NanoLog 도 `%s` 는 복사한다 — 문서 기준, 미확인). 비용: 문자열 바이트만큼 memcpy + 길이 2 B — 그래서 문자열을 200 B 에서 자른다.

**스레드별 링과 thread id 재사용 (실제로 밟은 함정).** 처음엔 느린 경로가 "`std::this_thread::get_id()` 가 같은 링이 있으면 재사용" 이었다. 연습 2 에서 스레드 A 를 join 한 뒤 B 를 만들자 링이 하나뿐이었다 — 이 맥은 끝난 스레드의 id 를 바로 다시 준다 (`0x16dc23000 0x16dc23000 same=1`, 두 번 실행 모두). 그래서 지금은 **thread_local 목록** (로거 uid → 링)으로만 찾는다. 로거 uid 는 전역 카운터라 같은 주소에 새 로거가 생겨도 옛 캐시가 맞지 않는다.

**백그라운드 스트레스.** 4 KB 링 × 4 생산자 × 20000, BLOCK: 80000 줄, 순서 위반 0, drop 0, stall 은 실행마다 다르다 (한 실행 626 번 — 출력만). 링 안은 FIFO 라 스레드별 순서가 보장되고, **스레드 사이** 순서는 소비자가 링을 도는 순서일 뿐이다 → 시간순이 필요하면 ts 로 병합 (연습 2).

**다른 로거와 비교** (이 레슨 외에는 문서·소스 기준 요약, NanoLog/Trice 세부는 미검증):

| | hot path 가 하는 일 | 버퍼 | 가득 참 | 출력 |
|---|---|---|---|---|
| `fprintf` | 포맷 + FILE 잠금 + 가끔 `write(2)` | stdio 4 KB | (해당 없음, 호출자가 기다림) | 텍스트 |
| spdlog 동기 | fmt 포맷 + 패턴 포맷 + 싱크 쓰기 | 싱크 버퍼 | — | 텍스트 |
| spdlog 비동기 | fmt 포맷 + mutex 큐 넣기 (+ notify) | MPMC 큐 (기본 8192 칸) | block / overrun_oldest / discard_new | 텍스트 (작업 스레드) |
| NanoLog | id + rdtsc + 원시 인자 → 스레드별 링 | 스레드별 staging buffer | 생산자 대기 | 압축 바이너리 → 오프라인 디코더 |
| 이 레슨 nlog | id + `cntvct_el0` + 원시 인자 → 스레드별 링 | 스레드별 SPSC 바이트 링 | DROP(마커) / BLOCK(상한) | 텍스트 또는 바이너리 + 디코더 |
| Trice / defmt (펌웨어) | id + (선택) 타임스탬프 + 인자 → RAM 링 / 전송 | RTT up-buffer, UART FIFO | RTT 는 버림 / 자름 / 대기 중 모드로 고름 | 호스트가 사전으로 디코드 |

### Part C — 바이너리 프레임과 사전

프레임 = `[u16 site][u64 ts][인자]`. 링 레코드에서 `size`·`argbytes`·패딩을 버린 것이고, 인자 표현은 링과 같아서 디코더가 Part A 의 `format_message` 를 그대로 쓴다. 이 맥(arm64)과 Cortex-M 은 둘 다 리틀 엔디언이라 `memcpy` 그대로 썼다 (빅 엔디언 타깃이면 C-M05 처럼 바이트 단위 직렬화).

| 호출 | 프레임 바이트 |
|---|---|
| `"boot"` | 10 |
| `"a=%d b=%u c=%lld d=%x"` (4 스칼라) | 42 |
| `"pi=%.3f tiny=%g sci=%e"` (3 double) | 34 |
| `"s=%s\|%-6s\|%.2s"` ("hello","ab","xyz") | 10 + 7 + 4 + 5 = 26 |
| `"100%% done, ch=%c ..."` (4 스칼라) | 42 |
| `"p=%p"` | 18 |
| 헤더 `"NLG1"` + 해시 | 8 → 합계 **180 B** (텍스트 212 B) |

180 vs 212 — 압축 없이 8 B 원시 값이면 **바이너리가 텍스트보다 별로 작지 않다** (작은 정수 `-5` 는 텍스트로 2 B, 바이너리로 8 B). 진짜 이득은 (1) 포맷 문자열 텍스트가 아예 안 나간다, (2) 타깃 CPU 가 포맷팅을 안 한다, (3) 압축하면 크게 준다 — 연습 1 의 센서 로그는 raw 709 B → varint/zigzag/ts 델타로 **160 B**, 같은 내용의 텍스트는 847 B.

**프레임에 길이가 없는 대가**: 사전 없이는 경계조차 못 찾는다. 사전이 다른 빌드의 것이면 엉뚱한 종류로 읽어 쓰레기가 나온다 → 스트림 헤더에 사전 해시(FNV-1a)를 넣고 디코더가 비교한다 (검사: 포맷 하나에 "!" 를 붙인 사전 → -1). 펌웨어에서는 이 해시가 빌드 id 역할을 한다. 디코더는 바이트를 믿지 않는다: 프레임마다 "남은 바이트 ≥ 필요" 를 먼저 보고, 문자열 길이 > 200 이면 손상 → -2 (검사: 길이를 0xFFFF 로 깨도 ASan 깨끗).

**펌웨어판 hot path** (`demo_cortexm.c`, `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -mthumb -O2 -S`, 실제 출력 일부): 10 B 레코드 `{u16 id, u32 DWT_CYCCNT, u32 arg}` 를 바이트 링에.

```text
log_id1:
	ldr.w	r3, [r12]          @ tail (relaxed)
	ldr	r2, [r2]           @ head
	dmb	sy                 @ ← acquire load 뒤의 배리어
	...
	movw	r2, #4100
	movt	r2, #57344         @ 0xE0001004 = DWT->CYCCNT
	ldr	r2, [r2]
	strb.w	r0, [lr, r4]       @ 바이트 10 개 (끝에서 쪼개짐 허용, DS-M10)
	...
	dmb	sy                 @ ← release store 앞의 배리어
	str.w	r0, [r12]          @ tail = t + 10
```

<!-- code: demo_cortexm.c -->

- Cortex-M4 에는 `ldar/stlr` 가 없어서 acquire/release 가 `ldr; dmb sy` / `dmb sy; str` 이 됐다. 단일 코어에서 ISR ↔ main 만 맞추면 되는 경우엔 CPU 배리어 대신 컴파일러 배리어로 충분하다는 논의는 C-A02 / CPP-A04 에 있다 (여기서 다시 재지 않음).
- 처음 버전은 소비자 함수가 같은 파일에 없었더니 컴파일러가 **링 쓰기와 head load 를 통째로 지웠다** (static 인데 아무도 안 읽음 → 죽은 store, head 는 한 번도 안 쓰임 → 0). 데모에 `log_drain` 을 넣은 이유다. 실제 펌웨어에서 링을 디버거(RTT)만 읽는다면 버퍼를 `volatile` 이나 외부 링크로 둬야 같은 일이 안 생긴다.
- 생산자가 여럿(main + 여러 ISR)이면 SPSC 가 아니다. 단일 코어에서 가장 짧은 해법은 예약~commit 동안 `PRIMASK` 로 인터럽트를 막는 것 (`log_id1_any`: `mrs r1, primask` · `cpsid i` … `msr primask, r1` 을 실제 asm 에서 확인). 우선순위별 링을 두는 방법도 있다.
- 실제 도구 (세부는 문서 기준, 미검증): **Trice** — 소스의 `TRICE(...)` 에 도구가 id 를 넣고, 호스트의 id 목록 파일로 디코드, UART/RTT 로 전송. **SEGGER RTT** — 타깃 RAM 의 링 버퍼(up-buffer)를 J-Link 가 CPU 를 멈추지 않고 디버그 포트로 읽는다. **ARM ITM** — stimulus 포트 레지스터에 store 하면 SWO 핀으로 나간다 (M3/M4/M7, M0/M0+ 에는 없음). **defmt** (Rust) — 포맷 문자열을 ELF 의 플래시에 올리지 않는 섹션에 두고 인덱스만 보낸다. **Zephyr** 사전 기반 로깅도 같은 생각.
- 플래시 절약: 포맷 문자열(이 레슨 테스트의 사이트 20 개만 해도 수백 B)과 printf 구현(부동소수점 포함 newlib printf 는 보통 수 KB–수십 KB, 구현·옵션에 따라 다름 — 이 맥에서 측정 안 함)이 타깃에서 빠진다.

### 측정 — 호출당 지연과 버스트

기계: Apple M6 (Super 2 + Performance 4 + Efficiency 6), `Apple clang version 21.0.0 (clang-2100.0.123.102)`, `-O2`, spdlog v1.17.0 (`git clone --depth 1 --branch v1.17.0`, 헤더 전용, 설치 안 함). **같은 시간에 다른 레슨 작성자 여럿이 벤치를 돌리고 있었다 (load average 5.96, 앞선 실행은 8.5–9.0)** → 절대 ns 가 아니라 **배수** 를 믿을 것. 코어 고정 불가 (QoS 힌트뿐).

시계: `cntvct_el0` 는 `cntfrq_el0 = 1000000000` 으로 보고되지만 실제 값은 평균 42.15 ns 마다 바뀐다 (1천만 번 읽어 67609 번 변함 — 24 MHz 원천). `mach_absolute_time` 도 timebase 125/3 (= 41.67 ns). 그래서 한 호출 백분위(p50…max)는 **~42 ns 눈금** 이다: nlog 의 p50 = 0 은 "42 ns 미만" 이란 뜻. 평균은 시계 없이 돈 루프 전체 시간 / 호출 수라 눈금 영향이 없다. 메시지 = `"order id=%d px=%.2f qty=%u sym=%s"` (+ fprintf/snprintf 는 같은 텍스트가 나오게 `[ts] I ` 접두사; nlog 와 fprintf 출력 파일이 둘 다 24942920 B 로 같았다). 라운드당 20000 호출 × 10, 라운드 사이에 소비자가 다 비운 뒤 (= 버퍼가 차지 않은 정상 상태).

<!-- code: demo_bench.cpp -->

호출당 지연 (16:25, load 5.96 — 그때 3 회 연속 실행 중 가운데. 괄호 = 3 회 범위. 앞서 load 8.5–9.0 에서 돌린 3 회도 배수는 같았다):

| 로거 | 평균 ns | p50 | p99 | p99.9 | max |
|---|---|---|---|---|---|
| nlog (이 레슨) | **10.3** (9.2–11.9) | 0 | 42 | 42 (42–42) | 6166 |
| `snprintf` → 스택 버퍼 | 145.9 (139–147) | 125 | 208 | 250 (209–375) | 13042 |
| `fprintf` → 파일 | 214.0 (187–214) | 125 | 2834 | 5041 (3834–5667) | 378500 |
| spdlog 동기 파일 (`_st`) | 181.9 (164–182) | 125 | 2958 | 4500 (3541–5125) | 652000 |
| spdlog 비동기 (큐 65536, block) | 232.4 (204–247) | 125 | 2833 | 6458 (5250–6458) | 34374 |

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="13">
<text x="10" y="20" fill="currentColor" font-weight="bold">호출당 평균 지연 (ns, 같은 메시지, load 5.96) — 막대 끝 숫자 = p99</text>
<text x="10" y="56" fill="currentColor">nlog</text>
<rect x="190" y="42" width="11" height="22" class="f-ok" stroke="currentColor"/><text x="208" y="58" fill="currentColor">10.3 ns · p99 42</text>
<text x="10" y="96" fill="currentColor">snprintf → 스택</text>
<rect x="190" y="82" width="160" height="22" class="f-mut" stroke="currentColor"/><text x="358" y="98" fill="currentColor">146 · p99 208</text>
<text x="10" y="136" fill="currentColor">fprintf → 파일</text>
<rect x="190" y="122" width="235" height="22" class="f-warn" stroke="currentColor"/><text x="433" y="138" fill="currentColor">214 · p99 2834</text>
<text x="10" y="176" fill="currentColor">spdlog 동기</text>
<rect x="190" y="162" width="200" height="22" class="f-warn" stroke="currentColor"/><text x="398" y="178" fill="currentColor">182 · p99 2958</text>
<text x="10" y="216" fill="currentColor">spdlog 비동기</text>
<rect x="190" y="202" width="256" height="22" class="f-warn" stroke="currentColor"/><text x="454" y="218" fill="currentColor">232 · p99 2833</text>
<path d="M190 34 L190 232" stroke="currentColor" fill="none"/>
<text x="10" y="252" fill="currentColor" font-size="12">척도: 1 ns = 1.1 px. 평균은 nlog 가 14–23 배 빠르고, p99 는 I/O·잠금이 hot path 에 있는 쪽이 µs 로 튄다.</text>
</svg>
```

읽는 법:
- **nlog vs snprintf ~14×**: snprintf 는 I/O 가 없는데도 ~146 ns — 비용 대부분이 `%.2f` 같은 **포맷팅 자체** 다. nlog 는 그걸 백그라운드로 옮겼을 뿐 없앤 게 아니다 (아래 버스트 참고).
- **fprintf p99 ~3 µs**: 이 맥의 파일 FILE 버퍼는 4096 B (`_bf._size=4096`, `st_blksize 4096`). 줄 ~62 B 면 ~66 줄마다 `write(2)` 한 번 = 호출의 ~1.5% → p99 에 시스템 호출이 걸린다.
- **spdlog 동기(포맷 + 패턴 + 파일)가 snprintf(포맷만)보다 평균 ~20% 느릴 뿐**: fmt 의 포맷팅이 libc printf 보다 빠르다는 뜻으로 보인다 (추정, 원인 분리 안 함).
- **spdlog 비동기도 p50 ~125 ns**: v1.17.0 소스(`logger.h` `log_()`)를 보면 호출 스레드에서 `fmt::vformat_to` 로 메시지를 **먼저 포맷** 하고, `mpmc_blocking_q` 의 `std::mutex` + condvar 로 넣는다. 비동기라도 포맷 비용과 잠금은 hot path 에 남는다 — NanoLog 와의 결정적 차이. p99 의 µs 는 잠든 작업 스레드를 깨우는 condvar notify(커널 호출)로 보인다 (추정).

버스트: 한 스레드가 100 만 개를 쉬지 않고, 링 1 MB (레코드 48 B → ~21.8k 개), spdlog 큐 8192 (16:25, load 5.96; 괄호 = 3 회 범위):

| 설정 | 생산자 ms | Mmsg/s | dropped | 다 쓸 때까지 ms | p50 | p99.9 | max |
|---|---|---|---|---|---|---|---|
| nlog DROP, 텍스트 | 6.5 | 154.4 (154–178) | 953935 (95%) | 11.8 | 0 | 42 | 24209 |
| nlog BLOCK, 텍스트 | 258.5 | 3.9 (3.9–4.3) | 0 | 263.7 | 0 | 66001 | 8526709 |
| nlog DROP, 바이너리 | 6.6 | 152.4 (129–152) | 716902 (72%) | 7.4 | 0 | 42 | 8417 |
| nlog BLOCK, 바이너리 | 20.7 | 48.3 (48.3–50.3) | 0 | 21.2 | 0 | 2042 | 1366667 |
| spdlog 비동기 block | 291.8 | 3.4 (3.4–4.5) | 0 | 292.8 | 125 | 9083 | 2695876 |
| spdlog 비동기 discard_new | 205.5 | 4.9 (4.9–5.3) | 519984 | 206.5 | 84 | 5208 | 1785666 |

- 지속 처리량은 **소비자** 가 정한다: 텍스트로 포맷하는 소비자는 ~4 M msg/s, 바이너리를 그대로 쌓는 소비자는 ~48–50 M msg/s. 생산자는 ~150 M msg/s 로 넣을 수 있으니 버스트가 링보다 크면 DROP 은 95% 를 버리고, BLOCK 은 소비자 속도로 떨어지며 p99.9 가 60–66 µs, max 가 ms 단위가 된다. **"평균 10 ns" 는 버퍼가 남아 있을 때만의 이야기** 다. 그래서 NanoLog 는 런타임에 포맷하지 않고 압축 바이너리만 쓰고 오프라인 디코더로 텍스트를 만든다.
- spdlog `discard_new` 는 절반 넘게 버리면서도 생산자가 ~5 M/s 에 그친다 — 버려질 메시지도 호출 스레드에서 이미 포맷했기 때문.
- 링 크기 = 버스트 흡수량: 1 MB / 48 B ≈ 2.18 만 개. 필요한 크기는 "버스트 길이 × (생산 속도 − 소비 속도)/생산 속도" 로 어림한다.
- NanoLog 논문의 숫자(초록 기준, 원문 대조 안 함): 마이크로벤치 호출당 ~8 ns, 최대 ~80 M msg/s, 기존 로거보다 1–2 자릿수 빠름. 이 맥의 nlog 9–14 ns 는 같은 자릿수다.

## 6. 정답 코드

<!-- code: solution.cpp diff=starter.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread solution.cpp -o so && ./so
# -- Part B: ... --
#   [stress] 4 x 20000 msgs, ring 4 KB: lines 80000, stalls 626 (출력만)
#   [size] binary 180 B vs text 212 B (사전 20 사이트는 호스트에만)
# 40/40 passed          (8 회 연속 실행 모두 40/40, 약 3 초 — 대부분 20 ms timeout 검사와 스트레스)
c++ -std=c++20 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer -pthread solution.cpp -o san && ./san   # 40/40, 깨끗
c++ -std=c++20 -g -O1 -fsanitize=thread -pthread solution.cpp -o tsan && ./tsan                                     # 40/40, 경고 0
```

새니타이저 빌드는 `__has_feature(address_sanitizer / thread_sanitizer)` 로 스트레스 양을 1/10 (4 × 2000) 로 줄인다 (20 초 규칙). 줄이지 않은 TSan 빌드(4 × 20000)도 따로 돌려 경고 0 (2.8 초). 링 바이트는 보통 메모리지만 생산자의 쓰기 → `tail` release → 소비자 acquire 로만 공유되므로 TSan 이 순서를 이해한다 (독립 fence 없음 → MCPP-M13 의 오탐 문제 없음).

## 7. 자주 하는 실수

1. **`head` 를 relaxed 로 읽는다.** → 생산자는 "칸이 비었다" 는 사실만 보고, 소비자가 그 칸을 다 읽었다는 사실은 못 본다 → 약한 메모리에서 읽는 중인 바이트를 덮어쓴다. 고침: 모자랄 때 읽는 진짜 `head` 는 acquire, 소비자는 다 읽은 뒤 release store.
2. **commit 전에 tail 을 옮기거나, 패드를 commit 뒤에 쓴다.** → 소비자가 쓰레기 `size` 를 보고 엉뚱한 곳으로 뛴다. 고침: 패드·헤더·인자 전부 쓴 다음 `tail.store(pending, release)` 한 번.
3. **문자열 인자를 `const char*` 로 저장.** → 소비 시점의 내용(또는 해제된 메모리)을 찍는다. 고침: 바이트 복사 + 길이 상한 (`demo_lifetime` 의 UAF).
4. **printf 에 저장된 8 B 를 그대로 넘긴다** (`snprintf("%d", int64)`). → 가변 인자 타입 불일치 = UB, arm64 Apple ABI 에선 가변 인자가 스택 8 B 슬롯이라 우연히 맞아 보일 수 있다. 고침: 명세의 길이 수식어로 캐스트 (`fmt_int`).
5. **링을 thread id 로 찾는다.** → 끝난 스레드의 id 가 재사용되어 다른 스레드가 남의 링을 이어 쓴다 (이 맥에서 실제로 같은 id). 고침: thread_local 목록 + 로거 uid.
6. **BLOCK 에 상한이 없다.** → 소비자 스레드가 멈추면(디스크 꽉 참, 디버거) 로그를 부르는 모든 스레드가 멈춘다. 고침: `block_timeout` 후 drop 으로 바꾸고 `timeouts` 를 센다.
7. **drop 을 숫자로만 센다.** → 출력에 구멍이 어디 있는지 모른다. 고침: 다음 자리에 DROP 마커 레코드.
8. **`n > cap/2` 레코드를 허용.** → 오프셋에 따라 빈 링에도 안 들어가 BLOCK 이 영원히(또는 timeout 까지) 기다린다. 고침: 생성 시/호출 시 크기 상한 → `oversize` 로 거절.
9. **레벨 필터를 `log()` 안에서.** → 걸러질 메시지도 인자를 평가하고 함수를 부른다. 고침: 매크로에서 먼저 비교 (검사: `evals == 1`).

## 8. 면접 Q&A

<details>
<summary>Q1. NanoLog 가 printf 류보다 10 배 넘게 빠른 이유를 한 문장씩.</summary>

(1) 포맷 문자열·파일·줄·레벨·인자 타입은 컴파일/첫 호출 때 한 번만 처리하고 id 로 바꾼다. (2) hot path 는 시각 + 원시 인자 바이트만 스레드 전용 링에 memcpy — 잠금·시스템 호출·포맷팅 없음. (3) 포맷팅은 백그라운드 스레드(NanoLog 는 아예 오프라인 디코더)가 한다. 이 맥에서 같은 메시지로 nlog 9–12 ns vs snprintf 139–147 ns vs fprintf 187–214 ns (load ~6).
</details>

<details>
<summary>Q2. 링의 각 인덱스 접근에 필요한 memory_order 는? 하나만 틀리면 무슨 일이?</summary>

생산자: 자기 `tail` relaxed load, 모자랄 때 `head` acquire load, 바이트 쓰기, `tail` release store. 소비자: 자기 `head` relaxed, `tail` acquire, 읽기, `head` release. `tail` 쪽이 틀리면 반쯤 쓴 레코드를 읽고, `head` 쪽이 틀리면 생산자가 아직 읽히는 바이트를 덮어쓴다. 이 칩에서 acquire 는 `ldapr`, release 는 `stlr`, relaxed 는 보통 `ldr`/`str` 로 나왔다.
</details>

<details>
<summary>Q3. 꼬리질문 — 함수 내부 static 으로 id 를 받으면 hot path 에 비용이 있나?</summary>

있다, 작다. C++11 이후 함수 내부 static 초기화는 스레드 안전해야 해서([stmt.dcl]), 컴파일러는 매 통과마다 가드 바이트를 **acquire** 로 읽는다 — 이 맥에서 `ldaprb` + `tbz`. 첫 통과만 `__cxa_guard_acquire` 로 등록한다. NanoLog 원조는 전처리기로 빌드 때 id 를 박아 이 비용도 없앴다 (C++17 판은 런타임 등록 — 문서 기준, 미확인). 대안: 링커 섹션에 사이트 구조체를 모아 두고(펌웨어의 defmt 처럼) 주소를 id 로 쓰기.
</details>

<details>
<summary>Q4. 버퍼가 가득 차면? DROP 과 BLOCK 중 무엇을 고르겠나?</summary>

어떤 손실이 더 나쁜지로 고른다. 제어 루프·트레이딩 hot path 는 지연이 계약이라 **DROP** (+ 정확한 drop 수와 빠진 자리 마커 — 감사 로그가 아니라면). 감사·규제 로그처럼 한 줄도 잃으면 안 되면 **BLOCK** 인데, 버스트에서 지연 꼬리가 소비자 속도로 무너진다 (이 맥: p99.9 60–66 µs, max ms 단위). 어느 쪽이든 BLOCK 에는 상한을 둔다. 근본 대책은 소비자를 빠르게(바이너리 출력: 텍스트 ~4 → ~48 M msg/s) 하고 링을 버스트 크기에 맞추는 것.
</details>

<details>
<summary>Q5. 꼬리질문 — 여러 스레드가 로그를 남기는데 출력 순서는 어떻게 보장하나?</summary>

스레드마다 SPSC 링이라 **스레드 안** 순서만 FIFO 로 보장된다. 전역 순서는 레코드의 타임스탬프로 병합한다 (연습 2 의 k-way merge). 실시간 병합이라면 "모든 링에서 ts ≤ W 인 것은 다 봤다" 는 워터마크까지만 내보낼 수 있다. 하나의 MPMC 큐를 쓰면 순서는 생기지만 모든 스레드가 같은 캐시 라인을 두고 싸운다 (DS-A03) — 로거가 스레드별 버퍼를 쓰는 이유. 코어 간 시계 차이: 같은 SoC 의 `cntvct_el0` 는 시스템 카운터 하나라 코어끼리 맞다고 보는 게 보통 (Arm ARM 의 generic timer — 이 맥에서 코어 간 비교 측정은 안 함).
</details>

<details>
<summary>Q6. 문자열 인자는 왜 복사해야 하나? 비용을 줄이는 방법은?</summary>

소비자는 나중에 읽는데, 호출자의 버퍼 수명은 호출이 끝나면 보장되지 않는다 (스택 버퍼 재사용, `std::string` 해제 → `demo_lifetime` 의 heap-use-after-free). 리터럴만 포인터로 보내도 되지만 `const char*` 타입으로는 구분이 안 된다. 줄이는 법: 길이 상한(여기 200 B), 자주 쓰는 문자열(상태 이름)은 enum/정수로 바꿔 사전에서 이름을 찾기, 리터럴임을 컴파일 타임에 아는 별도 매크로.
</details>

<details>
<summary>Q7. 펌웨어에서 같은 걸 하면? 플래시·대역폭 이득은 어디서 오나?</summary>

ISR/태스크가 `{id, 사이클 카운터, 원시 인자}` 를 RAM 링에 쓰고, UART/SWO/RTT 로 내보내며, 호스트가 사전(빌드 산출물)으로 디코드 — Trice, SEGGER RTT 위의 로거, ITM stimulus 포트, Rust defmt 가 이 구조다 (세부는 문서 기준). 이득: 포맷 문자열과 printf 구현이 플래시에서 빠지고, ISR 에서 printf 를 부르지 않아 실행 시간이 짧고 예측 가능하다. 주의: 압축 없이 8 B 원시 값이면 대역폭 이득이 작다 (Part C 180 vs 212 B) → varint 압축(연습 1: 709 → 160 B). 사전과 펌웨어 빌드가 어긋나면 해독 불가 → 빌드 해시를 스트림에 넣는다.
</details>

<details>
<summary>Q8. 꼬리질문 — Cortex-M 의 ISR 과 main 이 둘 다 로그를 남기면 SPSC 링이 깨지나?</summary>

깨진다. ISR 이 main 의 reserve 와 commit 사이에 끼어들면 같은 tail 에서 예약해 서로 덮어쓴다. 단일 코어 해법: 예약~commit 구간을 `PRIMASK`(또는 `BASEPRI`) 로 막는다 — 구간이 수십 명령이라 인터럽트 지연이 짧다 (`log_id1_any`). 또는 우선순위별 링, 또는 LDREX/STREX CAS 로 tail 을 예약하는 MPSC (M3 이상; M0 은 exclusive 명령이 없어 인터럽트 금지뿐). 그리고 M4 에서 acquire/release 는 `dmb sy` 로 나온다 — 단일 코어 ISR↔main 에 꼭 필요한지는 C-A02/CPP-A04.
</details>

<details>
<summary>Q9. 측정할 때 이 맥의 타이머 해상도가 문제가 되지 않나?</summary>

된다. `cntvct_el0` 는 1 GHz 단위로 보고되지만 ~42 ns 마다 바뀐다 (24 MHz 원천), `mach_absolute_time` 도 41.67 ns. 그래서 (1) 평균은 시계 없이 N 번 돈 루프 전체 시간으로, (2) 한 호출 백분위는 42 ns 눈금으로 양자화됐다고 밝히고 꼬리(µs 단위)만 의미 있게 본다. `isb; mrs` 는 호출당 ~8.7 ns, `mrs` 만은 파이프라인에서 겹쳐 ~0.2 ns 로 보였다 (재배치 가능성 있음). 사이클 카운터(PMU)는 맥 유저 모드에서 못 읽는다.
</details>

<details>
<summary>Q10. 이 로거의 hot path 는 lock-free 인가, wait-free 인가?</summary>

정의(DS-A03 에서 자세히): lock-free = 누군가 멈춰도 **시스템 전체** 는 유한 단계 안에 진행, wait-free = **각 호출** 이 유한 단계 안에 끝남. DROP 정책의 `log()` 는 소비자가 무엇을 하든(멈춰도) 정해진 단계 안에 성공하거나 drop 으로 끝난다 → 정상 경로는 wait-free (생산자가 하나라 경쟁 CAS 도 없다). 예외: 사이트의 첫 등록과 스레드의 첫 링 생성은 mutex 를 잡는다 (한 번뿐인 콜드 경로) — 엄밀히 말하면 "첫 호출 제외 wait-free". BLOCK 은 소비자가 멈추면 생산자도 멈추므로 **blocking** 이다 (상한을 둬서 "최악 block_timeout" 으로 묶었을 뿐).
</details>

## 9. 추가 연습

### 연습 1 — NanoLog 식 압축: varint + zigzag + ts 델타

바이너리 스트림을 다시 인코딩한다: site 는 varint, ts 는 앞 프레임과의 차이를 zigzag varint (스레드가 섞이면 음수 가능), I64 는 zigzag varint, U64/PTR 은 varint, double 은 8 B 그대로, 문자열은 varint 길이 + 바이트. 복원 결과가 원본과 **바이트 단위로** 같아야 하고, 잘린 압축 스트림은 거절해야 한다.

<details>
<summary>답</summary>

`compress_stream` / `decompress_stream` (practice.cpp). 센서 로그 21 줄(작은 정수 3 개 × 20 + 문자열 2 개): raw 709 B → **160 B** (텍스트 847 B). 압축 경계도 사전으로 찾는다. zigzag `(n << 1) ^ (n >> 63)` 는 -1 → 1, 1 → 2 로 작은 음수를 작은 양수로 바꿔 varint 를 짧게 한다 (`n >> 63` 은 C++20 에서 산술 시프트로 정의). NanoLog 도 백그라운드에서 이런 압축을 한다 (논문 기준).
</details>

### 연습 2 — 스레드별 링 → 시간순 병합

스레드 A 가 ts 1, 4, 5, 9, 스레드 B 가 ts 2, 3, 5, 8 로 쓴다 (호출마다 ts 를 정하는 시계). 링마다 따로 모은 뒤 최소 힙으로 합쳐 ts 순으로, 같은 ts 면 링 번호 순으로 낸다.

<details>
<summary>답</summary>

`poll_merged` — 링 안은 이미 ts 순(FIFO + 호출 시각)이므로 k-way merge 로 O(N log k). 기대 출력 `[1] A1 [2] B2 [3] B3 [4] A4 [5] A5 [5] B5 [8] B8 [9] A9`. 이 연습이 thread id 재사용 버그를 찾아냈다 (A 를 join 한 뒤 만든 B 가 같은 id → 링 1 개). 실시간이라면 워터마크(모든 링의 최신 ts 중 최솟값)까지만 내보낸다.
</details>

### 연습 3 — 호출 지점별 속도 제한

`NLOG_RATELIMIT(lg, lvl, K, W, fmt, ...)`: 창 W 틱마다 처음 K 개만 기록, 새 창의 첫 줄 앞에 `suppressed N`. 1..30 틱에 한 번씩, K = 3, W = 10.

<details>
<summary>답</summary>

상태 `{start, count, suppressed}` 를 매크로 안 `thread_local` 로 → 호출 지점별·스레드별, 경쟁 없음. 출력은 정확히 11 줄: 창마다 3 줄 + 두 번째·세 번째 창 앞의 `suppressed 7`. 고장 난 센서가 ISR 에서 초당 수만 번 같은 경고를 낼 때 링을 지키는 방법이다 (Linux `printk_ratelimited` 와 같은 생각).
</details>

### 연습 4 — `flush_sync`: 크래시 직전 동기 비우기

치명 오류 직전에 "지금까지 이 스레드가 남긴 로그가 전부 소비자를 통과했다" 를 기다리는 함수. 백그라운드 소비자가 도는 상태에서 500 줄 + 마지막 `fatal` 한 줄 → 돌아온 직후 텍스트 끝이 그 줄이어야 한다.

<details>
<summary>답</summary>

내 링의 `tail` 을 찍고(relaxed — 내 변수), `head` 를 **acquire** 로 읽으며 그 이상이 될 때까지 기다린다 (상한 있음). 소비자는 레코드를 싱크에 넘긴 **다음** `head` 를 release 로 쓰므로, 기다림이 끝나면 그 텍스트가 보인다 (TSan 깨끗). 진짜 크래시 핸들러(시그널/HardFault)에서는 소비자 스레드를 믿을 수 없으니 핸들러가 링을 직접 비우거나(링은 그냥 메모리) 펌웨어처럼 noinit RAM 에 남겨 재부팅 후 읽는다 (DS-M10 Part C).
</details>

### 한 번에 검증

<!-- code: practice.cpp -->

```bash
c++ -std=c++20 -Wall -Wextra -Werror -O2 -pthread practice.cpp -o pr && ./pr   # 13/13 passed (solution.cpp 를 include)
#   raw 709 B -> compressed 160 B (text 847 B)
```

ASan+UBSan, TSan 빌드 모두 13/13, 경고 0.

## 10. 핵심 정리

- 정적인 것(포맷·파일·줄·레벨·인자 종류)은 한 번, 동적인 것(ts·인자 값)만 원시 바이트로 — 포맷팅은 다른 스레드나 다른 기계에서.
- 사이트 id = 함수 내부 static (가드 `ldaprb` 한 번), 포맷 검사 = `if (false) printf(fmt, args...)` 로 컴파일러에게.
- 스레드마다 SPSC 바이트 링: `reserve`(연속 + 패드) → 쓰기 → `tail` release, 소비자 `tail` acquire → 읽기 → `head` release, 생산자는 모자랄 때만 `head` acquire.
- 문자열은 복사한다 — 포인터 저장은 수명 버그 (UAF).
- 버퍼가 차면 DROP(정확한 수 + 마커) 아니면 BLOCK(상한 필수). 평균 10 ns 는 링에 자리가 있을 때만 — 지속 처리량은 소비자(텍스트 ~4 vs 바이너리 ~48 M msg/s)가 정한다.
- 펌웨어판 = Trice/RTT/ITM/defmt: id + 사이클 카운터 + 인자, 호스트 사전으로 디코드, 사전 해시로 빌드 불일치 차단, varint 로 대역폭 절약.
- 측정: 이 맥 시계는 ~42 ns 눈금 → 평균은 루프 전체로, 꼬리만 백분위로. 부하 높을 땐 배수만.

## 참고자료

- Stephen Yang, Seo Jin Park, John Ousterhout, "NanoLog: A Nanosecond Scale Logging System", USENIX ATC 2018 — https://www.usenix.org/conference/atc18/presentation/yang-stephen
- NanoLog 소스 (Stanford PlatformLab) — https://github.com/PlatformLab/NanoLog
- spdlog v1.17.0 — https://github.com/gabime/spdlog (`include/spdlog/logger.h` `log_()`, `details/mpmc_blocking_q.h`, `async_logger.h` 의 `async_overflow_policy`)
- C++ 표준: [stmt.dcl] (함수 내부 static 의 스레드 안전 초기화), [intro.races], [atomics.order]; C11 7.21.6.1 (fprintf 변환 명세와 인자 타입)
- Arm Architecture Reference Manual (DDI 0487) — LDAPR/STLR, generic timer `CNTVCT_EL0`/`CNTFRQ_EL0`
- ARMv7-M Architecture Reference Manual (DDI 0403) — ITM, DWT CYCCNT
- Trice — https://github.com/rokath/trice · SEGGER RTT — https://www.segger.com/products/debug-probes/j-link/technology/about-real-time-transfer/ · defmt — https://defmt.ferrous-systems.com/ · Zephyr 로깅 (dictionary based) — https://docs.zephyrproject.org/latest/services/logging/index.html
- 관련 레슨: DS-M10 (가변 길이 레코드 링), DS-A01 (SPSC 정확성), LLAT-M01 (SPSC 지연 튜닝), DS-A03 (MPMC), C-A03 (release/acquire), C-M14 (ISR 공유), C-A02 / CPP-A04 (Cortex-M 배리어), ARM-M05 (LDAPR vs LDAR)
