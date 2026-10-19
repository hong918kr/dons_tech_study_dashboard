# M8. 버그 탐지기 — KASAN · KFENCE · kmemleak · UBSAN · KCSAN · lockdep · `CONFIG_DEBUG_*`

> **이 노트를 다 읽으면**: KASAN 리포트(slab-out-of-bounds, use-after-free, stack/global OOB)를 첫 줄부터 마지막 shadow 덤프까지 읽고 버그 줄을 짚을 수 있다 · KASAN의 1:8 shadow memory·redzone·quarantine, KFENCE의 guard page 샘플링, kmemleak의 mark-and-sweep, UBSAN·KCSAN의 원리를 그림으로 설명할 수 있다 · 각 탐지기의 비용(이미지 크기·부팅 시간·메모리·syscall 비용)을 실측값으로 말할 수 있다 · "개발 PC → CI → 양산 필드"에 어떤 탐지기를 켤지 근거를 대며 고를 수 있다
> **왜 필요한가**: "드라이버에서 가끔 메모리가 깨진다, 어떻게 찾나?", "KASAN을 양산에 켤 수 있나? KFENCE는? MTE는?"은 커널·드라이버 면접의 단골이고, 실무에서는 "1주일에 한 번 나는 패닉"(contents.md 시나리오)을 재현 가능한 한 줄짜리 리포트로 바꾸는 도구다
> **Don 기준 난이도**: SSD 펌웨어에서 쓰던 메모리 가드 패턴(canary, fill pattern 0xDEADBEEF, MPU 가드 영역, 해제 후 poison)은 이미 안다 / 새로 배울 부분은 "컴파일러가 모든 load/store 앞에 검사 코드를 심는다"는 발상, shadow memory 인코딩, 샘플링 탐지기(KFENCE)와 하드웨어 태그(MTE)의 비용 모델, KUnit 테스트 스위트로 탐지기 자체를 검증하는 법
> **선행 노트**: E2(커널 config·빌드), E3(모듈), E4 §5(DEBUG_ATOMIC_SLEEP)·§7(KASAN 스택 크기), F2 §10(문맥 위반 로그), F4 §13(lockdep), F9 §9(slab_debug·page_owner), M3(Oops 해독) — M1(printk)·M5(ftrace)·L7(크래시 수집)과 이어진다

---

## 0. 큰 그림 — 이게 왜 필요한가

메모리 버그의 가장 나쁜 성질은 **원인과 증상이 시간·공간으로 떨어져 있다**는 것이다. 드라이버 A가 버퍼 끝을 4바이트 넘겨 썼다(원인, t0). 그 4바이트는 옆 객체 — 예를 들어 네트워크 스택의 `sk_buff` 포인터 — 위에 있었다. 몇 초 뒤, 혹은 몇 시간 뒤 네트워크 코드가 그 포인터를 따라가다 `Unable to handle kernel paging request`로 죽는다(증상, t1). M3에서 배운 Oops 해독을 아무리 잘해도 콜스택은 **네트워크 코드**를 가리킨다. 범인 드라이버는 스택 어디에도 없다.

Don이 SSD 펌웨어에서 겪었던 "어느 날 FTL 테이블 한 엔트리가 깨져 있다"와 같은 종류다. 그때 했던 일 — 버퍼 앞뒤에 canary를 두고, 해제한 메모리를 0xDEADBEEF로 채우고, MPU로 가드 영역을 만들어 쓰는 순간 abort가 나게 하기 — 을 Linux 커널은 **체계적인 도구 모음**으로 갖고 있다. 이 노트의 주인공들이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <line x1="30" y1="150" x2="630" y2="150" stroke="currentColor" stroke-width="1.5" marker-end="url(#ah1)"/>
  <text x="600" y="172" font-size="12">시간</text>
  <rect x="40" y="70" width="170" height="60" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="125" y="95" font-size="13" text-anchor="middle">t0 원인: 드라이버가</text>
  <text x="125" y="115" font-size="13" text-anchor="middle">버퍼 끝을 4B 넘겨 씀</text>
  <rect x="440" y="70" width="190" height="60" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="535" y="95" font-size="13" text-anchor="middle">t1 증상: 남의 코드에서</text>
  <text x="535" y="115" font-size="13" text-anchor="middle">Oops (스택에 범인 없음)</text>
  <text x="325" y="105" font-size="13" text-anchor="middle">몇 초 ~ 몇 시간, 재현 불가</text>
  <line x1="215" y1="100" x2="435" y2="100" stroke="#888" stroke-dasharray="5 4"/>
  <line x1="125" y1="135" x2="125" y2="190" stroke="#3f9a6b" stroke-width="1.5" marker-end="url(#ah1)"/>
  <rect x="30" y="195" width="300" height="88" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="180" y="218" font-size="13" text-anchor="middle">탐지기: t0 그 명령에서 바로 리포트</text>
  <text x="180" y="240" font-size="12" text-anchor="middle">KASAN · KFENCE · UBSAN · KCSAN · lockdep</text>
  <text x="180" y="262" font-size="12" text-anchor="middle">범인 줄 + 할당/해제 스택이 같이 나온다</text>
  <rect x="350" y="195" width="280" height="88" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="490" y="218" font-size="13" text-anchor="middle">사후 검사기: 나중에 발견</text>
  <text x="490" y="240" font-size="12" text-anchor="middle">slab_debug(F9) · kmemleak · DEBUG_LIST</text>
  <text x="490" y="262" font-size="12" text-anchor="middle">"깨졌다"는 알지만 언제인지는 모름</text>
</svg>
```
그림 1 — 메모리 버그는 원인(t0)과 증상(t1)이 떨어져 있다. 탐지기의 가치는 t1을 t0으로 당겨 오는 것이다.

탐지기는 크게 네 부류다.

| 부류 | 원리 | 예 | 비용 |
|---|---|---|---|
| 컴파일러 계측 (모든 접근) | 컴파일러가 load/store·연산 앞에 검사 호출을 심는다 | KASAN generic/SW_TAGS, UBSAN, KCSAN | 크다 (2~3배 느림, 메모리 1/8 이상) |
| 샘플링 | 할당 일부만 특별한 페이지에 놓는다 | KFENCE | 거의 0 → 양산 가능 |
| 하드웨어 태그 | CPU가 포인터 태그와 메모리 태그를 비교 | KASAN HW_TAGS (arm64 MTE) | 작다 (ARMv8.5+ 칩 필요) |
| 런타임 상태 추적 | 커널 자료구조에 검사 로직을 넣는다 | lockdep, DEBUG_ATOMIC_SLEEP, DEBUG_OBJECTS, DEBUG_LIST, DMA_API_DEBUG, kmemleak, slab_debug | 기능별로 작음~중간 |

이 노트는 랩의 **`Image-kasan`**(6.18.55-kasan) 커널로 각 탐지기를 직접 돌린다. 버그를 일부러 만드는 코드는 새로 짜지 않는다. 대신 **커널 업스트림이 탐지기 검증용으로 들고 있는 테스트 스위트** — KASAN KUnit `kasan_test`, KFENCE KUnit `kfence_test`, `lib/test_ubsan.c`, `samples/kmemleak/kmemleak-test.c` — 를 그대로 돌려 "진짜 리포트"를 얻는다. 이것이 실무에서도 맞는 방법이다: 새 SoC·새 컴파일러에서 KASAN이 제대로 동작하는지부터 이 스위트로 확인한다.

### 0.1 실행 환경

- VM: Lima `elinux` (aarch64, Ubuntu 26.04, `uname -r` = `7.0.0-38-generic`). 게스트는 QEMU `virt` + `-cpu cortex-a57 -smp 2 -m 1G`, **TCG 에뮬레이션**(KVM 없음). 모든 시간 측정은 TCG + 17명이 같이 쓰는 VM 부하 위의 값이라 **실기 숫자가 아니다**. 비율(같은 조건에서 커널 A 대 B)만 의미가 있다.
- 커널: `/var/lab/out/Image-trace`(6.18.55-trace, 기준선 — ftrace 전부 + 전체 DWARF) 과 `/var/lab/out/Image-kasan`(6.18.55-kasan = trace 설정 + 아래 탐지기 전부).
- 게스트 유저랜드: BusyBox initramfs 복사본(`/var/tmp/M8/`) + 정적 링크 테스트 바이너리 + `.ko`. 실행기는 아래 스크립트다(이후 예제가 전부 이걸 쓴다).

```sh
#!/bin/bash
# m8_run.sh KERNEL TAG SCRIPT [extra cmdline]  -> /var/tmp/M8/TAG.log
set -e
K=$1; TAG=$2; SCR=$3; shift 3; EXTRA="$*"
W=/var/tmp/M8; R=$W/r_$TAG
rm -rf $R; mkdir -p $R; cp -a /var/lab/build/rootfs/. $R/
mkdir -p $R/m8; cp $W/m8_bench $R/m8/ 2>/dev/null || true
cp $W/ko/*.ko $R/m8/ 2>/dev/null || true
cp $SCR $R/m8/t.sh; chmod +x $R/m8/t.sh
(cd $R && find . | cpio -o -H newc --quiet | gzip -1) > $W/i_$TAG.gz
rm -rf $R
T0=$(date +%s.%N)
timeout ${TMO:-900} qemu-system-aarch64 -machine virt -cpu cortex-a57 -smp 2 -m ${MEM:-1G} -nographic \
  -kernel /var/lab/out/$K -initrd $W/i_$TAG.gz \
  -append "console=ttyAMA0 LAB_CMD=sh~/m8/t.sh $EXTRA" > $W/$TAG.log 2>&1 || true
T1=$(date +%s.%N)
echo "HOSTWALL $(echo "$T1 - $T0" | bc)" >> $W/$TAG.log
rm -f $W/i_$TAG.gz
```

랩 initramfs의 `/init`은 `LAB_CMD=`에 준 명령 하나를 실행하고 전원을 끈다(C 모듈 이후 공용 랩 규칙). 여기서는 그 명령이 `sh /m8/t.sh`이고, 예제마다 `t.sh` 내용만 바꾼다.

## 1. 디버그 커널이라는 물건 — `Kernel hacking` 메뉴와 랩의 `Image-kasan`

탐지기는 전부 **Kconfig 옵션**이다(E2). `make menuconfig`의 맨 아래 `Kernel hacking` 메뉴(`lib/Kconfig.debug`, `lib/Kconfig.kasan`, `lib/Kconfig.kfence`, `lib/Kconfig.ubsan`, `lib/Kconfig.kcsan`)에 모여 있고, 대부분 `CONFIG_DEBUG_KERNEL=y`가 있어야 보인다. 실무에서는 같은 소스로 **두 벌의 커널**을 만든다: 양산용(탐지기 거의 없음)과 디버그용(아래 옵션 다수). 랩도 그렇게 되어 있다.

### 1.1 예제 1 — 두 커널의 탐지기 옵션 비교

무엇을 확인하나: 기준 커널(`Image-trace`)과 디버그 커널(`Image-kasan`)이 정확히 어떤 탐지기 옵션에서 다른지.

```sh
for k in trace kasan; do echo "== $k"
  grep -E "^CONFIG_(KASAN|KASAN_GENERIC|KASAN_INLINE|KASAN_VMALLOC|KASAN_STACK|KASAN_KUNIT_TEST|KFENCE|KFENCE_SAMPLE_INTERVAL|KFENCE_NUM_OBJECTS|KFENCE_KUNIT_TEST|DEBUG_KMEMLEAK|DEBUG_KMEMLEAK_AUTO_SCAN|UBSAN|UBSAN_BOUNDS|UBSAN_SHIFT|UBSAN_BOOL|UBSAN_ENUM|TEST_UBSAN|KCSAN|PROVE_LOCKING|LOCK_STAT|DEBUG_ATOMIC_SLEEP|DEBUG_OBJECTS|DEBUG_OBJECTS_TIMERS|DEBUG_LIST|DEBUG_SG|DEBUG_PAGEALLOC|SLUB_DEBUG|SLUB_DEBUG_ON|DMA_API_DEBUG|DEBUG_VM|KUNIT)=" \
    /var/lab/build/linux-$k/.config | sed "s/^CONFIG_//" | paste -sd" " | fold -s -w 100
done
```

```text
== trace
SLUB_DEBUG=y
== kasan
DMA_API_DEBUG=y UBSAN=y UBSAN_BOUNDS=y UBSAN_SHIFT=y UBSAN_BOOL=y UBSAN_ENUM=y TEST_UBSAN=m 
DEBUG_PAGEALLOC=y SLUB_DEBUG=y SLUB_DEBUG_ON=y DEBUG_KMEMLEAK=y DEBUG_KMEMLEAK_AUTO_SCAN=y 
DEBUG_OBJECTS=y DEBUG_OBJECTS_TIMERS=y DEBUG_VM=y KASAN=y KASAN_GENERIC=y KASAN_INLINE=y 
KASAN_STACK=y KASAN_VMALLOC=y KASAN_KUNIT_TEST=m KFENCE=y KFENCE_SAMPLE_INTERVAL=100 
KFENCE_NUM_OBJECTS=255 KFENCE_KUNIT_TEST=m PROVE_LOCKING=y LOCK_STAT=y DEBUG_ATOMIC_SLEEP=y 
DEBUG_LIST=y DEBUG_SG=y KUNIT=y
```

출력에서 볼 것: 기준 커널에는 `SLUB_DEBUG=y`(코드만 들어 있고 부팅 인자 `slab_debug=`로 켬, F9 §9)뿐이다. 디버그 커널은 KASAN generic(inline + 스택 + vmalloc), KFENCE(100 ms, 255 객체), kmemleak(자동 스캔), UBSAN 4종, lockdep(+LOCK_STAT), DEBUG_ATOMIC_SLEEP, DEBUG_OBJECTS(+timer), DEBUG_LIST/SG/PAGEALLOC/VM, SLUB_DEBUG_ON(=`slab_debug`가 기본 켜짐), DMA_API_DEBUG와 KUnit 테스트 모듈(=m)을 갖는다. `KCSAN`은 줄에 없다 — KASAN과 같이 켤 수 없다(§7). `UBSAN_DIV_ZERO`·`UBSAN_ALIGNMENT`도 없다(출력에 없는 옵션은 `is not set`). 실무에서 "디버그 커널"이라고 하면 대략 이 묶음이다. 아래 비용 측정(§10)도 이 묶음 전체의 비용이라는 점을 기억하자.

랩 문제 하나: 처음 빌드된 `Image-kasan`에는 KUnit 테스트 모듈이 빠져 있었다(빌드 스크립트의 `KASAN_KUNIT_TEST=m` 지정 방식 문제). 랩 관리자가 같은 버전으로 다시 빌드해 위처럼 `=m`이 들어갔고, 이 노트의 모든 실행은 다시 빌드된 커널(`#2`)로 했다. 실무에서도 흔한 함정이다 — `scripts/config`로 넣은 값은 `olddefconfig`가 의존성 때문에 조용히 지울 수 있으니 **빌드 후 `.config`를 꼭 grep으로 확인한다**.

### 1.2 lockdep — 이미 F4에서 돌려 봤다 (요약)

lockdep(`CONFIG_PROVE_LOCKING`)은 F4 §13에서 실제로 돌렸다. 핵심만 다시 정리한다. lockdep은 개별 락이 아니라 **락 클래스**(같은 `spin_lock_init` 자리에서 초기화된 락 전부)를 단위로, "A를 쥔 채 B를 잡았다"는 **순서 간선**과 "이 락은 hardirq 안에서도 쓰인다" 같은 **사용 상태**를 기록한다. 그래서 실제 deadlock이 한 번도 일어나지 않아도 ABBA 사이클(`possible circular locking dependency detected`)과 IRQ 상태 불일치(`inconsistent {IN-HARDIRQ-W} -> {HARDIRQ-ON-W} usage`)를 **처음 그 경로를 지나는 순간** 보고한다. F4 예제 14(`f4_abba`)는 두 스레드가 겹치지 않게 돌아도 잡히는 것을, 예제 15는 `/proc/lockdep`에 쌓인 의존 그래프를 보여 줬다. F2 §10은 같은 디버그 커널에서 `DEBUG_ATOMIC_SLEEP`(E4 §5)이 "BUG: sleeping function called from invalid context"를 찍는 네 가지 문맥 위반을 다뤘다. 이 노트에서 새로 볼 것은 딱 하나다: **lockdep은 첫 보고 뒤 스스로 꺼진다**(`debug_locks=0`). 한 부팅에 한 개의 lockdep 버그만 볼 수 있다는 뜻이고, CI가 "lockdep 경고 0개"를 판정할 때 이 값을 본다.

#### 예제 2 — `Image-kasan`의 lockdep 상태와 통계

무엇을 확인하나: 탐지기가 잔뜩 켜진 커널이 부팅 직후 이미 몇 개의 락 클래스·의존 간선을 추적하고 있는지, `debug_locks`가 살아 있는지.

```sh
# 게스트 t_misc.sh 안에서
grep -E "lock-classes|direct dependencies|dependency chains|in-hardirq chains|hardirq-safe locks|debug_locks" /proc/lockdep_stats
```

```text
 lock-classes:                          793 [max: 8192]
 direct dependencies:                  5178 [max: 32768]
 indirect dependencies:               17256
 all direct dependencies:             45838
 dependency chains:                    5195 [max: 65536]
 in-hardirq chains:                      29
 hardirq-safe locks:                     22
 max locking depth:                      12
 debug_locks:                             1
```

출력에서 볼 것: BusyBox 셸이 뜬 직후(부팅 약 22초, TCG)인데도 793개 락 클래스와 5178개 직접 의존 간선이 이미 기록돼 있다. 대괄호 `[max: …]`가 lockdep 표의 크기 상한이고(부팅 로그에 `memory used by lock dependency info: 6941 kB`가 찍힌다), 상한에 닿으면 lockdep은 "BUG: MAX_LOCKDEP_KEYS too low!" 같은 메시지와 함께 꺼진다. 마지막 줄 `debug_locks: 1`이 "lockdep이 아직 살아 있다"는 표시다. 어떤 lockdep 경고든 한 번 나면(그리고 §3에서 볼 KASAN 리포트가 나도) 이 값이 0이 된다. 참고로 `/proc/sys/kernel/debug_locks` 같은 sysctl 파일은 없다 — 이 줄이 유일한 런타임 확인 지점이다.

---

## 2. KASAN의 원리 — 컴파일러가 모든 load/store 앞에 검사를 심는다

### 2.1 직관 — "주소마다 붙은 접근 허가 비트맵"

Don이 SSD 펌웨어에서 "이 DRAM 영역은 지금 써도 되나"를 검사하려 했다면 아마 **비트맵 하나**를 두고, 버퍼를 할당할 때 비트를 켜고 해제할 때 끄고, 모든 접근 전에 비트를 확인했을 것이다. 문제는 "모든 접근 전에"다 — 손으로는 불가능하다. KASAN(Kernel Address SANitizer)은 이걸 **컴파일러**에게 시킨다. `-fsanitize=kernel-address`로 빌드하면 gcc/clang이 커널의 모든 load/store 앞에 "이 주소의 허가 비트를 보고, 금지면 리포트 함수를 불러라"는 코드를 끼워 넣는다. 그 허가 비트맵이 **shadow memory**다.

### 2.2 정의 — shadow memory 1:8 인코딩 (generic KASAN)

- 커널 메모리 **8바이트(granule)마다 shadow 1바이트**. 그래서 shadow는 커널 주소 공간의 1/8을 차지한다.
- 주소 → shadow 주소: `shadow = (addr >> 3) + KASAN_SHADOW_OFFSET`. arm64 48비트 VA·generic에서 `KASAN_SHADOW_OFFSET = 0xdfff800000000000`(`arch/arm64/Kconfig`).
- shadow 값의 뜻: `0x00` = 8바이트 전부 접근 가능, `0x01`~`0x07` = 앞의 N바이트만 가능(객체 끝이 granule 중간에 있을 때), 음수(최상위 비트 1) = 전부 금지이고 값이 **이유**를 말한다.

| shadow 값 | 매크로 (`mm/kasan/kasan.h`) | 뜻 |
|---|---|---|
| `fc` | `KASAN_SLAB_REDZONE` | slab 객체 뒤 redzone |
| `fb` | `KASAN_SLAB_FREE` | 해제된 slab 객체 |
| `fa` | `KASAN_SLAB_FREE_META` | 해제된 객체 + 해제 스택 메타데이터 |
| `fe` | `KASAN_PAGE_REDZONE` | `kmalloc_large`(페이지 단위) 뒤 redzone |
| `ff` | `KASAN_PAGE_FREE` | 해제된 페이지 |
| `f9` | `KASAN_GLOBAL_REDZONE` | 전역 변수 뒤 redzone |
| `f1`/`f2`/`f3` | `KASAN_STACK_LEFT/MID/RIGHT` | 스택 변수 사이 redzone |
| `f8` | `KASAN_VMALLOC_INVALID` | vmalloc 영역의 빈 공간 |
| `ca`/`cb` | `KASAN_ALLOCA_LEFT/RIGHT` | `alloca`/VLA 앞뒤 |

```svg
<svg viewBox="0 0 660 330" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="20" y="28" font-size="13">커널 메모리: kmalloc(13) → kmalloc-16 칸 + 뒤 redzone</text>
  <rect x="60" y="45" width="160" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="140" y="72" font-size="12" text-anchor="middle">8B 전부 유효</text>
  <rect x="220" y="45" width="100" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="270" y="72" font-size="12" text-anchor="middle">5B 유효</text>
  <rect x="320" y="45" width="60" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="350" y="72" font-size="12" text-anchor="middle">3B</text>
  <rect x="380" y="45" width="160" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="460" y="72" font-size="12" text-anchor="middle">redzone 8B</text>
  <text x="60" y="106" font-size="12">0x...00</text>
  <text x="220" y="106" font-size="12">0x...08</text>
  <text x="380" y="106" font-size="12">0x...10</text>
  <line x1="140" y1="112" x2="190" y2="200" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="300" y1="112" x2="250" y2="200" stroke="currentColor" marker-end="url(#ah2)"/>
  <line x1="460" y1="112" x2="310" y2="200" stroke="currentColor" marker-end="url(#ah2)"/>
  <text x="400" y="152" font-size="12">shadow = (addr &gt;&gt; 3)</text>
  <text x="400" y="170" font-size="12">  + 0xdfff800000000000</text>
  <text x="20" y="196" font-size="13">shadow memory (granule당 1B):</text>
  <rect x="160" y="205" width="60" height="40" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="190" y="230" font-size="13" text-anchor="middle">00</text>
  <rect x="220" y="205" width="60" height="40" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="250" y="230" font-size="13" text-anchor="middle">05</text>
  <rect x="280" y="205" width="60" height="40" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="310" y="230" font-size="13" text-anchor="middle">fc</text>
  <text x="20" y="280" font-size="12">검사(4바이트 store): s = shadow[addr]; s == 0 이면 통과, 아니면</text>
  <text x="20" y="300" font-size="12">(addr &amp; 7) + 3 &gt;= s 이면 __asan_report_store4_noabort(addr) 호출</text>
  <text x="20" y="320" font-size="12">음수(fc, fb …)는 부호 있는 비교에서 항상 금지가 된다</text>
</svg>
```
그림 2 — generic KASAN의 1:8 shadow. 객체 끝이 granule 중간이면 shadow에 "앞 N바이트만 유효"를 적고, 그 뒤는 이유 코드(`fc` = slab redzone)로 막는다.

### 2.3 예제 3 — 컴파일러가 심는 코드를 직접 보기

무엇을 확인하나: `*p = v` 한 줄이 KASAN 빌드에서 몇 개의 명령이 되는지. 커널을 빌드하지 않고 VM의 gcc 15로 함수 하나만 커널과 같은 플래그(`scripts/Makefile.kasan`: `-fsanitize=kernel-address -fasan-shadow-offset=...`, inline이면 `asan-instrumentation-with-call-threshold=10000`, outline이면 `0`)로 어셈블한다. 실행하지 않는 코드라 안전하다.

```c
/* m8_inst.c: KASAN 계측이 store 하나 앞에 무엇을 심는지 보기 (실행하지 않고 어셈블리만 본다) */
void set_u32(unsigned int *p, unsigned int v)
{
	*p = v;
}
```

```sh
F="-O2 -fsanitize=kernel-address -fasan-shadow-offset=0xdfff800000000000"
gcc $F --param asan-instrumentation-with-call-threshold=10000 -S -o - m8_inst.c   # INLINE
gcc $F --param asan-instrumentation-with-call-threshold=0     -S -o - m8_inst.c   # OUTLINE
```

INLINE (지시어 줄은 걸러 냈다):

```text
set_u32:
.LASANPC0:
.LFB0:
	lsr	x4, x0, 3
	mov	x3, 140737488355328
	movk	x3, 0xdfff, lsl 48
	ldrsb	w4, [x4, x3]
	and	x3, x0, 7
	add	w3, w3, 3
	cmp	w4, 0
	ccmp	w3, w4, 1, ne
	bge	.L11
	str	w1, [x0]
	ret
.L11:
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	str	x0, [sp, 16]
	str	w1, [sp, 28]
	bl	__asan_report_store4_noabort
	ldr	x2, [sp, 16]
	ldr	w1, [sp, 28]
	str	w1, [x2]
	ldp	x29, x30, [sp], 32
	ret
```

OUTLINE:

```text
set_u32:
	stp	x29, x30, [sp, -32]!
	mov	x29, sp
	stp	x19, x20, [sp, 16]
	mov	x19, x0
	mov	w20, w1
	bl	__asan_store4_noabort
	str	w20, [x19]
	ldp	x19, x20, [sp, 16]
	ldp	x29, x30, [sp], 32
	ret
```

출력에서 볼 것: INLINE에서 진짜 일은 `str w1, [x0]` 한 줄이고, 그 앞 9개 명령이 그림 2의 검사 그대로다 — `lsr x4, x0, 3`(addr >> 3), `mov/movk`로 `0xdfff800000000000`을 만들고(`140737488355328` = `0x800000000000`), `ldrsb`로 shadow를 **부호 있게** 읽고(그래서 `fc` 같은 값이 음수), `(addr & 7) + 3`(4바이트 접근의 마지막 바이트)과 비교한다. 정상 경로는 분기 하나로 빠져나간다. OUTLINE은 같은 검사를 `__asan_store4_noabort()`(`mm/kasan/generic.c`의 `DEFINE_ASAN_LOAD_STORE(4)`) 호출로 바꿔 코드가 작지만 매번 함수 호출 비용이 든다. 랩 커널은 `KASAN_INLINE`이다 — 커널 문서 기준 inline이 빠르고 이미지가 크다. `_noabort`는 "리포트 후 계속 진행"이라는 뜻이다(커널은 기본적으로 첫 리포트에서 멈추지 않는다, `kasan.fault=panic`으로 바꿀 수 있다).

### 2.4 redzone과 quarantine — "옆을 막고, 해제한 건 바로 돌려주지 않는다"

shadow 비트맵만으로는 부족하다. 두 객체가 딱 붙어 있으면 A를 넘어 쓴 바이트가 **B의 정상 영역**이라 shadow가 `00`이다. 그래서 KASAN은 slab 할당자(SLUB)에 끼어들어 두 가지를 더 한다.

- **redzone**: 객체 뒤에 금지 구역을 둔다(shadow `fc`). `kmalloc(13)`이면 13~15번 바이트는 granule 부분값(`05`)으로, 그 뒤는 redzone으로 막힌다. 그래서 `kmalloc_oob_right`처럼 1바이트만 넘겨도 잡힌다.
- **quarantine**(generic 전용): `kfree()`된 객체를 즉시 free list로 돌려보내지 않고 FIFO 격리 구역에 잠시 둔다. shadow는 `fb`/`fa`. 그동안 누가 그 주소를 만지면 use-after-free로 잡힌다. 즉시 재사용되면 UAF 접근이 **새 주인의 정상 데이터**를 건드리는 것이라 못 잡는다. 격리 구역 크기는 전체 RAM의 1/32(`QUARANTINE_FRACTION 32`, `mm/kasan/quarantine.c`) 이고 CPU마다 1 MiB 배치로 비운다.
- **스택 기록**: 할당·해제할 때의 콜스택을 stack depot(중복 제거 저장소)에 넣고 객체 메타데이터에 핸들만 적는다. 리포트에 "Allocated by task …", "Freed by task …"가 나오는 이유다. Don의 펌웨어 경험으로 치면 "버퍼 헤더에 할당자 LR을 적어 두기"를 커널이 자동으로 해 주는 셈이다.

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="20" y="26" font-size="13">slab 한 페이지 (kmalloc-128, KASAN generic)</text>
  <rect x="20" y="40" width="130" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="85" y="67" font-size="12" text-anchor="middle">객체 A (사용 중)</text>
  <rect x="150" y="40" width="60" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="180" y="67" font-size="12" text-anchor="middle">rz fc</text>
  <rect x="210" y="40" width="130" height="44" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="275" y="67" font-size="12" text-anchor="middle">객체 B (해제됨) fb</text>
  <rect x="340" y="40" width="60" height="44" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="370" y="67" font-size="12" text-anchor="middle">rz fc</text>
  <rect x="400" y="40" width="130" height="44" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="465" y="67" font-size="12" text-anchor="middle">객체 C (사용 중)</text>
  <text x="85" y="108" font-size="12" text-anchor="middle">A[128] 쓰기</text>
  <line x1="150" y1="96" x2="170" y2="88" stroke="#d0564a" marker-end="url(#ah3)"/>
  <text x="290" y="108" font-size="12" text-anchor="middle">B 해제 후 읽기 → UAF</text>
  <line x1="275" y1="120" x2="275" y2="170" stroke="currentColor" marker-end="url(#ah3)"/>
  <rect x="120" y="175" width="430" height="50" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="335" y="197" font-size="13" text-anchor="middle">quarantine FIFO (RAM의 1/32): kfree한 객체가 여기서 대기</text>
  <text x="335" y="216" font-size="12" text-anchor="middle">B → … → 오래된 것부터 진짜 free list로</text>
  <line x1="550" y1="200" x2="610" y2="200" stroke="currentColor" marker-end="url(#ah3)"/>
  <text x="618" y="205" font-size="12">재사용</text>
  <rect x="20" y="245" width="620" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="330" y="264" font-size="12" text-anchor="middle">객체 메타데이터: alloc stack 핸들 + free stack 핸들 → stack depot</text>
  <text x="330" y="281" font-size="12" text-anchor="middle">리포트의 "Allocated by task …" / "Freed by task …" 가 여기서 나온다</text>
</svg>
```
그림 3 — redzone은 옆 객체로 넘어가는 접근을, quarantine은 해제 직후 재사용으로 UAF가 숨는 것을 막는다.

### 2.5 KASAN의 세 가지 모드

| 모드 | Kconfig | 원리 | shadow | 잡는 정밀도 | 비용 | 쓰는 곳 |
|---|---|---|---|---|---|---|
| Generic | `KASAN_GENERIC` | 컴파일러 계측 + 1:8 shadow | 메모리의 1/8 | 바이트 단위, quarantine | 가장 큼 | 개발·CI (모든 아키텍처) |
| Software tag-based | `KASAN_SW_TAGS` | arm64 TBI(포인터 최상위 바이트 무시)에 8비트 태그, 16B granule, 계측은 태그 비교 | 1/16 (`KASAN_SHADOW_SCALE_SHIFT := 4`) | 16B 단위, 확률적(태그 충돌 1/256) | 중간 | arm64 개발 기기에서 실기 테스트 |
| Hardware tag-based | `KASAN_HW_TAGS` | arm64 **MTE**(ARMv8.5): 4비트 태그를 CPU가 load/store마다 비교, 계측 없음 | 없음 (태그는 HW 저장) | 16B 단위, 충돌 1/16 | 작음 (sync < asymm < async 순으로 느림) | **양산 후보** (MTE 칩: Pixel 8+, 일부 Cortex-A7xx/X 계열) |

HW_TAGS는 부팅 인자 `kasan=on/off`, `kasan.mode=sync/async/asymm`, `kasan.stacktrace=off`(스택 수집 끄기, 비용 절감) 등을 받는다. sync는 위반한 바로 그 명령에서 tag check fault가 나서 정확한 리포트가 나오고, async는 "어디선가 위반이 있었다"는 플래그만 남겨 다음 커널 진입 때 보고한다(정확도↓ 비용↓). 랩의 QEMU `-cpu cortex-a57`에는 MTE가 없어서 HW_TAGS는 **가이드(실행 안 함)** 이다. QEMU는 `-machine virt,mte=on -cpu max`로 MTE를 에뮬레이션할 수 있지만 그 커널은 따로 빌드해야 한다.

## 3. KASAN 리포트 읽기 — 업스트림 `kasan_test` KUnit 스위트로

### 3.1 KUnit과 `kasan_test`

KUnit은 커널 안에서 도는 단위 테스트 프레임워크다(`CONFIG_KUNIT`). 테스트 함수 하나가 `KUNIT_CASE`, 묶음이 suite이고, 결과는 KTAP(TAP 형식) 줄로 dmesg에 찍힌다. `mm/kasan/kasan_test_c.c`는 KASAN 개발자들이 "KASAN이 이런 버그를 잡는가"를 검증하려고 만든 스위트로, 6.18 기준 75개 케이스가 있다(`kmalloc_oob_right`, `kmalloc_uaf`, `kasan_stack_oob`, `kasan_global_oob_right`, `kmalloc_double_kzfree`, `vmalloc_oob` …). 각 케이스는 `KUNIT_EXPECT_KASAN_FAIL(test, 식)` 매크로로 "이 식을 실행하면 KASAN 리포트가 **나와야** 한다"를 검사한다. 즉 리포트가 나오면 **ok**다. 매크로는 console tracepoint에 probe를 걸어 리포트 문자열이 찍혔는지 본다.

버그를 일으키는 코드를 새로 쓰지 않고 이 스위트를 쓰는 이유는 둘이다. (1) 커널 개발자가 이미 검증한, 정확히 한 종류씩만 버그를 내는 코드다. (2) 실무에서 새 보드·새 컴파일러로 KASAN 커널을 만들면 **제일 먼저 이 스위트를 돌려** 탐지기가 제대로 붙었는지 확인한다.

### 3.2 예제 4 — `kasan_test.ko` 실행

무엇을 확인하나: 랩 `Image-kasan`에서 75개 케이스가 몇 개 통과하고, 어떤 종류의 리포트가 몇 개 나오는지. 모듈은 랩 빌드가 만든 업스트림 모듈(`/var/lab/out/modules-kasan/lib/modules/6.18.55-kasan/kernel/mm/kasan/kasan_test.ko`)을 그대로 initramfs에 넣었다.

```sh
# VM: 업스트림 테스트 모듈을 initramfs에 넣을 디렉터리로
M=/var/lab/out/modules-kasan/lib/modules/6.18.55-kasan/kernel
mkdir -p /var/tmp/M8/ko && cp $M/mm/kasan/kasan_test.ko $M/mm/kfence/kfence_test.ko $M/lib/test_ubsan.ko /var/tmp/M8/ko/
cat > /var/tmp/M8/t_kasan.sh <<'EOF'
echo "M8UPTIME $(cut -d' ' -f1 /proc/uptime)"
cat /proc/cmdline
insmod /m8/kasan_test.ko
echo "M8DONE rc=$?"
EOF
cd /var/tmp/M8 && TMO=2400 ./m8_run.sh Image-kasan kasan3 t_kasan.sh log_buf_len=16M
grep -E "# Subtest|1\.\.75|not ok|# kasan: pass|# Totals" kasan3.log
grep "BUG: KASAN" kasan3.log | sed 's/.*BUG: KASAN: //; s/ in .*//' | sort | uniq -c
```

```text
[   23.282822][   T80]     # Subtest: kasan
[   23.286336][   T80]     1..75
[   25.160458][   T80]     not ok 63 kasan_strings
[   25.665495][   T80] # kasan: pass:62 fail:1 skip:12 total:75
[   25.665977][   T80] # Totals: pass:62 fail:1 skip:12 total:75
[   25.666322][   T80] not ok 1 kasan
      2 alloca-out-of-bounds
      5 double-free
      1 global-out-of-bounds
      4 invalid-free
      1 out-of-bounds
    131 slab-out-of-bounds
     22 slab-use-after-free
      1 stack-out-of-bounds
      4 use-after-free
      2 vmalloc-out-of-bounds
```

출력에서 볼 것:
- **62 통과, 12 건너뜀, 1 실패**, 리포트 173개. 건너뛴 것은 대부분 `# SKIP Test requires CONFIG_KASAN_GENERIC=n`(tag 모드 전용 테스트), `CONFIG_CC_IS_CLANG=y`(`kasan_global_oob_left`는 clang만 계측), `CONFIG_RUST=y`다. 즉 **모드·컴파일러마다 잡는 범위가 다르다**는 것을 스위트가 직접 알려 준다.
- 실패 1개(`kasan_strings`)의 내용은 다음과 같다.

```text
[   25.142995][  T206]     # kasan_strings: EXPECTATION FAILED at mm/kasan/kasan_test_c.c:1699
[   25.142995][  T206]     KASAN failure expected in "strscpy(ptr, src + KASAN_GRANULE_SIZE, KASAN_GRANULE_SIZE)", but none occurred
```

이미 해제된 버퍼의 첫 바이트부터 읽는 `strscpy()`를 KASAN이 놓쳤다는 뜻이다. 같은 테스트의 `strchr`·`strcmp` 등 다른 문자열 함수 UAF는 잡혔다(바로 뒤 `BUG: KASAN: slab-use-after-free in strchr+0xc4/0xd0`). 원인은 확인하지 않았지만, `strscpy()`가 워드 단위로 읽는 구현(`read_word_at_a_time()` 계열, KASAN 검사를 일부러 우회하는 접근)을 쓰기 때문일 가능성이 크다. 교훈은 하나다: **탐지기도 사각지대가 있고, 그 사각지대는 테스트 스위트가 드러낸다.** 새 커널·새 컴파일러에서 스위트를 먼저 돌리는 이유다.
- 시간: TCG에서 75개 전체가 약 2.4초(23.28 → 25.67초). KASAN 리포트 자체는 비싸지 않다. 비싼 것은 리포트가 아니라 **모든 접근에 붙은 검사**다(§10).

### 3.3 예제 5 — slab-out-of-bounds 리포트 한 줄씩 읽기

`kmalloc_oob_right`의 첫 리포트다(타임스탬프 접두어는 지웠다, 주소는 실행마다 다르다). 테스트 코드는 `size = 128 - KASAN_GRANULE_SIZE - 5 = 115`바이트를 `kmalloc`하고 `ptr[size] = 'x'`로 딱 한 바이트 넘겨 쓴다(§0의 "버퍼 끝 넘겨 쓰기" 그대로).

```text
==================================================================
BUG: KASAN: slab-out-of-bounds in kmalloc_oob_right+0x594/0x620 [kasan_test]
Write of size 1 at addr ffff00000e106e73 by task kunit_try_catch/81

CPU: 0 UID: 0 PID: 81 Comm: kunit_try_catch Tainted: G                 N  6.18.55-kasan #2 PREEMPT 
Tainted: [N]=TEST
Hardware name: linux,dummy-virt (DT)
Call trace:
 show_stack+0x30/0x90 (C)
 dump_stack_lvl+0x70/0x98
 print_report+0x168/0x4c0
 kasan_report+0x88/0x180
 __asan_report_store1_noabort+0x20/0x30
 kmalloc_oob_right+0x594/0x620 [kasan_test]
 kunit_try_run_case+0x150/0x3e0
 kunit_generic_run_threadfn_adapter+0x88/0x108
 kthread+0x34c/0x6b0
 ret_from_fork+0x10/0x20

Allocated by task 81:
 kasan_save_stack+0x40/0x68
 kasan_save_track+0x20/0x40
 kasan_save_alloc_info+0x44/0x58
 __kasan_kmalloc+0xbc/0xd8
 __kmalloc_cache_noprof+0x29c/0x7e0
 kmalloc_oob_right+0xb0/0x620 [kasan_test]
 kunit_try_run_case+0x150/0x3e0
 kunit_generic_run_threadfn_adapter+0x88/0x108
 kthread+0x34c/0x6b0
 ret_from_fork+0x10/0x20

The buggy address belongs to the object at ffff00000e106e00
 which belongs to the cache kmalloc-128 of size 128
The buggy address is located 0 bytes to the right of
 allocated 115-byte region [ffff00000e106e00, ffff00000e106e73)

The buggy address belongs to the physical page:
page: refcount:0 mapcount:0 mapping:0000000000000000 index:0xffff00000e106f80 pfn:0x4e106
...  (page flags / raw 줄 8개 생략)
page dumped because: kasan: bad access detected

Memory state around the buggy address:
 ffff00000e106d00: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
 ffff00000e106d80: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
>ffff00000e106e00: 00 00 00 00 00 00 00 00 00 00 00 00 00 00 03 fc
                                                             ^
 ffff00000e106e80: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
 ffff00000e106f00: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
==================================================================
```

위에서부터 읽는 순서:

| 블록 | 읽을 것 | 이 리포트에서 |
|---|---|---|
| 제목 | 버그 종류 + 접근한 함수+오프셋 | `slab-out-of-bounds`, `kmalloc_oob_right+0x594` |
| 접근 줄 | read/write, 크기, 주소, 태스크 | 1바이트 **쓰기**, `…e73` |
| Call trace | 위쪽 5줄은 리포트 장치(`print_report` → `kasan_report` → `__asan_report_store1_noabort`), 그 아래 첫 줄이 **범인** | `kmalloc_oob_right+0x594` |
| Allocated by | 이 객체를 누가 할당했나 | 같은 함수 `+0xb0`의 `kmalloc` |
| 객체 설명 | 어느 캐시, 객체 시작, 몇 바이트 밖 | kmalloc-128 칸, 115바이트 영역의 **0 bytes to the right** |
| Memory state | shadow 덤프, `^`가 버그 주소의 shadow | `00`×14 + `03` + `fc` |

shadow 줄을 그림 2와 맞춰 보자. 115 = 14 × 8 + 3이라 앞 14개 granule은 `00`, 15번째는 "앞 3바이트만 유효" `03`, 그 뒤 kmalloc-128 칸의 나머지와 redzone은 `fc`다. `^`는 `03` 칸을 가리킨다 — 주소 `…e73`은 그 granule의 4번째 바이트(오프셋 3)라 금지 구역이다. 한 줄이 16 shadow 바이트 = 128 메모리 바이트를 덮으므로 **한 줄 = kmalloc-128 객체 하나**인 것도 보인다. 그리고 이 첫 리포트 바로 다음 줄에 `Disabling lock debugging due to kernel taint`가 찍혔다 — KASAN 리포트가 taint를 남기면서 lockdep을 끈다는 §12의 내용이 실제로 일어난 것이다. 위아래 줄이 전부 `fc`인 것은 같은 slab 페이지의 **아직 할당되지 않은 객체들**이다 — SLUB이 새 slab 페이지를 만들 때 KASAN이 페이지 전체를 `fc`로 칠해 두고(`__kasan_poison_slab()`, `mm/kasan/common.c`), 할당되는 객체만 그때 푼다.

### 3.4 예제 6 — 범인 줄 번호로 바꾸기

무엇을 확인하나: 리포트의 `함수+오프셋/길이`를 소스 줄로 바꾸는 법(M3의 `faddr2line`·`decode_stacktrace.sh`와 같다). 모듈은 같은 빌드의 `.ko`를 쓴다.

```sh
/var/lab/src/linux-6.18.55/scripts/faddr2line /var/tmp/M8/ko/kasan_test.ko \
    kmalloc_oob_right+0x594/0x620 kmalloc_uaf+0x318/0x368
```

```text
kmalloc_oob_right+0x594/0x620:
kmalloc_oob_right at mm/kasan/kasan_test_c.c:212 (discriminator 15)

kmalloc_uaf+0x318/0x368:
kmalloc_uaf at mm/kasan/kasan_test_c.c:692 (discriminator 15)
```

출력에서 볼 것: 212행은 `KUNIT_EXPECT_KASAN_FAIL(test, ptr[size] = 'x');` — 정확히 넘겨 쓴 줄이다. 692행은 다음 예제의 해제 후 읽기 줄이다. `discriminator`는 한 줄이 여러 기본 블록으로 나뉠 때(여기서는 매크로 전개) 붙는 번호라 무시해도 된다.

### 3.5 예제 7 — use-after-free: 할당·해제 스택이 둘 다 나온다

`kmalloc_uaf`는 `kmalloc(10)` → `kfree(ptr)` → `((volatile char *)ptr)[8]` 읽기다.

```text
BUG: KASAN: slab-use-after-free in kmalloc_uaf+0x318/0x368 [kasan_test]
Read of size 1 at addr ffff000014b31a18 by task kunit_try_catch/129

CPU: 0 UID: 0 PID: 129 Comm: kunit_try_catch Tainted: G    B            N  6.18.55-kasan #2 PREEMPT 
Tainted: [B]=BAD_PAGE, [N]=TEST
...  (Call trace: 예제 5와 같은 리포트 장치 5줄 + kmalloc_uaf+0x318 + kunit 4줄)

Allocated by task 129:
 kasan_save_stack+0x40/0x68
 kasan_save_track+0x20/0x40
 kasan_save_alloc_info+0x44/0x58
 __kasan_kmalloc+0xbc/0xd8
 __kmalloc_cache_noprof+0x29c/0x7e0
 kmalloc_uaf+0xb8/0x368 [kasan_test]
 ...

Freed by task 129:
 kasan_save_stack+0x40/0x68
 kasan_save_track+0x20/0x40
 __kasan_save_free_info+0x4c/0x80
 __kasan_slab_free+0x78/0xa0
 kfree+0x144/0x700
 kmalloc_uaf+0x124/0x368 [kasan_test]
 ...

The buggy address belongs to the object at ffff000014b31a10
 which belongs to the cache kmalloc-16 of size 16
The buggy address is located 8 bytes inside of
 freed 16-byte region [ffff000014b31a10, ffff000014b31a20)
...
Memory state around the buggy address:
 ffff000014b31900: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fa fb
 ffff000014b31980: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
>ffff000014b31a00: fc fc fa fb fc fc fc fc fc fc fc fc fc fc fc fc
                            ^
 ffff000014b31a80: fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc fc
```

출력에서 볼 것:
- **Freed by task** 블록이 새로 생겼다. 실무에서 UAF를 고칠 때 가장 중요한 정보다 — "누가 먼저 해제했나"가 곧 수명 관리 버그의 다른 한쪽이다. 드라이버라면 보통 `remove()`나 에러 경로의 `kfree`가 여기 찍힌다.
- "located 8 bytes **inside of freed** 16-byte region" — OOB가 아니라 해제된 객체 내부 접근이다.
- shadow `fa fb`: 16바이트 객체의 첫 granule은 `fa`(KASAN_SLAB_FREE_META, 해제 스택 메타데이터가 들어 있는 granule), 둘째는 `fb`(해제됨). 우리가 읽은 오프셋 8은 `fb` 칸이다. 이 객체가 quarantine에 있어서 아직 재사용되지 않았기 때문에 잡을 수 있었다(그림 3).
- `Tainted: [B]=BAD_PAGE` — 앞선 KASAN 리포트가 커널을 taint했다(§12). 이 시점부터 lockdep도 꺼진 상태다.

### 3.6 예제 8 — 스택·전역 변수 OOB: 컴파일러가 redzone을 직접 만든다

slab이 아닌 메모리도 KASAN이 지킨다. 스택(`CONFIG_KASAN_STACK=y`)과 전역 변수는 할당자가 없으므로 **컴파일러가 변수 사이에 redzone을 끼워 넣고** 함수 입구에서 shadow를 칠한다. `kasan_stack_oob`는 `char stack_array[10]`의 `array[10]`(+ tag 오프셋, generic에서는 0)을 읽는다.

```text
BUG: KASAN: stack-out-of-bounds in kasan_stack_oob+0x2d0/0x330 [kasan_test]
Read of size 1 at addr ffff800080c77c6a by task kunit_try_catch/196
...
The buggy address belongs to stack of task kunit_try_catch/196
 and is located at offset 138 in frame:
 kasan_stack_oob+0x0/0x330 [kasan_test]

This frame has 4 objects:
 [48, 49) '__assertion'
 [64, 72) 'array'
 [96, 112) '__assertion'
 [128, 138) 'stack_array'

The buggy address belongs to a 8-page vmalloc region starting at 0xffff800080c70000 allocated at kernel_clone+0x148/0x780
...
Memory state around the buggy address:
 ffff800080c77b80: 00 00 00 00 00 00 00 00 00 00 00 00 f1 f1 f1 f1
>ffff800080c77c00: f1 f1 01 f2 00 f2 f2 f2 00 00 f2 f2 00 02 f3 f3
                                                          ^
 ffff800080c77c80: 00 00 00 00 00 00 00 00 f1 f1 f1 f1 00 00 f2 f2
```

출력에서 볼 것: 리포트가 **스택 프레임의 변수 배치**를 보여 준다. `stack_array`는 프레임 오프셋 [128, 138)이고 버그 주소는 오프셋 138 — 바로 한 칸 밖이다. shadow에서 `f1`(프레임 왼쪽 redzone), `f2`(변수 사이), `f3`(오른쪽 끝)이 변수를 감싸고, `00 02`가 10바이트 배열(8 + 2)이다. "8-page vmalloc region … allocated at kernel_clone"은 arm64의 `VMAP_STACK`: 커널 스택이 vmalloc 영역에 있다는 뜻이다(E4 §7의 KASAN 시 스택 32 KiB = 8페이지와 맞는다).

전역 변수 버전 `kasan_global_oob_right`(`static char global_array[10]`의 범위 밖 읽기)는 이렇게 나온다:

```text
BUG: KASAN: global-out-of-bounds in kasan_global_oob_right+0x2d0/0x340 [kasan_test]
Read of size 1 at addr ffffc425330c486d by task kunit_try_catch/192
...
The buggy address belongs to the variable:
 global_array+0xd/0x40 [kasan_test]
...
>ffffc425330c4800: 00 00 00 00 02 f9 f9 f9 f9 f9 f9 f9 00 02 f9 f9
                                                          ^
```

`global_array+0xd/0x40`: 변수 이름과 그 안 오프셋 13(크기 표기 0x40은 redzone을 포함한 슬롯 크기). shadow `00 02` 다음 `f9`(KASAN_GLOBAL_REDZONE)가 전역 redzone이다. 컴파일러는 전역 변수마다 뒤에 redzone을 붙이고 `__asan_register_globals()`로 커널에 목록을 등록한다.

## 4. KFENCE — 양산 커널에 넣을 수 있는 샘플링 탐지기

### 4.1 직관 — "가끔 한 개만 MPU 가드 페이지 옆에 놓는다"

KASAN은 정확하지만 비싸서 양산에 못 쓴다. 그런데 필드에서 1주일에 한 번 나는 메모리 버그는 개발 PC에서는 재현이 안 된다. KFENCE(Kernel Electric-Fence)의 답은 **샘플링**이다. 100 ms에 한 번(기본값, `CONFIG_KFENCE_SAMPLE_INTERVAL=100`) 다음 slab 할당 **하나**를 특별한 풀에 놓는다. 그 풀은 "객체 페이지 하나 + 접근 금지 guard 페이지 하나"가 번갈아 놓인 구조라, 그 객체를 넘어 쓰면 **MMU가 page fault를 낸다**. 검사 코드가 없으니 나머지 99.99% 할당은 비용이 0이다. Don이 MPU 영역 하나를 "버퍼 바로 뒤 접근 금지"로 설정해 두고 범인을 기다리던 것과 똑같다. 차이는 수천 대의 기기가 동시에 기다린다는 것 — 한 대에서는 확률이 낮아도 **함대 전체로는** 버그가 결국 샘플에 걸린다.

### 4.2 정의

- 풀 크기: `CONFIG_KFENCE_NUM_OBJECTS=255`개 객체. 객체마다 페이지 하나 + guard 페이지 → `(255 + 1) × 2 × 4 KiB = 2 MiB`를 부팅 때 예약한다.
- 객체 배치: 페이지 안에서 **오른쪽 끝** 또는 **왼쪽 끝**에 무작위로 붙인다. 오른쪽 끝이면 오른쪽 OOB가 guard 페이지 fault로 즉시 잡힌다. 왼쪽 끝이면 왼쪽 OOB가 즉시 잡힌다. 반대쪽 남는 공간은 **canary 바이트**로 채워 두고 `kfree()` 때 검사한다("memory corruption" 리포트).
- 해제된 KFENCE 객체의 페이지는 접근 금지로 바뀐다 → UAF가 page fault로 잡힌다.
- 잡는 것: OOB(read/write), UAF, invalid free, double free, 객체 옆 canary 손상. 리포트 제목은 `BUG: KFENCE: out-of-bounds read in …`, `BUG: KFENCE: use-after-free write in …`, `BUG: KFENCE: memory corruption in …`, `BUG: KFENCE: invalid free in …`.
- 제어: 부팅 인자 `kfence.sample_interval=<ms>`(0이면 끔), 런타임 `/sys/module/kfence/parameters/sample_interval`, 상태 `/sys/kernel/debug/kfence/stats`·`objects`.

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <text x="20" y="24" font-size="13">KFENCE 풀 (부팅 때 2 MiB 예약, 4 KiB 페이지 단위)</text>
  <rect x="20" y="40" width="70" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="55" y="70" font-size="12" text-anchor="middle">guard</text>
  <rect x="90" y="40" width="130" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="170" y="40" width="50" height="50" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
  <text x="130" y="70" font-size="12" text-anchor="middle">canary</text>
  <text x="195" y="70" font-size="12" text-anchor="middle">obj</text>
  <rect x="220" y="40" width="70" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="255" y="70" font-size="12" text-anchor="middle">guard</text>
  <rect x="290" y="40" width="130" height="50" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <rect x="290" y="40" width="50" height="50" fill="#3f9a6b" fill-opacity="0.2" stroke="#3f9a6b"/>
  <text x="315" y="70" font-size="12" text-anchor="middle">obj</text>
  <text x="380" y="70" font-size="12" text-anchor="middle">canary</text>
  <rect x="420" y="40" width="70" height="50" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="455" y="70" font-size="12" text-anchor="middle">guard</text>
  <text x="560" y="70" font-size="13" text-anchor="middle">… × 255</text>
  <line x1="215" y1="112" x2="232" y2="94" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah4)"/>
  <text x="120" y="126" font-size="12">오른쪽 OOB → guard 페이지 fault</text>
  <line x1="300" y1="112" x2="284" y2="94" stroke="#d0564a" stroke-width="1.5" marker-end="url(#ah4)"/>
  <text x="310" y="126" font-size="12">왼쪽 OOB → guard 페이지 fault</text>
  <rect x="20" y="150" width="300" height="120" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="170" y="174" font-size="13" text-anchor="middle">샘플링 타이머 (기본 100 ms)</text>
  <text x="170" y="196" font-size="12" text-anchor="middle">타이머가 "다음 할당 하나"를 무장</text>
  <text x="170" y="216" font-size="12" text-anchor="middle">→ kmalloc 한 번이 KFENCE 풀로 감</text>
  <text x="170" y="236" font-size="12" text-anchor="middle">나머지 할당: 일반 SLUB 그대로</text>
  <text x="170" y="256" font-size="12" text-anchor="middle">(fast path 비용 거의 0)</text>
  <rect x="340" y="150" width="300" height="120" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="490" y="174" font-size="13" text-anchor="middle">해제 시</text>
  <text x="490" y="196" font-size="12" text-anchor="middle">canary 검사 → 손상이면 리포트</text>
  <text x="490" y="216" font-size="12" text-anchor="middle">객체 페이지를 접근 금지로</text>
  <text x="490" y="236" font-size="12" text-anchor="middle">→ 이후 접근은 UAF fault</text>
  <text x="490" y="256" font-size="12" text-anchor="middle">잠시 뒤 슬롯 재활용</text>
</svg>
```
그림 4 — KFENCE는 할당 일부만 guard 페이지 사이에 놓는다. 검사는 MMU가 하므로 계측 비용이 없다.

### 4.3 KFENCE 직접 보기

#### 예제 9 — 부팅 직후 KFENCE 상태

무엇을 확인하나: 양산 커널에도 켤 수 있는 KFENCE가 부팅 직후 얼마나 일하고 있는지. `t_misc.sh` 게스트에서 읽었다(첫 줄 `dmesg`는 같은 커널의 비용 측정 부팅 로그).

```sh
dmesg | grep "kfence: initialized"
cat /sys/kernel/debug/kfence/stats
cat /sys/module/kfence/parameters/sample_interval
```

```text
[    0.007408][    T0] kfence: initialized - using 2097152 bytes for 255 objects at 0xffff00003fe00000-0xffff000040000000
enabled: 1
currently allocated: 123
total allocations: 208
total frees: 85
zombie allocations: 0
total bugs: 0
skipped allocations (incompatible): 0
skipped allocations (capacity): 0
skipped allocations (covered): 0
100
```

출력에서 볼 것: 풀은 정확히 2097152바이트(= 2 MiB = (255+1)×2×4 KiB)이고, 물리 메모리 끝 근처(`…3fe00000-…40000000`)에 한 덩어리로 잡혔다. 부팅 약 22초 동안 208개 할당이 샘플링됐고 그중 123개가 아직 살아 있다 — 부팅 중 할당된 오래 사는 객체가 슬롯을 차지하고 있다. `skipped allocations (covered)`는 "같은 할당 지점(스택)이 이미 풀에 많이 있어서 건너뜀" 카운터로, 한 핫 경로가 풀을 독점하지 못하게 하는 장치다.

#### 예제 10 — 업스트림 `kfence_test` KUnit 스위트

무엇을 확인하나: KFENCE가 OOB(좌/우)·UAF·잘못된 해제·canary 손상을 실제로 잡는지. 모듈은 랩 빌드의 `kernel/mm/kfence/kfence_test.ko` 그대로다. 테스트는 KFENCE 객체가 나올 때까지 할당을 반복한다(`test_alloc()`, 최대 `100 × sample_interval` ms). 게스트 스크립트 `t_kfence.sh`의 내용은 다음 네 줄이다: `cat /sys/kernel/debug/kfence/stats` → `insmod /m8/kfence_test.ko` → `echo "M8DONE rc=$?"` → 다시 `cat …/stats`.

```sh
cd /var/tmp/M8 && TMO=900 ./m8_run.sh Image-kasan kfence2 t_kfence.sh log_buf_len=8M
grep -E "# kfence: pass|M8DONE|^total|^currently" kfence2.log
grep -oE "BUG: KFENCE: [a-z -]+ (read|write|free|corruption)" kfence2.log | sort | uniq -c
```

```text
currently allocated: 100
total allocations: 157
total frees: 57
total bugs: 0
[  162.661117][   T80] # kfence: pass:25 fail:0 skip:2 total:27
M8DONE rc=0
currently allocated: 105
total allocations: 1511
total frees: 1406
total bugs: 21
   4 BUG: KFENCE: invalid free
   1 BUG: KFENCE: invalid read
   5 BUG: KFENCE: memory corruption
   5 BUG: KFENCE: out-of-bounds read
   2 BUG: KFENCE: out-of-bounds write
   4 BUG: KFENCE: use-after-free read
```

27개 중 25개 통과, 2개 건너뜀(`CONFIG_INIT_ON_FREE_DEFAULT_ON`이 필요한 테스트), 리포트 21개(`total bugs: 21`과 일치). `test_gfpzero` 하나가 약 128초를 먹었다(33.9 → 162.2초) — 풀 전체를 돌아 같은 슬롯이 재사용될 때까지 기다리는 테스트라 샘플링 간격에 비례해 느리다.

대표 리포트 셋(타임스탬프·레지스터 덤프는 생략). 먼저 **오른쪽 OOB** — 객체가 페이지 오른쪽 끝에 붙어 있어 바로 다음 바이트가 guard 페이지다:

```text
BUG: KFENCE: out-of-bounds read in test_out_of_bounds_read+0x268/0x4d0 [kfence_test]

Out-of-bounds read at 0xffff00003ff7f000 (32B right of kfence-#190):
 test_out_of_bounds_read+0x268/0x4d0 [kfence_test]
 kunit_try_run_case+0x150/0x3e0
 ...
kfence-#190: 0xffff00003ff7efe0-0xffff00003ff7efff, size=32, cache=kmalloc-32

allocated by task 81 on cpu 0 at 19.692296s (0.001262s ago):
 test_alloc+0x27c/0x710 [kfence_test]
 test_out_of_bounds_read+0x234/0x4d0 [kfence_test]
 ...
```

**왼쪽 OOB 쓰기** — 이번 객체는 페이지 왼쪽 끝(`…a000`)에 붙어 있다:

```text
BUG: KFENCE: out-of-bounds write in test_out_of_bounds_write+0x114/0x238 [kfence_test]

Out-of-bounds write at 0xffff00003ff89fff (1B left of kfence-#196):
 test_out_of_bounds_write+0x114/0x238 [kfence_test]
 ...
kfence-#196: 0xffff00003ff8a000-0xffff00003ff8a01f, size=32, cache=kmalloc-32
```

**canary 손상** — 객체 옆 같은 페이지 안을 덮어써서 fault는 안 났고, `kfree()` 때 검사에 걸렸다:

```text
BUG: KFENCE: memory corruption in test_corruption+0x14c/0x338 [kfence_test]

Corrupted memory at 0xffff00003ffa4020 [ 0x2a . . . . . . . . . . . . . . . ] (in kfence-#209):
 test_corruption+0x14c/0x338 [kfence_test]
 ...
kfence-#209: 0xffff00003ffa4000-0xffff00003ffa401f, size=32, cache=kmalloc-32

allocated by task 105 on cpu 0 at 22.620620s (0.001114s ago):
 test_alloc+0x27c/0x710 [kfence_test]
 test_corruption+0xf4/0x338 [kfence_test]
 ...
freed by task 105 on cpu 0 at 22.620805s (0.001214s ago):
 test_corruption+0x14c/0x338 [kfence_test]
 ...
```

출력에서 볼 것:
- 오른쪽 OOB 주소 `…7f000`은 **페이지 경계**다. 객체(`…7efe0-…7efff`, 32 B)가 페이지 끝에 딱 붙어 있어서 바로 뒤가 guard 페이지이고, 그 첫 바이트를 읽는 순간 MMU fault → arm64 fault 경로(`arch/arm64/mm/fault.c`)가 `kfence_handle_page_fault()`를 불러 리포트를 만들었다. "32B right of"는 객체 **시작**에서 32바이트, 즉 끝 바로 다음이라는 뜻이다.
- 왼쪽 OOB는 반대로 객체가 `…8a000`(페이지 시작)에 붙어 있어 1바이트 앞 `…89fff`가 guard 페이지다. 그림 4의 두 배치가 그대로 보인다.
- canary 리포트의 `[ 0x2a . . . ]`는 손상된 바이트만 값으로, 멀쩡한 canary 바이트는 `.`로 보여 준다. 객체 끝(`…401f`) 바로 다음 `…4020`이 0x2a로 덮였다. 여기서는 **해제 스택 = 리포트 스택**이다 — 검사가 `kfree` 안에서 일어났기 때문이다. 즉 범인은 할당과 해제 사이 어딘가이고, KASAN처럼 범인 명령을 짚지는 못한다. KFENCE의 정밀도 한계다.
- KASAN 리포트와 달리 shadow 덤프가 없고 "kfence-#번호"(풀 슬롯)와 시간 정보(`at 19.69s (0.0012s ago)`, 할당 후 얼마 뒤 깨졌나)가 나온다.
- 이 실험은 KASAN도 켜진 커널에서 했다. KASAN 훅은 KFENCE 주소를 건너뛰므로(`mm/kasan/common.c`의 `is_kfence_address()` 검사) 두 탐지기의 리포트가 겹치지 않았다. 양산에서는 KFENCE만 켠다.

---

## 5. kmemleak — 커널용 "참조 없는 객체" 탐지 (GC의 mark 단계만)

### 5.1 직관과 정의

kmemleak은 garbage collector의 **mark 단계만** 하는 도구다. 모든 `kmalloc`/`vmalloc`/`alloc_percpu` 등 할당을 rbtree에 기록해 두고, 스캔할 때 **루트**(데이터·bss 섹션, 각 태스크 커널 스택, per-CPU 영역)와 이미 도달한 객체의 메모리를 8바이트씩 훑으며 "이 값이 어떤 할당 객체의 주소 범위 안을 가리키는가"를 본다. 끝까지 아무도 가리키지 않는 객체(5초 이상 된 것, `MSECS_MIN_AGE 5000`)를 **unreferenced object**로 보고한다. 즉 "포인터를 잃어버린 메모리"를 찾는다.

- 자동 스캔: `CONFIG_DEBUG_KMEMLEAK_AUTO_SCAN=y`면 부팅 60초 뒤(`SECS_FIRST_SCAN 60`) 첫 스캔, 이후 10분마다(`SECS_SCAN_WAIT 600`).
- 수동: `echo scan > /sys/kernel/debug/kmemleak` 후 `cat /sys/kernel/debug/kmemleak`. `echo clear > …`는 지금까지의 의심 목록을 "보고 완료"로 지운다(새 누수만 보려고 쓴다).
- 한계: 보수적이라 **오탐·미탐**이 있다. 포인터를 XOR하거나 하위 비트를 태그로 쓰거나, 물리 주소로만 들고 있으면(DMA 디스크립터) 누수로 오탐한다 → `kmemleak_not_leak()`, `kmemleak_ignore()`로 알려 준다. 반대로 우연히 그 주소 값을 가진 정수가 있으면 미탐한다. 그래서 "의심 목록"으로 읽는다.

### 5.2 예제 11 — 업스트림 `samples/kmemleak/kmemleak-test.c`

이 샘플 모듈(**업스트림 테스트 모듈**, `samples/kmemleak/kmemleak-test.c`)은 랩 커널 빌드에 포함되지 않아서, 소스 파일을 **수정 없이** `/var/tmp/M8/kmemleak/`에 복사하고 한 줄짜리 Kbuild로 `linux-build-kasan`에 대해 out-of-tree 빌드했다. 모듈이 하는 일은 `module_init`에서 `kmalloc(32)`×2, `kmalloc(1024/2048/4096)`×2씩, `vmalloc(64)`×5를 받아 **포인터를 버리고**(주소만 출력), `kzalloc`한 노드 10개를 모듈 전역 리스트에 걸고, per-CPU 변수에 `kmalloc(129)`를 넣고, `__alloc_percpu(64, 4)`를 버리는 것이다. `module_exit`은 리스트에서 노드를 떼기만 하고 해제하지 않는다.

```make
obj-m += kmemleak-test.o
```

```sh
make -C /var/lab/out/linux-build-kasan M=/var/tmp/M8/kmemleak modules
# 게스트 (t_misc.sh 중 kmemleak 부분)
insmod /m8/kmemleak-test.ko; rmmod kmemleak_test; sleep 8
echo scan > /sys/kernel/debug/kmemleak; sleep 2; echo scan > /sys/kernel/debug/kmemleak
cat /sys/kernel/debug/kmemleak > /tmp/k.txt; grep -c "unreferenced object" /tmp/k.txt; head -80 /tmp/k.txt
dmesg | grep -E "Kmemleak testing|kmalloc\(32\)|vmalloc\(64\)|kmemleak:" | head -12
```

```text
26
unreferenced object 0xffff000014faeaa0 (size 32):
  comm "insmod", pid 96, jiffies 4294898390
  hex dump (first 32 bytes):
    6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b  kkkkkkkkkkkkkkkk
    6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b 6b a5  kkkkkkkkkkkkkkk.
  backtrace (crc 362c6625):
    kmemleak_alloc+0xb8/0xd0
    __kmalloc_cache_noprof+0x574/0x7e0
    0xffffd78d4b5d52a4
    do_one_initcall+0x12c/0x780
    do_init_module+0x228/0x740
    load_module+0x4784/0x6278
    __do_sys_init_module+0x254/0x348
    __arm64_sys_init_module+0x70/0xc0
    invoke_syscall.constprop.0+0x64/0x290
    el0_svc_common.constprop.0+0xa8/0x250
    do_el0_svc+0x44/0x68
    el0_svc+0x50/0x218
    el0t_64_sync_handler+0xa0/0xe8
    el0t_64_sync+0x198/0x1a0
unreferenced object 0xffff000014faeb60 (size 32):
...  (같은 형식이 25개 더, 생략)
[   20.056331][    T1] kmemleak: Kernel memory leak detector initialized (mem pool available: 15795)
[   20.056837][   T65] kmemleak: Automatic memory scanning thread started
[   24.857444][   T96] kmemleak: Kmemleak testing
[   24.857661][   T96] kmemleak: kmalloc(32) = 0xffff000014faeaa0
[   24.857910][   T96] kmemleak: kmalloc(32) = 0xffff000014faeb60
[   24.858145][   T96] kmemleak: kmalloc(1024) = 0xffff00001211f000
[   24.858463][   T96] kmemleak: kmalloc(1024) = 0xffff00000e3b0400
...
```

출력에서 볼 것:
- 누수 26개. 소스 기준으로 세면 버린 `kmalloc` 8 + `vmalloc` 5 + 리스트 노드 10(모듈이 내려가 리스트 헤드가 사라짐) + per-CPU 포인터가 들고 있던 `kmalloc(129)` 2(CPU 2개, 모듈의 per-CPU 영역이 해제됨) + `__alloc_percpu` 1 = 26으로 맞는다(종류별 분해는 출력 전체를 세지 않고 소스로 계산한 값이다).
- 첫 객체 주소 `…faeaa0`이 모듈이 찍은 첫 `kmalloc(32)` 주소와 같다. "unreferenced"의 근거는 **스캔해 보니 이 주소를 담은 8바이트가 루트에서 도달 가능한 메모리 어디에도 없었다**는 것이다.
- hex dump가 `6b 6b … a5`다. 이 커널은 `SLUB_DEBUG_ON`이라 slab 객체를 `POISON_FREE`(0x6b, 'k')로 채우고 마지막 바이트를 `POISON_END`(0xa5)로 둔다(F9 §9). `kmalloc`은 메모리를 0으로 지우지 않으므로 받은 그대로 아무도 쓰지 않았다는 것까지 보인다.
- backtrace 셋째 줄이 심볼 없이 `0xffffd78d4b5d52a4`다. 할당한 함수(`kmemleak_test_init`)가 **이미 언로드된 모듈** 안에 있어서 kallsyms가 이름을 못 붙인다. 실무 팁: 모듈 누수를 볼 때는 `rmmod` 전에 스캔하거나, 모듈을 다시 `insmod`한 뒤 `/proc/modules`의 로드 주소로 오프셋을 계산한다(같은 주소에 다시 올라간다는 보장은 없다).
- 리스트 노드 10개는 `rmmod` **전에는** 누수가 아니다 — 모듈 전역 `test_list`(모듈 .data는 스캔 루트)가 가리키고 있으니까. 소스 주석도 "They should only appear as orphan after the module is removed"라고 적는다(이 노트에서는 rmmod 전 스캔은 따로 하지 않았다). 이게 kmemleak의 "도달 가능성" 정의다.

---

## 6. UBSAN — C의 미정의 동작을 런타임에 잡기

### 6.1 직관과 정의

C 표준은 signed overflow, 배열 범위 밖 인덱스, 폭 이상 shift, 0 나눗셈, 잘못된 `bool`/`enum` 값 읽기, 정렬 위반 등을 **미정의 동작(UB)** 으로 둔다. 컴파일러는 "UB는 일어나지 않는다"고 가정하고 최적화하므로, 버그가 있으면 엉뚱한 코드가 생성된다(예: 범위 검사가 통째로 지워짐). UBSAN(`-fsanitize=bounds,shift,…`)은 해당 연산 앞에 검사를 넣고 위반이면 `__ubsan_handle_*()`를 불러 리포트를 찍는다. KASAN과 달리 **메모리 상태를 추적하지 않는다** — 그래서 싸다. 특히 `UBSAN_BOUNDS`는 배열 크기를 컴파일러가 아는 경우(구조체 안 고정 배열, `__counted_by` 붙은 flexible array)에만 동작하는 매우 싼 검사라, 일부 배포판·Android 커널은 양산에도 켠다(`UBSAN_TRAP`으로 리포트 대신 trap 명령을 넣으면 코드도 작아진다).

| Kconfig | 잡는 UB | 랩 Image-kasan |
|---|---|---|
| `UBSAN_BOUNDS` (+`_STRICT`/`_ARRAY_BOUNDS`) | 크기를 아는 배열의 범위 밖 인덱스 | 켬 (`BOUNDS_STRICT`) |
| `UBSAN_SHIFT` | 음수 shift, 폭 이상 shift, signed left shift overflow | 켬 |
| `UBSAN_DIV_ZERO` | 0으로 나누기 (arm64는 HW가 trap하지 않고 0을 돌려준다) | 끔 |
| `UBSAN_BOOL`, `UBSAN_ENUM` | 잘못된 bool/enum 값 load | 켬 (enum은 리포트 안 나옴, 예제 12) |
| `UBSAN_ALIGNMENT` | 정렬 위반 (arm64는 HW가 허용해서 기본 꺼짐) | 끔 |
| `UBSAN_INTEGER_WRAP` | signed/unsigned 산술 wrap (6.15+, clang 전용) | gcc 빌드라 없음 |
| `UBSAN_TRAP` | 리포트 대신 `brk` trap (양산용, 작음) | 끔 |

### 6.2 예제 12 — 업스트림 `lib/test_ubsan.c`

무엇을 확인하나: 이 커널에서 켜진 UBSAN 검사(shift, bounds, bool, enum)가 실제로 무엇을 잡고 무엇을 지나치는지. 업스트림 테스트 모듈 `lib/test_ubsan.c`(랩 빌드의 `kernel/lib/test_ubsan.ko`)는 `module_init`에서 UB 함수 9개를 차례로 부르고, 각 함수 앞에 "해당 Kconfig가 켜졌는지"를 한 줄 찍는다(`UBSAN_TEST()` 매크로). 0 나누기 테스트는 모듈을 Oops시키므로 업스트림이 일부러 빼 두었다(`skip_ubsan_array`).

```sh
insmod /m8/test_ubsan.ko
dmesg | grep -A12 -E "test_ubsan|UBSAN" | head -150
```

```text
test_ubsan_add_overflow (CONFIG_UBSAN_INTEGER_WRAP=n)
test_ubsan_sub_overflow (CONFIG_UBSAN_INTEGER_WRAP=n)
test_ubsan_mul_overflow (CONFIG_UBSAN_INTEGER_WRAP=n)
test_ubsan_negate_overflow (CONFIG_UBSAN_INTEGER_WRAP=n)
test_ubsan_truncate_signed (CONFIG_UBSAN_INTEGER_WRAP=n)
test_ubsan_shift_out_of_bounds negative exponent (CONFIG_UBSAN_SHIFT=y)
------------[ cut here ]------------
UBSAN: shift-out-of-bounds in /var/lab/src/linux-6.18.55/lib/test_ubsan.c:72:7
shift exponent -1 is negative
CPU: 0 UID: 0 PID: 92 Comm: insmod Not tainted 6.18.55-kasan #2 PREEMPT 
Hardware name: linux,dummy-virt (DT)
Call trace:
 show_stack+0x30/0x90 (C)
 dump_stack_lvl+0x70/0x98
 dump_stack+0x1c/0x28
 ubsan_epilogue+0x10/0x48
 __ubsan_handle_shift_out_of_bounds+0x16c/0x1f0
 test_ubsan_shift_out_of_bounds+0xd0/0x148 [test_ubsan]
 test_ubsan_init+0x60/0xfd0 [test_ubsan]
 do_one_initcall+0x12c/0x780
 ...  (모듈 로드 syscall 경로 10줄 생략)
---[ end trace ]---
test_ubsan_shift_out_of_bounds left overflow (CONFIG_UBSAN_SHIFT=y)
test_ubsan_out_of_bounds above (CONFIG_UBSAN_BOUNDS=y)
------------[ cut here ]------------
UBSAN: array-index-out-of-bounds in /var/lab/src/linux-6.18.55/lib/test_ubsan.c:92:10
index 4 is out of range for type 'int [4]'
...
test_ubsan_out_of_bounds below (CONFIG_UBSAN_BOUNDS=y)
------------[ cut here ]------------
UBSAN: array-index-out-of-bounds in /var/lab/src/linux-6.18.55/lib/test_ubsan.c:95:10
index -1 is out of range for type 'int [4]'
...
test_ubsan_load_invalid_value bool (CONFIG_UBSAN_BOOL=y)
------------[ cut here ]------------
UBSAN: invalid-load in /var/lab/src/linux-6.18.55/lib/test_ubsan.c:117:7
load of value 255 is not a valid value for type '_Bool'
...
test_ubsan_load_invalid_value enum (CONFIG_UBSAN_ENUM=y)
test_ubsan_misaligned_access (CONFIG_UBSAN_ALIGNMENT=n)
```

출력에서 볼 것:
- 리포트 4개: 음수 shift, 배열 `int [4]`의 인덱스 4와 -1, `_Bool`에 255 load. 각 리포트 첫 줄이 **파일:줄:열**까지 준다 — KASAN과 달리 `faddr2line`이 필요 없다(컴파일러가 위치 정보를 데이터로 넣어 둔다). 리포트 형식은 `WARN()`과 같은 `cut here` … `end trace` 블록이다.
- `INTEGER_WRAP=n` 다섯 줄은 검사 자체가 없어서 조용히 지나간다(6.15+의 `UBSAN_INTEGER_WRAP`은 clang 전용, 이 커널은 gcc 빌드).
- **`left overflow`(`INT_MAX << 4`)는 `UBSAN_SHIFT=y`인데도 리포트가 없다.** 커널은 최상위 Makefile에서 `-fno-strict-overflow`로 빌드되고, gcc에서 이 옵션은 signed overflow를 wrap으로 정의하는 `-fwrapv`를 포함한다. 그래서 커널 C에서 signed left shift overflow는 UB가 아니게 되어 검사가 빠진 것으로 보인다. 음수 shift 같은 진짜 UB만 남는다.
- **`enum`도 `UBSAN_ENUM=y`인데 리포트가 없다.** 이 gcc 15 빌드에서는 C 코드의 enum load 검사가 나오지 않았다(원인은 확인하지 않았다 — 컴파일러마다 C enum 검사 지원이 다르다). "켰다 = 잡는다"가 아니라는 좋은 예다. 테스트 모듈이 `(CONFIG_X=y)`를 찍어 주는 이유가 이것이다: 켜졌는데 리포트가 없으면 컴파일러 쪽 한계다.
- `Not tainted`: UBSAN 리포트는 taint를 남기지 않는다(KASAN은 `B`). 그래서 lockdep도 꺼지지 않는다. 대신 `panic_on_warn=1`이면 첫 리포트에서 panic한다.

---

## 7. KCSAN — 데이터 레이스 탐지기 (가이드)

### 7.1 직관 — "watchpoint를 걸고 잠깐 기다린다"

F4에서 본 `int++` 레이스는 lockdep이 못 잡는다(락을 안 썼으니까). KCSAN(Kernel Concurrency SANitizer)은 컴파일러 계측(`-fsanitize=thread`)으로 모든 메모리 접근 앞에 훅을 넣고, **가끔**(기본 약 4000번 접근마다 한 번, `CONFIG_KCSAN_SKIP_WATCH=4000`) 그 접근 주소에 소프트웨어 **watchpoint**를 건 뒤 잠깐(태스크 문맥 기본 80 us, `KCSAN_UDELAY_TASK`) 지연한다. 그 사이 다른 CPU가 같은 주소를 만지면(둘 중 하나가 write이고, 둘 다 marked atomic 접근이 아니면) **data race**로 보고한다. 지연 전후 값이 바뀌었는지도 본다(watchpoint에 안 걸린 쓰기, "value changed"). Don의 경험으로는 JTAG data watchpoint를 랜덤한 주소에 계속 걸었다 풀었다 하는 것이다.

### 7.2 리포트 모양 (가이드 — 이 랩에서 실행 안 함)

KCSAN은 KASAN과 **같이 켤 수 없다**(`lib/Kconfig.kcsan`: `depends on … !KASAN`). 랩 `Image-kasan`에는 없고, 쓰려면 trace 설정에서 KASAN 계열을 빼고 `CONFIG_KCSAN=y`, `CONFIG_KCSAN_KUNIT_TEST=m`으로 별도 커널을 빌드해야 한다(`-j2`에서 TCG VM 기준 1시간 이상이라 이 노트에서는 하지 않았다). 업스트림 `kernel/kcsan/kcsan_test.c` KUnit 스위트(`test_basic`, `test_concurrent_races`, `test_novalue_change`, `test_write_write_assume_atomic` 등)가 KCSAN 자체를 검증한다. 리포트 형식은 `Documentation/dev-tools/kcsan.rst` 예시와 같다:

```text
==================================================================
BUG: KCSAN: data-race in test_kernel_read / test_kernel_write

write to 0xffffffffc009a628 of 8 bytes by task 487 on cpu 0:
 test_kernel_write+0x1d/0x30
 access_thread+0x89/0xd0
 kthread+0x23e/0x260
 ret_from_fork+0x22/0x30

read to 0xffffffffc009a628 of 8 bytes by task 488 on cpu 6:
 test_kernel_read+0x10/0x20
 access_thread+0x89/0xd0
 kthread+0x23e/0x260
 ret_from_fork+0x22/0x30

value changed: 0x00000000000009a6 -> 0x00000000000009b2

Reported by Kernel Concurrency Sanitizer on:
CPU: 6 PID: 488 Comm: access_thread Not tainted 5.12.0-rc2+ #1
Hardware name: QEMU Standard PC (i440FX + PIIX, 1996), BIOS 1.14.0-2 04/01/2014
==================================================================
```

(위 블록은 커널 문서의 예시를 옮긴 것이다 — 이 랩에서 실행한 출력이 아니다. x86 QEMU 주소·5.12 버전이 그 표시다.)

읽는 법: 첫 줄 `data-race in A / B`가 경쟁하는 두 함수, 아래 두 블록이 각 접근(누가, 어느 CPU, 몇 바이트, read/write, 스택), `value changed`가 실제로 값이 바뀌었다는 증거다. 고치는 법은 F4·F5대로 (1) 진짜 공유면 락, (2) 의도된 lockless 접근이면 `READ_ONCE()`/`WRITE_ONCE()`/`atomic_*`로 **marked access** 표시, (3) 통계 카운터처럼 레이스가 무해하면 `data_race(x)`로 의도를 밝힌다. 커널 메인라인은 syzbot이 KCSAN 커널을 계속 돌려 레이스를 보고한다.

---

## 8. 그 밖의 `CONFIG_DEBUG_*` — 자료구조·API 사용 규칙 검사기

컴파일러 계측 없이, 커널 자료구조 코드 안에 검사를 넣은 옵션들이다. 각각 "한 가지 규칙"을 지킨다.

| 옵션 | 지키는 규칙 | 위반 시 메시지 (첫 줄) | 비용 |
|---|---|---|---|
| `DEBUG_ATOMIC_SLEEP` | atomic 문맥에서 잠들지 말 것 (E4 §5, F2 §10) | `BUG: sleeping function called from invalid context` | 작음 |
| `PROVE_LOCKING` (lockdep) | 락 순서·IRQ 상태 일관성 (F4 §13) | `possible circular locking dependency detected` | 큼 (메모리·락 경로) |
| `DEBUG_OBJECTS` (+`_TIMERS`, `_WORK`, `_RCU_HEAD`, `_FREE`) | timer·work·rcu_head 같은 객체의 생애주기: init 전 사용, 활성 상태로 해제, 이중 init | `ODEBUG: free active (active state 0) object: … object type: timer_list hint: …` | 작음~중간 |
| `DEBUG_LIST` | `list_add`/`list_del` 때 이웃 포인터 일관성 (손상된 리스트, 이중 삭제) | `list_add corruption. next->prev should be prev …` 후 BUG | 작음 (양산에도 쓰는 곳 있음, `LIST_HARDENED`) |
| `DEBUG_SG` | scatterlist 마커·체인 검사 | `BUG` at `include/linux/scatterlist.h` | 작음 |
| `DEBUG_PAGEALLOC` | 해제된 페이지를 커널 매핑에서 빼서 접근 시 fault (페이지 단위 UAF) | `Unable to handle kernel paging request` | 크다 (매핑 변경·TLB) — `debug_pagealloc=on` 부팅 인자로 활성 |
| `SLUB_DEBUG_ON` | `slab_debug` 기본 켬 (F9 §9.1: redzone·poison·사용자 추적, 사후 검사) | `BUG kmalloc-64 (…): Right Redzone overwritten` | 중간 |
| `DMA_API_DEBUG` | `dma_map_*`/`dma_unmap_*` 짝, 방향, 크기, unmap 누락, 매핑한 메모리를 CPU가 건드림 | `DMA-API: device driver frees DMA memory with different size …` | 중간 |
| `DEBUG_VM` | mm 내부 불변식 (`VM_BUG_ON`) | `kernel BUG at mm/…` | 작음~중간 |

DMA_API_DEBUG는 Don에게 특히 중요하다. SSD 펌웨어의 DMA는 캐시를 직접 관리했지만, Linux 드라이버에서는 `dma_map_single()`이 캐시 유지·IOMMU 매핑을 대신 하고, 그 **짝 맞추기 규칙**(map한 크기·방향 그대로 unmap, 매핑 중 CPU 접근 금지, `dma_sync_*` 없이 읽지 말 것)을 어기면 실기에서만, 그것도 가끔 데이터가 틀린다(H 모듈 DMA 노트). DMA_API_DEBUG는 모든 매핑을 해시 테이블에 기록해 두고 unmap 때 맞춰 본다. `/sys/kernel/debug/dma-api/`에서 `error_count`, `num_errors`, `driver_filter`(특정 드라이버만 검사)를 본다.

### 8.1 예제 13 — 디버그 옵션들의 런타임 상태 보기

무엇을 확인하나: DEBUG_OBJECTS와 DMA_API_DEBUG가 런타임에 무엇을 들고 있고 CI가 무엇을 읽으면 되는지(같은 `t_misc.sh` 게스트, 위반 없이 부팅만 한 상태). 이 둘을 일부러 위반하는 업스트림 테스트는 별도 모듈이 없어서(`DEBUG_OBJECTS_SELFTEST`는 부팅 때 도는 built-in 자체 시험) 이 노트에서는 **상태 읽기만** 했다.

```sh
cat /sys/kernel/debug/debug_objects/stats
ls /sys/kernel/debug/dma-api/
for f in error_count num_errors num_free_entries min_free_entries nr_total_entries driver_filter; do
    echo "$f: $(cat /sys/kernel/debug/dma-api/$f)"; done
dmesg | grep "DMA-API"     # 이 줄만 같은 커널의 비용 측정 부팅 로그에서
```

```text
max_chain     : 6
max_checked   : 0
warnings      : 0
fixups        : 0
pool_free     : 315
pool_pcp_free : 27
pool_min_free : 272
pool_used     : 789
pool_max_used : 816
on_free_list  : 0
objs_allocated: 1104
objs_freed    : 0
all_errors        dump              nr_total_entries
disabled          error_count       num_errors
driver_filter     min_free_entries  num_free_entries
error_count: 0
num_errors: 1
num_free_entries: 65536
min_free_entries: 65536
nr_total_entries: 65536
driver_filter: 
[    1.308324][    T1] DMA-API: preallocated 65536 debug entries
```

출력에서 볼 것:
- DEBUG_OBJECTS: 지금 789개 객체를 추적 중이고(이 커널에서 켠 종류는 `DEBUG_OBJECTS_TIMERS`뿐이라 timer·hrtimer다) `warnings : 0`이다. CI는 이 숫자가 0인지 보면 된다. 위반이 생기면 dmesg에 `ODEBUG: <동작> <상태> (active state 0) object: … object type: timer_list hint: <콜백 함수>` 형식이 찍히고 `warnings`가 오른다(형식은 `lib/debugobjects.c`의 `debug_print_object()` 기준, 이 노트에서 실행으로 확인하지는 않았다). `hint:`가 timer 콜백 함수 이름이라 어느 드라이버의 timer인지 바로 안다.
- DMA_API_DEBUG: 매핑 기록용 엔트리 65536개를 미리 잡아 두었고 아직 하나도 안 썼다(`num_free_entries = min_free_entries = 65536`) — virtio 장치는 DMA API를 쓰지만 이 게스트에서는 블록 장치 없이 initramfs로 부팅해 매핑이 거의 없었다. `error_count: 0`이 위반 개수다. 헷갈리기 쉬운 것은 `num_errors: 1`인데, 이건 위반 개수가 아니라 **"앞으로 dmesg에 자세히 찍을 남은 횟수"**다(`show_num_errors`, 기본 1, 하나 찍을 때마다 1씩 준다, `kernel/dma/debug.c`). `all_errors`에 1을 쓰면 전부 찍는다. (`ls` 출력의 색 코드는 지웠다.) `driver_filter`에 드라이버 이름을 쓰면 그 드라이버의 매핑만 검사한다 — 큰 시스템에서 내 드라이버만 보고 싶을 때 쓴다.
- 부팅 인자로 끄는 법: `debug_objects`는 `no_debug_objects`, DMA API 검사는 `dma_debug=off`, KFENCE는 `kfence.sample_interval=0`, kmemleak은 `kmemleak=off`, slab_debug는 `slab_debug=-`. 같은 디버그 커널로 "어느 탐지기가 비용을 먹나"를 나눠 볼 때 쓴다(§10).

## 9. 무엇이 무엇을 잡나 — 탐지기 커버리지 지도

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">● 잡는다   ◐ 일부·조건부   (빈칸) 못 잡는다</text>
  <g font-size="12" text-anchor="middle">
    <text x="205" y="50">범위</text><text x="205" y="66">초과</text>
    <text x="265" y="58">UAF</text>
    <text x="325" y="50">이중</text><text x="325" y="66">해제</text>
    <text x="385" y="58">누수</text>
    <text x="445" y="50">UB 연산</text><text x="445" y="66">(shift 등)</text>
    <text x="505" y="50">데이터</text><text x="505" y="66">레이스</text>
    <text x="565" y="50">데드락</text><text x="565" y="66">순서</text>
    <text x="630" y="50">문맥·</text><text x="630" y="66">API 규칙</text>
  </g>
  <line x1="20" y1="76" x2="665" y2="76" stroke="currentColor"/>
  <g font-size="12">
    <text x="20" y="98">KASAN generic</text>
    <text x="20" y="128">KASAN HW_TAGS (MTE)</text>
    <text x="20" y="158">KFENCE</text>
    <text x="20" y="188">slab_debug (F9)</text>
    <text x="20" y="218">DEBUG_PAGEALLOC</text>
    <text x="20" y="248">kmemleak</text>
    <text x="20" y="278">UBSAN</text>
    <text x="20" y="308">KCSAN</text>
    <text x="20" y="338">lockdep (F4)</text>
    <text x="20" y="368">DEBUG_ATOMIC_SLEEP</text>
    <text x="20" y="398">DEBUG_OBJECTS · LIST</text>
    <text x="20" y="428">DMA_API_DEBUG</text>
  </g>
  <g fill="#4a7bd0">
    <circle cx="205" cy="94" r="6"/><circle cx="265" cy="94" r="6"/><circle cx="325" cy="94" r="6"/>
    <circle cx="205" cy="124" r="6"/><circle cx="265" cy="124" r="6"/><circle cx="325" cy="124" r="6"/>
    <circle cx="325" cy="154" r="6"/>
    <circle cx="325" cy="184" r="6"/>
    <circle cx="265" cy="214" r="6"/>
    <circle cx="385" cy="244" r="6"/>
    <circle cx="445" cy="274" r="6"/>
    <circle cx="505" cy="304" r="6"/>
    <circle cx="565" cy="334" r="6"/><circle cx="630" cy="334" r="6"/>
    <circle cx="630" cy="364" r="6"/>
    <circle cx="630" cy="394" r="6"/>
    <circle cx="630" cy="424" r="6"/>
  </g>
  <g fill="#e08a3c" fill-opacity="0.35" stroke="#e08a3c">
    <circle cx="205" cy="154" r="6"/><circle cx="265" cy="154" r="6"/>
    <circle cx="205" cy="184" r="6"/><circle cx="265" cy="184" r="6"/>
    <circle cx="205" cy="274" r="6"/>
    <circle cx="265" cy="394" r="6"/><circle cx="325" cy="394" r="6"/>
    <circle cx="325" cy="424" r="6"/><circle cx="385" cy="424" r="6"/>
  </g>
</svg>
```
그림 5 — 탐지기 커버리지. KFENCE의 범위 초과·UAF는 "샘플에 걸린 객체만"(◐), slab_debug는 "다음 검사 시점에, 쓰기만", UBSAN은 "크기를 아는 배열만", DEBUG_OBJECTS는 "timer·work가 활성인 채 해제" 같은 수명 위반, DMA_API_DEBUG는 "map/unmap 짝 불일치·unmap 누락"을 잡는다. 한 도구로 다 되지 않으니 **조합**한다.

읽는 법: 메모리 손상(왼쪽 세 열)은 KASAN이 가장 정확하고, 양산에서는 KFENCE·MTE가 그 자리를 대신한다. 레이스는 KCSAN뿐이고, 락 순서는 lockdep뿐이다. 누수는 kmemleak뿐이다.

---

## 10. 비용 — 직접 잰 숫자

### 10.1 예제 14 — 이미지 크기

무엇을 확인하나: 탐지기를 켜면 커널 이미지와 섹션이 얼마나 커지나.

```sh
cd /var/lab/out; ls -l Image-trace Image-kasan | awk '{print $5, $9}'; size vmlinux-trace vmlinux-kasan
```

```text
154823168 Image-kasan
65681920 Image-trace
   text	   data	    bss	    dec	    hex	filename
35586492	29895229	 934832	66416553	3f56fa9	vmlinux-trace
73440713	81148037	19843776	174432526	a65a10e	vmlinux-kasan
```

출력에서 볼 것: `Image`(부트로더가 올리는 압축 안 된 커널)가 65.7 MB → 154.8 MB, **2.4배**. text는 35.6 → 73.4 MB(2.1배) — 모든 load/store 앞의 inline 검사(예제 3의 9개 명령) 때문이다. data는 2.7배, bss는 21배(0.9 → 19.8 MB) — lockdep 해시 테이블·kmemleak 풀·KASAN 전역 redzone 등 정적 테이블이 bss에 있다. 디버그 정보는 `Image`에 들어가지 않으므로(그건 `vmlinux` 쪽) 이 차이는 순수하게 코드·데이터다. 임베디드 관점: 부트 파티션이 64 MB인 보드라면 이 커널은 **들어가지도 않는다**. 디버그 커널은 TFTP/NFS 부팅(C 모듈)이나 큰 파티션을 따로 준비해야 한다.

### 10.2 예제 15 — 부팅 시간·메모리·syscall 비용

무엇을 확인하나: 같은 initramfs·같은 QEMU 설정(`-smp 2 -m 1G`)에서 기준 커널과 디버그 커널의 부팅 → `/init` 시간, 부팅 직후 메모리, 커널 경로 몇 개의 평균 비용. 세 번째 열은 같은 `Image-kasan`에 부팅 인자 `slab_debug=- kmemleak=off kfence.sample_interval=0`을 줘서 slab_debug·kmemleak·KFENCE를 끈 것이다(KASAN·lockdep·UBSAN 등 컴파일 시점 탐지기는 남는다). 각 2회씩 번갈아 돌린 평균이다.

벤치마크는 버그를 내지 않는 평범한 syscall 루프다(정적 링크, VM 호스트에서 빌드).

```c
/* m8_bench: 커널 경로 몇 개의 평균 비용(ns/op)을 잰다 — 디버그 커널 비용 비교용 */
#define _GNU_SOURCE
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static double now_ns(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec * 1e9 + ts.tv_nsec;
}

#define BENCH(name, n, body) do {                                   \
	double t0 = now_ns();                                        \
	for (long i = 0; i < (n); i++) { body; }                     \
	printf("%-22s %10.0f ns/op\n", name, (now_ns() - t0) / (n)); \
} while (0)

int main(void)
{
	int p[2];

	BENCH("getppid", 20000, getppid());
	BENCH("open+close /dev/null", 2000, {
		int fd = open("/dev/null", O_RDONLY);
		if (fd < 0) { perror("open"); exit(1); }
		close(fd);
	});
	BENCH("pipe+close x2", 2000, {
		if (pipe(p) < 0) { perror("pipe"); exit(1); }
		close(p[0]); close(p[1]);
	});
	BENCH("mmap+touch+munmap 64K", 500, {
		char *m = mmap(NULL, 65536, PROT_READ | PROT_WRITE,
			       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (m == MAP_FAILED) { perror("mmap"); exit(1); }
		for (int k = 0; k < 65536; k += 4096)
			m[k] = 1;
		munmap(m, 65536);
	});
	BENCH("fork+wait", 50, {
		pid_t c = fork();
		if (c < 0) { perror("fork"); exit(1); }
		if (c == 0) _exit(0);
		waitpid(c, NULL, 0);
	});
	return 0;
}
```

게스트 스크립트 `t_cost.sh`와 실행:

```sh
cat > /var/tmp/M8/t_cost.sh <<'XEOF'
echo "M8UPTIME $(cut -d' ' -f1 /proc/uptime)"
uname -r
dmesg | grep -E "Memory:|kfence|KFENCE|kasan|KASAN|kmemleak|lockdep|Lock dependency" | head -12
free -k
grep -E "MemTotal|MemFree|MemAvailable|^Slab|SUnreclaim|KernelStack|Percpu|VmallocUsed" /proc/meminfo
/m8/m8_bench
XEOF
gcc -std=gnu11 -Wall -Wextra -O2 -static m8_bench.c -o /var/tmp/M8/m8_bench
cd /var/tmp/M8
for i in 1 2; do for k in trace kasan; do ./m8_run.sh Image-$k cost_${k}_$i t_cost.sh; done; done
for i in 1 2; do ./m8_run.sh Image-kasan cost_kasanlite_$i t_cost.sh slab_debug=- kmemleak=off kfence.sample_interval=0; done
```

`cost_trace_1.log`의 핵심 줄(나머지 다섯 로그도 같은 형식):

```text
[    0.115093][    T1] Memory: 920444K/1048576K available (23680K kernel code, 7246K rwdata, 28828K rodata, 4224K init, 912K bss, 89408K reserved, 32768K cma-reserved)
lab initramfs: 6.18.55-trace up 2.44s
MemTotal:         965288 kB
MemFree:          896528 kB
Slab:              11716 kB
Percpu:              304 kB
getppid                     24653 ns/op
open+close /dev/null       113815 ns/op
pipe+close x2              196724 ns/op
mmap+touch+munmap 64K     1128060 ns/op
fork+wait                 2439944 ns/op
```

`cost_kasan_1.log`:

```text
[    0.621273][    T1] Memory: 633348K/1048576K available (67520K kernel code, 39962K rwdata, 35724K rodata, 7808K init, 19378K bss, 370004K reserved, 32768K cma-reserved)
lab initramfs: 6.18.55-kasan up 17.33s
MemTotal:         688276 kB
MemFree:          503532 kB
Slab:              67836 kB
Percpu:             3936 kB
getppid                     52544 ns/op
open+close /dev/null      1395540 ns/op
pipe+close x2             3129511 ns/op
mmap+touch+munmap 64K     5299550 ns/op
fork+wait                20428868 ns/op
```

여섯 로그를 모은 표(2회 평균, 반올림):

| 항목 | Image-trace | Image-kasan | kasan + 런타임 3종 끔 | kasan ÷ trace |
|---|---|---|---|---|
| 부팅 → `/init` (게스트 uptime) | 2.37 s | 17.52 s | 10.15 s | 7.4x |
| `MemTotal` (커널이 쓸 수 있게 넘긴 RAM) | 942.7 MiB | 672.1 MiB | 674.4 MiB | −270 MiB |
| 부팅 직후 `MemFree` | 877.3 MiB | 493.0 MiB | 533.8 MiB | −384 MiB |
| `Slab` | 11.5 MiB | 66.7 MiB | 38.9 MiB | 5.8x |
| `Percpu` | 0.3 MiB | 3.8 MiB | 3.8 MiB | 12.9x |
| getppid | 24.7 us | 53.1 us | 55.0 us | 2.2x |
| open+close `/dev/null` | 111 us | 1421 us | 477 us | 12.8x |
| pipe+close ×2 | 196 us | 3096 us | 939 us | 15.8x |
| mmap+touch+munmap 64K | 1.12 ms | 5.22 ms | 4.10 ms | 4.7x |
| fork+wait | 2.46 ms | 21.8 ms | 10.8 ms | 8.9x |

```svg
<svg viewBox="0 0 680 422" xmlns="http://www.w3.org/2000/svg">
  <text x="20" y="22" font-size="13">Image-kasan ÷ Image-trace (배수, 1.0 = 같음) — QEMU TCG, 2회 평균</text>
  <rect x="160" y="32" width="14" height="10" fill="#d0564a" fill-opacity="0.6"/><text x="180" y="42" font-size="12">Image-kasan 기본 (전부 켬)</text>
  <rect x="370" y="32" width="14" height="10" fill="#4a7bd0" fill-opacity="0.6"/><text x="390" y="42" font-size="12">slab_debug=- kmemleak=off kfence 끔</text>
  <text x="20" y="77" font-size="12">부팅 → /init</text>
  <rect x="160" y="60" width="192" height="16" fill="#d0564a" fill-opacity="0.6"/>
  <text x="358" y="73" font-size="12">7.4x</text>
  <rect x="160" y="80" width="111" height="16" fill="#4a7bd0" fill-opacity="0.6"/>
  <text x="277" y="93" font-size="12">4.3x</text>
  <text x="20" y="129" font-size="12">getppid</text>
  <rect x="160" y="112" width="56" height="16" fill="#d0564a" fill-opacity="0.6"/>
  <text x="222" y="125" font-size="12">2.2x</text>
  <rect x="160" y="132" width="58" height="16" fill="#4a7bd0" fill-opacity="0.6"/>
  <text x="224" y="145" font-size="12">2.2x</text>
  <text x="20" y="181" font-size="12">open+close</text>
  <rect x="160" y="164" width="332" height="16" fill="#d0564a" fill-opacity="0.6"/>
  <text x="498" y="177" font-size="12">12.8x</text>
  <rect x="160" y="184" width="111" height="16" fill="#4a7bd0" fill-opacity="0.6"/>
  <text x="277" y="197" font-size="12">4.3x</text>
  <text x="20" y="233" font-size="12">pipe+close ×2</text>
  <rect x="160" y="216" width="412" height="16" fill="#d0564a" fill-opacity="0.6"/>
  <text x="578" y="229" font-size="12">15.8x</text>
  <rect x="160" y="236" width="125" height="16" fill="#4a7bd0" fill-opacity="0.6"/>
  <text x="291" y="249" font-size="12">4.8x</text>
  <text x="20" y="285" font-size="12">mmap+touch 64K</text>
  <rect x="160" y="268" width="121" height="16" fill="#d0564a" fill-opacity="0.6"/>
  <text x="287" y="281" font-size="12">4.7x</text>
  <rect x="160" y="288" width="95" height="16" fill="#4a7bd0" fill-opacity="0.6"/>
  <text x="261" y="301" font-size="12">3.7x</text>
  <text x="20" y="337" font-size="12">fork+wait</text>
  <rect x="160" y="320" width="231" height="16" fill="#d0564a" fill-opacity="0.6"/>
  <text x="397" y="333" font-size="12">8.9x</text>
  <rect x="160" y="340" width="114" height="16" fill="#4a7bd0" fill-opacity="0.6"/>
  <text x="280" y="353" font-size="12">4.4x</text>
  <line x1="160" y1="56" x2="160" y2="372" stroke="currentColor"/>
  <line x1="186" y1="56" x2="186" y2="372" stroke="#888" stroke-dasharray="3 3"/>
  <text x="186" y="388" font-size="12" text-anchor="middle">1x</text>
  <text x="420" y="388" font-size="12" text-anchor="middle">10x</text>
  <line x1="420" y1="56" x2="420" y2="372" stroke="#888" stroke-dasharray="3 3"/>
</svg>
```
그림 6 — 실측 비용 배수(QEMU TCG, 같은 initramfs, 2회 평균). 빨강은 디버그 커널 기본 상태, 파랑은 같은 커널에서 slab_debug·kmemleak·KFENCE를 부팅 인자로 끈 상태다.

출력에서 볼 것·해석:
- **메모리**: `Memory:` 줄의 `reserved`가 89408K → 370004K로 **약 274 MiB** 늘었다. 그 대부분이 KASAN shadow다 — 1 GiB RAM의 1/8 = 128 MiB가 선형 매핑 shadow, 나머지는 커진 커널 이미지(code 23.7 → 67.5 MB, bss 0.9 → 19.4 MB)와 vmalloc·모듈 영역 shadow 등이다(항목별 분해는 하지 않았다). 그래서 `MemTotal` 자체가 270 MiB 줄었다. 부팅 직후 여유 메모리는 877 → 493 MiB. 1 GiB 보드에서 디버그 커널을 돌리면 **앱이 쓸 메모리가 거의 절반**이 된다는 뜻이다. 512 MiB 보드라면 OOM이 먼저 온다.
- **Slab 5.8배**: SLUB_DEBUG_ON의 redzone·추적 메타데이터, KASAN 메타데이터(alloc/free 스택 핸들), kmemleak 객체 기록 때문이다. 세 번째 열(런타임 3종 끔)에서 38.9 MiB로 줄어드는 것으로 보아 약 28 MiB가 slab_debug·kmemleak 몫이다. **Percpu 12.9배**는 lockdep의 CPU별 통계(`LOCK_STAT`)가 주범이다(런타임 인자로 꺼지지 않으므로 세 번째 열도 같다).
- **syscall 비용**: `getppid`(커널에서 거의 아무것도 안 함)는 2.2배인데, `open+close`·`pipe`처럼 **객체를 할당·해제하는 경로는 13~16배**다. 세 번째 열에서 open/pipe가 4.3~4.8배로 내려가는 것을 보면 그 차이의 대부분이 **slab_debug(SLUB_DEBUG_ON)와 kmemleak** — 할당·해제마다 하는 검사·기록 — 이고, KASAN+lockdep+UBSAN 자체는 약 4~5배다. 이 4~5배도 KASAN 혼자의 비용이 아니다 — lockdep(모든 락 획득에서 그래프 검사)·DEBUG_OBJECTS·DMA_API_DEBUG 등이 같이 켜져 있고, TCG는 메모리 접근이 많은 코드를 특히 느리게 에뮬레이션한다. 탐지기 하나씩의 비용을 정확히 나누려면 config를 하나씩 바꾼 커널을 따로 빌드해야 한다(이 노트에서는 하지 않았다).
- **부팅 7.4배**(2.4 → 17.5초): 부팅은 할당과 락의 연속이라 최악에 가깝다. 실기 보드에서도 디버그 커널은 부팅이 눈에 띄게 느리다 — watchdog·systemd 타임아웃(L1, M10)을 늘려야 할 때가 많다.
- **TCG와 공유 VM 주의**: 이 숫자들은 실기 수치가 아니다. 같은 `Image-trace`로 VM에 빌드 부하가 걸려 있던 시각(load average 11)에 잰 첫 시도(`cost_trace1.log`)에서는 `getppid`가 254101 ns/op, 부팅이 39.9초로 위 표의 약 10~17배였다. 그래서 위 표는 VM 부하가 내려간(load average 약 3) 뒤 **두 커널을 번갈아** 2회씩 잰 값만 썼다. 면접에서는 "QEMU에서 비율로 봤을 때 할당이 많은 경로는 한 자릿수~십수 배, 메모리는 shadow 1/8 + α"처럼 말하고, 실기 측정이 필요하다고 덧붙인다.

---

## 11. 어디에 무엇을 켜나 — 개발 · CI · 양산 전략

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah6" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="15" y="20" width="205" height="250" rx="8" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="117" y="44" font-size="13" text-anchor="middle">개발자 기기 · QEMU</text>
  <text x="117" y="64" font-size="12" text-anchor="middle">"느려도 다 켠다"</text>
  <g font-size="12" text-anchor="middle">
    <text x="117" y="94">KASAN generic</text>
    <text x="117" y="116">lockdep + ATOMIC_SLEEP</text>
    <text x="117" y="138">UBSAN · kmemleak</text>
    <text x="117" y="160">DEBUG_OBJECTS · LIST</text>
    <text x="117" y="182">DMA_API_DEBUG</text>
    <text x="117" y="204">KCSAN (별도 커널)</text>
    <text x="117" y="236">드라이버 새로 쓸 때</text>
    <text x="117" y="256">매일 이 커널로</text>
  </g>
  <rect x="237" y="20" width="205" height="250" rx="8" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="339" y="44" font-size="13" text-anchor="middle">CI · nightly</text>
  <text x="339" y="64" font-size="12" text-anchor="middle">"자동으로, 경고 0개"</text>
  <g font-size="12" text-anchor="middle">
    <text x="339" y="94">KASAN 커널로 KUnit</text>
    <text x="339" y="116">kselftest · 드라이버 테스트</text>
    <text x="339" y="138">syzkaller 퍼징</text>
    <text x="339" y="160">dmesg 에 BUG/WARN/</text>
    <text x="339" y="182">KASAN/UBSAN 있으면 실패</text>
    <text x="339" y="204">debug_locks 가 0이면 실패</text>
    <text x="339" y="236">kmemleak scan 결과</text>
    <text x="339" y="256">신규 항목이면 실패</text>
  </g>
  <rect x="459" y="20" width="205" height="250" rx="8" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="561" y="44" font-size="13" text-anchor="middle">양산 · 필드</text>
  <text x="561" y="64" font-size="12" text-anchor="middle">"거의 공짜인 것만"</text>
  <g font-size="12" text-anchor="middle">
    <text x="561" y="94">KFENCE (샘플링)</text>
    <text x="561" y="116">MTE async (칩이 지원)</text>
    <text x="561" y="138">UBSAN_BOUNDS + TRAP</text>
    <text x="561" y="160">LIST_HARDENED</text>
    <text x="561" y="182">refcount_t · FORTIFY</text>
    <text x="561" y="204">pstore/ramoops (L7)</text>
    <text x="561" y="236">리포트를 텔레메트리로</text>
    <text x="561" y="256">수집 → 함대 통계</text>
  </g>
  <line x1="220" y1="290" x2="459" y2="290" stroke="currentColor" marker-end="url(#ah6)"/>
  <text x="340" y="312" font-size="12" text-anchor="middle">오른쪽으로 갈수록 비용 예산이 작고, 기기 수가 많다</text>
</svg>
```
그림 7 — 같은 소스, 세 가지 커널. 개발 커널은 정확도, 양산 커널은 비용이 기준이다.

### 11.1 CI 판정 스크립트의 핵심

CI에서는 "테스트가 통과했다"만으로 부족하다. 탐지기는 대부분 **리포트만 찍고 계속 진행**하므로(`_noabort`), 테스트가 PASS여도 dmesg에 KASAN 리포트가 있을 수 있다. 그래서 테스트 후 로그를 판정한다. 아래는 이 노트의 게스트 로그에 그대로 쓴 판정 스크립트다(`m8_gate.sh`). DMA-API 줄은 부팅 때 정상적으로 찍히는 `DMA-API: preallocated …`를 빼려고 에러성 단어가 있는 줄만 센다.

```sh
# m8_gate.sh LOG : 탐지기 리포트가 하나라도 있으면 실패 (CI 게이트)
LOG=$1
PAT='BUG: KASAN|BUG: KFENCE|UBSAN:|BUG: KCSAN|possible circular locking|inconsistent .* usage|sleeping function called from invalid context|ODEBUG:|list_(add|del) corruption|DMA-API:.*(error|frees|unmap)|unreferenced object|WARNING: CPU'
n=$(grep -cE "$PAT" "$LOG")
if [ "$n" -gt 0 ]; then
    echo "GATE FAIL: $n detector lines"; grep -oE "$PAT" "$LOG" | sort | uniq -c | sort -rn | head
    exit 1
fi
echo "GATE PASS"
```

예제 16 — 이 노트의 로그 세 개에 게이트를 걸어 본 결과(VM 호스트에서 실행):

```sh
for f in cost_kasan_1.log misc2.log kasan3.log; do echo "\$ sh m8_gate.sh $f"; sh m8_gate.sh $f; echo "exit=$?"; done
```

```text
$ sh m8_gate.sh cost_kasan_1.log
GATE PASS
exit=0
$ sh m8_gate.sh misc2.log
GATE FAIL: 8 detector lines
      4 unreferenced object
      4 UBSAN:
exit=1
$ sh m8_gate.sh kasan3.log
GATE FAIL: 173 detector lines
    173 BUG: KASAN
exit=1
```

출력에서 볼 것: 디버그 커널을 그냥 부팅하고 벤치마크만 돌린 로그는 **깨끗하다**(`GATE PASS`) — 이 랩 커널의 built-in 드라이버들은 이 탐지기 묶음에서 경고를 내지 않는다는 뜻이고, 이것이 CI의 출발점이다. 테스트 모듈을 넣은 로그는 실패한다(UBSAN 4개 + 이 로그에 담긴 kmemleak 항목 4개, KASAN 173개). 실제 CI에서는 "테스트 스위트가 의도적으로 낸 리포트"를 제외하는 허용 목록이 필요하다 — KUnit 스위트는 별도 잡으로 돌리고 결과를 KTAP(`# Totals: pass:62 fail:1`)으로 판정하는 게 보통이다.

실무 팁: 부팅 인자 `panic_on_warn=1`, `kasan.fault=panic`(또는 `kasan_multi_shot` 없이 첫 리포트만), `oops=panic`을 주면 **첫 리포트에서 멈추고 kdump/pstore로 남긴다**(L7). 반대로 탐지기 버그를 한꺼번에 모으려면 `kasan_multi_shot`으로 계속 진행시킨다. syzbot도 같은 원리다 — KASAN/KCSAN/UBSAN/lockdep 커널을 퍼징하고 dmesg 패턴으로 버그를 판정한다.

### 11.2 양산 질문에 답하기

- **KASAN generic을 양산에?** 아니다. 메모리 1/8(+quarantine 1/32) 상시 사용, 이미지가 커지고, 커널 경로가 수 배 느리다(§10 실측). 개발·CI 전용.
- **KFENCE를 양산에?** 그렇다 — 설계 목표가 그것이다. 2 MiB 풀과 타이머 하나, fast path는 static key 분기뿐이다. 커널 문서(`Documentation/dev-tools/kfence.rst`)도 "양산 커널에 켜도록 설계됐고 오버헤드가 거의 0"이며, 충분한 총 가동 시간을 얻는 빠른 방법이 "large fleet of machines"에 배포하는 것이라고 적는다. 단, 한 대에서는 확률이 낮으므로 **리포트 수집 체계**(pstore·로그 업로드)가 짝이다.
- **MTE(KASAN HW_TAGS)를 양산에?** 칩이 MTE를 지원하면(ARMv8.5+, 예: Pixel 8 이후 Tensor) async 또는 asymm 모드로 가능하다. 태그 저장용 메모리(태그 3% 정도, HW가 별도 저장)와 태그 설정 비용이 있다. 임베디드 SoC(i.MX8·AM62·Orin의 Cortex-A53/A72/A78AE)는 MTE가 **없다** — 그런 보드에서는 KFENCE가 현실적인 선택이다.
- **UBSAN을 양산에?** `UBSAN_BOUNDS`+`UBSAN_TRAP` 정도는 일부 배포판·Android가 켠다. 전체 UBSAN은 개발용.

---

## 12. 커널 안에서는 무슨 일이 일어나나

KASAN generic 리포트가 나오기까지의 경로를 6.18 소스로 따라가 보자. 예제의 리포트 스택에 나온 함수 이름이 전부 이 경로 위에 있다.

```svg
<svg viewBox="0 0 660 380" xmlns="http://www.w3.org/2000/svg">
  <defs>
    <marker id="ah8" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
  </defs>
  <rect x="20" y="20" width="290" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="165" y="42" font-size="12" text-anchor="middle">빌드: scripts/Makefile.kasan</text>
  <text x="165" y="60" font-size="12" text-anchor="middle">-fsanitize=kernel-address</text>
  <rect x="350" y="20" width="290" height="50" rx="6" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/>
  <text x="495" y="42" font-size="12" text-anchor="middle">부팅: kasan_init() (arm64)</text>
  <text x="495" y="60" font-size="12" text-anchor="middle">shadow 영역 매핑, 처음엔 zero page</text>
  <rect x="20" y="100" width="290" height="70" rx="6" fill="#3f9a6b" fill-opacity="0.12" stroke="#3f9a6b"/>
  <text x="165" y="122" font-size="12" text-anchor="middle">kmalloc → slab_alloc_node</text>
  <text x="165" y="140" font-size="12" text-anchor="middle">→ kasan_slab_alloc / kasan_kmalloc</text>
  <text x="165" y="158" font-size="12" text-anchor="middle">shadow 언포이즌 + alloc stack 저장</text>
  <rect x="350" y="100" width="290" height="70" rx="6" fill="#e08a3c" fill-opacity="0.12" stroke="#e08a3c"/>
  <text x="495" y="122" font-size="12" text-anchor="middle">kfree → slab_free</text>
  <text x="495" y="140" font-size="12" text-anchor="middle">→ kasan_slab_free: 포이즌 fb/fa</text>
  <text x="495" y="158" font-size="12" text-anchor="middle">free stack 저장 → kasan_quarantine_put</text>
  <rect x="20" y="200" width="620" height="50" rx="6" fill="#888" fill-opacity="0.12" stroke="#888"/>
  <text x="330" y="222" font-size="12" text-anchor="middle">드라이버의 모든 load/store: 인라인 shadow 검사 (또는 __asan_load4 / __asan_store4)</text>
  <text x="330" y="240" font-size="12" text-anchor="middle">memcpy/memset 등은 kasan_check_range()로 범위 전체 검사</text>
  <line x1="330" y1="250" x2="330" y2="278" stroke="currentColor" marker-end="url(#ah8)"/>
  <rect x="20" y="282" width="620" height="80" rx="6" fill="#d0564a" fill-opacity="0.12" stroke="#d0564a"/>
  <text x="330" y="304" font-size="12" text-anchor="middle">위반: __asan_report_store4_noabort → kasan_report() (mm/kasan/report.c)</text>
  <text x="330" y="324" font-size="12" text-anchor="middle">bug type 판정(shadow 값) → 접근 스택 → alloc/free 스택(stack depot)</text>
  <text x="330" y="344" font-size="12" text-anchor="middle">→ 버그 주소의 slab·page 정보 → shadow 덤프 → 계속 진행 또는 panic</text>
</svg>
```
그림 8 — KASAN generic의 생애주기. 할당자 훅이 shadow를 칠하고, 컴파일러가 심은 검사가 읽고, `kasan_report()`가 리포트를 조립한다.

- 할당자 훅: `mm/slub.c`의 할당·해제 경로에서 `kasan_slab_alloc()`·`kasan_kmalloc()`·`kasan_slab_free()`(`include/linux/kasan.h` 래퍼 → `mm/kasan/common.c`)가 불린다. 페이지 할당자는 `kasan_unpoison_pages()`/`kasan_poison_pages()`, vmalloc은 `KASAN_VMALLOC`일 때 `kasan_populate_vmalloc()`로 shadow 자체를 그때그때 매핑한다.
- 스택 저장: `kasan_save_stack()` → `stack_depot_save()`(`lib/stackdepot.c`). 같은 스택은 한 번만 저장되고 객체에는 4바이트 핸들만 들어간다.
- 리포트: `kasan_report()` → `print_report()`가 `print_error_description()`(bug 종류) → 접근 스택 → `print_address_description()`(alloc/free 스택, 객체 위치 "located 0 bytes to the right of allocated 123-byte region") → `print_memory_metadata()`(shadow 덤프)를 찍는다. 출력이 끝나면 `end_report()`가 `add_taint(TAINT_BAD_PAGE, LOCKDEP_NOW_UNRELIABLE)`를 불러 이후 Oops에 taint 문자 `B`가 붙는다. 두 번째 인자 때문에 **KASAN 리포트가 한 번 나면 lockdep도 꺼진다** — 한 부팅에서 KASAN 버그를 먼저 고쳐야 lockdep 결과를 믿을 수 있다.
- 계측 제외: 부팅 초기 코드·KASAN 자신 등은 Makefile의 `KASAN_SANITIZE_foo.o := n`으로 빠진다. 드라이버 하나만 계측하고 싶으면 반대로 `KASAN_SANITIZE := y`를 그 Makefile에 두는 식도 가능하다.
- 드라이버 작성과 다시 만나는 곳: `kmalloc`/`kfree`(F9), `dma_map_*`(H 모듈, DMA_API_DEBUG), `timer_setup`/`INIT_WORK`(F7·F8, DEBUG_OBJECTS), `list_head`(E5, DEBUG_LIST), 스핀락·뮤텍스(F4, lockdep). 탐지기가 감시하는 API가 곧 드라이버가 매일 쓰는 API다.

---

## 13. Embedded 실무에서 다시 보기

### 13.1 "필드에서 1주일에 한 번 패닉" — 센서 키트 사례

예를 들어 센서 키트의 IMU SPI 드라이버가 FIFO를 읽을 때 장치가 보고한 샘플 수를 그대로 믿고 `memcpy(buf, rx, n * 12)`를 한다고 하자. 평소 n ≤ 32지만 SPI 노이즈로 FIFO count 레지스터가 가끔 0x3FF로 읽히면 384바이트 버퍼 뒤를 덮어쓴다. 증상은 몇 시간 뒤 네트워크 스택 Oops다. 접근 순서:

1. Oops 콜스택에 범인이 없음 → 메모리 손상 의심(M3).
2. **같은 소스로 KASAN 커널**을 만들어 같은 보드(또는 QEMU + 장치 모델)에서 스트레스 테스트. KASAN은 `memcpy` 순간 `slab-out-of-bounds in imu_read_fifo`를 찍는다(`kasan_check_range()`).
3. 재현이 안 되면 **필드 커널에 KFENCE**(이미 켜 두는 게 좋다) + pstore. 수백 대가 돌면 언젠가 그 384바이트 버퍼가 샘플에 걸려 `BUG: KFENCE: out-of-bounds write in imu_read_fifo`가 수집된다.
4. 고친 뒤 회귀 방지: FIFO count 범위 검사 + `UBSAN_BOUNDS`가 볼 수 있게 버퍼를 `struct { u8 buf[384]; }` 고정 배열로.

### 13.2 드라이버 `remove()`에서의 timer·work 정리

DEBUG_OBJECTS는 "timer가 아직 활성인데 그것을 품은 구조체를 `kfree`"를 `ODEBUG: free active … object type: timer_list`로 잡는다. 실기에서는 그 timer가 나중에 해제된 메모리 위에서 발화해 **가끔** 패닉이 난다. `remove()`에서 `timer_delete_sync()`(예전 이름 `del_timer_sync()`는 6.18에서 이미 없다)·`cancel_work_sync()`를 빠뜨린 전형적인 버그다(F7·F8, G3).

### 13.3 DMA 버퍼 짝

카메라·LiDAR 드라이버(I 모듈)가 에러 경로에서 `dma_unmap_single()`을 빼먹거나, map은 `DMA_FROM_DEVICE`로 하고 unmap은 `DMA_TO_DEVICE`로 하는 버그는 x86 개발 PC(캐시 일관)에서는 증상이 없고 **Cortex-A 보드에서만** 가끔 데이터가 틀린다. DMA_API_DEBUG는 개발 PC에서도 규칙 위반 자체를 찍어 준다.

### 13.4 BSP 커널 업그레이드 검증

벤더 BSP를 새 LTS로 올릴 때 첫 일은 **디버그 config로 부팅해 dmesg를 0으로 만드는 것**이다. 벤더 드라이버의 lockdep·DEBUG_OBJECTS·KASAN 경고가 수십 개 나오는 일이 흔하다. 이 노트의 `kasan_test`처럼 KUnit 스위트를 먼저 돌려 "탐지기 자체가 이 SoC·컴파일러에서 정상 동작"하는지 확인한 뒤, 드라이버 경고를 하나씩 없앤다.

---

## 14. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| KASAN 리포트가 나왔는데 테스트가 PASS라 무시 | 버그가 양산까지 감 | KASAN은 기본 `_noabort`, 리포트 후 계속 진행 | CI에서 dmesg 게이트(§11.1), 또는 `kasan.fault=panic`·`panic_on_warn=1` |
| lockdep 경고 하나 고친 뒤 "이제 깨끗" | 다음 부팅에 다른 경고 | 첫 보고 뒤 `debug_locks=0`으로 lockdep이 꺼져 나머지를 못 봄 | 하나씩 고치고 매번 재부팅, CI가 `debug_locks` 확인 |
| KASAN과 KCSAN을 한 커널에 | config에서 KCSAN이 안 켜짐 | `KCSAN depends on !KASAN` | 커널을 두 벌 빌드 |
| 디버그 커널용으로 빌드한 `.ko`를 양산 커널에 | `disagrees about version of symbol`/vermagic 오류, 또는 `__asan_*` 미정의 심볼 | 계측된 모듈은 KASAN 런타임이 필요 | 모듈은 항상 같은 빌드 트리로 (E3) |
| kmemleak 출력 전부를 누수로 판단 | 엉뚱한 곳 수정 | 물리 주소·태그 포인터만 보관하는 객체는 오탐 | `kmemleak_not_leak()`/`kmemleak_ignore()`, 두 번 스캔해 계속 나오는 것만 |
| KFENCE가 1시간째 아무것도 못 잡음 → 버그 없다고 결론 | 필드에서 계속 크래시 | 샘플링이라 한 대·짧은 시간에서는 확률이 낮음 | 함대 수집, 개발 중엔 `kfence.sample_interval=1`로 촘촘히, 확정은 KASAN |
| 4 KiB 버퍼 OOB를 slab_debug로만 찾으려 함 | 리포트 안 나옴 | `kmalloc_large`(> 8 KiB)·페이지 할당은 slab redzone 밖 | KASAN(page redzone `fe`) 또는 `debug_pagealloc=on` |
| QEMU TCG에서 잰 KASAN 오버헤드를 실기 수치로 발표 | 숫자 신뢰 상실 | TCG는 명령 번역 비용이 섞이고 공유 VM 부하가 큼 | 비율만 말하고 실기에서 다시 잼 |
| KASAN 커널에서 스택 overflow | `kernel stack overflow`/이상한 Oops | 계측이 스택을 더 씀 (E4 §7: arm64는 KASAN 시 스택 2배) | 큰 지역 배열 제거, `FRAME_WARN` 경고 확인 |

---

## 15. 면접에서 이렇게 말한다

**Q.** "We have intermittent memory corruption in a driver — crashes show up in unrelated code once a week. How do you find it?"

**A.** 콜스택이 무관한 코드를 가리키면 원인과 증상이 떨어진 메모리 손상이다. 같은 소스로 KASAN generic 커널을 만들어 스트레스·퍼징을 돌리면 손상을 일으키는 바로 그 명령에서 리포트가 나오고, 할당·해제 스택까지 준다. 재현이 안 되면 필드 커널에 KFENCE와 pstore를 켜서 함대에서 샘플로 잡는다. 범위를 좁히는 보조 도구는 slab_debug, DEBUG_OBJECTS(timer/work 수명), DMA_API_DEBUG(DMA 짝)다.

> When the crash stack points at unrelated code, it's corruption with cause and symptom separated in time. First I build the same tree with generic KASAN and stress the driver — KASAN reports at the faulting instruction with the allocation and free stacks, so the culprit line is right there. If it won't reproduce in the lab, I rely on KFENCE plus pstore in the field kernel; it's cheap enough to leave on, and across a fleet the bad object eventually gets sampled. Around that I use DEBUG_OBJECTS for timer and work lifetime bugs and DMA_API_DEBUG for map/unmap mismatches, and I gate CI on a clean dmesg.

**꼬리 질문**: "KASAN이 아무것도 못 찾으면?" → DMA가 쓰는 메모리는 CPU 계측 밖이다(장치가 직접 씀) — DMA_API_DEBUG, IOMMU fault 로그, 버퍼 끝 canary를 보고, 레이스면 KCSAN.

**Q.** "Can you ship KASAN in production? What about KFENCE and MTE?"

**A.** generic KASAN은 메모리 1/8 + quarantine, 수 배 느린 커널 경로 때문에 양산 불가다(이 노트의 실측도 그렇다). KFENCE는 양산용으로 설계됐다 — 2 MiB 풀과 샘플링 타이머만 쓰고 fast path 비용이 사실상 0이다. MTE(HW_TAGS)는 ARMv8.5 MTE가 있는 칩에서 async/asymm 모드로 양산 가능한 수준이지만, 대부분의 임베디드 SoC 코어(A53/A72/A78AE)는 MTE가 없다.

> Generic KASAN, no — it reserves an eighth of memory for shadow plus a quarantine, and kernel paths run several times slower. KFENCE, yes — that's its design goal: a small guarded pool and a sampling timer, near-zero fast-path cost, and fleet scale makes up for the low per-device probability. MTE-backed KASAN can run in production in async or asymmetric mode, but only on ARMv8.5+ cores with MTE; most embedded SoCs today, like Cortex-A53 or A78AE parts, don't have it, so KFENCE is the realistic choice there.

**Q.** "How does KASAN know an access is bad?"

**A.** 컴파일러가 모든 load/store 앞에 shadow 검사를 심는다. 8바이트마다 shadow 1바이트가 있고 0이면 전부 유효, 1~7이면 앞 N바이트만 유효, 음수면 금지이며 값이 이유(redzone, freed 등)다. 할당자가 할당 때 shadow를 풀고, 해제 때 칠하고 객체를 quarantine에 넣어 재사용을 늦춘다.

> The compiler instruments every load and store with a shadow check. Each 8-byte granule has one shadow byte: zero means fully addressable, 1 to 7 means only the first N bytes are, and negative values mean poisoned, with the value encoding why — slab redzone, freed object, stack redzone. The allocator unpoisons on allocation, poisons on free and holds freed objects in a quarantine so use-after-free doesn't land on a reused object.

**Q.** "What's the difference between lockdep and KCSAN?"

**A.** lockdep은 **락을 쓴 코드**의 순서·문맥 규칙을 검증한다 — 실제 deadlock 없이도 ABBA 가능성을 증명한다. KCSAN은 **락을 안 쓴(또는 잘못 안 쓴) 접근**의 데이터 레이스를 watchpoint 샘플링으로 찾는다. 하나는 정적 그래프, 하나는 동적 관찰이다.

> Lockdep validates code that does take locks: it builds a graph of lock-class ordering and IRQ usage and reports a possible deadlock the first time an inconsistent path runs, even if it never actually deadlocks. KCSAN looks for the opposite problem — plain concurrent accesses with no proper synchronization — by setting sampled watchpoints and catching a conflicting access from another CPU. Different bug classes, and you can't build KCSAN together with KASAN.

**Q.** "How do you read a KASAN report?"

**A.** 위에서부터: 버그 종류와 함수(`slab-out-of-bounds in foo+0x..`), 접근 크기·주소·태스크, 접근 콜스택(범인), Allocated by / Freed by 스택, "located N bytes to the right of M-byte region"(얼마나 넘었나), 마지막 shadow 덤프(`fc`·`fb` 등으로 무엇을 건드렸나). 함수+오프셋은 `faddr2line`/`decode_stacktrace.sh`로 줄 번호로 바꾼다(M3).

> Top down: the bug type and function, the access size and address, the access stack — that's where the bad code is — then the allocation and free stacks, then the object description that says how many bytes past which region, and finally the shadow dump where values like fc or fb tell me whether I hit a redzone or freed memory. I run the stack through decode_stacktrace or faddr2line to get source lines.

**Q.** "What would you put in a CI pipeline for kernel drivers?"

**A.** 같은 소스로 디버그 커널(KASAN+UBSAN+lockdep+DEBUG_OBJECTS+DMA_API_DEBUG+kmemleak)을 빌드하고 QEMU나 실기에서 KUnit·kselftest·드라이버 테스트를 돌린 뒤, 테스트 결과와 별개로 dmesg에 탐지기 패턴이 있으면 실패시킨다. lockdep이 꺼졌는지(`debug_locks`)도 본다. 별도 잡으로 KCSAN 커널, 가능하면 syzkaller.

> A debug-config build of the same tree — KASAN, UBSAN, lockdep, DEBUG_OBJECTS, DMA_API_DEBUG, kmemleak — booted in QEMU or on a board, running KUnit, kselftests and our driver tests. Pass/fail isn't just the test result: I grep dmesg for any detector report and fail on a single hit, check that debug_locks is still 1, and run a kmemleak scan at the end. A separate job runs a KCSAN kernel, and syzkaller if we expose ioctls.

**Q.** "Why didn't slab_debug catch my overflow but KASAN did?"

**A.** slab_debug는 redzone을 **나중에**(해제·검증 시점) 확인하는 사후 검사이고 쓰기만 본다. 읽기 OOB나 UAF 읽기는 못 잡고, 잡아도 "언제"를 모른다. KASAN은 모든 접근에서 즉시 검사한다. 대신 slab_debug는 재컴파일 없이 부팅 인자로 켤 수 있다(F9 §9).

> slab_debug checks red zones and poison after the fact, at free or validation time, so it only sees writes and can't tell you when it happened; it also misses out-of-bounds reads. KASAN checks every access as it happens. The upside of slab_debug is that it's a boot parameter on any SLUB_DEBUG kernel, no rebuild needed.

---

## 16. 직접 해보기

1. (개념) `kmalloc(100)`을 받은 뒤 `p[100]`과 `p[104]`와 `p[128]`에 쓰면 generic KASAN의 shadow는 각각 무엇이고, 셋 다 잡히나? — 정답/힌트: 100 = 12×8 + 4라 13번째 granule shadow는 `04`, `p[100]`·`p[104]`는 그 granule 밖이거나 부분값 위반으로 잡히고, `p[128]`은 kmalloc-128 칸 뒤 redzone(`fc`)이라 잡힌다(`kmalloc_oob_right` 테스트와 같은 구조).
2. (관찰) `kasan_test`를 `kasan.fault=panic`으로 다시 돌려 첫 리포트에서 패닉하는지 확인하라. — 정답/힌트: 부팅 인자 추가, 첫 `BUG: KASAN` 직후 `Kernel panic - not syncing: kasan.fault=panic set ...`.
3. (관찰) `kfence.sample_interval=1`로 부팅한 뒤 `/sys/kernel/debug/kfence/stats`의 `total allocations`가 기본(100 ms)보다 얼마나 빨리 느는지 비교하라. — 정답/힌트: 약 100배 빠르게 풀이 돈다. 풀이 꽉 차면 `skipped allocations (capacity)`가 는다.
4. (코드) §11.1 게이트 스크립트를 kmemleak "새 항목만" 판정하도록 고쳐라(부팅 직후 `clear`, 테스트 후 `scan`). — 정답/힌트: 테스트 전에 `echo clear > /sys/kernel/debug/kmemleak`, 후에 `echo scan`을 두 번(오탐 줄이기) 하고 `unreferenced object` 개수 판정.
5. (개념) KFENCE 객체가 페이지 오른쪽 끝에 붙어 있을 때 **왼쪽** 1바이트 OOB 쓰기는 언제 어떻게 발견되나? — 정답/힌트: guard 페이지가 아니라 canary 위라 즉시 fault는 없고, `kfree()` 때 canary 검사로 `BUG: KFENCE: memory corruption`이 난다.
6. (관찰) `Image-kasan`에서 `/proc/lockdep_stats`의 `lock-classes`를 부팅 직후와 `kasan_test` 후 비교하라. — 정답/힌트: 새 코드 경로가 돌면 클래스·간선이 는다. 상한(`MAX_LOCKDEP_KEYS` 8192)에 닿으면 lockdep이 스스로 꺼진다.
7. (빌드) trace config에서 KASAN 계열을 빼고 `CONFIG_KCSAN=y`, `CONFIG_KCSAN_KUNIT_TEST=m`으로 `O=/var/tmp/<ID>/kcsan` 커널을 빌드해 `kcsan_test`를 돌려 보라. — 정답/힌트: `scripts/config -d KASAN -e KCSAN -e KCSAN_KUNIT_TEST=m` 후 olddefconfig, arm64는 `EXPERT=y`가 필요(`HAVE_ARCH_KCSAN if EXPERT`).

---

## 17. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| KASAN | Kernel Address SANitizer | 컴파일러 계측 + shadow memory로 OOB·UAF를 접근 순간 잡는다 |
| shadow memory | 그림자 메모리 | 커널 메모리 8B마다 1B로 접근 가능 여부·이유를 적은 영역 |
| granule | 검사 단위 | generic 8B, tag 모드 16B |
| redzone | 금지 구역 | 객체·스택 변수·전역 뒤에 둔 접근 금지 영역 |
| quarantine | 격리 구역 | 해제 객체를 바로 재사용하지 않고 잠시 두는 FIFO (RAM의 1/32) |
| stack depot | 스택 저장소 | 할당·해제 콜스택을 중복 없이 저장, 핸들로 참조 |
| inline / outline | 계측 방식 | 검사 코드를 함수 안에 펼치기 / `__asan_*` 호출로 |
| KFENCE | Kernel Electric-Fence | guard 페이지 사이에 할당 일부만 놓는 샘플링 탐지기 |
| MTE | Memory Tagging Extension | ARMv8.5 HW 메모리 태그, KASAN HW_TAGS의 기반 |
| TBI | Top Byte Ignore | arm64가 포인터 최상위 바이트를 주소 변환에서 무시 → SW_TAGS가 태그를 둠 |
| kmemleak | 커널 누수 탐지기 | 루트에서 도달 못 하는 할당 객체를 보고 |
| UBSAN | Undefined Behavior SANitizer | C 미정의 동작(범위 밖 인덱스, shift 등)을 런타임에 검사 |
| KCSAN | Kernel Concurrency SANitizer | watchpoint 샘플링으로 data race 탐지 |
| marked access | 표시된 접근 | `READ_ONCE`/`WRITE_ONCE`/atomic — KCSAN이 의도된 동시 접근으로 본다 |
| lockdep | 락 검증기 | 락 클래스 순서·IRQ 상태 그래프로 deadlock 가능성 증명 (F4) |
| `debug_locks` | lockdep 생존 플래그 | 첫 보고 후 0이 되어 lockdep이 꺼진다 |
| DEBUG_OBJECTS | 객체 수명 검사 | timer·work 등이 활성인 채 해제·재초기화되는지 |
| DMA_API_DEBUG | DMA API 검사 | map/unmap 짝, 방향·크기 불일치, unmap 누락 |
| KUnit | 커널 단위 테스트 프레임워크 | `kasan_test`·`kfence_test`처럼 커널 안에서 도는 테스트 스위트 |
| syzkaller / syzbot | 커널 퍼저 / 그 자동 서비스 | 탐지기 커널에 syscall을 퍼징해 리포트를 판정 |

---

## 18. 요약 & 체크리스트

메모리·동시성 버그는 원인과 증상이 떨어져 있어서 Oops 스택만으로는 범인을 못 찾는다. Linux는 이걸 "원인 시점"으로 당겨 오는 탐지기를 Kconfig로 제공한다. KASAN generic은 컴파일러가 모든 접근 앞에 1:8 shadow 검사를 심고 할당자가 redzone·quarantine·스택 기록을 더해 OOB·UAF를 그 명령에서 잡지만, 메모리·속도 비용이 커서 개발·CI 전용이다. KFENCE는 guard 페이지와 샘플링으로 비용을 거의 0으로 만들어 양산에 켜고, MTE가 있는 칩이면 HW_TAGS KASAN이 그 중간이다. kmemleak은 도달 불가 객체를, UBSAN은 C 미정의 동작을, KCSAN은 data race를(별도 커널), lockdep은 락 순서를(F4), DEBUG_OBJECTS·DEBUG_LIST·DMA_API_DEBUG는 API 사용 규칙을 지킨다. 실무 전략은 "개발 커널은 다 켜고, CI는 dmesg 0개를 게이트로, 양산은 공짜에 가까운 것만 + 리포트 수집"이다.

- [ ] 메모리 손상에서 원인과 증상이 왜 떨어지는지, 탐지기가 무엇을 바꾸는지 설명할 수 있다
- [ ] generic KASAN의 shadow 인코딩(00, 01~07, fc/fb/f1…)과 주소 변환식을 그릴 수 있다
- [ ] inline/outline 계측이 어떤 어셈블리가 되는지 설명할 수 있다
- [ ] redzone과 quarantine이 각각 무엇을 막는지 말할 수 있다
- [ ] KASAN 리포트를 위에서부터 읽고 범인 줄·할당/해제 스택·넘은 바이트 수를 찾을 수 있다
- [ ] KFENCE의 guard 페이지·canary·샘플링 원리와 양산에 쓸 수 있는 이유를 설명할 수 있다
- [ ] kmemleak을 수동 스캔하고 오탐을 구분할 수 있다
- [ ] UBSAN이 잡는 UB 종류와 `UBSAN_BOUNDS`의 한계를 말할 수 있다
- [ ] KCSAN 리포트를 읽고 lock·marked access·`data_race()` 중 무엇으로 고칠지 고를 수 있다
- [ ] lockdep이 첫 보고 후 꺼지는 것과 CI에서의 의미를 설명할 수 있다
- [ ] 탐지기별 비용을 실측값(이미지·부팅·메모리·syscall)으로 말할 수 있다
- [ ] 개발·CI·양산에 어떤 탐지기를 켤지 근거와 함께 제안할 수 있다

## 참고 자료

- docs.kernel.org — The Kernel Address Sanitizer (KASAN): https://docs.kernel.org/dev-tools/kasan.html
- docs.kernel.org — Kernel Electric-Fence (KFENCE): https://docs.kernel.org/dev-tools/kfence.html
- docs.kernel.org — Kernel Memory Leak Detector: https://docs.kernel.org/dev-tools/kmemleak.html
- docs.kernel.org — The Undefined Behavior Sanitizer (UBSAN): https://docs.kernel.org/dev-tools/ubsan.html
- docs.kernel.org — The Kernel Concurrency Sanitizer (KCSAN): https://docs.kernel.org/dev-tools/kcsan.html
- docs.kernel.org — Runtime locking correctness validator: https://docs.kernel.org/locking/lockdep-design.html
- docs.kernel.org — Dynamic DMA mapping using the generic device (DMA_API_DEBUG 절): https://docs.kernel.org/core-api/dma-api.html
- docs.kernel.org — The object-lifetime debugging infrastructure: https://docs.kernel.org/core-api/debug-objects.html
- docs.kernel.org — KUnit: https://docs.kernel.org/dev-tools/kunit/index.html
- 소스: `mm/kasan/` (`kasan_test_c.c`, `report.c`, `generic.c`, `quarantine.c`), `mm/kfence/`, `mm/kmemleak.c`, `lib/ubsan.c`, `lib/test_ubsan.c`, `samples/kmemleak/kmemleak-test.c`, `kernel/kcsan/`, `lib/debugobjects.c`, `kernel/dma/debug.c` — https://elixir.bootlin.com/linux/v6.18/source
- syzkaller: https://github.com/google/syzkaller

