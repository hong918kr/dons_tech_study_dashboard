"""
강의노트 웹서버 (단순화 버전).

흐름:
  - 홈에서 YouTube 링크 입력 → 자막+메타 수집 → 노트 생성 → 강의 페이지로
  - 사이드바: 출처(Stanford/MIT/...)별로 강의 묶어 표시
  - 강의 페이지: 영상 임베드 + notes.md 렌더 + 원본 자막 토글

실행:
    python scripts/server.py   →  http://127.0.0.1:5055
"""
from __future__ import annotations

import re
import sys
import traceback
from pathlib import Path

from flask import (
    Flask,
    abort,
    jsonify,
    redirect,
    render_template,
    request,
    url_for,
)

# scripts/ 를 import 경로에 추가 (단독 실행 대비)
sys.path.insert(0, str(Path(__file__).resolve().parent))

from config import (  # noqa: E402
    EDIT_SECTIONS,
    HOST,
    LECTURES_DIR,
    NAV,
    PORT,
    SECTIONS_DIR,
    course_code,
    course_name,
    lecture_order,
    source_label,
)
from fetch_transcript import extract_video_id, fetch_one  # noqa: E402
from note_builder import build_for_dir  # noqa: E402
from note_renderer import render_notes_md  # noqa: E402
from utils import read_json, setup_console, write_json  # noqa: E402

setup_console()

WEBAPP = Path(__file__).resolve().parent.parent / "webapp"
app = Flask(
    __name__,
    template_folder=str(WEBAPP / "templates"),
    static_folder=str(WEBAPP / "static"),
)


# ── 강의 인덱싱 ──────────────────────────────────────────────
def lecture_record(d: Path) -> dict:
    meta = read_json(d / "meta.json", {})
    vid = meta.get("id") or d.name[-8:]
    has_tx = (d / "transcript.txt").exists()
    has_note = (d / "notes.md").exists()
    title = meta.get("title") or d.name
    return {
        "dir": d.name,
        "id": vid,
        "title": title,
        "source": source_label(meta.get("channel")),
        "course": course_code(title),
        "order": lecture_order(title),
        "channel": meta.get("channel") or "",
        "upload_date": meta.get("upload_date") or "",
        "duration": meta.get("duration") or "",
        "has_transcript": has_tx,
        "has_note": has_note,
        # 임베드 가능 여부 (None/누락 시 가능으로 간주)
        "embeddable": meta.get("playable_in_embed") is not False,
    }


def all_lectures() -> list[dict]:
    if not LECTURES_DIR.exists():
        return []
    return [lecture_record(d) for d in LECTURES_DIR.iterdir() if d.is_dir()]


def grouped_lectures() -> list[dict]:
    """학교 → 강의(course) 2단계 그룹.
    [{source, count, courses:[{course, count, videos:[...]}]}]."""
    schools: dict[str, dict[str, list[dict]]] = {}
    for r in all_lectures():
        schools.setdefault(r["source"], {}).setdefault(r["course"], []).append(r)

    out: list[dict] = []
    for source, courses in schools.items():
        course_list: list[dict] = []
        for course, vids in courses.items():
            # 강의 내 정렬: lecture 번호 → 날짜
            vids.sort(key=lambda v: (v["order"], v["upload_date"]))
            course_list.append(
                {
                    "course": course,
                    "name": course_name(course),
                    "count": len(vids),
                    "videos": vids,
                }
            )
        # 강의 그룹 정렬: 영상 많은 순 → 이름. '기타'는 뒤로.
        course_list.sort(key=lambda c: (c["course"] == "기타", -c["count"], c["course"]))
        total = sum(c["count"] for c in course_list)
        out.append({"source": source, "count": total, "courses": course_list})
    out.sort(key=lambda g: (-g["count"], g["source"]))
    return out


def find_dir_by_id(vid: str) -> Path | None:
    matches = [d for d in LECTURES_DIR.glob(f"*_{vid[:8]}*") if d.is_dir()]
    return matches[0] if matches else None


def lecture_ctx(current_vid: str = "") -> dict:
    """강의 사이드바/통계 컨텍스트."""
    groups = grouped_lectures()
    total = sum(g["count"] for g in groups)
    noted = sum(
        1 for g in groups for c in g["courses"] for v in c["videos"] if v["has_note"]
    )
    return {
        "groups": groups,
        "current_vid": current_vid,
        "stats": {"total": total, "noted": noted},
    }


@app.context_processor
def inject_nav() -> dict:
    """모든 템플릿에 상위 5개 내비를 주입."""
    return {"nav": NAV}


# ── 라우트 ───────────────────────────────────────────────────
@app.route("/")
def landing():
    """대시보드 메인 — 상위 5개 옵션 카드."""
    groups = grouped_lectures()
    lecture_total = sum(g["count"] for g in groups)
    return render_template("landing.html", active="", lecture_total=lecture_total)


# ── 강의노트 (전용 대시보드) ─────────────────────────────────
@app.route("/lectures")
def lectures_home():
    return render_template("lectures.html", active="lectures", **lecture_ctx())


@app.route("/add", methods=["POST"])
def add():
    raw = (request.form.get("url") or "").strip()
    if not raw:
        return redirect(url_for("lectures_home"))
    try:
        extract_video_id(raw)
    except ValueError:
        return render_template(
            "lectures.html",
            active="lectures",
            error=f"링크에서 video_id를 찾을 수 없습니다: {raw}",
            **lecture_ctx(),
        )
    try:
        out_dir = fetch_one(raw)
        build_for_dir(out_dir, force=request.form.get("force") == "1")
    except Exception as ex:  # noqa: BLE001
        traceback.print_exc()
        return render_template(
            "lectures.html", active="lectures", error=f"수집 실패: {ex}", **lecture_ctx()
        )
    vid = extract_video_id(raw)
    return redirect(url_for("lecture", vid=vid))


@app.route("/lecture/<vid>")
def lecture(vid: str):
    d = find_dir_by_id(vid)
    if not d:
        abort(404)
    rec = lecture_record(d)
    meta = read_json(d / "meta.json", {})
    notes_path = d / "notes.md"
    note_html = render_notes_md(notes_path.read_text(encoding="utf-8")) if notes_path.exists() else ""
    tx_path = d / "transcript.txt"
    transcript = tx_path.read_text(encoding="utf-8") if tx_path.exists() else ""
    return render_template(
        "lecture.html",
        active="lectures",
        rec=rec,
        meta=meta,
        note_html=note_html,
        transcript=transcript,
        **lecture_ctx(current_vid=vid),
    )


@app.route("/regenerate/<vid>", methods=["POST"])
def regenerate(vid: str):
    d = find_dir_by_id(vid)
    if not d:
        abort(404)
    build_for_dir(d, force=True)
    return redirect(url_for("lecture", vid=vid))


# ── 편집형 섹션 + 하위 항목(topic) ───────────────────────────
def _section_dir(key: str) -> Path:
    return SECTIONS_DIR / key


def _topics_file(key: str) -> Path:
    return _section_dir(key) / "_topics.json"


def _topic_slug(label: str) -> str:
    s = re.sub(r"[^\w]+", "-", label.strip(), flags=re.UNICODE).strip("-").lower()
    return s or "topic"


def load_topics(key: str) -> list[dict]:
    """섹션의 항목 목록. 없으면 seed로 초기화해 저장."""
    cfg = EDIT_SECTIONS[key]
    tf = _topics_file(key)
    topics = read_json(tf, None)
    if topics is None:
        topics = [{"slug": _topic_slug(lbl), "label": lbl} for lbl in cfg.get("seed", [])]
        write_json(tf, topics)
    # 각 항목에 내용 유무 표시
    for t in topics:
        md = _section_dir(key) / f"{t['slug']}.md"
        t["has_content"] = md.exists() and bool(md.read_text(encoding="utf-8").strip())
    return topics


def add_topic(key: str, label: str) -> str:
    topics = read_json(_topics_file(key), []) or []
    slug = _topic_slug(label)
    existing = {t["slug"] for t in topics}
    if slug in existing:  # 중복 회피
        n = 2
        while f"{slug}-{n}" in existing:
            n += 1
        slug = f"{slug}-{n}"
    topics.append({"slug": slug, "label": label.strip()})
    write_json(_topics_file(key), topics)
    return slug


def _render_section(key: str, selected: str | None):
    cfg = EDIT_SECTIONS[key]
    topics = load_topics(key)
    raw, html, sel_label = "", "", ""
    if selected:
        match = next((t for t in topics if t["slug"] == selected), None)
        if not match:
            abort(404)
        sel_label = match["label"]
        md = _section_dir(key) / f"{selected}.md"
        if md.exists():
            raw = md.read_text(encoding="utf-8")
            html = render_notes_md(raw) if raw.strip() else ""
    return render_template(
        "section.html",
        active=key,
        key=key,
        label=cfg["label"],
        icon=cfg["icon"],
        hint=cfg["hint"],
        topics=topics,
        selected=selected,
        selected_label=sel_label,
        raw=raw,
        html=html,
    )


@app.route("/section/<key>")
def section(key: str):
    if key not in EDIT_SECTIONS:
        abort(404)
    topics = load_topics(key)
    # 항목이 있으면 첫 항목으로
    if topics:
        return redirect(url_for("section_topic", key=key, slug=topics[0]["slug"]))
    return _render_section(key, None)


@app.route("/section/<key>/<slug>")
def section_topic(key: str, slug: str):
    if key not in EDIT_SECTIONS:
        abort(404)
    return _render_section(key, slug)


@app.route("/section/<key>/add", methods=["POST"])
def section_add(key: str):
    if key not in EDIT_SECTIONS:
        abort(404)
    label = (request.form.get("label") or "").strip()
    if not label:
        return redirect(url_for("section", key=key))
    slug = add_topic(key, label)
    return redirect(url_for("section_topic", key=key, slug=slug))


@app.route("/section/<key>/<slug>/save", methods=["POST"])
def section_topic_save(key: str, slug: str):
    if key not in EDIT_SECTIONS:
        abort(404)
    content = request.form.get("content", "")
    _section_dir(key).mkdir(parents=True, exist_ok=True)
    (_section_dir(key) / f"{slug}.md").write_text(content, encoding="utf-8")
    return redirect(url_for("section_topic", key=key, slug=slug))


# ── 코딩 문제 (AlgoExpert 스타일: 작성 → 컴파일 → 테스트) ────
from config import PROBLEMS_DIR  # noqa: E402
from problems_data import PROBLEMS, get_problem  # noqa: E402


def _solutions_dir() -> Path:
    return PROBLEMS_DIR / "solutions"


def _solution_path(pid: str) -> Path:
    return _solutions_dir() / f"{pid}.c"


def _solved_set() -> set:
    return set(read_json(PROBLEMS_DIR / "solved.json", []) or [])


def _mark_solved(pid: str, solved: bool) -> None:
    s = _solved_set()
    if solved:
        s.add(pid)
    else:
        s.discard(pid)
    write_json(PROBLEMS_DIR / "solved.json", sorted(s))


def _problem_list() -> list[dict]:
    """그냥 쭉 나열한 평평한 문제 목록 (그룹 없음)."""
    solved = _solved_set()
    out = []
    for p in PROBLEMS:
        out.append(
            {
                "id": p["id"],
                "num": p["num"],
                "title": p["title"],
                "range": p.get("range", ""),
                "lang": p.get("lang", "c"),
                "difficulty": p["difficulty"],
                "solved": p["id"] in solved,
                "started": _solution_path(p["id"]).exists(),
            }
        )
    # PROBLEMS 순서(카테고리→번호) 유지 — 같은 출처끼리 연속, 헤더는 없는 평평한 리스트
    return out


@app.route("/problems")
def problems_home():
    if PROBLEMS:
        return redirect(url_for("problem", pid=PROBLEMS[0]["id"]))
    return render_template("problems.html", active="problems", problems=[], p=None)


@app.route("/problems/<pid>")
def problem(pid: str):
    p = get_problem(pid)
    if not p:
        abort(404)
    sp = _solution_path(pid)
    code = sp.read_text(encoding="utf-8") if sp.exists() else p["starter"]

    # 설명 렌더: 가져온 문제(원본 코멘트)는 <pre>, 직접 만든 문제는 마크다운
    desc = p.get("description") or ""
    if p.get("source_file"):
        from markupsafe import escape

        desc_html = f"<pre class='spec'>{escape(desc)}</pre>" if desc.strip() else ""
    else:
        from note_renderer import render_notes_md

        desc_html = render_notes_md(desc) if desc.strip() else ""

    return render_template(
        "problems.html",
        active="problems",
        problems=_problem_list(),
        p=p,
        code=code,
        desc_html=desc_html,
        solved=pid in _solved_set(),
    )


@app.route("/problems/<pid>/run", methods=["POST"])
def problem_run(pid: str):
    p = get_problem(pid)
    if not p:
        abort(404)
    from code_runner import compile_and_run

    code = (request.get_json(silent=True) or {}).get("code", "")
    # 코드 저장 (이어서 풀 수 있게)
    _solutions_dir().mkdir(parents=True, exist_ok=True)
    _solution_path(pid).write_text(code, encoding="utf-8")

    result = compile_and_run(
        code, p["harness"], p.get("prelude", ""), p.get("lang", "c"), p.get("grade_mode", "")
    )
    result["expected_output"] = p.get("expected_output", "")
    if result.get("ok") and result.get("compiled") and not result.get("timed_out"):
        total = result.get("total", 0)
        # 자동 채점 가능한 경우(result/passfail/assert)에만 통과 표시
        if result.get("mode") in ("result", "passfail", "assert"):
            _mark_solved(pid, total > 0 and result.get("passed", 0) == total)
    return jsonify(result)


@app.route("/problems/<pid>/reset", methods=["POST"])
def problem_reset(pid: str):
    p = get_problem(pid)
    if not p:
        abort(404)
    sp = _solution_path(pid)
    if sp.exists():
        sp.unlink()
    _mark_solved(pid, False)
    return redirect(url_for("problem", pid=pid))


# ── 자막 IP 차단 상태 확인 (대시보드용 API) ──────────────────
@app.route("/api/ip-status")
def ip_status():
    """YouTube 자막 API 차단 여부 + 자막 누락 개수를 JSON으로."""
    from check_ip_block import is_blocked, missing_transcripts

    try:
        blocked = is_blocked()
        err = ""
    except Exception as ex:  # noqa: BLE001
        return jsonify({"ok": False, "error": str(ex)})
    return jsonify({"ok": True, "blocked": blocked, "missing": missing_transcripts()})


def main() -> int:
    LECTURES_DIR.mkdir(parents=True, exist_ok=True)
    SECTIONS_DIR.mkdir(parents=True, exist_ok=True)
    print(f"[server] Study Dashboard → http://{HOST}:{PORT}")
    print("[server] 종료: Ctrl+C")
    app.run(host=HOST, port=PORT, debug=False, use_reloader=False)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
