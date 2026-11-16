---
id: TOOL-M03
time: 80분
updated: 2026-11-15
---
# TOOL-M03 커스텀 섹션으로 자동 등록 테이블 만들기

## 학습 목표

- `SHELL_CMD(name, fn, help)` 매크로 한 줄이 `const struct` 하나를 **커스텀 섹션(custom section)** 에 넣고, 링커가 여러 `.o` 의 같은 섹션을 이어 붙여 만든 "배열" 을 **경계 심볼(boundary symbol)** 로 순회하는 원리를 설명하고 직접 짠다 — Mach-O `section$start$__DATA$__shellcmds` / ELF `__start_shellcmds` / 링커 스크립트 `KEEP` + 직접 정의한 심볼, 세 방식 모두.
- 항목이 **어느 단계에서 사라지는지** 구분한다: 컴파일러(`used` 없음 → static 객체 삭제), 링커 GC(`-dead_strip`, `--gc-sections`, lld 의 `-z start-stop-gc` 기본값 → `retain`/`KEEP` 필요), 정적 라이브러리 멤버 미포함(B09).
- 간격(stride)이 `sizeof` 라는 가정이 깨지는 경우 — `char *` 산술, Mac ASan 의 전역 레드존(실측 24 → 64 B), 정렬 — 를 측정하고 막는다 (`no_sanitize("address")`, `aligned`, 링커 `ASSERT`).
- 순서는 보장되지 않는다는 것(clang 은 정의 순서, gcc 는 역순, 파일은 링크 순서 — 실측)을 알고, 정렬 인덱스 · 중복 검출 · 이진 검색으로 셸 디스패처를 만든다. initcall 레벨은 `SORT()` 또는 레벨별 섹션으로 만든다.
- 같은 테이블을 Cortex-M3 이미지의 Flash 에 두고 (`fw/fw.ld`), QEMU 에서 실제로 부팅해 명령이 돌아가는 것을 확인한다.

## 1. 문제

펌웨어에서 "기능 모듈을 추가하면 **중앙 목록을 고치지 않아도** 자동으로 등록" 되게 하는 표준 기법이 커스텀 섹션 테이블이다. Linux `initcall` · `module_param`, Zephyr `SHELL_CMD_REGISTER` · `SYS_INIT`, U-Boot `U_BOOT_CMD` 가 모두 이것이고, 면접에서는 "등록 매크로를 짜 보세요", "`--gc-sections` 켜니까 명령이 사라졌어요 — 왜?", "`__start_` 심볼은 누가 만들죠?" 로 나온다. 링커가 무엇을 하는지 알아야 버그를 잡을 수 있는 주제다.

아이디어는 단순하다. 각 `.c` 파일이 같은 이름의 섹션에 같은 타입의 객체를 하나씩 넣으면, 링커는 입력 섹션을 **같은 이름끼리 이어 붙여** 출력 섹션 하나로 만든다. 그 결과는 사실상 `struct shell_cmd table[N]` 이다. 시작과 끝 주소만 알면 `for (p = begin; p < end; p++)` 로 돈다. 아래 그림은 `multi/` 데모의 실제 결과다 (Mac, `demo_macho.txt` 5번).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">링커가 같은 이름의 섹션을 이어 붙인다 (Mac 실측: 96 bytes, 4 entries)</text>
  <rect x="10" y="40" width="200" height="70" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="58" fill="currentColor" font-family="monospace" font-size="12">cmd_sys.o</text>
  <rect x="20" y="66" width="180" height="36" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="28" y="82" fill="currentColor" font-family="monospace" font-size="12">__shellcmds: uptime</text>
  <text x="28" y="97" fill="currentColor" font-family="monospace" font-size="12">             reboot(sys)</text>
  <rect x="10" y="125" width="200" height="56" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="143" fill="currentColor" font-family="monospace" font-size="12">cmd_pwr.o</text>
  <rect x="20" y="150" width="180" height="24" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="28" y="167" fill="currentColor" font-family="monospace" font-size="12">__shellcmds: reboot(pwr)</text>
  <rect x="10" y="196" width="200" height="56" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="214" fill="currentColor" font-family="monospace" font-size="12">cmd_led.o</text>
  <rect x="20" y="221" width="180" height="24" rx="3" class="f-acc" stroke="currentColor"/>
  <text x="28" y="238" fill="currentColor" font-family="monospace" font-size="12">__shellcmds: led</text>
  <path d="M210 84 L330 120" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <path d="M210 162 L330 168" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <path d="M210 233 L330 216" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="236" y="70" fill="currentColor" font-size="12">링크 순서대로</text>
  <rect x="330" y="80" width="230" height="170" rx="5" stroke="currentColor" fill="none"/>
  <text x="340" y="98" fill="currentColor" font-family="monospace" font-size="12">a.out  __DATA,__shellcmds</text>
  <rect x="340" y="106" width="210" height="32" class="f-ok" stroke="currentColor"/>
  <text x="348" y="127" fill="currentColor" font-family="monospace" font-size="12">+0   uptime  (24 B)</text>
  <rect x="340" y="138" width="210" height="32" class="f-ok" stroke="currentColor"/>
  <text x="348" y="159" fill="currentColor" font-family="monospace" font-size="12">+24  reboot (sys)</text>
  <rect x="340" y="170" width="210" height="32" class="f-warn" stroke="currentColor"/>
  <text x="348" y="191" fill="currentColor" font-family="monospace" font-size="12">+48  reboot (pwr) 중복!</text>
  <rect x="340" y="202" width="210" height="32" class="f-ok" stroke="currentColor"/>
  <text x="348" y="223" fill="currentColor" font-family="monospace" font-size="12">+72  led</text>
  <path d="M575 106 L555 106" stroke="currentColor" stroke-width="2"/>
  <text x="580" y="104" fill="currentColor" font-size="12">begin (+0)</text>
  <text x="580" y="120" fill="currentColor" font-family="monospace" font-size="12">section$start$…</text>
  <text x="580" y="136" fill="currentColor" font-family="monospace" font-size="12">__start_shellcmds</text>
  <path d="M575 234 L555 234" stroke="currentColor" stroke-width="2"/>
  <text x="580" y="232" fill="currentColor" font-size="12">end (+96, 하나 지난 곳)</text>
  <text x="580" y="248" fill="currentColor" font-family="monospace" font-size="12">section$end$…</text>
  <text x="580" y="264" fill="currentColor" font-family="monospace" font-size="12">__stop_shellcmds</text>
  <text x="10" y="290" fill="currentColor" font-size="12">코드는 begin..end 만 안다 → 모듈을 더해도 중앙 목록 수정 없음. 대신 "누가 등록했나" 는 링크 결과가 정한다.</text>
  <text x="10" y="310" fill="currentColor" font-size="12">순서(링크 순서 · 컴파일러) · 존재(used · GC · .a) · 간격(sizeof · 레드존 · 정렬)은 코드가 직접 확인해야 한다.</text>
</svg>
```

### Part A — 등록 매크로와 경계 심볼 (이식성 헤더)

- `SHELL_CMD(name_, fn_, help_)` → `static const struct shell_cmd shell_cmd_<name>` 하나를 섹션에 넣는다. 속성 `SHELL_ENTRY_ATTR` 를 채운다 (`used`, 정렬, `section(...)`, Mac ASan 대응).
- 이식성 블록: Mach-O 는 `"__DATA,__shellcmds"` + `extern ... __asm("section$start$__DATA$__shellcmds")`, ELF 는 C 식별자 이름 `"shellcmds"` + `__start_shellcmds` / `__stop_shellcmds`, 링커 스크립트판(`-DSHELL_LDSCRIPT`)은 `".shellcmds"` + 스크립트가 정의하는 `__shellcmds_start/_end`. 섹션 이름 문자열은 **한 곳에서만** 정의한다.
- `shell_count(b, e)` = 항목 수, `shell_at(b, i)` = i 번째 항목 주소.
- 체크: 평범한 배열 3개로 count/at, 실제 섹션에서 "이름으로 참조한 `shell_cmd_help` 가 [begin, end) 안에 있다", 섹션 바이트 수가 `sizeof` 의 배수, 항목 수 = 7, 모든 항목의 name/fn/help 가 NULL 아님.

### Part B — 함정: 사라지는 항목, 틀린 간격, 오타, 중복

시작 코드에는 **실제로 자주 무는 함정 4개**가 들어 있다 (각각 자기 FAIL 줄이 있다 — 5. 풀이 B-1 의 단독 주입 실험).

| # | 시작 코드 | 증상 |
|---|---|---|
| 1 | `__attribute__((unused, section(...)))` — 경고만 끄고 `used` 없음 | 이름으로 안 부르는 6개가 **컴파일 단계에서** 사라짐 (-O0 에서도) |
| 2 | `shell_count` 가 `(char *)e - (char *)b` | 항목 수 대신 바이트 수(72) |
| 3 | `shell_at` 이 `(char *)b + i` | 1 바이트씩 이동 → 쓰레기 포인터 (단독으로 넣으면 SIGSEGV, rc 139) |
| 4 | 경계 심볼 이름 `__shellcmd` (s 빠짐) | Mac ld64 는 **크기 0 섹션을 새로 만들어** 링크 성공 → 조용히 빈 표 |

그리고 TODO 로 `shell_build_index` (포인터 인덱스 + 이름순 `qsort`, 동률은 주소순), `shell_find_dups` (정렬 후 인접 비교, 세 번 나와도 한 번 보고), `shell_lookup` (이진 검색, 중복이면 `SHELL_EDUP`) 을 채운다. 섹션은 Flash/const 라서 **제자리 정렬이 불가능** 하다는 점이 설계의 출발점이다.

### Part C — 펌웨어 셸 디스패처

- `shell_init` (부팅 때 한 번: 인덱스 + 중복 검사), `shell_exec(line)` (제자리 토큰화, 공백·탭, argv 최대 8, 빈 줄 0, 모름 `SHELL_ENOENT`, 9개 이상 `SHELL_E2BIG`), `shell_help` (이름순 `"%-8s %s\n"`, 버퍼 넘치면 -1).
- 체크: `led on`, `led blink`(usage 2), `"  sum 1\t2   0x27 "` → `42`, `echo  hello   shell` → `hello shell`, 정확한 help 텍스트.
- 데모(`demo_fw.sh`): 같은 테이블을 Cortex-M3 이미지의 Flash 에 `KEEP` 으로 두고, initcall 4개를 `SORT` 로 레벨순 배치 → VM QEMU 에서 실제 부팅. `KEEP` 을 빼면 무엇이 사라지는지 `--print-gc-sections` 로 본다.

## 2. Problem (English)

> **Self-registering command table.**
>
> Firmware modules register debug-shell commands with one macro, `SHELL_CMD(name, fn, help)`, placed anywhere in any `.c` file. No central list may be edited when a module is added.
>
> 1. Make the macro emit a `static const struct shell_cmd { const char *name; int (*fn)(int, char **); const char *help; }` into a dedicated section and iterate the section with linker-provided bounds. It must build on macOS (Mach-O: `section$start$__DATA$__shellcmds`) and on ELF (`__start_shellcmds`/`__stop_shellcmds`, or a linker script with `KEEP` and your own symbols).
> 2. Entries that nobody references by name must survive `-O2` and dead-code elimination; the iteration must not assume anything the toolchain does not guarantee (order, padding).
> 3. At boot, build a sorted index (the table itself lives in flash and cannot be sorted in place), report duplicate names, and dispatch a command line: tokenize in place on spaces/tabs, at most 8 arguments, return `-1` for unknown, `-2` for too many arguments, `-3` for an ambiguous (duplicate) name.
> 4. `help` prints `"%-8s %s\n"` per command in name order.
>
> Example: `"  sum 1\t2   0x27 "` → `fn` gets `argc = 4`, prints `42`. `help` → `"echo     echo args\nhelp     list commands\n..."`.
>
> Follow-ups: what happens with `--gc-sections` on GNU ld vs lld; why do entries disappear when built into a static library; what does AddressSanitizer do to this table; how would you order init levels (Linux initcalls)?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o sol_asan && ./sol_asan
```

처음 실행 결과 (실제 출력, Mac):

```text
FAIL starter.c:195  shell_count(arr, arr + 3) == 3
FAIL starter.c:196  shell_at(arr, 2) == &arr[2] && shell_at(arr, 2)->name[0] == 'c'
FAIL starter.c:201  mine >= b && mine < e
FAIL starter.c:203  shell_count(b, e) == N_REGISTERED
FAIL starter.c:209  ok
section: 0 bytes, sizeof(entry) = 24, 0 entries
FAIL starter.c:222  n == 6
…  (Part B 8줄, Part C 8줄 더)
FAIL starter.c:276  shell_exec(&g_sh, l9) == 0 && strcmp(g_out, want) == 0
3/26 passed
```

`section: 0 bytes` — 오타 난 경계 심볼이 가리키는 빈 섹션을 보고 있다. 경고 0개, 링크 성공, 표만 비었다. 이것이 이 레슨의 첫 번째 교훈이다: **등록 테이블 버그는 빌드 에러가 아니라 "조용히 빈 표" 로 나타난다.**

관찰용 데모 (체크와 별개, 실제 출력은 `*.txt`):

| 파일 | 하는 일 | 실행 |
|---|---|---|
| `demo_bugs.sh` → `demo_bugs.txt` | 시작 코드의 함정을 **하나씩만** solution.c 에 다시 넣어 어떤 CHECK 가 잡는지 | `bash demo_bugs.sh` (Mac) |
| `demo_macho.sh` → `demo_macho.txt` | `demo_survive.c` 생존 행렬, 오타 섹션, `nm -m`, `demo_asan.c` 레드존, `__TEXT`/`__DATA_CONST`, `multi/` 링크 순서 · `.a` · dead_strip · 중복 링크 에러 | `bash demo_macho.sh` (Mac) |
| `demo_elf.sh` → `demo_elf.txt` | 같은 코드를 ELF 로 (gcc 15.2 / clang 21 / ld.bfd 2.46 / ld.lld 21), `--gc-sections` × `-z start-stop-gc` × `retain` 행렬, 오타 · weak, ASan, `initcall_host.ld` + `demo_initcall.c` 의 `SORT` | `bash demo_elf.sh` (Mac 에서 → VM) |
| `demo_fw.sh` → `demo_fw.txt` | `fw/fw.c` + `fw/fw.ld`: Cortex-M3 이미지, KEEP 유무, used 유무, 간격 ASSERT, QEMU 실행 | `bash demo_fw.sh` (Mac 빌드 → VM QEMU) |

## 4. 힌트

<details><summary>힌트 1 — Mach-O 경계 심볼은 C 이름이 아니다</summary>

`section$start$__DATA$__shellcmds` 에는 `$` 가 들어 있어 C 식별자로 쓸 수 없다. clang 의 asm label 로 C 이름과 링커 심볼 이름을 분리한다: `extern const struct shell_cmd shell_cmds_begin[] __asm("section$start$__DATA$__shellcmds");`. `__asm` 안은 **문자열 리터럴 연결**이 되니, 섹션 이름 매크로 하나에서 세 문자열(섹션 지정, start, end)을 모두 만들 수 있다.
</details>

<details><summary>힌트 2 — "unused" 와 "used" 는 정반대</summary>

`unused` 는 "안 쓰여도 경고하지 마" 일 뿐이고, 컴파일러는 아무도 안 부르는 static 객체를 그대로 버린다. `used` 는 "아무도 안 불러도 **내보내라**" 다. Mach-O 에서는 `used` 가 `[no dead strip]` 플래그까지 붙인다 (`nm -m` 으로 확인).
</details>

<details><summary>힌트 3 — 포인터 산술의 단위</summary>

`const struct shell_cmd *` 에 `+ i` 하면 이미 `i * sizeof(struct shell_cmd)` 바이트 이동이다. 반대로 `char *` 로 바꾸는 순간 단위가 1 바이트가 된다. `e - b` (같은 타입 포인터끼리) 는 항목 수다.
</details>

<details><summary>힌트 4 — 정렬은 포인터 배열로</summary>

표는 Flash(또는 const)에 있으므로 옮길 수 없다. `const struct shell_cmd *idx[SHELL_MAX]` 에 주소를 담고 `qsort` 의 비교 함수에서 `*(const struct shell_cmd *const *)a` 로 꺼낸다. 동률일 때 주소로 비교하면 결과가 매번 같다 (qsort 는 안정 정렬이 아니다).
</details>

<details><summary>힌트 5 — 중복과 이진 검색</summary>

정렬 후에는 같은 이름이 붙어 있다. "첫 번째로 `>= name` 인 위치(lower bound)" 를 찾고, 그 위치와 다음 위치가 둘 다 `name` 이면 중복이다. 접두사(`"ec"`)는 일치가 아니다 — `strcmp` 결과가 0 일 때만.
</details>

## 5. 풀이

### Part A — 링커는 무엇을 해 주나

**A-1. 섹션 = 같은 이름의 입력 섹션 묶음.** 컴파일러는 `section("__DATA,__shellcmds")` 붙은 객체를 그 `.o` 의 해당 섹션에 차례로 놓는다. 링커는 모든 입력 파일의 같은 이름 섹션을 출력 섹션 하나로 **이어 붙이며** (각 입력 섹션의 정렬만 맞춘다) 그 사이에 아무것도 넣지 않는다. 그래서 정렬 요구가 `sizeof` 의 약수인 한, 결과는 빈틈 없는 배열이다. 실측: solution 의 섹션은 Mac · ELF(gcc · clang) 모두 `168 bytes, sizeof(entry) = 24, 7 entries`.

**A-2. 경계 심볼은 누가 만드나.**

| 형식 | 섹션 이름 | 시작 / 끝 | 누가 정의 |
|---|---|---|---|
| Mach-O (ld64) | `__DATA,__shellcmds` (세그먼트,섹션; 각 16자 이내) | `section$start$__DATA$__shellcmds` / `section$end$…` | ld64 가 이 형식의 미정의 심볼을 보면 그 섹션의 시작/끝 주소로 풀어 준다. **섹션이 없으면 크기 0 섹션을 만든다** (실측, 아래) |
| ELF (bfd · lld) | `shellcmds` — **C 식별자여야 함** | `__start_shellcmds` / `__stop_shellcmds` | 참조가 있고 그 이름의 출력 섹션이 있을 때 링커가 정의 (`nm`: `D __start_shellcmds`). 섹션이 없으면 **undefined reference** |
| ELF + 링커 스크립트 | 아무 이름 (`.shellcmds`) | 스크립트에서 `__shellcmds_start = .;` | 내가 정의. `KEEP` 도 내가 책임 |

ELF 에서 점(`.`)으로 시작하는 이름에는 `__start_` 가 생기지 않는다. 실측 (`demo_dotname.c`, `demo_elf.txt` 1번, gcc 15.2 + bfd): `section(".shellcmds")` + `extern __start_shellcmds[]` → `undefined reference to '__start_shellcmds'`. `__start_` + 이름이 C 에서 쓸 수 있는 심볼이어야 하기 때문에 이 규칙이 있다.

VM 의 ELF 결과 (`demo_elf.txt` 1번):

```text
0000000000020078 D __start_shellcmds
0000000000020120 D __stop_shellcmds
  [25] shellcmds         PROGBITS        0000000000020078 010078 0000a8 00  WA  0   0  8
```

0x120 − 0x78 = 0xa8 = 168 = 7 × 24. Flg `WA` = 쓰기 가능 + 메모리 할당: const 인데 W 인 이유는 포인터를 담은 const 객체가 PIE(Ubuntu gcc 기본)에서 로더의 재배치 대상이라 `.data.rel.ro` 성격이기 때문으로 본다 (MCU 의 정적 링크에서는 `fw.ld` 의 `.shellcmds` 처럼 `A` 만 — 아래 Part C).

**A-3. 정답 코드의 이식성 블록 요점.**

```c
#  define SHELL_SECT_NAME "__shellcmds"
#  define SHELL_SECTION   "__DATA," SHELL_SECT_NAME
extern const struct shell_cmd shell_cmds_begin[] __asm("section$start$__DATA$" SHELL_SECT_NAME);
extern const struct shell_cmd shell_cmds_end[]   __asm("section$end$__DATA$" SHELL_SECT_NAME);
...
#define SHELL_ENTRY_ATTR __attribute__((used, aligned(_Alignof(struct shell_cmd)), section(SHELL_SECTION))) SHELL_NO_ASAN
```

- 이름 문자열을 한 곳에서 만들면 함정 4(오타)가 원천 차단된다. ELF 쪽은 `SHELL_CAT(__start_, SHELL_SECT_ID)` 토큰 붙이기로 같은 효과.
- `extern ... []` (불완전 배열) 로 선언하면 `shell_cmds_end - shell_cmds_begin` 이 자연스럽고, 컴파일러가 "단일 객체 하나" 라고 가정하지 않는다.
- `aligned(_Alignof(...))` 는 정렬을 **명시**해 두는 것이다. GCC 는 일부 타깃에서 큰 전역 객체의 정렬을 성능 때문에 올리는데(예: x86-64 에서 32 B 이상 객체 — 이 맥/VM 에서는 재현할 x86-64 GCC 가 없어 미검증, 확인 권장), 그러면 항목 사이에 패딩이 생겨 배열 가정이 깨진다. Linux 커널의 `struct kernel_param` 등록이 `__aligned(sizeof(void *))` 를 붙이는 이유로 알려져 있다 (확인 권장). 사용자가 `aligned` 를 지정하면 GCC 는 그 값을 쓰고 임의로 올리지 않는다.

### Part B — 함정은 어디서 무나

**B-1. 함정을 하나씩만 넣으면 (`demo_bugs.txt`, 실측).** solution.c 에 버그 하나만 되돌려 넣고 돌렸다:

| 주입 | 처음 걸리는 CHECK (solution 줄) | 결과 |
|---|---|---|
| `unused` (used 없음) | 239 `shell_count(b, e) == N_REGISTERED` + Part C 7개 | `section: 24 bytes … 1 entries` (이름으로 참조한 help 하나만 생존), 18/26 |
| count = 바이트 | 231 `shell_count(arr, arr + 3) == 3` | 7/26 (인덱스 크기가 168 > 32 → `SHELL_EFULL`) |
| at = `char *` + i | 232 `shell_at(arr, 2) == &arr[2]…` | 그 줄 FAIL 출력 후 **rc=139** (SIGSEGV: 쓰레기 포인터로 `strcmp`) |
| 경계 이름 오타 (`__shellcmd`) | 239 `mine >= b && mine < e` | `section: 0 bytes`, 15/26 |
| `qsort` 생략 | 261 정렬 순서 | 이진 검색이 엉뚱한 답 → 17/26 |

실제 출력 일부:

```text
##### bug1_unused
FAIL line 239  shell_count(b, e) == N_REGISTERED
section: 24 bytes, sizeof(entry) = 24, 1 entries
##### bug3_stride
FAIL line 232  shell_at(arr, 2) == &arr[2] && shell_at(arr, 2)->name[0] == 'c'
section: 168 bytes, sizeof(entry) = 24, 7 entries
rc=139
```

주의: `main` 첫 줄의 `setvbuf(stdout, NULL, _IOLBF, 0)` 이 없으면 파이프로 볼 때 크래시 전에 찍은 FAIL 줄이 버퍼째 사라진다 (처음 실행에서 실제로 아무것도 안 보였다). 필드 펌웨어 로그도 같다 — 크래시 직전 로그는 플러시돼 있어야 의미가 있다.

**B-2. 누가 살아남나 — 컴파일러 단계 (`demo_macho.txt` 1번).**

```text
cc -O0:                entries: used global  (n=2)   typo-section n=0
cc -O2:                entries: used global  (n=2)   typo-section n=0
cc -O2 -Wl,-dead_strip: entries: used  (n=1)   typo-section n=0
== -DPLAIN (static, 속성 없음) + -Werror
demo_survive.c:13:25: error: unused variable 'c_plain' [-Werror,-Wunused-const-variable]
```

- `static` + `unused` → **-O0 에서도** 없음. clang 은 아무도 참조하지 않는 내부 링키지 전역을 애초에 내보내지 않는다. 경고도 없다 — 가장 나쁜 조합.
- `static`, 속성 없음 → `-Wunused-const-variable` 경고(= `-Werror` 빌드 실패). 여기서 "경고 끄려고 `unused` 를 붙이는" 것이 함정 1의 실제 발생 경로다.
- 외부 링키지(`global`), 속성 없음 → 컴파일은 통과, 그러나 `-dead_strip` 이 지운다.
- `used` → 모든 경우 생존. `nm -m` 에 `[no dead strip]` (`demo_macho.txt` 2번):

```text
00000000000020c0 (__DATA,__shellcmds) non-external [no dead strip] _shell_cmd_led
                 (undefined) external section$end$__DATA$__shellcmds
```

`section$end$…` 가 `.o` 안에서는 **미정의 외부 심볼**이다 — 링크 때 ld64 가 채운다.

**B-3. 누가 살아남나 — 링커 GC 단계 (ELF, `demo_elf.txt` 3번).** `multi/` 4개 파일(gcc -O2), 항목은 모두 `static` + `used`:

| 링커 / 옵션 | `used` 만 | `used, retain` |
|---|---|---|
| bfd 2.46 `--gc-sections` (기본) | 4 entries | 4 |
| bfd `--gc-sections -z start-stop-gc` | **2 entries** (첫 입력 파일 것만) | 4 |
| bfd `-z nostart-stop-gc` | 4 | 4 |
| lld 21 `--gc-sections` (기본) | **`ld.lld: error: undefined symbol: __stop_shellcmds`** | 4 |
| lld `-z start-stop-gc` | 같은 에러 | 4 |
| lld `-z nostart-stop-gc` | 4 | 4 |

해석:
- GC 는 "루트(엔트리 · KEEP · export 등)에서 재배치(relocation)로 닿는 섹션" 만 남긴다. 등록 항목은 **아무도 재배치로 가리키지 않는다**. 옛 규칙(`nostart-stop-gc`)은 "`__start_X` 를 참조하면 X 라는 이름의 입력 섹션 전부 살림" 이었고 bfd 는 지금도 이것이 기본이다.
- lld 는 `start-stop-gc` 가 기본이라(lld 13 에서 바뀐 것으로 알려짐, 확인 권장) 아무것도 남지 않고, 그러면 출력 섹션이 없으니 `__stop_` 를 정의할 수도 없어 에러가 난다. **조용히 빈 표보다 낫다.**
- `used` 는 **컴파일러**에게만 하는 말이다. ELF 섹션 플래그를 보면 차이가 보인다 (`demo_elf.txt` 2번):

```text
  [ 5] shellcmds         PROGBITS        0000000000000000 000050 000018 00  WA  0   0  8
  [ 5] shellcmds         PROGBITS        0000000000000000 000050 000018 00 WAR  0   0  8
```

`retain` 을 붙이면 `R` = `SHF_GNU_RETAIN` 이 생기고, 이것은 링커 GC 의 루트다. (GCC 11 초기에는 `used` 가 `R` 을 같이 냈다가 분리됐다는 이력이 있다 — 확인 권장. 실측: gcc 15.2 · clang 21 모두 `used` 만으로는 `R` 없음.)
- bfd `start-stop-gc` 에서 첫 파일만 남는 현상: 순서를 `led pwr sys` 로 바꾸니 `removing unused section 'shellcmds' in file 'cmd_pwr.o'`, `… 'cmd_sys.o'` 가 찍히고 `led` 만 남았다. `__start_` 를 정의하는 기준이 되는 첫 입력 섹션이 참조된 것으로 취급되는 것으로 보인다 (관찰 기반 추정, 확인 권장). 어느 쪽이든 "일부만 남는" 최악의 결과다.

**B-4. 오타 — 형식마다 증상이 다르다.**

| 형식 | 경계 심볼 오타 | 실측 |
|---|---|---|
| Mach-O | 링크 성공, 빈 섹션 | `otool -l`: `sectname __shelcmds … size 0x0000000000000000` (`demo_macho.txt` 1번) |
| ELF strong extern | 링크 실패 | `undefined reference to '__stop_shellcmd'` (bfd), `ld.lld: error: undefined symbol: __stop_shellcmd` |
| ELF weak extern | 링크 성공, NULL..NULL | `table (nil)..(nil) = 0 bytes, 0 entries:` |

"빈 레벨도 허용하려고" 경계를 `weak` 로 선언하는 코드가 많은데 (추가 연습 1 이 실제로 그렇게 한다), 그 대가는 오타를 링커가 못 잡는 것이다. 그래서 **부팅 셀프 테스트로 "표가 비어 있지 않다" 를 확인**하는 것이 실무 습관이다 (Part A 의 `mine >= b && mine < e` 체크가 그 역할). 덧붙여, start 만 오타·end 는 정상인 "반쪽 오타" 는 더 나쁘다: 실측에서 begin 이 end 보다 뒤에 놓여 `e - b` 가 음수 → `size_t` 로 거대한 수가 되어 루프가 끝나지 않았다 (1분 넘게 돌아 강제 종료). 추가 연습 3 의 `table_ok` 가 이것을 막는다.

**B-5. ASan 레드존 — 간격이 sizeof 가 아니게 된다.** ASan 은 전역 변수 뒤에 레드존(redzone)을 붙여 넘침을 잡는다 (TOOL-B11). 커스텀 섹션 표에서는 그 레드존이 배열 사이에 끼어든다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 3개 항목, 섹션 크기 72 B vs 192 B (Mac clang 21, demo_asan.c 실측)</text>
  <text x="10" y="50" fill="currentColor" font-size="12">보통 빌드</text>
  <rect x="100" y="36" width="72" height="26" class="f-ok" stroke="currentColor"/>
  <text x="108" y="54" fill="currentColor" font-family="monospace" font-size="12">x1 24B</text>
  <rect x="172" y="36" width="72" height="26" class="f-ok" stroke="currentColor"/>
  <text x="180" y="54" fill="currentColor" font-family="monospace" font-size="12">x2 24B</text>
  <rect x="244" y="36" width="72" height="26" class="f-ok" stroke="currentColor"/>
  <text x="252" y="54" fill="currentColor" font-family="monospace" font-size="12">x3 24B</text>
  <text x="330" y="54" fill="currentColor" font-size="12">72 bytes, p++ = +24 → 정확</text>
  <text x="10" y="104" fill="currentColor" font-size="12">-fsanitize=</text>
  <text x="10" y="120" fill="currentColor" font-size="12">address</text>
  <rect x="100" y="90" width="72" height="26" class="f-ok" stroke="currentColor"/>
  <text x="108" y="108" fill="currentColor" font-family="monospace" font-size="12">x1 24B</text>
  <rect x="172" y="90" width="120" height="26" class="f-warn" stroke="currentColor"/>
  <text x="180" y="108" fill="currentColor" font-family="monospace" font-size="12">f9 f9… 40B</text>
  <rect x="292" y="90" width="72" height="26" class="f-ok" stroke="currentColor"/>
  <text x="300" y="108" fill="currentColor" font-family="monospace" font-size="12">x2 24B</text>
  <rect x="364" y="90" width="120" height="26" class="f-warn" stroke="currentColor"/>
  <text x="372" y="108" fill="currentColor" font-family="monospace" font-size="12">f9 f9… 40B</text>
  <rect x="484" y="90" width="72" height="26" class="f-ok" stroke="currentColor"/>
  <text x="492" y="108" fill="currentColor" font-family="monospace" font-size="12">x3 24B</text>
  <rect x="556" y="90" width="120" height="26" class="f-warn" stroke="currentColor"/>
  <text x="564" y="108" fill="currentColor" font-family="monospace" font-size="12">f9 f9… 40B</text>
  <path d="M136 130 C 150 160, 190 160, 196 122" stroke="currentColor" stroke-width="1.5" fill="none"/>
  <text x="120" y="176" fill="currentColor" font-size="12">p++ (+24) → 레드존 한가운데: "READ of size 8 … 0 bytes after global variable 'shell_cmd_led'"</text>
  <text x="10" y="206" fill="currentColor" font-size="12">고침: 항목에 __attribute__((no_sanitize("address"))) → 다시 72 bytes, gap 24 (Mac 실측)</text>
  <text x="10" y="228" fill="currentColor" font-size="12">ELF: gcc 는 사용자 섹션 전역을 아예 계측 안 함, clang 은 C 식별자 이름 섹션만 건너뜀 (VM 실측)</text>
</svg>
```

실측 (`demo_macho.txt` 3번, `demo_elf.txt` 5번):

| 컴파일러 · 형식 | 식별자 이름 섹션 | 점 들어간 이름 섹션 |
|---|---|---|
| Mac clang 21 (Mach-O) `-fsanitize=address` | `192 bytes for 3 entries, gap 64` | gap 64 |
| Mac clang + `no_sanitize("address")` | `72 bytes`, gap 24 | gap 24 |
| VM clang 21 (ELF) | `72 bytes`, gap 24 | **gap 64** |
| VM gcc 15.2 (ELF) | `72 bytes`, gap −24 (역순 배치) | gap −24 |

solution.c 에서 `SHELL_NO_ASAN` 을 비우고 ASan 으로 돌리면 (`demo_macho.txt` 3번):

```text
==PID==ERROR: AddressSanitizer: global-buffer-overflow on address 0x… at pc 0x… bp 0x… sp 0x…
READ of size 8 at 0x… thread T0
0x… is located 0 bytes after global variable 'shell_cmd_led' defined in 'sol_noasanfix.c' (0x…) of size 24
SUMMARY: AddressSanitizer: global-buffer-overflow sol_noasanfix.c:319 in main
rc=134
```

LLVM 은 ELF 에서 "C 식별자처럼 생긴 사용자 섹션은 `__start_/__stop_` 순회에 쓰이니 계측하지 않는다" 는 예외를 두고, GCC 는 사용자 섹션 전역 전체를 보호 대상에서 뺀다 (각각 `AddressSanitizer.cpp`, `asan.cc` 의 주석으로 알려진 동작 — 실측과 일치, 소스 확인 권장). Mach-O 에는 그 예외가 없어서 우리가 직접 `no_sanitize("address")` 를 붙인다. 정답 코드는 `__has_feature(address_sanitizer) && __APPLE__` 일 때만 붙인다. 정답의 ASan+UBSan 결과: Mac `26/26 passed`, VM gcc · clang `26/26 passed`.

**B-6. 순서는 아무도 약속하지 않는다.**

| 상황 | 실측 순서 |
|---|---|
| 한 파일 안 (cmd_sys.c: `uptime` 정의 후 `reboot`) | Mac clang · VM clang: `uptime reboot` / VM gcc: `reboot uptime` (역순) |
| 파일 사이 | 링크 명령의 순서: `main sys pwr led` → `uptime reboot reboot(pwr) led`, 뒤집으면 `led reboot(pwr) uptime reboot(sys)` |
| 중복 이름 (두 모듈 모두 static) | 링크 성공, 표에 둘 다. 이름으로 찾으면 **링크 순서가 승자를 정한다** |

그래서 정답은 런타임에 인덱스를 정렬하고(동률은 주소순 → 결정적), 중복이면 `SHELL_EDUP` 으로 **거부**한다 — "어느 reboot 가 불릴지 빌드마다 다른" 상황보다 낫다. 링크 단계에서 중복을 잡고 싶으면 항목을 외부 링키지로 만들면 된다 (`multi/shell.h -DSHELL_GLOBAL`, 실측):

```text
duplicate symbol '_shell_cmd_reboot' in:
    g_cmd_pwr.o
    g_cmd_sys.o
ld: 1 duplicate symbols
```

GNU ld: `multiple definition of 'shell_cmd_reboot'; g_cmd_sys.o:(shellcmds+0x0): first defined here`. 대가: 심볼이 전역 네임스페이스를 더럽히고, 다른 모듈의 같은 C 이름 변수와 충돌할 수 있다.

### Part C — 셸과 펌웨어 이미지

**C-1. 디스패처.** 부팅 때 한 번 `shell_init` 이 O(n log n) 정렬, 그 뒤 명령마다 O(log n) 검색. 토큰화는 제자리(`'\0'` 을 써 넣음)라 malloc 이 없다 — 펌웨어 셸의 기본. 경계 조건: argv 가 정확히 8개면 성공(`sum 1 2 3 4 5 6 7` → `28`), 9개째를 만나는 순간 `SHELL_E2BIG`. help 는 `snprintf` 반환값으로 잘림을 감지한다 (`w >= cap - len` → -1).

**C-2. 생존 경로 한눈에.**

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">등록 항목이 최종 이미지까지 가는 길 — 세 관문 (모두 실측)</text>
  <rect x="10" y="40" width="150" height="60" rx="5" class="f-mut" stroke="currentColor"/>
  <text x="20" y="62" fill="currentColor" font-family="monospace" font-size="12">SHELL_CMD(led,…)</text>
  <text x="20" y="82" fill="currentColor" font-size="12">static const 객체</text>
  <path d="M160 70 L190 70" stroke="currentColor" stroke-width="2"/>
  <rect x="190" y="40" width="160" height="60" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="200" y="60" fill="currentColor" font-weight="bold">① 컴파일러</text>
  <text x="200" y="80" fill="currentColor" font-size="12">used 없으면 버림</text>
  <text x="200" y="95" fill="currentColor" font-size="12">(-O0 에서도, 경고 없이)</text>
  <path d="M350 70 L380 70" stroke="currentColor" stroke-width="2"/>
  <rect x="380" y="40" width="170" height="60" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="390" y="60" fill="currentColor" font-weight="bold">② 아카이브 추출</text>
  <text x="390" y="80" fill="currentColor" font-size="12">.a 멤버는 미정의 심볼이</text>
  <text x="390" y="95" fill="currentColor" font-size="12">있어야 들어옴 (B09)</text>
  <path d="M550 70 L580 70" stroke="currentColor" stroke-width="2"/>
  <rect x="580" y="40" width="150" height="60" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="590" y="60" fill="currentColor" font-weight="bold">③ 링커 GC</text>
  <text x="590" y="80" fill="currentColor" font-size="12">재배치로 안 닿으면</text>
  <text x="590" y="95" fill="currentColor" font-size="12">삭제</text>
  <rect x="190" y="120" width="160" height="120" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="200" y="140" fill="currentColor" font-size="12">통과시키는 것:</text>
  <text x="200" y="160" fill="currentColor" font-family="monospace" font-size="12">used</text>
  <text x="200" y="180" fill="currentColor" font-size="12">(Mach-O 는 여기에</text>
  <text x="200" y="196" fill="currentColor" font-size="12"> [no dead strip] 도 붙음)</text>
  <rect x="380" y="120" width="170" height="120" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="390" y="140" fill="currentColor" font-size="12">통과시키는 것:</text>
  <text x="390" y="160" fill="currentColor" font-family="monospace" font-size="12">-force_load x.a</text>
  <text x="390" y="178" fill="currentColor" font-family="monospace" font-size="12">--whole-archive</text>
  <text x="390" y="196" fill="currentColor" font-size="12">또는 .o 로 직접 링크</text>
  <text x="390" y="214" fill="currentColor" font-size="12">실측: 2 → 3 entries</text>
  <rect x="580" y="120" width="150" height="120" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="590" y="140" fill="currentColor" font-size="12">통과시키는 것:</text>
  <text x="590" y="160" fill="currentColor" font-family="monospace" font-size="12">KEEP(*(.x))</text>
  <text x="590" y="178" fill="currentColor" font-family="monospace" font-size="12">retain (R 플래그)</text>
  <text x="590" y="196" fill="currentColor" font-size="12">bfd 기본 규칙</text>
  <text x="590" y="214" fill="currentColor" font-size="12">Mac: used 로 충분</text>
  <text x="10" y="262" fill="currentColor" font-size="12">하나라도 놓치면 결과는 같다: 빌드 성공 + 표에서 항목이 빠짐. 그래서 "항목 수" 를 테스트/부팅 로그로 확인한다.</text>
</svg>
```

정적 라이브러리 함정은 B09 연습 3 의 constructor 등록과 똑같다. 실측 (`demo_macho.txt` 5번 / `demo_elf.txt` 4번):

```text
ranlib: warning: 'libcmds.a(cmd_led.o)' has no symbols
== cc main.o cmd_sys.o -L. -lcmds
48 bytes, 2 entries: uptime(sys) reboot(sys)
== cc main.o cmd_sys.o -Wl,-force_load,libcmds.a
72 bytes, 3 entries: uptime(sys) reboot(sys) led(led)
```

항목이 전부 static 이라 멤버에 전역 심볼이 하나도 없고(`has no symbols`), 아무도 그 멤버를 끌어오지 않는다. GNU ld 도 같다 (`--whole-archive … --no-whole-archive` 로 2 → 3).

**C-3. Cortex-M 이미지 (`fw/fw.ld`, `demo_fw.txt`).** 펌웨어에서는 링커 스크립트를 직접 쓰므로 세 번째 방식이 표준이다.

<!-- code: fw/fw.ld -->

Mac `arm-none-eabi-gcc 16.2.0` (`-mcpu=cortex-m3 -mthumb -O2 -nostdlib -ffunction-sections -fdata-sections -Wl,--gc-sections`) 결과:

```text
.isr_vector          64           0
.text              1232          64
.shellcmds           36        1296
.initcalls           16        1332
  [ 3] .shellcmds        PROGBITS        00000510 001510 000024 00   A  0   0  4
00000510 R __shellcmds_start
00000510 r shell_cmd_help
0000051c r shell_cmd_version
00000528 r shell_cmd_led
00000534 R __shellcmds_end
Contents of section .shellcmds:
 0510 c4040000 51000000 cc040000 b8040000  ....Q...........
```

- 항목은 12 B (32비트 포인터 3개), 3개 = 36 B, Flash 에만 있고 RAM 비용 0. 플래그 `A` (쓰기 불가) — 정적 링크라 로더 재배치가 없다.
- 두 번째 워드 `0x00000051` = `cmd_help` 의 주소 `0x50` | 1. **Thumb 비트**: Cortex-M 의 함수 포인터는 홀수다 (`nm` 의 `00000050 t cmd_help` 와 비교). 테이블을 손으로 만들 때 이 비트를 빼먹으면 `BX` 가 ARM 상태로 가려다 UsageFault(INVSTATE) — ARM-M12 참고.
- 정의 순서는 led, version, help 인데 섹션에는 help, version, led — GCC 의 역순 배치가 여기서도 보인다.
- `.initcalls` 는 `KEEP(*(SORT(.initcall.*)))` 덕분에 정의 순서(3, 0, 2, 1)와 무관하게 `clock(0) gpio(1) uart(2) app(3)` 순서.

VM `qemu-system-arm -M mps2-an385` 실행 (semihosting 출력, 실측):

```text
shellcmds @ 0x00000510, 3 entries
initcall 0 clock
initcall 1 gpio
initcall 2 uart
initcall 3 app
> help
  help - list commands
  version - firmware version
  led - led on|off
> led on
  led -> on
...
RESULT: OK
qemu exit=0
```

`KEEP` 을 뺀 스크립트(`*(.shellcmds)`)로 같은 소스를 링크하면:

```text
ld: removing unused section '.shellcmds' in file 'ccjCz1N6.o'
.shellcmds            0        1076
shellcmds @ 0x00000434, 0 entries
> help
  unknown command
RESULT: FAIL
qemu exit=1
```

링커 스크립트에서 정의한 `__shellcmds_start = .;` 같은 심볼은 입력 섹션을 "참조" 하지 않으므로 GC 를 막지 못한다 — `KEEP` 이 유일한 방법이다. 반대로 `KEEP` 은 있는데 `used` 가 없으면 GCC 가 `'shell_cmd_help' defined but not used [-Wunused-const-variable=]` 경고와 함께 객체를 먼저 버려 `.shellcmds 0` 이 된다 (`demo_fw.txt` 3번): **`used` 는 컴파일러용, `KEEP` 은 링커용 — 둘 다 필요.**

마지막으로 스크립트의 `ASSERT((__shellcmds_end - __shellcmds_start) % 12 == 0, …)` 는 간격 가정을 링크 시점에 지킨다. 섹션에 엉뚱한 2 바이트(`SHORT(0)`)를 끼우면 실측:

```text
ld: TOOL-M03: .shellcmds stride is not sizeof(struct shell_cmd)
collect2: error: ld returned 1 exit status
```

(ASSERT · 정렬 · RAM 함수 등 스크립트 심화는 TOOL-A07, 스크립트 기초는 TOOL-M01.)

**C-4. 여러 파일 데모 헤더.** `multi/shell.h` 는 정답과 같은 이식성 블록에 실험 스위치(`NO_USED`, `SHELL_RETAIN`, `SHELL_WEAK`, `SHELL_GLOBAL`, `SECT_ID`)를 붙인 것이다.

<!-- code: multi/shell.h -->

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o sol && ./sol
```

실제 출력 (Mac; VM gcc 15.2 · clang 21 도 같은 출력, ASan+UBSan 빌드 포함):

```text
section: 168 bytes, sizeof(entry) = 24, 7 entries
26/26 passed
```

`-O0` 빌드도 `26/26 passed` (실측). 바뀐 곳: 이름 단일 정의(45–47행), `SHELL_ENTRY_ATTR` (67행: `used` + `aligned` + `SHELL_NO_ASAN`), count/at 의 포인터 단위, 인덱스 · 중복 · 검색 · 토큰화 · help 본문.

## 7. 자주 하는 실수

**1. `unused` 로 경고를 끈다.** → static 객체가 컴파일 단계에서 사라진다 (실측: -O0/-O2 모두, 경고 0개). 고침: `__attribute__((used, section(...)))`.

**2. `-Werror` 를 피하려고 항목을 외부 링키지로 바꾸고 끝.** → 컴파일러는 남기지만 `-Wl,-dead_strip` (실측: `used global` → `used` 만) / GC 가 지운다. 외부 링키지는 중복 검출(B-6)이 목적일 때만, 그래도 `used` 는 붙인다.

**3. `char *` 로 순회.** → `(char *)b + i` 는 1 바이트 이동 (실측 SIGSEGV), `(char *)e - (char *)b` 는 바이트 수. 고침: 타입 있는 포인터로 `b + i`, `e - b`. 바이트가 필요하면 그때만 `char *`.

**4. 섹션 이름을 세 군데에 손으로 쓴다.** → Mac 에선 조용히 빈 표, ELF weak 면 NULL..NULL, start 만 틀리면 끝나지 않는 루프 (실측). 고침: 매크로 하나에서 문자열 연결/토큰 붙이기로 만든다 + 부팅 때 "표가 비지 않았다" 확인.

**5. ELF 섹션 이름에 점을 넣고 `__start_` 를 기대.** → `.shellcmds` 에는 `__start_` 가 없다 (`undefined reference to '__start_shellcmds'`, 실측). 고침: C 식별자 이름, 또는 링커 스크립트에서 직접 정의.

**6. lld 로 바꾸고 `--gc-sections` 를 켰더니 링크 에러/빈 표.** → lld 기본 `start-stop-gc` 에서 `used` 만으로는 부족 (실측 `undefined symbol: __stop_shellcmds`). 고침: `__attribute__((retain))` (R 플래그), 스크립트 `KEEP`, 또는 `-z nostart-stop-gc`.

**7. 섹션 안 순서에 의존 (첫 항목이 기본 명령, initcall 이 정의 순서대로 돈다고 가정).** → gcc 역순, clang 정의 순, 파일은 링크 순 (실측). 고침: 런타임 정렬, 레벨별 섹션, 또는 스크립트 `SORT`.

**8. Mac ASan 빌드에서 "테이블이 깨졌다" 며 코드를 고친다.** → 레드존 때문 (24 → 64 B 간격). 고침: 항목에 `no_sanitize("address")`. ELF 에서는 재현이 안 되니(gcc · clang 식별자 섹션) 크로스 플랫폼 CI 에서만 터지는 버그가 된다.

**9. 포인터가 든 const 표를 Mac `__TEXT` 에 넣는다.** → 링크 에러 (실측):

```text
Illegal text-relocations:
  text-relocation in '_c_used'+0x10 (seg-540fe8.o) to 'anon-3'
```

PIE 실행 파일에서 포인터는 로드 주소에 따라 고쳐 써야 하므로 쓰기 불가 세그먼트에 둘 수 없다. 고침: `__DATA_CONST,__shellcmds` (실측 동작, dyld 가 고친 뒤 읽기 전용) 또는 `__DATA`. 정답은 브리프대로 `__DATA` 를 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. 섹션 기반 등록을 왜 쓰나요? 중앙 배열이나 constructor 와 비교하면?</summary>

- **중앙 배열** `const struct cmd cmds[] = { … }`: 가장 단순하고 순서 · 중복이 소스에 보인다. 하지만 모듈을 추가할 때마다 공용 파일을 고쳐야 하고, 빌드 설정(Kconfig 등)으로 모듈을 빼면 `#ifdef` 가 그 배열에 몰린다.
- **constructor** (`__attribute__((constructor))` 로 런타임 리스트에 push): 런타임 코드 · RAM(연결 리스트) 이 필요하고, 실행 순서가 덜 통제되며, 베어메탈에서는 startup 이 `.init_array` 를 돌려야 한다 (BM-B08).
- **커스텀 섹션**: 링크 시점에 표가 완성되고 Flash 에 const 로 들어가 RAM 0 B, 런타임 등록 코드 0. 대가: 링커 · 컴파일러의 규칙(used/KEEP/GC/ASan/정렬/순서)을 알아야 하고 디버깅이 "조용히 빈 표" 형태.
꼬리: "디버거에서 표를 보려면?" → `p *__shellcmds_start@N` (gdb), lldb 는 `parray N shell_cmds_begin`; 또는 `objdump -s -j .shellcmds`.
</details>

<details><summary>Q2. `__start_foo` 는 누가, 언제 정의하나요? 섹션 이름 규칙은?</summary>

ELF 링커(bfd · lld — 둘 다 실측)가 정의한다. 조건: (1) 출력 섹션 이름이 C 식별자로 쓸 수 있는 문자열(`[A-Za-z_][A-Za-z0-9_]*`), (2) 코드에서 `__start_foo`/`__stop_foo` 를 참조, (3) 그 이름의 출력 섹션이 존재. 정의 안 되면 `undefined reference`(strong) 또는 0(weak). 실측: `D __start_shellcmds` = 섹션 시작, `__stop_` = 끝 다음 주소. Mach-O 는 `section$start$SEG$SECT` 로 같은 일을 하고, 섹션이 없으면 크기 0 섹션을 만들어 준다 (실측). 링커 스크립트가 출력 섹션을 직접 기술하면 거기서 `PROVIDE(__start_x = .)` 처럼 손으로 정의하는 게 보통이다.
</details>

<details><summary>Q3. `used`, `retain`, `KEEP` 의 차이는?</summary>

단계가 다르다. `used` = 컴파일러에게 "참조가 없어도 객체를 내보내라" (Mach-O 는 `[no dead strip]` 도 붙어서 ld64 dead strip 까지 막음). `retain` = 오브젝트 섹션에 `SHF_GNU_RETAIN`(readelf `R`) 을 달아 ELF 링커 GC 의 루트로 만든다 (GCC 11+ · clang 13+ 로 알려짐; 실측 gcc 15.2 · clang 21 에서 `WAR`). `KEEP(*(.x))` = 링커 스크립트에서 "이 입력 섹션은 GC 하지 마라". 실측 행렬: lld `--gc-sections` 에서 `used` 만 → 에러, `retain` → 4 entries; fw.ld 에서 KEEP 제거 → 0 entries, KEEP 있고 used 없음 → 0 entries.
꼬리: "`-z start-stop-gc` 가 뭐죠?" → `__start_`/`__stop_` 참조가 섹션을 살리는지 여부. 꺼져 있으면(bfd 기본) 참조만으로 그 이름의 입력 섹션 전부 생존.
</details>

<details><summary>Q4. 표의 순서는 무엇이 정하나요? Linux initcall 은 어떻게 레벨 순서를 보장하죠?</summary>

파일 사이 = 링크 명령의 입력 순서, 파일 안 = 컴파일러(실측: clang 정의 순, gcc 역순). 둘 다 계약이 아니다. 순서가 필요하면 (a) 런타임 정렬(이 레슨의 인덱스), (b) 레벨마다 다른 섹션을 만들고 레벨 순서대로 순회(추가 연습 1, Mach-O 도 가능), (c) 섹션 이름에 키를 넣고 링커 스크립트 `SORT` (= `SORT_BY_NAME`) 로 정렬. Linux 는 `.initcall<N>.init` 같은 레벨별 입력 섹션을 `vmlinux.lds.h` 에서 레벨 순으로 나열한다 (`include/linux/init.h`, 확인 권장). 실측: `KEEP(*(.initcall.*))` 만 쓰면 clang 오브젝트에서 `3 app 0 clock 2 uart 1 gpio`, `SORT` 를 넣으면 `0 1 2 3`. gcc 오브젝트는 섹션이 우연히 이름순으로 나와 SORT 없이도 맞았다 — "우연히 맞는" 것이 제일 위험하다. 같은 레벨 안의 순서는 Linux 도 링크 순서(Makefile 순서)에 기댄다.
</details>

<details><summary>Q5. ASan 빌드에서만 표가 깨졌어요. 왜죠? 어떻게 고치나요?</summary>

ASan 은 전역 객체마다 뒤에 레드존을 붙여(전역 크기를 키움) 섹션 안 항목 간격이 sizeof 보다 커진다. Mac clang 실측: 24 B 항목 간격 64 B (3개에 192 B), `p++` 가 레드존을 읽어 `global-buffer-overflow … 0 bytes after global variable 'shell_cmd_led'`. 고침: 항목에 `__attribute__((no_sanitize("address")))`. ELF 에서는 gcc 가 사용자 섹션 전역을 계측하지 않고, clang 은 C 식별자 이름 섹션만 예외로 둔다(점 들어간 섹션은 실측 gap 64). 그래서 "Linux CI 는 통과, Mac 개발자 빌드만 크래시" 가 된다. 바이트 수가 `sizeof` 의 배수인지 검사(`table_ok`, 링커 `ASSERT`)하면 이런 종류를 일찍 잡는다.
</details>

<details><summary>Q6. 정적 라이브러리에 넣었더니 명령이 없어졌어요.</summary>

아카이브 멤버는 "아직 미정의인 심볼을 정의할 때만" 링크된다 (TOOL-B09). 등록 파일은 보통 전부 static 이라 전역 심볼이 없다 (`ranlib: … has no symbols`, 실측). 해결: 그 `.a` 를 `-Wl,--whole-archive … -Wl,--no-whole-archive` (GNU) / `-Wl,-force_load,lib.a` (Mac) 로 링크, 오브젝트 라이브러리(CMake `OBJECT`)로 링크, 또는 모듈마다 전역 앵커 심볼을 하나 두고 `-u anchor` 로 끌어오기. 실측: 2 entries → 3 entries.
</details>

<details><summary>Q7. 표를 Flash 에 두는데 const 객체에 함수 포인터가 있어요. 문제 없나요?</summary>

MCU 정적 링크에서는 문제없다 — 링크 때 주소가 확정돼 `.shellcmds` 는 `A`(읽기 전용) Flash 섹션이다 (실측 fw.elf). 주의점 두 가지. (1) Cortex-M 함수 포인터는 Thumb 비트가 1 (실측 `0x51` = `cmd_help` 0x50|1) — 손으로 만든 표나 다른 도구가 만든 표에서 빠지면 INVSTATE UsageFault. (2) 호스트(PIE/공유 라이브러리)에서는 포인터를 로더가 고쳐 써야 해서 읽기 전용 텍스트에 못 둔다: Mac `__TEXT` 는 `Illegal text-relocations` (실측), `__DATA_CONST` 는 OK. ELF 에서도 그래서 섹션이 `WA` 로 나온다. 꼬리: "RAM 을 아끼려면?" → 포인터 대신 32비트 상대 오프셋을 저장하는 방식도 있다 (Linux 의 PREL32 initcall — 확인 권장).
</details>

<details><summary>Q8. 같은 이름의 명령을 두 모듈이 등록하면? 빌드에서 잡을 수 있나요?</summary>

static 이면 링크가 성공하고 둘 다 표에 들어간다 (실측 `duplicate: reboot (sys, pwr)`). 검색은 정렬 순서 · 링크 순서에 따라 아무거나 잡을 수 있으니, 정답처럼 부팅 시 중복을 검출해 거부(`SHELL_EDUP`)하거나 assert 한다. 빌드에서 잡으려면 항목 심볼을 외부 링키지로(`shell_cmd_<name>`): 실측 ld64 `duplicate symbol '_shell_cmd_reboot'`, GNU ld `multiple definition`. U-Boot 의 linker list 처럼 이름을 섹션 이름에 넣고 `SORT` 하면 링크 시점에 정렬된 표까지 얻는다 (U-Boot 문서 확인 권장).
</details>

<details><summary>Q9. 이 표를 디버깅할 때 제일 먼저 보는 것은?</summary>

(1) 항목 수: 부팅 로그(`shellcmds @ 0x00000510, 3 entries`)나 `nm` 의 start/end 차이. (2) `readelf -S` / `size -A` 로 출력 섹션 크기와 플래그(`R` 있나), `--print-gc-sections` 로 지워진 입력 섹션 (실측 `removing unused section '.shellcmds'`). (3) `nm` 으로 개별 항목 심볼이 있는지 (`r shell_cmd_led`). (4) Mac 은 `otool -l` 로 섹션이 진짜인지 0 크기인지 (`size 0x0` 이면 오타 의심). (5) 크기가 sizeof 의 배수인지 (ASan · 정렬).
</details>

## 9. 추가 연습

**연습 1 — initcall 레벨.** `INITCALL(level, fn)` 으로 `int (*)(void)` 를 레벨별 섹션(Mach-O `__DATA,__initcall<N>`, ELF `initcall<N>`)에 넣고 `run_initcalls()` 가 레벨 0 → 3 순서로 실행한다. 레벨 2 는 비어 있다. 같은 레벨 안의 순서는 검사하지 않는다 (컴파일러마다 다르다).

<details><summary>답</summary>

```c
#define INITCALL(l, fn) \
    static const initcall_t initcall_##fn __attribute__((used, section(INITCALL_SECTION(l)))) NO_ASAN = fn
```

레벨마다 `section$start/end` (Mac) 또는 weak `__start_initcallN` (ELF) 를 선언하고 `struct level_range lv[4]` 로 돈다. 빈 레벨: Mac 은 크기 0 섹션(begin == end), ELF 는 섹션이 아예 없어 strong 이면 링크 에러 → weak 로 받아 NULL == NULL. 실측 로그: Mac/VM clang `0clock 1gpio 1pinmux 3app 3sensor`, VM gcc `0clock 1pinmux 1gpio 3sensor 3app` — 레벨 순서는 같고 레벨 안 순서만 다르다. 테스트는 "레벨 숫자가 줄지 않는다" + 집합 비교로 이 차이를 허용한다. ELF 에서 SORT 로 하는 방법은 `initcall_host.ld` (`INSERT AFTER .data` 로 기본 스크립트에 끼워 넣음) 와 `fw/fw.ld` 의 `.initcalls` — `demo_elf.txt` 6번, `demo_fw.txt` 1번.
</details>

**연습 2 — 실패 정책.** initcall 하나(`sensor_init`)가 −5 를 돌려준다. 멈추지 말고 끝까지 돌며 실패 수 · 첫 에러 코드 · 레벨별 실행 수를 보고하라.

<details><summary>답</summary>

`struct initcall_report { ran, failed, first_err, per_level[4] }` 에 누적. 실측: `ran == 5`, `failed == 1`, `first_err == -5`, `per_level = {1, 2, 0, 2}`, 그리고 `3app` 까지 실행됨. Linux 도 initcall 실패를 로그로 남기고 다음으로 넘어가는 쪽이다 (`initcall_debug` 로 각 호출 시간·반환값 출력 — 확인 권장). 펌웨어에서는 "필수 레벨(클럭 · 메모리) 실패 = 안전 모드로 부팅, 선택 레벨(센서) 실패 = 기능만 끔" 처럼 레벨별로 정책을 다르게 두는 경우가 많다.
</details>

**연습 3 — 경계 검사 `table_ok(b, e, elem)`.** begin > end(반쪽 오타), 바이트 수가 elem 의 배수가 아님(레드존 · 패딩), elem == 0 을 거부하고 빈 표는 허용하라.

<details><summary>답</summary>

`uintptr_t` 로 바꿔 `ue < ub` → 0, `(ue - ub) % elem != 0` → 0. 포인터끼리 `<` 비교는 같은 배열 안에서만 정의돼 있어 서로 다른 섹션 심볼 비교는 엄밀히 C 표준 밖이다 → 정수로 바꿔 비교 (구현 정의 변환이지만 평평한 주소 공간에서는 의도대로 동작). 실측: `table_ok(arr+6, arr, 4) == 0`, 22 바이트 → 0, 빈 표 → 1. 펌웨어에서는 이 검사를 링커 `ASSERT` 로 옮기면 비용 0 (C-3 실측: `ld: TOOL-M03: .shellcmds stride is not sizeof(struct shell_cmd)`).
</details>

**연습 4 — 탭 완성 `shell_complete(names, n, prefix, out, cap)`.** 정렬된 이름에서 접두사 일치 수를 반환하고, 1개면 `"이름 "`, 여러 개면 가장 긴 공통 접두사, 0개면 `""`. `cap` 이 모자라면 −1.

<details><summary>답</summary>

lower bound 로 첫 후보를 찾고 `strncmp(names[i], prefix, plen) == 0` 인 동안 전진 → 범위 [first, last). 공통 접두사는 첫 이름 길이에서 시작해 각 후보와 비교하며 줄인다. 실측: `"he"` → `help ` (1), `"fl"` → `flash_` (3), `""` → 7개 · `""`, `"x"` → 0. 정렬된 인덱스를 이미 만들어 두었기 때문에 완성이 O(log n + k) 다 — 런타임 정렬의 덤.
</details>

**연습 5 — (생각) 링크 시점 정렬.** 런타임 정렬 없이 이름순 표를 얻으려면?

<details><summary>답</summary>

이름을 섹션 이름에 넣는다: `section(".shellcmd." #name)` + 스크립트 `KEEP(*(SORT(.shellcmd.*)))`. 그러면 링커가 이름순으로 배치하고 `shell_lookup` 이 섹션 위에서 바로 이진 검색할 수 있다 (RAM 인덱스 0 B). 대가: ELF + 링커 스크립트 전용(Mach-O 불가, `__start_` 도 불가 — 점 들어간 이름), 입력 섹션 수가 명령 수만큼 늘어남. U-Boot linker list 가 이 방식으로 알려져 있다 (확인 권장). `demo_initcall.c` 의 SORT 실측이 같은 메커니즘이다.
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o prac && ./prac
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined practice.c -o prac_asan && ./prac_asan
```

실제 출력 (Mac; ASan 빌드도 같음):

```text
initcall log: 0clock 1gpio 1pinmux 3app 3sensor 
18/18 passed
```

## 10. 핵심 정리

- 같은 이름의 섹션은 링커가 이어 붙인다 → 경계 심볼 두 개로 배열처럼 순회. Mach-O `section$start$SEG$SECT` (`__asm` 레이블), ELF `__start_<C식별자>` / `__stop_`, 스크립트면 직접 정의.
- 세 관문: **컴파일러**(`used` — `unused` 는 반대!), **아카이브**(`--whole-archive` / `-force_load`), **링커 GC**(`KEEP`, `retain`=R 플래그; lld 는 start-stop-gc 기본이라 `used` 만으론 에러).
- 간격 = sizeof 는 가정일 뿐: 타입 포인터로 `b + i`, `e - b`; Mac ASan 은 24 → 64 B (`no_sanitize("address")`); 정렬은 `aligned` 로 명시; 링커 `ASSERT` 로 지킨다.
- 오타는 Mac 에서 조용히 빈 표, ELF strong 이면 링크 에러, weak 면 NULL..NULL — 부팅 셀프 테스트로 항목 수를 확인.
- 순서 · 중복은 링크 결과가 정한다 (gcc 역순, clang 정의 순, 파일은 링크 순) → 런타임 정렬 + 중복 거부, 또는 `SORT` / 레벨별 섹션.
- 펌웨어: `.shellcmds` 는 Flash 의 `A` 섹션, 함수 포인터는 Thumb 비트 1, `KEEP` + `used` 둘 다 필요 (실측: 빼면 0 entries → `RESULT: FAIL`).

## 참고자료

- GNU ld 매뉴얼 — Input Section Keep (`KEEP`), Input Section Wildcards (`SORT`), Miscellaneous Commands (`INSERT`), Options (`-z start-stop-gc`, `--gc-sections`, `--print-gc-sections`): https://sourceware.org/binutils/docs/ld/
- LLD — `-z start-stop-gc` 설명: https://lld.llvm.org/ELF/start-stop-gc.html
- GCC 변수 속성 (`section`, `used`, `retain`, `aligned`): https://gcc.gnu.org/onlinedocs/gcc/Common-Variable-Attributes.html
- Clang 속성 레퍼런스 (`retain`, `used`, `no_sanitize`): https://clang.llvm.org/docs/AttributeReference.html
- Apple `ld(1)` man page — `section$start$`/`section$end$`, `-dead_strip`, `-force_load` (`man ld` on macOS)
- MaskRay, "Metadata sections, COMDAT and SHF_LINK_ORDER" (start-stop-gc 와 `SHF_GNU_RETAIN` 배경): https://maskray.me/blog/2021-01-31-metadata-sections-comdat-and-shf-link-order
- LLVM `AddressSanitizer.cpp` (전역 계측 제외 규칙) / GCC `asan.cc` (`asan_protect_global`): https://github.com/llvm/llvm-project/blob/main/llvm/lib/Transforms/Instrumentation/AddressSanitizer.cpp
- Linux `include/linux/init.h`, `include/asm-generic/vmlinux.lds.h` (initcall 레벨): https://github.com/torvalds/linux/blob/master/include/linux/init.h
- Zephyr Iterable Sections: https://docs.zephyrproject.org/latest/kernel/iterable_sections/index.html
- U-Boot Linker-Generated Arrays: https://docs.u-boot.org/en/latest/develop/linker_lists.html
- 관련 레슨: TOOL-B04 (오브젝트 섹션 · nm), TOOL-B05 (weak · 링크 에러), TOOL-B06 (size), TOOL-B09 (정적 라이브러리 · whole-archive), TOOL-B11 (ASan), TOOL-M01 (링커 스크립트), TOOL-M02 (맵 파일), TOOL-M13 (gc-sections 다이어트), TOOL-A07 (스크립트 심화 · ASSERT), BM-B08 (Cortex-M 부팅 · .init_array), ARM-M12 (UsageFault · Thumb 비트)
