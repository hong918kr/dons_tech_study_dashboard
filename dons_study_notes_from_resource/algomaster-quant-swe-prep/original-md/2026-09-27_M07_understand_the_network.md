# Module 7 — Understand The Network (네트워크를 이해하기)

> 출처: AlgoMonster Quant SWE Interview Prep, Module 7 (레슨 6개) · 한국어 개념 정리 노트
> 목표: 마켓 업데이트가 선에서 `MarketUpdate`가 되기까지 — 소켓, TCP vs UDP, 피드 핸들러, 패킷 드롭·backpressure, 수신 경로 레이턴시 사다리, RDMA. **네트워크 = 원거리 메시지 패싱.** 경로 위의 모든 복사·커널 횡단은 설계로 없앨 수 있는 레이턴시.

## 한눈에 보기 (5분 복습용)

| 주제 | 핵심 한 줄 | 면접 키 문장 |
|---|---|---|
| Socket | fd = 연결의 한쪽 끝. `send`/`recv`는 **커널 버퍼 경유 복사** | "`send` 반환 = 커널이 받았다는 뜻이지 상대가 받았다는 뜻 아님" |
| 스트림 | TCP `recv` 하나 ≠ 메시지 하나 | "바이트를 주지 메시지를 주지 않는다 → framing은 내 일" |
| Blocking | 빈 버퍼에서 `recv` 블록 = **voluntary context switch** | "hot receiver는 non-blocking + poll" |
| TCP | 신뢰·순서 바이트 스트림 (handshake, ACK, 재전송) | "주문은 TCP: 유실·중복·순서 뒤바뀜 = 돈 문제" |
| UDP | best-effort datagram, **메시지 경계 유지**, 최소 오버헤드 | "시세는 UDP multicast: 재전송된 호가는 이미 stale" |
| HOL blocking | 한 패킷 유실이 뒤의 모든 바이트를 재전송까지 막음 | "순서 보장의 대가" |
| Feed handler | read → frame → parse(zero-copy) → dispatch + seq gap 체크 | "할당·복사·블록·커널 0" |
| Drop | 수신 버퍼 가득 → 커널이 **조용히** 버림 → seq gap으로만 보임 | "UDP엔 flow control 없음 → backpressure = 유실" |
| 수신 레이턴시 | 7홉: DMA → 인터럽트 → 스택 → wake → mode switch → 복사 | "내가 쓴 건 한 홉, 6홉이 먼저" |
| RDMA | 원격 NIC가 **원격 메모리에 직접** 씀, 원격 CPU·커널 0 | "거래소 피드용 아님, 사내 데이터센터 배관" |

---

## 1. Socket Programming Basics

**핵심:** 소켓 = 네트워크 연결의 **한쪽 끝**. 프로그램이 쥐는 건 연결도 선도 아닌 **file descriptor(fd)** — 커널이 관리하는 무언가를 가리키는 작은 정수 핸들.

- 열 때 지정: **IP 주소**(어느 머신) + **포트**(그 머신의 어느 리스너). 시세 서비스와 주문 서비스는 같은 호스트라도 다른 포트.
```cpp
int fd = socket(AF_INET, SOCK_STREAM, 0);    // IPv4, 바이트 스트림(TCP)
connect(fd, exchange_addr, addr_len);        // 거래소 IP+port로 다이얼
char buf[2048];
ssize_t n = recv(fd, buf, sizeof(buf), 0);   // 커널에서 바이트를 복사해 IN
// n은 메시지 1개 미만일 수도, 여러 개일 수도
send(fd, order_bytes, order_len, 0);         // 커널로 바이트를 복사해 OUT
close(fd);
```

### send/recv는 커널을 통해 복사한다
- 소켓마다 커널 안에 **send buffer / receive buffer** (Module 6 bypass가 지우는 바로 그 버퍼).
- `recv`: ① 수신 버퍼에 바이트 있으면 요청 개수까지 내 buf로 **복사** ② 그만큼 수신 버퍼에서 제거 ③ 복사한 개수 반환 ④ 비었으면 **대기**.
- `send`: 내 바이트를 send buffer로 복사 후 반환 → 커널이 자기 일정대로 전송. **`send` 반환 ≠ 거래소 도착.**
- 비유: `send` = 우체통에 넣기(손을 떠나면 우체국 소유, 도착 여부 모름), `recv` = 우편함 확인(3통일 수도 0통일 수도). **모든 메시지가 커널 보관으로 복사됐다가 다시 복사돼 나옴.**

### recv 한 번 ≠ 메시지 하나
- 스트림 소켓은 **바이트**를 줄 뿐 메시지를 주지 않음. `recv` 한 번이 업데이트 반 개, 또는 2.5개를 붙여서 줄 수 있음.
- 메시지 경계 찾기 = **framing** → 내 코드의 일 (피드 핸들러 레슨).

### Blocking = voluntary context switch
- 빈 수신 버퍼에 blocking `recv` → 스레드 sleep → Module 6의 **자발적 컨텍스트 스위치** → 코어 뺏김, 캐시·TLB cold → 데이터 도착해 깨면 스위치 µs + cold restart 지불 후에야 바이트를 봄.
- **Non-blocking 소켓:** 읽을 게 없어도 즉시 반환("would block"). 한 스레드가 여러 소켓을 park 없이 확인 → busy-poll 수신 루프의 기반.
- = Module 4의 **spin vs sleep**을 네트워크 가장자리에서: 블록(코어 반납·전력 절약) vs 폴링(코어 hot·레이턴시 절약). **tick-to-trade는 poll.**

- ✅ 체크포인트: 조용한 피드에서 수신 버퍼가 빈 채로 blocking `recv` → 스레드가 데이터 올 때까지 잠듦(voluntary switch), 깰 때 스위치 + cold 재시작 → hot receiver는 non-blocking 폴링. (`recv`가 0을 반환하는 것도, 커널이 다음 패킷을 버리는 것도 아님)

---

## 2. TCP vs UDP Trade-offs

**핵심:** 소켓 생성 때 이미 선택: `SOCK_STREAM` = TCP, `SOCK_DGRAM` = UDP. 트레이딩 시스템은 **시세와 주문에 정반대 선택**을 한다.
```cpp
int md = socket(AF_INET, SOCK_DGRAM, 0);    // 시세: UDP, 연결 없음, best-effort
int oe = socket(AF_INET, SOCK_STREAM, 0);   // 주문: TCP, 신뢰·순서 스트림
```

| | TCP | UDP |
|---|---|---|
| 약속 | **모든 바이트 도착 + 보낸 순서대로** | 거의 없음 |
| 기계 | handshake, 바이트 번호, **ACK**, 타임아웃 **재전송**, 순서 재조립 | 연결 없음, ACK·재전송 없음 |
| 유실 | 재전송으로 복구 | 그냥 사라짐 (순서 뒤바뀜·드물게 중복도) |
| 경계 | **바이트 스트림** (메시지 경계 없음) | **send 1번 = datagram 1개 = recv 1번** |
| 오버헤드 | 연결 상태, ACK 트래픽, 재조립 버퍼 | 최소 |
| flow control | 있음 | 없음 |
- 신뢰성과 framing은 **다른 문제**: TCP는 바이트가 다 있고 순서가 맞다는 것만 보장, MarketUpdate 경계는 표시 안 함.

### 신뢰성의 대가 = 레이턴시 (Head-of-line blocking)
- TCP 순서 보장의 날카로운 모서리: 패킷 하나 유실 → 그 뒤 바이트를 **재전송분이 도착할 때까지 못 넘겨줌** (넘기면 순서 약속 위반) → 송신자 왕복 1회 동안 **뒤의 모든 데이터가 대기**.
- 시세엔 정반대로 작동: 재전송된 호가는 도착했을 땐 **이미 stale**, 그걸 기다리느라 더 신선한 호가들도 갇힘. → "지금 새 가격 + 하나 빠졌다는 사실"이 낫다. UDP는 유실 = 구멍 하나, 다음 datagram은 즉시 전달. **Fresh-but-lossy > complete-but-late.**

### 시세 = UDP, 주문 = TCP
- **시세 → UDP multicast:** 거래소가 업데이트를 **한 번** 보내면 네트워크가 모든 구독자에게 fan-out (수천 부 전송 불필요). 본질적으로 유실 가능 → 어떻게 아나? **`MarketUpdate::sequence`** (Module 1부터 있던 필드): 단조 증가, 구멍 = 드롭. 신선한 데이터는 계속 처리하고 빠진 건 **별도로 복구**.
- **주문 → TCP:** 주문은 stale해지는 호가가 아니라 **돈이 걸린 지시**. 매수 주문 유실·중복·두 주문 순서 뒤바뀜 = 레이턴시 문제가 아니라 **정확성 실패**. 가끔의 재전송 왕복은 감수할 가치. 주문은 나↔거래소 point-to-point라 multicast할 것도 없음.
- **프로토콜은 "무엇을 틀리면 안 되나"를 따른다:** 시세는 staleness, 주문은 loss.

- ✅ 체크포인트: "업데이트 하나도 안 놓치려고" 시세를 TCP로 → 버스트 중 한 패킷 유실 → 호가가 잠깐 얼었다 점프 → **HOL blocking**(유실 패킷 재전송까지 뒤의 모든 패킷 대기). 표준 해법: 시세는 UDP + sequence로 유실 감지. (TCP가 뒤 패킷을 버린 것도, 재연결 handshake도 아님)

---

## 3. Market Data Feed Handler Shape

**핵심:** 피드 핸들러 = 트레이딩 시스템의 **정문**. Module 1 이후 처리한 모든 업데이트가 이 모양의 코드를 통과했다.

### 패킷마다 4단계
1. **Read:** 소켓에서 datagram을 바이트 버퍼로 (`recv`)
2. **Frame:** 바이트 안에서 개별 메시지 경계 찾기 — 바이너리 피드는 보통 **고정 크기 레코드** → `sizeof(MarketUpdate)` 단위로 걷기. 가변 길이면 앞의 **length 필드**로 다음 메시지로 점프. 가진 버퍼 위의 싼 산술.
3. **Parse:** 각 메시지 → `MarketUpdate`
4. **Dispatch:** 전략에 전달

```cpp
void on_datagram(const unsigned char* data, std::size_t len) {
    for (std::size_t off = 0; off + sizeof(MarketUpdate) <= len;
         off += sizeof(MarketUpdate)) {                            // (2) FRAME
        const MarketUpdate* u =
            reinterpret_cast<const MarketUpdate*>(data + off);    // (3) PARSE: zero-copy
        if (expected_seq_ && u->sequence != expected_seq_)        // 0 = 첫 패킷
            note_gap(expected_seq_, u->sequence);                 // gap → 복구 시작
        expected_seq_ = u->sequence + 1;
        strategy_.on_update(*u);                                  // (4) DISPATCH
    }
}
```

### Parse = zero-copy (struct를 바이트에 겨누기)
- ❌ 초보: 필드마다 버퍼에서 읽어 새 `MarketUpdate`로 복사 → 패킷당 순수 낭비.
- ✅ **Module 2의 wire struct + zero-copy:** 와이어 바이트가 이미 `MarketUpdate` 순서 → 포인터를 수신 바이트에 겨누고 `reinterpret_cast`로 제자리에서 읽음 (Module 2가 말한 `reinterpret_cast`의 유일한 정당한 용도). 레코드 파싱 = **포인터 캐스트 1번, 바이트 복사 0.**
- **책임:** 와이어 레이아웃 = struct 레이아웃이 **정확히** 일치해야 함 — 필드 순서, 크기, 버퍼 정렬, **바이트 순서**.
  - 거래소 피드는 호스트와 다른 byte order를 지정할 수 있음 → 캐스트 후 정수 필드 **byte-swap**.
  - 컴파일러가 와이어에 없는 패딩을 넣지 않도록 struct **pack**.
  - C++ 객체 수명 규칙까지 지키려면 프로덕션은 **`memcpy`로 실제 `MarketUpdate`에** 복사 → -O2에서 고정 크기 복사는 같은 제자리 로드로 컴파일 → **실질적으로 zero-copy이면서 well-defined**. (Module 2 strict aliasing과 연결)

### sequence 필드가 드디어 일한다
- 다음에 기대하는 seq를 들고, 업데이트마다 비교 → 불일치 = **gap** (하나 이상 미도착) → 기록 + 복구 시작(별도 서비스에서 빠진 업데이트 재요청).
- gap 감지 = 업데이트당 **정수 비교 1번**, 파싱·디스패치와 같은 루프에.

### 모든 모듈의 교훈이 여기 착지
| 규칙 | 출처 |
|---|---|
| **할당 없음:** 시작 시 크기 잡은 버퍼 위에서 framing·parsing | Module 1, 3 |
| **복사 없음:** 파싱은 필드 복제가 아니라 포인터 캐스트 | Module 2 |
| **블록 없음:** non-blocking 폴링 `recv` → 코어 hot | Module 4, 6, 7-1 |
| **커널 없는 핸드오프:** 전략이 다른 코어면 dispatch = SPSC ring push | Module 4 |
- 한 줄 설계: **pre-fault 버퍼로의 폴링 non-blocking read + 제자리 zero-copy parse + gap 감지 정수 비교 1번 + 전략으로의 lock-free 핸드오프.** 경로 위 어디에도 할당·복사·블록·커널 없음. 새 기법이 아니라 **코스의 모든 기법을 한 루프에 조립**한 것.

- ✅ 체크포인트: parse 단계에서 필드 복사 대신 `reinterpret_cast`하는 이유와 조건 → 와이어 바이트가 이미 레이아웃이라 제자리 읽기(복사 0). 필드 순서·크기·정렬·byte order가 와이어와 정확히 일치할 때만 정확 → 아니면 byte-swap + pack. (bounds check·방어적 복사 얘기가 아님)

---

## 4. Packet Drops & Backpressure

**상황:** 장 시작, 경제지표 발표, 변동성 스파이크 → 평균보다 훨씬 빠른 **버스트**. 몇 ms 동안 처리 가능량보다 많은 패킷. 그 패킷은 어디로? — **크래시보다 조용하고 더 위험하다.**

### 버퍼는 버스트를 흡수하다가 넘친다
- 선과 코드 사이 버퍼 체인: **NIC 링** → **커널 소켓 수신 버퍼** → **앱 큐(Module 4 ring)**. 각각 생산자가 잠깐 빠를 때 초과분을 보관.
- **Backpressure:** 느린 소비자 때문에 공유 버퍼가 차오르는 것 = 생산자를 늦추라는 반발력.
- 모든 버퍼는 **유한** → 버퍼 깊이보다 긴 버스트 → 가득 → 다음 패킷은 갈 곳 없음.

### 드롭은 조용하고, 네트워크 유실처럼 보인다
- 커널 수신 버퍼 가득 + UDP datagram 도착 → **커널이 버림.** 에러·예외·반환 코드 없음 — `recv`가 닿기 전에 폐기됐으니 알릴 대상이 없음.
- 결과: 핸들러가 살아남은 다음 datagram을 읽음 → seq gap 발견 (피드 핸들러의 gap 경로). 그런데 이번엔 **네트워크가 아니라 내 머신에서** 유실 — 버퍼를 제때 못 비워서.
- **자초한 유실 = 네트워크 유실과 구별 불가.** 둘 다 seq gap, 둘 다 업데이트 소실. 과부하는 실패로 알리지 않고 **데이터를 조용히 지운 뒤 seq 번호가 사후에 알려줌.**

### UDP엔 flow control이 없다 → backpressure = 유실
| | TCP | UDP |
|---|---|---|
| 느린 수신자 | 차오르는 버퍼가 **송신자에게 늦추라고 알림**, 송신자의 `send`가 블록 | 연결도 피드백 채널도 없음 |
| backpressure의 행방 | 소스까지 전파 (예의 바르지만 느린 소비자가 송신자를 멈춤) | **갈 곳 없음 → 드롭** |
- 수천 구독자에게 multicast하는 거래소는 제일 뒤처진 소비자를 위해 늦출 수 없음. **이 피드에서 backpressure는 생산자를 늦추지 않고 패킷을 지운다.**

### 부서지기 전에 과부하를 읽어라
- 시그니처 (아무것도 throw하지 않고 **표류**함): **seq gap 카운트 상승**, 앱 큐 깊이 평소보다 깊음, OS가 보고하는 **소켓 수신 버퍼 채움률**이 가득 쪽으로. 전략이 나쁜 거래를 한 뒤에야 알면 너무 늦다.
- 처방 = 도착 속도와 배수 속도의 격차 넓히기:
  1. **더 빠른 소비자** (최선) — 피드 핸들러 레슨 전체: hot path에 할당·복사·블록·커널 없음.
  2. **더 큰 소켓 수신 버퍼** (`SO_RCVBUF`) — 더 긴 버스트를 버틸 여유. 문제를 늦출 뿐 제거하진 않음.
  3. **작업 분리** — 소켓을 비우는 것만 하는 **전용 핀닝 스레드** → 내 큰 ring으로. 수신과 처리를 분리해 느린 전략이 소켓을 멈추지 않게 (Module 4 producer-consumer를 네트워크 가장자리에).
  4. **의도적 load shedding = conflation** — 뒤처졌으면 stale 업데이트를 전부 처리하지 말고 **심볼별 최신 호가만** 유지, 대체된 건 건너뜀. 완전성 ↔ 신선도 트레이드 (시세를 UDP에 올린 것과 같은 본능).
- 🎤 정직한 프레이밍: "유한 버퍼는 충분히 큰 버스트에선 넘친다. 질문은 '절대 안 드롭하게 하려면?'이 아니라 **'드롭되면 어떻게 되나?'**" → UDP 피드의 답: **드롭 → gap 감지 → 복구.** 유실이 불가능한 척하지 말 것.

- ✅ 체크포인트: 버스트 중 전략이 잠깐 느려졌고, 네트워크는 건강했는데 사후에 seq gap → 소비자가 느린 동안 수신 버퍼가 차서 **커널이 초과 datagram을 조용히 드롭**. UDP엔 flow control이 없어 송신자를 늦출 수 없고, 유실은 네트워크 유실과 똑같이 seq gap으로만 나타남.

---

## 5. Network Stack Latency

**핵심:** "`recv`니까 ns" ❌. 네 `recv`는 긴 체인의 **마지막 한 단계**. 레이턴시 대부분은 네 코드가 돌기 **전에** 쓰였다.

### 일반 스택의 7홉 (UDP datagram 하나)
1. 패킷이 선에서 **NIC** 도착
2. NIC가 **DMA로 커널 버퍼에 복사** (Module 6의 DMA 쓰기) — **복사 #1**
3. NIC가 **인터럽트** → CPU가 하던 일을 멈추고 커널 핸들러로 점프
4. **커널 네트워크 스택**이 헤더 처리, 어느 소켓인지 확인, payload를 소켓 수신 버퍼에
5. 스레드가 `recv`에서 블록 중이었다면 → runnable 표시 → 스케줄러가 결국 전환 (**wake + context switch** + cold 재시작)
6. `recv` 실행: **mode switch**로 커널 진입 + 소켓 버퍼 → 내 버퍼 **복사 #2**
7. 이제야 코드가 바이트를 봄
- **7단계 중 내가 쓴 건 1개. 6개는 내 첫 명령 전에 일어남.**

### 모든 홉이 이미 이름 붙인 비용
| 홉 | Module 6 비용 |
|---|---|
| 2, 6 | **복사** (수신 경로만 2번) |
| 3 | 인터럽트 = 강제 커널 진입 (mode switch 친척) |
| 4 | 프로토콜 헤더 파싱 CPU 작업 |
| 5 | **컨텍스트 스위치** + cold cache/TLB 꼬리 |
| 6 | syscall mode switch |

### Jitter는 가변 홉에 산다
- 평균은 중요한 부분을 숨김 (allocation long tail과 같은 교훈, p99/p99.9를 앞세우는 이유).
- 가변 홉이 꼬리를 결정:
  - **Interrupt coalescing:** NIC가 CPU를 아끼려고 여러 패킷을 인터럽트 하나로 묶으려 **일부러 대기** (효율 ↔ 레이턴시).
  - **Wake (홉 5):** 코어가 바쁘면 스케줄 대기, 돌면 cold.
  - 소켓 버퍼가 다른 패킷들 뒤에 밀려 있을 수 있음.
- 중앙값은 빨라 보여도 **p99.9 수신은 컨텍스트 스위치 + cold cache를 통째로 먹는다.** tick-to-trade에선 수신 꼬리가 돈을 잃는 숫자.

### 사다리 접기 = kernel bypass
- 레이턴시를 더하는 홉은 **전부 커널 홉** → 그래서 bypass가 레이턴시 도구.
- bypass: NIC가 userspace ring에 DMA, 앱이 폴링 → 인터럽트·커널 스택·wake·두 번째 복사·`recv` syscall **한 번에 삭제**. **7홉 → 2홉** (카드가 메모리에 씀, 내가 읽음).
- 또 하나의 spin vs sleep:
  | | Interrupt-driven (일반 스택) | Poll-driven (bypass) |
  |---|---|---|
  | 조용할 때 | CPU 낭비 0 | 코어 계속 소모 |
  | 패킷마다 | 인터럽트 + wake 레이턴시 | 레이턴시 0 |
  | 유리할 때 | **패킷이 드물 때** | **패킷이 끊임없을 때** (바쁜 시세 피드) |

- ✅ 체크포인트: 패킷 수신이 함수 호출보다 훨씬 비싸고 최악이 중앙값보다 훨씬 큰 이유 → recv 반환 전 커널·HW 홉 체인(DMA 복사, 인터럽트, 스택, wake+schedule, mode switch, 두 번째 복사), 가변 홉(인터럽트, cold-cache wake)이 꼬리를 결정. (몇백 바이트 복사가 본질적으로 비싸다거나, 광속이라 고정이라는 답 ❌)

---

## 6. RDMA & Specialized Networking (Intro)

**핵심:** kernel bypass는 **내 머신**의 커널을 제거했다. 하지만 머신은 둘이고, **원격 측은 여전히 CPU를 써서 복사**한다.

### 남아 있는 복사
- 모든 로컬 최적화 후에도: 내 CPU가 send buffer에 씀 → 내 NIC가 선에 → 원격 NIC 수신 → **원격 CPU가 코드를 돌려** 데이터를 알아채고 카드 버퍼에서 앱이 원하는 곳으로 **복사**. bypass는 내 쪽만 고침.

### RDMA (Remote Direct Memory Access)
- 한 머신의 NIC가 **다른 머신 메모리의 특정 영역에 직접** 씀. **원격 CPU·원격 커널 관여 0.**
- 원격 앱이 메모리 영역을 **사전 등록(register) + 접근 권한 부여** → 이후 송·수신 NIC가 협력해 그 영역에 바이트를 직접 배치. 원격 CPU는 인터럽트도, 수신 코드 실행도 없이 **이미 메모리에 있는 데이터를 발견**.
- = kernel bypass를 **선 너머 양쪽 끝으로** 확장: 송신측 커널·복사 0, 수신측 커널·복사·**CPU** 0.
- 택배 비유 3단계:
  | 방식 | 비유 |
  |---|---|
  | 일반 네트워킹 | 수신자가 우편실에서 직접 찾아와 정리해야 하는 편지 |
  | Kernel bypass | 택배기사가 우편실을 건너뛰고 수신자에게 직접 건넴 |
  | RDMA | 택배기사가 들어와 수신자 서류함의 **정확한 서랍에 직접 넣음** — 수신자는 책상에서 고개도 안 듦 |

### 패브릭
- 일반 인터넷 연결에서 켜는 게 아님 → 양쪽 전용 HW + 지원 네트워크.
- **InfiniBand:** Ethernet과 별개의 목적 설계 저지연 네트워킹 표준 (HPC·데이터센터).
- **RoCE** (RDMA over Converged Ethernet): 같은 RDMA 연산을 **일반 Ethernet HW 위에서** → 별도 InfiniBand 패브릭 없이 RDMA.
- 둘 다 **RDMA 지원 NIC** 필요. 면접엔 이름 + 한 줄 차이면 충분 (queue pair 설정까지 기대 안 함).

### 어디에 맞고 어디에 안 맞나
- 비용: 전용 HW + 맞는 네트워크, 프로그래밍 복잡 (명시적 메모리 등록·접근 제어 — 원격 카드가 내 메모리에 쓰게 하는 게 보안 구멍이 되지 않도록).
- 맞는 곳: **사내 데이터센터 안** 머신 간 최소 CPU·레이턴시 데이터 이동 — primary↔backup **상태 복제**, 분산 스토리지, 서버 간 대용량 데이터셋 전달.
- ❌ **거래소 공개 시세 수신용 아님:** 그 피드는 표준 네트워크 위 UDP multicast. 거래소가 내 메모리에 직접 쓰지 않음. 트레이딩 맥락에서 RDMA = **양쪽 다 내가 통제하는 사내 배관.**

- ✅ 체크포인트: "거래소 시세를 RDMA로 최저 레이턴시 수신" 발언이 틀린 이유 → RDMA는 양쪽 RDMA HW가 협력해 원격 등록 메모리에 직접 쓰는 것(원격 CPU·복사 제거), 거래소 시세는 표준망 UDP multicast → RDMA는 사내 데이터센터용. (RDMA가 "빠른 UDP"이거나 TCP라서 틀린 게 아님)
- 🎤 "RDMA가 kernel bypass에 없는 무엇을 제거하나?" → bypass는 내 머신만 고침. 원격은 여전히 CPU로 복사. RDMA는 바이트를 원격 메모리에 바로 놓아 **원격 CPU와 복사**를 제거.

---

## 모듈 7 요약 — 패킷의 여정
소켓과 **커널 경유 복사 2번** → 시세 UDP / 주문 TCP (**HOL blocking**이 이 분할을 강제) → 바이트 스트림을 `MarketUpdate`로 **framing** + **sequence로 gap 감지**하는 피드 핸들러 → 유한 버퍼가 차면 **조용한 드롭** → 수신 경로 **7홉 레이턴시 사다리** → **RDMA**로 복사와 원격 CPU까지 제거. 관통하는 한 가지: **네트워크는 원거리 메시지 패싱이고, 경로 위 모든 복사·커널 횡단은 때로 설계로 없앨 수 있는 레이턴시다.**

## 용어 사전

| 용어 | 한 줄 정의 |
|---|---|
| Socket / fd | 네트워크 연결의 한쪽 끝 / 커널 객체를 가리키는 정수 핸들 |
| IP / port | 머신 지정 / 그 머신의 리스너 지정 |
| Send / receive buffer | 소켓별 커널 스테이징 영역 |
| Framing | 바이트 스트림에서 메시지 경계를 찾는 작업 |
| Non-blocking socket | 읽을 게 없으면 즉시 "would block" 반환 |
| TCP | 신뢰·순서 바이트 스트림 (handshake, ACK, 재전송, flow control) |
| UDP / datagram | best-effort 자기완결 패킷, 메시지 경계 유지 |
| Head-of-line blocking | 순서 보장 때문에 유실 하나가 뒤 전체를 막음 |
| Multicast | 한 번 전송 → 네트워크가 모든 구독자에게 fan-out |
| Sequence gap | 시퀀스 번호 점프 = 드롭 신호 |
| Feed handler | read → frame → parse → dispatch 파이프라인 |
| Zero-copy parse | 수신 바이트에 layout 일치 struct를 겨눠 제자리 읽기 |
| Byte order / pack | 엔디언 일치 / 컴파일러 패딩 제거 |
| Backpressure | 느린 소비자로 버퍼가 차오르며 생기는 반발 |
| Flow control | 수신자가 송신자를 늦추게 하는 TCP 메커니즘 |
| Conflation | 뒤처지면 심볼별 최신 호가만 유지하는 load shedding |
| Interrupt coalescing | NIC가 여러 패킷을 인터럽트 하나로 묶으려 대기 |
| Interrupt- vs poll-driven | 도착 시 알림 받기 vs 계속 확인하기 |
| RDMA | NIC가 원격 등록 메모리에 직접 쓰기, 원격 CPU 무관여 |
| InfiniBand / RoCE | 전용 저지연 패브릭 / Ethernet 위 RDMA |
