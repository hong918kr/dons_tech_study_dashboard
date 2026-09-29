#!/usr/bin/env python3
"""이 폴더의 *.md -> 같은 이름의 .html (job-research 렌더러 재사용).

사용: python3 build_study_notes.py
md가 원본이다. 생성된 html은 직접 고치지 않는다.
"""
import os
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[1] / "job_interview_prep_research"  # 지원 현황판이 있는 곳
sys.path.insert(0, str(Path.home() / ".claude/skills/job-research/scripts"))
import render_html as rh  # noqa: E402

# 렌더러는 <dir>/<file> 깊이를 가정해 "../<현황판>"으로 링크한다 → 실제 상대경로로 교체
dash_rel = os.path.relpath(ROOT / rh.DASH_HTML, HERE)

for md in sorted(HERE.glob("*.md")):
    out = rh.render_context(md, ROOT)
    html = out.read_text(encoding="utf-8")
    html = html.replace('href="../' + rh.DASH_HTML + '"', 'href="' + dash_rel + '"')
    out.write_text(html, encoding="utf-8")
    print("rendered", out)
