# 📝 ISR-safe 로깅 프런트엔드 (SPSC ring + deferred formatting) — Q1~Q6

> Neros **Senior Firmware Engineer, Platform** 은 "모든 펌웨어 팀이 쓰는 공통 런타임
> (logging / telemetry / IPC / configuration)"을 소유하는 자리다. 그 런타임의 **1번 부품**이
> 로깅이다. 로깅은 (1) ISR/고우선순위 컨텍스트에서 호출되고, (2) 고정 RAM 안에서 살아야 하고,
> (3) 링크 대역폭을 먹고, (4) 망가지면 **팀 전체가 디버깅을 못 한다**. 그래서 이 토픽은
> "링 버퍼 구현해봐"가 아니라 **플랫폼 API 설계 문제**로 나온다.

---

## 1. 전체 그림

```
  [제어 태스크 / ISR]                     [저우선순위 flush 태스크]
  LOG_INFO("gyro=%d", g)                  log_drain(&rb, out, cap)
        |                                          |
   level filter (Q3)                               v
        |                                    UART / USB / 파일 / MAVLink
   log_encode (Q4)  -- 16B 바이너리 -->  [ SPSC ring (Q1,Q2) ] --> 호스트에서 포맷 (Q5)
        |                                                          ^
   spsc_write (all-or-nothing, Q2)                        fmt_id -> 문자열 테이블
```

핵심 원칙 한 줄: **로그를 남기는 쪽은 "바이트를 복사"만 하고 즉시 리턴한다.**
포맷팅·I/O·블로킹은 전부 소비자 쪽으로 미룬다.

---

## 2. ISR 에서 `printf` 가 왜 안 되는가 ⭐

면접에서 거의 반드시 나온다. 이유를 **4가지**로 말할 수 있어야 한다.

1. **시간(latency)**: `printf`/`snprintf` 는 수천~수만 사이클. ISR 예산은 보통 수 µs다.
   그 사이 다음 UART/IMU 인터럽트를 놓친다(overrun).
2. **스택**: 내부 버퍼·부동소수 변환 때문에 수백 바이트 스택을 쓴다. ISR 스택이 작은
   MCU 에서는 그대로 **스택 오버플로**.
3. **재진입성(reentrancy)**: `stdout` 은 전역 버퍼 + 락을 쓴다. 태스크가 락을 쥔 상태에서
   ISR 이 들어오면 **데드락**(또는 우선순위 역전). newlib 의 `_impure_ptr` 는 아예 ISR 안전하지 않다.
4. **블로킹 I/O**: UART 폴링 전송이면 115200bps 에서 40바이트 = **3.5ms** 동안 인터럽트 컨텍스트
   점유. 제어 루프가 죽는다.

> 대안: ISR 은 **고정 크기 바이너리 레코드를 링에 복사**만 하고 끝낸다(수십 사이클).

---

## 3. SPSC lock-free 가 성립하는 조건

| 조건 | 왜 필요한가 |
|---|---|
| 생산자 **정확히 1개**, 소비자 **정확히 1개** | 2명이 같은 인덱스를 store 하면 write-write 경합 → 즉시 깨짐 |
| `head` 는 생산자만 store, `tail` 은 소비자만 store | 서로 상대 인덱스는 **읽기만** → 락 불필요의 근거 |
| 공유 `count` 필드 **금지** | 양쪽이 갱신 → 경합. 그래서 `used = head - tail` 로 계산 |
| 인덱스 store 는 **원자적** (tearing 없음) | 32비트 MCU 의 정렬된 32비트 store 는 원자적. 8비트 MCU 의 16비트 인덱스는 아님! |
| 데이터 **먼저**, 인덱스 **나중** (+ 배리어) | 순서가 뒤집히면 소비자가 쓰레기를 읽는다 |

**"ISR ↔ main" 은 사실상 SPSC 의 교과서 사례**다. ISR 안에서 mutex 를 잡는 것은 금지
(우선순위 역전/데드락)이므로 무락이 **선택이 아니라 필수**다.

### free-running 인덱스 (이 문제의 방식)

`head`/`tail` 을 마스킹하지 않고 계속 증가시키고 **버퍼 접근 시에만** `& mask`.

```c
used  = (uint32_t)(head - tail);   // 부호없는 뺄셈 → 2^32 랩되어도 정확
full  = (used == size);            // 한 칸 희생 불필요 → 용량 = size 전부
empty = (head == tail);
buf[head & mask] = b;              // % 대신 AND 한 사이클
```

조건: `size` 는 **2의 거듭제곱**, 그리고 `size <= 2^31`.

---

## 4. acquire / release — 최소한 이것만은

```c
// producer
uint_fast32_t h = atomic_load_explicit(&q->head, memory_order_relaxed); // 내 것
uint_fast32_t t = atomic_load_explicit(&q->tail, memory_order_acquire); // 상대 것
if ((uint32_t)(h - t) == q->size) return false;
q->buf[h & q->mask] = data;                                   // 1) 데이터
atomic_store_explicit(&q->head, h + 1, memory_order_release); // 2) 공개
```

- **release store**: "이 store **이전의** 모든 쓰기가, 이 store 를 본 쪽에게 먼저 보인다."
  → 데이터 쓰기가 인덱스 공개보다 먼저 보이도록 보장.
- **acquire load**: release store 와 **짝**을 이뤄 그 순서를 관측하게 한다.
- 내 인덱스는 나만 쓰니까 **relaxed** 로 읽어도 된다 (불필요한 배리어 = 낭비).
- 비용: ARMv7-M(단일코어)에서는 사실상 **컴파일러 배리어**로 끝. ARMv8 SMP(Linux 타깃)에서는
  `stlr`/`ldar` 한 개. **둘 다 락보다 압도적으로 싸다.**

### volatile vs atomic ⭐

| | volatile | C11 atomic |
|---|---|---|
| 컴파일러 최적화(레지스터 캐싱) 억제 | ✅ | ✅ |
| **CPU 메모리 재정렬** 방지 | ❌ | ✅ (acquire/release) |
| 원자성 (read-modify-write) | ❌ (`x++` 는 3연산) | ✅ |
| 이식성 있는 동기화 의미 | ❌ (표준이 보장 안 함) | ✅ |

- **단일코어 베어메탈 ISR↔main**: `volatile` + "데이터 먼저, 인덱스 나중" 으로 전통적으로 충분.
- **멀티코어 / Linux / 캐시 일관성 있는 SMP**: `volatile` 만으로는 **틀림**. atomics 또는 DMB 필요.
- 면접 한 줄 답: **"volatile 은 컴파일러에게 하는 말이고, atomic 은 CPU 에게 하는 말이다."**

---

## 5. 오버플로 정책 3가지 — 플랫폼 팀의 진짜 설계 결정

| 정책 | 동작 | 언제 쓰나 | 비용 |
|---|---|---|---|
| **drop-new** (이 문제) | 새 로그를 버리고 `dropped++` | 로그·텔레메트리 일반. **생산자만 인덱스를 건드리므로 SPSC 불변식이 안 깨진다** | 최신 이벤트 손실 |
| **drop-oldest** (overwrite) | `tail` 을 밀어 오래된 것을 버림 | 크래시 직전 상황을 보고 싶은 **블랙박스/포스트모템 링** | ⚠️ 생산자가 `tail` 을 써야 함 → **SPSC 무락이 깨진다**. 소비자 없는(=덮어쓰기 전용) 링에서만 안전 |
| **block / backpressure** | 자리 날 때까지 대기 | 절대 잃으면 안 되는 이벤트(안전 로그, 커맨드) | ⚠️ **ISR 에서 절대 금지**. 태스크 컨텍스트에서만, 그리고 제어 루프 지터를 유발 |

> **면접 답변 팁**: "드론에서는 데이터 종류마다 정책이 다르다 — 디버그 로그는 drop-new +
> 카운터, 크래시 링은 drop-oldest, arm/disarm 같은 안전 이벤트는 별도 작은 큐에 block 또는
> 절대 우선순위" 라고 말하면 플랫폼 오너로 보인다.

**드롭은 반드시 세어서 보고한다.** 조용한 손실은 디버깅을 거짓말로 만든다.
`dropped` 카운터를 주기적으로 `"logger: dropped N records"` 로 같이 내보내는 게 정석.
(이 카운터는 **생산자만 갱신**하므로 원자화가 필요 없다 — 그것도 설계의 일부.)

---

## 6. deferred formatting — flash·bandwidth·CPU 3중 절약 ⭐

MCU 가 **포맷된 문자열**이 아니라 `fmt_id + 인자`만 보낸다.

```
전통:  "imu: gyro=-1234 dt=417 us\n"            → 27 bytes + snprintf 수천 사이클
이 방식: [id=0x1234][WARN][2][ts][-1234][417]   → 16 bytes + memcpy 수십 사이클
```

- **flash**: 포맷 문자열 수백 개(수 KB~수십 KB)가 ROM 에서 **사라진다**. 문자열 테이블은
  빌드 산출물(.elf 섹션 / 별도 manifest)로 빠져 호스트가 갖는다.
  (실물 예: Segger SystemView, `defmt`(Rust), NuttX `nxrmsg`, Zephyr `LOG_MODE_DEFERRED`)
- **bandwidth**: 드론 다운링크는 수십~수백 kbps. 문자열 로그는 링크를 그냥 먹어치운다.
- **compute**: `snprintf` 는 ISR 금지 사유 1번. 포맷팅은 **호스트 PC 가 한다**.
- **구조화**: 레벨/타임스탬프/인자가 고정 필드 → grep 이 아니라 **쿼리**가 된다.
- **대가(trade-off)**: 펌웨어 바이너리와 호스트 디코더의 **버전이 맞아야 한다**.
  → 빌드 ID/스키마 해시를 스트림 맨 앞에 한 번 실어 보내고 디코더가 검증하는 게 정석.
  이게 이 방식의 **가장 큰 운영 리스크**이며, 면접에서 먼저 말하면 점수.

### 바이트 단위 직렬화를 쓰는 이유

```c
out[0] = (uint8_t)(fmt_id & 0xFF);
out[1] = (uint8_t)(fmt_id >> 8);      // little-endian, 수동
```
`memcpy(out, &rec, sizeof rec)` 는 **구조체 패딩·정렬·엔디안**에 의존한다. MCU(LE, 4바이트 패딩)와
호스트(x86 LE) / 다른 SoC(BE) 사이에서 깨진다. **wire format 은 항상 바이트 단위로 명시**한다.

---

## 7. 각 Q 핵심 포인트 한 줄

| Q | 함수 | 한 줄 |
|---|---|---|
| **Q1** | `spsc_init` / `spsc_push` / `spsc_pop` | pow2 검증 + free-running 인덱스, **데이터 먼저 → release store 로 인덱스 공개** |
| **Q2** | `spsc_write` / `spsc_free_space` | **all-or-nothing**: 반쪽 레코드는 디코더를 영구히 망가뜨린다. 못 넣으면 `dropped++` (drop-new) |
| **Q3** | `log_should_emit` | 관문 2개 — **컴파일타임**(`LOG_LEVEL_MIN`, flash 절약) + **런타임**(모듈별 임계값, 현장 튜닝) |
| **Q4** | `log_encode` | 검사 **먼저**, 기록 **나중**. `8 + 4*nargs`, 바이트 단위 LE, 안 맞으면 0바이트 |
| **Q5** | `log_decode` | 링크에서 온 바이트 = 적대적 입력. `len == 8 + 4*nargs` 와 `nargs <= MAX` 를 **읽기 전에** 검증 |
| **Q6** | `log_drain` | 소비자 전용 flush 경로. cap 만큼만 빼고 나머지는 남긴다 → I/O 가 느려도 링은 계속 돈다 |

---

## 8. 흔한 함정 (체크리스트)

- [ ] **인덱스 먼저, 데이터 나중** 순서 뒤집기 → 소비자가 쓰레기를 읽는다. 가장 흔한 버그.
- [ ] `size` 가 2의 거듭제곱이 아닌데 `& mask` 사용 → 조용히 잘못된 인덱스.
- [ ] `head`/`tail` 을 매번 마스킹해서 저장 → `head == tail` 이 full 인지 empty 인지 **구분 불가**.
- [ ] 무락이라면서 공유 `count` 필드 유지 → 양쪽이 write → 경합. 자기모순.
- [ ] `spsc_write` 를 부분 기록 허용 → 잘린 레코드가 스트림에 섞여 **이후 전체가 오정렬**.
- [ ] `log_decode` 에서 `nargs` 범위 검사 누락 → `args[]` **배열 오버플로**(원격 입력이면 보안 이슈).
- [ ] `len < 8` 검사 없이 `in[3]` 읽기 → 버퍼 오버리드.
- [ ] `log_encode` 에서 검사 전에 일부를 써버림 → 실패 시에도 출력 버퍼가 오염됨.
- [ ] `dropped` 를 소비자가 리셋 → 생산자와 경합. **카운터 소유자도 한 명**이어야 한다.
- [ ] 8/16비트 MCU 에서 32비트 인덱스 store 가 **원자적이 아님**(tearing) → 인덱스 폭을 워드 크기에 맞춰라.
- [ ] `volatile` 만 믿고 SMP(Linux 타깃)로 포팅 → 간헐적이고 재현 안 되는 데이터 손상.
- [ ] 드롭을 세지 않음 → "로그가 이상한데?" 를 영원히 못 푼다.

---

## 9. 면접 꼬리질문 (답 요지 포함)

**Q. "이 링을 그대로 MPSC(여러 태스크가 로그를 남김)로 쓰면?"**
→ 깨진다. `head` 를 여러 컨텍스트가 store 하는 순간 무락 불변식이 무너진다. 선택지 3가지:
(1) **태스크마다 전용 링**을 두고 소비자가 여러 링을 합친다 — MCU 정석, 락 0개, 코어 캐시 친화적.
(2) `head` 예약을 **CAS 루프**(`atomic_compare_exchange`)로 바꿔 MPSC 로 승격 — 단 "예약했지만
아직 안 쓴 구간"을 소비자가 읽지 않도록 커밋용 인덱스/시퀀스가 추가로 필요(복잡도 급증).
(3) 짧은 **critical section**(인터럽트 마스킹 or spinlock) — 가장 단순하지만 ISR 지터가 생긴다.
플랫폼 라이브러리에서는 보통 **(1)**을 고른다.

**Q. "링 크기를 어떻게 정할 건가?"**
→ `size >= 최악 인입률(B/s) × 소비자 최악 지연(s) × 안전계수`. 예: 로그가 최대 4 KB/s 이고
flush 태스크가 최악 50 ms 지연 → 200 B, 2의 거듭제곱으로 올려 **512 B**, 버스트 여유로 1 KB.
그리고 **"모르면 계측한다"**: `dropped` 와 링 최대 점유율(high-water mark)을 텔레메트리로 내보내
현장 데이터로 조정한다. 이 답의 핵심은 숫자가 아니라 **"카운터를 심어 측정한다"**는 태도.

**Q. "`memory_order_seq_cst` 로 다 하면 안 되나?"**
→ 동작은 맞다(더 강한 순서니까). 비용이 문제: ARM 에서 full barrier(`dmb ish`)가 양쪽에 깔리고
x86 에서도 store 에 `mfence`/`xchg` 가 붙는다. SPSC 링에 필요한 건 **"데이터 쓰기 ↔ 인덱스 공개"
단 한 쌍의 순서**뿐이므로 acquire/release 면 충분하다. 기본값으로 seq_cst 를 쓰고, 프로파일링
후 이렇게 좁히는 것이 실무 순서 — **"먼저 맞게, 그다음 필요한 만큼만 완화"**.

**Q. "타임스탬프는 어디서 찍나? 그리고 MCU 여러 개면 시계는?"**
→ 반드시 **생산자(로그 호출 지점)**에서 찍는다. 소비자에서 찍으면 링 대기 시간이 섞여 인과관계가
깨진다. 보드 간에는 자유 증가 로컬 틱을 싣고, 주기적인 **sync 레코드**(로컬틱 ↔ 공통 시각)를
같이 내보내 호스트가 후처리로 정렬한다. 32비트 ms 카운터는 **약 49.7일에 랩**하므로 호스트 쪽
디코더가 랩을 펴주거나 64비트 µs 를 쓴다.

**Q. "전원이 갑자기 끊기면 링에 남은 로그는?"**
→ RAM 링은 날아간다. 그래서 (1) **크래시 핸들러**(HardFault/assert)에서 링을 **동기식으로**
UART/플래시에 밀어내는 경로를 따로 두고, (2) 포스트모템용 링은 리셋에도 살아남는
**no-init RAM 섹션**(+ 매직/CRC)에 두어 재부팅 후 읽어 올린다. 드론에서는 "왜 떨어졌는지"가
전부이므로 플랫폼 팀이 반드시 설계해야 하는 경로다.

**Q. "이걸 MCU 와 Linux 양쪽에서 같은 코드로 쓰려면?"**
→ 링/인코딩 코어는 **OS 의존성 0**(malloc·printf·락 없음, 저장소는 호출자가 준다)으로 유지하고,
**백엔드만 주입**한다(MCU: UART DMA / Linux: `write(2)` 또는 shared memory). 차이는 두 군데뿐 —
메모리 순서(SMP 라 atomics 필수)와 flush 컨텍스트(태스크 vs 스레드). 이 구조가 "common runtime
library" 의 핵심 설계이자 Neros 가 이 롤에서 보고 싶어하는 그림이다.

---

## 10. 빌드 / 실행

```bash
make sol  N=01_ring_logging   # 정답 — 16개 전부 PASS 확인
make prob N=01_ring_logging   # 연습 stub — FAIL 을 PASS 로 바꾸기

# Makefile 없이
cc -std=c11 -Wall -Wextra -O0 -g solutions/01_ring_logging.c -o /tmp/n01 && /tmp/n01
```
