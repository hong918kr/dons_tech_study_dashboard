# J5. 모델 업데이트와 OTA — 패키징, 호환성, A/B 슬롯, 서명, 롤백, 단계적 배포

> **이 노트를 다 읽으면**: "이번 변경은 모델만 보내면 되나, 펌웨어도 보내야 하나"를 규칙으로 판정할 수 있다 · 모델 패키지 헤더(magic·포맷 버전·모델 ID·semver·런타임 ABI·op set·입출력 텐서 spec·전처리 버전·arena·HW ID·보안 버전)를 설계하고, Python 패키저와 C 검증기를 직접 짜서 잘못된 패키지를 거부할 수 있다 · A/B 모델 슬롯과 영속 메타데이터 상태 기계(쓰기 → 검증 → pending → 안전 지점 활성화 → golden vector self-test → 확정 또는 롤백)를 만들고, 모든 flash 연산에서 전원을 끊어도 기기가 항상 유효한 모델로 부팅함을 시뮬레이션으로 보일 수 있다 · 서명·anti-rollback·암호화의 역할을 구분하고, 결정적 cohort·자동 halt·shadow mode로 새 wake-word 모델을 안전하게 배포하는 계획을 숫자로 말할 수 있다
> **JD 연결**: "Integrate ML inference into embedded firmware in C, C++, or Rust" — 통합의 마지막 단계는 "출시 후에도 모델을 안전하게 바꾸는 것"이다 · study_prep_list **J5** 행: 모델 버전 관리, 런타임 호환성, A/B 슬롯·OTA로 모델 교체 (P1, Don 출발점: 🟡 SSD FW 업데이트 흐름)
> **Don 기준 난이도**: NVMe Firmware Image Download → Firmware Commit → reset 후 활성화, 펌웨어 slot, 이미지 서명 검증, 전원 차단 안전성(PLP), 양산 FW 버전 관리는 이미 몸에 익었다 / 새로 배울 것은 **모델이 펌웨어보다 훨씬 자주 바뀐다**는 운영 리듬, 모델과 런타임 사이의 호환성 축(op set·ABI·전처리·arena), "통과했지만 품질이 나쁜" 업데이트를 잡는 통계적 rollout 판정, threshold처럼 모델에 묶인 파라미터의 원자적 배포
> **선행 노트**: E8(§6 부팅 체인·여러 이미지의 OTA), F7(§9.4 모델 헤더 + CRC), F3·F8(버전 매트릭스), H1(§6 전원 차단 안전 chunk commit), H2(재개 가능한 업로드 — 이 노트는 반대 방향), H8(§2 원격 config·서명·staged rollout·kill switch, §3 결정적 cohort, §7 필드 피드백), I1(§4 OTA A/B flash 예산), I4(전처리 배치), C8(§5 golden vector), H7(fleet 모니터링). 이 노트는 그 메커니즘을 다시 설명하지 않고 **한 줄 복습 + ID**로 가리킨다

---

## 0. 큰 그림 — 모델은 펌웨어보다 자주 바뀐다

### 0.1 왜 자주 바뀌나 — data flywheel

H8에서 본 루프를 다시 떠올리자. 기기가 필드에서 데이터를 모으고(H1·H2), 서버가 받아 라벨을 붙이고(H3·H4), 데이터셋 버전을 새로 자르고(H5), 모델을 다시 학습하고, 그 모델을 **다시 기기로 보낸다**. 이 루프가 한 바퀴 돌 때마다 새 모델이 하나 나온다. 이것을 data flywheel(데이터 플라이휠)이라고 부른다 — 기기가 많을수록 데이터가 많고, 데이터가 많을수록 모델이 좋아지고, 좋은 모델이 더 많은 사용을 부른다는 뜻이다.

펌웨어와 모델의 리듬은 다르다. 아래 숫자는 설명용 가정이지만 방향은 일반적이다.

| 항목 | 펌웨어(코드) | 모델(데이터) |
|---|---|---|
| 바뀌는 이유 | 버그 수정, 기능 추가, 보안 패치 | 새 데이터, hard negative 보강, 새 사용자 층, threshold 조정 |
| 배포 주기 (가정) | 분기 1회 ~ 월 1회 | 2주 ~ 월 1회, 긴급 threshold는 수일 |
| 바뀌는 범위 | 코드 경로·드라이버·메모리 지도 | 가중치 수십~수백 KB, 가끔 그래프 구조 |
| 검증 방식 | 기능 테스트, 회귀, HIL | 정확도 회귀(C8 §6), golden vector, 필드 통계 |
| 실패 모양 | 크래시, hang, 기능 고장 | **조용히 나빠짐**: 오탐 증가, 놓침 증가, 배터리 소모 |

마지막 행이 이 노트 전체의 동기다. 펌웨어 업데이트가 실패하면 대개 시끄럽다(크래시, watchdog). 모델 업데이트는 **부팅도 되고 추론도 도는데 결과만 나빠지는** 방식으로 실패한다. 그래서 "기기가 벽돌이 되지 않는다"(전원 차단 안전성)와 "기능이 나빠지지 않는다"(단계적 배포와 통계 판정) 두 가지를 모두 설계해야 한다.

### 0.2 전체 흐름 한 장

```svg
<svg viewBox="0 0 680 360" xmlns="http://www.w3.org/2000/svg"><defs><marker id="j5a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="22" font-size="13">모델 OTA의 전체 흐름 — 서버(위)와 기기(아래), 그리고 다시 학습으로</text><rect x="20" y="40" width="110" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="75" y="60" font-size="12" text-anchor="middle">학습 · 평가</text><text x="75" y="78" font-size="12" text-anchor="middle">H5 데이터 버전</text><line x1="130" y1="64" x2="150" y2="64" stroke="currentColor" marker-end="url(#j5a)"/>
<rect x="152" y="40" width="110" height="48" rx="6" fill="#4a7bd0" fill-opacity="0.18" stroke="currentColor"/><text x="207" y="60" font-size="12" text-anchor="middle">변환 · 양자화</text><text x="207" y="78" font-size="12" text-anchor="middle">C1·C2·F1</text><line x1="262" y1="64" x2="282" y2="64" stroke="currentColor" marker-end="url(#j5a)"/><rect x="284" y="40" width="110" height="48" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="339" y="60" font-size="12" text-anchor="middle">패키징 · 서명</text><text x="339" y="78" font-size="12" text-anchor="middle">§2 · §5</text><line x1="394" y1="64" x2="414" y2="64" stroke="currentColor" marker-end="url(#j5a)"/>
<rect x="416" y="40" width="110" height="48" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="471" y="60" font-size="12" text-anchor="middle">카탈로그 · cohort</text><text x="471" y="78" font-size="12" text-anchor="middle">§3 · §6</text><line x1="526" y1="64" x2="546" y2="64" stroke="currentColor" marker-end="url(#j5a)"/><rect x="548" y="40" width="112" height="48" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="604" y="60" font-size="12" text-anchor="middle">check-in 응답</text><text x="604" y="78" font-size="12" text-anchor="middle">capability 협상</text>
<line x1="604" y1="88" x2="604" y2="146" stroke="currentColor" marker-end="url(#j5a)"/><text x="612" y="122" font-size="12">다운로드</text><line x1="20" y1="118" x2="660" y2="118" stroke="currentColor" stroke-dasharray="4 4"/><text x="20" y="112" font-size="12">서버</text><text x="20" y="134" font-size="12">기기</text><rect x="548" y="148" width="112" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="604" y="168" font-size="12" text-anchor="middle">B 슬롯에 쓰기</text><text x="604" y="186" font-size="12" text-anchor="middle">H2 재개 가능</text><line x1="548" y1="172" x2="528" y2="172" stroke="currentColor" marker-end="url(#j5a)"/>
<rect x="416" y="148" width="110" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="471" y="168" font-size="12" text-anchor="middle">검증</text><text x="471" y="186" font-size="12" text-anchor="middle">서명 · hash · 호환</text><line x1="416" y1="172" x2="396" y2="172" stroke="currentColor" marker-end="url(#j5a)"/><rect x="284" y="148" width="110" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="339" y="168" font-size="12" text-anchor="middle">pending 표시</text><text x="339" y="186" font-size="12" text-anchor="middle">안전 지점 대기</text><line x1="284" y1="172" x2="264" y2="172" stroke="currentColor" marker-end="url(#j5a)"/>
<rect x="152" y="148" width="110" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="207" y="168" font-size="12" text-anchor="middle">시험 활성화</text><text x="207" y="186" font-size="12" text-anchor="middle">golden self-test</text><line x1="152" y1="172" x2="132" y2="172" stroke="currentColor" marker-end="url(#j5a)"/><rect x="20" y="148" width="110" height="48" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="75" y="168" font-size="12" text-anchor="middle">확정 또는</text><text x="75" y="186" font-size="12" text-anchor="middle">롤백 (A로)</text><line x1="75" y1="196" x2="75" y2="226" stroke="currentColor" marker-end="url(#j5a)"/>
<rect x="20" y="228" width="240" height="48" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="140" y="248" font-size="12" text-anchor="middle">텔레메트리: self-test 결과, 오탐률,</text><text x="140" y="266" font-size="12" text-anchor="middle">에러 카운터, 배터리 (H7)</text><line x1="260" y1="252" x2="300" y2="252" stroke="currentColor" marker-end="url(#j5a)"/><rect x="302" y="228" width="200" height="48" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor"/><text x="402" y="248" font-size="12" text-anchor="middle">rollout 판정</text><text x="402" y="266" font-size="12" text-anchor="middle">다음 단계 · halt · rollback</text>
<polyline points="402,228 402,210 471,210 471,90" fill="none" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#j5a)"/><polyline points="140,276 140,320 75,320 75,90" fill="none" stroke="#4a7bd0" stroke-dasharray="5 3" marker-end="url(#j5a)"/><text x="150" y="314" font-size="12">필드 데이터 → 다음 학습 (data flywheel, H8 §7)</text><text x="20" y="346" font-size="12">빨간 점선: 판정이 카탈로그·cohort를 바꾼다. 파란 점선: 필드 데이터가 다음 모델이 된다.</text></svg>
```

그림 1 — 모델 OTA의 전체 흐름. 윗줄은 서버 쪽(학습 → 변환 → 패키징·서명 → 카탈로그 → check-in 응답), 아랫줄은 기기 쪽(쓰기 → 검증 → pending → 시험 활성화 → 확정/롤백), 맨 아래는 운영 루프다. 이 노트의 절 번호가 각 상자에 붙어 있다.

### 0.3 Don의 출발점 — NVMe firmware download/commit

Don은 SSD에서 이 흐름의 절반을 이미 만들었다. 호스트가 Firmware Image Download로 이미지를 조각조각 내려보내고, Firmware Commit으로 "어느 slot에 넣고 언제 활성화할지"를 정하고, reset(또는 reset 없는 활성화) 뒤에 새 펌웨어가 돈다. 이미지는 서명 검증을 거치고, 실패하면 이전 slot으로 부팅한다. 8절에서 이 대응을 표로 정리한다. 지금은 한 가지만 기억하자: **SSD 펌웨어 업데이트에서 "download"와 "activate"를 분리한 이유가 모델 OTA에서도 그대로 성립한다** — 쓰는 일은 언제든 해도 되지만, 바꿔 끼우는 일은 안전한 순간에만 해야 한다.

### 0.4 앞 노트에서 가져오는 것 — recap

| 노트 | 한 줄 복습 | 이 노트에서 쓰는 곳 |
|---|---|---|
| E8 §6 | Boot ROM부터 단계마다 다음 이미지 서명을 검증(chain of trust). 이기종 기기의 펌웨어는 AP·DSP·NPU·MCU 이미지의 **세트** | §5 서명, §7 다중 이미지 |
| F7 §9.4 | 32 B 헤더(magic·버전·길이·CRC·arena) + readback CRC 검증 | §2에서 확장 |
| F3 · F8 §2 | "그 결과는 어떤 SDK·런타임·모델 조합에서 나왔나" 버전 매트릭스 | §3 호환성 |
| H1 §6 | 데이터 → CRC → commit marker 순서로 쓰면 어디서 끊겨도 "그 chunk 전체를 버린다"로 수렴 | §4 상태 기계 |
| H2 §4 | 끊겨도·중복돼도·깨져도 이어지는 업로드 (offset·chunk hash) | 다운로드는 반대 방향으로 같은 기법 |
| H8 §2~3 | 서명된 원격 config, capability 협상, staged rollout, kill switch, salt 넣은 hash cohort | §3 · §6 |
| I1 §4 | flash를 처음부터 A/B 두 슬롯으로 그려야 모델 몫이 나온다 | §4.1 |
| I4 | 전처리를 어디서 하느냐(MCU·DSP·모델 안) — 전처리 버전이 모델과 짝 | §2 · §7 |
| C8 §5 | 기준 모델에서 golden vector를 뽑아 펌웨어에서 비교 | §4.4 self-test |
| H7 §3 | 나쁜 릴리스는 일부 HW에서만 드러난다 — 쪼개서 봐야 한다(희석 산수) | §6 halt 판정 |

---

## 1. 모델만 보낼까, 펌웨어도 보낼까

### 1.1 직관 — 악보와 연주자

모델 파일은 **악보**이고, 펌웨어 안의 런타임(TFLM 인터프리터, 커널, 전처리 코드)은 **연주자**다. 악보를 바꾸는 것은 쉽다. 하지만 새 악보에 연주자가 모르는 기법(새 op)이 나오거나, 악기 편성(입력 특징의 모양)이 바뀌거나, 무대가 좁으면(arena 부족) 연주자를 바꿔야 한다 — 즉 펌웨어 업데이트가 필요하다.

모델 파일(예: `.tflite` flatbuffer)에 들어 있는 것과 펌웨어에 들어 있는 것을 나눠 보자.

| 들어 있는 곳 | 내용 | 바꾸려면 |
|---|---|---|
| 모델 파일 | 그래프 구조(어떤 op가 어떤 순서로), 가중치, 양자화 파라미터(scale·zero point), 텐서 shape | 모델 OTA |
| 펌웨어 — 런타임 | op 커널 구현(CMSIS-NN 등), 인터프리터, 지원하는 flatbuffer 스키마 버전 | 펌웨어 OTA |
| 펌웨어 — 통합 코드 | arena 크기(정적 배열), 입력 버퍼, 전처리(MFCC·정규화), 후처리(smoothing·refractory), 센서 설정 | 펌웨어 OTA (단, 파라미터로 빼 두면 모델 번들로) |
| 파라미터 번들 | threshold, smoothing window, mel 설정값, 정규화 상수, label map | 번들 OTA — **모델과 짝**(§7) |

세 번째 행이 설계의 핵심이다. 전처리나 후처리의 **코드**는 펌웨어에 있어야 하지만, 그 **숫자**(threshold, mel bin 수, 정규화 평균·분산)를 파라미터로 빼 두면 모델과 함께 보낼 수 있다. 반대로 숫자를 `#define`으로 박아 두면 threshold 하나 바꾸는 데도 펌웨어 릴리스가 필요하다.

### 1.2 판정 규칙

새 모델이 다음 중 하나라도 해당하면 **펌웨어 업데이트가 먼저(또는 함께)** 필요하다.

1. 런타임이 모르는 op를 쓴다 (예: GRU 추가, 새 activation).
2. 변환기(converter) 스키마나 런타임 ABI가 바뀌었다 (예: 새 TFLite 변환기가 만든 flatbuffer를 옛 인터프리터가 못 읽는 경우).
3. 필요한 arena가 펌웨어가 잡아 둔 정적 arena보다 크다 (D2·I1).
4. 전처리 알고리즘이 바뀌었다 (예: mel bin 40 → 64, 창 길이 변경) — 전처리 코드가 펌웨어에 있다면.
5. 모델 파티션(슬롯)보다 크다 — flash 파티션 표를 바꿔야 하므로 bootloader·펌웨어 영역.
6. NPU·DSP용으로 컴파일된 바이너리가 특정 NPU 펌웨어·SDK 버전에 묶여 있다 (F4 — Qualcomm QNN의 context binary처럼 컴파일된 그래프는 SDK·HW 버전과 짝인 경우가 많다고 알려져 있다, hedge).

어느 것에도 해당하지 않으면 **모델만** 보낸다. 가중치만 바뀌는 재학습은 거의 항상 여기다.

### 1.3 코드로 확인 — 예제 1: 변경 7가지 판정

**예제 1** — 무엇을 확인하나: 현재 펌웨어 1.5의 능력(ABI, op 집합, arena, 전처리 버전, 슬롯 크기)과 새 모델의 요구를 비교해, 변경마다 "모델만 / 펌웨어 + 모델 / 파라미터만"을 판정한다.

```python
# 이 변경은 모델만 보내면 되나, 펌웨어도 보내야 하나? — 현재 FW 1.5의 능력과 비교
FW = dict(abi={1, 2}, ops={"CONV_2D", "DEPTHWISE_CONV_2D", "FULLY_CONNECTED", "SOFTMAX",
          "RESHAPE", "AVERAGE_POOL_2D", "ADD"}, arena=64 * 1024, preproc={2, 3}, slot=256 * 1024)
BASE = dict(abi=2, ops={"CONV_2D", "DEPTHWISE_CONV_2D", "FULLY_CONNECTED", "SOFTMAX"},
            arena=48 * 1024, preproc=3, size=200 * 1024, cfg_only=False)
def need(m):
    if m["cfg_only"]: return "params only", []
    why = [f"ABI {m['abi']}"] * (m["abi"] not in FW["abi"])
    why += [f"op {o}" for o in sorted(m["ops"] - FW["ops"])]
    why += [f"arena {m['arena'] // 1024} KB > {FW['arena'] // 1024}"] * (m["arena"] > FW["arena"])
    why += [f"preproc v{m['preproc']}"] * (m["preproc"] not in FW["preproc"])
    why += [f"size {m['size'] // 1024} KB > slot"] * (m["size"] > FW["slot"])
    return ("model-only" if not why else "FIRMWARE + model"), why
changes = {"retrained weights, same graph": {},
           "threshold 0.83 -> 0.88": dict(cfg_only=True),
           "wider layers, arena 60 KB": dict(arena=60 * 1024, size=240 * 1024),
           "add GRU layer": dict(ops=BASE["ops"] | {"GRU"}, arena=70 * 1024),
           "40 -> 64 mel bins (preproc v4)": dict(preproc=4),
           "new converter schema (ABI 3)": dict(abi=3),
           "bigger model 300 KB": dict(size=300 * 1024)}
for name, delta in changes.items():
    kind, why = need({**BASE, **delta})
    print(f"{name:32s} -> {kind:16s} {', '.join(why)}")
```

```text
retrained weights, same graph    -> model-only       
threshold 0.83 -> 0.88           -> params only      
wider layers, arena 60 KB        -> model-only       
add GRU layer                    -> FIRMWARE + model op GRU, arena 70 KB > 64
40 -> 64 mel bins (preproc v4)   -> FIRMWARE + model preproc v4
new converter schema (ABI 3)     -> FIRMWARE + model ABI 3
bigger model 300 KB              -> FIRMWARE + model size 300 KB > slot
```

출력에서 볼 것:

- 재학습과 "조금 넓힌 층"은 모델만으로 된다. 펌웨어가 **여유**(arena 64 KB, 슬롯 256 KB)를 미리 잡아 두었기 때문이다. I1 §8에서 말한 margin이 여기서 "펌웨어 릴리스 없이 모델을 키울 수 있는 공간"으로 돌아온다.
- GRU 추가는 이유가 두 개다(op 없음 + arena 초과). 펌웨어 릴리스 계획에 "다음 모델이 쓸 op를 미리 넣어 두기"를 포함하면, 모델 팀이 펌웨어 일정에 덜 묶인다. 단 쓰지 않는 커널은 flash를 먹는다 — op를 미리 넣는 것도 예산 결정이다.
- threshold만 바꾸는 경우는 "파라미터만"이지만, §7에서 보듯 threshold는 **모델 버전에 묶여** 있다. 그래서 실제로는 "같은 가중치 + 새 threshold"인 **새 번들 버전**으로 보낸다.

### 1.4 펌웨어가 모델을 "기다리게" 만드는 설계

판정 규칙을 거꾸로 쓰면 펌웨어 설계 지침이 된다.

- **op는 넉넉하게, 그러나 측정해서**: 다음 두 모델 세대가 쓸 법한 op(ADD, MUL, 다른 pooling)를 펌웨어에 미리 등록한다. TFLM의 `MicroMutableOpResolver`에 넣는 op 수는 flash 비용과 직결된다(F2).
- **arena와 슬롯에 성장 여유**: I1 §8의 "모델 교체 → 슬롯·arena에 성장 여유" 행.
- **전처리를 파라미터화**: mel bin 수, 창 길이, 정규화 상수를 코드 상수가 아니라 번들 파라미터로. 단 바뀔 수 있는 범위를 펌웨어가 검사해야 한다(예: mel bin ≤ 64).
- **입출력 텐서 spec을 모델에서 읽기**: 입력 shape·scale·zero point를 하드코딩하지 않고 패키지 헤더(또는 flatbuffer)에서 읽어 전처리 출력 양자화에 쓴다(I4 §6). 하드코딩하면 재학습으로 입력 scale만 바뀌어도 입력이 조용히 틀어진다.

### 1.5 함정

- **"가중치만 바꿨다"는 말을 믿지 않는다**: 변환기 버전이 바뀌면 같은 모델도 다른 op(예: 융합 방식이 다른 op)로 나올 수 있다. 판정은 사람의 말이 아니라 **패키지에서 뽑은 op 목록**으로 한다.
- **입력 양자화 파라미터 변경**: 재학습 후 입력 scale이 0.1176에서 0.1250으로 바뀌었는데 펌웨어가 옛 scale로 특징을 양자화하면, 모델은 돌지만 정확도가 떨어진다. 헤더에 입력 spec을 넣고 펌웨어가 그것을 쓰거나, 최소한 일치를 검사한다.
- **전처리 버전을 모델과 따로 관리**: 같은 MFCC라도 FFT 크기·창 함수·로그 바닥값이 다르면 특징 분포가 바뀐다(G5·I4). 전처리 버전 번호는 **모델이 학습된 전처리**를 가리켜야 한다.

---

## 2. 모델 패키지 포맷 설계

### 2.1 직관 — 송장이 붙은 봉인된 상자

택배 상자에는 송장(받는 사람, 무게, 취급 주의)이 붙어 있고, 상자는 봉인되어 있다. 받는 쪽은 상자를 열기 전에 송장을 보고 "우리 집 물건이 맞나, 들어갈 자리가 있나"를 판단하고, 봉인이 뜯겼으면 받지 않는다. 모델 패키지도 같다.

- **헤더**(송장): 이 모델이 무엇이고, 무엇을 요구하는지. 기기가 payload를 해석하기 **전에** 읽는다.
- **payload**(내용물): `.tflite` flatbuffer, NPU context binary, GGUF(F3) 같은 런타임이 먹는 바이트.
- **metadata**(동봉 서류): 학습 데이터 버전(H5), git commit, 검증 지표, threshold 같은 번들 파라미터. 사람이 추적할 때 쓰고, 일부는 기기가 쓴다.
- **서명**(봉인): 헤더 + payload + metadata 전체에 대한 서명. 하나라도 바뀌면 거부.

### 2.2 헤더 필드 — 무엇을 왜 넣나

F7 §9.4의 32 B 헤더를 실전용으로 키운다. 크기는 128 B로 고정한다 — 16의 배수라 payload 정렬(F7 §3.5)이 유지되고, 나중 필드를 위한 여유도 있다.

| 오프셋 | 크기 | 필드 | 왜 필요한가 | 누가 검사하나 |
|---|---|---|---|---|
| 0 | 4 | magic `MPK1` | 엉뚱한 파일(0xFF로 지워진 슬롯 포함) 거르기 | 기기 |
| 4 | 2 | fmt_version | 헤더 레이아웃 자체의 버전 — 모르는 포맷은 해석하지 않는다 | 기기 |
| 6 | 2 | header_len | 미래에 헤더가 커져도 옛 파서가 payload 위치를 안다 | 기기 |
| 8 | 4 | model_id | 어떤 기능의 모델인가 (KWS·제스처·VAD) — 엉뚱한 슬롯에 들어가는 것 방지 | 기기 · 서버 |
| 12 | 4 | semver | 기능 버전 major.minor.patch — 사람과 대시보드용 | 서버 · 텔레메트리 |
| 16 | 2 | runtime_abi | 이 payload를 읽으려면 필요한 런타임 계약 번호 (스키마·커널 의미) | 기기 |
| 18 | 2 | preproc_ver | 학습 때 쓴 전처리 버전 (I4) | 기기 |
| 20 | 4 | hw_id | 대상 HW (보드·마이크 rev) — 다른 HW용 모델 설치 방지 | 기기 · 서버 |
| 24 | 4 | arena_bytes | 필요 arena — 설치 **전에** 거부 가능 | 기기 |
| 28 | 4 | payload_len | payload 크기 | 기기 |
| 32 | 4 | meta_len | metadata 크기 | 기기 |
| 36 | 4 | security_ver | anti-rollback 카운터와 비교 (§5.4) | 기기 |
| 40 | 8 | ops_mask | 필요한 op 집합 (비트 = op 번호) | 기기 · 서버 |
| 48 | 20 | input spec | dtype, ndim, dims[4], scale, zero point | 기기 (전처리 양자화) |
| 68 | 20 | output spec | 같은 형식 | 기기 (후처리) |
| 88 | 32 | payload SHA-256 | payload 무결성 — 슬롯에 쓴 뒤 readback 검증에 재사용 | 기기 |
| 120 | 4 | flags | 예약 (암호화 여부 등) | 기기 |
| 124 | 4 | header CRC-32 | 서명 검사 전에 싸게 "헤더가 깨졌나" 판정 | 기기 |

op set을 **비트마스크**로 넣은 이유: hash(예: 정렬한 op 이름 목록의 SHA-256)는 "같은가"만 말해 준다. 비트마스크는 "**부분집합인가**"를 한 번의 AND로 판정한다 — `required & ~supported`가 0이 아니면 모르는 op가 있다. op가 64개를 넘을 일은 MCU 런타임에서 드물지만, 넘으면 비트 배열을 늘리거나 op 번호 목록을 metadata에 둔다. ONNX의 `opset_import`, TFLite flatbuffer 안의 operator code 목록도 같은 정보를 담고 있으므로, 패키저가 그것을 읽어 비트마스크를 **자동 생성**해야 한다(사람이 손으로 쓰지 않는다).

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><text x="20" y="22" font-size="13">MPK1 패키지 레이아웃 (예제 2의 정상 패키지, 총 20,270 B)</text><rect x="20" y="40" width="64" height="40" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><text x="52" y="65" font-size="12" text-anchor="middle">헤더</text><rect x="84" y="40" width="500" height="40" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="334" y="65" font-size="12" text-anchor="middle">payload — .tflite flatbuffer 20,008 B (16 B 정렬 시작)</text><rect x="584" y="40" width="40" height="40" fill="#3f9a6b" fill-opacity="0.3" stroke="currentColor"/><text x="604" y="65" font-size="12" text-anchor="middle">meta</text>
<rect x="624" y="40" width="36" height="40" fill="#d0564a" fill-opacity="0.35" stroke="currentColor"/><text x="642" y="65" font-size="12" text-anchor="middle">sig</text><text x="52" y="98" font-size="12" text-anchor="middle">128 B</text><text x="604" y="98" font-size="12" text-anchor="middle">102 B</text><text x="642" y="114" font-size="12" text-anchor="middle">32 B</text><line x1="20" y1="122" x2="624" y2="122" stroke="#d0564a" stroke-width="2"/><text x="322" y="138" font-size="12" text-anchor="middle">서명 범위: 헤더 + payload + meta (sig 자신은 제외)</text><text x="20" y="166" font-size="12">헤더 128 B 확대</text>
<rect x="20" y="176" width="40" height="34" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="40" y="197" font-size="12" text-anchor="middle">magic</text><rect x="60" y="176" width="60" height="34" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="90" y="197" font-size="12" text-anchor="middle">fmt·len</text><rect x="120" y="176" width="80" height="34" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><text x="160" y="197" font-size="12" text-anchor="middle">id·semver</text><rect x="200" y="176" width="110" height="34" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/><text x="255" y="197" font-size="12" text-anchor="middle">abi·pre·hw·arena</text>
<rect x="310" y="176" width="70" height="34" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="345" y="197" font-size="12" text-anchor="middle">len·secver</text><rect x="380" y="176" width="50" height="34" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/><text x="405" y="197" font-size="12" text-anchor="middle">ops</text><rect x="430" y="176" width="100" height="34" fill="#3f9a6b" fill-opacity="0.3" stroke="currentColor"/><text x="480" y="197" font-size="12" text-anchor="middle">in · out spec</text>
<rect x="530" y="176" width="90" height="34" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="575" y="197" font-size="12" text-anchor="middle">SHA-256</text><rect x="620" y="176" width="40" height="34" fill="#888" fill-opacity="0.25" stroke="currentColor"/><text x="640" y="197" font-size="12" text-anchor="middle">CRC</text><text x="20" y="232" font-size="12">주황 = 호환성 검사 (설치 전에 거부할 근거) · 초록 = 전처리·후처리가 쓰는 텐서 spec</text><text x="20" y="252" font-size="12">빨강 = 무결성 (payload hash) · 회색 = 구조 (magic, 길이, CRC)</text><text x="20" y="276" font-size="12">헤더 CRC는 "깨졌나"를 싸게, 서명은 "누가 만들었나"를 비싸게 — 순서는 CRC → 길이 → 서명 → 의미 검사</text></svg>
```

그림 2 — MPK1 패키지 레이아웃. 위는 전체(비율은 근사), 아래는 헤더 128 B를 필드 묶음으로 확대한 것. 서명 범위는 sig 앞의 모든 바이트다.

### 2.3 코드로 확인 — 예제 2: Python 패키저

**예제 2** — 무엇을 확인하나: `struct`로 128 B 헤더를 만들고(CRC는 앞 124 B에 대해), payload·metadata를 붙이고, 전체에 서명을 붙인다. 정상 패키지 1개와 **거부되어야 할 변형 7개**(ABI 불일치, op 누락, arena 초과, 다른 HW, 롤백, 서명 위조, 잘림)를 파일로 만든다.

서명 주의: 이 환경에는 `cryptography` 패키지가 없어서 표준 라이브러리의 **HMAC-SHA256으로 서명을 흉내 낸다**. HMAC은 대칭 키라 기기에도 같은 비밀키가 있어야 하고, 기기 하나가 털리면 누구나 "서명"을 만들 수 있다. **실제 제품은 Ed25519나 ECDSA P-256 같은 비대칭 서명을 쓰고, 기기에는 공개키(또는 그 hash)만 둔다** — H8 §2.4와 같은 결정이다. 코드 구조(무엇을 서명하고 언제 검사하나)는 똑같다.

```python
# mpk.py — 모델 패키지(MPK1) 만들기: 128 B 헤더 + payload + metadata(JSON) + 32 B 서명
import struct, zlib, hashlib, hmac, json
KEY = b"demo-signing-key"          # 데모용 HMAC 키. 실제 제품은 Ed25519/ECDSA 비밀키(서버 HSM) + 기기엔 공개키만
OPS = ["CONV_2D", "DEPTHWISE_CONV_2D", "FULLY_CONNECTED", "SOFTMAX",
       "RESHAPE", "AVERAGE_POOL_2D", "ADD", "MUL", "GRU"]          # op 번호 = 비트 위치
DT = {"int8": 1, "int16": 2, "float32": 3}
TSPEC = struct.Struct("<BBxx4Hfi")                                 # dtype, ndim, dims[4], scale, zero_point = 20 B
HDR = struct.Struct("<4sHHIIHHIIIIIQ20s20s32sI")                    # CRC 앞까지 124 B
def tspec(dtype, dims, scale, zp):
    d = list(dims) + [0] * (4 - len(dims))
    return TSPEC.pack(DT[dtype], len(dims), *d, scale, zp)
def semver(s):
    a, b, c = map(int, s.split(".")); return (a << 16) | (b << 8) | c
def build(payload, meta, *, model_id=0x4B575331, ver="2.1.0", abi=2, preproc=3,
          hw=0x00020001, arena=48 * 1024, secver=4, ops=("CONV_2D", "DEPTHWISE_CONV_2D",
          "FULLY_CONNECTED", "SOFTMAX", "RESHAPE", "AVERAGE_POOL_2D"), key=KEY):
    m = json.dumps(meta, separators=(",", ":"), sort_keys=True).encode()
    mask = sum(1 << OPS.index(o) for o in ops)
    body = HDR.pack(b"MPK1", 1, 128, model_id, semver(ver), abi, preproc, hw, arena,
                    len(payload), len(m), secver, mask,
                    tspec("int8", (1, 49, 10, 1), 0.1176, -12),     # 입력: 49 frame × 10 MFCC
                    tspec("int8", (1, 12), 0.00390625, -128),       # 출력: 12 class 확률
                    hashlib.sha256(payload).digest(), 0)
    hdr = body + struct.pack("<I", zlib.crc32(body))
    signed = hdr + payload + m
    return signed + hmac.new(key, signed, hashlib.sha256).digest()
```

```python
# make_pkgs.py — 정상 패키지 1개 + 거부되어야 할 변형 7개를 만든다
import numpy as np, struct, os
from mpk import build, HDR
rng = np.random.default_rng(5)
payload = b"\x1c\x00\x00\x00TFL3" + rng.integers(-128, 128, 20_000, dtype=np.int8).tobytes()
meta = {"train_data": "kws-ds-2026.09.3", "git": "a1b2c3d", "val_frr": 0.041, "val_fa_per_h": 0.31,
        "threshold": 0.83}
os.makedirs("pkgs", exist_ok=True)
cases = {"good": build(payload, meta),
         "wrong_abi": build(payload, meta, abi=3),
         "missing_op": build(payload, meta, ops=("CONV_2D", "FULLY_CONNECTED", "GRU")),
         "arena_too_big": build(payload, meta, arena=96 * 1024),
         "wrong_hw": build(payload, meta, hw=0x00030001),
         "rollback": build(payload, meta, secver=2)}
bad = bytearray(cases["good"]); bad[128 + 5000] ^= 0x01; cases["bad_sig"] = bytes(bad)
cases["truncated"] = cases["good"][:-100]
for name, blob in cases.items():
    open(f"pkgs/{name}.mpk", "wb").write(blob)
f = HDR.unpack(cases["good"][:124])
print(f"header={HDR.size + 4} B  payload={f[9]} B  meta={f[10]} B  sig=32 B  total={len(cases['good'])} B")
print(f"model_id={f[3]:#010x} ver={f[4] >> 16}.{(f[4] >> 8) & 255}.{f[4] & 255} abi={f[5]} "
      f"preproc={f[6]} hw={f[7]:#010x} arena={f[8]} secver={f[11]} ops_mask={f[12]:#06x}")
print("first 48 header bytes:", cases["good"][:48].hex(" "))
print("files:", sorted(os.listdir("pkgs")))
```

```text
header=128 B  payload=20008 B  meta=102 B  sig=32 B  total=20270 B
model_id=0x4b575331 ver=2.1.0 abi=2 preproc=3 hw=0x00020001 arena=49152 secver=4 ops_mask=0x003f
first 48 header bytes: 4d 50 4b 31 01 00 80 00 31 53 57 4b 00 01 02 00 02 00 03 00 01 00 02 00 00 c0 00 00 28 4e 00 00 66 00 00 00 04 00 00 00 3f 00 00 00 00 00 00 00
files: ['arena_too_big.mpk', 'bad_sig.mpk', 'good.mpk', 'missing_op.mpk', 'rollback.mpk', 'truncated.mpk', 'wrong_abi.mpk', 'wrong_hw.mpk']
```

출력에서 볼 것:

- hex를 손으로 읽어 보자. `4d 50 4b 31` = "MPK1", `01 00` = fmt 1, `80 00` = 128(little-endian). `31 53 57 4b`는 model_id `0x4B575331`이 little-endian으로 놓인 것 — 이름 "KWS1"을 정수로 쓴 것이라 바이트 순서가 뒤집혀 보인다. `00 01 02 00`은 semver `0x00020100` = 2.1.0. `00 c0 00 00` = 0xC000 = 49,152 = 48 KB arena. `3f` = op 0~5 비트.
- payload 앞 8 B(`1c 00 00 00 54 46 4c 33`)는 실제 `.tflite`처럼 offset 4에 "TFL3" 식별자를 둔 흉내다(F7 예제 14에서 확인한 실제 파일 모양). 나머지는 난수다.
- metadata는 `sort_keys=True`, 공백 없는 JSON이다. 서명 대상이므로 **같은 내용이면 같은 바이트**가 나와야 한다(정규화). 이것을 빼먹으면 "같은 패키지를 다시 만들었더니 서명이 다르다"가 생긴다.

### 2.4 코드로 확인 — 예제 3: C SHA-256 / HMAC이 Python과 같은가

기기 쪽 검증기를 짜기 전에, C로 쓴 SHA-256과 HMAC이 Python `hashlib`·`hmac`과 비트 단위로 같은지부터 확인한다. 실제 MCU에서는 하드웨어 해시 가속기나 검증된 라이브러리(mbedTLS, TinyCrypt 등)를 쓴다 — 아래는 원리를 보이기 위한 작은 참조 구현이다(FIPS 180-4).

**예제 3** — 무엇을 확인하나: `sha256("abc")` 표준 테스트 벡터, payload 해시, 패키지 HMAC을 C와 Python에서 각각 계산해 비교한다.

```c
/* sha256.h — FIPS 180-4 SHA-256 + HMAC-SHA256, 작은 참조 구현 (실제 MCU는 HW 가속기·검증된 라이브러리 사용) */
#include <stdint.h>
#include <string.h>
static const uint32_t K256[64] = {
 0x428a2f98,0x71374491,0xb5c0fbcf,0xe9b5dba5,0x3956c25b,0x59f111f1,0x923f82a4,0xab1c5ed5,
 0xd807aa98,0x12835b01,0x243185be,0x550c7dc3,0x72be5d74,0x80deb1fe,0x9bdc06a7,0xc19bf174,
 0xe49b69c1,0xefbe4786,0x0fc19dc6,0x240ca1cc,0x2de92c6f,0x4a7484aa,0x5cb0a9dc,0x76f988da,
 0x983e5152,0xa831c66d,0xb00327c8,0xbf597fc7,0xc6e00bf3,0xd5a79147,0x06ca6351,0x14292967,
 0x27b70a85,0x2e1b2138,0x4d2c6dfc,0x53380d13,0x650a7354,0x766a0abb,0x81c2c92e,0x92722c85,
 0xa2bfe8a1,0xa81a664b,0xc24b8b70,0xc76c51a3,0xd192e819,0xd6990624,0xf40e3585,0x106aa070,
 0x19a4c116,0x1e376c08,0x2748774c,0x34b0bcb5,0x391c0cb3,0x4ed8aa4a,0x5b9cca4f,0x682e6ff3,
 0x748f82ee,0x78a5636f,0x84c87814,0x8cc70208,0x90befffa,0xa4506ceb,0xbef9a3f7,0xc67178f2};
typedef struct { uint32_t h[8]; uint64_t n; uint8_t buf[64]; size_t used; } sha256_t;
#define ROR(x, r) (((x) >> (r)) | ((x) << (32 - (r))))
static void sha256_block(sha256_t *s, const uint8_t *p) {
    uint32_t w[64], a[8];
    for (int i = 0; i < 16; i++) w[i] = (uint32_t)p[4*i] << 24 | (uint32_t)p[4*i+1] << 16 | (uint32_t)p[4*i+2] << 8 | p[4*i+3];
    for (int i = 16; i < 64; i++)
        w[i] = w[i-16] + (ROR(w[i-15], 7) ^ ROR(w[i-15], 18) ^ (w[i-15] >> 3)) + w[i-7]
             + (ROR(w[i-2], 17) ^ ROR(w[i-2], 19) ^ (w[i-2] >> 10));
    memcpy(a, s->h, sizeof a);
    for (int i = 0; i < 64; i++) {
        uint32_t t1 = a[7] + (ROR(a[4], 6) ^ ROR(a[4], 11) ^ ROR(a[4], 25)) + ((a[4] & a[5]) ^ (~a[4] & a[6])) + K256[i] + w[i];
        uint32_t t2 = (ROR(a[0], 2) ^ ROR(a[0], 13) ^ ROR(a[0], 22)) + ((a[0] & a[1]) ^ (a[0] & a[2]) ^ (a[1] & a[2]));
        memmove(a + 1, a, 7 * sizeof a[0]); a[4] += t1; a[0] = t1 + t2;
    }
    for (int i = 0; i < 8; i++) s->h[i] += a[i];
}
static void sha256_init(sha256_t *s) {
    static const uint32_t iv[8] = {0x6a09e667,0xbb67ae85,0x3c6ef372,0xa54ff53a,0x510e527f,0x9b05688c,0x1f83d9ab,0x5be0cd19};
    memcpy(s->h, iv, sizeof iv); s->n = 0; s->used = 0;
}
static void sha256_update(sha256_t *s, const uint8_t *p, size_t len) {
    s->n += len;
    while (len--) { s->buf[s->used++] = *p++; if (s->used == 64) { sha256_block(s, s->buf); s->used = 0; } }
}
static void sha256_final(sha256_t *s, uint8_t out[32]) {
    uint64_t bits = s->n * 8; uint8_t pad = 0x80, z = 0;
    sha256_update(s, &pad, 1);
    while (s->used != 56) sha256_update(s, &z, 1);
    for (int i = 7; i >= 0; i--) { uint8_t b = (uint8_t)(bits >> (8 * i)); sha256_update(s, &b, 1); }
    for (int i = 0; i < 32; i++) out[i] = (uint8_t)(s->h[i / 4] >> (24 - 8 * (i % 4)));
}
static void hmac_sha256(const uint8_t *key, size_t klen, const uint8_t *msg, size_t mlen, uint8_t out[32]) {
    uint8_t k[64] = {0}, ip[64], op[64], inner[32]; sha256_t s;   /* key ≤ 64 B 가정 */
    memcpy(k, key, klen);
    for (int i = 0; i < 64; i++) { ip[i] = k[i] ^ 0x36; op[i] = k[i] ^ 0x5c; }
    sha256_init(&s); sha256_update(&s, ip, 64); sha256_update(&s, msg, mlen); sha256_final(&s, inner);
    sha256_init(&s); sha256_update(&s, op, 64); sha256_update(&s, inner, 32); sha256_final(&s, out);
}
```

```c
/* sha_test.c — C 결과를 Python과 비교하기 위해 출력 */
#include <stdio.h>
#include "sha256.h"
static void hex(const char *tag, const uint8_t d[32]) {
    printf("%-12s", tag); for (int i = 0; i < 32; i++) printf("%02x", d[i]); printf("\n");
}
int main(void) {
    uint8_t d[32]; sha256_t s; const char *m = "abc";
    sha256_init(&s); sha256_update(&s, (const uint8_t *)m, 3); sha256_final(&s, d); hex("sha256(abc)", d);
    FILE *f = fopen("pkgs/good.mpk", "rb"); static uint8_t b[1 << 16];
    size_t n = fread(b, 1, sizeof b, f); fclose(f);
    sha256_init(&s); sha256_update(&s, b + 128, n - 128 - 102 - 32); sha256_final(&s, d); hex("payload", d);
    hmac_sha256((const uint8_t *)"demo-signing-key", 16, b, n - 32, d); hex("hmac(pkg)", d);
    printf("%-12s", "sig in file"); for (int i = 0; i < 32; i++) printf("%02x", b[n - 32 + i]); printf("\n");
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 sha_test.c -o sha_test && ./sha_test
.venv/bin/python -c "
import hashlib; b=open('pkgs/good.mpk','rb').read()
print('py sha(abc) ', hashlib.sha256(b'abc').hexdigest()); print('py payload  ', hashlib.sha256(b[128:128+20008]).hexdigest()); print('py hdr field', b[88:120].hex())"
```

```text
sha256(abc) ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
payload     176d0249ec45224b96c7f93511648bf2b000f14ab6ca66a7b5b40154273c2058
hmac(pkg)   cd038fc987b2c773d7e30bbbd94f137b4dbae83ec4ec946fc99ccd56d741351e
sig in file cd038fc987b2c773d7e30bbbd94f137b4dbae83ec4ec946fc99ccd56d741351e
py sha(abc)  ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad
py payload   176d0249ec45224b96c7f93511648bf2b000f14ab6ca66a7b5b40154273c2058
py hdr field 176d0249ec45224b96c7f93511648bf2b000f14ab6ca66a7b5b40154273c2058
```

출력에서 볼 것:

- `sha256("abc")`가 FIPS 180-4의 표준 예제 값 `ba7816bf…15ad`와 같다. 해시 구현을 직접 쓰면 **표준 테스트 벡터부터** 맞춘다 — golden vector(C8)와 같은 습관이다.
- C가 계산한 payload 해시 = Python 해시 = 헤더 88~119 바이트에 Python이 넣은 값. C가 계산한 HMAC = 파일 끝 32 B. 호스트 도구와 펌웨어가 **같은 바이트 정의**를 공유한다는 것이 확인됐다.
- 이 참조 구현은 바이트마다 함수 호출을 하므로 느리다. 수백 KB 모델이면 MCU에서 수십 ms가 걸릴 수 있다 — 부팅·활성화 시간 예산에 넣거나 HW 가속기를 쓴다.

### 2.5 코드로 확인 — 예제 4: C 검증기와 거부 사례 7개

검사 순서가 설계다. 원칙은 **"최소한만 파싱 → 서명 확인 → 그다음에야 내용을 믿는다"**이다.

1. 크기 ≥ 헤더, magic, 헤더 CRC, fmt_version — 싸고, 실패하면 더 볼 필요가 없다.
2. 길이 일관성(`128 + payload + meta + 32 == 파일 크기`) — 서명 위치를 알려면 길이가 필요하다. 이 덧셈은 64비트로 해서 **overflow로 검사를 우회**하지 못하게 한다(공격자가 `payload_len = 0xFFFFFF00` 같은 값을 넣는 경우).
3. 서명 — 여기를 통과하기 전의 필드는 "공격자가 쓴 값일 수도 있다"로 취급한다.
4. 의미 검사 — HW, ABI, op, arena, 보안 버전, payload hash.

**예제 4** — 무엇을 확인하나: 펌웨어 빌드가 아는 능력(ABI 1~2, op 7개, arena 64 KB, HW ID, 보안 카운터 3)을 상수로 두고, 예제 2의 8개 파일을 검증한다.

```c
/* mpk_check.c — 기기 쪽 MPK1 검증기: "최소 파싱 → 서명 → 그다음에야 내용 신뢰" */
#include <stdio.h>
#include "sha256.h"
typedef struct __attribute__((packed)) {
    uint8_t dtype, ndim, pad[2]; uint16_t dims[4]; float scale; int32_t zero_point;
} tspec_t;
typedef struct __attribute__((packed)) {
    char magic[4]; uint16_t fmt_version, header_len;
    uint32_t model_id, semver; uint16_t runtime_abi, preproc_ver;
    uint32_t hw_id, arena_bytes, payload_len, meta_len, security_ver;
    uint64_t ops_mask; tspec_t in, out; uint8_t payload_sha256[32];
    uint32_t flags, hdr_crc32;
} mpk_hdr_t;
_Static_assert(sizeof(mpk_hdr_t) == 128, "MPK1 header is 128 bytes");
/* 이 펌웨어 빌드가 아는 것 — 빌드 때 런타임 op resolver에서 생성된다고 가정 */
enum { ABI_MIN = 1, ABI_MAX = 2, ARENA_AVAIL = 64 * 1024 };
static const uint64_t OPS_SUPPORTED = 0x7F;           /* CONV..ADD (bit0~6). MUL·GRU 없음 */
static const uint32_t HW_ID = 0x00020001;
static uint32_t sec_counter = 3;                       /* anti-rollback: OTP/보안 저장소의 값 */
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}
static const char *check(const uint8_t *b, size_t n) {
    mpk_hdr_t h; uint8_t d[32]; sha256_t s;
    if (n < sizeof h) return "REJECT: shorter than header";
    memcpy(&h, b, sizeof h);                                   /* 정렬 안 된 flash 버퍼 → 복사 */
    if (memcmp(h.magic, "MPK1", 4)) return "REJECT: bad magic";
    if (crc32(b, 124) != h.hdr_crc32) return "REJECT: header CRC";
    if (h.fmt_version != 1 || h.header_len != 128) return "REJECT: unknown package format";
    uint64_t total = 128ull + h.payload_len + h.meta_len + 32;  /* 64비트로 — 덧셈 overflow 방지 */
    if (total != n) return "REJECT: length mismatch (truncated?)";
    hmac_sha256((const uint8_t *)"demo-signing-key", 16, b, n - 32, d);
    if (memcmp(d, b + n - 32, 32)) return "REJECT: bad signature";  /* 실제는 상수 시간 비교 */
    /* ---- 여기부터는 서명된 내용이므로 의미 검사 ---- */
    if (h.hw_id != HW_ID) return "REJECT: wrong hardware";
    if (h.runtime_abi < ABI_MIN || h.runtime_abi > ABI_MAX) return "REJECT: runtime ABI unsupported";
    if (h.ops_mask & ~OPS_SUPPORTED) return "REJECT: model needs an op this firmware lacks";
    if (h.arena_bytes > ARENA_AVAIL) return "REJECT: arena too big";
    if (h.security_ver < sec_counter) return "REJECT: rollback (security version too old)";
    sha256_init(&s); sha256_update(&s, b + 128, h.payload_len); sha256_final(&s, d);
    if (memcmp(d, h.payload_sha256, 32)) return "REJECT: payload hash";
    static char ok[96];
    snprintf(ok, sizeof ok, "ACCEPT v%u.%u.%u in[%u,%u,%u,%u] scale=%.4f zp=%d",
             (unsigned)(h.semver >> 16), (unsigned)(h.semver >> 8 & 255), (unsigned)(h.semver & 255),
             h.in.dims[0], h.in.dims[1], h.in.dims[2], h.in.dims[3], h.in.scale, (int)h.in.zero_point);
    return ok;
}
int main(int argc, char **argv) {
    static uint8_t buf[1 << 16];
    for (int i = 1; i < argc; i++) {
        FILE *f = fopen(argv[i], "rb"); if (!f) continue;
        size_t n = fread(buf, 1, sizeof buf, f); fclose(f);
        printf("%-22s %s\n", argv[i] + 5, check(buf, n));
    }
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 mpk_check.c -o mpk_check && ./mpk_check pkgs/good.mpk pkgs/wrong_abi.mpk \
  pkgs/missing_op.mpk pkgs/arena_too_big.mpk pkgs/wrong_hw.mpk pkgs/rollback.mpk pkgs/bad_sig.mpk pkgs/truncated.mpk
```

```text
good.mpk               ACCEPT v2.1.0 in[1,49,10,1] scale=0.1176 zp=-12
wrong_abi.mpk          REJECT: runtime ABI unsupported
missing_op.mpk         REJECT: model needs an op this firmware lacks
arena_too_big.mpk      REJECT: arena too big
wrong_hw.mpk           REJECT: wrong hardware
rollback.mpk           REJECT: rollback (security version too old)
bad_sig.mpk            REJECT: bad signature
truncated.mpk          REJECT: length mismatch (truncated?)
```

출력에서 볼 것:

- 8개 중 정상 1개만 통과하고, 나머지 7개는 **각자 다른 이유**로 거부된다. 거부 사유를 구분해서 텔레메트리로 올리는 것이 중요하다 — "ABI 거부가 1.4 펌웨어 기기에서 1,200건"은 서버의 호환성 판정(§3)이 틀렸다는 신호다. 기기는 원래 이런 패키지를 받으면 안 된다.
- `bad_sig`는 payload의 비트 하나만 뒤집었다. payload hash도 틀렸지만 **서명 단계에서 먼저** 걸린다. 순서상 서명 이후의 의미 검사는 신뢰할 수 있는 값으로만 한다.
- `missing_op`은 GRU(비트 8)를 요구한다. `0x105 & ~0x7F = 0x100`이 0이 아니므로 거부. 실제 TFLM에서는 이 검사가 없으면 `AllocateTensors()`(또는 op resolver 조회)에서야 실패하고, 그 시점은 이미 옛 모델을 내린 뒤일 수 있다 — 설치 **전** 거부가 핵심이다.
- packed struct를 `memcpy`로 지역 변수에 복사했다. flash 버퍼 포인터를 그대로 struct 포인터로 캐스트하면 정렬 안 된 접근(Cortex-M0+는 fault)과 strict aliasing 문제가 생길 수 있다.

### 2.6 검증 시점 — TOCTOU

TOCTOU(time-of-check to time-of-use)는 "검사한 것과 쓰는 것이 다를 수 있다"는 보안 함정이다. 모델을 외부 QSPI flash에서 XIP로 읽는다면, 설치 때 서명을 검사한 뒤 누군가(물리 접근, 다른 버그)가 flash 내용을 바꿔도 모른다. 대책은 셋 중 하나다.

- **부팅·로드할 때마다 hash 검증**: payload SHA-256을 다시 계산해 서명된 헤더 값과 비교. 비용은 모델 크기에 비례(예제 3 참조) — 부팅 예산에 넣는다.
- **RAM으로 복사한 뒤 검증하고, 검증한 복사본만 쓴다**: AP처럼 RAM이 넉넉한 곳의 기본.
- **flash를 쓰기 금지 영역으로**: MPU·flash write protection으로 앱이 슬롯을 못 쓰게 하고, 업데이트 에이전트만 쓰게 한다.

### 2.7 함정

- **헤더 버전과 모델 버전 혼동**: `fmt_version`은 헤더 레이아웃의 버전, `semver`는 모델 기능 버전, `runtime_abi`는 런타임 계약 버전, `security_ver`는 롤백 방지 버전이다. 하나로 합치면 "헤더 필드 추가"가 "보안 롤백"으로 오인되거나 그 반대가 생긴다.
- **endianness와 float 필드**: 헤더는 little-endian으로 고정하고 문서화한다. float scale을 넣을 때 NaN·Inf 검사도 한다(서명이 있어도 학습 파이프라인 버그로 들어올 수 있다).
- **metadata를 파싱하는 JSON 파서**: MCU에 JSON 파서를 넣으면 공격 면이 커진다. 기기가 쓰는 값(threshold 등)은 고정 struct나 CBOR로, 사람용 정보만 JSON으로 두는 편이 안전하다(H1 §3).

---

## 3. 호환성 매트릭스와 capability 협상

### 3.1 세 개의 축

예제 4의 거부는 **최후의 방어선**이다. 정상 운영에서는 기기가 호환되지 않는 패키지를 **받지 않아야** 한다 — 다운로드 바이트·배터리·flash 쓰기 횟수가 아깝고, 거부가 반복되면 기기는 매 check-in마다 같은 실패를 되풀이한다. 그래서 서버가 먼저 고른다. 무엇을 기준으로? 세 축이다.

| 축 | 기기 쪽 값 | 모델 쪽 값 | 호환 조건 |
|---|---|---|---|
| 런타임 계약 | 지원 ABI 집합 {1, 2} | 요구 ABI 2 | 요구 ∈ 지원 |
| 연산 | 등록된 op 비트마스크 | 요구 op 비트마스크 | 요구 ⊆ 지원 |
| 자원 | 정적 arena, 슬롯 크기 | 필요 arena, 패키지 크기 | 요구 ≤ 가용 |
| 신호 처리 | 지원 전처리 버전 집합 | 학습 때 전처리 버전 | 요구 ∈ 지원 |
| 하드웨어 | HW ID (보드·마이크 rev) | 대상 HW ID 목록 | 포함 |
| 보안 | 보안 카운터 | security_ver | ≥ |

말로 하면: 기기는 "내가 할 수 있는 것"을 집합과 숫자로 말하고, 모델은 "내가 필요한 것"을 같은 단위로 말하고, 서버는 **부분집합·이하·포함** 검사만 하면 된다.

중요한 설계 결정 하나: 기기가 check-in 때 **펌웨어 버전 문자열만** 보내고 서버가 "1.5면 ABI 2, op 7개…"를 표로 갖고 있게 할 수도 있고, 기기가 **능력 자체**를 보내게 할 수도 있다. 후자를 권한다. 펌웨어 빌드 변형(디버그 빌드, 지역별 빌드, 핫픽스)이 늘어도 서버 표가 틀어지지 않는다. H8 §2.5의 capability bitmask와 같은 생각이다.

### 3.2 코드로 확인 — 예제 5: 협상과 매트릭스

**예제 5** — 무엇을 확인하나: 펌웨어 3종(1.4·1.5·1.6)과 모델 빌드 4종(1.9.0~3.0.0)의 호환 여부와 거부 사유를 매트릭스로 만들고, 각 펌웨어에 서버가 보낼 "가장 새 호환 모델"을 고른다.

```python
# capability 협상: 기기가 check-in 때 능력을 말하고, 서버는 호환되는 가장 새 모델을 고른다
FW = {"1.4": dict(abi={1}, ops=0b0001111, arena=48, preproc={2}),
      "1.5": dict(abi={1, 2}, ops=0b1111111, arena=64, preproc={2, 3}),
      "1.6": dict(abi={1, 2}, ops=0b111111111, arena=96, preproc={3, 4})}   # bit7 MUL, bit8 GRU
MODELS = [("1.9.0", dict(abi=1, ops=0b0001111, arena=40, preproc=2)),
          ("2.0.0", dict(abi=2, ops=0b0001111, arena=48, preproc=3)),
          ("2.1.0", dict(abi=2, ops=0b1001111, arena=56, preproc=3)),      # ADD(bit6) 추가
          ("3.0.0", dict(abi=2, ops=0b100001111, arena=80, preproc=4))]    # GRU, mel 64
def why_not(cap, m):
    if m["abi"] not in cap["abi"]: return "abi"
    if m["ops"] & ~cap["ops"]: return "op"
    if m["arena"] > cap["arena"]: return "arena"
    if m["preproc"] not in cap["preproc"]: return "pre"
    return ""
print("FW \\ model " + "".join(f"{v:>8s}" for v, _ in MODELS) + "   -> served")
for fw, cap in FW.items():
    cells = [why_not(cap, m) or "ok" for _, m in MODELS]
    best = [v for (v, _), c in zip(MODELS, cells) if c == "ok"]
    print(f"FW {fw:8s}" + "".join(f"{c:>8s}" for c in cells) + f"   -> {best[-1] if best else 'none'}")
fleet = {"1.4": 1200, "1.5": 15800, "1.6": 3000}                        # check-in 집계 (가정)
pairs = sum(1 for fw in FW for _, m in MODELS if not why_not(FW[fw], m))
print(f"compatible pairs={pairs} of {len(FW) * len(MODELS)}; pairs actually served={len(FW)}; "
      f"devices stuck below 2.x without FW update={fleet['1.4']}")
```

```text
FW \ model    1.9.0   2.0.0   2.1.0   3.0.0   -> served
FW 1.4           ok     abi     abi     abi   -> 1.9.0
FW 1.5           ok      ok      ok      op   -> 2.1.0
FW 1.6          pre      ok      ok      ok   -> 3.0.0
compatible pairs=7 of 12; pairs actually served=3; devices stuck below 2.x without FW update=1200
```

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg"><text x="20" y="22" font-size="13">펌웨어 × 모델 호환 매트릭스 (예제 5 실측) — 굵은 테두리 = 서버가 실제로 보내는 조합</text><text x="215" y="58" font-size="12" text-anchor="middle">1.9.0</text><text x="325" y="58" font-size="12" text-anchor="middle">2.0.0</text><text x="435" y="58" font-size="12" text-anchor="middle">2.1.0</text><text x="545" y="58" font-size="12" text-anchor="middle">3.0.0</text><text x="120" y="98" font-size="12" text-anchor="end">FW 1.4 (1,200대)</text><text x="120" y="158" font-size="12" text-anchor="end">FW 1.5 (15,800대)</text><text x="120" y="218" font-size="12" text-anchor="end">FW 1.6 (3,000대)</text>
<rect x="165" y="70" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="3"/><text x="215" y="100" font-size="12" text-anchor="middle">ok</text><rect x="275" y="70" width="100" height="50" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="325" y="100" font-size="12" text-anchor="middle">ABI</text><rect x="385" y="70" width="100" height="50" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="435" y="100" font-size="12" text-anchor="middle">ABI</text><rect x="495" y="70" width="100" height="50" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="545" y="100" font-size="12" text-anchor="middle">ABI</text>
<rect x="165" y="130" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="215" y="160" font-size="12" text-anchor="middle">ok</text><rect x="275" y="130" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="325" y="160" font-size="12" text-anchor="middle">ok (롤백 대상)</text><rect x="385" y="130" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="3"/><text x="435" y="160" font-size="12" text-anchor="middle">ok</text>
<rect x="495" y="130" width="100" height="50" fill="#d0564a" fill-opacity="0.25" stroke="currentColor"/><text x="545" y="160" font-size="12" text-anchor="middle">op (GRU)</text><rect x="165" y="190" width="100" height="50" fill="#e08a3c" fill-opacity="0.3" stroke="currentColor"/><text x="215" y="220" font-size="12" text-anchor="middle">전처리 v2 없음</text><rect x="275" y="190" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="325" y="220" font-size="12" text-anchor="middle">ok</text>
<rect x="385" y="190" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="435" y="220" font-size="12" text-anchor="middle">ok (롤백 대상)</text><rect x="495" y="190" width="100" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor" stroke-width="3"/><text x="545" y="220" font-size="12" text-anchor="middle">ok</text><text x="20" y="264" font-size="12">테스트해야 할 조합 = 실제로 보내는 3칸 + 롤백 대상(각 FW의 직전 모델) + FW 롤백 시 남는 조합</text></svg>
```

그림 3 — 펌웨어 × 모델 호환 매트릭스. 초록은 호환, 빨강은 런타임 거부 사유, 주황은 FW 1.6이 전처리 v2를 지원하지 않아 생긴 "뒤로 호환 끊김"이다. 굵은 테두리가 서버가 각 펌웨어에 보내는 모델이다.

출력에서 볼 것:

- **FW 1.4 기기 1,200대는 모델 2.x를 영원히 못 받는다** — 펌웨어 업데이트를 먼저 받아야 한다. 이 숫자가 "모델 개선이 fleet에 닿는 비율"의 상한이다. 대시보드에 "모델 버전 분포"와 함께 "**호환 가능한 최신 모델을 못 받는 기기 수**"를 띄운다.
- FW 1.6은 전처리 v2를 버렸다(`pre`). 펌웨어가 옛 전처리를 지우면 그 펌웨어에서는 옛 모델로 **롤백할 수 없다**. 롤백 경로를 지키려면 펌웨어 N은 모델 N−1이 요구하는 것을 최소 한 세대는 유지해야 한다.
- 호환 조합은 7개지만 실제로 보내는 조합은 3개다. 테스트는 7개 전부가 아니라 "보내는 조합 + 롤백 시 생기는 조합"에 집중한다.

### 3.3 check-in 메시지 — 무엇을 주고받나

H8 §2.5의 config check-in에 모델 정보를 얹는다. 형식은 예시다.

```text
기기 → 서버 (check-in)
  device_id      : dev01234            (cohort hash용, H8 §3)
  hw_id          : 0x00020001          (보드·마이크 rev)
  fw             : 1.5.2 (build a9f3)  (사람용)
  caps.abi       : [1, 2]
  caps.ops_mask  : 0x7F
  caps.arena     : 65536
  caps.preproc   : [2, 3]
  caps.slot      : 262144
  sec_counter    : 3
  models.kws     : active 2.0.0 (slot A), pending none, last_reject "none"
  health.kws     : selftest_pass 1, invoke_err 0, fa_per_h_24h 0.41

서버 → 기기 (응답)
  kws: bundle 2.1.0, url, size 20270, sha256 176d…2058, activate "next_idle"
  (호환되는 것이 없거나 cohort 밖이면: "no update")
```

말로 하면: 기기가 "할 수 있는 것 + 지금 무엇을 돌리는지 + 건강 상태"를 말하고, 서버는 cohort와 호환성을 따져 **하나의 번들**을 고르거나 "없음"이라고 답한다. `health` 필드가 §6 rollout 판정의 원재료다.

### 3.4 순서 규칙 — 펌웨어가 먼저, 모델이 나중

E8 §6.3의 "새 AP가 옛 MCU와도 대화할 수 있게"를 모델에 적용하면 이렇다.

1. 펌웨어 N+1은 모델 N(현재)과 N+1(다음)을 **모두** 돌릴 수 있어야 한다.
2. 펌웨어를 먼저 배포하고, fleet 대부분이 N+1 펌웨어가 된 뒤에 모델 N+1을 배포한다.
3. 모델 N+1은 펌웨어 N+1 이상에만 보낸다(서버 협상).

이 순서를 지키면 어느 시점에도 "펌웨어와 모델이 안 맞는" 기기가 없다. 반대로 둘을 **동시에** 한 OTA로 묶을 수도 있다(§7 번들) — 그때는 둘이 함께 활성화되고 함께 롤백되어야 한다.

### 3.5 함정

- **서버가 펌웨어 문자열로 능력을 추정**: 핫픽스 빌드 1.5.2-hf1이 op 하나를 뺐는데 서버 표는 "1.5.x = 7 op"라면, 서버가 보낸 모델을 기기가 거부한다. 능력을 기기가 직접 보고하게 한다.
- **"최신만 테스트"**: 롤백 조합(펌웨어 N+1 + 모델 N)을 테스트하지 않으면, 롤백 버튼을 누르는 순간 처음 보는 조합이 fleet에 깔린다.
- **호환성 판정이 서버에만 있음**: 서버 버그 한 번이면 fleet 전체가 안 맞는 모델을 받는다. 기기 쪽 검증(예제 4)은 반드시 유지한다 — defense in depth(다층 방어).

---

## 4. 저장과 활성화 — A/B 슬롯, 상태 기계, 전원 차단

### 4.1 flash 지도 — 모델 슬롯을 따로 둘까

I1 §4에서 본 것처럼, 모델 가중치가 펌웨어 이미지 **안에** 있으면 펌웨어 A/B 슬롯마다 모델이 한 벌씩 들어간다. 모델만 바꾸려 해도 펌웨어 이미지 전체(수백 KB~MB)를 보내야 한다. 모델 업데이트가 잦다면 **모델 전용 A/B 파티션**을 두는 편이 낫다. 아래는 2 MB 내부 flash MCU의 예시 지도다(숫자는 설명용 가정).

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg"><text x="20" y="22" font-size="13">2 MB flash 지도 (가정) — 펌웨어 A/B와 모델 A/B를 분리</text><rect x="20" y="50" width="20.0" height="50" fill="#888" fill-opacity="0.35" stroke="currentColor"/><rect x="40.0" y="50" width="200.0" height="50" fill="#4a7bd0" fill-opacity="0.25" stroke="currentColor"/><rect x="240.0" y="50" width="200.0" height="50" fill="#4a7bd0" fill-opacity="0.12" stroke="currentColor"/><rect x="440.0" y="50" width="80.0" height="50" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="520.0" y="50" width="80.0" height="50" fill="#3f9a6b" fill-opacity="0.18" stroke="currentColor"/>
<rect x="600.0" y="50" width="2.5" height="50" fill="#d0564a" stroke="currentColor"/><rect x="602.5" y="50" width="57.5" height="50" fill="#888" fill-opacity="0.12" stroke="currentColor"/><text x="30" y="118" font-size="12" text-anchor="middle">BL</text><text x="140" y="80" font-size="12" text-anchor="middle">FW 슬롯 A 640 KB</text><text x="340" y="80" font-size="12" text-anchor="middle">FW 슬롯 B 640 KB</text><text x="480" y="72" font-size="12" text-anchor="middle">모델 A</text><text x="480" y="90" font-size="12" text-anchor="middle">256 KB</text><text x="560" y="72" font-size="12" text-anchor="middle">모델 B</text><text x="560" y="90" font-size="12" text-anchor="middle">256 KB</text>
<text x="631" y="80" font-size="12" text-anchor="middle">로그</text><text x="601" y="118" font-size="12" text-anchor="middle">메타 2×4 KB</text><line x1="601" y1="104" x2="601" y2="106" stroke="#d0564a"/><text x="20" y="150" font-size="12">BL 64 KB · FW 2×640 KB · 모델 2×256 KB · 메타 2×4 KB(ping-pong) · 로그·설정 184 KB = 2,048 KB</text><text x="20" y="174" font-size="12">모델만 바꿀 때: 모델 B 슬롯 256 KB만 지우고 쓴다 — FW 슬롯은 건드리지 않는다</text><text x="20" y="198" font-size="12">메타 = "어느 모델 슬롯이 active, 무엇이 pending, 남은 시도 횟수" — 두 사본, seq + CRC (예제 6)</text><text x="20" y="222" font-size="12">대가: 슬롯 크기가 모델 상한이다 (예제 1의 "bigger model 300 KB" → 파티션 표 변경 = FW 영역의 일)</text></svg>
```

그림 4 — 모델 전용 A/B 파티션을 둔 2 MB flash 지도(가정, 폭은 크기에 비례). 모델 업데이트는 모델 B 슬롯과 메타 섹터만 건드린다.

Don 연결: SSD에서 FW slot 여러 개(NVMe는 최대 7개)와 "다음 reset에 어느 slot으로 부팅할지"를 기록하는 영역을 따로 둔 것과 같은 구조다.

### 4.2 상태 기계 — 쓰기, 검증, pending, 시험, 확정

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg"><defs><marker id="j5s" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="22" font-size="13">모델 업데이트 상태 기계 — ◆ = 영속 메타데이터 쓰기 (전원이 끊겨도 남는 결정)</text><rect x="20" y="44" width="120" height="50" rx="8" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="80" y="65" font-size="12" text-anchor="middle">ACTIVE = A</text><text x="80" y="83" font-size="12" text-anchor="middle">평소 상태</text><line x1="140" y1="69" x2="178" y2="69" stroke="currentColor" marker-end="url(#j5s)"/>
<rect x="180" y="44" width="120" height="50" rx="8" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="240" y="65" font-size="12" text-anchor="middle">① ◆ B 무효화</text><text x="240" y="83" font-size="12" text-anchor="middle">len[B] = 0</text><line x1="300" y1="69" x2="338" y2="69" stroke="currentColor" marker-end="url(#j5s)"/><rect x="340" y="44" width="120" height="50" rx="8" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="400" y="65" font-size="12" text-anchor="middle">② B 지우고 쓰기</text><text x="400" y="83" font-size="12" text-anchor="middle">재개 가능</text><line x1="460" y1="69" x2="498" y2="69" stroke="currentColor" marker-end="url(#j5s)"/>
<rect x="500" y="44" width="160" height="50" rx="8" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="580" y="65" font-size="12" text-anchor="middle">③ readback 검증</text><text x="580" y="83" font-size="12" text-anchor="middle">hash · 서명 · 호환</text><line x1="580" y1="94" x2="580" y2="132" stroke="currentColor" marker-end="url(#j5s)"/><rect x="500" y="134" width="160" height="50" rx="8" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="580" y="155" font-size="12" text-anchor="middle">④ ◆ PENDING = B</text><text x="580" y="173" font-size="12" text-anchor="middle">tries = 2</text>
<line x1="500" y1="159" x2="462" y2="159" stroke="currentColor" marker-end="url(#j5s)"/><text x="481" y="150" font-size="12" text-anchor="middle">안전</text><text x="481" y="196" font-size="12" text-anchor="middle">지점</text><rect x="340" y="134" width="120" height="50" rx="8" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="400" y="155" font-size="12" text-anchor="middle">⑤ ◆ tries − 1</text><text x="400" y="173" font-size="12" text-anchor="middle">B로 시험 활성화</text><line x1="340" y1="159" x2="302" y2="159" stroke="currentColor" marker-end="url(#j5s)"/>
<rect x="180" y="134" width="120" height="50" rx="8" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="240" y="155" font-size="12" text-anchor="middle">⑥ self-test</text><text x="240" y="173" font-size="12" text-anchor="middle">golden vector</text><line x1="180" y1="159" x2="142" y2="159" stroke="currentColor" marker-end="url(#j5s)"/><text x="161" y="150" font-size="12" text-anchor="middle">통과</text><rect x="20" y="134" width="120" height="50" rx="8" fill="#3f9a6b" fill-opacity="0.3" stroke="currentColor"/><text x="80" y="155" font-size="12" text-anchor="middle">⑦ ◆ 확정</text><text x="80" y="173" font-size="12" text-anchor="middle">ACTIVE = B</text>
<line x1="240" y1="184" x2="240" y2="236" stroke="#d0564a" marker-end="url(#j5s)"/><text x="248" y="214" font-size="12">실패</text><line x1="400" y1="184" x2="400" y2="236" stroke="#d0564a" stroke-dasharray="5 3" marker-end="url(#j5s)"/><text x="408" y="214" font-size="12">크래시 · watchdog → 재부팅, tries 남으면 ⑤</text><rect x="180" y="238" width="280" height="44" rx="8" fill="#d0564a" fill-opacity="0.2" stroke="currentColor"/><text x="320" y="257" font-size="12" text-anchor="middle">⑧ ◆ 롤백: PENDING 해제, rejected = 버전</text><text x="320" y="274" font-size="12" text-anchor="middle">tries = 0 이어도 같은 길</text>
<polyline points="180,260 80,260 80,96" fill="none" stroke="#d0564a" marker-end="url(#j5s)"/><text x="88" y="230" font-size="12">A 유지</text><text x="20" y="314" font-size="12">어느 화살표에서 전원이 끊겨도, 다음 부팅은 메타의 마지막 유효 사본이 가리키는 "검증된 슬롯"으로 간다.</text></svg>
```

그림 5 — 모델 업데이트 상태 기계. ◆는 메타데이터를 flash에 쓰는 지점(전원이 끊겨도 남는 결정)이다. 핵심 순서는 "B 무효화 → 쓰기 → 검증 → pending 표시 → (안전 지점) → 시도 횟수를 먼저 줄이고 시험 → self-test → 확정 또는 롤백"이다.

단계마다 "왜 이 순서인가"를 짚자.

1. **① B를 먼저 무효로**: B에 옛 업데이트의 흔적(이전 버전)이 남아 있을 수 있다. 쓰기 도중 끊겼을 때 메타가 여전히 "B = 옛 버전, 유효"라고 말하면, 반쯤 쓴 B를 믿게 된다. 먼저 메타에서 B를 지운다.
2. **② 쓰기는 재개 가능하게**: 다운로드는 H2 §4의 업로드를 뒤집은 것이다 — offset·chunk hash로 끊긴 곳부터 받는다. 예제 6은 단순화를 위해 처음부터 다시 쓴다.
3. **③ readback 검증**: 받은 바이트가 아니라 **flash에서 다시 읽은 바이트**로 hash를 계산한다. flash 쓰기 오류, 지우기 누락(NOR는 1→0만 가능), 드라이버 버그를 여기서 잡는다. Don이 SSD에서 program 후 read-verify를 하던 것과 같다.
4. **④ pending 표시**: 이 쓰기 한 번이 "이제 B를 시도해도 된다"는 결정이다. 원자적이어야 하므로 메타는 두 사본(ping-pong)에 seq와 CRC를 두고 번갈아 쓴다 — H1 §6의 commit marker와 같은 생각.
5. **⑤ 시도 횟수를 먼저 줄인다**: B로 넘어가기 **전에** tries를 줄여 저장한다. B가 크래시하면 다음 부팅은 줄어든 tries를 본다. 순서를 바꾸면(시험 후 줄이기) 크래시하는 모델로 무한 재부팅한다. MCUboot의 "test 이미지는 확인(confirm)하지 않으면 다음 부팅에 되돌린다"와 같은 원리다.
6. **⑥ self-test**: golden vector로 새 모델을 돌려 기대 출력과 비교한다(4.4절).
7. **⑦ 확정 / ⑧ 롤백**: 확정은 active를 B로 바꾸는 메타 쓰기 한 번. 롤백은 pending을 지우고 **거부한 버전을 기록**해서, 같은 패키지를 다시 받지 않게 한다(서버에도 보고).

**안전 지점(safe point)** 은 무엇인가. 추론 도중에 모델을 바꾸면 안 된다 — arena 안의 중간 텐서는 옛 모델의 레이아웃이다. 펌웨어는 다음 순간을 골라 바꾼다.

- **MCU (TFLM, 모델이 flash에서 XIP)**: 추론 task가 한 프레임의 `Invoke()`를 끝낸 직후, 다음 프레임 전에 인터프리터를 새 모델 포인터로 다시 만들고 `AllocateTensors()`를 부른다. arena가 하나뿐이면 그동안 몇 프레임을 버린다. streaming 상태(특징 창, smoothing 버퍼, RNN hidden state, refractory 타이머)도 함께 초기화한다 — 옛 모델의 확률 이력을 새 모델 threshold와 섞으면 안 된다.
- **AP (RAM 여유)**: 새 인터프리터를 백그라운드에서 만들고 warm-up까지 끝낸 뒤 포인터를 한 번에 바꾼다(RCU처럼). 끊김이 없다.
- **사용자 맥락**: 대화 중, 녹음 중, 충전 중이 아닐 때 등 제품 규칙. 서버 응답의 `activate: "next_idle"` 같은 필드로 정책을 준다.

### 4.3 코드로 확인 — 예제 6: 모든 flash 연산에서 전원을 끊는다

**예제 6** — 무엇을 확인하나: 시뮬레이션 flash(NOR 의미: erase → 0xFF, program은 1→0만, sector 4 KB, page 256 B)에 모델 슬롯 A/B와 메타 두 사본을 두고, 그림 5의 상태 기계를 구현한다. 끊김 없는 한 번의 실행에서 flash 연산이 몇 번 일어나는지 센 뒤, **k = 0, 1, 2, …, 마지막 연산 각각의 도중에** 전원을 끊는다(erase 도중이면 그 sector가 쓰레기, program 도중이면 앞 절반만 기록). 그 뒤 전원을 다시 넣고 부팅을 이어 가서 (1) 벽돌이 되는지, (2) 검증 안 된 모델로 부팅한 적이 있는지(각 부팅마다 독립적으로 슬롯 CRC를 원본과 비교), (3) 최종 버전이 무엇인지 센다. 새 모델이 정상 / self-test 실패 / 부팅하면 크래시인 세 경우를 모두 돌리고, 마지막에 슬롯 하나에 덮어쓰는 방식과 비교한다.

```c
/* ab_sim.c — A/B 모델 슬롯 + 영속 메타데이터 상태 기계. 모든 flash 연산 직후에 전원을 끊어 본다. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stddef.h>
enum { SECT = 4096, PAGE = 256, SLOT_SECT = 6, SLOT = SECT * SLOT_SECT, IMG = 20000 };
enum { SLOT_A = 0, META0 = 2 * SLOT, FLASH = 2 * SLOT + 2 * SECT, NONE = 0xFF };
static uint8_t flash[FLASH], img[3][IMG];           /* img[1]=v1, img[2]=v2 (다운로드한 패키지) */
static long ops, cut_at = -1; static int dead;       /* cut_at번째 연산 도중 전원 차단 */
static int op_state(void) {                          /* 0=정상, 1=이 연산 도중 끊김, 2=이미 꺼짐 */
    if (dead) return 2; if (ops++ == cut_at) { dead = 1; return 1; } return 0;
}
static void f_erase(uint32_t a) {                    /* NOR sector erase → 0xFF. 도중에 끊기면 쓰레기 */
    int st = op_state(); if (st == 2) return;
    for (int i = 0; i < SECT; i++) flash[a + i] = st ? (uint8_t)rand() : 0xFF;
}
static void f_prog(uint32_t a, const void *src, uint32_t n) {   /* program: 1→0만. 끊기면 앞 절반만 */
    int st = op_state(); if (st == 2) return;
    const uint8_t *p = src; uint32_t m = st ? n / 2 : n;
    for (uint32_t i = 0; i < m; i++) flash[a + i] &= p[i];
}
static uint32_t crc32(const uint8_t *p, size_t n) {
    uint32_t c = 0xFFFFFFFFu;
    while (n--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}
typedef struct { uint32_t magic, seq; uint8_t active, pending, tries, rejected;
                 uint32_t ver[2], len[2], crc[2], self_crc; } meta_t;
static uint32_t meta_crc(const meta_t *m) { return crc32((const uint8_t *)m, offsetof(meta_t, self_crc)); }
static int meta_load(meta_t *out) {                  /* 두 사본 중 CRC가 맞고 seq가 큰 것 */
    int best = -1; meta_t m;
    for (int i = 0; i < 2; i++) {
        memcpy(&m, flash + META0 + i * SECT, sizeof m);
        if (m.magic == 0x4154454D && m.self_crc == meta_crc(&m) && (best < 0 || m.seq > out->seq)) { *out = m; best = i; }
    }
    return best;
}
static void meta_store(meta_t m) {                   /* 오래된 사본 자리에 새 seq로 쓴다 (ping-pong) */
    meta_t cur; int at = meta_load(&cur); m.seq = (at < 0 ? 0 : cur.seq) + 1;
#ifdef SINGLE_META
    uint32_t dst = META0;                            /* 나쁜 예: 사본 하나를 지우고 다시 쓴다 */
#else
    uint32_t dst = META0 + (at == 0 ? SECT : 0);
#endif
    m.self_crc = meta_crc(&m);
    f_erase(dst); f_prog(dst, &m, sizeof m);
}
static int slot_ok(const meta_t *m, int s) {
    return m->len[s] == IMG && crc32(flash + SLOT_A + s * SLOT, IMG) == m->crc[s];
}
static void write_slot(int s, const uint8_t *src) {
    for (int i = 0; i < SLOT_SECT; i++) f_erase(SLOT_A + s * SLOT + i * SECT);
    for (int o = 0; o < IMG; o += PAGE) f_prog(SLOT_A + s * SLOT + o, src + o, IMG - o < PAGE ? IMG - o : PAGE);
}
/* 부트 로직: pending이 있으면 tries를 먼저 줄여 저장한 뒤 시험 부팅. 0이면 롤백 */
static int boot(meta_t *m, int *trial) {
    *trial = 0; if (meta_load(m) < 0) return -1;
    if (m->pending != NONE) {
        if (m->tries > 0 && slot_ok(m, m->pending)) { m->tries--; meta_store(*m); *trial = 1; return m->pending; }
        m->rejected = (uint8_t)m->ver[m->pending]; m->pending = NONE; meta_store(*m);  /* 롤백 */
    }
    if (slot_ok(m, m->active)) return m->active;
    return slot_ok(m, !m->active) ? !m->active : -1;                    /* 마지막 보루 */
}
static int new_bad;           /* 0=정상, 1=새 모델이 golden vector 시험 실패, 2=새 모델로 부팅하면 크래시 */
static long boots, bad_boots;  /* 부팅 횟수, 검증 안 된 모델로 부팅한 횟수 */
static int run_device(int *ver) {                    /* 부팅 → (시험이면) self-test → 확정/롤백 → 필요하면 업데이트 */
    for (int iter = 0; iter < 10 && !dead; iter++) {
        meta_t m; int trial, s = boot(&m, &trial); if (dead) break;
        if (s < 0) return -1;                                           /* 벽돌 */
        *ver = (int)m.ver[s]; boots++;
        if (crc32(flash + SLOT_A + s * SLOT, IMG) != crc32(img[*ver], IMG)) bad_boots++;  /* 독립 확인 */
        if (trial) {
            if (new_bad == 2) continue;                                  /* 크래시 → watchdog 리셋 */
            if (new_bad == 1) { m.rejected = (uint8_t)m.ver[s]; m.pending = NONE; meta_store(m); continue; }  /* self-test 실패 → 롤백 */
            m.active = (uint8_t)s; m.pending = NONE; meta_store(m); continue;  /* 확정 */
        }
        if (m.ver[m.active] >= 2 || m.rejected == 2) return 0;          /* 최신이거나 이미 거부한 버전 */
        int t = !m.active;                                               /* 비활성 슬롯에 다운로드 */
        m.len[t] = 0; m.pending = NONE; meta_store(m); if (dead) break;  /* ① 먼저 B를 "무효"로 */
        write_slot(t, img[2]); if (dead) break;                          /* ② 쓰기 */
        if (crc32(flash + SLOT_A + t * SLOT, IMG) != crc32(img[2], IMG)) continue;  /* ③ readback 검증 */
        m.ver[t] = 2; m.len[t] = IMG; m.crc[t] = crc32(img[2], IMG);
        m.pending = (uint8_t)t; m.tries = 2; meta_store(m);              /* ④ pending 표시 → 다음 안전 지점에 재부팅 */
    }
    return dead ? 1 : 0;
}
static void factory(void) {
    memset(flash, 0xFF, FLASH); ops = 0; cut_at = -1; dead = 0;
    write_slot(SLOT_A, img[1]);
    meta_t m = {0x4154454D, 0, SLOT_A, NONE, 0, 0, {1, 0}, {IMG, 0}, {crc32(img[1], IMG), 0}, 0};
    meta_store(m); ops = 0;
}
int main(void) {
    srand(7); for (int v = 1; v <= 2; v++) for (int i = 0; i < IMG; i++) img[v][i] = (uint8_t)rand();
    const char *name[3] = {"passes self-test", "fails self-test", "crashes on boot"};
    for (new_bad = 0; new_bad <= 2; new_bad++) {
        int ver = 0; factory(); run_device(&ver); long total = ops;     /* 끊김 없는 1회 = 연산 수 */
        long bricked = 0, fin[3] = {0}; boots = bad_boots = 0;
        for (long k = 0; k < total; k++) {
            factory(); cut_at = k; ver = 0; run_device(&ver);           /* k번째 연산 도중 전원 차단 */
            dead = 0; cut_at = -1;                                       /* 전원 복구 → 다시 부팅 */
            if (run_device(&ver) < 0) bricked++; else fin[ver]++;
        }
        printf("new model %-16s: ops=%2ld cuts=%2ld bricked=%ld bad_boots=%ld/%ld final v2=%2ld v1=%2ld\n",
               name[new_bad], total, total, bricked, bad_boots, boots, fin[2], fin[1]);
    }
    /* 비교: 슬롯 하나에 덮어쓰기 (in-place) */
    long bricked = 0, total = SLOT_SECT + (IMG + PAGE - 1) / PAGE;
    for (long k = 0; k < total; k++) {
        factory(); cut_at = k; write_slot(SLOT_A, img[2]); dead = 0;
        meta_t m; meta_load(&m); if (!slot_ok(&m, SLOT_A) && crc32(flash, IMG) != crc32(img[2], IMG)) bricked++;
    }
    printf("single slot, overwrite in place: cuts tested=%ld, no valid model after cut=%ld\n", total, bricked);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 ab_sim.c -o ab_sim && ./ab_sim
cc -std=c11 -Wall -Wextra -O2 -DSINGLE_META ab_sim.c -o ab_sim1 && ./ab_sim1 | head -3
```

```text
new model passes self-test: ops=93 cuts=93 bricked=0 bad_boots=0/370 final v2=93 v1= 0
new model fails self-test : ops=93 cuts=93 bricked=0 bad_boots=0/370 final v2= 0 v1=93
new model crashes on boot : ops=95 cuts=95 bricked=0 bad_boots=0/469 final v2= 0 v1=95
single slot, overwrite in place: cuts tested=85, no valid model after cut=85
new model passes self-test: ops=93 cuts=93 bricked=8 bad_boots=0/350 final v2=85 v1= 0
new model fails self-test : ops=93 cuts=93 bricked=8 bad_boots=0/350 final v2= 0 v1=85
new model crashes on boot : ops=95 cuts=95 bricked=10 bad_boots=0/441 final v2= 0 v1=85
```

출력에서 볼 것:

- **93개 연산을 손으로 세어 보자**: ① 메타 쓰기 2(erase + program) + ② 슬롯 쓰기 85(sector erase 6 + page program ⌈20000/256⌉ = 79) + ④ 메타 2 + ⑤ 시험 부팅 때 tries 감소 2 + ⑦ 확정 2 = 93. 크래시 경우는 시험 부팅이 두 번(tries 2 → 1 → 0)이고 ⑦ 대신 ⑧ 롤백이 있어 2 + 85 + 2 + 2 + 2 + 2 = 95.
- **A/B + ping-pong 메타: 281번 끊어서 벽돌 0, 검증 안 된 모델로 부팅 0**(1,209번의 부팅 모두 원본과 CRC 일치). 그리고 끊긴 뒤 재부팅하면 **결국 의도한 상태에 도달**한다 — 정상 모델은 93/93이 v2, self-test 실패·크래시 모델은 전부 v1로 돌아왔다. "벽돌이 안 된다"와 "업데이트가 결국 끝난다"를 둘 다 확인한 것이다.
- **덮어쓰기(슬롯 하나)는 85번 끊으면 85번 모두 유효한 모델이 없다**. 첫 sector erase가 시작되는 순간 옛 모델은 사라지고, 마지막 page가 끝나기 전까지 새 모델은 완성되지 않는다. 그 사이 어디서 끊겨도 기능이 죽는다.
- **`-DSINGLE_META`(메타 사본 하나를 지우고 다시 쓰기)는 8번(크래시 경우 10번) 벽돌**이다. 메타 쓰기 4번(크래시면 5번) × 연산 2개(erase, program) — 메타 erase 도중이나 program 도중에 끊기면 유효한 메타가 하나도 없다. 슬롯을 A/B로 해도 **"어느 슬롯이 유효한가"를 기록하는 곳**이 하나뿐이면 그곳이 단일 실패 지점이다. 이것이 메타를 두 사본 + seq + CRC로 두는 이유다.
- 이 시뮬레이션의 한계: torn program을 "앞 절반만"으로 모델링했고, 실제 NOR/NAND의 반쯤 program된 셀은 읽을 때마다 값이 달라질 수 있다(H1 §6.2). CRC 검사가 있으므로 결론은 같지만, 실기기에서는 **전원 차단 지그로 수천 번** 반복하는 시험이 따로 필요하다(J6).

### 4.4 self-test — 무엇을, 얼마나

시험 활성화 직후의 self-test는 "이 모델이 이 기기에서 **기대대로 계산하나**"를 본다. "이 모델이 **좋은가**"(정확도)는 서버와 rollout이 본다(§6). 둘을 섞지 않는다.

| 검사 | 방법 | 잡는 것 | 시간 (가정) |
|---|---|---|---|
| 로드 | 인터프리터 생성, `AllocateTensors()` 성공, 입력·출력 shape가 헤더와 일치 | op 누락, arena 부족, 스키마 불일치 | 수 ms |
| golden vector | 패키지에 동봉한 입력 N개 → 출력이 기대값과 일치(int8은 bit-exact 또는 ±1 LSB, C8 §3) | 커널 버그, 양자화 파라미터 혼동, 잘못된 payload | 추론 N회 |
| 전처리 포함 | 원시 오디오 1초 → 특징 → 모델 → 확률이 기대값 근처 | 전처리 버전 불일치(§7) | 추론 수십 회 |
| 자원 | 추론 시간 ≤ 예산, stack·arena 고수위 | 성능 회귀 | 위와 함께 |
| 안정성 | 시험 기간(예: 첫 10분) 동안 watchdog·HardFault 없음 | 드물게 터지는 크래시 | 시간 |

golden vector는 C8 §5처럼 **기준 모델(호스트의 같은 int8 모델)에서** 뽑아 패키지 metadata에 넣는다. 그러면 golden vector 자체가 서명 범위 안에 들어가므로 위조할 수 없고, 모델과 항상 짝이 맞는다. J6에서 golden vector 생성과 bit-exact 기준을 더 다룬다.

### 4.5 부트로더 방식 비교 — swap, overwrite, direct-XIP, dual-bank

펌웨어 OTA에 쓰는 부트로더(예: MCUboot)는 A/B 슬롯을 다루는 방식을 여러 개 제공한다. 이름과 큰 동작은 MCUboot 문서 기준이며, 세부 동작·지원 여부는 버전과 포트마다 다르다(hedge).

| 방식 | 동작 | flash 비용 | 롤백 | 모델 파티션에 쓸 때 |
|---|---|---|---|---|
| overwrite-only | secondary를 검증 후 primary에 복사 | 슬롯 2 | 복사 후엔 불가 | 단순하지만 롤백 없음 |
| swap (scratch / move) | primary와 secondary를 sector 단위로 맞바꿈, 진행 상태를 trailer에 기록해 끊겨도 이어서 | 슬롯 2 + scratch 또는 sector 1개 | 가능 (test → confirm, 미확정 시 되돌림) | 바꾸는 시간 = 슬롯 전체 erase·program, 수명 소모 2배 |
| direct-XIP | 두 슬롯 중 버전이 높고 유효한 쪽에서 직접 실행 | 슬롯 2 | 가능 (메타 플래그로) | **모델에 가장 자연스럽다** — 포인터만 바꾸면 된다 (예제 6의 방식) |
| ram-load | 선택한 슬롯을 RAM에 복사해 실행 | 슬롯 2 + RAM | 가능 | AP·RAM 넉넉한 곳 |
| HW dual-bank | 일부 MCU는 flash 두 bank의 주소를 옵션 비트로 맞바꿈 | 슬롯 2 | 가능 | 펌웨어용. 칩마다 다름 |

모델은 대개 **주소 독립적인 데이터**라서(flatbuffer는 상대 offset을 쓴다) 어느 슬롯에 있든 포인터만 넘기면 된다 — 펌웨어 코드처럼 링크 주소에 묶이지 않는다. 그래서 swap처럼 비싼 맞바꾸기가 필요 없고, direct-XIP 식(예제 6)이 가장 싸다. 단 모델 안에 절대 주소가 들어가는 포맷(일부 NPU 컴파일 결과가 메모리 주소를 고정하는 경우)이라면 확인이 필요하다.

### 4.6 delta 업데이트 — 얼마나 줄어드나

펌웨어 OTA에서는 bsdiff 같은 **delta(차분) 업데이트**가 흔하다. bsdiff(Colin Percival)는 옛 파일과 새 파일 사이에서 비슷한 구간을 찾아, "옛 파일의 어디를 복사하고 무엇을 더하라"는 지시 + 차이 바이트를 압축해 보낸다. 코드는 한 함수만 바뀌어도 주소가 밀려 바이트가 많이 바뀌지만, 차이가 규칙적이라 잘 압축된다.

모델은 어떨까. 직관: 가중치는 **재학습하면 거의 전부 바뀐다**. 처음부터 다시 학습하면 옛 가중치와 새 가중치는 무관한 난수처럼 보이고, 차이를 보내 봐야 이득이 없다. 미세조정(fine-tuning)이면 대부분의 가중치가 조금씩만 움직이므로 int8로 양자화하면 많은 값이 같거나 ±1만 다르다 — 차이가 잘 압축된다. 숫자로 확인한다.

**예제 7** — 무엇을 확인하나: 250 KB int8 가중치에서 (1) 미세조정 + 옛 scale 유지, (2) 미세조정 + scale 재계산(가장 큰 가중치가 5% 커짐), (3) 마지막 5% 층만 재학습, (4) 처음부터 재학습 네 경우의 "전체 압축 크기"와 "XOR·뺄셈 차분의 압축 크기"를 비교한다.

```python
# delta 업데이트가 얼마나 줄여 주나: 미세조정 vs 재학습, 양자화 scale 고정 vs 재계산
import numpy as np, zlib
rng = np.random.default_rng(0)
N = 250_000                                                     # int8 가중치 250 KB짜리 모델
w_old = rng.normal(0, 0.05, N).astype(np.float32)
def quant(w, scale=None):
    s = scale if scale is not None else np.abs(w).max() / 127   # per-tensor 대칭 int8
    return np.clip(np.round(w / s), -127, 127).astype(np.int8), s
q_old, s_old = quant(w_old)
w_ft = w_old + rng.normal(0, 0.0005, N).astype(np.float32)     # 미세조정: 작은 변화
w_ft[np.abs(w_ft).argmax()] *= 1.05                             # 가장 큰 가중치가 5% 커졌다
cases = {
    "fine-tune, scale frozen": quant(w_ft, s_old)[0],           # 옛 scale 유지 (큰 값은 clip)
    "fine-tune, scale recomputed": quant(w_ft)[0],              # scale이 5% 바뀜 → 거의 전부 이동
    "head-only fine-tune (last 5%)": np.concatenate([q_old[: int(N * 0.95)],
                                    quant(rng.normal(0, 0.05, N - int(N * 0.95)), s_old)[0]]),
    "full retrain (new init)": quant(rng.normal(0, 0.05, N).astype(np.float32))[0],
}
z = lambda b: len(zlib.compress(b, 9))
print(f"full image: raw {N} B, zlib {z(q_old.tobytes())} B")
print(f"{'case':31s} {'changed':>8s} {'full zlib':>10s} {'XOR zlib':>9s} {'SUB zlib':>9s}")
for name, q_new in cases.items():
    xor = np.bitwise_xor(q_old.view(np.uint8), q_new.view(np.uint8)).tobytes()
    sub = (q_new.astype(np.int16) - q_old).astype(np.int8).tobytes()   # 차이를 int8로 (wrap)
    print(f"{name:31s} {np.mean(q_new != q_old):8.1%} {z(q_new.tobytes()):10d} {z(xor):9d} {z(sub):9d}")
```

```text
full image: raw 250000 B, zlib 214548 B
case                             changed  full zlib  XOR zlib  SUB zlib
fine-tune, scale frozen            21.4%     214540     47915     39299
fine-tune, scale recomputed        72.5%     212096    117208     95686
head-only fine-tune (last 5%)       5.0%     214571     12037     12240
full retrain (new init)            99.0%     214989    225929    230275
```

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><text x="20" y="22" font-size="13">250 KB int8 모델의 업데이트 전송 크기 (zlib -9, 예제 7 실측)</text><text x="182" y="70" font-size="12" text-anchor="end">미세조정, scale 고정</text><rect x="190" y="50" width="308.9" height="18" fill="#888" fill-opacity="0.45"/><text x="504.9" y="64" font-size="12">전체 214.5 KB</text><rect x="190" y="72" width="56.6" height="18" fill="#3f9a6b"/><text x="252.6" y="86" font-size="12">delta 39.3 KB (18%)</text><text x="182" y="126" font-size="12" text-anchor="end">미세조정, scale 재계산</text><rect x="190" y="106" width="305.4" height="18" fill="#888" fill-opacity="0.45"/>
<text x="501.4" y="120" font-size="12">전체 212.1 KB</text><rect x="190" y="128" width="137.8" height="18" fill="#3f9a6b"/><text x="333.8" y="142" font-size="12">delta 95.7 KB (45%)</text><text x="182" y="182" font-size="12" text-anchor="end">마지막 5%만 재학습</text><rect x="190" y="162" width="309.0" height="18" fill="#888" fill-opacity="0.45"/><text x="505.0" y="176" font-size="12">전체 214.6 KB</text><rect x="190" y="184" width="17.6" height="18" fill="#3f9a6b"/><text x="213.6" y="198" font-size="12">delta 12.2 KB (5.7%)</text><text x="182" y="238" font-size="12" text-anchor="end">처음부터 재학습</text><rect x="190" y="218" width="309.6" height="18" fill="#888" fill-opacity="0.45"/>
<text x="505.6" y="232" font-size="12">전체 215.0 KB</text><rect x="190" y="240" width="331.6" height="18" fill="#d0564a"/><text x="527.6" y="254" font-size="12">delta 230.3 KB (107%)</text><line x1="190" y1="44" x2="190" y2="270" stroke="currentColor"/><line x1="190.0" y1="270" x2="190.0" y2="275" stroke="currentColor"/><text x="190.0" y="289" font-size="12" text-anchor="middle">0</text><line x1="262.0" y1="270" x2="262.0" y2="275" stroke="currentColor"/><text x="262.0" y="289" font-size="12" text-anchor="middle">50</text><line x1="334.0" y1="270" x2="334.0" y2="275" stroke="currentColor"/><text x="334.0" y="289" font-size="12" text-anchor="middle">100</text>
<line x1="406.0" y1="270" x2="406.0" y2="275" stroke="currentColor"/><text x="406.0" y="289" font-size="12" text-anchor="middle">150</text><line x1="478.0" y1="270" x2="478.0" y2="275" stroke="currentColor"/><text x="478.0" y="289" font-size="12" text-anchor="middle">200</text><line x1="550.0" y1="270" x2="550.0" y2="275" stroke="currentColor"/><text x="550.0" y="289" font-size="12" text-anchor="middle">250</text><text x="660" y="289" font-size="12" text-anchor="end">KB</text></svg>
```

그림 6 — 예제 7의 전송 크기. 회색은 새 모델 전체를 압축해 보낼 때, 초록·빨강은 옛 모델과의 뺄셈 차분을 압축해 보낼 때다. 괄호는 전체 대비 비율이다.

출력에서 볼 것:

- **int8 가중치 자체는 거의 압축되지 않는다**: 250,000 B → 214,548 B(86%). 정규분포 가중치를 int8로 양자화한 값은 엔트로피가 높다. "모델은 zlib으로 줄이면 된다"는 기대는 맞지 않는다.
- **처음부터 재학습하면 delta가 오히려 크다**(107%): 99%의 바이트가 바뀌었고, 두 난수의 차이는 난수보다 엔트로피가 높다. 이 경우 delta 기능은 해만 된다 — 서버가 "delta가 전체보다 작을 때만 delta"를 고르게 한다.
- **미세조정이라도 양자화 scale이 바뀌면 이득이 반으로 준다**: 가장 큰 가중치 하나가 5% 커져서 per-tensor scale이 바뀌자, 바뀐 바이트가 21.4% → 72.5%로 늘고 delta가 18% → 45%가 됐다. 모든 값이 새 scale로 다시 반올림되기 때문이다. 양자화 scale을 고정하면(대신 큰 값은 clip — 정확도 확인 필요) delta가 작게 유지된다.
- **마지막 층만 재학습(head-only fine-tune)이 delta에 가장 유리하다**(5.7%). 앞쪽 feature extractor를 얼리고 classifier head만 바꾸는 것은 ML에서 흔한 기법이고, 이 경우 delta OTA가 진짜로 이득이다. 더 나아가 head만 **별도 파일**로 분리하면 delta 없이도 작게 보낼 수 있다.
- 결론: 모델 delta는 "학습 방식이 허락할 때만" 의미가 있다. 기본은 전체 전송 + 재개 가능한 다운로드(H2), delta는 서버가 크기를 비교해 선택하는 최적화다. delta를 쓰면 기기가 **옛 슬롯을 읽어 새 슬롯을 만드는** 동안 옛 슬롯이 온전해야 하므로 A/B가 전제다.

### 4.7 함정

- **readback 검증 생략**: "다운로드한 바이트의 hash가 맞았다"는 flash에 제대로 써졌다는 뜻이 아니다. 슬롯에서 다시 읽어 검증한다.
- **pending 전에 활성화**: 쓰기가 끝나자마자 메모리상의 포인터를 새 모델로 바꾸고 메타는 나중에 쓰면, 그 사이 리셋 시 기기가 무엇을 돌렸는지 기록이 없다. 메타가 먼저, 활성화가 나중.
- **tries를 시험 후에 줄임**: 크래시하는 모델로 무한 재부팅(boot loop). 줄이고 저장한 뒤 시험한다.
- **streaming 상태를 안 지움**: 새 모델로 바꾼 직후 옛 모델의 확률 이력이 smoothing 창에 남아 첫 1~2초 동안 엉뚱한 trigger가 난다.
- **flash 수명**: 메타 섹터를 매 부팅마다 쓰면(예: 부팅 카운터) NOR erase 수명(대개 10만 회 수준, 부품마다 다름)을 금방 먹는다. 메타는 상태가 **바뀔 때만** 쓴다. 예제 6에서 정상 경로의 메타 쓰기는 업데이트 1회당 4번이다.

---

## 5. 보안 — 서명, secure boot, anti-rollback, 암호화

### 5.1 왜 모델에 서명하나 — "데이터지만 행동을 바꾸는 데이터"

모델은 코드가 아니라 데이터라서 "서명까지 필요한가?"라는 질문이 나온다. 필요하다. 이유 세 가지.

- **행동을 바꾼다**: wake-word 모델을 바꿔치기하면 기기가 아무 소리에나 깨어나 녹음을 시작하게 할 수 있다(프라이버시, H6). 제스처 모델이면 의도하지 않은 명령이 실행된다.
- **파서는 공격 면이다**: flatbuffer 파서·인터프리터에 조작된 offset·shape를 먹이면 범위 밖 메모리 접근이 날 수 있다. 런타임이 모든 입력을 완벽히 검사한다고 가정하지 않는다 — **서명을 통과한 파일만 파서에 넣는다**(예제 4의 순서).
- **추적성**: 서명 = "우리 CI가, 승인 절차를 거쳐 만든 파일"이라는 증명. 사고가 났을 때 "필드의 이 모델은 어느 빌드인가"에 답할 수 있다.

E8 §6.1의 chain of trust를 다시 보자. Boot ROM이 1단 bootloader를, bootloader가 펌웨어를 검증한다. 모델을 어디에 두느냐에 따라 **누가 모델을 검증하나**가 정해진다.

| 모델 위치 | 검증 주체 | 장점 | 단점 |
|---|---|---|---|
| 펌웨어 이미지 안 | bootloader (펌웨어 서명에 포함) | 추가 작업 없음 | 모델만 바꿀 수 없음 |
| 별도 파티션 (MCU) | 펌웨어의 업데이트 에이전트 + 부팅 시 로더 | 모델만 업데이트 | 펌웨어가 서명 검증 코드·공개키를 가져야 함 |
| AP 파일시스템 | 앱/서비스 또는 TEE | 유연 | 파일시스템 무결성(dm-verity 등)과 별개로 모델 서명 필요 |
| DSP·NPU 전용 메모리 | 서브시스템 로더(TEE·보안 블록인 경우 많음, E8 §6.1) | HW 격리 | 벤더 도구·서명 체계에 묶임 |

### 5.2 CRC, hash, MAC, 서명 — 무엇이 무엇을 막나

| 도구 | 막는 것 | 못 막는 것 | 기기에 필요한 것 |
|---|---|---|---|
| CRC-32 | 우연한 비트 오류 (flash 열화, 전송 오류) | 의도적 변조 (누구나 CRC를 다시 계산) | 없음 |
| SHA-256 | 우연한 오류 + 서명된 값과 비교할 때 변조 | hash 자체를 바꿔치기하면 무력 | 없음 |
| HMAC (대칭) | 키를 모르는 자의 변조 | 기기 하나에서 키를 뽑으면 **fleet 전체 위조 가능** | 비밀키 (보호 필요) |
| Ed25519 · ECDSA (비대칭) | 비밀키 없는 자의 변조 | 서버 비밀키 유출 | **공개키만** (유출돼도 위조 불가) |

예제 2·4가 HMAC을 쓴 것은 환경 제약 때문이다. 제품에서는 **비대칭 서명**을 쓰고, 기기에는 공개키만 둔다. 공개키는 펌웨어 이미지 안(펌웨어 서명으로 보호됨)이나 OTP에 hash로 둔다. 서명 알고리즘 선택은 검증 비용·코드 크기·HW 가속기 지원으로 정한다 — Ed25519(RFC 8032)는 구현이 단순하고 빠르며, ECDSA P-256은 많은 MCU의 암호 가속기가 지원한다.

### 5.3 anti-rollback — 기능 롤백과 보안 롤백은 다르다

롤백은 **우리가 원할 때**는 기능이다(나쁜 모델에서 이전 모델로). 그런데 공격자가 원할 때는 공격이다 — 서명은 정상인 **옛 취약 버전**을 다시 설치하는 것(downgrade attack). 둘을 구분하려면 버전을 두 개 둔다.

- **semver**: 기능 버전. 롤백하면 내려갈 수 있다.
- **security_version**: 보안 버전. 기기의 **단조 증가 카운터**(OTP fuse, RPMB, 보안 저장소)보다 낮으면 거부한다. 보안 문제를 고친 릴리스에서만 올린다.

**예제 8** — 무엇을 확인하나: fuse(한 번 1이 되면 0으로 못 돌리는 비트)로 만든 카운터와 두 버전을 써서, 같은 보안 버전 안의 기능 롤백은 허용되고 보안 수정 이후의 다운그레이드는 거부되는지 본다.

```python
# anti-rollback: 기능 버전(semver)과 보안 버전(security_version, 단조 카운터)을 분리한다
fuses = 0b0000_0011                          # OTP fuse: 한 번 1이 되면 0으로 못 돌린다. 카운터 = 1의 개수
counter = lambda: bin(fuses).count("1")
def install(name, semver, secver, signed=True):
    if not signed: return f"{name:31s} REJECT  bad signature"
    if secver < counter(): return f"{name:31s} REJECT  secver {secver} < counter {counter()}"
    return f"{name:31s} ACCEPT  (semver {semver}, secver {secver})"
def confirm(secver):                         # self-test 통과 + 확정 후에만 fuse를 태운다
    global fuses
    while counter() < secver: fuses |= 1 << counter()
print("counter =", counter())
print(install("kws 2.1.0", "2.1.0", 2))
print(install("kws 2.0.3 (rollback, same sec)", "2.0.3", 2))     # 기능 롤백은 허용
print(install("kws 2.2.0 (fixes CVE)", "2.2.0", 3)); confirm(3)
print("counter =", counter(), f"fuses={fuses:08b}")
print(install("kws 2.1.0 (vulnerable)", "2.1.0", 2))             # 취약 버전으로 되돌리기 차단
print(install("kws 2.2.1", "2.2.1", 3))
print(install("kws 9.9.9 (forged)", "9.9.9", 9, signed=False))
```

```text
counter = 2
kws 2.1.0                       ACCEPT  (semver 2.1.0, secver 2)
kws 2.0.3 (rollback, same sec)  ACCEPT  (semver 2.0.3, secver 2)
kws 2.2.0 (fixes CVE)           ACCEPT  (semver 2.2.0, secver 3)
counter = 3 fuses=00000111
kws 2.1.0 (vulnerable)          REJECT  secver 2 < counter 3
kws 2.2.1                       ACCEPT  (semver 2.2.1, secver 3)
kws 9.9.9 (forged)              REJECT  bad signature
```

출력에서 볼 것:

- 2.1.0 → 2.0.3은 semver가 내려가지만 security_version이 같아서 허용된다. **나쁜 모델에서 도망칠 길**은 열려 있다.
- 2.2.0을 확정한 뒤 카운터가 3이 되자, 서명이 정상인 2.1.0도 거부된다. 공격자가 옛 패키지를 재전송해도 소용없다.
- fuse는 **확정(confirm) 후에만** 태운다. 시험 활성화 단계에서 태우면, 새 모델이 self-test에 실패해 롤백하려 할 때 옛 모델이 이미 "너무 오래된 버전"이 되어 **돌아갈 곳이 없어진다**. 예제 6의 ⑦ 확정과 같은 순간에 카운터를 올린다.
- fuse 비트는 유한하다(이 예에선 8개). 그래서 security_version은 **보안 수정 때만** 올린다 — 모델 재학습마다 올리면 금방 바닥난다. 모델이 보안 문제가 아니라면 security_version은 펌웨어와 공유하거나 거의 안 올린다.

실제 구현 예: Android Verified Boot는 vbmeta의 rollback index를 변조 방지 저장소와 비교하고, MCUboot는 이미지 TLV의 security counter를 HW 카운터와 비교하는 옵션이 있다(세부는 각 문서 참고, hedge).

### 5.4 암호화 — 모델 IP 보호 (hedge)

서명은 **변조**를 막지만 **복사**는 막지 않는다. 경쟁사가 flash를 덤프하면 모델 가중치를 그대로 가져갈 수 있다. 모델이 회사의 핵심 자산이라면 암호화를 검토한다. 선택지와 비용을 정리하면(구체 기능은 칩마다 다르므로 hedge):

- **전송 중 암호화**: TLS로 다운로드. 기본. 하지만 기기에 내려온 뒤에는 평문이다.
- **저장 시 암호화(at rest)**: 패키지 payload를 AES(예: AES-GCM — 기밀성과 무결성을 함께)로 암호화하고, 기기에서 복호화. 키는 **기기 고유 키**(칩 안 보안 저장소, 꺼낼 수 없는 키)로 감싼 형태로 둔다. fleet 공통 키 하나를 펌웨어에 박으면 한 대만 털려도 끝이다.
- **복호화 위치**: RAM으로 복호화해서 쓰면 RAM이 모델 크기만큼 필요하다(MCU에선 비쌈). 일부 MCU는 외부 flash를 읽는 순간 하드웨어가 복호화해 주는 기능(on-the-fly decryption)을 제공한다. AP에서는 TEE 안에서 복호화하고 NPU 보안 메모리로 넘기는 구조가 쓰인다고 알려져 있다.
- **비용**: 부팅·활성화 시간, 코드 크기, 키 provisioning(공장에서 기기마다 키 주입 — Don의 factory test station 영역), 디버깅 난이도(덤프를 봐도 모른다).

현실적으로 "모델 IP 보호"는 **비용 대비 효과**를 따져야 한다. 수백 KB짜리 KWS 모델보다는 큰 LLM·독자 데이터로 만든 모델에서 더 논의된다.

### 5.5 키 관리 기초

- **서명 키는 HSM(hardware security module)이나 클라우드 KMS 안에** 두고, CI가 서명을 **요청**만 한다. 개발자 노트북에 비밀키가 있으면 안 된다.
- **릴리스 승인 게이트**: 서명 요청 전에 정확도 회귀(C8 §6), golden vector 생성, 호환성 검사가 통과해야 한다. "서명됐다 = 검증 게이트를 통과했다"가 되게 한다.
- **키 순환(rotation)**: 기기에 공개키를 **두 개 이상**(현재 + 다음) 넣어 두면, 비밀키가 유출됐을 때 다음 키로 서명을 바꾸고 옛 키를 폐기하는 펌웨어를 배포할 수 있다. 공개키 하나만 ROM에 박으면 유출 시 돌이킬 수 없다.
- **용도별 키 분리**: 펌웨어 서명 키, 모델 서명 키, config 서명 키(H8)를 나눈다. 모델 팀이 펌웨어 키에 접근할 필요가 없다 — 권한과 피해 범위를 나눈다.
- **dev 키와 prod 키**: 개발 기기는 dev 키를 신뢰하고 양산 기기는 prod 키만 신뢰한다. dev 서명 모델이 양산 기기에 설치되면 안 된다(공장 provisioning 단계에서 결정).

### 5.6 함정

- **서명 전에 파싱**: 헤더의 길이 필드로 메모리를 할당하거나 payload를 읽기 시작한 뒤에 서명을 검사하면, 서명 검사 전에 공격이 끝난다.
- **비교를 `memcmp`로**: 서명·MAC 비교는 상수 시간 비교를 쓴다. `memcmp`는 첫 다른 바이트에서 멈춰 timing으로 정보를 흘릴 수 있다(예제 4의 주석).
- **security_version을 semver에서 자동 생성**: "major가 바뀌면 security_version도 올린다" 같은 규칙은 기능 롤백을 막아 버린다. 둘은 독립적으로 관리한다.

---

## 6. 단계적 배포와 모니터링

### 6.1 왜 단계적인가 — "통과했지만 나쁜" 업데이트

예제 6의 self-test는 "계산이 기대대로인가"를 본다. 그런데 모델 업데이트의 진짜 위험은 **계산은 맞는데 결과가 나쁜** 경우다. 학습 데이터에 없던 HW rev의 마이크, 특정 언어 억양, 특정 환경 소음에서만 오탐이 늘어난다. 이것은 기기 한 대가 스스로 알 수 없다 — **fleet 통계**로만 보인다. 그래서 일부 기기에 먼저 배포하고, 지표를 보고, 넓힌다.

단계 예시(숫자는 가정): 사내 dogfood(H8 §6) → 1% → 5% → 25% → 50% → 100%. 단계마다 최소 관찰 기간(예: 48시간 — 사람의 하루 사용 패턴을 두 번 본다)과 최소 표본 수를 정한다.

**결정적 cohort**(H8 §3): 기기를 `hash(salt + device_id) mod 10000`으로 bucket에 넣고, `bucket < pct × 100`인 기기만 새 모델을 받는다. salt를 릴리스마다 바꾸면 매번 같은 기기가 첫 1%가 되는 것(같은 사용자가 늘 실험 대상)을 피한다. 단계를 넓혀도 앞 단계 기기는 그대로 포함된다(`bucket < 100`이면 `bucket < 500`이기도 하다).

### 6.2 무엇을 보나 — 지표

| 지표 | 출처 | 나쁜 신호 (예시 기준, 가정) | 성격 |
|---|---|---|---|
| 활성화 성공률 | 기기 보고: 확정 / 롤백 / 거부 사유 | 롤백 > 0.5% | 즉시 halt |
| self-test 통과율 | 기기 보고 | < 99.5% | 즉시 halt |
| 크래시·watchdog | 리셋 사유 레지스터, 크래시 덤프 | 대조군 대비 증가 | 즉시 halt |
| 추론 에러 카운터 | `Invoke` 실패, arena 할당 실패, 타임아웃 | > 0 이 늘어남 | 즉시 halt |
| trigger율 (오탐 proxy) | 하루 wake 횟수, 깨어난 뒤 아무 말 없음 비율 | 대조군 대비 × 1.5 | 통계 판정 |
| 놓침 proxy | 사용자가 다시 말한 비율, 버튼으로 깨운 비율 | 대조군 대비 증가 | 통계 판정 |
| latency | 프레임당 추론 시간 p99 | 예산 초과 | 통계 판정 |
| 배터리 | 하루 평균 전류·mAh/day (H7) | 대조군 대비 + 3% | 느린 판정 (수일) |

**대조군**이 핵심이다. 같은 기간에 옛 모델을 쓰는 기기와 비교해야 요일·날씨·이벤트 효과가 상쇄된다. 단계적 배포는 그 자체로 무작위 대조 실험(A/B test)이다 — hash bucket이 무작위 배정 역할을 한다.

### 6.3 halt와 rollback

- **자동 halt**: 즉시 halt 지표는 사람 승인을 기다리지 않고 배포를 멈춘다. 통계 지표는 기준을 넘으면 halt + 사람에게 알림.
- **rollback은 싸다**: 기기의 A 슬롯에 옛 모델이 그대로 있으므로, 서버가 "이전 번들로 돌아가라"(서명된 명령)를 보내면 **다운로드 없이** 메타의 active만 바꾸면 된다. H8 §2.5의 kill switch와 같은 경로로 간다 — 그래서 rollback이 기기에 닿는 시간은 check-in 주기에 묶인다. 긴급 rollback이 필요한 기능이라면 check-in 주기를 그것에 맞춘다.
- **기기 쪽 자체 판단**: 서버를 기다릴 수 없는 명백한 이상(새 모델로 바꾼 뒤 1시간 trigger가 평소의 10배)은 기기가 스스로 롤백하는 규칙을 둘 수도 있다. 단 기준을 보수적으로 — 기기 혼자서는 "시끄러운 파티에 있다"와 "모델이 나쁘다"를 구분하기 어렵다.

### 6.4 코드로 확인 — 예제 9: 합산하면 놓친다

**예제 9** — 무엇을 확인하나: 20,000대 fleet(HW rev A 45%, B 40%, C 15%)에 새 모델을 배포한다. 새 모델은 **rev C에서만 오탐이 3배**다(가정). 단계마다 2일 × 16시간 착용 동안의 오탐 수를 Poisson으로 뽑아, (1) 전체 합산 비율로 판정하는 정책과 (2) HW rev별로 쪼개서 가장 나쁜 rev로 판정하는 정책을 비교한다. halt 기준은 둘 다 "새/옛 오탐률 비 > 1.5".

```python
# staged rollout: 결정적 cohort + 단계별 halt 판정. 새 모델이 HW rev C(15%)에서만 오탐 3배라면?
import hashlib, numpy as np
rng = np.random.default_rng(11)
N = 20_000
ids = [f"dev{i:05d}" for i in range(N)]
rev = rng.choice(["A", "B", "C"], N, p=[0.45, 0.40, 0.15])
bucket = np.array([int.from_bytes(hashlib.sha256(f"kws-2.1.0:{d}".encode()).digest()[:4], "big") % 10_000
                   for d in ids])                                    # salt = 릴리스 이름
BASE_FA, HOURS = 0.5, 2 * 16                                         # 오탐/시간, 단계당 2일 × 16시간 착용
def fa_rate(new):                                                    # 기기별 실제 오탐률 (가정)
    return np.where(new & (rev == "C"), BASE_FA * 3.0, BASE_FA)
def judge(new, mask):                                                # 같은 기간 대조군(옛 모델)과 비교
    fa = rng.poisson(fa_rate(new) * HOURS)
    t, c = mask & new, mask & ~new
    return fa[t].sum() / (t.sum() * HOURS), fa[c].sum() / (c.sum() * HOURS), t.sum()
for policy in ("pooled", "per-rev"):
    exposed_c = 0
    for pct in (1, 5, 25, 50):
        new = bucket < pct * 100                                     # 넓혀도 앞 단계 기기는 그대로 포함
        exposed_c = int((new & (rev == "C")).sum())
        r_t, r_c, n = judge(new, np.ones(N, bool))
        per = {h: judge(new, rev == h) for h in "ABC"}
        worst = max((v[0] / v[1], h) for h, v in per.items())
        print(f"{policy:7s} stage {pct:2d}%: n={n:5d} pooled x{r_t / r_c:.2f}  worst rev {worst[1]} x{worst[0]:.2f}")
        if (r_t / r_c if policy == "pooled" else worst[0]) > 1.5:
            print(f"        -> HALT + rollback at {pct}%"); break
    else:
        exposed_c = int((rev == "C").sum()); print("        -> promoted to 100% (no control group left)")
    print(f"        rev C devices that got the bad model: {exposed_c}")
print("rev share:", {h: round(float(np.mean(rev == h)), 3) for h in "ABC"},
      " 1% cohort size by rev:", {h: int(((bucket < 100) & (rev == h)).sum()) for h in "ABC"})
```

```text
pooled  stage  1%: n=  208 pooled x1.32  worst rev C x2.99
pooled  stage  5%: n= 1037 pooled x1.31  worst rev C x2.99
pooled  stage 25%: n= 5026 pooled x1.29  worst rev C x3.00
pooled  stage 50%: n=10097 pooled x1.29  worst rev C x3.02
        -> promoted to 100% (no control group left)
        rev C devices that got the bad model: 2953
per-rev stage  1%: n=  208 pooled x1.31  worst rev C x2.93
        -> HALT + rollback at 1%
        rev C devices that got the bad model: 34
rev share: {'A': 0.457, 'B': 0.396, 'C': 0.148}  1% cohort size by rev: {'A': 81, 'B': 93, 'C': 34}
```

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg"><text x="20" y="22" font-size="13">새 모델 / 옛 모델 오탐률 비 — 단계별 (예제 9 실측, pooled 정책 실행)</text><line x1="50" y1="250" x2="430" y2="250" stroke="currentColor"/><line x1="50" y1="40" x2="50" y2="250" stroke="currentColor"/><text x="44" y="254" font-size="12" text-anchor="end">x0</text><text x="44" y="194" font-size="12" text-anchor="end">x1</text><text x="44" y="134" font-size="12" text-anchor="end">x2</text><text x="44" y="74" font-size="12" text-anchor="end">x3</text><line x1="50" y1="160.0" x2="430" y2="160.0" stroke="#d0564a" stroke-dasharray="5 4"/><text x="428" y="154.0" font-size="12" text-anchor="end">halt 기준 x1.5</text>
<polyline points="70,170.8 180,171.4 290,172.6 400,172.6" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="70" cy="170.8" r="4" fill="#4a7bd0"/><circle cx="180" cy="171.4" r="4" fill="#4a7bd0"/><circle cx="290" cy="172.6" r="4" fill="#4a7bd0"/><circle cx="400" cy="172.6" r="4" fill="#4a7bd0"/><polyline points="70,70.6 180,70.6 290,70.0 400,68.8" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="70" cy="70.6" r="4" fill="#e08a3c"/><circle cx="180" cy="70.6" r="4" fill="#e08a3c"/><circle cx="290" cy="70.0" r="4" fill="#e08a3c"/><circle cx="400" cy="68.8" r="4" fill="#e08a3c"/><text x="70" y="56.8" font-size="12">HW rev C만 (x3.0)</text>
<text x="180" y="191.4" font-size="12">전체 합산 (x1.3) — 기준 아래</text><text x="70" y="268" font-size="12" text-anchor="middle">1%</text><text x="180" y="268" font-size="12" text-anchor="middle">5%</text><text x="290" y="268" font-size="12" text-anchor="middle">25%</text><text x="400" y="268" font-size="12" text-anchor="middle">50%</text><text x="240" y="286" font-size="12" text-anchor="middle">rollout 단계</text><text x="470" y="60" font-size="13">rev C 기기 중 나쁜 모델을 받은 수</text><text x="470" y="90" font-size="12">pooled 정책</text><rect x="470" y="98" width="150.0" height="22" fill="#d0564a"/><text x="626.0" y="114" font-size="12">2953대</text><text x="470" y="150" font-size="12">rev별 정책</text>
<rect x="470" y="158" width="2.0" height="22" fill="#3f9a6b"/><text x="478.0" y="174" font-size="12">34대</text><text x="470" y="236" font-size="12">pooled: 100%까지 승격</text><text x="470" y="254" font-size="12">rev별: 1%에서 halt + rollback</text></svg>
```

그림 7 — 예제 9. 왼쪽: pooled 정책으로 진행한 단계별 오탐률 비. 전체 합산(파랑)은 1.3 근처에서 halt 기준(빨간 점선) 아래에 머물고, rev C만 따로 보면(주황) 처음부터 3배다. 오른쪽: 나쁜 모델을 받은 rev C 기기 수.

출력에서 볼 것:

- **합산 비율은 1.3배**다. 손으로 확인: 0.85 × 1 + 0.15 × 3 = 1.30. 15% 기기의 3배 문제는 전체로 보면 30% 증가로 **희석**된다(H7 §3.4의 희석 산수). 기준 1.5를 넘지 못해 100%까지 승격되고, rev C 기기 2,953대 전부가 나쁜 모델을 받는다.
- **rev별로 쪼개면 1% 단계에서 바로 잡힌다**. 1% cohort의 rev C는 34대뿐이지만, 34대 × 32시간 × 0.5/h ≈ 544건(옛 모델 기대값) vs 3배인 1,632건이면 Poisson 잡음(√544 ≈ 23)보다 훨씬 크다. 노출된 rev C 기기는 34대로 끝난다.
- 그러면 "모든 차원으로 다 쪼개면 되나?" — 차원이 많으면 우연히 기준을 넘는 칸이 생긴다(다중 비교). 쪼갤 차원은 **알려진 위험 축**(HW rev, 마이크 공급사, 펌웨어 버전, 지역·언어)으로 미리 정하고, 칸마다 최소 표본 수를 둔다. H7 §5의 CUSUM·EWMA를 칸별 시계열에 쓰면 오경보와 검출 지연의 균형을 잡을 수 있다.
- 100% 단계에서는 **대조군이 사라진다**. 그 뒤로는 "배포 전 기간"과 비교할 수밖에 없어 판정이 약해진다. 그래서 일부 기기(예: 1%)를 한동안 옛 모델에 남겨 두는 holdout을 두기도 한다.

### 6.5 shadow mode — 활성화 전에 몰래 돌려 보기

H8 §7과 C8에서 본 shadow mode를 rollout 앞에 붙일 수 있다. 새 모델을 **함께 돌리되 결과로 행동하지 않고**, 옛 모델과의 불일치만 기록한다.

- **무엇을 얻나**: 실제 사용자 환경에서 "새 모델이라면 몇 번 깨어났을까"를 사용자 경험 손상 없이 잰다. 불일치 구간을 (동의 범위 안에서) 올려 라벨하면, 새 모델의 FP 감소와 FN 변화를 직접 잴 수 있다(H8 예제 7).
- **비용**: 추론이 두 배다. always-on KWS를 MCU·DSP에서 두 개 돌리는 것은 전력·메모리 예산(I1)을 깬다. 그래서 (1) 충전 중에만, (2) 1차 단계(VAD 통과 프레임)에만, (3) AP가 이미 깨어 있을 때만, (4) 녹음된 버퍼에 대해 나중에 — 같은 방식으로 비용을 제한한다.
- **언제 쓰나**: threshold·민감도를 바꾸는 등 사용자 경험이 크게 달라질 수 있는 변경, 새 HW rev에 처음 들어가는 모델. 순서는 shadow(행동 안 함) → canary(작은 cohort에서 행동) → 단계적 확대.

### 6.6 함정

- **대조군 없는 비교**: "어제보다 오탐이 늘었다"는 어제가 일요일이었을 수도 있다. 같은 기간 옛 모델 기기와 비교한다.
- **합산만 봄**: 예제 9. 알려진 위험 축으로 쪼갠다.
- **사람이 승인해야 halt**: 금요일 밤에 나빠지면 월요일까지 퍼진다. 명백한 지표는 자동 halt.
- **rollout 단계 = 다운로드 단계**: 다운로드를 다 받아 놓고 활성화만 단계적으로 하면 롤백은 빠르지만, flash 쓰기·데이터 비용은 전체가 낸다. 반대로 다운로드를 단계적으로 하면 100%까지 시간이 걸린다. 기능의 위험도로 고른다.

---

## 7. 다중 이미지 일관성 — 하나의 원자적 번들

### 7.1 threshold는 모델에 묶여 있다

wake-word 모델의 출력은 "wake word일 확률"이고, 기기는 `확률 ≥ threshold`이면 깨어난다. 그런데 이 확률의 **눈금**은 모델마다 다르다. 새 모델이 더 자신감 있게(전체적으로 높은 점수를) 내도록 학습되면, 같은 threshold에서 오탐이 폭증한다. threshold는 각 모델의 검증 데이터로 "FA/h 목표"(예: 0.5회/시간)를 맞추도록 **모델마다** 정해진다.

**예제 10** — 무엇을 확인하나: 두 모델(v1, v2)의 점수 분포를 가정하고(v2가 양성·음성을 더 잘 가르지만 전체 점수가 위로 이동), 각 모델에 맞춘 threshold와 **짝이 어긋난** threshold에서 FRR(놓침 비율)과 FA/h(시간당 오탐)를 잰다.

```python
# wake-word threshold는 모델 버전에 묶여 있다: 새 모델 + 옛 threshold = 오탐 폭주
import numpy as np
rng = np.random.default_rng(3)
HOURS, RATE = 100, 10                                  # 음성 아닌 소리 100시간, 초당 10회 판정
sig = lambda x: 1 / (1 + np.exp(-x))
models = {"v1": dict(pos=(4.0, 1.4), neg=(-4.0, 1.2)),  # logit 분포 (가정)
          "v2": dict(pos=(5.6, 1.3), neg=(-2.0, 1.1))}  # 더 잘 가르지만 전체가 위로 이동
S = {}
for m, p in models.items():
    S[m] = (sig(rng.normal(*p["pos"], 5_000)),                      # wake word 발화 5000개
            sig(rng.normal(*p["neg"], HOURS * 3600 * RATE)))       # 음성 아닌 판정 360만 회
def tune(neg, target_fa_h=0.5):                       # FA/h 목표를 맞추는 threshold
    return float(np.quantile(neg, 1 - target_fa_h / (3600 * RATE)))
th = {m: tune(S[m][1]) for m in S}
print("tuned thresholds:", {m: round(t, 4) for m, t in th.items()})
for m in ("v1", "v2"):
    for tname in ("v1", "v2"):
        pos, neg = S[m]
        frr = np.mean(pos < th[tname]); fa_h = np.sum(neg >= th[tname]) / HOURS
        print(f"model {m} + threshold({tname})={th[tname]:.4f}: FRR {frr:6.2%}  FA/h {fa_h:7.2f}")
```

```text
tuned thresholds: {'v1': 0.7449, 'v2': 0.9306}
model v1 + threshold(v1)=0.7449: FRR  1.88%  FA/h    0.50
model v1 + threshold(v2)=0.9306: FRR 15.72%  FA/h    0.00
model v2 + threshold(v1)=0.7449: FRR  0.08%  FA/h   93.41
model v2 + threshold(v2)=0.9306: FRR  0.90%  FA/h    0.50
```

```svg
<svg viewBox="0 0 680 270" xmlns="http://www.w3.org/2000/svg"><text x="20" y="22" font-size="13">모델과 threshold 조합별 오탐(FA/h)과 놓침(FRR) — 예제 10 실측</text><text x="250" y="48" font-size="12" text-anchor="middle">FA / 시간 (0~100)</text><text x="530" y="48" font-size="12" text-anchor="middle">FRR (0~16%)</text><text x="150" y="80" font-size="12" text-anchor="end">v1 + th(v1) 0.745</text><rect x="160" y="64" width="1.5" height="22" fill="#4a7bd0"/><text x="167.5" y="80" font-size="12">0.50</text><rect x="440" y="64" width="21.1" height="22" fill="#4a7bd0"/><text x="467.1" y="80" font-size="12">1.88%</text><text x="150" y="126" font-size="12" text-anchor="end">v2 + th(v2) 0.931</text>
<rect x="160" y="110" width="1.5" height="22" fill="#4a7bd0"/><text x="167.5" y="126" font-size="12">0.50</text><rect x="440" y="110" width="10.1" height="22" fill="#4a7bd0"/><text x="456.1" y="126" font-size="12">0.90%</text><text x="150" y="172" font-size="12" text-anchor="end">v2 + th(v1) 0.745</text><rect x="160" y="156" width="168.1" height="22" fill="#d0564a"/><text x="334.1" y="172" font-size="12">93.41</text><rect x="440" y="156" width="1.5" height="22" fill="#4a7bd0"/><text x="447.5" y="172" font-size="12">0.08%</text><text x="150" y="218" font-size="12" text-anchor="end">v1 + th(v2) 0.931</text>
<rect x="160" y="202" width="1.5" height="22" fill="#4a7bd0"/><text x="167.5" y="218" font-size="12">0.00</text><rect x="440" y="202" width="176.8" height="22" fill="#d0564a"/><text x="622.9" y="218" font-size="12">15.72%</text><line x1="160" y1="58" x2="160" y2="246" stroke="currentColor"/><line x1="440" y1="58" x2="440" y2="246" stroke="currentColor"/><text x="20" y="262" font-size="12">짝이 맞으면 두 지표 모두 정상. 짝이 어긋나면 한쪽이 폭발한다 (빨강).</text></svg>
```

그림 8 — 예제 10의 네 조합. 위 두 줄은 짝이 맞는 조합(둘 다 FA 0.5/h, FRR 1~2%), 아래 두 줄은 짝이 어긋난 조합이다.

출력에서 볼 것:

- **새 모델 + 옛 threshold = 시간당 오탐 93회** — 목표 0.5의 187배. 사용자 입장에서는 "업데이트 후 기기가 계속 혼자 깨어난다"이고, 배터리(AP를 깨우는 비용, E8 §5)와 프라이버시(원치 않는 녹음)까지 같이 망가진다. 새 모델 자체는 **더 좋은** 모델인데도.
- **옛 모델 + 새 threshold = 놓침 15.7%** — 모델은 롤백했는데 threshold는 그대로 둔 경우다. 롤백도 **번들 단위**여야 한다.
- 짝이 맞으면 v2가 v1보다 좋다(같은 FA에서 FRR 1.88% → 0.90%). 개선은 **짝이 맞을 때만** 실현된다.

### 7.2 번들 — 한 번에 바뀌어야 하는 것들

threshold만이 아니다. 모델이 학습될 때 가정한 모든 것이 짝이다.

| 구성 요소 | 예 | 모델과 어긋나면 |
|---|---|---|
| 모델 가중치·그래프 | `.tflite` | — |
| 전처리 파라미터 | mel bin 수, FFT 크기, 창, log 바닥, 정규화 평균·분산 (I4) | 특징 분포가 틀어져 정확도 하락, 조용히 |
| 입력 양자화 | scale, zero point (헤더 input spec) | 입력 포화·해상도 손실 |
| 후처리 | threshold, smoothing 창, refractory 시간, 클래스별 threshold | 예제 10 |
| label map | 출력 index → 이름 | 엉뚱한 명령 실행 |
| golden vector | self-test 입력·기대 출력 | self-test가 정상 모델을 거부하거나 그 반대 |

그래서 **번들 매니페스트** 하나가 이 모두를 묶고, 번들에 버전 하나를 준다. 기기는 번들 단위로 다운로드·검증하고, **포인터 하나**(메타의 active 슬롯)를 바꿔 한꺼번에 활성화한다. 예제 6의 슬롯 하나에 번들 전체(헤더 + 모델 + 파라미터 + golden vector)를 넣으면 원자성은 공짜로 따라온다.

```text
bundle kws 2.1.0  (security_version 4, min_runtime_abi 2, hw 0x00020001)
  model.tflite        sha256 176d…2058   20008 B
  preproc.bin         sha256 9e01…77aa     64 B   (preproc v3: 49 frames × 10 MFCC, 30 ms / 20 ms hop)
  postproc.bin        sha256 4c2d…0b19     32 B   (threshold 0.9306, smooth 3 frames, refractory 1.5 s)
  labels.txt          sha256 a0f3…11c2    120 B
  golden.bin          sha256 5b7e…e940   1600 B   (8 vectors, int8 bit-exact expected)
  signature           Ed25519 over the manifest above  (예제에서는 HMAC으로 흉내)
```

(위 매니페스트의 hash 값은 첫 줄 model.tflite 앞부분만 예제 3의 실제 값이고, 나머지는 형식을 보여 주기 위한 자리 표시다.)

### 7.3 DSP·NPU 펌웨어 의존성

이기종 기기(E8)에서는 모델이 **다른 코어의 펌웨어**에 묶이기도 한다.

- **NPU용 컴파일 결과**: QNN context binary 같은 사전 컴파일된 그래프는 특정 SDK·NPU 펌웨어 버전과 짝인 경우가 많다고 알려져 있다(F4, hedge). NPU 펌웨어가 바뀌면 모델을 다시 컴파일해 보내야 하고, 반대로 새 모델이 새 NPU 기능을 쓰면 NPU 펌웨어를 먼저 올려야 한다.
- **DSP의 전처리**: 전처리가 audio DSP 펌웨어 안에 있으면, 전처리 v4를 쓰는 모델은 DSP 펌웨어 업데이트를 요구한다(예제 1의 preproc v4 행).
- **IPC 프로토콜**: 모델 출력을 MCU → AP로 넘기는 메시지 형식(E8 §3.7)에 클래스 수가 박혀 있으면, 클래스를 늘린 모델은 양쪽 펌웨어를 바꿔야 한다.

규칙은 E8 §6.3과 같다. **호환성 매트릭스를 이미지 세트 전체에 대해** 관리하고(AP OS, DSP, NPU 펌웨어, MCU, 모델 번들), "어느 순서로 올려도 중간 상태가 동작하는가"를 설계 리뷰에서 확인한다. 순서를 보장할 수 없으면 관련 이미지를 **한 OTA 패키지로 묶어** A/B 전체를 함께 바꾼다(Android의 A/B 시스템 업데이트처럼 파티션 세트를 통째로 전환하는 방식).

### 7.4 함정

- **threshold를 서버 config로 따로 보냄**: H8의 원격 config는 편하지만, threshold를 거기 두면 모델과 config가 서로 다른 rollout 일정으로 움직인다. 모델 버전별로 threshold를 키잉하거나(`threshold[model_version]`), 번들 안에 둔다.
- **전처리 상수 하드코딩**: `#define N_MEL 40`이 펌웨어에 있으면, 64 mel 모델은 shape 검사에서 걸리면 다행이고, shape가 우연히 맞으면 조용히 틀린다.
- **롤백이 모델만**: 롤백 경로도 번들 단위로 테스트한다(예제 10 마지막 행).

---

## 8. SSD 펌웨어 활성화와 비교 — Don의 멘탈 모델

NVMe의 펌웨어 업데이트는 두 명령으로 나뉜다(명령 이름과 Commit Action 값은 NVMe Base Specification 기준, 세부는 버전에 따라 다르다).

- **Firmware Image Download** (admin): 이미지를 offset·길이로 나눠 컨트롤러 버퍼로 보낸다. 조각 크기·정렬은 Identify Controller의 FWUG(Firmware Update Granularity)를 따른다.
- **Firmware Commit** (admin, 예전 이름 Firmware Activate): 받은 이미지를 어느 **slot**(1~7, slot 1은 읽기 전용일 수 있음 — Identify Controller의 FRMW 필드)에 넣고 언제 활성화할지 **Commit Action**으로 정한다.

| Commit Action | 의미 | 모델 OTA의 대응 |
|---|---|---|
| 000b | 이미지를 slot에 저장, 활성화 안 함 | B 슬롯에 쓰고 검증까지 (①~③) |
| 001b | 저장하고 **다음 reset에** 활성화 | pending 표시 (④), 안전 지점에 전환 |
| 010b | 이미 있는 slot을 다음 reset에 활성화 | 다운로드 없는 롤백 — A 슬롯을 다시 active로 |
| 011b | 저장하고 **reset 없이 즉시** 활성화 | 추론 사이 안전 지점에서 핫스왑 (4.2절) |

reset 없는 즉시 활성화는 컨트롤러가 진행 중인 I/O를 정리할 수 있어야 가능하고, 최대 활성화 시간(MTFA)을 넘기면 안 된다. 그래서 컨트롤러는 "이 이미지는 Conventional Reset / NVM Subsystem Reset / Controller Level Reset이 필요하다" 같은 상태를 돌려줄 수 있다. 모델 OTA에서도 같은 질문이 나온다 — **이 변경은 핫스왑으로 되나, 재부팅이 필요한가**. 모델만 바뀌면 핫스왑(추론 사이), 펌웨어(런타임·전처리)가 바뀌면 재부팅이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg"><defs><marker id="j5n" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="20" y="22" font-size="13">NVMe 펌웨어 업데이트 ↔ 모델 OTA — 같은 뼈대</text><text x="20" y="52" font-size="12">NVMe SSD</text><text x="20" y="182" font-size="12">모델 OTA</text><rect x="100" y="36" width="130" height="50" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="165" y="57" font-size="12" text-anchor="middle">Image Download</text><text x="165" y="75" font-size="12" text-anchor="middle">offset · FWUG 조각</text>
<line x1="230" y1="61" x2="248" y2="61" stroke="currentColor" marker-end="url(#j5n)"/><rect x="250" y="36" width="130" height="50" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="315" y="57" font-size="12" text-anchor="middle">Commit (slot, CA)</text><text x="315" y="75" font-size="12" text-anchor="middle">검증 후 slot 기록</text><line x1="380" y1="61" x2="398" y2="61" stroke="currentColor" marker-end="url(#j5n)"/><rect x="400" y="36" width="120" height="50" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="460" y="57" font-size="12" text-anchor="middle">reset / 즉시</text><text x="460" y="75" font-size="12" text-anchor="middle">활성화</text>
<line x1="520" y1="61" x2="538" y2="61" stroke="currentColor" marker-end="url(#j5n)"/><rect x="540" y="36" width="120" height="50" rx="6" fill="#888" fill-opacity="0.2" stroke="currentColor"/><text x="600" y="57" font-size="12" text-anchor="middle">새 FW 부팅</text><text x="600" y="75" font-size="12" text-anchor="middle">실패 시 이전 slot</text><rect x="100" y="166" width="130" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="165" y="187" font-size="12" text-anchor="middle">재개 가능 다운로드</text><text x="165" y="205" font-size="12" text-anchor="middle">offset · chunk hash</text><line x1="230" y1="191" x2="248" y2="191" stroke="currentColor" marker-end="url(#j5n)"/>
<rect x="250" y="166" width="130" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="315" y="187" font-size="12" text-anchor="middle">검증 + pending</text><text x="315" y="205" font-size="12" text-anchor="middle">B 슬롯, tries</text><line x1="380" y1="191" x2="398" y2="191" stroke="currentColor" marker-end="url(#j5n)"/><rect x="400" y="166" width="120" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="460" y="187" font-size="12" text-anchor="middle">안전 지점</text><text x="460" y="205" font-size="12" text-anchor="middle">핫스왑 / 재부팅</text><line x1="520" y1="191" x2="538" y2="191" stroke="currentColor" marker-end="url(#j5n)"/>
<rect x="540" y="166" width="120" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="600" y="187" font-size="12" text-anchor="middle">self-test → 확정</text><text x="600" y="205" font-size="12" text-anchor="middle">실패 시 A로</text><line x1="165" y1="86" x2="165" y2="164" stroke="currentColor" stroke-dasharray="3 3"/><line x1="315" y1="86" x2="315" y2="164" stroke="currentColor" stroke-dasharray="3 3"/><line x1="460" y1="86" x2="460" y2="164" stroke="currentColor" stroke-dasharray="3 3"/><line x1="600" y1="86" x2="600" y2="164" stroke="currentColor" stroke-dasharray="3 3"/>
<text x="20" y="246" font-size="12">모델 OTA에만 있는 것: 호환성 협상(op·ABI·전처리), 번들 원자성(threshold), golden self-test,</text><text x="20" y="266" font-size="12">그리고 "부팅은 되는데 결과가 나쁜" 실패를 잡는 fleet 통계 판정 (단계적 배포, 대조군, 쪼개 보기)</text><text x="20" y="286" font-size="12">SSD에 대응하는 것: 호스트 쪽 qualification(고객사 인증)과 fleet 펌웨어 롤아웃 정책</text></svg>
```

그림 9 — NVMe 펌웨어 업데이트(위)와 모델 OTA(아래)의 대응. 점선은 같은 역할의 단계다.

대응을 말로 정리하면:

| SSD 펌웨어 (Don이 아는 것) | 모델 OTA (새로 배우는 것) |
|---|---|
| FW slot 1~7, Firmware Slot Information log page | 모델 슬롯 A/B + 메타 (active, pending, tries, rejected) |
| Download는 언제든, Commit이 결정 | 다운로드·검증은 언제든, pending 표시가 결정 |
| Commit Action 001b vs 011b (reset 필요 vs 즉시) | 재부팅 전환 vs 추론 사이 핫스왑 |
| 이미지 서명 검증, 실패 시 거부 | 패키지 서명 + 호환성 검사(ABI·op·arena·전처리) |
| PLP: ack한 데이터는 잃지 않는다 | 메타 ping-pong: 어디서 끊겨도 검증된 슬롯으로 부팅 |
| 고객사 qualification, 펌웨어 버전별 이슈 추적 | 단계적 배포, 대조군, HW rev별 판정 |
| 기능 버그 → 시끄러운 실패 | 품질 저하 → **조용한 실패** (통계로만 보임) |

면접에서 이 대응을 한 문장으로 쓰면 강하다: "SSD에서 download와 commit/activate를 분리하고, slot을 여러 개 두고, reset 종류에 따라 활성화 시점을 고르던 것을 모델에 그대로 적용합니다. 모델에서 추가되는 것은 호환성 협상, threshold까지 묶는 번들 원자성, 그리고 조용한 품질 저하를 잡는 통계적 rollout입니다."

---

## 9. 임베디드 관점에서 다시 보기 — 한 장짜리 설계안

Hark 같은 웨어러블(추정: always-on MCU에서 KWS, AP에서 큰 모델)이라면, 모델 업데이트 설계를 한 장으로 이렇게 쓸 수 있다. 숫자는 가정이다.

| 항목 | 결정 | 근거 |
|---|---|---|
| 업데이트 단위 | 번들(모델 + 전처리·후처리 파라미터 + labels + golden) 하나에 버전 하나 | §7, 예제 10 |
| 저장 (MCU) | 모델 전용 A/B 파티션 각 256 KB + 메타 2 섹터 ping-pong | §4.1, 예제 6, I1 §4 |
| 저장 (AP) | 파일 두 벌 + 원자적 rename/심볼릭 링크, 또는 A/B 파티션 | 같은 원리 |
| 실행 | MCU는 flash XIP에서 직접 (direct-XIP 방식), 포인터 전환 | §4.5 |
| 검증 | 헤더 CRC → 길이 → 서명(Ed25519, 공개키 2개) → HW·ABI·op·arena·전처리·secver → payload SHA-256 | §2.5, §5 |
| 활성화 | pending + tries 2, 다음 프레임 경계 또는 다음 유휴, streaming 상태 초기화 | §4.2 |
| self-test | 로드 + golden 8개 bit-exact + 원시 오디오 1초 end-to-end + 10분 무크래시 | §4.4 |
| 확정 | self-test 통과 시 active 전환, 그때 security 카운터 갱신 | §5.3 |
| 호환성 | 기기가 능력(ABI·op·arena·전처리·HW·secver)을 check-in에 보고, 서버가 선택 | §3 |
| 배포 | salt 넣은 hash cohort, dogfood → 1 → 5 → 25 → 50 → 100%, 단계당 48 h | §6 |
| 판정 | 즉시 halt: 롤백·self-test 실패·크래시. 통계: HW rev·FW·지역별 FA proxy, 배터리 | 예제 9 |
| 롤백 | 서명된 서버 명령 → A 슬롯으로 (다운로드 없음), 기기 자체 판단은 보수적으로 | §6.3 |
| delta | 서버가 크기 비교 후 선택, head-only 재학습일 때만 이득 | 예제 7 |

C 쪽에서 이것을 지키는 가장 싼 방법은 **헤더 struct와 펌웨어 능력 상수를 같은 소스에서 생성**하는 것이다. 예를 들어 빌드 스크립트가 op resolver에 등록한 op 목록에서 `OPS_SUPPORTED`를, 링커 맵에서 `ARENA_AVAIL`을 만들어 헤더에 넣고, 같은 값을 check-in 메시지에도 쓴다. 손으로 두 곳에 쓰면 언젠가 어긋난다. `_Static_assert(sizeof(mpk_hdr_t) == 128, …)`처럼 레이아웃도 컴파일 때 고정한다(예제 4).

---

## 10. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 슬롯 하나에 덮어쓰기 | 업데이트 중 배터리 방전 후 KWS가 영영 안 됨 | 옛 모델을 지운 뒤 새 모델 완성 전 차단 | A/B 슬롯 (예제 6: 85/85 → 0) |
| 메타 사본 하나 | 드물게 업데이트 후 모델이 사라짐 | 메타 erase·program 도중 차단 | 두 사본 + seq + CRC (예제 6 `-DSINGLE_META`) |
| tries를 시험 후 감소 | 새 모델 배포 후 일부 기기 boot loop | 크래시하는 모델로 계속 재시도 | 감소·저장 후 시험 |
| 설치 전 호환성 검사 없음 | `AllocateTensors()` 실패, 옛 모델은 이미 내림 | op·arena 요구를 헤더에 안 둠 | 헤더에 ops_mask·arena, 설치 전 거부 (예제 4) |
| threshold를 모델과 따로 배포 | 업데이트 후 "혼자 깨어남" 민원 폭주 | 새 모델 + 옛 threshold | 번들 원자성 (예제 10: FA 0.5 → 93/h) |
| 합산 지표로만 판정 | 특정 HW rev 사용자만 불만, 대시보드는 정상 | 희석 (15% × 3배 → 1.3배) | 위험 축별 판정 (예제 9) |
| fuse를 시험 활성화 때 태움 | self-test 실패 후 롤백 불가, 기능 정지 | 옛 모델이 security 카운터보다 낮아짐 | 확정 후에만 카운터 갱신 (예제 8) |
| HMAC 키를 기기에 | 한 대 분석으로 fleet 전체에 위조 모델 가능 | 대칭 키 | 비대칭 서명, 기기엔 공개키만 |
| delta를 항상 사용 | 재학습 릴리스에서 다운로드가 더 커짐 | 가중치가 거의 전부 바뀜 | 서버가 크기 비교 후 선택 (예제 7) |
| 펌웨어가 옛 전처리 제거 | 모델 롤백 버튼을 눌렀는데 거부됨 | 롤백 대상 모델이 요구하는 전처리 없음 | 펌웨어 N은 모델 N−1 요구사항 유지 (예제 5) |

---

## 11. 면접에서 이렇게 말한다

**Q.** "Design an OTA mechanism for on-device models."

**A.** 번들(모델 + 전처리·후처리 파라미터 + golden vector)을 버전 하나로 묶고, 헤더에 런타임 ABI·op 집합·arena·전처리 버전·HW ID·보안 버전을 넣어 서명한다. 기기는 check-in 때 능력을 보고하고 서버가 호환되는 최신 번들을 고른다. 기기는 비활성 슬롯에 재개 가능하게 받고, readback hash와 서명·호환성을 검증한 뒤 pending으로 표시한다. 다음 안전 지점에 시도 횟수를 줄여 저장하고 시험 활성화, golden vector self-test 통과 시 확정, 실패·크래시 시 이전 슬롯으로 롤백한다. 배포는 hash cohort로 단계적으로, HW rev별 지표로 자동 halt한다.

> I'd ship a versioned bundle — model, pre/post-processing parameters, labels and golden vectors — with a header declaring runtime ABI, required op set, arena size, preprocessing version, target hardware and a security version, all signed with an asymmetric key. Devices report their capabilities at check-in and the server picks the newest compatible bundle. The device downloads resumably into the inactive slot, re-reads it from flash to verify the hash, checks the signature and compatibility, then marks it pending. At the next safe point — between inferences — it decrements a persistent try counter, activates the new model on trial, runs golden-vector self-tests, and either confirms or falls back to the old slot. Rollout is staged by deterministic hash cohorts with automatic halt on per-hardware-revision metrics.

**Q.** "How do you guarantee a power loss during model update doesn't brick the feature?"

**A.** 옛 모델을 절대 먼저 지우지 않는다 — A/B 슬롯. "어느 슬롯이 유효한가"를 기록하는 메타데이터도 두 사본에 seq와 CRC를 두고 번갈아 쓴다. 순서는 새 슬롯 무효화 → 쓰기 → readback 검증 → pending 표시 → 시도 횟수를 먼저 줄여 저장 → 시험 → 확정. 어디서 끊겨도 부팅은 유효한 메타가 가리키는 검증된 슬롯으로 간다. 시뮬레이션에서 모든 flash 연산(93~95개)마다 끊어 봤고 벽돌 0, 덮어쓰기 방식은 85/85 실패, 메타 사본 하나면 8번 실패였다. 실기기에서는 전원 차단 지그로 반복 시험한다.

> Never erase the only good copy: keep A/B slots, and make the metadata that says which slot is valid itself power-safe — two copies with a sequence number and CRC, written alternately. The order is invalidate the target slot, write, read back and hash, mark pending, then on the next boot decrement and persist the try counter before running the new model, and only confirm after self-test. A cut at any point leaves the latest valid metadata pointing at a verified slot. I simulated a cut during every flash operation of the flow and never bricked; overwrite-in-place lost the model on every cut, and a single metadata copy bricked whenever the cut hit a metadata write. On hardware I'd back this with a power-cut jig running thousands of cycles.

**Q.** "When do you need a firmware update instead of a model update?"

**A.** 런타임이 모르는 op를 쓸 때, 변환기 스키마·런타임 ABI가 바뀔 때, 필요한 arena가 펌웨어의 정적 arena를 넘을 때, 전처리 코드가 바뀔 때, 모델이 슬롯보다 클 때, 그리고 NPU 컴파일 결과가 특정 NPU 펌웨어·SDK에 묶여 있을 때. 반대로 가중치만 바뀌는 재학습, 같은 op 안에서 층을 넓히는 것은 모델만으로 된다. 그래서 펌웨어에 op·arena·슬롯 여유를 미리 두고, 전처리 숫자를 파라미터로 빼서 모델 팀이 펌웨어 일정에 덜 묶이게 한다.

> Whenever the model needs something the firmware doesn't have: a new operator kernel, a newer converter schema or runtime ABI, more arena than the statically reserved one, a different preprocessing algorithm implemented in firmware, more flash than the model slot, or an NPU binary tied to a specific NPU firmware or SDK version. Pure retraining, or widening layers within the existing op set and memory budget, is model-only. So I design firmware with headroom — extra ops, arena and slot margin — and parameterize preprocessing, so the model team can ship without waiting for a firmware release.

**Q.** "How do you roll out a new wake-word model safely?"

**A.** threshold와 smoothing을 모델과 한 번들로 묶는다 — 새 모델에 옛 threshold면 오탐이 수십~수백 배가 될 수 있다. 먼저 shadow mode로 행동 없이 불일치를 보고, dogfood → 1% → 5% → 25% → 50% → 100%로 넓히며, 대조군과 비교한 오탐 proxy(깨어난 뒤 무발화 비율), 놓침 proxy(재발화), 배터리, 크래시를 HW rev·마이크·지역별로 쪼개 본다. 합산만 보면 15% 기기의 3배 문제가 1.3배로 희석돼 놓친다. 기준을 넘으면 자동 halt, 롤백은 기기에 남은 이전 슬롯으로 다운로드 없이 한다.

> First, the threshold and smoothing parameters ship in the same atomic bundle as the model — a new model with the old threshold can multiply false wakes by orders of magnitude. I'd run it in shadow mode to measure disagreement without acting, then roll out by hash cohort from dogfood to 1, 5, 25, 50 and 100 percent, comparing against a concurrent control group: false-wake proxies, re-try rates, battery drain and crashes, split by hardware revision, microphone vendor and locale, because a 3x regression on 15% of devices shows up as only 1.3x in the pooled number. Clear thresholds trigger automatic halt, and rollback is just flipping back to the previous slot that's still on the device.

**Q.** "What goes in a model package header?"

**A.** 구조(magic, 헤더 포맷 버전, 헤더 길이, 헤더 CRC), 정체(모델 ID, semver), 호환성(런타임 ABI, 필요한 op 비트마스크, arena 요구량, 전처리 버전, 대상 HW ID), 보안(security version), 인터페이스(입력·출력 텐서의 dtype·shape·scale·zero point), 무결성(payload 길이와 SHA-256, metadata 길이). 서명은 헤더 + payload + metadata 전체에. 크기를 16의 배수로 고정해 payload 정렬을 지키고, header_len으로 확장 여지를 둔다.

> Structure: magic, header format version, header length and a header CRC. Identity: model ID and semantic version. Compatibility: runtime ABI, a bitmask of required operators, arena size, preprocessing version and target hardware ID. Security: a monotonic security version for anti-rollback. Interface: input and output tensor specs including dtype, shape, scale and zero point, so the firmware quantizes features correctly. Integrity: payload and metadata lengths plus the payload SHA-256. A signature covers header, payload and metadata. I keep the header a fixed multiple of 16 bytes so the payload stays aligned, and carry the header length so it can grow.

**Q.** "Model updates are frequent. How do you keep the compatibility matrix from exploding?"

**A.** 기기가 능력을 직접 보고하게 해서 서버가 펌웨어 버전 표를 갖지 않게 하고, 실제로 보내는 조합 + 롤백 조합만 테스트한다. 펌웨어 N이 모델 N−1 요구사항을 한 세대는 유지하고, 오래된 펌웨어는 모델 개선을 받으려면 펌웨어부터 올리게 한다. 대시보드에 "호환 최신 모델을 못 받는 기기 수"를 띄워 펌웨어 업데이트 압력을 숫자로 관리한다.

> Devices report capabilities rather than just a version string, so the server matches requirements against capabilities instead of maintaining a hand-written table. We test the pairs we actually serve plus the rollback pairs, and keep a rule that firmware N still supports what model N−1 needs. Devices on very old firmware simply stay on the last compatible model until they update firmware, and the dashboard tracks how many devices are stuck behind that boundary.

---

## 12. 직접 해보기

1. 예제 6에서 슬롯 크기 6 sector, 이미지 20,000 B, page 256 B일 때 정상 경로의 flash 연산 수 93을 손으로 다시 세어라. 이미지가 30,000 B라면? 정답: 슬롯 쓰기 = 6 + ⌈30000/256⌉ = 6 + 118 = 124 (슬롯이 24,576 B라 실제로는 들어가지 않는다 — sector를 8개로 늘려야 하고 그러면 8 + 118 = 126), 메타 쓰기 연산 8개(①④⑤⑦ × 2)를 더해 134.
2. 예제 9에서 rev C가 전체의 5%이고 오탐이 3배라면 합산 비율은? halt 기준 1.5를 넘나? 정답: 0.95 + 0.05 × 3 = 1.10. 넘지 못한다 — 작은 하위 집단일수록 합산 판정은 더 무력하다.
3. 예제 8에서 fuse 32비트, 보안 릴리스가 분기마다 한 번이면 몇 년을 버티나? 모델 재학습(2주마다)마다 올리면? 정답: 32분기 = 8년. 2주마다면 32 × 2주 ≈ 1.2년 — 그래서 security_version은 보안 수정에만 올린다.
4. 코드 과제: 예제 2·4에 "필요 op를 비트마스크가 아니라 이름 목록으로 metadata에 두고, 거부 메시지에 빠진 op 이름을 출력"하도록 고쳐라. 힌트: `required & ~supported`의 각 비트를 op 이름 표로 찾으면 된다 — 거부 텔레메트리에 op 이름이 있으면 펌웨어 팀이 바로 행동할 수 있다.
5. 코드 과제: 예제 6에 "다운로드 재개"를 넣어라 — 메타에 `written_pages`를 두고, 끊겼다 다시 시작하면 그 page부터 쓴다. 모든 연산에서 끊어도 벽돌 0이 유지되는지, 총 연산 수가 얼마나 줄어드는지 확인하라. 힌트: `written_pages` 갱신이 메타 쓰기라 flash 수명과 맞바꾼다 — 매 page가 아니라 sector마다 기록한다.
6. 코드 과제: 예제 7에 "per-channel scale"(채널 64개)을 넣고 미세조정 + scale 재계산 경우의 delta 크기를 다시 재라. 힌트: 가장 큰 가중치가 속한 채널 하나의 scale만 바뀌므로 바뀐 바이트가 크게 줄어야 한다.

---

## 13. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| OTA (over-the-air) | 무선 업데이트 | 네트워크로 펌웨어·모델을 내려받아 교체 |
| data flywheel | 데이터 순환 | 필드 데이터 → 재학습 → 배포 → 더 많은 데이터 |
| bundle | 번들 | 모델 + 전처리·후처리 파라미터 + labels + golden vector를 한 버전으로 |
| runtime ABI | 런타임 계약 번호 | 이 payload를 해석하는 데 필요한 스키마·커널 의미의 버전 |
| ops mask | op 비트마스크 | 필요한(또는 지원하는) op 집합, 부분집합 검사를 AND 한 번으로 |
| capability negotiation | 능력 협상 | 기기가 능력을 보고하고 서버가 호환되는 것을 고름 |
| A/B slot | 이중 슬롯 | 새 것을 비활성 슬롯에 쓰고 옛 것을 남겨 롤백·전원 차단에 대비 |
| pending / trial / confirm | 시험 대기 / 시험 / 확정 | 새 슬롯을 시도해 보고, 통과하면 영구 전환 |
| tries | 시도 횟수 | 시험 전에 줄여 저장 — 0이면 자동 롤백 |
| ping-pong metadata | 교대 메타 | 두 사본을 번갈아 써서 쓰기 도중 차단에도 하나는 유효 |
| safe point | 안전 지점 | 추론 중이 아닌, 모델을 바꿔도 되는 순간 |
| readback verify | 재독 검증 | flash에서 다시 읽은 바이트로 hash 확인 |
| golden vector | 기준 테스트 벡터 | 기준 모델이 낸 입력·출력 쌍, self-test에 사용 (C8 §5) |
| direct-XIP | 슬롯 직접 실행 | 두 슬롯 중 유효한 쪽에서 바로 실행, 복사·swap 없음 |
| delta update | 차분 업데이트 | 옛 이미지와의 차이만 보냄 (bsdiff 등) |
| HMAC | 키 있는 hash | 대칭 키 MAC — 이 노트에서는 서명의 대용 |
| Ed25519 · ECDSA | 비대칭 서명 | 비밀키로 서명, 공개키로 검증 — 기기엔 공개키만 |
| security version | 보안 버전 | 단조 카운터와 비교해 downgrade 공격을 막는 번호 |
| anti-rollback | 롤백 방지 | 서명은 정상인 옛 취약 버전 설치를 거부 |
| OTP fuse | 1회 기록 퓨즈 | 한 번 1이 되면 되돌릴 수 없는 비트, 카운터에 사용 |
| TOCTOU | 검사·사용 시점 차이 | 검사한 뒤 내용이 바뀌는 공격 |
| canary / cohort | 선발대 / 집단 | 먼저 받는 소수 기기 / hash로 정한 기기 묶음 |
| holdout | 대조 유지군 | 100% 이후에도 옛 모델에 남겨 두는 비교용 기기 |
| shadow mode | 그림자 실행 | 새 모델을 함께 돌리되 결과로 행동하지 않음 |
| Commit Action | NVMe 활성화 방식 | 저장만 / 다음 reset에 / 즉시 활성화 선택 |

---

## 14. 요약 & 체크리스트

모델은 펌웨어보다 자주, 그리고 **조용히** 실패한다. 그래서 모델 업데이트는 두 가지를 동시에 보장해야 한다. 첫째, **기기가 벽돌이 되지 않는다**: 모델·전처리·후처리·golden vector를 한 번들로 묶고, 헤더에 런타임 ABI·op 집합·arena·전처리 버전·HW ID·보안 버전·텐서 spec을 넣어 서명하고, 기기는 서명을 먼저 검사한 뒤 호환성을 확인하고, 비활성 슬롯에 쓰고 다시 읽어 검증하고, ping-pong 메타에 pending을 표시하고, 시도 횟수를 먼저 줄여 시험하고, golden self-test 후 확정하거나 롤백한다(예제 6: 모든 연산에서 끊어도 벽돌 0). 둘째, **기능이 나빠지지 않는다**: 기기가 능력을 보고해 서버가 호환 모델만 보내고, hash cohort로 단계적으로 넓히고, 대조군과 비교한 지표를 위험 축별로 쪼개 자동 halt하며(예제 9: 합산은 1.3배로 놓치고, rev별은 1%에서 잡는다), threshold는 모델과 함께 움직인다(예제 10: 짝이 어긋나면 FA 187배). Don의 NVMe download → commit → activate 경험이 뼈대이고, 새로 더해지는 것은 호환성 협상, 번들 원자성, 통계적 rollout이다.

- [ ] 주어진 변경이 "모델만 / 펌웨어 + 모델 / 파라미터만"인지 여섯 가지 규칙으로 판정할 수 있다
- [ ] 모델 패키지 헤더의 필드를 나열하고 각 필드를 누가 언제 검사하는지 설명할 수 있다
- [ ] "최소 파싱 → 서명 → 의미 검사" 순서와 길이 overflow·TOCTOU 함정을 설명할 수 있다
- [ ] 펌웨어 × 모델 호환 매트릭스를 그리고, 테스트해야 할 조합(보내는 조합 + 롤백 조합)을 고를 수 있다
- [ ] A/B 슬롯 상태 기계를 그리고, 각 단계에서 전원이 끊기면 다음 부팅이 어디로 가는지 말할 수 있다
- [ ] tries를 시험 전에 줄이는 이유와 메타를 두 사본으로 두는 이유를 숫자(예제 6)로 말할 수 있다
- [ ] 미세조정·head-only·재학습에서 delta 업데이트의 이득을 예측하고 양자화 scale의 영향을 설명할 수 있다
- [ ] CRC·hash·HMAC·비대칭 서명의 차이와 semver·security_version 분리를 설명할 수 있다
- [ ] wake-word threshold가 모델에 묶이는 이유를 FA/h·FRR로 설명할 수 있다
- [ ] 단계적 배포의 지표·대조군·halt 기준을 정하고, 합산 판정이 놓치는 경우를 손으로 계산할 수 있다

---

## 참고 자료

- MCUboot 문서 — 설계(swap, overwrite-only, direct-XIP, ram-load), image trailer, test/confirm, security counter: [docs.mcuboot.com](https://docs.mcuboot.com/)
- NVM Express Base Specification — Firmware Image Download, Firmware Commit, Firmware Slot Information log page: [nvmexpress.org/specifications](https://nvmexpress.org/specifications/)
- IETF RFC 9019 "A Firmware Update Architecture for Internet of Things" · RFC 9124 "A Manifest Information Model for Firmware Updates in IoT Devices" (SUIT): [rfc-editor.org/rfc/rfc9019](https://www.rfc-editor.org/rfc/rfc9019) · [rfc-editor.org/rfc/rfc9124](https://www.rfc-editor.org/rfc/rfc9124)
- The Update Framework (TUF) — 업데이트 저장소 보안, 키 역할 분리·순환: [theupdateframework.io](https://theupdateframework.io/) · 차량용 확장 Uptane: [uptane.org](https://uptane.org/)
- Android A/B (seamless) system updates · Android Verified Boot (rollback index): [source.android.com/docs/core/ota/ab](https://source.android.com/docs/core/ota/ab) · [android.googlesource.com/platform/external/avb](https://android.googlesource.com/platform/external/avb/)
- Colin Percival, bsdiff: [daemonology.net/bsdiff](https://www.daemonology.net/bsdiff/)
- RFC 8032 (EdDSA / Ed25519), RFC 2104 (HMAC), FIPS 180-4 (SHA-256): [rfc-editor.org/rfc/rfc8032](https://www.rfc-editor.org/rfc/rfc8032) · [rfc-editor.org/rfc/rfc2104](https://www.rfc-editor.org/rfc/rfc2104)
- TensorFlow Lite for Microcontrollers (op resolver, `AllocateTensors`): [github.com/tensorflow/tflite-micro](https://github.com/tensorflow/tflite-micro)
- 이 노트 세트: E8 §6, F7 §9.4, F8 §2, H1 §6, H2 §4, H7 §3, H8 §2~3·§7, I1 §4, I4, C8 §5, 다음 노트 J6(테스트 — golden vector, HIL, 정확도 회귀 CI)
