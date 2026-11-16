---
id: TOOL-M14
time: 80분
updated: 2026-11-15
---
# TOOL-M14 디버그 정보와 심볼화: DWARF, strip, addr2line/atos

## 학습 목표

- `-g0 / -gline-tables-only / -g1 / -g / -g3` 가 **무엇을, 어디에** 남기는지 실측 표로 말한다. Mach-O 는 DWARF 를 `.o` 에 두고 실행 파일에는 "디버그 맵(debug map)"만 남기며 `dsymutil` 이 dSYM 으로 모은다. ELF 는 DWARF 를 실행 파일 안에 넣고 `objcopy --only-keep-debug` 로 떼어낸다.
- 스트립된(stripped) 바이너리의 크래시 PC 목록을 `atos -i -l <load address>` (맥) / `addr2line -f -i -e` (ELF) 로 `함수 (파일:줄)` 로 바꾸는 스크립트를 쓴다.
- 심볼화(symbolication)의 4대 함정을 실제 증상으로 안다: ASLR 슬라이드(slide) 무시, 리턴 주소에서 1을 안 뺌, 다른 빌드의 dSYM(UUID 불일치), 인라인 프레임 누락.
- DWARF 라인 테이블(line table)이 PC → 줄을 어떻게 찾는지, 인라인 프레임이 `DW_TAG_inlined_subroutine` 의 `DW_AT_call_line` 에서 나온다는 것을 `dwarfdump --lookup` 으로 직접 확인한다.
- 펌웨어 쪽: Cortex-M 의 Thumb 비트(lr | 1), EXC_RETURN, GNU build-id 와 `.gnu_debuglink`, 디버그 정보는 Flash 에 들어가지 않는다는 것을 실측으로 설명한다.

## 1. 문제

필드에서 돌아온 크래시 로그는 숫자뿐이다 — 출하 바이너리는 스트립돼 있고 주소는 매 실행 ASLR 로 밀려 있다. "이 주소 목록을 소스 줄로 바꿔 보세요" 는 펌웨어/플랫폼 면접의 단골이고, SSD 컨트롤러처럼 필드 로그만 남는 제품에서는 매주 하는 일이다.

```svg
<svg viewBox="0 0 760 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">빌드 1번 → 필드 크래시 → 심볼화 (crash_null.txt 실측 값)</text>
  <rect x="10" y="34" width="230" height="92" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">CI 빌드 (-g -O1)</text>
  <text x="20" y="74" fill="currentColor" font-family="monospace" font-size="12">crashgen (strip) → 출하</text>
  <text x="20" y="92" fill="currentColor" font-family="monospace" font-size="12">crashgen.dSYM  → 보관</text>
  <text x="20" y="112" fill="currentColor" font-size="12">열쇠 = UUID 4494D3AE-…</text>
  <rect x="265" y="34" width="230" height="92" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="275" y="54" fill="currentColor">필드 크래시 로그</text>
  <text x="275" y="74" fill="currentColor" font-family="monospace" font-size="12">load_address 0x10474c000</text>
  <text x="275" y="92" fill="currentColor" font-family="monospace" font-size="12">#0 pc 0x10474cfc4</text>
  <text x="275" y="112" fill="currentColor" font-family="monospace" font-size="12">#1 ret 0x10474cf14</text>
  <rect x="520" y="34" width="230" height="92" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="530" y="54" fill="currentColor">symbolicate</text>
  <text x="530" y="74" fill="currentColor" font-size="12">① UUID 3개 일치 확인</text>
  <text x="530" y="92" fill="currentColor" font-size="12">② 슬라이드 빼기 ③ #1부터 −1</text>
  <text x="530" y="112" fill="currentColor" font-size="12">④ atos -i (인라인 펼치기)</text>
  <path d="M240 80 L263 80" stroke="currentColor" stroke-width="2"/>
  <path d="M495 80 L518 80" stroke="currentColor" stroke-width="2"/>
  <rect x="10" y="146" width="740" height="76" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="166" fill="currentColor" font-family="monospace" font-size="12">파일 주소 = pc − load_address + 0x100000000 = 0x10474cfc4 − 0x10474c000 + 0x100000000 = 0x100000fc4</text>
  <text x="20" y="186" fill="currentColor" font-family="monospace" font-size="12">#1: 0x10474cf14 − 1 → 파일 주소 0x100000f13 (call 명령 안쪽 바이트)</text>
  <text x="20" y="206" fill="currentColor" font-size="12">DWARF: 라인 테이블 행 0x100000fc4 → 줄 39 / inlined_subroutine rd_le16 [0xfc4,0xfc8) call_line 64</text>
  <rect x="10" y="240" width="740" height="80" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="262" fill="currentColor" font-family="monospace" font-size="12">#0 rd_le16 (crashgen.c:39) [inlined]</text>
  <text x="20" y="280" fill="currentColor" font-family="monospace" font-size="12">#0 parse_record (crashgen.c:64)</text>
  <text x="20" y="298" fill="currentColor" font-family="monospace" font-size="12">#1 process_packet (crashgen.c:71)   #2 main (crashgen.c:233)   #3 ?? (not in image)</text>
  <text x="400" y="262" fill="currentColor" font-size="12">← 함수 + 줄, 인라인까지</text>
</svg>
```

실습 재료 (레슨 폴더):

| 파일 | 역할 |
|---|---|
| `crashgen.c` | 크래시 생성기. "센서 패킷 + IRQ" 파이프라인이 `null` / `panic` / `isr` 세 가지로 일부러 죽고, 시그널 핸들러가 **필드 장치처럼** 원시 PC + 로드 주소 + UUID 를 찍는다. `// @T 이름` 붙은 줄 = 정답(ground truth) |
| `crash_null.txt`, `crash_panic.txt`, `crash_isr.txt` | 스트립된 바이너리에서 실제로 받은 크래시 로그 (`demo_crashes.sh` 출력) |
| `starter.sh` / `solution.sh` / `practice.sh` | `symbolicate LOG BINARY DSYM` 과 도우미 함수 4개 (bash 3.2) |
| `check.sh` | 매번 새로 빌드 → dSYM 분리 → strip → **새 크래시 로그 3개** (ASLR 이 매번 다름) → 변형 스크립트 채점 |
| `demo_glevels.sh` / `demo_strip.sh` / `demo_dwarf.sh` / `demo_crashes.sh` → `glevels.txt` / `strip.txt` / `dwarf.txt` / `crashes.txt` | -g 레벨별 크기 표 · strip 단계와 UUID · 디버그 맵·라인 테이블·`dwarfdump --lookup` · 크래시 3개 심볼화 + **버그를 하나씩만 켰을 때** 증상 |
| `fw_m4.c`, `fw_m4.ld`, `demo_m4.sh` → `m4.txt` | Cortex-M4 ELF (맥 arm-none-eabi-gcc 16): Flash 크기, objcopy 분리, build-id, Thumb 비트 |
| `demo_vm.sh` → `vm.txt` | 같은 crashgen.c 를 Lima VM 의 aarch64 Linux PIE 로: addr2line, llvm-symbolizer, `.build-id` 디렉터리 |

### Part A — 디버그 정보는 어디에, 얼마나 있나

- `-g0 / -gline-tables-only / -g1 / -g / -g3` 로 빌드해 `.o` 크기, 실행 파일 크기, dSYM 의 `__debug_info / __debug_line / __debug_str` 크기를 표로 만든다. 코드(`__text`)는 변하는가?
- `strip -S / -x / strip` 후 심볼 수와 UUID, `dwarfdump --uuid` 로 바이너리 ↔ dSYM 짝 맞추기.
- `dwarfdump --debug-line` 발췌와 `dwarfdump --lookup <주소>` 로 "PC → 줄" 이 어떻게 계산되는지 본다.

### Part B — `symbolicate` 스크립트와 4개의 함정

`starter.sh` 의 TODO 4개를 고친다. 각각이 실제 현장에서 물리는 버그다: ① 로드 주소 대신 링크 주소(`text_vmaddr`)를 씀, ② 프레임 #1 이후에도 리턴 주소를 그대로 찾음, ③ dSYM 의 UUID 를 확인하지 않음, ④ 인라인 프레임을 펼치지 않음.

### Part C — 펌웨어 현실: ELF 분리, build-id, Thumb 비트

- Cortex-M4 이미지에서 `-g` 가 Flash(`.bin`)를 키우는지, `--only-keep-debug` / `--strip-debug` / `--add-gnu-debuglink` 결과, build-id 노트가 Flash 에 들어가는 위치.
- HardFault 덤프의 `lr = 0x080000bb` 같은 홀수 주소(Thumb 비트)와 `0xFFFFFFF9`(EXC_RETURN) 처리.
- 같은 crashgen.c 를 Linux ELF(PIE)로: `pc − load base`, `addr2line -f -i`, `llvm-symbolizer`, `/usr/lib/debug/.build-id/xx/…` 규칙.

## 2. Problem (English)

> **Symbolicate a field crash.**
>
> Release builds ship stripped; the debug info is archived separately (a `.dSYM` on macOS, a `.debug` file on ELF). A device sends back a crash log with the image UUID, the load address of `__TEXT` for that run (ASLR) and raw frame addresses:
>
> ```text
> image:        crashgen arm64 UUID 4494D3AE-806A-3080-AC03-E67AB5E705A8
> text_vmaddr:  0x0000000100000000
> load_address: 0x000000010474c000
> backtrace:
>   0   0x000000010474cfc4  crashgen
>   1   0x000000010474cf14  crashgen
>   2   0x000000010474c938  crashgen
>   3   0x000000018753e054  ?
> ```
>
> Write `symbolicate CRASH_LOG STRIPPED_BINARY DSYM` (bash 3.2, using `atos`) that prints one line per frame, inlined frames first:
>
> ```text
> #0 rd_le16 (crashgen.c:39) [inlined]
> #0 parse_record (crashgen.c:64)
> #1 process_packet (crashgen.c:71)
> #2 main (crashgen.c:233)
> #3 ?? 0x000000018753e054 (not in image)
> ```
>
> Requirements: honour the load address; frame 0 is the faulting pc but frames ≥ 1 are return addresses; refuse (exit 3, `UUID mismatch` on stderr, nothing on stdout) when the crash log, binary and dSYM UUIDs disagree; show inlined frames; frames outside the image print `??`.
>
> Follow-ups: what does `-g1` keep; why subtract 1 and not 4; why is the UUID different after a rebuild of the same source; how do you do this for a Cortex-M HardFault dump with `lr = 0x080000bb`; where does the build-id live in Flash?

## 3. 시작 코드

크래시 생성기 — 정답 줄에 `// @T` 표시 (check.sh 가 `grep -n` 으로 기대 줄 번호를 만든다):

<!-- code: crashgen.c -->

검사기 (모든 변형 공통 — 수정하지 않는다):

<!-- code: check.sh -->

시작 코드 — TODO 4개 (각 함수 위 주석이 요구사항):

<!-- code: starter.sh -->

```bash
bash check.sh starter        # 어디서 실행해도 됨. 작업은 /tmp/TOOL-M14_work/starter, 2–3 초
for d in glevels strip dwarf crashes m4; do bash demo_$d.sh > $d.txt; done   # 맥 (m4 = arm-none-eabi)
bash demo_vm.sh > vm.txt     # Lima VM elinux (gcc 15.2, binutils 2.46, llvm-symbolizer 21)
```

starter 실행 결과 (실제 출력, 주소는 실행마다 다르다):

```text
== build (cc = Apple clang version 21.0.0)
PASS  stripped binary has no parse_record symbol; binary = dSYM = crash log UUID 5245581C-BFD8-3237-9D3D-D4A58205D027
   crash_null.txt: load_address 0x0000000104008000  slide 0x0000000004008000  pc 0x0000000104008fc4
== helpers (starter.sh)
FAIL  crash_load_address = where __TEXT was loaded in this run (not text_vmaddr)  (got: 0x0000000100000000|)
FAIL  lookup_address: frame 0 keeps pc, frames >= 1 use return address - 1  (got: 0x100000fc4 0x100000f14 0x18753e054|)
PASS  uuid_check accepts the matching dSYM
FAIL  uuid_check rejects the v2 dSYM (wrong build)  (got: accepted|)
PASS  uuid_check rejects a crash log from the v2 binary
FAIL  atos_frame shows the inlined rd_le16 frame, then parse_record  (got: parse_record (crashgen.c:64)|)
== symbolicate: stripped binary + dSYM + fresh crash log, frame by frame
FAIL  null   #0 rd_le16 (crashgen.c:39) [inlined]  (got: #0 0x104008fc4)
…
FAIL  panic  #1 check_header (crashgen.c:58)  (got: #1 0x100adcfb8)
…
PASS  panic  #4 ?? ADDR (not in image)
…
FAIL  wrong dSYM -> exit 3 + UUID mismatch  (got rc=0, first line: #0 0x104008fc4)
PASS  lesson dir untouched (23 entries)
5/24 passed
```

starter 는 로드 주소부터 틀려서 `atos` 가 주소를 그대로 돌려준다(`#0 0x104008fc4`). 그래서 check.sh 는 도우미 함수 4개를 **따로** 검사한다 — 버그마다 자기 FAIL 줄이 있다. 버그를 하나씩만 켰을 때의 증상은 5. 풀이 Part B 에 있다.

## 4. 힌트

<details>
<summary>힌트 1 — 로그에 주소가 두 개 있다</summary>

`text_vmaddr` 는 링커가 정한 주소(arm64 실행 파일은 0x100000000), `load_address` 는 이번 실행에서 커널이 실제로 놓은 주소다. 둘의 차이가 슬라이드(slide)다. `atos -l` 은 어느 쪽을 원할까? `man atos` 의 `-l` 설명을 읽어 보자.

</details>

<details>
<summary>힌트 2 — 프레임 #1 의 주소는 "어디서 불렀나" 가 아니다</summary>

`bl` 은 lr 에 **다음 명령의 주소**를 넣는다. 그 명령은 다음 줄일 수도, 함수가 `noreturn` 호출로 끝나면 **다음 함수의 첫 명령**일 수도 있다. call 명령 안쪽의 아무 바이트나 가리키게 하려면? 프레임 #0 에는 왜 그러면 안 될까?

</details>

<details>
<summary>힌트 3 — 세 개의 UUID</summary>

크래시 로그가 자기 UUID 를 적어 보냈다. `dwarfdump --uuid` 는 바이너리와 dSYM 의 UUID 를 보여준다. 출하 바이너리와 로그는 당연히 맞는다 — 틀리기 쉬운 건 **세 번째**다.

</details>

<details>
<summary>힌트 4 — 한 주소가 함수 두 개</summary>

`-O1` 에서 `rd_le16` 은 `parse_record` 안으로 인라인된다. 주소 하나가 "rd_le16 의 39줄" 이면서 "parse_record 의 64줄" 이다. `atos` 의 옵션 목록에서 inline 을 찾아보자. 출력은 주소마다 여러 줄 + 빈 줄로 나뉜다.

</details>

## 5. 풀이

### Part A — 디버그 정보는 어디에, 얼마나

`demo_glevels.sh` 실측 (`glevels.txt`, Apple clang 21, `-O1`):

```text
level                      o_bytes   o_dbg exe_bytes dsym_dwarf    info   line    str   macro
-g0                           8400       0    34600     8350       0      0      1       0
-gline-tables-only           15088    5218    35592    13444    1097   1342    551       0
-g1                          15088    5218    35576    13444    1097   1342    551       0
-g                           27200   16790    35576    23940    4988   1847   2063       0
-g3                          27200   16790    35576    23940    4988   1847   2063       0
-g3 -fdebug-macro -gdwarf-4  289424  276159    35592   286086    6456   4893   2026  257502
```

읽는 법:

| 관찰 | 의미 |
|---|---|
| `-g1` 의 `.o` 와 dSYM 이 `-gline-tables-only` 와 바이트 수까지 같다 | clang 에서 `-g1` = 라인 테이블 + 함수 이름(서브프로그램)만. 변수·타입 정보 없음. 심볼화에는 충분하다 (아래 atos 결과) |
| `-g` 는 `__debug_info` 가 1097 → 4988 | 변수, 타입(struct packet …), 위치 표현식(location)이 추가 |
| `-g3` 가 `-g` 와 같다 | clang 은 `-g3` 만으로는 매크로 정보를 안 넣는다. `-fdebug-macro` 가 필요 (gcc 는 `-g3` 로 넣음 — 아래 VM 표) |
| `-fdebug-macro` 는 `-gdwarf-4` 를 같이 줬다 | 기본 DWARF 5 로는 이 clang 이 죽었다: `clang: error: unable to execute command: Segmentation fault: 11` / `clang: error: clang frontend command failed due to signal` (2줄 파일에서도 재현, 이 맥 실측). DWARF 4 로는 됐고 매크로 정보만 257 KB |
| 실행 파일(exe_bytes)은 거의 그대로 | **맥은 DWARF 를 실행 파일에 넣지 않는다.** 실행 파일엔 `.o` 경로를 가리키는 디버그 맵(stab)만 들어가고(-g0 대비 +976~992 B), DWARF 는 `.o` 에 남는다. `dsymutil` 이 그 `.o` 들을 읽어 dSYM 하나로 링크한다 |

코드는 레벨과 무관하다: 다섯 레벨 모두 `__text 2720  __cstring 467  __const 48  __unwind_info 112  __const 32  __bss 1060` (`glevels.txt` 둘째 부분).

심볼화 관점(같은 크래시의 프레임 #0, #1−1 을 `atos -i`): `-g0` 은 `parse_record (in crashgen) + 12` / `process_packet (in crashgen) + 27` 처럼 이름 + 오프셋뿐, `-gline-tables-only` 부터는 `rd_le16 (crashgen.c:39)` → `parse_record (crashgen.c:64)` → `process_packet (crashgen.c:71)` 로 -g 와 똑같다. `-g1`/`-gline-tables-only` 로도 줄 번호와 **인라인 프레임**이 나온다(인라인 정보는 라인 테이블 전용 모드에도 들어 있다). 없어지는 건 변수다 — lldb 실측:

```text
-- -gline-tables-only
       frame #0: 0x0000000100000fc0 crashgen`parse_record at crashgen.c:64:31 [opt]
   error: no variable information is available in debug info for this compile unit
-- -g
       frame #0: 0x0000000100000fc0 crashgen`parse_record(pk=0x0000000100004078) at crashgen.c:64:31 [opt]
   (const packet *) pk = 0x0000000100004078
   (unsigned int) id = <variable not available>
```

ELF 는 다르다. 같은 crashgen.c 를 VM 의 gcc 15.2 로 (`vm.txt`):

```text
level  file_bytes debug_sz     text
-g0       71936        0     6325
-g1       75936     3289     6325
-g        88680    15929     6325
-g3      256616   183761     6325
```

ELF 실행 파일은 `.debug_*` 섹션을 **자기 안에** 갖고 있어 파일이 커진다(-g3 는 디버그 섹션 합계 183 KB — -g 대비 늘어난 168 KB 가 매크로). `text`(코드+읽기전용)는 그대로 — 로드되는 세그먼트에 디버그 섹션이 없기 때문이다(`.debug_*` 는 `SHF_ALLOC` 이 없다).

#### 맥의 디버그 맵과 strip

`dwarf.txt` — 실행 파일 안의 stab 이 DWARF 의 위치를 가리킨다:

```text
000000006afa5ec7 - 00 0001   OSO /private/tmp/TOOL-M14_work/dwarf/crashgen.o
0000000100000f84 - 01 0000   FUN _check_header
0000000100000fb8 - 01 0000   FUN _parse_record
```

`OSO` 의 값 0x6afa5ec7 은 `.o` 의 수정 시각(mtime)이다. 이게 나중에 UUID 함정의 원인이 된다.

`strip.txt`:

```text
file                    bytes   nm  stabs  local  uuid
crashgen                35576   33     23      8  5C0E25C6-4065-36B7-BDD8-FD8E83866E17
s_S/crashgen            34616   33      0      8  5C0E25C6-4065-36B7-BDD8-FD8E83866E17
s_x/crashgen            34200   17      0      0  5C0E25C6-4065-36B7-BDD8-FD8E83866E17
s_all/crashgen          34184   16      0      0  5C0E25C6-4065-36B7-BDD8-FD8E83866E17
   dSYM bundle: 32 KB on disk, DWARF file 23936 bytes
```

- `strip -S`: 디버그 stab 만 제거(로컬 심볼 8개는 남음). `strip -x`: 로컬(`t`) 심볼도 제거 — `static` 함수 이름이 사라진다. 그냥 `strip`: dyld 에 필요 없는 건 다.
- **UUID 는 strip 해도 그대로** (`LC_UUID` 는 링크 때 박힘). 그래서 스트립된 출하 바이너리와 strip 전에 만든 dSYM 이 짝이 된다.
- dSYM 없이 스트립된 바이너리만 있으면:

```text
-- atos -o <fully stripped exe>, no dSYM anywhere near it:
0x0000000100000fc4 (in crashgen) + 12
0x0000000100000f13 (in crashgen) + 27
-- why '+ 12' without a name: LC_FUNCTION_STARTS survives strip (dyld_info -function_starts):
   0x100000EF8  __TEXT,__text+2400
   0x100000FB8  __TEXT,__text+2592
```

이름은 없지만 "함수 시작 + 12" 까지는 안다: 0x100000fc4 − 0x100000fb8 = 12, 0x100000f13 − 0x100000ef8 = 27. `LC_FUNCTION_STARTS` 가 strip 뒤에도 남아 있기 때문이다 (오프셋이 정확히 맞는 것으로 확인; atos 가 이걸 쓴다는 건 추정).

#### DWARF 가 PC 를 줄로 바꾸는 방법

라인 테이블은 "주소 → (파일, 줄, 열, 플래그)" 행의 나열이다. 실제로 저장된 건 행 자체가 아니라 그 행들을 만들어 내는 작은 바이트코드(상태 기계, `DW_LNS_advance_pc`, `advance_line`…)이고, `dwarfdump --debug-line` 은 그걸 실행한 결과를 보여준다 (`dwarf.txt`):

```text
Address            Line   Column File   ISA Discriminator OpIndex Flags
0x0000000100000fa8     60      1      0   0             0       0  is_stmt
0x0000000100000fb4     58      9      0   0             0       0  is_stmt
0x0000000100000fb8     63      0      0   0             0       0  is_stmt
0x0000000100000fc0     64     31      0   0             0       0  is_stmt prologue_end
0x0000000100000fc4     39     28      0   0             0       0  is_stmt
0x0000000100000fc8     39     28      0   0             0       0
0x0000000100000fcc     65     15      0   0             0       0  is_stmt
```

조회 규칙: **주소 ≤ PC 인 마지막 행**이 답이다 (다음 행 주소 전까지가 그 행의 범위). 0x100000fc4 → 39행. 그런데 39행은 `rd_le16` 안이다 — 함수 이름은 라인 테이블이 아니라 `.debug_info` 의 트리에서 온다:

```text
== dwarfdump --lookup 0x100000fc4  (null crash, frame 0, after subtracting the slide)
0x00001108:   DW_TAG_inlined_subroutine
                DW_AT_abstract_origin	(0x0000118f "rd_le16")
                DW_AT_low_pc	(0x0000000100000fc4)
                DW_AT_high_pc	(0x0000000100000fc8)
                DW_AT_call_line	(64)
                DW_AT_call_column	(19)
Line info: file 'crashgen.c', line 39, column 28, start file 'crashgen.c', start line 37
```

```svg
<svg viewBox="0 0 760 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">PC 0x100000fc4 하나 → 프레임 두 줄 (dSYM 실측)</text>
  <rect x="10" y="34" width="360" height="190" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="20" y="54" fill="currentColor">.debug_info (트리)</text>
  <rect x="24" y="64" width="332" height="148" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="34" y="84" fill="currentColor" font-family="monospace" font-size="12">DW_TAG_subprogram parse_record</text>
  <text x="34" y="102" fill="currentColor" font-family="monospace" font-size="12">  low_pc 0xfb8  high_pc 0xfd8</text>
  <rect x="40" y="114" width="300" height="88" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="50" y="134" fill="currentColor" font-family="monospace" font-size="12">DW_TAG_inlined_subroutine</text>
  <text x="50" y="152" fill="currentColor" font-family="monospace" font-size="12">  origin rd_le16</text>
  <text x="50" y="170" fill="currentColor" font-family="monospace" font-size="12">  low_pc 0xfc4  high_pc 0xfc8</text>
  <text x="50" y="188" fill="currentColor" font-family="monospace" font-size="12">  call_line 64  (call_column 19)</text>
  <rect x="390" y="34" width="360" height="110" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="400" y="54" fill="currentColor">.debug_line (주소 ≤ PC 인 마지막 행)</text>
  <text x="400" y="76" fill="currentColor" font-family="monospace" font-size="12">0xfc0  line 64  prologue_end</text>
  <text x="400" y="96" fill="currentColor" font-family="monospace" font-size="12" font-weight="bold">0xfc4  line 39   ← PC</text>
  <text x="400" y="116" fill="currentColor" font-family="monospace" font-size="12">0xfcc  line 65</text>
  <text x="400" y="134" fill="currentColor" font-size="12">(주소는 0x100000xxx 의 끝 3자리)</text>
  <rect x="390" y="156" width="360" height="68" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="400" y="178" fill="currentColor" font-family="monospace" font-size="12">#0 rd_le16 (crashgen.c:39) [inlined]</text>
  <text x="400" y="198" fill="currentColor" font-family="monospace" font-size="12">#0 parse_record (crashgen.c:64)</text>
  <text x="400" y="216" fill="currentColor" font-size="12">안쪽 줄 = 라인 테이블, 바깥 줄 = call_line</text>
  <path d="M340 160 L388 190" stroke="currentColor" stroke-width="2"/>
  <path d="M600 102 L600 154" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="248" fill="currentColor" font-size="12">여러 겹 인라인 = inlined_subroutine 중첩, 겹마다 call_line 한 줄 (atos -i, addr2line -i)</text>
</svg>
```

lldb 도 같은 결론 (`image lookup -v -a`): `Summary: crashgen`parse_record + 12 [inlined] rd_le16 at crashgen.c:39:28`.

### Part B — 함정 4개, 하나씩만 켜 보기

`demo_crashes.sh` 는 solution.sh 를 쓰되 함수 하나만 starter 버전으로 바꿔 돌린다 (`crashes.txt`, 출력 옆 `<-` 주석은 설명용으로 붙인 것). 정답 먼저:

```text
---- symbolicate (solution.sh)          [crash_panic]
#0 fw_panic (crashgen.c:51)
#1 check_header (crashgen.c:58)
#2 process_packet (crashgen.c:70)
#3 main (crashgen.c:234)
#4 ?? 0x000000018753e054 (not in image)
```

**버그 1 — ASLR 슬라이드 무시.** arm64 macOS 실행 파일은 PIE 라서 `__TEXT` 가 매번 다른 곳에 놓인다 (check.sh 를 돌릴 때마다 `load_address` 가 바뀌는 걸 볼 수 있다: 0x104008000, 0x1024e8000, 0x104fb8000, …). `-l` 에 링크 주소 0x100000000 을 주면 atos 는 "이 이미지에 없는 주소" 라며 숫자를 그대로 돌려준다:

```text
---- BUG 1 only: load address = text_vmaddr (ASLR slide ignored)   [crash_null]
#0 0x10474cfc4
#1 0x10474cf13
#2 0x10474c937
```

같은 답을 내는 세 방법 (`strip.txt` 실측): `atos -l <load_address>`, `atos -s <slide>`, 또는 직접 `pc − load_address + 0x100000000` 을 계산해 옵션 없이 주기 — 셋 다 `parse_record (in crashgen) (crashgen.c:64)`. ELF/Linux PIE 도 똑같다: `addr2line` 은 파일 주소를 원하므로 `pc − load base` (VM 실측: 0xbb4). 빼지 않으면 `?? ??:0`. Cortex-M 은 보통 ASLR 이 없어서 이 단계가 사라진다 (그래서 펌웨어 엔지니어가 맥/리눅스에서 처음 물린다).

**버그 2 — 리턴 주소에서 1을 안 뺌.** 프레임 #1 이후의 값은 "부른 곳" 이 아니라 "돌아올 곳" = call 다음 명령이다:

```svg
<svg viewBox="0 0 760 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">noreturn 호출이 함수의 마지막 명령일 때 (otool -tv + dwarfdump 실측)</text>
  <rect x="10" y="40" width="430" height="60" class="f-acc" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-family="monospace" font-size="12">check_header  [0x100000f84, 0x100000fb8)</text>
  <text x="20" y="84" fill="currentColor" font-family="monospace" font-size="12">… stp x29,x30 / mov x29,sp / 0xfb4: bl _fw_panic</text>
  <rect x="440" y="40" width="310" height="60" class="f-mut" stroke="currentColor"/>
  <text x="450" y="62" fill="currentColor" font-family="monospace" font-size="12">parse_record [0xfb8, 0xfd8)</text>
  <text x="450" y="84" fill="currentColor" font-family="monospace" font-size="12">0xfb8: stp x29,x30,…</text>
  <path d="M440 110 L440 140" stroke="currentColor" stroke-width="2"/>
  <text x="380" y="158" fill="currentColor" font-family="monospace" font-size="12">lr = 0xfb8</text>
  <path d="M432 110 L405 140" stroke="currentColor" stroke-width="2"/>
  <rect x="10" y="172" width="360" height="66" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="20" y="194" fill="currentColor" font-family="monospace" font-size="12">lr − 1 = 0xfb7 (bl 의 마지막 바이트)</text>
  <text x="20" y="214" fill="currentColor" font-family="monospace" font-size="12">→ check_header (crashgen.c:58)  맞음</text>
  <rect x="390" y="172" width="360" height="66" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="400" y="194" fill="currentColor" font-family="monospace" font-size="12">lr 그대로 = 0xfb8</text>
  <text x="400" y="214" fill="currentColor" font-family="monospace" font-size="12">→ parse_record (crashgen.c:63)  함수부터 틀림</text>
</svg>
```

```text
---- BUG 2 only: no '- 1' for return addresses                      [crash_panic]
#0 fw_panic (crashgen.c:51)
#1 parse_record (crashgen.c:63)        <- 실제로는 check_header 58행에서 불렀다
#2 process_packet (crashgen.c:71)      <- 실제로는 70행 (check_header 호출), 71행은 다음 줄
#3 main (crashgen.c:234)
```

`fw_panic` 이 `noreturn` 이라 컴파일러는 `bl _fw_panic` 뒤에 아무것도 두지 않았다. 그래서 리턴 주소 0xfb8 은 **다음 함수의 첫 명령**이다 (`dwarfdump --lookup 0x100000fb8` → `DW_AT_name ("parse_record")`, `0x100000fb7` → `("check_header")`, line 58). #2 는 더 흔한 경우: 호출 다음 명령이 다음 줄에 속해 줄 번호만 하나 밀린다. 이건 크래시 로그 리뷰에서 "71행이 왜 죽어요? 그냥 대입인데" 라는 헛수고를 만든다.

왜 −1 이지 −4 가 아닌가: 목표는 "call 명령 안쪽의 아무 바이트" 다. AArch64 는 명령이 4바이트 고정이라 −4 도 되지만, Thumb 은 `bl` 이 4바이트, `blx rN` 은 2바이트라 −4 는 엉뚱한 명령에 떨어질 수 있다. −1 은 길이와 상관없이 항상 call 명령 안이다. 디버거(lldb/gdb)와 sanitizer 백트레이스도 리턴 주소 프레임은 이렇게 조회한다(확인 권장). 반면 atos·addr2line 은 넣은 주소를 그대로 찾는다 — 빼는 건 호출하는 쪽 책임 (atos: 0xfb8 → parse_record, addr2line: 0x80000ba → 41행, 실측).

반대로 **프레임 #0 에서도 빼면** 틀린다 — #0 은 fault 가 난 명령 그 자체다:

```text
---- BUG 2b: '- 1' applied to frame 0 too                            [crash_null]
#0 parse_record (crashgen.c:64)        <- rd_le16 인라인 프레임과 39행이 사라졌다
#1 process_packet (crashgen.c:71)
```

0xfc4 − 1 = 0xfc3 은 앞 명령(0xfc0, 64행)의 마지막 바이트라 인라인 범위 [0xfc4, 0xfc8) 밖이다. (신호 핸들러·트램펄린을 거친 프레임처럼 "#0 이 아닌데 fault 위치" 인 경우는 크래시 리포터가 프레임 종류를 따로 기록해 구분한다 — 이 레슨 범위 밖, 확인 권장.)

**버그 3 — 다른 빌드의 dSYM.** 결과가 "그럴듯하게" 틀린다는 게 무서운 점이다:

```text
---- BUG 3 only: dSYM UUID never compared, v2 dSYM used              [crash_panic]
#0 fw_panic (crashgen.c:49)
#1 check_header (crashgen.c:59)
#2 on_crash (crashgen.c:149)           <- 크래시 핸들러가 크래시를 일으켰다?!
#3 main (crashgen.c:234)
---- fixed: wrong dSYM is refused
error: UUID mismatch: crash log 4494D3AE-806A-3080-AC03-E67AB5E705A8, binary 4494D3AE-806A-3080-AC03-E67AB5E705A8, dSYM 3C85F1EA-F9C9-3E93-AECA-367473635F08
exit 3
```

v2 는 `main` 에 호출 하나(`setvbuf`)를 더한 것뿐인데, `main` 이 `__text` 맨 앞에 있어서 뒤의 함수가 전부 밀렸다. 같은 주소가 v2 dSYM 에서는 다른 함수를 가리킨다.

더 중요한 사실 — **같은 소스를 다시 빌드해도 UUID 가 바뀐다** (`strip.txt`):

```text
   rebuild 1: 6C3F49FA-E03D-3E3E-BFF2-7393BF3EEDCA  OSO mtime 000000006afa5eca  __text md5 994d598cef9cb7eef850f248a0073a52
   rebuild 2: ABC0E355-19D0-36A1-921A-7FCC4AD2ECF2  OSO mtime 000000006afa5ecb  __text md5 994d598cef9cb7eef850f248a0073a52
   ZERO_AR_DATE=1 rebuild 1: 6F22AD62-C6FB-3C3E-8F67-115530305989  OSO mtime 0000000000000000
   ZERO_AR_DATE=1 rebuild 2: 6F22AD62-C6FB-3C3E-8F67-115530305989  OSO mtime 0000000000000000
```

코드(`__text` md5)는 같은데 1초 차이 빌드의 UUID 가 다르다. 달라진 건 디버그 맵의 `OSO` mtime 뿐이고, `ZERO_AR_DATE=1` 로 mtime 을 0 으로 만들면 UUID 가 같아졌다(실측) — ld64 의 UUID 해시가 디버그 맵까지 포함하는 것으로 보인다. 결론: **"같은 커밋을 다시 빌드해서 dSYM 을 만들면 된다" 는 맥에서는 통하지 않는다.** dSYM 은 그 빌드에서 바로 만들어 UUID 로 보관해야 한다 (재현 가능 빌드는 TOOL-A06). 비교로, VM 의 gcc + GNU ld build-id 는 내용 해시라 두 번 돌린 `demo_vm.sh` 에서 같은 값(a35c0a7f…)이 나왔다.

관찰된 함정 하나 더: dSYM 과 `.o` 를 다른 곳으로 옮긴 뒤에도 **이미 한 번 조회한 UUID** 에 대해서는 atos 가 줄 번호를 계속 돌려줬다 (새 UUID 의 바이너리에서는 `.o` 를 지우자 `parse_record (in x) + 12` 로 떨어짐). 어딘가에 캐시하거나 Spotlight 로 dSYM 을 찾는 것으로 보인다 — 원인은 확인 못 함. 그러니 "되네?" 가 아니라 UUID 를 직접 비교하는 게 맞다.

**버그 4 — 인라인 프레임 누락.**

```text
---- BUG 4 only: atos without -i                                     [crash_isr]
#0 uart_rx_isr (crashgen.c:79)         <- 실제 fault 는 rb_put 의 44행 (rb->buf[rb->head & 15u] = b)
#1 dispatch_irq (crashgen.c:88)
```

`-i` 없이 atos 는 바깥 함수와 그 함수 안의 호출 줄(79)만 준다. 틀린 건 아니지만 "rb_put 의 어느 줄?" 이 빠진다. 인라인은 `-O1` 이상, 특히 `static inline` 헬퍼·LTO 에서 흔하다 — 펌웨어의 레지스터 접근 헬퍼가 대개 그렇다.

#### 크래시 생성기가 프레임을 모으는 방법 (그리고 내가 실제로 밟은 버그)

`on_crash` 는 `ucontext` 에서 pc/lr/fp/sp 를 읽고, 프레임 레코드(frame record) 사슬을 따라간다: `[fp]` = 호출자의 fp, `[fp+8]` = 저장된 lr. 모든 함수가 레코드를 만들도록 `-fno-omit-frame-pointer -mno-omit-leaf-frame-pointer` 를 주고, 꼬리 호출이 프레임을 지우지 않게 `-fno-optimize-sibling-calls` 를 줬다 (꼬리 호출이 되면 그 함수는 백트레이스에서 그냥 사라진다). 처음에는 PAC 비트를 지우려고 `& 0x00007fffffffffff`(47비트)로 마스킹했는데, 맥에서는 멀쩡하던 것이 VM 에서 `lr 0x000038361c02117c`(실제는 0x0000b836…)가 되어 프레임 1부터 `?` 로 나왔다. 이 VM 의 aarch64 Linux 유저 주소는 48비트를 쓴다 → 마스크를 48비트로 고쳤다. 크래시 경로 코드는 테스트하기 어려워서 이런 버그가 필드에서야 드러난다.

### Part C — 펌웨어: ELF 분리, build-id, Thumb 비트

`m4.txt` (맥 arm-none-eabi-gcc 16.2.0, `-O1 -nostdlib`, `fw_m4.ld`):

```text
level  elf_bytes debug_sz     text bin_bytes
-g0        5528        0      236      236
-g1        7152     1210      236      236
-g         8564     2477      236      236
-g3       27696    21542      236      236
   cmp fw-g0.bin fw-g3.bin: fw-g0.bin fw-g3.bin differ: char 81, line 1; differing bytes 81-100 (20 bytes)
   build-id -g0 5b47fde7f50af8db0b21e13ec0dda551f6067083  -g3 2e6d3b2bad336c9d8348d899a1ca71afa8df18f5
   only .isr_vector+.text: identical at every -g level
```

- Flash 에 굽는 `.bin` 크기는 -g 레벨과 무관(236 B). "디버그 빌드라 Flash 가 모자라요" 는 `-g` 가 아니라 `-O0` 탓이다 (TOOL-B03, TOOL-M13).
- 그런데 `.bin` 내용은 81–100번째 바이트, 정확히 20바이트가 다르다. 0x40 부터가 build-id 노트(아래)이고 그 안의 sha1 20바이트(파일 오프셋 0x50–0x63)다. 코드가 같은데 build-id 가 다르다 = **GNU ld 의 build-id 해시에 디버그 섹션도 들어간다** (실측으로 본 결과; 해시 범위의 정확한 정의는 ld 문서 확인 권장). 코드(`.isr_vector`+`.text`)만 떼어 비교하면 같다.

분리 (GNU 방식):

```text
   fw.elf         8564 bytes  build-id f997728e3b6701be6f3276260518ea61e871dd7f   debug sections 8
   fw.debug       8428 bytes  build-id f997728e3b6701be6f3276260518ea61e871dd7f   debug sections 8
   fw.stripped    5588 bytes  build-id f997728e3b6701be6f3276260518ea61e871dd7f   debug sections 0
   fw.stripall    4796 bytes  build-id f997728e3b6701be6f3276260518ea61e871dd7f   debug sections 0
-- readelf -S fw.debug (code sections become NOBITS: addresses kept, bytes dropped)
   [ 3] .text             NOBITS          08000064 001064 000088 00  AX  0   0  4
-- objdump -s -j .gnu_debuglink fw.stripped (file name, pad to 4, CRC32 of fw.debug)
    0000 66772e64 65627567 00000000 39dfc72a  fw.debug....9..*
-- build-id lives in Flash: note section at 0x08000040, first bytes of fw.bin after the vector table:
   00000040: 0400 0000 1400 0000 0300 0000 474e 5500  ............GNU.
   00000050: f997 728e 3b67 01be 6f32 7626 0518 ea61  ..r.;g..o2v&...a
```

| 명령 | 결과 | 맥 대응 |
|---|---|---|
| `objcopy --only-keep-debug fw.elf fw.debug` | 디버그 섹션 + 심볼, 코드 섹션은 NOBITS(주소만) | dSYM (`dsymutil`) |
| `objcopy --strip-debug --add-gnu-debuglink=fw.debug` | 디버그 제거 + "fw.debug, CRC32 39dfc72a" 링크 | `strip -S` (+ dSYM 을 UUID 로 찾음) |
| `objcopy --strip-all` | 심볼까지 제거 | `strip` |
| `.note.gnu.build-id` (NT_GNU_BUILD_ID=3, 20바이트 sha1) | Flash 0x08000040 에 들어감 → 펌웨어가 크래시 로그에 자기 build-id 를 실을 수 있다 | `LC_UUID` |

노트 레이아웃이 그대로 보인다: namesz=4, descsz=0x14, type=3, "GNU\0", 그 뒤 20바이트 = readelf 의 build-id. 펌웨어 크래시 덤프에 이 20바이트(또는 앞 8바이트)를 넣어 두면 호스트 디코더가 CI 아카이브에서 정확한 `fw.elf` 를 찾는다 (덤프 포맷 설계는 TOOL-A05, 빌드 ID 정책은 TOOL-A06).

addr2line 이 링크를 따라가는지:

```text
-- fw.stripped:
   rd_be16 at fw_m4.c:15
    (inlined by) parse_frame at fw_m4.c:33
-- fw.stripped after moving fw.debug away (the debuglink can't be followed):
   parse_frame at ??:?
-- fw.stripall:
   ?? ??:0
```

`fw.stripped` 에는 디버그 정보가 없는데 줄이 나온다 — 같은 폴더의 `fw.debug` 를 debuglink 로 찾았기 때문. 치우면 심볼 테이블의 함수 이름만, `--strip-all` 이면 그것도 없다.

**Thumb 비트.** Cortex-M 은 Thumb 전용이라 lr 과 함수 포인터의 bit0 = 1 이다. HardFault 덤프의 lr 을 그대로 넣으면 (`m4.txt`):

```text
    80000b0:	f7ff ffe2 	bl	8000078 <check_crc>          fw_m4.c:39
    80000b6:	f7ff ffed 	bl	8000094 <parse_frame>        fw_m4.c:40
    80000ba:	4a02      	ldr	r2, [pc, #8]                fw_m4.c:41
== a stacked lr of 0x80000bb (bl <parse_frame> at 0x80000b6, +4, Thumb bit set) looked up 3 ways
   addr2line 0x80000bb  -> sensor_task fw_m4.c:41
   addr2line 0x80000ba  -> sensor_task fw_m4.c:41
   addr2line 0x80000b9  -> sensor_task fw_m4.c:40
```

(objdump 줄은 `demo_m4.sh` 의 `-d -l` 출력을 한 줄씩 줄여 옮김.) 비트를 지우는 것만으로는 부족하고, 지운 뒤 −1 까지 해야 40행이 나온다: `(lr & ~1) − 1`. 프레임 #0 의 stacked pc 는 원래 bit0 = 0 이지만, `& ~1` 은 무해하니 lr·함수 포인터 값과 같은 함수로 처리하려고 항상 한다. lr 이 `0xFFFFFFF9/FD/F1/E9…` 이면 그건 주소가 아니라 EXC_RETURN — "여기서 예외 진입" 표시다 (ARM-M12, ARM-M14). practice.sh 의 `m4_frame` 이 이 셋을 처리한다.

**Linux ELF (VM).** 같은 crashgen.c, gcc 15.2, PIE. 로그는 `load_address: 0x0000ada2d4100000`, `#0 0x0000ada2d4100bb4`, `#1 0x0000ada2d410117c` … (`vm.txt`):

```text
   file addresses: 0xbb4 0x117b 0x1597 0xaaf
-- addr2line -f -i -p -s -e crashgen.debug
   rd_le16 at crashgen.c:39
    (inlined by) parse_record at crashgen.c:65
   process_packet at crashgen.c:71
   main at crashgen.c:233 (discriminator 2)
   _start at ??:?
-- crashgen.stripall (no debuglink), crashgen.debug moved away, --debug-file-directory=dbg:
   rd_le16
   crashgen.c:39:28
```

- gcc 는 두 번째 `rd_le16` 호출(65행)의 `p[1]` 을 먼저 읽도록 순서를 바꿨다 (VM `objdump -d`: 0xbb4 `ldrb w1, [x2, #3]`, fault_addr 0x3 과 일치). clang 은 첫 호출(64행)에서 죽는다. **같은 소스라도 컴파일러가 다르면 정답 줄이 다르다** — 그래서 기대값을 "빌드한 바이너리 기준" 으로 만들어야 한다.
- `.build-id/a3/5c0a7f….debug` 경로 규칙: gdb, llvm-symbolizer(`--debug-file-directory`), debuginfod 가 build-id 로 디버그 파일을 찾는다. 디렉터리 없이 `--strip-all` 파일만 주면 `??`.

solution 실제 출력 (발췌 — 검사 24개 중 일부):

```text
== helpers (solution.sh)
PASS  crash_load_address = where __TEXT was loaded in this run (not text_vmaddr)
PASS  lookup_address: frame 0 keeps pc, frames >= 1 use return address - 1
PASS  uuid_check rejects the v2 dSYM (wrong build)
PASS  atos_frame shows the inlined rd_le16 frame, then parse_record
== symbolicate: stripped binary + dSYM + fresh crash log, frame by frame
PASS  null   #0 rd_le16 (crashgen.c:39) [inlined]
PASS  panic  #1 check_header (crashgen.c:58)
PASS  isr    #0 rb_put (crashgen.c:44) [inlined]
…
PASS  wrong dSYM -> exit 3, 'UUID mismatch' on stderr, no frames printed
24/24 passed
```

## 6. 정답 코드

<!-- code: solution.sh diff=starter.sh -->

바뀐 네 줄:

| TODO | starter | solution | 왜 |
|---|---|---|---|
| 1 | `crash_field "$1" text_vmaddr` | `crash_field "$1" load_address` | atos `-l` 은 "이번 실행에서 이미지가 놓인 주소" |
| 2 | 모든 프레임 그대로 | #0 그대로, #1부터 −1 | 리턴 주소 = call 다음 명령 |
| 3 | 로그 ↔ 바이너리만 비교 | 로그 = 바이너리 = dSYM | 틀리는 건 보관소에서 꺼낸 dSYM 쪽 |
| 4 | `atos -o …` | `atos -i -o …` | 인라인 체인 출력 |

## 7. 자주 하는 실수

1. **크래시 로그의 pc 를 `atos -o X.dSYM` 에 그대로 넣기.** → 슬라이드 때문에 이미지 밖 주소 → atos 가 숫자를 돌려줌. 고침: `-l <load_address>` (또는 `-s <slide>`), ELF PIE 는 `pc − base`.
2. **`lldb` 에서는 되는데 크래시 로그에서는 안 맞음.** → lldb 는 기본으로 ASLR 을 끄고 실행해서 이미지가 0x100000000 에 놓인다 (ARM-M14 에서 본 것). 필드 로그는 슬라이드가 있다. 고침: 로그의 로드 주소 사용.
3. **모든 프레임에 −1 (또는 아무 프레임에도 안 함).** → #0 에서 빼면 인라인 프레임이 사라지고(BUG 2b), #1 이후에 안 빼면 noreturn 호출 뒤에서 함수까지 틀린다. 고침: `if frame == 0: pc else pc − 1`, Thumb 은 `(lr & ~1) − 1`.
4. **"같은 태그로 다시 빌드해서 dSYM 만들면 되지".** → 맥은 OSO mtime 때문에 UUID 가 바뀌고(실측), ELF 도 경로·시간이 들어가면 build-id 가 바뀐다. 고침: CI 가 빌드 직후 dSYM/`.debug` 를 UUID/build-id 키로 보관.
5. **릴리스를 `-g0` 로 빌드.** → 크래시 로그를 영영 못 읽는다. 디버그 정보는 Flash/코드 크기에 0 바이트(실측). 고침: 릴리스도 `-g`(최소 `-gline-tables-only`)로 빌드하고 출하물만 strip.
6. **Cortex-M lr 을 그대로 addr2line.** → bit0 = 1 이라 다음 줄(41행). 고침: `& ~1` 후 −1. `0xFFFFFFxx` 는 EXC_RETURN.
7. **스트립된 ELF 가 줄 번호를 보여줘서 "strip 안 됐네?" 착각.** → `.gnu_debuglink` 가 옆의 `.debug` 를 찾은 것. `readelf -S | grep debug_info` 로 확인.

## 8. 면접 Q&A

<details>
<summary>Q1. 스트립된 바이너리의 크래시 주소를 소스 줄로 바꾸는 절차를 말해 보세요.</summary>

① 로그의 이미지 식별자(UUID / build-id)로 그 빌드의 디버그 파일(dSYM / `.debug` / 스트립 전 ELF)을 찾는다 — 불일치면 멈춘다. ② 프레임 주소를 파일 주소로: `pc − load_address + 링크 주소`(맥은 `atos -l` 이 해 줌, Linux PIE 는 `pc − base`, MCU 는 보통 그대로). ③ 프레임 #1 이후는 리턴 주소라 −1 (Thumb 은 `& ~1` 후 −1). ④ `atos -i` / `addr2line -f -i` / `llvm-symbolizer` 로 인라인 체인까지. 꼬리: "프레임 #0 에도 −1 하면?" → fault 명령이 아닌 앞 명령을 보게 돼 인라인 프레임이나 줄이 틀린다(실측 BUG 2b).

</details>

<details>
<summary>Q2. -g1, -gline-tables-only, -g, -g3 차이는? 릴리스에는 뭘 쓰나요?</summary>

clang: `-g1` = `-gline-tables-only`(이 맥에서 `.o` 크기까지 동일) — 라인 테이블 + 함수/인라인 정보, 변수·타입 없음. `-g` = 변수·타입·위치 표현식 추가(`__debug_info` 1097 → 4988 B). `-g3` 는 매크로 정보인데 clang 은 `-fdebug-macro` 가 있어야 넣는다(gcc 는 `-g3` 로 넣음: VM 에서 88 KB → 257 KB). 코드는 레벨과 무관(`__text` 2720 B 동일). 릴리스: 최소 라인 테이블, 보통 `-g` 로 빌드 후 strip + 디버그 파일 보관. 꼬리: "그럼 왜 다들 -g 를 무서워하죠?" → 맥은 dSYM, ELF 는 파일 크기·링크 시간·CI 저장공간 비용이지 런타임 비용이 아니다.

</details>

<details>
<summary>Q3. 리턴 주소에서 왜 1을 빼나요? 4가 아니고?</summary>

`bl` 이 lr 에 넣는 건 다음 명령 주소다. 그 명령은 다음 줄일 수 있고, 호출이 `noreturn` 이면 함수가 거기서 끝나 **다음 함수의 첫 명령**일 수 있다 — 실측: `check_header` 의 `bl _fw_panic`(0xfb4) 다음 0xfb8 은 `parse_record` 시작, 그대로 조회하면 `parse_record:63`. −1 은 명령 길이와 상관없이 call 명령 안쪽 바이트라 안전하다. Thumb 은 `bl` 4B, `blx r` 2B 로 길이가 섞여 −4 가 틀릴 수 있다. 꼬리: "디버거는?" → lldb/gdb 도 리턴 주소 프레임은 pc−1 로 줄을 찾는다(확인 권장).

</details>

<details>
<summary>Q4. 같은 커밋을 다시 빌드하면 예전 크래시 로그를 심볼화할 수 있나요?</summary>

코드가 바이트 단위로 같으면 주소는 맞지만, 식별자가 달라 "같은 빌드" 임을 증명할 수 없다. lldb 는 UUID 로 dSYM 을 고르고, `atos -o X.dSYM` 은 준 파일을 확인 없이 쓴다(BUG 3 실측: v2 dSYM 으로 `on_crash` 가 나옴). 이 맥에서 같은 소스를 1초 차이로 두 번 빌드하니 `__text` md5 는 같고 UUID 는 달랐다 — 디버그 맵의 OSO mtime 이 UUID 해시에 들어가는 것으로 보인다(`ZERO_AR_DATE=1` 이면 같아짐, 실측). 그래서 정답은 "빌드할 때 dSYM 을 만들어 UUID 로 보관". ELF 는 build-id 가 내용 해시라 재현 가능 빌드면 같아질 수 있다 (VM 에서 두 번 같은 값; 경로·시간 제거는 TOOL-A06).

</details>

<details>
<summary>Q5. Mach-O 와 ELF 에서 디버그 정보는 어디에 있나요?</summary>

Mach-O: 컴파일러가 DWARF 를 `.o` 에 넣고, ld64 는 그걸 복사하지 않고 실행 파일에 "디버그 맵"(stab: `SO` 소스, `OSO` .o 경로+mtime, `FUN` 함수 주소)만 남긴다. `dsymutil` 이 맵을 따라 `.o` 들의 DWARF 를 최종 주소로 고쳐 dSYM 에 모은다. 그래서 `.o` 를 지우고 dSYM 도 없으면 줄 번호가 없다(`+ 12` 만). ELF: 링커가 `.debug_*` 를 실행 파일에 합친다 → `objcopy --only-keep-debug` 로 떼고 `--strip-debug --add-gnu-debuglink` 로 링크. 펌웨어 `fw.elf` 는 디버그 포함이 기본이고 Flash 에는 `.bin`/`.hex` 만 간다.

</details>

<details>
<summary>Q6. 인라인된 함수는 어떻게 백트레이스에 나오나요?</summary>

`.debug_info` 의 `DW_TAG_subprogram` 안에 `DW_TAG_inlined_subroutine` 이 주소 범위(`low_pc/high_pc` 또는 ranges)와 `DW_AT_call_file/call_line/call_column` 을 갖고 중첩된다. PC 가 그 범위 안이면: 가장 안쪽 프레임의 줄 = 라인 테이블, 바깥 프레임의 줄 = 안쪽 것의 `call_line`. 실측 `dwarfdump --lookup 0x100000fc4`: `rd_le16` [0xfc4,0xfc8) call_line 64 → "rd_le16:39 [inlined] / parse_record:64". 도구: `atos -i`, `addr2line -i`, `llvm-symbolizer`(기본 on). 꼬리: "-gline-tables-only 에도 있나?" → 있다(실측).

</details>

<details>
<summary>Q7. Cortex-M HardFault 덤프에 lr = 0x080000BB, 다른 프레임에 0xFFFFFFF9 가 있습니다. 어떻게 읽죠?</summary>

0x080000BB: bit0 = Thumb 상태 표시. `(lr & ~1) − 1 = 0x080000B9` 를 `arm-none-eabi-addr2line -f -i -e fw.elf` 에 → `sensor_task fw_m4.c:40` (그대로 넣으면 41행 — 실측). 0xFFFFFFF9 는 EXC_RETURN(스레드 모드·MSP·기본 프레임)이라 주소가 아니다: 그 지점에서 예외 진입, 실제 중단 위치는 스택에 쌓인 exception frame 의 pc/lr 이다 (ARM-M12 CFSR, ARM-M14 덤프 해석). MCU 는 보통 ASLR 이 없어 슬라이드 단계는 없지만, 부트로더 + 앱처럼 이미지가 둘이면 어느 ELF 인지(주소 범위·build-id)부터 정한다 (BM-B06).

</details>

<details>
<summary>Q8. 펌웨어 팀의 심볼화 파이프라인을 설계해 보세요.</summary>

빌드: `-g` + `--build-id`, CI 가 `fw.elf`(또는 `.debug`)를 build-id 키로 아티팩트 저장소에 보관, `.bin` 만 배포. 장치: build-id 노트가 Flash 에 있으니(실측 0x08000040) 크래시 덤프(레지스터·스택 일부·fault 레지스터)에 build-id 앞 8–20바이트를 같이 저장(.noinit RAM 또는 Flash 로그 — BM-A09). 호스트: 디코더가 build-id 로 ELF 를 찾고 불일치면 거부, pc/lr 을 Thumb 규칙으로 정규화해 addr2line/llvm-symbolizer, 스택 스캔 결과는 "추정 프레임" 으로 표시. 맥/리눅스 도구도 같은 원리(UUID/build-id, 슬라이드, −1, 인라인). 포맷 설계·디코더 구현은 TOOL-A05.

</details>

## 9. 추가 연습

practice.sh = solution.sh + 아래 함수들. check.sh practice 가 전부 검사한다.

### 연습 1 — `.ips` 의 imageOffset 으로 심볼화

macOS `.ips` 리포트는 프레임을 `imageOffset`(= pc − 로드 주소)으로 준다 (ARM-M14 에서 본 `"imageOffset": 0x538`). `offset_frame DWARF FRAME OFFSET` 을 `atos -offset` 으로 작성하고, crash_null 의 프레임 0–2 에서 `symbolicate` 와 같은 출력이 나오는지 확인하라.

<details>
<summary>답</summary>

`atos -i -o DWARF -arch arm64 -offset <off>` — `-offset` 은 `-l`/`-s` 와 동시에 못 쓴다(man). 프레임 ≥ 1 이면 오프셋에서도 1을 뺀다. check.sh 는 `pc − load_address` 로 오프셋을 만들어 비교: `PASS  offset_frame == symbolicate for frames 0..2 of crash_null`. 오프셋은 슬라이드와 무관하므로 로그에 로드 주소가 없어도 된다는 게 장점.

</details>

### 연습 2 — Cortex-M 덤프: Thumb 비트, EXC_RETURN

`m4_lookup FRAME ADDR` 과 `m4_frame ELF FRAME ADDR` 을 작성하라: 프레임 0 은 `addr & ~1`, 1 이상은 `(addr & ~1) − 1`, `0xFFFFFF00` 이상은 `#n <exception return 0x…>`, addr2line 출력(함수/파일:줄 두 줄씩, `-i` 면 인라인부터)을 `#n func (file:line)` 으로. 테스트 벡터는 check.sh 가 `objdump -d -l` 에서 직접 뽑는다(`bl <parse_frame>` 주소 + 4 | 1 = 0x80000bb 등).

<details>
<summary>답</summary>

practice.sh 의 `m4_lookup`/`m4_frame`. 주의점: addr2line 이 줄 뒤에 ` (discriminator 1)` 을 붙일 때가 있어(같은 줄의 다른 기본 블록 구분, objdump 에서 `fw_m4.c:33 (discriminator 1)` 실측) 지운다. 실측 결과:

```text
   vectors from objdump: pc 0x800009a  lr(parse) 0x80000bb  lr(task) 0x80000d5  lr(fault) 0x8000091
PASS  M4 NULL body: inline + Thumb lr - 1 + EXC_RETURN
PASS  M4 fw_fault: noreturn call is the last insn of check_crc
PASS  m4_lookup clears the Thumb bit (frame 0) and also subtracts 1 (frame >= 1)
```

여기서 `check_crc` 의 `bl fw_fault` 다음 0x8000090 은 리터럴 풀(`.word`)이라 함수 범위 안이어서 −1 을 안 해도 함수는 맞았다 — 맥 쪽처럼 다음 함수로 넘어가는지는 레이아웃에 달렸다. 규칙은 항상 적용한다.

</details>

### 연습 3 — build-id 로 디버그 파일 짝 맞추기

`elf_build_id FILE` 과 `elf_debug_ok STRIPPED DEBUG` 를 작성하라. `fw.stripped`(=`--strip-debug --add-gnu-debuglink=fw.debug`)와 `fw.debug` 는 맞고, `-O2` 로 다시 빌드한 `fw.elf` 는 거부해야 한다.

<details>
<summary>답</summary>

`arm-none-eabi-readelf -n` 의 `Build ID:` 줄을 비교. `.gnu_debuglink` 의 CRC32 는 "그 파일이 맞나" 를 파일 내용으로 확인하지만 파일 이름 기반이라 보관소 검색 키로는 build-id 가 낫다 (`.build-id/xx/rest.debug` 규칙, VM 실측). 결과: `PASS  build-id: fw.stripped matches fw.debug, not the -O2 rebuild`, `PASS  fw.stripped: no .debug_info, has build-id + .gnu_debuglink -> fw.debug`.

</details>

### 한 번에 검증

<!-- code: practice.sh -->

```bash
bash check.sh practice
```

```text
…(solution 과 같은 27줄: build · helpers · symbolicate)
== practice 1: atos -offset (imageOffset form, as in .ips reports)
PASS  offset_frame == symbolicate for frames 0..2 of crash_null
== practice 2/3: Cortex-M4 ELF (arm-none-eabi 16.2.0), Thumb bit, build-id
…(연습 2 답의 4줄)
PASS  build-id: fw.stripped matches fw.debug, not the -O2 rebuild
PASS  fw.stripped: no .debug_info, has build-id + .gnu_debuglink -> fw.debug
PASS  lesson dir untouched (23 entries)
30/30 passed
```

## 10. 핵심 정리

- 디버그 정보는 코드에 0 바이트: `__text` 2720 B / Cortex-M `.bin` 236 B 가 -g 레벨과 무관. 릴리스도 `-g` 로 빌드하고 출하물만 strip.
- 맥: DWARF 는 `.o` 에, 실행 파일엔 디버그 맵(OSO), `dsymutil` → dSYM. ELF: 실행 파일 안 `.debug_*` → `objcopy --only-keep-debug` + `--strip-debug --add-gnu-debuglink`.
- 짝 맞추기는 UUID / build-id 로만. 맥은 같은 소스 재빌드도 UUID 가 바뀐다(OSO mtime) → 빌드할 때 dSYM 보관.
- 주소 정규화: 슬라이드 빼기(`atos -l load_address`, Linux `pc − base`), 프레임 ≥ 1 은 −1, Thumb 은 `(lr & ~1) − 1`, `0xFFFFFFxx` = EXC_RETURN.
- 줄 = 라인 테이블에서 "주소 ≤ PC 인 마지막 행", 함수/인라인 = `.debug_info` 트리, 인라인 바깥 줄 = `DW_AT_call_line`. `atos -i` / `addr2line -f -i` / `llvm-symbolizer`.
- `-g1` = `-gline-tables-only`: 줄 + 인라인 OK, 변수 없음. clang `-g3` 는 `-fdebug-macro` 없이는 `-g` 와 같다.

## 참고자료

- `man atos`, `man dsymutil`, `man strip`, `dwarfdump --help` (이 맥 CLT)
- DWARF Debugging Information Format Version 5 — 6.2 Line Number Information, 3.3.8 Inlined Subroutines: https://dwarfstd.org/dwarf5std.html
- Apple, "Adding identifiable symbol names to a crash report": https://developer.apple.com/documentation/xcode/adding-identifiable-symbol-names-to-a-crash-report
- GNU binutils: `objcopy` (`--only-keep-debug`, `--add-gnu-debuglink`), `addr2line`: https://sourceware.org/binutils/docs/binutils/
- GDB manual, "Debugging Information in Separate Files" (debuglink, build-id 디렉터리): https://sourceware.org/gdb/current/onlinedocs/gdb.html/Separate-Debug-Files.html
- LLVM `llvm-symbolizer` 문서: https://llvm.org/docs/CommandGuide/llvm-symbolizer.html
- ARMv7-M Architecture Reference Manual — Exception return behavior (EXC_RETURN 값의 의미)
- 관련 레슨: TOOL-B04 (오브젝트 파일·심볼), TOOL-B10 (lldb 기초), ARM-M12 (HardFault CFSR), ARM-M14 (크래시 레지스터 덤프), BM-A09 (.noinit 크래시 덤프), TOOL-A05 (덤프 포맷 + 디코더), TOOL-A06 (재현 가능 빌드·build-id)
