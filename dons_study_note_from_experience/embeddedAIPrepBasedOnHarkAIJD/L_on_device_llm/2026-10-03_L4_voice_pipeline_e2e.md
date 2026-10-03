# L4. 음성 파이프라인 end-to-end — VAD → ASR → LLM → TTS를 실제로 연결하고 지연을 쪼개 보기

> **이 노트를 다 읽으면**: 음성 비서 한 턴을 "말 끝 → endpoint → ASR final → TTFT → 첫 구절 → 첫 오디오(TTFA)"의 사건열로 정의하고 각 구간을 직접 잴 수 있다 · `say`로 만든 질의 10개와 잡음 4단계로 endpointing hangover, Whisper-tiny WER, llama-server streaming, TTS cold/warm을 이 Mac에서 측정하고 어느 단계가 TTFA를 지배하는지 숫자로 말할 수 있다 · 순차 실행과 streaming 겹치기(첫 구절부터 TTS)의 차이, barge-in 취소 경로와 에코 문제를 시뮬레이션으로 보여 줄 수 있다 · 같은 파이프라인이 웨어러블(MCU/DSP, NPU, 클라우드)로 가면 단계별로 무엇이 바뀌는지와 턴당 에너지를 가정과 함께 설명할 수 있다
> **JD 연결**: Hark는 음성 중심 AI 기기다 — "integrating small language models on device", "hybrid edge-LLM", "real-time performance", "latency, memory, power" — study_prep_list **L4**: VAD → wake word → ASR → LLM → TTS / 단계별 지연 예산 / barge-in / streaming ASR. 함께 닿는 행: **I1**(TTFA 예산), **I3**(cascade), **L2**(LLM 서비스·streaming·취소), **L3**(라우팅), **G4**(AEC)
> **Don 기준 난이도**: 상태기계, 링버퍼, 인터럽트로 작업 취소하기, 타임스탬프로 지연 쪼개기, 여러 단계 파이프라인의 병목 찾기는 이미 몸에 있다 / 새로 배울 것은 "알고리즘 지연(endpoint 대기)"이 계산 지연보다 클 수 있다는 감각, WER 재는 법, LLM token stream을 문장 조각으로 잘라 TTS와 겹치는 법, 에코가 barge-in을 망치는 방식
> **선행 노트**: B5 2·4·7절(VAD, CTC/RNN-T/Whisper 구조, TTS 3단 구조), G4(마이크·AEC), G5(STFT), I1 3절(TTFA 예산), I3(cascade, pre-roll), D5·D6(TTFT/TPOT, 알고리즘 지연 vs 계산 지연), F3(llama.cpp), L2(LLM 서비스와 streaming — 같은 시기에 작성). 이 노트는 그 내용을 **다시 설명하지 않고** 실제로 연결해서 잰다.

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 이 노트가 새로 하는 것

앞 노트들은 부품을 하나씩 봤다. B5는 VAD·KWS·ASR·TTS 모델 구조를, I1은 "TTFA 1000 ms 예산을 어떻게 나누나"를 **가정 숫자로** 계산했고, D5는 LLM의 TTFT·TPOT를, L2는 LLM 서비스의 streaming과 취소를 다뤘다. 그런데 실제 제품에서 사용자가 느끼는 것은 부품 하나의 속도가 아니라 **"내가 말을 끝내고 나서 기기가 대답을 시작하기까지"** 걸리는 시간 하나다.

펌웨어로 비유하면 이렇다. 지금까지는 DMA 엔진 하나, CRC 블록 하나, NAND 읽기 하나의 latency를 각각 쟀다. 이 노트는 **host 명령 하나가 들어와서 완료가 나가기까지 end-to-end trace**를 찍는 일이다. SSD 펌웨어에서 "평균 read latency가 왜 90 µs냐"를 따지면 결국 단계별 타임스탬프를 박고 막대그래프를 그려서 "제일 긴 막대"를 찾았다. 이 노트도 똑같다.

| 이미 본 곳 | 다룬 것 | 이 노트에서는 |
|---|---|---|
| B5 2절 | 에너지 VAD + 적응형 noise floor | 그 VAD에 **hangover endpointer**를 얹고 지연 vs 말 자름을 잰다 |
| B5 4.5절 | Whisper 구조, 30 s 창, tiny 39M | 실제 가중치로 **WER과 지연**을 잰다 (잡음 4단계) |
| I1 3절 | TTFA 예산을 가정 숫자로 나눔 | 같은 체인을 **이 Mac에서 실측**하고 예산과 비교 |
| D5·D6 | TTFT, TPOT, 알고리즘 지연 | llama-server SSE로 **token 도착 시각**을 직접 찍는다 |
| L2 | streaming, 취소, barge-in 개념 | 취소가 실제로 얼마나 빨리 서버를 비우는지 **잰다** |
| G4 | AEC 원리 | 에코가 있으면 barge-in VAD가 **어떻게 망가지는지** 시뮬레이션 |

### 0.2 실험 환경과 측정 원칙

- 기계: Apple M2 (8코어), macOS. Python은 노트 공용 `.venv` (numpy, scipy, torch, transformers).
- ASR: `openai/whisper-tiny` (HF 캐시, `local_files_only=True`), CPU와 MPS(Apple GPU).
- LLM: `llama-server` (F3에서 빌드한 llama.cpp) + **Qwen2.5-0.5B-Instruct Q4_K_M** GGUF, 127.0.0.1의 높은 포트, 기본 offload(이 빌드는 Metal backend 포함).
- TTS: macOS `say`(새 프로세스 = cold), 그리고 엔진을 켜 둔 채 쓰는 작은 Swift 도우미(warm). 둘 다 **온디바이스 신경망 TTS의 대역(proxy)**일 뿐이다. 음질·연산량이 실제 웨어러블 TTS와 같다는 뜻이 아니다.
- 모든 음성 파일은 `/private/tmp/claude-501/l4/`에 만든다.
- **측정 잡음 경고**: 이 노트를 쓰는 동안 같은 Mac에서 다른 작업(다른 llama-server 여러 개)이 돌았다. load average가 5에서 47까지 오르내렸다. 그래서 지연은 **3회 반복 중앙값**을 쓰고, 출력마다 load average를 같이 찍었다. 절대값보다 **단계 사이의 비율과 순서**를 보라. 6.4절에서 같은 코드를 다른 부하에서 돌려 어떻게 바뀌는지도 보여 준다.

공용 도우미 모듈은 다섯 개다. 노트의 예제는 모두 이것들을 import한다.

| 파일 | 역할 | 처음 나오는 곳 |
|---|---|---|
| `l4lib.py` | WAV 읽기/쓰기, frame 에너지, endpointer, WER | 1절, 2절 |
| `asrlib.py` | Whisper-tiny 로드와 전사 + 시간 측정 | 3절 |
| `llmlib.py` | llama-server SSE 클라이언트, phrase chunker | 4절 |
| `ttslib.py`, `ttsd.py` | `say` cold 합성, warm TTS 프로세스 | 5절 |
| `pipeline.py` | 한 턴 전체를 세 가지 모드로 실행 | 6절 |

### 0.3 한 턴의 타임라인 — 지연의 정의

먼저 무엇을 재는지 정확히 정의한다. 사건(event) 여섯 개를 시간축에 찍는다.

```svg
<svg viewBox="0 0 680 328" xmlns="http://www.w3.org/2000/svg"><text x="102.0" y="52.0" font-size="12" text-anchor="end">사용자</text><text x="102.0" y="82.0" font-size="12" text-anchor="end">VAD·endpoint</text><text x="102.0" y="112.0" font-size="12" text-anchor="end">ASR</text><text x="102.0" y="142.0" font-size="12" text-anchor="end">LLM</text><text x="102.0" y="172.0" font-size="12" text-anchor="end">TTS</text><text x="102.0" y="202.0" font-size="12" text-anchor="end">스피커</text><rect x="135.8" y="38.0" width="335.6" height="20.0" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="303.6" y="53.0" font-size="12" text-anchor="middle">질문 발화 (약 2.6 s)</text><rect x="135.8" y="68.0" width="335.6" height="20.0" fill="#888" fill-opacity="0.2" stroke="currentColor" stroke-width="0.5"/><text x="303.6" y="83.0" font-size="12" text-anchor="middle">말소리 감지 중 (onset 60 ms 뒤 SPEECH)</text><rect x="471.4" y="68.0" width="78.5" height="20.0" fill="#888" fill-opacity="0.6" stroke="currentColor" stroke-width="0.5"/><text x="510.6" y="83.0" font-size="11" text-anchor="middle">침묵 확인</text><rect x="549.9" y="98.0" width="26.7" height="20.0" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="576.6" y="128.0" width="6.3" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="582.9" y="128.0" width="47.4" height="20.0" fill="#e08a3c" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><rect x="595.8" y="158.0" width="6.6" height="20.0" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="602.4" y="188.0" width="62.6" height="20.0" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="633.6" y="203.0" font-size="11" text-anchor="middle">응답 재생 →</text><line x1="471.4" y1="30.0" x2="471.4" y2="222.0" stroke="#d0564a" stroke-width="1" stroke-dasharray="3 3"/><text x="471.4" y="236.0" font-size="11" text-anchor="middle">①</text><line x1="549.9" y1="30.0" x2="549.9" y2="234.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/><text x="549.9" y="248.0" font-size="11" text-anchor="middle">②</text><line x1="576.6" y1="30.0" x2="576.6" y2="246.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/><text x="576.6" y="260.0" font-size="11" text-anchor="middle">③</text><line x1="582.9" y1="30.0" x2="582.9" y2="222.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/><text x="582.9" y="236.0" font-size="11" text-anchor="middle">④</text><line x1="595.8" y1="30.0" x2="595.8" y2="234.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/><text x="595.8" y="248.0" font-size="11" text-anchor="middle">⑤</text><line x1="602.4" y1="30.0" x2="602.4" y2="246.0" stroke="#d0564a" stroke-width="1" stroke-dasharray="3 3"/><text x="602.4" y="260.0" font-size="11" text-anchor="middle">⑥</text><text x="10.0" y="318.0" font-size="12" text-anchor="start">① 말 끝 0 · ② endpoint 선언 608 · ③ ASR final 815 · ④ LLM 첫 token 864 · ⑤ 첫 구절 964 · ⑥ 첫 오디오 1015 ms</text><line x1="471.4" y1="282.0" x2="602.4" y2="282.0" stroke="#d0564a" stroke-width="2"/><polygon points="602.4,282 596.4,278 596.4,286" fill="#d0564a"/><text x="465.4" y="286.0" font-size="12" text-anchor="end">TTFA = 1015 ms (측정 중앙값, streaming)</text><line x1="135.8" y1="222.0" x2="665.0" y2="222.0" stroke="currentColor" stroke-width="1"/><line x1="213.3" y1="222.0" x2="213.3" y2="226.0" stroke="currentColor" stroke-width="1"/><line x1="342.3" y1="222.0" x2="342.3" y2="226.0" stroke="currentColor" stroke-width="1"/><line x1="471.4" y1="222.0" x2="471.4" y2="226.0" stroke="currentColor" stroke-width="1"/><line x1="600.5" y1="222.0" x2="600.5" y2="226.0" stroke="currentColor" stroke-width="1"/><text x="213.3" y="236.0" font-size="11" text-anchor="middle">−2 s</text><text x="342.3" y="236.0" font-size="11" text-anchor="middle">−1 s</text></svg>
```

그림 1 — 이 노트에서 측정한 한 턴(streaming 모드, 10개 질의 중앙값). 0은 사용자의 마지막 말소리가 끝난 순간이다. 회색 "침묵 확인" 막대가 가장 길다는 점이 이 노트 전체의 결론을 미리 보여 준다.

| 구간 | 정의 | 성격 (D6) | 이 노트의 측정값 (streaming, 중앙값) |
|---|---|---|---|
| endpoint 대기 | ① 말 끝 → ② "말이 끝났다" 선언 | **알고리즘 지연** — 침묵을 일정 시간 봐야 한다 | 608 ms (hangover 600 ms) |
| ASR final | ② → ③ 최종 전사 확정 | 계산 지연 (Whisper는 이때 처음 돈다) | 207 ms |
| LLM TTFT | ③ → ④ 첫 token 도착 | 계산 (prefill) + HTTP | 49 ms |
| 첫 구절 | ④ → ⑤ TTS에 넘길 만큼 token이 모임 | 계산 (decode K개) + chunker 규칙 | 100 ms |
| TTS 첫 버퍼 | ⑤ → ⑥ 첫 오디오 버퍼 준비 | 계산 + 엔진 상태(cold/warm) | 51 ms |
| **TTFA** | **① → ⑥** | 위의 합 (+ 실제 기기라면 출력 버퍼·DAC) | **1015 ms** |

용어 하나씩 풀어 둔다.

- **TTFA (time to first audio)**: 사용자가 말을 끝낸 순간부터 기기 스피커에서 응답의 첫 소리가 나기까지. 사용자가 실제로 느끼는 "반응 속도"다. I1에서 이것을 목표 p90 ≤ 1000 ms로 잡았다.
- **endpointing**: "말이 끝났다"를 판단하는 일. VAD(voice activity detection)는 frame마다 "말소리냐"만 답한다. endpointer는 그 위에 "침묵이 얼마나 길게 이어지면 끝으로 볼까"라는 **시간 규칙(hangover)**을 얹은 상태기계다.
- **ASR final**: streaming ASR은 말하는 도중에도 중간 결과(partial)를 내고, 끝나면 확정 결과(final)를 낸다. Whisper처럼 한 번에 처리하는 모델은 endpoint 이후에 처음 돌기 때문에 final까지의 시간이 통째로 지연이 된다.
- **TTFT (time to first token)**: LLM 요청을 보내고 첫 token이 오기까지 (D5). 주로 prompt prefill 시간이다.
- **첫 구절(first phrase)**: TTS는 한 token씩 읽을 수 없다. 억양 때문에 최소한 구절(문장 끝, 쉼표, 몇 단어) 단위가 필요하다. 그 구절이 완성되는 시각이다.

I1 3.2절의 예산과 나란히 놓으면 다음과 같다. I1은 Hark 같은 웨어러블을 가정한 **가정 숫자**, 오른쪽은 **이 Mac의 실측**이다.

| 단계 | I1 예산 (가정, p90) | 이 Mac 실측 (중앙값) | 비고 |
|---|---|---|---|
| endpoint | 300 | 608 | 이 노트는 hangover 600 ms를 썼다. 2절에서 왜 300이 위험한지 본다 |
| ASR final | 120 | 207 | Whisper-tiny는 streaming이 아니라 endpoint 뒤에 통째로 돈다 |
| TTFT + 첫 구절 | 120 + 200 | 49 + 100 | M2 GPU의 0.5B Q4는 웨어러블보다 훨씬 빠르다 |
| TTS 첫 청크 | 120 | 51 | warm 엔진 기준. cold면 1.6 s (5절) |
| 출력 버퍼 | 40 | (측정 안 함) | Mac에서는 실제 재생을 하지 않았다 |
| 합 | 900 + margin 100 | 1015 | **endpoint 하나가 60 %** |

---

## 1. 테스트 음성 만들기 — `say`로 질의 10개, 잡음 4단계

### 1.1 왜 직접 만드나

파이프라인을 재려면 **정답을 아는 입력**이 필요하다. 사람 녹음을 쓰면 말이 정확히 언제 끝났는지(정답 endpoint)를 손으로 표시해야 한다. 합성 음성을 쓰면 샘플 단위로 정확히 안다. 펌웨어 검증에서 실제 host 트래픽 대신 **알려진 패턴의 테스트 벡터**를 먼저 쓰는 것과 같다.

설계 원칙은 세 가지다.

- **목소리와 속도를 바꾼다**: Samantha, Daniel(영국 영어), Fred, Kathy, Ralph, 속도 160–230 wpm. 한 목소리에만 맞춘 튜닝(overfitting)을 피한다.
- **말 중간에 쉼을 넣는다**: `say`의 내장 명령 `[[slnc 450]]`은 450 ms 침묵을 넣는다. 사람은 "Set a timer for ten minutes… and remind me…"처럼 생각하며 쉰다. endpointer가 이걸 "말 끝"으로 착각하는지 보려는 것이다. "Um, …" 같은 filler 뒤의 긴 쉼(600 ms)도 넣었다.
- **앞 0.8 s, 뒤 2.0 s 침묵을 붙인다**: 앞 침묵은 noise floor 추정용, 뒤 침묵은 endpoint가 선언될 공간이다.

### 1.2 코드로 확인 — 질의 10개 합성 (예제 1)

무엇을 확인하는 코드인가: `say` → AIFF → `afconvert`로 16 kHz mono 16-bit WAV → 앞뒤 여백을 잘라 **정답 말소리 경계**를 샘플 단위로 기록한다.

```python
# 예제 1: say로 질의 10개를 합성 → 16 kHz mono WAV → 앞 0.8 s / 뒤 2.0 s 침묵 padding
import subprocess, json, numpy as np
from l4lib import D, SR, load, save
Q = [("Samantha", 180, "What's the weather like in San Francisco tomorrow?"),
     ("Daniel",   175, "Set a timer for ten minutes [[slnc 450]] and remind me to check the oven."),
     ("Fred",     170, "How many ounces are in a cup?"),
     ("Kathy",    200, "Call my mom [[slnc 300]] on her cell phone."),
     ("Ralph",    175, "Send a message to Alex saying I'm running late."),
     ("Samantha", 230, "Play some relaxing jazz music in the kitchen."),
     ("Daniel",   160, "Um, [[slnc 600]] what time does the pharmacy close today?"),
     ("Kathy",    185, "Translate good morning into Spanish."),
     ("Fred",     210, "Turn off the lights in the living room [[slnc 350]] please."),
     ("Samantha", 190, "What's on my calendar this afternoon?")]
meta = []
for i, (v, rate, text) in enumerate(Q):
    a, w = f"{D}/q{i}.aiff", f"{D}/q{i}_raw.wav"
    subprocess.run(["say", "-v", v, "-r", str(rate), "-o", a, text], check=True)
    subprocess.run(["afconvert", "-f", "WAVE", "-d", "LEI16@16000", "-c", "1", a, w], check=True)
    x = load(w)
    act = np.flatnonzero(np.abs(x) > 0.003)            # 실제 소리가 있는 샘플
    x = x[act[0]: act[-1] + 1]                          # say 앞뒤 여백 제거 → 정답 경계를 정확히
    y = np.concatenate([np.zeros(int(0.8 * SR)), x, np.zeros(int(2.0 * SR))])
    save(f"{D}/q{i}_clean.wav", y)
    ref = text.replace("[[slnc 450]] ", "").replace("[[slnc 300]] ", "") \
              .replace("[[slnc 600]] ", "").replace(" [[slnc 350]]", "")
    meta.append(dict(i=i, voice=v, rate=rate, ref=ref, start=0.8, end=0.8 + len(x) / SR))
    print(f"q{i} {v:8s} r{rate}  speech {len(x)/SR:4.2f} s  total {len(y)/SR:4.2f} s  | {ref}")
json.dump(meta, open(f"{D}/meta.json", "w"), indent=1)
```

```text
q0 Samantha r180  speech 2.58 s  total 5.38 s  | What's the weather like in San Francisco tomorrow?
q1 Daniel   r175  speech 3.99 s  total 6.79 s  | Set a timer for ten minutes and remind me to check the oven.
q2 Fred     r170  speech 1.89 s  total 4.69 s  | How many ounces are in a cup?
q3 Kathy    r200  speech 1.89 s  total 4.69 s  | Call my mom on her cell phone.
q4 Ralph    r175  speech 3.21 s  total 6.01 s  | Send a message to Alex saying I'm running late.
q5 Samantha r230  speech 1.95 s  total 4.75 s  | Play some relaxing jazz music in the kitchen.
q6 Daniel   r160  speech 3.30 s  total 6.10 s  | Um, what time does the pharmacy close today?
q7 Kathy    r185  speech 2.15 s  total 4.95 s  | Translate good morning into Spanish.
q8 Fred     r210  speech 2.47 s  total 5.27 s  | Turn off the lights in the living room please.
q9 Samantha r190  speech 1.85 s  total 4.65 s  | What's on my calendar this afternoon?
```

출력에서 볼 것: 질의 길이는 1.85–3.99 s로 실제 음성 비서 명령과 비슷하다. `meta.json`의 `end` 값(예: q1은 0.8 + 3.99 = 4.79 s)이 이후 모든 지연 계산의 **0점(①)**이다. (`l4lib.py`의 `load`/`save`는 scipy `wavfile`로 int16 ↔ float32 [−1, 1)을 바꾸는 두 줄짜리 함수다. `afconvert`가 붙이는 비표준 chunk 경고는 꺼 두었다.)

### 1.3 잡음 섞기 — SNR을 정확히 맞추기

**SNR(signal-to-noise ratio)**은 신호 전력과 잡음 전력의 비를 dB로 쓴 것이다.

```
SNR_dB = 10 · log10( P_signal / P_noise )
→ 원하는 SNR이 정해지면  P_noise = P_signal / 10^(SNR/10)
```

말로 하면: SNR 10 dB는 잡음 전력이 말소리 전력의 1/10, 0 dB는 둘이 같다는 뜻이다. 주의할 점은 P_signal을 **말소리가 있는 구간에서만** 재야 한다는 것이다. 앞뒤 침묵까지 포함해 평균을 내면 신호 전력이 작게 잡혀서 실제보다 잡음이 약하게 섞인다.

손으로 계산: q0의 말소리 구간 평균 전력이 −16 dBFS라고 하자(대략적인 값). SNR 20 dB를 원하면 잡음은 −36 dBFS, 0 dB면 −16 dBFS다. 아래 출력의 "앞 침묵 구간 noise floor"가 정확히 이 값(−35.8, −15.8)으로 나온다.

무엇을 확인하는 코드인가 (예제 2): white noise를 목표 SNR로 섞고, int16로 저장했다가 다시 읽어 SNR을 재측정한다.

```python
# 예제 2: 깨끗한 clip에 white noise를 목표 SNR로 섞고, 섞인 결과의 SNR을 다시 재서 확인
import json, numpy as np
from l4lib import D, SR, load, save
meta = json.load(open(f"{D}/meta.json"))
rng = np.random.default_rng(0)
for snr in (20, 10, 5, 0):
    got = []
    for q in meta:
        x = load(f"{D}/q{q['i']}_clean.wav")
        s, e = int(q["start"] * SR), int(q["end"] * SR)
        p_sig = np.mean(x[s:e] ** 2)                    # 말소리 구간 평균 전력
        p_noise = p_sig / 10 ** (snr / 10)              # SNR = 10·log10(Ps/Pn) 에서 Pn
        n = rng.standard_normal(len(x)) * np.sqrt(p_noise)
        save(f"{D}/q{q['i']}_snr{snr}.wav", x + n)
        y = load(f"{D}/q{q['i']}_snr{snr}.wav")         # int16로 저장한 뒤 다시 읽어서 측정
        nn = y - x
        got.append(10 * np.log10(p_sig / np.mean(nn ** 2)))
        if q["i"] == 0:
            nf = 10 * np.log10(np.mean(y[: int(0.8 * SR)] ** 2))
    print(f"목표 SNR {snr:2d} dB → 측정 {np.mean(got):5.2f} dB (10개 평균), "
          f"q0 앞 침묵 구간 noise floor {nf:6.1f} dBFS")
```

```text
목표 SNR 20 dB → 측정 20.00 dB (10개 평균), q0 앞 침묵 구간 noise floor  -35.8 dBFS
목표 SNR 10 dB → 측정 10.01 dB (10개 평균), q0 앞 침묵 구간 noise floor  -25.8 dBFS
목표 SNR  5 dB → 측정  5.00 dB (10개 평균), q0 앞 침묵 구간 noise floor  -20.7 dBFS
목표 SNR  0 dB → 측정  0.01 dB (10개 평균), q0 앞 침묵 구간 noise floor  -15.8 dBFS
```

출력에서 볼 것: 재측정 SNR이 목표와 0.01 dB 안에서 맞는다. 이제 질의 10개 × 조건 5개(clean, 20, 10, 5, 0 dB) = **50개 WAV**가 생겼다.

이 잡음 모델의 한계도 적어 둔다. white noise는 모든 주파수에 고르게 퍼진 "쉬—" 소리다. 실제 웨어러블이 듣는 잡음은 바람(저주파에 몰림), 사람들 말소리(babble — 말소리와 대역이 같아 VAD가 가장 어려워함), 옷 스침, 자기 발소리다. 여기서의 숫자는 **경향을 보는 용도**이고, 제품 튜닝은 실제 필드 녹음(H 모듈)으로 해야 한다.

---

## 2. VAD와 endpointing — hangover 하나가 지연의 절반

### 2.1 직관 — "상대가 말을 끝냈나?"

전화 통화에서 상대가 잠깐 말을 멈추면, 우리는 0.2초쯤 기다렸다가 끼어든다. 너무 빨리 끼어들면 "아직 안 끝났는데요"가 되고, 너무 오래 기다리면 어색한 침묵이 된다. 기계도 똑같은 문제를 푼다. 다만 기계는 문맥을 모르므로 보통 **"침묵이 H ms 이어지면 끝"**이라는 단순한 규칙을 쓴다. 이 H를 **hangover**(또는 end-of-speech timeout, trailing silence)라고 부른다.

펌웨어 비유: 버튼 debounce와 같다. 접점이 잠깐 떨어졌다고 바로 "놓음"으로 처리하면 채터링에 속는다. 그래서 "N ms 동안 계속 떨어져 있으면 놓음"으로 판단한다. N을 키우면 오검출은 줄지만 반응이 늦어진다. endpointing의 hangover가 정확히 이 trade-off다. 그리고 **이 대기 시간은 칩이 아무리 빨라도 줄지 않는다** — 순수한 알고리즘 지연이다(D6 6절).

### 2.2 정의 — 상태기계

B5 2.2절의 에너지 VAD(frame 에너지 > noise floor + margin이면 말소리, 조용한 frame에서만 floor를 천천히 갱신)를 그대로 쓰고, 그 위에 두 상태짜리 상태기계를 얹는다.

```
            말소리 frame이 onset(3)개 연속            
   ┌─────┐ ─────────────────────────────────▶ ┌────────┐
   │ SIL │                                     │ SPEECH │
   └─────┘ ◀───────────────────────────────── └────────┘
            침묵 frame이 hang/20 개 연속 → "endpoint 선언" (segment 하나 출력)
```

- frame = 20 ms (320 샘플). onset 3 frame = 60 ms 연속 말소리여야 시작으로 본다 (짧은 딸깍 소리 무시).
- SPEECH 상태에서 침묵 frame 수 `run`을 센다. 말소리 frame이 오면 0으로 리셋. `run`이 `hang/20`에 도달하면 endpoint.

말로 하면: **endpoint 시각 = 마지막 말소리 frame + hangover**다. 그래서 정답 말 끝에서 재면 endpoint 지연 ≈ hangover + (frame 양자화 0–20 ms)가 된다.

손으로 계산: hangover 600 ms, 마지막 말소리가 4.790 s에 끝났다면, 그 frame은 4.78–4.80 s이고 침묵 frame 30개(600 ms)가 지나는 5.40 s에 endpoint가 선언된다. 지연 = 5.40 − 4.79 = 610 ms. 6절 파이프라인의 endpoint 중앙값 608 ms가 바로 이것이다.

`l4lib.py`의 endpointer는 다음과 같다.

```python
def endpointer(db, hang_ms, margin_db=9.0, onset=3, floor_db=-60.0):
    """에너지 VAD + hangover endpointer. 반환: [(start_frame, end_frame), ...]
    end_frame = 침묵이 hang_ms 동안 이어져 '말 끝'을 선언한 frame (그 frame의 끝 시각)"""
    nf = np.min(db[:10])                         # 처음 200 ms로 noise floor 초기화
    hang = int(round(hang_ms / 20)); state, run, segs = "SIL", 0, []
    for k, e in enumerate(db):
        sp = e > max(nf + margin_db, floor_db)   # 이 frame이 말소리인가
        if not sp:
            nf = 0.95 * nf + 0.05 * e            # 조용한 frame에서만 floor를 천천히 따라감
        if state == "SIL":
            run = run + 1 if sp else 0
            if run >= onset:                     # 60 ms 연속 → 말 시작
                state, run, start = "SPEECH", 0, k - onset + 1
        else:
            run = 0 if sp else run + 1
            if run >= hang:                      # 침묵이 hangover만큼 → 말 끝 선언
                segs.append((start, k)); state, run = "SIL", 0
    return segs

def frame_db_band(x, lo=300, hi=3400):       # 말소리 대역(300–3400 Hz)만의 frame 에너지 (dB)
    n = len(x) // FRAME
    f = x[: n * FRAME].reshape(n, FRAME) * np.hanning(FRAME)
    P = np.abs(np.fft.rfft(f, axis=1)) ** 2
    k = np.fft.rfftfreq(FRAME, 1 / SR)
    return 10 * np.log10(P[:, (k >= lo) & (k <= hi)].sum(axis=1) / FRAME + 1e-10)
```

`frame_db_band`는 처음에 없던 함수다. 처음에 전 대역 에너지(`frame_db`)와 margin 9 dB로 해 보니 SNR 5 dB에서 10개 중 8개가 segment를 하나도 만들지 못했다. white noise는 0–8 kHz에 고르게 퍼져 있는데 말소리 에너지는 대부분 300–3400 Hz에 있다. **그 대역만 더하면 잡음의 절반 이상이 빠진다** (3.1 kHz / 8 kHz ≈ 39 %만 남음 → 약 4 dB 이득). 전화망이 300–3400 Hz만 쓰는 것과 같은 이유다. WebRTC VAD 같은 고전 VAD도 여러 주파수 대역의 에너지를 따로 본다. 단위는 FFT 크기에 따른 **상대 dB**이니 절대값(dBFS)으로 읽지 말 것.

### 2.3 한 clip을 눈으로 보기

```svg
<svg viewBox="0 0 680 310" xmlns="http://www.w3.org/2000/svg"><rect x="122.0" y="30.0" width="359.3" height="170.0" fill="#3f9a6b" fill-opacity="0.12" stroke="none"/><text x="126.0" y="42.0" font-size="12" text-anchor="start">정답 말소리 구간 0.80–4.79 s</text><polyline points="50.9,187.3 52.7,186.6 54.5,184.4 56.3,188.4 58.1,183.4 59.9,189.7 61.7,186.8 63.5,185.5 65.3,188.7 67.1,191.2 68.9,187.4 70.7,188.2 72.5,187.1 74.3,186.7 76.1,189.2 77.9,191.3 79.7,186.7 81.5,190.5 83.3,187.3 85.1,190.4 86.9,189.8 88.7,191.5 90.5,186.2 92.3,186.5 94.1,185.4 95.9,188.9 97.7,185.1 99.5,192.1 101.3,188.1 103.1,187.3 104.9,189.1 106.7,186.6 108.5,185.2 110.3,192.9 112.1,189.2 113.9,191.5 115.7,188.3 117.5,185.7 119.3,189.3 121.1,187.7 122.9,189.3 124.7,185.5 126.5,191.1 128.3,189.7 130.1,186.5 131.9,186.8 133.7,172.0 135.5,104.7 137.3,99.2 139.1,101.5 140.9,104.3 142.7,129.8 144.5,167.0 146.3,170.7 148.1,113.3 149.9,103.1 151.7,107.8 153.5,163.1 155.3,183.7 157.1,187.2 158.9,174.7 160.7,174.2 162.5,154.8 164.3,113.8 166.1,104.8 167.9,107.5 169.7,112.0 171.5,112.9 173.3,118.2 175.1,136.9 176.9,138.1 178.7,128.1 180.5,107.3 182.3,107.0 184.1,106.4 185.9,145.2 187.7,181.0 189.5,186.6 191.3,187.3 193.1,173.1 194.9,125.4 196.7,105.9 198.5,103.4 200.3,103.2 202.1,105.8 203.8,116.3 205.6,165.7 207.4,189.1 209.2,183.5 211.0,179.8 212.8,181.7 214.6,143.6 216.4,114.1 218.2,108.1 220.0,110.9 221.8,115.7 223.6,134.2 225.4,143.6 227.2,137.9 229.0,133.5 230.8,132.6 232.6,132.3 234.4,131.8 236.2,114.9 238.0,110.8 239.8,110.2 241.6,129.7 243.4,146.0 245.2,152.7 247.0,132.0 248.8,147.4 250.6,134.2 252.4,141.8 254.2,157.4 256.0,139.5 257.8,160.8 259.6,183.2 261.4,183.0 263.2,187.2 265.0,183.1 266.8,183.9 268.6,184.7 270.4,184.0 272.2,180.9 274.0,182.4 275.8,185.7 277.6,182.7 279.4,185.4 281.2,190.8 283.0,185.9 284.8,192.2 286.6,190.4 288.4,187.2 290.2,185.3 292.0,188.0 293.8,189.7 295.6,188.2 297.4,187.9 299.2,185.6 301.0,189.5 302.8,186.7 304.6,187.5 306.4,187.4 308.2,186.9 310.0,191.4 311.8,184.3 313.6,188.9 315.4,193.1 317.2,190.3 319.0,186.2 320.8,169.7 322.6,116.6 324.4,111.6 326.2,115.0 328.0,134.2 329.8,142.9 331.6,148.4 333.4,149.5 335.2,134.5 337.0,117.8 338.8,103.5 340.6,97.8 342.4,111.1 344.2,137.5 346.0,134.6 347.8,138.0 349.6,111.9 351.4,100.9 353.2,103.4 355.0,105.2 356.8,106.5 358.6,109.5 360.4,113.7 362.2,118.6 364.0,125.3 365.8,127.0 367.6,124.8 369.4,138.0 371.2,162.9 373.0,167.8 374.8,165.4 376.6,150.0 378.4,141.1 380.2,134.0 382.0,106.7 383.8,112.2 385.6,113.1 387.4,112.1 389.2,132.6 391.0,170.3 392.8,175.7 394.6,177.0 396.4,113.6 398.2,103.7 400.0,122.2 401.8,165.5 403.6,172.7 405.4,141.2 407.2,134.9 409.0,128.5 410.8,135.2 412.6,117.2 414.4,97.9 416.2,101.0 418.0,105.2 419.8,106.5 421.6,121.7 423.4,166.1 425.2,189.1 427.0,184.0 428.8,186.6 430.6,192.2 432.4,165.5 434.2,134.2 436.0,106.6 437.8,106.1 439.6,105.9 441.4,104.7 443.2,104.9 445.0,106.1 446.8,108.2 448.6,110.0 450.4,105.9 452.2,105.8 454.0,145.3 455.8,164.6 457.6,178.8 459.4,165.6 461.2,144.7 463.0,122.4 464.8,146.5 466.6,158.4 468.4,163.0 470.2,173.5 472.0,182.3 473.8,185.9 475.6,176.2 477.4,174.2 479.2,179.7 481.0,183.7 482.8,191.9 484.6,186.2 486.4,187.9 488.2,187.5 490.0,185.1 491.8,184.3 493.6,185.6 495.4,187.8 497.2,191.0 499.0,189.0 500.8,192.2 502.6,191.9 504.4,189.0 506.2,187.1 507.9,192.9 509.7,184.1 511.5,187.6 513.3,188.6 515.1,189.1 516.9,189.8 518.7,190.6 520.5,188.1 522.3,185.9 524.1,187.7 525.9,184.5 527.7,188.4 529.5,188.0 531.3,190.8 533.1,189.3 534.9,189.9 536.7,186.6 538.5,188.2 540.3,186.4 542.1,189.7 543.9,190.5 545.7,188.7 547.5,186.1 549.3,190.4 551.1,187.7 552.9,182.3 554.7,189.6 556.5,187.7 558.3,193.6 560.1,191.1 561.9,184.3 563.7,192.7 565.5,186.0 567.3,185.2 569.1,189.2 570.9,191.3 572.7,191.1 574.5,189.4 576.3,187.5 578.1,190.6 579.9,192.3 581.7,185.4 583.5,188.9 585.3,189.4 587.1,185.8 588.9,189.8 590.7,191.2 592.5,186.6 594.3,191.9 596.1,186.9 597.9,186.1 599.7,187.3 601.5,190.0 603.3,188.7 605.1,187.0 606.9,191.9 608.7,189.0 610.5,191.3 612.3,189.9 614.1,193.2 615.9,187.6 617.7,188.9 619.5,192.3 621.3,182.6 623.1,189.2 624.9,185.9 626.7,190.7 628.5,187.8 630.3,190.5 632.1,190.3 633.9,187.7 635.7,188.7 637.5,185.0 639.3,191.6 641.1,188.2 642.9,187.4 644.7,188.8 646.5,186.9 648.3,186.5 650.1,186.2 651.9,188.3 653.7,189.9 655.5,189.1 657.3,188.8 659.1,189.7" fill="none" stroke="#4a7bd0" stroke-width="1.2"/><polyline points="50.9,172.7 52.7,172.5 54.5,172.2 56.3,171.9 58.1,171.8 59.9,171.5 61.7,171.5 63.5,171.3 65.3,171.1 67.1,171.0 68.9,171.1 70.7,171.0 72.5,170.9 74.3,170.8 76.1,170.7 77.9,170.7 79.7,170.8 81.5,170.7 83.3,170.7 85.1,170.6 86.9,170.7 88.7,170.7 90.5,170.8 92.3,170.7 94.1,170.5 95.9,170.3 97.7,170.3 99.5,170.1 101.3,170.3 103.1,170.3 104.9,170.2 106.7,170.2 108.5,170.1 110.3,169.9 112.1,170.2 113.9,170.2 115.7,170.3 117.5,170.3 119.3,170.1 121.1,170.2 122.9,170.1 124.7,170.2 126.5,170.0 128.3,170.1 130.1,170.2 131.9,170.1 133.7,170.0 135.5,169.1 137.3,169.1 139.1,169.1 140.9,169.1 142.7,169.1 144.5,169.1 146.3,169.1 148.1,168.3 149.9,168.3 151.7,168.3 153.5,168.3 155.3,168.3 157.1,168.1 158.9,168.2 160.7,167.6 162.5,167.0 164.3,167.0 166.1,167.0 167.9,167.0 169.7,167.0 171.5,167.0 173.3,167.0 175.1,167.0 176.9,167.0 178.7,167.0 180.5,167.0 182.3,167.0 184.1,167.0 185.9,167.0 187.7,167.0 189.5,166.7 191.3,166.8 193.1,166.9 194.9,166.3 196.7,166.3 198.5,166.3 200.3,166.3 202.1,166.3 203.8,166.3 205.6,166.3 207.4,166.3 209.2,166.5 211.0,166.4 212.8,166.2 214.6,166.0 216.4,166.0 218.2,166.0 220.0,166.0 221.8,166.0 223.6,166.0 225.4,166.0 227.2,166.0 229.0,166.0 230.8,166.0 232.6,166.0 234.4,166.0 236.2,166.0 238.0,166.0 239.8,166.0 241.6,166.0 243.4,166.0 245.2,166.0 247.0,166.0 248.8,166.0 250.6,166.0 252.4,166.0 254.2,166.0 256.0,166.0 257.8,166.0 259.6,166.0 261.4,165.9 263.2,165.9 265.0,166.0 266.8,165.9 268.6,165.9 270.4,165.9 272.2,165.9 274.0,165.7 275.8,165.6 277.6,165.7 279.4,165.6 281.2,165.7 283.0,166.0 284.8,166.1 286.6,166.5 288.4,166.7 290.2,166.8 292.0,166.8 293.8,167.0 295.6,167.2 297.4,167.3 299.2,167.4 301.0,167.4 302.8,167.6 304.6,167.6 306.4,167.7 308.2,167.7 310.0,167.7 311.8,168.0 313.6,167.9 315.4,168.0 317.2,168.3 319.0,168.5 320.8,168.5 322.6,167.6 324.4,167.6 326.2,167.6 328.0,167.6 329.8,167.6 331.6,167.6 333.4,167.6 335.2,167.6 337.0,167.6 338.8,167.6 340.6,167.6 342.4,167.6 344.2,167.6 346.0,167.6 347.8,167.6 349.6,167.6 351.4,167.6 353.2,167.6 355.0,167.6 356.8,167.6 358.6,167.6 360.4,167.6 362.2,167.6 364.0,167.6 365.8,167.6 367.6,167.6 369.4,167.6 371.2,167.6 373.0,167.6 374.8,166.7 376.6,166.7 378.4,166.7 380.2,166.7 382.0,166.7 383.8,166.7 385.6,166.7 387.4,166.7 389.2,166.7 391.0,166.7 392.8,165.9 394.6,165.5 396.4,165.1 398.2,165.1 400.0,165.1 401.8,165.1 403.6,164.2 405.4,163.7 407.2,163.7 409.0,163.7 410.8,163.7 412.6,163.7 414.4,163.7 416.2,163.7 418.0,163.7 419.8,163.7 421.6,163.7 423.4,163.7 425.2,162.9 427.0,163.3 428.8,163.4 430.6,163.6 432.4,164.1 434.2,163.3 436.0,163.3 437.8,163.3 439.6,163.3 441.4,163.3 443.2,163.3 445.0,163.3 446.8,163.3 448.6,163.3 450.4,163.3 452.2,163.3 454.0,163.3 455.8,163.3 457.6,162.4 459.4,162.3 461.2,161.6 463.0,161.6 464.8,161.6 466.6,161.6 468.4,161.6 470.2,160.7 472.0,160.4 473.8,160.6 475.6,160.9 477.4,160.8 479.2,160.5 481.0,160.5 482.8,160.8 484.6,161.4 486.4,161.7 488.2,162.1 490.0,162.4 491.8,162.6 493.6,162.8 495.4,163.0 497.2,163.3 499.0,163.8 500.8,164.1 502.6,164.6 504.4,165.0 506.2,165.3 507.9,165.5 509.7,165.9 511.5,165.9 513.3,166.1 515.1,166.3 516.9,166.5 518.7,166.7 520.5,167.0 522.3,167.1 524.1,167.1 525.9,167.2 527.7,167.2 529.5,167.3 531.3,167.4 533.1,167.6 534.9,167.8 536.7,168.0 538.5,168.0 540.3,168.1 542.1,168.1 543.9,168.2 545.7,168.4 547.5,168.5 549.3,168.4 551.1,168.6 552.9,168.6 554.7,168.4 556.5,168.5 558.3,168.6 560.1,168.9 561.9,169.1 563.7,168.9 565.5,169.2 567.3,169.1 569.1,169.0 570.9,169.0 572.7,169.2 574.5,169.4 576.3,169.5 578.1,169.4 579.9,169.6 581.7,169.8 583.5,169.6 585.3,169.7 587.1,169.7 588.9,169.6 590.7,169.7 592.5,169.8 594.3,169.7 596.1,169.9 597.9,169.8 599.7,169.7 601.5,169.7 603.3,169.8 605.1,169.8 606.9,169.7 608.7,169.9 610.5,169.9 612.3,170.1 614.1,170.1 615.9,170.4 617.7,170.3 619.5,170.3 621.3,170.5 623.1,170.2 624.9,170.2 626.7,170.0 628.5,170.1 630.3,170.1 632.1,170.2 633.9,170.3 635.7,170.2 637.5,170.2 639.3,170.0 641.1,170.2 642.9,170.1 644.7,170.1 646.5,170.1 648.3,170.0 650.1,169.9 651.9,169.8 653.7,169.8 655.5,169.9 657.3,169.9 659.1,169.9" fill="none" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="4 2"/><line x1="50.0" y1="200.0" x2="660.0" y2="200.0" stroke="currentColor" stroke-width="1"/><line x1="50.0" y1="30.0" x2="50.0" y2="200.0" stroke="currentColor" stroke-width="1"/><line x1="50.0" y1="200.0" x2="50.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="50.0" y="217.0" font-size="12" text-anchor="middle">0</text><line x1="140.0" y1="200.0" x2="140.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="140.0" y="217.0" font-size="12" text-anchor="middle">1</text><line x1="229.9" y1="200.0" x2="229.9" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="229.9" y="217.0" font-size="12" text-anchor="middle">2</text><line x1="319.9" y1="200.0" x2="319.9" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="319.9" y="217.0" font-size="12" text-anchor="middle">3</text><line x1="409.9" y1="200.0" x2="409.9" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="409.9" y="217.0" font-size="12" text-anchor="middle">4</text><line x1="499.9" y1="200.0" x2="499.9" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="499.9" y="217.0" font-size="12" text-anchor="middle">5</text><line x1="589.8" y1="200.0" x2="589.8" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="589.8" y="217.0" font-size="12" text-anchor="middle">6</text><text x="660.0" y="232.0" font-size="12" text-anchor="end">시간 (s)</text><line x1="46.0" y1="169.1" x2="50.0" y2="169.1" stroke="currentColor" stroke-width="1"/><text x="43.0" y="173.1" font-size="12" text-anchor="end">-20</text><line x1="46.0" y1="107.3" x2="50.0" y2="107.3" stroke="currentColor" stroke-width="1"/><text x="43.0" y="111.3" font-size="12" text-anchor="end">0</text><line x1="46.0" y1="45.5" x2="50.0" y2="45.5" stroke="currentColor" stroke-width="1"/><text x="43.0" y="49.5" font-size="12" text-anchor="end">20</text><text x="14.0" y="20.0" font-size="12" text-anchor="start">대역 에너지 (상대 dB)</text><line x1="285.7" y1="30.0" x2="285.7" y2="250.0" stroke="#d0564a" stroke-width="1.5"/><text x="289.7" y="252.0" font-size="12" text-anchor="start">hang 300 → 2.62 s (잘림)</text><line x1="321.7" y1="30.0" x2="321.7" y2="266.0" stroke="#e08a3c" stroke-width="1.5"/><text x="325.7" y="268.0" font-size="12" text-anchor="start">hang 700 → 3.02 s (잘림)</text><line x1="557.4" y1="30.0" x2="557.4" y2="282.0" stroke="#3f9a6b" stroke-width="1.5"/><text x="561.4" y="284.0" font-size="12" text-anchor="start">hang 1000 → 5.64 s</text><text x="660.0" y="20.0" font-size="12" text-anchor="end">파랑 = frame 에너지, 주황 점선 = 문턱(noise floor + 6 dB)</text></svg>
```

그림 2 — q1("Set a timer for ten minutes … and remind me to check the oven", SNR 20 dB)의 대역 에너지(파랑)와 VAD 문턱(주황 점선). 넣은 쉼은 450 ms였지만, 잡음 속에서 "minutes"의 약한 끝소리와 "and"의 약한 시작이 문턱 아래로 묻혀서 **실제로 문턱 아래인 구간은 약 700 ms**가 된다. 그래서 hangover 300과 700은 둘 다 말 중간에서 끝을 선언한다(빨강, 주황). 1000이 되어서야 정답 끝(4.79 s) 뒤인 5.64 s에 선언한다.

여기서 중요한 관찰 두 가지:

- **잡음은 쉼을 길게 만든다.** 약한 자음(s, f, th)과 말끝 흐림이 noise floor 아래로 사라지기 때문이다. clean에서 괜찮던 hangover가 잡음 환경에서는 말을 자른다.
- **잡음은 끝도 당긴다.** 정답 끝 직전의 약한 소리가 이미 "침묵"으로 판정되므로, endpoint 지연이 hangover보다 **짧게** 측정될 수 있다. 아래 표에서 p50이 hangover보다 작은 이유다. 사용자 입장에서는 이득처럼 보이지만, 사실은 마지막 음절을 ASR에 못 줄 위험이다 (그래서 endpoint 뒤 수백 ms를 ASR 입력에 더 붙이는 trailing padding을 쓰기도 한다).

### 2.4 코드로 확인 — hangover 스윕 (예제 3)

무엇을 확인하는 코드인가: hangover를 100–1200 ms로 바꾸며 clip 10개 × 조건 4개(clean, 20, 10, 5 dB) = 40회에서 (1) **false cut-off**(정답 끝보다 100 ms 이상 먼저 끝을 선언한 횟수)와 (2) 정상 경우의 **endpoint 지연** 분포를 센다.

```python
# 예제 3: hangover를 바꿔 가며 endpoint 지연(말 끝 → 선언)과 false cut-off(말 도중 끝 선언)를 센다
import json, numpy as np
from l4lib import D, load, frame_db_band, endpointer
meta = json.load(open(f"{D}/meta.json"))
conds = ["clean", "snr20", "snr10", "snr5"]                  # 10 clip × 4 조건 = 40회
dbs = {(q["i"], c): frame_db_band(load(f"{D}/q{q['i']}_{c}.wav")) for q in meta for c in conds}
print("hang(ms)  cut-off/40  지연 p50  p90  max  (cut-off 안 된 경우, ms)")
for H in (100, 200, 300, 400, 500, 600, 800, 1000, 1200):
    cut, delay = 0, []
    for q in meta:
        for c in conds:
            segs = endpointer(dbs[(q["i"], c)], H, margin_db=6, floor_db=-200)
            t_end = (segs[0][1] + 1) * 0.02 if segs else 99.0      # 첫 endpoint 시각 (frame 끝)
            if t_end < q["end"] - 0.10:                            # 말 끝보다 100 ms 이상 먼저 선언
                cut += 1
            else:
                delay.append((t_end - q["end"]) * 1000)
    d = np.array(delay)
    print(f"{H:6d}    {cut:5d}     {np.median(d):6.0f} {np.percentile(d, 90):5.0f} {d.max():5.0f}")
miss = sum(not endpointer(frame_db_band(load(f"{D}/q{q['i']}_snr0.wav")), 600, 6, floor_db=-200)
           for q in meta)
print(f"SNR 0 dB, hang 600 ms: 말 시작조차 못 잡은 clip {miss}/10")
```

```text
hang(ms)  cut-off/40  지연 p50  p90  max  (cut-off 안 된 경우, ms)
   100       29        106   110   119
   200       21        149   210   219
   300       16        246   310   319
   400       12        326   410   419
   500       11        409   510   519
   600       10        507   610   619
   800        5        703   810   819
  1000        1        890  1009  1019
  1200        1       1090  1209  1219
SNR 0 dB, hang 600 ms: 말 시작조차 못 잡은 clip 7/10
```

출력에서 볼 것:

- 지연 p90과 max는 거의 정확히 **hangover + 10~20 ms**다. 2.2절의 손계산 그대로다. 반면 false cut-off는 hangover 300에서 40회 중 16회(40 %), 600에서도 10회(25 %), 1000에서야 1회다.
- 이 데이터에는 일부러 넣은 300–600 ms 쉼이 있다. 실제 사람의 말에도 "음…" 같은 쉼이 흔하다. **에너지 VAD + 고정 hangover만으로는 "빠르면서 안 자르는" 설정이 없다.** 이것이 업계가 의미 기반 endpointing(아래 2.6)으로 가는 이유다.
- SNR 0 dB에서는 7/10이 말 시작도 못 잡는다. 대역 에너지 VAD의 한계다. 말소리 frame 중 문턱을 넘는 것이 띄엄띄엄이라 "3 frame 연속"이 안 된다. 여기서부터는 ML VAD(B5 2.3: Silero VAD, WebRTC의 GMM 등)나 빔포밍(G4)이 필요하다.

```svg
<svg viewBox="0 0 680 280" xmlns="http://www.w3.org/2000/svg"><line x1="60.0" y1="230.0" x2="610.0" y2="230.0" stroke="currentColor" stroke-width="1"/><line x1="60.0" y1="30.0" x2="60.0" y2="230.0" stroke="currentColor" stroke-width="1"/><line x1="610.0" y1="30.0" x2="610.0" y2="230.0" stroke="currentColor" stroke-width="1"/><line x1="60.0" y1="230.0" x2="60.0" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="60.0" y="247.0" font-size="12" text-anchor="middle">0</text><line x1="151.7" y1="230.0" x2="151.7" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="151.7" y="247.0" font-size="12" text-anchor="middle">200</text><line x1="243.3" y1="230.0" x2="243.3" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="243.3" y="247.0" font-size="12" text-anchor="middle">400</text><line x1="335.0" y1="230.0" x2="335.0" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="335.0" y="247.0" font-size="12" text-anchor="middle">600</text><line x1="426.7" y1="230.0" x2="426.7" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="426.7" y="247.0" font-size="12" text-anchor="middle">800</text><line x1="518.3" y1="230.0" x2="518.3" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="518.3" y="247.0" font-size="12" text-anchor="middle">1000</text><line x1="610.0" y1="230.0" x2="610.0" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="610.0" y="247.0" font-size="12" text-anchor="middle">1200</text><line x1="56.0" y1="230.0" x2="60.0" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="53.0" y="234.0" font-size="12" text-anchor="end">0</text><line x1="56.0" y1="180.0" x2="60.0" y2="180.0" stroke="currentColor" stroke-width="1"/><text x="53.0" y="184.0" font-size="12" text-anchor="end">10</text><line x1="56.0" y1="130.0" x2="60.0" y2="130.0" stroke="currentColor" stroke-width="1"/><text x="53.0" y="134.0" font-size="12" text-anchor="end">20</text><line x1="56.0" y1="80.0" x2="60.0" y2="80.0" stroke="currentColor" stroke-width="1"/><text x="53.0" y="84.0" font-size="12" text-anchor="end">30</text><line x1="56.0" y1="30.0" x2="60.0" y2="30.0" stroke="currentColor" stroke-width="1"/><text x="53.0" y="34.0" font-size="12" text-anchor="end">40</text><line x1="610.0" y1="230.0" x2="614.0" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="617.0" y="234.0" font-size="12" text-anchor="start">0</text><line x1="610.0" y1="163.3" x2="614.0" y2="163.3" stroke="currentColor" stroke-width="1"/><text x="617.0" y="167.3" font-size="12" text-anchor="start">400</text><line x1="610.0" y1="96.7" x2="614.0" y2="96.7" stroke="currentColor" stroke-width="1"/><text x="617.0" y="100.7" font-size="12" text-anchor="start">800</text><line x1="610.0" y1="30.0" x2="614.0" y2="30.0" stroke="currentColor" stroke-width="1"/><text x="617.0" y="34.0" font-size="12" text-anchor="start">1200</text><polyline points="105.8,85.0 151.7,125.0 197.5,150.0 243.3,170.0 289.2,175.0 335.0,180.0 426.7,205.0 518.3,225.0 610.0,225.0" fill="none" stroke="#d0564a" stroke-width="2"/><circle cx="105.8" cy="85.0" r="3" fill="#d0564a"/><circle cx="151.7" cy="125.0" r="3" fill="#d0564a"/><circle cx="197.5" cy="150.0" r="3" fill="#d0564a"/><circle cx="243.3" cy="170.0" r="3" fill="#d0564a"/><circle cx="289.2" cy="175.0" r="3" fill="#d0564a"/><circle cx="335.0" cy="180.0" r="3" fill="#d0564a"/><circle cx="426.7" cy="205.0" r="3" fill="#d0564a"/><circle cx="518.3" cy="225.0" r="3" fill="#d0564a"/><circle cx="610.0" cy="225.0" r="3" fill="#d0564a"/><polyline points="105.8,212.3 151.7,205.2 197.5,189.0 243.3,175.7 289.2,161.8 335.0,145.5 426.7,112.8 518.3,81.7 610.0,48.3" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="105.8" cy="212.3" r="3" fill="#4a7bd0"/><circle cx="151.7" cy="205.2" r="3" fill="#4a7bd0"/><circle cx="197.5" cy="189.0" r="3" fill="#4a7bd0"/><circle cx="243.3" cy="175.7" r="3" fill="#4a7bd0"/><circle cx="289.2" cy="161.8" r="3" fill="#4a7bd0"/><circle cx="335.0" cy="145.5" r="3" fill="#4a7bd0"/><circle cx="426.7" cy="112.8" r="3" fill="#4a7bd0"/><circle cx="518.3" cy="81.7" r="3" fill="#4a7bd0"/><circle cx="610.0" cy="48.3" r="3" fill="#4a7bd0"/><text x="60.0" y="18.0" font-size="12" text-anchor="start">false cut-off (40회 중, 빨강)</text><text x="610.0" y="18.0" font-size="12" text-anchor="end">endpoint 지연 p50 ms (파랑)</text><text x="335.0" y="264.0" font-size="12" text-anchor="middle">hangover (ms)</text><rect x="335.0" y="30.0" width="183.3" height="200.0" fill="#3f9a6b" fill-opacity="0.1" stroke="none"/><text x="426.7" y="44.0" font-size="12" text-anchor="middle">흔히 고르는 구간</text></svg>
```

그림 3 — hangover를 키우면 false cut-off(빨강, 왼쪽 축)는 줄고 endpoint 지연(파랑, 오른쪽 축)은 거의 직선으로 늘어난다. 초록 띠는 제품에서 흔히 고르는 범위(약 0.6–1 s, 업계 감각치)다. "공짜 점심"이 없는 곡선이다.

### 2.5 손으로 계산 — false cut-off의 비용

hangover를 고르는 일은 두 비용의 저울질이다.

```
너무 짧음 (H = 300): 지연 300 ms, 그러나 40 % 확률로 말을 자름
  → 잘리면: 반쪽 질문으로 ASR+LLM이 돌고, 사용자가 다시 말해야 함
  → 다시 말하기 비용 ≈ 질문 길이(2.5 s) + 한 턴 전체(≈ 1 s) ≈ 3.5 s
  기대 지연 ≈ 0.6 × 0.3 + 0.4 × (0.3 + 3.5) ≈ 1.7 s

적당함 (H = 800): 지연 800 ms, 12.5 % 확률로 자름
  기대 지연 ≈ 0.875 × 0.8 + 0.125 × (0.8 + 3.5) ≈ 1.24 s

김 (H = 1000): 지연 1000 ms, 2.5 % 자름
  기대 지연 ≈ 0.975 × 1.0 + 0.025 × (1.0 + 3.5) ≈ 1.09 s
```

말로 하면: 이 데이터(일부러 쉼을 많이 넣은 질의)에서는 **긴 hangover가 기대값으로는 오히려 낫다.** 잘림 한 번의 비용(다시 말하기)이 수백 ms의 대기보다 훨씬 비싸기 때문이다. 다만 사용자는 기대값이 아니라 **매번의 대기**를 느낀다. 그래서 실제 제품은 "기본은 짧게, 불확실하면 길게"처럼 **조건부 hangover**를 쓴다 (다음 절).

### 2.6 endpointing을 줄이는 방법들

| 방법 | 아이디어 | 효과 | 비용·함정 |
|---|---|---|---|
| 조건부 hangover | partial 전사가 "문장처럼 끝났으면" 짧게, "and", "um"으로 끝났으면 길게 | 평균 대기 감소 | streaming ASR의 partial이 필요 |
| 학습된 end-of-query 모델 | 음향 + partial 텍스트로 "끝났을 확률"을 frame마다 예측 (Shannon et al., 2017) | 고정 hangover보다 수백 ms 단축 보고 | 학습 데이터, 언어별 튜닝 |
| ASR과 endpointing 결합 | E2E ASR이 "끝" token을 같이 출력 (Chang et al., 2019) | ASR final과 endpoint가 동시에 | 모델 설계 변경 |
| 투기적 실행 (speculative) | 짧은 hangover에서 미리 LLM을 시작, 말이 이어지면 버림 | 사용자 대기 감소 | 버리는 계산 = 전력 낭비, 취소 경로 필수 (7절) |
| push-to-talk / 버튼 | 사용자가 끝을 알려 줌 | endpoint 지연 ≈ 0 | UX 제약. 웨어러블에선 탭 제스처(IMU)로 가능 |

웨어러블 관점의 덧붙임: Hark 같은 기기에 IMU가 있다면 "손목 내림"이나 "탭"을 끝 신호로 쓰는 multimodal endpointing도 생각해 볼 수 있다(추정 — 제품 결정은 아니다).

### 2.7 펌웨어로 옮기기 — 정수 endpointer (C)

무엇을 확인하는 코드인가: 위 상태기계를 **MCU/DSP에서 돌릴 형태**로 쓴다. 부동소수점과 log를 없앤다. "+6 dB ≈ ×4"이므로 margin 비교는 `nf << 2`, noise floor IIR의 α = 1/16은 shift로 만든다. (대역 필터는 생략하고 전 대역 에너지를 쓴다 — DSP라면 앞단 band-pass biquad를 넣으면 된다, G5 4절.)

```c
/* endpoint.c — 펌웨어식 endpointer: 정수 frame 에너지, shift로 만든 noise floor IIR, hangover 카운터 */
#include <stdint.h>
#include <stdio.h>
#define FRAME 320                                  /* 20 ms @ 16 kHz */
typedef enum { SIL, SPEECH } state_t;
int main(int argc, char **argv) {
    if (argc < 3) return 1;
    FILE *f = fopen(argv[1], "rb"); int hang = 0; sscanf(argv[2], "%d", &hang);
    if (!f) return 1;
    int16_t s[FRAME]; uint64_t nf = 0; state_t st = SIL; int run = 0, k = 0, start = 0;
    while (fread(s, sizeof s[0], FRAME, f) == FRAME) {
        uint64_t e = 0;
        for (int i = 0; i < FRAME; i++) e += (int64_t)s[i] * s[i];   /* 에너지 합 (Q0) */
        e = e / FRAME + 1;                                           /* 평균 전력, 0 방지 */
        if (k < 10) nf = (k == 0 || e < nf) ? e : nf;                /* 처음 200 ms 최솟값 */
        int sp = e > (nf << 2) && e > 3000;                          /* +6 dB ≈ ×4, 절대 하한 */
        if (!sp && k >= 10) nf = nf - (nf >> 4) + (e >> 4);          /* α = 1/16 IIR */
        if (st == SIL) {
            run = sp ? run + 1 : 0;
            if (run >= 3) { st = SPEECH; start = k - 2; run = 0; }
        } else {
            run = sp ? 0 : run + 1;
            if (run >= hang / 20) {
                printf("  segment %4d ms → %4d ms (endpoint 선언)\n", start * 20, (k + 1) * 20);
                st = SIL; run = 0;
            }
        }
        k++;
    }
    fclose(f); return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 endpoint.c -o endpoint      # 경고 0개
for c in clean snr20; do for h in 300 700; do echo "$c $h"; ./endpoint q6_$c.pcm $h; done; done
```

```text
clean 300
  segment  800 ms → 1480 ms (endpoint 선언)
  segment 1740 ms → 4400 ms (endpoint 선언)
clean 700
  segment  800 ms → 4800 ms (endpoint 선언)
snr20 300
  segment  800 ms → 1300 ms (endpoint 선언)
  segment 1740 ms → 4400 ms (endpoint 선언)
snr20 700
  segment  800 ms → 1700 ms (endpoint 선언)
  segment 1740 ms → 4800 ms (endpoint 선언)
```

같은 정수 연산을 Python으로 옮긴 twin(`twin.py`)도 똑같은 segment를 낸다: `clean 300 [(800, 1480), (1740, 4400)]`, `clean 700 [(800, 4800)]`, `snr20 300 [(800, 1300), (1740, 4400)]`, `snr20 700 [(800, 1700), (1740, 4800)]`. (입력 `.pcm`은 WAV에서 헤더를 뺀 little-endian int16 원시 샘플이다.)

출력에서 볼 것: q6은 "Um, [600 ms 쉼] what time does the pharmacy close today?"이고 정답 말소리는 800–4100 ms다. hangover 300이면 clean에서도 "Um," 직후 1480 ms에 끝을 선언한다 — **filler 뒤의 쉼이 endpointing의 최악 사례**다. 700이면 clean에서는 버티지만 SNR 20 dB에서는 1700 ms에 잘린다. 잘린 첫 segment(800–1700 ms)를 ASR에 주면 "Um."만 나온다. 펌웨어에서는 이 상태기계가 frame ISR(또는 DMA 완료 콜백) 안에서 frame당 수백 사이클로 끝난다. endpoint 선언은 application SoC를 깨우는 **이벤트(IRQ/메일박스)**가 된다(9절).

---

## 3. ASR — Whisper-tiny를 파이프라인에 붙이기

### 3.1 이 단계가 하는 일

endpointer가 잘라 준 segment(말소리 구간)를 텍스트로 바꾼다. 모델 구조(encoder-decoder, 30 s 창, 80채널 log-mel)는 B5 4.5절에서 봤으므로 여기서는 **두 숫자만** 잰다.

- **정확도**: WER(word error rate). 잡음이 늘면 얼마나 나빠지나.
- **지연**: endpoint 선언 → 전사 확정(③)까지 몇 ms인가. 그리고 그 시간이 어디서 나오나(encoder vs decoder).

### 3.2 WER — 단어 단위 edit distance

**WER**은 정답 문장(reference)을 인식 결과(hypothesis)로 바꾸는 데 필요한 최소 편집 횟수를 정답 단어 수로 나눈 것이다.

```
WER = (S + D + I) / N
  S = 치환(substitution), D = 삭제(deletion), I = 삽입(insertion), N = 정답 단어 수
```

말로 하면: "몇 단어를 고치면 정답이 되나"를 정답 길이로 나눈 비율이다. 삽입이 많으면 100 %를 넘을 수도 있다. 최소 편집 횟수는 **Levenshtein 거리**를 단어 단위로 계산하면 된다. 이것은 동적 계획법(DP)이다.

```
d[i][j] = 정답 앞 i단어를 결과 앞 j단어로 바꾸는 최소 비용
d[i][0] = i (모두 삭제),  d[0][j] = j (모두 삽입)
d[i][j] = min( d[i-1][j] + 1            ← 정답 단어 삭제
               d[i][j-1] + 1            ← 결과 단어 삽입
               d[i-1][j-1] + (같으면 0, 다르면 1) )   ← 일치 또는 치환
```

손으로 계산: 정답 "set a timer for ten minutes"(N = 6), 결과 "said a time for 10 minutes". 정렬하면 set→said(S), a=a, timer→time(S), for=for, ten→10(S), minutes=minutes. S = 3, WER = 3/6 = 50 %. 펌웨어로 보면 이 DP 표는 `(N+1) × (M+1)` int 배열 하나와 이중 루프다 — `diff` 유틸리티가 줄 단위로 하는 일과 같다.

`l4lib.py`의 구현과 확인 (예제 4):

```python
def wer_counts(ref, hyp):                    # 단어 edit distance (S+D+I)와 참조 단어 수
    r, h = ref.split(), hyp.split()
    d = np.zeros((len(r) + 1, len(h) + 1), dtype=int)
    d[:, 0] = np.arange(len(r) + 1); d[0, :] = np.arange(len(h) + 1)
    for i in range(1, len(r) + 1):
        for j in range(1, len(h) + 1):
            d[i, j] = min(d[i-1, j] + 1, d[i, j-1] + 1,
                          d[i-1, j-1] + (r[i-1] != h[j-1]))
    return d[-1, -1], len(r)
```

`norm()`은 소문자로 바꾸고 구두점을 지우는 세 줄짜리 함수다(숫자 단어 통일은 하지 않는다).

```python
# 예제 4: 단어 단위 edit distance로 WER = (S + D + I) / N 을 계산 — 손계산과 맞춰 보기
from l4lib import wer_counts, norm
ref = "set a timer for ten minutes"
for hyp in ["set a timer for ten minutes",        # 완벽
            "set a timer for two minutes",        # 치환 1
            "set timer for ten minutes",          # 삭제 1
            "set a timer for ten minutes please", # 삽입 1
            "said a time for 10 minutes"]:        # 치환 3 (10 ≠ ten 도 오류로 센다)
    e, n = wer_counts(norm(ref), norm(hyp))
    print(f"{e} / {n} = WER {e / n:5.1%}   | {hyp}")
print(norm("What's the weather like in San Francisco, tomorrow?"))
```

```text
0 / 6 = WER  0.0%   | set a timer for ten minutes
1 / 6 = WER 16.7%   | set a timer for two minutes
1 / 6 = WER 16.7%   | set timer for ten minutes
1 / 6 = WER 16.7%   | set a timer for ten minutes please
3 / 6 = WER 50.0%   | said a time for 10 minutes
what's the weather like in san francisco tomorrow
```

출력에서 볼 것: 마지막 줄의 3/6이 손계산과 같다. 그런데 "ten"과 "10"은 사람이 보기엔 같은 말인데 오류로 셌다. 그래서 실제 WER 측정은 **텍스트 정규화**(normalization)를 먼저 한다. Whisper 저장소에는 영어 정규화기(`EnglishTextNormalizer`)가 있고, transformers의 `WhisperTokenizer.normalize()`가 그것을 쓴다. 이 정규화기는 "ten"을 "10"으로, "what's"를 "what is"로 바꾸고, "um" 같은 filler를 지운다. 다음 예제부터는 이것을 쓴다. **WER 숫자를 비교할 때는 정규화 규칙이 같은지부터 확인하라** — 논문마다 다르다.

### 3.3 공용 ASR 모듈

`asrlib.py`는 모델을 한 번 로드하고, 전처리(log-mel)와 생성(generate)의 시간을 따로 잰다.

```python
# asrlib.py — Whisper-tiny 로드 + 한 clip 전사 (시간 측정 포함)
import time, torch, warnings
from transformers import WhisperProcessor, WhisperForConditionalGeneration
from transformers.utils import logging as hf_logging
warnings.filterwarnings("ignore"); hf_logging.set_verbosity_error()
torch.set_num_threads(4)
proc = WhisperProcessor.from_pretrained("openai/whisper-tiny", local_files_only=True)
def load_model(dev):
    return WhisperForConditionalGeneration.from_pretrained(
        "openai/whisper-tiny", local_files_only=True).to(dev).eval()

def transcribe(model, x, dev="cpu"):
    t0 = time.perf_counter()
    feat = proc(x, sampling_rate=16000, return_tensors="pt").input_features.to(dev)  # 항상 [1,80,3000]
    t1 = time.perf_counter()
    with torch.no_grad():
        ids = model.generate(feat, language="en", task="transcribe", max_new_tokens=60)
    if dev == "mps": torch.mps.synchronize()
    t2 = time.perf_counter()
    text = proc.batch_decode(ids, skip_special_tokens=True)[0].strip()
    return text, (t1 - t0) * 1000, (t2 - t1) * 1000, ids.shape[1]
```

`torch.mps.synchronize()`는 GPU 작업이 실제로 끝날 때까지 기다리게 한다. GPU 호출은 비동기라서 이것 없이 시간을 재면 "명령을 큐에 넣은 시간"만 재게 된다 — DMA를 kick만 하고 완료 인터럽트를 안 기다린 채 latency를 재는 실수와 같다(D6 2절).

### 3.4 코드로 확인 — 잡음별 WER (예제 5)

무엇을 확인하는 코드인가: 40개 clip(10개 × clean, 10, 5, 0 dB)을 전사하고 정규화한 뒤 WER을 센다. greedy decoding이라 결과는 매번 같다.

```python
# 예제 5: Whisper-tiny로 40개 clip 전사 → 조건별 WER (greedy라 결과는 매번 같다)
import json
from l4lib import D, load, wer_counts
from asrlib import proc, load_model, transcribe
meta = json.load(open(f"{D}/meta.json")); model = load_model("cpu")
nz = proc.tokenizer.normalize        # Whisper 영어 정규화: "ten"→"10", "what's"→"what is", "um" 제거
res = {}
for c in ("clean", "snr10", "snr5", "snr0"):
    E = N = 0
    for q in meta:
        text = transcribe(model, load(f"{D}/q{q['i']}_{c}.wav"))[0]
        e, n = wer_counts(nz(q["ref"]), nz(text)); E += e; N += n
        res[f"q{q['i']}_{c}"] = dict(text=text, err=int(e), n=int(n))
    print(f"{c:6s} WER {E:2d}/{N} = {E / N:5.1%}   오류 있는 clip {sum(r['err'] > 0 for k, r in res.items() if k.endswith(c))}/10")
json.dump(res, open(f"{D}/asr.json", "w"), indent=1)
for k in ("q2_clean", "q3_clean", "q3_snr10", "q1_snr5", "q8_snr5"):
    q = meta[int(k[1])]
    print(f"{k:9s} ref: {q['ref']}\n{'':9s} hyp: {res[k]['text']}  (오류 {res[k]['err']})")
```

```text
clean  WER 11/82 = 13.4%   오류 있는 clip 4/10
snr10  WER 25/82 = 30.5%   오류 있는 clip 6/10
snr5   WER 36/82 = 43.9%   오류 있는 clip 7/10
snr0   WER 68/82 = 82.9%   오류 있는 clip 9/10
q2_clean  ref: How many ounces are in a cup?
          hyp: How many hours is already in the cup?  (오류 4)
q3_clean  ref: Call my mom on her cell phone.
          hyp: Call my mom under a sailboat.  (오류 4)
q3_snr10  ref: Call my mom on her cell phone.
          hyp: Oh man, I'm not sure about that.  (오류 8)
q1_snr5   ref: Set a timer for ten minutes and remind me to check the oven.
          hyp: Set a timer 410 minutes and remind me to check the other.  (오류 3)
q8_snr5   ref: Turn off the lights in the living room please.
          hyp: You know, the light in the living room.  (오류 4)
```

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg"><line x1="70.0" y1="220.0" x2="640.0" y2="220.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="30.0" x2="70.0" y2="220.0" stroke="currentColor" stroke-width="1"/><line x1="66.0" y1="220.0" x2="70.0" y2="220.0" stroke="currentColor" stroke-width="1"/><text x="63.0" y="224.0" font-size="12" text-anchor="end">0%</text><line x1="70.0" y1="220.0" x2="640.0" y2="220.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><line x1="66.0" y1="172.5" x2="70.0" y2="172.5" stroke="currentColor" stroke-width="1"/><text x="63.0" y="176.5" font-size="12" text-anchor="end">25%</text><line x1="70.0" y1="172.5" x2="640.0" y2="172.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><line x1="66.0" y1="125.0" x2="70.0" y2="125.0" stroke="currentColor" stroke-width="1"/><text x="63.0" y="129.0" font-size="12" text-anchor="end">50%</text><line x1="70.0" y1="125.0" x2="640.0" y2="125.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><line x1="66.0" y1="77.5" x2="70.0" y2="77.5" stroke="currentColor" stroke-width="1"/><text x="63.0" y="81.5" font-size="12" text-anchor="end">75%</text><line x1="70.0" y1="77.5" x2="640.0" y2="77.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><line x1="66.0" y1="30.0" x2="70.0" y2="30.0" stroke="currentColor" stroke-width="1"/><text x="63.0" y="34.0" font-size="12" text-anchor="end">100%</text><line x1="70.0" y1="30.0" x2="640.0" y2="30.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><rect x="110.0" y="194.5" width="70.0" height="25.5" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="145.0" y="188.5" font-size="13" text-anchor="middle">13.4%</text><text x="145.0" y="237.0" font-size="12" text-anchor="middle">clean</text><rect x="245.0" y="162.1" width="70.0" height="57.9" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="280.0" y="156.1" font-size="13" text-anchor="middle">30.5%</text><text x="280.0" y="237.0" font-size="12" text-anchor="middle">SNR 10</text><rect x="380.0" y="136.6" width="70.0" height="83.4" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="415.0" y="130.6" font-size="13" text-anchor="middle">43.9%</text><text x="415.0" y="237.0" font-size="12" text-anchor="middle">SNR 5</text><rect x="515.0" y="62.5" width="70.0" height="157.5" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="550.0" y="56.5" font-size="13" text-anchor="middle">82.9%</text><text x="550.0" y="237.0" font-size="12" text-anchor="middle">SNR 0</text><text x="70.0" y="18.0" font-size="12" text-anchor="start">Whisper-tiny WER (10개 질의, 82 단어)</text><text x="550.0" y="254.0" font-size="12" text-anchor="middle">에너지 VAD도 7/10 놓침</text></svg>
```

그림 4 — Whisper-tiny의 WER은 잡음이 5 dB씩 늘 때마다 크게 나빠진다. 0 dB에서는 82.9 %로 사실상 쓸 수 없고, 같은 조건에서 2절의 에너지 VAD도 10개 중 7개의 말 시작을 놓쳤다.

출력에서 볼 것:

- **clean에서도 13.4 %**다. `say` 목소리(특히 옛날식 Fred, Kathy)는 Whisper 학습 데이터의 실제 사람 목소리와 다르다. 그리고 tiny는 가장 작은 Whisper다. 이 숫자를 "Whisper-tiny의 일반적인 WER"로 쓰면 안 된다 — **이 데이터에서의 숫자**다.
- 오류의 **종류**가 중요하다. "Call my mom **under a sailboat**"(clean!)은 그럴듯한 영어지만 뜻이 완전히 다르다. "Set a timer **410** minutes"는 "for ten"이 "410"으로 붙은 것이다 — 숫자 하나 틀림이 행동(타이머 6시간 50분)을 바꾼다. "**You know**, the light in the living room"은 "Turn off"라는 **의도 동사**가 사라졌다. WER 4/9이 같아도 의도를 잃었느냐 아니냐는 천지 차이다. 그래서 음성 비서 팀은 WER 외에 **intent accuracy**, **slot error rate**(숫자·이름 같은 핵심 값 오류)를 따로 본다.
- SNR 10 dB에서 "Call my mom on her cell phone"이 "Oh man, I'm not sure about that"이 됐다. Whisper는 잡음 속에서 **그럴듯한 문장을 지어내는** 경향(hallucination)이 있다. 웹 자막으로 학습한 언어 모델 성격 때문이다. CTC 모델은 보통 이렇게 문장을 지어내지 않고 철자가 깨지는 쪽으로 틀린다.

8절에서 이 오류들을 LLM에 실제로 넣어 본다.

### 3.5 코드로 확인 — 지연 분해와 30 s 창의 비용 (예제 6)

무엇을 확인하는 코드인가: 길이가 다른 세 입력(4.7 s, 6.8 s, 세 clip을 이은 18.2 s)에서 encoder만의 시간과 전체 전사 시간을 CPU와 MPS로 잰다(warm-up 후 3회 중앙값). 마지막으로 30 s보다 짧은 mel을 넣으면 어떻게 되는지 본다.

```python
# 예제 6: Whisper-tiny 지연 분해 — encoder(항상 30 s 분량) vs decoder(token 수에 비례), CPU vs MPS
import os, time, json, numpy as np, torch
from l4lib import D, load
from asrlib import proc, load_model, transcribe
clips = {"q9 (4.7 s)": load(f"{D}/q9_clean.wav"), "q1 (6.8 s)": load(f"{D}/q1_clean.wav"),
         "q1+q4+q0 (18.2 s)": np.concatenate([load(f"{D}/q{i}_clean.wav") for i in (1, 4, 0)])}
print(f"load average {os.getloadavg()[0]:.1f}  (다른 작업과 함께 측정 — 절대값보다 비율을 볼 것)")
for dev in ("cpu", "mps"):
    m = load_model(dev); transcribe(m, clips["q9 (4.7 s)"], dev)          # warm-up
    for name, x in clips.items():
        feat = proc(x, sampling_rate=16000, return_tensors="pt").input_features.to(dev)
        enc = []
        for _ in range(3):
            t0 = time.perf_counter()
            with torch.no_grad(): m.model.encoder(feat)
            if dev == "mps": torch.mps.synchronize()
            enc.append((time.perf_counter() - t0) * 1000)
        runs = [transcribe(m, x, dev) for _ in range(3)]
        tot = np.median([r[1] + r[2] for r in runs]); dur = len(x) / 16000
        print(f"{dev:3s} {name:16s} mel {tuple(feat.shape)}  encoder {np.median(enc):5.0f} ms"
              f"  전체 {tot:5.0f} ms  token {runs[0][3]:2d}  RTF {tot / 1000 / dur:.2f}")
try:
    m.model.encoder(feat[:, :, :400])                  # 4 s 분량(400 frame)만 넣어 보기
except Exception as e:
    print("4 s 분량 mel만 넣으면:", str(e)[:90])
```

```text
load average 5.6  (다른 작업과 함께 측정 — 절대값보다 비율을 볼 것)
cpu q9 (4.7 s)       mel (1, 80, 3000)  encoder    96 ms  전체   180 ms  token  8  RTF 0.04
cpu q1 (6.8 s)       mel (1, 80, 3000)  encoder   106 ms  전체   254 ms  token 15  RTF 0.04
cpu q1+q4+q0 (18.2 s) mel (1, 80, 3000)  encoder   113 ms  전체   351 ms  token 25  RTF 0.02
mps q9 (4.7 s)       mel (1, 80, 3000)  encoder    88 ms  전체   151 ms  token  8  RTF 0.03
mps q1 (6.8 s)       mel (1, 80, 3000)  encoder    63 ms  전체   176 ms  token 15  RTF 0.03
mps q1+q4+q0 (18.2 s) mel (1, 80, 3000)  encoder    60 ms  전체   261 ms  token 25  RTF 0.01
4 s 분량 mel만 넣으면: Whisper expects the mel input features to be of length 3000, but found 400. Make sure to p
```

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg"><text x="142.0" y="36.0" font-size="12" text-anchor="end">CPU q9 · 8 tok</text><rect x="150.0" y="20.0" width="117.6" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="267.6" y="20.0" width="102.9" height="22.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="375.5" y="36.0" font-size="12" text-anchor="start">180 ms</text><text x="142.0" y="70.0" font-size="12" text-anchor="end">CPU q1 · 15 tok</text><rect x="150.0" y="54.0" width="129.9" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="279.9" y="54.0" width="181.3" height="22.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="466.1" y="70.0" font-size="12" text-anchor="start">254 ms</text><text x="142.0" y="104.0" font-size="12" text-anchor="end">CPU 18 s · 25 tok</text><rect x="150.0" y="88.0" width="138.4" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="288.4" y="88.0" width="291.6" height="22.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="585.0" y="104.0" font-size="12" text-anchor="start">351 ms</text><text x="142.0" y="138.0" font-size="12" text-anchor="end">MPS q9 · 8 tok</text><rect x="150.0" y="122.0" width="107.8" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="257.8" y="122.0" width="77.2" height="22.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="340.0" y="138.0" font-size="12" text-anchor="start">151 ms</text><text x="142.0" y="172.0" font-size="12" text-anchor="end">MPS q1 · 15 tok</text><rect x="150.0" y="156.0" width="77.2" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="227.2" y="156.0" width="138.4" height="22.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="370.6" y="172.0" font-size="12" text-anchor="start">176 ms</text><text x="142.0" y="206.0" font-size="12" text-anchor="end">MPS 18 s · 25 tok</text><rect x="150.0" y="190.0" width="73.5" height="22.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="223.5" y="190.0" width="246.2" height="22.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="474.7" y="206.0" font-size="12" text-anchor="start">261 ms</text><line x1="150.0" y1="230.0" x2="640.0" y2="230.0" stroke="currentColor" stroke-width="1"/><line x1="150.0" y1="230.0" x2="150.0" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="150.0" y="247.0" font-size="12" text-anchor="middle">0</text><line x1="272.5" y1="230.0" x2="272.5" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="272.5" y="247.0" font-size="12" text-anchor="middle">100</text><line x1="395.0" y1="230.0" x2="395.0" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="395.0" y="247.0" font-size="12" text-anchor="middle">200</text><line x1="517.5" y1="230.0" x2="517.5" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="517.5" y="247.0" font-size="12" text-anchor="middle">300</text><line x1="640.0" y1="230.0" x2="640.0" y2="234.0" stroke="currentColor" stroke-width="1"/><text x="640.0" y="247.0" font-size="12" text-anchor="middle">400</text><rect x="150.0" y="258.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="170.0" y="269.0" font-size="12" text-anchor="start">encoder (30 s 창 고정)</text><rect x="350.0" y="258.0" width="14.0" height="12.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="370.0" y="269.0" font-size="12" text-anchor="start">decoder + 전처리 (token 수에 비례)</text></svg>
```

그림 5 — Whisper-tiny 전사 시간 분해(load average 5.6일 때). 파랑(encoder)은 입력 길이와 상관없이 거의 같고, 주황(decoder + 전처리)은 생성 token 수를 따라 늘어난다.

출력에서 볼 것:

- mel 모양이 **항상 (1, 80, 3000)**이다. 4.7 s든 18.2 s든 30 s로 padding된다. 그래서 encoder 시간이 CPU에서 96–113 ms로 거의 일정하다. 짧은 mel을 직접 넣으면 transformers가 거부한다(마지막 줄).
- 손으로 계산 — padding 낭비: q9의 실제 말소리는 1.85 s다. encoder가 처리하는 30 s 중 1.85 / 30 ≈ 6 %만 말소리이고 **94 %는 0을 계산**한다. endpointer가 잘라 준 segment(2–4 s)만 넣는 6절 파이프라인에서도 마찬가지다.
- 손으로 계산 — decoder 비용: CPU에서 (254 − 180) / (15 − 8) ≈ 10.6 ms/token, (351 − 254) / (25 − 15) ≈ 9.7 ms/token. 대략 **token당 10 ms**다. tiny decoder는 4층뿐이지만 51,865 vocab의 출력층(384 × 51,865 ≈ 20 M MAC)이 token마다 돈다(B5 4.5의 "파라미터의 78 %가 decoder").
- **RTF(real-time factor)** = 처리 시간 / 오디오 길이. 0.04면 1초 오디오를 40 ms에 처리한다는 뜻이다. RTF < 1이면 "실시간보다 빠르다"지만, **RTF가 작다고 지연이 작은 것은 아니다.** Whisper는 끝까지 들은 뒤에 시작하므로 ASR final 지연 = 전체 처리 시간이다. streaming ASR은 RTF가 같아도 말하는 동안 대부분을 처리해 두므로 final 지연이 마지막 조각만큼이다.
- MPS(GPU)가 CPU보다 크게 빠르지 않다. tiny는 너무 작아서 GPU 커널 실행 오버헤드가 계산보다 크다. 웨어러블 NPU에서도 **작은 모델일수록 가속기 호출 오버헤드(드라이버, 메모리 동기화)가 상대적으로 커진다**(F4·E8).

### 3.6 Whisper가 streaming에 어색한 이유

B5 4.4–4.5절에서 구조를 봤다. 파이프라인 관점에서 다시 정리하면:

| 문제 | 이유 | 파이프라인에 미치는 영향 |
|---|---|---|
| 30 s 고정 창 | encoder 위치 임베딩이 1500 위치로 고정, 학습도 30 s 단위 | 짧은 말에도 30 s 계산 (위 측정: 94 % 낭비) |
| 끝까지 들어야 시작 | encoder가 전체 입력을 양방향으로 본다 | ASR final 지연 = 전체 처리 시간 (CPU 180–350 ms) |
| partial이 없음 | 중간 결과를 보려면 지금까지의 오디오로 **처음부터 다시** 돌려야 함 | 0.5 s마다 재실행하면 실행당 180 ms → CPU 36 % 이상을 계속 점유 |
| autoregressive decoder | token을 하나씩 생성, 매번 출력층 전체 | token당 약 10 ms (CPU) |
| hallucination | 웹 자막으로 학습한 언어 모델 성격 | 잡음·침묵에서 그럴듯한 문장을 지어냄 (3.4절 실측) |

그래서 Whisper를 실시간에 쓰려는 시도(예: 겹치는 창으로 반복 실행하고 두 번 연속 같은 접두어만 확정하는 "local agreement" 방식, Macháček et al., 2023)가 있지만, 계산을 여러 번 반복하는 대가를 치른다.

streaming ASR(RNN-T, CTC, 그리고 Conformer/Emformer 계열 encoder)은 구조가 다르다.

```
Whisper:     [────── 말 전체 ──────][endpoint] → encoder(30 s) → decoder … → final
                                               └──── 180–350 ms (이 Mac CPU) ────┘
streaming:   [chunk][chunk][chunk][chunk]…[endpoint] → 마지막 chunk만 → final
              ↓partial ↓partial ↓partial            └─ chunk 1개 + lookahead ─┘
```

말로 하면: streaming 모델은 80–320 ms chunk마다 encoder를 조금씩 돌리고(미래는 lookahead 몇 frame만 봄), partial 결과를 계속 낸다. 말이 끝나면 마지막 chunk만 처리하면 되므로 final 지연이 짧다. 그리고 partial 텍스트가 있으니 2.6절의 **조건부 endpointing**도 가능해진다. 대가는 미래 문맥을 덜 보기 때문에 생기는 약간의 정확도 손실이다. 웨어러블 on-device ASR이 보통 이 계열을 쓰는 이유다(9절).

---

## 4. LLM — token을 stream으로 받고, TTS용 구절로 자르기

### 4.1 SSE 클라이언트

`llama-server`는 OpenAI 호환 `/v1/chat/completions`에 `"stream": true`를 주면 **SSE(server-sent events)**로 token을 보낸다. HTTP 응답 본문이 끝나지 않은 채 `data: {...}` 줄이 token마다 하나씩 흘러나오는 형식이다. 펌웨어로 보면 "완료 인터럽트 하나" 대신 "데이터가 올 때마다 RX 인터럽트"를 받는 것과 같다. 서버 구조와 KV-cache·취소 처리 자체는 L2에서 다룬다. 여기서는 클라이언트 쪽에서 **도착 시각**만 정확히 찍는다.

```python
# llmlib.py — llama-server SSE streaming 클라이언트 + TTS용 phrase chunker
import http.client, json, re, time
PORT = 18490
SYS = ("You are a voice assistant on a small wearable. Reply in plain spoken English, "
       "two or three short sentences, no lists or markdown.")

def stream_chat(user, max_tokens=80, on_delta=None, stop_flag=None):
    """SSE로 token을 받으며 (도착 시각 ms, 텍스트 조각) 목록을 돌려준다. stop_flag()가 True면 끊는다."""
    body = json.dumps({"messages": [{"role": "system", "content": SYS},
                                    {"role": "user", "content": user}],
                       "stream": True, "temperature": 0, "max_tokens": max_tokens,
                       "cache_prompt": False})
    t0 = time.perf_counter(); out = []
    conn = http.client.HTTPConnection("127.0.0.1", PORT)
    conn.request("POST", "/v1/chat/completions", body, {"Content-Type": "application/json"})
    resp = conn.getresponse()
    for raw in resp:                                   # SSE: "data: {...}\n" 한 줄이 token 하나
        line = raw.decode().strip()
        if not line.startswith("data: ") or line == "data: [DONE]":
            continue
        d = json.loads(line[6:])["choices"][0]["delta"].get("content")
        if d:
            out.append(((time.perf_counter() - t0) * 1000, d))
            if on_delta: on_delta(out[-1])
        if stop_flag and stop_flag():
            break
    conn.close()                                       # 끊으면 서버가 생성을 멈춘다 (barge-in)
    return out
```

서버는 다음처럼 띄웠다(F3에서 만든 바이너리와 모델).

```sh
.tools/llama.cpp/build/bin/llama-server -m .tools/models/qwen2.5-0.5b-Q4_K_M.gguf \
    --host 127.0.0.1 --port 18490 -c 2048 -np 1
```

두 가지 설정 의도:

- `temperature 0`: greedy. 같은 입력이면 같은 답 → 측정 재현성.
- `cache_prompt: false`: system prompt의 KV를 재사용하지 않게 해서 매번 prefill을 다 하게 했다(보수적 측정). 실제 제품에서는 system prompt KV 캐시가 필수다(I1 3.3, D5).
- `-np 1`: slot(동시 요청 처리 단위) 1개. 웨어러블처럼 한 번에 한 대화만 처리하는 상황을 흉내 낸다. 7절에서 이 설정이 barge-in 취소의 중요성을 드러낸다.

### 4.2 phrase chunker — 언제 TTS에 넘기나

TTS는 token 하나("The")만 받아서는 자연스럽게 읽을 수 없다. 억양(prosody)은 구절 전체를 보고 정해지기 때문이다. 반대로 답 전체를 기다리면 streaming의 의미가 없다. 그래서 규칙을 정한다.

1. 문장 끝(`.`, `!`, `?` 뒤에 공백 또는 끝)이 오면 거기서 자른다.
2. 쉼표·세미콜론·콜론이 오고 앞에 단어가 4개 이상이면 자른다 ("Sure," 같은 너무 짧은 조각은 억양이 어색해서 피한다).
3. 구두점 없이 **완성된 단어가 10개** 쌓이면 거기서 자른다(안전장치). 문장이 길면 1, 2번만으로는 첫 구절이 한참 늦어진다.

```python
PHRASE_END = re.compile(r"([.!?](\s|$))|([,;:]\s)")
def first_phrase_cut(text, min_words=4, max_words=10):
    """TTS에 넘길 첫 구절의 끝 위치. 우선순위: 문장 끝(.!?) > 단어 min_words개 이상 뒤 쉼표
    > 구두점 없이 완성된 단어가 max_words개 쌓이면 거기서 자름. 없으면 None (더 기다림)"""
    for m in PHRASE_END.finditer(text):
        head = text[: m.end()].strip()
        if m.group(1) or len(head.split()) >= min_words:
            return len(text[: m.end()].rstrip())
    words = list(re.finditer(r"\S+\s", text))           # 뒤에 공백이 온 단어 = 완성된 단어
    return words[max_words - 1].end() - 1 if len(words) >= max_words else None
```

"뒤에 공백이 온 단어"만 센 이유: token은 단어 조각으로 온다("temper", "atures"). 마지막 단어는 아직 덜 왔을 수 있으므로 다음 공백이 와야 완성으로 본다.

무엇을 확인하는 코드인가 (예제 7): stream 도중의 여러 텍스트 상태에서 chunker가 어디를 자르는지.

```python
# 예제 7: phrase chunker가 stream 중간 상태의 텍스트에서 어디를 자르는지 (None = 아직 기다림)
from llmlib import first_phrase_cut
for t in ["A standard cup holds",                                  # 문장 미완성
          "A standard cup holds 8 ounces. That is",                # 문장 끝 있음
          "Sure, I can",                                           # 쉼표가 너무 이름 (4단어 미만)
          "Well, the forecast for tomorrow, mostly",               # 4단어 이상 뒤 쉼표
          "It is 3.5 degrees",                                     # 소수점은 문장 끝이 아님
          "The weather forecast for tomorrow shows clear skies with temperatures expected to"]:
    c = first_phrase_cut(t)
    print(f"{t[:44]!r:48s} → {t[:c]!r}" if c else f"{t[:44]!r:48s} → None")
```

```text
'A standard cup holds'                           → None
'A standard cup holds 8 ounces. That is'         → 'A standard cup holds 8 ounces.'
'Sure, I can'                                    → None
'Well, the forecast for tomorrow, mostly'        → 'Well, the forecast for tomorrow,'
'It is 3.5 degrees'                              → None
'The weather forecast for tomorrow shows clea'   → 'The weather forecast for tomorrow shows clear skies with temperatures'
```

왼쪽 열은 입력을 앞 44자까지만 보여 준 것이다(마지막 줄의 실제 입력은 "…with temperatures expected to"로 끝난다).

출력에서 볼 것: "3.5"의 점은 뒤에 공백이 없어서 문장 끝으로 보지 않는다. 마지막 줄은 구두점이 없는 긴 문장에서 10단어 안전장치가 작동한 경우다. 처음에는 규칙 1, 2만 있었는데, 실제로 돌려 보니 Qwen이 "The weather forecast … at night."처럼 쉼표 없는 26단어짜리 한 문장을 만들어서 첫 구절이 답 전체와 같아졌다. **chunker는 모델의 문체를 보고 튜닝해야 하는 부품**이다.

### 4.3 코드로 확인 — 한 턴의 token 흐름과 재생 일정 (예제 8)

무엇을 확인하는 코드인가: 날씨 질문 하나를 보내고, (1) token 도착 시각, (2) 구절이 완성되어 TTS로 넘어가는 시각, (3) warm TTS가 첫 버퍼를 내는 시각, (4) 구절을 이어서 재생할 때 끊김(underrun)이 생기는지를 본다. TTS는 별도 스레드에서 LLM과 **병렬로** 돈다. (`WarmTTS`는 5.3절에서 설명한다.)

```python
# 예제 8: 한 턴의 token 도착 시각 → 구절 자르기 → TTS 준비 시각 → 재생 일정 (끊김 검사)
import time, threading, queue
from llmlib import stream_chat, first_phrase_cut
from ttsd import WarmTTS
tts = WarmTTS(); jobs = queue.Queue(); done = []; T0 = time.perf_counter()
def worker():
    while (p := jobs.get()) is not None:
        r = tts.say(p); done.append((p, (r["t0"] - T0) * 1000 + r["first_ms"], r["audio_s"]))
th = threading.Thread(target=worker); th.start(); buf = [""]; toks = []
def on_delta(ev):
    toks.append(ev); buf[0] += ev[1]; cut = first_phrase_cut(buf[0])
    if cut:
        jobs.put(buf[0][:cut]); print(f"  {ev[0]:5.0f} ms  구절 완성 → TTS: {buf[0][:cut]!r}")
        buf[0] = buf[0][cut:].lstrip()
stream_chat("What's the weather like in San Francisco tomorrow?", on_delta=on_delta)
if buf[0].strip(): jobs.put(buf[0])
jobs.put(None); th.join(); tts.close()
gaps = [t2 - t1 for (t1, _), (t2, _) in zip(toks, toks[1:])]
print(f"token {len(toks)}개, TTFT {toks[0][0]:.0f} ms, 마지막 {toks[-1][0]:.0f} ms, "
      f"token 간격 중앙값 {sorted(gaps)[len(gaps) // 2]:.1f} ms")
play = 0.0
for p, ready, dur in done:                       # 앞 구절 재생이 끝나기 전에 다음 구절이 준비됐나
    start = max(ready, play); print(f"  재생 {start:6.0f} → {start + dur * 1000:6.0f} ms"
                                    f"  (준비 {ready:5.0f} ms, 대기 {start - ready:5.0f} ms, 끊김 {max(0, ready - play) if play else 0:4.0f} ms)")
    play = start + dur * 1000
import json; json.dump(dict(toks=toks, done=done), open("/private/tmp/claude-501/l4/ex9.json", "w"))
```

```text
    127 ms  구절 완성 → TTS: 'The weather forecast for tomorrow shows clear skies with temperatures'
    249 ms  구절 완성 → TTS: 'expected to reach around 70°F (21°C) during the day and'
    344 ms  구절 완성 → TTS: 'dropping to 50°F (10°C) at night.'
token 41개, TTFT 52 ms, 마지막 344 ms, token 간격 중앙값 7.0 ms
  재생    145 →   3775 ms  (준비   145 ms, 대기     0 ms, 끊김    0 ms)
  재생   3775 →   9375 ms  (준비   270 ms, 대기  3506 ms, 끊김    0 ms)
  재생   9375 →  13745 ms  (준비   362 ms, 대기  9014 ms, 끊김    0 ms)
```

```svg
<svg viewBox="0 0 680 265" xmlns="http://www.w3.org/2000/svg"><text x="70.0" y="16.0" font-size="12" text-anchor="start">(가) 처음 400 ms 확대</text><text x="64.0" y="44.0" font-size="12" text-anchor="end">LLM token</text><text x="64.0" y="84.0" font-size="12" text-anchor="end">TTS 첫 버퍼</text><line x1="145.1" y1="30.0" x2="145.1" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="157.1" y1="30.0" x2="157.1" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="168.7" y1="30.0" x2="168.7" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="179.3" y1="30.0" x2="179.3" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="189.9" y1="30.0" x2="189.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="200.6" y1="30.0" x2="200.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="211.3" y1="30.0" x2="211.3" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="222.0" y1="30.0" x2="222.0" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="232.8" y1="30.0" x2="232.8" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="243.3" y1="30.0" x2="243.3" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="253.4" y1="30.0" x2="253.4" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="269.4" y1="30.0" x2="269.4" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="279.9" y1="30.0" x2="279.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="290.0" y1="30.0" x2="290.0" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="299.9" y1="30.0" x2="299.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="310.0" y1="30.0" x2="310.0" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="320.2" y1="30.0" x2="320.2" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="330.2" y1="30.0" x2="330.2" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="340.3" y1="30.0" x2="340.3" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="350.4" y1="30.0" x2="350.4" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="360.4" y1="30.0" x2="360.4" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="370.6" y1="30.0" x2="370.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="380.6" y1="30.0" x2="380.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="390.7" y1="30.0" x2="390.7" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="400.6" y1="30.0" x2="400.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="410.8" y1="30.0" x2="410.8" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="420.9" y1="30.0" x2="420.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="430.9" y1="30.0" x2="430.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="447.9" y1="30.0" x2="447.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="458.3" y1="30.0" x2="458.3" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="468.5" y1="30.0" x2="468.5" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="478.7" y1="30.0" x2="478.7" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="488.6" y1="30.0" x2="488.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="498.6" y1="30.0" x2="498.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="508.8" y1="30.0" x2="508.8" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="518.6" y1="30.0" x2="518.6" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="528.8" y1="30.0" x2="528.8" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="539.0" y1="30.0" x2="539.0" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="549.0" y1="30.0" x2="549.0" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="558.9" y1="30.0" x2="558.9" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="569.0" y1="30.0" x2="569.0" y2="50.0" stroke="#4a7bd0" stroke-width="1"/><line x1="254.2" y1="24.0" x2="254.2" y2="56.0" stroke="#d0564a" stroke-width="2"/><text x="254.2" y="66.0" font-size="11" text-anchor="middle">구절 1</text><line x1="431.1" y1="24.0" x2="431.1" y2="56.0" stroke="#d0564a" stroke-width="2"/><text x="431.1" y="66.0" font-size="11" text-anchor="middle">구절 2</text><line x1="568.8" y1="24.0" x2="568.8" y2="56.0" stroke="#d0564a" stroke-width="2"/><text x="568.8" y="66.0" font-size="11" text-anchor="middle">구절 3</text><rect x="254.2" y="74.0" width="26.7" height="16.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="283.9" y="87.0" font-size="11" text-anchor="start">145</text><rect x="431.1" y="74.0" width="29.8" height="16.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="463.9" y="87.0" font-size="11" text-anchor="start">270</text><rect x="568.8" y="74.0" width="26.0" height="16.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="597.8" y="87.0" font-size="11" text-anchor="start">362</text><line x1="70.0" y1="100.0" x2="650.0" y2="100.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="100.0" x2="70.0" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="70.0" y="117.0" font-size="12" text-anchor="middle">0</text><line x1="142.5" y1="100.0" x2="142.5" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="142.5" y="117.0" font-size="12" text-anchor="middle">50</text><line x1="215.0" y1="100.0" x2="215.0" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="215.0" y="117.0" font-size="12" text-anchor="middle">100</text><line x1="287.5" y1="100.0" x2="287.5" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="287.5" y="117.0" font-size="12" text-anchor="middle">150</text><line x1="360.0" y1="100.0" x2="360.0" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="360.0" y="117.0" font-size="12" text-anchor="middle">200</text><line x1="432.5" y1="100.0" x2="432.5" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="432.5" y="117.0" font-size="12" text-anchor="middle">250</text><line x1="505.0" y1="100.0" x2="505.0" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="505.0" y="117.0" font-size="12" text-anchor="middle">300</text><line x1="577.5" y1="100.0" x2="577.5" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="577.5" y="117.0" font-size="12" text-anchor="middle">350</text><line x1="650.0" y1="100.0" x2="650.0" y2="104.0" stroke="currentColor" stroke-width="1"/><text x="650.0" y="117.0" font-size="12" text-anchor="middle">400</text><text x="650.0" y="131.0" font-size="12" text-anchor="end">ms</text><text x="70.0" y="160.0" font-size="12" text-anchor="start">(나) 재생 일정 0–14 s</text><text x="64.0" y="190.0" font-size="12" text-anchor="end">재생</text><rect x="76.0" y="176.0" width="150.4" height="20.0" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="151.2" y="191.0" font-size="12" text-anchor="middle">구절 1 · 3.6 s</text><rect x="226.4" y="176.0" width="232.0" height="20.0" fill="#4a7bd0" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="342.4" y="191.0" font-size="12" text-anchor="middle">구절 2 · 5.6 s</text><rect x="458.4" y="176.0" width="181.0" height="20.0" fill="#e08a3c" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="548.9" y="191.0" font-size="12" text-anchor="middle">구절 3 · 4.4 s</text><line x1="84.3" y1="168.0" x2="84.3" y2="204.0" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="3 2"/><text x="88.3" y="216.0" font-size="12" text-anchor="start">LLM은 0.34 s에 이미 끝남 — 나머지 13 s는 말하는 시간</text><line x1="70.0" y1="226.0" x2="650.0" y2="226.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="226.0" x2="70.0" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="70.0" y="243.0" font-size="12" text-anchor="middle">0</text><line x1="152.9" y1="226.0" x2="152.9" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="152.9" y="243.0" font-size="12" text-anchor="middle">2</text><line x1="235.7" y1="226.0" x2="235.7" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="235.7" y="243.0" font-size="12" text-anchor="middle">4</text><line x1="318.6" y1="226.0" x2="318.6" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="318.6" y="243.0" font-size="12" text-anchor="middle">6</text><line x1="401.4" y1="226.0" x2="401.4" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="401.4" y="243.0" font-size="12" text-anchor="middle">8</text><line x1="484.3" y1="226.0" x2="484.3" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="484.3" y="243.0" font-size="12" text-anchor="middle">10</text><line x1="567.1" y1="226.0" x2="567.1" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="567.1" y="243.0" font-size="12" text-anchor="middle">12</text><line x1="650.0" y1="226.0" x2="650.0" y2="230.0" stroke="currentColor" stroke-width="1"/><text x="650.0" y="243.0" font-size="12" text-anchor="middle">14</text><text x="650.0" y="257.0" font-size="12" text-anchor="end">s (요청 보낸 순간 = 0)</text></svg>
```

그림 6 — (가) 처음 400 ms: 파란 눈금이 token 41개, 빨간 선이 구절 경계, 주황 막대가 TTS 엔진이 구절을 받아 첫 버퍼를 낼 때까지(약 18–20 ms). (나) 재생은 145 ms에 시작해서 13.7 s까지 이어진다. LLM은 0.34 s에 이미 끝났다.

출력에서 볼 것:

- 첫 오디오가 **145 ms**에 준비됐다. 답 전체(344 ms)를 기다렸다면 344 + TTS ≈ 365 ms였다. streaming이 이 턴에서 약 220 ms를 벌었다.
- token 간격 7 ms = 약 140 token/s다. 사람의 말 속도는 약 150 단어/분 = 2.5 단어/s ≈ 3–4 token/s다. 이 Mac의 LLM은 **말보다 약 40배 빠르다.** 그래서 구절 2, 3은 앞 구절 재생이 끝나기 한참 전에 준비되어 기다린다(대기 3.5 s, 9.0 s). 끊김이 생길 리 없다.
- 손으로 계산 — 웨어러블이면: on-device 0.5–1B가 TPOT 50 ms(20 token/s, I1 가정)라도 말 속도의 5–6배라서 끊김은 드물다. 끊김이 위험한 경우는 (1) 클라우드 응답이 네트워크 jitter로 몇 백 ms씩 멈출 때, (2) 첫 구절이 아주 짧은데(예: "Sure.") 두 번째 구절이 긴 생성을 기다릴 때다. 그래서 재생 쪽 jitter buffer(링버퍼 깊이)와 첫 구절 최소 길이를 같이 설계한다 — 오디오 DMA 링버퍼 깊이를 정하는 것과 같은 문제다(D6 5.2).
- 이 답은 "70°F (21°C)"를 포함한다. TTS가 이것을 "seventy degrees Fahrenheit twenty-one degrees Celsius"로 읽느라 재생이 13.7 s나 된다. 음성용 LLM 출력에는 **텍스트 정규화(기호·단위·숫자 읽기)**와 "짧게 말하라"는 지시가 중요하다. system prompt의 "two or three short sentences"를 0.5B 모델은 잘 지키지 못했다.

---

## 5. TTS — 합성 시간보다 "엔진이 켜져 있나"가 중요하다

### 5.1 macOS `say`를 대역으로 쓰는 이유와 한계

웨어러블의 on-device TTS는 보통 작은 신경망(텍스트 → 음소 → acoustic 모델 → vocoder, B5 7.1)이다. 이 Mac에는 그런 모델을 받지 않기로 했으므로(새 다운로드 금지) macOS 내장 TTS를 **대역**으로 쓴다. 대역으로 볼 수 있는 것은 "구절 길이에 따라 합성 시간이 어떻게 변하나", "엔진 초기화 비용이 있나" 같은 **구조적 성질**이다. 절대 시간, 음질, 연산량은 실제 신경망 TTS와 다르다.

### 5.2 코드로 확인 — `say` 프로세스로 합성 (예제 9)

`ttslib.py`의 `synth(text, path)`는 `say -v Samantha -o path --file-format=WAVE --data-format=LEI16@16000 text`를 subprocess로 실행하고, 걸린 시간(ms)과 만들어진 WAV의 길이(s)를 돌려준다.

무엇을 확인하는 코드인가: 1단어짜리 filler, 첫 구절(8단어), 두 문장, 긴 답(31단어)을 각각 합성해서 시간과 오디오 길이를 재고, "합성 시간 = 고정비용 + 기울기 × 오디오 길이"로 직선을 맞춘다.

```python
# 예제 9: 첫 구절만 합성 vs 답 전체 합성 — 합성 시간, 오디오 길이, RTF (3회 중앙값)
import numpy as np
from l4lib import D
from ttslib import synth
texts = {"짧은 filler": "Okay.",
         "첫 구절": "A standard cup holds 8 ounces of liquid.",
         "두 문장": "A standard cup holds 8 ounces of liquid. That is about 237 milliliters.",
         "긴 답": "The current forecast indicates clear skies with temperatures expected to "
                 "reach around 70 degrees by midday, with a light breeze from the west in the "
                 "afternoon and cooler air after sunset."}
synth("warm up", f"{D}/tts_w.wav")
rows = []
for name, t in texts.items():
    r = [synth(t, f"{D}/tts_{len(rows)}.wav") for _ in range(3)]
    ms = np.median([a for a, _ in r]); dur = r[0][1]; rows.append((dur, ms))
    print(f"{name:8s} {len(t.split()):2d} 단어  합성 {ms:5.0f} ms  오디오 {dur:5.2f} s  RTF {ms / 1000 / dur:.2f}")
k, b = np.polyfit([d for d, _ in rows], [m for _, m in rows], 1)
print(f"직선 맞춤: 합성 ms ≈ {b:.0f} + {k:.0f} × (오디오 초)  → 고정비용 {b:.0f} ms")
```

```text
짧은 filler  1 단어  합성  1097 ms  오디오  0.57 s  RTF 1.91
첫 구절      8 단어  합성  1276 ms  오디오  2.41 s  RTF 0.53
두 문장     13 단어  합성  1157 ms  오디오  5.15 s  RTF 0.22
긴 답      31 단어  합성  1213 ms  오디오  9.96 s  RTF 0.12
직선 맞춤: 합성 ms ≈ 1159 + 6 × (오디오 초)  → 고정비용 1159 ms
```

출력에서 볼 것: 1단어든 31단어든 **약 1.1–1.3 s**다. 합성 자체는 오디오 1초당 약 6 ms로 거의 공짜이고, 시간의 대부분은 **프로세스 시작 + 음성 엔진 초기화**다. 그래서 `say`를 그대로 쓰면 "첫 구절만 합성"해도 이득이 거의 없다. 이것이 6절 `seq_cold` 모드의 TTS 막대가 1.6 s나 되는 이유다. 셸에서 `/usr/bin/time -p say -v Samantha -o a.aiff "Okay."`를 세 번 돌려도 `real 1.11`, `1.05`, `1.02`초가 나왔다.

### 5.3 엔진을 켜 둔 TTS — warm vs cold (예제 10)

해결책은 펌웨어에서 늘 하는 일이다: **초기화는 한 번, 요청마다는 일만.** Apple의 `AVSpeechSynthesizer.write(_:toBufferCallback:)`는 합성 결과를 오디오 버퍼 단위로 콜백에 넘겨준다. 이것으로 stdin에서 한 줄씩 받아 합성하는 작은 상주 프로세스를 만들었다(로컬 `swiftc`로 컴파일, 다운로드 없음).

```swift
// ttsd.swift — 엔진을 켜 둔 채(warm) 한 줄씩 텍스트를 받아 합성: 첫 오디오 버퍼까지 시간과 전체 시간 출력
import AVFoundation
let synth = AVSpeechSynthesizer()
let voice = AVSpeechSynthesisVoice(identifier: "com.apple.voice.compact.en-US.Samantha")
    ?? AVSpeechSynthesisVoice(language: "en-US")
func run(_ text: String) {
    let u = AVSpeechUtterance(string: text); u.voice = voice
    let t0 = DispatchTime.now().uptimeNanoseconds
    var first: Double = -1; var frames: AVAudioFrameCount = 0; var sr = 0.0
    let done = DispatchSemaphore(value: 0)
    synth.write(u) { buf in
        guard let pcm = buf as? AVAudioPCMBuffer else { return }
        if pcm.frameLength == 0 { done.signal(); return }        // 길이 0 버퍼 = 끝
        if first < 0 { first = Double(DispatchTime.now().uptimeNanoseconds - t0) / 1e6 }
        frames += pcm.frameLength; sr = pcm.format.sampleRate
    }
    while done.wait(timeout: .now() + 0.001) == .timedOut {
        RunLoop.current.run(until: Date().addingTimeInterval(0.001))
    }
    let total = Double(DispatchTime.now().uptimeNanoseconds - t0) / 1e6
    print(String(format: "first_buf_ms %.1f total_ms %.1f audio_s %.2f", first, total, Double(frames) / sr))
    fflush(stdout)
}
while let line = readLine() { run(line) }
```

```sh
swiftc -O ttsd.swift -o ttsd
printf 'Okay.\nA standard cup holds 8 ounces of liquid.\nOkay.\nA standard cup holds 8 ounces of liquid.\n' | ./ttsd
```

```text
first_buf_ms 913.8 total_ms 926.1 audio_s 0.57
first_buf_ms 23.1 total_ms 47.0 audio_s 2.41
first_buf_ms 18.6 total_ms 31.0 audio_s 0.57
first_buf_ms 23.5 total_ms 53.9 audio_s 2.41
```

출력에서 볼 것: **첫 요청만 914 ms**(cold — 음성 데이터 로드, 엔진 초기화)이고, 그다음부터는 첫 버퍼까지 **약 20 ms**, 2.4 s 분량 전체 합성도 50 ms 안이다. 또 "first_buf < total"이므로 이 엔진은 버퍼를 나눠서 내보낸다 — 첫 버퍼가 나오면 재생을 시작할 수 있다(streaming TTS).

Python 쪽(`ttsd.py`의 `WarmTTS` 클래스)은 이 프로세스를 `subprocess.Popen`으로 한 번 띄워 두고, 생성자에서 "warm up"을 한 번 보내 cold 비용을 미리 치른 뒤, `say(text)`마다 stdin에 한 줄을 쓰고 stdout에서 한 줄(`first_buf_ms … total_ms … audio_s …`)을 읽어 dict로 돌려준다.

임베디드 연결: 신경망 TTS도 똑같다. 가중치(수 MB–수십 MB)를 flash에서 DRAM으로 올리고, NPU 그래프를 컴파일·초기화(QNN context 생성, F4)하는 데 수백 ms가 걸릴 수 있다. 턴마다 이것을 하면 TTFA에 그대로 더해진다. 반대로 **상주시키면 DRAM과 대기 전력을 계속 쓴다.** 그래서 "wake word가 감지되는 순간 TTS·LLM 엔진을 미리 깨운다(pre-warm)" 같은 설계가 나온다. 사용자가 질문을 말하는 2–3 s 동안 초기화를 숨기는 것이다 — 부팅 시간을 사용자 입력 시간 뒤에 숨기는 펌웨어 기법과 같다.

---

## 6. 전체 파이프라인 — 순차 vs streaming, 그리고 가장 긴 막대

### 6.1 세 가지 실행 모드

같은 부품으로 세 가지 조립 방식을 비교한다.

| 모드 | LLM | TTS | 비유 |
|---|---|---|---|
| `seq_cold` | 답 전체를 다 받은 뒤 | `say` 새 프로세스로 답 전체 합성 | 매 명령마다 장치를 리셋하고 초기화하는 드라이버 |
| `seq_warm` | 답 전체를 다 받은 뒤 | 켜 둔 엔진으로 답 전체 합성 | 초기화는 한 번, 하지만 store-and-forward |
| `streaming` | token이 오는 대로 | 첫 구절이 완성되면 바로 합성 (LLM과 병렬) | cut-through: 헤더만 보고 바로 다음 단으로 넘김 |

endpoint 단계는 세 모드 모두 같다. 2절 endpointer(hangover 600 ms, 대역 에너지, margin 6 dB)를 파일에 돌려서 "말 끝 → endpoint 선언" 지연을 계산하고, **그 순간을 T0로 삼아** 이후 단계를 실제 벽시계로 잰다. 이렇게 한 이유: 파일을 실시간 속도로 흘려보내며 기다리는 것은 측정을 느리게 할 뿐 숫자를 바꾸지 않는다(endpoint 대기는 계산이 아니라 기다림이다). 대신 이 방식은 **ASR이 말하는 동안 미리 일을 하는 효과(streaming ASR)는 반영하지 못한다** — Whisper는 원래 그런 효과가 없으니 이 노트에서는 공정하다.

```python
# pipeline.py — 한 턴: endpoint(파일에서 계산) → ASR → LLM(stream) → TTS, 세 가지 모드
import time, threading, queue
from l4lib import load, frame_db_band, endpointer
from asrlib import transcribe
from llmlib import stream_chat, first_phrase_cut
from ttslib import synth

def run_turn(q, wav, asr_model, dev, tts, mode, hang_ms=600):
    x = load(wav)
    segs = endpointer(frame_db_band(x), hang_ms, margin_db=6, floor_db=-200)
    s, e = segs[0]; ep_ms = ((e + 1) * 0.02 - q["end"]) * 1000          # 말 끝 → endpoint 선언
    seg = x[s * 320: (e + 1) * 320]
    T0 = time.perf_counter()                                            # 이 순간 = endpoint 선언
    ms = lambda: (time.perf_counter() - T0) * 1000
    text = transcribe(asr_model, seg, dev)[0]; t_asr = ms()
    if mode == "streaming":
        jobs, ready = queue.Queue(), []
        def worker():                                                   # LLM과 병렬로 TTS
            while (p := jobs.get()) is not None:
                r = tts.say(p); ready.append((r["t0"] - T0) * 1000 + r["first_ms"])
        th = threading.Thread(target=worker); th.start()
        buf = {"txt": ""}; marks = {}
        def on_delta(ev):
            marks.setdefault("ttft", t_asr + ev[0]); buf["txt"] += ev[1]
            cut = first_phrase_cut(buf["txt"])
            if cut:                                                     # 구절 완성 → 바로 TTS로
                jobs.put(buf["txt"][:cut]); buf["txt"] = buf["txt"][cut:].lstrip()
                marks.setdefault("phrase", ms())
        stream_chat(text, on_delta=on_delta); t_llm = ms()
        if buf["txt"].strip(): jobs.put(buf["txt"])
        jobs.put(None); th.join()
        return dict(ep=ep_ms, asr=t_asr, ttft=marks["ttft"], phrase=marks.get("phrase", t_llm),
                    llm_done=t_llm, ttfa=ready[0], text=text)
    out = stream_chat(text); t_llm = ms(); ans = "".join(d for _, d in out)
    if mode == "seq_cold":                                              # say 프로세스 새로 띄움
        synth(ans, "/private/tmp/claude-501/l4/ans.wav"); ttfa = ms()
    else:                                                               # seq_warm: 켜 둔 엔진
        r = tts.say(ans); ttfa = (r["t0"] - T0) * 1000 + r["first_ms"]
    return dict(ep=ep_ms, asr=t_asr, ttft=t_asr + out[0][0], phrase=t_llm, llm_done=t_llm,
                ttfa=ttfa, text=text)
```

읽는 법: 모든 시각은 T0(endpoint 선언) 기준 누적값이다. `asr`은 ASR이 끝난 시각, `ttft`는 첫 token 도착 시각, `phrase`는 첫 구절 완성 시각, `ttfa`는 첫 오디오 버퍼가 준비된 시각이다. 말 끝 기준 TTFA는 `ep + ttfa`다. ASR 입력은 Whisper가 실제로 낸 전사이므로 **ASR 오류가 그대로 LLM에 들어간다** — 진짜 파이프라인이다. `seq_cold`의 `ttfa`는 파일 전체가 써진 시각이라 "첫 버퍼"보다 약간 늦게 잡힌다(보수적).

### 6.2 코드로 확인 — 10개 질의 × 3 모드 × 3회 (예제 11)

```python
# 예제 11: 10개 질의 × 3 모드 × 3회 — 단계별 지연(중앙값)과 TTFA(말 끝 → 첫 오디오 버퍼)
import json, os, numpy as np
from l4lib import D
from asrlib import load_model, transcribe
from ttsd import WarmTTS
from pipeline import run_turn
meta = json.load(open(f"{D}/meta.json")); m = load_model("mps"); tts = WarmTTS()
transcribe(m, np.zeros(16000, np.float32), "mps")                       # MPS warm-up
R = {mode: [] for mode in ("seq_cold", "seq_warm", "streaming")}
for q in meta:
    for mode in R:
        runs = [run_turn(q, f"{D}/q{q['i']}_clean.wav", m, "mps", tts, mode) for _ in range(3)]
        R[mode].append({k: float(np.median([r[k] for r in runs])) for k in runs[0] if k != "text"})
tts.close(); json.dump(R, open(f"{D}/pipeline.json", "w"), indent=1)
print(f"load average {os.getloadavg()[0]:.1f}   (단위 ms, 10개 질의의 중앙값, 각 질의는 3회 중앙값)")
print("mode       endpoint  ASR  →TTFT  →1st phrase  →LLM done   TTFA  (p90)")
for mode, rows in R.items():
    g = lambda k: np.median([r[k] for r in rows])
    tt = [r["ep"] + r["ttfa"] for r in rows]                            # 말 끝 기준 TTFA
    print(f"{mode:10s} {g('ep'):6.0f} {g('asr'):6.0f} {g('ttft'):6.0f} {g('phrase'):9.0f}"
          f" {g('llm_done'):10.0f} {np.median(tt):7.0f} ({np.percentile(tt, 90):4.0f})")
```

```text
load average 13.4   (단위 ms, 10개 질의의 중앙값, 각 질의는 3회 중앙값)
mode       endpoint  ASR  →TTFT  →1st phrase  →LLM done   TTFA  (p90)
seq_cold      608    230    275       561        561    2748 (3772)
seq_warm      608    221    267       512        512    1151 (1498)
streaming     608    207    256       356        623    1015 (1372)
```

(ASR·TTFT·1st phrase·LLM done 열은 endpoint 선언(T0) 기준 누적 시각, TTFA 열은 말 끝 기준이다.)

```svg
<svg viewBox="0 0 680 260" xmlns="http://www.w3.org/2000/svg"><text x="122.0" y="48.0" font-size="12" text-anchor="end">순차 + cold TTS</text><rect x="130.0" y="30.0" width="105.4" height="26.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="182.7" y="47.0" font-size="11" text-anchor="middle">endpoint</text><rect x="235.4" y="30.0" width="39.9" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="275.3" y="30.0" width="7.8" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="283.1" y="30.0" width="49.6" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="332.6" y="30.0" width="273.6" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="469.5" y="47.0" font-size="11" text-anchor="middle">TTS</text><text x="611.3" y="47.0" font-size="12" text-anchor="start">2748 ms</text><text x="122.0" y="100.0" font-size="12" text-anchor="end">순차 + warm TTS</text><rect x="130.0" y="82.0" width="105.4" height="26.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="182.7" y="99.0" font-size="11" text-anchor="middle">endpoint</text><rect x="235.4" y="82.0" width="38.3" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="273.6" y="82.0" width="8.0" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="281.6" y="82.0" width="42.6" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="324.2" y="82.0" width="5.3" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="334.5" y="99.0" font-size="12" text-anchor="start">1151 ms</text><text x="122.0" y="152.0" font-size="12" text-anchor="end">streaming</text><rect x="130.0" y="134.0" width="105.4" height="26.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="182.7" y="151.0" font-size="11" text-anchor="middle">endpoint</text><rect x="235.4" y="134.0" width="35.8" height="26.0" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="271.2" y="134.0" width="8.6" height="26.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="279.8" y="134.0" width="17.2" height="26.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><rect x="297.1" y="134.0" width="8.8" height="26.0" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="310.9" y="151.0" font-size="12" text-anchor="start">1015 ms</text><line x1="130.0" y1="186.0" x2="650.0" y2="186.0" stroke="currentColor" stroke-width="1"/><line x1="130.0" y1="186.0" x2="130.0" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="130.0" y="203.0" font-size="12" text-anchor="middle">0</text><line x1="216.7" y1="186.0" x2="216.7" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="216.7" y="203.0" font-size="12" text-anchor="middle">500</text><line x1="303.3" y1="186.0" x2="303.3" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="303.3" y="203.0" font-size="12" text-anchor="middle">1000</text><line x1="390.0" y1="186.0" x2="390.0" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="390.0" y="203.0" font-size="12" text-anchor="middle">1500</text><line x1="476.7" y1="186.0" x2="476.7" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="476.7" y="203.0" font-size="12" text-anchor="middle">2000</text><line x1="563.3" y1="186.0" x2="563.3" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="563.3" y="203.0" font-size="12" text-anchor="middle">2500</text><line x1="650.0" y1="186.0" x2="650.0" y2="190.0" stroke="currentColor" stroke-width="1"/><text x="650.0" y="203.0" font-size="12" text-anchor="middle">3000</text><text x="650.0" y="218.0" font-size="12" text-anchor="end">ms (사용자 말 끝 = 0) · 10개 질의 중앙값</text><rect x="30.0" y="230.0" width="14.0" height="12.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="50.0" y="241.0" font-size="12" text-anchor="start">endpoint 대기</text><rect x="155.0" y="230.0" width="14.0" height="12.0" fill="#3f9a6b" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="175.0" y="241.0" font-size="12" text-anchor="start">ASR</text><rect x="280.0" y="230.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="300.0" y="241.0" font-size="12" text-anchor="start">LLM TTFT</text><rect x="405.0" y="230.0" width="14.0" height="12.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="425.0" y="241.0" font-size="12" text-anchor="start">LLM 첫 구절/끝</text><rect x="530.0" y="230.0" width="14.0" height="12.0" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="550.0" y="241.0" font-size="12" text-anchor="start">TTS 첫 버퍼</text></svg>
```

그림 7 — 세 모드의 단계별 막대(10개 질의 중앙값, load 13.4). 단계 길이는 누적 시각의 중앙값끼리 뺀 근사라서 막대 합이 TTFA 중앙값과 몇 ms 다를 수 있다. 회색 endpoint 막대는 세 모드에서 똑같고, `seq_cold`의 빨간 TTS 막대가 압도적이다.

### 6.3 손으로 분해 — 무엇이 지배하나

streaming 행을 말 끝 기준으로 풀어 쓴다.

```
endpoint 대기        608 ms   ← 알고리즘 지연 (hangover 600 + frame 양자화)
ASR (Whisper, MPS)  +207 ms   → 815
LLM TTFT            + 49 ms   → 864   (256 − 207)
첫 구절까지 decode   +100 ms   → 964   (356 − 256), 첫 구절 K ≈ 12 token × 약 8 ms
TTS 첫 버퍼          + 51 ms   → 1015  (warm 엔진 약 20 ms + 스레드·큐 지연)
───────────────────────────────
TTFA               1015 ms   (endpoint 비중 608 / 1015 = 60 %)
```

말로 하면: **이 Mac에서 TTFA의 60 %는 "아무 계산도 하지 않고 기다리는 시간"이다.** M2 GPU 위의 0.5B LLM은 TTFT 49 ms, 첫 구절 100 ms로 이미 충분히 빠르다. 여기서 LLM을 두 배 빠르게 해도 TTFA는 약 75 ms(7 %)밖에 안 준다. hangover를 600 → 300 ms로 줄이면 300 ms(30 %)가 준다 — 대신 2절에서 본 것처럼 말을 40 % 확률로 자른다.

세 모드끼리 비교:

| 비교 | TTFA 차이 | 어디서 왔나 |
|---|---|---|
| `seq_cold` → `seq_warm` | 2748 → 1151 (−1597 ms) | TTS 엔진 초기화(약 1.1 s)와 합성 대기. **가장 큰 단일 개선** |
| `seq_warm` → `streaming` | 1151 → 1015 (−136 ms) | 첫 구절(356 ms)에서 TTS 시작 vs 답 끝(512 ms)에서 시작 |
| `streaming`의 남은 몫 | 1015 중 608 | endpoint. 다음 개선 대상은 계산이 아니라 **endpointing 알고리즘** |

streaming의 이득이 136 ms로 작아 보이는 것은 이 Mac의 decode가 매우 빠르기 때문이다(token당 약 8 ms). decode가 느려질수록 이득이 커진다 — 6.5절에서 계산한다. 또 streaming 행의 `LLM done`(623)이 `seq_warm`(512)보다 늦은 것은 TTS 스레드가 같은 CPU를 쓰며 LLM 클라이언트와 경쟁하기 때문이다. 사용자는 TTFA만 느끼므로 문제없지만, 기기에서 같은 코어를 나눠 쓰면 이런 간섭이 생긴다는 신호다.

### 6.4 같은 코드, 다른 부하 — 측정 잡음은 어디로 가나

같은 `예제 11`을 다른 작업이 더 많이 돌던 때(load average 28.5) 실행하면 이렇게 나왔다.

```text
load average 28.5   (단위 ms, 10개 질의의 중앙값, 각 질의는 3회 중앙값)
mode       endpoint  ASR  →TTFT  →1st phrase  →LLM done   TTFA  (p90)
seq_cold      608    369    463       947        947    3549 (4485)
seq_warm      608    339    422       879        879    1514 (1993)
streaming     608    352    446       705       1025    1338 (1525)
```

출력에서 볼 것: endpoint 608 ms는 **한 자리도 변하지 않았다** — 알고리즘 지연은 CPU 부하와 무관하다. 반면 계산 단계(ASR 207 → 352, 첫 구절 356 → 705)는 부하에 따라 1.5–2배 늘었다. 웨어러블에서도 같다: 다른 작업(카메라, BLE 동기화, 열 throttling, K4)이 같은 SoC를 쓰면 계산 단계만 늘어난다. 그래서 TTFA 예산은 **p50이 아니라 p90/p99**로, 그리고 **부하가 걸린 상태에서** 검증해야 한다(I1 8절, D6 2.5절).

### 6.5 손으로 → 코드로: 느린 엔진에서는 streaming이 더 중요하다 (예제 12)

이 Mac은 웨어러블보다 훨씬 빠르다. 같은 답(같은 token 수)을 느린 엔진에 올리면 어떻게 되나? 먼저 손으로:

```
TTFA_순차     = endpoint + ASR + TTFT + (N − 1) × TPOT + TTS 첫 청크 + 출력 버퍼
TTFA_streaming = endpoint + ASR + TTFT + (K − 1) × TPOT + TTS 첫 청크 + 출력 버퍼
절약           = (N − K) × TPOT
```

말로 하면: streaming의 이득은 "첫 구절 뒤에 남은 token 수 × token당 시간"이다. 이 노트의 답에서 N ≈ 23, K ≈ 12라면 11 × TPOT다. TPOT 8 ms(이 Mac)면 90 ms, 50 ms(웨어러블 가정)면 550 ms다.

무엇을 확인하는 코드인가: 실제 답 10개의 K와 N을 세고, 이 Mac(측정값)과 두 가정 시나리오에 넣는다. 가정 숫자는 I1의 예산표와 같은 자릿수로 골랐다(웨어러블: 학습된 endpointer로 400 ms, streaming ASR final 100 ms, on-device 0.5B TTFT 300 ms·TPOT 50 ms / 클라우드: TTFT에 왕복 포함 450 ms, TPOT 15 ms).

```python
# 예제 12: 같은 답(token 수)을 "더 느린 엔진"에 올리면 — 순차 vs streaming TTFA (가정 숫자 + 측정 token 수)
import json, numpy as np
from l4lib import D
from llmlib import stream_chat, first_phrase_cut
meta = json.load(open(f"{D}/meta.json")); K, N = [], []
for q in meta:                                    # 실제 답을 만들어 첫 구절까지 token 수(K)와 전체(N)를 센다
    out = stream_chat(q["ref"]); txt = ""
    for i, (_, d) in enumerate(out):
        txt += d
        if first_phrase_cut(txt): K.append(i + 1); break
    else: K.append(len(out))
    N.append(len(out))
print(f"첫 구절 token K 중앙값 {np.median(K):.0f} (범위 {min(K)}–{max(K)}), 전체 N 중앙값 {np.median(N):.0f}")
# 시나리오: (이름, endpoint, ASR final, TTFT, TPOT, TTS 첫 청크, 출력 버퍼) — 이 Mac 행만 측정, 나머지 가정
S = [("이 Mac (측정 중앙값)", 608, 207, 49, 10, 30, 0),
     ("웨어러블 on-device (가정)", 400, 100, 300, 50, 120, 40),
     ("클라우드 LLM (가정)", 400, 100, 450, 15, 120, 40)]
for name, ep, asr, ttft, tpot, tts, buf in S:
    seq = [ep + asr + ttft + (n - 1) * tpot + tts + buf for n in N]
    stm = [ep + asr + ttft + (k - 1) * tpot + tts + buf for k in K]
    print(f"{name:20s} 순차 {np.median(seq):5.0f} ms   streaming {np.median(stm):5.0f} ms"
          f"   절약 {np.median(seq) - np.median(stm):5.0f} ms")
```

```text
첫 구절 token K 중앙값 12 (범위 6–18), 전체 N 중앙값 23
이 Mac (측정 중앙값)       순차  1114 ms   streaming  1004 ms   절약   110 ms
웨어러블 on-device (가정)  순차  2060 ms   streaming  1510 ms   절약   550 ms
클라우드 LLM (가정)        순차  1440 ms   streaming  1275 ms   절약   165 ms
```

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg"><text x="182.0" y="50.0" font-size="12" text-anchor="end">이 Mac (측정)</text><rect x="190.0" y="24.0" width="200.5" height="20.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="395.5" y="39.0" font-size="12" text-anchor="start">순차 1114</text><rect x="190.0" y="48.0" width="180.7" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="375.7" y="63.0" font-size="12" text-anchor="start">streaming 1004 (−110)</text><text x="182.0" y="112.0" font-size="12" text-anchor="end">웨어러블 on-device (가정)</text><rect x="190.0" y="86.0" width="370.8" height="20.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="565.8" y="101.0" font-size="12" text-anchor="start">순차 2060</text><rect x="190.0" y="110.0" width="271.8" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="466.8" y="125.0" font-size="12" text-anchor="start">streaming 1510 (−550)</text><text x="182.0" y="174.0" font-size="12" text-anchor="end">클라우드 LLM (가정)</text><rect x="190.0" y="148.0" width="259.2" height="20.0" fill="#888" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="454.2" y="163.0" font-size="12" text-anchor="start">순차 1440</text><rect x="190.0" y="172.0" width="229.5" height="20.0" fill="#4a7bd0" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="424.5" y="187.0" font-size="12" text-anchor="start">streaming 1275 (−165)</text><line x1="190.0" y1="204.0" x2="640.0" y2="204.0" stroke="currentColor" stroke-width="1"/><line x1="190.0" y1="204.0" x2="190.0" y2="208.0" stroke="currentColor" stroke-width="1"/><text x="190.0" y="221.0" font-size="12" text-anchor="middle">0</text><line x1="280.0" y1="204.0" x2="280.0" y2="208.0" stroke="currentColor" stroke-width="1"/><text x="280.0" y="221.0" font-size="12" text-anchor="middle">500</text><line x1="370.0" y1="204.0" x2="370.0" y2="208.0" stroke="currentColor" stroke-width="1"/><text x="370.0" y="221.0" font-size="12" text-anchor="middle">1000</text><line x1="460.0" y1="204.0" x2="460.0" y2="208.0" stroke="currentColor" stroke-width="1"/><text x="460.0" y="221.0" font-size="12" text-anchor="middle">1500</text><line x1="550.0" y1="204.0" x2="550.0" y2="208.0" stroke="currentColor" stroke-width="1"/><text x="550.0" y="221.0" font-size="12" text-anchor="middle">2000</text><line x1="640.0" y1="204.0" x2="640.0" y2="208.0" stroke="currentColor" stroke-width="1"/><text x="640.0" y="221.0" font-size="12" text-anchor="middle">2500</text><line x1="370.0" y1="18.0" x2="370.0" y2="204.0" stroke="#d0564a" stroke-width="1.2" stroke-dasharray="5 3"/><text x="374.0" y="14.0" font-size="12" text-anchor="start">목표 1000 ms</text><text x="640.0" y="236.0" font-size="12" text-anchor="end">TTFA ms (중앙값, 출력 버퍼 포함)</text></svg>
```

그림 8 — 같은 답을 세 환경에 올렸을 때의 TTFA. 이 Mac 행만 측정값(TPOT은 4.3절에서 잰 token 간격 7–10 ms 중 보수적인 10 ms, 그래서 예제 11의 1015와 약간 다르다)이고 나머지 두 행은 가정이다. 웨어러블 on-device는 streaming을 해도 목표 1000 ms를 크게 넘는다.

출력에서 볼 것:

- 이득이 이 Mac 110 ms → 웨어러블 550 ms로 다섯 배가 된다. **느린 엔진일수록 streaming은 선택이 아니라 필수다.**
- 웨어러블 on-device는 streaming을 해도 1510 ms다. 무엇을 더 줄여야 하나? TTFT 300(prefix KV 캐시로 줄이기, I1 3.3), 첫 구절 K = 12 token × 50 ms = 600 ms(첫 구절을 짧게: 모델이 "Sure."나 "It's 8 ounces."처럼 **짧은 첫 문장으로 시작하도록** 학습·prompt), 그리고 여전히 endpoint 400.
- 클라우드는 TPOT이 빨라서 이득은 작지만, TTFT 450 ms(네트워크 왕복 포함)가 크다. 연결이 나쁘면 이 숫자가 몇 배가 된다 — 그래서 L3의 라우팅(짧고 쉬운 질문은 기기, 나머지는 클라우드)이 필요하다.
- 마지막 수단은 **체감 지연 가리기**다: endpoint 직후 짧은 확인음(earcon)이나 "Let me check." 같은 미리 합성해 둔 filler를 재생하면 사용자는 300 ms 안에 반응을 듣는다(D6 8.3). 진짜 TTFA는 그대로지만 "기기가 들었다"는 신호가 간다.

---

## 7. Barge-in — 사용자가 응답 도중에 끼어들 때

### 7.1 무엇이 일어나야 하나

**barge-in**은 기기가 말하는 도중에 사용자가 말을 시작하면, 기기가 즉시 입을 다물고 새 말을 듣는 기능이다. 사람끼리의 대화에서는 당연한 일이지만 기계에서는 세 가지가 동시에 일어나야 한다.

1. **감지**: 마이크에서 사용자 목소리를 감지한다. 문제는 마이크에 **기기 자신의 스피커 소리(에코)**도 들어온다는 것이다.
2. **재생 중단**: 스피커로 나가는 오디오를 멈춘다. 이미 출력 버퍼(DMA 링버퍼)에 들어간 샘플은 비우거나 짧게 fade-out한다(뚝 끊으면 클릭 소리).
3. **생성 취소**: 아직 돌고 있는 LLM 생성과 TTS 합성을 취소한다. 안 그러면 다음 질문이 줄을 선다.

```svg
<svg viewBox="0 0 680 262" xmlns="http://www.w3.org/2000/svg"><text x="112.0" y="52.0" font-size="12" text-anchor="end">스피커 (TTS)</text><text x="112.0" y="92.0" font-size="12" text-anchor="end">마이크: 사용자</text><text x="112.0" y="132.0" font-size="12" text-anchor="end">VAD (onset)</text><text x="112.0" y="172.0" font-size="12" text-anchor="end">LLM stream</text><rect x="120.0" y="38.0" width="270.0" height="20.0" fill="#3f9a6b" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="255.0" y="53.0" font-size="12" text-anchor="middle">응답 재생 중</text><rect x="372.0" y="38.0" width="18.0" height="20.0" fill="#d0564a" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="392.2" y="53.0" font-size="11" text-anchor="start">← 출력 버퍼 비움 40 ms (가정)</text><rect x="345.0" y="78.0" width="315.0" height="20.0" fill="#4a7bd0" fill-opacity="0.45" stroke="currentColor" stroke-width="0.5"/><text x="502.5" y="93.0" font-size="12" text-anchor="middle">"How many ounces…" (2.0 s 시작)</text><rect x="345.0" y="118.0" width="27.0" height="20.0" fill="#e08a3c" fill-opacity="0.55" stroke="currentColor" stroke-width="0.5"/><text x="374.2" y="133.0" font-size="11" text-anchor="start">3 frame 연속 = 60 ms → barge-in 이벤트</text><rect x="120.0" y="158.0" width="252.0" height="20.0" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor" stroke-width="0.5"/><text x="246.0" y="173.0" font-size="12" text-anchor="middle">남은 답 생성 중</text><line x1="372.0" y1="150.0" x2="372.0" y2="186.0" stroke="#d0564a" stroke-width="2"/><text x="374.2" y="173.0" font-size="11" text-anchor="start">끊음 → 새 질문 첫 token까지 48 ms (측정)</text><line x1="345.0" y1="30.0" x2="345.0" y2="200.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/><line x1="372.0" y1="30.0" x2="372.0" y2="200.0" stroke="currentColor" stroke-width="1" stroke-dasharray="3 3"/><line x1="120.0" y1="200.0" x2="660.0" y2="200.0" stroke="currentColor" stroke-width="1"/><line x1="120.0" y1="200.0" x2="120.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="120.0" y="217.0" font-size="12" text-anchor="middle">1500</text><line x1="210.0" y1="200.0" x2="210.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="210.0" y="217.0" font-size="12" text-anchor="middle">1700</text><line x1="300.0" y1="200.0" x2="300.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="300.0" y="217.0" font-size="12" text-anchor="middle">1900</text><line x1="390.0" y1="200.0" x2="390.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="390.0" y="217.0" font-size="12" text-anchor="middle">2100</text><line x1="480.0" y1="200.0" x2="480.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="480.0" y="217.0" font-size="12" text-anchor="middle">2300</text><line x1="570.0" y1="200.0" x2="570.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="570.0" y="217.0" font-size="12" text-anchor="middle">2500</text><line x1="660.0" y1="200.0" x2="660.0" y2="204.0" stroke="currentColor" stroke-width="1"/><text x="660.0" y="217.0" font-size="12" text-anchor="middle">2700</text><text x="660.0" y="232.0" font-size="12" text-anchor="end">ms (응답 재생 시작 기준)</text><text x="120.0" y="252.0" font-size="12" text-anchor="start">에코: 스피커 소리가 마이크로 되돌아오면 VAD가 '사용자'로 착각 → AEC 또는 재생 참조 gate 필요 (6절)</text></svg>
```

그림 9 — barge-in 타임라인 (응답 재생 시작 기준). 사용자가 2000 ms에 말을 시작하면 VAD가 3 frame(60 ms) 뒤 이벤트를 내고, 재생 중단(출력 버퍼 40 ms, 가정)과 LLM 취소(측정 48 ms)가 병렬로 일어난다.

펌웨어 비유: 이것은 **abort 경로**다. NVMe의 Abort 명령, 또는 DMA 전송 도중 상위 계층이 취소를 요청하는 경우와 같다. 정상 경로만 만들고 abort 경로를 대충 만들면 "취소했는데 데이터가 계속 나온다", "취소 직후 다음 명령이 이전 명령 뒤에 줄을 선다" 같은 버그가 생긴다. 음성 비서에서 이것은 "그만!"이라고 했는데 계속 말하는 기기, 또는 다음 질문에 한참 뒤에 답하는 기기로 나타난다.

### 7.2 코드로 확인 — LLM stream을 끊으면 정말 서버가 멈추나 (예제 13)

무엇을 확인하는 코드인가: 긴 이야기를 요청하고 0.3 s 뒤에 사용자가 끼어들었다고 하자. (a) `cancel`: 그 순간 HTTP 연결을 닫고 곧바로 새 질문을 보낸다. (b) `wait`: 연결을 유지해서 옛 답이 끝날 때까지 받은 뒤 새 질문을 보낸다. "끼어든 순간 → 새 질문의 첫 token"까지를 잰다. 서버는 slot 1개(`-np 1`)라서 옛 생성이 안 멈추면 새 질문은 기다려야 한다.

```python
# 예제 13: barge-in 때 LLM stream을 끊으면 — 끼어든 순간부터 "새 질문의 첫 token"까지 시간
import time, numpy as np
from llmlib import stream_chat
LONG, NEW = "Tell me a long story about a robot who learns to cook.", "What time is it in Tokyo?"
base = [stream_chat(NEW, max_tokens=8)[0][0] for _ in range(3)]
print(f"서버 idle일 때 새 질문 TTFT: {np.median(base):.0f} ms")
for mode in ("cancel", "wait"):
    r = []
    for _ in range(3):
        t0 = time.perf_counter()
        barge = lambda: time.perf_counter() - t0 > 0.3          # 응답 시작 0.3 s 뒤 사용자가 끼어듦
        out = stream_chat(LONG, max_tokens=300, stop_flag=barge if mode == "cancel" else None)
        t_send = time.perf_counter()
        first = stream_chat(NEW, max_tokens=8)[0][0]             # 곧바로 새 질문
        r.append(((t_send - t0) * 1000 + first - 300, len(out)))
    print(f"{mode:6s}: 끼어듦 → 새 질문 첫 token {np.median([a for a, _ in r]):5.0f} ms"
          f"   (옛 답에서 받은 token {int(np.median([b for _, b in r]))}개)")
```

```text
서버 idle일 때 새 질문 TTFT: 37 ms
cancel: 끼어듦 → 새 질문 첫 token    48 ms   (옛 답에서 받은 token 32개)
wait  : 끼어듦 → 새 질문 첫 token   924 ms   (옛 답에서 받은 token 91개)
```

출력에서 볼 것:

- `cancel`은 48 ms로 서버 idle일 때의 TTFT(37 ms)와 거의 같다. 즉 **연결을 닫자 llama-server가 바로 생성을 멈추고 slot을 비웠다.** 남은 11 ms는 "끼어든 순간 이후 다음 token이 도착해서 stop_flag를 확인할 때까지"(token 간격 약 8 ms)와 연결 정리 비용이다. 취소 확인이 token 단위로만 일어난다는 점은 기억해 둘 것 — 기기에서 TPOT이 50 ms면 취소 반응도 최대 50 ms 늦어진다.
- `wait`은 924 ms다. 옛 답(91 token)이 끝날 때까지 새 질문이 줄을 섰다. 이 Mac은 token당 약 8 ms라 1초 미만이지만, TPOT 50 ms인 기기에서 300 token 이야기를 끝까지 기다리면 15 s다. **취소 경로가 없으면 barge-in 지연 = 남은 생성 시간**이다.
- 취소는 LLM에서 끝나지 않는다. TTS 큐에 쌓인 구절(4.3절에서 구절 2, 3은 수 초간 대기했다)도 버려야 하고, 대화 기록(context)에는 **실제로 사용자에게 들린 부분까지만** 남겨야 한다. 들리지 않은 답을 context에 넣으면 LLM은 사용자가 그 내용을 들었다고 착각한다(L2).

### 7.3 에코 — barge-in을 망치는 주범 (예제 14)

웨어러블은 스피커와 마이크가 몇 cm 거리다. 기기가 말하는 소리가 마이크에 사용자 목소리보다 **크게** 들어올 수 있다. 그러면 VAD는 기기 자신의 목소리를 "사용자가 말한다"로 착각하고 스스로 말을 끊는다(self-interruption). 이것을 막는 것이 **AEC(acoustic echo cancellation, G4)**다. AEC는 스피커로 내보낸 신호(reference)를 알고 있으므로, 마이크 신호에서 "reference가 공간을 거쳐 돌아온 모양"을 추정해 빼낸다. 완벽하지 않아서 **잔류 에코**가 남는다.

무엇을 확인하는 코드인가: 마이크 = (에코: 응답 음성을 2 ms 지연·감쇠) + (사용자: 2.0 s에 "How many ounces are in a cup?" 시작) + 잡음(사용자 대비 −40 dB). 잔류 에코의 크기를 사용자 목소리 대비 +10 dB(AEC 없음, 스피커가 가까움)부터 −50 dB(좋은 AEC)까지 바꾸며 두 가지 감지기를 비교한다.

- 단순 VAD: 2절과 같은 에너지 VAD의 onset 부분(3 frame 연속).
- 재생 참조 gate: 기기는 자기가 무엇을 재생 중인지 안다. 그래서 문턱을 "noise floor + 6 dB"와 "**재생 신호 레벨 + 추정 에코비 + 3 dB**" 중 큰 쪽으로 올린다. AEC가 없거나 약할 때 쓰는 값싼 대책이다.

```python
# 예제 14: TTS 재생 중 사용자가 끼어듦(2.0 s) — 마이크 = 에코 + 사용자 + 잡음. AEC 잔류 에코 수준별 VAD 반응
import numpy as np
from l4lib import D, SR, load, frame_db_band
rng = np.random.default_rng(1)
tts = load(f"{D}/bi_tts.wav")[: 5 * SR]                    # 재생 중인 응답 (5 s)
user = load(f"{D}/q2_clean.wav")[int(0.8 * SR):]            # "How many ounces are in a cup?"
u = np.zeros(5 * SR); u[2 * SR:] = user[: 3 * SR]           # 사용자가 2.0 s에 말 시작
u_rms = np.sqrt(np.mean(user[: int(1.9 * SR)] ** 2)); t_rms = np.sqrt(np.mean(tts[tts != 0] ** 2))
def onset(db, ref_db=None, margin=6.0, gate=None):
    nf = np.min(db[:10]); run = 0                          # 처음 10 frame 최솟값으로 floor 초기화
    for k, e in enumerate(db):
        thr = nf + margin if gate is None else max(nf + margin, ref_db[k] + gate)
        sp = e > thr
        if not sp: nf = 0.95 * nf + 0.05 * e
        run = run + 1 if sp else 0
        if run >= 3: return (k + 1) * 0.02                 # 60 ms 연속이면 "사용자가 말한다"
    return None
ref_db = frame_db_band(tts)
print("에코/사용자 비(dB)   단순 VAD               재생 참조 gate (에코비 + 3 dB)")
for er in (10, 0, -10, -20, -30, -40, -50):                           # 잔류 에코 레벨 (사용자 목소리 대비)
    echo = np.zeros(5 * SR); echo[32:] = tts[:-32] * (u_rms / t_rms) * 10 ** (er / 20)  # 2 ms 지연
    mic = echo + u + rng.standard_normal(5 * SR) * u_rms * 10 ** (-40 / 20)
    db = frame_db_band(mic); res = []
    for gate in (None, er + 3):                             # gate: 참조 신호 레벨 + 추정 에코비 + 3 dB
        t = onset(db, ref_db, gate=gate)
        res.append("놓침" if t is None else f"{'오검출' if t < 2.0 else '검출'} {t:4.2f} s"
                   + ("" if t < 2.0 else f" (+{(t - 2) * 1000:3.0f} ms)"))
    print(f"   {er:+4d}            {res[0]:20s} {res[1]}")
```

`bi_tts.wav`는 응답 문장 하나를 `say -v Samantha -o bi_tts.wav --file-format=WAVE --data-format=LEI16@16000 "The weather forecast for tomorrow shows clear skies …"`로 만든 것이다.

```text
에코/사용자 비(dB)   단순 VAD               재생 참조 gate (에코비 + 3 dB)
    +10            오검출 0.08 s           놓침
     +0            오검출 0.08 s           검출 3.68 s (+1680 ms)
    -10            오검출 0.08 s           검출 2.20 s (+200 ms)
    -20            오검출 0.08 s           검출 2.06 s (+ 60 ms)
    -30            오검출 0.08 s           검출 2.06 s (+ 60 ms)
    -40            검출 2.06 s (+ 60 ms)  검출 2.06 s (+ 60 ms)
    -50            검출 2.06 s (+ 60 ms)  검출 2.06 s (+ 60 ms)
```

출력에서 볼 것:

- **단순 VAD는 잔류 에코가 −30 dB만 돼도 0.08 s에 오검출**한다. 재생이 시작되자마자 기기가 제 목소리에 놀라 말을 끊는 셈이다. 잡음 floor(−40 dB)보다 에코가 충분히 작아지는 −40 dB부터야 사용자를 제대로 잡는다(2.06 s = 60 ms onset). 손으로 확인: 문턱은 noise floor + 6 dB = −34 dB이므로, 에코가 −34 dB보다 크면 VAD는 에코를 말소리로 본다. −30은 넘고 −40은 못 넘는다.
- **재생 참조 gate**는 오검출을 없앤다. 대신 에코가 사용자보다 크면(+10 dB) 사용자를 아예 놓치고, 비슷하면(0 dB) 1.68 s나 늦게 잡는다. 에코가 −20 dB 이하면 60 ms로 단순 VAD와 같다.
- 결론: barge-in의 품질은 **AEC가 에코를 얼마나 깎느냐(ERLE, echo return loss enhancement)**에 달려 있다. 이 시뮬레이션에서는 잔류 에코가 사용자 목소리보다 약 −40 dB(잡음 수준)까지 내려와야 단순 VAD가 안전했다. 스피커가 사용자 목소리보다 10 dB 크게 들어오는 기기라면 AEC + 잔류 에코 억제로 50 dB 가까이 깎아야 한다는 뜻이다. 그래서 실제 시스템은 AEC 뒤에 **"사용자 목소리냐"를 판단하는 ML 기반 barge-in 검출기**를 두거나, KWS("stop", 호출어)로만 barge-in을 허용하기도 한다.
- 이 시뮬레이션은 에코 경로를 "2 ms 지연 + 감쇠"로 단순화했다. 실제 에코는 방·몸·케이스를 거친 잔향(수십–수백 ms의 impulse response), 스피커의 비선형 왜곡(작은 스피커를 크게 울리면 심함)이 있어 AEC가 더 어렵다(G4).

### 7.4 barge-in 지연 예산 — 손으로 계산

```
사용자 말 시작
  + VAD onset          60 ms   (3 frame × 20 ms, 예제 14)
  + 이벤트 전달         ~1 ms   (DSP → SoC IRQ/메일박스, 가정)
  + 재생 중단           40 ms   (출력 DMA 버퍼 깊이 또는 fade-out, 가정)  ─┐ 병렬
  + LLM 취소 + 새 질문   48 ms   (예제 13 측정, 이 Mac)                   ─┘
────────────────────────────────
스피커가 조용해지기까지 ≈ 60 + 1 + 40 ≈ 100 ms
```

말로 하면: 사용자가 말을 시작한 뒤 약 0.1 s 안에 기기가 조용해진다. 사람끼리 말이 겹칠 때 한쪽이 멈추는 시간과 비슷한 자릿수다. 줄이려면 onset frame 수(오검출과 trade-off)와 출력 버퍼 깊이(underrun과 trade-off)를 줄인다 — 둘 다 2절, 4.3절에서 본 trade-off가 다시 나타난다.

---

## 8. 품질 문제 — 빠른 것만으로는 부족하다

### 8.1 ASR 오류가 LLM으로 흘러가면 (예제 15)

3.4절의 잡음 전사를 그대로 LLM에 넣어 본다. 그리고 system prompt에 "숫자가 이상하거나 말이 안 되면 실행하지 말고 짧게 되물어라"를 덧붙였을 때 달라지는지 본다.

```python
# 예제 15: ASR 오류가 LLM으로 흘러가면 — 실제 noisy 전사를 그대로 넣었을 때의 답 (greedy, 재현 가능)
import llmlib
from llmlib import stream_chat
cases = ["Set a timer 410 minutes and remind me to check the other.",   # q1 SNR 5 dB 전사
         "Call my mom under a sailboat.",                                # q3 clean 전사
         "You know, the light in the living room."]                      # q8 SNR 5 dB 전사
CONFIRM = (" If the request contains an unusual number or does not make sense, do not act;"
           " ask one short confirmation question instead.")
for policy, sys in (("기본", llmlib.SYS), ("확인 질문 지시", llmlib.SYS + CONFIRM)):
    llmlib.SYS = sys
    print(f"[{policy}]")
    for c in cases:
        ans = "".join(d for _, d in stream_chat(c, max_tokens=40))
        print(f"  {c!r:62s}\n    → {ans!r}")
```

```text
[기본]
  'Set a timer 410 minutes and remind me to check the other.'   
    → "I'm ready! Please set your timer for 410 minutes and I'll check back with you."
  'Call my mom under a sailboat.'                               
    → "I'm sorry, but I can't assist with that request."
  'You know, the light in the living room.'                     
    → "I'm just here to listen and provide information. I don't have the ability to see or interact with physical objects like a living room."
[확인 질문 지시]
  'Set a timer 410 minutes and remind me to check the other.'   
    → "I'm ready! Please set your timer for 410 minutes and I'll check it right away."
  'Call my mom under a sailboat.'                               
    → "Understood! Let's proceed with your request. Is there anything specific you'd like to know about your mother?"
  'You know, the light in the living room.'                     
    → "I'm sorry, but I can't assist with that."
```

출력에서 볼 것:

- "410 minutes"를 0.5B 모델은 **그대로 받아들인다**(두 정책 모두). 6시간 50분짜리 타이머가 설정될 것이다. "확인 질문 지시"를 넣어도 0.5B 모델은 지시를 따르지 않았다. **작은 LLM에게 안전장치를 맡기면 안 된다.**
- "Call my mom under a sailboat"는 기본 정책에서 거절, 확인 정책에서는 엉뚱한 되물음이 나왔다. "You know, the light in the living room"은 "Turn off"를 잃어서 의도 자체가 사라졌다. LLM이 아무리 좋아도 **없는 정보는 복구할 수 없다.**
- 그래서 확인(confirmation)은 LLM 앞뒤의 **규칙 기반 층**에 둔다: (1) ASR confidence가 낮으면 되묻기(다음 예제), (2) 숫자·시간 슬롯이 그럴듯한 범위를 벗어나면 되묻기(타이머 > 3시간 등), (3) 되돌릴 수 없는 행동(전화 걸기, 메시지 보내기, 결제)은 항상 확인. 펌웨어의 "입력 검증은 신뢰 경계에서" 원칙과 같다.

### 8.2 ASR confidence로 되묻기 (예제 16)

Whisper decoder는 token마다 확률을 낸다. 생성된 token들의 평균 log 확률을 "이 전사를 얼마나 확신하나"의 점수로 쓸 수 있다. 무엇을 확인하는 코드인가: clean, 10 dB, 5 dB의 30개 전사에서 평균 log-prob를 구하고, 문턱 아래면 "확인 질문"을 한다고 할 때 실제 오류를 얼마나 잡는지 본다.

```python
# 예제 16: Whisper 평균 token log-prob를 "ASR confidence"로 — 오류 있는 전사를 골라낼 수 있나
import json, numpy as np, torch
from l4lib import D, load
from asrlib import proc, load_model
meta = json.load(open(f"{D}/meta.json")); asr = json.load(open(f"{D}/asr.json")); m = load_model("cpu")
rows = []
for c in ("clean", "snr10", "snr5"):
    for q in meta:
        feat = proc(load(f"{D}/q{q['i']}_{c}.wav"), sampling_rate=16000, return_tensors="pt").input_features
        with torch.no_grad():
            g = m.generate(feat, language="en", task="transcribe", max_new_tokens=60,
                           return_dict_in_generate=True, output_scores=True)
        lp = m.compute_transition_scores(g.sequences, g.scores, normalize_logits=True)[0]
        r = asr[f"q{q['i']}_{c}"]; rows.append((float(lp.mean()), r["err"] / r["n"]))
lp, w = np.array(rows).T
for thr in (-0.3, -0.5, -0.7):
    flag = lp < thr
    print(f"confidence < {thr}: 확인 질문 {flag.sum():2d}/30회 | 그중 실제 오류(WER>0) {np.sum(flag & (w > 0)):2d}"
          f" | 통과했는데 오류 {np.sum(~flag & (w > 0)):2d} (WER>20 %: {np.sum(~flag & (w > 0.2))})")
print(f"상관계수(log-prob, WER) = {np.corrcoef(lp, w)[0, 1]:.2f}")
```

```text
confidence < -0.3: 확인 질문 13/30회 | 그중 실제 오류(WER>0) 12 | 통과했는데 오류  5 (WER>20 %: 4)
confidence < -0.5: 확인 질문 10/30회 | 그중 실제 오류(WER>0) 10 | 통과했는데 오류  7 (WER>20 %: 6)
confidence < -0.7: 확인 질문  7/30회 | 그중 실제 오류(WER>0)  7 | 통과했는데 오류 10 (WER>20 %: 9)
상관계수(log-prob, WER) = -0.88
```

출력에서 볼 것:

- 평균 log-prob와 WER의 상관이 −0.88로 강하다. 확신이 낮을수록 틀린다.
- 문턱 −0.3이면 30번 중 13번 되묻고, 그중 12번은 실제로 틀린 전사였다(정밀도 92 %). 하지만 틀린 전사 17개 중 5개는 그냥 통과한다 — "under a sailboat"처럼 **자신 있게 틀리는** 경우다. 문턱을 −0.7로 내리면 되묻기는 7번으로 줄지만 10개가 통과한다.
- 되묻기는 공짜가 아니다. 한 번 되물을 때마다 한 턴(수 초)이 더 든다. 문턱은 "틀린 행동의 비용 × 확률"과 "되묻기의 귀찮음"을 저울질해서 정한다 — 2.5절 hangover 저울질과 같은 구조다. 그리고 이 숫자는 30개짜리 작은 표본이다. 제품에서는 수천 개 발화로 다시 정한다.

### 8.3 endpointing 품질 — 너무 성급함 vs 너무 느림

| 증상 | 원인 | 사용자 체감 | 대책 |
|---|---|---|---|
| 말 중간에 대답을 시작함 | hangover가 짧음, 잡음 속 약한 자음, filler 뒤 쉼 (2절 q1, q6) | "아직 안 끝났어!" — 가장 짜증 나는 실패 | 조건부 hangover, partial 텍스트로 "문장 미완성" 판단, 투기적 실행 후 버리기 |
| 대답이 늘 한 박자 늦음 | hangover가 김 | "느린 기기" | 학습된 endpointer, earcon으로 반응 먼저 |
| 잡음 속에서 끝을 못 냄 | noise floor 추정 실패, babble 잡음 | 기기가 계속 듣고 있음, 최대 녹음 시간에서 강제 종료 | 최대 발화 길이 timeout, ML VAD, 빔포밍 |
| 끝 음절이 잘린 전사 | endpoint 전 약한 소리가 이미 "침묵" | "minutes"가 "minute"로 | ASR 입력에 trailing padding 수백 ms 추가 |

### 8.4 filler 단어와 말 고치기

사람은 "Um, what time… no, wait, when does the pharmacy close?"처럼 말한다. 여기에는 세 문제가 있다.

- **endpointing**: "Um," 뒤의 긴 쉼이 endpoint를 유발한다(2.7절 q6).
- **ASR**: Whisper는 학습 데이터(자막)가 filler를 지운 경우가 많아서 "um"을 들쭉날쭉하게 출력한다. 이번 q6 전사는 clean에서 `um. what time does the pharmacy close today?`로 filler를 남겼지만, 10 dB와 5 dB에서는 `What time does the pharmacy close today?`로 지웠다(정규화기가 "um"을 지우므로 WER은 셋 다 0). 의도를 보기에는 지우는 편이 낫지만, "지금 망설이는 중"이라는 endpointing 단서는 잃는다.
- **LLM**: 자기 수정("no, wait")은 앞부분을 무효화한다. 큰 LLM은 잘 처리하지만 작은 모델은 앞의 요청을 실행할 수 있다. ASR 출력을 LLM에 주기 전에 간단한 disfluency 제거(규칙 또는 작은 모델)를 두기도 한다.

---

## 9. 웨어러블에서는 무엇이 바뀌나

### 9.1 단계별 배치

이 Mac에서는 모든 단계가 한 칩의 CPU/GPU에서 돌았다. 웨어러블에서는 단계마다 **다른 엔진**에 놓인다(E8, I3). 아래는 예를 들어 Hark 같은 기기라면 그럴듯한 배치다 — **추정**이며 실제 제품 사양이 아니다.

```svg
<svg viewBox="0 0 680 345" xmlns="http://www.w3.org/2000/svg"><rect x="10.0" y="20.0" width="660.0" height="96.0" fill="#3f9a6b" fill-opacity="0.08" stroke="currentColor" stroke-width="0.5"/><text x="662.0" y="110.0" font-size="12" text-anchor="end">always-on 섬 (MCU + 오디오 DSP)</text><rect x="10.0" y="130.0" width="660.0" height="96.0" fill="#4a7bd0" fill-opacity="0.08" stroke="currentColor" stroke-width="0.5"/><text x="662.0" y="220.0" font-size="12" text-anchor="end">application SoC (CPU + NPU)</text><rect x="10.0" y="240.0" width="660.0" height="96.0" fill="#e08a3c" fill-opacity="0.08" stroke="currentColor" stroke-width="0.5"/><text x="662.0" y="330.0" font-size="12" text-anchor="end">기기 밖 (폰 또는 클라우드)</text><rect x="30" y="34" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="80.0" y="53.0" font-size="12" text-anchor="middle">mic PDM</text><text x="80.0" y="70.0" font-size="11" text-anchor="middle">+ 데시메이션</text><rect x="150" y="34" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="200.0" y="53.0" font-size="12" text-anchor="middle">AEC · NS</text><text x="200.0" y="70.0" font-size="11" text-anchor="middle">(DSP)</text><rect x="270" y="34" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="320.0" y="53.0" font-size="12" text-anchor="middle">VAD + KWS</text><text x="320.0" y="70.0" font-size="11" text-anchor="middle">수십 KB 모델</text><rect x="390" y="34" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="440.0" y="53.0" font-size="12" text-anchor="middle">endpointer</text><text x="440.0" y="70.0" font-size="11" text-anchor="middle">+ pre-roll 링버퍼</text><rect x="150" y="154" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="200.0" y="173.0" font-size="12" text-anchor="middle">streaming ASR</text><text x="200.0" y="190.0" font-size="11" text-anchor="middle">(RNN-T/CTC급)</text><rect x="290" y="154" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="340.0" y="173.0" font-size="12" text-anchor="middle">SLM 0.5–1B</text><text x="340.0" y="190.0" font-size="11" text-anchor="middle">int4, NPU</text><rect x="430" y="154" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="480.0" y="173.0" font-size="12" text-anchor="middle">소형 TTS</text><text x="480.0" y="190.0" font-size="11" text-anchor="middle">(신경망)</text><rect x="560" y="154" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="610.0" y="173.0" font-size="12" text-anchor="middle">speaker</text><text x="610.0" y="190.0" font-size="11" text-anchor="middle">amp</text><rect x="290" y="264" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="340.0" y="283.0" font-size="12" text-anchor="middle">큰 LLM</text><text x="340.0" y="300.0" font-size="11" text-anchor="middle">(cloud)</text><rect x="430" y="264" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="480.0" y="283.0" font-size="12" text-anchor="middle">고품질 TTS</text><text x="480.0" y="300.0" font-size="11" text-anchor="middle">(cloud)</text><rect x="150" y="264" width="100" height="44" rx="6" fill="none" stroke="currentColor"/><text x="200.0" y="283.0" font-size="12" text-anchor="middle">큰 ASR</text><text x="200.0" y="300.0" font-size="11" text-anchor="middle">(Whisper급)</text><line x1="130.0" y1="56.0" x2="144.0" y2="56.0" stroke="currentColor" stroke-width="1.2"/><polygon points="150.0,56.0 144.0,52.5 144.0,59.5" fill="currentColor"/><line x1="250.0" y1="56.0" x2="264.0" y2="56.0" stroke="currentColor" stroke-width="1.2"/><polygon points="270.0,56.0 264.0,52.5 264.0,59.5" fill="currentColor"/><line x1="370.0" y1="56.0" x2="384.0" y2="56.0" stroke="currentColor" stroke-width="1.2"/><polygon points="390.0,56.0 384.0,52.5 384.0,59.5" fill="currentColor"/><line x1="440.0" y1="78.0" x2="215.7" y2="152.1" stroke="currentColor" stroke-width="1.2"/><polygon points="210.0,154.0 216.8,155.4 214.6,148.8" fill="currentColor"/><line x1="250.0" y1="176.0" x2="284.0" y2="176.0" stroke="currentColor" stroke-width="1.2"/><polygon points="290.0,176.0 284.0,172.5 284.0,179.5" fill="currentColor"/><line x1="390.0" y1="176.0" x2="424.0" y2="176.0" stroke="currentColor" stroke-width="1.2"/><polygon points="430.0,176.0 424.0,172.5 424.0,179.5" fill="currentColor"/><line x1="530.0" y1="176.0" x2="554.0" y2="176.0" stroke="currentColor" stroke-width="1.2"/><polygon points="560.0,176.0 554.0,172.5 554.0,179.5" fill="currentColor"/><line x1="340.0" y1="198.0" x2="340.0" y2="258.0" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3"/><polygon points="340.0,264.0 343.5,258.0 336.5,258.0" fill="currentColor"/><line x1="200.0" y1="198.0" x2="200.0" y2="258.0" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3"/><polygon points="200.0,264.0 203.5,258.0 196.5,258.0" fill="currentColor"/><line x1="480.0" y1="264.0" x2="480.0" y2="204.0" stroke="currentColor" stroke-width="1.2" stroke-dasharray="4 3"/><polygon points="480.0,198.0 476.5,204.0 483.5,204.0" fill="currentColor"/><text x="500.0" y="92.0" font-size="11" text-anchor="start">wake 시 SoC 깨움 (IRQ)</text><text x="348.0" y="234.0" font-size="11" text-anchor="start">L3 라우팅</text></svg>
```

그림 10 — 웨어러블 음성 파이프라인의 배치(가정). 위 띠는 항상 켜진 저전력 섬, 가운데는 필요할 때만 깨어나는 application SoC, 아래는 기기 밖. 점선은 L3 라우팅으로 기기 밖으로 보내는 경로다.

| 단계 | 이 Mac (이 노트) | 웨어러블 후보 (가정) | 바뀌는 것 | 관련 노트 |
|---|---|---|---|---|
| 마이크 → PCM | `say` 파일 | PDM 마이크 → DSP 데시메이션 | 실제 잡음·바람·몸 소리, 마이크 여러 개 | G4, G5 |
| AEC · 잡음 억제 | 없음 | 오디오 DSP (HiFi급) | barge-in이 가능해짐 (7.3) | G4 |
| VAD · KWS | numpy 에너지 VAD | MCU/DSP, 수십 KB 모델, always-on | 수 mW 이하 전력 예산, 고정소수점 (2.7절 C) | B5, F2, J3 |
| endpointing | 고정 hangover 600 ms | DSP의 hangover + SoC의 학습된 end-of-query 모델 | partial 텍스트를 쓰는 조건부 endpointing | 2.6 |
| ASR | Whisper-tiny, endpoint 뒤 일괄 | SoC NPU의 streaming 모델 (RNN-T/CTC + Conformer류 encoder급, 수십 M 파라미터 int8 — 추정) | final 지연이 마지막 chunk만큼으로 짧아짐, partial 생김 | B5 4.4, F4 |
| LLM | 0.5B Q4, M2 Metal, TPOT 약 8 ms | SoC NPU의 0.5–1B int4 (TPOT 수십 ms) 또는 클라우드 | streaming 이득 커짐 (6.5), KV·메모리 압박 | L1, L2, L3, D5 |
| TTS | `say` / AVSpeechSynthesizer (대역) | SoC NPU의 소형 신경망 TTS 또는 클라우드 TTS (추정) | 엔진 상주 vs 메모리, 음질 vs 크기 | B5 7.1 |
| 재생 | 실제 재생 안 함 | I2S → 앰프 → 스피커, DMA 링버퍼 | 출력 버퍼 깊이 = barge-in 반응과 underrun 사이 trade-off | J1, D6 5.2 |

몇 가지는 확실하지 않으므로 표시해 둔다. 웨어러블 SoC NPU에서 streaming ASR과 SLM, TTS를 **동시에** 상주시킬 메모리가 있는지는 기기마다 다르다(I1 4.2절의 DRAM 예산). 메모리가 부족하면 단계마다 모델을 교체 로드해야 하고, 그 로드 시간이 5.3절의 cold start 문제로 돌아온다.

### 9.2 턴당 에너지 — 가정과 함께 (예제 17)

무엇을 확인하는 코드인가: 음성 한 턴(사용자 3 s, 응답 재생 6 s)의 에너지를 단계별 "전력 × 시간"으로 더한다. **모든 전력값은 가정**이다(I1의 사례 기기와 D7의 자릿수 감각을 따름). 실측 전에는 "어느 항이 지배하나"를 보는 용도다.

```python
# 예제 17: 음성 한 턴의 에너지 (모든 전력값은 가정 — 실측 전에는 자릿수 감각용). 배터리 1155 mWh (I1 사례)
turn_s, answer_s = 3.0, 6.0                   # 사용자 발화 3 s, 응답 재생 6 s
ON_DEVICE = [  # (항목, 전력 mW, 시간 s)
    ("mic + codec + VAD (DSP)",        3, turn_s + 0.6 + answer_s),  # 말·endpoint·재생 내내 깨어 있음
    ("ASR streaming (NPU)",          150, turn_s * 0.4),             # 말하는 동안 40 % duty
    ("SoC 깨어 있음 (CPU·DRAM 기본)", 250, 0.6 + 0.3 + 23 * 0.05 + 0.3),
    ("LLM 0.5B int4 (NPU + DRAM)",   900, 0.3 + 23 * 0.05),          # TTFT 0.3 s + 23 token × 50 ms
    ("TTS (NPU)",                    300, 0.4),
    ("speaker amp 재생",               60, answer_s)]
CLOUD = [r for r in ON_DEVICE if not r[0].startswith(("LLM", "TTS"))] + [
    ("radio Wi-Fi 송수신 (TX/RX 활성)", 350, 1.5),                    # 요청 + 응답 오디오 스트림
    ("radio 꼬리 (tail, 대기 상태)",     40, 4.0)]
for name, rows in (("on-device", ON_DEVICE), ("cloud", CLOUD)):
    mj = {n: p * t for n, p, t in rows}; tot = sum(mj.values())
    top = max(mj, key=mj.get)
    print(f"{name:9s}: 턴당 {tot / 1000:5.2f} J  (최대 항목: {top} {mj[top] / tot:4.0%})"
          f"  → 하루 50턴 {50 * tot / 3.6e3:6.1f} mWh = 배터리 {50 * tot / 3.6e3 / 1155:5.1%}")
```

```text
on-device: 턴당  2.58 J  (최대 항목: LLM 0.5B int4 (NPU + DRAM)  51%)  → 하루 50턴   35.9 mWh = 배터리  3.1%
cloud    : 턴당  1.84 J  (최대 항목: SoC 깨어 있음 (CPU·DRAM 기본)  32%)  → 하루 50턴   25.6 mWh = 배터리  2.2%
```

손으로 확인 (on-device의 큰 항 두 개):

```
LLM:        900 mW × (0.3 + 23 × 0.05) s = 900 × 1.45 = 1305 mJ
SoC 기본:   250 mW × (0.6 + 0.3 + 1.15 + 0.3) s = 250 × 2.35 = 587.5 mJ
speaker:     60 mW × 6 s = 360 mJ
합 ≈ 2.58 J → 50턴 × 2.58 J = 129 J = 129 / 3.6 mWh ≈ 35.9 mWh → 1155 mWh의 3.1 %
```

말로 하면: 이 가정에서는 **LLM decode가 턴 에너지의 절반**이다. token 수(23)와 TPOT(50 ms)를 곱한 시간 동안 NPU와 DRAM이 최대로 돈다. 그래서 "답을 짧게"는 지연(6.5)과 에너지 둘 다에 듣는다. 클라우드는 이 가정에서 턴당 1.84 J로 더 싸다 — 하지만 radio 전력과 tail 시간은 연결 상태에 따라 몇 배로 변하고(D7 6.3), 연결이 없으면 아예 불가능하다. 하루 50턴에 3 % 안팎이라는 자릿수는 "음성 턴 자체는 배터리의 주범이 아니고, always-on 경로(VAD·KWS·false wake, B5 8절)와 SoC 깨어 있는 시간이 더 위험하다"는 I1의 결론과 맞는다.

---

## 10. 임베디드 관점에서 다시 보기

### 10.1 파이프라인은 이벤트 구동 상태기계다

이 노트의 Python 코드는 스레드와 큐로 만들었지만, 기기에서는 같은 구조가 **ISR · 메일박스 · 태스크**로 바뀐다(J1, J2).

```
[DSP, 20 ms frame ISR]  VAD/endpointer 상태기계 (2.7절 C)
        │ EV_SPEECH_START  → SoC wake (pre-roll 링버퍼에서 앞 300–500 ms 함께 전달, I3 8.2)
        │ EV_ENDPOINT      → ASR final 요청
        ▼
[SoC, ASR task]  chunk마다 encoder → partial / final
        │ EV_ASR_FINAL(text, confidence)
        ▼
[SoC, dialog task]  confidence·슬롯 검사 (8.1–8.2) → LLM 시작 (on-device 또는 cloud, L3)
        │ token 콜백 → phrase chunker → EV_PHRASE(text)
        ▼
[SoC, TTS task]  구절 합성 → PCM을 출력 링버퍼에 → I2S DMA
        │
[언제든]  EV_BARGE_IN → LLM cancel, TTS 큐 flush, 출력 버퍼 fade-out, context에 "들린 부분까지"만 기록
```

펌웨어 엔지니어에게 익숙한 원칙이 그대로 적용된다.

- **모든 단계에 취소 경로**: 각 태스크는 "현재 턴 ID"를 들고 있고, barge-in이 오면 턴 ID를 올려서 옛 턴의 결과(늦게 도착한 token, 합성된 PCM)를 버린다. 오래된 DMA 완료를 무시하는 generation counter와 같은 기법이다.
- **pre-roll 링버퍼**: VAD가 말 시작을 감지하는 데 60 ms 이상 걸리고, SoC가 깨어나는 데 수십–수백 ms가 걸린다. 그동안의 오디오를 DSP 쪽 링버퍼에 보관했다가 넘겨야 첫 음절이 잘리지 않는다(I3 8.2).
- **출력 링버퍼 깊이**: 깊으면 underrun(재생 끊김)에 강하지만 barge-in 반응이 느리다. 얕으면 반대다. 7.4절의 40 ms는 이 깊이다.
- **타임스탬프 하나의 시계로**: DSP와 SoC가 다른 클럭을 쓰면 단계 지연을 뺄셈할 수 없다. 공통 타임베이스(G7)로 사건 시각을 찍는다.

### 10.2 필드에서 TTFA 재기 — 턴 trace (예제 18)

무엇을 확인하는 코드인가: 기기 펌웨어에서 턴마다 사건 6개의 시각을 24바이트 구조체에 기록하고, 턴이 끝나면 단계별 지연을 계산해 TTFA 히스토그램 bucket에 넣는다. 원시 trace 대신 히스토그램만 텔레메트리로 올리면 대역폭과 프라이버시 부담이 작다(H1, D6 4.3). 입력 숫자는 6절의 두 측정(load 13.4와 28.5의 streaming 중앙값)을 절대 시각으로 바꾼 것이다.

```c
/* turntrace.c — 기기에서 턴마다 사건 시각을 기록하고 단계별 지연과 TTFA 히스토그램을 만든다 */
#include <stdint.h>
#include <stdio.h>
enum { EV_SPEECH_END, EV_ENDPOINT, EV_ASR_FINAL, EV_LLM_TTFT, EV_PHRASE, EV_AUDIO_OUT, EV_N };
static const char *name[EV_N] = { "speech_end", "endpoint", "asr_final", "llm_ttft", "phrase", "audio_out" };
typedef struct { uint32_t t_ms[EV_N]; } turn_trace_t;           /* 턴 하나 = 24 바이트 */
static uint16_t ttfa_hist[8];                                   /* 250 ms 폭 bucket, 마지막은 ≥1750 */

static void trace_close(const turn_trace_t *tr) {
    for (int e = 1; e < EV_N; e++)
        printf("  %-10s +%4u ms\n", name[e], (unsigned)(tr->t_ms[e] - tr->t_ms[e - 1]));
    uint32_t ttfa = tr->t_ms[EV_AUDIO_OUT] - tr->t_ms[EV_SPEECH_END];
    unsigned b = ttfa / 250; ttfa_hist[b > 7 ? 7 : b]++;
    printf("  TTFA %u ms → bucket %u\n", (unsigned)ttfa, b > 7 ? 7 : b);
}
int main(void) {
    /* 예제 11 streaming 중앙값을 절대 시각으로 (speech_end = 10000 ms 가정) */
    turn_trace_t a = { { 10000, 10608, 10815, 10864, 10964, 11015 } };
    /* load 28 실행의 streaming 중앙값 */
    turn_trace_t b = { { 20000, 20608, 20960, 21054, 21313, 21338 } };
    trace_close(&a); trace_close(&b);
    printf("hist:"); for (int i = 0; i < 8; i++) printf(" %u", ttfa_hist[i]); printf("\n");
    return 0;
}
```

(`cc -std=c11 -Wall -Wextra -O2`로 경고 0개, 실행 결과:)

```text
  endpoint   + 608 ms
  asr_final  + 207 ms
  llm_ttft   +  49 ms
  phrase     + 100 ms
  audio_out  +  51 ms
  TTFA 1015 ms → bucket 4
  endpoint   + 608 ms
  asr_final  + 352 ms
  llm_ttft   +  94 ms
  phrase     + 259 ms
  audio_out  +  25 ms
  TTFA 1338 ms → bucket 5
hist: 0 0 0 0 1 1 0 0
```

출력에서 볼 것: 부하가 걸린 턴(b)에서 endpoint는 그대로 608이고 ASR(207 → 352)과 첫 구절(100 → 259)이 늘었다. 필드 텔레메트리로 이런 **단계별 분포**를 받아야 "TTFA p90이 나빠졌다"가 "ASR이 느려졌다"인지 "endpointing이 바뀌었다"인지 구분된다. 주의: "말 끝(speech_end)"은 기기가 실시간으로 알 수 없다(그걸 알면 endpointing이 필요 없다!). 기기에서는 VAD가 **마지막으로 말소리라고 판단한 frame**의 시각을 나중에 채워 넣는다. 이 근사 때문에 필드 TTFA는 실험실의 정답 기반 TTFA와 수십 ms 다를 수 있다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 모델 지연만 재고 "TTFA 300 ms"라고 보고 | 사용자는 1초 넘게 느낌 | endpoint 대기(알고리즘 지연)를 빠뜨림 | 말 끝부터 첫 오디오까지 end-to-end로 잰다 (6절: endpoint가 60 %) |
| hangover를 clean 녹음으로 튜닝 | 필드에서 말 중간에 끊김 | 잡음이 약한 자음을 묻어 쉼이 길어짐 (2.3) | 잡음 조건별로 false cut-off를 같이 센다 |
| TTS·LLM 엔진을 턴마다 초기화 | 첫 오디오가 1초 이상 늦음 | cold start (5.2: `say` 1.1 s, 엔진 914 ms) | 상주시키거나 wake 시 pre-warm |
| 답 전체를 받은 뒤 TTS 시작 | 느린 엔진에서 TTFA 수백 ms 손해 | store-and-forward | 첫 구절 chunking (4.2), 6.5의 (N − K) × TPOT 이득 |
| chunker가 문장 끝만 봄 | 긴 문장에서 streaming 효과 없음 | 모델이 쉼표 없는 긴 문장 생성 | 단어 수 안전장치, 짧은 첫 문장 유도 |
| barge-in 때 연결만 끊고 큐는 그대로 | "그만"했는데 몇 초 더 말함 | TTS 큐·출력 버퍼에 남은 구절 | 턴 ID로 큐 flush, 출력 fade-out (7.2, 10.1) |
| AEC 없이 barge-in 켬 | 기기가 제 목소리에 말을 끊음 | 에코가 VAD 문턱을 넘음 (7.3: −30 dB에서도 오검출) | AEC + 잔류 에코 억제, 재생 참조 gate, KWS로만 barge-in |
| WER 비교에 정규화 규칙이 다름 | 같은 모델인데 WER이 5–10 %p 차이 | "ten" vs "10", 구두점, filler | 같은 normalizer로 정답·결과 둘 다 정규화 (3.2) |
| 작은 LLM에게 확인 질문을 맡김 | "410 minutes" 타이머가 그대로 설정됨 | 0.5B는 지시를 잘 안 따름 (8.1) | ASR confidence·슬롯 범위 검사를 규칙으로 앞에 둔다 |
| p50만 보고 예산 통과 판정 | 사용자 불만은 계속 | 부하·thermal에서 계산 단계가 2배 (6.4) | p90/p99, 부하 조건에서 검증 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Walk me through the latency of a voice assistant turn and how you'd cut it."

**A.** 말 끝을 0으로 놓고 endpoint 선언, ASR final, LLM 첫 token, 첫 구절, 첫 오디오까지 사건 다섯 개로 쪼갠다. 직접 Mac에서 VAD → Whisper-tiny → 0.5B LLM → TTS를 연결해 쟀더니 TTFA 중앙값 1015 ms 중 608 ms가 endpoint 대기였다. 그래서 순서는 (1) TTS·LLM 엔진 상주(cold 2.7 s → 1.15 s), (2) 첫 구절부터 TTS를 겹치는 streaming, (3) 그다음은 계산이 아니라 endpointing 알고리즘 — 조건부 hangover, 학습된 end-of-query 모델, earcon으로 체감 가리기다.

> I'd decompose the turn into events measured from the end of the user's speech: endpoint, ASR final, first LLM token, first TTS-ready phrase, and first audio out. When I wired up an energy VAD, Whisper-tiny, a 0.5B model on llama.cpp and a TTS engine on my laptop, the median time-to-first-audio was about one second, and 60 percent of it was the endpointing timeout, not compute. So I'd first keep the TTS and LLM engines resident, then stream the first phrase into TTS while the LLM keeps decoding, and then attack endpointing itself with a conditional or learned end-of-query detector, plus an earcon to acknowledge the user immediately.

**Q.** "How do you choose the endpointing timeout?"

**A.** 두 비용의 저울질이다. 측정해 보니 hangover 300 ms면 지연은 300 ms지만 잡음·filler 때문에 40 %를 말 중간에 잘랐고, 1000 ms면 2.5 %로 줄었다. 잘리면 사용자가 다시 말해야 하므로 수 초가 든다. 그래서 잡음 조건별 false cut-off 곡선을 그리고, 기대 비용과 체감 지연을 같이 보고 고른다. 한 값으로 못 맞추면 partial 텍스트를 보고 바꾸는 조건부 timeout으로 간다.

> It's a trade-off between latency and false cut-offs, and I'd measure both on noisy, disfluent speech rather than clean recordings. In my test set a 300 ms timeout cut users off 40 percent of the time, mostly at hesitation pauses that noise had made longer, while one second cut off 2.5 percent. Since a cut-off costs a full repeat, I pick from that curve by expected cost, and if no single value works I make the timeout conditional on the partial transcript, short after a complete sentence and long after words like "and" or "um".

**Q.** "Why is Whisper awkward for streaming?"

**A.** 입력이 항상 30 s 창(3000 mel frame)이라 1.85 s 말에도 encoder가 30 s를 계산했고(94 % padding), encoder가 전체를 보고 나서 decoder가 token을 하나씩 내므로 partial이 없다. 중간 결과를 보려면 지금까지의 오디오로 처음부터 다시 돌려야 해서 계산이 반복된다. 잡음에서 그럴듯한 문장을 지어내는 경향도 실측했다. 웨어러블에는 chunk 단위로 돌고 partial을 내는 RNN-T/CTC 계열이 맞다.

> Whisper always processes a fixed 30-second window, so a two-second command pays for 30 seconds of encoder compute, and the decoder only starts after the whole utterance is encoded. There are no native partial results; to get them you re-run on a growing buffer, which multiplies compute. In my noisy tests it also hallucinated fluent but wrong sentences. For an always-listening wearable I'd prefer a chunked streaming model, RNN-T or CTC with a Conformer-style encoder, which emits partials and only has to process the last chunk at end of speech.

**Q.** "How does barge-in work?"

**A.** 재생 중 VAD가 사용자 목소리를 감지하면 이벤트 하나로 세 가지를 병렬로 한다: 출력 버퍼 fade-out, TTS 큐 flush, LLM 생성 취소. llama-server로 재 보니 stream을 끊으면 새 질문 첫 token까지 48 ms였고, 안 끊으면 옛 답이 끝날 때까지 924 ms를 기다렸다. 핵심 난제는 에코다. 시뮬레이션에서 잔류 에코가 사용자 목소리보다 30 dB 작아도 단순 VAD가 기기 자신의 목소리에 반응했다. 그래서 AEC가 전제이고, 부족하면 재생 참조 gate나 KWS 기반 barge-in을 쓴다.

> When the VAD detects user speech during playback, one event fans out to three actions in parallel: fade out the output buffer, flush queued TTS phrases, and cancel LLM generation, then record only what the user actually heard into the context. Measured against llama-server, cancelling the stream let a new question get its first token in 48 ms, versus 924 ms when the old answer ran to completion. The hard part is echo: in my simulation a plain VAD still triggered on the device's own voice with residual echo 30 dB below the user, so you need AEC with enough suppression, or a playback-referenced gate or keyword-based barge-in as a fallback.

**Q.** "Where would each stage run on our wearable?"

**A.** (가정임을 밝히고) VAD·KWS·endpointer와 AEC는 always-on DSP/MCU, 말 시작이 확인되면 SoC를 깨워 pre-roll과 함께 NPU의 streaming ASR로 넘긴다. 짧고 흔한 요청은 SoC NPU의 0.5–1B int4 SLM, 어렵거나 연결이 좋으면 클라우드(L3). TTS는 소형 on-device 모델을 상주시키거나 클라우드. 측정상 TPOT 50 ms급 기기에서는 streaming 이득이 550 ms로 커지고, 턴 에너지의 절반이 LLM decode라는 가정 계산이 나왔다.

> I'd keep VAD, keyword spotting, the endpointer and echo cancellation on the always-on DSP or MCU, and only wake the application SoC when speech is confirmed, handing over a pre-roll buffer. Streaming ASR and a small int4 language model would run on the SoC's NPU for short, common requests, with harder ones routed to the cloud when connectivity is good. TTS would be a small resident on-device model with a cloud option. At wearable decode speeds, streaming the first phrase is worth about half a second, and LLM decode is roughly half the energy of a turn, so short answers help both latency and battery.

**Q.** "An ASR error reached the LLM and the device did the wrong thing. How do you prevent that?"

**A.** 실제로 잡음 전사 "Set a timer 410 minutes"를 0.5B에 넣었더니 그대로 410분 타이머를 받아들였고, "이상하면 되물어라"는 지시도 안 따랐다. 그래서 LLM 앞에 규칙 층을 둔다: Whisper 평균 log-prob가 낮으면 되묻기(내 측정에서 WER과 상관 −0.88), 숫자 슬롯이 범위를 벗어나면 확인, 되돌릴 수 없는 행동은 항상 확인. 다만 자신 있게 틀리는 경우가 남으므로 intent 정확도와 슬롯 오류율을 따로 모니터링한다.

> I fed a real noisy transcript, "set a timer 410 minutes", to a 0.5B model and it happily accepted it, even when told to ask for confirmation, so I wouldn't rely on a small LLM as the safety net. Instead I'd put a rule layer around it: ASR confidence from the average token log-probability, which correlated at minus 0.88 with WER in my test, triggers a clarifying question; numeric slots outside plausible ranges get confirmed; and irreversible actions always get confirmed. Confident errors still slip through, so I'd also track intent accuracy and slot error rate in the field.

---

## 13. 직접 해보기

1. (손계산) hangover 800 ms, 마지막 말소리 frame이 3.46–3.48 s일 때 endpoint는 몇 초에 선언되나? 정답 말 끝이 3.475 s라면 endpoint 지연은?
   정답: 침묵 frame 40개(800 ms)가 3.48 s부터 쌓여 4.28 s에 선언. 지연 = 4.28 − 3.475 = 805 ms.
2. (손계산) 정답 "turn off the lights in the living room please"(N = 9)와 결과 "you know the light in the living room"의 WER을 정렬을 써서 구하라.
   정답: turn→you(S), off→know(S), lights→light(S), please 삭제(D) — 4/9 = 44.4 %. 예제 5의 q8_snr5 "오류 4"와 같다.
3. (손계산) N = 40 token 답, 첫 구절 K = 8, TPOT 40 ms, 나머지 고정 지연 합이 900 ms다. 순차와 streaming의 TTFA, 그리고 첫 구절을 K = 4로 줄이면?
   정답: 순차 900 + 39 × 40 = 2460 ms, streaming 900 + 7 × 40 = 1180 ms, K = 4면 900 + 3 × 40 = 1020 ms.
4. (코드) 예제 3을 고쳐 조건별(clean, 20, 10, 5 dB)로 false cut-off를 따로 세라. hangover 600에서 어느 조건이 cut-off를 가장 많이 만드나?
   힌트: `conds` 루프 안에서 `cut`을 dict로 나눠 센다. 2.3절의 설명대로 잡음이 클수록 쉼이 길어지는지 확인하라.
5. (코드) 예제 14에서 onset frame 수를 3에서 5로 바꾸면 −30 dB 행의 단순 VAD 결과와 −10 dB 행의 gate 검출 지연이 어떻게 바뀌나? barge-in 반응 시간과 오검출 사이의 trade-off를 한 문장으로 써라.
   힌트: `if run >= 3`을 인자로 빼라. 연속 조건이 길수록 짧은 에코 버스트에는 강해지지만 정상 검출이 최소 100 ms로 늦어진다.
6. (설계) TPOT 50 ms 기기에서 TTFA 목표 1000 ms를 맞추려면 예제 12의 웨어러블 시나리오에서 무엇을 얼마나 줄여야 하나? 적어도 두 가지 조합을 제시하라.
   정답 예: endpoint 400 → 250, TTFT 300 → 100(prefix KV 캐시), K 12 → 4이면 250 + 100 + 100 + 3 × 50 + 120 + 40 = 760 ms. 또는 클라우드로 보내고 earcon으로 체감을 가린다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| TTFA | time to first audio | 사용자 말 끝 → 응답 첫 소리. 사용자가 느끼는 반응 시간 |
| endpointing | 발화 끝 판정 | VAD 위에 "침묵이 H ms면 끝" 같은 시간 규칙을 얹은 것 |
| hangover | 끝 판정 대기 시간 | 침묵이 이만큼 이어져야 말이 끝났다고 본다. 알고리즘 지연 |
| false cut-off | 성급한 끝 판정 | 사용자가 아직 말하는 중인데 끝을 선언함 |
| SNR | signal-to-noise ratio | 10·log10(신호 전력 / 잡음 전력), 말소리 구간에서 잰다 |
| WER | word error rate | (치환 + 삭제 + 삽입) / 정답 단어 수 |
| RTF | real-time factor | 처리 시간 / 오디오 길이. 작아도 지연이 작다는 보장은 없다 |
| TTFT | time to first token | LLM 요청 → 첫 token 도착 |
| TPOT | time per output token | token 하나당 decode 시간 |
| phrase chunking | 구절 자르기 | token stream을 TTS가 읽을 수 있는 단위로 자르는 규칙 |
| barge-in | 끼어들기 | 기기가 말하는 중 사용자가 말하면 즉시 멈추고 듣기 |
| AEC | acoustic echo cancellation | 스피커 소리가 마이크로 돌아온 성분을 reference로 추정해 빼기 |
| earcon | 짧은 알림음 | "들었다"를 즉시 알리는 소리. 체감 지연을 가린다 |
| pre-roll | 앞부분 보관 버퍼 | 감지 전의 오디오를 링버퍼에 두었다가 다음 단계에 넘김 |

---

## 15. 요약 & 체크리스트

음성 턴의 지연은 "말 끝 → endpoint → ASR final → TTFT → 첫 구절 → 첫 오디오"로 쪼개야 보인다. 이 Mac에서 `say`로 만든 질의 10개를 에너지 VAD(hangover 600 ms) → Whisper-tiny → Qwen2.5-0.5B Q4(llama-server streaming) → warm TTS로 돌린 결과 TTFA 중앙값은 1015 ms였고, 그중 608 ms(60 %)가 아무 계산 없이 침묵을 기다리는 endpoint였다. TTS를 매번 새로 띄우면 2748 ms, 엔진을 상주시키면 1151 ms, 첫 구절부터 TTS를 겹치면 1015 ms다. 느린 웨어러블 엔진(TPOT 50 ms 가정)에서는 streaming 이득이 550 ms로 커진다. hangover는 짧게 하면 말을 자르고(300 ms에서 40 %), Whisper-tiny는 30 s 창과 일괄 처리 때문에 streaming에 어색하며 잡음에서 WER이 13 % → 83 %로 무너진다. barge-in은 LLM stream을 끊으면 48 ms로 서버가 비지만, 에코가 사용자 목소리보다 −30 dB만 돼도 단순 VAD가 기기 자신의 목소리에 반응한다. ASR 오류("410 minutes")는 작은 LLM이 걸러 주지 않으므로 confidence·슬롯 검사를 규칙으로 둔다.

- [ ] 한 턴의 사건 6개와 TTFA를 정의하고, 각 구간이 알고리즘 지연인지 계산 지연인지 말할 수 있다
- [ ] hangover와 frame 크기로 endpoint 지연을 손으로 계산할 수 있다
- [ ] hangover vs false cut-off 곡선을 그리는 실험을 설계하고, 기대 비용으로 값을 고를 수 있다
- [ ] 단어 단위 edit distance로 WER을 손으로 계산하고, 정규화가 왜 필요한지 설명할 수 있다
- [ ] Whisper의 30 s 창 padding 비율과 encoder/decoder 시간 분해를 설명할 수 있다
- [ ] SSE token stream에서 TTFT, TPOT, 첫 구절 시각을 찍고 phrase chunker를 짤 수 있다
- [ ] 순차 vs streaming의 TTFA 차이를 (N − K) × TPOT으로 계산할 수 있다
- [ ] TTS cold start가 왜 TTFA를 지배할 수 있는지와 pre-warm 설계를 말할 수 있다
- [ ] barge-in의 세 동작(재생 중단, 큐 flush, 생성 취소)과 에코 문제, 지연 예산을 설명할 수 있다
- [ ] 웨어러블에서 각 단계가 어느 엔진에 놓일지와 턴당 에너지의 지배 항을 가정과 함께 말할 수 있다

## 참고 자료

- Radford et al., "Robust Speech Recognition via Large-Scale Weak Supervision" (Whisper, 2022) — [arXiv:2212.04356](https://arxiv.org/abs/2212.04356)
- Hugging Face Transformers 문서, Whisper 모델 페이지 — [huggingface.co/docs/transformers/model_doc/whisper](https://huggingface.co/docs/transformers/model_doc/whisper)
- Graves, "Sequence Transduction with Recurrent Neural Networks" (RNN-T, 2012) — [arXiv:1211.3711](https://arxiv.org/abs/1211.3711)
- Gulati et al., "Conformer: Convolution-augmented Transformer for Speech Recognition" (2020) — [arXiv:2005.08100](https://arxiv.org/abs/2005.08100)
- Shannon, Simko, Chang, Parada, "Improved End-of-Query Detection for Streaming Speech Recognition" (Interspeech 2017)
- Chang et al., "Joint Endpointing and Decoding with End-to-End Models" (ICASSP 2019)
- Macháček, Dabre, Bojar, "Turning Whisper into Real-Time Transcription System" (2023) — [arXiv:2307.14743](https://arxiv.org/abs/2307.14743)
- Stivers et al., "Universals and cultural variation in turn-taking in conversation" (PNAS, 2009)
- llama.cpp server 문서 — [github.com/ggml-org/llama.cpp/tree/master/tools/server](https://github.com/ggml-org/llama.cpp/tree/master/tools/server)
- Silero VAD — [github.com/snakers4/silero-vad](https://github.com/snakers4/silero-vad)
- Apple Developer 문서, AVSpeechSynthesizer — [developer.apple.com/documentation/avfaudio/avspeechsynthesizer](https://developer.apple.com/documentation/avfaudio/avspeechsynthesizer)
- 이 노트 시리즈: B5(음성 모델), G4(마이크·AEC), I1(예산), I3(cascade), D5·D6(LLM 지연, 실시간), F3(llama.cpp), L2(LLM 스택), L3(라우팅)
