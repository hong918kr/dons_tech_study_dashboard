"""notes.md → HTML 변환 공용 로직."""
from __future__ import annotations

import re

import markdown as md

_TASK_OPEN_RE = re.compile(r"<li>\s*\[\s*\]\s*", flags=re.IGNORECASE)
_TASK_DONE_RE = re.compile(r"<li>\s*\[\s*[xX]\s*\]\s*")


def convert_task_lists(html_text: str) -> str:
    html_text = _TASK_OPEN_RE.sub(
        '<li class="task"><input type="checkbox" disabled> ', html_text
    )
    html_text = _TASK_DONE_RE.sub(
        '<li class="task"><input type="checkbox" checked disabled> ', html_text
    )
    return html_text


def render_notes_md(md_text: str) -> str:
    body = md.markdown(
        md_text,
        extensions=["tables", "fenced_code", "attr_list", "toc"],
        output_format="html",
    )
    return convert_task_lists(body)
