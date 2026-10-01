#!/usr/bin/env python3
"""OSTEP(Operating Systems: Three Easy Pieces) 노트 → 브라우저용 HTML 사이트.

  python3 extract_book.py   # (PDF 바뀔 때만) 영문 원문 → book-md/
  python3 build_site.py     # site/ 전체 재생성

입력  notes/YYYY-MM-DD_CNN_*.md      한국어 공부 노트 (챕터별)
      book-md/CNN_*.md               영문 원문 추출본 (extract_book.py 생성)
      YYYY-MM-DD_OSTEP_study_plan.md 공부 계획·진도표
출력  site/index.html                허브 (파트별 챕터 · 검색 · 읽음 체크)
      site/notes/CNN.html            한국어 노트
      site/book/CNN.html             영문 원문
      site/plan.html                 공부 계획

의존성 없음(표준 라이브러리만). 마크다운 변환기는 algomaster-coding-pattern/build_site.py
에서 가져와 ```svg 펜스와 <details>/<summary> 원시 HTML 통과를 더했다.
생성된 HTML은 손으로 고치지 말 것.
"""
import html
import os
import re
from pathlib import Path

from extract_book import CHAPTERS, PDF

ROOT = Path(__file__).resolve().parent
NOTES = ROOT / "notes"
BOOK = ROOT / "book-md"
OUT = ROOT / "site"
TITLE = "OSTEP — 운영체제 아주 쉬운 세 가지 이야기 · 한국어 공부 노트"
SHORT = "OSTEP 한국어 노트"
LEAD = ("Remzi & Andrea Arpaci-Dusseau의 Operating Systems: Three Easy Pieces (v0.91, 675쪽)를 "
        "챕터별로 정리했다. 각 챕터마다 한국어 공부 노트(쉬운 설명 · 그림 · 직접 돌려본 C 코드 · 펌웨어 관점 · 면접 질문)와 "
        "영문 원문 추출본, PDF 원본 페이지 링크를 나란히 둔다.")
PARTS = {
    "Intro": ("🧭", "들어가며", "OS가 하는 세 가지 일: 가상화 · 동시성 · 영속성"),
    "Virtualization": ("🎭", "Part I · 가상화 (Virtualization)", "CPU와 메모리를 '혼자 쓰는 것처럼' 보이게 하는 법"),
    "Concurrency": ("🧵", "Part II · 동시성 (Concurrency)", "스레드 · 락 · 조건변수 · 세마포어 · 버그 패턴"),
    "Persistence": ("💾", "Part III · 영속성 (Persistence)", "I/O 장치 · 디스크 · RAID · 파일시스템 · 저널링 · 무결성"),
    "Distribution": ("🌐", "Distribution", "분산 시스템 · NFS · AFS"),
    "Appendix": ("📎", "부록 (Appendix)", "VMM · 모니터 · 랩 · Flash SSD"),
}
CTX = {"src": ROOT, "out": OUT}   # 현재 렌더 중인 md 위치 / html 위치 (링크 보정용)


def page_of(cid):
    return next(c[3] for c in CHAPTERS if c[0] == cid)


def fix_href(url):
    """md 기준 상대 링크 → html 기준 상대 링크. .md → 해당 사이트 페이지."""
    if re.match(r"^(https?:|mailto:|#)", url):
        return url
    path, frag = (url.split("#", 1) + [""])[:2]
    target = (CTX["src"] / path.replace("%20", " ")).resolve()
    if target.suffix == ".md":
        m = re.search(r"C([0-9A-I]{2})_", target.name)
        if target.parent == NOTES and m:
            target = OUT / "notes" / f"C{m.group(1)}.html"
        elif target.parent == BOOK and m:
            target = OUT / "book" / f"C{m.group(1)}.html"
        elif "study_plan" in target.name:
            target = OUT / "plan.html"
    rel = os.path.relpath(target, CTX["out"]).replace(" ", "%20")
    return rel + (("#" + frag) if frag else "")


# ----------------------------------------------------------------- markdown
FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*\s*$")
HR = re.compile(r"^\s{0,3}(-{3,}|\*{3,}|_{3,})\s*$")
TABLE_SEP = re.compile(r"^\s*\|?[\s:|-]+\|[\s:|-]*$")
ULI = re.compile(r"^(\s*)[-*+]\s+(.*)$")
OLI = re.compile(r"^(\s*)(\d+)[.)]\s+(.*)$")
RAW_HTML = re.compile(r"^\s*</?(details|summary)\b")


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
               lambda m: ph(f'<a href="{html.escape(fix_href(m.group(2)), quote=True)}">{m.group(1)}</a>'), s)
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
        elif (items and cur.strip() and not li and not HEAD.match(cur) and not RAW_HTML.match(cur)
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
        if RAW_HTML.match(line):
            out.append(line.strip())
            i += 1
            continue
        m = FENCE.match(line)
        if m and m.group(1) == "svg":
            buf, i = [], i + 1
            while i < n and not FENCE.match(md[i]):
                buf.append(md[i])
                i += 1
            i += 1
            out.append('<figure class="svg">' + "\n".join(buf) + "</figure>")
            continue
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
                RAW_HTML.match(md[i]) or HEAD.match(md[i]) or FENCE.match(md[i]) or HR.match(md[i])
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
figure.svg{margin:0 0 22px;padding:14px;background:var(--paper);border:1px solid var(--line);border-radius:10px;overflow-x:auto;text-align:center}
figure.svg svg{max-width:100%;height:auto}
details{margin:0 0 14px;border:1px solid var(--line);border-radius:10px;padding:10px 14px;background:var(--paper)}
details summary{cursor:pointer;font-weight:600}
details[open] summary{margin-bottom:8px}
.part{margin:38px 0 10px;display:flex;align-items:baseline;gap:12px;border-bottom:2px solid var(--ink);padding-bottom:6px}
.part h2{font-family:var(--serif);font-size:24px;margin:0}
.part span{color:var(--muted);font-size:13px}
.rows{display:grid;gap:8px}
.row{display:grid;grid-template-columns:52px minmax(0,1fr) auto;gap:12px;align-items:center;background:var(--paper);
  border:1px solid var(--line);border-radius:12px;padding:10px 14px}
.row.read{border-color:var(--ok)}
.row .cn{font-family:var(--mono);font-size:13px;color:var(--accent)}
.row.read .cn::after{content:" ✓";color:var(--ok)}
.row .tt{font-family:var(--serif);font-size:16px;line-height:1.35}
.row .tt small{display:block;font-family:var(--sans);font-size:12.5px;color:var(--muted);margin-top:2px}
.row .ln{display:flex;gap:6px;flex-wrap:wrap;justify-content:flex-end}
.row .ln a{font-size:12px;text-decoration:none;border:1px solid var(--line);border-radius:999px;padding:3px 10px;white-space:nowrap}
.row .ln a.ko{background:var(--accent);border-color:var(--accent);color:#fff}
.row .ln a:hover{border-color:var(--accent)}
.row.dlg .tt{color:var(--muted);font-size:14.5px}
.planbox{display:block;margin:18px 0;padding:16px 18px;border:1.5px solid var(--accent);border-radius:14px;background:var(--accent-soft);text-decoration:none;color:var(--ink)}
.planbox b{font-family:var(--serif);font-size:18px}
@media (max-width:640px){.row{grid-template-columns:44px minmax(0,1fr)}.row .ln{grid-column:1/-1;justify-content:flex-start}}
.src{font-size:13px;color:var(--muted);margin-top:40px}
@media (max-width:900px){.wrap{grid-template-columns:minmax(0,1fr)}aside{display:none}h1{font-size:26px}}
@media print{.top,aside,.nav,#bar{display:none}.wrap{display:block}body{background:#fff}}
"""

JS_COMMON = """
const KEY='ostep_read_v1',TKEY='ostep_theme';
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
def cid_key(cid):
    return cid.zfill(2)


def collect():
    chs = []
    for cid, part, title, p0 in CHAPTERS:
        key = cid_key(cid)
        book = next(iter(sorted(BOOK.glob(f"C{key}_*.md"))), None)
        note = next(iter(sorted(NOTES.glob(f"*_C{key}_*.md"))), None)
        nlines = note.read_text(encoding="utf-8").splitlines() if note else []
        ko = next((l[2:].strip() for l in nlines if l.startswith("# ")), "")
        secs = [strip_md(l[3:]) for l in nlines if l.startswith("## ")]
        chs.append({"cid": cid, "key": key, "part": part, "title": title, "page": p0,
                    "book": book, "note": note, "ko": ko, "lines": nlines, "secs": secs,
                    "dialogue": "Dialogue" in title,
                    "search": f"{cid} {title} {ko} {' '.join(secs)}".lower()})
    return chs


def topbar(back, label, readbtn=True):
    rb = '<button class="btn" id="readbtn">읽음 표시</button>' if readbtn else ""
    return (f'<div id="bar"></div><div class="top"><div class="top-in">'
            f'<a class="home" href="{back}">← 목차</a><span class="t">{esc(label)}</span>{rb}'
            f'<button class="btn" id="theme" aria-label="테마 전환">◐</button></div></div>')


def doc_page(lines, h1, kicker, meta, back, nav, note_id, src_dir, out_dir):
    CTX["src"], CTX["out"] = src_dir, out_dir
    toc, used = [], set()
    body_lines = [l for l in lines if not l.startswith("# ")]
    body = render(body_lines, toc, used)
    toc_html = "".join(f'<li class="l{l}"><a href="#{h}">{esc(t)}</a></li>' for l, t, h in toc if l in (2, 3))
    js = JS_PAGE if note_id else JS_PAGE.split("const id=document.body")[0] + JS_PAGE.split("sync();", 1)[1]
    return shell(h1, f"""{topbar(back, h1, bool(note_id))}
<div class="wrap"><main>
  <div class="hero"><div class="kicker">{kicker}</div><h1>{inline(h1)}</h1><div class="meta">{meta}</div></div>
  {body}
  <div class="nav">{nav}</div>
</main><aside><div class="lbl">목차</div><ol>{toc_html}</ol></aside></div>""", js, note_id)


def nav_html(prev, nxt, kind):
    out = ""
    for ch, cls, label in ((prev, "prev", "← 이전 (k)"), (nxt, "next", "다음 (j) →")):
        if ch:
            out += (f'<a class="{cls}" href="C{ch["key"]}.html"><span class="dir">{label}</span>'
                    f'<span class="nm">{ch["cid"]}. {esc(ch["ko"] if kind == "notes" and ch["ko"] else ch["title"])}</span></a>')
    return out


def chapter_links(ch, here):
    """here: 'hub' | 'notes' | 'book'"""
    pre = "" if here == "hub" else "../"
    pdf = os.path.relpath(PDF, OUT if here == "hub" else OUT / here).replace(" ", "%20")
    links = []
    if ch["note"]:
        links.append(f'<a class="ko" href="{pre}notes/C{ch["key"]}.html">📝 한국어 노트</a>')
    if ch["book"]:
        links.append(f'<a href="{pre}book/C{ch["key"]}.html">📖 원문</a>')
    links.append(f'<a href="{pdf}#page={ch["page"]}">PDF p.{ch["page"]}</a>')
    return links


def hub_page(chs, plan_exists):
    parts_html = []
    for part, (icon, name, sub) in PARTS.items():
        rows = []
        for ch in [c for c in chs if c["part"] == part]:
            ko = f'<small>{esc(ch["ko"])}</small>' if ch["ko"] else ""
            cls = "row dlg" if ch["dialogue"] else "row"
            rows.append(f'<div class="{cls}" data-id="C{ch["key"]}" data-search="{html.escape(ch["search"])}">'
                        f'<span class="cn">{esc(ch["cid"])}</span><div class="tt">{esc(ch["title"])}{ko}</div>'
                        f'<div class="ln">{"".join(chapter_links(ch, "hub"))}</div></div>')
        parts_html.append(f'<div class="part"><h2>{icon} {esc(name)}</h2><span>{esc(sub)}</span></div>'
                          f'<div class="rows">{"".join(rows)}</div>')
    n_notes = sum(1 for c in chs if c["note"])
    plan = ('<a class="planbox" href="plan.html"><b>🗺️ 공부 계획 · 진도표 열기</b><br>'
            '<span>어떤 순서로, 하루에 얼마나, 무엇을 손으로 해볼지 — 여기서 시작</span></a>') if plan_exists else ""
    js = JS_COMMON + """
const read=readSet(),rows=[...document.querySelectorAll('.row')];
rows.forEach(c=>{if(read.has(c.dataset.id))c.classList.add('read')});
document.getElementById('readcount').textContent=rows.filter(c=>read.has(c.dataset.id)).length;
const q=document.getElementById('q');
q.addEventListener('input',()=>{const v=q.value.trim().toLowerCase();
  rows.forEach(c=>c.classList.toggle('hide',v&&!c.dataset.search.includes(v)))});
document.getElementById('resetread').onclick=()=>{try{localStorage.removeItem(KEY)}catch(e){}location.reload()};
"""
    return shell(TITLE, f"""
<div class="top"><div class="top-in"><span class="t">{SHORT}</span>
  <button class="btn" id="theme" aria-label="테마 전환">◐</button></div></div>
<div class="hub">
  <div class="kicker">Operating Systems: Three Easy Pieces · v0.91</div>
  <h1>{esc(TITLE)}</h1>
  <p class="lead">{esc(LEAD)}</p>
  <div class="stats">
    <div class="stat"><b>{len(chs)}</b><span>챕터+부록</span></div>
    <div class="stat"><b>{n_notes}</b><span>한국어 노트</span></div>
    <div class="stat"><b id="readcount">0</b><span>읽음</span></div>
  </div>
  {plan}
  <div class="tools"><input id="q" placeholder="검색 (예: TLB, fork, 세마포어, journaling, FTL)">
    <button class="btn" id="resetread">읽음 초기화</button></div>
  {"".join(parts_html)}
  <p class="src">노트 원본: notes/*.md · 원문 추출: book-md/*.md · 재생성: <code>python3 build_site.py</code></p>
</div>""", js)


def main():
    chs = collect()
    for d in ("notes", "book"):
        (OUT / d).mkdir(parents=True, exist_ok=True)
        for f in (OUT / d).glob("*.html"):
            f.unlink()
    with_note = [c for c in chs if c["note"]]
    with_book = [c for c in chs if c["book"]]
    for seq, kind in ((with_note, "notes"), (with_book, "book")):
        for i, ch in enumerate(seq):
            prev = seq[i - 1] if i > 0 else None
            nxt = seq[i + 1] if i + 1 < len(seq) else None
            other = [l for l in chapter_links(ch, kind) if f'{kind}/C' not in l]
            if kind == "notes":
                lines, h1 = ch["lines"], ch["ko"] or ch["title"]
                kicker = f"OSTEP 한국어 노트 · Ch.{ch['cid']} · {PARTS[ch['part']][1]}"
                meta = (f"<span>{esc(ch['title'])}</span><span>원본: notes/{esc(ch['note'].name)}</span>"
                        + "".join(f"<span>{l}</span>" for l in other))
                src = NOTES
            else:
                lines = ch["book"].read_text(encoding="utf-8").splitlines()
                h1 = f"{ch['cid']}. {ch['title']}"
                kicker = f"OSTEP 영문 원문 · {PARTS[ch['part']][1]}"
                meta = "".join(f"<span>{l}</span>" for l in other)
                src = BOOK
            page = doc_page(lines, h1, kicker, meta, "../index.html", nav_html(prev, nxt, kind),
                            f"C{ch['key']}" if kind == "notes" else "", src, OUT / kind)
            (OUT / kind / f"C{ch['key']}.html").write_text(page, encoding="utf-8")
    plans = sorted(ROOT.glob("*_OSTEP_study_plan.md"))
    if plans:
        lines = plans[-1].read_text(encoding="utf-8").splitlines()
        h1 = next((l[2:].strip() for l in lines if l.startswith("# ")), "공부 계획")
        (OUT / "plan.html").write_text(doc_page(lines, h1, "OSTEP · 공부 계획", f"<span>원본: {plans[-1].name}</span>",
                                                "index.html", "", "", ROOT, OUT), encoding="utf-8")
    (OUT / "index.html").write_text(hub_page(chs, bool(plans)), encoding="utf-8")
    # 깨진 상대 링크 검사 (html + pdf)
    bad = []
    for f in OUT.rglob("*.html"):
        for href in re.findall(r'href="([^"#:]+)(?:#[^"]*)?"', f.read_text(encoding="utf-8")):
            if not (f.parent / href.replace("%20", " ")).resolve().exists():
                bad.append(f"{f.relative_to(OUT)} -> {href}")
    print(f"built {len(with_note)} notes + {len(with_book)} book chapters + index → {OUT}")
    if bad:
        print("BROKEN LINKS:\n  " + "\n  ".join(sorted(set(bad))[:50]))
        raise SystemExit(1)


if __name__ == "__main__":
    main()
