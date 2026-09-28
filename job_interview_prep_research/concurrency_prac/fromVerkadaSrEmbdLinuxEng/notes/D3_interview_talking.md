# D3. 면접에서 말하는 법 — 같은 코드로 점수를 더 받기

> **이 노트를 읽고 나면**
> - 리크루터가 명시한 **평가 기준 4가지**를 각각 "면접 중에 할 행동"으로 바꿔 실행할 수 있다
> - 시작 3분과 마지막 5분에 할 말을 **영어 문장 그대로** 갖고 들어갈 수 있다
> - 막혔을 때·모르는 API가 나왔을 때·힌트를 받았을 때 **점수를 잃지 않는 문장**을 쓸 수 있다
>
> **선행**: [D1. 만능 레시피](D1_wrapper_recipe.md), [D2. 문제 지도](D2_problem_map.md)
> **이 개념을 쓰는 문제**: 열 문제 전부. 특히 각 `question_note` 하단의 10문항이 곧 면접 대화다

---

## 1. 왜 이게 필요한가

2026-09-15 리크루터(Davis) 메일에 평가 기준이 네 줄로 적혀 있다.

> "The session will have two parts, focusing on your **problem-solving and system design** skills."
> 평가 기준: ① 설계 결정과 trade-off의 정당화 ② 깨끗하고 유지보수 가능한 코드
> ③ edge case와 concurrent 시스템의 견고성 ④ 커뮤니케이션과 사고 과정의 명료함

네 개 중 **코드가 다 짜져야만 채워지는 항목은 하나도 없다.** ①③④는 말로 채워지고, ②는 다 짜지
않은 코드에서도 보인다. 그런데 9/25 스크리닝에서 실제로 일어난 일은 "코드를 짜려다 막히고, 막힌
동안 아무 말도 하지 않았다"였다 — 네 항목을 모두 비우는 유일한 방법이다. 그래서 이 노트는
**같은 실력에서 점수를 더 받는 방법**이다. 실력을 올리는 것은 [D2의 경로](D2_problem_map.md)가
하고, 여기서는 그 실력을 **보이게** 만든다.

---

## 2. 그림으로 먼저

45분을 채점 항목에 대응시킨 지도다. 세로축이 "지금 무엇으로 점수를 받고 있나"다.
```svg
<svg viewBox="0 0 760 330" role="img" aria-label="45분 동안 어떤 평가 항목이 채워지는가">
  <line class="muted" x1="60" y1="290" x2="730" y2="290"/><text class="lbl" x="60" y="308">0분</text><text class="lbl" x="150" y="308">3분</text><text class="lbl" x="250" y="308">7분</text><text class="lbl" x="420" y="308">20분</text><text class="lbl" x="620" y="308">40분</text><text class="lbl" x="710" y="308">45분</text>
  <rect class="fill-soft" x="60" y="40" width="90" height="40" rx="6"/><text class="lbl" x="105" y="58" text-anchor="middle">질문 3개</text><text class="lbl" x="105" y="72" text-anchor="middle">가정 선언</text>
  <rect class="fill-soft" x="155" y="40" width="95" height="40" rx="6"/><text class="lbl" x="202" y="58" text-anchor="middle">API 선언</text><text class="lbl" x="202" y="72" text-anchor="middle">동기화 선택</text>
  <rect class="box" x="255" y="40" width="360" height="40" rx="6"/><text x="435" y="66" text-anchor="middle">구현 + 내레이션</text>
  <rect class="fill-soft" x="620" y="40" width="105" height="40" rx="6"/><text class="lbl" x="672" y="58" text-anchor="middle">테스트 전략</text><text class="lbl" x="672" y="72" text-anchor="middle">역질문</text>
  <rect class="box" x="60" y="110" width="190" height="34" rx="6"/><text class="lbl" x="155" y="131" text-anchor="middle">① trade-off 정당화</text>
  <rect class="box" x="620" y="110" width="105" height="34" rx="6"/><text class="lbl" x="672" y="131" text-anchor="middle">① 다시</text>
  <rect class="box" x="255" y="155" width="360" height="34" rx="6"/><text class="lbl" x="435" y="176" text-anchor="middle">② 깨끗한 코드 — 이름·함수 분리·주석</text>
  <rect class="box" x="380" y="200" width="345" height="34" rx="6"/><text class="lbl" x="552" y="221" text-anchor="middle">③ edge case · 종료 경로 · 견고성</text>
  <rect class="fill-soft" x="60" y="245" width="665" height="34" rx="6"/><text class="lbl" x="392" y="266" text-anchor="middle">④ 커뮤니케이션 — 처음부터 끝까지 계속 채워진다</text>
  <line class="dash" x1="250" y1="30" x2="250" y2="290"/><line class="dash" x1="615" y1="30" x2="615" y2="290"/>
  <text class="lbl" x="250" y="24" text-anchor="middle">설계 끝</text><text class="lbl" x="615" y="24" text-anchor="middle">코딩 끝</text>
</svg>
```

말할 것과 말하지 않을 것의 층위. 위 두 층만 소리 내고, 아래 층은 마음속에서 한다.
```
   소리 내어 말한다   ┌─ 의도    "다음으로 샘플러 루프를 짠다. 벤더 호출은 락 밖이다."
                     ├─ 근거    "seqlock을 쓰는 이유는 payload가 40바이트이기 때문이다."
                     └─ 불변식  "여기서 유지할 것은 seq가 짝수면 payload가 일관된다는 것."
   ──────────────────────────────────────────────────────────────────────
   말하지 않는다      ├─ 타이핑  "이제 중괄호를 닫고... i를 0으로..."
                     ├─ 자책    "아 이거 제가 원래 잘 하는데..."
                     └─ 침묵    (10초 이상 아무 말 없음 = 가장 큰 감점)
```

---

## 3. 개념

### 3.1 평가 기준 4가지 → 행동으로 번역

추상적인 기준을 **관찰 가능한 행동**으로 바꾼다. 면접관은 마음을 읽지 못하고 행동만 본다.
| 리크루터가 쓴 기준 | 면접관이 찾는 신호 | 내가 하는 행동 | 언제 |
| --- | --- | --- | --- |
| ① 설계 결정과 trade-off 정당화 | 대안을 알고 **고른 이유와 버릴 조건**을 말한다 | "A를 고른다. 이유는 B. 대가는 C. D가 되면 E로 바꾼다" 문형을 결정마다 1회 | 3~7분, 각 결정 직후 |
| ② 깨끗하고 유지보수 가능한 코드 | 함수가 짧고, 이름이 정확하고, 임계 구역이 눈에 보인다 | `publish()` / `history_append()` / `ring_evict()`로 쪼갠다. 파일 상단에 설계 근거 3~6줄 | 7~40분, 타이핑하면서 |
| ③ edge case와 concurrent 견고성 | **묻기 전에** 경계와 종료를 먼저 꺼낸다 | §3.6 체크리스트를 코딩 전에 읊고, 반환 규약을 주석으로 먼저 적고, `deinit` 순서를 말한다 | 구현 전 + 20~24분 |
| ④ 커뮤니케이션과 사고 과정 | 10초 이상 침묵이 없고, 다음에 할 일을 예고한다 | 의도 → 근거 → 불변식만 말한다. 막히면 막혔다고 말한다 | 처음부터 끝까지 |

리크루터가 복습을 권한 네 주제도 힌트다. **thread safety / 동기화 수단(mutex, condvar, atomic) /
실시간 데이터 처리 패턴(더블 버퍼링) / 모듈화·테스트 가능한 설계.** "더블 버퍼링"을 명시했다는 것은
**그 단어를 듣고 싶다**는 뜻이고 그 문제가 `05`와 `09`다. "testable"은
[D1 §3.7](D1_wrapper_recipe.md)의 테스트 3층을 말하라는 신호다.

### 3.2 시작 3분 스크립트 (영어 그대로)

순서가 고정이다. **요구사항 질문 → 가정 선언 → API 선언 → 동기화 전략 선언.**
짧다. 3분이면 충분하고, 이 3분이 나머지 42분을 만든다.

**(1) 지문을 되읽어 준다 — 30초**

> *"Let me read the header back so we agree on what it guarantees. `read_next_sample()` blocks up to
> one second, returns NO_CHANGE when the light did not change, is not thread-safe because it keeps
> static state, and the very first call returns immediately. Did I miss anything?"*

**(2) 요구사항 질문 — 60초. 세 개만.**

> *"Three quick questions before I write anything. First — how many threads may be inside the vendor
> call? The header says static state, so I'll assume exactly one. Second — do readers need only the
> latest value, or the history as well? And does 'non-blocking' mean 'never waits for the device', or
> 'never takes a lock at all'? Third — what's the worst-case sample rate, and how long a window do
> you want? I need both numbers to size the buffer."*

**(3) 가정 선언 — 30초. 답이 없어도 진행한다.**

> *"I'll write down my assumptions and stop me if any is wrong. One sampler thread owns the vendor
> call. Readers never block on the device; they may take a lock held for a few assignments. A window
> of ten minutes at up to 100 samples per second, so I size the ring for that and drop the oldest."*

**(4) API 선언 — 30초.** *"Here's the surface I'm going to write."* 그리고 다섯 줄을 적는다:
`init` / `deinit` / `get_latest(out)` / `value_at(t, out)` / `aggregate(t0, t1)`.

**(5) 동기화 전략 선언 — 30초. ①번 항목이 여기서 채워진다.**

> *"The payload is a 40-byte struct, so there is no single atomic store for it. My default is a mutex
> snapshot — the critical section is three assignments, tens of nanoseconds. If you want readers to
> take no lock at all, I'd move to a seqlock: an even/odd counter with a release fence after the
> payload and an acquire fence before re-reading it. I'll start with the mutex so we have something
> correct."*

이렇게 3분을 쓰면 면접관은 이미 ①과 ④에 표시를 했다. 코드를 한 줄도 안 쓴 상태에서다.

### 3.3 코딩 중 내레이션 — 무엇을 말하고 무엇은 말하지 않나

**말한다 — 세 가지만.** **의도**(다음에 짤 것 예고): *"Now the sampler loop. The vendor call is
outside the lock — that's the whole point of the wrapper."* **근거**: *"I'm checking status before I
touch lux, because on NO_CHANGE the payload was never written."* **불변식**: *"The ring stays sorted
by timestamp by construction, so a binary search is legal."*

**말하지 않는다.** 타이핑 낭독 — 면접관이 화면을 보고 있다. 자책과 사과 — 인상만 나빠진다.
결론 없는 중얼거림 — ④를 오히려 깎는다. 그리고 **침묵**: 10초 이상 조용하면 사고 과정이 보이지
않으니, 생각할 시간이 필요하면 그걸 말한다 — *"Give me twenty seconds — I want to get the fence
placement right."* 비율의 감각은 **코드 10줄에 한 문장**이다.

### 3.4 trade-off를 말하는 문장 틀

틀은 하나다. 네 조각이고, **네 번째 조각이 점수를 만든다.**
> *"I'm choosing **A** over **B** because **`<the constraint that decides it>`**.
> The price is **`<cost>`**. I'd switch to **B** the moment **`<trigger>`**."*

한국어로 외우는 형태: **고른 것 → 버린 것 → 결정 근거 → 대가 → 바꿀 조건.**
"이게 더 좋습니다"로 끝내면 ①이 안 채워진다. **바꿀 조건**을 말하는 사람이 설계를 아는 사람이다.
아래 다섯 개는 이 저장소의 실제 문제에서 나온 것이다. 그대로 외운다.

**예시 1 — mutex vs atomic** (`02_modem_rssi_window` vs `01_gps_fix_cache`)

> *"For an int plus a timestamp I'd pack both into one 64-bit atomic — 16 bits of dBm, 48 bits of
> microseconds — so one load gives a consistent pair and a reader takes no lock at all. The price is
> range and resolution. The moment the payload grows past eight bytes — a GPS fix is forty — packing
> is off the table and I move to a seqlock."*

**예시 2 — double vs triple buffer** (`05_frame_latest_and_replay`, `09_wifi_scan_snapshot`)

> *"Two buffers are enough only if no reader ever holds one across a capture. The cloud uploader
> stalls for tens of milliseconds holding a frame — exactly that case — so with two buffers the
> writer would have to wait. The third buffer costs one more frame: four kilobytes here, eight
> megabytes on a real 4K frame, and at that size I'd cap outstanding borrows instead."*

**예시 3 — 넘칠 때 drop 정책** (`03_event_tailer_shutdown`, `07_badge_audit_dedupe`, `08_battery_energy_pipeline`)

> *"Bounded queue, so I pick one of three: block the producer, drop the newest, drop the oldest.
> Blocking the producer is worst here — it is the only thread allowed inside the vendor call, so a
> stalled consumer would stop us noticing events at all. For telemetry I drop the oldest and count it;
> for an audit log I bound the wait, then drop and count. The count is the deliverable — silent loss
> is the actual bug."*

**예시 4 — 버킷 vs 정확값** (`03_event_tailer_shutdown`, `04_temp_single_flight`)

> *"Buckets make memory independent of the event rate: 300 one-second buckets times four types is
> under five kilobytes, whether we see ten events a second or ten thousand. The price is resolution —
> the answer includes the whole bucket holding the cutoff. For a reporter polling once a second, one
> second of slop is free. If the caller needed exact edges I'd keep the raw samples for the two
> partial buckets at the ends, still bounded by the window and not by the sample count."*

**예시 5 — 해시 용량과 만료** (`07_badge_audit_dedupe`, `09_wifi_scan_snapshot`)

> *"Open addressing, 1024 slots, probe window sixteen, no malloc and no rehash — embedded box, fixed
> footprint. So 'load factor 0.75' is a design budget, not something the code enforces, and I need a
> policy for a full probe window: evict the oldest sighting inside it and count it. No tombstones
> either — entries expire on a time window, so an expired slot is handed to the next insert that
> probes over it. Chaining avoids that case at the cost of a malloc per badge, which I don't want."*

### 3.5 막혔을 때 / 모르는 API가 나왔을 때 / 힌트를 받았을 때

**막혔을 때.** 규칙은 하나다. **막혔다는 사실 자체를 말한다.** 침묵은 "생각이 없다"로 읽힌다.

> *"I'm stuck on the fence placement, and I'd rather say that out loud than sit here. What I know:
> the writer must publish the payload before the counter becomes even. What I'm unsure of: whether
> the reader's acquire fence goes before or after re-reading it. Let me write the invariant first."*

그리고 **느리지만 맞는 버전으로 내려간다.** *"Can I do the correct-but-slow version first — a mutex
and a linear scan — and then say exactly what I'd change to make the query logarithmic?"*
후퇴가 아니라 ①과 ③을 동시에 채우는 움직임이다.

**모르는 API가 나왔을 때.** 모른다고 말하고 **필요한 성질**을 말한다. 이름을 못 떠올린 것은
감점이 아니고, 무엇이 필요한지 모르는 것이 감점이다.

> *"I don't remember the exact argument order of `pthread_cond_timedwait` — may I write the shape and
> fix the signature after? What I need is a wait with a deadline that a broadcast can cut short, so
> shutdown doesn't have to wait the timeout out. And on macOS there's no `pthread_condattr_setclock`,
> so I'd use the relative variant there."*

**힌트를 받았을 때.** 절대 조용히 받아들이지 않는다. **왜 그게 더 나은지 내 말로 되말한다.**
그 되말하기가 ①에 그대로 들어간다. *"That's better than what I had — let me say why. With a prefix
sum the whole-segment part of the range becomes one subtraction, so the query stops depending on how
many samples are inside it. The cost is that I have to fix the base value when I evict the tail."*
힌트가 내가 이미 버린 대안이라면 그것도 말한다. 방어가 아니라 근거로. *"I did consider a linked
list — it removes the capacity guess. I went with the ring because of the malloc per sample. If the
sample rate were unbounded I'd agree, and the next step is a red-black tree: dynamic size and still
O(log n) for insert, floor lookup and delete-min."*

### 3.6 edge case를 먼저 읊는 체크리스트

**묻기 전에 먼저 말한다.** 열 문제가 전부 같은 경계를 검사하니 목록 하나로 끝난다. Part 2를
시작하기 전에 10초 동안 읊고, 반환 규약을 주석으로 먼저 적는다. *"Before I write the query, let me
state the contract for the edges — I'd rather decide these now than discover them later."*

- 샘플된 적 없다 → "no data"(`-1`/`NAN`/`RSSI_NONE`). `init` 직후는 첫 호출 특례로 값이 하나 있다
- `t`가 미래 → 실패. 윈도우보다 오래됨 → 실패 또는 윈도우 시작으로 clamp (어느 쪽인지 말한다)
- `t`가 첫 샘플보다 이전 → 실패. `t0 == t1` → 0. `t0 > t1` → 실패. 범위 안 샘플 0~1개 → 0, 보간 없음
- 윈도우 경계를 **덮는** 샘플 한 개는 evict하지 않는다
- 버퍼가 꽉 찼다 → 가장 오래된 것 evict + 카운트 노출
- 벤더 에러가 즉시 반환된다 → 백오프. 그냥 재시도하면 코어를 태운다
- "새 값 없음"(`NO_CHANGE`/`BUSY`/`STALE`) → publish 금지, 이전 값 유효
- 시계가 뒤로 점프 → 삽입 시 클램프해서 링을 단조로 유지
- `deinit` 후 getter → 크래시 대신 "no data". `deinit` 두 번 → no-op
- 빌려준 포인터가 남아 있다 → `release`는 `deinit` 후에도 동작해야 한다

마지막 두 줄이 가장 자주 빠지고 가장 자주 채점된다 — `05`와 `06`의 10번 질문이 정확히 이것이다.

### 3.7 마지막 5분에 할 질문

40분에 손을 뗀다. 남은 5분은 **테스트 전략 + 개선 여지 + 역질문**이다. 리크루터가 "testable"을
명시했으니 테스트를 먼저 말한다.

> *"How I'd test this: first the invariants — received equals popped plus dropped plus depth, and no
> value the device never produced. Second, a brute-force O(n) reference for the range query, compared
> against the fast path over recorded ground truth, not hard-coded numbers. Third, ThreadSanitizer.
> With more time the next thing I'd change is `<one concrete thing>`."*

그리고 질문 두세 개. **설계에 대한 질문이 가장 좋다** — ①을 한 번 더 채운다.
- *"If this shipped, what would you want changed first?"*
- *"Where would this break on your hardware? An RT kernel, or a core that also runs the encoder — that's where a low-priority reader holding the lock would hurt."*
- *"How do you handle this on the gateway today — one sampler thread per vendor library, or a shared bus thread?"*
- *"What does the next round look like, and what would you want me to prepare?"*

"회사 문화 어떤가요"는 리크루터 라운드에 한다. 엔지니어와 5분이 있으면 **엔지니어링을 묻는다.**

### 3.8 9/25 "10가지"에서 얻은 교훈

이 폴더의 `question_note` 열 개는 모두 하단이 *"Things the interviewer is listening for"* 10문항이고
비율이 고정이다 — **동시성 5 + 자료구조 3 + 에러·수명 2.** 원본 ALS 문제도 같다. 우연이 아니라
**채점표의 모양**이다.

| 반복되는 질문 | 무엇을 준비해 두는가 |
| --- | --- |
| 1번은 거의 항상 "왜 getter가 벤더를 직접 못 부르나"(10문제 중 7개) | 첫 관문은 코드가 아니라 이 한 문장이다. 두 가지 이유(지연 + 벤더 static 상태)를 **둘 다** 말한다 |
| 1~2번에 "몇 개의 스레드인가, 왜 그 수인가"가 들어간다 | 헤더의 "not thread-safe" 문장을 **인용**한다. `07`처럼 자원별로 갈리면 그 문장이 스레드 수를 정한다. `01` `02` `03` `06`과 원본은 "non-blocking의 의미"도 따로 묻는다 — 정의를 먼저 내린다 |
| 메모리·용량 계산을 거의 모든 문제에서 묻는다 | 곱셈을 소리 내어 한다. "10분 × 최악 100 Hz × 40 B = 2.4 MB, 그래서 링을 자르고 카운트한다" |
| 경계 조건과 시계 비단조 질문이 고정 | §3.6 체크리스트를 코딩 전에 읊는다. 시계는 "삽입할 때 클램프해 링을 단조로 유지한다" 한 문장 |
| 마지막 2문항은 **열 문제 전부** 에러 + 수명(종료)이다 | 시간이 없어도 `deinit` 순서는 **입으로** 말한다. 락 → `stop=1` → broadcast → 락 해제 → 탈출 장치 → join |
| 하나의 정답보다 **진행**을 원한다 | 원본에서 면접관이 직접 말했다: circular queue → linked list → red-black tree |

가장 큰 교훈은 마지막 줄이다. 힌트가 *"the most optimized one is a red-black tree"*였는데 원하는
답은 rbtree 하나가 아니라 **세 단계의 진행과 각 단계의 이유**였다. 그래서 이 저장소의 모든
`_solution.c` 상단에 선택 근거 주석이 3~6줄 들어 있다.

---

## 4. 코드로 보기 — 같은 로직, 다른 점수

②번 항목은 말이 아니라 코드로만 채워진다. 아래 두 버전은 **완전히 같은 동작**을 한다.
### 4.1 점수를 잃는 버전

```c
static float v; static uint64_t t; static int f;
static float buf[6000]; static uint64_t tb[6000]; static int h, n;

void *th(void *a) {
    (void)a;
    while (!stop) {
        struct SensorReading r = read_next_sample();
        if (r.status != VALID) continue;
        pthread_mutex_lock(&m);
        v = r.lux; t = r.timestamp; f = 1;
        buf[h] = r.lux; tb[h] = r.timestamp;
        h = (h + 1) % 6000; if (n < 6000) n++;
        pthread_mutex_unlock(&m);
    }
    return NULL;
}
```

무엇이 보이지 않나. 임계 구역이 "최신값 publish"인지 "히스토리 추가"인지 구분되지 않는다.
`6000`이 세 번 나온다. `v` `t` `f` `h` `n`이 무엇인지 이름이 말해 주지 않는다. 링이 꽉 찰 때 무엇이
사라지고 누가 책임지는지 코드에 없다.

### 4.2 점수를 받는 버전

```c
/* ALS wrapper — Part 1: mutex 스냅샷(임계 구역 = 대입 3줄). payload가 8 B를 넘으면 seqlock.
 *               Part 2: 링 + 이진 탐색. 10분 x 최악 100 Hz = 60000개 x 12 B = 720 KB.
 *                       넘치면 오래된 것 evict + 카운트. */
#define LUX_CAPACITY 60000u

struct Latest { float lux; uint64_t ts; int valid; };
static struct Latest  g_latest;                  /* g_mu 아래에서만 만진다 */
static struct LuxRing g_ring;                    /* 같은 mutex를 공유한다  */
static uint64_t       g_evicted;                 /* 사라진 샘플 수를 노출  */

/* 샘플러 스레드만 호출한다. 락을 잡은 채 벤더를 부르지 않는다. */
static void publish_latest(float lux, uint64_t ts) {
    g_latest.lux = lux;                          /* 셋을 함께 써야 독자가 */
    g_latest.ts  = ts;                           /* 같은 샘플을 본다      */
    g_latest.valid = 1;
}

/* 꽉 차면 tail을 버린다. evict는 한 곳에만 있어야 부가 배열과 어긋나지 않는다. */
static void ring_append(float lux, uint64_t ts) {
    if (ts <= g_ring.last_ts) ts = g_ring.last_ts;   /* NTP 역행 방어: 단조 유지 */
    if (g_ring.count == LUX_CAPACITY) { ring_drop_oldest(); g_evicted++; }
    /* ... push ...  */  g_ring.last_ts = ts;
}
```

바뀐 것: 상단 주석이 **설계 근거 + 메모리 숫자 + 넘칠 때 정책**을 말한다. 함수 이름이 임계 구역의
목적을 말한다. 용량이 이름 있는 상수 하나다. evict가 한 곳에 있다. 버려진 개수가 API로 나간다.

**주석에 쓸 것은 "무엇"이 아니라 "왜"다.** `/* lux를 대입한다 */`는 0점이고 `/* 셋을 함께 써야
독자가 같은 샘플을 본다 */`는 ①과 ②를 동시에 채운다.

---

## 5. 단계별로 만들어 보기 — 같은 45분, 세 가지 결과

### v0 — 침묵하며 코딩 (9/25에 실제로 일어난 일)

```
0:00  지문을 읽는다. 질문 없음.   0:02  바로 타이핑. 전역 변수부터.
0:09  "벤더를 락 안에서 불러야 하나" 고민. 조용히 3분.   0:18  컴파일 에러. 조용히 고친다.
0:31  Part 1이 대충 돈다. Part 2 이진 탐색에서 막힌다.   0:44  "시간이 다 됐네요."
```

채점: ①없음 ②반쯤 ③없음 ④없음. **코드는 60% 나왔는데 점수는 15%다.**

### v1 — 내레이션만 붙인다 (3분 스크립트는 여전히 없다)

같은 코드에 §3.3의 의도·근거·불변식을 얹는다.

```
0:00  "전담 스레드를 하나 만들고 최신값을 publish하겠습니다."                       (의도)
0:09  "락 밖에서 벤더를 부르는 이유는, 락 안이면 독자가 100 ms 기다립니다."          (근거)
0:31  "링은 삽입 시 단조를 유지하니 이진 탐색이 합법입니다."                         (불변식)
```

채점: ①조금 ②반쯤 ③조금 ④좋음. **같은 코드에서 점수가 두 배다.** 이것만으로도 크다.

### v2 — 3분 스크립트 + 경계 선언 + 마지막 5분

```
0:00  §3.2 스크립트. 되읽기 → 질문 3개 → 가정 → API 5줄 → 동기화 전략과 근거.       [①④]
0:03  구현 시작. 의도·근거·불변식 내레이션.                                           [②④]
0:20  Part 1 컴파일 통과. §3.6 체크리스트 읊기 + deinit 순서를 입으로.                [③]
0:26  Part 2. "메모리는 10분 x 100 Hz x 12 B = 720 KB." 자료구조 근거 1문장.          [①]
0:38  질의 절반. "여기서 멈추고 나머지를 말로 마치겠습니다." 복잡도 설명.             [①④]
0:40  테스트 3층 + 개선 여지.      0:42  역질문 두 개.                                [③①④]
```

**Part 2 코드가 v0보다 오히려 적다.** 그런데 네 항목이 다 채워진다. v2는 실력이 더 좋은 사람의
면접이 아니라, **같은 실력을 다르게 배치한 면접**이다.

---

## 6. 흔한 실수와 증상

| 실수 | 증상 | 왜 | 고치는 법 |
| --- | --- | --- | --- |
| 질문 없이 바로 코딩 | 30분에 요구사항을 오해했음이 드러나고 되돌릴 시간이 없다 | ①④가 처음 3분에만 싸게 채워지는데 그 구간을 버렸다 | §3.2 스크립트를 외운다. 되읽기 → 질문 3개 → 가정 → API → 전략 |
| 막힌 동안 침묵 | 면접관이 힌트를 줄 타이밍을 못 잡는다. 5분이 사라진다 | 사고 과정이 보이지 않으면 없는 것으로 채점된다 | "막혔다"를 말하고 아는 것 / 모르는 것 / 다음 시도를 한 문장씩 |
| "이게 더 좋습니다"로 끝내기 | ①이 채워지지 않는다. 대안을 모르는 것처럼 보인다 | trade-off는 대가와 **바꿀 조건**이 있어야 성립한다 | §3.4의 4조각 문형. 네 번째 조각("D가 되면 E로")을 반드시 붙인다 |
| 힌트를 조용히 받아들인다 | 면접관이 "이해했는지" 알 수 없다. ①이 비어 있다 | 수용은 신호가 아니다. **되말하기**가 신호다 | "That's better — let me say why: `<이유>`." 버렸던 대안이면 그 근거도 |
| edge case를 물을 때까지 기다린다 | ③이 면접관 주도로만 채워지고, 못 묻고 끝나면 0이다 | 견고성은 "먼저 꺼내는가"로 평가된다 | Part 2 시작 전에 §3.6을 10초 읊고 반환 규약을 주석으로 먼저 적는다 |

---

## 7. 손으로 확인하기

혼자 연습하는 방법은 하나다. **녹음한다.** 45분 타이머, 소리 내어, 영어로.
```sh
cd 01_gps_fix_cache
cp gps_cache.c /tmp/attempt.c                 # 스텁에서 시작
sed -n '/listening for/,$p' question_note     # 10문항은 나중에 채점용으로만
# 타이머 45분 + 폰 녹음기 켜기 → 소리 내어 풀기
./main.sh                                     # 45분 지점에서 몇 개 PASS인지 기록
```

녹음을 다시 들으며 다음 다섯 개를 센다. 이게 곧 채점표다.

- 3분 안에 요구사항 질문을 **세 개** 했는가
- "I'm choosing A over B because..." 문형이 **몇 번** 나왔는가 (목표: 최소 3회)
- **10초 이상 침묵**이 몇 번인가 (목표: 0회). Part 2 전에 경계 조건을 읊었는가
- 40분에 손을 뗐는가, 마지막 5분에 테스트 전략과 역질문이 있었는가

세 문제쯤 이렇게 하면 문형이 입에 붙는다. 그 다음 원본 문제로 마무리한다 —
`../../../verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions`
에서 `./main.sh sol`(링 + 이진 탐색)과 `./main.sh rbtree`(면접관이 원했던 세 번째 단계)를 나란히
돌려 보고, **"circular queue → linked list → red-black tree"의 진행을 영어로 90초 안에** 설명할 수
있으면 9/25에 놓친 것을 되찾은 것이다.

---

## 8. 자가 점검

```check
Q: 리크루터가 명시한 평가 기준 네 개 중, 코드를 완성해야만 채워지는 것은 몇 개인가?
A: 하나도 없다. ①trade-off 정당화 ③edge case와 견고성 ④커뮤니케이션은 전부 말로 채워지고,
   ②깨끗한 코드는 미완성 코드에서도 보인다(이름, 함수 분리, 상단 설계 주석, 임계 구역의 가시성).
   45분에 Part 2를 못 끝내도 네 항목을 다 채울 수 있다. 반대로 코드를 다 짜고 아무 말도 안 하면
   한 항목만 채워진다.

Q: trade-off 문장의 네 조각은 무엇이고, 어느 조각이 가장 자주 빠지나?
A: 고른 것 / 결정 근거 / 대가 / 바꿀 조건이다. 가장 자주 빠지는 것은 마지막 "바꿀 조건"이다.
   "이게 더 좋습니다"로 끝내면 대안을 모르는 것처럼 보인다. "The moment the payload grows past
   eight bytes I move to a seqlock"처럼 전환 조건을 붙이면 설계를 아는 사람으로 읽힌다.

Q: 20분인데 Part 1이 안 돌아간다. 무엇을 하나?
A: mutex + 선형 탐색이라는 느리지만 맞는 버전으로 내려간다. 그리고 그 결정을 말한다:
   "Can I do the correct-but-slow version first and then say exactly what I'd change?"
   후퇴가 아니라 ①과 ③을 동시에 채우는 움직임이다. 동작하는 것을 먼저 만들고 개선 경로를 아는
   사람을 찾고 있기 때문이다. 20분 지점의 판단 기준으로 미리 정해 두면 그 자리에서 고민하지 않는다.

Q: 면접관이 힌트를 줬다. 무엇을 하고 무엇을 하지 않나?
A: 조용히 받아들이지 않는다. 왜 그게 더 나은지 내 말로 되말한다 — "That's better than what I had,
   let me say why: `<이유>`. The cost is `<대가>`." 그 되말하기가 ①에 그대로 들어간다.
   내가 이미 고려했다가 버린 것이라면 그 근거도 말하되, 방어가 아니라 판단 근거로 말한다.
   9/25 원본에서 면접관은 rbtree 하나를 원한 게 아니라 세 단계의 진행을 원했다.

Q: 마지막 5분에 코딩을 멈추고 무엇을 하나? 왜 코딩보다 이득인가?
A: 테스트 전략 3층(불변식 / brute force 비교 / TSan), 개선 여지 한 가지, 역질문 두세 개다.
   5분 더 타이핑해서 얻는 것은 ②의 일부지만, 그 5분에 포기하는 것은 ③(테스트 가능성과 견고성)의
   마지막 조각과 ①(개선 경로)이다. 게다가 리크루터가 "modular, testable embedded software"를
   복습 주제로 명시했으니 테스트 이야기는 기다려지는 항목이다.
```

---

## 9. 요약 카드

- 평가 기준 네 개 중 **코드 완성이 필요한 것은 하나도 없다.** ①③④는 말로, ②는 미완성 코드로도 채워진다.
- **시작 3분 고정 순서**: 지문 되읽기 → 질문 3개 → 가정 선언 → API 5줄 → 동기화 전략과 근거.
- **trade-off 4조각**: 고른 것 / 근거 / 대가 / **바꿀 조건**. 네 번째가 점수다.
- **내레이션은 의도·근거·불변식만.** 타이핑 낭독·자책·침묵 금지. 코드 10줄에 한 문장.
- **막히면 막혔다고 말하고** 느리지만 맞는 버전으로 내려간다. 20분에 Part 1이 안 되면 mutex.
- **힌트는 되말한다.** "That's better — let me say why."
- **40분에 손을 뗀다.** 마지막 5분 = 테스트 3층 + 개선 여지 + 역질문.
- 연습법: 45분 타이머 + 녹음 + 영어. 침묵 횟수와 "I'm choosing A over B" 횟수를 센다.
