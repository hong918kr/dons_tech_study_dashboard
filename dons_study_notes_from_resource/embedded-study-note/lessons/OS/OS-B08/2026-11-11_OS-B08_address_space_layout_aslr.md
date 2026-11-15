---
id: OS-B08
time: 60분
updated: 2026-11-11
---
# OS-B08 주소 공간 탐색: 텍스트·데이터·힙·스택과 ASLR

## 학습 목표

- 한 프로세스 안의 코드·rodata·data·bss·힙(작은/큰 할당)·mmap·공유 라이브러리·메인/스레드 스택 주소를 찍어 **배치 지도** 를 그리고, macOS(arm64)와 Linux(aarch64)의 지도가 어떻게 다른지 말할 수 있다.
- 여러 번 실행해서 **무엇이 바뀌고(ASLR), 무엇이 함께 움직이고, 무엇이 고정인지** 표로 정리한다 — 실행 파일 한 덩어리 = slide 하나, dyld 공유 캐시는 실행마다 고정, macOS 메인 스택은 slide 의 "거울", Linux 는 스택까지 페이지 안 오프셋을 섞는다.
- `vmmap`(macOS) / `/proc/<pid>/maps`(Linux) 출력을 읽고, 직접 만든 maps 파서·이진 탐색·`mach_vm_region_recurse` 로 같은 정보를 코드로 얻는다.
- `__PAGEZERO`(4 GB 접근 금지), 페이지 정렬(16 KB vs 4 KB), `mprotect` 가 영역을 쪼개는 모습을 실측으로 설명한다.
- MMU 도 ASLR 도 없는 Cortex-M 에서는 이 지도가 **링커 스크립트 그 자체** 라는 점과, 그래서 크래시 주소를 그대로 `addr2line` 에 넣을 수 있다는 점을 연결한다.

## 1. 문제

"크래시 로그의 PC 가 `0x104e685e8` 인데 `nm` 에는 `0x1000005e8` 이라고 나와요. 어느 게 맞죠?" — 펌웨어/임베디드 리눅스 면접에서 "프로세스 메모리 배치를 그려 보세요" 다음에 거의 반드시 따라오는 질문이다. 실무에서도 임베디드 리눅스 데몬의 크래시를 `/proc/<pid>/maps` + `addr2line` 으로 풀고, 메모리 누수를 `[heap]` 크기로 의심하고, 스레드 스택 8 MB × 스레드 수가 작은 보드의 가상 메모리를 먹는 걸 본다. 반대로 Cortex-M 은 MMU 가 없어서 주소가 링크 시점 그대로다 — 이 차이를 실측으로 몸에 붙이는 게 이 레슨이다.

OSTEP 13장은 "프로그램이 찍는 주소는 전부 가상 주소다" 라며 코드·힙·스택 주소를 출력하는 작은 프로그램을 보여 준다(그때 예시는 x86 Mac 이라 스택이 `0x7fff…` 였다). 오늘은 그걸 **이 맥(Apple M6, macOS 27.2)과 Lima VM(aarch64 Linux 7.0)** 에서 크게 확장해 본다. 아래 지도는 이 레슨에서 실제로 찍은 주소다 (축척 무시, 아래가 낮은 주소).

```svg
<svg viewBox="0 0 740 470" xmlns="http://www.w3.org/2000/svg" font-size="12">
  <text x="10" y="20" fill="currentColor" font-weight="bold" font-size="14">실측 주소 공간 (축척 무시, 아래 = 낮은 주소)</text>
  <text x="40" y="44" fill="currentColor" font-weight="bold" font-size="13">macOS 27.2 arm64 (16 KB 페이지)</text>
  <text x="400" y="44" fill="currentColor" font-weight="bold" font-size="13">Linux 7.0 aarch64 (4 KB 페이지)</text>
  <g stroke="currentColor">
    <rect x="40" y="56" width="300" height="34" class="f-mut"/>
    <rect x="40" y="90" width="300" height="46" class="f-acc"/>
    <rect x="40" y="136" width="300" height="34" class="f-ok"/>
    <rect x="40" y="170" width="300" height="34" class="f-warn"/>
    <rect x="40" y="204" width="300" height="34" class="f-warn"/>
    <rect x="40" y="238" width="300" height="40" class="f-ok"/>
    <rect x="40" y="278" width="300" height="34" class="f-mut"/>
    <rect x="40" y="312" width="300" height="34" class="f-acc"/>
    <rect x="40" y="346" width="300" height="70" fill="none" stroke-dasharray="5 4"/>
    <rect x="400" y="56" width="300" height="34" class="f-ok"/>
    <rect x="400" y="90" width="300" height="34" class="f-mut"/>
    <rect x="400" y="124" width="300" height="80" class="f-acc"/>
    <rect x="400" y="204" width="300" height="34" class="f-warn"/>
    <rect x="400" y="238" width="300" height="74" fill="none" stroke-dasharray="5 4"/>
    <rect x="400" y="312" width="300" height="34" class="f-ok"/>
    <rect x="400" y="346" width="300" height="46" class="f-acc"/>
  </g>
  <g fill="currentColor">
    <text x="48" y="77">Malloc Small  0x7c7f400000  ← malloc(1 MB)</text>
    <text x="48" y="111">dyld 공유 캐시 0x18000_0000–0x1e600_0000</text>
    <text x="48" y="127">&amp;printf 0x18765575c (부팅 동안 고정)</text>
    <text x="48" y="157">스레드 스택 0x16bcf…  (544K)</text>
    <text x="48" y="191">메인 스택 0x16bc6…  (8 MB, ↓)</text>
    <text x="48" y="225">Stack Guard 56 MB (---)</text>
    <text x="48" y="262">Malloc Tiny 0x10483…  ← malloc(32)</text>
    <text x="48" y="299">mmap 64 KB 0x1041bc000</text>
    <text x="48" y="333">__TEXT · __DATA_CONST · __DATA  0x104190000~</text>
    <text x="48" y="374">__PAGEZERO: [0, 4 GB + slide)</text>
    <text x="48" y="392">아무것도 매핑 안 됨 → NULL·32 비트 포인터 폴트</text>
    <text x="408" y="77">[stack] 0xfffffffdf000–2^48  (↓)</text>
    <text x="408" y="111">[vvar] [vdso]  ld-linux.so</text>
    <text x="408" y="145">libc.so.6 0xfffff7de0000~ (4 줄)</text>
    <text x="408" y="165">mmap 64 KB 0xfffff7dd0000</text>
    <text x="408" y="185">스레드 스택: 익명 mmap 8 MB + 64 KB</text>
    <text x="408" y="225">[heap] 0xaaaaaaac1000~ (brk, ↑)</text>
    <text x="408" y="270">큰 구멍 ≈ 85 TiB</text>
    <text x="408" y="333">demo_layout r-x·r--·rw- 0xaaaaaaaa0000~</text>
    <text x="408" y="374">(setarch -R 로 ASLR 끈 실행의 숫자)</text>
  </g>
  <text x="10" y="440" fill="currentColor">macOS: 스택이 실행 파일과 공유 캐시 "사이" 에 있다. Linux: 스택이 맨 위, 라이브러리·mmap 은 그 아래로 자란다.</text>
  <text x="10" y="460" fill="currentColor">두 OS 모두 페이지 0 근처는 비워 둔다. macOS 는 4 GB 전체를 비운다.</text>
</svg>
```

### Part A — `/proc/<pid>/maps` 파서와 영역 찾기 (Linux 출력을 코드로 읽기)

`start-end perms offset dev inode [path]` 한 줄을 `maps_ent_t` 로 파싱하는 `parse_maps_line()`, 정렬된 영역 배열에서 주소를 담은 영역을 **이진 탐색** 으로 찾는 `maps_find()`, 경로·권한으로 종류(실행 파일 text/ro/data, heap, stack, lib, anon, guard, vdso)를 매기는 `maps_kind()` 를 구현한다. 테스트 데이터는 Lima VM 에서 `setarch -R ./demo_layout -m` 으로 뽑은 **실제 maps 20 줄** 과 그 실행에서 찍힌 실제 주소들이다. 잘못된 줄(16 진수 아님, start ≥ end, 이상한 perms, 필드 부족)은 −1.

### Part B — 페이지 산수와 ASLR 분석 (여러 번 실행 결과를 코드로 판정)

`page_down/page_up/pages_spanned` (2 의 거듭제곱 페이지, 마스크로), 여러 실행의 주소에서 "한 번이라도 바뀐 비트" 를 구하는 `varying_bits()`, 두 객체가 같은 slide 로 함께 움직이는지 보는 `moves_together()` 를 구현한다. 테스트 데이터는 이 맥에서 같은 바이너리를 3 번 실행해 기록한 실제 주소 표다. 실측으로 **결론** 을 CHECK 한다: printf 는 고정, main 은 16 KB 단위로만 이동, main·data·bss·헤더는 함께 움직이고 힙·스택·mmap 은 아니다, 그리고 메인 스택은 `−slide` 로 움직인다.

### Part C — macOS 라이브 점검: 미니 vmmap

`mach_vm_region_recurse()` 로 "지금 이 프로세스" 의 주소가 어느 영역에 있고 권한이 뭔지 돌려주는 `vm_region_of()` 와 `prot_str()` 를 구현한다. 주소 숫자는 매번 다르지만 다음은 **매번 참** 이다: 코드 `r-x`, 리터럴·`const` 전역은 쓰기 불가, 전역·스택·힙은 `rw-` 이고 실행 불가, `[0, 4 GB)` 는 구멍, printf 는 main 과 다른 영역, 스레드 스택은 메인 스택과 다른 영역, 3 페이지 매핑의 가운데만 `mprotect` 하면 영역이 셋으로 쪼개지고 `munmap` 뒤엔 구멍.

## 2. Problem (English)

> **Explore a process address space.**
>
> 1. `int parse_maps_line(const char *line, maps_ent_t *e)` parses one line of Linux `/proc/<pid>/maps` (`start-end perms offset dev inode [path]`), returning 0 or −1 for malformed input (non-hex, `start >= end`, perms not matching `[r-][w-][x-][ps]`, missing fields). `int maps_find(const maps_ent_t *v, int n, uint64_t addr)` returns the index of the entry containing `addr` (half-open `[start, end)`) in O(log n), or −1 if `addr` is in a hole. `kind_t maps_kind(const maps_ent_t *e, const char *exe)` classifies an entry.
> 2. `page_down`, `page_up`, `pages_spanned(a, len, pg)` for power-of-two page sizes; `varying_bits(v, n)` = OR of `v[i] ^ v[0]`; `moves_together(a, b, n)` is true iff `a[i] - b[i]` is the same for all runs. Using recorded addresses from 3 runs on macOS, show which objects share one ASLR slide.
> 3. On macOS, `int vm_region_of(uintptr_t addr, vmr_t *out)` returns 0 and the region `[start, end)` + protection that contains `addr`, or 1 (hole) and the next region above. Must descend into submaps (the dyld shared cache is one).
>
> Example: `maps_find(sample, 20, 0xaaaaaaaa2000) == -1` (end is exclusive); `pages_spanned(0x1fff, 2, 4096) == 2`; `vm_region_of(0, &r) == 1 && r.start >= 4 GiB` on macOS.

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 깨짐 대비
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter
# 처음엔 "20/67 passed", exit 1 — TODO 10 곳(파서·탐색·분류, 페이지 산수 3, ASLR 2, vm_region_of, prot_str)
```

> macOS 전용이다 (Part C 가 `<mach/mach_vm.h>`). Part A/B 함수는 순수 C 라 Linux 에 그대로 옮겨진다. 데이터 파일은 필요 없다 — 샘플은 소스에 고정 문자열로 들어 있어 어느 cwd 에서 실행해도 된다.

관찰용 파일 2 개 (주소 숫자는 여기서만 본다):

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 -pthread demo_layout.c -o demo_layout && ./demo_layout   # 주소 표
./demo_layout -s 30 &  vmmap $!                                                               # 살아 있는 동안 vmmap
sh demo_aslr.sh 8                                                                             # 8 번 실행 비교표
```

<!-- code: demo_layout.c -->

<!-- code: demo_aslr.sh -->

## 4. 힌트

<details>
<summary>힌트 1 — maps 한 줄은 strtoull 세 번 + 건너뛰기 두 번</summary>

`strtoull(p, &end, 16)` 은 읽은 곳 다음을 `end` 에 돌려준다. `end == p` 면 숫자가 없었던 것. start 다음엔 반드시 `'-'`, end 다음엔 공백. perms 4 글자는 자리마다 허용 문자가 다르다(`"r-"`, `"w-"`, `"x-"`, `"ps"`). offset 다음 dev·inode 두 토큰은 값이 필요 없으니 건너뛰고, 남은 부분이 path (없을 수 있음 — 익명 매핑). 스택 끝 `1000000000000` 은 13 자리라 고정 폭으로 자르면 틀린다.

</details>

<details>
<summary>힌트 2 — 겹치지 않는 구간의 이진 탐색</summary>

일반 이진 탐색과 같고 비교만 둘이다: `addr < v[mid].start` 면 왼쪽, `addr >= v[mid].end` 면 오른쪽, 아니면 찾음. 구멍에 떨어지면 lo > hi 로 끝나 −1. `end` 는 포함하지 않는다.

</details>

<details>
<summary>힌트 3 — 페이지 산수는 마스크</summary>

`pg` 가 2 의 거듭제곱이면 `a % pg == a & (pg-1)`. 내림 `a & ~(pg-1)`, 올림 `(a + pg - 1) & ~(pg-1)`. 걸치는 페이지 수는 "마지막 바이트(a+len-1)의 페이지 − 첫 바이트의 페이지 + 1", 단 len 0 이면 0.

</details>

<details>
<summary>힌트 4 — "함께 움직인다" 의 수학</summary>

실행 i 에서 slide 를 s_i 라 하면 같은 이미지 안 객체는 `addr_i = link_addr + s_i`. 두 객체의 차이 `a_i − b_i = link_a − link_b` 는 i 와 무관하다. 차이가 실행마다 같으면 같은 slide 를 공유한 것. 만약 `a_i + b_i` 가 일정하다면? `b` 를 `−b` 로 바꿔 넣어 보면 같은 함수로 판정할 수 있다 (uint64 뺄셈은 2^64 모듈로라 안전).

</details>

<details>
<summary>힌트 5 — mach_vm_region 이 이상한 prot 를 돌려줄 때</summary>

`mach_vm_region(VM_REGION_BASIC_INFO_64)` 로 `&printf` 를 물으면 `[0x180000000, 0x1e6000000)` prot `r--` 라는, dylib 하나라기엔 너무 큰 영역이 나온다. dyld 공유 캐시는 **서브맵(submap)** 으로 붙어 있어서 겉 엔트리를 본 것이다. `mach_vm_region_recurse()` 를 쓰고 `info.is_submap` 이면 `depth++` 해서 **같은 주소로** 다시 묻는다. 매번 `address` 와 `count` 를 다시 채워야 한다 (호출이 덮어쓴다).

</details>

## 5. 풀이

### 실측 1 — 한 번 실행한 지도

`./demo_layout` (macOS, 위 지도와 같은 실행):

```text
pid 16428
  main (text)                       0x1041905e8
  "literal" (rodata)                0x104190c07
  g_rodata (const)                  0x104190bdc
  g_data (data)                     0x104198000
  g_bss (bss)                       0x104198014
  malloc(32)                        0x104849150
  malloc(1MB)                      0x7c7f400000
  mmap anon 64KB                    0x1041bc000
  &printf (libc)                    0x18765575c
  libc image base                   0x187628000
  main stack (local)                0x16bc6ebec
  thread stack (local)              0x16bcf6fbc
  main image header                 0x104190000
  main image slide                    0x4190000
```

같은 프로세스가 잠든 동안 `vmmap 16428` (420 줄 중 해당 줄만, 열 일부 생략):

```text
VM page size:  16384 bytes
__TEXT                      104190000-104194000    [   16K ...] r-x/r-x SM=COW   /tmp/OS-B08_work/demo_layout
__DATA_CONST                104194000-104198000    [   16K ...] r--/rw- SM=COW   /tmp/OS-B08_work/demo_layout
__DATA                      104198000-10419c000    [   16K ...] rw-/rw- SM=COW   /tmp/OS-B08_work/demo_layout
__LINKEDIT                  10419c000-1041a0000    [   16K ...] r--/r-- SM=COW   /tmp/OS-B08_work/demo_layout
Untagged (reserved)         1041bc000-1041cc000    [   64K ...] rw-/rwx SM=NUL   reserved VM address space (unallocated)
Malloc Tiny                 104838000-104c38000    [ 4096K ...] rw-/rwx SM=PRV   DefaultMallocZone_0x1046e4000
Stack Guard                 167c70000-16b474000    [ 56.0M ...] ---/rwx SM=NUL   stack guard for thread 0
Stack                       16b474000-16bc70000    [ 8176K ...] rw-/rwx SM=PRV   thread 0
Stack Guard                 16bc70000-16bc74000    [   16K ...] ---/rwx SM=NUL   stack guard for thread 1
Stack                       16bc74000-16bcfc000    [  544K ...] rw-/rwx SM=PRV   thread 1
__TEXT                      187628000-1876ab000    [  524K ...] r-x/r-x SM=COW   /usr/lib/system/libsystem_c.dylib
__DATA                      1f241c1c0-1f241e1c0    [    8K ...] rw-/rw- SM=COW   /usr/lib/system/libsystem_c.dylib
Malloc Small                7c7f400000-7c7f800000   [ 4096K ...] rw-/rwx SM=PRV   DefaultMallocZone_0x1046e4000
```

읽는 법:

| 내 객체 | vmmap 영역 | 관찰 |
|---|---|---|
| main, `"literal"`, `g_rodata` | `__TEXT` r-x | 코드와 `__TEXT,__const`/`__cstring` 이 한 세그먼트 = 한 페이지 (C-B11 의 섹션 표) |
| `g_data`, `g_bss` | `__DATA` rw- | data 와 bss(zerofill)가 같은 16 KB 페이지 |
| `__DATA_CONST` | r--/**rw-** | 현재 권한 r--, 최대 rw-: dyld 가 포인터를 고친(rebase) 뒤 읽기 전용으로 내림 |
| `malloc(32)` | Malloc Tiny 4 MB | 작은 할당은 이미지 바로 위 영역에서 |
| `malloc(1MB)` | Malloc Small @ `0x7c…` | 큰 할당은 수백 GB 위 전혀 다른 곳 |
| `mmap` 64 KB | Untagged (reserved), SM=NUL | 한 번도 안 건드려서 물리 페이지 0 (요구 시 0 채움, OSTEP 23장 demand zeroing) |
| 메인 스택 | Stack 8176K + Stack Guard 56 MB | 8 MB 스택 아래에 56 MB 접근 금지 |
| 스레드 스택 | Stack 544K + Guard 16K | 보조 스레드는 기본 512 KB 스택 (Apple 문서) + 여분. 메인 스택 **바로 위** |
| `&printf` | libsystem_c `__TEXT` | dyld 공유 캐시 안 |

**`__PAGEZERO` 는 vmmap 에 안 나온다.** 실행 파일에는 분명히 있다:

```text
$ otool -l demo_layout | grep -A5 LC_SEGMENT_64 | grep -E "segname|vmaddr|vmsize"   (일부)
  segname __PAGEZERO   vmaddr 0x0000000000000000   vmsize 0x0000000100000000
  segname __TEXT       vmaddr 0x0000000100000000   vmsize 0x0000000000004000
  segname __DATA_CONST vmaddr 0x0000000100004000   ...
$ size -m demo_layout
Segment __PAGEZERO: 4294967296 (zero fill)
Segment __TEXT: 16384
	Section __text: 948
	Section __const: 4
	Section __cstring: 255
...
$ otool -hv demo_layout
MH_MAGIC_64    ARM64        ALL  0x00     EXECUTE    18       1448   NOUNDEFS DYLDLINK TWOLEVEL PIE
```

`mach_vm_region` 으로 주소 0 을 물으면 `__TEXT` 시작(`0x10273c000`, 그 실행의 이미지 헤더)이 돌아왔다 — 0 부터 이미지 직전까지 **VM 엔트리 자체가 없다**. 이 커널이 `__PAGEZERO` 를 엔트리로 만들지 않고 그 아래를 아예 주소 공간 밖처럼 다루는 것으로 보인다 (내부 구현은 확인 안 함; 관찰은 "엔트리 없음 + 접근하면 폴트"). 기본 `size` 의 `others 4295000064` 가 큰 이유도 이 4 GB.

Linux VM, `setarch aarch64 -R ./demo_layout -m` (ASLR 끈 실행, Part A 의 테스트 데이터):

```text
aaaaaaaa0000-aaaaaaaa2000 r-xp 00000000 00:26 57270      /tmp/OS-B08/demo_layout
aaaaaaabf000-aaaaaaac0000 r--p 0000f000 00:26 57270      /tmp/OS-B08/demo_layout
aaaaaaac0000-aaaaaaac1000 rw-p 00010000 00:26 57270      /tmp/OS-B08/demo_layout
aaaaaaac1000-aaaaaac00000 rw-p 00000000 00:00 0          [heap]
fffff75c0000-fffff7dd0000 rw-p 00000000 00:00 0
fffff7dd0000-fffff7de0000 rw-p 00000000 00:00 0
fffff7de0000-fffff7f89000 r-xp 00000000 fd:01 5341       /usr/lib/aarch64-linux-gnu/libc.so.6
fffff7f89000-fffff7f9d000 ---p 001a9000 fd:01 5341       /usr/lib/aarch64-linux-gnu/libc.so.6
fffff7f9d000-fffff7fa0000 r--p 001ad000 fd:01 5341       /usr/lib/aarch64-linux-gnu/libc.so.6
fffff7fa0000-fffff7fa2000 rw-p 001b0000 fd:01 5341       /usr/lib/aarch64-linux-gnu/libc.so.6
...
fffff7ff6000-fffff7ffa000 r--p 00000000 00:00 0          [vvar]
fffff7ffa000-fffff7ffc000 r-xp 00000000 00:00 0          [vdso]
...
fffffffdf000-1000000000000 rw-p 00000000 00:00 0         [stack]
```

| 줄 | 정체 | 근거 |
|---|---|---|
| `r-xp` demo_layout | 코드 **+ rodata** | `g_rodata` 0x…14d8 가 여기. `readelf -lW`: 첫 LOAD 가 `R E`, Align 0x10000 |
| `r--p` demo_layout | RELRO (`.got`, `.data.rel.ro`) | 두 번째 LOAD(RW) 앞부분을 로더가 재배치 후 읽기 전용으로 (macOS `__DATA_CONST` 와 같은 생각) |
| `rw-p` demo_layout | `.data` + `.bss` | `g_data` 0x…0010, `g_bss` 0x…0030 |
| `[heap]` | brk 힙 | 끝이 `0xaaaaaac00000` = **2 MB 경계** (아래 실측 3) |
| 첫 익명 `rw-p` 0x810000 바이트 | 스레드 스택 | 8 MB + 64 KB. `strace` 에 `mmap(…, 8454144, …MAP_STACK…)` + `madvise(…, 65536, MADV_GUARD_INSTALL)` — 가드가 별도 `---p` 줄이 아니라 madvise 가드로 들어갔다 |
| libc `---p` | 세그먼트 사이 정렬 구멍 | 64 KB 정렬 때문에 생긴 빈칸을 접근 금지로 예약 |
| `[vvar]` `[vdso]` | 커널이 넣어 준 페이지 | `clock_gettime` 이 syscall 없이 도는 곳 (OS-B03) |
| `[stack]` | 메인 스레드 스택 | 끝이 `0x1000000000000` = 2^48 (48 비트 VA) |

### 실측 2 — 여러 번 실행: 무엇이 바뀌나 (`sh demo_aslr.sh 8`)

macOS:

```text
Darwin arm64  PAGESIZE=16384  runs=8
object                   distinct  d(main)     pgoff    varbits  run0 .. run1
main (text)                     8     same      same     26..14  0x1007f05e8 0x1027d85e8
"literal" (rodata)              8     same      same     26..14  0x1007f0c07 0x1027d8c07
g_rodata (const)                8     same      same     26..14  0x1007f0bdc 0x1027d8bdc
g_data (data)                   8     same      same     26..14  0x1007f8000 0x1027e0000
g_bss (bss)                     8     same      same     26..14  0x1007f8014 0x1027e0014
malloc(32)                      8        -      same     26..14  0x100d29150 0x1030c9150
malloc(1MB)                     8        -      same     35..22  0x7917400000 0x7a0f400000
mmap anon 64KB                  8        -      same     26..14  0x100bd4000 0x102804000
&printf (libc)                  1        -      same       none  0x18765575c 0x18765575c
libc image base                 1        -      same       none  0x187628000 0x187628000
main stack (local)              8   mirror      same     26..14  0x16f60eb8c 0x16d626b8c
thread stack (local)            8   mirror      same     26..14  0x16f696fbc 0x16d6aefbc
main image header               8     same      same     26..14  0x1007f0000 0x1027d8000
```

Linux VM (`randomize_va_space = 2`), 그리고 `setarch aarch64 -R` 로 끈 경우:

```text
Linux aarch64  PAGESIZE=4096  runs=8
object                   distinct  d(main)     pgoff    varbits  run0 .. run1
main (text)                     8     same      same     46..16  0xad0585290d40 0xb4ce2ad40d40
g_data (data)                   8     same      same     46..16  0xad05852b0010 0xb4ce2ad60010
malloc(32)                      8        -      same     46..12  0xad05c2af8010 0xb4ce49c0c010
malloc(1MB)                     8        -         -      46..4  0xad05c2af8040 0xb4ce49c0c040
mmap anon 64KB                  8        -      same     44..12  0xfd1163910000 0xe90cb71e0000
&printf (libc)                  8        -      same     44..16  0xfd116396f8a0 0xe90cb723f8a0
main stack (local)              8        -         -      29..4  0xfffffc8b6564 0xffffc1c05f54
thread stack (local)            8        -      same     44..12  0xfd116390e87c 0xe90cb71de87c
(setarch -R)  모든 줄 distinct = 1, varbits none   — main 0xaaaaaaaa0d40, [stack] 0xfffffffff884 ...
```

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">실행마다: 같은 색 = 같은 난수로 함께 움직이는 묶음 (실측)</text>
  <text x="20" y="48" fill="currentColor" font-weight="bold">macOS</text>
  <g stroke="currentColor">
    <rect x="20" y="58" width="230" height="78" rx="6" class="f-acc"/>
    <rect x="270" y="58" width="140" height="78" rx="6" class="f-acc" stroke-dasharray="5 3"/>
    <rect x="430" y="58" width="130" height="78" rx="6" class="f-warn"/>
    <rect x="580" y="58" width="140" height="78" rx="6" class="f-ok"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="80">실행 파일: text·rodata·</text>
    <text x="30" y="98">data·bss·헤더 = +slide</text>
    <text x="30" y="122" font-size="12">slide = 16 KB 배수</text>
    <text x="280" y="80">메인·스레드 스택</text>
    <text x="280" y="98">= 상수 − slide</text>
    <text x="280" y="122" font-size="12">("mirror")</text>
    <text x="440" y="80">힙·mmap</text>
    <text x="440" y="98">각자 따로</text>
    <text x="590" y="80">dyld 공유 캐시</text>
    <text x="590" y="98">실행마다 고정</text>
    <text x="590" y="122" font-size="12">(부팅마다 바뀜: 문서)</text>
  </g>
  <text x="20" y="170" fill="currentColor" font-weight="bold">Linux (aarch64)</text>
  <g stroke="currentColor">
    <rect x="20" y="180" width="230" height="78" rx="6" class="f-acc"/>
    <rect x="270" y="180" width="140" height="78" rx="6" class="f-warn"/>
    <rect x="430" y="180" width="130" height="78" rx="6" class="f-ok"/>
    <rect x="580" y="180" width="140" height="78" rx="6" class="f-mut"/>
  </g>
  <g fill="currentColor">
    <text x="30" y="202">실행 파일 (PIE)</text>
    <text x="30" y="220">64 KB 단위 (p_align)</text>
    <text x="280" y="202">[heap] brk</text>
    <text x="280" y="220">따로 무작위</text>
    <text x="440" y="202">mmap 영역: libc·</text>
    <text x="440" y="220">ld·스레드 스택</text>
    <text x="590" y="202">[stack]</text>
    <text x="590" y="220">페이지 안까지 섞음</text>
  </g>
  <text x="10" y="286" fill="currentColor">ASLR 은 "묶음의 시작점" 만 바꾼다 → 묶음 안 상대 거리는 그대로. 주소 하나가 새면 묶음 전체가 샌다.</text>
</svg>
```

표에서 읽어 낸 사실과 확인 수준:

| 사실 | OS | 근거 |
|---|---|---|
| 실행 파일 전체가 한 slide 로 이동, 섹션 간 거리 불변 | 둘 다 | `d(main) = same` (text·rodata·data·bss·헤더) |
| slide 는 페이지 배수 → 주소의 페이지 안 오프셋은 매번 같다 | macOS 16 KB, Linux 64 KB | `pgoff same`, varbits 하한 14 / 16. Linux 는 `readelf` LOAD Align 0x10000 과 일치 |
| macOS slide 범위 | macOS | 300 회 실행: 287 개 서로 다른 값, 최소 0x4000, 최대 0x4fb4000 (≈ 80 MB, 약 12~13 비트). **관찰 범위이지 커널이 보장하는 범위가 아님** |
| dyld 공유 캐시 고정 | macOS | 8·30·300 회 모두 `&printf = 0x18765575c`. `~/Library/Logs/DiagnosticReports/*.ips` 에서 11-04 ~ 11-08 의 **다른 프로세스들**(managedcorespotlightd 등) 크래시 리포트도 libsystem_c base `0x187628000`, sharedCache base `0x18708c000` — 이번 부팅(11-01) 동안 시스템 전체가 같은 값 |
| 공유 캐시는 **부팅마다** 새 slide | macOS | Apple/XNU 설명(공유 영역 slide 는 부팅 때 정함). **재부팅 전후 비교는 못 함** — 이전 부팅의 리포트가 디스크에 없었다 |
| 메인 스택 = 상수 − slide | macOS | 300 회 모두 `stack + slide = 0x16fdfebfc` 하나. 독립 난수가 아니다 → 코드 주소 하나가 새면 스택 위치도 계산된다. 이유(XNU 코드)는 확인 안 함 |
| 스레드 스택은 메인 스택 바로 위, 같이 움직임 | macOS | `mirror`, vmmap 에서 thread 0 Stack 끝 = thread 1 Guard 시작 |
| 힙 시작은 이미지와 따로 | 둘 다 | `d(main) = -` |
| Linux 스택은 페이지 안 오프셋까지 무작위 | Linux | `pgoff -`, varbits 29..4. 커널 `arch_align_stack()` 이 SP 를 페이지 크기 미만만큼 더 내린다 (arch/arm64/kernel/process.c) |
| `setarch -R` 은 프로세스별로 ASLR 을 끈다 | Linux | personality(ADDR_NO_RANDOMIZE). 모든 주소 고정, 실행 파일 `0xaaaaaaaa0000` |
| `-Wl,-no_pie` 는 arm64 macOS 에서 무시 | macOS | `ld: warning: -no_pie ignored for arm64`, `otool -hv` 여전히 `PIE`, slide 계속 바뀜 |

### 실측 3 — 큰 malloc 은 어디서 오나

macOS: `malloc(1MB)` 는 항상 `0x7…` 대의 "Malloc Small" 영역 (8 회 모두, 300 회 0x7489400000~0x7cf3400000). 작은 할당(Malloc Tiny)과 완전히 다른 곳이다.

Linux: 표의 `malloc(1MB)` 줄은 `pgoff -` 이다. 12 번 돌려 보니 **8 번은 `malloc(32)+0x30`(brk 힙)**, 4 번은 mmap 영역. glibc 의 mmap 임계값(기본 128 KB)보다 큰데 왜 힙에서? `strace -e brk,mmap` 실측:

```text
malloc(32) 0xca99d8c46010   malloc(1MB) 0xca99d8c46040
brk(NULL) = 0xca99d8c46000
brk(0xca99d8e00000) = 0xca99d8e00000          ← 첫 확장이 2 MB 경계까지: top 이 1.7 MB
---
malloc(32) 0xbadd39bda010   malloc(1MB) 0xe0b3ee2df010
brk(NULL) = 0xbadd39bda000
brk(0xbadd39c00000) = 0xbadd39c00000          ← 경계까지 152 KB 뿐
mmap(NULL, 1052672, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0) = 0xe0b3ee2df000
```

glibc 는 요청을 **먼저 top chunk 에서** 자를 수 있으면 자르고, 모자랄 때만 `sysmalloc` 에서 mmap 임계값을 본다. 이 glibc 2.43 은 첫 brk 를 다음 2 MB 경계까지 늘렸다 → 힙 시작이 경계에서 1 MB 이상 떨어져 있으면(무작위) 1 MB 가 힙에서 나온다. `GLIBC_TUNABLES=glibc.malloc.hugetlb=0` 으로도 2 MB 정렬은 그대로라, 왜 2 MB 인지는 확인하지 못했다. 교훈: **"큰 할당은 mmap" 은 규칙이 아니라 경향** 이다. 할당기 내부는 OS-M04/M05.

### Part A — maps 파서, 이진 탐색, 분류

파서는 형식 검사를 앞에서부터 차례로 한다 — `strtoull` 이 아무것도 못 읽었는지(`p == q`), 구분자가 맞는지, start < end 인지, perms 자리별 허용 문자. 그다음 dev·inode 를 토큰 단위로 건너뛰고 나머지를 path 로(앞뒤 공백·`\n` 제거, 63 자에서 자름).

`maps_find` 는 커널이 maps 를 **start 오름차순·겹침 없음** 으로 출력한다는 성질을 이용한 O(log n) 탐색이다. 테스트가 찌르는 경계:

| 주소 | 기대 | 이유 |
|---|---|---|
| `0xaaaaaaaa1fff` | 0 | 첫 영역의 마지막 바이트 |
| `0xaaaaaaaa2000` | −1 | `end` 는 제외 → 다음 영역(0x…bf000) 전까지 구멍 |
| `0xaaaaaaaa14d8` | 0, **EXE_TEXT** | `const` 전역이 r-x 줄에. ELF 링커가 `.rodata` 를 text 와 같은 읽기 전용 LOAD 에 넣었다 |
| `0xfffff7dce87c` | 4, **ANON** | 스레드 스택은 `[stack]` 이 아니다 — 이름 없는 mmap |
| `0xfffff7f90000` | 7, GUARD | path 가 libc 여도 `---p` 면 접근 금지 구멍 |
| `0` | −1 | NULL 근처는 아무것도 없다 |

`maps_kind` 에서 순서가 중요하다: `---p` 검사를 path 검사보다 **먼저** 해야 libc 의 정렬 구멍이 LIB 로 잘못 분류되지 않는다 (처음 짠 버전이 바로 이 실수로 `kind_ok` 에서 FAIL 했다).

### Part B — 페이지 산수와 ASLR 판정

| 함수 | 식 | 예 |
|---|---|---|
| `page_down(a, pg)` | `a & ~(pg-1)` | `0x16ce5eb4c` → `0x16ce5c000` (16 KB) |
| `page_up(a, pg)` | `(a + pg-1) & ~(pg-1)` | `0x102fa05e8` → `0x102fa4000` |
| `pages_spanned(a, len, pg)` | `(down(a+len-1) − down(a)) / pg + 1`, len 0 → 0 | `(0x1fff, 2, 4K)` = 2 — 2 바이트가 경계를 넘는다 |
| `varying_bits(v, n)` | `OR (v[i] ^ v[0])` | printf: 0, main: 비트 14~26 |
| `moves_together(a, b, n)` | 모든 i 에서 `a[i]−b[i] == a[0]−b[0]` | main·data: 참, main·malloc: 거짓 |

`1 MB` 가 4 KB 페이지로는 256 장, 16 KB 로는 64 장 — TLB 한 엔트리가 덮는 범위가 4 배라는 뜻이기도 하다 (Apple Silicon 이 16 KB 를 쓰는 이유 중 하나, OS-M01).

테스트의 "거울" 판정은 harness 가 `neg[i] = 0 − main[i]` 를 만들어 `moves_together(stack, neg, 3)` 로 묻는다: `stack[i] − (−main[i]) = stack[i] + main[i]` 가 일정한가. 3 회 실측값으로 세 번 모두 `0x26fdff134` 가 나온다 (main = 상수 + slide 이므로 stack + slide 도 일정 — 300 회 실험의 `0x16fdfebfc` 는 `-q` 인자 때문에 지역 변수 위치가 조금 다른 실행). 같은 데이터에서 스레드 스택 − 메인 스택도 `0x88480` 으로 일정했다.

### Part C — 미니 vmmap 과 서브맵 함정

처음엔 `mach_vm_region()` 을 썼고 `&printf` 검사가 FAIL 했다. 원인을 찍어 본 실제 출력:

```text
printf 0x18765575c -> kr=0 [0x180000000,0x1e6000000) prot=1 max=1          (mach_vm_region)
   recurse kr=0 [0x18748c000,0x18f150000) prot=5 depth=1                   (mach_vm_region_recurse)
main   0x104374460 -> kr=0 [0x104374000,0x104378000) prot=5 max=5
```

공유 캐시 1.6 GB 전체가 겉에서는 서브맵 엔트리 하나(prot `r--`)다. 한 단계 들어가야 실제 `r-x` 텍스트 영역이 보인다 (vmmap 은 여기에 dylib 이름표까지 붙여 쪼개 보여 준다). 그래서 `vm_region_of` 는 `is_submap` 이면 `depth++` 후 같은 주소로 다시 묻는다.

```svg
<svg viewBox="0 0 740 250" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">mprotect 는 VM 영역을 쪼갠다 (Part C 마지막 테스트, 16 KB 페이지)</text>
  <text x="20" y="52" fill="currentColor">mmap(3 페이지, rw-)</text>
  <g stroke="currentColor">
    <rect x="200" y="36" width="480" height="30" class="f-ok"/>
    <rect x="200" y="116" width="160" height="30" class="f-ok"/>
    <rect x="360" y="116" width="160" height="30" class="f-warn"/>
    <rect x="520" y="116" width="160" height="30" class="f-ok"/>
    <rect x="200" y="196" width="160" height="30" fill="none" stroke-dasharray="5 3"/>
    <rect x="360" y="196" width="160" height="30" fill="none" stroke-dasharray="5 3"/>
    <rect x="520" y="196" width="160" height="30" fill="none" stroke-dasharray="5 3"/>
  </g>
  <g fill="currentColor">
    <text x="410" y="56">rw-  (엔트리 1 개)</text>
    <text x="20" y="132">mprotect(b+pg, pg, R)</text>
    <text x="255" y="136">rw-</text>
    <text x="415" y="136">r--</text>
    <text x="575" y="136">rw-</text>
    <text x="200" y="166" font-size="12">end == b+pg</text>
    <text x="360" y="166" font-size="12">[b+pg, b+2pg) prot R</text>
    <text x="520" y="166" font-size="12">start == b+2pg</text>
    <text x="20" y="216">munmap(b, 3pg)</text>
    <text x="400" y="216">구멍 → vm_region_of == 1</text>
  </g>
  <path d="M440 70 L440 110" stroke="currentColor"/><path d="M434 102 L440 112 L446 102" stroke="currentColor" fill="none"/>
  <path d="M440 150 L440 190" stroke="currentColor" stroke-dasharray="3 3"/>
</svg>
```

권한은 페이지 단위로만 줄 수 있고 커널은 같은 속성의 연속 구간을 엔트리 하나로 관리한다(Linux 의 VMA, XNU 의 vm_map_entry). 가운데만 바꾸면 엔트리가 셋으로 나뉜다 — 사용자 공간 가드 페이지·JIT·`mprotect`+SIGSEGV 기법(OS-A06)이 모두 이 위에 있다. 첫 페이지의 `start` 는 검사하지 않는다: 바로 아래에 같은 속성의 익명 매핑이 있으면 커널이 합칠 수 있어서 매번 참인 사실이 아니다.

### 펌웨어 연결 — MMU 없는 Cortex-M 에는 "ASLR 지도" 가 없다

| | macOS / Linux 프로세스 | Cortex-M (예: STM32F4) |
|---|---|---|
| 주소 | 가상, 실행마다 slide | 물리 = 링크 주소. 매 부팅 같음 |
| 지도의 출처 | 로더(dyld / ld.so) + 커널 | 링커 스크립트 `MEMORY`/`SECTIONS` (TOOL-M01), map 파일 (BM-B05) |
| NULL 트랩 | `__PAGEZERO` 4 GB, Linux `mmap_min_addr` (VM: 32 KB) | 0x0000_0000 은 보통 flash 별칭(벡터 테이블!) — NULL 읽기가 **성공** 한다. MPU 로 막아야 (BM-A08) |
| 권한 | 페이지마다 r/w/x | MPU 영역 8~16 개 (ARCH-M15, SEC-M08) |
| 크래시 주소 → 심볼 | `atos -l <load>` / maps 로 slide 빼기 (연습 2) | 그대로 `addr2line` (연습 5, TOOL-M14) |
| 스택 넘침 | 가드 페이지 → SIGSEGV | 조용히 `.bss` 를 덮음 → 스택 페인팅·MPU 가드 (BM-A07) |

임베디드 리눅스(Cortex-A, MMU 있음)는 위 왼쪽 칸 그대로다. 보드에서 데몬이 죽으면 `cat /proc/<pid>/maps` 로 PC 가 어느 `.so` 의 몇 오프셋인지 Part A 처럼 찾고, 그 오프셋을 `addr2line -e lib.so` 에 넣는다.

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution
# 67/67 passed  — 레슨 폴더와 /tmp 두 곳에서, 연속 7 회 모두 같은 결과
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o so_asan && ./so_asan
# 67/67 passed, 리포트 없음
```

ASan 빌드는 그림자 메모리 등 큰 매핑을 추가하지만, 테스트가 확인하는 것은 "권한·포함 관계·[0, 4 GB) 구멍" 이라 그대로 통과했다. 주소 숫자를 CHECK 하지 않은 덕분이다.

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 |
|---|---|---|
| `nm` 주소(`0x1000005e8`)로 크래시 PC(`0x104e685e8`)를 찾음 | PIE 는 slide 만큼 밀려 있다 | `link = pc − (load − 0x100000000)` 또는 `atos -o bin -l <load> <pc>` (실측: `main (in demo_layout) + 0`) |
| maps 를 `%12lx-%12lx` 같은 고정 폭으로 파싱 | `[stack]` 끝이 13 자리 `1000000000000` | `strtoull` + 구분자 검사 |
| `addr <= end` 로 포함 검사 | 두 영역이 맞닿은 주소가 둘 다에 속함 | `start <= addr && addr < end` |
| `---p` 를 path 로 먼저 분류 | libc 정렬 구멍을 LIB 로 셈 | 권한 검사를 path 보다 먼저 |
| 스레드 스택을 `[stack]` 에서 찾음 | Linux `[stack]` 은 메인 스레드뿐 | 스레드 스택 = 익명 mmap (`pthread_getattr_np` 로 범위) |
| `mach_vm_region` 결과로 공유 캐시 권한 판단 | 서브맵 겉 엔트리(r--)를 본다 | `mach_vm_region_recurse` + `is_submap` 이면 depth++ |
| recurse 루프에서 address/count 를 재설정 안 함 | 호출이 address 를 영역 시작으로 덮어써서 엉뚱한 하위 엔트리를 받음 (실제로 겪음) | 반복마다 `a = addr; cnt = ..._COUNT_64;` |
| "ASLR 이니까 스택은 독립 난수" | macOS 실측: 스택 = 상수 − slide | 하나의 누출이 다른 것을 알려 줄 수 있다고 가정 |
| "큰 malloc 은 항상 mmap" | Linux 실측 12 회 중 8 회가 brk 힙에서 | 할당기 정책은 버전·상태 의존 — 측정 |
| macOS 에서 `-Wl,-no_pie` 로 ASLR 끄기 | arm64 에서 무시됨 (ld 경고) | 디버깅은 slide 를 빼서 하거나 lldb(기본으로 ASLR 끔) |
| 주소 숫자를 테스트에 하드코딩 | 실행마다 바뀐다 | 권한·순서·차이·페이지 오프셋 같은 **불변량** 만 CHECK |

## 8. 면접 Q&A

<details>
<summary>Q1. 프로세스 주소 공간을 그려 보세요. 각 영역은 언제 생기나요?</summary>

낮은 주소부터: 접근 금지 영역(NULL 트랩) → 실행 파일 세그먼트(text+rodata `r-x`, RELRO `r--`, data+bss `rw-`) → 힙(brk) → 큰 구멍 → mmap 영역(공유 라이브러리, 큰 malloc, 스레드 스택, 파일 매핑) → 메인 스택(위에서 아래로). 실행 파일·라이브러리는 `execve` 때 로더가 매핑하고, bss 는 0 페이지로(파일에 바이트 없음), 힙은 첫 malloc 이 brk 를 올릴 때, 스레드 스택은 `pthread_create` 의 mmap 으로, 메인 스택은 커널이 exec 때 argv/envp 를 올려 둔 채로 만든다. 꼬리: "macOS 도 같나요?" → 큰 틀은 같지만 실측상 스택이 실행 파일과 dyld 공유 캐시 사이(0x16b…)에 있고, 힙도 brk 가 아니라 malloc zone 들(Tiny/Small 영역)이다. `[0, 4 GB)` 는 통째로 `__PAGEZERO`.

</details>

<details>
<summary>Q2. ASLR 은 무엇을 무작위로 하고, 무엇은 못 하나요?</summary>

각 **묶음의 시작점** 만 바꾼다: 실행 파일(PIE 일 때), 라이브러리/mmap 기준점, 힙, 스택. 묶음 안의 상대 거리는 그대로다 — 실측에서 main·g_data·g_bss 의 차이는 실행마다 같았다. 그래서 **포인터 하나가 새면** 그 묶음 전체 주소가 풀린다(정보 누출 + ROP 의 기본 전제, SEC-A04). 페이지 안 오프셋도 대부분 고정(macOS 16 KB, Linux 실행 파일 64 KB 단위)이라 하위 비트는 엔트로피가 없다. 그리고 macOS 공유 캐시는 부팅 동안 모든 프로세스에서 같아서, 한 프로세스에서 알아낸 libc 주소가 다른 프로세스에서도 통한다. 꼬리: "엔트로피는 몇 비트?" → 직접 잰 값으로 말한다: 이 맥 300 회에서 slide 287 종, 범위 ≈ 80 MB / 16 KB ≈ 5100 슬롯 ≈ 12 비트대. Linux 는 실행 파일 varbits 16..46 범위가 바뀌는 것을 봤다 (VA 비트 수·mmap_rnd_bits 에 좌우; 이 VM 에선 sysctl 읽기가 막혀 직접 확인 못 함).

</details>

<details>
<summary>Q3. PIE 가 아니면 어떻게 되나요? 임베디드에서는요?</summary>

PIE 가 아닌 실행 파일은 링크 주소에 고정 로드되므로 실행 파일 쪽 slide 가 없다(라이브러리·스택·힙은 여전히 무작위). Linux 배포판은 기본 PIE(`-pie`), macOS arm64 는 PIE 가 강제 — `-Wl,-no_pie` 가 "ignored for arm64" 로 무시되는 걸 실측했다. Cortex-M 펌웨어는 PIE 가 아니라 **절대 주소** 로 링크하고 MMU 가 없으니 ASLR 개념 자체가 없다. 대신 크래시 덤프의 PC/LR 을 그대로 `addr2line` 에 넣을 수 있다. 위치 독립 펌웨어(`-fropi/-frwpi`, 부트로더가 A/B 슬롯에 다르게 올리는 이미지)는 예외적으로 쓰인다.

</details>

<details>
<summary>Q4. NULL 역참조는 왜 세그폴트가 나죠? 항상 나나요?</summary>

주소 0 이 들어 있는 페이지가 매핑되어 있지 않아서 MMU 가 변환 실패(페이지 폴트) → 커널이 유효 영역이 아님을 보고 SIGSEGV. OSTEP 23장(VAX/VMS)이 "코드 세그먼트는 페이지 0 에서 시작하지 않는다" 로 같은 설계를 설명한다. Linux 는 `vm.mmap_min_addr`(이 VM 에서 32768 = 32 KB)로 낮은 주소 매핑을 막고, macOS 는 `__PAGEZERO` 로 4 GB 전체를 막는다 — 실측으로 `vm_region_of(0)`, `(0x1000)`, `(0xffffffff)` 모두 구멍. 4 GB 인 이유: 64 비트 포인터가 실수로 32 비트로 잘려도 반드시 폴트하게. **항상은 아니다**: `p->field` 의 오프셋이 크면(예: 거대한 구조체 멤버, `p[i]` 에서 큰 i) 0 근처를 넘어 매핑된 곳에 닿을 수 있고, Cortex-M 은 주소 0 이 flash 별칭이라 NULL 읽기가 벡터 테이블 값을 조용히 돌려준다 → MPU 로 0 번지 영역을 막는다(BM-A08).

</details>

<details>
<summary>Q5. /proc/pid/maps 의 r--p 줄은 왜 실행 파일마다 하나씩 있나요?</summary>

RELRO(Relocation Read-Only). 동적 링커가 GOT·`.data.rel.ro` 같은 재배치 대상을 고친 뒤 `mprotect` 로 읽기 전용으로 내린 부분이다. 그래서 그 페이지는 원래 RW LOAD 세그먼트의 앞부분이다(실측: offset `0xf000`, 다음 `rw-p` 가 `0x10000`). 공격자가 GOT 를 덮어 함수 포인터를 바꾸는 것을 막는다(full RELRO 는 `-z now` 로 지연 바인딩도 끈다, SEC-M09). macOS 의 `__DATA_CONST` 가 같은 역할 — vmmap 에서 `r--/rw-` (현재 r--, 최대 rw-) 로 보였다.

</details>

<details>
<summary>Q6. 스레드를 100 개 만들면 메모리가 800 MB 드나요?</summary>

가상 주소 공간은 그만큼(Linux 기본 8 MB + 가드 × 100) 예약되지만 물리 메모리는 **실제로 건드린 페이지만** 든다. 실측 vmmap: 메인 스택 VSIZE 8176K 중 RSDNT 32K. 그래도 32 비트 임베디드 리눅스(주소 공간 3 GB)에선 가상 공간이 먼저 바닥나고, `overcommit` 설정에 따라 예약 자체가 실패할 수 있어서 `pthread_attr_setstacksize` 로 줄이는 게 관례다. macOS 보조 스레드 기본은 512 KB(실측 vmmap 544K). RTOS 에서는 반대로 태스크 스택이 정적 배열이라 **물리 RAM 을 그대로** 먹는다 — 그래서 high-water mark 로 크기를 맞춘다(BM-A07, RTOS 스택 항목).

</details>

<details>
<summary>Q7. 크래시 로그의 주소를 소스 줄로 바꾸는 과정을 설명해 보세요.</summary>

① PC 가 속한 이미지와 그 로드 주소를 찾는다(macOS 크래시 리포트의 Binary Images / Linux 는 `/proc/pid/maps` 또는 코어 덤프의 NT_FILE). ② `link_addr = pc − (load − link_base)` 로 slide 를 뺀다(macOS 실행 파일 link_base = `__TEXT` vmaddr `0x100000000`, Linux PIE 는 0 이라 maps 의 첫 줄 시작을 빼면 파일 오프셋 기준 주소). ③ 디버그 정보가 있는 바이너리(dSYM, 언스트립 ELF)로 `atos`/`addr2line`. 실측: `atos -o demo_layout -l 0x104190000 0x1041905e8` → `main (in demo_layout) + 0`. 꼬리: "공유 라이브러리 안이면?" → 같은 계산을 그 라이브러리 load 로. macOS 시스템 라이브러리는 공유 캐시 안이라 캐시 기준. 상세는 TOOL-M14, 코어 덤프는 TOOL-M11, Cortex-M 은 ARM-M14.

</details>

## 9. 추가 연습

### 연습 1 — vmmap 한 줄 파서

macOS `vmmap` 의 한 줄에서 영역 이름(공백 포함 가능: `Malloc Small (empty)`, `Stack Guard`), start/end, 현재·최대 권한, 상세(없으면 `""`)를 뽑는 `parse_vmmap_line()` 을 써라. 데이터는 이 레슨의 vmmap 실측 줄.

<details>
<summary>답 보기</summary>

이름 끝을 "공백 개수" 로 정하면 `Malloc Small (empty)` 에서 깨진다. 대신 **단어 경계에서 `hex-hex` 모양 토큰이 처음 나오는 곳** 을 찾고 그 앞을 이름으로 자른다. 그다음 `]` 뒤 7 글자가 `prot/max`, `SM=XXX` 토큰 뒤가 상세. `shared memory ... SM=SHM  ` 처럼 상세가 비면 `""`.

```c
while (*p) {
    if ((p == line || p[-1] == ' ') && is_hex_range_at(p)) break;
    p++;
}
copy_trim(v->type, sizeof v->type, line, p);
```

</details>

### 연습 2 — 크래시 주소 되돌리기 (atos 가 하는 일)

이미지 표 `{name, load, size, link_base}` 와 PC 하나를 받아 어느 이미지인지와 링크 시점 주소를 돌려주는 `symbolize()` 를 써라. `pc = 0x104e685e8`, 실행 파일 load `0x104e68000`, `__TEXT` vmaddr `0x100000000` 이면?

<details>
<summary>답 보기</summary>

slide = `0x104e68000 − 0x100000000 = 0x4e68000`, link = `0x1000005e8` — `nm -m` 의 `_main` 주소와 같다. 범위 검사는 `pc - load < size` 로 (덧셈 오버플로 없이). `__TEXT` 끝(`load + size`)은 밖. libsystem_c 의 `&printf = 0x18765575c` 는 load `0x187628000` 기준 오프셋 `0x2d75c`.

</details>

### 연습 3 — maps 통계

Part A 샘플에서 종류별 바이트 합을 구하라. 실행 파일 몇 바이트? 힙 4 KB 페이지 몇 장? 스레드 스택(첫 익명 rw-p) 크기는? 메인 `[stack]` 은?

<details>
<summary>답 보기</summary>

실행 파일 `0x4000`(r-x 2 페이지 + r-- 1 + rw- 1), 힙 `0xaaaaaac00000 − 0xaaaaaac1000 = 0x13f000` = **319 페이지**(첫 brk 가 2 MB 경계까지 올린 결과), 스레드 스택 `0x810000` = 8 MB + 64 KB(가드 포함), 메인 스택 `0x21000` = 132 KB — 아직 자라지 않았다(필요하면 커널이 `RLIMIT_STACK` 까지 아래로 늘린다). 종류별 합 = 전체 합이 되는지도 CHECK 한다(분류 누락 탐지).

</details>

### 연습 4 — 가장 큰 구멍

이웃한 두 영역 사이 가장 큰 빈 공간과 그 시작을 구하라.

<details>
<summary>답 보기</summary>

`[heap]` 끝 `0xaaaaaac00000` 부터 스레드 스택 시작 `0xfffff75c0000` 까지, `0x5555_4c9c_0000` ≈ 85 TiB. 이 구멍이 힙(위로)과 mmap 영역(아래로)이 자랄 공간이자 ASLR 이 기준점을 흔들 공간이다. 맞닿은 두 영역(`end == start`)은 구멍 0.

</details>

### 연습 5 — Cortex-M 스택 덤프에서 리턴 주소 고르기

HardFault 때 저장한 스택 워드들 중 "리턴 주소처럼 보이는 값" 을 골라라: `.text` 범위 `[0x0800_0000, 0x0801_2000)` 안이고 bit0 = 1 (Thumb).

<details>
<summary>답 보기</summary>

`0x080004c9`, `0x08011ffd`, `0x0800a1b3` 세 개. `0x080004c8` 은 짝수(데이터일 가능성), `0x08012001` 은 범위 밖, `0xfffffff9` 는 EXC_RETURN 매직 값(주소 아님), `0x20001ff0` 은 SRAM(스택 포인터 값). ASLR 이 없으니 bit0 을 지운 값이 **그대로** `addr2line -e fw.elf` 입력이다 — 연습 2 의 slide 빼기 단계가 없다. 휴리스틱이라 오탐이 있다(상수 테이블 값 등) → 정확히는 언와인드 정보나 프레임 포인터로 (ARM-M14, TOOL-A05).

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice     # 22/22 passed
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pa && ./pa   # 22/22, 깨끗
```

## 10. 핵심 정리

- 프로세스 지도: 접근 금지(NULL) → 실행 파일(r-x / r-- RELRO / rw- data+bss) → 힙 → 큰 구멍 → mmap(라이브러리·큰 malloc·스레드 스택) → 메인 스택. macOS arm64 는 `[0, 4 GB)` `__PAGEZERO`, 스택이 실행 파일과 공유 캐시 사이.
- ASLR 은 **묶음의 시작점** 만 민다: 실행 파일 전체가 한 slide(macOS 16 KB 배수, Linux 64 KB 배수 실측), 섹션 간 거리·페이지 오프셋은 불변 → 포인터 하나 누출 = 묶음 전체 누출.
- macOS 실측: 공유 캐시(`&printf`)는 부팅 동안 모든 프로세스에서 고정, 메인 스택은 `상수 − slide`, 큰 malloc 은 `0x7…` Malloc Small. Linux: 스택은 페이지 안까지 무작위, `setarch -R` 로 프로세스별 끄기.
- 크래시 PC → 심볼: `pc − (load − link_base)` 후 `atos`/`addr2line`. Cortex-M 은 slide 0 이라 바로 `addr2line`.
- `/proc/pid/maps` 는 정렬·비겹침 → 이진 탐색. 반열린 구간, 13 자리 끝 주소, `---p` 먼저, 스레드 스택은 익명.
- 커널은 같은 속성의 연속 구간을 엔트리 하나로 관리 → `mprotect` 는 쪼개고 `munmap` 은 구멍. macOS 공유 캐시는 서브맵이라 `mach_vm_region_recurse`.
- 테스트는 주소 숫자가 아니라 **불변량**(권한·포함·차이·오프셋)을 검사한다.

## 참고자료

- OSTEP — 13장 *The Abstraction: Address Spaces* (코드·힙·스택 주소 출력 예, "모든 주소는 가상"), 14장 *Interlude: Memory API*, 18장 *Paging: Introduction*, 23장 *The VAX/VMS Virtual Memory System* (페이지 0 접근 금지, demand zeroing): <https://pages.cs.wisc.edu/~remzi/OSTEP/> (로컬: `operating-system-three-easy-pieces/book-md/C13_…`, Don 노트 `notes/2026-09-30_C13_address_spaces.md`)
- Linux man-pages — proc_pid_maps(5) (형식·`[heap]`/`[stack]`/`[vdso]`), setarch(8) `-R`, personality(2) `ADDR_NO_RANDOMIZE`: <https://man7.org/linux/man-pages/man5/proc_pid_maps.5.html>
- Linux 커널 문서 — `kernel.randomize_va_space`: <https://docs.kernel.org/admin-guide/sysctl/kernel.html> ; `arch_align_stack()` (arch/arm64/kernel/process.c)
- macOS — `man vmmap`, `man otool`, `man size`, `man atos`; Mach `mach_vm_region_recurse` (`<mach/mach_vm.h>`, XNU osfmk/vm); Apple *Threading Programming Guide* — Thread Management (보조 스레드 기본 스택 512 KB)
- Mach-O `__PAGEZERO`·`LC_SEGMENT_64`: `<mach-o/loader.h>` (SDK 헤더)
- glibc malloc 내부(top chunk, `sysmalloc`, `M_MMAP_THRESHOLD`): `mallopt(3)`, <https://sourceware.org/glibc/wiki/MallocInternals>
- 관련 레슨: C-B11 (섹션별 변수 배치·Cortex-M Reset_Handler), OS-B03 (vDSO), OS-B09 (mmap), OS-B10 (페이지 테이블), OS-M01 (다단 페이지 테이블·TLB), OS-M03 (COW), OS-M04/M05 (malloc), OS-A06 (mprotect+SIGSEGV), TOOL-M01 (링커 스크립트), TOOL-M11 (코어 덤프), TOOL-M14 (addr2line/atos), BM-B05 (MCU 메모리 맵), BM-A07 (스택 오버플로 탐지), BM-A08 (MPU NULL 트랩), ARCH-M15 (MPU 영역 계산), ARM-M14 (크래시 레지스터 덤프), SEC-M09 (PIE·RELRO), SEC-A04 (ASLR 우회와 완화)
