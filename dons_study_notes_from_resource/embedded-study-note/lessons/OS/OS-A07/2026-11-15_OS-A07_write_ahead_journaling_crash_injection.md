---
id: OS-A07
time: 120분
updated: 2026-11-15
---
# OS-A07 크래시 일관성: write-ahead 저널링 + 크래시 주입

## 학습 목표

- OS-M14 의 VSFS 를 축소해 그 위에 **write-ahead 저널**을 직접 붙인다: 트랜잭션 시작(TxB) → 로그 블록 → barrier → 트랜잭션 끝(TxE, tid + crc32) → **커밋 지점** → checkpoint(제자리 쓰기) → 저널 superblock 갱신(저널 비우기). 마운트 때 **redo replay** 로 복구한다.
- 블록 쓰기 **N 번째마다 전원을 끊는 전수(exhaustive) 크래시 주입**으로 "replay 후 fsck 0" 을 증명한다. 8 개 워크로드 × 4 개 모드 × 모든 N 을 돌린다.
- **data / ordered / writeback(비순서) 저널링**의 차이를 숫자로 말한다. ordered 는 메타데이터는 언제나 맞지만 제자리 덮어쓰기가 섞일 수 있다. writeback 은 fsck 가 통과하는데도 **지워진 다른 파일의 바이트**가 새 파일에 보인다. 쓰기 증폭(WA)도 모드마다 잰다.
- **디스크 쓰기 캐시의 재정렬**을 모델로 만든다. barrier(FLUSH)를 빼면 TxE 가 로그보다 먼저 매체에 닿아 쓰레기가 replay 된다. TxE checksum(ext4 `journal_checksum`)이 이것을 잡는다. 다만 ordered 데이터까지는 못 지킨다는 것을 재현하고, ext4 소스에서 같은 결론을 확인한다.
- Don 의 SSD 펌웨어 경험(FTL L2P 테이블 저널링, PLP, power-loss 테스트)과 1:1 로 연결한다.

## 1. 문제

"저널이 있는데 왜 fsck 가 필요 없나? 언제는 여전히 필요한가?" 이 질문은 스토리지·임베디드 리눅스·SSD 펌웨어 면접의 단골이다. 답하려면 **커밋 지점이 정확히 어느 쓰기인지**, 그리고 **그 전후 어디서 전원이 나가도 무엇이 남는지**를 말할 수 있어야 한다. OS-M14 연습 4 는 저널 없이 쓰기 순서만 잘 고르면 크래시 뒤에 "새는 것(leak)" 만 남는다는 것을 보였다. 그래도 그걸 회수하려면 디스크 전체를 훑는 fsck 가 필요했고, rename 처럼 블록 여러 개를 한 번에 바꿔야 하는 연산은 순서만으로 원자적으로 만들 수 없었다. 이 레슨은 OSTEP 42장(§42.3)의 저널을 직접 구현하고, **모든 쓰기 지점에서 전원을 끊어** 정말 그런지 검증한다.

핵심 세 줄:

1. **먼저 적고(log), 나중에 한다(checkpoint).** 바꿀 블록들의 새 버전을 저널에 다 쓰고, 맨 끝에 TxE 한 블록을 쓴다. TxE 가 매체에 닿는 순간이 **커밋**이다. 그 전에 끊기면 없던 일이 되고, 그 뒤에 끊기면 replay 가 마저 한다.
2. **순서가 곧 정확성이다.** 로그가 매체에 닿은 "뒤에" TxE 가 닿아야 한다. 디스크·SSD 의 휘발성 쓰기 캐시는 순서를 바꿀 수 있으므로 그 사이에 FLUSH(barrier)를 넣거나, TxE 에 로그 전체의 checksum 을 넣어 찢어진 트랜잭션을 알아본다.
3. **무엇을 저널에 넣느냐가 모드다.** data 는 데이터까지 두 번 쓴다(원자적, 비쌈). ordered 는 데이터를 커밋 "전에" 제자리에 쓴다(메타데이터만 저널). writeback 은 데이터 순서를 지키지 않는다(빠름, 옛 데이터 노출).

```svg
<svg viewBox="0 0 740 330" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">디스크 배치 (1 KB 블록 256 개) 와 트랜잭션 한 개의 쓰기 순서</text>
  <rect x="10" y="34" width="40" height="30" class="f-mut" stroke="currentColor"/><text x="18" y="54" fill="currentColor">sb</text>
  <rect x="50" y="34" width="40" height="30" class="f-acc" stroke="currentColor"/><text x="58" y="54" fill="currentColor">js</text>
  <rect x="90" y="34" width="260" height="30" class="f-acc" stroke="currentColor"/><text x="100" y="54" fill="currentColor">저널 J0..J63 (원형 로그)</text>
  <rect x="350" y="34" width="50" height="30" class="f-mut" stroke="currentColor"/><text x="358" y="54" fill="currentColor">ibm</text>
  <rect x="400" y="34" width="50" height="30" class="f-mut" stroke="currentColor"/><text x="408" y="54" fill="currentColor">dbm</text>
  <rect x="450" y="34" width="90" height="30" class="f-mut" stroke="currentColor"/><text x="458" y="54" fill="currentColor">inode I0..I3</text>
  <rect x="540" y="34" width="190" height="30" class="f-ok" stroke="currentColor"/><text x="550" y="54" fill="currentColor">데이터 D0..D183</text>
  <text x="10" y="92" fill="currentColor" font-family="monospace">create("/a") trace: W J0 W J1 W J2 W J3 F W J4 F W ibm W I0 W D0 F W js F</text>
  <rect x="10" y="108" width="80" height="34" class="f-acc" stroke="currentColor"/><text x="18" y="130" fill="currentColor">TxB tid=1</text>
  <rect x="94" y="108" width="60" height="34" class="f-acc" stroke="currentColor"/><text x="102" y="130" fill="currentColor">ibm'</text>
  <rect x="158" y="108" width="60" height="34" class="f-acc" stroke="currentColor"/><text x="166" y="130" fill="currentColor">I0'</text>
  <rect x="222" y="108" width="60" height="34" class="f-acc" stroke="currentColor"/><text x="230" y="130" fill="currentColor">D0'</text>
  <line x1="292" y1="102" x2="292" y2="150" stroke="currentColor" stroke-width="3"/><text x="280" y="166" fill="currentColor">F</text>
  <rect x="302" y="108" width="90" height="34" class="f-warn" stroke="currentColor"/><text x="308" y="130" fill="currentColor">TxE tid,crc</text>
  <line x1="402" y1="102" x2="402" y2="150" stroke="currentColor" stroke-width="3"/><text x="390" y="166" fill="currentColor">F</text>
  <rect x="412" y="108" width="150" height="34" class="f-ok" stroke="currentColor"/><text x="420" y="130" fill="currentColor">checkpoint: ibm I0 D0</text>
  <line x1="572" y1="102" x2="572" y2="150" stroke="currentColor" stroke-width="3"/><text x="560" y="166" fill="currentColor">F</text>
  <rect x="582" y="108" width="100" height="34" class="f-mut" stroke="currentColor"/><text x="588" y="130" fill="currentColor">js: tail=5</text>
  <line x1="692" y1="102" x2="692" y2="150" stroke="currentColor" stroke-width="3"/><text x="680" y="166" fill="currentColor">F</text>
  <text x="10" y="196" fill="currentColor">① 로그 쓰기 (TxB + 새 버전들)   ② 커밋 = TxE 가 매체에 닿음   ③ checkpoint   ④ free: 저널 비움</text>
  <rect x="10" y="212" width="340" height="104" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="20" y="234" fill="currentColor">TxE 전에 전원 차단 (N ≤ 4)</text>
  <text x="20" y="256" fill="currentColor">replay 할 것이 없다 → create 는 없던 일</text>
  <text x="20" y="278" fill="currentColor">제자리 블록은 하나도 안 바뀌었다</text>
  <text x="20" y="300" fill="currentColor">→ fsck 0, 상태 = 이전 상태</text>
  <rect x="380" y="212" width="350" height="104" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="390" y="234" fill="currentColor">TxE 뒤에 전원 차단 (N ≥ 5)</text>
  <text x="390" y="256" fill="currentColor">마운트 → js 의 tail 부터 TxB·TxE 확인</text>
  <text x="390" y="278" fill="currentColor">→ 로그 사본을 제자리에 다시 쓴다 (멱등)</text>
  <text x="390" y="300" fill="currentColor">→ fsck 0, 상태 = 새 상태</text>
</svg>
```

### Part A — 저널: commit · checkpoint · recover

메모리 블록 배열 디스크(`bread/bwrite/bflush`, 횟수·trace 기록) 위에 OS-M14 VSFS 축약판을 둔다. 크기만 줄였다: 1 KB 블록, inode 64 B, direct 6 + 1단 indirect, dirent 32 B. 연산(create, mkdir, write, unlink, rename)은 메타데이터를 `mput` 으로, 파일 데이터를 `dput` 으로 쓴다. 바깥 연산이 끝나면 그동안 바뀐 블록들(같은 블록은 **흡수(absorption)**)을 트랜잭션 하나로 커밋한다. 구현할 것은 셋이다: `jnl_commit`(TxB → 로그 → barrier → TxE(tid, crc32) → flush), `jnl_checkpoint`(제자리 → flush → js(tail_tid, tail_off) → flush), `jnl_recover`(js 의 tail 부터 커밋된 트랜잭션만 replay, wrap 처리, 찢어진 꼬리에서 멈추고 그 tid 는 건너뜀).

### Part B — 모드별 데이터 경로 + 크래시 전수 스윕

`dput` 을 모드별로 구현한다: none(바로 씀) / data(로그) / ordered(커밋 **전에** 제자리) / writeback(커밋 **뒤에** 제자리). 다음으로 `sweep(workload)` 를 만든다. 크래시 없는 기준 실행으로 연산 k 개 뒤의 논리 상태 요약 `s_k`(메타데이터 해시, 내용 포함 해시)와 총 쓰기 수 W 를 얻는다. 그다음 **N = 0..W 모두**에 대해 "쓰기 #N 직전에 전원 차단 → 재부팅 → 마운트(replay) → fsck → 상태 비교" 를 돌린다. 크래시가 op j 도중이면 허용 상태는 `{s_{j-1}, s_j}` 다(old-or-new). 워크로드는 create+write, indirect 로 넘어가는 append, unlink, mkdir+create, **rename-over**(설정 파일 교체, OS-B13), reuse(지운 블록 재사용), 제자리 overwrite, 저널 wrap 이다. 끝으로 모드별 **쓰기 증폭** 표를 만든다.

### Part C — 쓰기 캐시 재정렬: barrier 와 checksum

장치에 휘발성 쓰기 캐시(창 4 개, FIFO 축출)를 켠다. 전원이 나가는 순간 캐시에 남은 쓰기들 중 **아무 부분집합**만 매체에 남을 수 있다. 이것을 모든 N × 모든 부분집합(mask)으로 돈다. 세 가지 변형을 비교한다. (1) TxE 전 barrier 있음 (2) barrier 없음 + checksum 없음 (3) barrier 없음 + checksum(= ext4 `journal_async_commit` 의 아이디어). TODO 는 `wc_crash(mask)`(전원 차단 순간의 캐시 처리)와 `crc32_update` 다. SSD 의 volatile write cache, FLUSH/FUA, PLP 커패시터와 연결한다.

## 2. Problem (English)

Add write-ahead journaling to a tiny VSFS and prove crash consistency by exhaustive crash injection.

- Block device: `bread(b, buf)`, `bwrite(b, buf)`, `bflush()`. Setting `crash_at = N` simulates power loss between write #N−1 and write #N. Write #N and later never reach the media, and neither does a flush issued in between. With the volatile write cache on, the device holds up to 4 unflushed writes. At power loss an arbitrary subset `mask` of them survives: `void wc_crash(unsigned mask)`.
- Journal format: `TxB{magic, tid, n, blk[n]}`, n logged block images, `TxE{magic, tid, n, crc32(TxB‖images)}`. The journal superblock `js{tail_tid, tail_off, csum}` marks where the live log starts.
- Implement `int jnl_commit(void)`, `void jnl_checkpoint(void)`, `int jnl_recover(void)` (returns the number of replayed transactions), `int dput(uint32_t blk, const void *buf)` and `void wb_writeout(void)` for modes `none/data/ordered/writeback`, `uint32_t crc32_update(uint32_t crc, const void *p, size_t n)` (zlib-compatible, chainable), and `int sweep(const workload_t *w, sweep_t *r)`.
- `sweep` must, for every N in [0, W] (and every cache subset when the cache is on), restore the pre-workload image, run the workload with the crash armed, cut power, remount (replay), run fsck, and classify the logical state against the crash-free states `s_{j-1}`, `s_j`. It counts `fsck_bad`, `meta_bad`, `data_bad` (with the offending N values) and `lost` (an op committed before the crash is missing).
- Required results: no journal → fsck finds problems for most workloads, and rename-over is non-atomic. Data journaling → 0 of everything. Ordered → fsck 0 and metadata always old-or-new, only in-place overwrite mixes content. Writeback → fsck 0, but a reused block exposes another file's old bytes. Write cache without a barrier and without a checksum → corruption. With a checksum → the torn transaction is skipped.
- Constraints: C11, no heap allocation in the engine, deterministic (no timing in checks), whole run < 3 s.

Example (trace of `create("/a")` in ordered mode, F = flush):
`W J0 W J1 W J2 W J3 F W J4 F W ibm W I0 W D0 F W js F`

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o /tmp/OS-A07_st && /tmp/OS-A07_st    # 17/43 passed, exit 1, 0.1 s
```

주어진 것: 블록 장치(쓰기 캐시와 전원 차단 모델 포함), 저널 메모리 상태(열린 트랜잭션 `J.tx_*`, 커밋됐지만 체크포인트 전인 `J.ck_*`, writeback 의 미룬 데이터 `J.wb_*`), 읽기 경로 `rd()`(열린 tx → 미룬 데이터 → 체크포인트 대기 → 디스크), `op_enter/op_exit`(바깥 연산이 끝날 때 wrap 확인 → 커밋 → writeback 데이터 → checkpoint → 마지막 flush), VSFS 축약판 전체, fsck(검사만, M14 와 같은 이름: bad-ptr, dup-block, dangling-dirent, orphan-inode, nlink, inode-bitmap, leaked-block, unmarked-block), 논리 상태 요약 `digest()`(경로·종류·크기·nlink 와 내용의 순서 무관 해시), 워크로드 8 개, `prepare/restore_and_mount/clean_run`.

TODO 는 8 개다: **A1** `jnl_commit`, **A2** `jnl_checkpoint`, **A3** `jnl_recover`, **B1a** `dput`, **B1b** `wb_writeout`, **B2** `sweep`, **C1** `crc32_update`, **C2** `wc_crash`. 시작 코드의 `jnl_commit` 은 저널 없이 바로 제자리에 쓴다. 그래서 크래시가 없으면 파일 시스템은 멀쩡히 돈다. 하지만 저널 trace·복구·스윕·쓰기 증폭 검사는 FAIL 한다(17/43, 멈춤 없음).

## 4. 힌트

<details><summary>힌트 1 — 커밋 지점은 정확히 한 블록이다</summary>

TxE 블록 하나가 매체에 닿는 순간이 커밋이다. 그러므로 TxE 는 원자적으로 써지는 단위 안에 있어야 한다. OSTEP §42.3 은 디스크가 512 B 섹터 쓰기를 원자적으로 보장한다고 가정하므로 TxE 를 한 섹터로 만들라고 한다. 코드에 `_Static_assert(sizeof(txe_t) <= 512)` 가 있는 이유다. TxE 앞의 flush 는 "로그가 다 닿았다" 를 보장한다. TxE 뒤의 flush 는 "커밋이 닿았다" 를 보장하고, 그래야 checkpoint 로 제자리를 덮어도 된다.
</details>

<details><summary>힌트 2 — 복구는 "다음 tid" 를 기대하며 걷는다</summary>

js 의 `(tail_tid, tail_off)` 부터 시작한다. 그 자리에 `magic == TXB && tid == 기대값` 인 블록이 없으면 끝(또는 wrap: 0 에서 한 번 더)이다. 저널 자리는 재사용되므로 옛 TxB·TxE 가 그대로 남아 있다. **tid 가 단조 증가**해야 옛 것을 새 것으로 착각하지 않는다. 찢어진 꼬리(TxB 는 있는데 TxE 가 없거나 crc 가 틀림)를 만났다면 그 tid 는 건너뛰어라(`next_tid = tid + 1`). 다음 트랜잭션이 같은 자리에 같은 tid 로 써지다 또 끊기면 옛 TxE 와 짝이 맞아 보일 수 있기 때문이다.
</details>

<details><summary>힌트 3 — replay 는 멱등이어야 한다</summary>

물리 로깅(physical logging, 블록 이미지 통째)이라 "같은 이미지를 같은 자리에 다시 쓰기" 는 몇 번을 해도 결과가 같다. 체크포인트 도중에 끊겨도, 복구 도중에 끊겨도(연습 3) 다시 마운트하면 처음부터 다시 replay 하면 된다. 논리 로깅("블록 7 을 파일 X 에 붙여라")이면 두 번 적용하면 틀린다.
</details>

<details><summary>힌트 4 — ordered 의 forget</summary>

ordered/writeback 에서 데이터 블록 b 를 쓸 때, b 의 **옛 메타데이터 버전**(예: 방금 해제된 indirect 블록)이 메모리의 체크포인트 대기 목록에 남아 있을 수 있다. 그러면 나중 checkpoint 가 그 옛 버전으로 새 데이터를 덮는다. `forget(b)` 로 지워라. 크래시가 없는데도 깨지는 버그라 테스트에 바로 걸린다. 크래시가 끼면 생기는 문제는 연습 4(revoke)에서 다룬다.
</details>

<details><summary>힌트 5 — 스윕의 허용 상태</summary>

`run_ops` 는 전원이 나갈 때 진행 중이던 op 번호 j 를 돌려준다(안 나갔으면 nops+1). op 마다 커밋 + flush 를 하므로 j−1 개까지는 영속(durable)이다. 그래서 허용 상태는 `{s_{j-1}, s_j}` 다. 더 앞의 상태와 같으면 "잃어버림(lost)", 어느 것과도 다르면 위반이다. 메타데이터 해시와 내용 해시를 따로 비교해야 "fsck·메타는 맞는데 내용만 틀림"(writeback)을 구별할 수 있다.
</details>

<details><summary>힌트 6 — 쓰기 캐시 부분집합의 중복 제거</summary>

mask 는 0..2^창−1 을 돌지만, 전원이 나간 순간 캐시에 p 개만 있었다면 `mask ≥ 1<<p` 는 앞의 mask 와 결과가 같다. `power_cut()` 뒤의 `D.pend_at_crash` 로 걸러라. 그래야 `runs` 가 "서로 다른 크래시 결과의 수" 가 된다.
</details>

## 5. 풀이

### Part A — 커밋: 실제 trace 로 읽기

`J.tx_*` 는 블록 번호별로 하나씩만 갖는다. create 는 inode 블록 I0 를 두 번 바꾸지만(새 inode, 그리고 부모 디렉터리 size) 로그에는 한 번만 들어간다. 이것이 흡수다. 실제 출력:

```text
  create /a (ordered): W J0 W J1 W J2 W J3 F W J4 F W ibm W I0 W D0 F W js F
  write 3000 B (ordered): W D1 W D2 W D3 W J5 W J6 W J7 F W J8 F W dbm W I0 F W js F
  append 1000 B (data):  W J9 W J10 W J11 W J12 W J13 F W J14 F W D3 W dbm W D4 W I0 F W js F
  append 1100 B (wb):    W J15 W J16 W J17 F W J18 F W D4 W D5 W dbm W I0 F W js F
```

| 연산(모드) | 로그 블록 n | 이 연산의 쓰기 | 데이터는 언제 | 핵심 |
|---|---|---|---|---|
| create (ordered) | 3: ibm, I0, D0 | 10 | 없음 | I0 두 번 → 한 번 (흡수) |
| write 3000 B (ordered) | 2: dbm, I0 | 3 + 9 = 12 | **TxB 전에** 제자리 D1..D3 | 데이터가 먼저 → 커밋된 포인터는 늘 새 데이터를 가리킴 |
| append 1000 B (data) | 4: D3, dbm, D4, I0 | 2×4 + 4 = 12 | 로그 안 (두 번 씀) | 데이터도 원자적 |
| append 1100 B (wb) | 2: dbm, I0 | 2 + 8 = 10 | **TxE 뒤에** D4, D5 | 커밋과 데이터 사이에 창 |

`jnl_commit` 의 뼈대:

```c
bwrite(JSTART + J.head, TxB);                     /* ① tid, n, 블록 목록 */
for (i...) bwrite(JSTART + J.head + 1 + i, img[i]);
if (J.barrier) bflush();                           /* ② 로그가 다 닿은 뒤에야 */
bwrite(JSTART + J.head + 1 + n, TxE);              /* ③ tid, n, crc32(TxB‖img) */
bflush();                                          /* ④ 커밋 지점 — 이 뒤로는 replay 가 책임진다 */
```

### Part A — 복구: 끊긴 자리별 결과

create /b 를 하다가 쓰기 #N 직전에 전원을 끊는다. 쓰기 순서는 TxB(#0), 로그 3개(#1–#3), TxE(#4), 제자리 3개(#5–#7), js(#8)다.

```text
  crash at write #5 of create /b → replayed 1, /b exists, fsck 0
  crash at write #4 of create /b → replayed 0, /b absent, fsck 0
  crash at write #0 of create /b → replayed 0, /b absent, fsck 0
  crash at write #7 of create /b → replayed 1, /b exists, fsck 0
```

#4(TxE)를 잃으면 없던 일이 된다. #5 이후를 잃으면 replay 가 마저 한다. #7 은 ibm 과 I0 는 이미 제자리에 있고 D0 만 빠진 상태다. replay 가 셋 다 다시 써도 결과는 같다(멱등). 복구 직후 다시 마운트하면 replay 0, 쓰기 0 이다(테스트). 커밋된 트랜잭션의 로그 사본 1 바이트를 뒤집으면 crc 가 맞지 않아 그 트랜잭션은 버려지고(`skipped+1`, `/c` 없음, fsck 0), `next_tid` 는 그 tid 를 건너뛴다. 13 번 create 로 저널이 한 바퀴 돌아도(head 60 → 0) 마운트 후 fsck 0 이다.

복구 알고리즘 (`jnl_recover`):

1. js 를 읽는다. `tid = tail_tid`, `off = tail_off`.
2. `off`(아니면 wrap 으로 0)에 `TxB(tid)` 가 있나? 없으면 끝.
3. 로그 n 개와 TxE 를 읽는다. TxE 의 magic/tid/n 이 맞고, csum 이 켜져 있으면 crc 도 맞나? 블록 번호가 저널·superblock 을 가리키지 않나?
4. 맞으면 제자리에 쓰고 `off += n+2`, `tid++` 한 뒤 2 로 간다. 틀리면 찢어진 꼬리이니 멈추고 `torn = 1`.
5. 무언가 했으면 flush → `js = (tid + torn, 0)` → flush. 아무것도 없었으면 쓰지 않고 이어 쓴다.

Linux JBD2 는 같은 일을 세 번에 나눠 한다(`fs/jbd2/recovery.c:306-310`, Linux 6.18.55). `PASS_SCAN`(끝 찾기) → `PASS_REVOKE`(revoke 레코드 모으기) → `PASS_REPLAY`(쓰기). 블록 종류는 `include/linux/jbd2.h:122-126`(DESCRIPTOR=1, COMMIT=2, REVOKE=5)에 있다. 우리 TxB = descriptor, TxE = commit block 이다. 연습 4 가 REVOKE 가 왜 필요한지 보인다.

### Part B — 세 모드의 쓰기 순서

```svg
<svg viewBox="0 0 740 300" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">append(파일에 블록 Db 추가) — 모드별 쓰기 순서와 "커밋" 위치</text>
  <text x="10" y="56" fill="currentColor">data</text>
  <rect x="100" y="38" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="110" y="57" fill="currentColor">TxB</text>
  <rect x="164" y="38" width="110" height="28" class="f-acc" stroke="currentColor"/><text x="172" y="57" fill="currentColor">dbm' I' Db'</text>
  <rect x="284" y="38" width="60" height="28" class="f-warn" stroke="currentColor"/><text x="294" y="57" fill="currentColor">TxE</text>
  <rect x="354" y="38" width="150" height="28" class="f-ok" stroke="currentColor"/><text x="362" y="57" fill="currentColor">ckpt dbm I Db</text>
  <text x="520" y="57" fill="currentColor">Db 를 두 번 씀 · 원자적</text>
  <text x="10" y="116" fill="currentColor">ordered</text>
  <rect x="100" y="98" width="60" height="28" class="f-ok" stroke="currentColor"/><text x="112" y="117" fill="currentColor">Db</text>
  <rect x="164" y="98" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="174" y="117" fill="currentColor">TxB</text>
  <rect x="228" y="98" width="70" height="28" class="f-acc" stroke="currentColor"/><text x="236" y="117" fill="currentColor">dbm' I'</text>
  <rect x="302" y="98" width="60" height="28" class="f-warn" stroke="currentColor"/><text x="312" y="117" fill="currentColor">TxE</text>
  <rect x="372" y="98" width="110" height="28" class="f-ok" stroke="currentColor"/><text x="380" y="117" fill="currentColor">ckpt dbm I</text>
  <text x="500" y="117" fill="currentColor">포인터는 늘 새 Db 를 가리킴</text>
  <text x="10" y="176" fill="currentColor">writeback</text>
  <rect x="100" y="158" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="110" y="177" fill="currentColor">TxB</text>
  <rect x="164" y="158" width="70" height="28" class="f-acc" stroke="currentColor"/><text x="172" y="177" fill="currentColor">dbm' I'</text>
  <rect x="238" y="158" width="60" height="28" class="f-warn" stroke="currentColor"/><text x="248" y="177" fill="currentColor">TxE</text>
  <rect x="308" y="158" width="60" height="28" class="f-warn" stroke="currentColor"/><text x="320" y="177" fill="currentColor">Db</text>
  <rect x="372" y="158" width="110" height="28" class="f-ok" stroke="currentColor"/><text x="380" y="177" fill="currentColor">ckpt dbm I</text>
  <path d="M300 196 L300 214 L366 214 L366 196" stroke="currentColor" fill="none"/>
  <text x="250" y="232" fill="currentColor">이 사이에서 끊기면: 커밋된 I 는 Db 를 가리키는데 Db 는 옛 내용</text>
  <text x="250" y="250" fill="currentColor">(새 블록이면 0, 재사용 블록이면 지워진 다른 파일의 바이트)</text>
  <text x="10" y="284" fill="currentColor">메타데이터 규칙은 셋 다 같다 → fsck 는 셋 다 0. 차이는 데이터가 닿는 시점뿐.</text>
</svg>
```

이 세 이름은 Linux ext3/ext4 의 `data=journal | ordered | writeback` 마운트 옵션 이름과 같다. 공식 문서 `Documentation/admin-guide/ext4.rst:172-183`(6.18.55)의 정의는 다음과 같다. data=journal 은 "All data are committed into the journal prior to being written into the main file system". data=ordered(기본값 표시 `(*)`)는 "All data are forced directly out to the main file system prior to its metadata being committed to the journal". data=writeback 은 "data may be written into the main file system after its metadata has been committed". 같은 문서 62–65 행은 writeback 이 크래시 뒤 "stale data exposed in recently written files … could be a security exposure" 를 남길 수 있다고 경고한다. 우리 스윕이 그 노출을 그대로 재현한다.

### Part B — 전수 스윕: 실제 결과

모든 워크로드 × 모든 모드 × 모든 N (총 W+1 번씩). 실제 출력:

```text
  create+write      none      W= 11 runs=  12 fsck_bad=  9 meta_bad=  0 data_bad=  0 lost=0
  append->indirect  none      W= 16 runs=  17 fsck_bad= 10 meta_bad=  0 data_bad=  0 lost=0
  unlink            none      W=  4 runs=   5 fsck_bad=  3 meta_bad=  0 data_bad=  0 lost=0
  mkdir+create      none      W= 10 runs=  11 fsck_bad=  8 meta_bad=  0 data_bad=  0 lost=0
  rename-over       none      W=  5 runs=   6 fsck_bad=  4 meta_bad=  4 data_bad=  4 lost=0  N=1,2,3,4
  reuse             none      W= 12 runs=  13 fsck_bad=  9 meta_bad=  0 data_bad=  0 lost=0
  overwrite         none      W=  3 runs=   4 fsck_bad=  0 meta_bad=  0 data_bad=  2 lost=0  N=1,2
  wrap              none      W=  9 runs=  10 fsck_bad=  7 meta_bad=  0 data_bad=  0 lost=0
  create+write      data      W= 22 runs=  23 fsck_bad=  0 meta_bad=  0 data_bad=  0 lost=0
  ... (data: 8 개 워크로드 모두 0 0 0)
  create+write      ordered   W= 19 runs=  20 fsck_bad=  0 meta_bad=  0 data_bad=  0 lost=0
  append->indirect  ordered   W= 24 runs=  25 fsck_bad=  0 meta_bad=  0 data_bad=  0 lost=0
  reuse             ordered   W= 29 runs=  30 fsck_bad=  0 meta_bad=  0 data_bad=  0 lost=0
  overwrite         ordered   W=  3 runs=   4 fsck_bad=  0 meta_bad=  0 data_bad=  2 lost=0  N=1,2
  ... (ordered: 나머지 0)
  create+write      writeback W= 19 runs=  20 fsck_bad=  0 meta_bad=  0 data_bad=  3 lost=0  N=13,14,15
  append->indirect  writeback W= 24 runs=  25 fsck_bad=  0 meta_bad=  0 data_bad=  6 lost=0  N=5,6,7,8,18,19
  reuse             writeback W= 29 runs=  30 fsck_bad=  0 meta_bad=  0 data_bad=  2 lost=0  N=24,25
  overwrite         writeback W=  3 runs=   4 fsck_bad=  0 meta_bad=  0 data_bad=  2 lost=0  N=1,2
  wrap              writeback W= 18 runs=  19 fsck_bad=  0 meta_bad=  0 data_bad=  2 lost=0  N=13,14
  writeback reuse, crash at N=24: fsck=0, /b size=2048, /b == old /a bytes? YES (stale data exposure)
```

| 모드 | fsck 0? | 메타 old-or-new? | 내용 old-or-new? | 해석 |
|---|---|---|---|---|
| none | ✗ (7/8 워크로드) | rename-over 만 ✗ | rename, overwrite ✗ | M14 의 쓰기 순서 덕에 대부분 **누수**만 남지만 회수하려면 전체 fsck. rename 은 순서로 못 구함 |
| data | ✓ | ✓ | ✓ | 전부 원자적 |
| ordered | ✓ | ✓ | overwrite 만 ✗ | 제자리 덮어쓰기(블록 3개)가 커밋과 무관하게 일부만 닿음 = ext4 ordered 도 같은 의미 |
| writeback | ✓ | ✓ | create+write, append, reuse, overwrite, wrap ✗ | **fsck 는 통과하는데 내용이 틀림**. reuse 는 지워진 /a 의 바이트가 /b 에 그대로 |

`demo_crash_table.c` 로 create+write 를 N 마다 끊어 본 표가 아래다(실제 출력에서 발췌). `op` 는 전원이 나갈 때 진행 중이던 연산, `s0/s1/s2` 는 연산 0/1/2 개 뒤의 상태다.

<!-- code: demo_crash_table.c -->

```text
== mode none: W=11
   writes: W ibm W I0 W D1 W I0 W dbm W D2 W dbm W D3 W dbm W D4 W I0
   N= 1 lose=I0   op=1 replay=0 fsck=1 inode-bitmap=1  -> s0
   N= 2 lose=D1   op=1 replay=0 fsck=1 orphan-inode=1  -> s0
   N= 5 lose=D2   op=2 replay=0 fsck=1 leaked-block=1  -> s1
   N=10 lose=I0   op=2 replay=0 fsck=3 leaked-block=3  -> s1
== mode ordered: W=19
   writes: W J7 W J8 W J9 W J10 F W J11 F W ibm W I0 W D1 F W js F W D2 W D3 W D4 W J12 W J13 W J14 F W J15 F W dbm W I0 F W js F
   N= 4 lose=J11  op=1 replay=0 fsck=0  -> s0
   N= 5 lose=ibm  op=1 replay=1 fsck=0  -> s1
   N=15 lose=J15  op=2 replay=0 fsck=0  -> s1
   N=16 lose=dbm  op=2 replay=1 fsck=0  -> s2
== mode writeback: W=19
   N=12 lose=J15  op=2 replay=0 fsck=0  -> s1
   N=13 lose=D2   op=2 replay=1 fsck=0  -> DATA!
   N=15 lose=D4   op=2 replay=1 fsck=0  -> DATA!
   N=16 lose=dbm  op=2 replay=1 fsck=0  -> s2
```

ordered 의 N=9 는 `lose=D2` 인데 `op=1` 로 나온다. crash_at=9 는 "#8 과 #9 사이" 라서 op 1 의 마지막 flush 도중에 끊긴 것이기 때문이다. 이 모델에서는 사이에 낀 flush 도 완료되지 않는다(Part C 에서 중요하다).

### Part B — 쓰기 증폭 (같은 워크로드, 장치 쓰기 / 사용자 데이터 블록)

```text
  mode       ckpt  | seq 16 KB (16 user blk)        | 8 small files (8 user blk)
  none       each  | writes  51  WA  3.19  flush  1 | writes  56  WA  7.00  flush  1
  data       each  | writes  75  WA  4.69  flush 21 | writes 144  WA 18.00  flush 65
  data       lazy  | writes  62  WA  3.88  flush 13 | writes  98  WA 12.25  flush 37
  ordered    each  | writes  59  WA  3.69  flush 21 | writes 136  WA 17.00  flush 65
  ordered    lazy  | writes  46  WA  2.88  flush 13 | writes  90  WA 11.25  flush 37
  writeback  each  | writes  59  WA  3.69  flush 21 | writes 136  WA 17.00  flush 65
  writeback  lazy  | writes  46  WA  2.88  flush 17 | writes  90  WA 11.25  flush 45
```

읽는 법:

- **data > ordered = writeback.** 차이는 데이터 블록을 로그에 한 번 더 쓰는 것뿐이다(16 KB 에서 75 vs 59). ordered 와 writeback 은 쓰는 블록 집합이 같고 순서만 다르다(쓰기 수 동일, 검사함).
- **작은 연산은 저널 고정비가 지배한다.** 연산마다 TxB + TxE + js + flush 4 번이 붙는다. 1 KB 파일 8 개면 WA 17–18 이다. 실제 JBD2 는 여러 연산을 한 트랜잭션에 모아(`commit=nrsec` 옵션, 기본 5 초: ext4.rst:185-187) 이 고정비를 나눈다. 연습 2 의 그룹 커밋이 그 효과다.
- **lazy checkpoint** 는 저널이 찰 때만 체크포인트한다. 같은 inode·bitmap 블록이 여러 트랜잭션에 걸쳐 한 번만 제자리에 써지고, js 갱신도 줄어든다. ordered-lazy(46)가 저널 없는 none(51)보다도 적다. 이 축약 VSFS 는 none 모드에서 블록 하나를 할당할 때마다 bitmap 을 바로 쓰기 때문이다. 흡수(absorption)가 저널의 숨은 이득이다.
- 이 숫자는 이 모델의 블록 수다. 실제 디스크의 시간 비용(순차 저널 쓰기 vs 랜덤 제자리 쓰기, flush 지연)은 반영하지 않았다.

### Part C — 쓰기 캐시가 순서를 바꾸면

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">휘발성 쓰기 캐시 (창 4) — barrier 없이 TxE 를 보냈을 때</text>
  <text x="10" y="52" fill="currentColor">호스트 순서</text>
  <rect x="110" y="34" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="120" y="53" fill="currentColor">TxB</text>
  <rect x="174" y="34" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="184" y="53" fill="currentColor">ibm'</text>
  <rect x="238" y="34" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="250" y="53" fill="currentColor">I0'</text>
  <rect x="302" y="34" width="60" height="28" class="f-acc" stroke="currentColor"/><text x="312" y="53" fill="currentColor">D0'</text>
  <rect x="366" y="34" width="60" height="28" class="f-warn" stroke="currentColor"/><text x="376" y="53" fill="currentColor">TxE</text>
  <text x="440" y="53" fill="currentColor">(F 없음) → 전원 차단</text>
  <text x="10" y="102" fill="currentColor">캐시 [4 칸]</text>
  <rect x="110" y="84" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="120" y="103" fill="currentColor">ibm'</text>
  <rect x="174" y="84" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="186" y="103" fill="currentColor">I0'</text>
  <rect x="238" y="84" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="248" y="103" fill="currentColor">D0'</text>
  <rect x="302" y="84" width="60" height="28" class="f-mut" stroke="currentColor"/><text x="312" y="103" fill="currentColor">TxE</text>
  <text x="380" y="103" fill="currentColor">TxB 는 이미 축출 = 매체에 있음</text>
  <text x="10" y="152" fill="currentColor">살아남음 mask</text>
  <text x="110" y="152" fill="currentColor">= {TxE, ibm'}  (I0', D0' 는 사라짐)</text>
  <rect x="10" y="172" width="355" height="100" rx="4" class="f-warn" stroke="currentColor"/>
  <text x="20" y="194" fill="currentColor">checksum 없음:</text>
  <text x="20" y="216" fill="currentColor">TxB(tid) + TxE(tid) 짝이 맞음 → "커밋됨"</text>
  <text x="20" y="238" fill="currentColor">J2·J3 의 옛 내용(이전 트랜잭션 사본)을</text>
  <text x="20" y="260" fill="currentColor">I0·D0 에 replay → fsck 오류 / 엉뚱한 상태</text>
  <rect x="380" y="172" width="350" height="100" rx="4" class="f-ok" stroke="currentColor"/>
  <text x="390" y="194" fill="currentColor">TxE 에 crc32(TxB‖로그) 가 있으면:</text>
  <text x="390" y="216" fill="currentColor">replay 전에 다시 계산 → 불일치</text>
  <text x="390" y="238" fill="currentColor">→ 찢어진 트랜잭션으로 보고 버림</text>
  <text x="390" y="260" fill="currentColor">→ 없던 일 (원자성 유지)</text>
</svg>
```

모든 N × 모든 살아남는 부분집합. 실제 출력:

```text
  barrier, no csum     data     create+write      runs= 161 fsck_bad=  0 meta_bad=  0 data_bad=  0 skipped= 64 (crc 0)
  barrier, no csum     ordered  append->indirect  runs= 193 fsck_bad=  0 meta_bad=  0 data_bad=  0 skipped= 62 (crc 0)
  NO barrier, no csum  data     create+write      runs= 189 fsck_bad= 11 meta_bad= 10 data_bad= 13 skipped= 78 (crc 0)
  NO barrier, no csum  data     append->indirect  runs= 317 fsck_bad= 10 meta_bad=  8 data_bad= 14 skipped=142 (crc 0)
  NO barrier, no csum  ordered  create+write      runs= 149 fsck_bad= 10 meta_bad=  8 data_bad=  8 skipped= 51 (crc 0)
  NO barrier + csum    data     create+write      runs= 189 fsck_bad=  0 meta_bad=  0 data_bad=  0 skipped= 92 (crc 14)
  NO barrier + csum    data     append->indirect  runs= 317 fsck_bad=  0 meta_bad=  0 data_bad=  0 skipped=156 (crc 14)
  NO barrier + csum    ordered  create+write      runs= 149 fsck_bad=  0 meta_bad=  0 data_bad=  0 skipped= 61 (crc 10)
```

- barrier 가 있으면 캐시가 아무리 섞어도 0 이다. `skipped` 는 TxE 가 아직 안 닿은 정상적인 미완성 트랜잭션이다.
- barrier 와 checksum 이 둘 다 없으면 **메타데이터까지 깨진다**(fsck_bad 10–14). 저널이 있는데도 fsck 가 다시 필요해지는 경우다. "저널이 있으면 fsck 가 필요 없다" 는 **장치가 순서(flush)를 지킨다는 가정 위에서만** 참이다.
- checksum 이 있으면 barrier 없이도 data 모드는 0 이다(`crc 14` = crc 가 실제로 잡아낸 횟수). 커밋 전 flush 한 번(디스크라면 회전 한 바퀴)을 아끼는 것이 OSTEP §42.3 의 "transaction checksum" 이고, ext4 의 `journal_checksum` / `journal_async_commit`(ext4.rst:149-158)이다.
- 창 4 에서는 ordered + checksum 도 0 이다. 하지만 **창을 6 으로 키우면 data_bad 가 나온다**(연습 1). checksum 은 저널 블록만 덮고, 커밋 전에 제자리로 보낸 ordered 데이터는 덮지 않기 때문이다. 6.18.55 의 ext4 는 바로 이 조합의 마운트를 거부한다. `fs/ext4/super.c:4976-4980`: `data=ordered` 이고 `JOURNAL_ASYNC_COMMIT` 이면 `"can't mount with journal_async_commit in data=ordered mode"`. 시뮬레이터가 커널 개발자의 결론을 독립적으로 다시 찾은 셈이다.

JBD2 의 실제 barrier 위치(6.18.55, `fs/jbd2/commit.c`):

- 546 행 `journal_submit_data_buffers`: ordered 데이터를 먼저 제출한다. 738 행 `journal_finish_inode_data_buffers` 에서 그 완료를 기다린다.
- 152–154 행: 커밋 블록 쓰기에 `REQ_PREFLUSH | REQ_FUA` 를 단다. 단 `JBD2_BARRIER` 이고 async_commit 이 아닐 때만이다. PREFLUSH 는 "앞의 쓰기를 먼저 매체로", FUA 는 "이 쓰기 자체를 캐시를 거치지 않고 매체로" 라는 뜻이다. 우리 코드의 TxE 앞 flush + TxE 뒤 flush 와 같다.
- 774–786 행: 저널이 별도 장치면 커밋 전에 파일 시스템 장치를 flush 한다. async_commit 이면 커밋 블록을 이때 바로 제출한다. 883–886 행: async_commit 이면 그 뒤에 저널 장치를 flush 한다.

### Part C — 펌웨어 쪽에서 보면 (Don 의 SSD 경험)

| 이 레슨 | SSD 펌웨어 |
|---|---|
| 제자리 메타데이터 (inode, bitmap) | FTL L2P 매핑 테이블 (DRAM 에 있고, NAND 에 주기적으로 저장) |
| 저널(로그) + TxE | L2P **변경 로그(delta journal)** + 체크포인트. 전원이 나가면 마지막 체크포인트 + 로그 replay(+ OOB/spare 영역의 LBA 태그 스캔)로 복구 (MEM-M14 가 다룰 예정) |
| `bflush()` / FUA | NVMe Flush 명령, Write 의 FUA 비트. Identify Controller 의 VWC(Volatile Write Cache) 필드가 캐시 유무를 알림 |
| 쓰기 캐시 부분집합 생존 | PLP(전원 차단 보호 커패시터)가 없는 제품은 DRAM 캐시 내용을 잃음. PLP 가 있으면 남은 전력으로 캐시를 덤프 → 사실상 "flush 가 항상 됐다" |
| 전수 크래시 주입 | 펌웨어의 power-loss 테스트: 랙에서 전원을 수천 번 끊고 데이터 무결성(LBA + 시퀀스 스탬프) 확인. 이 레슨은 그것을 결정적·전수로 축소한 것 |
| TxE checksum, tid | NAND 페이지마다 ECC·CRC 와 시퀀스 번호. 찢어진(부분 program) 페이지는 버리고 가장 최근의 완전한 버전을 택함 |

(SSD 쪽 내용은 일반적인 설계 패턴으로 적었다. 제품별 세부는 다르다.) 플래시용 파일 시스템 **littlefs** 는 저널을 쓰지 않는다. 메타데이터 블록 쌍(metadata pair)에 commit 을 덧붙이는 COW 방식이다. 이 내용은 littlefs 설계 문서 기준이며 STOR-A03 이 다룰 예정이다. 저널 대신 "새 위치에 쓰고 포인터 하나를 원자적으로 바꾸기" 를 쓰는 셈이고, 같은 생각이 BM-A02(플래시 쓰기 규칙)와 DS-A10(플래시 로그 구조 KV)에 있다. APFS·ZFS·btrfs 도 COW 계열이다(일반 지식, 세부는 확인하지 않았다).

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o /tmp/OS-A07_so && /tmp/OS-A07_so      # 43/43 passed, ~0.4 s
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o /tmp/OS-A07_as && /tmp/OS-A07_as   # clean
```

측정 환경: Apple M6, `Apple clang version 21.0.0 (clang-2100.0.123.102)`, macOS SDK 26.4. 5 번 반복해 모두 43/43 이었고, 다른 cwd(`/tmp`)에서도 같다. 실행 중 `uptime` 의 load average 는 22.37 이었다(다른 작업과 공유하는 기계). 결과는 전부 결정적이라 부하와 무관하다. 데모·연습은 `#define A07_NO_MAIN` 후 `#include "solution.c"` 로 엔진을 그대로 쓴다. 그래서 solution.c 와 같은 폴더에서 빌드해야 한다.

## 7. 자주 하는 실수

1. **TxE 를 로그와 같은 배치로 보내기 (barrier 없음).** 장치가 순서를 바꾸면 TxE 가 먼저 닿는다. Part C 의 `NO barrier, no csum` 행에서 fsck_bad 가 10 이상 나왔다. 고침: TxE 앞에 `bflush()`, 또는 TxE 에 crc 를 넣어 replay 전에 확인한다.
2. **checkpoint 를 커밋 flush 전에 시작하기.** 제자리 블록이 TxE 보다 먼저 닿을 수 있다. 그런데 그 트랜잭션이 찢어져 버려지면, 제자리에는 반쯤 바뀐 상태가 남고 되돌릴 방법이 없다(redo 로그에는 undo 정보가 없다). 고침: TxE 뒤의 flush 가 끝난 뒤에 checkpoint 한다. async_commit 에서도 JBD2 는 커밋 뒤에 flush 한다(commit.c:883-886).
3. **tid 를 확인하지 않거나, 찢어진 tid 를 재사용하기.** 원형 저널에는 옛 TxB·TxE 가 남아 있다. tid 를 확인하지 않으면 옛 트랜잭션을 다시 replay 하고, 새 로그와 옛 TxE 가 짝지어진다. 고침: `h.tid == 기대값`, `e.tid == h.tid`, 찢어진 꼬리가 있었으면 `next_tid = tid + 1`.
4. **ordered 에서 forget 을 빼기.** 해제된 indirect 블록의 메타데이터 버전이 체크포인트 대기 목록에 남는다. 그 블록이 새 파일의 데이터로 쓰이면, 다음 checkpoint 가 옛 포인터 표로 데이터를 덮는다. 크래시가 없는데도 깨진다. 고침: `dput` 에서 `forget(b)` 한다.
5. **writeback 데이터를 미뤄 놓고 같은 연산 안의 읽기가 디스크를 보게 하기.** 부분 블록 RMW 가 옛 내용을 읽는다. 고침: `rd()` 가 미룬 목록을 먼저 본다(주어진 코드가 그렇게 되어 있다).
6. **스윕에서 "fsck 0" 만 확인하기.** writeback 은 fsck 가 늘 0 인데 내용이 틀린다. 고침: 크래시 없는 기준 실행의 상태 요약(메타·내용 따로)과 비교한다. 허용 집합은 `{s_{j-1}, s_j}` 다.
7. **mask 중복을 세기.** 캐시에 2 개만 있을 때 mask 0..15 는 결과가 4 가지뿐이다. `runs` 가 부풀어 "검사 수" 가 거짓이 된다. 고침: `mask >= 1u << D.pend_at_crash` 이면 건너뛴다.

## 8. 면접 Q&A

<details><summary>Q1. 저널링 파일 시스템은 왜 부팅 때 fsck 가 필요 없나요? 언제는 여전히 필요하죠?</summary>

크래시 뒤에 가능한 디스크 상태가 "마지막으로 커밋된 트랜잭션까지 반영된 상태" 하나뿐이기 때문입니다. 커밋 전 트랜잭션은 제자리를 건드리지 않았고, 커밋된 것은 replay 로 마저 합니다. 그래서 복구 비용이 O(디스크)에서 O(로그)로 줄어듭니다. 연습 5 의 실측에서 replay 는 9–10 번 읽었고, fsck 는 파일 4 개일 때 83 번, 16 개일 때 119 번 읽었습니다. fsck 가 여전히 필요한 경우는 그 전제가 깨질 때입니다. (1) 장치가 flush 를 무시하거나 순서를 바꿨는데 checksum 이 없을 때(Part C 에서 fsck_bad 10+). (2) 매체 손상, 비트 플립, 펌웨어 버그처럼 저널이 모르는 손상. (3) 커널 버그. 그래서 e2fsck 는 주기적 검사(마운트 횟수·기간 기준) 옵션을 여전히 갖고 있습니다(세부 기본값은 배포판마다 다릅니다).
</details>

<details><summary>Q2. 커밋 지점은 정확히 어디인가요? 그 앞뒤에 flush 가 왜 둘 다 필요하죠?</summary>

TxE 블록이 매체에 닿는 순간입니다. 앞의 flush 는 "TxE 가 닿았다면 로그도 다 닿았다" 를 보장합니다. 이것이 없으면 쓰레기가 replay 됩니다. 뒤의 flush 는 "제자리 덮어쓰기(checkpoint)가 시작될 때 커밋이 이미 영속" 을 보장합니다. 이것이 없으면 제자리는 바뀌었는데 커밋은 사라져 되돌릴 수 없습니다. TxE 는 원자적으로 써지는 단위(OSTEP 가정: 512 B 섹터) 안에 있어야 합니다. JBD2 는 이 둘을 커밋 블록 한 번의 `REQ_PREFLUSH | REQ_FUA`(commit.c:152-154)로 표현합니다.
</details>

<details><summary>Q3. data, ordered, writeback 모드를 비교해 주세요. 기본값이 ordered 인 이유는?</summary>

셋 다 메타데이터는 같은 트랜잭션 규칙을 따르므로 fsck 결과는 같습니다(스윕에서 전부 0). 차이는 데이터가 닿는 시점입니다. data 는 데이터도 로그에 넣어 old-or-new 가 보장되지만 데이터를 두 번 씁니다(16 KB 순차 쓰기에서 75 vs 59). ordered 는 데이터를 커밋 전에 제자리에 써서, 커밋된 포인터가 늘 진짜 데이터를 가리킵니다. 비용은 writeback 과 같습니다. 단 제자리 덮어쓰기는 원자적이지 않습니다(overwrite 워크로드에서 data_bad 2). writeback 은 같은 비용에 순서 제약이 없어 빠를 수 있지만, 크래시 뒤 지워진 파일의 바이트가 새 파일에 보일 수 있습니다(reuse N=24 재현). 이는 보안 문제입니다(ext4.rst:62-65). ordered 는 "writeback 비용으로 보안 문제를 없앤다" 는 절충이라 기본값입니다.
</details>

<details><summary>Q4. (꼬리) ordered 에서 "덮어쓰기는 원자적이지 않다" 면 데이터베이스는 어떻게 하죠?</summary>

파일 시스템이 아니라 응용이 원자성을 만듭니다. 방법은 둘입니다. (1) 자기 WAL(write-ahead log)을 두고 fsync 로 커밋 지점을 만든다(SQLite WAL, PostgreSQL WAL). 페이지가 반쯤 써지는 문제(torn page)는 체크섬이나 full-page write 로 막습니다(PostgreSQL 의 full_page_writes 가 그 예로 알고 있지만 세부는 확인하지 않았습니다). (2) 설정 파일 크기라면 OS-B13 의 tmp → fsync → rename → 디렉터리 fsync 를 씁니다. 이 레슨의 rename-over 워크로드가 정확히 그 rename 이고, 저널 모드에서는 이름 교체가 old-or-new 였습니다. 저널이 없으면 이름이 둘이거나 nlink 가 틀린 상태가 나왔습니다(meta_bad 4).
</details>

<details><summary>Q5. revoke 레코드는 왜 필요한가요?</summary>

메타데이터 블록 X 가 저널에 기록됩니다(예: indirect 블록). 그 파일이 지워져 X 가 해제되고, X 가 다른 파일의 **데이터**로 재사용됩니다. ordered 라서 데이터는 저널에 없습니다. 그런데 첫 트랜잭션이 아직 체크포인트되지 않았다면, 크래시 뒤 replay 가 X 에 옛 메타데이터를 다시 써서 새 파일 데이터를 덮습니다. 연습 4 에서 재현했고, `/c` 의 두 번째 블록이 `75 0 0 0…`(indirect 표)로 덮였습니다. ext3/JBD2 는 X 를 해제할 때 revoke 레코드를 저널에 쓰고, 복구 때 `PASS_REVOKE` 에서 모아 그보다 앞선 트랜잭션의 X 는 replay 하지 않습니다(recovery.c:306-310, revoke.c:326 `jbd2_journal_revoke`). OSTEP §42.3 의 Tweedie 예시와 같은 내용입니다. 더 단순한 대안은 "체크포인트 안 된 저널에 옛 버전이 있는 블록은 재할당하지 않는다" 이고, 연습 4 가 이것으로 고쳤습니다. data 모드는 새 데이터도 나중 트랜잭션에 있어 replay 순서상 이기므로 이 문제가 없었습니다(data_bad 0).
</details>

<details><summary>Q6. 디스크 쓰기 캐시가 있으면 무엇이 깨지나요? checksum 이 다 해결하나요?</summary>

캐시는 flush 전의 쓰기들을 임의 순서로 매체에 내립니다. barrier 없이 TxE 를 보내면 TxE 만 닿고 로그 일부는 사라질 수 있고, 그러면 쓰레기가 replay 됩니다(Part C 에서 fsck_bad 10–14). TxE 의 crc 가 로그 전체를 덮으면 그런 트랜잭션은 버려지므로 저널 자체는 안전합니다(data 모드 0, crc 가 14 번 잡음). 하지만 checksum 은 저널 밖에서 커밋 전에 닿아야 하는 쓰기, 즉 ordered 데이터를 보호하지 못합니다. 창 6 에서 ordered 의 data_bad 는 3/2 였습니다. 그래서 ext4 는 `journal_async_commit` + `data=ordered` 마운트를 거부합니다(super.c:4976-4980). 고치려면 커밋 전에 데이터용 flush 한 번이 필요하고(연습 1 의 dflush → 0), 이것이 commit.c:774-778 의 "fs 장치 flush" 와 같은 역할입니다.
</details>

<details><summary>Q7. (설계) 이 저널을 SSD 펌웨어의 L2P 테이블 보호에 쓴다면 어떻게 바꾸겠나요?</summary>

(1) **논리 로깅**으로 바꿉니다. L2P 갱신은 (LBA → 새 PPA) 몇 바이트라 블록 이미지를 통째로 로그하는 것은 낭비입니다. 로그 레코드를 NAND 페이지에 모아 쓰고, replay 를 멱등하게 만듭니다. 적용 순서를 시퀀스 번호로 정하고, 같은 LBA 는 마지막 것이 이기게 합니다. (2) **TxE = 페이지 단위 CRC + 시퀀스 번호.** NAND 의 부분 program(전원 차단 중 쓰기)을 찢어진 트랜잭션으로 보고 버립니다. (3) **체크포인트 = L2P 스냅샷을 새 블록에 쓰고 루트 포인터를 원자적으로 교체**합니다(COW, 제자리 덮어쓰기 불가한 NAND 에 맞음). 저널 free 는 그 뒤에 합니다. (4) PLP 가 있으면 "마지막 flush 이후" 의 창을 커패시터로 덮어 로그를 덤프합니다. 없으면 호스트 Flush/FUA 를 받을 때까지 ack 를 미뤄야 합니다. (5) 검증은 이 레슨처럼 **모든 program 지점에서 전원 차단**(결정적 시뮬레이터)과 실제 보드의 반복 전원 차단을 같이 합니다. MEM-M14(FTL 전원 차단 복구)가 그 다음 단계입니다.
</details>

<details><summary>Q8. (꼬리) 이 검증 방법이 틀리는 경우, 즉 놓치는 것은 무엇인가요?</summary>

(1) **블록 안의 찢어짐**: 우리는 1 KB 블록이 원자적이라고 가정했습니다. 실제 4 KB 블록은 512 B 섹터 단위로 찢어질 수 있습니다. TxE 를 한 섹터에 두고 데이터 블록은 crc 로 덮어야 합니다. (2) **장치의 거짓말**: flush 를 무시하는 장치는 모델 밖입니다(OSTEP 는 그런 디스크가 있다는 연구를 인용합니다). (3) **부분집합 모델의 한계**: 창 4 에서는 ordered + checksum 의 버그가 안 보였고 창 6 에서 보였습니다. 창이 작으면 버그를 놓칩니다. (4) **매체 오류, 읽기 실패, 비트 플립**은 주입하지 않았습니다. (5) **동시성**이 없습니다. 실제 JBD2 는 트랜잭션이 커밋되는 동안 다음 트랜잭션이 열립니다. (6) 워크로드 8 개 밖의 연산 조합. 학계 도구(ALICE, CrashMonkey 등)는 기록한 쓰기 trace 에서 재정렬 가능한 상태를 체계적으로 만들어 이 공백을 줄입니다. 이 이름들은 기억에 의존한 것이라 원문을 확인하지 않았습니다.
</details>

<details><summary>Q9. 복구 중에 다시 전원이 나가면요?</summary>

replay 는 같은 이미지를 같은 자리에 쓰는 일이라 몇 번을 해도 결과가 같습니다(멱등). 복구가 끝나기 전에는 js 를 바꾸지 않습니다(replay → flush → js 갱신 → flush). 그래서 도중에 끊기면 다음 마운트가 같은 tail 부터 다시 replay 합니다. 연습 3 에서 (N1, N2) 쌍 77 개를 전수로 돌렸고 위반은 0 이었습니다. 순서를 거꾸로 해서 js 를 먼저 비우면 replay 가 반쯤 된 상태가 영구히 남습니다.
</details>

## 9. 추가 연습

### 연습 1 — async commit 은 ordered 를 지키나? (쓰기 캐시 창 6)

창을 6 으로 키우고(`#define A07_WC_MAX 6`) barrier 없음 + checksum 으로 data·ordered 를 스윕하라. ordered 가 깨지면, TxE 전에 **데이터만** 내리는 flush(`J.dflush`)로 고쳐라.

<details><summary>답</summary>

```text
-- ex1: window 6, NO barrier + csum (async commit)
  data     create+write      runs= 333 data_bad=0 crc-rejected=38
  ordered  create+write      runs= 277 data_bad=3 (N=16 mask=0x3c first)
  ordered+dflush create+write runs= 131 data_bad=0
  data     append->indirect  runs= 765 data_bad=0 crc-rejected=62
  ordered  append->indirect  runs= 541 data_bad=2 (N=9 mask=0x3e first)
  ordered+dflush append->indirect runs= 193 data_bad=0
```

N=16, mask 0x3c 는 캐시 6 칸 중 앞의 2 칸(ordered 데이터 D2, D3)이 사라지고 뒤의 4 칸(TxB, dbm, I0, TxE)이 살아남은 경우다. checksum 은 저널 블록만 덮으므로 트랜잭션은 "완전" 하다. 커밋된 inode 는 D2 를 가리키는데 D2 에는 새 데이터가 없다. ordered 의 약속("데이터가 메타데이터보다 먼저")이 깨진 것이다. 메타데이터 쪽 fsck·meta 는 0 이다. 커밋 전에 flush 한 번(dflush)이면 고쳐진다. ext4 가 `journal_async_commit` + `data=ordered` 를 거부하는 이유(super.c:4976-4980)가 이것이다.
</details>

### 연습 2 — 그룹 커밋

create 4 개를 `op_enter()` … `op_exit()` 한 쌍으로 묶어 트랜잭션 하나로 만들어라. 쓰기 수를 비교하고, 모든 N 에서 파일이 0 개 또는 4 개만 남는지 확인하라.

<details><summary>답</summary>

```text
-- ex2: group commit of 4 creates
  writes: grouped 9 (1 tx) vs one-by-one 36 (4 tx)
  crash sweep: runs=10, all-or-nothing violations=0 (0 files: 5 runs, 4 files: 5 runs)
```

ibm·I0·D0 가 네 연산에서 같은 블록이라 흡수되어, 로그 3 블록 + TxB/TxE + 제자리 3 + js = 9 번이 된다. 하나씩 하면 36 번이다. 묶은 연산은 함께 원자적이다. 대가는 지연이다. 앞 연산이 커밋을 기다리므로 fsync 지연과 처리량을 맞바꾼다. JBD2 는 `commit=` 주기(기본 5 초) 동안 연산을 모으고, fsync 가 오면 그 트랜잭션을 바로 커밋한다.
</details>

### 연습 3 — 복구 도중의 크래시

ordered + lazy checkpoint(트랜잭션이 여러 개 쌓임)에서 첫 크래시 N1 과 복구 중 크래시 N2 의 모든 쌍을 돌려라. 두 번째 마운트 뒤 fsck 0, 상태 ∈ {s_{j-1}, s_j} 여야 한다.

<details><summary>답</summary>

```text
-- ex3: crash during recovery (ordered, lazy checkpoint)
  create+write      W=12  (N1,N2) pairs=38  bad=0
  append->indirect  W=16  (N1,N2) pairs=39  bad=0
```

replay 는 물리 이미지를 같은 자리에 쓰는 일이라 멱등이다. js 는 replay 와 flush 가 끝난 뒤에만 바꾼다. 그래서 몇 번을 끊어도 다음 마운트가 같은 곳에서 다시 시작한다.
</details>

### 연습 4 — revoke 문제 재현과 수정

ordered + lazy checkpoint 에서 다음을 순서대로 하라: `/p` 의 fbn 6 에 쓰기(indirect 블록 X 할당, 메타데이터로 저널됨) → unlink `/p`(X 해제) → create `/c` → `/c` 에 2 KB 쓰기(X 가 데이터로 재사용됨). 데이터가 깨지는 N 을 찾고, 재할당 금지(veto)로 고쳐라.

<details><summary>답</summary>

```text
-- ex4: freed indirect block reused as data (ordered)
  ordered, ckpt each     : runs=40 fsck_bad=0 data_bad=0
  ordered, lazy          : runs=24 fsck_bad=0 data_bad=1  N=23
  ordered, lazy + veto   : runs=24 fsck_bad=0 data_bad=0
  data,    lazy          : runs=24 fsck_bad=0 data_bad=0
  N=23: /c bytes 1024..1039 as u32 = 75 0 0 0 (an indirect block: entry0 = block number)
```

N=23 은 마지막 트랜잭션의 커밋 뒤, 체크포인트 전이다. replay 가 첫 트랜잭션의 X(indirect 표: 첫 칸 = 블록 75)를 다시 써서 `/c` 의 데이터를 덮는다. fsck 는 0 이다(메타데이터는 다 맞다). 체크포인트를 매번 하면 저널에 옛 버전이 남지 않으니 문제가 없다. veto(`J.ck_*` 나 열린 tx 에 메타데이터 버전이 있는 블록은 할당하지 않음)로 고쳐지고, data 모드는 새 데이터가 나중 트랜잭션에 있으므로 문제가 없다. 실제 JBD2 는 revoke 레코드로 같은 일을 한다(Q5).
</details>

### 연습 5 — 복구 비용: fsck vs replay

7 KB 파일(indirect 포함) 4 개 / 16 개를 만든 뒤, 마지막 create 를 커밋만 하고 전원을 끊어라. 마운트(replay)의 읽기 수와 fsck 의 읽기 수를 비교하라.

<details><summary>답</summary>

```text
-- ex5: recovery cost — fsck reads vs journal replay reads
   4 files: replay reads=9 (replayed /last), fsck reads=83, fsck findings=0
  16 files: replay reads=10 (replayed /last), fsck reads=119, fsck findings=0
```

replay 는 sb, js, TxB, 로그 3, TxE, 다음 자리 확인으로 로그 크기만큼만 읽는다. 16 개 쪽이 1 많은 것은 wrap 확인 한 번 때문이다. fsck 는 inode 표 전체, 디렉터리, indirect 블록을 읽으므로 파일 수에 비례해 늘어난다. 이 축약 fsck 는 캐시가 없어 inode 블록을 inode 마다 다시 읽으므로 절대값은 부풀려져 있다. 하지만 "O(메타데이터 전체) vs O(로그)" 라는 모양은 같다(OSTEP §42.5).
</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o /tmp/OS-A07_pr && /tmp/OS-A07_pr    # 10/10 passed, ~0.7 s (solution.c 를 include)
cc -std=c11 -Wall -Wextra -Werror -O2 demo_crash_table.c -o /tmp/OS-A07_demo && /tmp/OS-A07_demo
```

## 10. 핵심 정리

- 커밋 = **TxE 한 블록이 매체에 닿는 순간**. 그 전 크래시는 없던 일이 되고, 그 뒤 크래시는 replay(멱등 redo)로 마저 한다. 순서는 로그 → flush → TxE → flush → checkpoint → flush → js(free) → flush.
- 원형 저널에는 옛 레코드가 남는다. **단조 증가 tid** + TxE 의 crc 로 "이번 트랜잭션의 완전한 사본" 만 replay 한다. 찢어진 tid 는 건너뛴다.
- 전수 크래시 스윕(8 워크로드 × 모든 N): none 은 fsck 가 7/8 워크로드에서 문제를 찾고 rename 은 비원자적이다. data 는 0. ordered 는 메타데이터 0, 제자리 덮어쓰기만 섞인다. writeback 은 **fsck 0 인데 지워진 파일의 바이트가 보인다**.
- 쓰기 증폭: data > ordered = writeback. 작은 연산은 TxB/TxE/js 고정비가 지배한다. 그룹 커밋·lazy checkpoint 의 흡수가 이를 줄인다(36 → 9 writes).
- 쓰기 캐시가 순서를 바꾸면 barrier 없는 저널은 메타데이터까지 깨진다. **checksum 은 저널을 지키지만 ordered 데이터는 못 지킨다.** 그래서 ext4 는 `journal_async_commit` + `data=ordered` 를 거부한다(super.c:4976).
- revoke: 해제된 메타데이터 블록이 데이터로 재사용되면, 체크포인트 안 된 옛 버전의 replay 가 데이터를 덮는다. JBD2 는 revoke 레코드로, 이 레슨은 재할당 금지로 막았다.
- SSD 펌웨어의 L2P 로그 + 체크포인트, Flush/FUA, PLP, power-loss 테스트는 이 레슨과 같은 구조다.

## 참고자료

- OSTEP 42장 "Crash Consistency: FSCK and Journaling": §42.1 예제, §42.2 fsck, §42.3 journaling(data journaling, recovery, batching, 유한한 로그 = circular log + journal superblock, metadata journaling, block reuse/revoke, Fig 42.1/42.2 타임라인), §42.4 다른 방법(soft updates, COW, optimistic crash consistency), §42.5 요약. 디스크: `operating-system-three-easy-pieces/book-md/C42_crash_consistency_fsck_and_journaling.md`. Don 의 한국어 노트: `operating-system-three-easy-pieces/notes/2026-09-30_C42_crash_consistency_journaling.md`
- Linux 6.18.55 소스(Lima VM `/var/lab/src/linux-6.18.55`): `Documentation/admin-guide/ext4.rst:62-65, 149-158, 172-183, 185-187, 396-425`; `include/linux/jbd2.h:112, 122-126, 279-285`; `fs/jbd2/recovery.c:306-310`; `fs/jbd2/commit.c:152-154, 546, 738, 774-786, 883-886`; `fs/jbd2/revoke.c:303-326`; `fs/ext4/super.c:4976-4980`
- V. Prabhakaran et al., "IRON File Systems", SOSP 2005: 트랜잭션 checksum(OSTEP 인용) / S. Tweedie, "Journaling the Linux ext2fs Filesystem", LinuxExpo 1998 / V. Chidambaram et al., "Optimistic Crash Consistency", SOSP 2013 (모두 OSTEP 42장 참고문헌 기준)
- (기억 기준, 원문 미확인) T. S. Pillai et al., "All File Systems Are Not Created Equal: On the Complexity of Crafting Crash-Consistent Applications", OSDI 2014 (ALICE) / M. Zheng et al., "Understanding the Robustness of SSDs under Power Fault", FAST 2013
- littlefs 설계 문서: github.com/littlefs-project/littlefs (DESIGN.md, metadata pair + COW)
- NVM Express Base Specification: Flush 명령, FUA, Identify Controller VWC (nvmexpress.org)
- 이 트랙의 관련 레슨: OS-M14(VSFS·fsck, 연습 4 = 저널 없는 크래시 스윕), OS-B13(write → fsync → rename, F_FULLFSYNC), BM-A02(플래시 쓰기 규칙), DS-A09(FTL 매핑·GC), DS-A10(플래시 로그 구조 KV). 다음 단계: OS-A08(LFS·FTL 세그먼트 클리닝), MEM-M14(FTL 전원 차단 복구), BM-A06(모든 쓰기 지점 power-cut 주입), STOR-A03(LittleFS 내부)
