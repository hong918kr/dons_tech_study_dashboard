# 강의노트 (Lecture Notes)

YouTube 강의 링크를 붙여넣으면 **자막을 받아 강의 노트로 정리**해주는 단순한 웹 대시보드입니다.
(예: Stanford [@stanfordonline](https://www.youtube.com/@stanfordonline),
MIT [@mitocw](https://www.youtube.com/@mitocw))

> `asset_study/durumi_lecture_note` 프로젝트의 핵심 흐름(자막 수집 → 노트 생성 → 열람)만
> 떼어내 단순화한 버전입니다. Don's Pick / memo / journal / 채널 자동탐지 / 진행률 등
> 심화 기능은 모두 제거했습니다.

---

## 동작 흐름

1. 홈(또는 왼쪽 사이드바)에 YouTube 강의 링크를 붙여넣기
2. `yt-dlp`로 메타데이터, `youtube-transcript-api`로 **영어 자막**을 받아
   `lectures/<날짜>_<제목>_<id8>/` 에 저장
3. 자막을 시간 구간(챕터)별로 정리한 **구조화 노트**(`notes.md`) 자동 생성
4. 출처(Stanford / MIT / …)별로 묶인 사이드바에서 클릭해 열람 — 영상 임베드 + 노트 + 원본 자막

---

## 설치 & 실행

```powershell
cd C:\workspace\don_tech_study\lecture_notes
python -m pip install -r requirements.txt
python scripts\server.py
```

콘솔에 `http://127.0.0.1:5055` 가 뜨면 브라우저로 접속하세요.
(또는 `start_server.bat` 더블클릭 → 서버 + 브라우저 자동 실행)

- **종료**: 서버 콘솔에서 `Ctrl + C`
- `.py` 코드 수정 후엔 서버 재시작 / `webapp/`(HTML·CSS·JS)만 바꿨으면 `Ctrl + F5`

---

## (선택) Claude로 요약 풀노트 생성

기본은 **자막 기반 구조화 초안**(LLM 불필요)만 만듭니다.
Claude로 요약·핵심개념이 들어간 풀노트를 원하면:

```powershell
python -m pip install anthropic
$env:ANTHROPIC_API_KEY = "sk-ant-..."   # 키 설정
python scripts\server.py
```

- 모델은 기본 `claude-sonnet-4-6` (환경변수 `NOTE_MODEL`로 변경 가능)
- 키가 있으면 새로 받는 강의는 자동으로 풀노트, 기존 강의는 강의 페이지의
  **“↻ 노트 다시 생성”** 버튼으로 업그레이드

---

## 명령줄 사용 (선택)

```powershell
# 자막+메타만 받기
python scripts\fetch_transcript.py <링크 또는 video_id>

# 재생목록(playlist) 통째로 받기 — 노트는 안 만들고 목록만 채움
python scripts\fetch_playlists.py            # data/playlists.json 의 전체
python scripts\fetch_playlists.py --list     # 재생목록/영상 수 현황

# 자막이 빠진 강의만 천천히 이어받기 (IP 차단 풀린 뒤)
python scripts\backfill_transcripts.py

# YouTube 자막 API IP 차단 여부만 점검
python scripts\check_ip_block.py             # ✅ OK / ❌ BLOCKED

# 받아둔 폴더로 노트 생성 / 다시 생성
python scripts\note_builder.py <video_id>            # 1개
python scripts\note_builder.py --all                 # 노트 없는 것 전부
python scripts\note_builder.py <video_id> --force    # 덮어쓰기
```

> **자막 IP 차단 주의**: 짧은 시간에 많은 영상을 받으면 YouTube가 자막 API를
> 일시 차단합니다. 메타데이터(목록)는 영향 없이 저장되며, 자막은
> `check_ip_block.py`로 풀렸는지 확인 후 `backfill_transcripts.py`로 이어받으세요.

---

## 디렉토리 구조

```
lecture_notes/
├─ scripts/
│  ├─ server.py            # Flask 웹서버 (이 파일 실행)
│  ├─ fetch_transcript.py  # 자막 + 메타 수집
│  ├─ note_builder.py      # 자막 → notes.md (초안 / Claude 풀노트)
│  ├─ note_renderer.py     # notes.md → HTML
│  ├─ config.py / utils.py
├─ webapp/
│  ├─ templates/           # base / home / lecture
│  └─ static/              # styles.css / app.js
├─ lectures/               # 강의별 폴더 (자막·메타·notes.md) — 실행 중 생성
├─ requirements.txt
└─ start_server.bat
```

## 코딩 문제 (AlgoExpert 스타일)

상단 **💻 코딩 문제** 탭 — 문제를 보고 C 코드를 작성해 **컴파일 & 테스트**합니다.

- 왼쪽: 문제 목록 (🟢 통과 / 🟡 작성중 / ⚪ 미시작)
- 가운데: 문제 설명 + 함수 시그니처
- 오른쪽: 코드 에디터 + **▶ 컴파일 & 테스트** + 콘솔(통과/실패, 컴파일 에러, 시간초과)
- 컴파일러: **clang / clang++** (`C:\Program Files\LLVM\bin`). C·C++ 모두 지원. 코드 자동 저장.
- 채점: `RESULT`/`[PASS]·[FAIL]` 자동 채점, 또는 출력 비교(수동 — 기대 출력 표시).
- 문제 출처:
  - 인터뷰 레포 import — `python scripts/import_problems.py` (로컬 레포의 프롬프트 파일을 문제로 변환 → `data/problems/imported.json`). 현재 Anduril 100ea 폴더 17문제.
  - 직접 만든 샘플 10문제 — `scripts/problems_data.py`.
- 다른 회사 폴더 추가: `import_problems.py` 의 `TARGETS` 에 `(상대경로, "라벨")` 추가 후 재실행.

## 참고

- 자막이 없는(자막 비공개) 영상은 메타·설명만으로 노트를 만듭니다.
- 서버는 링크 1건을 받을 때 자막을 **동기로** 가져오므로 긴 강의는 잠시 멈춘 듯 보일 수 있습니다.
