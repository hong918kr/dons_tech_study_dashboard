---
id: TOOL-B09
time: 60분
updated: 2026-11-15
---
# TOOL-B09 정적 라이브러리와 링크 순서

## 학습 목표

- `ar rcs` 로 정적 라이브러리(static library, `.a`)를 만들고 `ar t` / `ar tv` / 인덱스(`nm --print-armap`, GNU `nm -s`)로 속을 본다. `.a` 는 **`.o` 묶음 + 심볼 인덱스**이고, 링커는 **멤버(.o) 단위**로만 끌어온다는 것을 맵 파일로 증명한다.
- GNU ld(bfd) 의 "왼쪽→오른쪽 한 번, 지금 미정의인 심볼만" 아카이브 탐색 규칙으로 `-lfoo -lbar` 실패, `-lfoo -lbar -lfoo` / `--start-group` 성공을 **예측**하고, ld64(맥)와 ld.lld 는 왜 순서와 무관한지 설명한다.
- 같은 심볼이 두 `.a` 에 있을 때 **먼저 탐색된 쪽이 조용히 이긴다**는 것과, 같은 멤버에 다른 심볼이 붙어 있으면 multiple definition 으로 바뀌는 함정을 재현한다.
- 펌웨어 링크(`arm-none-eabi-gcc --specs=nano.specs --specs=nosys.specs`)에서 `libuart.a` 안의 `_write` 재지정(retarget)이 무시되고 libnosys 스텁이 들어가는 버그를 `-Wl,-y,_write` 와 맵으로 찾고, 링크 줄만 고쳐 해결한다.
- 증거 도구를 고른다: 맥 `-Wl,-why_load`, `-Wl,-map,x.map` / GNU `-Wl,-Map=x.map`, `-Wl,-y,sym`, `--trace` / lld `--why-extract=-`, `--warn-backrefs`.

## 1. 문제

"맥에서는 빌드되는데 CI(Linux, arm-none-eabi-gcc)에서만 `undefined reference` 가 나요" — 펌웨어 팀에서 가장 흔한 링크 질문이고, 면접에서도 "링커가 아카이브를 어떻게 뒤지나요?" 로 그대로 나온다. SSD 컨트롤러 펌웨어처럼 HAL · 드라이버 · FTL · 유틸을 각각 `.a` 로 빌드해 마지막에 묶는 구조에서는, 라이브러리끼리 서로 부르는 순간 **링크 줄의 순서**가 빌드 성패와 **어떤 구현이 들어가는지**를 결정한다.

정적 라이브러리는 특별한 것이 아니다. `ar` 로 묶은 `.o` 파일 모음 + "이 심볼은 어느 멤버에 있다" 는 인덱스다. 링커는 아카이브 전체를 넣지 않는다 — **미정의(undefined) 심볼을 정의하는 멤버만** 끌어온다. 이 레슨의 프로젝트(`src/`)는 그 규칙이 드러나도록 짰다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <defs>
    <marker id="b09arr1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto">
      <path d="M0 0 L10 5 L0 10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="10" y="115" width="120" height="50" rx="6" class="f-acc" stroke="currentColor"/>
  <text x="20" y="136" fill="currentColor" font-family="monospace" font-size="13">main.o</text>
  <text x="20" y="155" fill="currentColor" font-family="monospace" font-size="12">U foo_entry</text>
  <rect x="150" y="10" width="280" height="270" rx="8" fill="none" stroke="currentColor"/>
  <text x="160" y="30" fill="currentColor" font-weight="bold">libfoo.a  (!&lt;arch&gt; + 인덱스 + 멤버)</text>
  <rect x="160" y="42" width="260" height="46" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="168" y="61" fill="currentColor" font-family="monospace" font-size="12">인덱스: foo_entry→foo_a.o</text>
  <text x="168" y="79" fill="currentColor" font-family="monospace" font-size="12">foo_leaf→foo_b.o  foo_unused→…</text>
  <rect x="160" y="100" width="260" height="46" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="168" y="119" fill="currentColor" font-family="monospace" font-size="12">foo_a.o  T foo_entry</text>
  <text x="168" y="137" fill="currentColor" font-family="monospace" font-size="12">         U bar_mid</text>
  <rect x="160" y="158" width="260" height="46" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="168" y="185" fill="currentColor" font-family="monospace" font-size="12">foo_b.o  T foo_leaf</text>
  <rect x="160" y="216" width="260" height="52" rx="4" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="168" y="237" fill="currentColor" font-family="monospace" font-size="12">foo_unused.o  T foo_unused</text>
  <text x="168" y="257" fill="currentColor" font-size="12">아무도 안 부름 → 안 끌려옴</text>
  <rect x="470" y="10" width="260" height="270" rx="8" fill="none" stroke="currentColor"/>
  <text x="480" y="30" fill="currentColor" font-weight="bold">libbar.a</text>
  <rect x="480" y="42" width="240" height="46" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="488" y="61" fill="currentColor" font-family="monospace" font-size="12">인덱스: bar_mid→bar_a.o</text>
  <text x="488" y="79" fill="currentColor" font-family="monospace" font-size="12">bar_unused→bar_unused.o</text>
  <rect x="480" y="100" width="240" height="46" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="488" y="119" fill="currentColor" font-family="monospace" font-size="12">bar_a.o  T bar_mid</text>
  <text x="488" y="137" fill="currentColor" font-family="monospace" font-size="12">         U foo_leaf</text>
  <rect x="480" y="216" width="240" height="52" rx="4" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="488" y="237" fill="currentColor" font-family="monospace" font-size="12">bar_unused.o  T bar_unused</text>
  <text x="488" y="257" fill="currentColor" font-size="12">안 끌려옴</text>
  <path d="M130 135 L158 125" stroke="currentColor" stroke-width="2" marker-end="url(#b09arr1)"/>
  <text x="134" y="122" fill="currentColor" font-size="12">①</text>
  <path d="M420 115 L478 115" stroke="currentColor" stroke-width="2" marker-end="url(#b09arr1)"/>
  <text x="443" y="107" fill="currentColor" font-size="12">②</text>
  <path d="M480 140 L422 178" stroke="currentColor" stroke-width="2" stroke-dasharray="6 3" marker-end="url(#b09arr1)"/>
  <text x="452" y="178" fill="currentColor" font-size="12">③</text>
  <text x="10" y="302" fill="currentColor" font-size="12">① main.o 가 foo_entry 필요 → foo_a.o    ② foo_a.o 가 bar_mid 필요 → bar_a.o</text>
  <text x="10" y="322" fill="currentColor" font-size="12">③ bar_a.o 가 foo_leaf 필요 → foo_b.o  (이미 지나온 libfoo.a 로 돌아가는 참조, 점선)</text>
</svg>
```

`main.o → foo_a.o → bar_a.o → foo_b.o` 순서로 필요가 생긴다. ③ 이 **이미 지나간 libfoo.a 로 되돌아가는 참조(back-reference)** 라서 GNU ld 에서 문제가 된다. `foo_unused.o`, `bar_unused.o` 는 어느 링커에서도 최종 바이너리에 들어가면 안 된다.

| 파일 | 내용 |
|---|---|
| `src/a/` | Part A: `main.c`, `foo_a.c`, `foo_b.c`, `foo_unused.c` → `libfoo.a` / `bar_a.c`, `bar_unused.c` → `libbar.a` |
| `src/b/` | Part B: `crc_fast.c` → `libcrc_fast.a`, `crc_ref.c` + `crc_vectors.c` → `libcrc_ref.a`, `main_b.c`, `crc.h` |
| `src/c/` | Part C (Cortex-M4): `uart_hw.c` + `uart_retarget.c` → `libuart.a`, `fw_main.c`, `uart.h` |
| `src/p/` | 추가 연습: `test_*.c` → `libtests.a` (생성자로 자기 등록), `main_p.c`, `registry.h` |
| `starter.link` / `solution.link` / `practice.link` | **풀 대상** — 링크 줄과 예측값 (check.sh 가 `source` 한다) |
| `check.sh` | 맥 ld64 + Lima VM(GNU ld.bfd, ld.lld, arm-none-eabi-gcc) 로 링크·실행·맵 검사 |
| `demo_archive.sh` `demo_order.sh` `demo_dup.sh` `demo_fw.sh` `demo_register.sh` | 관찰용. 실제 출력은 `archive.txt` `order.txt` `dup.txt` `fw.txt` `register.txt` |

### Part A — 서로 부르는 두 라이브러리: 예측하고 고치기

- `starter.link` 의 `PRED_*` 10개 칸(`?`)에 각 링크가 `ok` 인지 `fail` 인지 **먼저 예측**한다: ld64 3개(`-lfoo -lbar`, `-lbar -lfoo`, 라이브러리를 `main.o` 앞에), GNU ld.bfd 4개(`-lfoo -lbar`, `-lbar -lfoo`, `-lbar -lfoo -lbar`, 라이브러리 먼저), ld.lld 2개.
- `A_GNU` 를 고쳐 GNU ld.bfd 에서 링크되게 한다. 조건: 실행 결과 `A: foo_entry(5) = 71`, 맵 파일의 "Archive member included" 가 정확히 `foo_a.o`, `foo_b.o`, `bar_a.o` 3개(= `--whole-archive` 금지), 같은 줄이 ld.lld 에서도 링크.

### Part B — 같은 심볼이 두 라이브러리에: 누가 이기나

- `libcrc_fast.a` 와 `libcrc_ref.a` 가 둘 다 `crc32()` · `crc32_impl()` 을 정의한다. `libcrc_ref.a` 에는 별도 멤버 `crc_vectors.o`(테스트 벡터)도 있다. 앱은 **fast 구현 + ref 의 테스트 벡터**를 원한다.
- `B_MAC`(ld64), `B_GNU`(bfd 와 lld 둘 다) 를 고친다. 두 라이브러리 모두 줄에 남겨야 하고 `-all_load` / `-force_load` / `--whole-archive` 금지. 체크: 출력 `B: crc32 impl=fast vectors=3/3 ok`, 맵에 `crc_fast.o` + `crc_vectors.o` 가 있고 `crc_ref.o` 는 없음.

### Part C — 펌웨어: printf 가 UART 로 안 나온다

- Cortex-M4 펌웨어를 `arm-none-eabi-gcc -mcpu=cortex-m4 -mthumb --specs=nano.specs --specs=nosys.specs` 로 링크한다. 벤더가 준 `libuart.a` 에는 `uart_init()`(uart_hw.o)와 newlib 재지정용 `_write()`(uart_retarget.o)가 들어 있다. 링크는 **성공**하는데 printf 출력이 없다.
- `C_GNU` 를 고쳐 `_write` 가 `libuart.a(uart_retarget.o)` 에서 오게 한다. 조건: 아카이브 그대로(`uart_*.o` 를 따로 링크 금지), `-Wl,-y,_write` 가 `libuart.a(uart_retarget.o): definition of _write` 를 보고, 링커 경고 `_write is not implemented and will always fail` 이 없어야 한다.

## 2. Problem (English)

> **Static libraries and link order.**
>
> Our firmware is built as several static libraries and linked at the end. A teammate reports: "It links on my Mac, but the Linux CI build fails with `undefined reference to 'foo_leaf'`", and separately "the board boots but `printf` prints nothing."
>
> You may change **only the link lines** in `<variant>.link` (no source edits, no new files):
>
> 1. `libfoo.a(foo_a.o)` calls `bar_mid()` in `libbar.a`, and `libbar.a(bar_a.o)` calls back `foo_leaf()` in `libfoo.a`. Predict which of ten given link commands succeed on Apple ld64, GNU ld.bfd and ld.lld. Then give a GNU-ld line that links and pulls **only** the three needed members (prove it with the map file).
> 2. `crc32()` exists in both `libcrc_fast.a` and `libcrc_ref.a`; the app needs the fast `crc32` and the test vectors that only `libcrc_ref.a` provides. Keep both libraries on the line, no whole-archive tricks. Expected output: `B: crc32 impl=fast vectors=3/3 ok` on all three linkers.
> 3. Cortex-M4, newlib-nano + nosys: make `printf` reach `libuart.a(uart_retarget.o)`'s `_write` instead of the libnosys stub.
>
> Follow-ups: how does GNU ld search archives, and why do ld64/lld not care about order? What does `--start-group` cost? Two archives define the same symbol — error or not? When do you need `--whole-archive`?

## 3. 시작 코드

풀 파일은 링크 줄과 예측값뿐이다 (bash 문법, check.sh 가 `source`):

<!-- code: starter.link -->

라이브러리 소스는 짧다 (Part A 핵심만):

```c
/* src/a/main.c */      int foo_entry(int x);  ... printf("A: foo_entry(5) = %d\n", foo_entry(5));
/* src/a/foo_a.c */     int bar_mid(int x);   int foo_entry(int x) { return bar_mid(x) + 1; }
/* src/a/bar_a.c */     int foo_leaf(int x);  int bar_mid(int x)   { return foo_leaf(x) * 10; }
/* src/a/foo_b.c */                           int foo_leaf(int x)  { return x + 2; }
/* src/a/foo_unused.c */ const char foo_unused_banner[] = "..."; int foo_unused(int x) { return x * 3; }
```

`(5 + 2) * 10 + 1 = 71`. Part C 의 재지정 함수 (`src/c/uart_retarget.c`) — 앱 코드는 `_write` 를 **직접 부르지 않는다**, libc_nano 의 stdio 만 부른다:

```c
int _write(int fd, const char *buf, int len)
{
	(void)fd;
	for (int i = 0; i < len; i++) {
		if (buf[i] == '\n')
			uart_putc('\r');
		uart_putc(buf[i]);
	}
	return len;
}
```

체커:

<!-- code: check.sh -->

```bash
bash check.sh starter      # 일부 FAIL, exit 1
bash check.sh solution     # 22/22, exit 0
bash check.sh practice     # 25/25, exit 0
```

check.sh 는 `/tmp/TOOL-B09_work/<variant>` (맥, VM 각각) 에서만 작업한다. 맥 단계는 `cc` → Apple ld (`ld -v`: `PROJECT:ld-1266.8`), VM 단계는 `limactl shell elinux -- bash -s` 로 스크립트를 넘겨 GNU ld 2.46 / ld.lld 21.1.8 / arm-none-eabi-gcc 14.2.1 로 돌린다 (VM 은 맥 홈을 읽기 전용으로 마운트하므로 소스는 그 경로에서 읽고, 빌드는 VM 의 /tmp 에서). 전체 약 2.4 초.

처음 실행 결과 (실제 출력):

```text
--- Mac (Apple ld64) ---
FAIL A ld64 main.o -lfoo -lbar: predicted ?, actual ok
FAIL A ld64 main.o -lbar -lfoo: predicted ?, actual ok
FAIL A ld64 -lfoo -lbar main.o: predicted ?, actual ok
PASS B ld64 rules: both -lcrc_fast and -lcrc_ref on the line, no forced load
FAIL B ld64 links and runs: 'B: crc32 impl=ref vectors=3/3 ok'
FAIL B ld64 map: crc_fast.o + crc_vectors.o loaded, crc_ref.o not loaded
--- Lima VM elinux (GNU ld.bfd 2.46 / ld.lld 21 / arm-none-eabi-gcc 14.2) ---
FAIL A bfd main.o -lfoo -lbar: predicted ?, actual fail
FAIL A bfd main.o -lbar -lfoo: predicted ?, actual fail
FAIL A bfd main.o -lbar -lfoo -lbar: predicted ?, actual fail
FAIL A bfd -lfoo -lbar main.o: predicted ?, actual fail
FAIL A lld main.o -lbar -lfoo: predicted ?, actual ok
FAIL A lld -lfoo -lbar main.o: predicted ?, actual ok
FAIL A bfd links and runs: 'link failed'
FAIL A bfd only needed members pulled (foo_a, foo_b, bar_a; no *_unused in nm)
PASS A lld same line still links: 'A: foo_entry(5) = 71'
PASS B GNU rules: both -lcrc_fast and -lcrc_ref on the line, no forced load
FAIL B bfd links and runs: 'link failed'
FAIL B bfd map: crc_fast.o + crc_vectors.o pulled, crc_ref.o not
FAIL B lld links and runs: 'B: crc32 impl=ref vectors=3/3 ok'
PASS C links with libuart.a as an archive (no loose uart_*.o on the line)
FAIL C -y _write: 'libnosys.a(write.o): definition of _write'
FAIL C no '_write is not implemented' warning (libnosys stub not used)
     fw.elf text=7584 data=108 bss=504
4/22 passed
```

starter 의 세 버그가 다 보인다: Part B 는 맥과 lld 에서 **링크는 성공하는데 ref 구현이 들어갔고**(조용한 오답), Part C 도 **링크는 성공**하는데 `_write` 가 libnosys 에서 왔다.

## 4. 힌트

<details>
<summary>힌트 1 — GNU ld 는 "지금 미정의인 심볼" 목록만 들고 왼쪽에서 오른쪽으로 한 번 간다</summary>

아카이브를 만나는 순간의 미정의 심볼 집합 U 를 종이에 적어라. 그 아카이브에서 U 의 심볼을 정의하는 멤버만 끌려오고, 끌려온 멤버가 새로 만든 미정의 심볼도 **같은 아카이브 안에서는** 다시 찾는다. 하지만 지나간 아카이브로는 돌아가지 않는다. 그러면 `-lfoo -lbar` 끝에 U 에 무엇이 남는지 보인다. 라이브러리가 `main.o` 보다 앞에 있으면, 그 아카이브를 지날 때 U 가 비어 있다.

</details>

<details>
<summary>힌트 2 — 순환은 "한 번 더 적기" 또는 "그룹"으로 푼다. --whole-archive 는 아니다</summary>

`foo_leaf` 를 찾으려면 `libbar.a` 다음에 `libfoo.a` 를 한 번 더 지나가야 한다. 아니면 `-Wl,--start-group ... -Wl,--end-group` 으로 묶어 새 미정의가 안 생길 때까지 반복하게 한다. `--whole-archive` 도 링크는 되지만 `foo_unused.o`, `bar_unused.o` 까지 들어간다 — 체커가 맵과 `nm` 으로 잡는다.

</details>

<details>
<summary>힌트 3 — 중복 심볼: 아카이브끼리는 에러가 아니다. 먼저 찾은 쪽에서 멈춘다</summary>

`crc32` 가 미정의인 상태에서 처음 탐색되는 아카이브가 그 멤버를 준다. 그 뒤로 `crc32` 는 정의됨이라 다른 아카이브의 `crc_ref.o` 를 끌어올 이유가 없다 — 그래서 에러도 경고도 없다. ld64 와 lld 는 순서와 무관하게 링크해 주지만 **중복일 때는 줄에 먼저 적힌 아카이브**를 쓴다. GNU ld 는 거기에 더해 `main_b.o` 가 라이브러리 뒤에 있으면 아예 실패한다.

</details>

<details>
<summary>힌트 4 — gcc 는 링크 줄 맨 끝에 libc 를 몰래 붙인다. _write 가 미정의가 되는 시점을 봐라</summary>

`arm-none-eabi-gcc -v` 의 collect2 인자를 보면 내 `-luart` 뒤에 `--start-group -lgcc -lc_nano -lnosys --end-group` 이 붙는다. `-luart` 를 지날 때 `_write` 는 아직 아무도 원하지 않았다(printf 는 libc_nano 안). 링커에게 "`_write` 는 처음부터 미정의" 라고 알려 주는 옵션이 있다: `-u` (`--undefined`).

</details>

## 5. 풀이

### 0. 아카이브 속 — `demo_archive.sh` (`archive.txt`)

맥 (Apple `ar`, 실제 출력):

```text
$ ar rcs libfoo.a foo_a.o foo_b.o foo_unused.o
$ ar tv libfoo.a
rw-r--r--     501/20           96 Nov 15 15:11 2026 __.SYMDEF SORTED
rw-r--r--     501/0           568 Nov 15 15:11 2026 foo_a.o
rw-r--r--     501/0           512 Nov 15 15:11 2026 foo_b.o
rw-r--r--     501/0           712 Nov 15 15:11 2026 foo_unused.o
$ nm --print-armap libfoo.a | head -3   # Mac nm 의 -s 는 "segment section" 이라 다르다
Archive map
_foo_entry in foo_a.o
_foo_leaf in foo_b.o
```

VM (GNU ar 2.46):

```text
$ ar tv libfoo.a
rw-r--r-- 0/0   1424 Dec 31 16:00 1969 foo_a.o
rw-r--r-- 0/0   1256 Dec 31 16:00 1969 foo_b.o
rw-r--r-- 0/0   1488 Dec 31 16:00 1969 foo_unused.o
$ nm -s libfoo.a                     # GNU nm 의 -s = 아카이브 인덱스 출력
Archive index:
foo_entry in foo_a.o
foo_leaf in foo_b.o
foo_unused in foo_unused.o
foo_unused_banner in foo_unused.o   (← 같은 멤버의 심볼 2개)
```

| 항목 | 맥 (BSD 형식, Mach-O 멤버) | GNU (ELF 멤버) |
|---|---|---|
| 파일 매직 | `!<arch>\n` (od 로 확인) | `!<arch>\n` |
| 인덱스 멤버 | `__.SYMDEF SORTED` (`ar t` 에 보임) | 이름 `/` 멤버 (`ar t` 에 안 보임, `nm -s` 로 봄) |
| 인덱스 생성 | `ar rc` 만 해도 생겼다 (`ar -S` 로 끌 수 있음) | `ar rc`, `ar qc` 도 생성 (실측). `s` 는 사실상 중복, `S` 로 끔 |
| 타임스탬프 | 실제 시각, uid 501 | 0/0, mtime 0 (→ 1969 표시, VM TZ 가 PST) — Ubuntu binutils 가 결정적 모드(D)를 기본으로 켠 것으로 보임 (확인 권장). 재현 빌드는 TOOL-A06 |
| 멤버 크기 | foo_a.o 568 B | foo_a.o 1424 B (ELF 섹션 헤더·심볼 테이블이 더 큼) |

인덱스가 없으면? `ar rcS` 로 만들어 봤다 (실제 출력):

```text
# Mac
ld: 64-bit mach-o member 'foo_unused.o' not 8-byte aligned in './libnoidx.a'
link rc=1                 → ranlib libnoidx.a 후 link rc=0
# VM
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libnoidx.a: error adding symbols: archive has no index; run ranlib to add one
bfd rc=1
lld rc=0                  → ld.lld 는 인덱스 없이도 멤버 심볼 테이블을 직접 읽어 링크했다
```

맥 메시지는 인덱스가 아니라 **정렬** 이야기다 — `-S` 로 만들면 멤버가 8바이트 정렬되지 않고, `ranlib` 이 다시 쓰면서 고쳐지는 것으로 보인다 (추정, 확인 권장). 실무 결론은 같다: 직접 `ar` 를 부르면 `ar rcs` 로, 맥에서는 `libtool -static -o` 도 같은 결과(멤버 목록 동일, 실측).

### Part A — GNU ld 의 한 번 훑기 (single pass)

`demo_order.sh` 의 실제 매트릭스 (`order.txt`):

| 링크 줄 (`<ld> ... -o app`) | ld64 (맥) | GNU ld.bfd | ld.lld |
|---|---|---|---|
| `main.o -L. -lfoo -lbar` | ok | **fail** `undefined reference to 'foo_leaf'` | ok |
| `main.o -L. -lbar -lfoo` | ok | **fail** `undefined reference to 'bar_mid'` | ok |
| `main.o -L. -lbar -lfoo -lbar` | (안 돌림) | **fail** `undefined reference to 'foo_leaf'` | ok |
| `-L. -lfoo -lbar main.o` | ok | **fail** `undefined reference to 'foo_entry'` | ok |
| `main.o -L. -lfoo -lbar -lfoo` | ok + `ld: warning: ignoring duplicate libraries: '-lfoo'` | ok | ok |
| `main.o -L. -Wl,--start-group -lfoo -lbar -Wl,--end-group` | — | ok | ok |

GNU ld 의 실제 메시지는 **누가** 참조했는지(멤버까지)를 알려 준다:

```text
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libbar.a(bar_a.o): in function `bar_mid':
bar_a.c:(.text+0x8): undefined reference to `foo_leaf'
collect2: error: ld returned 1 exit status
```

읽는 법: "`libbar.a` 의 멤버 `bar_a.o` 안 `bar_mid` 함수가 `foo_leaf` 를 부르는데 못 찾았다". `foo_leaf` 는 분명 `libfoo.a` 에 있다 — 그러니 순서 문제다. 아래 그림이 bfd 가 실제로 하는 일이다.

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">GNU ld.bfd: 왼쪽→오른쪽 한 번. 아카이브는 "그 순간 미정의(U)인 심볼" 만 해결</text>
  <text x="10" y="44" fill="currentColor" font-family="monospace" font-size="12">gcc -fuse-ld=bfd main.o -L. -lfoo -lbar</text>
  <rect x="10" y="52" width="135" height="70" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="18" y="72" fill="currentColor" font-family="monospace" font-size="12">main.o</text>
  <text x="18" y="92" fill="currentColor" font-size="12">무조건 포함</text>
  <text x="18" y="112" fill="currentColor" font-family="monospace" font-size="12">U={foo_entry}</text>
  <rect x="155" y="52" width="135" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="163" y="72" fill="currentColor" font-family="monospace" font-size="12">-lfoo</text>
  <text x="163" y="92" fill="currentColor" font-size="12">+foo_a.o</text>
  <text x="163" y="112" fill="currentColor" font-family="monospace" font-size="12">U={bar_mid}</text>
  <rect x="300" y="52" width="135" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="308" y="72" fill="currentColor" font-family="monospace" font-size="12">-lbar</text>
  <text x="308" y="92" fill="currentColor" font-size="12">+bar_a.o</text>
  <text x="308" y="112" fill="currentColor" font-family="monospace" font-size="12">U={foo_leaf}</text>
  <rect x="590" y="52" width="140" height="70" rx="5" class="f-warn" stroke="currentColor"/>
  <text x="598" y="72" fill="currentColor" font-weight="bold">끝: 실패</text>
  <text x="598" y="92" fill="currentColor" font-size="12">foo_leaf 남음</text>
  <text x="598" y="112" fill="currentColor" font-size="12">libfoo 는 지나감</text>
  <path d="M145 87 L155 87 M290 87 L300 87 M435 87 L590 87" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="160" fill="currentColor" font-family="monospace" font-size="12">gcc -fuse-ld=bfd main.o -L. -lfoo -lbar -lfoo</text>
  <rect x="10" y="168" width="135" height="70" rx="5" class="f-acc" stroke="currentColor"/>
  <text x="18" y="188" fill="currentColor" font-family="monospace" font-size="12">main.o</text>
  <text x="18" y="228" fill="currentColor" font-family="monospace" font-size="12">U={foo_entry}</text>
  <rect x="155" y="168" width="135" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="163" y="188" fill="currentColor" font-family="monospace" font-size="12">-lfoo</text>
  <text x="163" y="208" fill="currentColor" font-size="12">+foo_a.o</text>
  <text x="163" y="228" fill="currentColor" font-family="monospace" font-size="12">U={bar_mid}</text>
  <rect x="300" y="168" width="135" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="308" y="188" fill="currentColor" font-family="monospace" font-size="12">-lbar</text>
  <text x="308" y="208" fill="currentColor" font-size="12">+bar_a.o</text>
  <text x="308" y="228" fill="currentColor" font-family="monospace" font-size="12">U={foo_leaf}</text>
  <rect x="445" y="168" width="135" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="453" y="188" fill="currentColor" font-family="monospace" font-size="12">-lfoo (다시)</text>
  <text x="453" y="208" fill="currentColor" font-size="12">+foo_b.o</text>
  <text x="453" y="228" fill="currentColor" font-family="monospace" font-size="12">U={}</text>
  <rect x="590" y="168" width="140" height="70" rx="5" class="f-ok" stroke="currentColor"/>
  <text x="598" y="188" fill="currentColor" font-weight="bold">끝: 성공</text>
  <text x="598" y="208" fill="currentColor" font-size="12">unused 2개는</text>
  <text x="598" y="228" fill="currentColor" font-size="12">끝까지 안 들어감</text>
  <path d="M145 203 L155 203 M290 203 L300 203 M435 203 L445 203 M580 203 L590 203" stroke="currentColor" stroke-width="2"/>
  <text x="10" y="270" fill="currentColor" font-size="12">• 한 아카이브를 지나는 동안은 인덱스를 반복해서 본다 → 같은 .a 안의 뒤로 가는 참조는 OK (아래 inner 실험)</text>
  <text x="10" y="290" fill="currentColor" font-size="12">• --start-group A B --end-group = 묶음 전체를 "새 미정의가 안 생길 때까지" 반복 (맵 결과는 위와 같은 3개 멤버)</text>
  <text x="10" y="310" fill="currentColor" font-size="12">• ld64 · ld.lld: 아카이브 심볼을 기억해 두고 나중에도 찾는다 → 순서 무관. 단 중복 심볼은 먼저 적힌 쪽 (Part B)</text>
</svg>
```

`-lbar -lfoo -lbar` 가 실패하는 이유도 같은 방법으로 풀린다: 첫 `-lbar` 때 U={foo_entry} 라 아무것도 안 끌려오고, `-lfoo` 에서 foo_a.o 를 끌어오면 U={bar_mid} — 이 순간 `foo_leaf` 는 아직 아무도 원하지 않으므로 foo_b.o 는 안 들어온다. 마지막 `-lbar` 에서 bar_a.o 가 들어오며 U={foo_leaf} 가 되지만 그 뒤에 libfoo 가 없다. **반복해 적을 라이브러리는 "뒤로 가는 참조의 대상"** 이다.

**정답 (bfd)**: `A_GNU="main.o -L. -lfoo -lbar -lfoo"`. 증거 — bfd 맵 파일의 "Archive member included" 절과 `-y foo_leaf`, `--trace` (실제 출력, `/usr/lib` 줄 생략):

```text
main.o
./libfoo.a
./libbar.a
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libbar.a(bar_a.o): reference to foo_leaf
./libfoo.a
/usr/bin/aarch64-linux-gnu-ld.bfd: ./libfoo.a(foo_b.o): definition of foo_leaf
Archive member included to satisfy reference by file (symbol)

./libfoo.a(foo_a.o)           main.o (foo_entry)
./libbar.a(bar_a.o)           ./libfoo.a(foo_a.o) (bar_mid)
./libfoo.a(foo_b.o)           ./libbar.a(bar_a.o) (foo_leaf)
```

맵의 한 줄 = "이 멤버가, 이 파일의 (이 심볼) 참조 때문에 들어왔다". 세 줄이 그림의 ①②③ 화살표와 정확히 같고, `foo_unused.o` / `bar_unused.o` 는 없다. `--trace` 는 bfd 2.46 에서 멤버가 아니라 **아카이브 이름**만 찍는다 — 두 번째 `./libfoo.a` 가 바로 다시 훑은 흔적이다. `--start-group` 버전의 맵도 같은 세 줄이었다.

같은 아카이브 안의 뒤로 가는 참조는 괜찮은가? `demo_order.sh` 의 inner 실험: `liba.a` 멤버 순서 `m1.o m2.o`, m2(entry) 가 m1(leaf) 을 부름:

```text
./liba.a(m2.o)                main2.o (entry)          ← ar t 순서는 m1.o m2.o, link rc=0, run rc=0
./liba.a(m1.o)                ./liba.a(m2.o) (leaf)
```

m1.o 가 아카이브에서 **앞**에 있는데도 들어왔다 → bfd 는 한 아카이브를 지나는 동안 인덱스를 새 미정의가 없을 때까지 반복해 본다. 그래서 "아카이브 안 멤버 순서" 는 (인덱스가 있는 한) 상관없고, 문제는 **아카이브 사이**의 순서뿐이다. 옛날 인덱스 없던 시절엔 `lorder | tsort` 로 멤버 순서를 정했다 — 이 맥에 아직 있다 (`order.txt`):

```text
-- tsort (members):
bar_unused.o foo_unused.o main.o foo_a.o bar_a.o foo_b.o
-- tsort (libraries):
tsort: cycle in data tsort: libfoo tsort: libbar main.o libbar libfoo
```

멤버 단위로는 순환이 없다(main → foo_a → bar_a → foo_b). 순환은 **라이브러리로 묶는 방식** 때문에 생긴다 — foo_b.o 를 libbar 나 제3의 라이브러리로 옮기면 사라진다. 이게 근본 해법이고, `-lfoo` 반복이나 그룹은 증상 처리다.

#### ld64 와 ld.lld 는 왜 순서를 안 타나

- **ld64**: `man ld` 의 Libraries 절 그대로 — "Unlike traditional linkers, ld will continually search a static library while linking. There is no need to specify a static library multiple times on the command line." 그래서 `-lfoo` 를 두 번 쓰면 `ignoring duplicate libraries` 경고까지 낸다. 증거 도구는 `-Wl,-why_load` 와 `-Wl,-map,a.map` (실제 출력):

```text
'_foo_entry' caused load of /private/tmp/TOOL-B09_demo_order/libfoo.a[2](foo_a.o)
'_bar_mid' caused load of /private/tmp/TOOL-B09_demo_order/libbar.a[2](bar_a.o)
'_foo_leaf' caused load of /private/tmp/TOOL-B09_demo_order/libfoo.a[3](foo_b.o)
# Object files:                      (a.map 발췌)
[  1] main.o
[  2] ./libfoo.a(foo_a.o)
[  3] ./libfoo.a(foo_b.o)
[  4] ./libbar.a(bar_a.o)
```

  `[2]`, `[3]` 은 아카이브 안 멤버 번호 (`[1]` 이 `__.SYMDEF SORTED`). 주의: `-Wl,-t` 는 man 에 "Logs each file the linker loads" 라고 돼 있지만, 이 맥 ld 는 `foo_unused.o`, `bar_unused.o` 까지 찍었다 (최종 `nm app` 에는 없음; `-ld_classic` 도 마찬가지). **읽은 파일 목록이지 들어간 목록이 아니다** — 판단은 `-why_load` / `-map` 으로.
- **ld.lld**: 아카이브를 만나면 그 심볼들을 "lazy 심볼" 로 심볼 테이블에 등록해 두고, 나중에 누가 참조하면 그때 멤버를 꺼낸다 (lld 문서의 archive 설명 요지). 그래서 라이브러리가 `main.o` 앞에 있어도 된다. `--help` 에는 `--start-group  Ignored for compatibility with GNU unless you pass --warn-backrefs` 라고 나온다. 증거는 `--why-extract=-`:

```text
reference	extracted	symbol
./libfoo.a(foo_a.o)	./libbar.a(bar_a.o)	bar_mid
main.o	./libfoo.a(foo_a.o)	foo_entry
./libbar.a(bar_a.o)	./libfoo.a(foo_b.o)	foo_leaf
```

**CI 에서 쓸 만한 것 — `--warn-backrefs`**: lld 로 빌드하면서 "GNU ld 였다면 깨졌을 순서" 를 경고받는다 (실제 출력):

| 줄 | lld `--warn-backrefs` 결과 | bfd 실제 |
|---|---|---|
| `-lfoo -lbar` | `warning: backward reference detected: foo_leaf in ./libbar.a(bar_a.o) refers to ./libfoo.a(foo_b.o)` | fail |
| `-lbar -lfoo` | `warning: backward reference detected: bar_mid in ./libfoo.a(foo_a.o) refers to ./libbar.a(bar_a.o)` | fail |
| `-lfoo -lbar -lfoo` | 경고 없음 | ok |
| `-lbar -lfoo -lbar` | **경고 없음** | **fail** |

마지막 줄이 함정이다: lld 는 경고하지 않았는데 bfd 는 실패한다. lld 는 멤버를 꺼내는 시점이 bfd 와 달라서(foo_a.o 를 꺼내는 즉시 bar_a.o 를 꺼냄) bfd 의 실패 경로를 완벽히 흉내 내지 못하는 것으로 보인다 (원인 추정). `--warn-backrefs` 는 좋은 린트지만, **GNU ld 가 타깃이면 GNU ld 로 CI 를 돌리는 것**이 확실한 검사다.

gcc 자신도 순환을 반복으로 푼다 — `gcc -v` 의 collect2 인자 꼬리 (실제):

```text
... crtbeginS.o -lfoo -lbar -lfoo -lgcc --push-state --as-needed -lgcc_s --pop-state -lc -lgcc --push-state --as-needed -lgcc_s --pop-state crtendS.o crtn.o
```

`-lgcc` 가 `-lc` 앞뒤로 두 번 — libc 가 libgcc 의 헬퍼를 다시 필요로 할 수 있기 때문이다 (`-lfoo -lbar -lfoo` 와 같은 모양).

### Part B — 중복 심볼: 먼저 탐색된 아카이브가 조용히 이긴다

`demo_dup.sh` 실제 결과 (`dup.txt`):

| 줄 | ld64 | bfd | lld |
|---|---|---|---|
| `main_b.o -L. -lcrc_ref -lcrc_fast` | impl=**ref** | impl=**ref** | impl=**ref** |
| `main_b.o -L. -lcrc_fast -lcrc_ref` | impl=fast | impl=fast | impl=fast |
| `-L. -lcrc_ref -lcrc_fast main_b.o` (starter) | impl=**ref** | **fail** (undefined `crc_vectors_n` …) | impl=**ref** |

세 링커 모두 **에러도 경고도 없이** 먼저 적힌 아카이브를 썼다. 맥 `-why_load` 가 이유를 보여 준다:

```text
'_crc32' caused load of /private/tmp/TOOL-B09_demo_dup/libcrc_ref.a[2](crc_ref.o)
'_crc_vectors' caused load of /private/tmp/TOOL-B09_demo_dup/libcrc_ref.a[3](crc_vectors.o)
B: crc32 impl=ref vectors=3/3 ok
```

왜 에러가 아닌가: 링커가 아카이브에서 멤버를 꺼내는 이유는 "미정의 심볼을 채우려고" 뿐이다. `crc_ref.o` 로 `crc32` 가 채워지면 `libcrc_fast.a` 의 `crc_fast.o` 를 꺼낼 이유가 없다 → 두 정의가 **한 링크에 같이 들어가는 일이 없다**. `.o` 두 개를 직접 링크하면 multiple definition(TOOL-B05)인 것과 대조적이다. 그래서 테스트 벡터가 3/3 통과해도(두 구현 모두 맞으니까) 어떤 구현이 들어갔는지는 모른다 — 성능 회귀나 "디버그용 구현이 양산 펌웨어에 들어간" 사고가 이렇게 생긴다.

**정답**: `B_MAC` = `B_GNU` = `"main_b.o -L. -lcrc_fast -lcrc_ref"` — 오브젝트를 먼저(bfd 필수), 이길 라이브러리를 먼저. bfd 맵 (실제):

```text
Archive member included to satisfy reference by file (symbol)

./libcrc_fast.a(crc_fast.o)   main_b.o (crc32)
./libcrc_ref.a(crc_vectors.o)
                              main_b.o (crc_vectors_n)
```

`libcrc_ref.a` 에서 `crc_vectors.o` 만 왔고 `crc_ref.o` 는 안 왔다 — **같은 아카이브라도 멤버 단위**.

**같은 멤버 함정** (`demo_dup.sh` 의 mono): `crc_ref.c` 와 `crc_vectors.c` 를 한 파일로 합쳐 `libcrc_mono.a` 를 만들고 `main_b.o -L. -lcrc_fast -lcrc_mono` 로 링크하면, 벡터 때문에 꺼낸 멤버에 `crc32` 가 같이 붙어 와 충돌한다 (실제):

```text
# Mac ld64
duplicate symbol '_crc32' in:
    /private/tmp/TOOL-B09_demo_dup/libcrc_fast.a[2](crc_fast.o)
    /private/tmp/TOOL-B09_demo_dup/libcrc_mono.a[2](crc_ref_mono.o)
ld: 2 duplicate symbols                       (_crc32_impl 도 같은 형식으로 한 번 더)
# VM bfd
crc_ref_mono.c:(.text+0x0): multiple definition of `crc32'; ./libcrc_fast.a(crc_fast.o):crc_fast.c:(.text+0x0): first defined here
# VM lld
ld.lld: error: duplicate symbol: crc32       (>>> defined at … in archive ./libcrc_fast.a / ./libcrc_mono.a)
```

`--whole-archive` 로 두 라이브러리를 다 넣어도 같은 multiple definition 이 났다. 교훈: 라이브러리는 **함수 하나(또는 같이 쓰이는 묶음) 당 .o 하나**로 쪼개야 "필요한 것만" 이 제대로 동작한다 — newlib 이 `libc_a-writer.o`, `libc_a-closer.o` 처럼 잘게 쪼개진 이유다.

### Part C — 펌웨어: `_write` 를 libnosys 가 가져갔다

`demo_fw.sh` (전부 VM, `fw.txt`). 먼저 gcc 가 내 줄 뒤에 무엇을 붙이는지:

```text
crt0.o fw_main.o -luart --start-group -lgcc -lc_nano --end-group --start-group -lgcc -lc_nano -lnosys --end-group --start-group -lgcc -lc_nano -lnosys --end-group
```

(nano.specs 가 `-lc` 를 `-lc_nano` 로, nosys.specs 가 `--start-group %G %(nosys_libc) %(nosys_libgloss) --end-group` 을 붙인다 — `nosys.specs` 원문 확인. 같은 그룹이 반복되는 건 specs 가 겹쳐 적용된 결과로 보인다.) libc 와 libnosys 가 서로 필요해서 **gcc 스스로 `--start-group`** 을 쓴다는 점도 기억하자.

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C: _write 가 "미정의" 가 되는 시점이 -luart 보다 늦다</text>
  <rect x="10" y="34" width="80" height="40" rx="4" class="f-mut" stroke="currentColor"/>
  <text x="18" y="58" fill="currentColor" font-family="monospace" font-size="12">crt0.o</text>
  <rect x="100" y="34" width="100" height="40" rx="4" class="f-acc" stroke="currentColor"/>
  <text x="108" y="58" fill="currentColor" font-family="monospace" font-size="12">fw_main.o</text>
  <rect x="210" y="34" width="90" height="40" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="218" y="58" fill="currentColor" font-family="monospace" font-size="12">-luart</text>
  <rect x="310" y="34" width="420" height="40" rx="4" class="f-mut" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="318" y="58" fill="currentColor" font-family="monospace" font-size="12">--start-group -lgcc -lc_nano -lnosys --end-group</text>
  <text x="318" y="90" fill="currentColor" font-size="12">↑ gcc + nano/nosys specs 가 자동으로 붙임</text>
  <text x="10" y="118" fill="currentColor" font-weight="bold">starter: fw_main.o -L. -luart</text>
  <text x="20" y="140" fill="currentColor" font-size="12">-luart 를 지날 때 U = {uart_init, printf}  →  uart_hw.o 만. _write 는 아직 아무도 원하지 않음</text>
  <text x="20" y="160" fill="currentColor" font-size="12">그룹: printf → … → _write_r(libc_a-writer.o) → U = {_write}  →  같은 그룹의 libnosys.a(write.o) 가 채움</text>
  <rect x="20" y="168" width="700" height="28" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="30" y="187" fill="currentColor" font-family="monospace" font-size="12">_write @0x9ce0: errno = 88 (ENOSYS); return -1   → printf 는 조용히 버려짐, 링크는 성공</text>
  <text x="10" y="222" fill="currentColor" font-weight="bold">solution: fw_main.o -L. -Wl,--undefined=_write -luart</text>
  <text x="20" y="244" fill="currentColor" font-size="12">시작부터 U = {_write}  →  -luart 에서 uart_retarget.o 를 꺼냄 (맵 이유 칸: "(_write)")</text>
  <rect x="20" y="254" width="700" height="28" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="30" y="273" fill="currentColor" font-family="monospace" font-size="12">_write @0x8154 (uart_putc 0x8144 바로 뒤) → libnosys 의 write.o 는 필요 없어져 안 들어감</text>
</svg>
```

starter 링크의 실제 증거 (`<sysroot>` = `/usr/lib/gcc/arm-none-eabi/14.2.1/../../../arm-none-eabi/` 로 줄임, `_close/_fstat/_isatty/_lseek/_read` 경고 생략):

```text
<sysroot>/bin/ld: <sysroot>/lib/thumb/v7e-m/nofp/libc_nano.a(libc_a-writer.o): reference to _write
<sysroot>/bin/ld: <sysroot>/lib/thumb/v7e-m/nofp/libnosys.a(write.o): definition of _write
<sysroot>/bin/ld: <sysroot>/lib/thumb/v7e-m/nofp/libc_nano.a(libc_a-writer.o): in function `_write_r':
/build/newlib-Ipb6mw/newlib-4.6.0.20260123/build_nano/arm-none-eabi/thumb/v7e-m/nofp/newlib/../../../../../../newlib/libc/reent/writer.c:49:(.text+0x14): warning: _write is not implemented and will always fail
rc=0

./libuart.a(uart_hw.o)        fw_main.o (uart_init)
--
<sysroot>/lib/thumb/v7e-m/nofp/libnosys.a(write.o)
                              <sysroot>/lib/thumb/v7e-m/nofp/libc_nano.a(libc_a-writer.o) (_write)
00008144 T uart_putc
00009ce0 T _write
   7584	    108	    504	   8196	   2004	fw.elf          (text data bss dec hex)
00009ce0 <_write>:
    9ce0:	4b02      	ldr	r3, [pc, #8]	@ (9cec <_write+0xc>)
    9ce2:	2258      	movs	r2, #88	@ 0x58
    9ce4:	601a      	str	r2, [r3, #0]
    9ce6:	f04f 30ff 	mov.w	r0, #4294967295	@ 0xffffffff
    9cea:	4770      	bx	lr
```

한 줄씩: (1) `-y _write` — libc_nano 의 `writer.o` 가 `_write` 를 참조, 정의는 `libnosys.a(write.o)`. (2) 링커 경고 — libnosys 스텁에는 "링크되면 경고하라" 는 표시가 붙어 있어서 `warning: _write is not implemented and will always fail` 이 뜬다 (경고 섹션 메커니즘 — 확인 권장). **CI 로그에서 이 한 줄이 유일한 단서**다. (3) 맵 — `uart_hw.o` 는 `uart_init` 때문에 들어왔다. 즉 "libuart 는 분명 링크됐다" 는 착각이 생긴다. (4) 디스어셈블리 — `errno = 88`(newlib 의 ENOSYS), `return -1`. printf 는 에러를 무시하므로 보드는 정상 부팅하는데 로그만 없다.

**정답**: `C_GNU="fw_main.o -L. -Wl,--undefined=_write -luart"`. 결과 (실제):

```text
<sysroot>/bin/ld: ./libuart.a(uart_retarget.o): definition of _write
<sysroot>/bin/ld: <sysroot>/lib/thumb/v7e-m/nofp/libc_nano.a(libc_a-writer.o): reference to _write
rc=0

./libuart.a(uart_hw.o)        fw_main.o (uart_init)
./libuart.a(uart_retarget.o)  (_write)
00008144 T uart_putc
00008154 T _write
   7616	    108	    504	   8228	   2024	fw.elf          (text data bss dec hex)
```

맵의 `(_write)` 앞이 비어 있다 = 어떤 파일의 참조가 아니라 **명령줄 `-u` 가 만든 미정의** 때문에 들어왔다는 뜻. 정의가 참조보다 먼저 찍힌 것도 그 순서를 보여 준다. text 는 7584 → 7616 B (+32 B: 진짜 `_write` 루프가 스텁보다 길다).

`-u` 의 위치: `-luart -Wl,-u,_write` 처럼 **뒤에** 적어도 결과가 같았다(`fw.txt` 셋째 블록, `_write @0x8154`). 이 ld 2.46 에서는 `-u` 가 위치와 무관하게 처음부터 미정의로 등록되는 것으로 관찰됐다 — 문서상 보장으로 믿기보다 앞에 두는 습관을 권한다 (확인 권장).

다른 해법과 비교:

| 방법 | 장점 | 단점 |
|---|---|---|
| `-Wl,-u,_write` (정답) | 의도가 링크 줄에 드러남, 그 멤버만 | 재지정 심볼마다 적어야 함 (`_read`, `_sbrk` …) |
| `-Wl,--whole-archive -luart -Wl,--no-whole-archive` (연습 2) | libuart 전체 보장 | 안 쓰는 드라이버까지 Flash 에 (TOOL-M13 의 gc-sections 로 일부 회수) |
| `-lc_nano -luart` (libc 뒤에 다시) | 링크 줄만 | gcc 가 붙이는 줄 구조에 의존, 깨지기 쉬움 (실측으로는 동작) |
| 재지정 `.o` 를 아카이브에 넣지 않고 앱 오브젝트로 링크 | 가장 확실 (오브젝트는 무조건 포함) | 이번 문제 조건(벤더 아카이브만) 위반 — 실제 프로젝트라면 이게 정석 |
| `--specs=nosys.specs` 빼기 | 스텁이 아예 없어 미정의 에러로 드러남 | 다른 syscalls(`_sbrk` 등) 를 모두 직접 제공해야 함 |

같은 원리의 친척: 스타트업의 weak 인터럽트 핸들러(`USART1_IRQHandler`)를 `.a` 안에 강한 정의로 넣으면, 벡터 테이블이 weak 기본값을 이미 가지고 있어서 미정의가 생기지 않고 → 아카이브 멤버가 **안 끌려온다** (weak/alias 자체는 TOOL-B05). 핸들러·재지정·자기 등록 객체는 아카이브가 아니라 오브젝트로 링크하거나, `-u` / `--whole-archive` 로 명시한다.

## 6. 정답 코드

<!-- code: solution.link diff=starter.link -->

```bash
bash check.sh solution
```

```text
--- Mac (Apple ld64) ---
PASS A ld64 main.o -lfoo -lbar: predicted ok, actual ok
PASS A ld64 main.o -lbar -lfoo: predicted ok, actual ok
PASS A ld64 -lfoo -lbar main.o: predicted ok, actual ok
PASS B ld64 rules: both -lcrc_fast and -lcrc_ref on the line, no forced load
PASS B ld64 links and runs: 'B: crc32 impl=fast vectors=3/3 ok'
PASS B ld64 map: crc_fast.o + crc_vectors.o loaded, crc_ref.o not loaded
--- Lima VM elinux (GNU ld.bfd 2.46 / ld.lld 21 / arm-none-eabi-gcc 14.2) ---
PASS A bfd main.o -lfoo -lbar: predicted fail, actual fail
PASS A bfd main.o -lbar -lfoo: predicted fail, actual fail
PASS A bfd main.o -lbar -lfoo -lbar: predicted fail, actual fail
PASS A bfd -lfoo -lbar main.o: predicted fail, actual fail
PASS A lld main.o -lbar -lfoo: predicted ok, actual ok
PASS A lld -lfoo -lbar main.o: predicted ok, actual ok
PASS A bfd links and runs: 'A: foo_entry(5) = 71'
     map members: ./libbar.a(bar_a.o) ./libfoo.a(foo_a.o) ./libfoo.a(foo_b.o) 
PASS A bfd only needed members pulled (foo_a, foo_b, bar_a; no *_unused in nm)
PASS A lld same line still links: 'A: foo_entry(5) = 71'
PASS B GNU rules: both -lcrc_fast and -lcrc_ref on the line, no forced load
PASS B bfd links and runs: 'B: crc32 impl=fast vectors=3/3 ok'
PASS B bfd map: crc_fast.o + crc_vectors.o pulled, crc_ref.o not
PASS B lld links and runs: 'B: crc32 impl=fast vectors=3/3 ok'
PASS C links with libuart.a as an archive (no loose uart_*.o on the line)
PASS C -y _write: 'libuart.a(uart_retarget.o): definition of _write'
PASS C no '_write is not implemented' warning (libnosys stub not used)
     fw.elf text=7616 data=108 bss=504
22/22 passed
```

체커가 잡는 "편법": Part A 를 `--whole-archive` 로 풀면 링크·실행은 통과하지만 맵에 `bar_unused.o`, `foo_unused.o` 가 `(--whole-archive)` 이유로 찍혀 `A bfd only needed members` 가 FAIL 한다 (starter.link 에 임시로 넣어 직접 확인).

## 7. 자주 하는 실수

1. **라이브러리를 오브젝트보다 앞에 적음** — `gcc -lfoo -lbar main.o`. 맥·lld 에서는 되는데 bfd 는 `undefined reference to 'foo_entry'`. 규칙: **오브젝트 → 그 오브젝트가 쓰는 라이브러리 → 그 라이브러리가 쓰는 라이브러리** (의존하는 쪽이 왼쪽). Makefile 이면 `$(CC) $(OBJS) $(LDFLAGS) $(LDLIBS)` 순서 (TOOL-B08).
2. **"맥에서 되니까 CI 에서도 된다"** — ld64 는 아카이브를 계속 검색하므로 순서 버그를 숨긴다. 크로스 타깃(arm-none-eabi = GNU ld)은 순서를 탄다. 대책: CI 에서 GNU ld 로 링크, 또는 lld `--warn-backrefs` (단 위 표처럼 놓치는 경우 있음).
3. **순환을 `--whole-archive` 로 "해결"** — 안 쓰는 멤버가 전부 들어가 Flash 낭비, 중복 정의가 있으면 multiple definition. 순환은 `-lfoo -lbar -lfoo` / 그룹으로, 근본적으로는 멤버를 옮겨 라이브러리 의존을 한 방향으로.
4. **`--whole-archive` 를 닫지 않음** — `-Wl,--whole-archive -ltests` 로 끝내면 gcc 가 뒤에 붙이는 `libgcc.a` 까지 통째로 들어가 실제로 `multiple definition of '__aarch64_cas1_relax'` (bfd) / `duplicate symbol: __aarch64_cas1_relax` (lld) 가 났다 (`register.txt`). 항상 `-Wl,--no-whole-archive` 로 닫는다.
5. **맥 `-Wl,-t` 를 "들어간 파일" 로 읽음** — 이 맥 ld 는 안 들어간 `foo_unused.o` 도 찍었다. 들어간 이유는 `-why_load`, 결과는 `-map` / `nm`.
6. **중복 심볼이 링크 에러가 아니라고 안심** — 아카이브 사이 중복은 조용히 첫 번째가 이긴다. 같은 API 의 두 구현(HAL 레퍼런스 vs 최적화, 테스트용 mock vs 실제)을 둘 다 링크 줄에 두면 순서가 곧 사양이다. `-why_load` / 맵으로 확인하고, mock 은 별도 테스트 바이너리로 분리.
7. **재지정/핸들러/자기 등록 객체를 `.a` 에 넣음** — 아무도 이름으로 참조하지 않으면 안 끌려온다(Part C, 연습 3). 링크는 성공하므로 테스트로만 드러난다.

## 8. 면접 Q&A

<details>
<summary>Q1. 정적 라이브러리(.a)는 정확히 무엇이고, 링커는 거기서 무엇을 가져오나요?</summary>

`ar` 형식(`!<arch>\n` 매직)의 `.o` 묶음 + 심볼 인덱스(GNU: `/` 멤버, 맥: `__.SYMDEF SORTED`)다. 링크 시점에 복사되므로 실행 파일에 코드가 들어가고 런타임 의존이 없다 — 동적 로더가 없는 MCU 펌웨어는 사실상 이것만 쓴다 (공유 라이브러리는 TOOL-M06). 링커는 인덱스에서 **현재 미정의인 심볼**을 찾아 그 심볼을 정의한 **멤버(.o) 전체**를 가져온다. 아카이브 전체도, 함수 하나도 아닌 멤버 단위다. 그래서 같은 멤버의 다른 함수도 딸려 오고(Part B mono 함정), 함수별로 파일을 나누는 것(또는 `-ffunction-sections` + `--gc-sections`, TOOL-M13)이 크기에 중요하다.

</details>

<details>
<summary>Q2. GNU ld 에서 링크 순서가 왜 중요하죠? 올바른 순서 규칙은?</summary>

bfd 는 명령줄을 왼쪽에서 오른쪽으로 한 번 처리한다. 오브젝트는 무조건 넣고, 아카이브는 그 순간의 미정의 심볼을 채우는 멤버만 넣는다(그 아카이브 안에서는 새 미정의가 없을 때까지 반복). 지나간 아카이브로는 돌아가지 않는다. 그래서 규칙: 사용하는 쪽이 왼쪽, 제공하는 쪽이 오른쪽 — 라이브러리 의존 그래프의 위상 정렬(topological sort) 순서. 순환이 있으면 순환의 대상을 한 번 더 적거나(`-lfoo -lbar -lfoo`) `--start-group/--end-group`. 실측: `-lfoo -lbar`, `-lbar -lfoo`, `-lbar -lfoo -lbar` 셋 다 bfd 에서 실패, `-lfoo -lbar -lfoo` 성공.

</details>

<details>
<summary>Q3. 맥(ld64)이나 lld 에서는 순서가 상관없던데, 그럼 순서를 신경 안 써도 되나요?</summary>

ld64 는 man 페이지에 "continually search a static library" 라고 명시돼 있고, lld 는 아카이브 심볼을 lazy 심볼로 기억해 두었다가 나중 참조 때 꺼낸다. 둘 다 Part A 의 모든 순서를 링크했다. 하지만 (1) 타깃 툴체인(arm-none-eabi-gcc, Yocto 의 gcc)은 대개 GNU ld 라 CI 에서 깨지고, (2) **중복 심볼이 있으면 순서가 어떤 구현이 들어갈지 결정**한다 — Part B 에서 세 링커 모두 먼저 적힌 아카이브를 썼다. 그래서 순서는 여전히 "의존 순서대로" 쓰고, lld 를 쓰면 `--warn-backrefs` 로 GNU 호환성을 점검한다 (단 `-lbar -lfoo -lbar` 처럼 놓치는 경우를 실측했다).

</details>

<details>
<summary>Q4. --start-group 은 왜 기본 동작이 아닌가요? 비용은?</summary>

GNU ld man 페이지: 그룹 안 아카이브들을 "새 미정의가 안 생길 때까지 반복" 검색하고 "Using this option has a significant performance cost". 큰 프로젝트에서 수십 개 아카이브를 매번 재스캔하면 링크 시간이 늘어난다(이 작은 예제로는 측정하지 않았다). 또 순환 의존을 숨겨 설계 냄새를 덮는다. 실무에서는 순환인 몇 개만 그룹으로 묶거나, 반복 적기를 쓴다. 참고로 gcc 자신도 `-lgcc -lc -lgcc` 반복, newlib 의 nosys.specs 는 `--start-group -lgcc -lc_nano -lnosys --end-group` 그룹을 쓴다.

</details>

<details>
<summary>Q5. 두 라이브러리에 같은 함수가 있으면 링크 에러인가요?</summary>

아카이브 사이라면 **보통 에러가 아니다**. 먼저 그 심볼이 필요해진 시점에 탐색된 아카이브의 멤버가 들어가고, 그 뒤에는 미정의가 아니므로 다른 쪽 멤버를 꺼낼 이유가 없다. 실측: `-lcrc_ref -lcrc_fast` 면 ld64·bfd·lld 모두 ref, 순서를 바꾸면 fast, 경고 없음. 에러가 되는 경우: 다른 이유로 같은 멤버를 꺼냈는데 그 멤버에 충돌 심볼이 같이 있을 때(mono 실험: `duplicate symbol '_crc32'` / `multiple definition of 'crc32'`), `--whole-archive`/`-all_load` 로 다 넣을 때, `.o` 를 직접 두 개 링크할 때(TOOL-B05). 확인 도구: 맥 `-Wl,-why_load`, GNU `-Wl,-Map` 의 "Archive member included", `-Wl,-y,crc32`.

</details>

<details>
<summary>Q6. --whole-archive (맥 -force_load) 는 언제 필요하고 무엇이 위험한가요?</summary>

아무도 이름으로 참조하지 않지만 반드시 들어가야 하는 멤버가 있을 때: 생성자(`__attribute__((constructor))`)로 자기 등록하는 테스트·드라이버(연습 3, 실측 `P: 0 tests registered` → `3`), 링커 섹션으로 모으는 등록 테이블(TOOL-M03), `.a` 에 든 ISR/재지정 함수. 위험: 안 쓰는 멤버 전부 포함(크기), 중복 정의 에러, 그리고 `--no-whole-archive` 로 닫지 않으면 뒤따르는 libgcc 까지 통째로 들어가 multiple definition (실측). 대안: 특정 심볼만 `-u sym`(심볼이 global 일 때만 — 연습 3 의 멤버처럼 static 생성자뿐이면 인덱스에 아무것도 없어 `-u` 로 못 끈다: 맥 ranlib `has no symbols` 경고).

</details>

<details>
<summary>Q7. 펌웨어가 정상 부팅하는데 printf 출력이 없습니다. 링크 쪽에서 무엇을 의심하나요?</summary>

newlib 의 `printf → _write_r → _write` 체인에서 `_write` 가 누구 것인지. `-Wl,-y,_write` 로 정의한 파일을 보고, 빌드 로그에서 `_write is not implemented and will always fail` 경고를 찾는다. 재지정 `_write` 가 `.a` 안에 있고 앱이 직접 부르지 않으면, 그 아카이브를 지날 때 `_write` 는 아직 미정의가 아니라 안 끌려오고, gcc 가 뒤에 붙인 그룹의 libnosys 스텁(`errno=ENOSYS(88)`, `return -1`)이 들어간다. 해결: `-Wl,-u,_write`, 재지정 파일을 오브젝트로 링크, 또는 `--whole-archive`. 맵의 `./libuart.a(uart_retarget.o)  (_write)` 로 확인. (세미호스팅/SWO/RTT 같은 다른 출력 경로는 TOOL-A04.)

</details>

<details>
<summary>Q8. 링크 에러 메시지에서 "누가" 참조했는지 어떻게 읽나요? ld64 와 GNU ld 차이는?</summary>

GNU: `./libbar.a(bar_a.o): in function 'bar_mid': bar_a.c:(.text+0x8): undefined reference to 'foo_leaf'` — 참조한 멤버, 함수, 섹션 오프셋, 심볼. 맥 ld64: `"_bar_mid", referenced from: _foo_entry in libfoo.a[2](foo_a.o)` 형식(`-lfoo` 만 줬을 때 실측, `order.txt` 첫 블록) — `[2]` 는 아카이브 안 멤버 번호, C 심볼에 `_` 접두사(Mach-O). 이걸 보고 "정의는 어느 라이브러리에 있나" (`nm -A lib*.a | grep ' T foo_leaf'`) → "그 라이브러리가 참조자보다 오른쪽에 있나" 를 확인하면 된다.

</details>

## 9. 추가 연습

`practice.link` 는 solution 과 Part A·C 의 **다른 정답** + 연습 3 의 두 줄이다. `bash check.sh practice` 하나로 전부 검증한다.

### 연습 1 — Part A 를 그룹으로

`A_GNU` 를 `--start-group` 으로 바꿔라. 맵은 달라지는가?

<details>
<summary>답 보기</summary>

`A_GNU="main.o -L. -Wl,--start-group -lfoo -lbar -Wl,--end-group"`. bfd 맵의 "Archive member included" 는 `-lfoo -lbar -lfoo` 와 **같은 세 줄**이었다(`order.txt`) — 그룹은 "몇 번 더 훑느냐" 만 바꾸지 무엇을 넣는지는 바꾸지 않는다. lld 에서는 그룹이 무시되지만(`--help` 문구) 링크는 원래 됐다. 맥에서 `-Wl,--start-group` 은 ld64 옵션이 아니므로 이 줄은 GNU 전용이다 (맥은 필요 없음).

</details>

### 연습 2 — Part C 를 --whole-archive 로

`C_GNU` 를 `-u` 없이 고쳐라. 이번엔 `--whole-archive` 를 써도 된다 — 왜 여기서는 괜찮고 Part A 에서는 감점인가?

<details>
<summary>답 보기</summary>

`C_GNU="fw_main.o -L. -Wl,--whole-archive -luart -Wl,--no-whole-archive"`. `-y` 결과는 `./libuart.a(uart_retarget.o): definition of _write` 가 참조보다 **먼저** 찍혔다 (`-u` 와 같은 모양). libuart 의 두 멤버는 어차피 둘 다 필요하므로 낭비가 없다 — Part A 는 unused 멤버가 있어서 낭비였다. 다만 벤더가 나중에 libuart 에 SPI 드라이버를 추가하면 그것도 들어온다(크기 증가). `--no-whole-archive` 를 빼면 뒤의 libgcc/libc_nano 까지 영향받는다(실수 4).

</details>

### 연습 3 — 자기 등록 테스트가 사라진다

`src/p/` 의 `test_uart.c`, `test_crc.c`, `test_ring.c` 는 각자 `__attribute__((constructor)) static void reg_*(void)` 로 `test_register()` 를 부른다. `libtests.a` 로 묶어 `main_p.o -L. -ltests` 로 링크하면 결과는? `P_MAC`, `P_GNU` 를 채워 3개 모두 등록되게 하라.

<details>
<summary>답 보기</summary>

실측 (`register.txt`): 맥·bfd·lld 모두 `P: 0 tests registered, 0 passed` (exit 1). 멤버에 **global 심볼이 하나도 없다** — 맥 ranlib 이 `warning: 'libtests.a(test_uart.o)' has no symbols` 를 내고, GNU `nm -s` 에도 인덱스 항목이 하나도 안 나온다. 미정의 심볼로 꺼낼 방법이 없으니 아무것도 안 들어간다. 해법: `P_MAC="main_p.o -Wl,-force_load,./libtests.a"` (또는 `-Wl,-all_load` — 모든 아카이브라 과함), `P_GNU="main_p.o -L. -Wl,--whole-archive -ltests -Wl,--no-whole-archive"` → `P: 3 tests registered, 3 passed`. `-Wl,-u,_reg_uart` 는 `reg_uart` 가 static 이라 맥에서 (`register.txt`) `Undefined symbols ... "_reg_uart", referenced from: <initial-undefines>` 로 실패했다. GoogleTest·Zephyr 의 `SYS_INIT`·Linux `initcall` 같은 "자기 등록" 패턴은 모두 이 문제를 가진다 — 섹션 기반 등록은 TOOL-M03.

</details>

### 연습 4 — 의존 순서를 자동으로

라이브러리 의존(`app→libapp`, `libapp→libhal`, `libhal→libutil`, `libapp→libutil`)이 주어졌을 때 GNU 링크 순서를 기계적으로 만드는 방법은? 순환이 있으면?

<details>
<summary>답 보기</summary>

위상 정렬. 맥 `tsort` 실측: `printf 'fw_main.o libapp\nlibapp libhal\nlibhal libutil\nlibapp libutil\n' | tsort` → `fw_main.o libapp libhal libutil` — 그대로 `-lapp -lhal -lutil`. Part A 의 라이브러리 그래프를 넣으면 `tsort: cycle in data` 와 함께 순환 멤버(`libfoo`, `libbar`)를 알려 준다 → 반복 적기/그룹 대상. 멤버 단위(`lorder` 의 출력)로는 순환이 없었다는 점이 "라이브러리 경계를 다시 나누라" 는 힌트다. CMake 는 `target_link_libraries` 의존 그래프로 이 순서를 자동 생성하고, 순환이면 `LINK_INTERFACE_MULTIPLICITY` 만큼 반복해 적는다 (TOOL-M04 — 동작 세부는 확인 권장).

</details>

### 한 번에 검증

<!-- code: practice.link diff=solution.link -->

```bash
bash check.sh practice     # 25/25 passed (Part A·B·C 다른 정답 + P ld64/bfd/lld)
```

```text
PASS P ld64 self-registering tests: 'P: 3 tests registered, 3 passed'
...                                   (solution 과 같은 22줄 PASS)
PASS P bfd self-registering tests: 'P: 3 tests registered, 3 passed'
PASS P lld self-registering tests: 'P: 3 tests registered, 3 passed'
25/25 passed
```

## 10. 핵심 정리

- `.a` = `ar` 로 묶은 `.o` + 심볼 인덱스(`ar rcs`, `ranlib`; 맵 `__.SYMDEF SORTED` / GNU `/`). 링커는 **미정의 심볼을 채우는 멤버(.o) 단위**로만 꺼낸다 — 안 쓰는 멤버는 안 들어간다.
- GNU ld.bfd: 왼쪽→오른쪽 한 번, 지나간 아카이브는 다시 안 봄(한 아카이브 안에서는 반복). 순서 = 쓰는 쪽 왼쪽, 주는 쪽 오른쪽. 순환은 `-lfoo -lbar -lfoo` 또는 `--start-group … --end-group`(비용 있음). 오브젝트를 라이브러리 뒤에 두면 실패.
- ld64(“continually search”)와 ld.lld(lazy 심볼)는 순서 무관 → 맥에서 되고 CI(GNU)에서 깨진다. lld `--warn-backrefs` 는 유용하지만 놓치는 경우가 있다(`-lbar -lfoo -lbar`).
- 아카이브 사이 중복 심볼은 에러가 아니라 **먼저 탐색된 쪽이 조용히 이김**(세 링커 모두). 같은 멤버에 붙은 다른 심볼 때문에 꺼내지면 multiple definition.
- 증거: 맥 `-Wl,-why_load`, `-Wl,-map,x.map` (`-t` 는 읽은 파일까지 찍음) / GNU `-Wl,-Map=x.map` "Archive member included … (symbol)", `-Wl,-y,sym`, `--trace` / lld `--why-extract=-`.
- 아무도 이름으로 안 부르는 멤버(newlib `_write` 재지정, ISR, 생성자 자기 등록)는 `.a` 에서 안 끌려온다. `-Wl,-u,sym` 또는 `--whole-archive … --no-whole-archive`(맥 `-force_load`), 아니면 오브젝트로 링크. nosys 스텁이면 `_write is not implemented and will always fail` 경고가 유일한 단서.

## 참고자료

- GNU ld manual — Options: `-( archives -)` / `--start-group`, `--whole-archive`, `-u`, `-y`, `-t`, `-Map`: <https://sourceware.org/binutils/docs/ld/Options.html>
- GNU binutils `ar` (D 결정적 모드, `s`/`S` 인덱스): <https://sourceware.org/binutils/docs/binutils/ar.html>
- Apple `man ld` — Libraries 절 ("continually search a static library"), `-all_load`, `-force_load`, `-why_load`, `-map`, `-t` (이 맥의 로컬 man 페이지)
- LLD 문서 — archive 처리와 `--warn-backrefs`: <https://lld.llvm.org/ELF/warn_backrefs.html>, <https://lld.llvm.org/>
- newlib/libgloss — `nosys.specs`, `nano.specs`, syscall stubs (`libnosys`), `_write_r`: <https://sourceware.org/newlib/>
- John R. Levine, *Linkers and Loaders* — 6장 Libraries (아카이브 형식, 검색 순서)
- Ian Lance Taylor, "Linkers" 블로그 시리즈 (archive 처리, gold): <https://www.airs.com/blog/archives/38>
- 관련 레슨: TOOL-B04 (오브젝트 파일·nm 글자), TOOL-B05 (링크 에러·weak/alias), TOOL-B06 (size), TOOL-B08 (Make 의 LDLIBS 순서), TOOL-M03 (섹션 기반 자동 등록), TOOL-M04 (CMake 링크 의존), TOOL-M06 (공유 라이브러리), TOOL-M13 (gc-sections), TOOL-A04 (세미호스팅·SWO·RTT), TOOL-A06 (재현 가능한 빌드), BM-B08 (Cortex-M 부트·벡터 테이블)
