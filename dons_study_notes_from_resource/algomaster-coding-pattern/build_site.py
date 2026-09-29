#!/usr/bin/env python3
"""AlgoMonster Coding Patterns 한국어 노트(md) → 브라우저용 HTML 사이트.

  python3 build_site.py          # site/ 전체 재생성

입력  original-md/YYYY-MM-DD_MNN_*.md   (모듈별 노트, 파일명 순서 = 모듈 순서)
출력  site/index.html                   허브 (모듈 카드 · 검색 · 읽음 진행률)
      site/MNN_*.html                   모듈 노트 (사이드 목차 · 진행바 · 이전/다음 · j/k)

의존성 없음(표준 라이브러리만). 마크다운 변환기는 verkada 노트 사이트 빌더
(job_interview_prep_research/verkada_.../build_notes_site.py)에서 가져와
한 단계 중첩 목록 지원을 더했다. 생성된 HTML은 손으로 고치지 말 것.
"""
import html
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent
SRC = ROOT / "original-md"
OUT = ROOT / "site"
NOTE_GLOB = "*_M[0-9][0-9]_*.md"

COURSE = {
    "title": "AlgoMonster Coding Patterns — 한국어 정리 노트",
    "short": "AlgoMonster Coding Patterns",
    "kicker": "AlgoMonster · Coding Patterns",
    "lead": ("AlgoMonster 코딩 인터뷰 패턴 코스 11개 모듈(Getting Started → Miscellaneous)을 빠르게 복습할 수 있게 "
             "한국어로 정리한 노트. 모듈마다 5분 복습표 → 레슨별 핵심 아이디어·템플릿 코드·복잡도·함정 → 패턴 인식 키워드 순서."),
    "source": "https://algo.monster/dashboard",
}
ICONS = ["🧭", "🔍", "👉", "🌲", "🔙", "🌊", "🕸️", "⛰️", "🧮", "🏗️", "🧩"]

# ----------------------------------------------------------------- markdown
FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
HR = re.compile(r"^\s{0,3}(-{3,}|\*{3,}|_{3,})\s*$")
TABLE_SEP = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]*$")
ULI = re.compile(r"^(\s*)[-*+]\s+(.*)$")
OLI = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
LESSON_NO = re.compile(r"^\d+\.\s*")


def esc(s):
    return html.escape(s, quote=False)


def strip_md(s):
    s = re.sub(r"\[([^\]]+)\]\([^)]*\)", r"\1", s)
    return re.sub(r"[*`]", "", s).strip()


def slug(text, used):
    base = re.sub(r"[^\w가-힣]+", "-", strip_md(text)).strip("-").lower() or "sec"
    s, n = base, 2
    while s in used:
        s, n = f"{base}-{n}", n + 1
    used.add(s)
    return s


def inline(s):
    keep = []

    def ph(h):
        keep.append(h)
        return f"\x00{len(keep) - 1}\x00"

    s = re.sub(r"`([^`]+)`", lambda m: ph(f"<code>{esc(m.group(1))}</code>"), s)
    s = esc(s)
    s = re.sub(r"\[([^\]]+)\]\(([^)\s]+)\)",
               lambda m: ph(f'<a href="{html.escape(m.group(2), quote=True)}">{m.group(1)}</a>'), s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", s)
    return re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], s)


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def is_table_start(lines, i):
    return "|" in lines[i] and i + 1 < len(lines) and TABLE_SEP.match(lines[i + 1])


def list_item(line):
    """(indent, ordered, text) 또는 None."""
    mo = OLI.match(line)
    if mo:
        return len(mo.group(1)), True, mo.group(3)
    mu = ULI.match(line)
    if mu:
        return len(mu.group(1)), False, mu.group(2)
    return None


def render_list(lines, i):
    """최상위 목록 + 한 단계 중첩(들여쓰기 ≥2칸). 표 행은 목록 안에서 표로 렌더."""
    n = len(lines)
    _, ordered, _ = list_item(lines[i])
    items = []                      # [text, children(list of (ordered, text)), extra_html]
    while i < n:
        cur = lines[i]
        li = list_item(cur)
        if li and li[0] < 2 and li[1] == ordered:
            items.append([li[2], [], ""])
        elif li and li[0] >= 2 and items:
            items[-1][1].append((li[1], li[2]))
        elif items and cur.strip().startswith("|") and is_table_start(lines, i):
            tbl, i = render_table(lines, i)          # 목록 항목 아래 들여쓴 표
            items[-1][2] += tbl
            continue
        elif (items and cur.strip() and not li and not HEAD.match(cur)
              and not FENCE.match(cur) and not HR.match(cur) and not cur.startswith(">")
              and not is_table_start(lines, i)):
            if items[-1][1]:                          # lazy continuation
                o, t = items[-1][1][-1]
                items[-1][1][-1] = (o, t + " " + cur.strip())
            else:
                items[-1][0] += " " + cur.strip()
        else:
            break
        i += 1
    tag = "ol" if ordered else "ul"
    out = []
    for text, kids, extra in items:
        sub = ""
        if kids:
            ktag = "ol" if kids[0][0] else "ul"
            sub = f"<{ktag}>" + "".join(f"<li>{inline(t)}</li>" for _, t in kids) + f"</{ktag}>"
        out.append(f"<li>{inline(text)}{sub}{extra}</li>")
    return f"<{tag}>{''.join(out)}</{tag}>", i


def render_table(lines, i):
    head = split_row(lines[i])
    i += 2
    rows = []
    while i < len(lines) and "|" in lines[i] and lines[i].strip():
        rows.append(split_row(lines[i]))
        i += 1
    th = "".join(f"<th>{inline(c)}</th>" for c in head)
    body = "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in (r + [""] * len(head))[:len(head)])
                   + "</tr>" for r in rows)
    return (f'<div class="tw"><table><thead><tr>{th}</tr></thead><tbody>{body}</tbody></table></div>', i)


def render(md, toc, used):
    out, i, n = [], 0, len(md)
    while i < n:
        line = md[i]
        m = FENCE.match(line)
        if m:
            lang, buf, i = m.group(1), [], i + 1
            while i < n and not FENCE.match(md[i]):
                buf.append(md[i])
                i += 1
            i += 1
            label = f'<span class="lang">{esc(lang)}</span>' if lang else ""
            out.append(f'<figure class="code">{label}<pre><code>{esc(chr(10).join(buf))}</code></pre></figure>')
            continue
        m = HEAD.match(line)
        if m:
            lvl, text = len(m.group(1)), m.group(2)
            if lvl > 1:
                hid = slug(text, used)
                toc.append((lvl, strip_md(text), hid))
                out.append(f'<h{lvl} id="{hid}">{inline(text)}'
                           f'<a class="anchor" href="#{hid}" aria-label="link">#</a></h{lvl}>')
            i += 1
            continue
        if HR.match(line):
            out.append("<hr>")
            i += 1
            continue
        if line.startswith(">"):
            buf = []
            while i < n and md[i].startswith(">"):
                buf.append(md[i][1:].lstrip())
                i += 1
            out.append(f"<blockquote>{render(buf, [], used)}</blockquote>")
            continue
        if is_table_start(md, i):
            tbl, i = render_table(md, i)
            out.append(tbl)
            continue
        if list_item(line):
            lst, i = render_list(md, i)
            out.append(lst)
            continue
        if not line.strip():
            i += 1
            continue
        buf = []
        while i < n and md[i].strip() and not (
                HEAD.match(md[i]) or FENCE.match(md[i]) or HR.match(md[i])
                or md[i].startswith(">") or list_item(md[i]) or is_table_start(md, i)):
            buf.append(md[i].strip())
            i += 1
        out.append(f"<p>{inline(' '.join(buf))}</p>")
    return "\n".join(out)


# ----------------------------------------------------------------- 스타일
CSS = """
:root{--bg:#faf9f7;--paper:#fff;--ink:#1c1b19;--muted:#6b6862;--line:#e3e0da;
  --accent:#1f5f8b;--accent-soft:#e8f0f6;--code-bg:#f6f4f0;--code-ink:#2a2724;--ok:#2f7d55;
  --serif:Georgia,"Apple SD Gothic Neo","Noto Serif KR",serif;
  --sans:-apple-system,BlinkMacSystemFont,"Apple SD Gothic Neo","Segoe UI",sans-serif;
  --mono:ui-monospace,SFMono-Regular,"SF Mono",Menlo,Consolas,monospace}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){
  --bg:#15171a;--paper:#1c1f23;--ink:#e6e4df;--muted:#9da3a8;--line:#30343a;
  --accent:#6fb3e0;--accent-soft:#1e2a33;--code-bg:#111316;--code-ink:#e0ddd6;--ok:#63c08c;color-scheme:dark}}
:root[data-theme=dark]{--bg:#15171a;--paper:#1c1f23;--ink:#e6e4df;--muted:#9da3a8;--line:#30343a;
  --accent:#6fb3e0;--accent-soft:#1e2a33;--code-bg:#111316;--code-ink:#e0ddd6;--ok:#63c08c;color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);-webkit-font-smoothing:antialiased}
a{color:var(--accent)}
.top{position:sticky;top:0;z-index:20;background:color-mix(in srgb,var(--bg) 88%,transparent);
  backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.top-in{max-width:1180px;margin:0 auto;padding:10px 16px;display:flex;align-items:center;gap:12px}
.top a.home{text-decoration:none;color:var(--muted);font-size:13px;white-space:nowrap}
.top .t{font-size:13px;color:var(--muted);overflow:hidden;text-overflow:ellipsis;white-space:nowrap;flex:1}
.btn{font:inherit;font-size:12px;padding:5px 10px;border:1px solid var(--line);border-radius:999px;
  background:var(--paper);color:var(--muted);cursor:pointer}
.btn:hover{color:var(--accent);border-color:var(--accent)}
.btn.on{background:var(--accent);border-color:var(--accent);color:#fff}
#bar{position:fixed;top:0;left:0;height:2px;background:var(--accent);width:0;z-index:30}
.wrap{max-width:1180px;margin:0 auto;padding:0 16px;display:grid;grid-template-columns:minmax(0,1fr) 260px;gap:40px}
main{max-width:780px;padding:36px 0 100px;min-width:0}
aside{position:sticky;top:60px;padding:36px 0;max-height:calc(100vh - 70px);overflow:auto;align-self:start}
aside .lbl{font-size:11px;letter-spacing:.14em;text-transform:uppercase;color:var(--muted);margin-bottom:10px}
aside ol{list-style:none;margin:0;padding:0;font-size:13px;line-height:1.45}
aside li.l3{padding-left:14px;font-size:12.5px}
aside a{display:block;padding:3px 8px;border-radius:6px;text-decoration:none;color:var(--muted);border-left:2px solid transparent}
aside a:hover{color:var(--ink);background:var(--accent-soft)}
aside a.active{color:var(--accent);border-left-color:var(--accent);background:var(--accent-soft)}
.hero{border-bottom:2px solid var(--ink);padding-bottom:20px;margin-bottom:30px}
.kicker{font-size:11px;letter-spacing:.16em;text-transform:uppercase;color:var(--accent);margin-bottom:10px}
h1{font-family:var(--serif);font-size:32px;line-height:1.25;margin:0 0 12px}
.meta{font-size:12.5px;color:var(--muted);display:flex;gap:14px;flex-wrap:wrap}
main h2{font-family:var(--serif);font-size:24px;margin:44px 0 14px;padding-top:12px;border-top:1px solid var(--line);line-height:1.3}
main h3{font-family:var(--serif);font-size:18.5px;margin:28px 0 10px;line-height:1.35}
main p,main li{font-size:16px;line-height:1.72}
main p{margin:0 0 14px}
main ul,main ol{padding-left:22px;margin:0 0 16px}
main li{margin:4px 0}
main li>ul,main li>ol{margin:4px 0 6px}
code{font-family:var(--mono);font-size:.86em;background:var(--code-bg);padding:1px 5px;border-radius:4px;border:1px solid var(--line)}
figure.code{position:relative;margin:0 0 20px;background:var(--code-bg);border:1px solid var(--line);border-radius:10px;overflow:hidden}
figure.code .lang{position:absolute;top:0;right:0;font-family:var(--mono);font-size:10px;letter-spacing:.1em;
  text-transform:uppercase;color:var(--muted);padding:5px 10px}
figure.code pre{margin:0;padding:15px 17px;overflow-x:auto}
figure.code code{background:none;border:0;padding:0;color:var(--code-ink);font-size:13px;line-height:1.6}
blockquote{margin:0 0 20px;padding:12px 16px;background:var(--accent-soft);border-left:3px solid var(--accent);border-radius:0 8px 8px 0}
blockquote p{margin:0 0 6px;font-size:15px}
blockquote p:last-child{margin:0}
.tw{overflow-x:auto;margin:0 0 20px}
table{border-collapse:collapse;width:100%;font-size:14px}
th,td{padding:8px 11px;text-align:left;border-bottom:1px solid var(--line);vertical-align:top}
thead th{border-bottom:1.5px solid var(--ink);font-size:12px;letter-spacing:.03em;color:var(--muted)}
tbody tr:hover{background:var(--accent-soft)}
hr{border:0;border-top:1px solid var(--line);margin:32px 0}
.anchor{opacity:0;margin-left:8px;font-size:.6em;text-decoration:none;color:var(--muted)}
h2:hover .anchor,h3:hover .anchor{opacity:1}
.nav{display:flex;gap:12px;margin-top:56px;padding-top:22px;border-top:1px solid var(--line)}
.nav a{flex:1;text-decoration:none;border:1px solid var(--line);border-radius:10px;padding:13px 15px;background:var(--paper)}
.nav a:hover{border-color:var(--accent)}
.nav .dir{font-size:11px;letter-spacing:.12em;text-transform:uppercase;color:var(--muted)}
.nav .nm{font-family:var(--serif);font-size:15px;color:var(--ink);margin-top:4px;display:block}
.nav a.next{text-align:right}
.hub{max-width:1180px;margin:0 auto;padding:40px 16px 90px}
.hub h1{font-size:34px}
.lead{font-family:var(--serif);font-size:17px;line-height:1.7;color:var(--muted);max-width:720px}
.stats{display:flex;gap:26px;flex-wrap:wrap;margin:24px 0 8px;padding:14px 0;border-top:1px solid var(--line);border-bottom:1px solid var(--line)}
.stat b{display:block;font-family:var(--serif);font-size:26px;line-height:1.1}
.stat span{font-size:11.5px;color:var(--muted);letter-spacing:.06em;text-transform:uppercase}
.tools{display:flex;gap:10px;align-items:center;margin:20px 0 22px;flex-wrap:wrap}
#q{flex:1;min-width:200px;font:inherit;font-size:14px;padding:9px 13px;border:1px solid var(--line);border-radius:999px;background:var(--paper);color:var(--ink)}
#q:focus{outline:none;border-color:var(--accent)}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(280px,1fr));gap:16px}
.card{display:block;text-decoration:none;color:inherit;background:var(--paper);border:1px solid var(--line);border-radius:14px;padding:17px;transition:border-color .15s,transform .15s}
.card:hover{border-color:var(--accent);transform:translateY(-2px)}
.card .ic{font-size:22px}
.card .no{font-size:11px;letter-spacing:.14em;color:var(--accent);margin-left:6px}
.card h3{font-family:var(--serif);font-size:17px;margin:8px 0 8px;line-height:1.35}
.card ul{margin:0 0 10px;padding-left:18px;font-size:13px;line-height:1.55;color:var(--muted)}
.card .foot{display:flex;gap:10px;font-size:11.5px;color:var(--muted)}
.pill{border:1px solid var(--line);border-radius:999px;padding:2px 9px}
.card.read{border-color:var(--ok)}
.card.read .ic::after{content:" ✓";color:var(--ok);font-size:14px}
.hide{display:none}
.src{font-size:13px;color:var(--muted);margin-top:40px}
@media (max-width:900px){.wrap{grid-template-columns:minmax(0,1fr)}aside{display:none}h1{font-size:26px}}
@media print{.top,aside,.nav,#bar{display:none}.wrap{display:block}body{background:#fff}}
"""

JS_COMMON = """
const KEY='amcp_read_v1',TKEY='amcp_theme';
const readSet=()=>{try{return new Set(JSON.parse(localStorage.getItem(KEY)||'[]'))}catch(e){return new Set()}};
try{const t=localStorage.getItem(TKEY);if(t)document.documentElement.dataset.theme=t}catch(e){}
const tb=document.getElementById('theme');
if(tb)tb.onclick=()=>{const d=document.documentElement;
  const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
  const nx=cur==='dark'?'light':'dark';d.dataset.theme=nx;try{localStorage.setItem(TKEY,nx)}catch(e){}};
"""

JS_PAGE = JS_COMMON + """
const bar=document.getElementById('bar');
const onScroll=()=>{const h=document.body.scrollHeight-innerHeight;bar.style.width=(h>0?Math.min(100,scrollY/h*100):0)+'%'};
addEventListener('scroll',onScroll,{passive:true});onScroll();
const id=document.body.dataset.note,rb=document.getElementById('readbtn');
const sync=()=>{const on=readSet().has(id);rb.classList.toggle('on',on);rb.textContent=on?'✓ 읽음':'읽음 표시'};
rb.onclick=()=>{const s=readSet();s.has(id)?s.delete(id):s.add(id);try{localStorage.setItem(KEY,JSON.stringify([...s]))}catch(e){}sync()};sync();
const links=[...document.querySelectorAll('aside a')];
const map=new Map(links.map(a=>[a.getAttribute('href').slice(1),a]));
const io=new IntersectionObserver(es=>es.forEach(e=>{if(e.isIntersecting){links.forEach(a=>a.classList.remove('active'));
  const a=map.get(e.target.id);if(a){a.classList.add('active');a.scrollIntoView({block:'nearest'})}}}),{rootMargin:'-70px 0px -70% 0px'});
document.querySelectorAll('main h2[id],main h3[id]').forEach(h=>io.observe(h));
addEventListener('keydown',e=>{if(e.target.matches('input,textarea'))return;
  if(e.key==='j'){const n=document.querySelector('.nav a.next');if(n)location.href=n.href}
  if(e.key==='k'){const p=document.querySelector('.nav a.prev');if(p)location.href=p.href}});
"""

JS_HUB = JS_COMMON + """
const read=readSet(),cards=[...document.querySelectorAll('.card')];
cards.forEach(c=>{if(read.has(c.dataset.id))c.classList.add('read')});
document.getElementById('readcount').textContent=cards.filter(c=>read.has(c.dataset.id)).length;
const q=document.getElementById('q');
q.addEventListener('input',()=>{const v=q.value.trim().toLowerCase();
  cards.forEach(c=>c.classList.toggle('hide',v&&!c.dataset.search.includes(v)))});
document.getElementById('resetread').onclick=()=>{try{localStorage.removeItem(KEY)}catch(e){}location.reload()};
"""


def shell(title, body, js, note_id=""):
    attr = f' data-note="{note_id}"' if note_id else ""
    return (f'<!doctype html>\n<html lang="ko"><head><meta charset="utf-8">'
            f'<meta name="viewport" content="width=device-width,initial-scale=1">'
            f"<title>{esc(title)}</title><style>{CSS}</style></head>"
            f"<body{attr}>{body}<script>{js}</script></body></html>\n")


# ----------------------------------------------------------------- 수집·출력
def collect():
    notes = []
    for idx, p in enumerate(sorted(SRC.glob(NOTE_GLOB))):
        lines = p.read_text(encoding="utf-8").splitlines()
        h1 = next((l[2:].strip() for l in lines if l.startswith("# ")), p.stem)
        mnum = re.search(r"_M(\d\d)_", p.name).group(1)
        # 레슨 제목(## 0./1./2. ...) = 카드 요약
        lessons = [strip_md(l[3:]) for l in lines
                   if l.startswith("## ") and re.match(r"## \d+\.", l)]
        text = "\n".join(lines)
        notes.append({
            "id": f"M{mnum}", "src": p, "file": f"M{mnum}_{p.stem.split(f'_M{mnum}_', 1)[1]}.html",
            "h1": h1, "title": re.sub(r"^Module \d+ — ", "", h1), "lines": lines,
            "lessons": lessons, "icon": ICONS[idx % len(ICONS)], "num": int(mnum),
            "minutes": max(1, round(len(re.sub(r"\s+", "", text)) / 700)),
            "search": (h1 + " " + " ".join(lessons)).lower(),
        })
    return notes


def note_page(nt, prev, nxt):
    toc, used = [], set()
    body = render(nt["lines"], toc, used)
    toc_html = "".join(f'<li class="l{l}"><a href="#{h}">{esc(t)}</a></li>'
                       for l, t, h in toc if l in (2, 3))
    nav = ""
    if prev:
        nav += (f'<a class="prev" href="{prev["file"]}"><span class="dir">← 이전 (k)</span>'
                f'<span class="nm">M{prev["num"]}. {esc(prev["title"])}</span></a>')
    if nxt:
        nav += (f'<a class="next" href="{nxt["file"]}"><span class="dir">다음 (j) →</span>'
                f'<span class="nm">M{nxt["num"]}. {esc(nxt["title"])}</span></a>')
    return shell(nt["h1"], f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="index.html">← 모듈 목록</a>
  <span class="t">{esc(nt["h1"])}</span>
  <button class="btn" id="readbtn">읽음 표시</button>
  <button class="btn" id="theme" aria-label="테마 전환">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">{COURSE["short"]} · Module {nt["num"]}</div>
      <h1>{inline(nt["h1"])}</h1>
      <div class="meta"><span>레슨 {len(nt["lessons"])}개</span><span>약 {nt["minutes"]}분</span>
        <span>원본 md: original-md/{esc(nt["src"].name)}</span></div>
    </div>
    {body}
    <div class="nav">{nav}</div>
  </main>
  <aside><div class="lbl">목차</div><ol>{toc_html}</ol></aside>
</div>""", JS_PAGE, nt["id"])


def hub_page(notes):
    cards = []
    for nt in notes:
        li = "".join("<li>" + esc(LESSON_NO.sub("", l)) + "</li>" for l in nt["lessons"][:4])
        more = f"<li>… 외 {len(nt['lessons']) - 4}개</li>" if len(nt["lessons"]) > 4 else ""
        cards.append(
            f'<a class="card" href="{nt["file"]}" data-id="{nt["id"]}" data-search="{html.escape(nt["search"])}">'
            f'<span class="ic">{nt["icon"]}</span><span class="no">MODULE {nt["num"]}</span>'
            f'<h3>{esc(nt["title"])}</h3><ul>{li}{more}</ul>'
            f'<div class="foot"><span class="pill">레슨 {len(nt["lessons"])}</span>'
            f'<span class="pill">~{nt["minutes"]}분</span></div></a>')
    total = sum(len(n["lessons"]) for n in notes)
    return shell(COURSE["title"], f"""
<div class="top"><div class="top-in"><span class="t">{COURSE["short"]}</span>
  <button class="btn" id="theme" aria-label="테마 전환">◐</button></div></div>
<div class="hub">
  <div class="kicker">{COURSE["kicker"]}</div>
  <h1>{esc(COURSE["title"])}</h1>
  <p class="lead">{esc(COURSE["lead"])}</p>
  <div class="stats">
    <div class="stat"><b>{len(notes)}</b><span>모듈</span></div>
    <div class="stat"><b>{total}</b><span>레슨</span></div>
    <div class="stat"><b id="readcount">0</b><span>읽음</span></div>
  </div>
  <div class="tools"><input id="q" placeholder="모듈·레슨 검색 (예: sliding window, 위상 정렬, knapsack)">
    <button class="btn" id="resetread">읽음 초기화</button></div>
  <div class="grid">{"".join(cards)}</div>
  <p class="src">원문: <a href="{COURSE["source"]}">{COURSE["source"]}</a> · 노트 원본은 original-md/, 재생성은 <code>python3 build_site.py</code></p>
</div>""", JS_HUB)


def main():
    notes = collect()
    OUT.mkdir(exist_ok=True)
    for i, nt in enumerate(notes):
        prev = notes[i - 1] if i > 0 else None
        nxt = notes[i + 1] if i + 1 < len(notes) else None
        (OUT / nt["file"]).write_text(note_page(nt, prev, nxt), encoding="utf-8")
    (OUT / "index.html").write_text(hub_page(notes), encoding="utf-8")
    # 깨진 상대 링크 검사
    bad = []
    for f in OUT.glob("*.html"):
        for href in re.findall(r'href="([^"#:]+\.html)"', f.read_text(encoding="utf-8")):
            if not (OUT / href).exists():
                bad.append(f"{f.name} -> {href}")
    print(f"built {len(notes)} notes + index → {OUT}")
    if bad:
        print("BROKEN LINKS:\n  " + "\n  ".join(bad))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
