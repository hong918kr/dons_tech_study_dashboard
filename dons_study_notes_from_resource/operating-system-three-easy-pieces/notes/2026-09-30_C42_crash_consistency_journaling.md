# Ch.42 크래시 일관성 — fsck와 저널링(write-ahead logging)

> 📖 원문: [42. Crash Consistency: FSCK and Journaling](../book-md/C42_crash_consistency_fsck_and_journaling.md) · [PDF p.508](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=508) · ⏱️ 읽기 약 70분 (C 시뮬레이터 + fsck.py 포함 시 +60분) · 🔗 선행: [Ch.40 FS 구현](2026-09-30_C40_file_system_implementation.md), [Ch.41 FFS](2026-09-30_C41_ffs.md)

## 0. 한눈에 보기

> **CRUX — "The system may crash or lose power between any two writes, and thus the on-disk state may only partially get updated. After the crash, the system boots and wishes to mount the file system again (in order to access files and such). Given that crashes can occur at arbitrary points in time, how do we ensure the file system keeps the on-disk image in a reasonable state?"**
> (시스템은 어떤 두 쓰기 사이에서든 크래시하거나 전원을 잃을 수 있고, 그러면 디스크 상태는 일부만 갱신된다. 재부팅 후 FS를 다시 mount 하려 할 때, 크래시가 아무 때나 일어날 수 있다면 디스크 이미지를 어떻게 "말이 되는" 상태로 유지할까?)

- 파일에 블록 하나 append = **inode, data bitmap, data block 세 블록**을 써야 한다. 디스크는 한 번에 하나씩 쓰므로 그 사이 전원이 나가면 **불일치·공간 누수·쓰레기 데이터**가 생긴다 (**crash-consistency problem**).
- 해법 1 **fsck**: 일단 놔두고 재부팅 때 디스크 **전체**를 훑어 메타데이터를 고친다. 동작은 하지만 **O(디스크 크기)** 라 너무 느리고, "메타데이터는 일관된데 데이터는 쓰레기"는 못 고친다.
- 해법 2 **저널링(= write-ahead logging)**: 제자리에 덮어쓰기 **전에** 저널에 "할 일"을 먼저 쓴다. `TxB + 내용 → (대기) → TxE(커밋) → (대기) → checkpoint → free`. 복구는 커밋된 트랜잭션만 **redo** — **O(로그 크기)**.
- 비용을 줄인 변형: **metadata(ordered) journaling** — 데이터는 저널에 안 쓰고 **"가리켜지는 것(데이터)을 가리키는 것(inode)보다 먼저"** 쓴다. 까다로운 케이스: **블록 재사용 → revoke 레코드**. 커밋 대기를 없애는 **트랜잭션 체크섬**(ext4).
- 그 밖: **soft updates**(쓰기 순서를 정교하게), **COW**(ZFS, LFS), **backpointer 기반 일관성**, **optimistic crash consistency**.
- 이 장의 C 코드는 **작은 WAL을 직접 구현하고 모든 단계에서 크래시를 주입**해 위 주장들을 하나하나 확인한다.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| crash-consistency problem | 여러 블록 갱신 중 크래시 → 일부만 반영 | I[v2]만 쓰이고 B[v2]는 안 쓰임 |
| 불일치 (inconsistency) | FS 자료구조끼리 말이 안 맞음 | inode는 블록 5 사용, bitmap은 free |
| space leak | 할당 표시는 됐지만 아무도 안 가리킴 | bitmap만 쓰인 경우 |
| fsck | 재부팅 시 전체 스캔으로 메타데이터 수리 | "집 전체 뒤져서 열쇠 찾기" |
| lost+found | 아무 디렉터리도 안 가리키는 inode를 모으는 곳 | fsck가 고아 inode를 옮김 |
| journaling / WAL | 덮어쓰기 전에 로그에 먼저 기록 | ext3/4, XFS, NTFS, JFS |
| TxB / TxE | 트랜잭션 시작/끝(커밋) 블록, TID 포함 | TxE는 512B 단일 섹터로 원자적 |
| physical logging | 블록 내용 그대로 로그에 | ext3 |
| logical logging | "파일 X에 블록 append" 같은 논리 기록 | 공간 절약, 복잡 |
| checkpoint | 커밋된 내용을 제자리(최종 위치)에 쓰기 | 끝나면 로그 공간 해제 가능 |
| redo logging | 복구 시 커밋된 tx를 순서대로 다시 쓰기 | 중복 쓰기는 무해(멱등) |
| circular log / journal superblock | 로그 재사용 + 살아 있는 tx 범위 기록 | head/tail 포인터 |
| data journaling | 메타데이터 + 데이터 모두 저널 | ext3 `data=journal` |
| ordered (metadata) journaling | 메타데이터만 저널, 데이터는 커밋 전에 제자리 | ext3/4 `data=ordered` (기본) |
| revoke record | "이 블록의 옛 로그 내용은 재생하지 마" | 블록 재사용 문제 해결 |
| write barrier / FLUSH / FUA | 쓰기 순서·영속성을 디바이스에 강제 | 디스크 쓰기 캐시 때문에 필요 |
| transaction checksum | TxB/TxE에 내용 체크섬 → 한 번에 issue | ext4 `journal_checksum` |
| soft updates / COW / BBC | 쓰기 순서 / 제자리 안 쓰기 / 역포인터 | FreeBSD, ZFS·btrfs, 연구 |

## 2. 자세한 예: append 하나에 블록 3개 (42.1)

작은 FS: inode bitmap 8비트, data bitmap 8비트, inode 8개(블록 4개에), 데이터 블록 8개. 현재 inode 2번이 할당되어 데이터 블록 4번(Da)을 가리킨다.

```text
I[v1]: owner=remzi, perm=rw, size=1, ptr = 4, null, null, null
B[v1]: 00001000
```

append(4KB) 후 바라는 최종 상태:

```text
I[v2]: size=2, ptr = 4, 5, null, null
B[v2]: 00001100
Db   : 데이터 블록 5번의 새 내용
```

이 세 블록은 보통 바로 쓰이지 않고 page cache에 5~30초 머물다 FS가 내보낸다([Ch.40](2026-09-30_C40_file_system_implementation.md) write buffering). 그 사이 언제든 크래시가 날 수 있다.

### 2.1 크래시 시나리오 6가지 (+ 2가지 trivial)

| 디스크에 도달한 쓰기 | 결과 | 문제 종류 |
|---|---|---|
| Db만 | 아무도 안 가리키는 블록에 데이터 → 쓰기가 없었던 것과 같음 | FS 관점 문제 없음 (사용자 데이터는 잃음) |
| I[v2]만 | inode는 블록 5를 가리키는데 bitmap은 free, 블록 5는 옛 쓰레기 | **불일치 + 쓰레기 읽기** |
| B[v2]만 | bitmap은 5 사용 중, 가리키는 inode 없음 | **불일치 + space leak** |
| I[v2] + B[v2] | 메타데이터는 완벽히 일관됨, 그러나 블록 5는 쓰레기 | **일관되지만 쓰레기** |
| I[v2] + Db | inode가 올바른 데이터를 가리키지만 bitmap은 free | **불일치** (나중에 이중 할당 위험) |
| B[v2] + Db | 데이터도 있고 bitmap도 사용 중인데 어느 파일 것인지 모름 | **불일치 + leak** |

아래 10절 코드의 시나리오 B가 이 8가지 조합을 실제로 만들어 mini-fsck로 판정한다.

이상적으로는 FS를 한 일관된 상태에서 다른 일관된 상태로 **원자적으로** 옮기고 싶다. 하지만 디스크는 한 번에 하나씩만 커밋한다. 이것이 **crash-consistency problem**(또는 consistent-update problem)이다.

## 3. 해법 1: 파일 시스템 검사기 fsck (42.2)

"불일치는 일어나게 두고 재부팅 때 고친다"는 게으른 접근. fsck는 mount **전**에 돌며(다른 활동 없음 가정) 다음을 검사한다 (McKusick & Kowalski):

1. **Superblock**: 크기 > 할당 블록 수 같은 sanity check. 이상하면 **다른 superblock 사본**을 쓴다.
2. **Free blocks**: inode·간접 블록·이중 간접 블록… 을 전부 훑어 "실제로 할당된 블록" 집합을 만들고 **bitmap을 그에 맞게 재작성** — inode를 신뢰한다. inode bitmap도 같은 방식.
3. **Inode 상태**: 할당된 inode마다 type 필드가 유효한지 등. 고치기 어려우면 그 inode를 **지우고** bitmap 갱신.
4. **Inode links**: 루트부터 디렉터리 트리 전체를 돌며 각 inode의 링크 수를 다시 세서 inode의 값과 비교·수정. 할당됐는데 아무 디렉터리도 안 가리키면 **lost+found** 로.
5. **Duplicates**: 두 inode가 같은 블록을 가리키면 — 명백히 나쁜 inode를 지우거나, 블록을 **복사**해 하나씩 준다.
6. **Bad blocks**: 파티션 범위 밖을 가리키는 포인터는 그냥 지운다.
7. **Directory checks**: `.` 과 `..` 가 맨 앞인지, 엔트리가 가리키는 inode가 할당됐는지, 디렉터리가 두 번 이상 링크되지 않았는지.

문제:

- 구현이 복잡하다(정확히 만들기 어렵다 — SQCK 논문이 실제 fsck의 버그를 많이 찾았다).
- **근본적으로 너무 느리다**: 디스크/RAID가 커지면 수 분~수 시간. 블록 3개 갱신 중 크래시 났는데 디스크 전체를 훑는 건 "침실에서 열쇠를 떨어뜨리고 지하실부터 집 전체를 뒤지는" 꼴.
- **I[v2]+B[v2] 케이스(일관되지만 쓰레기)는 못 고친다** — 메타데이터 일관성만 목표다.

## 4. 해법 2: 저널링 (write-ahead logging) (42.3)

데이터베이스에서 훔쳐 온 아이디어. FS 최초 적용은 **Cedar**(1987), 지금은 ext3/ext4, reiserfs, JFS, XFS, NTFS.

기본 생각: 제자리 구조를 덮어쓰기 전에, **잘 알려진 다른 곳(log)** 에 "지금부터 할 일"을 메모한다(**write-ahead**). 크래시가 나면 메모를 보고 정확히 무엇을 다시 할지 안다 → 디스크 전체 스캔이 필요 없다. 평상시 약간의 일을 더 해서 복구 때의 일을 크게 줄이는 거래.

ext3 배치: ext2와 같은 block group들 + **journal**(같은 파티션 안, 다른 디바이스, 또는 파일).

```text
ext2:  Super | Group 0 | Group 1 | ... | Group N
ext3:  Super | Journal | Group 0 | Group 1 | ... | Group N
```

### 4.1 data journaling — 기본 프로토콜

append 예제를 저널에 쓰면:

```text
Journal: | TxB(tid=1, 최종주소들) | I[v2] | B[v2] | Db | TxE(tid=1) |
```

- **TxB**: 이 트랜잭션의 정보(블록들의 최종 주소) + **TID**.
- 가운데: 블록 내용 그대로 = **physical logging**. (대안 **logical logging**: "파일 X에 Db를 append" — 작지만 복잡.)
- **TxE**: 끝 표시 + 같은 TID.

로그가 안전하게 디스크에 가면 제자리에 덮어쓴다 = **checkpoint**.

**함정: 5블록을 한 번에 내보내면?** 하나의 큰 순차 쓰기로 만들면 빠르겠지만, 디스크는 내부적으로 조각들을 **아무 순서로나** 완료할 수 있다. TxB, I[v2], B[v2], TxE가 먼저 쓰이고 Db가 안 쓰인 채 전원이 나가면:

```text
Journal: | TxB id=1 | I[v2] | B[v2] | ?? | TxE id=1 |
```

TxB와 TxE의 TID가 맞으니 **유효한 트랜잭션처럼 보이고**, 복구는 `??`(이 로그 칸의 옛 내용)를 Db의 최종 위치로 **복사해 버린다**. 사용자 데이터라도 나쁘고, 그게 superblock이었다면 FS가 mount 불가가 된다. (10절 코드 시나리오 C가 정확히 이 상황을 재현한다.)

그래서 **두 단계로** 쓴다. 그리고 디스크는 **512B 쓰기는 전부 되거나 전혀 안 되거나(원자적)** 를 보장하므로 **TxE를 512B 한 섹터**로 만든다.

1. **Journal write**: TxB + 메타데이터 + 데이터를 저널에 (한꺼번에 issue 가능). **완료 대기.**
2. **Journal commit**: TxE를 저널에 쓴다. **완료 대기.** → 이제 트랜잭션은 **committed**.
3. **Checkpoint**: 메타데이터·데이터를 최종 위치에 쓴다.

```svg
<svg viewBox="0 0 700 330" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="13">
  <defs><marker id="C42-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
  <text x="20" y="22" fill="currentColor" font-weight="bold">Journal</text>
  <g stroke="currentColor" fill="none">
    <rect x="20" y="32" width="60" height="40" style="fill:var(--accent-soft)"/>
    <rect x="80" y="32" width="90" height="40"/>
    <rect x="80" y="32" width="270" height="40"/>
    <rect x="80" y="32" width="180" height="40"/>
    <rect x="80" y="32" width="90" height="40"/>
    <rect x="350" y="32" width="60" height="40" style="stroke:var(--accent);fill:var(--accent-soft)" stroke-width="2"/>
    <rect x="410" y="32" width="270" height="40" stroke-dasharray="4 3"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="50" y="50">TxB</text><text x="50" y="65" font-size="10">tid=1</text>
    <text x="125" y="57">I[v2]</text><text x="215" y="57">B[v2]</text><text x="305" y="57">Db</text>
    <text x="380" y="50" font-weight="bold">TxE</text><text x="380" y="65" font-size="10">512B 원자적</text>
    <text x="545" y="57" font-size="11">다음 트랜잭션 공간 (circular)</text>
  </g>
  <g stroke="#d9534f" stroke-dasharray="6 4" stroke-width="1.5"><path d="M345,82 L345,110"/></g>
  <g fill="currentColor" font-size="11">
    <text x="20" y="96">① journal write (TxB+내용 동시 issue) → 완료 대기</text>
    <text x="352" y="96">② commit: TxE 쓰기 → 완료 대기</text>
  </g>
  <text x="352" y="122" fill="#d9534f" font-size="11">↑ 이 선(barrier/FLUSH) 이전이면 복구 시 무시</text>
  <text x="20" y="190" fill="currentColor" font-weight="bold">File system (최종 위치)</text>
  <g stroke="currentColor" fill="none">
    <rect x="60" y="200" width="120" height="40"/>
    <rect x="230" y="200" width="120" height="40"/>
    <rect x="400" y="200" width="120" height="40"/>
  </g>
  <g fill="currentColor" text-anchor="middle">
    <text x="120" y="225">inode 블록</text><text x="290" y="225">data bitmap</text><text x="460" y="225">data blk 5</text>
  </g>
  <g stroke="currentColor" fill="none" stroke-width="1.4" marker-end="url(#C42-arrow)" style="stroke:var(--accent)">
    <path d="M125,74 C125,140 120,150 120,198"/>
    <path d="M215,74 C215,140 290,150 290,198"/>
    <path d="M305,74 C305,140 460,150 460,198"/>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="20" y="168">③ checkpoint: 최종 위치에 쓰기 (중간에 크래시 → 재생하면 됨, 멱등)</text>
    <text x="20" y="270">④ free: 나중에 journal superblock 의 tail 을 옮겨 공간 반환</text>
    <text x="20" y="290">복구 = 저널 스캔 → TxB/TxE(TID, checksum) 맞는 tx만 순서대로 redo</text>
    <text x="20" y="310">TxE 없는 tx → 그냥 버림 (갱신은 "없었던 일")</text>
  </g>
</svg>
```

### 4.2 복구 (recovery)

- **커밋 전(2단계 완료 전) 크래시**: 그 업데이트는 그냥 **건너뛴다**. 아무 일도 없었던 것.
- **커밋 후, checkpoint 완료 전 크래시**: 부팅 때 로그를 스캔해 **커밋된 트랜잭션을 순서대로 다시 쓴다** = **redo logging**.
- checkpoint 도중 크래시도 괜찮다. 최악의 경우 일부 블록이 **한 번 더** 쓰일 뿐이다(같은 내용을 같은 곳에 = 멱등). 복구는 드문 일이니 중복 쓰기는 문제가 안 된다.

### 4.3 로그 업데이트 묶기 (batching)

같은 디렉터리에 file1, file2를 연달아 만들면 inode bitmap, (같은 블록의) inode들, 디렉터리 데이터, 디렉터리 inode를 **두 번씩** 저널에 쓰게 된다. ext3는 업데이트마다 커밋하지 않고 **전역 트랜잭션**에 dirty 블록을 모았다가 (예: 5초마다) 한 번에 커밋한다. 같은 블록의 반복 갱신이 한 번으로 줄어든다.

### 4.4 로그를 유한하게: circular log + journal superblock

로그가 계속 자라면 (1) 복구가 길어지고 (2) 꽉 차면 더 이상 커밋을 못 한다 → FS가 멈춘다. 해법: **checkpoint가 끝난 트랜잭션의 공간을 해제**해 로그를 원형으로 재사용. **journal superblock**(FS superblock과 별개)에 아직 checkpoint 안 된 가장 오래된/최신 트랜잭션을 기록한다.

최종 data journaling 프로토콜:

1. **Journal write** → 대기
2. **Journal commit**(TxE) → 대기
3. **Checkpoint**
4. **Free**: 나중에 journal superblock 갱신

> **ASIDE — 쓰기를 디스크에 강제하기.** 옛날엔 A를 쓰고 완료 인터럽트를 받은 뒤 B를 쓰면 순서가 보장됐다. 그런데 디스크에 **쓰기 캐시**(immediate reporting)가 있으면 "완료"가 캐시에 들어갔다는 뜻일 뿐이라 순서가 깨진다. 해법은 캐시 끄기 또는 **write barrier**(배리어 이전 쓰기가 전부 매체에 간 뒤에 이후 쓰기). 그런데 일부 제조사는 "빠른 디스크"처럼 보이려고 **배리어를 무시**한다는 연구가 있다. Kahan: "the fast almost always beats out the slow, even if the fast is wrong."

> **ASIDE — 로그 쓰기 최적화: 트랜잭션 체크섬.** TxB/내용을 쓰고 완료를 기다린 뒤 TxE를 쓰면 보통 **회전 한 바퀴**를 더 기다린다. Prabhakaran의 아이디어: TxB/TxE에 **트랜잭션 내용의 체크섬**을 넣고 **전부 한 번에** 쓴다. 복구 때 계산한 체크섬이 안 맞으면 "쓰는 도중 크래시" → 버린다. 빨라지고, 저널 읽기가 체크섬으로 보호되니 더 안전하다. **ext4**에 들어갔다 (Android 포함 수백만 대).

### 4.5 metadata journaling (ordered mode)

data journaling은 **모든 데이터를 두 번 쓴다**(저널 + 제자리). 순차 쓰기는 peak의 절반, 저널↔본체 사이 seek까지. 그래서 더 흔한 건 **ordered journaling(= metadata journaling)**: 사용자 데이터는 저널에 안 쓴다.

```text
Journal: | TxB | I[v2] | B[v2] | TxE |      (Db 는 제자리에만 한 번)
```

**언제 Db를 써야 하나?** 커밋 **후**에 쓰면: I[v2], B[v2]는 커밋됐는데 Db가 디스크에 못 간 채 크래시 → 복구가 I[v2], B[v2]를 재생해 **메타데이터는 일관되지만 I[v2]가 쓰레기를 가리킨다**. (10절 시나리오 D2 k=4 가 바로 이 결과.)

그래서 ext3 ordered mode는 **데이터를 먼저** 쓴다:

1. **Data write**: 데이터를 최종 위치에 (완료 대기는 선택)
2. **Journal metadata write**: TxB + 메타데이터 → 대기
3. **Journal commit**: TxE → 대기 (이제 데이터까지 포함해 커밋)
4. **Checkpoint metadata**
5. **Free**

> **핵심 규칙 — "가리켜지는 객체를 가리키는 객체보다 먼저 써라 (write the pointed-to object before the object with the pointer to it)."** 크래시 일관성의 심장이고, soft updates는 이 규칙을 FS 전체로 확장한 것이다.

정확히는 1과 2를 **동시에 issue** 해도 되고, 진짜 요구 조건은 **1과 2가 모두 완료된 뒤에 3(커밋)을 issue** 하는 것뿐이다.

- NTFS, XFS: **non-ordered** metadata journaling. ext3: `data=journal` / `data=ordered` / `data=writeback`(unordered: 데이터 아무 때나). **모든 모드가 메타데이터 일관성은 지킨다**; 차이는 데이터 의미론.

### 4.6 까다로운 경우: 블록 재사용과 revoke

Tweedie(ext3 주 개발자): "가장 끔찍한 부분이 뭐냐고? 파일 삭제다. 삭제와 관련된 건 다 털이 숭숭하다… 블록이 지워졌다 재할당되면 무슨 일이 생길지 악몽을 꾼다."

시나리오 (metadata journaling):

1. 디렉터리 `foo` 에 엔트리 추가 → 디렉터리 데이터는 **메타데이터**이므로 저널에 들어감. foo의 데이터는 블록 1000.

```text
Journal: TxB id=1 | I[foo] ptr:1000 | D[foo] final addr:1000 | TxE id=1
```

2. 사용자가 `foo` 안의 모든 것과 `foo` 자체를 지움 → 블록 1000 해제.
3. 새 파일 `foobar` 생성, **블록 1000 재사용**. foobar의 inode는 저널에 들어가지만, foobar의 **데이터는 사용자 데이터라 저널에 안 들어간다**.

```text
Journal: TxB id=1 | I[foo] | D[foo]→1000 | TxE id=1 | TxB id=2 | I[foobar] ptr:1000 | TxE id=2
```

4. 크래시. 복구는 로그 전체를 재생 → tx1의 **D[foo](옛 디렉터리 내용)를 블록 1000에 덮어써** foobar의 데이터가 사라진다!

해법들: 삭제가 저널에서 checkpoint 될 때까지 블록을 재사용하지 않기, 또는 ext3처럼 **revoke 레코드**: 삭제할 때 "블록 1000 revoke"를 저널에 쓴다. 복구는 **먼저 revoke를 모두 스캔**하고, revoke된 블록의 (그보다 오래된) 로그 내용은 **재생하지 않는다**. 10절 시나리오 E가 revoke OFF/ON을 비교한다.

### 4.7 타임라인 정리

원문 그림 42.1/42.2를 다시 그린 것. 시간은 아래로, **빨간 점선 = 그 위의 쓰기가 모두 완료되어야 아래 쓰기를 issue 할 수 있다**.

```svg
<svg viewBox="0 0 710 320" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="12">
  <text x="10" y="18" fill="currentColor" font-weight="bold" font-size="13">Data journaling (그림 42.1)</text>
  <g fill="currentColor" text-anchor="middle" font-size="11">
    <text x="45" y="40">TxB</text><text x="105" y="40">내용(메타+데이터)</text><text x="175" y="40">TxE</text><text x="235" y="40">메타(FS)</text><text x="300" y="40">데이터(FS)</text>
    <text x="45" y="62">issue</text><text x="105" y="62">issue</text>
    <text x="45" y="84">complete</text><text x="105" y="100">complete</text>
    <text x="175" y="142">issue</text><text x="175" y="160">complete</text>
    <text x="235" y="202">issue</text><text x="300" y="202">issue</text>
    <text x="300" y="220">complete</text><text x="235" y="238">complete</text>
  </g>
  <g stroke="#d9534f" stroke-dasharray="6 4" stroke-width="1.4">
    <path d="M10,118 L340,118"/><path d="M10,178 L340,178"/>
  </g>
  <text x="10" y="270" fill="currentColor" font-size="11">데이터가 저널에 한 번, 제자리에 한 번 = 2번 쓰기</text>
  <g stroke="currentColor" opacity="0.4"><path d="M355,25 L355,300"/></g>
  <text x="370" y="18" fill="currentColor" font-weight="bold" font-size="13">Metadata (ordered) journaling (그림 42.2)</text>
  <g fill="currentColor" text-anchor="middle" font-size="11">
    <text x="405" y="40">TxB</text><text x="470" y="40">내용(메타)</text><text x="535" y="40">TxE</text><text x="600" y="40">메타(FS)</text><text x="665" y="40">데이터(FS)</text>
    <text x="405" y="62">issue</text><text x="470" y="62">issue</text><text x="665" y="62">issue</text>
    <text x="665" y="80">complete</text><text x="405" y="98">complete</text><text x="470" y="108">complete</text>
    <text x="535" y="142">issue</text><text x="535" y="160">complete</text>
    <text x="600" y="202">issue</text><text x="600" y="220">complete</text>
  </g>
  <g stroke="#d9534f" stroke-dasharray="6 4" stroke-width="1.4">
    <path d="M370,118 L705,118"/><path d="M370,178 L705,178"/>
  </g>
  <g fill="currentColor" font-size="11">
    <text x="370" y="262">데이터 쓰기는 TxB·메타와 동시에 issue 가능,</text>
    <text x="370" y="278">단 TxE issue 전에 반드시 complete</text>
    <text x="370" y="296">(완료 시각은 I/O 서브시스템이 정함 — 순서는 점선만 보장)</text>
  </g>
</svg>
```

## 5. 해법 3: 다른 접근들 (42.4)

- **Soft Updates** (Ganger & Patt): 모든 쓰기를 **정교하게 순서화**해 디스크가 **절대** 불일치 상태가 되지 않게 한다 (데이터 블록을 inode보다 먼저 등, 모든 구조에 대해 규칙 유도). 저널이 필요 없지만 FS 자료구조를 속속들이 알아야 해서 **매우 복잡**하다 (FreeBSD UFS).
- **Copy-on-Write (COW)** (ZFS, btrfs, LFS): 파일·디렉터리를 **제자리에 덮어쓰지 않고** 빈 곳에 새로 쓴 뒤, 여러 업데이트가 끝나면 **루트 포인터를 원자적으로 교체**. 일관성 유지가 단순해진다 → [Ch.43 LFS](2026-09-30_C43_lfs.md).
- **Backpointer-based consistency (BBC)**: 쓰기 순서를 **전혀** 강제하지 않는다. 모든 블록에 **역포인터**(데이터 블록 → 소유 inode)를 둔다. 접근 시 정방향 포인터(inode → 블록)가 가리키는 블록이 다시 그 inode를 가리키면 일관, 아니면 에러. 게으른 크래시 일관성.
- **Optimistic crash consistency**: 일반화된 트랜잭션 체크섬 등으로 가능한 한 많은 쓰기를 기다리지 않고 issue하고, 불일치는 감지. 일부 워크로드(fsync 많은)에서 **10배** 빨라지지만, 제대로 하려면 약간 다른 디스크 인터페이스가 필요하다.

## 6. 요약 (42.5)

- fsck는 동작하지만 현대 시스템에서 복구가 너무 느리다.
- 저널링은 복구 시간을 **O(볼륨 크기) → O(로그 크기)** 로 줄인다.
- 가장 흔한 형태는 **ordered metadata journaling**: 저널 트래픽을 줄이면서 메타데이터와 (합리적인 수준의) 사용자 데이터 일관성을 지킨다.

## 7. 직접 해보기 — 작은 WAL을 만들고 모든 지점에서 크래시시키기

### 7.1 설계

- "디스크" = 64바이트 블록 40개 배열. 0=SB, 1=journal superblock(JSB), 2~17=저널, 19=data bitmap(문자 '0'/'1'), 20=inode 블록(inode 4개), 24~31=데이터 블록 0~7.
- 모든 디스크 쓰기를 순서대로 리스트에 담는다. **"k번째 쓰기 직후 크래시" = 초기 이미지에 앞 k개 쓰기만 적용**. 그 이미지에 `recover()` 를 돌리고 mini-fsck(`check()`)로 판정한다.
- `recover()`: JSB가 가리키는 위치부터 TxB(magic, 기대 TID) → 내용 n개 → TxE(magic, 같은 TID, **체크섬**) 를 확인하며 커밋된 tx만 수집. 1차 패스로 **revoke** 를 모으고, 2차 패스로 revoke 안 된 블록만 순서대로 redo. 끝나면 JSB를 갱신(로그 비움).
- 데이터 블록의 옛 내용은 `'x'`(쓰레기), 저널 영역의 옛 내용은 `'?'`, Da=`'a'`, Db=`'b'`.

### 7.2 코드 (`code/C42_wal_journal.c`)

```c
/*
 * C42_wal_journal.c — 아주 작은 write-ahead log(저널) + 크래시 복구 시뮬레이터
 *
 * "디스크" = 메모리 배열. 모든 디스크 쓰기를 순서대로 리스트에 담고,
 * "k번째 쓰기 직후 전원이 나갔다" = 초기 이미지에 앞의 k개 쓰기만 적용한 상태.
 * 그 상태에서 recover()(저널 재생)를 돌리고 mini-fsck(check)로 결과를 판정한다.
 *
 * 시나리오 (OSTEP 42장 예제: 파일에 블록 하나 append → I[v2], B[v2], Db)
 *   B. 저널 없이 3개 블록 중 일부만 기록된 8가지 경우
 *   A. data journaling: 매 단계마다 크래시
 *   C. TxB..TxE 를 한 번에 내보냈는데 디스크가 순서를 바꿈 (체크섬 없음/있음)
 *   D. ordered(metadata) journaling + "데이터를 커밋 뒤에 쓰는" 버그 버전
 *   E. 블록 재사용 + revoke 레코드 (ext3 의 그 악명 높은 케이스)
 *
 * build: cc -Wall -Wextra -O0 code/C42_wal_journal.c -o .work/bin/C42_wal_journal
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BS 64      /* 블록 크기(바이트) — 장난감이라 작게 */
#define NBLK 40
#define JSB 1      /* 저널 슈퍼블록 */
#define JSTART 2   /* 저널 영역 2..17 */
#define JLEN 16
#define DBMAP 19   /* data bitmap: '0'/'1' 문자 8개 */
#define INODE 20   /* inode 블록: inode 4개 */
#define DATA0 24   /* data block 0..7 = 디스크 블록 24..31 */

typedef struct { uint8_t b[NBLK][BS]; } disk_t;

typedef struct { int8_t used, size, ptr[4]; char name[10]; } inode_t; /* 16B */
typedef struct { char magic[4]; int32_t tid, n; int8_t addr[6], kind[6]; } txb_t;
typedef struct { char magic[4]; int32_t tid; uint32_t csum; } txe_t;
typedef struct { char magic[4]; int32_t start, tid; } jsb_t;

enum { K_COPY = 0, K_REVOKE = 1 };

typedef struct { int addr; uint8_t data[BS]; char label[28]; } wr_t;
typedef struct { wr_t w[64]; int n; } wlist_t;

static void add(wlist_t *wl, int addr, const uint8_t *buf, const char *label) {
    wr_t *w = &wl->w[wl->n++];
    w->addr = addr;
    memcpy(w->data, buf, BS);
    snprintf(w->label, sizeof w->label, "%s", label);
}

static uint32_t fnv1a(const uint8_t *p, size_t len) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < len; i++) { h ^= p[i]; h *= 16777619u; }
    return h;
}

/* ---------- 블록 내용 만들기 ---------- */
static void fill(uint8_t *blk, char c) { memset(blk, c, BS); }

static void mk_jsb(uint8_t *blk, int start, int tid) {
    jsb_t j = { "JSB", start, tid };
    memset(blk, 0, BS);
    memcpy(blk, &j, sizeof j);
}

static void mk_inode_blk(uint8_t *blk, const inode_t ino[4]) {
    memset(blk, 0, BS);
    memcpy(blk, ino, 4 * sizeof(inode_t));
}

static void get_inodes(const disk_t *d, inode_t ino[4]) {
    memcpy(ino, d->b[INODE], 4 * sizeof(inode_t));
}

/* 트랜잭션 하나를 저널에 기록하는 쓰기들을 wl 에 추가. 다음 저널 위치를 반환 */
typedef struct { int addr, kind; const uint8_t *data; const char *name; } item_t;

static int journal_tx(wlist_t *wl, int pos, int tid, const item_t *it, int n) {
    uint8_t blk[BS], content[6 * BS];
    txb_t b;
    memset(&b, 0, sizeof b);
    memcpy(b.magic, "TXB", 4);
    b.tid = tid;
    b.n = n;
    for (int i = 0; i < n; i++) { b.addr[i] = (int8_t)it[i].addr; b.kind[i] = (int8_t)it[i].kind; }
    memset(blk, 0, BS);
    memcpy(blk, &b, sizeof b);
    char lab[28];
    snprintf(lab, sizeof lab, "J: TxB(tid=%d)", tid);
    add(wl, pos, blk, lab);
    for (int i = 0; i < n; i++) {
        memcpy(content + i * BS, it[i].data, BS);
        snprintf(lab, sizeof lab, "J: %s", it[i].name);
        add(wl, pos + 1 + i, it[i].data, lab);
    }
    txe_t e = { "TXE", tid, fnv1a(content, (size_t)n * BS) };
    memset(blk, 0, BS);
    memcpy(blk, &e, sizeof e);
    snprintf(lab, sizeof lab, "J: TxE(tid=%d)", tid);
    add(wl, pos + 1 + n, blk, lab);
    return pos + 2 + n;
}

/* ---------- 복구: 저널 스캔 → (revoke 수집) → 재생 ---------- */
static int recover(disk_t *d, int verify_csum, char *log, size_t loglen) {
    jsb_t j;
    memcpy(&j, d->b[JSB], sizeof j);
    int pos = j.start, expect = j.tid, ntx = 0, nrev = 0, replayed = 0, skipped = 0;
    int txpos[8], txtid[8], revaddr[16], revtid[16];
    const char *why = "end of log";
    log[0] = 0;
    while (pos < JSTART + JLEN) {
        txb_t b;
        txe_t e;
        memcpy(&b, d->b[pos], sizeof b);
        if (memcmp(b.magic, "TXB", 4) != 0 || b.tid != expect) { why = "no more TxB"; break; }
        if (pos + 1 + b.n >= JSTART + JLEN) { why = "truncated"; break; }
        memcpy(&e, d->b[pos + 1 + b.n], sizeof e);
        if (memcmp(e.magic, "TXE", 4) != 0 || e.tid != b.tid) { why = "TxE missing (uncommitted)"; break; }
        if (verify_csum && fnv1a(d->b[pos + 1], (size_t)b.n * BS) != e.csum) {
            why = "checksum MISMATCH -> discard";
            break;
        }
        txpos[ntx] = pos; txtid[ntx] = b.tid; ntx++;
        for (int i = 0; i < b.n; i++)
            if (b.kind[i] == K_REVOKE) {
                int32_t a;
                memcpy(&a, d->b[pos + 1 + i], sizeof a);
                revaddr[nrev] = a; revtid[nrev] = b.tid; nrev++;
            }
        pos += b.n + 2;
        expect++;
    }
    for (int t = 0; t < ntx; t++) {
        txb_t b;
        memcpy(&b, d->b[txpos[t]], sizeof b);
        for (int i = 0; i < b.n; i++) {
            if (b.kind[i] != K_COPY) continue;
            int revoked = 0;
            for (int r = 0; r < nrev; r++)
                if (revaddr[r] == b.addr[i] && revtid[r] > txtid[t]) revoked = 1;
            if (revoked) { skipped++; continue; }
            memcpy(d->b[b.addr[i]], d->b[txpos[t] + 1 + i], BS);
            replayed++;
        }
    }
    mk_jsb(d->b[JSB], pos, expect); /* 재생 끝 → 저널 비움 */
    snprintf(log, loglen, "%d tx committed, %d blk replayed%s%s (stop: %s)", ntx, replayed,
             skipped ? ", revoked-skip=" : "", skipped ? (skipped == 1 ? "1" : "n") : "", why);
    return ntx;
}

/* ---------- mini fsck: append 예제용 (inode 2 + data bitmap + data) ---------- */
static void check(const disk_t *d, char *out, size_t len) {
    inode_t ino[4];
    get_inodes(d, ino);
    const inode_t *f = &ino[2];
    char issues[160] = "";
    int bad = 0, garbage = 0;
    for (int i = 0; i < 4; i++) {
        int p = f->ptr[i];
        if (p < 0) continue;
        if (d->b[DBMAP][p] != '1') {
            snprintf(issues + strlen(issues), sizeof issues - strlen(issues),
                     " [inode->blk%d but bitmap=0]", p);
            bad = 1;
        }
        char c = (char)d->b[DATA0 + p][0];
        if (c != 'a' && c != 'b') garbage = 1;
    }
    for (int k = 0; k < 8; k++) {
        if (d->b[DBMAP][k] != '1') continue;
        int pointed = 0;
        for (int i = 0; i < 4; i++) if (f->ptr[i] == k) pointed = 1;
        if (!pointed) {
            snprintf(issues + strlen(issues), sizeof issues - strlen(issues),
                     " [bitmap blk%d=1 but no inode -> leak]", k);
            bad = 1;
        }
    }
    if (bad) snprintf(out, len, "INCONSISTENT%s", issues);
    else if (garbage) snprintf(out, len, "consistent but GARBAGE (blk5='%c')", d->b[DATA0 + 5][0]);
    else if (f->size == 1) snprintf(out, len, "OK  old state (Da)");
    else snprintf(out, len, "OK  new state (Da+Db)");
}

/* ---------- 초기 디스크 이미지 & 새 블록들 ---------- */
static disk_t base;
static uint8_t Iv2[BS], Bv2[BS], Db[BS];

static void init_base(void) {
    memset(&base, 0, sizeof base);
    for (int i = 0; i < JLEN; i++) fill(base.b[JSTART + i], '?'); /* 예전 저널 찌꺼기 */
    mk_jsb(base.b[JSB], JSTART, 1);
    memset(base.b[DBMAP], 0, BS);
    memcpy(base.b[DBMAP], "00001000", 8);
    inode_t ino[4];
    memset(ino, 0, sizeof ino);
    for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) ino[i].ptr[k] = -1;
    ino[2] = (inode_t){ 1, 1, { 4, -1, -1, -1 }, "file" };
    mk_inode_blk(base.b[INODE], ino);
    for (int i = 0; i < 8; i++) fill(base.b[DATA0 + i], 'x'); /* 쓰레기(이전 내용) */
    fill(base.b[DATA0 + 4], 'a');                          /* Da */

    ino[2] = (inode_t){ 1, 2, { 4, 5, -1, -1 }, "file" };
    mk_inode_blk(Iv2, ino);
    memset(Bv2, 0, BS);
    memcpy(Bv2, "00001100", 8);
    fill(Db, 'b');
}

static void apply_prefix(disk_t *d, const wlist_t *wl, int k) {
    *d = base;
    for (int i = 0; i < k; i++) memcpy(d->b[wl->w[i].addr], wl->w[i].data, BS);
}

static void crash_sweep(const char *title, const wlist_t *wl) {
    printf("\n== %s ==\n", title);
    printf("%-3s %-22s %-64s %s\n", "k", "last write persisted", "recovery", "fsck result");
    for (int k = 0; k <= wl->n; k++) {
        disk_t d;
        char rlog[120], res[200];
        apply_prefix(&d, wl, k);
        recover(&d, 1, rlog, sizeof rlog);
        check(&d, res, sizeof res);
        printf("%-3d %-22s %-64s %s\n", k, k ? wl->w[k - 1].label : "(nothing)", rlog, res);
    }
}

int main(void) {
    init_base();
    uint8_t blk[BS];

    /* ---- B. 저널 없음: {Db, I[v2], B[v2]} 의 8가지 부분집합 ---- */
    printf("== B. no journal: which of the 3 writes reached disk? ==\n");
    for (int m = 0; m < 8; m++) {
        disk_t d = base;
        char res[200];
        if (m & 1) memcpy(d.b[DATA0 + 5], Db, BS);
        if (m & 2) memcpy(d.b[INODE], Iv2, BS);
        if (m & 4) memcpy(d.b[DBMAP], Bv2, BS);
        check(&d, res, sizeof res);
        printf("Db=%d I[v2]=%d B[v2]=%d  -> %s\n", m & 1, (m >> 1) & 1, (m >> 2) & 1, res);
    }

    /* ---- A. data journaling ---- */
    wlist_t A = { .n = 0 };
    item_t items[3] = {
        { INODE, K_COPY, Iv2, "I[v2]" },
        { DBMAP, K_COPY, Bv2, "B[v2]" },
        { DATA0 + 5, K_COPY, Db, "Db" },
    };
    int next = journal_tx(&A, JSTART, 1, items, 3);
    add(&A, INODE, Iv2, "CP: I[v2]");
    add(&A, DBMAP, Bv2, "CP: B[v2]");
    add(&A, DATA0 + 5, Db, "CP: Db");
    mk_jsb(blk, next, 2);
    add(&A, JSB, blk, "free: JSB.start++");
    crash_sweep("A. data journaling, crash after k-th write", &A);

    /* ---- C. 5블록을 한꺼번에 issue → 디스크가 Db(저널본)를 늦게 씀 ---- */
    printf("\n== C. TxB,I,B,Db,TxE issued together; disk persisted TxE before J:Db ==\n");
    {
        disk_t d = base;
        char rlog[120], res[200];
        /* A 리스트의 0..4 중 J:Db(index 3) 만 빠진 채 전원 차단 */
        for (int i = 0; i < 5; i++)
            if (i != 3) memcpy(d.b[A.w[i].addr], A.w[i].data, BS);
        disk_t d2 = d;
        recover(&d, 0, rlog, sizeof rlog);
        check(&d, res, sizeof res);
        printf("no checksum : %-46s -> %s\n", rlog, res);
        recover(&d2, 1, rlog, sizeof rlog);
        check(&d2, res, sizeof res);
        printf("checksum    : %-46s -> %s\n", rlog, res);
    }

    /* ---- D. ordered (metadata) journaling ---- */
    wlist_t D = { .n = 0 };
    add(&D, DATA0 + 5, Db, "Db -> final location");
    next = journal_tx(&D, JSTART, 1, items, 2); /* I[v2], B[v2] 만 저널 */
    add(&D, INODE, Iv2, "CP: I[v2]");
    add(&D, DBMAP, Bv2, "CP: B[v2]");
    mk_jsb(blk, next, 2);
    add(&D, JSB, blk, "free: JSB.start++");
    crash_sweep("D1. ordered journaling (data first), crash after k-th write", &D);

    wlist_t Dbug = { .n = 0 };
    next = journal_tx(&Dbug, JSTART, 1, items, 2);
    add(&Dbug, DATA0 + 5, Db, "Db (AFTER commit!)");
    add(&Dbug, INODE, Iv2, "CP: I[v2]");
    add(&Dbug, DBMAP, Bv2, "CP: B[v2]");
    crash_sweep("D2. BUGGY metadata journaling (data after commit)", &Dbug);

    /* ---- E. 블록 재사용 + revoke ---- */
    printf("\n== E. block reuse: dir foo's block (data6) freed, reused by file foobar ==\n");
    for (int use_revoke = 0; use_revoke <= 1; use_revoke++) {
        wlist_t E = { .n = 0 };
        inode_t ino[4];
        uint8_t ib1[BS], ib2[BS], ib3[BS], dfoo[BS], dfoobar[BS], rv[BS];
        memset(ino, 0, sizeof ino);
        for (int i = 0; i < 4; i++) for (int k = 0; k < 4; k++) ino[i].ptr[k] = -1;
        ino[1] = (inode_t){ 1, 1, { 6, -1, -1, -1 }, "foo/" };
        mk_inode_blk(ib1, ino);
        fill(dfoo, 'D'); /* 디렉터리 내용 = 메타데이터 → 저널에 들어감 */
        item_t t1[2] = { { INODE, K_COPY, ib1, "I[foo]" }, { DATA0 + 6, K_COPY, dfoo, "D[foo]" } };
        int p = journal_tx(&E, JSTART, 1, t1, 2);
        add(&E, INODE, ib1, "CP: I[foo]");
        add(&E, DATA0 + 6, dfoo, "CP: D[foo]");
        /* rm -r foo : 블록 6 해제 (+ revoke 레코드) */
        ino[1] = (inode_t){ 0, 0, { -1, -1, -1, -1 }, "" };
        mk_inode_blk(ib2, ino);
        memset(rv, 0, BS);
        int32_t a = DATA0 + 6;
        memcpy(rv, &a, sizeof a);
        item_t t2[2] = { { INODE, K_COPY, ib2, "I[-foo]" }, { 0, K_REVOKE, rv, "REVOKE blk" } };
        p = journal_tx(&E, p, 2, t2, use_revoke ? 2 : 1);
        add(&E, INODE, ib2, "CP: I[-foo]");
        /* foobar 생성: 블록 6 재사용, 사용자 데이터는 저널 안 함(ordered) */
        fill(dfoobar, 'F');
        add(&E, DATA0 + 6, dfoobar, "foobar data");
        ino[3] = (inode_t){ 1, 1, { 6, -1, -1, -1 }, "foobar" };
        mk_inode_blk(ib3, ino);
        item_t t3[1] = { { INODE, K_COPY, ib3, "I[foobar]" } };
        journal_tx(&E, p, 3, t3, 1);
        add(&E, INODE, ib3, "CP: I[foobar]");
        /* JSB 는 아직 tx1 을 가리킴 (체크포인트 후 free 전에) → 크래시 */
        disk_t d;
        char rlog[120];
        apply_prefix(&d, &E, E.n);
        printf("before crash : data6='%c' (foobar)\n", d.b[DATA0 + 6][0]);
        recover(&d, 1, rlog, sizeof rlog);
        inode_t after[4];
        get_inodes(&d, after);
        printf("revoke=%s : %s\n              -> data6='%c' owner=%s  %s\n", use_revoke ? "ON " : "OFF", rlog,
               d.b[DATA0 + 6][0], after[3].name,
               d.b[DATA0 + 6][0] == 'F' ? "OK" : "CORRUPTED: old dir bytes replayed over foobar!");
    }
    return 0;
}
```

### 7.3 실제 출력

```text
$ cc -Wall -Wextra -O0 -pthread code/C42_wal_journal.c -o .work/bin/C42_wal_journal && .work/bin/C42_wal_journal
== B. no journal: which of the 3 writes reached disk? ==
Db=0 I[v2]=0 B[v2]=0  -> OK  old state (Da)
Db=1 I[v2]=0 B[v2]=0  -> OK  old state (Da)
Db=0 I[v2]=1 B[v2]=0  -> INCONSISTENT [inode->blk5 but bitmap=0]
Db=1 I[v2]=1 B[v2]=0  -> INCONSISTENT [inode->blk5 but bitmap=0]
Db=0 I[v2]=0 B[v2]=1  -> INCONSISTENT [bitmap blk5=1 but no inode -> leak]
Db=1 I[v2]=0 B[v2]=1  -> INCONSISTENT [bitmap blk5=1 but no inode -> leak]
Db=0 I[v2]=1 B[v2]=1  -> consistent but GARBAGE (blk5='x')
Db=1 I[v2]=1 B[v2]=1  -> OK  new state (Da+Db)

== A. data journaling, crash after k-th write ==
k   last write persisted   recovery                                                         fsck result
0   (nothing)              0 tx committed, 0 blk replayed (stop: no more TxB)               OK  old state (Da)
1   J: TxB(tid=1)          0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
2   J: I[v2]               0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
3   J: B[v2]               0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
4   J: Db                  0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
5   J: TxE(tid=1)          1 tx committed, 3 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
6   CP: I[v2]              1 tx committed, 3 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
7   CP: B[v2]              1 tx committed, 3 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
8   CP: Db                 1 tx committed, 3 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
9   free: JSB.start++      0 tx committed, 0 blk replayed (stop: no more TxB)               OK  new state (Da+Db)

== C. TxB,I,B,Db,TxE issued together; disk persisted TxE before J:Db ==
no checksum : 1 tx committed, 3 blk replayed (stop: no more TxB) -> consistent but GARBAGE (blk5='?')
checksum    : 0 tx committed, 0 blk replayed (stop: checksum MISMATCH -> discard) -> OK  old state (Da)

== D1. ordered journaling (data first), crash after k-th write ==
k   last write persisted   recovery                                                         fsck result
0   (nothing)              0 tx committed, 0 blk replayed (stop: no more TxB)               OK  old state (Da)
1   Db -> final location   0 tx committed, 0 blk replayed (stop: no more TxB)               OK  old state (Da)
2   J: TxB(tid=1)          0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
3   J: I[v2]               0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
4   J: B[v2]               0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
5   J: TxE(tid=1)          1 tx committed, 2 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
6   CP: I[v2]              1 tx committed, 2 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
7   CP: B[v2]              1 tx committed, 2 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
8   free: JSB.start++      0 tx committed, 0 blk replayed (stop: no more TxB)               OK  new state (Da+Db)

== D2. BUGGY metadata journaling (data after commit) ==
k   last write persisted   recovery                                                         fsck result
0   (nothing)              0 tx committed, 0 blk replayed (stop: no more TxB)               OK  old state (Da)
1   J: TxB(tid=1)          0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
2   J: I[v2]               0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
3   J: B[v2]               0 tx committed, 0 blk replayed (stop: TxE missing (uncommitted)) OK  old state (Da)
4   J: TxE(tid=1)          1 tx committed, 2 blk replayed (stop: no more TxB)               consistent but GARBAGE (blk5='x')
5   Db (AFTER commit!)     1 tx committed, 2 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
6   CP: I[v2]              1 tx committed, 2 blk replayed (stop: no more TxB)               OK  new state (Da+Db)
7   CP: B[v2]              1 tx committed, 2 blk replayed (stop: no more TxB)               OK  new state (Da+Db)

== E. block reuse: dir foo's block (data6) freed, reused by file foobar ==
before crash : data6='F' (foobar)
revoke=OFF : 3 tx committed, 4 blk replayed (stop: no more TxB)
              -> data6='D' owner=foobar  CORRUPTED: old dir bytes replayed over foobar!
before crash : data6='F' (foobar)
revoke=ON  : 3 tx committed, 3 blk replayed, revoked-skip=1 (stop: no more TxB)
              -> data6='F' owner=foobar  OK
```

### 7.4 해석

- **B (저널 없음)**: 2.1절 표의 6가지 + trivial 2가지가 그대로 나온다. 특히 `Db=0 I=1 B=1` → **"consistent but GARBAGE"** — fsck가 고칠 수 없는 바로 그 경우다.
- **A (data journaling)**: k=0~4(TxE 전)는 전부 **옛 상태**, k=5(TxE가 디스크에 감) 이후는 전부 **새 상태**. 중간 상태가 하나도 없다 = **원자성**. 커밋 지점은 정확히 "TxE 한 섹터가 기록된 순간"이다. k=6~8(checkpoint 도중)은 재생으로 같은 블록을 한 번 더 쓰지만 결과는 같다(멱등). k=9는 JSB가 tx 뒤를 가리키므로 재생할 게 없고 본체가 이미 새 상태.
- **C (한꺼번에 issue + 디스크 재정렬)**: 체크섬 없이 TID만 보면 tx가 유효해 보여 **로그 칸의 옛 내용 `'?'` 가 블록 5에 복사**된다. TxE에 내용 체크섬을 넣으면 **MISMATCH → 버림 → 옛 상태**. ext4 `journal_checksum` 이 2단계 대기를 없앨 수 있는 이유.
- **D1 (ordered)**: 데이터를 먼저 쓰면 어느 k에서도 쓰레기가 없다. k=1은 "Db만 기록" — 2.1절의 첫 번째 행과 같은 무해한 상태.
- **D2 (데이터를 커밋 뒤에)**: k=4(커밋 직후)에서 **consistent but GARBAGE**. 메타데이터 일관성은 저널이 지키지만 "가리켜지는 것 먼저" 규칙을 어기면 데이터가 쓰레기가 된다.
- **E (revoke)**: revoke 없이 재생하면 tx1의 옛 디렉터리 내용 `'D'` 가 foobar의 데이터 `'F'` 를 덮는다 — Tweedie의 악몽. revoke ON이면 tx1의 그 블록 1개를 건너뛴다(`revoked-skip=1`).

### 7.5 fsck.py 시뮬레이터

```text
cd .tools/ostep-homework/file-journaling
python3 fsck.py -D -p        # 손상 없이, 파일 목록까지
```

```text
Final state of file system:

inode bitmap 1000100010000101
inodes       [d a:0 r:4] [] [] [] [d a:12 r:2] [] [] [] [d a:6 r:2] [] [] [] [] [f a:-1 r:2] [] [f a:-1 r:1] 
data bitmap  1000001000001000
data         [(.,0) (..,0) (g,8) (w,4) (m,13) (z,13)] [] [] [] [] [] [(.,8) (..,0) (s,15)] [] [] [] [] [] [(.,4) (..,0)] [] [] [] 

Summary of files, directories::
  Files:       ['/m', '/z', '/g/s']
  Directories: ['/', '/g', '/w']
```

(`ARG` 줄 생략.) 읽는 법: 루트(inode 0, 블록 0)에 g(8), w(4), m(13), z(13). **m과 z가 같은 inode 13 → 하드 링크라서 r:2**. 디렉터리의 r은 "2 + 하위 디렉터리 수"처럼 쓰여 루트 r:4 (g, w 두 개).

이제 손상을 하나씩 넣고 `-c` 로 정답 확인:

```text
python3 fsck.py -S 1 -c
CORRUPTION::INODE BITMAP corrupt bit 13
inode bitmap 1000100010000001          ← bit 13 이 0 인데 inode 13 [f a:-1 r:2] 은 m, z 가 가리킴

python3 fsck.py -S 3 -c
CORRUPTION::INODE 15 refcnt increased
inodes ... [f a:-1 r:2]                ← inode 15 (/g/s) 를 가리키는 엔트리는 하나뿐인데 r:2

python3 fsck.py -S 19 -c
CORRUPTION::INODE 8 refcnt decreased
inodes ... [d a:6 r:1]                 ← 디렉터리 /g 의 r 이 2 → 1

python3 fsck.py -S 5 -c
CORRUPTION::INODE 8 with directory [('.', 8), ('..', 0), ('s', 15)]:
  entry ('s', 15) altered to refer to different name (y)
data ... [(.,8) (..,0) (y,15)]         ← 이름 s → y
```

(출력 일부 발췌, 손상 지점에 `←` 주석을 덧붙임.) fsck 관점 판정:

- **-S 1 (inode bitmap 비트 꺼짐)**: 탐지·수리 가능 — inode를 신뢰해 bitmap을 다시 만든다(3절 2번 단계).
- **-S 3 / -S 19 (링크 수 틀림)**: 탐지·수리 가능 — 디렉터리 트리를 돌며 다시 센 값으로 고친다(3절 4번 단계).
- **-S 5 (엔트리 이름 변경)**: **탐지 불가** — `y` 도 완벽히 합법적인 이름이다. 메타데이터가 "일관"되기만 하면 fsck는 그것이 "맞는지"는 모른다. 원문이 말한 fsck의 한계("make sure the file system metadata is internally consistent")를 그대로 보여 준다.

## 8. 펌웨어 엔지니어의 눈으로

- **이 장 전체가 Don의 SSD 전원 손실 복구(SPOR) 그 자체다.** FTL 메타데이터 저널링은 OSTEP의 WAL과 1:1로 대응한다: L2P 매핑 변경을 **delta log(저널)** 로 NAND에 append → 주기적으로 **맵 스냅샷(checkpoint)** → 부팅 시 마지막 스냅샷 로드 + 이후 delta를 **redo**. "커밋 지점 = TxE 한 섹터"에 해당하는 것이 **로그 페이지의 커밋 마커/시퀀스 번호 + CRC**이고, 복구 스캔은 "시퀀스가 연속이고 CRC가 맞는 마지막 페이지"에서 멈춘다 — 7절 `recover()` 의 `expect` TID 검사와 체크섬 검사가 정확히 그 로직이다.
- **트랜잭션 체크섬 ↔ NAND 페이지 단위 ECC/CRC.** 디스크의 "512B 원자 쓰기" 가정은 NAND에서 깨진다: 전원이 프로그램 도중 나가면 **부분 프로그램된 페이지**가 남고, 심지어 MLC/TLC에서는 **이미 커밋된 짝 페이지(lower page)까지 깨질 수 있다**(paired-page corruption). 그래서 FW는 TxE 원자성을 가정하지 않고 **체크섬 + 시퀀스 번호로 마지막 유효 엔트리를 판정**하고, 커밋 로그를 **SLC 모드 블록**이나 lower page만 쓰는 영역에 두기도 한다.
- **ordered mode의 "데이터 먼저" = FTL의 "NAND 프로그램 완료 후 L2P 갱신".** 호스트 데이터 페이지가 NAND에 확실히 프로그램된 다음에야 그 위치를 가리키는 매핑 delta를 로그에 넣는다. 순서를 어기면 복구 후 LBA가 **지워진/옛 페이지**를 가리킨다 — 7.4의 D2 "consistent but GARBAGE"와 동일한 버그 클래스.
- **revoke ↔ GC 후 블록 재사용.** GC로 erase된 블록이 재할당됐는데 replay 로그에 "옛 매핑"이 남아 있으면, 재생 시 LBA가 **새 데이터가 들어간 같은 물리 위치를 옛 의미로** 가리키게 된다. FW는 블록마다 **erase/할당 시퀀스 번호**를 두고 "로그 엔트리의 시퀀스 < 블록의 현재 할당 시퀀스면 무시"하는 식으로 revoke와 같은 효과를 낸다.
- **write barrier/FLUSH/FUA 무시 = 신뢰의 문제.** OS 저널링은 디바이스가 FLUSH를 정직하게 처리한다고 믿는다. 반대로 SSD FW 입장에서는 **Flush 완료를 보고하기 전에** 쓰기 버퍼 + 관련 매핑 + (필요 시) 로그까지 NAND에 영속화해야 하며, PLP 커패시터가 있으면 "volatile write cache 없음"으로 광고해 FLUSH를 사실상 no-op으로 만들 수 있다. 면접 단골: "Flush를 빨리 처리하려고 무시하면 무슨 일이?" → 바로 이 장의 C 시나리오.
- **fsck ↔ FW의 full-scan 복구.** 저널이 깨졌을 때의 최후 수단으로 SSD FW도 **모든 블록의 OOB(spare) 영역에 기록된 LBA + 시퀀스 번호를 스캔해 L2P를 재구성**한다 — fsck처럼 O(용량)이라 수십 초~분이 걸린다. 그래서 정상 경로는 저널(O(로그))이고 full-scan은 비상용. 7.5의 fsck.py `-S 5` 처럼 "합법적으로 보이는 오류"는 스캔으로도 못 잡는다는 한계도 같다.

## 9. 면접 질문

### Q1. 파일 append 하나가 왜 크래시 일관성 문제를 일으키나? 가능한 부분 쓰기 결과를 설명하라.
<details>
<summary>답 보기</summary>

**inode(I[v2]), data bitmap(B[v2]), data block(Db)** 세 블록을 써야 하는데 디스크는 하나씩 커밋한다. Db만 → 무해(데이터만 잃음). I만 → inode↔bitmap **불일치** + 쓰레기 읽기. B만 → **space leak**. I+B → 메타데이터는 **일관되지만 쓰레기**(fsck도 못 고침). I+Db → bitmap 불일치(이중 할당 위험). B+Db → leak + 어느 파일 것인지 모름. 목표는 한 일관 상태에서 다른 일관 상태로의 **원자적** 전이.

</details>

### Q2. 저널링 프로토콜을 단계별로 설명하고, 각 단계 사이에 왜 대기(barrier)가 필요한가?
<details>
<summary>답 보기</summary>

(1) **journal write**: TxB + 내용 → 완료 대기, (2) **commit**: TxE(512B, 원자적) → 완료 대기, (3) **checkpoint**: 최종 위치에 쓰기, (4) **free**: journal superblock 갱신. (1)→(2) 대기가 없으면 디스크가 내부 재정렬로 TxE를 먼저 써서 **TxB·TxE는 맞는데 내용은 쓰레기인 tx**가 재생된다. (2)→(3) 대기가 없으면 커밋 안 된 상태에서 제자리가 바뀌어 원자성이 깨진다. 디스크 쓰기 캐시 때문에 "완료 인터럽트"만으로는 부족하고 **FLUSH/FUA/barrier** 가 필요하다. 복구는 커밋된 tx만 **redo**(멱등).

</details>

### Q3. ext3/4의 data=journal, data=ordered, data=writeback 차이는? 기본값이 ordered인 이유는?
<details>
<summary>답 보기</summary>

**journal**: 데이터+메타데이터 모두 저널 → 가장 안전, 데이터 **2번 쓰기**. **ordered**: 메타데이터만 저널, 단 **데이터를 커밋 전에 제자리에 완료** → 메타데이터가 쓰레기를 가리키는 일 없음. **writeback**: 메타데이터만 저널, 데이터 순서 무관 → 크래시 후 파일에 옛/쓰레기 데이터가 보일 수 있음. 세 모드 모두 **메타데이터 일관성**은 보장. ordered가 기본인 이유: 쓰기 트래픽 대부분인 데이터를 한 번만 쓰면서 "가리켜지는 것 먼저" 규칙으로 쓰레기 노출을 막는 균형점.

</details>

### Q4. 저널링에서 블록 재사용이 왜 문제이고, revoke 레코드는 어떻게 동작하나?
<details>
<summary>답 보기</summary>

metadata journaling에서 디렉터리 블록(메타데이터) 1000이 tx1에 로그됨 → 디렉터리 삭제로 1000 해제 → 새 파일의 **사용자 데이터**(저널 안 됨)가 1000을 재사용 → 크래시 → 재생이 tx1의 **옛 디렉터리 내용으로 1000을 덮어씀**. ext3는 삭제 시 **revoke 레코드**를 저널에 쓰고, 복구 시 **먼저 revoke를 모두 수집**한 뒤, revoke tx보다 **오래된** tx의 해당 블록은 재생하지 않는다. 대안: 삭제가 checkpoint될 때까지 블록 재사용 금지.

</details>

### Q5. 트랜잭션 체크섬은 어떤 문제를 풀고, 무엇을 가정하나?
<details>
<summary>답 보기</summary>

TxB+내용 완료를 기다렸다가 TxE를 쓰는 **추가 대기(회전 지연)** 를 없앤다. TxE에 트랜잭션 내용의 체크섬을 넣고 **전부 한 번에 issue**; 복구 시 재계산한 값이 다르면 쓰는 도중 크래시로 보고 버린다. 덤으로 저널 읽기 자체가 체크섬으로 보호된다(ext4). 가정: 체크섬 충돌 확률이 무시할 만하고(CRC32c), **checkpoint는 여전히 커밋이 영속화된 뒤**에 해야 한다(그 사이 FLUSH는 필요).

</details>

### Q6. SSD FTL의 전원 손실 복구를 OS 저널링과 비교해 설명해 보라.
<details>
<summary>답 보기</summary>

같은 구조: 매핑 변경을 **로그(delta)** 로 append, 주기적 **checkpoint(맵 스냅샷)**, 부팅 시 **스냅샷 + delta redo**, 커밋 판정은 **시퀀스 번호 + CRC**. 차이: (1) NAND는 **부분 프로그램/paired page** 때문에 "섹터 원자 쓰기"를 가정 못 함 → 체크섬 필수, (2) "데이터 먼저, 매핑 나중" = ordered mode, (3) GC로 인한 블록 재사용 = revoke 문제 → 블록 할당 시퀀스로 옛 엔트리 무시, (4) 최후 수단은 OOB의 LBA를 스캔하는 full rebuild = fsck. PLP가 있으면 DRAM 상태를 덤프해 복구 경로를 단순화할 수 있다.

</details>

## 10. 자가 점검 & 숙제

### 퀴즈 1. data journaling에서 TxE 쓰기가 끝난 직후, checkpoint 전에 전원이 나갔다. 재부팅 후 상태는?
<details>
<summary>답 보기</summary>

**새 상태**. 트랜잭션은 커밋됐으므로 복구가 저널의 I[v2], B[v2], Db를 최종 위치에 redo 한다 (7.3 출력 A의 k=5).

</details>

### 퀴즈 2. ordered mode에서 "데이터 쓰기 완료"를 TxB 쓰기 전에 기다릴 필요가 있나?
<details>
<summary>답 보기</summary>

**없다.** 데이터, TxB, 메타데이터를 동시에 issue해도 된다. 요구 조건은 **TxE(커밋)를 issue하기 전에** 데이터와 저널 내용이 **모두 완료**되는 것뿐이다.

</details>

### 퀴즈 3. 저널이 128MB, 트랜잭션이 평균 64KB, 초당 200개 커밋된다. checkpoint/free 가 전혀 안 되면 몇 초 만에 로그가 차나? fsck 대비 복구 시간의 차원은?
<details>
<summary>답 보기</summary>

64KB × 200 = 12.8MB/s → 128MB / 12.8MB/s = **10초**. 그래서 circular log + 주기적 checkpoint가 필수. 복구 시간은 **O(로그 크기)**(최대 128MB 읽기, 수백 ms~수 초) vs fsck **O(볼륨 크기)**(TB급이면 수십 분 이상).

</details>

### 퀴즈 4. fsck가 "고칠 수 없는" 상황 두 가지를 이 장에서 찾아라.
<details>
<summary>답 보기</summary>

(1) I[v2]+B[v2]만 쓰인 경우: 메타데이터는 일관되지만 데이터가 쓰레기 — fsck는 데이터 내용을 모른다. (2) fsck.py `-S 5`: 디렉터리 엔트리 이름이 다른 합법적 이름으로 바뀐 경우 — 일관성 위반이 아니므로 탐지 불가. (덤: Db+B만 쓰인 경우 블록은 회수되지만 사용자 데이터는 복구 불가.)

</details>

### 원문 Homework 중 꼭 해볼 것

이 판(v0.91) 원문에는 42장 Homework 문항이 없지만, `fsck.py` README의 흐름대로:

- **`-S` 1~20 을 바꿔 가며 `-c` 없이 손상 찾기**: 비트맵 vs inode vs 디렉터리 중 **무엇을 신뢰해 무엇을 고치는지**(3절의 fsck 단계)를 몸에 익히는 문제.
- **같은 `-s`(파일 시스템)에 다른 `-S`(손상)**: 어떤 손상은 수리 가능하고(-S 1, 3, 19) 어떤 손상은 탐지조차 안 되는지(-S 5) 분류해 보는 문제.
- **`code/C42_wal_journal.c` 수정 과제**: (a) revoke 대신 "checkpoint 전 블록 재사용 금지"로 E를 고쳐 보기, (b) TxE 를 쓰지 않고 TxB 에만 체크섬을 넣으면 무엇이 깨지는지 확인.

## 11. 다음으로

- 아예 제자리에 쓰지 않는다면? → [Ch.43 LFS](2026-09-30_C43_lfs.md) (COW, 체크포인트 영역 2개, roll-forward)
- 크래시가 아니라 디스크가 **조용히 틀린 데이터**를 돌려주면? → [Ch.44 데이터 무결성](2026-09-30_C44_data_integrity.md)
- 같은 문제를 SSD 내부에서: → [부록 I Flash SSD](2026-09-30_C0I_flash_ssd.md)
- 복습: [Ch.40 vsfs의 create/write I/O 타임라인](2026-09-30_C40_file_system_implementation.md)
