# M1. 실리콘 평가 기준 — 제품용 AI 칩을 고르는 기준표와 의사결정 과정

> **이 노트를 다 읽으면**: TOPS 밖의 평가 기준 30여 개를 그룹별로 말하고, 각각을 어디서 어떻게 얻는지(datasheet·eval kit 실측·벤더 주장)와 믿을 수 있는 정도를 구분할 수 있다 · 칩이 손에 오기 전에 datasheet 숫자만으로 우리 모델의 latency·에너지 범위를 추정하고, 벤더 숫자를 역산해서 "조건이 다른 숫자"를 골라낼 수 있다 · must-have gate → 앵커 기반 효용 → 가중 점수 → Monte Carlo 승률 → 민감도로 이어지는 결정 절차를 코드로 돌리고 설명할 수 있다 · 공수·TCO·공급 리스크까지 넣어 RFI부터 decision review까지 선정 과정을 이끌 수 있다
> **JD 연결**: "**Evaluate and select silicon platforms (GPUs, NPUs etc.)** for Hark's next gen on-device and edge deployment of a wide range of models" — study_prep_list **M1** 행: TOPS, TOPS/W, 메모리 대역폭, SRAM 크기, 지원 정밀도(INT4/FP16), op 커버리지, 툴체인 성숙도, 벤더 지원, 전원 상태, 가격·공급 (P0). 함께 닿는 행: **M2**(벤치마크 방법론), **M3**(벤더 지형), **M4**(데이터시트 해석), **PJ8**(가상 실리콘 선정 보고서)
> **Don 기준 난이도**: 새 실리콘 bring-up, NPI 단계 관리, 벤더(FAE)와의 이슈 처리, 전력·성능 margin sign-off, 부품 qualification은 이미 해 봤다 / 새로 배울 것은 "ML workload로 칩을 비교하는 법", 불확실한 숫자로 결정하는 법(범위·Monte Carlo·민감도), 공수·TCO·공급 리스크를 같은 표에 올리는 법
> **선행 노트**: D1(MAC 세기), D3(roofline·ridge), D7(에너지 모델·TOPS/W), E5(NPU 해부·활용률), E7(대역폭 계산), F4(Qualcomm·AI Hub), F8(bring-up·op sweep·벤더 대응), I1(예산 → 모델 제약), I5(벤치마크 하니스·점수화·Pareto). 이 노트는 그 내용을 **다시 설명하지 않고 ID로 가리킨다**.

---

## 0. 큰 그림 — 칩 선정은 시스템 결정이다

### 0.1 면접에서 실제로 나오는 질문

> "We're planning our next-generation wearable. How would you choose the SoC or NPU for it?"

여기서 "TOPS가 제일 높은 칩이요"라고 답하면 끝이다. "우리 모델로 벤치마크해서 빠른 것이요"도 반만 맞다. 좋은 답은 **절차**다. 제품 요구사항에서 예산을 만들고(I1), 예산에서 gate를 만들고, 후보를 gate로 거르고, 남은 후보를 여러 기준으로 점수화하되 숫자의 불확실성을 같이 다루고, 비용·공수·공급 리스크까지 넣어서, 여러 팀이 같이 결정한다.

Don에게 익숙한 말로 바꾸면 **부품 선정 + qualification + 2nd source 검토**를 칩 한 개에 대해, 훨씬 큰 판돈으로 하는 일이다. SoC는 한번 고르면 보드·전원·펌웨어·ML 툴체인·공급 계약까지 2~4년을 묶는다. 잘못 고르면 bring-up 단계(F8)에서 알게 되고, 그때는 이미 늦다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="m1a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="340" y="20" font-size="13" text-anchor="middle">칩 선정 = workload에서 출발해 bring-up으로 끝나는 사슬의 한가운데</text><rect x="8" y="40" width="96" height="60" rx="8" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="56" y="64" font-size="13" text-anchor="middle">workload</text><text x="56" y="82" font-size="12" text-anchor="middle">KWS·ASR·LLM</text><rect x="120" y="40" width="96" height="60" rx="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="168" y="64" font-size="13" text-anchor="middle">예산 (I1)</text>
<text x="168" y="82" font-size="12" text-anchor="middle">mW·ms·MB·$</text><rect x="232" y="40" width="96" height="60" rx="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="280" y="64" font-size="13" text-anchor="middle">후보</text><text x="280" y="82" font-size="12" text-anchor="middle">RFI → 4~6개</text><rect x="344" y="40" width="96" height="60" rx="8" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/><text x="392" y="64" font-size="13" text-anchor="middle">평가</text><text x="392" y="82" font-size="12" text-anchor="middle">추정 + PoC</text><rect x="456" y="40" width="96" height="60" rx="8" fill="#e08a3c" fill-opacity="0.2" stroke="#e08a3c"/>
<text x="504" y="64" font-size="13" text-anchor="middle">결정</text><text x="504" y="82" font-size="12" text-anchor="middle">gate + 점수</text><rect x="568" y="40" width="104" height="60" rx="8" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/><text x="620" y="64" font-size="13" text-anchor="middle">bring-up (F8)</text><text x="620" y="82" font-size="12" text-anchor="middle">→ NPI → 양산</text><line x1="104" y1="70" x2="118" y2="70" stroke="currentColor" marker-end="url(#m1a)"/><line x1="216" y1="70" x2="230" y2="70" stroke="currentColor" marker-end="url(#m1a)"/><line x1="328" y1="70" x2="342" y2="70" stroke="currentColor" marker-end="url(#m1a)"/>
<line x1="440" y1="70" x2="454" y2="70" stroke="currentColor" marker-end="url(#m1a)"/><line x1="552" y1="70" x2="566" y2="70" stroke="currentColor" marker-end="url(#m1a)"/><path d="M504,100 L504,126 L168,126 L168,102" fill="none" stroke="currentColor" stroke-dasharray="5 3" marker-end="url(#m1a)"/><text x="336" y="120" font-size="12" text-anchor="middle">아무도 gate를 못 넘으면 → 요구사항·모델을 다시 협상</text><text x="340" y="160" font-size="13" text-anchor="middle">누가 무엇을 들고 오나</text><rect x="8" y="172" width="104" height="110" rx="6" fill="none" stroke="#4a7bd0"/><text x="60" y="192" font-size="13" text-anchor="middle">HW</text><text x="60" y="212" font-size="12" text-anchor="middle">전원·열·패키지</text>
<text x="60" y="230" font-size="12" text-anchor="middle">인터페이스</text><text x="60" y="248" font-size="12" text-anchor="middle">보드 면적</text><rect x="120" y="172" width="104" height="110" rx="6" fill="none" stroke="#4a7bd0"/><text x="172" y="192" font-size="13" text-anchor="middle">FW/SW</text><text x="172" y="212" font-size="12" text-anchor="middle">BSP·RTOS·Linux</text><text x="172" y="230" font-size="12" text-anchor="middle">power state</text><text x="172" y="248" font-size="12" text-anchor="middle">debug 접근</text><rect x="232" y="172" width="104" height="110" rx="6" fill="none" stroke="#e08a3c"/><text x="284" y="192" font-size="13" text-anchor="middle">ML</text>
<text x="284" y="212" font-size="12" text-anchor="middle">model zoo</text><text x="284" y="230" font-size="12" text-anchor="middle">op·정밀도</text><text x="284" y="248" font-size="12" text-anchor="middle">툴체인</text><rect x="344" y="172" width="104" height="110" rx="6" fill="none" stroke="#e08a3c"/><text x="396" y="192" font-size="13" text-anchor="middle">Product</text><text x="396" y="212" font-size="12" text-anchor="middle">기능·배터리</text><text x="396" y="230" font-size="12" text-anchor="middle">출시 일정</text><text x="396" y="248" font-size="12" text-anchor="middle">로드맵</text><rect x="456" y="172" width="104" height="110" rx="6" fill="none" stroke="#3f9a6b"/>
<text x="508" y="192" font-size="13" text-anchor="middle">Supply chain</text><text x="508" y="212" font-size="12" text-anchor="middle">lead time</text><text x="508" y="230" font-size="12" text-anchor="middle">수명·2nd source</text><text x="508" y="248" font-size="12" text-anchor="middle">allocation</text><rect x="568" y="172" width="104" height="110" rx="6" fill="none" stroke="#3f9a6b"/><text x="620" y="192" font-size="13" text-anchor="middle">Finance·법무</text><text x="620" y="212" font-size="12" text-anchor="middle">BOM·NRE</text><text x="620" y="230" font-size="12" text-anchor="middle">license·royalty</text><text x="620" y="248" font-size="12" text-anchor="middle">계약·NDA</text>
</svg>
```

그림 1 — 칩 선정의 위치. 위 줄은 시간 순서, 아래 줄은 결정에 숫자를 들고 오는 팀이다. ML 엔지니어의 몫은 주황 두 칸(평가·결정)과 ML 상자지만, 결정은 여섯 팀이 같이 내린다. 점선 화살표가 중요하다: 어느 후보도 gate를 넘지 못하면 칩을 더 찾는 대신 요구사항이나 모델(I2, C7)을 바꾸는 협상을 한다.

### 0.2 Don의 경험과 겹치는 부분

| Don이 해 본 일 | 칩 선정에서 같은 일 | 이 노트의 절 |
|---|---|---|
| FPGA pre-silicon bring-up, 새 실리콘 통합 | 칩이 오기 전 추정, eval kit PoC | 3, 7.5 |
| 전력·성능 margin sign-off | gate를 범위로 보고 통과 확률을 계산 | 4, 6.1 |
| 벤더 FAE와 버스 장애 root cause | 벤더 대응 품질을 평가 기준으로 재기 | 1.2, 7.5 |
| NPI → MP, factory test | 양산 공급·수명·certification 기준 | 1.2, 5.3 |
| SSD 펌웨어 성능 튜닝 (대역폭·캐시) | roofline 기반 성능 추정 | 3 |
| 부품 qualification, 2nd source | 공급 리스크·lifecycle 점수 | 5.3 |

새로 배울 것은 세 가지다. (1) **ML workload로 칩을 재는 법** — TOPS는 출발점일 뿐이고, 우리 모델의 MAC·바이트·op가 결과를 정한다. (2) **불확실한 숫자로 결정하는 법** — 평가 초기 숫자의 절반은 벤더 주장이거나 추정이다. 점 하나가 아니라 범위로 다루고, "이 결정이 뒤집힐 확률"을 말해야 한다. (3) **기술 외 기준**(공수, TCO, 공급, 로드맵)을 같은 표에 올리는 법.

### 0.3 이미 배운 것과 이 노트의 경계

| 주제 | 어디서 배웠나 | 이 노트에서 하는 일 |
|---|---|---|
| roofline, ridge point | D3 | 칩 spec + 모델 목록 → latency 범위 계산기로 묶는다 (3절) |
| TOPS/W 해석, 추론당 에너지 | D7 5절 | 계산기의 에너지 항에 그대로 쓴다 |
| NPU 해부, 활용률, "NPU 평가 질문" | E5 1.5·3.7·9절 | 활용률을 **범위**로 넣는 근거 |
| LPDDR 대역폭 계산 | E7 3절 | 후보 칩의 BW 행 |
| AI Hub로 실기기 프로파일 | F4 | 하드웨어 없이 실측에 가까운 숫자를 얻는 경로 (1.2절 표) |
| op sweep, 버전 매트릭스, 벤더 대응 | F8 3·7·9절 | PoC 계획에서 가리킨다 (7.5절) |
| 예산 → 모델 제약 | I1 | gate의 숫자가 여기서 온다 |
| 하니스, 기하평균 점수, Pareto, 가중치 민감도 | I5 5·6절 | **불확실성까지 넣은** 점수화로 확장한다 (6절) |
| 전력 측정, 열·지속 성능 | K3, K4 | PoC의 측정 방법 |
| 벤더별 칩 지형 | M3 (예정) | 이 노트는 **가상 칩 A~D**만 쓴다 |
| 데이터시트 숫자의 숨은 조건 | M4 (예정) | 3.6절은 맛보기만 |

이 노트의 모든 칩·가격·확률은 **설명용으로 지어낸 숫자**다. 특정 벤더 제품과 무관하다. 실제 벤더 숫자는 M3에서 다룬다.

---

## 1. 기준 분류 체계 — TOPS는 빙산의 일각

### 1.1 직관 — 물 위와 물 아래

datasheet 첫 페이지에는 TOPS, 공정, CPU 코어 수가 있다. 결정을 실제로 가르는 것은 대부분 물 아래에 있다: 우리 모델에서의 실효 성능, always-on 전력, 툴체인이 우리 모델을 통째로 받아 주는지, 벤더가 작은 고객의 버그 리포트에 답하는지, 3년 뒤에도 그 칩을 살 수 있는지.

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="20" y1="92" x2="660" y2="92" stroke="#4a7bd0" stroke-width="2" stroke-dasharray="8 4"/><text x="650" y="84" font-size="12" text-anchor="end">datasheet 첫 페이지 (수면)</text><polygon points="300,30 380,30 420,92 260,92" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="340" y="56" font-size="13" text-anchor="middle">peak TOPS</text><text x="340" y="76" font-size="12" text-anchor="middle">공정 · CPU 코어 수</text><polygon points="260,92 420,92 600,300 80,300" fill="#4a7bd0" fill-opacity="0.12" stroke="#4a7bd0"/><text x="340" y="122" font-size="13" text-anchor="middle">우리 모델에서의 실효 성능 · 활용률 · DRAM 대역폭 · SRAM 크기</text>
<text x="340" y="148" font-size="13" text-anchor="middle">정밀도 지원 · op coverage · dynamic shape · attention</text><text x="340" y="174" font-size="13" text-anchor="middle">always-on 전력 · power state 전환 시간 · 지속(열) 성능</text><text x="340" y="200" font-size="13" text-anchor="middle">툴체인 성숙도 · BSP 품질 · 프로파일러 · 업데이트 주기</text><text x="340" y="226" font-size="13" text-anchor="middle">벤더 지원 (FAE · errata · 응답 시간) · 생태계</text><text x="340" y="252" font-size="13" text-anchor="middle">단가 · NRE · royalty · 엔지니어링 공수</text><text x="340" y="278" font-size="13" text-anchor="middle">공급 수명 · 2nd source · allocation · 로드맵 · 인증</text><text x="40" y="200" font-size="12" text-anchor="middle">결정을</text>
<text x="40" y="216" font-size="12" text-anchor="middle">가르는 곳</text>
</svg>
```

그림 2 — 평가 기준의 빙산. 수면 위 숫자는 마케팅이 고른 것이고, 결정은 아래에서 난다. 아래로 갈수록 숫자로 만들기 어렵지만(소프트웨어 성숙도, 벤더 지원, 공급), 어렵다고 빼면 결정이 위쪽 숫자로만 쏠린다. 이 노트의 절반은 아래쪽 기준을 숫자로 만드는 방법이다.

### 1.2 숫자를 어디서 얻나 — 출처와 신뢰도

같은 "LLM decode 40 tok/s"라도 어디서 왔는지에 따라 무게가 다르다. 기준표의 모든 칸에 **출처**와 **신뢰도**를 붙인다. 펌웨어로 말하면 "spec 값", "typical 값", "우리 보드 실측값"을 구분하는 것과 같다.

| 출처 | 예 | 신뢰도 | 얻는 데 걸리는 시간 | 주의 |
|---|---|---|---|---|
| 공개 datasheet·제품 brief | peak TOPS, LPDDR 종류·폭, 패키지 | 중 (조건이 숨어 있다, M4) | 즉시 | INT4·sparsity·최고 클럭 기준인지 확인 |
| NDA datasheet·TRM·errata | power state 표, SRAM 구성, 전환 시간 | 중~상 | NDA 후 1~3주 | 버전·날짜를 기록 |
| 벤더 벤치마크·슬라이드 | "MobileNetV2 0.08 ms" | 하 (벤치마케팅) | 즉시 | 3.6절처럼 역산해서 조건을 묻는다 |
| 벤더가 우리 모델을 대신 돌려 준 결과 | 컴파일 리포트, 사이클 추정 | 중 | 1~2주 | 버전·설정·양자화 방식 기록 |
| 공개 벤치마크 | MLPerf Tiny·Mobile 등 (I5 2절) | 중 | 즉시 | 우리 모델·시나리오와 다르다 |
| 원격 실기기 서비스 | Qualcomm AI Hub 같은 hosted device 프로파일 (F4) | 중~상 | 수 시간 | 기기·OS·런타임 버전이 우리 제품과 같은지 |
| **eval kit 실측** | 우리 모델 latency·전력·열 | **상** | 키트 도착 후 2~6주 | 측정 셋업(K3), 보드 차이 |
| 추정 모델 (roofline 등) | 3절의 계산기 | 중 (범위로만) | 수 시간 | 가정을 표에 같이 적는다 |
| 레퍼런스 고객·커뮤니티 | 포럼, 다른 팀 경험, GitHub issue | 하~중 | 1~2주 | 일화, 그러나 툴체인 품질의 좋은 신호 |
| 계약 문서 | 수명 보장, 가격, LTB 조건 | 상 (서명되면) | 수주~수개월 | 서명 전에는 "의향"일 뿐 |

말로 하면: **평가 초기에 표의 대부분은 "하~중" 칸이고, 평가의 목적은 결정에 영향을 주는 칸을 "상"으로 올리는 것이다.** 모든 칸을 실측할 시간은 없다. 6.4절의 민감도 분석이 "어느 칸을 먼저 실측해야 하나"를 알려 준다.

### 1.3 기준표 — 32개 기준

열의 뜻: **얻는 법**은 1.2절의 출처, **신뢰도**는 처음 얻었을 때 기준, **G/S**는 gate(G, 통과/탈락)인지 점수(S, 가중치)인지. 많은 기준이 "최소선은 gate, 그 위는 점수" 둘 다다(G+S).

| 그룹 | 기준 | 무엇을 보나 | 얻는 법 | 신뢰도 | G/S |
|---|---|---|---|---|---|
| 연산 | peak TOPS (정밀도별) | INT8/INT4/FP16 각각, dense 기준인지 | datasheet | 중 | 참고만 |
| 연산 | 실효 성능 (우리 모델) | 대표 모델의 latency, 실효 TOPS | 추정 → eval kit | 하 → 상 | G+S |
| 연산 | 활용률 | 실효 / peak, 레이어 모양별 (E5 3.7) | 컴파일 리포트, 프로파일러 | 중 | S |
| 연산 | CPU·DSP 여유 | NPU 밖 전처리·후처리·fallback을 감당할 코어 | datasheet + 실측 | 중 | S |
| 메모리 | on-chip SRAM | NPU 전용/공유, 크기, retention 가능 여부 | NDA TRM | 중~상 | G+S |
| 메모리 | DRAM 종류·폭·속도 | LPDDR4X/5/5X, ×16/×32/×64 → 피크 BW (E7) | datasheet | 상 | S |
| 메모리 | 실효 대역폭 | 경합 상황의 NPU 실효 BW | eval kit (STREAM 류, D3 5절) | 하 → 상 | S |
| 메모리 | 용량 한계 | 지원 최대 DRAM, 주소 공간, NPU가 볼 수 있는 범위 | TRM | 상 | G |
| 정밀도 | 정수 | INT8 per-channel, INT16 activation, INT4 weight | 툴체인 문서 + 실험 | 중 | G+S |
| 정밀도 | 부동소수점 | FP16, BF16, FP8 | datasheet | 중 | S |
| 모델 지원 | op coverage | 우리 model zoo의 op가 전부 가속기에서 도나 (F8 3절) | op sweep | 상 (직접 하면) | G+S |
| 모델 지원 | dynamic shape | 가변 길이 입력, KV-cache 성장 | 실험 | 중 | S |
| 모델 지원 | transformer 지원 | attention, LayerNorm, GELU, softmax 효율 | 실험 + 프로파일 | 중 | G (LLM 쓰면) |
| 전력 | active 전력 | 대표 모델 추론 중 레일 전력 | eval kit (K3) | 하 → 상 | S |
| 전력 | idle·sleep 전력 | 화면 꺼짐·대기 상태의 바닥 전력 | datasheet → 실측 | 중 | S |
| 전력 | always-on island | KWS·착용 감지 경로 전력 (4절) | TRM + 실측 | 중 | G |
| 전력 | power state·전환 시간 | 상태 수, wake latency, 전환 에너지, retention | TRM + 실측 | 중 | G+S |
| 열 | 지속 성능 | 10분 이상 부하에서 클럭·latency (K4) | eval kit (열 셋업 주의) | 하 → 중 | S |
| 인터페이스 | 센서·오디오 | MIPI CSI, I2S/PDM 마이크 수, I3C/I2C/SPI | datasheet | 상 | G |
| 인터페이스 | 연결 | PCIe, USB, 내장 radio (BLE·Wi-Fi) 또는 외부 칩 | datasheet | 상 | G+S |
| 보안 | secure boot·TEE | root of trust, TEE, 보안 디버그 잠금 | NDA 문서 | 중 | G |
| 보안 | crypto·key 저장 | HW crypto, OTP/eFuse, 보안 key 저장 | datasheet | 중 | G |
| 소프트웨어 | 툴체인 성숙도 | 변환·양자화·컴파일 경로, 오류 메시지, 문서 | 실험 + 레퍼런스 | 하 → 중 | S |
| 소프트웨어 | 런타임 지원 | TFLite/LiteRT, ONNX Runtime EP, 벤더 런타임(QNN 류), llama.cpp 백엔드 | 문서 + 실험 | 중 | G+S |
| 소프트웨어 | OS·BSP 품질 | Linux/Android/RTOS BSP, 커널 버전, upstream 여부 | 실험 | 하 → 중 | S |
| 소프트웨어 | 업데이트 주기·호환 | SDK 릴리스 주기, 하위 호환, 지원 기간 | 릴리스 노트 이력 | 중 | S |
| 소프트웨어 | 디버그·프로파일러 | JTAG/SWD, trace, NPU 프로파일러, 레이어별 사이클 | 실험 | 중 | S |
| 생태계 | 커뮤니티·레퍼런스 | 공개 예제, 포럼 활동, 다른 제품 채택 | 조사 | 하~중 | S |
| 벤더 | 지원 품질 | FAE 접근성, 응답 시간, errata 공개, 작은 고객 대우 | 평가 기간에 직접 시험 | 중 | S |
| 비용 | 단가·NRE·license | 수량별 단가, 개발비, SDK·런타임 royalty | RFQ | 중 → 상 | G+S |
| 공급 | 수명·2nd source·allocation | 수명 보장 기간, LTB 정책, 대체 칩, 공급 우선순위 | 계약 협상 | 중 | G+S |
| 로드맵·물리 | 다음 세대·패키지·인증 | pin 호환 후속 칩, 크기·높이, 무선 모듈 인증·규격 | NDA 로드맵, datasheet | 하 / 상 | S / G |

말로 하면: 32개 중 **G가 들어간 것은 16개**, 그중 9개는 "최소선"만 gate(G+S)다. 순수 gate는 인터페이스·보안·용량·AO 전력처럼 **없으면 제품이 성립하지 않는 것**이다. 반대로 peak TOPS는 아예 점수에서 빠진다 — 실효 성능 행이 그 역할을 대신하고, peak를 같이 넣으면 같은 것을 두 번 세게 된다.

### 1.4 gate와 점수를 나누는 규칙

1. **"없으면 제품이 안 되는가?"** → gate. 예: 마이크 2개를 받을 PDM 입력, secure boot, LLM 0.5B를 담을 메모리.
2. **"더 좋으면 더 좋은가?"** → 점수. 예: tok/s, mW, 단가.
3. 둘 다면 **최소선을 gate로, 그 위를 점수로**. 최소선은 예산(I1)에서 온다.
4. **정확도는 점수가 아니라 gate**다 (I5 6.1). "10% 빠르면 정확도 1 pp 손해 봐도 된다"는 교환은 보통 성립하지 않는다.
5. gate는 **적게** 둔다. gate가 15개면 아무도 통과 못 하고, 그러면 사람들이 슬그머니 gate를 "점수"로 바꾼다. 진짜 must-have만.
6. 같은 것을 두 번 세지 않는다. peak TOPS와 실효 성능, DRAM BW와 LLM tok/s는 강하게 겹친다. 결과(실효 성능, tok/s)를 점수에 넣고 원인(TOPS, BW)은 설명용으로만 둔다.

### 1.5 함정

- **기준표를 벤더 datasheet 목차로 만든다**: 벤더가 자랑하는 항목만 남는다. 기준표는 **우리 제품 요구사항에서** 만든다(I1의 예산표가 출발점).
- **소프트웨어·벤더 기준을 "정성 평가"로 빼 둔다**: 점수에 안 들어가면 결정에서 무시된다. 5.4절의 rubric으로 숫자로 만든다.
- **기준을 평가가 끝난 뒤에 고친다**: 결과를 보고 가중치를 바꾸면 원하는 칩이 이기도록 조작하는 것과 같다. 기준과 가중치는 **PoC 시작 전에 서명**한다(7.6절).

---

## 2. 공통 사례 — 가상 후보 Chip A~D와 우리 workload

예를 들어 Hark 같은 음성 중심 웨어러블의 다음 세대를 고른다고 하자(제품 맥락은 추정이고, 칩은 전부 가상이다). I1에서 만든 예산을 그대로 쓴다.

| 예산·요구 (I1 가정) | 숫자 | 이 노트에서의 역할 |
|---|---|---|
| always-on KWS 경로 | 평균 ≤ 1 mW, hop 40 ms | gate G1 (4절) |
| on-device LLM 0.5B int4 decode | ≥ 10 tok/s (최소), 목표 40 | gate G2 + 점수 |
| LLM 메모리 몫 | ≈ 600 MiB (가중치 + KV + 버퍼) | 용량 gate |
| ASR encoder, 비전(MobileNetV2급) | 실시간보다 충분히 빠를 것 | 점수 (A~C는 모두 여유) |
| 보안 | secure boot, HW key 저장 | gate |
| 출시 | 1차 양산 100k대 가정, EVT까지 6개월 | 공수·TCO 계산 |

후보 4개는 일부러 성격이 다르게 만들었다.

| 후보 | 성격 | peak INT8 | DRAM | NPU SRAM | AO 경로 | 한 줄 인상 |
|---|---|---|---|---|---|---|
| Chip A | 대형 SoC (큰 벤더) | 12 TOPS | LPDDR5 ×32, 25.6 GB/s, 4 GB | 8 MB | SoC 내 LP-DSP | 빠르고 비싸다, 우리는 작은 고객 |
| Chip B | 중형 SoC | 6 TOPS | LPDDR4X ×32, 17.1 GB/s, 2 GB | 4 MB | AO microNPU | 균형형 |
| Chip C | 스타트업 NPU | 10 TOPS (광고 20 = 2:4 sparsity) | LPDDR4X ×16, 8.5 GB/s, 1 GB | 16 MB | 외부 MCU 필요 | SRAM이 크고 효율이 좋다는 주장, 툴체인 미성숙 |
| Chip D | AI MCU | 0.5 TOPS | 없음 (OSPI flash 0.2 GB/s) | 4 MB | 자체 | 싸고 저전력, LLM은 못 돈다 |

### 2.1 데이터 파일 — 범위로 적는다 (공용 모듈)

무엇을 하는 코드인가: 칩과 모델을 dict로 적는다. 확실하지 않은 숫자(활용률, 실효 BW 비율, 에너지 상수, 런타임 오버헤드)는 **(낮음, 기준, 높음)** 세 값으로 적는다. 이후 예제가 이 파일을 import한다.

```python
# m1_chips.py — 가상의 후보 칩 4개와 우리 모델 세트 (숫자는 전부 설명용으로 지어낸 값)
# 범위는 (낮음, 기준, 높음) 3-tuple. 실제 제품명·벤더와 무관하다.
CHIPS = {
  "A": dict(desc="대형 SoC", tops8=12.0, tops4=24.0, util=(0.25, 0.40, 0.55),
            bw=25.6, bw_eff=(0.55, 0.65, 0.75), sram_mb=8, dram_gb=4,
            pj_mac=(0.6, 0.9, 1.3), pj_byte=(25, 32, 40), p_on_mw=(120, 150, 190), ovh_ms=(0.2, 0.3, 0.5)),
  "B": dict(desc="중형 SoC", tops8=6.0, tops4=6.0, util=(0.30, 0.45, 0.60),
            bw=17.1, bw_eff=(0.55, 0.65, 0.75), sram_mb=4, dram_gb=2,
            pj_mac=(0.7, 1.0, 1.4), pj_byte=(30, 40, 50), p_on_mw=(80, 100, 130), ovh_ms=(0.2, 0.3, 0.5)),
  "C": dict(desc="스타트업 NPU", tops8=10.0, tops4=20.0, util=(0.15, 0.35, 0.60),  # 광고 20 TOPS = 2:4 sparsity 기준
            bw=8.5, bw_eff=(0.45, 0.60, 0.75), sram_mb=16, dram_gb=1,
            pj_mac=(0.3, 0.5, 0.9), pj_byte=(30, 40, 50), p_on_mw=(40, 60, 90), ovh_ms=(0.3, 0.8, 2.0)),
  "D": dict(desc="AI MCU", tops8=0.5, tops4=0.5, util=(0.30, 0.50, 0.70),
            bw=0.2, bw_eff=(0.70, 0.80, 0.90), sram_mb=4, dram_gb=0, flash_mb=64,  # DRAM 없음: weight는 OSPI flash에서 0.2 GB/s
            pj_mac=(0.3, 0.4, 0.6), pj_byte=(80, 100, 130), p_on_mw=(10, 15, 20), ovh_ms=(0.05, 0.1, 0.2)),
}
# 모델: MAC/추론, weight 바이트, activation DRAM 트래픽(SRAM에 안 들어갈 때), peak activation, 정밀도
MODELS = {
  "asr_enc":   dict(mac=104.5e6, w=5.0e6,   act=4.0e6,   peak=1.5e6, prec=8),
  "mnv2":      dict(mac=300.8e6, w=3.49e6,  act=13.45e6, peak=1.6e6, prec=8),
  "slm_pf128": dict(mac=63.2e9,  w=278e6,   act=30e6,    peak=8e6,   prec=4),   # 0.5B int4, prompt 128 token
  "slm_dec":   dict(mac=0.494e9, w=303e6,   act=0.5e6,   peak=1e6,   prec=4),   # token 1개: weight 278 MB + KV 25 MB를 매번 읽는다
}
```

숫자의 출처: ASR encoder 104.5 M MAC은 I5 3.2절, MobileNetV2의 MAC·가중치·activation 트래픽은 D7 3.2절, 0.5B int4의 가중치 278 MB와 KV 25 MB(ctx 2048)는 L1 1.3절에서 가져왔다. 칩 숫자는 모두 지어냈다. B의 `tops4=6.0`은 "INT4 weight는 저장만 되고 연산은 INT8로 한다"는 뜻이다 — 실제 칩에서 흔한 경우이고, datasheet의 INT4 TOPS가 **연산**인지 **저장 형식**인지는 꼭 물어야 할 질문이다.

---

## 3. 하드웨어 없이 성능 추정하기 — datasheet에서 우리 모델의 ms까지

### 3.1 직관 — eval kit이 오기 전 6주를 버리지 않는다

RFI 응답이 오고 eval kit이 도착하기까지 보통 몇 주가 걸린다. 그 사이에 할 수 있는 가장 가치 있는 일은 **datasheet 숫자 몇 개로 우리 모델의 latency·에너지 범위를 계산**하는 것이다. 목적은 정답을 맞히는 게 아니라 두 가지다.

1. **명백한 탈락자를 일찍 거른다** — 대역폭이 부족해서 LLM decode가 물리적으로 10 tok/s를 못 내는 칩은 kit을 기다릴 필요가 없다.
2. **벤더 숫자의 sanity check 기준을 만든다** — 내 추정 범위 밖의 벤더 숫자는 "조건이 다르다"는 신호다(3.6절).

Don이 FPGA pre-silicon 단계에서 "실리콘이 오면 이 정도 나와야 한다"는 기대값을 먼저 세워 두던 것과 같다. 기대값이 없으면 실측이 이상해도 이상한 줄 모른다.

### 3.2 공식 — roofline에 오버헤드와 범위를 붙인다

D3의 roofline을 latency 형태로 쓰면 이렇다.

```
t_compute = 2 · MAC / (peak_ops · u)          u = 활용률 (E5 3.7: 모양에 따라 2~90%)
t_memory  = DRAM_bytes / (BW_peak · e)        e = 실효 대역폭 비율 (E7 3.4: 보통 0.5~0.8)
t         = max(t_compute, t_memory) + t_ovh  t_ovh = 런타임·드라이버·launch 오버헤드
E         = MAC · e_mac + DRAM_bytes · e_byte + P_on · t      (D7 3.1의 단순화)
```

말로 하면: 연산 시간과 메모리 시간 중 **긴 쪽**이 latency를 정하고(겹쳐서 실행된다는 낙관적 가정), 거기에 고정 오버헤드를 더한다. 에너지는 연산·데이터 이동·켜져 있는 시간의 합이다.

새로 넣는 것은 **범위**다. `u`, `e`, `t_ovh`, `e_mac`, `e_byte`, `P_on`은 칩이 오기 전에는 모른다. 그래서 각각 (낮음, 기준, 높음)을 넣고, 세 번 계산한다: 모두 좋은 쪽(낙관), 기준, 모두 나쁜 쪽(비관). 이렇게 만든 범위는 넓지만, 넓다는 것 자체가 정보다 — **범위가 넓은 칸이 PoC에서 먼저 재야 할 칸**이다.

DRAM 바이트 계산에는 규칙 두 개를 넣었다(가정이다).

- weight + peak activation이 **NPU SRAM 절반** 안에 들어가면 weight가 SRAM에 상주한다고 본다 (나머지 절반은 다른 모델·런타임 몫). 그러면 DRAM에서 weight를 다시 읽지 않는다.
- peak activation이 SRAM 절반 안이면 activation은 DRAM에 나가지 않는다. 아니면 D7의 activation 트래픽을 더한다.

이 규칙이 Chip C(SRAM 16 MB)를 작은 모델에서 유리하게 만든다. 그 유리함이 실제로 나타나려면 컴파일러가 상주를 실제로 해 줘야 한다 — 이것도 PoC에서 확인할 항목이다.

### 3.3 손계산 — Chip A에서 LLM decode 1 token

```
DRAM 바이트  = 303e6 (weight 278 MB + KV 25 MB) + 0.1e6 (입출력) = 303.1e6 B
              (weight+act = 304 MB > SRAM 절반 4 MB → 상주 불가)
t_memory     = 303.1e6 / (25.6e9 × 0.65)  = 303.1e6 / 16.64e9 = 18.2 ms
t_compute    = 2 × 0.494e9 / (24e12 × 0.40) = 0.988e9 / 9.6e12 = 0.10 ms
t            = max(18.2, 0.10) + 0.3     = 18.5 ms   →  1000 / 18.5 = 54 tok/s
E            = 0.494e9 × 0.9 pJ  (= 0.44 mJ)
             + 303.1e6 × 32 pJ   (= 9.70 mJ)
             + 150 mW × 18.5 ms  (= 2.78 mJ)   =  12.9 mJ/token
```

말로 하면: decode는 연산이 0.1 ms뿐이고 나머지 18 ms는 전부 **바이트를 읽는 시간**이다. 에너지도 75%가 DRAM 읽기다. 그래서 LLM decode에서는 TOPS가 아니라 **DRAM 대역폭과 바이트당 에너지**가 칩을 가른다(D5, D7). 이 계산 하나로 "LLM이 목표라면 TOPS보다 LPDDR 폭을 먼저 보라"는 결론이 나온다.

### 3.4 코드로 확인 — 추정기 (예제 1)

무엇을 확인하는 코드인가: 3.2절의 식을 칩 4개 × 모델 4개에 적용해서 latency·에너지의 (낙관/기준/비관)과 병목(C = compute, M = memory)을 표로 만든다.

```python
# m1_est.py — roofline 기반 latency/energy 범위 추정기 (D3 roofline + D7 에너지 모델)
from m1_chips import CHIPS, MODELS
def dram_bytes(c, m):
    if (m["w"] > c["dram_gb"] * 1e9 + c.get("flash_mb", 0) * 1e6       # weight 둘 곳
            or m["peak"] > c["dram_gb"] * 1e9 + c["sram_mb"] * 1e6):   # activation 둘 곳
        return None                                         # 용량 부족: 아예 못 돈다
    resident = m["w"] + m["peak"] <= 0.5 * c["sram_mb"] * 1e6  # SRAM 절반 안이면 weight 상주 가정
    w = 0 if resident else m["w"]
    a = 0 if m["peak"] <= 0.5 * c["sram_mb"] * 1e6 else m["act"]
    return w + a + 0.1e6                                    # 입출력 0.1 MB
def estimate(c, m, k):          # k = 0 낙관, 1 기준, 2 비관
    b = dram_bytes(c, m)
    if b is None: return None
    tops = c["tops4"] if m["prec"] == 4 else c["tops8"]
    u, e, o = c["util"][2 - k], c["bw_eff"][2 - k], c["ovh_ms"][k]
    t_cmp = 2 * m["mac"] / (tops * 1e12 * u) * 1e3          # ms
    t_mem = b / (c["bw"] * 1e9 * e) * 1e3                   # ms
    t = max(t_cmp, t_mem) + o
    e_mj = (m["mac"] * c["pj_mac"][k] + b * c["pj_byte"][k]) * 1e-9 + c["p_on_mw"][k] * t * 1e-3
    return t, e_mj, ("C" if t_cmp >= t_mem else "M")
```

```python
# 예제 1: 칩 4개 × 모델 4개 → latency·에너지 범위 (낙관 ~ 비관), 병목(C=compute, M=memory)
from m1_chips import CHIPS, MODELS
from m1_est import estimate
print(f"{'chip':4s} {'model':10s} {'latency ms (opt/nom/pes)':>27s} {'energy mJ (opt/nom/pes)':>26s} bound")
for cn, c in CHIPS.items():
    for mn, m in MODELS.items():
        r = [estimate(c, m, k) for k in range(3)]
        if r[0] is None:
            print(f"{cn:4s} {mn:10s} {'용량 부족 — 실행 불가':>27s}"); continue
        lat = "/".join(f"{x[0]:.3g}" for x in r); en = "/".join(f"{x[1]:.3g}" for x in r)
        print(f"{cn:4s} {mn:10s} {lat:>27s} {en:>26s}   {r[1][2]}")
print("decode tok/s (pes~opt):", {cn: (round(1e3/estimate(c, MODELS['slm_dec'], 2)[0], 1),
       round(1e3/estimate(c, MODELS['slm_dec'], 0)[0], 1)) for cn, c in CHIPS.items() if estimate(c, MODELS['slm_dec'], 0)})
```

```text
chip model         latency ms (opt/nom/pes)    energy mJ (opt/nom/pes) bound
A    asr_enc              0.466/0.606/0.862          0.246/0.348/0.504   M
A    mnv2                 0.387/0.516/0.755          0.317/0.463/0.678   M
A    slm_pf128               16.2/18.8/22.4             47.6/69.6/98.7   M
A    slm_dec                     16/18.5/22               9.79/12.9/17   M
B    asr_enc               0.598/0.759/1.04          0.274/0.384/0.537   M
B    mnv2                  0.48/0.623/0.882          0.357/0.507/0.715   M
B    slm_pf128               35.3/47.1/70.7              56.3/80.2/113   C
B    slm_dec                 23.8/27.6/32.7             11.3/15.4/20.1   M
C    asr_enc                0.335/0.86/2.14         0.0477/0.108/0.292   C
C    mnv2                     0.4/0.972/2.4          0.109/0.213/0.492   C
C    slm_pf128               43.9/55.3/74.7               29.1/46/77.5   M
C    slm_dec                 47.8/60.2/81.2               11.2/16/22.9   M
D    asr_enc                   28.4/32/36.6            0.723/1.03/1.46   M
D    mnv2                      20/22.5/25.8           0.577/0.817/1.16   M
D    slm_pf128                용량 부족 — 실행 불가
D    slm_dec                  용량 부족 — 실행 불가
decode tok/s (pes~opt): {'A': (45.4, 62.6), 'B': (30.6, 42.0), 'C': (12.3, 20.9)}
```

출력에서 볼 것:

- **손계산과 일치**: A의 `slm_dec` 기준값 18.5 ms, 12.9 mJ.
- **LLM decode는 모든 칩에서 M(메모리)**: tok/s 순위가 TOPS 순위(A 12 > C 10 > B 6)가 아니라 **대역폭 순위**(A 25.6 > B 17.1 > C 8.5)를 그대로 따른다. C는 광고 TOPS가 B의 3배지만 decode는 B보다 2배 느리다.
- **B의 prefill만 C(연산)**: B는 INT4 연산이 없어서(tops4 = 6) prefill 63 G MAC이 연산에 묶인다. prefill이 TTFT를 정하므로(D5) "INT4 연산 지원"이 B의 약점으로 드러난다.
- **C의 작은 모델은 범위가 넓다**: ASR 0.34~2.14 ms, 6배 차이. SRAM 상주 덕에 연산·메모리 시간은 0.06 ms 수준이고, 나머지는 **런타임 오버헤드 (0.3~2.0 ms)**다. 미성숙한 런타임의 launch 오버헤드가 작은 모델의 성능을 결정한다 — PoC에서 가장 먼저 재야 할 숫자.
- **C의 에너지는 작은 모델에서 가장 좋다** (ASR 0.108 mJ vs A 0.348). 큰 SRAM으로 DRAM 왕복을 없앤 효과다. "TOPS/W가 좋다"는 C의 주장은 **상주가 되는 모델에 한해** 사실이다.
- **D는 LLM 용량 부족**: 278 MB weight를 둘 곳이 없다. ASR·비전은 돌지만 flash 대역폭에 묶여 20~37 ms다.

### 3.5 그림으로 — 네 칩의 roofline과 우리 모델의 위치

```svg
<svg viewBox="0 0 680 400" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="330" x2="650" y2="330" stroke="currentColor"/><line x1="70" y1="40" x2="70" y2="330" stroke="currentColor"/><line x1="70.0" y1="330" x2="70.0" y2="335" stroke="currentColor"/><text x="70.0" y="349" font-size="12" text-anchor="middle">1</text><line x1="215.0" y1="330" x2="215.0" y2="335" stroke="currentColor"/><text x="215.0" y="349" font-size="12" text-anchor="middle">10</text><line x1="360.0" y1="330" x2="360.0" y2="335" stroke="currentColor"/><text x="360.0" y="349" font-size="12" text-anchor="middle">100</text><line x1="505.0" y1="330" x2="505.0" y2="335" stroke="currentColor"/><text x="505.0" y="349" font-size="12" text-anchor="middle">1000</text>
<line x1="650.0" y1="330" x2="650.0" y2="335" stroke="currentColor"/><text x="650.0" y="349" font-size="12" text-anchor="middle">10000</text><line x1="65" y1="330.0" x2="70" y2="330.0" stroke="currentColor"/><text x="62" y="334.0" font-size="12" text-anchor="end">10</text><line x1="65" y1="257.5" x2="70" y2="257.5" stroke="currentColor"/><text x="62" y="261.5" font-size="12" text-anchor="end">100</text><line x1="65" y1="185.0" x2="70" y2="185.0" stroke="currentColor"/><text x="62" y="189.0" font-size="12" text-anchor="end">1000</text><line x1="65" y1="112.5" x2="70" y2="112.5" stroke="currentColor"/><text x="62" y="116.5" font-size="12" text-anchor="end">10000</text>
<line x1="65" y1="40.0" x2="70" y2="40.0" stroke="currentColor"/><text x="62" y="44.0" font-size="12" text-anchor="end">100000</text><text x="360.0" y="368" font-size="13" text-anchor="middle">arithmetic intensity (op/byte, DRAM 기준)</text><text x="18" y="185.0" font-size="13" text-anchor="middle" transform="rotate(-90 18 185.0)">attainable GOPS (int8 peak)</text>
<polyline fill="none" stroke-width="2" stroke="#4a7bd0" points="70.0,300.4 72.9,299.0 75.8,297.5 78.7,296.1 81.6,294.6 84.5,293.2 87.4,291.7 90.3,290.3 93.2,288.8 96.1,287.4 99.0,285.9 101.9,284.5 104.8,283.0 107.7,281.6 110.6,280.1 113.5,278.7 116.4,277.2 119.3,275.8 122.2,274.3 125.1,272.9 128.0,271.4 130.9,270.0 133.8,268.5 136.7,267.1 139.6,265.6 142.5,264.2 145.4,262.7 148.3,261.3 151.2,259.8 154.1,258.4 157.0,256.9 159.9,255.5 162.8,254.0 165.7,252.6 168.6,251.1 171.5,249.7 174.4,248.2 177.3,246.8 180.2,245.3 183.1,243.9 186.0,242.4 188.9,241.0 191.8,239.5 194.7,238.1 197.6,236.6 200.5,235.2 203.4,233.7 206.3,232.3 209.2,230.8 212.1,229.4 215.0,227.9 217.9,226.5 220.8,225.0 223.7,223.6 226.6,222.1 229.5,220.7 232.4,219.2 235.3,217.8 238.2,216.3 241.1,214.9 244.0,213.4 246.9,212.0 249.8,210.5 252.7,209.1 255.6,207.6 258.5,206.2 261.4,204.7 264.3,203.3 267.2,201.8 270.1,200.4 273.0,198.9 275.9,197.5 278.8,196.0 281.7,194.6 284.6,193.1 287.5,191.7 290.4,190.2 293.3,188.8 296.2,187.3 299.1,185.9 302.0,184.4 304.9,183.0 307.8,181.5 310.7,180.1 313.6,178.6 316.5,177.2 319.4,175.7 322.3,174.3 325.2,172.8 328.1,171.4 331.0,169.9 333.9,168.5 336.8,167.0 339.7,165.6 342.6,164.1 345.5,162.7 348.4,161.2 351.3,159.8 354.2,158.3 357.1,156.9 360.0,155.4 362.9,154.0 365.8,152.5 368.7,151.1 371.6,149.6 374.5,148.2 377.4,146.7 380.3,145.3 383.2,143.8 386.1,142.4 389.0,140.9 391.9,139.5 394.8,138.0 397.7,136.6 400.6,135.1 403.5,133.7 406.4,132.2 409.3,130.8 412.2,129.3 415.1,127.9 418.0,126.4 420.9,125.0 423.8,123.5 426.7,122.1 429.6,120.6 432.5,119.2 435.4,117.7 438.3,116.3 441.2,114.8 444.1,113.4 447.0,111.9 449.9,110.5 452.8,109.0 455.7,107.6 458.6,106.8 461.5,106.8 464.4,106.8 467.3,106.8 470.2,106.8 473.1,106.8 476.0,106.8 478.9,106.8 481.8,106.8 484.7,106.8 487.6,106.8 490.5,106.8 493.4,106.8 496.3,106.8 499.2,106.8 502.1,106.8 505.0,106.8 507.9,106.8 510.8,106.8 513.7,106.8 516.6,106.8 519.5,106.8 522.4,106.8 525.3,106.8 528.2,106.8 531.1,106.8 534.0,106.8 536.9,106.8 539.8,106.8 542.7,106.8 545.6,106.8 548.5,106.8 551.4,106.8 554.3,106.8 557.2,106.8 560.1,106.8 563.0,106.8 565.9,106.8 568.8,106.8 571.7,106.8 574.6,106.8 577.5,106.8 580.4,106.8 583.3,106.8 586.2,106.8 589.1,106.8 592.0,106.8 594.9,106.8 597.8,106.8 600.7,106.8 603.6,106.8 606.5,106.8 609.4,106.8 612.3,106.8 615.2,106.8 618.1,106.8 621.0,106.8 623.9,106.8 626.8,106.8 629.7,106.8 632.6,106.8 635.5,106.8 638.4,106.8 641.3,106.8 644.2,106.8 647.1,106.8 650.0,106.8"/>
<circle cx="457.3" cy="106.8" r="3.5" fill="#4a7bd0"/><line x1="100" y1="58" x2="124" y2="58" stroke="#4a7bd0" stroke-width="2"/><text x="130" y="62" font-size="12">Chip A: 12 TOPS · 25.6 GB/s · ridge 469</text>
<polyline fill="none" stroke-width="2" stroke="#e08a3c" points="70.0,313.1 72.9,311.7 75.8,310.2 78.7,308.8 81.6,307.3 84.5,305.9 87.4,304.4 90.3,303.0 93.2,301.5 96.1,300.1 99.0,298.6 101.9,297.2 104.8,295.7 107.7,294.3 110.6,292.8 113.5,291.4 116.4,289.9 119.3,288.5 122.2,287.0 125.1,285.6 128.0,284.1 130.9,282.7 133.8,281.2 136.7,279.8 139.6,278.3 142.5,276.9 145.4,275.4 148.3,274.0 151.2,272.5 154.1,271.1 157.0,269.6 159.9,268.2 162.8,266.7 165.7,265.3 168.6,263.8 171.5,262.4 174.4,260.9 177.3,259.5 180.2,258.0 183.1,256.6 186.0,255.1 188.9,253.7 191.8,252.2 194.7,250.8 197.6,249.3 200.5,247.9 203.4,246.4 206.3,245.0 209.2,243.5 212.1,242.1 215.0,240.6 217.9,239.2 220.8,237.7 223.7,236.3 226.6,234.8 229.5,233.4 232.4,231.9 235.3,230.5 238.2,229.0 241.1,227.6 244.0,226.1 246.9,224.7 249.8,223.2 252.7,221.8 255.6,220.3 258.5,218.9 261.4,217.4 264.3,216.0 267.2,214.5 270.1,213.1 273.0,211.6 275.9,210.2 278.8,208.7 281.7,207.3 284.6,205.8 287.5,204.4 290.4,202.9 293.3,201.5 296.2,200.0 299.1,198.6 302.0,197.1 304.9,195.7 307.8,194.2 310.7,192.8 313.6,191.3 316.5,189.9 319.4,188.4 322.3,187.0 325.2,185.5 328.1,184.1 331.0,182.6 333.9,181.2 336.8,179.7 339.7,178.3 342.6,176.8 345.5,175.4 348.4,173.9 351.3,172.5 354.2,171.0 357.1,169.6 360.0,168.1 362.9,166.7 365.8,165.2 368.7,163.8 371.6,162.3 374.5,160.9 377.4,159.4 380.3,158.0 383.2,156.5 386.1,155.1 389.0,153.6 391.9,152.2 394.8,150.7 397.7,149.3 400.6,147.8 403.5,146.4 406.4,144.9 409.3,143.5 412.2,142.0 415.1,140.6 418.0,139.1 420.9,137.7 423.8,136.2 426.7,134.8 429.6,133.3 432.5,131.9 435.4,130.4 438.3,129.0 441.2,128.6 444.1,128.6 447.0,128.6 449.9,128.6 452.8,128.6 455.7,128.6 458.6,128.6 461.5,128.6 464.4,128.6 467.3,128.6 470.2,128.6 473.1,128.6 476.0,128.6 478.9,128.6 481.8,128.6 484.7,128.6 487.6,128.6 490.5,128.6 493.4,128.6 496.3,128.6 499.2,128.6 502.1,128.6 505.0,128.6 507.9,128.6 510.8,128.6 513.7,128.6 516.6,128.6 519.5,128.6 522.4,128.6 525.3,128.6 528.2,128.6 531.1,128.6 534.0,128.6 536.9,128.6 539.8,128.6 542.7,128.6 545.6,128.6 548.5,128.6 551.4,128.6 554.3,128.6 557.2,128.6 560.1,128.6 563.0,128.6 565.9,128.6 568.8,128.6 571.7,128.6 574.6,128.6 577.5,128.6 580.4,128.6 583.3,128.6 586.2,128.6 589.1,128.6 592.0,128.6 594.9,128.6 597.8,128.6 600.7,128.6 603.6,128.6 606.5,128.6 609.4,128.6 612.3,128.6 615.2,128.6 618.1,128.6 621.0,128.6 623.9,128.6 626.8,128.6 629.7,128.6 632.6,128.6 635.5,128.6 638.4,128.6 641.3,128.6 644.2,128.6 647.1,128.6 650.0,128.6"/>
<circle cx="439.0" cy="128.6" r="3.5" fill="#e08a3c"/><line x1="100" y1="77" x2="124" y2="77" stroke="#e08a3c" stroke-width="2"/><text x="130" y="81" font-size="12">Chip B: 6 TOPS · 17.1 GB/s · ridge 351</text>
<polyline fill="none" stroke-width="2" stroke="#3f9a6b" points="81.6,329.3 84.5,327.9 87.4,326.4 90.3,325.0 93.2,323.5 96.1,322.1 99.0,320.6 101.9,319.2 104.8,317.7 107.7,316.3 110.6,314.8 113.5,313.4 116.4,311.9 119.3,310.5 122.2,309.0 125.1,307.6 128.0,306.1 130.9,304.7 133.8,303.2 136.7,301.8 139.6,300.3 142.5,298.9 145.4,297.4 148.3,296.0 151.2,294.5 154.1,293.1 157.0,291.6 159.9,290.2 162.8,288.7 165.7,287.3 168.6,285.8 171.5,284.4 174.4,282.9 177.3,281.5 180.2,280.0 183.1,278.6 186.0,277.1 188.9,275.7 191.8,274.2 194.7,272.8 197.6,271.3 200.5,269.9 203.4,268.4 206.3,267.0 209.2,265.5 212.1,264.1 215.0,262.6 217.9,261.2 220.8,259.7 223.7,258.3 226.6,256.8 229.5,255.4 232.4,253.9 235.3,252.5 238.2,251.0 241.1,249.6 244.0,248.1 246.9,246.7 249.8,245.2 252.7,243.8 255.6,242.3 258.5,240.9 261.4,239.4 264.3,238.0 267.2,236.5 270.1,235.1 273.0,233.6 275.9,232.2 278.8,230.7 281.7,229.3 284.6,227.8 287.5,226.4 290.4,224.9 293.3,223.5 296.2,222.0 299.1,220.6 302.0,219.1 304.9,217.7 307.8,216.2 310.7,214.8 313.6,213.3 316.5,211.9 319.4,210.4 322.3,209.0 325.2,207.5 328.1,206.1 331.0,204.6 333.9,203.2 336.8,201.7 339.7,200.3 342.6,198.8 345.5,197.4 348.4,195.9 351.3,194.5 354.2,193.0 357.1,191.6 360.0,190.1 362.9,188.7 365.8,187.2 368.7,185.8 371.6,184.3 374.5,182.9 377.4,181.4 380.3,180.0 383.2,178.5 386.1,177.1 389.0,175.6 391.9,174.2 394.8,172.7 397.7,171.3 400.6,169.8 403.5,168.4 406.4,166.9 409.3,165.5 412.2,164.0 415.1,162.6 418.0,161.1 420.9,159.7 423.8,158.2 426.7,156.8 429.6,155.3 432.5,153.9 435.4,152.4 438.3,151.0 441.2,149.5 444.1,148.1 447.0,146.6 449.9,145.2 452.8,143.7 455.7,142.3 458.6,140.8 461.5,139.4 464.4,137.9 467.3,136.5 470.2,135.0 473.1,133.6 476.0,132.1 478.9,130.7 481.8,129.2 484.7,127.8 487.6,126.3 490.5,124.9 493.4,123.4 496.3,122.0 499.2,120.5 502.1,119.1 505.0,117.6 507.9,116.2 510.8,114.7 513.7,113.3 516.6,112.5 519.5,112.5 522.4,112.5 525.3,112.5 528.2,112.5 531.1,112.5 534.0,112.5 536.9,112.5 539.8,112.5 542.7,112.5 545.6,112.5 548.5,112.5 551.4,112.5 554.3,112.5 557.2,112.5 560.1,112.5 563.0,112.5 565.9,112.5 568.8,112.5 571.7,112.5 574.6,112.5 577.5,112.5 580.4,112.5 583.3,112.5 586.2,112.5 589.1,112.5 592.0,112.5 594.9,112.5 597.8,112.5 600.7,112.5 603.6,112.5 606.5,112.5 609.4,112.5 612.3,112.5 615.2,112.5 618.1,112.5 621.0,112.5 623.9,112.5 626.8,112.5 629.7,112.5 632.6,112.5 635.5,112.5 638.4,112.5 641.3,112.5 644.2,112.5 647.1,112.5 650.0,112.5"/>
<circle cx="515.2" cy="112.5" r="3.5" fill="#3f9a6b"/><line x1="100" y1="96" x2="124" y2="96" stroke="#3f9a6b" stroke-width="2"/><text x="130" y="100" font-size="12">Chip C: 10 TOPS · 8.5 GB/s · ridge 1176</text>
<polyline fill="none" stroke-width="2" stroke="#888" points="316.5,329.9 319.4,328.5 322.3,327.0 325.2,325.6 328.1,324.1 331.0,322.7 333.9,321.2 336.8,319.8 339.7,318.3 342.6,316.9 345.5,315.4 348.4,314.0 351.3,312.5 354.2,311.1 357.1,309.6 360.0,308.2 362.9,306.7 365.8,305.3 368.7,303.8 371.6,302.4 374.5,300.9 377.4,299.5 380.3,298.0 383.2,296.6 386.1,295.1 389.0,293.7 391.9,292.2 394.8,290.8 397.7,289.3 400.6,287.9 403.5,286.4 406.4,285.0 409.3,283.5 412.2,282.1 415.1,280.6 418.0,279.2 420.9,277.7 423.8,276.3 426.7,274.8 429.6,273.4 432.5,271.9 435.4,270.5 438.3,269.0 441.2,267.6 444.1,266.1 447.0,264.7 449.9,263.2 452.8,261.8 455.7,260.3 458.6,258.9 461.5,257.4 464.4,256.0 467.3,254.5 470.2,253.1 473.1,251.6 476.0,250.2 478.9,248.7 481.8,247.3 484.7,245.8 487.6,244.4 490.5,242.9 493.4,241.5 496.3,240.0 499.2,238.6 502.1,237.1 505.0,235.7 507.9,234.2 510.8,232.8 513.7,231.3 516.6,229.9 519.5,228.4 522.4,227.0 525.3,225.5 528.2,224.1 531.1,222.6 534.0,221.2 536.9,219.7 539.8,218.3 542.7,216.8 545.6,215.4 548.5,213.9 551.4,212.5 554.3,211.0 557.2,209.6 560.1,208.1 563.0,206.8 565.9,206.8 568.8,206.8 571.7,206.8 574.6,206.8 577.5,206.8 580.4,206.8 583.3,206.8 586.2,206.8 589.1,206.8 592.0,206.8 594.9,206.8 597.8,206.8 600.7,206.8 603.6,206.8 606.5,206.8 609.4,206.8 612.3,206.8 615.2,206.8 618.1,206.8 621.0,206.8 623.9,206.8 626.8,206.8 629.7,206.8 632.6,206.8 635.5,206.8 638.4,206.8 641.3,206.8 644.2,206.8 647.1,206.8 650.0,206.8"/>
<circle cx="562.7" cy="206.8" r="3.5" fill="#888"/><line x1="100" y1="115" x2="124" y2="115" stroke="#888" stroke-width="2"/><text x="130" y="119" font-size="12">Chip D: 0.5 TOPS · 0.2 GB/s · ridge 2500</text><rect x="140.4" y="259.2" width="8" height="8" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="151.4" y="255.2" font-size="12">LLM decode (3)</text><rect x="299.8" y="179.5" width="8" height="8" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="310.8" y="175.5" font-size="12">ASR enc (41)</text><rect x="388.5" y="135.1" width="8" height="8" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="384.5" y="132.1" font-size="12" text-anchor="end">MobileNetV2 (168)</text>
<rect x="444.9" y="107.0" width="8" height="8" fill="none" stroke="#4a7bd0" stroke-width="2"/><text x="440.9" y="104.0" font-size="12" text-anchor="end">LLM prefill (410)</text>
<text x="78" y="26" font-size="13">□ = Chip A에서 우리 모델 4개의 위치 (괄호: op/byte)</text></svg>
```

그림 3 — 칩 4개의 INT8 roofline(피크 대역폭 기준)과 Chip A에서 우리 모델 4개의 위치(□, 괄호 안은 DRAM 기준 op/byte). 점은 ridge point다. LLM decode(3 op/byte)는 모든 칩에서 경사면(메모리 천장) 깊숙이 있다. 여기서는 칩 사이의 **세로 간격 = 대역폭 차이**만 의미가 있고, 지붕 높이(TOPS)는 상관없다. LLM prefill(410)도 A의 ridge(469)보다 왼쪽이라 아직 메모리에 묶인다. C의 ridge는 1176이라 우리 모델 대부분이 C에서는 메모리에 묶인다 — 단 SRAM 상주가 되는 작은 모델은 DRAM 바이트가 거의 0이라 이 그림의 x축 밖(오른쪽 끝)으로 간다. LLM 점들은 실제로 INT4라 연산 지붕이 2배 높지만, 메모리에 묶여 있으므로 결론은 같다.

### 3.6 벤더 숫자를 역산한다 (예제 2)

벤더 슬라이드의 숫자를 그대로 표에 넣지 않는다. 먼저 **"이 숫자를 내려면 활용률과 대역폭이 얼마여야 하나"**를 역산한다.

```
필요 활용률  u_need  = 2 · MAC / (peak_ops · t_claim)
필요 대역폭 비율     = DRAM_bytes / t_claim / BW_peak
```

말로 하면: 주장된 시간 안에 그 연산과 그 바이트를 처리하려면 칩이 얼마나 일해야 하는지를 계산한다. 1을 넘으면 **물리적으로 불가능**하다 — 벤더가 거짓말을 한다는 뜻이 아니라, **측정 조건이 우리 가정과 다르다**는 뜻이다(sparsity, batch, weight 상주, 더 높은 클럭, 다른 모델 변형). M4에서 조건 목록을 자세히 다룬다.

무엇을 확인하는 코드인가: 가상의 벤더 주장 6개를 우리 추정 범위와 비교하고, 필요한 활용률·대역폭을 역산해서 판정한다.

```python
# 예제 2: 벤더가 준 숫자를 "역산"한다 — 그 latency를 내려면 활용률·대역폭이 얼마여야 하나?
from m1_chips import CHIPS, MODELS
from m1_est import estimate, dram_bytes
claims = [("A", "mnv2", 0.45), ("B", "mnv2", 0.55), ("C", "mnv2", 0.08), ("D", "mnv2", 15.0),
          ("A", "slm_dec", 12.0), ("C", "slm_dec", 25.0)]           # 벤더 슬라이드 숫자 (가상)
print(f"{'chip':4s} {'model':8s} {'claim':>7s} {'우리 범위(opt~pes)':>18s} {'필요 util':>9s} {'필요 BW%':>8s}  판정")
for cn, mn, t in claims:
    c, m = CHIPS[cn], MODELS[mn]
    lo, hi = estimate(c, m, 0)[0], estimate(c, m, 2)[0]
    tops = c["tops4"] if m["prec"] == 4 else c["tops8"]
    u_need = 2 * m["mac"] / (tops * 1e12 * t * 1e-3)               # claim을 내려면 필요한 활용률
    bw_need = dram_bytes(c, m) / (t * 1e-3) / (c["bw"] * 1e9)      # 필요한 대역폭 / 피크
    verdict = "범위 안" if lo <= t <= hi else ("낙관보다 빠름 → 조건 확인" if t < lo else "비관보다 느림")
    if u_need > 1 or bw_need > 1: verdict = "물리적으로 불가 → 조건이 다름"
    print(f"{cn:4s} {mn:8s} {t:7.2f} {lo:8.3g} ~ {hi:<8.3g} {u_need:9.2f} {bw_need:8.0%}  {verdict}")
```

```text
chip model      claim     우리 범위(opt~pes)   필요 util   필요 BW%  판정
A    mnv2        0.45    0.387 ~ 0.755         0.11      31%  범위 안
B    mnv2        0.55     0.48 ~ 0.882         0.18      38%  범위 안
C    mnv2        0.08      0.4 ~ 2.4           0.75      15%  낙관보다 빠름 → 조건 확인
D    mnv2       15.00       20 ~ 25.8          0.08     120%  물리적으로 불가 → 조건이 다름
A    slm_dec    12.00       16 ~ 22            0.00      99%  낙관보다 빠름 → 조건 확인
C    slm_dec    25.00     47.8 ~ 81.2          0.00     143%  물리적으로 불가 → 조건이 다름
```

출력에서 볼 것과 벤더에게 보낼 질문:

- **C mnv2 0.08 ms**: dense 10 TOPS 기준 활용률 75%가 필요하다. 광고 20 TOPS(sparsity) 기준이면 38%로 그럴듯해진다 → 질문: "sparse weight로 잰 숫자인가? batch는? launch 오버헤드를 포함했나?" 우리 모델은 sparse가 아니다.
- **D mnv2 15 ms**: weight를 flash에서 읽는다면 피크의 120%가 필요하다 → 불가. 질문: "weight를 내부 SRAM/MRAM에 두고 잰 것인가? 그 모델의 weight는 int8 3.49 MB가 맞나?" 아마 더 작은 변형(width 0.5 등)이었을 것이다.
- **C LLM 25 ms/token**: 피크 대역폭의 143%가 필요하다 → 불가. 질문: "모델 크기·양자화(int4? int3?), context 길이, KV 포함 여부?" ctx가 짧고 weight가 더 작은 모델이면 가능하다.
- **A LLM 12 ms**: 피크의 99% — 이론상 가능하지만 실효 BW 99%는 현실적이지 않다(E7 3.4). 조건 확인 대상.

이것이 **benchmarketing**을 다루는 방법이다. 벤더 숫자를 무시하지도, 믿지도 않고 **역산해서 조건을 묻는다**. 답이 오면 그 조건을 우리 추정에 반영하거나(예: C의 상주 규칙 확인), 우리 조건으로 다시 재 달라고 요청한다.

### 3.7 함정

- **peak TOPS로 latency를 나눈다**: 활용률 100% 가정. E5 3.7절의 표처럼 depthwise·GEMV는 2~20%다. 활용률은 항상 범위로.
- **실효 대역폭 = 피크 대역폭**: LPDDR의 실효는 보통 피크의 50~80%이고, 카메라·디스플레이·CPU와 경합하면 더 내려간다(E7 3.4).
- **오버헤드를 뺀다**: 큰 모델에선 무시할 수 있지만 KWS·ASR 같은 작은 모델은 오버헤드가 전부일 수 있다(C의 ASR).
- **추정치를 실측처럼 보고한다**: 표에 "추정, 가정: u = 0.25~0.55"를 같이 적는다. 숫자만 돌아다니면 6개월 뒤 누군가 그것을 spec으로 믿는다.
- **우리 모델 대신 벤더 모델로 추정한다**: MobileNet은 모든 NPU가 최적화해 둔 모델이다. 추정과 PoC 모두 **우리 model zoo**로 한다(I5 4.2).

---

## 4. Always-on 섬은 따로 평가한다

### 4.1 왜 따로인가

웨어러블의 배터리 수명은 큰 NPU가 아니라 **하루 24시간 켜져 있는 작은 경로**가 정한다(I1 2절, D7 6절). wake word, 착용 감지, 제스처 같은 always-on 기능은 mW 이하에서 돌아야 하고, 이 경로는 메인 NPU와 완전히 다른 기준으로 평가해야 한다.

| 보는 것 | 메인 AP/NPU | always-on 섬 (AO MCU/DSP/microNPU) |
|---|---|---|
| 핵심 지표 | latency, tok/s, mJ/추론 | **평균 mW** (µW 단위 바닥 전력 포함) |
| 지배 항 | DRAM 대역폭, 활용률 | 바닥 전력(retention, 마이크 front-end), 전환 비용 |
| 메모리 | LPDDR GB | SRAM KB, retention 가능한 영역 |
| 소프트웨어 | Linux/Android + 벤더 런타임 | RTOS/bare-metal + TFLM·CMSIS-NN 류 (F2, J3) |
| 결정적 질문 | 우리 모델이 NPU에서 통째로 도나 | **잠들고 깨는 비용이 얼마인가**, 메인을 깨우는 데 몇 ms인가 |

### 4.2 공식과 손계산

```
P_avg = P_fe + P_ret + (P_act · t_inf + E_wake + E_reload) / T_hop
t_be  = (E_wake + E_reload) / (P_idle − P_ret)
```

- `P_fe`: 마이크 PDM·decimation·DMA처럼 항상 켜진 front-end
- `P_ret`: AO 코어가 SRAM을 유지한 채 자는 전력
- `E_wake`: sleep → run 전환 에너지(PLL lock, 전압 상승). `E_reload`: 깨어날 때 다시 읽어야 하는 weight(NPU SRAM이 retention되지 않으면)
- `t_be`: **break-even 시간**. hop 사이 틈이 이보다 짧으면 자지 말고 clock-gate로 기다리는 편이 낫다.

말로 하면: 평균 전력 = 항상 켜진 바닥 + (한 번 깨서 일하는 에너지) × (초당 횟수). mW·ms = µJ, µJ/ms = mW라서 단위가 깔끔하다.

Chip A의 LP-DSP를 손으로 (hop 40 ms):

```
한 hop 에너지 = 4.0 mW × 4.5 ms + 3.0 µJ = 18 + 3 = 21 µJ
P_avg        = 0.25 + 0.10 + 21 µJ / 40 ms = 0.35 + 0.525 = 0.875 mW   → gate(≤ 1 mW) 통과
hop 20 ms면  = 0.35 + 21 / 20 = 1.40 mW                               → 탈락
```

말로 하면: **같은 칩이 hop 설정 하나로 gate를 통과하기도, 탈락하기도 한다.** 그래서 gate 문장에는 조건(hop 40 ms, 모델 2.7 M MAC)이 반드시 같이 있어야 한다.

### 4.3 코드로 확인 — AO 경로 평균 전력 (예제 3, C)

무엇을 확인하는 코드인가: 네 칩의 AO 경로 숫자(가상)로 hop 40/20 ms의 평균 전력, break-even 시간, 키워드 후 메인 프로세서가 일할 수 있을 때까지의 시간을 계산한다. 펌웨어에서 power manager가 하는 계산을 그대로 C로 쓴다.

```c
/* 예제 3: always-on 경로(KWS) 평균 전력 — 상태 전환 비용까지 넣어서 gate(≤ 1 mW)를 본다. 숫자는 가상 */
#include <stdio.h>
typedef struct {
    const char *name;
    double p_fe_mw;   /* 마이크 PDM + decimation + DMA: 항상 켜짐 */
    double p_ret_mw;  /* AO 코어 retention sleep (SRAM 유지) */
    double p_idle_mw; /* 잠들지 않고 clock-gate로 기다릴 때 */
    double p_act_mw;  /* KWS 추론 중 */
    double t_inf_ms;  /* KWS 1회 (2.7 M MAC) */
    double e_wake_uj; /* sleep → run 전환 에너지 (PLL, 전압 상승) */
    double reload_kb; /* 깨어날 때 다시 읽어야 하는 weight (NPU SRAM 미유지) */
    double main_wake_ms; /* 키워드 후 메인 AP/NPU가 일할 수 있을 때까지 */
} ao_t;
static double avg_mw(const ao_t *c, double hop_ms) {
    double e_reload_uj = c->reload_kb * 1024 * 100e-12 * 1e6;        /* flash 읽기 100 pJ/B 가정 */
    double e_per_hop_uj = c->p_act_mw * c->t_inf_ms + c->e_wake_uj + e_reload_uj; /* mW·ms = µJ */
    return c->p_fe_mw + c->p_ret_mw + e_per_hop_uj / hop_ms;          /* µJ / ms = mW */
}
int main(void) {
    const ao_t c[] = {
        {"A: LP-DSP in SoC",  0.25, 0.10, 1.2, 4.0, 4.5, 3.0,  0, 5},
        {"B: AO microNPU",    0.20, 0.06, 0.9, 4.0, 1.5, 1.0, 25, 8},
        {"C: external MCU", 0.18, 0.05, 0.8, 5.0, 5.0, 0.1,  0, 350},
        {"D: MCU+microNPU",   0.15, 0.08, 0.7, 6.0, 1.0, 0.3,  0, 0},
    };
    printf("%-18s %9s %9s %9s %10s %s\n", "AO path", "hop40 mW", "hop20 mW", "t_be ms", "main ms", "gate(hop40 <= 1 mW)");
    for (unsigned i = 0; i < sizeof c / sizeof c[0]; i++) {
        double e_trans = c[i].e_wake_uj + c[i].reload_kb * 1024 * 100e-12 * 1e6;
        double t_be = e_trans / (c[i].p_idle_mw - c[i].p_ret_mw);      /* 이보다 짧은 틈이면 자지 않는 게 낫다 */
        double p40 = avg_mw(&c[i], 40), p20 = avg_mw(&c[i], 20);
        printf("%-18s %9.3f %9.3f %9.2f %10.0f %s\n", c[i].name, p40, p20, t_be,
               c[i].main_wake_ms, p40 <= 1.0 ? "PASS" : "FAIL");
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 ao.c -o ao && ./ao
```

```text
AO path             hop40 mW  hop20 mW   t_be ms    main ms gate(hop40 <= 1 mW)
A: LP-DSP in SoC       0.875     1.400      2.73          5 PASS
B: AO microNPU         0.499     0.738      4.24          8 PASS
C: external MCU        0.858     1.485      0.13        350 PASS
D: MCU+microNPU        0.387     0.545      0.48          0 PASS
```

출력에서 볼 것:

- **손계산과 일치**: A 0.875 / 1.400 mW. 경고 0개로 컴파일된다.
- **A와 C는 gate 경계에 있다** (0.875, 0.858). hop 20 ms면 둘 다 탈락. 실측 오차 ±15%만 생겨도 결과가 바뀔 수 있다 — 6.1절에서 이것을 **통과 확률**로 바꾼다.
- **B의 숨은 비용**: NPU SRAM이 sleep에서 유지되지 않아 매번 25 KB를 다시 읽는다. 한 hop 9.56 µJ 중 2.56 µJ(27%)가 reload다. datasheet의 "microNPU active 전력"만 보면 안 보이는 항이다. 질문: "NPU SRAM retention 모드가 있나? 그 모드의 전력은?"
- **C의 main wake 350 ms**: C는 AO 섬이 없어서 메인 칩을 꺼 두고 외부 MCU가 KWS를 한다. 키워드 후 메인 칩이 cold boot해야 해서 350 ms가 걸린다. I1의 "호출어 끝 → chime 300 ms" 예산을 이것 하나로 넘는다. 평균 전력 gate는 통과했지만 **latency gate에서 걸리는** 경우다. 하나의 숫자만 gate로 보면 이런 것을 놓친다.
- **t_be**: A는 hop 사이 틈(40 − 4.5 = 35.5 ms)이 t_be 2.73 ms보다 훨씬 길어서 자는 게 맞다. hop이 아주 짧은 기능(예: 고속 IMU 샘플마다 깨기)이라면 t_be와 비교해서 sleep 정책을 정한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<line x1="60" y1="200" x2="650" y2="200" stroke="currentColor"/><line x1="60" y1="30" x2="60" y2="200" stroke="currentColor"/><text x="355" y="236" font-size="13" text-anchor="middle">시간 (hop 40 ms 두 번, 가로 축척은 근사)</text><text x="22" y="115" font-size="13" text-anchor="middle" transform="rotate(-90 22 115)">전류(전력)</text><rect x="60" y="186" width="590" height="14" fill="#888" fill-opacity="0.3"/><text x="640" y="182" font-size="12" text-anchor="end">P_fe + P_ret (항상, 0.35 mW)</text><rect x="80" y="150" width="10" height="36" fill="#d0564a" fill-opacity="0.6"/><text x="85" y="144" font-size="12" text-anchor="middle">wake</text>
<rect x="90" y="70" width="60" height="116" fill="#4a7bd0" fill-opacity="0.5"/><text x="120" y="62" font-size="12" text-anchor="middle">KWS 4.5 ms</text><rect x="150" y="160" width="8" height="26" fill="#e08a3c" fill-opacity="0.6"/><text x="230" y="120" font-size="12" text-anchor="middle">retention sleep</text><text x="230" y="136" font-size="12" text-anchor="middle">(틈 35.5 ms ≫ t_be 2.7 ms)</text><rect x="370" y="150" width="10" height="36" fill="#d0564a" fill-opacity="0.6"/><rect x="380" y="70" width="60" height="116" fill="#4a7bd0" fill-opacity="0.5"/><rect x="440" y="160" width="8" height="26" fill="#e08a3c" fill-opacity="0.6"/>
<line x1="80" y1="40" x2="370" y2="40" stroke="currentColor" stroke-dasharray="4 3"/><text x="225" y="34" font-size="12" text-anchor="middle">T_hop = 40 ms</text><text x="540" y="96" font-size="12" text-anchor="middle">빨강 = 전환 에너지 E_wake</text><text x="540" y="114" font-size="12" text-anchor="middle">파랑 = P_act × t_inf</text><text x="540" y="132" font-size="12" text-anchor="middle">주황 = 뒷정리·재진입 (B는 reload)</text>
</svg>
```

그림 4 — AO 경로의 전력 파형 개념도(Chip A 기준, 축척 근사). 평균 전력은 회색 바닥 + (빨강 + 파랑 + 주황 면적) ÷ T_hop이다. datasheet의 "active 전력"은 파랑의 높이만 알려 준다. 바닥 높이(retention, front-end)와 빨강·주황(전환)은 TRM과 실측으로만 알 수 있고, mW 이하 영역에서는 이것들이 평균을 좌우한다. 실제 측정은 K3의 방법(GPIO 마커 + 이벤트 정렬 평균)으로 한다.

### 4.4 AO 섬 평가 체크리스트

- [ ] AO 도메인에서 **마이크 PDM/I2S와 IMU(I3C/SPI)를 메인 도메인 없이** 받을 수 있나? (front-end가 메인 도메인에 있으면 AO가 성립하지 않는다)
- [ ] retention SRAM 크기: KWS 모델 weight + arena + 오디오 링버퍼가 들어가나? (I1 4.1의 SRAM 지도)
- [ ] NPU/DSP SRAM이 sleep에서 유지되나, 매번 reload인가?
- [ ] power state 표: 각 상태의 전력, 진입·탈출 시간, 깨울 수 있는 소스(타이머, GPIO, 오디오 VAD, IMU 인터럽트)
- [ ] 하드웨어 VAD(음성 활동 감지)나 IMU의 FIFO·wake-on-motion으로 AO 코어조차 재울 수 있나?
- [ ] 키워드 후 메인 AP/NPU 깨우기 latency: retention resume인지 cold boot인지, DRAM self-refresh 탈출 시간 포함
- [ ] AO 소프트웨어: 어떤 RTOS·컴파일러, TFLM/CMSIS-NN 류 지원, AO 코어 디버그 접근(SWD가 메인과 별도인가)
- [ ] AO와 메인 사이 통신: mailbox/IPC, 공유 메모리, 메시지 latency
- [ ] 실측 가능성: AO 레일을 따로 잴 수 있게 eval kit에 측정 지점(shunt, 점퍼)이 있나 (K3 3.2)

### 4.5 함정

- **"typical sleep 전류"를 그대로 쓴다**: 어떤 상태·온도·전압인지 확인. retention SRAM 크기에 따라 수 배 달라진다.
- **AO 평가를 메인 칩 평가에 묻는다**: 메인 NPU가 훌륭해도 AO가 1 mW를 못 지키면 배터리 목표가 무너진다. **gate는 AO에 따로 둔다.**
- **외부 MCU를 붙이면 해결된다고 본다**: BOM·보드 면적·두 번째 펌웨어·칩 간 프로토콜·메인 wake latency가 따라온다(C의 350 ms). 5절의 공수·TCO에 반드시 반영한다.

---

## 5. 비용·공수·리스크 — 기술 밖의 기준을 숫자로

### 5.1 엔지니어링 공수 — 소프트웨어 성숙도를 person-month로 바꾼다

"C는 툴체인이 미성숙하다"는 말은 결정 회의에서 힘이 없다. "C를 고르면 EVT까지 공수가 29 PM(P50)이고, 우리 팀 24 PM을 넘을 확률이 96%다"는 힘이 있다. **소프트웨어 성숙도는 공수와 일정 리스크로 번역**할 때 결정에 반영된다.

공수는 작업 항목별 **3점 추정**(낙관 o, 가능성 높음 m, 비관 p)으로 한다. PERT 평균은 이렇다.

```
PERT 평균 = (o + 4m + p) / 6        표준편차 ≈ (p − o) / 6
```

말로 하면: 가장 그럴듯한 값에 4배 무게를 주고 양 끝을 조금 섞는다. 고전적인 일정 추정법이다(참고 자료의 Malcolm et al. 1959).

항목은 Don에게 익숙한 bring-up 단위로 나눈다: BSP·드라이버, 전원 관리(AO 포함), 툴체인·CI 통합, LLM 런타임, 그리고 **op gap**(가속기가 지원하지 않는 우리 모델의 op 하나당 공수 — custom kernel, 그래프 수술, 모델 수정; F8 4.4). op gap 개수는 op sweep(F8 3.3)으로 세고, 개당 공수는 툴체인 성숙도가 정한다.

무엇을 확인하는 코드인가: 항목별 삼각분포를 2만 번 뽑아 합해서 P10~P90과 "팀 공수 24 PM(4명 × 6개월)을 넘을 확률"을 계산한다.

```python
# 예제 4: 엔지니어링 공수(person-month)를 3점 추정 → Monte Carlo로 P50/P80, 일정 초과 확률
import numpy as np
rng = np.random.default_rng(7)
# 작업 항목별 (낙관, 가능성 높음, 비관) PM. op gap은 "개수 × 개당 공수". 모든 숫자는 가상
WORK = {
  "A": dict(bsp=(2, 3, 5), power=(1, 2, 3), tool=(0.5, 1, 2), llm=(1, 2, 3),   gaps=1, per_op=(0.3, 0.6, 1.5)),
  "B": dict(bsp=(2, 4, 7), power=(1, 2, 4), tool=(0.5, 1, 2), llm=(1.5, 3, 5), gaps=3, per_op=(0.3, 0.8, 2.0)),
  "C": dict(bsp=(4, 7, 14), power=(2, 3, 6), tool=(1, 2, 4), llm=(3, 5, 9),    gaps=6, per_op=(0.5, 1.0, 3.0)),
  "D": dict(bsp=(1, 2, 3), power=(0.5, 1, 2), tool=(0.5, 1, 1.5), llm=(0, 0, 0), gaps=2, per_op=(0.3, 0.6, 1.5)),
}
N, TEAM_PM = 20000, 4 * 6            # 4명 × 6개월 = EVT까지 쓸 수 있는 공수
tri = lambda t: rng.triangular(t[0], t[1], t[2], N) if t[2] > t[0] else np.full(N, t[1])
pert = lambda t: (t[0] + 4 * t[1] + t[2]) / 6
print(f"{'chip':4s} {'PERT 합':>8s}" + "".join(f"{'P'+str(q):>6s}" for q in (10, 50, 80, 90)) + f" {'P(>24 PM)':>10s}  op gap 몫(P50)")
for cn, w in WORK.items():
    items = [tri(w[k]) for k in ("bsp", "power", "tool", "llm")]
    ops = sum(tri(w["per_op"]) for _ in range(w["gaps"]))   # op마다 독립적으로 뽑는다
    total = sum(items) + ops
    pert_sum = sum(pert(w[k]) for k in ("bsp", "power", "tool", "llm")) + w["gaps"] * pert(w["per_op"])
    qs = "".join(f"{np.percentile(total, q):6.1f}" for q in (10, 50, 80, 90))
    print(f"{cn:4s} {pert_sum:8.1f}{qs} {np.mean(total > TEAM_PM):10.1%}  {np.median(ops):5.1f}")
```

```text
chip   PERT 합   P10   P50   P80   P90  P(>24 PM)  op gap 몫(P50)
A         8.9   8.1   9.3  10.1  10.6       0.0%    0.8
B        13.2  12.1  14.1  15.4  16.1       0.0%    3.1
C        26.0  25.2  28.9  31.5  32.9      96.1%    8.9
D         5.5   4.9   5.8   6.3   6.6       0.0%    1.6
```

출력에서 볼 것:

- **Monte Carlo P50이 PERT 합보다 크다** (C: 28.9 vs 26.0). 삼각분포의 평균은 (o + m + p)/3이라 PERT보다 비관 쪽 꼬리를 더 무겁게 반영한다. 공수 추정은 오른쪽으로 꼬리가 길어서(늦어지는 방법은 많고 빨라지는 방법은 적다) **P50이 아니라 P80으로 계획**하는 것이 보통이다.
- **C의 op gap 몫이 8.9 PM** — 전체의 30%. op gap 개수가 공수를 지배한다. 그래서 PoC 1주차에 op sweep을 한다(7.5절).
- **C는 96% 확률로 팀 용량을 넘는다**. 이것은 C를 탈락시키는 숫자가 아니라 **선택지를 만드는** 숫자다: 사람을 더 쓰거나(→ 비용), EVT를 미루거나(→ 일정), 벤더 엔지니어링 지원을 계약에 넣거나(→ NRE). 결정 회의에 이 세 가지를 같이 올린다.

### 5.2 TCO — BOM과 NRE의 trade-off, break-even 수량

칩 비용은 단가 하나가 아니다.

```
TCO(V)       = 고정비 + V × 단위비용
고정비       = NRE (SDK·지원 계약·개발 키트·벤더 엔지니어링 서비스) + 공수 × 인건비
단위비용     = 칩 + DRAM + PMIC + 동반 칩(외부 MCU, radio, secure element) + 단위 royalty
대당 총비용  = 고정비 / V + 단위비용
break-even   V* = (고정비_y − 고정비_x) / (단위비용_x − 단위비용_y)
```

말로 하면: 고정비는 수량이 늘수록 대당 몫이 줄고, 단위비용은 그대로 남는다. 고정비는 싸고 단가가 비싼 칩 x와, 고정비는 비싸고 단가가 싼 칩 y가 있으면, **어느 수량부터 y가 이기는지**가 break-even이다.

B→C를 손으로: 고정비 차 = 1,030k − 388k = 642k$, 단가 차 = 26.3 − 25.5 = 0.8$ → break-even = 642,000 / 0.8 = **802,500대**. 단가가 0.8$ 싸 봐야 고정비 차이를 메우려면 80만 대를 팔아야 한다.

무엇을 확인하는 코드인가: 칩별 단위비용·고정비로 수량별 대당 총비용과 break-even 수량을 계산한다. 공수는 예제 4의 P80을 쓴다.

```python
# 예제 5: TCO = 고정비(NRE + 엔지니어링) + 수량 × 단위 비용 → 수량별 순위와 break-even 수량
COST_PER_PM = 20_000                      # 1 person-month 인건비(loaded) 가정, USD
C = {  # unit = 칩 + DRAM + PMIC + 동반 칩(외부 MCU·BLE 등) + 단위 royalty. nre = SDK·지원 계약·개발 키트·벤더 서비스
  "A": dict(unit=30 + 9 + 2.5,             nre=150_000, pm=10.1),   # pm = 예제 4의 P80
  "B": dict(unit=19 + 5 + 2.0 + 0.3,       nre=80_000,  pm=15.4),
  "C": dict(unit=14 + 3.5 + 3 + 2.5 + 2.0 + 0.5, nre=400_000, pm=31.5),
  "D": dict(unit=7 + 1.5 + 2.5,            nre=30_000,  pm=6.3),
}
for k, v in C.items(): v["fixed"] = v["nre"] + v["pm"] * COST_PER_PM
print(f"{'chip':4s} {'unit $':>7s} {'fixed $k':>9s}" + "".join(f"{f'{V//1000}k':>9s}" for V in (10_000, 50_000, 200_000, 1_000_000)))
for k, v in C.items():
    row = "".join(f"{(v['fixed'] / V + v['unit']):9.2f}" for V in (10_000, 50_000, 200_000, 1_000_000))
    print(f"{k:4s} {v['unit']:7.2f} {v['fixed'] / 1e3:9.0f}{row}")   # 칸 = 대당 총비용(고정비 상각 포함)
def breakeven(x, y):        # x가 고정비는 싸고 단가는 비싼 쪽일 때, y가 따라잡는 수량
    du = C[x]["unit"] - C[y]["unit"]; df = C[y]["fixed"] - C[x]["fixed"]
    return df / du if du > 0 and df > 0 else None
for x, y in (("A", "B"), ("B", "C"), ("A", "C")):
    v = breakeven(x, y)
    print(f"break-even {x}→{y}: " + (f"{v:,.0f} 대 이상이면 {y}가 더 싸다" if v else "없음"))
```

```text
chip  unit $  fixed $k      10k      50k     200k    1000k
A      41.50       352    76.70    48.54    43.26    41.85
B      26.30       388    65.10    34.06    28.24    26.69
C      25.50      1030   128.50    46.10    30.65    26.53
D      11.00       156    26.60    14.12    11.78    11.16
break-even A→B: 2,368 대 이상이면 B가 더 싸다
break-even B→C: 802,500 대 이상이면 C가 더 싸다
break-even A→C: 42,375 대 이상이면 C가 더 싸다
```

출력에서 볼 것:

- **손계산과 일치**: B→C 802,500대.
- **C의 칩 단가(14$)는 가장 싸지만 단위비용은 B와 비슷하다**: 외부 MCU 3$ + 외부 BLE 2.5$ + royalty 0.5$가 붙는다. "칩 가격"과 "칩 때문에 드는 단위비용"은 다르다.
- **수량이 결론을 바꾼다**: 10k대에서 C는 대당 128$로 최악, 1M대에서는 B와 거의 같다. 스타트업 제품의 1세대 수량에서는 고정비가 지배한다.
- **A→B break-even이 2,368대**: 사실상 항상 B가 싸다. A를 고른다면 비용이 아닌 이유(성능 여유, 로드맵)여야 한다.

```svg
<svg viewBox="0 0 680 350" xmlns="http://www.w3.org/2000/svg">
<line x1="70" y1="290" x2="600" y2="290" stroke="currentColor"/><line x1="70" y1="30" x2="70" y2="290" stroke="currentColor"/><line x1="70.0" y1="290" x2="70.0" y2="295" stroke="currentColor"/><text x="70.0" y="309" font-size="12" text-anchor="middle">10k</text><line x1="300.3" y1="290" x2="300.3" y2="295" stroke="currentColor"/><text x="300.3" y="309" font-size="12" text-anchor="middle">100k</text><line x1="530.7" y1="290" x2="530.7" y2="295" stroke="currentColor"/><text x="530.7" y="309" font-size="12" text-anchor="middle">1M</text><line x1="65" y1="290.0" x2="70" y2="290.0" stroke="currentColor"/><text x="62" y="294.0" font-size="12" text-anchor="end">0</text>
<line x1="65" y1="252.9" x2="70" y2="252.9" stroke="currentColor"/><text x="62" y="256.9" font-size="12" text-anchor="end">20</text><line x1="65" y1="215.7" x2="70" y2="215.7" stroke="currentColor"/><text x="62" y="219.7" font-size="12" text-anchor="end">40</text><line x1="65" y1="178.6" x2="70" y2="178.6" stroke="currentColor"/><text x="62" y="182.6" font-size="12" text-anchor="end">60</text><line x1="65" y1="141.4" x2="70" y2="141.4" stroke="currentColor"/><text x="62" y="145.4" font-size="12" text-anchor="end">80</text><line x1="65" y1="104.3" x2="70" y2="104.3" stroke="currentColor"/><text x="62" y="108.3" font-size="12" text-anchor="end">100</text>
<line x1="65" y1="67.1" x2="70" y2="67.1" stroke="currentColor"/><text x="62" y="71.1" font-size="12" text-anchor="end">120</text><line x1="65" y1="30.0" x2="70" y2="30.0" stroke="currentColor"/><text x="62" y="34.0" font-size="12" text-anchor="end">140</text><text x="335.0" y="330" font-size="13" text-anchor="middle">생산 수량 (대, 로그 축)</text><text x="20" y="160.0" font-size="13" text-anchor="middle" transform="rotate(-90 20 160.0)">대당 총비용 $ (고정비 상각 포함)</text>
<polyline fill="none" stroke-width="2" stroke="#4a7bd0" points="70.0,147.6 76.6,151.7 83.2,155.7 89.9,159.3 96.5,162.8 103.1,166.0 109.8,169.0 116.4,171.8 123.0,174.4 129.6,176.9 136.2,179.2 142.9,181.4 149.5,183.4 156.1,185.3 162.8,187.1 169.4,188.7 176.0,190.3 182.6,191.7 189.2,193.1 195.9,194.4 202.5,195.5 209.1,196.7 215.8,197.7 222.4,198.7 229.0,199.6 235.6,200.4 242.2,201.2 248.9,202.0 255.5,202.7 262.1,203.4 268.8,204.0 275.4,204.5 282.0,205.1 288.6,205.6 295.2,206.1 301.9,206.5 308.5,206.9 315.1,207.3 321.8,207.7 328.4,208.0 335.0,208.3 341.6,208.6 348.2,208.9 354.9,209.1 361.5,209.4 368.1,209.6 374.8,209.8 381.4,210.0 388.0,210.2 394.6,210.4 401.2,210.5 407.9,210.7 414.5,210.8 421.1,211.0 427.8,211.1 434.4,211.2 441.0,211.3 447.6,211.4 454.2,211.5 460.9,211.6 467.5,211.7 474.1,211.8 480.8,211.9 487.4,211.9 494.0,212.0 500.6,212.0 507.2,212.1 513.9,212.2 520.5,212.2 527.1,212.3 533.8,212.3 540.4,212.3 547.0,212.4 553.6,212.4 560.2,212.4 566.9,212.5 573.5,212.5 580.1,212.5 586.8,212.6 593.4,212.6 600.0,212.6"/>
<text x="606" y="216.6" font-size="12">A</text>
<polyline fill="none" stroke-width="2" stroke="#e08a3c" points="70.0,169.1 76.6,173.7 83.2,178.0 89.9,182.1 96.5,185.9 103.1,189.4 109.8,192.7 116.4,195.8 123.0,198.7 129.6,201.5 136.2,204.0 142.9,206.4 149.5,208.6 156.1,210.7 162.8,212.6 169.4,214.5 176.0,216.2 182.6,217.8 189.2,219.3 195.9,220.7 202.5,222.0 209.1,223.2 215.8,224.4 222.4,225.4 229.0,226.5 235.6,227.4 242.2,228.3 248.9,229.1 255.5,229.9 262.1,230.6 268.8,231.3 275.4,231.9 282.0,232.5 288.6,233.1 295.2,233.6 301.9,234.1 308.5,234.5 315.1,234.9 321.8,235.3 328.4,235.7 335.0,236.1 341.6,236.4 348.2,236.7 354.9,237.0 361.5,237.2 368.1,237.5 374.8,237.7 381.4,238.0 388.0,238.2 394.6,238.3 401.2,238.5 407.9,238.7 414.5,238.9 421.1,239.0 427.8,239.1 434.4,239.3 441.0,239.4 447.6,239.5 454.2,239.6 460.9,239.7 467.5,239.8 474.1,239.9 480.8,240.0 487.4,240.0 494.0,240.1 500.6,240.2 507.2,240.2 513.9,240.3 520.5,240.4 527.1,240.4 533.8,240.5 540.4,240.5 547.0,240.5 553.6,240.6 560.2,240.6 566.9,240.7 573.5,240.7 580.1,240.7 586.8,240.7 593.4,240.8 600.0,240.8"/>
<text x="606" y="236.8" font-size="12">B</text>
<polyline fill="none" stroke-width="2" stroke="#3f9a6b" points="70.0,51.4 76.6,63.6 83.2,75.1 89.9,85.8 96.5,95.9 103.1,105.3 109.8,114.1 116.4,122.3 123.0,130.0 129.6,137.2 136.2,144.0 142.9,150.3 149.5,156.2 156.1,161.8 162.8,167.0 169.4,171.8 176.0,176.3 182.6,180.6 189.2,184.6 195.9,188.3 202.5,191.8 209.1,195.0 215.8,198.1 222.4,200.9 229.0,203.6 235.6,206.1 242.2,208.5 248.9,210.6 255.5,212.7 262.1,214.6 268.8,216.4 275.4,218.1 282.0,219.7 288.6,221.1 295.2,222.5 301.9,223.8 308.5,225.0 315.1,226.1 321.8,227.2 328.4,228.2 335.0,229.1 341.6,230.0 348.2,230.8 354.9,231.6 361.5,232.3 368.1,232.9 374.8,233.6 381.4,234.1 388.0,234.7 394.6,235.2 401.2,235.7 407.9,236.1 414.5,236.5 421.1,236.9 427.8,237.3 434.4,237.6 441.0,238.0 447.6,238.3 454.2,238.5 460.9,238.8 467.5,239.0 474.1,239.3 480.8,239.5 487.4,239.7 494.0,239.9 500.6,240.1 507.2,240.2 513.9,240.4 520.5,240.5 527.1,240.7 533.8,240.8 540.4,240.9 547.0,241.0 553.6,241.1 560.2,241.2 566.9,241.3 573.5,241.4 580.1,241.5 586.8,241.6 593.4,241.6 600.0,241.7"/>
<text x="606" y="253.7" font-size="12">C</text>
<polyline fill="none" stroke-width="2" stroke="#888" points="70.0,240.6 76.6,242.5 83.2,244.2 89.9,245.8 96.5,247.3 103.1,248.8 109.8,250.1 116.4,251.3 123.0,252.5 129.6,253.6 136.2,254.6 142.9,255.6 149.5,256.5 156.1,257.3 162.8,258.1 169.4,258.8 176.0,259.5 182.6,260.2 189.2,260.8 195.9,261.3 202.5,261.9 209.1,262.4 215.8,262.8 222.4,263.3 229.0,263.7 235.6,264.0 242.2,264.4 248.9,264.7 255.5,265.0 262.1,265.3 268.8,265.6 275.4,265.9 282.0,266.1 288.6,266.3 295.2,266.5 301.9,266.7 308.5,266.9 315.1,267.1 321.8,267.2 328.4,267.4 335.0,267.5 341.6,267.7 348.2,267.8 354.9,267.9 361.5,268.0 368.1,268.1 374.8,268.2 381.4,268.3 388.0,268.4 394.6,268.4 401.2,268.5 407.9,268.6 414.5,268.6 421.1,268.7 427.8,268.8 434.4,268.8 441.0,268.9 447.6,268.9 454.2,268.9 460.9,269.0 467.5,269.0 474.1,269.1 480.8,269.1 487.4,269.1 494.0,269.2 500.6,269.2 507.2,269.2 513.9,269.2 520.5,269.3 527.1,269.3 533.8,269.3 540.4,269.3 547.0,269.3 553.6,269.3 560.2,269.4 566.9,269.4 573.5,269.4 580.1,269.4 586.8,269.4 593.4,269.4 600.0,269.4"/>
<text x="606" y="273.4" font-size="12">D</text><circle cx="214.4" cy="197.5" r="4" fill="#d0564a"/><text x="220.4" y="187.5" font-size="12">A=C @ 42k</text><circle cx="508.7" cy="240.3" r="4" fill="#d0564a"/><text x="514.7" y="230.3" font-size="12">B=C @ 802k</text>
</svg>
```

그림 5 — 수량별 대당 총비용(고정비 상각 포함, 예제 5 숫자). 빨간 점이 break-even이다. C(초록)는 왼쪽에서 가장 비싸고 오른쪽으로 갈수록 B(주황)에 붙는다. 이 그림 한 장이 "우리 1세대 수량이 얼마인가?"라는 질문을 결정 회의의 중심으로 끌어온다. 수량 전망이 불확실하면 10k / 100k / 1M 세 시나리오를 다 보여 준다.

실무에서 더 넣을 것: 단가는 수량 구간마다 다르고(price break), 연간 가격 인하 조건이 붙기도 한다. DRAM 가격은 시장에 따라 크게 움직인다. 이런 것은 단위비용을 범위로 넣어서(6절) 다룬다.

### 5.3 공급·수명·벤더 리스크 — 기대 비용으로

Don이 양산에서 겪어 봤을 일들이다: 부품 allocation으로 생산이 멈추고, EOL 공지가 오고, 대체 부품으로 보드를 다시 돌린다. 칩 선정에서는 이것을 **리스크 등록부**(risk register)로 만들고, 각 항목에 **확률 × 영향 비용**을 붙인다.

| 리스크 유형 | 무엇을 묻나 (RFI·RFQ) | 줄이는 법 |
|---|---|---|
| allocation | 공급 부족 시 우선순위, 최소 주문 수량, lead time 이력 | 장기 계약, buffer stock, 2nd source |
| 수명 (EOL) | 수명 보장 기간(longevity program), EOL 공지 기간, last-time-buy 조건 | 계약서에 명시, 후속 pin 호환 칩 |
| 2nd source | 같은 칩의 다른 공급 경로, pin 호환 대체품 | 보드 설계 시 대체 footprint |
| 벤더 존속 | 재무 상태, 투자 단계, 고객 수 (스타트업) | escrow(소스·툴 위탁), 단계적 계약 |
| 소프트웨어 정책 | SDK 유료화, 지원 종료, license 조건 변경 | 계약에 지원 기간·조건 명시 |
| 로드맵 | 다음 세대 일정, 우리 제품 2세대와의 정렬 | NDA 로드맵, 마일스톤 연동 |

무엇을 확인하는 코드인가: 후보별 리스크 2~3개(가상 확률·비용)를 독립 Bernoulli로 10만 번 뽑아서 기대 비용, "하나라도 터질 확률", P90 비용을 계산한다.

```python
# 예제 6: 공급·수명·벤더 리스크 등록부 → 기대 비용과 "나쁜 해(P90)" 비용 (확률·영향은 가상)
import numpy as np
rng = np.random.default_rng(11)
RISKS = {  # (이름, 제품 수명 3년 동안 일어날 확률, 일어나면 드는 비용 USD)
  "A": [("allocation(작은 고객 후순위)", 0.25, 300e3), ("EOL < 5년", 0.10, 500e3), ("SDK 유료화·정책 변경", 0.10, 100e3)],
  "B": [("allocation", 0.15, 300e3), ("EOL < 5년", 0.15, 500e3), ("BSP 지원 종료", 0.10, 200e3)],
  "C": [("벤더 도산·인수", 0.20, 1.5e6), ("양산 수율·공급 지연", 0.30, 400e3), ("로드맵 칩 지연", 0.30, 200e3)],
  "D": [("allocation", 0.10, 200e3), ("EOL < 5년", 0.05, 300e3)],
}
N = 100_000
print(f"{'chip':4s} {'기대비용 $k':>11s} {'P(≥1건)':>8s} {'P90 $k':>8s}  가장 큰 항목")
for cn, rs in RISKS.items():
    hits = np.stack([rng.random(N) < p for _, p, _ in rs])           # 리스크마다 독립 Bernoulli
    cost = (hits * np.array([c for *_, c in rs])[:, None]).sum(0)
    ev = sum(p * c for _, p, c in rs)
    top = max(rs, key=lambda r: r[1] * r[2])
    print(f"{cn:4s} {ev / 1e3:11.0f} {hits.any(0).mean():8.1%} {np.percentile(cost, 90) / 1e3:8.0f}  {top[0]}")
```

```text
chip     기대비용 $k   P(≥1건)   P90 $k  가장 큰 항목
A            135    39.3%      400  allocation(작은 고객 후순위)
B            140    35.0%      500  EOL < 5년
C            480    60.8%     1700  벤더 도산·인수
D             35    14.5%      200  allocation
```

출력에서 볼 것:

- **손으로 확인**: A의 P(≥1건) = 1 − 0.75 × 0.9 × 0.9 = 0.3925 → 39.3%. 기대 비용 = 0.25 × 300 + 0.1 × 500 + 0.1 × 100 = 135k$.
- **기대값과 P90이 다른 이야기를 한다**: C의 기대 비용은 480k$지만 나쁜 해에는 1.7M$다. 작은 회사에게 1.7M$는 기대값 계산이 의미 없을 만큼 치명적일 수 있다. 그래서 리스크는 **기대값과 꼬리를 둘 다** 보고하고, 꼬리가 회사가 감당 못 하는 수준이면 점수가 아니라 **gate나 완화 조건**(escrow, 단계 계약)으로 다룬다.
- **A의 최대 리스크가 "작은 고객 후순위"**: 큰 벤더의 칩은 기술적으로 안전하지만, 스마트폰 고객과 같은 공장 용량을 나눠 쓰는 작은 웨어러블 회사는 공급 부족 때 뒤로 밀린다. 벤더 지원 기준(FAE 접근성)도 같은 이유로 A가 낮다.
- 확률과 비용은 supply chain·finance 팀과 같이 정한다. ML 엔지니어 혼자 정하면 안 되는 숫자다.

### 5.4 정성 기준을 숫자로 — rubric

소프트웨어 성숙도, 벤더 지원, 로드맵은 1~5 rubric으로 점수화한다. 핵심은 **각 점수의 정의를 관찰 가능한 사실로** 쓰는 것이다. "좋다/나쁘다"가 아니라 "평가 기간에 이것이 일어났다/안 일어났다".

| 점수 | 툴체인·SW 성숙도 정의 (예) |
|---|---|
| 5 | 우리 model zoo 전부가 문서대로 변환·컴파일되고 CPU fallback 0, CLI·컨테이너 지원, 레이어별 프로파일러, 공개 릴리스 노트·errata |
| 4 | 1~2개 모델에 작은 수정(op 교체) 필요, 프로파일러 있음, 문서에 빈 곳이 있으나 FAE가 1주 안에 답 |
| 3 | 여러 모델에 그래프 수술 필요, 일부 op가 CPU fallback, 프로파일러는 GUI 전용, 릴리스마다 호환성 문제 1~2건 |
| 2 | 대표 모델 중 하나가 컴파일 안 됨, 오류 메시지로 원인을 알 수 없음, 벤더 엔지니어가 직접 만져야 동작 |
| 1 | 우리 모델로 end-to-end 결과를 아직 못 봄 |

| 점수 | 벤더 지원 정의 (예) |
|---|---|
| 5 | 일부러 보낸 재현 케이스(F8 7절)에 2영업일 안에 root cause, 담당 FAE·AE 배정, errata 공개 |
| 3 | 1~2주 안에 workaround, 담당자는 있지만 공유 큐 |
| 1 | 응답 없음, 또는 "다음 릴리스에서 고려"만 반복 |

평가자는 두 명 이상이 따로 매기고, 차이가 크면 근거를 보고 합의한다. 점수와 함께 **근거 링크**(버그 티켓, 로그)를 남긴다. 한 명이 매긴 정성 점수는 그 사람의 선호다.

---

## 6. 결정 — gate, 효용, 가중 점수, 그리고 불확실성

### 6.1 gate를 확률로 본다 (예제 7)

점 추정으로 보면 A~C 모두 AO gate를 "PASS"했다(예제 3). 그런데 A는 0.875 mW, 한계 1.0 mW까지 여유가 12.5%뿐이다. 측정 오차와 보드 차이를 생각하면 **"PASS"가 아니라 "높은 확률로 PASS"**다.

점수표를 범위로 적는다. 앞 예제들의 출력을 가져오고, 점 추정만 있는 항목(AO 전력, 비용, 리스크)에는 불확실성 폭을 붙인다. 측정값은 대개 나빠지는 쪽으로 더 길게 붙인다 — 실제 보드는 eval kit보다 거의 항상 나쁘다.

```python
# m1_score.py — 점수표: 기준마다 (낮음, 가능성 높음, 높음) 범위. 앞 예제들의 출력을 옮겨 왔다 (모두 가상 칩)
import numpy as np
NAN = (np.nan,) * 3
AO = {"A": 0.875, "B": 0.499, "C": 0.858, "D": 0.387}               # 예제 3 hop 40 ms
DATA = {   # tps·e_tok: 예제 1 / cost: 예제 5 (100k대, 고정비 상각) / pm: 예제 4 P10·P50·P90 / risk: 예제 6 기대비용 $k
  "A": dict(tps=(45.4, 54.1, 62.6), e_tok=(9.79, 12.9, 17.0), sw=(3.5, 4.5, 5), vendor=(2, 3, 4),   cost=45.02, pm=(8.1, 9.3, 10.6),  risk=135, road=(3, 4, 5)),
  "B": dict(tps=(30.6, 36.2, 42.0), e_tok=(11.3, 15.4, 20.1), sw=(3, 3.5, 4.5), vendor=(3, 4, 4.5), cost=30.18, pm=(12.1, 14.1, 16.1), risk=140, road=(2, 3, 4)),
  "C": dict(tps=(12.3, 16.6, 20.9), e_tok=(11.2, 16.0, 22.9), sw=(1, 2, 3.5),   vendor=(3, 4.5, 5), cost=35.80, pm=(25.2, 28.9, 32.9), risk=480, road=(2, 3.5, 5)),
  "D": dict(tps=(0, 0, 0),          e_tok=NAN,                sw=(3, 3.5, 4),   vendor=(3, 3.5, 4), cost=12.56, pm=(4.9, 5.8, 6.6),   risk=35,  road=(2, 3, 4)),
}
for k, d in DATA.items():          # 점 추정만 있는 항목에 불확실성 폭을 붙인다 (측정은 보통 나빠지는 쪽으로 더 길다)
    d["ao"] = (AO[k] * 0.85, AO[k], AO[k] * 1.20)
    d["cost"] = (d["cost"] * 0.90, d["cost"], d["cost"] * 1.15)
    d["risk"] = (d["risk"] * 0.6, d["risk"], d["risk"] * 1.8)
# 효용 앵커: (0점 = 받아들일 수 있는 최악, 1점 = 이 이상은 가치 없음). 후보가 아니라 요구사항(I1)에서 온다
ANCHOR = dict(tps=(10, 40), e_tok=(25, 8), ao=(1.0, 0.3), sw=(1, 5), vendor=(1, 5),
              cost=(50, 15), pm=(30, 6), risk=(600, 0), road=(1, 5))
WEIGHT = dict(tps=.15, e_tok=.10, ao=.15, sw=.15, vendor=.05, cost=.15, pm=.10, risk=.10, road=.05)
def utility(crit, x):
    lo, hi = ANCHOR[crit]
    return np.clip((np.asarray(x, float) - lo) / (hi - lo), 0, 1)
def gates(d_sample):               # must-have: 하나라도 실패하면 점수 계산 전에 탈락
    return (d_sample["ao"] <= 1.0) & (d_sample["tps"] >= 10)
```

비용 행의 100k대 값은 예제 5의 고정비를 100k대로 나눈 것이다: A = 352k/100k + 41.5 = 45.02$. tok/s 가운데 값은 예제 1의 기준 latency에서 1000 / 18.5 = 54.1 식으로 계산했다. 보안·인터페이스 gate는 네 칩 모두 통과(C는 외부 BLE를 BOM에 넣는 조건)라서 코드에서 생략했다.

무엇을 확인하는 코드인가: AO 전력과 tok/s를 삼각분포로 2만 번 뽑아서 gate 통과 확률을 계산한다.

```python
# 예제 7: gate를 점 추정으로 보면 "PASS"인데, 범위로 보면 통과 확률이 얼마인가
import numpy as np
from m1_score import DATA
rng = np.random.default_rng(3); N = 20000
tri = lambda t: rng.triangular(*t, N) if t[2] > t[0] else np.full(N, t[1])
print(f"{'chip':4s} {'AO 점추정':>9s} {'P(AO≤1mW)':>10s} {'tok/s 점':>8s} {'P(≥10)':>7s} {'P(둘 다)':>8s}")
for k, d in DATA.items():
    ao, tps = tri(d["ao"]), tri(d["tps"])
    g1, g2 = ao <= 1.0, tps >= 10
    print(f"{k:4s} {d['ao'][1]:9.3f} {g1.mean():10.1%} {d['tps'][1]:8.1f} {g2.mean():7.1%} {(g1 & g2).mean():8.1%}")
```

```text
chip    AO 점추정  P(AO≤1mW)  tok/s 점  P(≥10)   P(둘 다)
A        0.875      95.4%     54.1  100.0%    95.4%
B        0.499     100.0%     36.2  100.0%   100.0%
C        0.858      98.3%     16.6  100.0%    98.3%
D        0.387     100.0%      0.0    0.0%     0.0%
```

출력에서 볼 것: A는 "PASS"가 아니라 **95% PASS**다. 5%의 위험이 있다는 뜻이고, 이것은 **PoC에서 A의 AO 전력을 먼저 실측하라**는 신호다. 실측으로 범위가 좁아지면 이 숫자가 0% 또는 100%로 간다. D는 LLM gate에서 확정 탈락이다.

### 6.2 효용 — 앵커를 요구사항에서 가져온다

단위가 다른 기준(tok/s, mW, $, PM)을 더하려면 0~1 **효용**(utility)으로 바꿔야 한다. I5 6.1절은 기준 후보 대비 비율의 기하평균을 썼다. 여기서는 다른 방법을 쓴다: **고정 앵커 선형 효용**.

```
u(x) = clip( (x − x_worst) / (x_best − x_worst), 0, 1 )
x_worst = 받아들일 수 있는 최악 (보통 gate 값)  → 0점
x_best  = 이보다 좋아도 제품 가치가 늘지 않는 값 → 1점
```

말로 하면: 요구사항이 정한 최소선에서 0점, 목표에서 1점, 그 사이는 직선. 목표를 넘는 성능은 추가 점수가 없다(clip). 40 tok/s 이상은 사람이 읽는 속도보다 빨라서 가치가 늘지 않는다는 판단을 숫자로 적은 것이다.

왜 앵커를 **후보가 아니라 요구사항에서** 가져오나: 후보끼리 min-max 정규화를 하면(가장 나쁜 후보 = 0, 가장 좋은 후보 = 1), **후보 하나를 추가하거나 빼는 것만으로 다른 후보들의 순위가 바뀔 수 있다**(rank reversal). 예를 들어 비용이 아주 싼 D를 넣으면 비용 축의 1점이 D가 되어 A·B·C의 비용 점수 차이가 압축된다. 고정 앵커는 이 문제가 없다. 그리고 앵커를 정하는 과정 자체가 "얼마면 충분한가"를 product 팀과 합의하게 만든다.

손계산 — Chip B:

```
tps   36.2  → (36.2 − 10) / (40 − 10)   = 0.873
e_tok 15.4  → (15.4 − 25) / (8 − 25)    = 0.565
ao    0.499 → (0.499 − 1.0) / (0.3 − 1.0) = 0.716
sw    3.5   → (3.5 − 1) / 4             = 0.625
vendor 4    → 0.75     cost 30.18 → (30.18 − 50)/(15 − 50) = 0.566
pm    14.1  → (14.1 − 30)/(6 − 30)      = 0.663
risk  140   → (140 − 600)/(0 − 600)     = 0.767     road 3 → 0.5

score = .15·0.873 + .10·0.565 + .15·0.716 + .15·0.625 + .05·0.75
      + .15·0.566 + .10·0.663 + .10·0.767 + .05·0.5
      = 0.131 + 0.057 + 0.107 + 0.094 + 0.038 + 0.085 + 0.066 + 0.077 + 0.025 = 0.679
```

### 6.3 코드로 확인 — 점 추정 점수 (예제 8)

무엇을 확인하는 코드인가: 각 기준의 가운데 값으로 효용과 가중합을 계산하고, gate 실패 후보는 점수를 내지 않는다.

```python
# 예제 8: 점 추정(가운데 값)으로 효용 → 가중합. 어떤 기준이 점수를 만들었는지 기여도까지
import numpy as np
from m1_score import DATA, WEIGHT, utility, gates
crits = list(WEIGHT)
print("chip " + " ".join(f"{c:>6s}" for c in crits) + "   score  gate")
for k, d in DATA.items():
    mid = {c: d[c][1] for c in crits}
    u = {c: float(utility(c, mid[c])) for c in crits}
    score = sum(WEIGHT[c] * u[c] for c in crits)
    ok = bool(gates({c: np.array(mid[c]) for c in crits}))
    print(f"{k:4s} " + " ".join(f"{u[c]:6.2f}" for c in crits) + (f"   {score:.3f}  PASS" if ok else "     -    FAIL (점수 안 냄)"))
```

```text
chip    tps  e_tok     ao     sw vendor   cost     pm   risk   road   score  gate
A      1.00   0.71   0.18   0.88   0.50   0.14   0.86   0.78   0.75   0.627  PASS
B      0.87   0.56   0.72   0.62   0.75   0.57   0.66   0.77   0.50   0.679  PASS
C      0.22   0.53   0.20   0.25   0.88   0.41   0.05   0.20   0.62   0.314  PASS
D      0.00    nan   0.88   0.62   0.62   1.00   1.00   0.94   0.50     -    FAIL (점수 안 냄)
```

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg">
<text x="91.0" y="38" font-size="12" text-anchor="middle">LLM tok/s</text><text x="91.0" y="52" font-size="12" text-anchor="middle">w=0.15</text><text x="153.0" y="38" font-size="12" text-anchor="middle">mJ/token</text><text x="153.0" y="52" font-size="12" text-anchor="middle">w=0.10</text><text x="215.0" y="38" font-size="12" text-anchor="middle">AO mW</text><text x="215.0" y="52" font-size="12" text-anchor="middle">w=0.15</text><text x="277.0" y="38" font-size="12" text-anchor="middle">SW 성숙</text><text x="277.0" y="52" font-size="12" text-anchor="middle">w=0.15</text><text x="339.0" y="38" font-size="12" text-anchor="middle">벤더지원</text>
<text x="339.0" y="52" font-size="12" text-anchor="middle">w=0.05</text><text x="401.0" y="38" font-size="12" text-anchor="middle">$/대</text><text x="401.0" y="52" font-size="12" text-anchor="middle">w=0.15</text><text x="463.0" y="38" font-size="12" text-anchor="middle">공수 PM</text><text x="463.0" y="52" font-size="12" text-anchor="middle">w=0.10</text><text x="525.0" y="38" font-size="12" text-anchor="middle">공급리스크</text><text x="525.0" y="52" font-size="12" text-anchor="middle">w=0.10</text><text x="587.0" y="38" font-size="12" text-anchor="middle">로드맵</text><text x="587.0" y="52" font-size="12" text-anchor="middle">w=0.05</text>
<text x="50" y="85.0" font-size="13" text-anchor="end">Chip A</text><rect x="61" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.90" stroke="#888" stroke-width="0.5"/><text x="91.0" y="85.0" font-size="12" text-anchor="middle">1.00</text><rect x="123" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.67" stroke="#888" stroke-width="0.5"/><text x="153.0" y="85.0" font-size="12" text-anchor="middle">0.71</text><rect x="185" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.24" stroke="#888" stroke-width="0.5"/><text x="215.0" y="85.0" font-size="12" text-anchor="middle">0.18</text>
<rect x="247" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.80" stroke="#888" stroke-width="0.5"/><text x="277.0" y="85.0" font-size="12" text-anchor="middle">0.88</text><rect x="309" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.50" stroke="#888" stroke-width="0.5"/><text x="339.0" y="85.0" font-size="12" text-anchor="middle">0.50</text><rect x="371" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.21" stroke="#888" stroke-width="0.5"/><text x="401.0" y="85.0" font-size="12" text-anchor="middle">0.14</text><rect x="433" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.79" stroke="#888" stroke-width="0.5"/>
<text x="463.0" y="85.0" font-size="12" text-anchor="middle">0.86</text><rect x="495" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.72" stroke="#888" stroke-width="0.5"/><text x="525.0" y="85.0" font-size="12" text-anchor="middle">0.78</text><rect x="557" y="61" width="60" height="38" fill="#4a7bd0" fill-opacity="0.70" stroke="#888" stroke-width="0.5"/><text x="587.0" y="85.0" font-size="12" text-anchor="middle">0.75</text><text x="626" y="85.0" font-size="13">0.627</text><text x="50" y="125.0" font-size="13" text-anchor="end">Chip B</text><rect x="61" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.80" stroke="#888" stroke-width="0.5"/>
<text x="91.0" y="125.0" font-size="12" text-anchor="middle">0.87</text><rect x="123" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.55" stroke="#888" stroke-width="0.5"/><text x="153.0" y="125.0" font-size="12" text-anchor="middle">0.56</text><rect x="185" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.67" stroke="#888" stroke-width="0.5"/><text x="215.0" y="125.0" font-size="12" text-anchor="middle">0.72</text><rect x="247" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.60" stroke="#888" stroke-width="0.5"/><text x="277.0" y="125.0" font-size="12" text-anchor="middle">0.62</text>
<rect x="309" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.70" stroke="#888" stroke-width="0.5"/><text x="339.0" y="125.0" font-size="12" text-anchor="middle">0.75</text><rect x="371" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.55" stroke="#888" stroke-width="0.5"/><text x="401.0" y="125.0" font-size="12" text-anchor="middle">0.57</text><rect x="433" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.63" stroke="#888" stroke-width="0.5"/><text x="463.0" y="125.0" font-size="12" text-anchor="middle">0.66</text><rect x="495" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.71" stroke="#888" stroke-width="0.5"/>
<text x="525.0" y="125.0" font-size="12" text-anchor="middle">0.77</text><rect x="557" y="101" width="60" height="38" fill="#4a7bd0" fill-opacity="0.50" stroke="#888" stroke-width="0.5"/><text x="587.0" y="125.0" font-size="12" text-anchor="middle">0.50</text><text x="626" y="125.0" font-size="13">0.679</text><text x="50" y="165.0" font-size="13" text-anchor="end">Chip C</text><rect x="61" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.28" stroke="#888" stroke-width="0.5"/><text x="91.0" y="165.0" font-size="12" text-anchor="middle">0.22</text><rect x="123" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.52" stroke="#888" stroke-width="0.5"/>
<text x="153.0" y="165.0" font-size="12" text-anchor="middle">0.53</text><rect x="185" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.26" stroke="#888" stroke-width="0.5"/><text x="215.0" y="165.0" font-size="12" text-anchor="middle">0.20</text><rect x="247" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.30" stroke="#888" stroke-width="0.5"/><text x="277.0" y="165.0" font-size="12" text-anchor="middle">0.25</text><rect x="309" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.80" stroke="#888" stroke-width="0.5"/><text x="339.0" y="165.0" font-size="12" text-anchor="middle">0.88</text>
<rect x="371" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.42" stroke="#888" stroke-width="0.5"/><text x="401.0" y="165.0" font-size="12" text-anchor="middle">0.41</text><rect x="433" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.14" stroke="#888" stroke-width="0.5"/><text x="463.0" y="165.0" font-size="12" text-anchor="middle">0.05</text><rect x="495" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.26" stroke="#888" stroke-width="0.5"/><text x="525.0" y="165.0" font-size="12" text-anchor="middle">0.20</text><rect x="557" y="141" width="60" height="38" fill="#4a7bd0" fill-opacity="0.60" stroke="#888" stroke-width="0.5"/>
<text x="587.0" y="165.0" font-size="12" text-anchor="middle">0.62</text><text x="626" y="165.0" font-size="13">0.314</text><text x="50" y="205.0" font-size="13" text-anchor="end">Chip D</text><rect x="61" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.10" stroke="#888" stroke-width="0.5"/><text x="91.0" y="205.0" font-size="12" text-anchor="middle">0.00</text><rect x="123" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.00" stroke="#888" stroke-width="0.5"/><text x="153.0" y="205.0" font-size="12" text-anchor="middle">—</text><rect x="185" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.80" stroke="#888" stroke-width="0.5"/>
<text x="215.0" y="205.0" font-size="12" text-anchor="middle">0.88</text><rect x="247" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.60" stroke="#888" stroke-width="0.5"/><text x="277.0" y="205.0" font-size="12" text-anchor="middle">0.62</text><rect x="309" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.60" stroke="#888" stroke-width="0.5"/><text x="339.0" y="205.0" font-size="12" text-anchor="middle">0.62</text><rect x="371" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.90" stroke="#888" stroke-width="0.5"/><text x="401.0" y="205.0" font-size="12" text-anchor="middle">1.00</text>
<rect x="433" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.90" stroke="#888" stroke-width="0.5"/><text x="463.0" y="205.0" font-size="12" text-anchor="middle">1.00</text><rect x="495" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.85" stroke="#888" stroke-width="0.5"/><text x="525.0" y="205.0" font-size="12" text-anchor="middle">0.94</text><rect x="557" y="181" width="60" height="38" fill="#4a7bd0" fill-opacity="0.50" stroke="#888" stroke-width="0.5"/><text x="587.0" y="205.0" font-size="12" text-anchor="middle">0.50</text><text x="626" y="205.0" font-size="13">gate 탈락</text><text x="626" y="52" font-size="12">가중합</text>
<line x1="60" y1="180" x2="618" y2="180" stroke="#d0564a" stroke-width="2" stroke-dasharray="5 3"/>
<text x="60" y="244" font-size="12">칸 = 효용 0~1 (진할수록 좋다). 앵커는 요구사항에서 온다. D는 LLM gate 실패라 점수를 내지 않는다</text></svg>
```

그림 6 — 효용 히트맵(예제 8). 행 하나를 가로로 읽으면 그 칩의 강점·약점 프로필이다. A는 성능·소프트웨어가 진하고 AO·비용이 옅다. B는 고르게 중간 이상이다. C는 벤더 지원만 진하다. D는 비용·공수·리스크가 가장 진하지만 빨간 점선 아래 — gate 탈락이라 아무리 진해도 점수를 내지 않는다. 이 그림은 결정 회의에서 가장 먼저 보여 줄 장표다: 숫자 하나(가중합)보다 **왜 그 숫자가 나왔는지**를 보여 준다.

출력에서 볼 것: 손계산과 같은 B 0.679. **A가 지는 이유는 AO(0.18)와 비용(0.14)** 두 칸이다. 성능에서는 A가 이기지만 tps 효용이 1.0에서 잘려서(40 tok/s 이상은 가치 없음) 54 tok/s의 여유가 점수로 이어지지 않는다. 이것이 앵커 clip의 효과다 — 만약 "2세대 모델은 1B가 될 수 있다"는 로드맵이 있다면 tps 앵커를 올려야 하고, 그러면 A와 B의 차이가 줄고 결정이 훨씬 덜 견고해진다(직접 해보기 3).

### 6.4 Monte Carlo — 각 칩이 이길 확률 (예제 9)

점 추정으로 B가 0.052 차이로 이겼다. 이 차이는 믿을 만한가? 모든 기준이 범위를 가지고 있으니, **범위 안에서 2만 개의 "가능한 세상"을 뽑아 매번 1등을 세면** 승률이 나온다. gate도 표본마다 다시 적용한다 — A의 AO가 1 mW를 넘는 표본에서는 A가 탈락한다.

두 가지를 본다. (a) 값만 불확실. (b) 가중치도 불확실 — 가중치는 사람들이 합의한 숫자라 그 자체가 흔들린다. Dirichlet 분포(합이 1인 무작위 가중치)로 평균은 원래 가중치, 0.15짜리 가중치의 표준편차가 약 0.05가 되게 뽑는다.

무엇을 확인하는 코드인가: 칩·기준마다 삼각분포 표본을 뽑아 gate → 효용 → 가중합 → 1등을 2만 번 반복한다.

```python
# 예제 9: 범위로 Monte Carlo — 각 칩이 1등할 확률. (a) 값만 불확실 (b) 가중치도 불확실(Dirichlet)
import numpy as np
from m1_score import DATA, WEIGHT, utility, gates
rng = np.random.default_rng(2026); N = 20000
crits, chips = list(WEIGHT), list(DATA)
tri = lambda t: rng.triangular(*t, N) if t[2] > t[0] else np.full(N, t[1])
S = {k: {c: tri(DATA[k][c]) for c in crits} for k in chips}             # 칩×기준 표본 (칩마다 독립)
def run(W):                                                              # W: (N, 기준 수) 가중치 행렬
    sc = np.full((len(chips), N), -np.inf)
    for i, k in enumerate(chips):
        u = np.stack([np.nan_to_num(utility(c, S[k][c])) for c in crits], 1)
        sc[i] = np.where(gates(S[k]), (u * W).sum(1), -np.inf)          # gate 실패 표본은 -inf
    win = sc.argmax(0); none = np.isinf(sc.max(0))
    return sc, [(np.mean((win == i) & ~none)) for i in range(len(chips))]
w0 = np.array([WEIGHT[c] for c in crits])
sc, pa = run(np.tile(w0, (N, 1)))
_, pb = run(rng.dirichlet(50 * w0, N))                                   # 평균은 w0. w=0.15면 표준편차 ≈ 0.05
for i, k in enumerate(chips):
    s = sc[i][np.isfinite(sc[i])]
    q = "/".join(f"{x:.3f}" for x in np.percentile(s, [10, 50, 90])) if s.size else "gate 탈락 (표본 0개)"
    print(f"{k}: score P10/P50/P90 = {q:19s}  P(win) 값만 {pa[i]:6.1%}   값+가중치 {pb[i]:6.1%}")
```

```text
A: score P10/P50/P90 = 0.580/0.611/0.641    P(win) 값만   2.5%   값+가중치  11.1%
B: score P10/P50/P90 = 0.645/0.675/0.706    P(win) 값만  97.5%   값+가중치  88.9%
C: score P10/P50/P90 = 0.257/0.302/0.346    P(win) 값만   0.0%   값+가중치   0.0%
D: score P10/P50/P90 = gate 탈락 (표본 0개)      P(win) 값만   0.0%   값+가중치   0.0%
```

```svg
<svg viewBox="0 0 620 230" xmlns="http://www.w3.org/2000/svg">
<text x="100" y="47" font-size="13" text-anchor="end">Chip A</text><rect x="110" y="30" width="11.0" height="13" fill="#4a7bd0" fill-opacity="0.85"/><text x="127.0" y="41" font-size="12">2.5%</text><rect x="110" y="45" width="48.8" height="13" fill="#4a7bd0" fill-opacity="0.4"/><text x="164.8" y="56" font-size="12">11.1%</text><text x="100" y="93" font-size="13" text-anchor="end">Chip B</text><rect x="110" y="76" width="429.0" height="13" fill="#e08a3c" fill-opacity="0.85"/><text x="545.0" y="87" font-size="12">97.5%</text><rect x="110" y="91" width="391.2" height="13" fill="#e08a3c" fill-opacity="0.4"/><text x="507.2" y="102" font-size="12">88.9%</text>
<text x="100" y="139" font-size="13" text-anchor="end">Chip C</text><rect x="110" y="122" width="0.0" height="13" fill="#3f9a6b" fill-opacity="0.85"/><text x="116.0" y="133" font-size="12">0.0%</text><rect x="110" y="137" width="0.0" height="13" fill="#3f9a6b" fill-opacity="0.4"/><text x="116.0" y="148" font-size="12">0.0%</text><text x="100" y="185" font-size="13" text-anchor="end">Chip D</text><rect x="110" y="168" width="0.0" height="13" fill="#888" fill-opacity="0.85"/><text x="116.0" y="179" font-size="12">0.0% (gate 탈락)</text><rect x="110" y="183" width="0.0" height="13" fill="#888" fill-opacity="0.4"/><text x="116.0" y="194" font-size="12">0.0%</text>
<line x1="110" y1="22" x2="110" y2="212" stroke="currentColor"/>
<text x="110" y="222" font-size="12">각 칩: 위 막대 = 값만 불확실 · 아래(옅은) 막대 = 값 + 가중치(Dirichlet) 불확실 · N = 20,000</text></svg>
```

그림 7 — 1등 확률(예제 9). 진한 막대는 값만 불확실, 옅은 막대는 가중치까지 불확실할 때다. 가중치의 불확실성을 넣으면 A의 승률이 2.5%에서 11.1%로 오른다 — 결정에 남은 불확실성의 상당 부분이 **측정이 아니라 합의(가중치)**에 있다는 뜻이다.

출력에서 볼 것:

- **A의 MC 점수 중앙값(0.611)이 점 추정(0.627)보다 낮다**: A의 범위가 나쁜 쪽으로 길고(AO ×1.2, 비용 ×1.15), 효용 clip 때문에 좋은 쪽 이득이 잘린다(tps는 이미 1.0). 비대칭 범위에서는 "가운데 값의 점수"와 "점수의 가운데 값"이 다르다.
- **B의 승률 89~98%**: 결정은 꽤 견고하다. 결정 문서에 "B, 승률 약 90% (가중치 불확실성 포함)"라고 쓸 수 있다. "B가 0.679점으로 1등"보다 훨씬 정직한 문장이다.
- **C는 0%**: 효율이 좋다는 주장이 사실이어도(e_tok 범위 끝까지 가도) 소프트웨어·공수·리스크·대역폭에서 진 점수를 메우지 못한다.
- 칩끼리 표본을 독립으로 뽑았다. 실제로는 상관이 있다(예: DRAM 가격이 오르면 A·B·C 모두 비싸진다). 상관을 무시하면 승률이 과장될 수도, 축소될 수도 있다 — 공통 요인은 같은 난수로 뽑는 것이 정석이다.

### 6.5 민감도 — 무엇이 결론을 뒤집나 (예제 10)

I5 6.2절은 무작위 가중치에서의 1등 빈도와 한 가중치의 전환점을 봤다. 여기서는 결정 회의에서 바로 쓰는 두 질문을 계산한다.

1. **"가중치 하나를 몇 배로 올려야 1등이 바뀌나?"** — 회의에서 누군가 "소프트웨어가 더 중요하지 않나?"라고 하면 바로 답할 수 있다.
2. **"기준 하나가 A에게 가장 유리하게, B에게 가장 불리하게 풀리면 뒤집히나?"** (swing) — 뒤집히는 기준이 있으면 그것이 **PoC에서 먼저 실측할 칸**이다.

```python
# 예제 10: (1) 가중치 하나를 몇 배로 바꿔야 1등이 바뀌나 (2) 기준 하나를 범위 끝으로 몰면 뒤집히나 (A vs B)
import numpy as np
from m1_score import DATA, WEIGHT, ANCHOR, utility
crits = list(WEIGHT)
mid = lambda k, c: DATA[k][c][1]
def score(k, w, pick=mid):
    return sum(w[c] * float(np.nan_to_num(utility(c, pick(k, c)))) for c in crits)
def winner(w):                              # D는 gate 탈락이라 A·B·C만. 가중치 합이 1이 아니어도 순위는 같다
    return max("ABC", key=lambda k: score(k, w))
print("(1) 가중치 하나에 배수 m을 곱할 때 1등이 B에서 바뀌는 최소 m")
for c in crits:
    for m in np.arange(1.0, 8.01, 0.05):
        w = {x: WEIGHT[x] * (m if x == c else 1) for x in crits}
        if winner(w) != "B":
            print(f"   {c:6s} ×{m:.2f} (실효 비중 {WEIGHT[c] * m / sum(w.values()):.0%}) → {winner(w)}"); break
    else:
        print(f"   {c:6s} ×8까지 B 유지")
print("(2) swing: 그 기준만 A는 좋은 끝, B는 나쁜 끝으로 몰았을 때 B − A 점수 차")
good = {c: 2 if ANCHOR[c][1] > ANCHOR[c][0] else 0 for c in crits}   # 큰 값이 좋은 기준이면 index 2
print(f"   가운데 값 기준: {score('B', WEIGHT) - score('A', WEIGHT):+.3f}")
for c in crits:
    def pick(k, x):
        if x != c: return mid(k, x)
        return DATA[k][x][good[x]] if k == "A" else DATA[k][x][2 - good[x]]
    d = score("B", WEIGHT, pick) - score("A", WEIGHT, pick)
    print(f"   {c:6s} {d:+.3f}" + ("  ← 이것 하나로 뒤집힌다" if d < 0 else ""))
```

```text
(1) 가중치 하나에 배수 m을 곱할 때 1등이 B에서 바뀌는 최소 m
   tps    ×3.75 (실효 비중 40%) → A
   e_tok  ×4.55 (실효 비중 34%) → A
   ao     ×8까지 B 유지
   sw     ×2.40 (실효 비중 30%) → A
   vendor ×8까지 B 유지
   cost   ×8까지 B 유지
   pm     ×3.65 (실효 비중 29%) → A
   risk   ×8까지 B 유지
   road   ×5.20 (실효 비중 21%) → A
(2) swing: 그 기준만 A는 좋은 끝, B는 나쁜 끝으로 몰았을 때 B − A 점수 차
   가운데 값 기준: +0.052
   tps    +0.024
   e_tok  +0.006
   ao     +0.003
   sw     +0.015
   vendor +0.027
   cost   +0.013
   pm     +0.039
   risk   +0.024
   road   +0.027
```

출력에서 볼 것과 회의에서 하는 말:

- **가장 가까운 전환점은 소프트웨어 성숙도**: 가중치를 0.15에서 2.4배(실효 30%)로 올리면 A가 이긴다. "소프트웨어 성숙도를 전체의 30% 이상으로 보는 사람이 있다면 A가 맞다. 그렇지 않다면 B다." — 결정이 **어떤 가치 판단에 달려 있는지**를 한 문장으로 말할 수 있게 된다.
- **AO·비용·리스크 가중치는 아무리 올려도 B** — B가 이 세 축에서 이미 이기고 있기 때문이다.
- **단일 swing으로는 뒤집히지 않는다** (모두 +). 가장 크게 좁히는 것은 `e_tok`(+0.006)과 `ao`(+0.003): A의 AO 전력이 범위의 좋은 끝(0.74 mW)이고 B가 나쁜 끝(0.60 mW)이어도 B가 이긴다. 즉 **PoC에서 AO 실측은 A의 gate 통과 여부(6.1)를 위해서는 중요하지만, 순위를 바꾸지는 않는다.** 이런 판단이 "무엇부터 잴까"를 정한다 — 결정을 바꿀 수 없는 측정은 뒤로 미룬다.

### 6.6 Pareto — 한 숫자로 줄이기 전에 (I5 6절 복습)

점수화 전에 두 축(예: 대당 총비용 vs LLM tok/s)으로 **지배당한 후보**를 확인한다. 어떤 후보가 다른 후보보다 모든 축에서 같거나 나쁘면 가중치와 상관없이 탈락이다.

```
gate 통과 후보 (100k대 대당 총비용 $, tok/s):  A (45.0, 54.1)   B (30.2, 36.2)   C (35.8, 16.6)
C vs B: 비용 35.8 > 30.2 (나쁨), tok/s 16.6 < 36.2 (나쁨)  → C는 B에 지배당한다
A vs B: A는 비싸지만 빠르다 → 서로 지배하지 않는다          → Pareto front = {A, B}
```

말로 하면: 이 두 축에서는 C를 고를 이유가 없다. C를 살리려면 이 두 축 밖의 기준(예: 작은 모델 에너지, 큰 SRAM)이 결정적이어야 한다. **Pareto front 위의 후보 사이에서만 가중치가 일을 한다** — A와 B 사이의 선택은 "추가 15$로 tok/s 여유와 소프트웨어 성숙도를 살 가치가 있나"라는 질문이 된다.

### 6.7 결정 문서에 쓰는 법

결정 문서(decision memo)는 한 쪽이면 된다. 구조:

1. **권고**: "Chip B. 승률 약 90%(가중치 불확실성 포함)."
2. **gate 결과**: D 탈락(LLM 용량). A는 AO 전력 95% 통과 — PoC 실측 필요.
3. **왜 B인가**: 히트맵(그림 6) + "A 대비 대당 15$ 저렴, AO 전력 여유 50%, 공수 +5 PM".
4. **무엇이 결론을 뒤집나**: "소프트웨어 성숙도 가중치 30% 이상, 또는 2세대에 1B LLM이 필요하다는 로드맵 결정".
5. **조건과 다음 단계**: B의 prefill 약점(INT4 연산 없음)에 대한 완화책, 계약에 넣을 항목(수명, 가격, 지원 SLA), 남은 실측 항목.
6. **가정과 출처 목록**: 모든 숫자의 출처·신뢰도(1.2절).

---

## 7. 선정 과정을 실제로 돌리기 — RFI에서 계약까지

### 7.1 단계

| 단계 | 목적 | 산출물 | 기간 (예) | 주도 |
|---|---|---|---|---|
| 0. 요구사항 고정 | 예산·gate·가중치·model zoo를 **평가 전에** 고정 | 예산표(I1), 기준표(1.3절), 앵커·가중치, 대표 모델 3~5개 | 1~2주 | Product + ML + HW |
| 1. RFI | 넓게 묻는다 (6~10개 벤더) | 질문지 응답, 공개 자료 | 2~3주 | 구매 + ML |
| 2. shortlist | 서류 gate + 3절 추정으로 3~4개로 | 추정 표(예제 1), 탈락 사유 | 1주 | ML + HW |
| 3. NDA·eval kit | 상세 문서·SDK·키트 확보 | NDA, TRM, SDK 접근, 키트 | 1~3주 (lead time) | 구매 + 법무 |
| 4. PoC | gate와 민감한 칸을 실측으로 | PoC 리포트 (7.5절) | 4~6주 | ML + FW |
| 5. RFQ·상업 | 가격·수명·지원 조건 | 견적, 수명·LTB 조건, 지원 SLA | PoC와 병행 | 구매 + Finance |
| 6. decision review | 점수·불확실성·리스크로 결정 | 결정 문서(6.7절), 반대 의견 기록 | 1~2주 | 리더십 + 전원 |
| 7. 계약·로드맵 정렬 | 결정을 문서로 묶는다 | 공급 계약, 지원 계약, 로드맵 마일스톤 | 2~4주 | 구매 + 법무 + Product |

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<line x1="190" y1="46" x2="190" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="190" y="40" font-size="12" text-anchor="middle">W0</text><line x1="248" y1="46" x2="248" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="248" y="40" font-size="12" text-anchor="middle">W2</text><line x1="306" y1="46" x2="306" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="306" y="40" font-size="12" text-anchor="middle">W4</text><line x1="364" y1="46" x2="364" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="364" y="40" font-size="12" text-anchor="middle">W6</text>
<line x1="422" y1="46" x2="422" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="422" y="40" font-size="12" text-anchor="middle">W8</text><line x1="480" y1="46" x2="480" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="480" y="40" font-size="12" text-anchor="middle">W10</text><line x1="538" y1="46" x2="538" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="538" y="40" font-size="12" text-anchor="middle">W12</text><line x1="596" y1="46" x2="596" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="596" y="40" font-size="12" text-anchor="middle">W14</text>
<line x1="654" y1="46" x2="654" y2="272" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="654" y="40" font-size="12" text-anchor="middle">W16</text><text x="182" y="67" font-size="12" text-anchor="end">요구사항·model zoo 고정</text><rect x="190" y="55" width="58" height="16" rx="3" fill="#4a7bd0" fill-opacity="0.55"/><text x="182" y="89" font-size="12" text-anchor="end">RFI 발송·응답</text><rect x="219" y="77" width="58" height="16" rx="3" fill="#888" fill-opacity="0.55"/><text x="182" y="111" font-size="12" text-anchor="end">서류 평가 → shortlist</text><rect x="277" y="99" width="29" height="16" rx="3" fill="#888" fill-opacity="0.55"/>
<text x="182" y="133" font-size="12" text-anchor="end">NDA·eval kit 수급</text><rect x="277" y="121" width="87" height="16" rx="3" fill="#888" fill-opacity="0.55"/><text x="182" y="155" font-size="12" text-anchor="end">하드웨어 없는 추정 (3절)</text><rect x="248" y="143" width="87" height="16" rx="3" fill="#4a7bd0" fill-opacity="0.55"/><text x="182" y="177" font-size="12" text-anchor="end">PoC ① op sweep·정확도</text><rect x="335" y="165" width="87" height="16" rx="3" fill="#e08a3c" fill-opacity="0.55"/><text x="182" y="199" font-size="12" text-anchor="end">PoC ② 성능·전력·열</text><rect x="393" y="187" width="116" height="16" rx="3" fill="#e08a3c" fill-opacity="0.55"/>
<text x="182" y="221" font-size="12" text-anchor="end">RFQ·상업 협상</text><rect x="422" y="209" width="116" height="16" rx="3" fill="#3f9a6b" fill-opacity="0.55"/><text x="182" y="243" font-size="12" text-anchor="end">scorecard·decision review</text><rect x="509" y="231" width="58" height="16" rx="3" fill="#d0564a" fill-opacity="0.55"/><text x="182" y="265" font-size="12" text-anchor="end">계약·로드맵 정렬</text><rect x="567" y="253" width="87" height="16" rx="3" fill="#3f9a6b" fill-opacity="0.55"/><polygon points="306,279 313,286 306,293 299,286" fill="#d0564a"/><text x="306" y="308" font-size="12" text-anchor="middle">shortlist 3~4개</text>
<polygon points="567,279 574,286 567,293 560,286" fill="#d0564a"/><text x="567" y="308" font-size="12" text-anchor="middle">결정</text>
<text x="340" y="16" font-size="13" text-anchor="middle">선정 과정 일정 예 (약 16주, 주 단위)</text></svg>
```

그림 8 — 약 16주짜리 선정 일정의 예. 파랑은 내부 준비(요구사항, 추정), 회색은 벤더 대기, 주황은 PoC, 초록은 상업·계약, 빨강은 결정이다. 회색 구간(kit 수급)과 파랑 "하드웨어 없는 추정"이 겹치는 것이 핵심이다 — 기다리는 시간에 3절의 계산을 해 두면 kit이 도착한 날 무엇을 먼저 잴지 이미 정해져 있다. RFQ를 PoC와 병행하는 이유는 가격·수명 조건이 늦게 오면 기술 평가가 끝나고도 결정을 못 하기 때문이다.

### 7.2 팀 — 누가 무엇을 책임지나

| 역할 | 책임 | 결정에서의 무게 |
|---|---|---|
| ML/edge AI 엔지니어 (Don의 자리) | model zoo, 추정, PoC 성능·정확도, 점수표 운영 | 기술 점수의 주인, 과정의 진행자 |
| 펌웨어/시스템 | BSP·전원 상태·AO·debug 평가, 공수 추정 | 공수·AO gate |
| HW | 전원·열·패키지·인터페이스·보드 면적 | 인터페이스·열 gate |
| Product | 기능·배터리 목표, 출시 일정, 가중치 합의 | 가중치·앵커의 주인 |
| Supply chain·구매 | RFI/RFQ, lead time, 수명·allocation 협상 | 공급 리스크 |
| Finance·법무 | TCO, NRE, license·royalty, NDA·계약 | 비용 축, 계약 조건 |

Don의 경험에서 가장 직접 가져올 수 있는 역할은 **진행자**다. 새 실리콘 통합과 NPI에서 여러 팀의 일정과 sign-off를 맞춰 본 경험은 "누가 어떤 숫자를 언제까지 가져오나"를 관리하는 이 일과 같다.

### 7.3 RFI 질문 목록 (예)

RFI는 같은 질문을 모든 벤더에게 같은 형식으로 보낸다. 답이 표로 모이게 한다.

- 연산: 정밀도별 peak TOPS와 **측정 조건**(dense/sparse, 클럭, 전압). INT4는 연산인가 저장 형식인가?
- 메모리: NPU 전용 SRAM 크기와 공유 여부, LPDDR 종류·폭·최대 속도, NPU가 볼 수 있는 주소 범위
- 우리 모델: 첨부한 대표 모델 3개(ONNX)를 당신의 툴로 컴파일한 리포트(op 배치, CPU fallback, 사이클 추정)를 줄 수 있나?
- 소프트웨어: 지원 런타임(TFLite/LiteRT, ONNX Runtime EP, 자체 런타임), LLM 런타임 지원, SDK 릴리스 주기, 하위 호환 정책
- OS·BSP: Linux 커널 버전과 upstream 상태, Android 지원, RTOS 지원(AO 코어), BSP 지원 기간
- 전력: power state 표(상태별 전력, 진입·탈출 시간, wake source), AO 도메인의 주변장치 목록
- 보안: secure boot, TEE, key 저장, 보안 디버그 정책, 보안 인증 이력
- 디버그: JTAG/SWD·trace 접근, 양산 칩에서의 디버그 잠금·해제 절차, NPU 프로파일러
- 지원: 우리 규모 고객에게 배정되는 FAE·AE, 응답 SLA, errata 공개 방식
- 공급: 수명 보장 기간, EOL 공지 기간, LTB 정책, lead time 이력, 2nd source 또는 pin 호환 대체품
- 상업: 수량 구간별 단가, NRE, SDK·런타임 license와 royalty, 개발 키트 가격·수량
- 로드맵: 다음 세대 일정과 pin·소프트웨어 호환 계획 (NDA 하에서)
- 인증: 내장 radio의 모듈 인증 여부, 레퍼런스 디자인의 인증 이력

### 7.4 PoC 계획 — 4~6주에 무엇을 재나

PoC의 목적은 **gate를 확정하고, 민감도 분석(6.5절)이 지목한 칸을 실측으로 좁히는 것**이다. 모든 것을 재려고 하면 6주 안에 아무것도 끝나지 않는다.

| 주차 | 작업 | 방법 (노트) | 종료 기준 |
|---|---|---|---|
| 1 | 환경 고정, hello-world, 버전 매트릭스 | F8 1·2절 | 같은 결과를 두 번 재현 |
| 1~2 | model zoo op sweep, CPU fallback 목록 | F8 3·4절 | op gap 개수 확정 → 예제 4의 `gaps` 갱신 |
| 2~3 | 정확도: float 기준 대비 레이어별 비교, bit-exact/tolerance 판정 | C8 2~4절 | 대표 모델 전부 정확도 gate 통과 또는 원인 확인 |
| 2 | 벤더 대응 시험: 재현 케이스 하나를 일부러 보낸다 | F8 7·9절 | 응답 시간·품질 기록 → rubric 점수 |
| 3~4 | 성능: init·steady·p99, 활용률, 하니스 | I5 5절, K2 | 예제 1의 u·e·ovh 범위를 실측으로 교체 |
| 3~5 | 전력: 추론당 mJ, idle, AO 경로 평균 | K3 | AO gate 확정, 예제 3 숫자 교체 |
| 4~5 | 열: 지속 부하 10분 이상 | K4 | sustained 성능 비율 |
| 5~6 | 점수표 갱신, Monte Carlo 재실행 | 6절 | 결정 문서 초안 |

PoC에서 지켜야 할 규칙:

- **같은 모델, 같은 양자화, 같은 입력, 같은 측정 방법**으로 모든 후보를 잰다(I5 4.5). 한 벤더가 자기 버전의 모델로 잰 숫자는 별도 표시한다.
- **벤더 엔지니어가 만진 결과와 우리 엔지니어가 혼자 낸 결과를 구분**한다. 양산 2년 동안 우리를 돕는 것은 대부분 우리 엔지니어다.
- **NDA 위생**: 한 벤더의 숫자·문서를 다른 벤더에게 넘기지 않는다(F8 9절). 점수표의 공유 범위도 정한다.
- PoC 중간에 기준·가중치를 바꾸지 않는다. 바꿔야 하면 바꾼 이유와 전후 결과를 둘 다 기록한다.

### 7.5 decision review

- 기준·앵커·가중치는 **0단계에서 서명**된 것을 그대로 쓴다. 회의에서 바꾸자는 제안이 나오면 6.5절의 민감도 결과로 "그렇게 바꾸면 결론이 바뀌는가"를 바로 보여 준다.
- 히트맵(그림 6) → 승률(그림 7) → 전환점(예제 10) → TCO 그림(그림 5) → 리스크 표 순서로 보여 준다.
- **반대 의견을 기록**한다. "HW는 A의 열 여유를 이유로 반대"처럼 이름과 근거를 남긴다. 나중에 그 리스크가 현실이 되면 이 기록이 대응을 빠르게 한다.
- 결정은 조건부로 내린다: "B로 간다. 단 계약에 수명 7년과 지원 SLA가 들어가지 않으면 A로 재검토."

### 7.6 계약과 로드맵 정렬 — 결정을 문서로 묶는다

기술 평가에서 확인한 것 중 계약에 넣지 않은 것은 약속이 아니다.

- 수명 보장 기간과 EOL 공지 기간, last-time-buy 조건
- 수량 구간별 가격과 기간, 공급 부족 시 배정 원칙
- 지원 SLA: 담당 FAE, 응답 시간, 치명적 버그의 수정 기간
- SDK·런타임 license 조건과 royalty, 지원 기간
- 로드맵 마일스톤: 우리 2세대 일정과 벤더 다음 칩 일정의 연동, 소프트웨어 호환 약속
- 스타트업 벤더라면: 소스·툴 escrow, 단계별 계약(마일스톤 달성 시 다음 단계)

### 7.7 조직적 함정

| 함정 | 모습 | 왜 위험한가 | 대응 |
|---|---|---|---|
| benchmarketing | 벤더 슬라이드 숫자가 점수표에 그대로 들어간다 | 조건이 다른 숫자끼리 비교된다 | 역산(3.6절), 우리 모델·우리 측정으로만 점수 |
| sunk cost | "이미 C에 3개월을 썼으니 C로 가자" | 지나간 비용은 앞으로의 선택과 무관하다 | 결정 시점의 **남은** 공수·리스크만 비교 |
| "벤더가 고쳐 줄 것" | 미지원 op·버그를 "다음 릴리스에 된다"로 넘긴다 | 다음 릴리스는 늦고, 우리 일정은 안 늦춰진다 | 지금 동작하는 것만 점수, 약속은 계약 조항으로 |
| 첫 후보에 닻 내리기 | 처음 본 칩이 기준이 되고 나머지는 "그보다 나은가"로만 평가 | 다른 강점이 안 보인다 | 고정 앵커(6.2절), 기준표를 먼저 만든다 |
| 데모를 평가 | 벤더 데모 앱이 잘 돈다 | 데모는 벤더 모델·벤더 설정 | 우리 model zoo로 PoC |
| 한 명의 champion | 한 사람이 특정 칩을 밀고 평가도 그 사람이 한다 | 확증 편향 | 평가자 두 명 이상, 점수 근거 링크 |
| 골대 옮기기 | 결과를 본 뒤 가중치를 바꾼다 | 원하는 결론에 맞춘 점수 | 가중치 사전 서명, 변경 시 전후 기록 |
| 상위자의 선호 | 회의에서 가장 높은 사람의 의견으로 수렴 | 근거 없는 결정 | 숫자를 먼저, 의견은 나중에. 반대 의견 기록 |

sunk cost는 사람이 이미 쓴 비용 때문에 계속 투자하는 경향으로, 행동경제학에서 잘 알려진 편향이다(참고 자료의 Arkes & Blumer). Don이 펌웨어에서 "이 workaround를 2주 했으니 끝까지 가자"를 겪어 봤다면 같은 현상이다.

---

## 8. 임베디드 관점에서 다시 보기 — 펌웨어 엔지니어가 꼭 보는 것

ML 팀은 TOPS와 op coverage를 보고, HW 팀은 전원과 패키지를 본다. **그 사이에 아무도 안 보는 칸이 펌웨어 칸이고, 양산 일정을 무너뜨리는 것은 대개 여기다.** Don이 평가에 가장 크게 기여할 수 있는 부분이다.

### 8.1 펌웨어 중심 체크리스트

- [ ] **BSP 나이와 출처**: 커널 버전, upstream과 얼마나 떨어진 vendor fork인가, 보안 패치를 얼마나 자주 받나. 오래된 fork는 2년 동안 보안 패치·드라이버 backport 비용을 만든다.
- [ ] **power state 전환 실측**: datasheet의 wake latency가 아니라, 우리 BSP에서 GPIO 마커로 잰 실제 suspend/resume 시간(K3 4절). 드라이버 하나가 resume을 수십 ms 늘리는 일은 흔하다.
- [ ] **NPU 드라이버의 에러 경로**: 타임아웃, hang 감지, 리셋 후 복구, 에러 코드가 있나(E5 8.2). 데모에서는 절대 안 보이고 필드에서만 보인다.
- [ ] **debug 접근**: 양산 칩에서 JTAG/SWD를 쓸 수 있나, 보안 잠금 해제 절차(인증서, 벤더 승인)는 얼마나 걸리나, AO 코어를 따로 디버그할 수 있나, trace(ETM 류)가 있나.
- [ ] **boot 시간과 경로**: cold boot, secure boot 검증 시간, 모델 blob 로드 시간. 웨어러블은 자주 깨어나서 이것이 체감 성능과 전력이 된다.
- [ ] **메모리 맵과 DMA·캐시**: NPU가 쓰는 버퍼의 정렬·영역 제약, 캐시 관리 규칙(E7 6절), IOMMU 유무.
- [ ] **OTA와 A/B**: 부트로더가 A/B 파티션과 롤백을 지원하나, 모델과 런타임을 한 단위로 업데이트할 수 있나(J5).
- [ ] **errata 양과 질**: errata 문서의 항목 수, workaround의 구체성, 새 리비전의 수정 이력. Don이 RF 칩셋에서 보던 것과 같은 방식으로 읽는다.
- [ ] **텔레메트리 훅**: NPU 에러 카운터, thermal 이벤트, 전력 상태 통계를 펌웨어에서 읽을 수 있나(F8 10절).
- [ ] **factory test 지원**: 양산 라인에서 NPU·SRAM·AO 경로를 빠르게 검사할 수 있는 self-test가 있나.

### 8.2 AO MCU와 메인 SoC를 두 개의 평가로

4절에서 본 것처럼 AO 경로는 다른 기준으로 본다. 실무에서는 **점수표를 두 장** 만드는 경우가 많다: 메인 AP/NPU 점수표와 AO 섬(내장이든 외부 MCU든) 점수표. 그리고 "메인 + AO 조합"을 후보 단위로 삼는다. C처럼 AO가 외부 MCU인 조합은 두 번째 칩의 BOM·펌웨어·통신 프로토콜·wake latency가 조합의 점수에 들어간다.

```
후보 단위 = (메인 SoC, AO 경로)
  A  = (A,       A 내장 LP-DSP)
  B  = (B,       B 내장 microNPU)
  C  = (C,       외부 MCU)        ← BOM +3$, 펌웨어 2벌, 칩 간 IPC, main wake 350 ms
  D  = (D 단독)                   ← LLM 불가 → 클라우드 LLM이면 다시 후보가 된다 (I3)
```

말로 하면: D는 "on-device LLM" gate에서 탈락했지만, 제품 결정이 "LLM은 폰·클라우드에서"로 바뀌면(I3의 partitioning) 가장 싸고 전력이 낮은 후보로 살아난다. **gate는 제품 결정의 함수**이고, 그래서 0단계에서 product와 gate를 같이 고정해야 한다.

### 8.3 Don의 경험을 이 틀에 놓기

면접에서 "칩 평가 경험"을 물으면 Don은 ML 칩이 아니어도 답할 재료가 있다. SSD 컨트롤러 bring-up과 RF 칩셋 통합에서 다음을 했는지 정리해 두면 좋다 — 실제 내용은 Don이 채운다.

| 이 노트의 단계 | Don의 경험에서 찾을 것 |
|---|---|
| 하드웨어 없는 추정 | [pre-silicon/FPGA 단계에서 성능·전력 기대값을 세운 일] |
| PoC·실측 | [새 실리콘 첫 bring-up에서 측정 기반으로 문제를 찾은 일] |
| 벤더 대응 평가 | [벤더/FAE와 버스 장애 root cause를 같이 찾은 일, 응답 품질] |
| gate·margin sign-off | [전력·성능 margin sign-off 기준과 판단] |
| 공급·양산 | [NPI → MP 전환에서 부품·공급 이슈를 다룬 일] |

---

## 9. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| peak TOPS로 순위를 매김 | 스펙 2배 칩이 LLM에서 더 느림 | decode는 대역폭에 묶인다 | 3절 추정기로 우리 모델 latency 비교 |
| AO 경로를 메인 칩 평가에 섞음 | 출시 후 배터리 목표 미달 | 바닥 전력·전환 비용 미평가 | AO gate를 따로, 전환 에너지까지 실측 (4절) |
| 칩 단가만 비교 | 싼 칩을 골랐는데 BOM이 더 비쌈 | 외부 MCU·radio·royalty 누락 | 단위비용 = 칩 + 동반 칩 + royalty (5.2절) |
| 공수를 안 셈 | EVT 지연, 인력 추가 | 툴체인 미성숙, op gap | op sweep → 3점 추정 → P80 (5.1절) |
| 후보끼리 min-max 정규화 | 후보 하나 추가로 순위가 바뀜 | rank reversal | 요구사항 기반 고정 앵커 (6.2절) |
| 점 추정 점수만 보고 | "0.05 차이로 B"가 실측 후 뒤집힘 | 불확실성 무시 | 범위 + Monte Carlo 승률 (6.4절) |
| 결과 보고 가중치 수정 | 결정이 신뢰를 잃음 | 골대 옮기기 | 가중치 사전 서명, 민감도로 답 (6.5절) |
| 벤더 숫자를 점수에 직접 | 조건이 다른 숫자 비교 | benchmarketing | 역산 후 조건 질문 (3.6절) |
| 계약에 안 넣음 | EOL 공지·지원 축소에 대응 못 함 | 구두 약속 | 수명·SLA·가격을 계약 조항으로 (7.6절) |
| 펌웨어 칸을 비워 둠 | 양산 직전 resume 지연·디버그 잠금 문제 | 아무도 BSP를 안 봄 | 8.1절 체크리스트를 PoC에 포함 |

---

## 10. 면접에서 이렇게 말한다

**Q.** "How would you choose the NPU or SoC for our next wearable?"

**A.** 칩이 아니라 요구사항에서 시작한다. 제품 기능을 예산(always-on mW, LLM tok/s, 메모리, 단가)으로 바꾸고, 몇 개의 must-have gate와 가중 점수 기준을 **평가 전에** 합의한다. RFI로 넓게 묻고, datasheet로 우리 모델의 latency 범위를 추정해서 shortlist를 만든 뒤, eval kit에서 우리 model zoo로 PoC를 한다. 결정은 점 추정 점수가 아니라 범위와 승률, 그리고 무엇이 결론을 뒤집는지까지 보여 주면서 여러 팀과 같이 내린다.

> "I'd start from the product, not the chip. First I turn the features into budgets — always-on power, LLM tokens per second, memory, unit cost — and agree on a few must-have gates and a weighted set of scored criteria before we look at any vendor. Then a broad RFI, a datasheet-level performance estimate on our own models to build a shortlist, and a four-to-six-week proof of concept on eval kits with our model zoo, measuring op coverage, accuracy, latency, power and thermals the same way on every candidate. The decision review shows the scorecard with uncertainty ranges, the probability each candidate wins, and what would flip the result — and it includes cost, engineering effort and supply risk, not just performance."

**Q.** "What criteria matter beyond TOPS?"

**A.** 실효 성능은 TOPS보다 대역폭·SRAM·활용률이 정한다 — LLM decode는 거의 전부 메모리 읽기다. 그다음 op coverage와 정밀도 지원, always-on 경로의 평균 전력과 전환 비용, 툴체인과 BSP의 성숙도, 벤더 지원, 그리고 단가·NRE·공수·공급 수명. 웨어러블에서는 배터리를 always-on 경로가 정하기 때문에 그것을 따로 gate로 둔다.

> "TOPS is a ceiling, not a prediction. What actually sets performance on our models is memory bandwidth, on-chip SRAM and utilization — LLM decode on a wearable is almost entirely a bandwidth problem. Beyond that I look at op coverage and precision support for our model zoo, the always-on path's average power including wake-up and retention costs, toolchain and BSP maturity, how the vendor responds to a real bug report, and the business side: unit cost including companion chips, NRE, engineering effort, and supply longevity. For a wearable I treat the always-on island as its own evaluation with its own power gate."

**Q.** "How do you estimate performance before you have the chip?"

**A.** roofline으로 한다. 모델마다 MAC과 DRAM 바이트를 세고, datasheet의 peak 연산과 대역폭에 활용률·실효 대역폭 비율·런타임 오버헤드를 **범위로** 넣어서 latency와 에너지의 낙관~비관 범위를 만든다. 그 범위로 명백한 탈락자를 거르고, 벤더 숫자는 역산해서 필요한 활용률이나 대역폭이 100%를 넘으면 측정 조건을 묻는다. 범위가 넓은 항목이 PoC에서 먼저 잴 항목이다.

> "With a roofline-style model. For each of our models I count MACs and DRAM bytes, then combine them with the datasheet's peak compute and bandwidth, using ranges for utilization, effective bandwidth and runtime overhead, to get optimistic-to-pessimistic latency and energy. That's enough to drop candidates that physically can't hit a gate — for example, a chip whose memory bandwidth caps LLM decode below our tokens-per-second floor. I also invert vendor claims: if a number would require more than the chip's peak bandwidth, the measurement conditions differ from ours, and I ask what they were. Whatever has the widest range is what I measure first once the kits arrive."

**Q.** "How do you account for software maturity in the decision?"

**A.** 정성 평가로 두지 않고 두 가지 숫자로 바꾼다. 첫째, 관찰 가능한 사실로 정의한 1~5 rubric(우리 모델이 fallback 없이 컴파일되나, 프로파일러가 있나, 재현 케이스에 며칠 만에 답하나). 둘째, 공수: op sweep으로 op gap을 세고, 항목별 3점 추정을 Monte Carlo로 합해서 P80 person-month와 팀 용량을 넘을 확률을 낸다. 그 공수가 TCO와 일정 리스크로 들어간다.

> "I turn it into numbers instead of leaving it as a gut feeling. One is a one-to-five rubric defined by observable facts — does our model zoo compile with zero CPU fallback, is there a per-layer profiler, how many days did the vendor take to root-cause a reproducer we deliberately sent. The other is engineering effort: the op-coverage sweep gives the number of op gaps, and three-point estimates per work item, summed by Monte Carlo, give a P80 person-month figure and the probability of exceeding the team's capacity before EVT. That effort flows into total cost and schedule risk, which is where immature software actually hurts."

**Q.** "How would you handle a vendor whose benchmark numbers look much better than your estimate?"

**A.** 무시하지도 믿지도 않는다. 그 숫자를 내려면 필요한 활용률과 대역폭을 역산한다. 물리적으로 불가능하면 조건이 다른 것이니 sparsity, batch, weight 상주, 모델 변형, 클럭을 묻고, 우리 모델·우리 조건으로 다시 재 달라고 하거나 PoC에서 직접 잰다. 점수표에는 우리 조건의 숫자만 들어간다.

> "I don't dismiss it and I don't put it in the scorecard. I back-calculate what utilization and bandwidth that number would require on the datasheet hardware. If it needs more than a hundred percent of peak bandwidth, the conditions are different — sparsity, batch size, weights resident in SRAM, a smaller model variant, a higher clock — so I ask exactly which, and request a rerun on our model under our conditions, or measure it myself during the PoC. Only numbers measured under our conditions go into the scorecard."

**Q.** "Tell me about a time you evaluated a vendor's silicon."

**A.** STAR로 답한다. 아래 괄호는 Don이 자기 경험으로 채울 자리다 — 지어내지 않는다.

- **S**: [어느 프로젝트에서, 어떤 새 실리콘/부품을, 어떤 일정 압박 속에서 — 예: SSD 컨트롤러 신규 리비전 bring-up 또는 RF 칩셋 통합]
- **T**: [Don이 맡은 책임 — 예: 성능·전력 margin sign-off, 벤더 이슈 root cause]
- **A**: [한 일 — 기대값을 먼저 세웠는지, 어떤 측정을 자동화했는지, 벤더와 어떤 형식으로 이슈를 주고받았는지, 어떤 데이터로 결정을 설득했는지]
- **R**: [결과 — 숫자로: 찾은 결함 수, 단축한 일정, sign-off 여부, 벤더가 수정한 errata]
- **연결 문장**: "ML 가속기 평가도 같은 구조다: 기대값 → 우리 workload로 실측 → 벤더와 데이터로 대화 → 기준에 따라 sign-off."

> "In my [role] at [company], we were integrating [new silicon] under [schedule constraint]. I was responsible for [specific responsibility]. Before the parts arrived I [set expectations / built a test plan], and once we had silicon I [measurements, automation, root-cause work with the vendor's FAEs]. The outcome was [quantified result]. Evaluating an ML accelerator follows the same pattern: set expectations from first principles, measure on our own workload, talk to the vendor with data, and sign off against criteria agreed up front."

**Q.** "What would make you revisit a silicon decision after it's made?"

**A.** 결정 문서에 미리 적어 둔 "결론을 뒤집는 조건"이 현실이 될 때다: 예를 들어 gate에 가까웠던 항목(AO 전력)이 실제 보드에서 넘어가거나, 계약에 수명·SLA가 안 들어가거나, 로드맵이 바뀌어 더 큰 모델이 필요해질 때. 그때도 지금까지 쓴 비용은 빼고(sunk cost), **남은** 비용과 리스크만으로 다시 비교한다.

> "The triggers should already be written in the decision record — the conditions under which the ranking flips. For example, if a criterion that was close to its gate, like always-on power, fails on our real board, if the contract can't include the longevity and support terms we assumed, or if the roadmap changes and we need a much larger model. When that happens I re-run the comparison using only the remaining cost and risk from that point on — what we've already spent is sunk and shouldn't drive the choice."

---

## 11. 직접 해보기

1. **손계산**: Chip B에서 LLM decode 1 token의 기준 latency와 tok/s를 3.3절처럼 계산하라 (BW 17.1 GB/s, e = 0.65, 오버헤드 0.3 ms, 바이트 303.1 MB). 정답: 303.1e6 / (17.1e9 × 0.65) = 27.27 ms, + 0.3 = 27.57 ms → 36.3 tok/s (예제 1의 27.6 ms와 일치).
2. **손계산**: Chip B의 AO 경로를 hop 30 ms로 돌리면 평균 전력은? (한 hop 9.56 µJ, 바닥 0.26 mW). 정답: 0.26 + 9.56 / 30 = 0.579 mW.
3. **코드**: `ANCHOR["tps"]`를 (10, 80)으로 바꿔 "2세대에 더 큰 모델이 필요하다"는 판단을 반영하고 예제 8·10을 다시 돌려라. 1등이 바뀌나? tps 가중치 전환점은? 정답: A 0.571, B 0.604로 여전히 B. 하지만 tps 가중치 전환점이 ×3.75(40%)에서 ×1.90(25%)로 크게 내려온다 — 로드맵 판단 하나가 결정의 견고함을 바꾼다.
4. **손계산**: C의 벤더가 NRE를 300k$ 깎아 줘서 C의 고정비가 730k$가 되면 B→C break-even 수량은? 정답: (730 − 388)k / 0.8 = 427,500대.
5. **코드 과제**: 예제 9에서 칩끼리 독립으로 뽑은 비용을 "공통 DRAM 가격 요인"으로 묶어 보라 (한 표본에서 모든 칩의 비용에 같은 배수 z ~ triangular(0.9, 1, 1.2)를 곱한다). 승률이 어떻게 변하나? 힌트: 공통 요인은 순위에 덜 영향을 준다 — 모두 같이 오르기 때문이다. 대신 DRAM 비중이 큰 칩(A)이 더 영향을 받게 하려면 칩별 DRAM 금액에만 z를 곱한다.
6. **설계 과제**: Hark 같은 웨어러블의 AO 섬 RFI 질문 10개를 4.4절 체크리스트에서 골라 벤더에게 보낼 문장으로 써 보라. 힌트: 각 질문이 "어떤 숫자를, 어떤 조건에서" 달라고 하는지 명시한다 (예: "retention sleep 전력을 25 °C, SRAM 256 KB 유지 조건에서").

---

## 12. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| RFI | Request for Information | 벤더에게 넓게 정보를 묻는 첫 질문지 |
| RFQ | Request for Quotation | 수량·조건을 정해 가격·조건 견적을 요청 |
| PoC | Proof of Concept | eval kit에서 우리 workload로 하는 짧은 실증 |
| eval kit / EVB | 평가 보드 | 벤더가 제공하는 개발·측정용 보드 |
| gate | must-have 조건 | 통과 못 하면 점수와 무관하게 탈락 |
| 효용 (utility) | 0~1로 바꾼 기준값 | 단위가 다른 기준을 더하기 위한 변환 |
| 앵커 | 효용의 0점·1점 기준값 | 후보가 아니라 요구사항에서 정한다 |
| rank reversal | 순위 역전 | 후보를 추가·제거했을 뿐인데 다른 후보의 순위가 바뀌는 현상 |
| Monte Carlo | 무작위 표본 반복 | 불확실한 입력을 여러 번 뽑아 결과의 분포를 얻는 방법 |
| Dirichlet 분포 | 합이 1인 무작위 벡터의 분포 | 가중치 자체의 불확실성을 표현 |
| swing | 한 기준을 범위 끝으로 미는 분석 | 어느 칸이 결론을 뒤집을 수 있는지 찾는다 |
| Pareto front | 지배당하지 않는 후보 집합 | 모든 축에서 더 나은 후보가 없는 후보들 |
| PERT | 3점 추정법 | (o + 4m + p) / 6로 기대 공수를 계산 |
| person-month (PM) | 인월 | 한 사람이 한 달 하는 일의 양 |
| TCO | Total Cost of Ownership | 단가 × 수량 + 고정비(NRE·공수) |
| NRE | Non-Recurring Engineering | 수량과 무관하게 한 번 드는 개발 비용 |
| BOM | Bill of Materials | 기기 하나에 들어가는 부품 목록과 비용 |
| break-even 수량 | 손익 분기 수량 | 두 후보의 총비용이 같아지는 생산 수량 |
| LTB / EOL | Last-Time-Buy / End-of-Life | 단종 공지와 마지막 구매 기회 |
| allocation | 공급 배정 | 공급 부족 시 고객별로 물량을 나누는 것 |
| 2nd source | 대체 공급원 | 같은 부품 또는 호환 부품의 다른 공급 경로 |
| escrow | 위탁 보관 | 벤더 소스·툴을 제3자에 맡겨 벤더 소멸에 대비 |
| benchmarketing | 마케팅용 벤치마크 | 유리한 조건에서만 잰 숫자 |
| sunk cost | 매몰 비용 | 이미 써서 회수할 수 없는 비용 — 결정에서 빼야 한다 |
| break-even 시간 (t_be) | 잠들 가치가 있는 최소 시간 | 전환 에너지 ÷ (idle 전력 − sleep 전력) |

---

## 13. 요약 & 체크리스트

칩 선정은 TOPS 비교가 아니라 **요구사항에서 출발하는 시스템 결정**이다. 예산에서 소수의 gate와 가중 기준을 평가 전에 합의하고, 32개 안팎의 기준마다 출처와 신뢰도를 붙인다. 칩이 오기 전에는 roofline 추정기로 우리 모델의 latency·에너지 범위를 계산해서 탈락자를 거르고 벤더 숫자를 역산한다. always-on 경로는 바닥 전력과 전환 비용까지 넣어 따로 gate를 둔다. 소프트웨어 성숙도는 rubric과 person-month로, 비용은 TCO와 break-even 수량으로, 공급은 기대 비용과 꼬리로 숫자화한다. 결정은 고정 앵커 효용의 가중합에 Monte Carlo 승률과 민감도(가중치 전환점, swing)를 붙여서 "B, 승률 약 90%, 소프트웨어 가중치가 30%를 넘으면 A"처럼 말한다. 과정은 RFI → shortlist → NDA·kit → PoC → RFQ → decision review → 계약으로 약 16주이고, 펌웨어 칸(BSP, 전원 상태, 디버그 접근)과 조직적 함정(benchmarketing, sunk cost, "벤더가 고쳐 줄 것")을 놓치지 않는 것이 Don이 더할 수 있는 가치다.

- [ ] TOPS 외의 기준 10개 이상을 그룹별로 말하고, 각각을 어디서 얻는지와 신뢰도를 말할 수 있다
- [ ] gate와 점수를 나누는 규칙(없으면 제품이 안 되는가 / 더 좋으면 더 좋은가)을 설명할 수 있다
- [ ] datasheet 숫자로 LLM decode tok/s를 손으로 계산하고, 왜 TOPS가 아니라 대역폭이 정하는지 말할 수 있다
- [ ] 벤더 숫자에서 필요한 활용률·대역폭을 역산하고 조건 질문을 만들 수 있다
- [ ] AO 경로의 평균 전력을 전환 에너지·reload까지 넣어 손으로 계산할 수 있다
- [ ] 3점 추정으로 공수를 내고 P50 대신 P80으로 계획하는 이유를 말할 수 있다
- [ ] break-even 수량을 손으로 계산하고 수량이 결론을 바꾸는 예를 들 수 있다
- [ ] 고정 앵커 효용이 min-max 정규화보다 나은 이유(rank reversal)를 설명할 수 있다
- [ ] Monte Carlo 승률과 가중치 전환점으로 결정의 견고함을 한 문장으로 말할 수 있다
- [ ] RFI부터 계약까지의 단계와 각 단계의 산출물, 그리고 조직적 함정 3개를 말할 수 있다

---

## 참고 자료

- Williams, Waterman, Patterson, "Roofline: An Insightful Visual Performance Model for Multicore Architectures", Communications of the ACM 52(4), 2009 — 3절 추정기의 기반
- Horowitz, "Computing's Energy Problem (and what we can do about it)", ISSCC 2014 — 연산 대 데이터 이동 에너지 (D7에서 자세히)
- Sze, Chen, Yang, Emer, "Efficient Processing of Deep Neural Networks: A Tutorial and Survey", Proceedings of the IEEE, 2017 (arXiv:1703.09039) — 가속기 평가 지표와 활용률
- Banbury et al., "MLPerf Tiny Benchmark", NeurIPS Datasets and Benchmarks, 2021 (arXiv:2106.07597) · Reddi et al., "MLPerf Inference Benchmark", ISCA 2020 (arXiv:1911.02549) — 공개 벤치마크의 규칙 (I5)
- Malcolm, Roseboom, Clark, Fazar, "Application of a Technique for Research and Development Program Evaluation", Operations Research 7(5), 1959 — PERT 3점 추정의 원전
- Keeney, Raiffa, "Decisions with Multiple Objectives: Preferences and Value Tradeoffs", Wiley, 1976 — 다기준 효용 함수와 가중치의 고전
- Arkes, Blumer, "The Psychology of Sunk Cost", Organizational Behavior and Human Decision Processes 35(1), 1985 — sunk cost 편향
- numpy 문서: `numpy.random.Generator.triangular`, `numpy.random.Generator.dirichlet` (numpy.org) — 예제 4·7·9의 표본 추출
- 이 노트 세트: D1, D3, D5, D7(성능·에너지 모델), E5, E7(NPU·메모리), F4, F8(벤더 스택·bring-up), C8(검증), I1, I3, I5(예산·partitioning·벤치마크), K2~K4(측정), L1(SLM 선정의 같은 구조), M2~M4(벤치마크 방법론·벤더 지형·데이터시트 해석)
