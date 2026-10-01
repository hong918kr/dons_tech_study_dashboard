# K1. 메모리 프로파일링 — 어디에 몇 바이트가 쓰이는지 측정하고 회귀를 막는 법

> **이 노트를 다 읽으면**: 실제로 링크된 Cortex-M4 ELF의 맵 파일을 스크립트로 집계해 "flash·RAM을 누가 먹는가"를 심볼·오브젝트 단위로 답할 수 있다 · 두 빌드의 size-diff 리포트와 예산 JSON으로 CI 메모리 게이트(PASS/FAIL)를 직접 만들 수 있다 · `-fstack-usage` + 디스어셈블 호출 그래프로 최악 스택을 계산하고 재귀·함수 포인터·ISR 프레임의 함정을 설명할 수 있다 · 계측 allocator·단편화 시뮬레이터·pool로 "힙을 왜 피하고, 쓴다면 어떻게 증명하나"에 숫자로 답하고, 호스트에서 torch/ORT 프로세스의 RSS가 왜 모델 크기의 수백 배인지 `vmmap`·`footprint`로 분해할 수 있다
> **JD 연결**: "Profile and optimize memory usage, power consumption, real-time performance" · study_prep_list K1 행 — map 파일 읽기, stack/heap 분석, arena 사용량, peak memory 측정
> **Don 기준 난이도**: 링커 맵, `.bss`/`.data`, 스택 overflow 디버깅, 텔레메트리 카운터는 SSD 펌웨어에서 매일 한 일 / 그걸 **도구로 자동화해 CI에 거는 법**, 호출 그래프 기반 최악 스택 계산, 단편화를 숫자로 보이는 법, ML 프레임워크 프로세스의 메모리 해부는 새로 배움
> **선행 노트**: D2 (메모리 계산 — 추정기, `size -m`, stack painting), F2 (TFLM arena, `arena_used_bytes`), F7 (섹션·링커 스크립트·LMA/VMA·`llvm-size`), J1 (정적 메모리 지도, `static_assert`), I1 (메모리 예산), I5 (RSS로 잰 메모리의 함정), H7 (fleet 텔레메트리 모니터링)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 추정은 D2에서 했다. 이 노트는 "측정"과 "감시"다

D2에서는 모델을 읽어 가중치·활성값·peak·arena를 **계산**했다. 하지만 제품 펌웨어의 메모리는 모델만이 아니다. RTOS, BLE 스택, 오디오 DMA 버퍼, 로그, 텔레메트리, 부트로더 공유 영역, 그리고 매주 누군가가 추가하는 기능이 있다. 메모리가 터지는 건 대부분 이런 식이다:

- 모델 팀이 정확도 +0.8 %짜리 새 모델을 줬다. 가중치 +8 KB, arena +8 KB. 아무도 합계를 안 봤다.
- 디버그 로그 한 줄을 추론 태스크에 넣었다. 스택 +900 B (D2 예제 12의 printf). 1년 뒤 특정 입력에서만 hard fault.
- 이벤트 문자열을 `malloc`으로 잡았다. 3일 연속 착용 후 8 KB 버퍼 할당이 실패한다 — 총 여유는 10 KB인데.

이 세 사고를 막는 건 계산이 아니라 **측정 습관**이다: (1) 어디에 몇 바이트가 있는지 **도구로** 뽑고, (2) 빌드마다 **기준선과 비교**하고, (3) 런타임의 최댓값(high-water)을 **기기에서 세서** 올려 보낸다. Don이 SSD 펌웨어에서 NAND 버퍼 풀의 최소 여유를 텔레메트리로 올리고, 양산 전 margin sign-off를 했던 것과 똑같은 일을 ML 펌웨어에 하는 것이다.

### 0.2 메모리 회계 지도 — 무엇을 무엇으로 재나

```svg
<svg viewBox="0 0 680 410" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">MCU 메모리 회계 지도 — 위 = 낮은 주소 (막대 길이는 비례 아님)</text> <text x="100" y="44" font-size="13" text-anchor="middle">FLASH</text> <rect x="40" y="52" width="120" height="26" fill="#888" fill-opacity="0.5" stroke="currentColor"/> <rect x="40" y="78" width="120" height="18" fill="#3f9a6b" fill-opacity="0.6" stroke="currentColor"/> <rect x="40" y="96" width="120" height="64" fill="#4a7bd0" fill-opacity="0.7" stroke="currentColor"/> <rect x="40" y="160" width="120" height="10" fill="#e08a3c" fill-opacity="0.8" stroke="currentColor"/> <rect x="40" y="170" width="120" height="22" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="170" y="69" font-size="12">벡터 테이블 + .text (코드)</text> <text x="170" y="91" font-size="12">.rodata (LUT · 문자열)</text> <text x="170" y="132" font-size="12">모델 가중치 (.model)</text> <text x="170" y="169" font-size="12">.data 초기값 원본</text> <text x="170" y="186" font-size="12">여유</text> <line x1="380" y1="52" x2="380" y2="192" stroke="currentColor"/> <line x1="372" y1="52" x2="380" y2="52" stroke="currentColor"/><line x1="372" y1="192" x2="380" y2="192" stroke="currentColor"/> <text x="392" y="112" font-size="12">정적 — 링크 때 확정</text> <text x="392" y="130" font-size="12">맵 파일 → fwsize · sizediff (2·3절)</text> <text x="100" y="216" font-size="13" text-anchor="middle">SRAM</text>
<rect x="40" y="224" width="120" height="10" fill="#e08a3c" fill-opacity="0.8" stroke="currentColor"/> <rect x="40" y="234" width="120" height="56" fill="#e08a3c" fill-opacity="0.45" stroke="currentColor"/> <rect x="40" y="290" width="120" height="20" fill="#3f9a6b" fill-opacity="0.6" stroke="currentColor"/> <rect x="40" y="310" width="120" height="24" fill="#888" fill-opacity="0.5" stroke="currentColor"/> <rect x="40" y="334" width="120" height="20" fill="none" stroke="currentColor" stroke-dasharray="4 3"/> <rect x="40" y="354" width="120" height="40" fill="#d0564a" fill-opacity="0.35" stroke="currentColor"/> <text x="170" y="233" font-size="12">.data (실행용 사본)</text>
<text x="170" y="266" font-size="12">.bss: tensor arena</text> <text x="170" y="304" font-size="12">.bss: DMA · 센서 링버퍼</text> <text x="170" y="326" font-size="12">pool / heap</text> <text x="170" y="348" font-size="12">여유</text> <text x="170" y="378" font-size="12">스택 (MSP + task별) ↑ 자람</text> <line x1="380" y1="224" x2="380" y2="310" stroke="currentColor"/> <line x1="372" y1="224" x2="380" y2="224" stroke="currentColor"/><line x1="372" y1="310" x2="380" y2="310" stroke="currentColor"/> <text x="392" y="252" font-size="12">정적 크기: 맵 파일 + 심볼 예산 (2·8절)</text> <text x="392" y="270" font-size="12">arena 안쪽: arena_used_bytes (F2 · 7절)</text>
<text x="392" y="288" font-size="12">DMA 버퍼: 정렬 · 배치 영역 확인 (F7)</text> <text x="392" y="326" font-size="12">동적: 계측 allocator · pool 통계 (5절)</text> <line x1="380" y1="354" x2="380" y2="394" stroke="currentColor"/> <line x1="372" y1="354" x2="380" y2="354" stroke="currentColor"/><line x1="372" y1="394" x2="380" y2="394" stroke="currentColor"/> <text x="392" y="370" font-size="12">정적 최악: .su + 호출 그래프 (4절)</text> <text x="392" y="388" font-size="12">실측 high-water: painting (D2) · 텔레메트리 (7절)</text>
</svg>
```

그림 1 — MCU에서 바이트가 사는 곳과, 각각을 재는 도구. 위쪽 FLASH와 SRAM의 `.data`/`.bss`는 링커가 이미 계산해 놓은 **정적** 숫자라 맵 파일만 읽으면 된다. 스택과 힙은 **실행 경로에 따라 달라지는 동적** 숫자라 정적 분석(최악)과 런타임 high-water(실측)를 둘 다 본다. 호스트·SoC 프로세스(6절)는 여기에 가상 메모리·공유 라이브러리·압축 메모리·GPU/NPU 공유 버퍼라는 층이 더 붙는다.

### 0.3 이 노트의 경계 — 이미 다른 노트에 있는 것

| 주제 | 이미 있는 곳 | K1에서 하는 것 |
|---|---|---|
| 가중치·활성값·peak·arena 손계산, torch.fx 추정기 | D2 2~5절 | 안 반복. 추정값은 "예산"으로만 쓴다 |
| `tracemalloc` vs `ru_maxrss`, op별 할당 표 | D2 6.1 | **시간축 타임라인**과 peak 분해 (6.2절) |
| `size -m`, 섹션 개념, stack painting C 예제 | D2 6.2~6.3 | painting은 요약만. **정적 최악 스택 계산** 도구 (4절) |
| 링커 스크립트·LMA/VMA, 맵 파일 형식(예시), `llvm-size` | F7 4·8절 | **실제로 링크한 ELF와 진짜 맵 파일**을 도구로 집계 (1~3절) |
| `arena_used_bytes`, RecordingMicroAllocator | F2 | 텔레메트리로 내보내는 법만 (7절) |
| 정적 메모리 지도 + `static_assert` | J1, I1 10절 | 빌드 산출물 기반 **CI 게이트** (8절) |

### 0.4 실습 환경과 정직한 한계

F7에서는 "이 Mac에 ARM 링커가 없다"고 했다. 그런데 J4에서 설치한 Rust 툴체인 안에 **`rust-lld`**(LLVM의 `ld.lld`)가 들어 있다. `-flavor gnu`로 부르면 GNU ld 문법의 링커 스크립트를 받는 ELF 링커가 된다. 이 노트는 그걸로 **C로 짠 Cortex-M4 펌웨어를 실제로 링크**해서, 진짜 ELF와 진짜 맵 파일을 분석한다.

| 도구 | 결과 | 이 노트에서 한 일 |
|---|---|---|
| Apple clang 21 `--target=thumbv7em-none-eabihf` | ✅ | Cortex-M4F 오브젝트 컴파일 |
| `-fstack-usage` (`.su` 파일) | ✅ | 함수별 스택 프레임 (4절) |
| `-fstack-size-section` | ✅ (섹션 생성됨) | 읽을 `llvm-readobj`가 Xcode에 없어 사용 안 함 |
| `-Wframe-larger-than=N` | ✅ | 빌드 때 큰 프레임 경고 |
| GCC 전용 `-Wstack-usage=`, `-fcallgraph-info` | ❌ clang이 모름 | 호출 그래프는 디스어셈블에서 직접 뽑는다 |
| `rust-lld -flavor gnu` (LLD 23.1.1) | ✅ (`DYLD_LIBRARY_PATH` 필요) | 링커 스크립트 + `-Map` + `--gc-sections` |
| `xcrun llvm-size` / `llvm-nm` / `llvm-objdump` | ✅ | 섹션·심볼·디스어셈블 |
| `-fsanitize=address` + `detect_leaks=1` | ❌ "not supported on this platform" | 대신 `leaks` 사용 |
| `leaks --atExit`, `heap`, `vmmap`, `footprint` | ✅ sudo 없이 (내 프로세스) | 5·6절 |
| `.venv/bin/python` torch 2.8, onnxruntime 1.19.2 | ✅ | `psutil`은 없음 → RSS는 `ps -o rss=`로 |

링크된 ELF는 실행하지 않는다(보드가 없다). 크기·스택 분석은 **바이너리를 읽는 정적 분석**이라 실행이 필요 없고, 동적인 부분(힙, 프레임워크)은 호스트에서 잰다.

---

## 1. 실습용 미니 펌웨어 — 진짜로 링크된 Cortex-M4 ELF

### 1.1 구성 — v1(KWS만)과 v2(제스처 기능 추가)

"예를 들어 Hark 같은 웨어러블이라면" 항상 켜진 KWS(wake word)가 있고, 다음 릴리스에서 IMU 제스처 인식을 추가한다고 하자. 두 빌드를 만들고 그 차이를 도구로 잡는 것이 이 노트 전체의 실습이다.

| 파일 | 내용 | 메모리에서의 역할 |
|---|---|---|
| `startup.c` | 벡터 테이블, `Reset_Handler`(`.data` 복사·`.bss` 0) | `.isr_vector`, 코드 |
| `audio.c` | 마이크 ping-pong DMA 버퍼, Hann LUT, mel 경계, 특징 추출 | `.bss` 2 KB, `.rodata` 1 KB, **스택 1 KB** |
| `kws.c` | KWS arena(v1 48 KB, v2 56 KB), requant 계수 | `.bss` arena, `.data` |
| `kernels.c` | `conv_s8`(im2col 열을 스택에), `dense_s8`, **재귀** quicksort | 코드, 스택 |
| `log.c` | UART 로그 한 줄 (스택 128 B 버퍼) | 스택 |
| `main.c` | 메인 루프, 검출 콜백은 **전역 함수 포인터** | 간접 호출 |
| `gesture.c` (v2) | 제스처 arena 16 KB, IMU 링버퍼 6 KB, 추론 | `.bss`, 스택 384 B |
| `models.S` | `.incbin`으로 모델 blob (KWS v1 40 KB, v2 48 KB, 제스처 24 KB) | `.model` (flash) |

모델 파일은 크기만 의미가 있으므로 `/dev/urandom`에서 잘라 만들었다.

### 1.2 소스 — 메모리 거동이 드러나는 부분

각 파일은 10~20줄이다. 4·5절에서 "범인"으로 잡히는 코드를 중심으로 싣는다.

```c
/* audio.c — 특징 추출: 창 버퍼 1 KB를 스택에 잡는다 (4절에서 범인으로 잡힌다) */
#include "fw.h"
#define FRAME 512
#define NMEL 40
__attribute__((aligned(32))) int16_t g_audio_dma[2][FRAME];   /* ping-pong, DMA가 채운다 */
const int16_t g_hann[FRAME] = {0, 1, 2};                        /* Hann 창 LUT */
const uint8_t g_mel_bins[NMEL][2] = {{1, 3}, {2, 5}};          /* mel 필터 경계 */
volatile uint32_t g_audio_ready;
void audio_dma_irq(void) { g_audio_ready ^= 1u; }
void audio_features(const int16_t *pcm, int8_t *feat) {
    int32_t win[FRAME / 2];                                      /* 스택 1 KB */
    for (int i = 0; i < FRAME / 2; i++) win[i] = (pcm[i] * g_hann[i]) >> 15;
    for (int m = 0; m < NMEL; m++) {
        int32_t e = 0;
        for (int b = g_mel_bins[m][0]; b < g_mel_bins[m][1]; b++) e += win[b] * win[b];
        feat[m] = (int8_t)(e >> 24);
    }
}
```

```c
/* kernels.c (발췌) — conv_s8은 im2col 열 int8_t col[64]를 스택에, dense_s8은 단순 MAC 루프 */
/* top-k를 재귀 quicksort로 고른다 — 재귀는 정적 스택 분석의 적 */
void qsort_scores(int32_t *a, int lo, int hi) {
    if (lo >= hi) return;
    int32_t p = a[(lo + hi) / 2]; int i = lo, j = hi;
    while (i <= j) {
        while (a[i] > p) i++;
        while (a[j] < p) j--;
        if (i <= j) { int32_t t = a[i]; a[i] = a[j]; a[j] = t; i++; j--; }
    }
    qsort_scores(a, lo, j); qsort_scores(a, i, hi);
}
```

나머지는 짧아서 말로 적는다. `kws.c`는 `__attribute__((aligned(16))) static int8_t g_kws_arena[KWS_ARENA_KB * 1024]`(기본 48, v2에서 `-DKWS_ARENA_KB=56`), 초기값 있는 `int32_t g_kws_quant_mult[16]`(`.data`), 그리고 스택에 `int32_t scores[12]`를 두고 `audio_features → conv_s8 × 12 → dense_s8 → qsort_scores`를 부르는 `kws_infer()`다. `main.c`는 무한 루프에서 `kws_infer()`(v2는 `gesture_infer()`도)를 부르고, 검출되면 **전역 함수 포인터** `event_cb_t g_on_detect = log_event;`를 통해 콜백한다(4.3절에서 간접 호출로 나타난다). `gesture.c`(v2만)는 16 KB 제스처 arena, `int16_t g_imu_ring[3][1024]`, IMU ISR, 그리고 스택에 `int8_t window[384]`를 잡는 `gesture_infer()`다. `fw.h`는 함수 원형과 `KMAX 64`, `log.c`의 `log_event()`는 스택에 `char line[128]`을 잡아 태그와 숫자를 찍고 UART 레지스터 흉내(`volatile char`)로 내보낸다. `startup.c`는 F7 4.5절의 축소판으로 `Reset_Handler`가 `.data`를 `_sidata`에서 복사하고 `.bss`를 0으로 채운 뒤 `main()`을 부르며, 벡터 테이블 `g_vectors[32]`(`.isr_vector` 섹션)의 16·17번에 `audio_dma_irq`, `imu_irq`를 둔다. `rt.c`는 libc가 없으니 `memcpy`/`memset`을 바이트 루프로 직접 제공하고(이 빌드에서는 아무도 안 불러서 gc가 지운다), `imu_stub.c`는 v1용 빈 `void imu_irq(void) {}`, `models.S`는 F7 3.3절의 `.incbin` 방식 그대로 `g_kws_model`을 `.model.kws` 섹션에, v2에서는 `#ifdef FEATURE_GESTURE`로 `g_gesture_model`을 `.model.gesture` 섹션에 넣는다(`.size`도 붙인다 — F7에서 본 "크기 0 심볼" 함정 방지).

### 1.3 링커 스크립트

```text
/* link.ld — 예시 MCU: 512 KB flash, 128 KB SRAM (Cortex-M4F) */
MEMORY {
  FLASH (rx)  : ORIGIN = 0x08000000, LENGTH = 512K
  RAM   (rwx) : ORIGIN = 0x20000000, LENGTH = 128K
}
STACK_SIZE = 8K;
ENTRY(Reset_Handler)
SECTIONS {
  .isr_vector : { KEEP(*(.isr_vector)) } > FLASH
  .text   : { *(.text .text.*) } > FLASH
  .rodata : { *(.rodata .rodata.*) . = ALIGN(4); } > FLASH
  .model  : { KEEP(*(.model.*)) } > FLASH
  .data   : { _sdata = .; *(.data .data.*) . = ALIGN(4); _edata = .; } > RAM AT > FLASH
  _sidata = LOADADDR(.data);
  .bss (NOLOAD) : { _sbss = .; *(.bss .bss.* COMMON) . = ALIGN(4); _ebss = .; } > RAM
  .stack (NOLOAD) : { . = ALIGN(8); _sstack = .; . += STACK_SIZE; _estack = .; } > RAM
  ASSERT(_estack <= ORIGIN(RAM) + LENGTH(RAM), "RAM overflow: stack does not fit")
}
```

스택을 `.stack` 섹션으로 **명시적으로 예약**한 것이 포인트다. 이렇게 하면 스택 8 KB가 맵 파일과 `size` 출력에 나타나고, RAM이 모자라면 `ASSERT`가 링크를 실패시킨다. "스택은 RAM 끝에서 알아서 자란다"로 두면 정적 데이터가 늘어날 때 스택 공간이 **조용히** 줄어든다.

### 1.4 빌드 (예제 1)

무엇을 확인하는 코드인지: 진짜 ELF 링커로 v1·v2를 빌드하고, 각 빌드의 `text/data/bss`를 본다.

```sh
#!/bin/bash
# build.sh — 사용: ./build.sh <출력폴더> <kws 모델 파일> [추가 CFLAGS...]
set -e
OUT=$1; MODEL=$2; shift 2
T=<노트폴더>/.tools/rustup/toolchains/stable-aarch64-apple-darwin
LLD="env DYLD_LIBRARY_PATH=$T/lib $T/lib/rustlib/aarch64-apple-darwin/bin/rust-lld -flavor gnu"
CF="--target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard
    -Os -ffreestanding -ffunction-sections -fdata-sections -fstack-usage -Wall -Wextra $*"
mkdir -p $OUT && cp $MODEL $OUT/kws.bin && cp gesture.bin $OUT/
SRCS="startup.c rt.c kernels.c audio.c kws.c log.c main.c"
case "$*" in *FEATURE_GESTURE*) SRCS="$SRCS gesture.c";; *) SRCS="$SRCS imu_stub.c";; esac
for s in $SRCS; do cc $CF -c $s -o $OUT/${s%.c}.o; done
(cd $OUT && cc $CF -c ../models.S -o models.o)
$LLD -T link.ld --gc-sections -Map=$OUT/fw.map -o $OUT/fw.elf $OUT/*.o
xcrun llvm-size $OUT/fw.elf
```

```sh
./build.sh v1 kws.bin
./build.sh v2 kws_v2.bin -DFEATURE_GESTURE -DKWS_ARENA_KB=56
```

```text
   text	   data	    bss	    dec	    hex	filename
  43052	     68	  59432	 102552	  19098	v1/fw.elf
   text	   data	    bss	    dec	    hex	filename
  75988	     68	  90168	 166224	  28950	v2/fw.elf
```

출력에서 볼 것:

- 컴파일 경고 0개, 링크 성공. `rust-lld`는 `libLLVM.dylib`을 찾아야 해서 `DYLD_LIBRARY_PATH`를 툴체인의 `lib`로 줘야 한다(안 주면 `Library not loaded: @rpath/libLLVM.dylib`로 죽는다 — 실측).
- `-fstack-usage` 때문에 오브젝트마다 `.su` 파일이 `-o` 경로 옆에 생긴다(4절에서 쓴다).
- `size`의 세 숫자를 F7 규칙으로 읽으면 v1은 flash `text + data = 43,120 B`, RAM `data + bss = 59,500 B`. `bss`에는 `.stack`(NOLOAD) 8 KB도 들어 있다.
- v1 → v2: flash +32,936 B, RAM +30,736 B. **이 두 숫자만으로는 무엇이 늘었는지 모른다.** 그래서 맵 파일을 읽는다.

### 1.5 진짜 맵 파일 읽기 — ld.lld 형식

F7 8.1절의 맵은 GNU ld 형식의 **예시**였다. 이번엔 실제 링크 출력이다. ld.lld의 맵은 열이 고정되어 있어 사람과 스크립트 둘 다 읽기 쉽다.

```text
     VMA      LMA     Size Align Out     In      Symbol
       0        0        0     1 STACK_SIZE = 8K
 8000000  8000000       80     4 .isr_vector
 8000000  8000000       80     4         v1/startup.o:(.isr_vector)
 8000000  8000000       80     1                 g_vectors
 ...
20000000  800a830       44     4 .data
20000000  800a830        0     1         _sdata = .
20000000  800a830       40     4         v1/kws.o:(.data.g_kws_quant_mult)
20000000  800a830       40     1                 g_kws_quant_mult
20000040  800a870        4     4         v1/main.o:(.data.g_on_detect)
20000040  800a870        4     1                 g_on_detect
20000044  800a874        0     1         . = ALIGN(4)
20000044  800a874        0     1         _edata = .
20000044  800a874        0     1 _sidata = LOADADDR(.data)
20000060 20000060     c824    32 .bss
20000060 20000060        0     1         _sbss = .
20000060 20000060        4     4         v1/audio.o:(.bss.g_audio_ready)
20000060 20000060        4     1                 g_audio_ready
20000080 20000080      800    32         v1/audio.o:(.bss.g_audio_dma)
20000080 20000080      800     1                 g_audio_dma
20000880 20000880     c000    16         v1/kws.o:(.bss.g_kws_arena)
20000880 20000880     c000     1                 g_kws_arena
2000c880 2000c880        1     1         v1/log.o:(.bss.g_uart_tx)
2000c880 2000c880        1     1                 g_uart_tx
2000c881 2000c881        3     1         . = ALIGN(4)
2000c884 2000c884        0     1         _ebss = .
```

읽는 법:

- **들여쓰기 깊이가 의미**다. 0칸 = 출력 섹션(`.bss`), 8칸 = 입력 섹션(`kws.o:(.bss.g_kws_arena)` — 어느 오브젝트의 어느 섹션), 16칸 = 그 안의 심볼. 스크립트가 이 규칙 하나로 파싱한다(2절).
- **Size는 16진수**다. `c000` = 49,152 B = arena 48 KB, `800` = 2,048 B = DMA 버퍼, `c824` = 51,236 B = `.bss` 전체.
- `.data` 줄의 **VMA `20000000`과 LMA `800a830`이 다르다**. 초기값은 flash `0x0800_A830`에 구워지고 부팅 때 RAM `0x2000_0000`으로 복사된다(F7 4.2절). 그래서 `.data`는 flash와 RAM에 **두 번** 센다.
- `.bss` 시작이 `0x2000_0044`가 아니라 `0x2000_0060`이다. `g_audio_dma`의 `aligned(32)` 때문에 출력 섹션 자체가 32 B 정렬(`Align` 열 = 32)이 되어 `.data`와의 사이에 28 B가 비었다(이 틈은 어느 섹션에도 속하지 않아 `size`도 안 센다). `.bss` **안에서도** `g_audio_ready`(4 B, `0x60`) 다음 `g_audio_dma`가 `0x80`에서 시작해 28 B가 비는데, 이 바이트는 `.bss` 크기에는 들어가지만 **맵 파일에 줄로 안 나온다** — 2절 도구가 이걸 "gap"으로 따로 센다.
- `g_on_detect`(4 B)가 `.data`에 있다. 함수 포인터를 초기값과 함께 전역으로 두었기 때문이다.

---

## 2. 정적 메모리 분석 — 맵 파일을 도구로 집계한다

### 2.1 세 가지 질문

정적 메모리 분석은 항상 이 세 질문에 답하는 일이다.

1. **무엇이 큰가** — 심볼 top-N. "flash의 97 %가 모델 두 개" 같은 답.
2. **누가 넣었나** — 오브젝트(파일)·라이브러리·컴포넌트 단위 합계. "BLE 스택이 120 KB" 같은 답. 팀 단위 예산은 이 축으로 잡는다.
3. **지난번보다 왜 커졌나** — 두 빌드의 diff (3절).

`size`는 1번도 못 한다. `nm --size-sort`는 1번은 하지만 2번(오브젝트)과 정렬 틈을 모른다. **맵 파일은 셋 다 가능**하다: 입력 섹션마다 오브젝트 이름이 붙어 있기 때문이다.

### 2.2 `fwsize.py` — 맵 파일 집계기 (예제 2)

무엇을 확인하는 코드인지: ld.lld 맵을 들여쓰기 규칙으로 파싱해 입력 섹션 목록을 만들고, flash/RAM 합계(LMA까지 고려), 오브젝트×섹션 합계, 심볼 top-N을 낸다. 합계가 `llvm-size`와 **바이트 단위로 일치**하는지가 도구의 검증이다.

```python
"""fwsize.py — ld.lld 맵 파일을 읽어 flash/RAM을 오브젝트·섹션·심볼 단위로 집계한다."""
import re, sys, collections
ROW = re.compile(r"^\s*([0-9a-f]+)\s+([0-9a-f]+)\s+([0-9a-f]+)\s+(\d+) (\s*)(.*)$")
FLASH, RAM = (0x08000000, 0x08080000), (0x20000000, 0x20020000)   # link.ld의 MEMORY와 같게

def region(addr):
    return "flash" if FLASH[0] <= addr < FLASH[1] else "ram" if RAM[0] <= addr < RAM[1] else None

def parse_map(path):
    """반환: 입력 섹션 목록 [(out, obj, insec, sym, vma, lma, size)]"""
    rows, out, cur, outs = [], None, None, {}
    for line in open(path):
        m = ROW.match(line)
        if not m: continue
        vma, lma, size = (int(m.group(i), 16) for i in (1, 2, 3))
        depth, text = len(m.group(5)), m.group(6)
        if depth == 0: out = text; outs[out] = (vma, lma, size)     # 출력 섹션 (.text, .bss …)
        elif depth == 8 and ":(" in text:                           # 입력 섹션: obj:(.text.foo)
            obj, insec = text.split(":(", 1)
            cur = [out, obj.split("/")[-1], insec.rstrip(")"), insec.rstrip(")"), vma, lma, size]
            rows.append(cur)
        elif depth == 8 and text.startswith(". = ALIGN") and size:  # 정렬 패딩
            rows.append([out, "(padding)", "fill", "fill", vma, lma, size])
        elif depth == 8 and text.startswith(". +=") and size:       # 링커 스크립트 예약 (스택)
            rows.append([out, "(linker)", out, out, vma, lma, size])
        elif depth == 16 and cur and not text.startswith("$"):      # 심볼 이름이 있으면 붙인다
            cur[3] = text
    for out, (vma, lma, size) in outs.items():                      # 섹션 안 정렬 틈 (맵에 줄이 없다)
        gap = size - sum(r[6] for r in rows if r[0] == out)
        if gap > 0: rows.append([out, "(padding)", "gap", "gap", vma, lma, gap])
    return [r for r in rows if region(r[4])]

def usage(rows):
    """flash = VMA가 flash인 것 + .data처럼 LMA만 flash인 것(초기값 원본). RAM = VMA가 RAM."""
    tot = collections.Counter()
    for out, obj, insec, sym, vma, lma, size in rows:
        tot[region(vma)] += size
        if region(vma) == "ram" and region(lma) == "flash": tot["flash"] += size
    return tot

if __name__ == "__main__":
    rows, top = parse_map(sys.argv[1]), int(sys.argv[2]) if len(sys.argv) > 2 else 8
    u = usage(rows)
    print(f"flash {u['flash']:7,d} B   ram {u['ram']:7,d} B")
    by_obj = collections.Counter()
    for r in rows: by_obj[(r[1], r[0])] += r[6]
    print("\n-- 오브젝트 × 출력 섹션 --")
    for (obj, out), s in by_obj.most_common(top): print(f"{obj:<12} {out:<11} {s:7,d}")
    print(f"\n-- 심볼 top {top} --")
    for r in sorted(rows, key=lambda r: -r[6])[:top]:
        print(f"{r[3]:<20} {r[0]:<11} {region(r[4]):<5} {r[6]:7,d}  ({r[1]})")
```

```sh
.venv/bin/python fwsize.py fw/v1/fw.map 8
```

```text
flash  43,120 B   ram  59,500 B

-- 오브젝트 × 출력 섹션 --
kws.o        .bss         49,152
models.o     .model       40,960
(linker)     .stack        8,192
audio.o      .bss          2,052
audio.o      .rodata       1,104
kernels.o    .text           252
log.o        .text           182
audio.o      .text           130

-- 심볼 top 8 --
g_kws_arena          .bss        ram    49,152  (kws.o)
g_kws_model          .model      flash  40,960  (models.o)
.stack               .stack      ram     8,192  ((linker))
g_audio_dma          .bss        ram     2,048  (audio.o)
g_hann               .rodata     flash   1,024  (audio.o)
log_event            .text       flash     182  (log.o)
g_vectors            .isr_vector flash     128  (startup.o)
qsort_scores         .text       flash     118  (kernels.o)
```

출력에서 볼 것:

- **`llvm-size`와 정확히 일치**: flash 43,120 = `text 43,052 + data 68`, RAM 59,500 = `data 68 + bss 59,432`. 처음 버전은 flash 43,106 / RAM 59,472로 14 B, 28 B가 모자랐다. 원인은 1.5절에서 본 **맵에 줄이 없는 정렬 틈**(`.text` 안 함수 사이 4 B 정렬 14 B, `.bss` 안 `aligned(32)` 앞 28 B)이었다. 출력 섹션 크기 − 입력 섹션 합 = gap 행을 추가해서 맞췄다. **도구를 만들면 먼저 기존 도구와 합계를 맞춰라** — 회계의 기본이다.
- 손으로 검산: flash = `.isr_vector 0x80(128) + .ARM.exidx 0x10(16) + .text 0x346(838) + .rodata 0x456(1,110) + .model 0xa000(40,960) + .data 원본 0x44(68)` = 43,120. RAM = `.data 68 + .bss 0xc824(51,236) + .stack 0x2004(8,196)` = 59,500.
- RAM의 83 %가 arena 하나(49,152 / 59,500), flash의 95 %가 모델 하나(40,960 / 43,120). **ML 펌웨어의 전형적인 모양**이다(F7 그림 6과 같은 결론을 이번엔 링크된 바이너리로 확인).
- 코드는 다 합쳐 838 B다. 실제 제품에서는 TFLM·RTOS·BLE가 수십~수백 KB를 더하므로 **오브젝트/라이브러리 축의 표가 훨씬 길어진다** — 그때 이 도구의 2번 질문이 쓸모 있다.

### 2.3 그림으로 — 어디가 큰가

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="32" font-size="13">FLASH: 사용 76,056 B / 용량 524,288 B (14.5%)</text> <rect x="20.0" y="40" width="413.6" height="60" fill="#4a7bd0" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="226.8" y="67" font-size="12" text-anchor="middle">g_kws_model</text> <text x="226.8" y="84" font-size="12" text-anchor="middle">49,152 B (65%)</text> <rect x="433.6" y="40" width="206.8" height="60" fill="#4a7bd0" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="537.0" y="67" font-size="12" text-anchor="middle">g_gesture_model</text> <text x="537.0" y="84" font-size="12" text-anchor="middle">24,576 B (32%)</text>
<rect x="640.4" y="40" width="19.6" height="60" fill="#888" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="654.2" y="116" font-size="12" text-anchor="end">코드·상수·벡터 2,328 ↑</text> <text x="20" y="132" font-size="13">RAM: 사용 90,236 B / 용량 131,072 B (68.8%)</text> <rect x="20.0" y="140" width="406.7" height="60" fill="#e08a3c" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="223.4" y="167" font-size="12" text-anchor="middle">g_kws_arena</text> <text x="223.4" y="184" font-size="12" text-anchor="middle">57,344 B (64%)</text> <rect x="426.7" y="140" width="116.2" height="60" fill="#e08a3c" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/>
<text x="484.8" y="167" font-size="12" text-anchor="middle">g_gesture_arena</text> <text x="484.8" y="184" font-size="12" text-anchor="middle">16,384 B (18%)</text> <rect x="542.9" y="140" width="58.1" height="60" fill="#d0564a" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="576.0" y="216" font-size="12" text-anchor="end">.stack 8,192 ↑</text> <rect x="601.0" y="140" width="43.6" height="60" fill="#3f9a6b" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="626.8" y="230" font-size="12" text-anchor="end">g_imu_ring 6,144 ↑</text>
<rect x="644.6" y="140" width="14.5" height="60" fill="#3f9a6b" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="655.9" y="244" font-size="12" text-anchor="end">dma 2,048 ↑</text> <rect x="659.1" y="140" width="0.9" height="60" fill="#888" fill-opacity="0.7" stroke="currentColor" stroke-width="0.5"/> <text x="20" y="268" font-size="12">작은 조각(오른쪽 끝): FLASH 코드 998 + rodata 1,118 + 벡터 128 + exidx 16 + .data 원본 68 = 2,328 B</text> <text x="20" y="286" font-size="12">RAM: g_audio_dma 2,048 (dma) · 기타 124 B (.data 68, 플래그·정렬 틈) — 폭은 사용량 대비 비례</text>
</svg>
```

그림 2 — v2 빌드의 사용량을 폭 비례로 그린 treemap식 막대(`fwsize.py` 실측). 파랑 = 모델 가중치, 주황 = arena, 빨강 = 스택, 초록 = 버퍼, 회색 = 나머지. FLASH는 용량의 14.5 %만 쓰지만 RAM은 68.8 %를 쓴다 — 이 기기에서 **binding constraint는 RAM**이고, 그 RAM의 82 %가 arena 두 개다. 최적화 순서가 그림에서 바로 나온다: 코드 크기를 줄이는 노력은 거의 의미가 없고, arena(활성값 peak, D2 9절)와 버퍼 배치가 레버다.

### 2.4 `llvm-nm`으로 교차 확인

독립 도구로 검증한다: `xcrun llvm-nm -S --size-sort fw/v2/fw.elf | tail -3`은 `0800c8e0 00006000 R g_gesture_model`, `080008e0 0000c000 R g_kws_model`, `20006090 0000e000 b g_kws_arena`를 낸다 — 크기(`0xe000` = 57,344 B 등)가 맵 파서와 같다. 하지만 `nm`은 심볼 테이블만 보므로 (1) 오브젝트 귀속, (2) 정렬 틈, (3) 링커 스크립트가 만든 `.stack` 예약을 모른다. **빠른 확인은 `nm`, 회계는 맵 파일**이 원칙이다. `--gc-sections`로 버려진 것은 둘 다에 안 나온다(GNU ld 맵에는 "Discarded input sections" 목록이 따로 있다 — F7 8.1절).

### 2.5 집계 축 — 무엇으로 묶을까

| 축 | 어디서 얻나 | 답하는 질문 | 메모 |
|---|---|---|---|
| 출력 섹션 | `size -A`, 맵 0칸 줄 | flash/RAM 영역별 합 | 예산의 1차 단위 |
| 오브젝트(.o) | 맵 8칸 줄의 `file.o:` | 이 기능/파일이 얼마 | 파일 = 소유자일 때 유용 |
| 라이브러리 멤버 | GNU ld 맵의 `libfoo.a(bar.o)` | 벤더 SDK·런타임이 얼마 | TFLM·CMSIS-NN·BLE 스택 |
| 심볼 접두어·namespace | 심볼 이름 (`tflite::`, `ble_`, `g_kws_`) | 컴포넌트 합 | C++은 `llvm-cxxfilt`로 demangle 후 |
| 컴파일 유닛·소스 디렉터리 | DWARF (Bloaty 등) | 디렉터리 = 팀 | 디버그 정보 필요 |

이 Mac에서는 Apple `ar`가 ELF 멤버를 버리는 문제(F7 6.3절) 때문에 `.a` 멤버 축은 실측하지 못했다. 원리는 같다: 맵의 오브젝트 이름이 `libtensorflow-microlite.a(conv.o)` 꼴로 나오므로 괄호 앞을 라이브러리로 묶으면 된다. Google의 Bloaty나 puncover 같은 기존 도구도 결국 이 축들을 제공한다(F7 8.1절). 직접 50줄로 짜 보는 이유는 **팀의 예산 단위(컴포넌트 소유자)에 맞춰 묶는 규칙**을 넣을 수 있기 때문이다.

### 2.6 함정

- `.data`를 flash에서 빼먹는다. LMA가 flash인 섹션은 flash에도 센다(`usage()`의 두 번째 줄).
- 정렬 틈을 무시하면 합계가 안 맞는다. 틈이 크면(예: 4 KB 정렬된 DMA 버퍼 앞) 그 자체가 최적화 대상이다 — 정렬 큰 것끼리 모아 배치한다.
- `<internal>`(링커가 만든 것: `.ARM.exidx`, 병합된 문자열 `.rodata.str1.1`)은 오브젝트 귀속이 안 된다. 문자열 병합은 여러 파일의 같은 문자열을 하나로 합치므로 **파일별 합은 정의상 근사**다.
- LTO를 켜면 오브젝트 귀속이 거의 사라진다(전부 한 덩어리 `.o`로 보인다). LTO 빌드는 심볼 축으로 보고, 오브젝트 축은 non-LTO 빌드에서 본다.
- `static` 함수·변수는 다른 파일에 같은 이름이 있을 수 있다. 심볼 이름만 키로 쓰면 합쳐져 버린다(3절 diff 도구의 한계 — 연습문제 3).

---

## 3. 크기 diff — 두 빌드를 비교한다 (CI의 핵심)

### 3.1 왜 절대값이 아니라 diff인가

RAM 90,236 B라는 숫자를 PR 리뷰어가 보고 판단할 수 있을까? 못 한다. 하지만 "**이 PR이 RAM을 +30,736 B 늘렸고, 그중 16,384 B는 새 `g_gesture_arena`, 8,192 B는 `g_kws_arena`가 커진 것**"은 판단할 수 있다. 회귀 감시의 단위는 언제나 **변화량과 그 원인 심볼**이다. Don이 SSD 펌웨어에서 성능 회귀를 볼 때 "지난 빌드 대비 몇 %, 어느 커밋"으로 봤던 것과 같다.

### 3.2 `sizediff.py` (예제 3)

무엇을 확인하는 코드인지: 두 맵을 `fwsize.parse_map`으로 읽어 영역 합계 Δ와 심볼별 Δ(NEW/REMOVED/changed)를 큰 순서로 markdown 표로 낸다. 이 표가 그대로 PR 코멘트가 된다.

```python
"""sizediff.py — 두 빌드의 맵 파일을 비교해 영역·섹션·심볼별 증감을 보여 준다 (CI 리포트용)."""
import sys, collections
from fwsize import parse_map, usage, region

def by_symbol(rows):
    d = collections.Counter()
    for out, obj, insec, sym, vma, lma, size in rows:
        d[(region(vma), out, sym if obj[0] != "(" else f"{obj}{out}")] += size
    return d

def diff(old_map, new_map, top=10):
    a, b = parse_map(old_map), parse_map(new_map)
    ua, ub = usage(a), usage(b)
    lines = ["| 영역 | before | after | Δ |", "|---|---:|---:|---:|"]
    for reg in ("flash", "ram"):
        lines.append(f"| {reg} | {ua[reg]:,} | {ub[reg]:,} | {ub[reg]-ua[reg]:+,} |")
    sa, sb = by_symbol(a), by_symbol(b)
    deltas = [(k, sa.get(k, 0), sb.get(k, 0)) for k in set(sa) | set(sb) if sa.get(k, 0) != sb.get(k, 0)]
    deltas.sort(key=lambda t: (-abs(t[2] - t[1]), t[0][2]))
    lines += ["", "| 심볼 | 영역 | 섹션 | before | after | Δ | 상태 |", "|---|---|---|---:|---:|---:|---|"]
    for (reg, out, sym), x, y in deltas[:top]:
        state = "NEW" if x == 0 else "REMOVED" if y == 0 else "changed"
        lines.append(f"| `{sym}` | {reg} | {out} | {x:,} | {y:,} | {y-x:+,} | {state} |")
    if len(deltas) > top: lines.append(f"| … 외 {len(deltas)-top}개 | | | | | | |")
    return ua, ub, "\n".join(lines)

if __name__ == "__main__":
    print(diff(sys.argv[1], sys.argv[2])[2])
```

```sh
.venv/bin/python sizediff.py fw/v1/fw.map fw/v2/fw.map
```

```text
| 영역 | before | after | Δ |
|---|---:|---:|---:|
| flash | 43,120 | 76,056 | +32,936 |
| ram | 59,500 | 90,236 | +30,736 |

| 심볼 | 영역 | 섹션 | before | after | Δ | 상태 |
|---|---|---|---:|---:|---:|---|
| `g_gesture_model` | flash | .model | 0 | 24,576 | +24,576 | NEW |
| `g_gesture_arena` | ram | .bss | 0 | 16,384 | +16,384 | NEW |
| `g_kws_arena` | ram | .bss | 49,152 | 57,344 | +8,192 | changed |
| `g_kws_model` | flash | .model | 40,960 | 49,152 | +8,192 | changed |
| `g_imu_ring` | ram | .bss | 0 | 6,144 | +6,144 | NEW |
| `gesture_infer` | flash | .text | 0 | 120 | +120 | NEW |
| `main` | flash | .text | 62 | 86 | +24 | changed |
| `imu_irq` | flash | .text | 2 | 20 | +18 | changed |
| `(padding).bss` | ram | .bss | 31 | 43 | +12 | changed |
| `.rodata.str1.1` | flash | .rodata | 4 | 12 | +8 | changed |
| … 외 2개 | | | | | | |
```

출력에서 볼 것:

- 영역 합계 Δ가 1.4절의 `size` 차이(flash +32,936, RAM +30,736)와 같다. 이번엔 **원인이 심볼 단위로** 나온다.
- 큰 것 다섯 개가 Δ의 99 % 이상이다: 새 제스처 모델 24 KB, 새 arena 16 KB, KWS 모델·arena 각 +8 KB, IMU 링버퍼 6 KB. 코드 증가(`gesture_infer` 120 B, `main` +24 B)는 잡음 수준이다.
- `imu_irq`가 2 → 20 B로 바뀌었다(v1은 빈 stub, v2는 진짜 핸들러). `(padding).bss` +12 B — 정렬 틈도 변한다. **diff가 0이 아닌 줄이 많아도 놀라지 말고 큰 순으로 본다.**
- 정렬 키에 심볼 이름을 넣은 이유: 처음에는 `|Δ|`만으로 정렬했더니 Δ가 똑같은 `g_kws_arena`와 `g_kws_model`(둘 다 +8,192)의 순서가 **실행할 때마다 바뀌었다**(Python `set`의 순회 순서는 실행마다 달라질 수 있다). CI 리포트가 매번 다르게 나오면 리뷰어는 리포트를 믿지 않는다. **도구 출력은 결정적이어야 한다.**

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">v1 → v2 심볼별 증가 (sizediff 실측, 막대 = Δ 바이트)</text> <text x="200" y="53" font-size="12" text-anchor="end">g_gesture_model</text> <rect x="210" y="40" width="380.0" height="18" fill="#4a7bd0" fill-opacity="0.8"/> <text x="596.0" y="53" font-size="12">+24,576 NEW</text> <text x="200" y="83" font-size="12" text-anchor="end">g_gesture_arena</text> <rect x="210" y="70" width="253.3" height="18" fill="#e08a3c" fill-opacity="0.8"/> <text x="469.3" y="83" font-size="12">+16,384 NEW</text> <text x="200" y="113" font-size="12" text-anchor="end">g_kws_arena</text> <rect x="210" y="100" width="126.7" height="18" fill="#e08a3c" fill-opacity="0.8"/>
<text x="342.7" y="113" font-size="12">+8,192 56 KB←48</text> <text x="200" y="143" font-size="12" text-anchor="end">g_kws_model</text> <rect x="210" y="130" width="126.7" height="18" fill="#4a7bd0" fill-opacity="0.8"/> <text x="342.7" y="143" font-size="12">+8,192 48 KB←40</text> <text x="200" y="173" font-size="12" text-anchor="end">g_imu_ring</text> <rect x="210" y="160" width="95.0" height="18" fill="#e08a3c" fill-opacity="0.8"/> <text x="311.0" y="173" font-size="12">+6,144 NEW</text> <text x="200" y="203" font-size="12" text-anchor="end">코드 합 (gesture_infer 등)</text> <rect x="210" y="190" width="2.6" height="18" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="218.6" y="203" font-size="12">+166 </text> <line x1="210" y1="34" x2="210" y2="218" stroke="currentColor"/> <rect x="210" y="226" width="12" height="12" fill="#4a7bd0" fill-opacity="0.8"/><text x="228" y="237" font-size="12">flash (합 +32,936 B)</text> <rect x="390" y="226" width="12" height="12" fill="#e08a3c" fill-opacity="0.8"/><text x="408" y="237" font-size="12">RAM (합 +30,736 B)</text>
</svg>
```

그림 3 — v1 → v2 심볼별 증가량(sizediff 실측). 파랑 = flash, 주황 = RAM. 기능 하나("제스처 인식")가 모델·arena·센서 버퍼·코드 네 군데에 동시에 흔적을 남긴다. 리뷰어가 볼 것: 새 arena 16 KB가 D2 추정기의 peak와 맞는가? KWS arena가 왜 8 KB 커졌나(모델 교체 때문인가, 실수인가)?

### 3.3 Δ의 원인 분류표 — 리뷰할 때 쓰는 머릿속 표

| Δ의 모양 | 흔한 원인 | 확인 방법 |
|---|---|---|
| `.model`/`.rodata`의 큰 blob 하나 변화 | 모델 교체·양자화 변경 | 모델 해시·파라미터 수와 대조 (D2 2절) |
| `.bss` arena 변화 | 모델 구조 변화(peak), 혹은 매직 넘버 수정 | `arena_used_bytes` 로그와 대조 (F2) |
| 새 `.bss` 버퍼 (NEW) | 새 센서·기능 | 설계 문서의 버퍼 표에 있는가 |
| `.text`가 여러 함수에서 조금씩 | 컴파일러 버전·플래그 변경, inline 결정 변화 | 같은 소스를 두 컴파일러로 빌드해 비교 |
| `core::fmt`, `printf`류 수 KB 등장 | 디버그 로그·패닉 메시지가 포매팅을 끌고 옴 | J4 8.2절, defmt 같은 대안 |
| `REMOVED` + `NEW` 쌍, 크기 비슷 | 이름 변경(rename) | 실제 증가 아님 — 짝지어 읽기 |
| padding·gap 변화 | 정렬 요구가 있는 버퍼 추가 | 큰 정렬끼리 모으기 |

---

## 4. 스택 — 최악 깊이를 정적으로 계산한다

### 4.1 직관 — 최악 스택 = "프레임 합이 가장 큰 호출 경로"

함수가 호출되면 스택에 **프레임**(저장한 레지스터 + 지역 변수 + 정렬)이 쌓이고, 반환하면 걷힌다. 그러니 어느 순간의 스택 깊이는 "지금 실행 중인 호출 체인의 프레임 합"이다. 최악 스택은 **가능한 모든 호출 경로 중 프레임 합의 최댓값**이다. 호출 그래프가 트리(재귀 없음)라면 말 그대로 "가장 무거운 뿌리→잎 경로"다.

손계산 — v2 빌드의 실측 프레임(4.3절 표)으로:

```
경로 A: Reset_Handler(0) → main(0) → kws_infer(80) → audio_features(1040)        = 1120 B
경로 B: Reset_Handler(0) → main(0) → kws_infer(80) → conv_s8(96)                  =  176 B
경로 C: Reset_Handler(0) → main(0) → gesture_infer(400) → conv_s8(96)             =  496 B
경로 D: Reset_Handler(0) → main(0) → kws_infer(80) → qsort_scores(40) × 재귀 깊이 =   80 + 40·d B
최악 = max(A, B, C, D, …)  → d ≤ 26 이면 A가 최악
```

말로 하면: 각 경로에서 프레임을 더하고, 그중 가장 큰 것을 고른다. 다만 (1) 재귀가 있으면 깊이 `d`를 누군가 정해 줘야 하고, (2) 함수 포인터로 부르는 곳은 그래프에 화살표가 없으며, (3) 인터럽트는 **아무 지점에서나** 이 위에 얹힌다. 4.3~4.5절이 이 세 가지를 다룬다.

### 4.2 함수별 프레임 — `-fstack-usage`와 `-Wframe-larger-than`

`-fstack-usage`를 주면 컴파일러가 오브젝트마다 `.su` 파일을 만든다. 형식은 `파일:줄:함수 <탭> 바이트 <탭> 종류`이고, 종류는 `static`(고정), `dynamic`(VLA·`alloca`로 실행 중 변함), `dynamic,bounded`(변하지만 상한이 알려짐)다. GCC의 기능이지만 Apple clang 21도 Cortex-M 타깃에서 지원한다(실측).

```text
$ cat v2/audio.su v2/kws.su
audio.c:8:audio_dma_irq	0	static
audio.c:9:audio_features	1040	static
kws.c:8:kws_infer	80	static
```

빌드 단계에서 큰 프레임을 바로 잡고 싶으면 `-Wframe-larger-than=N`을 쓴다(`-Werror=`로 올리면 빌드 실패). 무엇을 확인하는 코드인지: 512 B 넘는 프레임에 경고가 나는지.

```sh
cc --target=thumbv7em-none-eabihf -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
   -Os -ffreestanding -Wframe-larger-than=512 -c audio.c -o audio.o
```

```text
audio.c:9:6: warning: stack frame size (1040) exceeds limit (512) in 'audio_features' [-Wframe-larger-than]
    9 | void audio_features(const int16_t *pcm, int8_t *feat) {
      |      ^
1 warning generated.
```

출력에서 볼 것: 경고 문구가 `.su`와 같은 1040을 말한다. GCC의 `-Wstack-usage=N`은 clang이 모른다(`unknown warning option` — 실측). 팀 규칙 예: "ISR과 추론 태스크의 함수는 프레임 256 B 이하, 넘으면 `-Werror`" — 큰 버퍼는 arena나 정적 영역으로 옮기라는 신호다.

### 4.3 호출 그래프를 붙여 최악을 계산한다 — `stackcheck.py` (예제 4)

`.su`는 함수 하나의 프레임만 안다. 경로를 알려면 **누가 누구를 부르는지**가 필요하다. GCC는 `-fcallgraph-info`로 그래프를 내 주지만 clang은 없으므로, **링크된 ELF를 디스어셈블해서** `bl`(호출), `b`/`b.w`(다른 함수로의 tail call), `blx rN`(간접 호출)을 직접 뽑는다. 덤으로 각 함수 프롤로그의 `push {…}`(레지스터 4 B씩), `vpush {d…}`(8 B씩), `sub sp, #N`을 더해 `.su`와 **교차 검증**한다 — `.su`가 없는 어셈블리 함수나 벤더 라이브러리에는 이 프롤로그 값이 유일한 정보다.

```python
"""stackcheck.py — .su(함수별 프레임) + 디스어셈블(호출 그래프)로 최악 스택 깊이를 계산한다."""
import re, sys, glob, subprocess
build = sys.argv[1]
extra = dict(a.split("=") for a in sys.argv[2:] if "=" in a)    # main=log_event : 간접 호출 대상
depth = {f: int(n) for f, n in (a.split("@") for a in sys.argv[2:] if "@" in a)}  # qsort_scores@12 : 재귀 상한
frame = {}
for su in glob.glob(f"{build}/*.su"):                                  # audio.c:9:audio_features\t1040\tstatic
    for line in open(su):
        loc, n, kind = line.rstrip("\n").split("\t")
        frame[loc.split(":")[-1]] = (int(n), kind)
asm = subprocess.run(["xcrun", "llvm-objdump", "-d", "--no-show-raw-insn", f"{build}/fw.elf"],
                     capture_output=True, text=True).stdout
calls, tails, indirect, prologue, fn = {}, {}, set(), {}, None
for line in asm.splitlines():
    if m := re.match(r"^[0-9a-f]+ <(\w+)>:", line):
        fn = m.group(1); calls[fn], tails[fn], prologue[fn] = set(), set(), 0; continue
    if fn is None or "\t" not in line: continue
    op, args = (line.split("\t") + [""])[1:3]
    if op == "bl" and (m := re.search(r"<(\w+)>", args)): calls[fn].add(m.group(1))
    elif op in ("b", "b.w") and (m := re.search(r"<(\w+)>", args)) and m.group(1) != fn:
        tails[fn].add(m.group(1))                                       # tail call: 내 프레임은 이미 pop
    elif op.startswith("blx") and args.startswith("r"): indirect.add(fn)
    elif op.startswith("push") or op.startswith("vpush"):             # 프롤로그 교차 검증용
        regs = len(args.split("}")[0].split(","))                      # {r4, r5, r7, lr} -> 4개
        prologue[fn] += regs * (8 if op.startswith("vpush") else 4)
    elif op.startswith("sub") and args.startswith("sp, ") and "#" in args:
        prologue[fn] += int(args.split("#")[1].split()[0], 0)
for f, c in extra.items(): calls[f] |= set(c.split(","))               # 사람이 적어 준 간접 호출 대상

def worst(f, path=()):
    """반환: (최악 바이트, 그 경로)"""
    if f in path: raise RecursionError(" -> ".join(path + (f,)))
    own = frame.get(f, (prologue.get(f, 0), "asm"))[0] * depth.get(f, 1)   # 자기 재귀는 상한 × 프레임
    below = max([worst(c, path + (f,)) for c in calls.get(f, ()) if c != f or f not in depth],
                default=(0, ()), key=lambda t: t[0])
    tail = [(b, (f + "~",) + p) for b, p in (worst(t, path + (f,)) for t in tails.get(f, ()))]
    return max([(own + below[0], (f,) + below[1])] + tail, key=lambda t: t[0])

print(f"{'함수':<16}{'.su':>6}{'프롤로그':>9}  호출")
for f in calls:
    su = frame.get(f, ("-", ""))[0]
    note = ("간접호출! " if f in indirect else "") + ",".join(sorted(calls[f])) + \
           "".join(f" tail:{t}" for t in sorted(tails[f]))
    print(f"{f:<16}{su:>6}{prologue[f]:>9}  {note}")
print()
EXC = 104                                       # FPU 활성 시 예외 스택 프레임 (확장 프레임 26 word)
try:
    w = {r: worst(r) for r in ["Reset_Handler", "audio_dma_irq", "imu_irq"]}
except RecursionError as e: sys.exit(f"계산 불가 — 재귀: {e}  (상한을 f@N으로 주세요)")
if indirect - set(extra): print(f"경고: 간접 호출 대상 미지정 {sorted(indirect - set(extra))} -> 과소평가 가능")
for r, (v, p) in w.items(): print(f"최악 스택 {r:<14} = {v:5d} B  경로: {' > '.join(p)}")
isr = (w["audio_dma_irq"][0] + EXC) + (w["imu_irq"][0] + EXC)  # 두 ISR이 서로 다른 우선순위라 중첩 가능하다고 가정
print(f"main 스택 합계 = {w['Reset_Handler'][0]} + ISR 중첩 {isr} = {w['Reset_Handler'][0] + isr} B")
```

첫 실행 — 아무 정보도 주지 않으면:

```sh
.venv/bin/python -u stackcheck.py fw/v2
```

```text
함수                 .su     프롤로그  호출
audio_dma_irq        0        0  
audio_features    1040     1040  
imu_irq              0        0  
gesture_infer      400      400  conv_s8,dense_s8
conv_s8             96       96  
dense_s8             8        8  
qsort_scores        40       40  qsort_scores
kws_infer           80       80  audio_features,conv_s8,dense_s8,qsort_scores
log_event          160      160  
main                 0        0  간접호출! gesture_infer,kws_infer
Reset_Handler        0        0  main
Default_Handler      0        0  

계산 불가 — 재귀: Reset_Handler -> main -> kws_infer -> qsort_scores -> qsort_scores  (상한을 f@N으로 주세요)
```

출력에서 볼 것:

- **`.su`와 프롤로그 파싱이 모든 함수에서 일치**한다. 예: `kws_infer`는 `push.w {r4-r10, lr}` 8개 × 4 = 32 B + `sub sp, #0x30` 48 B = 80 B. `audio_features`는 `push {r4, r5, r7, lr}` 16 B + `sub.w sp, sp, #0x400` 1,024 B = 1,040 B. 두 독립 출처가 맞으니 프레임 숫자를 믿을 수 있다.
- `main`의 프레임이 0이다. `main`이 절대 반환하지 않는 무한 루프라서 컴파일러가 `lr`을 저장하지 않았다. 
- **도구가 계산을 거부했다**: `qsort_scores → qsort_scores` 재귀. 재귀 깊이는 코드만 봐서는 모른다(입력에 달렸다). 정적 분석 도구가 "모르겠다"고 말하는 게 정답이다 — 0으로 치고 넘어가는 도구가 위험하다.
- `main`에 `간접호출!` 표시: `g_on_detect(...)`가 `blx r2`로 컴파일됐다. 처음에 `g_on_detect`를 `static`으로 선언했을 때는 컴파일러가 "아무도 안 바꾼다"는 걸 알아서 `bl log_event`로 **직접 호출로 바꿔 버렸다**(devirtualization). 전역으로 바꾸자 간접 호출이 됐다. 같은 소스라도 컴파일러 판단에 따라 그래프가 달라진다.

사람이 아는 정보를 준다: 재귀 깊이 상한 12(점수 12개를 정렬하므로 최악 깊이 ≤ 12 — 보수적으로), 간접 호출 대상 `log_event`.

```sh
.venv/bin/python -u stackcheck.py fw/v2 qsort_scores@12 main=log_event
```

(함수 표는 첫 실행과 같아서 바뀐 `main` 줄과 결과만 남겼다.)

```text
main                 0        0  간접호출! gesture_infer,kws_infer,log_event
main 스택 합계 = 1120 + ISR 중첩 208 = 1328 B

최악 스택 Reset_Handler  =  1120 B  경로: Reset_Handler > main > kws_infer > audio_features
최악 스택 audio_dma_irq  =     0 B  경로: audio_dma_irq
최악 스택 imu_irq        =     0 B  경로: imu_irq
main 스택 합계 = 1120 + ISR 중첩 208 = 1328 B
```

출력에서 볼 것:

- 최악 경로는 `main > kws_infer > audio_features` 1,120 B다. 범인은 **`audio_features`의 지역 배열 `int32_t win[256]` 1 KB** — 전체의 93 %. 재귀 quicksort는 `80 + 40 × 12 = 560 B`로 2위, 제스처 경로는 `400 + 96 = 496 B`다.
- 수정 방향이 바로 나온다: `win[]`을 arena나 정적 scratch로 옮기면 `audio_features` 경로는 96 B로 줄고, 최악은 재귀 quicksort 경로 560 B가 된다 — 1,120 → 560 B로 반. 그다음 최악이 무엇인지까지 도구가 알려 준다. 프레임을 줄이는 노력은 **최악 경로 위의 함수**에만 의미가 있다(병목은 하나다 — Amdahl).
- ISR 두 개는 자체 프레임 0 B지만 진입할 때마다 하드웨어가 **예외 프레임**을 쌓는다(4.5절). 두 ISR이 우선순위가 달라 중첩될 수 있다고 보면 `2 × 104 = 208 B`가 더해져 1,328 B. `STACK_SIZE` 8 KB 대비 16 %다.

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="k1a4" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker> <marker id="k1r4" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="#d0564a"/></marker></defs> <rect x="20" y="40" width="120" height="36" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="80" y="63" font-size="12" text-anchor="middle">Reset_Handler 0</text> <rect x="170" y="40" width="90" height="36" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="215" y="63" font-size="12" text-anchor="middle">main 0</text>
<rect x="300" y="40" width="110" height="36" rx="4" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="355" y="63" font-size="12" text-anchor="middle">kws_infer 80</text> <rect x="450" y="6" width="170" height="36" rx="4" fill="#d0564a" fill-opacity="0.25" stroke="#d0564a" stroke-width="2"/> <text x="535" y="29" font-size="12" text-anchor="middle">audio_features 1040</text> <rect x="450" y="58" width="150" height="36" rx="4" fill="none" stroke="currentColor"/> <text x="525" y="81" font-size="12" text-anchor="middle">qsort_scores 40</text> <path d="M600,66 C640,56 640,96 600,86" fill="none" stroke="#e08a3c" stroke-width="1.5" marker-end="url(#k1a4)"/>
<text x="638" y="112" font-size="12" text-anchor="end">재귀 × 12?</text> <rect x="480" y="130" width="110" height="36" rx="4" fill="none" stroke="currentColor"/> <text x="535" y="153" font-size="12" text-anchor="middle">conv_s8 96</text> <rect x="480" y="180" width="110" height="36" rx="4" fill="none" stroke="currentColor"/> <text x="535" y="203" font-size="12" text-anchor="middle">dense_s8 8</text> <rect x="290" y="150" width="130" height="36" rx="4" fill="none" stroke="currentColor"/> <text x="355" y="173" font-size="12" text-anchor="middle">gesture_infer 400</text> <rect x="140" y="150" width="120" height="36" rx="4" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
<text x="200" y="173" font-size="12" text-anchor="middle">log_event 160</text> <line x1="140" y1="58" x2="168" y2="58" stroke="#d0564a" stroke-width="2" marker-end="url(#k1r4)"/> <line x1="260" y1="58" x2="298" y2="58" stroke="#d0564a" stroke-width="2" marker-end="url(#k1r4)"/> <line x1="410" y1="52" x2="448" y2="30" stroke="#d0564a" stroke-width="2" marker-end="url(#k1r4)"/> <line x1="410" y1="64" x2="448" y2="74" stroke="currentColor" marker-end="url(#k1a4)"/> <line x1="390" y1="76" x2="480" y2="136" stroke="currentColor" marker-end="url(#k1a4)"/> <line x1="375" y1="76" x2="482" y2="184" stroke="currentColor" marker-end="url(#k1a4)"/>
<line x1="235" y1="76" x2="320" y2="148" stroke="currentColor" marker-end="url(#k1a4)"/> <line x1="420" y1="162" x2="478" y2="152" stroke="currentColor" marker-end="url(#k1a4)"/> <line x1="420" y1="176" x2="478" y2="194" stroke="currentColor" marker-end="url(#k1a4)"/> <line x1="200" y1="76" x2="200" y2="148" stroke="currentColor" stroke-dasharray="4 3" marker-end="url(#k1a4)"/> <text x="148" y="118" font-size="12" text-anchor="end">blx r2 (간접)</text> <rect x="20" y="236" width="190" height="36" rx="4" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="115" y="259" font-size="12" text-anchor="middle">audio_dma_irq 0 + 프레임 104</text>
<rect x="230" y="236" width="170" height="36" rx="4" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="315" y="259" font-size="12" text-anchor="middle">imu_irq 0 + 프레임 104</text> <text x="420" y="259" font-size="12">← 아무 지점에서나 얹힌다</text> <text x="20" y="300" font-size="12">빨간 경로 = 최악: 0 + 0 + 80 + 1040 = 1,120 B,  + ISR 중첩 2 × 104 = 1,328 B  (STACK_SIZE 8,192 B)</text> <text x="20" y="320" font-size="12">점선 = 디스어셈블에 없는 간접 호출 (사람이 대상 지정) · 주황 = 재귀 (사람이 깊이 상한 지정)</text>
</svg>
```

그림 4 — v2의 호출 그래프와 함수별 프레임(B). 상자 안 숫자가 `.su` 값이고, 빨간 경로가 도구가 찾은 최악 경로다. 정적 분석의 두 구멍 — 점선(함수 포인터)과 주황 고리(재귀) — 은 사람이 정보를 줘야 메워진다. 아래 파란 상자들은 호출 그래프와 **별개로** 어느 순간에나 더해지는 인터럽트 스택이다.

### 4.4 인터럽트와 예외 프레임 — 그래프 밖에서 더해지는 바이트

Cortex-M은 예외가 들어오면 하드웨어가 현재 스택에 레지스터를 자동으로 쌓는다(stacking).

| 상황 | 자동으로 쌓는 것 | 크기 |
|---|---|---|
| 기본 프레임 | r0–r3, r12, lr, pc, xPSR | 8 word = 32 B |
| FPU 문맥 있음 (Cortex-M4F/M7, 스레드가 FPU를 썼음) | 기본 + s0–s15 + FPSCR + 예약 1 word | 26 word = 104 B |
| 8 B 정렬 보정 | 필요 시 패딩 | +4 B |

말로 하면: hard-float으로 빌드한 M4F에서는 **ISR 진입 한 번에 최대 104(+4) B**가 핸들러의 프레임과 별개로 쓰인다. lazy stacking은 FP 레지스터를 실제로 저장하는 시점을 늦출 뿐 **공간은 진입 때 예약**한다. 우선순위가 다른 인터럽트가 중첩되면 단계마다 이 값과 각 ISR의 최악 프레임이 더해진다. 그래서 최악 = 메인 경로 최악 + Σ(중첩 가능한 ISR 단계마다 ISR 최악 + 예외 프레임)이다. 같은 우선순위 그룹끼리는 중첩되지 않으므로 그룹당 최댓값만 더한다(NVIC 우선순위 설정을 읽어야 한다).

RTOS에서는 하나 더: 스레드는 PSP(process stack), 핸들러는 MSP(main stack)를 쓴다. **예외 프레임은 인터럽트당한 스레드의 PSP 스택에** 쌓이고, 핸들러 코드와 중첩된 ISR은 MSP에 쌓인다. 그래서 각 task 스택 = 그 task의 최악 + 예외 프레임 1개(104 B), MSP 크기 = ISR 중첩 최악이다. Don이 SSD 펌웨어에서 "ISR 스택은 따로, 태스크 스택에는 프레임 하나만"이라는 규칙을 썼다면 바로 이 구조다.

### 4.5 정적 분석이 못 보는 것 — 그리고 대안

| 구멍 | 증상 | 대응 |
|---|---|---|
| 재귀 | 경로 길이가 무한 | 금지(MISRA C 계열 규칙) 또는 깊이 상한을 코드로 강제하고 도구에 알림 |
| 함수 포인터·가상 함수·콜백 테이블 | 그래프가 끊김 → 과소평가 | 대상 목록을 주석/설정으로 관리, 테이블을 읽는 스크립트 |
| VLA·`alloca` | `.su`에 `dynamic` | 금지(`-Wvla`), 또는 bounded로 만들기 |
| `.su` 없는 라이브러리·어셈블리 | 프레임 모름 | 프롤로그 파싱(위 도구), 벤더에 요청 |
| 인라인 어셈블리의 `sub sp` | 파서가 놓침 | 리뷰 |
| 컴파일러 버전·플래그 | 프레임이 바뀜 | CI에서 매 빌드 재계산 (8절) |

Rust 쪽 사정: rustc의 `-Z emit-stack-sizes`(`.stack_sizes` 섹션)와 이를 쓰는 `cargo-call-stack`이 있지만 nightly 전용이다(J4의 stable 툴체인에서는 시도하지 않았다). clang에도 같은 섹션을 내는 `-fstack-size-section`이 있고 이 Mac에서 섹션 생성까지 확인했지만, 그 섹션을 읽는 `llvm-readobj --stack-sizes`가 Xcode에 없어 쓰지 않았다. 상용 도구로는 AbsInt StackAnalyzer 같은 바이너리 정적 분석기가 항공·자동차 인증에 쓰인다.

### 4.6 런타임 high-water와 크기 결정 규칙

정적 최악은 **상한**(일어날 수 있는 최대), stack painting(D2 6.3절)은 **실측**(실제로 지나간 최대)이다. 둘은 서로를 검증한다.

- painting 값 > 정적 최악 → 정적 분석에 구멍이 있다(간접 호출 누락, 라이브러리, ISR 중첩 가정 오류). **버그 신호**다.
- painting 값 ≪ 정적 최악 → 최악 경로를 테스트가 안 돌았다(가장 긴 입력, 에러 경로, 로그 켠 빌드). 테스트 커버리지 신호다.

크기 결정 규칙(팀마다 다르지만 흔한 형태):

```
task 스택 = roundup( max(정적 최악, 실측 high-water) × (1 + margin) + 예외 프레임, 8 )
margin   = 정적 분석이 완전하면 10~20 %, 간접 호출·라이브러리가 섞이면 30~50 %
```

말로 하면: 둘 중 큰 값에 여유를 얹고, 예외 프레임 하나를 더한 뒤 8 B 정렬한다. v2 메인 스택이라면 `1,328 × 1.25 ≈ 1,660 → 1,664 B`면 충분하다는 계산이 나온다. 지금 8 KB를 잡아 둔 건 6.5 KB를 낭비하는 셈이다 — 이 RAM을 arena에 돌릴 수 있다. 반대로 **이 계산 없이 스택을 줄이는 건 도박**이다.

---

## 5. 힙 — 왜 피하고, 쓴다면 어떻게 증명하나

### 5.1 펌웨어가 `malloc`을 피하는 네 가지 이유

1. **단편화(fragmentation)**: 총 여유는 충분한데 **연속된** 빈 공간이 없어 할당이 실패한다. 시간이 지나야 나타나서 테스트에서 안 잡힌다(5.4절에서 숫자로 본다).
2. **비결정성**: first-fit 탐색 시간은 빈 블록 수에 비례한다. 실시간 경로(J2)에서 지연 상한을 보장할 수 없다.
3. **실패 처리**: `malloc`이 `NULL`을 주면 펌웨어는 무엇을 하나? 추론을 건너뛰나, 리셋하나? 정적 할당은 이 질문 자체를 링크 시간으로 옮긴다.
4. **스레드·ISR 안전성**: libc `malloc`은 lock이 필요하고 ISR에서 부를 수 없다.

그래서 흔한 규칙은 "**초기화 후 힙 금지**" 또는 "힙 금지, 고정 블록 pool만"이다(MISRA C:2012 Rule 21.3도 `<stdlib.h>` 할당 함수 사용을 금한다). TFLM이 arena 하나를 받아 그 안에서만 할당하는 것(F2)도 같은 철학이다. 하지만 BLE 스택, 파일시스템, 벤더 SDK가 내부적으로 힙을 쓰는 경우가 흔하다 — 그래서 **측정**이 필요하다.

먼저 우리 펌웨어에 힙이 없다는 걸 확인하는 한 줄:

```sh
xcrun llvm-nm -u fw/v2/fw.elf | wc -l                                   # 미해결 심볼
xcrun llvm-nm fw/v2/fw.elf | grep -ciE "malloc|free|calloc|realloc"     # 할당 함수 흔적
```

```text
       0
0
```

출력에서 볼 것: 링크된 이미지에 할당 함수가 하나도 없다 — "이 펌웨어는 힙을 쓰지 않는다"의 가장 싼 증명이다. CI에 넣어 두면 누군가 `printf`나 `std::vector`를 넣어 `malloc`을 끌고 오는 순간 잡힌다. libc가 링크되는 실제 프로젝트에서는 GNU ld/lld의 `--wrap=malloc`으로 모든 호출을 `__wrap_malloc`으로 돌려 "초기화 이후 호출되면 assert"를 거는 방법도 쓴다.

### 5.2 계측 allocator — 할당 패턴을 숫자로 (예제 5)

힙을 쓰는 코드(벤더 SDK, 호스트 시뮬레이션)를 측정할 때는 `malloc`/`free`를 감싸 **header**를 붙인다. header에 크기와 호출 위치를 적고, 살아 있는 블록을 리스트로 엮으면 현재 사용량, peak, 횟수, 크기 분포, 누수 목록이 다 나온다. FreeRTOS의 `pvPortMalloc` 훅이나 newlib의 `__wrap_malloc`이 하는 일과 같은 구조다.

무엇을 확인하는 코드인지: 프레임마다 특징·점수 버퍼를 힙에서 잡는 (나쁜) 추론 루프를 계측해, 에러 경로 하나가 만든 누수를 위치까지 찾는다.

```c
/* cmalloc.c — 계측 allocator: 블록 앞에 header를 붙여 크기·호출 위치를 기록한다 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
typedef struct hdr { struct hdr *next, *prev; size_t size; int line; uint32_t magic; } hdr_t;
#define MAGIC 0xA110C8EDu
static hdr_t live = {&live, &live, 0, 0, 0};                 /* 살아 있는 블록의 이중 연결 리스트 */
static size_t cur, peak, n_alloc, n_free, hist[8];           /* hist: ≤16,≤64,≤256,…,>64K B */
void *t_malloc(size_t n, int line) {
    hdr_t *h = malloc(sizeof *h + n);
    if (!h) return NULL;
    *h = (hdr_t){live.next, &live, n, line, MAGIC};
    live.next->prev = h; live.next = h;
    cur += n; if (cur > peak) peak = cur; n_alloc++;
    int b = 0; for (size_t s = 16; b < 7 && n > s; s <<= 2) b++;
    hist[b]++;
    return h + 1;
}
void t_free(void *p, int line) {
    if (!p) return;
    hdr_t *h = (hdr_t *)p - 1;
    if (h->magic != MAGIC) { printf("  !! line %d: 잘못된 free (double free 또는 남의 포인터)\n", line); return; }
    h->magic = 0; h->prev->next = h->next; h->next->prev = h->prev;
    cur -= h->size; n_free++; free(h);
}
void t_report(void) {
    printf("alloc %zu회, free %zu회, 현재 %zu B, peak %zu B\n", n_alloc, n_free, cur, peak);
    const char *lab[8] = {"<=16", "<=64", "<=256", "<=1K", "<=4K", "<=16K", "<=64K", ">64K"};
    printf("크기 분포:"); for (int i = 0; i < 8; i++) if (hist[i]) printf(" %s:%zu", lab[i], hist[i]);
    printf("\n");
    for (hdr_t *h = live.next; h != &live; h = h->next) printf("  LEAK %zu B (line %d)\n", h->size, h->line);
}
#define MALLOC(n) t_malloc((n), __LINE__)
#define FREE(p)   t_free((p), __LINE__)
```

```c
/* workload.c — 추론 세션 흉내: 프레임마다 특징 버퍼·결과 문자열을 힙에서 잡는다 (안티패턴 데모) */
#include "cmalloc.c"
#include <string.h>
int main(void) {
    for (int frame = 0; frame < 100; frame++) {
        int8_t *feat = MALLOC(40 * 49);                    /* 특징 1,960 B */
        int32_t *scores = MALLOC(12 * sizeof(int32_t));     /* 점수 48 B */
        memset(feat, 0, 40 * 49); scores[0] = frame;
        if (frame % 25 == 24) {                             /* 검출 이벤트: 로그 문자열 */
            char *msg = MALLOC(64);
            snprintf(msg, 64, "kws hit at frame %d", frame);
            if (frame == 49) continue;                      /* 버그: 에러 경로에서 free 누락 */
            FREE(msg);
        }
        FREE(scores); FREE(feat);
    }
    void *model_copy = MALLOC(40 * 1024);                   /* 시작 시 1회: 모델 RAM 복사 */
    FREE(model_copy);
    t_report();
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 workload.c -o workload && ./workload
```

```text
alloc 205회, free 202회, 현재 2072 B, peak 43032 B
크기 분포: <=64:104 <=4K:100 <=64K:1
  LEAK 64 B (line 10)
  LEAK 48 B (line 7)
  LEAK 1960 B (line 6)
```

출력에서 볼 것:

- 경고 0개. 누수 3블록이 **줄 번호와 함께** 나왔다: `continue` 한 줄이 `msg`뿐 아니라 `scores`와 `feat`까지 새게 했다. 에러 경로의 early return/continue가 누수의 고전적 원인이다. 합 2,072 B = 64 + 48 + 1,960.
- peak 43,032 B = 모델 복사 40,960 + 이미 샌 2,072. **누수는 peak를 영구히 끌어올린다.** 매 49번째 프레임마다 샌다면 하루 만에 힙이 바닥난다.
- 크기 분포: 64 B 이하 104회, 4 KB 이하 100회, 64 KB 이하 1회. 이 분포가 **pool 등급을 정하는 근거**다(5.6절): 이 워크로드라면 64 B 등급, 2 KB 등급, 그리고 시작 시 1회짜리는 정적 버퍼로.
- header 비용: 이 호스트(64-bit)에서 `hdr_t`는 32 B다(포인터 2개 16 + `size_t` 8 + `int` 4 + magic 4). Cortex-M(32-bit)이면 20 B. 64 B 블록에 header가 붙으면 오버헤드가 31~50 % — 작은 할당이 많은 코드일수록 힙 자체가 비싸다.

### 5.3 OS 도구로 교차 확인 — `leaks`, ASan, `heap` (예제 6)

무엇을 확인하는 코드인지: 계측 없이 OS 도구가 같은 종류의 누수를 찾는지, 그리고 이 Mac에서 무엇이 되는지. `leak.c`는 예제 5의 버그만 남긴 평범한 코드다 — `make_msg()`가 `malloc(64)`로 문자열을 만들고, 100번 루프 중 `f == 49`에서 `continue`로 `free`를 건너뛴다.

```sh
cc -std=c11 -Wall -Wextra -O1 -g -fsanitize=address leak.c -o leak_asan
ASAN_OPTIONS=detect_leaks=1 ./leak_asan; echo "exit=$?"
cc -std=c11 -Wall -Wextra -O1 -g leak.c -o leak
leaks --atExit -- ./leak
```

```text
==25817==AddressSanitizer: detect_leaks is not supported on this platform.
exit=134
--- leaks --atExit (발췌)
Process 25919: 189 nodes malloced for 12 KB
Process 25919: 1 leak for 80 total leaked bytes.
STACK OF 1 INSTANCE OF 'ROOT LEAK: <malloc in main>':
1   leak                                  0x104d28498 main + 56  leak.c:11
0   libsystem_malloc.dylib                0x18c5c4178 _malloc_zone_malloc_instrumented_or_legacy + 152 
```

출력에서 볼 것:

- **ASan의 leak 검출(LeakSanitizer)은 이 Apple Silicon Mac에서 지원되지 않는다** — `detect_leaks=1`을 주면 실행 자체를 거부한다(exit 134). Linux CI에서는 ASan 빌드에 LSan이 기본으로 켜져 있어 같은 버그를 잡는다(J6의 sanitizer 빌드). 플랫폼마다 도구가 다르다는 것 자체가 배울 점이다.
- `leaks --atExit`는 sudo 없이 동작했고 `leak.c:11`(인라인된 `make_msg` 호출 지점)을 정확히 짚었다. 종료 코드는 누수가 있으면 1(별도 실행으로 확인) — CI에서 바로 쓸 수 있다.
- 요청은 64 B인데 80 B로 보고됐다. allocator가 실제로 잡은 블록 크기(크기 등급 반올림)로 보인다 — `leaks`의 바이트는 "요청량"이 아니라 "allocator 블록" 기준이다.
- 함정 하나를 실측했다: 예제 5의 `workload`를 `leaks --atExit`로 돌리면 **누수 0개, 종료 코드 0**이 나온다. 계측 allocator가 샌 블록까지 `live` 리스트로 붙잡고 있어서, 도달 가능성(reachability)으로 누수를 판정하는 `leaks` 눈에는 "아직 누가 가리키는 메모리"로 보이기 때문이다. **계측이 다른 도구를 가린다** — 도구는 한 번에 하나씩 쓴다.

같은 계열의 `heap` 명령은 살아 있는 프로세스의 할당을 크기 등급별로 보여 준다. 1,960 B × 100개와 40 KB 하나를 잡고 잠든 프로그램(`g_keep[]`에 포인터를 붙잡아 컴파일러가 `malloc`을 지우지 못하게 했다 — 처음 버전은 `-O1`에서 사용하지 않는 `malloc`+`memset`이 통째로 사라져 아무것도 안 보였다):

```text
$ heap <pid>          (발췌)
All zones: 285 nodes malloced - Sizes: 48KB[1] 2KB[100] 1.5KB[1] 1.25KB[1] 1KB[1] 640[1] 128[1] 96[2] 80[1] 64[10] 48[16] 32[146] 16[4]
```

출력에서 볼 것: 1,960 B 요청이 **2 KB 블록**으로, 40,960 B 요청이 **48 KB 블록**으로 잡혔다. 크기 등급 allocator의 **내부 단편화**(요청보다 큰 블록을 주고 남는 부분)가 그대로 보인다. MCU에서 pool 등급을 정할 때도 같은 낭비가 생긴다(5.6절).

### 5.4 단편화 — 총 여유는 있는데 할당이 실패한다 (예제 7)

직관: 주차장에 빈 칸이 10개 있어도 버스(연속 4칸)는 못 댈 수 있다. 빈 칸이 흩어져 있으면. **외부 단편화**는 "빈 메모리의 총량"과 "가장 큰 연속 빈 블록"이 달라지는 현상이고, 지표는 다음과 같다.

```
frag = 1 − (가장 큰 free 블록) / (총 free)
```

말로 하면: 0이면 빈 공간이 한 덩어리, 1에 가까우면 잘게 흩어져 있다. 할당 성공 조건은 "총 free ≥ 요청"이 아니라 "**가장 큰 free 블록 ≥ 요청 + header**"다.

무엇을 확인하는 코드인지: 20 KB 힙에 웨어러블다운 워크로드 — 잠깐 사는 오디오 프레임(1 KB)·BLE 패킷(244 B), 오래 살다가 **순서 없이** 빠지는 이벤트 기록(48~128 B, 최대 60개) — 를 2,000 step 돌리면서 매 step 8 KB 특징 창 버퍼를 요청해 본다. first-fit + 인접 블록 병합(coalescing)을 하는 정직한 allocator다.

```python
import random
HEAP, HDR, BIG = 20 * 1024, 8, 8 * 1024        # 20 KB 힙, header 8 B, 주기적 8 KB 요청
class FirstFit:
    def __init__(s): s.free = [(0, HEAP)]      # 빈 구멍 (시작, 크기), 주소순
    def alloc(s, n):
        n = (n + HDR + 7) & ~7
        for i, (a, sz) in enumerate(s.free):
            if sz >= n:                        # 첫 번째로 맞는 구멍을 쪼갠다
                s.free[i:i + 1] = [(a + n, sz - n)] if sz > n else []
                return (a, n)
    def release(s, blk):
        m = []
        for a, sz in sorted(s.free + [blk]):   # 인접한 구멍은 합친다 (coalescing)
            if m and m[-1][0] + m[-1][1] == a: m[-1] = (m[-1][0], m[-1][1] + sz)
            else: m.append((a, sz))
        s.free = m

rng, h, short, logs, trace = random.Random(1), FirstFit(), [], [], []
peak = {"1024": 0, "256": 0, "128": 0}         # 크기 등급별 동시 사용 최대 (pool 크기 산정용)
for t in range(2000):
    n, life = rng.choice([(1024, rng.randint(1, 6)), (244, rng.randint(1, 15))])  # 오디오·BLE: 잠깐 산다
    if b := h.alloc(n): short.append((t + life, n, b))
    if b := h.alloc(rng.randint(48, 128)): logs.append(b)                      # 이벤트 기록 (오래 산다)
    if len(logs) > 60: h.release(logs.pop(rng.randrange(len(logs))))          # 폰이 순서 없이 가져간다
    for e in [x for x in short if x[0] <= t]: short.remove(e); h.release(e[2])
    peak["1024"] = max(peak["1024"], sum(1 for x in short if x[1] == 1024))
    peak["256"] = max(peak["256"], sum(1 for x in short if x[1] == 244))
    peak["128"] = max(peak["128"], len(logs))
    big = h.alloc(BIG)
    if big: h.release(big)
    trace.append((sum(z for _, z in h.free), max((z for _, z in h.free), default=0), big is not None))
for i in range(0, 2000, 400):
    w = trace[i:i + 400]
    print(f"t {i:4d}-{i+399:4d}: 평균 free {sum(f for f, _, _ in w)/400:6,.0f} B, 평균 largest {sum(l for _, l, _ in w)/400:6,.0f} B,"
          f" 8KB 실패 {sum(not ok for *_, ok in w):3d}/400")
print(f"전체 최소 free = {min(f for f, _, _ in trace):,} B (8 KB 요청 + header = {BIG + HDR:,} B)")
pool = 1024 * peak["1024"] + 256 * peak["256"] + 128 * peak["128"] + BIG
print(f"pool 크기 산정: 1KB×{peak['1024']} + 256B×{peak['256']} + 128B×{peak['128']} + 8KB×1 = {pool:,} B → 실패 0 (정의상)")
```

```text
t    0- 399: 평균 free 12,085 B, 평균 largest  9,439 B, 8KB 실패 132/400
t  400- 799: 평균 free 11,595 B, 평균 largest  8,847 B, 8KB 실패 145/400
t  800-1199: 평균 free 11,622 B, 평균 largest  9,020 B, 8KB 실패 108/400
t 1200-1599: 평균 free 11,769 B, 평균 largest  9,258 B, 8KB 실패  89/400
t 1600-1999: 평균 free 11,535 B, 평균 largest  8,696 B, 8KB 실패 131/400
전체 최소 free = 8,384 B (8 KB 요청 + header = 8,200 B)
pool 크기 산정: 1KB×5 + 256B×10 + 128B×60 + 8KB×1 = 23,552 B → 실패 0 (정의상)
```

출력에서 볼 것:

- **2,000번 내내 총 free는 한 번도 8,200 B 아래로 내려가지 않았다**(최소 8,384 B). 그런데 8 KB 요청은 605번(30 %) 실패했다. 실패 원인이 100 % 단편화다. "메모리가 남았는데 왜 실패하지?"의 정체다.
- 단편화는 처음 400 step 안에 이미 평형에 도달하고(실패율 22~36 %에서 출렁임), 그 뒤로 좋아지지 않는다. **오래 사는 작은 블록이 순서 없이 빠지면** 그 사이사이 구멍이 계속 생긴다. 이벤트 기록을 무작위 대신 **오래된 순서(FIFO)**로 빼도록 한 줄만 바꿔 같은 조건으로 돌리면 실패가 605회 → 407회(20 %)로 줄지만 사라지지 않는다 — 같은 크기의 할당이라도 **수명 패턴**이 단편화 정도를 바꾼다.
- 이 시뮬레이터가 실제 allocator(newlib, FreeRTOS heap_4 등)와 같은 숫자를 낸다는 보장은 없다 — 전략(first/best-fit), header, 정렬이 다르다. 배울 것은 현상과 측정법이다.

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="30" x2="70" y2="210" stroke="currentColor"/><line x1="70" y1="210" x2="650" y2="210" stroke="currentColor"/> <text x="64" y="214" font-size="12" text-anchor="end">0 KB</text> <text x="64" y="169" font-size="12" text-anchor="end">5 KB</text> <text x="64" y="124" font-size="12" text-anchor="end">10 KB</text> <text x="64" y="79" font-size="12" text-anchor="end">15 KB</text> <text x="64" y="34" font-size="12" text-anchor="end">20 KB</text> <text x="70" y="240" font-size="12" text-anchor="middle">t=0</text> <text x="263" y="240" font-size="12" text-anchor="middle">t=100</text> <text x="457" y="240" font-size="12" text-anchor="middle">t=200</text>
<text x="650" y="240" font-size="12" text-anchor="middle">t=300</text> <line x1="70" y1="138" x2="650" y2="138" stroke="#d0564a" stroke-dasharray="5 3"/> <text x="650" y="134" font-size="12" text-anchor="end">8 KB 요청 + header = 8,200 B</text>
<polyline points="70,40 74,44 78,55 82,68 85,54 89,49 93,59 97,52 101,56 105,69 109,71 113,70 116,84 120,92 124,78 128,87 132,82 136,81 140,87 143,100 147,99 151,110 155,87 159,86 163,97 167,88 171,92 174,103 178,111 182,95 186,114 190,114 194,122 198,108 201,95 205,100 209,99 213,92 217,98 221,110 225,109 229,104 232,112 236,111 240,102 244,102 248,114 252,116 256,104 259,95 263,101 267,99 271,105 275,107 279,104 283,107 287,111 290,93 294,100 298,104 302,107 306,100 310,107 314,114 317,117 321,124 325,99 329,95 333,103 337,122 341,121 345,117 348,101 352,106 356,106 360,108 364,102 368,104 372,116 375,124 379,123 383,109 387,110 391,101 395,100 399,98 403,100 406,106 410,108 414,108 418,109 422,106 426,97 430,105 433,114 437,125 441,98 445,102 449,100 453,107 457,125 461,98 464,99 468,100 472,118 476,125 480,105 484,105 488,98 491,93 495,99 499,110 503,110 507,102 511,111 515,109 519,102 522,113 526,106 530,113 534,113 538,114 542,110 546,101 549,98 553,101 557,100 561,110 565,106 569,118 573,125 577,130 580,109 584,95 588,96 592,97 596,106 600,101 604,112 607,110 611,119 615,96 619,106 623,115 627,130 631,128 635,103 638,116 642,94 646,105" fill="none" stroke="#4a7bd0" stroke-width="1.5"/>
<polyline points="70,40 74,53 78,62 82,71 85,82 89,82 93,62 97,63 101,63 105,72 109,74 113,83 116,85 120,94 124,97 128,97 132,97 136,89 140,98 143,107 147,116 151,116 155,89 159,92 163,110 167,110 171,101 174,110 178,119 182,101 186,119 190,122 194,140 198,140 201,122 205,122 209,131 213,122 217,109 221,118 225,127 229,127 232,120 236,123 240,123 244,123 248,123 252,135 256,126 259,126 263,126 267,126 271,135 275,135 279,120 283,130 287,130 290,105 294,114 298,114 302,118 306,118 310,127 314,145 317,145 321,145 325,126 329,108 333,117 337,135 341,135 345,137 348,137 352,137 356,137 360,137 364,120 368,129 372,138 375,148 379,157 383,157 387,129 391,129 395,120 399,129 403,120 406,120 410,132 414,141 418,141 422,132 426,132 430,141 433,141 437,150 441,132 445,132 449,132 453,141 457,150 461,159 464,132 468,131 472,140 476,149 480,131 484,140 488,140 491,122 495,122 499,131 503,131 507,131 511,131 515,140 519,140 522,140 526,122 530,131 534,140 538,131 542,140 546,131 549,122 553,122 557,122 561,131 565,132 569,141 573,150 577,159 580,159 584,132 588,132 592,132 596,132 600,132 604,131 607,140 611,140 615,122 619,131 623,149 627,158 631,167 635,140 638,140 642,122 646,122" fill="none" stroke="#e08a3c" stroke-width="1.5"/>
<line x1="194" y1="216" x2="194" y2="224" stroke="#d0564a"/><line x1="196" y1="216" x2="196" y2="224" stroke="#d0564a"/><line x1="198" y1="216" x2="198" y2="224" stroke="#d0564a"/><line x1="200" y1="216" x2="200" y2="224" stroke="#d0564a"/><line x1="314" y1="216" x2="314" y2="224" stroke="#d0564a"/><line x1="316" y1="216" x2="316" y2="224" stroke="#d0564a"/><line x1="317" y1="216" x2="317" y2="224" stroke="#d0564a"/><line x1="319" y1="216" x2="319" y2="224" stroke="#d0564a"/><line x1="321" y1="216" x2="321" y2="224" stroke="#d0564a"/><line x1="323" y1="216" x2="323" y2="224" stroke="#d0564a"/><line x1="370" y1="216" x2="370" y2="224" stroke="#d0564a"/><line x1="372" y1="216" x2="372" y2="224" stroke="#d0564a"/><line x1="374" y1="216" x2="374" y2="224" stroke="#d0564a"/><line x1="375" y1="216" x2="375" y2="224" stroke="#d0564a"/><line x1="377" y1="216" x2="377" y2="224" stroke="#d0564a"/><line x1="379" y1="216" x2="379" y2="224" stroke="#d0564a"/><line x1="381" y1="216" x2="381" y2="224" stroke="#d0564a"/><line x1="383" y1="216" x2="383" y2="224" stroke="#d0564a"/><line x1="389" y1="216" x2="389" y2="224" stroke="#d0564a"/><line x1="414" y1="216" x2="414" y2="224" stroke="#d0564a"/><line x1="416" y1="216" x2="416" y2="224" stroke="#d0564a"/><line x1="418" y1="216" x2="418" y2="224" stroke="#d0564a"/><line x1="430" y1="216" x2="430" y2="224" stroke="#d0564a"/><line x1="432" y1="216" x2="432" y2="224" stroke="#d0564a"/><line x1="433" y1="216" x2="433" y2="224" stroke="#d0564a"/><line x1="435" y1="216" x2="435" y2="224" stroke="#d0564a"/><line x1="437" y1="216" x2="437" y2="224" stroke="#d0564a"/><line x1="439" y1="216" x2="439" y2="224" stroke="#d0564a"/><line x1="453" y1="216" x2="453" y2="224" stroke="#d0564a"/><line x1="455" y1="216" x2="455" y2="224" stroke="#d0564a"/><line x1="457" y1="216" x2="457" y2="224" stroke="#d0564a"/><line x1="459" y1="216" x2="459" y2="224" stroke="#d0564a"/><line x1="461" y1="216" x2="461" y2="224" stroke="#d0564a"/><line x1="462" y1="216" x2="462" y2="224" stroke="#d0564a"/><line x1="472" y1="216" x2="472" y2="224" stroke="#d0564a"/><line x1="474" y1="216" x2="474" y2="224" stroke="#d0564a"/><line x1="476" y1="216" x2="476" y2="224" stroke="#d0564a"/><line x1="478" y1="216" x2="478" y2="224" stroke="#d0564a"/><line x1="482" y1="216" x2="482" y2="224" stroke="#d0564a"/><line x1="484" y1="216" x2="484" y2="224" stroke="#d0564a"/><line x1="486" y1="216" x2="486" y2="224" stroke="#d0564a"/><line x1="488" y1="216" x2="488" y2="224" stroke="#d0564a"/><line x1="490" y1="216" x2="490" y2="224" stroke="#d0564a"/><line x1="515" y1="216" x2="515" y2="224" stroke="#d0564a"/><line x1="517" y1="216" x2="517" y2="224" stroke="#d0564a"/><line x1="519" y1="216" x2="519" y2="224" stroke="#d0564a"/><line x1="520" y1="216" x2="520" y2="224" stroke="#d0564a"/><line x1="522" y1="216" x2="522" y2="224" stroke="#d0564a"/><line x1="524" y1="216" x2="524" y2="224" stroke="#d0564a"/><line x1="532" y1="216" x2="532" y2="224" stroke="#d0564a"/><line x1="534" y1="216" x2="534" y2="224" stroke="#d0564a"/><line x1="536" y1="216" x2="536" y2="224" stroke="#d0564a"/><line x1="540" y1="216" x2="540" y2="224" stroke="#d0564a"/><line x1="542" y1="216" x2="542" y2="224" stroke="#d0564a"/><line x1="569" y1="216" x2="569" y2="224" stroke="#d0564a"/><line x1="571" y1="216" x2="571" y2="224" stroke="#d0564a"/><line x1="573" y1="216" x2="573" y2="224" stroke="#d0564a"/><line x1="575" y1="216" x2="575" y2="224" stroke="#d0564a"/><line x1="577" y1="216" x2="577" y2="224" stroke="#d0564a"/><line x1="578" y1="216" x2="578" y2="224" stroke="#d0564a"/><line x1="580" y1="216" x2="580" y2="224" stroke="#d0564a"/><line x1="606" y1="216" x2="606" y2="224" stroke="#d0564a"/><line x1="607" y1="216" x2="607" y2="224" stroke="#d0564a"/><line x1="609" y1="216" x2="609" y2="224" stroke="#d0564a"/><line x1="611" y1="216" x2="611" y2="224" stroke="#d0564a"/><line x1="621" y1="216" x2="621" y2="224" stroke="#d0564a"/><line x1="623" y1="216" x2="623" y2="224" stroke="#d0564a"/><line x1="625" y1="216" x2="625" y2="224" stroke="#d0564a"/><line x1="627" y1="216" x2="627" y2="224" stroke="#d0564a"/><line x1="629" y1="216" x2="629" y2="224" stroke="#d0564a"/><line x1="631" y1="216" x2="631" y2="224" stroke="#d0564a"/><line x1="633" y1="216" x2="633" y2="224" stroke="#d0564a"/><line x1="635" y1="216" x2="635" y2="224" stroke="#d0564a"/><line x1="636" y1="216" x2="636" y2="224" stroke="#d0564a"/><line x1="638" y1="216" x2="638" y2="224" stroke="#d0564a"/><line x1="640" y1="216" x2="640" y2="224" stroke="#d0564a"/>
<text x="70" y="260" font-size="12">파랑 = 총 free · 주황 = 가장 큰 연속 free 블록 · 축 아래 빨간 눈금 = 8 KB 요청 실패 (76/300)</text> <text x="70" y="278" font-size="12">총 free(파랑)는 늘 빨간 선 위인데, 주황이 선 아래로 내려갈 때마다 실패한다</text>
</svg>
```

그림 5 — 처음 300 step의 총 free(파랑)와 가장 큰 연속 free 블록(주황), 8,200 B 요청선(빨강 점선). 축 아래 빨간 눈금이 실패 시각이다. 파랑은 항상 선 위에 있는데 주황이 선 아래로 내려갈 때마다 실패한다 — **"총 여유"를 텔레메트리로 올리는 건 쓸모가 없고, "가장 큰 free 블록"(또는 할당 실패 카운터)을 올려야 하는 이유**다.

```svg
<svg viewBox="0 0 680 150" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="22" font-size="13">t=64의 20 KB 힙 (실측 시뮬레이션): 회색 = 사용 중, 초록 = 빈 구멍 19개</text> <rect x="20" y="40" width="640" height="40" fill="#888" fill-opacity="0.45" stroke="currentColor"/> <rect x="24.2" y="40" width="2.0" height="40" fill="#3f9a6b"/> <rect x="42.0" y="40" width="1.2" height="40" fill="#3f9a6b"/> <rect x="62.5" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="74.5" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="84.2" y="40" width="1.2" height="40" fill="#3f9a6b"/> <rect x="88.5" y="40" width="2.5" height="40" fill="#3f9a6b"/> <rect x="98.2" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="122.8" y="40" width="0.8" height="40" fill="#3f9a6b"/>
<rect x="133.2" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="147.0" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="167.2" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="174.2" y="40" width="8.0" height="40" fill="#3f9a6b"/> <rect x="186.0" y="40" width="3.5" height="40" fill="#3f9a6b"/> <rect x="201.5" y="40" width="0.8" height="40" fill="#3f9a6b"/> <rect x="204.0" y="40" width="2.5" height="40" fill="#3f9a6b"/> <rect x="214.0" y="40" width="2.2" height="40" fill="#3f9a6b"/> <rect x="232.5" y="40" width="5.2" height="40" fill="#3f9a6b"/> <rect x="273.5" y="40" width="32.2" height="40" fill="#3f9a6b"/>
<rect x="410.5" y="40" width="249.5" height="40" fill="#3f9a6b"/> <text x="535" y="98" font-size="12" text-anchor="middle">가장 큰 구멍 7,984 B &lt; 8,200 B</text> <text x="20" y="122" font-size="12">총 free 10,056 B (요청의 1.2배)이지만 나머지 18개 구멍은 8~1,032 B 조각 → 8 KB 할당 실패</text> <text x="20" y="140" font-size="12">0 B</text><text x="660" y="140" font-size="12" text-anchor="end">20,480 B</text>
</svg>
```

그림 6 — 첫 실패 중 하나(t=64)의 힙 스냅숏. 회색이 사용 중, 초록이 빈 구멍이다. 총 free 10,056 B로 요청의 1.2배지만, 19개 구멍 중 가장 큰 것(끝부분 7,984 B)도 8,200 B에 못 미친다. 앞쪽 12 KB에 흩어진 이벤트 기록(작은 회색 조각들)이 구멍을 쪼갰다.

### 5.5 "힙을 크게 잡으면 되지 않나?" — 숫자로 답한다

같은 시뮬레이션에서 힙 크기와 seed만 바꿔 2,000 step 동안 8 KB 실패 횟수를 셌다(seed 1~5, 같은 FirstFit·워크로드 코드).

```text
HEAP 22 KB: [51, 41, 43, 98, 56]
HEAP 23 KB: [7, 1, 8, 29, 8]
HEAP 24 KB: [0, 0, 4, 5, 6]
HEAP 26 KB: [0, 0, 0, 0, 0]
```

출력에서 볼 것: 26 KB에서 다섯 seed 모두 0회였다. 하지만 이건 "다섯 번 시험해서 안 터졌다"일 뿐 **증명이 아니다** — 여섯 번째 seed, 3일 연속 착용, 다른 사용 패턴에서 터질 수 있다(24 KB에서는 seed 1·2가 0이었지만 3~5는 실패했다). 반면 예제 7의 마지막 줄 — **크기 등급별 동시 사용 최대를 세서 pool을 잡으면 23,552 B**에서 실패가 정의상 0이다. pool이 heap보다 **더 작으면서 증명까지 된다.** 대가는 등급별 낭비(244 B 패킷이 256 B 블록, 48 B 기록이 128 B 블록)와 "등급별 최대 동시 개수"라는 가정이 지켜져야 한다는 것이다 — 그 가정은 pool의 실패 카운터로 현장에서 감시한다.

### 5.6 해법 — 고정 블록 pool + 통계 (예제 8)

무엇을 확인하는 코드인지: 크기 등급 하나당 고정 크기 블록 N개를 정적으로 잡고, 빈 블록끼리 연결 리스트(블록 자체에 next 포인터를 저장)로 O(1) 할당·해제한다. 사용량·high-water·실패 횟수를 세서 텔레메트리 레코드로 묶는다.

```c
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
/* 고정 블록 pool: O(1) alloc/free, 외부 단편화 없음, 사용량·high-water·실패를 센다 */
typedef struct { uint8_t *mem; void *free_list; uint16_t blk, n, used, hwm, fails; } pool_t;
#define POOL_DEFINE(name, BLK, N) \
    static _Alignas(8) uint8_t name##_mem[(BLK) * (N)]; static pool_t name = {name##_mem, 0, BLK, N, 0, 0, 0}
static void pool_init(pool_t *p) {
    p->free_list = NULL;
    for (int i = p->n - 1; i >= 0; i--) {                  /* 빈 블록끼리 연결 리스트 */
        void **b = (void **)(p->mem + (size_t)i * p->blk); *b = p->free_list; p->free_list = b;
    }
}
static void *pool_alloc(pool_t *p) {
    void **b = p->free_list;
    if (!b) { p->fails++; return NULL; }
    p->free_list = *b;
    if (++p->used > p->hwm) p->hwm = p->used;
    return b;
}
static void pool_free(pool_t *p, void *v) { *(void **)v = p->free_list; p->free_list = v; p->used--; }

typedef struct __attribute__((packed)) {                    /* 텔레메트리 레코드 (H1/H7로 올림) */
    uint16_t audio_hwm, audio_fails, log_hwm, log_fails, stack_hwm, arena_used_kb;
} mem_telemetry_t;
static_assert(sizeof(mem_telemetry_t) == 12, "telemetry layout is an ABI");

POOL_DEFINE(audio_pool, 1024, 5);
POOL_DEFINE(log_pool, 128, 60);
int main(void) {
    pool_init(&audio_pool); pool_init(&log_pool);
    void *a[6];
    for (int i = 0; i < 6; i++) a[i] = pool_alloc(&audio_pool);   /* 6번째는 실패해야 한다 */
    for (int i = 0; i < 5; i++) pool_free(&audio_pool, a[i]);
    for (int i = 0; i < 40; i++) pool_free(&log_pool, pool_alloc(&log_pool));
    mem_telemetry_t t = {audio_pool.hwm, audio_pool.fails, log_pool.hwm, log_pool.fails, 1328, 56};
    printf("audio: used %u, hwm %u/%u, fails %u | log: hwm %u/%u, fails %u\n", audio_pool.used,
           t.audio_hwm, audio_pool.n, t.audio_fails, t.log_hwm, log_pool.n, t.log_fails);
    printf("telemetry %zu B: stack_hwm %u B, arena %u KB\n", sizeof t, t.stack_hwm, t.arena_used_kb);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 pool.c -o pool && ./pool
```

```text
audio: used 0, hwm 5/5, fails 1 | log: hwm 1/60, fails 0
telemetry 12 B: stack_hwm 1328 B, arena 56 KB
```

출력에서 볼 것:

- 경고 0개. 6번째 오디오 블록 요청이 실패로 **세어졌다**(fails 1). pool은 실패해도 다른 등급을 망가뜨리지 않고, 실패가 "언제·어느 등급에서" 났는지 카운터로 남는다. `hwm 5/5`는 "여유 0 — 등급 크기를 늘리거나 생산자를 늦춰라"라는 신호다.
- log pool은 할당 직후 해제를 40번 반복해 hwm이 1이다. 현장 텔레메트리에서 hwm이 60에 닿는 기기가 있다면 그 기기의 사용 패턴을 조사한다(H7 3절의 fleet 분해).
- 텔레메트리 레코드 크기를 `static_assert`로 고정했다. 백엔드 파서가 이 레이아웃에 의존하므로 **레코드 형식은 ABI**다(J1의 정적 지도와 같은 사고방식). 숫자 1328과 56은 4절과 v2 arena에서 가져온 예시 값이다.
- pool의 정적 메모리는 맵 파일에 `audio_pool_mem`, `log_pool_mem` 심볼로 그대로 보인다 — 2·3절 도구와 8절 게이트가 그대로 감시한다. **힙을 pool로 바꾸면 동적 메모리 문제가 정적 메모리 문제로 바뀐다.** 정적 문제는 빌드가 잡는다.

### 5.7 "단편화 문제가 없다"를 증명하는 세 단계

1. **초기화 이후 힙 없음**: 링크된 이미지에 `malloc` 계열 심볼이 없거나(5.1절의 `nm` 검사), `--wrap=malloc`으로 초기화 완료 플래그 이후의 호출을 assert한다.
2. **pool 크기의 근거**: 등급별 동시 사용 최대를 계측(예제 5의 분포, 예제 7의 peak)과 설계(생산자·소비자 속도, 큐 깊이)로 정하고 margin을 얹는다. 근거를 예산표(I1)에 적는다.
3. **현장 감시**: pool hwm·fails, (힙을 쓰는 서드파티가 있다면) 최소 free와 가장 큰 free 블록을 텔레메트리로 올리고 soak 테스트(J6)와 fleet(H7)에서 본다.

---

## 6. ML 프레임워크 메모리 (호스트·SoC) — 무엇이 RSS를 만드나

SoC(Linux/Android)나 개발 PC에서 torch·ONNX Runtime으로 추론하면 메모리 질문이 바뀐다. 링커 맵 대신 **가상 메모리 시스템**이 숫자를 만든다. 먼저 단어부터 정리한다.

### 6.1 "메모리"라는 단어가 가리키는 일곱 가지

| 지표 | 뜻 | 어디서 | 함정 |
|---|---|---|---|
| VSZ (virtual size) | 예약한 가상 주소 공간 | `ps -o vsz` | 수백 GB도 정상. 메모리 사용량 아님 |
| RSS (resident set size) | 지금 물리 메모리에 올라온 페이지 | `ps -o rss`, `/proc/pid/status` | 공유 라이브러리 페이지를 프로세스마다 중복 계산 |
| PSS (proportional) | 공유 페이지를 공유자 수로 나눈 RSS | Linux `smaps_rollup`, Android `dumpsys meminfo` | macOS에는 없음 |
| dirty / footprint | 이 프로세스가 쓴(더럽힌) 페이지 — 버릴 수 없는 메모리 | macOS `footprint`, `vmmap` | 메모리 압박 판단에는 이게 맞다 |
| `ru_maxrss` | 생애 최대 RSS (high-water) | `getrusage` | 줄지 않음, macOS 바이트·Linux KB (D2 6.1) |
| `tracemalloc` | Python 객체 할당 | Python | torch/ORT C++ 할당은 못 봄 (D2 6.1) |
| 프레임워크 프로파일러 | 그 프레임워크가 직접 잡은 텐서·workspace | `torch.profiler`, ORT 프로파일 | allocator 캐싱·라이브러리 코드는 안 보임 |

말로 하면: "모델이 메모리를 얼마 쓰나?"라는 질문에는 **어느 열의 숫자인지**를 먼저 정해야 답할 수 있다. I5에서 "작은 모델이 160 MiB를 쓴다"는 결론이 나온 것도 측정 정의(프로세스 RSS 증분)가 프레임워크 로딩까지 포함했기 때문이었다.

### 6.2 `torch.profiler`로 시간축 메모리 타임라인 (예제 9)

D2 예제 10은 op별 할당 **합계 표**를 봤다. 여기서는 같은 이벤트를 **시간순으로 누적**해 "어느 순간 몇 바이트가 살아 있었나"(live bytes)와 그 peak의 구성을 본다. peak가 무엇으로 이루어졌는지가 최적화 대상을 정한다.

무엇을 확인하는 코드인지: 작은 2-conv 모델(입력 1×1×49×10, KWS 특징 크기)을 eager로 한 번 돌리며 할당(+)·해제(−) 이벤트를 시간순으로 더한다.

```python
import torch
from torch.profiler import profile, ProfilerActivity
torch.manual_seed(0)
m = torch.nn.Sequential(torch.nn.Conv2d(1, 32, 3, padding=1), torch.nn.ReLU(), torch.nn.Conv2d(32, 32, 3, padding=1),
                        torch.nn.ReLU(), torch.nn.Flatten(), torch.nn.Linear(32 * 49 * 10, 12)).eval()
x = torch.randn(1, 1, 49, 10)
with torch.no_grad():
    m(x)                                                         # warm-up
    with profile(activities=[ProfilerActivity.CPU], profile_memory=True) as prof:
        m(x)
# 할당(+)은 이벤트 시작 시각, 해제(−)는 끝 시각에 일어났다고 보고 시간순으로 누적한다
pts = [(e.time_range.start if e.self_cpu_memory_usage > 0 else e.time_range.end, e.name, e.self_cpu_memory_usage)
       for e in prof.events() if e.self_cpu_memory_usage]
cur = peak = 0
for t, name, d in sorted(pts):
    cur += d; peak = max(peak, cur)
    print(f"{name:<27} {d:+9,d} B   live {cur:9,d} B")
print(f"peak live = {peak:,} B   (활성값 1개 = 32·49·10·4 = {32*49*10*4:,} B)")
```

```text
aten::empty                   +62,720 B   live    62,720 B
aten::clamp_min               +62,720 B   live   125,440 B
[memory]                      -62,720 B   live    62,720 B
aten::empty                  +564,480 B   live   627,200 B
aten::resize_                 +62,720 B   live   689,920 B
aten::_slow_conv2d_forward   -564,480 B   live   125,440 B
[memory]                      -62,720 B   live    62,720 B
aten::clamp_min               +62,720 B   live   125,440 B
[memory]                      -62,720 B   live    62,720 B
aten::addmm                       +48 B   live    62,768 B
[memory]                      -62,720 B   live        48 B
[memory]                          -48 B   live         0 B
```

```text
peak live = 689,920 B   (활성값 1개 = 32·49·10·4 = 62,720 B)
```

출력에서 볼 것:

- 이벤트 하나하나가 손계산과 맞는다. 활성값 하나 = `32 × 49 × 10 × 4 B = 62,720 B`. ReLU(`clamp_min`)는 새 텐서를 만든다(eager는 in-place가 아니다 — D2 예제 10과 같은 결론). 다 쓴 텐서는 `[memory]` 이벤트로 −62,720.
- **peak 689,920 B = 활성값의 11배**. 구성은 `relu1 출력 62,720 + im2col workspace 564,480 + conv2 출력 62,720`. workspace 크기를 손으로: conv2의 im2col 행렬은 `(C_in·k·k) × (H·W) = (32·3·3) × (49·10) = 288 × 490`개 float → `× 4 B = 564,480 B`. **peak의 82 %가 활성값이 아니라 커널의 임시 버퍼**다. MCU의 CMSIS-NN은 이런 workspace를 작은 타일로 잡으므로(D2 4.1절) 이 숫자를 배포 예산으로 쓰면 안 된다.
- 첫 conv(입력 채널 1)에서는 workspace 이벤트가 없었다 — 다른 커널 경로를 탄 것으로 보인다. 같은 프레임워크 안에서도 op 모양에 따라 메모리 거동이 달라진다.
- 처리 규칙에 주의: 부모 이벤트(`_slow_conv2d_forward`)의 self 사용량이 −564,480인 것은 자식(`empty`)이 잡은 workspace를 부모가 끝날 때 놓았기 때문이다. 처음에 모든 이벤트를 **시작 시각**으로 정렬했더니 해제가 할당보다 먼저 와서 live가 −501,760 B로 음수가 됐다. 해제는 **끝 시각**에 놓아야 타임라인이 물리적으로 말이 된다. 세 번 다시 돌려 peak가 같음(689,920)을 확인했다.

GPU라면 `torch.cuda.max_memory_allocated()`·`torch.cuda.memory_summary()`가 caching allocator의 통계를 바로 준다(이 Mac에는 CUDA가 없어 실행하지 않았다).

### 6.3 ONNX Runtime의 arena와 memory pattern — 켜고 끄면 RSS가 어떻게 바뀌나 (예제 10)

ORT CPU 실행에는 메모리 옵션 두 개가 있다.

- `enable_cpu_mem_arena`: 텐서 메모리를 OS에 돌려주지 않고 arena에 모아 재사용한다(기본 켜짐). 빠르지만 한번 커지면 잘 안 줄어든다.
- `enable_mem_pattern`: 첫 실행의 할당 패턴을 기억해 다음 실행에서 필요한 메모리를 **한 덩어리로 미리** 잡는다(기본 켜짐). 입력 shape가 바뀌면 패턴을 다시 만든다.

무엇을 확인하는 코드인지: 3층 conv(64채널, 가중치 305 KB)를 ONNX로 내보내 네 조합을 **각각 새 프로세스**에서 띄우고, 입력 224 → 448 → 224 순서로 돌리며 단계별 RSS를 잰다. 큰 입력 한 번 뒤에 메모리가 돌아오는지 보려는 것이다.

```python
import subprocess, sys
CHILD = r'''
import os, sys, subprocess, numpy as np, onnxruntime as ort
def rss_mb():                                        # 현재 RSS (ps는 KB 단위)
    return int(subprocess.check_output(["ps", "-o", "rss=", "-p", str(os.getpid())])) / 1024
arena, pattern = sys.argv[1] == "1", sys.argv[2] == "1"
r0 = rss_mb()
so = ort.SessionOptions()
so.enable_cpu_mem_arena, so.enable_mem_pattern = arena, pattern
so.intra_op_num_threads = 1
s = ort.InferenceSession("cnn.onnx", so, providers=["CPUExecutionProvider"])
r1 = rss_mb()
out = [r0, r1]
for hw in (224, 448, 224):                           # 큰 입력 한 번 뒤 다시 작은 입력
    x = np.random.default_rng(0).standard_normal((1, 3, hw, hw), dtype=np.float32)
    for _ in range(5): s.run(None, {"x": x})
    out.append(rss_mb())
print(" ".join(f"{v:6.1f}" for v in out))
'''
print("arena pattern | 시작   세션후  224후  448후  224후  (RSS MB)")
for a, p in ((1, 1), (1, 0), (0, 1), (0, 0)):
    r = subprocess.run([sys.executable, "-c", CHILD, str(a), str(p)], capture_output=True, text=True)
    print(f"  {a}     {p}    | {r.stdout.strip()}")
```

(`cnn.onnx`는 `Conv(3→64) ReLU Conv(64→64) ReLU Conv(64→64) ReLU GAP Linear(64→10)`을 `torch.onnx.export(..., dynamic_axes={"x": {2: "h", 3: "w"}}, dynamo=False)`로 내보낸 것. 가중치 305,192 B.)

```text
arena pattern | 시작   세션후  224후  448후  224후  (RSS MB)
  1     1    | 29.4   35.5   90.9  197.2  197.8
  1     0    | 29.5   35.6   65.4  169.7  169.7
  0     1    | 29.5   35.7   88.9  189.2  189.6
  0     0    | 29.6   35.7   64.5  164.8  165.1
```

출력에서 볼 것 (두 번 돌려 ±0.5 MB 안에서 같았다):

- 모델 가중치는 0.3 MB인데 세션 생성만으로 RSS +6 MB, 224 입력에서 +30~55 MB, 448 입력에서 +130~160 MB. 448 입력의 활성값 하나가 `64 × 448 × 448 × 4 B = 51.4 MB`이므로 **메모리는 모델 크기가 아니라 입력 해상도 × 채널이 정한다**(D2 3절).
- `enable_mem_pattern`을 끄면 224 단계에서 약 25 MB, 448 단계에서 약 24~28 MB 줄었다. 패턴 플래너가 한 덩어리로 미리 잡는 몫으로 보인다(정확한 내역은 이 실험으로 분해하지 못했다).
- arena를 꺼도 차이는 1~8 MB뿐이고, **네 조합 모두 448 → 224로 돌아와도 RSS가 줄지 않았다.** arena를 껐는데도 안 줄어드는 이유는 OS의 `malloc` 자체가 해제한 큰 블록의 물리 페이지를 바로 돌려주지 않기 때문으로 보인다(6.4절 `vmmap`에서 `MALLOC_LARGE`가 그 페이지들이다). 즉 **"arena 끄기 = 메모리 반환"이 아니다.** 메모리를 정말 돌려받아야 하는 서비스는 프로세스를 다시 띄우거나 입력 shape를 고정해 peak 자체를 줄인다.
- 실무 결론: 입력 shape가 고정인 온디바이스 추론에서는 두 옵션을 켜 두는 게 속도에 유리하고, 메모리 예산은 "**가장 큰 입력으로 한 번 돌린 뒤의 RSS**"로 잡아야 한다. 처음 몇 번의 작은 입력으로 잰 RSS는 과소평가다.

### 6.4 `vmmap`과 `footprint` — 프로세스를 해부한다 (예제 11)

무엇을 확인하는 코드인지: 448 입력으로 세 번 돌린 뒤 잠든 ORT 프로세스를 `ps`, `vmmap --summary`, `footprint`로 동시에 보고, 숫자들이 무엇을 세는지 대조한다. 셋 다 **sudo 없이** 내 프로세스에 대해 동작했다.

```python
# hold_ort.py — 측정 대상 프로세스
import time, numpy as np, onnxruntime as ort
so = ort.SessionOptions(); so.intra_op_num_threads = 1
s = ort.InferenceSession("cnn.onnx", so, providers=["CPUExecutionProvider"])
x = np.zeros((1, 3, 448, 448), np.float32)
for _ in range(3): s.run(None, {"x": x})
print("ready", flush=True); time.sleep(20)
```

```sh
.venv/bin/python hold_ort.py & sleep 6; P=$(pgrep -f hold_ort.py)
echo "ps rss KB: $(ps -o rss= -p $P)  vsz KB: $(ps -o vsz= -p $P)"
vmmap --summary $P | grep -E "Physical footprint|^TOTAL|^MALLOC_LARGE |^MALLOC_SMALL  |^__TEXT |DefaultMallocZone"
footprint $P | head -9
```

```text
ps rss KB: 248064  vsz KB: 442217024
Physical footprint:         227.5M
Physical footprint (peak):  227.5M
MALLOC_LARGE                     262.3M   202.3M   202.3M       0K       0K       0K       0K        6         see MALLOC ZONE table below
MALLOC_SMALL                      36.0M    14.5M    14.5M       0K       0K       0K       0K        9         see MALLOC ZONE table below
__TEXT                           380.5M   183.9M       0K       0K       0K       0K       0K      380 
TOTAL                              1.5G   506.5M   227.5M       0K       0K       0K       0K     2046 
DefaultMallocZone_0x105434000     311.1M     217.8M     217.8M         0K      57145     275.3M         0K      0%      21
======================================================================
Python [27464]: 64-bit    Footprint: 227 MB (16384 bytes per page)
======================================================================

  Dirty      Clean  Reclaimable    Regions    Category
    ---        ---          ---        ---    ---
 202 MB        0 B          0 B          6    MALLOC_LARGE
  15 MB        0 B          0 B         11    MALLOC_SMALL
7152 KB        0 B          0 B         34    untagged (VM_ALLOCATE)
```

(`vmmap`의 열 순서: VIRTUAL SIZE, RESIDENT SIZE, DIRTY SIZE, SWAPPED, … REGION COUNT. 줄 머리와 나머지 열은 생략.)

출력에서 볼 것:

- 같은 프로세스에 숫자가 다섯 개다: VSZ **421 GB**(가상 예약 — 의미 없음), `vmmap` resident **506.5 MB**, `ps` RSS **242 MB**, physical footprint **227.5 MB**, 그중 `MALLOC_LARGE` dirty **202 MB**. 모델 가중치는 **0.3 MB**.
- `__TEXT` 183.9 MB resident / 0 dirty: torch·ORT·numpy·Python의 **코드 페이지**다. 깨끗한(clean) 공유 페이지라 메모리가 부족하면 OS가 버렸다가 다시 읽을 수 있다. 그래서 footprint에는 안 들어간다. `vmmap` resident가 506 MB로 큰 건 dyld 공유 캐시의 시스템 라이브러리 페이지까지 세기 때문이다.
- `MALLOC_LARGE` 202 MB dirty: 448 입력의 활성값·workspace를 담았던 큰 블록들이다. 추론이 끝나 텐서는 해제됐지만 페이지는 여전히 dirty로 남아 있다 — 6.3절에서 RSS가 안 줄어든 바로 그 메모리.
- `DefaultMallocZone` 줄의 `% FRAG` 열이 **0 %**다. `vmmap`은 malloc zone별 단편화율을 보여 준다. 5절의 개념이 OS allocator 수준에서도 그대로 지표로 쓰인다.
- 메모리 압박(jetsam, Android의 lmkd)이 보는 숫자는 footprint/dirty 쪽이다. **RSS는 공유 코드 페이지 때문에 부풀고, VSZ는 아예 무관하다.**

```svg
<svg viewBox="0 0 680 220" xmlns="http://www.w3.org/2000/svg">
<text x="20" y="20" font-size="13">같은 프로세스, 다섯 개의 "메모리" (ORT, 448×448 입력 3회 후, MB)</text> <text x="235" y="52" font-size="12" text-anchor="end">모델 가중치 (cnn.onnx)</text> <rect x="245" y="39" width="1.5" height="20" fill="#3f9a6b" fill-opacity="0.8"/> <text x="252.5" y="53" font-size="12">0.3</text> <text x="235" y="84" font-size="12" text-anchor="end">MALLOC_LARGE dirty (활성값·arena)</text> <rect x="245" y="71" width="159.8" height="20" fill="#e08a3c" fill-opacity="0.8"/> <text x="410.8" y="85" font-size="12">202.3</text> <text x="235" y="116" font-size="12" text-anchor="end">physical footprint</text> <rect x="245" y="103" width="179.7" height="20" fill="#4a7bd0" fill-opacity="0.8"/>
<text x="430.7" y="117" font-size="12">227.5</text> <text x="235" y="148" font-size="12" text-anchor="end">ps RSS</text> <rect x="245" y="135" width="191.3" height="20" fill="#4a7bd0" fill-opacity="0.8"/> <text x="442.3" y="149" font-size="12">242.2</text> <text x="235" y="180" font-size="12" text-anchor="end">vmmap resident (공유 lib 포함)</text> <rect x="245" y="167" width="400.0" height="20" fill="#888" fill-opacity="0.8"/> <text x="651.0" y="181" font-size="12">506.5</text> <text x="20" y="208" font-size="12">VSZ(가상 주소 예약)는 421 GB — 막대로 그릴 수 없을 만큼 크고, 메모리 사용량이 아니다</text>
</svg>
```

그림 7 — 같은 ORT 프로세스를 다섯 가지 자로 잰 값(MB, 실측). 모델 가중치(초록, 0.3 MB)는 막대가 거의 보이지 않는다. 메모리의 대부분은 활성값·workspace를 담았던 malloc 블록(주황)이고, RSS·resident는 여기에 공유 라이브러리 코드 페이지가 더해진 값이다.

### 6.5 "RSS가 모델 크기의 3배" — 분해 체크리스트

면접 단골 질문이다. 3배는 오히려 작은 편이고, 위 실험은 footprint 기준 약 750배, RSS 기준 약 800배였다. 분해 순서:

| 항목 | 크기를 정하는 것 | 확인 방법 |
|---|---|---|
| 가중치 | 파라미터 수 × 바이트 (+ 런타임이 변환한 사본 — 예: prepacked weights) | 모델 파일 크기와 세션 생성 직후 Δ |
| 활성값 peak | 입력 해상도·batch·채널, 그래프 구조 | D2 추정기, 프로파일러 타임라인(6.2) |
| 커널 workspace | im2col·GEMM 패킹, conv 알고리즘 선택 | 프로파일러의 `empty` 이벤트 |
| arena·pattern 캐싱 | 지금까지 본 **가장 큰** 입력 | 옵션 on/off 비교 (6.3) |
| allocator 보유 | 해제했지만 반환 안 된 페이지 | `vmmap`의 MALLOC_LARGE dirty |
| 프레임워크 코드·데이터 | torch/ORT 라이브러리 크기 | `__TEXT` (clean), `__DATA` (dirty) |
| 스레드 스택·스레드 풀 | `intra_op_num_threads` × 스택 | `vmmap`의 Stack |
| 인터프리터 | Python 자체, import한 모듈 | 빈 프로세스의 baseline |

말로 하면: **모델 크기에 비례하는 건 첫 줄 하나뿐**이다. 나머지는 입력 크기, 프레임워크 선택, allocator 정책이 정한다. 그래서 SoC 배포에서 메모리를 줄이는 손잡이는 "모델을 더 양자화"보다 "입력 shape 고정 + peak 줄이기 + 가벼운 런타임(ORT minimal build, TFLite, QNN 등) + 프로세스 하나에 여러 모델 공유"인 경우가 많다.

Linux/Android에서는 같은 해부를 `/proc/<pid>/smaps_rollup`(Rss·Pss·Private_Dirty), Android `dumpsys meminfo <pkg>`(Native Heap, Graphics 등), 힙 프로파일러 heaptrack이나 valgrind massif로 한다(이 노트에서는 실행하지 않았다 — 이름과 용도만). GPU/NPU가 쓰는 dma-buf·ION 버퍼는 프로세스 RSS에 안 잡히고 별도 카운터로 보이는 경우가 많으니, NPU 추론의 메모리는 벤더 도구(QNN 프로파일러 등, F4)와 시스템 전체 meminfo를 같이 본다.

---

## 7. MCU 런타임 계측 — 현장에서 high-water를 텔레메트리로

### 7.1 무엇을 셀 것인가

정적 분석과 데스크 테스트는 출시 전 이야기다. 현장의 수만 대는 우리가 시험하지 않은 입력과 사용 패턴을 만난다. 그래서 **최댓값과 실패 횟수를 기기 안에서 세서** 올려 보낸다. 평균이나 현재값은 거의 쓸모없다 — 메모리 사고는 꼬리에서 난다.

| 지표 | 어떻게 얻나 | 왜 |
|---|---|---|
| task별 스택 high-water | FreeRTOS `uxTaskGetStackHighWaterMark()`, Zephyr `k_thread_stack_space_get()` (painting 방식, D2 6.3) | 정적 분석 검증, 스택 축소 근거 |
| MSP(ISR) high-water | 시작 때 MSP 영역 painting, 주기적으로 스캔 | ISR 중첩 가정 검증 |
| arena 사용량 | TFLM `arena_used_bytes()` (F2) — 모델 로드 시 1회 | 모델 OTA 후 arena 여유 확인 (J5) |
| pool hwm·fails | 예제 8의 카운터 | 등급 크기 검증 |
| 힙 최소 여유·가장 큰 블록 | FreeRTOS `xPortGetMinimumEverFreeHeapSize()`, `vPortGetHeapStats()`(heap_4/5), Zephyr `sys_heap_runtime_stats_get()`, newlib `mallinfo()` | 서드파티 힙 감시 |
| 할당 실패 카운터 | `vApplicationMallocFailedHook()` 같은 훅 | 실패는 0이어야 한다 |
| 리셋 원인 | hard fault·watchdog 리셋 레지스터 + fault 시 SP 값 | 스택 overflow 사후 분석 |

API 이름은 RTOS 버전마다 조금씩 다르니 쓰기 전에 문서를 확인한다(이 노트에서 컴파일하지 않았다). 특히 FreeRTOS의 `uxTaskGetStackHighWaterMark()`는 반환값이 **바이트가 아니라 word(`StackType_t`) 단위의 남은 양**이고, "사용량"이 아니라 "**지금까지 가장 적었던 여유**"다. 4 B word인 Cortex-M에서 반환값 50은 "최소 여유 200 B"다. Zephyr는 `CONFIG_THREAD_ANALYZER`를 켜면 스레드별 스택 사용량을 주기적으로 출력해 주는 thread analyzer가 있다.

### 7.2 텔레메트리 레코드로 묶어 올린다

```
부팅 · 모델 로드 시 1회 : fw 버전, 모델 해시, arena_used_bytes, arena 크기
주기적 (예: 1시간) 또는 이상 시 : task별 stack 최소 여유, MSP hwm, pool hwm·fails, heap min-ever-free
리셋 직후 : 이전 리셋 원인, fault 시 PC·SP (no-init RAM에 남겨 둔 값)
```

설계 원칙은 Don이 SSD 텔레메트리에서 쓰던 것과 같다: **카운터는 단조 증가(래치), 레코드는 고정 레이아웃(예제 8의 `static_assert`), 수집 비용은 무시할 수준**. painting 스캔은 스택 크기에 비례하는 시간이 드니 idle task에서 조금씩 한다.

백엔드에서는 H7의 방식 그대로 본다: fw 버전별로 `stack 최소 여유`의 분포(p1, 최솟값)를 그리고, 새 릴리스에서 분포가 아래로 이동하면 알림. 예를 들어 "v2.3에서 추론 task 최소 여유의 p1이 900 B → 180 B" 같은 신호는 크래시가 늘기 **전에** 온다. 평균은 거의 안 움직이므로 반드시 꼬리를 본다(H7 3.4절의 희석 문제).

### 7.3 arena는 "한 번 재고 끝"이 아니다

TFLM arena는 `AllocateTensors()` 때 계획이 고정되므로 같은 모델이면 사용량이 매번 같다. 하지만 OTA로 모델이 바뀌면(J5) arena 요구량이 바뀐다. 그래서:

- 모델 패키지 메타데이터에 "필요 arena 바이트"를 넣고, 펌웨어는 로드 전에 자기 arena와 비교해 **거부**할 수 있어야 한다.
- 로드 후 실제 `arena_used_bytes()`를 텔레메트리로 올려 메타데이터와 대조한다(호스트 빌드의 TFLM 값과 기기 값이 다르면 커널 구현 차이 — CMSIS-NN scratch 등 — 를 의심).
- arena를 "추정 × 2"로 잡아 두었다면(D2 6.4) 현장 데이터로 줄일 근거가 생긴다.

---

## 8. 메모리 회귀를 CI에서 막기

### 8.1 무엇을 게이트할까

I1에서 예산을 세웠고, J1·I1 10절에서 `static_assert`로 "합이 넘으면 빌드 실패"를 걸었다. 하지만 `static_assert`는 **소스에 적힌 숫자**만 검사한다. 실제 링크 결과(라이브러리가 끌고 온 코드, 컴파일러가 바꾼 프레임, 정렬 틈)는 빌드 산출물을 읽어야 안다. CI 게이트는 다섯 종류를 본다.

| 게이트 | 입력 | 규칙 예 | 실패 시 |
|---|---|---|---|
| 영역 한도 | 맵 → flash/RAM 합 | RAM ≤ 128 KB − 예비 12 KB | 차단 (승인으로도 못 넘김) |
| 증가 한도 | 기준 빌드 대비 Δ | flash +8 KB, RAM +4 KB 초과 시 | 승인 라벨이 있으면 통과 |
| 심볼 예산 | 특정 심볼 크기 | `g_kws_arena` ≤ 52 KB | 차단 — 모델 팀과 협상 |
| 스택 | `stackcheck` 최악 + 예외 프레임 | 각 task 스택 크기의 80 % 이하 | 차단 |
| 힙 금지 | `nm` | `malloc` 계열 심볼 0개 | 차단 |

"예비(reserve)"는 I1 8절의 margin을 코드로 옮긴 것이다. 예비를 깎는 건 PR 하나가 아니라 **예산 소유자의 결정**이어야 하므로, 영역 한도 위반은 승인 라벨로도 못 넘기게 한다.

### 8.2 `mem_gate.py` — 예산 JSON + size diff로 PASS/FAIL (예제 12)

```text
// budget.json — 예산의 단일 출처 (I6 12.3절). 숫자는 이 예시 MCU 기준
{
  "region":  {"flash": {"limit": 98304, "max_growth": 8192},
              "ram":   {"limit": 98304, "max_growth": 4096}},
  "symbol":  {"g_kws_arena": 53248, "g_audio_dma": 2048},
  "reserve": {"ram": 12288}
}
```

(JSON은 주석을 허용하지 않으므로 실제 파일에는 첫 줄 주석이 없다.)

무엇을 확인하는 코드인지: 기준 빌드와 PR 빌드의 맵, 예산 JSON을 받아 영역 한도·증가 한도·심볼 예산을 검사하고, 3절의 diff 표와 판정을 markdown으로 출력한다. 종료 코드 1이면 CI가 PR을 막는다.

```python
"""mem_gate.py — 예산(JSON) + 기준 빌드 대비 증가량으로 PR을 통과/차단한다. 종료 코드 1 = 차단."""
import json, sys
from fwsize import parse_map
from sizediff import diff, by_symbol

base_map, new_map, budget_path = sys.argv[1:4]
allow_growth = "--approved-growth" in sys.argv          # PR에 사람이 승인 라벨을 붙인 경우
B = json.load(open(budget_path))
ua, ub, table = diff(base_map, new_map, top=6)
fails = []
for reg, rule in B["region"].items():
    room = rule["limit"] - B.get("reserve", {}).get(reg, 0)
    if ub[reg] > room:
        fails.append(f"{reg} {ub[reg]:,} B > 한도 {room:,} B (limit {rule['limit']:,} − 예비)")
    if ub[reg] - ua[reg] > rule["max_growth"] and not allow_growth:
        fails.append(f"{reg} 증가 {ub[reg]-ua[reg]:+,} B > 허용 {rule['max_growth']:,} B (승인 필요)")
sizes = {k[2]: v for k, v in by_symbol(parse_map(new_map)).items()}
for sym, limit in B["symbol"].items():
    if sizes.get(sym, 0) > limit: fails.append(f"`{sym}` {sizes[sym]:,} B > 심볼 예산 {limit:,} B")
print(table)
print("\n" + ("**MEMORY GATE: FAIL**\n" + "\n".join(f"- {f}" for f in fails) if fails
              else f"**MEMORY GATE: PASS** (flash {ub['flash']:,} / ram {ub['ram']:,} B)"))
sys.exit(1 if fails else 0)
```

세 가지 PR을 흉내 낸다. (a) KWS arena만 48 → 50 KB로 늘린 작은 PR(`./build.sh v1b kws.bin -DKWS_ARENA_KB=50`), (b) 제스처 기능 PR(v2), (c) (b)에 사람이 "증가 승인" 라벨을 붙인 경우.

```sh
.venv/bin/python mem_gate.py fw/v1/fw.map fw/v1b/fw.map budget.json; echo "exit=$?"
```

```text
| 영역 | before | after | Δ |
|---|---:|---:|---:|
| flash | 43,120 | 43,120 | +0 |
| ram | 59,500 | 61,548 | +2,048 |

| 심볼 | 영역 | 섹션 | before | after | Δ | 상태 |
|---|---|---|---:|---:|---:|---|
| `g_kws_arena` | ram | .bss | 49,152 | 51,200 | +2,048 | changed |

**MEMORY GATE: PASS** (flash 43,120 / ram 61,548 B)
exit=0
```

```sh
.venv/bin/python mem_gate.py fw/v1/fw.map fw/v2/fw.map budget.json; echo "exit=$?"
```

```text
(diff 표 생략 — 3.2절 표의 상위 6행과 같다)

**MEMORY GATE: FAIL**
- flash 증가 +32,936 B > 허용 8,192 B (승인 필요)
- ram 90,236 B > 한도 86,016 B (limit 98,304 − 예비)
- ram 증가 +30,736 B > 허용 4,096 B (승인 필요)
- `g_kws_arena` 57,344 B > 심볼 예산 53,248 B
exit=1
```

```sh
.venv/bin/python mem_gate.py fw/v1/fw.map fw/v2/fw.map budget.json --approved-growth; echo "exit=$?"
```

```text
(diff 표 생략)

**MEMORY GATE: FAIL**
- ram 90,236 B > 한도 86,016 B (limit 98,304 − 예비)
- `g_kws_arena` 57,344 B > 심볼 예산 53,248 B
exit=1
```

출력에서 볼 것:

- (a)는 RAM +2,048 B로 증가 한도(4 KB) 안, arena 50 KB가 심볼 예산(52 KB) 안이라 **PASS, exit 0**. 리뷰어는 diff 표 한 줄만 보면 된다.
- (b)는 네 가지 이유로 **FAIL**: flash·RAM 증가가 한도 초과(승인 필요), RAM 90,236 B가 예비를 뺀 한도 86,016 B 초과, KWS arena 56 KB가 심볼 예산 52 KB 초과. 실패 이유가 **사람이 읽는 문장**으로 나온다 — "CI가 빨간불"만으로는 아무도 안 고친다.
- (c) 승인 라벨을 붙여도 증가 한도 두 줄만 사라지고 **영역 한도와 심볼 예산은 남아 여전히 FAIL**이다. 큰 기능은 들어올 수 있지만 예비를 먹거나 남의 예산을 넘는 건 PR 수준에서 결정할 수 없다는 규칙을 코드로 표현했다. 이 PR이 들어오려면 (1) 스택 8 KB를 4절 계산대로 2 KB로 줄여 6 KB를 확보하거나, (2) KWS arena 증가를 모델 팀과 다시 협상하거나, (3) 예산 소유자가 `budget.json`을 바꾸는 별도 PR을 승인해야 한다 — 마지막 것도 **기록이 남는 결정**이 된다.

### 8.3 파이프라인에서의 위치

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="k1a8" viewBox="0 0 8 8" refX="7" refY="4" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs> <rect x="20" y="30" width="130" height="40" rx="5" fill="none" stroke="currentColor"/> <text x="85" y="48" font-size="12" text-anchor="middle">기준 빌드</text><text x="85" y="63" font-size="12" text-anchor="middle">(merge-base)</text> <rect x="20" y="100" width="130" height="40" rx="5" fill="none" stroke="currentColor"/> <text x="85" y="118" font-size="12" text-anchor="middle">PR 빌드</text><text x="85" y="133" font-size="12" text-anchor="middle">fw.elf · fw.map · .su</text>
<rect x="190" y="30" width="120" height="40" rx="5" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="250" y="55" font-size="12" text-anchor="middle">sizediff.py</text> <rect x="190" y="100" width="120" height="40" rx="5" fill="none" stroke="#4a7bd0" stroke-width="1.5"/> <text x="250" y="118" font-size="12" text-anchor="middle">stackcheck.py</text><text x="250" y="133" font-size="12" text-anchor="middle">nm 힙 검사</text> <rect x="190" y="170" width="120" height="40" rx="5" fill="none" stroke="#3f9a6b" stroke-width="1.5"/> <text x="250" y="188" font-size="12" text-anchor="middle">budget.json</text><text x="250" y="203" font-size="12" text-anchor="middle">(예산 소유자)</text>
<rect x="350" y="80" width="120" height="60" rx="5" fill="none" stroke="currentColor" stroke-width="2"/> <text x="410" y="106" font-size="13" text-anchor="middle">mem_gate.py</text><text x="410" y="124" font-size="12" text-anchor="middle">한도 · 증가 · 심볼</text> <rect x="510" y="30" width="150" height="40" rx="5" fill="none" stroke="#3f9a6b" stroke-width="2"/> <text x="585" y="55" font-size="12" text-anchor="middle">PASS → merge</text> <rect x="510" y="100" width="150" height="40" rx="5" fill="none" stroke="#d0564a" stroke-width="2"/> <text x="585" y="118" font-size="12" text-anchor="middle">FAIL → PR 코멘트</text><text x="585" y="133" font-size="12" text-anchor="middle">diff 표 + 이유</text>
<rect x="510" y="170" width="150" height="40" rx="5" fill="none" stroke="#e08a3c" stroke-width="1.5"/> <text x="585" y="188" font-size="12" text-anchor="middle">승인 라벨: 증가만 허용</text><text x="585" y="203" font-size="12" text-anchor="middle">한도·예비는 불가</text> <line x1="150" y1="50" x2="188" y2="50" stroke="currentColor" marker-end="url(#k1a8)"/> <line x1="150" y1="115" x2="188" y2="60" stroke="currentColor" marker-end="url(#k1a8)"/> <line x1="150" y1="120" x2="188" y2="120" stroke="currentColor" marker-end="url(#k1a8)"/> <line x1="310" y1="55" x2="348" y2="92" stroke="currentColor" marker-end="url(#k1a8)"/> <line x1="310" y1="120" x2="348" y2="112" stroke="currentColor" marker-end="url(#k1a8)"/>
<line x1="310" y1="185" x2="348" y2="130" stroke="currentColor" marker-end="url(#k1a8)"/> <line x1="470" y1="95" x2="508" y2="55" stroke="#3f9a6b" marker-end="url(#k1a8)"/> <line x1="470" y1="115" x2="508" y2="120" stroke="#d0564a" marker-end="url(#k1a8)"/> <line x1="585" y1="140" x2="585" y2="168" stroke="#e08a3c" marker-end="url(#k1a8)"/> <text x="20" y="238" font-size="12">매 커밋 실행 · 수 초 · 정적 크기는 결정적이라 잡음 바닥(A/A)이 필요 없다 — latency 회귀(I5 7.2)와 다른 점</text>
</svg>
```

그림 8 — 메모리 게이트의 위치. 기준 빌드와 PR 빌드의 산출물(맵·`.su`·ELF)을 도구가 읽고, 예산 JSON과 비교해 판정한다. 실패하면 diff 표와 이유가 PR 코멘트로 붙는다. J6 5절 파이프라인의 "커밋마다" 차선에 들어가는 항목이다.

### 8.4 운영 규칙

- **기준선은 merge-base 빌드**다. "어제 nightly"와 비교하면 다른 PR의 변화가 섞인다. 기준 빌드의 맵을 아티팩트로 보관하면 매번 다시 빌드할 필요가 없다.
- **같은 툴체인 버전**으로 두 빌드를 만든다. 컴파일러 업그레이드는 그 자체로 별도 PR이 되어야 diff가 해석된다(3.3절 표의 `.text` 산발 변화).
- 정적 크기는 **결정적**이다. latency처럼 A/A로 잡음 바닥을 잴 필요가 없고, 임계값을 1 B 단위로 걸어도 된다. 대신 빌드가 재현 가능해야 한다(타임스탬프·경로가 바이너리에 들어가지 않게).
- 리포트는 PR에 붙는 **표 하나**로 끝나야 한다. 링크 타고 들어가야 보이는 리포트는 안 읽힌다.
- 추세도 저장한다: 매 merge의 flash/RAM/스택 최악을 시계열로 남기면 "6개월 동안 RAM 여유가 매달 1.5 KB씩 줄고 있다" 같은 대화를 예산 리뷰(I6 12.2절)에서 할 수 있다.

---

## 9. 임베디드 관점에서 다시 보기 — 측정 플레이북

질문 하나에 도구 하나. 면접이나 현장에서 "그걸 어떻게 재죠?"에 바로 답하기 위한 표다.

| 질문 | 1차 도구 | 이 노트의 예제 | 함정 |
|---|---|---|---|
| flash를 누가 먹나 | 맵 파일 집계 (오브젝트·심볼) | 예제 2 | 정렬 틈, LTO, `.data` 이중 계산 |
| 이 PR이 왜 커졌나 | 두 맵의 심볼 diff | 예제 3 | 정렬 순서 비결정성, rename |
| 최악 스택은 | `.su` + 호출 그래프 + ISR 프레임 | 예제 4 | 재귀, 함수 포인터, 라이브러리 |
| 실제로 쓴 스택은 | painting, `uxTaskGetStackHighWaterMark` | D2 예제 12, 7절 | 최악 경로를 안 돌리면 과소 |
| 힙이 새나 | 계측 allocator, `leaks`, LSan(Linux) | 예제 5·6 | 계측이 `leaks`를 가림 |
| 단편화로 실패하나 | 가장 큰 free 블록·실패 카운터 | 예제 7 | 총 free는 거짓말 |
| 힙을 없앨 수 있나 | pool + 등급별 hwm | 예제 8 | 등급 낭비, 최대 동시 개수 가정 |
| arena 여유는 | `arena_used_bytes`, 메타데이터 대조 | F2, 7.3절 | OTA 후 변화 |
| ML 프로세스 메모리 구성 | 프로파일러 타임라인, `vmmap`, `footprint` | 예제 9·10·11 | RSS ≠ 모델, arena 미반환 |
| 회귀를 어떻게 막나 | 예산 JSON + 게이트 | 예제 12 | 예비를 PR이 먹지 못하게 |

Don의 강점이 그대로 쓰이는 지점: SSD 펌웨어의 버퍼 풀·DMA 디스크립터 풀은 이미 pool 설계였고, 양산 텔레메트리의 최소 여유 카운터는 7절 그대로다. 새로 배울 것은 (1) ML 런타임의 arena와 프레임워크 메모리 모델, (2) 이것들을 **빌드 산출물 기반 자동화**로 묶어 팀의 PR 흐름에 넣는 것이다.

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| `size`의 `text`만 보고 flash 판단 | 실제 flash가 수십~수백 B 더 큼, 큰 `.data`면 KB 단위 | `.data` 초기값 원본이 flash에도 있음 | flash = text + data, 맵의 LMA로 확인 |
| 스택을 "RAM 끝에서 알아서 자라게" 둠 | 정적 데이터가 늘자 랜덤 hard fault | 스택 공간이 조용히 줄어듦 | `.stack` 섹션으로 예약 + 링커 `ASSERT` |
| 맵 집계 합계가 `size`와 안 맞는데 무시 | 리포트를 아무도 안 믿음 | 정렬 틈·링커 생성 섹션 누락 | gap 행 추가, 기존 도구와 바이트 단위로 맞추기 |
| diff 리포트 정렬이 실행마다 다름 | 같은 커밋인데 리포트가 바뀜 | set 순회 순서, 동률 정렬 | 정렬 키에 이름 추가 — 출력은 결정적이어야 |
| 재귀·함수 포인터를 0으로 치는 스택 도구 | 정적 최악 < 실측 high-water | 그래프 구멍 | 도구가 "계산 불가"로 멈추게, 상한·대상을 설정으로 |
| ISR 예외 프레임을 빼고 스택 계산 | 인터럽트 많은 상황에서만 overflow | M4F는 진입마다 최대 104(+4) B | 우선순위 그룹별 ISR 최악 + 프레임 합산 |
| 텔레메트리로 "총 free 힙"을 올림 | 여유가 있는데 할당 실패 보고 | 외부 단편화 | 가장 큰 free 블록·실패 카운터·min-ever-free |
| 계측 allocator와 `leaks`를 동시에 | 누수 0개로 나옴 | 계측 리스트가 샌 블록을 붙잡음 | 도구는 하나씩, 또는 계측 쪽 리포트 사용 |
| 작은 입력으로 잰 RSS를 SoC 예산으로 | 현장에서 OOM·lmkd kill | arena·pattern이 가장 큰 입력 기준으로 커짐 | 최대 입력으로 돌린 뒤 footprint/PSS로 예산 |
| RSS·VSZ를 "모델 메모리"로 보고 | 작은 모델이 수백 MB라는 결론 (I5) | 라이브러리 코드 페이지·가상 예약 포함 | dirty/footprint·PSS, 세션 전후 Δ로 분리 |
| `uxTaskGetStackHighWaterMark` 값을 바이트·사용량으로 읽음 | 스택을 4배 잘못 잡음 | word 단위 "최소 남은 양" | 단위·의미를 문서로 확인하고 변환 |
| 메모리 게이트에 "승인하면 다 통과" | 예비가 PR마다 조금씩 사라짐 | 예산 소유권 부재 | 증가 한도만 승인 가능, 한도·예비는 별도 결정 |

---

## 11. 면접에서 이렇게 말한다

**Q.** How do you find what's eating flash in a firmware image?

**A.** `size`로 영역 합계를 보고, 링커 맵을 스크립트로 집계해 오브젝트·라이브러리·심볼 top-N을 낸다. 합계가 `size`와 바이트 단위로 맞는지(정렬 틈, `.data` 이중 계산) 먼저 검증한다. ML 펌웨어라면 대개 모델 blob이 압도적이라 양자화·모델 위치(XIP)·중복 사본이 레버고, 코드 쪽은 사용 안 하는 op 커널과 포매팅 코드가 흔한 범인이다. 그리고 이걸 한 번이 아니라 매 PR diff로 본다.

> "I start with `size` for the region totals, then parse the linker map with a small script that attributes every input section to its object, library and symbol, and I check that the totals match `size` to the byte — alignment gaps and the flash copy of `.data` are the usual discrepancies. In an ML firmware the model blob usually dominates, so the levers are quantization, where the model lives, and accidental duplicate copies; on the code side it's unused op kernels and formatting code pulled in by logging. Most importantly I don't do it once: the same script produces a per-symbol diff on every pull request."

**Q.** How do you determine worst-case stack usage?

**A.** 정적·동적 두 가지를 같이 쓴다. 정적: `-fstack-usage`로 함수별 프레임, 디스어셈블이나 `-fcallgraph-info`로 호출 그래프를 얻어 가장 무거운 경로를 계산하고, ISR은 우선순위 그룹별로 핸들러 최악 + 예외 프레임(M4F는 FPU 문맥 포함 104 B)을 더한다. 재귀와 함수 포인터는 도구가 멈추게 하고 상한·대상을 명시한다. 동적: painting high-water로 검증한다. 실측이 정적보다 크면 분석 구멍, 훨씬 작으면 테스트가 최악 경로를 안 돈 것이다. 크기는 둘 중 큰 값에 margin을 얹는다.

> "Two complementary methods. Statically, I take per-function frame sizes from `-fstack-usage`, build the call graph from the disassembly or the compiler's call-graph output, and compute the heaviest path from each entry point; on top of that I add, per interrupt priority level, the worst ISR path plus the hardware exception frame, which is 104 bytes on a Cortex-M4F with FPU context. Recursion and function pointers make the tool stop rather than guess, and we annotate bounds and targets explicitly. Dynamically, I paint the stacks and read the high-water marks under worst-case tests. If the measured value exceeds the static bound, the analysis has a hole; if it's far below, the tests didn't exercise the worst path. The stack size is the larger of the two plus a margin."

**Q.** Why avoid malloc in firmware, and how do you prove there's no fragmentation issue?

**A.** 단편화로 "총 여유는 있는데 연속 블록이 없어" 실패할 수 있고, 시간·실패 처리가 비결정적이고, ISR에서 못 쓰기 때문이다. 시뮬레이션에서 20 KB 힙의 총 free가 한 번도 8.2 KB 아래로 안 갔는데 8 KB 요청이 30 % 실패한 걸 본 적이 있다. 증명은 세 단계: 초기화 이후 할당 함수가 링크되지 않거나 호출되면 assert, 고정 블록 pool의 등급별 크기를 계측한 최대 동시 개수 + margin으로 근거화, 현장에서 pool hwm·실패 카운터를 텔레메트리로 감시.

> "Because a general-purpose heap can fail from external fragmentation even when there's plenty of total free memory, its timing isn't bounded, and it isn't ISR-safe. In a simulation I ran, a 20 KB heap never dropped below 8.2 KB of total free space, yet an 8 KB request failed thirty percent of the time. To prove we don't have the problem, first, after init there's no allocator at all — the linked image has no malloc symbols, or a wrap asserts if it's called. Second, anything dynamic goes through fixed-block pools whose per-class sizes come from measured peak concurrency plus margin, written down in the budget. Third, pool high-water marks and failure counters go into fleet telemetry so the assumption is checked in the field."

**Q.** The RSS of the inference process is three times the model size. Why?

**A.** RSS에는 모델 말고도 활성값 peak(입력 해상도에 비례), 커널 workspace(im2col 같은), 런타임 arena와 memory pattern이 본 가장 큰 입력 기준으로 잡아 둔 메모리, 해제했지만 allocator가 OS에 안 돌려준 페이지, 프레임워크·라이브러리 코드 페이지, 스레드 스택이 다 들어간다. 저는 footprint/PSS로 dirty와 공유 페이지를 나누고, 세션 생성 전후와 최대 입력 전후 Δ를 따로 잰다. 실제로 가중치 0.3 MB짜리 ORT 프로세스가 footprint 227 MB였는데 그중 202 MB가 448 입력의 활성값이 남긴 malloc 블록이었다.

> "Because RSS isn't model memory. It includes the activation peak, which scales with input resolution, kernel workspaces like im2col, the runtime's arena and memory-pattern buffers sized for the largest input seen so far, pages the allocator freed but never returned, the framework's code pages, and thread stacks. I separate dirty from shared pages with footprint or PSS, and measure deltas around session creation and around the largest input. In one experiment a model with 0.3 MB of weights had a 227 MB footprint, and 202 MB of that was large malloc blocks left over from a 448-by-448 input."

**Q.** How do you stop memory regressions?

**A.** 예산을 코드로 두고(JSON 하나, 소유자 지정) 매 PR에서 merge-base 빌드와 맵을 비교한다. 영역 한도, 증가 한도, 핵심 심볼(arena 등) 예산, 스택 최악, 힙 금지를 검사해 실패 이유와 심볼별 diff 표를 PR 코멘트로 단다. 증가는 승인으로 허용할 수 있지만 예비를 먹는 건 예산 소유자의 별도 결정으로 한다. 정적 크기는 결정적이라 1 B 단위로 걸 수 있다. 출시 후에는 high-water 텔레메트리를 버전별 분포로 본다.

> "Budgets live in the repo as one file with an owner, and every pull request is compared against its merge-base build. The gate checks region limits, growth limits, budgets for key symbols such as the tensor arena, worst-case stack, and that no heap is linked, and it posts the reasons plus a per-symbol diff table on the PR. Growth can be approved with a label, but eating into the reserve requires a separate budget change by the owner. Static sizes are deterministic, so thresholds can be exact. After release, the high-water telemetry is tracked per firmware version, looking at the tail of the distribution."

---

## 12. 직접 해보기

1. **손계산**: 맵에 `.text 0x1a40`, `.rodata 0x3c8`, `.model 0x18000`, `.data 0x120 (VMA 0x20000000, LMA 0x0801c000)`, `.bss 0x9c00`, `.stack 0x1000`이 있다. flash와 RAM 사용량은? — 정답: flash = 6,720 + 968 + 98,304 + 288 = 106,280 B, RAM = 288 + 39,936 + 4,096 = 44,320 B (`.data`는 양쪽에).
2. **손계산**: 프레임이 `isr_a 48`, `isr_b 120`, `task_main 64 → infer 200 → kernel 512`, `task_main → log 300`이고 Cortex-M4F(FPU 문맥 104 B), `isr_a`와 `isr_b`는 우선순위가 달라 중첩 가능, RTOS 없음(전부 MSP)이다. 최악 MSP 사용량은? — 정답: 메인 64 + 200 + 512 = 776, ISR (48 + 104) + (120 + 104) = 376, 합 1,152 B (정렬 패딩 미포함).
3. **코드**: `sizediff.py`는 심볼 이름만 키로 쓴다. 두 파일에 같은 이름의 `static` 버퍼가 있으면 합쳐진다. 키에 오브젝트 이름을 넣고, rename을 "REMOVED + NEW 쌍이고 크기가 같다"로 추정해 표시하도록 고쳐라. — 힌트: `by_symbol`의 키를 `(region, out, obj, sym)`으로, 쌍 찾기는 크기·섹션이 같은 REMOVED/NEW를 매칭.
4. **코드**: `stackcheck.py`의 재귀 처리는 자기 자신을 부르는 경우만 다룬다. `a → b → a` 같은 상호 재귀에서 사람이 준 "사이클 깊이 상한"을 적용하도록 고쳐라. — 힌트: 강연결요소(SCC)를 찾아 SCC 하나를 "프레임 합 × 상한"인 노드로 접는다.
5. **실험**: 예제 7의 `FirstFit`을 best-fit(맞는 구멍 중 가장 작은 것)으로 바꿔 8 KB 실패 횟수를 비교하라. 실패가 줄어도 "증명"이 되지 않는 이유를 한 문장으로. — 힌트: 전략은 확률을 바꿀 뿐, 최악의 할당 순서에 대한 상한을 주지 않는다.
6. **측정**: 예제 10의 RSS 대신 `footprint`의 phys_footprint(또는 `vmmap --summary`의 Physical footprint)를 각 단계에서 읽도록 바꾸고, RSS와의 차이가 무엇 때문인지 설명하라. — 힌트: 공유 라이브러리의 clean 페이지는 RSS에만 있다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| map file (링커 맵) | 링커가 쓰는 배치 보고서 | 출력 섹션·입력 섹션(오브젝트)·심볼의 주소와 크기 |
| VMA / LMA | 실행 주소 / 적재 주소 | `.data`는 LMA(flash)에 굽고 VMA(RAM)로 복사 |
| alignment gap | 정렬 때문에 생긴 빈 바이트 | 맵에 줄로 안 나와 합계 불일치의 원인 |
| `.su` 파일 | `-fstack-usage` 출력 | 함수별 스택 프레임 바이트와 static/dynamic |
| tail call | 반환 대신 다른 함수로 점프 | 호출자 프레임이 먼저 사라져 합산하지 않음 |
| exception frame | 예외 진입 시 하드웨어가 쌓는 레지스터 | Cortex-M 32 B, FPU 문맥 시 104 B |
| MSP / PSP | main / process stack pointer | RTOS에서 ISR은 MSP, task는 PSP |
| high-water mark | 지금까지의 최대 사용(또는 최소 여유) | painting·카운터로 측정 |
| stack painting | 스택을 패턴으로 칠하고 지워진 깊이를 셈 | D2 6.3, FreeRTOS 방식 |
| instrumented allocator | header를 붙여 할당을 기록하는 wrapper | 현재·peak·분포·누수 위치 |
| external fragmentation | 빈 공간이 흩어져 큰 할당 실패 | 총 free ≠ 가장 큰 free 블록 |
| internal fragmentation | 요청보다 큰 블록을 줘서 생기는 낭비 | 크기 등급 allocator·pool의 대가 |
| fixed-block pool | 같은 크기 블록 N개의 allocator | O(1), 외부 단편화 없음 |
| RSS / PSS | 상주 페이지 / 공유를 나눈 상주 페이지 | RSS는 공유 라이브러리를 중복 계산 |
| footprint (dirty) | 프로세스가 더럽힌 페이지 | macOS 메모리 압박 판단 기준 |
| memory arena (런타임) | 할당을 모아 재사용하는 큰 영역 | ORT `enable_cpu_mem_arena`, TFLM arena |

---

## 14. 요약 & 체크리스트

메모리 프로파일링은 계산(D2)을 **측정과 감시**로 바꾸는 일이다. 정적 메모리는 링커가 이미 계산해 두었으니 맵 파일을 도구로 집계해 심볼·오브젝트 단위로 답하고, `size`와 바이트 단위로 맞춰 신뢰를 얻은 뒤, 두 빌드의 diff로 "왜 커졌나"를 PR마다 본다. 스택은 `-fstack-usage` 프레임과 디스어셈블 호출 그래프로 최악 경로를 계산하고, 재귀·함수 포인터에서는 도구가 멈추게 하며, ISR 예외 프레임(M4F 104 B)을 우선순위 단계마다 더한다. 실측 high-water는 정적 분석을 검증한다. 힙은 단편화 때문에 "총 여유가 있는데 실패"할 수 있어(시뮬레이션에서 30 %), 초기화 후 힙 금지 + 계측 근거로 크기를 정한 pool + 현장 hwm·실패 카운터로 대체한다. 호스트·SoC의 ML 프로세스에서 RSS는 모델이 아니라 활성값 peak·workspace·arena 캐싱·allocator 보유·라이브러리 코드가 만든다(가중치 0.3 MB, footprint 227 MB). 마지막으로 이 모든 숫자를 예산 JSON과 게이트로 CI에 걸고, 출시 후에는 텔레메트리 분포의 꼬리를 본다.

- [ ] ld.lld/GNU ld 맵 파일의 출력 섹션·입력 섹션·심볼 줄을 구분해 읽고, `.data`의 VMA/LMA를 설명할 수 있다
- [ ] 맵 집계 합계를 `size`의 `text + data`, `data + bss`와 바이트 단위로 맞출 수 있다 (정렬 틈 포함)
- [ ] 두 빌드의 심볼별 size diff를 만들고, Δ의 원인을 분류할 수 있다
- [ ] `.su` 프레임과 호출 그래프로 최악 스택 경로를 손으로 계산할 수 있다
- [ ] ISR 예외 프레임(32/104 B)과 MSP/PSP 구분을 포함해 스택 크기를 정할 수 있다
- [ ] 재귀·함수 포인터·VLA가 정적 스택 분석을 깨는 이유를 설명할 수 있다
- [ ] 계측 allocator로 peak·크기 분포·누수 위치를 뽑고, 그 결과로 pool 등급을 정할 수 있다
- [ ] 외부 단편화를 "가장 큰 free 블록" 지표로 설명하고, pool이 증명이 되는 이유를 말할 수 있다
- [ ] VSZ·RSS·PSS·footprint의 차이를 알고, ML 프로세스의 RSS를 항목별로 분해할 수 있다
- [ ] 예산 JSON + size diff로 메모리 게이트를 만들고, 승인 가능한 것과 불가능한 것을 나눌 수 있다

---

## 참고 자료

- GNU ld 매뉴얼 — 링커 스크립트, `MEMORY`, `AT>`, `ASSERT`, `--wrap`, `-Map`: https://sourceware.org/binutils/docs/ld/
- LLVM lld 문서 (ELF 링커, GNU ld 호환 옵션): https://lld.llvm.org/
- GCC 매뉴얼 Developer Options — `-fstack-usage`, `-fcallgraph-info`: https://gcc.gnu.org/onlinedocs/gcc/Developer-Options.html (clang의 `-fstack-usage`, `-Wframe-larger-than`은 Clang command line reference)
- Arm, Cortex-M4 Devices Generic User Guide (DUI 0553) — 예외 진입 시 stacking, FP 확장 프레임, lazy stacking
- Google Bloaty (바이너리 크기 분석): https://github.com/google/bloaty
- puncover (펌웨어 코드·스택 분석기): https://github.com/HBehrens/puncover
- cargo-call-stack (Rust 정적 스택 분석, nightly): https://github.com/japaric/cargo-call-stack
- P. R. Wilson et al., "Dynamic Storage Allocation: A Survey and Critical Review" (1995) — 단편화의 고전 정리
- MISRA C:2012 — Rule 21.3 (동적 메모리 할당 함수 금지)
- FreeRTOS 문서 — `uxTaskGetStackHighWaterMark`, 힙 구현(heap_1~heap_5), `xPortGetMinimumEverFreeHeapSize`, `vPortGetHeapStats`
- Zephyr 문서 — Thread analyzer, `sys_heap` runtime stats
- PyTorch Profiler: https://pytorch.org/docs/stable/profiler.html
- ONNX Runtime 문서 (Session options, 성능 튜닝): https://onnxruntime.ai/docs/
- macOS 매뉴얼 페이지 — `man vmmap`, `man footprint`, `man leaks`, `man heap`
- 이 노트와 연결: D2(추정기·painting), F2(arena), F7(섹션·링커 스크립트), J1(정적 메모리 지도), I1(예산·margin), I5(RSS 측정의 함정), I6(예산의 단일 출처), J6(CI 파이프라인), H7(fleet 모니터링)
