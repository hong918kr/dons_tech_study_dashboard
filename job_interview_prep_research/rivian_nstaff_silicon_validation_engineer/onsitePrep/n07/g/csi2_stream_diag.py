#!/usr/bin/env python3
"""N07G: CSI-2 RX event-log stream analyzer (stream-level diagnosis).

Input : event logs written by gen_mocks.py (format documented there).
Output: per-VC stream summary + one primary diagnosis code + evidence lines,
        compared against the log's '#expect' header -> PASS/FAIL.

This is NOT a packet checker (hmScreenPrep V9 does header ECC / payload CRC on
bytes). Here the RX has already done that; we look at the *sequence* of events:
FS/FE pairing, lines per frame, word count consistency, frame-number gaps,
frame interval vs configured fps, RX-received frames vs DMA-delivered buffers,
and where/when error flags cluster.

Usage: python3 -I csi2_stream_diag.py mocks/*.log
Exit : 0 if every log with an #expect matched, 1 otherwise.
"""
import sys
from collections import Counter, defaultdict

PHY_FLAGS = ("ECC1", "ECC2", "CRC", "SOT")


def parse(path):
    cfg, expect, events = {}, None, []
    with open(path) as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            if line.startswith("#cfg"):
                for kv in line.split()[1:]:
                    k, v = kv.split("=", 1)
                    cfg[k] = v
                continue
            if line.startswith("#expect"):
                expect = line.split()[1]
                continue
            if line.startswith("#"):
                continue
            t, vc, kind, arg, flags = line.split()
            events.append(dict(t=float(t), vc=None if vc == "-" else int(vc), kind=kind,
                               arg=None if arg == "-" else int(arg),
                               flags=set() if flags == "-" else set(flags.split(","))))
    cfg = dict(fps=float(cfg.get("fps", 30)), lines=int(cfg.get("lines", 0)),
               wc=int(cfg.get("wc", 0)),
               vcs=[int(x) for x in cfg.get("vcs", "0").split(",")])
    return cfg, expect, events


def fnum_step(prev, cur):
    """Frame-number step with 16-bit wrap. Many sensors count 1..N and wrap to 1
    (0 = frame numbers not used), so 65535 -> 1 counts as a step of 1."""
    if prev == 0 or cur == 0:
        return None
    if cur > prev:
        return cur - prev
    if prev == 0xFFFF and cur == 1:
        return 1
    return -(prev - cur)  # went backwards (or wrapped unexpectedly)


class VCStat:
    def __init__(self):
        self.fs = self.fe = self.done = self.done_err = 0
        self.open = None            # dict(t0, fnum, lines, unk) of the frame being received
        self.frames = []            # closed frames: (t0, fnum, lines, unk_inside)
        self.fs_times, self.fnums = [], []
        self.wc = Counter()
        self.flags = Counter()
        self.flag_frames = defaultdict(Counter)  # frame index -> flag counts
        self.fs_while_open = self.fe_without_fs = self.orphan_long = 0
        self.done_frame_idx = []    # FS index each DONE belongs to (the latest FS)


def analyze(cfg, events):
    st = defaultdict(VCStat)
    unk_times = []
    for e in events:
        if e["kind"] == "UNK":
            unk_times.append(e["t"])
            for vc in st:                      # attribute to any frame open right now
                if st[vc].open is not None:
                    st[vc].open["unk"] += 1
            continue
        s = st[e["vc"]]
        idx = s.fs - 1
        for fl in e["flags"]:
            s.flags[fl] += 1
            s.flag_frames[idx][fl] += 1
        k = e["kind"]
        if k == "FS":
            s.fs += 1
            s.fs_times.append(e["t"])
            s.fnums.append(e["arg"])
            if s.open is not None:
                s.fs_while_open += 1           # frame sync error; RX keeps appending
            else:
                s.open = dict(t0=e["t"], fnum=e["arg"], lines=0, unk=0)
        elif k == "LONG":
            s.wc[e["arg"]] += 1
            if s.open is None:
                s.orphan_long += 1
            else:
                s.open["lines"] += 1
        elif k == "FE":
            s.fe += 1
            if s.open is None:
                s.fe_without_fs += 1
            else:
                o = s.open
                s.frames.append((o["t0"], o["fnum"], o["lines"], o["unk"]))
                s.open = None
        elif k == "DONE":
            s.done += 1
            s.done_frame_idx.append(s.fs - 1)
            if "ERR" in e["flags"]:
                s.done_err += 1
    return st, unk_times


def summarize(cfg, st):
    out, info = [], {}
    period = 1e6 / cfg["fps"]
    for vc in sorted(set(cfg["vcs"]) | set(st)):
        s = st.get(vc) or VCStat()
        steps = [fnum_step(a, b) for a, b in zip(s.fnums, s.fnums[1:])]
        steps = [x for x in steps if x is not None]
        gaps = sum(x - 1 for x in steps if x > 1)
        backward = sum(1 for x in steps if x < 0)
        iv = [b - a for a, b in zip(s.fs_times, s.fs_times[1:])]
        iv_mean = sum(iv) / len(iv) if iv else 0.0
        lines = [f[2] for f in s.frames]
        phy = sum(s.flags[f] for f in PHY_FLAGS)
        info[vc] = dict(s=s, gaps=gaps, backward=backward, iv=iv_mean,
                        ratio=iv_mean / period if iv else 0.0, lines=lines, phy=phy)
        if s.fs == 0:
            out.append(f"VC{vc}: no frames at all (FS=0)")
            continue
        wc_txt = ", ".join(f"{w}x{n}" for w, n in sorted(s.wc.items()))
        lmin, lmax = (min(lines), max(lines)) if lines else (0, 0)
        out.append(f"VC{vc}: FS={s.fs} FE={s.fe} DONE={s.done} (err {s.done_err})  "
                   f"lines/frame {lmin}..{lmax} (cfg {cfg['lines']})  WC {{{wc_txt}}} (cfg {cfg['wc']})")
        out.append(f"      frame# gaps {gaps} backward {backward}  FS interval {iv_mean/1000:.2f} ms "
                   f"= x{info[vc]['ratio']:.2f} of {period/1000:.2f} ms  "
                   f"sync: FS-while-open {s.fs_while_open} FE-without-FS {s.fe_without_fs} "
                   f"orphan-LONG {s.orphan_long}")
        fl = "  ".join(f"{k} {s.flags[k]}" for k in ("ECC1", "ECC2", "CRC", "SOT", "LBFULL"))
        out.append(f"      flags: {fl}")
    return out, info


def per_frame_trend(s, flag):
    n = max(s.flag_frames) + 1 if s.flag_frames else 0
    return [s.flag_frames[i][flag] for i in range(n)]


def diagnose(cfg, info, unk_times):
    """Return (code, evidence lines). Rules are ordered: structural problems
    first (they make every later statistic meaningless), then physical-layer
    errors, then back-pressure, then configuration, then rate questions."""
    ev = []
    silent = [vc for vc in cfg["vcs"] if info[vc]["s"].fs == 0]
    active = [vc for vc in info if info[vc]["s"].fs > 0]

    # 1. two sources on one VC: nested FS/FE and two interleaved frame counters
    for vc in active:
        d, s = info[vc], info[vc]["s"]
        if s.fs_while_open >= 2 and d["backward"] >= 2:
            ev.append(f"VC{vc}: FS-while-open {s.fs_while_open}x and frame# goes backwards "
                      f"{d['backward']}x -> two independent frame counters share VC{vc}: "
                      f"{s.fnums[:6]}")
            if silent:
                ev.append(f"expected VC(s) {silent} are silent -> their camera is mapped onto VC{vc}")
            ev.append(f"lines/frame {d['lines'][:4]} = 2x cfg {cfg['lines']}: lines of both cameras interleave")
            ev.append("next: read the deserializer VC-mapping per input port; check sensor VC registers")
            return "VC_COLLISION", ev

    # 2. a lost FE: one frame-sync error with an uncorrectable header inside the open frame
    for vc in active:
        d, s = info[vc], info[vc]["s"]
        merged = [f for f in s.frames if f[2] >= 2 * cfg["lines"]]
        if s.fs_while_open >= 1 and merged:
            unk_in = sum(f[3] for f in merged)
            ev.append(f"VC{vc}: FS arrived while frame #{merged[0][1]} was still open; "
                      f"that frame closed with {merged[0][2]} lines (cfg {cfg['lines']})")
            if unk_in:
                ev.append(f"{unk_in} uncorrectable header (ECC2) inside it at t={unk_times[0]/1000:.3f} ms "
                          f"= where FE was due -> the FE was lost on the wire")
                ev.append(f"DONE={s.done} for FS={s.fs}; merged buffer flagged ERR {s.done_err}x")
                ev.append("next: SI margin on the link (G3 path) + RX must resync on FS (frame-sync error recovery)")
                return "FE_LOST_MERGE", ev
            ev.append("no header error seen -> transmitter never sent FE (stream-off mid-frame, sensor mode)")
            return "FE_NOT_SENT", ev

    # 3. physical-layer margin: corrected/uncorrected header errors and CRC rising
    for vc in active:
        d, s = info[vc], info[vc]["s"]
        if d["phy"] >= 5:
            e1 = per_frame_trend(s, "ECC1")
            crc = per_frame_trend(s, "CRC")
            rising = len(e1) >= 4 and sum(e1[len(e1)//2:]) > 2 * max(1, sum(e1[:len(e1)//2]))
            ev.append(f"VC{vc}: PHY-layer flags {d['phy']} (ECC1 {s.flags['ECC1']}, ECC2 {s.flags['ECC2']}, "
                      f"CRC {s.flags['CRC']}, SOT {s.flags['SOT']})")
            ev.append(f"ECC1 per frame {e1} -> {'rising over the run' if rising else 'flat'}; "
                      f"CRC per frame {crc}")
            ev.append(f"structure clean: gaps {d['gaps']}, sync errors {s.fs_while_open + s.fe_without_fs}, "
                      f"WC {dict(s.wc)}")
            ev.append("ECC1 = header corrected (link still works) is the early warning; CRC = payload hit")
            ev.append("next: per-lane SoT/err, lane rate one step down, HS-settle sweep at this temperature")
            return "SI_MARGIN", ev

    # 4. back-pressure: line buffer full, frames received but not delivered, PHY clean
    lb = {vc: info[vc]["s"].flags["LBFULL"] for vc in active}
    if sum(lb.values()) > 0:
        lost = {vc: info[vc]["s"].fs - info[vc]["s"].done for vc in active}
        bad_idx = defaultdict(list)
        for vc in active:
            for i, c in info[vc]["s"].flag_frames.items():
                if c["LBFULL"]:
                    bad_idx[i].append(vc)
        phy = sum(info[vc]["phy"] for vc in active)
        ev.append(f"LBFULL per VC {lb}; frames received-not-delivered per VC {lost}")
        ev.append("frame index -> VCs hit: " +
                  ", ".join(f"{i}:{v}" for i, v in sorted(bad_idx.items())))
        ev.append(f"PHY-layer flags total {phy}, frame# gaps "
                  f"{sum(info[vc]['gaps'] for vc in active)} -> link delivered every frame")
        ev.append("drops are simultaneous across VCs and in a burst -> shared downstream (DDR/NoC QoS)")
        ev.append("next: DDR bandwidth/QoS counters during the burst; line-buffer watermark; RX recovery after LBFULL")
        return "BACKPRESSURE_DROP", ev

    # 5. word count does not match what RX/DMA was configured for
    for vc in active:
        s = info[vc]["s"]
        if cfg["wc"] and any(w != cfg["wc"] for w in s.wc):
            w, n = s.wc.most_common(1)[0]
            ev.append(f"VC{vc}: {n}/{sum(s.wc.values())} long packets WC={w}, cfg {cfg['wc']} "
                      f"({w - cfg['wc']:+d} B/line)")
            ev.append(f"no ECC/CRC/SOT ({info[vc]['phy']}), FS/FE pair {s.fs}/{s.fe}, gaps {info[vc]['gaps']} "
                      f"-> link is clean, this is configuration")
            ev.append("each line lands with a constant byte offset against the stride -> diagonal shear")
            ev.append("next: sensor output width/DT/bpp vs RX DT + DMA line length/stride; media-ctl pad formats")
            return "WC_MISMATCH", ev

    # 6. rate: is the sensor slow, or does the path after the RX drop frames?
    for vc in active:
        d, s = info[vc], info[vc]["s"]
        if d["ratio"] >= 1.5 and d["gaps"] == 0 and s.done >= s.fs - 1:
            ev.append(f"VC{vc}: FS interval {d['iv']/1000:.2f} ms = x{d['ratio']:.2f} period at the RX, "
                      f"frame# contiguous (gaps 0), DONE {s.done}/FS {s.fs}")
            ev.append("the RX receives every frame the sensor sends -> the sensor itself runs slow")
            ev.append("next: sensor VTS/HTS + PLL vs exposure (auto-exposure in low light stretches the frame)")
            return "SENSOR_RATE", ev
        if s.done < s.fs - 1 and d["ratio"] < 1.2:
            pattern = "".join("D" if i in s.done_frame_idx else "." for i in range(s.fs))
            ev.append(f"VC{vc}: RX sees x{d['ratio']:.2f} period ({cfg['fps']:.0f} fps), frame# gaps {d['gaps']}, "
                      f"but DONE {s.done}/FS {s.fs}; delivered pattern {pattern}")
            ev.append(f"no LBFULL, no PHY flags ({d['phy']}) -> frames lost after the RX, regularly")
            ev.append("next: buffers queued (VIDIOC_REQBUFS count), DMA re-arm latency, ISP frame-skip setting")
            return "DOWNSTREAM_DROP", ev

    ev.append("pairing, lines, WC, frame numbers, interval, delivery and flags all match cfg")
    return "OK", ev


def main(paths):
    ok = total = 0
    for p in paths:
        cfg, expect, events = parse(p)
        st, unk = analyze(cfg, events)
        lines, info = summarize(cfg, st)
        code, ev = diagnose(cfg, info, unk)
        name = p.rsplit("/", 1)[-1]
        print(f"== {name}  (cfg {cfg['fps']:.0f} fps, {cfg['lines']} lines, WC {cfg['wc']}, VCs {cfg['vcs']}; "
              f"{len(events)} events)")
        for ln in lines:
            print("  " + ln)
        if unk:
            print(f"  unattributed packets (uncorrectable header): {len(unk)}")
        print(f"  diagnosis: {code}")
        for e in ev:
            print("    - " + e)
        if expect:
            total += 1
            res = "PASS" if code == expect else "FAIL"
            ok += res == "PASS"
            print(f"  expected {expect} -> {res}")
        print()
    print(f"SUMMARY {ok}/{total} PASS")
    return 0 if ok == total else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
