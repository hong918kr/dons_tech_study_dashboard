#!/usr/bin/env python3
"""노트 마크다운 → 읽기 좋은 HTML 사이트 (논문 읽듯이).

  python3 build_notes_site.py            # notes_site/ 전체 재생성

입력
  concurrency_practice/notes/*.md   기초 노트 (basics)
  verkada_prep/notes/*.md           면접 복습 노트 (review, data/*.json 메타 사용)

출력
  notes_site/index.html             허브 (카드 · 검색 · 진행률)
  notes_site/basics/NN.html         개별 노트 (사이드 TOC · 진행바 · 이전/다음)
  notes_site/review/NN.html

의존성 없음(표준 라이브러리만). 마크다운 지원 범위는 NOTES_SPEC.md 에 맞춘 부분집합:
제목 h1~h3, 문단, 목록(-, 1., - [ ]), 표, 인용, 코드펜스, 구분선, 인라인 코드/굵게/링크.
"""
import html
import json
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent
OUT = ROOT / "notes_site"
COLLECTIONS = [
    {"key": "basics", "dir": ROOT / "concurrency_practice" / "notes",
     "title_ko": "기초 노트", "title_en": "Foundations",
     "blurb": "동시성을 처음부터. 비유 → 개념 → 코드 한 줄씩 → 직접 확인.",
     "src_hint": "concurrency_practice/notes"},
    {"key": "review", "dir": ROOT / "verkada_prep" / "notes",
     "title_ko": "면접 복습 노트", "title_en": "Interview Review",
     "blurb": "문제 은행 8세트의 요약 노트. 표·함정·면접에서 말할 문장 중심.",
     "src_hint": "verkada_prep/notes"},
]
META_DIR = ROOT / "verkada_prep" / "data"

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
               lambda m: ph(f'<a href="{html.escape(m.group(2), quote=True)}">{m.group(1)}</a>'), s)
    s = re.sub(r"\*\*([^*]+)\*\*", r"<strong>\1</strong>", s)
    s = re.sub(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])", r"<em>\1</em>", s)
    s = re.sub(r"\x00(\d+)\x00", lambda m: keep[int(m.group(1))], s)
    return s


def split_row(line):
    line = line.strip()
    if line.startswith("|"):
        line = line[1:]
    if line.endswith("|"):
        line = line[:-1]
    return [c.strip() for c in line.split("|")]


def render_check(lines):
    """```check 블록 → 클릭해서 답을 보는 자가 점검 카드.

    형식:  Q: 질문 (다음 줄로 이어 써도 됨)
           A: 답   (여러 줄 가능, 빈 줄 = 문단 구분)
    """
    pairs, cur = [], None
    for raw in lines:
        s = raw.rstrip()
        if s.startswith("Q:"):
            cur = {"q": [s[2:].strip()], "a": []}
            pairs.append(cur)
        elif s.startswith("A:") and cur is not None:
            cur["a"].append(s[2:].strip())
            cur["in_a"] = True
        elif cur is not None:
            (cur["a"] if cur.get("in_a") else cur["q"]).append(s.strip())
    cards = []
    for n, p in enumerate(pairs, 1):
        q = inline(" ".join(x for x in p["q"] if x))
        paras, buf = [], []
        for x in p["a"] + [""]:
            if x:
                buf.append(x)
            elif buf:
                paras.append("<p>" + inline(" ".join(buf)) + "</p>")
                buf = []
        cards.append(f'<details class="qa"><summary><span class="qn">Q{n}</span>'
                     f'<span class="qt">{q}</span><span class="rv">답 보기</span></summary>'
                     f'<div class="ans">{"".join(paras)}</div></details>')
    return ('<div class="check"><div class="check-h">✎ 자가 점검 — 먼저 머릿속으로 답하고 펼쳐 볼 것'
            f'</div>{"".join(cards)}</div>')


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
            if lang == "check":                         # ---- 자가 점검 카드
                out.append(render_check(buf))
                continue
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
const KEY='vk_notes_read_v1', TKEY='vk_notes_theme', CKEY='vk_notes_check_v1';
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
const KEY='vk_notes_read_v1', TKEY='vk_notes_theme';
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
<style>{CSS}{GUIDE_CSS}</style></head>
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


def hub_page(colls, totals, guides=()):
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
    return shell("Verkada 준비 노트", f"""
<div class="top"><div class="top-in">
  <span class="t"><b>Verkada</b> · Senior Embedded Linux Engineer, Connectivity — 스터디 노트</span>
  <button class="btn" id="resetread">읽음 초기화</button>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="hub">
  <h1>준비 노트</h1>
  <p class="lead">동시성·실시간·설계를 <b>기초부터</b> 다시 쌓고, 면접 직전에 <b>요약</b>으로 훑는다.
     왼쪽 카드부터 순서대로 읽으면 된다. 읽은 노트는 체크로 남고, 체크리스트도 저장된다.</p>
  <div class="stats">
    <div class="stat"><b>{totals["notes"]}</b><span>노트</span></div>
    <div class="stat"><b id="readcount">0</b><span>읽음</span></div>
    <div class="stat"><b>{totals["minutes"]}분</b><span>총 읽기 시간</span></div>
    <div class="stat"><b>{totals["sections"]}</b><span>절</span></div>
  </div>
  <div class="tools"><input id="q" placeholder="노트 검색 — 제목·요약·소제목 (예: condvar, double buffer, epoll)"></div>
  {guide_html(guides)}
  {"".join(tracks)}
</div>""", JS_HUB)


def guide_html(guides):
    if not guides:
        return ""
    cards = "".join(
        f'<a class="card" style="border-color:var(--accent)" data-id="guide/{esc(g["file"].stem)}" '
        f'data-search="{esc(g["title"].lower())} guide 가이드" href="../{esc(g["file"].name)}">'
        f'<div class="ic">🧭</div><h3>{esc(g["title"])}</h3>'
        f'<p>JD와 리크루터 메일의 4개 주제를 순서대로 — 개념 → 그림 → 핵심 코드 → 자가 점검 → 연습 연결. '
        f'여기서 시작해서 아래 노트로 내려가면 된다.</p>'
        f'<div class="foot"><span class="pill pri">여기서 시작</span>'
        f'<span class="pill">{g["chapters"]}개 장</span><span class="pill">{g["minutes"]}분</span></div></a>'
        for g in guides)
    return (f'<section class="track"><h2>학습 가이드 <span style="color:var(--muted);font-size:15px">'
            f'· Study Guide</span></h2><div class="sub">한 편을 처음부터 끝까지 순서대로 읽는 코스. '
            f'장마다 완료 체크가 저장된다.</div><div class="grid">{cards}</div></section>')


# ----------------------------------------------------------------- 수집
PRIORITY = {"02_condvar_queues": "① 최우선", "04_realtime_buffers": "②",
            "07_modular_design": "③", "01_bounded_queue": "① 최우선",
            "03_double_buffer": "②", "10_testable_driver": "③"}


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
                "headings": heads, "icon": m.get("icon", "📘" if c["key"] == "basics" else "📗"),
                "badge": m.get("range", ""), "priority": PRIORITY.get(f.stem, ""),
                "sections": sum(1 for l in lines if HEAD.match(l) and len(HEAD.match(l).group(1)) == 2),
            })
        c = dict(c, notes=notes)
        colls.append(c)
    return colls


# ----------------------------------------------------------------- 학습 가이드
# ROOT/YYYY-MM-DD_*study_guide.md → 같은 이름의 .html (장별 완료 체크 · 이어 읽기)
GUIDE_CSS = """
.check{margin:26px 0;border:1px solid var(--line);border-radius:12px;background:var(--paper);overflow:hidden}
.check-h{font-size:12px;letter-spacing:.06em;color:var(--accent);padding:10px 16px;
         border-bottom:1px solid var(--line);background:var(--accent-soft)}
details.qa{border-bottom:1px solid var(--line)}
details.qa:last-child{border-bottom:0}
details.qa summary{list-style:none;cursor:pointer;display:flex;gap:12px;align-items:flex-start;
                   padding:13px 16px;font-family:var(--serif);font-size:16px;line-height:1.55}
details.qa summary::-webkit-details-marker{display:none}
.qn{font-family:var(--mono);font-size:11px;color:var(--muted);padding-top:4px;min-width:24px}
.qt{flex:1}
.rv{font-family:var(--sans);font-size:11px;color:var(--accent);border:1px solid var(--accent);
    border-radius:999px;padding:2px 9px;white-space:nowrap;margin-top:2px}
details.qa[open] .rv{background:var(--accent);color:#fff}
details.qa[open] .rv::after{content:""}
.ans{padding:2px 16px 14px 52px}
.ans p{font-size:15.5px;line-height:1.7;margin:0 0 10px}
section.chapter{scroll-margin-top:70px}
.ch-foot{display:flex;gap:12px;align-items:center;justify-content:space-between;flex-wrap:wrap;
         margin:40px 0 10px;padding:18px 20px;border:1px dashed var(--line);border-radius:12px}
.ch-foot .done{font:inherit;font-size:14px;padding:9px 16px;border-radius:999px;cursor:pointer;
               border:1.5px solid var(--ok);background:transparent;color:var(--ok)}
.ch-foot .done.on{background:var(--ok);color:#fff}
.ch-foot a.nx{font-family:var(--serif);font-size:15px;text-decoration:none}
aside li.ch a{font-weight:600;color:var(--ink)}
aside li.ch.done a::before{content:"✓ ";color:var(--ok)}
aside li.sub{display:none;padding-left:14px;font-size:12.5px}
aside li.sub.show{display:block}
.resume{font:inherit;font-size:13px;margin-top:16px;padding:9px 16px;border-radius:999px;cursor:pointer;
        border:1.5px solid var(--accent);background:var(--accent);color:#fff}
#chprog{font-size:12px;color:var(--muted);white-space:nowrap}
"""

GUIDE_JS = """
(function(){
  const KEY='vk_guide_done_v1:'+document.body.dataset.guide, TKEY='vk_notes_theme';
  try{const t=localStorage.getItem(TKEY); if(t)document.documentElement.dataset.theme=t;}catch(e){}
  const tb=document.getElementById('theme');
  if(tb)tb.onclick=()=>{const d=document.documentElement;
    const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
    const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx; try{localStorage.setItem(TKEY,nx)}catch(e){}};
  const bar=document.getElementById('bar');
  const onScroll=()=>{const h=document.body.scrollHeight-innerHeight;
    bar.style.width=(h>0?Math.min(100,scrollY/h*100):0)+'%';};
  addEventListener('scroll',onScroll,{passive:true}); onScroll();

  let done=new Set(); try{done=new Set(JSON.parse(localStorage.getItem(KEY)||'[]'))}catch(e){}
  const save=()=>{try{localStorage.setItem(KEY,JSON.stringify([...done]))}catch(e){}};
  const chapters=[...document.querySelectorAll('section.chapter')];
  const prog=document.getElementById('chprog');
  const sync=()=>{
    chapters.forEach(s=>{
      const on=done.has(s.id);
      const b=s.querySelector('.done'); if(b){b.classList.toggle('on',on); b.textContent=on?'✓ 완료한 장':'이 장 완료 표시';}
      const li=document.querySelector('aside li.ch[data-ch="'+s.id+'"]'); if(li)li.classList.toggle('done',on);
    });
    if(prog)prog.textContent=done.size+' / '+chapters.length+' 장 완료';
  };
  chapters.forEach(s=>{const b=s.querySelector('.done');
    if(b)b.onclick=()=>{done.has(s.id)?done.delete(s.id):done.add(s.id); save(); sync();};});
  sync();
  const rs=document.getElementById('resume');
  if(rs)rs.onclick=()=>{const t=chapters.find(s=>!done.has(s.id))||chapters[0]; t.scrollIntoView({behavior:'smooth'});};

  // 체크리스트 저장
  const CKEY=KEY+':check'; let cs={}; try{cs=JSON.parse(localStorage.getItem(CKEY)||'{}')}catch(e){}
  document.querySelectorAll('li.task input').forEach((el,i)=>{ if(cs[i])el.checked=true;
    el.addEventListener('change',()=>{cs[i]=el.checked; try{localStorage.setItem(CKEY,JSON.stringify(cs))}catch(e){}});});

  // 목차: 현재 장 강조 + 그 장의 절만 펼치기
  const links=[...document.querySelectorAll('aside a')];
  const heads=[...document.querySelectorAll('main h2[id],main h3[id]')];
  const io=new IntersectionObserver(es=>{es.forEach(e=>{ if(!e.isIntersecting)return;
      links.forEach(a=>a.classList.remove('active'));
      const a=links.find(x=>x.getAttribute('href')==='#'+e.target.id); if(a)a.classList.add('active');
      const sec=e.target.closest('section.chapter'); if(!sec)return;
      document.querySelectorAll('aside li.sub').forEach(li=>li.classList.toggle('show',li.dataset.ch===sec.id));
  });},{rootMargin:'-80px 0px -70% 0px'});
  heads.forEach(h=>io.observe(h));
})();
"""


def guide_files():
    return sorted(ROOT.glob("20[0-9][0-9]-[0-9][0-9]-[0-9][0-9]_*study_guide.md"))


def build_guide(md_path):
    lines = md_path.read_text(encoding="utf-8").splitlines()
    h1 = next((HEAD.match(l).group(2) for l in lines
               if HEAD.match(l) and len(HEAD.match(l).group(1)) == 1), md_path.stem)
    # 머리말(h1 과 첫 h2 사이) + h2 단위 장으로 분할
    chunks, cur = [], {"title": None, "lines": []}
    for l in lines:
        m = HEAD.match(l)
        if m and len(m.group(1)) == 2:
            chunks.append(cur)
            cur = {"title": m.group(2), "lines": [l]}
        else:
            cur["lines"].append(l)
    chunks.append(cur)
    intro, chapters = chunks[0], [c for c in chunks[1:] if c["title"]]

    used, toc_all = set(), []
    intro_html = render(intro["lines"], [], used)
    secs = []
    for idx, ch in enumerate(chapters):
        toc = []
        body = render(ch["lines"], toc, used)
        cid = toc[0][2] if toc else f"ch{idx}"
        toc_all.append((cid, toc))
        nxt = chapters[idx + 1]["title"] if idx + 1 < len(chapters) else None
        nxt_link = ""
        if nxt:
            nxt_id = None  # 다음 장 id 는 렌더 후에 채운다
            nxt_link = f'<a class="nx" data-next="{idx + 1}" href="#">다음 장 → {esc(strip_md(nxt))}</a>'
        secs.append(f'<section class="chapter" id="{cid}">{body}'
                    f'<div class="ch-foot"><button class="done">이 장 완료 표시</button>{nxt_link}</div>'
                    f'</section>')
    # 다음 장 링크 id 채우기
    ids = [cid for cid, _ in toc_all]
    html_secs = "\n".join(secs)
    for k, cid in enumerate(ids):
        html_secs = html_secs.replace(f'data-next="{k}" href="#"', f'href="#{cid}"')

    aside = []
    for cid, toc in toc_all:
        for lvl, text, hid in toc:
            if lvl == 2:
                aside.append(f'<li class="ch" data-ch="{cid}"><a href="#{hid}">{esc(text)}</a></li>')
            elif lvl == 3:
                aside.append(f'<li class="sub" data-ch="{cid}"><a href="#{hid}">{esc(text)}</a></li>')

    text = "\n".join(lines)
    minutes = read_time(text)
    rel_hub = os.path.relpath(OUT / "index.html", md_path.parent)
    page = shell(strip_md(h1), f"""
<div id="bar"></div>
<div class="top"><div class="top-in">
  <a class="home" href="{rel_hub}">← 노트 허브</a>
  <span class="t">{esc(strip_md(h1))}</span>
  <span id="chprog"></span>
  <button class="btn" id="theme">◐</button>
</div></div>
<div class="wrap">
  <main>
    <div class="hero">
      <div class="kicker">Study Guide · 개념부터 차근차근</div>
      <h1>{inline(h1)}</h1>
      <div class="meta"><span>{len(chapters)}개 장</span><span>약 {minutes}분</span>
        <span>{len(lines)}줄</span><span>원본 {esc(md_path.name)}</span></div>
      <button class="resume" id="resume">▶ 이어 읽기 (완료 안 한 첫 장으로)</button>
    </div>
    {intro_html}
    {html_secs}
  </main>
  <aside><div class="lbl">장</div><ol>{"".join(aside)}</ol></aside>
</div>""", GUIDE_JS).replace("</style>", GUIDE_CSS + "</style>", 1).replace(
        "<body>", f'<body data-guide="{esc(md_path.stem)}">', 1)
    out = md_path.with_suffix(".html")
    out.write_text(page, encoding="utf-8")
    print(f"  guide    {md_path.name} → {out.name}  ({len(chapters)}장 · {minutes}분)")
    return {"title": title_text(h1), "file": out, "minutes": minutes, "chapters": len(chapters)}


def main():
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
            (d / nt["file"]).write_text(
                note_page(nt, prev_n, next_n, c).replace(
                    "<body>", f'<body data-note="{esc(nt["id"])}">', 1), encoding="utf-8")
            totals["notes"] += 1
            totals["minutes"] += nt["minutes"]
            totals["sections"] += nt["sections"]
        print(f"  {c['key']:<8} {len(c['notes'])}개 → notes_site/{c['key']}/")
    guides = [build_guide(g) for g in guide_files()]
    (OUT / "index.html").write_text(hub_page(colls, totals, guides), encoding="utf-8")
    print(f"built {OUT/'index.html'}  (노트 {totals['notes']} · {totals['minutes']}분 · "
          f"{totals['sections']}절)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
