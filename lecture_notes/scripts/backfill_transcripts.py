"""
자막이 없는(transcript.txt 누락) 강의 폴더만 골라 자막을 **천천히** 재시도한다.
IP 차단이 풀린 뒤 이어받기(resume)용. yt-dlp 메타는 다시 받지 않는다.

사용:
    python scripts/backfill_transcripts.py            # 전체 누락분
    python scripts/backfill_transcripts.py --delay 2  # 요청 간격(초)
    python scripts/backfill_transcripts.py --limit 30 # 이번 실행 최대 개수

차단(IpBlocked)이 연속 감지되면 "아직 차단 중"으로 보고 조기 종료한다(재실행 가능).
"""
from __future__ import annotations

import argparse
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import LECTURES_DIR  # noqa: E402
from fetch_transcript import TranscriptBlocked, save_transcript  # noqa: E402
from utils import read_json, setup_console  # noqa: E402

setup_console()

BLOCK_STOP = 3  # 연속 차단 N회면 조기 종료


def missing_dirs() -> list[Path]:
    out = []
    for d in sorted(LECTURES_DIR.iterdir()):
        if d.is_dir() and (d / "meta.json").exists() and not (d / "transcript.txt").exists():
            out.append(d)
    return out


def main() -> int:
    ap = argparse.ArgumentParser()
    ap.add_argument("--delay", type=float, default=1.5, help="요청 간격(초)")
    ap.add_argument("--limit", type=int, default=0, help="이번 실행 최대 개수(0=무제한)")
    args = ap.parse_args()

    dirs = missing_dirs()
    if args.limit:
        dirs = dirs[: args.limit]
    print(f"[backfill] 자막 누락 {len(dirs)}개 시도 (간격 {args.delay}s)")

    ok = none = 0
    consecutive_block = 0
    for i, d in enumerate(dirs, 1):
        meta = read_json(d / "meta.json", {})
        vid = meta.get("id")
        if not vid:
            continue
        try:
            got = save_transcript(d, vid, duration_sec=meta.get("duration_sec"))
            consecutive_block = 0
            if got:
                ok += 1
                print(f"  [{i}/{len(dirs)}] OK   {vid}  {meta.get('title','')[:50]}", flush=True)
            else:
                none += 1
                print(f"  [{i}/{len(dirs)}] none {vid}  (자막 없음/비공개)", flush=True)
        except TranscriptBlocked:
            consecutive_block += 1
            print(f"  [{i}/{len(dirs)}] BLOCKED {vid}  (IP 차단)", file=sys.stderr, flush=True)
            if consecutive_block >= BLOCK_STOP:
                print(
                    f"\n[stop] IP가 아직 차단 상태입니다 ({BLOCK_STOP}회 연속). "
                    f"잠시 뒤 다시 실행하세요. (지금까지 자막 {ok}개 추가)",
                    file=sys.stderr,
                )
                return 2
            time.sleep(args.delay * 4)  # 차단 시 더 길게 쉼
            continue
        time.sleep(args.delay)

    print(f"\n[done] 자막 추가 {ok} · 자막 없음 {none} · 남은 누락 {len(missing_dirs())}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
