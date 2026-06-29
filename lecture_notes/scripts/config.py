"""전역 설정. 경로와 자막 언어, 노트 생성용 모델 설정."""
from __future__ import annotations

import os
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DATA_DIR = ROOT / "data"
LECTURES_DIR = ROOT / "lectures"
SECTIONS_DIR = DATA_DIR / "sections"  # 편집형 섹션 마크다운 저장소
PROBLEMS_DIR = DATA_DIR / "problems"  # 코딩 문제 풀이/현황 저장소

# 자막 언어 우선순위 (영어 강의 대상). a.en = 자동생성 영어 자막.
PREFERRED_SUBTITLE_LANGS = ["en", "en-US", "en-GB", "en-orig", "a.en"]

# 서버
HOST = "127.0.0.1"
PORT = 5055

# ── 노트 생성 (선택: Claude LLM) ───────────────────────────────
# ANTHROPIC_API_KEY 가 설정되어 있고 anthropic 패키지가 깔려 있으면
# 자막을 Claude 로 요약해 풀노트를 만든다. 없으면 자막 기반 구조화 초안만 생성.
ANTHROPIC_API_KEY = os.environ.get("ANTHROPIC_API_KEY", "")
NOTE_MODEL = os.environ.get("NOTE_MODEL", "claude-sonnet-4-6")
NOTE_MAX_TOKENS = int(os.environ.get("NOTE_MAX_TOKENS", "8000"))

# 알려진 채널 → 보기 좋은 출처 라벨 (사이드바 그룹핑용)
SOURCE_LABELS = {
    "stanford online": "Stanford",
    "stanfordonline": "Stanford",
    "mit opencourseware": "MIT",
    "mitocw": "MIT",
}


def source_label(channel: str | None) -> str:
    """채널명을 짧은 출처(학교) 라벨로. 매칭 없으면 채널명 그대로."""
    if not channel:
        return "기타"
    key = channel.strip().lower()
    return SOURCE_LABELS.get(key, channel.strip())


# 제목에서 강의 코드를 뽑는다. 예) "Stanford CS230 | ..." → "CS230",
#                              "Lec 1 | MIT 18.06 ..." → "18.06"
# 예) "CS230", "EE364A", "CME295" — 대문자 2~4 + (선택 공백) + 숫자
_COURSE_RE = re.compile(r"\b([A-Z]{2,4})\s?(\d{2,4}[A-Z]?)(?!\.\d)\b")
# MIT 형식 "18.06", "6.034"
_MIT_NUM_RE = re.compile(r"\b(\d{1,2}\.\d{2,3}[A-Z]?)\b")
_LEC_RE = re.compile(r"(?:Lecture|Lec)\s*\.?\s*(\d+)", re.IGNORECASE)


def course_code(title: str | None) -> str:
    """제목에서 강의 코드 추출. 없으면 '기타'."""
    if not title:
        return "기타"
    m = _COURSE_RE.search(title)
    if m:
        return f"{m.group(1)}{m.group(2)}"
    m = _MIT_NUM_RE.search(title)
    if m:
        return m.group(1)
    return "기타"


def lecture_order(title: str | None) -> int:
    """제목의 'Lecture N' / 'Lec N' → 정렬용 번호. 없으면 큰 값."""
    m = _LEC_RE.search(title or "")
    return int(m.group(1)) if m else 9999


# 강의 코드 → 짧은 이름. machine learning→ML, deep learning→DL 로 축약.
COURSE_NAMES = {
    "CS224R": "Deep RL",
    "CME295": "Transformers & LLMs",
    "CME296": "Diffusion & Vision",
    "CS336": "LM from Scratch",
    "CS231N": "Deep Learning for CV",
    "CS224W": "Machine Learning with Graphs",
    "CS224N": "NLP with Deep Learning",
    "CS149": "Parallel Computing",
    "CS153": "Frontier Systems",
    "CS236": "Deep Generative Models",
    "CS230": "Deep Learning",
    "CS229": "Machine Learning",
    "CS25": "Transformers",
    "18.06": "Linear Algebra",
}

_ABBR = [
    (re.compile(r"machine learning", re.I), "ML"),
    (re.compile(r"deep learning", re.I), "DL"),
]


def course_name(code: str | None) -> str:
    """강의 코드의 짧은 이름. 없으면 빈 문자열. ML/DL 축약 적용."""
    name = COURSE_NAMES.get(code or "", "")
    for pat, repl in _ABBR:
        name = pat.sub(repl, name)
    return name


# ── 대시보드 상위 5개 옵션 ────────────────────────────────────
# 강의노트(lectures)는 전용 페이지(/lectures), 나머지 4개는 편집형 섹션.
# 편집형 섹션. 각 섹션은 하위 항목(topic)들로 구성된다. seed = 기본 항목.
EDIT_SECTIONS = {
    "research": {
        "label": "Research Subject",
        "icon": "🔬",
        "hint": "관심 있는 연구 주제·논문·키워드를 항목별로 정리하세요.",
        "seed": [],
    },
    "job": {
        "label": "Job별 Study노트",
        "icon": "💼",
        "hint": "목표 직무(Job)별로 필요한 역량·학습 내용을 정리하세요.",
        "seed": [],
    },
    "interview": {
        "label": "인터뷰 연습",
        "icon": "🧑‍💻",
        "hint": "인터뷰 준비를 주제별로 정리하세요.",
        "seed": ["Embedded", "ML", "DSAL", "Embedded System Design", "SWE System Design"],
    },
    "datacenter": {
        "label": "데이터센터",
        "icon": "🗄️",
        "hint": "데이터셋·인프라·자료 출처를 모아두는 공간입니다.",
        "seed": [],
    },
}

# 상위 내비게이션 (표시 순서). key='lectures'는 강의 대시보드로 연결.
NAV = [
    {"key": "research", "label": "Research Subject", "icon": "🔬", "href": "/section/research"},
    {"key": "job", "label": "Job별 Study노트", "icon": "💼", "href": "/section/job"},
    {"key": "lectures", "label": "강의노트", "icon": "🎓", "href": "/lectures"},
    {"key": "interview", "label": "인터뷰 연습", "icon": "🧑‍💻", "href": "/section/interview"},
    {"key": "problems", "label": "코딩 문제", "icon": "💻", "href": "/problems"},
    {"key": "datacenter", "label": "데이터센터", "icon": "🗄️", "href": "/section/datacenter"},
]
