#!/usr/bin/env python3
"""OSTEP PDF → 챕터별 영문 원문 markdown (book-md/) + 원문 텍스트 (.work/chapters/).

  python3 extract_book.py

pdftotext -layout 결과를 페이지 단위로 자르고, 머리말/꼬리말 제거, 스몰캡스
("T HE C RUX" → "THE CRUX") 복원, 섹션 제목·코드·TIP/ASIDE 박스·그림 캡션을
markdown 으로 바꾼다. 그림 자체는 텍스트로 깨질 수 있으므로 각 챕터 상단에
PDF 원본 페이지 링크(#page=N)를 단다. 생성 파일은 손으로 고치지 말 것.
"""
import re
import subprocess
from collections import Counter
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PDF = ROOT / "Operating Systems - Three Easy Pieces.pdf"
OUT = ROOT / "book-md"
RAW = ROOT / ".work" / "chapters"

# (id, part, title, first PDF page)  — 다음 항목의 시작 페이지 - 1 이 끝 페이지
CHAPTERS = [
    ("01", "Intro", "A Dialogue on the Book", 22),
    ("02", "Intro", "Introduction to Operating Systems", 24),
    ("03", "Virtualization", "A Dialogue on Virtualization", 44),
    ("04", "Virtualization", "The Abstraction: The Process", 46),
    ("05", "Virtualization", "Interlude: Process API", 58),
    ("06", "Virtualization", "Mechanism: Limited Direct Execution", 69),
    ("07", "Virtualization", "Scheduling: Introduction", 83),
    ("08", "Virtualization", "Scheduling: The Multi-Level Feedback Queue", 95),
    ("09", "Virtualization", "Scheduling: Proportional Share", 107),
    ("10", "Virtualization", "Multiprocessor Scheduling (Advanced)", 117),
    ("11", "Virtualization", "Summary Dialogue on CPU Virtualization", 128),
    ("12", "Virtualization", "A Dialogue on Memory Virtualization", 130),
    ("13", "Virtualization", "The Abstraction: Address Spaces", 132),
    ("14", "Virtualization", "Interlude: Memory API", 141),
    ("15", "Virtualization", "Mechanism: Address Translation", 152),
    ("16", "Virtualization", "Segmentation", 166),
    ("17", "Virtualization", "Free-Space Management", 178),
    ("18", "Virtualization", "Paging: Introduction", 195),
    ("19", "Virtualization", "Paging: Faster Translations (TLBs)", 209),
    ("20", "Virtualization", "Paging: Smaller Tables", 226),
    ("21", "Virtualization", "Beyond Physical Memory: Mechanisms", 241),
    ("22", "Virtualization", "Beyond Physical Memory: Policies", 250),
    ("23", "Virtualization", "The VAX/VMS Virtual Memory System", 268),
    ("24", "Virtualization", "Summary Dialogue on Memory Virtualization", 277),
    ("25", "Concurrency", "A Dialogue on Concurrency", 282),
    ("26", "Concurrency", "Concurrency: An Introduction", 284),
    ("27", "Concurrency", "Interlude: Thread API", 299),
    ("28", "Concurrency", "Locks", 310),
    ("29", "Concurrency", "Lock-based Concurrent Data Structures", 333),
    ("30", "Concurrency", "Condition Variables", 346),
    ("31", "Concurrency", "Semaphores", 361),
    ("32", "Concurrency", "Common Concurrency Problems", 379),
    ("33", "Concurrency", "Event-based Concurrency (Advanced)", 394),
    ("34", "Concurrency", "Summary Dialogue on Concurrency", 404),
    ("35", "Persistence", "A Dialogue on Persistence", 408),
    ("36", "Persistence", "I/O Devices", 410),
    ("37", "Persistence", "Hard Disk Drives", 423),
    ("38", "Persistence", "Redundant Arrays of Inexpensive Disks (RAIDs)", 440),
    ("39", "Persistence", "Interlude: Files and Directories", 459),
    ("40", "Persistence", "File System Implementation", 478),
    ("41", "Persistence", "Locality and The Fast File System", 496),
    ("42", "Persistence", "Crash Consistency: FSCK and Journaling", 508),
    ("43", "Persistence", "Log-structured File Systems", 528),
    ("44", "Persistence", "Data Integrity and Protection", 543),
    ("45", "Persistence", "Summary Dialogue on Persistence", 556),
    ("46", "Distribution", "A Dialogue on Distribution", 558),
    ("47", "Distribution", "Distributed Systems", 560),
    ("48", "Distribution", "Sun's Network File System (NFS)", 575),
    ("49", "Distribution", "The Andrew File System (AFS)", 591),
    ("50", "Distribution", "Summary Dialogue on Distribution", 605),
    ("A", "Appendix", "A Dialogue on Virtual Machine Monitors", 607),
    ("B", "Appendix", "Virtual Machine Monitors", 608),
    ("C", "Appendix", "A Dialogue on Monitors", 621),
    ("D", "Appendix", "Monitors (Deprecated)", 622),
    ("E", "Appendix", "A Dialogue on Labs", 635),
    ("F", "Appendix", "Laboratory: Tutorial", 637),
    ("G", "Appendix", "Laboratory: Systems Projects", 649),
    ("H", "Appendix", "Laboratory: xv6 Projects", 653),
    ("I", "Appendix", "Flash-based SSDs", 656),
]
LAST_PAGE = 676

FOOTER = re.compile(
    r"(O\s?PERATING|S\s?YSTEMS|\[V\s?ERSION|WWW\.\s?OSTEP|T\s?HREE|E\s?ASY|P\s?IECES|ARPACI-DUSSEAU|^\s*c\s+20\d\d)")
SECTION = re.compile(r"^\s*([0-9A-I]{1,2}\.\d{1,2})\s+([A-Z][^.]*[^.,;])$")
CODE_LINE = re.compile(r"^\s*\d{1,3}(\s{2,}|$)")
SHELL = re.compile(r"^\s*(prompt>|\$ |#include|int main)")
BOX_START = re.compile(r"^(TIP|ASIDE|THE CRUX|CRUX|DESIGN TIP|KEY|HOW TO)")


def slugify(title):
    return re.sub(r"[^a-z0-9]+", "_", title.lower()).strip("_")[:48]


def fix_smallcaps(s):
    """'T HE C RUX OF THE P ROBLEM :' → 'THE CRUX OF THE PROBLEM:'

    스몰캡스는 '첫 글자 + 공백 + 나머지 대문자' 로 추출된다. 토큰 단위로
    '한 글자 대문자 토큰 + 대문자 토큰' 을 붙인다 (이미 붙인 토큰은 다시 안 붙임)."""
    toks, out, i = s.split(" "), [], 0
    while i < len(toks):
        t = toks[i]
        if len(t) == 1 and t.isupper() and i + 1 < len(toks) and re.fullmatch(r"[A-Z][A-Z’'\-]*[,:?.!)]*", toks[i + 1]):
            out.append(t + toks[i + 1]); i += 2
        elif t == "S" and out and out[-1].isupper():   # "CPU S" → "CPUs"
            out[-1] += "s"; i += 1
        else:
            out.append(t); i += 1
    s = " ".join(x for x in out if x != "")
    s = re.sub(r"\s+([:,.?)])", r"\1", s)
    s = re.sub(r"\(\s+", "(", s)
    return s


BODY_CAPS = {"U NIX": "UNIX", "L INUX": "Linux", "W INDOWS": "Windows", "M AC": "Mac", "POSIX": "POSIX"}


def fix_body(s):
    for k, v in BODY_CAPS.items():
        s = s.replace(k, v)
    return s


def is_capsline(s):
    letters = [c for c in s if c.isalpha()]
    return len(letters) >= 3 and sum(c.isupper() for c in letters) / len(letters) > 0.9


def clean_page(text, first):
    lines = text.splitlines()
    # 꼬리말(O PERATING / S YSTEMS / [V ERSION 0.91] ... ) 과 머리말 제거
    out = []
    nonblank = [i for i, l in enumerate(lines) if l.strip()]
    head_idx = nonblank[0] if nonblank else -1
    for i, l in enumerate(lines):
        st = l.strip()
        if i == head_idx and not first and (re.match(r"^\d+\s{3,}", st) or re.search(r"\s{3,}\d+$", st)):
            continue
        if is_capsline(st) and (re.match(r"^\d+\s{8,}\S", st) or re.search(r"\S\s{8,}\d+$", st)):
            continue   # 페이지 중간에 끼어든 머리말
        if FOOTER.search(st) and is_capsline(st):
            continue
        if re.fullmatch(r"\d{1,3}", st):
            continue
        out.append(l.rstrip())
    # 페이지별 본문 들여쓰기(최빈값)를 빼서 짝/홀 페이지 여백 차이 제거
    ind = Counter(len(l) - len(l.lstrip()) for l in out if len(l.strip()) > 40)
    base = min((k for k, v in ind.items() if v >= max(ind.values()) * 0.3), default=0) if ind else 0
    return [l[base:] if l[:base].strip() == "" else l.lstrip() for l in out]


def codeish(st):
    return bool(re.search(r"(;\s*(//.*)?$|[{}]\s*$|^\s*(//|/\*|\*/|#include|#define|struct |typedef |int |void |char |"
                          r"while \(|if \(|for \(|return\b|}))", st))


def prose(st):
    """코드 블록 안에서 산문(본문 문장)이 다시 시작됐는지."""
    return len(st) > 50 and not re.search(r"[;{}=]|//", st) and len(st.split()) >= 8


def tableish(l):
    return len(re.findall(r"\S\s{3,}\S", l.strip())) >= 2


def to_markdown(lines, cid, title, part, p0, p1):
    md = [f"# {cid}. {title}", "",
          f"> 원문: *Operating Systems: Three Easy Pieces* (v0.91) — Part **{part}**, "
          f"PDF p.{p0}–{p1} · [PDF 원본에서 보기 (그림 포함)](../{PDF.name.replace(' ', '%20')}#page={p0})", "",
          "> ⚠️ PDF 텍스트 자동 추출본. 그림·표는 깨질 수 있으니 그림이 나오면 PDF 링크로 확인.", ""]
    para, mode, buf = [], None, []   # mode: code / box / pre

    def join_para(lines_):
        s = ""
        for l in lines_:
            l = l.strip()
            if s.endswith("-") and l[:1].islower():
                s = s[:-1] + l
            else:
                s = (s + " " + l) if s else l
        return s

    def flush_buf():
        nonlocal mode
        if not buf:
            mode = None
            return
        if mode == "code":
            lang = "c" if any(re.search(r"[;{}]\s*(//.*)?$|#include", x) for x in buf) else "text"
            md.append("```" + lang)
            md.extend(buf)
            md.append("```")
        elif mode == "box":
            head = fix_smallcaps(buf[0].strip())
            body = join_para(buf[1:])
            # 박스 제목이 두 줄인 경우(대문자 줄 연속)
            md.append(f"> **{head}**")
            if body:
                md.append(">")
                md.append("> " + body)
        md.append("")
        buf.clear()
        mode = None

    blank_run = 0
    started = False
    for l in lines:
        st = l.strip()
        if not st:
            blank_run += 1
            if mode == "box" and len(buf) >= 2:
                flush_buf()
            if mode == "code" and blank_run >= 3:
                flush_buf()
            continue
        gap = blank_run
        blank_run = 0
        if not started:
            # 챕터 번호/제목 줄은 건너뜀
            if re.fullmatch(r"[0-9A-I]{1,2}", st) or fix_smallcaps(st).lower().startswith(title.lower()[:12]) or \
                    title.lower().startswith(st.lower()[:12]):
                continue
            started = True
        m = SECTION.match(l)
        if m and mode is None and len(st) < 90:
            if para:
                md.append(join_para(para)); para.clear()
            md.append("")
            md.append(f"## {m.group(1)} {fix_smallcaps(m.group(2).strip())}")
            md.append("")
            continue
        if re.match(r"^(Figure|Table)\s+[0-9A-I]+\.\d+:", st):
            if mode:
                flush_buf()
            if para:
                md.append(join_para(para)); md.append(""); para.clear()
            frag = []
            while md and (md[-1] == "" or (len(md[-1]) < 60 and not md[-1].startswith(("#", ">", "*", "```", "- "))
                                            and not md[-1].rstrip().endswith((".", ":")))):
                x = md.pop()
                if x:
                    frag.append(x)
            if frag:
                md.append("```text")
                md.extend(reversed(frag))
                md.append("```")
                md.append("")
            md.append(f"*{st}*")
            md.append("")
            continue
        if mode == "box":
            if is_capsline(st) and len(buf) == 1:
                buf[0] += " " + st
            else:
                buf.append(st)
            continue
        if is_capsline(st) and BOX_START.match(fix_smallcaps(st)) and gap >= 1:
            if para:
                md.append(join_para(para)); md.append(""); para.clear()
            if mode:
                flush_buf()
            mode = "box"
            buf.append(st)
            continue
        if CODE_LINE.match(l) or SHELL.match(st) or codeish(st) or tableish(l) or \
                (mode == "code" and not prose(st)):
            if mode != "code":
                if para:
                    md.append(join_para(para)); md.append(""); para.clear()
                if mode:
                    flush_buf()
                mode = "code"
            buf.append(l.rstrip())
            continue
        if mode:
            flush_buf()
        # 새 문단: 들여쓰기된 줄 또는 빈 줄 뒤
        indent = len(l) - len(l.lstrip())
        if st.startswith("•"):
            if para:
                md.append(join_para(para)); md.append(""); para.clear()
            para.append("- " + st.lstrip("• ").strip())
            continue
        if para and (indent >= 2 or gap >= 1) and not (para[0].startswith("- ") and gap == 0):
            md.append(join_para(para))
            md.append("")
            para.clear()
        if st.startswith("References") and len(st) < 15:
            md.append("## References")
            md.append("")
            continue
        if st.startswith("Homework") and len(st) < 40:
            md.append(f"## {st}")
            md.append("")
            continue
        para.append(st)
    if mode:
        flush_buf()
    if para:
        md.append(join_para(para))
    text = fix_body("\n".join(md))
    text = re.sub(r"\n{3,}", "\n\n", text)
    return text.strip() + "\n"


def main():
    raw = subprocess.run(["pdftotext", "-layout", str(PDF), "-"], capture_output=True, text=True, check=True).stdout
    pages = raw.split("\f")
    OUT.mkdir(exist_ok=True)
    RAW.mkdir(parents=True, exist_ok=True)
    for f in OUT.glob("*.md"):
        f.unlink()
    for i, (cid, part, title, p0) in enumerate(CHAPTERS):
        p1 = (CHAPTERS[i + 1][3] - 1) if i + 1 < len(CHAPTERS) else LAST_PAGE
        chunk = pages[p0 - 1:p1]
        (RAW / f"{cid}.txt").write_text("\n\f\n".join(chunk))
        lines = []
        for j, pg in enumerate(chunk):
            lines.extend(clean_page(pg, j == 0))
            lines.append("")
        md = to_markdown(lines, cid, title, part, p0, p1)
        name = f"C{cid.zfill(2)}_{slugify(title)}.md"
        (OUT / name).write_text(md)
        print(f"{name:60s} p{p0}-{p1} {len(md):7d} chars")


if __name__ == "__main__":
    main()
