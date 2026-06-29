"""
영상 1개의 자막 + 메타데이터를 가져와 lectures/{date}_{slug}_{id8}/ 에 저장.

사용:
    python scripts/fetch_transcript.py <URL_or_VIDEO_ID>

산출물:
    lectures/{date}_{slug}_{id8}/
        meta.json            - 메타데이터
        transcript.srt       - 타임스탬프 포함 자막
        transcript.txt       - 평문 자막
        transcript.meta.json - 자막 언어/스니펫 수
"""
from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path
from typing import Any

from yt_dlp import YoutubeDL
from youtube_transcript_api import YouTubeTranscriptApi
from youtube_transcript_api._errors import CouldNotRetrieveTranscript


class TranscriptBlocked(RuntimeError):
    """YouTube가 IP를 일시 차단(과다요청) — 나중에 재시도하면 됨."""

from config import LECTURES_DIR, PREFERRED_SUBTITLE_LANGS
from utils import (
    fmt_duration,
    fmt_upload_date,
    lecture_dir,
    now_iso,
    setup_console,
    write_json,
)

setup_console()

_VIDEO_ID_RE = re.compile(r"^[A-Za-z0-9_-]{11}$")


def extract_video_id(arg: str) -> str:
    """URL 또는 video_id에서 11자리 video_id 추출."""
    arg = arg.strip()
    if _VIDEO_ID_RE.match(arg):
        return arg
    m = re.search(r"youtu\.be/([A-Za-z0-9_-]{11})", arg)
    if m:
        return m.group(1)
    m = re.search(r"[?&]v=([A-Za-z0-9_-]{11})", arg)
    if m:
        return m.group(1)
    m = re.search(r"youtube\.com/(?:shorts|live|embed)/([A-Za-z0-9_-]{11})", arg)
    if m:
        return m.group(1)
    raise ValueError(f"video_id를 추출할 수 없습니다: {arg!r}")


def fetch_metadata(video_id: str) -> dict[str, Any]:
    """yt-dlp로 메타데이터 추출 (다운로드 없음)."""
    opts: dict[str, Any] = {"quiet": True, "no_warnings": True, "skip_download": True}
    url = f"https://www.youtube.com/watch?v={video_id}"
    with YoutubeDL(opts) as ydl:
        info = ydl.extract_info(url, download=False)
    return {
        "id": info.get("id"),
        "title": info.get("title"),
        "url": info.get("webpage_url") or url,
        "channel": info.get("channel"),
        "channel_id": info.get("channel_id"),
        "uploader": info.get("uploader"),
        "upload_date": fmt_upload_date(info.get("upload_date")),
        "duration_sec": info.get("duration"),
        "duration": fmt_duration(info.get("duration")),
        "view_count": info.get("view_count"),
        "like_count": info.get("like_count"),
        "playable_in_embed": info.get("playable_in_embed"),
        "description": info.get("description") or "",
        "tags": info.get("tags") or [],
        "categories": info.get("categories") or [],
        "chapters": [
            {
                "title": c.get("title"),
                "start_time": c.get("start_time"),
                "end_time": c.get("end_time"),
            }
            for c in (info.get("chapters") or [])
        ],
    }


def fetch_transcript_snippets(
    video_id: str, languages: list[str]
) -> tuple[list[dict[str, Any]], str]:
    """자막 가져오기. 반환: (snippets, used_language)."""
    api = YouTubeTranscriptApi()
    try:
        fetched = api.fetch(video_id, languages=languages)
    except CouldNotRetrieveTranscript as ex:
        msg = str(ex)
        low = msg.lower()
        if "blocking requests from your ip" in low or "too many requests" in low:
            raise TranscriptBlocked(msg) from ex
        raise RuntimeError(f"자막 없음/비공개: {ex}") from ex

    used_lang = getattr(fetched, "language_code", None) or "?"
    snippets: list[dict[str, Any]] = []
    for s in fetched:
        snippets.append(
            {
                "text": getattr(s, "text", "") or "",
                "start": float(getattr(s, "start", 0.0)),
                "duration": float(getattr(s, "duration", 0.0)),
            }
        )
    return snippets, used_lang


def _fmt_srt_time(sec: float) -> str:
    ms = int(round(sec * 1000))
    h, rem = divmod(ms, 3_600_000)
    m, rem = divmod(rem, 60_000)
    s, ms = divmod(rem, 1000)
    return f"{h:02d}:{m:02d}:{s:02d},{ms:03d}"


def to_srt(snippets: list[dict[str, Any]]) -> str:
    lines = []
    for i, s in enumerate(snippets, start=1):
        start = float(s["start"])
        end = start + float(s["duration"])
        lines.append(str(i))
        lines.append(f"{_fmt_srt_time(start)} --> {_fmt_srt_time(end)}")
        lines.append(str(s["text"]))
        lines.append("")
    return "\n".join(lines)


def to_plain_text(snippets: list[dict[str, Any]]) -> str:
    return "\n".join(str(s["text"]).strip() for s in snippets if str(s["text"]).strip())


def save_transcript(
    out_dir: Path, video_id: str, langs: list[str] | None = None, duration_sec=None
) -> bool:
    """기존 폴더에 자막 파일들을 저장. 성공 True. 자막 없음 False.
    IP 차단 시 TranscriptBlocked 예외를 던진다(재시도용)."""
    langs = langs or PREFERRED_SUBTITLE_LANGS
    try:
        snippets, used_lang = fetch_transcript_snippets(video_id, langs)
    except TranscriptBlocked:
        raise
    except RuntimeError:
        return False  # 자막 없음/비공개
    (out_dir / "transcript.srt").write_text(to_srt(snippets), encoding="utf-8")
    (out_dir / "transcript.txt").write_text(to_plain_text(snippets), encoding="utf-8")
    write_json(
        out_dir / "transcript.meta.json",
        {
            "video_id": video_id,
            "language": used_lang,
            "snippet_count": len(snippets),
            "duration_sec": duration_sec,
            "fetched_at": now_iso(),
        },
    )
    return True


def fetch_one(video_arg: str, langs: list[str] | None = None, force: bool = False) -> Path:
    """영상 1개를 받아 폴더에 저장하고 폴더 경로를 반환. (서버에서 import해 사용)"""
    langs = langs or PREFERRED_SUBTITLE_LANGS
    vid = extract_video_id(video_arg)
    meta = fetch_metadata(vid)
    if not meta.get("title"):
        raise RuntimeError(f"메타데이터를 가져오지 못함: {vid}")

    out_dir = lecture_dir(LECTURES_DIR, meta["upload_date"], meta["title"], vid)
    if out_dir.exists() and not force and list(out_dir.glob("transcript.*")):
        return out_dir  # 이미 받아둠

    out_dir.mkdir(parents=True, exist_ok=True)
    write_json(out_dir / "meta.json", {**meta, "fetched_at": now_iso()})

    # 메타는 저장됨. 자막 실패(없음/차단)해도 폴더는 반환 (목록엔 표시됨).
    try:
        save_transcript(out_dir, vid, langs, meta.get("duration_sec"))
    except TranscriptBlocked:
        pass
    return out_dir


def main() -> int:
    p = argparse.ArgumentParser(description="유튜브 영상 1개의 자막+메타 추출")
    p.add_argument("video", help="URL 또는 video_id")
    p.add_argument("--langs", nargs="+", default=PREFERRED_SUBTITLE_LANGS)
    p.add_argument("--force", action="store_true", help="이미 있어도 덮어쓰기")
    args = p.parse_args()

    try:
        out_dir = fetch_one(args.video, args.langs, args.force)
    except (ValueError, RuntimeError) as ex:
        print(f"[error] {ex}", file=sys.stderr)
        return 1
    has_tx = bool(list(out_dir.glob("transcript.txt")))
    print(f"[done] {out_dir}  (자막: {'있음' if has_tx else '없음'})")
    print(str(out_dir))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
