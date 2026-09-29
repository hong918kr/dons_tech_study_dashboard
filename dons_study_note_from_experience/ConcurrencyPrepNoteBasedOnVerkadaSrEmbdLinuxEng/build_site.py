#!/usr/bin/env python3
"""연습 문제 10개 → 브라우저에서 읽는 HTML (index + 문제별 페이지 + 코드 뷰).

  python3 build_site.py

스타일과 마크다운 렌더러는 Verkada 준비 폴더의 build_notes_site.py 를 그대로 재사용한다.
출력물은 생성물이므로 직접 고치지 말 것 — 원본은 question_note 와 .c/.h 파일이다.
"""
import html
import importlib.util
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
VK = (ROOT / ".." / ".." / "job_interview_prep_research" / "verkada_sr_embedded_linux_engineer_connectivity").resolve()

spec = importlib.util.spec_from_file_location("vkbuild", VK / "build_notes_site.py")
vk = importlib.util.module_from_spec(spec)
spec.loader.exec_module(vk)          # CSS/GUIDE_CSS/CODE_CSS, shell(), render(), JS_*

esc = vk.esc

# 문제별 한 줄 설명 (index 카드용) — 폴더가 없으면 건너뛴다
TOPICS = {
    "01_gps_fix_cache":           ("🛰️", "구조체 스냅샷 (mutex vs seqlock)", "링버퍼 + 이진 탐색 + prefix sum"),
    "02_modem_rssi_window":       ("📶", "값+시각 묶어 publish, staleness", "monotonic deque 슬라이딩 min/max"),
    "03_event_tailer_shutdown":   ("🚪", "타임아웃 없는 블로킹에서 종료, drop 정책", "시간 버킷 링 카운트"),
    "04_temp_single_flight":      ("🌡️", "요청 합치기 (condvar + in-flight)", "버킷 min/max/avg 범위 질의"),
    "05_frame_latest_and_replay": ("🎞️", "더블/트리플 버퍼 + refcount", "메모리 예산 링 + 이진 탐색"),
    "06_config_publish_rollback": ("⚙️", "포인터 교체 + refcount (UAF 함정)", "버전 링 + 롤백"),
    "07_badge_audit_dedupe":      ("🪪", "MPSC 4생산자, 순서 보존, 종료", "오픈 어드레싱 해시 + 시간 만료"),
    "08_battery_energy_pipeline": ("🔋", "2단 파이프라인 + timed wait + 토큰버킷", "사다리꼴 적분 prefix sum"),
    "09_wifi_scan_snapshot":      ("📡", "가변 길이 결과 스냅샷", "해시맵 + size-k min-heap"),
    "10_heartbeat_watchdog":      ("🐕", "다수 writer / 단일 reader, wait-free", "lazy update min-heap"),
}

CODE_EXT = {".c", ".h", ".sh"}


def problem_dirs():
    return sorted(d for d in ROOT.iterdir()
                  if d.is_dir() and re.match(r"^\d\d_", d.name) and (d / "question_note").exists())


def code_page(src, pdir, out_dir):
    lines = src.read_text(encoding="utf-8", errors="replace").splitlines()
    rows = "".join(
        f'<div class="cl" id="L{i}"><span class="n"><a href="#L{i}" '
        f'style="color:inherit;text-decoration:none">{i}</a></span>'
        f'<span class="t">{esc(l) or "&nbsp;"}</span></div>'
        for i, l in enumerate(lines, 1))
    kind = ("★ 여기에 내가 구현한다" if src.name.endswith(".c") and "solution" not in src.name
            and src.stem not in ("main",) else
            "모범답안 — 다 풀고 나서" if "solution" in src.name else
            "주어지는 파일")
    body = f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="../index.html">← 문제 목록</a>
  <span class="t">{esc(pdir.name)} · {esc(src.name)}</span>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap" style="grid-template-columns:minmax(0,1fr)">
  <main style="max-width:1000px">
    <div class="hero">
      <div class="kicker">{esc(pdir.name)}</div>
      <h1>{esc(src.name)}</h1>
      <div class="meta"><span>{len(lines)}줄</span><span>{esc(kind)}</span>
        <span>원본 {esc(str(src.relative_to(ROOT)))}</span></div>
    </div>
    <div class="cmdbar">$ cd {esc(pdir.name)} &amp;&amp; ./main.sh        # 내 코드로 실행
$ cd {esc(pdir.name)} &amp;&amp; ./main.sh sol    # 모범답안으로 실행</div>
    <div class="codeview">{rows}</div>
  </main>
</div>"""
    (out_dir / (src.name + ".html")).write_text(
        vk.shell(f"{pdir.name} · {src.name}", body, vk.JS_PAGE), encoding="utf-8")


def problem_page(pdir, out_dir, prev_d, next_d):
    note = (pdir / "question_note").read_text(encoding="utf-8", errors="replace")
    icon, p1, p2 = TOPICS.get(pdir.name, ("📄", "", ""))
    files = sorted(p for p in pdir.iterdir() if p.suffix in CODE_EXT or p.name == "main.sh")
    flist = "".join(
        f'<a href="{esc(pdir.name)}/{esc(p.name)}.html">{esc(p.name)}'
        f'<span class="sz">{max(1, p.stat().st_size // 1024)} KB</span></a>' for p in files)
    nav = []
    if prev_d:
        nav.append(f'<a class="prev" href="#{esc(prev_d.name)}"><span class="dir">← 이전</span>'
                   f'<span class="nm">{esc(prev_d.name)}</span></a>')
    if next_d:
        nav.append(f'<a class="next" href="#{esc(next_d.name)}"><span class="dir">다음 →</span>'
                   f'<span class="nm">{esc(next_d.name)}</span></a>')
    return f"""
<section class="chapter" id="{esc(pdir.name)}">
<h2 id="{esc(pdir.name)}-h">{icon} {esc(pdir.name)}</h2>
<div class="tw"><table><thead><tr><th>Part 1 (동시성)</th><th>Part 2 (자료구조)</th></tr></thead>
<tbody><tr><td>{esc(p1)}</td><td>{esc(p2)}</td></tr></tbody></table></div>
<div class="cmdbar">$ cd {esc(pdir.name)} &amp;&amp; ./main.sh          # 내 코드 (stub → 구현)
$ cd {esc(pdir.name)} &amp;&amp; ./main.sh sol      # 모범답안</div>
<div class="filelist">{flist}</div>
<figure class="code"><span class="lang">question_note</span><pre><code>{esc(note)}</code></pre></figure>
<div class="nav">{"".join(nav)}</div>
</section>"""



# ----------------------------------------------------------------- 스터디 노트
TRACKS = [
    ("A", "준비운동", "C·메모리·도구·Linux 기초 — 문제를 읽기 위한 최소 장비"),
    ("B", "동시성", "스레드부터 lock-free까지. Part 1을 푸는 힘"),
    ("C", "자료구조", "링버퍼·이진탐색·누적합·덱·버킷·해시·힙. Part 2를 푸는 힘"),
    ("D", "문제 유형 마스터", "레시피 · 문제 지도 · 면접에서 말하는 법"),
]
NOTES_DIR = ROOT / "basic_study_notes"
NOTES_OUT = ROOT / "basic_study_notes"           # md 옆에 html 을 둔다


def note_files():
    if not NOTES_DIR.exists():
        return []
    return sorted(NOTES_DIR.glob("[A-D][0-9]*.md"))


def note_title(md):
    for line in md.read_text(encoding="utf-8").splitlines():
        m = vk.HEAD.match(line)
        if m and len(m.group(1)) == 1:
            return vk.strip_md(m.group(2))
    return md.stem


def build_notes():
    files = note_files()
    if not files:
        print("  notes    (없음)")
        return []
    metas = []
    for i, md in enumerate(files):
        text = md.read_text(encoding="utf-8")
        lines = text.splitlines()
        toc, used = [], set()
        body = vk.render(lines, toc, used)
        title = note_title(md)
        # .md 링크를 .html 로
        body = re.sub(r'href="([A-D][0-9][^"]*)\.md"', r'href="\1.html"', body)
        prev_f = files[i - 1] if i else None
        next_f = files[i + 1] if i + 1 < len(files) else None
        nav = []
        if prev_f:
            nav.append(f'<a class="prev" href="{prev_f.stem}.html"><span class="dir">← 이전</span>'
                       f'<span class="nm">{esc(note_title(prev_f))}</span></a>')
        if next_f:
            nav.append(f'<a class="next" href="{next_f.stem}.html"><span class="dir">다음 →</span>'
                       f'<span class="nm">{esc(note_title(next_f))}</span></a>')
        toc_html = "".join(f'<li class="l{lvl}"><a href="#{hid}">{esc(tx)}</a></li>'
                           for lvl, tx, hid in toc if lvl in (2, 3))
        minutes = vk.read_time(text)
        h1 = next((vk.HEAD.match(l).group(2) for l in lines
                   if vk.HEAD.match(l) and len(vk.HEAD.match(l).group(1)) == 1), md.stem)
        page = f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="index.html">← 노트 목록</a>
  <span class="t">{esc(title)}</span>
  <button class="btn" id="readbtn">읽음 표시</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">스터디 노트 · {esc(md.stem.split("_")[0])}</div>
      <h1>{vk.inline(h1)}</h1>
      <div class="meta"><span>{minutes}분</span><span>{len(lines)}줄</span>
        <span>{len(toc)}개 절</span><span>원본 basic_study_notes/{esc(md.name)}</span></div>
    </div>
    {body}
    <div class="nav">{"".join(nav)}</div>
  </main>
  <aside><div class="lbl">목차</div><ol>{toc_html}</ol></aside>
</div>"""
        (NOTES_OUT / (md.stem + ".html")).write_text(
            vk.shell(title, page, vk.JS_PAGE).replace(
                "<body>", f'<body data-note="note/{esc(md.stem)}">', 1), encoding="utf-8")
        metas.append({"stem": md.stem, "title": title, "minutes": minutes,
                      "track": md.stem[0], "lines": len(lines)})
    # 허브
    secs = []
    for key, name, blurb in TRACKS:
        items = [m for m in metas if m["track"] == key]
        if not items:
            continue
        cards = "".join(
            f'<a class="card" data-id="note/{esc(m["stem"])}" data-search="{esc(m["title"].lower())}" '
            f'href="{m["stem"]}.html"><div class="ic">📘</div><h3>{esc(m["title"])}</h3>'
            f'<div class="foot"><span class="pill">{esc(m["stem"].split("_")[0])}</span>'
            f'<span class="pill">{m["minutes"]}분</span></div></a>' for m in items)
        secs.append(f'<section class="track"><h2>{key}. {esc(name)}</h2>'
                    f'<div class="sub">{esc(blurb)}</div><div class="grid">{cards}</div></section>')
    total_min = sum(m["minutes"] for m in metas)
    hub = f"""
<div class="top"><div class="top-in">
  <a class="home" href="../index.html">← 연습 문제</a>
  <span class="t">스터디 노트 — 기초부터</span>
  <button class="btn" id="resetread">읽음 초기화</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="hub">
  <h1>스터디 노트</h1>
  <p class="lead">이 폴더의 연습 문제 10개를 스스로 풀 수 있게 만드는 것이 목표다.
     동시성·자료구조·Linux를 처음부터 쌓는다. <b>A → B → C → D 순서</b>로 읽고,
     각 노트 끝의 자가 점검을 먼저 답해 본 뒤 펼친다.</p>
  <div class="stats">
    <div class="stat"><b>{len(metas)}</b><span>노트</span></div>
    <div class="stat"><b id="readcount">0</b><span>읽음</span></div>
    <div class="stat"><b>{total_min}분</b><span>총 분량</span></div>
    <div class="stat"><b>10</b><span>연습 문제</span></div>
  </div>
  <div class="tools"><input id="q" placeholder="노트 검색 — 예: mutex, 링버퍼, 해시"></div>
  {"".join(secs)}
</div>"""
    (NOTES_OUT / "index.html").write_text(vk.shell("스터디 노트", hub, vk.JS_HUB), encoding="utf-8")
    print(f"  notes    {len(metas)}편 → basic_study_notes/  ({total_min}분)")
    return metas


def main():
    dirs = problem_dirs()
    if not dirs:
        print("문제 폴더가 아직 없습니다."); return 1
    secs, toc = [], []
    for i, d in enumerate(dirs):
        out_dir = ROOT / d.name
        for f in d.iterdir():
            if f.suffix in CODE_EXT:
                code_page(f, d, out_dir)
        secs.append(problem_page(d, out_dir,
                                 dirs[i - 1] if i else None,
                                 dirs[i + 1] if i + 1 < len(dirs) else None))
        icon = TOPICS.get(d.name, ("📄",))[0]
        toc.append(f'<li class="ch" data-ch="{esc(d.name)}">'
                   f'<a href="#{esc(d.name)}">{icon} {esc(d.name)}</a></li>')

    intro = """
<blockquote><p><b>이 폴더는 2026-09-25 Verkada 1차 스크리닝 유형을 반복 연습하려고 만든 것이다.</b>
모든 문제가 같은 골격이다 — 다루기 불편한 벤더 API를 <b>Part 1에서 동시성으로</b>,
<b>Part 2에서 자료구조로</b> 감싼다.</p></blockquote>
<h3>드릴 방법 (문제당 60~75분)</h3>
<ol>
<li>question_note 만 읽고 맨 아래 "10가지"에 <b>종이로</b> 답한다 (5분, 코드 금지)</li>
<li>Part 1 구현 → <code>./main.sh</code> 로 Part 1 테스트 통과 (25분)</li>
<li>Part 2 구현 → 전부 PASS (25분)</li>
<li><code>*_solution.c</code> 와 비교하고 다른 점 3줄 적기 (10분)</li>
<li>내 코드를 <b>소리 내어</b> 설명 — 왜 이 동기화인지, 메모리가 왜 그만큼인지, 경계 조건은 무엇인지 (10분)</li>
</ol>
<p>막히면 순서대로: question_note 의 10가지 →
<a href="../../job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity/verkada_sep_25_2026_1st_screening_questions/2026-09-25_als_easy_explainer.html">ALS 쉬운 설명</a> → 모범답안.</p>
"""
    notes = build_notes()
    notes_card = (f'<blockquote><p><b>📘 <a href="basic_study_notes/index.html">스터디 노트 {len(notes)}편</a></b> — '
                  f'동시성·자료구조·Linux를 기초부터. 문제가 막히면 먼저 여기로.</p></blockquote>'
                  if notes else "")
    body = f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="../../job_interview_prep_research/verkada_sr_embedded_linux_engineer_connectivity/index.html">← 준비 목차</a>
  <span class="t">벤더 API 감싸기 연습 — 10문제</span>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">Practice · Part 1 동시성 / Part 2 자료구조</div>
      <h1>벤더 API 감싸기 연습 10문제</h1>
      <div class="meta"><span>{len(dirs)}문제</span><span>문제당 60~75분</span>
        <span>원본 유형: 2026-09-25 Verkada 1차</span></div>
    </div>
    {notes_card}
    {intro}
    {"".join(secs)}
  </main>
  <aside><div class="lbl">문제</div><ol>{"".join(toc)}</ol></aside>
</div>"""
    (ROOT / "index.html").write_text(
        vk.shell("벤더 API 감싸기 연습 10문제", body, vk.JS_PAGE).replace(
            "<body>", '<body data-note="practice/index">', 1), encoding="utf-8")
    print(f"built {ROOT/'index.html'}  ({len(dirs)}문제)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
