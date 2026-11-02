# F2. 메모리 풀 · 슬랩 — 크기를 고정하면 할당이 O(1)이 된다

> **이 노트를 다 읽으면**: free list를 빈 블록 안에 꿰는 고정 크기 풀을 FreeRTOS에 세 가지 방식(임계 구역 · 세마포어 · 포인터 큐)으로 직접 만들고 비용을 숫자로 비교할 수 있다 · Zephyr `k_mem_slab` / `k_heap`(`sys_heap`) / `sys_mem_blocks`와 ThreadX block pool / byte pool이 소스 안에서 어떻게 동작하는지(대기자 처리, 병합 시점, 헤더 크기) 말할 수 있다 · 같은 랜덤 작업량에서 9개 할당기의 지연 분포 · 점유율별 최악값 · 객체당 바이트를 직접 잰 결과로 "어디에 무엇을 쓰나"를 설명할 수 있다 · 크기 등급과 블록 수를 확률로 정할 수 있다
> **왜 필요한가**: "RTOS에서 malloc 써도 되나요?"(F1) 다음 꼬리 질문이 거의 항상 "그럼 무엇을 쓰나요?"다. 답은 "고정 크기 풀"이고, 면접관은 그 풀이 왜 O(1)인지, 고갈되면 어떻게 하는지, ISR에서 써도 되는지를 이어서 묻는다. 드론 FC의 텔레메트리 버퍼, 모터 컨트롤러의 CAN 프레임, SSD의 명령 슬롯이 전부 이 구조다
> **Don 기준 난이도**: SSD 펌웨어의 버퍼 풀 · 명령 슬롯 free list는 이미 매일 쓰던 것이다 / 새로 배울 부분은 RTOS 커널이 풀에 얹어 주는 것(블로킹 alloc + timeout, 대기자에게 직접 넘기기, 통계), Zephyr `sys_heap`의 bucket 구조와 "상한 있는 힙", ThreadX byte pool의 "검색하면서 병합", 그리고 할당기별 실측 차이
> **선행 노트**: F1(정적 vs 동적 할당 — heap_1 ~ heap_5 내부), D4 §3(포인터 큐로 소유권 넘기기), D10 §3(`k_stack` free-list), A5 §1.4(풀 API 로제타 표), EL F9(Linux slab — SLUB)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 직관 — "같은 크기 상자만 쓰는 창고"

범용 힙(F1의 heap_4, newlib malloc)은 "아무 크기나 달라"는 요청을 받는다. 그래서 빈 공간 목록을 **찾아야** 하고(검색), 남는 조각을 **쪼개야** 하고(split), 돌려받을 때 이웃과 **합쳐야** 한다(merge). 찾는 시간은 빈 조각 수에 따라 늘고, 쪼개고 합치다 보면 "빈 바이트는 충분한데 연속된 자리가 없어서" 실패한다(외부 단편화, external fragmentation).

고정 크기 풀(fixed-size block pool)은 질문을 바꾼다. **"모든 상자를 같은 크기로 만들면?"**

- 어느 빈 상자를 줘도 똑같으니 **찾을 필요가 없다**. 목록 맨 앞의 것을 준다 → O(1).
- 쪼갤 일도 합칠 일도 없다 → 외부 단편화 0.
- 빈 상자 목록은 따로 메모리를 쓰지 않는다. **빈 상자 안의 첫 word에 "다음 빈 상자 주소"를 적어 두면** 그 자체가 연결 리스트다(intrusive free list). 상자가 사용 중일 때는 그 word를 사용자가 덮어써도 된다 — 어차피 목록에 없으니까.

대가는 하나다. 20 B가 필요한데 상자가 24 B면 4 B를 버린다(내부 단편화, internal fragmentation). 그래서 크기가 몇 종류뿐인 객체(메시지, 프레임, 디스크립터, 명령 컨텍스트)에 쓴다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">고정 크기 풀 — free list는 빈 블록 자신의 첫 word에 산다</text>
<rect x="40" y="80" width="90" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="85" y="104" font-size="13" text-anchor="middle">#0</text>
<text x="85" y="124" font-size="12" text-anchor="middle">사용 중</text>
<rect x="140" y="80" width="90" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<rect x="140" y="80" width="40" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="160" y="95" font-size="12" text-anchor="middle">next</text>
<text x="185" y="124" font-size="12" text-anchor="middle">#1 free</text>
<rect x="240" y="80" width="90" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="285" y="104" font-size="13" text-anchor="middle">#2</text>
<text x="285" y="124" font-size="12" text-anchor="middle">사용 중</text>
<rect x="340" y="80" width="90" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<rect x="340" y="80" width="40" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="360" y="95" font-size="12" text-anchor="middle">next</text>
<text x="385" y="124" font-size="12" text-anchor="middle">#3 free</text>
<rect x="440" y="80" width="90" height="56" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<rect x="440" y="80" width="40" height="20" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="460" y="95" font-size="12" text-anchor="middle">next</text>
<text x="485" y="124" font-size="12" text-anchor="middle">#4 free</text>
<rect x="540" y="80" width="90" height="56" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="585" y="104" font-size="13" text-anchor="middle">#5</text>
<text x="585" y="124" font-size="12" text-anchor="middle">사용 중</text>
<path d="M350,80 Q265,36 172,78" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<path d="M165,80 Q310,22 452,78" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<path d="M470,80 Q520,46 566,52" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<text x="600" y="56" font-size="12" text-anchor="middle">NULL</text>
<rect x="305" y="170" width="160" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="385" y="192" font-size="13" text-anchor="middle">pool.free (head)</text>
<line x1="385" y1="169" x2="385" y2="139" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
<text x="40" y="236" font-size="13">alloc: b = head; head = b-&gt;next — 포인터 두 번 옮기면 끝 (O(1))</text>
<text x="40" y="260" font-size="13">free : b-&gt;next = head; head = b — 반납한 블록이 다음 alloc에 먼저 나간다 (LIFO)</text>
</svg>
```
그림 1 — 고정 크기 풀의 전부. 블록 #3 → #1 → #4 → NULL 순서의 free list가 빈 블록 안에 꿰어 있고, 사용 중 블록(#0, #2, #5)은 목록에 없다. 메타데이터는 head 포인터 하나다.

### 0.2 RTOS 펌웨어에서 어디에 쓰나

- **드론 FC 텔레메트리**: MAVLink 메시지 버퍼(최대 280 B 안팎)를 풀에서 꺼내 채우고, 포인터만 UART TX 태스크로 넘긴다(D4 §3의 소유권 넘기기). 텔레메트리 태스크가 늦어도 비행 제어 루프는 힙을 건드리지 않는다.
- **모터 컨트롤러 CAN**: CAN 프레임(16 B 남짓) 풀. RX ISR이 풀에서 꺼내 채우고 큐에 넣는다. ISR에서 할당하려면 할당이 짧고 상한이 있어야 한다 — 풀이 정확히 그 조건이다.
- **센서 허브**: 1 kHz IMU 샘플 묶음을 64 B 블록에 담아 SPI 슬레이브 DMA로 내보내는 버퍼.
- **Don의 SSD 컨트롤러**: 호스트 데이터 버퍼(4 KiB 단위), NVMe 명령 슬롯(명령 컨텍스트), DMA 디스크립터. SSD 펌웨어는 데이터 경로에서 malloc을 절대 부르지 않고 부팅 때 만든 free list에서 꺼내 쓴다. **이 노트의 고정 크기 풀은 그것과 같은 물건이다.** RTOS가 더해 주는 것은 "비었으면 재워 두었다가 누가 반납하면 깨워 주기"와 통계뿐이다.

### 0.3 Linux slab과 무엇이 다른가 (EL F9 재사용)

Linux SLUB(EL F9 §6)도 "크기 X짜리 객체 캐시 + 빈 객체 안의 freelist 포인터"라는 같은 아이디어다. MCU RTOS에서 달라지는 점만 적는다.

| 항목 | Linux SLUB (EL F9) | MCU RTOS 풀 (이 노트) |
|---|---|---|
| 뒤에 있는 메모리 | buddy에서 페이지를 받아 slab을 늘리고 줄인다 | 빌드 시점에 크기가 정해진 정적 배열. 늘지 않는다 |
| CPU별 캐시 | per-CPU slab으로 락 없이 할당 | 없음. 단일 코어라 임계 구역(수십 cycle)이면 충분 |
| 고갈 시 | 회수(reclaim), compaction, OOM | 실패를 돌려주거나 timeout까지 **스레드를 재운다** |
| 크기 등급 | `kmalloc-8` ~ `-8k` 자동 | 앱이 직접 정한다(§3) |
| 디버그 | `slab_debug=FZPU` 레드존 · poison | 커널마다 다르다: Zephyr 포인터 검증 · 힙 hardening, 직접 만든 풀은 비트맵(§1) |

### 실습 환경 한 줄

QEMU `mps2-an385`(Cortex-M3 @ 25 MHz) 위에서 FreeRTOS V11.3.2(템플릿, `-O2`), Zephyr v4.4.2(`mps2/an385`, `CONFIG_QEMU_ICOUNT_SHIFT=5`), Eclipse ThreadX 6.5.2(A5 노트가 포팅한 Cortex-M3 GNU 포트, `-O2`)를 **같은 가상 보드**에서 돌린다. 시간은 전부 `ICOUNT=1`(명령 1개 = 32 ns 가상 시간)로 잰 값이고, 사이클은 MPS2 FPGAIO 카운터(0x40028018, 25 MHz, 1 count = 40 ns)로 읽는다. **QEMU icount 기준이라 실제 칩과 절대값은 다르다**(캐시 · 플래시 wait state · 버스 미모델링). 할당기끼리의 상대 비교와 "점유율에 따라 늘어나느냐"는 모양이 이 노트의 관심사다. Python 계산과 차트는 VM 호스트에서 돌렸다.

빌드 스크립트 세 개(FreeRTOS · Zephyr · ThreadX)는 랩 템플릿을 그대로 부른다. ThreadX는 A5가 받아 둔 클론(`/var/tmp/rtos/A5/threadx`, 커밋 `56c8695`)을 읽기 전용으로 쓰고, 오브젝트는 `/var/tmp/rtos/F2/`에 따로 컴파일했다.

```sh
#!/bin/bash
# fr.sh <name> <src.c> [make args...] — FreeRTOS build into /var/tmp/rtos/F2/<name>, run with ICOUNT=1
N=$1; S=$2; shift 2
O=/var/tmp/rtos/F2/$N; mkdir -p $O
make -B -s -f /var/rtoslab/templates/freertos-mps2/Makefile SRCS="$S" OUT=$O \
  EXTRA_CFLAGS="-I/Users/donh/.rtos_scratch/F2/common $XC" "$@" >$O.build 2>&1 || { cat $O.build; exit 9; }
grep -iE "warn|error" $O.build
[ -n "$NORUN" ] && exit 0
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $O/app.elf ${TO:-30}; echo "[rc=$?]"
```

```sh
#!/bin/bash
# zb.sh <ex> <build-name> [west args...] — Zephyr build into /var/tmp/rtos/F2/<build-name>, run with icount shift 5
E=$1; N=$2; shift 2
D=/var/tmp/rtos/F2/$N
/var/rtoslab/bin/rtos-zbuild mps2/an385 /Users/donh/.rtos_scratch/F2/$E $D "$@" || exit 9
[ -n "$NORUN" ] && exit 0
ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 $D/zephyr/zephyr.elf ${TO:-20}; echo "[rc=$?]"
```

`tx.sh`는 A5 §2.4의 `build.sh`와 같은 일(ThreadX 공통 소스 + 포트 어셈블리 7개 + 랩 `startup.c`/`board.c` + A5의 `tx_glue.c`)을 하되, 커널 컴파일 플래그(예: `-DTX_BYTE_POOL_ENABLE_PERFORMANCE_INFO`)별로 오브젝트 폴더를 나눈다. 앱 코드는 `-Wall -Wextra` 경고 0개, ThreadX 커널 소스는 A5처럼 `-w`로 컴파일했다.

---

## 1. 고정 크기 블록 풀의 원리

### 1.1 정의

**고정 크기 블록 풀**: 같은 크기(`bsz`) 블록 `n`개를 담은 연속 메모리 + 빈 블록들의 단일 연결 리스트 head. 초기화 때 모든 블록을 리스트에 꿴다. alloc은 head를 떼고, free는 head에 붙인다.

필요한 성질 네 가지.

1. **블록 크기 ≥ 포인터 크기**: 빈 블록 안에 next 포인터를 써야 하니까(Cortex-M에서 4 B).
2. **정렬**: 블록 크기가 정렬 단위의 배수여야 모든 블록이 정렬된다. 시작 주소가 8 B 정렬이고 블록이 24 B면 모든 블록이 8 B 정렬이다. `double`·`uint64_t`나 LDRD/STRD를 쓰는 구조체는 8 B, DMA 엔진은 흔히 4 ~ 32 B, 캐시 유지(M7)가 필요한 DMA 버퍼는 **캐시 라인(32 B) 정렬 + 32 B의 배수 크기**가 필요하다(B8). Zephyr `K_MEM_SLAB_DEFINE`이 "block_size는 align의 배수"를 `BUILD_ASSERT`로 강제하는 이유다(§4).
3. **임계 구역은 포인터 두 번 옮기는 동안만**: 검색이 없으니 잠그는 시간이 상수다. 그래서 ISR에서도 쓸 수 있다.
4. **반납 검사는 공짜가 아니다**: 순수한 free list는 "이 포인터가 정말 이 풀의 블록인가", "이미 반납된 것 아닌가"를 모른다. 확인하려면 범위 · 정렬 검사(싸다)와 사용 중 비트맵(블록당 1비트)이 필요하다.

### 1.2 외부 단편화 vs 내부 단편화

| 구분 | 뜻 | 범용 힙 | 고정 크기 풀 |
|---|---|---|---|
| 외부 단편화 | 빈 바이트 합은 충분한데 연속 공간이 없어 실패 | 생긴다(§7에서 78 ~ 80% 점유에서 첫 실패) | **0** — 빈 블록 하나면 언제나 성공 |
| 내부 단편화 | 받은 블록이 요청보다 커서 버리는 바이트 | 헤더 4 ~ 8 B + 8 B 반올림 | 블록 크기 − 요청 크기 (크기 등급 설계로 줄인다, §3) |
| 할당 시간 | | 빈 조각 수에 따라 변한다 | 상수 |
| 고갈 판단 | "될지 안 될지 해 봐야 안다" | | `used == n`이면 실패, 미리 알 수 있다 |

마지막 행이 실무에서 크다. 풀은 **용량 계획이 개수로 끝난다.** "IMU 묶음 버퍼는 최대 8개가 동시에 살아 있다"를 증명하면 실패는 없다. 힙은 같은 증명을 하려면 할당 순서와 크기 패턴까지 따져야 한다.

### 1.3 예제 1 — 빈 블록 안을 들여다보기, LIFO, 그리고 double free

무엇을 확인하는 코드인지: 공용 헤더 `pool.h`(아래)로 24 B × 6 블록 풀을 만들고, 빈 블록의 첫 word가 정말 다음 블록 주소인지, 반납 순서대로 다시 나가는지(LIFO), 같은 블록을 두 번 반납하면 무슨 일이 생기는지를 `POOL_DEBUG` 있을 때와 없을 때 비교한다.

먼저 공용 헤더다(§2, §3, §7에서도 쓴다). 74줄이라 예제 길이 기준을 조금 넘지만 라이브러리라 한 번에 보인다.

```c
/* F2 pool.h — fixed-size block pool for FreeRTOS: free list threaded through the free blocks.
 * O(1) alloc/free, critical section only around two pointer moves. POOL_DEBUG adds a used-bitmap
 * (1 bit per block) that catches double free and foreign pointers. */
#pragma once
#include "FreeRTOS.h"
#include "task.h"
#include <stdint.h>
#ifndef POOL_MAXBLK
#define POOL_MAXBLK 256
#endif
typedef struct {
	void     *free;              /* head of the free list (NULL = empty) */
	uint8_t  *base;
	uint32_t  bsz, n, used, peak;
#ifdef POOL_DEBUG
	uint32_t  inuse[(POOL_MAXBLK + 31) / 32];
#endif
} pool_t;

static inline void pool_init(pool_t *p, void *mem, uint32_t bsz, uint32_t n)
{
	configASSERT(bsz >= sizeof(void *) && (bsz % sizeof(void *)) == 0);
	configASSERT(((uintptr_t)mem % sizeof(void *)) == 0 && n <= POOL_MAXBLK);
	p->base = mem; p->bsz = bsz; p->n = n; p->used = p->peak = 0; p->free = NULL;
	for (int32_t i = (int32_t)n - 1; i >= 0; i--) {   /* block 0 ends up at the head */
		void **b = (void **)(p->base + (uint32_t)i * bsz);
		*b = p->free; p->free = b;
	}
#ifdef POOL_DEBUG
	for (uint32_t i = 0; i < (POOL_MAXBLK + 31) / 32; i++) p->inuse[i] = 0;
#endif
}
static inline void *pool_take(pool_t *p)               /* caller holds the lock */
{
	void **b = p->free;
	if (b) {
		p->free = *b;
		if (++p->used > p->peak) p->peak = p->used;
#ifdef POOL_DEBUG
		uint32_t i = (uint32_t)((uint8_t *)b - p->base) / p->bsz;
		p->inuse[i / 32] |= 1u << (i % 32);
#endif
	}
	return b;
}
static inline int pool_give(pool_t *p, void *v)        /* 0 ok, -1 rejected */
{
#ifdef POOL_DEBUG
	uint32_t off = (uint32_t)((uint8_t *)v - p->base), i = off / p->bsz;
	if ((uint8_t *)v < p->base || i >= p->n || off % p->bsz) return -1;      /* foreign */
	if (!(p->inuse[i / 32] & (1u << (i % 32)))) return -1;                 /* double free */
	p->inuse[i / 32] &= ~(1u << (i % 32));
#endif
	*(void **)v = p->free; p->free = v; p->used--;
	return 0;
}
static inline void *pool_alloc(pool_t *p)
{
	taskENTER_CRITICAL(); void *b = pool_take(p); taskEXIT_CRITICAL(); return b;
}
static inline int pool_free(pool_t *p, void *v)
{
	taskENTER_CRITICAL(); int r = pool_give(p, v); taskEXIT_CRITICAL(); return r;
}
static inline void *pool_alloc_isr(pool_t *p)
{
	UBaseType_t s = taskENTER_CRITICAL_FROM_ISR(); void *b = pool_take(p);
	taskEXIT_CRITICAL_FROM_ISR(s); return b;
}
static inline int pool_free_isr(pool_t *p, void *v)
{
	UBaseType_t s = taskENTER_CRITICAL_FROM_ISR(); int r = pool_give(p, v);
	taskEXIT_CRITICAL_FROM_ISR(s); return r;
}
```

```c
/* F2 ex1 — a free list threaded through the free blocks: what is inside a free block,
 * LIFO reuse, internal fragmentation, and what a double free does (with/without POOL_DEBUG). */
#include "pool.h"
#include "board.h"

#define BSZ 24
#define NB  6
static uint32_t mem[NB * BSZ / 4];             /* word aligned backing store */
static pool_t pl;

static void dump(const char *tag)
{
	board_printf_raw("%s used=%u free list:", tag, (unsigned)pl.used);
	int hops = 0;
	for (void **b = pl.free; b && hops < 8; b = *b, hops++)
		board_printf_raw(" #%u", (unsigned)(((uint8_t *)b - pl.base) / BSZ));
	board_printf_raw(hops == 8 ? " ... (cycle!)\n" : " -> NULL\n");
}

static void demo(void *pv)
{
	(void)pv;
	pool_init(&pl, mem, BSZ, NB);
	board_printf_raw("base=0x%x bsz=%u n=%u\n", (unsigned)(uintptr_t)pl.base, BSZ, NB);
	board_printf_raw("word0 of each free block (= next pointer):");
	for (int i = 0; i < NB; i++) board_printf_raw(" 0x%x", (unsigned)mem[i * BSZ / 4]);
	board_printf_raw("\n");
	dump("init  ");
	void *a = pool_alloc(&pl), *b = pool_alloc(&pl), *c = pool_alloc(&pl);
	board_printf_raw("alloc a=#%u b=#%u c=#%u\n", (unsigned)(((uint8_t *)a - pl.base) / BSZ),
	                 (unsigned)(((uint8_t *)b - pl.base) / BSZ), (unsigned)(((uint8_t *)c - pl.base) / BSZ));
	pool_free(&pl, a); pool_free(&pl, c);
	dump("free a,c");
	void *d = pool_alloc(&pl);
	board_printf_raw("next alloc d=#%u (LIFO: last freed comes back first)\n",
	                 (unsigned)(((uint8_t *)d - pl.base) / BSZ));
	uint32_t req = 20;
	board_printf_raw("request %u B -> block %u B: internal waste %u B (%u%%)\n",
	                 (unsigned)req, BSZ, (unsigned)(BSZ - req), (unsigned)((BSZ - req) * 100 / BSZ));

	int r1 = pool_free(&pl, d), r2 = pool_free(&pl, d);   /* double free */
	board_printf_raw("free d -> %d, free d again -> %d\n", r1, r2);
	dump("after 2x");
	void *x = pool_alloc(&pl), *y = pool_alloc(&pl);
	board_printf_raw("two allocs: x=#%u y=#%u %s\n", (unsigned)(((uint8_t *)x - pl.base) / BSZ),
	                 (unsigned)(((uint8_t *)y - pl.base) / BSZ), x == y ? "<- SAME BLOCK TWICE" : "(distinct)");
	int r3 = pool_free(&pl, (uint8_t *)b + 4);             /* pointer into the middle of a block */
	board_printf_raw("free(b+4) -> %d\n", r3);
	board_exit(0);
}

int main(void)
{
	static StaticTask_t tcb; static StackType_t stk[512];
	xTaskCreateStatic(demo, "demo", 512, NULL, 2, stk, &tcb);
	vTaskStartScheduler();
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; $S/fr.sh ex1 $S/ex1/main.c; echo ======; XC=-DPOOL_DEBUG $S/fr.sh ex1d $S/ex1/main.c'
```

```text
base=0x200008e0 bsz=24 n=6
word0 of each free block (= next pointer): 0x200008f8 0x20000910 0x20000928 0x20000940 0x20000958 0x0
init   used=0 free list: #0 #1 #2 #3 #4 #5 -> NULL
alloc a=#0 b=#1 c=#2
free a,c used=1 free list: #2 #0 #3 #4 #5 -> NULL
next alloc d=#2 (LIFO: last freed comes back first)
request 20 B -> block 24 B: internal waste 4 B (16%)
free d -> 0, free d again -> 0
after 2x used=0 free list: #2 #2 #2 #2 #2 #2 #2 #2 ... (cycle!)
two allocs: x=#2 y=#2 <- SAME BLOCK TWICE
free(b+4) -> 0
[board_exit] code=0
[rc=0]
======
base=0x20000900 bsz=24 n=6
word0 of each free block (= next pointer): 0x20000918 0x20000930 0x20000948 0x20000960 0x20000978 0x0
init   used=0 free list: #0 #1 #2 #3 #4 #5 -> NULL
alloc a=#0 b=#1 c=#2
free a,c used=1 free list: #2 #0 #3 #4 #5 -> NULL
next alloc d=#2 (LIFO: last freed comes back first)
request 20 B -> block 24 B: internal waste 4 B (16%)
free d -> 0, free d again -> -1
after 2x used=1 free list: #2 #0 #3 #4 #5 -> NULL
two allocs: x=#2 y=#0 (distinct)
free(b+4) -> -1
[board_exit] code=0
[rc=0]
```

출력에서 볼 것:

- 초기화 직후 각 빈 블록의 첫 word가 바로 다음 블록 주소(`0x...8f8`, `0x...910` … 24 B 간격)이고 마지막은 `0x0`이다. 메타데이터를 따로 두지 않는다는 말이 이것이다.
- `a`, `c`를 반납하면 리스트가 `#2 #0 #3 …` — 마지막에 반납한 `c`(#2)가 맨 앞이다. 다음 alloc이 #2를 준다(LIFO). 방금 쓴 블록이 캐시에 남아 있을 가능성이 크다는 장점이 있지만, **use-after-free 버그를 숨기는 단점**도 있다: 반납한 포인터를 실수로 계속 쓰면 그 블록이 곧바로 다른 사람에게 나가 두 주인이 같은 메모리를 쓴다.
- 디버그 없는 빌드에서 같은 블록을 두 번 반납하면 `#2 → #2 → #2 …` 자기 자신을 가리키는 고리가 된다. 다음 두 번의 alloc이 **같은 블록을 두 번** 준다. 증상은 한참 뒤 엉뚱한 곳에서 데이터가 깨지는 것이다. `used` 카운터도 0으로 틀어졌다(실제로는 `b`가 아직 사용 중).
- `POOL_DEBUG`(블록당 1비트 사용 중 비트맵 + 범위 · 정렬 검사)를 켜면 두 번째 반납과 블록 중간 포인터(`b+4`) 반납이 `-1`로 거부되고 리스트가 멀쩡하다. 비용은 6블록에 4 B 비트맵과 free 경로의 나눗셈 한 번이다. 개발 빌드에는 켜 두고, 양산 빌드에서도 범위 검사 정도는 남겨 두는 팀이 많다.

---

## 2. FreeRTOS에는 풀 API가 없다 — 세 가지로 직접 만든다

FreeRTOS 커널 V11.3.2에는 고정 크기 풀 API가 없다(A5 §1.4 로제타 표: "없음 — heap_1 ~ heap_5 또는 직접 구현"). 커널 소스에서 확인하면 `portable/MemMang/`의 heap_1 ~ heap_5(F1)뿐이다. 그래서 프로젝트마다 직접 만든다. 흔한 구현은 셋이다.

| 구현 | 구조 | 비었을 때 | ISR | 비용(예제 2 실측) |
|---|---|---|---|---|
| (a) free list + 임계 구역 | `pool.h` 그대로 | 즉시 NULL | `pool_alloc_isr()` (`taskENTER_CRITICAL_FROM_ISR`) | 68 cycles / pair |
| (b) free list + counting semaphore | 세마포어 count = 남은 블록 수, take 성공하면 반드시 블록이 있다 | `xSemaphoreTake(s, timeout)`으로 **블로킹** | `xSemaphoreTakeFromISR` + `pool_alloc_isr` | 195 cycles / pair |
| (c) 포인터 큐 | 길이 N 큐에 블록 포인터 N개를 넣어 둔다. alloc = receive, free = send | `xQueueReceive(q, &p, timeout)`으로 **블로킹** | `xQueueReceiveFromISR` | 183 cycles / pair |

(c)는 D4 §3의 "free 큐"와 같다 — 거기서는 소유권 넘기기의 반쪽으로 썼고, 여기서는 그 free 큐 자체를 **풀**로 본다. 장점은 블로킹 alloc과 timeout이 공짜로 따라온다는 것, 대기 태스크가 우선순위 순으로 깨어난다는 것(큐의 `xTasksWaitingToReceive`는 우선순위 정렬 리스트, D4 §10)이다. 단점은 블록당 큐 슬롯 4 B와 `StaticQueue_t` 84 B, 그리고 alloc · free마다 큐 커널 경로(임계 구역, 대기 리스트 확인)를 타는 비용이다.

### 2.1 예제 2 — 비용, timeout 만료, 반납에 깨어나기

무엇을 확인하는 코드인지: 세 구현의 alloc + free 1쌍 비용(1000회 평균), 그리고 (c) 포인터 큐 풀에서 블록을 다 가져간 뒤 50 ms timeout이 정확히 만료되는지, 낮은 우선순위 태스크가 30 ms 뒤 반납하면 기다리던 태스크가 그 블록을 받는지 본다.

```c
/* F2 ex2 — three FreeRTOS pools: (a) free list + critical section, (b) free list + counting
 * semaphore (blocking), (c) queue of pointers (blocking for free). Cost per alloc/free pair,
 * then exhaustion: timeout expiry and wake-up when another task returns a block. */
#include "pool.h"
#include "queue.h"
#include "semphr.h"
#include "board.h"
#define FPGA_CNT (*(volatile uint32_t *)0x40028018u)
#define NB 4
static uint32_t mem[NB][16];                         /* 4 blocks x 64 B */
static pool_t pl;  static SemaphoreHandle_t sem;  static QueueHandle_t q;

static void *a_crit(void) { return pool_alloc(&pl); }
static void  f_crit(void *b) { pool_free(&pl, b); }
static void *a_sem(void) { return xSemaphoreTake(sem, 0) ? pool_alloc(&pl) : NULL; }
static void  f_sem(void *b) { pool_free(&pl, b); xSemaphoreGive(sem); }
static void *a_q(void) { void *b; return xQueueReceive(q, &b, 0) ? b : NULL; }
static void  f_q(void *b) { xQueueSend(q, &b, 0); }

static void cost(const char *nm, void *(*a)(void), void (*f)(void *))
{
	uint32_t t0 = FPGA_CNT;
	for (int i = 0; i < 1000; i++) { void *b = a(); f(b); }
	board_printf_raw("%s: %u cycles per alloc+free pair\n", nm, (unsigned)((FPGA_CNT - t0) / 1000));
}
static void releaser(void *pv)                       /* lower priority: returns one block later */
{
	vTaskDelay(pdMS_TO_TICKS(30));
	board_printf_raw("[%u ms] releaser: give back block %x\n", (unsigned)xTaskGetTickCount(), (unsigned)(uintptr_t)pv);
	void *b = pv; xQueueSend(q, &b, 0);
	vTaskDelete(NULL);
}
static void demo(void *pv)
{
	(void)pv; void *hold[NB], *b;
	cost("(a) freelist+critical ", a_crit, f_crit);
	cost("(b) freelist+semaphore", a_sem, f_sem);
	cost("(c) queue of pointers ", a_q, f_q);
	for (int i = 0; i < NB; i++) hold[i] = a_q();    /* take everything */
	board_printf_raw("pool empty: waiting=%u\n", (unsigned)uxQueueMessagesWaiting(q));
	TickType_t t = xTaskGetTickCount();
	BaseType_t r = xQueueReceive(q, &b, pdMS_TO_TICKS(50));
	board_printf_raw("alloc(timeout 50 ms) -> %s after %u ms\n", r ? "OK" : "FAIL", (unsigned)(xTaskGetTickCount() - t));
	xTaskCreate(releaser, "rel", 256, hold[2], 1, NULL);
	t = xTaskGetTickCount();
	r = xQueueReceive(q, &b, pdMS_TO_TICKS(200));
	board_printf_raw("[%u ms] alloc(timeout 200 ms) -> %s block %x after %u ms\n", (unsigned)xTaskGetTickCount(),
	                 r ? "OK" : "FAIL", (unsigned)(uintptr_t)b, (unsigned)(xTaskGetTickCount() - t));
	board_printf_raw("sizeof: pool_t=%u, queue storage=%u + Queue_t(StaticQueue_t)=%u\n",
	                 (unsigned)sizeof(pool_t), (unsigned)(NB * sizeof(void *)), (unsigned)sizeof(StaticQueue_t));
	board_exit(0);
}
int main(void)
{
	static StaticQueue_t qs; static uint8_t qbuf[NB * sizeof(void *)]; static StaticSemaphore_t ss;
	pool_init(&pl, mem, sizeof mem[0], NB);
	sem = xSemaphoreCreateCountingStatic(NB, NB, &ss);
	q = xQueueCreateStatic(NB, sizeof(void *), qbuf, &qs);
	for (int i = 0; i < NB; i++) { void *b = mem[i]; xQueueSend(q, &b, 0); }
	static StaticTask_t tcb; static StackType_t stk[512];
	xTaskCreateStatic(demo, "demo", 512, NULL, 2, stk, &tcb);
	vTaskStartScheduler();
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; $S/fr.sh ex2 $S/ex2/main.c'
```

```text
(a) freelist+critical : 68 cycles per alloc+free pair
(b) freelist+semaphore: 195 cycles per alloc+free pair
(c) queue of pointers : 183 cycles per alloc+free pair
pool empty: waiting=0
alloc(timeout 50 ms) -> FAIL after 50 ms
[97 ms] releaser: give back block 20000a20
[97 ms] alloc(timeout 200 ms) -> OK block 20000a20 after 30 ms
sizeof: pool_t=24, queue storage=16 + Queue_t(StaticQueue_t)=84
[board_exit] code=0
[rc=0]
```

출력에서 볼 것:

- (a)가 68 cycles(2.7 µs @ 25 MHz)로 가장 싸다. (b)와 (c)는 약 2.8배다. 1000쌍 루프에는 함수 포인터 호출과 tick ISR 2 ~ 3번이 섞여 있어 절대값은 대략이다 — 정밀한 분포는 §7에서 IRQ를 막고 한 op씩 잰다.
- 빈 풀에서 50 ms timeout은 **정확히 50 ms**에 실패한다(FreeRTOS 1 kHz tick, `pdMS_TO_TICKS(50)` = 50 tick). Zephyr는 같은 요청이 60 ms가 된다(§4) — A5 §3.2에서 본 "Zephyr는 상대 timeout에 +1 tick, 100 Hz 보드" 차이가 풀에도 그대로 나온다.
- 낮은 우선순위(1) releaser가 30 ms 뒤 `0x20000a20` 블록을 send하는 순간, 우선순위 2로 기다리던 demo가 그 블록을 받고 바로 선점해 돈다(두 줄이 같은 97 ms). 풀 = 큐라서 "반납 = 대기자 깨우기"가 커널 안에서 처리된다.
- 메모리: (a)의 제어 구조는 24 B, (c)는 블록 4개에 큐 저장소 16 B + `StaticQueue_t` 84 B. 블록이 256개면 큐 저장소가 1 KiB다.

### 2.2 어느 것을 쓰나

- **ISR이 할당하고 태스크가 반납하는, 비면 버리면 되는 경로**(CAN RX, IMU 샘플): (a). 비었으면 그 샘플을 버리고 카운터를 올린다. 할당이 가장 짧다.
- **태스크가 "버퍼 하나 날 때까지 기다리겠다"가 자연스러운 경로**(로거가 플래시 쓰기 버퍼를 기다림): (b) 또는 (c). (c)가 코드가 짧고, (b)는 큐 저장소가 없는 대신 세마포어와 리스트 두 개를 일관되게 다뤄야 한다(세마포어를 먼저 take해야 리스트가 비어 있지 않음이 보장된다).
- **ISR ↔ 태스크 사이에 커널 없이**: D8의 lock-free SPSC 링에 블록 인덱스를 넣는 방식. SSD 펌웨어의 코어 간 free 인덱스 링이 이것이다.

### 2.3 ISR에서 할당할 때의 규칙

1. **블로킹 금지**: (a)는 원래 블로킹이 없고, (b) · (c)는 `FromISR` 버전(timeout 인자 없음)만 쓴다.
2. **임계 구역 API를 ISR용으로**: `taskENTER_CRITICAL_FROM_ISR()`은 BASEPRI를 올리고 이전 값을 돌려준다. 태스크용 `taskENTER_CRITICAL()`을 ISR에서 부르면 V11 ARM_CM3 포트는 `port.c` 438행의 `configASSERT`(VECTACTIVE 검사, A5 §3.3에서 본 그 assert)로 잡는다. `pool.h`가 `_isr` 함수를 따로 둔 이유다.
3. **인터럽트 우선순위**: `configMAX_SYSCALL_INTERRUPT_PRIORITY`(랩 0x20)보다 높은(숫자가 작은) ISR은 BASEPRI로 막히지 않는다. 그런 ISR에서 (a)를 쓰면 임계 구역이 그 ISR을 막지 못해 리스트가 깨진다. zero-latency ISR(E2)은 풀을 쓰지 말고 미리 받아 둔 블록만 쓴다.
4. **"FreeRTOS 힙은 ISR 금지"**: `pvPortMalloc()`은 `vTaskSuspendAll()`로 보호하므로 ISR에서 부를 수 없다(F1). 풀이 ISR 할당의 표준 답인 이유다.

---

## 3. 크기 등급(size class) — 내부 단편화를 설계로 줄인다

### 3.1 직관

모든 메시지를 가장 큰 크기(512 B) 블록에 담으면 구현은 쉽지만 작은 메시지가 대부분일 때 RAM의 대부분을 버린다. 그래서 **크기 등급**을 몇 개 둔다: 32 B 풀, 128 B 풀, 512 B 풀. 요청 크기를 덮는 가장 작은 등급에서 꺼낸다. Linux `kmalloc`이 `kmalloc-32`, `-64` … 로 하는 것과 같은 생각이다(EL F9 §6.3).

등급이 생기면 새 질문이 둘 생긴다.

- 작은 등급이 바닥나면? **위 등급으로 넘어갈지(fallback)** 실패할지 정한다. 넘어가면 실패는 줄지만 큰 블록이 작은 요청에 묶여 큰 요청이 나중에 실패할 수 있다.
- 각 등급에 블록을 몇 개 줄까? 등급마다 따로 고갈되므로 **여유분이 등급 수만큼 필요하다.** 등급을 늘리면 낭비는 줄지만 총 예약량은 늘 수 있다(§3.3).

### 3.2 예제 3 — 같은 8 KiB, 등급 1개 vs 3개, fallback 유무

무엇을 확인하는 코드인지: 드론 FC 메시지를 흉내 낸 크기 분포(70%가 8 ~ 32 B, 25%가 33 ~ 128 B, 5%가 129 ~ 512 B)로 슬롯 48개에 6000번 랜덤 alloc/free를 하고, 같은 8 KiB RAM을 (A) 512 B × 16, (B) 32 B × 24 + 128 B × 16 + 512 B × 10 + fallback, (C) B와 같은 등급 fallback 없음으로 나눴을 때 실패 수 · 낭비율 · 등급별 최대 사용량을 본다.

```c
/* F2 ex3 — size classes: same 8 KiB of RAM as (A) one 512 B class, (B) 32/128/512 classes with
 * fallback to the next class, (C) same classes, no fallback. Workload ~ drone FC messages:
 * 70% 8..32 B, 25% 33..128 B, 5% 129..512 B; 48 live slots, 6000 random ops. */
#include "pool.h"
#include "board.h"
typedef struct { uint32_t bsz, n; } cls_t;
static const cls_t CA[] = { {512, 16} }, CB[] = { {32, 24}, {128, 16}, {512, 10} };
static uint32_t ram[8192 / 4];
static pool_t P[3];
static uint32_t rs;
static uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static uint32_t req_size(void)
{
	uint32_t r = rnd() % 100;
	return r < 70 ? 8 + rnd() % 25 : r < 95 ? 33 + rnd() % 96 : 129 + rnd() % 384;
}
static void run(const char *nm, const cls_t *c, int nc, int fallback)
{
	uint8_t *m = (uint8_t *)ram;
	for (int i = 0; i < nc; i++) { pool_init(&P[i], m, c[i].bsz, c[i].n); m += c[i].bsz * c[i].n; }
	void *slot[48] = {0}; uint8_t cl[48];
	uint32_t fail = 0, fb = 0, nalloc = 0, req_sum = 0, blk_sum = 0;
	rs = 12345;
	for (int op = 0; op < 6000; op++) {
		int k = rnd() % 48;
		if (slot[k]) { pool_free(&P[cl[k]], slot[k]); slot[k] = 0; continue; }
		uint32_t n = req_size(); int i = 0;
		while (i < nc && c[i].bsz < n) i++;
		int first = i;
		for (; i < nc; i++) {
			if ((slot[k] = pool_alloc(&P[i])) != NULL) break;
			if (!fallback) { i = nc; break; }
		}
		nalloc++;
		if (i >= nc) { fail++; continue; }
		if (i != first) fb++;
		cl[k] = (uint8_t)i; req_sum += n; blk_sum += c[i].bsz;
	}
	board_printf_raw("%s allocs=%u fail=%u fallback=%u waste=%u%% peak:", nm, (unsigned)nalloc,
	                 (unsigned)fail, (unsigned)fb, (unsigned)(100 - req_sum * 100 / blk_sum));
	for (int i = 0; i < nc; i++) board_printf_raw(" %u/%u", (unsigned)P[i].peak, (unsigned)P[i].n);
	board_printf_raw("\n");
}
static void demo(void *pv)
{
	(void)pv;
	run("(A) 512x16                 ", CA, 1, 0);
	run("(B) 32x24/128x16/512x10 +fb", CB, 3, 1);
	run("(C) same classes, no fb    ", CB, 3, 0);
	board_exit(0);
}
int main(void)
{
	static StaticTask_t tcb; static StackType_t stk[512];
	xTaskCreateStatic(demo, "demo", 512, NULL, 2, stk, &tcb);
	vTaskStartScheduler();
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; $S/fr.sh ex3 $S/ex3/main.c'
```

```text
(A) 512x16                  allocs=4077 fail=2138 fallback=0 waste=91% peak: 16/16
(B) 32x24/128x16/512x10 +fb allocs=3012 fail=0 fallback=16 waste=38% peak: 24/24 14/16 7/10
(C) same classes, no fb     allocs=3016 fail=15 fallback=0 waste=38% peak: 24/24 14/16 5/10
[board_exit] code=0
[rc=0]
```

출력에서 볼 것:

- (A) 512 B 한 등급: 블록 16개로는 동시에 살아 있는 객체(평균 24개)를 못 담아 **절반 넘게 실패**(2138 / 4077)하고, 성공한 할당도 91%를 버린다.
- (B) 등급 3개: 같은 8 KiB로 실패 0, 낭비 38%. 32 B 등급이 24/24로 바닥난 순간 16번 128 B로 넘어갔다(fallback=16).
- (C) fallback을 끄면 같은 순간들에 15번 실패한다. 32 B 등급을 몇 개 더 주거나, fallback을 허용하거나 둘 중 하나다. fallback은 "작은 등급 고갈"을 "큰 등급 압박"으로 옮길 뿐이라, 큰 등급 최대 사용량(B 7/10)도 함께 봐야 한다.
- 낭비율은 성공한 할당 전체에서 1 − Σ요청 / Σ블록이다. 등급 경계가 분포 경계(32, 128)와 맞아서 3등급으로도 38%까지 내려왔다.

### 3.3 예제 3b — 등급과 개수를 계산으로 정하기 (Python)

무엇을 확인하는 코드인지: 같은 분포에서 등급 k개(맨 위는 512)의 최적 경계를 전수 탐색하고, 동시 생존 객체 24개일 때 등급별로 "고갈 확률 < 10⁻³"을 만족하는 블록 수를 이항분포 꼬리로 구한다. Don의 응용수학이 바로 쓰이는 곳이다.

```python
#!/usr/bin/env python3
"""F2: pick 3 size classes (multiples of 8, top = 512) that minimize expected internal waste
for the ex3 workload (70% U[8,32], 25% U[33,128], 5% U[129,512]); then size each class for
24 live objects so that P(class exhausted) < 1e-3 (binomial tail)."""
from math import comb
pmf = {}
for lo, hi, w in [(8, 32, .70), (33, 128, .25), (129, 512, .05)]:
    for s in range(lo, hi + 1): pmf[s] = pmf.get(s, 0) + w / (hi - lo + 1)
def cost(cls):
    req = blk = 0.0
    for s, p in pmf.items():
        b = next(c for c in cls if c >= s); req += p * s; blk += p * b
    return 1 - req / blk, blk
import itertools
cdf = [0.0] * 513
for s in range(1, 513): cdf[s] = cdf[s - 1] + pmf.get(s, 0)
ereq = sum(s * p for s, p in pmf.items())
def fast(cls):                                # E[block] via CDF, O(#classes)
    lo, eb = 0, 0.0
    for c in cls: eb += c * (cdf[c] - cdf[lo]); lo = c
    return 1 - ereq / eb, eb
for k in (1, 2, 3, 4):
    w, cls = min((fast(c + (512,))[0], c + (512,)) for c in itertools.combinations(range(8, 512, 8), k - 1))
    print(f"best {k} classes {str(cls):20s} waste {w*100:5.1f}%  mean block {fast(cls)[1]:6.1f} B")

def need(p, n=24, eps=1e-3):                 # smallest k with P(Binom(n,p) > k) < eps
    k = 0
    while sum(comb(n, i) * p**i * (1-p)**(n-i) for i in range(k + 1, n + 1)) >= eps: k += 1
    return k
for cls in [(32, 128, 512), (32, 80, 128, 512), (512,)]:
    lo, tot, parts = 0, 0, []
    for c in cls:
        p = sum(v for s, v in pmf.items() if lo < s <= c); k = need(p); tot += k * c
        parts.append(f"{c}Bx{k}"); lo = c
    print(f"24 live, P(exhaust)<1e-3 per class: {' + '.join(parts)} = {tot} B")
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/F2/py/sizeclass.py
```

```text
best 1 classes (512,)               waste  90.2%  mean block  512.0 B
best 2 classes (128, 512)           waste  65.9%  mean block  147.2 B
best 3 classes (32, 128, 512)       waste  37.3%  mean block   80.0 B
best 4 classes (32, 80, 128, 512)   waste  32.2%  mean block   74.0 B
24 live, P(exhaust)<1e-3 per class: 32Bx23 + 128Bx13 + 512Bx5 = 4960 B
24 live, P(exhaust)<1e-3 per class: 32Bx23 + 80Bx9 + 128Bx9 + 512Bx5 = 5168 B
24 live, P(exhaust)<1e-3 per class: 512Bx24 = 12288 B
```

출력에서 볼 것:

- 낭비율은 등급 1 → 2 → 3에서 90 → 66 → 37%로 크게 줄고, 4등급(37 → 32%)부터는 효과가 작다.
- **4등급이 오히려 RAM을 더 예약한다**(5168 B > 4960 B). 등급마다 따로 꼬리 여유를 잡아야 하기 때문이다. 이 "등급별 안전 여유"는 낭비율 계산에 안 보이는 비용이다. 실무에서 등급을 3 ~ 4개 이상 잘 늘리지 않는 이유다.
- 512 B 한 등급으로 같은 보장(24개 전부)을 하려면 12 KiB가 든다. 등급 3개면 4.9 KiB.
- 주의: 이항 모델은 "각 객체의 크기가 독립"이라고 가정한다. 실제 FC에서는 "로그 덤프 시작 → 큰 메시지 몰림"처럼 상관이 있으니, 모델로 시작값을 정하고 §4의 `max_used` 같은 런타임 통계로 확인한다.

---

## 4. Zephyr `k_mem_slab` — 커널이 주는 고정 크기 풀

### 4.1 API 한눈에 (v4.4.2 `include/zephyr/kernel.h`에서 확인)

| API | 하는 일 | ISR |
|---|---|---|
| `K_MEM_SLAB_DEFINE(name, block_size, num_blocks, align)` | 버퍼 + 객체를 정적으로 정의. `block_size % align == 0`과 align이 2의 거듭제곱인지 `BUILD_ASSERT` | — |
| `K_MEM_SLAB_DEFINE_STATIC(...)`, `K_MEM_SLAB_DEFINE_IN_SECT(...)` | `static` 버전, 버퍼를 지정 섹션에 (예: DMA 가능 SRAM, F8) | — |
| `k_mem_slab_init(slab, buf, block_size, num_blocks)` | 런타임 초기화. 정렬이 word가 아니면 `-EINVAL` | — |
| `k_mem_slab_alloc(slab, &mem, timeout)` | 0, `-ENOMEM`(K_NO_WAIT로 실패), `-EAGAIN`(timeout) | K_NO_WAIT만 |
| `k_mem_slab_free(slab, mem)` | 대기자가 있으면 그 스레드에게 **직접** 넘긴다 | 가능 |
| `k_mem_slab_num_used_get()`, `k_mem_slab_num_free_get()` | 현재 사용 · 남은 블록 수 | 가능 |
| `k_mem_slab_max_used_get()` | 최대 동시 사용 수. `CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION=y`가 아니면 0 | 가능 |
| `k_mem_slab_runtime_stats_get(slab, &sys_memory_stats)` | 바이트 단위 allocated / free / max_allocated | 가능 |
| `k_mem_slab_runtime_stats_reset_max()` | max를 현재값으로 (TRACE 옵션 있을 때만) | 가능 |

알아 둘 사실 셋.

- `K_MEM_SLAB_DEFINE`은 버퍼를 `__noinit_named(k_mem_slab_buf_<name>)`에 둔다. 즉 **`.noinit` 섹션이고 0으로 지워지지 않는다**(예제 4의 `objdump -h`에서 `noinit` 섹션 확인). 풀 블록은 원래 alloc 뒤 사용자가 채우는 것이니 문제없지만, "새 블록은 0"이라고 가정한 코드는 틀린다.
- 블록 크기는 `WB_UP()`으로 word 배수로 올라간다. 100 B 슬랩의 stride는 100 B(이미 4의 배수, 예제 8), 24 B 요청에 align 8이면 24 B.
- `CONFIG_MEM_SLAB_POINTER_VALIDATE`(기본값 = `CONFIG_ASSERT`)를 켜면 free할 때 "버퍼 범위 안, 블록 경계"를 검사하고 어기면 `k_panic()`. 예제 1의 `POOL_DEBUG` 범위 검사와 같다. double free는 검사하지 않는다.

### 4.2 소스 — `kernel/mem_slab.c` (354줄)

**초기화** `create_free_list()`: 블록 크기와 버퍼 주소가 word 정렬인지 확인한 뒤, **마지막 블록부터 거꾸로** 꿴다. 그래서 head가 블록 #0이 되고 처음 할당은 #0, #1, #2 … 순서로 나간다(예제 1의 `pool_init`과 같은 순서).

```c
/* kernel/mem_slab.c create_free_list() — 발췌 */
	slab->free_list = NULL;
	p = (char *)(total_size - slab->info.block_size);   /* total_size = buffer + n*bsz */
	for (uint32_t i = 0; i < slab->info.num_blocks; i++) {
		*(char **)p = slab->free_list;
		slab->free_list = p;
		p -= slab->info.block_size;
	}
```

**alloc** `k_mem_slab_alloc()`: spinlock(단일 코어에서는 BASEPRI 올리기) 아래에서 세 갈래.

1. `free_list != NULL`: head를 떼고 `num_used++`, 옵션이면 `max_used` 갱신. 끝.
2. 비었고 `K_NO_WAIT`(또는 `CONFIG_MULTITHREADING=n`): `*mem = NULL`, `-ENOMEM`.
3. 비었고 timeout이 있으면: `z_pend_curr(&slab->lock, key, &slab->wait_q, timeout)`로 잠든다. 깨어나 결과가 0이면 `*mem = _current->base.swap_data` — **반납자가 넣어 준 블록**을 받는다.

**free** `k_mem_slab_free()`: free_list가 비어 있을 때만(= 대기자가 있을 수 있을 때만) `z_unpend_first_thread(&slab->wait_q)`를 본다. 대기자가 있으면 `z_thread_return_value_set_with_data(pending_thread, 0, mem)`로 블록을 그 스레드의 `swap_data`에 직접 넣고 깨운 뒤 `z_reschedule()`. 이 경우 **free_list에도 넣지 않고 `num_used`도 줄이지 않는다** — 블록이 반납자 손에서 대기자 손으로 바로 건너가기 때문이다. 대기자가 없으면 head에 붙이고 `num_used--`.

이 "직접 전달(hand-off)"이 중요한 이유: 반납과 재할당 사이에 다른 스레드가 끼어들어 블록을 가로챌 틈이 없다. 기다린 스레드가 반드시 받는다. D10 §10.1에서 본 `k_msgq` · `k_stack`의 "대기자에게 직접, 없으면 버퍼에" 패턴과 같다.

### 4.3 예제 4 — 배치, LIFO, 통계, `-ENOMEM` vs `-EAGAIN`, 직접 전달, ISR 할당

무엇을 확인하는 코드인지: 24 B × 4 블록 슬랩의 버퍼 정렬과 할당 순서, 반납 후 LIFO, 사용량 · 통계 API, 빈 슬랩에서 `K_NO_WAIT`와 `K_MSEC(50)`의 반환값과 실제 대기 시간, 대기 스레드가 있을 때 free가 `num_used`를 바꾸지 않는지(직접 전달), `k_timer` 콜백(ISR)에서의 `K_NO_WAIT` 할당.

```c
/* F2 ex4 — Zephyr k_mem_slab: layout, LIFO reuse, counters/stats, -ENOMEM vs -EAGAIN,
 * direct hand-off to a waiting thread, and allocation from an ISR (k_timer). */
#include <zephyr/kernel.h>
#include "zf2.h"

K_MEM_SLAB_DEFINE(slab, 24, 4, 8);                 /* 4 blocks x 24 B, 8-byte aligned */
extern char _k_mem_slab_buf_slab[];
#define IDX(p) ((int)(((char *)(p) - _k_mem_slab_buf_slab) / 24))

static void show(const char *t)
{
	struct sys_memory_stats st;
	k_mem_slab_runtime_stats_get(&slab, &st);
	printk("[%3lld ms] %-13s used=%u free=%u max_used=%u | stats alloc=%u free=%u max=%u B\n",
	       k_uptime_get(), t,
	       k_mem_slab_num_used_get(&slab), k_mem_slab_num_free_get(&slab), k_mem_slab_max_used_get(&slab),
	       (unsigned)st.allocated_bytes, (unsigned)st.free_bytes, (unsigned)st.max_allocated_bytes);
}
static void waiter(void *a, void *b, void *c)
{
	ARG_UNUSED(a); ARG_UNUSED(b); ARG_UNUSED(c);
	void *m; int64_t t0 = k_uptime_get();
	int r = k_mem_slab_alloc(&slab, &m, K_FOREVER);
	printk("[%3lld ms] waiter: alloc -> %d block #%d after %lld ms\n", k_uptime_get(), r, IDX(m),
	       k_uptime_get() - t0);
}
K_THREAD_DEFINE(wt, 1024, waiter, NULL, NULL, NULL, 1, 0, 100);   /* starts at t=100 ms */

static void tmr_cb(struct k_timer *t)
{
	ARG_UNUSED(t); void *m;
	int r = k_mem_slab_alloc(&slab, &m, K_NO_WAIT);   /* ISR: K_NO_WAIT only */
	printk("ISR(in_isr=%d): alloc -> %d block #%d\n", k_is_in_isr(), r, r ? -1 : IDX(m));
	if (!r) k_mem_slab_free(&slab, m);
}
K_TIMER_DEFINE(tmr, tmr_cb, NULL);

int main(void)
{
	void *b[4], *m;
	printk("buf=%p (mod 8 = %u) block_size=%u num_blocks=%u sizeof(struct k_mem_slab)=%u\n",
	       _k_mem_slab_buf_slab, (unsigned)((uintptr_t)_k_mem_slab_buf_slab % 8),
	       (unsigned)slab.info.block_size, slab.info.num_blocks, (unsigned)sizeof(struct k_mem_slab));
	for (int i = 0; i < 4; i++) k_mem_slab_alloc(&slab, &b[i], K_NO_WAIT);
	printk("alloc order: #%d #%d #%d #%d\n", IDX(b[0]), IDX(b[1]), IDX(b[2]), IDX(b[3]));
	show("all taken");
	k_mem_slab_free(&slab, b[1]); k_mem_slab_free(&slab, b[3]);
	k_mem_slab_alloc(&slab, &m, K_NO_WAIT);
	printk("free #1,#3 then alloc -> #%d (LIFO)\n", IDX(m));
	k_mem_slab_alloc(&slab, &b[1], K_NO_WAIT);
	k_timer_start(&tmr, K_MSEC(5), K_NO_WAIT); k_sleep(K_MSEC(10));
	printk("K_NO_WAIT on empty -> %d (-ENOMEM=%d)\n", k_mem_slab_alloc(&slab, &m, K_NO_WAIT), -ENOMEM);
	int64_t t0 = k_uptime_get();
	int r = k_mem_slab_alloc(&slab, &m, K_MSEC(50));
	printk("K_MSEC(50) on empty -> %d (-EAGAIN=%d) after %lld ms\n", r, -EAGAIN, k_uptime_get() - t0);
	k_sleep(K_MSEC(60));                               /* waiter blocks at t=100 ms */
	k_sleep(K_MSEC(30));
	k_mem_slab_free(&slab, b[2]);                       /* hand #2 to the waiter */
	show("after free #2");
	k_sleep(K_MSEC(1));
	k_mem_slab_free(&slab, b[0]);
	k_timer_start(&tmr, K_MSEC(1), K_NO_WAIT); k_sleep(K_MSEC(20));
	show("end");
	qemu_exit(0);
	return 0;
}
```

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_QEMU_ICOUNT_SHIFT=5
CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION=y
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/F2/zb.sh ex4 ex4; \
  arm-none-eabi-nm /var/tmp/rtos/F2/ex4/zephyr/zephyr.elf | grep mem_slab_buf; \
  arm-none-eabi-objdump -h /var/tmp/rtos/F2/ex4/zephyr/zephyr.elf | grep noinit'
```

```text
           FLASH:       15184 B         4 MB      0.36%
             RAM:        7448 B         4 MB      0.18%
*** Booting Zephyr OS build v4.4.2 ***
buf=0x200007b8 (mod 8 = 0) block_size=24 num_blocks=4 sizeof(struct k_mem_slab)=32
alloc order: #0 #1 #2 #3
[  0 ms] all taken     used=4 free=0 max_used=4 | stats alloc=96 free=0 max=96 B
free #1,#3 then alloc -> #3 (LIFO)
ISR(in_isr=1): alloc -> -12 block #-1
K_NO_WAIT on empty -> -12 (-ENOMEM=-12)
K_MSEC(50) on empty -> -11 (-EAGAIN=-11) after 60 ms
[190 ms] after free #2 used=4 free=0 max_used=4 | stats alloc=96 free=0 max=96 B
[190 ms] waiter: alloc -> 0 block #2 after 80 ms
ISR(in_isr=1): alloc -> 0 block #0
[240 ms] end           used=3 free=1 max_used=4 | stats alloc=72 free=24 max=96 B
[rc=0]
200007b8 B _k_mem_slab_buf_slab
 26 noinit        00001960  200003b8  200003b8  00003c30  2**3
```

출력에서 볼 것:

- 버퍼가 8 B 정렬(`mod 8 = 0`)이고 `.noinit` 섹션(0x200003b8부터 0x1960 B) 안에 있다. 할당 순서는 #0 → #3, 반납 뒤에는 LIFO(#3).
- 빈 슬랩: `K_NO_WAIT`는 `-ENOMEM`(-12), timeout은 `-EAGAIN`(-11). D10 §3.2에서 본 대로 **객체마다 "없음" 에러 코드가 다르다**(`k_stack`은 `-EBUSY`, `k_msgq`는 `-ENOMSG`). 반환값은 `!= 0`으로 검사한다.
- `K_MSEC(50)`이 60 ms 만에 끝났다. 이 보드는 100 Hz tick이라 50 ms = 5 tick, 상대 timeout에 +1 tick. FreeRTOS(예제 2)는 50 ms였다.
- 대기 스레드는 110 ms에 시작해 190 ms까지 80 ms 기다렸다. 190 ms에 main이 #2를 free한 **직후에도 `used=4`**다. 블록이 free_list를 거치지 않고 대기자에게 건너갔다는 증거다. 대기자 출력은 main보다 낮은 우선순위라 main이 `show()`를 찍은 뒤에 나왔다.
- `k_timer` 콜백은 ISR 문맥(`in_isr=1`)이다. 빈 슬랩이면 -12, 블록이 있으면 0으로 성공한다. ISR에서 timeout을 주면 안 된다(문서의 "`timeout` must be set to K_NO_WAIT if called from ISR").
- `sizeof(struct k_mem_slab)`은 32 B. 예제 8처럼 `TRACE_MAX_UTILIZATION` 없이 빌드하면 28 B다(`max_used` 필드 4 B).

### 4.4 슬랩 + `k_fifo`로 포인터 넘기기 — 주의 하나

Zephyr에서 D4 §3의 "블록은 풀에, 포인터만 큐로" 패턴은 보통 `k_mem_slab` + `k_fifo`(또는 포인터를 담은 `k_msgq`)로 짠다. `k_fifo`는 아이템의 **첫 word를 next 포인터로 쓴다**(D10 §2). 슬랩도 빈 블록의 첫 word를 쓰지만 둘이 겹치는 순간은 없다 — fifo 안에 있는 블록은 할당된 상태이고, free된 블록은 fifo에 없다. 대신 구조체 첫 필드를 `void *fifo_reserved;`로 비워 두는 것을 잊으면 fifo가 사용자 데이터를 덮어쓴다(D10 §2.3의 "k_fifo를 부수는 방법" 첫 번째).

---

## 5. Zephyr `k_heap` · `sys_heap` · `k_malloc` · `sys_mem_blocks`

### 5.1 층 구조

- **`sys_heap`** (`lib/heap/heap.c`, 794줄): 락도 대기도 없는 순수 할당기. 아무 메모리 영역에나 `sys_heap_init(heap, mem, bytes)`로 만든다.
- **`k_heap`** (`kernel/kheap.c`): `sys_heap` + spinlock + `wait_q`. `K_HEAP_DEFINE(name, bytes)`, `k_heap_alloc(h, bytes, timeout)`, `k_heap_aligned_alloc()`, `k_heap_calloc()`, `k_heap_realloc()`, `k_heap_free()`. 블로킹 할당이 된다.
- **`k_malloc` / `k_free`** (`kernel/mempool.c`): `CONFIG_HEAP_MEM_POOL_SIZE` 바이트짜리 시스템 `k_heap`(`_system_heap`). 블로킹 없음(항상 K_NO_WAIT 의미). 블록 앞에 **소속 힙 포인터 4 B**를 숨겨 두고 `k_free`가 그걸 읽어 어느 힙에 돌려줄지 안다.
- **`sys_mem_blocks`** (`lib/mem_blocks/mem_blocks.c`, `CONFIG_SYS_MEM_BLOCKS`): 고정 크기 블록 할당기지만 free list가 아니라 **비트맵**(`sys_bitarray`)으로 관리한다. 블록 메모리를 **한 바이트도 건드리지 않는다** — 그래서 캐시를 끈(uncached) DMA 영역, 다른 코어와 공유하는 영역, 외부 메모리처럼 "할당기가 블록 안에 쓰면 곤란한" 곳에 쓴다. 여러 개를 한 번에 받는 `sys_mem_blocks_alloc_contiguous()`도 있다. 대가는 비트맵 검색(§7: alloc 127 ~ 149 cycles, 점유율에 따라 조금씩 증가)과 대기 기능 없음.

### 5.2 `sys_heap` 내부 — 청크, 헤더, bucket

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">sys_heap — 8 B 청크, 4 B 헤더(작은 힙), 2의 거듭제곱 bucket</text>
<rect x="20" y="44" width="120" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="80" y="64" font-size="12" text-anchor="middle">chunk 0</text>
<text x="80" y="82" font-size="12" text-anchor="middle">헤더 + bucket</text>
<rect x="140" y="44" width="30" height="50" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="155" y="74" font-size="12" text-anchor="middle">hdr</text>
<rect x="170" y="44" width="110" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="225" y="74" font-size="12" text-anchor="middle">사용 중 데이터</text>
<rect x="280" y="44" width="30" height="50" fill="#3f9a6b" fill-opacity="0.3" stroke="#3f9a6b"/>
<text x="295" y="74" font-size="12" text-anchor="middle">hdr</text>
<rect x="310" y="44" width="150" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="385" y="64" font-size="12" text-anchor="middle">free: prev · next</text>
<text x="385" y="82" font-size="12" text-anchor="middle">(청크 ID, 16비트)</text>
<rect x="460" y="44" width="30" height="50" fill="#d0564a" fill-opacity="0.3" stroke="#d0564a"/>
<text x="475" y="74" font-size="12" text-anchor="middle">hdr</text>
<rect x="490" y="44" width="110" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="545" y="74" font-size="12" text-anchor="middle">사용 중 데이터</text>
<rect x="600" y="44" width="60" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="630" y="74" font-size="12" text-anchor="middle">end</text>
<text x="20" y="120" font-size="12">hdr = LEFT_SIZE(왼쪽 이웃 크기) + SIZE_AND_USED(내 크기, 최하위 비트 = 사용 중)</text>
<text x="20" y="140" font-size="12">→ 이웃을 O(1)로 찾아 free 때 바로 병합한다 (heap_4와 달리 리스트를 훑지 않는다)</text>
<text x="20" y="176" font-size="13">buckets[i] = 크기가 [2^i, 2^(i+1)) 청크인 free 청크의 원형 이중 연결 리스트</text>
<rect x="20" y="192" width="70" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="55" y="212" font-size="12" text-anchor="middle">b0</text>
<rect x="90" y="192" width="70" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="125" y="212" font-size="12" text-anchor="middle">b1</text>
<rect x="160" y="192" width="70" height="30" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="195" y="212" font-size="12" text-anchor="middle">b2 ●</text>
<rect x="230" y="192" width="70" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="265" y="212" font-size="12" text-anchor="middle">b3</text>
<rect x="300" y="192" width="70" height="30" fill="#4a7bd0" fill-opacity="0.3" stroke="#4a7bd0"/><text x="335" y="212" font-size="12" text-anchor="middle">b4 ●</text>
<rect x="370" y="192" width="70" height="30" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="405" y="212" font-size="12" text-anchor="middle">…</text>
<path d="M335,191 C335,150 385,140 385,98" fill="none" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah2)"/>
<text x="460" y="212" font-size="12">avail_buckets 비트맵 = 0b10100</text>
<text x="20" y="254" font-size="12">alloc(n): ① 딱 맞는 bucket bi에서 최대 SYS_HEAP_ALLOC_LOOPS(기본 3)개만 크기 확인</text>
<text x="20" y="276" font-size="12">② 없으면 avail_buckets에서 bi보다 큰 첫 비트를 __builtin_ctz로 골라 그 bucket 맨 앞 청크를 무조건 사용</text>
<text x="20" y="298" font-size="12">③ 남는 부분은 split해서 다시 bucket에 넣는다 → 검색 길이에 상한이 있다 (bucket 수 ≤ 32)</text>
<text x="20" y="320" font-size="12">대가: 맞는 조각이 있는데도 ①에서 3개 안에 못 찾으면 더 큰 청크를 쪼갠다 (단편화를 시간과 맞바꿈)</text>
</svg>
```
그림 2 — `sys_heap`의 메모리 배치와 bucket. 헤더가 왼쪽 · 자기 크기를 둘 다 가지고 있어서 free가 이웃을 바로 합치고, alloc은 bucket 비트맵 덕분에 검색 길이가 정해져 있다.

소스(`lib/heap/heap.h`, `heap.c`)에서 확인한 사실:

- **청크 단위 8 B**(`CHUNK_UNIT`). 청크는 버퍼 시작에서의 8 B 단위 오프셋(청크 ID)으로 부른다.
- **헤더 크기**: 힙이 `0x7fff` 청크(256 KiB) 미만이면 필드를 16비트로 저장해 헤더가 **4 B**, 넘으면 32비트 필드 8 B(`chunk_header_bytes()`). Kconfig로 `SYS_HEAP_SMALL_ONLY` · `BIG_ONLY` · `AUTO`(기본, SRAM ≤ 256 KiB면 SMALL_ONLY가 기본) 중 하나를 고른다. AUTO는 필드 접근마다 "큰 힙이냐"를 런타임에 확인한다.
- **요청 → 청크 수**: `bytes_to_chunksz()` = ceil((요청 + 헤더 4) / 8). 요청 1 ~ 4 B → 1청크(8 B), 5 ~ 12 → 2청크, 29 → 5청크(40 B), 100 → 13청크(104 B). 예제 5의 stride가 정확히 이것이다.
- **`alloc_chunk()`**: 위 그림의 ① ② 그대로. 주석이 직접 "constant time performance"와 "linear time searching을 피한다"고 쓴다. `CONFIG_SYS_HEAP_ALLOC_LOOPS`를 키우면 단편화에 강해지는 대신 최악 시간이 늘어난다.
- **`free_chunk()`**: 오른쪽 이웃이 free면 합치고, 왼쪽 이웃이 free면 합친다. 둘 다 헤더만 보면 된다(O(1)). 합친 청크를 크기에 맞는 bucket에 넣는다.
- **hardening** (`CONFIG_SYS_HEAP_HARDENING_*`, v4.4.2 소스에 있음): NONE(0) · BASIC(1, 기본) · MODERATE(2, `CONFIG_ASSERT`면 기본) · FULL(3, 청크마다 8 B canary trailer) · EXTREME(4, 매 연산마다 힙 전체 검증). BASIC은 free 때 "이미 free인가(double free)"와 "`left_chunk(right_chunk(c)) == c`인가(오른쪽 넘침)"를 본다. 예제 5에서 double free를 잡는 것이 이 BASIC 검사다.

### 5.3 예제 5 — stride, `k_malloc`의 숨은 포인터, 통계, 그리고 free가 대기자를 **모두** 깨운다

무엇을 확인하는 코드인지: (1) 2 KiB `k_heap`에서 요청 크기별 실제 소비 바이트(연속 할당의 주소 차 = stride)와 `sys_heap_usable_size()`, (2) `k_malloc`이 블록 앞에 힙 포인터를 두는지, (3) `sys_heap_runtime_stats_get()`, (4) 힙을 가득 채운 뒤 1000 B · 200 B · 200 B를 원하는 세 스레드(우선순위 1 · 2 · 3)가 기다릴 때 낮은 우선순위 main이 64 B 블록 6개를 하나씩 free하면 무슨 일이 생기는지, free 한 번이 몇 cycle 걸리는지.

```c
/* F2 ex5 — Zephyr k_heap / sys_heap: bytes consumed per request (stride), k_malloc's hidden
 * heap pointer, runtime stats, and what k_heap_free does to blocked allocators (wakes ALL). */
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include "zf2.h"
#define FPGA_CNT (*(volatile uint32_t *)0x40028018u)

K_HEAP_DEFINE(h, 2048);
K_SEM_DEFINE(go, 0, 3);
static void *p[64];
static int got[3];
static const size_t want[3] = { 1000, 200, 200 };

static void w(void *a, void *b, void *c)
{
	ARG_UNUSED(b); ARG_UNUSED(c);
	int id = (int)(intptr_t)a;
	k_sem_take(&go, K_FOREVER);
	void *m = k_heap_alloc(&h, want[id], K_MSEC(300));
	got[id] = m != NULL;
	printk("[%3lld ms] waiter%d (prio %d) want %u -> %s\n", k_uptime_get(), id, id + 1,
	       (unsigned)want[id], m ? "OK" : "timeout");
}
K_THREAD_DEFINE(w0, 1024, w, (void *)0, NULL, NULL, 1, 0, 0);
K_THREAD_DEFINE(w1, 1024, w, (void *)1, NULL, NULL, 2, 0, 0);
K_THREAD_DEFINE(w2, 1024, w, (void *)2, NULL, NULL, 3, 0, 0);

int main(void)
{
	static const size_t sz[] = { 1, 4, 5, 12, 13, 20, 28, 29, 100 };
	struct sys_memory_stats st;
	void *q[10];
	for (int i = 0; i < 10; i++) q[i] = k_heap_alloc(&h, i < 9 ? sz[i] : 8, K_NO_WAIT);
	for (int i = 0; i < 9; i++)
		printk("req %3u B -> stride %3d B, usable %3u B\n", (unsigned)sz[i], (int)((char *)q[i + 1] - (char *)q[i]),
		       (unsigned)sys_heap_usable_size(&h.heap, q[i]));
	void *m1 = k_malloc(32), *m2 = k_malloc(32), *k1 = k_heap_alloc(&h, 32, K_NO_WAIT), *k2 = k_heap_alloc(&h, 32, K_NO_WAIT);
	printk("32 B: k_malloc stride %d B (heap ptr at %p = %p), k_heap_alloc stride %d B\n",
	       (int)((char *)m2 - (char *)m1), (void *)((void **)m1 - 1), *((void **)m1 - 1), (int)((char *)k2 - (char *)k1));
	uint32_t t0 = FPGA_CNT; k_heap_free(&h, q[0]);
	printk("k_heap_free, no waiters: %u cycles\n", (unsigned)(FPGA_CNT - t0));
	for (int i = 1; i < 10; i++) k_heap_free(&h, q[i]);
	k_heap_free(&h, k1); k_heap_free(&h, k2);
	sys_heap_runtime_stats_get(&h.heap, &st);
	printk("empty heap(2048): free=%u alloc=%u max=%u B\n", (unsigned)st.free_bytes,
	       (unsigned)st.allocated_bytes, (unsigned)st.max_allocated_bytes);
	int n = 0;
	while (n < 64 && (p[n] = k_heap_alloc(&h, 64, K_NO_WAIT)) != NULL) n++;
	while (k_heap_alloc(&h, 8, K_NO_WAIT)) { }
	sys_heap_runtime_stats_get(&h.heap, &st);
	printk("filled: %d x 64 B, free=%u B, alloc=%u B\n", n, (unsigned)st.free_bytes, (unsigned)st.allocated_bytes);
	k_thread_priority_set(k_current_get(), 5);         /* waiters (1..3) now outrank main */
	k_sem_give(&go); k_sem_give(&go); k_sem_give(&go);
	k_sleep(K_MSEC(10));
	for (int i = 2; i < 8; i++) {                       /* free 6 neighbours, one at a time */
		uint32_t t = FPGA_CNT; k_heap_free(&h, p[i]); t = FPGA_CNT - t;
		printk("[%3lld ms] main: freed p[%d] (contiguous free %d B) - call took %u cycles\n",
		       k_uptime_get(), i, (i - 1) * 72, (unsigned)t);
	}
	k_sleep(K_MSEC(400));
	printk("got=%d/%d/%d\n", got[0], got[1], got[2]);
#ifdef DOUBLE
	void *d = k_heap_alloc(&h, 16, K_NO_WAIT);
	k_heap_free(&h, p[10]); k_heap_free(&h, p[10]);     /* double free */
	ARG_UNUSED(d);
#endif
	qemu_exit(0);
	return 0;
}
```

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_QEMU_ICOUNT_SHIFT=5
CONFIG_HEAP_MEM_POOL_SIZE=4096
CONFIG_SYS_HEAP_RUNTIME_STATS=y
```

```sh
limactl shell elinux -- bash -lc '/Users/donh/.rtos_scratch/F2/zb.sh ex5 ex5'
```

```text
           FLASH:       16336 B         4 MB      0.39%
             RAM:       16124 B         4 MB      0.38%
*** Booting Zephyr OS build v4.4.2 ***
req   1 B -> stride   8 B, usable   4 B
req   4 B -> stride   8 B, usable   4 B
req   5 B -> stride  16 B, usable  12 B
req  12 B -> stride  16 B, usable  12 B
req  13 B -> stride  24 B, usable  20 B
req  20 B -> stride  24 B, usable  20 B
req  28 B -> stride  32 B, usable  28 B
req  29 B -> stride  40 B, usable  36 B
req 100 B -> stride 104 B, usable 100 B
32 B: k_malloc stride 40 B (heap ptr at 0x20002ef4 = 0x200000a4), k_heap_alloc stride 40 B
k_heap_free, no waiters: 230 cycles
empty heap(2048): free=1972 alloc=0 max=320 B
filled: 27 x 64 B, free=0 B, alloc=1860 B
[ 20 ms] main: freed p[2] (contiguous free 72 B) - call took 3667 cycles
[ 20 ms] main: freed p[3] (contiguous free 144 B) - call took 3870 cycles
[ 20 ms] waiter1 (prio 2) want 200 -> OK
[ 20 ms] main: freed p[4] (contiguous free 216 B) - call took 5996 cycles
[ 20 ms] main: freed p[5] (contiguous free 288 B) - call took 2640 cycles
[ 20 ms] main: freed p[6] (contiguous free 360 B) - call took 2669 cycles
[ 20 ms] waiter2 (prio 3) want 200 -> OK
[ 20 ms] main: freed p[7] (contiguous free 432 B) - call took 4842 cycles
[320 ms] waiter0 (prio 1) want 1000 -> timeout
got=0/1/1
[rc=0]
```

출력에서 볼 것:

- **stride = ceil((요청 + 4) / 8) × 8.** 1 ~ 4 B가 8 B, 29 B가 40 B, 100 B가 104 B. usable은 stride − 4(헤더). 28 B 요청은 낭비 0, 29 B는 7 B 낭비 — 구조체 크기를 8k − 4로 맞추면 공짜 바이트를 얻는다.
- `k_malloc(32)`도 stride 40 B인데, 그 이유가 다르다: 요청 32 + 힙 포인터 4 = 36 → 헤더 4 더해 40. 블록 바로 앞 word(`0x20002ef4`)에 `_system_heap` 주소(`0x200000a4`)가 들어 있다. 32 B에서는 우연히 같지만 100 B에서는 `k_malloc` 112 B, `k_heap_alloc` 104 B로 갈린다(예제 8).
- 2048 B 힙의 실제 free는 1972 B. 76 B는 chunk 0(`struct z_heap` + bucket 8개 = 64 B), 끝 표지 4 B, 마지막 청크의 헤더 4 B, 8 B 정렬 반올림이다. **K_HEAP_DEFINE(name, N)의 N이 그대로 쓸 수 있는 바이트가 아니다.**
- 대기자가 없을 때 `k_heap_free`는 230 cycles. 대기자 셋이 있을 때는 **2640 ~ 5996 cycles**로 10 ~ 26배다. 이유는 `kernel/kheap.c`의 `k_heap_free()`에 있다: `z_unpend_all(&heap->wait_q)` — **대기자를 전부** 깨운다. 셋 모두 main보다 우선순위가 높으니 free 호출 안에서 차례로 돌아 `sys_heap_alloc()`을 다시 해 보고, 안 되면 다시 잠든다(`z_heap_alloc_helper()`의 while 루프). free 한 번마다 재시도 3번과 컨텍스트 스위치 여러 번이 붙는다.
- 200 B(26청크 = 208 B)를 원한 waiter1은 contiguous free가 216 B가 된 p[4] free 때 성공했다(그 줄이 "freed p[4]" 출력보다 먼저 찍힌 것은 waiter가 free 호출 안에서 먼저 돌았기 때문). waiter2는 p[7]에서 성공했다. **가장 높은 우선순위(1)의 waiter0은 1000 B를 원해서 끝내 못 받고 300 ms timeout**으로 끝났다. 우선순위가 낮은 작은 요청이 먼저 가져간다 — `k_heap`의 대기는 "공정한 줄"이 아니라 "깨워 줄 테니 각자 다시 시도"다.
- `k_mem_slab`(예제 4)은 정반대다: 블록이 모두 같으니 첫 대기자 **한 명**에게 직접 준다. 비교는 §8 그림 7.

같은 프로그램에 double free를 넣고(`-DDOUBLE`, `CONFIG_LOG=y`, `CONFIG_LOG_MODE_MINIMAL=y`) 돌리면 BASIC hardening이 잡는다.

```sh
limactl shell elinux -- bash -lc 'NORUN=1 /Users/donh/.rtos_scratch/F2/zb.sh ex5 ex5d -- -DEXTRA_CFLAGS=-DDOUBLE \
  -DCONFIG_LOG=y -DCONFIG_LOG_MODE_MINIMAL=y && ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F2/ex5d/zephyr/zephyr.elf 5 | tail -10'
```

```text
got=0/1/1
E: heap corruption (double free?) at 0x200014bc
E: r0/a1:  0x00000004  r1/a2:  0x0000000a  r2/a3:  0x00000000
E: r3/a4:  0x00000004 r12/ip:  0x00000000 r14/lr:  0x00000371
E:  xpsr:  0x21000000
E: Faulting instruction address (r15/pc): 0x0000037e
E: >>> ZEPHYR FATAL ERROR 4: Kernel panic on CPU 0
E: Current thread: 0x200002e0 (unknown)
E: Halting system
```

출력에서 볼 것: `sys_heap_free()`의 첫 검사(`!chunk_used(h, c)`)가 두 번째 free를 잡아 `k_panic()`으로 멈췄다. 직접 만든 풀(예제 1)은 같은 상황에서 조용히 고리를 만들었다. `k_mem_slab`은 double free를 잡지 않는다(§4.1) — 직접 만든 풀처럼 비트맵이 필요하면 직접 얹어야 한다.

### 5.4 언제 무엇을 (Zephyr 안에서)

| 상황 | 고를 것 | 이유 |
|---|---|---|
| 같은 크기 객체, 태스크가 기다려도 됨 | `k_mem_slab` + timeout | O(1), 대기자에게 직접 전달, ISR에서도 K_NO_WAIT로 |
| 크기가 제각각, 상한 있는 시간이 필요 | 전용 `k_heap` (서브시스템마다 따로) | 시간 상한(§7: 최악 243 cycles, 점유율 무관). 대신 단편화로 실패 가능 |
| 부팅 · 설정 경로의 가끔 쓰는 할당 | `k_malloc` | 편하지만 시스템 힙 하나를 모두가 나눠 쓴다. 블로킹 없음 |
| 블록 안에 메타데이터를 쓰면 안 되는 메모리 | `sys_mem_blocks` | 비트맵이 밖에 있다. 연속 블록 여러 개 할당도 된다 |
| ISR ↔ 스레드, 버퍼 포인터만 돌림 | `k_stack` free-list (D10 §3) | 슬랩보다 단순, LIFO |

---

## 6. ThreadX — block pool과 byte pool

ThreadX는 두 할당기를 처음부터 커널 객체로 준다. 둘 다 "메모리를 호출자가 넘겨준다"(A5 §2.4의 ThreadX 스타일).

### 6.1 block pool (`tx_block_pool_create` / `tx_block_allocate` / `tx_block_release`)

소스 `common/src/tx_block_pool_create.c`, `tx_block_allocate.c`, `tx_block_release.c`에서 확인한 구조:

- 블록 크기를 `ALIGN_TYPE`(ULONG, 4 B) 배수로 올리고, 블록마다 **앞에 포인터 1개(4 B) 헤더**를 둔다. 블록 수 = `pool_size / (block_size + sizeof(UCHAR *))`. 1024 B 풀에 32 B 블록이면 1024 / 36 = 28개(예제 6).
- 헤더는 free일 때 "다음 free 블록", **할당되면 "소속 풀 포인터"**가 된다(`tx_block_allocate.c` 163 ~ 164행: `*next_block_ptr = pool_ptr`). 그래서 `tx_block_release(block_ptr)`는 **풀 인자가 없다** — 헤더에서 풀을 찾는다. Zephyr `k_mem_slab_free(slab, mem)`는 풀을 넘겨야 하는 것과 대비된다. 대가가 블록당 4 B다.
- 비었을 때 wait_option이 있으면 스레드를 풀의 suspension list(FIFO, `tx_block_pool_prioritize()`로 최고 우선순위를 맨 앞으로 옮길 수 있다)에 건다. release는 대기자가 있으면 그 스레드에게 블록을 직접 넘긴다 — Zephyr 슬랩과 같은 hand-off다.
- ISR: `_txe_block_allocate()`는 ISR에서 `TX_NO_WAIT`면 허용한다.

### 6.2 byte pool (`tx_byte_pool_create` / `tx_byte_allocate` / `tx_byte_release`)

byte pool은 Don이 예상하는 "범용 힙"과 동작이 꽤 다르다. `tx_byte_pool_search.c`를 읽으면:

- 블록마다 **8 B 헤더**: 다음 블록 포인터(주소 순서) + 상태 word(free면 `TX_BYTE_BLOCK_FREE` = `0xFFFFEEEE`, 사용 중이면 소속 풀 포인터). 리스트는 **free 블록만이 아니라 모든 블록**을 주소 순서로 잇는다.
- 할당은 **first-fit**: `tx_byte_pool_search`(roving 검색 포인터)부터 블록을 하나씩 본다. 사용 중이면 건너뛰고, free인데 작으면 **바로 다음 블록이 free인지 보고 합친다**(merge). 맞는 것을 찾으면 남는 부분이 `TX_BYTE_BLOCK_MIN`(20 B) 이상일 때만 split한다.
- **release는 상태 word를 FREE로 바꾸고 검색 포인터를 당길 뿐 합치지 않는다.** 병합은 다음 검색이 그 자리를 지나갈 때 일어난다. 그래서 release는 O(1)이고 alloc은 블록 수(fragments)에 비례한다.
- 검색 중 **블록 하나 볼 때마다 인터럽트를 잠깐 켠다**(`TX_RESTORE` / `TX_DISABLE`). 그 사이 다른 스레드가 풀을 건드리면 `tx_byte_pool_owner`가 바뀐 것을 보고 검색을 처음부터 다시 한다. 인터럽트 지연은 짧게 유지하지만 alloc 시간은 더 비결정적이 된다.
- 대기자가 있을 때 release는 대기자마다 `_tx_byte_pool_search()`를 돌려 본다(`tx_byte_release.c`) — **release도 길어질 수 있다.**
- ISR: `_txe_byte_allocate()`는 ISR에서 부르면 `TX_NO_WAIT`라도 `TX_CALLER_ERROR`(0x13)를 돌려준다. `tx_byte_release()`도 같다.

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">ThreadX byte pool — 모든 블록을 주소 순서로 훑는 first-fit</text>
<rect x="20" y="60" width="100" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="70" y="82" font-size="12" text-anchor="middle">A 사용 중</text><text x="70" y="100" font-size="12" text-anchor="middle">64 B</text>
<rect x="130" y="60" width="100" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="180" y="82" font-size="12" text-anchor="middle">B free</text><text x="180" y="100" font-size="12" text-anchor="middle">64 B</text>
<rect x="240" y="60" width="100" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="290" y="82" font-size="12" text-anchor="middle">C free</text><text x="290" y="100" font-size="12" text-anchor="middle">64 B</text>
<rect x="350" y="60" width="100" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="400" y="82" font-size="12" text-anchor="middle">D 사용 중</text><text x="400" y="100" font-size="12" text-anchor="middle">64 B</text>
<rect x="460" y="60" width="200" height="50" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="560" y="82" font-size="12" text-anchor="middle">E free</text><text x="560" y="100" font-size="12" text-anchor="middle">2000 B</text>
<path d="M60,60 Q120,34 168,58" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
<path d="M170,60 Q230,34 278,58" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
<path d="M280,60 Q340,34 388,58" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
<path d="M390,60 Q450,34 498,58" fill="none" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="20" y="44" font-size="12">next (8 B 헤더의 첫 word)</text>
<rect x="122" y="140" width="116" height="30" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="180" y="160" font-size="12" text-anchor="middle">search 포인터</text>
<line x1="180" y1="139" x2="180" y2="113" stroke="currentColor" marker-end="url(#ah3)"/>
<text x="252" y="152" font-size="12">alloc(100): B 작음 → 오른쪽 C도 free → B+C 병합(128 B)</text>
<text x="250" y="170" font-size="12">→ 맞으면 split, 아니면 D 건너뛰고 E로 (블록마다 IRQ 잠깐 허용)</text>
<text x="20" y="212" font-size="12">release(x): 상태 word = 0xFFFFEEEE 로만 바꾼다 → O(1), 병합 안 함</text>
<text x="20" y="234" font-size="12">alloc 비용 ∝ 검색 포인터 뒤에 늘어선 블록 수 (사용 중 블록도 하나씩 지나간다)</text>
<text x="20" y="256" font-size="12">대기자가 있으면 release가 대기자마다 검색을 다시 돈다 → release도 길어질 수 있다</text>
</svg>
```
그림 3 — byte pool의 검색. 리스트는 사용 중 블록까지 포함한 주소 순서 리스트이고, 병합은 검색이 지나가면서 한다. 그래서 할당 시간은 단편화 정도(fragments)에 비례한다.

### 6.3 예제 6 — block pool 헤더, timeout, hand-off, byte pool 검색 · 병합 카운터, ISR 규칙

무엇을 확인하는 코드인지: 1024 B / 32 B block pool의 블록 수와 stride, 헤더 word가 정말 풀 주소인지, 빈 풀에서 50 tick 대기 만료와 다른 스레드의 반납에 의한 hand-off, 4 KiB byte pool을 64 B × 40으로 잘게 만든 뒤 홀수 · 짝수 반납 → 100 B · 2000 B 할당 때 검색 · 병합 · split 카운터와 cycles(`TX_BYTE_POOL_ENABLE_PERFORMANCE_INFO`로 커널을 컴파일), 그리고 TIMER0 ISR에서 두 API를 불렀을 때의 반환값.

```c
/* F2 ex6 — ThreadX 6.5.2: block pool (fixed, O(1), 4 B header holding the pool pointer) vs
 * byte pool (first-fit walk, merges only while searching). Exhaustion + timeout, ISR rules. */
#include "tx_api.h"
#include "board.h"
#define FPGA_CNT (*(volatile uint32_t *)0x40028018u)
static TX_THREAD t1, t2; static ULONG s1[512], s2[256];
static TX_BLOCK_POOL bp; static ULONG bp_mem[1024 / 4];
static TX_BYTE_POOL yp;  static ULONG yp_mem[4096 / 4];
static VOID *held[32], *y[40];
static volatile UINT isr_blk = 99, isr_byte = 99;

void TIMER0_IRQHandler(void)
{
	TIMER_INTCLR(TIMER0_BASE) = 1; TIMER_CTRL(TIMER0_BASE) = 0;
	VOID *p;
	isr_byte = tx_byte_allocate(&yp, &p, 16, TX_NO_WAIT);   /* not allowed from ISR */
	isr_blk = tx_block_allocate(&bp, &p, TX_NO_WAIT);        /* allowed with TX_NO_WAIT */
	if (isr_blk == TX_SUCCESS) tx_block_release(p);
}
static void byte_stat(const char *tag, ULONG us)
{
	ULONG avail, frag, a, r, srch, mrg, spl, su, to;
	tx_byte_pool_info_get(&yp, TX_NULL, &avail, &frag, TX_NULL, TX_NULL, TX_NULL);
	tx_byte_pool_performance_info_get(&yp, &a, &r, &srch, &mrg, &spl, &su, &to);
	board_printf_raw("%s: %u cycles | avail=%u fragments=%u | total searched=%u merges=%u splits=%u\n",
	                 tag, (unsigned)us, (unsigned)avail, (unsigned)frag, (unsigned)srch, (unsigned)mrg, (unsigned)spl);
}
static void releaser(ULONG in) { tx_thread_sleep(30); tx_block_release(held[in]); }
static void demo(ULONG in)
{
	(void)in; ULONG avail, total; VOID *p; UINT st; ULONG t;
	tx_block_pool_create(&bp, "bp", 32, bp_mem, sizeof bp_mem);
	tx_block_pool_info_get(&bp, TX_NULL, &avail, &total, TX_NULL, TX_NULL, TX_NULL);
	int n = 0; while (tx_block_allocate(&bp, &held[n], TX_NO_WAIT) == TX_SUCCESS) n++;
	board_printf_raw("block pool 1024 B / 32 B blocks: total=%u, stride=%u B, header word=0x%x (&bp=0x%x)\n",
	                 (unsigned)total, (unsigned)((UCHAR *)held[1] - (UCHAR *)held[0]),
	                 (unsigned)((ULONG *)held[0])[-1], (unsigned)(ULONG)&bp);
	t = tx_time_get(); st = tx_block_allocate(&bp, &p, 50);
	board_printf_raw("empty, wait 50 ticks -> 0x%x after %u ms\n", st, (unsigned)(tx_time_get() - t));
	tx_thread_create(&t2, "rel", releaser, 5, s2, sizeof s2, 4, 4, TX_NO_TIME_SLICE, TX_AUTO_START);
	t = tx_time_get(); st = tx_block_allocate(&bp, &p, 200);
	board_printf_raw("wait 200 -> 0x%x after %u ms, got held[5]? %s\n", st, (unsigned)(tx_time_get() - t),
	                 p == held[5] ? "yes" : "no");
	tx_byte_pool_create(&yp, "yp", yp_mem, sizeof yp_mem);
	byte_stat("byte pool created      ", 0);
	for (int i = 0; i < 40; i++) tx_byte_allocate(&yp, &y[i], 64, TX_NO_WAIT);
	for (int i = 0; i < 40; i += 2) tx_byte_release(y[i]);          /* every other one */
	byte_stat("40 x 64, freed evens   ", 0);
	t = FPGA_CNT; tx_byte_allocate(&yp, &p, 100, TX_NO_WAIT); t = FPGA_CNT - t;
	byte_stat("alloc 100 (holes 64)   ", t);
	tx_byte_release(p);
	for (int i = 1; i < 40; i += 2) tx_byte_release(y[i]);          /* everything free now */
	byte_stat("all released           ", 0);
	t = FPGA_CNT; tx_byte_allocate(&yp, &p, 2000, TX_NO_WAIT); t = FPGA_CNT - t;
	byte_stat("alloc 2000 (merges)    ", t);
	t = FPGA_CNT; tx_byte_release(p); tx_byte_allocate(&yp, &p, 2000, TX_NO_WAIT); t = FPGA_CNT - t;
	byte_stat("release+alloc 2000 again", t);
	tx_block_release(held[0]);                                       /* one free block for the ISR */
	TIMER_RELOAD(TIMER0_BASE) = 2500; TIMER_CTRL(TIMER0_BASE) = 9;   /* one IRQ after 0.1 ms */
	NVIC_IPR(8) = 0x80; NVIC_ISER0 = 1u << 8;
	tx_thread_sleep(2);
	board_printf_raw("from ISR: tx_byte_allocate -> 0x%x, tx_block_allocate -> 0x%x\n", isr_byte, isr_blk);
	board_exit(0);
}
void tx_application_define(void *first_unused)
{
	(void)first_unused;
	tx_thread_create(&t1, "demo", demo, 0, s1, sizeof s1, 3, 3, TX_NO_TIME_SLICE, TX_AUTO_START);
}
int main(void) { tx_kernel_enter(); return 0; }
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; $S/tx.sh ex6 $S/ex6/main.c "-DTX_BYTE_POOL_ENABLE_PERFORMANCE_INFO"'
```

```text
block pool 1024 B / 32 B blocks: total=28, stride=36 B, header word=0x200015dc (&bp=0x200015dc)
empty, wait 50 ticks -> 0x10 after 50 ms
wait 200 -> 0x0 after 30 ms, got held[5]? yes
byte pool created      : 0 cycles | avail=4088 fragments=2 | total searched=0 merges=0 splits=0
40 x 64, freed evens   : 0 cycles | avail=2648 fragments=42 | total searched=40 merges=0 splits=40
alloc 100 (holes 64)   : 708 cycles | avail=2540 fragments=43 | total searched=81 merges=0 splits=41
all released           : 0 cycles | avail=4088 fragments=43 | total searched=81 merges=0 splits=41
alloc 2000 (merges)    : 1037 cycles | avail=2072 fragments=16 | total searched=109 merges=27 splits=41
release+alloc 2000 again: 134 cycles | avail=2072 fragments=16 | total searched=110 merges=27 splits=41
from ISR: tx_byte_allocate -> 0x13, tx_block_allocate -> 0x0
[board_exit] code=0
[rc=0]
```

출력에서 볼 것:

- block pool: 28블록, stride 36 B(32 + 헤더 4). 할당된 블록 바로 앞 word가 `&bp`(0x200015dc)다. 그래서 release에 풀 인자가 없다.
- 빈 풀에서 50 tick(1 kHz) 대기 → `TX_NO_MEMORY`(0x10), 정확히 50 ms. 우선순위 4 releaser가 30 ms 뒤 `held[5]`를 반납하자 우선순위 3 대기자가 **바로 그 블록**을 받았다(hand-off).
- byte pool 생성 직후 fragments=2(전체 free 블록 + 끝 표지), 쓸 수 있는 바이트 4088 B. 64 B × 40 할당 + 짝수 반납 뒤 fragments=42, avail 2648.
- 64 B 구멍 20개가 있을 때 100 B 할당: 검색 41블록(81 − 40), **708 cycles**. 구멍마다 "작다 → 오른쪽 이웃은 사용 중 → 넘어감"을 반복했다. 병합 0.
- 전부 반납해도 **fragments=43 그대로** — release는 합치지 않는다. 다음 2000 B 할당이 검색하면서 27번 병합해 1037 cycles가 걸렸고, fragments가 16으로 줄었다. 같은 2000 B를 반납하고 다시 받으면(이미 병합된 상태) 134 cycles.
- ISR에서 `tx_byte_allocate` → `TX_CALLER_ERROR`(0x13), `tx_block_allocate`(TX_NO_WAIT) → 0 성공. **byte pool은 ISR 금지, block pool은 ISR 가능.**

---

## 7. 측정 — 같은 랜덤 작업량에서 9개 할당기

### 7.1 무엇을 어떻게 재나

공용 헤더 `bench.h` 하나를 FreeRTOS · Zephyr · ThreadX 앱이 똑같이 include한다.

- **작업량**: 슬롯 256개. 매 op마다 xorshift32로 슬롯 하나를 고르고, 비어 있으면 16 ~ 256 B 랜덤 크기를 할당, 차 있으면 해제한다. 8단계(phase)로 나눠 단계 p에서는 앞쪽 S = 32(p+1)개 슬롯만 쓴다 → 살아 있는 바이트가 단계마다 늘어난다. 단계당 1500 op, 합계 12000 op. 시드가 같으니 **할당 실패가 처음 나기 전까지는 모든 할당기가 완전히 같은 요청열**을 받는다.
- **점유율(x축)**: 할당 직전 "살아 있는 요청 바이트 합 / 16 KiB". 할당기와 무관한 작업량의 상태다. 힙(heap_4, `k_heap`, `tx_byte`)은 16 KiB 영역을 받고, 풀은 256 B 블록 256개(64 KiB)를 받아 **절대 고갈되지 않게** 했다. 풀이 "메모리를 더 써서 결정성을 산다"는 사실은 §7.4에서 따로 잰다.
- **시간**: 각 op 하나를 PRIMASK로 IRQ를 막고 FPGAIO 카운터 두 번 읽기 사이에 넣는다. tick ISR이 샘플 안에 끼지 않는다. 카운터 두 번 읽는 비용은 먼저 재서 뺀다. 할당기 자신의 락(BASEPRI 올리기, ThreadX의 PRIMASK 저장 · 복원)은 그대로 들어간다.
- **결과**: 1 cycle 폭 히스토그램에서 p50 / p99 / p99.9 / max, 점유율 10% 구간별 최악값(`D` 줄), 실패 수와 첫 실패 때 점유율.

```c
/* F2 common: the SAME random alloc/free workload for every allocator (FreeRTOS, Zephyr, ThreadX).
 * Define PR(...) (printf-like, %u %s only) before including. Each op is timed with IRQs off
 * using the MPS2 FPGAIO 25 MHz counter, so ticks/ISRs never land inside a sample. */
#pragma once
#include <stdint.h>
#include <stddef.h>
#define FPGA_CNT (*(volatile uint32_t *)0x40028018u)  /* 25 MHz free-running counter, 40 ns */
#define NSLOT  256
#define NPHASE 8                  /* phase p uses S = 32*(p+1) slots -> occupancy ramps up */
#define OPS    1500               /* ops per phase */
#define HMAX   2048               /* 1-count histogram bins, last = overflow */
#define ARENA  16384u             /* x-axis: live requested bytes / 16 KiB (same for all) */

typedef void *(*alloc_fn)(void *ctx, size_t n);
typedef void (*free_fn)(void *ctx, void *p);
typedef void (*probe_fn)(void *ctx, uint32_t phase);

static uint32_t rs;
static inline uint32_t rnd(void) { rs ^= rs << 13; rs ^= rs >> 17; rs ^= rs << 5; return rs; }
static void *slot[NSLOT];
static uint16_t ssz[NSLOT];
static uint16_t ha[HMAX], hf[HMAX];
static uint32_t dmax[11];

static inline uint32_t irq_off(void)
{
	uint32_t m;
	__asm volatile("mrs %0, primask\n cpsid i" : "=r"(m) : : "memory");
	return m;
}
static inline void irq_restore(uint32_t m) { __asm volatile("msr primask, %0" : : "r"(m) : "memory"); }

static uint32_t pct(const uint16_t *h, uint32_t n, uint32_t pm)   /* pm = per-mille */
{
	uint32_t want = (n * pm + 999u) / 1000u, acc = 0;
	for (uint32_t v = 0; v < HMAX; v++) { acc += h[v]; if (acc >= want && h[v]) return v; }
	return HMAX - 1;
}
static uint32_t hmaxv(const uint16_t *h) { for (int v = HMAX - 1; v > 0; v--) if (h[v]) return v; return 0; }

static void bench(const char *name, alloc_fn a, free_fn f, void *ctx, uint32_t maxsz, probe_fn pr)
{
	uint32_t ovh = 0xffffffffu, na = 0, nf = 0, fail = 0, ffocc = 0, live = 0;
	for (int i = 0; i < 64; i++) {             /* cost of the timing window itself */
		uint32_t m = irq_off(), t0 = FPGA_CNT, t1 = FPGA_CNT; irq_restore(m);
		if (t1 - t0 < ovh) ovh = t1 - t0;
	}
	rs = 0x2545F491u;
	for (int i = 0; i < NSLOT; i++) slot[i] = 0;
	for (int i = 0; i < HMAX; i++) ha[i] = hf[i] = 0;
	for (int i = 0; i < 11; i++) dmax[i] = 0;
	for (uint32_t p = 0; p < NPHASE; p++) {
		uint32_t S = 32 * (p + 1);
		for (uint32_t i = 0; i < OPS; i++) {
			uint32_t k = rnd() % S, m, t0, t1, d;
			if (!slot[k]) {
				uint32_t n = 16 + rnd() % (maxsz - 15), occ = live * 10 / ARENA;
				m = irq_off(); t0 = FPGA_CNT; void *q = a(ctx, n); t1 = FPGA_CNT; irq_restore(m);
				d = t1 - t0 - ovh; ha[d < HMAX ? d : HMAX - 1]++; na++;
				if (occ > 10) occ = 10;
				if (d > dmax[occ]) dmax[occ] = d;
				if (!q) { if (!fail++) ffocc = live * 100 / ARENA; }
				else { slot[k] = q; ssz[k] = (uint16_t)n; live += n; }
			} else {
				m = irq_off(); t0 = FPGA_CNT; f(ctx, slot[k]); t1 = FPGA_CNT; irq_restore(m);
				d = t1 - t0 - ovh; hf[d < HMAX ? d : HMAX - 1]++; nf++;
				live -= ssz[k]; slot[k] = 0;
			}
		}
		if (pr) pr(ctx, p);
	}
	for (int i = 0; i < NSLOT; i++) if (slot[i]) { f(ctx, slot[i]); slot[i] = 0; }
	PR("R %s alloc n=%u p50=%u p99=%u p999=%u max=%u | free n=%u p50=%u p99=%u max=%u | fail=%u at=%u%%\n",
	   name, (unsigned)na, (unsigned)pct(ha, na, 500), (unsigned)pct(ha, na, 990),
	   (unsigned)pct(ha, na, 999), (unsigned)hmaxv(ha), (unsigned)nf, (unsigned)pct(hf, nf, 500),
	   (unsigned)pct(hf, nf, 990), (unsigned)hmaxv(hf), (unsigned)fail, (unsigned)ffocc);
	PR("D %s", name);
	for (int i = 0; i < 11; i++) PR(" %u", (unsigned)dmax[i]);
	PR("\n");
	PR("V %s", name);                          /* sparse alloc histogram value:count */
	for (uint32_t v = 0; v < HMAX; v++) if (ha[v]) PR(" %u:%u", (unsigned)v, (unsigned)ha[v]);
	PR("\n");
}
```

(공용 헤더라 83줄이다. 각 앱의 본체는 아래처럼 짧다.)

### 7.2 예제 7 · 8 · 9 — 세 커널에서 실행

무엇을 확인하는 코드인지(예제 7): FreeRTOS에서 직접 만든 풀(a), 포인터 큐 풀(c), heap_4(16 KiB)를 같은 작업량으로 잰다. 모든 태스크 · 큐를 정적으로 만들어 heap_4가 빈 상태에서 시작하게 했고, 단계마다 `vPortGetHeapStats()`로 free 블록 수 · 가장 큰 free 블록을 찍는다.

```c
/* F2 ex7 — FreeRTOS: same random workload (bench.h) on (1) free-list pool, (2) queue-of-pointers
 * pool, (3) heap_4 (16 KiB). All tasks/queues static, so heap_4 starts empty. */
#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"
#include "board.h"
#include "pool.h"
#define PR board_printf_raw
#include "bench.h"

#define NB 256
static uint32_t pmem[NB][256 / 4];
static pool_t pl;
static QueueHandle_t q;
static volatile uint32_t mfail;
void vApplicationMallocFailedHook(void) { mfail++; }

static void *a_pool(void *c, size_t n) { (void)n; return pool_alloc(c); }
static void  f_pool(void *c, void *p) { pool_free(c, p); }
static void *a_q(void *c, size_t n) { (void)n; void *b; return xQueueReceive(c, &b, 0) ? b : NULL; }
static void  f_q(void *c, void *p) { xQueueSend(c, &p, 0); }
static void *a_h4(void *c, size_t n) { (void)c; return pvPortMalloc(n); }
static void  f_h4(void *c, void *p) { (void)c; vPortFree(p); }
static void probe_h4(void *c, uint32_t ph)
{
	(void)c; HeapStats_t s; vPortGetHeapStats(&s);
	PR("P heap_4 phase=%u S=%u free_blocks=%u largest=%u free=%u min_ever=%u\n", (unsigned)ph,
	   (unsigned)(32 * (ph + 1)), (unsigned)s.xNumberOfFreeBlocks, (unsigned)s.xSizeOfLargestFreeBlockInBytes,
	   (unsigned)s.xAvailableHeapSpaceInBytes, (unsigned)s.xMinimumEverFreeBytesRemaining);
}
static void run(void *pv)
{
	(void)pv;
	PR("heap_4 total=%u free at start=%u\n", (unsigned)configTOTAL_HEAP_SIZE, (unsigned)xPortGetFreeHeapSize());
	void *a = pvPortMalloc(32), *b = pvPortMalloc(32), *c = pvPortMalloc(100), *d = pvPortMalloc(100);
	PR("heap_4 stride: 32 B -> %u B, 100 B -> %u B\n", (unsigned)((char *)b - (char *)a), (unsigned)((char *)d - (char *)c));
	vPortFree(a); vPortFree(b); vPortFree(c); vPortFree(d);
	bench("fr_pool", a_pool, f_pool, &pl, 256, NULL);
	bench("fr_queue", a_q, f_q, q, 256, NULL);
	bench("heap_4", a_h4, f_h4, NULL, 256, probe_h4);
	PR("heap_4 malloc-failed hook calls=%u\n", (unsigned)mfail);
	board_exit(0);
}
int main(void)
{
	static StaticQueue_t qs; static uint8_t qbuf[NB * sizeof(void *)];
	pool_init(&pl, pmem, 256, NB);
	q = xQueueCreateStatic(NB, sizeof(void *), qbuf, &qs);
	for (int i = 0; i < NB; i++) { void *b = pmem[i]; xQueueSend(q, &b, 0); }
	static StaticTask_t tcb; static StackType_t stk[1024];
	xTaskCreateStatic(run, "bench", 1024, NULL, 2, stk, &tcb);
	vTaskStartScheduler();
	return 0;
}
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; TO=60 $S/fr.sh ex7 $S/ex7/main.c HEAP=heap_4 \
  "EXTRA_CFLAGS=-I$S/common -DconfigTOTAL_HEAP_SIZE=16384"'
```

```text
heap_4 total=16384 free at start=0
heap_4 stride: 32 B -> 40 B, 100 B -> 112 B
R fr_pool alloc n=6063 p50=38 p99=39 p999=39 max=39 | free n=5937 p50=35 p99=36 max=36 | fail=0 at=0%
D fr_pool 39 39 39 39 39 39 39 39 39 39 39
V fr_pool 38:3641 39:2422
R fr_queue alloc n=6063 p50=84 p99=84 p999=84 max=84 | free n=5937 p50=102 p99=102 max=102 | fail=0 at=0%
D fr_queue 84 84 84 84 84 84 84 84 84 84 84
V fr_queue 84:6063
P heap_4 phase=0 S=32 free_blocks=7 largest=12792 free=13576 min_ever=12784
P heap_4 phase=1 S=64 free_blocks=11 largest=10568 free=12048 min_ever=9872
P heap_4 phase=2 S=96 free_blocks=17 largest=7088 free=10256 min_ever=6464
P heap_4 phase=3 S=128 free_blocks=21 largest=2648 free=5720 min_ever=4040
P heap_4 phase=4 S=160 free_blocks=28 largest=488 free=3856 min_ever=1632
P heap_4 phase=5 S=192 free_blocks=25 largest=600 free=3352 min_ever=1040
P heap_4 phase=6 S=224 free_blocks=19 largest=120 free=944 min_ever=944
P heap_4 phase=7 S=256 free_blocks=31 largest=192 free=2216 min_ever=808
R heap_4 alloc n=6242 p50=183 p99=391 p999=435 max=453 | free n=5758 p50=126 p99=195 max=224 | fail=376 at=78%
D heap_4 198 216 243 295 331 375 453 445 356 0 0
V heap_4 116:58 120:70 121:245 123:45 124:12 125:19 128:267 ... (서로 다른 값 238개, 생략)
heap_4 malloc-failed hook calls=376
[board_exit] code=0
[rc=0]
```

무엇을 확인하는 코드인지(예제 8): Zephyr에서 `k_mem_slab`(256 B × 256), `sys_mem_blocks`(같은 크기, 비트맵), `k_heap`(16 KiB). 단계마다 `sys_heap_runtime_stats_get()`. 시작 전에 `k_heap` · `k_malloc` · 100 B 슬랩의 stride도 잰다.

```c
/* F2 ex8 — Zephyr: same random workload (bench.h) on k_mem_slab, sys_mem_blocks (bitmap),
 * and k_heap (sys_heap, 16 KiB). */
#include <zephyr/kernel.h>
#include <zephyr/sys/sys_heap.h>
#include <zephyr/sys/mem_blocks.h>
#include "zf2.h"
#define PR printk
#include "bench.h"

K_MEM_SLAB_DEFINE(slab, 256, 256, 4);
SYS_MEM_BLOCKS_DEFINE(mb, 256, 256, 4);
K_HEAP_DEFINE(kh, 16384);
K_MEM_SLAB_DEFINE(s100, 100, 2, 4);

static void *a_slab(void *c, size_t n) { ARG_UNUSED(n); void *b; return k_mem_slab_alloc(c, &b, K_NO_WAIT) ? NULL : b; }
static void  f_slab(void *c, void *p) { k_mem_slab_free(c, p); }
static void *a_mb(void *c, size_t n) { ARG_UNUSED(n); void *b; return sys_mem_blocks_alloc(c, 1, &b) ? NULL : b; }
static void  f_mb(void *c, void *p) { sys_mem_blocks_free(c, 1, &p); }
static void *a_kh(void *c, size_t n) { return k_heap_alloc(c, n, K_NO_WAIT); }
static void  f_kh(void *c, void *p) { k_heap_free(c, p); }
static void probe_kh(void *c, uint32_t ph)
{
	struct sys_memory_stats st;
	sys_heap_runtime_stats_get(&((struct k_heap *)c)->heap, &st);
	PR("P k_heap phase=%u S=%u free=%u alloc=%u max_alloc=%u\n", (unsigned)ph, (unsigned)(32 * (ph + 1)),
	   (unsigned)st.free_bytes, (unsigned)st.allocated_bytes, (unsigned)st.max_allocated_bytes);
}
int main(void)
{
	PR("hardening level=%d sizeof k_mem_slab=%u sys_mem_blocks_t=%u k_heap=%u\n", CONFIG_SYS_HEAP_HARDENING_LEVEL,
	   (unsigned)sizeof(struct k_mem_slab), (unsigned)sizeof(sys_mem_blocks_t), (unsigned)sizeof(struct k_heap));
	void *a = k_heap_alloc(&kh, 32, K_NO_WAIT), *b = k_heap_alloc(&kh, 32, K_NO_WAIT);
	void *c = k_heap_alloc(&kh, 100, K_NO_WAIT), *d = k_heap_alloc(&kh, 100, K_NO_WAIT);
	PR("k_heap stride: 32 B -> %u B, 100 B -> %u B\n", (unsigned)((char *)b - (char *)a), (unsigned)((char *)d - (char *)c));
	k_heap_free(&kh, a); k_heap_free(&kh, b); k_heap_free(&kh, c); k_heap_free(&kh, d);
	a = k_malloc(32); b = k_malloc(32); c = k_malloc(100); d = k_malloc(100);
	PR("k_malloc stride: 32 B -> %u B, 100 B -> %u B\n", (unsigned)((char *)b - (char *)a), (unsigned)((char *)d - (char *)c));
	k_mem_slab_alloc(&s100, &a, K_NO_WAIT); k_mem_slab_alloc(&s100, &b, K_NO_WAIT);
	PR("k_mem_slab(100 B) stride %u B\n", (unsigned)((char *)b - (char *)a));
	bench("k_mem_slab", a_slab, f_slab, &slab, 256, NULL);
	bench("sys_mem_blocks", a_mb, f_mb, &mb, 256, NULL);
	bench("k_heap", a_kh, f_kh, &kh, 256, probe_kh);
	qemu_exit(0);
	return 0;
}
```

```text
# prj.conf
CONFIG_PRINTK=y
CONFIG_QEMU_ICOUNT_SHIFT=5
CONFIG_SYS_HEAP_RUNTIME_STATS=y
CONFIG_SYS_MEM_BLOCKS=y
CONFIG_MAIN_STACK_SIZE=2048
CONFIG_HEAP_MEM_POOL_SIZE=1024
```

Zephyr의 기본 최적화는 `CONFIG_SIZE_OPTIMIZATIONS=y`(`-Os`)이고 FreeRTOS · ThreadX 빌드는 `-O2`다. 공정하게 비교하려고 **`-O2`(`CONFIG_SPEED_OPTIMIZATIONS=y`)로 한 번, 기본 `-Os`로 한 번** 돌렸다.

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; NORUN=1 $S/zb.sh ex8 ex8_o2 -- -DCONFIG_SPEED_OPTIMIZATIONS=y; \
  ICOUNT=1 /var/rtoslab/bin/rtos-run-mps2 /var/tmp/rtos/F2/ex8_o2/zephyr/zephyr.elf 60; TO=60 $S/zb.sh ex8 ex8 | grep "^R k_heap"'
```

```text
*** Booting Zephyr OS build v4.4.2 ***
hardening level=1 sizeof k_mem_slab=28 sys_mem_blocks_t=16 k_heap=20
k_heap stride: 32 B -> 40 B, 100 B -> 104 B
k_malloc stride: 32 B -> 40 B, 100 B -> 112 B
k_mem_slab(100 B) stride 100 B
R k_mem_slab alloc n=6063 p50=29 p99=29 p999=29 max=29 | free n=5937 p50=20 p99=20 max=20 | fail=0 at=0%
D k_mem_slab 29 29 29 29 29 29 29 29 29 29 29
V k_mem_slab 28:1189 29:4874
R sys_mem_blocks alloc n=6063 p50=127 p99=145 p999=149 max=149 | free n=5937 p50=110 p99=110 max=110 | fail=0 at=0%
D sys_mem_blocks 127 127 132 132 132 138 138 144 144 149 149
V sys_mem_blocks ... (생략)
P k_heap phase=0 S=32 free=13600 alloc=2600 max_alloc=3360
P k_heap phase=1 S=64 free=12028 alloc=4104 max_alloc=6124
P k_heap phase=2 S=96 free=10304 alloc=5768 max_alloc=9252
P k_heap phase=3 S=128 free=6024 alloc=9896 max_alloc=11488
P k_heap phase=4 S=160 free=3304 alloc=12520 max_alloc=13624
P k_heap phase=5 S=192 free=2436 alloc=13332 max_alloc=14664
P k_heap phase=6 S=224 free=1896 alloc=13824 max_alloc=14772
P k_heap phase=7 S=256 free=2540 alloc=13128 max_alloc=14856
R k_heap alloc n=6242 p50=210 p99=243 p999=243 max=243 | free n=5758 p50=155 p99=212 max=212 | fail=372 at=80%
D k_heap 242 242 243 243 243 243 243 243 243 0 0
V k_heap ... (서로 다른 값 49개, 생략)
R k_heap alloc n=6242 p50=406 p99=480 p999=480 max=480 | free n=5758 p50=422 p99=592 max=592 | fail=372 at=80%
```

(마지막 줄이 기본 `-Os` 빌드의 `k_heap`이다. `k_mem_slab`은 -Os에서 28 / 17, `sys_mem_blocks`는 129 ~ 158로 거의 같았다.)

무엇을 확인하는 코드인지(예제 9): ThreadX block pool(256 B × 256)과 byte pool(16 KiB). 단계마다 `tx_byte_pool_info_get()`의 fragments.

```c
/* F2 ex9 — ThreadX 6.5.2: same random workload (bench.h) on tx_block_pool (256 x 256 B)
 * and tx_byte_pool (16 KiB). */
#include "tx_api.h"
#include "board.h"
#define PR board_printf_raw
#include "bench.h"

static TX_THREAD th; static ULONG stk[1024];
static TX_BLOCK_POOL bp; static ULONG bp_mem[256 * (256 + 4) / 4];
static TX_BYTE_POOL yp;  static ULONG yp_mem[16384 / 4];
static TX_BLOCK_POOL b100; static ULONG b100_mem[2 * (100 + 4) / 4];

static void *a_bp(void *c, size_t n) { (void)n; VOID *b; return tx_block_allocate(c, &b, TX_NO_WAIT) ? NULL : b; }
static void  f_bp(void *c, void *p) { (void)c; tx_block_release(p); }
static void *a_yp(void *c, size_t n) { VOID *b; return tx_byte_allocate(c, &b, n, TX_NO_WAIT) ? NULL : b; }
static void  f_yp(void *c, void *p) { (void)c; tx_byte_release(p); }
static void probe_yp(void *c, uint32_t ph)
{
	ULONG avail, frag;
	tx_byte_pool_info_get(c, TX_NULL, &avail, &frag, TX_NULL, TX_NULL, TX_NULL);
	PR("P tx_byte phase=%u S=%u avail=%u fragments=%u\n", (unsigned)ph, (unsigned)(32 * (ph + 1)),
	   (unsigned)avail, (unsigned)frag);
}
static void run(ULONG in)
{
	(void)in; ULONG total; VOID *a, *b, *c, *d;
	tx_block_pool_create(&bp, "bp", 256, bp_mem, sizeof bp_mem);
	tx_block_pool_info_get(&bp, TX_NULL, TX_NULL, &total, TX_NULL, TX_NULL, TX_NULL);
	tx_byte_pool_create(&yp, "yp", yp_mem, sizeof yp_mem);
	tx_byte_allocate(&yp, &a, 32, TX_NO_WAIT); tx_byte_allocate(&yp, &b, 32, TX_NO_WAIT);
	tx_byte_allocate(&yp, &c, 100, TX_NO_WAIT); tx_byte_allocate(&yp, &d, 100, TX_NO_WAIT);
	PR("block pool blocks=%u; byte pool stride: 32 B -> %u B, 100 B -> %u B; sizeof TX_BLOCK_POOL=%u TX_BYTE_POOL=%u\n",
	   (unsigned)total, (unsigned)((UCHAR *)b - (UCHAR *)a), (unsigned)((UCHAR *)d - (UCHAR *)c),
	   (unsigned)sizeof(TX_BLOCK_POOL), (unsigned)sizeof(TX_BYTE_POOL));
	tx_byte_release(a); tx_byte_release(b); tx_byte_release(c); tx_byte_release(d);
	tx_block_pool_create(&b100, "b100", 100, b100_mem, sizeof b100_mem);
	tx_block_allocate(&b100, &a, TX_NO_WAIT); tx_block_allocate(&b100, &b, TX_NO_WAIT);
	PR("block pool(100 B) stride %u B\n", (unsigned)((UCHAR *)b - (UCHAR *)a));
	bench("tx_block", a_bp, f_bp, &bp, 256, NULL);
	bench("tx_byte", a_yp, f_yp, &yp, 256, probe_yp);
	board_exit(0);
}
void tx_application_define(void *first_unused)
{
	(void)first_unused;
	tx_thread_create(&th, "bench", run, 0, stk, sizeof stk, 3, 3, TX_NO_TIME_SLICE, TX_AUTO_START);
}
int main(void) { tx_kernel_enter(); return 0; }
```

```sh
limactl shell elinux -- bash -lc 'S=/Users/donh/.rtos_scratch/F2; TO=60 $S/tx.sh ex9 $S/ex9/main.c ""'
```

```text
block pool blocks=256; byte pool stride: 32 B -> 40 B, 100 B -> 108 B; sizeof TX_BLOCK_POOL=48 TX_BYTE_POOL=52
block pool(100 B) stride 104 B
R tx_block alloc n=6063 p50=32 p99=32 p999=32 max=32 | free n=5937 p50=25 p99=25 max=25 | fail=0 at=0%
D tx_block 32 32 32 32 32 32 32 32 32 32 32
V tx_block 32:6063
P tx_byte phase=0 S=32 avail=13636 fragments=32
P tx_byte phase=1 S=64 avail=12116 fragments=57
P tx_byte phase=2 S=96 avail=10276 fragments=79
P tx_byte phase=3 S=128 avail=5880 fragments=99
P tx_byte phase=4 S=160 avail=5172 fragments=122
P tx_byte phase=5 S=192 avail=3448 fragments=121
P tx_byte phase=6 S=224 avail=1920 fragments=145
P tx_byte phase=7 S=256 avail=1316 fragments=145
R tx_byte alloc n=6243 p50=516 p99=1853 p999=1913 max=1941 | free n=5757 p50=43 p99=44 max=44 | fail=369 at=79%
D tx_byte 422 566 758 960 1092 1315 1676 1940 1941 0 0
V tx_byte ... (서로 다른 값 1555개, 생략)
[board_exit] code=0
[rc=0]
```

### 7.3 결과 — 표와 차트

예제 11(`charts.py`, 아래 §7.6)이 세 출력에서 뽑은 요약이다. 단위는 cycles(25 MHz, 1 cycle = 40 ns).

| 할당기 | alloc p50 | alloc p99 | alloc max | free p50 | free max | 실패 | 첫 실패 점유율 |
|---|---|---|---|---|---|---|---|
| FreeRTOS 직접 만든 풀 (a) | 38 | 39 | 39 | 35 | 36 | 0 | — |
| FreeRTOS 포인터 큐 풀 (c) | 84 | 84 | 84 | 102 | 102 | 0 | — |
| FreeRTOS heap_4 | 183 | 391 | 453 | 126 | 224 | 376 | 78% |
| Zephyr `k_mem_slab` | 29 | 29 | 29 | 20 | 20 | 0 | — |
| Zephyr `sys_mem_blocks` | 127 | 145 | 149 | 110 | 110 | 0 | — |
| Zephyr `k_heap` (-O2) | 210 | 243 | 243 | 155 | 212 | 372 | 80% |
| Zephyr `k_heap` (-Os, 기본) | 406 | 480 | 480 | 422 | 592 | 372 | 80% |
| ThreadX block pool | 32 | 32 | 32 | 25 | 25 | 0 | — |
| ThreadX byte pool | 516 | 1853 | 1941 | 43 | 44 | 369 | 79% |

```svg
<svg viewBox="0 0 680 348" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">alloc 지연 분포 — 막대 p50, 굵은 눈금 p99, 마름모 max (cycles, log)</text>
<line x1="170.0" y1="34" x2="170.0" y2="292" stroke="#888" stroke-dasharray="2 3"/>
<text x="170.0" y="308" font-size="12" text-anchor="middle">10</text>
<line x1="260.5" y1="34" x2="260.5" y2="292" stroke="#888" stroke-dasharray="2 3"/>
<text x="260.5" y="308" font-size="12" text-anchor="middle">30</text>
<line x1="359.7" y1="34" x2="359.7" y2="292" stroke="#888" stroke-dasharray="2 3"/>
<text x="359.7" y="308" font-size="12" text-anchor="middle">100</text>
<line x1="450.3" y1="34" x2="450.3" y2="292" stroke="#888" stroke-dasharray="2 3"/>
<text x="450.3" y="308" font-size="12" text-anchor="middle">300</text>
<line x1="549.5" y1="34" x2="549.5" y2="292" stroke="#888" stroke-dasharray="2 3"/>
<text x="549.5" y="308" font-size="12" text-anchor="middle">1000</text>
<line x1="640.0" y1="34" x2="640.0" y2="292" stroke="#888" stroke-dasharray="2 3"/>
<text x="640.0" y="308" font-size="12" text-anchor="middle">3000</text>
<text x="160" y="57" font-size="12" text-anchor="end">fr_pool</text>
<rect x="170" y="45" width="110.0" height="16" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="282.1" y1="42" x2="282.1" y2="64" stroke="#3f9a6b" stroke-width="3"/>
<path d="M282.1,46 L288.1,53 L282.1,60 L276.1,53 z" fill="#3f9a6b" stroke="currentColor" stroke-width="0.5"/>
<text x="292.1" y="57" font-size="12">39</text>
<text x="160" y="85" font-size="12" text-anchor="end">fr_queue</text>
<rect x="170" y="73" width="175.4" height="16" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="345.4" y1="70" x2="345.4" y2="92" stroke="#3f9a6b" stroke-width="3"/>
<path d="M345.4,74 L351.4,81 L345.4,88 L339.4,81 z" fill="#3f9a6b" stroke="currentColor" stroke-width="0.5"/>
<text x="355.4" y="85" font-size="12">84</text>
<text x="160" y="113" font-size="12" text-anchor="end">heap_4</text>
<rect x="170" y="101" width="239.5" height="16" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<line x1="472.1" y1="98" x2="472.1" y2="120" stroke="#e08a3c" stroke-width="3"/>
<path d="M484.2,102 L490.2,109 L484.2,116 L478.2,109 z" fill="#e08a3c" stroke="currentColor" stroke-width="0.5"/>
<text x="494.2" y="113" font-size="12">453</text>
<text x="160" y="141" font-size="12" text-anchor="end">k_mem_slab</text>
<rect x="170" y="129" width="87.7" height="16" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="257.7" y1="126" x2="257.7" y2="148" stroke="#3f9a6b" stroke-width="3"/>
<path d="M257.7,130 L263.7,137 L257.7,144 L251.7,137 z" fill="#3f9a6b" stroke="currentColor" stroke-width="0.5"/>
<text x="267.7" y="141" font-size="12">29</text>
<text x="160" y="169" font-size="12" text-anchor="end">sys_mem_blocks</text>
<rect x="170" y="157" width="209.4" height="16" fill="#888" fill-opacity="0.35" stroke="#888"/>
<line x1="390.4" y1="154" x2="390.4" y2="176" stroke="#888" stroke-width="3"/>
<path d="M392.6,158 L398.6,165 L392.6,172 L386.6,165 z" fill="#888" stroke="currentColor" stroke-width="0.5"/>
<text x="402.6" y="169" font-size="12">149</text>
<text x="160" y="197" font-size="12" text-anchor="end">k_heap</text>
<rect x="170" y="185" width="250.9" height="16" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="432.9" y1="182" x2="432.9" y2="204" stroke="#4a7bd0" stroke-width="3"/>
<path d="M432.9,186 L438.9,193 L432.9,200 L426.9,193 z" fill="#4a7bd0" stroke="currentColor" stroke-width="0.5"/>
<text x="442.9" y="197" font-size="12">243</text>
<text x="160" y="225" font-size="12" text-anchor="end">k_heap(-Os)</text>
<rect x="170" y="213" width="305.2" height="16" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<line x1="489.0" y1="210" x2="489.0" y2="232" stroke="#4a7bd0" stroke-width="3"/>
<path d="M489.0,214 L495.0,221 L489.0,228 L483.0,221 z" fill="#4a7bd0" stroke="currentColor" stroke-width="0.5"/>
<text x="499.0" y="225" font-size="12">480</text>
<text x="160" y="253" font-size="12" text-anchor="end">tx_block</text>
<rect x="170" y="241" width="95.8" height="16" fill="#3f9a6b" fill-opacity="0.35" stroke="#3f9a6b"/>
<line x1="265.8" y1="238" x2="265.8" y2="260" stroke="#3f9a6b" stroke-width="3"/>
<path d="M265.8,242 L271.8,249 L265.8,256 L259.8,249 z" fill="#3f9a6b" stroke="currentColor" stroke-width="0.5"/>
<text x="275.8" y="253" font-size="12">32</text>
<text x="160" y="281" font-size="12" text-anchor="end">tx_byte</text>
<rect x="170" y="269" width="325.0" height="16" fill="#d0564a" fill-opacity="0.35" stroke="#d0564a"/>
<line x1="600.3" y1="266" x2="600.3" y2="288" stroke="#d0564a" stroke-width="3"/>
<path d="M604.1,270 L610.1,277 L604.1,284 L598.1,277 z" fill="#d0564a" stroke="currentColor" stroke-width="0.5"/>
<text x="594.1" y="281" font-size="12" text-anchor="end">1941</text>
<line x1="170" y1="292" x2="640" y2="292" stroke="currentColor"/>
<text x="405" y="330" font-size="12" text-anchor="middle">cycles @25 MHz (1 cycle = 40 ns, QEMU icount) — 같은 랜덤 작업 12000 op</text>
</svg>
```
그림 4 — alloc 지연 분포(측정). 막대가 p50, 굵은 세로선이 p99, 마름모가 max다. 고정 블록 계열(초록)은 막대 · 선 · 마름모가 한 점에 겹친다 — 분포 폭이 0 ~ 1 cycle이다. log 축이라 tx_byte의 max(1941)는 고정 풀의 약 50 ~ 70배다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="14" text-anchor="middle" font-weight="bold">점유율 구간별 최악 alloc 지연 (cycles)</text>
<line x1="70" y1="280.0" x2="520" y2="280.0" stroke="#888" stroke-dasharray="2 3"/>
<text x="64" y="284.0" font-size="12" text-anchor="end">0</text>
<line x1="70" y1="222.5" x2="520" y2="222.5" stroke="#888" stroke-dasharray="2 3"/>
<text x="64" y="226.5" font-size="12" text-anchor="end">500</text>
<line x1="70" y1="165.0" x2="520" y2="165.0" stroke="#888" stroke-dasharray="2 3"/>
<text x="64" y="169.0" font-size="12" text-anchor="end">1000</text>
<line x1="70" y1="107.5" x2="520" y2="107.5" stroke="#888" stroke-dasharray="2 3"/>
<text x="64" y="111.5" font-size="12" text-anchor="end">1500</text>
<line x1="70" y1="50.0" x2="520" y2="50.0" stroke="#888" stroke-dasharray="2 3"/>
<text x="64" y="54.0" font-size="12" text-anchor="end">2000</text>
<text x="92.5" y="296" font-size="12" text-anchor="middle">0</text>
<text x="137.5" y="296" font-size="12" text-anchor="middle">10</text>
<text x="182.5" y="296" font-size="12" text-anchor="middle">20</text>
<text x="227.5" y="296" font-size="12" text-anchor="middle">30</text>
<text x="272.5" y="296" font-size="12" text-anchor="middle">40</text>
<text x="317.5" y="296" font-size="12" text-anchor="middle">50</text>
<text x="362.5" y="296" font-size="12" text-anchor="middle">60</text>
<text x="407.5" y="296" font-size="12" text-anchor="middle">70</text>
<text x="452.5" y="296" font-size="12" text-anchor="middle">80</text>
<text x="497.5" y="296" font-size="12" text-anchor="middle">90</text>
<line x1="70" y1="280" x2="520" y2="280" stroke="currentColor"/><line x1="70" y1="280" x2="70" y2="40" stroke="currentColor"/>
<text x="295.0" y="316" font-size="12" text-anchor="middle">점유율 구간 시작 % (live 요청 바이트 / 16 KiB)</text>
<polyline points="92.5,231.5 137.5,214.9 182.5,192.8 227.5,169.6 272.5,154.4 317.5,128.8 362.5,87.3 407.5,56.9 452.5,56.8" fill="none" stroke="#d0564a" stroke-width="2"/>
<circle cx="92.5" cy="231.5" r="3" fill="#d0564a"/>
<circle cx="137.5" cy="214.9" r="3" fill="#d0564a"/>
<circle cx="182.5" cy="192.8" r="3" fill="#d0564a"/>
<circle cx="227.5" cy="169.6" r="3" fill="#d0564a"/>
<circle cx="272.5" cy="154.4" r="3" fill="#d0564a"/>
<circle cx="317.5" cy="128.8" r="3" fill="#d0564a"/>
<circle cx="362.5" cy="87.3" r="3" fill="#d0564a"/>
<circle cx="407.5" cy="56.9" r="3" fill="#d0564a"/>
<circle cx="452.5" cy="56.8" r="3" fill="#d0564a"/>
<line x1="532" y1="60" x2="552" y2="60" stroke="#d0564a" stroke-width="2"/>
<text x="558" y="64" font-size="12">tx_byte</text>
<polyline points="92.5,257.2 137.5,255.2 182.5,252.1 227.5,246.1 272.5,241.9 317.5,236.9 362.5,227.9 407.5,228.8 452.5,239.1" fill="none" stroke="#e08a3c" stroke-width="2"/>
<circle cx="92.5" cy="257.2" r="3" fill="#e08a3c"/>
<circle cx="137.5" cy="255.2" r="3" fill="#e08a3c"/>
<circle cx="182.5" cy="252.1" r="3" fill="#e08a3c"/>
<circle cx="227.5" cy="246.1" r="3" fill="#e08a3c"/>
<circle cx="272.5" cy="241.9" r="3" fill="#e08a3c"/>
<circle cx="317.5" cy="236.9" r="3" fill="#e08a3c"/>
<circle cx="362.5" cy="227.9" r="3" fill="#e08a3c"/>
<circle cx="407.5" cy="228.8" r="3" fill="#e08a3c"/>
<circle cx="452.5" cy="239.1" r="3" fill="#e08a3c"/>
<line x1="532" y1="82" x2="552" y2="82" stroke="#e08a3c" stroke-width="2"/>
<text x="558" y="86" font-size="12">heap_4</text>
<polyline points="92.5,224.8 137.5,224.8 182.5,224.8 227.5,224.8 272.5,224.8 317.5,224.8 362.5,224.8 407.5,224.8 452.5,224.8" fill="none" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/>
<circle cx="92.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="137.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="182.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="227.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="272.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="317.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="362.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="407.5" cy="224.8" r="3" fill="#4a7bd0"/>
<circle cx="452.5" cy="224.8" r="3" fill="#4a7bd0"/>
<line x1="532" y1="104" x2="552" y2="104" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="6 4"/>
<text x="558" y="108" font-size="12">k_heap(-Os)</text>
<polyline points="92.5,252.2 137.5,252.2 182.5,252.1 227.5,252.1 272.5,252.1 317.5,252.1 362.5,252.1 407.5,252.1 452.5,252.1" fill="none" stroke="#4a7bd0" stroke-width="2"/>
<circle cx="92.5" cy="252.2" r="3" fill="#4a7bd0"/>
<circle cx="137.5" cy="252.2" r="3" fill="#4a7bd0"/>
<circle cx="182.5" cy="252.1" r="3" fill="#4a7bd0"/>
<circle cx="227.5" cy="252.1" r="3" fill="#4a7bd0"/>
<circle cx="272.5" cy="252.1" r="3" fill="#4a7bd0"/>
<circle cx="317.5" cy="252.1" r="3" fill="#4a7bd0"/>
<circle cx="362.5" cy="252.1" r="3" fill="#4a7bd0"/>
<circle cx="407.5" cy="252.1" r="3" fill="#4a7bd0"/>
<circle cx="452.5" cy="252.1" r="3" fill="#4a7bd0"/>
<line x1="532" y1="126" x2="552" y2="126" stroke="#4a7bd0" stroke-width="2"/>
<text x="558" y="130" font-size="12">k_heap</text>
<polyline points="92.5,265.4 137.5,265.4 182.5,264.8 227.5,264.8 272.5,264.8 317.5,264.1 362.5,264.1 407.5,263.4 452.5,263.4 497.5,262.9" fill="none" stroke="#888" stroke-width="2"/>
<circle cx="92.5" cy="265.4" r="3" fill="#888"/>
<circle cx="137.5" cy="265.4" r="3" fill="#888"/>
<circle cx="182.5" cy="264.8" r="3" fill="#888"/>
<circle cx="227.5" cy="264.8" r="3" fill="#888"/>
<circle cx="272.5" cy="264.8" r="3" fill="#888"/>
<circle cx="317.5" cy="264.1" r="3" fill="#888"/>
<circle cx="362.5" cy="264.1" r="3" fill="#888"/>
<circle cx="407.5" cy="263.4" r="3" fill="#888"/>
<circle cx="452.5" cy="263.4" r="3" fill="#888"/>
<circle cx="497.5" cy="262.9" r="3" fill="#888"/>
<line x1="532" y1="148" x2="552" y2="148" stroke="#888" stroke-width="2"/>
<text x="558" y="152" font-size="12">sys_mem_blocks</text>
<polyline points="92.5,276.7 137.5,276.7 182.5,276.7 227.5,276.7 272.5,276.7 317.5,276.7 362.5,276.7 407.5,276.7 452.5,276.7 497.5,276.7" fill="none" stroke="#3f9a6b" stroke-width="2"/>
<circle cx="92.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="137.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="182.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="227.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="272.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="317.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="362.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="407.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="452.5" cy="276.7" r="3" fill="#3f9a6b"/>
<circle cx="497.5" cy="276.7" r="3" fill="#3f9a6b"/>
<line x1="532" y1="170" x2="552" y2="170" stroke="#3f9a6b" stroke-width="2"/>
<text x="558" y="174" font-size="12">고정 풀 (29~39)</text>
</svg>
```
그림 5 — 점유율 구간별 최악 alloc 지연(측정, `D` 줄). tx_byte는 점유율(= 조각 수)에 비례해 422 → 1941 cycles로 자라고, heap_4는 198 → 453으로 완만히 자란다. `k_heap`은 -O2 242 ~ 243, -Os 480으로 **구간과 무관하게 평평하다**. 고정 풀 셋은 29 ~ 39로 바닥에 붙어 있다. 힙 셋은 80% 구간 넘어서는 실패해서 점이 없다.

### 7.4 객체 하나가 먹는 바이트

```svg
<svg viewBox="0 0 680 335" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" font-size="14" text-anchor="middle" font-weight="bold">객체 하나가 실제로 먹는 바이트 — 요청 32 B(파랑) · 100 B(주황)</text>
<line x1="60" y1="270.0" x2="660" y2="270.0" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="274.0" font-size="12" text-anchor="end">0</text>
<line x1="60" y1="233.3" x2="660" y2="233.3" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="237.3" font-size="12" text-anchor="end">20</text>
<line x1="60" y1="196.7" x2="660" y2="196.7" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="200.7" font-size="12" text-anchor="end">40</text>
<line x1="60" y1="160.0" x2="660" y2="160.0" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="164.0" font-size="12" text-anchor="end">60</text>
<line x1="60" y1="123.3" x2="660" y2="123.3" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="127.3" font-size="12" text-anchor="end">80</text>
<line x1="60" y1="86.7" x2="660" y2="86.7" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="90.7" font-size="12" text-anchor="end">100</text>
<line x1="60" y1="50.0" x2="660" y2="50.0" stroke="#888" stroke-dasharray="2 3"/>
<text x="54" y="54.0" font-size="12" text-anchor="end">120</text>
<line x1="60" y1="211.3" x2="660" y2="211.3" stroke="#4a7bd0" stroke-width="1.5"/>
<line x1="60" y1="86.7" x2="660" y2="86.7" stroke="#e08a3c" stroke-width="1.5"/>
<rect x="70.0" y="211.3" width="21.3" height="58.7" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="80.7" y="207.3" font-size="12" text-anchor="middle">32</text>
<rect x="94.3" y="86.7" width="21.3" height="183.3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="105.0" y="82.7" font-size="12" text-anchor="middle">100</text>
<text x="93.3" y="286" font-size="12" text-anchor="middle">fr_pool</text>
<rect x="136.7" y="204.0" width="21.3" height="66.0" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="147.3" y="200.0" font-size="12" text-anchor="middle">36</text>
<rect x="161.0" y="79.3" width="21.3" height="190.7" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="171.7" y="75.3" font-size="12" text-anchor="middle">104</text>
<text x="160.0" y="300" font-size="12" text-anchor="middle">fr_queue</text>
<rect x="203.3" y="211.3" width="21.3" height="58.7" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="214.0" y="207.3" font-size="12" text-anchor="middle">32</text>
<rect x="227.7" y="86.7" width="21.3" height="183.3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="238.3" y="82.7" font-size="12" text-anchor="middle">100</text>
<text x="226.7" y="286" font-size="12" text-anchor="middle">k_mem_slab</text>
<rect x="270.0" y="211.1" width="21.3" height="58.9" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="280.7" y="207.1" font-size="12" text-anchor="middle">32+</text>
<rect x="294.3" y="86.4" width="21.3" height="183.6" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="305.0" y="82.4" font-size="12" text-anchor="middle">100+</text>
<text x="293.3" y="300" font-size="12" text-anchor="middle">mem_blocks</text>
<rect x="336.7" y="204.0" width="21.3" height="66.0" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="347.3" y="200.0" font-size="12" text-anchor="middle">36</text>
<rect x="361.0" y="79.3" width="21.3" height="190.7" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="371.7" y="75.3" font-size="12" text-anchor="middle">104</text>
<text x="360.0" y="286" font-size="12" text-anchor="middle">tx_block</text>
<rect x="403.3" y="196.7" width="21.3" height="73.3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="414.0" y="192.7" font-size="12" text-anchor="middle">40</text>
<rect x="427.7" y="64.7" width="21.3" height="205.3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="438.3" y="60.7" font-size="12" text-anchor="middle">112</text>
<text x="426.7" y="300" font-size="12" text-anchor="middle">heap_4</text>
<rect x="470.0" y="196.7" width="21.3" height="73.3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="480.7" y="192.7" font-size="12" text-anchor="middle">40</text>
<rect x="494.3" y="79.3" width="21.3" height="190.7" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="505.0" y="75.3" font-size="12" text-anchor="middle">104</text>
<text x="493.3" y="286" font-size="12" text-anchor="middle">k_heap</text>
<rect x="536.7" y="196.7" width="21.3" height="73.3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="547.3" y="192.7" font-size="12" text-anchor="middle">40</text>
<rect x="561.0" y="64.7" width="21.3" height="205.3" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="571.7" y="60.7" font-size="12" text-anchor="middle">112</text>
<text x="560.0" y="300" font-size="12" text-anchor="middle">k_malloc</text>
<rect x="603.3" y="196.7" width="21.3" height="73.3" fill="#4a7bd0" fill-opacity="0.35" stroke="#4a7bd0"/>
<text x="614.0" y="192.7" font-size="12" text-anchor="middle">40</text>
<rect x="627.7" y="72.0" width="21.3" height="198.0" fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c"/>
<text x="638.3" y="68.0" font-size="12" text-anchor="middle">108</text>
<text x="626.7" y="286" font-size="12" text-anchor="middle">tx_byte</text>
<line x1="60" y1="270" x2="660" y2="270" stroke="currentColor"/>
<text x="360" y="322" font-size="12" text-anchor="middle">고정 블록 계열(왼쪽 5개)은 헤더 0~4 B, 힙 계열(오른쪽 4개)은 헤더 4~8 B + 8 B 반올림</text>
</svg>
```
그림 6 — 요청 32 B · 100 B 하나가 실제로 소비하는 바이트(측정한 stride, 풀은 정의대로). 가로선이 요청 크기다. fr_queue는 블록 + 큐 슬롯 4 B, sys_mem_blocks는 블록 + 비트맵 1비트(`+`로 표시), tx_block은 헤더 4 B. 힙 계열은 헤더(4 B 또는 8 B)와 8 B 반올림이 붙는다. `k_malloc`은 100 B에서 힙 포인터 4 B 때문에 한 청크 더 먹는다.

여기에 **풀의 진짜 메모리 비용**을 더해야 공정하다. 이 작업량(16 ~ 256 B)을 고정 풀 하나로 받으려면 블록이 256 B여야 하고, 같은 작업량을 실패 없이 받으려면 블록 수가 동시 생존 최대치(이 실험에서 256)여야 했다 — **64 KiB**. 힙 셋은 16 KiB로 78 ~ 80% 점유까지 버텼다. 고정 풀은 메모리를 4배 써서 "실패 0 + 상수 시간"을 샀다. 크기 분포가 넓으면 §3의 크기 등급으로 이 차이를 줄인다.

제어 구조 크기(측정): `pool_t` 24 B, `StaticQueue_t` 84 B, `struct k_mem_slab` 28 B(+4 B TRACE), `sys_mem_blocks_t` 16 B(+ 비트맵), `struct k_heap` 20 B(+ 힙 영역 안 chunk 0, 2 KiB 힙에서 64 B), `TX_BLOCK_POOL` 48 B, `TX_BYTE_POOL` 52 B.

### 7.5 결과 읽기

1. **고정 블록 풀은 "분포"가 없다.** `k_mem_slab` 6063번 할당이 28 또는 29 cycles 두 값뿐, ThreadX block pool은 6063번 전부 32, 포인터 큐 풀도 전부 84. 점유율 0%에서도 95%에서도 같다. **"O(1)"이 실측으로 이렇게 보인다.** 지터를 걱정할 이유가 없다는 것이 제어 루프 · ISR에서 풀을 쓰는 이유다.
2. **`k_heap`(sys_heap)은 "느리지만 상한이 있는 힙"이다.** 중앙값은 heap_4보다 크지만(210 vs 183) 최악값이 점유율과 무관하게 243에서 멈춘다. 서로 다른 지연 값이 49개뿐이다(heap_4 238개, tx_byte 1555개). bucket 구조(그림 2) 덕분이다. 대가는 단편화: `ALLOC_LOOPS=3`에서 못 찾으면 더 큰 청크를 쪼개므로, 실패 시작점(80%)은 heap_4(78%)와 비슷하다.
3. **heap_4의 최악값은 free 블록 수를 따라간다.** heap_4 alloc은 주소 순 free 리스트를 first-fit으로 훑는다(F1). 이 작업량에서는 free 블록이 최대 31개라 최악이 453에 머물렀지만, 작은 조각이 수백 개 생기는 패턴이면 더 커진다. 상한은 "free 블록 수의 상한"을 증명할 수 있을 때만 말할 수 있다.
4. **ThreadX byte pool은 최악이 가장 크고 점유율에 정비례한다.** 사용 중 블록까지 하나씩 지나가는 검색이라 fragments 145개에서 1941 cycles(78 µs @ 25 MHz). 대신 release는 43 cycles로 거의 상수 — 병합을 alloc 쪽으로 미뤘기 때문이다. 이 측정은 IRQ를 막고 쟀다. 실제 커널은 검색 중 블록마다 인터럽트를 켜 주므로 **인터럽트 지연**은 짧지만, 그 사이 다른 스레드가 풀을 건드리면 검색을 처음부터 다시 하므로 **alloc 시간**은 이 측정보다 더 길어질 수 있다.
5. **단편화 실패**: heap_4는 마지막 단계에서 free가 2216 B인데 가장 큰 free 블록이 192 B였다(`P heap_4 phase=7`). 256 B 요청은 실패한다. 고정 풀에는 이 실패 양식이 없다.
6. **`-Os`가 `sys_heap`을 2배 느리게 한다.** -Os에서는 필드 접근 함수 `chunk_field()` · `chunk_set()`이 인라인되지 않고 함수로 남는다(§10.2의 `nm` 출력에 22 B짜리 함수로 보인다). AUTO 모드라 접근마다 "큰 힙인가" 분기도 탄다. `k_mem_slab`은 포인터 두 번 옮기기라 -Os 영향이 없다. Zephyr 프로젝트에서 힙이 핫 경로에 있으면 `-O2`나 `CONFIG_SYS_HEAP_SMALL_ONLY=y`를 검토할 만하다(SMALL_ONLY 효과는 이 노트에서 재지 않았다).
7. **FreeRTOS 풀(38)과 `k_mem_slab`(29)의 차이**는 임계 구역 비용이다. `taskENTER_CRITICAL()`은 `vPortEnterCritical()` 함수 호출 + 중첩 카운터 + (첫 진입이면) VECTACTIVE assert를 하고, Zephyr spinlock은 단일 코어에서 BASEPRI 올리기 인라인이다. FreeRTOS도 `-Os`로 다시 재 보니 풀 38 / 큐 87 / heap_4 183으로 거의 같았다.

hardening 단계별로 `k_heap`도 재 봤다(-Os 기본 빌드에서 레벨만 바꿈).

| `CONFIG_SYS_HEAP_HARDENING_*` | alloc p50 / max | free p50 / max | 실패 |
|---|---|---|---|
| NONE (0) | 406 / 480 | 388 / 557 | 372 |
| BASIC (1, 기본) | 406 / 480 | 422 / 592 | 372 |
| FULL (3, 청크마다 8 B canary) | 599 / 697 | 658 / 973 | 429 |

BASIC의 비용은 free에 30 ~ 35 cycles로 작다 — 양산 빌드에 남겨 둘 만하다. FULL은 alloc · free 모두 1.5배쯤 비싸지고 청크마다 8 B가 붙어 같은 16 KiB에서 실패가 15% 늘었다(429 vs 372). 개발 · 퍼징 빌드용이다.

### 7.6 예제 11 — 출력을 읽어 표와 차트를 만드는 Python

무엇을 확인하는 코드인지: 위 세 출력 파일의 `R` · `D` 줄을 파싱해 §7.3 표를 찍고, 그림 4 ~ 6의 SVG 좌표를 계산한다(앞부분만 싣는다).

```python
#!/usr/bin/env python3
"""F2 charts: parse bench outputs (R/D lines) -> 3 measured SVG charts + a summary table."""
import math, re, sys
D = sys.argv[1] if len(sys.argv) > 1 else "/var/tmp/rtos/F2"
files = {"fr": "ex7.out", "zo2": "ex8_o2.out", "zos": "ex8.out", "tx": "ex9.out"}
R, DEC = {}, {}
for tag, fn in files.items():
    for ln in open(f"{D}/{fn}", errors="replace"):
        m = re.match(r"R (\S+) alloc n=(\d+) p50=(\d+) p99=(\d+) p999=(\d+) max=(\d+) \| free n=(\d+) p50=(\d+) p99=(\d+) max=(\d+) \| fail=(\d+) at=(\d+)%", ln)
        if m:
            name = m.group(1) + ("(-Os)" if tag == "zos" else "")
            R[name] = [int(x) for x in m.groups()[1:]]
        m = re.match(r"D (\S+) ((?:\d+ ?){11})", ln)
        if m:
            name = m.group(1) + ("(-Os)" if tag == "zos" else "")
            DEC[name] = [int(x) for x in m.group(2).split()]
# ... (log 축 막대 차트, 점유율 구간 선 차트, stride 막대 차트 — 좌표 계산 후 SVG 문자열로 저장)
```

```sh
limactl shell elinux -- python3 /Users/donh/.rtos_scratch/F2/py/charts.py
```

```text
| allocator | alloc p50 | p99 | max | free p50 | free max | fail | first fail at |
| fr_pool | 38 | 39 | 39 | 35 | 36 | 0 | 0% |
| fr_queue | 84 | 84 | 84 | 102 | 102 | 0 | 0% |
| heap_4 | 183 | 391 | 453 | 126 | 224 | 376 | 78% |
| k_mem_slab | 29 | 29 | 29 | 20 | 20 | 0 | 0% |
| sys_mem_blocks | 127 | 145 | 149 | 110 | 110 | 0 | 0% |
| k_heap | 210 | 243 | 243 | 155 | 212 | 372 | 80% |
| k_heap(-Os) | 406 | 480 | 480 | 422 | 592 | 372 | 80% |
| tx_block | 32 | 32 | 32 | 25 | 25 | 0 | 0% |
| tx_byte | 516 | 1853 | 1941 | 43 | 44 | 369 | 79% |
DEC tx_byte [422, 566, 758, 960, 1092, 1315, 1676, 1940, 1941, 0, 0]
DEC heap_4 [198, 216, 243, 295, 331, 375, 453, 445, 356, 0, 0]
DEC k_heap(-Os) [480, 480, 480, 480, 480, 480, 480, 480, 480, 0, 0]
DEC k_heap [242, 242, 243, 243, 243, 243, 243, 243, 243, 0, 0]
DEC sys_mem_blocks [127, 127, 132, 132, 132, 138, 138, 144, 144, 149, 149]
DEC k_mem_slab [29, 29, 29, 29, 29, 29, 29, 29, 29, 29, 29]
chart C written
```

출력에서 볼 것: `D` 배열의 0은 그 구간에 할당 시도가 없었다는 뜻이다(힙은 80% 근처에서 실패가 시작돼 더 올라가지 못했다). 고정 풀은 실패가 없어 점유율이 100%를 넘는 구간까지 갔다.

---

## 8. 고갈 정책 — 실패를 돌려줄까, 재울까

### 8.1 두 가지 정책

- **즉시 실패(K_NO_WAIT, TX_NO_WAIT, timeout 0)**: ISR과 hard real-time 경로의 기본. 실패하면 무엇을 할지가 설계에 들어 있어야 한다 — 샘플 버리기 + 카운터, 이전 버퍼 재사용, 낮은 우선순위 데이터 드롭(D4 §5의 "큐가 꽉 찼을 때" 정책 다섯 가지가 그대로 적용된다).
- **블로킹 + timeout**: 태스크 문맥에서 "버퍼가 날 때까지 기다리는 게 자연스러운" 곳. 생산자가 소비자보다 빠를 때 자연스러운 **역압(backpressure)**이 된다. timeout은 "이 시간 안에 안 나면 시스템이 이상하다"는 감시값으로 잡는다(무한 대기 금지 — 풀 누수가 데드락처럼 보인다).

### 8.2 커널별 대기자 처리 — 같은 "블로킹 alloc"이 아니다

| | 대기 줄 순서 | 반납 때 | 같은 블록을 받나 | 실측 |
|---|---|---|---|---|
| FreeRTOS 포인터 큐 풀 | 우선순위 순 | `xQueueSend`가 가장 높은 대기자를 깨움, 깨어난 태스크가 receive | 깨어나기 전에 다른 태스크가 가로챌 수 있다(D4 §6.3) | 예제 2: 30 ms에 받음 |
| Zephyr `k_mem_slab` | `wait_q` (우선순위 순) | 첫 대기자에게 **직접 전달**, free_list 안 거침 | 예 | 예제 4: free 직후 `used=4` 유지 |
| Zephyr `k_heap` | `wait_q` | **모두 깨움**(`z_unpend_all`), 각자 재시도 | 아니오 — 작은 요청이 먼저 가져갈 수 있다 | 예제 5: free 1회 230 → 2640 ~ 5996 cycles, 1000 B 대기자 굶음 |
| ThreadX block pool | FIFO (`tx_block_pool_prioritize`로 조정) | 첫 대기자에게 직접 전달 | 예 | 예제 6: `held[5]` 그대로 |
| ThreadX byte pool | FIFO | 대기자마다 검색을 돌려 봄(release가 길어짐) | — | (소스로만 확인) |

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah4" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">반납 한 번이 대기자에게 하는 일</text>
<text x="20" y="52" font-size="13" font-weight="bold">k_mem_slab · tx_block</text>
<rect x="20" y="64" width="110" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="75" y="87" font-size="12" text-anchor="middle">free(blk)</text>
<line x1="131" y1="82" x2="198" y2="82" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="200" y="64" width="190" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="295" y="87" font-size="12" text-anchor="middle">첫 대기자 swap_data = blk</text>
<line x1="391" y1="82" x2="448" y2="82" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="450" y="64" width="210" height="36" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="555" y="87" font-size="12" text-anchor="middle">한 명만 깨어나 바로 사용</text>
<text x="20" y="124" font-size="12">free_list 안 거침 · num_used 변화 없음 · 가로채기 불가 · 비용 = 컨텍스트 스위치 1번</text>
<text x="20" y="166" font-size="13" font-weight="bold">k_heap</text>
<rect x="20" y="178" width="110" height="36" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="75" y="201" font-size="12" text-anchor="middle">free(p)</text>
<line x1="131" y1="196" x2="198" y2="196" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="200" y="178" width="190" height="36" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="295" y="201" font-size="12" text-anchor="middle">z_unpend_all: 전부 깨움</text>
<line x1="391" y1="188" x2="448" y2="172" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="391" y1="196" x2="448" y2="208" stroke="currentColor" marker-end="url(#ah4)"/>
<line x1="391" y1="204" x2="448" y2="244" stroke="currentColor" marker-end="url(#ah4)"/>
<rect x="450" y="156" width="210" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="175" font-size="12" text-anchor="middle">1000 B 재시도 → 실패, 다시 잠듦</text>
<rect x="450" y="194" width="210" height="28" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="555" y="213" font-size="12" text-anchor="middle">200 B 재시도 → 성공</text>
<rect x="450" y="232" width="210" height="28" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
<text x="555" y="251" font-size="12" text-anchor="middle">200 B 재시도 → 실패, 다시 잠듦</text>
<text x="20" y="288" font-size="12">대기자 N명이면 free 한 번에 재시도 N번 + 스위치 여러 번 · 큰 요청은 굶을 수 있다</text>
</svg>
```
그림 7 — 고정 블록 풀은 "한 명에게 직접", `k_heap`은 "모두 깨워 각자 다시"다. 크기가 다른 요청끼리는 누가 받아야 맞는지 커널이 알 수 없기 때문에 생긴 차이다.

실무 결론: **블로킹 대기가 필요한 경로에는 고정 크기 풀을 쓴다.** 가변 크기 힙에 여러 태스크가 블로킹으로 매달리는 설계는 큰 요청 기아와 free 비용 폭증(예제 5)을 함께 부른다.

---

## 9. 무엇을 언제 쓰나

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="ah5" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs>
<text x="340" y="20" font-size="14" text-anchor="middle" font-weight="bold">할당기 고르기 — 위에서 아래로</text>
<rect x="180" y="36" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="61" font-size="13" text-anchor="middle">부팅 때 한 번 잡고 끝인가?</text>
<line x1="501" y1="56" x2="538" y2="56" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="519" y="49" font-size="12" text-anchor="middle">예</text>
<rect x="540" y="36" width="130" height="40" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="605" y="61" font-size="12" text-anchor="middle">정적 배열 (F1)</text>
<line x1="340" y1="77" x2="340" y2="104" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="352" y="96" font-size="12">아니오</text>
<rect x="180" y="106" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="131" font-size="13" text-anchor="middle">크기가 한 종류 또는 몇 종류인가?</text>
<line x1="501" y1="126" x2="538" y2="126" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="519" y="119" font-size="12" text-anchor="middle">예</text>
<rect x="540" y="98" width="130" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
<text x="605" y="120" font-size="12" text-anchor="middle">고정 크기 풀</text>
<text x="605" y="140" font-size="12" text-anchor="middle">(등급 3개 안팎)</text>
<line x1="340" y1="147" x2="340" y2="174" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="352" y="166" font-size="12">아니오</text>
<rect x="180" y="176" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="201" font-size="13" text-anchor="middle">ISR · 제어 루프에서 할당하나?</text>
<line x1="501" y1="196" x2="538" y2="196" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="519" y="189" font-size="12" text-anchor="middle">예</text>
<rect x="540" y="168" width="130" height="56" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
<text x="605" y="190" font-size="12" text-anchor="middle">미리 받아 두기</text>
<text x="605" y="210" font-size="12" text-anchor="middle">또는 풀로 바꾸기</text>
<line x1="340" y1="217" x2="340" y2="244" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="352" y="236" font-size="12">아니오</text>
<rect x="180" y="246" width="320" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="340" y="271" font-size="13" text-anchor="middle">시간 상한을 문서로 증명해야 하나?</text>
<line x1="501" y1="266" x2="538" y2="266" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="519" y="259" font-size="12" text-anchor="middle">예</text>
<rect x="540" y="246" width="130" height="40" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
<text x="605" y="271" font-size="12" text-anchor="middle">전용 k_heap</text>
<line x1="340" y1="287" x2="340" y2="314" stroke="currentColor" marker-end="url(#ah5)"/>
<text x="352" y="306" font-size="12">아니오</text>
<rect x="120" y="316" width="440" height="52" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
<text x="340" y="338" font-size="12" text-anchor="middle">범용 힙: heap_4 · k_malloc · tx_byte_pool</text>
<text x="340" y="358" font-size="12" text-anchor="middle">태스크 문맥, 비실시간 경로(설정 · 로그 · 셸)만, 실패 처리 필수</text>
</svg>
```
그림 8 — 할당기 결정 순서. 대부분의 펌웨어 객체는 위의 두 단계(정적 · 고정 풀)에서 끝나고, 범용 힙까지 내려오는 것은 크기를 예측할 수 없는 비실시간 경로뿐이다.

| 커널 | 고정 크기 풀 | 상한 있는 가변 힙 | 범용 가변 힙 | 블록 안 안 건드림 |
|---|---|---|---|---|
| FreeRTOS | 없음 → 직접 (§2) | 없음 | heap_4 / heap_5 (F1) | 직접 (비트맵) |
| Zephyr | `k_mem_slab` | `k_heap` / `sys_heap` | `k_malloc` (= 시스템 `k_heap`), libc malloc | `sys_mem_blocks` |
| ThreadX | `tx_block_pool` | 없음 | `tx_byte_pool` | 없음 |
| CMSIS-RTOS2 | `osMemoryPoolNew` / `Alloc` / `Free` (A5) | 없음 | 없음 | 없음 |

---

## 10. RTOS 커널 안에서는 무슨 일이 일어나나

### 10.1 소스 지도

| 무엇 | 파일 · 함수 | 핵심 |
|---|---|---|
| Zephyr 슬랩 초기화 | `kernel/mem_slab.c` `create_free_list()`, `k_mem_slab_init()`, `init_mem_slab_obj_core_list()`(정적 슬랩을 부팅 때 초기화) | 뒤에서부터 꿰어 head = 블록 0 |
| Zephyr 슬랩 alloc/free | `k_mem_slab_alloc()`, `k_mem_slab_free()` | spinlock, `z_pend_curr()`, `z_unpend_first_thread()` + `z_thread_return_value_set_with_data()` (직접 전달) |
| Zephyr 힙 | `lib/heap/heap.c` `alloc_chunk()`, `sys_heap_alloc()`, `sys_heap_free()`, `free_chunk()`, `split_chunks()`, `merge_chunks()`; `lib/heap/heap.h` `bucket_idx()`, `bytes_to_chunksz()` | bucket 비트맵 + `__builtin_ctz`, 헤더로 이웃 병합 |
| Zephyr `k_heap` | `kernel/kheap.c` `z_heap_alloc_helper()`, `k_heap_free()` | 재시도 while 루프, `z_unpend_all()` |
| Zephyr `k_malloc` | `kernel/mempool.c` `z_alloc_helper()`, `k_free()` | 블록 앞 4 B에 `struct k_heap *` |
| Zephyr 비트맵 블록 | `lib/mem_blocks/mem_blocks.c` `alloc_blocks()` → `sys_bitarray_alloc()` | 블록 메모리 안 건드림 |
| ThreadX block | `common/src/tx_block_allocate.c`, `tx_block_release.c`, `tx_block_pool_create.c` | 4 B 헤더 = next 또는 소속 풀 |
| ThreadX byte | `common/src/tx_byte_allocate.c`, `tx_byte_pool_search.c`, `tx_byte_release.c` | 주소 순 전체 리스트, 검색 중 병합, owner 재시작 |
| FreeRTOS | `portable/MemMang/heap_4.c` (F1), 풀은 앱 코드 | 커널은 큐 · 세마포어 · 임계 구역만 준다 |

```
k_mem_slab_alloc(slab, &mem, timeout)                 k_mem_slab_free(slab, mem)
  key = k_spin_lock(&slab->lock)                        [POINTER_VALIDATE] range/boundary or k_panic
  free_list != NULL ?                                   key = k_spin_lock(&slab->lock)
    yes: mem = head; head = *head; num_used++           free_list == NULL ?
    no + K_NO_WAIT: -ENOMEM                               t = z_unpend_first_thread(&wait_q)
    no + timeout: z_pend_curr(...)  ------- sleep ---->   t ? swap_data(t) = mem; z_ready_thread(t);
          <------------------- wake, mem = swap_data       z_reschedule(); return     (num_used unchanged)
  k_spin_unlock                                         *mem = head; head = mem; num_used--
```

### 10.2 예제 10 — 할당기 코드 크기 (`nm`)

무엇을 확인하는 코드인지: 각 할당기의 alloc/free 함수 크기(바이트)와, `-Os`에서 `sys_heap`의 필드 접근 함수가 따로 남는지.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/rtos/F2; arm-none-eabi-nm -S --size-sort ex7/app.elf | grep -E " (pvPortMalloc|vPortFree|xQueueReceive|xQueueGenericSend|a_pool|f_pool)$"; \
  arm-none-eabi-nm -S --size-sort ex8/zephyr/zephyr.elf | grep -E " (k_mem_slab_alloc|k_mem_slab_free|sys_heap_alloc|sys_heap_free|alloc_chunk|chunk_field|chunk_set|k_heap_alloc|k_heap_free|sys_mem_blocks_alloc|sys_bitarray_alloc)$"; \
  arm-none-eabi-nm -S --size-sort ex8_o2/zephyr/zephyr.elf | grep -E " (sys_heap_alloc|sys_heap_free|alloc_chunk|chunk_field)$"; \
  arm-none-eabi-nm -S --size-sort ex9/app.elf | grep -E " (_tx_block_allocate|_tx_block_release|_tx_byte_allocate|_tx_byte_release|_tx_byte_pool_search)$"'
```

```text
000005c4 0000001e t f_pool
00000170 00000026 t a_pool
00002268 00000120 T vPortFree
00001b14 00000160 T xQueueReceive
000020e4 00000184 T pvPortMalloc
00001980 00000194 T xQueueGenericSend
00002276 00000016 t chunk_field
0000228c 00000016 t chunk_set
0000134c 0000001c T k_heap_alloc
00003250 00000038 T k_heap_free
000027ea 0000004e T sys_mem_blocks_alloc
00001390 0000005c T k_mem_slab_alloc
000032cc 0000006e T k_mem_slab_free
000023dc 00000074 t alloc_chunk
00002632 0000008e T sys_heap_alloc
00002998 000000cc T sys_bitarray_alloc
00002562 000000d0 T sys_heap_free
0000087c 0000008c t alloc_chunk
00000b34 000000be T sys_heap_alloc
000009e0 00000154 T sys_heap_free
00000944 00000094 T _tx_block_allocate
00000b50 00000068 T _tx_block_release
00000bb8 000000bc T _tx_byte_allocate
00000db4 000000fc T _tx_byte_pool_search
00000eb0 00000120 T _tx_byte_release
```

출력에서 볼 것:

- 직접 만든 풀의 alloc / free는 38 B / 30 B(0x26 / 0x1e) — 임계 구역 호출을 포함해도 이 정도다. `k_mem_slab_alloc` 92 B, `_tx_block_allocate` 148 B는 대기 경로와 트레이스 훅 때문에 크다.
- `-Os` Zephyr 빌드(두 번째 묶음)에는 `chunk_field`, `chunk_set`이 22 B짜리 **독립 함수**로 있다. `sys_heap`의 거의 모든 필드 접근이 이 호출을 거친다 — §7.5의 "-Os에서 2배"의 원인이다. `-O2` 빌드(세 번째 묶음)에는 이 함수들이 없고 대신 `sys_heap_free`가 0xd0 → 0x154 B로 커졌다(인라인됨).
- ThreadX byte pool은 search 252 B + allocate 188 B + release 288 B. block pool의 두 배 넘는 코드다.

### 10.3 나중 모듈과의 연결

- 풀 블록을 DMA 버퍼로 쓸 때 캐시 라인 정렬 · 무효화: B8, E6.
- 풀 버퍼를 TCM · DMA 가능 SRAM 뱅크에 두기(`K_MEM_SLAB_DEFINE_IN_SECT`, 링커 섹션): F8.
- Zephyr user mode 스레드가 슬랩을 쓰려면 커널 객체 권한이 필요하다(`k_mem_slab_alloc`은 syscall이 아니다 — user 스레드는 직접 못 부른다. 4.4.2 `kernel.h`에 `__syscall` 표시가 없다): F5.
- 풀 사용량을 텔레메트리로 내보내 필드에서 "고갈 직전"을 잡기: M 모듈(관측).

---

## 11. 실무에서 다시 보기

### 11.1 드론 비행 제어기 — 텔레메트리와 로그

예를 들어 드론 FC라면 MAVLink 메시지 크기는 수십 ~ 280 B다. 64 B · 128 B · 288 B 세 등급 슬랩을 두고, 각 생산 태스크는 `K_NO_WAIT`로 할당한다. 실패하면 그 메시지 종류의 드롭 카운터를 올리고 넘어간다 — **비행 제어 rate loop는 절대 기다리지 않는다**. 로거 태스크(SD 카드 쓰기)는 4 KiB 블록 슬랩에서 `K_MSEC(20)`으로 기다린다: SD가 느려 블록이 안 돌아오면 20 ms 뒤 실패하고 "로그 손실"을 기록한다. 이착륙 테스트 뒤 `k_mem_slab_max_used_get()` 값(`CONFIG_MEM_SLAB_TRACE_MAX_UTILIZATION`)을 텔레메트리로 받아 블록 수를 다시 정한다(§3.3의 모델 → 실측 보정).

### 11.2 로봇 관절 모터 컨트롤러 — CAN RX

CAN RX ISR이 16 B 프레임 블록을 풀에서 꺼내 채우고 포인터를 큐로 보낸다. FreeRTOS라면 `pool_alloc_isr()`(예제 1 · 2의 (a)), Zephyr라면 `k_mem_slab_alloc(..., K_NO_WAIT)`. 20 kHz FOC 전류 루프 ISR은 이보다 우선순위가 높고 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 위에 있을 수 있으니(E2), 그 ISR은 풀을 만지지 않는다. 풀 크기는 "CAN 버스트 최대 프레임 수 + 처리 태스크가 쥐고 있는 수"로 잡는다(D4 §4의 깊이 계산과 같은 식).

### 11.3 센서 허브 MCU — DMA 버퍼와 비트맵 할당기

Linux SoC로 IMU 묶음을 SPI 슬레이브 DMA로 보내는 센서 허브에서, DMA 버퍼를 캐시 꺼진 SRAM 영역에 둔다면 free list를 블록 안에 쓰는 슬랩보다 `sys_mem_blocks`가 맞다 — 할당기가 버퍼 안에 아무것도 쓰지 않으니 DMA가 읽는 중인 버퍼를 할당기가 건드릴 일이 없고, 비트맵은 캐시되는 일반 RAM에 있다.

### 11.4 Don의 SSD 펌웨어 — 같은 문제, 다른 이름

| SSD 펌웨어 | 이 노트의 이름 |
|---|---|
| 호스트 데이터 버퍼 풀(4 KiB 단위, DRAM/SRAM) | 고정 크기 풀, 크기 등급 1개 |
| NVMe 명령 슬롯(CID → 명령 컨텍스트) | 고정 크기 풀 + 인덱스(포인터 대신 16비트 인덱스로 메모리 절약) |
| DMA 디스크립터 링 / 풀 | 고정 크기 풀, 정렬 = 하드웨어 요구 |
| "버퍼가 없으면 명령 fetch를 멈춘다" | 블로킹 alloc = 역압(backpressure) |
| 코어별 free 인덱스 큐 | per-core 풀 (SMP에서 락 경합 회피 — Linux per-CPU slab과 같은 이유) |
| 버퍼 누수 감시(주기적 free 개수 확인) | `k_mem_slab_num_free_get()`, `max_used`, ThreadX `tx_block_pool_info_get()` |

면접에서 이 표의 왼쪽 열 경험을 오른쪽 용어로 말하면 "RTOS를 모른다"가 "같은 문제를 이미 양산에서 풀어 봤다"가 된다(§13).

---

## 12. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| double free | 한참 뒤 두 태스크가 같은 버퍼에 쓰는 데이터 깨짐 | free list에 같은 블록이 두 번 → 고리(예제 1) | 디버그 비트맵, owner 필드, free 후 포인터 NULL. Zephyr 힙은 BASIC hardening이 잡는다 |
| 블록 크기 < 포인터 또는 정렬 안 맞음 | HardFault(unaligned LDRD) 또는 free list 깨짐 | next 포인터를 못 담음, 8 B 객체가 4 B 경계에 | 블록 크기를 정렬 단위 배수로, Zephyr는 `BUILD_ASSERT`가 잡아 준다 |
| ISR에서 timeout 있는 alloc | Zephyr assert 또는 ISR이 잠들려다 시스템 정지 | 블로킹 API를 ISR에서 | ISR은 K_NO_WAIT / TX_NO_WAIT / `FromISR`만 |
| ISR에서 `tx_byte_allocate` | `TX_CALLER_ERROR`(0x13), 반환값 안 보면 NULL 포인터 사용 | byte pool은 ISR 금지(예제 6) | ISR은 block pool |
| FreeRTOS ISR에서 `taskENTER_CRITICAL` 풀 | `configASSERT` (port.c VECTACTIVE 검사) | 태스크용 임계 구역 | `_FROM_ISR` 버전 함수를 따로 둔다 |
| 풀 크기를 평균으로 잡음 | 가끔 할당 실패, 텔레메트리 드롭 | 버스트 · 소비자 정지 시간 무시 | 최악 동시 생존 수로 잡고 `max_used`로 검증(§3.3, D4 §4) |
| 블로킹 alloc에 `K_FOREVER` / `portMAX_DELAY` | 누수 하나가 데드락처럼 보임 | 블록이 영영 안 돌아옴 | 유한 timeout + 실패 로그 + 워치독(E8) |
| 여러 태스크가 `k_heap`에 블로킹 | free가 느려지고 큰 요청이 굶음 | `z_unpend_all` + 재시도 경쟁(예제 5) | 크기별 슬랩으로 바꾸거나 대기하지 않게 설계 |
| "새 슬랩 블록은 0"이라고 가정 | 부팅마다 다른 쓰레기 값 | `K_MEM_SLAB_DEFINE` 버퍼는 `.noinit` | alloc 뒤 필요한 필드를 초기화 |
| `K_HEAP_DEFINE(h, N)`에 N바이트 객체를 넣으려 함 | 첫 할당부터 실패 | 헤더 · chunk 0 · 끝 표지(2 KiB에 76 B) | 여유를 두고, `sys_heap_runtime_stats_get()`으로 확인 |
| heap_4 사용량을 첫 malloc 전에 읽음 | `xPortGetFreeHeapSize()`가 0 | heap_4는 첫 `pvPortMalloc()`에서 초기화(예제 7 첫 줄) | 첫 할당 뒤에 읽거나 `configTOTAL_HEAP_SIZE`를 직접 쓴다 |

---

## 13. 면접에서 이렇게 말한다

**Q.** "Why do embedded/RTOS developers prefer memory pools over malloc?"

**A.** 할당 시간이 상수이고(검색 없음), 외부 단편화가 없어서 "블록 N개면 절대 실패 안 한다"를 개수로 증명할 수 있다. 대가는 내부 단편화이고 크기 등급으로 줄인다. 실측으로 고정 풀은 6000번 할당이 전부 같은 cycle이었고 힙은 점유율에 따라 최악값이 늘었다.

> Because a fixed-block pool makes allocation O(1) and fragmentation-free. Every block is the same size, so allocation is just popping the head of a free list that lives inside the free blocks themselves — no search, no split, no coalescing. That gives me a constant, provable worst case, and capacity planning becomes a count: if I can show at most N buffers are live, it never fails. The cost is internal fragmentation, which I manage with two or three size classes. In my measurements on a Cortex-M3 model, a Zephyr mem slab allocated in 29 cycles every single time, while general heaps ranged from about 120 to almost 2000 cycles depending on how fragmented they were.

**꼬리 질문**: "How does the free list cost no extra memory?" → 빈 블록의 첫 word에 next 포인터를 쓴다. 사용 중 블록은 목록에 없으니 그 word는 사용자 것이다.

**Q.** "How would you implement a memory pool in FreeRTOS, which has none?"

**A.** 정적 배열 + 블록 안 free list + 임계 구역이 가장 싸다(68 cycles/쌍). 블로킹 alloc이 필요하면 포인터 N개를 넣은 큐를 풀로 쓰면 timeout과 우선순위 순 대기가 공짜로 따라온다(약 2.7배 비용). ISR용 함수는 `_FROM_ISR` 임계 구역으로 따로 둔다.

> Statically allocate the blocks, thread a free list through them, and protect the two pointer moves with a critical section — that's the cheapest version and it's ISR-safe if I provide a FromISR variant. If callers need to block until a buffer is available, I use a queue pre-loaded with N block pointers: receive is allocate, send is free, and I get timeouts and priority-ordered waiting from the kernel. It costs roughly two to three times more per operation, so I use it only on task-level paths.

**Q.** "What happens in Zephyr when a thread is waiting on a memory slab and another thread frees a block?"

**A.** free가 대기자를 발견하면 블록을 free list에 넣지 않고 그 스레드에게 직접 넘긴다(`swap_data`). 그래서 `num_used`가 그대로고 다른 스레드가 가로챌 수 없다. 반면 `k_heap`은 free가 대기자를 전부 깨워 각자 재시도하게 해서, free가 10배 이상 느려지고 큰 요청이 굶는 것을 측정했다.

> k_mem_slab_free checks the wait queue; if a thread is pending, it hands the block directly to that thread and readies it — the block never goes back on the free list, so nobody can steal it. k_heap is different: a free wakes all waiters and each retries, because the kernel can't know which variable-size request now fits. I measured a single k_heap_free going from about 230 cycles to several thousand with three waiters, and the largest request starved. So for blocking allocation I use fixed-size slabs.

**Q.** "Is Zephyr's sys_heap deterministic?"

**A.** 상한이 있다. 크기별 bucket(2의 거듭제곱)과 비트맵이 있어서, 맞는 bucket에서 최대 3개만 보고 없으면 더 큰 bucket 하나를 ctz로 바로 꺼낸다. 측정에서 최악값이 점유율과 무관하게 평평했다. 다만 상수가 풀보다 크고 단편화로 실패할 수는 있다. 그리고 -Os 빌드에서 2배 느려지는 것을 봤다.

> It's bounded rather than constant. Free chunks are kept in power-of-two buckets with a bitmap; allocation tries at most a few chunks from the exact bucket, then takes the first chunk of the next non-empty larger bucket using count-trailing-zeros. Freeing coalesces neighbours in O(1) using the size fields in the headers. In my test the worst case stayed flat at about 240 cycles regardless of occupancy, but it can still fail from fragmentation, and with the default size optimization it was about twice as slow because the field accessors weren't inlined.

**Q.** "ThreadX has byte pools and block pools. When would you use each?"

**A.** block pool은 고정 크기, O(1), ISR에서 no-wait 가능, 4 B 헤더에 소속 풀이 있어서 release에 풀 인자가 없다. byte pool은 first-fit 검색이고 사용 중 블록까지 다 훑으며 병합은 검색 때 한다 — 측정상 fragments에 비례해 최악 1900 cycles까지 갔고, ISR에서는 아예 금지(`TX_CALLER_ERROR`)다. 실시간 경로엔 block pool, 초기화 · 비실시간 경로에만 byte pool.

> Block pools for anything real-time: fixed size, constant time, callable from an ISR with TX_NO_WAIT, and each block carries a 4-byte header pointing to its pool, so release doesn't need the pool argument. Byte pools are first-fit over an address-ordered list of all blocks, and they coalesce lazily during the search, so allocation time grows with fragmentation — I saw it reach almost 2000 cycles — and they can't be called from an ISR at all. I'd restrict byte pools to initialization and non-time-critical code.

**Q.** "How do you size a buffer pool?"

**A.** 동시에 살아 있는 최대 개수 = 도착률 × 최악 소비 지연 + 소비자가 쥐고 있는 수(D4의 Little's law와 같은 식). 크기가 여러 종류면 등급별로 따로 여유를 잡아야 해서 등급을 너무 늘리면 오히려 총량이 는다. 모델로 시작하고 `max_used` 같은 런타임 통계로 확인한다.

> I start from the worst-case number of buffers simultaneously in flight: arrival rate times worst-case consumer stall, plus whatever the consumer holds. With multiple size classes each class needs its own margin, so more classes reduce internal waste but can increase total reservation — in one model, going from three to four classes cut waste from 37 to 32 percent but needed more RAM. Then I verify with high-water-mark statistics from real runs, like Zephyr's max_used counter.

**Q.** "Tell me how you handled buffer management in your SSD firmware." (Don 경험 질문)

**A.** 데이터 버퍼와 명령 슬롯을 부팅 때 고정 개수로 만들고 free 인덱스 리스트로 돌렸다. 데이터 경로에서 malloc은 없었다. 버퍼가 바닥나면 명령 fetch를 멈춰 역압을 걸었다. RTOS로 말하면 고정 크기 풀 + 블로킹 alloc이고, 커널이 그 대기와 깨우기를 대신 해 준다.

> In our enterprise SSD firmware we never called malloc on the data path. Host data buffers, command slots and DMA descriptors were fixed-count pools created at boot, managed as free index lists per core. When buffers ran out, we stopped fetching new commands — that's backpressure. In RTOS terms it's exactly a fixed-block pool with blocking allocation; Zephyr's mem slab or a ThreadX block pool would give me the waiting and wake-up for free, plus usage statistics.

---

## 14. 직접 해보기

1. **개념**: 블록 24 B, 정렬 8 B 슬랩에서 `uint64_t`를 담는 20 B 구조체를 쓴다. 블록 크기를 20으로 바꾸면 Zephyr에서 무슨 일이 생기나? **정답/힌트:** `K_MEM_SLAB_DEFINE(…, 20, n, 8)`은 `20 % 8 != 0`이라 `BUILD_ASSERT`로 빌드 실패. 런타임 `k_mem_slab_init`은 word 정렬만 검사하므로 20 B(4의 배수)는 통과하지만 홀수 번째 블록(주소 = 시작 + 20 × 홀수)이 8 B 경계에서 4 B 어긋난다.
2. **코드**: 예제 1의 `pool.h`에 "free 할 때 블록을 0xDEADBEEF로 채우고, alloc 할 때 첫 word를 뺀 나머지가 패턴 그대로인지 검사"하는 `POOL_POISON`을 추가하라. use-after-free를 잡는지 확인하라. **정답/힌트:** 첫 word는 next 포인터라 검사에서 빼야 한다. 반납 뒤 `b->x = 1`을 하는 버그를 넣으면 다음 alloc이 패턴 불일치를 보고한다.
3. **측정**: 예제 8에 `-DCONFIG_SYS_HEAP_SMALL_ONLY=y`를 주고 `-Os`로 `k_heap`을 다시 재라. `chunk_field`가 `nm`에서 사라지나, p50이 얼마나 줄어드나? **정답/힌트:** SMALL_ONLY면 `big_heap()`이 컴파일 타임 false가 된다. 인라인 여부는 컴파일러 판단이니 `nm`으로 직접 확인한다(이 노트에서는 재지 않았다).
4. **측정**: 예제 7의 heap_4를 heap_5(F1)로, 또는 작업량 크기를 16 ~ 64 B로 바꿔 free 블록 수를 늘리면 heap_4의 최악값이 어떻게 변하나? `P` 줄의 `free_blocks`와 `D` 줄을 같이 그려 보라. **정답/힌트:** 작은 조각이 많아질수록 first-fit 검색이 길어져 최악값이 free 블록 수에 비례해 늘어나는 것이 보여야 한다.
5. **설계**: §3.3의 Python을 고쳐 "총 RAM 예산 6 KiB 안에서 실패 확률을 최소화하는 3등급 블록 수"를 찾아라. **정답/힌트:** 등급 경계는 고정하고 (n1, n2, n3)를 전수 탐색, 각 등급 고갈 확률의 합(합집합 상한)을 최소화한다.
6. **코드**: Zephyr에서 `k_heap`에 크기가 다른 블로킹 대기자를 두는 대신, 64 · 256 · 1024 B 세 슬랩 + "요청 크기에 맞는 슬랩에서 timeout 대기" 래퍼를 만들고 예제 5의 시나리오(1000 · 200 · 200 B 대기)를 다시 돌려라. **정답/힌트:** 1000 B 대기자는 1024 B 슬랩에서만 기다리므로 200 B 반납에 깨어나지 않는다 — free 비용이 상수로 돌아온다.
7. **조사**: ThreadX byte pool의 "검색 중 인터럽트 허용 + owner가 바뀌면 재시작"을 GDB 없이 보이려면? **정답/힌트:** 검색 중 TIMER0 ISR이 다른 스레드를 깨워 그 스레드가 같은 풀에서 할당하게 하고, `TX_BYTE_POOL_ENABLE_PERFORMANCE_INFO`의 fragments_searched가 한 번의 할당에서 fragments보다 크게 늘어나는지 본다.

---

## 15. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| 고정 크기 블록 풀 (fixed-size block pool) | 같은 크기 블록 N개 + free list | alloc/free가 O(1), 외부 단편화 없음 |
| intrusive free list | 빈 블록 안에 next 포인터를 둔 연결 리스트 | 메타데이터 = head 포인터 하나 |
| 외부 단편화 (external fragmentation) | 빈 바이트 합은 충분한데 연속 공간이 없음 | 힙의 실패 원인, 풀에는 없음 |
| 내부 단편화 (internal fragmentation) | 블록이 요청보다 커서 버리는 바이트 | 풀의 대가, 크기 등급으로 줄임 |
| 크기 등급 (size class) | 32 / 128 / 512 B처럼 몇 가지 블록 크기 | 요청을 덮는 가장 작은 등급에서 할당 |
| fallback | 등급이 바닥나면 위 등급에서 할당 | 실패를 줄이지만 큰 등급을 압박 |
| hand-off (직접 전달) | 반납 블록을 free list 대신 대기자에게 바로 줌 | `k_mem_slab`, ThreadX block pool |
| thundering herd | 반납 하나에 대기자 전부가 깨어나 경쟁 | `k_heap_free`의 `z_unpend_all` |
| `k_mem_slab` | Zephyr 고정 크기 풀 | `K_MEM_SLAB_DEFINE`, `.noinit` 버퍼 |
| `sys_heap` | Zephyr 가변 크기 할당기(락 없음) | 8 B 청크, bucket 비트맵, 상한 있는 시간 |
| `k_heap` | `sys_heap` + 락 + 대기 큐 | 블로킹 할당, free가 대기자 모두 깨움 |
| `sys_mem_blocks` | 비트맵 기반 고정 블록 할당기 | 블록 메모리를 건드리지 않음 |
| chunk / bucket | `sys_heap`의 8 B 할당 단위 / 크기 2의 거듭제곱별 free 리스트 | `bucket_idx()` = log2 |
| hardening level | `sys_heap` 무결성 검사 단계(0 ~ 4) | 기본 BASIC = double free · 오른쪽 넘침 검사 |
| block pool | ThreadX 고정 크기 풀 | 4 B 헤더 = 소속 풀, ISR no-wait 가능 |
| byte pool | ThreadX first-fit 가변 할당기 | 검색 중 병합, ISR 금지 |
| first-fit | 맞는 첫 빈 블록을 쓰는 정책 | heap_4, ThreadX byte pool |
| high-water mark (`max_used`) | 지금까지 최대 동시 사용 수 | 풀 크기 검증의 근거 |
| backpressure (역압) | 자원이 없으면 생산자를 늦춤 | 블로킹 alloc이 자연스럽게 만든다 |

---

## 16. 요약 & 체크리스트

크기를 고정하면 할당기가 할 일은 "free list의 머리를 떼고 붙이는 것"뿐이고, 그래서 O(1)과 외부 단편화 0을 얻는다. free list는 빈 블록 자신의 첫 word에 살아서 추가 메모리가 없다. 대가는 내부 단편화이고, 크기 등급 2 ~ 3개와 등급별 블록 수 계산(확률 모델 + 런타임 `max_used`)으로 다룬다. FreeRTOS에는 풀이 없어서 정적 배열 + 임계 구역(가장 쌈, ISR 가능) 또는 포인터 큐(블로킹 · timeout 공짜)로 직접 만든다. Zephyr는 `k_mem_slab`(O(1), 대기자에게 직접 전달), `k_heap`/`sys_heap`(bucket 비트맵으로 상한 있는 가변 힙, 대신 free가 대기자를 모두 깨움), `sys_mem_blocks`(비트맵, 블록 안 안 건드림)를, ThreadX는 block pool(O(1), ISR 가능)과 byte pool(first-fit, 검색 중 병합, ISR 금지)을 준다. 같은 랜덤 작업량 실측에서 고정 풀 셋은 29 ~ 39 cycles로 분포 폭 0 ~ 1, `k_heap`은 최악 243으로 평평, heap_4는 198 → 453, ThreadX byte pool은 422 → 1941로 점유율을 따라 늘었고, 힙 셋은 78 ~ 80% 점유에서 단편화로 실패하기 시작했다.

- [ ] 고정 크기 풀이 O(1)인 이유와 free list가 추가 메모리를 쓰지 않는 이유를 그림 1로 설명할 수 있다
- [ ] 외부 단편화와 내부 단편화를 구분하고, 풀이 어느 쪽을 없애고 어느 쪽을 대가로 내는지 말할 수 있다
- [ ] double free가 free list에 고리를 만드는 과정과 잡는 방법(비트맵, hardening)을 안다
- [ ] FreeRTOS에서 풀을 세 방식으로 만들고 각 비용(68 / 195 / 183 cycles/쌍)과 쓰임을 말할 수 있다
- [ ] ISR 할당 규칙: no-wait만, `_FROM_ISR` 임계 구역, `configMAX_SYSCALL_INTERRUPT_PRIORITY` 위의 ISR은 풀 금지
- [ ] 크기 등급과 블록 수를 이항 꼬리로 정하고, 등급을 늘리면 총 예약량이 늘 수 있다는 것을 안다
- [ ] `k_mem_slab`의 `-ENOMEM` / `-EAGAIN`, `.noinit` 버퍼, `max_used` 옵션, 직접 전달을 말할 수 있다
- [ ] `sys_heap`의 청크 · 헤더 4 B · bucket · `ALLOC_LOOPS`와 "상한 있는 힙"의 의미를 설명할 수 있다
- [ ] `k_heap_free`가 대기자를 모두 깨운다는 것과 그 결과(free 비용 폭증, 큰 요청 기아)를 안다
- [ ] ThreadX block pool의 4 B 헤더(소속 풀)와 byte pool의 "검색 중 병합", ISR 규칙을 말할 수 있다
- [ ] 측정 결과 표(§7.3)와 그림 5의 모양을 근거로 "어디에 무엇을 쓰나"(그림 8)를 설명할 수 있다
- [ ] SSD 펌웨어 버퍼 · 명령 슬롯 경험을 RTOS 풀 용어로 번역해 말할 수 있다

---

## 참고 자료

- FreeRTOS Kernel V11.3.2 소스: `portable/MemMang/heap_4.c`, `queue.c`, `portable/GCC/ARM_CM3/port.c`(`vPortEnterCritical`) — 랩 `/var/rtoslab/src/FreeRTOS-Kernel`
- FreeRTOS 문서 "Memory Management": https://www.freertos.org/a00111.html
- Zephyr v4.4.2 소스: `kernel/mem_slab.c`, `kernel/kheap.c`, `kernel/mempool.c`, `lib/heap/heap.c`, `lib/heap/heap.h`, `lib/heap/Kconfig`, `lib/mem_blocks/mem_blocks.c`, `include/zephyr/kernel.h`, `include/zephyr/sys/mem_blocks.h` — 랩 `/var/rtoslab/zephyrproject/zephyr`
- Zephyr 문서 "Memory Slabs": https://docs.zephyrproject.org/latest/kernel/memory_management/slabs.html
- Zephyr 문서 "Memory Heaps": https://docs.zephyrproject.org/latest/kernel/memory_management/heap.html
- Zephyr 문서 "Memory Blocks Allocator": https://docs.zephyrproject.org/latest/kernel/memory_management/sys_mem_blocks.html
- Eclipse ThreadX 소스(커밋 `56c8695`, 6.5.2): `common/src/tx_block_*.c`, `tx_byte_*.c`, `tx_byte_pool_search.c`, `common/inc/tx_api.h`, `common/inc/tx_byte_pool.h` — https://github.com/eclipse-threadx/threadx
- Eclipse ThreadX 문서 "Memory Pools"(ThreadX User Guide 3장): https://github.com/eclipse-threadx/rtos-docs
- 관련 노트: F1(heap_1 ~ heap_5), D4 §3 · §4(소유권 넘기기, 깊이 계산), D10 §2 · §3(`k_fifo` 첫 word, `k_stack` free-list), A5 §1.4 · §2.4(로제타 표, ThreadX 포팅), EL F9 §6(Linux SLUB)
