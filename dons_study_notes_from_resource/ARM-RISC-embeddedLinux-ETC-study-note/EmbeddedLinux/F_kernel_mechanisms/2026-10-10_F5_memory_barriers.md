# F5. 메모리 배리어와 순서 — compiler barrier · smp_mb/rmb/wmb · READ_ONCE/WRITE_ONCE · acquire/release · MMIO·DMA 순서

> **이 노트를 다 읽으면**: 순서를 바꾸는 주체 둘(컴파일러, CPU)을 나눠서 버그를 진단하고 `barrier()`·`READ_ONCE`·`smp_*`·`dma_*`·`mb()` 중 맞는 것을 고를 수 있다 · 커널 배리어 매크로가 arm64에서 정확히 어떤 명령(`dmb ish`/`ishld`/`ishst`, `dmb osh*`, `dsb`, `ldar`/`stlr`)이 되는지 objdump로 보여 줄 수 있다 · MP/SB/LB litmus 패턴을 LKMM 언어로 설명하고 실제 arm64 코어에서 재배치 횟수를 측정해 보일 수 있다 · `readl`/`writel`과 `_relaxed`의 차이, "디스크립터 쓰기 → `dma_wmb()` → owner 비트 → `writel(doorbell)`" 패턴, posted write와 read-back을 소스 근거로 말할 수 있다
> **왜 필요한가**: 멀티코어 SoC 드라이버의 "한 달에 한 번 깨지는" 버그는 대부분 순서 문제다. 면접에서는 "`volatile`이면 되지 않나요?", "`wmb()`와 `smp_wmb()` 차이?", "`writel_relaxed`는 언제 써도 되나?", "DMA 디스크립터를 넘길 때 어떤 배리어가 필요한가?"가 단골로 나온다
> **Don 기준 난이도**: SSD 펌웨어에서 DMA 디스크립터, owner 비트, `DMB`/`DSB`, 캐시 clean/invalidate, PCIe posted write를 이미 다뤘다 / 새로 배울 부분은 그 지식을 **Linux가 붙인 이름**(`smp_*` vs `dma_*` vs 필수 배리어, `_relaxed` 접근자)과 **LKMM**(Linux Kernel Memory Model)이라는 형식 모델, 그리고 컴파일러 쪽 재배치를 막는 `READ_ONCE`/`WRITE_ONCE` 규칙
> **선행 노트**: A8(유저스페이스 동기화 — C11 acquire/release, MP litmus 첫 실험, `ldar`/`stlr`), E3(모듈 빌드·로드), E5 §5.6(`READ_ONCE` 미리보기), F4(동시성과 락 — 락은 배리어를 품고 있다)

---

## 0. 큰 그림 — 이게 왜 필요한가

Don이 SSD 펌웨어에서 DMA 디스크립터를 넘길 때 한 일을 떠올려 보자. 디스크립터에 주소와 길이를 쓰고, `DMB`(또는 `DSB`)를 넣고, owner 비트를 장치 쪽으로 넘기고, 마지막으로 doorbell 레지스터를 쳤다. 배리어를 빼면 컨트롤러가 owner 비트는 보지만 주소는 옛 값을 읽는 일이 생긴다. 이 노트는 그 경험을 **Linux 커널의 어휘**로 옮긴다.

A8에서는 유저스페이스 C11 atomic으로 "arm64는 순서를 지켜 주지 않는다"를 MP litmus 하나로 확인했다(약 5천만 번에 3~7번). 이 노트는 그 다음 단계다.

- 순서를 바꾸는 주체는 **둘**이다: 컴파일러와 CPU. 막는 도구도 따로다.
- 커널은 C11 `<stdatomic.h>`를 쓰지 않는다. 대신 `READ_ONCE`, `smp_mb()`, `smp_store_release()` 같은 자체 매크로와 **LKMM**이라는 자체 메모리 모델이 있다.
- 관찰자도 여럿이다: 다른 CPU(inner shareable), DMA 엔진(outer shareable), MMIO 장치. 관찰자에 따라 배리어 종류가 달라진다(`smp_*` vs `dma_*` vs `mb()`/`readl()`).

```svg
<svg viewBox="0 0 680 450" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" font-size="14" text-anchor="middle">메모리 접근 순서를 바꾸는 두 단계, 그리고 각 단계를 막는 도구</text>
  <rect x="20" y="40" width="300" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="60" font-size="13" text-anchor="middle">C 소스 = 프로그램 순서</text>
  <text x="170" y="77" font-size="12" text-anchor="middle">data = 1;  flag = 1;</text>
  <line x1="170" y1="84" x2="170" y2="108" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="110" width="300" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="170" y="134" font-size="14" text-anchor="middle">① 컴파일러 (gcc -O2)</text>
  <text x="170" y="156" font-size="12" text-anchor="middle">재배치 · 합치기 · 없애기 · 쪼개기 · 끌어올리기</text>
  <text x="170" y="172" font-size="12" text-anchor="middle">"싱글 스레드라면 결과가 같다"면 무엇이든</text>
  <line x1="170" y1="180" x2="170" y2="204" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="206" width="300" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="170" y="226" font-size="13" text-anchor="middle">명령 순서</text>
  <text x="170" y="243" font-size="12" text-anchor="middle">objdump로 보이는 것</text>
  <line x1="170" y1="250" x2="170" y2="274" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="276" width="300" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="170" y="300" font-size="14" text-anchor="middle">② CPU 코어 (arm64 = weakly ordered)</text>
  <text x="170" y="322" font-size="12" text-anchor="middle">store buffer · OoO load · 캐시 coherence</text>
  <text x="170" y="338" font-size="12" text-anchor="middle">다른 관찰자에게 보이는 순서가 바뀐다</text>
  <line x1="170" y1="346" x2="170" y2="370" stroke="currentColor" marker-end="url(#ah1)"/>
  <rect x="20" y="372" width="300" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="395" font-size="13" text-anchor="middle">③ 관찰자</text>
  <text x="170" y="418" font-size="12" text-anchor="middle">다른 CPU · DMA 엔진 · MMIO 장치</text>
  <rect x="370" y="110" width="290" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="515" y="134" font-size="13" text-anchor="middle">barrier()  → 명령 0개, 컴파일러만 막음</text>
  <text x="515" y="156" font-size="12" text-anchor="middle">READ_ONCE / WRITE_ONCE → volatile 접근</text>
  <text x="515" y="172" font-size="12" text-anchor="middle">(한 번만, 쪼개지 말고, 지우지 말고)</text>
  <line x1="368" y1="145" x2="322" y2="145" stroke="#e08a3c" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
  <rect x="370" y="276" width="290" height="70" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="515" y="298" font-size="12" text-anchor="middle">smp_* → dmb ish / ishld / ishst</text>
  <text x="515" y="317" font-size="12" text-anchor="middle">acquire / release → ldar / stlr</text>
  <text x="515" y="336" font-size="12" text-anchor="middle">(전부 컴파일러 배리어도 겸한다)</text>
  <line x1="368" y1="311" x2="322" y2="311" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
  <rect x="370" y="372" width="290" height="60" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="515" y="394" font-size="12" text-anchor="middle">dma_* → dmb osh*,  mb() → dsb sy</text>
  <text x="515" y="414" font-size="12" text-anchor="middle">readl / writel = 접근 + 배리어 내장</text>
  <line x1="368" y1="402" x2="322" y2="402" stroke="#4a7bd0" stroke-dasharray="5 3" marker-end="url(#ah1)"/>
</svg>
```
그림 1 — 순서를 바꾸는 두 주체와 막는 도구. 컴파일러 단계는 `barrier()`/`READ_ONCE`로, CPU 단계는 `dmb`/`ldar`/`stlr`로, 장치까지 가는 순서는 `dma_*`·`mb()`·`readl/writel`로 다룬다. CPU 배리어 매크로는 전부 `"memory"` clobber를 가진 asm이라 컴파일러 배리어 역할도 겸한다.

bare-metal과 연결하면 이렇다.

| Don이 펌웨어에서 쓰던 것 | Linux 커널에서 | 이 노트 |
|---|---|---|
| ISR과 공유하는 변수에 `volatile` | 접근 지점에 `READ_ONCE`/`WRITE_ONCE` | §1 |
| `__DMB()` (CMSIS), `__asm volatile("" ::: "memory")` | `smp_mb()`, `barrier()` | §1, §2 |
| 디스크립터 쓰고 `DMB` 후 owner 비트 | `dma_wmb()` 후 owner 비트 | §6 |
| doorbell 전에 `DSB` | `writel()` (안에 `dmb oshst`), 필요 시 `wmb()` | §5, §6 |
| PCIe posted write를 read-back으로 flush | `writel()` 후 같은 장치 `readl()` | §5.4 |
| 캐시 clean 후 DMA 시작, DMA 끝나면 invalidate | `dma_map_single()`/`dma_sync_single_for_*()` (H9) | §6.3 |
| 펌웨어 팀의 "lock-free 링버퍼 규칙" | LKMM + litmus 테스트 (`tools/memory-model`) | §3 |

> 실행 환경: 유저스페이스 예제는 Lima VM `elinux`(호스트 커널 `uname -r` = `7.0.0-38-generic`, gcc 15.2, `-std=gnu11 -Wall -Wextra -O2`, vCPU 6개, Apple M2 위의 Apple Virtualization 프레임워크 — 진짜 arm64 코어에서 돈다)에서 실행했다. 커널 모듈은 공용 랩 **Linux 6.18.55** 빌드 트리로 컴파일해 QEMU `virt`(cortex-a57, **-smp 4**, 512 MB, TCG)에서 insmod했다. 모든 빌드는 경고 0개. 출력은 전부 실제 실행 결과이고, 측정값과 주소는 실행마다 다르다. VM에는 다른 작업이 동시에 돌고 있었다(load average 5~15) — 측정값의 절대치는 참고용이다. herd7(LKMM 시뮬레이터)은 설치돼 있지 않아 **실행하지 않았다**. §3에서 인용하는 herd7 출력은 커널 소스의 `tools/memory-model/README`에 실린 것이다.

### 0.1 실습 준비 — 커널 매크로를 유저스페이스로 옮기기

유저스페이스 litmus 실험을 커널과 **같은 명령**으로 하려고, 랩 소스(`arch/arm64/include/asm/barrier.h`, `include/asm-generic/rwonce.h`, `include/linux/compiler.h`)의 정의를 그대로 옮긴 헤더를 만든다. (`smp_store_release`/`smp_load_acquire`는 4바이트 경우만 남겼다.)

```c
/* kbar.h — 커널 arm64 배리어 매크로를 유저스페이스로 옮겨 온 것 (6.18 소스와 같은 asm) */
#ifndef KBAR_H
#define KBAR_H
#define barrier()        __asm__ __volatile__("" : : : "memory")
#define READ_ONCE(x)     (*(const volatile __typeof__(x) *)&(x))
#define WRITE_ONCE(x, v) do { *(volatile __typeof__(x) *)&(x) = (v); } while (0)
#define dmb(opt)         __asm__ __volatile__("dmb " #opt : : : "memory")
#define dsb(opt)         __asm__ __volatile__("dsb " #opt : : : "memory")
#define smp_mb()         dmb(ish)
#define smp_rmb()        dmb(ishld)
#define smp_wmb()        dmb(ishst)
#define smp_store_release(p, v) \
	__asm__ __volatile__("stlr %w1, %0" : "=Q"(*(p)) : "rZ"(v) : "memory")
#define smp_load_acquire(p) ({ int __v; \
	__asm__ __volatile__("ldar %w0, %1" : "=r"(__v) : "Q"(*(p)) : "memory"); __v; })
/* RCpc acquire (ARMv8.3 LRCPC) — 커널은 LTO 빌드에서 READ_ONCE에 이걸 쓴다 */
#define load_acquire_pc(p) ({ int __v; \
	__asm__ __volatile__(".arch_extension rcpc\n ldapr %w0, %1" \
	: "=r"(__v) : "Q"(*(p)) : "memory"); __v; })
#endif
```

이 VM의 CPU가 `ldapr`를 지원하는지는 `/proc/cpuinfo`의 `lrcpc` 플래그로 확인한다.

```sh
limactl shell elinux -- bash -lc 'grep -m1 Features /proc/cpuinfo; nproc'
```
```text
Features	: fp asimd evtstrm aes pmull sha1 sha2 crc32 atomics fphp asimdhp cpuid asimdrdm jscvt fcma lrcpc dcpop sha3 asimddp sha512 asimdfhm dit uscat ilrcpc flagm sb paca pacg dcpodp flagm2 frint i8mm bf16 bti
6
```
출력에서 볼 것: `atomics`(LSE, `ldadd`/`stadd` 같은 단일 명령 atomic), `lrcpc`(`ldapr`), `uscat`(LSE2, 정렬 조건이 완화된 single-copy atomicity)이 있다. QEMU의 cortex-a57(ARMv8.0)에는 셋 다 없다 — 이 차이가 §2.5에서 커널 atomic의 alternative 패치로 드러난다.

커널 모듈은 E5와 같은 방식으로 랩 rootfs를 복사해 `.ko`와 게스트 스크립트를 넣고 부팅한다. 이번엔 SMP 실험을 위해 `-smp 4`, 그리고 TCG의 스레드 모드(`thread=multi`는 vCPU마다 호스트 스레드 하나, `thread=single`은 호스트 스레드 하나가 vCPU를 돌아가며 실행)를 인자로 받는다.

```sh
#!/bin/sh
# boot.sh guest_script.sh [tcg-thread=multi|single] -> lab kernel(-smp 4) 부팅, 스크립트 실행, 콘솔 출력
set -e
W=/var/tmp/F5
rm -rf $W/r && mkdir -p $W/r && cp -a /var/lab/build/rootfs/. $W/r/
cp $W/mod/*.ko $W/r/
cp "$1" $W/r/f5.sh
(cd $W/r && find . | cpio -o -H newc --quiet | gzip) > $W/i.gz
timeout 300 qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 4 -m 512M -nographic \
  -accel tcg,thread=${2:-multi} -kernel /var/lab/out/Image -initrd $W/i.gz \
  -append "console=ttyAMA0 quiet LAB_CMD=sh~/f5.sh" | tr -d '\r'
```

---

## 1. 첫 번째 재배치자 — 컴파일러

### 1.1 직관: 컴파일러는 "다른 스레드"를 모른다

C 표준의 **as-if 규칙**: 컴파일러는 **이 스레드 하나만 보고** 관찰 가능한 결과가 같다면 코드를 마음대로 바꿔도 된다. 그런데 "관찰 가능한 결과"에 다른 CPU, ISR, DMA 장치는 들어가지 않는다. 그래서 컴파일러 입장에서는 정당한 최적화가, 다른 관찰자에게는 순서 위반이 된다. 게다가 C11 이후 표준은 **data race**(락이나 atomic 없이 두 스레드가 같은 변수에 접근하고 하나 이상이 쓰기)를 정의되지 않은 동작(UB)으로 본다. 컴파일러가 "race는 없다"고 가정해도 되는 근거다.

Don에게 익숙한 장면: Cortex-M에서 ISR이 세우는 플래그를 `while (!flag);`로 기다렸는데 `-O2`에서 무한 루프가 된 경험. 그게 이 절의 전부다. 커널은 그 문제를 `volatile` **변수 선언**이 아니라 `READ_ONCE` **접근 지점 표시**로 푼다.

### 1.2 예제 1 — 폴링 루프: 평범한 load vs `READ_ONCE`

다른 스레드가 100 ms 뒤 `ready = 1`을 쓰는 동안 메인 스레드가 기다린다. 차이는 루프 조건 한 줄뿐이다.

```c
// ex1_poll.c — 폴링 루프: 평범한 load vs READ_ONCE. 다른 스레드가 100 ms 뒤 ready=1.
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "kbar.h"

int ready;                        /* 다른 스레드가 바꾸는 플래그 (volatile 아님) */

static void *setter(void *arg)
{
    (void)arg;
    usleep(100000);
    WRITE_ONCE(ready, 1);
    return NULL;
}

__attribute__((noinline)) void wait_ready(void)
{
#ifdef USE_ONCE
    while (!READ_ONCE(ready))     /* 매번 메모리에서 다시 읽어라 */
        ;
#else
    while (!ready)                /* 컴파일러: "루프 안에서 ready를 바꾸는 코드가 없네" */
        ;
#endif
}

int main(void)
{
    pthread_t t;
    if (pthread_create(&t, NULL, setter, NULL)) { perror("pthread_create"); exit(1); }
    wait_ready();
    printf("saw ready=1\n");
    pthread_join(t, NULL);
    return 0;
}
```
```sh
limactl shell elinux -- bash -lc 'mkdir -p /var/tmp/F5/u && cd /var/tmp/F5/u && S=/Users/donh/.elinux_scratch/F5/u; gcc -std=gnu11 -Wall -Wextra -O2 -pthread $S/ex1_poll.c -o ex1_plain && gcc -std=gnu11 -Wall -Wextra -O2 -pthread -DUSE_ONCE $S/ex1_poll.c -o ex1_once && for b in ex1_plain ex1_once; do echo "=== $b"; objdump -d --no-show-raw-insn $b | sed -n "/<wait_ready>:/,/^$/p"; done; echo "=== run"; timeout 3 ./ex1_plain; echo "ex1_plain exit=$?"; timeout 3 ./ex1_once; echo "ex1_once exit=$?"'
```
```text
=== ex1_plain
0000000000000aec <wait_ready>:
 aec:	adrp	x0, 20000 <__data_start>
 af0:	ldr	w0, [x0, #20]
 af4:	cbz	w0, afc <wait_ready+0x10>
 af8:	ret
 afc:	b	afc <wait_ready+0x10>

=== ex1_once
0000000000000aec <wait_ready>:
 aec:	adrp	x1, 20000 <__data_start>
 af0:	add	x1, x1, #0x14
 af4:	nop
 af8:	nop
 afc:	nop
 b00:	ldr	w0, [x1]
 b04:	cbz	w0, b00 <wait_ready+0x14>
 b08:	ret

=== run
ex1_plain exit=124
saw ready=1
ex1_once exit=0
```
출력에서 볼 것: 평범한 버전은 `ready`를 **딱 한 번** 읽고(`af0`), 0이면 `afc: b afc` — **자기 자신으로 점프하는 무한 루프**로 간다. load가 루프 밖으로 끌어올려진(hoisting) 것이다. 실제로 3초 timeout에 걸려 종료 코드 124로 죽었다. `READ_ONCE` 버전은 루프 안에 `ldr`이 있어(`b00` ↔ `b04`) 100 ms 뒤 정상 종료한다. 이건 CPU와 무관한, 순수한 **컴파일러** 문제다 — x86에서도 똑같이 걸린다.

커널에서는 이런 대기를 직접 짜기보다 `smp_cond_load_acquire(ptr, cond)`나 `readl_poll_timeout()`(MMIO, H 모듈)을 쓴다. arm64의 `smp_cond_load_*`는 `READ_ONCE`/`smp_load_acquire`로 읽고, 조건이 안 맞으면 `__cmpwait_relaxed()`(exclusive monitor + `wfe`)로 그 캐시 라인이 바뀔 때까지 잠깐 잠든다 — 랩 소스 `arch/arm64/include/asm/barrier.h`에서 확인했다. 바쁜 대기의 전력 소모를 줄이는 arm64식 최적화다.

### 1.3 예제 2 — 컴파일러가 평범한 접근에 하는 일 네 가지

LKMM 문서(`tools/memory-model/Documentation/access-marking.txt`)와 `Documentation/memory-barriers.txt`가 경고하는 변환을 gcc 15 `-O2`로 직접 확인한다.

```c
// ex2_compiler.c — 컴파일러가 평범한 접근에 하는 일 4가지 (컴파일만 해서 asm 비교)
#include "kbar.h"
unsigned long long v64;
int flag, data, x;
int arr[2];

/* (a) store fusing: 같은 변수에 두 번 쓰면 앞의 store가 사라진다 */
void fuse_plain(void) { flag = 1; flag = 2; }
void fuse_once(void)  { WRITE_ONCE(flag, 1); WRITE_ONCE(flag, 2); }

/* (b) load fusing: 같은 변수를 두 번 읽으면 한 번만 읽는다 */
int  load2_plain(void) { int a = x; int b = x; return a + b; }
int  load2_once(void)  { int a = READ_ONCE(x); int b = READ_ONCE(x); return a + b; }

/* (c) store tearing: 상위·하위가 같은 64비트 상수를 32비트 두 번으로 쪼갠다 */
void tear_plain(void) { v64 = 0x1234567812345678ULL; }
void tear_once(void)  { WRITE_ONCE(v64, 0x1234567812345678ULL); }

/* (d) 서로 다른 변수의 store 재배치 + compiler barrier */
void order_plain(int v) { data = v; flag = 1; data = v + 1; }
void order_barrier(int v) { data = v; barrier(); flag = 1; barrier(); data = v + 1; }
```
```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/u && gcc -std=gnu11 -Wall -Wextra -O2 -c /Users/donh/.elinux_scratch/F5/u/ex2_compiler.c -o ex2.o && objdump -d --no-show-raw-insn ex2.o | grep -v "^$" | grep -v "file format\|Disassembly\|nop$"'
```
```text
0000000000000000 <fuse_plain>:
   0:	adrp	x0, 0 <fuse_plain>
   4:	mov	w1, #0x2                   	// #2
   8:	str	w1, [x0]
   c:	ret
0000000000000010 <fuse_once>:
  10:	adrp	x0, 0 <fuse_plain>
  14:	mov	w1, #0x1                   	// #1
  18:	str	w1, [x0]
  1c:	mov	w1, #0x2                   	// #2
  20:	str	w1, [x0]
  24:	ret
0000000000000028 <load2_plain>:
  28:	adrp	x0, 0 <fuse_plain>
  2c:	ldr	w0, [x0]
  30:	lsl	w0, w0, #1
  34:	ret
0000000000000040 <load2_once>:
  40:	adrp	x1, 0 <fuse_plain>
  44:	add	x1, x1, #0x0
  48:	ldr	w2, [x1, #4]
  4c:	ldr	w0, [x1, #4]
  50:	add	w0, w2, w0
  54:	ret
0000000000000060 <tear_plain>:
  60:	adrp	x0, 0 <fuse_plain>
  64:	add	x0, x0, #0x0
  68:	mov	w1, #0x5678                	// #22136
  6c:	movk	w1, #0x1234, lsl #16
  70:	stp	w1, w1, [x0, #8]
  74:	ret
0000000000000080 <tear_once>:
  80:	adrp	x0, 0 <fuse_plain>
  84:	add	x0, x0, #0x0
  88:	mov	x1, #0x5678                	// #22136
  8c:	movk	x1, #0x1234, lsl #16
  90:	orr	x1, x1, x1, lsl #32
  94:	str	x1, [x0, #8]
  98:	ret
00000000000000a0 <order_plain>:
  a0:	adrp	x1, 0 <fuse_plain>
  a4:	add	x2, x1, #0x0
  a8:	mov	w3, #0x1                   	// #1
  ac:	add	w0, w0, w3
  b0:	str	w3, [x1]
  b4:	str	w0, [x2, #16]
  b8:	ret
00000000000000c0 <order_barrier>:
  c0:	adrp	x2, 0 <fuse_plain>
  c4:	add	x1, x2, #0x0
  c8:	str	w0, [x1, #16]
  cc:	mov	w3, #0x1                   	// #1
  d0:	str	w3, [x2]
  d4:	add	w0, w0, w3
  d8:	str	w0, [x1, #16]
  dc:	ret
```
(함수 사이 패딩 `nop` 줄은 grep으로 뺐다. 주소 0은 아직 재배치 전인 `.o`라서다.)

출력에서 볼 것, 네 가지:

- (a) **store fusing**: `fuse_plain`은 `str` 하나(값 2)뿐이다. `flag = 1`은 사라졌다. 장치나 다른 CPU가 1을 보는 순간은 영영 오지 않는다. `WRITE_ONCE` 버전은 `str` 두 개를 그대로 낸다.
- (b) **load fusing**: `load2_plain`은 `ldr` 하나 + `lsl #1`(= 두 배). "두 번 읽어 비교"하는 코드(예: seqlock 스타일, 하드웨어 카운터 두 번 읽기)가 무력화된다. `READ_ONCE` 버전은 `ldr` 두 개.
- (c) **store tearing**: `tear_plain`은 64비트 상수를 32비트 레지스터 하나로 만들고 `stp w1, w1` — **32비트 store 두 개짜리 pair**로 쓴다. 상수 만드는 명령을 하나 아끼려는 최적화다. Arm 아키텍처는 `stp`의 두 원소 각각만 single-copy atomic이라고 보장하므로(LSE2가 조건을 넓히긴 하지만 그걸 기대하면 안 된다), 다른 CPU가 상위만 바뀐 값을 볼 수 있다. `WRITE_ONCE` 버전은 64비트 레지스터를 만들어 `str x1` 하나로 쓴다.
- (d) **재배치 + dead store 제거**: `order_plain`은 `flag`를 **먼저** 쓰고(`b0`) `data`는 마지막 값 하나만 쓴다(`b4`). 소스의 `data = v`는 사라졌다. `barrier()`를 넣은 `order_barrier`는 `data=v`(`c8`) → `flag=1`(`d0`) → `data=v+1`(`d8`) 소스 순서 그대로다. 그리고 **`barrier()` 자체는 명령을 하나도 만들지 않았다**.

한 가지 더, **invented load**(없던 load 만들기)도 있다. 레지스터가 모자라면 컴파일러가 지역 변수에 담아 둔 값을 버리고 공유 변수를 **다시 읽을** 수 있다. `int t = p; if (t) use(t->x);`가 `if (p) use(p->x);`처럼 두 번 읽는 코드가 되면, 그 사이 다른 CPU가 `p = NULL`을 쓸 경우 NULL 역참조가 난다. 짧은 예제로는 재현이 어려워 **이 노트에서는 실행하지 않았다**. 대책은 같다: 공유 변수는 `READ_ONCE`로 한 번 읽어 지역 변수에 담는다.

### 1.4 `barrier()` — 명령 0개짜리 컴파일러 배리어

```c
/* include/linux/compiler.h (랩 소스 85행) */
# define barrier() __asm__ __volatile__("": : :"memory")
```

빈 inline asm에 `"memory"` clobber를 붙인 것이다. gcc에게 "이 asm은 아무 메모리나 읽고 쓸 수 있다"고 말하는 셈이라, gcc는

- 이 지점 앞에서 레지스터에 들고 있던 메모리 값을 **버리고**(뒤에서 다시 읽고),
- 앞의 store는 이 지점 전에 **실제로 메모리에 쓰고**,
- 메모리 접근을 이 지점 너머로 옮기지 않는다.

CPU에게는 아무 영향이 없다. 그래서 `barrier()`가 충분한 경우는 **관찰자가 같은 CPU**일 때뿐이다 — 같은 CPU의 인터럽트 핸들러, preemption 경계, `CONFIG_SMP=n` 커널. 실제로 `include/asm-generic/barrier.h`는 `CONFIG_SMP`가 꺼지면 `smp_mb()`/`smp_rmb()`/`smp_wmb()`를 `barrier()`로 정의한다(랩 소스 110~122행). 단일 코어에서는 CPU가 자기 자신의 접근 순서를 항상 프로그램 순서대로 **관찰**하기 때문이다.

### 1.5 `READ_ONCE` / `WRITE_ONCE` — 접근 지점에 붙이는 표시

```c
/* include/asm-generic/rwonce.h (랩 소스, 핵심만) */
#define __READ_ONCE(x)	(*(const volatile __unqual_scalar_typeof(x) *)&(x))
#define READ_ONCE(x)							\
({									\
	compiletime_assert_rwonce_type(x);				\
	__READ_ONCE(x);							\
})
#define __WRITE_ONCE(x, val)						\
do {									\
	*(volatile typeof(x) *)&(x) = (val);				\
} while (0)
```

핵심은 `volatile` 캐스트다. gcc는 volatile 접근을 지우거나, 합치거나, 개수를 바꾸지 않는다. 그리고 `compiletime_assert_rwonce_type()`이 크기를 검사한다 — `char`/`short`/`int`/`long`/`long long` 크기가 아니면 **컴파일 에러**. 그래서 machine word 크기의 정렬된 접근이 되고 tearing이 없다.

| 보장 | `READ_ONCE`/`WRITE_ONCE` | `barrier()` | `smp_*` 배리어 |
|---|---|---|---|
| 접근을 지우거나 합치지 않음 | ✅ (그 접근만) | 간접적 | 간접적 |
| tearing 없음 (word 크기) | ✅ | ❌ | ❌ |
| 컴파일러 재배치 금지 | volatile끼리만 | ✅ (모든 메모리) | ✅ |
| 다른 CPU에 대한 순서 | ❌ | ❌ | ✅ |
| 명령 비용 | 평범한 `ldr`/`str` | 0 | `dmb` 등 |

언제 쓰나 — 커널 규칙(LKMM `access-marking.txt`의 요지):

- 락 없이 **동시에** 접근될 수 있는 공유 변수는 양쪽 다 `READ_ONCE`/`WRITE_ONCE`로 표시한다. 표시하지 않은 동시 접근은 data race이고, KCSAN(Kernel Concurrency Sanitizer, M 모듈)이 잡는다.
- 락으로 보호된 변수는 필요 없다. 락이 컴파일러·CPU 배리어를 다 품고 있다(F4).
- `READ_ONCE`는 배리어가 **아니다**. 다른 CPU에 대한 순서는 §2의 도구로 따로 만든다(§3.5 실험의 "MP relaxed"가 정확히 `READ_ONCE`/`WRITE_ONCE`만 쓴 경우다 — 순서가 깨진다).

arm64 특이점 하나: `arch/arm64/include/asm/rwonce.h`는 **`CONFIG_LTO`일 때** `READ_ONCE`를 `ldar`(또는 RCpc가 있으면 `ldapr`)로 바꾼다. LTO가 주소 의존(address dependency)을 컨트롤 의존으로 바꿔 버릴 위험이 있어서, 아예 acquire load로 만들어 순서를 지킨다는 주석이 붙어 있다. 랩 커널은 LTO가 아니므로 `READ_ONCE`는 평범한 `ldr`이다(§2.4 objdump에서 확인).

### 1.6 함정

- `volatile` 변수 선언으로 때우기: 커널 코딩 스타일 문서(`Documentation/process/volatile-considered-harmful.rst`)가 명시적으로 반대한다. `volatile`은 컴파일러만 막고 CPU 순서는 못 막으며, 락으로 보호된 구간에서는 최적화만 방해한다. 예외는 `jiffies` 같은 극소수와 MMIO 접근자 내부.
- "`READ_ONCE`를 붙였으니 순서도 맞다": 아니다. 두 `READ_ONCE` 사이 순서는 컴파일러만 지키고 arm64 CPU는 바꿀 수 있다.
- 구조체 통째로 `READ_ONCE`: 크기 검사에 걸려 컴파일 에러가 나거나(8바이트 초과), 통과해도 필드별 일관성은 보장되지 않는다. 여러 필드를 일관되게 읽으려면 락, seqlock, RCU(F4).
- `barrier()`로 SMP 순서 맞추기: 단일 코어 테스트에선 통과하고 멀티코어 양산에서 깨진다.

---

## 2. 두 번째 재배치자 — CPU (arm64는 weakly ordered)

### 2.1 직관: 코어 하나에는 순서대로, 남에게는 뒤죽박죽

A8 §7.2에서 본 것처럼 arm64 코어는 성능을 위해

- store를 **store buffer**에 넣어 두고 다음 명령을 계속 실행한다(캐시에 반영되기 전에 뒤의 load가 먼저 끝날 수 있다 → store→load 재배치, SB 패턴).
- load를 **out-of-order**로 실행한다(뒤의 load가 먼저 캐시를 읽을 수 있다 → load→load 재배치, MP 읽는 쪽).
- 서로 다른 캐시 라인의 store가 다른 순서로 다른 코어에 보일 수 있다(store→store 재배치, MP 쓰는 쪽).

같은 코어에서 자기 접근을 보면 항상 프로그램 순서대로 보인다. 문제는 **다른 관찰자**다. 그리고 Armv8은 **other-multi-copy atomic**이다: 어떤 store가 자기 코어 밖의 관찰자 하나에게 보이면 모든 다른 관찰자에게도 동시에 보인다. 그래서 "CPU A와 B가 같은 두 store를 서로 다른 순서로 보는" IRIW 결과는 arm64 하드웨어에선 안 나온다(Power는 나온다 — LKMM이 IRIW를 "Sometimes"로 두는 이유, §3.7).

x86(TSO)과 비교:

| 재배치 | x86 (TSO) | arm64 | Linux가 주는 도구 |
|---|---|---|---|
| store → store | 안 함 | 함 | `smp_wmb()`, `smp_store_release()` |
| load → load | 안 함 | 함 | `smp_rmb()`, `smp_load_acquire()` |
| load → store | 안 함 | 함 | acquire/release, `smp_mb()` |
| store → load (다른 주소) | **함** (store buffer) | 함 | `smp_mb()` 만 |

x86에서 `smp_rmb()`/`smp_wmb()`가 사실상 `barrier()`인 이유가 이 표다. 그래서 x86에서만 테스트한 드라이버가 arm64 SoC에 와서 깨진다.

### 2.2 arm64 배리어 명령 — 종류 × 범위

`DMB <option>`(Data Memory Barrier)의 option은 두 축으로 이뤄진다.

- **어떤 접근을 순서 짓나**: (없음) = load와 store 전부, `LD` = 앞의 load ↔ 뒤의 load·store, `ST` = 앞의 store ↔ 뒤의 store.
- **누구에게(shareability domain)**: `ISH`(inner shareable — 같은 OS 아래의 모든 CPU), `OSH`(outer shareable — DMA 마스터 같은 장치까지 포함하는 넓은 도메인), `SY`(full system).

그 밖의 명령:

- `DSB`(Data Synchronization Barrier): 앞의 메모리 접근이 **완료될 때까지** 뒤의 **모든 명령**을 멈춘다. `DMB`는 "보이는 순서"만 정하고 실행은 계속하는 반면, `DSB`는 실행 자체를 막는다. 캐시·TLB 유지보수 완료 대기, 시스템 레지스터 쓰기 전, WFI 전에 필요하다.
- `ISB`: 파이프라인을 비워 뒤 명령을 새로 fetch한다(시스템 레지스터 변경 반영, self-modifying code).
- `LDAR`/`STLR`: acquire load / release store. 배리어를 접근 자체에 붙인 형태라 `dmb`보다 가볍다. Arm은 `STLR` 뒤의 `LDAR`도 순서를 지킨다(RCsc, sequentially consistent acquire/release).
- `LDAPR`(ARMv8.3 LRCPC): RCpc acquire. **앞의 `STLR`과의 순서를 보장하지 않는다.** §3.6에서 측정으로 차이를 본다.

### 2.3 Linux의 배리어 매크로 가족

커널에는 같은 모양(mb/rmb/wmb)의 배리어가 **세 가족**으로 있다. 이름이 비슷해 가장 많이 헷갈리는 부분이다.

| 가족 | 매크로 | 순서를 맞추는 상대 | 언제 |
|---|---|---|---|
| SMP 배리어 | `smp_mb()`, `smp_rmb()`, `smp_wmb()`, `smp_load_acquire()`, `smp_store_release()`, `smp_mb__before_atomic()`/`__after_atomic()`, `smp_store_mb()` | 다른 CPU (일반 cacheable 메모리) | lock-free 자료구조, 플래그 넘기기 |
| DMA 배리어 | `dma_mb()`, `dma_rmb()`, `dma_wmb()` | DMA 장치가 보는 coherent 메모리 | 디스크립터 owner 비트 |
| 필수(mandatory) 배리어 | `mb()`, `rmb()`, `wmb()` | 모든 것 — MMIO, 일반 메모리, 장치 | `_relaxed` MMIO와 일반 메모리를 섞어 순서를 맞출 때 |

이름 규칙: `smp_`가 붙은 것은 `CONFIG_SMP=n`이면 `barrier()`로 줄어든다. `mb()`류는 UP 커널에서도 진짜 명령이 된다(장치는 여전히 다른 관찰자이므로). `virt_mb()` 등은 SMP 게스트가 UP로 빌드됐어도 하이퍼바이저와 순서를 맞춰야 할 때 쓰는 변형이다.

A8의 C11 표현과 짝지으면:

| 커널 | C11 대응 (근사) | 비고 |
|---|---|---|
| `READ_ONCE(x)` / `WRITE_ONCE(x, v)` | `memory_order_relaxed` load/store | 커널판은 volatile, C11은 atomic 타입 |
| `smp_load_acquire(p)` | `atomic_load_explicit(p, memory_order_acquire)` | 둘 다 arm64 `ldar` |
| `smp_store_release(p, v)` | `atomic_store_explicit(p, v, memory_order_release)` | 둘 다 `stlr` |
| `smp_mb()` | `atomic_thread_fence(memory_order_seq_cst)` | 둘 다 `dmb ish` |
| `smp_rmb()` / `smp_wmb()` | 정확한 대응 없음 | C11 acquire fence는 load→load와 load→store 둘 다 |
| `atomic_inc_return()` | `atomic_fetch_add(…, seq_cst)` | 커널판은 앞뒤 full barrier |

커널이 C11을 안 쓰는 이유: C11 모델이 커널보다 늦게 나왔고, 커널은 C11이 다루지 못하는 것(컨트롤 의존, MMIO, RCU의 주소 의존 규칙)에 기대고 있으며, 컴파일러 버전마다 C11 atomic 구현 품질이 달랐다. LKMM은 커널이 실제로 기대는 보장을 형식화한 것이다.

### 2.4 예제 3 — 매크로 하나당 함수 하나, objdump로 명령 확인 (커널 모듈 `f5_macros`)

"`smp_rmb()`는 무슨 명령이 되나?"는 외우는 것보다 **직접 컴파일해서 보는** 게 확실하다. 매크로 하나를 함수 하나에 넣은 모듈을 랩 커널 트리로 빌드한다.

```c
// f5_macros.c — 커널 배리어 매크로 하나당 함수 하나. objdump로 arm64 명령을 확인한다.
#include <linux/module.h>
#include <linux/io.h>
#include <linux/atomic.h>

/* 전역(비 static)으로 둬야 컴파일러가 "아무도 안 읽는 store"로 지우지 않는다 */
int f5_x, f5_y;
atomic_t f5_a;
void __iomem *f5_reg;
u32 f5_v;

#define F(name, body) static noinline void name(void) { body; }
F(f5_barrier,        f5_x = 1; barrier(); f5_y = 1)
F(f5_read_once,      f5_v = READ_ONCE(f5_x))
F(f5_write_once,     WRITE_ONCE(f5_x, 1))
F(f5_smp_mb,         smp_mb())
F(f5_smp_rmb,        smp_rmb())
F(f5_smp_wmb,        smp_wmb())
F(f5_mb,             mb())
F(f5_rmb,            rmb())
F(f5_wmb,            wmb())
F(f5_dma_mb,         dma_mb())
F(f5_dma_rmb,        dma_rmb())
F(f5_dma_wmb,        dma_wmb())
F(f5_store_release,  smp_store_release(&f5_x, 1))
F(f5_load_acquire,   f5_v = smp_load_acquire(&f5_x))
F(f5_store_mb,       smp_store_mb(f5_x, 1))
F(f5_before_atomic,  smp_mb__before_atomic(); atomic_inc(&f5_a))
F(f5_after_atomic,   atomic_inc(&f5_a); smp_mb__after_atomic())
F(f5_inc_return,     f5_v = atomic_inc_return(&f5_a))
F(f5_inc_relaxed,    f5_v = atomic_inc_return_relaxed(&f5_a))
F(f5_readl,          f5_v = readl(f5_reg))
F(f5_readl_relaxed,  f5_v = readl_relaxed(f5_reg))
F(f5_writel,         writel(1, f5_reg))
F(f5_writel_relaxed, writel_relaxed(1, f5_reg))
F(f5_isb,            isb())

static void (*const f5_fns[])(void) = {
	f5_barrier, f5_read_once, f5_write_once, f5_smp_mb, f5_smp_rmb, f5_smp_wmb,
	f5_mb, f5_rmb, f5_wmb, f5_dma_mb, f5_dma_rmb, f5_dma_wmb, f5_store_release,
	f5_load_acquire, f5_store_mb, f5_before_atomic, f5_after_atomic,
	f5_inc_return, f5_inc_relaxed, f5_isb,      /* MMIO 함수는 f5_reg가 없어 호출 안 함 */
};
static void (*const f5_mmio_fns[])(void) __used = {
	f5_readl, f5_readl_relaxed, f5_writel, f5_writel_relaxed };

static int __init f5_macros_init(void)
{
	for (size_t i = 0; i < ARRAY_SIZE(f5_fns); i++)
		f5_fns[i]();
	pr_info("f5_macros: ran %zu barrier functions, f5_a=%d f5_x=%d\n",
		ARRAY_SIZE(f5_fns), atomic_read(&f5_a), f5_x);
	return 0;
}
static void __exit f5_macros_exit(void) { }
module_init(f5_macros_init);
module_exit(f5_macros_exit);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F5: barrier macro -> arm64 instruction map");
```

Kbuild는 이 노트의 모듈 세 개를 한 번에 빌드한다.

```make
obj-m := f5_macros.o f5_barrier.o f5_doorbell.o
```
```sh
limactl shell elinux -- bash -lc 'rm -rf /var/tmp/F5/mod && mkdir -p /var/tmp/F5/mod && cp ~/.elinux_scratch/F5/m/* /var/tmp/F5/mod/ && cd /var/tmp/F5/mod && make -C /var/lab/out/linux-build M=$PWD modules 2>&1 | grep -E "error|warning|LD"'
```
```text
  LD [M]  f5_macros.ko
  LD [M]  f5_barrier.ko
  LD [M]  f5_doorbell.ko
```
경고·에러 줄이 없다. 먼저 QEMU에서 실제로 로드되는지 본다(`g1.sh`는 `insmod /f5_macros.ko && dmesg | grep f5_macros`).

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F5/boot.sh /Users/donh/.elinux_scratch/F5/g1.sh multi'
```
```text
lab initramfs: 6.18.55 up 1.30s
6.18.55
4
f5_macros: loading out-of-tree module taints kernel.
f5_macros: ran 20 barrier functions, f5_a=4 f5_x=1
[    1.643568] reboot: Power down
```
(부팅 로그 앞부분은 생략했다.) 출력에서 볼 것: 게스트 커널 6.18.55, CPU 4개. 20개 함수가 실행됐고 atomic을 증가시킨 함수 4개만큼 `f5_a=4`. 이제 명령을 본다. 단순 배리어부터:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/mod && objdump -d --no-show-raw-insn f5_macros.ko | grep -v "^$" | grep -v "file format\|Disassembly\|nop$" | sed -n "/<f5_barrier>:/,/<cleanup_module>/p"'
```
```text
0000000000000000 <f5_barrier>:
   0:	adrp	x1, 0 <f5_barrier>
   4:	mov	w0, #0x1                   	// #1
   8:	add	x2, x1, #0x0
   c:	str	w0, [x1]
  10:	str	w0, [x2, #4]
  14:	ret
0000000000000020 <f5_read_once>:
  20:	adrp	x0, 0 <f5_barrier>
  24:	add	x1, x0, #0x0
  28:	ldr	w0, [x0]
  2c:	str	w0, [x1, #8]
  30:	ret
0000000000000040 <f5_write_once>:
  40:	adrp	x0, 0 <f5_barrier>
  44:	mov	w1, #0x1                   	// #1
  48:	str	w1, [x0]
  4c:	ret
0000000000000050 <f5_smp_mb>:
  50:	dmb	ish
  54:	ret
0000000000000060 <f5_smp_rmb>:
  60:	dmb	ishld
  64:	ret
0000000000000068 <f5_smp_wmb>:
  68:	dmb	ishst
  6c:	ret
0000000000000070 <f5_mb>:
  70:	dsb	sy
  74:	ret
0000000000000080 <f5_rmb>:
  80:	dsb	ld
  84:	ret
0000000000000088 <f5_wmb>:
  88:	dsb	st
  8c:	ret
0000000000000090 <f5_dma_mb>:
  90:	dmb	osh
  94:	ret
00000000000000a0 <f5_dma_rmb>:
  a0:	dmb	oshld
  a4:	ret
00000000000000a8 <f5_dma_wmb>:
  a8:	dmb	oshst
  ac:	ret
00000000000000b0 <f5_store_release>:
  b0:	adrp	x0, 0 <f5_barrier>
  b4:	mov	w1, #0x1                   	// #1
  b8:	add	x0, x0, #0x0
  bc:	stlr	w1, [x0]
  c0:	ret
00000000000000c4 <f5_load_acquire>:
  c4:	adrp	x0, 0 <f5_barrier>
  c8:	add	x1, x0, #0x0
  cc:	ldar	w0, [x1]
  d0:	str	w0, [x1, #8]
  d4:	ret
00000000000000e0 <f5_store_mb>:
  e0:	adrp	x0, 0 <f5_barrier>
  e4:	mov	w1, #0x1                   	// #1
  e8:	str	w1, [x0]
  ec:	dmb	ish
  f0:	ret
```
(출력이 길어 세 덩어리로 나눠 싣는다. 이것이 첫 덩어리 — `f5_store_mb`까지.)

출력에서 볼 것:

- `f5_barrier`: `str` 두 개뿐. **`barrier()`는 명령 0개**다. 하는 일은 그 사이로 컴파일러가 접근을 옮기지 못하게 하는 것뿐이다.
- `f5_read_once`/`f5_write_once`: 평범한 `ldr`/`str`. 이 커널은 LTO가 아니라서 `READ_ONCE`가 `ldar`로 바뀌지 않았다(§1.5).
- `smp_mb`/`smp_rmb`/`smp_wmb` → `dmb ish`/`dmb ishld`/`dmb ishst`. **inner shareable** 도메인 = CPU끼리.
- `mb`/`rmb`/`wmb` → `dsb sy`/`dsb ld`/`dsb st`. **DSB**다 — 완료까지 실행을 멈춘다. 이름은 비슷해도 `smp_wmb()`(`dmb ishst`)와 `wmb()`(`dsb st`)는 전혀 다른 무게다.
- `dma_mb`/`dma_rmb`/`dma_wmb` → `dmb osh`/`dmb oshld`/`dmb oshst`. **outer shareable** — DMA 마스터까지. DMB라서 실행은 안 멈춘다.
- `smp_store_release` → `stlr`, `smp_load_acquire` → `ldar`. 배리어 명령 없이 접근 하나로 끝난다.
- `smp_store_mb(x, v)` → `str` + `dmb ish`. "쓰고 나서 full barrier" — 잠들기 전에 상태를 쓰고 조건을 다시 보는 `set_current_state()`가 이 패턴이다(F3).

```svg
<svg viewBox="0 0 680 564" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" font-size="14" text-anchor="middle">커널 배리어 매크로 → arm64 명령 (f5_macros.ko objdump, Linux 6.18.55)</text>
  <text x="22" y="46" font-size="12">매크로</text><text x="300" y="46" font-size="12">arm64 명령</text><text x="510" y="46" font-size="12">무엇을 막나</text>
  <rect x="10" y="58" width="660" height="50" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="22" y="76" font-size="12">barrier()</text>
  <text x="300" y="76" font-size="12">(명령 없음)</text>
  <text x="510" y="76" font-size="12">컴파일러 재배치 금지</text>
  <text x="22" y="98" font-size="12">READ_ONCE() / WRITE_ONCE()</text>
  <text x="300" y="98" font-size="12">ldr / str</text>
  <text x="510" y="98" font-size="12">한 번, 쪼개지 않게</text>
  <rect x="10" y="114" width="660" height="182" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="22" y="132" font-size="12">smp_mb(), smp_mb__*_atomic()</text>
  <text x="300" y="132" font-size="12">dmb ish</text>
  <text x="510" y="132" font-size="12">CPU 간 전부</text>
  <text x="22" y="154" font-size="12">smp_rmb()</text>
  <text x="300" y="154" font-size="12">dmb ishld</text>
  <text x="510" y="154" font-size="12">앞 load ↔ 뒤 접근</text>
  <text x="22" y="176" font-size="12">smp_wmb()</text>
  <text x="300" y="176" font-size="12">dmb ishst</text>
  <text x="510" y="176" font-size="12">store ↔ store</text>
  <text x="22" y="198" font-size="12">smp_store_mb(x, v)</text>
  <text x="300" y="198" font-size="12">str + dmb ish</text>
  <text x="510" y="198" font-size="12">store 후 full</text>
  <text x="22" y="220" font-size="12">smp_load_acquire(p)</text>
  <text x="300" y="220" font-size="12">ldar</text>
  <text x="510" y="220" font-size="12">뒤 접근이 못 올라옴</text>
  <text x="22" y="242" font-size="12">smp_store_release(p, v)</text>
  <text x="300" y="242" font-size="12">stlr</text>
  <text x="510" y="242" font-size="12">앞 접근이 못 내려감</text>
  <text x="22" y="264" font-size="12">atomic_inc_return()</text>
  <text x="300" y="264" font-size="12">ldaddal / LL-SC+dmb</text>
  <text x="510" y="264" font-size="12">앞뒤 full</text>
  <text x="22" y="286" font-size="12">atomic_inc(), *_relaxed()</text>
  <text x="300" y="286" font-size="12">stadd / ldadd</text>
  <text x="510" y="286" font-size="12">순서 없음</text>
  <rect x="10" y="302" width="660" height="72" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="22" y="320" font-size="12">dma_mb()</text>
  <text x="300" y="320" font-size="12">dmb osh</text>
  <text x="510" y="320" font-size="12">장치 포함 전부</text>
  <text x="22" y="342" font-size="12">dma_rmb()</text>
  <text x="300" y="342" font-size="12">dmb oshld</text>
  <text x="510" y="342" font-size="12">load ↔ 뒤 접근</text>
  <text x="22" y="364" font-size="12">dma_wmb()</text>
  <text x="300" y="364" font-size="12">dmb oshst</text>
  <text x="510" y="364" font-size="12">store ↔ store</text>
  <rect x="10" y="380" width="660" height="72" rx="5" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="22" y="398" font-size="12">mb()</text>
  <text x="300" y="398" font-size="12">dsb sy</text>
  <text x="510" y="398" font-size="12">완료까지 정지</text>
  <text x="22" y="420" font-size="12">rmb()</text>
  <text x="300" y="420" font-size="12">dsb ld</text>
  <text x="510" y="420" font-size="12">load 완료까지</text>
  <text x="22" y="442" font-size="12">wmb()</text>
  <text x="300" y="442" font-size="12">dsb st</text>
  <text x="510" y="442" font-size="12">store 완료까지</text>
  <rect x="10" y="458" width="660" height="72" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="22" y="476" font-size="12">readl()</text>
  <text x="300" y="476" font-size="12">ldr + dmb oshld + eor/cbnz</text>
  <text x="510" y="476" font-size="12">뒤 메모리 읽기·delay</text>
  <text x="22" y="498" font-size="12">writel()</text>
  <text x="300" y="498" font-size="12">dmb oshst + str</text>
  <text x="510" y="498" font-size="12">앞 메모리 쓰기 먼저</text>
  <text x="22" y="520" font-size="12">readl_relaxed() / writel_relaxed()</text>
  <text x="300" y="520" font-size="12">ldr / str</text>
  <text x="510" y="520" font-size="12">같은 장치끼리만</text>
  <text x="340" y="552" font-size="12" text-anchor="middle">ish = 같은 OS의 CPU들 · osh = 장치(DMA 마스터)까지 · sy = 시스템 전체 · dsb는 완료까지 실행을 멈춘다</text>
</svg>
```
그림 2 — 커널 배리어 매크로가 arm64에서 되는 명령(위 objdump와 §2.5, §5.2의 objdump를 한 장으로 모은 것). 색은 가족: 회색 = 컴파일러만, 파랑 = SMP(ISH), 초록 = DMA(OSH), 빨강 = 필수(DSB), 주황 = MMIO 접근자.

### 2.5 atomic과 `smp_mb__before_atomic()` / `__after_atomic()`

커널 atomic API의 순서 규칙(`Documentation/atomic_t.txt`):

- **값을 돌려주지 않는** RMW(`atomic_inc()`, `atomic_add()`, `set_bit()`)는 **relaxed** — 순서 보장 없음.
- **값을 돌려주는** RMW(`atomic_inc_return()`, `atomic_xchg()`, 성공한 `atomic_cmpxchg()`, `test_and_set_bit()`)는 **fully ordered** — 앞뒤로 `smp_mb()`가 있는 것처럼.
- `_relaxed`/`_acquire`/`_release` 접미사로 약하게 만들 수 있다.
- relaxed RMW에 순서가 필요하면 `smp_mb__before_atomic()`/`smp_mb__after_atomic()`을 붙인다. x86처럼 모든 locked 명령이 이미 full barrier인 아키텍처에선 이게 `barrier()`로 사라지고, arm64에선 진짜 `dmb ish`가 된다 — 그래서 별도 매크로가 있다.

두 번째 덩어리:

```text
0000000000000100 <f5_before_atomic>:
 100:	dmb	ish
 104:	b	120 <f5_before_atomic+0x20>
 108:	adrp	x0, 0 <f5_barrier>
 10c:	add	x0, x0, #0x0
 110:	mov	w1, #0x1                   	// #1
 114:	add	x0, x0, #0x10
 118:	stadd	w1, [x0]
 11c:	ret
 120:	adrp	x0, 0 <f5_barrier>
 124:	add	x0, x0, #0x0
 128:	add	x0, x0, #0x10
 12c:	prfm	pstl1strm, [x0]
 130:	ldxr	w1, [x0]
 134:	add	w1, w1, #0x1
 138:	stxr	w2, w1, [x0]
 13c:	cbnz	w2, 130 <f5_before_atomic+0x30>
 140:	ret
0000000000000144 <f5_after_atomic>:
 144:	b	164 <f5_after_atomic+0x20>
 148:	adrp	x0, 0 <f5_barrier>
 14c:	add	x0, x0, #0x0
 150:	mov	w1, #0x1                   	// #1
 154:	add	x0, x0, #0x10
 158:	stadd	w1, [x0]
 15c:	dmb	ish
 160:	ret
 164:	adrp	x0, 0 <f5_barrier>
 168:	add	x0, x0, #0x0
 16c:	add	x0, x0, #0x10
 170:	prfm	pstl1strm, [x0]
 174:	ldxr	w1, [x0]
 178:	add	w1, w1, #0x1
 17c:	stxr	w2, w1, [x0]
 180:	cbnz	w2, 174 <f5_after_atomic+0x30>
 184:	dmb	ish
 188:	ret
000000000000018c <f5_inc_return>:
 18c:	b	1b0 <f5_inc_return+0x24>
 190:	adrp	x0, 0 <f5_barrier>
 194:	add	x0, x0, #0x0
 198:	mov	w1, #0x1                   	// #1
 19c:	add	x2, x0, #0x10
 1a0:	ldaddal	w1, w1, [x2]
 1a4:	add	w1, w1, #0x1
 1a8:	str	w1, [x0, #8]
 1ac:	ret
 1b0:	adrp	x0, 0 <f5_barrier>
 1b4:	add	x0, x0, #0x0
 1b8:	add	x3, x0, #0x10
 1bc:	prfm	pstl1strm, [x3]
 1c0:	ldxr	w1, [x3]
 1c4:	add	w1, w1, #0x1
 1c8:	stlxr	w2, w1, [x3]
 1cc:	cbnz	w2, 1c0 <f5_inc_return+0x34>
 1d0:	dmb	ish
 1d4:	str	w1, [x0, #8]
 1d8:	ret
00000000000001e0 <f5_inc_relaxed>:
 1e0:	b	204 <f5_inc_relaxed+0x24>
 1e4:	adrp	x0, 0 <f5_barrier>
 1e8:	add	x0, x0, #0x0
 1ec:	mov	w1, #0x1                   	// #1
 1f0:	add	x2, x0, #0x10
 1f4:	ldadd	w1, w1, [x2]
 1f8:	add	w1, w1, #0x1
 1fc:	str	w1, [x0, #8]
 200:	ret
 204:	adrp	x0, 0 <f5_barrier>
 208:	add	x0, x0, #0x0
 20c:	add	x3, x0, #0x10
 210:	prfm	pstl1strm, [x3]
 214:	ldxr	w1, [x3]
 218:	add	w1, w1, #0x1
 21c:	stxr	w2, w1, [x3]
 220:	cbnz	w2, 214 <f5_inc_relaxed+0x34>
 224:	str	w1, [x0, #8]
 228:	ret
```

출력에서 볼 것, 이 객체 코드엔 **두 갈래**가 같이 있다:

- 각 함수 첫머리의 `b 120`(또는 `b 164`, `b 1b0`) 다음에 **LSE 경로**(`stadd`, `ldaddal` 한 명령)가 있고, 점프 대상에는 **LL/SC 경로**(`ldxr`/`stxr` 루프)가 있다. 기본은 LL/SC로 점프하는 것이고, 부팅 때 CPU가 LSE를 지원하면 커널이 그 `b`를 `nop`으로 **패치**한다(alternative). 아래 `.altinstructions` 재배치가 그 패치 목록이다 — 각 함수의 `b` 주소(`0x104`, `0x144`, `0x18c`, `0x1e0`)가 `alt_cb_patch_nops` 콜백과 짝지어져 있다.
- `smp_mb__before_atomic()` → `dmb ish` 후 `stadd`. `__after_atomic()` → `stadd` 후 `dmb ish`.
- `atomic_inc_return()`(값 반환 = fully ordered): LSE 경로는 `ldaddal` — `a`(acquire) + `l`(release)이 붙은 단일 명령. LL/SC 경로는 `ldxr` … `stlxr`(release store-exclusive) + 끝의 `dmb ish`. "release + 뒤에 full barrier"로 fully ordered를 만든다.
- `atomic_inc_return_relaxed()`: `ldadd`(접미사 없음) / `ldxr`+`stxr`, `dmb` 없음.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/mod && objdump -r -j .altinstructions f5_macros.ko | tail -n +5'
```
```text
OFFSET           TYPE              VALUE
0000000000000000 R_AARCH64_PREL32  .text+0x0000000000000104
0000000000000004 R_AARCH64_PREL32  alt_cb_patch_nops
000000000000000c R_AARCH64_PREL32  .text+0x0000000000000144
0000000000000010 R_AARCH64_PREL32  alt_cb_patch_nops
0000000000000018 R_AARCH64_PREL32  .text+0x000000000000018c
000000000000001c R_AARCH64_PREL32  alt_cb_patch_nops
0000000000000024 R_AARCH64_PREL32  .text+0x00000000000001e0
0000000000000028 R_AARCH64_PREL32  alt_cb_patch_nops
0000000000000030 R_AARCH64_PREL32  .text+0x0000000000000238
0000000000000034 R_AARCH64_PREL32  .text+0x00000000000002c8
000000000000003c R_AARCH64_PREL32  .text+0x000000000000026c
0000000000000040 R_AARCH64_PREL32  .text+0x00000000000002cc
```
출력에서 볼 것: 앞 네 쌍이 atomic의 LSE 패치(콜백 `alt_cb_patch_nops`), 뒤 두 쌍은 `readl`/`readl_relaxed`의 `ldr`(`0x238`, `0x26c`)을 `ldar`(`0x2c8`, `0x2cc`)로 바꿀 수 있게 걸어 둔 것이다(§5.2). QEMU의 cortex-a57은 LSE가 없으니 이 게스트에서는 LL/SC 경로가 실행된다. Don의 VM(M2)이나 Cortex-A55/A78 같은 v8.2+ 코어에서는 LSE 경로가 된다. **하나의 커널 바이너리가 두 세대의 CPU에서 각자 최선의 명령을 쓰는** 방법이다.

### 2.6 예제 4 — 배리어 비용 측정: `dmb` vs `dsb` vs `ldar`/`stlr`

"배리어는 비싸다"는 말을 숫자로 확인한다. store 하나 + 배리어 하나를 2천만 번 도는 루프의 1회 시간을 잰다(VM, vCPU 3에 고정, 경쟁 없음).

```c
// ex4_cost.c — "store 1개 + 배리어 1개" 루프의 1회 비용(ns). 배리어 종류별 비교.
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include "kbar.h"

#define N 20000000
static int buf[1024];
static double now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e9 + t.tv_nsec; }

#define BENCH(name, body) do { double t0 = now();                         \
    for (int i = 0; i < N; i++) { body; }                                  \
    printf("%-28s %6.2f ns\n", name, (now() - t0) / N); } while (0)

int main(void)
{
    int sink = 0;
    BENCH("str only (WRITE_ONCE)",     WRITE_ONCE(buf[i & 1023], i));
    BENCH("str + barrier()",           WRITE_ONCE(buf[i & 1023], i); barrier());
    BENCH("str + dmb ishst (smp_wmb)", WRITE_ONCE(buf[i & 1023], i); smp_wmb());
    BENCH("str + dmb ishld (smp_rmb)", WRITE_ONCE(buf[i & 1023], i); smp_rmb());
    BENCH("str + dmb ish (smp_mb)",    WRITE_ONCE(buf[i & 1023], i); smp_mb());
    BENCH("stlr (store_release)",      smp_store_release(&buf[i & 1023], i));
    BENCH("str + dmb oshst (dma_wmb)", WRITE_ONCE(buf[i & 1023], i); dmb(oshst));
    BENCH("str + dsb st (wmb)",        WRITE_ONCE(buf[i & 1023], i); dsb(st));
    BENCH("str + dsb sy (mb)",         WRITE_ONCE(buf[i & 1023], i); dsb(sy));
    BENCH("ldr only (READ_ONCE)",      sink += READ_ONCE(buf[i & 1023]));
    BENCH("ldar (load_acquire)",       sink += smp_load_acquire(&buf[i & 1023]));
    BENCH("ldapr (RCpc acquire)",      sink += load_acquire_pc(&buf[i & 1023]));
    return sink == 42;
}
```
```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/u && gcc -std=gnu11 -Wall -Wextra -O2 /Users/donh/.elinux_scratch/F5/u/ex4_cost.c -o ex4 && uptime > ex4_raw.txt; for r in 1 2 3 4 5 6 7; do taskset -c 3 ./ex4; done >> ex4_raw.txt; python3 /Users/donh/.elinux_scratch/F5/u/median.py ex4_raw.txt'
```
```text
15:37:03 up  5:39,  1 user,  load average: 0.88, 3.49, 6.49
str only (WRITE_ONCE)        median   0.34  min   0.32  max   0.38
str + barrier()              median   0.34  min   0.32  max   0.36
str + dmb ishst (smp_wmb)    median   0.74  min   0.61  max   1.11
str + dmb ishld (smp_rmb)    median   0.71  min   0.64  max   0.77
str + dmb ish (smp_mb)       median   0.68  min   0.63  max   0.80
stlr (store_release)         median   0.58  min   0.57  max   0.59
str + dmb oshst (dma_wmb)    median   0.71  min   0.64  max   0.76
str + dsb st (wmb)           median   5.35  min   5.16  max   5.50
str + dsb sy (mb)            median   5.32  min   5.24  max   5.60
ldr only (READ_ONCE)         median   0.41  min   0.40  max   0.42
ldar (load_acquire)          median   0.58  min   0.57  max   0.63
ldapr (RCpc acquire)         median   0.57  min   0.57  max   0.59
```
(첫 줄은 측정 시점의 `uptime`. `median.py`는 같은 이름의 행을 모아 중앙값·최소·최대를 내는 열 줄짜리 스크립트다.)

```svg
<svg viewBox="0 0 680 412" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" font-size="14" text-anchor="middle">루프 1회 비용 (ns, 경쟁 없는 단일 코어, 7회 중앙값)</text>
  <text x="202" y="66" font-size="12" text-anchor="end">str</text>
  <rect x="210" y="54" width="23.2" height="17" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="238.2" y="67" font-size="12">0.34</text>
  <text x="202" y="92" font-size="12" text-anchor="end">str + barrier()</text>
  <rect x="210" y="80" width="23.2" height="17" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="238.2" y="93" font-size="12">0.34</text>
  <text x="202" y="118" font-size="12" text-anchor="end">str + dmb ishst (smp_wmb)</text>
  <rect x="210" y="106" width="50.6" height="17" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="265.6" y="119" font-size="12">0.74</text>
  <text x="202" y="144" font-size="12" text-anchor="end">str + dmb ishld (smp_rmb)</text>
  <rect x="210" y="132" width="48.5" height="17" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="263.5" y="145" font-size="12">0.71</text>
  <text x="202" y="170" font-size="12" text-anchor="end">str + dmb ish (smp_mb)</text>
  <rect x="210" y="158" width="46.5" height="17" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="261.5" y="171" font-size="12">0.68</text>
  <text x="202" y="196" font-size="12" text-anchor="end">stlr (store_release)</text>
  <rect x="210" y="184" width="39.6" height="17" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
  <text x="254.6" y="197" font-size="12">0.58</text>
  <text x="202" y="222" font-size="12" text-anchor="end">str + dmb oshst (dma_wmb)</text>
  <rect x="210" y="210" width="48.5" height="17" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="263.5" y="223" font-size="12">0.71</text>
  <text x="202" y="248" font-size="12" text-anchor="end">str + dsb st (wmb)</text>
  <rect x="210" y="236" width="365.6" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="580.6" y="249" font-size="12">5.35</text>
  <text x="202" y="274" font-size="12" text-anchor="end">str + dsb sy (mb)</text>
  <rect x="210" y="262" width="363.5" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="578.5" y="275" font-size="12">5.32</text>
  <text x="202" y="300" font-size="12" text-anchor="end">ldr (READ_ONCE)</text>
  <rect x="210" y="288" width="28.0" height="17" fill="#888" fill-opacity="0.7" stroke="#888"/>
  <text x="243.0" y="301" font-size="12">0.41</text>
  <text x="202" y="326" font-size="12" text-anchor="end">ldar (load_acquire)</text>
  <rect x="210" y="314" width="39.6" height="17" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
  <text x="254.6" y="327" font-size="12">0.58</text>
  <text x="202" y="352" font-size="12" text-anchor="end">ldapr (RCpc)</text>
  <rect x="210" y="340" width="38.9" height="17" fill="#3f9a6b" fill-opacity="0.7" stroke="#3f9a6b"/>
  <text x="253.9" y="353" font-size="12">0.57</text>
  <line x1="210" y1="50" x2="210" y2="366" stroke="currentColor"/>
  <line x1="210" y1="366" x2="620" y2="366" stroke="currentColor"/>
  <line x1="210.0" y1="366" x2="210.0" y2="371" stroke="currentColor"/>
  <text x="210.0" y="385" font-size="12" text-anchor="middle">0</text>
  <line x1="278.3" y1="366" x2="278.3" y2="371" stroke="currentColor"/>
  <text x="278.3" y="385" font-size="12" text-anchor="middle">1</text>
  <line x1="346.7" y1="366" x2="346.7" y2="371" stroke="currentColor"/>
  <text x="346.7" y="385" font-size="12" text-anchor="middle">2</text>
  <line x1="415.0" y1="366" x2="415.0" y2="371" stroke="currentColor"/>
  <text x="415.0" y="385" font-size="12" text-anchor="middle">3</text>
  <line x1="483.3" y1="366" x2="483.3" y2="371" stroke="currentColor"/>
  <text x="483.3" y="385" font-size="12" text-anchor="middle">4</text>
  <line x1="551.7" y1="366" x2="551.7" y2="371" stroke="currentColor"/>
  <text x="551.7" y="385" font-size="12" text-anchor="middle">5</text>
  <line x1="620.0" y1="366" x2="620.0" y2="371" stroke="currentColor"/>
  <text x="620.0" y="385" font-size="12" text-anchor="middle">6</text>
  <text x="628" y="385" font-size="12">ns</text>
</svg>
```
그림 3 — 배리어 종류별 루프 1회 비용(VM의 M2 코어, 7회 중앙값). `dmb`류와 `stlr`/`ldar`는 0.2~0.4 ns를 더할 뿐이지만, `dsb`(`wmb()`/`mb()`)는 5 ns가 넘는다 — `dmb`의 7~8배. 경쟁 없는 단일 코어라 store buffer가 금방 비는 가장 싼 경우다. 캐시 미스가 쌓여 있거나 MMIO가 섞이면 `dsb`는 그 완료를 모두 기다리므로 훨씬 비싸진다.

출력에서 볼 것: 숫자는 시끄럽지만 순서는 일관된다. (1) `barrier()`는 공짜. (2) `stlr`은 `str + dmb ishst`보다 싸고, `ldar`/`ldapr`은 `ldr`보다 약간 비쌀 뿐이다 — 그래서 커널은 "가능하면 `smp_wmb`/`smp_rmb` 대신 release/acquire"를 권한다(LKMM 테스트 주석에도 그렇게 쓰여 있다, §3.2). (3) `wmb()`(`dsb st`)를 습관적으로 `smp_wmb()` 자리에 쓰면 매번 몇 배를 낸다. 반대로 `dma_wmb()`는 `smp_wmb()`와 비용이 비슷하다 — 같은 DMB이고 범위만 넓다.

---

## 3. LKMM과 litmus 테스트 — 순서를 "증명"하는 언어

### 3.1 LKMM이란

**LKMM(Linux Kernel Memory Model)**은 커널 소스 `tools/memory-model/`에 들어 있는 형식 메모리 모델이다. `linux-kernel.cat`(cat 언어로 쓴 공리들), `linux-kernel.bell`, `linux-kernel.def`(`smp_mb()` 같은 매크로를 모델 이벤트로 매핑)로 이뤄지고, 외부 도구 **herd7**이 litmus 테스트의 **모든 가능한 실행**을 열거해 "이 결과가 허용되나?"를 답한다. `klitmus7`은 같은 litmus를 커널 모듈로 바꿔 실제 하드웨어에서 돌린다.

LKMM은 **모든 아키텍처의 공통 분모**다. arm64에서 우연히 안 나는 결과라도 Power나 Alpha에서 나면 LKMM은 "허용(Sometimes)"이라고 답한다. 드라이버는 그 약한 보장에만 기대야 이식성이 있다.

```sh
limactl shell elinux -- bash -lc 'ls /var/lab/src/linux-6.18.55/tools/memory-model/ /var/lab/src/linux-6.18.55/tools/memory-model/Documentation; which herd7 klitmus7 || echo "herd7/klitmus7: not installed"'
```
```text
/var/lab/src/linux-6.18.55/tools/memory-model/:
Documentation
README
linux-kernel.bell
linux-kernel.cat
linux-kernel.cfg
linux-kernel.def
litmus-tests
lock.cat
scripts

/var/lab/src/linux-6.18.55/tools/memory-model/Documentation:
README
access-marking.txt
cheatsheet.txt
control-dependencies.txt
explanation.txt
glossary.txt
herd-representation.txt
litmus-tests.txt
locking.txt
ordering.txt
recipes.txt
references.txt
simple.txt
herd7/klitmus7: not installed
```
출력에서 볼 것: 모델 파일과 문서가 있다. 특히 `Documentation/simple.txt`(입문), `recipes.txt`(패턴별 처방), `cheatsheet.txt`(한 장 요약), `explanation.txt`(형식 정의 해설). herd7은 **설치돼 있지 않고, 이 노트에서는 설치하지 않았다**. 아래는 README에 실린 herd7 실행 예시를 인용한 것이다(실행 결과 아님).

```text
  $ herd7 -conf linux-kernel.cfg litmus-tests/SB+fencembonceonces.litmus

  Test SB+fencembonceonces Allowed
  States 3
  0:r0=0; 1:r0=1;
  0:r0=1; 1:r0=0;
  0:r0=1; 1:r0=1;
  No
  Witnesses
  Positive: 0 Negative: 3
  Condition exists (0:r0=0 /\ 1:r0=0)
  Observation SB+fencembonceonces Never 0 3
```
읽는 법: 가능한 최종 상태가 3개이고, `exists` 조건(둘 다 0)을 만족하는 상태는 0개(`Positive: 0`) → **Never**. 대신 우리는 §3.5에서 **실제 하드웨어로** 같은 질문을 던진다.

litmus 파일 이름에는 규칙이 있다: `MP+pooncerelease+poacquireonce` = MP 패턴, P0는 "po(프로그램 순서) + once(WRITE_ONCE) → release", P1은 "acquire → once". `fencewmb`는 `smp_wmb()`, `fencemb`는 `smp_mb()`, `ctrl`은 컨트롤 의존.

### 3.2 MP (message passing) — "데이터 쓰고 플래그"

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/tools/memory-model/litmus-tests; cat MP+poonceonces.litmus; grep -h -A6 "Result" MP+fencewmbonceonce+fencermbonceonce.litmus MP+pooncerelease+poacquireonce.litmus'
```
```text
C MP+poonceonces

(*
 * Result: Sometimes
 *
 * Can the counter-intuitive message-passing outcome be prevented with
 * no ordering at all?
 *)

{}

P0(int *buf, int *flag) // Producer
{
	WRITE_ONCE(*buf, 1);
	WRITE_ONCE(*flag, 1);
}

P1(int *buf, int *flag) // Consumer
{
	int r0;
	int r1;

	r0 = READ_ONCE(*flag);
	r1 = READ_ONCE(*buf);
}

exists (1:r0=1 /\ 1:r1=0) (* Bad outcome. *)
 * Result: Never
 *
 * This litmus test demonstrates that smp_wmb() and smp_rmb() provide
 * sufficient ordering for the message-passing pattern.  However, it
 * is usually better to use smp_store_release() and smp_load_acquire().
 *)

--
 * Result: Never
 *
 * This litmus test demonstrates that smp_store_release() and
 * smp_load_acquire() provide sufficient ordering for the message-passing
 * pattern.
 *)
```
출력에서 볼 것: 배리어 없는 MP는 "Sometimes", `smp_wmb()`+`smp_rmb()` 짝이나 release+acquire 짝이면 "Never". 그리고 주석 그대로 "**usually better to use `smp_store_release()` and `smp_load_acquire()`**".

```svg
<svg viewBox="0 0 680 380" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" font-size="14" text-anchor="middle">MP — 생산자는 buf→flag 순서로 쓰고, 소비자는 flag→buf 순서로 읽는다</text>
  <text x="160" y="50" font-size="13" text-anchor="middle">P0 (생산자, CPU1)</text>
  <text x="500" y="50" font-size="13" text-anchor="middle">P1 (소비자, CPU2)</text>
  <rect x="40" y="62" width="240" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="84" font-size="13" text-anchor="middle">WRITE_ONCE(buf, 1)</text>
  <rect x="40" y="110" width="240" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="160" y="130" font-size="12" text-anchor="middle">smp_wmb()  또는  release로 묶기</text>
  <rect x="40" y="154" width="240" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="160" y="176" font-size="13" text-anchor="middle">WRITE_ONCE(flag, 1)</text>
  <rect x="380" y="62" width="240" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="84" font-size="13" text-anchor="middle">r0 = READ_ONCE(flag)</text>
  <rect x="380" y="110" width="240" height="30" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="500" y="130" font-size="12" text-anchor="middle">smp_rmb()  또는  acquire로 묶기</text>
  <rect x="380" y="154" width="240" height="34" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="500" y="176" font-size="13" text-anchor="middle">r1 = READ_ONCE(buf)</text>
  <line x1="282" y1="171" x2="378" y2="81" stroke="#d0564a" stroke-width="2" marker-end="url(#ah3)"/>
  <text x="330" y="112" font-size="12" text-anchor="middle">rf</text>
  <line x1="378" y1="168" x2="282" y2="82" stroke="#888" stroke-dasharray="5 3" marker-end="url(#ah3)"/>
  <text x="330" y="150" font-size="12" text-anchor="middle">fr?</text>
  <text x="40" y="222" font-size="13">가능한 결과 (r0, r1)</text>
  <rect x="40" y="232" width="140" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="110" y="252" font-size="12" text-anchor="middle">(0, 0) 아직 안 옴</text>
  <rect x="190" y="232" width="140" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="260" y="252" font-size="12" text-anchor="middle">(0, 1) 중간</text>
  <rect x="340" y="232" width="140" height="30" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="410" y="252" font-size="12" text-anchor="middle">(1, 1) 정상 전달</text>
  <rect x="490" y="232" width="150" height="30" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="565" y="252" font-size="12" text-anchor="middle">(1, 0) 깨진 전달</text>
  <text x="40" y="292" font-size="12">(1, 0): flag는 새 값인데 buf는 옛 값 — 소비자가 쓰레기 데이터를 처리한다</text>
  <text x="40" y="314" font-size="12">배리어 없음: LKMM Sometimes, M2 실측 1천만 회당 수백 회 (§3.5)</text>
  <text x="40" y="336" font-size="12">한쪽만 (wmb만 / rmb만): 여전히 깨진다 — 짝이 맞아야 한다</text>
  <text x="40" y="358" font-size="12">wmb + rmb 짝, release + acquire 짝: LKMM Never, 실측 0</text>
</svg>
```
그림 4 — MP 패턴. 빨간 화살표(rf, reads-from)는 P1이 P0의 flag 쓰기를 읽었다는 뜻이다. 그런데도 P1이 buf의 옛 값(0)을 읽으면(회색 점선, fr = from-read) 깨진 전달이다. 막으려면 **쓰는 쪽의 store→store**와 **읽는 쪽의 load→load**를 둘 다 막아야 한다.

이건 Don의 DMA 디스크립터 패턴과 똑같은 모양이다. P0 = CPU, P1 = DMA 엔진, buf = 디스크립터 내용, flag = owner 비트. 그때 쓰는 배리어만 `dma_wmb()`로 바뀐다(§6).

### 3.3 SB (store buffering) — "나는 쓰고 너를 읽는다"

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/tools/memory-model/litmus-tests; cat SB+poonceonces.litmus | sed -n "/^P0/,\$p"; grep -B2 -A6 "Result" SB+fencembonceonces.litmus'
```
```text
P0(int *x, int *y)
{
	int r0;

	WRITE_ONCE(*x, 1);
	r0 = READ_ONCE(*y);
}

P1(int *x, int *y)
{
	int r0;

	WRITE_ONCE(*y, 1);
	r0 = READ_ONCE(*x);
}

exists (0:r0=0 /\ 1:r0=0)

(*
 * Result: Never
 *
 * This litmus test demonstrates that full memory barriers suffice to
 * order the store-buffering pattern, where each process writes to the
 * variable that the preceding process reads.  (Locking and RCU can also
 * suffice, but not much else.)
 *)
```
출력에서 볼 것: SB는 **full barrier(`smp_mb()`)만** 막는다고 주석이 말한다("Locking and RCU can also suffice, but not much else"). `smp_wmb()`/`smp_rmb()`는 store→load 순서를 다루지 않으므로 소용없다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" font-size="14" text-anchor="middle">SB — 각 코어의 store가 store buffer에 머무는 동안 load가 먼저 끝난다</text>
  <rect x="30" y="44" width="220" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="140" y="66" font-size="13" text-anchor="middle">CPU1 (P0)</text>
  <text x="140" y="90" font-size="12" text-anchor="middle">① WRITE_ONCE(x, 1)</text>
  <text x="140" y="112" font-size="12" text-anchor="middle">② r0 = READ_ONCE(y) → 0</text>
  <text x="140" y="138" font-size="12" text-anchor="middle">(smp_mb()가 ①을 비운 뒤 ②)</text>
  <rect x="430" y="44" width="220" height="110" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="540" y="66" font-size="13" text-anchor="middle">CPU2 (P1)</text>
  <text x="540" y="90" font-size="12" text-anchor="middle">① WRITE_ONCE(y, 1)</text>
  <text x="540" y="112" font-size="12" text-anchor="middle">② r0 = READ_ONCE(x) → 0</text>
  <text x="540" y="138" font-size="12" text-anchor="middle">(smp_mb()가 ①을 비운 뒤 ②)</text>
  <rect x="50" y="180" width="180" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="140" y="205" font-size="12" text-anchor="middle">store buffer: x = 1 대기</text>
  <rect x="450" y="180" width="180" height="40" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="540" y="205" font-size="12" text-anchor="middle">store buffer: y = 1 대기</text>
  <line x1="140" y1="154" x2="140" y2="178" stroke="currentColor" marker-end="url(#ah4)"/>
  <line x1="540" y1="154" x2="540" y2="178" stroke="currentColor" marker-end="url(#ah4)"/>
  <rect x="200" y="260" width="280" height="44" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="280" font-size="13" text-anchor="middle">캐시 / 메모리</text>
  <text x="340" y="297" font-size="12" text-anchor="middle">x = 0, y = 0 (아직 아무도 안 보임)</text>
  <line x1="230" y1="130" x2="300" y2="258" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah4)"/>
  <line x1="450" y1="130" x2="380" y2="258" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#ah4)"/>
  <line x1="140" y1="220" x2="230" y2="270" stroke="#e08a3c" marker-end="url(#ah4)"/>
  <line x1="540" y1="220" x2="450" y2="270" stroke="#e08a3c" marker-end="url(#ah4)"/>
  <text x="340" y="334" font-size="12" text-anchor="middle">빨간 점선 = ② load가 캐시에서 옛 값 0을 읽음 · 주황 = store buffer가 나중에 비워짐</text>
  <text x="340" y="352" font-size="12" text-anchor="middle">결과 (r0, r0) = (0, 0): 어떤 순차 실행으로도 설명 불가 — x86에서도 일어난다</text>
</svg>
```
그림 5 — SB 패턴과 store buffer. 각 CPU는 자기 store를 store buffer에 넣고 곧바로 load를 실행한다. 자기 store는 store forwarding으로 자기에게는 보이지만, 상대에게는 아직 안 보인다. 두 CPU가 동시에 그러면 둘 다 0을 읽는다. 막는 방법은 **store buffer를 비울 때까지 load를 기다리게 하는 full barrier**뿐이다.

SB가 커널 어디에 나오나: "나는 잠들겠다고 표시하고(store) → 깨울 조건을 다시 본다(load)" vs "조건을 만들고(store) → 자는 사람이 있나 본다(load)". `set_current_state()` + 조건 검사 vs 조건 설정 + `wake_up()`(F3)이 정확히 SB다. 그래서 `set_current_state()`가 `smp_store_mb()`(store + `smp_mb()`)이고, waker 쪽도 full barrier를 쓴다. 이걸 놓치면 **lost wakeup**이 난다.

### 3.4 LB (load buffering)와 의존성

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/tools/memory-model/litmus-tests; sed -n "/^P0/,\$p" LB+poonceonces.litmus; sed -n "/^P0/,\$p" LB+fencembonceonce+ctrlonceonce.litmus'
```
```text
P0(int *x, int *y)
{
	int r0;

	r0 = READ_ONCE(*x);
	WRITE_ONCE(*y, 1);
}

P1(int *x, int *y)
{
	int r0;

	r0 = READ_ONCE(*y);
	WRITE_ONCE(*x, 1);
}

exists (0:r0=1 /\ 1:r0=1)
P0(int *x, int *y)
{
	int r0;

	r0 = READ_ONCE(*x);
	if (r0)
		WRITE_ONCE(*y, 1);
}

P1(int *x, int *y)
{
	int r0;

	r0 = READ_ONCE(*y);
	smp_mb();
	WRITE_ONCE(*x, 1);
}

exists (0:r0=1 /\ 1:r0=1)
```
출력에서 볼 것: LB는 "각자 상대가 **나중에** 쓸 값을 먼저 읽는" 결과(둘 다 1)다. 시간 역행처럼 보이지만, CPU가 load가 끝나기 전에 뒤의 독립적인 store를 먼저 내보내면 가능하다. LKMM과 Arm 아키텍처는 이를 **허용**한다. 그런데 두 번째 파일처럼 P0의 store가 load 결과에 **컨트롤 의존**(`if (r0)`)하고 P1이 `smp_mb()`를 쓰면 Never다. 투기 실행된 store는 다른 CPU에게 보이지 않으므로, 하드웨어는 컨트롤 의존 load→store 순서를 지킨다.

의존성 순서(dependency ordering)는 배리어 없이 얻는 순서다.

- **주소 의존**: `p = READ_ONCE(gp); v = READ_ONCE(p->x);` — 두 번째 load의 주소가 첫 load 결과에 달려 있다. Alpha를 뺀 모든 CPU가 이 순서를 지킨다. RCU의 `rcu_dereference()`가 여기에 기댄다(F4).
- **컨트롤 의존**: `if (READ_ONCE(x)) WRITE_ONCE(y, 1);` — load→store만 순서가 생긴다(load→load는 아님). 컴파일러가 분기를 없애면(양쪽 분기에 같은 store가 있을 때 등) 의존이 사라지므로 매우 깨지기 쉽다. `Documentation/control-dependencies.txt` 참고. 드라이버에서는 기대지 말 것.

### 3.5 예제 5 — MP·SB·LB를 실제 arm64 코어에서 1천만 번씩 (유저스페이스, 커널 매크로 그대로)

A8의 MP 실험은 큰 배열을 반대 방향으로 훑는 방식이라 약 5천만 번에 3~7번 관찰됐다. 이번엔 litmus7 방식처럼 **슬롯마다 두 스레드가 스핀 배리어로 출발을 맞추고**, 각 슬롯의 x, y를 서로 다른 캐시 라인에 둔다. 스레드는 vCPU 1과 2에 고정한다. 변형 11개를 한 바이너리에 넣었다.

```c
// ex3_litmus.c — MP / SB / LB litmus를 커널 매크로(kbar.h)로 실제 arm64에서 돌린다.
// 사용: ./ex3 <variant> <iters>   매 슬롯마다 두 스레드가 스핀 배리어로 출발을 맞춘다.
#define _GNU_SOURCE
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kbar.h"

struct slot { int x; char p1[60]; int y; char p2[60]; } __attribute__((aligned(64)));
#define K 100000                                  /* 배치당 슬롯 수 */
static struct slot *s; static int r0[K], r1[K];
static atomic_int arrive; static int V;           /* V = 변형 번호 */
static const char *names[] = { "MP relaxed", "MP wmb only", "MP rmb only", "MP wmb+rmb",
    "MP release+acquire", "SB relaxed", "SB wmb", "SB mb", "SB release+acquire(ldar)",
    "SB release+acquire(ldapr)", "LB relaxed" };
#define NV (int)(sizeof(names) / sizeof(names[0]))

static void sync_slot(int i) { atomic_fetch_add(&arrive, 1); while (atomic_load_explicit(&arrive, memory_order_relaxed) < 2 * (i + 1)) ; }
static void pin(int cpu) { cpu_set_t c; CPU_ZERO(&c); CPU_SET(cpu, &c); if (pthread_setaffinity_np(pthread_self(), sizeof c, &c)) { perror("affinity"); exit(1); } }

static void *p0(void *a) { (void)a; pin(1);
    for (int i = 0; i < K; i++) { struct slot *q = &s[i]; sync_slot(i);
        switch (V) {
        case 0: case 2: WRITE_ONCE(q->x, 1); WRITE_ONCE(q->y, 1); break;     /* MP writer */
        case 1: case 3: WRITE_ONCE(q->x, 1); smp_wmb(); WRITE_ONCE(q->y, 1); break;
        case 4: WRITE_ONCE(q->x, 1); smp_store_release(&q->y, 1); break;
        case 5: WRITE_ONCE(q->x, 1); r0[i] = READ_ONCE(q->y); break;          /* SB P0 */
        case 6: WRITE_ONCE(q->x, 1); smp_wmb(); r0[i] = READ_ONCE(q->y); break;
        case 7: WRITE_ONCE(q->x, 1); smp_mb();  r0[i] = READ_ONCE(q->y); break;
        case 8: smp_store_release(&q->x, 1); r0[i] = smp_load_acquire(&q->y); break;
        case 9: smp_store_release(&q->x, 1); r0[i] = load_acquire_pc(&q->y); break;
        case 10: r0[i] = READ_ONCE(q->x); WRITE_ONCE(q->y, 1); break;        /* LB P0 */
        } }
    return NULL; }

static void *p1(void *a) { (void)a; pin(2);
    for (int i = 0; i < K; i++) { struct slot *q = &s[i]; sync_slot(i);
        switch (V) {
        case 0: case 1: r0[i] = READ_ONCE(q->y); r1[i] = READ_ONCE(q->x); break; /* MP reader */
        case 2: case 3: r0[i] = READ_ONCE(q->y); smp_rmb(); r1[i] = READ_ONCE(q->x); break;
        case 4: r0[i] = smp_load_acquire(&q->y); r1[i] = READ_ONCE(q->x); break;
        case 5: WRITE_ONCE(q->y, 1); r1[i] = READ_ONCE(q->x); break;            /* SB P1 */
        case 6: WRITE_ONCE(q->y, 1); smp_wmb(); r1[i] = READ_ONCE(q->x); break;
        case 7: WRITE_ONCE(q->y, 1); smp_mb();  r1[i] = READ_ONCE(q->x); break;
        case 8: smp_store_release(&q->y, 1); r1[i] = smp_load_acquire(&q->x); break;
        case 9: smp_store_release(&q->y, 1); r1[i] = load_acquire_pc(&q->x); break;
        case 10: r1[i] = READ_ONCE(q->y); WRITE_ONCE(q->x, 1); break;           /* LB P1 */
        } }
    return NULL; }

int main(int argc, char **argv)
{
    if (argc < 3 || (V = atoi(argv[1])) < 0 || V >= NV) { fprintf(stderr, "usage: %s 0..%d iters\n", argv[0], NV - 1); return 1; }
    long iters = atol(argv[2]), hist[4] = {0}, weird = 0;
    if (!(s = aligned_alloc(64, sizeof(struct slot) * K))) { perror("alloc"); return 1; }
    for (long done = 0; done < iters; done += K) {
        memset(s, 0, sizeof(struct slot) * K); memset(r0, 0, sizeof r0); memset(r1, 0, sizeof r1);
        atomic_store(&arrive, 0);
        pthread_t a, b;
        if (pthread_create(&a, NULL, p0, NULL) || pthread_create(&b, NULL, p1, NULL)) { perror("create"); return 1; }
        pthread_join(a, NULL); pthread_join(b, NULL);
        for (int i = 0; i < K; i++) {
            int u = r0[i], w = r1[i];                 /* MP: (flag,data)  SB/LB: (P0 r, P1 r) */
            hist[u * 2 + w]++;
            if (V <= 4 ? (u == 1 && w == 0) : V <= 9 ? (u == 0 && w == 0) : (u == 1 && w == 1)) weird++;
        }
    }
    printf("%-26s 00=%-8ld 01=%-8ld 10=%-8ld 11=%-8ld forbidden-by-SC=%ld\n", names[V], hist[0], hist[1], hist[2], hist[3], weird);
    free(s); return 0;
}
```
(이 예제는 harness 때문에 60줄을 조금 넘는다. 실험 본체는 `p0`/`p1`의 `switch` 한 줄씩이다.)

먼저 각 변형이 의도한 명령이 됐는지 본다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/u && gcc -std=gnu11 -Wall -Wextra -O2 -pthread /Users/donh/.elinux_scratch/F5/u/ex3_litmus.c -o ex3 && objdump -d --no-show-raw-insn ex3 | sed -n "/<p0>:/,/^\$/p" | grep -E "dmb|stlr|ldar|ldapr"; echo; objdump -d --no-show-raw-insn ex3 | sed -n "/<p1>:/,/^\$/p" | grep -E "dmb|stlr|ldar|ldapr"'
```
```text
    13ec:	dmb	ishst
    1450:	stlr	w24, [x21]
    1458:	ldapr	w0, [x0]
    1464:	stlr	w24, [x21]
    146c:	ldar	w0, [x0]
    147c:	dmb	ish
    1490:	dmb	ishst
    14b8:	stlr	w24, [x0]

    123c:	dmb	ishld
    1294:	stlr	w25, [x0]
    1298:	ldar	w0, [x21]
    12d8:	dmb	ishst
    12ec:	stlr	w25, [x0]
    12f0:	ldapr	w0, [x21]
    1300:	dmb	ish
    1324:	ldar	w0, [x0]
```
출력에서 볼 것: 위가 `p0`, 아래가 `p1`. `dmb ishst`(wmb), `dmb ishld`(rmb, 두 case가 공유해서 하나), `dmb ish`(mb), `stlr`+`ldar`, `stlr`+`ldapr`이 다 있다. 배리어가 없는 case는 평범한 `ldr`/`str`이다(grep에서 빠짐).

이제 1천만 번씩, 세 번 돌린다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/u && uptime && for r in 1 2 3; do echo "--- run $r"; for v in 0 1 2 3 4 5 6 7 8 9 10; do ./ex3 $v 10000000; done; done'
```
```text
 15:14:43 up  5:17,  1 user,  load average: 5.42, 3.17, 3.90
--- run 1
MP relaxed                 00=5011952  01=8912     10=1307     11=4977829  forbidden-by-SC=1307
MP wmb only                00=4121013  01=4145651  10=637      11=1732699  forbidden-by-SC=637
MP rmb only                00=4996376  01=35102    10=33       11=4968489  forbidden-by-SC=33
MP wmb+rmb                 00=3334178  01=4231387  10=0        11=2434435  forbidden-by-SC=0
MP release+acquire         00=4996958  01=14541    10=0        11=4988501  forbidden-by-SC=0
SB relaxed                 00=10489    01=4994200  10=4994528  11=783      forbidden-by-SC=10489
SB wmb                     00=11676    01=4993175  10=4994305  11=844      forbidden-by-SC=11676
SB mb                      00=0        01=4997303  10=4994779  11=7918     forbidden-by-SC=0
SB release+acquire(ldar)   00=0        01=4998193  10=4986057  11=15750    forbidden-by-SC=0
SB release+acquire(ldapr)  00=30701    01=4990889  10=4970446  11=7964     forbidden-by-SC=30701
LB relaxed                 00=31533    01=4986236  10=4982231  11=0        forbidden-by-SC=0
--- run 2
MP relaxed                 00=5004990  01=3366     10=561      11=4991083  forbidden-by-SC=561
MP wmb only                00=4181503  01=3867226  10=428      11=1950843  forbidden-by-SC=428
MP rmb only                00=4996549  01=35760    10=17       11=4967674  forbidden-by-SC=17
MP wmb+rmb                 00=3338627  01=4146168  10=0        11=2515205  forbidden-by-SC=0
MP release+acquire         00=4997872  01=16616    10=0        11=4985512  forbidden-by-SC=0
SB relaxed                 00=12801    01=4992972  10=4993444  11=783      forbidden-by-SC=12801
SB wmb                     00=8491     01=4994999  10=4995962  11=548      forbidden-by-SC=8491
SB mb                      00=0        01=4996382  10=4993679  11=9939     forbidden-by-SC=0
SB release+acquire(ldar)   00=0        01=4998443  10=4986197  11=15360    forbidden-by-SC=0
SB release+acquire(ldapr)  00=31910    01=4985407  10=4975302  11=7381     forbidden-by-SC=31910
LB relaxed                 00=18938    01=4991441  10=4989621  11=0        forbidden-by-SC=0
--- run 3
MP relaxed                 00=5004741  01=4928     10=761      11=4989570  forbidden-by-SC=761
MP wmb only                00=4166287  01=4052962  10=472      11=1780279  forbidden-by-SC=472
MP rmb only                00=4995300  01=44265    10=33       11=4960402  forbidden-by-SC=33
MP wmb+rmb                 00=3440040  01=3992842  10=0        11=2567118  forbidden-by-SC=0
MP release+acquire         00=4997664  01=18195    10=0        11=4984141  forbidden-by-SC=0
SB relaxed                 00=18450    01=4990110  10=4989555  11=1885     forbidden-by-SC=18450
SB wmb                     00=17327    01=4989670  10=4991366  11=1637     forbidden-by-SC=17327
SB mb                      00=0        01=4996923  10=4994354  11=8723     forbidden-by-SC=0
SB release+acquire(ldar)   00=0        01=4995796  10=4989490  11=14714    forbidden-by-SC=0
SB release+acquire(ldapr)  00=34291    01=4983209  10=4974719  11=7781     forbidden-by-SC=34291
LB relaxed                 00=22244    01=4990305  10=4987451  11=0        forbidden-by-SC=0
```
출력에서 볼 것(열 이름: MP는 `(flag, data)`, SB·LB는 `(P0의 r, P1의 r)`; `forbidden-by-SC` = 순차 일관성이라면 불가능한 결과의 수):

- **MP relaxed**: 1천만 번에 561~1307번 `flag=1, data=0`. A8보다 100배 이상 잘 잡힌다 — 슬롯마다 출발을 맞춘 덕이다.
- **MP wmb만**: 여전히 428~637번. 쓰는 쪽 순서를 고쳐도 **읽는 쪽 load→load 재배치**가 남아 있다.
- **MP rmb만**: 17~33번. 읽는 쪽을 고쳐도 **쓰는 쪽 store→store 재배치**가 남아 있다. 두 재배치가 모두 이 코어에서 실제로 일어난다는 증거다.
- **MP wmb+rmb**, **MP release+acquire**: 0. 짝이 맞으면 막힌다.
- **SB relaxed**: 10,489~18,450번 둘 다 0 — MP보다 열 배 이상 흔하다. store buffer는 어느 CPU에나 있으니까.
- **SB wmb**: 거의 그대로. `dmb ishst`는 store끼리만 순서를 맞추므로 store→load에 무력하다.
- **SB mb**: 0. full barrier만이 SB를 막는다.
- **SB stlr+ldar**: 0, **SB stlr+ldapr**: 30,701~34,291번. 다음 절에서 설명한다.
- **LB relaxed**: 0. 아키텍처는 허용하지만 이 코어에서 3천만 번 동안 한 번도 안 나왔다. "허용되지만 관찰 안 됨"은 "안전"이 아니다 — 다음 세대 코어나 다른 벤더에서는 나올 수 있다.

```svg
<svg viewBox="0 0 680 386" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" font-size="14" text-anchor="middle">SC에서 금지된 결과가 나온 횟수 (1천만 회당, 3회 중앙값, 로그 축)</text>
  <text x="182" y="66" font-size="12" text-anchor="end">MP relaxed</text>
  <rect x="190" y="54" width="253.6" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="448.6" y="67" font-size="12">761</text>
  <text x="182" y="92" font-size="12" text-anchor="end">MP smp_wmb만</text>
  <rect x="190" y="80" width="235.4" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="430.4" y="93" font-size="12">472</text>
  <text x="182" y="118" font-size="12" text-anchor="end">MP smp_rmb만</text>
  <rect x="190" y="106" width="134.8" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="329.8" y="119" font-size="12">33</text>
  <text x="182" y="144" font-size="12" text-anchor="end">MP smp_wmb + smp_rmb</text>
  <text x="195" y="145" font-size="12">0</text>
  <text x="182" y="170" font-size="12" text-anchor="end">MP release + acquire</text>
  <text x="195" y="171" font-size="12">0</text>
  <text x="182" y="196" font-size="12" text-anchor="end">SB relaxed</text>
  <rect x="190" y="184" width="361.4" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="556.4" y="197" font-size="12">12,801</text>
  <text x="182" y="222" font-size="12" text-anchor="end">SB smp_wmb</text>
  <rect x="190" y="210" width="357.9" height="17" fill="#d0564a" fill-opacity="0.7" stroke="#d0564a"/>
  <text x="552.9" y="223" font-size="12">11,676</text>
  <text x="182" y="248" font-size="12" text-anchor="end">SB smp_mb</text>
  <text x="195" y="249" font-size="12">0</text>
  <text x="182" y="274" font-size="12" text-anchor="end">SB stlr + ldar</text>
  <text x="195" y="275" font-size="12">0</text>
  <text x="182" y="300" font-size="12" text-anchor="end">SB stlr + ldapr (RCpc)</text>
  <rect x="190" y="288" width="396.3" height="17" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="591.3" y="301" font-size="12">31,910</text>
  <text x="182" y="326" font-size="12" text-anchor="end">LB relaxed</text>
  <text x="195" y="327" font-size="12">0</text>
  <line x1="190" y1="50" x2="190" y2="340" stroke="currentColor"/>
  <line x1="190" y1="340" x2="630" y2="340" stroke="currentColor"/>
  <line x1="190.0" y1="340" x2="190.0" y2="345" stroke="currentColor"/>
  <text x="190.0" y="359" font-size="12" text-anchor="middle">0</text>
  <line x1="278.0" y1="340" x2="278.0" y2="345" stroke="currentColor"/>
  <text x="278.0" y="359" font-size="12" text-anchor="middle">10</text>
  <line x1="366.0" y1="340" x2="366.0" y2="345" stroke="currentColor"/>
  <text x="366.0" y="359" font-size="12" text-anchor="middle">100</text>
  <line x1="454.0" y1="340" x2="454.0" y2="345" stroke="currentColor"/>
  <text x="454.0" y="359" font-size="12" text-anchor="middle">1k</text>
  <line x1="542.0" y1="340" x2="542.0" y2="345" stroke="currentColor"/>
  <text x="542.0" y="359" font-size="12" text-anchor="middle">10k</text>
  <line x1="630.0" y1="340" x2="630.0" y2="345" stroke="currentColor"/>
  <text x="630.0" y="359" font-size="12" text-anchor="middle">100k</text>
  <line x1="190" y1="179" x2="630" y2="179" stroke="#888" stroke-dasharray="4 3"/>
  <line x1="190" y1="309" x2="630" y2="309" stroke="#888" stroke-dasharray="4 3"/>
</svg>
```
그림 6 — 변형별 "금지된 결과" 횟수(1천만 회당, 3회 중앙값, 로그 축 — 0은 막대 없이 0으로 표시). 빨강 = 배리어가 없거나 맞지 않는 짝, 초록 = 올바른 짝, 주황 = RCpc acquire로는 SB를 막지 못하는 경우, 회색 = LB.

### 3.6 `stlr`→`ldar`는 순서를 지키고 `stlr`→`ldapr`는 안 지킨다 — 그리고 LKMM은 둘 다 믿지 말라고 한다

SB release+acquire 결과가 갈린 이유:

- Arm의 `LDAR`은 **RCsc**(release consistency, sequentially consistent): 프로그램 순서상 앞에 있는 `STLR`이 완료되기 전에 `LDAR`이 끝나지 않는다. 그래서 `stlr x; ldar y`는 SB를 막는다 — 측정 0.
- `LDAPR`(ARMv8.3 LRCPC)은 **RCpc**: 앞의 `STLR`과의 순서를 보장하지 않는다. 그래서 store buffer에 `stlr`이 머무는 동안 `ldapr`이 먼저 끝난다 — 측정 3만 번.

그럼 커널 코드에서 `smp_store_release(); smp_load_acquire();`로 SB를 막을 수 있나? **아니다.** LKMM에서 release→acquire(다른 변수)는 store→load 순서를 주지 않는다. 지금 arm64 커널의 `smp_load_acquire()`가 `ldar`이라서 우연히 막히는 것뿐이다. 실제로 arm64 커널은 `CONFIG_LTO`에서 `READ_ONCE`를 `ldapr`로 바꾸고, RCpc를 쓰는 아키텍처(Power의 `lwsync` 기반 acquire 등)에선 SB가 그대로 일어난다. **SB에는 `smp_mb()`.** 측정이 이 규칙의 이유를 보여 준다.

### 3.7 커널에 들어 있는 litmus 테스트 전체 결과표

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/tools/memory-model/litmus-tests; grep -H "Result:" *.litmus | sed "s/.litmus: \* Result://"'
```
```text
CoRR+poonceonce+Once Never
CoRW+poonceonce+Once Never
CoWR+poonceonce+Once Never
CoWW+poonceonce Never
IRIW+fencembonceonces+OnceOnce Never
IRIW+poonceonces+OnceOnce Sometimes
ISA2+pooncelock+pooncelock+pombonce Never
ISA2+poonceonces Sometimes
ISA2+pooncerelease+poacquirerelease+poacquireonce Never
LB+fencembonceonce+ctrlonceonce Never
LB+poacquireonce+pooncerelease Never
LB+poonceonces Sometimes
LB+unlocklockonceonce+poacquireonce Never
MP+fencewmbonceonce+fencermbonceonce Never
MP+onceassign+derefonce Never
MP+polockmbonce+poacquiresilsil Never
MP+polockonce+poacquiresilsil Sometimes
MP+polocks Never
MP+poonceonces Sometimes
MP+pooncerelease+poacquireonce Never
MP+porevlocks Never
MP+unlocklockonceonce+fencermbonceonce Never
R+fencembonceonces Never
R+poonceonces Sometimes
S+fencewmbonceonce+poacquireonce Never
S+poonceonces Sometimes
SB+fencembonceonces Never
SB+poonceonces Sometimes
SB+rfionceonce-poonceonces Sometimes
WRC+poonceonces+Once Sometimes
WRC+pooncerelease+fencermbonceonce+Once Never
Z6.0+pooncelock+poonceLock+pombonce Never
Z6.0+pooncelock+pooncelock+pombonce Sometimes
Z6.0+pooncerelease+poacquirerelease+fencembonceonce Sometimes
dep+plain Never
```
드라이버 작성자에게 중요한 줄만 골라 정리하면:

| 패턴 | 배리어 없이 | 이렇게 하면 Never | 실무 장면 |
|---|---|---|---|
| MP | Sometimes | `smp_wmb`+`smp_rmb`, release+acquire, 락 | 데이터 채우고 "준비됨" 플래그 |
| SB | Sometimes | 양쪽 `smp_mb()`, 락 | sleep/wakeup, 양방향 플래그 |
| LB | Sometimes | 컨트롤 의존 + `smp_mb`, acquire+release | 드묾 |
| IRIW | Sometimes | 양쪽 읽는 쪽 `smp_mb()` | 세 CPU 이상이 순서에 합의해야 할 때 |
| WRC, ISA2 | Sometimes | release-acquire 사슬 | 전달의 전달 (A→B→C) |
| Z6.0 | Sometimes | non-rf 링크마다 full barrier | release-acquire 사슬로는 부족한 경우 |
| R, S | Sometimes | `smp_mb`, `smp_wmb`+acquire | store 순서 vs 관찰 |
| CoRR/CoRW/CoWR/CoWW | Never | — | 같은 변수는 coherence가 지켜 준다 |

맨 마지막 줄이 중요하다: **같은 변수 하나**에 대한 접근 순서는 하드웨어 coherence가 항상 지킨다. 배리어는 **서로 다른 변수 사이**의 순서를 위한 것이다.

또 하나: `Z6.0+pooncerelease+poacquirerelease+fencembonceonce`가 release-acquire 사슬에 `smp_mb()`까지 넣고도 **Sometimes**인 이유를 파일 주석이 설명한다 — 사슬 중 "상대의 쓰기를 읽어서(rf) 이어지는" 링크가 아닌 링크(non-rf: 같은 변수에 대한 쓰기 순서 co, 옛 값을 읽은 fr)가 둘 이상이면, 그런 링크마다 full barrier가 필요하다. release/acquire는 "값을 읽어서 전달되는" 사슬에만 강하다.

---

## 4. 커널 안에서 litmus — QEMU `-smp 4`에서 `f5_barrier` 모듈

### 4.1 예제 6 — kthread 두 개를 CPU1·CPU2에 묶어 MP·SB 돌리기

같은 실험을 커널 코드로 옮긴다. `kthread_create_on_cpu()`로 스레드를 특정 CPU에 묶고, 끝나면 `completion`으로 알린다(F3). 결과만 찍고 `-EAGAIN`을 돌려 로드를 실패시키므로 `rmmod`가 필요 없다.

```c
// f5_barrier.c — 커널 안에서 MP/SB litmus: CPU1·CPU2에 묶은 kthread 두 개, 슬롯마다 출발 동기화
#include <linux/module.h>
#include <linux/kthread.h>
#include <linux/vmalloc.h>
#include <linux/completion.h>

static int iters = 200000;
module_param(iters, int, 0444);
struct f5_slot { int x; int pad1[15]; int y; int pad2[15]; } __aligned(64);
static struct f5_slot *s;
static int *r0, *r1, v;
static atomic_t arrive;
static DECLARE_COMPLETION(d0);
static DECLARE_COMPLETION(d1);
static const char *const names[] = { "MP relaxed", "MP smp_wmb+smp_rmb", "MP release+acquire",
				     "SB relaxed", "SB smp_mb", "SB release+acquire" };

static void sync_slot(int i)
{
	atomic_inc(&arrive);
	while (atomic_read(&arrive) < 2 * (i + 1))
		cpu_relax();
}

static int t0(void *a)
{
	for (int i = 0; i < iters; i++) {
		struct f5_slot *q = &s[i];
		sync_slot(i);
		switch (v) {
		case 0: WRITE_ONCE(q->x, 1); WRITE_ONCE(q->y, 1); break;
		case 1: WRITE_ONCE(q->x, 1); smp_wmb(); WRITE_ONCE(q->y, 1); break;
		case 2: WRITE_ONCE(q->x, 1); smp_store_release(&q->y, 1); break;
		case 3: WRITE_ONCE(q->x, 1); r0[i] = READ_ONCE(q->y); break;
		case 4: WRITE_ONCE(q->x, 1); smp_mb(); r0[i] = READ_ONCE(q->y); break;
		case 5: smp_store_release(&q->x, 1); r0[i] = smp_load_acquire(&q->y); break;
		}
	}
	complete(&d0);
	return 0;
}

static int t1(void *a)
{
	for (int i = 0; i < iters; i++) {
		struct f5_slot *q = &s[i];
		sync_slot(i);
		switch (v) {
		case 0: r0[i] = READ_ONCE(q->y); r1[i] = READ_ONCE(q->x); break;
		case 1: r0[i] = READ_ONCE(q->y); smp_rmb(); r1[i] = READ_ONCE(q->x); break;
		case 2: r0[i] = smp_load_acquire(&q->y); r1[i] = READ_ONCE(q->x); break;
		case 3: WRITE_ONCE(q->y, 1); r1[i] = READ_ONCE(q->x); break;
		case 4: WRITE_ONCE(q->y, 1); smp_mb(); r1[i] = READ_ONCE(q->x); break;
		case 5: smp_store_release(&q->y, 1); r1[i] = smp_load_acquire(&q->x); break;
		}
	}
	complete(&d1);
	return 0;
}

static int __init f5_init(void)
{
	s = vzalloc(sizeof(*s) * iters);
	r0 = vzalloc(sizeof(int) * iters);
	r1 = vzalloc(sizeof(int) * iters);
	if (!s || !r0 || !r1)
		goto out;
	for (v = 0; v < ARRAY_SIZE(names); v++) {
		long h[4] = { 0 };
		ktime_t t = ktime_get();

		memset(s, 0, sizeof(*s) * iters); memset(r0, 0, sizeof(int) * iters);
		memset(r1, 0, sizeof(int) * iters); atomic_set(&arrive, 0);
		reinit_completion(&d0); reinit_completion(&d1);
		wake_up_process(kthread_create_on_cpu(t0, NULL, 1, "f5_t0/%u"));
		wake_up_process(kthread_create_on_cpu(t1, NULL, 2, "f5_t1/%u"));
		wait_for_completion(&d0); wait_for_completion(&d1);
		for (int i = 0; i < iters; i++)
			h[r0[i] * 2 + r1[i]]++;
		pr_info("f5: %-20s 00=%-7ld 01=%-7ld 10=%-7ld 11=%-7ld (%lld ms)\n", names[v],
			h[0], h[1], h[2], h[3], ktime_ms_delta(ktime_get(), t));
	}
out:
	vfree(s); vfree(r0); vfree(r1);
	return -EAGAIN;            /* 결과만 찍고 로드는 실패시킨다 (rmmod 불필요) */
}
module_init(f5_init);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("F5: MP/SB litmus inside the kernel");
```
(harness까지 한 파일이라 60줄을 넘는다. 실제 실험은 `t0`/`t1`의 `switch` 한 줄씩이다. 실무 코드라면 `kthread_create_on_cpu()`의 `IS_ERR()` 검사를 빠뜨리면 안 된다 — 여기선 실험 코드라 생략했다.)

모듈 안에서도 매크로가 의도한 명령이 됐는지 먼저 확인한다.

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/mod && for f in t0 t1; do echo "=== $f"; objdump -d --no-show-raw-insn f5_barrier.ko | sed -n "/<$f>:/,/^\$/p" | grep -E "dmb|stlr|ldar"; done'
```
```text
=== t0
 268:	stlr	w12, [x10]
 270:	ldar	w1, [x0]
 2b0:	stlr	w12, [x0]
 2d0:	dmb	ishst
 2e0:	dmb	ish
=== t1
 114:	stlr	w12, [x0]
 118:	ldar	w1, [x10]
 154:	ldar	w1, [x0]
 17c:	dmb	ishld
 194:	dmb	ish
```
출력에서 볼 것: `t0`에 SB용 `stlr`→`ldar` 쌍(`268`→`270`), MP용 `stlr`(`2b0`), `dmb ishst`, `dmb ish`. `t1`에 `stlr`→`ldar`(`114`→`118`), MP용 `ldar`(`154`), `dmb ishld`, `dmb ish`. 유저스페이스 ex3와 같은 명령이다.

게스트 스크립트 `g2.sh`는 `insmod /f5_barrier.ko iters=200000; dmesg | grep "f5:"`(타임스탬프 제거)다. MTTCG로 세 번, 단일 스레드 TCG로 한 번 부팅한다.

```sh
cd ~/.elinux_scratch/F5 && for m in multi multi multi single; do echo "##### thread=$m"; limactl shell elinux -- bash -lc "sh /Users/donh/.elinux_scratch/F5/boot.sh /Users/donh/.elinux_scratch/F5/g2.sh $m" 2>&1 | grep -E "f5:|insmod"; done
```
```text
##### thread=multi
insmod: can't insert '/f5_barrier.ko': Resource temporarily unavailable
f5: MP relaxed           00=100081  01=440     10=6       11=99473   (37 ms)
f5: MP smp_wmb+smp_rmb   00=99644   01=1912    10=0       11=98444   (38 ms)
f5: MP release+acquire   00=99663   01=900     10=0       11=99437   (38 ms)
f5: SB relaxed           00=1213    01=99201   10=99549   11=37      (40 ms)
f5: SB smp_mb            00=0       01=99164   10=100125  11=711     (37 ms)
f5: SB release+acquire   00=280     01=99425   10=100291  11=4       (37 ms)
##### thread=multi
insmod: can't insert '/f5_barrier.ko': Resource temporarily unavailable
f5: MP relaxed           00=99616   01=987     10=1       11=99396   (35 ms)
f5: MP smp_wmb+smp_rmb   00=99270   01=1541    10=0       11=99189   (36 ms)
f5: MP release+acquire   00=99101   01=1749    10=0       11=99150   (37 ms)
f5: SB relaxed           00=793     01=99767   10=99438   11=2       (37 ms)
f5: SB smp_mb            00=0       01=99404   10=98943   11=1653    (36 ms)
f5: SB release+acquire   00=2231    01=98812   10=98951   11=6       (37 ms)
##### thread=multi
insmod: can't insert '/f5_barrier.ko': Resource temporarily unavailable
f5: MP relaxed           00=99968   01=410     10=6       11=99616   (37 ms)
f5: MP smp_wmb+smp_rmb   00=99440   01=2302    10=0       11=98258   (38 ms)
f5: MP release+acquire   00=99622   01=1124    10=0       11=99254   (36 ms)
f5: SB relaxed           00=1018    01=99346   10=99630   11=6       (35 ms)
f5: SB smp_mb            00=0       01=99350   10=99460   11=1190    (35 ms)
f5: SB release+acquire   00=1031    01=98716   10=100251  11=2       (37 ms)
##### thread=single
insmod: can't insert '/f5_barrier.ko': Resource temporarily unavailable
f5: MP relaxed           00=100000  01=0       10=0       11=100000  (61 ms)
f5: MP smp_wmb+smp_rmb   00=100000  01=0       10=0       11=100000  (58 ms)
f5: MP release+acquire   00=100000  01=0       10=0       11=100000  (64 ms)
f5: SB relaxed           00=0       01=100000  10=100000  11=0       (64 ms)
f5: SB smp_mb            00=0       01=100000  10=100000  11=0       (62 ms)
f5: SB release+acquire   00=0       01=100000  10=100000  11=0       (60 ms)
```
`insmod: can't insert ... Resource temporarily unavailable`은 일부러 돌려준 `-EAGAIN`이다.

### 4.2 결과를 정직하게 읽기 — QEMU는 메모리 순서 검증 도구가 아니다

출력에서 볼 것, 세 가지:

- **MTTCG(`thread=multi`)에서는 재배치가 보인다.** MP relaxed 1~6번(20만 회 중), SB relaxed 793~1213번. QEMU가 guest의 `ldr`/`str`을 호스트(M2)의 `ldr`/`str`로 번역하고 vCPU마다 호스트 스레드가 따로 돌기 때문에, **호스트 CPU의 weak ordering이 게스트로 비쳐 들어온다**. `smp_wmb`+`smp_rmb`, `smp_mb`는 0 — QEMU가 게스트 `dmb`를 호스트 배리어로 번역한다.
- **그런데 SB release+acquire(`stlr`→`ldar`)가 280~2231번 깨졌다.** 진짜 arm64 코어에서는 아키텍처가 금지하는 결과다(§3.5에서 M2로 3천만 번 0). 즉 QEMU TCG는 `stlr`→`ldar`의 RCsc 순서를 **재현하지 못한다**. 원인은 추정이다 — TCG가 `stlr`을 "배리어 + store", `ldar`를 "load + 배리어"로 번역하면 store→load 사이가 비게 된다. QEMU 소스는 이 VM에 없어 **확인하지 않았다**.
- **단일 스레드 TCG(`thread=single`)에서는 아무것도 재배치되지 않는다.** 결과가 00 아니면 11, 01 아니면 10으로 딱 반씩이다. 호스트 스레드 하나가 vCPU를 번갈아 실행하니 진짜 동시 실행이 없다. 이 모드에서 통과한 테스트는 순서에 대해 아무것도 증명하지 않는다.

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg">
  <text x="340" y="22" font-size="14" text-anchor="middle">같은 litmus, 실제 M2 코어 vs QEMU TCG(-smp 4): 금지된 결과 / 100만 회 (로그 축)</text>
  <rect x="150" y="34" width="14" height="12" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="170" y="45" font-size="12">VM 유저스페이스 (Apple M2, ex3)</text>
  <rect x="400" y="34" width="14" height="12" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="420" y="45" font-size="12">QEMU MTTCG 게스트 커널 (f5_barrier)</text>
  <rect x="78.3" y="217.0" width="26" height="83.0" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="91.3" y="213.0" font-size="12" text-anchor="middle">76</text>
  <rect x="108.3" y="234.4" width="26" height="65.6" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="121.3" y="230.4" font-size="12" text-anchor="middle">30</text>
  <text x="108.3" y="320" font-size="12" text-anchor="middle">MP relaxed</text>
  <text x="188.0" y="296" font-size="12" text-anchor="middle">0</text>
  <text x="218.0" y="296" font-size="12" text-anchor="middle">0</text>
  <text x="205.0" y="320" font-size="12" text-anchor="middle">MP wmb+rmb</text>
  <text x="284.7" y="296" font-size="12" text-anchor="middle">0</text>
  <text x="314.7" y="296" font-size="12" text-anchor="middle">0</text>
  <text x="301.7" y="320" font-size="12" text-anchor="middle">MP rel+acq</text>
  <rect x="368.3" y="163.3" width="26" height="136.7" fill="#4a7bd0" fill-opacity="0.7" stroke="#4a7bd0"/>
  <text x="381.3" y="159.3" font-size="12" text-anchor="middle">1,280</text>
  <rect x="398.3" y="136.9" width="26" height="163.1" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="411.3" y="132.9" font-size="12" text-anchor="middle">5,090</text>
  <text x="398.3" y="320" font-size="12" text-anchor="middle">SB relaxed</text>
  <text x="478.0" y="296" font-size="12" text-anchor="middle">0</text>
  <text x="508.0" y="296" font-size="12" text-anchor="middle">0</text>
  <text x="495.0" y="320" font-size="12" text-anchor="middle">SB smp_mb</text>
  <text x="574.7" y="296" font-size="12" text-anchor="middle">0</text>
  <rect x="591.7" y="136.7" width="26" height="163.3" fill="#e08a3c" fill-opacity="0.7" stroke="#e08a3c"/>
  <text x="604.7" y="132.7" font-size="12" text-anchor="middle">5,155</text>
  <text x="591.7" y="320" font-size="12" text-anchor="middle">SB stlr+ldar</text>
  <line x1="60" y1="300" x2="640" y2="300" stroke="currentColor"/>
  <line x1="60" y1="80" x2="60" y2="300" stroke="currentColor"/>
  <line x1="55" y1="300.0" x2="60" y2="300.0" stroke="currentColor"/>
  <text x="52" y="304.0" font-size="12" text-anchor="end">0</text>
  <line x1="55" y1="256.0" x2="60" y2="256.0" stroke="currentColor"/>
  <text x="52" y="260.0" font-size="12" text-anchor="end">10</text>
  <line x1="55" y1="212.0" x2="60" y2="212.0" stroke="currentColor"/>
  <text x="52" y="216.0" font-size="12" text-anchor="end">100</text>
  <line x1="55" y1="168.0" x2="60" y2="168.0" stroke="currentColor"/>
  <text x="52" y="172.0" font-size="12" text-anchor="end">1k</text>
  <line x1="55" y1="124.0" x2="60" y2="124.0" stroke="currentColor"/>
  <text x="52" y="128.0" font-size="12" text-anchor="end">10k</text>
  <line x1="55" y1="80.0" x2="60" y2="80.0" stroke="currentColor"/>
  <text x="52" y="84.0" font-size="12" text-anchor="end">100k</text>
  <text x="340" y="348" font-size="12" text-anchor="middle">SB stlr+ldar: 실제 arm64는 0 (아키텍처가 금지), QEMU는 relaxed 수준으로 깨진다</text>
</svg>
```
그림 7 — 같은 litmus를 실제 코어(VM, 1천만 회 × 3)와 QEMU MTTCG 게스트(20만 회 × 3)에서 돌린 결과를 100만 회당으로 맞춘 비교(중앙값). 배리어가 없는 경우는 둘 다 깨지지만, `stlr`+`ldar` SB에서 QEMU만 깨진다. 반대 방향의 위험도 있다 — QEMU가 하드웨어보다 **강하게** 동작하는 경우(예: 단일 스레드 TCG) 버그를 숨긴다.

교훈: **메모리 순서 버그는 실제 하드웨어(가능하면 타깃 SoC)에서, LKMM으로 논증하고, 스트레스 테스트로 확인한다.** QEMU에서 0이 나왔다고 안전한 게 아니고, QEMU에서 깨졌다고 하드웨어에서 깨지는 것도 아니다. KCSAN은 QEMU에서도 의미가 있다 — 순서가 아니라 "표시 안 된 동시 접근"을 잡는 도구라서.

---

## 5. MMIO 순서 — `readl`/`writel` vs `_relaxed`

### 5.1 Device 메모리 속성부터

`ioremap()`이 돌려준 가상 주소는 페이지 테이블에 **Device-nGnRE** 속성으로 매핑된다(랩 소스 `arch/arm64/include/asm/io.h` 285행: `__ioremap_prot(..., PROT_DEVICE_nGnRE)`). Don이 Cortex-R/M에서 MPU로 설정하던 "Device" 영역과 같은 개념이다.

| 글자 | 뜻 | 결과 |
|---|---|---|
| nG (non-Gathering) | 여러 접근을 하나로 합치지 않음 | 레지스터 두 번 쓰기가 한 번이 되지 않는다 |
| nR (non-Reordering) | 같은 장치(주변장치 블록)에 대한 접근끼리 순서 유지 | 같은 장치의 레지스터 쓰기·읽기가 프로그램 순서대로 도착 |
| E (Early write ack) | 쓰기 완료 응답을 중간(인터커넥트)에서 일찍 줄 수 있음 | **posted write** — CPU는 "끝났다"고 보지만 장치에는 아직 안 도착 |

변형: `ioremap_wc()`는 Normal-NC(write-combining, 합치기·재배치 허용 — 프레임버퍼), `ioremap_np()`는 Device-nGnRnE(non-posted — 일부 플랫폼 전용, `Documentation/driver-api/device-io.rst`: PCI BAR에는 쓰면 안 된다, PCI 메모리 쓰기는 항상 posted이므로).

중요한 것: Device 속성이 주는 순서는 **같은 장치에 대한 MMIO끼리**뿐이다. **일반 메모리(DMA 버퍼) 접근과 MMIO 접근 사이**의 순서는 아무도 보장하지 않는다. 그걸 메워 주는 게 `readl`/`writel` 안의 배리어다.

### 5.2 `readl`/`writel`의 해부 — 랩 소스와 objdump

```c
/* include/asm-generic/io.h (랩 소스, tracepoint 줄 생략) */
static inline u32 readl(const volatile void __iomem *addr)
{
	u32 val;
	__io_br();
	val = __le32_to_cpu((__le32 __force)__raw_readl(addr));
	__io_ar(val);
	return val;
}
static inline void writel(u32 value, volatile void __iomem *addr)
{
	__io_bw();
	__raw_writel((u32 __force)__cpu_to_le32(value), addr);
	__io_aw();
}
/* readl_relaxed()/writel_relaxed()는 __io_b?/__io_a? 없이 __raw_* 만 부른다 */
```
```c
/* arch/arm64/include/asm/io.h (랩 소스, 핵심만) */
static __always_inline u32 __raw_readl(const volatile void __iomem *addr)
{
	u32 val;
	asm volatile(ALTERNATIVE("ldr %w0, [%1]",
				 "ldar %w0, [%1]",
				 ARM64_WORKAROUND_DEVICE_LOAD_ACQUIRE)
		     : "=r" (val) : "r" (addr));
	return val;
}
#define __io_ar(v)							\
({									\
	unsigned long tmp;						\
	dma_rmb();							\
	/* Create a dummy control dependency from the IO read to any	\
	 * later instructions. This ensures that a subsequent call to	\
	 * udelay() will be ordered due to the ISB in get_cycles(). */	\
	asm volatile("eor	%0, %1, %1\n"				\
		     "cbnz	%0, ."					\
		     : "=r" (tmp) : "r" ((unsigned long)(v))		\
		     : "memory");					\
})
#define __io_bw()		dma_wmb()
#define __io_br(v)
#define __io_aw(v)
```

즉 arm64에서

- `writel` = `dma_wmb()`(`dmb oshst`) + `str`. **앞의 일반 메모리 쓰기**(예: 디스크립터)가 장치에 보이기 전에 MMIO 쓰기가 나가지 않게 한다.
- `readl` = `ldr` + `dma_rmb()`(`dmb oshld`) + 가짜 컨트롤 의존(`eor x, v, v` → 항상 0, `cbnz`). **뒤의 일반 메모리 읽기**(예: DMA로 들어온 데이터)가 MMIO 읽기보다 먼저 실행되지 않게 하고, 뒤에 오는 `udelay()`가 읽기 완료 뒤에 시작되게 한다.
- `__raw_readl`의 `ldr`은 ARM erratum 832075(Cortex-A57 초기 리비전, `cpu_errata.c`의 설명 문자열 "ARM erratum 832075")가 있는 CPU에서 부팅 시 `ldar`로 패치된다. §2.5의 `.altinstructions` 마지막 두 쌍이 그것이다.

§2.4의 모듈에서 실제 명령을 본 세 번째 덩어리:

```text
000000000000022c <f5_readl>:
 22c:	adrp	x0, 0 <f5_barrier>
 230:	add	x0, x0, #0x0
 234:	ldr	x1, [x0, #24]
 238:	ldr	w1, [x1]
 23c:	dmb	oshld
 240:	mov	w2, w1
 244:	eor	x2, x2, x2
 248:	cbnz	x2, 248 <f5_readl+0x1c>
 24c:	str	w1, [x0, #8]
 250:	ret
0000000000000260 <f5_readl_relaxed>:
 260:	adrp	x0, 0 <f5_barrier>
 264:	add	x0, x0, #0x0
 268:	ldr	x1, [x0, #24]
 26c:	ldr	w1, [x1]
 270:	str	w1, [x0, #8]
 274:	ret
0000000000000280 <f5_writel>:
 280:	adrp	x0, 0 <f5_barrier>
 284:	ldr	x1, [x0]
 288:	dmb	oshst
 28c:	mov	w0, #0x1                   	// #1
 290:	str	w0, [x1]
 294:	ret
00000000000002a0 <f5_writel_relaxed>:
 2a0:	adrp	x0, 0 <f5_barrier>
 2a4:	ldr	x1, [x0]
 2a8:	mov	w0, #0x1                   	// #1
 2ac:	str	w0, [x1]
 2b0:	ret
00000000000002c0 <f5_isb>:
 2c0:	isb
 2c4:	ret
 2c8:	ldar	w1, [x1]
 2cc:	ldar	w1, [x1]
```
출력에서 볼 것: `f5_readl`은 `ldr w1, [x1]`(MMIO 읽기) → `dmb oshld` → `eor x2, x2, x2` + `cbnz x2, 248`(자기 자신으로 가는, 절대 안 타는 분기 = 가짜 의존) 순서. `f5_readl_relaxed`는 `ldr` 하나. `f5_writel`은 `dmb oshst` → `str`. `f5_writel_relaxed`는 `str` 하나. 마지막 `ldar w1, [x1]` 두 줄(`2c8`, `2cc`)은 함수가 아니라 erratum용 **대체 명령**이 `.text` 끝에 놓인 것이다.

```svg
<svg viewBox="0 0 680 340" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah5" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" font-size="14" text-anchor="middle">arm64 MMIO 접근자 안에 든 배리어 (Linux 6.18)</text>
  <text x="20" y="58" font-size="13">writel(v, reg)</text>
  <rect x="140" y="40" width="150" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="215" y="60" font-size="12" text-anchor="middle">dmb oshst (dma_wmb)</text>
  <line x1="290" y1="55" x2="318" y2="55" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="320" y="40" width="150" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="395" y="60" font-size="12" text-anchor="middle">str w, [reg]</text>
  <text x="140" y="90" font-size="12">앞의 일반 메모리 쓰기(디스크립터·DMA 버퍼)가 장치보다 먼저 보이게</text>
  <text x="20" y="138" font-size="13">readl(reg)</text>
  <rect x="140" y="120" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="140" font-size="12" text-anchor="middle">ldr w, [reg]</text>
  <line x1="260" y1="135" x2="288" y2="135" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="290" y="120" width="160" height="30" rx="5" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="370" y="140" font-size="12" text-anchor="middle">dmb oshld (dma_rmb)</text>
  <line x1="450" y1="135" x2="478" y2="135" stroke="currentColor" marker-end="url(#ah5)"/>
  <rect x="480" y="120" width="180" height="30" rx="5" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="570" y="140" font-size="12" text-anchor="middle">eor + cbnz (가짜 의존)</text>
  <text x="140" y="170" font-size="12">뒤의 일반 메모리 읽기(DMA로 들어온 데이터)가 앞지르지 못하게</text>
  <text x="140" y="188" font-size="12">뒤의 udelay()가 읽기 완료 후에 시작되게</text>
  <text x="20" y="236" font-size="13">*_relaxed()</text>
  <rect x="140" y="218" width="120" height="30" rx="5" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="200" y="238" font-size="12" text-anchor="middle">str / ldr 하나</text>
  <text x="275" y="238" font-size="12">배리어 없음</text>
  <text x="140" y="268" font-size="12">같은 장치에 대한 MMIO끼리의 순서만 (Device-nGnRE의 nR이 보장)</text>
  <rect x="20" y="288" width="640" height="40" rx="5" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="313" font-size="12" text-anchor="middle">모든 MMIO는 posted일 수 있다(E 속성) — 장치 도착을 확인하려면 같은 장치를 readl로 읽는다</text>
</svg>
```
그림 8 — arm64 `writel`/`readl`/`_relaxed`의 명령 구성. 배리어는 MMIO 접근의 **바깥쪽**(writel은 앞, readl은 뒤)에 붙는다. 일반 메모리와의 순서를 맞추는 것이 목적이기 때문이다.

### 5.3 `memory-barriers.txt`의 다섯 가지 보장

랩 소스 `Documentation/memory-barriers.txt`의 "KERNEL I/O BARRIER EFFECTS" 절은 `ioremap()` 매핑에 대한 `readX()`/`writeX()`의 보장을 다섯 개로 정리한다(요약).

1. 같은 장치에 대한 `readX()`/`writeX()`는 서로 순서가 지켜진다.
2. 스핀락을 잡고 한 `writeX()`는, 같은 락을 나중에 잡은 다른 CPU의 `writeX()`보다 장치에 먼저 도착한다.
3. `writeX()`는 같은 스레드가 앞서 한(또는 그 스레드에 전파된) **모든 메모리 쓰기의 완료를 먼저 기다린다** — `dma_alloc_coherent()` 버퍼에 쓴 내용이 DMA 시작 레지스터 쓰기보다 먼저 보인다.
4. `readX()`는 같은 스레드의 **뒤따르는 메모리 읽기가 시작되기 전에 완료된다** — DMA 완료 상태 레지스터를 읽은 뒤 coherent 버퍼를 읽으면 옛 데이터가 아니다.
5. `readX()`는 뒤따르는 `delay()` 루프가 시작되기 전에 완료된다.

`readX_relaxed()`/`writeX_relaxed()`는 **1번만** 보장한다. 2~5번이 필요하면 직접 배리어를 넣거나 non-relaxed를 쓴다.

§5.2의 명령이 정확히 이 보장을 구현한다: 3번 = `writel`의 `dmb oshst`, 4번 = `readl`의 `dmb oshld`, 5번 = 가짜 컨트롤 의존(+ `get_cycles()` 안의 `isb`). 2번은 아키텍처마다 구현이 다르다. arm64는 스핀락 unlock의 `stlr`(release)이 앞의 MMIO 쓰기까지 순서 지어 준다고 보고 별도 처리가 없다. 반면 랩 소스에서 riscv와 ppc64는 `ARCH_HAS_MMIOWB`를 선택해, 스핀락 unlock 때 `include/asm-generic/mmiowb.h`의 추적 장치가 필요한 경우에만 MMIO 쓰기 배리어를 넣는다. 예전 드라이버 코드에 보이는 `mmiowb()` 호출이 이 일을 드라이버가 직접 하던 흔적이다.

### 5.4 Posted write와 read-back — Don의 PCIe 지식 그대로

PCIe Memory Write는 **posted** 트랜잭션이다(completion이 없다). CPU의 `str`은 Root Complex에 넘기는 순간 끝난 것으로 치고 다음 명령으로 간다. 그래서

```c
writel(CTRL_RESET, base + CTRL);     /* 장치 리셋 */
udelay(10);                          /* "10 us 기다렸다"고 믿지만… */
writel(CTRL_ENABLE, base + CTRL);    /* 쓰기 두 개가 거의 붙어서 도착할 수 있다 */
```

에서 첫 쓰기가 아직 스위치나 브리지 버퍼에 있는 동안 `udelay(10)`이 시작되고 끝날 수 있다. 장치 입장에선 리셋과 enable 사이 간격이 10 us보다 짧다. 해결은 Don이 알던 그대로 **같은 장치에서 아무 레지스터나 읽는 것**(read-back)이다. PCIe 순서 규칙상 Memory Read 요청은 같은 경로의 앞선 posted write를 추월할 수 없고, 읽기는 non-posted라 completion이 돌아와야 끝난다.

```c
writel(CTRL_RESET, base + CTRL);
readl(base + CTRL);                  /* flush: 앞의 posted write가 장치에 도착했다 */
udelay(10);                          /* readl 안의 가짜 의존 덕에 읽기 완료 후 시작 (5번 보장) */
writel(CTRL_ENABLE, base + CTRL);
```

`memory-barriers.txt`의 5번 보장 예시가 정확히 이 모양이다(`writel(42, DEVICE_REGISTER_0); readl(DEVICE_REGISTER_0); udelay(1); writel(42, DEVICE_REGISTER_1);`). 같은 이유로 **인터럽트 마스크 레지스터에 쓴 직후 `free_irq()`나 락 해제를 하기 전**에도 read-back을 넣는다 — 그렇지 않으면 "마스크했는데 인터럽트가 하나 더 들어오는" 경쟁이 생긴다.

주의: read-back은 **같은 장치**여야 한다. 다른 장치를 읽어도 다른 경로라 앞의 posted write를 밀어내지 않는다. 그리고 `wmb()`(`dsb st`)는 posted write를 장치까지 밀어내지 **않는다** — DSB는 "CPU 쪽에서 완료"를 기다릴 뿐이고, Early write ack 때문에 그 "완료"가 인터커넥트 어딘가에서 이미 났을 수 있다.

예제로 확인한다. 진짜 PCIe 장치가 없으니 QEMU virt의 PL031 RTC Match 레지스터(`0x09010004`, 쓰기·읽기 가능, 알람을 켜지 않으면 무해)를 빌려 쓴다. §6의 모듈 `f5_doorbell`이 doorbell 쓰기 뒤 같은 레지스터를 `readl`로 다시 읽어 값을 확인한다(출력은 §6.2). TCG 에뮬레이션이라 posted write의 **시간 효과**는 측정할 수 없다. 여기서 확인하는 건 패턴과 명령뿐이다.

### 5.5 `_relaxed`는 언제 써도 되나

| 상황 | 써도 되나 | 이유 |
|---|---|---|
| 같은 장치의 레지스터 여러 개 설정(초기화 시퀀스) | ✅ `writel_relaxed` | 같은 장치끼리는 nR이 순서를 지킨다 |
| 그 시퀀스의 **마지막** "시작" 비트 | 마지막 하나만 `writel` | 앞의 DMA 버퍼·디스크립터 쓰기와 순서가 필요 |
| 고속 FIFO를 반복해서 읽기 (`readsl`) | ✅ | DMA 데이터와 무관 |
| DMA 완료 상태를 읽고 바로 버퍼를 읽기 | ❌ `readl` | 4번 보장이 필요 |
| 인터럽트 핸들러의 status 읽기 후 clear 쓰기 | ✅ (같은 장치) | 단, 그 결과로 메모리 버퍼를 읽으면 `readl` 또는 `dma_rmb()` |
| `_relaxed` 쓰다가 일반 메모리와 순서가 필요해짐 | `wmb()`/`rmb()` 또는 `dma_wmb()`+`writel` | 필수 배리어 |

실무 팁: 드라이버 초기화처럼 느린 경로는 그냥 `readl`/`writel`. 프로파일링으로 hot path가 확인된 곳만 `_relaxed`로 바꾸고 주석으로 이유를 남긴다. 랩 소스 `drivers/` 전체에서 `writel_relaxed(`가 4,914줄, `writel(`이 30,867줄 나온다(§8.3의 grep).

---

## 6. DMA 순서 — 디스크립터, owner 비트, doorbell

### 6.1 고전 패턴

`memory-barriers.txt`의 `dma_wmb()` 설명에 실린 예시(랩 소스 1928~1945행)가 Don이 SSD 펌웨어에서 매일 쓰던 패턴이다.

```c
	if (desc->status != DEVICE_OWN) {
		/* do not read data until we own descriptor */
		dma_rmb();

		/* read/modify data */
		read_data = desc->data;
		desc->data = write_data;

		/* flush modifications before status update */
		dma_wmb();

		/* assign ownership */
		desc->status = DEVICE_OWN;

		/* Make descriptor status visible to the device followed by
		 * notify device of new descriptor
		 */
		writel(DESC_NOTIFY, doorbell);
	}
```

순서를 지켜야 하는 곳이 세 군데다.

- owner를 확인한 **뒤에** 내용을 읽기: `dma_rmb()`. 장치가 내용을 쓰고 status를 바꿨어도 CPU가 내용을 먼저(투기적으로) 읽었다면 옛 내용이다. MP의 읽는 쪽.
- 내용을 쓴 **뒤에** owner 넘기기: `dma_wmb()`. MP의 쓰는 쪽.
- owner를 넘긴 **뒤에** doorbell: `writel()` 안의 `dmb oshst`가 해 준다. `writel_relaxed()`를 쓴다면 그 앞에 `wmb()` 또는 `dma_wmb()`가 필요하다.

같은 문서의 경고: **`dma_*()` 배리어는 MMIO에 대한 순서는 주지 않는다.** 그건 `readl`/`writel` 또는 필수 배리어 몫이다.

```svg
<svg viewBox="0 0 680 420" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" font-size="14" text-anchor="middle">디스크립터 넘기기 — CPU · coherent 메모리 · 장치 사이의 순서</text>
  <text x="110" y="48" font-size="13" text-anchor="middle">CPU (드라이버)</text>
  <text x="340" y="48" font-size="13" text-anchor="middle">coherent 메모리 (디스크립터)</text>
  <text x="570" y="48" font-size="13" text-anchor="middle">장치 (DMA 엔진)</text>
  <line x1="110" y1="56" x2="110" y2="400" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="340" y1="56" x2="340" y2="400" stroke="#888" stroke-dasharray="3 3"/>
  <line x1="570" y1="56" x2="570" y2="400" stroke="#888" stroke-dasharray="3 3"/>
  <rect x="20" y="66" width="180" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="85" font-size="12" text-anchor="middle">① d->addr, d->len 쓰기</text>
  <line x1="200" y1="80" x2="268" y2="80" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="270" y="66" width="140" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="340" y="85" font-size="12" text-anchor="middle">addr, len</text>
  <rect x="20" y="104" width="180" height="26" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="122" font-size="12" text-anchor="middle">② dma_wmb() = dmb oshst</text>
  <rect x="20" y="140" width="180" height="28" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="159" font-size="12" text-anchor="middle">③ d->status = OWN_DEV</text>
  <line x1="200" y1="154" x2="268" y2="154" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="270" y="140" width="140" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="159" font-size="12" text-anchor="middle">status = OWN</text>
  <rect x="20" y="178" width="180" height="40" rx="4" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="110" y="195" font-size="12" text-anchor="middle">④ writel(tail, doorbell)</text>
  <text x="110" y="211" font-size="12" text-anchor="middle">= dmb oshst + str</text>
  <line x1="200" y1="198" x2="478" y2="198" stroke="#d0564a" stroke-width="2" marker-end="url(#ah6)"/>
  <text x="430" y="191" font-size="12" text-anchor="middle">posted write</text>
  <rect x="480" y="182" width="180" height="32" rx="4" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="570" y="203" font-size="12" text-anchor="middle">doorbell 레지스터</text>
  <rect x="480" y="232" width="180" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="249" font-size="12" text-anchor="middle">⑤ status 확인 → OWN</text>
  <text x="570" y="265" font-size="12" text-anchor="middle">addr, len 읽고 전송</text>
  <line x1="412" y1="154" x2="478" y2="246" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah6)"/>
  <line x1="412" y1="80" x2="478" y2="256" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah6)"/>
  <rect x="480" y="290" width="180" height="40" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="570" y="307" font-size="12" text-anchor="middle">⑥ 결과 쓰기, status = DONE</text>
  <text x="570" y="323" font-size="12" text-anchor="middle">그 뒤 MSI 인터럽트</text>
  <line x1="478" y1="306" x2="412" y2="306" stroke="currentColor" marker-end="url(#ah6)"/>
  <rect x="270" y="292" width="140" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="311" font-size="12" text-anchor="middle">status = DONE</text>
  <rect x="20" y="340" width="180" height="56" rx="4" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="110" y="358" font-size="12" text-anchor="middle">⑦ ISR: status 읽기</text>
  <text x="110" y="374" font-size="12" text-anchor="middle">→ dma_rmb() = dmb oshld</text>
  <text x="110" y="390" font-size="12" text-anchor="middle">→ 결과 데이터 읽기</text>
  <line x1="268" y1="312" x2="200" y2="352" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#ah6)"/>
</svg>
```
그림 9 — 디스크립터 넘기기의 전체 순서. ②가 없으면 장치가 ⑤에서 OWN을 보고 옛 addr/len을 읽는다(MP의 쓰는 쪽). ④의 `writel` 안 배리어가 없으면 doorbell이 status보다 먼저 도착할 수 있다. ⑦의 `dma_rmb()`가 없으면 CPU가 결과 데이터를 status보다 먼저 읽어 옛 값을 쓴다(MP의 읽는 쪽). 장치 쪽 ⑤·⑥의 순서는 장치 하드웨어의 책임이다.

### 6.2 예제 7 — `f5_doorbell`: coherent 링, streaming 버퍼, 디스크립터 넘기기

진짜 DMA 장치는 없지만 **DMA API가 메모리를 어떻게 준비하는지**와 **넘기기 코드가 어떤 명령이 되는지**는 확인할 수 있다. 장치 대신 `platform_device_register_simple()`로 빈 platform device를 하나 만들어 DMA API의 `struct device`로 쓴다.

```c
// f5_doorbell.c — "디스크립터 쓰기 → dma_wmb → owner 비트 → writel(doorbell)" 패턴.
// 진짜 DMA 장치가 없으므로 doorbell은 QEMU virt의 pl031 RTC Match 레지스터(무해)를 빌려 쓴다.
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/dma-mapping.h>
#include <linux/dma-map-ops.h>      /* dev_is_dma_coherent() */
#include <linux/io.h>

struct f5_desc { __le64 addr; __le32 len; __le32 status; };   /* 장치가 읽는 형식 */
#define F5_OWN_DEV	cpu_to_le32(0x80000000)
#define PL031_MR	0x04

static noinline void f5_post(struct f5_desc *d, dma_addr_t buf, u32 len, void __iomem *db)
{
	d->addr = cpu_to_le64(buf);
	d->len  = cpu_to_le32(len);
	dma_wmb();                     /* 내용이 owner 비트보다 먼저 보이게 (dmb oshst) */
	d->status = F5_OWN_DEV;        /* 이제 장치 소유 */
	writel(1, db);                 /* writel 안의 dmb oshst: status store → MMIO store */
}

static int __init f5_db_init(void)
{
	struct platform_device *pd = platform_device_register_simple("f5_dmadev", -1, NULL, 0);
	struct f5_desc *ring; dma_addr_t ring_dma, buf_dma; void *buf; void __iomem *rtc;
	u32 old;

	if (IS_ERR(pd))
		return PTR_ERR(pd);
	if (dma_set_mask_and_coherent(&pd->dev, DMA_BIT_MASK(64)))
		goto out_pd;
	ring = dma_alloc_coherent(&pd->dev, PAGE_SIZE, &ring_dma, GFP_KERNEL);   /* coherent */
	buf = kmalloc(256, GFP_KERNEL);
	rtc = ioremap(0x09010000, 0x1000);
	if (!ring || !buf || !rtc)
		goto out_free;
	buf_dma = dma_map_single(&pd->dev, buf, 256, DMA_TO_DEVICE);              /* streaming */
	if (dma_mapping_error(&pd->dev, buf_dma))
		goto out_free;
	pr_info("f5_db: dev coherent=%d ring cpu=%px dma=%pad (vmalloc area=%d)\n",
		dev_is_dma_coherent(&pd->dev), ring, &ring_dma, is_vmalloc_addr(ring));
	pr_info("f5_db: buf cpu=%px virt_to_phys=%pa dma=%pad\n", buf, &(phys_addr_t){virt_to_phys(buf)}, &buf_dma);
	old = readl(rtc + PL031_MR);
	f5_post(&ring[0], buf_dma, 256, rtc + PL031_MR);
	pr_info("f5_db: desc addr=%#llx len=%u status=%#x, doorbell read-back=%u (old MR=%u)\n",
		le64_to_cpu(ring[0].addr), le32_to_cpu(ring[0].len), le32_to_cpu(ring[0].status),
		readl(rtc + PL031_MR), old);
	writel(old, rtc + PL031_MR);
	dma_unmap_single(&pd->dev, buf_dma, 256, DMA_TO_DEVICE);
out_free:
	if (rtc) iounmap(rtc);
	kfree(buf);
	if (ring) dma_free_coherent(&pd->dev, PAGE_SIZE, ring, ring_dma);
out_pd:
	platform_device_unregister(pd);
	return -EAGAIN;                /* 관찰만 하고 로드 실패로 끝낸다 */
}
module_init(f5_db_init);
MODULE_LICENSE("GPL");
```
(`%px`는 실제 주소를 찍는 포맷이다 — 학습용이라 썼고, 실무 로그에는 `%p`(해시된 주소)를 쓴다.)

먼저 `f5_post`가 어떤 명령이 됐는지:

```sh
limactl shell elinux -- bash -lc 'cd /var/tmp/F5/mod && objdump -d --no-show-raw-insn f5_doorbell.ko | sed -n "/<f5_post.*>:/,/ret/p"'
```
```text
0000000000000018 <f5_post.constprop.0>:
  18:	str	x1, [x0]
  1c:	mov	w1, #0x100                 	// #256
  20:	str	w1, [x0, #8]
  24:	dmb	oshst
  28:	mov	w1, #0x80000000            	// #-2147483648
  2c:	str	w1, [x0, #12]
  30:	dmb	oshst
  34:	mov	w0, #0x1                   	// #1
  38:	str	w0, [x2]
  3c:	ret
```
출력에서 볼 것: `str x1`(addr) → `str w1`(len) → **`dmb oshst`**(`dma_wmb`) → `str`(status = `0x80000000`) → **`dmb oshst`**(`writel` 안의 `__io_bw`) → `str`(doorbell). 그림 9의 ①~④가 명령 여섯 줄로 그대로 보인다. (gcc가 len이 항상 256이라 상수를 접어 넣은 `constprop` 버전을 만들었다.) `dmb oshst`가 두 번인 게 낭비 같지만 각자 다른 쌍을 순서 짓는다: 첫째는 내용↔status(둘 다 메모리), 둘째는 status↔doorbell(메모리↔MMIO).

```sh
limactl shell elinux -- bash -lc 'sh /Users/donh/.elinux_scratch/F5/boot.sh /Users/donh/.elinux_scratch/F5/g3.sh multi'
```
게스트 스크립트 `g3.sh`:
```sh
insmod /f5_doorbell.ko; dmesg | grep "f5_db" | sed 's/^\[[ 0-9.]*\] //'
grep -m3 -i "rtc\|pl031" /proc/iomem; ls /sys/bus/amba/drivers/ | grep -i rtc
```
```text
insmod: can't insert '/f5_doorbell.ko': Resource temporarily unavailable
f5_db: dev coherent=0 ring cpu=ffff80008003d000 dma=0x0000000043fde000 (vmalloc area=1)
f5_db: buf cpu=ffff0000034cb600 virt_to_phys=0x00000000434cb600 dma=0x00000000434cb600
f5_db: desc addr=0x434cb600 len=256 status=0x80000000, doorbell read-back=1 (old MR=0)
09010000-09010fff : pl031@9010000
  09010000-09010fff : rtc-pl031
rtc-pl031
[    3.764261] reboot: Power down
```
(부팅 로그 앞부분은 생략했다.) 출력에서 볼 것, 한 줄씩:

- `dev coherent=0`: 이 장치는 **non-coherent**다(DT에 `dma-coherent` 속성이 없는 일반적인 경우와 같다). 그래서 `dma_alloc_coherent()`가 준 CPU 주소 `ffff80008003d000`이 **vmalloc 영역**(`vmalloc area=1`)이다. 커널이 페이지를 할당한 뒤 **Normal Non-Cacheable** 속성으로 다시 매핑한 것이다(`CONFIG_DMA_DIRECT_REMAP=y`, `pgprot_dmacoherent()` = `MT_NORMAL_NC` — 랩 소스 `arch/arm64/include/asm/pgtable.h` 806행에서 확인). 캐시를 안 거치니 "coherent"하다. 장치 쪽 주소는 `0x43fde000`.
- `buf cpu=ffff0000034cb600`: `kmalloc` 버퍼는 **linear map**(cacheable) 주소이고, IOMMU가 없으니 `dma_map_single()`의 DMA 주소가 물리 주소와 같다(`0x434cb600`). 이 버퍼는 캐시를 거치므로, 장치가 읽기 전에 캐시를 clean해야 한다 — `dma_map_single()`이 그 일을 한다(§6.3).
- `desc addr=0x434cb600 len=256 status=0x80000000`: 디스크립터에 streaming 버퍼의 DMA 주소가 들어갔다.
- `doorbell read-back=1 (old MR=0)`: `writel`로 쓴 값을 같은 레지스터에서 다시 읽었다. 진짜 장치라면 이 `readl`이 posted write를 flush하는 역할이다(§5.4). 끝에서 원래 값으로 되돌렸다.
- `/proc/iomem`: `0x09010000`은 이미 `rtc-pl031` 드라이버 것이다. `ioremap()`은 영역을 "점유"하지 않아서 겹쳐 매핑이 된다 — 실험이니까 허용되는 일이고, 실무 드라이버는 `devm_ioremap_resource()`로 점유 검사를 받는다(H 모듈).

### 6.3 coherent vs streaming — 배리어와 캐시 유지보수의 분업 (H9 미리보기)

| | coherent (`dma_alloc_coherent`) | streaming (`dma_map_single`/`dma_map_sg`) |
|---|---|---|
| 용도 | 디스크립터 링, 상태 블록 — CPU와 장치가 **동시에 계속** 보는 작은 영역 | 데이터 버퍼 — 한 번 넘기고 한 번 돌려받는 큰 영역 |
| non-coherent arm64에서의 CPU 매핑 | Normal-NC (캐시 안 씀, 위 실험의 vmalloc 주소) | 평범한 cacheable linear map |
| 캐시 유지보수 | 필요 없음 | map/unmap/sync 때 커널이 자동: `arch_sync_dma_for_device()` → `dcache_clean_poc()`, `arch_sync_dma_for_cpu()` → `dcache_inval_poc()` (FROM_DEVICE/BIDIRECTIONAL일 때) |
| 순서 | **배리어가 필요하다**: `dma_wmb()`/`dma_rmb()` | 소유권이 map/unmap/sync 호출로 넘어가므로 그 경계가 순서 지점 |
| CPU 접근 속도 | 느림 (uncached) | 빠름 |

랩 소스로 확인: `arch/arm64/mm/dma-mapping.c`의 `arch_sync_dma_for_device()`는 `dcache_clean_poc(start, start + size)` 한 줄이고, `arch/arm64/mm/cache.S`의 `dcache_clean_poc`는 `dcache_by_line_op cvac, sy, ...` — 캐시 라인마다 `dc cvac`(clean by VA to Point of Coherency) 후 끝에 `dsb sy`(`assembler.h`의 `dcache_by_myline_op` 끝 `dsb \domain`). **캐시 clean 뒤의 `dsb sy`가 "clean이 끝났다"는 보장**이다. Don이 SSD 펌웨어에서 "clean → DSB → DMA 시작"을 했던 그 순서를 커널이 대신 해 준다.

그래서 coherent 메모리의 배리어가 왜 `dmb osh*`면 충분한가: Normal-NC 메모리는 캐시를 안 거치니 "완료"를 기다릴 필요 없이 **장치(outer shareable 관찰자)에게 보이는 순서**만 정하면 된다. 반대로 coherent 장치(`dma-coherent`, 하드웨어 snoop — 예: 일부 SoC의 PCIe RC나 CCI에 붙은 마스터)에서는 coherent 버퍼가 cacheable이고, 그래도 `dma_wmb()`는 같은 `dmb oshst`다 — 관찰자가 OSH 도메인 안에 있기 때문이다.

### 6.4 함정

- **`dma_wmb()` 대신 `smp_wmb()`**: `dmb ishst`는 inner shareable — CPU끼리의 순서만 정한다. 많은 SoC에서 우연히 동작하지만(ISH와 OSH가 같은 하드웨어 경로인 경우) 이식성 없는 버그다.
- **`writel_relaxed()`로 doorbell**: 앞의 status store와 순서가 없다. 장치가 doorbell을 받고 status를 읽었는데 아직 OWN이 아니면 장치는 "할 일 없음"으로 잠든다 — 타임아웃 후 재시도로 가려져 성능 저하로만 보인다.
- **`dma_rmb()` 빼먹기(완료 경로)**: `if (d->status == DONE) use(d->data)` — status와 data 읽기가 재배치되면 옛 데이터. 특히 NAPI 폴링 루프처럼 인터럽트 없이 링을 읽는 경로에서 터진다.
- **streaming 버퍼를 map한 뒤 CPU가 다시 쓰기**: map 시점에 clean했으니 그 뒤 쓰기는 캐시에만 남는다. 쓰기는 map **전에**, 또는 `dma_sync_single_for_device()`로 소유권을 다시 넘긴다.
- **배리어로 캐시 문제를 풀려는 시도**: `dsb`를 아무리 넣어도 cacheable 버퍼의 dirty 라인은 메모리에 안 간다. 순서(barrier)와 가시성(cache maintenance)은 다른 문제다.

---

## 7. 커널 안에서는 무슨 일이 일어나나

드라이버가 쓰는 배리어 매크로 한 줄은 헤더 세 겹을 지나 명령 하나가 된다.

```svg
<svg viewBox="0 0 680 430" xmlns="http://www.w3.org/2000/svg">
  <defs><marker id="ah7" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="340" y="22" font-size="14" text-anchor="middle">배리어 매크로가 명령이 되기까지 (Linux 6.18, arm64)</text>
  <rect x="20" y="40" width="640" height="36" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="340" y="63" font-size="13" text-anchor="middle">드라이버:  smp_wmb();   dma_wmb();   writel(v, reg);   x = readl(reg);</text>
  <line x1="170" y1="76" x2="170" y2="98" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="76" x2="510" y2="98" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="100" width="300" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="122" font-size="12" text-anchor="middle">include/asm-generic/barrier.h</text>
  <text x="170" y="142" font-size="12" text-anchor="middle">smp_wmb() = kcsan_wmb() + __smp_wmb()</text>
  <text x="170" y="160" font-size="12" text-anchor="middle">CONFIG_SMP=n → barrier()</text>
  <text x="170" y="175" font-size="12" text-anchor="middle">dma_wmb() = kcsan_wmb() + __dma_wmb()</text>
  <rect x="360" y="100" width="300" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="122" font-size="12" text-anchor="middle">include/asm-generic/io.h</text>
  <text x="510" y="142" font-size="12" text-anchor="middle">writel = __io_bw + __raw_writel + __io_aw</text>
  <text x="510" y="160" font-size="12" text-anchor="middle">readl = __io_br + __raw_readl + __io_ar</text>
  <text x="510" y="175" font-size="12" text-anchor="middle">(+ rwmmio tracepoint)</text>
  <line x1="170" y1="180" x2="170" y2="202" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="180" x2="510" y2="202" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="204" width="300" height="80" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="226" font-size="12" text-anchor="middle">arch/arm64/include/asm/barrier.h</text>
  <text x="170" y="246" font-size="12" text-anchor="middle">__smp_wmb() = dmb(ishst)</text>
  <text x="170" y="263" font-size="12" text-anchor="middle">__dma_wmb() = dmb(oshst)</text>
  <text x="170" y="279" font-size="12" text-anchor="middle">__wmb() = dsb(st)</text>
  <rect x="360" y="204" width="300" height="80" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="510" y="226" font-size="12" text-anchor="middle">arch/arm64/include/asm/io.h</text>
  <text x="510" y="246" font-size="12" text-anchor="middle">__io_bw() = dma_wmb()</text>
  <text x="510" y="263" font-size="12" text-anchor="middle">__io_ar(v) = dma_rmb() + eor/cbnz</text>
  <text x="510" y="279" font-size="12" text-anchor="middle">__raw_readl = ALTERNATIVE(ldr, ldar)</text>
  <line x1="170" y1="284" x2="170" y2="306" stroke="currentColor" marker-end="url(#ah7)"/>
  <line x1="510" y1="284" x2="510" y2="306" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="308" width="640" height="40" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="340" y="333" font-size="13" text-anchor="middle">asm volatile("dmb ishst" : : : "memory")  — 명령 + 컴파일러 배리어</text>
  <line x1="340" y1="348" x2="340" y2="370" stroke="currentColor" marker-end="url(#ah7)"/>
  <rect x="20" y="372" width="640" height="46" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="340" y="392" font-size="12" text-anchor="middle">부팅 시 alternative 패치 (.altinstructions): LSE atomics면 LL/SC 분기를 nop으로,</text>
  <text x="340" y="410" font-size="12" text-anchor="middle">erratum 832075가 있는 CPU면 MMIO 읽기 ldr을 ldar로 바꾼다</text>
</svg>
```
그림 10 — 배리어 매크로의 헤더 계층. 아키텍처 공통 헤더가 이름과 의미(+ KCSAN 계측 훅)를 정하고, 아키텍처 헤더가 실제 명령을 정한다. arm64가 `__smp_mb__before_atomic`을 따로 정의하지 않으므로 공통 기본값 `__smp_mb()` = `dmb ish`가 쓰인다(§2.5에서 확인).

핵심 지점 정리:

- **`include/asm-generic/barrier.h`**: `mb()`/`rmb()`/`wmb()`/`dma_*()`/`smp_*()`/`smp_store_mb()`/`smp_mb__before_atomic()`/`smp_load_acquire()`의 공통 정의. 아키텍처가 `__`붙은 버전을 정의하면 그걸 쓰고, 아니면 더 강한 것으로 대체한다(예: `dma_rmb()`가 없으면 `rmb()`). 각 매크로 앞의 `kcsan_mb()` 등은 KCSAN이 켜졌을 때 "여기서 순서가 생겼다"를 알리는 훅이다.
- **`arch/arm64/include/asm/barrier.h`**: §2.4 표의 원천. `__smp_store_release`/`__smp_load_acquire`는 크기별(`stlrb`/`stlrh`/`stlr`) inline asm이고, `smp_cond_load_acquire()`는 `__cmpwait_relaxed()`로 `wfe` 대기를 한다.
- **`include/asm-generic/rwonce.h`**, **`arch/arm64/include/asm/rwonce.h`**: `READ_ONCE`/`WRITE_ONCE`. arm64는 LTO일 때만 덮어쓴다.
- **`include/asm-generic/io.h`**, **`arch/arm64/include/asm/io.h`**: `readl`/`writel` = raw 접근 + `__io_*` 훅. 훅의 정의가 아키텍처마다 다르다(x86에서는 MMIO가 이미 강하게 순서 지어져 훅이 거의 비어 있다).
- **`kernel/locking/`, `include/linux/spinlock.h`**: 락의 acquire/release도 결국 `ldaxr`/`stlr`류(arm64 qspinlock은 `atomic_cond_read_acquire` 등)다 — F4에서 락이 "배리어를 품고 있다"고 한 근거.
- **`Documentation/memory-barriers.txt`**: 3,000줄짜리 원전. **`tools/memory-model/`**: LKMM과 litmus 테스트. **`Documentation/atomic_t.txt`**: atomic 순서 규칙.

드라이버를 쓸 때 다시 만나는 곳:

| 장면 | 모듈 | 이 노트의 도구 |
|---|---|---|
| `wait_event()`의 조건 + `wake_up()` | F3 | `set_current_state()` = `smp_store_mb()` (SB) |
| 락, RCU, seqlock, lock-free 카운터 | F4 | 락의 acquire/release, `rcu_dereference()`의 주소 의존, `smp_mb__after_atomic()` |
| ISR ↔ threaded IRQ ↔ `read()` 사이 링버퍼 | F6, G | `smp_store_release`/`smp_load_acquire` (`circular-buffers.rst`) |
| 레지스터 접근 | H1~H3 | `readl`/`writel`/`_relaxed`, `readl_poll_timeout()` |
| DMA 디스크립터 링, coherent/streaming | H9 | `dma_wmb`/`dma_rmb`, `dma_map_*`의 캐시 유지보수 |
| 네트워크 드라이버 NAPI, 카메라 V4L2 버퍼 큐 | I | owner 비트 + `dma_rmb()`, doorbell `writel` |
| KCSAN으로 data race 찾기 | M | `READ_ONCE`/`WRITE_ONCE` 표시 |

---

## 8. Embedded 실무에서 다시 보기

### 8.1 센서 키트 — IMU 샘플 링버퍼 (threaded IRQ → `read()`)

예를 들어 센서 키트의 IMU 드라이버가 threaded IRQ에서 SPI로 샘플을 읽어 링에 넣고, 유저 `read()`가 꺼낸다고 하자. 생산자 하나, 소비자 하나라면 락 없이 `Documentation/core-api/circular-buffers.rst`의 패턴을 쓴다(아래는 그 패턴을 IMU 링에 맞춰 옮긴 코드다 — 이 노트에서 실행하지 않았다. 같은 원리의 유저스페이스 SPSC는 A8 §7.4에서 실행했다).

```c
/* 생산자: threaded IRQ (CPU 아무거나) */
unsigned long head = imu->head;                      /* 내가 유일한 writer */
unsigned long tail = READ_ONCE(imu->tail);           /* 소비자가 바꾼다 */
if (CIRC_SPACE(head, tail, RING_SZ) >= 1) {
	imu->ring[head] = sample;                        /* ① 데이터 */
	smp_store_release(&imu->head, (head + 1) & (RING_SZ - 1));   /* ② 공개: ①이 먼저 보인다 */
	wake_up_interruptible(&imu->wq);
}

/* 소비자: read() (다른 CPU) */
unsigned long head = smp_load_acquire(&imu->head);   /* ③ 짝: 뒤의 데이터 읽기가 못 올라온다 */
unsigned long tail = imu->tail;
if (CIRC_CNT(head, tail, RING_SZ) >= 1) {
	struct imu_sample s = imu->ring[tail];           /* ④ 데이터 */
	smp_store_release(&imu->tail, (tail + 1) & (RING_SZ - 1));   /* ⑤ 슬롯 반납: ④가 먼저 끝난다 */
}
```
②-③이 MP(데이터 → 인덱스), ④-⑤가 반대 방향 MP(데이터 다 읽고 → 슬롯 반납)다. ⑤를 평범한 store로 쓰면 생산자가 아직 읽히지 않은 슬롯을 덮어쓸 수 있다. 이 패턴을 쓸 수 있는 조건: 생산자 하나, 소비자 하나. 여럿이면 락(spinlock, F4) 또는 `kfifo`의 락 버전.

### 8.2 이더넷 MAC 드라이버 — 실제 소스의 owner 비트와 tail pointer

i.MX8M Plus, STM32MP1, 여러 Rockchip SoC가 쓰는 Synopsys DWMAC 드라이버(`drivers/net/ethernet/stmicro/stmmac/`)가 이 노트의 패턴을 그대로 보여 준다.

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55/drivers/net/ethernet/stmicro/stmmac; sed -n 350,362p dwmac4_descs.c; echo ...; sed -n 4157,4164p stmmac_main.c; echo ...; sed -n 37,42p dwmac4_lib.c'
```
```text
	/* Finally set the OWN bit. Later the DMA will start! */
	if (tx_own)
		tdes3 |= TDES3_OWN;

	if (is_fs && tx_own)
		/* When the own bit, for the first frame, has to be set, all
		 * descriptors for the same frame has to be set before, to
		 * avoid race condition.
		 */
		dma_wmb();

	p->des3 = cpu_to_le32(tdes3);
}
...
	/* The own bit must be the latest setting done when prepare the
	 * descriptor and then barrier is needed to make sure that
	 * all is coherent before granting the DMA engine.
	 */
	wmb();

	tx_q->tx_tail_addr = tx_q->dma_tx_phy + (tx_q->cur_tx * desc_size);
	stmmac_set_tx_tail_ptr(priv, priv->ioaddr, tx_q->tx_tail_addr, queue);
...
void dwmac4_set_tx_tail_ptr(struct stmmac_priv *priv, void __iomem *ioaddr,
			    u32 tail_ptr, u32 chan)
{
	const struct dwmac4_addrs *dwmac4_addrs = priv->plat->dwmac4_addrs;

	writel(tail_ptr, ioaddr + DMA_CHAN_TX_END_ADDR(dwmac4_addrs, chan));
```
출력에서 볼 것: (1) 디스크립터의 다른 필드를 다 쓴 뒤 **OWN 비트를 마지막에**, 그 앞에 `dma_wmb()`. (2) 모든 디스크립터를 준비한 뒤 `wmb()` 후 tail pointer 레지스터에 `writel()` — doorbell이다. arm64에서 `writel()`은 이미 `dmb oshst`를 품고 있으므로 이 `wmb()`(`dsb st`)는 엄밀히는 중복이지만, 모든 아키텍처에서 안전하도록 보수적으로 둔 것이다. 이런 "중복이지만 무해한" 배리어를 리뷰에서 지울지 말지는 해당 경로의 성능 측정으로 판단한다(§2.6: `dsb`는 `dmb`의 몇 배다).

### 8.3 랩 소스에서 얼마나 쓰이나

```sh
limactl shell elinux -- bash -lc 'cd /var/lab/src/linux-6.18.55; for w in "READ_ONCE(" "WRITE_ONCE(" "smp_mb()" "smp_rmb()" "smp_wmb()" "smp_store_release(" "smp_load_acquire(" "smp_mb__before_atomic()" "smp_mb__after_atomic()" "dma_rmb()" "dma_wmb()" "readl(" "writel(" "readl_relaxed(" "writel_relaxed("; do printf "%-26s %s\n" "$w" "$(grep -rF --include=*.c "$w" drivers | wc -l)"; done; for w in mb rmb wmb; do printf "%-26s %s\n" "$w()" "$(grep -rE --include=*.c "(^|[^_a-z])$w\(\)" drivers | wc -l)"; done'
```
```text
READ_ONCE(                 1738
WRITE_ONCE(                1208
smp_mb()                   254
smp_rmb()                  183
smp_wmb()                  293
smp_store_release(         121
smp_load_acquire(          93
smp_mb__before_atomic()    101
smp_mb__after_atomic()     147
dma_rmb()                  195
dma_wmb()                  282
readl(                     20554
writel(                    30867
readl_relaxed(             3198
writel_relaxed(            4914
mb()                       502
rmb()                      268
wmb()                      1370
```
출력에서 볼 것: 숫자는 `drivers/`의 `.c` 파일에서 그 문자열이 나온 **줄 수**(근사치 — `readl(`에는 `__raw_readl(`도 걸린다). MMIO 접근자가 압도적이고, `dma_wmb()`/`dma_rmb()`(477줄)가 `wmb()`/`rmb()`(1,638줄)보다 적다 — `dma_*` 배리어는 2014년(3.19)에야 들어와서, 오래된 드라이버는 더 무거운 `wmb()`/`rmb()`를 쓴다. 새 코드에서는 coherent 메모리 순서에 `dma_*`를 쓴다.

### 8.4 "가끔 프레임이 깨진다" 디버깅 시나리오

카메라 파이프라인(V4L2, ISP가 DMA로 프레임을 씀)에서 "1만 프레임에 한 번, 위쪽 몇 줄이 이전 프레임"이라는 버그. 순서대로 의심한다.

1. 완료 인터럽트에서 status를 `readl_relaxed()`로 읽고 바로 버퍼를 넘기나? → `readl()` 또는 `dma_rmb()`.
2. 버퍼가 streaming 매핑인데 `dma_sync_single_for_cpu()`(또는 vb2의 `finish` 경로)를 건너뛰었나? → 캐시에 남은 옛 라인을 읽고 있다. 배리어로는 못 고친다(§6.4).
3. 버퍼를 큐에 다시 넣을 때(`qbuf`) 주소 레지스터를 쓰고 enable을 `writel_relaxed()`로 쳤나? → 마지막은 `writel()`.
4. 재현이 QEMU에서 안 되나? → 당연하다(§4.2). 타깃 보드에서 stress + `trace_printk`(M 모듈)로 타임스탬프를 남긴다.

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 폴링 루프에서 평범한 변수 읽기 | `-O2`에서만 무한 루프, 디버그 빌드는 정상 | 컴파일러가 load를 루프 밖으로 끌어올림 | `READ_ONCE()`, 또는 `smp_cond_load_acquire()`/`readl_poll_timeout()` |
| `volatile`로 SMP 순서를 맞추려 함 | x86에선 정상, arm64에서 드물게 데이터 깨짐 | volatile은 CPU 재배치를 못 막음 | `smp_store_release`/`smp_load_acquire` 짝 |
| 쓰는 쪽만 `smp_wmb()` | MP 결과 (1, 0)이 드물게 발생 (§3.5 "wmb만" 428~637/1천만) | 읽는 쪽 load→load 재배치 | 읽는 쪽 `smp_rmb()` 또는 acquire — 항상 짝으로 |
| SB 패턴에 release/acquire 사용 | lost wakeup, 양쪽 다 "상대가 안 했다"고 판단 | release→acquire는 store→load를 LKMM상 보장 안 함 | 양쪽 `smp_mb()` |
| `smp_wmb()`를 DMA 디스크립터에 | 특정 SoC에서만 장치가 옛 디스크립터 읽음 | ISH 범위는 DMA 마스터를 포함 안 할 수 있음 | `dma_wmb()` |
| doorbell을 `writel_relaxed()`로 | 장치가 가끔 "할 일 없음", 타임아웃 후 재시도로 성능 저하 | status store와 MMIO store 사이 순서 없음 | `writel()` 또는 앞에 `wmb()` |
| `writel()` 후 바로 `udelay()` | 리셋 펄스·타이밍 요구가 가끔 위반 | posted write가 아직 장치에 안 감 | 같은 장치 `readl()`로 read-back |
| 모든 곳에 `mb()`/`wmb()` | 기능은 맞지만 hot path가 느림 | `dsb`는 `dmb`의 7~8배 (§2.6) | 필요한 최소: `smp_*`, `dma_*`, acquire/release |
| 배리어로 캐시 문제를 풀려 함 | streaming 버퍼 데이터가 여전히 옛 값 | 순서와 캐시 가시성은 별개 | `dma_map_*`/`dma_sync_*` |
| QEMU에서 순서 테스트 통과로 안심 | 보드에서 재현 | TCG는 하드웨어 순서를 정확히 재현하지 않음 (§4.2) | 실제 하드웨어 stress + LKMM 논증 |

---

## 10. 면접에서 이렇게 말한다

**Q.** What's the difference between a compiler barrier and a memory barrier? Is `volatile` enough?

**A.** 재배치하는 주체가 둘이다. 컴파일러는 싱글 스레드 관점의 as-if 규칙으로 load/store를 옮기고, 합치고, 지운다. `barrier()`는 빈 asm에 `"memory"` clobber를 붙인 것이라 명령은 0개이고 컴파일러만 막는다. CPU는 store buffer와 out-of-order 실행으로 다른 코어가 보는 순서를 바꾸는데, 그건 `dmb` 같은 진짜 명령이 있어야 막힌다. `volatile`(커널에선 `READ_ONCE`/`WRITE_ONCE`)은 접근을 지우거나 쪼개지 않게 할 뿐 CPU 순서는 못 막는다. 실제로 gcc `-O2`에서 평범한 폴링 루프가 `b .` 무한 루프로 컴파일되는 걸 objdump로 봤고, `READ_ONCE`만 쓴 MP 테스트가 arm64에서 1천만 번에 수백 번 깨지는 걸 측정했다.

> There are two reorderers. The compiler can move, merge, or delete plain accesses because it only has to preserve single-threaded behavior; `barrier()` is an empty asm with a memory clobber, so it emits zero instructions and only constrains the compiler. The CPU reorders through store buffers and out-of-order loads, and that needs a real instruction like `dmb`. `volatile`, or `READ_ONCE`/`WRITE_ONCE` in the kernel, prevents tearing, fusing, and hoisting, but gives no cross-CPU ordering. I've seen a plain polling loop compile to a branch-to-self at -O2, and a message-passing test with only `READ_ONCE`/`WRITE_ONCE` fail several hundred times per ten million on arm64.

**꼬리 질문**: "그럼 `READ_ONCE`는 언제 필요 없나?" → 락으로 보호된 변수. 락이 컴파일러·CPU 배리어를 다 품고 있다.

**Q.** Explain `smp_wmb()` vs `wmb()` vs `dma_wmb()`. What do they compile to on arm64?

**A.** 세 가족이다. `smp_wmb()`는 CPU끼리 일반 메모리 store 순서 — arm64에서 `dmb ishst`, UP 커널에선 `barrier()`. `dma_wmb()`는 CPU와 DMA 장치가 공유하는 coherent 메모리의 store 순서 — `dmb oshst`, outer shareable 도메인까지. `wmb()`는 필수 배리어로 MMIO까지 포함한 모든 store — `dsb st`라서 앞의 store가 완료될 때까지 파이프라인을 세운다. 모듈을 빌드해 objdump로 확인했고, 측정하면 `dsb`가 `dmb`보다 7~8배 비쌌다. 그래서 디스크립터 owner 비트 앞에는 `dma_wmb()`, CPU 간 플래그엔 `smp_wmb()`나 release, `wmb()`는 `_relaxed` MMIO와 메모리를 섞을 때만 쓴다.

> They're three families. `smp_wmb()` orders stores to normal memory between CPUs — `dmb ishst` on arm64, and just a compiler barrier on a UP kernel. `dma_wmb()` orders stores to coherent DMA memory as seen by a device — `dmb oshst`, the outer-shareable domain. `wmb()` is the mandatory barrier that also covers MMIO — it's `dsb st`, which stalls until prior stores complete. I verified the mapping by disassembling a test module, and in a microbenchmark `dsb` cost roughly seven to eight times a `dmb`. So: `dma_wmb()` before handing a descriptor's OWN bit to the device, `smp_wmb()` or release for CPU-to-CPU flags, and `wmb()` only when mixing relaxed MMIO with memory.

**꼬리 질문**: "x86에서는 이게 다 뭐가 되나?" → TSO라서 `smp_wmb()`/`smp_rmb()`/`dma_wmb()`는 컴파일러 배리어, `smp_mb()`는 locked 명령이나 `mfence` 계열. 그래서 x86에서만 테스트한 드라이버가 arm64에서 깨진다.

**Q.** Walk me through posting a DMA descriptor and ringing the doorbell. Which barriers, and why?

**A.** 디스크립터 주소·길이를 쓰고, `dma_wmb()`, 그다음 OWN 비트를 장치로 넘기고, `writel()`로 doorbell. `dma_wmb()`가 없으면 장치가 OWN은 새 값, 주소는 옛 값으로 볼 수 있다 — MP 패턴의 쓰는 쪽이다. doorbell은 `writel()`이어야 하는데, arm64 `writel()`은 store 앞에 `dmb oshst`가 있어 앞의 OWN 쓰기가 장치에 먼저 보이게 한다. `writel_relaxed()`를 쓰면 그 보장이 없어 `wmb()`를 따로 넣어야 한다. 완료 경로에선 반대로 status를 읽고 `dma_rmb()` 후 데이터를 읽는다. 모듈로 이 함수를 컴파일해 보면 `str, str, dmb oshst, str(OWN), dmb oshst, str(doorbell)` 여섯 줄이 나온다. SSD 펌웨어에서 하던 "데이터 → DMB → owner → doorbell"과 같은데, Linux는 범위별로 배리어 이름이 다를 뿐이다.

> Write the descriptor fields, `dma_wmb()`, then flip the OWN bit, then `writel()` the doorbell. Without `dma_wmb()` the device can observe OWN set but stale address or length — the writer side of message passing. The doorbell must be `writel()`, not the relaxed variant: on arm64 `writel()` puts a `dmb oshst` before the store, so the OWN write is visible to the device before the doorbell arrives. On completion it's the mirror image: read status, `dma_rmb()`, then read the data. When I compiled that function, it was literally str, str, `dmb oshst`, str OWN, `dmb oshst`, str doorbell. It's the same thing I did in SSD firmware with DMB and owner bits — Linux just names the barrier by who the observer is.

**꼬리 질문**: "링이 `dma_alloc_coherent()`가 아니라 streaming이면?" → 그땐 순서가 아니라 캐시가 문제다. `dma_map_single()`/`dma_sync_single_for_device()`가 `dc cvac` + `dsb sy`로 clean해 주고, 그 호출이 소유권 경계다.

**Q.** When is it safe to use `readl_relaxed()`/`writel_relaxed()`?

**A.** 같은 장치의 MMIO끼리는 Device-nGnRE 매핑이 순서를 지켜 주므로, 레지스터 초기화 시퀀스나 FIFO 반복 읽기처럼 **일반 메모리와 순서가 필요 없는** 곳에선 relaxed가 안전하다. 반대로 DMA 버퍼와 엮이는 곳 — DMA를 시작하는 레지스터 쓰기, DMA 완료 상태 읽기 — 은 non-relaxed가 필요하다. arm64 소스를 보면 `writel`은 `dmb oshst + str`, `readl`은 `ldr + dmb oshld + 가짜 컨트롤 의존`이고 relaxed는 `str`/`ldr` 하나뿐이다. 실무에선 기본은 non-relaxed, 프로파일로 hot path가 확인된 곳만 relaxed로 바꾸고 주석을 단다.

> Accesses to the same device are already ordered by the Device-nGnRE mapping, so relaxed accessors are fine for register init sequences or draining a FIFO — anywhere you don't need ordering against normal memory. Anything that pairs with a DMA buffer — the register write that starts DMA, or the status read that says DMA finished — needs the non-relaxed version. On arm64, `writel` is `dmb oshst` plus the store, `readl` is the load plus `dmb oshld` plus a fake control dependency for `udelay`, and the relaxed ones are a single instruction. My default is non-relaxed, and I switch a hot path to relaxed only after profiling, with a comment explaining why.

**Q.** What is a posted write, and how do you make sure a register write has reached the device?

**A.** PCIe 메모리 쓰기는 completion이 없는 posted 트랜잭션이고, arm64 `ioremap()`의 Device-nGnRE도 E(early ack) 속성이라 CPU는 쓰기가 끝났다고 보고 진행한다. 그래서 `writel()` 직후 `udelay()`나 `free_irq()`는 장치 도착과 순서가 없다. 해결은 같은 장치 레지스터를 `readl()`로 읽는 read-back이다 — read는 non-posted라 completion을 기다리고, PCIe 순서 규칙상 앞선 posted write를 추월하지 못한다. `wmb()`(`dsb`)로는 안 된다, CPU 쪽 완료만 기다리니까. 커널 `memory-barriers.txt`에 `writel; readl; udelay(1); writel` 예시가 그대로 있다.

> PCIe memory writes are posted — no completion — and on arm64 `ioremap()` gives Device-nGnRE, where the E means early write acknowledgment. The CPU considers the store done long before the device sees it, so a `udelay()` or `free_irq()` right after `writel()` isn't ordered against arrival. The fix is a read-back from the same device: the read is non-posted, must wait for its completion, and can't pass the earlier posted write on the same path. A `wmb()` doesn't help because `dsb` only waits for completion from the CPU's point of view. `memory-barriers.txt` literally shows writel, readl, udelay(1), writel as the pattern.

**Q.** What are litmus tests and the LKMM? Give me MP and SB and how to fix each.

**A.** LKMM은 커널 `tools/memory-model`에 있는 형식 메모리 모델이고, herd7이 litmus 테스트의 모든 실행을 열거해 결과가 허용되는지 답한다. MP는 데이터 쓰고 플래그, 반대편은 플래그 읽고 데이터 — 쓰는 쪽 `smp_wmb`와 읽는 쪽 `smp_rmb` 짝, 또는 release/acquire 짝으로 막는다. 한쪽만 고치면 안 되는 것도 측정했다(wmb만이면 1천만에 수백, rmb만이면 수십). SB는 각자 자기 변수 쓰고 상대 변수 읽기 — store buffer 때문에 둘 다 옛 값을 볼 수 있고, `smp_mb()`만 막는다. 흥미로운 점은 arm64 `stlr`→`ldar`는 SB를 막는데(측정 0) `ldapr`로 바꾸면 3만 번 깨졌다. LKMM은 release/acquire로 SB가 막힌다고 보장하지 않으니 커널 코드에선 반드시 `smp_mb()`를 쓴다. 실무 예가 sleep/wakeup — `set_current_state()`가 `smp_store_mb()`인 이유다.

> The LKMM is the formal model in tools/memory-model; herd7 enumerates every execution of a litmus test and says whether an outcome is allowed. Message passing is "write data, then flag" versus "read flag, then data" — fixed with a paired `smp_wmb`/`smp_rmb`, or better, release/acquire. I measured that fixing only one side isn't enough. Store buffering is each CPU writing its own variable and reading the other's; both can read zero because of store buffers, and only a full `smp_mb()` forbids it. On arm64, `stlr` followed by `ldar` happened to forbid it — zero in thirty million runs — but with `ldapr` it failed thirty thousand times, and the LKMM doesn't promise release/acquire fixes SB, so kernel code uses `smp_mb()`. That's exactly why `set_current_state()` is `smp_store_mb()`.

**꼬리 질문**: "LB는?" → 각자 읽고 나서 상대 변수를 쓴다. 둘 다 1을 읽는 결과를 LKMM과 Arm 아키텍처는 허용하지만 실제 코어에서 거의 안 보인다(측정 3천만 번 0). 컨트롤 의존 + `smp_mb()`나 acquire/release로 막는다.

**Q.** How would you test memory-ordering code? Would you trust QEMU?

**A.** 세 겹으로 한다. 첫째 LKMM으로 논증 — 핵심 패턴을 litmus로 써서 herd7으로 돌린다. 둘째 실제 하드웨어에서 stress — 두 스레드를 다른 코어에 고정하고 슬롯마다 출발을 맞춰 수백만 번 돌리면 드문 재배치가 잡힌다. 셋째 KCSAN으로 표시 안 된 동시 접근을 찾는다. QEMU는 믿지 않는다 — 직접 해 보니 MTTCG에서는 호스트의 약한 순서가 비쳐서 relaxed 재배치가 보이긴 하지만, 하드웨어가 금지하는 `stlr`→`ldar` SB도 수천 번 깨졌고, 단일 스레드 TCG에선 아무것도 재배치되지 않았다. 에뮬레이터는 하드웨어보다 약할 수도 강할 수도 있다.

> Three layers. Reason with the LKMM — write the core pattern as a litmus test and run herd7. Stress on real hardware — pin two threads to different cores, synchronize the start of every iteration, and run millions of iterations; that's how rare reorderings show up. And run KCSAN to catch unmarked concurrent accesses. I wouldn't trust QEMU: when I tried, multi-threaded TCG leaked the host's weak ordering so relaxed reorderings showed up, but it also broke a store-release/load-acquire SB case that real arm64 forbids, and single-threaded TCG reordered nothing at all. An emulator can be weaker or stronger than the hardware.

---

## 11. 직접 해보기

1. (개념) `smp_rmb()`, `rmb()`, `dma_rmb()`가 arm64에서 각각 어떤 명령이 되고, 어떤 관찰자에 대한 순서인지 표로 써라. UP 커널(`CONFIG_SMP=n`)에서는 무엇이 바뀌나?
   정답/힌트: `dmb ishld` / `dsb ld` / `dmb oshld`. UP에서는 `smp_rmb()`만 `barrier()`로 줄어든다(`asm-generic/barrier.h` 110~122행).

2. (코드) ex1의 폴링 루프를 `smp_cond_load_acquire()`와 같은 동작을 하도록 유저스페이스에서 `ldar` + `wfe`로 다시 짜고(`sevl; wfe` 트릭은 빼도 된다), objdump로 루프 안에 `ldar`이 있는지 확인하라.
   정답/힌트: kbar.h의 `smp_load_acquire()`를 루프 조건에 쓰면 된다. `wfe`는 EL0에서도 실행 가능하지만, 깨워 줄 이벤트(`sev`나 exclusive monitor 해제)가 없으면 이벤트 스트림(`evtstrm`, cpuinfo에 있음) 주기마다 깬다.

3. (측정) ex3에 `S` 패턴(`S+fencewmbonceonce+poacquireonce.litmus`)을 추가하라: P0 `WRITE_ONCE(x, 2); smp_wmb(); WRITE_ONCE(y, 1);`, P1 `r0 = smp_load_acquire(&y); WRITE_ONCE(x, 1);`. 금지 결과는 `r0 == 1 && x == 2`. 배리어를 빼면 관찰되나?
   정답/힌트: 최종 x 값을 슬롯마다 기록해야 한다. 배리어를 뺀 버전에서 관찰되는지는 코어에 따라 다르다 — 결과를 보고 "허용되지만 관찰 안 됨"과 "금지됨"을 구분해서 기록하라.

4. (커널) `f5_barrier`에 `smp_mb__after_atomic()` 변형을 추가하라: SB의 store를 `atomic_set()` 대신 `atomic_inc()` + `smp_mb__after_atomic()`으로 바꿨을 때 0이 되는지 QEMU `thread=multi`에서 확인하고, objdump로 `stadd`/LL-SC 뒤에 `dmb ish`가 있는지 보라.
   정답/힌트: §2.5의 `f5_after_atomic` 출력과 같은 모양이 나와야 한다. QEMU 결과는 §4.2의 이유로 참고만 한다.

5. (관찰) 랩 소스에서 `drivers/net/ethernet/` 아래 `dma_rmb()`를 쓰는 디스크립터 회수 경로 하나를 찾아, "OWN/status 읽기 → `dma_rmb()` → 나머지 필드 읽기" 순서를 확인하라.
   정답/힌트: `grep -rn "dma_rmb()" drivers/net/ethernet/stmicro/stmmac/` — `stmmac_main.c`의 TX 완료 처리 루프에 "Make sure descriptor fields are read after reading the own bit." 주석과 함께 있다.

6. (개념) `writel(START, reg)` 직전에 coherent 버퍼에 데이터를 썼다. `dma_wmb()`를 따로 넣어야 하나? `writel_relaxed()`라면? streaming 버퍼라면?
   정답/힌트: `writel`이면 불필요(`__io_bw` = `dma_wmb`). relaxed면 `wmb()`(또는 최소 `dma_wmb()` — 문서상 `dma_*`는 MMIO 순서를 보장하지 않으므로 이식성 있게는 `wmb()`). streaming이면 순서보다 먼저 `dma_map_single()`/`dma_sync_single_for_device()`가 필요하다.

7. (토론) "`smp_store_release()`/`smp_load_acquire()`로 SB를 고쳤고 arm64 보드에서 1억 번 테스트해서 0이 나왔다"는 PR 설명에 리뷰 코멘트를 써라.
   정답/힌트: LKMM은 release→acquire의 store→load 순서를 보장하지 않는다. arm64 `ldar`이 RCsc라 우연히 통과했을 뿐, LTO 빌드(`READ_ONCE`가 `ldapr`)나 다른 아키텍처에서 깨진다(§3.6의 `ldapr` 3만 번). `smp_mb()`로.

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| compiler barrier | 컴파일러 배리어 | `barrier()`. 명령 0개, 컴파일러의 메모리 접근 이동만 막는다 |
| `READ_ONCE`/`WRITE_ONCE` | 한 번 접근 표시 | volatile 캐스트. tearing·fusing·hoisting·invented load 금지, CPU 순서는 아님 |
| tearing | 쪼개기 | 한 접근이 더 작은 접근 여러 개로 나뉨 (`stp w1, w1`) |
| fusing | 합치기 | 같은 위치의 접근 여러 개가 하나로 합쳐짐 |
| weakly ordered | 약한 순서 | 다른 주소 간 load/store 순서를 하드웨어가 지켜 주지 않음 (arm64, Power, RISC-V) |
| TSO | Total Store Order | x86 모델. store→load 재배치만 허용 |
| store buffer | 스토어 버퍼 | 캐시에 반영 전 store가 머무는 코어 내부 버퍼. SB 패턴의 원인 |
| DMB | Data Memory Barrier | 앞뒤 메모리 접근의 관찰 순서를 정함. 실행은 계속 |
| DSB | Data Synchronization Barrier | 앞의 접근이 완료될 때까지 모든 명령 정지 |
| ISH / OSH / SY | shareability domain | CPU들 / 장치(DMA)까지 / 시스템 전체 |
| LDAR / STLR | acquire load / release store | 배리어를 접근에 붙인 명령. RCsc |
| LDAPR | RCpc acquire load | ARMv8.3. 앞의 `STLR`과 순서를 안 지킴 |
| acquire / release | 획득 / 해제 의미 | 뒤 접근이 못 올라옴 / 앞 접근이 못 내려감. 짝으로 MP를 막음 |
| full barrier | 전체 배리어 | 모든 종류 순서 보장. `smp_mb()`. SB를 막는 유일한 배리어 |
| LKMM | Linux Kernel Memory Model | `tools/memory-model`의 형식 모델. herd7으로 litmus 판정 |
| litmus test | 리트머스 테스트 | 특정 순서 결과가 허용되는지 묻는 작은 동시성 프로그램 |
| MP / SB / LB | 패턴 이름 | message passing / store buffering / load buffering |
| other-multi-copy atomic | 다중 복사 원자성 | 한 store가 다른 관찰자 하나에게 보이면 모두에게 보임 (Armv8) |
| control / address dependency | 컨트롤 / 주소 의존 | 앞 load 값에 달린 분기·주소로 생기는 순서. 깨지기 쉬움 |
| Device-nGnRE | 장치 메모리 속성 | 합치지 않음·같은 장치 순서 유지·early write ack (posted) |
| posted write | 응답 없는 쓰기 | 장치 도착 전에 CPU가 완료로 간주. read-back으로 flush |
| `_relaxed` 접근자 | 약한 MMIO 접근 | 같은 장치끼리 순서만. 메모리·락·delay와 순서 없음 |
| coherent DMA memory | 일관 DMA 메모리 | `dma_alloc_coherent()`. non-coherent arm64에선 Normal-NC 매핑, `dma_*` 배리어 필요 |
| streaming DMA | 스트리밍 DMA | `dma_map_*`. 캐시 유지보수(clean/invalidate)로 소유권 이전 |
| alternative | 런타임 명령 패치 | 부팅 시 CPU 기능·erratum에 따라 명령을 바꿔 끼움 (LSE, `ldr`→`ldar`) |
| KCSAN | Kernel Concurrency Sanitizer | 표시 안 된 동시 접근(data race)을 런타임에 탐지 |

---

## 13. 요약 & 체크리스트

메모리 순서는 **컴파일러**와 **CPU** 두 단계에서 바뀐다. 컴파일러 단계는 `barrier()`(명령 0개)와 `READ_ONCE`/`WRITE_ONCE`(volatile 접근 — 실측: 폴링 루프가 `b .`로 컴파일되는 걸 막음)로 다룬다. CPU 단계는 관찰자에 따라 세 가족을 쓴다: CPU끼리는 `smp_*`(arm64 `dmb ish*`, `ldar`/`stlr`), DMA coherent 메모리는 `dma_*`(`dmb osh*`), MMIO까지 포함하면 `mb()`/`rmb()`/`wmb()`(`dsb`, 7~8배 비쌈). MP는 쓰는 쪽과 읽는 쪽의 짝(`smp_wmb`+`smp_rmb` 또는 release+acquire)으로, SB는 `smp_mb()`로만 막는다 — 실제 M2 코어에서 relaxed MP가 1천만 번에 수백 번, SB가 1만 번 넘게 깨졌고, 올바른 짝에서는 0이었다. MMIO는 `writel` = `dmb oshst` + `str`, `readl` = `ldr` + `dmb oshld` + 가짜 의존이며, `_relaxed`는 같은 장치끼리만 순서를 준다. DMA 디스크립터는 "내용 → `dma_wmb()` → OWN → `writel(doorbell)`", 완료는 "status → `dma_rmb()` → 데이터", posted write는 같은 장치 `readl`로 flush한다. QEMU TCG는 순서 검증 도구가 아니다(`stlr`→`ldar` SB가 깨지고, 단일 스레드 모드는 아무것도 재배치하지 않는다).

- [ ] 컴파일러 재배치와 CPU 재배치를 구분하고, 각각을 막는 도구를 말할 수 있다
- [ ] `READ_ONCE`/`WRITE_ONCE`가 막는 네 가지 변환(hoisting, fusing, tearing, invented load)을 objdump 예로 설명할 수 있다
- [ ] `smp_mb/rmb/wmb`, `mb/rmb/wmb`, `dma_mb/rmb/wmb`가 arm64에서 어떤 명령인지 외우지 않고 모듈 objdump로 확인할 수 있다
- [ ] `smp_load_acquire`/`smp_store_release`가 `ldar`/`stlr`이 되는 것과, `ldapr`(RCpc)와의 차이를 설명할 수 있다
- [ ] 값을 돌려주는 atomic과 안 돌려주는 atomic의 순서 차이, `smp_mb__before/after_atomic()`의 용도를 말할 수 있다
- [ ] MP, SB, LB litmus를 그리고 각각을 막는 최소 배리어를 고를 수 있다
- [ ] 두 스레드를 코어에 고정하고 슬롯마다 동기화하는 litmus harness를 짜서 재배치 횟수를 측정할 수 있다
- [ ] `readl`/`writel`과 `_relaxed`의 차이를 arm64 소스(`__io_ar`, `__io_bw`)로 설명할 수 있다
- [ ] "디스크립터 → `dma_wmb()` → OWN → `writel(doorbell)`"과 완료 경로의 `dma_rmb()`를 코드로 짤 수 있다
- [ ] posted write와 read-back flush, `wmb()`로는 안 되는 이유를 말할 수 있다
- [ ] coherent와 streaming DMA에서 배리어와 캐시 유지보수의 분업을 설명할 수 있다
- [ ] QEMU에서 순서 테스트가 왜 신뢰할 수 없는지 실측 근거로 말할 수 있다

## 참고 자료

- 커널 문서 (랩 소스와 같은 내용이 docs.kernel.org에 있다): `Documentation/memory-barriers.txt` — 특히 "KERNEL I/O BARRIER EFFECTS"와 `dma_wmb()` 절. [docs.kernel.org — Linux Kernel Memory Barriers](https://docs.kernel.org/core-api/wrappers/memory-barriers.html)
- `Documentation/atomic_t.txt` — atomic 연산의 순서 규칙 (relaxed vs fully ordered, `smp_mb__before/after_atomic`)
- `Documentation/core-api/circular-buffers.rst` — SPSC 링버퍼와 acquire/release. [docs.kernel.org — Circular Buffers](https://docs.kernel.org/core-api/circular-buffers.html)
- `Documentation/driver-api/device-io.rst` — `ioremap`/`ioremap_np`, posted write, `readl`/`writel` 순서. [docs.kernel.org — Bus-Independent Device Accesses](https://docs.kernel.org/driver-api/device-io.html)
- `Documentation/core-api/dma-api.rst`, `dma-api-howto.rst` — coherent vs streaming (H9에서 자세히)
- `Documentation/process/volatile-considered-harmful.rst`
- `tools/memory-model/` — LKMM, `Documentation/simple.txt`, `recipes.txt`, `cheatsheet.txt`, `explanation.txt`, `litmus-tests/`
- 랩 소스 위치: `arch/arm64/include/asm/barrier.h`, `arch/arm64/include/asm/io.h`, `arch/arm64/include/asm/rwonce.h`, `include/asm-generic/barrier.h`, `include/asm-generic/io.h`, `include/asm-generic/rwonce.h`, `arch/arm64/mm/dma-mapping.c`, `arch/arm64/mm/cache.S` — [elixir.bootlin.com/linux/v6.18/source/arch/arm64/include/asm/barrier.h](https://elixir.bootlin.com/linux/v6.18/source/arch/arm64/include/asm/barrier.h)
- Paul E. McKenney, "Is Parallel Programming Hard, And, If So, What Can You Do About It?" — 메모리 순서 장(Memory Ordering)과 부록의 하드웨어 설명
- Arm Architecture Reference Manual for A-profile (Arm DDI 0487) — B2장 "The AArch64 Application Level Memory Model" (DMB/DSB, shareability, LDAR/STLR/LDAPR, Device 메모리 속성)
- herdtools7 (herd7, klitmus7) — LKMM litmus를 실행하는 외부 도구. 이 노트에서는 설치·실행하지 않았다
- 선행·후속 노트: A8 §7(C11 acquire/release, MP 첫 실험), E5 §5.6, F3(sleep/wakeup), F4(락·RCU), H9(DMA API)
