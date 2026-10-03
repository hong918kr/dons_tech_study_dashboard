# L5. 온디바이스 메모리와 RAG — 개인 기억을 기기 안에 저장하고 찾아 쓰기

> **이 노트를 다 읽으면**: 어시스턴트의 "기억"을 단기(context window)·일화·의미 기억으로 나누고 RAG가 그것을 프롬프트에 끼워 넣는 흐름을 그릴 수 있다 · MiniLM 임베딩을 직접 만들고 float32/int8/binary로 양자화해서 바이트와 recall의 교환을 숫자로 말할 수 있다 · brute-force와 IVF 검색의 속도·recall을 측정하고 SQLite에 벡터와 메타데이터를 같이 넣어 필터 검색을 할 수 있다 · 0.5B LLM으로 RAG를 돌려 no-RAG/RAG/RAG+필터를 비교하고, 메모리 쓰기 경로(추출·중복 제거·점수·TTL)와 "제대로 지우기"를 설계할 수 있다
> **JD 연결**: Hark가 내세우는 "persistent memory" — 사용자를 기억하는 AI 기기. study_prep_list L5: embedding 모델, 소형 vector search, 개인 데이터 저장과 프라이버시. JD 우대 "lightweight LLM / hybrid edge-LLM"(L1–L3)과 붙어 있다.
> **Don 기준 난이도**: 캐시 계층·메모리 대역폭·SQLite 같은 임베디드 DB·NAND의 "지웠는데 데이터가 남는" 문제(stale page, garbage collection)는 이미 강하다 / 문장 임베딩, 벡터 검색 알고리즘(IVF, HNSW, PQ), 프롬프트에 기억을 넣는 RAG, 기억을 언제 쓰고 언제 잊을지 정하는 정책은 새로 배운다
> **선행 노트**: A1(내적·코사인 유사도·임베딩 §2), B4(attention·transformer), B5(화자 임베딩 §5), B8(소형 언어 모델), C1(양자화 이론), C3(LLM 양자화), D2(메모리 계산), D3(roofline), H6(프라이버시·보관·삭제), L2(온디바이스 LLM 스택 — context 관리)

> **이 노트의 데이터는 전부 지어낸 것이다.** "Robin Park"이라는 가상의 사용자가 남긴 메모 40개(치과 예약, 여동생 생일, 주차 위치 등)와 템플릿으로 만든 잡다한 메모 160개를 쓴다. 실제 사람·실제 일정과 관계없다. 전체 데이터 파일은 맨 끝 부록 A에 있다.

> **측정 환경**: Apple M2 MacBook (macOS), `.venv/bin/python` (Python 3.9, numpy 2.0.2, torch 2.8, transformers 4.57, scikit-learn 1.6.1, SQLite 3.51), 임베딩 모델 `sentence-transformers/all-MiniLM-L6-v2`(HF 캐시, `transformers`로 직접 로드), 생성 모델 `qwen2.5-0.5b-Q8_0.gguf`(Qwen2.5-0.5B-Instruct를 GGUF Q8_0으로 변환한 것)을 llama.cpp `llama-server`로 서빙. **다른 작업이 많이 돌던 공유 머신에서 잰 시간이라 절대값은 ±50% 흔들린다.** 시간은 "어느 쪽이 몇 배 빠른가"라는 비율로만 읽는다.

---

## 0. 큰 그림 — "persistent memory"가 기기 안에서 뜻하는 것

### 0.1 LLM은 기억이 없다

LLM은 함수다. 입력 토큰을 받아 다음 토큰을 낸다. 가중치는 학습이 끝난 뒤 고정되므로, "어제 Robin이 Lot C에 주차했다"는 사실은 가중치 어디에도 없다. 대화 중에 그 말을 들었다면 그것은 **context window**(LLM이 한 번에 볼 수 있는 토큰 창, L2 참고) 안에만 있고, 대화가 끝나 KV-cache가 비워지면 사라진다.

그래서 "나를 기억하는 기기"를 만들려면 LLM 바깥에 **기억 저장소**를 두고, 질문이 들어올 때마다 관련 기억을 찾아서 프롬프트에 끼워 넣어야 한다. 이것이 **RAG(retrieval-augmented generation, 검색 증강 생성)**다. 말 그대로 "검색해서 보태 준 다음 생성한다".

Don에게 익숙한 그림으로 바꾸면 이렇다.

| 펌웨어 세계 | 어시스턴트 세계 |
|---|---|
| CPU 레지스터 · L1 SRAM (빠르고 작다, 전원 끄면 사라짐) | context window · KV-cache (수천 토큰, 대화 끝나면 사라짐) |
| NAND flash (크고 느리고 영구) | 기억 저장소 (SQLite 파일 + 임베딩 벡터) |
| demand paging: 필요한 페이지만 DRAM으로 올림 | RAG: 질문과 관련된 기억 k개만 context로 올림 |
| page table / FTL 매핑 | 벡터 인덱스 (질문 → 어떤 기억이 관련 있나) |
| TRIM · garbage collection · secure erase | 기억 삭제 · TTL · 파생 데이터까지 지우기 |

이 비유는 꽤 정확하다. context window는 비싸고(토큰 하나마다 prefill 연산, L2·D5) 작다. 저장소는 싸고 크다. 사이를 잇는 "어느 페이지를 올릴까"가 **검색(retrieval)** 문제다.

### 0.2 이 노트에서 만드는 것

```
[질문 음성] → ASR(L4) → "When is my dentist appointment?"
                              │
                              ▼
            ① 질문 임베딩 (MiniLM, 384-d)         §2
                              │
            ② 저장소 검색: SQL 메타데이터 필터     §6
               + 벡터 유사도 top-k                 §3 §4 §5
                              │
            ③ 토큰 예산 안에서 기억 고르기         §8
                              │
            ④ 프롬프트 템플릿 → 0.5B LLM 생성      §7
                              │
                              ▼
            "Your dentist appointment is on Thursday, October 8 at 3:30 PM."

  (별도 경로) 대화 → 기억 추출 → 중복 확인 → 점수 → 저장 / TTL / 삭제   §9 §10
```

예를 들어 Hark 같은 웨어러블이라면 (추정) 기억 저장소는 기기 본체나 짝을 이룬 폰에 있고, 여기서 다루는 모든 계산이 그 안에서 끝날 수 있다. §11에서 숫자로 보겠지만 **기억 검색은 LLM 생성에 비하면 연산·에너지가 거의 0**이다. 어려운 부분은 연산이 아니라 **무엇을 저장하고, 무엇을 찾아오고, 어떻게 지우느냐**다.

---

## 1. 어시스턴트의 기억 — 단기 · 일화 · 의미, 그리고 RAG

### 1.1 세 종류의 기억

인지과학 용어를 빌려 오면 구분이 깔끔하다 (엄밀한 정의라기보다 설계용 분류다).

| 종류 | 무엇 | 예 (Robin) | 어디에 사는가 | 수명 |
|---|---|---|---|---|
| 단기 기억 (working memory) | 지금 대화의 내용 | "방금 말한 그 식당" | context window, KV-cache (L2) | 대화 한 번 |
| 일화 기억 (episodic) | 언제 무슨 일이 있었나 — 시간이 붙은 사건·대화 | "10/1 공항 Lot C row 14에 주차" | 저장소, `type=event/conversation` | 수 주~수 개월, TTL 대상 |
| 의미 기억 (semantic) | 시간과 무관한 사실·선호 | "비행기는 aisle석", "페니실린 알레르기" | 저장소, `type=fact/preference` | 갱신될 때까지 |

일화 기억이 쌓이면 그중 일부가 의미 기억으로 굳는다. "Robin은 세 번 연속 oat milk flat white를 시켰다" (일화 3개) → "Robin은 oat milk flat white를 좋아한다" (의미 1개). 이 **요약·승격(consolidation)**이 §9의 쓰기 경로에서 하는 일이다. 그리고 §10에서 보겠지만, 요약은 원본에서 **파생된 데이터**라서 원본을 지울 때 같이 지워야 한다.

### 1.2 RAG — 기억을 프롬프트에 끼워 넣기

```svg
<svg viewBox="0 0 660 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="l5a1" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="20" y="40" width="190" height="80" fill="#3f9a6b" opacity="0.15" stroke="#3f9a6b"/><text x="115" y="62" text-anchor="middle" font-size="13">일화 기억 (episodic)</text><text x="115" y="84" text-anchor="middle" font-size="12">[10-01] Lot C row 14 주차</text><text x="115" y="104" text-anchor="middle" font-size="12">[10-08] 치과 3:30 PM</text><rect x="20" y="170" width="190" height="80" fill="#4a7bd0" opacity="0.15" stroke="#4a7bd0"/>
<text x="115" y="192" text-anchor="middle" font-size="13">의미 기억 (semantic)</text><text x="115" y="214" text-anchor="middle" font-size="12">aisle석 선호</text><text x="115" y="234" text-anchor="middle" font-size="12">페니실린 알레르기</text><text x="115" y="28" text-anchor="middle" font-size="12">장기 기억 = 저장소 (flash)</text><rect x="260" y="80" width="180" height="140" fill="#e08a3c" opacity="0.12" stroke="#e08a3c"/><text x="350" y="100" text-anchor="middle" font-size="13">context window</text><text x="350" y="118" text-anchor="middle" font-size="12">(단기 기억, L2)</text>
<text x="350" y="146" text-anchor="middle" font-size="12">system prompt</text><text x="350" y="168" text-anchor="middle" font-size="12">검색된 기억 k개</text><text x="350" y="190" text-anchor="middle" font-size="12">현재 질문·대화</text><rect x="490" y="110" width="150" height="80" fill="#888" opacity="0.15" stroke="#888"/><text x="565" y="146" text-anchor="middle" font-size="13">소형 LLM</text><text x="565" y="166" text-anchor="middle" font-size="12">(0.5B, 가중치 고정)</text><line x1="210" y1="80" x2="258" y2="155" stroke="currentColor" marker-end="url(#l5a1)"/>
<line x1="210" y1="210" x2="258" y2="170" stroke="currentColor" marker-end="url(#l5a1)"/><text x="232" y="66" font-size="12">retrieve top-k</text><line x1="440" y1="150" x2="488" y2="150" stroke="currentColor" marker-end="url(#l5a1)"/><path d="M565,190 L565,282 L115,282 L115,253" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#l5a1)"/><text x="340" y="276" text-anchor="middle" font-size="12">write path: 대화에서 새 기억 추출 → 중복 확인 → 저장 (§9)</text>
</svg>
```

그림 1 — 기억의 세 층. 장기 기억(왼쪽, flash에 영구 저장)에서 질문과 관련된 것만 골라 context window(가운데, 단기 기억)에 올리고, LLM은 그 창만 본다. 점선은 반대 방향인 쓰기 경로다.

정의를 한 줄로: **RAG = 질문 q가 오면 저장소에서 관련 문서 k개를 검색해 프롬프트에 붙이고, LLM이 그것을 근거로 답하게 하는 것**. Lewis et al.(2020)이 이 이름을 붙였다. 원래는 Wikipedia 같은 큰 지식 베이스용이었지만, 온디바이스 개인 비서에서는 "지식 베이스 = 내 기억"이 된다.

왜 가중치에 기억을 넣지 않나(fine-tuning)? 이유가 셋이다.

1. 기억은 매일 바뀐다. 주차 위치를 기억시키려고 매번 학습할 수 없다.
2. 지우기가 불가능에 가깝다. 가중치에 녹아 든 사실을 "정확히 그것만" 빼는 방법(machine unlearning)은 아직 연구 단계다. 저장소의 행은 `DELETE` 한 줄이다.
3. 근거를 보여 줄 수 있다. RAG는 "이 메모를 보고 답했다"고 출처를 댈 수 있다.

### 1.3 Don 경험과 연결

SSD 펌웨어의 read path를 떠올리면 된다. 호스트가 LBA를 주면 FTL이 매핑 테이블을 보고 물리 페이지를 찾아 DRAM 버퍼로 올린다. RAG는 "LBA" 대신 **의미**로 주소를 찾는다. "치과 언제였지?"에는 정확히 일치하는 키가 없다. 그래서 질문과 기억을 같은 벡터 공간에 놓고 **가까운 것**을 찾는다. 다음 절이 그 벡터 공간 이야기다.

### 1.4 함정

- context window를 크게 잡으면 RAG가 필요 없다고 생각하기 쉽다. 기억 1만 개 × 30 토큰 = 30만 토큰이다. 0.5B 모델 context(32k)에도 안 들어가고, 들어간다 해도 prefill 비용이 토큰 수에 비례한다 (§8).
- "기억"을 원문 대화 전체로 저장하면 검색도 프라이버시도 나빠진다. 저장 단위는 **짧은 사실 한 문장**이 좋다 (§9).

---

## 2. 임베딩 — 문장을 방향으로 바꾸기

### 2.1 직관 (A1 복습)

A1 §2에서 본 것처럼 **임베딩(embedding)**은 문장을 고정 길이 벡터로 바꾼 것이고, 모델이 "의미가 비슷하면 방향이 비슷하도록" 학습되어 있다. B5 §5의 화자 임베딩(d-vector)과 똑같은 구조다. 화자 임베딩은 "같은 사람의 목소리"가 가깝도록, 문장 임베딩은 "같은 뜻의 문장"이 가깝도록 학습했을 뿐이다.

비교는 **코사인 유사도**로 한다.

```
cos(u, v) = (u · v) / (|u| |v|)          ∈ [−1, 1]
```

말로 하면: 길이는 무시하고 두 화살표가 이루는 각도만 본다. 벡터를 미리 길이 1로 정규화해 두면 분모가 1이 되어 **코사인 = 내적**이다. 그래서 검색은 "저장된 행렬 M(N×384)과 질문 벡터 q의 행렬-벡터 곱 한 번"이 된다. 펌웨어로 치면 N개 FIR 탭에 대한 MAC 루프다.

### 2.2 손계산

u = (3, 4, 0), v = (6, 8, 0), w = (0, 3, 4)라 하자.

```
|u| = √(9+16) = 5,   |v| = 10,   |w| = 5
cos(u, v) = (18 + 32 + 0) / (5 · 10) = 50/50 = 1.0      ← 같은 방향 (길이만 다름)
cos(u, w) = (0 + 12 + 0) / (5 · 5)  = 12/25 = 0.48
정규화하면  û = (0.6, 0.8, 0),  ŵ = (0, 0.6, 0.8),  û · ŵ = 0.48
|û − ŵ|² = 0.36 + 0.04 + 0.64 = 1.04 = 2 − 2·0.48
```

마지막 줄이 중요하다. 단위 벡터에서는 **L2 거리² = 2 − 2·cos**. 그러니 "코사인 최대"와 "L2 거리 최소"가 같은 순서를 준다. 벡터 DB 문서에서 metric을 L2로 하든 inner product로 하든, 정규화해 두면 결과가 같다.

아래 코드는 이 손계산을 확인한다.

```python
import numpy as np
u = np.array([3.0, 4.0, 0.0])      # |u| = 5
v = np.array([6.0, 8.0, 0.0])      # same direction, twice as long
w = np.array([0.0, 3.0, 4.0])
cos = lambda a, b: a @ b / (np.linalg.norm(a) * np.linalg.norm(b))
print("cos(u,v) =", cos(u, v), " cos(u,w) =", cos(u, w))
un, wn = u / np.linalg.norm(u), w / np.linalg.norm(w)
print("normalized dot =", un @ wn, " L2 dist^2 =", np.sum((un - wn) ** 2), " 2-2cos =", 2 - 2 * cos(u, w))
```

```text
cos(u,v) = 1.0  cos(u,w) = 0.48
normalized dot = 0.48  L2 dist^2 = 1.04  2-2cos = 1.04
```

출력에서 볼 것: v는 u의 두 배 길이지만 코사인 1.0이다. L2² = 2 − 2cos가 숫자로 맞는다.

### 2.3 문장 임베딩 모델 — all-MiniLM-L6-v2

`all-MiniLM-L6-v2`는 sentence-transformers 프로젝트가 공개한 작은 문장 임베딩 모델이다. 구조는 B4에서 본 transformer encoder 6층, hidden 384. 문장을 토큰으로 자르고, 6층을 통과시킨 뒤 나온 토큰별 벡터(T × 384)를 **평균(mean pooling)**해서 384차원 하나로 만들고, 길이 1로 정규화한다.

```
입력:  "When do I see the dentist next?"  → 토큰 ~10개
encoder 6층 → H ∈ ℝ^(T×384)
mean pooling:  e = (1/T') · ∑_{t: mask=1} H[t]     (padding 토큰은 빼고 평균)
L2 normalize:  ê = e / |e|                         → |ê| = 1
```

말로 하면: 문장 안 모든 토큰의 "의미 벡터"를 평균내서 문장 전체의 대표 방향을 얻는다. padding을 평균에 넣으면 짧은 문장일수록 0쪽으로 끌려가므로 attention mask로 빼야 한다.

`sentence-transformers` 패키지는 이 venv에 없다. 그래서 `transformers`로 직접 로드하고 pooling을 손으로 쓴다. 이렇게 하면 패키지가 숨기던 단계가 다 보인다 (온디바이스 런타임으로 옮길 때 어차피 이 단계를 직접 구현해야 한다).

```python
# emb.py — 이후 모든 예제가 import 하는 임베딩 함수
import numpy as np, torch
from transformers import AutoTokenizer, AutoModel
NAME = "sentence-transformers/all-MiniLM-L6-v2"
tok = AutoTokenizer.from_pretrained(NAME, local_files_only=True)
model = AutoModel.from_pretrained(NAME, local_files_only=True).eval()

def embed(texts, bs=64):
    out = []
    for i in range(0, len(texts), bs):
        b = tok(texts[i:i+bs], padding=True, truncation=True, max_length=128, return_tensors="pt")
        with torch.no_grad():
            h = model(**b).last_hidden_state            # [B, T, 384]
        m = b["attention_mask"].unsqueeze(-1).float()   # [B, T, 1]
        v = (h * m).sum(1) / m.sum(1)                   # mean pooling (padding 제외)
        v = torch.nn.functional.normalize(v, dim=-1)    # L2 normalize -> |v| = 1
        out.append(v.numpy())
    return np.concatenate(out).astype(np.float32)
```

### 2.4 코드로 확인 — 기억 200개, 질문 30개

아래 코드는 Robin의 기억 200개와 일부러 말을 바꾼 질문 30개(예: 메모는 "allergic to penicillin", 질문은 "Which antibiotics should I avoid?")를 임베딩하고, 최근접 이웃과 recall@k를 본다. **recall@k** = 정답 기억이 상위 k개 안에 들어온 질문의 비율.

```python
import time, numpy as np, torch
torch.manual_seed(0); torch.set_num_threads(4)
from memdata import all_memories, QUERIES
from emb import embed, model
mems = all_memories()
texts = [m[1] for m in mems]; ids = [m[0] for m in mems]
print("params:", sum(p.numel() for p in model.parameters()))
t0 = time.perf_counter(); M = embed(texts); t1 = time.perf_counter()
Q = embed([q for q, _ in QUERIES])
print("M:", M.shape, M.dtype, "norm[0]=%.4f" % np.linalg.norm(M[0]))
print("embed %d memories: %.0f ms (%.2f ms/item)" % (len(texts), (t1-t0)*1e3, (t1-t0)*1e3/len(texts)))
S = Q @ M.T                                  # [30, 200] cosine (already normalized)
for qi in [0, 8, 12]:
    top = np.argsort(-S[qi])[:3]
    print(f"\nQ: {QUERIES[qi][0]}  (gold {QUERIES[qi][1]})")
    for j in top:
        print(f"  {S[qi,j]:.3f} {ids[j]} {texts[j][:62]}")
gold = np.array([ids.index(g) for _, g in QUERIES])
rank = np.argsort(-S, axis=1)
for k in (1, 3, 5):
    print(f"recall@{k} = {np.mean([gold[i] in rank[i,:k] for i in range(len(gold))]):.3f}")
np.save("M.npy", M); np.save("Q.npy", Q)
miss = [i for i in range(len(gold)) if rank[i,0] != gold[i]]
for i in miss: print("miss@1:", QUERIES[i][0], "->", ids[rank[i,0]])
ts = []
for _ in range(20):
    t0 = time.perf_counter(); embed(["When do I see the dentist next?"]); ts.append(time.perf_counter()-t0)
print("single query embed: median %.1f ms" % (np.median(ts)*1e3))
```

```text
params: 22713216
M: (200, 384) float32 norm[0]=1.0000
embed 200 memories: 263 ms (1.32 ms/item)

Q: When do I see the dentist next?  (gold m000)
  0.708 m002 Mom's dentist appointment is on October 20, 2026 at 10 AM; I p
  0.659 m000 Dentist appointment with Dr. Alvarez on Thursday, October 8, 2
  0.589 m001 Had a dental cleaning with Dr. Alvarez on March 3, 2026; next 

Q: Where did I leave the car at the airport?  (gold m008)
  0.625 m008 Parked the car at the airport in Lot C, row 14, on October 1, 
  0.348 m005 I prefer an aisle seat on flights, never the middle.
  0.329 m009 Parked at the mall garage level B, spot 3, on September 5, 202

Q: Which antibiotics should I avoid?  (gold m014)
  0.574 m014 I am allergic to penicillin.
  0.175 m017 Dog Mochi needs his heartworm pill on the first day of every m
  0.155 m019 I don't eat cilantro; it tastes like soap to me.
recall@1 = 0.967
recall@3 = 1.000
recall@5 = 1.000
miss@1: When do I see the dentist next? -> m002
single query embed: median 3.6 ms
```

(첫 실행은 워밍업 때문에 200개에 960 ms가 걸렸다. 위는 두 번째 실행이다.)

출력에서 볼 것:

- "antibiotics"와 "penicillin"은 단어가 하나도 겹치지 않는데 0.574로 1등이다. 키워드 검색(grep)으로는 못 찾는다. 이게 임베딩 검색의 힘이다.
- 30개 중 29개가 1등으로 맞았다. 틀린 하나가 교훈적이다. "When do I see the **dentist** next?"에 **엄마의** 치과 예약(m002, 0.708)이 내 예약(m000, 0.659)보다 높다. 임베딩은 "치과 예약"이라는 주제는 잘 잡지만 **누구의**, **언제의**를 잘 구분하지 못한다. 이건 모델 탓이라기보다 임베딩이라는 표현의 한계다. §6의 메타데이터 필터와 §7의 RAG 실험에서 이 문제를 다시 만난다.
- 공항 질문에서 3등이 "mall garage" 주차다. 같은 "주차" 주제지만 점수 차(0.625 vs 0.329)가 크다. 점수의 **절대값**보다 1등과 2등의 **차이(margin)**가 확신도를 말해 준다.
- 크기: 파라미터 2,271만 개 → fp32 약 91 MB, int8로 양자화하면 약 23 MB (C1). 질문 하나 임베딩 3.6 ms(M2 CPU, 4스레드).

### 2.5 임베딩 모델 고르기 — 무엇을 보나

| 기준 | 왜 중요한가 | MiniLM-L6-v2의 경우 |
|---|---|---|
| 차원 d | 벡터 저장·검색 비용이 d에 비례 | 384 (작은 편. 768·1024짜리도 흔하다) |
| 파라미터 수 | flash·RAM·질문당 지연 | 22.7M |
| 최대 입력 길이 | 긴 메모는 잘린다 | 이 노트에선 128 토큰으로 자름 (모델 카드상 학습 길이도 짧다) |
| 언어 | 한국어 메모를 쓸 사용자라면 다국어 모델 필요 | 영어 위주 |
| 라이선스 | 제품 탑재 가능 여부 | Apache-2.0 (모델 카드 기준) |

LLM 자체(GGUF)로 임베딩을 뽑을 수도 있다 (`llama-embedding`). 모델을 하나로 합칠 수 있다는 장점이 있지만, 0.5B LLM은 MiniLM보다 20배 이상 크고, 검색용으로 학습된 모델이 아니면 품질도 보장되지 않는다. 보통은 작은 전용 임베딩 모델을 따로 둔다.

### 2.6 함정

- **임베딩 모델을 바꾸면 저장된 벡터 전부가 쓰레기가 된다.** 모델 A의 벡터 공간과 모델 B의 공간은 다른 좌표계다. DB에 `embed_model_version`을 꼭 같이 저장하고, 모델 업데이트 시 전체 재임베딩(마이그레이션)을 계획한다. 펌웨어로 치면 FTL 메타데이터 포맷 변경과 같다.
- 정규화를 잊으면 긴 문장(노름이 큰 벡터)이 내적 검색에서 이긴다.
- 질문과 메모의 "형태"가 다르다(질문형 vs 서술형). 어떤 모델은 질문 앞에 접두어(`query:`) 같은 규칙을 요구한다. 모델 카드를 확인한다. MiniLM-L6-v2는 접두어 없이 쓴다.

---

## 3. 임베딩 양자화 — float32 · int8 · binary

### 3.1 직관

벡터 하나가 384 × 4 B = 1,536 B다. 기억 1만 개면 15 MB. 폰에서는 작지만 웨어러블 RAM(수 MB~수십 MB, 추정)에서는 아깝다. C1에서 가중치를 int8로 줄인 것처럼 **저장된 임베딩도 양자화**할 수 있다. 그리고 임베딩 검색은 **순위만 맞으면 된다**는 점에서 가중치보다 관대하다. 점수가 0.708이든 0.70이든 순서만 같으면 결과가 같다.

세 가지 단계를 비교한다.

| 형식 | 벡터당 바이트 (384-d) | 방법 | 검색 연산 |
|---|---|---|---|
| float32 | 1,536 | 그대로 | float 내적 |
| float16 | 768 | 반정밀도로 저장 | float 내적 |
| int8 + scale | 384 + 4 = 388 | 벡터마다 대칭 scale `s = max(abs(x))/127`, `q = round(x/s)` | int8 × int8 → int32 누산, 마지막에 × s |
| binary | 384 / 8 = 48 | 부호 비트만: `b = (x > 0)` | XOR + popcount (Hamming 거리) |

### 3.2 손계산 — 4차원 벡터 하나

x = (0.40, −0.10, 0.25, −0.80) 을 int8과 binary로 바꿔 보자.

```
int8 (대칭, per-vector):
  s = max|x| / 127 = 0.80 / 127 = 0.0063
  q = round(x / s) = round(63.5, −15.9, 39.7, −127) = (64, −16, 40, −127)
  복원 x̂ = q · s = (0.403, −0.101, 0.252, −0.800)       ← 오차 0.003 이하

binary:
  b = (x > 0) = (1, 0, 1, 0)      → 4비트
  두 binary 벡터의 Hamming 거리 = popcount(b1 XOR b2) = 서로 다른 비트 수
```

말로 하면: int8은 "자"의 눈금을 255칸으로 나눠 값을 반올림하고, binary는 각 축에서 "양수냐 음수냐"만 남긴다. binary는 32배 압축이지만 크기 정보를 다 버린다. 그래도 고차원에서는 "부호가 같은 축의 개수"가 각도와 꽤 상관이 있다 (무작위 초평면으로 각도를 추정하는 SimHash 계열 아이디어와 같은 원리 — 단, 여기서는 무작위 초평면 대신 좌표축을 그대로 쓴다).

query는 양자화하지 않고 float로 두는 것이 흔하다(asymmetric). 저장 공간은 DB 쪽만 문제이기 때문이다. 아래 코드도 int8 경로에서 query는 float32다.

### 3.3 코드로 확인 — 5,040개에서 순위가 얼마나 흔들리나

기억 200개로는 recall이 이미 포화라 차이가 안 보인다. 그래서 템플릿 메모 5,000개를 더 섞어 5,040개로 늘리고, 같은 30개 질문으로 잰다. 지표 두 개:

- **top-10 overlap**: float32 결과 상위 10개와 몇 개가 겹치나 (양자화가 순위를 얼마나 바꾸나).
- **recall@1, @5**: 정답 기억을 찾았나 (사용자가 실제로 느끼는 품질).

```python
import numpy as np
from memdata import ANCHORS, QUERIES, filler
from emb import embed
mems = ANCHORS + filler(5000, seed=1)              # 5,040 items: anchors + many distractors
ids = [m[0] for m in mems]
M = embed([m[1] for m in mems]); Q = embed([q for q, _ in QUERIES])
gold = np.array([ids.index(g) for _, g in QUERIES])

s = np.abs(M).max(1, keepdims=True) / 127.0        # int8: per-vector symmetric scale
M8 = np.round(M / s).astype(np.int8)
Mb = np.packbits(M > 0, axis=1)                    # binary: sign bit -> 384 bits = 48 B
Qb = np.packbits(Q > 0, axis=1)

def scores(name):
    if name == "float32": return Q @ M.T
    if name == "float16": return Q.astype(np.float16).astype(np.float32) @ M.astype(np.float16).astype(np.float32).T
    if name == "int8":    return (Q @ M8.T.astype(np.float32)) * s.T   # query stays float
    if name == "binary":  return -np.bitwise_count(Qb[:, None, :] ^ Mb[None]).sum(-1).astype(np.float32)
ref = np.argsort(-scores("float32"), 1)[:, :10]
for name, nbytes in [("float32", 1536), ("float16", 768), ("int8", 384 + 4), ("binary", 48)]:
    r = np.argsort(-scores(name), 1)
    ov = np.mean([len(set(r[i, :10]) & set(ref[i])) / 10 for i in range(len(Q))])
    r1 = np.mean(r[:, 0] == gold); r5 = np.mean([gold[i] in r[i, :5] for i in range(len(Q))])
    print(f"{name:8s} {nbytes:5d} B/vec  top10-overlap={ov:.3f}  recall@1={r1:.3f}  recall@5={r5:.3f}")
# binary 1st pass (top-C) -> rerank those C with float32
hb_all = np.argsort(-scores("binary"), 1)
for C in (50, 200, 1000):
    hb = hb_all[:, :C]
    rr = np.array([hb[i][np.argsort(-(M[hb[i]] @ Q[i]))] for i in range(len(Q))])
    ov = np.mean([len(set(rr[i, :10]) & set(ref[i])) / 10 for i in range(len(Q))])
    print(f"binary top-{C:<4d} + float rerank: top10-overlap={ov:.3f}  recall@1={np.mean(rr[:,0]==gold):.3f}")
```

```text
float32   1536 B/vec  top10-overlap=1.000  recall@1=0.967  recall@5=1.000
float16    768 B/vec  top10-overlap=0.983  recall@1=0.967  recall@5=1.000
int8       388 B/vec  top10-overlap=0.957  recall@1=0.967  recall@5=1.000
binary      48 B/vec  top10-overlap=0.287  recall@1=0.933  recall@5=0.967
binary top-50   + float rerank: top10-overlap=0.477  recall@1=0.967
binary top-200  + float rerank: top10-overlap=0.717  recall@1=0.967
binary top-1000 + float rerank: top10-overlap=0.933  recall@1=0.967
```

```svg
<svg viewBox="0 0 640 250" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" text-anchor="middle" font-size="14">벡터 1개당 바이트 (384-d) 와 float32 top-10 일치율</text><text x="122" y="65" text-anchor="end" font-size="13">float32</text><rect x="130" y="50" width="360.0" height="24" fill="#4a7bd0"/><text x="496.0" y="67" font-size="13">1536 B</text><text x="600" y="67" text-anchor="end" font-size="13">overlap 1.000</text><text x="122" y="113" text-anchor="end" font-size="13">float16</text><rect x="130" y="98" width="180.0" height="24" fill="#3f9a6b"/><text x="316.0" y="115" font-size="13">768 B</text>
<text x="600" y="115" text-anchor="end" font-size="13">overlap 0.983</text><text x="122" y="161" text-anchor="end" font-size="13">int8 + scale</text><rect x="130" y="146" width="90.9" height="24" fill="#e08a3c"/><text x="226.9" y="163" font-size="13">388 B</text><text x="600" y="163" text-anchor="end" font-size="13">overlap 0.957</text><text x="122" y="209" text-anchor="end" font-size="13">binary (sign)</text><rect x="130" y="194" width="11.2" height="24" fill="#d0564a"/><text x="147.2" y="211" font-size="13">48 B</text>
<text x="600" y="211" text-anchor="end" font-size="13">overlap 0.287</text><line x1="130" y1="40" x2="130" y2="240" stroke="currentColor"/>
</svg>
```

그림 2 — 벡터 하나당 바이트(막대 길이)와 float32 대비 top-10 일치율. int8은 1/4 크기에 순위가 거의 그대로, binary는 1/32 크기에 순위가 크게 흔들린다.

출력에서 볼 것:

- **int8은 거의 공짜다.** 크기 1/4, 정답 recall은 float32와 똑같고, top-10 순위도 96%가 같다. 기억 저장의 기본값으로 삼을 만하다.
- **binary는 1/32 크기지만 순위가 크게 흔들린다** (top-10 overlap 0.287). 그래도 정답 recall@1은 0.967 → 0.933으로 한 문제만 놓쳤다. 이 데이터의 템플릿 메모들은 서로 매우 비슷해서 점수 차가 아주 작은 "동점권"이 많다. 그래서 overlap은 가혹하게 떨어지지만, 질문과 확실히 가까운 정답은 binary로도 대부분 잡힌다.
- **2단 검색(binary로 후보 → float로 재정렬)**이 실전 패턴이다. 후보 1,000개(전체의 20%)만 float로 다시 보면 top-10이 93% 회복된다. 1단계는 48 B짜리 벡터를 XOR+popcount로 훑고, 2단계는 소수만 flash에서 읽어 정밀 계산한다. 펌웨어로 치면 Bloom filter나 해시로 후보를 거르고 실제 비교는 소수만 하는 것과 같다.

### 3.4 임베디드 연결

- int8 검색은 C1·C3의 int8 GEMV와 똑같다. ARM의 `SDOT`(dot product 명령, Armv8.2의 선택 확장), Hexagon HVX 같은 SIMD가 그대로 쓰인다. §12에 C 구현이 있다.
- binary 검색은 ARM NEON의 `CNT`(바이트 단위 popcount) 같은 명령으로 매우 빠르다. 48 B = 캐시 라인 하나(64 B) 안에 들어간다.
- scale을 벡터마다 두면 4 B가 더 든다 (388 B). 전체 공통 scale 하나로 줄이면 384 B지만, 벡터마다 최대값이 다르면 정밀도가 떨어진다 (C1 §5 granularity와 같은 이야기).

### 3.5 함정

- numpy는 int8 행렬곱에 BLAS를 쓰지 않아서 int8이 float32보다 **느리다**. 위 코드가 `M8`을 float로 바꿔서 곱한 이유다. int8의 속도 이득은 C·NEON·NPU에서 나온다. 호스트 numpy 속도로 기기 속도를 판단하지 말 것.
- binary를 "임베딩 모델이 binary용으로 학습되지 않았는데" 쓰면 손실이 크다. 어떤 모델은 양자화 친화적으로 학습되었다고 밝히기도 한다 — 모델 카드를 확인하고, 항상 **자기 데이터로 recall을 측정**한다.

---

## 4. 온디바이스 벡터 검색 ① — brute force

### 4.1 정의

brute force(전수 검색, flat search) = 저장된 N개 벡터 전부와 내적을 계산하고 상위 k개를 고른다.

```
점수 계산:   s = M · q          M ∈ ℝ^(N×d), q ∈ ℝ^d     → N·d MAC
top-k 선택:  argpartition(−s, k)                         → O(N)
```

말로 하면: 행렬-벡터 곱(GEMV) 한 번과 선택 한 번이다. 정확도는 100%(정의상 정답)이고, 비용은 N에 정비례한다.

손계산: N = 10,000, d = 384 → 3.84M MAC. 데이터는 float32면 15.4 MB, int8이면 3.9 MB. GEMV는 산술 강도(arithmetic intensity, D3)가 1 MAC/원소 정도로 아주 낮아서 **메모리 대역폭이 시간을 정한다**. 15.4 MB를 30 GB/s로 읽으면 약 0.5 ms, 캐시에 다 들어가면 그보다 훨씬 빠르다.

### 4.2 코드로 확인 — N을 1k에서 100k까지

아래 코드는 무작위 단위 벡터 N개에 대해 질문 1개를 검색하는 시간을 잰다 (중앙값 50회).

```python
import time, numpy as np
rng = np.random.default_rng(0)
D = 384
def unit(x): return (x / np.linalg.norm(x, axis=1, keepdims=True)).astype(np.float32)
def bench(f, reps=50):
    f(); ts = []
    for _ in range(reps):
        t0 = time.perf_counter(); f(); ts.append(time.perf_counter() - t0)
    return np.median(ts) * 1e3
q = unit(rng.standard_normal((1, D)))[0]; qb = np.packbits(q > 0)
print("     N   fp32 MB  fp32 ms  top10 ms  bin MB  bin ms")
for N in (1_000, 10_000, 30_000, 100_000):
    M = unit(rng.standard_normal((N, D)))
    Mb = np.packbits(M > 0, axis=1)
    t_dot = bench(lambda: M @ q)
    t_top = bench(lambda: np.argpartition(-(M @ q), 10)[:10])
    t_bin = bench(lambda: np.bitwise_count(Mb ^ qb).sum(1))
    print(f"{N:6d} {M.nbytes/1e6:8.1f} {t_dot:8.3f} {t_top:9.3f} {Mb.nbytes/1e6:7.2f} {t_bin:7.3f}")
```

```text
     N   fp32 MB  fp32 ms  top10 ms  bin MB  bin ms
  1000      1.5    0.015     0.023    0.05   0.062
 10000     15.4    0.129     0.214    0.48   0.348
 30000     46.1    1.522     2.410    1.44   1.497
100000    153.6    6.234     7.194    4.80   6.203
```

```svg
<svg viewBox="0 0 640 360" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" text-anchor="middle" font-size="14">brute-force 검색 1회 시간 vs 저장된 벡터 수 (M2, numpy fp32, log-log)</text><line x1="80" y1="300" x2="600" y2="300" stroke="currentColor"/><line x1="80" y1="40" x2="80" y2="300" stroke="currentColor"/><line x1="80.0" y1="300" x2="80.0" y2="305" stroke="currentColor"/><text x="80.0" y="320" text-anchor="middle" font-size="12">1k</text><line x1="340.0" y1="300" x2="340.0" y2="305" stroke="currentColor"/><text x="340.0" y="320" text-anchor="middle" font-size="12">10k</text><line x1="464.1" y1="300" x2="464.1" y2="305" stroke="currentColor"/>
<text x="464.1" y="320" text-anchor="middle" font-size="12">30k</text><line x1="600.0" y1="300" x2="600.0" y2="305" stroke="currentColor"/><text x="600.0" y="320" text-anchor="middle" font-size="12">100k</text><line x1="75" y1="300.0" x2="80" y2="300.0" stroke="currentColor"/><text x="71" y="304.0" text-anchor="end" font-size="12">0.01</text><line x1="75" y1="213.3" x2="80" y2="213.3" stroke="currentColor"/><text x="71" y="217.3" text-anchor="end" font-size="12">0.1</text><line x1="75" y1="126.7" x2="80" y2="126.7" stroke="currentColor"/>
<text x="71" y="130.7" text-anchor="end" font-size="12">1</text><line x1="75" y1="40.0" x2="80" y2="40.0" stroke="currentColor"/><text x="71" y="44.0" text-anchor="end" font-size="12">10</text><text x="340.0" y="342" text-anchor="middle" font-size="12">N (벡터 수)</text><text x="22" y="170.0" font-size="12" transform="rotate(-90 22 170.0)" text-anchor="middle">ms / query</text><polyline points="80.0,284.7 340.0,203.7 464.1,110.9 600.0,57.8" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="80.0" cy="284.7" r="4" fill="#4a7bd0"/><circle cx="340.0" cy="203.7" r="4" fill="#4a7bd0"/>
<circle cx="464.1" cy="110.9" r="4" fill="#4a7bd0"/><circle cx="600.0" cy="57.8" r="4" fill="#4a7bd0"/><polyline points="80.0,268.7 340.0,184.7 464.1,93.6 600.0,52.4" fill="none" stroke="#e08a3c" stroke-width="2"/><circle cx="80.0" cy="268.7" r="4" fill="#e08a3c"/><circle cx="340.0" cy="184.7" r="4" fill="#e08a3c"/><circle cx="464.1" cy="93.6" r="4" fill="#e08a3c"/><circle cx="600.0" cy="52.4" r="4" fill="#e08a3c"/><rect x="348.0" y="48" width="108.1" height="244" fill="#888" opacity="0.15"/><text x="402.0" y="68" text-anchor="middle" font-size="12">15 MB→46 MB</text>
<text x="402.0" y="84" text-anchor="middle" font-size="12">캐시 밖으로</text><line x1="100" y1="330" x2="125" y2="330" stroke="#4a7bd0" stroke-width="2"/><text x="130" y="334" font-size="12">M @ q (점수만)</text><line x1="300" y1="330" x2="325" y2="330" stroke="#e08a3c" stroke-width="2"/><text x="330" y="334" font-size="12">+ argpartition top-10</text>
</svg>
```

그림 3 — brute-force 검색 시간과 N (log-log). 10k→30k 사이에서 기울기가 가팔라진다. 데이터가 15 MB에서 46 MB로 커지며 칩 내부 캐시를 넘어 DRAM 대역폭에 묶이기 때문이다.

출력에서 볼 것:

- **기억 1만 개는 0.13 ms다.** 사람이 질문하고 답을 듣는 데 걸리는 수백 ms~수 초에 비하면 0이다. 개인 기억 규모(수천~수만 개)에서는 brute force로 충분하다는 것이 이 노트의 첫 번째 결론이다.
- 10k(15.4 MB/0.129 ms ≈ 119 GB/s)와 100k(153.6 MB/6.2 ms ≈ 25 GB/s)의 "유효 대역폭"이 다르다. 앞은 캐시 안, 뒤는 DRAM이다. D3 roofline의 메모리 지붕 그대로다. M2 P-core 클러스터의 L2가 16 MB급이라는 점과 맞아떨어진다.
- 마지막 열(binary)은 numpy에서 **오히려 느리다**. 데이터는 32배 작은데 `bitwise_count`가 바이트 단위로 돌고 `.sum(1)`이 따로 한 번 더 돈다. 같은 연산을 C로 짜면 int8보다 5배 빠르다 (§12 실측). 다시 한번: 호스트 Python의 상대 속도로 알고리즘을 판정하지 말 것.

### 4.3 언제 brute force로 충분한가

```
  N (기억 수)      fp32 크기     int8 크기    brute force (M2, 추정 포함)
  1,000            1.5 MB       0.4 MB      ~0.02 ms
  10,000           15 MB        3.9 MB      ~0.1–0.2 ms
  100,000          154 MB       39 MB       ~6 ms (DRAM bound)
  1,000,000        1.5 GB       390 MB      RAM에 못 올림 → ANN·디스크 인덱스 필요
```

하루에 기억 30개를 저장해도 10년이면 약 11만 개다. 개인 비서 기억은 대부분 **10만 개 이하**에 머문다. 이 영역에서는 정확도 100%인 brute force가 기본값이고, ANN(다음 절)은 "brute force가 지연·전력 예산을 넘을 때" 꺼내는 도구다.

---

## 5. 온디바이스 벡터 검색 ② — 근사 검색 (ANN)

### 5.1 왜 근사인가

**ANN(approximate nearest neighbor)** = 정확한 top-k 대신 "거의 맞는" top-k를 훨씬 적은 연산으로 찾는 방법. recall@10이 0.95면 "진짜 상위 10개 중 평균 9.5개를 찾는다"는 뜻이다. 대표적인 세 가지:

| 방법 | 아이디어 | 펌웨어 비유 | 메모리 | 갱신(삽입·삭제) |
|---|---|---|---|---|
| IVF (inverted file) | k-means로 공간을 nlist개 칸으로 나누고, 질문과 가까운 nprobe개 칸만 전수 비교 | 해시 버킷 / set-associative 캐시의 set 선택 | 원본 + centroid | 쉬움 (가까운 칸에 추가) |
| HNSW (hierarchical navigable small world) | 이웃끼리 링크한 다층 그래프를 위에서부터 탐욕적으로 내려감 | skip list | 원본 + 벡터당 링크 수십 개 | 삽입 쉬움, 삭제는 까다로움 |
| PQ (product quantization) | 벡터를 m조각으로 나누고 조각마다 256개 대표값 중 하나의 번호(1 B)로 저장 | 조각별 LUT 압축 | 벡터당 m 바이트 | 코드북 고정이면 쉬움 |

IVF와 PQ는 함께 쓰는 경우가 많다 (IVF-PQ: 칸을 고르고, 칸 안은 PQ 코드로 비교). Faiss 라이브러리가 이 조합들을 구현한 대표 구현체다.

```svg
<svg viewBox="0 0 660 270" xmlns="http://www.w3.org/2000/svg">
<circle cx="90" cy="90" r="56" fill="#888" opacity="0.08" stroke="#888" stroke-dasharray="4 3"/><circle cx="135" cy="34" r="3" fill="#888"/><circle cx="99" cy="78" r="3" fill="#888"/><circle cx="80" cy="85" r="3" fill="#888"/><circle cx="46" cy="85" r="3" fill="#888"/><circle cx="71" cy="163" r="3" fill="#888"/><circle cx="95" cy="82" r="3" fill="#888"/><circle cx="84" cy="75" r="3" fill="#888"/><circle cx="67" cy="81" r="3" fill="#888"/><circle cx="101" cy="85" r="3" fill="#888"/><rect x="85" y="85" width="10" height="10" fill="#e08a3c"/>
<text x="90" y="28" text-anchor="middle" font-size="12">list 0</text><circle cx="210" cy="70" r="56" fill="#888" opacity="0.08" stroke="#888" stroke-dasharray="4 3"/><circle cx="231" cy="66" r="3" fill="#888"/><circle cx="211" cy="104" r="3" fill="#888"/><circle cx="222" cy="59" r="3" fill="#888"/><circle cx="206" cy="82" r="3" fill="#888"/><circle cx="253" cy="64" r="3" fill="#888"/><circle cx="205" cy="92" r="3" fill="#888"/><circle cx="190" cy="64" r="3" fill="#888"/><circle cx="229" cy="83" r="3" fill="#888"/><circle cx="212" cy="85" r="3" fill="#888"/>
<rect x="205" y="65" width="10" height="10" fill="#e08a3c"/><text x="210" y="8" text-anchor="middle" font-size="12">list 1</text><circle cx="150" cy="190" r="56" fill="#888" opacity="0.08" stroke="#888" stroke-dasharray="4 3"/><circle cx="88" cy="212" r="3" fill="#888"/><circle cx="129" cy="153" r="3" fill="#888"/><circle cx="156" cy="205" r="3" fill="#888"/><circle cx="140" cy="166" r="3" fill="#888"/><circle cx="151" cy="189" r="3" fill="#888"/><circle cx="181" cy="206" r="3" fill="#888"/><circle cx="154" cy="214" r="3" fill="#888"/><circle cx="145" cy="170" r="3" fill="#888"/>
<circle cx="163" cy="203" r="3" fill="#888"/><rect x="145" y="185" width="10" height="10" fill="#e08a3c"/><text x="150" y="266" text-anchor="middle" font-size="12">list 2</text><circle cx="290" cy="170" r="56" fill="#4a7bd0" opacity="0.18" stroke="#4a7bd0" stroke-dasharray="4 3"/><circle cx="285" cy="153" r="3" fill="#4a7bd0"/><circle cx="295" cy="115" r="3" fill="#4a7bd0"/><circle cx="305" cy="181" r="3" fill="#4a7bd0"/><circle cx="254" cy="171" r="3" fill="#4a7bd0"/><circle cx="269" cy="187" r="3" fill="#4a7bd0"/><circle cx="245" cy="150" r="3" fill="#4a7bd0"/>
<circle cx="306" cy="195" r="3" fill="#4a7bd0"/><circle cx="243" cy="159" r="3" fill="#4a7bd0"/><circle cx="297" cy="157" r="3" fill="#4a7bd0"/><rect x="285" y="165" width="10" height="10" fill="#e08a3c"/><text x="290" y="246" text-anchor="middle" font-size="12">list 3</text><circle cx="330" cy="70" r="56" fill="#4a7bd0" opacity="0.18" stroke="#4a7bd0" stroke-dasharray="4 3"/><circle cx="365" cy="44" r="3" fill="#4a7bd0"/><circle cx="338" cy="47" r="3" fill="#4a7bd0"/><circle cx="361" cy="70" r="3" fill="#4a7bd0"/><circle cx="322" cy="32" r="3" fill="#4a7bd0"/>
<circle cx="367" cy="87" r="3" fill="#4a7bd0"/><circle cx="347" cy="95" r="3" fill="#4a7bd0"/><circle cx="338" cy="56" r="3" fill="#4a7bd0"/><circle cx="312" cy="52" r="3" fill="#4a7bd0"/><circle cx="360" cy="38" r="3" fill="#4a7bd0"/><rect x="325" y="65" width="10" height="10" fill="#e08a3c"/><text x="330" y="8" text-anchor="middle" font-size="12">list 4</text><path d="M318,119 L321,125 L327,125 L322,130 L324,137 L318,133 L312,137 L314,130 L309,125 L315,125 Z" fill="#d0564a"/><text x="330" y="132" font-size="12">query</text>
<text x="410" y="60" font-size="13">① 학습: k-means로 centroid(■) 256개</text><text x="410" y="86" font-size="13">② 저장: 각 벡터를 가장 가까운 list에</text><text x="410" y="112" font-size="13">③ 검색: query와 centroid만 먼저 비교</text><text x="410" y="138" font-size="13">④ 가까운 nprobe개 list(파랑)만 전수 비교</text><text x="410" y="164" font-size="13">→ 경계 근처 이웃은 놓칠 수 있다</text><text x="410" y="190" font-size="13">   (recall이 1보다 작아짐, nprobe로 조절)</text>
</svg>
```

그림 4 — IVF 개념도. 벡터(점)를 가장 가까운 centroid(■)의 list에 넣어 두고, 질문(★)이 오면 centroid만 먼저 비교해 가까운 list 2개(파랑)만 전수 비교한다. 질문이 칸 경계 근처면 진짜 이웃이 옆 칸에 있어서 놓칠 수 있다.

### 5.2 PQ 손계산 — 왜 48 B인가

384-d 벡터를 m = 48조각(조각당 8차원)으로 자르고, 각 조각 위치마다 k-means로 대표 벡터 256개(코드북)를 학습한다.

```
저장:      벡터 하나 = 48조각 × (0..255 번호 1 B) = 48 B          (float32 대비 1/32)
코드북:    48 × 256 × 8차원 × 4 B = 393,216 B ≈ 384 KB           (전체에서 한 번)
검색 준비: 질문 q를 48조각으로 자르고, 조각별로 256개 대표와의 내적표 T[48][256] 계산
           = 48 × 256 × 8 = 98,304 MAC  (질문당 한 번)
점수:      s(x) = ∑_{j=0..47} T[j][code_x[j]]                    (벡터당 표 조회 48번 + 덧셈)
```

말로 하면: 곱셈을 미리 표로 만들어 두고, 각 벡터는 "표에서 48칸을 찾아 더하기"만 한다. 펌웨어에서 CRC를 테이블 조회로 계산하는 것과 정확히 같은 트릭이다. binary도 48 B였지만 PQ는 조각별 대표값을 학습하므로 보통 같은 크기에서 더 정확하다 (단, 학습과 코드북이 필요하다).

### 5.3 코드로 확인 — tiny IVF를 직접 만들기

scikit-learn의 `KMeans`로 centroid 256개를 학습하고, 벡터를 list별로 연속 메모리에 모아 둔다 (C로 짜면 list별 배열 + offset 테이블). 데이터는 "주제" 500개 주위에 흩어진 100k개 합성 벡터다 (실제 임베딩처럼 군집이 있게).

```python
import time, numpy as np
from sklearn.cluster import KMeans
rng = np.random.default_rng(0); D, N, K = 384, 100_000, 10
def unit(x): return (x / np.linalg.norm(x, axis=1, keepdims=True)).astype(np.float32)
topics = unit(rng.standard_normal((500, D)))                       # 500 hidden "topics"
X = unit(topics[rng.integers(0, 500, N)] + 0.07 * rng.standard_normal((N, D)))
Qs = unit(X[rng.integers(0, N, 200)] + 0.03 * rng.standard_normal((200, D)))
truth = [set(np.argpartition(-(X @ q), K)[:K]) for q in Qs]         # exact top-10

t0 = time.perf_counter()
km = KMeans(n_clusters=256, n_init=1, max_iter=20, random_state=0).fit(X[:20_000])
C = unit(km.cluster_centers_); assign = np.argmax(X @ C.T, 1)       # nearest centroid
order = np.argsort(assign, kind="stable"); Xs = X[order]            # pack lists contiguously
starts = np.searchsorted(assign[order], np.arange(257))
print(f"build: {time.perf_counter()-t0:.1f} s, list sizes min/median/max = "
      f"{np.diff(starts).min()}/{int(np.median(np.diff(starts)))}/{np.diff(starts).max()}")

def ivf(q, nprobe):
    lists = np.argpartition(-(C @ q), nprobe)[:nprobe]
    idx = np.concatenate([np.arange(starts[l], starts[l+1]) for l in lists])
    s = np.concatenate([Xs[starts[l]:starts[l+1]] @ q for l in lists])  # contiguous slices
    return order[idx[np.argpartition(-s, K)[:K]]], len(idx)
def run(f):
    t0 = time.perf_counter(); res = [f(q) for q in Qs]; t = (time.perf_counter() - t0) / len(Qs)
    return res, t * 1e3
res, t = run(lambda q: np.argpartition(-(X @ q), K)[:K])
print(f"brute     : recall@10=1.000  scanned=100000  {t:.3f} ms/query")
for nprobe in (1, 2, 4, 8, 16, 32, 64):
    res, t = run(lambda q: ivf(q, nprobe))
    rec = np.mean([len(set(r) & tr) / K for (r, _), tr in zip(res, truth)])
    scanned = np.mean([n for _, n in res])
    print(f"nprobe={nprobe:3d}: recall@10={rec:.3f}  scanned={scanned:6.0f}  {t:.3f} ms/query")
```

```text
build: 11.7 s, list sizes min/median/max = 190/248/2304
brute     : recall@10=1.000  scanned=100000  16.553 ms/query
nprobe=  1: recall@10=0.873  scanned=   737  0.477 ms/query
nprobe=  2: recall@10=0.885  scanned=  1107  0.340 ms/query
nprobe=  4: recall@10=0.917  scanned=  1938  0.566 ms/query
nprobe=  8: recall@10=0.930  scanned=  3541  0.854 ms/query
nprobe= 16: recall@10=0.947  scanned=  6724  1.622 ms/query
nprobe= 32: recall@10=0.955  scanned= 13011  3.029 ms/query
nprobe= 64: recall@10=0.974  scanned= 25496  6.681 ms/query
```

```svg
<svg viewBox="0 0 640 340" xmlns="http://www.w3.org/2000/svg">
<text x="340" y="22" text-anchor="middle" font-size="14">tiny IVF (256 lists, N=100k): 훑은 벡터 수 vs recall@10</text><line x1="80" y1="290" x2="600" y2="290" stroke="currentColor"/><line x1="80" y1="40" x2="80" y2="290" stroke="currentColor"/><line x1="148.0" y1="290" x2="148.0" y2="295" stroke="currentColor"/><text x="148.0" y="310" text-anchor="middle" font-size="12">1k</text><line x1="255.9" y1="290" x2="255.9" y2="295" stroke="currentColor"/><text x="255.9" y="310" text-anchor="middle" font-size="12">3k</text><line x1="374.0" y1="290" x2="374.0" y2="295" stroke="currentColor"/>
<text x="374.0" y="310" text-anchor="middle" font-size="12">10k</text><line x1="481.8" y1="290" x2="481.8" y2="295" stroke="currentColor"/><text x="481.8" y="310" text-anchor="middle" font-size="12">30k</text><line x1="600.0" y1="290" x2="600.0" y2="295" stroke="currentColor"/><text x="600.0" y="310" text-anchor="middle" font-size="12">100k</text><line x1="75" y1="290.0" x2="80" y2="290.0" stroke="currentColor"/><text x="71" y="294.0" text-anchor="end" font-size="12">0.85</text><line x1="75" y1="206.7" x2="80" y2="206.7" stroke="currentColor"/>
<text x="71" y="210.7" text-anchor="end" font-size="12">0.90</text><line x1="75" y1="123.3" x2="80" y2="123.3" stroke="currentColor"/><text x="71" y="127.3" text-anchor="end" font-size="12">0.95</text><line x1="75" y1="40.0" x2="80" y2="40.0" stroke="currentColor"/><text x="71" y="44.0" text-anchor="end" font-size="12">1.00</text><text x="340.0" y="332" text-anchor="middle" font-size="12">query당 실제로 내적한 벡터 수 (log)</text><text x="22" y="165.0" font-size="12" transform="rotate(-90 22 165.0)" text-anchor="middle">recall@10</text>
<polyline points="118.1,251.7 158.0,231.7 213.0,178.3 272.1,156.7 335.1,128.3 399.8,115.0 465.9,83.3" fill="none" stroke="#4a7bd0" stroke-width="2"/><circle cx="118.1" cy="251.7" r="4" fill="#4a7bd0"/><text x="118.1" y="242.7" text-anchor="middle" font-size="12">1</text><circle cx="158.0" cy="231.7" r="4" fill="#4a7bd0"/><text x="158.0" y="222.7" text-anchor="middle" font-size="12">2</text><circle cx="213.0" cy="178.3" r="4" fill="#4a7bd0"/><text x="213.0" y="169.3" text-anchor="middle" font-size="12">4</text><circle cx="272.1" cy="156.7" r="4" fill="#4a7bd0"/>
<text x="272.1" y="147.7" text-anchor="middle" font-size="12">8</text><circle cx="335.1" cy="128.3" r="4" fill="#4a7bd0"/><text x="335.1" y="119.3" text-anchor="middle" font-size="12">16</text><circle cx="399.8" cy="115.0" r="4" fill="#4a7bd0"/><text x="399.8" y="106.0" text-anchor="middle" font-size="12">32</text><circle cx="465.9" cy="83.3" r="4" fill="#4a7bd0"/><text x="465.9" y="74.3" text-anchor="middle" font-size="12">64</text><circle cx="600.0" cy="40.0" r="5" fill="#d0564a"/><text x="592.0" y="58.0" text-anchor="end" font-size="12">brute force (100k, recall 1.0)</text>
<text x="124.1" y="271.7" font-size="12">점 위 숫자 = nprobe</text>
</svg>
```

그림 5 — tiny IVF의 교환 곡선. 가로축은 질문 하나당 실제로 내적한 벡터 수(시간과 거의 비례, 기계 부하와 무관한 결정적 값). nprobe를 늘리면 recall이 오르지만 훑는 양도 늘어난다. 빨간 점이 brute force.

출력에서 볼 것:

- nprobe=1이면 전체의 0.7%만 훑고 recall@10 0.873, 시간은 brute force의 약 1/35. nprobe=64면 25%를 훑고 0.974. **recall 1.0에 가까워질수록 비용이 급격히 늘어난다** — ANN의 전형적인 모양이다.
- list 크기가 190~2,304로 고르지 않다. k-means 칸은 균일하지 않아서, 큰 list에 걸린 질문은 느리다. 실시간 시스템에서는 **최악 지연**이 중요하므로 이 꼬리가 문제다 (D6). 펌웨어의 해시 충돌 체인 길이 문제와 같다.
- brute force가 여기선 16.5 ms로 §4.2(7.2 ms)보다 느리게 나왔다. 질문 200개를 연달아 돌리는 루프였고, 다른 작업이 몰린 머신이었다. 그래서 그림 5의 가로축은 시간 대신 "훑은 개수"로 그렸다.
- 빌드(k-means)에 12초 걸렸다. 기기에서 기억이 추가될 때마다 다시 학습할 수는 없다. 실제로는 centroid를 가끔(예: 충전 중) 재학습하고, 그 사이 새 벡터는 기존 centroid에 할당만 한다.

### 5.4 HNSW는 왜 직접 안 만드나 — 그리고 폰에서의 선택

HNSW는 서버 벡터 DB에서 가장 많이 쓰는 ANN이다. 높은 recall에서 빠르다는 평가가 많다. 다만 온디바이스에서는 단점이 분명하다.

- 벡터마다 이웃 링크(수십 개 × 4 B ID)가 붙는다. 384 B짜리 int8 벡터에 링크가 100~200 B 붙으면 인덱스가 벡터 자체만큼 커진다.
- 그래프 탐색은 **무작위 메모리 접근**이다. 캐시·flash 친화적이지 않다 (SSD의 random read를 떠올리면 된다).
- 삭제가 어렵다. 노드를 빼면 그래프 연결성이 깨지므로 보통 "삭제 표시(tombstone)" 후 나중에 재구성한다. **§10의 "제대로 지우기"와 충돌한다** — tombstone된 노드의 벡터는 인덱스 안에 남아 있다.

정리하면:

| 상황 | 추천 |
|---|---|
| 기억 수천~수만 개, 폰·웨어러블 | int8 brute force (+ SQL 메타데이터 필터). 정확, 단순, 삭제 쉬움 |
| 수만~수십만, 지연 예산 빠듯 | binary/PQ 1단계 + int8·float 재정렬, 또는 IVF |
| 수백만 이상 | 기기보다 서버 영역. Faiss·HNSW 계열 |

### 5.5 함정

- ANN의 recall을 **자기 데이터와 자기 질문**으로 재지 않고 벤치마크 숫자만 믿는 것. 군집 구조에 따라 같은 nprobe에서도 recall이 크게 달라진다 (이 실험도 노이즈 0.05에서는 nprobe=1에서 recall 1.0, 0.08에서는 0.80이었다).
- 메타데이터 필터와 ANN을 섞을 때: ANN으로 top-10을 먼저 뽑고 필터를 걸면(post-filter) 필터를 통과하는 게 0개일 수 있다. 개인 기억 규모에서는 **필터 먼저(pre-filter) → 남은 것 brute force**가 가장 안전하다 (다음 절).

---

## 6. 저장 — SQLite에 벡터와 메타데이터를 같이

### 6.1 왜 SQLite인가

기억 저장소에는 벡터만 있는 게 아니다. 원문, 종류, 날짜, 프라이버시 등급, 누구에 관한 것인지, 출처, 임베딩 모델 버전… 이런 **메타데이터**로 거르고, 지우고, 사용자에게 보여 줘야 한다. 그건 데이터베이스가 잘하는 일이다. SQLite는 폰 OS(iOS·Android 모두)에 기본으로 들어 있고, 파일 하나, 트랜잭션(전원 차단에도 일관성), 인덱스를 준다. 펌웨어로 치면 "power-loss safe한 메타데이터 저널"을 이미 다 짜 둔 라이브러리다.

벡터는 `BLOB`(바이트 덩어리) 열에 `float32` 384개를 그대로 넣는다 (1,536 B). SQLite에 벡터 검색 기능은 없으므로 **SQL로 후보를 거르고, 벡터 비교는 numpy(기기에서는 C)로** 한다. 벡터 검색을 SQLite 확장으로 붙이는 오픈소스 프로젝트(예: sqlite-vec)도 있지만, 여기서는 원리를 보기 위해 직접 한다.

```
memory 테이블 한 행 (예)
┌──────┬──────────────────────────────────────┬───────┬────────────┬─────────┬──────────────┐
│ id   │ text                                 │ type  │ ts         │ privacy │ vec (BLOB)   │
├──────┼──────────────────────────────────────┼───────┼────────────┼─────────┼──────────────┤
│ m000 │ Dentist appointment with Dr. Alva... │ event │ 2026-10-08 │ health  │ 1536 bytes   │
└──────┴──────────────────────────────────────┴───────┴────────────┴─────────┴──────────────┘
INDEX idx_type_ts ON (type, ts)   ← "앞으로의 event만" 같은 필터를 빠르게
```

### 6.2 코드로 확인 — 필터 + 벡터 검색

아래 코드는 기억 200개를 SQLite에 넣고, 같은 질문을 필터 세 가지로 검색한다.

```python
import sqlite3, os, numpy as np
from memdata import all_memories, NOW
from emb import embed
mems = all_memories(); M = np.load("M.npy")              # 200 x 384 float32 from example 1
if os.path.exists("mem.db"): os.remove("mem.db")
db = sqlite3.connect("mem.db")
db.execute("""CREATE TABLE memory(id TEXT PRIMARY KEY, text TEXT, type TEXT,
              ts TEXT, privacy TEXT, vec BLOB)""")
db.execute("CREATE INDEX idx_type_ts ON memory(type, ts)")
db.executemany("INSERT INTO memory VALUES (?,?,?,?,?,?)",
               [(m[0], m[1], m[2], m[3], m[4], M[i].tobytes()) for i, m in enumerate(mems)])
db.commit()
print("db size:", os.path.getsize("mem.db"), "bytes for", len(mems), "rows")

def search(q, k=3, where="1=1", args=()):
    rows = db.execute(f"SELECT id, text, ts, vec FROM memory WHERE {where}", args).fetchall()
    V = np.frombuffer(b"".join(r[3] for r in rows), dtype=np.float32).reshape(len(rows), 384)
    s = V @ embed([q])[0]
    top = np.argsort(-s)[:k]
    print(f"  candidates={len(rows)}")
    for j in top: print(f"  {s[j]:.3f} {rows[j][0]} {rows[j][2]} {rows[j][1][:55]}")

q = "When do I see the dentist next?"
print("no filter:"); search(q)
print("filter: type='event' AND ts >= today"); search(q, where="type=? AND ts>=?", args=("event", NOW))
print("filter: privacy != 'health'"); search(q, where="privacy != ?", args=("health",))
```

```text
db size: 434176 bytes for 200 rows
no filter:
  candidates=200
  0.708 m002 2026-10-20 Mom's dentist appointment is on October 20, 2026 at 10 
  0.659 m000 2026-10-08 Dentist appointment with Dr. Alvarez on Thursday, Octob
  0.589 m001 2026-03-03 Had a dental cleaning with Dr. Alvarez on March 3, 2026
filter: type='event' AND ts >= today
  candidates=7
  0.708 m002 2026-10-20 Mom's dentist appointment is on October 20, 2026 at 10 
  0.659 m000 2026-10-08 Dentist appointment with Dr. Alvarez on Thursday, Octob
  0.362 m032 2026-10-12 Next haircut booked for October 12, 2026 at 11 AM with 
filter: privacy != 'health'
  candidates=193
  0.362 m032 2026-10-12 Next haircut booked for October 12, 2026 at 11 AM with 
  0.326 m028 2026-10-24 Anniversary dinner reservation at Nopa on October 24, 2
  0.312 f105 2026-01-26 Meeting with Grace about the customer escalation on 202
```

출력에서 볼 것:

- **시간 필터**는 과거 치과 기록(m001)을 지우고 후보를 200 → 7개로 줄였다. 벡터 비교량도 1/28이 된다. pre-filter는 품질과 속도를 함께 올린다.
- 그래도 엄마의 예약(m002)은 남는다. "누구에 관한 기억인가"는 시간·종류 필터로 못 거른다. §7에서 `about` 메타데이터를 추가해 해결한다.
- **프라이버시 필터의 함정**: "health 등급 기억은 검색하지 말자"는 정책을 걸면 치과 질문에 대한 답 자체가 사라진다. 1등이 미용실 예약(0.362)이다. 이 상태로 LLM에 넘기면 "미용실 예약이 10/12"라고 엉뚱하게 답할 수 있다. 프라이버시 필터는 **"검색하지 않는다"가 아니라 "이 맥락(예: 다른 사람이 들을 수 있는 스피커 출력, 클라우드로 보내는 프롬프트)에서는 쓰지 않는다"**로 설계해야 하고, 걸러졌을 때는 "건강 관련 정보라 여기서는 말하지 않을게요" 같은 명시적 경로가 필요하다.
- DB 크기 434,176 B / 200행 ≈ 2.2 KB/행. 벡터 1,536 B + 텍스트 ~80 B + 페이지·인덱스 오버헤드. 1만 개면 약 22 MB이고, int8로 넣으면 대략 1/3 수준으로 준다 (§11).

### 6.3 메타데이터 설계 — 무엇을 열로 둘까

| 열 | 예 | 쓰임 |
|---|---|---|
| `type` | event / fact / preference / conversation | 시간 필터 대상 결정, TTL 정책 |
| `ts` (사건 시각), `created` (저장 시각) | 2026-10-08 / 2026-09-30 | "다가오는", "최근" 질문, recency 점수 |
| `about` | self / mom / hana | "내" 일정 vs 가족 일정 구분 |
| `privacy` | normal / sensitive / health | 출력 채널·클라우드 전송 정책 (H6 §2 분류) |
| `source` | 대화 id, 앱 이름 | 출처 표시, 원본 삭제 시 연쇄 삭제 |
| `superseded_by` | m011 | 갱신된 선호의 옛 버전 표시 (§9) |
| `embed_model` | minilm-l6-v2@rev | 모델 교체 시 재임베딩 대상 찾기 |
| `expires_at` | 2027-04-01 | TTL (§9.5) |

---

## 7. RAG 파이프라인 — 0.5B LLM으로 개인 질문에 답하기

### 7.1 프롬프트 템플릿

검색된 기억을 프롬프트에 넣는 방식은 단순하다. 날짜를 같이 넣는 것이 중요하다 (LLM은 "오늘"을 모른다).

```
[system] You are Robin's private on-device assistant. Today is 2026-10-03.
         Answer in one short sentence. Use the memories if given;
         if the answer is not there, say you don't know.
[user]   Memories:
         - [2026-10-20] Mom's dentist appointment is on October 20, 2026 at 10 AM; ...
         - [2026-10-08] Dentist appointment with Dr. Alvarez on Thursday, October 8, ...
         - [2026-03-03] Had a dental cleaning with Dr. Alvarez on March 3, 2026; ...

         Question: When is my dentist appointment?
```

"모르면 모른다고 하라"는 지시는 hallucination을 줄이려는 것이다. 작은 모델은 이런 지시를 잘 안 따르기도 한다 — 아래에서 확인한다.

### 7.2 실험 설정

- 생성: `llama-server -m .tools/models/qwen2.5-0.5b-Q8_0.gguf --port 8095 -c 4096 -np 1 --temp 0 --seed 0` (Qwen2.5-0.5B-Instruct, Q8_0, 531 MB), OpenAI 호환 `/v1/chat/completions`, temperature 0 (매번 같은 답), 최대 48 토큰.
- 검색: §2의 MiniLM 임베딩, 기억 200개, top-k.
- 채점: 질문마다 정답 키워드 목록(예: 치과 → "3:30" 또는 "October 8")이 답에 들어 있으면 정답. 단순하지만 결정적이고 재현 가능하다. (실제 제품 평가는 사람 채점이나 더 정교한 checker가 필요하다 — A4.)

공용 헬퍼(`rag.py`)는 아래와 같다. `ABOUT`/`make_filter`는 §7.4에서 쓴다.

```python
import json, urllib.request, numpy as np
from memdata import all_memories, NOW
from emb import embed
mems = all_memories(); M = np.load("M.npy")
QA = [("When is my dentist appointment?",        ["3:30", "October 8"]),
      ("When is my sister's birthday?",          ["November 14", "Nov 14"]),
      ("Which seat do I prefer on flights?",     ["aisle"]),
      ("What's my gym locker number?",           ["217"]),
      ("What book did Priya recommend?",         ["Overstory"]),
      ("Where did I park at the airport?",       ["Lot C"]),
      ("Who is my manager?",                     ["Teresa"]),
      ("How do I take my coffee?",               ["oat"]),
      ("When does my passport expire?",          ["2027"]),
      ("What am I allergic to?",                 ["penicillin"])]
SYS = ("You are Robin's private on-device assistant. Today is %s. "
       "Answer in one short sentence. Use the memories if given; if the answer is not there, say you don't know." % NOW)

LAST = {}
def ask(question, memories):
    user = question if not memories else \
        "Memories:\n" + "\n".join(f"- [{m[3]}] {m[1]}" for m in memories) + f"\n\nQuestion: {question}"
    body = {"messages": [{"role": "system", "content": SYS}, {"role": "user", "content": user}],
            "temperature": 0, "max_tokens": 48, "seed": 0, "cache_prompt": False}
    req = urllib.request.Request("http://localhost:8095/v1/chat/completions",
                                 json.dumps(body).encode(), {"Content-Type": "application/json"})
    r = json.load(urllib.request.urlopen(req)); LAST.update(r.get("timings", {}))
    return r["choices"][0]["message"]["content"].strip(), r["usage"]["prompt_tokens"]

def retrieve(q, k=3, keep=lambda m: True):
    s = M @ embed([q])[0]
    order = [i for i in np.argsort(-s) if keep(mems[i])]
    return [mems[i] for i in order[:k]]

def ok(ans, keys): return any(k.lower() in ans.lower() for k in keys)

# metadata written at memory-creation time: who is this memory about? (default: the user)
ABOUT = {"m002": "mom", "m003": "hana", "m004": "jun", "m035": "hana", "m017": "mochi",
         "m018": "mochi", "m029": "lena", "m038": "daniel"}
ALIAS = {"mom": ["mom", "mother"], "hana": ["sister", "hana"], "jun": ["brother", "jun"],
         "mochi": ["dog", "mochi", "vet"], "lena": ["lena"], "daniel": ["daniel"]}
def make_filter(q):
    ql = q.lower()
    future = any(w in ql for w in ("appointment", "next", "upcoming"))
    def keep(m):
        who = ABOUT.get(m[0], "self")
        if who != "self" and not any(a in ql for a in ALIAS[who]): return False
        if future and m[2] == "event" and m[3] < NOW: return False
        return True
    return keep
```

`"cache_prompt": False`는 llama-server가 직전 요청과 겹치는 프롬프트 앞부분(system prompt)의 KV-cache를 재사용하지 못하게 해서, prefill 시간을 매번 처음부터 재기 위한 설정이다 (L6의 prompt caching이 바로 이것을 켜는 최적화다).

### 7.3 코드로 확인 — no-RAG vs RAG

```python
from rag import QA, ask, retrieve, ok
score = {"no-RAG": 0, "RAG k=3": 0}
for q, keys in QA:
    a0, t0 = ask(q, None)
    a1, t1 = ask(q, retrieve(q, k=3))
    score["no-RAG"] += ok(a0, keys); score["RAG k=3"] += ok(a1, keys)
    print(f"Q: {q}\n  no-RAG [{'O' if ok(a0, keys) else 'X'}] ({t0:3d} tok) {a0[:70]}"
          f"\n  RAG    [{'O' if ok(a1, keys) else 'X'}] ({t1:3d} tok) {a1[:70]}")
print(score)
```

```text
Q: When is my dentist appointment?
  no-RAG [X] ( 67 tok) I'm sorry, but I don't have access to your personal information or cur
  RAG    [X] (192 tok) Your dentist appointment is on October 20, 2026 at 10 AM.
Q: When is my sister's birthday?
  no-RAG [X] ( 68 tok) I'm sorry, but I don't have access to your sister's personal informati
  RAG    [O] (154 tok) My sister Hana's birthday is November 14.
Q: Which seat do I prefer on flights?
  no-RAG [X] ( 69 tok) I don't have personal preferences for seats on flights as I am a machi
  RAG    [O] (164 tok) I prefer an aisle seat on flights.
Q: What's my gym locker number?
  no-RAG [X] ( 68 tok) I'm sorry, but I don't have access to your personal information or mem
  RAG    [O] (172 tok) My gym locker number is 217.
Q: What book did Priya recommend?
  no-RAG [X] ( 68 tok) I'm sorry, but I don't have any information about a specific book that
  RAG    [O] (160 tok) Priya recommended "The Overstory" by Richard Powers.
Q: Where did I park at the airport?
  no-RAG [X] ( 69 tok) I'm sorry, but I don't have access to your current location or any inf
  RAG    [O] (183 tok) I parked my car at Lot C, row 14, on October 1, 2026.
Q: Who is my manager?
  no-RAG [X] ( 66 tok) I'm Robin's private on-device assistant and do not have access to pers
  RAG    [O] (156 tok) My manager is Teresa Wong.
Q: How do I take my coffee?
  no-RAG [X] ( 68 tok) You can use your phone's camera to brew a cup of coffee!
  RAG    [O] (154 tok) You take your coffee as an oat milk flat white, no sugar.
Q: When does my passport expire?
  no-RAG [X] ( 67 tok) As of 2026-10-03, your passport will expire on October 31, 2026.
  RAG    [O] (173 tok) My passport expires on February 19, 2027.
Q: What am I allergic to?
  no-RAG [X] ( 67 tok) I'm not sure what specific allergen you're referring to for an accurat
  RAG    [O] (147 tok) I am allergic to penicillin.
{'no-RAG': 0, 'RAG k=3': 9}
```

출력에서 볼 것:

- **no-RAG 0/10, RAG 9/10.** 0.5B 모델 자체는 Robin에 대해 아무것도 모른다. 기억 3개(프롬프트 +80~120 토큰)를 붙이는 것만으로 개인 비서가 된다.
- no-RAG의 여권 답이 무섭다: "your passport will expire on October 31, 2026" — **그럴듯한 날짜를 지어냈다** (hallucination). 대부분은 "모른다"고 했지만, 한 번이라도 지어내면 사용자는 여권 갱신을 놓칠 수 있다. 개인 사실은 RAG 없이 모델에게 묻지 않는다.
- 참고로 temperature 0이어도 출력이 비트 단위로 항상 같지는 않다. 처음에 prompt cache를 켠 채(`cache_prompt` 기본값) 돌렸을 때는 커피 no-RAG 답이 "You can use your phone to brew your coffee by tapping on the app…"로 달랐다. KV-cache 재사용 여부에 따라 GPU 연산 순서가 바뀌고, 부동소수 합의 순서가 바뀌면 거의 동점인 토큰의 argmax가 뒤집힐 수 있다. RAG 답 10개와 정답 수는 두 설정에서 같았다. 회귀 테스트는 "문자열 완전 일치"보다 checker 기반으로 짜는 이유다.
- RAG의 실패 하나가 §2에서 본 바로 그 문제다. 검색 1등이 엄마의 치과 예약(10/20 10 AM)이었고, 0.5B 모델은 맨 위 기억을 그대로 읽었다. 정답(m000)도 2등으로 프롬프트 안에 있었는데 모델이 "my"와 "Mom's"를 구분하지 못했다. **작은 모델일수록 검색 순서와 프롬프트에 들어간 잡음에 민감하다.** 그러니 검색 단계에서 잡음을 덜어 내야 한다.

### 7.4 코드로 확인 — RAG + 메타데이터 필터

`make_filter`는 두 가지 규칙을 건다. ① 질문에 그 사람을 가리키는 말(sister, mom…)이 없으면 다른 사람에 관한 기억(`about != self`)은 뺀다. ② 질문에 appointment/next/upcoming이 있으면 지난 event는 뺀다. `about`은 기억을 **쓸 때** 붙여 둔 메타데이터다 (§9의 추출 단계가 채운다).

```python
import numpy as np
from rag import QA, ask, retrieve, ok, make_filter, LAST
print("RAG + metadata filter (k=3):")
n = 0
for q, keys in QA:
    a, t = ask(q, retrieve(q, k=3, keep=make_filter(q)))
    n += ok(a, keys)
    if q.startswith("When is my"): print(f"  [{'O' if ok(a, keys) else 'X'}] ({t} tok) {q} -> {a}")
print(f"  correct: {n}/10\n")
print("context budget sweep (plain RAG):")
for k in (1, 3, 5, 10, 20):
    res = [(ok(a, keys), t, LAST["prompt_ms"]) for q, keys in QA for a, t in [ask(q, retrieve(q, k=k))]]
    acc = sum(r[0] for r in res); toks = np.mean([r[1] for r in res]); ms = np.mean([r[2] for r in res])
    print(f"  k={k:2d}: correct {acc}/10, mean prompt tokens {toks:4.0f}, prefill {ms:5.1f} ms (M2 Metal)")
```

```text
RAG + metadata filter (k=3):
  [O] (173 tok) When is my dentist appointment? -> The dentist appointment with Dr. Alvarez scheduled for Thursday, October 8, 2026 at 3:30 PM.
  [O] (164 tok) When is my sister's birthday? -> My sister Hana's birthday is November 14, 2026.
  correct: 10/10

context budget sweep (plain RAG):
  k= 1: correct 9/10, mean prompt tokens  102, prefill 205.6 ms (M2 Metal)
  k= 3: correct 9/10, mean prompt tokens  166, prefill 276.6 ms (M2 Metal)
  k= 5: correct 9/10, mean prompt tokens  227, prefill 385.6 ms (M2 Metal)
  k=10: correct 9/10, mean prompt tokens  386, prefill 614.5 ms (M2 Metal)
  k=20: correct 9/10, mean prompt tokens  697, prefill 1175.6 ms (M2 Metal)
```

출력에서 볼 것:

- 필터 하나로 **10/10**. 치과 질문에서 엄마 예약이 빠지자 모델이 정확히 "October 8, 3:30 PM"을 말했다. 여동생 질문에서는 "sister"가 질문에 있으므로 Hana에 관한 기억이 그대로 남아 정답이 유지되었다. 모델을 키우지 않고 **검색 쪽 메타데이터로** 고친 것이다.
- 다만 여동생 답에 "November 14, **2026**"이라고 원래 메모에 없는 연도를 붙였다. 작은 모델의 미묘한 덧붙임이다. 채점 기준(날짜 문자열)으로는 정답이지만, 실제 제품이라면 "기억에 없는 내용을 덧붙이지 않았나"도 검사해야 한다 (faithfulness).
- 이 필터는 손으로 짠 키워드 규칙이라 취약하다. 실제로는 질문 의도 분류(작은 분류기 또는 LLM의 구조화 출력)로 `about`, `time_range`를 뽑아 SQL 조건으로 바꾸는 방식이 흔하다. 원리는 같다 — **"누구의, 언제의"는 벡터가 아니라 메타데이터로 푼다.**
- k를 1에서 20으로 늘려도 정답 수는 9/10 그대로인데 prompt 토큰은 102 → 697로 7배, prefill 시간도 약 6배다. 다음 절의 주제다.

---

## 8. Context 예산 — 기억이 context window를 잡아먹지 않게

### 8.1 숫자로 보기

```svg
<svg viewBox="0 0 640 300" xmlns="http://www.w3.org/2000/svg">
<text x="320" y="22" text-anchor="middle" font-size="14">top-k 메모리 수 → prompt 토큰 (정답 수는 모두 9/10)</text><line x1="80" y1="250" x2="600" y2="250" stroke="currentColor"/><rect x="100" y="222.3" width="60" height="27.7" fill="#4a7bd0"/><text x="130" y="200.3" text-anchor="middle" font-size="12">102 tok</text><text x="130" y="215.3" text-anchor="middle" font-size="12">206 ms</text><text x="130" y="268" text-anchor="middle" font-size="13">k=1</text><rect x="202" y="204.9" width="60" height="45.1" fill="#4a7bd0"/><text x="232" y="182.9" text-anchor="middle" font-size="12">166 tok</text>
<text x="232" y="197.9" text-anchor="middle" font-size="12">277 ms</text><text x="232" y="268" text-anchor="middle" font-size="13">k=3</text><rect x="304" y="188.4" width="60" height="61.6" fill="#4a7bd0"/><text x="334" y="166.4" text-anchor="middle" font-size="12">227 tok</text><text x="334" y="181.4" text-anchor="middle" font-size="12">386 ms</text><text x="334" y="268" text-anchor="middle" font-size="13">k=5</text><rect x="406" y="145.2" width="60" height="104.8" fill="#4a7bd0"/><text x="436" y="123.2" text-anchor="middle" font-size="12">386 tok</text>
<text x="436" y="138.2" text-anchor="middle" font-size="12">614 ms</text><text x="436" y="268" text-anchor="middle" font-size="13">k=10</text><rect x="508" y="60.8" width="60" height="189.2" fill="#4a7bd0"/><text x="538" y="38.8" text-anchor="middle" font-size="12">697 tok</text><text x="538" y="53.8" text-anchor="middle" font-size="12">1176 ms</text><text x="538" y="268" text-anchor="middle" font-size="13">k=20</text><text x="320" y="290" text-anchor="middle" font-size="12">ms = llama-server prefill 시간 (부하 걸린 M2 — 비율만 볼 것)</text>
</svg>
```

그림 6 — top-k를 늘릴 때 프롬프트 토큰과 prefill 시간. 정답 수는 k=1부터 20까지 모두 9/10으로 같았다. 늘어난 토큰은 순수한 비용이다.

L2에서 본 것처럼 prefill 비용은 프롬프트 토큰 수에 (짧은 구간에서는 거의) 비례하고, KV-cache 메모리도 토큰 수에 비례한다. 기억 한 줄이 약 30 토큰(아래 실측)이니 k=20이면 600 토큰이다. 이것은 다음과 같은 비용으로 이어진다.

```
Qwen2.5-0.5B KV-cache (L2·D5 방식으로 계산):
  24 layers × 2 (K,V) × 2 KV heads × 64 head_dim × 2 B (fp16) = 12,288 B / token ≈ 12 KB
  600 토큰 → 약 7.4 MB 추가.   웨어러블 RAM에서는 무시할 수 없는 양이다.
```

(Qwen2.5-0.5B 설정값: 24층, KV head 2개, head 차원 64 — 모델 config 기준. KV-cache 정밀도는 런타임 설정에 따라 다르다.)

### 8.2 코드로 확인 — 토큰 예산 안에서 고르기

아래 코드는 Qwen 토크나이저로 기억 한 줄의 실제 토큰 수를 세고, "점수 순으로 담되 예산을 넘으면 건너뛰고, 점수가 0.30 미만이면 멈춘다"는 greedy packing을 해 본다.

```python
import numpy as np
from transformers import AutoTokenizer
from memdata import all_memories
from emb import embed
tok = AutoTokenizer.from_pretrained("Qwen/Qwen2.5-0.5B-Instruct", local_files_only=True)
mems = all_memories(); M = np.load("M.npy")
lines = [f"- [{m[3]}] {m[1]}" for m in mems]
ntok = np.array([len(tok(l)["input_ids"]) for l in lines])
print(f"tokens per memory line: mean {ntok.mean():.1f}, min {ntok.min()}, max {ntok.max()}")
def pack(q, budget, min_score=0.30):
    s = M @ embed([q])[0]; used, chosen = 0, []
    for i in np.argsort(-s):
        if s[i] < min_score: break                    # relevance floor: stop on weak matches
        if used + ntok[i] > budget: continue          # skip items that don't fit
        chosen.append(mems[i][0]); used += ntok[i]
    return chosen, used
for q in ["When is my dentist appointment?", "What's my gym locker number?", "Tell me about my family."]:
    for budget in (64, 128):
        c, u = pack(q, budget)
        print(f"{q[:32]:32s} budget={budget:3d} -> {len(c)} memories, {u} tokens {c}")
```

```text
tokens per memory line: mean 29.8, min 20, max 41
When is my dentist appointment?  budget= 64 -> 1 memories, 41 tokens ['m002']
When is my dentist appointment?  budget=128 -> 3 memories, 120 tokens ['m002', 'm000', 'm001']
What's my gym locker number?     budget= 64 -> 2 memories, 62 tokens ['m006', 'f002']
What's my gym locker number?     budget=128 -> 3 memories, 99 tokens ['m006', 'f002', 'm009']
Tell me about my family.         budget= 64 -> 0 memories, 0 tokens []
Tell me about my family.         budget=128 -> 0 memories, 0 tokens []
```

출력에서 볼 것:

- 기억 한 줄은 평균 30 토큰. "k개"보다 "토큰 예산"으로 생각하는 게 정확하다. 긴 메모 하나가 짧은 메모 둘을 밀어낼 수 있다.
- **예산 64에서 치과 질문은 엄마의 예약(m002, 41 토큰) 하나만 들어갔다.** 정답 m000은 2등인데 남은 23 토큰에 안 들어가서 빠졌다. 예산을 줄이면 §7의 오답이 그대로 재현된다. 예산 압박이 있을수록 **필터와 순위의 질**이 더 중요해진다.
- 사물함 질문에서는 관련 없는 f002(달리기 기록)가 0.30을 넘어 들어갔다. 관련성 하한(relevance floor)을 0.30보다 올리면 잡음이 줄지만, 말을 많이 바꾼 질문은 정답 점수 자체가 낮다. §2의 질문 30개에서 정답 점수는 0.338("Which herb do I hate?" → 고수 메모)부터 0.876까지 퍼져 있었다. 하한을 0.35로 올리면 고수 질문은 답을 잃는다. 임계값은 자기 데이터로 정한다.
- **"Tell me about my family."는 0개다.** 가족 정보는 "sister Hana", "brother Jun", "Mom's dentist"처럼 흩어져 있고 어느 것도 "family"와 0.30 이상 가깝지 않다. 이런 **집계형 질문**은 기억 하나하나를 찾는 RAG로 풀기 어렵다. 그래서 실제 시스템은 "가족: 여동생 Hana(11/14 생일), 남동생 Jun(4/2), 엄마" 같은 **요약 프로필**(의미 기억)을 미리 만들어 두고 함께 검색한다 (§9.6). 그 요약은 원본에서 파생된 데이터라 삭제 때 같이 처리해야 한다 (§10).

### 8.3 예산을 지키는 방법들

| 방법 | 내용 | 대가 |
|---|---|---|
| 토큰 예산 + 관련성 하한 | 위 코드 | 하한 튜닝 필요 |
| 메타데이터 pre-filter | §6, §7.4 | 질문 의도 파악 필요 |
| 기억을 짧게 저장 | 원문 대화 대신 한 문장 사실 (§9) | 추출 품질에 의존 |
| 요약·프로필 | 관련 기억 여러 개를 한 줄로 | 요약 갱신·삭제 연쇄 관리 |
| 중복 제거 (MMR 등) | 거의 같은 기억 여러 개가 예산을 나눠 먹지 않게 | 계산 조금 |
| 고정 프롬프트 부분 캐싱 | system prompt KV-cache 재사용 (L6) | 메모리 상주 |

---

## 9. 쓰기 경로 — 무엇을, 어떻게 기억하나

### 9.1 큰 그림

읽기 경로(RAG)보다 어려운 것이 쓰기 경로다. 무엇을 저장할지, 이미 아는 것인지, 얼마나 중요한지, 언제 잊을지를 정해야 한다.

```svg
<svg viewBox="0 0 680 250" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="l5a2" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="10" y="60" width="110" height="60" fill="#888" opacity="0.15" stroke="#888"/><text x="65" y="86" text-anchor="middle" font-size="13">대화 / 알림</text><text x="65" y="104" text-anchor="middle" font-size="12">(ASR 텍스트)</text><rect x="145" y="60" width="120" height="60" fill="#4a7bd0" opacity="0.15" stroke="#4a7bd0"/><text x="205" y="86" text-anchor="middle" font-size="13">① 추출</text>
<text x="205" y="104" text-anchor="middle" font-size="12">LLM → JSON 사실</text><rect x="290" y="60" width="120" height="60" fill="#e08a3c" opacity="0.15" stroke="#e08a3c"/><text x="350" y="86" text-anchor="middle" font-size="13">② 중복 확인</text><text x="350" y="104" text-anchor="middle" font-size="12">cos 최근접 + 규칙</text><rect x="435" y="60" width="110" height="60" fill="#3f9a6b" opacity="0.15" stroke="#3f9a6b"/><text x="490" y="86" text-anchor="middle" font-size="13">③ 점수·TTL</text><text x="490" y="104" text-anchor="middle" font-size="12">중요도, 만료일</text>
<rect x="570" y="60" width="100" height="60" fill="#888" opacity="0.15" stroke="#888"/><text x="620" y="86" text-anchor="middle" font-size="13">④ 저장</text><text x="620" y="104" text-anchor="middle" font-size="12">SQLite 행+벡터</text><line x1="120" y1="90" x2="143" y2="90" stroke="currentColor" marker-end="url(#l5a2)"/><line x1="265" y1="90" x2="288" y2="90" stroke="currentColor" marker-end="url(#l5a2)"/><line x1="410" y1="90" x2="433" y2="90" stroke="currentColor" marker-end="url(#l5a2)"/><line x1="545" y1="90" x2="568" y2="90" stroke="currentColor" marker-end="url(#l5a2)"/>
<line x1="205" y1="120" x2="205" y2="168" stroke="currentColor" marker-end="url(#l5a2)"/><text x="205" y="186" text-anchor="middle" font-size="12">저장할 것 없음 → 버림</text><line x1="350" y1="120" x2="350" y2="168" stroke="currentColor" marker-end="url(#l5a2)"/><text x="350" y="186" text-anchor="middle" font-size="12">MERGE / 갱신(superseded)</text><text x="350" y="204" text-anchor="middle" font-size="12">/ 사용자 확인</text><line x1="620" y1="120" x2="620" y2="168" stroke="currentColor" marker-end="url(#l5a2)"/><text x="620" y="186" text-anchor="middle" font-size="12">사용자: 보기·수정·삭제</text>
<text x="620" y="204" text-anchor="middle" font-size="12">(§9.6, §10)</text><text x="340" y="36" text-anchor="middle" font-size="14">메모리 쓰기 경로 — 충전 중 배치 처리로 미뤄도 된다</text>
</svg>
```

그림 7 — 쓰기 경로. 각 단계에서 "버림"이 기본값이다. 저장하지 않은 데이터는 유출될 수도, 지울 필요도 없다 (H6 §1 데이터 최소화).

쓰기 경로는 실시간일 필요가 없다. 대화가 끝난 뒤, 혹은 기기가 충전 중일 때 배치로 처리해도 된다 (예를 들어 Hark 같은 기기라면 전력 예산상 이 편이 자연스럽다 — 추정). 사용자가 "이거 기억해 둬"라고 명시할 때만 즉시 저장한다.

### 9.2 ① 추출 — LLM에게 JSON으로 뽑게 하기

대화 원문을 그대로 저장하지 않고 "기억할 만한 사실" 한두 문장으로 바꾼다. 이때 LLM 출력을 **JSON schema로 강제**하면 파싱이 실패하지 않는다. llama-server는 `response_format`에 JSON schema를 주면 내부적으로 문법(grammar)으로 바꿔서, 디코딩 중 schema에 맞지 않는 토큰을 아예 고를 수 없게 막는다 (constrained decoding).

```python
import json, urllib.request
SCHEMA = {"type": "object", "required": ["memories"], "properties": {"memories": {
    "type": "array", "maxItems": 3, "items": {"type": "object",
    "required": ["text", "type", "date"], "properties": {
        "text": {"type": "string"},
        "type": {"enum": ["event", "fact", "preference"]},
        "date": {"type": "string", "pattern": "^(none|2026-[0-9]{2}-[0-9]{2})$"}}}}}}
SYS = ("Today is 2026-10-03 (Saturday). Extract facts about the user worth remembering long-term "
       "from the utterance. Use date YYYY-MM-DD or 'none'. Return an empty list if nothing is worth storing.\n"
       'Example: "My yoga class moved to Mondays at 6." -> {"memories": [{"text": "User\'s yoga class is on Mondays at 6 PM.", "type": "fact", "date": "none"}]}\n'
       'Example: "What is the weather?" -> {"memories": []}')
def extract(utt):
    body = {"messages": [{"role": "system", "content": SYS}, {"role": "user", "content": utt}],
            "temperature": 0, "max_tokens": 160, "seed": 0,
            "response_format": {"type": "json_schema", "json_schema": {"schema": SCHEMA}}}
    req = urllib.request.Request("http://localhost:8095/v1/chat/completions",
                                 json.dumps(body).encode(), {"Content-Type": "application/json"})
    return json.loads(json.load(urllib.request.urlopen(req))["choices"][0]["message"]["content"])
for utt in ["The plumber is coming Tuesday, October 6, between 9 and 11 to replace the water heater.",
            "Starting in November my new manager will be Luis Ortega.",
            "I really can't stand horror movies, never suggest them.",
            "Set a timer for ten minutes."]:
    print(utt, "\n  ->", json.dumps(extract(utt)["memories"]))
```

```text
The plumber is coming Tuesday, October 6, between 9 and 11 to replace the water heater. 
  -> [{"text": "The plumber will be replacing the water heater on Tuesday, October 6.", "type": "fact", "date": "2026-10-06"}, {"text": "The plumber is coming between 9 and 11 PM.", "type": "fact", "date": "none"}]
Starting in November my new manager will be Luis Ortega. 
  -> [{"text": "New manager's name is Luis Ortega.", "type": "fact", "date": "2026-11-01"}]
I really can't stand horror movies, never suggest them. 
  -> [{"text": "User doesn't like horror movies and avoids suggesting them.", "type": "fact", "date": "2026-10-03"}]
Set a timer for ten minutes. 
  -> [{"text": "Set a timer for 10 minutes.", "type": "fact", "date": "2026-10-03"}]
```

출력에서 볼 것:

- **형식은 100% 지켜졌다.** 4개 모두 `json.loads`가 성공했고, `type`은 enum 안, `date`는 패턴 안이다. 이것이 grammar 제약의 역할이다. 펌웨어로 치면 레지스터 필드에 유효한 값만 쓰도록 하드웨어가 막아 주는 것과 같다.
- **내용은 틀릴 수 있다.** 배관공 시간에 원문에 없는 "PM"을 붙였고(오전일 가능성이 높다), 공포 영화 비선호를 `preference`가 아니라 `fact`로 분류했고, 타이머 명령처럼 **저장할 필요가 없는 것**을 저장했다. grammar는 "모양"을 보장할 뿐 "의미"를 보장하지 않는다.
- 이 버전은 system prompt에 예시 두 개(few-shot)를 넣은 것이다. 예시 없이 돌렸을 때(같은 schema)는 `{"text": "2026-10-03", ...}`처럼 날짜를 text에 넣는 등 훨씬 나빴다. 0.5B로 추출하려면 예시·규칙을 많이 줘야 하고, 그래도 틀린다.
- 실전 대책: 명령형 발화(타이머·날씨)는 추출 전에 의도 분류로 거른다 · 숫자·시간은 원문에 있는 문자열인지 검사한다(원문에 "PM"이 없으면 거부) · 애매하면 사용자에게 "배관공 10/6 오전 9–11시, 기억할까요?"라고 확인 · 정확도가 중요하면 추출만 충전 중에 더 큰 모델로 돌린다 (L3 hybrid 라우팅 — 단, 클라우드로 보내면 프라이버시 정책이 달라진다).

### 9.3 ② 중복 확인 — 코사인 임계값의 함정

같은 사실이 여러 번 들어오면(사용자가 사물함 번호를 세 번 말함) 기억이 중복되어 context 예산을 나눠 먹는다. 가장 쉬운 방법은 "새 기억과 가장 가까운 기존 기억의 코사인이 임계값 이상이면 합친다"이다. 그런데 정말 그래도 되나?

```python
import numpy as np
from memdata import all_memories
from emb import embed
mems = all_memories(); M = np.load("M.npy")
incoming = ["My locker at the gym is number 217.",
            "Hana's birthday is on Nov 14.",
            "I take my coffee as an oat milk flat white without sugar.",
            "Parked the car at the airport in Lot D, row 2, on October 2, 2026.",
            "I switched to green tea in the mornings instead of coffee.",
            "Bought a new tent for the camping trip."]
E = embed(incoming)
S = E @ M.T
for i, t in enumerate(incoming):
    j = int(np.argmax(S[i]))
    v = S[i, j]
    act = "MERGE (dup)" if v >= 0.90 else "REVIEW (related)" if v >= 0.75 else "INSERT (new)"
    print(f"{v:.3f} {act:16s} {t[:48]:48s} ~ {mems[j][0]}")
```

```text
0.883 REVIEW (related) My locker at the gym is number 217.              ~ m006
0.900 REVIEW (related) Hana's birthday is on Nov 14.                    ~ m003
0.986 MERGE (dup)      I take my coffee as an oat milk flat white witho ~ m011
0.932 MERGE (dup)      Parked the car at the airport in Lot D, row 2, o ~ m008
0.655 INSERT (new)     I switched to green tea in the mornings instead  ~ m012
0.405 INSERT (new)     Bought a new tent for the camping trip.          ~ m022
```

출력에서 볼 것 — 이 표가 이 절의 핵심이다:

- **진짜 중복인데 임계값 아래**: 사물함 217(0.883)은 m006과 같은 사실인데 0.90을 못 넘었다. 생일(0.900)은 반올림 경계에 걸렸다 (`0.900`으로 찍혔지만 실제 값이 0.90보다 아주 조금 작아서 REVIEW로 갔다).
- **다른 사실인데 임계값 위 — 가장 위험한 경우**: 새 주차 위치 "Lot D, row 2, 10/2"가 기존 "Lot C, row 14, 10/1"과 0.932로 MERGE 판정을 받았다. 이대로 합치면 새 정보를 버리거나, 옛 정보를 새 정보로 착각한다. 임베딩은 "같은 **주제**"를 보지 "같은 **값**"을 보지 않는다. 숫자·날짜·이름이 다른 문장은 코사인으로 구분이 안 된다.
- 녹차로 바꿨다는 말(0.655)은 커피 선호를 **갱신**하는 정보인데 "새 기억"으로 들어간다. 그러면 저장소에 "oat milk flat white를 마신다"와 "녹차로 바꿨다"가 동시에 산다. 이건 중복 제거가 아니라 **충돌 해결(conflict resolution)** 문제다.

그래서 실전 규칙은 이렇게 된다.

1. 코사인은 **후보 찾기**에만 쓴다 (예: 0.75 이상이면 "관련 있음").
2. 후보와 새 기억의 숫자·날짜·고유명사를 비교한다. 같으면 MERGE(타임스탬프만 갱신), 다르면 같은 슬롯의 **새 값**으로 보고 옛 것에 `superseded_by`를 단다 (주차 위치처럼 "최신 하나만 의미 있는" 슬롯).
3. 판단이 애매하면 LLM에게 "같은 사실인가, 갱신인가, 별개인가"를 구조화 출력으로 묻거나 사용자에게 확인한다.

### 9.4 ③ 중요도와 최신성 — 점수에 시간을 섞기

검색 점수를 코사인만으로 매기면 **옛날 선호**가 이길 수 있다. Park et al.(2023, "Generative Agents")은 에이전트 기억 검색 점수를 relevance(관련성) + recency(최신성, 지수 감쇠) + importance(중요도)의 가중합으로 매겼다. 같은 아이디어를 Robin에게 적용한다.

```
score = cos(q, m) + a · importance(m) + b · recency(m)
recency(m) = exp(−age_days / τ)            τ = 90일 → 반감기 τ·ln2 ≈ 62일
```

말로 하면: 관련성에, 중요한 종류(사실 > 이벤트 > 잡담)와 최근 것에 가산점을 준다. 손계산: m011(커피 선호, 44일 전) → exp(−44/90) = 0.613. m012(옛 커피 습관, 519일 전) → exp(−519/90) = 0.003.

```python
import numpy as np
from datetime import date
from memdata import all_memories, NOW
from emb import embed
mems = all_memories(); M = np.load("M.npy"); today = date.fromisoformat(NOW)
age = np.array([(today - date.fromisoformat(m[3])).days for m in mems])   # negative = future
IMP = {"fact": 0.8, "preference": 0.7, "event": 0.5, "conversation": 0.4}  # importance prior
imp = np.array([IMP[m[2]] for m in mems])
rec = np.exp(-np.maximum(age, 0) / 90.0)               # recency: half-life ~ 62 days
def rank(q, a=0.0, b=0.0, k=2):
    s = M @ embed([q])[0] + a * imp + b * rec
    return [(mems[i][0], round(float(s[i]), 3), mems[i][3]) for i in np.argsort(-s)[:k]]
q = "What do I drink every morning?"
for b in (0.0, 0.1, 0.3):
    print(f"cos + {b}·recency:", rank(q, b=b))
# TTL: past events / conversations older than 180 days are forgotten unless importance >= 0.8
ttl_days = {"event": 180, "conversation": 180}
expired = [m[0] for m, a, i in zip(mems, age, imp) if m[2] in ttl_days and a > ttl_days[m[2]] and i < 0.8]
print(f"expired by TTL: {len(expired)} of {len(mems)} ->", expired[:6], "...")
```

```text
cos + 0.0·recency: [('m012', 0.573, '2025-05-02'), ('m011', 0.454, '2026-08-20')]
cos + 0.1·recency: [('m012', 0.574, '2025-05-02'), ('m011', 0.515, '2026-08-20')]
cos + 0.3·recency: [('m011', 0.638, '2026-08-20'), ('m012', 0.574, '2025-05-02')]
expired by TTL: 58 of 200 -> ['m001', 'f005', 'f007', 'f010', 'f015', 'f016'] ...
```

출력에서 볼 것:

- "What do I drink every morning?"은 단어가 겹치는 옛 메모 "I used to drink black coffee every morning"(m012, 2025년)을 0.573으로 1등에 올린다. 지금 선호인 m011은 0.454.
- recency 가중치 0.1로는 부족하고(0.515), 0.3에서 뒤집힌다(0.454 + 0.3 × 0.613 = 0.638). 가중치는 **튜닝 대상**이고, 너무 키우면 "작년 생일 선물" 같은 오래되었지만 정답인 기억이 밀린다.
- 더 근본적인 해법은 쓰기 경로에서 m012에 `superseded_by = m011`을 달아 검색 후보에서 빼는 것이다 (§9.3 규칙 2). **점수 튜닝으로 버티지 말고 데이터 모델로 푼다.**
- TTL(180일, 중요도 0.8 미만) 하나로 200개 중 58개가 만료된다. 지난 치과 기록 m001도 포함된다 — 이건 §6에서 "다음 치과" 질문을 헷갈리게 하던 기억이기도 하다. 잊는 것은 프라이버시뿐 아니라 **검색 품질**에도 좋다.

### 9.5 ④ 망각 — TTL과 등급별 정책 (H6 연결)

H6 §9.1의 "데이터 클래스별 TTL"을 기억에 적용하면 이렇다 (예시 정책 — 실제 값은 제품·법무가 정한다).

| 기억 종류 | 예 | 기본 TTL (예시) | 비고 |
|---|---|---|---|
| 지난 일정 (event, 과거) | 3월 치과 청소 | 180일 | 반복 패턴은 요약으로 승격 후 삭제 |
| 잡담 (conversation) | "Lena가 Denver로 이사 간대" | 90–180일 | 사용자가 고정(pin)하면 유지 |
| 사실·선호 (fact, preference) | aisle석 선호 | 무기한, 갱신 시 옛 것 삭제 | superseded 된 것은 짧은 TTL |
| 건강·금융 (health, sensitive) | 혈압 128/82 | 짧게 + 명시적 동의 | 출력 채널 제한 |
| 원본 오디오 | 대화 녹음 | 저장 안 함 (텍스트 추출 후 즉시 폐기) | H6 §5 최소화 |

### 9.6 사용자가 보는 기억 — 보기·고치기·지우기

"기억하는 기기"가 신뢰를 얻으려면 사용자가 **무엇을 기억하는지 볼 수 있어야** 한다. 최소 기능:

- 기억 목록 (종류·날짜·출처별), 검색
- 개별 수정·삭제, "이 대화에서 나온 것 전부 삭제", "지난 1시간 전부 삭제"
- "이건 기억하지 마" 명령 (그 주제를 앞으로 추출하지 않는 차단 목록)
- 답변에 근거 표시 ("10/1 메모를 보고 답했어요")
- 전체 기억 끄기 / 전체 삭제

사용자 화면에 보이는 것이 "원문 한 줄"이라는 점이 중요하다. 그러려면 저장 단위가 사람이 읽을 수 있는 짧은 문장이어야 한다 (§9.2). 벡터만 저장하면 사용자는 무엇이 저장되었는지 알 수 없다.

---

## 10. 프라이버시와 보안

H6에서 다룬 원칙(최소화, 동의, 암호화, 보관·삭제)을 기억 저장소에 적용한다. 개인 기억은 사용자에 관한 **가장 민감한 데이터의 농축액**이다 — 일정, 건강, 가족, 위치가 한 파일에 정리되어 있다.

### 10.1 기본값: 기기 안에서만

- 기억 저장소, 임베딩 계산, 검색은 **전부 기기에서** 한다. §11에서 보듯 비용이 작아서 클라우드로 보낼 이유가 없다.
- LLM 생성을 클라우드로 보내는 hybrid 구성(L3)이라면, **검색된 기억 k개가 프롬프트에 실려 나간다**는 것을 잊으면 안 된다. 기억 자체는 기기에 있어도 프롬프트를 통해 새어 나간다. `privacy` 등급별로 "클라우드 프롬프트에 넣지 않음" 규칙을 걸고, 그 경우는 온디바이스 모델로만 답한다.

### 10.2 저장 시 암호화 (encryption at rest)

- 폰 OS는 파일 단위 암호화(file-based encryption)와 하드웨어 키 저장소(보안 영역·TEE)를 제공한다. 기억 DB 파일은 "기기 잠금 해제 후에만 읽힘" 같은 가장 강한 보호 등급에 두는 것이 기본이다 (OS마다 이름과 세부가 다르다 — 플랫폼 문서 확인).
- DB 자체 암호화가 필요하면 SQLite 페이지 단위 암호화 확장(예: SQLCipher)을 쓴다.
- 웨어러블 본체에 저장한다면 Don이 SSD에서 다뤘던 것과 같은 구조다: 키는 보안 요소/OTP에서 파생, 데이터는 AES로 암호화, **키를 지우면 데이터 전체가 즉시 무의미해지는 crypto-erase** (H6 §8.2 crypto-shredding).

### 10.3 제대로 지우기 — 원본, 임베딩, 파생 데이터, 그리고 파일 속 잔해

```svg
<svg viewBox="0 0 660 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="l5a3" markerWidth="8" markerHeight="8" refX="7" refY="4" orient="auto"><path d="M0,0 L8,4 L0,8 z" fill="currentColor"/></marker></defs><rect x="250" y="20" width="160" height="50" fill="#d0564a" opacity="0.15" stroke="#d0564a"/><text x="330" y="42" text-anchor="middle" font-size="13">기억 m014 삭제 요청</text><text x="330" y="60" text-anchor="middle" font-size="12">"페니실린 알레르기"</text><rect x="10" y="120" width="120" height="50" fill="#4a7bd0" opacity="0.15" stroke="#4a7bd0"/><text x="70" y="142" text-anchor="middle" font-size="13">원문 행</text>
<text x="70" y="160" text-anchor="middle" font-size="12">memory.text</text><rect x="140" y="120" width="120" height="50" fill="#4a7bd0" opacity="0.15" stroke="#4a7bd0"/><text x="200" y="142" text-anchor="middle" font-size="13">임베딩 벡터</text><text x="200" y="160" text-anchor="middle" font-size="12">BLOB, ANN 인덱스</text><rect x="270" y="120" width="120" height="50" fill="#e08a3c" opacity="0.15" stroke="#e08a3c"/><text x="330" y="142" text-anchor="middle" font-size="13">파생 요약</text><text x="330" y="160" text-anchor="middle" font-size="12">"건강 프로필"</text>
<rect x="400" y="120" width="120" height="50" fill="#e08a3c" opacity="0.15" stroke="#e08a3c"/><text x="460" y="142" text-anchor="middle" font-size="13">캐시</text><text x="460" y="160" text-anchor="middle" font-size="12">prompt/KV, 로그</text><rect x="530" y="120" width="120" height="50" fill="#888" opacity="0.15" stroke="#888"/><text x="590" y="142" text-anchor="middle" font-size="13">다른 기기·백업</text><text x="590" y="160" text-anchor="middle" font-size="12">sync 복제본</text><line x1="300" y1="70" x2="75" y2="118" stroke="currentColor" marker-end="url(#l5a3)"/>
<line x1="315" y1="70" x2="205" y2="118" stroke="currentColor" marker-end="url(#l5a3)"/><line x1="330" y1="70" x2="330" y2="118" stroke="currentColor" marker-end="url(#l5a3)"/><line x1="345" y1="70" x2="455" y2="118" stroke="currentColor" marker-end="url(#l5a3)"/><line x1="360" y1="70" x2="585" y2="118" stroke="currentColor" marker-end="url(#l5a3)"/><text x="135" y="212" text-anchor="middle" font-size="12">DELETE + secure_delete</text><text x="135" y="230" text-anchor="middle" font-size="12">또는 VACUUM (빈 페이지 잔해)</text>
<text x="330" y="212" text-anchor="middle" font-size="12">lineage로 찾아 삭제·재생성</text><text x="460" y="212" text-anchor="middle" font-size="12">비우기 / 재시작</text><text x="590" y="212" text-anchor="middle" font-size="12">tombstone 전파 (H6 §9.2)</text><text x="330" y="270" text-anchor="middle" font-size="13">한 칸이라도 빠지면 "지웠다"고 말할 수 없다</text>
</svg>
```

그림 8 — 기억 하나를 지울 때 함께 지워야 하는 것들. 원문만 지우면 벡터·요약·캐시·복제본에 정보가 남는다.

Don에게 이건 익숙한 문제다. NAND에서 덮어쓰기는 새 페이지에 쓰고 옛 페이지를 stale로 표시할 뿐이라, 논리적으로 지운 데이터가 물리적으로 남는다. 그래서 Sanitize·crypto erase가 있다. SQLite도 똑같다. `DELETE`는 행을 B-tree에서 빼고 페이지를 freelist에 돌려줄 뿐, 바이트를 0으로 덮지 않을 수 있다.

아래 코드는 기억 하나와 그 파생 요약을 넣고, 삭제한 뒤 **DB 파일 바이트에 원문과 임베딩이 남는지** 직접 grep 한다. 요약 테이블은 `ON DELETE CASCADE` 외래 키로 원본에 묶어 둔다 (lineage).

```python
import sqlite3, os, numpy as np
M = np.load("M.npy"); R = np.random.default_rng(0).standard_normal((200, 384)).astype(np.float32)
SECRET = "allergic to penicillin"; vec = M[14].tobytes()   # m014
def build(path, secure):
    if os.path.exists(path): os.remove(path)
    db = sqlite3.connect(path)
    db.execute(f"PRAGMA secure_delete = {'ON' if secure else 'OFF'}")
    db.execute("PRAGMA foreign_keys = ON")
    db.execute("CREATE TABLE memory(id TEXT PRIMARY KEY, text TEXT, vec BLOB)")
    db.execute("""CREATE TABLE summary(id TEXT PRIMARY KEY, text TEXT,
                  src TEXT REFERENCES memory(id) ON DELETE CASCADE)""")   # derived data
    for i in range(200):
        db.execute("INSERT INTO memory VALUES (?,?,?)", (f"x{i}", f"filler note {i}", R[i].tobytes()))
    db.execute("INSERT INTO memory VALUES ('m014', 'I am allergic to penicillin.', ?)", (vec,))
    db.execute("INSERT INTO summary VALUES ('s1', 'Health: Robin is allergic to penicillin', 'm014')")
    db.commit()
    db.execute("DELETE FROM memory WHERE id = 'm014'"); db.commit()   # cascade removes s1 too
    left = db.execute("SELECT count(*) FROM summary").fetchone()[0]
    db.close(); return left
def residue(path):
    raw = open(path, "rb").read()
    return raw.count(SECRET.encode()), raw.count(vec)
for secure in (False, True):
    left = build("del.db", secure)
    print(f"secure_delete={'ON ' if secure else 'OFF'}: summary rows left={left}, "
          f"file residue (text, embedding) = {residue('del.db')}")
build("del.db", False); db = sqlite3.connect("del.db"); db.execute("VACUUM"); db.close()
print("secure_delete=OFF + VACUUM: file residue =", residue("del.db"))
```

```text
secure_delete=OFF: summary rows left=0, file residue (text, embedding) = (2, 1)
secure_delete=ON : summary rows left=0, file residue (text, embedding) = (0, 0)
secure_delete=OFF + VACUUM: file residue = (0, 0)
```

출력에서 볼 것:

- `ON DELETE CASCADE` 덕분에 파생 요약 행도 논리적으로 지워졌다 (summary rows left=0). 외래 키는 SQLite에서 `PRAGMA foreign_keys = ON`을 **연결마다** 켜야 동작한다 — 잊으면 조용히 무시된다.
- 그런데 **secure_delete OFF에서는 파일 안에 "allergic to penicillin"이 2번(원문 + 요약), 임베딩 바이트 1,536 B가 그대로 남았다.** 파일을 복사해 간 사람(백업, 포렌식, 탈취된 기기)은 지운 기억을 읽을 수 있다. NAND의 stale page와 똑같다.
- `PRAGMA secure_delete = ON`이면 삭제 시 내용을 0으로 덮어서 잔해가 0이다. 또는 삭제 후 `VACUUM`으로 DB를 새로 써도 사라진다 (단, VACUUM은 전체를 다시 쓰므로 flash 마모·시간 비용이 있다).
- 참고: 이 Python의 SQLite 기본값은 `PRAGMA secure_delete` 조회 시 `2`(FAST 모드)였다. 위 코드는 명시적으로 OFF/ON을 줬다. 빌드마다 기본값이 다르니 **명시적으로 설정**한다.
- 그리고 이것은 SQLite 파일 수준 이야기일 뿐이다. 그 아래 **flash 자체**(FTL)에도 옛 페이지가 남을 수 있다. 그래서 파일 덮어쓰기만으로는 물리적 삭제를 보장할 수 없고, 저장 시 암호화 + 키 폐기(crypto-erase)가 최종 방어선이다. Don이 SSD에서 Sanitize를 구현하면서 알던 내용 그대로다.
- WAL 모드(`journal_mode=WAL`)를 쓰면 `-wal` 파일에도 옛 내용이 남을 수 있다. 체크포인트 후 확인한다.

삭제 체크리스트 (기억 하나):

- [ ] 원문 행 삭제 (secure_delete 또는 VACUUM)
- [ ] 임베딩 벡터 삭제 — 별도 벡터 파일·ANN 인덱스가 있으면 거기서도 (HNSW tombstone 주의, §5.4)
- [ ] 파생 데이터: 요약·프로필·다른 기억의 merge 기록에서 제거 또는 재생성 (lineage 필요)
- [ ] 메모리 상의 캐시: prompt cache·KV-cache·최근 검색 결과 캐시
- [ ] 로그·크래시 덤프에 원문이 찍히지 않았는지 (H1 로깅 정책)
- [ ] 다른 기기·백업으로 tombstone 전파 (H6 §9.2)
- [ ] 학습 데이터로 수집된 적이 있다면 데이터셋에서 제외 (H5·H6)

### 10.4 여러 기기 간 동기화 — E2E 암호화 개념

폰·웨어러블·태블릿이 같은 기억을 공유하려면 동기화가 필요하다. 서버를 거치더라도 **서버는 암호문만 보게** 하는 것이 E2E(end-to-end) 암호화다.

```
기기 A ──(사용자 키로 암호화한 변경분)──► 서버 (암호문 저장·중계만, 키 없음) ──► 기기 B (복호화)
키는 기기끼리만 공유 (페어링 시 교환, 보안 영역에 보관)
삭제 = tombstone 레코드도 암호화해서 전파. 모든 기기가 받을 때까지 tombstone 유지
```

설계 포인트:

- **벡터를 동기화할까, 텍스트만 동기화하고 각 기기에서 다시 임베딩할까?** 텍스트만 보내면 전송량이 작고 기기마다 다른 임베딩 모델(버전)을 써도 된다. 대신 각 기기가 임베딩 계산을 해야 한다 (기억 하나 수 ms, §2.4). 벡터를 보내면 모든 기기가 같은 `embed_model` 버전이어야 한다.
- 충돌: 두 기기에서 같은 기억을 동시에 고치면? 타임스탬프 기반 last-writer-wins가 가장 단순하다.
- 키 분실 = 기억 전체 분실. 복구 키 설계가 필요하다.

### 10.5 임베딩은 익명이 아니다 — embedding inversion

"원문은 지우고 벡터만 남기면 괜찮지 않나?" — 그렇지 않다는 연구가 있다.

- Song & Raghunathan(2020, "Information Leakage in Embedding Models")은 문장 임베딩에서 원문 단어 일부나 속성(작성자 등)을 상당 부분 복원·추론할 수 있음을 보였다.
- Morris et al.(2023, "Text Embeddings Reveal (Almost) As Much As Text", Vec2Text)은 특정 임베딩 모델에 대해, 임베딩만으로 짧은 원문 문장을 높은 비율로 거의 그대로 복원하는 반복 교정 방법을 보고했다.

복원 성능은 모델·문장 길이·공격자가 가진 자원에 따라 크게 다르고, 이 노트에서 직접 재현하지는 않았다. 하지만 엔지니어링 결론은 분명하다. **임베딩은 원문과 같은 민감도로 취급한다** — 같이 암호화하고, 같이 지우고, 익명화된 데이터라고 클라우드로 보내지 않는다. §10.3에서 원문은 지우고 벡터만 남는 경우(residue = (0, 1)처럼)도 "지워지지 않은 것"으로 본다.

---

## 11. 자원 예산 — 웨어러블·폰에서 얼마나 드나

### 11.1 코드로 확인 — 저장 공간과 연산·메모리 이동량

```python
N, D, TEXT_B, META_B = 10_000, 384, 120, 40          # 10k memories, avg text 120 B, metadata 40 B
for name, vb in [("float32", 4 * D), ("float16", 2 * D), ("int8+scale", D + 4), ("binary", D // 8)]:
    vec = N * vb / 2**20; tot = N * (vb + TEXT_B + META_B) / 2**20
    print(f"{name:10s} {vb:5d} B/vec  vectors {vec:6.2f} MiB  with text+meta {tot:6.2f} MiB")
# compute per question (FLOPs ~ 2 x params x tokens for transformer forward, ignoring attention)
emb_q   = 2 * 22.7e6 * 12                          # MiniLM, ~12-token query
search  = 2 * N * D                                # brute-force dot products
llm_pre = 2 * 494e6 * 166                          # Qwen2.5-0.5B prefill, 166 tokens (k=3)
llm_dec = 2 * 494e6 * 30                           # 30 generated tokens
tot = emb_q + search + llm_pre + llm_dec
for n, f in [("query embedding", emb_q), ("vector search", search), ("LLM prefill", llm_pre), ("LLM decode", llm_dec)]:
    print(f"{n:16s} {f/1e9:8.3f} GFLOP  {100*f/tot:5.2f} %")
# DRAM traffic: weights must be streamed once per decode step (Q8_0 GGUF = 531 MB)
w = 531e6
for n, b in [("search int8 (10k)", N * (D + 4)), ("LLM prefill (1 pass)", w), ("LLM decode (30 steps)", 30 * w)]:
    print(f"{n:22s} {b/1e6:9.1f} MB moved")
```

```text
float32     1536 B/vec  vectors  14.65 MiB  with text+meta  16.17 MiB
float16      768 B/vec  vectors   7.32 MiB  with text+meta   8.85 MiB
int8+scale   388 B/vec  vectors   3.70 MiB  with text+meta   5.23 MiB
binary        48 B/vec  vectors   0.46 MiB  with text+meta   1.98 MiB
query embedding     0.545 GFLOP   0.28 %
vector search       0.008 GFLOP   0.00 %
LLM prefill       164.008 GFLOP  84.45 %
LLM decode         29.640 GFLOP  15.26 %
search int8 (10k)            3.9 MB moved
LLM prefill (1 pass)       531.0 MB moved
LLM decode (30 steps)    15930.0 MB moved
```

출력에서 볼 것:

- **저장**: 기억 1만 개를 int8로 두면 벡터 3.7 MiB, 텍스트·메타데이터 포함 약 5.2 MiB (SQLite 페이지 오버헤드는 별도, §6.2에서 행당 수백 B 수준). binary면 2 MiB. 폰에서는 무시할 수준이고, 웨어러블에서도 flash에 충분히 들어간다. 다만 **텍스트+메타데이터(1.5 MiB)가 binary 벡터(0.46 MiB)보다 크다** — 벡터를 극단적으로 줄여도 원문이 남는다.
- **연산**: 질문 하나 처리에서 기억 검색(임베딩 + 검색)은 전체 FLOP의 0.3%다. 99.7%는 LLM이다. 연산 대신 **메모리 이동량**으로 봐도 검색 3.9 MB vs LLM decode 16 GB로 4,000배 차이다. 에너지는 메모리 이동이 지배하므로(D7) **기억 검색의 배터리 영향은 LLM에 비해 무시할 수 있다**.
- 반대로 말하면, 기억이 배터리를 먹는 경로는 검색이 아니라 **기억이 늘린 prefill 토큰**이다 (§8: k=20이면 prefill 토큰 7배). 기억 시스템의 전력 최적화 = context 예산 관리다.
- FLOP 계산은 "2 × 파라미터 × 토큰" 근사로 attention 항을 뺐다(D1·D5). 짧은 context에서는 오차가 작다.

### 11.2 모바일 CPU 지연 추정 (라벨: 추정)

호스트 측정 → 기기 추정은 늘 조심해야 한다. 아래는 M2 실측에 대략적인 배율을 곱한 **추정치**다. 실제 기기에서 반드시 다시 잰다 (K1·K2).

| 단계 | M2 실측 | 폰 big core (추정, M2의 1–3배) | 웨어러블급 저전력 A-core (추정, M2의 5–15배) |
|---|---|---|---|
| 질문 임베딩 (MiniLM fp32, torch 4스레드) | 3.6 ms | ~5–15 ms (int8·NPU면 더 작게) | ~20–60 ms |
| int8 brute force 10k (C, 1스레드, §12) | 0.16 ms | ~0.2–0.5 ms | ~1–3 ms |
| binary brute force 10k (C, 1스레드) | 0.03 ms | ~0.05–0.1 ms | ~0.2–0.5 ms |
| SQLite 필터 조회 | (측정 생략, 인덱스 사용 시 수 ms 이하로 예상) | — | — |
| LLM prefill 166 토큰 (Metal, 부하 상태) | 277 ms | 기기·런타임 따라 수백 ms~수 초 | 이 급에서 0.5B LLM은 빠듯 — 폰이나 클라우드로 (L3) |

배율은 클럭·마이크로아키텍처·메모리 대역폭 차이를 뭉뚱그린 감이다 (E1). 결론만은 배율이 2배 틀려도 바뀌지 않는다: **검색은 수 ms 이하, LLM은 수백 ms 이상**. 개인 기억 규모에서 검색 최적화(ANN)는 우선순위가 낮다.

---

## 12. 임베디드 관점에서 다시 보기

### 12.1 C로 같은 검색 — int8 내적과 binary popcount

numpy에서 int8·binary가 느렸던 것(§3.5, §4.2)은 numpy 탓이다. 기기에서 실제로 돌 코드에 가까운 C로 다시 잰다. 아래 코드는 int8 벡터 1만 개에 대한 내적 최대값 검색과, 48 B binary 벡터 1만 개에 대한 Hamming 최소값 검색을 각각 100번 돌려 질문당 시간을 낸다 (데이터는 무작위, 단일 스레드).

```c
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define N 10000
#define D 384
#define W (D / 64)                       /* 6 x uint64 = 48 bytes per binary vector */
static int8_t   m8[N][D];
static uint64_t mb[N][W];
static double now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1e6 + t.tv_nsec / 1e3; }
int main(void) {
    srand(1);
    int8_t q8[D]; uint64_t qb[W] = {0};
    for (int i = 0; i < N; i++) for (int d = 0; d < D; d++) m8[i][d] = (int8_t)(rand() % 255 - 127);
    for (int i = 0; i < N; i++) for (int w = 0; w < W; w++) mb[i][w] = ((uint64_t)rand() << 33) ^ ((uint64_t)rand() << 2) ^ (uint64_t)rand();
    for (int d = 0; d < D; d++) { q8[d] = (int8_t)(rand() % 255 - 127); if (q8[d] > 0) qb[d / 64] |= 1ull << (d % 64); }
    int best = 0; int32_t bests = INT32_MIN;
    double t0 = now_us();
    for (int rep = 0; rep < 100; rep++)
        for (int i = 0; i < N; i++) {                 /* int8 dot, int32 accumulate */
            int32_t acc = 0;
            for (int d = 0; d < D; d++) acc += (int32_t)m8[i][d] * q8[d];
            if (acc > bests) { bests = acc; best = i; }
        }
    double t1 = now_us(); int bestb = 0, bestd = 1 << 30;
    for (int rep = 0; rep < 100; rep++)
        for (int i = 0; i < N; i++) {                 /* binary: XOR + popcount */
            int dist = 0;
            for (int w = 0; w < W; w++) dist += __builtin_popcountll(mb[i][w] ^ qb[w]);
            if (dist < bestd) { bestd = dist; bestb = i; }
        }
    double t2 = now_us();
    printf("int8   10k x 384: %.3f ms/query (best=%d)\n", (t1 - t0) / 100 / 1e3, best);
    printf("binary 10k x 384: %.3f ms/query (best=%d dist=%d)\n", (t2 - t1) / 100 / 1e3, bestb, bestd);
    printf("bytes: int8 %d KB, binary %d KB\n", (int)(sizeof m8 / 1024), (int)(sizeof mb / 1024));
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 search.c -o search && ./search
```

```text
int8   10k x 384: 0.161 ms/query (best=9400)
binary 10k x 384: 0.031 ms/query (best=3380 dist=153)
bytes: int8 3750 KB, binary 468 KB
```

출력에서 볼 것:

- int8 1만 개 0.16 ms — numpy float32(§4.2, 0.13 ms, 멀티스레드 BLAS)와 비슷한 수준을 **단일 스레드 C**로 낸다. 컴파일러가 안쪽 루프를 NEON SIMD로 자동 벡터화한 덕이다 (Apple clang `-O2`). 3.84M MAC / 0.161 ms ≈ 24 GMAC/s.
- binary는 0.031 ms로 int8보다 5배 빠르고, 데이터는 8배 작다 (468 KB — 웨어러블 SRAM이나 L2에 들어갈 크기). 경고 0개로 컴파일된다.
- 기기에서는 여기에 top-k 힙(크기 k 최소 힙, 펌웨어에서 흔한 자료구조), 메타데이터 비트마스크 필터(예: `type`·`privacy`를 비트로 두고 `if (meta[i] & mask)`로 건너뛰기)를 붙인다. SQL 없이도 pre-filter가 된다.

### 12.2 메모리 배치

```
flash (영구)                          RAM (검색 중)
┌──────────────────────────┐          ┌──────────────────────────────┐
│ SQLite DB: text, meta     │          │ binary 벡터 전체 (10k × 48 B)│ ← 1단계: 상주 (468 KB)
│ int8 벡터 (10k × 388 B)   │──필요시──►│ 후보 int8 벡터 (C × 388 B)   │ ← 2단계: 후보만 읽기
│ 임베딩 모델 (int8 ~23 MB) │──mmap───►│ 활성값 버퍼 (작음)           │
└──────────────────────────┘          └──────────────────────────────┘
```

- 1단계(binary) 벡터만 RAM에 상주시키고, 2단계 재정렬용 int8 벡터는 flash에서 후보만 읽는 구조가 RAM이 빠듯한 기기에 맞다. SSD 펌웨어의 "매핑 테이블 일부만 DRAM 캐시" 전략과 같다.
- 임베딩 모델은 LLM과 별개의 NN이다. NPU에 올릴 수 있으면 질문 임베딩도 수 ms 이하로 줄어든다 (F·I 모듈). 다만 NPU 컨텍스트 전환 비용과 LLM과의 메모리 경합을 따져야 한다.
- 쓰기 경로(추출·재임베딩·IVF 재학습·VACUUM)는 **충전 중 + 화면 꺼짐** 같은 조건에서 몰아서 한다. flash 쓰기 횟수(마모)도 같이 줄어든다.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 임베딩 정규화 누락 | 긴 메모가 항상 상위에 뜸 | 내적이 노름에 비례 | 저장·질문 모두 L2 정규화, 단위 테스트로 노름 1 확인 |
| 임베딩 모델 교체 후 재임베딩 안 함 | 검색 결과가 무작위처럼 됨 | 다른 모델의 벡터 공간은 호환 안 됨 | `embed_model` 열 저장, 버전 다르면 재임베딩 마이그레이션 |
| "누구의/언제의"를 벡터로 풀려 함 | 엄마 일정·지난 일정을 내 다음 일정으로 답함 (§7.3) | 임베딩은 주제는 잡지만 주체·시간 구분이 약함 | `about`, `ts` 메타데이터 + pre-filter |
| 프라이버시 필터를 검색 단계에 걸고 끝 | 건강 질문에 엉뚱한 답 (§6.2) | 정답이 걸러진 상태로 LLM이 그럴듯한 답 생성 | 출력 채널별 정책 + "말할 수 없음" 경로 명시 |
| 코사인 임계값만으로 중복 제거 | 새 주차 위치가 옛 것과 합쳐짐 (§9.3, 0.932) | 같은 주제·다른 값 문장은 코사인이 높음 | 코사인은 후보 찾기에만, 숫자·날짜·이름 비교 + superseded 처리 |
| top-k를 크게 고정 | 지연·KV-cache 증가, 정답률은 그대로 (§8, k=20 → 7배 토큰) | context 예산 개념 없음 | 토큰 예산 + 관련성 하한 + 필터 |
| `DELETE`만으로 삭제 완료 처리 | DB 파일에 원문·벡터 잔해 (§10.3) | SQLite freelist, WAL, flash stale page | secure_delete / VACUUM, 파생 데이터 lineage, 암호화 + 키 폐기 |
| 호스트 numpy 속도로 알고리즘 판정 | "int8·binary가 더 느리다"는 잘못된 결론 | numpy의 정수·비트 연산 경로가 최적화 안 됨 | C/NEON/타깃 런타임에서 측정 (§12) |
| ANN을 먼저 도입 | 삭제가 어렵고 recall 손실, 디버깅 난이도 상승 | 개인 기억 규모(≤10만)에서는 불필요 | brute force부터, 측정 후 필요하면 IVF·binary 2단 |
| 작은 LLM의 추출 결과를 그대로 저장 | 없던 "PM", 타이머 명령이 기억으로 저장 (§9.2) | grammar는 형식만 보장 | 의도 필터, 원문 대조 검증, 사용자 확인 |

---

## 14. 면접에서 이렇게 말한다

**Q.** "How would you give an on-device assistant long-term memory?"

**A.** 가중치가 아니라 기기 안 저장소에 짧은 사실 단위로 기억을 저장하고, 질문마다 관련 기억을 검색해 프롬프트에 넣는 RAG로 한다. 저장소는 SQLite(원문 + 메타데이터 + int8 임베딩), 검색은 메타데이터 pre-filter 후 brute-force 코사인, 쓰기 경로는 추출 → 중복·충돌 확인 → TTL. 0.5B 모델로 해 보니 no-RAG 0/10, RAG 9/10, 메타데이터 필터 추가 10/10이었다.

> I'd keep memory outside the model weights: a local SQLite store of short, human-readable facts with metadata — type, timestamp, who it's about, privacy class — plus an int8 embedding per row. At query time I pre-filter with SQL, do a brute-force cosine search, pack the top results under a token budget, and put them in the prompt. Writes go through extraction, dedup and conflict checks, and a TTL policy. In a small experiment with a 0.5B model, personal questions went from 0 of 10 without retrieval to 9 of 10 with RAG and 10 of 10 once I added metadata filters.

**Q.** "Brute-force vs ANN search on a phone — when would you use which?"

**A.** 개인 기억은 보통 1만~10만 개라 brute force가 기본이다. 1만 개 int8은 C 단일 스레드로 0.16 ms, 정확도 100%, 삭제도 쉽다. ANN은 brute force가 지연·전력 예산을 넘을 때 쓰는데, 그 전에 binary 1단계 + int8 재정렬이 더 단순하다. IVF는 직접 만들어 보니 전체의 0.7%만 훑고 recall@10 0.87, 25%를 훑어야 0.97이었다 — recall 1.0에 다가갈수록 비싸다.

> For personal memory, N is usually ten to a hundred thousand, so I start with exact brute force: 10k int8 vectors take about 0.16 ms single-threaded on a laptop core, it's exact, and deletes are trivial. I'd only reach for ANN when measurements show brute force breaking the latency or power budget, and even then a binary first pass with int8 re-ranking is simpler than a graph index. When I built a small IVF, scanning 0.7% of the data gave 0.87 recall@10 and getting to 0.97 needed a quarter of the data — the last few points of recall are expensive.

**Q.** "How do you quantize embeddings, and what do you lose?"

**A.** 벡터마다 대칭 scale 하나로 int8(388 B, 1/4)로 하면 순위가 거의 그대로다 — 5천 개 실험에서 top-10 일치율 0.96, 정답 recall 변화 없음. 부호 비트만 남기는 binary(48 B, 1/32)는 순위가 크게 흔들려 top-10 일치율 0.29였지만, binary로 후보를 뽑고 float로 재정렬하면 회복된다. 손실은 동점권 순서이고, 확실한 정답은 대부분 유지된다. 반드시 자기 데이터로 recall을 잰다.

> Per-vector symmetric int8 is nearly free: a quarter of the size, and in my test on five thousand memories the top-10 overlap with float32 was 0.96 with no change in answer recall. Sign-bit binary is 32 times smaller but reorders results heavily — 0.29 overlap — so I'd use it only as a first-pass filter and re-rank candidates in higher precision. What you lose is the ordering among near-ties; strong matches mostly survive. The query can stay in float, and I'd always measure recall on our own data.

**Q.** "How do you delete a memory properly?"

**A.** 원문 행만 지우면 안 된다. 임베딩, 파생 요약·프로필, 캐시, 로그, 다른 기기 복제본까지 지워야 하고, 그러려면 lineage(어떤 데이터가 어디서 왔나)를 처음부터 기록해야 한다. 파일 수준에서도 SQLite `DELETE` 후 파일 바이트에 원문과 벡터가 남는 것을 직접 확인했다 — secure_delete나 VACUUM이 필요하고, flash 아래층까지 생각하면 암호화 + 키 폐기가 최종 방어선이다. SSD Sanitize와 같은 문제다.

> A memory isn't one row. Deleting it means the text, its embedding and any index entries, derived summaries or profiles, caches and logs, and replicas on other devices via a tombstone — so lineage has to be recorded at write time. At the file level, I checked that a plain SQLite DELETE left both the text and the embedding bytes in the database file; you need secure_delete or a VACUUM, and since flash itself keeps stale pages, encryption at rest with key destruction is the real backstop. It's the same problem as SSD sanitize, which I worked on.

**Q.** "How do you keep retrieved memories from blowing the context budget?"

**A.** k개가 아니라 토큰 예산으로 생각한다. 기억 한 줄이 약 30 토큰이고, k를 1에서 20으로 늘리면 프롬프트가 7배, prefill이 약 6배가 되는데 정답률은 그대로였다. 그래서 메타데이터 pre-filter로 후보를 줄이고, 관련성 하한과 토큰 예산으로 담고, 반복되는 정보는 요약 프로필로 합친다. 기억 시스템의 전력 비용도 검색이 아니라 이 늘어난 prefill에서 나온다.

> I budget in tokens, not in k. Each memory line was about 30 tokens; going from k=1 to k=20 multiplied prompt length by seven and prefill time by about six, with no gain in accuracy. So I pre-filter with metadata, pack by score under a token budget with a relevance floor, and fold recurring facts into a short profile. That also matters for power — the retrieval itself is a tiny fraction of the compute, but the extra prefill tokens it adds are not.

**Q.** "Is it safe to keep only embeddings and drop the raw text?"

**A.** 아니다. 임베딩에서 원문이나 속성을 상당 부분 복원할 수 있다는 연구가 있다(Song & Raghunathan 2020, Vec2Text 2023). 복원 정도는 모델과 공격 조건에 따라 다르지만, 설계에서는 임베딩을 원문과 같은 민감도로 다룬다 — 같이 암호화하고 같이 지운다. 그리고 원문이 없으면 사용자가 무엇이 기억되었는지 볼 수도 없다.

> No. Published work has shown that sentence embeddings can leak a lot of the original text and attributes — inversion attacks like Vec2Text reconstruct short inputs surprisingly well for some models. How much leaks depends on the model and the attacker, but I'd treat embeddings with the same sensitivity as the text: encrypted together, deleted together. And without the text, the user can't see what the device remembers, which hurts trust.

---

## 15. 직접 해보기

1. (손계산) u = (1, 2, 2), v = (2, 1, −2). 코사인 유사도와 단위 벡터 사이 L2 거리²를 구하라.
정답: u·v = 2 + 2 − 4 = 0, |u| = |v| = 3 → cos = 0, L2² = 2 − 0 = 2.

2. (손계산) 기억 5만 개, 384-d. float32·int8(벡터당 scale 4 B 포함)·binary 저장량을 MiB로. 그리고 PQ(m=48, 256 centroid) 코드 + 코드북 크기는?
정답: 73.2 / 18.5 / 2.29 MiB, PQ 코드 2.29 MiB + 코드북 384 KB.

3. (코드) §3.3 코드에 "전체 공통 scale 하나"인 int8(per-tensor)을 추가해서 per-vector와 top-10 overlap을 비교하라.
힌트: `s = np.abs(M).max() / 127` 하나. C1 §5의 granularity 이야기와 같은 결과가 나오는지 본다.

4. (코드) §9.3의 중복 판정을 고쳐라: 코사인 0.75 이상인 후보에 대해 두 문장의 숫자 토큰(`re.findall(r"\d+", t)`) 집합이 같으면 MERGE, 다르면 SUPERSEDE로 판정. 주차 Lot D와 사물함 217이 각각 어떻게 나오나?
정답: 사물함 → MERGE (숫자 217 동일), 주차 → SUPERSEDE (숫자 집합 {2, 2026} vs {14, 1, 2026}으로 다름). 단 "Lot C"와 "Lot D"처럼 글자로 된 값은 숫자 비교로 안 잡힌다는 한계도 확인할 것.

5. (코드) §5.3의 IVF에 nlist를 64와 1024로 바꿔 recall@10 0.95를 내는 데 필요한 "훑은 벡터 수"를 비교하라.
힌트: nlist가 크면 칸이 작아져 nprobe가 커지고, centroid 비교 비용(nlist × d)도 무시 못 하게 된다. 보통 nlist ≈ √N 근처에서 시작한다.

6. (설계) 사용자가 "내 건강 관련 기억 전부 지워줘"라고 했다. §10.3 체크리스트를 따라 실제로 실행할 SQL·파일 작업·동기화 작업 순서를 적어라. 요약 프로필에 건강과 비건강 정보가 섞여 있으면 어떻게 하나?
힌트: 요약은 "지우기"가 아니라 "건강 원본을 뺀 나머지로 재생성". 그래서 요약마다 src 목록(lineage)이 필요하다.

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| RAG (retrieval-augmented generation) | 검색 증강 생성 | 관련 문서를 검색해 프롬프트에 붙인 뒤 생성 |
| embedding | 임베딩 | 의미가 비슷하면 방향이 비슷한 고정 길이 벡터 |
| mean pooling | 평균 풀링 | 토큰별 벡터를 mask 고려해 평균내서 문장 벡터 하나로 |
| cosine similarity | 코사인 유사도 | 두 벡터 각도의 코사인. 단위 벡터면 내적과 같다 |
| recall@k | 상위 k 재현율 | 정답이 상위 k개 안에 든 질문의 비율 |
| episodic memory | 일화 기억 | 시간이 붙은 사건·대화 |
| semantic memory | 의미 기억 | 시간과 무관한 사실·선호 |
| brute force / flat search | 전수 검색 | 모든 벡터와 비교. 정확, 비용 O(N) |
| ANN | 근사 최근접 이웃 | 일부만 비교해 거의 맞는 top-k를 빠르게 |
| IVF | inverted file index | k-means 칸으로 나누고 가까운 nprobe칸만 비교 |
| nprobe | 탐색 칸 수 | IVF에서 전수 비교할 list 수. recall과 속도 교환 |
| HNSW | 계층형 근접 그래프 | 이웃 링크 그래프를 탐욕적으로 탐색하는 ANN |
| PQ (product quantization) | 곱 양자화 | 벡터를 조각내 조각별 코드북 번호로 저장, 표 조회로 점수 |
| binary embedding | 이진 임베딩 | 부호 비트만 남긴 벡터, Hamming 거리로 비교 |
| re-ranking | 재정렬 | 싼 1단계로 후보를 뽑고 정밀한 2단계로 순서 다시 매김 |
| pre-filter / post-filter | 선·후 필터 | 메타데이터 조건을 벡터 검색 전/후에 거는 것 |
| constrained decoding | 제약 디코딩 | grammar/JSON schema에 맞는 토큰만 고르게 강제 |
| supersede | 대체 | 새 값이 옛 기억을 무효화 (선호 변경, 새 주차 위치) |
| recency / importance | 최신성 / 중요도 | 검색 점수에 더하는 시간 감쇠·종류별 가중치 |
| TTL (time to live) | 보관 기한 | 지나면 자동 삭제되는 기간 |
| lineage | 계보 | 파생 데이터가 어떤 원본에서 나왔는지의 기록 |
| secure_delete | SQLite 보안 삭제 | 삭제된 내용을 파일에서 0으로 덮는 PRAGMA |
| crypto-erase | 암호 삭제 | 키를 폐기해 암호화된 데이터를 무의미하게 만듦 |
| E2E encryption | 종단 간 암호화 | 서버는 암호문만 보고 키는 기기에만 |
| embedding inversion | 임베딩 역추적 | 임베딩에서 원문·속성을 복원하는 공격 |

---

## 17. 요약 & 체크리스트

LLM은 가중치가 고정된 함수라 사용자를 기억하지 못한다. "persistent memory"는 LLM 바깥에 짧은 사실 단위의 기억 저장소(SQLite: 원문 + 메타데이터 + 임베딩)를 두고, 질문마다 관련 기억을 검색해 context window에 올리는 RAG로 만든다 — 펌웨어로 치면 NAND에서 필요한 페이지만 DRAM으로 올리는 demand paging이다. MiniLM(22.7M 파라미터, 384-d) 임베딩은 단어가 겹치지 않는 질문도 잘 찾지만(recall@1 0.967) "누구의·언제의"는 구분하지 못한다. 그래서 메타데이터 필터가 필수이고, 0.5B 모델 실험에서 no-RAG 0/10 → RAG 9/10 → RAG+필터 10/10이 되었다. 저장 벡터는 int8로 1/4로 줄여도 거의 손실이 없고, binary는 1/32이지만 재정렬과 함께 써야 한다. 개인 기억 규모(≤10만)에서는 brute force가 0.1~수 ms로 충분하고, IVF 같은 ANN은 recall을 내주고 속도를 사는 도구다. 검색 비용은 LLM 대비 0.3% 수준이라 배터리 문제는 검색이 아니라 기억이 늘린 prefill 토큰이다. 쓰기 경로에서는 grammar로 형식을 강제해도 내용 검증이 필요하고, 코사인 임계값만으로는 "같은 사실"과 "같은 주제의 새 값"을 구분할 수 없다. 삭제는 원문·임베딩·파생 요약·캐시·복제본 모두에 걸쳐야 하고, SQLite 파일 수준(secure_delete/VACUUM)과 flash 수준(암호화 + 키 폐기)까지 내려가야 끝난다. 임베딩도 원문만큼 민감하다.

- [ ] 단기·일화·의미 기억을 구분하고 각각 어디에 저장되는지 그림으로 설명할 수 있다
- [ ] 단위 벡터에서 L2² = 2 − 2cos를 손으로 보이고, 코사인 검색이 GEMV 한 번임을 설명할 수 있다
- [ ] mean pooling + L2 정규화로 문장 임베딩을 직접 만들 수 있다
- [ ] 임베딩을 int8(per-vector scale)과 binary로 양자화하고 top-k overlap·recall로 손실을 잴 수 있다
- [ ] 기억 N개의 저장량을 형식별로 MiB 단위로 계산하고 brute force 시간을 대역폭으로 어림할 수 있다
- [ ] IVF를 k-means로 만들고 nprobe에 따른 recall·훑은 양 교환을 설명할 수 있다
- [ ] SQLite에 벡터 BLOB과 메타데이터를 넣고 pre-filter 검색을 짤 수 있다
- [ ] RAG 프롬프트를 만들고 no-RAG/RAG/RAG+필터를 결정적 checker로 비교할 수 있다
- [ ] 토큰 예산·관련성 하한으로 기억을 고르고, 기억이 prefill·KV-cache에 주는 비용을 계산할 수 있다
- [ ] 기억 하나를 "제대로" 지우는 체크리스트(원문·벡터·파생·캐시·복제본·파일 잔해·flash)를 말할 수 있다

---

## 참고 자료

- Lewis et al., "Retrieval-Augmented Generation for Knowledge-Intensive NLP Tasks", NeurIPS 2020 — RAG라는 이름의 출처.
- Reimers & Gurevych, "Sentence-BERT: Sentence Embeddings using Siamese BERT-Networks", EMNLP 2019 — 문장 임베딩 학습의 기본.
- Wang et al., "MiniLM: Deep Self-Attention Distillation for Task-Agnostic Compression of Pre-Trained Transformers", NeurIPS 2020 — MiniLM 계열의 바탕.
- all-MiniLM-L6-v2 모델 카드: [huggingface.co/sentence-transformers/all-MiniLM-L6-v2](https://huggingface.co/sentence-transformers/all-MiniLM-L6-v2)
- Jégou, Douze, Schmid, "Product Quantization for Nearest Neighbor Search", IEEE TPAMI 2011 — PQ와 IVF-ADC.
- Malkov & Yashunin, "Efficient and robust approximate nearest neighbor search using Hierarchical Navigable Small World graphs", IEEE TPAMI 2018 (arXiv 2016) — HNSW.
- Johnson, Douze, Jégou, "Billion-scale similarity search with GPUs", 2017 — Faiss. 코드: [github.com/facebookresearch/faiss](https://github.com/facebookresearch/faiss)
- Park et al., "Generative Agents: Interactive Simulacra of Human Behavior", UIST 2023 — recency·importance·relevance 기억 검색 점수.
- Packer et al., "MemGPT: Towards LLMs as Operating Systems", 2023 — context window를 OS의 메모리 계층처럼 관리하는 발상.
- Song & Raghunathan, "Information Leakage in Embedding Models", ACM CCS 2020.
- Morris et al., "Text Embeddings Reveal (Almost) As Much As Text", EMNLP 2023 — Vec2Text.
- SQLite PRAGMA 문서 (secure_delete, foreign_keys): [sqlite.org/pragma.html](https://www.sqlite.org/pragma.html)
- llama.cpp (llama-server, JSON schema → grammar): [github.com/ggml-org/llama.cpp](https://github.com/ggml-org/llama.cpp)
- 이 노트 시리즈: A1 §2(코사인·임베딩), B5 §5(화자 임베딩), C1(양자화), D2·D3(메모리·roofline), H6 §8–9(암호화·보관·삭제), L2(context 관리), L3(hybrid 라우팅), L6(prompt caching)

---

## 부록 A. 예제 데이터와 실행 방법

모든 Python 예제는 같은 폴더에 아래 `memdata.py`와 §2.3의 `emb.py`, §7.2의 `rag.py`를 두고 실행한다. urllib3가 LibreSSL 경고를 찍으므로 `PYTHONWARNINGS=ignore .venv/bin/python ex.py`로 돌렸다. RAG 예제(§7, §9.2)는 llama-server가 8095 포트에 떠 있어야 한다:

```sh
.tools/llama.cpp/build/bin/llama-server -m .tools/models/qwen2.5-0.5b-Q8_0.gguf \
    --port 8095 -c 4096 -np 1 --temp 0 --seed 0
```

`memdata.py` — 가상 사용자 Robin Park의 기억 (전부 허구):

```python
# Fictional personal memory store for "Robin Park" (a made-up user). All data synthetic.
import random

NOW = "2026-10-03"

# (id, text, type, date, privacy)
ANCHORS = [
    ("m000", "Dentist appointment with Dr. Alvarez on Thursday, October 8, 2026 at 3:30 PM.", "event", "2026-10-08", "health"),
    ("m001", "Had a dental cleaning with Dr. Alvarez on March 3, 2026; next checkup in six months.", "event", "2026-03-03", "health"),
    ("m002", "Mom's dentist appointment is on October 20, 2026 at 10 AM; I promised to drive her.", "event", "2026-10-20", "health"),
    ("m003", "My sister Hana's birthday is November 14.", "fact", "2026-01-10", "normal"),
    ("m004", "My brother Jun's birthday is April 2.", "fact", "2026-01-10", "normal"),
    ("m005", "I prefer an aisle seat on flights, never the middle.", "preference", "2026-02-11", "normal"),
    ("m006", "My gym locker number is 217 at the Riverside fitness club.", "fact", "2026-06-01", "normal"),
    ("m007", "Priya recommended I read the novel The Overstory by Richard Powers.", "conversation", "2026-09-12", "normal"),
    ("m008", "Parked the car at the airport in Lot C, row 14, on October 1, 2026.", "event", "2026-10-01", "normal"),
    ("m009", "Parked at the mall garage level B, spot 3, on September 5, 2026.", "event", "2026-09-05", "normal"),
    ("m010", "My manager is Teresa Wong; our one-on-one is every Tuesday.", "fact", "2026-03-15", "normal"),
    ("m011", "I take my coffee as an oat milk flat white, no sugar.", "preference", "2026-08-20", "normal"),
    ("m012", "I used to drink black coffee every morning.", "preference", "2025-05-02", "normal"),
    ("m013", "My passport expires on February 19, 2027; renew it before the Japan trip.", "fact", "2026-04-04", "sensitive"),
    ("m014", "I am allergic to penicillin.", "fact", "2025-11-30", "health"),
    ("m015", "Flight to Tokyo departs December 18, 2026 at 11:05 AM from SFO.", "event", "2026-12-18", "normal"),
    ("m016", "The Wi-Fi network at the cabin is called PineHollow.", "fact", "2026-07-07", "normal"),
    ("m017", "Dog Mochi needs his heartworm pill on the first day of every month.", "fact", "2026-05-01", "normal"),
    ("m018", "Mochi's vet is Dr. Okafor at Bayview Animal Clinic.", "fact", "2026-05-01", "normal"),
    ("m019", "I don't eat cilantro; it tastes like soap to me.", "preference", "2026-02-20", "normal"),
    ("m020", "Book club meets the last Sunday of each month at Lena's house.", "fact", "2026-03-01", "normal"),
    ("m021", "Car insurance renewal is due on November 30, 2026.", "event", "2026-11-30", "sensitive"),
    ("m022", "Lent my camping stove to Marcus in August; he hasn't returned it yet.", "conversation", "2026-08-14", "normal"),
    ("m023", "Favorite running route is the 8 km loop around Lake Merritt.", "preference", "2026-06-18", "normal"),
    ("m024", "Promised to send Daniel the slides from the robotics talk.", "conversation", "2026-09-29", "normal"),
    ("m025", "The kids' school pickup on Wednesdays is at 2:45 PM.", "fact", "2026-08-25", "normal"),
    ("m026", "Blood pressure reading was 128 over 82 at the clinic.", "event", "2026-09-10", "health"),
    ("m027", "Prefer meetings before noon; afternoons are for deep work.", "preference", "2026-04-22", "normal"),
    ("m028", "Anniversary dinner reservation at Nopa on October 24, 2026 at 7 PM.", "event", "2026-10-24", "normal"),
    ("m029", "Lena said she is moving to Denver in January.", "conversation", "2026-09-27", "normal"),
    ("m030", "Garage door code was changed last week; the new code is stored in the password manager.", "fact", "2026-09-20", "sensitive"),
    ("m031", "Want to learn to make fresh pasta this winter.", "preference", "2026-09-01", "normal"),
    ("m032", "Next haircut booked for October 12, 2026 at 11 AM with Sam.", "event", "2026-10-12", "normal"),
    ("m033", "Annual physical exam scheduled for January 9, 2027.", "event", "2027-01-09", "health"),
    ("m034", "The plumber said the water heater is about 12 years old and should be replaced soon.", "conversation", "2026-09-18", "normal"),
    ("m035", "Hana's favorite flowers are peonies.", "fact", "2026-05-05", "normal"),
    ("m036", "Electric bill autopay is set for the 15th of each month.", "fact", "2026-02-02", "sensitive"),
    ("m037", "Shoe size is US 10.5 for running shoes.", "fact", "2026-03-30", "normal"),
    ("m038", "Daniel's daughter just started kindergarten.", "conversation", "2026-09-02", "normal"),
    ("m039", "Thinking about switching to a standing desk because of back pain.", "conversation", "2026-08-08", "health"),
]

# 30 retrieval queries: (query, gold id) -- paraphrased on purpose
QUERIES = [
    ("When do I see the dentist next?", "m000"), ("When was my last teeth cleaning?", "m001"),
    ("Do I need to drive my mother somewhere this month?", "m002"), ("What day is my sister's birthday?", "m003"),
    ("When is Jun's birthday?", "m004"), ("Which airplane seat do I like?", "m005"),
    ("What's the number of my locker at the gym?", "m006"), ("Which book did Priya suggest?", "m007"),
    ("Where did I leave the car at the airport?", "m008"), ("Who is my boss?", "m010"),
    ("How do I like my coffee?", "m011"), ("When does my passport expire?", "m013"),
    ("Which antibiotics should I avoid?", "m014"), ("What time is my flight to Japan?", "m015"),
    ("What's the cabin's wifi name?", "m016"), ("When does the dog get his medicine?", "m017"),
    ("Who is our veterinarian?", "m018"), ("Which herb do I hate?", "m019"),
    ("Where does the book club meet?", "m020"), ("When is the car insurance due?", "m021"),
    ("Who has my camping stove?", "m022"), ("Where do I usually go running?", "m023"),
    ("What did I promise to send Daniel?", "m024"), ("What time do I pick up the kids on Wednesday?", "m025"),
    ("What was my blood pressure?", "m026"), ("When do I like to schedule meetings?", "m027"),
    ("Where are we going for our anniversary?", "m028"), ("Is anyone moving away soon?", "m029"),
    ("What flowers should I buy for my sister?", "m035"), ("Is the water heater okay?", "m034"),
]

PEOPLE = ["Alicia", "Ben", "Chloe", "Diego", "Emma", "Farid", "Grace", "Hiro", "Isla", "Kofi", "Maya", "Noah", "Omar", "Rosa", "Theo"]
PLACES = ["Blue Bottle", "Tartine", "the farmers market", "Costco", "Trader Joe's", "the library", "the hardware store", "Golden Gate Park", "the office", "the climbing gym"]
PROJECTS = ["the Q4 roadmap", "the sensor bring-up", "the battery test plan", "the hiring loop", "the demo video", "the budget review", "the firmware release", "the customer escalation"]
ITEMS = ["paper towels", "a phone charger", "new headphones", "dog food", "a rain jacket", "batteries", "olive oil", "a birthday card", "light bulbs", "printer ink"]
TOPICS = ["home renovation ideas", "a podcast about chip design", "weekend hiking plans", "a new board game", "electric bikes", "the World Cup", "sourdough starters", "a documentary about octopuses"]

def filler(n=160, seed=0):
    rng = random.Random(seed)
    out = []
    templates = [
        ("Had lunch with {p} at {pl} on {d}.", "event"),
        ("Meeting with {p} about {pr} on {d}.", "event"),
        ("Bought {it} at {pl} on {d}.", "event"),
        ("Talked with {p} about {t}.", "conversation"),
        ("{p} mentioned they are interested in {t}.", "conversation"),
        ("Ran {k} km in {pl2} on {d}.", "event"),
        ("Reminder to follow up with {p} on {pr}.", "conversation"),
        ("Watched {t} in the evening on {d}.", "event"),
    ]
    for i in range(n):
        tpl, typ = templates[i % len(templates)]
        mo = rng.randint(1, 9); da = rng.randint(1, 28)
        d = f"2026-{mo:02d}-{da:02d}"
        text = tpl.format(p=rng.choice(PEOPLE), pl=rng.choice(PLACES), pr=rng.choice(PROJECTS),
                          it=rng.choice(ITEMS), t=rng.choice(TOPICS), k=rng.randint(3, 15),
                          pl2=rng.choice(["Golden Gate Park", "the Presidio", "Lake Merritt", "the waterfront"]), d=d)
        out.append((f"f{i:03d}", text, typ, d, "normal"))
    return out

def all_memories():
    return ANCHORS + filler()
```

예제 실행 순서: §2.4(예제가 `M.npy`, `Q.npy`를 저장) → 나머지는 순서 무관. §5.3의 IVF는 k-means 학습에 수 초~십수 초가 걸린다.
