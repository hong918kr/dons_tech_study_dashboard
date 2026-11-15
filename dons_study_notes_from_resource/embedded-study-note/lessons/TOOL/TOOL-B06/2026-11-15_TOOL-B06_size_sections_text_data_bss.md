---
id: TOOL-B06
time: 60분
updated: 2026-11-15
---
# TOOL-B06 size로 섹션 크기 읽기: text data bss

## 학습 목표

- `size` 의 두 출력 형식 — Berkeley(`text data bss dec hex filename`)와 `-A`(SysV: `section size addr`) — 을 읽고, **Flash = text + data, RAM = data + bss** 를 실제 Cortex-M4 이미지 숫자로 확인한다.
- 전역 배열의 초기화 방식(없음 / `= {0}` / 0 아닌 값 / `const`), 문자열 리터럴, `const char *` vs `const char *const` 포인터 표, `printf` 한 줄이 각각 **어느 섹션을 몇 바이트** 바꾸는지 실측 표로 말할 수 있다.
- `-A` 출력의 섹션을 Flash 전용 / Flash+RAM / RAM 전용 / 이미지에 안 실림(`.comment`, `.ARM.attributes`, `.debug_*`)으로 나누는 함수를 짜고, Berkeley 숫자와 교차 검증한다.
- `.o` 만 보고 크기를 판단하면 왜 틀리는지(printf 비용 0, `-fcommon` 의 bss 누락), 맥 `size -m`(Mach-O 세그먼트)과 ELF `size` 가 왜 다른지 안다.
- CI 크기 게이트(용량 초과 + PR 당 증가량)를 unsigned 뺄셈 함정 없이 구현한다.

## 1. 문제

"이 `arm-none-eabi-size` 출력 보고 Flash, RAM 몇 바이트 쓰는지 말해 보세요" — 펌웨어 면접에서 가장 흔한 툴체인 질문이고, 실무에서는 매 빌드마다 CI 로그 마지막 줄에 찍히는 숫자다. SSD 컨트롤러 펌웨어처럼 ROM/SRAM 이 빠듯한 칩에서는 "기능 하나 넣었더니 RAM 이 넘쳤다" 가 출시 일정을 흔든다. 숫자를 **섹션 단위로** 읽을 줄 알아야 범인을 바로 찾는다.

이 레슨의 실험 재료는 하나의 작은 "펌웨어 같은" 파일 `fw.c` 다. `-D` 플래그 하나씩으로 512 바이트 표(`uint16_t table[256]`)의 저장 방식, 문자열 출력, `printf` 사용을 바꾼다. 같은 파일을 두 툴체인으로 잰다:

| 어디서 | 명령 | 무엇을 봄 |
|---|---|---|
| 맥 (Apple clang 21) | `clang --target=thumbv7em-none-eabi -mcpu=cortex-m4 -Os -c fw.c` → `llvm-size -A` / `llvm-size` | 오브젝트(`.o`) 한 개 — 주소는 전부 0 |
| Lima VM (arm-none-eabi-gcc 14.2.1, GNU size 2.45.50) | `startup.c + fw.c` 를 `fw.ld`(FLASH 64K @0x08000000, RAM 16K @0x20000000)로 **완전 링크**, newlib-nano | 진짜 펌웨어 이미지(`fw.elf`)의 Flash/RAM |
| 맥 (호스트, 대조용) | `cc -Os fw.c` → `size -m` | arm64 Mach-O 세그먼트 보기 |

<!-- code: fw.c -->

`startup.c`(벡터 표 + `.data` 복사 + `.bss` 0 채우기)와 `fw.ld` 는 "주어진 것" 으로 쓴다 — 부팅 과정은 BM-B08, 링커 스크립트 문법은 TOOL-M01 의 몫이다. 측정은 전부 `bash demo_size.sh` 한 번으로 돌고, 결과는 `size_*.txt` 로 이 디렉터리에 남는다. **코딩 문제의 테스트 데이터는 이 txt 들을 글자 그대로 문자열로 넣은 것**이다.

아래 그림이 이 레슨의 핵심이다. `-DTBL_INIT` 빌드(0 아닌 초기값을 가진 512 B 표)의 실제 주소다 (VM `arm-none-eabi-objdump -h`, 5. 풀이에 원본).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">-DTBL_INIT 빌드: Flash = text 212 + data 516 = 728 B,  RAM = data 516 + bss 1604 = 2120 B</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">FLASH 0x08000000</text>
  <rect x="140" y="35" width="80" height="34" class="f-acc" stroke="currentColor"/>
  <text x="146" y="50" fill="currentColor" font-size="12">.isr_vector</text>
  <text x="146" y="64" fill="currentColor" font-size="12">64</text>
  <rect x="220" y="35" width="110" height="34" class="f-acc" stroke="currentColor"/>
  <text x="226" y="50" fill="currentColor" font-size="12">.text</text>
  <text x="226" y="64" fill="currentColor" font-size="12">148</text>
  <rect x="330" y="35" width="250" height="34" class="f-warn" stroke="currentColor"/>
  <text x="336" y="50" fill="currentColor" font-size="12">.data 초기값 (LMA 0x080000d4)</text>
  <text x="336" y="64" fill="currentColor" font-size="12">516</text>
  <text x="586" y="57" fill="currentColor" font-size="12" font-family="monospace">끝 0x080002d8</text>
  <text x="140" y="88" fill="currentColor" font-size="12">├── Berkeley text 212 (.isr_vector + .text + .rodata 0) ──┤├── data 516 ──┤</text>
  <path d="M455 70 L455 120 L200 120 L200 165" fill="none" stroke="currentColor" stroke-width="2"/>
  <path d="M194 155 L200 167 L206 155" fill="none" stroke="currentColor" stroke-width="2"/>
  <text x="215" y="140" fill="currentColor" font-size="12">Reset_Handler 가 리셋 직후 Flash → RAM 복사 (startup.c)</text>
  <text x="10" y="190" fill="currentColor" font-weight="bold">RAM 0x20000000</text>
  <rect x="140" y="175" width="120" height="34" class="f-warn" stroke="currentColor"/>
  <text x="146" y="190" fill="currentColor" font-size="12">.data (VMA)</text>
  <text x="146" y="204" fill="currentColor" font-size="12">516</text>
  <rect x="260" y="175" width="70" height="34" class="f-ok" stroke="currentColor"/>
  <text x="266" y="190" fill="currentColor" font-size="12">.bss</text>
  <text x="266" y="204" fill="currentColor" font-size="12">64</text>
  <rect x="330" y="175" width="250" height="34" class="f-ok" stroke="currentColor"/>
  <text x="336" y="190" fill="currentColor" font-size="12">._user_heap_stack (힙+스택 예약)</text>
  <text x="336" y="204" fill="currentColor" font-size="12">1540</text>
  <text x="586" y="197" fill="currentColor" font-size="12" font-family="monospace">끝 0x20000848</text>
  <text x="140" y="228" fill="currentColor" font-size="12">├─ data 516 ─┤├──────── Berkeley bss 1604 (= 64 + 1540) ────────┤</text>
  <text x="10" y="262" fill="currentColor">• .data 는 두 번 센다: Flash 에 초기값 사본(LMA), RAM 에 실행 사본(VMA). 그래서 data 가 두 공식에 다 들어간다.</text>
  <text x="10" y="284" fill="currentColor">• .bss 는 Flash 에 아무것도 없다 — 크기만 기록되고 startup 이 0 으로 채운다.</text>
  <text x="10" y="306" fill="currentColor">• 끝 주소 − 시작 주소: 0x2d8 = 728 = Flash,  0x848 = 2120 = RAM.</text>
</svg>
```

### Part A — Berkeley 형식 읽기와 공식

- `parse_berkeley(txt, rows, max)` — CI 로그처럼 `$ 명령줄` 이 섞인 텍스트에서 `text data bss dec hex filename` 머리줄을 찾고 그 아래 행들을 읽는다. 여러 파일 + `-t` 의 `(TOTALS)` 행도 행이다. `dec ≠ text+data+bss` 또는 `hex ≠ dec` 이면 망가진 입력(-1).
- `berk_flash(r) = text + data`, `berk_ram(r) = data + bss`.
- 맥 `size` 의 Mach-O 형식(`__TEXT __DATA __OBJC others dec hex`)은 Berkeley 가 아니다 → -1 로 거절.

### Part B — `-A` 형식과 섹션 분류 (함정 모음)

- `parse_sysv(txt, secs, max)` — `section size addr` 부터 `Total N` 까지. 숫자는 10진 또는 `-x` 로 찍은 `0x` 16진. Total 이 크기 합과 다르거나 Total 줄이 없으면(출력 잘림) -1.
- `classify(name)` → `REG_FLASH` / `REG_FLASH_RAM`(`.data`) / `REG_RAM` / `REG_NONE`. 이름은 정확히 같거나 `이름.xxx` 조각(`.text.startup`, `.rodata.str1.1`, `.data.boot_count`)만 인정 — `.textual`, `.database` 는 아님. `.ARM.exidx` 는 Flash 인데 `.ARM.attributes` 는 안 실린다.
- `flash_bytes()` / `ram_bytes()` — printf 빌드의 `-A` 에는 newlib 이 달고 온 `.debug_*` 9개(117,850 B)가 섞여 있다. 이걸 빼고 Berkeley 와 정확히 같은 7496 / 2032 가 나와야 한다.

### Part C — 예산 검사와 CI 크기 게이트

- `fits(u, flash_limit, ram_limit)` — 딱 맞으면 들어간다.
- `size_gate(before, after, budget)` — `GATE_FLASH_OVER | GATE_RAM_OVER | GATE_FLASH_GROWTH | GATE_RAM_GROWTH` 비트. 크기가 **줄어든** 변경은 절대 GROWTH 가 아니다 (unsigned 뺄셈 함정).
- `format_usage()` — `"Flash 7496/65536 B (11.4%), RAM 2032/16384 B (12.4%)"` 처럼 0.1% 단위 반올림, 버퍼 부족 시 -1.
- 실측 시나리오: 가상 부품 Flash 32 KB / RAM 4 KB, PR 당 +1024 B / +256 B. `uart_puts` → `printf`(nano) 로 바꾸는 PR 은 게이트에 걸리고, nano 를 빼고 full newlib printf 로 가면 **32 KB 를 넘는다**(33516 B).

## 2. Problem (English)

> **Read `size` output and enforce a firmware memory budget.**
>
> Your CI prints `arm-none-eabi-size fw.elf` (Berkeley) and `arm-none-eabi-size -A fw.elf` (SysV) for a Cortex-M4 image linked with FLASH at 0x08000000 and RAM at 0x20000000. Implement:
>
> - `int parse_berkeley(const char *txt, berk_row_t *rows, int max)` — find the `text data bss dec hex filename` header inside an arbitrary log, parse every row (including `(TOTALS)`), reject rows whose `dec`/`hex` don't match. `uint32_t berk_flash(const berk_row_t *)`, `uint32_t berk_ram(const berk_row_t *)`.
> - `int parse_sysv(const char *txt, section_t *secs, int max)` — parse `section size addr` rows up to `Total`, decimal or `0x` hex; verify the total. `region_t classify(const char *name)`; `uint32_t flash_bytes(...)`, `uint32_t ram_bytes(...)`.
> - `bool fits(usage_t, uint32_t flash_limit, uint32_t ram_limit)`, `unsigned size_gate(usage_t before, usage_t after, const budget_t *)`, `int format_usage(char *buf, size_t n, usage_t, const budget_t *)`.
>
> Constraints: no allocation, no assumptions about column widths (tabs vs spaces), debug and metadata sections must not count, a shrinking build must never trip the growth gate.
>
> Example: the Berkeley row `7400  96  1936  9432  24d8  fw.elf` (text, data, bss, dec, hex, filename) means Flash = 7496, RAM = 2032. The `-A` view of the same file says `Total 127364` — explain why that is not the Flash size.
>
> Follow-ups: why does `.data` count against both Flash and RAM? Why does adding one `printf` cost ~6.7 KB? What RAM does `size` *not* show you?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/TOOL-B06_s && /tmp/TOOL-B06_s
bash demo_size.sh        # (선택) 실험 다시 돌리기: Mac + VM, size_*.txt 갱신
```

처음 실행 결과 (실제 출력):

```text
FAIL starter.c:330  parse_berkeley(VM_INIT, r, 4) == 1
FAIL starter.c:331  r[0].text == 212u && r[0].data == 516u && r[0].bss == 1604u && strcmp(r[0].file, "fw.elf") == 0
FAIL starter.c:332  berk_flash(&r[0]) == 728u && berk_ram(&r[0]) == 2120u
FAIL starter.c:333  parse_berkeley(VM_PRINTF_FULL, r, 4) == 1 && berk_flash(&r[0]) == 33516u && berk_ram(&r[0]) == 3704u
FAIL starter.c:334  parse_berkeley(MAC_STRINGS, r, 4) == 1 && berk_flash(&r[0]) == 709u && berk_ram(&r[0]) == 68u
FAIL starter.c:335  parse_berkeley(VM_OBJS_TOTAL, r, 4) == 3 && strcmp(r[2].file, "(TOTALS)") == 0
FAIL starter.c:343  n == 8 && strcmp(s[3].name, ".data") == 0 && s[3].size == 516u && s[3].addr == 0x20000000u
FAIL starter.c:344  classify(".text") == REG_FLASH && classify(".rodata.str1.1") == REG_FLASH && classify(".isr_vector") == REG_FLASH
FAIL starter.c:345  classify(".ARM.exidx") == REG_FLASH && classify(".init_array") == REG_FLASH && classify(".text.startup") == REG_FLASH
FAIL starter.c:346  classify(".data") == REG_FLASH_RAM && classify(".data.boot_count") == REG_FLASH_RAM
FAIL starter.c:347  classify(".bss") == REG_RAM && classify("._user_heap_stack") == REG_RAM && classify(".noinit") == REG_RAM
FAIL starter.c:350  flash_bytes(s, n) == 728u && ram_bytes(s, n) == 2120u
FAIL starter.c:352  n == 18 && flash_bytes(s, n) == 7496u && ram_bytes(s, n) == 2032u
FAIL starter.c:354  n2 == 8 && s2[3].size == 0x204u && s2[3].addr == 0x20000000u && flash_bytes(s2, n2) == 728u && ram_bytes(s2, n2) == 2120u
FAIL starter.c:356  n == 10 && flash_bytes(s, n) == 709u && ram_bytes(s, n) == 68u
FAIL starter.c:368  u_const.flash == 712u && u_const.ram == 1608u && u_str.flash == 800u && u_full.flash == 33516u
FAIL starter.c:369  fits(u_pf, small.flash_limit, small.ram_limit) && !fits(u_full, small.flash_limit, small.ram_limit)
FAIL starter.c:370  fits(u_pf, 7496u, 2032u) && !fits(u_pf, 7495u, 2032u) && !fits(u_pf, 7496u, 2031u)
FAIL starter.c:373  size_gate(u_str, u_pf, &small) == (GATE_FLASH_GROWTH | GATE_RAM_GROWTH)
FAIL starter.c:374  size_gate(u_pf, u_full, &small) == (GATE_FLASH_OVER | GATE_FLASH_GROWTH | GATE_RAM_GROWTH)
FAIL starter.c:377  format_usage(line, sizeof line, u_pf, &f4) > 0 && strcmp(line, "Flash 7496/65536 B (11.4%), RAM 2032/16384 B (12.4%)") == 0
FAIL starter.c:379  format_usage(line, sizeof line, u_full, &small) > 0 && strcmp(line, "Flash 33516/32768 B (102.3%), RAM 3704/4096 B (90.4%)") == 0
11/33 passed
```

통과하는 11개는 "거절해야 하는" 체크(-1 기대)와 "GATE_OK 기대" 체크라서 스텁이 우연히 맞는 것이다. 하네스: `next_line()` (한 줄 복사, TODO 아님), `CHECK` 매크로, `N/M passed`, 실패 시 exit 1. 테스트 문자열 `VM_INIT`, `VM_PRINTF`… 는 각각 같은 이름의 `size_vm_*.txt` / `size_mac_*.txt` 내용이다 (파일 이름이 문자열 위 주석에 있다).

## 4. 힌트

<details><summary>힌트 1 — 칸 폭을 믿지 말 것</summary>

GNU size 의 Berkeley 출력은 **탭**으로 칸을 나누고(`"   text\t   data\t..."`), `-A` 는 공백으로 맞춘다. 둘 다 `sscanf` 의 공백 지시자(`" "`, `%s`, `%lu` 앞의 자동 공백 건너뛰기)가 탭과 공백을 똑같이 먹으니 칸 위치 대신 "토큰 순서" 로 읽는다. 머리줄은 첫 네 토큰이 `text data bss dec` 인 줄.
</details>

<details><summary>힌트 2 — 표는 어디서 끝나나</summary>

Berkeley: 머리줄 다음에 `"%lu %lu %lu %lu %lx %47s"` 6칸이 안 읽히는 첫 줄(`$ arm-none-eabi-size -A ...`, 빈 줄)에서 멈춘다. `-A`: `Total` 줄에서 끝 — 그때 합계를 검증하고 돌아간다. Total 없이 텍스트가 끝나면 잘린 출력이다.
</details>

<details><summary>힌트 3 — 16진 주소</summary>

`size -A -x` 는 `0x204   0x20000000` 처럼 찍는다. `strtoul(s, &end, 0)` (base 0)은 `0x` 접두사면 16진, 아니면 10진으로 읽는다. `*end != '\0'` 이면 숫자가 아닌 찌꺼기가 있는 것.
</details>

<details><summary>힌트 4 — 분류는 규칙 표 하나로</summary>

`{".text", REG_FLASH}, {".data", REG_FLASH_RAM}, {".bss", REG_RAM}, ...` 표를 돌면서 `strncmp(name, base, k) == 0 && (name[k] == '\0' || name[k] == '.')`. 마지막 조건이 `.textual` 과 `.ARM.attributes`(규칙 `.ARM.exidx` 와 다름)를 걸러낸다. 표에 없는 이름은 `REG_NONE`.
</details>

<details><summary>힌트 5 — 게이트의 뺄셈</summary>

`after.flash - before.flash > max` 는 `after < before` 이면 unsigned 랩어라운드로 40억이 된다. `after > before &&` 를 먼저. 퍼센트는 `(used * 1000 + limit / 2) / limit` 를 **uint64_t** 로 (0.1% 단위 반올림), `snprintf` 반환값 ≥ n 이면 잘린 것.
</details>

## 5. 풀이

### 실험 결과 먼저 — `bash demo_size.sh`

<!-- code: demo_size.sh -->

실제 출력 (요약표 부분; 맥 호스트 부분은 Part C 에서):

```text
===== Mac: clang thumbv7em-none-eabi -mcpu=cortex-m4 -Os -c (object only) =====
variant    text   data    bss   (llvm-size Berkeley on fw.o)
uninit      106      4    576
zero        106      4    576
init        106    516     64
const       602      4     64
strings     705      4     64
strptrs     693     16     64
printf      714      4     64
common      106      4     64   <- -fcommon

===== VM elinux: arm-none-eabi-gcc full link (nano.specs + nosys.specs, fw.ld) =====
variant         text   data    bss |  Flash    RAM
uninit           216      4   2116 |    220   2120
zero             216      4   2116 |    220   2120
init             212    516   1604 |    728   2120
const            708      4   1604 |    712   1608
strings          796      4   1604 |    800   1608
strptrs          780     16   1600 |    796   1616
printf          7400     96   1936 |   7496   2032
printf_float   22476    460   1940 |  22936   2400
printf_full    31792   1724   1980 |  33516   3704
```

VM 링크 이미지의 섹션별 숫자(`-A`, `.comment`/`.ARM.attributes`/`.debug_*` 제외)와 각 변경이 뜻하는 것:

| 변형 | 바꾼 것 | .text | .rodata | .data | .bss | heap+stack | Flash | RAM | 읽는 법 |
|---|---|---|---|---|---|---|---|---|---|
| uninit | `uint16_t table[256];` | 152 | 0 | 4 | 576 | 1540 | 220 | 2120 | 표 512 + `rx_ring` 64 = .bss 576. Flash 비용 0 |
| zero | `= {0}` | 152 | 0 | 4 | 576 | 1540 | 220 | 2120 | uninit 과 **완전히 같다** — 0 초기화는 .bss |
| init | `= {1, 2, 3}` | 148 | 0 | 516 | 64 | 1540 | 728 | 2120 | 0 아닌 값 하나 → 512 B 전체가 .data: Flash +508, RAM 그대로 |
| const | `const ... = {1,2,3}` | 132 | 512 | 4 | 64 | 1540 | 712 | 1608 | Flash 로만 이사: RAM −512 |
| strings | + `uart_puts` 3 메시지 | 168 | 564 | 4 | 64 | 1540 | 800 | 1608 | .rodata += 포인터 표 12 + 문자열 39 + 정렬 1 |
| strptrs | `const char *msgs[]` | 164 | 552 | 16 | 64 | 1536 | 796 | 1616 | 포인터 3개(12 B)가 .data 로 → RAM 비용 |
| printf | `printf` (nano) | 6684 | 644 | 96 | 396 | 1540 | 7496 | 2032 | 한 줄에 Flash +6696, RAM +424 |
| printf_float | nano + `-u _printf_float` | 20948 | 1456 | 460 | 400 | 1540 | 22936 | 2400 | 부동소수 서식 지원만으로 +15 KB |
| printf_full | nano 없이 (full newlib) | 30236 | 1484 | 1724 | 444 | 1536 | 33516 | 3704 | 32 KB 부품이면 이미 넘침 |

(`printf*` 행에는 `.ARM.exidx` 8 B 와 `.isr_vector` 64 B 가 더 있고, 모든 행에 `.isr_vector` 64 가 Flash 에 있다. heap+stack 이 1536/1540 으로 갈리는 건 `._user_heap_stack` 시작을 8 바이트로 맞추는 패딩 차이다.)

### Part A — Berkeley: 세 칸의 정확한 뜻

VM `size_vm_init.txt` (실제):

```text
$ arm-none-eabi-size fw.elf    # -DTBL_INIT --specs=nano.specs --specs=nosys.specs
   text	   data	    bss	    dec	    hex	filename
    212	    516	   1604	   2332	    91c	fw.elf
$ arm-none-eabi-size -A fw.elf
fw.elf  :
section             size        addr
.isr_vector           64   134217728
.text                148   134217792
.rodata                0   134217940
.data                516   536870912
.bss                  64   536871428
._user_heap_stack   1540   536871492
.comment              38           0
.ARM.attributes       46           0
Total               2416
```

두 출력을 맞춰 보면 Berkeley 의 세 칸은 **섹션 이름이 아니라 섹션 플래그로** 모은 합이다:

| Berkeley 칸 | 들어가는 섹션 (이 빌드) | 플래그 (objdump -h) | 합 |
|---|---|---|---|
| text | `.isr_vector` 64 + `.text` 148 + `.rodata` 0 | ALLOC + LOAD + **READONLY** | 212 |
| data | `.data` 516 | ALLOC + LOAD, 쓰기 가능 | 516 |
| bss | `.bss` 64 + `._user_heap_stack` 1540 | ALLOC 만 (파일 내용 없음, NOBITS) | 1604 |
| (안 셈) | `.comment` 38, `.ARM.attributes` 46 | ALLOC 없음 | — |

그래서 "Berkeley `text` = 코드" 가 아니다 — **읽기 전용 상수(.rodata)와 벡터 표, unwind 표(.ARM.exidx)까지 다 들어간다**. const 빌드에서 `.rodata` 512 가 text 708 안에 들어 있는 게 그 증거다. 그리고 STM32CubeIDE 식 `._user_heap_stack` 예약은 bss 에 붙는다 — "bss 1604 인데 내 전역은 64 B 밖에 없는데?" 의 정답. (GNU size 가 정확히 어떤 플래그 조합을 어느 칸에 넣는지는 binutils 소스 기준 확인 권장. 위 표는 이 레슨의 9개 이미지에서 숫자로 맞춰 본 것.)

`dec` = text + data + bss = 2332, `hex` = 0x91c = 2332. 이 둘은 **Flash 도 RAM 도 아닌 의미 없는 합**이다 (.data 를 한 번만 세고 bss 를 더하니까). `-A` 의 `Total 2416` 은 거기에 `.comment` + `.ARM.attributes` 84 B 까지 더한 값이다.

**왜 .data 는 두 번 세나** — `size_vm_init_objdump.txt` (실제):

```text
Idx Name          Size      VMA       LMA       File off  Algn
  0 .isr_vector   00000040  08000000  08000000  00010000  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, DATA
  1 .text         00000094  08000040  08000040  00010040  2**2
                  CONTENTS, ALLOC, LOAD, READONLY, CODE
  2 .rodata       00000000  080000d4  080000d4  00020204  2**0
                  CONTENTS, ALLOC, LOAD, DATA
  3 .data         00000204  20000000  080000d4  00020000  2**2
                  CONTENTS, ALLOC, LOAD, DATA
  4 .bss          00000040  20000204  080002d8  00020204  2**0
                  ALLOC
  5 ._user_heap_stack 00000604  20000244  080002d8  00020244  2**0
                  ALLOC
```

`.data` 만 **VMA(실행 주소, RAM 0x20000000) ≠ LMA(적재 주소, Flash 0x080000d4)** 다. 프로그래머(ST-Link, J-Link)는 LMA 에 초기값을 굽고, 전원이 들어오면 `Reset_Handler` 가 LMA → VMA 로 복사한다. 초기값이 RAM 에 저절로 있을 수 없으니(전원 꺼지면 사라짐) Flash 에 사본이 하나 더 필요한 것 — 그래서 `Flash = text + data`, `RAM = data + bss`. `.bss` 의 LMA 080002d8 은 의미 없다 (CONTENTS 플래그가 없다 = 파일에 바이트가 없다). 0x080000d4 + 0x204 = 0x080002d8 → 이미지 끝 − 0x08000000 = 728 = Flash. 연습 3 이 이 계산을 코드로 한다.

<!-- code: fw.ld -->

<!-- code: startup.c -->

**여러 파일 + `-t`** — `size_vm_objs_total.txt` (실제):

```text
$ arm-none-eabi-size -t startup.o fw.o
   text	   data	    bss	    dec	    hex	filename
    132	      0	      0	    132	     84	startup.o
     80	    516	     64	    660	    294	fw.o
    212	    516	     64	    792	    318	(TOTALS)
```

`.o` 합계 text 212 는 링크된 이미지 text 212 와 같지만 bss 64 는 1604 가 아니다 — 힙/스택 예약은 링커 스크립트가 만드는 것이라 `.o` 에는 없다. `(TOTALS)` 는 파일 이름 칸에 들어간 그냥 한 행이라 파서는 따로 처리할 필요가 없다 (체크: 앞 두 행의 합 == TOTALS 행).

**구현 요점** (`parse_berkeley`): ① 머리줄 전까지는 `sscanf("%15s %15s %15s %15s")` 로 첫 네 토큰만 비교, ② 행은 `"%lu %lu %lu %lu %lx %47s"` — `hex` 칸은 `%lx`, ③ `dec != t+d+b || hex != dec` 면 -1 (복붙하다 숫자가 바뀐 로그를 잡는다), ④ `n >= max` 면 -1 (조용히 버리지 않는다), ⑤ 머리줄을 못 찾았거나 행이 0개면 -1 — 맥 Mach-O `size` 출력(`__TEXT __DATA __OBJC others dec hex`)이 여기서 거절된다.

### Part B — `-A`: 섹션을 네 칸으로 나누기

```svg
<svg viewBox="0 0 740 320" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">printf(nano) 빌드의 -A 18줄 → 4칸 (size_vm_printf.txt)</text>
  <rect x="10" y="35" width="175" height="150" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="56" fill="currentColor" font-weight="bold">REG_FLASH</text>
  <text x="20" y="78" fill="currentColor" font-size="12" font-family="monospace">.isr_vector    64</text>
  <text x="20" y="96" fill="currentColor" font-size="12" font-family="monospace">.text        6684</text>
  <text x="20" y="114" fill="currentColor" font-size="12" font-family="monospace">.rodata       644</text>
  <text x="20" y="132" fill="currentColor" font-size="12" font-family="monospace">.ARM.exidx      8</text>
  <text x="20" y="170" fill="currentColor" font-size="12">Berkeley text 7400</text>
  <rect x="195" y="35" width="160" height="150" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="205" y="56" fill="currentColor" font-weight="bold">REG_FLASH_RAM</text>
  <text x="205" y="78" fill="currentColor" font-size="12" font-family="monospace">.data          96</text>
  <text x="205" y="110" fill="currentColor" font-size="12">초기값 Flash + 실행</text>
  <text x="205" y="128" fill="currentColor" font-size="12">사본 RAM</text>
  <text x="205" y="170" fill="currentColor" font-size="12">Berkeley data 96</text>
  <rect x="365" y="35" width="185" height="150" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="375" y="56" fill="currentColor" font-weight="bold">REG_RAM</text>
  <text x="375" y="78" fill="currentColor" font-size="12" font-family="monospace">.bss              396</text>
  <text x="375" y="96" fill="currentColor" font-size="12" font-family="monospace">._user_heap_stack 1540</text>
  <text x="375" y="128" fill="currentColor" font-size="12">Flash 에 바이트 없음</text>
  <text x="375" y="170" fill="currentColor" font-size="12">Berkeley bss 1936</text>
  <rect x="560" y="35" width="170" height="150" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="570" y="56" fill="currentColor" font-weight="bold">REG_NONE</text>
  <text x="570" y="78" fill="currentColor" font-size="12" font-family="monospace">.comment        38</text>
  <text x="570" y="96" fill="currentColor" font-size="12" font-family="monospace">.ARM.attributes 44</text>
  <text x="570" y="114" fill="currentColor" font-size="12" font-family="monospace">.debug_* (9) 117850</text>
  <text x="570" y="146" fill="currentColor" font-size="12">칩에 안 구움</text>
  <text x="570" y="170" fill="currentColor" font-size="12">Berkeley 무시</text>
  <rect x="10" y="205" width="345" height="40" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="230" fill="currentColor">Flash = 7400 + 96 = 7496</text>
  <rect x="195" y="255" width="355" height="40" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="205" y="280" fill="currentColor">RAM = 96 + 1936 = 2032</text>
  <text x="565" y="230" fill="currentColor" font-size="12">-A Total 127364</text>
  <text x="565" y="248" fill="currentColor" font-size="12">= 9432 + 38 + 44</text>
  <text x="565" y="266" fill="currentColor" font-size="12">  + 117850</text>
  <text x="565" y="290" fill="currentColor" font-size="12">≠ Flash (17배 과대)</text>
</svg>
```

`size_vm_printf.txt` 의 `-A` 부분 (실제):

```text
section               size        addr
.isr_vector             64   134217728
.text                 6684   134217792
.rodata                644   134224476
.ARM.exidx               8   134225120
.data                   96   536870912
.bss                   396   536871008
._user_heap_stack     1540   536871404
.comment                38           0
.ARM.attributes         44           0
.debug_info          62443           0
.debug_abbrev        15185           0
.debug_loclists      10426           0
.debug_aranges        1144           0
.debug_line          20422           0
.debug_str            5176           0
.debug_frame          2392           0
.debug_rnglists        503           0
.debug_line_str        159           0
Total               127364
```

우리 코드는 `-g` 없이 빌드했는데 `.debug_*` 가 117 KB 나 있다 — **newlib-nano 의 libc_nano.a 가 디버그 정보를 품고 배포**되기 때문이다 (printf 를 안 쓴 빌드에는 없다). 디버그 섹션은 addr 0, ALLOC 아님 — 칩에 안 올라간다. "`-A` 의 Total 을 Flash 로 보고" 하면 17배 틀린다.

**주소 칸으로 확인**: 134217728 = 0x08000000(Flash), 536870912 = 0x20000000(RAM), 0 = 안 실림. `-A` 는 기본이 10진이라 읽기 불편하다 — `-x` 로 찍으면(`size_vm_init_hex.txt`, 실제):

```text
section              size         addr
.isr_vector          0x40    0x8000000
.text                0x94    0x8000040
.rodata               0x0    0x80000d4
.data               0x204   0x20000000
.bss                 0x40   0x20000204
._user_heap_stack   0x604   0x20000244
.comment             0x26          0x0
.ARM.attributes      0x2e          0x0
Total               0x970
```

`.data` 의 addr 는 **VMA**(0x20000000)만 나온다. `-A` 로는 LMA 를 볼 수 없으니 ".data 가 Flash 도 먹는다" 는 이름(또는 objdump -h 의 LMA)으로 알아야 한다 — 그래서 분류를 이름으로 한다. (더 정확한 방법은 섹션 플래그 + LMA 를 읽는 것: 맵 파일 분석 TOOL-M02, ELF 직접 파싱 TOOL-A11.)

**맥 `.o` 의 `-A`** — `size_mac_strings.txt` (실제, `llvm-size -A`):

```text
fw.o  :
section             size   addr
.text                134      0
.ARM.exidx             8      0
.rodata              524      0
.data                  4      0
.rodata.str1.1        39      0
.bss                  64      0
.comment              51      0
.note.GNU-stack        0      0
.ARM.attributes       65      0
.llvm_addrsig          0      0
Total                889

   text	   data	    bss	    dec	    hex	filename
    705	      4	     64	    773	    305	fw.o
```

ELF 링크 이미지와 다른 점:

| 보이는 것 | 이유 |
|---|---|
| addr 가 전부 0 | 아직 링크 전 — 주소는 링커가 정한다 (TOOL-B04) |
| `.rodata.str1.1` 39 | 문자열 리터럴 전용 "병합 가능(mergeable), 1바이트 정렬" 섹션. 9 + 16 + 14 = 39 (`"boot ok\n"` 8+NUL …). 링크 때 `.rodata` 로 합쳐진다 (VM: 524 + 39 + 정렬 1 = 564) |
| `.rodata` 524 | 표 512 + `msgs` 포인터 3개 × 4 B |
| `.ARM.exidx` 8 | clang 은 이 타깃에서 unwind 표 항목을 기본으로 내고 gcc 는 안 낸다 (VM gcc 빌드는 printf 를 링크했을 때만 libc 에서 8 B). 기본값 차이는 이 두 컴파일러에서의 관찰 — 플래그 `-fno-unwind-tables` 등으로 바뀔 수 있음, 확인 권장 |
| `.note.GNU-stack`, `.llvm_addrsig` 0 | 링커에게 주는 메모(스택 실행 불가 / 주소 중요 심볼 목록). 이미지에 안 실림 |

검증: `.text` 134 + `.ARM.exidx` 8 + `.rodata` 524 + `.rodata.str1.1` 39 = 705 = Berkeley text. Flash 709, RAM 68.

**`-fcommon` 함정** — `size_mac_common.txt` (실제):

```text
$ llvm-size fw.o
   text	   data	    bss	    dec	    hex	filename
    106	      4	     64	    174	     ae	fw.o
$ llvm-nm fw.o | grep table
00000200 C table
```

`-fcommon` 이면 초기값 없는 `table` 이 COMMON 심볼(`C`, 크기 0x200)이 되어 **`.o` 의 어느 섹션에도 없다** → bss 576 이 아니라 64. VM gcc 도 같다: `-fcommon` `.o` 는 bss 64, 링크 후 이미지는 bss 2116 (uninit 과 동일, 실측). GCC 10 / Clang 11 부터 기본이 `-fno-common` 이라 요즘은 덜 보이지만 오래된 프로젝트·벤더 IDE 설정에서는 아직 만난다 (기본값 변경 버전은 기억에 의존, 확인 권장. common 심볼 자체는 TOOL-B04/B05).

**구현 요점** (`parse_sysv`, `classify`):

- `sscanf(line, "%63s %31s %31s")` 로 세 토큰, `strtoul(b, &e, 0)` 로 10진/16진 둘 다. 이름 31자 초과, 칸 수 이상, `n >= max` 는 -1.
- `Total` 을 만나면 `tot == sum` 검사 후 반환. 이 검사가 "로그를 복붙하다 한 줄 빠진" 입력을 잡는다 (BAD_TOTAL 체크).
- 분류는 규칙 표 + "정확히 같거나 뒤가 `.`" — 아래 실수 3 참고. 표에 없는 이름은 `REG_NONE` 이라, 프로젝트가 `.ramfunc`, `.ccmram` 같은 커스텀 섹션을 쓰면 표에 추가해야 한다 (TOOL-M03, TOOL-A07).

### Part C — printf 가 KB 단위인 이유, 그리고 게이트

맥 `.o` 만 보면 printf 빌드는 strings 빌드보다 **9 B 큰** 714 다 (`.text` 는 오히려 134 → 122 로 줄었다: `uart_puts` 루프가 사라지고 `bl printf` 한 번). 진짜 비용은 링크해야 보인다. VM 이미지 `nm --size-sort` 상위 (`size_vm_printf_top.txt`, 실제):

```text
$ arm-none-eabi-nm --size-sort -S fw.elf | grep " [Tt] " | tail -8    # printf
08000b74 00000114 T __sflush_r
080005a0 00000124 T _printf_common
08001860 00000130 T memmove
080016f8 00000134 T memcpy
080001a8 00000244 T _vfiprintf_r
080001a8 00000244 T _vfprintf_r
080006c4 0000028c T _printf_i
080011c8 000002d8 T __sfvwrite_r
$ arm-none-eabi-nm --size-sort -S fw.elf | grep " [Tt] " | tail -8    # printf_full
08006c9c 0000027a T __subdf3
08004e14 000002d8 T __sfvwrite_r
08006760 000002de T strcmp
080063d8 00000348 T _realloc_r
08001eac 0000058e T _malloc_r
08005524 00000d70 T _vfiprintf_r
0800256c 00001048 T _dtoa_r
08000398 00001a90 T _vfprintf_r
```

`printf` 는 함수 하나가 아니라 **서식 해석기(`_vfprintf_r`) + stdio 버퍼 계층(`__sfvwrite_r`, `__sflush_r`, `FILE` 구조체 `__sf` 가 .bss) + malloc(버퍼 할당)** 을 끌고 온다. nano 에서는 `_vfprintf_r` 과 `_vfiprintf_r` 이 같은 주소(0x080001a8)의 별칭이고, full newlib 은 `_vfprintf_r` 하나가 6800 B(0x1a90), 부동소수 변환 `_dtoa_r` 4168 B, 소프트 double 연산(`__subdf3` 등)까지 들어온다. nano 는 기본으로 `%f` 를 빼서 작고, `-u _printf_float` 로 다시 넣으면 Flash 7496 → 22936 이 된다.

링크 때 나온 경고도 실제로 떴다 (`size_vm_printf_ldwarn.txt` 첫 두 줄):

```text
ld: /usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/lib/thumb/v7e-m/nofp/libc_nano.a(libc_a-closer.o): in function `_close_r':
.../newlib-4.6.0.20260123/build_nano/arm-none-eabi/thumb/v7e-m/nofp/newlib/../../../../../../newlib/libc/reent/closer.c:47:(.text+0xc): warning: _close is not implemented and will always fail
```

`nosys.specs` 는 `_write`, `_close` … 를 "항상 실패" 스텁으로 채운다 (`_close`, `_fstat`, `_isatty`, `_lseek`, `_read`, `_write` 6개 경고). 진짜 펌웨어는 `_write` 를 UART 로 구현한다. 크기 측정에는 상관없지만 **이 빌드의 printf 는 아무것도 출력하지 않는다**는 걸 기억할 것.

**포인터 표 함정 (strptrs)**: `const char *msgs[]` 는 "const char 를 가리키는 (const 아닌) 포인터" 의 배열이다. 배열 자체가 쓰기 가능하니 `.data` 로 간다 — data 4 → 16 (포인터 3 × 4 B), .rodata 564 → 552. `const char *const msgs[]` 로 두 번째 `const` 를 붙이면 Flash 에만 산다. 이 실험에서 처음엔 `static` 을 붙였더니 **두 변형의 크기가 똑같았다** — 아무도 쓰지 않는 `static` 배열은 컴파일러가 상수로 취급해 버린 것. 그래서 `fw.c` 에서는 외부 링크(non-static)로 바꿔 측정했다. 실제 코드(여러 파일에서 보는 전역 표)는 대부분 외부 링크라 이 함정에 걸린다.

**맥 호스트 `size -m` (대조)** — `size_mac_host.txt` (실제, arm64 Mach-O, `-DTBL_INIT`):

```text
$ size -m fw.o        # cc -Os -DTBL_INIT -c (arm64 Mach-O)
Segment : 728
	Section (__TEXT, __text): 112
	Section (__DATA, __data): 516
	Section (__DATA, __bss): 64 (zerofill)
	Section (__LD, __compact_unwind): 32
	total 724
total 728
$ size fw.o
__TEXT	__DATA	__OBJC	others	dec	hex
112	580	0	32	724	2d4
$ size -m host_fw     # linked executable
Segment __PAGEZERO: 4294967296 (zero fill) 
Segment __TEXT: 16384
	Section __text: 112
	Section __unwind_info: 88
	total 200
Segment __DATA: 16384
	Section __data: 516
	Section __bss: 64 (zerofill)
	total 580
Segment __LINKEDIT: 16384
total 4295016448
$ size host_fw
__TEXT	__DATA	__OBJC	others	dec	hex
16384	16384	0	4294983680	4295016448	10000c000
```

- 맥 `size` 는 **세그먼트**(가상 메모리 페이지 묶음) 단위다. 실행 파일의 `__TEXT` 16384 는 16 KB 페이지 하나 — 내용 200 B 와 무관하다. `__PAGEZERO` 4 GB 는 NULL 역참조를 잡으려고 비워 둔 주소 공간이지 메모리가 아니다. 펌웨어 예산 질문에 이 숫자를 쓰면 안 된다.
- Mach-O `__DATA` 칸(580)은 `__data` + `__bss` 를 합친 것 — Berkeley 의 data 와 뜻이 다르다. ELF 대응: `__TEXT,__text` ↔ `.text`, `__TEXT,__const`/`__cstring` ↔ `.rodata*`, `__DATA,__data` ↔ `.data`, `__DATA,__bss`/`__common` ↔ `.bss` (TOOL-B04).
- 맥 `/usr/bin/size` 는 ELF 를 못 읽는다 (실측: `error: .../usr/bin/size: file: printf.elf is not an object file`). ELF 는 `/Library/Developer/CommandLineTools/usr/bin/llvm-size` 로 — VM 의 printf.elf 를 맥으로 가져와 돌리면 `7400 96 1936 9432 24d8` 로 GNU size 와 똑같다 (실측). `/usr/local/bin/llvm-size` 는 이 맥에서 LLVM 3.2 라 쓰지 않는다.

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Flash 사용량 (VM 링크 이미지, 실측) vs 가상 부품 32 KB</text>
  <text x="10" y="58" fill="currentColor" font-size="12">uart_puts</text>
  <rect x="120" y="44" width="14" height="20" class="f-ok" stroke="currentColor"/>
  <text x="142" y="58" fill="currentColor" font-size="12">800</text>
  <text x="10" y="98" fill="currentColor" font-size="12">printf (nano)</text>
  <rect x="120" y="84" width="132" height="20" class="f-acc" stroke="currentColor"/>
  <text x="260" y="98" fill="currentColor" font-size="12">7496  (+6696)</text>
  <text x="10" y="138" fill="currentColor" font-size="12">nano + %f</text>
  <rect x="120" y="124" width="405" height="20" class="f-warn" stroke="currentColor"/>
  <text x="533" y="138" fill="currentColor" font-size="12">22936</text>
  <text x="10" y="178" fill="currentColor" font-size="12">full newlib</text>
  <rect x="120" y="164" width="592" height="20" class="f-warn" stroke="currentColor"/>
  <text x="560" y="200" fill="currentColor" font-size="12">33516 → 초과</text>
  <path d="M698 34 L698 192" stroke="currentColor" stroke-width="2" stroke-dasharray="5 4"/>
  <text x="600" y="30" fill="currentColor" font-size="12">32768 (32 KB)</text>
  <text x="10" y="230" fill="currentColor" font-size="12">막대 길이 ∝ 바이트 (1 px ≈ 56.6 B).</text>
  <text x="10" y="252" fill="currentColor" font-size="12">size_gate: strings→printf = FLASH_GROWTH | RAM_GROWTH,  printf→full = + FLASH_OVER</text>
</svg>
```

**게이트 구현 요점**:

- `fits`: `u.flash <= flash_limit && u.ram <= ram_limit` — 딱 맞는 7496/2032 는 통과, 1 B 모자라면 실패 (체크 3개).
- `size_gate`: OVER 는 `after` 만 보고, GROWTH 는 `after.x > before.x && after.x - before.x > max`. init → const 는 Flash 728 → 712, RAM 2120 → 1608 로 **줄었으므로** GATE_OK 여야 한다 — 비교 없이 빼면 `712u - 728u` = 4294967280 이 되어 거짓 경보.
- 실측 시나리오: const → strings (+88 B) 는 OK, strings → printf 는 Flash +6696 > 1024, RAM +424 > 256 → `FLASH_GROWTH | RAM_GROWTH`. printf → full 은 33516 > 32768 → `FLASH_OVER` 까지. RAM 3704 ≤ 4096 이라 RAM_OVER 는 아니다.
- `format_usage`: 7496 × 1000 / 65536 = 114.38 → 반올림 114 → `11.4%`. 33516 → 1022.8 → 1023 → `102.3%`. 곱셈은 `uint64_t` 로 (4 GB 근처 값 × 1000 은 32비트를 넘는다). 그리고 RAM 의 2120 에는 **힙 512 + 스택 1024 예약이 이미 들어 있다** — 이 예약이 실제 최대 스택 깊이보다 작으면 size 가 "들어간다" 고 해도 런타임에 스택이 .bss 를 덮는다 (TOOL-A08 `-fstack-usage`).

실행:

```text
$ cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/TOOL-B06_x && /tmp/TOOL-B06_x
33/33 passed
$ cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-sanitize-recover=undefined solution.c -o /tmp/TOOL-B06_san_solution && /tmp/TOOL-B06_san_solution
33/33 passed
```

(starter 도 sanitizer 빌드에서 경고 없이 `11/33 passed`, exit 1.)

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

## 7. 자주 하는 실수

**1. `-A` 의 Total(또는 Berkeley 의 dec)을 Flash 로 읽는다.** printf 빌드 Total 127364 vs 실제 Flash 7496. Total 은 `.comment`, `.ARM.attributes`, `.debug_*` 까지 더하고, dec 는 `.data` 를 한 번만 세면서 bss 를 더한다. 고침: Flash = text + data, RAM = data + bss 만 쓴다.

**2. Flash = text 로 끝낸다 (.data 초기값 누락).** init 빌드에서 text 212 만 보면 "512 B 표가 공짜" 처럼 보이지만 Flash 는 728. 링크 스크립트에 `AT > FLASH` 가 있는 섹션은 Flash 도 먹는다.

**3. 접두사 매칭을 너무 넓게.**

```c
if (strncmp(name, ".data", 5) == 0) return REG_FLASH_RAM;   /* ".database" 도 걸린다 */
if (strncmp(name, ".ARM", 4) == 0)  return REG_FLASH;       /* .ARM.attributes 까지 Flash 로 */
```

고침: 규칙 이름 전체 + 다음 글자가 `'\0'` 또는 `'.'` 인지 확인. `.init` 규칙이 `.init_array` 를 삼키지 않는 것도 같은 이유.

**4. `.o` 의 size 로 예산을 판단한다.** 맥 `.o` 에서 printf 비용은 +9 B, 링크 후에는 +6696 B. `-fcommon` 이면 초기값 없는 전역이 `.o` 의 bss 에서 아예 빠진다(576 → 64). 라이브러리, 힙/스택 예약, 링커 정렬 패딩은 **링크된 ELF** 에서만 보인다.

**5. "0 으로 초기화했으니 Flash 를 먹겠지" / "값 하나만 넣었으니 몇 바이트겠지".** `= {0}` 은 uninit 과 바이트 단위로 같다(.bss). 반대로 `= {1, 2, 3}` 은 나머지 509개가 0 이어도 512 B 전체가 .data 로 가서 Flash 와 RAM 을 다 먹는다. 큰 표는 `const` 로 Flash 에만 두거나, 런타임에 채울 거면 초기값 없이 .bss 에 둔다.

**6. `const char *names[]` 로 "상수 표" 를 만들었다고 생각한다.** 포인터 배열 자체는 쓰기 가능 → .data. `const char *const names[]`.

**7. 게이트를 unsigned 로 그냥 뺀다.**

```c
if (after.flash - before.flash > b->max_flash_growth) r |= GATE_FLASH_GROWTH;  /* 줄어들면 랩어라운드 */
```

크기를 줄인 PR 이 "4 GB 늘었다" 로 막힌다. `after.flash > before.flash &&` 를 먼저.

**8. 맥에서 엉뚱한 size 를 쓴다.** `/usr/bin/size` 는 Mach-O 전용(ELF 에 `is not an object file`), PATH 맨 앞의 `/usr/local/bin/llvm-size` 는 LLVM 3.2 — 전체 경로 `/Library/Developer/CommandLineTools/usr/bin/llvm-size` 를 쓴다.

## 8. 면접 Q&A

<details><summary>Q1. text, data, bss 가 각각 무엇이고 MCU 의 Flash/RAM 중 어디에 있나요?</summary>

text = 읽기 전용으로 적재되는 모든 것(코드, `const` 데이터, 문자열 리터럴, 벡터 표, unwind 표) → Flash. data = 0 아닌 초기값이 있는 쓰기 가능한 전역/정적 변수 → 초기값은 Flash, 실행 사본은 RAM. bss = 초기값 없음/0 인 전역·정적 → RAM 만, 파일에는 크기만. 그래서 Flash = text + data, RAM = data + bss. 이 레슨 init 빌드 실측: 212 + 516 = 728, 516 + 1604 = 2120.

꼬리: "bss 에 들어가는 게 전역 변수만인가?" → 링커 스크립트가 만든 힙/스택 예약 섹션(`._user_heap_stack` 1540 B)도 ALLOC-only 라 GNU size 는 bss 칸에 더한다. 그러니 bss 숫자는 "내 변수" 가 아니라 "RAM 정적 할당 전체" 다.
</details>

<details><summary>Q2. 왜 .data 는 Flash 와 RAM 을 둘 다 먹나요? 안 먹게 할 방법은?</summary>

RAM 은 전원이 꺼지면 내용이 사라지므로 초기값을 어딘가에 보관해야 한다. 링커는 `.data` 의 LMA 를 Flash(0x080000d4), VMA 를 RAM(0x20000000)으로 두고, 리셋 핸들러가 `_sidata` → `_sdata.._edata` 로 복사한다 (BM-B08). 피하려면 ① 바뀌지 않는 표는 `const` 로 (.rodata, Flash 만 — 실측 RAM −512), ② 런타임에 채울 버퍼는 초기값 없이 (.bss, Flash 0), ③ 큰 초기 데이터는 Flash 에 const 로 두고 필요한 부분만 RAM 으로 복사. 리눅스 같은 OS 환경에서는 로더가 파일에서 페이지를 매핑하므로 "Flash 사본" 개념이 파일 크기로 바뀔 뿐 원리는 같다.
</details>

<details><summary>Q3. Berkeley 출력과 -A 출력은 뭐가 다른가요? 왜 Berkeley text 가 .text 보다 큰가요?</summary>

Berkeley 는 섹션을 플래그로 세 칸(읽기전용 적재 / 쓰기가능 적재 / 적재 내용 없음)에 모은 요약, `-A`(SysV)는 섹션 하나하나의 크기와 주소(VMA). const 빌드에서 `.text` 132 인데 Berkeley text 708 인 건 `.isr_vector` 64 + `.rodata` 512 가 같이 들어가서다. "코드가 708 B" 라고 말하면 틀린다. 어떤 섹션이 커졌는지 찾으려면 `-A`, 예산은 Berkeley 두 공식, 심볼 단위는 `nm --size-sort` 나 맵 파일 (TOOL-M02).
</details>

<details><summary>Q4. printf 한 줄 넣었는데 왜 6~30 KB 가 늘어나나요? 어떻게 줄이나요?</summary>

printf 는 서식 해석기 + stdio(FILE, 버퍼, `__sfvwrite_r`) + malloc 을 끌고 온다. 실측: nano +6696 B Flash / +424 B RAM, nano 에 `%f`(`-u _printf_float`) 추가 시 22936, full newlib 33516 (`_vfprintf_r` 6800 B, `_dtoa_r` 4168 B). 줄이는 법: newlib-nano(`--specs=nano.specs`), `%f` 피하기(정수 고정소수점으로), 작은 printf 구현(mpaland/eyalroz printf 등 — 이름은 참고, 확인 권장), 아예 `uart_puts` + 정수 변환, 또는 문자열을 칩에 두지 않는 토큰화 로그(TOOL-B13). 그리고 `--gc-sections`/LTO 로 안 쓰는 코드 제거(TOOL-M13).
</details>

<details><summary>Q5. size 가 "들어간다" 고 했는데 현장에서 RAM 이 터졌습니다. size 가 안 보여 주는 RAM 은?</summary>

① 스택: size 에 보이는 건 링커 스크립트가 예약한 최소 크기(여기선 1024 B)일 뿐, 실제 최대 깊이(재귀, ISR 중첩, 큰 지역 배열)는 `-fstack-usage` + 콜 그래프(TOOL-A08)나 스택 페인팅(0xDEADBEEF 채우고 high-water mark)으로 잰다. ② 힙: malloc 실제 사용량, 단편화. ③ RTOS 태스크 스택/큐를 런타임에 `pvPortMalloc` 으로 만들면 bss 의 힙 풀 안에 숨는다 (RTOS-B 트랙). ④ DMA 버퍼 정렬 패딩 같은 링커 정렬 낭비는 size 에 포함되지만 어디서 생겼는지는 맵 파일을 봐야 안다.
</details>

<details><summary>Q6. 같은 코드를 맥에서 size 로 보니 __TEXT 16384 가 나왔습니다. 펌웨어 숫자와 왜 다르죠?</summary>

맥 `size` 는 Mach-O 세그먼트 크기 — 실행 파일에서는 16 KB 페이지 단위로 올림된 가상 메모리 크기라 `__text` 112 + `__unwind_info` 88 = 200 B 가 16384 로 보인다. `__PAGEZERO` 4 GB 도 예약 주소 공간일 뿐. 섹션 단위는 `size -m` 으로 보고, 펌웨어 예산은 반드시 타깃 ELF(`arm-none-eabi-size` 또는 `llvm-size`)로 잰다. 호스트 빌드 크기로 타깃 Flash 를 추정하면 ISA(arm64 vs Thumb-2), 라이브러리, 정렬이 다 달라서 의미가 없다.
</details>

<details><summary>Q7. CI 에서 펌웨어 크기 회귀를 어떻게 막나요?</summary>

매 빌드 `size` 를 파싱해 ① 칩 용량 대비 초과 → 실패, ② 기준(main 브랜치) 대비 PR 증가량이 임계치 초과 → 실패 또는 리뷰 요구, ③ 섹션별 증감과 가장 많이 늘어난 섹션/심볼을 PR 코멘트로 (연습 2, 심볼 단위는 맵 파일 diff — TOOL-M02). 주의: 줄어든 변경을 GROWTH 로 오판하지 않게 unsigned 비교 순서, 스택/힙 예약이 RAM 에 포함됐는지 명시, 디버그 섹션 제외. 링커 스크립트 `ASSERT` 나 MEMORY 영역 overflow 에러(`region 'FLASH' overflowed`)는 최후 방어선이고 CI 게이트는 그보다 일찍 "추세" 를 잡는 용도.
</details>

<details><summary>Q8. const 를 붙였는데 RAM 사용량이 안 줄었습니다. 가능한 원인은?</summary>

① 포인터 표: `const char *t[]` 는 배열이 쓰기 가능 → .data (실측 +12 B data). `const char *const t[]`. ② 지역 변수 `const uint8_t tbl[] = {...}` 를 함수 안에 `static` 없이 두면 매 호출마다 스택에 복사될 수 있다 → `static const`. ③ C++ 에서 생성자가 필요한 const 객체는 런타임 초기화라 RAM(.bss + init_array)에 간다 → `constexpr`. ④ 하버드 구조 AVR 처럼 const 데이터를 기본으로 RAM 에 복사하는 플랫폼은 `PROGMEM` 같은 별도 지정이 필요하다 (Cortex-M 은 Flash 가 같은 주소 공간이라 해당 없음 — AVR 세부는 확인 권장). ⑤ `-fno-zero-initialized-in-bss` 같은 플래그가 켜져 있으면 0 초기값까지 .data 로 간다 (맥 clang thumb 실측: zero 변형이 data 580 / bss 0).
</details>

## 9. 추가 연습

<details><summary>연습 1 — 맥 size -m 을 "Flash 비슷한 값 / RAM 비슷한 값" 으로 (macho_usage)</summary>

`size_mac_host.txt` 의 `.o` 블록과 실행 파일 블록에서 섹션 줄 두 모양(`Section (__TEXT, __text): 112` / `Segment __DATA:` 다음 `Section __data: 516`)을 다 읽는다. flash_like = `__TEXT` 의 모든 섹션 + `__DATA,__data/__const`, ram_like = `__DATA,__data + __bss + __common`. `__LD,__compact_unwind` 는 어느 쪽도 아님. 답: `.o` 628 / 580 (섹션 4개), 실행 파일 716 / 580 (`__unwind_info` 88 이 링크 때 생김).
</details>

<details><summary>연습 2 — 가장 많이 늘어난 섹션 (top_grower)</summary>

두 `-A` 출력을 이름으로 맞춰 실리는 섹션만 비교, before 에 없던 섹션은 0 에서 시작. strings → printf: `.text` 가 168 → 6684 로 +6516 이 최대, Flash 증감 +6696. 반대 방향(printf 제거)은 Flash −6696 이고 늘어난 섹션이 없으니 -1 (`._user_heap_stack` 은 1540 그대로). Flash 증감은 부호 있는 `int32_t` — 연습의 요점은 "unsigned 로 빼지 않기".
</details>

<details><summary>연습 3 — .data 의 VMA/LMA 로 이미지 끝 확인 (data_lma_check)</summary>

`objdump -h` 의 `3 .data 00000204 20000000 080000d4 ...` 줄을 16진으로 읽어, VMA 가 RAM 창, LMA 가 Flash 창 안이고 VMA ≠ LMA 인지 확인한 뒤 LMA + size 를 돌려준다. 답 0x080002d8 → −0x08000000 = 728 = Berkeley Flash. RAM 창을 0x30000000 으로 잘못 주면 0 (실패). `.data` 가 Flash 에 마지막으로 놓인다는 건 `fw.ld` 배치의 가정이다 — 일반화하려면 모든 LOAD 섹션의 LMA 끝 최댓값을 구해야 한다.
</details>

<details><summary>연습 4 (생각) — 스택 여유 계산</summary>

`fw.ld` 의 RAM 16 KB 에서 printf 빌드는 2032 B (힙 512 + 스택 1024 예약 포함)를 쓴다. 스택은 `_estack` = 0x20004000 에서 아래로 자라므로 실제로 쓸 수 있는 스택은 16384 − (96 + 396) − 512(힙) = 15380 B 정도다 (예약 1024 는 "최소 보장" 일 뿐). 반대로 RAM 4 KB 부품이면 printf_full 3704 B 에서 남는 건 392 B — 예약 1024 B 를 포함한 숫자이므로 링크는 되지만, 예약 크기가 실제 최대 스택보다 작으면 런타임에 넘친다. 실제 최대 깊이는 TOOL-A08 에서 잰다.
</details>

<details><summary>연습 5 (생각) — 같은 변경을 `.o` 만 보는 CI 가 놓치는 것</summary>

`size *.o` 만 합산하는 CI 를 가정하면: printf 추가 +9 B (실제 +6696), `-fcommon` 프로젝트의 큰 uninit 배열 0 B (실제 +512), 링커 정렬 패딩 0 B. 결론 — 게이트는 링크된 ELF 로. `.o` 별 size 는 "어느 파일이 커졌나" 를 찾는 보조 도구 (`size_vm_objs_total.txt` 의 `-t`).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/TOOL-B06_p && /tmp/TOOL-B06_p
```

```text
9/9 passed
```

## 10. 핵심 정리

- **Flash = text + data, RAM = data + bss.** dec 와 `-A` Total 은 둘 다 아니다 (Total 에는 `.comment`/`.debug_*` 까지).
- Berkeley text = 읽기전용 적재 전부(.text + .rodata + .isr_vector + .ARM.exidx), bss = 내용 없는 적재 전부(.bss + 힙/스택 예약).
- `.data` 는 LMA(Flash 초기값) + VMA(RAM 실행 사본) — 리셋 핸들러가 복사. objdump -h 에서 VMA ≠ LMA 인 유일한 섹션.
- `= {0}` 은 공짜(.bss), 0 아닌 값 하나면 배열 전체가 Flash+RAM, `const` 는 Flash 만, `const char *const` 까지 써야 포인터 표도 Flash.
- printf(nano) 한 줄 = Flash +6.7 KB / RAM +0.4 KB, `%f` 는 +15 KB 더, full newlib 은 33.5 KB (실측, arm-none-eabi-gcc 14.2.1 -Os).
- `.o` 의 size 는 라이브러리·예약·`-fcommon` 을 못 본다 → 예산은 링크된 ELF 로. 맥 `size -m` 은 Mach-O 페이지 단위라 펌웨어 숫자가 아니다.
- CI 게이트: 용량 초과 + PR 증가량, 줄어든 변경은 절대 GROWTH 아님(unsigned 뺄셈 금지), 스택 실제 깊이는 별도로(TOOL-A08).

## 참고자료

- GNU binutils `size` 매뉴얼 — Berkeley/SysV 형식, `-A`, `-B`, `-t`, `-x`: https://sourceware.org/binutils/docs/binutils/size.html
- LLVM `llvm-size` 문서: https://llvm.org/docs/CommandGuide/llvm-size.html
- GNU ld 매뉴얼 — Output Section LMA (`AT>`), MEMORY: https://sourceware.org/binutils/docs/ld/Output-Section-LMA.html
- newlib / newlib-nano (Arm GNU Toolchain 의 `nano.specs`, `-u _printf_float`): https://developer.arm.com/downloads/-/arm-gnu-toolchain-downloads
- Apple `size(1)` / `otool(1)` man 페이지 (`man size` — Mach-O `-m`)
- 관련 레슨: BM-B08 (리셋에서 main 까지, .data 복사), TOOL-B04 (오브젝트 파일 해부, common 심볼), TOOL-M01 (링커 스크립트), TOOL-M02 (맵 파일 분석), TOOL-M13 (gc-sections/LTO/nano 다이어트), TOOL-A08 (스택 사용량), TOOL-B13 (컴파일 타임 로그 제거)
