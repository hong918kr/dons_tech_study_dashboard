# J3 · Pick a Boot Slot (A/B Images with Rollback)

> **ID**: J3 · **Priority**: P1 · **Pattern**: header validation + CRC + state machine with persisted counters · **Time**: 30 min
> 파일: `stub.c`(여기서 풀기) · `solution.c`(정답) · 실행: `./run.sh J3` / `./run.sh J3 sol`

## 1. Problem

A fielded robot updates its MCU firmware over the air. The bootloader keeps **two slots** (A and B) in flash. A new image is always written into the slot that is *not* running, then booted on trial. If it never confirms that it is healthy, the bootloader must **roll back** to the old image.

Each slot is `SLOT_SIZE` (4096) bytes and starts with a 16-byte **little-endian** header, followed by the image. The image begins with a Cortex-M vector table.

```
off 0   magic       u32   0x544F4F42  (bytes 42 4F 4F 54 = "BOOT")
off 4   version     u32
off 8   image_size  u32   8 .. slot_size - 16
off 12  crc32       u32   CRC-32/IEEE over the image_size bytes after the header
off 16  image:  word0 = initial SP,  word1 = reset handler,  code ...
```

Slot base addresses: A = `0x08004000`, B = `0x08024000`, so `image_base = slot_base + 16`. RAM is `[0x20000000, 0x20020000]`.

**Part A** — `image_check()`: validate one slot in this order and fill `*out` on success.

| Return | When |
|---|---|
| `IMG_BAD_MAGIC` | magic is not `0x544F4F42` (erased flash `FF FF FF FF` lands here) |
| `IMG_BAD_SIZE` | `slot_size < 16`, or `image_size < 8` (no room for SP + reset), or `image_size > slot_size - 16` |
| `IMG_BAD_CRC` | `crc32(image, image_size)` differs from the header |
| `IMG_BAD_VECTOR` | SP outside RAM or not 4-byte aligned; reset handler without the Thumb bit (bit 0), or `reset & ~1` outside `[image_base, image_base + image_size)` |
| `IMG_OK` | all checks pass |

Parse the header with **explicit byte assembly** — do not cast the byte array to a struct.

**Part B** — `boot_select()`: a 4-byte `BootState` record (in EEPROM) survives resets.

- A slot is **usable** if it is not marked in `bad_mask` and `image_check() == IMG_OK`.
- If `pending` is a slot (an update installed but not yet confirmed):
  - pending slot not usable, or `attempts >= MAX_ATTEMPTS` (3) → mark it bad, `pending = NO_PENDING`, `attempts = 0`, then fall through to the normal rule.
  - otherwise `attempts++` and return the pending slot (trial boot).
- Normal rule: return the usable slot with the **highest version** (tie → `active`) and set `active` to it. No usable slot → `-1` (stay in the bootloader / DFU mode).
- Sanitize a corrupt record first (`active > 1` → 0, `pending` not 0/1/`NO_PENDING` → `NO_PENDING`).

**Part C** — `boot_confirm()`: the application calls it once it is healthy: `active = pending`, `pending = NO_PENDING`, `attempts = 0`. No-op if nothing is pending.

```c
#define SLOT_SIZE 4096u
#define HDR_SIZE 16u
#define IMG_MAGIC 0x544F4F42u
#define RAM_START 0x20000000u
#define RAM_END   0x20020000u
#define MAX_ATTEMPTS 3u
#define NO_PENDING 0xFFu
static const uint32_t SLOT_BASE[2] = { 0x08004000u, 0x08024000u };

typedef enum { IMG_OK, IMG_BAD_MAGIC, IMG_BAD_SIZE, IMG_BAD_CRC, IMG_BAD_VECTOR } ImgStatus;
typedef struct { uint32_t version, image_size, sp, reset; } ImgInfo;
typedef struct { uint8_t active; uint8_t pending; uint8_t attempts; uint8_t bad_mask; } BootState;

uint32_t  crc32(const uint8_t *p, size_t n);                                   /* given */
uint32_t  rd32le(const uint8_t *p);
ImgStatus image_check(const uint8_t *slot, size_t slot_size, uint32_t slot_base, ImgInfo *out);
int       boot_select(BootState *st, const uint8_t *slots[2], size_t slot_size);
void      boot_confirm(BootState *st);
```

### Example

```
slot A: version 1, size 512, crc 0x4AF3A0F3, SP 0x20020000, reset 0x08004051 -> IMG_OK
slot B: version 2, size 512, crc 0xB5EDD439, SP 0x20020000, reset 0x08024051 -> IMG_OK
state {active 0, pending none, attempts 0, bad 0x00}  -> boot_select = 1 (B is newer), active = 1

flip one bit at B image[300] -> crc32 = 0xEBD66D8D != 0xB5EDD439 -> IMG_BAD_CRC -> boot_select = 0

A = v2, B = v3 just installed, state {active 0, pending 1, attempts 0, bad 0x00}, B never confirms:
  boot 1 -> 1 (attempts 1)   boot 2 -> 1 (attempts 2)   boot 3 -> 1 (attempts 3)
  boot 4 -> 0, bad_mask = 0x02, pending none, attempts 0   (rollback; B is never tried again)
```

### Constraints

- No `malloc`. The slot buffers are plain `uint8_t` arrays (assume no alignment).
- `image_check` must never read outside `slot[0 .. slot_size)`, whatever the header says.
- `boot_select` runs once per reset; every change to `BootState` is a persisted write.

### Clarifying questions to ask (first 3 minutes)

1. Endianness of the header? Does the CRC cover the header, or only the image? Which CRC-32 variant?
2. Is the initial SP allowed to equal the end of RAM (full-descending stack starts *at* the top)?
3. Must the reset handler be inside the image, or just inside the slot?
4. When does `attempts` increase — before the jump, or after the app starts? Who clears it?
5. If both slots are valid with the same version, which wins? If none is valid, what do we do?
6. Can a pending image that was rolled back ever be retried (until a new image is written)?

## 2. Your workspace

빈 에디터처럼 아래 `stub.c`를 복사해서 시작하거나, 로컬에서 `./run.sh J3`로 바로 테스트하세요. 테스트 8묶음이 들어 있습니다(1 정상, 2 CRC, 3 magic, 4 size, 5 vector table, 6 3회 크래시 → 롤백, 7 두 번째 시도에서 confirm, 8 pending 손상 + 전부 무효 → -1). `make_image()`/`reseal()`은 테스트용 헬퍼라 손대지 않아도 됩니다.

<div class="code" data-src="stub.c"></div>

---

## 3. 힌트 (막힐 때 하나씩)

<details>
<summary>Hint 1 — rd32le 하나로 헤더 전부를 읽는다</summary>

`p[0] | p[1] << 8 | p[2] << 16 | p[3] << 24` (각각 `uint32_t`로 캐스트한 다음 shift). 이 함수 하나로 magic, version, size, crc, SP, reset 6개를 모두 읽습니다. `uint8_t`는 `int`로 승격되므로 `p[3] << 24`를 캐스트 없이 쓰면 `0x80` 이상에서 signed overflow(UB)가 납니다.

</details>

<details>
<summary>Hint 2 — 검사 순서가 곧 안전성</summary>

magic → **size** → CRC → vector 순서입니다. size를 먼저 확인해야 `crc32(img, size)`가 slot 밖을 읽지 않습니다. 헤더는 신뢰할 수 없는 입력이라 `0xFFFFFFFF` 같은 값이 그대로 들어올 수 있습니다(테스트 4). reset 범위는 `entry - img_base < size` 한 번의 unsigned 비교로 끝납니다. `entry < img_base`이면 뺄셈이 랩어라운드해서 큰 값이 되므로 자동으로 걸러집니다.

</details>

<details>
<summary>Hint 3 — attempts는 점프 "전에" 올린다</summary>

새 이미지가 부팅 중에 죽으면 다시 bootloader로 돌아올 때 코드가 아무것도 기록하지 못합니다. 그래서 **점프하기 전에** `attempts++`를 저장해 두고, 앱이 건강하면 `boot_confirm()`으로 지웁니다. 다음 부팅에서 `attempts >= 3`이면 "세 번 시도했는데 한 번도 confirm 못 했다" → bad 표시, 롤백합니다.

</details>

---

## 4. 풀이

### 4.1 핵심 아이디어 — 플래시 지도부터 그리기

테스트 1번의 두 슬롯을 바이트 단위로 그리면 이렇습니다. 헤더 4필드는 CRC 범위 밖이고, vector table부터 이미지 끝까지가 CRC 범위입니다.

<div class="chart" data-src="chart1.svg" data-caption="슬롯 A/B 레이아웃. 메모리에는 42 4F 4F 54 순서로 저장되고, little-endian으로 읽으면 0x544F4F42. 아래 노란 박스가 image_check()의 검사 순서"></div>

**struct 캐스팅을 쓰지 않는 이유** (면접에서 꼭 말할 것):
- **정렬(alignment)**: `uint8_t` 버퍼는 4바이트 정렬이 보장되지 않습니다. Cortex-M0, 또는 M3/M4의 `LDRD`/`LDM`에서 비정렬 접근은 HardFault입니다.
- **엔디언**: struct 캐스트는 CPU의 엔디언을 따릅니다. 바이트 조립은 파일 포맷의 엔디언을 코드에 명시하므로 호스트(PC 툴, 테스트)에서도 같은 결과입니다.
- **strict aliasing**: `uint8_t[]`를 `Header *`로 읽는 것은 C 표준상 UB이고, `-O2`에서 컴파일러가 읽기 순서를 바꿀 수 있습니다. `memcpy`로 struct에 복사하는 방법은 합법이지만 엔디언과 padding 문제는 그대로 남습니다.
- 대가는 거의 없습니다. GCC/Clang은 `rd32le` 패턴을 인식해서 little-endian 타깃에서는 `LDR` 한 개(또는 바이트 로드 4개)로 컴파일합니다.

### 4.2 부팅 상태 머신

`BootState` 4바이트가 상태 머신 전체입니다. `pending`이 있으면 TRIAL, 없으면 NORMAL입니다.

<div class="chart" data-src="chart2.svg" data-caption="NORMAL → (OTA 기록) → TRIAL → confirm이면 CONFIRMED, 3번 연속 confirm 실패면 ROLLBACK. 유효한 슬롯이 하나도 없으면 DFU(-1)"></div>

핵심은 **"앱이 confirm을 못 했다" = "죽었다"**로 해석한다는 점입니다. bootloader는 앱이 왜 죽었는지 알 필요가 없습니다. 크래시, 무한 루프(→ watchdog reset), 브라운아웃 모두 같은 경로(confirm 없이 리셋)로 들어옵니다.

### 4.3 손으로 따라가기 (trace)

**테스트 6 — B v3가 매번 크래시** (A = v2, B = v3, 둘 다 `IMG_OK`)

| 부팅 | 진입 시 상태 {active, pending, attempts, bad} | 판단 | 반환 | 저장된 상태 |
|---|---|---|---|---|
| 1 | {0, 1, 0, 0x00} | pending B usable, 0 < 3 → **trial** | 1 | {0, 1, 1, 0x00} |
| – | B 실행 중 크래시 (confirm 없음) | | | |
| 2 | {0, 1, 1, 0x00} | 1 < 3 → trial | 1 | {0, 1, 2, 0x00} |
| 3 | {0, 1, 2, 0x00} | 2 < 3 → trial | 1 | {0, 1, 3, 0x00} |
| 4 | {0, 1, 3, 0x00} | 3 ≥ 3 → **ROLLBACK**: bad \|= 0x02, pending 해제. usable = A만 | **0** | {0, none, 0, 0x02} |
| 5 | {0, none, 0, 0x02} | NORMAL: B(v3)가 더 새롭지만 bad → A | 0 | {0, none, 0, 0x02} |

B는 정확히 3번 실행됩니다(`MAX_ATTEMPTS`). 4번째 부팅에서 롤백합니다.

**테스트 7 — 두 번째 시도에서 confirm**

| 단계 | 상태 | 동작 |
|---|---|---|
| boot 1 | {0, 1, 0, 0} → {0, 1, 1, 0} | trial B, 크래시 |
| boot 2 | {0, 1, 1, 0} → {0, 1, 2, 0} | trial B, 이번엔 정상 |
| `boot_confirm` | → {1, none, 0, 0} | B가 active |
| boot 3 | {1, none, 0, 0} | NORMAL: B v3 > A v2 → 1, attempts는 0 그대로 |

**image_check 경계값 (테스트 4, 5, 슬롯 B, image_base = `0x08024010`, size 512)**

| 입력 | 계산 | 결과 |
|---|---|---|
| size = 0 / 7 | `< 8` | BAD_SIZE |
| size = 4081 / `0xFFFFFFFF` | `> 4096 − 16 = 4080` | BAD_SIZE (CRC 계산 전에 거름) |
| size = 4080 | 슬롯을 꽉 채움 | OK |
| reset `0x08024050` | bit0 = 0 (ARM 상태 → 즉시 UsageFault) | BAD_VECTOR |
| reset `0x08004051` | A용으로 링크된 이미지: `0x08004050 − 0x08024010` 랩어라운드 → 큰 값 | BAD_VECTOR |
| reset `0x08024211` | `0x08024210 − 0x08024010 = 0x200 = 512`, `512 < 512` 거짓 | BAD_VECTOR |
| reset `0x0802420F` | `0x1FE = 510 < 512` | OK |
| SP `0x20020004` / `0x2001FFFE` | RAM 밖 / 4-정렬 아님 | BAD_VECTOR |

### 4.4 정답 코드

<details>
<summary>💡 정답 코드 펼치기 (30분 풀어 본 다음에)</summary>

<div class="code" data-src="solution.c"></div>

</details>

**코드 읽는 포인트**
- `rd32le`의 각 바이트는 `(uint32_t)`로 캐스트한 뒤 shift합니다. `p[3] << 24`를 그대로 쓰면 `int` shift라 슬롯 B crc의 `0xB5 << 24`에서 UB가 납니다(UBSan이 잡음).
- `entry - img_base >= size` 한 줄이 "아래쪽 밖"과 "위쪽 밖"을 동시에 검사합니다. 랩어라운드 안전 시간 비교와 같은 트릭입니다.
- `attempts++`는 `return p` **바로 전에** 있습니다. 실제 펌웨어에서는 여기서 EEPROM/플래시 쓰기가 완료된 뒤 점프합니다.
- 롤백한 뒤 `ok[p] = false`로 두고 NORMAL 규칙으로 **fall through**합니다. 덕분에 "A도 깨져 있으면 -1"이 따로 코드 없이 처리됩니다.
- `boot_confirm`은 이미 confirm된 상태면 아무것도 쓰지 않습니다. 앱이 매 부팅마다 호출해도 EEPROM 수명을 깎지 않습니다.

### 4.5 복잡도

- `image_check`: 시간 **O(image_size)** (CRC가 지배), 추가 메모리 O(1).
- `boot_select`: 슬롯 2개 검사 → O(slot_size), 상태 4바이트. 테이블 없는 bitwise CRC는 바이트당 8회 루프라, 실제 MCU에서는 256-entry 테이블(1 KiB) 또는 하드웨어 CRC 주변장치(TM4C129x에는 CCM/CRC 모듈이 있음)를 씁니다.

---

## 5. 임베디드 관점으로 말할 것

- **면접관 맥락**: Jason은 Charles Machine Works에서 **TM4C129x용 bootloader와 공통 소프트웨어 라이브러리**를 개발했습니다. 현장에 나가 있는 로봇은 사람이 JTAG를 꽂아 줄 수 없으므로, OTA 업데이트가 실패해도 **반드시 부팅 가능한 이미지가 하나는 남는 것**이 bootloader의 존재 이유입니다. "A/B + trial boot + rollback"이 그 답이라고 말하면 됩니다.
- **헤더는 신뢰할 수 없는 입력**: 반쯤 쓰다 전원이 나간 슬롯, 지워진 슬롯(`0xFF`), 잘못된 슬롯 주소로 링크된 이미지가 모두 현실에서 일어납니다. 그래서 size를 CRC보다 먼저, vector table을 CRC 다음에 검사합니다. 테스트 5의 "A용 이미지를 B에 쓴 경우"는 position-dependent 링크 때문에 실제로 자주 나는 사고입니다.
- **VTOR 정렬 주의**: 이 문제는 단순하게 헤더 16바이트 바로 뒤에 vector table을 두었지만, `SCB->VTOR`는 테이블 크기 이상의 2의 거듭제곱 정렬(최소 128 B, IRQ가 많은 TM4C129x는 1 KiB 정도)이 필요합니다. 실제로는 MCUboot처럼 헤더를 `0x200`까지 패딩하거나 trailer 방식(이미지 끝에 메타데이터)을 씁니다. 이 점을 짚으면 실무 경험이 드러납니다.
- **주소 체계**: 예제 주소는 STM32 스타일(`0x0800_0000`)입니다. TM4C129x는 플래시가 `0x0000_0000`부터 시작하고 TivaWare boot_loader는 `APP_START_ADDRESS`로 앱 위치를 정합니다. 로직은 동일합니다.
- **상태 기록 = 플래시 쓰기**: `BootState`가 바뀔 때마다 EEPROM/플래시에 씁니다. 그래서 바뀔 때만 쓰고(`boot_confirm` no-op), 레코드에 CRC나 시퀀스 번호를 붙여 두 벌로 번갈아 쓰면 쓰다가 전원이 나가도 안전합니다(→ Follow-up).
- 관련 문제: 프레임 파싱 + CRC는 [E2](../E2_extract_packet/index.html), 비트 조작은 [T2-6](../T2-6_bit_ops/index.html).

## 6. Follow-up 질문과 답

| Follow-up (English) | 답 |
|---|---|
| What does the actual jump look like on Cortex-M? | IRQ 끄기 → bootloader가 켠 SysTick/주변장치 정지, NVIC enable/pending 전부 클리어 → `SCB->VTOR = image_base` → `DSB/ISB` → `MSP = word0` → `word1`로 branch. 아래 코드 참고. 앱의 reset handler는 `.data/.bss`를 다시 초기화하므로 bootloader의 전역 상태는 넘어가지 않습니다. |
| What if power is lost in the middle of writing the update? | 순서가 핵심입니다. ① 비활성 슬롯 지우기 → ② 이미지 본문 쓰기 → ③ 다시 읽어서 CRC 확인 → ④ **마지막에** 헤더(magic) 쓰기 → ⑤ `pending` 설정. 어느 단계에서 끊겨도 magic이 없거나 CRC가 틀려서 그 슬롯은 무시되고, 실행 중이던 슬롯은 건드리지 않았으므로 그대로 부팅됩니다. `BootState` 자체도 두 벌 + 시퀀스 번호 + CRC로 기록합니다. |
| Is CRC enough? What about authenticity? | CRC는 **우연한 손상**만 잡습니다. 공격자는 CRC를 다시 계산하면 그만입니다. 방산 로봇이면 SHA-256 해시 + **ECDSA P-256(또는 Ed25519) 서명**을 검증하고, 공개키는 bootloader(가능하면 OTP/ROM)에 둡니다. 서명 검증은 수십~수백 ms 걸리지만 부팅 때 한 번이면 됩니다. bootloader 자체는 쓰기 보호(flash protection)를 걸어 둡니다. |
| What is the watchdog's role in a trial boot? | 앱이 크래시가 아니라 **멈춰 버리면**(deadlock, 무한 루프) 리셋이 안 일어나서 롤백도 안 됩니다. 그래서 bootloader가 점프 전에 WDT를 켜고, 앱은 끌 수 없게 둡니다. 앱이 멈추면 WDT 리셋 → attempts가 쌓여 롤백됩니다. confirm은 "부팅 직후"가 아니라 **센서·통신·제어 루프가 실제로 돌기 시작한 뒤**에 해야 의미가 있습니다. |
| How do you stop an attacker from installing an old, vulnerable signed image? | **anti-rollback counter**: 서명된 헤더에 `security_version`을 넣고, 단조 증가 카운터(OTP fuse 또는 보호된 플래시)보다 작으면 거부합니다. 카운터는 새 이미지가 **confirm된 뒤에만** 올립니다(trial 중에 올리면 롤백할 곳이 사라짐). 이 문제의 `version`은 선택용일 뿐 보안 장치가 아닙니다. |
| How does this map to embedded Linux? | 같은 패턴입니다. U-Boot의 `bootcount`/`bootlimit`/`altbootcmd`가 `attempts`/`MAX_ATTEMPTS`/rollback이고, 사용자 공간이 부팅 성공 후 `bootcount`를 0으로(=confirm) 만듭니다. rootfs A/B 파티션은 RAUC, SWUpdate, Mender가 관리하고, 업데이트 툴이 U-Boot env(`BOOT_ORDER`, `BOOT_x_LEFT`)를 바꿉니다. MCU 쪽 대응물은 MCUboot(swap/test/confirm)입니다. |
| The BootState record itself gets corrupted. | 이 코드처럼 범위를 벗어난 값은 안전한 기본값으로 바꿉니다. 더 나아가 레코드에 CRC를 붙이고 두 벌을 번갈아 쓰면(ping-pong), 깨진 쪽은 버리고 시퀀스 번호가 큰 유효한 쪽을 씁니다. |

### 점프 코드 (Follow-up 1)

```c
typedef void (*entry_fn)(void);

static void jump_to_image(uint32_t image_base)          /* already validated by image_check() */
{
    uint32_t sp    = *(const volatile uint32_t *)image_base;        /* real flash: aligned */
    uint32_t reset = *(const volatile uint32_t *)(image_base + 4);

    __disable_irq();
    SysTick->CTRL = 0;                                   /* stop bootloader timers */
    for (unsigned i = 0; i < 8; i++) {                   /* disable + clear every IRQ */
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB->VTOR = image_base;                              /* needs table-size alignment */
    __DSB(); __ISB();
    __set_MSP(sp);                                       /* after this, no locals on stack */
    ((entry_fn)reset)();                                 /* bit0 = 1 keeps Thumb state */
    for (;;) { }                                         /* never returns */
}
```

`__set_MSP` 뒤에는 기존 스택이 사라지므로 `sp`, `reset`이 레지스터에 남아 있어야 합니다. 그래서 실무에서는 마지막 두 줄을 짧은 inline asm(`msr msp, r0; bx r1`)으로 쓰기도 합니다. 앱 쪽 startup 코드가 `__enable_irq()`로 PRIMASK를 다시 풉니다.

## 7. 자주 하는 실수

- `rd32le`에서 캐스트 없이 `p[3] << 24` → 바이트가 `0x80` 이상이면 signed overflow(UB). 슬롯 B의 crc 바이트 `39 D4 ED B5`(p[3] = `0xB5`)가 바로 그 경우입니다.
- **size 검사 전에 CRC 계산** → `image_size = 0xFFFFFFFF`에서 버퍼 밖 읽기(ASan이 잡음, 실제 MCU면 HardFault 또는 엉뚱한 메모리 CRC) → 테스트 4.
- reset 범위를 `entry >= img_base && entry <= img_base + size`로 써서 **끝 경계 포함** → `0x08024211` 통과 → 테스트 5. 또 Thumb bit을 지우지 않고 범위를 비교해서 마지막 halfword 경계를 틀림.
- `attempts`를 **점프 후 앱에서** 올림 → 앱이 그 코드까지 못 가고 죽으면 영원히 trial 반복(boot loop) → 테스트 6이 롤백하지 않음.
- 롤백할 때 `bad_mask`를 안 세움 → 다음 부팅에서 NORMAL 규칙이 "B가 더 새롭다"며 깨진 B를 다시 고름 → 테스트 6의 5번째 부팅 실패.
- 동점에서 `active`가 아니라 무조건 A(또는 B)를 고름 → 테스트 1 후반(active = 1 동점 → 1, active = 0 동점 → 0).
- pending 슬롯이 **이미 깨져 있는** 경우를 처리하지 않아 trial로 깨진 이미지에 점프 → 테스트 8.
