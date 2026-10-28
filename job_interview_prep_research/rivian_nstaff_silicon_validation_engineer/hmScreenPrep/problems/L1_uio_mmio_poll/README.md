# L1 · Userspace Register Access on Zynq PetaLinux (/dev/mem · UIO)

> **ID**: L1 · **Priority**: P0 · **Pattern**: mmap page-align + volatile MMIO + monotonic-deadline poll + UIO IRQ handshake · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh L1` / `./run.sh L1 sol`

## 0. 문제 한국어로 먼저 보기

> 영어 원문(§1)을 읽기 전에 **무슨 상황이고 뭘 만들어야 하는지**만 빠르게 잡는 요약입니다. 풀이 힌트는 없습니다.

**한 줄 요약**: Zynq UltraScale+ PetaLinux에서 **userspace validation 툴**이 PL IP 블록의 레지스터를 `/dev/mem` mmap으로 읽고 쓰고, 인터럽트를 **UIO**(`/dev/uioN`)로 기다리는 작은 라이브러리를 짭니다.

**상황**
- PL에 올린 IP 블록 `pgen`이 PS의 **M_AXI_HPM0_FPD** 포트 뒤, 물리 주소 **0xA001_0000**에 있습니다. 커널 드라이버는 없습니다.
- 레지스터 접근은 `/dev/mem`을 열고 물리 주소 구간을 `mmap()`해서 합니다. 인터럽트는 device tree에서 `generic-uio`로 묶인 `/dev/uio0` fd로 받습니다.
- UIO 규칙
  - fd에 **4바이트 값 1을 write** → 인터럽트 다시 켜기(unmask)
  - 인터럽트가 오면 fd가 readable → **4바이트 read** = 지금까지의 **인터럽트 총 횟수**
- `pgen` 레지스터: `CTRL`(0x00, b0 START) · `STATUS`(0x04, b0 BUSY, b1 DONE, b15:8 RESULT) · `ID`(0x08, `0xC0DE0101`) · `SCRATCH`(0x0C)
- 하드웨어 없이 macOS에서 테스트합니다. `/dev/mem` 대신 **sparse temp 파일**을 쓰고(물리 주소 P = 파일 offset P), 같은 파일을 mmap한 **thread가 PL IP 역할**을 합니다. UIO는 **socketpair**(양방향 pipe)로 흉내 냅니다.

**구현할 것**

| 함수 | 하는 일 | 규칙 |
|---|---|---|
| `regmap_open(m, path, phys, len)` | `[phys, phys+len)` 구간을 mmap | `O_RDWR \| O_SYNC`로 open. 페이지 크기는 `sysconf(_SC_PAGESIZE)`. `base`는 레지스터 0번 주소. `m`/`path` NULL, `len == 0`, `phys`나 `len`이 4의 배수 아님, `phys + len` overflow → `-EINVAL`. 그 밖의 실패는 `-errno`. **실패하면 `m`은 닫힌 상태**(`fd = -1`, `map = NULL`, `base = NULL`) |
| `regmap_close(m)` | munmap + close | 닫힌 map에 불러도, 두 번 불러도 안전 |
| `reg_rd32(m, off)` / `reg_wr32(m, off, v)` | 32-bit 레지스터 읽기/쓰기 | `off`는 4의 배수이고 `off + 4 <= len`이어야 함. 아니면 **버스 접근 없이** read는 `REG_BAD`(0xFFFFFFFF), write는 무시 |
| `reg_poll(m, off, mask, want, timeout_ms)` | `(reg & mask) == want`가 될 때까지 대기 | `CLOCK_MONOTONIC` 기준. 성공 0, 시간 초과 `-ETIMEDOUT`. `off`가 잘못됐거나 `want`에 `mask` 밖 비트가 있으면 `-EINVAL` |
| `uio_wait_irq(fd, timeout_ms, count)` | UIO 인터럽트 1개 기다리기 | 1 write → `poll()` → 4바이트 read. `*count`는 **in/out**(들어올 때 마지막으로 본 횟수, 나갈 때 새 횟수). 반환값 = **놓친 인터럽트 수**(정상 = 0), `-ETIMEDOUT`, `-EIO`(짧은 read/write, EOF, 횟수가 안 늘어남), `-EINVAL`(`count` NULL), 그 밖은 `-errno` |

**예시를 말로 풀면**
- `regmap_open(0xA001_0000, 0x1000)` → `ID` 읽으면 `0xC0DE0101`. `SCRATCH`에 `0xDEADBEEF`를 쓰면 device 쪽에서 그 값이 보이고, device가 바꾼 값도 다음 read에서 바로 보입니다.
- `regmap_open(0xA001_0F00, 0x200)`(페이지 중간에서 시작): 4 KiB 페이지면 `0xA001_0000`부터 **2페이지(0x2000)** 를 mmap하고 `base = map + 0xF00`. offset `0x100` = 물리 `0xA001_1000`(페이지 경계)도 맞게 읽혀야 합니다.
- 같은 map에서 `reg_rd32(0x200)`은 mmap 범위 안이지만 `len` 밖이므로 `0xFFFFFFFF`, `reg_wr32(0x200, …)`는 아무것도 안 씁니다.
- `CTRL.START`를 쓰면 IP thread가 20 ms 뒤 `STATUS.DONE`과 `RESULT = 0x5A`를 세웁니다 → `reg_poll(STATUS, DONE, DONE, 2000)`은 0. 절대 안 서는 비트를 50 ms 기다리면 `-ETIMEDOUT`.
- UIO: 마지막으로 본 횟수 1, 이번에 읽은 횟수 4 → 반환 **2**(두 개 놓침), `*count = 4`. 마지막 `0xFFFFFFFF` → 새 `1`은 wrap이라 반환 1.

**지켜야 할 조건**
- C11 + POSIX, `malloc` 금지. 함수는 errno를 남기지 말고 **`-errno`를 반환**.
- 레지스터 접근은 반드시 **한 번의 32-bit volatile load/store**.
- 테스트는 thread와 socketpair를 쓰지만 타이밍은 넉넉하게(수십 ms ~ 2 s) 잡혀 있습니다.

## 1. Problem

You are writing the low-level layer of a **userspace validation tool** on a **Zynq UltraScale+ MPSoC running PetaLinux**. A PL IP block (`pgen`) sits behind **M_AXI_HPM0_FPD** at physical address **0xA001_0000**; there is no kernel driver for it. The tool reaches the registers by `mmap()`ing `/dev/mem`, and receives the IP's interrupt through a **UIO** device (`/dev/uio0`, bound with `compatible = "generic-uio"`).

UIO semantics: writing a 4-byte `1` to the fd re-enables (unmasks) the interrupt; when the interrupt fires, the fd becomes readable and a 4-byte `read()` returns the **total interrupt count**.

```c
#define REG_BAD 0xFFFFFFFFu

struct regmap {
    int               fd;        /* -1 when closed */
    void             *map;       /* page-aligned mmap() result, NULL when closed */
    size_t            map_len;   /* bytes mapped, multiple of the page size */
    volatile uint8_t *base;      /* register offset 0 (= map + in-page offset) */
    size_t            len;       /* register window in bytes */
    uint64_t          phys;      /* physical address of offset 0 */
};

int      regmap_open(struct regmap *m, const char *path, uint64_t phys, size_t len);
void     regmap_close(struct regmap *m);
uint32_t reg_rd32(const struct regmap *m, uint32_t off);
void     reg_wr32(const struct regmap *m, uint32_t off, uint32_t val);
int      reg_poll(const struct regmap *m, uint32_t off, uint32_t mask, uint32_t want,
                  unsigned timeout_ms);
int      uio_wait_irq(int fd, unsigned timeout_ms, uint32_t *count);
```

Requirements:

- **`regmap_open`**: `m` or `path` NULL, `len == 0`, `phys` or `len` not 4-byte aligned, or `phys + len` overflowing → `-EINVAL`. Open with `O_RDWR | O_SYNC`, `mmap()` the **page-aligned window** that covers `[phys, phys + len)` (page size from `sysconf(_SC_PAGESIZE)`), and set `base` to the register at `phys`. Return `0` or `-errno`. On **any** failure leave `m` closed (`fd = -1`, `map = NULL`, `base = NULL`) and leak nothing.
- **`regmap_close`**: unmap and close; safe on a closed regmap and when called twice.
- **`reg_rd32` / `reg_wr32`**: one 32-bit volatile access at `base + off`. Contract: `off` must be 4-byte aligned and `off + 4 <= len`. A bad offset (or a closed / NULL map) does **no bus access**: read returns `REG_BAD`, write is dropped.
- **`reg_poll`**: wait until `(reg & mask) == want`, using a `CLOCK_MONOTONIC` deadline. Return `0` or `-ETIMEDOUT`. The register must be read **at least once after the deadline** before giving up. Bad offset, or `want` has bits outside `mask` → `-EINVAL`.
- **`uio_wait_irq`**: write a 4-byte `1`, `poll()` for `POLLIN` up to `timeout_ms`, read the 4-byte count. `*count` is **in/out**: in = the last count the caller saw, out = the new count. Return the number of **missed** interrupts (`new - last - 1`, so `0` means exactly one new interrupt), `-ETIMEDOUT`, `-EIO` (short read/write, EOF, or the count did not advance), `-EINVAL` (`count` NULL), or `-errno` from `write`/`poll`/`read`. Retry on `EINTR`.

The harness runs on macOS/Linux without hardware: "/dev/mem" is a sparse temp file (physical address P = file offset P), a thread maps the same pages and plays the PL IP, and UIO is a `socketpair()` whose device end reads your unmask word and writes counts.

### Example

```
regmap_open(&m, devmem, 0xA0010000, 0x1000)       -> 0, base == map, map_len = 1 page
reg_rd32(&m, 0x08)                                -> 0xC0DE0101       (ID)

regmap_open(&m, devmem, 0xA0010F00, 0x200)        -> 0   (4 KiB pages)
  page_base = 0xA0010000, base = map + 0xF00, map_len = 0x2000
  reg_rd32(&m, 0x100)  -> value at phys 0xA0011000 (crosses the page boundary)
  reg_rd32(&m, 0x200)  -> 0xFFFFFFFF  (mapped slack, but off + 4 > len)
  reg_rd32(&m, 0x102)  -> 0xFFFFFFFF  (misaligned)

reg_wr32(CTRL, START); reg_poll(STATUS, DONE, DONE, 2000) -> 0 after ~20 ms, RESULT = 0x5A
reg_poll(STATUS, DONE, DONE, 50)  on a bit that never sets -> -ETIMEDOUT after >= 50 ms

last = 1; device count = 4  -> uio_wait_irq(...) = 2, last = 4      (two missed)
last = 0xFFFFFFFF; count 1  -> 1                                     (wrap-safe)
last = 4; count 4           -> -EIO                                  (count did not advance)
nothing fires in 50 ms      -> -ETIMEDOUT, last unchanged
```

### Constraints

- C11 + POSIX (`open`, `mmap`, `poll`, `clock_gettime`). No malloc. Return `-errno`, never `-1`.
- Every register access is exactly one 32-bit volatile load or store.
- `off_t` must be 64-bit (`_FILE_OFFSET_BITS=64` on 32-bit ARM), checked by a `_Static_assert`.

### Clarifying questions to ask (first 3 minutes)

1. Is the IP's address range 4 KiB-aligned in the Vivado address editor, or can the tool be asked to map an arbitrary sub-range?
2. Does the AXI-Lite slave accept only 32-bit accesses? Any read-to-clear or FIFO registers (so a stray read is harmful)?
3. Is the interrupt level or edge, and who clears the source — the tool via a W1C status register?
4. Is `CONFIG_STRICT_DEVMEM` / `CONFIG_IO_STRICT_DEVMEM` set on this image, and does a kernel driver already claim this range?
5. Can more than one process open the UIO device? (decides how much a count jump means)
6. What should happen if the PL is not programmed or its clock is off — fail fast, or is a hang acceptable on a bring-up board?

## 2. Your workspace

`./run.sh L1`로 바로 테스트하세요. 테스트 8묶음(CHECK 51개)이 있고, 파일 아래쪽 harness(PL IP thread, UIO socketpair)는 건드리지 않습니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — mmap offset은 페이지 배수여야 함</summary>

`mmap()`의 offset은 페이지 크기의 배수여야 합니다(아니면 `EINVAL`). 그래서 `page_base = phys & ~(pg - 1)`부터 매핑하고, `delta = phys - page_base`만큼 `base`를 밀어 줍니다. 길이는 `len`이 아니라 `delta + len`을 페이지 단위로 올림합니다. 끝을 빼먹으면 페이지 경계를 넘는 레지스터가 잘립니다.

</details>

<details>
<summary>Hint 2 — poll 루프에서 "시간 먼저, 읽기 나중"</summary>

루프마다 **먼저 시간을 보고**(`late = now >= deadline`), 그다음 레지스터를 읽고, 맞으면 0, 아니고 `late`였으면 `-ETIMEDOUT`. 이렇게 하면 마지막 read가 항상 deadline 뒤에 일어납니다. 반대 순서면 스케줄러가 툴을 오래 재운 경우 "실제로는 DONE인데 timeout"이 나옵니다.

</details>

<details>
<summary>Hint 3 — UIO count는 unsigned 차이로</summary>

`delta = new - last`를 `uint32_t`로 계산하면 2^32 wrap에서도 맞습니다. `delta == 1`이면 정상(반환 0), `delta > 1`이면 `delta - 1`개를 놓친 것, `delta == 0`이면 이상한 상태(`-EIO`). unmask write는 **poll 전에** 합니다. 테스트 7번의 device thread는 unmask를 받아야만 인터럽트를 냅니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 페이지 정렬된 창 + 안쪽 offset

`/dev/mem`의 파일 offset이 곧 물리 주소입니다. mmap은 페이지 단위로만 되므로 "원하는 구간을 덮는 가장 작은 페이지 묶음"을 매핑하고, 그 안에서 레지스터 0번 위치를 기억합니다.

<div class="chart" data-src="chart1.svg" data-caption="phys 0xA001_0F00, len 0x200: 4 KiB 페이지 기준 0xA001_0000부터 2페이지를 매핑하고 base = map + 0xF00. 오른쪽 노란 영역은 매핑은 됐지만 len 밖이라 접근 금지(REG_BAD). macOS는 16 KiB 페이지라 map_len만 0x4000으로 바뀜"></div>

- `munmap()`에는 `base`가 아니라 **`map`(페이지 정렬된 원래 주소)** 과 `map_len`을 넘깁니다. 그래서 구조체에 둘 다 둡니다.
- 경계 검사는 `map_len`이 아니라 **`len`** 기준입니다. slack 영역은 다른 IP의 레지스터일 수 있습니다.

### 4.2 UIO 인터럽트 핸드셰이크

<div class="chart" data-src="chart2.svg" data-caption="uio_pdrv_genirq는 IRQ가 오면 커널 핸들러에서 바로 mask하고 count를 올린 뒤 깨웁니다. 그래서 툴이 1을 써야 다음 IRQ가 옵니다. IP status를 지우기(⑥) 전에 unmask하면 level IRQ가 바로 다시 터집니다"></div>

- **①을 ②보다 먼저**: unmask 없이 poll하면 영원히(또는 timeout까지) 기다립니다. 테스트 7번 device thread가 이 순서를 확인합니다.
- **⑥은 호출자 책임**: `uio_wait_irq()`가 돌아온 뒤 IP의 W1C status를 지우고 다음 호출에서 unmask합니다.
- count는 **장치 전체 누적값**입니다. 차이로 놓친 횟수를 계산합니다.

### 4.3 손으로 따라가기 (trace)

`regmap_open(path, 0xA001_0F00, 0x200)` (테스트 2번):

| 단계 | 4 KiB 페이지 (Zynq 실제) | 16 KiB 페이지 (macOS harness) |
|---|---|---|
| `pmask = pg - 1` | 0xFFF | 0x3FFF |
| `page_base = phys & ~pmask` | 0xA001_0000 | 0xA001_0000 |
| `delta = phys - page_base` | 0xF00 | 0xF00 |
| `map_len = (delta + len + pmask) & ~pmask` | (0x1100 + 0xFFF) & ~0xFFF = **0x2000** | (0x1100 + 0x3FFF) & ~0x3FFF = **0x4000** |
| `mmap(NULL, map_len, RW, MAP_SHARED, fd, page_base)` | 2 pages | 1 page |
| `base` | `map + 0xF00` | `map + 0xF00` |

경계 검사 (`len = 0x200`):

| 호출 | `off & 3` | `off + 4` vs `len` | 결과 |
|---|---|---|---|
| `reg_rd32(0x100)` | 0 | 0x104 ≤ 0x200 | 물리 0xA001_1000의 값 `0x600DCAFE` |
| `reg_wr32(0x1FC, 0xA1B2C3D4)` | 0 | 0x200 ≤ 0x200 | 써짐 (마지막 레지스터) |
| `reg_rd32(0x200)` | 0 | 0x204 > 0x200 | `REG_BAD`, slack의 `0x5A5A5A5A`는 안 읽힘 |
| `reg_rd32(0x102)` | 2 | – | `REG_BAD` |
| `reg_wr32(0xFFFFFFFC, 0)` | 0 | `size_t`로 0x1_0000_0000 > 0x200 | 무시 (32-bit였으면 0으로 wrap되어 통과) |

`uio_wait_irq` (테스트 6–7번):

| 들어올 때 `*count` | device가 쓴 count | `delta = new - last` (u32) | 반환 | 나갈 때 `*count` |
|---|---|---|---|---|
| 0 | 1 | 1 | **0** | 1 |
| 1 | 4 | 3 | **2** (2개 놓침) | 4 |
| 0xFFFFFFFF | 1 | 2 | **1** | 1 |
| 4 | 4 | 0 | **-EIO** | 4 |
| 4 | (없음, 50 ms) | – | **-ETIMEDOUT** | 4 (그대로) |
| 4 | 5 (unmask 받고 20 ms 뒤) | 1 | **0** | 5 |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `regmap_open`은 맨 처음 `regmap_reset(m)`으로 닫힌 상태를 만들어 둡니다. 이후 어느 경로로 실패해도 `fd = -1`이 보장되고, mmap 실패 시에는 `errno`를 먼저 저장한 뒤 `close(fd)`합니다(`close`가 errno를 덮어쓸 수 있음).
- overflow 검사 두 개: `len > UINT64_MAX - phys`(물리 주소 wrap), `len > SIZE_MAX - delta - pmask`(올림 계산 wrap). `page_base > INT64_MAX`는 signed `off_t`에 안 들어가므로 `-EOVERFLOW`.
- `reg_ok`의 `(size_t)off + 4u <= m->len`: `off + 4`를 `uint32_t`로 계산하면 `0xFFFFFFFC + 4 = 0`이 되어 검사를 통과합니다.
- 접근은 `*(volatile uint32_t *)(m->base + off)` 한 줄. `base`가 `volatile uint8_t *`이고 `off`가 4의 배수라 정렬이 맞습니다.
- `reg_poll`은 20 µs `nanosleep`으로 CPU를 양보합니다. bring-up 툴에서는 충분하고, µs 단위 지연 측정이 필요하면 sleep 없이 spin합니다.
- `uio_wait_irq`는 남은 시간을 매번 다시 계산해 `EINTR` 뒤에도 전체 timeout을 지킵니다. `POLLIN` 없이 `POLLHUP/POLLERR`만 오면 `-EIO`.

### 4.5 복잡도

- `regmap_open/close`: syscall 2–3개, O(1). `reg_rd32/wr32`: 버스 접근 1회.
- `reg_poll`: 읽기 횟수 ≈ timeout / (20 µs + syscall 시간). 50 ms면 수백~수천 번. 메모리 O(1).
- `uio_wait_irq`: syscall 3개(write, poll, read), 기다리는 동안 CPU 0.

---

## 5. 임베디드 관점으로 말할 것

- **PS ↔ PL 경로**: ZynqMP에서 PS가 PL 레지스터를 읽는 길은 **M_AXI_HPM0_FPD**(0xA000_0000 – 0xAFFF_FFFF, 256 MB), HPM1_FPD(0xB000_0000 –), HPM0_LPD(0x8000_0000 –)입니다. Zynq-7000은 **M_AXI_GP0**(0x4000_0000 – 0x7FFF_FFFF)라서 Vivado 기본 주소가 **0x43C0_0000**입니다. 주소는 Vivado Address Editor → XSA → PetaLinux가 만든 `pl.dtsi`에 그대로 들어갑니다.
- **O_SYNC와 memory type**: arm64 커널의 `/dev/mem` mmap은 RAM이 아닌 주소(PL 레지스터)를 **항상 Device memory(Device-nGnRnE, uncached)** 로 매핑합니다. `O_SYNC`가 실제로 차이를 내는 곳은 **RAM**(예: PL DMA와 공유하는 reserved DDR) 쪽으로, 이때 cached 대신 Normal-NC(write-combine)가 됩니다. 그래도 항상 `O_SYNC`를 붙이는 게 이식성 있는 습관입니다(x86 등은 이걸로 UC를 결정).
- **왜 volatile이고 memcpy가 아닌가**: 컴파일러는 일반 포인터 read를 루프 밖으로 빼거나 합치거나 없앱니다(`-O2`에서 poll 무한 루프). `memcpy`는 크기·정렬에 따라 바이트 단위, 64/128-bit(`ldp q`), 비정렬 접근을 씁니다. AXI-Lite slave는 32-bit 접근만 받고, Device memory에 비정렬 접근을 하면 **alignment fault(SIGBUS)** 입니다. glibc `memset`은 `DC ZVA`를 써서 Device memory에서 바로 fault 납니다. read-to-clear / FIFO 레지스터는 한 번 더 읽는 것 자체가 버그입니다.
- **volatile은 순서 보장이 아님**: 같은 장치에 대한 Device memory 접근끼리는 순서가 지켜지지만, "DDR 버퍼 쓰기 → PL 도어벨 레지스터"처럼 Normal memory와 섞이면 `__sync_synchronize()`(arm64 `dmb ish`) 또는 `dsb st`가 필요합니다 → [V3](../V3_dma_desc_ring/index.html)의 `dma_wmb()`와 같은 이야기.
- **device tree + generic-uio**: PetaLinux `system-user.dtsi`에서 IP 노드를 덮어씁니다.

```dts
&pgen_0 {                                  /* node generated in pl.dtsi */
    compatible = "generic-uio";
    interrupt-parent = <&gic>;
    interrupts = <0 89 4>;                 /* pl_ps_irq0[0] = SPI 89, level-high */
};
/ { chosen { bootargs = "earlycon console=ttyPS0,115200 clk_ignore_unused uio_pdrv_genirq.of_id=generic-uio"; }; };
```

`uio_pdrv_genirq`는 기본으로 아무 compatible에도 안 붙기 때문에 `of_id=generic-uio` 모듈 파라미터가 필요합니다. 확인은 `/sys/class/uio/uio0/name`, `maps/map0/addr`, `maps/map0/size`. UIO fd 자체를 mmap하면(`offset = map 번호 × page size`) `/dev/mem` 없이도 레지스터에 접근됩니다.

- **/dev/mem vs UIO vs 커널 드라이버**

| | `/dev/mem` | UIO (`generic-uio`) | 커널 드라이버 |
|---|---|---|---|
| 준비 | 없음 (root만) | DT 노드 + bootargs | 드라이버 코드 + DT |
| 범위 | 아무 물리 주소 (오타 = 다른 IP 망가뜨림) | DT의 `reg` 범위만 | 드라이버가 정함 |
| 인터럽트 | 못 받음 | count + unmask (fd) | 전부 (threaded IRQ, 지연 최소) |
| DMA / 캐시 | 직접 못 함 | 제한적 (`uio_dmem_genirq`, `u-dma-buf`) | `dma_alloc_coherent`, IOMMU |
| 보안 / 격리 | `STRICT_DEVMEM`에 막힐 수 있음 | 파일 권한으로 제어 | 커널이 관리 |
| 쓰는 곳 | bring-up 첫날, `devmem` peek/poke | validation 툴, 빠른 반복 | 제품, 성능, 전원 관리 |

- **CONFIG_STRICT_DEVMEM**: 켜져 있으면 `/dev/mem`으로 **RAM 영역은 접근 불가**(`mmap` → `EPERM`), MMIO는 허용. `CONFIG_IO_STRICT_DEVMEM`까지 켜면 **드라이버가 이미 잡은 I/O 영역도 불가**입니다. 그래서 같은 IP에 UIO와 `/dev/mem`을 같이 쓰면 이미지 설정에 따라 막힐 수 있습니다.
- **PL 클럭이 꺼졌을 때**: PL이 프로그램 안 됐거나, `pl0_ref` 클럭이 꺼졌거나, PL reset이 걸린 상태에서 0xA000_0000을 읽으면 AXI 응답이 안 와서 **CPU가 멈추거나**, 인터커넥트가 SLVERR/DECERR를 주면 **SError**(비동기 external abort → 보통 커널 panic) 또는 동기 abort면 프로세스가 **SIGBUS**를 받습니다. 흔한 원인은 PL 클럭을 잡는 드라이버가 없어서 부팅 끝에 `clk_disable_unused`가 꺼 버리는 것(→ `clk_ignore_unused` 또는 DT에 `clocks = <&zynqmp_clk 71>`). 툴은 시작할 때 `/sys/class/fpga_manager/fpga0/state == operating`을 확인하고 **ID 레지스터를 먼저 읽어** 기대값과 비교합니다.

## 5.1 면접관 Iris Lee(SW·시스템 출신 Principal)가 파고들 포인트

- **"Why /dev/mem here, and when would you stop using it?"** → bring-up 첫날과 레지스터 peek/poke에는 `/dev/mem`이 가장 빠릅니다. 인터럽트가 필요해지면 UIO, DMA·전원 관리·여러 프로세스 공유가 필요해지면 커널 드라이버로 옮깁니다. 이 regmap 층을 두면 위의 테스트 코드는 그대로입니다. *"I start with /dev/mem for day-one peek and poke, move to UIO once I need interrupts, and only write a kernel driver when DMA, power management or sharing make userspace the wrong place — the test code above the regmap layer does not change."*
- **"Your tool read 0xA000_0000 and the board hung. Walk me through it."** → bitstream 로드 여부(`fpga_manager` state), PL 클럭(`clk_disable_unused`), PL reset, Address Editor와 DT 주소 불일치 순으로 봅니다. JTAG(XSDB)로 같은 주소를 읽어 CPU/인터커넥트 문제와 IP 문제를 나눕니다. *"First I check the bitstream is loaded and the PL clock was not gated by clk_disable_unused, then I read the same address over JTAG to split an interconnect problem from an IP problem."*
- **"How do you know you didn't miss an interrupt?"** → UIO count는 누적값이라 차이로 압니다. 다만 `uio_pdrv_genirq`는 IRQ마다 mask하므로, 정상 흐름에서는 1씩만 늘어납니다. 1보다 크게 뛰면 timeout 뒤에 늦게 온 IRQ, 다른 프로세스가 unmask, 또는 mask를 안 하는 UIO 드라이버입니다. 검증에서는 IP 쪽 **이벤트 카운터 레지스터**와 UIO count를 비교하는 게 확실합니다. *"The count is cumulative, so a jump larger than one is a missed event; I cross-check it against an event counter register in the IP, which is the ground truth."*
- **"Would the same code work for a PCIe-attached accelerator?"** → 레지스터는 `/sys/bus/pci/devices/<BDF>/resource0`을 mmap하면 같은 regmap으로 됩니다(offset 0 = BAR 시작). 인터럽트는 UIO 대신 **VFIO**(MSI-X → eventfd, IOMMU 격리)를 씁니다. 링크가 끊기면 read가 `0xFFFFFFFF`로 오는데, 그래서 `REG_BAD`를 같은 값으로 골랐습니다. *"Same regmap over the BAR's resource0 file, VFIO with eventfds instead of UIO for MSI-X, and an all-ones read is my cue that the link dropped."*
- **"Is polling every 20 µs OK? What about timing accuracy?"** → bring-up 툴에서는 CPU 사용량보다 단순함이 중요합니다. 지연을 µs 단위로 재려면 sleep 없이 spin, `SCHED_FIFO` + `mlockall` + `isolcpus`, 또는 IP 안의 타임스탬프 카운터를 씁니다. `CLOCK_MONOTONIC`이라 NTP/`date`에 안 흔들립니다. *"For bring-up a 20 µs poll is fine; for latency measurements I spin on an isolated SCHED_FIFO core, or better, read a timestamp counter inside the IP."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| What does `O_SYNC` actually change on arm64? | PL 레지스터 같은 non-RAM 주소는 `O_SYNC`가 없어도 Device memory(uncached)로 매핑됩니다. RAM 주소일 때만 차이가 나서 cached 대신 Normal-NC(write-combine)가 됩니다. PL DMA와 공유하는 reserved DDR을 볼 때 중요합니다. |
| Why not `memcpy` a block of registers into a struct? | 접근 폭과 횟수를 제어할 수 없습니다. 64/128-bit나 바이트 접근, 비정렬 접근이 나와서 AXI-Lite가 SLVERR를 주거나 Device memory alignment fault가 납니다. read-to-clear 레지스터는 부작용도 생깁니다. 레지스터마다 32-bit volatile 접근 1회가 원칙입니다. |
| This tool must also run on a Zynq-7000. What breaks? | 32-bit ARM이라 `off_t`가 기본 32-bit입니다. 0xA000_0000 같은 주소는 signed 32-bit에 안 들어가서 mmap이 실패합니다(`-D_FILE_OFFSET_BITS=64` 필요, 그래서 `_Static_assert`). 주소도 GP0의 0x43C0_0000 대역으로 바뀌고 페이지는 4 KiB입니다. |
| How do you read a 64-bit counter over a 32-bit AXI-Lite bus? | hi → lo → hi를 읽어 두 hi가 같으면 확정, 다르면 다시 읽습니다. IP가 lo를 읽을 때 hi를 latch하는 설계면 그 순서를 지킵니다. 64-bit 단일 접근은 인터커넥트가 두 번으로 쪼갤 수 있어 원자적이지 않습니다. |
| `mmap` of `/dev/mem` returns `EPERM`. Why? | `CONFIG_STRICT_DEVMEM`이 RAM 영역을 막았거나, `CONFIG_IO_STRICT_DEVMEM`에서 이미 드라이버(UIO 포함)가 그 영역을 `request_mem_region`으로 잡은 경우입니다. root가 아니면 open 단계에서 이미 `EACCES`/`EPERM`입니다. 해결은 UIO fd를 mmap하거나 커널 설정 변경입니다. |
| How would you map registers through the UIO fd instead? | `mmap(NULL, size, RW, MAP_SHARED, uio_fd, N * page_size)`로 mapN을 엽니다. 크기·주소는 `/sys/class/uio/uio0/maps/mapN/{size,addr,offset}`에서 읽고, 페이지 안 offset은 `offset` 파일 값만큼 더합니다. 이 문제의 `delta`와 같은 개념입니다. |
| Why `poll()` and not a blocking `read()`? | blocking read는 timeout이 없어 IRQ가 안 오면 툴이 영원히 멈춥니다. `poll()`은 timeout과 여러 fd(예: 종료 신호용 eventfd, 원격 제어 socket)를 함께 기다릴 수 있습니다. 실제 UIO read는 count가 바뀔 때까지 막히므로 poll로 readable을 확인한 뒤 읽습니다. |
| Could the count "not advance" on real UIO? | 정상이라면 아닙니다. UIO read는 이 fd가 마지막으로 본 값과 다를 때만 돌아옵니다. 그래서 `delta == 0`은 fd 혼동이나 툴 버그를 뜻하는 방어 코드로 `-EIO`를 줍니다. |
| How would you measure interrupt latency with this setup? | IP가 IRQ를 올린 순간의 타임스탬프 레지스터와, `poll()`이 깨어난 직후 같은 타임베이스(예: PL의 free-running counter)를 읽어 차이를 냅니다. 수천 번 반복해 min/median/p99/max 히스토그램을 그립니다 → [V4](../V4_perf_bench_stats/index.html). PREEMPT_RT와 CPU 격리가 꼬리 지연을 줄입니다. |
| The IP gets reset in the middle of a test. What does your tool see? | AXI-Lite 레지스터가 reset 값으로 돌아갑니다(ID는 그대로, SCRATCH는 0). PL 전체가 재프로그램되면 접근 자체가 hang/SError 위험입니다. 그래서 긴 테스트는 단계마다 SCRATCH 패턴 쓰고 읽기로 IP가 살아 있는지 확인합니다 → [V2](../V2_reg_field_rmw/index.html). |

## 7. 자주 하는 실수

- `mmap()`의 offset에 `phys`를 그대로 넣음 → 페이지 배수가 아니면 `EINVAL` (테스트 2번)
- `page_base`로 매핑하고 `delta`를 안 더함 → 페이지 시작 레지스터를 읽음 (테스트 2번 `base - map == 0xF00`)
- 매핑 길이를 `round_up(len)`으로만 계산 → 페이지 경계 뒤 레지스터가 잘려서 SIGBUS/SIGSEGV (테스트 2번 `0x1FC`)
- `munmap(base, len)` → 페이지 정렬이 안 된 주소라 실패, 매핑이 새어 나감
- 경계 검사를 `map_len` 기준으로 하거나 `off + 4`를 `uint32_t`로 계산 → slack 접근, wrap 통과 (테스트 3번)
- `volatile` 없는 포인터로 poll → `-O2`에서 read가 루프 밖으로 나가 무한 루프
- timeout을 루프 횟수나 `time()`/`CLOCK_REALTIME`으로 셈 → NTP가 시계를 당기면 timeout이 틀어짐
- mmap 실패 시 `close(fd)`를 안 하거나, `close` 뒤에 `errno`를 읽음 → fd 누수, 엉뚱한 에러 코드
- UIO에 1바이트나 8바이트로 read/write → 실제 UIO는 `EINVAL`. unmask write를 빼먹음 → 두 번째 IRQ가 안 옴 (테스트 7번)
- `-1`을 반환하고 errno에 맡김 → 이 API는 `-errno` 규약 (테스트 4·8번 `-ENOENT`, `-EBADF`, `-EPIPE`)
