"""
재생목록(playlist) 단위로 영상의 자막+메타를 일괄 다운로드한다.
**노트(notes.md)는 만들지 않는다** — 대시보드 목록(목차)만 채운다.
노트는 이후 Claude Code가 강의별로 따로 작성.

사용:
    python scripts/fetch_playlists.py                # data/playlists.json 의 전체
    python scripts/fetch_playlists.py <PL_ID> ...    # 특정 재생목록만
    python scripts/fetch_playlists.py --list         # 진행 현황만 출력

레지스트리: data/playlists.json  (재생목록 id/제목/강의코드/영상수 — 기억용)
로그:       logs/fetch_playlists.log
"""
from __future__ import annotations

import sys
import time
from pathlib import Path

from yt_dlp import YoutubeDL

sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import DATA_DIR, LECTURES_DIR, course_code  # noqa: E402
from fetch_transcript import fetch_one  # noqa: E402
from utils import now_iso, read_json, setup_console, write_json  # noqa: E402

setup_console()

REGISTRY = DATA_DIR / "playlists.json"

# 기억해둔 재생목록 (사용자가 준 12개 — Stanford). 새로 추가하면 여기에.
DEFAULT_PLAYLISTS = [
    "PLoROMvodv4rPwxE0ONYRa_itZFdaKCylL",  # CS224R Deep Reinforcement Learning
    "PLoROMvodv4rOCXd21gf0CF4xr35yINeOy",  # CME295 Transformers & LLMs (Autumn 2025)
    "PLoROMvodv4rNdy8rt2rZ4T2xM0OjADnfu",  # CME296 Diffusion & Large Vision Models
    "PLoROMvodv4rMqXOcazWaTUHhq-yembLCV",  # CS336 Language Modeling from Scratch (Spring 2026)
    "PLoROMvodv4rObv1FMizXqumgVVdzX4_05",  # Large Language Models (LLMs)
    "PLoROMvodv4rOmsNzYBMe0gJY2XS8AQg16",  # CS231N Deep Learning for Computer Vision (2025)
    "PLoROMvodv4rOY23Y0BoGoBGgQ1zmU_MT_",  # CS336 Language Modeling from Scratch (2025)
    "PLoROMvodv4rOP-ImU-O1rYRg2RFxomvFp",  # CS224W Machine Learning with Graphs
    "PLoROMvodv4rOaMFbaqxPDoLWjDaRAdP9D",  # CS224N NLP with Deep Learning (Spring 2024)
    "PLoROMvodv4rMp7MTFr4hQsDEcX7Bx6Odp",  # CS149 Parallel Computing (2023)
    "PLoROMvodv4rN447WKQ5oz_YdYbS74M5IA",  # CS153 Frontier Systems
    "PLoROMvodv4rPOWA-omMM6STXaWW4FvJT8",  # CS236 Deep Generative Models (2023)
]


def flat_entries(pl_id: str) -> tuple[str, list[tuple[str, str]]]:
    """재생목록 제목과 (video_id, title) 목록 (flat, 다운로드 없음)."""
    url = f"https://www.youtube.com/playlist?list={pl_id}"
    opts = {"quiet": True, "no_warnings": True, "extract_flat": True, "skip_download": True}
    with YoutubeDL(opts) as y:
        info = y.extract_info(url, download=False)
    entries = [
        (e.get("id"), e.get("title") or "")
        for e in (info.get("entries") or [])
        if e.get("id")
    ]
    return info.get("title") or pl_id, entries


def update_registry(pl_id: str, title: str, entries: list[tuple[str, str]]) -> None:
    reg = read_json(REGISTRY, [])
    reg = [r for r in reg if r.get("id") != pl_id]
    # 강의 코드: 첫 영상 제목에서 추출 (없으면 재생목록 제목)
    code = "기타"
    for _, vt in entries:
        c = course_code(vt)
        if c != "기타":
            code = c
            break
    if code == "기타":
        code = course_code(title)
    reg.append(
        {
            "id": pl_id,
            "url": f"https://www.youtube.com/playlist?list={pl_id}",
            "title": title,
            "course": code,
            "count": len(entries),
            "video_ids": [vid for vid, _ in entries],
            "updated": now_iso(),
        }
    )
    write_json(REGISTRY, reg)


def already_have(vid: str) -> bool:
    return any(LECTURES_DIR.glob(f"*_{vid[:8]}*"))


def main() -> int:
    args = [a for a in sys.argv[1:] if a]
    if "--list" in args:
        reg = read_json(REGISTRY, [])
        for r in reg:
            print(f"{r['count']:3d}  [{r['course']}]  {r['title']}")
        print(f"\n총 강의(재생목록): {len(reg)}, 영상: {sum(r['count'] for r in reg)}")
        return 0

    playlists = args or DEFAULT_PLAYLISTS
    LECTURES_DIR.mkdir(parents=True, exist_ok=True)

    grand_ok = grand_skip = grand_fail = grand_notx = 0
    for pi, pl in enumerate(playlists, 1):
        try:
            title, entries = flat_entries(pl)
        except Exception as ex:  # noqa: BLE001
            print(f"[ERR] playlist {pl}: {ex}", file=sys.stderr)
            continue
        update_registry(pl, title, entries)
        print(f"\n=== ({pi}/{len(playlists)}) {title} — {len(entries)}개 ===", flush=True)

        for vi, (vid, vtitle) in enumerate(entries, 1):
            if already_have(vid):
                grand_skip += 1
                print(f"  [{vi:2d}/{len(entries)}] skip  {vid}  {vtitle[:50]}", flush=True)
                continue
            try:
                out_dir = fetch_one(vid)
                has_tx = bool(list(out_dir.glob("transcript.txt")))
                if has_tx:
                    grand_ok += 1
                else:
                    grand_notx += 1
                print(
                    f"  [{vi:2d}/{len(entries)}] {'OK ' if has_tx else 'meta'}  "
                    f"{vid}  {vtitle[:50]}",
                    flush=True,
                )
            except Exception as ex:  # noqa: BLE001
                grand_fail += 1
                print(f"  [{vi:2d}/{len(entries)}] FAIL  {vid}  {ex}", file=sys.stderr, flush=True)
            time.sleep(0.4)  # 가벼운 레이트 완화

    print(
        f"\n[summary] 자막+메타 {grand_ok} · 메타만(자막없음) {grand_notx} · "
        f"이미있음 {grand_skip} · 실패 {grand_fail}"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
