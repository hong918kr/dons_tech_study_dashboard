#!/usr/bin/env python3
"""Scout technical screen practice site: problems/*/README.md → problems/*/index.html + index.html.

README.md is the source of truth (problem in English, explanation in Korean). Never hand-edit HTML.
Reuses the job-research renderer (markdown subset + pastel theme) and adds:
  <div class="code" data-src="solution.c"></div>               → file contents as a C code block
  <div class="chart" data-src="chart1.svg" data-caption="…"></div> → inline SVG figure

Usage:
  python3 build.py                 # every problem + index
  python3 build.py problems/T1-1_* # just those problems (index is refreshed too)
"""
import datetime
import html
import importlib.util
import re
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROB_DIR = HERE / "problems"
spec = importlib.util.spec_from_file_location(
    "rh", Path.home() / ".claude/skills/job-research/scripts/render_html.py")
rh = importlib.util.module_from_spec(spec)
spec.loader.exec_module(rh)

CODE_RE = re.compile(r'<div class="code" data-src="([^"]+)"\s*>\s*</div>')
CHART_RE = re.compile(r'<div class="chart" data-src="([^"]+)"(?: data-caption="([^"]*)")?\s*>\s*</div>')
META_RE = re.compile(r"\*\*(\w[\w ]*)\*\*\s*:\s*([^·]+)")

EXTRA_CSS = r"""
.code-wrap{margin:12px 0; border:1px solid var(--line); border-radius:10px; overflow:hidden; background:#fdfcff}
.code-head{display:flex; justify-content:space-between; align-items:center; padding:6px 12px; background:var(--panel2);
  border-bottom:1px solid var(--line); font:600 .78rem var(--mono); color:var(--ink2)}
.code-head button,.timer button{font:600 .74rem inherit; border:1px solid var(--line2); background:#fff; color:var(--accent);
  border-radius:7px; padding:3px 10px; cursor:pointer}
.code-head button:hover,.timer button:hover{background:var(--accent-dim)}
.code-wrap pre.code{margin:0; border:0; border-radius:0; box-shadow:none}
figure.chart{margin:16px 0; padding:14px; background:#fff; border:1px solid var(--line); border-radius:12px; text-align:center}
figure.chart svg{max-width:100%; height:auto}
figure.chart figcaption{font-size:.8rem; color:var(--muted); margin-top:6px}
details.fold.spoiler{background:#fffaf2; border-color:#f1dfbf}
details.fold.spoiler>summary{color:var(--warn); font-weight:600}
.tk-c{color:#9a93a8; font-style:italic} .tk-k{color:#8a4fc0; font-weight:600} .tk-t{color:#2f7f9f}
.tk-n{color:#c0632f} .tk-s{color:#3f8f55} .tk-p{color:#c4554e}
.timer{position:fixed; right:18px; bottom:18px; z-index:9; display:flex; gap:8px; align-items:center;
  background:#fff; border:1px solid var(--line2); border-radius:12px; padding:8px 12px; box-shadow:0 4px 14px rgba(70,50,110,.12)}
.timer .t{font:700 1.05rem var(--mono); color:var(--ink); min-width:62px; text-align:center}
.timer.late .t{color:var(--crit)}
.prob-tbl td:first-child{white-space:nowrap; font-family:var(--mono)}
.pill{display:inline-block; font-size:.72rem; font-weight:700; padding:1px 8px; border-radius:999px}
.p0{background:var(--crit-bg); color:var(--crit)} .p1{background:var(--warn-bg); color:var(--warn)} .p2{background:var(--blue-bg); color:var(--blue)}
"""

EXTRA_JS = r"""
(function(){
  var KW=/^(if|else|for|while|do|switch|case|default|break|continue|return|goto|sizeof|static|const|volatile|extern|inline|typedef|struct|enum|union|register|restrict|true|false|NULL)$/;
  var TY=/^(void|char|short|int|long|float|double|signed|unsigned|bool|size_t|ssize_t|u?int(8|16|32|64)_t|uintptr_t|[A-Z][A-Za-z0-9]*_t|pthread_\w+|atomic_\w+)$/;
  var TOK=/(\/\*[\s\S]*?\*\/|\/\/[^\n]*)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(^[ \t]*#[^\n]*)|(\b0[xX][0-9a-fA-F]+[uUlL]*\b|\b\d+(?:\.\d+)?[uUlLf]*\b)|([A-Za-z_]\w*)/gm;
  function e(s){return s.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')}
  document.querySelectorAll('code[data-lang="c"],code[data-lang="cpp"]').forEach(function(c){
    var src=c.textContent, out='', last=0, m;
    TOK.lastIndex=0;
    while((m=TOK.exec(src))){
      out+=e(src.slice(last,m.index)); last=TOK.lastIndex;
      var cls=m[1]?'c':m[2]?'s':m[3]?'p':m[4]?'n':KW.test(m[5])?'k':TY.test(m[5])?'t':'';
      out+=cls?'<span class="tk-'+cls+'">'+e(m[0])+'</span>':e(m[0]);
    }
    c.innerHTML=out+e(src.slice(last));
  });
  document.querySelectorAll('.code-head button').forEach(function(b){
    b.addEventListener('click',function(){
      var t=b.closest('.code-wrap').querySelector('code').textContent;
      (navigator.clipboard?navigator.clipboard.writeText(t):Promise.reject()).then(
        function(){b.textContent='copied ✓'; setTimeout(function(){b.textContent='copy'},1200)},
        function(){b.textContent='select manually'});
    });
  });
  var tm=document.querySelector('.timer'); if(!tm) return;
  var left=1800, id=null, show=tm.querySelector('.t');
  function draw(){var a=Math.abs(left); show.textContent=(left<0?'-':'')+Math.floor(a/60)+':'+('0'+a%60).slice(-2);
    tm.classList.toggle('late',left<=300)}
  tm.querySelector('.go').addEventListener('click',function(){
    if(id){clearInterval(id); id=null; this.textContent='start'}
    else{id=setInterval(function(){left--; draw()},1000); this.textContent='pause'}});
  tm.querySelector('.rs').addEventListener('click',function(){left=1800; draw()});
  draw();
})();
"""


def esc(s):
    return html.escape(s, quote=False)


def expand(body, pdir):
    def code(m):
        f = pdir / m.group(1)
        if not f.exists():
            return f'<p class="note">⚠ missing {esc(m.group(1))}</p>'
        lang = "c" if f.suffix in (".c", ".h") else ""
        return (f'<div class="code-wrap"><div class="code-head"><span>{esc(f.name)}</span><button>copy</button></div>'
                f'<pre class="code"><code data-lang="{lang}">{esc(f.read_text(encoding="utf-8").rstrip())}</code></pre></div>')

    def chart(m):
        f = pdir / m.group(1)
        if not f.exists():
            return f'<p class="note">⚠ missing {esc(m.group(1))}</p>'
        svg = re.sub(r"<\?xml.*?\?>", "", f.read_text(encoding="utf-8")).strip()
        cap = f"<figcaption>{esc(html.unescape(m.group(2)))}</figcaption>" if m.group(2) else ""
        return f'<figure class="chart">{svg}{cap}</figure>'

    body = CODE_RE.sub(code, body)
    body = CHART_RE.sub(chart, body)
    return re.sub(r'<details class="fold">(\s*<summary>💡)', r'<details class="fold spoiler">\1', body)


def render_problem(md):
    pdir = md.parent
    raw = md.read_text(encoding="utf-8")
    lines = re.sub(r"<!--.*?-->", "", raw, flags=re.S).splitlines()
    ctx = rh.Ctx(page_key=pdir.name)
    title, meta, rest = rh.split_hero(lines)
    rows, notes, _ = rh.render_meta(meta, ctx)
    body = expand(rh.render_blocks(rest, ctx, top=True), pdir)
    hero = (f'<header class="hero"><div class="crumb"><a href="../../index.html">← 문제 목록</a> · '
            f'<a href="../../../2026-10-12_scout_technical_screen_prep.html">준비 문서</a></div>'
            f'<h1>{rh.inline(title or pdir.name, ctx)}</h1>'
            f'<div class="meta">{"".join(rows)}</div>{"".join(notes)}</header>')
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    footer = (f"<code>{esc(pdir.name)}/README.md</code>에서 자동 생성 · {now}"
              '<div class="timer"><span class="t">30:00</span><button class="go">start</button>'
              f'<button class="rs">reset</button></div><script>{EXTRA_JS}</script>')
    out = pdir / "index.html"
    out.write_text(rh.page(rh.plain(title or pdir.name), hero, body, ctx.toc, footer,
                           '<a class="back" href="../../index.html">← 문제 목록</a>',
                           rh.PASTEL_CSS + EXTRA_CSS), encoding="utf-8")
    return out


def meta_of(md):
    raw = md.read_text(encoding="utf-8")
    title = next((l[2:].strip() for l in raw.splitlines() if l.startswith("# ")), md.parent.name)
    quote = " ".join(l for l in raw.splitlines()[:8] if l.startswith(">"))
    meta = {k.strip(): v.strip() for k, v in META_RE.findall(quote)}
    return title, meta


def sort_key(p):
    m = re.match(r"(T1|T2|D)-?(\d*)", p.name)
    order = {"T1": 0, "T2": 1, "D": 2}
    return (order.get(m.group(1), 9) if m else 9, int(m.group(2) or 0) if m else 0, p.name)


def render_index():
    groups = {"T1": [], "T2": [], "D": []}
    for pdir in sorted((d for d in PROB_DIR.iterdir() if (d / "README.md").exists()), key=sort_key):
        title, meta = meta_of(pdir / "README.md")
        groups.setdefault(pdir.name.split("-")[0].split("_")[0], []).append((pdir, title, meta))
    names = {"T1": "Tier 1 — Sensor / Overheat family (most likely)",
             "T2": "Tier 2 — LeetCode-ish with an embedded wrapper", "D": "Debug drills"}
    secs = []
    for g, items in groups.items():
        if not items:
            continue
        rows = []
        for pdir, title, meta in items:
            pr = meta.get("Priority", "")
            pcls = pr.lower() if pr in ("P0", "P1", "P2") else ""
            key = f"scout-tsp:{pdir.name}"
            short = re.sub(r"^[^·]*·\s*", "", title)
            rows.append(
                f'<tr><td><label><input type="checkbox" data-key="{key}"> {esc(meta.get("ID", pdir.name))}</label></td>'
                f'<td><a href="problems/{pdir.name}/index.html">{esc(short)}</a></td>'
                f'<td>{esc(meta.get("Pattern", ""))}</td><td><span class="pill {pcls}">{esc(pr)}</span></td></tr>')
        secs.append(f'<section class="sec" id="{g}"><h2 id="{g}-h">{esc(names.get(g, g))}</h2>'
                    '<div class="tbl"><table class="prob-tbl"><thead><tr><th>✓ ID</th><th>Problem</th>'
                    f'<th>Pattern</th><th>Pri</th></tr></thead><tbody>{"".join(rows)}</tbody></table></div></section>')
    toc = [(2, names[g], f"{g}-h") for g in groups if groups[g]]
    hero = ('<header class="hero"><div class="crumb"><a href="../2026-10-12_scout_technical_screen_prep.html">'
            '← Technical screen 준비 문서</a> · <a href="../scout_ai_embedded_context.html">포지션 컨텍스트</a></div>'
            '<h1>Scout AI — Coding Practice Set</h1>'
            '<p>문제는 <strong>영어</strong>(면접과 같은 조건), 풀이·설명은 <strong>한국어</strong>. '
            '각 문제: ① 문제 읽기 → ② 오른쪽 아래 <strong>30분 타이머</strong> 켜고 <code>stub.c</code>로 풀기 → '
            '③ <code>./run.sh T1-1</code>로 테스트 → ④ 힌트·풀이·차트로 복기 → ⑤ 체크.</p>'
            '<pre class="code"><code>cd technicalScreenPrep\n./run.sh T1-1        # compile + run your stub.c (ASan/UBSan)\n'
            './run.sh T1-1 sol    # reference solution\n./check_all.sh       # every solution must print ALL PASS</code></pre>'
            '<p class="note">체크 상태는 이 브라우저에만 저장됩니다. 권장 순서: T1-1 → T1-2 → T1-5 → T1-3 → Debug → T2-1 → T2-2</p></header>')
    now = datetime.datetime.now().strftime("%Y-%m-%d %H:%M")
    out = HERE / "index.html"
    out.write_text(rh.page("Scout Coding Practice", hero, "".join(secs), toc,
                           f"<code>build.py</code>로 자동 생성 · {now}",
                           '<a class="back" href="../2026-10-12_scout_technical_screen_prep.html">← 준비 문서</a>',
                           rh.PASTEL_CSS + EXTRA_CSS), encoding="utf-8")
    return out


def main(argv):
    targets = [Path(a).resolve() for a in argv] or sorted(PROB_DIR.iterdir(), key=sort_key)
    for d in targets:
        md = d / "README.md" if d.is_dir() else d
        if md.exists():
            print("rendered", render_problem(md).relative_to(HERE))
    print("rendered", render_index().relative_to(HERE))


if __name__ == "__main__":
    main(sys.argv[1:])
