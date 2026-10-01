# H3. 백엔드 ingestion — object storage, 큐, 스키마·메타데이터, 파티셔닝, 데이터 버전

> **이 노트를 다 읽으면**: 10k 기기 × MB/day를 TB/month·MB/s·워커 수·대략 비용으로 바로 환산할 수 있다 · raw(bronze) → curated(silver) → features(gold) 레이어와 불변 저장·재처리·멱등(idempotent) 처리를 설계하고 "파서 버그를 3개월 뒤에 발견하면?"에 답할 수 있다 · 큐의 at-least-once 전달, 재시도, DLQ, backpressure, ordering key를 로컬 시뮬레이션으로 설명할 수 있다 · 센서 로그를 dt 파티션 Parquet으로 바꾸고 predicate pushdown·small files·compaction을 숫자로 말하고, SQLite catalog와 manifest + content hash로 재현 가능한 학습 데이터셋을 만들 수 있다
> **JD 연결**: "Build data collection and **ingestion pipelines** … for various sensors, **at scale**", "Experience building sensor data collection pipelines" — study_prep_list **H3**: object storage(S3), queue(Kafka/PubSub) / 스키마·메타데이터(기기 ID, FW 버전, 센서 설정) / 데이터 버전 관리 ("at scale", P1). 함께 닿는 행: **H1**(로그 포맷), **H2**(전송·재개·CRC), **H5**(데이터셋 관리), **H6**(동의·보관 기간), **H7**(품질 모니터링), **H8**(fleet 규모 수집)
> **Don 기준 난이도**: 바이너리 로그 포맷, CRC, 버전 필드, 재시도, 테스트 결과 수집·스케줄링 인프라는 이미 몸에 있다 (SSD 테스트 플랫폼 SDK·챔버 스케줄링·상태 수집) / 새로 배울 것은 클라우드 저장의 어휘(bucket·prefix·lifecycle), 분산 큐의 전달 보장, 컬럼 저장(Parquet)과 파티셔닝, "데이터셋 = 버전 붙은 파일 목록"이라는 사고법
> **선행 노트**: A6(pandas·바이너리 로그 읽기), G7(seq·gap 검출과 time QA — 7절), H1(온디바이스 로그 포맷 — 병행 노트), H2(전송·재개 — 병행 노트). 다음 노트: H5(데이터셋 관리), H7(품질 모니터링)

---

## 0. 큰 그림 — 이게 왜 필요한가

### 0.1 기기에서 학습 데이터까지

H1에서 펌웨어가 센서 샘플을 청크로 묶어 flash에 쓰고, H2에서 그 청크를 BLE·Wi-Fi로 폰이나 서버까지 올렸다. 여기서 끝이 아니다. 서버에 도착한 바이트 덩어리는 아직 **학습 데이터가 아니다**. 모델팀이 "fw 1.4 이상, 드롭 없는 세션, 동의한 사용자, 손목 착용 데이터만 주세요"라고 했을 때 10분 안에 정확한 파일 목록을 줄 수 있어야 비로소 데이터 파이프라인이다.

H3은 그 사이, 즉 **"업로드된 바이트" → "질의할 수 있는, 버전 붙은 데이터셋"**을 다룬다. 이 일을 보통 **ingestion**(수집·적재)이라고 부른다. ingestion은 "외부에서 들어온 데이터를 받아서, 검증하고, 정해진 형태로 저장소에 넣는 일" 전체를 뜻한다.

```svg
<svg viewBox="0 0 680 440" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="h3a" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="22" font-size="13">H3 레퍼런스 아키텍처 (클라우드 제품명은 예시 — 개념은 같다)</text><rect x="10" y="50" width="110" height="56" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="65" y="74" font-size="13" text-anchor="middle">기기 10k대</text><text x="65" y="93" font-size="12" text-anchor="middle">청크 로그 (H1)</text><rect x="150" y="50" width="120" height="56" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="210" y="74" font-size="13" text-anchor="middle">폰 앱 / 게이트웨이</text><text x="210" y="93" font-size="12" text-anchor="middle">배치·재개 (H2)</text><rect x="300" y="50" width="150" height="56" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="375" y="74" font-size="13" text-anchor="middle">Upload API</text>
<text x="375" y="93" font-size="12" text-anchor="middle">인증 · presigned URL</text><rect x="480" y="50" width="180" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="570" y="74" font-size="13" text-anchor="middle">Raw object store</text><text x="570" y="93" font-size="12" text-anchor="middle">bronze · 불변 · 받은 그대로</text><rect x="480" y="150" width="180" height="56" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="570" y="174" font-size="13" text-anchor="middle">이벤트 큐</text><text x="570" y="193" font-size="12" text-anchor="middle">Kafka · Pub/Sub · SQS</text><rect x="250" y="150" width="170" height="56" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="335" y="174" font-size="13" text-anchor="middle">Parse/validate 워커 ×N</text><text x="335" y="193" font-size="12" text-anchor="middle">버전별 파서 · QA</text>
<rect x="480" y="236" width="180" height="40" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor" stroke-dasharray="5 4"/><text x="570" y="261" font-size="13" text-anchor="middle">DLQ (dead-letter)</text><rect x="20" y="150" width="180" height="56" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="110" y="174" font-size="13" text-anchor="middle">Curated (silver)</text><text x="110" y="193" font-size="12" text-anchor="middle">Parquet · dt 파티션</text><rect x="20" y="240" width="180" height="50" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="110" y="261" font-size="13" text-anchor="middle">Catalog / 메타데이터 DB</text><text x="110" y="279" font-size="12" text-anchor="middle">세션·fw·QA·경로·해시</text><rect x="250" y="320" width="170" height="50" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="335" y="341" font-size="13" text-anchor="middle">Query engine</text>
<text x="335" y="359" font-size="12" text-anchor="middle">DuckDB · Spark · BigQuery</text><rect x="20" y="330" width="180" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="110" y="351" font-size="13" text-anchor="middle">Dataset builder (H5)</text><text x="110" y="369" font-size="12" text-anchor="middle">manifest · split</text><rect x="480" y="320" width="180" height="50" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="570" y="341" font-size="13" text-anchor="middle">Dashboards (H7)</text><text x="570" y="359" font-size="12" text-anchor="middle">드롭률 · drift · fleet</text><line x1="120" y1="78" x2="148" y2="78" stroke="currentColor" marker-end="url(#h3a)"/><line x1="270" y1="78" x2="298" y2="78" stroke="currentColor" marker-end="url(#h3a)"/><line x1="450" y1="78" x2="478" y2="78" stroke="currentColor" marker-end="url(#h3a)"/>
<line x1="570" y1="106" x2="570" y2="148" stroke="currentColor" marker-end="url(#h3a)"/><text x="578" y="132" font-size="12">object created</text><line x1="480" y1="178" x2="422" y2="178" stroke="currentColor" marker-end="url(#h3a)"/><line x1="420" y1="196" x2="478" y2="250" stroke="#d0564a" marker-end="url(#h3a)"/><line x1="250" y1="178" x2="202" y2="178" stroke="currentColor" marker-end="url(#h3a)"/><line x1="250" y1="198" x2="202" y2="255" stroke="currentColor" marker-end="url(#h3a)"/><path d="M 480 96 Q 420 110 400 148" fill="none" stroke="currentColor" stroke-dasharray="5 4" marker-end="url(#h3a)"/><text x="398" y="124" font-size="12" text-anchor="end">replay</text><line x1="200" y1="204" x2="290" y2="318" stroke="currentColor" marker-end="url(#h3a)"/><line x1="200" y1="280" x2="248" y2="332" stroke="currentColor" marker-end="url(#h3a)"/>
<line x1="250" y1="356" x2="202" y2="356" stroke="currentColor" marker-end="url(#h3a)"/><line x1="420" y1="345" x2="478" y2="345" stroke="currentColor" marker-end="url(#h3a)"/><text x="10" y="412" font-size="12">실선 = 정상 흐름, 점선 = 파서가 바뀌었을 때 bronze에서 다시 흘려보내는(replay) 경로.</text><text x="10" y="430" font-size="12">원칙: bronze는 절대 고치지 않는다 → silver·gold·catalog는 언제든 bronze에서 다시 만들 수 있다.</text>
</svg>
```

그림 1 — H3 레퍼런스 아키텍처. 기기 → 업로드 API → 불변 raw 저장소(bronze) → 이벤트 큐 → 파싱·검증 워커 → 컬럼 저장(silver)과 catalog → 질의 엔진 → 학습 데이터셋(H5)·대시보드(H7). 실패한 메시지는 DLQ로, 파서가 바뀌면 bronze에서 다시 흘려보낸다. 예를 들어 Hark 같은 회사라면 이와 비슷한 구조를 쓸 것이라고 추정할 수 있지만 실제 구성은 모른다.

### 0.2 Don이 이미 만든 것과 1:1로 대응된다

Don은 SSD 양산 펌웨어 시절 사내 테스트 인프라(테스트 호스트용 SDK, 챔버 스케줄링, 상태 수집)를 만들고 IT·보안·네트워크 팀과 협업했다. 그 구조를 그대로 옮기면 ML ingestion이 된다.

| SSD 테스트 인프라 | ML 센서 ingestion | 공통 원리 |
|---|---|---|
| 테스트 호스트에 깔리는 SDK (결과 포맷·업로드 규약) | 기기 펌웨어 로거 + 업로드 클라이언트 (H1·H2) | 생산자 쪽 "계약"을 코드로 고정 |
| 테스트 로그 원본 보관 서버 | raw object store (bronze) | 원본은 고치지 않고 쌓는다 |
| 챔버·슬롯 스케줄러, 작업 큐 | 이벤트 큐 + 워커 풀 | 일을 큐에 넣고 여러 일꾼이 가져간다 |
| 결과 파싱 스크립트 → 결과 DB | 버전별 파서 → Parquet + catalog | 파싱 로직은 버전이 있다 |
| 드라이브 S/N·FW rev·lot·챔버 온도 태그 | device_id·fw·센서 설정·clock 정보 메타데이터 | 메타데이터 없이 데이터는 쓸모없다 |
| 상태 수집 (heartbeat, 진행률) | fleet 상태·업로드 지연·QA 대시보드 (H7·H8) | 파이프라인 자체를 모니터링 |
| "파싱 스크립트 버그 → 지난 결과 다시 돌리기" | bronze replay → silver 재생성 | 원본이 남아 있어야 가능 |
| IT·보안·네트워크 팀과 방화벽·계정·저장소 협의 | IAM·bucket policy·암호화·PII(H6)·egress 비용 | 데이터 인프라는 혼자 못 만든다 |

면접에서 이 표의 왼쪽 열을 경험으로, 오른쪽 열을 "그래서 ML 쪽에서는 이렇게 하겠다"로 말하면 된다.

### 0.3 이 노트의 지도와 실습 환경

클라우드 접속 없이 **로컬에서 작은 ingestion 파이프라인**을 끝까지 만든다. 파일시스템을 object storage로, Python `queue`와 thread를 메시지 큐로, SQLite를 ledger·catalog로 쓴다. 모든 산출물은 `/private/tmp/claude-501/h3/` 아래에 생긴다.

```
/private/tmp/claude-501/h3/
├── h3fmt.py, h3parse.py, objstore.py      ← 예제 2·3에서 만드는 모듈 (이후 예제가 import)
├── lake/                                  ← "버킷" 하나라고 생각한다
│   ├── bronze/imu/dt=…/dev=…/sess=…/chunk=….hklg   ← 받은 그대로 (예제 4)
│   ├── staging/<sha1(key)>.parquet                  ← 워커 출력, 청크당 1개 (예제 4)
│   ├── silver/imu/dt=…/part-0.parquet               ← compaction 결과 (예제 7)
│   ├── _ledger.db  catalog.db  manifest_<id>.json   ← dedup·catalog·버전 (예제 4·8·10)
└── bulk/                                  ← 3백만 행 Parquet vs CSV 비교 (예제 5·6)
```

| 절 | 내용 | 예제 |
|---|---|---|
| 1 | 규모 계산 — TB/month, MB/s, 워커 수, 비용 | 1 |
| 2 | 레이어 — bronze/silver/gold, 불변성, 재처리 | — |
| 3 | 스키마·메타데이터, 스키마 진화, 버전별 파서 | 2 |
| 4 | object storage — key 설계, 불변 put, lifecycle, 비용 | 3 |
| 5 | 큐와 워커 — at-least-once, 재시도, DLQ, 멱등 | 4 |
| 6 | Parquet, 파티셔닝, pushdown, small files | 5, 6, 7 |
| 7 | catalog — SQLite 메타데이터 DB | 8 |
| 8 | 파서 버그와 재처리 | 9 |
| 9 | 데이터 버전 — manifest, content hash | 10 |
| 10 | 임베디드 관점 — 펌웨어가 백엔드를 돕는 법 | 11 (C) |

---

## 1. 규모부터 계산한다 — back-of-envelope

### 1.1 직관

설계 면접에서 "10k 기기에서 센서 로그를 받는 백엔드를 설계하라"를 들으면 박스부터 그리지 말고 **숫자부터** 낸다. SSD에서 "IOPS × 블록 크기 = 대역폭"을 먼저 계산하고 큐 깊이를 정하는 것과 같다. 숫자가 정해지면 "Kafka가 필요한가, 큐 하나면 되나", "워커가 3개인가 300개인가"가 저절로 나온다.

### 1.2 손으로 계산

IMU 6축 int16 = 12 B/샘플, 100 Hz, 하루 16시간 착용이라고 하자.

```
12 B × 100 Hz × 3600 s × 16 h = 69,120,000 B ≈ 69 MB/기기/일   (압축 전)
10,000 기기 × 69 MB = 691 GB/일  → × 30 = 20.7 TB/월
평균 ingest rate = 691 GB / 86,400 s ≈ 8.0 MB/s
```

말로 하면: IMU만 연속으로 올려도 한 달에 20 TB 규모다. 오디오를 연속으로 올리면(16 kHz × 16-bit = 32 kB/s → 16시간에 1.8 GB/기기/일) 차원이 달라진다. 그래서 always-listening 기기는 원본 오디오를 다 올리지 않고 **이벤트 주변만, 동의한 사용자만, 샘플링해서** 올리는 정책(H6·H8)이 먼저 정해져야 한다.

평균만 보면 안 된다. 웨어러블은 **밤에 충전하면서 Wi-Fi로 몰아서** 올릴 가능성이 높다. 그러면 하루치가 몇 시간에 몰려 피크가 평균의 3~6배가 된다. 아래 계산기는 피크 배수를 4로 가정한다.

### 1.3 코드로 확인 — 규모 계산기

**예제 1** — 무엇을 확인하나: 기기당 하루 업로드 양(MB)에 따라 월 저장량, 평균 ingest rate, 피크를 감당할 워커 수, 월 PUT 요청 수, 대략 비용이 어떻게 변하는지.

```python
# 규모 계산기: 10k dogfood 기기 → 저장량·ingest rate·PUT 수·대략 비용 (단가는 가정값)
import math
DEV, DAYS = 10_000, 30
GB_MONTH_HOT, GB_MONTH_COLD, PUT_PER_1K = 0.023, 0.001, 0.005   # $ — 가정, 리전·시기마다 다름
CHUNK_MB, PEAK, WORKER_MBPS = 4, 4, 20       # 업로드 객체 크기, 피크/평균 배수, 워커 1개 처리량
def plan(mb_per_dev_day, curated_ratio=0.5):
    raw_tb = DEV * mb_per_dev_day * DAYS / 1e6                   # MB → TB (10진)
    avg_mbps = DEV * mb_per_dev_day / 86_400
    workers = math.ceil(avg_mbps * PEAK / WORKER_MBPS)
    puts = DEV * mb_per_dev_day / CHUNK_MB * DAYS
    hot_cost = raw_tb * 1000 * (1 + curated_ratio) * GB_MONTH_HOT   # 그 달 쌓인 양을 한 달 보관
    return raw_tb, avg_mbps, workers, puts, hot_cost, puts / 1000 * PUT_PER_1K
print(f"{'MB/dev/day':>10} {'raw TB/mo':>9} {'avg MB/s':>8} {'workers':>7} {'PUT/mo':>10}"
      f" {'$store/mo':>9} {'$PUT/mo':>7}")
for mb in (10, 50, 200, 1000):
    tb, mbps, w, puts, c, p = plan(mb)
    print(f"{mb:>10} {tb:>9.1f} {mbps:>8.1f} {w:>7} {puts:>10,.0f} {c:>9,.0f} {p:>7,.0f}")
# 1년 누적 (월 200 MB/dev/day, 90일 뒤 cold로 이동)
tb_m = plan(200)[0]; hot = 3 * tb_m; cold = 9 * tb_m
print(f"1년 뒤: hot {hot:.0f} TB + cold {cold:.0f} TB → 월 ${hot*1000*GB_MONTH_HOT:,.0f} + ${cold*1000*GB_MONTH_COLD:,.0f}")
```

```text
MB/dev/day raw TB/mo avg MB/s workers     PUT/mo $store/mo $PUT/mo
        10       3.0      1.2       1    750,000       104       4
        50      15.0      5.8       2  3,750,000       518      19
       200      60.0     23.1       5 15,000,000     2,070      75
      1000     300.0    115.7      24 75,000,000    10,350     375
1년 뒤: hot 180 TB + cold 540 TB → 월 $4,140 + $540
```

출력에서 볼 것: (1) 기기당 50 MB/day면 월 15 TB, 평균 5.8 MB/s — 생각보다 작은 대역폭이다. 대역폭보다 **객체 수(PUT 375만 건/월)와 누적 저장량**이 설계를 좌우한다. (2) 저장 비용은 매달 **누적**된다. 1년이면 hot 180 TB만으로 월 수천 달러이고, 그래서 lifecycle(오래된 raw를 cold로)이 중요하다.

### 1.4 단가는 가정이다 — 반드시 확인

| 항목 | 이 노트의 가정값 | 메모 |
|---|---|---|
| hot object storage | 약 $0.02~0.026 / GB-월 | AWS S3 Standard·GCS Standard 공개 가격표의 대략적 범위 (리전·용량 구간·시기에 따라 다름) |
| cold / archive | 약 $0.001~0.004 / GB-월 | 꺼낼 때 비용·지연(수 시간)·최소 보관 기간이 붙는 경우가 많다 |
| PUT 요청 | 약 $0.005 / 1,000건 | 작은 객체를 많이 올리면 요청 비용이 저장 비용을 넘을 수 있다 |
| 인터넷 egress | 약 $0.05~0.12 / GB | 클라우드 밖으로 내보낼 때. 같은 리전 안 처리는 보통 훨씬 싸다 |

말로 하면: 숫자 자체보다 **구조**를 기억한다 — 저장은 누적, 요청은 객체 수에 비례, egress는 "데이터를 어디서 처리하느냐"에 비례. 학습 클러스터가 다른 클라우드나 사내에 있으면 매 학습마다 egress가 나간다. 면접에서는 "list price 기준 대략 이 정도, 실제로는 할인·리전에 따라 다르다"고 말한다.

### 1.5 임베디드 연결과 함정

- 청크 크기는 펌웨어가 정한다. 256 KB 청크로 올리면 4 MB 대비 PUT 수가 16배다. 기기 쪽 RAM·flash 페이지·BLE 재전송 단위(H2)와 백엔드 객체 수 사이의 trade-off다.
- 10진(TB = 10¹²)과 2진(TiB = 2⁴⁰)을 섞지 않는다. 클라우드 가격표는 보통 GB(10진)로 표기한다고 알려져 있지만 서비스마다 표기를 확인한다.
- 피크를 무시하고 평균으로 워커 수를 잡으면 밤마다 큐가 밀린다 (5.5절 backpressure).

---

## 2. 레이어 설계 — raw는 불변, 나머지는 다시 만들 수 있다

### 2.1 직관 — "원본 로그는 절대 지우지 않는다"

SSD 불량 분석에서 파싱된 결과만 남기고 원본 로그를 지웠다가, 파싱 스크립트가 틀렸다는 걸 알게 되면 끝이다. 데이터 파이프라인의 제1원칙은 **받은 그대로의 원본을 불변으로 보관하고, 나머지는 전부 그 원본에서 다시 만들 수 있게 한다**는 것이다.

### 2.2 정의 — 세 레이어

업계에서는 이 층을 **bronze / silver / gold**(Databricks 계열에서 "medallion architecture"라고 부르는 이름) 또는 **raw / clean(curated) / features**라고 부른다. 이름은 회사마다 다르고 뜻은 같다.

| 레이어 | 무엇이 들어가나 | 형식 | 고칠 수 있나 | 누가 쓰나 |
|---|---|---|---|---|
| bronze (raw) | 기기가 보낸 바이트 그대로 + 수신 메타(수신 시각, 업로드 ID) | 원본 바이너리 (.hklg, .pb, .wav…) | **절대 안 고침** (append-only) | 재처리, 포렌식 |
| silver (curated) | 파싱·검증·단위 변환·timestamp 정리된 샘플 | Parquet, 날짜 파티션 | 파서 버전이 바뀌면 **새로 만들어** 교체 | 분석, 대시보드, 데이터셋 빌더 |
| gold (features) | 창(window)·특징·라벨이 붙은 학습용 테이블 | Parquet / TFRecord / numpy shard | 실험마다 새 버전 | 학습 (H5) |

```svg
<svg viewBox="0 0 680 290" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="h3b" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="22" font-size="13">세 레이어와 재처리 — 화살표는 "무엇으로부터 만들어지나"</text><rect x="20" y="50" width="180" height="120" rx="8" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="110" y="74" font-size="13" text-anchor="middle">bronze (raw)</text><text x="110" y="98" font-size="12" text-anchor="middle">청크 바이너리 그대로</text><text x="110" y="118" font-size="12" text-anchor="middle">append-only · 불변</text><text x="110" y="138" font-size="12" text-anchor="middle">key = 기기/세션/청크</text><text x="110" y="158" font-size="12" text-anchor="middle">수명: 길게 → cold</text><rect x="250" y="50" width="180" height="120" rx="8" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="340" y="74" font-size="13" text-anchor="middle">silver (curated)</text>
<text x="340" y="98" font-size="12" text-anchor="middle">샘플 테이블 Parquet</text><text x="340" y="118" font-size="12" text-anchor="middle">parser_version 태그</text><text x="340" y="138" font-size="12" text-anchor="middle">dt 파티션 · QA 결과</text><text x="340" y="158" font-size="12" text-anchor="middle">수명: 다시 만들 수 있음</text><rect x="480" y="50" width="180" height="120" rx="8" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="570" y="74" font-size="13" text-anchor="middle">gold (features)</text><text x="570" y="98" font-size="12" text-anchor="middle">창 · 특징 · 라벨</text><text x="570" y="118" font-size="12" text-anchor="middle">manifest로 버전 고정</text><text x="570" y="138" font-size="12" text-anchor="middle">split (H5)</text><text x="570" y="158" font-size="12" text-anchor="middle">수명: 실험 단위</text>
<line x1="200" y1="100" x2="248" y2="100" stroke="currentColor" marker-end="url(#h3b)"/><line x1="430" y1="100" x2="478" y2="100" stroke="currentColor" marker-end="url(#h3b)"/><text x="224" y="92" font-size="12" text-anchor="middle">파서</text><text x="454" y="92" font-size="12" text-anchor="middle">빌더</text><path d="M 110 170 Q 110 240 300 240 Q 340 240 340 172" fill="none" stroke="#d0564a" stroke-dasharray="6 4" marker-end="url(#h3b)"/><text x="120" y="232" font-size="12">파서 v2.0 → v2.1: bronze에서 silver를 새 버전으로 재생성</text><text x="20" y="270" font-size="12">silver·gold가 망가져도 bronze만 있으면 복구된다. bronze가 망가지면 복구할 방법이 없다.</text>
</svg>
```

그림 2 — bronze → silver → gold. 파서가 바뀌면 빨간 점선처럼 bronze에서 silver를 새 버전으로 다시 만든다. 예전 silver는 그 버전을 참조하는 데이터셋 manifest가 남아 있는 동안 지우지 않는다 (9절).

### 2.3 불변성(immutability)이 주는 것

- **재처리 가능**: 파서 버그, 새 특징, 새 timestamp 알고리즘(G7)이 나오면 언제든 원본에서 다시 돌린다.
- **동시성 문제가 사라진다**: 쓰기가 "새 객체 생성"뿐이면 읽는 쪽이 반쯤 쓰인 파일을 볼 일이 없다. 펌웨어로 말하면 **ping-pong 버퍼에서 다 찬 버퍼만 넘기는 것**과 같다.
- **감사(audit)와 디버깅**: "이 학습 샘플은 어느 기기의 어느 청크에서 왔나"를 끝까지 추적할 수 있다 (lineage, 계보).

불변성의 비용은 저장 공간이다. 그래서 bronze는 압축된 원본 그대로 두고, 일정 기간 뒤 cold로 내린다(4.5절). 그리고 **삭제 의무(H6)는 불변성보다 우선**한다 — 사용자가 동의를 철회하면 bronze에서도 지워야 한다. 그래서 bronze key에 device/user를 넣어 "이 사람의 데이터 전부"를 prefix로 찾을 수 있게 설계한다.

### 2.4 멱등(idempotent) 처리 — 같은 입력을 두 번 처리해도 결과가 같다

**멱등성**(idempotency)은 "같은 연산을 여러 번 해도 한 번 한 것과 결과가 같은 성질"이다. 레지스터에 `REG = 0x5`를 두 번 써도 같지만 `REG += 5`를 두 번 하면 다르다. 분산 시스템에서는 메시지가 **두 번 이상 올 수 있으므로**(5절) 모든 처리를 "`=` 형태"로 만들어야 한다.

멱등을 만드는 세 가지 도구:

1. **결정적(deterministic) 출력 경로**: 입력 key가 같으면 출력 파일 경로도 같다 → 두 번 쓰면 같은 내용으로 덮어쓴다.
2. **dedup key + ledger**: 처리 완료한 key를 DB에 기록(`PRIMARY KEY`)하고, 이미 있으면 건너뛴다.
3. **업로드 ID**: 기기가 청크마다 (device_id, session_id, chunk_idx) 같은 고유 ID를 붙여 보내면, 재전송된 청크가 같은 key로 들어와 자연히 합쳐진다.

"exactly-once"는 보통 **전달**이 정확히 한 번이라는 뜻이 아니라, at-least-once 전달 + 멱등 처리로 **결과가** 정확히 한 번인 것처럼 보이게 만든다는 뜻이다 ("effectively-once"라고도 부른다). 예제 4에서 직접 확인한다.

### 2.5 흔한 함정

- bronze에 "파싱된 CSV"를 넣는다 → 원본이 아니므로 파서 버그를 되돌릴 수 없다.
- 업로드 API가 손상 청크를 버린다 → 나중에 "왜 이 기기만 드롭이 많지?"를 조사할 증거가 없다. 손상 청크도 DLQ 쪽 prefix에 보관한다.
- silver를 같은 경로에 덮어쓴다 → 그 파일로 만든 학습 데이터셋이 조용히 바뀐다 (예제 10에서 직접 본다).

---

## 3. 스키마와 메타데이터 — 데이터의 "계약"

### 3.1 어떤 메타데이터가 필요한가

센서 샘플 값만 있으면 쓸모가 없다. 모델팀이 던지는 질문은 거의 다 메타데이터에 대한 질문이다.

| 범주 | 필드 예 | 왜 필요한가 | 어디서 오나 |
|---|---|---|---|
| 식별 | device_id, session_id, segment_id, chunk_idx | 사용자·기기 단위 split(H5), 중복 제거, 순서 | 기기 헤더 |
| 펌웨어 | fw 버전, 로그 포맷 버전, 빌드 해시 | "fw 1.4 이상만", 파서 선택, 버그 영향 범위 | 기기 헤더 |
| 센서 설정 | ODR, full-scale(±4 g), 필터 설정, 센서 모델·축 매핑 | 단위 변환, 분포 차이 설명 | 기기 헤더 (또는 config hash) |
| 시계 | 기기 monotonic t0, UTC 앵커, 추정 ppm, 시계 소스 | 정렬·라벨 매칭 (G7) | 기기 + 폰 + 서버 수신 시각 |
| 품질 | 샘플 수, gap 수, 최대 gap, 드롭률, clipping 비율 | 데이터 필터링 (G7 7절, H7) | 파싱 워커가 계산 |
| 동의·정책 | consent 플래그·범위, 지역, 보관 기한 | 학습 사용 가능 여부, 삭제 (H6) | 계정 서비스 |
| 라벨 참조 | label_set_id, 라벨 버전, 어노테이터 | 라벨과 데이터의 결합 (H4) | 라벨링 도구 |
| 계보 | bronze key, sha256, parser_version, 처리 시각 | 재처리·재현성 | 파이프라인 |

원칙: **기기가 아는 것은 기기가 헤더에 적는다.** 서버에서 "그 시점의 fw 버전"을 다른 DB에서 조인해 추정하면 OTA 업데이트 경계에서 틀린다. Don이 SSD 테스트 결과에 드라이브가 보고한 FW rev를 그대로 기록했던 것과 같은 이유다.

### 3.2 스키마 진화(schema evolution)

**스키마 진화**는 "이미 쌓인 데이터와 이미 배포된 기기가 있는 상태에서 데이터 형식을 바꾸는 일"이다. fleet에는 항상 여러 fw 버전이 섞여 있다(예제 4의 시뮬레이션도 1.2.0~1.10.0 네 가지). 그래서 규칙이 필요하다.

| 변경 | 안전한가 | 방법 |
|---|---|---|
| 필드 **추가** (끝에) | 안전 | 새 버전 번호, 옛 데이터는 기본값/null |
| 필드 삭제 | 주의 | 읽는 쪽이 없는 필드를 기본값으로 처리할 수 있어야 함 |
| 필드 의미·단위 변경 | **위험** | 같은 이름을 재사용하지 말고 새 필드로 (예: `acc_raw` → `acc_mg`) |
| 필드 크기·순서 변경 | 위험 (고정 레이아웃) | 포맷 버전을 올리고 파서를 분기 |

protobuf·FlatBuffers·Avro 같은 스키마 언어는 "필드 번호로 식별, 모르는 필드는 건너뛰기, 없는 필드는 기본값" 규칙을 내장해서 이 문제를 상당 부분 해결한다 (H1에서 자세히). 이 노트는 펌웨어에서 흔한 **고정 레이아웃 C 구조체 헤더**를 가정하고, 버전 바이트로 파서를 분기하는 방법을 직접 만든다.

### 3.3 우리 실습용 청크 포맷

H1의 포맷을 단순화한 가상의 포맷이다 (Hark의 실제 포맷이 아니다).

```
offset  size  필드            v1   v2
0       4     magic "HKLG"     ●    ●
4       1     ver              1    2
5       1     hdr_len          40   48     ← 리더가 모르는 꼬리를 건너뛸 수 있게
6       1     sensor (1=IMU)   ●    ●
7       1     flags            ●    ●
8       4     device_id        ●    ●
12      4     fw (major<<16 | minor<<8 | patch)
16      4     session_id       ●    ●
20      4     chunk_idx        ●    ●
24      8     t0_us (기기 monotonic)
32      2     odr_hz           ●    ●
34      2     n (샘플 수)      ●    ●
36      1     acc_fs_g         ●    ●
37      3     pad
40      8     utc_ms           —    ●      ← v2에서 추가: 폰에서 받은 wall clock 앵커 (G7)
hdr_len 14×n  레코드: seq u16, ax ay az gx gy gz i16
끝      4     CRC32 (헤더+레코드)
```

v2는 **끝에 필드 하나를 추가**했다. 이게 스키마 진화의 가장 흔하고 안전한 형태다. 파서는 ver 바이트로 분기하고, v1 데이터에는 `utc_ms = None`(null)을 채운다.

### 3.4 코드로 확인 — 버전별 파서

**예제 2** — 무엇을 확인하나: v1·v2 청크를 만드는 시뮬레이터와 버전별로 분기하는 파서. 손상 검사(길이·magic·CRC·버전·레코드 길이)를 순서대로 하고, 재시도해도 소용없는 오류는 `Permanent` 예외로 구분한다. 이 두 모듈은 이후 모든 예제가 import한다.

```python
# h3fmt.py — 기기 청크 포맷 v1/v2 (H1의 단순화 버전) + 시뮬레이터
import struct, zlib, numpy as np
MAGIC = b"HKLG"
HDR_V1 = struct.Struct("<4sBBBBIIIIQHHB3x")    # 40 B
HDR_V2 = struct.Struct("<4sBBBBIIIIQHHB3xQ")   # 48 B: + utc_ms (폰에서 받은 wall clock 앵커)
REC = np.dtype([("seq", "<u2"), ("ax", "<i2"), ("ay", "<i2"), ("az", "<i2"),
                ("gx", "<i2"), ("gy", "<i2"), ("gz", "<i2")])          # 14 B/샘플
def fw_pack(s):  a, b, c = map(int, s.split(".")); return a << 16 | b << 8 | c
def fw_str(v):   return f"{v >> 16}.{(v >> 8) & 255}.{v & 255}"

def make_chunk(ver, dev, fw, sess, idx, t0_us, n=250, odr=50, seq0=0, utc_ms=0, rng=None):
    rng = rng or np.random.default_rng(dev * 1000 + idx)
    r = np.zeros(n, REC)
    r["seq"] = (seq0 + np.arange(n)) & 0xFFFF
    for c in ("ax", "ay", "gx", "gy", "gz"):
        r[c] = rng.normal(0, 60, n).astype(np.int16)
    r["az"] = (8192 + rng.normal(0, 60, n)).astype(np.int16)        # ±4 g → 1 g ≈ 8192 LSB
    hlen = HDR_V1.size if ver == 1 else HDR_V2.size                   # 리더가 모르는 꼬리를 건너뛸 수 있게
    fields = (MAGIC, ver, hlen, 1, 0, dev, fw_pack(fw), sess, idx, t0_us, odr, n, 4)
    hdr = HDR_V1.pack(*fields) if ver == 1 else HDR_V2.pack(*fields, utc_ms)
    body = hdr + r.tobytes()
    return body + struct.pack("<I", zlib.crc32(body))                 # trailer: CRC32
```

```python
# h3parse.py — 버전별 파서 (dispatch on header version byte)
import struct, zlib, numpy as np
from h3fmt import MAGIC, HDR_V1, HDR_V2, REC, fw_str
PARSER_VERSION = "2.1.0"
class Permanent(Exception): pass                 # 재시도해도 안 되는 오류 → DLQ

def parse_chunk(buf):
    if len(buf) < HDR_V1.size + 4:           raise Permanent("truncated")
    if buf[:4] != MAGIC:                      raise Permanent("bad_magic")
    body, (crc,) = buf[:-4], struct.unpack("<I", buf[-4:])
    if zlib.crc32(body) != crc:               raise Permanent("crc_mismatch")
    ver = buf[4]
    if ver == 1:   h = HDR_V1.unpack_from(body); utc_ms = None     # v1에는 없는 필드 → null
    elif ver == 2: h = HDR_V2.unpack_from(body); utc_ms = h[13]
    else:                                     raise Permanent(f"unknown_ver_{ver}")
    hdr_size = HDR_V1.size if ver == 1 else HDR_V2.size
    n = h[11]
    if len(body) - hdr_size != n * REC.itemsize: raise Permanent("length_mismatch")
    rec = np.frombuffer(body, REC, count=n, offset=hdr_size)
    meta = dict(fmt_ver=ver, device_id=h[5], fw=fw_str(h[6]), session_id=h[7],
                chunk_idx=h[8], t0_us=h[9], odr_hz=h[10], n=n, acc_fs_g=h[12], utc_ms=utc_ms)
    return meta, rec
```

```python
from h3fmt import make_chunk, HDR_V1, HDR_V2
from h3parse import parse_chunk
a = make_chunk(1, dev=17, fw="1.3.1", sess=501, idx=0, t0_us=1_000_000, n=4)
b = make_chunk(2, dev=42, fw="1.4.0", sess=902, idx=7, t0_us=5_000_000, n=4,
               utc_ms=1_790_000_000_000)
print("header v1/v2 =", HDR_V1.size, "/", HDR_V2.size, "B;  chunk bytes =", len(a), "/", len(b))
for buf in (a, b):
    meta, rec = parse_chunk(buf)
    print(meta)
    print("  seq", rec["seq"].tolist(), " az", rec["az"].tolist())
```

```text
header v1/v2 = 40 / 48 B;  chunk bytes = 100 / 108
{'fmt_ver': 1, 'device_id': 17, 'fw': '1.3.1', 'session_id': 501, 'chunk_idx': 0, 't0_us': 1000000, 'odr_hz': 50, 'n': 4, 'acc_fs_g': 4, 'utc_ms': None}
  seq [0, 1, 2, 3]  az [8183, 8174, 8150, 8168]
{'fmt_ver': 2, 'device_id': 42, 'fw': '1.4.0', 'session_id': 902, 'chunk_idx': 7, 't0_us': 5000000, 'odr_hz': 50, 'n': 4, 'acc_fs_g': 4, 'utc_ms': 1790000000000}
  seq [0, 1, 2, 3]  az [8173, 8230, 8156, 8210]
```

출력에서 볼 것: 같은 파서가 두 버전을 읽고, v1에는 `utc_ms: None`이 채워진다. 청크 크기는 헤더 40/48 B + 14 B × 4 + CRC 4 B = 100/108 B로 손계산과 같다. az ≈ 8192는 ±4 g 레인지에서 1 g(중력)다.

### 3.5 파서도 버전이 있다

포맷 버전(기기가 쓴 형식)과 **파서 버전**(서버가 읽는 코드)은 다르다. 같은 v2 포맷을 파서 2.0.0과 2.1.0이 다르게 읽을 수 있다(8절의 버그 시나리오). 그래서 silver의 모든 행·파일·catalog 행에 `parser_version`을 남긴다. 펌웨어로 말하면 "이 바이너리를 만든 컴파일러 버전·빌드 해시를 이미지 헤더에 박는 것"이다.

### 3.6 흔한 함정

- 필드 의미를 바꾸면서 버전을 안 올린다 → 같은 컬럼에 단위가 다른 값이 섞인다 (가장 찾기 어려운 버그).
- `hdr_len`이 없다 → 새 필드가 생기면 옛 파서가 레코드 시작 위치를 모른다. 8절의 버그가 정확히 이것이다.
- 센서 설정을 서버 config에서 추정 → 현장 기기가 다른 설정으로 돌았을 때 알 수 없다. 설정 전체를 넣기 부담스러우면 **config hash**(설정 구조체의 CRC)만이라도 헤더에 넣고 서버에서 해시 → 설정 테이블로 조인한다.

---

## 4. Object storage — 버킷, key, 불변 put, lifecycle

### 4.1 직관과 정의

**Object storage**(S3, GCS, Azure Blob)는 "key → 바이트 덩어리(object)" 사전이다. 파일시스템처럼 보이지만 디렉터리가 진짜로 있는 게 아니라, key 문자열에 `/`가 들어 있을 뿐이다.

| 용어 | 뜻 | 펌웨어 비유 |
|---|---|---|
| bucket | object의 최상위 이름공간, 권한·리전·lifecycle 단위 | flash의 파티션 |
| key | object 이름 (예: `bronze/imu/dt=2026-09-30/dev=00042/…`) | LBA 대신 이름으로 찾는 주소 |
| prefix | key의 앞부분, 나열(list)과 권한·lifecycle의 단위 | 디렉터리처럼 쓰는 key 앞부분 |
| object | 통째로 쓰고 통째로(또는 범위로) 읽는 값, 부분 수정 불가 | append 불가한 erase block |
| metadata/tags | object에 붙는 작은 key-value | 이미지 헤더 |

핵심 성질 세 가지:

1. **부분 수정이 없다.** object는 통째로 새로 쓴다. 그래서 "청크 단위로 새 object"가 자연스럽고, 큰 파일에 append하는 설계는 맞지 않는다.
2. **일관성**: AWS S3는 2020년 말부터 PUT 직후 읽기·나열이 강한 일관성(strong read-after-write consistency)을 제공한다고 발표했다. GCS도 강한 일관성을 문서화하고 있다. 다만 서비스·기능별로 세부가 다르니 쓰는 서비스 문서를 확인한다.
3. **처리량은 병렬로 얻는다.** object 하나의 지연은 수십 ms 수준이지만, 많은 object를 동시에 읽고 쓰면 대역폭이 거의 선형으로 늘어난다. SSD의 queue depth를 올려 IOPS를 얻는 것과 같은 사고방식이다.

### 4.2 key 설계

key는 나중에 **"무엇을 한 번에 나열·삭제·이동해야 하나"**를 기준으로 짠다.

```
bronze/imu/dt=2026-09-30/dev=00042/sess=902/chunk=000007.hklg
│      │   │              │         │        └─ 청크 번호: 같은 청크 → 같은 key (멱등)
│      │   │              │         └─ 세션
│      │   │              └─ 기기: "이 기기 전부 삭제"(H6)를 prefix로
│      │   └─ 날짜: lifecycle·재처리 범위를 날짜로
│      └─ 센서 종류: 권한·보관 정책이 센서마다 다를 수 있다 (오디오 ≠ IMU)
└─ 레이어
```

`dt=2026-09-30` 같은 `이름=값` 형식을 **hive 스타일 파티션**이라고 부른다. Spark·DuckDB·pyarrow 같은 도구가 경로에서 컬럼 값을 자동으로 읽어 간다(6절).

**순서 선택은 trade-off**다. `dt`가 먼저면 "하루치 재처리"와 lifecycle이 쉽고, `dev`가 먼저면 "한 사용자 삭제"가 prefix 하나로 끝난다. 둘 다 필요하면 하나는 key로, 다른 하나는 catalog 인덱스(7절)로 푼다.

**hot prefix**: 예전 S3는 key 앞부분이 비슷하면 같은 내부 파티션에 몰려 느려진다고 해서 key 앞에 해시를 붙이라는 조언이 많았다. 현재 AWS 문서는 prefix당 초당 수천 건(PUT 계열 약 3,500, GET 계열 약 5,500)을 지원하고 부하에 따라 자동으로 나눈다고 설명한다. 10k 기기 × 4 MB 청크 수준(예제 1 기준 초당 수십 건)이면 문제가 안 될 가능성이 높다. 다만 한 prefix에 갑자기 몰리는 경우(모든 기기가 자정에 같은 `dt=` prefix로 업로드)는 여전히 관찰 대상이다. 단정하지 말고 "서비스의 현재 가이드를 확인하고 측정한다"가 맞는 답이다.

### 4.3 업로드 경로 — 기기가 버킷에 직접 쓰지 않는다

기기에 클라우드 비밀키를 넣으면 안 된다. 흔한 패턴은:

1. 기기(또는 폰 앱)가 Upload API에 인증하고 "청크 (dev, sess, idx), 크기, sha256"을 알린다.
2. API가 그 key 하나에만 쓸 수 있는 **presigned URL**(짧은 시간만 유효한 서명된 업로드 주소)을 준다.
3. 기기가 그 URL로 직접 PUT → API 서버가 대역폭 병목이 되지 않는다.
4. 저장소가 "object created" 이벤트를 큐로 보낸다 (S3 event notification → SQS/SNS/EventBridge, GCS → Pub/Sub notification).

큰 파일은 multipart/resumable upload(나눠 올리고 끊기면 이어서)를 쓴다 — H2의 재개(resume)가 서버 쪽에서는 이 기능과 대응한다. 이 부분이 **IT·보안·네트워크 팀과 협업하는 지점**이다: 인증서, 방화벽, VPC endpoint, 버킷 정책, 암호화 키(KMS), 접근 로그. Don이 테스트 인프라에서 겪은 그 협의가 그대로 반복된다.

### 4.4 코드로 확인 — 불변 put과 멱등 업로드

**예제 3** — 무엇을 확인하나: 파일시스템을 object storage처럼 쓰는 작은 클래스. key가 이미 있으면 내용 해시를 비교해 "같은 재전송"과 "같은 key인데 다른 내용(충돌)"을 구분하고, 임시 파일 + 원자적 link로 반쯤 쓰인 object가 보이지 않게 한다.

```python
# objstore.py — 파일시스템을 'object storage'처럼: 불변(put-if-absent) + 원자적 쓰기 + sha256 검사
import hashlib, os, tempfile
class LocalObjectStore:
    def __init__(self, root): self.root = root
    def _path(self, key):     return os.path.join(self.root, key)
    def put(self, key, data):
        p, sha = self._path(key), hashlib.sha256(data).hexdigest()
        if os.path.exists(p):                                 # 같은 key가 이미 있다
            old = hashlib.sha256(open(p, "rb").read()).hexdigest()
            return ("exists_same" if old == sha else "CONFLICT"), sha
        os.makedirs(os.path.dirname(p), exist_ok=True)
        fd, tmp = tempfile.mkstemp(dir=os.path.dirname(p))   # 임시 파일에 쓰고
        with os.fdopen(fd, "wb") as f: f.write(data)
        try:    os.link(tmp, p)                               # 원자적 'create-if-absent'
        except FileExistsError: os.unlink(tmp); return self.put(key, data)
        os.unlink(tmp); return "created", sha
    def get(self, key):       return open(self._path(key), "rb").read()
    def list(self, prefix):
        base = self._path(prefix)
        for d, _, fs in os.walk(base):
            for f in sorted(fs): yield os.path.relpath(os.path.join(d, f), self.root)
def raw_key(m, day):          # 결정적(deterministic) key: 같은 청크 → 항상 같은 key
    return (f"bronze/imu/dt={day}/dev={m['device_id']:05d}/"
            f"sess={m['session_id']}/chunk={m['chunk_idx']:06d}.hklg")
```

```python
import shutil
from objstore import LocalObjectStore, raw_key
from h3fmt import make_chunk
from h3parse import parse_chunk
shutil.rmtree("/private/tmp/claude-501/h3/s3_ex3", ignore_errors=True)
store = LocalObjectStore("/private/tmp/claude-501/h3/s3_ex3")
c = make_chunk(2, dev=42, fw="1.4.0", sess=902, idx=7, t0_us=5_000_000, utc_ms=1)
meta, _ = parse_chunk(c)
key = raw_key(meta, "2026-09-30")
print(key)
print("1st upload   :", store.put(key, c)[0])
print("retry (same) :", store.put(key, c)[0])              # 기기가 ACK를 못 받고 재전송
bad = bytearray(c); bad[100] ^= 0xFF                        # 같은 key, 다른 내용
print("same key, diff bytes:", store.put(key, bytes(bad))[0])
print("objects:", list(store.list("bronze/")))
```

```text
bronze/imu/dt=2026-09-30/dev=00042/sess=902/chunk=000007.hklg
1st upload   : created
retry (same) : exists_same
same key, diff bytes: CONFLICT
objects: ['bronze/imu/dt=2026-09-30/dev=00042/sess=902/chunk=000007.hklg']
```

출력에서 볼 것: 기기가 ACK를 못 받아 같은 청크를 다시 보내도 object는 하나뿐이다 (`exists_same`). 같은 key에 다른 바이트가 오면 덮어쓰지 않고 `CONFLICT`로 알린다 — 펌웨어가 chunk_idx를 재사용하는 버그(예: 재부팅 후 카운터 리셋)를 잡아내는 경보가 된다.

클라우드에서는 같은 동작을 **조건부 쓰기**로 얻는다. GCS는 `ifGenerationMatch=0` precondition으로 "없을 때만 생성"을 지원하고, S3도 2024년에 `If-None-Match` 조건부 PUT을 지원하기 시작한 것으로 알고 있다 (세부 지원 범위는 문서 확인). 또 버킷 versioning이나 object lock(WORM)으로 "실수로 덮어써도 이전 버전이 남게" 할 수 있다.

### 4.5 Lifecycle — 오래된 데이터는 싸게, 기한이 지나면 삭제

**Lifecycle rule**은 "prefix X 아래 object가 N일 지나면 storage class를 바꾸거나 삭제"하는 버킷 설정이다. 예를 들어:

| prefix | 규칙 (예) | 이유 |
|---|---|---|
| `bronze/imu/` | 90일 뒤 cold(archive), 2년 뒤 삭제 | 재처리는 대부분 최근 데이터, 오래된 원본은 가끔 |
| `bronze/audio/` | 30일 뒤 삭제 (동의 범위에 따라) | 오디오는 PII 위험이 커서 보관 기간을 짧게 (H6) |
| `staging/` | 7일 뒤 삭제 | compaction 끝난 작은 파일 |
| `dlq/` | 180일 보관 | 포렌식용 |

예제 1의 1년 누적 계산(hot 180 TB + cold 540 TB → 월 약 $4,140 + $540, 가정 단가)이 lifecycle의 효과다. 함정: cold 계층은 **꺼낼 때 비용·시간**(수 시간이 걸리는 계층도 있다)과 **최소 보관 기간**이 붙는 경우가 많아서, "오래된 데이터로 재처리"를 자주 하면 오히려 비싸다. 재처리 빈도를 보고 정한다.

삭제 의무(H6): lifecycle은 "기간 만료 삭제"만 한다. 사용자 요청 삭제는 catalog에서 그 사용자의 key 목록을 찾아 지우는 별도 작업이 필요하다. 그리고 silver·gold·manifest에도 그 사용자의 데이터가 퍼져 있으므로 **계보(lineage)를 남겨야 지울 수 있다** (9.5절).

---

## 5. 큐와 워커 — at-least-once, 재시도, DLQ, backpressure

### 5.1 직관 — 챔버 스케줄러와 같다

Don의 챔버 스케줄러를 떠올리면 된다. 테스트 작업이 큐에 쌓이고, 빈 챔버(워커)가 하나씩 가져가서 돌리고, 끝나면 "완료"를 보고한다. 챔버가 중간에 죽으면 그 작업은 다시 큐로 돌아가야 하고, 몇 번을 다시 돌려도 실패하는 작업은 사람이 보도록 따로 빼야 한다. 메시지 큐도 똑같다.

### 5.2 정의 — 전달 보장(delivery guarantee)

| 보장 | 뜻 | 대가 |
|---|---|---|
| at-most-once | 최대 한 번. 잃어버릴 수 있다 | 처리 전에 ack → 워커가 죽으면 유실 |
| at-least-once | 최소 한 번. **중복이 올 수 있다** | 처리 후 ack → ack 전에 죽으면 다른 워커가 다시 받음 |
| exactly-once | 결과가 정확히 한 번 | at-least-once + 멱등 처리 (또는 특정 시스템 내부의 트랜잭션) |

대부분의 실전 시스템은 **at-least-once + 멱등 소비자**를 쓴다. 이유는 간단하다: "처리했는데 ack가 유실된" 경우를 구분할 방법이 없기 때문이다. SSD 펌웨어에서 호스트가 명령 완료(CQ entry)를 못 받으면 같은 명령을 재발행할 수 있으므로 펌웨어가 같은 쓰기를 두 번 받아도 결과가 같게 만드는 것과 같은 문제다.

### 5.3 주요 시스템 개념 (개념 수준 — 세부는 문서 확인)

| 개념 | Kafka | Google Pub/Sub | AWS SQS |
|---|---|---|---|
| 모델 | 파티션된 append-only 로그, 소비자가 offset을 커밋 | topic → subscription, 메시지마다 ack | 큐, 메시지마다 receive → delete |
| 재전달 | offset을 커밋 안 하고 죽으면 그 지점부터 다시 | ack deadline 안에 ack 없으면 재전달 | visibility timeout 지나면 다시 보임 |
| 순서 | 파티션 안에서 보장, key로 파티션 결정 | ordering key 옵션 | Standard는 best-effort, FIFO 큐는 message group 단위 |
| 실패 메시지 | 보통 별도 "DLQ topic"을 앱이 구현 | dead-letter topic (최대 전달 시도 횟수) | redrive policy (maxReceiveCount) → DLQ |
| 재처리 | 보존 기간 동안 offset을 되돌려 replay | seek(시각/스냅샷)으로 replay | DLQ redrive |
| exactly-once 관련 | idempotent producer·트랜잭션 (Kafka 내부) | exactly-once delivery 옵션 (조건 있음) | FIFO 큐의 dedup ID (일정 시간 창) |

말로 하면: Kafka는 "로그를 여러 소비자가 각자 읽는" 모델이라 replay와 대용량 스트림에 강하고, SQS·Pub/Sub는 "일감 하나씩 나눠주는" 모델이라 운영이 단순하다. 10k 기기 × 청크 단위 이벤트(초당 수십~수백 건) 정도면 managed 큐(SQS·Pub/Sub)로 충분할 가능성이 높고, 센서 샘플을 실시간 스트림으로 흘리고 여러 소비자(실시간 대시보드, 이상 탐지, 저장)가 붙는 구조라면 Kafka 계열을 고려한다. 정답은 규모·팀 역량·클라우드에 따라 다르다.

**큐에 무엇을 넣나**: 데이터 자체가 아니라 **포인터**(bucket + key + 크기 + sha256)를 넣는다. 메시지 크기 제한(서비스마다 수백 KB~수 MB)이 있고, 데이터는 이미 object store에 불변으로 있기 때문이다. DMA descriptor에 버퍼 주소만 넣는 것과 같다.

### 5.4 실패의 두 종류와 DLQ

| 종류 | 예 | 처리 |
|---|---|---|
| 일시 오류 (transient) | 저장소 timeout, 네트워크 끊김, DB lock | 지수 백오프(exponential backoff)로 재시도 |
| 영구 오류 (permanent) | CRC 불일치, 모르는 포맷 버전, 잘린 파일 | 재시도해도 같음 → 바로 DLQ |
| 둘을 구분 못 한 경우 | — | 최대 시도 횟수(예: 3~5회) 후 DLQ |

**Poison message**는 "처리할 때마다 워커를 죽이거나 실패하는 메시지"다. 최대 시도 횟수가 없으면 이 메시지 하나가 영원히 재전달되며 워커를 붙잡는다. **DLQ**(dead-letter queue)는 그런 메시지를 격리해 두는 큐로, 사람이 원인을 고친 뒤 다시 흘려보낸다(redrive).

```svg
<svg viewBox="0 0 680 330" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="h3c" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="22" font-size="13">메시지 하나의 운명 (예제 4 첫 실행의 실제 개수)</text><rect x="10" y="130" width="120" height="60" rx="6" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="70" y="155" font-size="13" text-anchor="middle">큐</text><text x="70" y="174" font-size="12" text-anchor="middle">1,048 전달</text><rect x="190" y="130" width="130" height="60" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="255" y="155" font-size="13" text-anchor="middle">워커</text><text x="255" y="174" font-size="12" text-anchor="middle">ledger 확인 → 파싱</text><line x1="130" y1="160" x2="188" y2="160" stroke="currentColor" marker-end="url(#h3c)"/><rect x="420" y="40" width="250" height="44" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/>
<text x="545" y="67" font-size="13" text-anchor="middle">ok → silver staging, ledger 기록 (947)</text><rect x="420" y="100" width="250" height="44" rx="6" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="545" y="127" font-size="13" text-anchor="middle">중복 → 건너뜀 / 경합 흡수 (45)</text><rect x="420" y="160" width="250" height="44" rx="6" fill="#e08a3c" fill-opacity="0.2" stroke="currentColor"/><text x="545" y="187" font-size="13" text-anchor="middle">일시 오류 → attempt+1 재투입 (59)</text><rect x="420" y="220" width="250" height="60" rx="6" fill="#d0564a" fill-opacity="0.15" stroke="currentColor" stroke-dasharray="5 4"/><text x="545" y="244" font-size="13" text-anchor="middle">DLQ (56 메시지 / 53 key)</text><text x="545" y="264" font-size="12" text-anchor="middle">CRC·magic·버전 오류, 3회 실패</text><line x1="320" y1="148" x2="418" y2="62" stroke="currentColor" marker-end="url(#h3c)"/><line x1="320" y1="155" x2="418" y2="122" stroke="currentColor" marker-end="url(#h3c)"/>
<line x1="320" y1="168" x2="418" y2="182" stroke="currentColor" marker-end="url(#h3c)"/><line x1="320" y1="178" x2="418" y2="246" stroke="#d0564a" marker-end="url(#h3c)"/><path d="M 545 204 Q 545 300 70 300 L 70 192" fill="none" stroke="#e08a3c" stroke-dasharray="5 4" marker-end="url(#h3c)"/><text x="230" y="294" font-size="12">재시도: 큐로 돌아감 (backoff)</text><text x="10" y="322" font-size="12">1,048 = 업로드 1,000 + 브로커 중복 전달 48. 재시도 59회는 이 1,048에 더해진 추가 전달이다.</text>
</svg>
```

그림 3 — 큐에서 나온 메시지 하나가 갈 수 있는 네 갈래. 숫자는 예제 4 첫 실행의 실제 출력에서 가져왔다 (ok 947, 중복 6 + 39 = 45, 재시도 59, DLQ 56).

### 5.5 Backpressure와 ordering key

**Backpressure**(배압)는 "소비자가 못 따라갈 때 생산자를 늦추거나 버퍼에 쌓는 메커니즘"이다. 펌웨어로 말하면 링버퍼가 차면 DMA를 멈추거나 오래된 샘플을 버리는 정책이다. ingestion에서는:

- 큐 자체가 거대한 버퍼다. 큐 길이(backlog)와 **가장 오래된 메시지의 나이**(oldest message age)를 지표로 삼는다.
- 워커 수를 backlog에 따라 자동으로 늘린다(autoscaling). 예제 1의 "피크 4배 → 워커 5개" 같은 계산이 기준선이다.
- 워커가 동시에 가져가는 메시지 수(prefetch, in-flight 한도)를 제한해 한 워커가 메모리를 터뜨리지 않게 한다.
- 기기 쪽: 업로드 API가 429(too many requests)를 돌려주면 기기가 지수 백오프 + jitter로 재시도한다 (H2). 10k 기기가 동시에 재시도하면 "thundering herd"가 생기므로 jitter가 필수다.

**Ordering key**: 같은 세션의 청크가 순서대로 처리돼야 하나? 청크마다 독립적으로 파싱하고(seq·t0가 헤더에 있으니), 정렬은 compaction이나 질의 시에 하면 **순서 보장이 필요 없다**. 순서 보장은 병렬성을 깎으므로(같은 key는 한 파티션·한 소비자) 가능하면 설계로 없앤다. 꼭 필요하면 ordering key = session_id로 세션 단위만 순서를 보장한다.

### 5.6 코드로 확인 — 1,000개 업로드, 워커 4개, DLQ

시뮬레이션 설정: 기기 50대 × 청크 20개 = 1,000개 업로드. fw 4종(1.2.0·1.3.1은 포맷 v1, 1.4.0·1.10.0은 v2). 업로드의 약 4%를 손상시킨다(비트 플립, 잘림, magic 손상, 개발 빌드가 만든 포맷 v3). 큐는 메시지 약 5%를 두 번 전달하고(at-least-once), 첫 시도의 4%는 "저장소 timeout" 일시 오류를 낸다(재시도 때는 50% — 장애가 계속되는 상황). 무작위성은 key 해시로 정해서 실행마다 같다.

**예제 4** — 무엇을 확인하나: (a) 업로드: 1,000개 청크를 bronze에 받은 그대로 저장, (b) 워커 모듈: ledger(SQLite `PRIMARY KEY`)로 중복 건너뛰기, key에서 결정된 출력 경로, 일시/영구 오류 구분, (c) 실행: 워커 thread 4개, 재시도 3회, DLQ 집계 — 그리고 장애가 풀린 뒤 이벤트 전체를 다시 흘렸을 때(replay) 결과가 망가지지 않는지. 아래 세 블록은 차례로 `ex4_upload.py`, `ex4_workers.py`, `ex4_run.py`로 저장한다.

```python
# 1,000개 업로드 시뮬레이션: 기기 50대 × 청크 20개, 일부는 손상 (bronze에 '받은 그대로' 저장)
import shutil, numpy as np
from objstore import LocalObjectStore
from h3fmt import make_chunk
ROOT = "/private/tmp/claude-501/h3/lake"; shutil.rmtree(ROOT, ignore_errors=True)
store, rng = LocalObjectStore(ROOT), np.random.default_rng(7)
FW = ["1.2.0", "1.3.1", "1.4.0", "1.10.0"]                  # 1.4.0부터 포맷 v2
msgs, kinds = [], {}
for dev in range(50):
    fw = FW[dev % 4]; ver = 1 if fw in ("1.2.0", "1.3.1") else 2
    day = f"2026-09-{28 + dev % 3:02d}"; sess = 1000 + dev
    for idx in range(20):
        c = bytearray(make_chunk(ver, dev, fw, sess, idx, t0_us=idx * 5_000_000,
                                 seq0=idx * 250, utc_ms=1_790_000_000_000 + idx * 5000))
        u, kind = rng.random(), "ok"
        if   u < 0.020: c[200] ^= 0x10; kind = "crc"          # 비트 플립
        elif u < 0.030: c = c[:-1000]; kind = "trunc"         # 업로드 중간에 끊김
        elif u < 0.035: c[:4] = b"XXXX"; kind = "magic"
        elif u < 0.040: c = bytearray(make_chunk(3, dev, fw, sess, idx, 0)); kind = "ver3"  # 개발 빌드
        key = f"bronze/imu/dt={day}/dev={dev:05d}/sess={sess}/chunk={idx:06d}.hklg"
        store.put(key, bytes(c)); msgs.append(key); kinds[kind] = kinds.get(kind, 0) + 1
print("uploaded objects:", len(msgs), " injected:", kinds)
import json; json.dump(msgs, open(f"{ROOT}/_events.json", "w"))
```

```python
# 큐 + 워커 4개: at-least-once 전달(중복 5%), 일시 오류 재시도, 영구 오류 → DLQ, ledger로 dedup
import json, queue, sqlite3, threading, hashlib, time, os, numpy as np, pyarrow as pa, pyarrow.parquet as pq
from objstore import LocalObjectStore
from h3parse import parse_chunk, Permanent
ROOT = "/private/tmp/claude-501/h3/lake"; store = LocalObjectStore(ROOT)
LEDGER = f"{ROOT}/_ledger.db"; MAX_TRY = 3; TRANSIENT = os.environ.get("OUTAGE", "1") == "1"
db = sqlite3.connect(LEDGER); db.execute("CREATE TABLE IF NOT EXISTS done(key TEXT PRIMARY KEY, out TEXT)"); db.commit()
def h(s): return int(hashlib.md5(s.encode()).hexdigest()[:8], 16) / 2**32    # 결정적 '난수'
q, dlq, stats, lock = queue.Queue(), [], dict(ok=0, dup_skip=0, dup_race=0, retry=0), threading.Lock()
for k in json.load(open(f"{ROOT}/_events.json")):
    q.put((k, 1))
    if h("dup" + k) < 0.05: q.put((k, 1))                # 브로커가 같은 메시지를 두 번 준다
def handle(con, key, attempt):
    if con.execute("SELECT 1 FROM done WHERE key=?", (key,)).fetchone():
        return "dup_skip"
    if TRANSIENT and h(f"io{key}{attempt}") < (0.04 if attempt == 1 else 0.5):
        raise TimeoutError("object store read timeout")  # 일시 오류(transient)
    meta, rec = parse_chunk(store.get(key))
    t = pa.table({c: rec[c] for c in rec.dtype.names}).append_column(
        "t_us", pa.array(meta["t0_us"] + np.arange(meta["n"], dtype=np.int64) * 1_000_000 // meta["odr_hz"]))
    for f in ("device_id", "session_id", "chunk_idx", "fw"): t = t.append_column(f, pa.array([meta[f]] * meta["n"]))
    out = f"{ROOT}/staging/{hashlib.sha1(key.encode()).hexdigest()}.parquet"  # key → 결정적 경로
    os.makedirs(os.path.dirname(out), exist_ok=True); pq.write_table(t, out)   # 덮어써도 결과 같음
    cur = con.execute("INSERT OR IGNORE INTO done VALUES(?,?)", (key, out)); con.commit()
    return "ok" if cur.rowcount == 1 else "dup_race"   # 두 워커가 동시에 같은 key를 처리한 경우
```

```python
from ex4_workers import *
def worker():
    con = sqlite3.connect(LEDGER, timeout=30)
    while True:
        key, attempt = q.get()
        try:
            r = handle(con, key, attempt)
            with lock: stats[r] += 1
        except Permanent as e:                       # 포맷 오류: 재시도 무의미 → 바로 DLQ
            with lock: dlq.append((key, str(e)))
        except TimeoutError:
            with lock:
                if attempt < MAX_TRY: stats["retry"] += 1; q.put((key, attempt + 1))
                else: dlq.append((key, "max_retries"))
        finally: q.task_done()
t0 = time.perf_counter()
for _ in range(4): threading.Thread(target=worker, daemon=True).start()
q.join(); dt = time.perf_counter() - t0
reasons = {}
for _, r in dlq: reasons[r] = reasons.get(r, 0) + 1
n_done = db.execute("SELECT COUNT(*) FROM done").fetchone()[0]
print("stats:", stats)
print("DLQ:", len(dlq), "msgs /", len({k for k, _ in dlq}), "keys", dict(sorted(reasons.items())))
print("ledger rows:", n_done, " staging files:", len(os.listdir(f"{ROOT}/staging")), f" wall {dt:.2f}s")
json.dump(dlq, open(f"{ROOT}/_dlq.json", "w"))
```

실행 순서: 업로드 스크립트 → 실행 스크립트(장애 중) → `OUTAGE=0`으로 실행 스크립트를 한 번 더(장애가 풀린 뒤 이벤트 로그 전체 replay).

```text
uploaded objects: 1000  injected: {'ok': 956, 'crc': 17, 'ver3': 8, 'trunc': 8, 'magic': 11}
stats: {'ok': 947, 'dup_skip': 6, 'dup_race': 39, 'retry': 59}
DLQ: 56 msgs / 53 keys {'bad_magic': 12, 'crc_mismatch': 26, 'max_retries': 10, 'unknown_ver_3': 8}
ledger rows: 947  staging files: 947  wall 0.84s
--- replay all events, outage fixed (OUTAGE=0) ---
stats: {'ok': 9, 'dup_skip': 992, 'dup_race': 1, 'retry': 0}
DLQ: 46 msgs / 44 keys {'bad_magic': 12, 'crc_mismatch': 26, 'unknown_ver_3': 8}
ledger rows: 956  staging files: 956  wall 0.24s
```

출력에서 볼 것:

- **DLQ 사유가 주입한 손상과 맞는다.** `crc_mismatch` 26 = CRC 비트 플립 17 + 잘림 8(잘린 파일은 마지막 4바이트를 CRC로 읽으니 CRC 불일치로 잡힌다) + 중복 전달 1. `bad_magic` 12 = 11 + 중복 1, `unknown_ver_3` 8. 그리고 `max_retries` 10 = 일시 오류가 3번 연속 난 메시지(9개 key + 중복 1).
- **중복은 ledger가 흡수한다.** 정상 key 947개에 대해 중복 전달 45건(6 + 39)이 있었지만 ledger와 staging 파일은 947개다. 여기서 `dup_race` 39가 중요하다. 중복 메시지가 큐에 연달아 들어가서 **두 워커가 거의 동시에** 같은 key를 가져갔고, 둘 다 ledger 확인을 통과했다 (check-then-act 경합). 그래도 출력 경로가 key로 결정되므로 같은 파일을 같은 내용으로 덮어썼고, `INSERT OR IGNORE`가 두 번째 기록을 무시했다. **"먼저 확인"만으로는 멱등이 안 되고, 쓰기 자체가 멱등이어야 한다**는 걸 보여 준다. 6과 39의 나뉨은 thread 스케줄링에 따라 실행마다 조금씩 바뀐다 (다른 실행에서 9/36, 10/35를 봤다). 합 45와 ledger 947은 항상 같다.
- **Replay가 안전하다.** 장애가 풀린 뒤 이벤트 1,000개 전체(중복 포함 1,048 전달)를 다시 흘리면, 이미 처리된 것은 전부 건너뛰고(992 + 1) `max_retries`로 DLQ에 갔던 9개만 새로 처리된다 → ledger 956 = 정상 청크 956개와 정확히 일치. 영구 오류는 다시 DLQ로 간다. 이게 "DLQ redrive" 또는 "전체 replay"를 겁 없이 할 수 있는 이유다.
- 처리 시간 0.84 s는 이 Mac에서의 한 번 측정값일 뿐이다 (청크 1,000개 × 약 3.5 KB).

### 5.7 임베디드 연결과 함정

- Python thread는 GIL 때문에 CPU 작업에서 병렬이 제한된다. 실제 워커는 프로세스·컨테이너 단위로 늘린다. 여기서는 개념(경합·중복·재시도)을 보는 게 목적이다.
- ledger를 처리 **전에** 기록하면(at-most-once) 워커가 죽었을 때 그 청크는 영원히 처리되지 않는다. 반드시 **출력 쓰기 → ledger 기록 → ack** 순서다. 펌웨어의 "데이터 쓰기 → 메타데이터 커밋" 순서(전원 차단 안전)와 같다.
- 재시도에 jitter가 없으면 장애 복구 순간 모든 워커·기기가 동시에 몰린다.
- DLQ를 만들고 아무도 안 본다 → DLQ 크기와 사유별 개수를 H7 대시보드에 올리고 경보를 건다. `unknown_ver_3`이 갑자기 늘면 "개발 빌드가 dogfood fleet에 나갔다"는 신호다.

---

## 6. 컬럼 저장 — Parquet, 파티셔닝, pushdown, small files

### 6.1 직관 — 행으로 저장하느냐, 열로 저장하느냐

기기 로그는 **행(row) 단위**로 생긴다: 샘플 하나 = (seq, ax, ay, az, gx, gy, gz). 그런데 분석·학습은 대부분 **열(column) 단위**로 읽는다: "az만 보고 싶다", "device 23만", "9월 3일만". 행 저장이면 az 하나를 읽으려고 14바이트 레코드 전체를 읽어야 한다.

C로 말하면 **AoS(array of structs) vs SoA(struct of arrays)**다. DSP에서 SIMD를 쓰려고 AoS를 SoA로 바꾸는 이유(같은 종류 값이 연속이면 한꺼번에 처리하기 좋다)가 저장에서도 똑같이 통한다. 같은 열의 값은 비슷하므로 압축도 훨씬 잘 된다.

### 6.2 정의 — Parquet 파일의 구조

**Parquet**은 Apache의 오픈 컬럼 저장 파일 포맷이다. 구조는:

- 파일 = **row group** 여러 개 + **footer**
- row group = 행 묶음(예: 15만 행). 그 안에서 열마다 **column chunk** 하나
- column chunk = **page** 여러 개 (인코딩·압축 단위)
- footer = 스키마 + 각 row group·column chunk의 위치, 크기, **통계(min/max, null 수)**

```svg
<svg viewBox="0 0 680 320" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="22" font-size="13">행 저장(바이너리 로그·CSV) vs Parquet 컬럼 저장</text><text x="20" y="50" font-size="12">행 저장: 레코드가 연속</text><rect x="20" y="60" width="40" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><rect x="60" y="60" width="40" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><rect x="100" y="60" width="40" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="140" y="60" width="40" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><rect x="180" y="60" width="40" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><rect x="220" y="60" width="40" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="260" y="60" width="40" height="22" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><rect x="300" y="60" width="40" height="22" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/>
<rect x="340" y="60" width="40" height="22" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="40" y="76" font-size="12" text-anchor="middle">t</text><text x="80" y="76" font-size="12" text-anchor="middle">dev</text><text x="120" y="76" font-size="12" text-anchor="middle">az</text><text x="160" y="76" font-size="12" text-anchor="middle">t</text><text x="200" y="76" font-size="12" text-anchor="middle">dev</text><text x="240" y="76" font-size="12" text-anchor="middle">az</text><text x="280" y="76" font-size="12" text-anchor="middle">t</text><text x="320" y="76" font-size="12" text-anchor="middle">dev</text><text x="360" y="76" font-size="12" text-anchor="middle">az</text>
<text x="395" y="76" font-size="12">… az만 필요해도 전부 읽음</text><text x="20" y="120" font-size="12">Parquet 파일</text><rect x="20" y="130" width="420" height="70" rx="4" fill="none" stroke="currentColor"/><text x="30" y="148" font-size="12">row group 0 (dev 0~9)</text><rect x="30" y="156" width="120" height="34" fill="#e08a3c" fill-opacity="0.35" stroke="currentColor"/><rect x="160" y="156" width="120" height="34" fill="#4a7bd0" fill-opacity="0.35" stroke="currentColor"/><rect x="290" y="156" width="140" height="34" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><text x="90" y="178" font-size="12" text-anchor="middle">t 열 (pages)</text><text x="220" y="178" font-size="12" text-anchor="middle">dev 열 (RLE)</text><text x="360" y="178" font-size="12" text-anchor="middle">az 열 (pages)</text>
<rect x="20" y="206" width="420" height="40" rx="4" fill="none" stroke="currentColor"/><text x="30" y="231" font-size="12">row group 1 (dev 10~19) … 같은 구조 반복</text><rect x="20" y="252" width="420" height="40" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="30" y="270" font-size="12">footer: 스키마, 각 column chunk의 offset·크기,</text><text x="30" y="286" font-size="12">통계 min/max (예: RG0 device_id 0..9) → 읽기 전에 건너뛸지 판단</text><text x="460" y="150" font-size="12">① 열 선택: az만 → az chunk만 읽음</text><text x="460" y="176" font-size="12">② 통계: dev=23이면 RG0·RG1</text><text x="472" y="194" font-size="12">건너뜀 (min/max 밖)</text><text x="460" y="220" font-size="12">③ 인코딩: dictionary, RLE,</text><text x="472" y="238" font-size="12">bit-packing + 압축(snappy·zstd)</text>
<text x="460" y="264" font-size="12">④ 리더는 footer를 먼저 읽는다</text><text x="472" y="282" font-size="12">(파일 끝 → range read)</text>
</svg>
```

그림 4 — 행 저장과 Parquet. Parquet은 row group 안에서 열마다 따로 저장하고, footer에 위치와 min/max 통계를 둔다. 리더는 footer를 먼저 읽어 필요한 열·row group만 골라 읽는다 (object storage에서는 byte range GET).

Parquet이 주는 네 가지:

| 기능 | 뜻 | 센서 로그에서 |
|---|---|---|
| column pruning (projection) | 필요한 열만 읽기 | "az만"이면 7개 센서 열 중 1개 |
| 인코딩 | dictionary(반복 값 → 번호), RLE(같은 값 연속 → (값, 개수)) | device_id·fw·session_id가 거의 공짜 |
| 압축 | page 단위 snappy(빠름)·zstd(작음)·gzip | 정렬하면 더 작아짐 |
| 통계 + predicate pushdown | row group·page의 min/max로 조건에 안 맞는 부분을 아예 안 읽음 | "device 23", "t가 이 구간" |

**Predicate pushdown**은 "필터 조건(predicate)을 저장소 읽기 단계까지 내려보내서(push down) 읽기 전에 거르는 것"이다. 다 읽고 pandas로 거르는 것과 정반대다.

### 6.3 코드로 확인 — 3백만 행: 바이너리·CSV·Parquet 크기

**예제 5** — 무엇을 확인하나: 기기 40대 × 5일 × 5분(50 Hz) = 3,000,000 샘플을 같은 데이터로 CSV, CSV.gz, Parquet(snappy·zstd, `dt`로 hive 파티션)으로 쓰고 크기를 비교한다. 신호는 "느린 움직임(25샘플 이동평균한 잡음) + 센서 잡음 8 LSB"로 만든다.

```python
# bronze(바이너리 청크) → silver(Parquet, dt 파티션) 대량 변환 + 포맷별 크기 비교
import os, shutil, glob, numpy as np, pandas as pd, pyarrow as pa, pyarrow.dataset as ds
from h3fmt import REC
OUT = "/private/tmp/claude-501/h3/bulk"; shutil.rmtree(OUT, ignore_errors=True); os.makedirs(OUT)
rng, N, FW = np.random.default_rng(1), 15_000, ["1.2.0", "1.3.1", "1.4.0", "1.10.0"]  # 50 Hz × 5분
parts, raw_bytes = [], 0
for day in range(5):
    for dev in range(40):
        r = np.zeros(N, REC); r["seq"] = np.arange(N) & 0xFFFF
        for c, base in zip(("ax", "ay", "az", "gx", "gy", "gz"), (0, 0, 8192, 0, 0, 0)):
            walk = np.convolve(rng.normal(0, 40, N), np.ones(25) / 25, "same")   # 느린 움직임
            r[c] = (base + walk + rng.normal(0, 8, N)).astype(np.int16)          # + 센서 잡음
        raw_bytes += r.nbytes + (N // 250) * 44                                  # 청크 헤더+CRC
        df = pd.DataFrame(r); df["t_us"] = np.arange(N, dtype=np.int64) * 20_000 + day * 86_400_000_000
        df["device_id"] = np.int32(dev); df["session_id"] = np.int32(day * 1000 + dev)
        df["fw"] = FW[dev % 4]; df["dt"] = f"2026-09-{day + 1:02d}"
        parts.append(df)
df = pd.concat(parts, ignore_index=True).sort_values(["dt", "device_id", "t_us"])
df.to_csv(f"{OUT}/all.csv", index=False); df.to_csv(f"{OUT}/all.csv.gz", index=False)
tbl = pa.Table.from_pandas(df, preserve_index=False)
for codec in ("snappy", "zstd"):
    ds.write_dataset(tbl, f"{OUT}/silver_{codec}", format="parquet", partitioning=["dt"],
                     partitioning_flavor="hive", preserve_order=True,
                     min_rows_per_group=150_000, max_rows_per_group=150_000,
                     file_options=ds.ParquetFileFormat().make_write_options(compression=codec))
size = lambda p: sum(os.path.getsize(f) for f in glob.glob(p, recursive=True))
print(f"rows={len(df):,}  raw binary={raw_bytes/1e6:.1f} MB")
for name, pat in [("CSV", "all.csv"), ("CSV.gz", "all.csv.gz"),
                  ("Parquet snappy", "silver_snappy/**/*.parquet"), ("Parquet zstd", "silver_zstd/**/*.parquet")]:
    s = size(f"{OUT}/{pat}"); print(f"{name:15s} {s/1e6:7.1f} MB  ({s/raw_bytes:.2f}× raw)")
print(sorted(os.listdir(f"{OUT}/silver_zstd")), os.listdir(f"{OUT}/silver_zstd/dt=2026-09-01"))
```

```text
rows=3,000,000  raw binary=42.5 MB
CSV               183.4 MB  (4.31× raw)
CSV.gz             36.4 MB  (0.86× raw)
Parquet snappy     29.1 MB  (0.68× raw)
Parquet zstd       17.4 MB  (0.41× raw)
['dt=2026-09-01', 'dt=2026-09-02', 'dt=2026-09-03', 'dt=2026-09-04', 'dt=2026-09-05'] ['part-0.parquet']
```

출력에서 볼 것: CSV는 숫자를 글자로 쓰니 원본 바이너리의 4.3배다 (int16 하나가 "8187," 5~6글자). CSV.gz는 작지만 **열을 골라 읽을 수도, 중간부터 읽을 수도 없다**. Parquet zstd는 원본 바이너리보다도 작고(0.41×), 그러면서 열 단위·row group 단위로 골라 읽을 수 있다. 디렉터리는 `dt=…` 5개, 각 안에 파일 1개다.

정렬의 효과도 직접 봤다: 이 스크립트를 처음에 `preserve_order` 없이 돌렸을 때(행 순서가 섞이고 row group이 약 3만 행 단위로 잘게 쪼개진 상태) Parquet zstd가 26.5 MB, snappy가 39.9 MB였다. **기기·시간 순으로 정렬하고 row group을 크게** 쓰자 17.4 / 29.1 MB가 됐다. 같은 열에 비슷한 값이 연속으로 오면 인코딩·압축이 잘 되기 때문이다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="22" font-size="13">같은 3,000,000 샘플의 저장 크기 (MB, 예제 5 실측)</text><line x1="180" y1="34" x2="180" y2="268" stroke="currentColor"/><text x="170" y="57" font-size="12" text-anchor="end">원본 바이너리</text><rect x="180" y="40" width="102.0" height="24" fill="#888"/><text x="288" y="57" font-size="12">42.5</text><text x="170" y="95" font-size="12" text-anchor="end">CSV</text><rect x="180" y="78" width="440.2" height="24" fill="#d0564a"/><text x="626" y="95" font-size="12">183.4</text><text x="170" y="133" font-size="12" text-anchor="end">CSV.gz</text><rect x="180" y="116" width="87.4" height="24" fill="#e08a3c"/>
<text x="273" y="133" font-size="12">36.4 (열 선택 불가)</text><text x="170" y="171" font-size="12" text-anchor="end">Parquet zstd 정렬 X</text><rect x="180" y="154" width="63.6" height="24" fill="#4a7bd0" fill-opacity="0.5"/><text x="250" y="171" font-size="12">26.5</text><text x="170" y="209" font-size="12" text-anchor="end">Parquet snappy</text><rect x="180" y="192" width="69.8" height="24" fill="#4a7bd0"/><text x="256" y="209" font-size="12">29.1</text><text x="170" y="247" font-size="12" text-anchor="end">Parquet zstd</text><rect x="180" y="230" width="41.8" height="24" fill="#3f9a6b"/><text x="228" y="247" font-size="12">17.4</text>
<text x="10" y="290" font-size="12">막대 길이 = MB × 2.4 px. "정렬 X" = preserve_order 없이 쓴 첫 시도. 실제 센서 데이터에서는 비율이 달라진다.</text>
</svg>
```

그림 5 — 예제 5의 크기 비교. CSV는 원본의 4.3배, Parquet zstd(기기·시간 정렬)는 원본의 0.41배다. 잡음이 많은 실제 IMU·오디오는 압축률이 이보다 나쁠 수 있으므로 자기 데이터로 다시 재야 한다.

### 6.4 파티셔닝 — 무엇으로 디렉터리를 나누나

**파티셔닝**(partitioning)은 데이터를 어떤 열의 값으로 디렉터리를 나눠 저장하는 것이다(`dt=2026-09-03/`). 질의에 그 열 조건이 있으면 **다른 디렉터리는 아예 열지 않는다** (partition pruning).

| 파티션 열 후보 | 장점 | 위험 |
|---|---|---|
| `dt` (날짜) | 거의 모든 질의·lifecycle·재처리가 날짜 범위 | 하루 데이터가 작으면 파일이 작아짐 |
| `sensor` (imu/audio/ppg) | 스키마가 센서마다 다르므로 자연스러운 분리 | 보통 테이블 자체를 나누는 게 낫다 |
| `device_id` | "한 기기" 질의가 빠름 | 10k 기기 × 365일 = 365만 디렉터리 → **small files 폭발** |
| `fw` | fw별 비교 | 값 종류가 적어 효과가 작고, 질의 패턴이 바뀌면 무용 |

경험칙: **파티션은 cardinality(값의 종류 수)가 낮고 거의 모든 질의에 들어가는 열**(보통 날짜)로 하고, 기기·세션처럼 cardinality가 높은 열은 파티션 대신 **파일 안에서 정렬**해서 row group 통계로 거른다. 예제 5가 정확히 그렇게 했다: `dt`로 파티션, 파일 안은 `device_id, t_us` 순 정렬 → row group 4개가 각각 기기 0~9, 10~19, 20~29, 30~39를 담는다.

### 6.5 코드로 확인 — predicate pushdown이 실제로 건너뛰는 양

**예제 6** — 무엇을 확인하나: "9월 3일, 기기 23의 t_us·az만" 질의에서 ① 파티션 pruning으로 몇 개 파일이, ② row group 통계로 몇 개 row group이 남는지, ③ 필요한 열의 압축 바이트가 전체의 얼마인지를 Parquet 메타데이터로 계산하고, 실제 읽기 시간을 CSV 전체 스캔과 비교한다.

```python
# predicate pushdown: "기기 23의 9/3 az만" — 파티션·row group·컬럼을 얼마나 건너뛰나
import time, pandas as pd, pyarrow.dataset as ds, pyarrow.parquet as pq
B = "/private/tmp/claude-501/h3/bulk"
d = ds.dataset(f"{B}/silver_zstd", format="parquet", partitioning="hive")
flt = (ds.field("dt") == "2026-09-03") & (ds.field("device_id") == 23)
cols = ["t_us", "az"]
files = list(d.get_fragments(filter=flt))                         # ① 파티션 pruning
rgs = [rg for f in files for rg in f.split_by_row_group(flt, schema=d.schema)]     # ② row group 통계 pruning
need = 0
for rg in rgs:
    md = rg.metadata; i = rg.row_groups[0].id; g = md.row_group(i)
    need += sum(g.column(j).total_compressed_size for j in range(g.num_columns)
                if g.column(j).path_in_schema in cols)            # ③ 필요한 컬럼만
total = sum(f.metadata.row_group(k).column(j).total_compressed_size for f in d.get_fragments()
            for k in range(f.metadata.num_row_groups) for j in range(f.metadata.num_columns))
print(f"files: {len(files)}/{len(list(d.get_fragments()))}  row groups: {len(rgs)}/20  "
      f"bytes needed: {need/1e6:.2f} / {total/1e6:.1f} MB")
t0 = time.perf_counter(); t = d.to_table(columns=cols, filter=flt); t1 = time.perf_counter()
print(f"pyarrow.dataset  rows={t.num_rows:,}  {1e3*(t1-t0):.0f} ms")
t0 = time.perf_counter(); df = pd.read_csv(f"{B}/all.csv")
sel = df[(df.dt == "2026-09-03") & (df.device_id == 23)][cols]; t1 = time.perf_counter()
print(f"pandas CSV full scan rows={len(sel):,}  {1e3*(t1-t0):.0f} ms")
g = pq.ParquetFile(f"{B}/silver_zstd/dt=2026-09-03/part-0.parquet").metadata.row_group(0)
print({g.column(j).path_in_schema: round(g.column(j).total_compressed_size / 1e3)
       for j in range(g.num_columns)}, "KB/col in RG0")
```

```text
files: 1/5  row groups: 1/20  bytes needed: 0.19 / 17.3 MB
pyarrow.dataset  rows=15,000  1 ms
pandas CSV full scan rows=15,000  1121 ms
{'seq': 65, 'ax': 124, 'ay': 121, 'az': 123, 'gx': 122, 'gy': 122, 'gz': 122, 't_us': 62, 'device_id': 0, 'session_id': 0, 'fw': 0} KB/col in RG0
```

출력에서 볼 것:

- 파일 5개 중 1개, row group 20개 중 1개(기기 20~29를 담은 RG2), 그리고 그 안에서 2개 열만 → 읽어야 할 압축 바이트가 **0.19 MB / 17.3 MB ≈ 1.1%**다. 이 숫자는 메타데이터에서 계산한 "필요한 column chunk 크기 합"이다. 실제 I/O는 footer 읽기와 page 단위 디코딩 때문에 조금 더 많다.
- 시간은 1 ms 대 1,121 ms. 로컬 SSD + OS 파일 캐시에서의 한 번 측정이라 절대값은 의미가 적고(첫 실행에서는 4 ms였다), **자릿수 차이**가 요점이다. object storage에서는 요청 하나에 수십 ms 지연이 붙으므로 "몇 번의 GET, 몇 바이트"가 그대로 시간과 비용이 된다.
- 열별 크기: `device_id`·`session_id`·`fw`는 0 KB(반올림) — 정렬된 상태라 dictionary + RLE로 거의 공짜다. 그러니 **자주 거르는 메타데이터 열(fw, sensor 설정 해시)을 silver에 같이 넣는 비용은 거의 없다**(denormalization). 센서 열은 잡음 때문에 열마다 약 120 KB다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<text x="10" y="22" font-size="13">질의: dt = 2026-09-03 AND device_id = 23, 열 = t_us, az</text><text x="20" y="54" font-size="12">① 파티션</text><rect x="100" y="40" width="96" height="26" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="206" y="40" width="96" height="26" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="312" y="40" width="96" height="26" rx="4" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="418" y="40" width="96" height="26" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="524" y="40" width="96" height="26" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="148" y="58" font-size="12" text-anchor="middle">dt=09-01</text><text x="254" y="58" font-size="12" text-anchor="middle">dt=09-02</text><text x="360" y="58" font-size="12" text-anchor="middle">dt=09-03</text>
<text x="466" y="58" font-size="12" text-anchor="middle">dt=09-04</text><text x="572" y="58" font-size="12" text-anchor="middle">dt=09-05</text><line x1="360" y1="66" x2="360" y2="96" stroke="currentColor"/><text x="20" y="124" font-size="12">② row group</text><rect x="120" y="106" width="110" height="28" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="240" y="106" width="110" height="28" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="360" y="106" width="110" height="28" rx="4" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="480" y="106" width="110" height="28" rx="4" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="175" y="125" font-size="12" text-anchor="middle">RG0 dev 0..9</text><text x="295" y="125" font-size="12" text-anchor="middle">RG1 dev 10..19</text>
<text x="415" y="125" font-size="12" text-anchor="middle">RG2 dev 20..29</text><text x="535" y="125" font-size="12" text-anchor="middle">RG3 dev 30..39</text><line x1="415" y1="134" x2="415" y2="164" stroke="currentColor"/><text x="20" y="196" font-size="12">③ 열</text><rect x="80" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="130" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="180" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="230" y="176" width="50" height="30" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="280" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="330" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/>
<rect x="380" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="430" y="176" width="50" height="30" fill="#3f9a6b" fill-opacity="0.35" stroke="currentColor"/><rect x="480" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="530" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><rect x="580" y="176" width="50" height="30" fill="#888" fill-opacity="0.15" stroke="currentColor"/><text x="105" y="195" font-size="12" text-anchor="middle">seq</text><text x="155" y="195" font-size="12" text-anchor="middle">ax</text><text x="205" y="195" font-size="12" text-anchor="middle">ay</text><text x="255" y="195" font-size="12" text-anchor="middle">az</text><text x="305" y="195" font-size="12" text-anchor="middle">gx</text>
<text x="355" y="195" font-size="12" text-anchor="middle">gy</text><text x="405" y="195" font-size="12" text-anchor="middle">gz</text><text x="455" y="195" font-size="12" text-anchor="middle">t_us</text><text x="505" y="195" font-size="12" text-anchor="middle">dev</text><text x="555" y="195" font-size="12" text-anchor="middle">sess</text><text x="605" y="195" font-size="12" text-anchor="middle">fw</text><text x="20" y="240" font-size="13">결과: 파일 1/5 · row group 1/20 · 열 2/11 → 압축 바이트 0.19 / 17.3 MB (약 1.1%)</text><text x="20" y="264" font-size="12">초록 = 읽는 부분, 회색 = footer 통계만 보고 건너뛴 부분. RG 안에서 dev 23 외 행은</text><text x="20" y="282" font-size="12">읽은 뒤 필터로 버린다 (page index가 있으면 더 잘게 건너뛸 수 있다).</text>
</svg>
```

그림 6 — 예제 6의 세 단계 pruning. 파티션(디렉터리) → row group 통계(footer의 min/max) → 열 선택. 셋 다 "읽기 전에" 결정된다.

### 6.6 파일 크기 — small files 문제

워커가 청크마다 Parquet 파일 하나를 쓰면 어떻게 될까? 파일마다 footer·스키마·열별 메타데이터가 붙고, 질의 엔진은 파일마다 열기·footer 읽기를 해야 한다. object storage에서는 파일 하나당 GET 요청 최소 1~2번이다. 이게 **small files 문제**다.

**예제 7** — 무엇을 확인하나: 예제 4의 워커가 만든 청크당 Parquet 파일(956개)과, 이를 `dt`별로 합치고 정렬한 파일(compaction)의 개수·크기·스캔 시간을 비교한다.

```python
# small files 문제: 청크당 Parquet 1개(staging) vs dt별로 합친(compaction) 파일
import glob, os, time, shutil, sqlite3, pyarrow as pa, pyarrow.dataset as ds
L = "/private/tmp/claude-501/h3/lake"
keys = dict(sqlite3.connect(f"{L}/_ledger.db").execute("SELECT out, key FROM done"))
small = sorted(glob.glob(f"{L}/staging/*.parquet"))
def scan(paths):
    t0 = time.perf_counter(); n = ds.dataset(paths, format="parquet").to_table(columns=["az"]).num_rows
    return n, 1e3 * (time.perf_counter() - t0)
tbls = []
for p in small:                                       # 출처 key에서 dt를 꺼내 컬럼으로 붙인다
    t = ds.dataset(p, format="parquet").to_table(); day = keys[p].split("dt=")[1][:10]
    tbls.append(t.append_column("dt", pa.array([day] * t.num_rows)))
big = pa.concat_tables(tbls).sort_by([("dt", "ascending"), ("device_id", "ascending"), ("t_us", "ascending")])
shutil.rmtree(f"{L}/silver", ignore_errors=True)
ds.write_dataset(big, f"{L}/silver/imu", format="parquet", partitioning=["dt"], partitioning_flavor="hive",
                 preserve_order=True, basename_template="part-{i}.parquet")
compact = sorted(glob.glob(f"{L}/silver/imu/**/*.parquet", recursive=True))
for name, paths in (("staging (small)", small), ("compacted", compact)):
    n, ms = scan(paths); sz = sum(map(os.path.getsize, paths))
    print(f"{name:16s} files={len(paths):4d} total={sz/1e6:5.1f} MB avg={sz/len(paths)/1e3:6.1f} KB "
          f"rows={n:,} scan={ms:.0f} ms")
print("partitions:", sorted(os.listdir(f"{L}/silver/imu")))
```

```text
staging (small)  files= 956 total= 10.9 MB avg=  11.4 KB rows=239,000 scan=61 ms
compacted        files=   3 total=  2.9 MB avg= 981.9 KB rows=239,000 scan=1 ms
partitions: ['dt=2026-09-28', 'dt=2026-09-29', 'dt=2026-09-30']
```

출력에서 볼 것: 같은 239,000행인데 small files는 **3.8배 크고**(파일당 메타데이터 오버헤드 + 250행짜리 page는 압축이 잘 안 됨) 스캔이 수십 배 느리다. 로컬 디스크에서 이 정도이니, 파일마다 네트워크 왕복이 붙는 object storage에서는 차이가 더 커진다. 그래서 실전 파이프라인은 **워커는 staging에 작게 빨리 쓰고, 주기적인 compaction 작업이 파티션별로 큰 파일로 합친다**. 데이터 레이크에서 자주 인용되는 목표 파일 크기는 수십 MB~1 GB 정도(128 MB~512 MB 근처가 흔한 권장)인데, 엔진·질의 패턴마다 다르니 경험칙으로만 기억한다.

compaction도 멱등이어야 한다: 같은 입력 집합 → 같은 출력. 그리고 **새 경로에 쓰고 catalog의 포인터를 바꾸는** 방식이어야 읽는 쪽이 반쯤 바뀐 파티션을 보지 않는다(9절의 테이블 포맷이 이 일을 대신 해 준다).

### 6.7 질의 엔진 — 어디서 읽나

같은 Parquet 파일을 여러 엔진이 읽는다. 이게 오픈 포맷의 장점이다.

| 엔진 | 성격 | 언제 |
|---|---|---|
| pyarrow.dataset / pandas | 단일 머신 라이브러리 | 노트북, 작은 배치, 워커 내부 |
| DuckDB | 단일 머신 내장형 SQL 엔진, Parquet·S3 직접 질의 지원 | 수십~수백 GB까지 노트북에서 SQL |
| Spark / Trino / Athena | 분산 SQL | TB 이상, 여러 사람이 공유 |
| BigQuery / Snowflake | 관리형 데이터 웨어하우스 (외부 테이블 또는 적재) | 조직 표준 분석 환경 |

면접에서는 "silver는 Parquet + 날짜 파티션이라 DuckDB로 노트북에서 바로, 규모가 커지면 Spark/BigQuery로 같은 파일을 읽는다"고 말하면 충분하다.

---

## 7. Catalog — 메타데이터 DB

### 7.1 직관

수십만 개 파일을 매번 열어서 "fw 1.4 이상이고 드롭 없는 세션"을 찾을 수는 없다. 세션 단위로 한 행씩 요약한 **catalog**(메타데이터 DB)가 필요하다. SSD 테스트 인프라의 "테스트 결과 DB"(드라이브 S/N, FW rev, 챔버, pass/fail, 로그 경로)와 정확히 같은 역할이다.

### 7.2 무엇을 넣나

| 열 | 예 | 용도 |
|---|---|---|
| session_id (PK), device_id | 1042, 42 | 식별, 사용자 단위 split |
| fw (문자열) + fw_num (정수) | "1.10.0", 0x010A00 | 표시용 + **비교용** |
| dt, duration_s, n_samples | 2026-09-29, 100.0, 5000 | 양 집계 |
| n_gaps, max_gap_s | 0, 0.02 | G7 time QA 결과 → 필터 |
| consent | 1/0 | H6 — 학습 사용 가능 여부 |
| parser_version | 2.1.0 | 재처리 대상 찾기 (8절) |
| path, file_sha256 | silver/imu/dt=…/part-0.parquet, 해시 | 계보·재현성 (9절) |

실전에서는 이 catalog가 PostgreSQL 같은 RDBMS이거나, Hive Metastore·AWS Glue Data Catalog 같은 테이블 catalog + 별도 세션 테이블이다. 여기서는 SQLite로 같은 개념을 만든다.

### 7.3 코드로 확인 — SQLite catalog와 "fw ≥ X, gap ≤ 1 s" 질의

**예제 8** — 무엇을 확인하나: 예제 7의 silver 파일에서 세션별 요약(샘플 수, gap 수, 최대 gap, 동의 여부, 파일 경로·해시)을 계산해 SQLite에 넣고, (a) fw 버전을 문자열로 비교하는 함정, (b) "fw ≥ 1.4.0, 1초 넘는 gap 없음, 동의함" 질의를 실행한다. 50 Hz이므로 간격 > 30 ms를 gap으로 센다 (G7 7절의 time QA를 단순화).

```python
# 메타데이터 catalog (SQLite): 세션 단위 한 행 — fw, 길이, 샘플 수, gap(G7 QA), 동의, 파일·해시
import glob, hashlib, sqlite3, pyarrow.dataset as ds, numpy as np
from h3fmt import fw_pack
from h3parse import PARSER_VERSION
L = "/private/tmp/claude-501/h3/lake"; OPTED_OUT = {5, 13}           # H6: 동의 철회 기기 (가정)
cat = sqlite3.connect(f"{L}/catalog.db"); cat.execute("DROP TABLE IF EXISTS sessions")
cat.execute("""CREATE TABLE sessions(session_id INT PRIMARY KEY, device_id INT, fw TEXT, fw_num INT,
  dt TEXT, duration_s REAL, n_samples INT, n_gaps INT, max_gap_s REAL, consent INT,
  parser_version TEXT, path TEXT, file_sha256 TEXT)""")
for path in sorted(glob.glob(f"{L}/silver/imu/dt=*/*.parquet")):
    sha = hashlib.sha256(open(path, "rb").read()).hexdigest()[:16]; day = path.split("dt=")[1][:10]
    t = ds.dataset(path, format="parquet").to_table(columns=["session_id", "device_id", "fw", "t_us"])
    sid = t["session_id"].to_numpy(); tus = t["t_us"].to_numpy()
    for s in np.unique(sid):
        ts = np.sort(tus[sid == s]); dts = np.diff(ts) / 1e6; i = int(np.flatnonzero(sid == s)[0])
        dev, fw = int(t["device_id"][i].as_py()), t["fw"][i].as_py()
        cat.execute("INSERT INTO sessions VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?)", (int(s), dev, fw, fw_pack(fw), day,
            (ts[-1] - ts[0]) / 1e6 + 0.02, len(ts), int((dts > 0.03).sum()), float(dts.max()),
            int(dev not in OPTED_OUT), PARSER_VERSION, path.split("lake/")[1], sha))
cat.commit()
q = lambda sql: cat.execute(sql).fetchall()
print("sessions:", q("SELECT COUNT(*), SUM(n_samples), SUM(n_gaps>0) FROM sessions")[0])
print("fw >= '1.4.0' (TEXT 비교):", q("SELECT COUNT(*) FROM sessions WHERE fw >= '1.4.0'")[0][0],
      "  fw_num >= 1.4.0:", q(f"SELECT COUNT(*) FROM sessions WHERE fw_num >= {fw_pack('1.4.0')}")[0][0])
sql = f"""SELECT fw, COUNT(*), ROUND(SUM(duration_s)/60,1) FROM sessions WHERE fw_num >= {fw_pack('1.4.0')}
          AND max_gap_s <= 1.0 AND consent = 1 GROUP BY fw ORDER BY fw_num"""
print("fw>=1.4.0 & no gap>1s & consent:", q(sql))
print("worst:", q("SELECT session_id, fw, n_gaps, max_gap_s, path FROM sessions ORDER BY max_gap_s DESC LIMIT 2"))
```

```text
sessions: (50, 239000, 26)
fw >= '1.4.0' (TEXT 비교): 12   fw_num >= 1.4.0: 24
fw>=1.4.0 & no gap>1s & consent: [('1.4.0', 8, 13.1), ('1.10.0', 4, 6.7)]
worst: [(1009, '1.3.1', 3, 10.02, 'silver/imu/dt=2026-09-28/part-0.parquet'), (1043, '1.10.0', 3, 10.02, 'silver/imu/dt=2026-09-29/part-0.parquet')]
```

출력에서 볼 것:

- **문자열 비교 함정**: `fw >= '1.4.0'`은 12개 세션만 찾는다. 사전순으로 `'1.10.0' < '1.4.0'`이기 때문이다('1' 다음 글자 '1' < '4'). 정수로 묶은 `fw_num`(`major<<16 | minor<<8 | patch`)으로 비교해야 24개가 맞다. 펌웨어 엔지니어라면 익숙한 버그다 — 버전은 숫자 튜플로 저장한다.
- 50개 세션 중 26개에 gap이 있다. 예제 4에서 DLQ로 간 청크(5초 = 250샘플)가 그대로 **세션 안의 구멍**이 됐기 때문이다. 최악은 10.02 s (연속 두 청크 유실). ingestion 실패가 곧 데이터 품질 문제라는 걸 catalog가 숫자로 보여 준다 → H7 대시보드에 "DLQ로 인한 gap"을 따로 표시해야 하는 이유.
- 최종 질의: fw 1.4.0에서 8개 세션 13.1분, 1.10.0에서 4개 세션 6.7분. 모델팀에 "조건에 맞는 데이터가 총 20분 정도"라고 바로 답할 수 있다.

### 7.4 임베디드 연결과 함정

- catalog는 **파생 데이터**다. silver(그리고 결국 bronze)에서 언제든 다시 만들 수 있어야 한다. 손으로 고친 값이 catalog에만 있으면 재처리 때 사라진다. 사람이 붙이는 정보(라벨, 동의)는 별도 원천 테이블에 두고 조인한다.
- QA 지표는 계산 방법이 바뀌면 버전을 붙인다 (`qa_version`). gap 기준(30 ms)이 바뀌면 같은 세션의 n_gaps가 바뀐다.
- 시간대: `dt`가 UTC 날짜인지 기기 현지 날짜인지 정하고 문서화한다. 섞이면 하루 경계 질의가 틀린다 (G7의 wall clock 문제).

---

## 8. 파서 버그를 3개월 뒤에 발견하면 — 재처리

### 8.1 시나리오

fw 1.4.0이 나오면서 포맷 v2(헤더 48 B)가 생겼다. 그런데 서버 파서 2.0.0은 **헤더 길이를 v1 값(40 B)으로 고정**해 두었고, 레코드 길이 검사도 없었다. v2 청크의 레코드를 8바이트 앞에서부터 읽으니 값이 한 칸씩 밀린 쓰레기가 되는데, 에러는 나지 않는다. 3개월 뒤 모델팀이 "fw 1.4 이상 기기는 중력이 없다"고 보고한다.

손으로 따져 보자. 버그 파서의 레코드 k는 실제 오프셋 48 + 14k − 8 = 48 + 14(k−1) + 6에서 시작한다. 즉 **진짜 레코드 k−1의 6번째 바이트**부터다. 레코드 안에서 오프셋 6~7은 az다. 그러니 버그 파서의 `seq` = 진짜 az(≈ 8192, 잡음), 버그 파서의 `az`(오프셋 6) = 진짜 오프셋 12 = gz(≈ 0). "seq가 연속이 아니고, az 평균이 0 g"가 될 것이라고 예측할 수 있다.

### 8.2 대응 절차

1. **멈춘다**: 해당 포맷의 silver 생성을 중단하거나 플래그를 건다. 그 데이터로 만든 데이터셋 manifest(9절)를 찾아 모델팀에 알린다.
2. **영향 범위**: catalog에서 `parser_version = '2.0.0' AND fmt_ver = 2` (여기서는 fw ≥ 1.4.0) 세션을 찾는다.
3. **고친다 + 테스트**: 파서 2.1.0 (`hdr_len`/버전별 헤더 크기 + 레코드 길이 검사). 버그를 재현하는 회귀 테스트를 추가한다.
4. **재처리**: bronze에서 영향받은 객체만 다시 흘린다. 출력은 **새 경로/새 버전**으로 쓴다 (`parser_version=2.1.0`).
5. **QA로 검증**: seq 연속성, 물리적으로 말이 되는 값(중력 1 g) 같은 불변식(invariant)을 확인.
6. **전환**: catalog 포인터를 새 파일로 바꾼다. 옛 silver는 그것을 참조하는 manifest가 정리될 때까지 보관.
7. **재발 방지**: QA 불변식("정지 구간 |a| ≈ 1 g", "seq 연속률 > 99%")을 ingestion 단계의 자동 검사로 넣는다 (H7).

### 8.3 코드로 확인 — 영향 범위 찾기와 재처리

**예제 9** — 무엇을 확인하나: 버그 파서 2.0.0을 재현하고, catalog에서 영향받은 세션을 찾아 bronze 객체를 다시 읽은 뒤, 버그 파서와 고친 파서의 QA 지표(seq 연속 비율, az 평균)를 비교한다. (실습 catalog는 처음부터 2.1.0으로 만들었으므로, 실제로는 `parser_version = '2.0.0'` 조건이 같이 들어간다고 생각한다.)

```python
# parser 버그 시나리오: 2.0.0은 v2 헤더(48 B)를 v1 길이(40 B)로 읽었다 → 영향 범위 찾기 + bronze 재처리
import sqlite3, struct, numpy as np
from objstore import LocalObjectStore
from h3fmt import HDR_V1, REC, fw_pack
from h3parse import parse_chunk, Permanent
L = "/private/tmp/claude-501/h3/lake"; store = LocalObjectStore(L)
def parse_2_0_0(buf):                                   # 버그 버전: 헤더 길이 고정 + 길이 검사 없음
    n = struct.unpack_from("<H", buf, 34)[0]
    return np.frombuffer(buf, REC, count=n, offset=HDR_V1.size)
def qa(rec):                                            # G7/H7식 QA: seq 연속 비율, az 평균(g)
    return np.mean(np.diff(rec["seq"].astype(np.int64)) % 65536 == 1), rec["az"].mean() / 8192
cat = sqlite3.connect(f"{L}/catalog.db")
aff = cat.execute("SELECT session_id, device_id, dt FROM sessions WHERE fw_num >= ?",
                  (fw_pack("1.4.0"),)).fetchall()
print("affected sessions:", len(aff), "/", cat.execute("SELECT COUNT(*) FROM sessions").fetchone()[0])
old, new, n_obj, n_bytes = [], [], 0, 0
for sid, dev, day in aff:
    for key in store.list(f"bronze/imu/dt={day}/dev={dev:05d}/sess={sid}/"):
        buf = store.get(key)
        try: _, rec = parse_chunk(buf)                 # 고친 파서 (현재 버전)
        except Permanent: continue                     # DLQ 대상은 그대로 DLQ
        old.append(qa(parse_2_0_0(buf))); new.append(qa(rec)); n_obj += 1; n_bytes += len(buf)
o, n = np.array(old), np.array(new)
print(f"re-read {n_obj} bronze objects ({n_bytes/1e6:.2f} MB)")
print(f"parser 2.0.0: seq continuity {o[:,0].mean():.3f}, mean az {o[:,1].mean():+.3f} g")
print(f"parser {__import__('h3parse').PARSER_VERSION}: seq continuity {n[:,0].mean():.3f}, mean az {n[:,1].mean():+.3f} g")
```

```text
affected sessions: 24 / 50
re-read 462 bronze objects (1.64 MB)
parser 2.0.0: seq continuity 0.005, mean az -0.000 g
parser 2.1.0: seq continuity 1.000, mean az +1.000 g
```

출력에서 볼 것: 손으로 한 예측 그대로 버그 파서는 seq 연속률 0.5%, az 평균 0 g(실제로는 gz를 읽음)이다. 고친 파서는 1.000과 +1.000 g. 영향 범위는 50개 중 24개 세션이고, 다시 읽은 것은 bronze 객체 462개(1.64 MB)뿐이다 — 전체를 다시 돌릴 필요가 없다. **bronze key에 dt·dev·sess가 있고 catalog에 fw·parser_version이 있어서** 영향 범위를 prefix 나열로 좁힐 수 있었다. bronze가 없었다면 이 데이터는 복구할 방법이 없다.

### 8.4 규모를 키우면

3개월 × 10k 기기 × 50 MB/day = 45 TB의 bronze 중 v2 포맷이 절반이라면 22 TB를 다시 읽어야 한다. 워커 하나가 20 MB/s라면 22 TB / 20 MB/s ≈ 1.1×10⁶ s ≈ 13일이다. 워커 100개면 약 3시간이다. 그래서:

- 재처리 경로는 평상시 ingestion과 **같은 코드, 별도 큐/우선순위**로 돌린다 (실시간 ingestion을 굶기지 않게 — backpressure).
- cold 계층에 내려간 bronze는 꺼내는 비용·시간이 있다 (4.5절). 재처리가 잦은 기간(예: 최근 90일)은 hot에 둔다.
- 재처리 비용 자체를 줄이려면 silver에서 고칠 수 있는 버그(단위 스케일 등)는 silver → silver 변환으로 처리하고, 바이트 해석 버그만 bronze에서 다시 한다.

---

## 9. 데이터 버전 — 재현 가능한 학습 데이터셋

### 9.1 직관 — "그 모델은 정확히 어떤 데이터로 학습했나?"

3개월 전 모델이 지금 모델보다 잘 됐다. 왜? 코드는 git으로 되돌릴 수 있는데 데이터는? catalog 질의를 다시 돌리면 그 사이 들어온 세션, 동의 철회, 재처리 때문에 **다른 결과**가 나온다. 펌웨어로 말하면 "릴리스 바이너리의 빌드 입력(소스 해시, 툴체인, 설정)을 전부 고정해서 똑같이 다시 빌드할 수 있어야 한다"는 reproducible build와 같은 요구다.

### 9.2 정의 — manifest와 content hash

- **Snapshot(스냅샷)**: 어느 시점의 데이터 상태를 고정한 것. 불변 파일들의 목록이면 충분하다.
- **Manifest**: 데이터셋을 이루는 파일 목록 + 각 파일의 **content hash**(내용의 sha256) + 행 선택 조건(세션 목록) + 만든 질의·파서 버전.
- **Dataset ID**: manifest 자체를 정규화(canonical JSON, 키 정렬)해서 해시한 값. 내용이 같으면 ID가 같고, 하나라도 다르면 ID가 다르다. git commit 해시와 같은 원리다.

이 방식이 동작하려면 전제가 하나 있다: **manifest가 가리키는 파일은 절대 같은 경로에 덮어쓰지 않는다.** 바꿀 때는 새 경로에 쓰고(copy-on-write), 새 manifest를 만든다.

```svg
<svg viewBox="0 0 680 300" xmlns="http://www.w3.org/2000/svg">
<defs><marker id="h3d" viewBox="0 0 10 10" refX="9" refY="5" markerWidth="7" markerHeight="7" orient="auto-start-reverse"><path d="M0,0 L10,5 L0,10 z" fill="currentColor"/></marker></defs><text x="10" y="22" font-size="13">manifest = 불변 파일들을 가리키는 목록 (copy-on-write)</text><rect x="20" y="50" width="170" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="105" y="72" font-size="13" text-anchor="middle">manifest 1ca4d36e521a</text><text x="105" y="92" font-size="12" text-anchor="middle">12 세션 · 59,250 행</text><rect x="20" y="130" width="170" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor"/><text x="105" y="152" font-size="13" text-anchor="middle">manifest 4763be46ea52</text><text x="105" y="172" font-size="12" text-anchor="middle">동의 철회 후 11 세션</text><rect x="20" y="210" width="170" height="54" rx="6" fill="#3f9a6b" fill-opacity="0.2" stroke="currentColor" stroke-dasharray="5 4"/><text x="105" y="232" font-size="13" text-anchor="middle">manifest (재처리 후)</text>
<text x="105" y="252" font-size="12" text-anchor="middle">새 ID</text><rect x="400" y="50" width="250" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="525" y="72" font-size="12" text-anchor="middle">dt=09-28/part-0 (sha 7f…)</text><rect x="400" y="100" width="250" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="525" y="122" font-size="12" text-anchor="middle">dt=09-29/part-0 (sha 3a…)</text><rect x="400" y="150" width="250" height="34" rx="4" fill="#4a7bd0" fill-opacity="0.2" stroke="currentColor"/><text x="525" y="172" font-size="12" text-anchor="middle">dt=09-30/part-0 (sha c2…)</text><rect x="400" y="210" width="250" height="34" rx="4" fill="#e08a3c" fill-opacity="0.25" stroke="currentColor"/><text x="525" y="232" font-size="12" text-anchor="middle">dt=09-29/part-0 @parser 2.1.1 (새 파일)</text><line x1="190" y1="70" x2="398" y2="67" stroke="currentColor" marker-end="url(#h3d)"/>
<line x1="190" y1="78" x2="398" y2="117" stroke="currentColor" marker-end="url(#h3d)"/><line x1="190" y1="86" x2="398" y2="165" stroke="currentColor" marker-end="url(#h3d)"/><line x1="190" y1="150" x2="398" y2="72" stroke="currentColor" stroke-opacity="0.6" marker-end="url(#h3d)"/><line x1="190" y1="157" x2="398" y2="122" stroke="currentColor" stroke-opacity="0.6" marker-end="url(#h3d)"/><line x1="190" y1="164" x2="398" y2="170" stroke="currentColor" stroke-opacity="0.6" marker-end="url(#h3d)"/><line x1="190" y1="226" x2="398" y2="78" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#h3d)"/><line x1="190" y1="236" x2="398" y2="227" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#h3d)"/><line x1="190" y1="246" x2="398" y2="178" stroke="#e08a3c" stroke-dasharray="4 3" marker-end="url(#h3d)"/><text x="10" y="290" font-size="12">바뀐 파일만 새로 쓰고 나머지는 공유한다. 옛 manifest가 가리키는 파일은 그대로 남아 재현이 가능하다.</text>
</svg>
```

그림 7 — manifest와 copy-on-write. 위 두 manifest ID는 예제 10의 실제 출력이다. 재처리로 9/29 파일이 바뀌면 새 경로에 쓰고 새 manifest가 그 파일을 가리킨다 (점선은 개념 예시, sha 접두어도 예시). 옛 manifest의 파일은 지우지 않는다.

### 9.3 코드로 확인 — manifest, 검증, 동의 철회

**예제 10** — 무엇을 확인하나: catalog 질의로 manifest를 만들고, (a) 같은 입력으로 두 번 만들면 ID가 같은지, (b) manifest만으로 학습 테이블을 다시 읽을 수 있는지, (c) 누군가 같은 경로에 파일을 "내용은 같지만 인코딩만 다르게" 덮어쓰면 검증이 잡아내는지, (d) 동의 철회 후에는 새 ID가 나오는지.

```python
# 데이터셋 manifest: catalog 쿼리 → (파일, sha256, 행 필터) 목록 → manifest 자체의 hash = dataset 버전 ID
import hashlib, json, sqlite3, pyarrow.dataset as ds, pyarrow.parquet as pq
from h3fmt import fw_pack
L = "/private/tmp/claude-501/h3/lake"; cat = sqlite3.connect(f"{L}/catalog.db")
sha = lambda p: hashlib.sha256(open(f"{L}/{p}", "rb").read()).hexdigest()
def build(name):
    rows = cat.execute("""SELECT path, session_id FROM sessions WHERE fw_num >= ? AND max_gap_s <= 1.0
                          AND consent = 1 ORDER BY path, session_id""", (fw_pack("1.4.0"),)).fetchall()
    files = {}
    for p, s in rows: files.setdefault(p, []).append(s)
    m = {"name": name, "query": "fw>=1.4.0 & max_gap<=1s & consent", "parser": "2.1.0",
         "files": [{"path": p, "sha256": sha(p), "sessions": ss} for p, ss in sorted(files.items())]}
    m["id"] = hashlib.sha256(json.dumps(m, sort_keys=True).encode()).hexdigest()[:12]
    return m
def verify(m):
    return [f["path"] for f in m["files"] if sha(f["path"]) != f["sha256"]] or "OK"
def load(m):                                             # manifest 그대로 학습 테이블 재구성
    return ds.dataset([f"{L}/{f['path']}" for f in m["files"]], format="parquet").to_table(
        filter=ds.field("session_id").isin([s for f in m["files"] for s in f["sessions"]]))
m1, m2 = build("imu_gesture_v1"), build("imu_gesture_v1")
print("manifest id:", m1["id"], "| rebuilt:", m2["id"], "| files:", len(m1["files"]),
      "| sessions:", sum(len(f["sessions"]) for f in m1["files"]), "| rows:", load(m1).num_rows)
json.dump(m1, open(f"{L}/manifest_{m1['id']}.json", "w"), indent=1)
p = m1["files"][0]["path"]                               # 안티패턴: 같은 경로에 '덮어쓰기' (내용 동일, 인코딩만 다름)
pq.write_table(pq.read_table(f"{L}/{p}"), f"{L}/{p}", row_group_size=10_000)
print("after in-place rewrite → verify:", verify(m1), "| rows still:", load(m1).num_rows)
cat.execute("UPDATE sessions SET consent = 0 WHERE device_id = 42")   # H6: 동의 철회
m3 = build("imu_gesture_v1")
print("after consent revoke → new id:", m3["id"], "sessions:", sum(len(f["sessions"]) for f in m3["files"]))
```

```text
manifest id: 1ca4d36e521a | rebuilt: 1ca4d36e521a | files: 3 | sessions: 12 | rows: 59250
after in-place rewrite → verify: ['silver/imu/dt=2026-09-28/part-0.parquet'] | rows still: 59250
after consent revoke → new id: 4763be46ea52 sessions: 11
```

출력에서 볼 것:

- 같은 catalog에서 두 번 만들면 ID가 같다 (`1ca4d36e521a`). 예제 4~8을 처음부터 다시 돌려도 이 ID가 같게 나왔다 — 파이프라인 전체가 결정적이라는 증거다.
- 같은 경로에 덮어쓰면 **행 수는 그대로(59,250)인데도** 검증이 실패한다. 이 경우는 내용이 같아서 무해하지만, 검증 입장에서는 "내용이 바뀌었는지 아닌지 모른다"가 핵심이다. 덮어쓰기를 허용하면 manifest의 약속이 깨진다. 그래서 파일 이름 자체를 해시로 짓는 **content-addressed** 경로(`…/<sha256>.parquet`)를 쓰면 덮어쓰기가 구조적으로 불가능해진다.
- 동의 철회(기기 42)를 반영하면 세션 12 → 11, 새 ID가 나온다. 옛 manifest `1ca4d36e521a`는 그대로 남지만, 이제 **그 사용자의 데이터를 포함하므로 학습에 다시 쓰면 안 된다** — 9.5절의 긴장 관계.

### 9.4 도구 — 직접 만들지 말고 이것들을 쓴다 (개념 수준)

| 도구 | 핵심 아이디어 | 어울리는 경우 |
|---|---|---|
| DVC | git 저장소에 작은 메타 파일(.dvc: 해시·크기)만 커밋, 실제 데이터는 원격 저장소에 해시 이름으로 | 연구팀, 파일 단위 데이터셋, 코드와 데이터 버전을 같이 |
| lakeFS | object storage 위에 git 같은 branch·commit·merge 제공 | 버킷 전체를 브랜치 단위로 실험·롤백 |
| Delta Lake | Parquet + 트랜잭션 로그(`_delta_log`), time travel, ACID 커밋 | Spark 중심 레이크하우스 |
| Apache Iceberg | Parquet + 메타데이터 트리(snapshot → manifest list → manifest → 파일), 스냅샷 단위 time travel, 스키마·파티션 진화 | 여러 엔진(Spark·Trino·Flink 등)이 같은 테이블 공유 |
| Apache Hudi | upsert·증분 처리 중심 테이블 포맷 | 갱신이 잦은 데이터 |

Iceberg의 이름 자체가 이 노트의 개념과 같다는 점이 재미있다 — snapshot, manifest, 파일 단위 통계. 우리가 예제 10에서 손으로 만든 것을 표준 포맷으로 만든 것이라고 이해하면 된다. 테이블 포맷을 쓰면 compaction(6.6절)과 "새 파일 쓰고 포인터 원자적 교체"도 커밋 하나로 처리된다. 정확한 기능·호환성은 버전마다 다르므로 도입 시 문서를 확인한다.

### 9.5 재현성 vs 삭제 의무 (H6)

불변 스냅샷과 "사용자 요청 시 삭제"는 정면으로 부딪친다. 실무 방향은 대략 이렇다 (법적 판단은 법무팀 몫):

- manifest와 catalog에 **사용자·기기 → 파일·행** 계보를 남겨, 삭제 요청 시 영향받는 스냅샷을 찾을 수 있게 한다.
- 삭제가 반영된 새 스냅샷을 만들고, 옛 스냅샷은 "사용 금지 + 기한 내 삭제"로 표시한다. 테이블 포맷의 스냅샷 만료(expire snapshots) 기능이 이 일을 한다.
- 학습된 모델에서 데이터를 "지우는" 문제(machine unlearning)는 별도 주제다. 보통은 다음 재학습에서 빼는 것으로 대응한다고 설명한다.

H5에서는 manifest 위에 사용자 단위 split, class 균형, dataset card를 얹는다.

---

## 10. 임베디드 관점에서 다시 보기 — 펌웨어가 백엔드를 돕는 법

### 10.1 백엔드 문제의 절반은 펌웨어에서 막을 수 있다

이 노트의 거의 모든 백엔드 기법은 **기기 헤더에 무엇이 있느냐**에 기대고 있다. Don이 펌웨어 쪽 owner로서 정할 수 있는 것들:

| 펌웨어가 넣을 것 | 백엔드에서 얻는 것 | 이 노트의 예제 |
|---|---|---|
| (device_id, session_id, chunk_idx) 고유 ID | 결정적 key → 멱등 업로드·처리 | 예제 3·4 |
| magic + format ver + `hdr_len` | 버전별 파서, 모르는 꼬리 건너뛰기 | 예제 2·9 |
| CRC32 (헤더+레코드) | 손상 즉시 판별 → DLQ | 예제 4 |
| fw 버전(정수 묶음) + 빌드 해시 | 영향 범위 질의, 문자열 비교 함정 회피 | 예제 8·9 |
| 센서 설정 또는 config hash | 단위 변환, 분포 차이 설명 | 3.1절 |
| seq 카운터, monotonic t0, UTC 앵커 | gap 검출, 정렬 (G7) | 예제 8 |
| 재부팅 후에도 단조 증가하는 session_id | key 충돌 방지 | 예제 3 (CONFLICT) |
| 청크 크기 정책 | PUT 수, small files, BLE 재전송 단위 | 예제 1·7 |

### 10.2 코드로 확인 — 기기 쪽 헤더와 서버 쪽 해석이 같은지

헤더 레이아웃은 펌웨어(C 구조체)와 서버(Python `struct`)에 **두 번** 정의된다. 둘이 어긋나면 8절 같은 버그가 생긴다. 가장 싼 방어는 같은 입력에 대한 바이트·CRC가 같은지 CI에서 비교하는 것이다.

**예제 11** — 무엇을 확인하나: C의 packed 구조체로 만든 v2 헤더가 48바이트이고 `n`·`utc_ms` 오프셋이 3.3절 표와 같은지, 그리고 C로 계산한 CRC32가 Python `zlib.crc32`와 같은지.

```c
/* 기기 쪽: v2 청크 헤더 구조체 + CRC32 (zlib와 같은 다항식 0xEDB88320, reflected) */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
typedef struct __attribute__((packed)) {
    char     magic[4];   uint8_t ver, hdr_len, sensor, flags;
    uint32_t device_id, fw, session_id, chunk_idx;
    uint64_t t0_us;      uint16_t odr_hz, n;
    uint8_t  acc_fs_g, pad[3];
    uint64_t utc_ms;                              /* v2에서 추가 — 반드시 '끝에' 붙인다 */
} chunk_hdr_v2_t;
_Static_assert(sizeof(chunk_hdr_v2_t) == 48, "v2 header must be 48 bytes");
static uint32_t crc32(const uint8_t *p, size_t len) {
    uint32_t c = 0xFFFFFFFFu;
    while (len--) { c ^= *p++; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & -(c & 1u)); }
    return ~c;
}
int main(void) {
    chunk_hdr_v2_t h; memset(&h, 0, sizeof h);
    memcpy(h.magic, "HKLG", 4); h.ver = 2; h.hdr_len = 48; h.sensor = 1; h.device_id = 42;
    h.fw = (1u << 16) | (4u << 8); h.session_id = 902; h.chunk_idx = 7; h.t0_us = 5000000;
    h.odr_hz = 50; h.n = 4; h.acc_fs_g = 4; h.utc_ms = 1790000000000ull;
    printf("sizeof=%zu offsetof(n)=%zu offsetof(utc_ms)=%zu crc32(hdr)=0x%08X\n", sizeof h,
           (size_t)((uint8_t *)&h.n - (uint8_t *)&h), (size_t)((uint8_t *)&h.utc_ms - (uint8_t *)&h),
           (unsigned)crc32((const uint8_t *)&h, sizeof h));
    return 0;
}
```

```python
import zlib
from h3fmt import HDR_V2, fw_pack
h = HDR_V2.pack(b"HKLG", 2, 48, 1, 0, 42, fw_pack("1.4.0"), 902, 7, 5_000_000, 50, 4, 4, 1_790_000_000_000)
print(f"size={len(h)} crc32(hdr)=0x{zlib.crc32(h):08X}")
```

```text
sizeof=48 offsetof(n)=34 offsetof(utc_ms)=40 crc32(hdr)=0x1607EAFB
size=48 crc32(hdr)=0x1607EAFB
```

출력에서 볼 것: `cc -std=c11 -Wall -Wextra -O2`로 경고 없이 컴파일됐고, 크기 48, `n` 오프셋 34(예제 9의 버그 파서가 읽은 위치), `utc_ms` 오프셋 40, CRC가 Python과 정확히 같다. 이런 "골든 바이트" 테스트를 펌웨어 CI와 서버 CI 양쪽에 두면 8절의 버그는 배포 전에 잡힌다. 참고로 `__attribute__((packed))`는 GCC/Clang 확장이다. 이 레이아웃은 모든 필드가 자연 정렬돼 있어 packed 없이도 48바이트지만, 의도를 명시하려고 붙였다. little-endian 가정도 명시적으로 문서화한다 (Cortex-M은 보통 little-endian).

### 10.3 MCU 예산 관점

- CRC32는 많은 MCU에 하드웨어 CRC 유닛이 있다. 없으면 테이블 방식(1 KB 테이블)이 위 비트 루프보다 수 배 빠르다.
- 헤더 48 B / 청크 3.5 KB(250샘플) ≈ 1.4% 오버헤드. 청크를 키우면 오버헤드와 PUT 수가 줄지만 RAM 버퍼와 "유실 시 잃는 양"이 커진다.
- 기기에서 압축(H1)을 하면 업로드 바이트는 줄지만, 서버 bronze는 압축된 원본을 그대로 보관하고 파서가 해제한다 — 압축 포맷도 버전 필드의 일부다.

---

## 11. 흔한 실수와 증상

| 실수 | 증상 | 원인 | 고치는 법 |
|---|---|---|---|
| 원본을 보관하지 않고 파싱 결과만 저장 | 파서 버그 발견 후 복구 불가 | bronze 레이어 없음 | 불변 bronze + replay 경로 (예제 9) |
| ledger 확인만으로 중복 방지 | 가끔 같은 행이 두 번 들어감 | check-then-act 경합 (예제 4 `dup_race`) | 결정적 출력 경로 + `INSERT OR IGNORE`/조건부 쓰기 |
| 처리 전에 ack/ledger 기록 | 워커 재시작 후 데이터 영구 유실 | at-most-once 순서 | 출력 → ledger → ack 순서 |
| 재시도 횟수 제한 없음 | 워커 하나가 같은 메시지만 붙잡고 backlog 증가 | poison message | 최대 시도 + DLQ + 사유별 경보 |
| 청크당 Parquet 파일 하나로 끝 | 질의가 점점 느려지고 GET 비용 증가 | small files (예제 7: 3.8배 크기, 수십 배 느림) | 주기적 compaction, 목표 파일 크기 |
| device_id로 파티션 | 디렉터리 수백만 개, 메타데이터 폭발 | 고 cardinality 파티션 | dt로 파티션 + 파일 안 정렬 (예제 6) |
| fw 버전을 문자열로 비교 | 1.10 기기가 "1.4 이상"에서 빠짐 | 사전순 비교 (예제 8) | 정수 묶음 또는 튜플 열 |
| 같은 경로에 silver 덮어쓰기 | 과거 실험 재현 불가, manifest 검증 실패 | 가변 파일 (예제 10) | 새 경로 + 포인터 교체, content-addressed 이름 |
| 필드 의미를 바꾸고 버전 유지 | 같은 열에 단위가 섞임, 분포가 두 봉우리 | 스키마 진화 규칙 위반 | 새 필드 + 포맷 버전 증가 |
| DLQ를 아무도 안 봄 | 특정 fw의 데이터가 조용히 비어 있음 | 모니터링 부재 | DLQ 크기·사유를 H7 대시보드에, 세션 gap과 연결 |

---

## 12. 면접에서 이렇게 말한다

**Q.** "Design the backend to ingest sensor logs from 10,000 devices."

**A.** 숫자부터: 기기당 50 MB/day면 월 15 TB, 평균 6 MB/s, 충전 시간 피크를 4배로 잡아도 워커 몇 개 수준이다 — 대역폭보다 객체 수와 누적 저장이 문제다. 구조: 기기는 (device, session, chunk) ID·버전·CRC가 있는 청크를 presigned URL로 bronze 버킷에 올린다 → object-created 이벤트가 큐로 → 워커가 버전별 파서로 검증·파싱해 dt 파티션 Parquet(silver)과 catalog에 쓰고, 실패는 DLQ로. at-least-once 큐 + 멱등 쓰기, 주기적 compaction, lifecycle로 오래된 raw는 cold. 모델팀은 catalog 질의 → 버전 붙은 manifest로 데이터셋을 받는다. SSD 테스트 인프라에서 SDK·스케줄러·결과 DB를 만든 경험과 같은 구조다.

> "I'd start with numbers. At 50 megabytes per device per day, 10,000 devices is about 15 terabytes a month and roughly 6 megabytes per second on average — even with a 4x nightly charging peak that's a handful of workers, so the real drivers are object count and cumulative storage. Devices upload self-describing chunks — device, session and chunk IDs, format version, firmware version, CRC — through presigned URLs into an immutable raw bucket. Object-created events go to a queue; stateless workers validate and parse with versioned parsers, write date-partitioned Parquet plus a row in a metadata catalog, and send anything malformed to a dead-letter queue. Delivery is at-least-once, so every write is idempotent and keyed by the chunk ID. A compaction job keeps files large, lifecycle rules move old raw data to cold storage, and training sets are cut as versioned manifests from catalog queries. It's very close to the test infrastructure I built for SSD validation — an SDK on the producer side, a scheduler and queue, and a results database everyone queried."

**Q.** "Why Parquet, and how would you partition sensor data?"

**A.** 컬럼 저장이라 필요한 열만 읽고, 열마다 인코딩·압축이 잘 되고, footer의 min/max 통계로 row group을 건너뛴다. 실험에서 3백만 샘플이 CSV 183 MB, Parquet zstd 17 MB였고, "하루·기기 하나·두 열" 질의는 압축 바이트의 1%만 필요했다. 파티션은 거의 모든 질의에 들어가는 저 cardinality 열 — 날짜(그리고 센서 종류는 테이블로 분리). 기기 ID는 파티션하면 small files가 폭발하므로 파일 안에서 정렬해 row group 통계로 거른다.

> "Parquet is columnar, so a query reads only the columns it needs, similar values sit together and compress well, and per-row-group min/max statistics in the footer let the reader skip data before reading it. In a small experiment, three million IMU samples were 183 megabytes as CSV and 17 as zstd Parquet, and a one-device, one-day, two-column query needed about one percent of the compressed bytes. I partition by date because nearly every query, retention rule and reprocessing job is date-bounded, and keep different sensors in separate tables. I don't partition by device ID — with 10,000 devices that's millions of tiny files — instead I sort by device and time inside each file so row-group statistics do the pruning, and run compaction so files stay in the hundreds-of-megabytes range."

**Q.** "You discover a parser bug after ingesting three months of data. What do you do?"

**A.** 먼저 영향받은 silver를 플래그하고 그걸로 만든 데이터셋 manifest를 찾아 모델팀에 알린다. catalog에서 parser_version과 포맷 버전으로 영향 범위를 좁히고, 버그를 재현하는 테스트와 함께 파서를 고친다. 그다음 bronze에서 그 범위만 replay해 새 버전 경로에 쓰고, 물리적 불변식(정지 시 1 g, seq 연속)으로 검증한 뒤 catalog 포인터를 바꾼다. 옛 파일은 참조하는 manifest가 정리될 때까지 둔다. 이게 가능한 건 원본을 불변으로 보관했기 때문이다. 재발 방지로 그 불변식을 ingestion QA에 넣는다.

> "First, contain it: flag the affected curated data and find every dataset manifest built from it so the model team knows which experiments are suspect. Then scope it from the catalog — parser version times log-format version — fix the parser with a regression test that reproduces the bug, and replay only the affected raw objects from the immutable bronze layer into a new versioned output path, on a separate lower-priority queue so live ingestion isn't starved. I validate with physical invariants — gravity is 1 g at rest, sequence numbers are contiguous — then atomically switch the catalog to the new files, keeping the old ones until no manifest references them. In a simulation of exactly this bug, the broken parser showed 0.5 percent sequence continuity and zero mean gravity, and the replay touched only the 24 affected sessions. Finally I'd add those invariants as automatic checks at ingestion so the next one is caught in hours, not months."

**Q.** "How do you make ingestion idempotent?"

**A.** 큐는 at-least-once라 중복은 반드시 온다. 그래서 (1) 기기가 청크마다 고유 ID를 붙이고 그것으로 bronze key를 정해 재업로드가 같은 key로 가게 하고, (2) 워커 출력 경로를 입력 key에서 결정적으로 만들어 두 번 써도 같은 결과가 되게 하고, (3) 처리 ledger에 key를 primary key로 기록한다. ledger 확인만으로는 두 워커가 동시에 같은 메시지를 받을 때 막지 못한다 — 시뮬레이션에서 중복 45건 중 39건이 그런 경합이었지만 결정적 경로 덕분에 결과는 하나였다. 순서는 출력 → ledger → ack.

> "Assume every message can arrive more than once. The device gives each chunk a unique ID — device, session, chunk index — and that ID becomes the raw object key, so a re-upload lands on the same key, and a conditional put tells me if the same key ever arrives with different bytes. Workers derive their output path deterministically from the input key, so processing twice just rewrites identical bytes, and they record the key in a ledger with a primary-key constraint. The ledger check alone isn't enough — in my simulation most duplicates were two workers grabbing the same message at the same moment and both passing the check — but because the write itself was idempotent the result was still exactly one copy. Order matters too: write output, then record, then acknowledge."

**Q.** "How do you make training datasets reproducible?"

**A.** 데이터셋을 "질의"가 아니라 "불변 파일 목록 + 각 파일 sha256 + 행 선택 + 파서·질의 버전"인 manifest로 고정하고, manifest를 정규화해 해시한 값을 데이터셋 ID로 쓴다. 학습 run은 코드 커밋과 함께 이 ID를 기록한다. 파일은 절대 덮어쓰지 않고 새 경로에 쓴다. 직접 만들 수도 있지만 보통 DVC, lakeFS, Iceberg/Delta 스냅샷을 쓴다. 삭제 요청(동의 철회)과의 충돌은 계보를 남기고 새 스냅샷을 만들어 옛 것을 만료시키는 방식으로 푼다.

> "A dataset is a manifest, not a query: the exact list of immutable files with their content hashes, the row selection, and the parser and query versions, and the hash of that canonical manifest is the dataset ID that gets logged with the code commit for every training run. Files are never overwritten in place — changes go to new paths, copy-on-write — and a verify step re-hashes everything before training. In practice I'd use a table format with snapshots like Iceberg or Delta, or DVC or lakeFS, rather than hand-rolling it. The one tension is deletion: when a user withdraws consent, lineage in the manifest tells me which snapshots contain their data, I cut a new snapshot without them and expire the old ones."

**Q.** "A firmware bug starts sending malformed logs from part of the fleet. How does your pipeline behave?"

**A.** 워커가 CRC·magic·버전·길이를 순서대로 검사하고, 재시도해도 소용없는 오류는 바로 DLQ로 보낸다 — poison message가 워커를 붙잡지 않는다. bronze에는 원본이 남아 있으니 원인 분석 후 파서로 처리할 수 있는 경우(예: 새 포맷 버전)는 파서를 추가하고 DLQ를 redrive한다. DLQ 사유별 개수와 fw 버전별 비율을 대시보드에 올려 "fw 1.11-dev에서 unknown_ver_3 급증" 같은 경보를 건다. 세션 catalog에는 그로 인한 gap이 보이므로 데이터셋에서 자동으로 걸러진다.

> "Validation is layered — length, magic, CRC, format version, record length — and anything that fails deterministically goes straight to the dead-letter queue instead of being retried, so a poison message can't pin a worker. The raw bytes are still in the immutable bucket, so once we understand the bug, if it's parseable — say an unreleased format version — I add a parser and redrive the DLQ; if it's genuinely corrupt it stays quarantined as evidence. DLQ counts by reason and firmware version are on the fleet dashboard with alerts, and since the lost chunks show up as gaps in the session catalog, dataset queries filter those sessions out automatically."

**Q.** "How do you evolve the log schema without breaking older devices?"

**A.** fleet에는 항상 여러 fw가 섞여 있으니 서버는 모든 살아 있는 포맷 버전을 읽어야 한다. 규칙: 필드는 끝에 추가만, 의미를 바꾸려면 새 필드, 헤더에 버전과 길이를 넣어 옛 리더가 모르는 꼬리를 건너뛰게 한다. 서버는 버전별 파서로 분기하고 없는 필드는 null/기본값으로 채우며, 기기 C 구조체와 서버 파서가 같은 골든 바이트로 CI에서 검증된다. protobuf 같은 스키마 언어를 쓰면 이 규칙 대부분이 내장된다.

> "Assume the fleet always runs several firmware versions, so the backend must read every live format. Fields are only appended, never repurposed — a meaning change gets a new field — and every chunk carries a format version and header length so an older reader can skip a tail it doesn't understand. The server dispatches to a parser per version and fills missing fields with nulls or defaults, and both the firmware struct and the server parser are checked against the same golden bytes in CI. With a schema language like protobuf most of those rules come built in, which is a good reason to use one for anything beyond a fixed sample header."

---

## 13. 직접 해보기

1. 손계산: 기기 10,000대가 IMU 100 Hz × 6축 int16을 하루 16시간, 그리고 이벤트 주변 오디오 16 kHz·16-bit를 하루 2분 올린다. 하루 총량과 월(30일) TB는? 정답: IMU 69.1 MB + 오디오 32,000 B/s × 120 s = 3.84 MB → 약 73 MB/기기/일 → 730 GB/일 → 약 21.9 TB/월.
2. 손계산: 예제 1에서 기기당 200 MB/day를 4 MB 대신 256 KB 청크로 올리면 월 PUT 수와 PUT 비용(가정 단가 $0.005/1,000건)은? 정답: 10,000 × 200 / 0.256 × 30 ≈ 2.34억 건 → 약 $1,172/월 (4 MB일 때 $75의 약 16배).
3. 손계산: 예제 6에서 파일 안을 기기 순으로 정렬하지 않고 row group마다 모든 기기가 섞여 있다면, "dt=09-03, device 23" 질의는 row group을 몇 개 읽어야 하나? 정답: 해당 파일의 4개 전부 — 모든 row group의 device_id min/max가 0..39가 되어 통계로 건너뛸 수 없다. 파티션 pruning(파일 1/5)은 여전히 동작한다.
4. 코드 과제: 포맷 v3를 만들어 헤더 끝에 `temp_c_x100`(int16)과 pad를 추가하고, `h3parse.py`가 모르는 버전이라도 `hdr_len`을 믿고 v2 필드까지만 읽는 "forward-compatible" 모드를 추가하라. 예제 4의 `unknown_ver_3` DLQ 8건이 어떻게 바뀌나? 힌트: ver ≥ 2이면 `HDR_V2.unpack_from`으로 앞 48 B를 읽고 레코드 시작을 `hdr_len`으로 잡는다. 단 예제 4의 v3 청크는 시뮬레이터가 48 B 헤더로 만들었으므로 그대로 파싱돼 DLQ에서 빠진다 — 정책상 "모르는 버전은 격리"가 더 안전한지 토론해 보라.
5. 코드 과제: 예제 4의 워커를 `multiprocessing.Pool`로 바꾸고, ledger를 처리 **전에** 기록하는 버전을 만들어 일시 오류 후 어떤 청크가 영원히 빠지는지 세어 보라. 힌트: 재시도 때 ledger에 이미 있으니 `dup_skip`으로 빠진다 → 일시 오류를 낸 청크 수만큼 silver에서 사라진다.
6. 코드 과제: 예제 10의 manifest 파일 경로를 content-addressed로 바꿔라 — compaction 결과를 `silver/imu/cas/<sha256[:2]>/<sha256>.parquet`으로 저장하고 catalog에는 dt → 해시 매핑을 둔다. 같은 경로 덮어쓰기 실험(예제 10의 안티패턴)이 왜 불가능해지는지 설명하라. 정답: 내용이 바뀌면 해시가 바뀌어 다른 경로가 되므로, 기존 경로의 파일은 구조적으로 바뀔 수 없다.

---

## 14. 용어 사전

| 용어 | 뜻 | 한 줄 설명 |
|---|---|---|
| ingestion | 수집·적재 | 외부 데이터를 받아 검증하고 저장소에 넣는 전체 과정 |
| object storage | 객체 저장소 | key → 바이트 덩어리, 부분 수정 없음 (S3, GCS) |
| bucket / key / prefix | 버킷 / 이름 / 이름 앞부분 | 권한·lifecycle·나열의 단위 |
| presigned URL | 서명된 임시 업로드 주소 | 기기에 비밀키 없이 특정 key 하나만 쓰게 허용 |
| lifecycle rule | 수명 규칙 | N일 뒤 cold로 이동 또는 삭제 |
| bronze / silver / gold | raw / curated / features | 원본 → 정제 → 학습용 레이어 |
| immutability | 불변성 | 한번 쓴 것은 고치지 않고 새로 쓴다 |
| idempotency | 멱등성 | 여러 번 해도 한 번 한 것과 결과가 같음 |
| at-least-once | 최소 한 번 전달 | 유실은 없지만 중복 가능 |
| effectively-once | 결과적 정확히 한 번 | at-least-once + 멱등 처리 |
| DLQ | dead-letter queue | 처리 불가 메시지 격리 큐 |
| poison message | 독 메시지 | 처리할 때마다 실패하는 메시지 |
| backpressure | 배압 | 소비자가 느릴 때 생산을 늦추거나 버퍼링 |
| ordering key | 순서 키 | 같은 key끼리만 순서 보장 |
| Parquet | 컬럼 저장 파일 포맷 | row group · column chunk · page · footer 통계 |
| row group | 행 묶음 | Parquet 안의 수평 분할, 통계 단위 |
| predicate pushdown | 조건 내려보내기 | 읽기 전에 통계로 거름 |
| column pruning | 열 선택 | 필요한 열만 읽음 |
| hive partitioning | `이름=값` 디렉터리 | `dt=2026-09-30/` 경로에서 열 값을 얻음 |
| small files problem | 작은 파일 문제 | 파일당 오버헤드로 느려지고 비싸짐 |
| compaction | 합치기 | 작은 파일을 큰 파일로 다시 쓰기 |
| catalog | 메타데이터 DB | 세션·파일·버전·QA를 질의 가능하게 |
| schema evolution | 스키마 진화 | 이미 데이터가 있는 상태에서 형식 변경 |
| lineage | 계보 | 이 행이 어느 원본에서 어떤 코드로 왔나 |
| manifest | 목록 | 데이터셋을 이루는 파일 + 해시 + 선택 조건 |
| content hash / content-addressed | 내용 해시 / 내용으로 주소 지정 | 이름 = 내용의 해시 → 덮어쓰기 불가 |
| snapshot / time travel | 스냅샷 / 과거 시점 조회 | 테이블 포맷(Iceberg·Delta)이 제공 |
| DVC / lakeFS | 데이터 버전 도구 | git 메타 + 원격 저장 / 버킷 위 branch·commit |

---

## 15. 요약 & 체크리스트

ingestion은 "업로드된 바이트"를 "질의할 수 있고 버전이 붙은 데이터셋"으로 바꾸는 일이다. 숫자부터 낸다 — 10k 기기 × 50 MB/day = 월 15 TB, 평균 6 MB/s이고, 설계를 좌우하는 것은 대역폭보다 객체 수와 누적 저장이다. 원본은 bronze에 불변으로 두고, silver(dt 파티션 Parquet)와 catalog는 언제든 원본에서 다시 만든다. 큐는 at-least-once라 중복이 반드시 오므로 결정적 key·결정적 출력 경로·ledger로 쓰기 자체를 멱등하게 만들고(예제 4: 중복 45건, 결과 947개 그대로), 영구 오류는 바로 DLQ로 보내고, 장애가 끝나면 전체 replay도 안전하다. Parquet은 열 선택·인코딩·통계로 읽는 양을 줄이고(예제 6: 압축 바이트의 1.1%만 필요), 파티션은 날짜로, 기기는 파일 안 정렬로, small files는 compaction으로 다룬다. catalog는 fw를 정수로 비교하고 QA·동의·parser_version·해시를 담는다. 파서 버그는 catalog로 범위를 좁혀 bronze에서 그 부분만 재처리한다(예제 9). 학습 데이터셋은 manifest(파일 + sha256 + 선택 조건)의 해시를 ID로 고정하고, 파일은 덮어쓰지 않는다. 그리고 이 모든 것은 펌웨어가 헤더에 ID·버전·길이·CRC·설정·시계 정보를 제대로 넣어 줄 때 쉬워진다.

- [ ] 기기 수 × MB/day에서 TB/월, 평균·피크 MB/s, 워커 수, PUT 수를 암산할 수 있다
- [ ] bronze/silver/gold의 차이와 "bronze는 불변"인 이유를 설명할 수 있다
- [ ] at-least-once + 멱등 처리로 effectively-once를 만드는 세 가지 도구(결정적 key, 결정적 출력, ledger)를 말할 수 있다
- [ ] 일시 오류와 영구 오류를 구분하고 재시도·DLQ·redrive 흐름을 그릴 수 있다
- [ ] Parquet의 row group·column chunk·footer 통계가 pushdown에 어떻게 쓰이는지 설명할 수 있다
- [ ] 무엇으로 파티션하고 무엇은 파일 안 정렬로 처리할지 cardinality로 판단할 수 있다
- [ ] small files 문제와 compaction을 숫자(파일 수·크기·스캔 시간)로 설명할 수 있다
- [ ] catalog에 들어갈 열을 고르고, fw 버전 문자열 비교 함정을 피할 수 있다
- [ ] 파서 버그 발견 시 차단 → 범위 → 수정 → replay → 검증 → 전환 절차를 말할 수 있다
- [ ] manifest + content hash로 재현 가능한 데이터셋 ID를 만들고, 삭제 의무와의 긴장을 설명할 수 있다

---

## 참고 자료

- Martin Kleppmann, "Designing Data-Intensive Applications" (O'Reilly, 2017) — 3장(저장·컬럼 저장), 11장(스트림 처리, 전달 보장, 멱등성)
- Apache Parquet 문서 (파일 포맷, 인코딩, 통계): https://parquet.apache.org/docs/
- Apache Arrow — pyarrow Dataset API (`pyarrow.dataset`, 파티셔닝, 필터): https://arrow.apache.org/docs/python/dataset.html
- Apache Arrow — pyarrow Parquet (`pyarrow.parquet`, 메타데이터·row group): https://arrow.apache.org/docs/python/parquet.html
- Apache Kafka 문서 (파티션, consumer group, offset, 전달 보장): https://kafka.apache.org/documentation/
- Amazon S3 사용자 가이드 — 성능 최적화(prefix당 요청률), lifecycle, event notifications, 조건부 요청: https://docs.aws.amazon.com/AmazonS3/latest/userguide/
- Amazon SQS 개발자 가이드 — visibility timeout, dead-letter queue, FIFO: https://docs.aws.amazon.com/AWSSimpleQueueService/latest/SQSDeveloperGuide/
- Google Cloud Pub/Sub 문서 — ack deadline, ordering keys, dead-letter topics, exactly-once delivery: https://cloud.google.com/pubsub/docs
- Google Cloud Storage 문서 — preconditions(`ifGenerationMatch`), lifecycle, Pub/Sub notifications: https://cloud.google.com/storage/docs
- Apache Iceberg (테이블 포맷, snapshot·manifest): https://iceberg.apache.org/
- Delta Lake (트랜잭션 로그, time travel): https://delta.io/
- DVC (Data Version Control): https://dvc.org/doc
- lakeFS: https://docs.lakefs.io/
- DuckDB (Parquet·S3 직접 질의): https://duckdb.org/docs/
- 클라우드 가격은 반드시 각 서비스의 현재 공식 가격 페이지에서 확인 (이 노트의 단가는 대략적 가정값)
- 이 노트 세트: A6(바이너리 로그·pandas), G7(seq·gap·time QA), H1(로그 포맷), H2(전송·재개), H5(데이터셋 관리), H6(프라이버시·동의), H7(품질 모니터링), H8(fleet 수집)
