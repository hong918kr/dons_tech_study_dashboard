"""
자막(transcript) → 강의 노트(notes.md) 생성.

두 가지 경로:
  1. 기본(항상 동작): 자막을 읽기 좋게 정리하고, 챕터/구간별로 쪼갠
     **구조화 초안** 노트를 만든다. (LLM 불필요)
  2. 선택(ANTHROPIC_API_KEY + anthropic 설치 시): 자막을 Claude에 보내
     요약·핵심개념·구간정리가 들어간 **풀노트**를 생성한다.

사용:
    python scripts/note_builder.py <lecture_dir 또는 video_id>
    python scripts/note_builder.py --all          # 노트 없는 폴더 전부
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

from config import (
    ANTHROPIC_API_KEY,
    LECTURES_DIR,
    NOTE_MAX_TOKENS,
    NOTE_MODEL,
    source_label,
)
from utils import read_json, setup_console

setup_console()


# ── 자막 파싱/정리 ────────────────────────────────────────────
def parse_srt(srt_text: str) -> list[dict[str, Any]]:
    """srt → [{start, end, text}] (초 단위)."""
    segments: list[dict[str, Any]] = []
    blocks = re.split(r"\n\s*\n", srt_text.strip())
    time_re = re.compile(
        r"(\d{2}):(\d{2}):(\d{2})[,.](\d{3})\s*-->\s*(\d{2}):(\d{2}):(\d{2})[,.](\d{3})"
    )
    for block in blocks:
        lines = block.splitlines()
        if len(lines) < 2:
            continue
        m = time_re.search(block)
        if not m:
            continue
        h1, m1, s1, ms1, h2, m2, s2, ms2 = map(int, m.groups())
        start = h1 * 3600 + m1 * 60 + s1 + ms1 / 1000
        end = h2 * 3600 + m2 * 60 + s2 + ms2 / 1000
        text_lines = [ln for ln in lines if not time_re.search(ln) and not ln.strip().isdigit()]
        text = " ".join(ln.strip() for ln in text_lines if ln.strip())
        if text:
            segments.append({"start": start, "end": end, "text": text})
    return segments


def _dedupe_join(texts: list[str]) -> str:
    """유튜브 자동자막의 줄 겹침을 줄이며 한 덩어리로 합친다."""
    out: list[str] = []
    prev = ""
    for t in texts:
        t = re.sub(r"\s+", " ", t).strip()
        if not t or t == prev:
            continue
        # 직전 끝과 현재 시작이 겹치면 겹친 부분 제거
        if prev and prev.endswith(t[: min(len(t), 12)]):
            pass
        out.append(t)
        prev = t
    joined = " ".join(out)
    return re.sub(r"\s+", " ", joined).strip()


def to_paragraphs(text: str, sentences_per_para: int = 5) -> str:
    """문장 단위로 끊어 문단으로 묶는다 (가독성)."""
    sentences = re.split(r"(?<=[.!?。])\s+", text)
    sentences = [s.strip() for s in sentences if s.strip()]
    paras: list[str] = []
    for i in range(0, len(sentences), sentences_per_para):
        paras.append(" ".join(sentences[i : i + sentences_per_para]))
    return "\n\n".join(paras)


def fmt_ts(sec: float) -> str:
    s = int(sec)
    h, rem = divmod(s, 3600)
    m, sec2 = divmod(rem, 60)
    return f"{h}:{m:02d}:{sec2:02d}" if h else f"{m}:{sec2:02d}"


def split_into_sections(
    segments: list[dict[str, Any]], chapters: list[dict[str, Any]], target_parts: int = 8
) -> list[tuple[str, str]]:
    """(헤더, 본문) 섹션 리스트. 챕터 있으면 챕터별, 없으면 시간 균등 분할."""
    if not segments:
        return []

    def collect(lo: float, hi: float) -> str:
        texts = [s["text"] for s in segments if lo <= s["start"] < hi]
        return to_paragraphs(_dedupe_join(texts))

    sections: list[tuple[str, str]] = []
    if chapters:
        for ch in chapters:
            start = float(ch.get("start_time") or 0)
            end = float(ch.get("end_time") or segments[-1]["end"])
            title = ch.get("title") or "구간"
            body = collect(start, end)
            if body:
                sections.append((f"[{fmt_ts(start)}] {title}", body))
    else:
        total_end = segments[-1]["end"]
        step = total_end / target_parts if target_parts else total_end
        for i in range(target_parts):
            lo = i * step
            hi = (i + 1) * step if i < target_parts - 1 else total_end + 1
            body = collect(lo, hi)
            if body:
                sections.append((f"[{fmt_ts(lo)}] Part {i + 1}", body))
    return sections


# ── 기본(비-LLM) 구조화 초안 노트 ──────────────────────────────
def build_draft_note(meta: dict[str, Any], segments: list[dict[str, Any]]) -> str:
    title = meta.get("title") or "(제목 없음)"
    url = meta.get("url") or f"https://www.youtube.com/watch?v={meta.get('id','')}"
    src = source_label(meta.get("channel"))
    date = meta.get("upload_date") or "????-??-??"
    duration = meta.get("duration") or "??:??"
    chapters = meta.get("chapters") or []

    out: list[str] = [f"# {title}", ""]
    out.append(f"> {src} · {date} · {duration} · [{url}]({url})")
    out.append("")

    if not segments:
        out.append("## ⚠️ 자막 없음")
        out.append("")
        out.append("이 영상은 자막을 가져오지 못했습니다 (자막 비공개 또는 IP 차단).")
        desc = (meta.get("description") or "").strip()
        if desc:
            out.append("")
            out.append("## 설명 (description)")
            out.append("")
            out.append(desc)
        return "\n".join(out) + "\n"

    # 목차
    sections = split_into_sections(segments, chapters)
    if chapters:
        out.append("## 📑 목차 (챕터)")
        out.append("")
        for ch in chapters:
            out.append(f"- **[{fmt_ts(float(ch.get('start_time') or 0))}]** {ch.get('title') or ''}")
        out.append("")

    # 구간별 정리
    out.append("## 📝 구간별 자막 정리")
    out.append("")
    out.append("> 자막을 시간 구간별로 정리한 **초안**입니다. "
               "(요약 풀노트는 Claude 연동 시 자동 생성 — README 참고)")
    out.append("")
    for header, body in sections:
        out.append(f"### {header}")
        out.append("")
        out.append(body)
        out.append("")

    # 학습 메모 (빈칸)
    out.append("## ✍️ 학습 메모")
    out.append("")
    out.append("- ")
    out.append("")
    out.append("---")
    out.append(f"<sub>자막 기반 구조화 초안 · video_id: `{meta.get('id','')}`</sub>")
    out.append("")
    return "\n".join(out)


# ── 선택: Claude 풀노트 ───────────────────────────────────────
_LLM_PROMPT = """You are an expert study-note writer. Below is the full transcript of a \
university lecture ("{title}" from {source}). Write thorough, well-structured study notes \
in Markdown that a student could review instead of re-watching.

Requirements:
- Start with `## 📌 Summary` — 3-5 sentences on what the lecture covers.
- `## 🎯 Key Concepts` — bulleted list of the core ideas/terms with one-line explanations.
- `## 📚 Detailed Notes` — walk through the lecture in order, using `###` subheadings for \
each major topic. Explain definitions, derivations, examples, and intuition clearly. Use \
bullet lists, numbered steps, and fenced code/math blocks where helpful.
- `## 🔑 Takeaways` — the most important things to remember.
- Write in clear English (the lecture is in English). Do not invent content not in the \
transcript. Do not include the raw transcript verbatim.

TRANSCRIPT:
{transcript}
"""


def build_llm_note(meta: dict[str, Any], transcript_text: str) -> str | None:
    """anthropic + 키가 있으면 Claude로 풀노트 생성. 실패 시 None."""
    if not ANTHROPIC_API_KEY:
        return None
    try:
        from anthropic import Anthropic
    except ImportError:
        print("[info] anthropic 미설치 — 초안 노트만 생성 (pip install anthropic)", file=sys.stderr)
        return None

    title = meta.get("title") or ""
    src = source_label(meta.get("channel"))
    # 과도하게 긴 자막은 컷 (대략 보호용)
    transcript = transcript_text[:120_000]
    prompt = _LLM_PROMPT.format(title=title, source=src, transcript=transcript)

    client = Anthropic(api_key=ANTHROPIC_API_KEY)
    try:
        resp = client.messages.create(
            model=NOTE_MODEL,
            max_tokens=NOTE_MAX_TOKENS,
            messages=[{"role": "user", "content": prompt}],
        )
    except Exception as ex:  # 네트워크/쿼터 등
        print(f"[warn] Claude 노트 생성 실패: {ex}", file=sys.stderr)
        return None

    body = "".join(b.text for b in resp.content if getattr(b, "type", "") == "text").strip()
    if not body:
        return None

    url = meta.get("url") or ""
    date = meta.get("upload_date") or ""
    duration = meta.get("duration") or ""
    header = f"# {title}\n\n> {src} · {date} · {duration} · [{url}]({url})\n\n"
    footer = f"\n\n---\n<sub>Claude({NOTE_MODEL}) 자막 요약 풀노트 · video_id: `{meta.get('id','')}`</sub>\n"
    return header + body + footer


# ── 진입점 ───────────────────────────────────────────────────
def resolve_dir(arg: str) -> Path | None:
    p = Path(arg)
    if p.is_dir():
        return p
    matches = list(LECTURES_DIR.glob(f"*_{arg[:8]}*"))
    return matches[0] if matches else None


def build_for_dir(lec_dir: Path, force: bool = False) -> str:
    """폴더 1개에 대해 notes.md 생성. 반환: 'llm' | 'draft' | 'skip'."""
    notes_path = lec_dir / "notes.md"
    if notes_path.exists() and not force:
        return "skip"
    meta = read_json(lec_dir / "meta.json", {})
    srt_path = lec_dir / "transcript.srt"
    txt_path = lec_dir / "transcript.txt"
    segments = parse_srt(srt_path.read_text(encoding="utf-8")) if srt_path.exists() else []

    note = None
    kind = "draft"
    if txt_path.exists():
        note = build_llm_note(meta, txt_path.read_text(encoding="utf-8"))
        if note:
            kind = "llm"
    if note is None:
        note = build_draft_note(meta, segments)
        kind = "draft"
    notes_path.write_text(note, encoding="utf-8")
    return kind


def main() -> int:
    ap = argparse.ArgumentParser(description="자막 → 강의 노트 생성")
    ap.add_argument("target", nargs="?", help="lecture_dir 또는 video_id")
    ap.add_argument("--all", action="store_true", help="notes.md 없는 폴더 전부")
    ap.add_argument("--force", action="store_true", help="이미 있어도 다시 생성")
    args = ap.parse_args()

    dirs: list[Path] = []
    if args.all:
        dirs = [d for d in sorted(LECTURES_DIR.glob("*")) if d.is_dir()]
    elif args.target:
        d = resolve_dir(args.target)
        if not d:
            print(f"[error] 폴더를 찾을 수 없음: {args.target}", file=sys.stderr)
            return 1
        dirs = [d]
    else:
        ap.print_help()
        return 2

    for d in dirs:
        kind = build_for_dir(d, force=args.force)
        print(f"[{kind:5s}] {d.name}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
