#!/usr/bin/env python3
"""notes/*.md → html/ (리서치 페이퍼 뷰 + 스터디 대시보드).

의존성 없음 (Python 3.9+). markdown 이 원본이고 HTML 은 생성물이다 — HTML 을 직접 고치지 말 것.

  python3 build_notes_html.py          # 전체 재생성
  open html/index.html                 # 대시보드 열기
"""
import html as _html
import json
import re
import sys
from datetime import datetime
from pathlib import Path

ROOT = Path(__file__).resolve().parent
NOTES = ROOT / "notes"
OUT = ROOT / "html"

# ───────────────────────────── 메타데이터 (대시보드 카드용) ─────────────────────────────
META = {
    "00_drone_architecture": dict(
        icon="🛩️", layer="D", kind="리서치 맵",
        blurb="드론 펌웨어 전체 블록도와 서브시스템 해부. 코드가 아니라 개념 지도 — 온사이트 시스템 설계·디버깅 라운드의 출처.",
        drill=None, prio=1),
    "01_ring_logging": dict(
        icon="🪵", layer="A", kind="플랫폼",
        blurb="ISR-safe 로깅 프론트엔드. SPSC 링, drop 정책, deferred formatting(fmt_id만 전송).",
        drill="01_ring_logging", prio=4),
    "02_telemetry_framing": dict(
        icon="📡", layer="A", kind="플랫폼",
        blurb="바이트 스트림 위로 텔레메트리 레코드를 온전히 넘기는 법. CRC-16 → COBS → 수신 FSM 재동기화.",
        drill="02_telemetry_framing", prio=2),
    "03_config_store": dict(
        icon="🗄️", layer="A", kind="플랫폼",
        blurb="펌웨어가 올라가도 깨지지 않는 파라미터 저장소. TLV, 버전 마이그레이션, A/B 슬롯 원자적 커밋.",
        drill="03_config_store", prio=7),
    "04_ipc_budget": dict(
        icon="⚖️", layer="A", kind="플랫폼",
        blurb="서브시스템이 같은 예산을 두고 싸울 때. 풀 allocator, 메시지 큐, token bucket, 우선순위 대역폭 분배.",
        drill="04_ipc_budget", prio=6),
    "05_embedded_core": dict(
        icon="🔩", layer="B", kind="기본기",
        blurb="폰스크린 단골. 레지스터 RMW와 volatile, 부호확장, 비정렬 접근, Q15 고정소수점, 패딩 vs 와이어 포맷.",
        drill="05_embedded_core", prio=5),
    "06_isr_timing": dict(
        icon="⏱️", layer="B", kind="기본기",
        blurb="ISR 핸드셰이크, 틱 wraparound, 64비트 틱 스냅샷, 드리프트 없는 주기 스케줄링, bottom half.",
        drill="06_isr_timing", prio=5),
    "07_sensors_actuators": dict(
        icon="🚁", layer="C", kind="드론 도메인",
        blurb="실제 신호 경로. IMU 버스트 리드 → 필터 → CRSF 언팩/failsafe → quad-X 믹서 → DShot.",
        drill="07_sensors_actuators", prio=3),
}
LAYERS = [
    ("D", "아키텍처 리서치", "코드가 아닌 개념 지도 — 여기서 시작"),
    ("A", "플랫폼 (JD 직결)", "JD 필수 자격 문장을 그대로 문제로 바꾼 층"),
    ("B", "임베디드 기본기", "어느 펌웨어 면접에나 나오는 것"),
    ("C", "드론 도메인", "센서에서 모터까지 실제 신호 경로"),
]

# ───────────────────────────── markdown → html ─────────────────────────────
INLINE_CODE = re.compile(r"`([^`]+)`")
BOLD = re.compile(r"\*\*(.+?)\*\*", re.S)
ITALIC = re.compile(r"(?<![\w*])\*([^*\n]+)\*(?![\w*])")
LINK = re.compile(r"\[([^\]]+)\]\(([^)\s]+)\)")
BARE_URL = re.compile(r"(?<![\"'=(])\bhttps?://[^\s<>)\]]+")
HEAD = re.compile(r"^(#{1,6})\s+(.*?)\s*#*$")
FENCE = re.compile(r"^\s*```\s*([\w+-]*)\s*$")
HR = re.compile(r"^\s{0,3}(-{3,}|\*{3,}|_{3,})\s*$")
TABLE_SEP = re.compile(r"^\s*\|?[\s:|-]*-{2,}[\s:|-]*\|?\s*$")
LIST = re.compile(r"^(\s*)([-*+]|\d+[.)])\s+(.*)$")
TASK = re.compile(r"^\[([ xX])\]\s*(.*)$")
TAG = re.compile(r"\[(확인됨|추정|정합|영상요약·미검증|JD)\]")


def esc(s):
    return _html.escape(s, quote=False)


def slug(text, used):
    base = re.sub(r"[^\w가-힣]+", "-", strip_md(text)).strip("-").lower() or "sec"
    s, i = base, 2
    while s in used:
        s, i = f"{base}-{i}", i + 1
    used.add(s)
    return s


def strip_md(s):
    s = LINK.sub(r"\1", s)
    return re.sub(r"[*`~]", "", s).strip()


def inline(s):
    ph, out = [], s

    def keep(htm):
        ph.append(htm)
        return f"\x00{len(ph)-1}\x00"

    out = INLINE_CODE.sub(lambda m: keep(f"<code>{esc(m.group(1))}</code>"), out)
    out = LINK.sub(lambda m: keep(
        f'<a href="{_html.escape(m.group(2), quote=True)}" target="_blank" rel="noopener">{esc(m.group(1))}</a>'), out)
    out = esc(out)
    out = BOLD.sub(lambda m: f"<strong>{m.group(1)}</strong>", out)
    out = ITALIC.sub(lambda m: f"<em>{m.group(1)}</em>", out)
    out = TAG.sub(lambda m: f'<span class="tag t-{ {"확인됨":"ok","정합":"ok","추정":"guess","영상요약·미검증":"warn","JD":"jd"}[m.group(1)] }">{m.group(1)}</span>', out)
    out = BARE_URL.sub(lambda m: f'<a href="{m.group(0)}" target="_blank" rel="noopener">{m.group(0)}</a>', out)
    return re.sub(r"\x00(\d+)\x00", lambda m: ph[int(m.group(1))], out)


def convert(lines, key):
    """markdown 라인 리스트 → (html, toc, 체크박스 수)"""
    out, toc, used = [], [], set()
    i, n = 0, len(lines)
    task_n = [0]

    def close_lists(stack):
        while stack:
            out.append(f"</{stack.pop()}>")

    stack = []
    while i < n:
        line = lines[i]

        m = FENCE.match(line)
        if m:
            close_lists(stack)
            lang = m.group(1)
            i += 1
            buf = []
            while i < n and not FENCE.match(lines[i]):
                buf.append(lines[i])
                i += 1
            i += 1
            cls = f' class="lang-{lang}"' if lang else ""
            out.append(f'<div class="codewrap"><pre><code{cls}>{esc(chr(10).join(buf))}</code></pre></div>')
            continue

        m = HEAD.match(line)
        if m:
            close_lists(stack)
            lvl, text = len(m.group(1)), m.group(2)
            if lvl == 1:
                i += 1
                continue  # h1 은 히어로에서 처리
            sid = slug(text, used)
            if lvl in (2, 3):
                toc.append((lvl, sid, strip_md(text)))
            out.append(f'<h{lvl} id="{sid}">{inline(text)}<a class="anchor" href="#{sid}">#</a></h{lvl}>')
            i += 1
            continue

        if HR.match(line):
            close_lists(stack)
            out.append("<hr>")
            i += 1
            continue

        if line.startswith(">"):
            close_lists(stack)
            buf = []
            while i < n and lines[i].startswith(">"):
                buf.append(lines[i][1:].lstrip())
                i += 1
            # 여러 줄에 걸친 **굵게** 를 살리려고 한 덩어리로 inline 처리 후 줄바꿈 복원
            out.append(f'<blockquote>{inline(chr(10).join(buf)).replace(chr(10), "<br>")}</blockquote>')
            continue

        # table
        if "|" in line and i + 1 < n and TABLE_SEP.match(lines[i + 1]):
            close_lists(stack)

            def cells(l):
                l = l.strip()
                if l.startswith("|"):
                    l = l[1:]
                if l.endswith("|"):
                    l = l[:-1]
                return [c.strip() for c in l.split("|")]

            hdr = cells(line)
            i += 2
            rows = []
            while i < n and "|" in lines[i] and lines[i].strip():
                rows.append(cells(lines[i]))
                i += 1
            th = "".join(f"<th>{inline(c)}</th>" for c in hdr)
            tb = "".join("<tr>" + "".join(f"<td>{inline(c)}</td>" for c in r) + "</tr>" for r in rows)
            out.append(f'<div class="tablewrap"><table><thead><tr>{th}</tr></thead><tbody>{tb}</tbody></table></div>')
            continue

        m = LIST.match(line)
        if m:
            indent, marker, text = len(m.group(1)), m.group(2), m.group(3)
            tag = "ol" if marker[0].isdigit() else "ul"
            depth = indent // 2
            while len(stack) > depth + 1:
                out.append(f"</{stack.pop()}>")
            if len(stack) == depth + 1 and stack[-1] != tag:
                out.append(f"</{stack.pop()}>")
            if len(stack) < depth + 1:
                out.append(f"<{tag}>")
                stack.append(tag)
            # 들여쓴 연속 줄은 같은 항목의 일부 (lazy continuation)
            while i + 1 < n and lines[i + 1].strip() and lines[i + 1][:1] in (" ", "\t") \
                    and not LIST.match(lines[i + 1]) and not FENCE.match(lines[i + 1]) \
                    and not HEAD.match(lines[i + 1]) and "|" not in lines[i + 1][:3]:
                i += 1
                text += " " + lines[i].strip()
            t = TASK.match(text)
            if t:
                task_n[0] += 1
                checked = " checked" if t.group(1).lower() == "x" else ""
                out.append(f'<li class="task"><label><input type="checkbox" data-key="{key}:{task_n[0]}"{checked}>'
                           f'<span>{inline(t.group(2))}</span></label></li>')
            else:
                out.append(f"<li>{inline(text)}</li>")
            i += 1
            continue

        if not line.strip():
            close_lists(stack)
            i += 1
            continue

        close_lists(stack)
        buf = []
        while i < n and lines[i].strip() and not HEAD.match(lines[i]) and not LIST.match(lines[i]) \
                and not FENCE.match(lines[i]) and not HR.match(lines[i]) and not lines[i].startswith(">") \
                and not ("|" in lines[i] and i + 1 < n and TABLE_SEP.match(lines[i + 1])):
            buf.append(lines[i])
            i += 1
        out.append(f"<p>{inline(' '.join(buf))}</p>")

    close_lists(stack)
    return "\n".join(out), toc, task_n[0]


# ───────────────────────────── 템플릿 ─────────────────────────────
CSS = """
:root{
  --bg:#0e1116; --panel:#151a21; --panel2:#1b212a; --line:#242c37; --line2:#303a47;
  --ink:#e8edf4; --ink2:#c3ccd8; --muted:#8a95a3; --accent:#5ec8ff; --accent2:#7ee0a8;
  --warn:#ffcf6b; --bad:#ff8a8a; --serif:"Iowan Old Style","Charter","Georgia","Noto Serif KR",serif;
  --sans:-apple-system,BlinkMacSystemFont,"Segoe UI","Pretendard","Apple SD Gothic Neo",sans-serif;
  --mono:"SF Mono",ui-monospace,Menlo,Consolas,monospace;
}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);line-height:1.7;
     -webkit-font-smoothing:antialiased}
a{color:var(--accent);text-decoration:none} a:hover{text-decoration:underline}
code{font-family:var(--mono);font-size:.86em;background:var(--panel2);border:1px solid var(--line);
     border-radius:5px;padding:1px 5px;color:#ffd9a0}
pre{margin:0;overflow-x:auto;padding:16px 18px}
pre code{background:none;border:0;padding:0;color:var(--ink2);font-size:.8rem;line-height:1.55;white-space:pre}
.codewrap{background:#0a0d12;border:1px solid var(--line);border-radius:10px;margin:16px 0}
.tablewrap{overflow-x:auto;margin:16px 0;border:1px solid var(--line);border-radius:10px}
table{border-collapse:collapse;width:100%;font-size:.9rem;min-width:420px}
th,td{border-bottom:1px solid var(--line);padding:9px 13px;text-align:left;vertical-align:top}
th{background:var(--panel2);color:var(--accent);font-weight:600;white-space:nowrap}
tbody tr:hover{background:#ffffff06}
blockquote{margin:16px 0;padding:12px 18px;background:var(--panel);border-left:3px solid var(--accent);
           border-radius:0 10px 10px 0;color:var(--ink2)}
hr{border:0;border-top:1px solid var(--line);margin:30px 0}
.tag{display:inline-block;font-size:.72rem;padding:1px 7px;border-radius:99px;margin:0 2px;
     border:1px solid currentColor;vertical-align:1px}
.t-ok{color:var(--accent2)} .t-guess{color:var(--warn)} .t-warn{color:var(--bad)} .t-jd{color:var(--accent)}
li.task{list-style:none;margin-left:-22px}
li.task label{display:flex;gap:9px;align-items:flex-start;cursor:pointer}
li.task input{margin-top:7px;accent-color:var(--accent2);flex:none}
li.task input:checked+span{color:var(--muted);text-decoration:line-through}
.anchor{opacity:0;margin-left:8px;color:var(--muted);font-weight:400;font-size:.8em}
h2:hover .anchor,h3:hover .anchor{opacity:1}
"""

PAPER_CSS = """
.progress{position:fixed;top:0;left:0;height:3px;background:var(--accent);width:0;z-index:50;transition:width .1s}
.wrap{display:grid;grid-template-columns:260px minmax(0,1fr);gap:40px;max-width:1240px;margin:0 auto;padding:0 28px 80px}
.side{position:sticky;top:0;align-self:start;height:100vh;overflow-y:auto;padding:26px 0 40px;font-size:.85rem}
.side .home{display:inline-block;color:var(--muted);margin-bottom:18px}
.side .navtitle{color:var(--muted);text-transform:uppercase;letter-spacing:.09em;font-size:.68rem;margin:0 0 10px}
.side a.toc{display:block;color:var(--ink2);padding:4px 10px;border-left:2px solid transparent;line-height:1.4}
.side a.toc.l3{padding-left:22px;color:var(--muted);font-size:.8rem}
.side a.toc:hover{color:var(--accent);text-decoration:none}
.side a.toc.active{color:var(--accent);border-left-color:var(--accent);background:#5ec8ff0f}
main{padding:34px 0 0;max-width:860px}
.hero{border-bottom:1px solid var(--line);padding-bottom:22px;margin-bottom:30px}
.eyebrow{color:var(--muted);font-size:.78rem;letter-spacing:.14em;text-transform:uppercase}
.hero h1{font-family:var(--serif);font-size:2.1rem;line-height:1.25;margin:10px 0 12px;font-weight:600}
.hero .facts{display:flex;flex-wrap:wrap;gap:8px;margin-top:14px}
.fact{background:var(--panel);border:1px solid var(--line);border-radius:99px;padding:3px 12px;
      font-size:.78rem;color:var(--ink2)}
article{font-size:1rem}
article h2{font-family:var(--serif);font-size:1.5rem;margin:44px 0 14px;padding-top:10px;
           border-top:1px solid var(--line);font-weight:600}
article h3{font-family:var(--serif);font-size:1.16rem;margin:28px 0 10px;color:var(--accent2);font-weight:600}
article h4{font-size:.98rem;margin:20px 0 8px;color:var(--ink2)}
article p{color:var(--ink2);margin:12px 0}
article ul,article ol{color:var(--ink2);padding-left:24px;margin:12px 0}
article li{margin:5px 0}
.pager{display:flex;justify-content:space-between;gap:14px;margin-top:56px;border-top:1px solid var(--line);padding-top:22px}
.pager a{background:var(--panel);border:1px solid var(--line);border-radius:12px;padding:12px 16px;
         flex:1;color:var(--ink2)}
.pager a:hover{border-color:var(--accent);text-decoration:none}
.pager .dir{display:block;color:var(--muted);font-size:.72rem;margin-bottom:3px}
footer{color:var(--muted);font-size:.76rem;margin-top:30px}
@media (max-width:900px){
  .wrap{grid-template-columns:minmax(0,1fr);gap:0;padding:0 18px 60px}
  .side{position:static;height:auto;overflow:visible;padding:18px 0 0}
  .side .toclist{display:none}
  .hero h1{font-size:1.6rem}
}
@media print{
  :root{--bg:#fff;--panel:#f6f7f9;--panel2:#f0f2f5;--line:#d8dde3;--ink:#111;--ink2:#222;--muted:#666}
  .side,.pager,.progress,.anchor{display:none} .wrap{display:block} main{max-width:none}
}
"""

INDEX_CSS = """
.page{max-width:1180px;margin:0 auto;padding:36px 28px 80px}
.top{display:flex;flex-wrap:wrap;gap:20px;align-items:flex-end;justify-content:space-between;
     border-bottom:1px solid var(--line);padding-bottom:24px;margin-bottom:8px}
.top h1{font-family:var(--serif);font-size:2rem;margin:8px 0 6px;font-weight:600}
.top p{color:var(--muted);margin:0;font-size:.92rem}
.ring{display:flex;align-items:center;gap:14px;background:var(--panel);border:1px solid var(--line);
      border-radius:14px;padding:14px 20px}
.ring .num{font-size:1.9rem;font-family:var(--serif);color:var(--accent)}
.ring .lbl{color:var(--muted);font-size:.78rem;line-height:1.4}
.bar{height:6px;background:var(--panel2);border-radius:99px;overflow:hidden;margin-top:6px;width:150px}
.bar > i{display:block;height:100%;background:linear-gradient(90deg,var(--accent),var(--accent2));width:0}
.sec{margin-top:36px}
.sec h2{font-family:var(--serif);font-size:1.25rem;margin:0 0 4px;font-weight:600}
.sec .sub{color:var(--muted);font-size:.84rem;margin:0 0 16px}
.grid{display:grid;grid-template-columns:repeat(auto-fill,minmax(330px,1fr));gap:16px}
.card{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px 20px;
      display:flex;flex-direction:column;gap:10px;transition:border-color .15s,transform .15s}
.card:hover{border-color:var(--accent);transform:translateY(-2px)}
.card .hd{display:flex;gap:12px;align-items:flex-start}
.card .ico{font-size:1.5rem;line-height:1}
.card h3{margin:0;font-size:1.02rem;font-weight:600}
.card h3 a{color:var(--ink)}
.card .num{color:var(--muted);font-family:var(--mono);font-size:.76rem}
.card p{margin:0;color:var(--ink2);font-size:.88rem;line-height:1.6}
.card .meta{display:flex;flex-wrap:wrap;gap:6px;margin-top:2px}
.chip{font-size:.72rem;color:var(--muted);background:var(--panel2);border:1px solid var(--line);
      border-radius:99px;padding:2px 9px}
.chip.prio{color:var(--warn);border-color:#ffcf6b44}
.card .act{display:flex;gap:8px;align-items:center;margin-top:6px;flex-wrap:wrap}
.btn{font-size:.8rem;border:1px solid var(--line2);background:var(--panel2);color:var(--ink2);
     border-radius:9px;padding:6px 12px;cursor:pointer;font-family:inherit}
.btn:hover{border-color:var(--accent);color:var(--accent)}
.btn.read.on{border-color:var(--accent2);color:var(--accent2);background:#7ee0a814}
.cmd{font-family:var(--mono);font-size:.74rem;color:var(--muted);background:#0a0d12;border:1px solid var(--line);
     border-radius:8px;padding:5px 9px;cursor:pointer}
.cmd:hover{color:var(--accent2)}
.mini{height:4px;background:var(--panel2);border-radius:99px;overflow:hidden}
.mini > i{display:block;height:100%;background:var(--accent2);width:0}
.plan{background:var(--panel);border:1px solid var(--line);border-radius:14px;padding:18px 22px;margin-top:36px}
.plan h2{font-family:var(--serif);font-size:1.15rem;margin:0 0 12px}
.plan ol{margin:0;padding-left:20px;color:var(--ink2);font-size:.9rem}
.plan li{margin:6px 0}
.links{display:flex;flex-wrap:wrap;gap:10px;margin-top:22px}
.links a{font-size:.83rem;background:var(--panel);border:1px solid var(--line);border-radius:10px;padding:8px 14px;color:var(--ink2)}
.links a:hover{border-color:var(--accent);color:var(--accent);text-decoration:none}
footer{color:var(--muted);font-size:.76rem;margin-top:40px;text-align:center}
@media (max-width:640px){.page{padding:22px 16px 60px}.top h1{font-size:1.5rem}}
"""

PAPER_JS = """
(function(){
  function get(k){try{return localStorage.getItem(k)}catch(e){return null}}
  function set(k,v){try{localStorage.setItem(k,v)}catch(e){}}
  var boxes=[].slice.call(document.querySelectorAll('input[data-key]'));
  boxes.forEach(function(cb){
    var k='nrs:'+cb.dataset.key, v=get(k);
    if(v!==null) cb.checked=v==='1';
    cb.addEventListener('change',function(){ set(k,cb.checked?'1':'0'); stats(); });
  });
  function stats(){
    var done=boxes.filter(function(c){return c.checked}).length;
    var el=document.getElementById('taskcount');
    if(el&&boxes.length) el.textContent='리서치 항목 '+done+' / '+boxes.length;
    set('nrs:done:'+PAGE_KEY, done);
  }
  stats();
  var bar=document.querySelector('.progress');
  function scroll(){
    var h=document.documentElement, max=h.scrollHeight-h.clientHeight;
    bar.style.width=(max>0?(h.scrollTop/max*100):0)+'%';
  }
  document.addEventListener('scroll',scroll,{passive:true}); scroll();
  var links=[].slice.call(document.querySelectorAll('a.toc'));
  if('IntersectionObserver' in window && links.length){
    var by={}; links.forEach(function(a){by[a.getAttribute('href').slice(1)]=a});
    var obs=new IntersectionObserver(function(es){
      es.forEach(function(e){
        if(e.isIntersecting&&by[e.target.id]){
          links.forEach(function(a){a.classList.remove('active')});
          by[e.target.id].classList.add('active');
        }
      });
    },{rootMargin:'0px 0px -75% 0px'});
    document.querySelectorAll('h2[id],h3[id]').forEach(function(h){obs.observe(h)});
  }
  set('nrs:read:'+PAGE_KEY,'1');
})();
"""

INDEX_JS = """
(function(){
  function get(k){try{return localStorage.getItem(k)}catch(e){return null}}
  function set(k,v){try{localStorage.setItem(k,v)}catch(e){}}
  var total=0, done=0;
  NOTES.forEach(function(n){
    var card=document.querySelector('[data-note="'+n.key+'"]');
    if(!card) return;
    var d=parseInt(get('nrs:done:'+n.key)||'0',10);
    if(n.tasks){
      total+=n.tasks; done+=Math.min(d,n.tasks);
      card.querySelector('.mini i').style.width=(n.tasks?d/n.tasks*100:0)+'%';
      card.querySelector('.tasklbl').textContent=d+' / '+n.tasks+' 항목';
    }
    var btn=card.querySelector('.btn.read');
    function paint(){
      var on=get('nrs:read:'+n.key)==='1';
      btn.classList.toggle('on',on);
      btn.textContent=on?'✓ 읽음':'읽음 표시';
    }
    btn.addEventListener('click',function(){
      set('nrs:read:'+n.key, get('nrs:read:'+n.key)==='1'?'0':'1'); paint(); recount();
    });
    paint();
  });
  function recount(){
    var r=NOTES.filter(function(n){return get('nrs:read:'+n.key)==='1'}).length;
    document.getElementById('readnum').textContent=r+' / '+NOTES.length;
    document.getElementById('readbar').style.width=(r/NOTES.length*100)+'%';
  }
  recount();
  document.getElementById('tasknum').textContent=done+' / '+total;
  document.getElementById('taskbar').style.width=(total?done/total*100:0)+'%';
  document.querySelectorAll('.cmd').forEach(function(el){
    el.addEventListener('click',function(){
      navigator.clipboard&&navigator.clipboard.writeText(el.dataset.cmd||el.textContent);
      var t=el.textContent; el.textContent='복사됨 ✓';
      setTimeout(function(){el.textContent=t},900);
    });
  });
})();
"""


def page(title, css, body, js, extra_head=""):
    return f"""<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title>{extra_head}
<style>{CSS}{css}</style>
</head><body>
{body}
<script>{js}</script>
</body></html>"""


def build():
    OUT.mkdir(exist_ok=True)
    files = sorted(NOTES.glob("*.md"))
    if not files:
        print("notes/*.md 없음", file=sys.stderr)
        return 1

    parsed = []
    for f in files:
        key = f.stem
        raw = f.read_text(encoding="utf-8")
        lines = raw.splitlines()
        title = next((l[2:].strip() for l in lines if l.startswith("# ")), key)
        body, toc, tasks = convert(lines, key)
        words = len(re.findall(r"\S+", raw))
        meta = META.get(key, dict(icon="📄", layer="A", kind="노트", blurb="", drill=None, prio=9))
        parsed.append(dict(key=key, title=title, body=body, toc=toc, tasks=tasks,
                           minutes=max(3, round(words / 320)), words=words, **meta))

    stamp = datetime.now().strftime("%Y-%m-%d %H:%M")

    # ── 개별 페이퍼 ──
    for idx, p in enumerate(parsed):
        prev_p = parsed[idx - 1] if idx else None
        next_p = parsed[idx + 1] if idx + 1 < len(parsed) else None
        toc_html = "".join(
            f'<a class="toc l{lvl}" href="#{sid}">{esc(t)}</a>' for lvl, sid, t in p["toc"])
        facts = [f'<span class="fact">{p["kind"]}</span>',
                 f'<span class="fact">약 {p["minutes"]}분</span>']
        if p["tasks"]:
            facts.append(f'<span class="fact" id="taskcount">리서치 항목 0 / {p["tasks"]}</span>')
        if p["drill"]:
            facts.append(f'<span class="fact">🔧 make prob N={p["drill"]}</span>')
        pager = []
        if prev_p:
            pager.append(f'<a href="{prev_p["key"]}.html"><span class="dir">← 이전</span>{esc(prev_p["title"])}</a>')
        if next_p:
            pager.append(f'<a href="{next_p["key"]}.html" style="text-align:right">'
                         f'<span class="dir">다음 →</span>{esc(next_p["title"])}</a>')
        body = f"""<div class="progress"></div>
<div class="wrap">
  <nav class="side">
    <a class="home" href="index.html">← 스터디 대시보드</a>
    <p class="navtitle">목차</p>
    <div class="toclist">{toc_html}</div>
  </nav>
  <main>
    <header class="hero">
      <div class="eyebrow">{p['icon']} Neros · Senior Firmware Engineer, Platform</div>
      <h1>{esc(p['title'])}</h1>
      <p style="color:var(--muted);margin:0">{esc(p['blurb'])}</p>
      <div class="facts">{''.join(facts)}</div>
    </header>
    <article>{p['body']}</article>
    <div class="pager">{''.join(pager)}</div>
    <footer><code>notes/{p['key']}.md</code>에서 자동 생성 · {stamp} · 내용 수정은 md에서 (체크 상태는 이 브라우저에만 저장)</footer>
  </main>
</div>"""
        js = f"var PAGE_KEY={json.dumps(p['key'])};\n" + PAPER_JS
        (OUT / f"{p['key']}.html").write_text(
            page(p["title"], PAPER_CSS, body, js), encoding="utf-8")

    # ── 대시보드 ──
    secs = []
    for code, name, sub in LAYERS:
        items = [p for p in parsed if p["layer"] == code]
        if not items:
            continue
        cards = []
        for p in items:
            num = p["key"].split("_")[0]
            chips = [f'<span class="chip">{p["kind"]}</span>', f'<span class="chip">약 {p["minutes"]}분</span>']
            if p["prio"] <= 3:
                chips.append(f'<span class="chip prio">우선순위 {p["prio"]}</span>')
            drill = (f'<span class="cmd" data-cmd="make prob N={p["drill"]}">make prob N={p["drill"]}</span>'
                     if p["drill"] else '<span class="chip">코드 없음 · 개념</span>')
            prog = (f'<div class="mini"><i></i></div><span class="chip tasklbl">0 / {p["tasks"]} 항목</span>'
                    if p["tasks"] else "")
            cards.append(f"""<div class="card" data-note="{p['key']}">
  <div class="hd"><div class="ico">{p['icon']}</div>
    <div><div class="num">{num}</div><h3><a href="{p['key']}.html">{esc(p['title'].split('—')[-1].strip())}</a></h3></div></div>
  <p>{esc(p['blurb'])}</p>
  <div class="meta">{''.join(chips)}</div>
  {prog}
  <div class="act"><a class="btn" href="{p['key']}.html">읽기 →</a>
    <button class="btn read">읽음 표시</button>{drill}</div>
</div>""")
        secs.append(f'<section class="sec"><h2>{name}</h2><p class="sub">{sub}</p>'
                    f'<div class="grid">{"".join(cards)}</div></section>')

    plan = """<div class="plan"><h2>📌 추천 학습 순서</h2><ol>
<li><strong>00 아키텍처 §1~§2</strong> — 전체 블록도와 제어 루프 타이밍 예산. 30분이면 대화의 질이 달라진다.</li>
<li><strong>02 텔레메트리 프레이밍</strong> — 출제 확률 1위. COBS+CRC+재동기화 파서는 이 직군의 시그니처.</li>
<li><strong>07 센서·액추에이터</strong> — 드론 도메인 갭을 가장 빨리 메운다. CRSF·DShot·믹서를 손으로.</li>
<li><strong>01 링·로깅</strong> — JD 첫 줄이 로깅.</li>
<li><strong>05 / 06 기본기</strong> — 폰스크린 단골. 감이 무뎠으면 여기부터 워밍업.</li>
<li><strong>04 → 03</strong> — 시스템 설계 라운드용.</li>
<li><strong>00 §9 Top 15</strong> — 남는 시간에 리서치 항목을 하나씩 소화.</li>
</ol></div>"""

    body = f"""<div class="page">
  <div class="top">
    <div>
      <div class="eyebrow" style="color:var(--muted);font-size:.78rem;letter-spacing:.14em">NEROS TECHNOLOGIES · TORRANCE, CA</div>
      <h1>Senior Firmware Engineer, Platform — 스터디 노트</h1>
      <p>7세트 42문제 · 123체크 + 아키텍처 리서치 맵 · 각 노트는 리서치 페이퍼 형식으로 읽습니다.</p>
    </div>
    <div style="display:flex;gap:12px;flex-wrap:wrap">
      <div class="ring"><div class="num" id="readnum">0 / {len(parsed)}</div>
        <div class="lbl">읽은 노트<div class="bar"><i id="readbar"></i></div></div></div>
      <div class="ring"><div class="num" id="tasknum">0 / 0</div>
        <div class="lbl">리서치 항목<div class="bar"><i id="taskbar"></i></div></div></div>
    </div>
  </div>
  {''.join(secs)}
  {plan}
  <div class="links">
    <a href="../README.md">📖 practice/README.md</a>
    <a href="../../neros_tech_senior_firmware_engineer_context.html">🏢 회사·적합도·예상 인터뷰</a>
    <a href="../../neros_phone_screen_prep_2026-09-16.html">📞 리크루터 콜 시트</a>
    <a href="../../../dons_job_dashboard.html">📋 지원 현황판</a>
  </div>
  <footer>notes/*.md 에서 자동 생성 · {stamp} · 재생성: <code>python3 build_notes_html.py</code></footer>
</div>"""
    payload = "var NOTES=" + json.dumps(
        [{"key": p["key"], "tasks": p["tasks"]} for p in parsed], ensure_ascii=False) + ";\n"
    (OUT / "index.html").write_text(
        page("Neros Platform — 스터디 노트", INDEX_CSS, body, payload + INDEX_JS), encoding="utf-8")

    print(f"생성 완료: {OUT}/index.html + {len(parsed)}개 노트 페이지")
    for p in parsed:
        print(f"  {p['key']}.html  ({p['minutes']}분, 리서치 항목 {p['tasks']}개, 섹션 {len(p['toc'])}개)")
    return 0


if __name__ == "__main__":
    sys.exit(build())
