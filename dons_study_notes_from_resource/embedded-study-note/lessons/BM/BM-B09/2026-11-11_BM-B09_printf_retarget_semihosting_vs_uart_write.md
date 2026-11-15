---
id: BM-B09
time: 60분
updated: 2026-11-11
---
# BM-B09 printf 리타겟팅: semihosting vs UART _write

## 학습 목표

- newlib `printf` 가 글자를 내보내기까지의 경로 — `vfprintf` → `FILE` 버퍼 → `__sflush` → **`_write` 시스템 콜(syscall)** — 를 그림으로 그리고, 각 syscall(`_write/_sbrk/_fstat/_isatty/_close/_lseek/_read/_exit`)이 누가 왜 부르는지 말할 수 있다.
- CMSDK APB UART0 (QEMU `mps2-an385`, 0x40004000) 를 **폴링하는 `_write`** 와 최소 syscall 세트를 직접 써서, 진짜 Cortex-M3 이미지에서 `printf` 를 동작시킨다.
- 세미호스팅(semihosting)의 정체 — `BKPT 0xAB` + `r0`=작업 번호 — 를 알고, `--specs=rdimon.specs` 이미지가 **디버거 없이** 돌면 왜 멈추거나 HardFault 가 나는지 QEMU 에서 직접 보고 실제 칩 규칙으로 설명한다.
- 코드 크기를 실측해서 비교한다: nano vs full newlib, `printf` vs `iprintf`, `%f`(`-u _printf_float`), UART vs rdimon.

## 1. 문제

보드를 처음 살릴 때(bring-up) 가장 먼저 하는 일이 "`printf` 한 줄 띄우기" 이고, 펌웨어 면접에서도 **"베어메탈에서 printf 는 어떻게 화면에 도달하나요? 디버거를 뽑았더니 보드가 멈추는 이유는?"** 이 단골이다. Don 이 SSD 컨트롤러나 RF 모듈 보드에서 UART 콘솔 로그를 처음 띄울 때 실제로 고친 것들 — TX enable 빠뜨림, 힙 없는 stdio, 세미호스팅 빌드를 양산 보드에 올림 — 이 이 레슨의 Part B 고장 목록이다.

`arm-none-eabi-gcc` 에 딸린 C 라이브러리 **newlib** 은 OS 가 없다고 가정한다. `printf` 의 포맷팅·버퍼링은 newlib 이 하지만, "바이트를 실제로 어디로 보내나" 는 **`_write` 라는 빈 칸**으로 남겨 둔다. 그 칸을 채우는 일이 **리타겟팅(retargeting)** 이다. 채우는 방법은 두 가지가 대표적이다.

```svg
<svg viewBox="0 0 740 360" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">printf 한 줄이 밖으로 나가는 길 (newlib-nano, 이 레슨의 실측 값)</text>
  <rect x="200" y="34" width="340" height="34" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="212" y="56" fill="currentColor" font-family="monospace">printf("x=%d y=%s\n", 42, "ok")</text>
  <rect x="200" y="84" width="340" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="212" y="106" fill="currentColor">vfprintf → stdout 버퍼 (1 KB, malloc→_sbrk)</text>
  <rect x="200" y="134" width="340" height="34" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="212" y="156" fill="currentColor">'\n' 을 만나면 __sflush (stdout = 줄 버퍼)</text>
  <rect x="200" y="184" width="340" height="34" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="212" y="206" fill="currentColor" font-family="monospace">_write(1, buf, 10)  ← 리타겟 지점</text>
  <path d="M370 68 L370 84 M370 118 L370 134 M370 168 L370 184" stroke="currentColor"/>
  <path d="M370 218 L190 258 M370 218 L550 258" stroke="currentColor" fill="none"/>
  <rect x="20" y="258" width="340" height="90" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="30" y="278" fill="currentColor" font-weight="bold">(A) UART — 우리가 쓴 _write</text>
  <text x="30" y="296" fill="currentColor">'\n' → "\r\n" 이라 uart_putc 11번</text>
  <text x="30" y="314" fill="currentColor">STATE.TXFULL 폴링 → DATA @ 0x40004000</text>
  <text x="30" y="332" fill="currentColor">→ QEMU -serial 파일 / 보드의 TX 핀</text>
  <rect x="380" y="258" width="340" height="90" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="390" y="278" fill="currentColor" font-weight="bold">(B) 세미호스팅 — librdimon 의 _write</text>
  <text x="390" y="296" fill="currentColor">r0 = 0x05 SYS_WRITE, r1 = &amp;{핸들, buf, len}</text>
  <text x="390" y="314" fill="currentColor">BKPT 0xAB → 디버거(또는 QEMU)가 처리</text>
  <text x="390" y="332" fill="currentColor">→ 호스트 PC 의 stdout</text>
</svg>
```

| | (A) UART `_write` | (B) 세미호스팅 (rdimon) |
|---|---|---|
| 필요한 것 | UART 핀 + USB-UART 케이블 | 디버거 프로브가 **붙어 있고** 세미호스팅이 켜져 있어야 함 |
| 속도 | 115200 baud = 바이트당 86.8 µs (10비트/115200) | 호출마다 코어가 멈추고 프로브가 메모리를 읽음 — 보통 훨씬 느림 (프로브·호스트마다 다름) |
| 디버거 없이 | 그대로 동작 | **멈추거나 HardFault** (Part B 에서 실측) |
| 덤 | — | 호스트 파일 열기/쓰기, `exit` 코드를 호스트 셸로 (CI 테스트에 유용) |

### Part A — UART0 콘솔: `uart_init` / `uart_putc` / `_write`

CMSDK APB UART 레지스터 규칙(아래 5. 풀이 그림)대로 `uart_init(base, pclk, baud)` 는 `BAUDDIV = round(pclk/baud)` (16 미만·20비트 초과면 거부) 후 `CTRL = TX_EN|RX_EN`, `uart_putc` 는 `STATE.TXFULL` 을 **횟수 제한 있게** 폴링하고 `DATA` 에 쓴다. `_write(fd, buf, len)` 은 fd 1·2 만 받고 `'\n'` 을 `"\r\n"` 으로 바꿔 보내며, 반환값은 **소비한 바이트 수**다. 그러면 `printf` 가 UART 로 나온다.

### Part B — 버퍼·힙·고장 주입, 그리고 세미호스팅

`_sbrk`(힙), `_fstat`/`_isatty`(콘솔 = 문자 장치)를 채운다. 고장 주입: `TXFULL` 이 1 로 붙은 가짜 UART(`_write` → `EIO`), `_sbrk` 가 늘 실패(stdio 가 조용히 unbuffered 로 떨어짐 — 출력 순서가 바뀐다), `CTRL.TX_EN` 빠뜨림(QEMU trace 로 장치가 본 것을 확인). 세미호스팅 쪽은 `demo_semihost.c` 로: rdimon 으로 `printf` → 호스트 stdout, `exit(3)` → QEMU 종료 코드 3, `initialise_monitor_handles()` 를 빼먹으면? 그리고 **같은 이미지를 `-semihosting` 없이** 돌려 BKPT 가 무엇이 되는지 본다.

### Part C — 펌웨어처럼 쓰기 + 코드 크기

부트 배너, `[seq] level tag: msg` 로그(`vprintf`), hex dump, 개행 없이 끝난 줄이 `exit()` 의 flush 로 나가는지 확인. 그리고 `demo_size.c` 로 경로별 코드 크기 표를 실측한다. 실제 STM32 USART 폴링 드라이버와 BRR 계산은 BM-B12, 인터럽트 + 링버퍼 UART 는 BM-M04, 직접 만드는 작은 printf 는 C-M10, 이 레슨이 쓰는 startup 은 BM-B08 의 축약판이다.

## 2. Problem (English)

> **Retarget `printf` on a bare-metal Cortex-M3 (QEMU `mps2-an385`, newlib-nano).**
>
> The console is a CMSDK APB UART at `0x40004000`: `DATA` +0x00 (write = TX byte, read = RX byte), `STATE` +0x04 (bit0 `TXFULL`, bit1 `RXFULL`, read-only; bits 2/3 overrun, W1C), `CTRL` +0x08 (bit0 `TX_EN`, bit1 `RX_EN`), `BAUDDIV` +0x10 (20 bits, minimum 16, baud = PCLK / BAUDDIV). PCLK = 25 MHz.
>
> 1. `int uart_init(uint32_t base, uint32_t pclk, uint32_t baud)` — `BAUDDIV = round(pclk/baud)`; reject `baud == 0`, results `< 16` or `> 0xFFFFF` without touching any register (return -1). Otherwise write `BAUDDIV`, then `CTRL = TX_EN | RX_EN`, return 0. Example: `(25000000, 115200)` → `BAUDDIV = 217`; `(25000000, 1700000)` → -1 (14.7 rounds to 15).
> 2. `int uart_putc(uint32_t base, char c)` — wait for `TXFULL == 0` with a bounded poll count (100000); on timeout count it and return -1 without writing `DATA`.
> 3. `int _write(int fd, const char *buf, int len)` — fds 1 and 2 only (`EBADF` otherwise), translate `\n` to `\r\n`, return bytes consumed; if the UART stalls, return the partial count, or -1 with `EIO` if nothing was sent.
> 4. `void *_sbrk(ptrdiff_t incr)` over `[__heap_start, __HeapLimit)` from the linker script (`ENOMEM`, `(void *)-1` when out of range); `_fstat` reporting `S_IFCHR` for fds 0–2; `_isatty` returning 1 for fds 0–2.
>
> Constraints: no busy-wait without a bound; the same driver must work against a fake UART in RAM (a stuck `TXFULL` must not hang). Expected console output is compared byte-for-byte on the host.
>
> Follow-ups: What does `--specs=rdimon.specs` change? What happens if that image runs on a board with no debugger attached? Why does `printf("A1 ")` not appear before a later raw UART write? Why does a missing `_sbrk` not break `printf` but change its behaviour? How big is `printf` and how do you make it smaller? Why should you never call a polling `printf` from an ISR?

## 3. 시작 코드

<!-- code: starter.c -->

펌웨어라 맥에서 직접 돌리지 않는다. Lima VM `elinux` 안에서 빌드·실행하고 **UART 출력을 파일로 캡처해 `expected_solution.txt` 와 비교**한다 (Mac 홈은 VM 에서 읽기 전용이라 스크립트가 `/tmp/BM-B09_work` 로 복사해 빌드):

```bash
L=/Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note/lessons/BM/BM-B09
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh starter"    # FAIL, 종료 코드 1
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh solution"   # PASS, 종료 코드 0
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"   # 추가 연습 (세미호스팅 켬/끔 두 번)
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh semihost"   # demo_semihost.c: rdimon printf
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh nodebug"    # 같은 이미지, -semihosting 없이
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh bugs"       # 고장 주입 빌드들
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh size"       # 코드 크기 표
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh link"       # syscall 없을 때 링크 에러
```

빌드 플래그: `-mcpu=cortex-m3 -mthumb -std=c11 -O2 -g -Wall -Wextra -Werror --specs=nano.specs -nostartfiles -T mps2_an385.ld -ffunction-sections -fdata-sections -Wl,--gc-sections`. QEMU: `qemu-system-arm -M mps2-an385 -nographic -monitor none -no-reboot -serial file:X.uart -kernel X.elf` (`-no-reboot` + 펌웨어의 `AIRCR.SYSRESETREQ` 로 QEMU 가 스스로 끝난다).

starter 실제 결과 — **UART 에 0 바이트**. `_write` 가 "보낸 척" 만 하고 UART 도 켜지 않았기 때문이다. 자체 검사의 FAIL 줄조차 printf 로 나가므로 아무것도 안 보인다 (실제 보드에서 가장 흔한 "printf 가 조용한" 상태):

```text
   text	   data	    bss	    dec	    hex	filename
   9956	     92	    368	  10416	   28b0	starter.elf
qemu exit=0
--- captured starter.uart (0 bytes, CR=0 LF=0) ---

CHECK FAIL: starter.uart != expected_solution.txt (CR=0 LF=0)
```

<!-- code: run_qemu.sh -->

<!-- code: mps2_an385.ld -->

## 4. 힌트

<details>
<summary>힌트 1 — 순서: BAUDDIV 먼저, 그다음 TX_EN</summary>

QEMU 모델(`hw/char/cmsdk-apb-uart.c`)은 `CTRL` 에 `TX_EN` 을 쓸 때 BAUDDIV 가 16 미만이면 guest error 로그를 남긴다. 실제 UART 도 "클럭 분주를 정한 뒤 켠다" 가 순서다. 반올림은 `div = pclk / baud; if (pclk % baud >= (baud + 1) / 2) div++;` — `pclk + baud/2` 꼴보다 오버플로 걱정이 없다.

</details>

<details>
<summary>힌트 2 — _write 의 반환값은 "보낸 바이트 수"</summary>

newlib 의 `__sflush_r` 는 `_write` 가 `n` 보다 작은 양수를 돌려주면 나머지를 다시 부르고, 0 이하면 스트림에 에러 플래그(`ferror`)를 세운다. 그래서 "거짓 성공(`return len`)" 은 starter 처럼 조용히 출력을 버리고, UART 가 중간에 멈췄을 때는 그때까지의 개수를 돌려주는 게 정직하다.

</details>

<details>
<summary>힌트 3 — Part A 만 채우면 순서가 이상해진다</summary>

`uart_init/uart_putc/_write` 만 채우면 글자는 나오는데 `A1 R A2` 처럼 순서가 바뀌고 FAIL 이 5개 나온다 (5. 풀이에 실측 출력). 원인은 `_sbrk` 스텁: `malloc` 이 실패하면 stdio 는 에러 없이 **unbuffered** 로 떨어진다.

</details>

<details>
<summary>힌트 4 — _sbrk 는 "이전 break" 를 돌려준다</summary>

`static char *brk = __heap_start;` 로 시작해 `prev = brk; brk += incr; return prev;`. 범위 검사는 포인터 덧셈 전에 차이(`__HeapLimit - brk`)로 비교해야 범위 밖 포인터를 만들지 않는다. `incr` 이 음수일 수도 있다(newlib 의 `malloc_trim` 류).

</details>

## 5. 풀이

### 0. newlib 의 바닥 — 누가 어떤 syscall 을 부르나

| syscall | 누가 부르나 | 이 레슨의 구현 |
|---|---|---|
| `_write(fd, buf, n)` | `__sflush_r` (버퍼 비울 때), unbuffered 스트림은 글자마다 | UART0 로 (A), rdimon 은 SYS_WRITE (B) |
| `_sbrk(incr)` | `malloc` ← `__smakebuf_r` (stdout 의 1 KB 버퍼) | `[__heap_start, __HeapLimit)` |
| `_fstat`, `_isatty` | `__swhatbuf_r`/`__smakebuf_r` (버퍼 크기, 문자 장치인지) | fd 0~2 = `S_IFCHR`, tty |
| `_close`, `_lseek`, `_read` | `fclose`, `fseek`, `scanf`/`fgets` (stdio 가 링크하면 참조됨) | 실패/EOF 스텁 (`_read` 는 연습 4) |
| `_exit`, `_kill`, `_getpid` | `exit()`, `abort()` | UART 에 `[exit N]` 후 SYSRESETREQ |

syscall 을 하나도 안 주면 링크가 실패한다 (`run_qemu.sh link`, 실측):

```text
=== nano.specs, syscall 하나도 없음 → 링크 에러
      1 undefined reference to `_close'      (… `_fstat' `_isatty' `_lseek' `_read' 도 각 1줄)
      1 undefined reference to `_sbrk'
      1 undefined reference to `_write'
=== nano.specs + nosys.specs → 링크는 되지만 (경고) _write 가 늘 -1
warning: _close is not implemented and will always fail      (… _fstat _isatty _lseek _read 도)
warning: _write is not implemented and will always fail
qemu exit=0, UART0 captured 0 bytes
```

`--specs=nosys.specs` 는 "링크만 되게" 해 주는 스텁 라이브러리(libnosys)다 — `_write` 가 `ENOSYS` 로 실패하므로 printf 는 **조용히** 사라진다. 링커 경고를 무시한 채 보드에 올리면 starter 와 같은 증상이다.

### Part A — CMSDK APB UART 레지스터 규칙

```svg
<svg viewBox="0 0 740 260" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">CMSDK APB UART0 @ 0x40004000 (QEMU cmsdk-apb-uart.c v10.2.0 의 REG32/FIELD)</text>
  <text x="10" y="52" fill="currentColor" font-family="monospace">+0x00 DATA</text>
  <rect x="140" y="34" width="580" height="28" class="f-mut" stroke="currentColor"/>
  <text x="150" y="52" fill="currentColor">[7:0] 쓰기 = TX 1바이트 (TXFULL 세움) / 읽기 = RX 1바이트 (RXFULL 내림)</text>
  <text x="10" y="100" fill="currentColor" font-family="monospace">+0x04 STATE</text>
  <rect x="140" y="82" width="145" height="28" class="f-mut" stroke="currentColor"/>
  <text x="148" y="100" fill="currentColor">b3 RXOVR (W1C)</text>
  <rect x="285" y="82" width="145" height="28" class="f-mut" stroke="currentColor"/>
  <text x="293" y="100" fill="currentColor">b2 TXOVR (W1C)</text>
  <rect x="430" y="82" width="145" height="28" class="f-mut" stroke="currentColor"/>
  <text x="438" y="100" fill="currentColor">b1 RXFULL (r)</text>
  <rect x="575" y="82" width="145" height="28" class="f-warn" stroke="currentColor"/>
  <text x="583" y="100" fill="currentColor">b0 TXFULL (r)</text>
  <text x="10" y="148" fill="currentColor" font-family="monospace">+0x08 CTRL</text>
  <rect x="140" y="130" width="82" height="28" class="f-mut" stroke="currentColor"/>
  <text x="146" y="148" fill="currentColor">b6 HSTEST</text>
  <rect x="222" y="130" width="82" height="28" class="f-mut" stroke="currentColor"/>
  <text x="228" y="148" fill="currentColor">b5 RXOIE</text>
  <rect x="304" y="130" width="82" height="28" class="f-mut" stroke="currentColor"/>
  <text x="310" y="148" fill="currentColor">b4 TXOIE</text>
  <rect x="386" y="130" width="82" height="28" class="f-mut" stroke="currentColor"/>
  <text x="392" y="148" fill="currentColor">b3 RXIE</text>
  <rect x="468" y="130" width="82" height="28" class="f-mut" stroke="currentColor"/>
  <text x="474" y="148" fill="currentColor">b2 TXIE</text>
  <rect x="550" y="130" width="85" height="28" class="f-ok" stroke="currentColor"/>
  <text x="556" y="148" fill="currentColor">b1 RX_EN</text>
  <rect x="635" y="130" width="85" height="28" class="f-warn" stroke="currentColor"/>
  <text x="641" y="148" fill="currentColor">b0 TX_EN</text>
  <text x="10" y="196" fill="currentColor" font-family="monospace">+0x10 BAUDDIV</text>
  <rect x="140" y="178" width="580" height="28" class="f-acc" stroke="currentColor"/>
  <text x="150" y="196" fill="currentColor">[19:0] baud = PCLK / BAUDDIV, 최소 16 — 25 MHz / 115200 = 217.01 → 217</text>
  <text x="10" y="232" fill="currentColor">+0x0C INTSTATUS(W1C), +0xFD0~0xFFC PID/CID. 우리 _write 가 만지는 것은 DATA·STATE.TXFULL·CTRL·BAUDDIV 넷뿐.</text>
  <text x="10" y="250" fill="currentColor">실제 CMSDK 하드웨어도 같은 배치 (Arm CMSDK TRM 확인 권장). TX 버퍼는 1바이트 → TXFULL 하나로 충분.</text>
</svg>
```

`uart_init` 경계값 (테스트에 그대로 들어 있다, PCLK = 25 MHz):

| baud | pclk/baud | 반올림 | 결과 |
|---|---|---|---|
| 115200 | 217.01 | 217 | OK, 실제 115207 baud (+0.006 %) |
| 9600 | 2604.17 | 2604 | OK |
| 1600000 | 15.63 | 16 | OK (최소값) |
| 1700000 | 14.71 | 15 | **-1**, 레지스터 그대로 (`fake_uart[4] == 0xAAAA`) |
| 23 | 1086956.5 | 1086957 | **-1** (> 0xFFFFF = 1048575) |
| 0 | — | — | **-1** (0 으로 나누기 방지) |

**`_write` 가 `"ok\n"` 을 보낼 때의 레지스터 접근** (`g_console = UART0`):

| # | 접근 | 값 | 이유 |
|---|---|---|---|
| 1 | R STATE | TXFULL=0 | 비었나? |
| 2 | W DATA | 0x6F 'o' | |
| 3 | R STATE | TXFULL=0 | (QEMU 는 DATA 쓰기 즉시 백엔드로 보내 TXFULL 을 바로 내린다) |
| 4 | W DATA | 0x6B 'k' | |
| 5 | R STATE / W DATA | 0x0D '\r' | `'\n'` 앞에 CR 추가 |
| 6 | R STATE / W DATA | 0x0A '\n' | 반환 3 (CR 은 세지 않는다 — **호출자의 바이트** 수) |

QEMU 의 trace 로 장치가 실제로 본 쓰기를 찍을 수 있다 (`solution.elf` 에 `-d trace:cmsdk_apb_uart_write,trace:cmsdk_apb_uart_tx -D tr.log`, 로그 첫 5줄 실측):

```text
cmsdk_apb_uart_write CMSDK APB UART write: offset 0x10 data 0xd9 size 4
cmsdk_apb_uart_write CMSDK APB UART write: offset 0x8 data 0x3 size 4
cmsdk_apb_uart_write CMSDK APB UART write: offset 0x0 data 0x48 size 4
cmsdk_apb_uart_tx CMSDK APB UART: character 0x48 sent to backend
cmsdk_apb_uart_write CMSDK APB UART write: offset 0x0 data 0x65 size 4
```

BAUDDIV=0xD9(217) → CTRL=3 → 'H'(0x48) 'e' … 순서 그대로다. 실제 115200 baud UART 라면 TXFULL 이 바이트마다 약 86.8 µs 동안 1 이므로, 61바이트(CR 포함) 한 줄에 **약 5.3 ms 를 폴링**으로 보낸다 — 그래서 ISR 안에서 printf 는 금지이고, 실전 로그는 링버퍼 + TX 인터럽트(BM-M04)나 DMA 로 간다.

정답 실행 결과 (`run_qemu.sh solution`, 실측):

```text
   text	   data	    bss	    dec	    hex	filename
  10580	    100	    368	  11048	   2b28	solution.elf
qemu exit=0
--- captured solution.uart (511 bytes, CR=16 LF=16) ---
Hello from printf -> _write -> UART0 @ 0x40004000
dec=42 neg=-7 hex=0xDEADBEEF str=ok chr=Z pct=%
W1 via _write
[info] heap used by stdio buffers = 1032 bytes
x=42 y=ok
R A1 A2
E1 (stderr is unbuffered)
P1 P2
[info] that 26-byte stderr line took 26 _write calls
[BOOT] BM-B09 fw, SYSCLK=25000000 Hz, UART0 115200 baud (BAUDDIV=217)
[   1] I adc: ch3=1234 mV
[   2] W pwr: vbat=3.41 V low
DE AD BE EF 00 01 02 03
7F 80 FE FF
20/20 passed
bye (no newline: exit() must flush this)[exit 0x00000000]

CHECK PASS: solution.uart == expected_solution.txt
```

`CR=16 LF=16` — 스크립트가 CRLF 변환도 검사한다 (CR 을 지운 뒤 diff).

### Part B — 버퍼링: 출력 순서가 말해 주는 것

newlib 의 stdout 은 **처음부터 줄 버퍼(line-buffered)** 다. `findfp.c` 가 `HAVE_FCNTL` 이 없는 플랫폼(베어메탈)에서 stdout 을 `__SWR | __SLBF` 로 만든다 (newlib 소스 확인). 첫 출력 때 `__smakebuf_r` 가 `_fstat` 으로 버퍼 크기를 정하고 `malloc(1024)` (BUFSIZ) 을 부른다 — 실측 힙 사용량 1032 B (1024 + nano-malloc 청크 헤더·8바이트 정렬). stderr 는 unbuffered 다.

| 코드 | 화면 | 이유 |
|---|---|---|
| `printf("A1 "); raw_puts("R "); printf("A2\n");` | `R A1 A2` | `A1 ` 은 `'\n'` 까지 버퍼에 있고, raw 출력은 버퍼를 안 거친다 |
| `printf("P1 "); fprintf(stderr,"E1…\n"); printf("P2\n");` | `E1…` 다음 줄에 `P1 P2` | stderr 는 즉시, stdout 은 줄 끝에 |
| `printf("x=%d y=%s\n", 42, "ok")` | `_write` **1번** | 줄 버퍼: 한 줄 = UART 버스트 1번 |
| `fprintf(stderr, "E1 (stderr is unbuffered)\n")` (26 B) | `_write` **26번** | nano 의 unbuffered 스트림은 글자마다 `_write` (실측) |
| `printf("bye …")` 후 `return` | `bye …[exit 0x00000000]` | `exit()` 가 stdio 를 flush 한 뒤 `_exit` |

**`_sbrk` 가 실패하면?** `malloc` 이 NULL → `__smakebuf_r` 는 에러를 내지 않고 스트림을 **unbuffered(`__SNBF`)** 로 바꾼다. printf 는 여전히 "동작" 하지만 글자마다 `_write` 가 불리고 순서가 바뀐다. starter 에 Part A 만 채운 상태의 실측 출력:

```text
Hello from printf -> _write -> UART0 @ 0x40004000
dec=42 neg=-7 hex=0xDEADBEEF str=ok chr=Z pct=%
W1 via _write
FAIL starter_partA.c:214  _fstat(1, &st) == 0 && S_ISCHR(st.st_mode) && _isatty(1) == 1 && _isatty(7) == 0
[info] heap used by stdio buffers = 3758095919 bytes
x=42 y=ok
FAIL starter_partA.c:218  g_write_calls - w == 1u
A1 FAIL starter_partA.c:221  g_write_calls == w
R A2
P1 E1 (stderr is unbuffered)
P2
...
15/20 passed
```

`A1 R A2`, `P1 E1` — 순서가 바뀌었다. 힙 사용량 줄의 큰 수는 `(char *)-1 - __heap_start` 쓰레기 값이다. 같은 현상을 정답에 `-DBUG_SBRK` 로 주입한 빌드도 똑같이 보여 준다 (`run_qemu.sh bugs`).

**`_fstat` 이 `S_IFCHR` 를 안 주면?** 흔히 "isatty/fstat 이 줄 버퍼를 결정한다" 고 생각하지만, 베어메탈 newlib 에서는 아니다. `-DBUG_FSTAT` (st_mode = 0) 빌드의 실측 diff 는 **CHECK 1개뿐, 출력 순서는 그대로**다:

```text
=== solution.c -DBUG_FSTAT
3a4
> FAIL solution.c:227  _fstat(1, &st) == 0 && S_ISCHR(st.st_mode) && _isatty(1) == 1 && _isatty(7) == 0
15,16c16,17
< 20/20 passed
...
```

`__smakebuf_r` 는 "문자 장치이고 tty 면 `__SLBF` 를 **추가**" 할 뿐, 이미 붙은 `__SLBF` 를 지우지 않기 때문이다. (리눅스처럼 `HAVE_FCNTL` 이 있는 환경이면 얘기가 다르다.) 그래도 `_fstat` 은 `struct stat` 를 0 으로 채워야 한다 — `HAVE_BLKSIZE` 가 켜진 빌드는 `st_blksize` 를 버퍼 크기로 쓰므로 쓰레기 값이 들어가면 버퍼 크기가 쓰레기가 된다 (이 툴체인은 `st_blksize = 64` 를 줘도 1032 B 그대로였다 — 실측).

**고장 주입 — TXFULL 이 1 로 붙은 UART.** `g_console` 을 RAM 의 `fake_uart` 로 바꾸고 `STATE = TXFULL` 로 두면 `_write(1, "ab", 2)` 는 100000 번 폴링 후 `-1`, `errno == EIO`, `g_uart_timeouts` +1. 횟수 제한이 없었다면 여기서 영원히 멈춘다. 다시 `STATE = 0` 이면 `_write(1, "ok\n", 3) == 3` 이고 가짜 `DATA` 에 마지막으로 `'\n'` 이 남는다.

**고장 주입 — `CTRL.TX_EN` 빠뜨림** (`-DBUG_NO_TXEN`, QEMU trace 실측):

```text
UART0 captured 0 bytes; trace:
cmsdk_apb_uart_write CMSDK APB UART write: offset 0x10 data 0xd9 size 4
cmsdk_apb_uart_write CMSDK APB UART write: offset 0x0 data 0x48 size 4
```

`'H'` 하나가 DATA 에 들어가 TXFULL 을 세우지만, QEMU 의 `uart_transmit()` 은 `TX_EN` 이 0 이면 보내지 않고 돌아간다 → **TXFULL 이 영원히 1** → 이후의 모든 `uart_putc` 는 타임아웃(그래서 DATA 쓰기가 더 없다). 그 뒤 `CTRL` 을 써도 QEMU 는 대기 중인 바이트를 다시 보내지 않는다(`uart_write` 의 `A_CTRL` 분기는 재전송을 안 부름 — 소스 확인). 화면에는 0 바이트. 보드에서 "printf 는 리턴하는데 아무것도 안 나와요" 의 단골 원인이다.

### Part B — 세미호스팅: BKPT 0xAB 와 rdimon

세미호스팅(semihosting)은 "타깃이 호스트 PC 의 C 라이브러리를 빌려 쓰는" Arm 규약이다. Cortex-M(ARMv6-M/ARMv7-M)에서는 `r0` = 작업 번호, `r1` = 인자 블록 주소를 넣고 **`BKPT 0xAB`** 를 실행한다. 디버거가 코어를 멈춘 김에 `r0/r1` 과 메모리를 읽어 일을 해 주고 `r0` 에 결과를 넣어 재개한다. (A/R 프로파일은 SVC/HLT 를 쓴다 — Arm Semihosting 명세 확인 권장.)

| op | 이름 | 인자 블록 | 이 레슨에서 |
|---|---|---|---|
| 0x01 | SYS_OPEN | {이름, 모드, 길이} | `":tt"` 열기 = 콘솔 핸들 (rdimon) |
| 0x03 | SYS_WRITEC | 글자 1개 주소 | — |
| 0x04 | SYS_WRITE0 | NUL 로 끝나는 문자열 | 연습 1 (직접 구현) |
| 0x05 | SYS_WRITE | {핸들, buf, len} → 못 쓴 바이트 수 | rdimon 의 `_write` |
| 0x18 | SYS_EXIT | (32비트) 이유 코드 | `ADP_Stopped_ApplicationExit` → 상태 코드 없음 |
| 0x20 | SYS_EXIT_EXTENDED | {이유, 상태 코드} | 확장이 있으면 `exit(n)` 의 n 이 호스트로 |

<!-- code: demo_semihost.c -->

`--specs=rdimon.specs` 는 `libc_nano` + `librdimon_nano` (세미호스팅으로 구현된 syscall 들)을 링크하고, 원래는 startfile 로 `rdimon-crt0.o` 를 넣는다. 우리는 `-nostartfiles` 로 자체 Reset_Handler 를 쓰므로 crt0 가 하던 **`initialise_monitor_handles()` 를 직접 불러야** 한다. 실행 결과 (`run_qemu.sh semihost`, 실측):

```text
   text	   data	    bss	    dec	    hex	filename
  10200	    104	    504	  10808	   2a38	semi.elf
qemu exit=3
--- host stdout ---
Hello from printf -> rdimon _write -> semihosting SYS_WRITE
dec=42 hex=0xDEADBEEF str=ok (printf returned 60)
DHCSR=0x00000000 (C_DEBUGEN=0)
--- host stderr ---
this line went to stderr
--- UART0 (0 bytes) ---
```

- 출력이 **QEMU 프로세스의 stdout/stderr** 로 나왔다 (UART 는 0 바이트). rdimon 이 `":tt"` 를 모드 "w" 로 열어 stdout, "a" 로 열어 stderr 핸들을 받는다 (libgloss `syscalls.c`).
- `main` 이 3 을 반환 → `exit(3)` → SYS_EXIT_EXTENDED → **`qemu exit=3`**. 펌웨어 단위 테스트를 QEMU 로 CI 에 돌릴 때 이것이 "통과/실패" 신호가 된다.
- `DHCSR=0` — QEMU 는 C_DEBUGEN 을 모델링하지 않는다. QEMU 의 세미호스팅은 디버거가 아니라 **QEMU 자신**이 BKPT 를 가로채 처리하는 것이다.

**`initialise_monitor_handles()` 를 빼먹으면** (`-DBUG_NO_INIT_HANDLES`, 실측): `qemu exit=0`, host stdout 0 bytes, stderr 0 bytes. 출력이 없는 것보다 무서운 건 **종료 코드 0** 이다 — main 은 1 을 반환했는데도. 핸들 표가 0 으로 남아(-1 이 아님) `_open(":semihosting-features")` 가 빈 슬롯을 못 찾고 실패 → EXIT_EXTENDED 지원을 모른다고 판단 → 상태 코드를 못 싣는 SYS_EXIT(ApplicationExit) 로 끝난다 (libgloss `_kill.c`/`syscalls.c` 소스로 확인). CI 가 "초록불" 인데 테스트는 하나도 안 돈 상태다.

### Part B — 디버거 없이 세미호스팅 이미지를 돌리면

```svg
<svg viewBox="0 0 740 310" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <rect x="250" y="12" width="240" height="32" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="262" y="33" fill="currentColor" font-family="monospace">BKPT 0xAB  (r0 = op, r1 = arg)</text>
  <rect x="200" y="66" width="340" height="32" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="212" y="87" fill="currentColor">디버거가 halting debug 를 켰나? (DHCSR.C_DEBUGEN)</text>
  <path d="M370 44 L370 66 M290 98 L185 140 M450 98 L555 140" stroke="currentColor" fill="none"/>
  <text x="200" y="124" fill="currentColor">예</text>
  <text x="520" y="124" fill="currentColor">아니오</text>
  <rect x="20" y="140" width="330" height="54" rx="6" class="f-ok" stroke="currentColor"/>
  <text x="30" y="160" fill="currentColor">코어가 Debug state 로 halt</text>
  <text x="30" y="180" fill="currentColor">세미호스팅 켠 디버거 → 처리하고 재개</text>
  <rect x="20" y="210" width="330" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="30" y="230" fill="currentColor">세미호스팅 꺼진 디버거(OpenOCD 기본값 등)</text>
  <text x="30" y="250" fill="currentColor">→ 멈춘 채 그대로: "펌웨어가 멈췄다"</text>
  <rect x="390" y="140" width="330" height="54" rx="6" class="f-mut" stroke="currentColor"/>
  <text x="400" y="160" fill="currentColor">DebugMonitor 켜짐 (DEMCR.MON_EN)?</text>
  <text x="400" y="180" fill="currentColor">→ DebugMon 예외 (모니터 디버거용)</text>
  <rect x="390" y="210" width="330" height="54" rx="6" class="f-warn" stroke="currentColor"/>
  <text x="400" y="230" fill="currentColor">아니면 HardFault 로 승격 (실제 칩: HFSR.DEBUGEVT)</text>
  <text x="400" y="250" fill="currentColor">QEMU 실측: HFSR = 0x40000000 (FORCED)</text>
  <path d="M185 194 L185 210 M555 194 L555 210" stroke="currentColor"/>
  <text x="20" y="288" fill="currentColor">QEMU 는 -semihosting-config enable=on 이면 BKPT 를 예외 전에 직접 처리한다 (디버거 역할).</text>
  <text x="20" y="304" fill="currentColor">옵션이 없으면 BKPT → DebugMonitor 펜딩 → 꺼져 있으니 HardFault (armv7m_nvic.c 의 escalate 경로).</text>
</svg>
```

같은 `semi.elf` 를 `-semihosting-config` 없이 실행 (`run_qemu.sh nodebug`, 실측). `demo_semihost.c` 의 HardFault 핸들러가 UART0 를 직접 켜서 덤프한다:

```text
qemu exit=0
--- host stdout+stderr (0 bytes) ---
--- UART0 (HardFault handler) ---

!! HardFault  PC=0x000022E6 insn=0x0000BEAB
   HFSR=0x40000000 CFSR=0x00000000 DFSR=0x00000000 DHCSR=0x00000000
   r0(op)=0x00000001 r1(arg)=0x203FFFCC
   -> BKPT 0xAB = semihosting call, but no debugger/host answered it
--- addr2line 0x000022E6 ---
do_AngelSWI
libgloss/arm/swi.h:118
initialise_monitor_handles
libgloss/arm/syscalls.c:161
--- objdump around PC ---
    22e0:	9402      	str	r4, [sp, #8]
    22e2:	4628      	mov	r0, r5
    22e4:	4639      	mov	r1, r7
    22e6:	beab      	bkpt	0x00ab
    22e8:	4605      	mov	r5, r0
```

읽는 법: 스택에 저장된 PC 의 명령이 `0xBEAB` (= `bkpt 0xab` 의 Thumb 인코딩), `r0 = 1` = SYS_OPEN — `main` 에 가기도 전에 `initialise_monitor_handles` 가 `":tt"` 를 여는 **첫 세미호스팅 호출**에서 죽었다. `r1 = 0x203FFFCC` 는 스택 위의 인자 블록. `HFSR = 0x40000000` 은 FORCED(bit30) — "다른 예외가 HardFault 로 승격됨". 실제 칩이라면 ARMv7-M ARM 상 BKPT 승격은 **HFSR.DEBUGEVT(bit31)** 와 **DFSR.BKPT(bit1)** 로 보고된다 (확인 권장). QEMU 는 이 부분을 일반 승격 경로로 모델링해 FORCED 를 세우고 DFSR 은 0 으로 둔다 — QEMU 와 실리콘이 다른 곳이니 디버깅 때 HFSR 비트를 그대로 믿지 말 것.

**실제 보드 규칙** (면접 답):
1. 디버거 미연결(C_DEBUGEN=0), DebugMonitor 꺼짐 → BKPT 는 **HardFault**. 기본 `HardFault_Handler` 가 `while(1)` 이면 "보드가 멈췄다".
2. 디버거 연결 + 세미호스팅 지원 꺼짐 → 코어는 **halt 된 채** 남는다. IDE 는 "breakpoint 에서 멈춤" 으로 보여 준다.
3. 그래서 양산 펌웨어는 세미호스팅을 **빌드에서 빼거나**(UART/ITM/RTT 로), 연습 2 처럼 BKPT 를 건너뛰는 HardFault 를 둔다. C_DEBUGEN 을 먼저 읽는 방법도 있지만 "디버거는 있는데 세미호스팅이 꺼진" 경우(2번)는 못 막는다.

### Part C — 펌웨어처럼 쓰기, 그리고 크기

`log_msg('I', "adc", "ch%d=%d mV", 3, 1234)` 는 `printf` 로 머리, `vprintf` 로 본문, `putchar('\n')` 로 끝 — 세 번 나눠 불러도 **줄 버퍼 덕에 `_write` 는 1번**(테스트로 확인). 로그 한 줄이 UART 에서 쪼개지지 않으니 다른 출력과 섞여도 줄 단위로 읽힌다. 마지막 `printf("bye (no newline…)")` 는 `'\n'` 이 없어 버퍼에 남지만 `main` 의 `return` → `exit()` → stdio 정리(flush) → `_exit` 순서라 `[exit …]` 앞에 나온다. 반대로 `_exit()` 를 직접 부르거나 리셋하면 그 줄은 사라진다.

코드 크기 (`run_qemu.sh size`, 같은 `x=%d y=%d s=%s` 한 줄, `-O2 -ffunction-sections -fdata-sections -Wl,--gc-sections`, 벡터·startup 포함, 실측):

<!-- code: demo_size.c -->

```text
variant                                         text   data    bss
raw uart_puts (no printf), nano                  936      4    316
nano   printf  + UART _write                    6788    104    328
nano   iprintf + UART _write                    6788    104    328
nano   printf %f, no -u _printf_float           7736    108    328
nano   printf %f + -u _printf_float            20224    472    328
full   printf  + UART _write                   29956   1736    776
full   iprintf + UART _write                   11524   1372    776
full   printf %f                               30020   1736    776
nano   printf  + rdimon (semihosting)           9264    112    504
full   printf  + rdimon (semihosting)          32316   1740    952
--- what %f prints (nano without / with -u _printf_float, full) ---
[--specs=nano.specs]                   -> f= g=
[--specs=nano.specs -u _printf_float]  -> f=3.250000 g=3.25
[full newlib]                          -> f=3.250000 g=3.25
```

| 관찰 | 해석 |
|---|---|
| printf 하나 = 약 5.9 KB (936 → 6788) | 포맷터 + stdio + malloc. 16 KB 플래시 칩이면 1/3 |
| nano 에서 printf == iprintf | nano 의 printf 는 원래 정수 전용 (float 은 약한 심볼로 빠져 있음) |
| full newlib 은 4.4배 (29956) | 그 대신 full 에서는 `iprintf` 가 11524 로 크게 준다 |
| nano `%f` 는 **빈칸** 출력 | 경고도 에러도 없다. `-u _printf_float` 로 float 포맷터를 강제로 링크 (+12.5 KB) |
| `%f` 만 썼는데 +948 B (6788 → 7736) | 가변 인자는 float 을 double 로 넘기므로 `__aeabi_f2d` 가 필요하고, libgcc 의 같은 오브젝트에 든 `__aeabi_dadd/dsub/i2d/ul2d…` 가 딸려 온다 (nm 비교 실측). Cortex-M3 는 FPU 가 없다 |
| rdimon nano 9264 vs UART nano 6788 | 세미호스팅 syscall 라이브러리(파일 핸들 표, 확장 기능 탐지)가 +2.5 KB, bss +176 |

RAM 쪽: stdout 버퍼 1032 B 는 **힙**이라 위 bss 에 안 보인다. 작은 MCU 에서는 연습 3 처럼 `setvbuf` 로 정적 버퍼를 주는 편이 "printf 한 줄에 RAM 1 KB" 를 막는다.

### startup 에서 newlib 이 요구하는 것 (BM-B08 축약판과의 차이)

- `-nostartfiles` 이면 `crti.o/crtbegin.o/crt0.o` 가 빠진다. `__libc_init_array` 는 `_init()` 을, full newlib 의 `exit` 은 `__libc_fini_array` → `_fini()` 를 부르므로 빈 `_init/_fini` 를 둔다 (`_fini` 를 빼면 full newlib 링크가 `undefined reference to '_fini'` 로 실패 — size 표를 만들다 실제로 겪음).
- 링커 스크립트에 `.init_array`, `.ARM.exidx`, 힙 시작 `end`(libgloss `_sbrk` 용), `__heap_start/__HeapLimit`(우리 `_sbrk` 용).
- `_exit` 은 "끝" 이 없는 베어메탈에서 정책이다: 여기선 `[exit N]` 을 UART 로 찍고 `AIRCR = 0x05FA0004`(SYSRESETREQ). QEMU 를 `-no-reboot` 로 띄우면 리셋 요청에서 QEMU 가 끝난다 — 실제 보드에서는 리셋.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

`BUG_NO_TXEN / BUG_SBRK / BUG_FSTAT` 는 `run_qemu.sh bugs` 가 쓰는 고장 주입 스위치다 (기본 빌드에는 영향 없음).

## 7. 자주 하는 실수

1. **`_write` 가 `len` 만 돌려주고 아무것도 안 함 / `nosys.specs` 경고 무시** → printf 가 조용하다. starter 와 `run_qemu.sh link` 의 nosys 실행이 정확히 이 상태 (UART 0 바이트). 고친 코드: 실제로 `uart_putc` 하고 보낸 수를 반환.
2. **`CTRL.TX_EN` 을 안 켬** → 첫 바이트가 TX 버퍼에 갇혀 TXFULL 이 영원히 1. 폴링에 횟수 제한이 없으면 첫 printf 에서 멈춘다. → `uart_init` 에서 BAUDDIV 다음 `CTRL = TX_EN | RX_EN`, `uart_putc` 는 bounded.
3. **`_sbrk` 를 대충 (`return (void*)-1`) 혹은 아예 스택과 겹치게** → 전자는 stdio 가 조용히 unbuffered(글자마다 `_write`, 순서 바뀜), 후자는 힙이 스택을 덮어 나중에 HardFault. → 링커 스크립트의 한계(`__HeapLimit`)까지만 늘리고 넘으면 `ENOMEM`.
4. **`printf("%f")` 가 빈칸** → nano 의 기본 printf 는 float 을 안 찍는다. `-u _printf_float` (+12.5 KB) 를 주거나, 정수로 고정소수점 출력(`%u.%02u`, Part C 의 vbat 줄처럼).
5. **세미호스팅 빌드(rdimon)를 디버거 없는 보드에 올림** → 첫 세미호스팅 호출(`initialise_monitor_handles` 의 SYS_OPEN)에서 HardFault/halt, `main` 도 못 간다. → 릴리스 빌드는 `nano.specs` + UART `_write`, 세미호스팅은 테스트 빌드에서만.
6. **`-nostartfiles` + rdimon 인데 `initialise_monitor_handles()` 를 안 부름** → 출력 없음 + `exit` 코드가 항상 0 (CI 가 거짓 초록불). → Reset_Handler 에서 `__libc_init_array` 전에 호출.
7. **ISR 안에서 printf** → 115200 baud 폴링이면 한 줄에 ms 단위로 막히고, newlib 의 stdio 는 재진입 보호(락)가 없어 main 의 printf 와 버퍼가 섞인다. → ISR 은 플래그/링버퍼에만 쓰고 출력은 main 에서 (BM-M04).

## 8. 면접 Q&A

<details>
<summary>Q1. 베어메탈에서 printf 는 어떻게 UART 에 도달하나요?</summary>

`printf` → `vfprintf` 가 stdout `FILE` 의 버퍼에 포맷한다. 버퍼는 첫 사용 때 `malloc` (→ `_sbrk`) 로 1 KB 를 받는다. stdout 은 줄 버퍼라 `'\n'` 에서 `__sflush` 가 `_write(1, buf, n)` 을 부른다. newlib 은 OS 가 없으니 `_write` 를 비워 두고, 펌웨어가 그것을 UART 드라이버로 구현한다 — 그게 리타겟팅이다. 이 레슨에선 `_write` → `uart_putc` → `STATE.TXFULL` 폴링 → `DATA` 쓰기. 링크하려면 `_sbrk/_fstat/_isatty/_close/_lseek/_read` 도 있어야 한다.

</details>

<details>
<summary>Q2. 세미호스팅은 무엇이고, 디버거를 뽑으면 왜 보드가 멈추나요?</summary>

`r0` 에 작업 번호, `r1` 에 인자 블록을 넣고 `BKPT 0xAB` 를 실행하면 디버거가 코어를 멈춘 김에 그 요청을 호스트에서 처리해 주는 규약이다. 디버거가 없으면(C_DEBUGEN=0) BKPT 는 DebugMonitor 예외가 되어야 하는데 보통 꺼져 있으니 HardFault 로 승격된다(실제 칩: HFSR.DEBUGEVT). 기본 HardFault 핸들러는 무한 루프라 "멈춤". 디버거는 붙어 있는데 세미호스팅이 꺼져 있으면 코어가 halt 상태로 남는다. QEMU 에서 `-semihosting` 없이 돌리니 `initialise_monitor_handles` 의 SYS_OPEN(r0=1) 에서 HardFault, HFSR=0x40000000 이었다.

</details>

<details>
<summary>Q3. 꼬리질문: 그럼 세미호스팅 대신 무엇을 쓰나요?</summary>

(1) UART — 핀 2개, 디버거 불필요, 대신 느리고 핀을 쓴다. (2) ITM/SWO — Cortex-M3/M4 이상의 ITM 스티뮬러스 포트에 쓰면 SWO 핀으로 비동기 트레이스 출력, 코어를 멈추지 않는다 (M0 에는 ITM 이 없음). (3) SEGGER RTT 류 — RAM 링버퍼를 프로브가 백그라운드로 읽는다, 매우 빠르고 핀이 필요 없다, 프로브가 없으면 버퍼만 차고 넘친다. 세미호스팅은 "호스트 파일 I/O + 종료 코드" 가 필요한 테스트 하네스(QEMU CI)에서 가장 빛난다.

</details>

<details>
<summary>Q4. printf("A1 ") 다음에 직접 UART 에 쓴 글자가 먼저 나왔습니다. 왜죠?</summary>

stdout 은 줄 버퍼라 `'\n'` 이 오기 전까지 `A1 ` 은 RAM 버퍼에 있다. raw UART 쓰기는 버퍼를 안 거친다. 베어메탈 newlib 은 `HAVE_FCNTL` 이 없어 stdout 을 처음부터 `__SLBF` 로 만든다 — `_isatty`/`_fstat` 이 결정하는 게 아니다(실측: fstat 을 망가뜨려도 순서 동일). 반대로 `_sbrk` 가 실패하면 stdio 는 unbuffered 로 떨어져 순서가 `A1 R A2` 가 된다. 섞이면 곤란한 출력(크래시 덤프 등) 앞에서는 `fflush(stdout)`.

</details>

<details>
<summary>Q5. printf 가 너무 큽니다. 어떻게 줄이나요?</summary>

실측: 같은 한 줄이 raw 936 B, nano printf 6788 B, full newlib printf 29956 B, nano + `%f` 20224 B. 순서대로: `--specs=nano.specs`; float 이 필요 없으면 `-u _printf_float` 를 넣지 말고 고정소수점으로; `-ffunction-sections -fdata-sections -Wl,--gc-sections`; 그래도 크면 printf 를 버리고 작은 포맷터(C-M10 의 mini printf, 정수/hex/문자열만)를 쓴다. 로그는 문자열 대신 ID+인자만 보내고 호스트에서 복원하는 방식(binary logging)도 있다.

</details>

<details>
<summary>Q6. 폴링 UART printf 를 ISR 에서 부르면 안 되는 이유는?</summary>

시간: 115200 baud 에서 바이트당 86.8 µs, 60자 한 줄이면 5 ms 넘게 ISR 이 붙잡혀 다른 인터럽트·타이밍이 망가진다. 재진입: 베어메탈 newlib 의 stdio·malloc 락은 기본 구현이 비어 있어(retargetable locking 의 기본 스텁 — 툴체인 빌드 옵션 확인 권장) main 의 printf 도중 ISR 의 printf 가 같은 버퍼와 힙을 건드린다. 해법은 ISR 에서 링버퍼에 넣고 TX 인터럽트/DMA 가 비우는 구조 (BM-M04).

</details>

<details>
<summary>Q7. _write 가 len 보다 작은 값을 돌려주면 newlib 은 어떻게 하나요? 왜 CR 은 세지 않나요?</summary>

`__sflush_r` 는 남은 바이트로 `_write` 를 다시 부르고, 0 이하이면 스트림에 에러 플래그를 세운다 (`ferror(stdout)`, printf 반환 -1). 반환값은 "호출자 버퍼에서 소비한 바이트 수" 라는 POSIX `write` 의미라, 우리가 끼워 넣은 `'\r'` 을 세면 newlib 은 버퍼의 다음 바이트를 건너뛰거나 범위를 넘는다. 실제 프린트 수와 반환값을 구별하는 것이 핵심.

</details>

<details>
<summary>Q8. QEMU 펌웨어 테스트를 CI 에 넣으려면 종료 코드를 어떻게 받나요?</summary>

세미호스팅 SYS_EXIT_EXTENDED: rdimon 이면 `exit(n)` 이 그대로 QEMU 종료 코드가 된다(실측 `exit(3)` → `qemu exit=3`). 32비트 SYS_EXIT 는 이유 코드만 실어 상태를 잃으므로 `initialise_monitor_handles` 를 빼먹으면 늘 0 — 거짓 초록불. UART 만 쓰는 이미지는 이 레슨처럼 출력 마지막에 `[exit N]` 마커를 찍고 호스트가 파싱하거나, 기대 출력과 diff 한다.

</details>

## 9. 추가 연습

### 연습 1 — rdimon 없이 직접 세미호스팅 stderr

stdout 은 UART, stderr(fd 2) 는 **직접 쓴 SYS_WRITE0** 로 호스트에 보내라. SYS_WRITE0 는 NUL 로 끝나는 문자열이라 (a) 버퍼에서 NUL 을 빼고 (b) 32바이트 청크 + NUL 로 나눠 보낸다.

<details>
<summary>답</summary>

`semihost_call(0x04, chunk)` — `register int r0 __asm__("r0") = op; register const void *r1 __asm__("r1") = arg; __asm__ volatile("bkpt 0xab" : "+r"(r0) : "r"(r1) : "memory");`. `"memory"` clobber 가 없으면 컴파일러가 청크 내용을 BKPT 뒤로 미룰 수 있다. QEMU 는 SYS_WRITE0 를 자신의 stderr 로 쓴다 (`semihosting/console.c`: chardev 미지정 → `stderr`). 실측 호스트 stderr: `E: sensor timeout on bus 2 (this line is longer than one 32-byte chunk)` / `nulskipped`. nano 의 stderr 는 unbuffered 라 이 72자 한 줄도 `_write` 72번 = BKPT 72번이다 — 실제 프로브에선 느리므로 `setvbuf(stderr, buf, _IOLBF, n)` 로 줄 단위로 묶는 게 낫다.

</details>

### 연습 2 — 세미호스팅-안전 HardFault

디버거가 없어도 죽지 않게: HardFault 에서 스택 프레임의 PC 가 가리키는 명령이 `0xBEAB` 면 `r0 = -1`, `PC += 2`, `HFSR` W1C 로 지우고 **복귀**하라. 이후 stderr 는 `[stderr] ` 를 붙여 UART 로.

<details>
<summary>답</summary>

`frame[0] = (uint32_t)-1; frame[6] = pc + 2u; SCB_HFSR = SCB_HFSR; return;` — 핸들러가 `b hardfault_c` 로 꼬리 분기했으므로 LR 은 EXC_RETURN 이고, C 함수가 `bx lr`/`pop {pc}` 로 돌아가면 예외 복귀가 일어나 스레드 코드가 BKPT 다음 명령부터 계속한다. `BKPT` 는 16비트 명령이라 +2. 실측(세미호스팅 끔): `[info] semihosting absent -> stderr fell back to UART, BKPT skipped 1 time(s)` 그리고 `[stderr] E: sensor timeout …`. 주의: 이 트릭은 "BKPT 가 HardFault 로 승격되는" 경우(디버거 미연결)만 구한다 — 디버거가 붙어 있는데 세미호스팅이 꺼져 있으면 코어는 halt 되어 핸들러까지 오지 않는다.

</details>

### 연습 3 — 힙 0 바이트 printf

첫 입출력 **전에** `setvbuf(stdout, g_stdout_buf, _IOLBF, 128)` (stdin 도 64 B 정적 버퍼)로 바꾸고, 모든 출력 후 `_sbrk(0) == __heap_start` (malloc 이 한 번도 안 불림)를 확인하라.

<details>
<summary>답</summary>

`setvbuf` 는 스트림을 처음 쓰기 전에만 유효하다 — 이미 1 KB 를 malloc 한 뒤엔 늦다. 정적 버퍼면 RAM 비용이 bss 에 보이고(링크 때 계산됨), 힙이 아예 필요 없으니 `_sbrk` 를 금지(늘 실패)로 두는 정책도 가능해진다. 실측: `[info] heap used by stdio = 0 bytes`. 줄 버퍼 크기보다 긴 줄은 버퍼가 찰 때마다 `_write` 가 나뉘어 불린다.

</details>

### 연습 4 — `_read`: UART RX 로 명령 받기

`_read(0, buf, len)`: `STATE.RXFULL` 폴링(제한 있음) → `DATA` 읽기(RXFULL 내려감) → 에코, `'\r'` 은 `'\n'` 으로, `'\n'` 에서 반환, 줄 처음의 Ctrl-D(0x04) 는 0 반환(EOF). `fgets` 로 `"led on"` 과 `"0x2A"` 를 읽고 세 번째 `fgets` 는 NULL + `feof` 여야 한다. 입력은 QEMU `-chardev file,id=u0,path=…,input-path=practice_input.txt -serial chardev:u0`.

<details>
<summary>답</summary>

newlib 의 `__srefill` 은 `_read(fd, buf, bufsize)` 를 부르고 0 이면 EOF 플래그를 세운다. "한 글자라도 받았으면 줄 끝에서 돌려준다" 가 콘솔의 정석 — `len` 을 다 채울 때까지 기다리면 사람이 Enter 를 쳐도 반응이 없다. RX 대기 한도는 넉넉히(20M 폴링): QEMU 는 파일 입력을 별도 I/O 스레드가 넣어 주므로 100000 폴링(수 ms)으로는 첫 글자가 도착하기 전에 EOF 로 끝나 버렸다(실측). 실제 보드라면 시간 기반 타임아웃(SysTick, BM-B13)이 맞다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
limactl shell elinux -- bash -lc "bash $L/run_qemu.sh practice"
```

실측 결과 요약 (세미호스팅 켬: UART 에 `[info] semihosting answered by host, BKPT skipped 0 time(s)` + 호스트 stderr 에 두 줄 / 끔: 아래):

```text
   text	   data	    bss	    dec	    hex	filename
  11104	    104	    540	  11748	   2de4	practice.elf
CHECK PASS: p_semi.uart == expected_practice.txt
CHECK PASS: semihosting stderr
=== (2) semihosting OFF (no debugger): BKPT 0xAB -> HardFault -> skipped, stderr -> UART
qemu exit=0
--- captured p_nodebug.uart (382 bytes, CR=10 LF=10) ---
practice: stdout -> UART0, stderr -> semihosting (or UART fallback)
line-buffered in a static 128-byte buffer
[stderr] E: sensor timeout on bus 2 (this line is longer than one 32-byte chunk)
[stderr] nulskipped
[info] semihosting absent -> stderr fell back to UART, BKPT skipped 1 time(s)
> led on
> 0x2A
[info] heap used by stdio = 0 bytes
11/11 passed
[exit 0x00000000]
CHECK PASS: p_nodebug.uart == expected_practice_nodebug.txt
practice: ALL PASS
```

## 10. 핵심 정리

- newlib 은 포맷·버퍼까지, **바이트의 목적지는 `_write`** — 그것을 채우는 게 리타겟팅. 링크엔 `_sbrk/_fstat/_isatty/_close/_lseek/_read/_exit` 도 필요 (`nosys.specs` = 링크만 되는 조용한 실패).
- UART `_write`: BAUDDIV(≥16) → `CTRL.TX_EN` → `TXFULL` **제한 있는** 폴링 → `DATA`. `\n`→`\r\n`, 반환 = 소비한 바이트 수.
- 베어메탈 newlib 의 stdout 은 처음부터 줄 버퍼(1 KB 를 malloc). `_sbrk` 가 실패하면 조용히 unbuffered → 글자마다 `_write`, 출력 순서가 바뀐다. stderr 는 unbuffered(nano: 글자마다).
- 세미호스팅 = `BKPT 0xAB` + `r0` op. rdimon + `-nostartfiles` 면 `initialise_monitor_handles()` 직접 호출, 안 하면 출력 0 + 종료 코드 0.
- 디버거 없음 → BKPT 는 HardFault (실칩 HFSR.DEBUGEVT, QEMU 실측 FORCED), 디버거 있는데 세미호스팅 꺼짐 → halt. 양산 빌드에 세미호스팅 금지.
- 크기 실측: raw 0.9 KB, nano printf 6.8 KB, full 30 KB, nano+float 20 KB, rdimon nano 9.3 KB. nano 의 `%f` 는 `-u _printf_float` 없이는 빈칸.
- 폴링 printf 는 115200 baud 에서 바이트당 86.8 µs — ISR 에서 금지, 실전은 링버퍼 + 인터럽트/DMA (BM-M04).

## 참고자료

- QEMU 소스 v10.2.0: `hw/char/cmsdk-apb-uart.c` (레지스터·TX_EN·BAUDDIV ≥ 16), `hw/arm/mps2.c` (UART0 0x40004000 = 첫 `-serial`, SYSCLK 25 MHz, 메모리 맵), `hw/intc/armv7m_nvic.c` (BKPT → DebugMonitor → HardFault 승격, FORCED), `target/arm/tcg/m_helper.c` (`EXCP_BKPT`), `semihosting/console.c` (SYS_WRITE0 → stderr) — https://gitlab.com/qemu-project/qemu
- QEMU 문서: Semihosting (`-semihosting-config`), `-chardev file,input-path=` — https://www.qemu.org/docs/master/about/emulation.html#semihosting
- newlib 4.5/4.6 소스: `newlib/libc/stdio/findfp.c` (stdout `__SLBF`), `makebuf.c` (`__smakebuf_r`/`__swhatbuf_r`), `libgloss/arm/syscalls.c`·`_kill.c` (rdimon, `initialise_monitor_handles`, EXIT_EXTENDED) — https://sourceware.org/git/?p=newlib-cygwin.git
- Arm, *Semihosting for AArch32 and AArch64* (SYS_* 번호, BKPT 0xAB on M-profile) — https://github.com/ARM-software/abi-aa/blob/main/semihosting/semihosting.rst
- Arm, *ARMv7-M Architecture Reference Manual* (DDI 0403): BKPT, Debug event 의 HardFault 승격, HFSR/DFSR/DHCSR/DEMCR (C1, B3.2) — 확인 권장
- Arm, *Cortex-M System Design Kit TRM* (DDI 0479): APB UART (BAUDDIV 최소 16) — 확인 권장
- Arm GNU Toolchain 문서: `nano.specs`, `rdimon.specs`, `nosys.specs`, `-u _printf_float`
- 관련 레슨: BM-B08 (startup·벡터 테이블), BM-B12 (STM32 USART 폴링 + BRR), BM-M04 (인터럽트 UART 링버퍼), C-M10 (mini printf), ARM-M08 (asm 벡터 테이블), BM-B13 (시간 기반 타임아웃)
