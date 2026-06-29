"""파일/문자열/시간 유틸. durumi 레퍼런스에서 가져와 단순화."""
from __future__ import annotations

import json
import re
import sys
from datetime import datetime
from pathlib import Path
from typing import Any


def setup_console() -> None:
    """Windows 콘솔에서 한글/유니코드 출력을 위해 UTF-8 재설정."""
    for name in ("stdout", "stderr"):
        stream = getattr(sys, name, None)
        reconfigure = getattr(stream, "reconfigure", None)
        if reconfigure:
            try:
                reconfigure(encoding="utf-8")
            except Exception:
                pass


_INVALID_FILENAME_CHARS = re.compile(r'[\\/:"*?<>|\r\n\t]+')
_MULTI_DASH = re.compile(r"-{2,}")


def slugify(text: str, max_length: int = 60) -> str:
    """폴더명으로 안전한 슬러그. 영문/한글 유지."""
    s = _INVALID_FILENAME_CHARS.sub("-", text).strip(" .-_")
    s = _MULTI_DASH.sub("-", s)
    return s[:max_length].rstrip(" .-_") or "untitled"


def fmt_upload_date(yt_date: str | None) -> str:
    """yt-dlp의 'YYYYMMDD' → 'YYYY-MM-DD'."""
    if not yt_date or len(yt_date) != 8:
        return "0000-00-00"
    return f"{yt_date[:4]}-{yt_date[4:6]}-{yt_date[6:8]}"


def fmt_duration(seconds: int | float | None) -> str:
    """초 → 'HH:MM:SS' 또는 'MM:SS'."""
    if seconds is None:
        return "??:??"
    s = int(seconds)
    h, rem = divmod(s, 3600)
    m, sec = divmod(rem, 60)
    return f"{h:02d}:{m:02d}:{sec:02d}" if h else f"{m:02d}:{sec:02d}"


def lecture_dir(lectures_root: Path, upload_date: str, title: str, video_id: str) -> Path:
    """강의 폴더 경로: {date}_{slug}_{id8}."""
    return lectures_root / f"{upload_date}_{slugify(title)}_{video_id[:8]}"


def read_json(path: Path, default: Any = None) -> Any:
    if not path.exists():
        return default
    return json.loads(path.read_text(encoding="utf-8"))


def write_json(path: Path, data: Any) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2), encoding="utf-8")


def now_iso() -> str:
    return datetime.now().isoformat(timespec="seconds")
