# 🌐 네트워크 · 프로토콜 파싱 (Networking & Protocol Parsing) — Q49-Q58

> Connectivity 팀이 매일 만지는 바로 그 영역. 셀룰러 모뎀(AT 커맨드), IP 주소·서브넷,
> 시리얼 링크 위 프레이밍, 체크섬. 실제 후기에서 보고된 코딩 문제가 문자 그대로
> **"Extract IP Addresses"** 였고, 라운드 하나는 **"제품을 연결시키고 트러블슈팅하라"** 였다.
> 전부 소켓 없이 **버퍼 위의 순수 함수**로 쪼갤 수 있다 — 그래서 유닛 테스트가 가능하다.

---

## 1. 핵심 아이디어

이 세트를 한 문장으로: **"바이트 스트림을 의미 있는 구조로 바꾸되, 입력이 잘리거나
악의적이거나 말이 안 될 때도 안전하게 실패하라."**

인터뷰어가 보는 것은 파싱 알고리즘이 아니라 **실패 경로**다.

| 축 | 질문 | 이 세트에서 |
|---|---|---|
| **엄격함** | "01.2.3.4 는 받나요?" | Q49 — 선행 0 = 8진 해석 = ACL 우회 취약점 |
| **버퍼 안전** | "출력 버퍼가 작으면?" | Q50, Q54 — 잘라 쓰지 말고 -1, 항상 널 종료 |
| **부분 입력** | "read()가 절반만 줬다면?" | Q55, Q57 — 상태를 들고 재개, 오프셋 전진 금지 |
| **적대적 입력** | "len 필드에 0xFFFF가 오면?" | Q57 — 정책 상한, size_t 오버플로 회피 |
| **모름 vs 나쁨** | "+CSQ 99는 최악인가요?" | Q56 — UNKNOWN ≠ POOR (틀리면 SIM 무한 스왑) |

---

## 2. IPv4 / CIDR 비트 연산 정리

### 2.1 dotted-quad ↔ uint32

```c
// "a.b.c.d" -> (a<<24)|(b<<16)|(c<<8)|d   (host order 정수, 상위 = 첫 옥텟)
ip = (ip << 8) | octet;     // 옥텟마다 8비트 밀어넣기
// 역변환
uint8_t a = (ip >> 24) & 0xFF;  // 이하 16, 8, 0
```

### 2.2 프리픽스 마스크 — **/0 이 함정이다**

```c
uint32_t cidr_mask(unsigned p) {
    if (p == 0)  return 0u;              // 0xFFFFFFFF << 32 는 UB!
    if (p >= 32) return 0xFFFFFFFFu;
    return 0xFFFFFFFFu << (32 - p);
}
```

> C 표준: 시프트량이 피연산자 비트폭 이상이면 **정의되지 않은 동작**. x86에서는
> 시프트량이 32로 mod 되어 `0xFFFFFFFF` 가 나오고(= 완전히 틀린 답), ARM에서는 0이
> 나오는 식으로 아키텍처마다 다르다. **면접에서 이걸 먼저 언급하면 즉시 가점이다.**

### 2.3 서브넷 계산 치트시트

| prefix | mask | 호스트 수 | 흔한 용도 |
|---|---|---|---|
| /8  | 255.0.0.0 | 16,777,214 | `10.0.0.0/8` 사설 |
| /10 | 255.192.0.0 | 4,194,302 | `100.64.0.0/10` **CGNAT** |
| /12 | 255.240.0.0 | 1,048,574 | `172.16.0.0/12` 사설 |
| /16 | 255.255.0.0 | 65,534 | `192.168.0.0/16` 사설, `169.254.0.0/16` link-local |
| /24 | 255.255.255.0 | 254 | 전형적 LAN 세그먼트 |
| /31 | 255.255.255.254 | 2 | 포인트-투-포인트 링크 (RFC 3021) |
| /32 | 255.255.255.255 | 1 | 호스트 라우트 |

```c
uint32_t net       = ip & mask;
uint32_t broadcast = ip | ~mask;
uint32_t hosts     = (p >= 31) ? (1u << (32 - p)) : (1u << (32 - p)) - 2;  // net/bcast 제외
bool     same_net  = ((a ^ b) & mask) == 0;   // XOR 트릭: 상위 p 비트가 같은가
```

### 2.4 알아둘 특수 대역

- `0.0.0.0/8` — "this network" / wildcard bind
- `127.0.0.0/8` — loopback
- `169.254.0.0/16` — **link-local (APIPA)**. DHCP 실패의 지문. 게이트웨이 트러블슈팅에서
  "IP가 169.254.x.x면 DHCP 서버를 못 찾은 것"은 반드시 말할 수 있어야 한다.
- `224.0.0.0/4` — multicast (mDNS `224.0.0.251`, 카메라 디스커버리)
- `100.64.0.0/10` — **CGNAT**. 캐리어가 주는 이 주소는 공인 IP가 아니다.

---

## 3. 엔디안 (htonl / ntohl)

**네트워크 바이트 오더 = 빅엔디안.** 호스트(x86, ARM 리틀엔디안)와 다르다.

```c
uint32_t net_order = htonl(host_order);   // host-to-network long  (32비트)
uint16_t net_port  = htons(1883);         // host-to-network short (16비트)
uint32_t host      = ntohl(net_order);
```

| | 리틀엔디안 호스트 | 빅엔디안(네트워크) |
|---|---|---|
| `0x0A000007` 메모리 배치 | `07 00 00 0A` | `0A 00 00 07` |
| `htonl` 이 하는 일 | 바이트 스왑 | no-op |

### 실무에서 중요한 세 가지

1. **`htonl` 은 "빅엔디안으로 바꿔라"가 아니라 "네트워크 오더로 바꿔라"다.** 빅엔디안
   머신에서는 아무것도 안 한다. 그래서 `htonl` 을 쓴 코드는 이식 가능하다.
2. **직접 조립하면 htonl 이 필요 없다.** Q53 의 체크섬과 Q57 의 길이 필드는
   `(p[0] << 8) | p[1]` 로 바이트에서 값을 만든다 — **호스트 엔디안과 완전히 무관**하다.
   임베디드 프로토콜 코드는 이 스타일이 안전하다(구조체 캐스팅은 정렬·패딩 문제까지 딸려온다).
3. **구조체를 패킷에 그대로 캐스팅하지 마라.** 패딩, 정렬(unaligned access가 ARM에서
   fault), 엔디안 세 가지가 동시에 터진다. `memcpy` + 명시적 바이트 조립이 정답.

```c
// 나쁨: 정렬/패딩/엔디안 세 겹의 지뢰
struct hdr *h = (struct hdr *)buf;      // 패딩·정렬·엔디안이 한꺼번에 터진다
// 좋음: 명시적, 이식 가능, 정렬 무관
uint16_t len = ((uint16_t)buf[1] << 8) | buf[2];
```

---

## 4. 인터넷 체크섬 (RFC 1071) 원리

**16비트 1의 보수 합의 1의 보수.**

```c
uint32_t sum = 0;
while (len > 1) { sum += (p[0] << 8) | p[1]; p += 2; len -= 2; }
if (len == 1)     sum += p[0] << 8;                 // 홀수 = 상위 바이트로 패딩
while (sum >> 16) sum = (sum & 0xFFFF) + (sum >> 16);  // 캐리 fold
return ~sum & 0xFFFF;
```

### 왜 이렇게 생겼나

- **1의 보수 합**은 캐리를 버리지 않고 하위로 되돌린다(end-around carry). 덕분에
  **덧셈 순서·워드 경계와 무관**하다 → 라우터가 헤더 한 필드(TTL)만 바꿔도
  전체 재계산 없이 **증분 갱신**이 가능하다 (RFC 1624).
- **검증 성질**: 체크섬 필드에 결과를 넣고 다시 계산하면 **0** 이 나온다.
  수신측은 `checksum(buf) == 0` 한 줄이면 끝 — 이게 이 알고리즘의 존재 이유다.
- 결과가 0x0000 이면 UDP에서는 0xFFFF 로 전송한다(0 = "체크섬 미사용" 의미이므로).

### 검출력 비교 — 면접에서 물어보는 지점

| 방식 | 비용 | 검출 못 하는 것 |
|---|---|---|
| XOR (NMEA) | 가장 쌈 | 짝수 개 비트 뒤집힘, 바이트 자리바꿈 |
| **Internet checksum** | 쌈, 증분 갱신 가능 | **워드 자리바꿈(transposition)**, 상쇄되는 오류쌍 |
| CRC-16/32 | LUT 256엔트리 | 버스트 오류에 강함 — 링크 계층이 쓰는 이유 |
| SHA/HMAC | 비쌈 | 위·변조 방어 (체크섬은 **보안이 아니다**) |

> 말할 문장: "체크섬은 **우발적 손상** 탐지용이지 위변조 방어가 아닙니다. 링크 계층 CRC와
> TLS가 각각 다른 층에서 다른 일을 합니다."

---

## 5. AT 커맨드 치트시트

모뎀은 CRLF 로 감싼 텍스트로 대답한다: `<CR><LF>응답<CR><LF>` 그리고 최종 결과
`OK` 또는 `ERROR`. **응답과 최종 결과는 별개의 줄**이라는 게 파서 설계의 출발점이다.

| 커맨드 | 뜻 | 응답 예 | 해석 |
|---|---|---|---|
| `AT` | 살아있나 | `OK` | 기본 헬스체크 |
| `ATE0` | 에코 끄기 | `OK` | **파서 첫 줄에 명령 에코가 섞이는 원인** |
| `AT+CPIN?` | SIM 상태 | `+CPIN: READY` | `SIM PIN` = PIN 필요, `ERROR` = SIM 없음 |
| `AT+CSQ` | 신호 품질 | `+CSQ: 23,99` | rssi 0~31(99=모름), ber 0~7(99=모름) |
| `AT+CREG?` | 2G/3G 등록 | `+CREG: 0,5` | `<n>,<stat>` |
| `AT+CEREG?` | **LTE 등록** | `+CEREG: 0,1` | LTE는 CEREG 를 봐야 한다 |
| `AT+COPS?` | 접속 사업자 | `+COPS: 0,0,"T-Mobile",7` | 마지막 = AcT(7 = E-UTRAN/LTE) |
| `AT+COPS=?` | 사업자 스캔 | 리스트 | **수십 초 블록** — 타임아웃 넉넉히 |
| `AT+CGDCONT?` | PDP 컨텍스트 | `+CGDCONT: 1,"IP","apn.carrier.com"` | APN 설정 확인 |
| `AT+CGDCONT=1,"IP","apn"` | APN 설정 | `OK` | 잘못된 APN = 등록은 되는데 데이터 안 됨 |
| `AT+CGPADDR=1` | 할당된 IP | `+CGPADDR: 1,"100.83.12.7"` | `100.64/10` 이면 **CGNAT** |
| `AT+CFUN=1,1` | 모뎀 리셋 | `OK` 후 재부팅 | 최후의 수단 |

### `<stat>` 값 (CREG/CEREG) — 외워둘 것

| 값 | 의미 |
|---|---|
| 0 | 미등록, 검색 안 함 |
| **1** | **등록됨, 홈 네트워크** ✅ |
| 2 | 미등록, **검색 중** (여기서 오래 머물면 안테나/대역 문제) |
| 3 | **등록 거부** (SIM/가입 문제 — 재시도해도 소용없다) |
| 4 | 알 수 없음 |
| **5** | **등록됨, 로밍** ✅ |

### CSQ → dBm

```
dBm = -113 + 2 * rssi        // rssi 0..31
  0 -> -113 dBm  (최악)
 10 ->  -93 dBm
 23 ->  -67 dBm  (좋음)
 31 ->  -51 dBm  (최상)
 99 -> 측정 불가 (안테나 미연결 / 검색 중)
```

LTE에서는 RSSI 대신 **RSRP**(-140 ~ -44 dBm, 셀 참조신호 세기)와
**RSRQ**(-20 ~ -3 dB, 품질/간섭)를 본다. 전파가 세도(RSRP 좋음) 간섭이 심하면
(RSRQ 나쁨) 처리량이 바닥난다 — **"막대기 꽉 찼는데 느려요"의 정체**다.

> **99를 -113으로 취급하는 버그**가 실무 단골이다. GC31-E 듀얼 SIM failover가
> "모름"을 "최악"으로 읽으면 부팅 직후 매번 SIM을 스왑하고, 스왑하면 다시 모름이 되어
> **무한 루프**에 빠진다. UNKNOWN은 별도 상태여야 하고, 스왑 판단에는 **히스테리시스**
> (예: "3회 연속 POOR일 때만 스왑, 복귀는 GOOD 이상일 때")가 필요하다.

---

## 6. 프레이밍 방식 비교

시리얼/TCP 는 **스트림**이라 "메시지 경계"가 없다. 경계를 만드는 방법은 셋뿐이다.

| 방식 | 오버헤드 | 장점 | 단점 |
|---|---|---|---|
| **길이 접두 (TLV)** | 고정 2~4B | 파싱 O(1), 미리 할당 가능, 페이로드 투명 | **길이 필드가 깨지면 스트림 전체가 미아**. 재동기 어려움 |
| **구분자 (CRLF, `\0`)** | 1~2B | 사람이 읽기 쉬움, 재동기 쉬움 | 페이로드에 구분자가 못 들어감 → 이스케이프 필요 |
| **SLIP** (RFC 1055) | 최악 2n+2 | 구현 10줄, 상태 없음, 재동기 즉시 | 크기 최대 2배, 길이를 미리 모름 |
| **COBS** | **최악 n/254+1** | 0x00 을 완전히 제거, 오버헤드 상한 보장 | 인코딩이 덜 직관적, 전체 블록 필요 |
| **HDLC/PPP** | 프레임당 ~6B | CRC + 주소/제어, 검증된 표준 | 무겁다 |

### 언제 뭘 쓰나 (면접 답변 템플릿)

- **TLV**: 메시지가 크고 링크가 신뢰할 만할 때(TCP 위, 내부 IPC). 반드시
  **max_len 정책 상한**과 **매직/sync 워드**를 같이 둬서 깨졌을 때 재동기 가능하게.
- **SLIP/COBS**: 잡음 있는 raw UART. **재동기 가능성**이 길이 효율보다 중요할 때.
  COBS는 오버헤드 상한이 0.4%로 보장되어 SLIP보다 우월 — 최신 설계의 기본값.
- **CRLF 구분자**: AT 커맨드처럼 상대가 정해놨을 때. 선택의 여지가 없다.

```
TLV : [type][len_hi][len_lo][payload...]
SLIP: C0 ...payload(C0->DB DC, DB->DB DD)... C0
COBS: [code][data...] 반복, 0x00 이 절대 안 나오므로 0x00 이 프레임 구분자
```

### 스트림 디코더의 불변식 3가지 (Q57의 핵심)

1. 바이트가 모자라면 **오프셋을 전진시키지 않는다** — 호출자가 남은 꼬리를 보관해
   다음 `read()` 와 이어붙일 수 있어야 한다.
2. **길이 상한을 정책으로 강제한다** — 0xFFFF 를 그대로 믿으면 64KB 할당 또는
   영원한 대기. 이건 DoS 벡터다.
3. **`off + hdr + len` 을 더해서 비교하지 않는다** — `size_t` 오버플로. 항상
   `avail - hdr < len` 처럼 **뺄셈**으로 비교한다.

---

## 7. MTU / PMTUD / CGNAT — 연결 트러블슈팅 개념

### MTU

- 이더넷 기본 **1500B**. LTE 는 보통 **1430~1500**, 캐리어/터널마다 다르다.
- 캡슐화가 MTU를 깎는다: IPsec −~60B, WireGuard −60B, PPPoE −8B, GRE −24B.
- MTU 초과 패킷은 **단편화(fragment)** 되거나, DF 비트가 있으면 폐기되고
  **ICMP Type 3 Code 4 "Fragmentation Needed"** 가 돌아온다.

### PMTUD 블랙홀 — 필드 현장 단골 증상

> "핑은 되는데 큰 파일 전송/TLS 핸드셰이크만 멈춘다."

원인: 중간 방화벽이 ICMP를 막아 "Fragmentation Needed"가 송신자에게 도달하지 못함.
송신자는 계속 큰 패킷을 보내고, 그것들은 조용히 사라진다.
대응: **MSS clamping**(`MSS = MTU − 40`), 인터페이스 MTU 하향(1400 등),
PMTUD 대신 **PLPMTUD**(RFC 4821) 사용.

```
증상 매트릭스
  ping OK / DNS OK / 대용량 전송 hang   -> MTU 블랙홀
  ping OK / DNS 실패                    -> DNS 설정·UDP 53 차단
  IP가 169.254.x.x                      -> DHCP 실패 (link-local 폴백)
  IP가 100.64.x.x                       -> CGNAT — 인바운드 불가는 정상
  +CEREG: 0,2 에서 안 넘어감            -> 안테나/대역/커버리지
  +CEREG: 0,3                           -> SIM 가입/요금제 거부 (재시도 무의미)
  +CSQ: 99,99                           -> 안테나 미연결 또는 모뎀 검색 중
```

### CGNAT (Carrier-Grade NAT, 100.64.0.0/10)

캐리어가 공인 IPv4 부족으로 가입자에게 사설 대역을 준다.

- **인바운드 연결 불가** → 게이트웨이에 직접 SSH/HTTP 접속 불가.
- 그래서 Verkada 같은 클라우드 관리 제품은 **기기가 클라우드로 아웃바운드 연결을
  열어두고 유지**하는 구조(persistent outbound tunnel)를 쓴다. Command에서
  원격 power-cycle 이 가능한 이유가 이것이다.
- NAT 매핑은 **유휴 시 타임아웃**(보통 30초~5분)된다 → **keepalive** 필수.
  keepalive가 없으면 "클라우드에는 연결됐다고 뜨는데 명령이 안 내려간다".
- 인터뷰에서: "오프라인 30분이면 자가 재부팅"(GW31-E)은 이 계열 문제의
  최후 방어선이다. 그 앞에 **키프얼라이브 + 지수 백오프 재연결 + 링크 품질 로깅**이 있어야 한다.

---

## 8. 흔한 함정 (인터뷰 감점 포인트)

| 함정 | 무슨 일이 나나 |
|---|---|
| `0xFFFFFFFF << 32` | **UB**. /0 CIDR 에서 아키텍처마다 다른 답 |
| `atoi`/`sscanf` 로 IP 파싱 | 선행 0·범위·꼬리 쓰레기를 전부 통과시킴 |
| `inet_addr` 사용 | 실패값이 `(in_addr_t)-1` = 유효 주소 255.255.255.255 와 구분 불가. `inet_pton` 을 쓸 것 |
| 선행 0 허용 | 8진 해석 → `010.1.1.1` 이 8.1.1.1 로. SSRF/ACL 우회 |
| `strcpy`/`sprintf` 로 IP 포맷 | 오버플로. 최소 **16바이트**(`INET_ADDRSTRLEN`) 필요 |
| 부분 기록 후 실패 반환 | 호출자가 잘린 문자열을 쓴다. 로컬에 만들고 크기 확인 후 복사 |
| 줄 단위 파싱을 `read()` 단위로 가정 | UART는 아무데서나 자른다. **상태 있는 FSM**이 답 |
| 과도한 줄에서 버퍼 오버런 | 넘치면 플래그만 세우고 버린 뒤 **다음 종결자에서 복구** |
| `len` 필드를 그대로 믿음 | 0xFFFF → 자원 고갈. **정책 상한 필수** |
| `off + 3 + len` 비교 | `size_t` 오버플로 → 경계 검사 통과. 뺄셈으로 비교 |
| NEED_MORE 인데 off 전진 | 데이터 유실. 스트림 재조립이 영영 깨짐 |
| `+CSQ: 99` 를 -113dBm 으로 | UNKNOWN ≠ POOR. SIM failover 무한 루프 |
| 히스테리시스 없는 failover | 경계값 근처에서 SIM 플래핑 |
| 체크섬을 보안으로 착각 | 체크섬은 **우발적 손상** 탐지용 |
| 패킷 버퍼를 구조체로 캐스팅 | 패딩 + 정렬(ARM fault) + 엔디안 3중 지뢰 |
| `ctype.h` 에 `char` 직접 전달 | 음수 char → UB. `(unsigned char)` 캐스팅 또는 직접 구현 |

---

## 9. 면접에서 말할 것 (한국어 + 영어)

### 파서의 엄격함

- 🇰🇷 "IPv4 파서는 엄격해야 합니다. 선행 0을 허용하면 `010.1.1.1` 이 8진수로 8.1.1.1 이
  되어 ACL 우회에 쓰입니다. 그래서 `inet_addr` 이 아니라 `inet_pton` 을 쓰거나 직접 짭니다."
- 🇺🇸 "The IPv4 parser has to be strict. If you allow leading zeros, `010.1.1.1` can be
  read as octal and become 8.1.1.1 — that's a real ACL-bypass vector. That's why I use
  `inet_pton`, never `inet_addr`, or hand-roll it."

### /0 시프트 UB

- 🇰🇷 "prefix 0은 특수 처리합니다. 32비트 값을 32비트 시프트하는 건 C에서 정의되지 않은
  동작이라 x86과 ARM이 다른 답을 냅니다."
- 🇺🇸 "I special-case a prefix length of zero — shifting a 32-bit value by 32 is undefined
  behavior in C, and x86 and ARM genuinely disagree on the result."

### 스트림 디코더

- 🇰🇷 "UART 읽기는 메시지 경계를 존중하지 않습니다. 그래서 파서를 호출 사이에 상태를
  유지하는 FSM으로 만들고, 바이트가 모자라면 오프셋을 전진시키지 않고 NEED_MORE 를
  돌려줍니다 — 호출자가 남은 꼬리를 다음 읽기와 이어붙일 수 있도록요."
- 🇺🇸 "A UART read doesn't respect message boundaries, so the parser is a state machine
  that survives across calls. When the buffer is short I return NEED_MORE *without*
  advancing the offset, so the caller can keep the tail and concatenate the next read."

### 적대적 입력

- 🇰🇷 "길이 필드는 신뢰하지 않습니다. 정책 상한을 넘으면 프레임을 버리고 재동기화합니다.
  그리고 `off + hdr + len` 처럼 더해서 비교하지 않습니다 — size_t 오버플로로 경계 검사를
  통과해버리니까요. 항상 뺄셈으로 비교합니다."
- 🇺🇸 "I never trust a length field. If it exceeds the policy cap I drop the frame and
  resync. And I compare with subtraction, never `off + hdr + len` — that addition can
  wrap a size_t and sail straight past the bounds check."

### 체크섬

- 🇰🇷 "인터넷 체크섬은 1의 보수 합이라 캐리를 되돌려 넣습니다. 그래서 덧셈 순서에
  무관하고, 라우터가 TTL만 바꿔도 증분 갱신이 가능합니다. 수신측은 체크섬 필드를 포함해
  다시 계산해서 0이 나오는지만 보면 됩니다."
- 🇺🇸 "The Internet checksum is a one's-complement sum, so carries wrap around. That makes
  it order-independent and lets a router update it incrementally when it just decrements
  the TTL. The receiver only has to recompute over the whole thing and check it comes out zero."

### 신호 품질 / failover

- 🇰🇷 "`+CSQ: 99` 는 최악이 아니라 '모름'입니다. 이걸 -113dBm 으로 매핑하면 듀얼 SIM
  failover가 부팅마다 SIM을 스왑하고, 스왑하면 다시 모름이 되어 무한 루프에 빠집니다.
  UNKNOWN 은 별도 상태로 두고, 스왑에는 N회 연속 조건과 히스테리시스를 겁니다."
- 🇺🇸 "`+CSQ: 99` means *unknown*, not *worst*. Map it to -113 dBm and your dual-SIM
  failover swaps on every boot, and the swap makes it unknown again — an infinite loop.
  I keep UNKNOWN as its own state and gate the swap behind N consecutive poor samples
  plus hysteresis on the way back."

### 트러블슈팅 스토리텔링

- 🇰🇷 "핑은 되는데 큰 전송만 멈추면 거의 항상 MTU 블랙홀입니다. 중간 방화벽이 ICMP
  'Fragmentation Needed' 를 막아 PMTUD가 실패한 거고, MSS clamping 이나 MTU 하향으로 뚫습니다."
- 🇺🇸 "Ping works but large transfers hang — that's almost always an MTU black hole.
  A middlebox is dropping the ICMP 'fragmentation needed', so PMTUD never completes.
  I fix it with MSS clamping or by lowering the interface MTU."

- 🇰🇷 "기기 IP가 100.64.x.x 면 CGNAT 입니다. 인바운드가 원천적으로 불가하니 기기가
  클라우드로 아웃바운드 터널을 열어두고 keepalive 로 NAT 매핑을 살려둬야 합니다."
- 🇺🇸 "If the device gets a 100.64.x.x address it's behind carrier-grade NAT — inbound is
  simply impossible. The device has to hold an outbound tunnel open and keepalive it,
  or the NAT mapping ages out and the cloud thinks it's connected when it isn't."

### 테스트 가능성 (모듈성 — 리크루터 메일 4번 항목)

- 🇰🇷 "이 로직은 전부 소켓 없이 버퍼 위의 순수 함수로 쪼갰습니다. 그래서 모뎀 하드웨어
  없이도 잘린 입력, 쓰레기 바이트, 악의적 길이를 유닛 테스트로 재현할 수 있습니다.
  I/O는 가장자리에만 둡니다."
- 🇺🇸 "All of this is pure functions over buffers — no sockets. That means I can unit-test
  truncated input, garbage bytes and hostile length fields without any modem hardware.
  I/O stays at the edges."

---

## 10. 체크리스트

- [ ] `ipv4_parse` 를 보지 않고 10분 안에, 선행 0/범위/점 개수/꼬리 쓰레기 전부 막고 작성
- [ ] `cidr_mask(0)` 이 왜 특수 처리가 필요한지 UB 용어로 설명
- [ ] 사설 대역 3개 + CGNAT + link-local 대역을 prefix 까지 암송
- [ ] "Extract IP Addresses" 를 경계/오탐 규칙까지 설명하며 화이트보드에 작성
- [ ] RFC 1071 체크섬을 fold 포함해 작성하고 "검증하면 0" 성질을 증명
- [ ] `htonl` 을 쓰는 코드와 바이트 직접 조립 코드의 차이를 설명
- [ ] AT: `CPIN?` → `CSQ` → `CEREG?` → `COPS?` → `CGDCONT?` → `CGPADDR` 순서의
      트러블슈팅 시퀀스를 이유와 함께 말하기
- [ ] `<stat>` 1/2/3/5 의 의미와 각각의 대응 조치
- [ ] CSQ 0/23/31/99 를 dBm 으로 즉답, 99가 UNKNOWN 인 이유 설명
- [ ] 길이접두 vs 구분자 vs SLIP vs COBS 의 오버헤드와 재동기 특성 비교
- [ ] 스트림 디코더 불변식 3개(off 불변 / 길이 상한 / 뺄셈 비교) 암송
- [ ] SLIP 인코드/디코드를 5분 안에 작성, 최악 크기 2n+2 설명
- [ ] MTU 블랙홀 증상 → 원인 → 조치를 30초 안에 설명
- [ ] CGNAT 에서 왜 아웃바운드 터널 + keepalive 구조가 되는지 설명
- [ ] 위 모든 로직이 하드웨어 없이 테스트 가능한 이유를 "I/O는 가장자리에" 로 요약
