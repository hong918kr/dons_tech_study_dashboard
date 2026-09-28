# B7. 락 없이 공유하는 세 가지 패턴 — SPSC 링 · seqlock · 포인터 교체

> **이 노트를 읽고 나면**:
> - SPSC 링버퍼 · seqlock · 포인터 교체+refcount 를 각각 **언제 쓰고 언제 쓰면 안 되는지** 말할 수 있다
> - 세 패턴의 `acquire`/`release` 짝을 손으로 짚어가며 왜 거기여야 하는지 설명할 수 있다
> - 포인터 교체에서 use-after-free 창이 어디에 열리는지 그리고, 닫는 방법 세 가지를 댈 수 있다
>
> **선행**: [B6. 원자 연산과 메모리 순서](B6_atomics_and_memory_order.md) — 이 노트는 B6 없이는 읽히지 않는다
> **이 개념을 쓰는 문제**: [01_gps_fix_cache](../01_gps_fix_cache/question_note) Part 1 (seqlock) · [06_config_publish_rollback](../06_config_publish_rollback/question_note) Part 1 (포인터 교체 + refcount) · [05_frame_latest_and_replay](../05_frame_latest_and_replay/question_note) Part 1 (더블버퍼 + refcount) · [03_event_tailer_shutdown](../03_event_tailer_shutdown/question_note) (링버퍼 + drop 정책)

---

## 1. 왜 이게 필요한가

mutex는 거의 항상 맞다. 그런데 이 세 경우에는 mutex가 **정말로** 문제가 된다.

- **ISR / 시그널 핸들러**에서 큐에 넣어야 한다. 인터럽트가 락을 잡은 스레드를 선점했으면
  그 락은 영원히 안 풀린다. 즉시 데드락이다.
- **writer가 절대 기다려서는 안 된다.** `01_gps_fix_cache`의 샘플러는 10 Hz로 GPS를 읽는다.
  reader(웹 UI 스레드)가 락을 잡은 채 선점되면 샘플러가 멈춘다. RT 커널에서는 그게
  priority inversion이다 — 웹 UI 때문에 GPS가 밀린다.
- **payload가 크다.** 4 MB 프레임을 락 안에서 복사하면 락을 수 ms 잡는다.

이 세 상황에 각각 대응하는 패턴이 하나씩 있다. 이 노트가 그 셋이다.
**가장 중요한 문장을 먼저 적는다**: 세 패턴 모두 대가가 있고, 그 대가를 말할 수 없으면
쓰면 안 된다. `06`의 모범답안조차 결국 **아주 짧은 mutex**를 고른다.

## 2. 그림으로 먼저

```svg
<svg viewBox="0 0 720 300" role="img" aria-label="세 가지 패턴 지도">
  <text x="360" y="20" text-anchor="middle">세 패턴 — 무엇을 공유하느냐가 패턴을 정한다</text>
  <rect class="box" x="10" y="40" width="222" height="240" rx="10"/>
  <text x="121" y="64" text-anchor="middle">① SPSC 링버퍼</text>
  <text class="lbl" x="121" y="82" text-anchor="middle">스트림 — 하나도 잃으면 안 됨</text>
  <rect class="fill-soft" x="26" y="96" width="60" height="30" rx="6"/><text class="lbl" x="56" y="116" text-anchor="middle">생산자1</text>
  <rect class="box" x="96" y="96" width="60" height="30" rx="6"/><text class="lbl" x="126" y="116" text-anchor="middle">링</text>
  <rect class="fill-soft" x="166" y="96" width="58" height="30" rx="6"/><text class="lbl" x="195" y="116" text-anchor="middle">소비자1</text>
  <line class="accent" x1="86" y1="111" x2="94" y2="111"/><line class="accent" x1="156" y1="111" x2="164" y2="111"/>
  <text class="lbl" x="26" y="152">인덱스 2개, 각각 한쪽만 쓴다</text>
  <text class="lbl" x="26" y="176">CAS 필요 없음 · wait-free</text>
  <text class="lbl" x="26" y="200">ISR 에서 쓸 수 있다</text>
  <text class="lbl" x="26" y="232">✗ 생산자나 소비자가</text><text class="lbl" x="26" y="252">   둘 이상이면 못 쓴다</text>

  <rect class="box" x="248" y="40" width="222" height="240" rx="10"/>
  <text x="359" y="64" text-anchor="middle">② seqlock</text>
  <text class="lbl" x="359" y="82" text-anchor="middle">최신값 하나 — 작은 구조체</text>
  <rect class="fill-soft" x="264" y="96" width="60" height="30" rx="6"/><text class="lbl" x="294" y="116" text-anchor="middle">writer1</text>
  <rect class="box" x="334" y="96" width="62" height="30" rx="6"/><text class="lbl" x="365" y="116" text-anchor="middle">seq+필드</text>
  <rect class="fill-soft" x="406" y="96" width="52" height="30" rx="6"/><text class="lbl" x="432" y="116" text-anchor="middle">reader N</text>
  <line class="accent" x1="324" y1="111" x2="332" y2="111"/><line class="accent dash" x1="396" y1="111" x2="404" y2="111"/>
  <text class="lbl" x="264" y="152">홀수 = 쓰는 중, 짝수 = 안정</text>
  <text class="lbl" x="264" y="176">writer 는 절대 안 기다린다</text>
  <text class="lbl" x="264" y="200">reader 는 재시도한다</text>
  <text class="lbl" x="264" y="232">✗ 포인터·리소스 금지</text><text class="lbl" x="264" y="252">✗ reader 기아 가능</text>

  <rect class="box" x="486" y="40" width="224" height="240" rx="10"/>
  <text x="598" y="64" text-anchor="middle">③ 포인터 교체 + refcount</text>
  <text class="lbl" x="598" y="82" text-anchor="middle">불변 객체 전체를 바꿈</text>
  <rect class="fill-soft" x="502" y="96" width="58" height="30" rx="6"/><text class="lbl" x="531" y="116" text-anchor="middle">발행자</text>
  <rect class="box" x="570" y="96" width="62" height="30" rx="6"/><text class="lbl" x="601" y="116" text-anchor="middle">g_current</text>
  <rect class="fill-soft" x="642" y="96" width="54" height="30" rx="6"/><text class="lbl" x="669" y="116" text-anchor="middle">reader N</text>
  <line class="accent" x1="560" y1="111" x2="568" y2="111"/><line class="accent dash" x1="632" y1="111" x2="640" y2="111"/>
  <text class="lbl" x="502" y="152">쓰기는 포인터 한 번</text>
  <text class="lbl" x="502" y="176">복사 비용 0 · 크기 무관</text>
  <text class="lbl" x="502" y="200">refcount 가 수명을 관리</text>
  <text class="lbl" x="502" y="232">✗ use-after-free 창이 열린다</text><text class="lbl" x="502" y="252">   (이 노트의 §3.4)</text>
</svg>
```

## 3. 개념 (용어를 하나씩)

### 3.1 세 패턴은 서로 다른 질문에 답한다

| 질문 | 패턴 | 공유되는 것 | 이 저장소 |
| --- | --- | --- | --- |
| 최신값 하나이고 64비트에 다 들어간다 | (패턴 불필요) **atomic 하나** | `_Atomic uint64_t` | `02_modem_rssi_window` Part 1 |
| 모든 항목을 다 처리해야 한다 | **SPSC 링버퍼** | 큐 + 인덱스 2개 | `03_event_tailer_shutdown` |
| 최신값 하나만 필요하고, payload가 작은 구조체다 | **seqlock** | 시퀀스 번호 + 필드들 | `01_gps_fix_cache` Part 1 |
| 최신값 하나인데 payload가 크거나 불변 객체 전체다 | **포인터 교체 + refcount** | 포인터 하나 | `06_config_publish_rollback`, `05_frame_latest_and_replay` |

**용어 정리** (면접에서 정확히 써야 한다):

- **wait-free**: 모든 스레드가 **정해진 횟수의 명령 안에** 끝난다. 재시도 루프도 없다. SPSC의 push/pop, 패킹된 atomic 하나 읽기, seqlock의 **writer**가 이 부류다.
- **lock-free**: 어떤 스레드가 멈춰도 **시스템 전체는 진행한다.** 개별 스레드는 무한히 재시도할 수 있다. CAS 루프, seqlock의 **reader**가 이 부류다.
- **non-blocking**: "오래 걸리는 것(네트워크, 벤더 블로킹 호출)을 기다리지 않는다"는 뜻으로 쓰인다. **lock-free와 같은 말이 아니다.** 문제 지문이 요구하는 것은 거의 항상 이쪽이고, 그래서 짧은 mutex로도 요구사항을 만족한다.

### 3.2 SPSC 링버퍼 — 인덱스를 각각 한쪽만 쓴다

**문제**: 인터럽트 핸들러(또는 벤더 콜백)가 이벤트를 만들고 워커 스레드 하나가 처리한다.
**전부 처리해야 한다** — 최신값만 보는 게 아니다. 그리고 ISR 쪽에서 mutex는 금지다.
**핵심 관찰**: 생산자 1명 + 소비자 1명이면 **CAS가 아예 필요 없다.** `head`는 생산자만 쓰고
소비자는 읽기만, `tail`은 반대다. 각 변수에 writer가 하나씩이니 RMW 경합이 없다.

```svg
<svg viewBox="0 0 700 260" role="img" aria-label="SPSC 링버퍼의 head/tail">
  <text x="350" y="18" text-anchor="middle">SPSC — head 는 생산자만 쓰고, tail 은 소비자만 쓴다</text>
  <rect class="box" x="60" y="60" width="70" height="46" rx="4"/><text class="lbl" x="95" y="88" text-anchor="middle">0</text>
  <rect class="fill-soft" x="130" y="60" width="70" height="46" rx="4"/><text class="lbl" x="165" y="88" text-anchor="middle">1 ●</text>
  <rect class="fill-soft" x="200" y="60" width="70" height="46" rx="4"/><text class="lbl" x="235" y="88" text-anchor="middle">2 ●</text>
  <rect class="fill-soft" x="270" y="60" width="70" height="46" rx="4"/><text class="lbl" x="305" y="88" text-anchor="middle">3 ●</text>
  <rect class="box" x="340" y="60" width="70" height="46" rx="4"/><text class="lbl" x="375" y="88" text-anchor="middle">4</text>
  <rect class="box" x="410" y="60" width="70" height="46" rx="4"/><text class="lbl" x="445" y="88" text-anchor="middle">5</text>
  <rect class="box" x="480" y="60" width="70" height="46" rx="4"/><text class="lbl" x="515" y="88" text-anchor="middle">6</text>
  <rect class="box" x="550" y="60" width="70" height="46" rx="4"/><text class="lbl" x="585" y="88" text-anchor="middle">7</text>
  <polygon class="accentf" points="165,44 159,32 171,32"/><text class="lbl" x="165" y="26" text-anchor="middle">tail (소비자가 쓴다)</text>
  <polygon class="accentf" points="375,122 369,134 381,134"/><text class="lbl" x="375" y="150" text-anchor="middle">head (생산자가 쓴다)</text>
  <text class="lbl" x="60" y="184">● = 채워진 칸.   head == tail  이면 비었다.</text>
  <text class="lbl" x="60" y="206">(head + 1) % CAP == tail  이면 가득 찼다 — 한 칸을 항상 비워 둔다.</text>
  <text class="lbl" x="60" y="228">그 한 칸이 없으면 "가득"과 "비었다"가 둘 다 head == tail 이 되어 구별할 수 없다.</text>
  <text class="lbl" x="60" y="250">그래서 실제 용량은 CAP - 1 이다.  8칸 링은 7개까지 담는다.</text>
</svg>
```

**한 칸 비우기**: 인덱스를 `[0, CAP)` 범위로 쓰면 `head == tail`이 "비었다"와 "가득 찼다"를
동시에 뜻해 버린다. 한 칸을 항상 비워 두면 구별된다 — 대가는 용량 하나. 대안은 free-running
카운터 + 마스크(`count = head - tail`, unsigned 랩이 알아서 맞는다)이고 `01`·`02`가 그 방식이다.

**ISR에서 쓸 수 있는 이유**: `push`가 락을 잡지 않고 정해진 횟수의 명령으로 끝난다(wait-free).
재시도 루프조차 없다. 조건이 둘 — 해당 atomic이 lock-free여야 하고(`atomic_is_lock_free`로 확인),
생산자/소비자가 정말 각각 하나여야 한다.

**한계**

| 한계 | 설명 |
| --- | --- |
| 생산자 2명 이상 → 못 쓴다 | `head`에 writer가 둘이 되는 순간 잃어버린 갱신이 생긴다. MPSC는 CAS 루프나 락이 필요하다 |
| 가득 찼을 때의 정책을 **내가** 정해야 한다 | 블로킹(그럼 락이 필요) / 최신 것 버리기 / 가장 오래된 것 버리기. `03_event_tailer_shutdown`이 이 판단을 문제로 낸다 |
| `%` 대신 마스크 | `CAP`을 2의 거듭제곱으로 잡고 `& (CAP-1)`. 나눗셈은 ISR에서 비싸다 |

### 3.3 seqlock — 홀짝 시퀀스로 "찢어짐"을 감지한다

**문제**: `01_gps_fix_cache`의 payload는 이것이다.

```c
struct GpsFix { int status; double lat, lon; float hdop; uint64_t timestamp; };  /* 40 B */
```

reader는 **한 fix에서 나온 lat, lon, hdop, timestamp**를 받아야 한다.
작년 위도 + 올해 경도 조합이면 geofence 알람이 엉뚱한 카운티에서 울린다.

40바이트다. 원자 스토어가 없다([B6 §3.7](B6_atomics_and_memory_order.md)). mutex 스냅샷이
**기본이고 맞는 답**이지만, writer가 reader를 기다리게 된다. seqlock은 그 비대칭을 뒤집는다 —
**writer는 절대 기다리지 않고, reader가 재시도한다.**

**원리**: 시퀀스 번호 하나를 둔다. writer는 쓰기 전에 홀수로, 다 쓰고 나서 짝수로 만든다.
reader는 (a) 처음 읽은 시퀀스가 홀수면 재시도, (b) 필드를 다 복사한 뒤 시퀀스를 다시 읽어
**달라졌으면** 재시도한다. 둘 다 통과했으면 그 복사본은 온전한 한 벌이다.

```svg
<svg viewBox="0 0 700 280" role="img" aria-label="seqlock 홀짝 시퀀스 타임라인">
  <text x="350" y="18" text-anchor="middle">seqlock — 홀수 = 쓰는 중, 짝수 = 안정</text>
  <text class="lbl" x="18" y="52">writer</text><line class="muted" x1="18" y1="62" x2="680" y2="62"/>
  <rect class="fill-soft" x="80" y="70" width="86" height="28" rx="6"/><text class="lbl" x="123" y="89" text-anchor="middle">seq 4→5</text>
  <rect class="box" x="176" y="70" width="150" height="28" rx="6"/><text class="lbl" x="251" y="89" text-anchor="middle">필드 5개 쓰기</text>
  <rect class="fill-soft" x="336" y="70" width="86" height="28" rx="6"/><text class="lbl" x="379" y="89" text-anchor="middle">seq 5→6</text>
  <text class="lbl" x="80" y="118">release fence</text><text class="lbl" x="336" y="118">release fence</text>
  <text class="lbl" x="18" y="168">reader A (운이 나쁘다)</text><line class="muted" x1="18" y1="178" x2="680" y2="178"/>
  <rect class="box" x="200" y="186" width="70" height="26" rx="6"/><text class="lbl" x="235" y="204" text-anchor="middle">seq=5</text>
  <text class="lbl" x="284" y="204">홀수 → 즉시 재시도</text>
  <text class="lbl" x="18" y="246">reader B (운이 좋다)</text><line class="muted" x1="18" y1="256" x2="680" y2="256"/>
  <rect class="box" x="440" y="232" width="68" height="26" rx="6"/><text class="lbl" x="474" y="250" text-anchor="middle">seq=6</text>
  <rect class="box" x="514" y="232" width="86" height="26" rx="6"/><text class="lbl" x="557" y="250" text-anchor="middle">필드 복사</text>
  <rect class="box" x="606" y="232" width="74" height="26" rx="6"/><text class="lbl" x="643" y="250" text-anchor="middle">seq=6 ✓</text>
  <line class="accent dash" x1="474" y1="230" x2="643" y2="230"/>
  <text class="lbl" x="490" y="222">앞뒤 seq 가 같고 짝수 → 온전한 한 fix</text>
</svg>
```

**한계 세 가지 — 이걸 말할 수 있어야 쓴 것이다**

**(1) C 표준상 data race라서 필드마다 relaxed atomic이 필요하다.**
reader는 writer가 쓰는 중에 필드를 읽고 버리는 것을 **의도적으로** 한다.
평범한 `double`이면 그 읽기가 data race = UB다. 실제 TSan 결과는 §4.2에 있다.

**(2) 포인터와 리소스는 절대 넣지 말 것.**
seqlock은 "읽고 나서 틀렸으면 버린다"가 전제다. 그런데 **버리기 전에 이미 써버린 것**은
되돌릴 수 없다. 찢어진 포인터를 dereference하면 그 자리에서 죽는다. `fd`를 읽어서
`close()`했으면 남의 fd를 닫는다. payload는 **작고, 복사 가능하고, 부작용이 없어야** 한다.

**(3) reader 기아(starvation).**
writer가 계속 쓰면 reader는 무한히 재시도할 수 있다. writer가 wait-free인 대가다.
10 Hz면 아무 문제 없고, 10 MHz면 reader가 굶는다.

여기에 하나 더: **writer가 하나여야 한다.** 둘이면 시퀀스 자체에 CAS나 락이 필요하고,
그러면 seqlock의 장점이 절반 사라진다.

### 3.4 포인터 교체 + refcount — 그리고 use-after-free 창

**문제**: `06_config_publish_rollback`은 게이트웨이의 프로비저닝 레코드(APN, 업로드 주기,
WAN 우선순위)를 클라우드에서 받는다. 모든 서브시스템이 자기 스레드에서 그걸 계속 읽는다.

필드를 하나씩 갱신하면 **옛 APN + 새 업로드 주기** 조합이 나오고, 모뎀이 잘못
프로비저닝된다. 그래서 **불변(immutable) 객체를 만들고 포인터 하나만 바꾼다.**

**왜 불변으로 만드는가**: 발행자가 새 객체를 **아무도 볼 수 없는 동안** 전부 채우고, 발행
후에는 그 바이트를 **아무도 쓰지 않는다**. 그러면 reader는 락 없이 필드를 읽어도 된다 —
경합할 writer가 아예 없다. 발행이 포인터 스토어 하나이므로 "절반만 적용된 설정"도 원리적으로
존재할 수 없다.

대신 새 문제가 생긴다. **옛 객체를 언제 free하는가.** 발행 순간에 free하면 아직 그 안에 있는
reader가 죽는다. 그래서 refcount다 — 각 객체가 주인 수를 세고, 0이 되는 순간 **마지막으로 놓은
사람**이 free한다. 주인은 셋이다: `current` 슬롯, 히스토리 링의 각 슬롯, reader가 빌린 각 참조.
그런데 여기에 함정이 하나 있고, 그게 이 문제의 전부다.

```svg
<svg viewBox="0 0 700 290" role="img" aria-label="use-after-free 창 타임라인">
  <text x="350" y="18" text-anchor="middle">락을 빼면 A 와 B 사이에 창이 열린다</text>
  <text class="lbl" x="18" y="52">reader</text><line class="muted" x1="18" y1="62" x2="680" y2="62"/>
  <rect class="fill-soft" x="70" y="70" width="140" height="30" rx="6"/><text class="lbl" x="140" y="90" text-anchor="middle">A: p = g_current</text>
  <rect class="fill-soft" x="440" y="70" width="180" height="30" rx="6"/><text class="lbl" x="530" y="90" text-anchor="middle">B: fetch_add(&amp;p-&gt;refs, 1)</text>
  <line class="accent dash" x1="210" y1="85" x2="438" y2="85"/><text class="lbl" x="324" y="78" text-anchor="middle">창</text>
  <text class="lbl" x="18" y="160">발행자</text><line class="muted" x1="18" y1="170" x2="680" y2="170"/>
  <rect class="box" x="240" y="130" width="120" height="28" rx="6"/><text class="lbl" x="300" y="149" text-anchor="middle">g_current = 새것</text>
  <rect class="box" x="370" y="130" width="130" height="28" rx="6"/><text class="lbl" x="435" y="149" text-anchor="middle">refs 0 → free(옛것)</text>
  <line class="accent" x1="530" y1="132" x2="530" y2="68"/><text class="lbl" x="540" y="120">B 가 이미 free 된 메모리에 쓴다</text>
  <rect class="box" x="30" y="200" width="640" height="78" rx="8"/>
  <text class="lbl" x="45" y="222">닫는 방법 셋:</text>
  <text class="lbl" x="45" y="242">1) 아주 짧은 mutex — (A,B) 와 포인터 교체를 같은 락으로. 10줄. ← 06 이 고른 것</text>
  <text class="lbl" x="45" y="260">2) hazard pointer — "나 p 쓸 거임"을 내 슬롯에 적고 p 재확인. 회수자가 모든 슬롯을 훑는다</text>
  <text class="lbl" x="45" y="277">3) RCU / epoch — reader 는 임계 구역만 표시, 회수자는 grace period 를 기다린다. Linux 방식</text>
</svg>
```

세 방법의 트레이드오프다.
| 방법 | 읽기 비용 | 복잡도 | 언제 |
| --- | --- | --- | --- |
| 아주 짧은 mutex | 락 잡기 + 로드 + atomic add (경합 없으면 syscall 없이 수십 ns) | ~10줄 | **기본값.** 게이트웨이 설정처럼 발행이 드물 때 |
| hazard pointer | 스토어 1 + 로드 재확인. 락 없음 | ~200줄. 스레드 등록 + 회수 시 전체 슬롯 스캔 | 읽기가 정말 뜨겁고 스레드 수가 고정일 때 |
| RCU / epoch | 가장 싸다. 임계 구역 표시만 | quiescent state 추적 + deferred free 기계장치 | Linux 커널이 정확히 이런 설정 포인터에 쓴다 |

**한계**

| 한계 | 설명 |
| --- | --- |
| A/B 창을 반드시 닫아야 한다 | 안 닫으면 §4.4의 ASan 리포트가 프로덕션에서 나온다 |
| 살아 있는 객체 수가 일시적으로 늘어난다 | 히스토리 8개 + reader들이 든 것. reader 수로 상한이 잡히므로 bounded다 |
| ABA | 포인터 기반 lock-free 구조(스택·큐)에서는 주소 재사용 때문에 CAS만으로 부족하다. tagged pointer / hazard pointer ([B6 §4.2](B6_atomics_and_memory_order.md)) |

## 4. 코드로 보기

### 4.1 SPSC 링버퍼 — 20줄

컴파일하고 200회 스트레스까지 확인한 코드다.

```c
#define CAP 64u
static int                buf[CAP];
static _Atomic unsigned   head;      /* 생산자만 store, 소비자는 load만 */
static _Atomic unsigned   tail;      /* 소비자만 store, 생산자는 load만 */

static bool push(int v) {
    unsigned h = atomic_load_explicit(&head, memory_order_relaxed);   /* 내 것 */
    unsigned n = (h + 1u) % CAP;
    if (n == atomic_load_explicit(&tail, memory_order_acquire))       /* 남의 것 */
        return false;                                                /* 가득 */
    buf[h] = v;                                                      /* 데이터 */
    atomic_store_explicit(&head, n, memory_order_release);            /* 깃발  */
    return true;
}
static bool pop(int *out) {
    unsigned t = atomic_load_explicit(&tail, memory_order_relaxed);   /* 내 것 */
    if (t == atomic_load_explicit(&head, memory_order_acquire))       /* 남의 것 */
        return false;                                                /* 비었다 */
    *out = buf[t];                                                   /* 데이터 */
    atomic_store_explicit(&tail, (t + 1u) % CAP, memory_order_release);
    return true;
}
```

한 줄씩 왜 그런지.

- **내 인덱스는 `relaxed`**(나만 쓴다), **남의 인덱스는 `acquire`**(상대가 `release`로 세운 그 깃발을 읽는 것이다 — 짝이다).
- **`buf[]`는 평범한 `int` 배열이다.** `_Atomic`이 아니다. acquire/release 짝이 그 앞뒤를 묶어주기 때문이다 ([B6 §5](B6_atomics_and_memory_order.md)).
- **`buf[h] = v` 가 `store(head, release)` 앞에 있어야 한다. 이 순서가 뒤집히면?** 소비자가 아직 안 쓰인 칸을 읽는다. release가 막는 것이 정확히 이것이고, 안 막으면 §5에서 200회 중 38회 깨진다.
- **CAS가 없다.** 두 인덱스 모두 writer가 한 명이라서다. 생산자가 둘이면 `head`가 경합하고 그 순간 CAS(또는 락)가 필요해진다 — 그게 MPSC이고 훨씬 어렵다.

### 4.2 seqlock — `01_gps_fix_cache/gps_cache_solution.c` 실제 구현

writer 쪽이다. 인용은 원문 그대로다.

```c
static _Atomic uint32_t g_seq;              /* even = stable, odd = writing */
static _Atomic int g_status = GPS_NO_FIX;   /* 필드는 전부 _Atomic — §3.3 의 한계 (1) */
static _Atomic double g_lat, g_lon;  static _Atomic float g_hdop;  static _Atomic uint64_t g_ts;

static void publish_fix(const struct GpsFix *f)
{
    /* Single writer, so a plain load of the sequence is enough. */
    uint32_t s = atomic_load_explicit(&g_seq, memory_order_relaxed);

    atomic_store_explicit(&g_seq, s + 1u, memory_order_relaxed);   /* -> odd  */
    atomic_thread_fence(memory_order_release);   /* odd seq visible BEFORE fields */

    atomic_store_explicit(&g_status, f->status, memory_order_relaxed);
    atomic_store_explicit(&g_lat,    f->lat,    memory_order_relaxed);
    /* ... g_lon, g_hdop, g_ts 도 같은 방식으로 relaxed store ... */

    atomic_thread_fence(memory_order_release);   /* fields visible BEFORE even seq */
    atomic_store_explicit(&g_seq, s + 2u, memory_order_relaxed);   /* -> even */
}
```

reader 쪽이다.

```c
    struct GpsFix f;
    for (;;) {
        uint32_t s1 = atomic_load_explicit(&g_seq, memory_order_acquire);
        if (s1 & 1u)                  /* writer mid-update: nothing to copy yet */
            continue;

        f.status    = atomic_load_explicit(&g_status, memory_order_relaxed);
        f.lat       = atomic_load_explicit(&g_lat,    memory_order_relaxed);
        /* ... 나머지 필드도 같은 방식 ... */

        /* Field loads must not be reordered after this re-check, or the
         * "sequence unchanged" conclusion would be about the wrong values. */
        atomic_thread_fence(memory_order_acquire);
        if (atomic_load_explicit(&g_seq, memory_order_relaxed) == s1)
            break;                    /* no update overlapped: f is one whole fix */
    }
```

읽어야 할 점 셋.

- **fence가 `store(release)`가 아니라 `atomic_thread_fence(release)`다.** 시퀀스 스토어에
  release를 붙이면 "그 앞의 것이 뒤로 안 밀림"만 얻는다. 여기서 필요한 것은 반대다 —
  **홀수 시퀀스가 필드 쓰기보다 먼저 보여야** 한다. 독립 fence가 그 벽이다.
  ARM에서는 `dmb ish` / `dmb ishld`로 번역된다 ([B6 §5.5](B6_atomics_and_memory_order.md)).
- **이 fence가 없으면?** 필드 쓰기가 홀수 시퀀스보다 먼저 보일 수 있다. 그러면 reader가
  짝수 시퀀스를 두 번 확인했는데도 그 사이에 바뀐 필드를 들고 나간다 — 정확히 막으려던 찢어짐이다.
- **필드는 전부 `relaxed` atomic이다.** 평범한 `double`로 바꾸면 TSan이 이렇게 말한다 (실제 출력).

```
WARNING: ThreadSanitizer: data race (pid=9318)
  Read of size 8 at 0x000100fc8008 by thread T2:
    #0 reader seq_v0.c:30
  Previous write of size 8 at 0x000100fc8008 by thread T1:
    #0 writer seq_v0.c:17
  Location is global 'lat' at 0x000100fc8008
SUMMARY: ThreadSanitizer: data race seq_v0.c:30 in reader
```

`_Atomic double` + `relaxed`로 바꾸면 TSan 경고 0, 찢어진 읽기 0회.
**기계어는 똑같이 나온다** — 공짜로 정의된 동작을 얻는다. 모범답안 주석이 이 얘기다.

```
 * Every field is an _Atomic because a seqlock reader is *expected* to read
 * fields while the writer is mid-update and then throw the result away.  With
 * plain scalars that read is a data race = undefined behaviour, and TSan says
 * so.  Relaxed atomics make it defined and generate the same instructions;
 * the two fences below are what actually orders things.
```

seqlock을 고른 근거와 대가도 같은 주석에 영어로 적혀 있다 — *"readers can in principle spin
(a writer storm starves them -- fine at 10 Hz, not fine at 10 MHz) ... it only works because the
payload is small, copyable and side-effect free, so re-reading is cheap."* 면접에서 이 세 줄을
그대로 말하면 된다.

### 4.3 포인터 교체 + refcount — `06_config_publish_rollback/config_store_solution.c`

reader 쪽. A와 B를 **하나의 임계 구역**으로 묶는다.

```c
static pthread_mutex_t g_pub_lock = PTHREAD_MUTEX_INITIALIZER;
static struct cfg_node *g_current;            /* 평범한 포인터: 락이 지킨다 */

const struct Config *config_acquire(void)
{
    struct cfg_node *n;

    pthread_mutex_lock(&g_pub_lock);
    n = g_current;
    if (n) node_ref(n);          /* load and claim are ONE atomic step now */
    pthread_mutex_unlock(&g_pub_lock);

    return n ? &n->cfg : NULL;
}
```

발행 쪽 `publish_current()` 도 같은 락을 잡지만 **포인터 스토어 하나 동안만** 잡는다.
락 안에서 옛 포인터를 지역 변수로 떼어 놓고, `node_unref(old)` 는 **락을 놓은 뒤**에 부른다 —
`free()` 를 락 안에서 하지 않기 위해서다. refcount 자체는 이렇다.

```c
/* node_ref() 는 relaxed fetch_add 한 줄이다. 항상 g_pub_lock(reader 경로) 또는
 * g_hist_lock(히스토리 경로) 안에서 불리므로 n 이 살아 있음이 이미 보장된다. */

static void node_unref(struct cfg_node *n)
{
    if (!n) return;
    /* acq_rel: everything this thread did with the object must happen before
     * the free that some other thread may perform, and the thread that sees
     * the count reach 0 must see all of that. */
    if (atomic_fetch_sub_explicit(&n->refs, 1, memory_order_acq_rel) == 1) {
        atomic_fetch_add_explicit(&g_frees, 1, memory_order_relaxed);
        memset(n, 0xA5, sizeof *n);   /* 독을 넣는다 — 새니타이저 없이도 티가 나게 */
        free(n);
    }
}
```

**증가는 `relaxed`, 감소는 `acq_rel`인 이유** — 면접에서 묻는 지점이다.

- **증가는 `relaxed`**: 이미 살아 있음을 알고 부른다(락 안이거나 내가 참조를 하나 들고 있다). 아무것도 발행하지 않으므로 순서가 필요 없다.
- **감소의 release 쪽**: 내가 이 객체로 한 모든 읽기/쓰기가 **남이 하는 `free`보다 앞에** 있어야 한다. 아니면 내가 아직 읽는 중에 free될 수 있다.
- **감소의 acquire 쪽**: 0을 본 스레드는 **다른 스레드들이 그 객체로 한 일을 전부 봐야** 한다. 아니면 아직 도착하지 않은 쓰기를 남긴 채 free한다.
- 흔한 최적화: `fetch_sub(release)` 로 하고 `if (old == 1) atomic_thread_fence(acquire);` — 마지막 참조일 때만 acquire 비용을 낸다. Linux·Boost의 관용구다.
- **`fetch_sub`는 바뀌기 전 값을 준다.** `== 1`이 "내가 마지막"이다. `== 0`이라고 쓰면 절대 free되지 않는다.

**히스토리 링에서 evict해도 안전한 이유**도 refcount다 — evict는 free가 아니라 **링의 참조를
하나 놓는 것**이다. reader가 들고 있으면 카운트가 0이 아니므로 살아 있고, 나중에 그 reader가
free한다. 모범답안 주석의 표현: *"Eviction is safe precisely BECAUSE of the refcount:
evicting only drops the ring's reference."*

### 4.4 락을 빼면 진짜로 터진다 — 실측

`config_acquire()`에서 mutex 두 줄만 지우고(A와 B는 그대로 남긴 채)
AddressSanitizer로 같은 하네스를 돌린 결과다. 재현은 즉시였다.

```
==8142==ERROR: AddressSanitizer: heap-use-after-free on address 0x60600000f830
WRITE of size 4 at 0x60600000f830 thread T7
    #0 config_release config_store_broken.c:175
    #1 stress_reader main.c:270
0x60600000f830 is located 48 bytes inside of 52-byte region
freed by thread T1 here:
    #0 free   #1 publish_new config_store_broken.c:313   #2 fetch_main :339
previously allocated by thread T1 here:
    #0 malloc #1 publish_new config_store_broken.c:309
SUMMARY: AddressSanitizer: heap-use-after-free config_store_broken.c:175 in config_release
```

읽는 법: reader가 `config_release`에서 4바이트를 **쓰려고**(refcount 감소) 했는데,
그 52바이트 영역은 발행 스레드 T1이 `publish_new` 안에서 이미 free했다.
`48 bytes inside of 52-byte region` — `struct cfg_node`의 마지막 멤버인 `refs`가 정확히 거기다.

같은 하네스가 락 있는 원본에서는 이렇게 끝난다 (`./main.sh asan` 으로 직접 확인할 수 있다).

```
  4458 versions published during 385105 getter rounds (alloc 4530 / free 4522 / live 8)
  no reader ever touched a freed or recycled config PASS
  no leaks: every config was freed / no double frees   PASS
ALL CHECKS PASSED (0 failed)
```

왜 hazard pointer나 RCU가 아닌지의 근거가 파일 머리에 있다 — 이게 면접 답안이다.

```
 *    On this gateway the lock wins: it is held for one load and one atomic
 *    add (tens of nanoseconds, no syscall on an uncontended pthread mutex),
 *    the publisher holds it only across a pointer store — never across the
 *    fetch, the malloc or the free — and it is ~10 lines instead of ~200 that
 *    a reviewer has to trust.  "Non-blocking" here means the reader never
 *    waits on the NETWORK; it does not mean lock-free.  If profiling ever
 *    showed this lock hot, RCU is the next step, not a smarter lock.
```

## 5. 단계별로 만들어 보기 — SPSC를 세 번 만든다

세 버전을 실제로 컴파일하고 돌린 결과를 그대로 싣는다. Apple M2(ARM64),
Apple clang 21, `-O2`, 5000개 push/pop, 합의 정답은 `12497500`.

### v0 — 평범한 `unsigned head, tail`

프로그램이 **멈춘다**. 값이 틀리는 게 아니라 멈춘다. 소비자의 기계어에 이유가 그대로 있다.

```
	ldr	w9, [x9, _head@PAGEOFF]     ; ← 루프 밖에서 head 를 한 번만 읽는다
LBB2_1:	cmp	w11, w9                     ; 그 레지스터와 계속 비교
	b.eq	LBB2_4
LBB2_4:	b	LBB2_4                      ; ← 진짜 무한 루프
```

`-O0`으로 낮추면 우연히 잘 돌아간다(10만개로 돌려 `sum=4999950000`, 정답).
**"내 기계에서는 되는데요"의 정체가 이것이다.**

### v1 — `volatile unsigned head, tail`

hang은 사라진다. 컴파일러가 매번 메모리를 읽으니까. 그런데 **데이터가 깨진다.**
200회 돌린 결과다.

```
MISMATCH run 11: sum=12497244
MISMATCH run 14: sum=12496924
MISMATCH run 25: sum=12497372
...
mismatches=38 / 200
```

**200회 중 38회(19%) 실패.** 원인은 CPU 재배치다 — `buf[h] = v` 스토어가 `head = n`
스토어보다 늦게 도착해서 소비자가 아직 안 쓰인 칸을 읽는다.
[B6 §5의 v0 타임라인](B6_atomics_and_memory_order.md) 그림이 바로 이 순간이고, `volatile`이
컴파일러만 막고 CPU는 못 막는다는 실물 증거다. x86에서는 TSO라서 store-store 재배치가 없어
이 실험이 통과할 수도 있다 — **x86 노트북에서 테스트하고 ARM 보드에 올리는 것이 위험하다.**

### v2 — `_Atomic` + acquire/release (§4.1의 코드)

같은 200회 스트레스.

```
mismatches=0 / 200
```

TSan으로도 돌렸다 — 경고 0, `sum=12497500`.

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| SPSC 인덱스를 `volatile`로만 | 대부분 통과, ARM에서 19% 확률로 데이터 깨짐 | `volatile`은 CPU 배리어를 안 내보낸다 | `_Atomic` + 내 인덱스 relaxed / 남의 인덱스 acquire / 내 스토어 release |
| SPSC에 생산자를 하나 더 붙임 | 항목이 사라지거나 덮어써짐 | `head`에 writer가 둘이 되어 잃어버린 갱신 | MPSC로 재설계(CAS 또는 락). SPSC를 확장할 수는 없다 |
| seqlock 필드를 평범한 `double`로 | 대개 잘 돌지만 TSan이 race로 지목. UB이므로 최적화에 따라 언제든 깨질 수 있다 | reader가 하도록 **설계된** 찢어진 읽기가 표준상 UB | 모든 필드를 `_Atomic` + `relaxed`. 기계어는 같다 |
| seqlock의 두 fence를 빼고 시퀀스 스토어에 release만 붙임 | 아주 드물게 찢어진 값이 짝수 시퀀스로 확인됨 | release 스토어는 "앞의 것이 뒤로 밀리는 것"만 막는다. 홀수 시퀀스가 필드보다 먼저 보이게 하는 벽이 아니다 | `atomic_thread_fence(memory_order_release)` 두 개를 쓴다 (01의 구현) |
| seqlock에 포인터나 `fd`를 넣음 | 세그폴트, 남의 fd 닫힘 | seqlock은 "읽고 버린다"가 전제. 이미 dereference/close 한 것은 못 버린다 | payload는 작고 복사 가능하고 부작용 없는 것만. 포인터는 패턴 ③으로 |
| refcount 증가를 락 밖에서 | 간헐적 use-after-free. ASan은 즉시 잡는다 | "포인터 로드 → refs 증가" 사이에 free될 수 있다 | 로드와 증가를 하나의 임계 구역으로 (또는 hazard pointer / RCU) |
| `fetch_sub` 결과를 `== 0`으로 비교 | 절대 free되지 않음 → 메모리 누수 | `fetch_*`는 바뀌기 **전** 값을 준다 | `== 1` 이 "내가 마지막 참조" |
| `free()`를 락 안에서 | 락 보유 시간이 malloc 구현에 따라 길어짐 | `free`는 syscall을 할 수 있다 | 락 안에서는 포인터만 떼어 놓고, `unref`는 락 밖에서 |

## 7. 손으로 확인하기

```sh
cd 01_gps_fix_cache && ./main.sh sol          # seqlock — 찢어진 fix가 나오는지
cd ../06_config_publish_rollback && ./main.sh sol
./main.sh tsan                                # 모범답안: TSan 경고 0
./main.sh asan                                # 모범답안: ASan 깨끗
```

직접 깨뜨려 보는 것이 이 노트의 목적이다. 셋 다 5분 안에 된다.

```sh
# (1) seqlock 필드를 평범한 double 로 — §4.2 의 TSan 리포트를 재현한다
cd 01_gps_fix_cache && cp gps_cache_solution.c /tmp/g.c
#   /tmp/g.c:  static _Atomic double g_lat, g_lon;  →  static double g_lat, g_lon;
#   publish_fix / gps_get_last_fix 의 해당 atomic_store/load 도 평범한 대입으로 바꾼다
cc -std=c11 -O2 -Wall -pthread -fsanitize=thread -g -I. -o /tmp/t main.c /tmp/g.c -lm && /tmp/t
#   → ThreadSanitizer: data race ... Location is global 'g_lat'

# (2) refcount 의 A/B 창 열기 — §4.4 의 ASan 리포트를 재현한다
cd 06_config_publish_rollback && cp config_store_solution.c /tmp/c.c
#   /tmp/c.c 의 config_acquire() 에서 pthread_mutex_lock/unlock 두 줄만 지운다
cc -std=c11 -O2 -Wall -pthread -fsanitize=address -g -I. -o /tmp/a main.c /tmp/c.c -lm && /tmp/a
#   → heap-use-after-free in config_release

# (3) SPSC 를 volatile 로만 20줄로 직접 짜서 200번 돌린다 — §5 v1 의 19% 실패 재현.
#   자기 손으로 짜는 것이 이 노트에서 가장 남는 5분이다. 합이 항상 같은지만 보면 된다:
for i in $(seq 200); do ./spsc_volatile; done | sort | uniq -c
#   → 정답 한 줄만 나와야 정상. 여러 줄이 나오면 재배치를 눈으로 본 것이다.
```

## 8. 자가 점검

```check
Q: SPSC 링버퍼에서 CAS가 필요 없는 이유는? 생산자를 하나 더 붙이면 무엇이 깨지나?
A: 인덱스가 두 개이고 각 인덱스에 writer가 정확히 한 명이기 때문이다. `head`는 생산자만 쓰고 소비자는 읽기만, `tail`은 반대다. read-modify-write 경합이 아예 없으니 단순 load/store로 충분하다.
A: 생산자가 둘이 되면 `head`를 두 스레드가 읽고-더하고-쓰게 되어 잃어버린 갱신이 생긴다. 한 항목이 덮어써지거나 사라진다. 이건 SPSC를 조금 고쳐서 되는 게 아니고 MPSC(CAS 루프 또는 락)로 재설계해야 한다.

Q: SPSC에서 `buf[h] = v` 와 `store(head, release)` 의 순서를 바꾸면 어떤 일이 일어나나?
A: 소비자가 아직 데이터가 안 쓰인 칸을 유효한 것으로 보고 읽는다. 쓰레기 값이 나온다.
A: 실제로 측정했다. 인덱스를 `volatile`로만 두면 ARM에서 200회 중 38회(19%) 합이 틀렸다. `volatile`은 컴파일러 재배치만 막고 CPU 재배치는 못 막기 때문이다. x86에서는 TSO라서 통과할 수도 있어서, x86에서 테스트하고 ARM 보드에 올리는 것이 특히 위험하다.


Q: seqlock의 필드를 전부 `_Atomic` + `relaxed`로 해야 하는 이유는? 기계어가 달라지나?
A: seqlock의 reader는 writer가 쓰는 중에 필드를 읽고 그 결과를 버리는 것을 의도적으로 한다. 평범한 스칼라라면 그 읽기가 data race이고, C 표준상 UB다. TSan도 그대로 잡는다.
A: 기계어는 달라지지 않는다. `relaxed` atomic은 일반 load/store와 같은 명령으로 컴파일된다. 실제 순서를 만드는 것은 앞뒤의 두 `atomic_thread_fence`다. 즉 공짜로 UB를 없애는 것이다.


Q: seqlock에 포인터를 넣으면 왜 안 되나? 큰 프레임은?
A: seqlock은 "읽고 나서 시퀀스가 틀렸으면 버린다"가 전제다. 그런데 찢어진 포인터를 이미 dereference했으면 버릴 기회가 없다 — 그 자리에서 죽는다. `fd`를 읽어서 `close()`했으면 남의 fd를 닫는다. payload는 작고 복사 가능하고 부작용이 없어야 한다.
A: 큰 프레임도 안 맞는다. 재시도마다 전체를 다시 복사해야 하는데, 복사가 비싸면 재시도 확률이 올라가고 그게 다시 복사를 늘린다. 큰 것은 더블/트리플 버퍼 + refcount나 포인터 교체로 간다.

Q: 포인터 교체에서 use-after-free 창은 정확히 어디에 열리나? 닫는 방법 세 가지는?
A: `p = g_current`(A)와 `atomic_fetch_add(&p->refs, 1)`(B) 사이다. 그 사이에 발행자가 새 설정으로 교체하고 옛것의 마지막 참조를 놓아 `free`할 수 있다. 그러면 B가 이미 free된 메모리에 쓴다.
A: (1) 아주 짧은 mutex로 (A,B)와 포인터 교체를 같은 락 안에 넣는다 — `06`이 고른 방법, 약 10줄. (2) hazard pointer: "나 p를 쓸 것이다"를 내 스레드 슬롯에 적고 p를 재확인하고, 회수자는 free 전에 모든 슬롯을 스캔한다. (3) RCU/epoch: reader는 임계 구역만 표시하고 회수자가 grace period를 기다린다. Linux가 정확히 이런 설정 포인터에 쓴다.

Q: refcount 증가는 `relaxed`, 감소는 `acq_rel`인 이유를 각각 말해 보라.
A: 증가는 이미 객체가 살아 있음을 아는 상태에서만 부른다(락 안이거나 내가 이미 참조를 하나 들고 있다). 아무것도 발행하지 않으니 순서를 맞출 대상이 없다.
A: 감소는 양방향이 다 필요하다. release 쪽: 내가 이 객체로 한 접근이 남이 하는 `free`보다 앞에 있어야 한다. acquire 쪽: 카운트가 0인 것을 본 스레드가 다른 스레드들이 그 객체로 한 일을 전부 봐야 한다. 안 그러면 도착하지 않은 쓰기를 남긴 채 free한다. 흔한 최적화는 `fetch_sub(release)` 뒤에 마지막일 때만 `atomic_thread_fence(acquire)`를 넣는 것이다.


Q: "non-blocking"과 "lock-free"는 왜 다른 말인가?
A: 문제 지문이 "getter는 블로킹하지 않을 것"이라고 할 때 요구하는 것은 보통 "reader가 200 ms짜리 벤더 호출이나 네트워크를 기다리지 않을 것"이다. 수십 ns짜리 경합 없는 mutex는 그 요구를 만족한다.
A: lock-free는 훨씬 강한 조건이다 — 어떤 스레드가 멈춰도 시스템이 진행해야 한다. `06` 모범답안이 이 구분을 명시하고 짧은 mutex를 고른 뒤 "프로파일이 이 락을 지목하면 다음 단계는 더 똑똑한 락이 아니라 RCU다"라고 적어 둔 것이 정확한 태도다.
```

## 9. 요약 카드 — 고르는 순서도

```svg
<svg viewBox="0 0 700 330" role="img" aria-label="패턴 고르는 순서도">
  <rect class="fill-soft" x="230" y="14" width="240" height="34" rx="8"/><text x="350" y="36" text-anchor="middle">무엇을 공유하는가?</text>
  <line class="accent" x1="350" y1="48" x2="350" y2="66"/>
  <rect class="box" x="200" y="66" width="300" height="32" rx="8"/><text class="lbl" x="350" y="87" text-anchor="middle">전부 소비해야 하는 스트림인가?</text>
  <line class="accent" x1="200" y1="82" x2="120" y2="82"/><line class="accent" x1="120" y1="82" x2="120" y2="112"/>
  <rect class="box" x="18" y="112" width="205" height="52" rx="8"/>
  <text class="lbl" x="120" y="133" text-anchor="middle">생산자1 + 소비자1 → SPSC 링</text>
  <text class="lbl" x="120" y="153" text-anchor="middle">그 밖 → mutex + condvar 큐</text>
  <line class="accent" x1="350" y1="98" x2="350" y2="118"/>
  <rect class="box" x="200" y="118" width="300" height="32" rx="8"/><text class="lbl" x="350" y="139" text-anchor="middle">최신값 하나면 된다 → 64비트에 들어가나?</text>
  <line class="accent" x1="500" y1="134" x2="580" y2="134"/><line class="accent" x1="580" y1="134" x2="580" y2="160"/>
  <rect class="fill-soft" x="480" y="160" width="205" height="42" rx="8"/>
  <text class="lbl" x="582" y="177" text-anchor="middle">예 → packed atomic word</text>
  <text class="lbl" x="582" y="195" text-anchor="middle">02_modem_rssi_window</text>
  <line class="accent" x1="350" y1="150" x2="350" y2="170"/>
  <rect class="box" x="195" y="170" width="265" height="32" rx="8"/><text class="lbl" x="327" y="191" text-anchor="middle">아니오 → 작고 복사 가능한 구조체인가?</text>
  <line class="accent" x1="195" y1="186" x2="120" y2="186"/><line class="accent" x1="120" y1="186" x2="120" y2="212"/>
  <rect class="fill-soft" x="18" y="212" width="205" height="42" rx="8"/>
  <text class="lbl" x="120" y="229" text-anchor="middle">예 → seqlock (또는 mutex 스냅샷)</text>
  <text class="lbl" x="120" y="247" text-anchor="middle">01_gps_fix_cache</text>
  <line class="accent" x1="327" y1="202" x2="327" y2="222"/>
  <rect class="box" x="230" y="222" width="240" height="32" rx="8"/><text class="lbl" x="350" y="243" text-anchor="middle">큰 프레임인가 / 불변 객체인가?</text>
  <line class="accent" x1="470" y1="238" x2="560" y2="238"/><line class="accent" x1="560" y1="238" x2="560" y2="264"/>
  <rect class="fill-soft" x="435" y="264" width="250" height="58" rx="8"/>
  <text class="lbl" x="560" y="282" text-anchor="middle">프레임 → 더블/트리플 버퍼 + refcount (05)</text>
  <text class="lbl" x="560" y="300" text-anchor="middle">불변 객체 → 포인터 교체 + refcount (06)</text>
  <text class="lbl" x="560" y="316" text-anchor="middle">둘 다 A/B 창을 닫아야 한다</text>
  <line class="accent" x1="230" y1="238" x2="150" y2="238"/><line class="accent" x1="150" y1="238" x2="150" y2="270"/>
  <rect class="box" x="30" y="270" width="230" height="32" rx="8"/><text class="lbl" x="145" y="291" text-anchor="middle">해당 없음 → mutex. 그게 기본이다</text>
</svg>
```

외울 것.

- **SPSC 링** = 스트림 + 생산자1·소비자1. 인덱스 두 개, 각각 한쪽만 쓴다.
  내 것 relaxed / 남의 것 acquire / 내 스토어 release. CAS 없음, wait-free, ISR 가능.
- **seqlock** = 최신값 + 작은 구조체 + writer1. 홀짝 시퀀스. **writer가 절대 안 기다린다.**
  대가: 필드마다 relaxed atomic, 포인터·리소스 금지, reader 기아.
- **포인터 교체 + refcount** = 불변 객체 전체 교체. 쓰기는 포인터 하나, 크기 무관.
  대가: **A(로드) / B(refs 증가) 사이의 use-after-free 창** — 짧은 락 / hazard pointer / RCU로 닫는다.
- refcount: 증가는 `relaxed`, 감소는 `acq_rel`, `fetch_sub(...) == 1` 이 "내가 마지막". `free`는 **락 밖에서**.
- **`volatile`로는 셋 다 안 된다.** 실측: ARM에서 200회 중 38회 데이터 손상, `-O2`에서는 hang.
- **"non-blocking"과 "lock-free"는 다른 말이다.** 문제 지문이 요구한 것은 보통 전자다.
  **측정 없는 lock-free 금지** — 기본은 mutex, 락은 포인터 한 번 바꿀 동안만.

---

이전: [B6. 원자 연산과 메모리 순서](B6_atomics_and_memory_order.md) ·
다음: [B8. 시간과 타임아웃](B8_time_and_timeouts.md) ·
[D1. 벤더 API 감싸기 레시피](D1_wrapper_recipe.md) 에서 이 판단이 문제 풀이 순서로 정리된다.
