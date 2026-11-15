---
id: TOOL-B11
time: 60분
updated: 2026-11-15
---
# TOOL-B11 AddressSanitizer로 메모리 버그 잡기

## 학습 목표

- 패킷 버퍼 모듈에 숨은 메모리 버그 6종(heap / stack / global 오버플로, use-after-free, double-free, 누수)을 `-fsanitize=address` 로 잡고, 리포트 첫 줄만 보고 **버그 종류 · 읽기/쓰기 · 크기 · 소스 줄**을 바로 말할 수 있다.
- 리포트의 세 스택(**접근한 곳 / freed by / previously allocated by**), `located N bytes after M-byte region`, 스택 프레임의 `[32, 48) 'line'`, 섀도 바이트(shadow byte) 덤프와 범례(`fa fd f1 f2 f3 f5 f8 f9 f7`)를 한 줄씩 해석한다.
- 섀도 주소 공식 `Shadow = (Addr >> 3) + Offset` 을 실제 값으로 계산해 보고(맥 arm64 = 런타임이 정하는 동적 오프셋 `0x7000020000`, Linux aarch64 = 고정 `0x1000000000`), 컴파일러가 load 앞에 넣는 검사 명령을 asm 에서 찾는다.
- 맥 ASan 에는 누수 검출(LeakSanitizer)이 없다는 것을 알고, 맥은 `leaks --atExit`, Linux 는 LSan 으로 누수를 확인한다.
- ASan 의 비용(이 맥 실측: 메모리 약 13배, load/store 가 많은 루프 약 3배)과 Cortex-M 펌웨어에 못 쓰는 이유, 대신 펌웨어 모듈을 **호스트 단위 테스트 + ASan** 으로 CI 에서 돌리는 방법을 설명한다.

## 1. 문제

"이 ASan 리포트 읽어 보세요" — C 펌웨어 면접에서 포인터 질문 다음에 자주 나오는 실전 질문이다. 메모리 버그는 MCU 위에서는 대개 **조용히** 다른 변수를 덮거나 몇 시간 뒤 HardFault 로 나타나서 원인 찾기가 가장 비싼 버그다. 그래서 프로토콜 파서 · 패킷 큐 · 링 버퍼 같은 **하드웨어와 무관한 모듈**은 호스트(맥/Linux)에서 단위 테스트로 돌리고, 그때 ASan 을 켜서 "쓰는 순간" 에 잡는다. SSD 컨트롤러 펌웨어라면 FTL 매핑 테이블 · 커맨드 큐 · NVMe 파서가 바로 그런 모듈이다.

AddressSanitizer(ASan) 는 두 부분으로 되어 있다.

1. **컴파일러 계측(instrumentation)**: `-fsanitize=address` 로 빌드하면 모든 load/store 앞에 "이 주소, 접근해도 되나?" 검사 코드가 들어가고, 스택 배열과 전역 배열 주위에 **레드존(redzone)** 을 끼워 넣는다.
2. **런타임 라이브러리**: `malloc/free` 를 가로채(intercept) 블록 양옆에 레드존을 두고, `free` 된 블록은 바로 재사용하지 않고 **격리 구역(quarantine)** 에 잠시 둔다. 접근 가능 여부는 **섀도 메모리(shadow memory)** 에 기록한다 — 앱 메모리 8 바이트마다 섀도 1 바이트.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">malloc(10) 한 블록과 그 섀도 — 실제 리포트 값 (heap 케이스, 맥)</text>
  <text x="10" y="48" fill="currentColor" font-size="12">앱 메모리 (8 바이트 칸)</text>
  <rect x="10" y="58" width="90" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="100" y="58" width="90" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="190" y="58" width="120" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="310" y="58" width="30" height="34" class="f-ok" stroke="currentColor"/>
  <rect x="340" y="58" width="90" height="34" class="f-warn" stroke="currentColor"/>
  <rect x="430" y="58" width="90" height="34" class="f-warn" stroke="currentColor"/>
  <text x="20" y="80" fill="currentColor" font-size="12">레드존</text>
  <text x="110" y="80" fill="currentColor" font-size="12">레드존</text>
  <text x="198" y="80" fill="currentColor" font-size="12" font-family="monospace">s[0..7]</text>
  <text x="313" y="80" fill="currentColor" font-size="12" font-family="monospace">8,9</text>
  <text x="346" y="80" fill="currentColor" font-size="12" font-family="monospace">s[10..15]</text>
  <text x="440" y="80" fill="currentColor" font-size="12">레드존</text>
  <text x="190" y="108" fill="currentColor" font-size="12" font-family="monospace">0x…990</text>
  <text x="310" y="108" fill="currentColor" font-size="12" font-family="monospace">0x…998 (한 칸)</text>
  <path d="M55 92 L55 158" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M145 92 L145 158" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M250 112 L250 158" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M410 112 L410 158" stroke="currentColor" stroke-dasharray="4 3"/>
  <path d="M475 92 L475 158" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="10" y="140" fill="currentColor" font-size="12">섀도 (칸마다 1 바이트)</text>
  <rect x="10" y="160" width="90" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="100" y="160" width="90" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="190" y="160" width="120" height="30" class="f-mut" stroke="currentColor"/>
  <rect x="310" y="160" width="120" height="30" class="f-acc" stroke="currentColor"/>
  <rect x="430" y="160" width="90" height="30" class="f-mut" stroke="currentColor"/>
  <text x="45" y="180" fill="currentColor" font-family="monospace">fa</text>
  <text x="135" y="180" fill="currentColor" font-family="monospace">fa</text>
  <text x="240" y="180" fill="currentColor" font-family="monospace">00</text>
  <text x="360" y="180" fill="currentColor" font-family="monospace">02</text>
  <text x="465" y="180" fill="currentColor" font-family="monospace">fa</text>
  <text x="540" y="168" fill="currentColor" font-size="12">00 = 8 바이트 전부 OK</text>
  <text x="540" y="186" fill="currentColor" font-size="12">0k = 앞 k 바이트만 OK</text>
  <text x="540" y="204" fill="currentColor" font-size="12">음수(fa…) = 전부 금지</text>
  <rect x="10" y="222" width="720" height="98" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="244" fill="currentColor" font-family="monospace" font-size="12">Shadow = (Addr &gt;&gt; 3) + Offset   맥 arm64: Offset = 0x7000020000 (런타임이 정함)</text>
  <text x="20" y="264" fill="currentColor" font-family="monospace" font-size="12">                                  Linux aarch64: Offset = 0x1000000000 (코드에 상수)</text>
  <text x="20" y="286" fill="currentColor" font-size="12">s[10] 쓰기: 주소 0x60200000099a → 칸 0x…998 의 섀도 = 02, (Addr &amp; 7) = 2 ≥ 2 → 금지</text>
  <text x="20" y="306" fill="currentColor" font-size="12">→ store 전에 __asan_report_store1 → "WRITE of size 1 … 0 bytes after 10-byte region"</text>
</svg>
```

이 레슨의 모듈(`buf.h`, 펌웨어 UART/무선 패킷 같은 프레임 `[A5][kind][len_lo][len_hi][payload][crc16]`)에 버그가 하나씩 들어 있다. `driver.c` 는 버그 종류마다 케이스 하나를 **별도 프로세스**로 돌린다 (ASan 은 첫 오류에서 프로세스를 끝내므로).

| 케이스 | 함수 | 버그 (펌웨어에서 흔한 모양) | ASan 이 부르는 이름 |
|---|---|---|---|
| `heap` | `pkt_payload_str` | 문자열 복사에 `'\0'` 자리를 안 잡음 | heap-buffer-overflow |
| `stack` | `pkt_hex_prefix` | 지역 배열 `char line[16]` 에 16자 + `'\0'` | stack-buffer-overflow |
| `global` | `pkt_count_kind` | 선로에서 온 kind 를 `>` 로 검사 (`>=` 여야) | global-buffer-overflow |
| `uaf` | `txq_drain` | 리스트를 돌며 `free(p)` 후 `p->next` | heap-use-after-free |
| `dfree` | `pkt_reset` | `free` 후 포인터를 그대로 둠 → 나중에 또 `free` | double-free |
| `leak` | `pkt_from_wire` | CRC 오류 경로에서 할당한 패킷을 버리고 `return NULL` | (맥 ASan: 못 봄) / LSan: memory leaks |

### Part A — 공간(spatial) 버그: heap · stack · global 오버플로

- `bash check.sh starter` 로 세 케이스가 각각 어떤 리포트를 내는지 본다 (`asan_heap.txt`, `asan_stack.txt`, `asan_global.txt`).
- 리포트마다 **오류 줄 · 접근 종류와 크기 · `#0` 프레임 · 위치 설명(region / frame object / global variable) · 섀도 덤프의 `[ ]`** 를 짚고, 고친다.
- `demo_shadow.c` 로 섀도 공식을 손으로 계산해 리포트의 섀도 값(`00 02`, `f1 … f3`, `f9`)이 정말 그 자리에 있는지 확인한다.

### Part B — 시간(temporal) 버그: use-after-free · double-free · 누수

- `uaf`, `dfree` 리포트의 **세 스택** (지금 접근 / freed by / previously allocated by) 으로 "누가 할당했고, 누가 먼저 풀었고, 누가 다시 만졌나" 를 재구성하고 고친다.
- `leak`: 맥에서 `ASAN_OPTIONS=detect_leaks=1` 이 실패하는 것을 보고, `leaks --atExit` (맥) 와 LeakSanitizer (VM, clang/gcc) 로 같은 누수를 잡는다.

### Part C — 펌웨어 팀 CI 에 넣기

- `check.sh` 가 곧 CI 게이트다: 케이스마다 별도 프로세스, **exit 0 + `ERROR: AddressSanitizer` 없음 + 결과 줄 일치** 를 모두 요구한다 (버그만 지우고 기능을 망가뜨린 "수정" 도 FAIL).
- 계측 asm(맥 clang vs Linux gcc), 비용(`demo_cost.sh`), `ASAN_OPTIONS`, recover 모드의 함정, Cortex-M 에서 못 쓰는 이유와 대안을 정리한다.

## 2. Problem (English)

> **Fix a packet-buffer module using AddressSanitizer reports.**
>
> `buf.h` declares a small packet module used by a firmware UART/radio stack: `pkt_new`, `pkt_free`, `pkt_payload_str` (NUL-terminated copy of a text payload), `pkt_hex_prefix` (hex dump of the first 8 frame bytes into a caller buffer), `pkt_count_kind` (per-kind RX statistics, `kind` comes off the wire and is untrusted), `txq_push` / `txq_drain` (send-and-free a TX queue), `pkt_reset` (drop frame storage, keep the struct) and `pkt_from_wire` (validate a received frame, return `NULL` on a CRC error).
>
> The test driver runs one case per process: `./t heap|stack|global|uaf|dfree|leak`. Build with `-fsanitize=address -g -O1 -fno-omit-frame-pointer`. Each case currently aborts with an ASan report (the leak case is silent on macOS).
>
> - Read each report and state: bug class, READ/WRITE and size, the faulting line, and which allocation/variable the address belongs to.
> - Fix **only** `starter.c` (do not touch `buf.h` or `driver.c`). Every case must exit 0, print no `ERROR: AddressSanitizer`, and print exactly e.g. `uaf: drained=3 bytes=31 left=0`.
> - Prove the leak fix: `MallocStackLogging=1 leaks --atExit -- ./t leak` must report `0 leaks for 0 total leaked bytes.`, and LeakSanitizer on Linux must be silent.
>
> Follow-ups: how is the shadow address computed; what is the difference between `fa`, `fd`, `f1`, `f3`, `f9`; what ASan cannot catch; ASan vs Valgrind; why ASan does not run on a Cortex-M4.

## 3. 시작 코드

공용 헤더 (수정 금지):

<!-- code: buf.h -->

테스트 드라이버 (수정 금지) — 결과 줄을 하나 찍는다. ASan 이 `_exit` 해도 줄이 남도록 `stdout` 을 버퍼 없이 쓴다. `CASE` 의 `noinline` 은 스택 트레이스에 `case_xxx` 프레임이 보이게 하려는 것이다.

<!-- code: driver.c -->

고칠 파일 — `TODO(...)` 가 붙은 줄 6곳:

<!-- code: starter.c -->

검사 스크립트 (Form S: `bash check.sh <starter|solution|practice>`, 어느 디렉터리에서 불러도 됨, 작업은 `/tmp/TOOL-B11_work/<variant>` 에서만):

<!-- code: check.sh -->

손으로 돌리기:

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -g -O1 -fno-omit-frame-pointer -fsanitize=address starter.c driver.c -o t && ./t heap
bash check.sh starter
```

처음 실행 결과 (실제 출력):

```text
$ bash check.sh starter
PASS build -Werror (starter.c + driver.c)
FAIL heap  rc=1  SUMMARY: AddressSanitizer: heap-buffer-overflow starter.c:72 in pkt_payload_str
FAIL stack  rc=1  SUMMARY: AddressSanitizer: stack-buffer-overflow starter.c:87 in pkt_hex_prefix
FAIL global  rc=1  SUMMARY: AddressSanitizer: global-buffer-overflow starter.c:98 in pkt_count_kind
FAIL uaf  rc=1  SUMMARY: AddressSanitizer: heap-use-after-free starter.c:123 in txq_drain
FAIL dfree  rc=1  SUMMARY: AddressSanitizer: double-free starter.c:54 in pkt_free
PASS leak/asan  (leak: good=3 bad_rejected=3)
FAIL leak/leaks  (leaks --atExit rc=1: 6 leaks for 240 total leaked bytes.)
2/8 passed
[exit code 1]
```

두 가지가 눈에 띈다. ① 버그가 6개인데 **`-Werror` 빌드는 경고 0개로 통과**한다 — 컴파일러 경고로는 하나도 안 잡힌다. ② `leak/asan` 은 **PASS** — 맥 ASan 은 누수를 보지 못하므로, 누수 검사는 별도 도구(`leaks`)가 맡는다.

관찰용 데모 (전부 이 디렉터리, 출력은 `*.txt` 로 저장됨):

```bash
bash demo_reports.sh   # 맥: asan_{heap,stack,global,uaf,dfree}.txt, asan_legend.txt, leaks_{starter,solution}.txt,
                       #     asan_leak_mac.txt, asan_recover.txt, asm_mac.txt, shadow_mac.txt, 연습용 asan_{scope,ret,overlap,pool}.txt
bash demo_vm.sh        # Lima VM elinux: asan_heap_gcc_linux.txt, lsan_leak_linux.txt, shadow_linux.txt, asm_linux_gcc.txt, nm_asan_linux.txt
bash demo_cost.sh      # 비용: cost.txt
cc -std=c11 -g -O1 -fsanitize=address demo_shadow.c -o ds && ./ds      # 섀도를 직접 읽기
cc -O1 -fsanitize=address -S demo_instr.c -o -                          # 계측 asm
```

## 4. 힌트

<details>
<summary>힌트 1 — 첫 다섯 줄만 읽어도 절반은 끝난다</summary>

`ERROR: AddressSanitizer: <종류>` → 버그 분류. 그 다음 줄 `READ/WRITE of size N` → 읽기냐 쓰기냐, 몇 바이트짜리 접근이냐. `#0` 프레임 → 그 접근이 일어난 소스 줄 (함수 이름이 `memcpy`/`printf_common` 같은 런타임 함수면, 그 함수를 부른 첫 **내 코드** 프레임을 본다). 그 다음 "위치 설명" 한 문단이 **그 주소가 누구 것이었는지** 를 말해 준다.

</details>

<details>
<summary>힌트 2 — "0 bytes after N-byte region" = 딱 한 칸 넘침</summary>

`0 bytes after` 는 유효 범위 끝 바로 다음 바이트, 즉 off-by-one 이다. heap 케이스: 10 바이트 블록의 11번째 바이트 = `s[10]` = 문자열 끝 `'\0'`. stack 케이스: `[32, 48) 'line'` 에 offset 48 = 16 바이트 배열의 17번째. global: 16 바이트 배열 = `uint32_t[4]` 의 `[4]`. 셋 다 "크기 또는 경계 비교" 한 글자 문제다.

</details>

<details>
<summary>힌트 3 — 시간 버그는 스택 세 개를 시간 순서로 다시 읽는다</summary>

리포트 순서는 "지금 → 해제 → 할당" 이지만, 이야기는 "할당(previously allocated by) → 해제(freed by) → 지금 접근" 순서다. uaf: `pkt_new` 가 만든 32 바이트 구조체를 `txq_drain` 안의 `pkt_free` 가 풀었고, 같은 `txq_drain` 의 `for` 줄이 그 구조체의 offset 0 (`next`) 을 읽었다. 해제 전에 `next` 를 지역 변수에 옮겨 두면 된다. dfree: 두 번째 `free` 의 대상이 첫 번째 `free` 와 같은 9 바이트 블록 — 누가 포인터를 정리하지 않았나?

</details>

<details>
<summary>힌트 4 — 누수: 맥은 leaks, Linux 는 LSan, 고칠 곳은 "오류 경로"</summary>

`leaks` 리포트의 `ROOT LEAK: <calloc in pkt_new>` 스택에 `pkt_from_wire` 가 보인다. 정상 경로는 호출자가 `pkt_free` 하므로, 새는 곳은 `NULL` 을 돌려주는 경로 — 이미 할당한 `p` 를 버리는 `return NULL;` 이다. 펌웨어 코드 리뷰의 단골: "에러 경로마다 할당한 것 다 풀었나?"

</details>

## 5. 풀이

### Part A-1 — heap-buffer-overflow 리포트 해부

`asan_heap.txt` (맥, `-g -O1 -fno-omit-frame-pointer -fsanitize=address`, 실제 출력 — 섀도 줄 일부 생략):

```text
==31709==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x60200000099a at pc 0x000100748ca0 bp 0x00016f6b6ac0 sp 0x00016f6b6ab8
WRITE of size 1 at 0x60200000099a thread T0
    #0 0x000100748c9c in pkt_payload_str starter.c:72
    #1 0x00010074959c in case_heap driver.c:19
    #2 0x0001007494fc in main driver.c:100
    #3 0x00018753e050 in start+0x19f8 (dyld:arm64+0x30050)

0x60200000099a is located 0 bytes after 10-byte region [0x602000000990,0x60200000099a)
allocated by thread T0 here:
    #0 0x0001010a5164 in malloc+0x78 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41164)
    #1 0x000100748c24 in pkt_payload_str starter.c:68
    #2 0x00010074959c in case_heap driver.c:19
    #3 0x0001007494fc in main driver.c:100
    #4 0x00018753e050 in start+0x19f8 (dyld:arm64+0x30050)

SUMMARY: AddressSanitizer: heap-buffer-overflow starter.c:72 in pkt_payload_str
Shadow bytes around the buggy address:
  0x602000000880: fa fa 00 00 fa fa 00 00 fa fa 00 00 fa fa 00 04
  0x602000000900: fa fa 00 fa fa fa 00 04 fa fa 00 00 fa fa 00 00
=>0x602000000980: fa fa 00[02]fa fa fa fa fa fa fa fa fa fa fa fa
  0x602000000a00: fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa fa
[exit code 134]
```

| 줄 | 뜻 |
|---|---|
| `==31709==` | PID. 여러 프로세스 로그가 섞일 때 구분용 |
| `heap-buffer-overflow on address …99a` | 힙 블록 **밖** 접근. `pc`/`bp`/`sp` 는 그 순간 레지스터 |
| `WRITE of size 1` | 1 바이트 **쓰기** — `s[p->len] = '\0'` (`char` 한 개) |
| `#0 pkt_payload_str starter.c:72` | 그 쓰기를 한 줄. `#1`, `#2` 는 호출 경로 |
| `0 bytes after 10-byte region [0x…990,0x…99a)` | 10 바이트 블록의 끝 바로 다음 = off-by-one. 반열린 구간 `[시작, 끝)` |
| `allocated by thread T0 here: … starter.c:68` | 그 블록을 만든 `malloc(p->len)` 줄 — 원인은 대개 **여기** (크기 계산) |
| `=>0x…980: fa fa 00[02]fa …` | 왼쪽 주소는 그 줄 첫 섀도 바이트가 대표하는 **앱 주소** (한 줄 = 섀도 16 B = 앱 128 B). `[02]` 가 문제 칸 |
| `[exit code 134]` | 맥 ASan 기본값 `abort_on_error=1` → `abort()` → SIGABRT(128+6). Linux 는 기본 1 |

섀도 줄 읽기: `0x…980` 부터 칸마다 8 바이트이므로 `[02]` 는 네 번째 칸 = `0x…980 + 3×8 = 0x…998`. 블록 `[0x…990, 0x…99a)` 는 칸 `0x…990` (8 바이트 전부 → `00`) + 칸 `0x…998` (앞 2 바이트만 → `02`). 쓰려던 `0x…99a` 는 칸 안 offset 2 → 2 ≥ 2 이므로 금지. 양옆 `fa` 는 힙 레드존(Heap left redzone). 이 맥의 리포트는 섀도 줄 왼쪽에 **앱 주소**를 찍는다 (예전 버전 리포트는 섀도 주소를 찍었다 — 버전마다 다를 수 있음, 확인 권장).

수정: `malloc(p->len + 1)`. "복사 + 종단 문자" 는 C 에서 가장 흔한 힙 오버플로이고, 펌웨어에서는 AT 명령 응답 · 로그 문자열에서 매일 나온다.

### Part A-2 — 섀도 공식을 손으로 확인

`demo_shadow.c` 는 `__asan_get_shadow_mapping()` 으로 (scale, offset) 를 얻어 섀도를 **직접** 읽는다 (그 함수는 `no_sanitize("address")` — 계측된 코드로 섀도를 읽으면 섀도의 섀도를 검사하다 죽는다). 실제 출력:

```text
$ ./ds                                   # 맥 (shadow_mac.txt)
shadow scale=3 (1 shadow byte = 8 app bytes)  offset=0x7000020000
heap malloc(10) addr=0x602000000970 shadow=0x0c740002012e : fa fa 00 02 fa fa   poisoned(p[0])=0
heap after free addr=0x602000000970 shadow=0x0c740002012e : fa fa fd fd fa fa   poisoned(p[0])=1
stack buf[10]  addr=0x00016f3ceac0 shadow=0x00702de99d58 : f1 f1 f1 f1 00 02 f3 f3   poisoned(p[0])=0
global[13]     addr=0x000100a383c0 shadow=0x007020167078 : f9 f9 00 05 f9 f9 00 00   poisoned(p[0])=0

$ ./ds                                   # Lima VM, gcc 15 (shadow_linux.txt)
shadow scale=3 (1 shadow byte = 8 app bytes)  offset=0x1000000000
heap malloc(10) addr=0xeb074b3e0010 shadow=0x1d70e967c002 : fa fa 00 02 fa fa   poisoned(p[0])=0
global[13]     addr=0xc678832f03c0 shadow=0x18df1065e078 : f9 f9 00 05 f9 f9 f9 f9   poisoned(p[0])=0
(free 후 fd fd, 스택 f1 f1 f1 f1 00 02 f3 f3 — 맥과 같은 값, shadow_linux.txt)
```

손 계산: `(0x602000000970 >> 3) + 0x7000020000 = 0xC040000012E + 0x7000020000 = 0xC740002012E` — 출력과 같다. 같은 10 바이트가 힙에서도 스택에서도 `00 02`, 13 바이트 전역은 `00 05` (8 + 5). `free` 직후 같은 자리가 `fd fd` 로 바뀐다 → 이것이 UAF 를 잡는 원리. 스택은 왼쪽 레드존 `f1` ×4 (32 B), 오른쪽 `f3`. 전역은 뒤쪽에 `f9`.

컴파일러가 실제로 넣는 검사 (`demo_instr.c` 의 `return p[i];`, 실제 출력을 나란히 놓음 — 프롤로그 생략, `;` 주석은 설명용으로 붙임):

```text
; 맥 clang -O1 -fsanitize=address -S (asm_mac.txt)        ; VM gcc 15 -O1 -fsanitize=address -S (asm_linux_gcc.txt)
_get_byte:                                                 get_byte:
  adrp x8, ___asan_shadow_memory_dynamic_address@GOTPAGE     add   x2, x0, x1
  ldr  x8, [x8, ___asan_shadow_memory_dynamic_address@…]     lsr   x4, x2, 3
  ldr  x8, [x8]              ; Offset 를 전역에서 읽음        mov   x3, 68719476736   ; = 0x1000000000 상수
  add  x0, x0, x1            ; Addr = p + i                  ldrsb w3, [x4, x3]      ; 섀도 (부호 있게)
  lsr  x9, x0, #3            ; Addr >> 3                     and   w4, w2, 7
  ldrsb w8, [x9, x8]         ; 섀도 바이트 (부호 있게)        cmp   w3, 0
  cmp  w8, #0                                                ccmp  w4, w3, 1, ne
  and  w9, w0, #0x7          ; Addr & 7                      bge   .L6               ; → __asan_report_load1
  ccmp w9, w8, #8, ne        ; 섀도≠0 이면 (Addr&7) vs 섀도  ldrb  w0, [x0, x1]      ; 원래 load
  b.ge LBB0_2                ; → bl ___asan_report_load1 ; brk #0x1
  ldrb w0, [x0]              ; 원래 load
```

규칙: 섀도가 0 이면 통과, 아니면 `(Addr & 7) + size - 1 ≥ 섀도` 일 때 보고. `fa`, `fd` 같은 값은 **부호 있는** 바이트로 읽으면 음수라 항상 보고된다 (`ldrsb` 를 쓰는 이유). 맥은 런타임이 시작할 때 정해 전역 `___asan_shadow_memory_dynamic_address` 에 넣어 두는 **동적 섀도**(이 맥에서는 실행마다 `0x7000020000` 이었다), Linux aarch64 는 코드에 박힌 **고정 상수** — 그래서 맥은 오프셋을 얻는 데 명령 3개(`adrp`+`ldr`+`ldr`), gcc 는 `mov` 1개. 1:8 비율이라 섀도 자체는 주소 공간의 1/8 을 **예약**만 하고, 실제로 만진 페이지만 물리 메모리를 쓴다.

### Part A-3 — stack-buffer-overflow, global-buffer-overflow

`asan_stack.txt` (실제, 섀도는 문제 줄만):

```text
==31815==ERROR: AddressSanitizer: stack-buffer-overflow on address 0x00016bd6a9b0 at pc 0x000104094f78 bp 0x00016bd6a970 sp 0x00016bd6a968
WRITE of size 1 at 0x00016bd6a9b0 thread T0
    #0 0x000104094f74 in pkt_hex_prefix starter.c:87
    #1 0x0001040956a8 in case_stack driver.c:31
    #2 0x000104095504 in main driver.c:101

Address 0x00016bd6a9b0 is located in stack of thread T0 at offset 48 in frame
    #0 0x000104094cb0 in pkt_hex_prefix starter.c:78

  This frame has 1 object(s):
    [32, 48) 'line' (line 80) <== Memory access at offset 48 overflows this variable
=>0x00016bd6a980: f1 f1 f1 f1 00 00[f3]f3 00 00 00 00 00 00 00 00
```

- `This frame has 1 object(s): [32, 48) 'line' (line 80)` — 컴파일러가 프레임 안에 배치한 **지역 변수 지도**. ASan 은 각 지역 배열 앞뒤에 레드존을 둬서 `line` 이 프레임 offset 32..47 에 있다. offset 48 = 끝 바로 다음 → `overflows this variable`.
- 섀도: `f1 ×4` (왼쪽 레드존 32 B) + `00 00` (`line` 16 B) + `[f3]` (오른쪽 레드존). 지역 변수가 여럿이면 사이에 `f2` (mid redzone) 가 들어간다 (`asan_scope.txt` 다른 프레임 줄에 `f2` 가 보인다).
- 원인: 8 바이트 → 16 hex 문자 + `'\0'` = 17 바이트가 필요. 수정 `char line[2 * PKT_HEX_BYTES + 1];`. 펌웨어에서 이런 1 바이트 스택 오버플로는 바로 옆 지역 변수나 **저장된 LR** 을 깨서, 함수가 끝날 때 엉뚱한 곳으로 리턴 → HardFault 로 나타난다 (RTOS-M09 카나리, BM-A07 스택 페인팅이 잡으려는 것).

`asan_global.txt` (실제, 일부 줄 생략):

```text
==31868==ERROR: AddressSanitizer: global-buffer-overflow on address 0x0001002ac830 at pc 0x0001002a4fec bp 0x00016fb5aaa0 sp 0x00016fb5aa98
READ of size 4 at 0x0001002ac830 thread T0
    #0 0x0001002a4fe8 in pkt_count_kind starter.c:98
    #1 0x0001002a579c in case_global driver.c:42
    #2 0x0001002a550c in main driver.c:102

0x0001002ac830 is located 0 bytes after global variable 'g_kind_count' defined in 'starter.c' (0x0001002ac820) of size 16
=>0x0001002ac800: 00 00 00 00 00 00[f9]f9 00 00 00 00 00 00 00 00
```

- `READ of size 4` — `++g_kind_count[kind]` 는 읽고-더하고-쓰기인데 보고는 **읽기** 하나다. 같은 주소의 쓰기 검사는 같은 기본 블록 안에서 중복이라 컴파일러가 하나로 줄인다 (LLVM ASan 의 중복 검사 제거 — 확인 권장). 어쨌든 첫 접근에서 멈춘다.
- `global variable 'g_kind_count' … of size 16` — 전역은 이름과 크기까지 나온다 (`uint32_t[4]` = 16 B). `0 bytes after` → 인덱스 4.
- 수정: `kind >= PKT_NKINDS`. ASan 이 없으면 이 쓰기는 **조용히 다음 전역**을 1 증가시킨다 — MCU 의 `.bss` 에서 옆 변수가 이상하게 바뀌는 바로 그 버그 (C-B11 참고).

### Part B-1 — heap-use-after-free: 세 스택

`asan_uaf.txt` (실제, `start` 프레임 · 섀도 줄 일부 생략):

```text
==31980==ERROR: AddressSanitizer: heap-use-after-free on address 0x603000000fd0 at pc 0x000100e891c4 bp 0x00016ef769f0 sp 0x00016ef769e8
READ of size 8 at 0x603000000fd0 thread T0
    #0 0x000100e891c0 in txq_drain starter.c:123
    #1 0x000100e89944 in case_uaf driver.c:58
    #2 0x000100e89514 in main driver.c:103

0x603000000fd0 is located 0 bytes inside of 32-byte region [0x603000000fd0,0x603000000ff0)
freed by thread T0 here:
    #0 0x00010175d258 in free+0x7c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41258)
    #1 0x000100e8915c in txq_drain starter.c:125
    #2 0x000100e89944 in case_uaf driver.c:58

previously allocated by thread T0 here:
    #0 0x00010175d450 in calloc+0x80 (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41450)
    #1 0x000100e8890c in pkt_new starter.c:28
    #2 0x000100e898f0 in case_uaf driver.c:54
=>0x603000000f80: 00 00 fa fa 00 00 00 00 fa fa[fd]fd fd fd fa fa
```

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">uaf 리포트를 시간 순서로 — 32 바이트 pkt_t 한 개의 일생</text>
  <path d="M20 120 L720 120" stroke="currentColor" stroke-width="2"/>
  <path d="M710 114 L720 120 L710 126" fill="none" stroke="currentColor" stroke-width="2"/>
  <rect x="20" y="40" width="200" height="60" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="30" y="60" fill="currentColor" font-weight="bold">① 할당</text>
  <text x="30" y="78" fill="currentColor" font-size="12">calloc ← pkt_new :28</text>
  <text x="30" y="94" fill="currentColor" font-size="12">← case_uaf :54</text>
  <rect x="260" y="40" width="200" height="60" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="270" y="60" fill="currentColor" font-weight="bold">② 해제</text>
  <text x="270" y="78" fill="currentColor" font-size="12">free ← txq_drain :125</text>
  <text x="270" y="94" fill="currentColor" font-size="12">(pkt_free 가 인라인됨)</text>
  <rect x="500" y="40" width="210" height="60" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="510" y="60" fill="currentColor" font-weight="bold">③ 다시 읽음 ✗</text>
  <text x="510" y="78" fill="currentColor" font-size="12">p-&gt;next ← txq_drain :123</text>
  <text x="510" y="94" fill="currentColor" font-size="12">READ 8 @ offset 0</text>
  <text x="20" y="146" fill="currentColor" font-size="12">섀도:</text>
  <text x="70" y="146" fill="currentColor" font-family="monospace" font-size="12">00 00 00 00</text>
  <text x="300" y="146" fill="currentColor" font-family="monospace" font-size="12">fd fd fd fd  (freed)</text>
  <text x="540" y="146" fill="currentColor" font-family="monospace" font-size="12">fd → 보고</text>
  <rect x="260" y="160" width="450" height="30" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="270" y="180" fill="currentColor" font-size="12">격리 구역(quarantine): 풀린 블록을 바로 재사용하지 않고 잠시 fd 로 둔다</text>
  <text x="20" y="214" fill="currentColor" font-size="12">리포트 순서 = ③ → ② → ①  ("READ … / freed by … / previously allocated by …")</text>
  <text x="20" y="236" fill="currentColor" font-size="12">격리 구역이 넘쳐 블록이 재사용된 뒤라면 섀도가 00 으로 돌아가 → ASan 도 못 잡는다</text>
</svg>
```

- `0 bytes inside of 32-byte region` — `sizeof(pkt_t)` = 32, offset 0 = 첫 필드 `next`. `READ of size 8` = 포인터 하나.
- `freed by … txq_drain starter.c:125` — `pkt_free` 가 `txq_drain` 에 **인라인**되어 맥 심볼라이저는 `pkt_free` 프레임을 따로 보여 주지 않는다 (Linux 의 llvm-symbolizer 는 인라인 프레임을 보여 주는 편 — 확인 권장).
- 섀도 `fd fd fd fd` = 32 바이트 해제 영역 (Freed heap region).
- 수정: `for (pkt_t *p = q->head, *next; p != NULL; p = next) { next = p->next; … pkt_free(p); }`. 리스트 해제의 정석이다 (DS 트랙의 리스트, TOOL-B10 의 lldb 디버깅과 같은 모양).

ASan 없이 `-O2` 로 돌리면 이 버그는 대개 **멀쩡히 통과**한다 — 해제 직후 메모리에 `next` 값이 아직 남아 있기 때문. "테스트는 통과하는데 가끔 죽는" 버그가 되는 이유다.

### Part B-2 — double-free

`asan_dfree.txt` (실제, 런타임/`start` 프레임 일부 생략):

```text
dfree: len=0 frame=dangling
==32248==ERROR: AddressSanitizer: attempting double-free on 0x602000000970 in thread T0:
    #0 0x000103779258 in free+0x7c (libclang_rt.asan_osx_dynamic.dylib:arm64e+0x41258)
    #1 0x000102f20b84 in pkt_free starter.c:54
    #2 0x000102f2151c in main driver.c:104

0x602000000970 is located 0 bytes inside of 9-byte region [0x602000000970,0x602000000979)
freed by thread T0 here:
    #1 0x000102f21204 in pkt_reset starter.c:135
    #2 0x000102f21a20 in case_dfree driver.c:67
previously allocated by thread T0 here:
    #1 0x000102f2091c in pkt_new starter.c:31
    #2 0x000102f21a18 in case_dfree driver.c:66
SUMMARY: AddressSanitizer: double-free starter.c:54 in pkt_free
```

- 9 바이트 = 헤더 4 + `"abc"` 3 + CRC 2 → `p->frame`. 첫 `free` 는 `pkt_reset`, 두 번째는 `pkt_free`.
- 첫 스택에 `case_dfree` 가 **없다** (`pkt_free` → 바로 `main`). `case_dfree` 의 마지막 호출 `pkt_free(p)` 가 꼬리 호출(tail call, `b _pkt_free`)로 바뀌어 프레임이 사라진 것이다 — `-S` 에서 `b	_pkt_free` 를 확인했고, `-fno-optimize-sibling-calls` 로 다시 빌드하면 `#2 case_dfree driver.c:69` 가 나타난다 (실측). 스택이 이상하게 짧으면 꼬리 호출 · 인라인을 의심하라.
- 수정: `free(p->frame); p->frame = NULL;` — `free(NULL)` 은 C 표준상 아무 일도 안 한다 (C11 7.22.3.3). 더 근본적인 수정은 **소유권 규칙**을 정하는 것 (Q8).

### Part B-3 — 누수: 맥 ASan 은 못 본다 → leaks / LSan

```text
$ ASAN_OPTIONS=detect_leaks=1 ./t_asan leak              # asan_leak_mac.txt
==21690==AddressSanitizer: detect_leaks is not supported on this platform.
[exit code 134]
```

맥 `leaks --atExit` (일반 `-g -O1` 빌드, ASan 아님 — ASan 은 malloc 을 바꿔치기하므로 같이 쓰지 않는다). `leaks_starter.txt` 핵심 (Binary Images 목록은 잘라냄):

```text
$ MallocStackLogging=1 leaks --atExit -- ./starter.plain leak
leak: good=3 bad_rejected=3
Process 21713: 191 nodes malloced for 10 KB
Process 21713: 6 leaks for 240 total leaked bytes.

STACK OF 3 INSTANCES OF 'ROOT LEAK: <calloc in pkt_new>':
4   starter.plain    0x100c24a90 main + 280  driver.c:0
3   starter.plain    0x100c24dc4 case_leak + 140  driver.c:88
2   starter.plain    0x100c24940 pkt_from_wire + 72  starter.c:148
1   starter.plain    0x100c245e0 pkt_new + 64  starter.c:29
0   libsystem_malloc.dylib  0x188f02ad0 _malloc_zone_calloc_instrumented_or_legacy + 132
====
    6 (240 bytes) << TOTAL >>
      2 (80 bytes) ROOT LEAK: <calloc in pkt_new 0x753902c000> [48]
         1 (32 bytes) <malloc in pkt_new 0x7539030000> [32]
      … (같은 모양 2개 더)
[exit code 1]
```

- `ROOT LEAK` = 어디서도 가리키지 않는 블록 (구조체), 그 아래 들여쓴 블록 = 루트를 통해서만 닿는 블록 (`frame`). 3번 CRC 오류 × (구조체 + 프레임) = 6.
- 크기 `[48]`, `[32]` 는 요청 크기(32, 10)가 아니라 malloc zone 이 실제로 잡은 블록 크기로 보인다 (MallocStackLogging 사용 시 반올림 방식은 확인 권장). 스택의 줄 번호는 **리턴 주소** 기준이라 1줄 어긋날 수 있다 (`starter.c:148` vs 실제 호출 147).
- `leaks` 는 누수가 있으면 exit 1 → `check.sh` 가 그대로 게이트로 쓴다. 도구 깊이(Guard Malloc, `malloc_history`, valgrind)는 TOOL-M10.

Linux (VM) 에서는 ASan 에 **LeakSanitizer 가 기본으로 켜져** 있다. `lsan_leak_linux.txt` (clang 21, 실제 — 프레임 일부 생략):

```text
$ clang … -fsanitize=address starter.c driver.c -o t_clang && ./t_clang leak
leak: good=3 bad_rejected=3
==517317==ERROR: LeakSanitizer: detected memory leaks

Direct leak of 96 byte(s) in 3 object(s) allocated from:
    #1 0xb445ca0ad250 in pkt_new /tmp/TOOL-B11_work/vm/starter.c:28:16
    #2 0xb445ca0adb2c in pkt_from_wire /tmp/TOOL-B11_work/vm/starter.c:147:16
    #3 0xb445ca0ae334 in case_leak /tmp/TOOL-B11_work/vm/driver.c:87:14
Indirect leak of 20 byte(s) in 2 object(s) allocated from:
    #1 0xb445ca0ad260 in pkt_new /tmp/TOOL-B11_work/vm/starter.c:31:16
SUMMARY: AddressSanitizer: 116 byte(s) leaked in 5 allocation(s).
[exit code 1]

$ ./s_clang leak                                       # solution
leak: good=3 bad_rejected=3
[exit code 0]

$ clang -std=c11 -g -O1 -fsanitize=leak starter.c driver.c -o t_lsan && ./t_lsan leak   # LSan 단독
Direct leak of 96 byte(s) in 3 object(s) allocated from: …
Indirect leak of 30 byte(s) in 3 object(s) allocated from: …
SUMMARY: LeakSanitizer: 126 byte(s) leaked in 6 allocation(s).
[exit code 23]
```

- Direct = 루트 누수(3 × 32 B 구조체), Indirect = 누수 블록을 통해서만 닿는 것(프레임 10 B).
- 흥미로운 실측 차이: ASan 빌드는 Indirect 가 **2개 20 B**, LSan 단독 빌드는 **3개 30 B**. LSan 은 스택 · 레지스터 · 전역을 보수적으로(conservative) 훑기 때문에, 어딘가에 남은 옛 포인터 값 하나가 프레임 하나를 "아직 닿음" 으로 만든 것으로 보인다. 누수 개수는 **하한**으로 읽어라.
- 종료 코드: ASan+LSan = 1, LSan 단독 = 23 (각 런타임의 기본 `exitcode`). CI 는 0 이 아닌지만 본다.
- 수정: `if (crc mismatch) { pkt_free(p); return NULL; }`. 또는 CRC 를 **먼저** 검사하고 그 다음 할당 — 오류 경로에 할당이 아예 없게 만드는 것이 펌웨어에서는 더 좋다.

### Part C-1 — CI 게이트로서의 check.sh

```text
$ bash check.sh solution
PASS build -Werror (solution.c + driver.c)
PASS heap  (heap: str="TEMP=23.5C" strlen=10)
PASS stack  (stack: hex=A502050001020304)
PASS global  (global: counts=1,1,1,2 rejected=2)
PASS uaf  (uaf: drained=3 bytes=31 left=0)
PASS dfree  (dfree: len=0 frame=null)
PASS leak/asan  (leak: good=3 bad_rejected=3)
PASS leak/leaks  (leaks --atExit: 0 leaks for 0 total leaked bytes.)
8/8 passed
[exit code 0]
```

설계 포인트:

| 결정 | 이유 |
|---|---|
| 케이스마다 별도 프로세스 | ASan 은 첫 오류에서 끝낸다 → 한 프로세스면 첫 버그 뒤 케이스는 안 돈다 |
| exit code **와** `ERROR: AddressSanitizer` grep **와** 결과 줄 | recover 모드에선 오류가 나도 exit 0 일 수 있다 (아래). 결과 줄은 "버그를 지워 기능을 망가뜨린" 수정을 막는다 |
| `ASAN_OPTIONS=abort_on_error=0` | 맥 기본은 `abort()` (rc 134 + 크래시 리포트 파일). CI 에선 rc 1 이 깔끔 |
| `-g -O1 -fno-omit-frame-pointer` | 줄 번호, 적당한 속도, 프레임 포인터 기반 빠른 스택 수집. `-O0` 도 되지만 느리다 |
| 누수는 맥 `leaks`, Linux 는 LSan | 맥 ASan 은 `detect_leaks` 미지원 (위 실측) |
| `-Werror` 일반 빌드도 함께 | 경고는 ASan 이 못 보는 것을 본다 (TOOL-B02). 여기선 6 버그 모두 경고 0 이었다 |

**recover 모드 함정** (`-fsanitize-recover=address` + `ASAN_OPTIONS=halt_on_error=0`, `asan_recover.txt` 실제):

```text
$ ASAN_OPTIONS=halt_on_error=0 ./t_rec global   # built with -fsanitize-recover=address
==33929==ERROR: AddressSanitizer: global-buffer-overflow on address 0x000104c90830 at pc 0x000104c89080 bp 0x00016b176a70 sp 0x00016b176a68
READ of size 4 at 0x000104c90830 thread T0
0x000104c90830 is located 0 bytes after global variable 'g_kind_count' defined in 'starter.c' (0x000104c90820) of size 16
SUMMARY: AddressSanitizer: global-buffer-overflow starter.c:98 in pkt_count_kind
global: counts=1,1,1,2 rejected=1
[exit code 0]
```

오류를 보고하고도 계속 달려서 **exit 0** 으로 끝났다. 결과도 틀렸다 (`rejected=1` — kind 4 가 레드존에 카운트됨). 오류를 한 번에 많이 모을 땐 편하지만, CI 가 exit code 만 보면 통과시켜 버린다 → 로그 grep 이 필수.

### Part C-2 — 비용 (실측, `cost.txt`)

`demo_cost.c` 를 `solution.c` 와 링크해 `-O1 -g` 일반 vs ASan, 5회 실행 중앙값 (이 맥은 다른 작업이 동시에 돌고 있어 ±10% 정도 흔들림):

| 워크로드 | 일반 | ASan | 배율 |
|---|---|---|---|
| alloc: `pkt_new`+`txq_push` 20만 번 + `txq_drain` | 72.5 ms | 106.0 ms | ≈ 1.5× |
| crc: CRC-16 (8 스텝마다 load 1번, ALU 위주) | 104.8 ms | 107.3 ms | ≈ 1.0× |
| hist: `hist[g_big[i]]++` (매 바이트 load+store) | 12.1 ms | 39.7 ms | ≈ 3.3× |
| 최대 RSS | 3504 KiB | 45280 KiB | ≈ 13× |
| `__text` 크기 (`size -m`) | 1712 B | 4188 B | ≈ 2.4× |

- 느려지는 정도는 **메모리 접근 밀도**에 비례한다: 검사는 load/store 마다 붙으므로 ALU 위주 CRC 는 거의 공짜, 바이트 히스토그램은 3배. 흔히 "ASan 은 약 2배" 라고 하는 것은 평균적인 프로그램 얘기다.
- 메모리는 섀도 페이지 + 레드존 + 격리 구역 때문에 크게 는다 (격리 구역 기본 크기는 플랫폼별 — `quarantine_size_mb`, 확인 권장).
- 런타임은 별도 동적 라이브러리 (`@rpath/libclang_rt.asan_osx_dynamic.dylib`, `otool -L` 실측). ELF 오브젝트가 런타임에 요구하는 심볼(`nm_asan_linux.txt`): `U __asan_report_load1/4/8/_n`, `U __asan_report_store1/8/_n`, `U __asan_register_globals`, `U __asan_stack_malloc_0`, `U __asan_init` …

### Part C-3 — 왜 Cortex-M 펌웨어에서는 못 쓰나

| 필요한 것 | ASan (호스트) | Cortex-M4 MCU |
|---|---|---|
| 섀도 영역 | 주소 공간 1/8 을 예약, 만진 페이지만 사용 (가상 메모리) | MMU 없음 → 섀도를 실제 RAM 으로 잡아야 함. RAM 256 KB 면 섀도만 32 KB + 레드존 |
| 런타임 | malloc 가로채기, 심볼라이저, 스택 수집, 격리 구역 | newlib-nano, 보통 malloc 안 씀 (정적 풀) |
| 코드 크기 · 속도 | `__text` 2.4배, 접근 많은 루프 3배 | Flash 예산, ISR 지연 시간 예산이 깨짐 |
| 출력 | stderr | UART/RTT 뿐 |

그래서 펌웨어 팀은 보통 이렇게 나눈다 (구체 제품은 확인 권장):

- **하드웨어와 무관한 모듈**(파서, 큐, FTL 테이블, 상태 기계)은 이 레슨처럼 호스트 단위 테스트 + ASan/UBSan(TOOL-B12) 을 CI 에서.
- **타깃에서는** MPU 스택 가드 · NULL 트랩(BM-A08), 스택 카나리/페인팅(RTOS-M09, BM-A07), 힙/풀 블록 앞뒤 가드 패턴(DS-M02 풀에 붙여 볼 수 있음), HardFault 레지스터 덤프.
- 리눅스 커널은 KASan, Android arm64 는 주소 상위 바이트(TBI)를 쓰는 HWASan, ARMv8.5 MTE 는 하드웨어 태그로 비슷한 검사를 한다. Zephyr 같은 RTOS 는 호스트(POSIX) 타깃 빌드에서 ASan 을 켤 수 있다 (확인 권장).
- 정적 풀처럼 malloc 을 안 거치는 할당자는 ASan 이 **모른다** → 연습 4 의 수동 poisoning(`ASAN_POISON_MEMORY_REGION`)으로 알려 준다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

바뀐 곳 6군데가 각각 버그 하나다: `+ 1` (heap), `+ 1` (stack), `>=` (global), `next` 를 먼저 저장 (uaf), `p->frame = NULL` (double-free), 오류 경로 `pkt_free` (leak).

## 7. 자주 하는 실수

1. **리포트 `#0` 만 보고 고친다** → heap 오버플로의 `#0` 은 "넘친 쓰기" 줄(72)이지만 원인은 대개 `allocated by` 의 크기 계산(68)이다. `s[p->len] = '\0'` 을 지우는 것은 기능을 깨는 "수정" 이고, check.sh 의 결과 줄(`strlen=10`) 검사가 막는다.
2. **double-free 를 `free` 하나를 지워서 고친다** → `pkt_free` 의 `free(p->frame)` 을 지우면 dfree 케이스는 조용해지지만, reset 을 거치지 않은 모든 패킷의 프레임이 샌다. 실측: 그렇게 바꾼 빌드로 uaf 케이스를 `leaks --atExit` 에 돌리면 `3 leaks for 80 total leaked bytes.` 고친 코드: `free(p->frame); p->frame = NULL;` (해제한 쪽이 포인터를 정리).
3. **`-O2` 이상 + 프레임 포인터 생략으로 빌드** → 스택이 짧거나 인라인/꼬리 호출로 프레임이 빠져 헷갈린다 (Part B-2 의 `case_dfree` 사라짐). 진단 빌드는 `-O1 -g -fno-omit-frame-pointer`, 필요하면 `-fno-optimize-sibling-calls`.
4. **맥에서 ASan 이 누수까지 본다고 믿는다** → `leak/asan` 이 PASS 인데 누수 6개가 있었다. 맥은 `leaks --atExit`, Linux 는 LSan.
5. **ASan 빌드를 `leaks`/valgrind 아래서 돌린다** → ASan 은 자기 malloc 을 쓰고 섀도 영역을 크게 예약하므로 다른 메모리 도구와 겹치면 결과가 의미 없거나 실행이 안 된다. 도구마다 별도 빌드.
6. **오브젝트 일부만 `-fsanitize=address` 로 빌드** → 계측 안 된 `.c` 안의 오버플로는 못 잡는다 (malloc/memcpy 같은 가로챈 함수만 검사). 링크도 `-fsanitize=address` 로 해야 런타임이 붙는다 — 아니면 맥 ld 가 `Undefined symbols for architecture arm64: "___asan_init" … "___asan_report_load1"` 로 실패한다 (실측, TOOL-B05).
7. **TSan 과 같이 켠다** → `clang: error: invalid argument '-fsanitize=address' not allowed with '-fsanitize=thread'` (실측) — 섀도 배치가 서로 다르다. ASan+UBSan 은 같이 된다. TSan 은 TOOL-M09.

## 8. 면접 Q&A

<details>
<summary>Q1. AddressSanitizer 는 어떻게 동작하나요? 한 문단으로.</summary>

컴파일 시 모든 load/store 앞에 섀도 검사를 넣고(`Shadow = (Addr >> 3) + Offset` 의 1 바이트를 읽어 0 이면 통과, 아니면 칸 안 offset 과 비교), 스택 · 전역 배열 주위에 레드존을 끼워 넣는다. 런타임은 malloc/free 를 가로채 블록 양옆에 레드존(`fa`)을 두고, free 된 블록은 `fd` 로 칠해 격리 구역에 잠시 둔다. 금지된 바이트를 만지면 `__asan_report_*` 가 스택 트레이스 · 할당/해제 스택 · 섀도 덤프를 찍고 프로세스를 끝낸다. 결과적으로 오버플로와 UAF 를 **일어나는 그 접근에서** 잡는다 — 나중에 터지는 증상이 아니라.

</details>

<details>
<summary>Q2. (uaf 리포트를 보여 주며) 이 버그를 설명하고 고쳐 보세요.</summary>

"`READ of size 8` 이 32 바이트 블록의 offset 0 — `pkt_t` 의 `next` 필드. 블록은 `pkt_new` 의 `calloc` 이 만들었고(previously allocated by), `txq_drain` 125 줄의 `pkt_free` 가 풀었고(freed by), 같은 함수 123 줄 `for` 의 `p = p->next` 가 풀린 뒤 읽었다. 리스트를 돌며 지우는 전형적 실수다. 해제 전에 `next = p->next` 로 저장하고 `p = next` 로 진행한다." 꼬리질문 "ASan 없으면?" → 해제 직후엔 값이 남아 있어 대개 통과, 다른 할당이 그 자리를 재사용하면 엉뚱한 노드로 점프 — 재현 안 되는 크래시.

</details>

<details>
<summary>Q3. 섀도 바이트 02, fa, fd, f1, f3, f9 는 각각 무슨 뜻이죠? 왜 1:8 인가요?</summary>

`00` 8 바이트 모두 접근 가능, `01`~`07` 앞 k 바이트만 가능(블록 끝이 칸 중간에 걸림 — `malloc(10)` 의 두 번째 칸이 `02`), `fa` 힙 레드존, `fd` 해제된 힙, `f1`/`f2`/`f3` 스택 왼쪽/중간/오른쪽 레드존, `f5` return 이후 스택, `f8` 스코프 밖 스택, `f9` 전역 레드존, `f7` 사용자가 poison 함(연습 4). malloc 이 최소 8 바이트 정렬을 보장하므로 "칸의 앞 k 바이트만 유효" 로 표현이 충분하고, 1:8 이면 섀도가 주소 공간의 1/8 — 검사도 shift 하나 + load 하나로 끝난다 (`asan_legend.txt` 의 범례가 이 목록).

</details>

<details>
<summary>Q4. ASan 이 못 잡는 메모리 버그는?</summary>

① 레드존을 **건너뛰는** 접근 — 큰 인덱스로 다른 유효 블록 한가운데를 건드리면 섀도가 `00` 이라 통과. ② 구조체 **안** 필드 간 오버플로 (`struct { char a[4]; int b; }` 에서 `a[5]`) — 같은 객체 안이라 레드존이 없다. ③ 격리 구역이 넘쳐 재사용된 뒤의 UAF. ④ 초기화 안 한 메모리 읽기 (MemorySanitizer 몫, 맥 미지원). ⑤ 계측 안 된 코드(미리 빌드된 라이브러리) 안의 접근. ⑥ 자체 할당자(정적 풀) 안의 넘침 — 수동 poisoning 필요. ⑦ 맥에서의 누수. ⑧ 실행되지 않은 경로 — 동적 도구는 돈 코드만 본다. UB(TOOL-B12)·레이스(TOOL-M09)는 다른 새니타이저.

</details>

<details>
<summary>Q5. ASan 과 Valgrind(memcheck) 는 어떻게 다른가요?</summary>

ASan 은 **재컴파일**이 필요하지만 빠르고(이 맥 실측 1~3배), 스택 · 전역 오버플로까지 잡는다. Valgrind 는 바이너리를 그대로 가상 CPU 에서 돌려 재컴파일이 필요 없지만 훨씬 느리고(보통 수십 배로 알려짐 — 확인 권장), 레드존이 힙에만 있어 스택/전역 배열 오버플로는 거의 못 잡는 대신 **초기화 안 된 값 사용**을 잡는다. 맥(Apple Silicon)에는 Valgrind 가 없다 — VM 에는 있다. 깊이 비교는 TOOL-M10.

</details>

<details>
<summary>Q6. 펌웨어(Cortex-M) 에선 ASan 을 쓰나요? 아니면 무엇을 쓰죠?</summary>

보통 안 쓴다: MMU 가 없어 섀도를 실제 RAM 으로 잡아야 하고(RAM 의 1/8 + 레드존), 런타임(malloc 가로채기, 스택 수집, 심볼라이저)이 무겁고, 코드 크기가 2배 이상, 접근 많은 루프는 3배 느려져 ISR 타이밍이 깨진다. 대신 ① 하드웨어 무관 모듈을 호스트 단위 테스트 + ASan 으로 CI 에서, ② 타깃에선 MPU 스택 가드/NULL 트랩, 스택 카나리 · 페인팅, 풀 블록 가드 패턴, HardFault 덤프. Linux 가 도는 SoC 라면 KASan(커널), HWASan/MTE(arm64) 가 있다.

</details>

<details>
<summary>Q7. CI 에 ASan 을 넣을 때 주의점은?</summary>

별도 빌드 구성(`-O1 -g -fno-omit-frame-pointer -fsanitize=address`, 컴파일과 링크 둘 다), 테스트당 프로세스 하나, **exit code 와 로그 grep 둘 다** 확인(recover 모드는 오류가 나도 exit 0 — 실측), 맥이면 `abort_on_error=0` 으로 rc 1, Linux 는 LSan 이 기본으로 켜져 누수도 실패 처리됨, 심볼라이저가 있어야 줄 번호가 나옴, TSan 과는 별도 잡(job), UBSan 은 같은 잡에 합쳐도 됨. 그리고 테스트가 **경로를 실제로 돌려야** 잡힌다 — 커버리지가 곧 검출 범위다.

</details>

<details>
<summary>Q8. double-free 를 `p->frame = NULL` 로 고쳤습니다. 이게 근본 해결인가요?</summary>

증상 해결이자 좋은 방어 습관이지만, 근본은 **소유권(ownership) 규칙**이다: "frame 은 pkt 가 소유, `pkt_reset` 후 frame 은 없음(NULL)" 을 API 문서와 불변식으로 정하면 `pkt_free` 가 안전해진다. 반대로 NULL 대입이 진짜 설계 버그(두 곳이 같은 블록을 소유한다고 믿음)를 가릴 수도 있다 — 그럴 땐 소유권을 하나로 줄이는 것이 맞다. 펌웨어에선 아예 동적 할당 대신 정적 풀(DS-M02)과 "누가 반납하는가" 규칙을 둔다.

</details>

## 9. 추가 연습

`practice.c` 하나에 연습 1~4 의 **고친** 코드가 있고, `-DBUGGY` 로 빌드하면 고치기 전 코드가 들어간다. `check.sh practice` 는 고친 코드가 깨끗한지 **와** 버그 버전을 ASan 이 정말 잡는지 둘 다 확인한다.

### 연습 1 — stack-use-after-scope

`frame_tag()` 가 `if` 블록 안에 `char tmp[12]` 를 두고 `tag = tmp;` 한 뒤, 블록 **밖**에서 `tag` 를 복사한다. 리포트를 예상해 보고 고쳐라.

<details>
<summary>답 보기</summary>

`asan_scope.txt` (실제): `stack-use-after-scope … READ of size 1 … #0 frame_tag practice.c:44`, `[32, 44) 'tmp' (line 38) <== Memory access at offset 32 is inside this variable`, 섀도 `f1 f1 f1 f1[f8]f8 f3 f3`. 블록이 끝나면 컴파일러가 `tmp` 를 `f8` 로 칠한다(clang 은 use-after-scope 검사가 기본으로 켜져 있음). `-O2` 일반 빌드에서는 같은 스택 슬롯이 아직 살아 있어 대개 "잘 돌아간다" — 그래서 위험하다. 고침: `tmp` 를 쓰는 범위(함수) 수준으로 올린다.

</details>

### 연습 2 — stack-use-after-return 은 기본으로 꺼져 있다

`set_err()` 가 지역 배열에 `"E042"` 를 만들고 그 주소를 전역 `g_last_err` 에 저장, 나중에 `status_line()` 이 읽는다. 그냥 돌리면? `ASAN_OPTIONS=detect_stack_use_after_return=1` 이면?

<details>
<summary>답 보기</summary>

`asan_ret.txt` (실제): 기본 옵션으로는 **아무 보고 없이** `ret: err=E042` 가 찍히고 exit 0 — 죽은 프레임의 바이트가 우연히 남아 있었다. 옵션을 켜면 `stack-use-after-return … READ of size 5 … #2 snprintf … #3 status_line practice.c:70`, 위치는 `in frame #0 set_err … [32, 48) 'g_err_buf'`, 섀도 `f5`. 이 검사는 지역 변수를 "가짜 스택"(힙)에 두어야 해서 비용이 커 기본은 런타임 옵션으로 꺼져 있다(clang 의 `-fsanitize-address-use-after-return=runtime` 기본 — 확인 권장). 고침: 수명이 긴 저장소(`static char g_err_buf[16]`) 나 호출자가 준 버퍼.

</details>

### 연습 3 — memcpy-param-overlap

UART 줄 버퍼에서 처리한 앞 k 바이트를 버리고 나머지를 앞으로 당기는 `rx_consume()` 이 `memcpy(buf, buf + k, len - k)` 를 쓴다. 결과가 맞게 나와도 왜 버그인가?

<details>
<summary>답 보기</summary>

C11 7.24.2.1: `memcpy` 의 두 영역이 겹치면 동작이 정의되지 않는다. 구현이 뒤에서부터 복사하거나 블록 단위로 복사하면 결과가 깨질 수 있다. ASan 은 `memcpy` 를 가로채 겹침을 검사한다 — `asan_overlap.txt`: `memcpy-param-overlap: memory ranges [0x…aa0,0x…aa9) and [0x…aa8, 0x…ab1) overlap`, `#2 rx_consume practice.c:79`, 두 주소 모두 `main` 프레임의 `'line'`. 고침: `memmove`.

</details>

### 연습 4 — 정적 블록 풀에 ASan 을 가르치기 (수동 poisoning)

펌웨어식 `g_pool[4][32]` 풀은 malloc 을 안 거치므로, 블록 0 을 넘어 블록 1 을 써도 ASan 은 모른다 (전부 같은 전역 배열 안). `pool_alloc(n)` 이 요청한 n 바이트만 열고, `pool_free` 가 블록을 다시 닫게 만들어라. 일반 빌드에서는 비용 0 이어야 한다.

<details>
<summary>답 보기</summary>

`<sanitizer/asan_interface.h>` 의 `ASAN_POISON_MEMORY_REGION` / `ASAN_UNPOISON_MEMORY_REGION` 을 `__has_feature(address_sanitizer)` (clang) / `__SANITIZE_ADDRESS__` (gcc) 일 때만 쓰고, 아니면 빈 매크로. `pool_init` 에서 풀 전체 poison, `pool_alloc(20)` 은 앞 20 바이트만 unpoison, `pool_free` 는 블록 32 바이트 poison. `pool_bad` 케이스(`a[20] = 0x33`) 의 실제 리포트 `asan_pool.txt`: `use-after-poison … WRITE of size 1 … case_pool practice.c:127`, `located 20 bytes inside of global variable 'g_pool' … of size 128`, 섀도 `00 00[04]f7 00 f7 f7 …` — `04` 는 20 = 16 + 4 바이트, `f7` = Poisoned by user. 블록 크기가 8 의 배수가 아니면 poison 경계가 칸 단위로 반올림된다는 점에 주의 (블록을 `_Alignas(8)` 로 정렬한 이유).

</details>

### 연습 5 — 한 번에 여러 오류 모으기 (생각해 볼 것)

`-fsanitize-recover=address` + `ASAN_OPTIONS=halt_on_error=0` 은 언제 쓰고, CI 에서는 왜 위험한가?

<details>
<summary>답 보기</summary>

큰 레거시 코드에 ASan 을 처음 켤 때, 한 번 돌려 오류 목록을 다 모으는 데 쓴다. 하지만 Part C-1 실측처럼 오류를 보고하고도 **exit 0** 으로 끝날 수 있고, 첫 오류 이후 상태는 이미 망가져 있어(레드존에 쓴 값 등) 뒤 보고는 연쇄 증상일 수 있다. CI 는 기본(halt) 모드 + 로그 grep. 보고 파일을 따로 남기려면 `log_path=…`.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```text
$ bash check.sh practice
PASS build -Werror (no sanitizer; POISON/UNPOISON compile to nothing)
PASS scope  (scope: tag=kind=2 n=6)
PASS ret  (ret: err=E042)
PASS overlap  (overlap: left=9 next=AT+GMR)
PASS pool  (pool: sum=340 reuse=blk0)
PASS pool_bad  caught: AddressSanitizer: use-after-poison practice.c:127 in case_pool
PASS scope/BUGGY  caught: AddressSanitizer: stack-use-after-scope practice.c:44 in frame_tag
PASS ret/BUGGY  caught: AddressSanitizer: stack-use-after-return practice.c:70 in status_line
PASS overlap/BUGGY  caught: AddressSanitizer: memcpy-param-overlap practice.c:79 in rx_consume
9/9 passed
[exit code 0]
```

## 10. 핵심 정리

- ASan = 컴파일러가 load/store 마다 섀도 검사 + 런타임이 malloc 레드존(`fa`) · 해제 표시(`fd`) · 격리 구역. 스택(`f1 f2 f3`) · 전역(`f9`) 레드존은 컴파일러가 배치.
- `Shadow = (Addr >> 3) + Offset` — 8 바이트당 1 바이트, `00` 전부 OK, `0k` 앞 k 바이트, 음수 금지. 맥 arm64 는 동적 오프셋(실측 `0x7000020000`), Linux aarch64 는 상수 `0x1000000000`.
- 리포트 읽는 순서: 종류 → READ/WRITE·크기 → `#0` → 위치 설명(`N bytes after M-byte region` / `[a, b) 'var'` / `global variable`) → (시간 버그) freed by · allocated by 를 거꾸로 이야기로.
- `0 bytes after` = off-by-one, 원인은 대개 `allocated by` 쪽 크기 계산.
- 맥 ASan 에는 LSan 이 없다 → `leaks --atExit` (exit 1 = 누수). Linux 는 LSan 기본 on, 결과는 보수적 스캔이라 하한.
- 진단 빌드: `-g -O1 -fno-omit-frame-pointer -fsanitize=address` (컴파일+링크), 프로세스당 케이스 하나, exit code + 로그 grep, recover 모드는 exit 0 함정.
- 비용(실측): 메모리 ≈13배, 접근 많은 루프 ≈3배, 코드 ≈2.4배 → Cortex-M 에는 못 올리고, 호스트 단위 테스트 + 타깃 MPU/카나리/가드 패턴으로 나눈다. 자체 풀은 수동 poisoning.

## 참고자료

- Clang AddressSanitizer 문서: https://clang.llvm.org/docs/AddressSanitizer.html
- LeakSanitizer: https://clang.llvm.org/docs/LeakSanitizer.html
- google/sanitizers wiki — AddressSanitizerAlgorithm (섀도 매핑, 레드존), AddressSanitizerFlags (`ASAN_OPTIONS`): https://github.com/google/sanitizers/wiki
- Serebryany et al., "AddressSanitizer: A Fast Address Sanity Checker", USENIX ATC 2012.
- GCC Instrumentation Options (`-fsanitize=address`, `-fsanitize-recover`): https://gcc.gnu.org/onlinedocs/gcc/Instrumentation-Options.html
- `man leaks`, `man malloc` (MallocStackLogging) — macOS.
- Linux kernel KASAN 문서: https://docs.kernel.org/dev-tools/kasan.html
- 관련 레슨: C-B11 (변수 위치), DS-M02 (고정 크기 풀), RTOS-M09 · BM-A07 (스택 오버플로 탐지), BM-A08 (MPU), TOOL-B02 (경고), TOOL-B10 (lldb), TOOL-B12 (UBSan), TOOL-M09 (TSan), TOOL-M10 (leaks · Guard Malloc · valgrind)
