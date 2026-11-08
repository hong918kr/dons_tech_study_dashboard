# 웹 서비스화 계획표: 임베디드 스터디 노트 + 브라우저 에디터 (2026-11-08)

**아이디어:** 지금 맥/Lima/QEMU로 하는 실습 환경을 서버에 올리고, 스터디 노트를 인터넷에서 서비스한다. 문제를 읽다가 **브라우저 에디터에서 바로 코드를 쓰고 실행**한다.

결론부터: **가능하다.** 다만 "읽기 사이트"와 "코드 실행 서비스"는 난이도와 비용이 완전히 다르므로, 단계를 쪼개서 가야 한다. 코드 실행은 남의 코드를 내 서버에서 돌리는 일이라 **보안·남용 방지**가 비용의 대부분이다.

> **쓰는 법**
> - 단계(Phase)는 앞 단계만으로도 가치가 있게 나눴다. 중간에 멈춰도 손해가 없다.
> - 맨 아래 **결정이 필요한 것** 4가지에 답하면 Phase 0~1을 바로 시작할 수 있다.
> - 가격은 2026-11 기준 대략값이다. 결제 전에 각 업체 페이지에서 확인한다.

## 0. 먼저 못 박을 것: 무엇을 공개해도 되는가

서비스화에서 **가장 먼저 걸리는 건 기술이 아니라 저작권**이다.

| 대상 | 공개 가능? | 이유 |
|---|---|---|
| `embedded-study-note/` 의 커리큘럼·레슨 (직접 작성한 문제와 설명) | ✅ 가능 | Don이 만든 콘텐츠. 단 외부 책/스펙 인용은 짧게, 출처 표기 |
| AlgoMonster 노트, 퀀트 코스 노트 | ❌ 불가 | 유료 강의를 옮긴 한국어 노트. 공개하면 저작권 침해 |
| OSTEP book-md | △ 확인 필요 | OSTEP은 무료 공개지만 라이선스 조건 확인 |
| 면접 준비물 (회사별 context, 실제 면접 문제, 이력서) | ❌ 불가 | 개인정보 + 회사 면접 문제 유출 문제 |
| 논문 노트 | △ | 요약은 괜찮지만 원문 그림/표 복제는 피함 |

→ **공개 대상은 `embedded-study-note/` 하나로 한정**한다 (Don 확정, 2026-11-08). 레슨을 쓸 때부터 유료 강의 문장을 옮기지 않는 것을 규칙으로 하고, 레슨 안에 유료 강의에서 가져온 문장이 없는지 한 번 훑는다 (Phase 0 작업).

## 1. 아키텍처 선택지

| 방식 | 구조 | 장점 | 단점 | 적합한 env |
|---|---|---|---|---|
| **A. 브라우저 안에서 실행 (WASM)** | Monaco 에디터 + Pyodide(Python) + C는 WASM clang | 서버 비용 0, 보안 위험 0, 무한 확장 | QEMU·Linux·Verilator는 사실상 불가, 속도·기능 제한 | `mac`의 C/Python 상당수 |
| **B. 서버에서 실행 (샌드박스)** | 브라우저 → WebSocket → 컨테이너 안 컴파일·실행 | 진짜 clang/QEMU/Verilator, 맥과 동일한 결과 | 보안·비용·운영 부담이 큼 | `mac`, `qemu`, `fpga`, 일부 `linux` |
| **C. 하이브리드 (권장)** | 가벼운 건 A, 무거운 건 B | 비용 대비 효과 최고 | 두 경로를 다 만들어야 함 | 전부 |
| **D. 개인 원격 개발환경** | code-server(브라우저 속 VS Code)를 내 VPS에 두고 **나만** 접속 | 남용 걱정 없음, 가장 빠르게 가능 | 서비스가 아니라 내 도구 | 전부 (본인용) |

권장: **D로 먼저 환경을 서버에 올려보고 → A로 공개 플레이그라운드 → 필요하면 B**.

env별로 어디서 돌릴 수 있는지:

| env | 아이템 | A. 브라우저 WASM | B. 서버 컨테이너 | 비고 |
|---|---|---|---|---|
| `mac` | 1940 | 일부 (순수 C/Python 로직, mock 레지스터) | ✅ 대부분 | ASan/UBSan, 맥 전용 API, 포인터 폭 차이 주의 |
| `qemu` | 69 | 어려움 (qemu-wasm 실험적) | ✅ | 서버에 `qemu-system-*` + 크로스 툴체인 |
| `fpga` | 41 | 어려움 | ✅ | verilator/iverilog/cocotb는 컨테이너에서 쉬움 |
| `linux` | 133 | ❌ | △ | 컨테이너는 커널을 공유해 **커널 모듈·`/sys`·perf 항목은 안 됨** → 전용 microVM 또는 QEMU 안의 리눅스 필요 |
| `board` `gpu` `lab` | 8 | ❌ | ❌ | 하드웨어 필요 → 서비스 범위에서 제외하고 "로컬 가이드"로 안내 |

즉, 현실적인 서비스 범위는 **약 2050개(`mac`+`qemu`+`fpga`)**이고 `linux`는 후순위, 하드웨어 8개는 제외다.

## 2. 시스템 그림

그림은 4장이다. 위에서 아래로 "전체 구조 → 실행 한 번의 흐름 → 단계별 성장 → 콘텐츠가 사이트가 되는 길" 순서다.

### 2-1. 전체 구조 (최종 형태, Phase 3 이후)

점선 박스가 한 덩어리의 책임이다. ①에서 시작해 ②를 거쳐 ④까지 간다. 읽기만 하는 사용자는 ②의 Pages에서 끝나고 서버 비용이 들지 않는다.

```svg
<svg viewBox="0 0 920 560" xmlns="http://www.w3.org/2000/svg" role="img"><defs><marker id="a1" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect x="10" y="10" width="215" height="520" rx="12" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 4" opacity=".6"/>
<text x="22" y="28" font-size="12" font-weight="700" opacity=".8">① 사용자 브라우저</text>
<rect class="f-acc" x="25" y="40" width="185" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="117.5" y="59" text-anchor="middle" font-size="13" font-weight="700">문제 페이지</text>
<text x="117.5" y="79" text-anchor="middle" font-size="11.5">정적 HTML (site/)</text>
<text x="117.5" y="96" text-anchor="middle" font-size="11.5">문제 · 설명 · 그림</text>
<rect class="f-acc" x="25" y="125" width="185" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="117.5" y="144" text-anchor="middle" font-size="13" font-weight="700">Monaco 에디터</text>
<text x="117.5" y="164" text-anchor="middle" font-size="11.5">starter.c 불러오기</text>
<text x="117.5" y="181" text-anchor="middle" font-size="11.5">▶ 실행 버튼</text>
<rect class="f-ok" x="25" y="210" width="185" height="85" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="117.5" y="229" text-anchor="middle" font-size="13" font-weight="700">WASM 실행 (경로 A)</text>
<text x="117.5" y="249" text-anchor="middle" font-size="11.5">Pyodide(Python)</text>
<text x="117.5" y="266" text-anchor="middle" font-size="11.5">WASM clang(C)</text>
<text x="117.5" y="283" text-anchor="middle" font-size="11.5">서버 없이 즉시</text>
<rect class="f-acc" x="25" y="310" width="185" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="117.5" y="329" text-anchor="middle" font-size="13" font-weight="700">xterm.js 출력창</text>
<text x="117.5" y="349" text-anchor="middle" font-size="11.5">컴파일 로그 · stdout</text>
<text x="117.5" y="366" text-anchor="middle" font-size="11.5">테스트 결과</text>
<rect class="f-mut" x="25" y="400" width="185" height="110" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="117.5" y="419" text-anchor="middle" font-size="13" font-weight="700">localStorage</text>
<text x="117.5" y="439" text-anchor="middle" font-size="11.5">작성 중인 코드</text>
<text x="117.5" y="456" text-anchor="middle" font-size="11.5">마지막 위치</text>
<text x="117.5" y="473" text-anchor="middle" font-size="11.5">(로그인 전 임시)</text>
<rect x="255" y="10" width="230" height="300" rx="12" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 4" opacity=".6"/>
<text x="267" y="28" font-size="12" font-weight="700" opacity=".8">② Cloudflare (무료 플랜)</text>
<rect class="f-warn" x="270" y="40" width="200" height="60" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="370.0" y="59" text-anchor="middle" font-size="13" font-weight="700">DNS · HTTPS · CDN</text>
<text x="370.0" y="79" text-anchor="middle" font-size="11.5">도메인 연결, 인증서 자동</text>
<rect class="f-ok" x="270" y="112" width="200" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="370.0" y="131" text-anchor="middle" font-size="13" font-weight="700">Pages (정적 호스팅)</text>
<text x="370.0" y="151" text-anchor="middle" font-size="11.5">site/ 배포 · git push 자동</text>
<text x="370.0" y="168" text-anchor="middle" font-size="11.5">읽기 트래픽은 여기서 끝</text>
<rect class="f-warn" x="270" y="194" width="200" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="370.0" y="213" text-anchor="middle" font-size="13" font-weight="700">Turnstile · 레이트 리밋</text>
<text x="370.0" y="233" text-anchor="middle" font-size="11.5">봇 차단, 요청 상한</text>
<text x="370.0" y="250" text-anchor="middle" font-size="11.5">(▶ 실행 앞에서 검사)</text>
<rect x="255" y="330" width="230" height="200" rx="12" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 4" opacity=".6"/>
<text x="267" y="348" font-size="12" font-weight="700" opacity=".8">③ 계정 · 진행도</text>
<rect class="f-acc" x="270" y="358" width="200" height="60" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="370.0" y="377" text-anchor="middle" font-size="13" font-weight="700">Auth</text>
<text x="370.0" y="397" text-anchor="middle" font-size="11.5">GitHub 로그인(OAuth)</text>
<rect class="f-acc" x="270" y="430" width="200" height="85" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="370.0" y="449" text-anchor="middle" font-size="13" font-weight="700">DB (Postgres)</text>
<text x="370.0" y="469" text-anchor="middle" font-size="11.5">푼 문제 체크 · 저장된 코드</text>
<text x="370.0" y="486" text-anchor="middle" font-size="11.5">실행 기록 · 사용량</text>
<rect x="530" y="10" width="380" height="520" rx="12" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 4" opacity=".6"/>
<text x="542" y="28" font-size="12" font-weight="700" opacity=".8">④ 실행 서버 (VPS 1대, arm64)</text>
<rect class="f-warn" x="550" y="40" width="340" height="60" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="720.0" y="59" text-anchor="middle" font-size="13" font-weight="700">API 게이트웨이</text>
<text x="720.0" y="79" text-anchor="middle" font-size="11.5">로그인 확인 · 일일 한도 · env 선택</text>
<rect class="f-mut" x="550" y="115" width="340" height="50" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="720.0" y="134" text-anchor="middle" font-size="13" font-weight="700">작업 큐</text>
<text x="720.0" y="154" text-anchor="middle" font-size="11.5">동시 실행 상한 · 순서 보장</text>
<rect x="545" y="180" width="350" height="335" rx="12" fill="none" stroke="currentColor" stroke-width="1" stroke-dasharray="5 4" opacity=".6"/>
<text x="557" y="198" font-size="12" font-weight="700" opacity=".8">샌드박스 워커 (gVisor, 네트워크 차단)</text>
<rect class="f-ok" x="560" y="205" width="160" height="95" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="640.0" y="224" text-anchor="middle" font-size="13" font-weight="700">mac 이미지</text>
<text x="640.0" y="244" text-anchor="middle" font-size="11.5">clang + ASan/UBSan</text>
<text x="640.0" y="261" text-anchor="middle" font-size="11.5">python3</text>
<rect class="f-ok" x="725" y="205" width="160" height="95" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="805.0" y="224" text-anchor="middle" font-size="13" font-weight="700">qemu 이미지</text>
<text x="805.0" y="244" text-anchor="middle" font-size="11.5">qemu-system-arm/riscv</text>
<text x="805.0" y="261" text-anchor="middle" font-size="11.5">arm-none-eabi-gcc</text>
<rect class="f-ok" x="560" y="315" width="160" height="95" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="640.0" y="334" text-anchor="middle" font-size="13" font-weight="700">fpga 이미지</text>
<text x="640.0" y="354" text-anchor="middle" font-size="11.5">verilator · iverilog</text>
<text x="640.0" y="371" text-anchor="middle" font-size="11.5">yosys · cocotb</text>
<rect class="f-mut" x="725" y="315" width="160" height="95" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="805.0" y="334" text-anchor="middle" font-size="13" font-weight="700">(후순위) linux</text>
<text x="805.0" y="354" text-anchor="middle" font-size="11.5">microVM / QEMU</text>
<text x="805.0" y="371" text-anchor="middle" font-size="11.5">커널 모듈 항목</text>
<text x="715" y="440" text-anchor="middle" font-size="11.5">제한: CPU 5~10초 · 메모리 256MB · pids 제한</text>
<text x="715" y="458" text-anchor="middle" font-size="11.5">읽기 전용 FS + tmpfs 작업 디렉터리</text>
<text x="715" y="476" text-anchor="middle" font-size="11.5">실행마다 새 컨테이너, 끝나면 폐기</text>
<text x="715" y="500" text-anchor="middle" font-size="11.5" font-weight="700">비밀키 없는 전용 서버 (다른 서비스와 분리)</text>
<line x1="210" y1="75" x2="270" y2="140" stroke="currentColor" stroke-width="1.4" marker-end="url(#a1)"/>
<line x1="210" y1="160" x2="270" y2="235" stroke="currentColor" stroke-width="1.4" marker-end="url(#a1)"/>
<path d="M470,245 H517 V55 H550" fill="none" stroke="currentColor" stroke-width="1.4" marker-end="url(#a1)"/>
<text x="510" y="150" text-anchor="middle" font-size="11" transform="rotate(-90 510 150)">통과한 요청</text>
<line x1="850" y1="102" x2="850" y2="115" stroke="currentColor" stroke-width="1.4" marker-end="url(#a1)"/>
<line x1="850" y1="165" x2="850" y2="205" stroke="currentColor" stroke-width="1.4" marker-end="url(#a1)"/>
<path d="M470,388 H497 V80 H550" fill="none" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4" marker-end="url(#a1)"/>
<path d="M470,472 H497 V388" fill="none" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4"/>
<text x="506" y="330" text-anchor="middle" font-size="11" transform="rotate(-90 506 330)">로그인·기록</text>
</svg>
```

**읽는 법:** 초록색은 비용이 거의 안 드는 부분, 노란색은 보안·남용 방어, 파란색은 사용자가 만지는 부분이다. 서버(④)는 "실행"만 담당하고, 계정·진행도(③)와 분리돼 있어 서버가 뚫려도 DB 비밀이 없다.

### 2-2. ▶ 실행 버튼을 눌렀을 때 (서버 경로 B)

```svg
<svg viewBox="0 0 920 440" xmlns="http://www.w3.org/2000/svg" role="img"><defs><marker id="a2" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect class="f-acc" x="30" y="10" width="120" height="30" rx="6" stroke="currentColor"/>
<text x="90" y="30" text-anchor="middle" font-size="12" font-weight="700">브라우저</text>
<line x1="90" y1="40" x2="90" y2="425" stroke="currentColor" stroke-width="1" stroke-dasharray="3 4" opacity=".5"/>
<rect class="f-acc" x="200" y="10" width="120" height="30" rx="6" stroke="currentColor"/>
<text x="260" y="30" text-anchor="middle" font-size="12" font-weight="700">Cloudflare</text>
<line x1="260" y1="40" x2="260" y2="425" stroke="currentColor" stroke-width="1" stroke-dasharray="3 4" opacity=".5"/>
<rect class="f-acc" x="390" y="10" width="120" height="30" rx="6" stroke="currentColor"/>
<text x="450" y="30" text-anchor="middle" font-size="12" font-weight="700">API 게이트웨이</text>
<line x1="450" y1="40" x2="450" y2="425" stroke="currentColor" stroke-width="1" stroke-dasharray="3 4" opacity=".5"/>
<rect class="f-acc" x="590" y="10" width="120" height="30" rx="6" stroke="currentColor"/>
<text x="650" y="30" text-anchor="middle" font-size="12" font-weight="700">샌드박스 워커</text>
<line x1="650" y1="40" x2="650" y2="425" stroke="currentColor" stroke-width="1" stroke-dasharray="3 4" opacity=".5"/>
<rect class="f-acc" x="770" y="10" width="120" height="30" rx="6" stroke="currentColor"/>
<text x="830" y="30" text-anchor="middle" font-size="12" font-weight="700">DB</text>
<line x1="830" y1="40" x2="830" y2="425" stroke="currentColor" stroke-width="1" stroke-dasharray="3 4" opacity=".5"/>
<text x="90" y="62" text-anchor="middle" font-size="11">① 코드 작성</text>
<line x1="90" y1="85" x2="260" y2="85" stroke="currentColor" stroke-width="1.4" marker-end="url(#a2)"/>
<text x="175.0" y="80" text-anchor="middle" font-size="11">② ▶ 실행 (코드 + 아이템 ID + 토큰)</text>
<text x="260" y="106" text-anchor="middle" font-size="11">Turnstile · 레이트 리밋</text>
<line x1="260" y1="128" x2="450" y2="128" stroke="currentColor" stroke-width="1.4" marker-end="url(#a2)"/>
<text x="355.0" y="123" text-anchor="middle" font-size="11">③ 통과한 요청만 전달</text>
<line x1="450" y1="152" x2="830" y2="152" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4" marker-end="url(#a2)"/>
<text x="640.0" y="147" text-anchor="middle" font-size="11">④ 로그인 · 오늘 한도 확인</text>
<line x1="830" y1="174" x2="450" y2="174" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4" marker-end="url(#a2)"/>
<text x="640.0" y="169" text-anchor="middle" font-size="11">⑤ OK</text>
<text x="450" y="198" text-anchor="middle" font-size="11">⑥ 큐에 넣고 빈 워커 대기</text>
<line x1="450" y1="222" x2="650" y2="222" stroke="currentColor" stroke-width="1.4" marker-end="url(#a2)"/>
<text x="550.0" y="217" text-anchor="middle" font-size="11">⑦ 새 컨테이너 시작 + 코드 전달</text>
<text x="650" y="246" text-anchor="middle" font-size="11">⑧ 컴파일 → 실행 → 테스트 하니스</text>
<text x="650" y="262" text-anchor="middle" font-size="11">(시간·메모리 초과 시 강제 종료)</text>
<line x1="650" y1="288" x2="450" y2="288" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4" marker-end="url(#a2)"/>
<text x="550.0" y="283" text-anchor="middle" font-size="11">⑨ stdout · 종료 코드 · 소요 시간</text>
<line x1="450" y1="314" x2="90" y2="314" stroke="currentColor" stroke-width="1.4" stroke-dasharray="5 4" marker-end="url(#a2)"/>
<text x="270.0" y="309" text-anchor="middle" font-size="11">⑩ 결과 스트림 (WebSocket)</text>
<line x1="450" y1="340" x2="830" y2="340" stroke="currentColor" stroke-width="1.4" marker-end="url(#a2)"/>
<text x="640.0" y="335" text-anchor="middle" font-size="11">⑪ 실행 기록 · 통과 여부 저장</text>
<text x="650" y="370" text-anchor="middle" font-size="11">⑫ 컨테이너 폐기</text>
<text x="90" y="400" text-anchor="middle" font-size="11">⑬ 통과 표시 + 다음 문제로</text>
</svg>
```

핵심은 ③에서 **봇·한도 검사를 통과한 요청만** 서버에 닿고, ⑦에서 **실행마다 새 컨테이너**를 쓴다는 점이다. 이전 사용자의 파일이나 프로세스가 남지 않는다. 경로 A(WASM)는 ②에서 끝나고 서버를 거치지 않는다.

### 2-3. 단계별 성장

```svg
<svg viewBox="0 0 920 330" xmlns="http://www.w3.org/2000/svg" role="img"><defs><marker id="a3" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect class="f-ok" x="10" y="30" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="95.0" y="49" text-anchor="middle" font-size="13" font-weight="700">Phase 0: 정적 공개</text>
<text x="95.0" y="69" text-anchor="middle" font-size="11.5">Cloudflare Pages</text>
<text x="95.0" y="86" text-anchor="middle" font-size="11.5">+ 도메인</text>
<text x="95" y="152" text-anchor="middle" font-size="12" font-weight="700">월 ~$1</text>
<rect class="f-ok" x="190" y="30" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="275.0" y="49" text-anchor="middle" font-size="13" font-weight="700">Phase 1: 개인 원격 환경</text>
<text x="275.0" y="69" text-anchor="middle" font-size="11.5">VPS + code-server</text>
<text x="275.0" y="86" text-anchor="middle" font-size="11.5">나만 접속</text>
<text x="275" y="152" text-anchor="middle" font-size="12" font-weight="700">+ €5~15</text>
<rect class="f-acc" x="370" y="30" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="455.0" y="49" text-anchor="middle" font-size="13" font-weight="700">Phase 2: 브라우저 실행</text>
<text x="455.0" y="69" text-anchor="middle" font-size="11.5">Monaco + WASM</text>
<text x="455.0" y="86" text-anchor="middle" font-size="11.5">서버 없이 실행</text>
<text x="455" y="152" text-anchor="middle" font-size="12" font-weight="700">+ $0</text>
<rect class="f-warn" x="550" y="30" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="635.0" y="49" text-anchor="middle" font-size="13" font-weight="700">Phase 3: 서버 샌드박스</text>
<text x="635.0" y="69" text-anchor="middle" font-size="11.5">qemu · verilator</text>
<text x="635.0" y="86" text-anchor="middle" font-size="11.5">큐 · 레이트 리밋</text>
<text x="635" y="152" text-anchor="middle" font-size="12" font-weight="700">+ €5~10</text>
<rect class="f-warn" x="730" y="30" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="815.0" y="49" text-anchor="middle" font-size="13" font-weight="700">Phase 4~5: 계정 · 공개 운영</text>
<text x="815.0" y="69" text-anchor="middle" font-size="11.5">로그인 · 진행도</text>
<text x="815.0" y="86" text-anchor="middle" font-size="11.5">약관 · 모니터링</text>
<text x="815" y="152" text-anchor="middle" font-size="12" font-weight="700">+ 소규모</text>
<line x1="180" y1="80" x2="200" y2="80" stroke="currentColor" stroke-width="1.4" marker-end="url(#a3)"/>
<line x1="360" y1="80" x2="380" y2="80" stroke="currentColor" stroke-width="1.4" marker-end="url(#a3)"/>
<line x1="540" y1="80" x2="560" y2="80" stroke="currentColor" stroke-width="1.4" marker-end="url(#a3)"/>
<line x1="720" y1="80" x2="740" y2="80" stroke="currentColor" stroke-width="1.4" marker-end="url(#a3)"/>
<text x="460" y="190" text-anchor="middle" font-size="12" font-weight="700">누적 구성 — 앞 단계는 뒤 단계에서도 그대로 쓰인다</text>
<rect class="f-mut" x="10" y="205" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="95.0" y="224" text-anchor="middle" font-size="13" font-weight="700">멈춰도 가치 있음</text>
<text x="95.0" y="244" text-anchor="middle" font-size="11.5">어디서든 읽는 사이트</text>
<rect class="f-mut" x="190" y="205" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="275.0" y="224" text-anchor="middle" font-size="13" font-weight="700">멈춰도 가치 있음</text>
<text x="275.0" y="244" text-anchor="middle" font-size="11.5">어디서든 실습하는 내 환경</text>
<rect class="f-mut" x="370" y="205" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="455.0" y="224" text-anchor="middle" font-size="13" font-weight="700">멈춰도 가치 있음</text>
<text x="455.0" y="244" text-anchor="middle" font-size="11.5">문제 옆에서 바로 실행</text>
<text x="455.0" y="261" text-anchor="middle" font-size="11.5">(공개 가능)</text>
<rect class="f-mut" x="550" y="205" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="635.0" y="224" text-anchor="middle" font-size="13" font-weight="700">사용자가 생기면</text>
<text x="635.0" y="244" text-anchor="middle" font-size="11.5">무거운 env 지원</text>
<text x="635.0" y="261" text-anchor="middle" font-size="11.5">보안 책임 시작</text>
<rect class="f-mut" x="730" y="205" width="170" height="100" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="815.0" y="224" text-anchor="middle" font-size="13" font-weight="700">공개 서비스</text>
<text x="815.0" y="244" text-anchor="middle" font-size="11.5">남용 대응 필요</text>
</svg>
```

### 2-4. 콘텐츠가 사이트가 되는 길 (기존 파이프라인 위에 올라탐)

```svg
<svg viewBox="0 0 920 330" xmlns="http://www.w3.org/2000/svg" role="img"><defs><marker id="a4" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs>
<rect class="f-mut" x="10" y="30" width="160" height="90" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="90.0" y="49" text-anchor="middle" font-size="13" font-weight="700">curriculum/</text>
<text x="90.0" y="69" text-anchor="middle" font-size="11.5">tracks/*.json</text>
<text x="90.0" y="86" text-anchor="middle" font-size="11.5">2191개 아이템</text>
<rect class="f-acc" x="200" y="30" width="160" height="90" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="280.0" y="49" text-anchor="middle" font-size="13" font-weight="700">레슨 작성</text>
<text x="280.0" y="69" text-anchor="middle" font-size="11.5">NOTES_SPEC 규칙</text>
<text x="280.0" y="86" text-anchor="middle" font-size="11.5">에이전트 병렬 작성</text>
<rect class="f-acc" x="390" y="30" width="170" height="90" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="475.0" y="49" text-anchor="middle" font-size="13" font-weight="700">lessons/<TRACK>/<ID>/</text>
<text x="475.0" y="69" text-anchor="middle" font-size="11.5">레슨.md + starter.* </text>
<text x="475.0" y="86" text-anchor="middle" font-size="11.5">solution.* + practice.*</text>
<rect class="f-ok" x="590" y="30" width="150" height="90" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="665.0" y="49" text-anchor="middle" font-size="13" font-weight="700">verify_lessons.py</text>
<text x="665.0" y="69" text-anchor="middle" font-size="11.5">컴파일 · 실행 검증</text>
<text x="665.0" y="86" text-anchor="middle" font-size="11.5">통과해야 공개</text>
<rect class="f-acc" x="770" y="30" width="140" height="90" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="840.0" y="49" text-anchor="middle" font-size="13" font-weight="700">build_site.py</text>
<text x="840.0" y="69" text-anchor="middle" font-size="11.5">site/ 생성</text>
<text x="840.0" y="86" text-anchor="middle" font-size="11.5">학습가능 표시</text>
<line x1="170" y1="75" x2="200" y2="75" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<line x1="360" y1="75" x2="390" y2="75" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<line x1="560" y1="75" x2="590" y2="75" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<line x1="740" y1="75" x2="770" y2="75" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<rect class="f-mut" x="770" y="170" width="140" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="840.0" y="189" text-anchor="middle" font-size="13" font-weight="700">git push</text>
<text x="840.0" y="209" text-anchor="middle" font-size="11.5">main 브랜치</text>
<line x1="840" y1="120" x2="840" y2="170" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<rect class="f-ok" x="590" y="170" width="150" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="665.0" y="189" text-anchor="middle" font-size="13" font-weight="700">Cloudflare Pages</text>
<text x="665.0" y="209" text-anchor="middle" font-size="11.5">자동 빌드 · 배포</text>
<line x1="770" y1="205" x2="740" y2="205" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<rect class="f-ok" x="390" y="170" width="170" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="475.0" y="189" text-anchor="middle" font-size="13" font-weight="700">공개 사이트</text>
<text x="475.0" y="209" text-anchor="middle" font-size="11.5">https://도메인</text>
<text x="475.0" y="226" text-anchor="middle" font-size="11.5">읽기 + ▶ 실행</text>
<line x1="590" y1="205" x2="560" y2="205" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<rect class="f-warn" x="390" y="265" width="170" height="50" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="475.0" y="284" text-anchor="middle" font-size="13" font-weight="700">실행 서버가 읽는 것</text>
<text x="475.0" y="304" text-anchor="middle" font-size="11.5">starter.* + 테스트 하네스</text>
<line x1="475" y1="240" x2="475" y2="265" stroke="currentColor" stroke-width="1.4" marker-end="url(#a4)"/>
<rect class="f-warn" x="10" y="170" width="360" height="70" rx="8" stroke="currentColor" stroke-width="1.2"/>
<text x="190.0" y="189" text-anchor="middle" font-size="13" font-weight="700">핵심 병목: 레슨 작성</text>
<text x="190.0" y="209" text-anchor="middle" font-size="11.5">2191개 중 92개만 존재 → 나머지가 "문제 생성" 액션 아이템</text>
</svg>
```

이미 있는 `curriculum → lessons → build_site.py` 흐름이 그대로 서비스의 뼈대다. 레슨 폴더의 `starter.*`와 `solution.*`에 테스트 하네스가 들어 있어서, 실행 서버는 **새로 만들 것 없이 이 파일을 그대로 실행**하면 된다. 즉 서비스화의 진짜 병목은 서버가 아니라 **레슨이 아직 92개뿐**이라는 점이다.

## 3. 필요한 구성요소와 비용

| 항목 | 선택지 | 대략 비용 | 비고 |
|---|---|---|---|
| **도메인** | Cloudflare Registrar / Porkbun / Namecheap | `.com` 연 $10~15, `.dev` 연 $12~15 | Cloudflare는 원가 판매. 이름 후보는 아래 |
| **정적 호스팅** | Cloudflare Pages / GitHub Pages / Netlify | **무료** | 지금 `site/`가 이미 정적 HTML이라 바로 올라감 |
| **DNS · CDN · HTTPS · DDoS** | Cloudflare (무료 플랜) | 무료 | 도메인을 Cloudflare에 두면 한 곳에서 해결 |
| **브라우저 에디터** | Monaco (VS Code 에디터 엔진) 또는 CodeMirror 6 | 무료 (MIT) | Monaco는 무겁지만 기능 풍부, 모바일은 CodeMirror가 낫다 |
| **브라우저 터미널** | xterm.js | 무료 | QEMU 콘솔·셸 출력용 |
| **코드 실행 서버 (B)** | VPS 1대 (arm64 권장: Hetzner CAX 계열 등) | 월 €5~15 정도 | 맥이 arm64라 이미지를 그대로 쓸 수 있음. x86도 가능 |
| **샌드박스** | Docker + gVisor, 또는 Firecracker microVM, 또는 `isolate` | 무료 | Judge0(오픈소스)는 `isolate` 기반으로 C/C++/Python 실행 API를 이미 제공 |
| **에디터+실행 통합 기성품** | code-server, Eclipse Theia, Judge0, Piston | 무료 | 바닥부터 만들기 전에 먼저 평가 |
| **로그인** | GitHub OAuth / Clerk / Supabase Auth | 소규모 무료 | 남용 방지와 진행도 저장에 필요 |
| **진행도 저장** | Supabase(Postgres) / Cloudflare D1 | 소규모 무료 | 푼 문제 체크, 작성한 코드 저장 |
| **봇·남용 방지** | Cloudflare Turnstile, 레이트 리밋 | 무료 | 실행 버튼 앞에 둔다 |
| **모니터링** | Uptime Kuma / Cloudflare Analytics | 무료 | 서버 죽으면 알림 |

**예상 월 비용:** Phase 0 = 도메인 연 $12 뿐. Phase 2 = 월 €5~15. 사용자가 늘어도 실행 서버만 늘리면 된다 (정적 사이트는 무료 유지).

### 도메인 이름 후보 (가용 여부는 구매 전 확인)
`embedded-lab.dev`, `siliconnotes.dev`, `firmwaredrill.com`, `donslab.dev`

→ 개인 브랜드로 가면 `dons-lab.dev`처럼 여러 프로젝트(스터디, 연습문제)를 하위 도메인으로 쓸 수 있다.

## 4. 보안: 서버 실행에서 반드시 막을 것

남의 코드를 컴파일하고 실행한다는 건 **원격 코드 실행을 일부러 열어두는 것**이다.

| 위협 | 대책 |
|---|---|
| 서버 탈취·호스트 탈출 | gVisor 또는 microVM으로 격리, 컨테이너 root 금지, 읽기 전용 파일시스템 + tmpfs 작업 디렉터리 |
| 무한 루프·메모리 폭탄 | CPU 시간 5~10초, 메모리 256MB, 프로세스 수·파일 크기 제한 (`isolate`/cgroup) |
| 암호화폐 채굴·스팸 | **네트워크 완전 차단** (egress 없음), 실행 시간 제한, 로그인 필수 |
| 포크 폭탄 | pids 제한 |
| 비용 폭탄 | 사용자당 일일 실행 횟수 제한, 전체 동시 실행 상한, 큐 |
| 내 서버의 다른 비밀 | 실행 서버를 **다른 서비스와 분리**, 비밀키 없는 전용 VPS |

QEMU 항목은 "게스트 안에서 돌린다"는 이중 격리라 상대적으로 안전하지만, 컨테이너 격리는 그대로 해야 한다.

## 5. 단계별 계획표

시간은 Don이 취업 준비와 병행한다는 전제로 **주 5~8시간** 기준이다.

| Phase | 목표 | 결과물 | 작업 | 기간 | 비용 |
|---|---|---|---|---|---|
| **0. 정적 공개** | 읽기 사이트를 인터넷에 올린다 | `https://<도메인>` 에서 `site/` 접속 | ① 공개 범위 점검(0장) ② 레슨의 저작권 훑기 ③ 도메인 구매·Cloudflare 연결 ④ Pages 배포(git push 자동) ⑤ 개인 정보·경로·회사명 제거 | 1~2주 | 도메인 연 ~$12 |
| **1. 개인 원격 개발환경 (D)** | 환경을 서버에 올리는 게 실제로 되는지 검증 | 내 VPS + 브라우저 VS Code | ① VPS 1대 생성 ② Docker 이미지(clang·python·qemu·arm-none-eabi·verilator·cocotb) ③ code-server + **Cloudflare Access/Tailscale로 나만 접속** ④ 스모크 테스트(env 가이드 6절) 통과 | 1~2주 | VPS 월 €5~15 |
| **2. 브라우저 플레이그라운드 (A)** | 각 문제 페이지에 에디터+실행 버튼 | 문제 옆 Monaco, Python은 Pyodide, C는 WASM | ① 레슨 페이지에 에디터 컴포넌트 삽입(`build_site.py` 템플릿) ② 시작 코드·테스트 하니스를 레슨별로 데이터화 ③ localStorage로 코드 임시 저장 ④ 지원 가능한 아이템에만 "▶ 실행" 표시 | 3~4주 | 0 |
| **3. 서버 실행 (B)** | QEMU·verilator 등 무거운 env 지원 | `POST /run` → 결과 스트림 | ① 실행 API(Judge0 평가 후 채택/자체 구현) ② 샌드박스 격리(4절) ③ 큐·레이트 리밋·Turnstile ④ env별 이미지(`mac`용 clang, `qemu`, `fpga`) ⑤ 로그·알림 | 4~6주 | VPS 월 €10~20 |
| **4. 계정·진행도** | 푼 문제 저장, 학습 경로 | GitHub 로그인, 체크표시, 저장된 풀이 | ① OAuth ② DB 스키마(user, item, status, code) ③ 기존 진행도 UI 연결 | 2~3주 | 소규모 무료 |
| **5. 공개 운영** | 모르는 사람이 써도 안전 | 약관·개인정보, 모니터링, 백업 | ① 이용약관 · 면책 ② 남용 대응 절차 ③ 업타임 모니터링 ④ 월 비용 상한 알림 | 상시 | - |

### 5-1. 병행 트랙 C: 문제(레슨) 생성 — 서비스의 진짜 본체

서버는 몇 주면 만들지만, **콘텐츠는 지금 2191개 중 92개(4%)뿐**이다. 서비스를 열어도 대부분이 "준비중"이면 가치가 없으므로, 이 트랙이 Phase 0~5와 **동시에 계속 도는 상시 액션 아이템**이다.

| 트랙 | 작성됨 / 전체 | 비고 |
|---|---|---|
| C | 41 / 41 | ✅ 완료 |
| DS | 28 / 39 | Basic · Mid 완료, Advanced 남음 |
| CPP | 14 / 38 | Basic 완료 |
| SIV | 9 / 39 | 5개 신규 포함, 나머지 30개 |
| 나머지 65개 트랙 | 0 / 2034 | 전부 미작성 |

**액션 아이템**
1. **서비스 호환 규약 확정 (먼저, 반나절):** 새 레슨의 `starter.*`가 서비스에서 그대로 실행되게 규칙을 정한다.
   - 단일 파일, 외부 입력 없이 `main`에서 시작해 stdout으로 `PASS` / `FAIL: <이유>`를 출력하고 종료 코드로 판정
   - 파일·네트워크·시간 의존 금지 (결과가 항상 같아야 함)
   - 레슨 front matter에 `runner: wasm | server | local` 필드 추가 (wasm=브라우저 가능, server=qemu/verilator 필요, local=맥 전용)
2. **작성 순서 (우선순위):** 면접·목표 직무 직결 → 서비스에서 실행 가능한 것 순서.
   - 1순위: SIV · MEM · TEST · RTOS · ARCH · BM (실리콘 검증·펌웨어, `mac`/`qemu`)
   - 2순위: PCIE · PLAT · SEC · ARM · RISCV · RTLDV
   - 3순위: 나머지 (AI 가속기·로보틱스·퀀트·시스템 설계 등)
   - 마지막: `linux` env 133개 (서비스 범위 밖이면 "로컬 가이드형"으로 작성)
3. **배치 단위로 병렬 작성:** 트랙 하나(30~40개)를 NOTES_SPEC과 트랙별 WRITER_BRIEF로 에이전트에 나눠 맡기고, `verify_lessons.py`가 통과해야 머지한다 (지금까지의 C · DS · CPP 방식 그대로).
4. **공개 전 저작권 점검:** 레슨마다 외부 문장 인용이 없는지 훑는다. 유료 강의(AlgoMonster, 퀀트 코스)에서 온 문제는 **문제 본문을 새로 작성**한다.
5. **진행도 지표:** 사이트 허브의 "학습가능" 숫자를 공개 기준으로 삼는다. 제안: **전체의 30%(약 650개)가 되면 Phase 0 공개**, 100%까지는 점진적으로.

**공개 시점 제안:** 콘텐츠가 0이어도 Phase 0~1은 시작할 수 있다 (도메인과 환경 검증은 콘텐츠와 무관). 단 **불특정 공개는 학습가능 30% 이후**를 권장한다.

**최소 가치 순서:** Phase 0만으로도 "어디서든 읽는 사이트", Phase 1만으로도 "어디서든 실습하는 내 환경", Phase 2까지면 "문제 옆에서 바로 실행"이다. **Phase 3 이후는 사용자를 받을 때만** 의미가 크다.

## 6. 위험과 현실 점검

| 위험 | 영향 | 대응 |
|---|---|---|
| 운영 부담이 취업 준비를 잡아먹음 | 높음 | Phase 0~1에서 멈춰도 OK. Phase 3은 **사용자가 생기고 나서** |
| 저작권 문제 | 높음 | 0장 점검. 유료 강의 노트는 공개 대상에서 영구 제외 |
| 보안 사고 | 높음 | Phase 3 전에는 서버 실행을 공개하지 않는다. Phase 1은 본인만 접속 |
| `linux` env가 컨테이너에서 안 됨 | 중간 | 133개는 "로컬 Lima 가이드" 유지. 필요하면 나중에 microVM |
| WASM 환경과 맥 결과가 다름 | 중간 | 실행 버튼 옆에 "브라우저 근사 실행" 표시, ASan·타이밍 항목은 로컬 안내 |
| 비용 초과 | 낮음 | 동시 실행 상한 + 월 결제 알림 |
| 사용자가 없을 수 있음 | 중간 | **포트폴리오 가치**로도 충분: 풀스택·인프라·보안을 보여주는 프로젝트 |

## 7. 이 프로젝트의 가치 (이력서 관점)

Don의 목표(AI 가속기·로보틱스·실리콘 검증)와 직접 연결되진 않지만, **"임베디드 학습 플랫폼을 직접 설계·운영"**은 시스템 설계, 격리/보안, 툴체인 지식을 한 번에 보여준다. 다만 **지원 마감이 급한 회사가 있으면 이 프로젝트가 우선순위에서 밀려야 한다.**

## 8. 결정이 필요한 것

| # | 질문 | 선택지 | 내 추천 |
|---|---|---|---|
| 1 | 누구를 위한 서비스인가 | 나만 / 지인 공유 / 불특정 공개 | 처음엔 **나만 + 지인 소수** (Phase 0~2) |
| 2 | 공개 범위 | ~~레슨 전체 / 일부 샘플~~ | ✅ **확정: `embedded-study-note`만 공개** (유료 노트 제외). 공개 시점은 학습가능 30% 이후 권장 |
| 3 | 도메인 | 개인 브랜드 / 서비스 전용 | 개인 브랜드 도메인 1개에 하위 도메인 |
| 4 | 서버 실행 | 필요 / 브라우저 WASM만으로 충분 | Phase 2 쓰고 나서 **정말 필요한지 보고 결정** |

## 9. 다음 한 걸음 (이번 주 안에)

1. **서비스 호환 규약**(5-1 액션 1) 확정 후 SIV · MEM 레슨부터 작성 재개 (반나절 + 상시)
2. 공개해도 되는 레슨 점검 목록 만들기 (0장 기준, 1~2시간)
3. 도메인 후보 3개 가용 여부 확인 (10분)
4. 레슨 1개에 Monaco + Pyodide를 붙인 **프로토타입 한 페이지** 만들기 (반나절) — 이걸 해보면 Phase 2의 난이도가 체감된다
