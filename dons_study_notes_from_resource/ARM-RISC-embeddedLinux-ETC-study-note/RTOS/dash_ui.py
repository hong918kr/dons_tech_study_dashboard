"""RTOS 스터디 사이트 — 대시보드 · 커리큘럼 지도 · 내용 둘러보기 페이지 생성.

build_site.py 가 노트를 다 만든 뒤 build(...) 를 부른다.
  site/index.html   대시보드 (모듈 아이콘 타일 · 항목 점 · 진행률)
  site/map.html     커리큘럼 지도 (Phase 레인 → 모듈 → 항목 칩)
  site/browse.html  내용 둘러보기 (왼쪽 필터: 트랙 · 레벨 · 상태 · Don · 검색 → 카드 + 완료 체크)
  site/list.html    예전 노트 목록 허브 (build_site.hub_page 결과를 그대로 옮김)

데이터 출처: contents.md 의 "### X." 모듈 표(166항목) + 이미 쓴 노트 파일(collect 결과).
완료 상태는 브라우저 localStorage('rtos_done_v1')에 저장하고, 노트 페이지의 "읽음" 표시(eai_notes_read_v1)와 연동한다.
"""
import html
import json
import re

# Phase 구성 — contents.md §6 학습 순서와 같다
PHASES = [
    ("Phase 1", "원천", "ABCDE", "리셋부터 컨텍스트 스위치까지 레지스터 수준으로 설명하고, 직접 만든 미니 RTOS로 증명"),
    ("Phase 2", "플랫폼", "FGHJ", "FreeRTOS · Zephyr로 센서 앱을 짜고, 스택 · 메모리 · 드라이버 설계를 근거 있게 고름"),
    ("Phase 3", "시스템", "KLP", "업데이트 · 보안 · 저전력 · 멀티코어 설계를 말로 설명"),
    ("Phase 4", "이론 · 품질", "MNO", "HardFault 해독, 트레이스로 지터 증명, RTA로 스케줄 가능성 계산, 안전 인증 대화"),
    ("병행", "트렌드 · 도메인 · 면접", "IQRS", "Rust · Zephyr 트렌드 대화, 드론 · 차량 회사 맞춤 답변, 포트폴리오 실습"),
]


def _cells(line):
    return [c.strip() for c in line.strip().strip("|").split("|")]


def parse_items(contents_md):
    """contents.md → [{id, m, t, c, w, p, don, bm}]"""
    items, mod = [], None
    for line in contents_md.splitlines():
        m = re.match(r"^### ([A-Z])\. ", line)
        if m:
            mod = m.group(1)
            continue
        if mod and re.match(r"^\| %s\d+ \|" % mod, line):
            c = _cells(line)
            items.append({"id": c[0], "m": mod, "t": c[1], "c": c[2], "w": c[3], "p": c[4],
                          "don": c[5], "bm": c[6] if len(c) > 6 and c[6] not in ("—", "-") else ""})
        if line.startswith("## 5."):
            mod = None
    return items


def build(out_dir, contents_md, collections, colls, inline_fn, strip_md_fn, hub_html):
    items = parse_items(contents_md)
    notes = {}
    for c in colls:
        for nt in c["notes"]:
            m = re.search(r"_([A-Z]\d+)_", nt["stem"])
            if m:
                notes[m.group(1)] = {"href": f'{c["key"]}/{nt["file"]}', "nid": nt["id"],
                                     "min": nt["minutes"], "lines": nt["lines_n"]}
    mods = []
    phase_of = {k: i for i, ph in enumerate(PHASES) for k in ph[2]}
    for col in collections:
        if col["key"] == "compass":
            continue
        k = col["key"]
        mods.append({"k": k, "ko": re.sub(r"^[A-Z]\. ", "", col["title_ko"]), "en": col["title_en"],
                     "icon": col["icon"], "blurb": col["blurb"], "ph": phase_of.get(k, 4)})
    data_items = []
    for it in items:
        n = notes.get(it["id"])
        data_items.append({
            "id": it["id"], "m": it["m"], "t": strip_md_fn(it["t"]),
            "c": inline_fn(it["c"]), "w": strip_md_fn(it["w"]),
            "p": it["p"], "don": it["don"], "bm": it["bm"],
            "href": n["href"] if n else "", "nid": n["nid"] if n else "", "min": n["min"] if n else 0,
        })
    data = {"items": data_items, "mods": mods,
            "phases": [{"n": a, "ko": b, "mods": list(c), "goal": d} for a, b, c, d in PHASES]}
    blob = json.dumps(data, ensure_ascii=False).replace("</", "<\\/")

    pages = {
        "index.html": ("RTOS 대시보드", "dash", BODY_DASH),
        "map.html": ("커리큘럼 지도", "map", BODY_MAP),
        "browse.html": ("내용 둘러보기", "browse", BODY_BROWSE),
    }
    for fname, (title, key, body) in pages.items():
        (out_dir / fname).write_text(page(title, key, body, blob), encoding="utf-8")
    # 예전 허브는 list.html 로 보존
    (out_dir / "list.html").write_text(hub_html, encoding="utf-8")
    written = sum(1 for d in data_items if d["href"])
    print(f"  dashboard  index/map/browse/list ({len(data_items)}항목, 노트 {written}개)")


def page(title, active, body, blob):
    nav = "".join(
        f'<a href="{h}" class="{"on" if k == active else ""}">{esc(t)}</a>'
        for k, h, t in [("dash", "index.html", "대시보드"), ("map", "map.html", "커리큘럼 지도"),
                        ("browse", "browse.html", "내용 둘러보기"), ("compass", "compass/contents.html", "나침반"),
                        ("list", "list.html", "노트 목록")])
    return f"""<!doctype html>
<html lang="ko"><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>{esc(title)}</title>
<style>{CSS}</style></head>
<body data-page="{active}">
<header class="bar"><div class="bar-in">
  <a class="brand" href="index.html"><span class="logo">⏱</span><b>RTOS</b> 연구 노트</a>
  <nav>{nav}</nav>
  <button class="tbtn" id="theme" title="테마">◐</button>
</div></header>
{body}
<script type="application/json" id="data">{blob}</script>
<script>{JS}</script>
</body></html>"""


def esc(s):
    return html.escape(str(s), quote=True)


BODY_DASH = """
<main class="wrap">
  <section class="stats" id="stats"></section>
  <div class="sec-h"><h2>모듈</h2><span class="hint">타일을 누르면 그 트랙의 카드가 펼쳐진다 · 점 하나 = 항목 하나</span></div>
  <section class="tiles" id="tiles"></section>
  <div class="legend">
    <span><i class="dot"></i>예정</span><span><i class="dot w"></i>노트 있음</span>
    <span><i class="dot d"></i>완료</span><span><i class="dot p0"></i>P0 (굵은 테두리)</span>
  </div>
  <div class="two">
    <section class="panel"><h3>이어서 읽기</h3><div id="next" class="chips"></div></section>
    <section class="panel"><h3>Phase 진행</h3><div id="phases"></div></section>
  </div>
</main>"""

BODY_MAP = """
<main class="wrap">
  <div class="sec-h"><h2>커리큘럼 지도</h2><span class="hint">위에서 아래로 진행 · 칩을 누르면 노트(있으면) 또는 카드로 이동</span></div>
  <div class="legend">
    <span><i class="chip-l"></i>예정</span><span><i class="chip-l w"></i>노트 있음</span><span><i class="chip-l d"></i>완료</span>
    <span><i class="chip-l p0"></i>P0</span><span>🔁 = Bare-Metal 겹침</span>
  </div>
  <section id="map"></section>
</main>"""

BODY_BROWSE = """
<div class="browse">
  <aside class="side" id="side">
    <input id="q" type="search" placeholder="검색 — ID · 주제 · 개념">
    <div class="grp"><div class="gl">트랙</div><div id="f-mod"></div></div>
    <div class="grp"><div class="gl">레벨</div><div id="f-pri" class="pills"></div></div>
    <div class="grp"><div class="gl">상태</div><div id="f-st" class="pills"></div></div>
    <div class="grp"><div class="gl">Don 출발점</div><div id="f-don" class="pills"></div></div>
    <div class="grp"><label class="ck"><input type="checkbox" id="f-bm"> 🔁 Bare-Metal 겹침만</label></div>
    <button class="tbtn wide" id="reset">필터 초기화</button>
  </aside>
  <main class="cards-wrap">
    <div class="cards-h"><h2 id="ttl">전체</h2><span id="cnt" class="hint"></span></div>
    <div id="cards" class="cards"></div>
  </main>
</div>"""

CSS = r"""
:root{--bg:#f6f5f2;--paper:#fff;--ink:#1d1c1a;--muted:#6c6963;--line:#e2dfd8;--soft:#efece6;
  --acc:#8a5a2b;--acc-soft:#f3ece2;--blue:#4a7bd0;--green:#3f9a6b;--orange:#e08a3c;--red:#d0564a;--gray:#b9b5ad;
  --p1:#4a7bd0;--p2:#3f9a6b;--p3:#e08a3c;--p4:#d0564a;--p5:#888;
  --sans:-apple-system,BlinkMacSystemFont,"Apple SD Gothic Neo","Segoe UI",sans-serif;
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;color-scheme:light}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#16151a;--paper:#1f1e24;--ink:#e9e7e2;
  --muted:#a29e96;--line:#34323b;--soft:#28272e;--acc:#d9a441;--acc-soft:#2b271f;--gray:#55525c;color-scheme:dark}}
:root[data-theme=dark]{--bg:#16151a;--paper:#1f1e24;--ink:#e9e7e2;--muted:#a29e96;--line:#34323b;--soft:#28272e;
  --acc:#d9a441;--acc-soft:#2b271f;--gray:#55525c;color-scheme:dark}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--ink);font-family:var(--sans);font-size:14px;-webkit-font-smoothing:antialiased}
a{color:inherit;text-decoration:none}
.bar{position:sticky;top:0;z-index:20;background:color-mix(in srgb,var(--bg) 90%,transparent);backdrop-filter:blur(8px);border-bottom:1px solid var(--line)}
.bar-in{max-width:1280px;margin:0 auto;padding:10px 16px;display:flex;align-items:center;gap:14px;flex-wrap:wrap}
.brand{font-size:15px;display:flex;align-items:center;gap:6px;white-space:nowrap}
.logo{font-size:18px}
nav{display:flex;gap:4px;flex:1;flex-wrap:wrap}
nav a{padding:6px 11px;border-radius:999px;color:var(--muted);font-size:13px}
nav a:hover{background:var(--soft);color:var(--ink)}
nav a.on{background:var(--ink);color:var(--bg)}
.tbtn{font:inherit;font-size:12px;padding:6px 11px;border:1px solid var(--line);border-radius:999px;background:var(--paper);color:var(--muted);cursor:pointer}
.tbtn:hover{border-color:var(--acc);color:var(--acc)}
.tbtn.wide{width:100%;margin-top:6px}
.wrap{max-width:1280px;margin:0 auto;padding:18px 16px 60px}
.sec-h{display:flex;align-items:baseline;gap:12px;margin:22px 0 10px;flex-wrap:wrap}
.sec-h h2{margin:0;font-size:17px}
.hint{color:var(--muted);font-size:12px}
/* stats */
.stats{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:10px}
.stat{background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:12px 14px}
.stat b{display:block;font-size:22px;font-variant-numeric:tabular-nums}
.stat span{color:var(--muted);font-size:12px}
.stat .pbar{height:5px;background:var(--soft);border-radius:3px;margin-top:8px;overflow:hidden}
.stat .pbar i{display:block;height:100%;background:var(--green)}
/* tiles */
.tiles{display:grid;grid-template-columns:repeat(auto-fill,minmax(150px,1fr));gap:10px}
.tile{background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:10px 11px 9px;display:flex;flex-direction:column;gap:6px;
  border-top:3px solid var(--pc);transition:transform .1s,box-shadow .1s}
.tile:hover{transform:translateY(-2px);box-shadow:0 4px 14px rgba(0,0,0,.08)}
.tile .hd{display:flex;align-items:center;gap:7px}
.tile .ic{font-size:20px;line-height:1}
.tile .lt{font-weight:700;font-size:13px;color:var(--pc)}
.tile .nm{font-size:12.5px;line-height:1.3;min-height:33px}
.tile .num{font-size:11px;color:var(--muted);display:flex;justify-content:space-between}
.dots{display:flex;flex-wrap:wrap;gap:3px}
.dot{width:9px;height:9px;border-radius:50%;background:var(--soft);border:1px solid var(--gray);display:inline-block}
.dot.w{background:color-mix(in srgb,var(--blue) 35%,transparent);border-color:var(--blue)}
.dot.d{background:var(--green);border-color:var(--green)}
.dot.p0{border-width:2px;border-color:var(--ink)}
.legend{display:flex;gap:16px;flex-wrap:wrap;color:var(--muted);font-size:12px;margin:12px 0 4px;align-items:center}
.legend span{display:flex;align-items:center;gap:5px}
.two{display:grid;grid-template-columns:1fr 1fr;gap:12px;margin-top:18px}
@media (max-width:760px){.two{grid-template-columns:1fr}}
.panel{background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:12px 14px}
.panel h3{margin:0 0 10px;font-size:14px}
.chips{display:flex;flex-wrap:wrap;gap:6px}
.chipa{border:1px solid var(--line);border-radius:999px;padding:5px 10px;font-size:12px;background:var(--soft)}
.chipa:hover{border-color:var(--acc);color:var(--acc)}
.phrow{display:grid;grid-template-columns:86px 1fr 52px;gap:8px;align-items:center;margin:7px 0;font-size:12px}
.phrow .pbar{height:8px;background:var(--soft);border-radius:4px;overflow:hidden;display:flex}
.phrow .pbar i{height:100%}
/* map */
.lane{display:grid;grid-template-columns:150px minmax(0,1fr);gap:12px;margin:14px 0;position:relative}
.lane-h{border-left:4px solid var(--pc);padding:6px 10px}
.lane-h b{display:block;font-size:14px}
.lane-h span{color:var(--muted);font-size:12px;line-height:1.4;display:block;margin-top:4px}
.lane-b{display:flex;flex-wrap:wrap;gap:10px;align-items:stretch}
.mbox{background:var(--paper);border:1px solid var(--line);border-radius:10px;padding:8px 10px;min-width:180px;flex:1 1 200px;max-width:340px;position:relative}
.mbox .mh{display:flex;gap:6px;align-items:center;font-size:12.5px;margin-bottom:7px}
.mbox .mh .lt{font-weight:700;color:var(--pc)}
.mbox .mh .pr{margin-left:auto;color:var(--muted);font-size:11px}
.ichips{display:flex;flex-wrap:wrap;gap:4px}
.ic-chip{font:600 11px var(--mono);padding:3px 6px;border-radius:5px;border:1px solid var(--gray);background:var(--soft);color:var(--muted)}
.ic-chip.w{border-color:var(--blue);color:var(--blue);background:color-mix(in srgb,var(--blue) 12%,transparent)}
.ic-chip.d{border-color:var(--green);background:var(--green);color:#fff}
.ic-chip.p0{box-shadow:inset 0 -2px 0 var(--ink)}
.chip-l{width:22px;height:13px;border-radius:4px;border:1px solid var(--gray);background:var(--soft);display:inline-block}
.chip-l.w{border-color:var(--blue);background:color-mix(in srgb,var(--blue) 12%,transparent)}
.chip-l.d{border-color:var(--green);background:var(--green)}
.chip-l.p0{box-shadow:inset 0 -2px 0 var(--ink)}
.down{text-align:center;color:var(--muted);font-size:16px;margin:-6px 0}
@media (max-width:760px){.lane{grid-template-columns:1fr}}
/* browse */
.browse{display:grid;grid-template-columns:260px minmax(0,1fr);max-width:1380px;margin:0 auto;min-height:calc(100vh - 52px)}
.side{border-right:1px solid var(--line);padding:14px;position:sticky;top:52px;align-self:start;max-height:calc(100vh - 52px);overflow:auto}
.side input[type=search]{width:100%;font:inherit;padding:8px 10px;border:1px solid var(--line);border-radius:8px;background:var(--paper);color:var(--ink)}
.grp{margin-top:14px}
.gl{font-size:11px;color:var(--muted);text-transform:uppercase;letter-spacing:.06em;margin-bottom:6px}
.mrow{display:flex;align-items:center;gap:7px;width:100%;padding:5px 7px;border-radius:7px;cursor:pointer;font-size:12.5px;border:0;background:none;color:var(--ink);text-align:left}
.mrow:hover{background:var(--soft)}
.mrow.on{background:var(--ink);color:var(--bg)}
.mrow .l{font-weight:700;width:16px;color:var(--pc)}
.mrow.on .l{color:inherit}
.mrow .c{margin-left:auto;font-size:11px;color:var(--muted);font-variant-numeric:tabular-nums}
.mrow.on .c{color:inherit;opacity:.8}
.pills{display:flex;flex-wrap:wrap;gap:5px}
.pill{font:inherit;font-size:12px;padding:4px 10px;border-radius:999px;border:1px solid var(--line);background:var(--paper);color:var(--muted);cursor:pointer}
.pill.on{background:var(--acc);border-color:var(--acc);color:#fff}
.ck{display:flex;gap:6px;align-items:center;font-size:12.5px;cursor:pointer}
.cards-wrap{padding:16px 18px 60px}
.cards-h{display:flex;align-items:baseline;gap:10px;margin-bottom:12px}
.cards-h h2{margin:0;font-size:17px}
.cards{display:grid;grid-template-columns:repeat(auto-fill,minmax(270px,1fr));gap:12px}
.card{background:var(--paper);border:1px solid var(--line);border-radius:12px;padding:12px 13px;display:flex;flex-direction:column;gap:7px;border-left:4px solid var(--pc)}
.card.done{opacity:.72}
.card.done .ct{text-decoration:line-through;text-decoration-color:var(--green)}
.card .row{display:flex;align-items:center;gap:6px}
.idb{font:700 12px var(--mono);color:var(--pc)}
.pri{font-size:10.5px;font-weight:700;padding:2px 6px;border-radius:5px;background:var(--soft);color:var(--muted)}
.pri.P0{background:var(--red);color:#fff}
.pri.P1{background:color-mix(in srgb,var(--orange) 22%,transparent);color:var(--orange)}
.done-ck{margin-left:auto;display:flex;align-items:center;gap:4px;font-size:11.5px;color:var(--muted);cursor:pointer;user-select:none}
.done-ck input{width:17px;height:17px;accent-color:var(--green);cursor:pointer}
.ct{font-size:14.5px;font-weight:650;line-height:1.35}
.cc{font-size:12.5px;color:var(--ink);opacity:.85;line-height:1.5;display:-webkit-box;-webkit-line-clamp:3;-webkit-box-orient:vertical;overflow:hidden}
.cc code{font:11.5px var(--mono);background:var(--soft);padding:0 3px;border-radius:3px}
.cw{font-size:12px;color:var(--muted)}
.cf{display:flex;align-items:center;gap:8px;margin-top:auto;padding-top:6px;border-top:1px dashed var(--line);font-size:12px;flex-wrap:wrap}
.st-w{color:var(--blue)} .st-n{color:var(--muted)}
.bm{font-size:11px;color:var(--orange)}
.open{margin-left:auto;color:var(--acc);font-weight:600}
.open:hover{text-decoration:underline}
.empty{color:var(--muted);padding:40px;text-align:center}
.card:target{outline:2px solid var(--acc)}
@media (max-width:760px){.browse{grid-template-columns:1fr}.side{position:static;max-height:none;border-right:0;border-bottom:1px solid var(--line)}}
"""

JS = r"""
(function(){
const D=JSON.parse(document.getElementById('data').textContent);
const DK='rtos_done_v1', RK='eai_notes_read_v1', TK='eai_notes_theme';
const load=k=>{try{return new Set(JSON.parse(localStorage.getItem(k)||'[]'))}catch(e){return new Set()}};
const save=(k,s)=>{try{localStorage.setItem(k,JSON.stringify([...s]))}catch(e){}};
const isDone=it=>load(DK).has(it.id)||(it.nid&&load(RK).has(it.nid));
const setDone=(it,on)=>{const d=load(DK),r=load(RK);
  if(on){d.add(it.id);if(it.nid)r.add(it.nid);}else{d.delete(it.id);if(it.nid)r.delete(it.nid);} save(DK,d);save(RK,r);};
try{const t=localStorage.getItem(TK); if(t)document.documentElement.dataset.theme=t;}catch(e){}
document.getElementById('theme').onclick=()=>{const d=document.documentElement;
  const cur=d.dataset.theme||(matchMedia('(prefers-color-scheme: dark)').matches?'dark':'light');
  const nx=cur==='dark'?'light':'dark'; d.dataset.theme=nx; try{localStorage.setItem(TK,nx)}catch(e){}};
const PC=['var(--p1)','var(--p2)','var(--p3)','var(--p4)','var(--p5)'];
const modOf={}; D.mods.forEach(m=>modOf[m.k]=m);
const pc=k=>PC[(modOf[k]||{ph:4}).ph];
const esc=s=>String(s).replace(/[&<>"]/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;'}[c]));
const st=it=>isDone(it)?'d':(it.href?'w':'');
const link=it=>it.href||('browse.html#m='+it.m+'&i='+it.id);
const page=document.body.dataset.page;

if(page==='dash'){
  const all=D.items, w=all.filter(i=>i.href), dn=all.filter(isDone), p0=all.filter(i=>i.p==='P0');
  const p0d=p0.filter(isDone), mins=w.reduce((a,i)=>a+i.min,0);
  const stat=(v,l,pct)=>`<div class="stat"><b>${v}</b><span>${l}</span>${pct!=null?`<div class="pbar"><i style="width:${pct}%"></i></div>`:''}</div>`;
  document.getElementById('stats').innerHTML=
    stat(`${w.length}<small style="font-size:13px;color:var(--muted)"> / ${all.length}</small>`,'노트 작성',Math.round(w.length/all.length*100))+
    stat(`${dn.length}<small style="font-size:13px;color:var(--muted)"> / ${all.length}</small>`,'완료 체크',Math.round(dn.length/all.length*100))+
    stat(`${p0d.length}<small style="font-size:13px;color:var(--muted)"> / ${p0.length}</small>`,'P0 완료',Math.round(p0d.length/p0.length*100))+
    stat(`${Math.round(mins/60)}시간`,'작성된 노트 읽기 시간')+
    stat(`${D.mods.filter(m=>all.filter(i=>i.m===m.k).every(i=>i.href)).length} / ${D.mods.length}`,'작성 끝난 모듈');
  document.getElementById('tiles').innerHTML=D.mods.map(m=>{
    const its=all.filter(i=>i.m===m.k), ww=its.filter(i=>i.href).length, dd=its.filter(isDone).length;
    return `<a class="tile" style="--pc:${pc(m.k)}" href="browse.html#m=${m.k}" title="${esc(m.blurb)}">
      <div class="hd"><span class="ic">${m.icon}</span><span class="lt">${m.k}</span></div>
      <div class="nm">${esc(m.ko)}</div>
      <div class="dots">${its.map(i=>`<i class="dot ${st(i)} ${i.p==='P0'?'p0':''}" title="${i.id} ${esc(i.t)}"></i>`).join('')}</div>
      <div class="num"><span>노트 ${ww}/${its.length}</span><span>완료 ${dd}</span></div></a>`}).join('');
  const nxt=all.filter(i=>i.href&&!isDone(i)).slice(0,10);
  document.getElementById('next').innerHTML=nxt.length?nxt.map(i=>`<a class="chipa" href="${i.href}">${i.id} · ${esc(i.t)}</a>`).join(''):'<span class="hint">작성된 노트를 모두 완료했다</span>';
  document.getElementById('phases').innerHTML=D.phases.map((p,ix)=>{
    const its=all.filter(i=>p.mods.includes(i.m)), n=its.length, ww=its.filter(i=>i.href&&!isDone(i)).length, dd=its.filter(isDone).length;
    return `<div class="phrow"><span><b>${p.n}</b> ${p.ko}</span>
      <div class="pbar"><i style="width:${dd/n*100}%;background:var(--green)"></i><i style="width:${ww/n*100}%;background:color-mix(in srgb,var(--blue) 45%,transparent)"></i></div>
      <span class="hint">${dd+ww}/${n}</span></div>`}).join('');
}

if(page==='map'){
  document.getElementById('map').innerHTML=D.phases.map((p,ix)=>`
    ${ix?'<div class="down">↓</div>':''}
    <div class="lane" style="--pc:${PC[ix]}">
      <div class="lane-h"><b>${p.n} · ${p.ko}</b><span>${esc(p.goal)}</span></div>
      <div class="lane-b">${p.mods.map(k=>{const m=modOf[k], its=D.items.filter(i=>i.m===k);
        const ww=its.filter(i=>i.href).length;
        return `<div class="mbox" style="--pc:${PC[ix]}"><div class="mh"><span>${m.icon}</span><span class="lt">${k}</span><a href="browse.html#m=${k}">${esc(m.ko)}</a><span class="pr">${ww}/${its.length}</span></div>
          <div class="ichips">${its.map(i=>`<a class="ic-chip ${st(i)} ${i.p==='P0'?'p0':''}" href="${link(i)}" title="${i.id} ${esc(i.t)} · ${i.p}${i.bm?' · 🔁 '+esc(i.bm):''}">${i.id}${i.bm?'🔁':''}</a>`).join('')}</div></div>`}).join('')}
      </div></div>`).join('');
}

if(page==='browse'){
  const F={m:'',p:new Set(),s:'',don:new Set(),bm:false,q:''};
  const hs=new URLSearchParams(location.hash.slice(1));
  if(hs.get('m'))F.m=hs.get('m'); if(hs.get('p'))hs.get('p').split(',').forEach(x=>F.p.add(x));
  if(hs.get('s'))F.s=hs.get('s'); const focus=hs.get('i');
  const q=document.getElementById('q');
  const pills=(el,opts,set,single)=>{el.innerHTML=opts.map(([v,l])=>`<button class="pill" data-v="${v}">${l}</button>`).join('');
    el.querySelectorAll('.pill').forEach(b=>b.onclick=()=>{const v=b.dataset.v;
      if(single){F.s=F.s===v?'':v;} else {set.has(v)?set.delete(v):set.add(v);} draw();});};
  pills(document.getElementById('f-pri'),[['P0','P0 면접 필수'],['P1','P1'],['P2','P2']],F.p);
  pills(document.getElementById('f-st'),[['w','노트 있음'],['n','예정'],['d','완료'],['u','미완료']],null,true);
  pills(document.getElementById('f-don'),[['✅','✅ 강함'],['🟡','🟡 인접'],['❌','❌ 새로']],F.don);
  document.getElementById('f-bm').onchange=e=>{F.bm=e.target.checked;draw();};
  q.oninput=()=>{F.q=q.value.trim().toLowerCase();draw();};
  document.getElementById('reset').onclick=()=>{F.m='';F.p.clear();F.s='';F.don.clear();F.bm=false;F.q='';q.value='';document.getElementById('f-bm').checked=false;draw();};
  const match=it=>{
    if(F.m&&it.m!==F.m)return false; if(F.p.size&&!F.p.has(it.p))return false;
    if(F.don.size&&![...F.don].some(d=>it.don.includes(d)))return false; if(F.bm&&!it.bm)return false;
    const dn=isDone(it);
    if(F.s==='w'&&!it.href)return false; if(F.s==='n'&&it.href)return false;
    if(F.s==='d'&&!dn)return false; if(F.s==='u'&&dn)return false;
    if(F.q){const h=(it.id+' '+it.t+' '+it.c+' '+it.w).toLowerCase(); if(!h.includes(F.q))return false;}
    return true;};
  function draw(){
    // 트랙 목록
    const base=it=>{const m=F.m;F.m='';const r=match(it);F.m=m;return r;};
    document.getElementById('f-mod').innerHTML=
      `<button class="mrow ${F.m?'':'on'}" data-k=""><span class="l">∗</span>전체<span class="c">${D.items.filter(base).length}</span></button>`+
      D.mods.map(m=>{const its=D.items.filter(i=>i.m===m.k);const sh=its.filter(base).length;const dd=its.filter(isDone).length;
        return `<button class="mrow ${F.m===m.k?'on':''}" style="--pc:${pc(m.k)}" data-k="${m.k}"><span class="l">${m.k}</span>${m.icon} ${esc(m.ko)}<span class="c">${dd}/${its.length}</span></button>`}).join('');
    document.querySelectorAll('.mrow').forEach(b=>b.onclick=()=>{F.m=b.dataset.k;draw();scrollTo(0,0);});
    document.querySelectorAll('#f-pri .pill').forEach(b=>b.classList.toggle('on',F.p.has(b.dataset.v)));
    document.querySelectorAll('#f-st .pill').forEach(b=>b.classList.toggle('on',F.s===b.dataset.v));
    document.querySelectorAll('#f-don .pill').forEach(b=>b.classList.toggle('on',F.don.has(b.dataset.v)));
    const list=D.items.filter(match);
    const m=modOf[F.m];
    document.getElementById('ttl').textContent=m?`${m.icon} ${F.m}. ${m.ko}`:'전체 트랙';
    document.getElementById('cnt').textContent=`${list.length}개 · 완료 ${list.filter(isDone).length}`;
    const h=new URLSearchParams(); if(F.m)h.set('m',F.m); if(F.p.size)h.set('p',[...F.p].join(',')); if(F.s)h.set('s',F.s);
    history.replaceState(null,'','#'+h.toString());
    const box=document.getElementById('cards');
    if(!list.length){box.innerHTML='<div class="empty">조건에 맞는 항목이 없다</div>';return;}
    box.innerHTML=list.map(it=>{const dn=isDone(it);
      return `<article class="card ${dn?'done':''}" id="c-${it.id}" style="--pc:${pc(it.m)}">
        <div class="row"><span class="idb">${it.id}</span><span class="pri ${it.p}">${it.p}</span><span title="Don 출발점">${esc(it.don)}</span>
          <label class="done-ck"><input type="checkbox" data-id="${it.id}" ${dn?'checked':''}>완료</label></div>
        <div class="ct">${esc(it.t)}</div>
        <div class="cc">${it.c}</div>
        <div class="cw">왜: ${esc(it.w)}</div>
        <div class="cf">${it.href?`<span class="st-w">📝 노트 · ${it.min}분</span>`:'<span class="st-n">⏳ 예정</span>'}
          ${it.bm?`<span class="bm">🔁 ${esc(it.bm)}</span>`:''}
          ${it.href?`<a class="open" href="${it.href}">열기 →</a>`:''}</div></article>`}).join('');
    box.querySelectorAll('input[type=checkbox]').forEach(cb=>cb.onchange=()=>{
      const it=D.items.find(i=>i.id===cb.dataset.id); setDone(it,cb.checked);
      cb.closest('.card').classList.toggle('done',cb.checked); draw();});
  }
  draw();
  if(focus){const el=document.getElementById('c-'+focus); if(el){el.scrollIntoView({block:'center'}); el.style.outline='2px solid var(--acc)';}}
  addEventListener('keydown',e=>{if(e.key==='/'&&document.activeElement!==q){e.preventDefault();q.focus();}});
}
})();
"""
