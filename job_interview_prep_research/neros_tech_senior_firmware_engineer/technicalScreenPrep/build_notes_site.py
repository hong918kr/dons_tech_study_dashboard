#!/usr/bin/env python3
"""Neros technical screen (10-08) 준비 노트 마크다운 → 읽기 좋은 HTML 사이트.

  python3 build_notes_site.py            # site/ 전체 + 루트 index.html 재생성

입력
  plan/*.md · notes/*.md · python/problems/*.md · START_HERE.md
  python/{starters,solutions,lib,graders}/*.py · c/*.c   (읽기용 코드 페이지)

출력
  site/index.html        허브 (카드 · 검색 · 진행률)
  site/<coll>/X.html     노트 · 문제 페이지 (사이드 TOC · 진행바 · 이전/다음)
  site/code/*.html       코드 페이지 · site/coding.html 문제 목록
  site/start.html        START HERE · 루트 index.html (같은 내용, 경로만 루트 기준)

링크: md 안의 상대 경로는 'md 파일 위치 기준'으로 해석해서, 생성되는 HTML 위치 기준으로 다시 계산한다.
이 폴더의 .md/.py/.c 는 사이트 페이지로, 바깥 자료는 그대로(또는 같은 이름의 .html 로) 연결.
의존성 없음(표준 라이브러리만). 마크다운 지원 범위는 NOTES_SPEC.md 참고.
"""
import html
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "site"
COLLECTIONS = [
    {"key": "plan", "dir": ROOT / "plan",
     "title_ko": "게임 플랜", "title_en": "Game Plan",
     "blurb": "10-08(목) 11am technical screen까지 D-6 일정과 1시간 배분. 여기서 시작한다.",
     "src_hint": "plan"},
    {"key": "notes", "dir": ROOT / "notes",
     "title_ko": "준비 노트", "title_en": "Screen Notes",
     "blurb": "면접관·형식 → pytest 실전 → ring buffer Python/C → test·tooling system design → 말하면서 코딩.",
     "src_hint": "notes"},
    {"key": "drills", "dir": ROOT / "drills",
     "title_ko": "매일 타이핑 드릴", "title_en": "Daily Drills",
     "blurb": "Python 10 + C 10 × 5일. 정답을 보지 않고 친다 — 브라우저 트레이너(drills.html) 또는 drill.py new/check.",
     "src_hint": "drills"},
    {"key": "design", "dir": ROOT / "design",
     "title_ko": "직군별 펌웨어 테스트 설계", "title_en": "Test System Design by FW Team",
     "blurb": "Flight · Radio · Ground/Peripherals · Video/FPGA · Platform · Linux/Autonomy · Factory · Integration — 테스트베드와 면접 시나리오.",
     "src_hint": "design"},
    {"key": "playground", "dir": ROOT / "playground",
     "title_ko": "Playground", "title_en": "Scratch Space",
     "blurb": "템플릿 없는 빈 연습장. py/ · c/ 에 처음부터 쓰고 make py / make test / make c 로 실행.",
     "src_hint": "playground"},
    {"key": "problems", "dir": ROOT / "python" / "problems",
     "title_ko": "Python 문제", "title_en": "Python Problems",
     "blurb": "ring buffer 구현 · pytest로 테스트 쓰기(mutant 채점) · mock · conftest · thread-safe · stream framer · ctypes.",
     "src_hint": "python/problems"},
]
META_DIR = ROOT / "meta"          # 선택: <stem>.json 에 icon/range 지정

# ----------------------------------------------------------------- markdown
FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
HR = re.compile(r"^\s{0,3}(-{3,}|\*{3,}|_{3,})\s*$")
TABLE_SEP = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]*$")
ULI = re.compile(r"^(\s*)[-*+]\s+(.*)$")
OLI = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
TASK = re.compile(r"^\[([ xX])\]\s+(.*)$")


def esc(s):
    return html.escape(s, quote=False)


def slug(text, used):
    base = re.sub(r"[^\w가-힣]+", "-", strip_md(text)).strip("-").lower() or "sec"
    s, n = base, 2
    while s in used:
        s, n = f"{base}-{n}", n + 1
    used.add(s)
    return s


def strip_md(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)
    s = s.replace("~~", "")                      # 취소선만 제거, "Q11~20" 의 ~ 는 보존
    return re.sub(r"[*`]", "", s).strip()


EMOJI_HEAD = re.compile(
    r"^(?:[\U0001F000-\U0001FAFF\u2190-\u21FF\u2300-\u27BF\uFE0F\u2B00-\u2BFF]+\s*)+")


def title_text(h1):
    """카드/네비에 쓸 제목 — 앞머리 이모지는 아이콘으로 따로 보여주므로 제거."""
    return EMOJI_HEAD.sub("", strip_md(h1)).strip()


def inline(s):
    keep = []

    def ph(h):
        keep.append(h)
        return f"\x00{len(keep) - 1}\x00"

    s = re.sub(r"`([^`]+)`", lambda m: ph(f"<code>{esc(m.group(1))}</code>"), s)
    s = esc(s)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: ph(f'<a href="{html.escape(md2html(m.group(2)), quote=True)}">{m.group(1)}</a>'), s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", s)
    s = re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], s)
    return s


CTX = {"src": ROOT, "out": OUT}     # 지금 렌더링 중인 md 의 폴더 · 생성될 HTML 의 폴더


def map_target(t):
    """소스 경로 → 실제로 열 페이지 경로 (이 폴더 안이면 site/ 페이지, 바깥 .md 는 옆의 .html)."""
    try:
        rel = t.relative_to(ROOT)
    except ValueError:
        if t.suffix == ".md" and t.with_suffix(".html").exists():
            return t.with_suffix(".html")
        return t
    parts = rel.parts
    if rel.as_posix() == "START_HERE.md":
        return OUT / "start.html"
    if len(parts) == 2 and parts[0] in ("plan", "notes", "drills", "design", "playground") and t.suffix == ".md":
        return OUT / parts[0] / (t.stem + ".html")
    if parts[:2] == ("python", "problems") and t.suffix == ".md":
        return OUT / "problems" / (t.stem + ".html")
    if len(parts) >= 3 and parts[:1] == ("python",) and parts[1] in ("starters", "solutions") and t.suffix == ".py":
        kind = parts[1][:-1]
        name = "__".join(parts[2:])[:-3]
        return OUT / "code" / f"{name}_{kind}.html"
    if parts[:2] == ("python", "lib") and t.suffix == ".py" and len(parts) == 3:
        return OUT / "code" / f"lib_{t.stem}.html"
    if parts[:2] == ("python", "graders") and t.suffix == ".py":
        return OUT / "code" / f"grader_{t.stem}.html"
    if parts[:1] == ("c",) and t.suffix in (".c", ".h"):
        return OUT / "code" / f"c_{t.name}.html"
    return t


def md2html(href):
    if href.startswith(("http://", "https://", "#", "mailto:")):
        return href
    path, _, frag = href.partition("#")
    frag = "#" + frag if frag else ""
    target = Path(os.path.normpath(CTX["src"] / path))
    return os.path.relpath(map_target(target), CTX["out"]) + frag


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def render(md_lines, toc, used_ids):
    """마크다운 본문 → HTML. toc 에 (level, text, id) 를 채운다."""
    out, i, n = [], 0, len(md_lines)
    while i < n:
        line = md_lines[i]

        m = FENCE.match(line)
        if m:                                           # ---- 코드 블록
            lang, buf, i = m.group(1), [], i + 1
            while i < n and not FENCE.match(md_lines[i]):
                buf.append(md_lines[i])
                i += 1
            i += 1
            label = f'<span class="lang">{esc(lang)}</span>' if lang else ""
            out.append(f'<figure class="code">{label}<pre><code>'
                       f'{esc(chr(10).join(buf))}</code></pre></figure>')
            continue

        m = HEAD.match(line)
        if m:                                           # ---- 제목
            lvl, text = len(m.group(1)), m.group(2)
            if lvl == 1:                                # h1 은 hero 에서 처리
                i += 1
                continue
            hid = slug(text, used_ids)
            toc.append((lvl, strip_md(text), hid))
            out.append(f'<h{lvl} id="{hid}">{inline(text)}'
                       f'<a class="anchor" href="#{hid}" aria-label="link">#</a></h{lvl}>')
            i += 1
            continue

        if HR.match(line):
            out.append("<hr>")
            i += 1
            continue

        if line.startswith(">"):                        # ---- 인용(사이드노트)
            buf = []
            while i < n and md_lines[i].startswith(">"):
                buf.append(md_lines[i].lstrip(">").lstrip())
                i += 1
            inner_toc = []
            out.append(f'<blockquote>{render(buf, inner_toc, used_ids)}</blockquote>')
            continue

        if "|" in line and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]):   # ---- 표
            head = split_row(line)
            i += 2
            rows = []
            while i < n and "|" in md_lines[i] and md_lines[i].strip():
                rows.append(split_row(md_lines[i]))
                i += 1
            th = "".join(f"<th>{inline(c)}</th>" for c in head)
            body = ""
            for r in rows:
                r = (r + [""] * len(head))[:len(head)]
                body += "<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>"
            out.append(f'<div class="tw"><table><thead><tr>{th}</tr></thead>'
                       f"<tbody>{body}</tbody></table></div>")
            continue

        if ULI.match(line) or OLI.match(line):          # ---- 목록 (중첩 없음)
            ordered = bool(OLI.match(line))
            items = []
            while i < n:
                cur = md_lines[i]
                mo, mu = OLI.match(cur), ULI.match(cur)
                if ordered and mo:
                    items.append(mo.group(3))
                elif (not ordered) and mu:
                    items.append(mu.group(2))
                elif (items and cur.strip()                      # lazy continuation:
                      and not mo and not mu                      # 들여쓰기 없이 다음 줄로
                      and not HEAD.match(cur) and not FENCE.match(cur)
                      and not HR.match(cur) and not cur.startswith(">")
                      and not ("|" in cur and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]))):
                    items[-1] += " " + cur.strip()               # 이어붙여 **굵게**가 끊기지 않게
                else:
                    break
                i += 1
            lis = []
            for it in items:
                t = TASK.match(it)
                if t:
                    done = t.group(1).lower() == "x"
                    lis.append(f'<li class="task"><label><input type="checkbox" '
                               f'{"checked" if done else ""}> {inline(t.group(2))}</label></li>')
                else:
                    lis.append(f"<li>{inline(it)}</li>")
            tag = "ol" if ordered else "ul"
            cls = ' class="tasks"' if any("task" in x for x in lis) else ""
            out.append(f"<{tag}{cls}>{''.join(lis)}</{tag}>")
            continue

        if not line.strip():                            # ---- 빈 줄
            i += 1
            continue

        buf = []                                        # ---- 문단
        while i < n and md_lines[i].strip() and not (
                HEAD.match(md_lines[i]) or FENCE.match(md_lines[i]) or HR.match(md_lines[i])
                or md_lines[i].startswith(">") or ULI.match(md_lines[i]) or OLI.match(md_lines[i])
                or ("|" in md_lines[i] and i + 1 < n and TABLE_SEP.match(md_lines[i + 1]))):
            buf.append(md_lines[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(buf))}</p>")
    return "\n".join(out)


# ----------------------------------------------------------------- 페이지
CSS = """
:root{
  --bg:#faf9f7; --paper:#fff; --ink:#1c1b19; --muted:#6b6862; --line:#e3e0da;
  --accent:#8a5a2b; --accent-soft:#f3ece2; --code-bg:#f6f4f0; --code-ink:#2a2724;
  --mark:#fff3cd; --ok:#2f7d55; --warn:#b4541f;
  --serif: Georgia, "Times New Roman", "Apple SD Gothic Neo", "Noto Serif KR", serif;
  --sans: -apple-system, BlinkMacSystemFont, "Apple SD Gothic Neo", "Segoe UI", sans-serif;
  --mono: ui-monospace, SFMono-Regular, "SF Mono", Menlo, Consolas, monospace;
}
:root:not([data-theme=light]) { color-scheme: light; }
@media (prefers-color-scheme: dark){
  :root:not([data-theme=light]){
    --bg:#16151a; --paper:#1d1c22; --ink:#e8e6e1; --muted:#a09c94; --line:#33313a;
    --accent:#d9a441; --accent-soft:#2a2620; --code-bg:#14141a; --code-ink:#e3e0d8;
    --mark:#4a3f1e; --ok:#63c08c; --warn:#e2895a; color-scheme: dark;
  }
}
:root[data-theme=dark]{
  --bg:#16151a; --paper:#1d1c22; --ink:#e8e6e1; --muted:#a09c94; --line:#33313a;
  --accent:#d9a441; --accent-soft:#2a2620; --code-bg:#14141a; --code-ink:#e3e0d8;
  --mark:#4a3f1e; --ok:#63c08c; --warn:#e2895a; color-scheme: dark;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);
     -webkit-font-smoothing:antialiased}
a{color:var(--accent)}
/* ---- top bar ---- */
.top{position:sticky;top:0;z-index:20;background:color-mix(in srgb,var(--bg) 88%,transparent);
     backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.top-in{max-width:1180px;margin:0 auto;padding:10px 20px;display:flex;align-items:center;gap:14px}
.top a.home{text-decoration:none;color:var(--muted);font-size:13px;white-space:nowrap}
.top a.home:hover{color:var(--accent)}
.top .t{font-size:13px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.btn{font:inherit;font-size:12px;padding:5px 10px;border:1px solid var(--line);border-radius:999px;
     background:var(--paper);color:var(--muted);cursor:pointer}
.btn:hover{color:var(--accent);border-color:var(--accent)}
.btn.on{background:var(--accent);border-color:var(--accent);color:#fff}
#bar{position:fixed;top:0;left:0;height:2px;background:var(--accent);width:0;z-index:30}
/* ---- layout ---- */
.wrap{max-width:1180px;margin:0 auto;padding:0 20px;display:grid;
      grid-template-columns:minmax(0,1fr) 250px;gap:44px;align-items:start}
main{max-width:760px;padding:40px 0 100px}
aside{position:sticky;top:64px;padding:40px 0;max-height:calc(100vh - 80px);overflow:auto}
aside .lbl{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}
aside ol{list-style:none;margin:0;padding:0;font-size:13px;line-height:1.5}
aside li{margin:2px 0}
aside li.l3{padding-left:14px;font-size:12.5px}
aside a{display:block;padding:3px 8px;border-radius:6px;text-decoration:none;color:var(--muted);
        border-left:2px solid transparent}
aside a:hover{color:var(--ink);background:var(--accent-soft)}
aside a.active{color:var(--accent);border-left-color:var(--accent);background:var(--accent-soft)}
/* ---- article ---- */
.hero{border-bottom:2px solid var(--ink);padding-bottom:22px;margin-bottom:34px}
.kicker{font-size:11px;letter-spacing:.16em;text-transform:uppercase;color:var(--accent);margin-bottom:10px}
h1{font-family:var(--serif);font-size:34px;line-height:1.25;margin:0 0 12px;letter-spacing:-.01em}
.meta{font-size:12.5px;color:var(--muted);display:flex;gap:14px;flex-wrap:wrap}
main h2{font-family:var(--serif);font-size:24px;margin:44px 0 14px;padding-top:10px;
        border-top:1px solid var(--line);line-height:1.3}
main h2:first-of-type{border-top:0}
main h3{font-family:var(--serif);font-size:18.5px;margin:30px 0 10px;line-height:1.35}
main p{font-family:var(--serif);font-size:17px;line-height:1.75;margin:0 0 16px}
main li{font-family:var(--serif);font-size:16.5px;line-height:1.7;margin:5px 0}
main ul,main ol{padding-left:22px;margin:0 0 16px}
ul.tasks{list-style:none;padding-left:2px}
li.task label{display:flex;gap:9px;align-items:flex-start;cursor:pointer}
li.task input{margin-top:6px;accent-color:var(--accent)}
li.task input:checked + *{opacity:.55}
code{font-family:var(--mono);font-size:.875em;background:var(--code-bg);padding:1.5px 5px;
     border-radius:4px;border:1px solid var(--line)}
figure.code{position:relative;margin:0 0 20px;background:var(--code-bg);border:1px solid var(--line);
            border-radius:10px;overflow:hidden}
figure.code .lang{position:absolute;top:0;right:0;font-family:var(--mono);font-size:10px;
     letter-spacing:.1em;text-transform:uppercase;color:var(--muted);padding:5px 10px}
figure.code pre{margin:0;padding:16px 18px;overflow-x:auto}
figure.code code{background:none;border:0;padding:0;color:var(--code-ink);font-size:13px;line-height:1.6}
blockquote{margin:0 0 20px;padding:14px 18px;background:var(--accent-soft);
           border-left:3px solid var(--accent);border-radius:0 8px 8px 0}
blockquote p{margin:0 0 8px;font-size:15.5px;line-height:1.65}
blockquote p:last-child{margin:0}
.tw{overflow-x:auto;margin:0 0 22px}
table{border-collapse:collapse;width:100%;font-family:var(--sans);font-size:14px}
th,td{padding:9px 12px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
thead th{border-bottom:1.5px solid var(--ink);font-size:12px;letter-spacing:.04em;
         text-transform:uppercase;color:var(--muted)}
tbody tr:hover{background:var(--accent-soft)}
hr{border:0;border-top:1px solid var(--line);margin:34px 0}
.anchor{opacity:0;margin-left:8px;font-size:.6em;text-decoration:none;color:var(--muted)}
h2:hover .anchor,h3:hover .anchor{opacity:1}
.nav{display:flex;gap:12px;margin-top:60px;padding-top:24px;border-top:1px solid var(--line)}
.nav a{flex:1;text-decoration:none;border:1px solid var(--line);border-radius:10px;padding:14px 16px;
       background:var(--paper)}
.nav a:hover{border-color:var(--accent)}
.nav .dir{font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.nav .nm{font-family:var(--serif);font-size:15px;color:var(--ink);margin-top:4px;display:block}
.nav a.next{text-align:right}
/* ---- hub ---- */
.hub{max-width:1180px;margin:0 auto;padding:44px 20px 90px}
.hub h1{font-size:38px;margin-bottom:8px}
.lead{font-family:var(--serif);font-size:17.5px;line-height:1.7;color:var(--muted);max-width:680px}
.stats{display:flex;gap:26px;flex-wrap:wrap;margin:26px 0 8px;padding:16px 0;
       border-top:1px solid var(--line);border-bottom:1px solid var(--line)}
.stat b{display:block;font-family:var(--serif);font-size:26px;line-height:1.1}
.stat span{font-size:11.5px;color:var(--muted);letter-spacing:.06em;text-transform:uppercase}
.tools{display:flex;gap:10px;align-items:center;margin:22px 0 6px;flex-wrap:wrap}
#q{flex:1;min-width:220px;font:inherit;font-size:14px;padding:9px 13px;border:1px solid var(--line);
   border-radius:999px;background:var(--paper);color:var(--ink)}
#q:focus{outline:none;border-color:var(--accent)}
.track{margin-top:38px}
.track h2{font-family:var(--serif);font-size:23px;margin:0 0 4px}
.track .sub{font-size:14px;color:var(--muted);margin-bottom:18px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(285px,1fr));gap:16px}
.card{display:block;text-decoration:none;color:inherit;background:var(--paper);border:1px solid var(--line);
      border-radius:14px;padding:18px 18px 16px;transition:border-color .15s,transform .15s}
.card:hover{border-color:var(--accent);transform:translateY(-2px)}
.card .ic{font-size:22px;line-height:1}
.card h3{font-family:var(--serif);font-size:17px;margin:10px 0 6px;line-height:1.35}
.card p{font-size:13.5px;line-height:1.6;color:var(--muted);margin:0 0 12px;
        display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
.card .foot{display:flex;gap:10px;align-items:center;font-size:11.5px;color:var(--muted)}
.pill{border:1px solid var(--line);border-radius:999px;padding:2px 9px}
.pill.pri{border-color:var(--accent);color:var(--accent)}
.card.read{border-color:var(--ok)}
.card.read .ic::after{content:" ✓";color:var(--ok);font-size:14px}
.hide{display:none}
@media (max-width:900px){
  .wrap{grid-template-columns:minmax(0,1fr)} aside{display:none}
  main{padding-top:28px} h1{font-size:28px} main p{font-size:16px}
}
@media print{
  .top,aside,.nav,#bar{display:none} .wrap{display:block} main{max-width:none;padding:0}
  body{background:#fff}
}
"""

JS_PAGE = """
const KEY='neros_tsp_notes_read_v1', TKEY='neros_tsp_notes_theme', CKEY='neros_tsp_notes_check_v1';
const readSet=()=>{try{return new Set(JSON.parse(localStorage.getItem(KEY)||'[]'))}catch(e){return new Set()}};
const saveSet=s=>{try{localStorage.setItem(KEY,JSON.stringify([...s]))}catch(e){}};
(function(){
  try{const t=localStorage.getItem(TKEY); if(t)document.documentElement.dataset.theme=t;}catch(e){}
  const tb=document.getElementById('theme');
  if(tb)tb.onclick=()=>{const d=document.documentElement;
    const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
    const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx;
    try{localStorage.setItem(TKEY,nx)}catch(e){}};
  // 진행 바
  const bar=document.getElementById('bar');
  const onScroll=()=>{const h=document.body.scrollHeight-innerHeight;
    bar.style.width=(h>0?Math.min(100,scrollY/h*100):0)+'%';};
  addEventListener('scroll',onScroll,{passive:true}); onScroll();
  // 읽음 표시
  const id=document.body.dataset.note, rb=document.getElementById('readbtn');
  const sync=()=>{const s=readSet(); const on=s.has(id);
    rb.classList.toggle('on',on); rb.textContent=on?'✓ 읽음':'읽음 표시';};
  if(rb){rb.onclick=()=>{const s=readSet(); s.has(id)?s.delete(id):s.add(id); saveSet(s); sync();}; sync();}
  // 체크박스 저장
  let cs={}; try{cs=JSON.parse(localStorage.getItem(CKEY)||'{}')}catch(e){}
  document.querySelectorAll('li.task input').forEach((el,i)=>{
    const k=id+':'+i; if(cs[k])el.checked=true;
    el.addEventListener('change',()=>{cs[k]=el.checked;
      try{localStorage.setItem(CKEY,JSON.stringify(cs))}catch(e){}});
  });
  // TOC 하이라이트
  const links=[...document.querySelectorAll('aside a')];
  const map=new Map(links.map(a=>[a.getAttribute('href').slice(1),a]));
  const heads=[...document.querySelectorAll('main h2[id],main h3[id]')];
  if(heads.length){
    const io=new IntersectionObserver(es=>{
      es.forEach(e=>{ if(e.isIntersecting){ links.forEach(a=>a.classList.remove('active'));
        const a=map.get(e.target.id); if(a)a.classList.add('active'); }});
    },{rootMargin:'-80px 0px -70% 0px',threshold:0});
    heads.forEach(h=>io.observe(h));
  }
  // 키보드: j/k 다음·이전 노트
  addEventListener('keydown',e=>{
    if(e.target.matches('input,textarea'))return;
    if(e.key==='j'){const n=document.querySelector('.nav a.next'); if(n)location.href=n.href;}
    if(e.key==='k'){const p=document.querySelector('.nav a.prev'); if(p)location.href=p.href;}
  });
})();
"""

JS_HUB = """
const KEY='neros_tsp_notes_read_v1', TKEY='neros_tsp_notes_theme';
(function(){
  try{const t=localStorage.getItem(TKEY); if(t)document.documentElement.dataset.theme=t;}catch(e){}
  const tb=document.getElementById('theme');
  if(tb)tb.onclick=()=>{const d=document.documentElement;
    const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
    const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx; try{localStorage.setItem(TKEY,nx)}catch(e){}};
  let read=new Set(); try{read=new Set(JSON.parse(localStorage.getItem(KEY)||'[]'))}catch(e){}
  const cards=[...document.querySelectorAll('.card')];
  cards.forEach(c=>{ if(read.has(c.dataset.id)) c.classList.add('read'); });
  const rc=document.getElementById('readcount');
  if(rc)rc.textContent=cards.filter(c=>read.has(c.dataset.id)).length;
  const q=document.getElementById('q');
  if(q)q.addEventListener('input',()=>{
    const v=q.value.trim().toLowerCase();
    cards.forEach(c=>c.classList.toggle('hide', v && !c.dataset.search.includes(v)));
    document.querySelectorAll('.track').forEach(t=>{
      const any=[...t.querySelectorAll('.card')].some(c=>!c.classList.contains('hide'));
      t.classList.toggle('hide',!any);
    });
  });
  const rb=document.getElementById('resetread');
  if(rb)rb.onclick=()=>{ if(confirm('읽음 표시를 모두 지울까?')){
    try{localStorage.removeItem(KEY)}catch(e){} location.reload(); }};
})();
"""


def shell(title, body, js, extra_head=""):
    return f"""<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title>{extra_head}
<style>{CSS}</style></head>
<body>{body}<script>{js}</script></body></html>"""


def read_time(text):
    return max(1, round(len(re.sub(r"\s+", "", text)) / 500))


def note_page(note, prev_note, next_note, coll):
    toc, used = [], set()
    body = render(note["lines"], toc, used)
    toc_html = "".join(
        f'<li class="l{lvl}"><a href="#{hid}">{esc(text)}</a></li>'
        for lvl, text, hid in toc if lvl in (2, 3))
    nav = []
    if prev_note:
        nav.append(f'<a class="prev" href="{prev_note["file"]}">'
                   f'<span class="dir">← 이전</span><span class="nm">{esc(prev_note["title"])}</span></a>')
    if next_note:
        nav.append(f'<a class="next" href="{next_note["file"]}">'
                   f'<span class="dir">다음 →</span><span class="nm">{esc(next_note["title"])}</span></a>')
    meta = [f'{note["minutes"]}분 읽기', f'{note["lines_n"]}줄', f'{len(toc)}개 절']
    if note.get("badge"):
        meta.insert(0, note["badge"])
    return shell(note["title"], f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="../index.html">← 노트 목록</a>
  <span class="t">{esc(coll["title_ko"])} · {esc(note["title"])}</span>
  <button class="btn" id="readbtn">읽음 표시</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">{esc(coll["title_ko"])} · {esc(coll["title_en"])}</div>
      <h1>{inline(note["h1"])}</h1>
      <div class="meta">{"".join(f"<span>{esc(m)}</span>" for m in meta)}</div>
    </div>
    {body}
    <div class="nav">{"".join(nav)}</div>
  </main>
  <aside><div class="lbl">목차</div><ol>{toc_html}</ol></aside>
</div>""", JS_PAGE, extra_head="")


def hub_page(colls, totals):
    tracks = []
    for c in colls:
        cards = []
        for nt in c["notes"]:
            pills = [f'<span class="pill">{nt["minutes"]}분</span>']
            if nt.get("badge"):
                pills.append(f'<span class="pill">{esc(nt["badge"])}</span>')
            if nt.get("priority"):
                pills.append(f'<span class="pill pri">{esc(nt["priority"])}</span>')
            search = esc((nt["title"] + " " + nt["summary"] + " " + nt["headings"]).lower())
            cards.append(
                f'<a class="card" data-id="{esc(nt["id"])}" data-search="{search}" '
                f'href="{c["key"]}/{nt["file"]}">'
                f'<div class="ic">{esc(nt.get("icon", "📄"))}</div>'
                f'<h3>{esc(nt["title"])}</h3><p>{esc(nt["summary"])}</p>'
                f'<div class="foot">{"".join(pills)}</div></a>')
        tracks.append(f"""<section class="track">
  <h2>{esc(c["title_ko"])} <span style="color:var(--muted);font-size:15px">· {esc(c["title_en"])}</span></h2>
  <div class="sub">{esc(c["blurb"])} — <code>{esc(c["src_hint"])}/</code></div>
  <div class="grid">{"".join(cards)}</div>
</section>""")
    return shell("Neros Technical Screen 준비", f"""
<div class="top"><div class="top-in">
  <span class="t"><b>Neros</b> · Technical Screen — 10-08(목) 11am · 1시간 · 코딩</span>
  <button class="btn" id="resetread">읽음 초기화</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="hub">
  <h1>준비 노트</h1>
  <p class="lead">온사이트 전에 추가된 <b>technical screen</b> — <b>2026-10-08(목) 11:00, 1시간, 코딩 위주</b>. 주제: <b>pytest · ring buffer · Python/C · system design</b>.
     면접관은 Full Stack 엔지니어로 추정. <b>START HERE</b>에서 남은 시간에 맞는 코스를 고르고, 문제는 starter로 직접 풀어 <code>run.py</code>로 채점한다.</p>
  <div class="stats">
    <div class="stat"><b>{totals["notes"]}</b><span>노트</span></div>
    <div class="stat"><b id="readcount">0</b><span>읽음</span></div>
    <div class="stat"><b>{totals["minutes"]}분</b><span>총 읽기 시간</span></div>
    <div class="stat"><b>{totals["sections"]}</b><span>절</span></div>
  </div>
  <p class="lead"><a href="start.html"><b>🧭 START HERE — 어디서부터 볼까 (남은 시간별 코스)</b></a></p>
  <p class="lead"><a href="drills.html"><b>⌨️ 드릴 트레이너 — 매일 Python 10 + C 10 타이핑</b></a> · <a href="design/2026-10-03_S00_overview.html"><b>🏗️ 직군별 테스트 설계 지도</b></a></p>
  <p class="lead"><a href="coding.html"><b>→ Python 문제 7개</b></a> — 문제 · starter · 모범답안 · <code>python3 python/run.py NN</code> 채점 (pytest · mutant)</p>
  <div class="tools"><input id="q" placeholder="노트 검색 — 제목·요약·소제목 (예: fixture, mock, deque, SPSC, flaky, conftest)"></div>
  {"".join(tracks)}
</div>""", JS_HUB)


# ----------------------------------------------------------------- 수집
PRIORITY = {}


def load_meta():
    meta = {}
    for f in sorted(META_DIR.glob("[0-9]*.json")):
        try:
            d = json.load(open(f, encoding="utf-8"))
            meta[f.stem] = d
        except Exception:                                 # noqa: BLE001
            pass
    return meta


def collect():
    meta = load_meta()
    colls = []
    for c in COLLECTIONS:
        notes = []
        if not c["dir"].exists():
            print(f"  (없음) {c['dir']}")
            continue
        for f in sorted(c["dir"].glob("*.md")):
            text = f.read_text(encoding="utf-8")
            lines = text.splitlines()
            h1 = next((HEAD.match(l).group(2) for l in lines if HEAD.match(l)
                       and len(HEAD.match(l).group(1)) == 1), f.stem)
            title = title_text(h1)
            m = meta.get(f.stem, {})
            # 요약: 첫 인용문 또는 첫 문단
            summary, buf = "", []
            for l in lines[1:]:
                s = l.strip()
                if s.startswith(">"):
                    q = s.lstrip("> ").strip()
                    q = re.sub(r"^[-*+]\s+", "", q)          # 인용 안 불릿 기호 제거
                    if q:
                        buf.append(strip_md(q))
                    continue
                if buf:
                    break
                if s and not s.startswith("#") and not s.startswith("---"):
                    buf.append(strip_md(s))
                    break
            summary = " · ".join(x for x in buf if x)
            summary = re.sub(r"\s+", " ", re.sub(r"\*\*|\[|\]", "", summary))[:200]
            heads = " ".join(strip_md(HEAD.match(l).group(2)) for l in lines if HEAD.match(l))
            notes.append({
                "id": f"{c['key']}/{f.stem}", "stem": f.stem, "file": f.stem + ".html",
                "h1": h1, "title": title, "summary": summary or m.get("summary_ko", "")[:190],
                "lines": lines, "lines_n": len(lines), "minutes": read_time(text),
                "headings": heads, "icon": m.get("icon", {"plan": "🎯", "notes": "📘", "problems": "🐍", "drills": "⌨️", "design": "🏗️", "playground": "🧪"}.get(c["key"], "📗")), "src": f,
                "badge": m.get("range", ""), "priority": PRIORITY.get(f.stem, ""),
                "sections": sum(1 for l in lines if HEAD.match(l) and len(HEAD.match(l).group(1)) == 2),
            })
        c = dict(c, notes=notes)
        colls.append(c)
    return colls


# ------------------------------------------------------- 코드 페이지 (Python · C) + 문제 목록
PYDIR = ROOT / "python"
LANG = {".py": "python", ".c": "c", ".h": "c"}


def code_title(stem):
    """problems/<NN_x>.md 의 h1 을 제목으로."""
    md = PYDIR / "problems" / f"{stem}.md"
    if md.exists():
        for l in md.read_text(encoding="utf-8").splitlines():
            m = HEAD.match(l)
            if m and len(m.group(1)) == 1:
                return title_text(m.group(2))
    return stem


def code_sources():
    """(source path, label, problem stem or None)"""
    out = []
    for kind, label in (("starters", "starter (TODO)"), ("solutions", "모범답안")):
        for p in sorted((PYDIR / kind).glob("[0-9]*")):
            files = sorted(p.glob("*.py")) if p.is_dir() else [p] if p.suffix == ".py" else []
            for f in files:
                stem = p.stem if p.is_file() else p.name
                out.append((f, label if f.parent == PYDIR / kind else f"{label} · {f.name}", stem))
    for f in sorted((PYDIR / "lib").glob("*.py")):
        if f.name != "__init__.py":
            out.append((f, "피검 코드 (code under test)", None))
    for f in sorted((PYDIR / "graders").glob("*.py")):
        out.append((f, "채점기", None))
    for f in sorted((ROOT / "c").glob("*.c")):
        out.append((f, "C shim", None))
    return out


def code_page(src, label, stem):
    out_file = map_target(src)
    text = src.read_text(encoding="utf-8")
    rel = src.relative_to(ROOT).as_posix()
    title = code_title(stem) if stem else src.name
    nav = ""
    if stem:
        nav = (f'<div class="nav"><a class="prev" href="../problems/{esc(stem)}.html">'
               f'<span class="dir">← 문제</span><span class="nm">{esc(title)}</span></a></div>')
        cmd = f"python3 python/run.py {stem[:2]}" + (" --sol" if "/solutions/" in "/" + rel else "")
    else:
        cmd = rel
    body = f"""
<div class="top"><div class="top-in">
  <a class="home" href="../coding.html">← 문제 목록</a>
  <span class="t">{esc(title)} · {esc(label)}</span>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap"><main>
  <div class="hero">
    <div class="kicker">{esc(rel)}</div>
    <h1>{esc(title)} — {esc(label)}</h1>
    <div class="meta"><span>{len(text.splitlines())}줄</span><span>{esc(cmd)}</span></div>
  </div>
  <figure class="code"><span class="lang">{LANG.get(src.suffix, "")}</span><pre><code>{esc(text)}</code></pre></figure>
  {nav}
</main></div>"""
    return out_file, shell(f"{title} — {label}", body, JS_PAGE)


def coding_index():
    rows = []
    for md in sorted((PYDIR / "problems").glob("[0-9]*.md")):
        stem = md.stem
        def first(kind):
            p = PYDIR / kind / (stem if (PYDIR / kind / stem).is_dir() else stem + ".py")
            f = p / "conftest.py" if p.is_dir() else p
            return os.path.relpath(map_target(f), OUT)
        cells = [f"<b>{esc(stem[:2])}</b>", esc(code_title(stem)),
                 f'<a href="problems/{stem}.html">문제</a>',
                 f'<a href="{first("starters")}">starter</a>',
                 f'<a href="{first("solutions")}">모범답안</a>',
                 f"<code>python3 python/run.py {stem[:2]}</code>"]
        rows.append("<tr>" + "".join(f"<td>{c}</td>" for c in cells) + "</tr>")
    head = "".join(f"<th>{h}</th>" for h in ["N", "주제", "문제", "starter", "답안", "채점"])
    extra = []
    for f, label, stem in code_sources():
        if stem is None:
            extra.append(f'<li><a href="{os.path.relpath(map_target(f), OUT)}">{esc(f.relative_to(ROOT).as_posix())}</a> — {esc(label)}</li>')
    body = f"""
<div class="top"><div class="top-in">
  <a class="home" href="index.html">← 노트 목록</a>
  <span class="t">Python 문제 · Neros technical screen</span>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="hub">
  <h1>Python 문제 7개 — pytest · ring buffer · Python/C</h1>
  <p class="lead">문제를 읽고 <code>python/starters/</code>의 TODO를 채운다. 채점은 <code>technicalScreenPrep/</code>에서
     <code>python3 python/run.py 01</code> (starter) · <code>python3 python/run.py 01 --sol</code> (답안) · <code>python3 python/run.py all --sol</code>.
     <code>.venv</code>의 pytest를 자동으로 쓴다. 02 · 03은 <b>버그 심은 mutant 9개를 잡아야</b> 통과, 04는 pytester 채점기.</p>
  <div class="tw"><table><thead><tr>{head}</tr></thead><tbody>{''.join(rows)}</tbody></table></div>
  <h2>보조 코드</h2>
  <ul>{''.join(extra)}</ul>
  <p class="lead">mutant(<code>python/lib/mutants/</code>)는 사이트에 싣지 않는다 — 문제를 다 푼 뒤 폴더에서 직접 열어 볼 것.</p>
</div>"""
    return shell("Python 문제 — Neros technical screen", body, JS_HUB)


def build_coding():
    (OUT / "code").mkdir(parents=True, exist_ok=True)
    n = 0
    for f, label, stem in code_sources():
        out_file, page = code_page(f, label, stem)
        out_file.write_text(page, encoding="utf-8")
        n += 1
    (OUT / "coding.html").write_text(coding_index(), encoding="utf-8")
    print(f"  code     {n}개 → site/code/  · site/coding.html")
    return n


# ------------------------------------------------------- 드릴 (drills_data.py → md + 트레이너 페이지)
DRILL_DIR = ROOT / "drills"


def load_drills():
    if not (DRILL_DIR / "drills_data.py").exists():
        return None
    sys.path.insert(0, str(DRILL_DIR))
    import drill            # noqa: E402  (drills_data 도 같이 로드)
    drill.cmd_md()          # drills/2026-10-03_D*.md 재생성
    return drill.D


DRILL_CSS = """
.dr-wrap{max-width:1000px;margin:0 auto;padding:30px 20px 90px}
.dr-tabs{display:flex;gap:6px;flex-wrap:wrap;margin:16px 0 8px}
.dr-tabs .btn{font-size:13px;padding:6px 12px}
.dr-sum{display:flex;gap:18px;flex-wrap:wrap;font-size:13px;color:var(--muted);margin:10px 0 18px}
.dr-sum b{color:var(--ink)}
.dr-item{background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:16px 18px;margin:0 0 16px}
.dr-item.ok{border-color:var(--ok)} .dr-item.bad{border-color:var(--warn)}
.dr-head{display:flex;gap:10px;align-items:baseline;flex-wrap:wrap}
.dr-id{font-family:var(--mono);font-size:12px;color:var(--accent)}
.dr-title{font-weight:600;font-size:15.5px}
.dr-time{margin-left:auto;font-family:var(--mono);font-size:12px;color:var(--muted)}
.dr-prompt{font-size:14.5px;line-height:1.6;margin:8px 0 10px}
.dr-item pre{margin:0 0 10px;padding:10px 12px;background:var(--code-bg);border:1px solid var(--line);border-radius:8px;
  overflow-x:auto;font-family:var(--mono);font-size:12.5px;line-height:1.55;color:var(--code-ink)}
.dr-item pre.given{opacity:.75}
.dr-item pre.ghost{opacity:.55}
.dr-item textarea{width:100%;font-family:var(--mono);font-size:13px;line-height:1.55;padding:10px 12px;
  border:1px solid var(--line);border-radius:8px;background:var(--code-bg);color:var(--ink);resize:vertical;tab-size:4}
.dr-item textarea:focus{outline:none;border-color:var(--accent)}
.dr-btns{display:flex;gap:8px;flex-wrap:wrap;margin-top:8px}
.dr-diff{margin-top:10px;font-family:var(--mono);font-size:12.5px;line-height:1.5}
.dr-diff div{padding:1px 8px;border-left:3px solid transparent;white-space:pre-wrap}
.dr-diff .m{border-color:var(--ok)} .dr-diff .x{border-color:var(--warn);background:color-mix(in srgb,var(--warn) 10%,transparent)}
.dr-diff .exp{color:var(--muted)}
.dr-score{font-size:13px;margin-top:6px}
.dr-lang{font-size:11px;border:1px solid var(--line);border-radius:999px;padding:1px 8px;color:var(--muted)}
"""

DRILL_JS = r"""
const DATA = JSON.parse(document.getElementById('drill-data').textContent);
const SK = 'tsp_drill_v1';
let st = {}; try { st = JSON.parse(localStorage.getItem(SK) || '{}'); } catch (e) {}
const save = () => { try { localStorage.setItem(SK, JSON.stringify(st)); } catch (e) {} };
let view = {day: '1', lang: 'all', mode: 'recall'};
try { Object.assign(view, JSON.parse(localStorage.getItem(SK + '_view') || '{}')); } catch (e) {}
const saveView = () => { try { localStorage.setItem(SK + '_view', JSON.stringify(view)); } catch (e) {} };
const esc = s => s.replace(/[&<>]/g, c => ({'&': '&amp;', '<': '&lt;', '>': '&gt;'}[c]));
const norm = s => s.split('\n').map(l => l.replace(/\s+$/, '').replace(/\s+/g, ' ').trim()).filter(l => l);
let randomPick = null;

function items() {
  let xs = DATA.items;
  if (view.day === 'rand') {
    if (!randomPick) randomPick = [...xs].sort(() => Math.random() - .5).slice(0, 10).map(x => x.id);
    xs = xs.filter(x => randomPick.includes(x.id));
  } else if (view.day === 'wrong') {
    xs = xs.filter(x => (st[x.id] || {}).mark === 'bad');
  } else {
    xs = xs.filter(x => String(x.day) === view.day);
  }
  if (view.lang !== 'all') xs = xs.filter(x => x.lang === view.lang);
  return xs;
}

function diffHtml(typed, ans) {
  const a = norm(typed), b = norm(ans);
  let out = '', hit = 0;
  for (let i = 0; i < Math.max(a.length, b.length); i++) {
    const t = a[i] ?? '', e = b[i] ?? '';
    if (t === e) { hit++; out += `<div class="m">${esc(t)}</div>`; }
    else out += `<div class="x">${esc(t) || '∅'}<br><span class="exp">→ ${esc(e) || '(줄 없음)'}</span></div>`;
  }
  return {html: out, hit, total: b.length};
}

function card(it) {
  const s = st[it.id] || {};
  const el = document.createElement('div');
  el.className = 'dr-item' + (s.mark === 'ok' ? ' ok' : s.mark === 'bad' ? ' bad' : '');
  const rows = it.answer.split('\n').length + 1;
  el.innerHTML = `
    <div class="dr-head"><span class="dr-id">${it.id}</span><span class="dr-lang">${it.lang === 'py' ? 'Python' : 'C'}</span>
      <span class="dr-title">${esc(it.title)}</span><span class="dr-time">${s.best ? '최고 ' + s.best + '초' : ''}</span></div>
    <div class="dr-prompt">${esc(it.prompt)}</div>
    ${it.given ? `<pre class="given">${esc(it.given)}</pre>` : ''}
    ${view.mode === 'copy' ? `<pre class="ghost">${esc(it.answer)}</pre>` : ''}
    <textarea rows="${rows}" spellcheck="false" autocapitalize="off" autocomplete="off"
      placeholder="${view.mode === 'copy' ? '위 코드를 그대로 따라 친다' : '정답을 보지 않고 친다 — Tab = 4칸, Enter = 들여쓰기 유지'}">${esc(s.draft || '')}</textarea>
    <div class="dr-btns">
      <button class="btn cmp">비교</button><button class="btn show">정답 보기</button>
      <button class="btn okb">✓ 맞음</button><button class="btn badb">✗ 다시</button><button class="btn clr">지우기</button>
    </div>
    <div class="dr-out"></div>`;
  const ta = el.querySelector('textarea'), out = el.querySelector('.dr-out'), tm = el.querySelector('.dr-time');
  let t0 = null, tick = null;
  ta.addEventListener('keydown', e => {
    if (!t0) { t0 = Date.now(); tick = setInterval(() => { tm.textContent = Math.round((Date.now() - t0) / 1000) + '초'; }, 500); }
    if (e.key === 'Tab') {
      e.preventDefault();
      const p = ta.selectionStart; ta.setRangeText('    ', p, ta.selectionEnd, 'end');
    } else if (e.key === 'Enter') {
      e.preventDefault();
      const p = ta.selectionStart, line = ta.value.slice(0, p).split('\n').pop();
      let ind = (line.match(/^\s*/) || [''])[0];
      if (/[:{]\s*$/.test(line)) ind += '    ';
      ta.setRangeText('\n' + ind, p, ta.selectionEnd, 'end');
    }
  });
  ta.addEventListener('input', () => { st[it.id] = Object.assign(st[it.id] || {}, {draft: ta.value}); save(); });
  const stop = () => { if (tick) clearInterval(tick); tick = null; return t0 ? Math.round((Date.now() - t0) / 1000) : null; };
  el.querySelector('.cmp').onclick = () => {
    const secs = stop(), d = diffHtml(ta.value, it.answer);
    out.innerHTML = `<div class="dr-score">${d.hit}/${d.total}줄 일치${secs ? ' · ' + secs + '초' : ''} (공백 차이는 무시)</div><div class="dr-diff">${d.html}</div>`;
    if (d.hit === d.total && norm(ta.value).length === d.total) {
      const cur = st[it.id] || {};
      st[it.id] = Object.assign(cur, {mark: 'ok', best: secs && (!cur.best || secs < cur.best) ? secs : cur.best});
      save(); el.classList.add('ok'); el.classList.remove('bad'); summary();
    }
  };
  el.querySelector('.show').onclick = () => { out.innerHTML = `<pre>${esc(it.answer)}</pre>`; };
  el.querySelector('.okb').onclick = () => { st[it.id] = Object.assign(st[it.id] || {}, {mark: 'ok'}); save(); el.className = 'dr-item ok'; summary(); };
  el.querySelector('.badb').onclick = () => { st[it.id] = Object.assign(st[it.id] || {}, {mark: 'bad'}); save(); el.className = 'dr-item bad'; summary(); };
  el.querySelector('.clr').onclick = () => { ta.value = ''; out.innerHTML = ''; t0 = null; stop(); tm.textContent = '';
    st[it.id] = Object.assign(st[it.id] || {}, {draft: ''}); save(); ta.focus(); };
  return el;
}

function summary() {
  const xs = items(), ok = xs.filter(x => (st[x.id] || {}).mark === 'ok').length,
        bad = xs.filter(x => (st[x.id] || {}).mark === 'bad').length,
        all = DATA.items.filter(x => (st[x.id] || {}).mark === 'ok').length;
  document.getElementById('dr-sum').innerHTML =
    `<span>이 목록 <b>${ok}</b>/${xs.length} ✓ · <b>${bad}</b> ✗</span><span>전체 <b>${all}</b>/${DATA.items.length} ✓</span>` +
    `<span>에디터: <code>python3 drills/drill.py new ${/^\d$/.test(view.day) ? view.day : 'N'}</code> → <code>check</code></span>`;
}

function render() {
  document.querySelectorAll('[data-day]').forEach(b => b.classList.toggle('on', b.dataset.day === view.day));
  document.querySelectorAll('[data-lang]').forEach(b => b.classList.toggle('on', b.dataset.lang === view.lang));
  document.querySelectorAll('[data-mode]').forEach(b => b.classList.toggle('on', b.dataset.mode === view.mode));
  const d = DATA.days[view.day];
  document.getElementById('dr-theme').textContent = d ? `Day ${view.day} · ${d[0]} — ${d[1]}` :
    view.day === 'rand' ? '무작위 10개 (새로 고침하면 다시 뽑음)' : '✗ 표시한 항목만';
  const list = document.getElementById('dr-list'); list.innerHTML = '';
  items().forEach(it => list.appendChild(card(it)));
  if (!list.children.length) list.innerHTML = '<p class="lead">항목이 없다.</p>';
  summary();
}
document.querySelectorAll('[data-day],[data-lang],[data-mode]').forEach(b => b.onclick = () => {
  if (b.dataset.day) { view.day = b.dataset.day; randomPick = null; }
  if (b.dataset.lang) view.lang = b.dataset.lang;
  if (b.dataset.mode) view.mode = b.dataset.mode;
  saveView(); render();
});
document.getElementById('dr-reset').onclick = () => {
  if (confirm('✓/✗ 표시와 입력한 내용을 모두 지울까?')) { st = {}; save(); render(); }
};
render();
"""


def build_drills_page(D):
    data = {"days": {str(k): v for k, v in D.DAYS.items()},
            "items": [{k: it[k] for k in ("id", "day", "lang", "title", "prompt", "given", "answer")} for it in D.ITEMS]}
    blob = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")
    day_btns = "".join(f'<button class="btn" data-day="{k}">Day {k} · {esc(v[0])}</button>' for k, v in D.DAYS.items())
    md_links = " · ".join(f'<a href="drills/{f.stem}.html">D{f.stem.split("_D")[1][0]}</a>'
                          for f in sorted(DRILL_DIR.glob("*_D[0-9]_*.md")))
    body = f"""
<div class="top"><div class="top-in">
  <a class="home" href="index.html">← 노트 목록</a>
  <span class="t">⌨️ 드릴 트레이너 · Python 10 + C 10 × 5일</span>
  <a class="btn" href="start.html">START HERE</a>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="dr-wrap">
  <h1>⌨️ 매일 타이핑 드릴</h1>
  <p class="lead">정답을 보지 않고 친다 → <b>비교</b>로 줄 단위 확인 → 다 맞으면 자동 ✓ (최고 기록 저장). 처음엔 <b>따라 치기</b>로 손에 익히고, 다음엔 <b>기억해서 치기</b>.
     아침 20분(그날 Day) + 저녁 20분(✗ 항목). 실제 실행·채점은 에디터에서 <code>python3 drills/drill.py new N</code> → <code>python3 drills/drill.py check N</code>.
     정답 목록 md: {md_links}</p>
  <div class="dr-tabs">{day_btns}<button class="btn" data-day="rand">🎲 무작위 10</button><button class="btn" data-day="wrong">✗ 다시 볼 것</button></div>
  <div class="dr-tabs"><button class="btn" data-lang="all">전체</button><button class="btn" data-lang="py">Python</button><button class="btn" data-lang="c">C</button>
    <span style="width:14px"></span><button class="btn" data-mode="recall">기억해서 치기</button><button class="btn" data-mode="copy">따라 치기</button>
    <span style="width:14px"></span><button class="btn" id="dr-reset">기록 초기화</button></div>
  <h2 id="dr-theme" style="font-family:var(--serif);font-size:20px;margin:18px 0 4px"></h2>
  <div class="dr-sum" id="dr-sum"></div>
  <div id="dr-list"></div>
</div>
<script type="application/json" id="drill-data">{blob}</script>"""
    page = shell("드릴 트레이너 — Python · C", body, JS_HUB + DRILL_JS, extra_head=f"<style>{DRILL_CSS}</style>")
    (OUT / "drills.html").write_text(page, encoding="utf-8")
    print(f"  drills   {len(D.ITEMS)}개 → site/drills.html")



def start_page(out_dir):
    """START_HERE.md → out_dir 기준 링크로 렌더링한 페이지."""
    src = ROOT / "START_HERE.md"
    lines = src.read_text(encoding="utf-8").splitlines()
    h1 = next((HEAD.match(l).group(2) for l in lines if HEAD.match(l)
               and len(HEAD.match(l).group(1)) == 1), "START HERE")
    CTX.update(src=ROOT, out=out_dir)
    toc, used = [], set()
    body = render(lines, toc, used)
    toc_html = "".join(f'<li class="l{lvl}"><a href="#{hid}">{esc(text)}</a></li>'
                       for lvl, text, hid in toc if lvl in (2, 3))
    to = lambda p: os.path.relpath(p, out_dir)
    return shell(title_text(h1), f"""
<div class="top"><div class="top-in">
  <a class="home" href="{to(OUT / 'index.html')}">← 전체 노트 목록</a>
  <span class="t">START HERE · 읽는 순서</span>
  <a class="btn" href="{to(OUT / 'coding.html')}">Python 문제</a>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">Neros · Technical Screen · 10-08(목) 11am · 1시간</div>
      <h1>{inline(h1)}</h1>
    </div>
    {body}
  </main>
  <aside><div class="lbl">목차</div><ol>{toc_html}</ol></aside>
</div>""", JS_PAGE)


def build_start():
    if not (ROOT / "START_HERE.md").exists():
        return
    (OUT / "start.html").write_text(start_page(OUT), encoding="utf-8")
    (ROOT / "index.html").write_text(start_page(ROOT), encoding="utf-8")
    print("  start    START_HERE.md → site/start.html · index.html")


def main():
    D = load_drills()
    colls = collect()
    if not any(c["notes"] for c in colls):
        print("노트를 찾지 못했습니다."); return 1
    OUT.mkdir(exist_ok=True)
    totals = {"notes": 0, "minutes": 0, "sections": 0}
    for c in colls:
        d = OUT / c["key"]
        d.mkdir(exist_ok=True)
        for i, nt in enumerate(c["notes"]):
            prev_n = c["notes"][i - 1] if i else None
            next_n = c["notes"][i + 1] if i + 1 < len(c["notes"]) else None
            CTX.update(src=nt["src"].parent, out=d)
            (d / nt["file"]).write_text(
                note_page(nt, prev_n, next_n, c).replace(
                    "<body>", f'<body data-note="{esc(nt["id"])}">', 1), encoding="utf-8")
            totals["notes"] += 1
            totals["minutes"] += nt["minutes"]
            totals["sections"] += nt["sections"]
        print(f"  {c['key']:<8} {len(c['notes'])}개 → site/{c['key']}/")
    build_coding()
    if D:
        build_drills_page(D)
    build_start()
    CTX.update(src=ROOT, out=OUT)
    (OUT / "index.html").write_text(hub_page(colls, totals), encoding="utf-8")
    print(f"built {OUT/'index.html'}  (노트 {totals['notes']} · {totals['minutes']}분 · "
          f"{totals['sections']}절)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
