# D · Debug Drills (first-silicon bring-up)

> **ID**: D1–D10 · **Priority**: P0 · **Pattern**: find the bug in 1 minute · **Time**: 20 min
> 파일: `buggy.c`(버그 10개 + mock HW + 테스트) · `fixed.c`(정답) · 실행: `./run.sh D buggy` / `./run.sh D fixed` / 내 수정본 `./run.sh D my`

## 1. Problem

The interviewer shares a short piece of bring-up / validation code and asks: **"This works on the FPGA but fails on first silicon (or only sometimes). What is wrong?"** You get one to two minutes per snippet.

For each of the ten drills below:

1. Read the **intended behavior** and the **symptom on real hardware**.
2. Name the bug in one sentence, and say **which input or timing** triggers it.
3. Write the fixed line.
4. Say how you would have caught it (compiler warning, sanitizer, static analysis, review checklist, a directed test).

All ten functions live in `buggy.c`, each next to a tiny mock HW model (MMIO read counter, W1C register, D-cache modelled as a separate copy array, a DMA engine that snoops descriptors, a fake timer, fake RAM with canary words). The test `main()` exposes every bug without crashing or hanging. Your job is to make it print `ALL PASS`.

| Drill | Function | Topic |
|---|---|---|
| D1 | `wait_dma_done()` | poll a status register |
| D2 | `irq_ack()` | clear one bit of a W1C interrupt status |
| D3 | `dma_loopback_test()` | DMA loopback with a write-back D-cache |
| D4 | `desc_submit()` | hand a DMA descriptor to hardware (OWN bit) |
| D5 | `wait_pll_lock()` | timeout on a free-running 32-bit timer |
| D6 | `reg_from_dump()` | rebuild a 32-bit register from a byte dump |
| D7 | `UartRegs` + `uart_enable_rx_irq()` | register-map struct layout |
| D8 | `field_get()` | bit-field mask generation |
| D9 | `bw_mbps()` | bandwidth from a cycle count |
| D10 | `mem_test()` | memory test over a region given in bytes |

### Constraints

- Change only the function bodies (and, for D7, the struct declaration).
- Keep the mock HW helpers (`dma_status_read`, `intc_read/intc_write`, `cpu_rd/cpu_wr/dcache_*`, `dma_copy`, `hw_snoop`, `DESC_WR`, `timer_now`, `pll_status_read`) and the line marked `TEST GUARD` as they are. They stand in for hardware and only exist so the test can see what the bus, the cache and the DMA engine saw.
- The fixed file must pass with `-Wall -Wextra -Werror -fsanitize=address,undefined`.

### Clarifying questions to ask

1. What are the register access semantics? (RW, RO, W1C, write-only, read-to-clear)
2. Is the DMA cache-coherent with the CPU on this SoC (ACE/CHI port), or do I manage cache maintenance by hand?
3. What is the width and frequency of the timer / cycle counter, i.e. how often does it wrap?
4. What endianness is the device or the dump format? Is the CPU running little-endian?
5. Is this code running from a cached region, with the MMU on, or on a bare-metal flat map?

## 2. Your workspace

아래 `buggy.c`가 작업 파일입니다. **복사본을 만들어 그 안에서 바로 고치세요.**

```
cd hmScreenPrep
./run.sh D buggy                                   # 버그 10개가 모두 <-- BUG로 보임
cp problems/D_debug_drills/buggy.c problems/D_debug_drills/my.c
# my.c를 고친 뒤
./run.sh D my                                      # 목표: 10줄 모두 ok + ALL PASS
```

드릴마다 `D1 poll without volatile   ok` / `<-- BUG` 한 줄이 찍힙니다. D8은 UBSan이 `runtime error: shift exponent 32 is too large`를 먼저 출력합니다. 그것 자체가 힌트입니다.

<div class="code" data-src="buggy.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — 어디부터 의심하나</summary>

실리콘 bring-up 코드 버그는 대부분 다섯 종류입니다. **(1) 컴파일러가 HW 접근을 바꿈**(`volatile` 없음), **(2) 레지스터 의미**(W1C, RO, read-to-clear), **(3) 메모리 순서·캐시**(DMA coherency, OWN 비트 순서, barrier), **(4) 정수 폭**(overflow, shift, wrap), **(5) 레이아웃·단위**(struct offset, endianness, byte vs word). 스니펫을 보면 "HW는 이 순간 무엇을 보나?"와 "타입이 몇 비트지?"를 먼저 묻습니다.

</details>

<details>
<summary>Hint 2 — 숫자를 하나 넣어 보기</summary>

- D2: pending이 `0x13`이고 TX(bit 0)만 지우려 할 때, 실제로 쓰는 값은?
- D5: `start = 0xFFFFF000`, `timeout = 50000`이면 `start + timeout`은?
- D8: `width = 32`면 `1u << 32`는?
- D9: `16777216 * 1000`은 2^32보다 큰가?
- D10: `uint32_t *base`에 `+ 64`를 하면 몇 바이트 뒤인가?

</details>

<details>
<summary>Hint 3 — 도구가 알려 주는 것</summary>

`./run.sh`는 ASan/UBSan을 켭니다. 도구가 잡는 것은 **D8 하나**(UBSan `shift exponent 32`)뿐입니다. D7은 `_Static_assert(offsetof(...))`를 넣었다면 컴파일이 막혔을 것입니다. D10은 실제 보드라면 ASan 대신 canary/MPU가 잡습니다(여기서는 fake RAM이 4배 크기라 ASan도 조용함). 나머지 D1–D6, D9는 **HW 모델 + 방향성 테스트**로만 드러납니다. 그래서 validation 엔지니어는 "도구 + 경계값 테스트 + 리뷰 체크리스트"를 같이 씁니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어

각 드릴에서 말할 순서는 항상 같습니다: **① 버그 한 문장 → ② 터지는 입력/타이밍 → ③ 고친 한 줄 → ④ 어떻게 찾았을지.** HW 면접관에게는 여기에 **⑤ 첫 실리콘에서 어떻게 보였을지**(scope, 로그, JTAG)를 한 줄 붙이면 좋습니다. 아래는 드릴별 문제(English)와 힌트·답(펼치기)입니다.

### D1 · Poll a status register

**Intended behavior:** poll `DMA_STATUS` until bit `DONE` is 1. Return the number of polls that saw `DONE == 0`, or `-1` after `max_polls` polls. Every poll must be a real bus read.

Source as written on the bench:

```c
#define DMA_STATUS (*(uint32_t *)0x40001004u)      /* no volatile */
int wait_dma_done(uint32_t max_polls) {
    for (uint32_t i = 0; i < max_polls; i++)
        if (DMA_STATUS & DMA_DONE) return (int)i;
    return -1;
}
```

`buggy.c` writes out what `-O2` is allowed to make of this: one load before the loop, then a loop over the register copy.

**Symptom on first silicon:** works with `-O0` and under the debugger; the `-O2` release build always reports "DMA timeout". A logic analyzer / AXI monitor shows exactly **one** read of `0x40001004`, then nothing.

<details>
<summary>D1 힌트</summary>

컴파일러 입장에서 `*(uint32_t *)0x40001004`는 그냥 메모리입니다. 루프 안에서 아무도 그 메모리에 쓰지 않는데, 매번 다시 읽어야 할 이유가 있을까요?

</details>

<details>
<summary>💡 D1 답 펼치기</summary>

**버그**: 레지스터 포인터에 `volatile`이 없어서 컴파일러가 읽기를 루프 밖으로 끌어냅니다(loop-invariant hoisting). 첫 읽기에서 DONE이 0이면 이후 `max_polls`번 같은 값을 보고 `-1`을 돌려줍니다. 테스트: DONE이 4번째 읽기에 1이 되도록 하면 정답은 `3`, 버그 버전은 `-1`이고 버스 읽기 횟수는 `4`가 아니라 `1`입니다.

**고친 코드**
```c
#define DMA_STATUS (*(volatile uint32_t *)0x40001004u)
for (uint32_t i = 0; i < max_polls; i++)
    if (DMA_STATUS & DMA_DONE) return (int)i;   /* 매 반복 새 버스 읽기 */
```
`fixed.c`에서는 `dma_status_read()`를 루프 안에서 매번 부르는 것으로 같은 의미를 표현합니다. 실제 코드는 `readl()` / `REG32()` 같은 `volatile` 접근자를 쓰고, 레지스터 포인터를 직접 역참조하지 않는 것을 규칙으로 둡니다.

**어떻게 찾나**
- 컴파일러·sanitizer는 못 잡습니다(최적화로서 합법).
- 리뷰 체크리스트: "MMIO 접근은 모두 `volatile` 접근자를 거치는가?" `grep`으로 `(uint32_t *)0x` 같은 생 캐스트를 찾습니다.
- 디스어셈블: 루프에 `ldr`이 없으면 바로 보입니다. "`-O0`에서 되고 `-O2`에서 안 된다"는 증상은 거의 항상 `volatile` 아니면 UB입니다.
- `volatile`은 **컴파일러**만 막습니다. CPU 쪽 순서(Device memory 속성, barrier)는 D4에서 따로 다룹니다.

</details>

### D2 · Acknowledge one interrupt source (W1C)

**Intended behavior:** `INT_STATUS` is write-1-to-clear: writing 1 to a bit clears it, writing 0 leaves it alone. `irq_ack(bit)` must clear only that bit and leave every other pending source pending.

```c
void irq_ack(uint32_t bit) {
    intc_write(intc_read() | bit);       /* i.e. INT_STATUS |= bit; */
}
```

**Symptom on first silicon:** under load, RX completions and error interrupts are "sometimes lost". The interrupt line drops after the TX handler runs even though RX data is in the FIFO; the error counter in the log stays at 0 while the PHY reports CRC errors.

<details>
<summary>D2 힌트</summary>

`|=`는 "읽고, OR하고, 쓰기"입니다. 읽은 값에는 지금 pending인 **모든** 비트가 1로 들어 있습니다. W1C 레지스터에 그 값을 그대로 다시 쓰면?

</details>

<details>
<summary>💡 D2 답 펼치기</summary>

**버그**: W1C 레지스터에 read-modify-write를 했습니다. pending이 `0x13`(TX, RX, ERR)이면 쓰는 값도 `0x13`이 되어 세 비트가 **모두** 지워집니다. 결과 `0x00`, 기대값 `0x12`. RX와 ERR 이벤트가 처리되기 전에 사라집니다.

<div class="chart" data-src="chart1.svg" data-caption="D2: 읽은 값(0x13)을 그대로 다시 쓰면 W1C가 RX와 ERR까지 지움. 지울 비트(0x01)만 쓰면 나머지는 그대로 남음"></div>

**고친 한 줄**
```c
intc_write(bit);                         /* INT_STATUS = bit; */
```

**어떻게 찾나**
- 도구는 못 잡습니다. 레지스터 맵의 access type(RW/RO/W1C/RC)을 **헤더에 같이 생성**하고, W1C 필드에 `|=`를 쓰지 못하게 하는 접근자(`reg_w1c(addr, mask)`)를 두는 것이 근본 해결입니다.
- 방향성 테스트: 소스 2개 이상을 동시에 pending시키고 하나만 ack한 뒤 나머지가 남아 있는지 확인합니다(validation에서 "interrupt aggregation" 테스트).
- 같은 함정: 같은 레지스터에 RW 필드와 W1C 필드가 섞여 있으면 RW 필드를 바꾸려는 RMW가 W1C 비트를 지웁니다. 이때는 W1C 비트 위치를 0으로 마스킹하고 씁니다: `reg = (reg & ~W1C_MASK) | new_rw_bits;`. 자세한 버전은 [V2](../V2_reg_field_rmw/index.html).

</details>

### D3 · DMA loopback with a write-back D-cache

**Intended behavior:** CPU writes a pattern into `src`, DMA copies `src → dst`, CPU checks `dst`. Return the number of mismatching words. Must pass on every run, not only the first one. The DMA engine is **not** cache-coherent.

```c
for (i = 0; i < n; i++) cpu_wr(src + i, seed + i * 0x01010101u);
dcache_clean(src, n);                    /* make the pattern visible to DMA */
dma_copy(src, dst, n);                   /* start + wait for DONE */
for (i = 0; i < n; i++)
    if (cpu_rd(dst + i) != seed + i * 0x01010101u) bad++;
```

**Symptom on first silicon:** the DMA loopback test passes right after reset, then fails on the second and later iterations with **every** word wrong. The "wrong" data is exactly the previous iteration's pattern. Dumping `dst` over JTAG (which reads DRAM, not the cache) shows correct data.

<details>
<summary>D3 힌트</summary>

첫 번째 실행의 검증 루프가 `dst`를 읽으면서 그 라인들은 캐시에 들어왔습니다. 두 번째 실행에서 DMA가 DRAM에 새 데이터를 쓰면, CPU의 캐시는 그 사실을 알까요?

</details>

<details>
<summary>💡 D3 답 펼치기</summary>

**버그**: DMA가 끝난 뒤 `dst`의 캐시 라인을 **invalidate하지 않았습니다.** 1회차 검증에서 `dst` 라인이 캐시에 올라왔고, 2회차에 DMA가 DRAM에 `0x27270505`(word 5)를 써도 CPU는 캐시 hit로 1회차 값 `0x16160505`를 읽습니다. 16개 word 모두 mismatch → `bad = 16`. DRAM 자체(`ddr[37]`)는 맞습니다.

<div class="chart" data-src="chart2.svg" data-caption="D3: 2회차. DMA는 DDR에 새 값을 쓰지만 CPU는 1회차에 채운 캐시 라인을 hit로 읽음. DONE 이후 dst를 invalidate하면 miss → DDR에서 새 값을 채움"></div>

**고친 한 줄**
```c
dma_copy(src, dst, n);
dcache_invalidate(dst, n);               /* device wrote it -> drop CPU copies */
```
규칙은 두 개입니다. **장치가 메모리를 읽기 전에 clean**(src), **CPU가 장치가 쓴 메모리를 읽기 전에 invalidate**(dst). 실제 Cortex-A에서는 추측성 prefetch가 DMA 도중 `dst` 라인을 다시 채울 수 있어서, DMA 시작 **전**에 clean+invalidate 하고 끝난 **후**에 다시 invalidate하는 것이 안전합니다(Linux `dma_unmap_single(DMA_FROM_DEVICE)`가 하는 일). 버퍼는 캐시 라인(64 B) 정렬·크기여야 invalidate가 이웃 데이터를 버리지 않습니다.

**어떻게 찾나**
- 도구는 못 잡습니다. **같은 테스트를 2회 이상, 다른 seed로** 돌리는 것이 핵심입니다(이 드릴에서도 1회차는 통과).
- 진단 요령: JTAG로 DRAM을 직접 읽으면 맞고 CPU가 읽으면 틀리면 coherency 문제. 캐시를 끄거나(`SCTLR.C=0`) 버퍼를 non-cacheable로 매핑해서 증상이 사라지면 확정입니다.
- SoC 질문: "이 DMA 마스터는 ACE-Lite/CHI로 coherent interconnect에 붙어 있나?" coherent면 maintenance가 필요 없지만, 실리콘 초기에는 coherency를 끄고 테스트하는 경우가 많습니다. 자세한 링 버퍼 버전은 [V3](../V3_dma_desc_ring/index.html).

</details>

### D4 · Hand a descriptor to the DMA engine (OWN bit)

**Intended behavior:** fill `buf_addr` and length, then give the descriptor to hardware by setting `OWN`. The engine may fetch the descriptor at any moment once `OWN = 1`, so it must see the new `buf_addr` and length together with `OWN`. Layout: `ctrl[31] = OWN`, `ctrl[30] = IOC`, `ctrl[15:0] = length`.

```c
void desc_submit(DmaDesc *d, uint32_t buf, uint16_t len) {
    d->ctrl = DESC_OWN | DESC_IOC | len;
    dma_wmb();
    d->buf_addr = buf;
}
```

**Symptom on first silicon:** rare, load-dependent data corruption: occasionally a packet is transmitted from the **previous** buffer, or the engine raises a bus error on a stale address (here `0xDEAD0000`). It never happens on the FPGA (slow engine, fetches late) and gets worse at higher DMA clock.

<details>
<summary>D4 힌트</summary>

OWN을 쓰는 순간부터 descriptor는 HW 것입니다. 그 순간 `buf_addr`에는 어떤 값이 들어 있나요? barrier는 어느 두 store 사이에 있어야 할까요?

</details>

<details>
<summary>💡 D4 답 펼치기</summary>

**버그**: **OWN을 먼저 쓰고** 나머지 필드를 나중에 씁니다. 엔진이 OWN=1을 본 순간 가져간 descriptor는 `buf_addr = 0xDEAD0000`(지난번 값), `ctrl = 0xC0000100`입니다. barrier가 있긴 하지만 엉뚱한 위치(OWN 뒤)에 있습니다.

**고친 코드**
```c
d->buf_addr = buf;                       /* 1. payload fields */
dma_wmb();                               /* 2. make them visible first */
d->ctrl = DESC_OWN | DESC_IOC | len;     /* 3. OWN last = hand-off */
/* 4. (if the engine is idle) write the doorbell / tail pointer register */
```
반대 방향도 같습니다. 완료 처리할 때는 **OWN==0을 먼저 읽고**, `dma_rmb()` 후에 status/length를 읽습니다. 순서가 바뀌면 HW가 아직 쓰기 전의 length를 읽습니다.

**어떻게 찾나**
- 도구는 못 잡습니다. 리뷰 체크리스트: "ownership 비트는 마지막에 쓰고, 그 앞에 write barrier가 있는가?"
- C 컴파일러도 일반 store 순서를 바꿀 수 있고(descriptor가 `volatile`이 아니면), Arm의 Normal memory는 CPU도 store 순서를 바꿉니다. 그래서 `volatile`만으로는 부족하고 `dmb oshst`/`dsb` 같은 barrier가 필요합니다.
- 실리콘에서 재현: DMA 클럭을 올리고 descriptor를 연속 제출하는 스트레스 테스트, 또는 엔진 쪽 AXI 트레이스에서 descriptor fetch 시점과 CPU store 시점을 비교합니다. FPGA는 엔진이 느려서 순서 버그가 숨는 대표적인 경우입니다.

</details>

### D5 · PLL lock timeout on a wrapping timer

**Intended behavior:** poll `PLL_STATUS.LOCK`. Return the number of polls that saw `LOCK == 0`, or `-1` once **at least** `timeout` ticks have elapsed since the start. The 32-bit timer wraps from `0xFFFFFFFF` to `0`, and that must not matter.

```c
uint32_t start = timer_now();
while (!(pll_status_read() & PLL_LOCK)) {
    uint32_t now = timer_now();
    if (now > start + timeout) return -1;
    ...
}
```

**Symptom on first silicon:** about once every 43 s of uptime (a 100 MHz 32-bit counter wraps every 42.9 s), PLL lock "fails" after almost no time and the boot falls back to the bypass clock. Power-cycle and it is fine. In PVT runs this looks like a random lock-failure rate, which is the worst kind of false failure.

<details>
<summary>D5 힌트</summary>

`start`가 랩 직전(`0xFFFFF000`)이면 `start + timeout`은 작은 수가 됩니다. 첫 번째 `now`(`0xFFFFF3E8`)는 그 작은 수보다 큰가요?

</details>

<details>
<summary>💡 D5 답 펼치기</summary>

**버그**: 덧셈 비교 `now > start + timeout`은 `start + timeout`이 랩되면 깨집니다. `0xFFFFF000 + 50000 = 0x0000B350`(45904)이고, 첫 poll의 `now = 0xFFFFF3E8`은 그보다 크므로 **1000 tick 만에 타임아웃**(`d5_elapsed = 1000`, 기대 50000). 반대로 `start + timeout`은 랩되지 않았는데 `now`가 먼저 랩하면 다음 한 바퀴 동안 타임아웃이 안 납니다(hang). 덤으로 `>`라 정확히 `timeout`인 순간을 한 poll 놓칩니다.

<div class="chart" data-src="chart3.svg" data-caption="D5: 덧셈 비교는 deadline이 0x0000B350으로 넘어가서 첫 poll부터 '타임아웃'. 뺄셈 (now − start)은 랩을 넘어서도 경과 tick(1000)을 정확히 줌"></div>

**고친 한 줄**
```c
if ((uint32_t)(now - start) >= timeout) return -1;
```
부호 없는 뺄셈은 mod 2^32로 정의되어 있어서 경과 시간이 2^32보다 작기만 하면 랩과 무관하게 맞습니다. `(uint32_t)` 캐스트는 16-bit 카운터처럼 `int`로 승격되는 타입에서 꼭 필요하므로 습관으로 붙입니다.

**어떻게 찾나**
- 컴파일러·sanitizer 모두 조용합니다(unsigned 랩은 정의된 동작).
- 테스트: 타이머 초기값을 랩 직전으로 두는 시뮬레이션, 실리콘에서는 타이머를 `0xFFFF0000` 근처로 preload할 수 있으면 몇 ms 안에 재현됩니다.
- 리뷰 체크리스트: "시간 비교는 항상 `now - start`로". [V12](../V12_frame_timestamp_check/index.html)의 timestamp 비교도 같은 원리입니다.

</details>

### D6 · Rebuild a register value from a byte dump

**Intended behavior:** the on-chip debug monitor dumps each 32-bit register as 4 bytes, **little-endian on the wire**: `b[0]` = bits 7..0, …, `b[3]` = bits 31..24. Return the register value.

```c
uint32_t reg_from_dump(const uint8_t b[4]) {
    return ((uint32_t)b[0] << 24) | ((uint32_t)b[1] << 16) |
           ((uint32_t)b[2] << 8)  |  (uint32_t)b[3];
}
```

**Symptom on first silicon:** the bring-up script prints `CHIP_ID = 0x78563412` instead of the expected `0x12345678` and declares "wrong die". A status register with only bit 0 set reads `0x01000000`, so the script decodes bit 24 instead of bit 0.

<details>
<summary>D6 힌트</summary>

`{0x78, 0x56, 0x34, 0x12}`를 받아서 `0x12345678`을 만들어야 합니다. `b[0]`은 몇 번 비트 자리로 가야 하나요?

</details>

<details>
<summary>💡 D6 답 펼치기</summary>

**버그**: 와이어 포맷은 little-endian인데 big-endian(MSB first)으로 조립합니다. `{78 56 34 12}` → `0x78563412`, `{01 00 00 00}` → `0x01000000`, `{00 00 00 80}` → `0x00000080`. 바이트 순서가 뒤집힌 값이 나옵니다.

**고친 코드**
```c
return  (uint32_t)b[0]        | ((uint32_t)b[1] << 8) |
       ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
```
시프트로 조립하면 **호스트 endianness와 무관**합니다. `memcpy(&v, b, 4)`는 호스트가 LE일 때만 맞으므로(PC에서는 되고 BE 호스트에서는 틀림) 이식성 있는 코드에서는 피합니다. 각 바이트를 `(uint32_t)`로 캐스트하는 것도 중요합니다. 안 하면 `b[3] << 24`가 `int`로 계산되어 `b[3] >= 0x80`일 때 UB(D8과 같은 종류)입니다.

**어떻게 찾나**
- 도구는 못 잡습니다. **비대칭 테스트 값**(`0x12345678`, `0x00000001`, `0x80000000`)을 쓰는 것이 답입니다. `0x00000000`, `0xFFFFFFFF`, `0xA5A5A5A5`처럼 바이트가 대칭인 값은 endianness 버그를 숨깁니다.
- 실리콘 첫날 습관: CHIP_ID/버전 레지스터처럼 **알려진 값**을 먼저 읽어 버스·endianness·주소 디코드를 한 번에 확인합니다. 센서 쪽(I2C/SPI, 보통 MSB first)은 반대 방향이라는 것도 같이 말합니다.

</details>

### D7 · Register-map struct layout

**Intended behavior:** datasheet map of a UART block, all registers 32-bit: `0x00 CTRL`, `0x04 STATUS`, `0x08–0x0F reserved`, `0x10 INT_EN`, `0x14 DATA`. `uart_enable_rx_irq()` sets `INT_EN.RX` (bit 0) at offset `0x10`.

```c
typedef struct {
    uint32_t CTRL;          /* 0x00 */
    uint32_t STATUS;        /* 0x04 */
    uint16_t RESERVED[2];   /* 0x08 - 0x0F */
    uint32_t INT_EN;        /* 0x10 */
    uint32_t DATA;          /* 0x14 */
} UartRegs;
void uart_enable_rx_irq(volatile UartRegs *u) { u->INT_EN |= UART_INT_RX; }
```

**Symptom on first silicon:** RX interrupts never fire. Writes to `DATA` produce no TX activity on the scope. A bus trace shows the driver writing `0x0C` and `0x10` instead of `0x10` and `0x14`. The comments in the struct are correct; the struct is not.

<details>
<summary>D7 힌트</summary>

`uint16_t RESERVED[2]`는 몇 바이트인가요? 0x08부터 0x0F까지는 몇 바이트인가요? 주석의 오프셋은 컴파일러가 확인하지 않습니다.

</details>

<details>
<summary>💡 D7 답 펼치기</summary>

**버그**: reserved 영역을 `uint16_t[2]`(4바이트)로 선언했는데 실제로는 8바이트(32-bit 2개)입니다. 그 뒤 필드가 모두 4바이트씩 당겨져서 `offsetof(INT_EN) = 0x0C`, `offsetof(DATA) = 0x10`, `sizeof = 0x14`(기대 `0x10`, `0x14`, `0x18`). 드라이버는 reserved 레지스터(`0x0C`)에 쓰고, `DATA`에 쓴다는 것이 실제로는 `INT_EN`에 씁니다.

**고친 코드**
```c
uint32_t RESERVED[2];   /* 0x08 - 0x0F */
_Static_assert(offsetof(UartRegs, INT_EN) == 0x10, "UART INT_EN offset");
_Static_assert(offsetof(UartRegs, DATA)   == 0x14, "UART DATA offset");
_Static_assert(sizeof(UartRegs)           == 0x18, "UART block size");
```
`_Static_assert`가 진짜 수정입니다. 다음 사람이 필드를 잘못 바꿔도 **빌드가 실패**합니다.

**어떻게 찾나**
- `_Static_assert(offsetof(...))` 또는 `pahole`로 레이아웃 확인. 가장 좋은 방법은 레지스터 헤더를 손으로 쓰지 않고 **IP-XACT/SystemRDL에서 생성**하는 것입니다(실리콘 팀이 이미 RTL용으로 갖고 있음).
- validation 테스트: **register reset-value / walking-ones 테스트**를 생성된 맵 기준으로 돌리면 오프셋이 틀린 레지스터에서 바로 실패합니다. 이 테스트가 bring-up 첫날 하는 일입니다.
- 같은 종류: `uint16_t`로 선언한 32-bit 레지스터(접근 폭이 틀려 버스 에러 또는 상위 비트 유실), packed struct로 인한 unaligned 접근.

</details>

### D8 · Bit-field mask generation

**Intended behavior:** return bits `[lsb + width − 1 : lsb]` of `reg`, right-aligned. Valid for `1 <= width <= 32` and `lsb + width <= 32` (width 32 = whole register).

```c
uint32_t field_get(uint32_t reg, unsigned lsb, unsigned width) {
    uint32_t mask = (1u << width) - 1u;
    return (reg >> lsb) & mask;
}
```

**Symptom on first silicon:** every field decodes correctly except full-width ones: `field_get(PERF_CNT, 0, 32)` returns `0` on the Cortex-A/R target, so the throughput report shows 0 cycles. Built for a different core or with another compiler/optimization level, it returns something else.

<details>
<summary>D8 힌트</summary>

C에서 shift 양이 피연산자의 비트 폭 이상이면 무엇이 정의되어 있나요? `./run.sh D buggy`의 첫 줄을 보세요.

</details>

<details>
<summary>💡 D8 답 펼치기</summary>

**버그**: `width == 32`일 때 `1u << 32`는 **정의되지 않은 동작(UB)**입니다. Arm과 x86은 shift 양을 mod 32로 처리해서 `1u << 32 == 1`, mask가 `0`이 되고 결과는 `0`(기대 `0xABCD1234`). 다른 컴파일러나 최적화 수준에서는 다른 값이 나올 수 있습니다.

**고친 한 줄**
```c
uint32_t mask = (width >= 32) ? 0xFFFFFFFFu : ((1u << width) - 1u);
/* or, for 1 <= width <= 32:  uint32_t mask = 0xFFFFFFFFu >> (32u - width); */
```

**어떻게 찾나**
- UBSan이 바로 알려 줍니다: `buggy.c:228:25: runtime error: shift exponent 32 is too large for 32-bit type 'unsigned int'`. 호스트에서 sanitizer로 단위 테스트를 돌리는 이유입니다.
- 형제 버그: `1 << 31`(부호 있는 `int`에서 부호 비트로 shift, C11에서 UB) → `#define BIT(n) (1u << (n))`처럼 항상 unsigned 리터럴. `uint8_t` 바이트를 `<< 24`하면 `int`로 승격된 뒤 같은 UB가 됩니다(D6).
- 테스트 경계값: width 1, 31, 32와 lsb 0, 31.

</details>

### D9 · Bandwidth from a cycle count

**Intended behavior:** `bytes` were moved in `cycles` cycles of a `clk_mhz` MHz counter. Return MB/s (10^6 bytes per second), truncated: `MB/s = bytes * clk_mhz / cycles`. Must be exact for `bytes` up to 4 GiB − 1 and `clk_mhz` up to 2000.

```c
uint32_t bw_mbps(uint32_t bytes, uint32_t cycles, uint32_t clk_mhz) {
    uint64_t bw = bytes * clk_mhz / cycles;
    return (uint32_t)bw;
}
```

**Symptom on first silicon:** small transfers report sensible numbers (4 KiB → 4096 MB/s), but the 16 MiB benchmark reports **928 MB/s** for a DDR path the scope/perf counters say runs at 4 GB/s. The team starts debugging the DDR controller when the bug is in the report.

<details>
<summary>D9 힌트</summary>

`bw`가 `uint64_t`라는 것이 곱셈에 영향을 주나요? `bytes * clk_mhz`는 어떤 타입으로 계산되나요? `16777216 * 1000`을 2^32와 비교해 보세요.

</details>

<details>
<summary>💡 D9 답 펼치기</summary>

**버그**: 결과를 `uint64_t`에 담아도 **곱셈은 `uint32_t`로** 계산됩니다(두 피연산자 모두 32-bit). `16777216 × 1000 = 16,777,216,000`은 2^32를 넘어서 mod 2^32 = `3,892,314,112`가 되고, `/ 4194304` = **928**(기대 4000). `bytes = 0xFFFFFFFF, clk = 2000`이면 결과는 `0`(기대 2000).

**고친 한 줄**
```c
uint64_t bw = (uint64_t)bytes * clk_mhz / cycles;   /* widen BEFORE multiply */
```
캐스트는 **첫 피연산자**에 붙여야 합니다. `(uint64_t)(bytes * clk_mhz)`는 이미 넘친 값을 넓힐 뿐입니다. 최대값 검산: `(2^32 − 1) × 2000 < 2^43`이라 64-bit로 충분합니다. 순서를 `bytes / cycles * clk`로 바꾸면 overflow는 피하지만 정밀도를 잃습니다(곱하고 나누기).

**어떻게 찾나**
- unsigned overflow는 정의된 동작이라 UBSan 기본 설정은 조용합니다. clang `-fsanitize=unsigned-integer-overflow`(UB가 아니지만 검사 가능)나 `-Wconversion` 계열, 정적 분석(Coverity, clang-tidy `bugprone-implicit-widening-of-multiplication-result`)이 잡습니다.
- 테스트: 큰 값 + **독립 측정과 교차 검증**(perf counter, scope로 본 버스 활동, 이론 최대 대역폭과 비교). 벤치마크 숫자가 이론치의 23%면 "HW가 느리다"보다 "계산이 틀렸다"를 먼저 의심합니다. 자세한 버전은 [V4](../V4_perf_bench_stats/index.html).

</details>

### D10 · Memory test over a region given in bytes

**Intended behavior:** for every 32-bit word in `[base, base + len_bytes)` write a pattern, then read all words back. Return the number of mismatching words. Must not touch a single byte outside the region.

```c
uint32_t mem_test(uint32_t *base, size_t len_bytes) {
    uint32_t *end = base + len_bytes;
    for (uint32_t *p = base; p < end; p++) *p = 0xA5A50000u ^ (uint32_t)(p - base);
    ...                                  /* read back the same range */
}
```

**Symptom on first silicon:** the memory test **passes**, and right after it the board crashes or the stack/heap/log buffer that sits after the test region is corrupted. Testing a 64 KiB SRAM window trashes the next 192 KiB.

<details>
<summary>D10 힌트</summary>

`uint32_t *` 포인터에 정수 `n`을 더하면 주소는 몇 바이트 움직이나요? `len_bytes = 64`면 `end`는 `base`에서 몇 바이트 뒤인가요?

</details>

<details>
<summary>💡 D10 답 펼치기</summary>

**버그**: 포인터 산술은 **원소 단위**입니다. `base + len_bytes`는 `len_bytes × 4`바이트 뒤라서 테스트 범위가 4배가 됩니다. 64바이트(16 word) 영역을 테스트하면 64 word를 쓰고, 뒤의 canary 48개가 덮어써집니다. 그런데 쓴 범위를 그대로 다시 읽으므로 **결과는 `bad = 0`, 테스트 통과**입니다. 이게 가장 위험한 점입니다.

**고친 한 줄**
```c
uint32_t *end = base + len_bytes / sizeof *base;
```
`len_bytes`가 4의 배수인지(그리고 `base`가 4바이트 정렬인지)도 assert하는 것이 좋습니다. API를 아예 `size_t n_words`로 받으면 이 종류의 실수가 사라집니다.

**어떻게 찾나**
- 호스트에서는 ASan이 `global-buffer-overflow`로 잡습니다(이 드릴은 fake RAM을 4배 크게 잡아서 ASan 대신 canary로 보이게 함).
- 타깃에서는 **guard/canary 영역**을 두고 테스트 후 검사, MPU로 테스트 영역 밖을 no-access로 막기, 또는 JTAG data watchpoint를 `base + len_bytes`에 겁니다.
- "메모리 테스트 자체를 검증"하는 습관: fault injection(테스트 중 한 word를 일부러 뒤집기)으로 **테스트가 실패할 수 있는지** 확인합니다. March 알고리즘 버전은 [V1](../V1_march_memtest/index.html).

</details>

### 4.2 정리 표

| Drill | bug class | 첫 실리콘에서의 증상 | 고치는 한 줄 | 무엇이 잡나 |
|---|---|---|---|---|
| D1 | missing `volatile` (MMIO) | `-O2`에서 DMA timeout, 버스 읽기 1회 | `volatile` 접근자로 매 poll 읽기 | 리뷰, 디스어셈블 |
| D2 | RMW on W1C | 다른 IRQ(RX, ERR) 유실, `0x13 → 0x00` | `intc_write(bit);` | 레지스터 access type 헤더, 다중 pending 테스트 |
| D3 | missing cache invalidate | 2회차부터 전부 mismatch, JTAG로는 정상 | DONE 후 `dcache_invalidate(dst, n)` | 반복 실행 + 다른 seed |
| D4 | OWN written first | 가끔 이전 버퍼(`0xDEAD0000`)로 DMA | payload → `wmb` → OWN | 리뷰 체크리스트, 고속 스트레스 |
| D5 | wraparound (add-compare) | 43 s마다 PLL lock "실패" | `(uint32_t)(now - start) >= timeout` | 랩 직전 preload 테스트 |
| D6 | wrong byte order | `CHIP_ID = 0x78563412` | `b[0]`이 LSB | 비대칭 테스트 값 |
| D7 | struct layout (`uint16_t[2]`) | INT_EN이 `0x0C`에 써짐 | `uint32_t RESERVED[2]` + `_Static_assert` | `_Static_assert`, 생성 헤더 |
| D8 | shift by width (UB) | 32-bit 필드가 0 | `width >= 32 ? ~0u : …` | UBSan |
| D9 | 32-bit multiply overflow | 4000 MB/s가 928로 보고 | `(uint64_t)bytes * clk_mhz` | 정적 분석, 교차 검증 |
| D10 | pointer scaled by element | 테스트 통과 + 이웃 메모리 파괴 | `base + len_bytes / sizeof *base` | ASan, canary, MPU |

### 4.3 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (드릴 다 해 본 다음에)</summary>

<div class="code" data-src="fixed.c"></div>

</details>

**코드 읽는 포인트**
- 모든 수정에 `/* FIX: ... */` 주석이 붙어 있습니다. `diff buggy.c fixed.c`로 보면 드릴마다 1–4줄만 다릅니다.
- D1의 `dma_status_read()`는 "버스 읽기 1회"를 세는 hook입니다. 실제 `volatile` 버그는 최적화기가 만드는 것이라 호스트 테스트로 재현하기 어려워서, `buggy.c`는 `-O2`가 만든 결과를 C로 풀어 썼습니다.
- D3의 캐시 모델은 word 하나가 라인 하나인 단순화입니다. 실제 라인은 64 B라서 정렬·크기 문제가 추가됩니다.
- D4의 `DESC_WR()`는 store 한 번마다 DMA 엔진이 descriptor를 훔쳐보는(snoop) 최악의 타이밍을 모델링합니다.
- D10의 fake RAM은 일부러 4배 + 16 word로 잡았습니다. 버그 버전이 범위를 넘어도 ASan이 멈추지 않고 canary 48개 파괴로 보이게 하기 위해서입니다.

### 4.4 복잡도

드릴 자체는 복잡도 문제가 아닙니다. 고친 코드는 모두 원래와 같은 O(n) 또는 O(1)이고 추가 메모리가 없습니다. 비용이 드는 수정은 D3(라인 수만큼 cache maintenance 명령)과 D4(barrier 1개)뿐이며, 둘 다 DMA 전송 시간에 비하면 무시할 수준입니다.

---

## 5. 임베디드 관점으로 말할 것

- **HW가 보는 것 vs 코드가 보는 것**: D1(컴파일러), D3(캐시), D4(store 순서)는 모두 "CPU 코드가 생각하는 메모리 상태와 버스/디바이스가 실제로 보는 상태가 다르다"는 같은 문제입니다. 이 한 문장으로 세 드릴을 묶어서 말하면 시니어로 보입니다.
- **레지스터 의미는 헤더에 넣는다**: D2(W1C), D7(offset)은 레지스터 맵을 IP-XACT/SystemRDL에서 생성하고 access type·offset을 `_Static_assert`로 고정하면 사라집니다. 손으로 쓴 헤더는 첫 실리콘 버그의 단골 원인입니다.
- **도구 + 방향성 테스트**: `-Wall -Wextra -Werror`, 호스트 ASan/UBSan(D8, D10), 정적 분석(D9)으로 잡을 수 있는 것은 먼저 잡고, 나머지(D2, D3, D5, D6)는 다중 pending, 반복 실행, 랩 직전 preload, 비대칭 값 같은 **경계 조건 테스트**로 잡습니다.
- **FPGA에서 통과 ≠ 실리콘에서 통과**: FPGA/HAPS는 클럭이 10–50배 느리고 캐시·coherency 구성이 다를 수 있습니다. D3, D4 같은 타이밍·coherency 버그는 실리콘에서 처음 보이는 경우가 많습니다.
- **테스트를 테스트한다**: D10처럼 "통과하는데 틀린" 테스트가 제일 위험합니다. fault injection으로 테스트가 실제로 실패할 수 있는지 확인하는 것이 validation 코드의 기본입니다.

## 5.1 면접관(HW 출신 Sr Manager)이 파고들 포인트

- **"How would you tell a silicon bug from a software bug here?"** → 같은 동작을 다른 경로로 관찰합니다. D3이면 JTAG로 DRAM 직접 읽기 vs CPU 읽기, D4면 AXI 트레이스로 descriptor fetch 시점 확인, 캐시를 끄거나 non-cacheable로 매핑해서 증상이 사라지는지 봅니다. *"I change one variable at a time — cache off, lower DMA clock, JTAG read of DRAM — and see which observation disagrees; that tells me whether the bug is in my code, the coherency setup, or the RTL."*
- **"Why didn't this show up on the FPGA?"** → FPGA는 엔진이 느리고(D4 순서 버그가 숨음), 캐시·coherency 설정이 다를 수 있고, 타이머가 느려 랩까지 오래 걸립니다(D5). *"The FPGA runs ten to fifty times slower, so ordering and wrap bugs that need a tight window often only appear at silicon speed — that's why I add stress and preload tests before first silicon."*
- **"How do you know your validation test itself is correct?"** → fault injection(D10 canary, 일부러 flip한 bit), 비대칭 패턴(D6), 반복 실행(D3)으로 **테스트가 실패할 수 있음**을 먼저 보입니다. *"Every test I write has a negative case: I inject a fault and prove the test catches it, otherwise a passing result means nothing."*
- **"How do you stop these from coming back across the RAP1 family?"** → 레지스터 헤더 생성 + `_Static_assert`, `volatile`/W1C 접근자 라이브러리, CI에서 호스트 sanitizer 빌드, 리뷰 체크리스트. *"I'd put the fixes in the infrastructure — generated register headers with access types, one accessor layer, sanitizer builds in CI — so the next chip variant inherits them."*
- **"What would a false failure like D5 cost in PVT or SLT?"** → 랜덤해 보이는 실패율이 PVT 코너 데이터에 섞여 실제 마진 문제와 구분이 안 되고, SLT에서는 good die를 버립니다. *"A timing bug in the test looks like a random yield loss, so it pollutes the PVT data and can scrap good parts in SLT — test-code bugs are expensive."*

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| Does `volatile` alone fix D4? | 아닙니다. `volatile`은 컴파일러가 접근을 생략·재배치하지 않게 할 뿐이고, Arm의 Normal memory에서는 CPU/interconnect가 store 순서를 바꿀 수 있습니다. `dma_wmb()`(`dmb oshst` 또는 `dsb st`)가 순서를 보장합니다. |
| If the DMA is coherent (ACE-Lite), what changes in D3? | clean/invalidate가 필요 없습니다. interconnect가 snoop해서 캐시와 DRAM을 맞춰 줍니다. 대신 coherent 경로가 실제로 켜져 있는지(AxCACHE/AxDOMAIN 신호, SMMU 속성) 확인하는 것이 bring-up 항목이 됩니다. |
| Why is D3's buffer required to be cache-line aligned? | invalidate는 라인 단위입니다. 버퍼가 라인 중간에서 시작하면 같은 라인의 이웃 변수(CPU가 쓴 dirty 데이터)까지 버려집니다. 그래서 DMA 버퍼는 64 B 정렬·64 B 배수 크기로 잡습니다. |
| How long can the D5 timeout be? | `(uint32_t)(now - start)`이므로 2^32 − 1 tick까지. 100 MHz면 약 42.9초. 더 길면 64-bit 카운터(Arm generic timer `CNTPCT`는 64-bit)를 씁니다. |
| How do you read a 64-bit counter on a 32-bit bus without tearing? | hi → lo → hi를 읽고 두 hi가 같을 때까지 반복합니다. 다르면 lo가 그 사이에 랩한 것입니다. HW가 lo를 읽을 때 hi를 latch하는 방식이면 lo를 먼저 읽습니다(데이터시트 확인). |
| In D2, what if the register is read-to-clear instead of W1C? | 읽는 것만으로 지워지므로 RMW의 **읽기**가 모든 pending을 지웁니다. 한 번 읽은 값을 저장해서 모든 비트를 처리해야 하고, 디버거 메모리 창으로 그 레지스터를 보면 상태가 바뀝니다(debug side effect). |
| How would you catch D10 on a target without ASan? | 영역 뒤에 canary를 두고 테스트 후 검사, MPU region으로 영역 밖을 no-access, 또는 JTAG data watchpoint를 `base + len_bytes`에 걸어 쓰는 순간 정지시킵니다. |

## 7. 자주 하는 실수

- D1을 `volatile uint32_t st = dma_status_read();`로 고침 → 지역 변수를 `volatile`로 만들어도 **레지스터는 한 번만 읽음**. 읽기 자체가 루프 안에 있어야 함
- D2를 `intc_write(intc_read() & bit)`로 고침 → TX가 pending이 아닐 때는 아무것도 안 쓰니 결과는 맞지만, 불필요한 읽기이고 read-to-clear 레지스터라면 다른 비트가 지워짐. 그냥 `bit`만 씀
- D3에서 invalidate를 DMA **전에만** 함 → 이 모델에서는 통과하지만 실제 Cortex-A는 DMA 도중 추측성 prefetch로 라인이 다시 채워질 수 있음. DONE 후에도 invalidate
- D4에서 barrier를 빼고 순서만 바꿈 → 이 모델은 통과하지만 실제 Arm Normal memory에서는 store가 재배치될 수 있음. 순서 + `wmb` 둘 다
- D5를 `now - start > timeout`으로 고침 → 랩은 해결됐지만 정확히 `timeout`인 순간 한 poll 늦음. `>=`
- D8을 `(1ull << width) - 1`로 고침 → 동작은 맞지만 32-bit 코어에서 64-bit shift 코드가 생김. 조건식이나 `0xFFFFFFFFu >> (32 - width)`가 깔끔
- D9를 `(uint64_t)(bytes * clk_mhz)`로 고침 → 이미 넘친 32-bit 값을 넓힐 뿐. 캐스트는 피연산자에
- D10을 `base + len_bytes / 4`로 고침 → 지금은 맞지만 타입이 `uint64_t *`로 바뀌면 다시 틀림. `sizeof *base`
