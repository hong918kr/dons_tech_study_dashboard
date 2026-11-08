# Track item-list brief (for curriculum writers)

Workspace: /Users/donh/workspace/dons_tech_study_dashboard/dons_study_notes_from_resource/embedded-study-note
Stage/track metadata: curriculum/stages.json (track ids, Korean/English names, env enum).

## Who the learner is
Don — embedded/RF integration engineer + ex-SSD firmware engineer (C, NAND/FTL, NVMe), UC Berkeley math.
Pivoting toward AI accelerator / robotics / silicon-validation firmware roles. Learns by DOING:
every item is one hands-on, interview-style coding problem (or design exercise) that is
explained Basic → Mid → Advanced. Korean explanations, English problem statement.

## Output: one JSON file per track
Path: curriculum/tracks/<TRACK_ID>.json  (UTF-8, 2-space indent, valid JSON — validate with
`python3 -c "import json;json.load(open(PATH))"` before finishing).

{
  "id": "C",
  "intro": "트랙 소개 2–3문장 (한국어). 왜 이 트랙이 임베디드/면접에서 중요한지.",
  "goals": ["트랙 학습 목표 3–6개 (한국어, 한 줄씩)"],
  "prereq": ["다른 트랙 id, 없으면 빈 배열"],
  "refs": [{"title": "책/공식 문서/스펙/강의 이름", "url": "https://... (확실한 것만, 모르면 url 생략)"}],
  "items": [
    {
      "id": "C-B01",
      "level": "B",
      "title": "비트 연산 기초: Set / Clear / Toggle / Test",
      "goal": "학습목표 한 줄 (한국어)",
      "task": "실제로 풀 문제/과제 1–2문장 (한국어, 구체적: 무엇을 구현하고 무엇으로 검증하는지)",
      "tags": ["bitops", "register"],
      "env": "mac",
      "env_note": ""
    }
  ]
}

## Item rules
- id = <TRACK>-<LEVEL><2-digit seq>, LEVEL ∈ B (Basic) / M (Mid) / A (Advanced), seq restarts per level:
  C-B01..C-B14, C-M01..C-M14, C-A01..C-A10. Order items inside each level as a learning path
  (each builds on the previous).
- title: Korean + the English technical term where natural, ≤ 45 chars-ish, like the examples:
  "C-B01 비트연산 기초: Set Clear Toggle Test", "C-B02 고정 폭 정수와 정수 승격의 함정".
- Each item must be a concrete, testable exercise — "implement X and verify with Y", or for
  design-heavy topics "design X, write the key code path, and justify trade-offs". No vague
  "learn about X" items. Prefer problems that actually show up in embedded/firmware/systems
  interviews (Anduril, Apple, NVIDIA, Tesla, Rivian, Qualcomm, SpaceX, Google hardware, etc.).
- Coverage: be thorough — cover fundamentals carefully (Don wants to re-check basics he
  already knows by doing), then the mid-level mechanics, then advanced/real-world. Avoid
  duplicating items that clearly belong to another track (e.g. RTOS scheduling belongs in RTOS,
  not OS; ring buffer belongs in DS); cross-reference with a tag like "see:RTOS" if useful.
- env: default "mac" (Apple Silicon M2: clang C/C++, python3, can host-simulate registers,
  mock peripherals, model protocols in software). Choose a non-mac value ONLY when the core
  learning truly can't be done on the Mac:
    linux (Linux-only API/kernel: epoll, kernel modules, device tree, /sys, perf),
    qemu (needs cross toolchain + QEMU emulation, e.g. real Cortex-M vector table boot),
    board (real MCU/SBC), fpga (HDL sim/FPGA), gpu (CUDA/NVIDIA), lab (scope/LA/RF gear/cells).
  Prefer designing items so they are host-verifiable (mock register maps, simulated buses,
  software models), and say in env_note what external setup is needed when not "mac".
- Item counts per track (see your assignment): aim for the given range; quality > count.

## Done = 
Each assigned track file written, JSON validates, ids unique and sequential, every item has all
fields. Report back: track id → item counts per level, and any judgement calls (≤10 lines).
Do NOT build sites, write lessons, or touch other files.
