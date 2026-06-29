"""
YouTube 자막 API의 IP 차단 여부를 확인한다 (자막 다운로드와 분리된 점검용).

사용:
    python scripts/check_ip_block.py

동작:
  - 자막이 확실히 있는 영상 1개로 자막 조회를 시도
  - 차단이면 'BLOCKED', 정상이면 'OK' 출력 + 남은 자막 누락 개수 표시
종료코드: 0=정상, 2=차단, 1=기타 오류
"""
from __future__ import annotations

import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import LECTURES_DIR  # noqa: E402
from fetch_transcript import (  # noqa: E402
    TranscriptBlocked,
    fetch_transcript_snippets,
)
from utils import setup_console  # noqa: E402

setup_console()

# 영어 자막이 확실히 있는 안정적인 영상 (MIT 18.06 Lecture 1)
CHECK_VIDEO = "ZK3O402wf1c"


def missing_transcripts() -> int:
    if not LECTURES_DIR.exists():
        return 0
    return sum(
        1
        for d in LECTURES_DIR.iterdir()
        if d.is_dir() and (d / "meta.json").exists() and not (d / "transcript.txt").exists()
    )


def is_blocked() -> bool:
    """True면 현재 IP 차단 상태."""
    try:
        fetch_transcript_snippets(CHECK_VIDEO, ["en", "en-US", "a.en"])
        return False
    except TranscriptBlocked:
        return True
    except RuntimeError:
        # 차단은 아님 (이 영상 자막을 못 찾는 다른 이유) → 차단 아님으로 간주
        return False


def main() -> int:
    missing = missing_transcripts()
    print(f"[check] 테스트 영상: {CHECK_VIDEO}")
    try:
        blocked = is_blocked()
    except Exception as ex:  # noqa: BLE001
        print(f"[check] 확인 실패(기타 오류): {ex}", file=sys.stderr)
        return 1

    if blocked:
        print(f"❌ BLOCKED — YouTube가 현재 IP를 차단 중입니다. (자막 누락 {missing}개)")
        print("   → 잠시(수십 분~수 시간) 뒤 'python scripts/backfill_transcripts.py' 재시도")
        return 2
    print(f"✅ OK — 자막 API 정상입니다. (자막 누락 {missing}개)")
    if missing:
        print("   → 지금 받기: python scripts/backfill_transcripts.py")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
