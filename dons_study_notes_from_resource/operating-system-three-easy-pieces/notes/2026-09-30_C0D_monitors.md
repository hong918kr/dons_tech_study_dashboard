# 부록 D 모니터 — 락과 조건 변수를 언어 구조로 묶은 동기화 (Deprecated)

> 📖 원문: [D. Monitors (Deprecated)](../book-md/C0D_monitors_deprecated.md) · [PDF p.622](../Operating%20Systems%20-%20Three%20Easy%20Pieces.pdf#page=622) · ⏱️ 읽기 약 30분 · 🔗 선행: [Ch.28 락](2026-09-30_C28_locks.md), [Ch.30 조건 변수](2026-09-30_C30_condition_variables.md), [Ch.31 세마포어](2026-09-30_C31_semaphores.md)

## 0. 한눈에 보기

이 부록에는 CRUX 박스가 없다. 대신 원문이 "다른 건 다 잊어도 이것만은 기억하라" 고 한 문장:

> "**always recheck the condition after being woken!** Put in even simpler terms, use while loops and not if statements when checking conditions."
> (깨어난 뒤에는 **항상 조건을 다시 검사하라!** 더 간단히: 조건 검사에는 if 가 아니라 while 을 써라.)

- **모니터(monitor)** = "한 번에 한 스레드만 들어갈 수 있는 클래스". 정체는 **메서드마다 자동으로 잡고 놓는 락 하나**다 (Brinch Hansen 1973, Hoare 1974).
- 상호 배제만으로는 부족해서 모니터는 **조건 변수(condition variable)** 를 함께 제공한다: `wait()` / `signal()`.
- **Hoare semantics**: signal 하면 **즉시** 깨운 스레드에게 락과 CPU 를 넘긴다 → 깨어난 쪽은 조건이 참이라고 믿어도 된다. 증명엔 좋지만 구현이 어렵다.
- **Mesa semantics** (Lampson & Redell, Xerox PARC): signal 은 **힌트**일 뿐 — 깨운 스레드를 ready 로 옮기기만 한다 → 그 사이 다른 스레드가 끼어들 수 있으니 **while 로 재검사** 필수. 오늘날 pthread, Java, Linux 커널 전부 Mesa 다.
- `signal()` 로 엉뚱한 스레드를 깨우는 경우엔 `broadcast()`(Java 의 `notifyAll()`) — 대가는 **thundering herd**.

## 1. 5분 복습표

| 용어 | 한 줄 뜻 | 예시 / 비유 |
|---|---|---|
| 모니터(monitor) | 한 번에 한 스레드만 실행 가능한 메서드 묶음 + 암묵적 락 | Java `synchronized` 클래스 |
| 모니터 락 | 모니터 메서드 진입 시 자동 획득, 퇴장 시 해제 | 메서드 앞뒤 `pthread_mutex_lock/unlock` |
| 조건 변수(CV) | 조건이 바뀔 때까지 잠들고 깨우는 대기 큐 | `cond_t full, empty` |
| 상태 변수(state variable) | CV 와 짝을 이루는 "진짜 조건" | `fullEntries` |
| wait() | 락을 놓고 잠듦, 깨어나면 락을 다시 잡고 반환 | `pthread_cond_wait(&c, &m)` |
| signal() / notify() | 기다리는 스레드 하나를 깨움 | `pthread_cond_signal` |
| broadcast() / notifyAll() | 기다리는 스레드 전부를 깨움 | `pthread_cond_broadcast` |
| Hoare semantics | signal 즉시 깨운 쪽이 락을 넘겨받아 실행 | 이론가의 정의 |
| Mesa semantics | signal 은 힌트, 깨운 쪽은 나중에 실행 | 모든 현대 시스템 |
| 힌트(hint) | 대체로 맞지만 틀릴 수 있는 정보 (Lampson) | "아마 데이터 있을 거야" |
| thundering herd | broadcast 로 다 깨웠는데 하나만 진행, 나머지는 다시 잠듦 | 불필요한 문맥 교환 폭증 |
| ready / monitor / CV 큐 | Mesa 구현에서 스레드가 머무는 세 종류의 큐 | 그림 D.6 |

## 2. 모니터란 무엇인가 (도입)

동시성 프로그래밍이 중요해질 무렵 객체 지향도 떠오르고 있었다. 자연스럽게 "동기화를 구조적 프로그래밍 환경에 녹이자" 는 생각이 나왔고, 그게 **모니터**다.

C++ 문법으로 쓴 "가짜" 모니터 클래스(Figure D.1):

```c
monitor class account {
private:
    int balance = 0;
public:
    void deposit(int amount)  { balance = balance + amount; }
    void withdraw(int amount) { balance = balance - amount; }
};
```

C++ 엔 `monitor` 키워드가 없다(그래서 "가짜"). `deposit`, `withdraw` 는 원래 **임계 구역**이라 여러 스레드가 동시에 부르면 경쟁 조건이 생긴다. 하지만 모니터는 **한 번에 한 스레드만 모니터 안에서 활동**하도록 보장하므로 이 코드는 안전하다.

어떻게? **락**으로. 모니터 메서드를 부르면 암묵적으로 모니터 락을 잡으려 하고, 성공하면 실행, 실패하면 안에 있는 스레드가 끝날 때까지 블록된다. 그러니 아래 진짜 C++ 클래스(Figure D.2)와 완전히 같다.

```c
class account {
private:
    int balance = 0;
    pthread_mutex_t monitor;
public:
    void deposit(int amount) {
        pthread_mutex_lock(&monitor);
        balance = balance + amount;
        pthread_mutex_unlock(&monitor);
    }
    void withdraw(int amount) {
        pthread_mutex_lock(&monitor);
        balance = balance - amount;
        pthread_mutex_unlock(&monitor);
    }
};
```

모니터가 자동으로 해 주는 일은 이게 전부다: **락 잡고 놓기**.

## 3. 왜 굳이 모니터를? (D.1)

명시적 락 대신 모니터를 만든 이유는 기술적이라기보다 시대적이다. 객체 지향이 유행하기 시작했으니 동시성의 핵심 개념을 객체 지향의 기본 방식과 **우아하게 섞어 보자**는 것. 그 이상도 이하도 아니다.

## 4. 자동 락 이상의 것: 조건 변수 (D.2)

세마포어 장에서 봤듯 락만으로는 부족하다. 생산자/소비자 문제를 풀려면 "조건이 바뀔 때까지 잠들기"(버퍼가 빌 때까지 기다리는 생산자)와 "조건이 바뀌었다고 깨우기"(버퍼를 비운 소비자가 알려주기)가 필요하다. 모니터는 이를 **조건 변수**라는 명시적 구조로 제공한다.

Hoare 의 해법을 현대식으로 옮긴 bounded buffer (Figure D.3):

```c
monitor class BoundedBuffer {
  private:
    int buffer[MAX];
    int fill, use;
    int fullEntries = 0;
    cond_t empty;
    cond_t full;
  public:
    void produce(int element) {
        if (fullEntries == MAX)    // line P0
            wait(&empty);          // line P1
        buffer[fill] = element;    // line P2
        fill = (fill + 1) % MAX;   // line P3
        fullEntries++;             // line P4
        signal(&full);             // line P5
    }
    int consume() {
        if (fullEntries == 0)      // line C0
            wait(&full);           // line C1
        int tmp = buffer[use];     // line C2
        use = (use + 1) % MAX;     // line C3
        fullEntries--;             // line C4
        signal(&empty);            // line C5
        return tmp;                // line C6
    }
}
```

세마포어 해법과의 큰 차이: 조건 변수는 **외부 상태 변수**(`fullEntries`)와 **짝을 이뤄야** 한다. 세마포어는 내부에 정수 값이 있어서 그 자체가 상태 역할을 하지만, CV 는 "기다리는 줄" 일 뿐 상태가 없다.

`wait()` 은 호출한 스레드를 그 조건에서 재우고, `signal()` 은 그 조건에서 기다리는 스레드 **하나**를 깨운다. 여기까진 직관대로다. 미묘한 건 **signal 직후 누가 실행되느냐**다.

### Hoare semantics

Hoare semantics 에서 `signal()` 은 기다리던 스레드 하나를 **즉시 깨워 실행**한다. 실행 중인 스레드가 암묵적으로 쥐고 있던 모니터 락이 **깨어난 스레드에게 바로 넘어가고**, 깨어난 스레드는 블록되거나 모니터를 나갈 때까지 달린다. 기다리는 스레드가 여럿이면 signal 은 하나만 깨우고 나머지는 다음 signal 을 기다린다.

예: 생산자 1, 소비자 1.

1. 소비자가 먼저 `consume()` → `fullEntries == 0` (C0) → `wait(&full)` (C1).
2. 생산자가 와서 기다릴 필요 없음 (P0) → 버퍼에 넣고 (P2) → fill 증가 (P3) → `fullEntries` 증가 (P4) → `signal(&full)` (P5).
3. Hoare 에서는 생산자가 **signal 후 계속 달리지 않는다**. 제어가 즉시 소비자에게 넘어가고, 소비자가 `wait()` 에서 돌아와(C1) 방금 생산된 원소를 바로 소비한다(C2...).
4. 소비자가 모니터를 나간 뒤에야 생산자가 다시 돌아 `produce()` 에서 반환한다.

깨어난 순간 **조건이 참인 것이 보장**되므로 `if` 로 충분하다.

## 5. 이론과 현실 (D.3)

Hoare 는 이론가였다(퀵소트도 만들었다). 그의 signal/wait 의미론은 실제 구현에는 이상적이지 않았다.

> **OLD SAYING — 이론 vs 실제**: "이론상으로는 이론과 실제에 차이가 없다. 하지만 실제로는 있다." 물론 이건 실무자만 하는 말이다. 이론가라면 이게 틀렸다고 증명할 수 있을 테니까.

몇 년 뒤 Xerox PARC 의 **Butler Lampson** 과 **David Redell** 이 동시성 언어 **Mesa** 를 만들며 모니터를 기본 동시성 도구로 썼다. Hoare semantics 는 증명엔 좋지만 실제 시스템에선 구현이 어려웠다(락 소유권을 강제로 넘기고 즉시 문맥 교환해야 하는 등). 그래서 `signal()` 의 의미를 미묘하지만 결정적으로 바꿨다.

- `signal()` 은 이제 **힌트(hint)**: 기다리던 스레드 하나를 blocked → **runnable** 로 옮기기만 하고 **즉시 실행하지 않는다**.
- signal 한 스레드는 모니터를 나가고 디스케줄될 때까지 **제어를 유지**한다.

(Lampson 의 "Hints for Computer Systems Design" 에서 hint 는 "대체로 맞지만 틀릴 수 있는 것". 그 논문의 일반 힌트 중 하나가 "힌트를 써라" 다.)

```svg
<svg viewBox="0 0 720 300" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="12">
  <defs>
    <marker id="C0D-arrow" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/>
    </marker>
  </defs>
  <text x="10" y="20" font-size="13" font-weight="bold" fill="currentColor">Hoare: signal = 락과 CPU 를 즉시 넘김</text>
  <text x="10" y="50" fill="currentColor">Producer</text>
  <text x="10" y="80" fill="currentColor">Consumer1</text>
  <text x="10" y="110" fill="currentColor">Consumer2</text>
  <rect x="90" y="38" width="200" height="18" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="190" y="51" text-anchor="middle" fill="currentColor">P0 P2 P3 P4 P5(signal)</text>
  <rect x="290" y="68" width="200" height="18" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="390" y="81" text-anchor="middle" fill="currentColor">C1 반환 → C2..C6 (데이터 있음 보장)</text>
  <rect x="490" y="38" width="70" height="18" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="525" y="51" text-anchor="middle" fill="currentColor">반환</text>
  <rect x="560" y="98" width="140" height="18" fill="none" stroke="currentColor"/>
  <text x="630" y="111" text-anchor="middle" fill="currentColor">C0: 비었음 → wait</text>
  <line x1="290" y1="56" x2="290" y2="66" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C0D-arrow)"/>
  <text x="296" y="64" style="fill:var(--accent)" font-size="11">락 직접 전달</text>
  <line x1="10" y1="140" x2="710" y2="140" stroke="currentColor" stroke-dasharray="3 3"/>
  <text x="10" y="165" font-size="13" font-weight="bold" fill="currentColor">Mesa: signal = "ready 로 옮김" 힌트일 뿐</text>
  <text x="10" y="195" fill="currentColor">Producer</text>
  <text x="10" y="225" fill="currentColor">Consumer1</text>
  <text x="10" y="255" fill="currentColor">Consumer2</text>
  <rect x="90" y="183" width="230" height="18" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="205" y="196" text-anchor="middle" fill="currentColor">P0 P2 P3 P4 P5(signal) … 반환</text>
  <rect x="90" y="213" width="230" height="18" fill="none" stroke="currentColor" stroke-dasharray="4 3"/>
  <text x="205" y="226" text-anchor="middle" fill="currentColor">C1 에서 대기 → ready (아직 못 돎)</text>
  <rect x="320" y="243" width="200" height="18" style="fill:var(--accent-soft)" stroke="currentColor"/>
  <text x="420" y="256" text-anchor="middle" fill="currentColor">C0 C2 C3 C4 C5 C6 (끼어들어 소비)</text>
  <rect x="520" y="213" width="185" height="18" fill="none" stroke="#d9534f"/>
  <text x="612" y="226" text-anchor="middle" fill="#d9534f">C1 반환 → fullEntries=0 !</text>
  <text x="612" y="285" text-anchor="middle" fill="currentColor">→ while 이면 C0 재검사 후 다시 wait</text>
  <line x1="320" y1="201" x2="320" y2="241" style="stroke:var(--accent)" stroke-width="1.5" marker-end="url(#C0D-arrow)"/>
  <text x="326" y="236" style="fill:var(--accent)" font-size="11">락 해제 → 누가 먼저 잡을지 모름</text>
</svg>
```

## 6. 앗, 경쟁 조건이다 (D.4)

Mesa semantics 로 Figure D.3 의 코드를 다시 보자.

1. 소비자 1 이 들어와 버퍼가 빈 걸 보고 기다린다 (C1).
2. 생산자가 버퍼를 채우고 signal → 소비자 1 은 full CV 에서 blocked → **ready** 로. 생산자는 한동안 더 달리다 CPU 를 내놓는다.
3. 이때 **소비자 2** 가 `consume()` 을 호출 → 버퍼가 차 있으니 바로 소비하고 반환, `fullEntries` 는 0.
4. 드디어 소비자 1 이 실행되어 `wait()` 에서 반환 — 버퍼가 차 있으리라 기대하지만 **이미 비어 있다**.

signal 과 wait 반환 사이에 **조건이 바뀌었다**. Figure D.4 타임라인:

```text
Producer               Consumer1                  Consumer2
                       C0 (fullEntries=0)
                       C1 (Consumer 1: blocked)
P0 (fullEntries=0)
P2
P3
P4 (fullEntries=1)
P5 (Consumer1: ready)
                                                  C0 (fullEntries=1)
                                                  C2
                                                  C3
                                                  C4 (fullEntries=0)
                                                  C5
                                                  C6
                       C2 (using a buffer,
                           fullEntries=0!)
```

다행히 고치는 건 쉽다. 깨어난 스레드는 기다리던 **조건을 다시 검사**해야 한다 — signal 은 힌트일 뿐이니 조건이 (여러 번) 바뀌었을 수 있다. P0 와 C0 두 줄만 `if` → `while` (Figure D.5):

```c
void produce(int element) {
    while (fullEntries == MAX)   // line P0 (CHANGED IF->WHILE)
        wait(&empty);            // line P1
    ...
}
int consume() {
    while (fullEntries == 0)     // line C0 (CHANGED IF->WHILE)
        wait(&full);             // line C1
    ...
}
```

구현이 이렇게 쉬워서 오늘날 signal/wait 를 쓰는 사실상 모든 시스템이 Mesa semantics 다. **while 은 언제나 정답**이다 — 혹시 Hoare semantics 시스템에서 돌더라도 조건을 한 번 쓸데없이 더 검사할 뿐이다. 덤으로 POSIX 가 허용하는 **spurious wakeup**(아무도 signal 안 했는데 깨어남)도 같은 while 이 막아 준다.

## 7. 내부를 살짝 들여다보기 (D.5)

왜 Mesa 가 구현하기 쉬운지 보자. Lampson 과 Redell 은 스레드가 머무를 수 있는 세 종류의 큐를 설명한다: **ready 큐**, **모니터 락 큐**, **조건 변수 큐**. 모니터와 CV 인스턴스마다 큐가 하나씩이니, bounded buffer 모니터 하나면 큐는 4개: ready, 모니터 1개, CV 2개(full, empty).

```svg
<svg viewBox="0 0 720 290" xmlns="http://www.w3.org/2000/svg" font-family="-apple-system, sans-serif" font-size="12">
  <defs>
    <marker id="C0D-arrow2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" fill="currentColor"/>
    </marker>
    <marker id="C0D-arrow3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse">
      <path d="M0,0 L10,5 L0,10 z" style="fill:var(--accent)"/>
    </marker>
  </defs>
  <rect x="270" y="20" width="180" height="70" rx="8" style="fill:var(--accent-soft)" stroke="currentColor" stroke-width="1.5"/>
  <text x="360" y="45" text-anchor="middle" font-size="13" font-weight="bold" fill="currentColor">모니터 안 (Running)</text>
  <text x="360" y="68" text-anchor="middle" fill="currentColor">락을 쥔 스레드 단 하나</text>
  <rect x="20" y="130" width="190" height="50" rx="6" fill="none" stroke="currentColor"/>
  <text x="115" y="150" text-anchor="middle" font-weight="bold" fill="currentColor">Ready 큐</text>
  <text x="115" y="168" text-anchor="middle" fill="currentColor">실행 가능, CPU 대기</text>
  <rect x="265" y="130" width="190" height="50" rx="6" fill="none" stroke="currentColor"/>
  <text x="360" y="150" text-anchor="middle" font-weight="bold" fill="currentColor">Monitor 락 큐</text>
  <text x="360" y="168" text-anchor="middle" fill="currentColor">진입하려는데 락이 잡힘</text>
  <rect x="510" y="110" width="190" height="40" rx="6" fill="none" stroke="currentColor"/>
  <text x="605" y="135" text-anchor="middle" font-weight="bold" fill="currentColor">CV 큐: full</text>
  <rect x="510" y="165" width="190" height="40" rx="6" fill="none" stroke="currentColor"/>
  <text x="605" y="190" text-anchor="middle" font-weight="bold" fill="currentColor">CV 큐: empty</text>
  <line x1="450" y1="60" x2="590" y2="108" stroke="currentColor" stroke-width="1.5" marker-end="url(#C0D-arrow2)"/>
  <text x="540" y="70" fill="currentColor">wait(): 락 놓고</text>
  <text x="540" y="85" fill="currentColor">CV 큐로</text>
  <path d="M510 200 C 380 260, 200 250, 120 182" fill="none" style="stroke:var(--accent)" stroke-width="2" marker-end="url(#C0D-arrow3)"/>
  <text x="330" y="270" text-anchor="middle" style="fill:var(--accent)">signal(): CV 큐에서 하나를 Ready 로 (Mesa — 실행은 나중에)</text>
  <line x1="115" y1="130" x2="300" y2="92" stroke="currentColor" stroke-width="1.5" marker-end="url(#C0D-arrow2)"/>
  <text x="120" y="100" fill="currentColor">스케줄됨 + 락 획득</text>
  <line x1="360" y1="130" x2="360" y2="92" stroke="currentColor" stroke-width="1.5" marker-end="url(#C0D-arrow2)"/>
  <text x="366" y="118" fill="currentColor">락 풀림</text>
  <text x="20" y="225" fill="currentColor" font-size="11">깨어난 스레드는 다른 스레드들과</text>
  <text x="20" y="240" fill="currentColor" font-size="11">똑같이 락을 경쟁해야 한다 → 조건 재검사 필요</text>
</svg>
```

Figure D.6 — 소비자 둘(Con1, Con2), 생산자 하나(Prod). `FE` = fullEntries, Full 열 = full CV 큐의 대기자.

```text
 t | Con1 Con2 Prod | Mon | Empty | Full | FE | Comment
--------------------------------------------------------------------
 0   C0                                    0
 1   C1                             Con1   0   Con1 waiting on full
 2  <Context switch>                Con1   0   switch: Con1 to Prod
 3             P0                   Con1   0
 4             P2                   Con1   0   Prod doesn't wait (FE=0)
 5             P3                   Con1   0
 6             P4                   Con1   1   Prod updates fullEntries
 7             P5                          1   Prod signals: Con1 now ready
 8  <Context switch>                       1   switch: Prod to Con2
 9        C0                               1   switch to Con2
10        C2                               1   Con2 doesn't wait (FE=1)
11        C3                               1
12        C4                               0   Con2 changes fullEntries
13        C5                               0   Con2 signals empty (no waiter)
14        C6                               0   Con2 done
15  <Context switch>                       0   switch: Con2 to Con1
16   C0                                    0   recheck fullEntries: 0!
17   C1                             Con1   0   wait on full again
```

t=7 에 생산자의 signal 로 Con1 이 깨지만(Full 큐에서 빠져 ready 로), Con2 가 t=9~14 에 먼저 끼어들어 데이터를 가져간다. Con1 은 t=16 에 `fullEntries` 를 **재검사**해서 0 임을 보고 t=17 에 다시 잠든다. 재검사가 없었다면 없는 데이터를 소비했을 것. 이 "자연스러운 구현" 이 곧 Mesa semantics 로 이어진다 — signal 하는 쪽이 락을 넘기는 복잡한 일을 할 필요 없이, 그냥 대기자를 ready 큐로 옮기기만 하면 되니까.

## 8. 모니터의 다른 쓰임: broadcast (D.6)

Lampson 과 Redell 은 다른 종류의 신호가 필요한 경우도 지적했다. 간단한 메모리 할당기 (Figure D.7):

```c
monitor class allocator {
    int available; // how much memory is available?
    cond_t c;
    void *allocate(int size) {
        while (size > available)
            wait(&c);
        available -= size;
        // and then do whatever the allocator should do
    }
    void free(void *pointer, int size) {
        available += size;
        signal(&c);
    }
};
```

문제: 스레드 둘이 `allocate(20)` 과 `allocate(10)` 을 불렀는데 메모리가 없어서 둘 다 잔다. 다른 스레드가 `free(p, 15)` 로 15바이트를 풀고 signal 한다. 하필 **20바이트를 기다리던 스레드**를 깨우면, 그 스레드는 15 < 20 을 보고 다시 잔다. 15바이트로 충분했던 `allocate(10)` 스레드는 **깨어나지 못한다**.

해법: `signal()` 대신 기다리는 스레드를 **모두** 깨우는 **`broadcast()`**. 10바이트 스레드도 깨어나 15바이트를 보고 성공한다.

Mesa 에서 broadcast 는 **항상 정확하다** — 어차피 깨어난 모두가 조건을 재검사하니까. 하지만 성능 문제가 있다. 수백 개를 깨웠는데 하나만 진행하고 나머지가 곧바로 다시 잠들면, 추가 문맥 교환이 엄청나다. 이것이 **thundering herd** 문제다.

### 계산 예제: thundering herd 비용

대기자 N = 100, 문맥 교환 1회 ≈ 5 µs, free 한 번에 실제로 진행 가능한 스레드는 1개라고 하자.

- signal: 깨어남 1 → 문맥 교환 약 1회 → **5 µs** (단, 엉뚱한 스레드를 깨우면 아무도 진행 못 함)
- broadcast: 깨어남 100 → 각자 락을 잡고 조건 검사 후 99개가 다시 잠듦 → 문맥 교환 약 100회 이상 → **≥ 500 µs**, 게다가 100개가 **같은 락**을 두고 줄을 서므로 락 경합까지.

free 가 초당 1만 번 일어나면 broadcast 비용만 5 CPU-초/초 — 코어 5개를 그냥 태운다. 그래서 실전에선 "조건별로 CV 를 나누기"(크기 클래스별 CV), "충분할 것 같은 대기자만 골라 깨우기" 같은 최적화를 한다.

## 9. 모니터로 세마포어 만들기 (D.7)

모니터와 세마포어는 서로를 구현할 수 있다. 모니터로 세마포어를 만들면 (Figure D.8):

```c
monitor class Semaphore {
    int s; // value of the semaphore
    Semaphore(int value) { s = value; }
    void wait() {
        while (s <= 0)
            wait();
        s--;
    }
    void post() {
        s++;
        signal();
    }
};
```

`wait()` 는 값이 0보다 커질 때까지 기다렸다 감소, `post()` 는 증가시키고 (있다면) 대기자 하나를 깨운다. 1로 초기화해 임계 구역 앞뒤에 wait/post 를 두면 **이진 세마포어 = 락**. 아래 "직접 해보기" [4] 에서 실제로 돌려 본다. (반대 방향 — 세마포어로 CV 만들기 — 는 훨씬 까다롭다는 게 유명하다. Ch.31 참고.)

## 10. 현실 세계의 모니터 (D.8)

### 10.1 C++ 로 흉내 낸 모니터

C++ 에는 모니터가 없으니 락과 CV 로 직접 만든다 (Figure D.9):

```c
class BoundedBuffer {
  private:
    int buffer[MAX];
    int fill, use;
    int fullEntries;
    pthread_mutex_t monitor; // monitor lock
    pthread_cond_t empty;
    pthread_cond_t full;
  public:
    BoundedBuffer() { use = fill = fullEntries = 0; }
    void produce(int element) {
        pthread_mutex_lock(&monitor);
        while (fullEntries == MAX)
            pthread_cond_wait(&empty, &monitor);
        buffer[fill] = element;
        fill = (fill + 1) % MAX;
        fullEntries++;
        pthread_cond_signal(&full);
        pthread_mutex_unlock(&monitor);
    }
    int consume() {
        pthread_mutex_lock(&monitor);
        while (fullEntries == 0)
            pthread_cond_wait(&full, &monitor);
        int tmp = buffer[use];
        use = (use + 1) % MAX;
        fullEntries--;
        pthread_cond_signal(&empty);
        pthread_mutex_unlock(&monitor);
        return tmp;
    }
}
```

가짜 모니터 코드와 거의 같다. 차이는 (1) 명시적 락 "monitor", (2) POSIX `pthread_cond_wait()` 에 **잡고 있는 락을 같이 넘긴다**는 점. 잠들 때 그 락을 **놓고**, 반환 전에 **다시 잡아야** 하기 때문이다 — 모니터 안에서 일어나던 일을 명시적 락으로 하는 것.

### 10.2 Java 의 모니터: synchronized

Java 설계자들은 모니터가 언어에 동기화를 넣는 우아한 방법이라 보고 채택했다. 메서드에 `synchronized` 만 붙이면 된다 (Figure D.10):

```java
public class SynchronizedCounter {
    private int c = 0;
    public synchronized void increment() { c++; }
    public synchronized void decrement() { c--; }
    public synchronized int value() { return c; }
}
```

한 번에 한 스레드만 모니터 안에 들어가므로 `c` 를 둘러싼 경쟁 조건이 없다.

### 10.3 Java 와 단 하나의 조건 변수

초기 Java 는 synchronized 클래스마다 CV 를 **딱 하나** 줬다. `wait()` 와 `notify()`(= signal)로 쓴다. 그런데 생산자/소비자엔 CV 가 **두 개**(empty, full) 필요하다. 하나뿐이면?

MAX = 1, 소비자 둘이 먼저 돌아 둘 다 잔다. 생산자가 버퍼 하나를 채우고 소비자 하나를 깨운 뒤, 또 채우려다 버퍼가 차서 잔다. 이제 상태는: 빈 버퍼를 기다리는 생산자, 찬 버퍼를 기다리는 소비자, 깨어나 곧 돌 소비자.

깨어난 소비자가 버퍼를 비우고 `notify()` 하면 — CV 가 하나라서 — **생산자 대신 다른 소비자를 깨울 수 있다**. 그 소비자는 버퍼가 비었으니 다시 잔다. 이제 **모두가 잔다**. 해법은 다시 broadcast: Java 의 `notifyAll()`. 생산자와 소비자를 다 깨우면 소비자는 `fullEntries == 0` 을 보고 다시 자고 생산자는 진행한다. 물론 thundering herd 가 따라온다.

이 결함 때문에 Java 는 나중에 명시적 **`Condition` 클래스**(`java.util.concurrent.locks`)를 추가해, 락 하나에 CV 여러 개를 둘 수 있게 했다.

## 11. 요약 (D.9)

- 모니터는 Brinch Hansen 과 Hoare 가 70년대 초에 만든 구조화 개념이다. 모니터 안에서 실행 중인 스레드는 **암묵적으로 모니터 락**을 쥐고 있어 다른 스레드의 진입을 막으므로 상호 배제가 쉽게 만들어진다.
- 명시적 **조건 변수**로 signal/wait 를 할 수 있다. 의미론이 결정적인데, 현대 시스템은 모두 **Mesa semantics** 라서 잠들었던 조건을 깨어난 뒤 **재검사**해야 한다. signal 은 "뭔가 바뀌었다" 는 **힌트**일 뿐, 계속 진행해도 되는 조건인지 확인하는 건 깨어난 스레드의 책임이다.
- C++ 에는 모니터가 없으니 pthread 락과 CV 로 흉내 냈고, Java 는 synchronized 로 지원하며, CV 가 하나뿐일 때의 한계도 봤다.

## 12. 직접 해보기

### 12.1 C 코드: 모니터 4종 세트

`code/C0D_monitor.c` 하나로 원문의 네 예제를 실제로 돌린다.

1. Figure D.2 — mutex 하나를 모니터 락으로 쓴 계좌 vs 락 없는 계좌.
2. Figure D.5 — Mesa bounded buffer(MAX=1, 소비자 4). `while` 로 재검사하면서 "깨어났는데 조건이 거짓" 인 횟수를 센다 = `if` 였다면 버그가 났을 횟수.
3. Figure D.7 — 할당기에서 signal vs broadcast.
4. Figure D.8 — 모니터(mutex+cond)로 만든 세마포어를 락으로 사용.

```c
// C0D_monitor.c — 부록 D "모니터" 를 C(pthread)로 흉내 내기
//
//  [1] Figure D.2: mutex 하나 = 모니터 락. deposit/withdraw 를 4 스레드가 동시에 호출
//  [2] Figure D.4/D.5: Mesa semantics — signal 로 깨어났는데 조건이 이미 거짓인 경우가
//      실제로 몇 번 생기는지 센다. (if 로 썼다면 그 횟수만큼 빈 버퍼에서 꺼냈을 것)
//  [3] Figure D.7: 메모리 할당기 — signal() 하나로는 엉뚱한 스레드를 깨울 수 있다 → broadcast()
//  [4] Figure D.8: 모니터(mutex+cond)로 세마포어 만들기 → 그 세마포어를 락으로 사용
//
// build: cc -Wall -Wextra -O0 -pthread code/C0D_monitor.c -o .work/bin/C0D_monitor && .work/bin/C0D_monitor
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

// ------------------------------------------------------------ [1] account monitor
typedef struct { int balance; pthread_mutex_t monitor; } account_t;
static account_t the_acct = {0, PTHREAD_MUTEX_INITIALIZER};
static int racy_balance = 0;

static void deposit(account_t *a, int amt)  { pthread_mutex_lock(&a->monitor); a->balance += amt; pthread_mutex_unlock(&a->monitor); }
static void withdraw(account_t *a, int amt) { pthread_mutex_lock(&a->monitor); a->balance -= amt; pthread_mutex_unlock(&a->monitor); }

static void *acct_worker(void *arg) {
    long id = (long)arg;
    for (int i = 0; i < 1000000; i++) {
        if (id % 2) { deposit(&the_acct, 2); racy_balance += 2; }   // racy_balance: 모니터 없이 같은 일
        else        { withdraw(&the_acct, 1); racy_balance -= 1; }
    }
    return NULL;
}

// ------------------------------------------------------------ [2] bounded buffer (Mesa)
#define MAX 1
static int buffer[MAX], fill_i, use_i, fullEntries;
static pthread_mutex_t bb = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t empty = PTHREAD_COND_INITIALIZER, full = PTHREAD_COND_INITIALIZER;
static long woke_but_empty, woke_but_full, consumed_sum;
#define ITEMS 200000
#define NCONS 4

static void produce(int x) {
    pthread_mutex_lock(&bb);
    int waited = 0;
    while (fullEntries == MAX) {                 // P0 (if → while)
        if (waited) woke_but_full++;
        pthread_cond_wait(&empty, &bb);          // P1
        waited = 1;
    }
    buffer[fill_i] = x; fill_i = (fill_i + 1) % MAX; fullEntries++;   // P2-P4
    pthread_cond_signal(&full);                  // P5
    pthread_mutex_unlock(&bb);
}
static int consume(void) {
    pthread_mutex_lock(&bb);
    int waited = 0;
    while (fullEntries == 0) {                   // C0 (if → while)
        if (waited) woke_but_empty++;            // 깨어났는데 또 비어 있음 = Figure D.4 상황
        pthread_cond_wait(&full, &bb);           // C1
        waited = 1;
    }
    int tmp = buffer[use_i]; use_i = (use_i + 1) % MAX; fullEntries--;  // C2-C4
    pthread_cond_signal(&empty);                 // C5
    pthread_mutex_unlock(&bb);
    return tmp;                                  // C6
}
static void *producer(void *arg) {
    (void)arg;
    for (int i = 1; i <= ITEMS; i++) produce(i);
    for (int i = 0; i < NCONS; i++) produce(-1);  // 종료 신호
    return NULL;
}
static void *consumer(void *arg) {
    (void)arg;
    long s = 0;
    for (int v; (v = consume()) != -1;) s += v;
    pthread_mutex_lock(&bb); consumed_sum += s; pthread_mutex_unlock(&bb);
    return NULL;
}

// ------------------------------------------------------------ [3] allocator: signal vs broadcast
static int available = 0, use_broadcast = 0;
static pthread_mutex_t am = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t ac = PTHREAD_COND_INITIALIZER;

static void *alloc_thread(void *arg) {
    int size = (int)(long)arg;
    pthread_mutex_lock(&am);
    while (size > available) {
        pthread_cond_wait(&ac, &am);
        if (size > available) printf("    allocate(%d): 깨어났지만 available=%d → 다시 잠듦\n", size, available);
    }
    available -= size;
    printf("    allocate(%d): 성공 (남은 available=%d)\n", size, available);
    pthread_mutex_unlock(&am);
    return NULL;
}
static void my_free(int size) {
    pthread_mutex_lock(&am);
    available += size;
    if (use_broadcast) pthread_cond_broadcast(&ac); else pthread_cond_signal(&ac);
    pthread_mutex_unlock(&am);
}
static void allocator_demo(int bcast) {
    use_broadcast = bcast; available = 0;
    printf("  -- free() 가 %s 사용 --\n", bcast ? "broadcast()" : "signal()");
    pthread_t t20, t10;
    pthread_create(&t20, NULL, alloc_thread, (void *)20L); usleep(50000);   // 20 이 먼저 대기
    pthread_create(&t10, NULL, alloc_thread, (void *)10L); usleep(50000);
    printf("    free(15)\n");
    my_free(15);
    usleep(200000);
    pthread_mutex_lock(&am);
    printf("    200ms 뒤 available=%d %s\n", available,
           available == 15 ? "← 15바이트가 있는데 allocate(10) 이 아직 자고 있음!" : "");
    pthread_mutex_unlock(&am);
    use_broadcast = 1; my_free(100);   // 정리: 남은 스레드 모두 풀어 줌
    pthread_join(t20, NULL); pthread_join(t10, NULL);
}

// ------------------------------------------------------------ [4] semaphore from a monitor
typedef struct { int s; pthread_mutex_t m; pthread_cond_t c; } msem_t;
static void msem_init(msem_t *x, int v) { x->s = v; pthread_mutex_init(&x->m, NULL); pthread_cond_init(&x->c, NULL); }
static void msem_wait(msem_t *x) { pthread_mutex_lock(&x->m); while (x->s <= 0) pthread_cond_wait(&x->c, &x->m); x->s--; pthread_mutex_unlock(&x->m); }
static void msem_post(msem_t *x) { pthread_mutex_lock(&x->m); x->s++; pthread_cond_signal(&x->c); pthread_mutex_unlock(&x->m); }
static msem_t lock_sem;
static long shared_counter;
static void *sem_worker(void *arg) {
    (void)arg;
    for (int i = 0; i < 200000; i++) { msem_wait(&lock_sem); shared_counter++; msem_post(&lock_sem); }
    return NULL;
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    pthread_t t[NCONS + 1];

    printf("[1] 모니터 계좌: 2 스레드 deposit(2)x1e6, 2 스레드 withdraw(1)x1e6 → 기대 2000000\n");
    for (long i = 0; i < 4; i++) pthread_create(&t[i], NULL, acct_worker, (void *)i);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("  monitor balance = %d,  락 없는 racy balance = %d\n", the_acct.balance, racy_balance);

    printf("\n[2] bounded buffer (MAX=%d), 생산자 1 / 소비자 %d, %d개 아이템\n", MAX, NCONS, ITEMS);
    pthread_create(&t[0], NULL, producer, NULL);
    for (int i = 1; i <= NCONS; i++) pthread_create(&t[i], NULL, consumer, NULL);
    for (int i = 0; i <= NCONS; i++) pthread_join(t[i], NULL);
    long expect = (long)ITEMS * (ITEMS + 1) / 2;
    printf("  합계 %ld (기대 %ld) %s\n", consumed_sum, expect, consumed_sum == expect ? "OK" : "WRONG");
    printf("  소비자: 깨어났는데 버퍼가 비어 있던 횟수   = %ld  (if 였다면 빈 버퍼에서 꺼냈을 횟수)\n", woke_but_empty);
    printf("  생산자: 깨어났는데 버퍼가 꽉 차 있던 횟수 = %ld\n", woke_but_full);

    printf("\n[3] 메모리 할당기 (Figure D.7): allocate(20), allocate(10) 대기 중 free(15)\n");
    allocator_demo(0);
    allocator_demo(1);

    printf("\n[4] 모니터로 만든 세마포어(초기값 1)를 락으로: 4 스레드 x 200000 증가\n");
    msem_init(&lock_sem, 1);
    for (int i = 0; i < 4; i++) pthread_create(&t[i], NULL, sem_worker, NULL);
    for (int i = 0; i < 4; i++) pthread_join(t[i], NULL);
    printf("  shared_counter = %ld (기대 800000)\n", shared_counter);
    return 0;
}
```

```text
$ cc -Wall -Wextra -O0 -pthread code/C0D_monitor.c -o .work/bin/C0D_monitor && .work/bin/C0D_monitor
[1] 모니터 계좌: 2 스레드 deposit(2)x1e6, 2 스레드 withdraw(1)x1e6 → 기대 2000000
  monitor balance = 2000000,  락 없는 racy balance = 1991616

[2] bounded buffer (MAX=1), 생산자 1 / 소비자 4, 200000개 아이템
  합계 20000100000 (기대 20000100000) OK
  소비자: 깨어났는데 버퍼가 비어 있던 횟수   = 1  (if 였다면 빈 버퍼에서 꺼냈을 횟수)
  생산자: 깨어났는데 버퍼가 꽉 차 있던 횟수 = 0

[3] 메모리 할당기 (Figure D.7): allocate(20), allocate(10) 대기 중 free(15)
  -- free() 가 signal() 사용 --
    free(15)
    allocate(20): 깨어났지만 available=15 → 다시 잠듦
    200ms 뒤 available=15 ← 15바이트가 있는데 allocate(10) 이 아직 자고 있음!
    allocate(20): 성공 (남은 available=95)
    allocate(10): 성공 (남은 available=85)
  -- free() 가 broadcast() 사용 --
    free(15)
    allocate(20): 깨어났지만 available=15 → 다시 잠듦
    allocate(10): 성공 (남은 available=5)
    200ms 뒤 available=5 
    allocate(20): 성공 (남은 available=85)

[4] 모니터로 만든 세마포어(초기값 1)를 락으로: 4 스레드 x 200000 증가
  shared_counter = 800000 (기대 800000)
```

해석:

- **[1]** 모니터 계좌는 정확히 2,000,000. 락 없는 쪽은 실행마다 다르게 틀린다(이번엔 1,991,616) — 모니터가 해 주는 일은 정말로 "락 잡고 놓기" 뿐이고, 그것만으로 충분하다.
- **[2]** 합계는 항상 맞다(while 덕분). 흥미로운 건 "깨어났는데 버퍼가 비어 있던 횟수" — 같은 바이너리를 따로 6번 돌렸을 때 **1, 4, 5, 2, 3, 5** 번이었다(가끔 0). 즉 Figure D.4 의 "다른 소비자가 끼어들어 가져감" 이 실제 macOS 에서 20만 개당 몇 번씩 일어난다. `if` 였다면 그만큼 빈 버퍼에서 쓰레기를 꺼냈을 것이다. 생산자 쪽은 생산자가 하나뿐이라 0.
- **[3]** `signal()` 버전: free(15) 가 먼저 기다리던 allocate(20) 을 깨웠고(macOS pthread 는 이 실행에서 FIFO 처럼 동작), 20 은 15 < 20 이라 다시 잠들었다. 200ms 뒤에도 **available=15 인데 allocate(10) 이 자고 있다** — Figure D.7 의 버그가 그대로 재현. `broadcast()` 버전은 둘 다 깨어나 10 이 성공(남은 5). 어느 스레드를 깨울지는 POSIX 가 보장하지 않으므로, 다른 OS 에서는 signal 버전이 우연히 10 을 깨울 수도 있다 — 그래서 "우연히 동작" 하는 버그가 무섭다.
- **[4]** 모니터로 만든 세마포어를 락으로 써서 800000 정확.

### 12.2 OSTEP `threads-cv` 시뮬레이터로 Mesa 경쟁과 "CV 하나" 문제 재현

Ch.30 숙제용 실제 C 코드(`main-two-cvs-if.c`, `main-one-cv-while.c`)를 쓰면 부록 D 의 두 버그를 진짜 스레드로 재현할 수 있다. `-P`/`-C` 는 코드의 지점(p0~p6, c0~c6)마다 몇 초 잘지 지정하는 문자열이다. `.tools` 를 건드리지 않으려고 `.work/bin/hw` 에 따로 빌드했다(OSTEP 원본 코드의 int/pointer 캐스트 경고는 `-w` 로 끔).

```sh
cd .tools/ostep-homework/threads-cv
cc -o ../../../.work/bin/hw/C0D_main-two-cvs-if    main-two-cvs-if.c    -w -pthread
cc -o ../../../.work/bin/hw/C0D_main-two-cvs-while main-two-cvs-while.c -w -pthread
# stdout 을 unbuffered 로 만드는 헤더를 끼워 넣어, 강제 종료돼도 트레이스가 남게 함
cc -o ../../../.work/bin/hw/C0D_main-one-cv-while  main-one-cv-while.c  -w -pthread -include ../../../.work/bin/hw/unbuf.h
```

**① if 로 쓴 Mesa 코드의 경쟁 (Figure D.4 재현)** — 생산자 1, 소비자 2, 버퍼 1칸. 생산자가 signal 직후(p5) 1초 동안 락을 쥐고 자게 해서, 그동안 다른 소비자가 락 앞에 줄 서게 만든다.

```text
prompt> ./main-two-cvs-if -p 1 -c 2 -m 1 -l 3 -P 0,0,0,0,0,1,0 -C 0,0,0,0,0,0,0:1,0,0,0,0,0,0 -v
error: tried to get an empty buffer
 NF        P0 C0 C1 
  0 [*--- ] p0
  0 [*--- ] p1
  1 [*  0 ] p4
  1 [*  0 ] p5
  1 [*  0 ]    c0
  1 [*  0 ]       c0
  1 [*  0 ]       c1
  0 [*--- ]       c4
  0 [*--- ]       c5
  0 [*--- ]       c6
  0 [*--- ]       c0
  0 [*--- ]    c1
  0 [*--- ]    c2
  0 [*--- ] p6
  0 [*--- ] p0
  0 [*--- ] p1
  1 [*  1 ] p4
  1 [*  1 ] p5
  1 [*  1 ] p6
  1 [*  1 ]    c3
  1 [*  1 ] p0
  0 [*--- ]    c4
  0 [*--- ]    c5
  0 [*--- ]    c6
  0 [*--- ]    c0
  0 [*--- ]       c1
  0 [*--- ]       c2
  0 [*--- ] p1
  1 [*  2 ] p4
  1 [*  2 ] p5
  1 [*  2 ] p6
  1 [*  2 ]    c1
  0 [*--- ]    c4
  0 [*--- ]    c5
  0 [*--- ]    c6
  0 [*--- ]    c0
  0 [*--- ]       c3
```

`error: tried to get an empty buffer` 가 맨 위에 찍힌 건 stderr 가 버퍼링되지 않아서고, 실제로는 맨 마지막 `c3` 직후에 발생했다. 끝부분을 읽으면: C1 이 c2→c3 에서 full 을 기다림 → 생산자가 2 를 채우고 signal(p5) → 그 사이 **C0 가 c1 에서 락을 먼저 잡아** c4 로 2 를 가져감 → C1 이 c3 에서 깨어나 `if` 라서 재검사 없이 do_get → 빈 버퍼. 정확히 Figure D.4 다. 같은 명령을 20번 돌리면 **if 버전은 7/20 번 실패, while 버전은 0/20** 이었다(실행 순서가 스케줄러에 달려 있으므로 횟수는 매번 다르다).

**② CV 가 하나뿐인 Java 식 모니터의 교착 (10.3 재현)** — 소비자 2, 버퍼 1칸. 마지막에 main 스레드가 소비자 수만큼 EOS(end-of-stream) 표시를 넣는다.

```text
prompt> ./main-one-cv-while -p 1 -c 2 -m 1 -l 4 -P 0,0,0,0,0,0,1 -v
 NF        P0 C0 C1 
  ... (앞부분 생략) ...
  0 [*--- ]       c2
  0 [*--- ] p0
  0 [*--- ] p1
  1 [*  3 ] p4
  1 [*  3 ] p5
  1 [*  3 ] p6
  1 [*  3 ]    c3
  0 [*--- ]    c4
  0 [*--- ]    c5
  0 [*--- ]    c6
  0 [*--- ]    c0
  0 [*--- ]       c3
  0 [*--- ]       c2
  0 [*--- ]    c1
  0 [*--- ]    c2
  1 [*EOS ] [main: added end-of-stream marker]
  1 [*EOS ]       c3
  0 [*--- ]       c4
  0 [*--- ]       c5
  0 [*--- ]       c6
  0 [*--- ]    c3
  0 [*--- ]    c2
(여기서 멈춤 — 10초 alarm 으로 강제 종료, exit code 142)
```

마지막 장면: main 이 첫 EOS 를 넣고 → C1 이 깨어나 EOS 를 가져가며(c4) signal(c5) → CV 가 하나라서 **두 번째 EOS 를 넣으려고 기다리던 main 이 아니라 C0** 를 깨움 → C0 는 버퍼가 비어 c3→c2 에서 다시 잠듦 → main 도, C0 도 영원히 잠든다. 10초 alarm 으로 죽였고(exit 142), 같은 조건을 두 개의 CV 로 만든 `main-two-cvs-while` 은 정상 종료했다. 5번 중 5번 멈췄다.

## 13. 펌웨어 엔지니어의 눈으로

- **인터럽트는 Mesa 식 힌트다.** NVMe 드라이버/펌웨어가 MSI-X 나 내부 인터럽트를 받으면 "뭔가 완료됐을 것" 이라는 힌트일 뿐, 실제로는 CQ 엔트리의 **phase bit 를 while 로 다시 확인**하며 처리한다. 인터럽트 coalescing, 공유 IRQ 라인, 이미 다른 경로(폴링)가 처리한 경우 때문에 깨어났는데 할 일이 없는 경우가 정상이다. ISR 에서 `if (status) handle();` 한 번만 보고 끝내면 엣지 트리거 유실 버그가 난다 — "while 로 재검사" 의 하드웨어 버전.
- **RTOS 에서 ISR → 태스크 통지도 Mesa.** FreeRTOS 에서 ISR 이 `xSemaphoreGiveFromISR`/task notification 을 보내면 태스크는 나중에 스케줄되고, 그 사이 HW 상태 레지스터가 또 바뀌었을 수 있다. 그래서 태스크는 깨어나서 **FIFO level / status 레지스터를 다시 읽고** 루프를 돈다. Linux 커널의 `wait_event(wq, condition)` 매크로가 내부적으로 조건을 루프로 재검사하는 것도 같은 이유다.
- **Hoare 식 "직접 전달" 은 RTOS 뮤텍스 handoff 로 남아 있다.** 많은 RTOS 뮤텍스는 해제 시 대기 중인 최고 우선순위 태스크에게 **소유권을 직접 넘긴다**(barging 금지) — 우선순위 역전/기아 방지와 실시간 보장을 위해서다. 반면 범용 OS 락(macOS `os_unfair_lock`, Linux futex 기반 mutex)은 성능을 위해 **barging 을 허용**한다. 바로 Hoare(예측 가능, 비쌈) vs Mesa(빠름, 재검사 필요) 트레이드오프의 현대판.
- **thundering herd 는 멀티큐 디바이스에서도 나온다.** 여러 워커 스레드가 한 이벤트(eventfd, 공유 completion)를 기다리다 broadcast 로 전부 깨면 그중 하나만 일을 가져간다. NVMe 의 큐-per-코어 설계, `EPOLLEXCLUSIVE`, 크기 클래스별 대기 큐(할당기 예제의 해법)는 모두 "필요한 대기자만 깨우기" 다. 가속기 런타임의 커맨드 완료 대기도 스트림/큐별로 이벤트를 나눠 같은 문제를 피한다.
- **"모니터 = 데이터 + 그 데이터를 지키는 락" 은 펌웨어 모듈 설계 원칙.** 공유 자원(예: FTL 매핑 테이블, mailbox 버퍼)을 하나의 모듈로 감싸고 모든 접근을 그 모듈의 API(락 내장)로만 하게 하는 것이 모니터의 실용적 형태다. Rust 의 `Mutex<T>`(락을 잡아야 데이터에 접근 가능)는 이를 타입 시스템으로 강제한 현대판 모니터다.

## 14. 면접 질문

### Q1. Hoare semantics 와 Mesa semantics 의 차이는? 왜 현대 시스템은 Mesa 인가?
<details>
<summary>답 보기</summary>

**Hoare**: `signal()` 즉시 깨운 스레드에게 **모니터 락과 CPU 를 넘겨** 실행시킨다. 깨어난 쪽은 조건이 참임을 **보장**받으므로 `if` 로 충분하다. 대신 signal 하는 쪽이 즉시 문맥 교환되어야 하고, 락 소유권 이전을 스케줄러와 긴밀히 엮어야 해서 구현이 복잡하고 비싸다.

**Mesa**: `signal()` 은 **힌트** — 대기자를 ready 로 옮기기만 하고 signal 한 쪽이 계속 달린다. 깨어난 쪽은 나중에 다른 스레드들과 **락을 다시 경쟁**해야 하므로, 그 사이 조건이 바뀌었을 수 있어 **while 로 재검사**해야 한다. 구현이 단순하고(그냥 큐 이동) 문맥 교환이 줄며, spurious wakeup 도 같은 while 로 처리된다. 그래서 pthread, Java, Linux 커널 모두 Mesa.

</details>

### Q2. 조건 변수 대기에서 if 대신 while 을 써야 하는 이유를 구체적 시나리오로 설명하라.
<details>
<summary>답 보기</summary>

버퍼 1칸, 소비자 C1, C2, 생산자 P. C1 이 빈 버퍼를 보고 wait. P 가 채우고 signal → C1 은 ready 가 됐지만 아직 실행 전. 그 사이 **C2 가 락을 먼저 잡아** 데이터를 가져간다. C1 이 드디어 wait 에서 반환하면 버퍼는 **비어 있다**. if 였다면 그대로 빈 버퍼에서 꺼내 버그. while 이면 재검사 후 다시 잔다.

추가 이유: POSIX 는 **spurious wakeup** 을 허용하고, broadcast 로 여럿이 깨면 그중 일부만 조건을 만족한다. while 은 Hoare 시스템에서도 해가 없다(검사 한 번 더).

</details>

### Q3. signal 대신 broadcast 가 필요한 경우와 그 비용은?
<details>
<summary>답 보기</summary>

대기자들이 **서로 다른 조건**을 기다리는데 CV 를 공유할 때. 예: 할당기에서 allocate(20) 과 allocate(10) 이 같은 CV 에서 대기 중 free(15) 가 signal 하면, 20 을 깨울 경우 아무도 진행 못 한다(10 은 가능했는데). broadcast 로 모두 깨우면 각자 재검사해서 가능한 쪽이 진행한다. Mesa 에선 broadcast 가 **항상 정확**하다.

비용: **thundering herd** — N 개를 깨워 1개만 진행, N−1 개는 락 경합 후 다시 잠듦 → 불필요한 문맥 교환과 캐시 라인 핑퐁. 완화: 조건별로 CV 분리(생산자용/소비자용, 크기 클래스별), 필요한 대기자만 골라 깨우기.

</details>

### Q4. Java 의 synchronized + wait/notify 로 생산자/소비자를 짤 때의 함정은?
<details>
<summary>답 보기</summary>

객체마다 **CV 가 하나뿐**이다. 생산자와 소비자가 같은 CV 에서 기다리므로, 소비자가 `notify()` 하면 생산자 대신 **다른 소비자**를 깨울 수 있고, 그 소비자는 버퍼가 비어 다시 잔다 → 모두가 잠드는 **교착 같은 정지**. 해결은 `notifyAll()`(thundering herd 감수) 또는 `java.util.concurrent.locks.ReentrantLock` + 여러 개의 `Condition`(`notFull`, `notEmpty`). 그리고 당연히 `while (!cond) wait();`.

</details>

### Q5. 모니터로 세마포어를 구현해 보라. 세마포어로 CV 를 만드는 건 왜 어려운가?
<details>
<summary>답 보기</summary>

```c
void sem_wait(S) { lock(m); while (S.v <= 0) cond_wait(&c, &m); S.v--; unlock(m); }
void sem_post(S) { lock(m); S.v++; cond_signal(&c); unlock(m); }
```

반대로 세마포어로 CV 를 만들면, CV 는 **"대기자가 없으면 signal 은 사라진다(기억 없음)"** 인데 세마포어는 **post 를 값으로 기억**한다. 그래서 대기자 수를 따로 세고, "락을 놓고 잠들기" 사이의 경쟁(lost wakeup)을 막고, broadcast 를 구현하는 것이 까다롭다(Birrell 의 유명한 사례 분석). 즉 CV 는 상태 없는 대기 큐 + 외부 상태 변수, 세마포어는 상태를 내장한 카운터 — 이 차이가 핵심이다.

</details>

## 15. 자가 점검 & 숙제

### 퀴즈 1. 모니터가 "자동으로" 해 주는 일은 정확히 무엇인가?
<details>
<summary>답 보기</summary>

모니터 메서드 진입 시 **모니터 락 획득**, 퇴장 시 **해제**. 그 이상은 없다. 조건 대기/통지는 프로그래머가 CV 로 명시적으로 해야 한다.

</details>

### 퀴즈 2. Figure D.6 에서 t=7 의 signal 직후 Con1 은 어느 큐에 있나? t=17 에는?
<details>
<summary>답 보기</summary>

t=7: full CV 큐에서 빠져 **ready 큐**에 있다(Mesa: 실행은 아직). t=17: 재검사에서 fullEntries=0 을 보고 다시 wait 했으므로 **full CV 큐**에 있다.

</details>

### 퀴즈 3. 할당기 예제에서 대기자 allocate(20), allocate(10), allocate(5) 가 있고 free(15) 가 broadcast 했다. 대기 순서대로 락을 잡는다면 결과는?
<details>
<summary>답 보기</summary>

20: 15 < 20 → 다시 잠듦. 10: 15 ≥ 10 → 성공, available = 5. 5: 5 ≥ 5 → 성공, available = 0. 둘이 진행, 하나는 다시 잠든다. signal 이었고 20 을 깨웠다면 **아무도** 진행하지 못한다.

</details>

### 퀴즈 4. "while 로 재검사" 를 하면 broadcast 를 남발해도 정확성 문제는 없나?
<details>
<summary>답 보기</summary>

정확성은 지켜진다(Mesa 에서 broadcast 는 항상 정확). 문제는 **성능**: 대기자 N 개를 모두 깨우는 thundering herd. 반대로 signal 을 쓰려면 "깨어난 아무나 진행할 수 있다" 가 성립해야 한다(대기자들이 같은 조건을 기다리거나, CV 를 조건별로 나눴거나).

</details>

### 숙제 (부록이라 원문 Homework 절은 없다 — 대신 해볼 것)

- **숙제 1 — if 버전 실패 확률**: 위 ①의 명령에서 `-P` 의 p5 sleep 을 0 으로 바꾸고 50번 돌려 실패 횟수 비교 → **"signal 후 락을 쥔 시간" 이 경쟁 창(window)을 키운다**는 것을 확인하는 문제.
- **숙제 2 — CV 하나로 정지 피하기**: `main-one-cv-while.c` 를 복사해 signal 을 broadcast 로 바꿔 ②를 다시 실행 → **notifyAll 이 정지를 풀지만 깨어남 횟수(=비용)가 늘어나는 것**을 확인하는 문제.
- **숙제 3 — 할당기 개선**: `C0D_monitor.c` [3] 을 크기별 CV 두 개(작은 요청용, 큰 요청용)로 바꿔 signal 만으로 올바르게 동작하게 만들기 → **broadcast 없이 "조건별 CV 분리" 로 thundering herd 를 피하는 설계**를 연습하는 문제.

## 16. 다음으로

- 기초를 다시 보려면: [Ch.30 조건 변수](2026-09-30_C30_condition_variables.md) (Mesa semantics 와 covering condition), [Ch.31 세마포어](2026-09-30_C31_semaphores.md) (세마포어 ↔ CV 구현), [Ch.32 동시성 버그](2026-09-30_C32_concurrency_bugs.md).
- 다른 부록: [부록 B 가상 머신 모니터](2026-09-30_C0B_virtual_machine_monitors.md) — 이름은 "모니터" 지만 전혀 다른 개념(하이퍼바이저)이다. [부록 I Flash SSD](2026-09-30_C0I_flash_ssd.md).
