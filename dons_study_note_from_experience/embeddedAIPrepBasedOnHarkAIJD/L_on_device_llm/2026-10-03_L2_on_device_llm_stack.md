# L2. 온디바이스 LLM 스택 — 서비스 구조, 세션·KV 관리, 컨텍스트, 스트리밍, 취소, 메모리 압박

> **이 노트를 다 읽으면**: 웨어러블/폰 SoC 위 LLM을 "모델 파일"이 아니라 **프로세스·IPC·세션·메모리 정책을 가진 서비스**로 설계할 수 있다 · 여러 턴 대화에서 KV-cache를 재사용해 TTFT를 줄이고, context 한계를 orchestrator 정책으로 지킬 수 있다 · token 스트림을 TTS용 phrase로 자르고, 사용자가 말을 끊으면(barge-in) 생성을 수십 ms 안에 멈출 수 있다 · mmap·dirty/clean 메모리·idle unload를 숫자로 설명하고, deadline·취소·telemetry가 들어간 API 계약을 코드로 쓸 수 있다
> **JD 연결**: "(우대) Experience with lightweight LLM models, hybrid edge-LLM pipelines, or **integrating small language models on device**" — study_prep_list **L2**: tokenizer → runtime → sampler → streaming 출력 · KV-cache 관리, context 한계, 메모리 압박 시 동작
> **Don 기준 난이도**: 큐·명령 취소·자원 풀·전원 상태·장애 격리 같은 **펌웨어 시스템 설계 감각은 이미 강한 부분** / LLM 서비스가 대화 상태를 어떻게 들고 있는지(slot, prompt cache), context 정책, 스트리밍·barge-in, mmap 기반 메모리 회수는 새로 배울 부분
> **선행 노트**: B8 (tokenizer·sampler·chat template·TTFT/TPOT), D5 (KV-cache 크기·prefix caching·해석 모델), F3 (llama.cpp 내부·GGUF·`llama.h` 루프·mmap/repack), F4 (QNN/Genie), J1 (펌웨어 통합 패턴), J2 (실시간 설계), K2 (스레드 수와 tg), K4 (열·지속 성능)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 이미 본 조각들, 그리고 L2가 하는 일

지금까지 노트들은 LLM을 **부품 단위**로 봤다.

| 노트 | 이미 다룬 것 | L2에서는 |
|---|---|---|
| B8 | BPE tokenizer, sampler, chat template, TTFT/TPOT 첫 측정 | 다시 설명하지 않는다. "service 안의 한 블록"으로만 쓴다 |
| D5 | KV-cache 크기 공식, prefix caching의 원리, decode 대역폭 모델 | 공식은 인용만. **여러 세션·여러 턴에서 KV를 누가 언제 지우나**를 본다 |
| F3 | llama.cpp/ggml 구조, GGUF, `llama.h` C API 루프, mmap·repack | API 루프 대신 그 위의 **서버 프로세스**(`llama-server`)를 쓴다 |
| F4 | QNN, Genie | backend 후보로만 언급한다 |
| J1 | 센서→추론 파이프라인, 정적 메모리, 큐, 생명주기 | 같은 사고방식을 **LLM 서비스**에 적용한다 |
| J5 | 모델 OTA | 범위 밖 (모델 교체 시 세션 무효화만 짧게) |

L2의 질문은 하나다. **"작은 LLM을 기기 위에서 '제품'으로 돌리려면, 모델 바깥에 무엇이 있어야 하나?"**

모델은 `token id 배열 → 다음 token 확률`을 계산하는 함수일 뿐이다. 제품에서는 이런 일이 생긴다.

- 사용자는 대화를 **여러 턴** 이어 간다. 매 턴 전체 대화를 다시 계산하면 TTFT가 턴마다 늘어난다.
- 대화가 길어지면 **context window**를 넘는다. 누가, 무엇을 버릴지 정해야 한다.
- 음성 비서라면 답을 **다 만든 뒤 읽으면 늦다**. token이 나오는 대로 문장 조각을 TTS로 넘겨야 한다.
- 사용자가 답을 듣다가 **말을 끊는다**(barge-in). 생성·TTS를 즉시 멈추고 다음 질문을 받아야 한다.
- 다른 앱이 메모리를 원하면 OS가 **메모리를 회수하거나 프로세스를 죽인다**.
- 오디오 파이프라인과 **CPU·대역폭을 나눠 쓴다**. LLM이 오디오를 끊기게 하면 안 된다.

이 노트는 이것들을 **이 Mac 위의 실제 서비스(`llama-server`)로 하나씩 재 본다.**

### 0.2 스택 한 장

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 500">
<defs><marker id="l2arr" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><rect x="20" y="16" width="520" height="62" rx="8" fill="#4a7bd0" fill-opacity="0.15" stroke="#4a7bd0"/><text x="32" y="36" font-size="13" font-weight="bold">① App / UI · 음성 front-end</text><text x="32" y="56" font-size="12">mic → VAD → wake word → ASR (텍스트)          TTS ← phrase ← 스피커</text><text x="32" y="71" font-size="12">버튼·화면·haptic, 사용자 말 끊기(barge-in) 감지</text><rect x="20" y="92" width="520" height="78" rx="8" fill="#3f9a6b" fill-opacity="0.15" stroke="#3f9a6b"/><text x="32" y="112" font-size="13" font-weight="bold">② Assistant orchestrator (앱 쪽 프로세스)</text><text x="32" y="132" font-size="12">session store(대화 이력) · context policy(요약·잘라내기) · tool router</text><text x="32" y="148" font-size="12">deadline·취소 · phrase chunker → TTS · hybrid(L3) 라우팅 · telemetry</text><text x="32" y="163" font-size="12">"무엇을 물어볼지"를 정한다 — 모델은 모른다</text><line x1="10" y1="184" x2="550" y2="184" stroke="#d0564a" stroke-width="2" stroke-dasharray="7 5"/><text x="20" y="200" font-size="12">IPC 경계: Android = Binder/AIDL, Linux = gRPC·Unix socket, 이 노트 = HTTP + SSE (llama-server)</text><rect x="20" y="210" width="520" height="118" rx="8" fill="#e08a3c" fill-opacity="0.15" stroke="#e08a3c"/><text x="32" y="230" font-size="13" font-weight="bold">③ LLM service (별도 프로세스, 모델 1벌 소유)</text><rect x="32" y="240" width="92" height="36" rx="4" fill="none" stroke="currentColor"/><text x="78" y="263" font-size="12" text-anchor="middle">tokenizer</text><rect x="132" y="240" width="92" height="36" rx="4" fill="none" stroke="currentColor"/><text x="178" y="263" font-size="12" text-anchor="middle">scheduler</text><rect x="232" y="240" width="92" height="36" rx="4" fill="none" stroke="currentColor"/><text x="278" y="263" font-size="12" text-anchor="middle">runtime</text><rect x="332" y="240" width="92" height="36" rx="4" fill="none" stroke="currentColor"/><text x="378" y="263" font-size="12" text-anchor="middle">sampler</text><rect x="432" y="240" width="96" height="36" rx="4" fill="none" stroke="currentColor"/><text x="480" y="263" font-size="12" text-anchor="middle">KV manager</text><text x="32" y="296" font-size="12">slots(세션별 KV) · prompt cache · chat template · grammar/tool</text><text x="32" y="314" font-size="12">streaming 출력 · 취소 감지 · idle unload · metrics</text><rect x="20" y="342" width="520" height="56" rx="8" fill="#888" fill-opacity="0.15" stroke="#888"/><text x="32" y="362" font-size="13" font-weight="bold">④ Backend (ggml CPU·GPU / QNN·Genie / ExecuTorch …)</text><text x="32" y="384" font-size="12">CPU NEON 커널 · GPU(OpenCL/Vulkan/Metal) · NPU(Hexagon HTP 등)</text><rect x="20" y="412" width="520" height="70" rx="8" fill="#4a7bd0" fill-opacity="0.08" stroke="currentColor" stroke-dasharray="3 3"/><text x="32" y="432" font-size="13" font-weight="bold">⑤ OS / 플랫폼</text><text x="32" y="452" font-size="12">메모리: mmap page cache, reclaim, LMK·onTrimMemory</text><text x="32" y="470" font-size="12">전력: DVFS governor · 열: thermal throttling · 스케줄러: 우선순위·affinity</text><line x1="575" y1="40" x2="575" y2="455" stroke="#4a7bd0" stroke-width="2" marker-end="url(#l2arr)"/><text x="585" y="60" font-size="12">요청</text><text x="585" y="76" font-size="12">messages</text><text x="585" y="92" font-size="12">deadline</text><line x1="640" y1="300" x2="640" y2="45" stroke="#3f9a6b" stroke-width="2" marker-end="url(#l2arr)"/><text x="600" y="330" font-size="12">token</text><text x="600" y="346" font-size="12">stream</text><text x="600" y="362" font-size="12">(SSE)</text>
</svg>
```

그림 1 — 온디바이스 LLM 스택. 위에서 아래로 요청(messages, deadline)이 내려가고, 아래에서 위로 token stream이 올라온다. 빨간 점선이 프로세스 경계(IPC)다. 이 노트의 실험은 ③을 `llama-server`로, IPC를 HTTP+SSE로, ②를 Python으로 흉내 낸다.

각 층의 책임을 한 줄씩 정리하면:

| 층 | 책임 | 펌웨어 비유 |
|---|---|---|
| ① App / voice front-end | 마이크·스피커, VAD·ASR·TTS, 화면 | 호스트 애플리케이션 |
| ② Assistant orchestrator | 대화 이력 보관, 프롬프트 조립, context 정책, tool 호출, 취소·deadline, phrase→TTS | 호스트 드라이버 (NVMe driver: 큐 관리, timeout, abort) |
| ③ LLM service | 모델 1벌 소유, tokenize → prefill/decode → sample, 세션별 KV, 스케줄링, streaming | SSD 컨트롤러 펌웨어 (명령 큐, 자원 풀, 내부 스케줄러) |
| ④ Backend | 실제 연산 커널 (CPU NEON, GPU, NPU) | HW 가속기 + 드라이버 |
| ⑤ OS | 메모리 회수, DVFS, 열, 스케줄러 | 플랫폼 전원·열 관리 |

Don에게 가장 가까운 비유는 **NVMe SSD**다. 호스트(②)는 큐에 명령(요청)을 넣고, 컨트롤러 펌웨어(③)는 여러 큐(slot)를 스케줄링하며, 호스트는 Abort 명령(취소)을 보낼 수 있고, 컨트롤러는 자기 DRAM(KV-cache)과 NAND(저장된 slot 파일)를 관리한다. 전원 상태(PS0~PS4)에 따라 응답 지연이 달라지는 것도 같다 — LLM 서비스의 "모델 올려 둠/내림"이 그 역할을 한다.

### 0.3 이 노트의 실험 환경

| 항목 | 값 |
|---|---|
| 기계 | Apple M2 MacBook Air (8 core: P 4 + E 4, 24 GB unified memory) |
| 서비스 | `llama-server` (llama.cpp 빌드 `b1-f7b384c`, ggml 0.25.3) |
| 모델 | Qwen2.5-0.5B-Instruct Q4_K_M GGUF (494 M param, 파일 380 MiB, 24 layer, KV head 2 × 128) |
| 실행 | **CPU only** (`-ngl 0`), `-t 4`, 127.0.0.1의 높은 포트 |
| client | `.venv/bin/python` 표준 라이브러리(http.client, json, threading) |

`-ngl 0`으로 GPU를 끈 이유: 웨어러블 SoC에서 LLM은 CPU(NEON)나 NPU에서 돌 가능성이 높고, CPU 경로가 스레드·스케줄링 문제를 가장 잘 드러낸다.

> **측정 경고**: 이 노트의 측정은 다른 작업 여러 개(다른 llama-server 3~5개 포함)가 같은 Mac에서 돌던 중에 했다. load average가 **9~75** 사이를 오갔다(8 core 기계). 그래서 절대 시간(ms)은 조용한 기계보다 2~10배 나쁘고 흔들린다. 이 노트에서는 **같은 조건에서 번갈아 잰 비교(비율)**, **server가 직접 센 token 수**(시간과 무관)를 주로 믿고, 절대 ms는 "이 정도 크기" 감각으로만 쓴다. 실제 기기에서는 반드시 다시 재야 한다.

---

## 1. 서비스 구조 — 왜 LLM은 "별도 프로세스의 서비스"가 되나

### 1.1 직관 — 모델 1벌, 사용자 여럿

기기 안에서 LLM을 쓰고 싶은 주체는 여럿이다: 음성 비서, 알림 요약, 메시지 답장 제안, 번역. 각자 모델을 따로 올리면 380 MiB × 4가 된다. 그래서 보통 **모델을 1벌만 올린 서비스 프로세스**를 두고, 나머지는 IPC로 요청한다.

별도 프로세스로 두는 이유를 정리하면:

| 이유 | 설명 |
|---|---|
| 메모리 1벌 | 가중치·KV pool을 한 곳에서 관리. 여러 client가 공유 |
| 장애 격리 | backend 드라이버(GPU/NPU)가 죽어도 UI·오디오 프로세스는 산다. 서비스만 재시작 |
| 메모리 회계 | OS가 "LLM이 쓰는 메모리"를 한 프로세스로 보고 회수·kill 우선순위를 줄 수 있다 |
| 스케줄링 | 여러 요청을 한 곳에서 줄 세우고(slot), 우선순위(음성 > 배경 요약)를 줄 수 있다 |
| 권한·보안 | 모델 파일·개인 데이터 접근을 한 프로세스로 제한 |

비용도 있다: IPC 왕복 지연(보통 수십 µs~1 ms 수준으로 LLM 시간에 비하면 작다), 직렬화, 그리고 **상태(KV-cache)가 서비스 안에 있다**는 사실 — client가 "대화 상태"를 가졌다고 착각하면 안 된다.

### 1.2 플랫폼별 IPC — 개념 (실행하지 않음)

| 플랫폼 | 흔한 형태 | 스트리밍 방법 | 취소 방법 |
|---|---|---|---|
| Android | bound Service + AIDL (Binder) | client가 넘긴 callback interface(oneway)로 token 이벤트 | `cancel(requestId)` 메서드, 또는 Binder death 통지 |
| Embedded Linux | systemd 서비스 + Unix domain socket / gRPC | gRPC server-streaming, 또는 소켓에 길이-접두 메시지 | gRPC cancel (HTTP/2 RST_STREAM), 소켓 close |
| 이 노트 | `llama-server` HTTP | SSE (`text/event-stream`) | TCP 연결 close |

조심할 점(헤지): Android에서 실제 제품이 AIDL을 쓰는지, 시스템 서비스(AICore 같은)를 쓰는지, 앱 내부 라이브러리로 링크하는지는 회사·기기마다 다르다. 여기서는 **개념적 대응**만 기억한다 — 어떤 IPC든 "요청 / 이벤트 스트림 / 취소 / 오류"의 네 가지가 계약에 있어야 한다(11절).

### 1.3 `llama-server` 안쪽 — 한 장으로

소스(`tools/server/server-context.cpp`, `server-queue.cpp`)를 읽으면 구조는 펌웨어의 "명령 큐 + 단일 처리 루프"와 같다.

```
 HTTP threads (요청마다 1개)              main loop thread (1개)                 ggml threadpool (-t N)
 ─────────────────────────              ───────────────────────────             ───────────────────────
 POST /v1/chat/completions
   ├ chat template 적용 + tokenize
   ├ task 생성 → queue_tasks.post() ──► 빈 slot 고르기 (prompt가 가장 많이 겹치는 slot)
   │                                     slot마다: 공통 prefix 이후만 prefill
   │                                     모든 busy slot의 다음 token을 한 batch로
   │                                     llama_decode(batch) ─────────────────► 행렬곱 커널 (N 스레드)
   │                                     sample → slot.add_token
   ◄── queue_results (token 1개씩) ◄──── send_partial_response
   ├ SSE "data: {...}" 로 write
   └ 연결이 끊기면 should_stop() → CANCEL task를 큐 맨 앞에 넣음 ──► slot.release()
```

핵심 단어 세 개:

- **slot**: 동시에 처리할 수 있는 요청 자리. slot마다 자기 KV-cache 영역(sequence)과 "지금 KV에 들어 있는 token 목록"을 가진다. `--parallel N`(`-np`)으로 개수를 정한다.
- **continuous batching**: 여러 slot의 decode를 한 번의 `llama_decode`로 묶는다. 한 요청이 끝나면 그 자리에 바로 다음 요청이 들어온다.
- **prompt cache**: 새 요청의 token이 slot에 남아 있는 token과 앞부분이 같으면 그 부분은 다시 계산하지 않는다 (`cache_prompt`, 기본 true). 이것이 3절의 주제다.

### 1.4 이 빌드에서 확인한 기능 목록

`llama-server --help`와 `tools/server/README.md`(같은 소스 트리)로 확인했다. llama.cpp는 매주 바뀌므로, **다른 빌드에서는 옵션 이름이 다를 수 있다**.

| 기능 | 옵션 / 엔드포인트 | 이 노트에서 |
|---|---|---|
| context 크기 | `-c, --ctx-size` (0 = 모델 기본값) | 4096, 512 |
| slot 수 | `-np, --parallel` (기본 -1 = auto) | 1, 2 |
| prompt cache | `--cache-prompt` (기본 on), 요청 필드 `cache_prompt` | 3절 |
| KV shift 재사용 | `--cache-reuse N`, 요청 필드 `n_cache_reuse` | 개념만 (3.5) |
| host RAM prompt cache | `-cram, --cache-ram N` (기본 8192 MiB), `--cache-idle-slots` | 4.3 |
| slot 저장/복원 | `--slot-save-path`, `POST /slots/{id}?action=save/restore/erase` | 4.2 실측 |
| slot 상태 | `GET /slots` (기본 on) — `is_processing`, `n_decoded` 등 | 7절 |
| context shift | `--context-shift` (**기본 off**), 요청 필드 `n_keep` | 5절 |
| 로딩 방식 | `-lm, --load-mode {auto,none,mmap,mlock,mmap+mlock,dio}` — 이 빌드엔 `--no-mmap` 대신 이것 | 9절 |
| weight repack | `--repack / --no-repack` | 9절 |
| idle 시 모델 내리기 | `--sleep-idle-seconds N`, `GET /props`의 `is_sleeping` | 10절 |
| 지표 | `--metrics` → `GET /metrics` (Prometheus) | 11절 |
| 토큰 세기 | `POST /tokenize`, `POST /apply-template` | 5절 |
| 문법 제약 | 요청 필드 `grammar` (GBNF) | 11절 |
| 스레드 우선순위 | `--prio N` (0 normal ~ 3 realtime), `--cpu-mask` | 8절 개념 |

### 1.5 코드 — 이 노트 전체가 쓰는 SSE helper (예제 1)

무엇을 확인하는 코드인지: OpenAI 호환 `/v1/chat/completions`를 `stream: true`로 부르고, SSE 줄을 하나씩 읽어 (도착 시각, 새 텍스트, 원본 이벤트)를 내보낸다. `conn_out`으로 연결 객체를 넘겨 주면 호출자가 나중에 연결을 끊어 **취소**할 수 있다.

```python
# sse.py — llama-server의 SSE 스트림을 읽어 (도착시각, delta, 마지막 chunk) 를 내보낸다
import http.client, json, time

def stream_chat(port, messages, conn_out=None, **params):
    body = json.dumps({"messages": messages, "stream": True, **params})
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    if conn_out is not None:
        conn_out.append(conn)                      # 호출자가 close()로 취소할 수 있게
    t0 = time.perf_counter()
    conn.request("POST", "/v1/chat/completions", body,
                 {"Content-Type": "application/json"})
    resp = conn.getresponse()
    if resp.status != 200:
        raise RuntimeError(f"HTTP {resp.status}: {resp.read()[:200]!r}")
    for raw in resp:                               # SSE는 줄 단위: "data: {...}\n\n"
        line = raw.decode().strip()
        if not line.startswith("data: ") or line == "data: [DONE]":
            continue
        ev = json.loads(line[6:])
        ch = ev["choices"][0] if ev.get("choices") else {}
        text = ch.get("delta", {}).get("content") or ""
        yield time.perf_counter() - t0, text, ev
    conn.close()

def post(port, path, obj=None, method="POST"):
    conn = http.client.HTTPConnection("127.0.0.1", port, timeout=60)
    conn.request(method, path, json.dumps(obj) if obj is not None else None,
                 {"Content-Type": "application/json"})
    r = conn.getresponse(); data = r.read(); conn.close()
    return r.status, json.loads(data) if data else None
```

SSE(Server-Sent Events)는 HTTP 응답 본문을 닫지 않고 `data: <JSON>\n\n` 줄을 계속 흘려보내는 형식이다. 실제로 이 서버가 보낸 줄 하나(모델 경로는 줄였다):

```text
data: {"choices":[{"finish_reason":null,"index":0,"delta":{"content":" How"}}],"created":1791053967,"id":"chatcmpl-xw2uiXLfoRmK5wvTNRnw9oDv8WPdlUPE","model":"…/qwen2.5-0.5b-Q4_K_M.gguf","system_fingerprint":"b1-f7b384c","object":"chat.completion.chunk"}
```

마지막 chunk에는 `finish_reason`(`stop` = EOS, `length` = 한도)과 server 쪽 `timings`가 붙는다.

```text
data: {"choices":[{"finish_reason":"length","index":0,"delta":{}}], … ,"timings":{"cache_n":24,"prompt_n":8,"prompt_ms":83.172, … ,"predicted_n":8,"predicted_ms":622.112, …}}
```

출력에서 볼 것: `timings.cache_n`(KV에서 재사용한 prompt token 수)과 `prompt_n`(새로 계산한 prompt token 수)이 이 노트에서 가장 많이 쓰는 두 숫자다. 둘 다 **token 개수**라 기계가 바빠도 흔들리지 않는다.

---

## 2. 모델 로딩 — cold start, warm start, 그리고 첫 요청

### 2.1 직관 — 부팅 시퀀스와 같다

서비스가 "요청을 받을 수 있는 상태"가 되기까지 하는 일은 펌웨어 부팅과 닮았다.

1. GGUF header·metadata 파싱 (F3 4절). 수 ms.
2. 가중치 매핑: 기본은 `mmap` — 파일을 주소 공간에 붙이기만 하고, 실제로 읽는 것은 **처음 건드릴 때(page fault)**다. 펌웨어로 치면 "NAND의 FTL 테이블을 통째로 읽지 않고, 필요한 page만 demand loading".
3. **repack**: CPU backend는 일부 가중치를 SIMD 친화 배치로 다시 펼쳐 **익명 메모리(anonymous memory)**에 복사한다. 이 단계는 그 텐서를 전부 읽어야 하므로 flash I/O가 생긴다.
4. KV-cache와 compute buffer 할당 (ctx 크기에 비례).
5. warmup: 빈 입력으로 한 번 돌려 커널·스레드풀을 깨운다(`--warmup`, 기본 on).

이 Mac의 로그에서 실제 메모리 배분은 이렇게 찍혔다 (ctx 4096, CPU only).

```text
common_memory_breakdown_print: | memory breakdown [MiB] | total    free    self   model   context   compute    unaccounted |
common_memory_breakdown_print: |   - Host               |                   430 =   303 +      48 +      79                |
common_memory_breakdown_print: |   - CPU_REPACK         |                   208 =   208 +       0 +       0                |
```

말로 하면: model 열에 host 303 MiB(파일에서 매핑한 가중치)와 CPU_REPACK 208 MiB(SIMD 배치로 복사한 가중치)가 있고, KV-cache(context) 48 MiB, 중간 활성값(compute) 79 MiB가 있다. 두 model 값의 합(511 MiB)이 파일 크기(374 MiB)보다 크므로 일부 가중치는 매핑 영역과 repack 복사본에 걸쳐 회계되는 것으로 보인다(정확한 회계 규칙은 소스로 확인하지 않았다 — 9절에서 OS 쪽 숫자로 다시 본다). KV 48 MiB는 손으로 맞춰 볼 수 있다.

```
KV/token = 2 (K,V) × 24 layer × (2 head × 128) × 2 B (f16) = 12,288 B = 12 KiB
4096 token × 12 KiB = 48 MiB   ← 로그의 context 48과 일치
```

### 2.2 page cache가 "warm"을 만든다

cold와 warm의 차이는 **파일 내용이 이미 RAM의 page cache에 있느냐**다. 프로세스가 끝나도 커널은 읽었던 파일 page를 메모리가 남는 한 들고 있다. 그래서 두 번째 실행부터는 flash를 거의 안 읽는다.

이것을 직접 보려면 `mincore()` 시스템 콜을 쓴다. 파일을 mmap한 뒤 "각 page가 지금 RAM에 있나?"를 바이트 배열로 돌려준다. 읽지 않고 묻기만 하므로 page cache를 오염시키지 않는다.

무엇을 확인하는 코드인지: 모델 파일 중 몇 MiB가 page cache에 올라와 있는지 잰다 (예제 2).

```python
import ctypes, mmap, os, sys
import numpy as np
libc = ctypes.CDLL(None)
libc.mincore.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_void_p]
PG = os.sysconf("SC_PAGE_SIZE")

def resident_mib(path):
    """mincore(): 이 파일의 page 중 지금 RAM(page cache)에 있는 비율"""
    sz = os.path.getsize(path)
    with open(path, "rb") as f:
        m = mmap.mmap(f.fileno(), sz, prot=mmap.PROT_READ)
    a = np.frombuffer(m, dtype=np.uint8)          # 주소를 얻기 위한 view (읽지 않음)
    n = (sz + PG - 1) // PG
    vec = (ctypes.c_ubyte * n)()
    libc.mincore(a.ctypes.data, sz, vec)
    res = sum(v & 1 for v in vec)
    del a; m.close()
    return res * PG / 2**20, n * PG / 2**20

if __name__ == "__main__":
    for p in sys.argv[1:]:
        r, t = resident_mib(p)
        print(f"{os.path.basename(p):34s} {r:6.1f} / {t:6.1f} MiB resident ({100*r/t:5.1f}%)")
```

```sh
.venv/bin/python resid.py .tools/models/qwen2.5-0.5b-*.gguf     # 이 노트의 실험을 다 끝낸 뒤 실행
```

```text
qwen2.5-0.5b-f16.gguf                 0.0 /  948.1 MiB resident (  0.0%)
qwen2.5-0.5b-Q2_K.gguf                0.0 /  322.9 MiB resident (  0.0%)
qwen2.5-0.5b-Q4_0.gguf                0.0 /  335.8 MiB resident (  0.0%)
qwen2.5-0.5b-Q4_K_M.gguf            379.4 /  379.4 MiB resident (100.0%)
qwen2.5-0.5b-Q6_K.gguf                0.0 /  482.3 MiB resident (  0.0%)
qwen2.5-0.5b-Q8_0.gguf                0.0 /  506.5 MiB resident (  0.0%)
```

출력에서 볼 것: 지금 서비스가 쓰고 있는 Q4_K_M만 100%다. 흥미로운 것은 나머지다. 실험 시작 전에는 다른 실험이 막 썼던 Q8_0이 506.5 MiB(100%) resident였고, 아래 예제 3에서 Q6_K와 Q4_0을 각각 3번씩 띄워 100%로 만들어 놓았는데, **십여 분 뒤 이 측정에서는 셋 다 0%로 돌아갔다.** 다른 작업들(llama-server 여러 개)이 메모리를 쓰자 커널이 clean page cache를 먼저 버린 것이다. **"한 번 띄웠으니 다음엔 warm"이라는 가정은 메모리 압박 아래에서 깨진다** — 9절의 주제를 미리 본 셈이다.

### 2.3 코드로 확인 — cold vs warm 로딩 (예제 3)

무엇을 확인하는 코드인지: 한 번도 읽지 않은 모델로 서버를 3번 띄우며, 띄우기 직전의 resident 양, `/health`가 200이 될 때까지의 시간, 첫 요청의 prompt 처리 시간을 잰다. 먼저 이후 예제들이 쓰는 서버 기동 helper다. `llama-server`를 subprocess로 띄우고 `/health`가 200이 될 때까지 10 ms 간격으로 polling해서, 그때까지 걸린 시간을 돌려준다.

```python
# srv.py — llama-server를 띄우고 /health가 200이 될 때까지 기다린다
import subprocess, time, urllib.request, sys, signal
ROOT = "/Users/donh/workspace/dons_tech_study_dashboard/dons_study_note_from_experience/embeddedAIPrepBasedOnHarkAIJD/.tools/"
BIN, MD = ROOT + "llama.cpp/build/bin/llama-server", ROOT + "models/"
def start(model, port, log, *extra):
    t0 = time.perf_counter()
    p = subprocess.Popen([BIN, "-m", MD + model, "--host", "127.0.0.1", "--port", str(port), *extra],
                         stdout=open(log, "w"), stderr=subprocess.STDOUT)
    while True:
        try:
            if urllib.request.urlopen(f"http://127.0.0.1:{port}/health", timeout=1).status == 200: break
        except Exception: pass
        if p.poll() is not None: raise SystemExit("server died")
        time.sleep(0.01)
    return p, (time.perf_counter() - t0) * 1000
def stop(p):
    p.send_signal(signal.SIGINT); p.wait(timeout=20)
if __name__ == "__main__":
    model, port, log, keep = sys.argv[1], int(sys.argv[2]), sys.argv[3], sys.argv[4]
    p, ms = start(model, port, log, *sys.argv[5:])
    print(f"pid={p.pid} ready_ms={ms:.0f}")
    if keep == "stop": stop(p)
    else: open("server.pid", "w").write(str(p.pid))
```

그리고 실험 코드:

```python
# 예제: 같은 모델을 3번 띄운다 — 첫 번째는 flash에서, 나머지는 page cache에서 읽는다
import sys
from srv import start, stop, MD
from resid import resident_mib
from sse import post
model = sys.argv[1]                                   # 이 실험 전까지 한 번도 읽지 않은 파일
for i in range(3):
    r, tot = resident_mib(MD + model)                 # 띄우기 직전 page cache 상태
    p, ready = start(model, 23922, "load.log", "-c", "4096", "-np", "1", "-ngl", "0", "-t", "4")
    st, j = post(23922, "/v1/chat/completions",
                 {"messages": [{"role": "user", "content": "Hi"}], "max_tokens": 1})
    print(f"run {i}: resident before {r:5.1f}/{tot:.1f} MiB → /health OK {ready:5.0f} ms, "
          f"first prompt {j['timings']['prompt_ms']:.0f} ms")
    stop(p)
```

```text
run 0: resident before   0.0/482.3 MiB → /health OK  2393 ms, first prompt 706 ms
run 1: resident before 482.3/482.3 MiB → /health OK  1679 ms, first prompt 254 ms
run 2: resident before 482.3/482.3 MiB → /health OK  1582 ms, first prompt 108 ms
```

출력에서 볼 것: 실행 직전 0%였던 파일이 첫 실행 뒤 전체(482 MiB) page cache에 남았고, 그 뒤로 로딩이 약 0.7~0.8초 빨라졌다. 첫 실행의 "first prompt"(706 ms)도 느린데, mmap이라 로딩 때 안 건드린 텐서(repack되지 않은 것)를 첫 요청이 page fault로 읽기 때문으로 보인다(추정 — 로딩 경로를 trace로 확인하지는 않았다).

같은 실험을 다른 조건으로 반복한 결과(모두 실측, 바쁜 기계):

| 모델 · backend | cold (page cache 0%) | warm (100%) |
|---|---|---|
| Q6_K 482 MiB · CPU (위 출력) | 2393 ms | 1582~1679 ms |
| Q4_0 336 MiB · CPU | 1430 ms | 1082~1118 ms |
| Q4_K_M 380 MiB · Metal (기본 설정) | 808 ms | 398~451 ms |

이 Mac의 SSD는 수 GB/s라 cold 비용이 수백 ms에 그쳤다. **웨어러블의 eMMC/UFS는 더 느리고(특히 eMMC), page cache로 쓸 여유 RAM도 적다.** 그래서 기기에서는 cold 비용이 훨씬 크고, "page cache가 남아 있을 것"을 가정하면 안 된다.

### 2.4 임베디드 연결 — load를 숨기는 법

- **wake word에서 미리 로드**: wake word 검출 → ASR이 사용자의 말을 듣는 1~3초 동안 LLM을 로드한다. 사용자가 말을 마칠 때쯤 준비가 끝난다. (펌웨어의 "명령을 받기 전에 다음 블록을 prefetch".)
- **`madvise(WILLNEED)`/readahead 류**: 파일을 미리 page cache에 올려 두는 힌트. 효과와 지원은 OS마다 다르다(헤지).
- **warmup을 끄지 말 것**: 첫 요청에서 스레드풀 생성·커널 JIT(GPU)가 터지면 TTFT가 튄다.

### 2.5 함정

- "로딩 시간"을 warm 상태에서만 재고 cold를 잊는다 → 재부팅 직후 첫 질문이 2~5배 느리다.
- `--load-mode mlock`으로 전부 고정하면 빠르지만, **그 메모리는 OS가 절대 회수 못 한다** (9절).
- 로딩 시간 = 파일 크기 ÷ flash 대역폭 + repack + KV 할당. 모델을 바꿀 때 파일 크기만 보지 말고 repack 여부까지 본다.

---

## 3. 여러 턴 대화 — KV를 다시 쓰면 TTFT가 턴 수에 따라 늘지 않는다

### 3.1 직관 — API는 stateless, 서버는 stateful

OpenAI 호환 chat API는 **stateless**다. client는 매 턴 **대화 전체**(system + 지금까지의 user/assistant)를 보낸다. 서버가 아무것도 기억하지 않으면 매 턴 전체를 다시 prefill해야 하고, TTFT는 대화 길이에 비례해 커진다.

그런데 이번 턴의 프롬프트는 지난 턴의 프롬프트 + 지난 답 + 새 질문이다. 즉 **앞부분이 지난번과 똑같다**. `llama-server`의 slot은 지난 요청의 token 목록과 그 KV를 들고 있으므로, 새 프롬프트와 **공통 prefix**를 찾아 그 뒤만 계산하면 된다. 이것이 `cache_prompt`다 (D5 5.6의 prefix caching을 "같은 세션의 다음 턴"에 적용한 것).

```
턴 3 프롬프트: [system][u1][a1][u2][a2][u3]
slot의 KV  :  [system][u1][a1][u2][a2]          ← 턴 2에서 이미 계산 (a2는 턴 2에서 생성하며 KV에 들어감)
새로 계산   :                              [u3]  ← + chat template 토큰 몇 개
```

### 3.2 손계산 — 8턴이면 얼마나 아끼나

system prompt 142 token, 턴마다 (질문 + 답 + template)으로 약 53 token씩 늘어난다고 하자.

```
cache 끔:  매 턴 전체 = 142, 195, 248, …  → 합 = 8 × 142 + 53 × (0+1+…+7) = 1136 + 53 × 28 = 2620 token
cache 켬:  첫 턴 142 + 이후 7턴 × 약 17 token(새 질문 + template) ≈ 142 + 119 = 261 token
```

말로 하면: cache를 끄면 총 prefill 양이 **턴 수의 제곱**으로 늘고(O(N²)), 켜면 **턴 수에 비례**한다(O(N)). 8턴에서 이미 10배 차이다.

### 3.3 코드로 확인 — 같은 대화를 cache 끔/켬으로 (예제 4)

무엇을 확인하는 코드인지: 긴 system prompt + 8개의 이어지는 질문을 같은 서버에 보내며, 턴마다 server가 실제로 계산한 prompt token 수(`prompt_n`), 그 시간(`prompt_ms`), client가 잰 TTFT를 기록한다. 잡음이 커서 끔/켬을 **번갈아 3회** 돌리고 중앙값을 낸다.

```python
# 예제: 대화가 길어질 때 턴마다 TTFT — prompt cache(KV 재사용) 켬 vs 끔
import statistics as st
from sse import stream_chat, post
PORT = 23917
SYS = ("You are Hark, a concise voice assistant on a wearable device. Answer in at most "
       "two short sentences. Never use lists. Prefer metric units. ") * 4   # 긴 system prompt
QS = ["What is the boiling point of water?", "And at the top of Everest?",
      "Why is it different?", "How long should I boil an egg there?",
      "What about pasta?", "Is pressure cooking a fix?",
      "How does a pressure cooker work?", "Is it safe?"]

def run(cache):
    post(PORT, "/slots/0?action=erase")                   # 공정하게: 빈 slot에서 시작
    msgs, rows = [{"role": "system", "content": SYS}], []
    for q in QS:
        msgs.append({"role": "user", "content": q})
        ttft, reply = None, ""
        for t, piece, ev in stream_chat(PORT, msgs, max_tokens=40, temperature=0,
                                        cache_prompt=cache):
            if piece and ttft is None: ttft = t
            reply += piece; tm = ev.get("timings")
        msgs.append({"role": "assistant", "content": reply})
        rows.append((tm["cache_n"] + tm["prompt_n"], tm["prompt_n"], tm["prompt_ms"], 1000 * ttft))
    return rows

R = {False: [], True: []}
for rep in range(3):                                      # 잡음이 커서 3회 교대 반복 → 중앙값
    for c in (False, True): R[c].append(run(c))
med = {c: [[st.median(x) for x in zip(*turn)] for turn in zip(*R[c])] for c in R}
print("turn prompt_tok | OFF eval_tok prompt_ms TTFT_ms | ON eval_tok prompt_ms TTFT_ms")
for i, (a, b) in enumerate(zip(med[False], med[True]), 1):
    print(f"{i:4d} {a[0]:10.0f} | {a[1]:12.0f} {a[2]:9.1f} {a[3]:7.1f} | {b[1]:11.0f} {b[2]:9.1f} {b[3]:7.1f}")
print(f"sum TTFT: OFF {sum(r[3] for r in med[False]):.0f} ms, ON {sum(r[3] for r in med[True]):.0f} ms")
```

```text
turn prompt_tok | OFF eval_tok prompt_ms TTFT_ms | ON eval_tok prompt_ms TTFT_ms
   1        142 |          142     509.0   516.6 |         142     625.8   631.9
   2        177 |          177     613.5   619.3 |          17     144.8   151.2
   3        232 |          232     788.5   794.3 |          16     122.7   129.4
   4        291 |          291    1189.7  1201.9 |          20     236.6   246.2
   5        345 |          345    1244.2  1256.3 |          15     160.0   168.4
   6        401 |          401    1027.2  1034.4 |          17     187.6   197.6
   7        458 |          458    1120.2  1128.8 |          18     199.8   208.7
   8        512 |          512    1269.2  1278.4 |          15     175.7   185.7
sum TTFT: OFF 7830 ms, ON 1919 ms
```

출력에서 볼 것:

- **token 수는 손계산 그대로다**: 끔 = 2558 token(142 → 512로 매 턴 증가), 켬 = 260 token(턴 2부터 15~20개만). 실제 턴당 증가량이 53이 아니라 35~57로 들쭉날쭉한 것은 답 길이가 달라서다.
- **TTFT는 4.1배** 줄었다(합 7.8 s → 1.9 s). token 비율(9.8배)보다 작은 이유: 턴마다 고정 비용(HTTP, template, 스케줄링, 기계 잡음 수십~100 ms)이 있고, 짧은 prefill은 batch가 작아 token당 효율이 낮다.
- 턴 1의 ON(632 ms)이 OFF(517 ms)보다 느린 것은 잡음이다 — 둘 다 142 token을 똑같이 계산했다. **ms 대신 token 수를 보라**는 0.3절의 경고가 여기서 쓸모 있다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 300">
<text x="70.0" y="22.0" font-size="12" text-anchor="start">턴마다 새로 계산(prefill)한 prompt token 수 — server가 센 값 (3회 중앙값)</text><line x1="70.0" y1="250.0" x2="630.0" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="254.0" font-size="12" text-anchor="end">0</text><line x1="70.0" y1="216.7" x2="630.0" y2="216.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="220.7" font-size="12" text-anchor="end">100</text><line x1="70.0" y1="183.3" x2="630.0" y2="183.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="187.3" font-size="12" text-anchor="end">200</text><line x1="70.0" y1="150.0" x2="630.0" y2="150.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="154.0" font-size="12" text-anchor="end">300</text><line x1="70.0" y1="116.7" x2="630.0" y2="116.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="120.7" font-size="12" text-anchor="end">400</text><line x1="70.0" y1="83.3" x2="630.0" y2="83.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="87.3" font-size="12" text-anchor="end">500</text><line x1="70.0" y1="50.0" x2="630.0" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="54.0" font-size="12" text-anchor="end">600</text><line x1="70.0" y1="250.0" x2="630.0" y2="250.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="50.0" x2="70.0" y2="250.0" stroke="currentColor" stroke-width="1"/><rect x="80.0" y="202.7" width="22.0" height="47.3" fill="#e08a3c" fill-opacity="0.8"/><rect x="104.0" y="202.7" width="22.0" height="47.3" fill="#4a7bd0" fill-opacity="0.8"/><text x="91.0" y="198.7" font-size="11" text-anchor="middle">142</text><text x="115.0" y="198.7" font-size="11" text-anchor="middle">142</text><text x="103.0" y="266.0" font-size="12" text-anchor="middle">턴 1</text><rect x="150.0" y="191.0" width="22.0" height="59.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="174.0" y="244.3" width="22.0" height="5.7" fill="#4a7bd0" fill-opacity="0.8"/><text x="161.0" y="187.0" font-size="11" text-anchor="middle">177</text><text x="185.0" y="240.3" font-size="11" text-anchor="middle">17</text><text x="173.0" y="266.0" font-size="12" text-anchor="middle">턴 2</text><rect x="220.0" y="172.7" width="22.0" height="77.3" fill="#e08a3c" fill-opacity="0.8"/><rect x="244.0" y="244.7" width="22.0" height="5.3" fill="#4a7bd0" fill-opacity="0.8"/><text x="231.0" y="168.7" font-size="11" text-anchor="middle">232</text><text x="255.0" y="240.7" font-size="11" text-anchor="middle">16</text><text x="243.0" y="266.0" font-size="12" text-anchor="middle">턴 3</text><rect x="290.0" y="153.0" width="22.0" height="97.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="314.0" y="243.3" width="22.0" height="6.7" fill="#4a7bd0" fill-opacity="0.8"/><text x="301.0" y="149.0" font-size="11" text-anchor="middle">291</text><text x="325.0" y="239.3" font-size="11" text-anchor="middle">20</text><text x="313.0" y="266.0" font-size="12" text-anchor="middle">턴 4</text><rect x="360.0" y="135.0" width="22.0" height="115.0" fill="#e08a3c" fill-opacity="0.8"/><rect x="384.0" y="245.0" width="22.0" height="5.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="371.0" y="131.0" font-size="11" text-anchor="middle">345</text><text x="395.0" y="241.0" font-size="11" text-anchor="middle">15</text><text x="383.0" y="266.0" font-size="12" text-anchor="middle">턴 5</text><rect x="430.0" y="116.3" width="22.0" height="133.7" fill="#e08a3c" fill-opacity="0.8"/><rect x="454.0" y="244.3" width="22.0" height="5.7" fill="#4a7bd0" fill-opacity="0.8"/><text x="441.0" y="112.3" font-size="11" text-anchor="middle">401</text><text x="465.0" y="240.3" font-size="11" text-anchor="middle">17</text><text x="453.0" y="266.0" font-size="12" text-anchor="middle">턴 6</text><rect x="500.0" y="97.3" width="22.0" height="152.7" fill="#e08a3c" fill-opacity="0.8"/><rect x="524.0" y="244.0" width="22.0" height="6.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="511.0" y="93.3" font-size="11" text-anchor="middle">458</text><text x="535.0" y="240.0" font-size="11" text-anchor="middle">18</text><text x="523.0" y="266.0" font-size="12" text-anchor="middle">턴 7</text><rect x="570.0" y="79.3" width="22.0" height="170.7" fill="#e08a3c" fill-opacity="0.8"/><rect x="594.0" y="245.0" width="22.0" height="5.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="581.0" y="75.3" font-size="11" text-anchor="middle">512</text><text x="605.0" y="241.0" font-size="11" text-anchor="middle">15</text><text x="593.0" y="266.0" font-size="12" text-anchor="middle">턴 8</text><rect x="100.0" y="40.0" width="14.0" height="12.0" fill="#e08a3c" fill-opacity="0.8"/><text x="120.0" y="51.0" font-size="12" text-anchor="start">cache_prompt = false (매번 전체)</text><rect x="100.0" y="60.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="120.0" y="71.0" font-size="12" text-anchor="start">cache_prompt = true (새 부분만)</text><text x="350.0" y="288.0" font-size="12" text-anchor="middle">TTFT 합계(중앙값): false 7.8 s  vs  true 1.9 s  — 바쁜 기계라 ms는 흔들리지만 token 수는 정확</text>
</svg>
```

그림 2 — 턴마다 prefill한 token 수(예제 4, 3회 중앙값). 주황(cache 끔)은 대화 길이만큼 자라고, 파랑(cache 켬)은 턴 2부터 새 질문 크기(15~20 token)로 고정된다.

### 3.4 prefix가 깨지는 경우 — 가장 흔한 버그

공통 prefix는 **token 단위로 정확히 같아야** 한다. 한 token이라도 다르면 그 지점부터 끝까지 다시 계산한다. 실제로 prefix를 깨는 것들:

| 원인 | 예 | 결과 |
|---|---|---|
| system prompt에 변하는 값 | "현재 시각: 12:03:41" | 매 턴 system부터 전부 재계산 — cache가 사실상 꺼진다 |
| 저장한 답이 생성한 답과 다름 | 답을 후처리(공백 정리, 이모지 제거)해서 이력에 저장 | 그 답 위치부터 재계산 |
| 재토큰화 차이 | 생성된 token열과 같은 텍스트를 다시 tokenize하면 다른 쪼개짐이 나올 수 있다 (BPE 경계) | 경계 지점부터 재계산 (보통 작다) |
| 이력 앞쪽을 잘라냄 | context 관리로 가장 오래된 턴 삭제 | **system 바로 뒤부터 전부 재계산** — 5절에서 실측 |
| 다른 slot으로 배정 | `-np 2`에서 다음 턴이 다른 slot에 감 | 그 slot의 KV가 없으면 전부 재계산 (`id_slot`으로 고정 가능) |

변하는 정보(시각, 위치, 센서 상태)는 **system prompt 맨 앞이 아니라 이번 user 메시지 쪽**에 넣는다. 펌웨어로 치면 "자주 바뀌는 필드를 헤더 앞쪽에 두면 CRC를 매번 처음부터 다시 계산해야 한다"와 같다.

### 3.5 `--cache-reuse` — 중간이 바뀌어도 일부 살리기 (개념)

`--cache-reuse N`(요청 필드 `n_cache_reuse`)은 prefix가 깨진 뒤에도, 뒤쪽에 **N token 이상 똑같은 덩어리**가 있으면 그 KV를 위치만 옮겨(KV shifting, RoPE 위치 보정) 재사용하는 기능이다. 앞쪽 턴을 잘라낸 대화에 쓸 만해 보이지만, 위치가 바뀐 KV는 원래 계산과 정확히 같지 않다(attention이 보던 앞부분이 사라졌다). 품질 영향을 평가하지 않고 켜지 않는다. 이 노트에서는 실험하지 않았다.

---

## 4. 세션과 KV 관리 — slot, 저장/복원, 퇴출 정책

### 4.1 slot = 세션 하나의 KV 자리

`-np N`이면 slot이 N개 생기고, `-c`로 정한 KV 크기를 나눠 가진다. 이 빌드의 로그:

```text
load_model: initializing, n_slots = 1, n_ctx_slot = 4096, kv_unified = 'false'
load_model: initializing, n_slots = 2, n_ctx_slot = 2048, kv_unified = 'false'
```

말로 하면: `-c 4096 -np 2`는 "4096 token짜리 세션 2개"가 아니라 **"2048 token짜리 세션 2개"**다. KV 메모리 총량(48 MiB)은 그대로고 세션당 한도가 반으로 준다. 세션을 늘리면 각 세션의 기억이 짧아진다 — 고정 크기 SRAM을 큐 개수로 나누는 것과 같다.

| 설정 | slot당 ctx | KV 총량 (12 KiB/token) |
|---|---|---|
| `-c 4096 -np 1` | 4096 | 48 MiB |
| `-c 4096 -np 2` | 2048 | 48 MiB |
| `-c 8192 -np 2` | 4096 | 96 MiB |
| `-c 2048 -np 1` (웨어러블 예) | 2048 | 24 MiB |

(`--kv-unified`를 켜면 slot들이 하나의 KV pool을 공유해 길이가 다른 세션끼리 남는 공간을 나눠 쓴다. 이 노트에서는 쓰지 않았다.)

### 4.2 코드로 확인 — 세션 KV를 파일로 저장하고 복원 (예제 5)

무엇을 확인하는 코드인지: 긴 system prompt(약 610 token)를 가진 세션을 만들고, slot KV를 파일로 저장 → slot 비우기 → 다시 요청(KV 없음) → 파일에서 복원 → 다시 요청. 각 단계의 prefill token 수와 시간을 비교한다. 서버는 `--slot-save-path /private/tmp/claude-501/l2/slots`로 띄웠다.

```python
# 예제: 세션 KV를 파일로 저장 → slot 비우기 → 복원. 복원 후 첫 턴은 얼마나 빠른가
import os
from sse import post, stream_chat
PORT, D = 23917, "/private/tmp/claude-501/l2/slots"
SYS = "You are Hark, a wearable assistant. " + " ".join(f"Rule {i}: be brief and kind." for i in range(60))
msgs = [{"role": "system", "content": SYS}, {"role": "user", "content": "Hi!"}]
def ttft(m):
    for t, piece, ev in stream_chat(PORT, m, max_tokens=8, temperature=0):
        if ev.get("timings"): tm = ev["timings"]
    return tm["prompt_n"], tm["cache_n"], tm["prompt_ms"]
post(PORT, "/slots/0?action=erase")
print("cold   : eval %d, reused %d, prompt %.1f ms" % ttft(msgs))
st, r = post(PORT, "/slots/0?action=save", {"filename": "user42.bin"})
print("save   :", r["n_saved"], "tokens,", r["n_written"], "bytes,", r["timings"]["save_ms"], "ms",
      "| file", os.path.getsize(f"{D}/user42.bin"), "B")
print("erase  :", post(PORT, "/slots/0?action=erase")[1])
print("no KV  : eval %d, reused %d, prompt %.1f ms" % ttft(msgs))
post(PORT, "/slots/0?action=erase")
st, r = post(PORT, "/slots/0?action=restore", {"filename": "user42.bin"})
print("restore:", r["n_restored"], "tokens,", r["n_read"], "bytes,", r["timings"]["restore_ms"], "ms")
print("warm   : eval %d, reused %d, prompt %.1f ms" % ttft(msgs))
print("bytes/token = %.0f" % (r["n_read"] / r["n_restored"]))
```

```text
cold   : eval 614, reused 0, prompt 3055.4 ms
save   : 621 tokens, 7641404 bytes, 3.87 ms | file 7641404 B
erase  : {'id_slot': 0, 'n_erased': 621}
no KV  : eval 614, reused 0, prompt 2514.1 ms
restore: 621 tokens, 7641404 bytes, 2.291 ms
warm   : eval 1, reused 613, prompt 25.1 ms
bytes/token = 12305
```

출력에서 볼 것:

- 파일 크기 7,641,404 B = 621 token × 12,288 B(이론 KV/token) + 10,556 B. 남는 10 KB는 token id(621 × 4 B = 2.5 KB)와 메타데이터다. **KV 공식(D5)이 바이트 단위로 맞는다.**
- 저장·복원은 3~4 ms(page cache에 쓰고 읽은 것이라 빠르다 — 실제 flash 쓰기 대역폭이면 7.6 MB는 수십 ms가 될 수 있다). 복원 후 prefill은 **614 → 1 token, 2.5 s → 25 ms**.
- `warm`에서 eval이 0이 아니라 1인 것은 서버가 "slot마다 최소 1 token은 평가해야 다음 logits가 나온다"며 마지막 token 하나를 다시 계산하기 때문이다(로그: `need to evaluate at least 1 token for each active slot ... n_past was set to 613`).

이 기능이 기기에서 의미하는 것: **"긴 고정 프롬프트(성격, 규칙, tool 설명)의 KV를 공장에서/첫 부팅 때 한 번 계산해 flash에 두고, 매 부팅·매 세션 시작마다 복원"**할 수 있다. 610 token prefill 수 초 → 수십 ms. 단, KV 파일은 **모델 파일·양자화·런타임 버전·KV dtype에 묶인다**. 모델을 OTA로 바꾸면(J5) 이 파일도 무효화해야 한다 — 파일 헤더에 모델 해시를 넣고 맞지 않으면 버린다.

### 4.3 서버 안의 또 하나의 캐시 — host RAM prompt cache

이 빌드에는 slot KV와 별개로 **RAM prompt cache**(`--cache-ram`, 기본 8192 MiB 상한, `--cache-idle-slots` 기본 on)가 있다. 새 요청이 오면 slot 내용과 비교할 뿐 아니라, 예전 프롬프트들의 KV 사본 중 더 잘 맞는 것을 찾아 slot에 넣는다. 로그에서 이렇게 보인다.

```text
srv  get_availabl: updating prompt cache
srv          load:  - looking for better prompt, base f_keep = -1.000, f_sim = 0.000
srv          load:  - found better prompt with f_keep = 0.261, f_sim = 1.000
```

실제로 `POST /slots/0?action=erase`로 slot을 비운 직후의 요청이 이 cache 덕분에 prefill 없이 시작된 적이 있다(6.3절 예제의 TTFT 30 ms). 즉 **erase가 "서버가 이 대화를 잊었다"를 보장하지 않는다.** 두 가지 의미가 있다.

- 성능: 좋다. 여러 세션을 오가도 RAM이 허락하는 한 prefix를 되찾는다.
- 메모리와 프라이버시: 기본 상한 8 GiB는 웨어러블에 맞지 않는다. 기기에서는 `--cache-ram`을 작게 잡거나 0(끄기)으로 하고, "사용자가 대화 삭제 → KV 사본까지 삭제"를 보장하는 경로를 따로 둬야 한다(H6 프라이버시와 연결).

### 4.4 퇴출(eviction) 정책 — 누구의 KV를 내릴까

RAM에 둘 수 있는 세션 KV는 몇 개뿐이다. 세션이 더 많으면 SSD 펌웨어의 캐시 관리처럼 **퇴출 정책**이 필요하다.

| 정책 | 장점 | 단점 | 웨어러블에서 |
|---|---|---|---|
| LRU | 단순, 최근 대화가 남는다 | 주기적 접근 패턴에서 thrashing | 기본 후보 |
| 우선순위 + LRU | 음성 대화 세션은 고정(pin), 배경 작업만 LRU | 우선순위 설계 필요 | 추천 |
| 크기 기반 | 큰 KV부터 내린다 → 메모리 빨리 확보 | 긴 대화(가장 아까운 것)가 먼저 사라짐 | 메모리 압박 시에만 |
| TTL | N분 지난 세션은 무조건 내림 | 사용자가 돌아오면 재계산 | 대화 "끝" 판정과 함께 |

내릴 때 선택지는 두 가지다: **버리기**(다음에 다시 prefill) 또는 **flash에 저장**(4.2, 다음에 복원). 판단 기준은 "재계산 시간 × 재방문 확률" vs "flash 쓰기 시간·수명·공간"이다. 610 token 세션이면 저장 7.6 MB, 재계산 수 초 — 저장이 이긴다. 30 token짜리 세션은 그냥 버린다.

### 4.5 코드로 확인 — C로 쓴 KV slot pool과 LRU (예제 6)

무엇을 확인하는 코드인지: RAM slot 2개, 세션 3개(대화·알림 요약·번역)가 번갈아 오는 trace에서 LRU 퇴출이 저장·복원을 몇 번 일으키는지 본다. 기기 쪽 서비스가 slot을 관리하는 최소 골격이다.

```c
/* KV slot pool: 세션별 KV를 RAM slot에 두고, 넘치면 LRU 세션을 flash로 내린다 */
#include <stdio.h>
#include <stdint.h>

#define N_SLOTS        2              /* RAM에 동시에 둘 수 있는 세션 KV 수 */
#define KV_PER_TOKEN   12288u         /* qwen2.5-0.5B f16: 2 x 24 x 128 x 2 B */
typedef struct { int session; uint32_t n_tok; uint32_t last_use; } slot_t;
static slot_t slots[N_SLOTS];
static uint32_t now, saves, restores, misses;

static int find(int s) { for (int i = 0; i < N_SLOTS; i++) if (slots[i].session == s) return i; return -1; }
static int victim(void) {             /* 빈 slot이 있으면 그것, 없으면 가장 오래 안 쓴 slot */
    int v = 0;
    for (int i = 0; i < N_SLOTS; i++) {
        if (slots[i].session < 0) return i;
        if (slots[i].last_use < slots[v].last_use) v = i;
    }
    return v;
}
static void turn(int s, uint32_t new_tok, int on_flash[]) {
    int i = find(s);
    if (i < 0) {
        i = victim();
        if (slots[i].session >= 0) { on_flash[slots[i].session] = (int)slots[i].n_tok; saves++; }
        if (on_flash[s] > 0) { slots[i].n_tok = (uint32_t)on_flash[s]; restores++; }
        else { slots[i].n_tok = 0; misses++; }
        slots[i].session = s;
    }
    slots[i].n_tok += new_tok; slots[i].last_use = ++now;
    printf("t=%2u session %d -> slot %d, n_tok=%4u, KV=%6.2f MiB\n",
           now, s, i, slots[i].n_tok, slots[i].n_tok * KV_PER_TOKEN / 1048576.0);
}
int main(void) {
    int on_flash[4] = {0};
    for (int i = 0; i < N_SLOTS; i++) slots[i].session = -1;
    int trace[] = {0, 0, 1, 0, 2, 1, 0, 2};  /* 0=사용자 대화, 1=알림 요약, 2=번역 */
    for (unsigned k = 0; k < sizeof trace / sizeof trace[0]; k++) turn(trace[k], 60, on_flash);
    printf("saves=%u restores=%u cold_misses=%u\n", saves, restores, misses);
    return 0;
}
```

```sh
cc -std=c11 -Wall -Wextra -O2 kvpool.c -o kvpool && ./kvpool
```

```text
t= 1 session 0 -> slot 0, n_tok=  60, KV=  0.70 MiB
t= 2 session 0 -> slot 0, n_tok= 120, KV=  1.41 MiB
t= 3 session 1 -> slot 1, n_tok=  60, KV=  0.70 MiB
t= 4 session 0 -> slot 0, n_tok= 180, KV=  2.11 MiB
t= 5 session 2 -> slot 1, n_tok=  60, KV=  0.70 MiB
t= 6 session 1 -> slot 0, n_tok= 120, KV=  1.41 MiB
t= 7 session 0 -> slot 1, n_tok= 240, KV=  2.81 MiB
t= 8 session 2 -> slot 0, n_tok= 120, KV=  1.41 MiB
saves=4 restores=3 cold_misses=3
```

출력에서 볼 것: 세션 3개가 slot 2개를 돌아가며 쓰자 8턴 중 **저장 4번, 복원 3번**이 일어났다. 세션 0(사용자 대화)은 가장 자주 쓰이는데도 t=6에 세션 1에게 쫓겨나(flash로 저장) t=7에 slot 1로 복원됐다. 세션 0을 pin(퇴출 금지)하면 저장·복원이 배경 세션 쪽에서만 일어난다. **퇴출 정책은 trace로 시험한다** — SSD 캐시 정책을 워크로드 trace로 검증하던 것과 같다.

---

## 5. Context window 관리 — 넘치면 무슨 일이 생기고, 누가 막아야 하나

### 5.1 세 가지 반응

ctx(여기서는 slot당 512 token)를 넘을 때 서버가 할 수 있는 일은 세 가지다.

| 상황 | 이 빌드의 기본 동작 | 다른 선택 |
|---|---|---|
| 프롬프트 자체가 ctx보다 길다 | **HTTP 400**, `exceed_context_size_error` | 없음 — client가 줄여야 한다 |
| 프롬프트는 들어가지만 생성 중 ctx가 찬다 | 생성을 멈추고 `finish_reason = "length"` (서버 소스: `!params_base.ctx_shift && n_tokens + 1 >= n_ctx` → `STOP_TYPE_LIMIT`) | `--context-shift`: 앞의 `n_keep` token은 남기고 그 뒤 절반을 버린 뒤 KV 위치를 당겨 계속 생성 |
| 대화 이력이 계속 자란다 | 서버는 모른다 — 매 요청을 독립적으로 본다 | **orchestrator 정책** (5.3) |

context shift는 "무한 생성"용 응급 처치다. 버려지는 것이 대화 중간(예전 턴의 일부)이라 어떤 정보가 사라질지 제어할 수 없다. 그래서 이 빌드는 기본을 off로 둔다. **대화형 제품에서는 서버가 아니라 orchestrator가 잘라야 한다.**

### 5.2 코드로 확인 — 넘쳤을 때의 실제 응답 (예제 7)

무엇을 확인하는 코드인지: `-c 512` 서버에 (1) 1400 token짜리 프롬프트, (2) 짧은 프롬프트 + 끝없이 세는 생성(`ignore_eos`)을 보내 오류 형태와 멈춤 지점을 본다.

```python
# 예제: ctx-size(512)를 넘기면 server는 어떻게 반응하나
from sse import post
PORT = 23918
long_user = "Please remember this list: " + ", ".join(f"item{i}" for i in range(300))
n = len(post(PORT, "/tokenize", {"content": long_user})[1]["tokens"])
print("prompt tokens ≈", n)
# (1) 프롬프트 자체가 context보다 길다
st, r = post(PORT, "/v1/chat/completions",
             {"messages": [{"role": "user", "content": long_user}], "max_tokens": 16})
print("case1 HTTP", st, "→", r.get("error", r)) 
# (2) 프롬프트는 들어가지만 생성이 context 끝에 부딪힌다
short = "Count upward from 1 with words, never stop: one, two, three,"
st, r = post(PORT, "/v1/chat/completions",
             {"messages": [{"role": "user", "content": short}], "max_tokens": 2000,
              "ignore_eos": True, "temperature": 0})
u = r["usage"]
print("case2 HTTP", st, "finish_reason =", r["choices"][0]["finish_reason"],
      "| prompt", u["prompt_tokens"], "+ completion", u["completion_tokens"],
      "=", u["prompt_tokens"] + u["completion_tokens"])
```

```text
prompt tokens ≈ 1394
case1 HTTP 400 → {'code': 400, 'message': 'request (1423 tokens) exceeds the available context size (512 tokens), try increasing it', 'type': 'exceed_context_size_error', 'n_prompt_tokens': 1423, 'n_ctx': 512}
case2 HTTP 200 finish_reason = length | prompt 46 + completion 466 = 512
```

출력에서 볼 것:

- case1: `/tokenize`로 센 1394 token과 서버가 센 1423 token이 다르다. 차이 29 token은 **chat template**(`<|im_start|>system …` 기본 system 문구, 역할 표시 등)이다. 예산을 셀 때는 반드시 **template을 씌운 뒤**에 세야 한다(5.4의 `ntok()`가 `/apply-template` → `/tokenize`를 쓰는 이유).
- case2: prompt 46 + 생성 466 = **정확히 512**에서 멈췄고 HTTP는 200이다. 오류가 아니라 "잘린 답"이 정상 응답으로 온다. client가 `finish_reason`을 안 보면 **답이 중간에 끊긴 줄 모르고 TTS로 읽는다.**

### 5.3 orchestrator 정책 — system + 요약 + 최근 N턴

가장 흔한 정책은 이것이다.

```
[system prompt (고정)] [running summary: 내보낸 턴들의 핵심 사실] [최근 턴들 (원문)] [이번 질문]
 └──────────── 항상 남김 ───────────┘                                 └ 예산 안에서 최대한 ┘

예산 = n_ctx − max_new_tokens − 여유
     = 512 − 48 − 16 = 448 token
```

넘치면 가장 오래된 (user, assistant) 쌍을 내보내고, 내보낸 user 발화를 LLM에게 "사실 한 줄로 합쳐라"고 시켜 summary를 갱신한다.

여기서 **설계 변수 하나**가 성능을 크게 바꾼다: 한 번 넘쳤을 때 **얼마나 많이** 내보내나.

- eager(low_water = 1.0): 예산 아래로 들어갈 만큼만, 즉 보통 1턴씩 내보낸다. 이력이 최대한 남는다.
- hysteresis(low_water = 0.6): 예산의 60% 아래로 떨어질 때까지 한꺼번에 내보낸다. 몇 턴 동안은 다시 넘치지 않는다.

3.4의 표를 기억하면 답이 보인다. 앞쪽 턴을 지우거나 summary가 바뀌면 **system 바로 뒤부터 prefix가 깨진다.** eager는 넘친 뒤 **매 턴** 퇴출과 summary 갱신이 일어나므로 매 턴 전체를 다시 prefill한다. 펌웨어로 말하면 thermal throttling에 hysteresis를 안 넣어서 매 샘플마다 클럭이 오르내리는(chattering) 것과 같다.

### 5.4 코드로 확인 — 20턴 동안 예산 지키기, eager vs hysteresis (예제 8)

무엇을 확인하는 코드인지: 20개의 질문(이름·도시·목표 같은 사실을 앞에서 말하고, 뒤에서 다시 묻는다)을 `-c 512` 서버에 보낸다. 턴마다 실제 프롬프트 token 수(template 포함), server가 새로 prefill한 token 수, 남은 턴 수를 기록하고, 비교용으로 "이력을 하나도 안 버렸을 때(naive)"의 token 수도 센다.

```python
# 예제: orchestrator 쪽 context 정책 — system + running summary + 최근 turn, 예산 안에서
import sys
from sse import post
PORT, CTX, MAX_NEW = 23918, 512, 48
BUDGET = CTX - MAX_NEW - 16                            # 프롬프트에 쓸 수 있는 token 수 = 448
SYS = "You are Hark, a wearable voice assistant. Answer in one or two short sentences."
QS = ["My name is Mina and I live in Seoul.", "I am training for a 10 km run in May.",
      "What should I eat the night before?", "How much water during the race?",
      "I have a mild knee pain when running downhill.", "Any stretches for that?",
      "Remind me what my race distance is.", "What pace for a 55 minute finish?",
      "Is interval training useful?", "How often per week?", "I also swim on Sundays.",
      "Does swimming help running?", "What shoes do you suggest?", "How long do shoes last?",
      "I sleep only 6 hours.", "Is that enough for training?", "What city do I live in?",
      "Plan my week in one sentence.", "Any last advice before race day?", "What is my name?"]
def ntok(msgs):                                        # 실제 chat template을 씌운 뒤의 token 수
    p = post(PORT, "/apply-template", {"messages": msgs})[1]["prompt"]
    return len(post(PORT, "/tokenize", {"content": p})[1]["tokens"])
def chat(msgs, n=MAX_NEW):
    r = post(PORT, "/v1/chat/completions", {"messages": msgs, "max_tokens": n, "temperature": 0})[1]
    return r["choices"][0]["message"]["content"].strip(), r["timings"]["prompt_n"]
def build(summary, hist):
    s = SYS + (f"\nKnown facts about the user: {summary}" if summary else "")
    return [{"role": "system", "content": s}] + hist
def run(low_water):            # 넘치면 low_water × BUDGET 아래로 내려갈 때까지 오래된 turn을 내보낸다
    summary, hist, rows = "", [], []
    for q in QS:
        hist.append({"role": "user", "content": q})
        evicted = []
        if ntok(build(summary, hist)) > BUDGET:
            while ntok(build(summary, hist)) > low_water * BUDGET:
                evicted += hist[:2]; hist = hist[2:]
        if evicted:                                    # 내보낸 user 발화를 요약에 흡수 (LLM 호출 1번)
            txt = " ".join(m["content"] for m in evicted if m["role"] == "user")
            summary, _ = chat([{"role": "user", "content": "Merge into one short line of facts about "
                               f"the user (max 30 words).\nOld facts: {summary}\nNew: {txt}"}], 48)
        n = ntok(build(summary, hist)); a, ev = chat(build(summary, hist))
        hist.append({"role": "assistant", "content": a}); rows.append((n, ev, len(hist) // 2, bool(evicted)))
    return rows, summary, a
R = {lw: run(lw) for lw in (1.0, 0.6)}
print("turn | eager(1.0): prompt eval kept | hysteresis(0.6): prompt eval kept")
for i, (a, b) in enumerate(zip(R[1.0][0], R[0.6][0]), 1):
    f = lambda r: f"{r[0]:6d} {r[1]:4d} {r[2]:4d}{'*' if r[3] else ' '}"
    print(f"{i:4d} | {f(a):>27s} | {f(b):>30s}")
for lw in R:
    print(f"low_water={lw}: total prefill tokens {sum(r[1] for r in R[lw][0])}, "
          f"evictions {sum(r[3] for r in R[lw][0])}, last answer {R[lw][2][:40]!r}")
print("summary(0.6):", R[0.6][1][:150])
```

```text
turn | eager(1.0): prompt eval kept | hysteresis(0.6): prompt eval kept
   1 |               42   42    1  |                  42   22    1 
   2 |               92   23    2  |                  85   23    2 
   3 |              147   18    3  |                 139   18    3 
   4 |              212   18    4  |                 204   18    4 
   5 |              280   21    5  |                 272   21    5 
   6 |              343   16    6  |                 335   16    6 
   7 |              410   20    7  |                 402   20    7 
   8 |              441   20    8  |                 275  270    4*
   9 |              430  425    7* |                 338   16    5 
  10 |              444  439    7* |                 401   16    6 
  11 |              446  441    7* |                 212  207    3*
  12 |              441  436    7* |                 275   16    4 
  13 |              438  433    7* |                 339   17    5 
  14 |              404  399    6* |                 403   17    6 
  15 |              406  401    6* |                 220  215    3*
  16 |              407  402    6* |                 284   17    4 
  17 |              405  400    6* |                 349   18    5 
  18 |              392  387    6* |                 374   17    6 
  19 |              393  388    6* |                 425   17    7 
  20 |              392  387    6* |                 231  226    4*
low_water=1.0: total prefill tokens 5116, evictions 12, last answer 'Your name is Hark.'
low_water=0.6: total prefill tokens 1207, evictions 4, last answer 'Your name is Hark.'
summary(0.6): Mina, who lives in Seoul, plans a 10-kilometer run in May. She needs to eat less on the night of the event and drink plenty of water throughout the ra
```

(같은 실험의 앞선 실행에서 naive 열 — 이력을 전부 보낼 때의 token 수 — 은 턴 8에서 489, 턴 20에서 1189였다. 512를 넘는 턴 8부터는 5.2의 400 오류가 난다.)

출력에서 볼 것:

- **예산은 두 정책 모두 지켰다**: prompt 열의 최댓값 446(eager), 425(hysteresis) ≤ 448. 20턴 내내 오류가 없다.
- **prefill 양은 4.2배 차이**: eager는 턴 9부터 매 턴 퇴출(`*`)이 일어나 매 턴 약 400 token을 다시 계산했다(합 5116). hysteresis는 퇴출 4번, 그 턴에만 200~270 token을 계산하고 나머지 턴은 16~18 token이었다(합 1207). 퇴출 턴만 TTFT가 튀므로, 그 턴의 summary 갱신을 **TTS가 말하는 동안 배경에서** 미리 해 두면 더 숨길 수 있다.
- **0.5B 모델의 한계도 보인다**: 마지막 질문 "What is my name?"에 두 정책 모두 "Your name is Hark."라고 답했다. summary에는 "Mina"가 정확히 들어 있는데도 system prompt의 "You are Hark"와 헷갈렸다. 또 앞선 실행의 eager summary에는 사용자가 말한 적 없는 "25 years old"가 들어갔다 — **작은 모델의 요약은 사실을 지어낼 수 있다.** 정책이 token 예산은 지켜도 기억의 품질은 보장하지 않는다. 실제 제품이라면 이름·도시 같은 핵심 사실은 LLM 요약이 아니라 **구조화된 key-value(사용자 프로필)**로 따로 들고 프롬프트에 넣는다(L5 persistent memory와 연결).
- 같은 프롬프트인데 두 정책의 턴 2 prompt 수(92 vs 85)가 다른 것은 턴 1의 답이 달라서다. temperature 0이어도 **KV 재사용 여부에 따라 logits가 bit 단위로 달라질 수 있다**고 서버 README가 경고한다(`cache_prompt` 설명). "greedy면 결정적"이라는 가정은 서버 환경에서 깨진다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 430">
<text x="70.0" y="18.0" font-size="12" text-anchor="start">위: 프롬프트 token 수 (ctx 512, 예산 448)   아래: 그 턴에 새로 prefill한 token 수</text><line x1="70.0" y1="220.0" x2="600.0" y2="220.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="224.0" font-size="12" text-anchor="end">0</text><line x1="70.0" y1="177.5" x2="600.0" y2="177.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="181.5" font-size="12" text-anchor="end">300</text><line x1="70.0" y1="135.0" x2="600.0" y2="135.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="139.0" font-size="12" text-anchor="end">600</text><line x1="70.0" y1="92.5" x2="600.0" y2="92.5" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="96.5" font-size="12" text-anchor="end">900</text><line x1="70.0" y1="50.0" x2="600.0" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="54.0" font-size="12" text-anchor="end">1200</text><line x1="70.0" y1="147.5" x2="600.0" y2="147.5" stroke="#d0564a" stroke-width="1.2" stroke-dasharray="6 3"/><text x="604.0" y="151.5" font-size="11" text-anchor="start">ctx 512</text><line x1="70.0" y1="156.5" x2="600.0" y2="156.5" stroke="#e08a3c" stroke-width="1.2" stroke-dasharray="6 3"/><text x="604.0" y="168.5" font-size="11" text-anchor="start">예산 448</text><polyline points="70.0,210.2 97.9,201.7 125.8,192.4 153.7,183.2 181.6,173.5 209.5,164.6 237.4,160.4 265.3,150.7 293.2,141.8 321.1,132.9 348.9,123.8 376.8,115.5 404.7,106.4 432.6,97.3 460.5,88.1 488.4,79.5 516.3,72.9 544.2,63.7 572.1,54.5 600.0,51.6" fill="none" stroke="#888" stroke-width="2"/><polyline points="70.0,214.1 97.9,207.0 125.8,199.2 153.7,190.0 181.6,180.3 209.5,171.4 237.4,161.9 265.3,157.5 293.2,159.1 321.1,157.1 348.9,156.8 376.8,157.5 404.7,157.9 432.6,162.8 460.5,162.5 488.4,162.3 516.3,162.6 544.2,164.5 572.1,164.3 600.0,164.5" fill="none" stroke="#4a7bd0" stroke-width="2"/><polyline points="70.0,214.1 97.9,208.0 125.8,200.3 153.7,191.1 181.6,181.5 209.5,172.5 237.4,163.1 265.3,181.0 293.2,172.1 321.1,163.2 348.9,190.0 376.8,181.0 404.7,172.0 432.6,162.9 460.5,188.8 488.4,179.8 516.3,170.6 544.2,167.0 572.1,159.8 600.0,187.3" fill="none" stroke="#3f9a6b" stroke-width="2"/><line x1="70.0" y1="220.0" x2="600.0" y2="220.0" stroke="currentColor" stroke-width="1"/><line x1="70.0" y1="50.0" x2="70.0" y2="220.0" stroke="currentColor" stroke-width="1"/><text x="209.5" y="106.7" font-size="11" text-anchor="middle">naive → 턴 8부터 512 초과 (400 오류)</text><line x1="90.0" y1="36.0" x2="110.0" y2="36.0" stroke="#888" stroke-width="2"/><text x="114.0" y="40.0" font-size="11" text-anchor="start">naive</text><line x1="210.0" y1="36.0" x2="230.0" y2="36.0" stroke="#4a7bd0" stroke-width="2"/><text x="234.0" y="40.0" font-size="11" text-anchor="start">eager</text><line x1="330.0" y1="36.0" x2="350.0" y2="36.0" stroke="#3f9a6b" stroke-width="2"/><text x="354.0" y="40.0" font-size="11" text-anchor="start">hysteresis</text><line x1="70.0" y1="390.0" x2="600.0" y2="390.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="394.0" font-size="12" text-anchor="end">0</text><line x1="70.0" y1="332.2" x2="600.0" y2="332.2" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="336.2" font-size="12" text-anchor="end">200</text><line x1="70.0" y1="274.4" x2="600.0" y2="274.4" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="64.0" y="278.4" font-size="12" text-anchor="end">400</text><rect x="61.0" y="377.9" width="8.0" height="12.1" fill="#4a7bd0" fill-opacity="0.8"/><rect x="70.0" y="383.6" width="8.0" height="6.4" fill="#3f9a6b" fill-opacity="0.8"/><text x="70.0" y="405.0" font-size="11" text-anchor="middle">1</text><rect x="88.9" y="383.4" width="8.0" height="6.6" fill="#4a7bd0" fill-opacity="0.8"/><rect x="97.9" y="383.4" width="8.0" height="6.6" fill="#3f9a6b" fill-opacity="0.8"/><rect x="116.8" y="384.8" width="8.0" height="5.2" fill="#4a7bd0" fill-opacity="0.8"/><rect x="125.8" y="384.8" width="8.0" height="5.2" fill="#3f9a6b" fill-opacity="0.8"/><text x="125.8" y="405.0" font-size="11" text-anchor="middle">3</text><rect x="144.7" y="384.8" width="8.0" height="5.2" fill="#4a7bd0" fill-opacity="0.8"/><rect x="153.7" y="384.8" width="8.0" height="5.2" fill="#3f9a6b" fill-opacity="0.8"/><rect x="172.6" y="383.9" width="8.0" height="6.1" fill="#4a7bd0" fill-opacity="0.8"/><rect x="181.6" y="383.9" width="8.0" height="6.1" fill="#3f9a6b" fill-opacity="0.8"/><text x="181.6" y="405.0" font-size="11" text-anchor="middle">5</text><rect x="200.5" y="385.4" width="8.0" height="4.6" fill="#4a7bd0" fill-opacity="0.8"/><rect x="209.5" y="385.4" width="8.0" height="4.6" fill="#3f9a6b" fill-opacity="0.8"/><rect x="228.4" y="384.2" width="8.0" height="5.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="237.4" y="384.2" width="8.0" height="5.8" fill="#3f9a6b" fill-opacity="0.8"/><text x="237.4" y="405.0" font-size="11" text-anchor="middle">7</text><rect x="256.3" y="384.2" width="8.0" height="5.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="265.3" y="312.0" width="8.0" height="78.0" fill="#3f9a6b" fill-opacity="0.8"/><rect x="284.2" y="267.2" width="8.0" height="122.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="293.2" y="385.4" width="8.0" height="4.6" fill="#3f9a6b" fill-opacity="0.8"/><text x="293.2" y="405.0" font-size="11" text-anchor="middle">9</text><rect x="312.1" y="263.2" width="8.0" height="126.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="321.1" y="385.4" width="8.0" height="4.6" fill="#3f9a6b" fill-opacity="0.8"/><rect x="339.9" y="262.6" width="8.0" height="127.4" fill="#4a7bd0" fill-opacity="0.8"/><rect x="348.9" y="330.2" width="8.0" height="59.8" fill="#3f9a6b" fill-opacity="0.8"/><text x="348.9" y="405.0" font-size="11" text-anchor="middle">11</text><rect x="367.8" y="264.0" width="8.0" height="126.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="376.8" y="385.4" width="8.0" height="4.6" fill="#3f9a6b" fill-opacity="0.8"/><rect x="395.7" y="264.9" width="8.0" height="125.1" fill="#4a7bd0" fill-opacity="0.8"/><rect x="404.7" y="385.1" width="8.0" height="4.9" fill="#3f9a6b" fill-opacity="0.8"/><text x="404.7" y="405.0" font-size="11" text-anchor="middle">13</text><rect x="423.6" y="274.7" width="8.0" height="115.3" fill="#4a7bd0" fill-opacity="0.8"/><rect x="432.6" y="385.1" width="8.0" height="4.9" fill="#3f9a6b" fill-opacity="0.8"/><rect x="451.5" y="274.2" width="8.0" height="115.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="460.5" y="327.9" width="8.0" height="62.1" fill="#3f9a6b" fill-opacity="0.8"/><text x="460.5" y="405.0" font-size="11" text-anchor="middle">15</text><rect x="479.4" y="273.9" width="8.0" height="116.1" fill="#4a7bd0" fill-opacity="0.8"/><rect x="488.4" y="385.1" width="8.0" height="4.9" fill="#3f9a6b" fill-opacity="0.8"/><rect x="507.3" y="274.4" width="8.0" height="115.6" fill="#4a7bd0" fill-opacity="0.8"/><rect x="516.3" y="384.8" width="8.0" height="5.2" fill="#3f9a6b" fill-opacity="0.8"/><text x="516.3" y="405.0" font-size="11" text-anchor="middle">17</text><rect x="535.2" y="278.2" width="8.0" height="111.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="544.2" y="385.1" width="8.0" height="4.9" fill="#3f9a6b" fill-opacity="0.8"/><rect x="563.1" y="277.9" width="8.0" height="112.1" fill="#4a7bd0" fill-opacity="0.8"/><rect x="572.1" y="385.1" width="8.0" height="4.9" fill="#3f9a6b" fill-opacity="0.8"/><text x="572.1" y="405.0" font-size="11" text-anchor="middle">19</text><rect x="591.0" y="278.2" width="8.0" height="111.8" fill="#4a7bd0" fill-opacity="0.8"/><rect x="600.0" y="324.7" width="8.0" height="65.3" fill="#3f9a6b" fill-opacity="0.8"/><text x="600.0" y="405.0" font-size="11" text-anchor="middle">20</text><line x1="70.0" y1="390.0" x2="600.0" y2="390.0" stroke="currentColor" stroke-width="1"/><text x="335.0" y="422.0" font-size="12" text-anchor="middle">턴 → (합계: eager 5116 token, hysteresis 1207 token)</text>
</svg>
```

그림 3 — 예제 8의 20턴. 위: 프롬프트 token 수. naive(회색)는 턴 8에 ctx 512를 넘고, 두 정책은 예산 448 아래에 머문다. 아래: 턴마다 새로 prefill한 token. eager(파랑)는 턴 9부터 매 턴 ~400, hysteresis(초록)는 퇴출 턴에만 ~220.

### 5.5 정책 설계 체크리스트

| 결정 | 선택지 | 기준 |
|---|---|---|
| 예산 | n_ctx − max_new − 여유 | 여유에는 template 토큰, tool 결과 길이 변동을 넣는다 |
| 무엇을 먼저 버리나 | 오래된 턴 / tool 결과 / 긴 답 | tool 결과(검색 결과 등)가 가장 크고 재생성이 쉽다 |
| 얼마나 버리나 | low water mark | prefix 깨짐 횟수 vs 남는 이력 |
| 요약 | LLM 요약 / 추출식 / 구조화 프로필 | 작은 모델 요약은 hallucination 위험 → 핵심 사실은 구조화 |
| 언제 요약 | 퇴출 즉시 / 배경 / 대화 종료 시 | TTFT 경로에서 빼기 |
| 측정 | 턴별 prompt·eval token, 퇴출 횟수 | telemetry로 남긴다 (11절) |

---

## 6. 스트리밍 — token에서 TTS phrase까지

### 6.1 직관 — 음성 비서의 체감 지연은 TTFT가 아니다

D5 1.3절의 TTFA(time-to-first-audio)를 떠올리자. 사용자가 듣는 것은 첫 **소리**다. TTS는 보통 token 하나로는 자연스러운 억양을 만들 수 없어서 **문장 또는 구(phrase) 단위**로 입력을 받는다. 그래서 체인은 이렇다.

```
TTFT ──► token, token, token … ──► phrase 경계(. , ? !) ──► TTS 합성 시작 ──► 첫 소리
         └── ITL × (첫 phrase의 token 수) ──┘
```

TTFA ≈ TTFT + (첫 phrase까지의 token 수 × ITL) + TTS 첫 chunk 지연. 첫 phrase를 짧게 자를수록 빨리 말하기 시작하지만, 너무 짧으면 억양이 어색하고 TTS 호출 횟수가 는다.

### 6.2 코드로 확인 — TTFT와 ITL 분포 (예제 9)

무엇을 확인하는 코드인지: 같은 질문을 3번(매번 slot을 비우고) 스트리밍으로 받아, 첫 token 시각과 token 사이 간격(ITL, inter-token latency)의 p50/p90/max를 잰다.

```python
# 예제: 한 턴을 스트리밍으로 받아 TTFT와 token 간격(ITL)을 잰다 — 3회
import json, statistics as st
from sse import stream_chat, post
PORT = 23917
msgs = [{"role": "system", "content": "You are a concise voice assistant on a wearable."},
        {"role": "user", "content": "Explain in about five sentences why the sky is blue."}]
all_itl = []
for run in range(3):
    post(PORT, "/slots/0?action=erase")               # 공정하게: 빈 KV에서 시작
    times, last = [], None
    for t, piece, ev in stream_chat(PORT, msgs, max_tokens=160, temperature=0, seed=1):
        if piece: times.append(t)
        last = ev
    itl = [1000 * (b - a) for a, b in zip(times, times[1:])]; all_itl += itl
    tm = last["timings"]; q = sorted(itl)
    print(f"run {run}: TTFT {1000*times[0]:6.1f} ms (server prefill {tm['prompt_n']} tok "
          f"{tm['prompt_ms']:6.1f} ms) | {len(times)} tokens | ITL p50 {st.median(itl):5.1f} "
          f"p90 {q[int(.9*len(q))]:5.1f} max {q[-1]:6.1f} ms | {tm['predicted_per_second']:.1f} tok/s")
json.dump(all_itl, open("itl.json", "w"))
```

```text
run 0: TTFT  151.2 ms (server prefill 35 tok  147.2 ms) | 99 tokens | ITL p50   8.9 p90  12.2 max   21.9 ms | 104.1 tok/s
run 1: TTFT  196.1 ms (server prefill 35 tok  192.3 ms) | 99 tokens | ITL p50  12.7 p90  14.8 max  130.7 ms | 65.3 tok/s
run 2: TTFT  245.1 ms (server prefill 35 tok  239.6 ms) | 99 tokens | ITL p50  13.2 p90  16.3 max   32.3 ms | 71.5 tok/s
```

출력에서 볼 것:

- client TTFT − server prefill ≈ 4 ms. HTTP + SSE + JSON 파싱의 비용은 작다. **IPC는 병목이 아니다.**
- 같은 요청 3번인데 decode 속도가 65~104 tok/s로 흔들린다. 기계 부하 때문이다.
- p90과 max의 차이: run 1은 p90 14.8 ms인데 max가 130.7 ms다. 한 번의 긴 멈춤이 생겼다.

같은 스크립트를 부하가 더 심할 때(load average ~30) 돌렸을 때는 294개 간격의 p50 45 ms, p90 119 ms, p99 325 ms였다. **다른 프로세스와 CPU를 다투면 평균보다 꼬리가 먼저 무너진다.** 8절의 주제다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 290">
<text x="60.0" y="20.0" font-size="12" text-anchor="start">token 간격(ITL) 분포 — 3회 실측 294개, 2 ms 구간 (빨간 칸 = 40 ms 이상)</text><line x1="60.0" y1="240.0" x2="640.0" y2="240.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="244.0" font-size="12" text-anchor="end">0</text><line x1="60.0" y1="212.9" x2="640.0" y2="212.9" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="216.9" font-size="12" text-anchor="end">20</text><line x1="60.0" y1="185.7" x2="640.0" y2="185.7" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="189.7" font-size="12" text-anchor="end">40</text><line x1="60.0" y1="158.6" x2="640.0" y2="158.6" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="162.6" font-size="12" text-anchor="end">60</text><line x1="60.0" y1="131.4" x2="640.0" y2="131.4" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="135.4" font-size="12" text-anchor="end">80</text><line x1="60.0" y1="104.3" x2="640.0" y2="104.3" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="108.3" font-size="12" text-anchor="end">100</text><line x1="60.0" y1="77.1" x2="640.0" y2="77.1" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="81.1" font-size="12" text-anchor="end">120</text><line x1="60.0" y1="50.0" x2="640.0" y2="50.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="54.0" y="54.0" font-size="12" text-anchor="end">140</text><rect x="61.0" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="60.0" y="256.0" font-size="11" text-anchor="middle">0</text><rect x="88.6" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="116.2" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="143.9" y="227.8" width="25.6" height="12.2" fill="#4a7bd0" fill-opacity="0.8"/><rect x="171.5" y="153.1" width="25.6" height="86.9" fill="#4a7bd0" fill-opacity="0.8"/><rect x="199.1" y="166.7" width="25.6" height="73.3" fill="#4a7bd0" fill-opacity="0.8"/><text x="198.1" y="256.0" font-size="11" text-anchor="middle">10</text><rect x="226.7" y="70.4" width="25.6" height="169.6" fill="#4a7bd0" fill-opacity="0.8"/><rect x="254.3" y="210.1" width="25.6" height="29.9" fill="#4a7bd0" fill-opacity="0.8"/><rect x="282.0" y="226.4" width="25.6" height="13.6" fill="#4a7bd0" fill-opacity="0.8"/><rect x="309.6" y="237.3" width="25.6" height="2.7" fill="#4a7bd0" fill-opacity="0.8"/><text x="322.4" y="234.3" font-size="10" text-anchor="middle">2</text><rect x="337.2" y="237.3" width="25.6" height="2.7" fill="#4a7bd0" fill-opacity="0.8"/><text x="350.0" y="234.3" font-size="10" text-anchor="middle">2</text><text x="336.2" y="256.0" font-size="11" text-anchor="middle">20</text><rect x="364.8" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="392.4" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="420.0" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="447.7" y="237.3" width="25.6" height="2.7" fill="#4a7bd0" fill-opacity="0.8"/><text x="460.5" y="234.3" font-size="10" text-anchor="middle">2</text><rect x="475.3" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="474.3" y="256.0" font-size="11" text-anchor="middle">30</text><rect x="502.9" y="238.6" width="25.6" height="1.4" fill="#4a7bd0" fill-opacity="0.8"/><text x="515.7" y="235.6" font-size="10" text-anchor="middle">1</text><rect x="530.5" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="558.1" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="585.8" y="240.0" width="25.6" height="0.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="613.4" y="235.9" width="25.6" height="4.1" fill="#d0564a" fill-opacity="0.8"/><text x="626.2" y="232.9" font-size="10" text-anchor="middle">3</text><text x="612.4" y="256.0" font-size="11" text-anchor="middle">40</text><text x="640.0" y="256.0" font-size="11" text-anchor="end">ms</text><line x1="60.0" y1="240.0" x2="640.0" y2="240.0" stroke="currentColor" stroke-width="1"/><line x1="60.0" y1="50.0" x2="60.0" y2="240.0" stroke="currentColor" stroke-width="1"/><line x1="231.5" y1="50.0" x2="231.5" y2="240.0" stroke="#3f9a6b" stroke-width="1.5" stroke-dasharray="5 3"/><text x="235.5" y="62.0" font-size="12" text-anchor="start">p50 12.4 ms</text><line x1="261.2" y1="50.0" x2="261.2" y2="240.0" stroke="#e08a3c" stroke-width="1.5" stroke-dasharray="5 3"/><text x="265.2" y="77.0" font-size="12" text-anchor="start">p90 14.6 ms</text><line x1="640.0" y1="50.0" x2="640.0" y2="240.0" stroke="#d0564a" stroke-width="1.5" stroke-dasharray="5 3"/><text x="636.0" y="92.0" font-size="12" text-anchor="end">p99 46.9 ms</text><text x="350.0" y="276.0" font-size="12" text-anchor="middle">대부분 9~16 ms에 몰려 있지만 꼬리(최대 131 ms)가 있다 — TTS buffer가 덮어야 하는 것은 꼬리다</text>
</svg>
```

그림 4 — 예제 9의 ITL 분포(3회, 294개). 대부분 8~16 ms에 몰려 있지만 40 ms 이상인 꼬리가 3개 있다. 오디오 출력 쪽 buffer는 평균이 아니라 이 꼬리를 덮도록 잡는다.

### 6.3 코드로 확인 — token을 phrase로 자르기 (예제 10)

무엇을 확인하는 코드인지: token 스트림을 buffer에 모으다가 문장 끝(`.!?` + 공백) 또는 충분히 긴(24자 이상) 구의 쉼표에서 잘라 phrase를 내보낸다. 첫 phrase가 첫 token보다 얼마나 늦게 나오는지 본다.

```python
# 예제: token 스트림 → TTS용 phrase 조각. 첫 phrase가 언제 나오나?
import re
from sse import stream_chat, post
PORT = 23917
END = re.compile(r'([.!?])(\s|$)|([,;:])\s')          # 문장 끝 또는 (충분히 길면) 쉼표
def phrases(stream, min_chars=24, stats={}):
    buf = ""
    for t, piece, ev in stream:
        if piece and "ttft" not in stats: stats["ttft"] = t
        buf += piece
        while True:
            m = next((m for m in END.finditer(buf)
                      if m.group(1) or m.end() >= min_chars), None)
            if not m: break
            yield t, buf[:m.end()].strip(); buf = buf[m.end():]
    if buf.strip(): yield t, buf.strip()

msgs = [{"role": "system", "content": "You are a concise voice assistant on a wearable."},
        {"role": "user", "content": "Explain in about five sentences why the sky is blue."}]
post(PORT, "/slots/0?action=erase")                  # slot KV를 비운다 (RAM prompt cache는 남는다, 4.3)
stats = {}
for i, (t, ph) in enumerate(phrases(stream_chat(PORT, msgs, max_tokens=160, temperature=0,
                                                seed=1), stats=stats)):
    if i == 0: print(f"TTFT {1000*stats['ttft']:.1f} ms → first phrase +{1000*(t-stats['ttft']):.1f} ms")
    print(f"{1000*t:7.1f} ms  [{len(ph):3d} ch] {ph}")
```

```text
TTFT 30.7 ms → first phrase +149.6 ms
  180.3 ms  [ 50 ch] The sky appears blue because it reflects sunlight,
  318.1 ms  [ 58 ch] which contains all colors of the electromagnetic spectrum.
  835.7 ms  [133 ch] The color we see in the sky is actually the result of how different wavelengths of light are scattered and combined by air molecules.
 1049.0 ms  [ 44 ch] When sunlight passes through the atmosphere,
 1296.7 ms  [ 53 ch] it encounters various objects such as water droplets,
 1418.0 ms  [ 23 ch] dust, and ice crystals,
 1662.0 ms  [ 59 ch] causing them to scatter and combine into a range of colors.
 1933.1 ms  [ 63 ch] This process creates the familiar blue color we see in the sky.
```

출력에서 볼 것:

- 첫 phrase는 첫 token 뒤 **150 ms**에 나왔다 — 50자 ≈ 11 token × ITL 13 ms. 이것이 TTFA에 그대로 더해진다. 쉼표에서 자르지 않고 문장 끝만 기다렸다면 첫 phrase는 두 번째 줄 끝(318 ms)까지 밀렸다.
- 이 실행의 TTFT 30.7 ms는 slot을 비웠는데도 4.3절의 RAM prompt cache가 같은 프롬프트를 찾아 줬기 때문이다(로그 `found better prompt ... f_sim = 1.000`). 6.2의 151~245 ms가 cache 없는 값이다.
- 세 번째 phrase는 133자다. 쉼표가 없는 긴 문장은 TTS 입력이 길어진다. 앞 phrase를 읽는 동안 다음 phrase가 준비되면(파이프라이닝) 문제없지만, 첫 phrase가 길면 그대로 지연이 된다. 그래서 **첫 phrase만 더 공격적으로**(예: 단어 6개 이상이면 아무 경계에서나) 자르는 변형을 쓰기도 한다.

### 6.4 함정 — 스트리밍에서 깨지는 것들

| 함정 | 증상 | 대처 |
|---|---|---|
| 한 token이 UTF-8 글자의 일부 | 한국어·이모지가 깨진 글자로 TTS에 들어감 | 서버가 불완전 바이트는 붙잡아 둔다(README: `n_predict`가 multibyte 때문에 조금 넘을 수 있다). 자체 runtime이면 디토크나이저에서 같은 처리를 해야 한다 |
| 약어·숫자의 점 | "Dr. Kim", "3.5 km"에서 문장으로 잘림 | 경계 규칙에 "점 뒤 공백 + 대문자/줄끝", 숫자 사이 점 제외 |
| markdown·목록 기호 | TTS가 "별표 별표"라고 읽음 | system prompt로 금지 + 출력 필터 |
| 마지막 조각 누락 | 문장부호 없이 끝난 꼬리를 안 읽음 | 스트림 끝에서 buffer flush (예제 10의 마지막 `yield`) |
| `finish_reason = length` 무시 | 잘린 문장을 끝까지 읽음 | "말이 끊겼다" 처리 (예: "자세한 건 폰에서 볼게요") |

---

## 7. 취소와 barge-in — 사용자가 말을 끊으면

### 7.1 직관 — Abort 명령

사용자가 답을 듣다가 말한다. "아니, 그거 말고…". 스택 전체에서 일어나야 할 일:

1. ① front-end: VAD가 사용자 음성을 감지(TTS 출력의 echo를 AEC로 지운 뒤). 말 끊음으로 확정하는 데 보통 수백 ms(오탐 방지 대기 — 값은 제품마다 다르다).
2. ② orchestrator: **TTS 재생을 즉시 멈춘다(로컬, 가장 먼저)**, LLM 요청을 취소한다, 대화 이력에는 **실제로 말한 부분까지만** assistant 메시지로 남긴다.
3. ③ LLM service: 생성을 멈추고 slot을 비워 다음 요청을 받는다.

펌웨어로 치면 NVMe Abort 명령이다. 핵심 질문도 같다: "abort를 받고 진행 중인 명령이 실제로 멈추기까지 얼마나 걸리나, 그 사이 자원은 언제 풀리나."

`llama-server`에서는 **HTTP 연결을 끊는 것이 취소 신호**다. 소스를 따라가면: HTTP 쪽 스트리밍 루프가 연결이 닫힌 것(`should_stop()`)을 보고 응답 reader를 정리하며 → `server_response_reader::stop()`이 CANCEL task를 만들어 **task 큐의 맨 앞**에 넣고(`queue_tasks.post(..., true)`, 주석: "push to beginning of the queue, so it has highest priority") → main loop가 다음 반복에서 그 slot을 release한다. decode 한 step은 중간에 끊지 않으므로, 이미 시작된 step은 끝난다.

### 7.2 코드로 확인 — 끊고 나서 몇 ms 만에 slot이 비나 (예제 11)

무엇을 확인하는 코드인지: 1000 token짜리 생성을 시작하고 client가 40번째 token을 받은 순간 TCP 연결을 끊는다. 그 뒤 `GET /slots`를 5 ms 간격으로 polling해서 server가 몇 token을 더 만들었는지(`n_decoded`)와 slot이 idle이 되기까지의 시간을 잰다.

```python
# 예제: barge-in — 스트리밍 도중 연결을 끊으면 server는 얼마나 빨리 멈추고 slot을 비우나
import time
from sse import stream_chat, post
PORT = 23917
def slot0():
    return post(PORT, "/slots", method="GET")[1][0]

msgs = [{"role": "user", "content": "Tell me a very long story about a lighthouse keeper."}]
for trial in range(3):
    conns, n = [], 0
    for t, piece, ev in stream_chat(PORT, msgs, conn_out=conns, max_tokens=1000,
                                    ignore_eos=True, temperature=0.7, seed=trial):
        n += bool(piece)
        if n == 40:                                    # 40번째 token에서 사용자가 말을 끊음
            s = slot0()["next_token"][0]["n_decoded"]
            t_cut = time.perf_counter()
            conns[0].sock.shutdown(2); conns[0].close()  # = TCP 연결 끊기 (취소 신호)
            break
    last = s
    while True:                                       # slot이 idle이 될 때까지 5 ms 간격 polling
        st = slot0()
        if not st["is_processing"]: break
        last = st["next_token"][0]["n_decoded"]; time.sleep(0.005)
    dt = 1000 * (time.perf_counter() - t_cut)
    print(f"trial {trial}: client got 40, server had decoded {s} at cut, "
          f"last seen {last}, slot idle after {dt:.1f} ms")
```

```text
trial 0: client got 40, server had decoded 40 at cut, last seen 41, slot idle after 29.1 ms
trial 1: client got 40, server had decoded 40 at cut, last seen 41, slot idle after 45.4 ms
trial 2: client got 40, server had decoded 40 at cut, last seen 42, slot idle after 33.9 ms
```

출력에서 볼 것:

- 끊는 순간 server의 `n_decoded`(40)가 client가 받은 수(40)와 같다 — **server가 client보다 앞서 달리지 않는다**(token마다 바로 보낸다).
- 끊은 뒤 server는 **1~2 token을 더 만들고** 멈췄다. decode 한 step(약 13 ms) 단위로만 멈출 수 있기 때문이다.
- slot이 idle이 되기까지 **29~45 ms**(이 값에는 5 ms polling 간격과 `/slots` 요청 자체의 시간이 포함된다). 기계 부하가 더 심할 때 같은 실험을 두 번 더 돌리면 38~137 ms, 80~208 ms였다 — 취소 반응도 CPU 경합에 따라 늘어난다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 280">
<text x="20.0" y="20.0" font-size="12" text-anchor="start">barge-in 한 번의 시간표 (가로 축 ms, 0 = 사용자가 말하기 시작)</text><line x1="130.0" y1="34.0" x2="130.0" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="130.0" y="266.0" font-size="11" text-anchor="middle">0</text><line x1="216.7" y1="34.0" x2="216.7" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="216.7" y="266.0" font-size="11" text-anchor="middle">200</text><line x1="303.3" y1="34.0" x2="303.3" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="303.3" y="266.0" font-size="11" text-anchor="middle">400</text><line x1="390.0" y1="34.0" x2="390.0" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="390.0" y="266.0" font-size="11" text-anchor="middle">600</text><line x1="476.7" y1="34.0" x2="476.7" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="476.7" y="266.0" font-size="11" text-anchor="middle">800</text><line x1="563.3" y1="34.0" x2="563.3" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="563.3" y="266.0" font-size="11" text-anchor="middle">1000</text><line x1="650.0" y1="34.0" x2="650.0" y2="250.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="650.0" y="266.0" font-size="11" text-anchor="middle">1200</text><text x="122.0" y="58.0" font-size="12" text-anchor="end">사용자 음성</text><text x="122.0" y="100.0" font-size="12" text-anchor="end">VAD 판정</text><text x="122.0" y="142.0" font-size="12" text-anchor="end">TTS 재생</text><text x="122.0" y="184.0" font-size="12" text-anchor="end">LLM decode</text><text x="122.0" y="226.0" font-size="12" text-anchor="end">slot 상태</text><rect x="130.0" y="44.0" width="520.0" height="22.0" fill="#3f9a6b" fill-opacity="0.6"/><text x="390.0" y="59.0" font-size="11" text-anchor="middle">"아니, 그거 말고…"</text><rect x="130.0" y="86.0" width="86.7" height="22.0" fill="#888" fill-opacity="0.4"/><text x="173.3" y="101.0" font-size="11" text-anchor="middle">확인 대기</text><line x1="216.7" y1="40.0" x2="216.7" y2="250.0" stroke="#d0564a" stroke-width="2"/><text x="220.7" y="121.0" font-size="11" text-anchor="start">← 200 ms: 말 끊음 확정 (가정)</text><rect x="130.0" y="128.0" width="86.7" height="22.0" fill="#e08a3c" fill-opacity="0.7"/><rect x="216.7" y="128.0" width="13.0" height="22.0" fill="#e08a3c" fill-opacity="0.3"/><text x="234.0" y="144.0" font-size="11" text-anchor="start">fade-out ~30 ms, 즉시 멈춤 (로컬)</text><rect x="130.0" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="140.8" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="151.7" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="162.5" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="173.3" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="184.2" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="195.0" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="205.8" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="216.7" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.8"/><rect x="227.5" y="170.0" width="8.7" height="22.0" fill="#4a7bd0" fill-opacity="0.35"/><text x="242.7" y="186.0" font-size="11" text-anchor="start">취소 후 1~2 token 더 (실측)</text><rect x="130.0" y="212.0" width="86.7" height="22.0" fill="#4a7bd0" fill-opacity="0.25"/><text x="173.3" y="227.0" font-size="11" text-anchor="middle">busy</text><rect x="216.7" y="212.0" width="59.4" height="22.0" fill="#d0564a" fill-opacity="0.35"/><text x="279.5" y="227.0" font-size="11" text-anchor="start">idle까지 29~208 ms (실측 3세트, polling 포함)</text>
</svg>
```

그림 5 — barge-in 한 번의 시간표. VAD 확정 시점(200 ms)은 가정이고, LLM 쪽 "1~2 token 더"와 "idle까지 29~208 ms"는 예제 11을 부하가 다른 세 시점에 돌린 실측 범위다. TTS를 먼저 멈추는 것이 사용자 체감에 가장 중요하다 — LLM 취소는 그 뒤에 일어나도 사용자는 모른다.

### 7.3 취소 후 대화 이력 — 무엇을 남기나

이 부분을 틀리면 다음 턴에서 모델이 이상하게 군다.

| 선택 | 결과 |
|---|---|
| 생성된 답 전체를 이력에 남김 | 사용자가 **듣지 못한** 내용을 모델은 "말했다"고 믿는다 ("아까 말씀드린 것처럼…") |
| 답을 통째로 버림 | 사용자가 들은 부분과 모순되는 답이 나올 수 있다 |
| **실제로 재생된 phrase까지만 남김** (+ "[interrupted]" 표시) | 권장. TTS가 어디까지 읽었는지 orchestrator가 알고 있어야 한다 |

KV 관점: slot에는 생성된 41~42 token이 들어 있는데 이력에는 그보다 짧은 답이 들어간다. 다음 턴 프롬프트는 **잘린 지점부터 prefix가 달라진다.** 3.4절의 규칙대로 그 지점 이후만 다시 계산하므로 비용은 작다(수십 token).

### 7.4 취소를 못 받는 구간

- **prefill 도중**: 긴 프롬프트의 prefill(수백 ms~수 초)은 batch(`-ub 512`) 단위로 진행된다. 취소는 batch 경계에서야 반영된다. 기기에서 prefill이 길면 prefill도 작게 쪼개야 취소 반응이 빨라진다(J2의 chunking과 같은 원리).
- **NPU offload**: 그래프를 통째로 NPU에 넘기는 runtime(QNN/Genie 등)은 한 번 제출한 실행을 중간에 끊을 수 없는 경우가 있다. 그때는 "결과를 버리기"만 가능하고 전력은 계속 쓴다. 어느 단위로 끊기는지 runtime 문서로 확인해야 한다(헤지 — runtime마다 다르다).
- **연결이 끊긴 걸 늦게 아는 IPC**: TCP는 보통 즉시 알지만, 중간에 proxy·buffer가 있으면 늦는다. 명시적 `cancel(request_id)` 메서드를 계약에 두는 것이 안전하다.

---

## 8. 동시성 — 요청 여러 개, 그리고 다른 워크로드와 함께 살기

### 8.1 slot 2개 vs 1개 — 처리량과 지연의 거래

decode는 memory-bound다(D5). token 1개를 만들 때나 2개(서로 다른 요청)를 만들 때나 가중치는 한 번만 읽으면 되므로, 두 요청을 한 batch로 묶으면 **총 처리량은 오르고 각 요청의 속도는 내려간다**(D5 6.1의 batching). 사용자 입장에서 무엇이 좋은지는 워크로드에 달렸다.

### 8.2 코드로 확인 — 두 요청이 동시에 올 때 (예제 12)

무엇을 확인하는 코드인지: 같은 모델을 `-np 1`과 `-np 2`로 각각 띄우고, 96 token짜리 요청 두 개를 동시에 보내 각 요청의 TTFT·완료 시각과 총 처리량을 잰다(`--no-cache-prompt`로 cache 효과는 뺐다).

```python
# 예제: 요청 2개가 동시에 올 때 — slot 1개(직렬) vs slot 2개(continuous batching)
import threading, time
from srv import start, stop
from sse import stream_chat
def one(port, i, out):
    ttft = None
    for t, piece, ev in stream_chat(port, [{"role": "user", "content": f"Story #{i} about a robot."}],
                                    max_tokens=96, ignore_eos=True, temperature=0):
        if piece and ttft is None: ttft = t
    out[i] = (ttft, t)
for np_ in (1, 2):
    p, _ = start("qwen2.5-0.5b-Q4_K_M.gguf", 23919, f"par{np_}.log",
                 "-c", "4096", "-np", str(np_), "-ngl", "0", "-t", "4", "--no-cache-prompt")
    for rep in range(2):
        out = {}; t0 = time.perf_counter()
        th = [threading.Thread(target=one, args=(23919, i, out)) for i in (0, 1)]
        [x.start() for x in th]; [x.join() for x in th]
        wall = time.perf_counter() - t0
        s = " | ".join(f"req{i}: TTFT {1000*a:6.0f} ms, done {1000*b:6.0f} ms" for i, (a, b) in sorted(out.items()))
        print(f"np={np_} rep{rep}: {s} | total {2*96/wall:5.1f} tok/s")
    stop(p)
```

```text
np=1 rep0: req0: TTFT    401 ms, done   3702 ms | req1: TTFT   4242 ms, done   7929 ms | total  24.2 tok/s
np=1 rep1: req0: TTFT    431 ms, done   2843 ms | req1: TTFT   3238 ms, done   5160 ms | total  37.2 tok/s
np=2 rep0: req0: TTFT    688 ms, done   4769 ms | req1: TTFT    315 ms, done   4755 ms | total  40.3 tok/s
np=2 rep1: req0: TTFT    585 ms, done   4871 ms | req1: TTFT    586 ms, done   4872 ms | total  39.4 tok/s
```

출력에서 볼 것 (rep1로 손계산):

```
np=1  req0 decode: (2843 − 431) / 95 간격 = 25.4 ms/token     req1은 req0이 끝날 때까지 TTFT 3238 ms
np=2  각 요청   : (4871 − 585) / 95 간격 = 45.1 ms/token     → step 하나가 token 2개 → 22.6 ms/token 상당
```

- slot 2개는 **두 요청을 공평하게** 다룬다: 둘 다 TTFT 0.6 s. slot 1개면 두 번째 요청이 3.2 s를 기다린다.
- 대신 **각 요청의 decode는 1.8배 느려진다**(25 → 45 ms/token). 총 처리량은 37 → 39~40 tok/s로 조금만 올랐다. 이 작은 모델·이 CPU에서는 batch 2의 step이 batch 1 step보다 꽤 비싸서(45 vs 25 ms) 대역폭 이득이 작다. 큰 모델일수록, 대역폭이 좁을수록 batching 이득이 커진다(D5 6.1).
- 바쁜 기계라 rep0의 np=1(24 tok/s)처럼 한 회가 크게 흔들린다. 결론은 방향만 가져간다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 255">
<text x="20.0" y="20.0" font-size="12" text-anchor="start">요청 2개 동시 도착 — 회색: 대기+prefill(TTFT까지), 색: decode 96 token (rep1 실측)</text><line x1="110.0" y1="34.0" x2="110.0" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="110.0" y="215.0" font-size="11" text-anchor="middle">0 s</text><line x1="196.7" y1="34.0" x2="196.7" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="196.7" y="215.0" font-size="11" text-anchor="middle">1 s</text><line x1="283.3" y1="34.0" x2="283.3" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="283.3" y="215.0" font-size="11" text-anchor="middle">2 s</text><line x1="370.0" y1="34.0" x2="370.0" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="370.0" y="215.0" font-size="11" text-anchor="middle">3 s</text><line x1="456.7" y1="34.0" x2="456.7" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="456.7" y="215.0" font-size="11" text-anchor="middle">4 s</text><line x1="543.3" y1="34.0" x2="543.3" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="543.3" y="215.0" font-size="11" text-anchor="middle">5 s</text><line x1="630.0" y1="34.0" x2="630.0" y2="200.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="630.0" y="215.0" font-size="11" text-anchor="middle">6 s</text><text x="102.0" y="59.0" font-size="12" text-anchor="end">np=1 req0</text><rect x="110.0" y="42.0" width="37.4" height="24.0" fill="#888" fill-opacity="0.45"/><rect x="147.4" y="42.0" width="209.0" height="24.0" fill="#e08a3c" fill-opacity="0.8"/><text x="362.4" y="59.0" font-size="11" text-anchor="start">2.8 s</text><text x="102.0" y="97.0" font-size="12" text-anchor="end">np=1 req1</text><rect x="110.0" y="80.0" width="280.6" height="24.0" fill="#888" fill-opacity="0.45"/><rect x="390.6" y="80.0" width="166.6" height="24.0" fill="#e08a3c" fill-opacity="0.8"/><text x="563.2" y="97.0" font-size="11" text-anchor="start">5.2 s</text><text x="102.0" y="147.0" font-size="12" text-anchor="end">np=2 req0</text><rect x="110.0" y="130.0" width="50.7" height="24.0" fill="#888" fill-opacity="0.45"/><rect x="160.7" y="130.0" width="371.5" height="24.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="538.2" y="147.0" font-size="11" text-anchor="start">4.9 s</text><text x="102.0" y="185.0" font-size="12" text-anchor="end">np=2 req1</text><rect x="110.0" y="168.0" width="50.8" height="24.0" fill="#888" fill-opacity="0.45"/><rect x="160.8" y="168.0" width="371.5" height="24.0" fill="#4a7bd0" fill-opacity="0.8"/><text x="538.2" y="185.0" font-size="11" text-anchor="start">4.9 s</text><text x="370.0" y="240.0" font-size="12" text-anchor="middle">np=1: 처리량 37 tok/s, req1 TTFT 3.2 s · np=2: 처리량 39 tok/s, 둘 다 TTFT 0.6 s, 둘 다 4.9 s에 끝</text>
</svg>
```

그림 6 — 예제 12 rep1의 시간표. np=1은 직렬(둘째 요청이 오래 기다림), np=2는 동시 진행(둘 다 늦게 끝남).

음성 비서에 주는 교훈: **사용자 음성 요청과 배경 작업(알림 요약 등)이 같은 batch에 섞이면 음성 응답의 ITL이 1.8배가 된다.** 이 빌드의 요청 필드에서 우선순위 항목은 찾지 못했다. 그래서 우선순위는 서버 밖(orchestrator)에서 구현한다: 음성 턴이 시작되면 배경 요청을 취소(7절)하거나 시작하지 않고, 음성 세션이 끝난 뒤 배경 작업을 재개한다. 펌웨어의 "host I/O 중에는 background GC를 멈춘다"와 같은 규칙이다.

### 8.3 LLM 스레드 vs 오디오 파이프라인

웨어러블에서 같은 CPU 클러스터를 쓰는 다른 일:

| 워크로드 | 성격 | 지켜야 할 것 |
|---|---|---|
| 오디오 캡처 → AEC → VAD → (wake word) | 10~20 ms 주기, hard deadline | 놓치면 소리가 끊기거나 barge-in 감지 실패 |
| TTS 합성·재생 | 버퍼 underrun 금지 | 6.2의 ITL 꼬리를 덮는 buffer |
| LLM decode | 수십 ms 단위 throughput 작업 | TTFT·ITL 목표 (soft) |
| 센서·BLE | 주기적, 짧음 | 지연 허용폭 다양 |

원칙(J2 실시간 설계의 적용):

- **우선순위**: 오디오 경로가 가장 높고, LLM 스레드는 그 아래다. LLM이 아무리 급해도 오디오 스레드를 선점하면 안 된다. Android에서는 오디오 경로에 높은 우선순위(실시간 클래스) 스레드가 쓰이고 일반 앱 스레드는 그보다 낮다(정확한 정책은 OS 버전·벤더 HAL마다 다르다 — 헤지). `llama-server`의 `--prio`(0 normal ~ 3 realtime)는 **올리는 방향으로 쓰지 않는다** — LLM에 realtime 우선순위를 주면 오디오가 굶는다.
- **스레드 수**: `-t`를 core 수만큼 주는 것이 최선이 아니다. K2에서 이 Mac의 0.5B 모델 tg는 **2 스레드가 가장 빨랐고(96 t/s), 4 스레드는 동기화(barrier spin) 때문에 오히려 느렸다(80 t/s).** 작은 모델의 decode는 op가 작아서 스레드를 늘리면 barrier 대기만 는다. 게다가 spin하는 스레드는 **CPU를 쥔 채로 기다리므로** 같은 core의 오디오 스레드를 방해한다. 기기에서는 "decode 속도가 포화되는 최소 스레드 수"를 재서 그 값으로 고정하고, 남는 core는 오디오에 양보한다.
- **affinity**: big.LITTLE SoC라면 LLM은 big core 일부에, 오디오는 별도 core에 고정(`--cpu-mask`/`--cpu-range`)하는 것을 시험한다. 메모리 대역폭은 core를 나눠도 공유되므로, LLM decode가 DRAM을 포화시키면 오디오 DSP의 DMA 지연도 늘 수 있다 — 측정으로 확인.
- **prefill 쪼개기**: 긴 prefill은 큰 batch로 CPU 전체를 수백 ms 붙잡는다. `-ub`(physical batch)를 줄이면 한 번에 붙잡는 시간이 줄어 오디오·취소 반응이 좋아지지만 prefill 처리량은 떨어진다.

6.2에서 본 것처럼, 같은 스크립트의 ITL p99가 기계 부하에 따라 47 ms → 325 ms로 7배 변했다. **다른 일과 CPU를 다투는 LLM의 꼬리 지연은 LLM 혼자 잰 벤치마크로 예측할 수 없다.** 실제 오디오 파이프라인을 함께 돌린 상태로 재야 한다(K2, J6 HIL).

### 8.4 전력·열과의 상호작용 (K4 요약)

- decode는 token마다 DRAM을 가득 읽는다 → DRAM·memory controller 전력이 크다. 대역폭이 곧 전력이다(D7).
- 긴 대화에서 SoC가 열 한계에 닿으면 governor가 클럭을 내리고 tok/s가 계단처럼 떨어진다. K4 5.3에서처럼 **decode 속도에 상한을 걸어**(사람이 듣는 속도 이상은 필요 없다 — 영어 말하기 속도 약 150 단어/분은 초당 3~4 token 수준이라, TTFT만 짧다면 decode는 그 몇 배면 충분하다) 열 예산 안에 머무는 것이 burst 후 throttling보다 사용자 경험이 낫다.
- 음성 비서의 decode 목표는 "최대 tok/s"가 아니라 **"TTS 소비 속도보다 조금 빠르게, 그리고 꼬리가 짧게"**다.

---

## 9. 메모리 압박 — OS가 메모리를 돌려 달라고 할 때

### 9.1 직관 — dirty와 clean

OS가 메모리를 회수할 때 page는 두 종류로 나뉜다.

- **clean page**: 디스크(flash)에 똑같은 내용이 있는 page. mmap된 모델 파일의 page가 대표적이다. OS는 그냥 버리면 된다. 다시 필요하면 page fault로 flash에서 읽는다.
- **dirty(anonymous) page**: 프로세스가 만들거나 고친 내용. malloc한 buffer, KV-cache, repack된 가중치 복사본. 버릴 수 없다. swap(Android는 보통 zram: RAM 안에서 압축)으로 보내거나, **프로세스를 죽여야** 회수된다.

펌웨어 비유: FTL의 L2P 테이블 중 NAND에 최신본이 있는 부분(clean)은 DRAM에서 그냥 버리고 다시 읽으면 되지만, 아직 flush 안 된 변경분(dirty)은 버리면 데이터가 사라진다.

### 9.2 코드로 확인 — 로딩 방식에 따른 dirty/clean (예제 13)

무엇을 확인하는 코드인지: 같은 모델을 세 가지 로딩 방식으로 띄우고, 요청 1번 뒤에 macOS `footprint` 도구로 dirty 메모리(phys_footprint)와 mmap된 파일의 clean 메모리를, `ps`로 RSS를 읽는다.

```python
# 예제: 같은 모델, 로딩 방식만 바꿔서 메모리를 "dirty(회수 불가)"와 "clean(버릴 수 있음)"으로 나눠 본다
import re, subprocess
from srv import start, stop
from sse import post
def mem(pid):
    fp = subprocess.run(["footprint", "-p", str(pid)], capture_output=True, text=True).stdout
    rss = int(subprocess.run(["ps", "-o", "rss=", "-p", str(pid)], capture_output=True, text=True).stdout) / 1024
    dirty = re.search(r"phys_footprint: (\d+) MB", fp).group(1)
    clean = re.search(r"(\S+ \S+)\s+\S+ \S+\s+\d+\s+mapped file", fp).group(1)
    return f"RSS {rss:5.0f} MiB | dirty(footprint) {dirty:>4} MB | mapped-file clean {clean:>7}"
CFG = {"mmap + repack (default)": [],
       "mmap, --no-repack":       ["--no-repack"],
       "--load-mode none (read)": ["--load-mode", "none"]}
for name, extra in CFG.items():
    p, ms = start("qwen2.5-0.5b-Q4_K_M.gguf", 23920, "rss.log",
                  "-c", "4096", "-np", "1", "-ngl", "0", "-t", "4", *extra)
    post(23920, "/v1/chat/completions", {"messages": [{"role": "user", "content": "Hi"}], "max_tokens": 16})
    print(f"{name:24s} load {ms:5.0f} ms | {mem(p.pid)}")
    stop(p)
```

```text
mmap + repack (default)  load   929 ms | RSS   770 MiB | dirty(footprint)  324 MB | mapped-file clean  397 MB
mmap, --no-repack        load   830 ms | RSS   559 MiB | dirty(footprint)  113 MB | mapped-file clean  397 MB
--load-mode none (read)  load  1232 ms | RSS   841 MiB | dirty(footprint)  769 MB | mapped-file clean   23 MB
```

출력에서 볼 것:

- **RSS 하나만 보면 속는다.** 기본 설정의 RSS 770 MiB 중 397 MB는 clean(파일 397,807,616 B 전체)이라 OS가 언제든 가져갈 수 있다. 진짜 "회수 불가" 양은 dirty 324 MB다. Android의 메모리 회계(PSS, 그리고 LMK가 보는 값)도 이 구분을 한다 — 어느 숫자를 보는지 확인하고 말해야 한다.
- **repack은 dirty를 약 211 MB 늘린다** (324 − 113). 로그의 `CPU_REPACK 208 MiB`와 맞는다. repack은 CPU 행렬곱을 빠르게 하려고 가중치를 SIMD 친화 배치로 복사한 것이라, 그 복사본은 파일에 없는 dirty page다. 속도 이득(F3)과 회수 가능성 사이의 거래다. 이 노트에서 `--no-repack`의 속도 손실은 재지 않았다.
- **`--load-mode none`(mmap 없이 읽기)은 모델 전체가 dirty**가 된다(769 MB; 다른 실행에서는 617 MB — 할당자 상태에 따라 흔들린다). 메모리 압박 시 회수할 것이 없어 **프로세스 kill 후보 1순위**가 된다.

```svg
<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 680 268">
<text x="20.0" y="20.0" font-size="12" text-anchor="start">llama-server 메모리 구성 (macOS footprint, MB) — 같은 모델 Q4_K_M 380 MiB, ctx 4096</text><line x1="190.0" y1="40.0" x2="190.0" y2="190.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="190.0" y="205.0" font-size="11" text-anchor="middle">0</text><line x1="336.7" y1="40.0" x2="336.7" y2="190.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="336.7" y="205.0" font-size="11" text-anchor="middle">300</text><line x1="483.3" y1="40.0" x2="483.3" y2="190.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="483.3" y="205.0" font-size="11" text-anchor="middle">600</text><line x1="630.0" y1="40.0" x2="630.0" y2="190.0" stroke="#888" stroke-width="0.5" stroke-dasharray="2 3"/><text x="630.0" y="205.0" font-size="11" text-anchor="middle">900</text><text x="182.0" y="70.0" font-size="12" text-anchor="end">mmap + repack (기본)</text><rect x="190.0" y="50.0" width="158.4" height="28.0" fill="#d0564a" fill-opacity="0.75"/><rect x="348.4" y="50.0" width="194.1" height="28.0" fill="#4a7bd0" fill-opacity="0.55"/><text x="269.2" y="69.0" font-size="11" text-anchor="middle">324</text><text x="445.4" y="69.0" font-size="11" text-anchor="middle">397</text><text x="182.0" y="116.0" font-size="12" text-anchor="end">mmap + --no-repack</text><rect x="190.0" y="96.0" width="55.2" height="28.0" fill="#d0564a" fill-opacity="0.75"/><rect x="245.2" y="96.0" width="194.1" height="28.0" fill="#4a7bd0" fill-opacity="0.55"/><text x="217.6" y="115.0" font-size="11" text-anchor="middle">113</text><text x="342.3" y="115.0" font-size="11" text-anchor="middle">397</text><text x="182.0" y="162.0" font-size="12" text-anchor="end">--load-mode none</text><rect x="190.0" y="142.0" width="376.0" height="28.0" fill="#d0564a" fill-opacity="0.75"/><rect x="566.0" y="142.0" width="11.2" height="28.0" fill="#4a7bd0" fill-opacity="0.55"/><text x="378.0" y="161.0" font-size="11" text-anchor="middle">769</text><rect x="200.0" y="222.0" width="14.0" height="12.0" fill="#d0564a" fill-opacity="0.75"/><text x="220.0" y="233.0" font-size="12" text-anchor="start">dirty — 회수하려면 swap/압축하거나 프로세스를 죽여야 함</text><rect x="200.0" y="242.0" width="14.0" height="12.0" fill="#4a7bd0" fill-opacity="0.55"/><text x="220.0" y="253.0" font-size="12" text-anchor="start">clean (mmap된 파일) — 커널이 그냥 버리고 나중에 flash에서 다시 읽음</text>
</svg>
```

그림 7 — 예제 13의 메모리 구성. 빨강(dirty)은 OS가 프로세스를 죽여야만 회수할 수 있고, 파랑(clean, mmap된 파일)은 OS가 그냥 버릴 수 있다.

### 9.3 clean이라고 공짜는 아니다 — refault 폭주

mmap된 가중치를 OS가 버렸다고 하자. decode는 **token마다 가중치 전부를 읽는다**(B8 1.5). 그러면 token마다 버려진 page를 flash에서 다시 읽어야 한다.

```
가정: 가중치 380 MiB 중 절반이 회수됨, flash 순차 읽기 1 GB/s (UFS급 가정; eMMC는 더 느림)
token당 추가 I/O = 190 MiB ≈ 200 MB → 200 ms/token
평소 ITL 13 ms → 213 ms/token   (16배 느려짐, 그리고 그 사이 또 회수되면 반복)
```

말로 하면: 메모리 압박 아래에서 mmap 모델은 **"flash 대역폭으로 도는 LLM"**으로 추락한다. 이것이 thrashing이다. 그래서 선택지가 생긴다.

| 방식 | 압박 시 | 비용 |
|---|---|---|
| mmap (기본) | OS가 가중치를 버림 → refault로 극단적 감속, 그러나 안 죽음 | 성능 절벽 |
| mmap + mlock | 가중치를 RAM에 고정 → 성능 유지, 대신 다른 앱이 죽음 | 시스템 전체 메모리 압박 가중 |
| 서비스가 먼저 내려놓기 | 압박 신호를 받으면 스스로 unload (10절) | 다음 요청 cold start |

### 9.4 OS의 신호와 서비스의 대응 사다리 (개념 — 헤지)

- **Android**: 커널의 PSI(pressure stall information)를 보는 user-space `lmkd`가 압박이 심해지면 `oom_score_adj`가 높은(덜 중요한) 프로세스부터 죽인다. foreground/visible/bound-by-foreground 서비스는 점수가 낮아 늦게 죽는다. 앱은 `ComponentCallbacks2.onTrimMemory(level)`로 미리 알림을 받는다(최근 API 레벨에서 일부 level 상수가 deprecated되었다 — 타깃 API 문서를 확인할 것).
- **Embedded Linux**: cgroup v2의 `memory.high`(넘으면 회수 압박·감속)와 `memory.max`(넘으면 OOM), `/proc/pressure/memory`(PSI), OOM killer의 `oom_score_adj`.

서비스가 할 일은 신호의 세기에 따라 **값싼 것부터 내려놓는 사다리**를 갖는 것이다.

| 단계 | 신호(예) | 행동 | 잃는 것 |
|---|---|---|---|
| 1 | 약한 압박 | RAM prompt cache 비우기 (`--cache-ram` 영역) | 다른 세션의 prefix 재사용 |
| 2 | 중간 | idle slot KV를 flash에 저장 후 해제 (4.2) | 복원 수 ms~수십 ms |
| 3 | 강함 | ctx 축소 (KV 재할당) / compute buffer 축소 | 긴 대화 불가 |
| 4 | 위급 | 모델 unload (10절) — **죽기 전에 스스로** | 다음 요청 cold start |
| 5 | OS가 kill | 프로세스 재시작 | 모든 세션 상태 (flash에 저장 안 했다면) |

5단계까지 가면 서비스는 아무 상태도 저장하지 못한 채 죽는다. 그래서 2단계(세션 KV를 flash로)를 **주기적으로** 해 두는 것이 SSD의 "주기적 metadata flush"와 같은 역할을 한다.

---

## 10. 계속 올려 둘까, 내릴까 — idle unload

### 10.1 코드로 확인 — `--sleep-idle-seconds` (예제 14)

이 빌드에는 일정 시간 요청이 없으면 모델과 KV를 메모리에서 내리고, 다음 요청에 자동으로 다시 올리는 기능이 있다(README "Sleeping on Idle", `GET /props`의 `is_sleeping`).

무엇을 확인하는 코드인지: idle 3초 후 sleep하도록 띄우고, warm 턴 → 5초 대기 → sleep 상태와 메모리 확인 → 깨우는 턴 → 다음 턴의 TTFT와 footprint를 잰다.

```python
# 예제: idle 후 모델 내리기(sleep) vs 계속 올려 두기 — 깨어날 때 비용과 쉬는 동안 메모리
import time, subprocess, re
from srv import start, stop
from sse import post, stream_chat
PORT = 23921
def foot(pid):
    fp = subprocess.run(["footprint", "-p", str(pid)], capture_output=True, text=True).stdout
    return re.search(r"phys_footprint: (\d+ \S+)", fp).group(1)
def turn():
    t0 = time.perf_counter(); ttft = None
    for t, piece, ev in stream_chat(PORT, [{"role": "user", "content": "What time zone is Seoul in?"}],
                                    max_tokens=24, temperature=0):
        if piece and ttft is None: ttft = t
    return 1000 * ttft
p, ms = start("qwen2.5-0.5b-Q4_K_M.gguf", PORT, "idle.log", "-c", "4096", "-np", "1",
              "-ngl", "0", "-t", "4", "--sleep-idle-seconds", "3")
print(f"server ready {ms:.0f} ms")
print(f"warm turn  : TTFT {turn():6.0f} ms | footprint {foot(p.pid)}")
print(f"warm turn  : TTFT {turn():6.0f} ms | footprint {foot(p.pid)}")
time.sleep(5)
props = post(PORT, "/props", method="GET")[1]
print(f"after 5 s idle: is_sleeping={props.get('is_sleeping')} | footprint {foot(p.pid)}")
print(f"wake turn  : TTFT {turn():6.0f} ms | footprint {foot(p.pid)}")
print(f"next turn  : TTFT {turn():6.0f} ms")
stop(p)
```

```text
server ready 680 ms
warm turn  : TTFT    348 ms | footprint 340 MB
warm turn  : TTFT     29 ms | footprint 340 MB
after 5 s idle: is_sleeping=True | footprint 79 MB
wake turn  : TTFT   1289 ms | footprint 345 MB
next turn  : TTFT     27 ms
```

서버 로그에는 `start_loop: entering sleeping state` → `handle_sleep: server is entering sleeping state`가 찍힌다.

출력에서 볼 것:

- sleep 중 dirty 메모리 **340 → 79 MB**(약 260 MB 반환). 남은 79 MB는 프로세스·HTTP 서버·라이브러리 자체다.
- 깨우는 턴의 TTFT **1289 ms**(다른 실행에서는 1874 ms) vs warm 29~348 ms. 깨우기 = 모델 재로드 + KV 재할당 + **prefix cache도 사라졌으므로 프롬프트 전체 prefill**. 두 번째 warm 턴(29 ms)이 빠른 것은 같은 프롬프트라 prefix가 재사용됐기 때문이다.
- `/health`, `/props`는 idle 타이머를 리셋하지 않는다(README). 상태 polling이 모델을 계속 깨워 두는 실수는 없다.

### 10.2 손계산 — 에너지로 보면 무엇이 이기나

```
reload 비용 (가정: 로딩 중 SoC 2 W, 1.3 s)  E_load ≈ 2 W × 1.3 s = 2.6 J
계속 올려 둘 때 추가 전력 P_keep = ?
```

여기서 흔한 오해가 있다. **DRAM refresh 전력은 보통 "page가 사용 중이냐"와 무관하다** — 칩 전체가 refresh된다. 그래서 260 MB를 비워도 PASR(partial array self-refresh)처럼 비운 영역의 전원을 실제로 끄는 기능을 OS·SoC가 쓰지 않으면 **전력은 거의 그대로**다(SoC마다 다름 — 헤지). 즉:

- PASR 같은 기능이 없다면: 내려도 에너지 이득은 거의 0이고, 다시 올리는 2.6 J만 손해다. 내리는 이유는 **에너지가 아니라 메모리(다른 앱·page cache·카메라 같은 큰 사용자)**다.
- 그런 기능이 있어서 P_keep = 10 mW(가정)라면: 손익분기 idle 시간 = 2.6 J ÷ 0.01 W = **260 s**. 4분 넘게 안 쓸 것 같으면 내리는 게 이득.

### 10.3 정책 — 웨어러블 음성 비서의 예

| 상태 | 모델 | 이유 |
|---|---|---|
| 대화 중 + 마지막 턴 후 30~60 s | 올려 둠, 세션 KV 유지 | 후속 질문의 TTFT (cold면 +1~2 s) |
| 그 뒤 idle | 세션 KV는 flash에 저장, 모델은 압박이 없으면 유지 | 저장해 두면 압박 시 바로 내릴 수 있다 |
| 화면 꺼짐·충전 아님·장시간 idle | unload | 다른 작업에 메모리 양보 |
| wake word 검출 | **즉시 preload 시작** | ASR이 듣는 1~3 s 동안 로딩을 숨긴다 (2.4) |
| 메모리 압박 신호 | 9.4의 사다리 | 죽기 전에 스스로 |

이 정책을 결정하는 숫자 — cold/warm 로딩 시간, sleep 중 메모리, 깨우는 턴의 TTFT — 는 2절과 10.1에서 잰 것들이다. 기기에서 다시 재서 표를 채우는 것이 설계 리뷰의 첫 단계다.

---

## 11. API 계약 — orchestrator와 LLM service 사이의 약속

### 11.1 왜 계약부터 쓰나

J1 4절에서 `ml.h`를 먼저 정한 것과 같은 이유다. backend가 llama.cpp에서 Genie나 ExecuTorch로 바뀌어도, IPC가 HTTP에서 Binder로 바뀌어도 **orchestrator 코드가 안 바뀌게** 하려면 계약이 먼저다. 그리고 계약에는 행복한 경로보다 **실패 경로**가 더 많이 들어간다.

### 11.2 요청

| 필드 | 뜻 | 왜 필요한가 |
|---|---|---|
| `session_id` | 대화 세션 식별자 | slot·KV 재사용, 저장/복원의 키 (llama-server에서는 `id_slot`으로 흉내) |
| `messages` | system + 이력 + 이번 질문 (orchestrator가 예산 안으로 정리한 것) | 5절 정책의 결과 |
| `max_tokens` | 생성 상한 | 예산 계산의 일부, 폭주 방지 |
| `deadline_ms` | 이 시각까지 끝나지 않으면 포기 | 음성 턴은 "늦은 답 = 틀린 답" |
| `ttft_timeout_ms` | 첫 token이 이때까지 안 오면 포기 | cold start·큐 대기를 빨리 감지 → cloud fallback(L3) |
| `grammar` / `tools` | 출력 형식 제약 (GBNF, JSON schema) | tool 호출 인자를 파싱 가능한 형태로 |
| `sampling` | temperature, top_p, seed | 재현성 (단, 5.4절의 비결정성 주의) |
| `priority` | foreground 음성 / background | 8.2의 서버 밖 스케줄링 |

### 11.3 응답 이벤트 (스트림)

| 이벤트 | 내용 |
|---|---|
| `delta` | 새 텍스트 조각 (token 1개 이상) |
| `tool_call` | (tool 사용 시) 함수 이름 + 인자 |
| `done` | `finish_reason`(stop / length / cancelled), telemetry |
| `error` | 종류 + 메시지 (아래 표) — 스트림 도중에도 올 수 있다 |

### 11.4 오류 분류

| kind | 예 | orchestrator의 행동 |
|---|---|---|
| `overflow` | HTTP 400 `exceed_context_size_error` | 정책 버그. 이력을 더 줄여 1회 재시도, telemetry 경보 |
| `timeout` | TTFT/전체 deadline 초과 | 사용자에게 짧은 안내, cloud로 넘길지 결정 (L3) |
| `cancelled` | barge-in, 사용자가 화면 닫음 | 정상 경로. 재생된 부분까지만 이력에 저장 (7.3) |
| `unavailable` | 모델 로딩 중, 서비스 재시작 중, 메모리 부족으로 unload됨 | 재시도 정책 (backoff), 또는 fallback |
| `io` / `http` | 연결 끊김, 서버 crash | 서비스 재시작 감지, 세션 복원 |

### 11.5 telemetry — 매 턴 남길 숫자

| 필드 | 출처 | 쓰임 |
|---|---|---|
| `ttft_ms`, `total_ms` | client 시계 | 체감 지연 분포 (p50/p90/p99) |
| `prompt_n`, `cache_n` | server `timings` | prefix 재사용률 — 5절 정책·3.4절 버그 감지 |
| `predicted_n`, `predicted_per_second` | server `timings` | decode 속도, 열·부하 영향 |
| `finish_reason` | 마지막 chunk | `length` 비율이 높으면 max_tokens·ctx 설계 문제 |
| 퇴출·요약 횟수 | orchestrator | 5.4의 eager/hysteresis 비교 같은 정책 튜닝 |
| 모델 상태 전이 | service | cold start 빈도, unload 빈도 |

`llama-server --metrics`의 Prometheus 출력에도 누적값이 있다. 이 노트의 실험을 다 돌린 뒤의 실제 값(일부 줄만):

```text
llamacpp:prompt_tokens_total 25532
llamacpp:prompt_tokens_cached_total 20317
llamacpp:tokens_predicted_total 6639
llamacpp:n_tokens_max 621
llamacpp:predicted_tokens_seconds 10.3906
llamacpp:requests_deferred 0
```

`prompt_tokens_cached_total`이 재사용된 prompt token의 누적이다. 평균 decode 10.4 tok/s는 부하가 심했던 시간까지 섞인 값이라 6.2절의 65~104 tok/s보다 훨씬 낮다 — **누적 평균 하나로는 꼬리도, 시간대별 차이도 안 보인다.** 기기 telemetry는 턴 단위 분포로 남긴다(H1).

### 11.6 코드 — deadline과 취소가 있는 client 라이브러리 (예제 15)

무엇을 확인하는 코드인지: 위 계약을 65줄로 구현한다. `Turn` 객체 하나가 요청 하나다. `events()`는 `delta`/`done` 이벤트를 내보내고, 실패는 `LLMError(kind)`로 던진다. `cancel()`은 **다른 thread에서** 호출할 수 있고(barge-in 감지 thread), 소켓을 `shutdown`해서 막혀 있는 읽기를 즉시 깨운다. deadline은 "다음 chunk를 기다려도 되는 시간"을 매번 다시 계산해 소켓 timeout으로 건다.

```python
# llm_client.py — orchestrator가 LLM service를 부르는 계약(contract)을 코드로 고정한다
import http.client, json, socket, threading, time

class LLMError(Exception):
    def __init__(self, kind, msg=""):              # kind: overflow|timeout|cancelled|http|io
        super().__init__(f"{kind}: {msg}"); self.kind = kind

class Turn:
    """한 번의 생성 요청. events()로 받고, 다른 thread에서 cancel()로 끊는다."""
    def __init__(self, port, messages, max_tokens=128, deadline_s=10.0,
                 ttft_timeout_s=2.0, grammar=None, **sampling):
        self.port, self.cancelled = port, threading.Event()
        self.t0 = time.monotonic(); self.deadline = self.t0 + deadline_s
        self.ttft_deadline = self.t0 + ttft_timeout_s
        body = {"messages": messages, "max_tokens": max_tokens, "stream": True,
                "cache_prompt": True, **sampling}
        if grammar: body["grammar"] = grammar
        self.body = json.dumps(body); self.conn = None
        self.tel = {"ttft_ms": None, "n_tokens": 0, "finish": None}

    def cancel(self):                               # barge-in: 어느 thread에서나 호출 가능
        self.cancelled.set()
        if self.conn and self.conn.sock:
            try: self.conn.sock.shutdown(socket.SHUT_RDWR)   # 막혀 있는 recv를 깨운다
            except OSError: pass

    def _timeout(self):                             # 다음 chunk를 기다릴 수 있는 최대 시간
        lim = self.ttft_deadline if self.tel["ttft_ms"] is None else self.deadline
        left = min(lim, self.deadline) - time.monotonic()
        if left <= 0: raise LLMError("timeout", "first token" if lim != self.deadline else "deadline")
        return left

    def events(self):
        try:
            self.conn = http.client.HTTPConnection("127.0.0.1", self.port, timeout=self._timeout())
            self.conn.request("POST", "/v1/chat/completions", self.body,
                              {"Content-Type": "application/json"})
            r = self.conn.getresponse()
            if r.status != 200:
                err = json.loads(r.read() or b"{}").get("error", {})
                kind = "overflow" if err.get("type") == "exceed_context_size_error" else "http"
                raise LLMError(kind, f"{r.status} {err.get('message', '')}")
            while True:
                self.conn.sock.settimeout(self._timeout())
                line = r.readline()
                if self.cancelled.is_set(): raise LLMError("cancelled")
                if not line: break
                line = line.decode().strip()
                if not line.startswith("data: ") or line == "data: [DONE]": continue
                ev = json.loads(line[6:]); ch = (ev.get("choices") or [{}])[0]
                if ch.get("delta", {}).get("content"):
                    if self.tel["ttft_ms"] is None:
                        self.tel["ttft_ms"] = round(1000 * (time.monotonic() - self.t0), 1)
                    self.tel["n_tokens"] += 1
                    yield {"type": "delta", "text": ch["delta"]["content"]}
                if ch.get("finish_reason"):
                    self.tel.update(finish=ch["finish_reason"], server=ev.get("timings", {}))
            self.tel["total_ms"] = round(1000 * (time.monotonic() - self.t0), 1)
            yield {"type": "done", "telemetry": self.tel}
        except socket.timeout:
            raise LLMError("timeout", "no data before deadline")
        except OSError as e:
            raise LLMError("cancelled" if self.cancelled.is_set() else "io", str(e))
        finally:
            if self.conn: self.conn.close()
```

시험 코드 — 정상, grammar, 취소, deadline, overflow 다섯 경우를 한 번씩:

```python
# 예제: llm_client 계약 시험 — 정상 / grammar / 취소 / deadline / overflow
import threading, time
from llm_client import Turn, LLMError
PORT = 23917
ask = lambda q: [{"role": "user", "content": q}]

def run(name, turn, cancel_after=None):
    if cancel_after: threading.Timer(cancel_after, turn.cancel).start()
    text = ""
    try:
        for ev in turn.events():
            if ev["type"] == "delta": text += ev["text"]
            else: tel = ev["telemetry"]
        print(f"{name:9s} OK   {text[:40]!r:44s} ttft={tel['ttft_ms']} ms n={tel['n_tokens']} "
              f"finish={tel['finish']} total={tel['total_ms']} ms")
    except LLMError as e:
        print(f"{name:9s} ERR  kind={e.kind:9s} after {1000*(time.monotonic()-turn.t0):.0f} ms, "
              f"got {turn.tel['n_tokens']} tokens  ({str(e)[:60]})")

run("normal",   Turn(PORT, ask("Name a primary color."), max_tokens=20, temperature=0))
run("grammar",  Turn(PORT, ask("Is water wet? Answer yes or no."), max_tokens=4,
                     grammar='root ::= "yes" | "no"', temperature=0))
run("cancel",   Turn(PORT, ask("Write a long story."), max_tokens=500), cancel_after=0.5)
run("deadline", Turn(PORT, ask("Count from 1 to 500."), max_tokens=500, deadline_s=0.8,
                     ignore_eos=True))
run("overflow", Turn(PORT, ask("word " * 5000), max_tokens=8))
```

```text
normal    OK   'The primary color is typically defined a'   ttft=26.7 ms n=20 finish=length total=446.1 ms
grammar   OK   'yes'                                        ttft=184.7 ms n=1 finish=stop total=221.1 ms
cancel    ERR  kind=cancelled after 506 ms, got 18 tokens  (cancelled: )
deadline  ERR  kind=timeout   after 803 ms, got 27 tokens  (timeout: no data before deadline)
overflow  ERR  kind=overflow  after 66 ms, got 0 tokens  (overflow: 400 request (5030 tokens) exceeds the available co)
```

출력에서 볼 것:

- `normal`: max_tokens 20에 닿아 `finish=length`. 문장이 끊겼다는 신호를 client가 받는다(6.4).
- `grammar`: GBNF `root ::= "yes" | "no"`가 출력을 정확히 `yes` 한 단어로 묶었다. tool 호출 인자 같은 구조화 출력은 이렇게 **sampler 단계에서 제약**한다 — 작은 모델일수록 "JSON으로 답해"라는 지시만으로는 깨지기 쉽다.
- `cancel`: 0.5 s 타이머 thread가 `cancel()`을 부르자 **506 ms**에 `cancelled`로 끝났다. 18 token을 받은 뒤다.
- `deadline`: 0.8 s 전체 deadline에서 **803 ms**에 `timeout`. 27 token을 받았지만 답은 버려진다. 서버 쪽은 연결이 닫혔으므로 7절의 경로로 slot을 비운다 — **deadline도 결국 취소다.**
- `overflow`: 5030 token 프롬프트가 66 ms 만에 `overflow`로 분류됐다. HTTP 400 본문의 `type`을 보고 분류했으므로 orchestrator는 "이력을 줄여 재시도"를 자동으로 할 수 있다.

---

## 12. 임베디드 관점에서 다시 보기 — 설계 리뷰용 한 장

웨어러블 음성 비서(예를 들어 Hark 같은 기기라면 — 추정)를 위한 온디바이스 LLM 서비스 설계를 한 장으로 요약하면:

| 항목 | 결정 | 근거 (이 노트) |
|---|---|---|
| 프로세스 | LLM service 1개, 모델 1벌, client 여럿 | 1.1 |
| IPC | 요청 / 이벤트 스트림 / cancel / 오류 4종을 계약에 | 1.2, 11 |
| ctx | 2048 (예), slot 1~2개, KV f16 24 MiB | 4.1 |
| 고정 프롬프트 | KV를 첫 부팅 때 계산해 flash에 저장, 세션 시작 시 복원 | 4.2 (614 → 1 token) |
| 대화 | `cache_prompt` on, 변하는 값은 user 메시지 쪽에 | 3 (prefill 10배 감소) |
| context 정책 | system + 구조화 프로필 + LLM 요약 + 최근 턴, hysteresis 퇴출 | 5 (prefill 4.2배 감소) |
| 스트리밍 | phrase chunker, 첫 phrase는 공격적으로 | 6 (+150 ms) |
| barge-in | TTS 먼저 정지, 연결 끊기로 LLM 취소, 재생된 부분만 이력에 | 7 (1~2 token, 수십 ms) |
| 동시성 | 음성 턴 중 배경 요청 금지 | 8 (ITL 1.8배) |
| 스레드 | 포화 지점의 최소 스레드 수, realtime 우선순위 금지 | 8.3, K2 |
| 메모리 | mmap 유지, repack 여부는 속도와 dirty를 같이 재서 결정, 압박 사다리 | 9 |
| 생명주기 | wake word에 preload, 대화 후 30~60 s 유지, 압박·장시간 idle에 unload | 2, 10 |
| 관측 | 턴별 ttft, prompt_n, cache_n, finish_reason, 퇴출 횟수 | 11.5 |

펌웨어 엔지니어의 눈으로 보면 이 표는 **"명령 큐를 가진 장치 펌웨어"의 설계 문서**와 구조가 같다: 자원 풀(KV slot), 캐시 정책(prefix, 퇴출), abort 처리(취소), 전원 상태(load/unload), 장애 처리(오류 분류, 재시작 후 복원), 그리고 telemetry.

---

## 13. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| system prompt에 현재 시각을 넣음 | 대화가 길어질수록 TTFT가 선형 증가 | 매 턴 prefix가 첫 줄에서 깨짐 | 변하는 값은 이번 user 메시지에 |
| 답을 후처리해서 이력에 저장 | 턴마다 이전 답 길이만큼 다시 prefill | 저장한 텍스트 ≠ 생성한 token | 원문 그대로 저장, 표시용만 따로 가공 |
| 1턴씩 퇴출(eager) | context가 찬 뒤 매 턴 TTFT가 수백 ms 튐 | 퇴출·요약 갱신마다 prefix 붕괴 | low water mark로 한꺼번에 퇴출 |
| token 수를 template 없이 셈 | 예산을 지켰는데 400 오류 | chat template 토큰(29개 등) 누락 | `/apply-template` 후 `/tokenize` |
| `finish_reason` 무시 | 답이 문장 중간에서 끊겨 읽힘 | ctx·max_tokens 한도 도달 | `length`면 별도 처리 |
| barge-in 후 생성 전문을 이력에 | "아까 말씀드렸듯이…"라며 안 들은 내용 언급 | 재생 안 된 부분까지 저장 | 재생된 phrase까지만 저장 |
| `-t`를 core 수로 | tg가 오히려 느리고 오디오가 끊김 | barrier spin, core 경합 | 포화 지점의 최소 스레드 수 |
| RSS만 보고 메모리 판단 | "770 MiB나 쓴다" 또는 "괜찮다" 오판 | clean/dirty 구분 없음 | footprint/PSS로 dirty 따로 |
| `--load-mode none`이나 mlock을 무심코 | 다른 앱이 자꾸 죽거나 LLM이 먼저 죽음 | 회수 불가 메모리 증가 | mmap 기본 + 압박 사다리 |
| 작은 모델의 요약을 그대로 신뢰 | 사용자가 말한 적 없는 사실이 기억됨 | 요약 hallucination ("25 years old") | 핵심 사실은 구조화 프로필로 |

---

## 14. 면접에서 이렇게 말한다

**Q.** Design the on-device LLM service for a wearable voice assistant.

**A.** 모델 1벌을 소유하는 별도 서비스 프로세스를 두고, orchestrator와는 요청·이벤트 스트림·취소·오류 4가지로 된 계약으로 통신한다. 서비스 안은 tokenizer, scheduler(slot), runtime, sampler, KV manager다. 핵심 정책은 세 가지: 대화 prefix KV 재사용, orchestrator 쪽 context 예산 관리, 메모리 압박 사다리. 그리고 barge-in과 deadline은 모두 "취소"라는 한 경로로 처리한다.

> "I'd run the model in a single service process that owns one copy of the weights and a small pool of KV slots, behind an IPC contract with four parts: request with a deadline, a token event stream, cancel, and typed errors. Inside: tokenizer, a slot scheduler, the runtime, sampler and a KV manager. The orchestrator owns conversation state and enforces a token budget; the service reuses prefix KV across turns. For memory I keep weights mmapped, persist session KV to flash, and have a pressure ladder that drops caches before the OS kills us. I'd measure TTFT, cache hit tokens and finish reasons per turn."

**Q.** How do you handle multi-turn conversations without reprocessing the whole history?

**A.** 서비스가 세션별로 지난 턴의 token과 KV를 들고 있다가, 새 프롬프트와 공통 prefix를 찾아 그 뒤만 prefill한다. 내 측정에서 8턴 대화의 prefill이 2558 → 260 token, TTFT 합이 4배 줄었다. 대신 prefix를 깨는 것들 — system prompt의 변하는 값, 후처리한 답, 앞쪽 턴 삭제 — 을 막아야 하고, 긴 고정 프롬프트는 KV를 flash에 저장해 두고 복원한다(614 token → 1 token).

> "The service keeps each session's tokens and KV, finds the longest common prefix with the new prompt and only prefills the suffix. In my test an 8-turn conversation went from 2,558 to 260 prefilled tokens and total TTFT dropped about 4x. The catch is keeping the prefix byte-identical: no timestamps in the system prompt, store the exact generated text, and evict old turns in chunks, because every eviction breaks the prefix. For a long fixed system prompt I save its KV to flash once and restore it per session."

**Q.** The user interrupts mid-answer. What happens in the stack?

**A.** VAD(echo 제거 후)가 사용자 음성을 확정하면 orchestrator가 먼저 TTS를 끊고, LLM 요청을 취소한다. llama-server에서는 연결을 닫으면 cancel task가 큐 맨 앞에 들어가고, 내 측정에서 1~2 token 더 만든 뒤 수십 ms 안에 slot이 비었다. 이력에는 실제로 재생된 부분까지만 남긴다. prefill 중이거나 NPU에 그래프를 통째로 넘긴 경우엔 취소 단위가 커지므로 작업을 잘게 쪼갠다.

> "Once VAD confirms user speech, the orchestrator stops TTS first, then cancels the generation. With llama.cpp's server, closing the stream enqueues a cancel at the front of the task queue; I measured one or two extra tokens and the slot free within tens of milliseconds. The assistant message in history is truncated to what was actually spoken, so the model doesn't think it said things the user never heard. Long prefills or monolithic NPU graphs are the places where cancellation latency hides, so I chunk them."

**Q.** How do you keep memory usage bounded?

**A.** 세 층으로 막는다. 정적으로는 ctx·slot 수로 KV 상한을 고정하고(token당 12 KiB 같은 공식으로 계산), 요청 단위로는 orchestrator가 예산 안에서 프롬프트를 만든다. 런타임에는 압박 신호에 따라 prompt cache → idle slot KV(flash로) → ctx 축소 → 모델 unload 순으로 내려놓는다. 그리고 dirty와 clean을 구분해서 본다 — mmap된 가중치는 clean이지만 token마다 다시 읽히므로 회수되면 성능이 flash 대역폭으로 추락한다.

> "Statically, KV is capped by context size times slots, which I size from the per-token formula; per request, the orchestrator keeps prompts under a token budget; at runtime, a pressure ladder drops the prompt cache, then spills idle session KV to flash, then shrinks context, and finally unloads the model before the low-memory killer gets us. I track dirty versus clean memory separately: mmapped weights are reclaimable, but decode touches all of them every token, so if they get evicted you're suddenly running at flash bandwidth."

**Q.** Keep the model loaded or unload it?

**A.** 숫자로 정한다. 내 측정에서 unload하면 dirty 메모리가 340 → 79 MB로 줄지만 깨우는 턴의 TTFT가 1.3~1.9 s가 됐다(warm은 수십~수백 ms). 에너지로는 DRAM refresh가 사용량과 무관한 경우가 많아서 내린다고 거의 아끼지 못한다. 그래서 대화 중과 직후 30~60초는 유지, 장시간 idle이나 압박 신호에 unload, 그리고 wake word가 오면 ASR 동안 preload해서 cold start를 숨긴다.

> "It's a latency-versus-memory trade, mostly not an energy one. In my measurement unloading freed about 260 MB but the wake-up turn took 1.3 to 1.9 seconds versus tens of milliseconds warm. Unless the SoC can power down the freed DRAM, keeping it resident costs little energy. So: keep it warm during and shortly after a conversation, unload on long idle or memory pressure, and start preloading on the wake word so loading overlaps with ASR."

**Q.** Two requests arrive at once — one from the voice session, one background summary. What do you do?

**A.** continuous batching으로 둘을 같이 돌리면 처리량은 조금 오르지만 각 요청의 decode가 느려진다 — 내 측정에서 ITL이 25 → 45 ms. 음성은 지연이 생명이므로 배경 작업을 미루거나 취소하고, 음성 세션이 끝난 뒤 재개한다. SSD에서 host I/O 중 background GC를 멈추는 것과 같다.

> "Batching them raises aggregate throughput a little but slows each stream; I measured inter-token latency going from about 25 to 45 milliseconds. The voice turn is latency-critical, so the orchestrator defers or cancels background work while a voice session is active and resumes it afterwards, the same way SSD firmware pauses background garbage collection during host I/O."

---

## 15. 직접 해보기

1. (손계산) SmolLM2-360M은 32 layer, KV head 5개, head_dim 64다. KV f16 기준 token당 바이트와 ctx 2048 slot 하나의 KV 크기는? 정답: 2 × 32 × 320 × 2 B = 40,960 B(40 KiB)/token, × 2048 = 80 MiB.
2. (손계산) system prompt 300 token, 턴당 60 token 증가, 10턴. cache 끔/켬의 총 prefill token은? (켬일 때 턴당 새 token 20개 가정) 정답: 끔 = 10 × 300 + 60 × 45 = 5700, 켬 = 300 + 9 × 20 = 480.
3. (손계산) 가중치 380 MiB가 전부 회수되었고 flash가 400 MB/s(eMMC급 가정)라면 decode 한 token에 추가되는 시간은? 정답: 약 398 MB ÷ 400 MB/s ≈ 1.0 s/token — 사실상 정지.
4. (코드) 예제 10의 phrase chunker를 "첫 phrase는 단어 6개 이상이면 아무 공백에서나 자르기"로 바꾸고 time-to-first-phrase를 다시 재라. 힌트: `stats`에 phrase 개수를 두고 첫 번째일 때만 다른 정규식.
5. (코드) 예제 15의 `Turn`에 `on_phrase` 콜백을 넣어, TTS가 실제로 재생한 phrase만 모아 `spoken_text`를 돌려주게 하라. 그리고 취소 시 이력에 그것만 넣는 orchestrator 루프를 써라. 힌트: 7.3의 표.
6. (실험) 예제 8의 low_water를 0.4, 0.8로도 돌려 total prefill token과 남는 턴 수의 관계를 그려라. 힌트: 낮을수록 prefill은 줄지만 기억이 짧아진다 — 어디가 무릎(knee)인가?

---

## 16. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| slot | 서버의 요청 자리 | slot마다 자기 KV 영역과 token 목록을 가진다 (`-np`) |
| continuous batching | 연속 배칭 | 여러 slot의 decode를 한 번의 forward로 묶고, 끝난 자리에 바로 새 요청을 넣는다 |
| prompt cache / prefix reuse | 프롬프트 KV 재사용 | 새 프롬프트와 slot KV의 공통 prefix는 다시 계산하지 않는다 (`cache_prompt`) |
| `cache_n` / `prompt_n` | 재사용 / 새로 계산한 prompt token 수 | 서버 `timings`의 두 숫자, 정책 검증의 기본 지표 |
| context shift | 문맥 밀어내기 | ctx가 차면 앞 `n_keep` 뒤의 절반을 버리고 계속 생성 (이 빌드 기본 off) |
| low water mark | 하한 수위 | 넘쳤을 때 이 수준까지 한꺼번에 내려 재발을 늦추는 hysteresis |
| running summary | 누적 요약 | 내보낸 턴의 핵심을 짧게 들고 다니는 텍스트 |
| SSE | Server-Sent Events | HTTP 응답을 열어 둔 채 `data:` 줄을 흘려보내는 스트리밍 형식 |
| TTFT / ITL | 첫 token 시간 / token 간 간격 | 스트리밍 지연의 두 축 (D5) |
| TTFA | 첫 소리까지 시간 | TTFT + 첫 phrase까지의 token × ITL + TTS 지연 |
| barge-in | 말 끊기 | 시스템이 말하는 중에 사용자가 말해 끼어드는 것 |
| deadline | 마감 시각 | 이때까지 안 끝나면 결과를 버리고 취소 |
| GBNF grammar | 문법 제약 | sampler가 문법에 맞는 token만 고르게 하는 llama.cpp 형식 |
| mmap | 메모리 매핑 | 파일을 주소 공간에 붙이고 접근할 때 page 단위로 읽음 |
| repack | 가중치 재배치 | CPU SIMD에 맞게 가중치를 복사·재배열 — dirty 메모리가 된다 |
| dirty / clean page | 회수 불가 / 회수 가능 page | clean은 파일에 원본이 있어 버릴 수 있다 |
| footprint | macOS의 dirty 메모리 지표 | `phys_footprint` — Android의 PSS/LMK 회계와 비슷한 역할 |
| refault | 다시 page fault | 회수된 clean page를 다시 읽는 것 — 많아지면 thrashing |
| LMK / lmkd | Low Memory Killer | Android에서 압박 시 덜 중요한 프로세스를 죽이는 데몬 |
| PSI | Pressure Stall Information | 커널이 "메모리 때문에 멈춘 시간 비율"을 알려 주는 지표 |
| onTrimMemory | Android 메모리 알림 콜백 | 앱에게 메모리를 줄이라고 미리 알리는 신호 |
| idle unload (sleep) | 유휴 시 모델 내리기 | `--sleep-idle-seconds` |
| PASR | Partial Array Self-Refresh | 안 쓰는 DRAM 영역의 refresh를 꺼 전력을 아끼는 기능 (SoC 의존) |

---

## 17. 요약 & 체크리스트

온디바이스 LLM은 모델 파일이 아니라 **서비스**다. 서비스는 모델 1벌과 세션별 KV slot을 갖고, orchestrator와 요청·스트림·취소·오류로 된 계약을 맺는다. 이 Mac의 `llama-server`로 잰 결과: 대화 prefix 재사용은 8턴 prefill을 2558 → 260 token으로(TTFT 합 4배), 긴 고정 프롬프트의 KV 파일 복원은 614 → 1 token으로 줄였다. context는 서버가 아니라 orchestrator가 지켜야 하며, 넘치면 400 오류나 조용히 잘린 답(`length`)이 온다. 퇴출은 hysteresis로 해야 prefix가 덜 깨진다(prefill 4.2배 차이). 스트리밍은 phrase 단위로 TTS에 넘기고(첫 phrase +150 ms), barge-in은 연결을 끊어 1~2 token, 수십 ms 안에 멈춘다. 동시 요청은 처리량을 조금 올리지만 각 요청의 ITL을 1.8배로 늘린다. 메모리는 RSS가 아니라 dirty/clean으로 봐야 하고, mmap 가중치도 회수되면 decode가 flash 대역폭으로 추락한다. 모델을 내릴지는 에너지보다 메모리와 지연(깨우는 턴 1.3~1.9 s)의 문제다.

- [ ] 스택 5층(app, orchestrator, service, backend, OS)과 IPC 경계를 그리고 각 층의 책임을 말할 수 있다
- [ ] `cache_n`·`prompt_n`으로 prefix 재사용을 측정하고, prefix를 깨는 원인 5가지를 댈 수 있다
- [ ] KV/token 공식으로 slot 메모리와 KV 파일 크기를 손으로 계산하고 실측(12,305 B/token)과 맞춰 볼 수 있다
- [ ] ctx 초과 시 세 가지 동작(400, length, context shift)을 구분하고 orchestrator 예산을 template 포함으로 셀 수 있다
- [ ] eager 퇴출이 왜 매 턴 전체 prefill을 일으키는지 설명하고 low water mark로 고칠 수 있다
- [ ] TTFA = TTFT + 첫 phrase token × ITL + TTS를 쓰고 phrase chunker를 만들 수 있다
- [ ] barge-in 때 스택 각 층이 하는 일과, 이력에 무엇을 남길지 말할 수 있다
- [ ] dirty/clean 메모리를 구분하고 refault thrashing의 크기를 손으로 계산할 수 있다
- [ ] 메모리 압박 사다리와 idle unload 정책을 숫자(로딩 시간, 깨우는 TTFT, 반환 메모리)로 정당화할 수 있다
- [ ] deadline·취소·오류 분류·telemetry가 들어간 client 계약을 코드로 쓸 수 있다

---

## 참고 자료

- llama.cpp server README (빌드에 포함된 `tools/server/README.md`) — 엔드포인트, `cache_prompt`, `/slots` save/restore, Sleeping on Idle: https://github.com/ggml-org/llama.cpp/tree/master/tools/server
- llama.cpp 소스 `tools/server/server-context.cpp`, `server-queue.cpp` — slot, context shift, 취소 경로 (이 노트의 1.3, 5.1, 7.1)
- MDN, "Using server-sent events": https://developer.mozilla.org/en-US/docs/Web/API/Server-sent_events/Using_server-sent_events
- Android Developers, "Manage your app's memory" (onTrimMemory) 및 "Low memory killer daemon (lmkd)": https://developer.android.com/topic/performance/memory , https://source.android.com/docs/core/perf/lmkd
- Linux kernel docs, "PSI - Pressure Stall Information": https://docs.kernel.org/accounting/psi.html
- Kwon et al., "Efficient Memory Management for Large Language Model Serving with PagedAttention" (vLLM, SOSP 2023) — slot/KV 관리의 서버 쪽 원형
- Yu et al., "Orca: A Distributed Serving System for Transformer-Based Generative Models" (OSDI 2022) — continuous batching의 원조
- 이 노트 세트: B8, D5(5.6 prefix caching, 6.1 batching), F3, F4, J1, J2, J5, K2, K4, L1, L3, L4, L5
