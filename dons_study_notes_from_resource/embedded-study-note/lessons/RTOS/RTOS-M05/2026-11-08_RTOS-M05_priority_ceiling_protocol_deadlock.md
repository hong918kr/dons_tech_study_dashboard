---
id: RTOS-M05
time: 90분
updated: 2026-11-08
---
# RTOS-M05 우선순위 천장 프로토콜(PCP/IPCP)과 교착 방지

## 학습 목표

- **즉시 천장 프로토콜(Immediate Priority Ceiling Protocol, IPCP — "highest locker")** 뮤텍스를 미니 커널에 구현하고, 천장(ceiling) 계산 · 유효 우선순위(effective priority) · 설정 오류(EINVAL) 규칙을 코드로 말할 수 있다.
- 두 뮤텍스를 **반대 순서**로 잡는 교착(deadlock)이 일반 세마포어와 **우선순위 상속(PI)** 에서는 그대로 일어나고(“모든 태스크가 막힘” 으로 결정적 탐지), IPCP 에서는 아예 생기지 않음을 타임라인으로 보인다.
- 최악 블로킹이 **임계 구역(critical section) 1개** 로 제한되는 이유를 증명 수준으로 설명하고, 태스크 세트의 블로킹 상한 B_i 를 계산해 시뮬레이션과 맞춰 본다 (PI 는 연쇄 블로킹으로 임계 구역 2개).
- 원래의 PCP(OPCP, Sha·Rajkumar·Lehoczky 1990)와 IPCP 의 차이, 그리고 POSIX `PTHREAD_PRIO_PROTECT`(= IPCP) / `PTHREAD_PRIO_INHERIT`(= PI) 를 실제 Linux 에서 관찰한 결과로 설명한다.
- IPCP 의 전제(단일 코어, 임계 구역 안에서 블로킹 금지, 같은 우선순위 time slicing 없음)가 깨지면 무엇이 무너지는지 안다.

## 1. 문제

RTOS-M03 의 Mars Pathfinder 이야기를 마치면 면접관은 거의 항상 **"상속(inheritance) 말고 다른 방법은? 둘 중 뭘 쓰겠어요?"** 를 묻는다. 정답의 절반은 **우선순위 천장(priority ceiling)** 이다: 자원마다 "그 자원을 쓰는 태스크 중 가장 높은 우선순위" 를 미리 계산해 두고, 자원을 잡는 순간 그 우선순위로 올라간다. 그러면 역전이 임계 구역 1개로 묶일 뿐 아니라 **교착까지 사라진다**. 자동차 ECU 의 OSEK/AUTOSAR OS 가 자원(Resource)을 이 방식으로만 제공하는 이유이고, POSIX 에도 `PTHREAD_PRIO_PROTECT` 로 들어 있다.

장면: STM32 모터 드라이버 보드에서 FreeRTOS 태스크 셋이 돈다. **모터 제어 태스크(H)** 는 SPI 엔코더를 읽고 게인 파라미터 블록을 읽는다. **텔레메트리 태스크(M)** 는 같은 SPI 버스에 달린 CAN 컨트롤러로 상태를 보낸다. **로거 태스크(L)** 는 파라미터 블록을 플래시에 쓰려고 복사한다. 누군가 `spi_mutex` 와 `param_mutex` 를 서로 다른 순서로 잡는 코드를 넣는 순간, 상속 뮤텍스로는 보드가 가끔 멈춘다 — 워치독 리셋.

호스트에는 RTOS 가 없으니 **OS 스레드 하나에서 tick 단위로 도는 결정적 미니 커널** 을 쓴다 (RTOS-B01~B07 은 아직 없으므로 이 레슨 안에 커널이 다 들어 있다. M03/M04 와 같은 설계·같은 숫자의 시나리오).

| 모델 요소 | 의미 | 실제 RTOS 에서 |
|---|---|---|
| 태스크 = op 스크립트 `RUN(n)` / `LOCK(m)` / `UNLOCK(m)` / `END` | `RUN` 1 단위 = 1 tick 작업. 커널 호출 `LOCK`·`UNLOCK`·`END` 는 0 tick (막히면 그 tick 에 다른 태스크가 돈다) | 태스크 함수 본문 |
| 매 tick: READY 중 **유효 우선순위 최고** 태스크가 op 실행 | 숫자가 클수록 높다 (FreeRTOS 와 같은 방향) | 선점형 우선순위 스케줄러 |
| 같은 우선순위 | 돌던 태스크가 계속 (time slicing 없음 = `SCHED_FIFO`), 나머지는 READY 된 순서 | FreeRTOS `configUSE_TIME_SLICING 0` |
| `UNLOCK` 에 대기자가 있으면 소유권을 바로 넘김(handoff) | 대기열: 우선순위 높은 순 → 먼저 막힌 순 | FreeRTOS 이벤트 리스트 (우선순위 정렬) |
| 프로토콜 `PROTO_NONE` / `PROTO_PI` / `PROTO_IPCP` | 세마포어를 락으로 / 상속(연쇄 포함, M04 규칙) / **즉시 천장 (이 레슨의 TODO)** | 아래 5. 풀이 대응표 |

타임라인 문자는 시리즈 공통이다: `R` 실행, `B` 뮤텍스 때문에 막힘, `r` 준비됐지만 못 돎, `.` 출시 전/끝남. 출력의 tick 번호는 일의 자리만 찍는다 (`9 0 1` = 9, 10, 11).

**정규 시나리오** (RTOS-M03 이 정의, M04/M05 공용): H=3, M=2, L=1. L 은 t0 출시, t1 에 R 을 잡고 R 안에서 4 tick 일. H 는 t2 출시, 첫 op 가 `LOCK(R)`. M 은 t3 출시, R 없이 C_M tick CPU 일. 아래는 이 레슨의 `./solution -v` 가 실제로 찍은 타임라인을 그대로 그린 것이다.

```svg
<svg viewBox="0 0 740 334" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">정규 시나리오 C_M=4: 실제 ./solution -v 타임라인 (H=3, M=2, L=1, 공유 자원 R)</text>
  <text x="130" y="50" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="50" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="50" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="50" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="50" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="50" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="50" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="50" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="450" y="50" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="490" y="50" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="530" y="50" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="570" y="50" fill="currentColor" text-anchor="middle" font-size="12">11</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">plain</text>
  <text x="82" y="70" fill="currentColor" font-family="monospace">H</text><rect x="191" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="210" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="231" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="250" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="271" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="311" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="330" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="351" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="370" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="391" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="410" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="431" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="450" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="471" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="511" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="90" fill="currentColor" font-family="monospace">M</text><rect x="231" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="110" fill="currentColor" font-family="monospace">L</text><rect x="111" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="431" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="490" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="511" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="530" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="551" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="600" y="90" fill="currentColor" font-size="13">H 대기 7 (B)</text>
  <text x="130" y="138" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="138" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="138" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="138" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="138" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="138" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="138" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="138" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="450" y="138" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="490" y="138" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="530" y="138" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="570" y="138" fill="currentColor" text-anchor="middle" font-size="12">11</text>
  <text x="10" y="138" fill="currentColor" font-weight="bold">PI</text>
  <text x="82" y="158" fill="currentColor" font-family="monospace">H</text><rect x="191" y="145" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="210" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="231" y="145" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="250" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="271" y="145" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="311" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="178" fill="currentColor" font-family="monospace">M</text><rect x="231" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="431" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="511" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="198" fill="currentColor" font-family="monospace">L</text><rect x="111" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="410" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="431" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="450" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="490" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="511" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="530" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="551" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="600" y="178" fill="currentColor" font-size="13">H 대기 3 (B)</text>
  <text x="130" y="226" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="226" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="226" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="226" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="226" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="226" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="226" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="226" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="450" y="226" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="490" y="226" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="530" y="226" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="570" y="226" fill="currentColor" text-anchor="middle" font-size="12">11</text>
  <text x="10" y="226" fill="currentColor" font-weight="bold">IPCP</text>
  <text x="82" y="246" fill="currentColor" font-family="monospace">H</text><rect x="191" y="233" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="246" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="233" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="246" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="233" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="246" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="233" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="233" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="266" fill="currentColor" font-family="monospace">M</text><rect x="231" y="253" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="266" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="253" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="266" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="253" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="266" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="253" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="266" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="253" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="431" y="253" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="253" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="511" y="253" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="286" fill="currentColor" font-family="monospace">L</text><rect x="111" y="273" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="273" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="273" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="273" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="273" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="273" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="286" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="273" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="286" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="273" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="410" y="286" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="431" y="273" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="450" y="286" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="273" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="490" y="286" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="511" y="273" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="530" y="286" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="551" y="273" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="600" y="266" fill="currentColor" font-size="13">H 대기 3 (r)</text>
  <rect x="10" y="304" width="16" height="14" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="32" y="316" fill="currentColor" font-size="13">R 실행</text>
  <rect x="180" y="304" width="16" height="14" class="f-warn" stroke="currentColor" stroke-width="0.6"/>
  <text x="202" y="316" fill="currentColor" font-size="13">B 뮤텍스에 막힘</text>
  <rect x="350" y="304" width="16" height="14" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
  <text x="372" y="316" fill="currentColor" font-size="13">r 준비됐지만 못 돎</text>
  <text x="520" y="316" fill="currentColor" font-size="13">. 출시 전 / 끝남</text>
</svg>
```

숫자는 RTOS-M03 과 같다 (커널 호출 0 tick, plain C_M=4 의 H 행 `..BBBBBBBRR.`). H 가 올 때 L 은 이미 R 안에서 1 tick 일했으므로 남은 임계 구역은 3 이다. 일반 세마포어에서 H 는 M 이 도는 동안 계속 막힌다(7 = 3 + C_M). PI 는 L 이 H 의 우선순위를 빌려 M 을 앞질러 3 으로 끝난다. IPCP 도 3 이지만 **H 의 칸이 `B` 가 아니라 `r`** 이다: L 이 R 을 잡는 순간(t1) 이미 천장 3 으로 올라가 있어서, H 는 출시되자마자 막힌 게 아니라 **시작조차 못 하고 기다렸다**. H 가 `LOCK(R)` 을 실행한 t5 에는 R 이 비어 있다. 이 차이가 교착을 없애는 열쇠다.

### Part A — IPCP 뮤텍스

- `void compute_ceilings(kernel_t *k)` — 모든 태스크 스크립트의 `LOCK(m)` 을 훑어 `ceiling(m) = max{ base(t) : t 가 m 을 잠근다 }`. (실제로는 설계 단계에서 표로 계산하는 정적 분석이다.)
- `int ipcp_eff_prio(const kernel_t *k, int t)` — `max(base(t), 지금 t 가 보유한 모든 뮤텍스의 천장)`. 잡는 즉시 올라가고, 놓으면 "아직 보유 중인 것" 기준으로 다시 계산된다.
- `int ipcp_check_lock(const kernel_t *k, int t, int m)` — `base(t) > ceiling(m)` 이면 설정 오류 `K_EINVAL` (POSIX 와 같은 규칙), 아니면 `K_OK`.
- 확인: 정규 시나리오에서 C_M ∈ {0,2,4,8,16} 모두 H 대기 = 3, H 의 `B` 0개, **막힌 LOCK 0회**.

### Part B — 반대 순서 두 뮤텍스: 교착 탐지와 회피

L: `RUN1, LOCK A, RUN2, LOCK B, RUN1, UNLOCK B, UNLOCK A, RUN1`. H(t2 출시): `LOCK B, RUN1, LOCK A, RUN1, UNLOCK A, UNLOCK B, RUN1`.

- `bool sim_deadlocked(const kernel_t *k)` — "READY 도, 앞으로 출시될 태스크도 없는데 막힌 태스크가 있다" 면 true. `sim_tick` 이 아무도 못 돈 tick 에 이것을 불러 `SIM_DEADLOCK` 으로 멈춘다 (starter 에서는 64 tick 까지 돌다 `SIM_TIMEOUT` — 멈추지 않는다).
- 확인: plain · PI 모두 **t4 에 교착**(PI 는 L 이 3 으로 올라가 있는데도), IPCP 는 끝까지 완료 + 막힌 LOCK 0회.

### Part C — 펌웨어 태스크 세트의 블로킹 상한

H(모터 제어, 3): `LOCK SPI, RUN1, UNLOCK SPI, LOCK PARAM, RUN1, UNLOCK PARAM, RUN1`, M(텔레메트리, 2, t1 출시): `RUN1, LOCK SPI, RUN3, UNLOCK SPI, RUN1`, L(로거, 1, t0 출시): `LOCK PARAM, RUN4, UNLOCK PARAM, RUN1`.

- `int ipcp_blocking_bound(const int *prio, const use_t *u, int nu, int i)` — 사용 표 `{태스크, 자원, 그 자원을 쥐는 최장 tick}` 에서 `B_i = max{ cs : prio(j) < prio(i), ceiling(자원) ≥ prio(i) }`.
- 확인: B_H = 4, B_M = 4 (M 은 PARAM 을 안 쓰는데도!), B_L = 0. H 출시 시각을 t0~t12 로 훑으면 IPCP 최대 대기 = 3 ≤ B_H, PI 최대 = **5 = 2 + 3 > B_H** (두 임계 구역에 연달아 막힘. PI 단순 상한은 7).

## 2. Problem (English)

> **Immediate Priority Ceiling mutex in a deterministic mini kernel.**
>
> A single-threaded, tick-driven kernel runs scripted tasks (`RUN(n)` = n ticks of work; kernel calls `LOCK(m)`/`UNLOCK(m)`/`END` take zero time). Each tick the READY task with the highest effective priority runs; equal priorities do not preempt each other. `UNLOCK` hands ownership to the highest-priority (then FIFO) waiter. PROTO_NONE and PROTO_PI (transitive inheritance) are given. Implement PROTO_IPCP:
>
> 1. `void compute_ceilings(kernel_t *k)` — ceiling(m) = max base priority of all tasks whose script locks m.
> 2. `int ipcp_eff_prio(const kernel_t *k, int t)` — max(base, ceilings of all mutexes t currently owns).
> 3. `int ipcp_check_lock(const kernel_t *k, int t, int m)` — `K_EINVAL` if base(t) > ceiling(m) (POSIX `PTHREAD_PRIO_PROTECT` rule), else `K_OK`.
> 4. `bool sim_deadlocked(const kernel_t *k)` — no READY task, no task still to be released, and at least one BLOCKED.
> 5. `int ipcp_blocking_bound(const int *prio, const use_t *u, int nu, int i)` — worst-case blocking of task i under IPCP.
>
> Example (H=3, M=2, L=1, L locks R at t1 for 4 ticks, H requests R at t2, M runs C_M=4 ticks from t3): H waits 7 ticks with a plain semaphore (3 + C_M), 3 with PI (state B), 3 with IPCP (state r, never blocked). With L taking A→B and H taking B→A, PROTO_NONE and PROTO_PI deadlock at tick 4; PROTO_IPCP completes with zero blocking lock attempts.
>
> Follow-ups: why can IPCP never deadlock, and which assumptions does that proof need? What is the worst-case blocking under PI vs IPCP? How does the original PCP differ? What do POSIX `PTHREAD_PRIO_PROTECT` and OSEK `GetResource` do?

## 3. 시작 코드

<!-- code: starter.c -->

```bash
export SDKROOT=/Library/Developer/CommandLineTools/SDKs/MacOSX26.4.sdk   # 27.2 SDK 링크 오류 회피
cc -std=c11 -Wall -Wextra -Werror -O2 starter.c -o starter && ./starter      # 처음엔 19/43 passed, exit 1
./starter -v                                                                  # 타임라인도 같이 출력
```

처음 통과하는 19개는 주어진 커널만으로 되는 "재현" 테스트다: plain 의 H 대기 3 + C_M, PI 의 3, ABBA 교착 직전까지의 타임라인 등. 스레드·시그널을 쓰지 않으니 `-pthread` 는 필요 없고 실행 결과는 매번 똑같다.

관찰용 데모 (하네스와 별개):

```bash
# POSIX PTHREAD_PRIO_PROTECT 실측 — Linux(glibc) 는 root 필요 (SCHED_FIFO)
gcc -std=c11 -Wall -Wextra -O2 -pthread demo_posix_ceiling.c -o dpc && sudo ./dpc     # Lima VM elinux
cc  -std=c11 -Wall -Wextra -O2 -pthread demo_posix_ceiling.c -o dpc && ./dpc          # macOS 비교
```

## 4. 힌트

<details>
<summary>힌트 1 — 천장은 "누가 이 자원을 쓰나" 표에서 나온다</summary>

`ceiling(m)` 은 런타임 값이 아니다. 스크립트(= 소스 코드)에서 `LOCK(m)` 을 하는 태스크들의 **기본** 우선순위 최댓값이다. 0 으로 초기화한 뒤 `ops[pc].op == OP_LOCK` 인 곳마다 `max` 를 갱신하면 된다. `OP_END` 를 만나면 그 태스크 스크립트는 끝이다.

</details>

<details>
<summary>힌트 2 — 유효 우선순위를 "저장/복원" 하지 말고 매번 계산한다</summary>

"잡을 때 이전 우선순위를 저장했다가 놓을 때 복원" 은 해제 순서가 잡은 순서의 역(LIFO)일 때만 맞다. 커널이 `recompute()` 에서 매번 부르므로, `ipcp_eff_prio` 는 `m.owner == t` 인 뮤텍스들의 천장과 `base` 중 최댓값만 돌려주면 된다 (연습 3).

</details>

<details>
<summary>힌트 3 — 교착 판정: "아무도 못 돎" 만으로는 부족하다</summary>

아무도 안 도는 tick 은 단순히 idle 일 수도 있다. 아직 출시되지 않은 태스크(`T_UNREL`)가 있으면 미래에 상황이 바뀔 수 있으니 교착이 아니다. READY 가 하나라도 있으면 당연히 아니다. 남은 태스크가 전부 `T_BLOCKED` 또는 `T_DONE` 이고 `T_BLOCKED` 가 1개 이상일 때만 true.

</details>

<details>
<summary>힌트 4 — 블로킹 상한: "나보다 낮고, 천장이 나 이상인 자원" 의 임계 구역</summary>

i 를 막을 수 있는 것은 **더 낮은 태스크 j** 가 쥔 자원 중 **천장 ≥ prio(i)** 인 것뿐이다 (천장이 낮은 자원을 쥔 j 는 i 가 출시되면 바로 선점된다). IPCP 는 그중 **하나만** 겪으므로 합이 아니라 최댓값이다. `ipcp_ceiling()` 은 주어져 있다.

</details>

## 5. 풀이

### 0. 모델 ↔ 실제 API

| 이 레슨 | FreeRTOS (V11.3.2) | Zephyr | POSIX | OSEK / AUTOSAR OS |
|---|---|---|---|---|
| `PROTO_NONE` (세마포어를 락으로) | `xSemaphoreCreateBinary` + Take/Give | `k_sem` | `sem_t`, `PTHREAD_PRIO_NONE` | — |
| `PROTO_PI` | `xSemaphoreCreateMutex` — `xTaskPriorityInherit()` (tasks.c:6745) | `k_mutex` (상속. 연쇄 상속은 v4.5.0 부터 — RTOS-M04) | `PTHREAD_PRIO_INHERIT` | — |
| `PROTO_IPCP` | **없음** (tasks.c·queue.c·include 에 "ceiling" 이 한 번도 안 나온다) → `vTaskPrioritySet` 으로 흉내 | 없음 (문서 기준. `CONFIG_PRIORITY_CEILING` 은 **상속의 상한**이지 천장 프로토콜이 아니다 — 문서 기준, 미검증) | `PTHREAD_PRIO_PROTECT` + `pthread_mutexattr_setprioceiling` | `GetResource` / `ReleaseResource` (OSEK Priority Ceiling Protocol) |
| `compute_ceilings` | 설계 문서의 표 | — | 사람이 숫자로 지정 | OIL/ARXML 의 TASK ↔ RESOURCE 참조로 생성 도구가 계산 |
| 천장 = 모든 태스크 위 | `vTaskSuspendAll()` (스케줄러 잠금), `taskENTER_CRITICAL()` (BASEPRI = `configMAX_SYSCALL_INTERRUPT_PRIORITY`) | `k_sched_lock()`, `irq_lock()` | — | `RES_SCHEDULER`, ISR 이 쓰는 자원은 인터럽트 레벨까지 |
| `sim_deadlocked` | 없음 → 워치독 + 트레이스(RTOS-A06) | — | `PTHREAD_MUTEX_ERRORCHECK` 는 자기 교착만 | IPCP 라 생기지 않음 |

FreeRTOS 로 IPCP 를 흉내 낼 때의 모양 (`INCLUDE_vTaskPrioritySet` 를 1 로 — FreeRTOS.h:218 의 기본은 0):

```c
UBaseType_t prev = uxTaskPriorityGet(NULL);
vTaskPrioritySet(NULL, SPI_CEILING);      /* 잡기 "전에" 천장으로: 이 순간부터 다른 SPI 사용자는 못 돈다 */
xSemaphoreTake(spi_mutex, 0);             /* 단일 코어 + 블로킹 없음이면 항상 즉시 성공 (assert 용도) */
spi_transfer(...);                        /* 이 안에서 vTaskDelay / 큐 대기 금지 */
xSemaphoreGive(spi_mutex);
vTaskPrioritySet(NULL, prev);             /* 중첩이면 prev 복원이 아니라 재계산이 맞다 (연습 3) */
```

주의: `vTaskPrioritySet` 은 `configUSE_MUTEXES == 1` 일 때 **기본 우선순위(`uxBasePriority`)** 를 바꾼다 (tasks.c:2949–2964). 상속이 걸려 있으면 실행 우선순위는 그대로 두고 기본값만 바꾼다.

### Part A — 천장, 유효 우선순위, 그리고 "LOCK 이 막히지 않는" 이유

```c
static int ipcp_eff_prio(const kernel_t *k, int t) {
    int e = k->t[t].base;
    for (int m = 0; m < k->nm; m++)
        if (k->m[m].owner == t && k->m[m].ceiling > e) e = k->m[m].ceiling;
    return e;
}
```

`./solution -v` 의 실제 출력 (IPCP, C_M=4):

```text
canonical C_M=4  [IPCP]
tick : 0 1 2 3 4 5 6 7 8 9 0 1
H    : . . r r r R R . . . . .
M    : . . . r r r r R R R R .
L    : R R R R R r r r r r r R
-> DONE after 12 ticks, blocking locks=0
```

| tick | 일어난 일 | L 유효 우선순위 |
|---|---|---|
| 0 | L `RUN1` | 1 |
| 1 | L `LOCK R` (0 tick) → 비어 있음, 획득, **즉시 천장 3** → 같은 tick 에 R 안 작업 1 | 3 |
| 2 | H 출시(3). 실행 중인 L 도 3 → 같은 우선순위는 선점 안 함 → H 는 `r`. L 작업 2 | 3 |
| 3–4 | M 출시(2) → `r`. L 작업 3, 4 | 3 |
| 5 | L `UNLOCK R` (0 tick) → 1 로 복귀 → H 선점, H `LOCK R` — **비어 있다** → H 작업 | 1 |
| 5–6 | H 완료, 7–10 M, 11 L 의 남은 `RUN1` | 1 |

**왜 IPCP 의 LOCK 은 절대 막히지 않나** (단일 코어, 아래 세 전제에서):

1. U 가 m 을 쥐고 있는 동안 `eff(U) ≥ ceiling(m) ≥ base(V)` — m 을 쓰는 모든 태스크 V 에 대해.
2. U 가 m 을 잡은 순간 U 는 실행 중이었다 → 그때 READY 였던 V 는 `eff ≤ eff(U)` 였고, 잡은 뒤에는 U 가 천장 이상이라 V 는 U 를 선점할 수 없다 (같은 우선순위는 선점 안 함). 나중에 출시되는 V 도 마찬가지.
3. U 를 선점할 수 있는 것은 `base > ceiling(m)` 인 태스크뿐이고, 그런 태스크는 정의상 m 을 쓰지 않는다.

그러므로 V 가 `LOCK(m)` 을 실행하는 시점에는 m 이 비어 있다. 막힘이 없으니 **대기열도, 상속도, 교착도 없다**. 전제: ① 임계 구역 안에서 스스로 대기하지 않는다 (`vTaskDelay`, 큐 수신 블로킹, 이벤트 대기 금지 — 하면 U 보다 낮은 태스크가 돌 수 있다), ② 같은 우선순위 time slicing 이 없다 (연습 4), ③ 단일 코어 (다른 코어는 우선순위와 상관없이 돈다 — RTOS-A08).

C_M 을 바꿔 본 실제 표 (`./solution -v`):

```text
C_M | H wait plain (B) | H wait PI (B) | H wait IPCP (r) | H finish tick plain/PI/IPCP
  0 |                3 |             3 |               3 | 6 / 6 / 6
  2 |                5 |             3 |               3 | 8 / 6 / 6
  4 |                7 |             3 |               3 | 10 / 6 / 6
  8 |               11 |             3 |               3 | 14 / 6 / 6
 16 |               19 |             3 |               3 | 22 / 6 / 6
```

```svg
<svg viewBox="0 0 740 270" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">H 의 대기 tick vs M 의 실행 길이 C_M (./solution -v 의 표)</text>
  <line x1="70" y1="220" x2="560" y2="220" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="220" stroke="currentColor"/>
  <line x1="66" y1="220" x2="70" y2="220" stroke="currentColor"/><text x="62" y="224" fill="currentColor" text-anchor="end" font-size="12">0</text><line x1="66" y1="182" x2="70" y2="182" stroke="currentColor"/><text x="62" y="186" fill="currentColor" text-anchor="end" font-size="12">4</text><line x1="66" y1="144" x2="70" y2="144" stroke="currentColor"/><text x="62" y="148" fill="currentColor" text-anchor="end" font-size="12">8</text><line x1="66" y1="106" x2="70" y2="106" stroke="currentColor"/><text x="62" y="110" fill="currentColor" text-anchor="end" font-size="12">12</text><line x1="66" y1="68" x2="70" y2="68" stroke="currentColor"/><text x="62" y="72" fill="currentColor" text-anchor="end" font-size="12">16</text><line x1="66" y1="30" x2="70" y2="30" stroke="currentColor"/><text x="62" y="34" fill="currentColor" text-anchor="end" font-size="12">20</text>
  <text x="70" y="236" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="131" y="236" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="192" y="236" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="315" y="236" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="560" y="236" fill="currentColor" text-anchor="middle" font-size="12">16</text>
  <text x="315" y="256" fill="currentColor" text-anchor="middle" font-size="12">C_M (tick)</text><text x="14" y="125" fill="currentColor" font-size="12">대기</text>
  <polyline points="70,192 131,172 192,154 315,116 560,40" fill="none" stroke="currentColor" stroke-width="2"/><circle cx="70" cy="192" r="5" class="f-warn" stroke="currentColor"/><circle cx="131" cy="172" r="5" class="f-warn" stroke="currentColor"/><circle cx="192" cy="154" r="5" class="f-warn" stroke="currentColor"/><circle cx="315" cy="116" r="5" class="f-warn" stroke="currentColor"/><circle cx="560" cy="40" r="5" class="f-warn" stroke="currentColor"/>
  <polyline points="70,192 131,192 192,192 315,192 560,192" fill="none" stroke="currentColor" stroke-width="2" stroke-dasharray="6 4"/><rect x="65" y="186" width="10" height="10" class="f-ok" stroke="currentColor"/><rect x="126" y="186" width="10" height="10" class="f-ok" stroke="currentColor"/><rect x="188" y="186" width="10" height="10" class="f-ok" stroke="currentColor"/><rect x="310" y="186" width="10" height="10" class="f-ok" stroke="currentColor"/><rect x="555" y="186" width="10" height="10" class="f-ok" stroke="currentColor"/>
  <text x="570" y="44" fill="currentColor">plain: 3 + C_M</text><text x="570" y="196" fill="currentColor">PI = IPCP = 3</text>
  <text x="570" y="106" fill="currentColor" font-size="12">PI 는 B 로 3,</text><text x="570" y="122" fill="currentColor" font-size="12">IPCP 는 r 로 3</text>
</svg>
```

plain 은 **3 + C_M** (RTOS-M03 의 무한 역전, 같은 숫자), PI 와 IPCP 는 C_M 과 무관하게 **3 = L 의 남은 임계 구역** 이고 H 가 끝나는 tick(6)도 같다. 차이는 모양이다: PI 는 H 가 t2 에 `LOCK` 에서 막혔다가(`B`, 문맥 교환) L 의 `UNLOCK` 때 handoff 로 받아 깨어나고(또 문맥 교환), IPCP 는 H 가 시작 전에 기다렸다가(`r`) 비어 있는 R 을 바로 잡는다 — 막힘·깨움이 없다.

**설정 오류 (EINVAL):** 천장을 실제 사용자보다 낮게(2) 적으면 L 이 R 을 쥐어도 2 까지만 올라가 H(3)가 선점하고 들어와 R 을 기다리게 된다 — 보장이 통째로 깨진다. 그래서 POSIX 는 `pthread_mutex_lock` 에서 "호출 스레드의 우선순위가 그 뮤텍스의 천장보다 높으면 `EINVAL`" 로 막는다. 테스트: `ceiling = 2` 로 고정하면 H 의 `LOCK` 이 `K_EINVAL`, `lock_errors == 1`. (POSIX 문구의 "calling thread's priority" 를 기본으로 볼지 현재(올라간) 값으로 볼지는 구현 차이가 있을 수 있다 — 이 모델은 기본 우선순위로 검사, 미검증.)

**실제 OS 에서 — `demo_posix_ceiling.c`** (스레드 우선순위 20, 천장 40 인 A → 천장 30 인 B → A 먼저 해제). Lima VM `elinux` (Ubuntu aarch64, gcc 15.2, glibc 2.43, kernel 7.0.0-38), `sudo` 실행 실제 출력:

```text
setschedparam(SCHED_FIFO, 20) = 0 ok
start (base 20)            pthread_getschedparam=20  sched_getparam(kernel)=20
lock A (ceiling 40) = 0
  holding A                pthread_getschedparam=20  sched_getparam(kernel)=40
lock B (ceiling 30) = 0
  holding A+B              pthread_getschedparam=20  sched_getparam(kernel)=40
  unlock A (B still held)  pthread_getschedparam=20  sched_getparam(kernel)=30
  unlock B                 pthread_getschedparam=20  sched_getparam(kernel)=20
lock (ceiling 10 < my 20) = 22 Invalid argument
```

- 잡는 즉시 커널 우선순위가 40 (IPCP 의 "immediate"), A 를 먼저 놓으면 **아직 쥔 B 의 30** 으로 (LIFO 가 아니어도 재계산), 다 놓으면 20. 천장 10 짜리는 `EINVAL`. 이 레슨의 `ipcp_eff_prio` 와 같은 규칙이다.
- `pthread_getschedparam` 은 내내 20 — glibc 가 돌려주는 것은 **설정된 기본값** 이고, 부스트된 값은 `sched_getparam`(커널)에서만 보였다.
- root 없이 돌리면 `SCHED_FIFO` 설정이 `EPERM` 이고 천장 40 잠금이 `EINVAL` 이 되는 등 결과가 달라졌다 (원인 미분석 — RT 우선순위 권한 문제로 추정).
- **macOS 26 (Apple M6) 실측**: 속성 설정은 모두 0 을 돌려주지만 잠금 중에도 `pthread_getschedparam=20` 그대로, 천장 10 짜리 잠금도 `0 ok`. 즉 **천장이 적용된 흔적이 없다** (libpthread 소스는 확인 안 함). 맥에서 이 기능을 믿으면 안 된다.

<!-- code: demo_posix_ceiling.c -->

### Part B — 반대 순서 두 뮤텍스

```svg
<svg viewBox="0 0 740 274" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">L: A→B, H: B→A (반대 순서). 실제 ./solution -v 타임라인</text>
  <text x="130" y="50" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="50" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="50" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="50" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="50" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="50" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="50" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="50" fill="currentColor" text-anchor="middle" font-size="12">7</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">plain</text>
  <text x="82" y="70" fill="currentColor" font-family="monospace">H</text><rect x="191" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="250" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="271" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text>
  <text x="82" y="90" fill="currentColor" font-family="monospace">L</text><rect x="111" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="77" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text>
  <text x="440" y="80" fill="currentColor" font-size="13">t4 교착 (모두 B)</text>
  <text x="130" y="118" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="118" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="118" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="118" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="118" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="118" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="118" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="118" fill="currentColor" text-anchor="middle" font-size="12">7</text>
  <text x="10" y="118" fill="currentColor" font-weight="bold">PI</text>
  <text x="82" y="138" fill="currentColor" font-family="monospace">H</text><rect x="191" y="125" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="125" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="250" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="271" y="125" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text>
  <text x="82" y="158" fill="currentColor" font-family="monospace">L</text><rect x="111" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="145" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text>
  <text x="440" y="148" fill="currentColor" font-size="13">t4 교착 (상속해도)</text>
  <text x="130" y="186" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="186" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="186" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="186" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="186" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="186" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="186" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="186" fill="currentColor" text-anchor="middle" font-size="12">7</text>
  <text x="10" y="186" fill="currentColor" font-weight="bold">IPCP</text>
  <text x="82" y="206" fill="currentColor" font-family="monospace">H</text><rect x="191" y="193" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="206" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="193" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="206" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="193" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="193" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="193" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="226" fill="currentColor" font-family="monospace">L</text><rect x="111" y="213" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="213" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="213" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="213" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="213" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="226" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="213" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="226" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="213" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="226" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="213" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="440" y="216" fill="currentColor" font-size="13">완료, 막힘 0회</text>
  <rect x="10" y="244" width="16" height="14" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="32" y="256" fill="currentColor" font-size="13">R 실행</text>
  <rect x="180" y="244" width="16" height="14" class="f-warn" stroke="currentColor" stroke-width="0.6"/>
  <text x="202" y="256" fill="currentColor" font-size="13">B 뮤텍스에 막힘</text>
  <rect x="350" y="244" width="16" height="14" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
  <text x="372" y="256" fill="currentColor" font-size="13">r 준비됐지만 못 돎</text>
  <text x="520" y="256" fill="currentColor" font-size="13">. 출시 전 / 끝남</text>
</svg>
```

실제 출력 (`./solution -v`):

```text
ABBA  [plain]
tick : 0 1 2 3 4
H    : . . R B B
L    : R R r R B
-> DEADLOCK at tick 4 (all tasks blocked)
ABBA  [PI]                       (plain 과 칸 하나 다르지 않다 — 다른 점은 t3 부터 L 의 유효 우선순위가 3)
ABBA  [IPCP]
tick : 0 1 2 3 4 5 6 7
H    : . . r r R R R .
L    : R R R R r r r R
-> DONE after 8 ticks, blocking locks=0
```

plain/PI 에서 일어난 일: t1 L 이 A 획득 → t2 H 가 L 을 선점해 B 획득, 작업 → t3 H 가 A 요청, 막힘 (PI 면 L 이 3 으로) → 같은 tick 에 L 이 작업 → t4 L 이 B 요청, H 가 쥐고 있어 막힘 → t4 에 READY 0, 출시 예정 0, BLOCKED 2 → `sim_deadlocked()` true. 대기 그래프(wait-for graph)는 `H → A(소유 L) → L → B(소유 H) → H` 사이클이다 (연습 2 가 이 문자열을 만든다).

**PI 가 교착을 못 막는 이유:** 상속은 "누가 먼저 도느냐" 만 바꾼다. 교착의 네 조건(상호 배제, 보유하며 대기, 비선점, 순환 대기 — OS-M10) 중 어느 것도 건드리지 않는다. L 이 3 이 되어 빨리 달려도 결국 H 가 쥔 B 앞에서 멈춘다.

**IPCP 가 막는 이유:** A 와 B 의 천장은 둘 다 3 이다. L 이 A 를 잡는 순간(t1) 3 이 되므로 H 는 **B 를 잡을 기회 자체가 없다** — 순환 대기의 두 번째 고리가 생기지 않는다. 일반화하면, 어떤 태스크가 뮤텍스를 쥐고 있을 때 그 뮤텍스의 다른 사용자는 시작조차 못 하므로 "서로 하나씩 쥐고 기다리는" 상태에 도달할 수 없다. 테스트는 `blocking_locks == 0` (막힌 LOCK 0회)로 이를 확인한다. (3자 고리 `X:A→B, Y:B→C, Z:C→A` 도 PI 는 t5 교착, IPCP 는 완료 — 연습 2.)

**탐지 구현의 함정:** `sim_deadlocked` 는 "READY 없음" 만 보면 안 된다. 출시 예정 태스크가 남아 있는 idle 구간을 교착으로 오판한다 (테스트: 시작 직후 상태 → false). 그리고 이 판정은 **모든** 태스크가 막힌 전역 교착만 잡는다. 실제 시스템에서는 교착에 빠진 두 태스크 옆에서 idle·통신 태스크가 계속 돌기 때문에, 부분 교착은 대기 그래프 사이클(연습 2), 락 순서 기록(OS-M11 mini lockdep), `xSemaphoreTake` 타임아웃 + 워치독으로 잡는다.

### Part C — 블로킹 상한: 임계 구역 1개

```c
static int ipcp_blocking_bound(const int *prio, const use_t *u, int nu, int i) {
    int b = 0;
    for (int x = 0; x < nu; x++)
        if (prio[u[x].task] < prio[i] && ipcp_ceiling(prio, u, nu, u[x].res) >= prio[i] && u[x].cs > b)
            b = u[x].cs;
    return b;
}
```

| 사용 표 | 자원 천장 | 결과 |
|---|---|---|
| H: SPI 1, PARAM 1 · M: SPI 3 · L: PARAM 4 | SPI = 3, PARAM = 3 | B_H = max(M·SPI 3, L·PARAM 4) = **4**, B_M = L·PARAM **4**, B_L = 0 |
| PI 비교용 단순 상한 (낮은 태스크마다 하나씩 합) | — | H: 3 + 4 = **7** |

**왜 하나뿐인가 (IPCP):** H 가 출시될 때 "천장 ≥ 3 인 자원을 쥔 더 낮은 태스크" 는 많아야 하나다. 두 개(L 과 M)라면 나중에 잡은 쪽은 이미 다른 쪽이 천장 3 으로 올라가 있는 동안 실행되어야 했는데, 그 쪽 우선순위는 3 보다 낮으니 불가능하다. 그리고 H 는 시작한 뒤에는 Part A 의 논증대로 한 번도 막히지 않는다. 결과: **최악 블로킹 = 더 낮은 태스크의 (천장 ≥ prio(H)) 임계 구역 중 가장 긴 것 하나**, 그리고 그 블로킹은 **시작 전에** 한 번만 일어난다. Sha·Rajkumar·Lehoczky(1990)는 원래 PCP 에 대해 "임계 구역 1개" 와 교착 없음 정리를 증명했고, PI 는 "min(n, m) 개 임계 구역" (n = 더 낮은 태스크 수, m = 막을 수 있는 세마포어 수) 이 상한이다.

H 출시 시각을 0~12 로 바꿔 돌린 실제 결과:

```svg
<svg viewBox="0 0 740 290" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="18" fill="currentColor" font-weight="bold">Part C: H 출시 시각별 H 대기 (PI vs IPCP, 실제 스윕 결과)</text>
  <line x1="60" y1="230" x2="600" y2="230" stroke="currentColor"/><line x1="60" y1="40" x2="60" y2="230" stroke="currentColor"/>
  <line x1="56" y1="230" x2="60" y2="230" stroke="currentColor"/><text x="52" y="234" fill="currentColor" text-anchor="end" font-size="12">0</text><line x1="56" y1="182" x2="60" y2="182" stroke="currentColor"/><text x="52" y="186" fill="currentColor" text-anchor="end" font-size="12">2</text><line x1="56" y1="135" x2="60" y2="135" stroke="currentColor"/><text x="52" y="139" fill="currentColor" text-anchor="end" font-size="12">4</text><line x1="56" y1="88" x2="60" y2="88" stroke="currentColor"/><text x="52" y="92" fill="currentColor" text-anchor="end" font-size="12">6</text><line x1="56" y1="40" x2="60" y2="40" stroke="currentColor"/><text x="52" y="44" fill="currentColor" text-anchor="end" font-size="12">8</text>
  <rect x="64" y="230" width="17" height="0" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="81" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="81" y="246" fill="currentColor" text-anchor="middle" font-size="12">0</text>
  <rect x="106" y="159" width="17" height="71" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="122" y="159" width="17" height="71" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="122" y="246" fill="currentColor" text-anchor="middle" font-size="12">1</text>
  <rect x="147" y="159" width="17" height="71" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="164" y="182" width="17" height="48" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="164" y="246" fill="currentColor" text-anchor="middle" font-size="12">2</text>
  <rect x="189" y="111" width="17" height="119" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="205" y="206" width="17" height="24" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="205" y="246" fill="currentColor" text-anchor="middle" font-size="12">3</text>
  <rect x="230" y="135" width="17" height="95" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="247" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="247" y="246" fill="currentColor" text-anchor="middle" font-size="12">4</text>
  <rect x="272" y="159" width="17" height="71" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="288" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="288" y="246" fill="currentColor" text-anchor="middle" font-size="12">5</text>
  <rect x="313" y="159" width="17" height="71" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="330" y="182" width="17" height="48" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="330" y="246" fill="currentColor" text-anchor="middle" font-size="12">6</text>
  <rect x="355" y="182" width="17" height="48" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="372" y="206" width="17" height="24" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="372" y="246" fill="currentColor" text-anchor="middle" font-size="12">7</text>
  <rect x="396" y="206" width="17" height="24" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="413" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="413" y="246" fill="currentColor" text-anchor="middle" font-size="12">8</text>
  <rect x="438" y="230" width="17" height="0" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="455" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="455" y="246" fill="currentColor" text-anchor="middle" font-size="12">9</text>
  <rect x="479" y="230" width="17" height="0" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="496" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="496" y="246" fill="currentColor" text-anchor="middle" font-size="12">10</text>
  <rect x="521" y="230" width="17" height="0" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="538" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="538" y="246" fill="currentColor" text-anchor="middle" font-size="12">11</text>
  <rect x="562" y="230" width="17" height="0" class="f-warn" stroke="currentColor" stroke-width="0.6"/><rect x="579" y="230" width="17" height="0" class="f-acc" stroke="currentColor" stroke-width="0.6"/><text x="579" y="246" fill="currentColor" text-anchor="middle" font-size="12">12</text>
  <line x1="60" y1="135" x2="600" y2="135" stroke="currentColor" stroke-dasharray="5 4"/><text x="608" y="139" fill="currentColor" font-size="12">B_H(IPCP) = 4</text>
  <line x1="60" y1="64" x2="600" y2="64" stroke="currentColor" stroke-dasharray="2 3"/><text x="608" y="68" fill="currentColor" font-size="12">PI 상한 = 4+3 = 7</text>
  <text x="330" y="264" fill="currentColor" text-anchor="middle" font-size="12">H 출시 tick (L 은 t0, M 은 t1 출시)</text>
  <rect x="60" y="276" width="14" height="12" class="f-warn" stroke="currentColor"/><text x="80" y="287" fill="currentColor" font-size="12">PI (최대 5, t3 출시)</text><rect x="260" y="276" width="14" height="12" class="f-acc" stroke="currentColor"/><text x="280" y="287" fill="currentColor" font-size="12">IPCP (최대 3 = 임계 구역 1개의 남은 부분)</text>
</svg>
```

PI 최악(t3 출시)과 같은 출시의 IPCP (`./solution -v` 실제 타임라인):

```svg
<svg viewBox="0 0 740 246" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">Part C, H 출시 t3: PI 는 임계 구역 2개에 연달아 막힌다 (실제 출력)</text>
  <text x="128" y="50" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="164" y="50" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="200" y="50" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="236" y="50" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="272" y="50" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="308" y="50" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="344" y="50" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="380" y="50" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="416" y="50" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="452" y="50" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="488" y="50" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="524" y="50" fill="currentColor" text-anchor="middle" font-size="12">11</text><text x="560" y="50" fill="currentColor" text-anchor="middle" font-size="12">12</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">PI</text>
  <text x="82" y="70" fill="currentColor" font-family="monospace">H</text><rect x="219" y="57" width="34" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="236" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="255" y="57" width="34" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="272" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="291" y="57" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="327" y="57" width="34" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="344" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="363" y="57" width="34" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="380" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="399" y="57" width="34" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="416" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="435" y="57" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="57" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="90" fill="currentColor" font-family="monospace">M</text><rect x="147" y="77" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="183" y="77" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="219" y="77" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="255" y="77" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="291" y="77" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="308" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="327" y="77" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="344" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="363" y="77" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="380" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="399" y="77" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="416" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="435" y="77" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="452" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="77" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="488" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="507" y="77" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="110" fill="currentColor" font-family="monospace">L</text><rect x="111" y="97" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="147" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="164" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="183" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="200" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="219" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="236" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="255" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="272" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="291" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="308" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="327" y="97" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="363" y="97" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="399" y="97" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="435" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="452" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="488" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="507" y="97" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="524" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="543" y="97" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="588" y="90" fill="currentColor" font-size="13">H 대기 2+3=5</text>
  <text x="128" y="138" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="164" y="138" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="200" y="138" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="236" y="138" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="272" y="138" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="308" y="138" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="344" y="138" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="380" y="138" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="416" y="138" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="452" y="138" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="488" y="138" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="524" y="138" fill="currentColor" text-anchor="middle" font-size="12">11</text><text x="560" y="138" fill="currentColor" text-anchor="middle" font-size="12">12</text>
  <text x="10" y="138" fill="currentColor" font-weight="bold">IPCP</text>
  <text x="82" y="158" fill="currentColor" font-family="monospace">H</text><rect x="219" y="145" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="236" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="255" y="145" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="291" y="145" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="327" y="145" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="178" fill="currentColor" font-family="monospace">M</text><rect x="147" y="165" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="164" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="183" y="165" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="200" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="219" y="165" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="236" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="255" y="165" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="272" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="291" y="165" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="308" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="327" y="165" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="344" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="363" y="165" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="399" y="165" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="435" y="165" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="165" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="507" y="165" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="198" fill="currentColor" font-family="monospace">L</text><rect x="111" y="185" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="147" y="185" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="183" y="185" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="219" y="185" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="255" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="272" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="291" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="308" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="327" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="344" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="363" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="380" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="399" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="416" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="435" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="452" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="488" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="507" y="185" width="34" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="524" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="543" y="185" width="34" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="588" y="178" fill="currentColor" font-size="13">H 대기 1</text>
  <rect x="10" y="216" width="16" height="14" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="32" y="228" fill="currentColor" font-size="13">R 실행</text>
  <rect x="180" y="216" width="16" height="14" class="f-warn" stroke="currentColor" stroke-width="0.6"/>
  <text x="202" y="228" fill="currentColor" font-size="13">B 뮤텍스에 막힘</text>
  <rect x="350" y="216" width="16" height="14" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
  <text x="372" y="228" fill="currentColor" font-size="13">r 준비됐지만 못 돎</text>
  <text x="520" y="228" fill="currentColor" font-size="13">. 출시 전 / 끝남</text>
</svg>
```

PI 에서 H 는 t3 에 SPI 를 요청 → M 의 남은 SPI 임계 구역 2 tick(M 이 3 상속, t3–4) → t5 에 SPI 일을 하고 PARAM 요청 → 이번엔 L 의 남은 PARAM 임계 구역 3 tick(t6–8). 이것이 **연쇄 블로킹(chained blocking)** — 임계 구역 2개, 합 5 로 B_H = 4 를 넘는다 (단순 상한 7 이내). IPCP 에서는 L 이 t0 에 PARAM 을 잡자마자 3 이 되어 M 이 SPI 를 잡을 기회가 없었으므로 H 는 L 의 마지막 1 tick 만 기다렸다. IPCP 측정 최대는 3(t1 출시) ≤ B_H = 4. 상한보다 1 작은 것은 tick 이산화 때문이다: L 은 잠그는 tick 에 이미 1 단위 일을 하고, H 출시는 tick 경계에서만 일어나므로 이 모델의 최대 관측값은 cs − 1 이다 (PI 의 5 = (3−1) + (4−1) 도 같은 이유).

**IPCP 의 대가 — 천장 블로킹:** B_M = 4 가 말해 주듯, M 은 PARAM 을 쓰지 않는데도 L 이 PARAM 을 쥔 동안 막힌다. H 가 없는 실행(H 출시 t40)에서 실측: M 대기 IPCP 3, PI 0 (테스트). 이것이 "IPCP 는 불필요한 블로킹이 많다" 는 비판의 실체이고, 그래서 **임계 구역은 짧게** 가 IPCP 의 제1 규칙이다. 이 B_i 가 응답 시간 분석 `R_i = C_i + B_i + Σ_{j∈hp(i)} ⌈R_i / T_j⌉ C_j` 의 B_i 다 (RTOS-M11, RTOS-A04).

### OPCP(원래 PCP) vs IPCP

| | OPCP (Sha·Rajkumar·Lehoczky 1990 "PCP") | IPCP ("highest locker", Priority Ceiling Emulation) |
|---|---|---|
| 우선순위가 오르는 때 | 누가 그 태스크 때문에 막혔을 때 (상속) | 잡는 **즉시** 천장으로 |
| 잠금 규칙 | `eff(t) >` **시스템 천장**(다른 태스크가 쥔 뮤텍스들의 천장 최댓값)일 때만 — 원하는 뮤텍스가 비어 있어도 거절될 수 있다 | 비어 있으면 잡는다 (그리고 항상 비어 있다) |
| H 가 기다리는 모습 | 실행 중 `LOCK` 에서 `B` | 시작 전에 `r` |
| 최악 블로킹 · 교착 | 임계 구역 1개 · 없음 | 임계 구역 1개 · 없음 |
| 구현 · 문맥 교환 | 시스템 천장 추적 + 상속 → 복잡, 막힘/깨움 있음 | 우선순위 두 번 설정 → 단순, 막힘 없음 |
| 자원과 무관한 중간 태스크 | 덜 막힘 (연습 1: M 지연 0) | 더 막힘 (연습 1: M 지연 2) |
| 스택 공유 | 불가 | 가능 — 블로킹이 시작 전에만 있어서 같은 천장 태스크들이 스택 하나를 쓸 수 있다 (Baker 1991 SRP) |
| 실제 채택 | 상용 RTOS 에서 드묾 (문서 기준) | POSIX `PRIO_PROTECT`, OSEK/AUTOSAR 자원, Ada `Ceiling_Locking`, RTEMS 천장 세마포어 (문서 기준) |

## 6. 정답 코드

<!-- code: solution.c diff=starter.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 solution.c -o solution && ./solution     # 43/43 passed (6회 실행 동일)
./solution -v                                                                  # 위 타임라인·표
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer solution.c -o sol_san && ./sol_san -v   # 보고 없음
```

스레드를 쓰지 않으므로 TSan 대상이 아니다 (OS 스레드 1개에서 도는 결정적 시뮬레이터).

## 7. 자주 하는 실수

| 실수 | 왜 틀렸나 | 고친 코드 / 규칙 |
|---|---|---|
| 천장을 "넉넉하게" 최고 우선순위로 | 맞긴 하지만 그 자원과 무관한 모든 태스크가 천장 블로킹을 겪는다 (= 스케줄러 잠금) | `ceiling = max{base(t) : t 가 m 을 쓴다}` — 정확히 그 값 |
| 천장을 실제 사용자보다 낮게 | 쥔 태스크가 사용자보다 낮게 머물러 선점당함 → 보장 전부 무효 | 사용 표 갱신을 리뷰 항목에. POSIX 는 `EINVAL`, 이 모델은 `K_EINVAL` |
| 놓을 때 "잡기 전 값" 으로 복원 | 해제가 LIFO 가 아니면 B 를 쥔 채 1 로 떨어지거나 아무것도 없는데 3 으로 돌아간다 (연습 3) | `eff = max(base, 아직 쥔 것들의 천장)` 재계산 |
| 임계 구역 안에서 `vTaskDelay` / 큐 대기 | 쥔 태스크가 잠들면 더 낮은 사용자가 돌아 `LOCK` 에서 막힌다 → 교착 가능성 부활 | 임계 구역 = 계산과 레지스터 접근만. 기다릴 일은 밖에서 |
| time slicing 켠 채 천장 = 최고 사용자 | 같은 우선순위 H 가 시간 조각을 받아 `LOCK` 에서 막힘 (연습 4) | `configUSE_TIME_SLICING 0` 또는 천장 = 최고 사용자 + 1 |
| "PI 뮤텍스니까 교착도 안 난다" | 상속은 실행 순서만 바꾼다. 순환 대기 그대로 (Part B: t4 교착) | 락 순서 고정, 또는 IPCP |
| 멀티코어에서 "우선순위를 올렸으니 배타적" | 다른 코어의 태스크는 우선순위와 무관하게 돈다 | 진짜 뮤텍스/스핀락 유지 (RTOS-A08), 멀티코어 프로토콜(MPCP·MSRP·MrsP) |
| 교착 판정 = "지금 READY 없음" | idle 구간(출시 예정 태스크 있음)을 교착으로 오판 | `T_UNREL` 도 확인 (Part B 테스트) |
| IPCP 라서 뮤텍스 객체를 지워 버림 | 단일 코어에선 동작하지만, 전제가 깨지는 순간(멀티코어 이식, CS 안 블로킹 추가) 조용히 데이터 경쟁 | 뮤텍스는 남기고 "항상 즉시 성공" 을 assert |

## 8. 면접 Q&A

<details>
<summary>Q1. 우선순위 상속과 우선순위 천장을 30초 안에 비교해 보세요.</summary>

상속(PI)은 **막혔을 때** 소유자를 대기자의 우선순위로 올린다 — 설정이 필요 없고 동적이지만, 연쇄 블로킹(최대 min(n, m) 개 임계 구역)과 교착은 못 막는다. 천장(IPCP)은 자원마다 사용자 최고 우선순위를 미리 정해 두고 **잡는 즉시** 그 값으로 올린다 — 블로킹은 시작 전에 한 번, 임계 구역 1개로 묶이고 교착이 원천적으로 없으며 구현이 단순하다. 대가는 정적 분석(누가 무엇을 쓰는지)이 필요하고, 자원과 무관한 중간 우선순위 태스크도 막힌다는 것. 이 레슨 실측: 같은 펌웨어 세트에서 H 최악 대기 PI 5 vs IPCP 3 (상한 7 vs 4), 대신 H 가 없을 때 M 대기 PI 0 vs IPCP 3.

</details>

<details>
<summary>Q2. 천장 값은 어떻게 정하나요? ISR 도 그 자원을 쓰면요?</summary>

천장 = 그 자원을 잠그는 모든 태스크의 (기본) 우선순위 중 최댓값. 소스/설계 표에서 계산하며, OSEK/AUTOSAR 는 OIL/ARXML 에 "이 태스크가 이 자원을 쓴다" 를 적으면 생성 도구가 계산한다. ISR 이 같은 자원을 쓰면 천장은 **그 ISR 의 인터럽트 레벨** 까지 올라가야 한다 — 즉 자원을 잡는 동안 그 레벨 이하 인터럽트를 마스크하는 것과 같다 (Cortex-M 에서는 BASEPRI). AUTOSAR OS 가 ISR 자원을 이렇게 다루고(문서 기준), FreeRTOS 의 `taskENTER_CRITICAL()` 이 BASEPRI 를 `configMAX_SYSCALL_INTERRUPT_PRIORITY` 로 올리는 것이 "태스크 + 낮은 ISR 전부" 를 천장으로 하는 극단적인 경우다 (C-M14 PRIMASK/BASEPRI).

</details>

<details>
<summary>Q3. IPCP 는 왜 교착이 안 생기죠? 그 증명이 깨지는 조건은?</summary>

m 을 쥔 태스크는 천장 이상으로 올라가 있으므로 m 의 다른 사용자는 시작조차 못 한다 → 사용자가 `LOCK(m)` 을 실행할 때 m 은 항상 비어 있다 → 아무도 막히지 않으면 순환 대기가 없다. 깨지는 조건: ① 임계 구역 안에서 스스로 대기(delay, 큐/세마포어 블로킹) — 낮은 태스크가 돌게 된다, ② 같은 우선순위 time slicing — 천장과 같은 우선순위의 사용자가 시간 조각을 받는다 (연습 4, FreeRTOS 기본 `configUSE_TIME_SLICING` 1 — FreeRTOS.h:2771–2772), ③ 멀티코어 — 다른 코어는 우선순위를 무시하고 돈다, ④ 천장을 낮게 잘못 설정.

</details>

<details>
<summary>Q4. 그렇게 좋으면 FreeRTOS·Zephyr 는 왜 기본으로 안 주나요?</summary>

천장은 "모든 사용자를 미리 안다" 는 닫힌 시스템 가정이 필요하다. 범용 RTOS 의 뮤텍스는 라이브러리·동적 태스크·서드파티 코드에서 쓰이므로 사용자 집합을 커널이 알 수 없고, 잘못된 천장은 조용히 보장을 깬다. 상속은 설정 없이 "대체로 괜찮게" 동작한다. 또 IPCP 는 무관한 태스크까지 막아 평균 응답성이 나빠질 수 있다. 반대로 OSEK/AUTOSAR 처럼 모든 태스크·자원을 빌드 시 정적으로 선언하는 시스템은 IPCP 가 자연스럽고, 교착이 원천적으로 없다는 점과 스택 공유가 안전 인증(ISO 26262)에서 큰 장점이다. FreeRTOS V11.3.2 소스에 천장 관련 코드는 없다(직접 grep). Zephyr 는 문서 기준으로 `k_mutex` 상속만 제공한다(미검증).

</details>

<details>
<summary>Q5. 원래의 PCP(OPCP)와 IPCP 는 뭐가 다른가요? 왜 실무는 IPCP 인가요?</summary>

OPCP 는 잡을 때 우선순위를 올리지 않는다. 대신 "내 우선순위 > 시스템 천장(다른 태스크가 쥔 뮤텍스들의 천장 최댓값)" 일 때만 잠금을 허락하고, 거절되면 그 천장을 만든 태스크가 내 우선순위를 상속한다. 그래서 원하는 뮤텍스가 **비어 있어도** 막힐 수 있다 (연습 1: ABBA 에서 H 가 빈 B 앞에서 `B`). 최악 블로킹·교착 없음은 IPCP 와 같고, 자원과 무관한 태스크를 덜 막는다는 장점이 있다. 하지만 시스템 천장 추적 + 상속 + 막힘/깨움이 필요해 구현과 검증이 어렵다. IPCP 는 "우선순위 두 번 설정" 으로 끝나고 막힘 자체가 없어 문맥 교환이 적다. 그래서 POSIX·OSEK·Ada 가 채택한 것은 IPCP 다.

</details>

<details>
<summary>Q6. POSIX 의 PTHREAD_PRIO_PROTECT 와 PTHREAD_PRIO_INHERIT 는? 실제로 동작하던가요?</summary>

`pthread_mutexattr_setprotocol` 의 값이다. `PTHREAD_PRIO_INHERIT` = 우선순위 상속, `PTHREAD_PRIO_PROTECT` = 천장 (`pthread_mutexattr_setprioceiling` 으로 값 지정). POSIX 문구는 "PROTECT 뮤텍스를 하나 이상 쥔 스레드는 자기 우선순위와 쥔 뮤텍스들의 천장 중 최댓값으로 실행된다" — 즉 IPCP 다. 천장보다 높은 스레드가 잠그면 `EINVAL`. Linux(glibc 2.43, SCHED_FIFO, root) 실측: 커널 우선순위가 20 → 40 → (A 해제 후) 30 → 20, 천장 10 잠금 `EINVAL`. glibc 의 `pthread_getschedparam` 은 부스트를 보여 주지 않았다. macOS 26 은 속성을 받아들이지만 효과·EINVAL 모두 관찰되지 않았다. 덧: 리눅스 커널의 `rt_mutex`(futex PI)는 상속만 있고, PROTECT 는 glibc 가 사용자 공간에서 우선순위를 바꿔 구현한다 — 그래서 sched 권한이 필요하다(관찰과 일치, 소스 미확인).

</details>

<details>
<summary>Q7. 이 태스크 세트가 데드라인을 지키는지 블로킹까지 넣어 확인하려면?</summary>

각 태스크의 B_i 를 먼저 구한다 — IPCP 면 "더 낮은 태스크가 쥐는, 천장 ≥ prio(i) 인 자원의 임계 구역 중 최장 하나" (Part C: B_H = 4, B_M = 4). PI 면 낮은 태스크마다/세마포어마다 하나씩의 합(보수적, 여기서 7). 그다음 응답 시간 분석 `R_i = C_i + B_i + Σ_{j∈hp(i)} ⌈R_i/T_j⌉ C_j` 를 고정점까지 반복해 `R_i ≤ D_i` 확인 (RTOS-M11, 블로킹·지터 포함은 RTOS-A04). 꼬리질문 "임계 구역 길이는 어떻게 아나?" → 최악 실행 시간(WCET) 측정·분석 (RTOS-A03): 트레이스로 take→give 간격의 최댓값, 또는 정적 분석 도구.

</details>

<details>
<summary>Q8. 이미 PI 뮤텍스로 짜인 코드에서 교착이 의심됩니다. 어떻게 찾고 막나요?</summary>

찾기: ① 멈춘 보드에서 디버거로 각 태스크 상태와 각 뮤텍스 소유자를 읽어 대기 그래프를 그린다 — 사이클이면 교착 (연습 2 가 그 알고리즘), ② 개발 빌드에서 락 획득 순서를 기록해 "A 쥐고 B" 와 "B 쥐고 A" 가 둘 다 나오면 경고 (OS-M11 mini lockdep, TSan 의 lock-order-inversion — TOOL-M09), ③ `xSemaphoreTake(m, timeout)` 실패를 로깅 + 워치독. 막기: 전역 락 순서(모든 코드가 A → B), 두 자원을 하나의 뮤텍스로 합치기, 임계 구역 안에서 다른 락 안 잡기, 또는 자원을 IPCP 로 전환. 교착 4조건과 각 해법은 OS-M10.

</details>

## 9. 추가 연습

### 연습 1 — OPCP 구현: 시스템 천장 규칙

`PROTO_OPCP` 를 추가하라: `eff(t) >` (다른 태스크가 쥔 뮤텍스 천장의 최댓값) 일 때만 잠금 허락, 아니면 그 천장을 만든 뮤텍스에 막히고 소유자가 상속. 해제로 시스템 천장이 내려가 규칙을 통과하게 되면 높은 순서로 바로 넘겨준다. ABBA·정규 시나리오의 H 지연, 그리고 H 가 늦게(t30) 오는 정규 시나리오에서 M 의 지연을 네 프로토콜로 비교하라.

<details>
<summary>답 보기</summary>

핵심은 `opcp_blocker()` — 다른 태스크가 쥔 뮤텍스 중 천장이 가장 높은 것을 찾고, `eff(t)` 가 그보다 크면 -1(허락). 실제 출력 (`./practice -v`):

```svg
<svg viewBox="0 0 740 206" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">같은 ABBA, IPCP vs OPCP (./practice -v)</text>
  <text x="130" y="50" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="50" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="50" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="50" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="50" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="50" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="50" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="50" fill="currentColor" text-anchor="middle" font-size="12">7</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">IPCP</text>
  <text x="82" y="70" fill="currentColor" font-family="monospace">H</text><rect x="191" y="57" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="57" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="90" fill="currentColor" font-family="monospace">L</text><rect x="111" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="440" y="80" fill="currentColor" font-size="13">H: r (출시부터 못 돎)</text>
  <text x="130" y="118" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="118" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="118" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="118" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="118" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="118" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="118" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="118" fill="currentColor" text-anchor="middle" font-size="12">7</text>
  <text x="10" y="118" fill="currentColor" font-weight="bold">OPCP</text>
  <text x="82" y="138" fill="currentColor" font-family="monospace">H</text><rect x="191" y="125" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="210" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="231" y="125" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="250" y="138" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="271" y="125" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="125" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="125" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="158" fill="currentColor" font-family="monospace">L</text><rect x="111" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="440" y="148" fill="currentColor" font-size="13">H: B (B가 비어도 막힘)</text>
  <rect x="10" y="176" width="16" height="14" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="32" y="188" fill="currentColor" font-size="13">R 실행</text>
  <rect x="180" y="176" width="16" height="14" class="f-warn" stroke="currentColor" stroke-width="0.6"/>
  <text x="202" y="188" fill="currentColor" font-size="13">B 뮤텍스에 막힘</text>
  <rect x="350" y="176" width="16" height="14" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
  <text x="372" y="188" fill="currentColor" font-size="13">r 준비됐지만 못 돎</text>
  <text x="520" y="188" fill="currentColor" font-size="13">. 출시 전 / 끝남</text>
</svg>
```

- ABBA: OPCP 도 교착 없이 완료. t2 에 H 가 원한 B 는 **비어 있었지만** A(천장 3, L 소유)가 시스템 천장을 3 으로 만들어 H 는 `B` — "천장 블로킹". L 은 H 의 3 을 상속해 A·B 를 마저 끝낸다. H 지연 2 (IPCP 와 같다, 모양만 `B` vs `r`).
- 정규 시나리오 H 지연: plain 7, PI 3, IPCP 3, OPCP 3 (네 프로토콜 모두 "응답 시간 − 자기 일" 이 `B + r` 개수와 같음을 검사).
- H 가 t30 에 오면 M 의 지연: plain 0, PI 0, **IPCP 2**, OPCP 0. IPCP 만 "쓰지도 않는 R 의 천장" 때문에 M 을 붙잡는다. OPCP 는 누가 실제로 요청할 때만 막는다 — 대신 구현이 이만큼 길다 (`k_lock` 분기 + 해제 시 재평가 루프).

</details>

### 연습 2 — 대기 그래프 사이클 찾기

교착 상태의 커널에서 "막힌 태스크 → 기다리는 뮤텍스 → 소유자 → …" 를 따라가 사이클을 `"H>A>L>B>H"` 형태 문자열로 만드는 `find_cycle()` 을 작성하라. 3자 고리(X: A→B, Y: B→C, Z: C→A, 우선순위 1/2/3, 출시 t0/t1/t2)에도 적용하라.

<details>
<summary>답 보기</summary>

각 태스크는 뮤텍스 하나만 기다리고 뮤텍스 소유자는 하나이므로 대기 그래프의 각 노드는 나가는 간선이 1개(함수 그래프)다 → 시작점에서 따라가다 시작점으로 돌아오면 사이클, 소유자가 막혀 있지 않으면(READY/없음) 체인 끝. 결과: ABBA plain/PI → `"H>A>L>B>H"` (2개), 3자 고리 PI → t5 교착, `"X>B>Y>C>Z>A>X"` (3개). 3자 고리 IPCP → 완료, 막힘 0, 사이클 없음. OPCP 도 완료. 실제 커널 디버깅에서는 FreeRTOS 의 `xSemaphoreGetMutexHolder()` 와 태스크별 "기다리는 객체" (트레이스 훅) 를 이용해 같은 그래프를 만든다.

</details>

### 연습 3 — LIFO 가 아닌 해제 순서

천장 3 인 A, 천장 2 인 B 를 A → B 순서로 잡고 **A 를 먼저** 놓는다. "잡을 때 이전 값 저장, 놓을 때 그 값으로 복원" 하는 naive 구현과 재계산 구현의 유효 우선순위 변화를 비교하라.

<details>
<summary>답 보기</summary>

| 단계 | naive (저장/복원) | 재계산 `max(base, 쥔 것들의 천장)` | Linux glibc 실측 (20 / 40 / 30) |
|---|---|---|---|
| A 잡음 | 3 | 3 | 40 |
| B 잡음 | 3 | 3 | 40 |
| A 놓음 (B 쥔 채) | **1** (A 를 잡기 전 값) | 2 | 30 |
| B 놓음 | **3** (B 를 잡기 전 값) | 1 | 20 |

naive 는 B 를 쥔 채 1 로 떨어져 B 의 사용자(2)에게 선점당하고(보장 붕괴), 다 놓은 뒤엔 3 으로 남는다(무관한 태스크를 영원히 막음). 중첩 잠금은 대부분 LIFO 지만 에러 경로나 `goto cleanup` 에서 순서가 뒤집히기 쉽다. FreeRTOS 의 상속 해제(`xTaskPriorityDisinherit`)도 이 문제 때문에 "쥔 뮤텍스가 0 개가 될 때만" 기본값으로 돌아가는 보수적 규칙을 쓴다 (V11.3.2 tasks.c:6869–6870 `if( pxTCB->uxMutexesHeld == 0 )`, RTOS-M04 참고).

</details>

### 연습 4 — time slicing 이 IPCP 를 깨는 경우

정규 시나리오(IPCP, C_M=4)를 같은 우선순위끼리 매 tick 돌아가는(round robin) 스케줄러로 돌려 보라. 무엇이 깨지나? 천장을 하나 올리면?

<details>
<summary>답 보기</summary>

```svg
<svg viewBox="0 0 740 246" xmlns="http://www.w3.org/2000/svg" font-size="13">
  <text x="10" y="20" fill="currentColor" font-weight="bold">IPCP + 같은 우선순위 time slicing (./practice -v)</text>
  <text x="130" y="50" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="50" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="50" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="50" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="50" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="50" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="50" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="50" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="450" y="50" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="490" y="50" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="530" y="50" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="570" y="50" fill="currentColor" text-anchor="middle" font-size="12">11</text>
  <text x="10" y="50" fill="currentColor" font-weight="bold">ceil 3</text>
  <text x="82" y="70" fill="currentColor" font-family="monospace">H</text><rect x="191" y="57" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="250" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="271" y="57" width="38" height="17" class="f-warn" stroke="currentColor" stroke-width="0.6"/><text x="290" y="70" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">B</text><rect x="311" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="57" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="90" fill="currentColor" font-family="monospace">M</text><rect x="231" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="77" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="90" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="431" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="511" y="77" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="110" fill="currentColor" font-family="monospace">L</text><rect x="111" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="410" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="431" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="450" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="490" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="511" y="97" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="530" y="110" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="551" y="97" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="600" y="90" fill="currentColor" font-size="13">H 가 B: 보장 깨짐</text>
  <text x="130" y="138" fill="currentColor" text-anchor="middle" font-size="12">0</text><text x="170" y="138" fill="currentColor" text-anchor="middle" font-size="12">1</text><text x="210" y="138" fill="currentColor" text-anchor="middle" font-size="12">2</text><text x="250" y="138" fill="currentColor" text-anchor="middle" font-size="12">3</text><text x="290" y="138" fill="currentColor" text-anchor="middle" font-size="12">4</text><text x="330" y="138" fill="currentColor" text-anchor="middle" font-size="12">5</text><text x="370" y="138" fill="currentColor" text-anchor="middle" font-size="12">6</text><text x="410" y="138" fill="currentColor" text-anchor="middle" font-size="12">7</text><text x="450" y="138" fill="currentColor" text-anchor="middle" font-size="12">8</text><text x="490" y="138" fill="currentColor" text-anchor="middle" font-size="12">9</text><text x="530" y="138" fill="currentColor" text-anchor="middle" font-size="12">10</text><text x="570" y="138" fill="currentColor" text-anchor="middle" font-size="12">11</text>
  <text x="10" y="138" fill="currentColor" font-weight="bold">ceil 4</text>
  <text x="82" y="158" fill="currentColor" font-family="monospace">H</text><rect x="191" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="210" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="231" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="145" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="158" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="351" y="145" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="178" fill="currentColor" font-family="monospace">M</text><rect x="231" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="250" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="271" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="290" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="311" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="165" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="178" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="431" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="471" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="511" y="165" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="82" y="198" fill="currentColor" font-family="monospace">L</text><rect x="111" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="151" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="191" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="231" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="271" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/><rect x="311" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="330" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="351" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="370" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="391" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="410" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="431" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="450" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="471" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="490" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="511" y="185" width="38" height="17" class="f-mut" stroke="currentColor" stroke-width="0.6"/><text x="530" y="198" fill="currentColor" text-anchor="middle" font-size="12" font-family="monospace">r</text><rect x="551" y="185" width="38" height="17" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="600" y="178" fill="currentColor" font-size="13">막힘 0회</text>
  <rect x="10" y="216" width="16" height="14" class="f-acc" stroke="currentColor" stroke-width="0.6"/>
  <text x="32" y="228" fill="currentColor" font-size="13">R 실행</text>
  <rect x="180" y="216" width="16" height="14" class="f-warn" stroke="currentColor" stroke-width="0.6"/>
  <text x="202" y="228" fill="currentColor" font-size="13">B 뮤텍스에 막힘</text>
  <rect x="350" y="216" width="16" height="14" class="f-mut" stroke="currentColor" stroke-width="0.6"/>
  <text x="372" y="228" fill="currentColor" font-size="13">r 준비됐지만 못 돎</text>
  <text x="520" y="228" fill="currentColor" font-size="13">. 출시 전 / 끝남</text>
</svg>
```

천장 3 에서는 L(유효 3)과 H(3)가 같은 우선순위라 t3 에 H 가 차례를 받아 `LOCK(R)` 에서 막힌다 (`blocking_locks = 1`). "IPCP 에서 LOCK 은 막히지 않는다" 가 깨졌다 — 이 경우엔 대기열에서 handoff 로 회복되지만, 같은 상황이 두 뮤텍스에서 일어나면 교착 가능성도 돌아온다. 처방: ① 같은 우선순위 time slicing 끄기 (FreeRTOS `configUSE_TIME_SLICING 0`, POSIX `SCHED_FIFO`), ② 천장 = 최고 사용자 + 1 — 테스트에서 막힘 0, H 대기 3 으로 복구. FreeRTOS 는 기본이 time slicing 켜짐(FreeRTOS.h:2771–2772)이고 tick 마다 같은 우선순위 READY 가 있으면 전환을 요청한다(tasks.c:4964–4998). `vTaskPrioritySet` 으로 IPCP 를 흉내 낼 때 반드시 챙길 점이다.

</details>

### 한 번에 검증

<!-- code: practice.c -->

```bash
cc -std=c11 -Wall -Wextra -Werror -O2 practice.c -o practice && ./practice    # 19/19 passed
./practice -v                                                                  # 위 타임라인
cc -std=c11 -g -O1 -fsanitize=address,undefined -fno-omit-frame-pointer practice.c -o pr_san && ./pr_san -v   # 보고 없음
```

## 10. 핵심 정리

- **천장(m) = m 을 쓰는 태스크의 최고 기본 우선순위.** IPCP: 잡는 즉시 `eff = max(base, 쥔 것들의 천장)`, 놓으면 재계산 (저장값 복원 X).
- 단일 코어 · 임계 구역 안 블로킹 금지 · 같은 우선순위 time slicing 없음 ⇒ IPCP 의 `LOCK` 은 **항상 비어 있는 뮤텍스** 를 만난다 ⇒ 대기열·상속·교착이 없다.
- 최악 블로킹 = 더 낮은 태스크의 (천장 ≥ 내 우선순위) 임계 구역 **1개**, 그것도 **시작 전에 한 번**. PI 는 연쇄 블로킹으로 min(n, m) 개 (실측 PI 5 = 2 + 3 vs IPCP 3, 상한 7 vs 4).
- PI 는 역전은 묶어도 **교착은 못 막는다** (ABBA: plain·PI 모두 t4 교착). 교착 탐지는 "READY·출시 예정 없음 + BLOCKED 있음", 부분 교착은 대기 그래프 사이클.
- 대가: 자원과 무관한 중간 태스크도 막힌다 (천장 블로킹, M: IPCP 3 vs PI 0), 사용자 집합을 미리 알아야 한다, 천장이 낮으면 보장 붕괴(POSIX `EINVAL`).
- OPCP = 시스템 천장 규칙 + 상속 (같은 상한, 덜 막히지만 복잡). 실무 = IPCP: POSIX `PTHREAD_PRIO_PROTECT`, OSEK/AUTOSAR `GetResource`, Ada `Ceiling_Locking`. `PTHREAD_PRIO_INHERIT` = PI.
- FreeRTOS 에는 천장 뮤텍스가 없다 → `vTaskPrioritySet` 흉내 (기본값을 바꿈, time slicing 주의). 극단적 천장 = `vTaskSuspendAll` / `taskENTER_CRITICAL`.

## 참고자료

- L. Sha, R. Rajkumar, J. P. Lehoczky, "Priority Inheritance Protocols: An Approach to Real-Time Synchronization", *IEEE Transactions on Computers* 39(9), 1990 — PIP(min(n, m) 블로킹), PCP(임계 구역 1개, 교착 없음)
- T. P. Baker, "Stack-based scheduling of realtime processes", *Real-Time Systems* 3(1), 1991 — SRP, 스택 공유
- G. Buttazzo, *Hard Real-Time Computing Systems* (Springer) — 자원 접근 프로토콜 장 (NPP, PIP, PCP, SRP 비교)
- POSIX.1-2017 `pthread_mutexattr_getprotocol` / `setprotocol` (PRIO_NONE/INHERIT/PROTECT 정의), `pthread_mutex_lock` (EINVAL) — <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutexattr_getprotocol.html>, <https://pubs.opengroup.org/onlinepubs/9699919799/functions/pthread_mutex_lock.html>
- OSEK/VDX Operating System Specification 2.2.3 — Resource management, OSEK Priority Ceiling Protocol (문서 기준, 절 번호 미확인)
- Ada Reference Manual D.3 Priority Ceiling Locking — <https://www.adaic.org/resources/add_content/standards/12rm/html/RM-D-3.html>
- FreeRTOS-Kernel V11.3.2 소스 (직접 확인): tasks.c:6745 `xTaskPriorityInherit`, tasks.c:2844 `vTaskPrioritySet` (2949–2964 기본 우선순위 갱신), tasks.c:4964–4998 time slicing, include/FreeRTOS.h:218 `INCLUDE_vTaskPrioritySet`, :2771 `configUSE_TIME_SLICING` — <https://github.com/FreeRTOS/FreeRTOS-Kernel>
- Zephyr — Mutexes (priority inheritance) — <https://docs.zephyrproject.org/latest/kernel/services/synchronization/mutexes.html>
- 관련 레슨: RTOS-M03 (우선순위 역전 · Pathfinder), RTOS-M04 (상속 · 연쇄 상속), RTOS-B10 (뮤텍스 vs 세마포어), RTOS-M11 · RTOS-A04 (RTA 와 블로킹 항), RTOS-A03 (WCET), RTOS-A08 (SMP), RTOS-A10 (모터 제어 + 통신 + 로깅 설계), OS-M10 (교착 4조건), OS-M11 (mini lockdep), TOOL-M09 (TSan lock-order-inversion), C-M14 (PRIMASK/BASEPRI)
