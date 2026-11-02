#!/usr/bin/env python3
"""N07G: generate small CSI-2 RX event logs (one per scenario) into mocks/.

Each log is what a validation hook on the receiver would record: one line per
event the RX controller reported, plus a DONE line when the DMA handed a full
frame buffer to software. The frames are scaled down (a few lines per frame)
so the logs stay readable; the timing (frame period) is real.

Line format:   t_us  vc  kind  arg  flags
  kind  FS/FE   arg = 16-bit frame number from the short packet (0 = not used)
        LONG    arg = word count (bytes) from the packet header
        UNK     header with an uncorrectable error: VC/DT/WC unknown (arg '-')
        DONE    DMA completed a buffer for this VC, arg = buffer sequence
  flags '-' or comma list of: ECC1 (header 1-bit corrected), ECC2 (header
        2-bit, packet dropped), CRC (payload CRC mismatch), SOT (SoT error on
        a lane), LBFULL (stream line buffer full), ERR (buffer has an error,
        like V4L2_BUF_FLAG_ERROR)
Header lines:  #cfg key=value ...   #expect CODE   # free comment
Deterministic: no randomness, same output every run.
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(HERE, "mocks")
LINE_US = 15.0  # spacing between long packets in the scaled-down frame


class Log:
    def __init__(self, name, cfg, expect, comment):
        self.name, self.rows = name, []
        self.head = ["# " + comment, "#cfg " + " ".join(f"{k}={v}" for k, v in cfg.items()),
                     "#expect " + expect, "# t_us        vc  kind  arg    flags"]

    def ev(self, t, vc, kind, arg="-", flags="-"):
        self.rows.append((t, vc, kind, str(arg), flags))

    def frame(self, t0, vc, fnum, lines, wc, line_flags=None, fe=True, done_seq=None,
              done_flags="-", fe_flags="-", fe_unk=False):
        """Emit FS, `lines` long packets, FE, then DONE. Returns end time."""
        line_flags = line_flags or {}
        self.ev(t0, vc, "FS", fnum)
        t = t0 + 10.0
        for i in range(lines):
            self.ev(t, vc, "LONG", wc, line_flags.get(i, "-"))
            t += LINE_US
        if fe_unk:  # FE header hit by a 2-bit error: RX cannot tell what it was
            self.ev(t, "-", "UNK", "-", "ECC2")
        elif fe:
            self.ev(t, vc, "FE", fnum, fe_flags)
        if done_seq is not None:
            self.ev(t + 40.0, vc, "DONE", done_seq, done_flags)
        return t

    def write(self):
        self.rows.sort(key=lambda r: (r[0], str(r[1])))
        path = os.path.join(OUT, self.name)
        with open(path, "w") as f:
            f.write("\n".join(self.head) + "\n")
            for t, vc, kind, arg, flags in self.rows:
                f.write(f"{t:12.1f}  {vc!s:>2}  {kind:<5} {arg:<6} {flags}\n")
        return path


def period(fps):
    return 1e6 / fps


def normal():
    # 8 frames, frame number wraps 65535 -> 1 (0 means "frame numbers not used")
    lg = Log("G0_normal.log", dict(fps=30, lines=6, wc=2400, vcs="0"), "OK",
             "baseline: one camera VC0, RAW10 1920 px -> WC 2400, frame number wraps")
    fn = 65532
    for k in range(8):
        lg.frame(k * period(30), 0, fn, 6, 2400, done_seq=k)
        fn = 1 if fn == 65535 else fn + 1
    return lg


def g3_temperature():
    # ECC 1-bit corrections climb with chamber temperature, then CRC on payload
    lg = Log("G3_ecc_vs_temp.log", dict(fps=30, lines=6, wc=2400, vcs="0"), "SI_MARGIN",
             "G3 extra: chamber ramp 25->105 C; header ECC1 climbs, then CRC + frame errors")
    per_frame_ecc1 = [0, 0, 1, 1, 2, 3, 4, 5]
    for k in range(8):
        flags = {i: "ECC1" for i in range(per_frame_ecc1[k])}
        if k >= 6:
            flags[5] = "CRC"
        lg.frame(k * period(30), 0, 100 + k, 6, 2400, line_flags=flags, done_seq=k,
                 done_flags="ERR" if k >= 6 else "-")
    lg.head.insert(1, "# temp_C per frame: 25 45 65 75 85 95 100 105")
    return lg


def g4_wc_mismatch():
    # sensor programmed 1928 px wide (border pixels on); RX/DMA expect 1920
    lg = Log("G4_diagonal_tear.log", dict(fps=30, lines=6, wc=2400, vcs="0"), "WC_MISMATCH",
             "G4: image sheared diagonally; every line is 10 bytes longer than the RX/DMA stride")
    for k in range(6):
        lg.frame(k * period(30), 0, 10 + k, 6, 2410, done_seq=k)
    return lg


def g6_four_cam_drop():
    # VC0-3 FSYNC'd (small skew); frames 4-5 hit a DDR bandwidth dip (NPU burst)
    lg = Log("G6_four_cam_drop.log", dict(fps=30, lines=6, wc=2880, vcs="0,1,2,3"),
             "BACKPRESSURE_DROP",
             "G6: four cameras on one port as VC0-3; NPU job starts at ~133 ms")
    seq = [0, 0, 0, 0]
    for k in range(8):
        for vc in range(4):
            t0 = k * period(30) + vc * 3.0
            bad = k in (4, 5) and vc in ((0, 1, 2, 3) if k == 4 else (1, 3))
            flags = {3: "LBFULL", 4: "LBFULL"} if bad else {}
            if bad:
                lg.frame(t0, vc, 200 + k, 6, 2880, line_flags=flags, done_seq=None)
            else:
                lg.frame(t0, vc, 200 + k, 6, 2880, done_seq=seq[vc])
                seq[vc] += 1
    return lg


def g7_vc_collision():
    # deserializer maps camera 1 to VC0 as well -> both cameras land on VC0
    lg = Log("G7_vc_collision.log", dict(fps=30, lines=6, wc=2880, vcs="0,1"), "VC_COLLISION",
             "G7: camera on deserializer input B shows up in the VC0 buffer; VC1 is empty")
    for k in range(6):
        t0 = k * period(30)
        lg.ev(t0, 0, "FS", 500 + k)          # camera A
        lg.ev(t0 + 2.0, 0, "FS", 40 + k)     # camera B, FSYNC'd, own counter
        t = t0 + 10.0
        for i in range(12):                   # lines of A and B interleave
            lg.ev(t, 0, "LONG", 2880)
            t += LINE_US / 2
        lg.ev(t, 0, "FE", 500 + k)
        lg.ev(t + 2.0, 0, "FE", 40 + k)
        lg.ev(t + 40.0, 0, "DONE", k, "ERR")
    return lg


def g8_fe_lost():
    # frame 3's FE header gets a 2-bit error -> dropped -> frames 3 and 4 merge
    lg = Log("G8_fe_lost_merge.log", dict(fps=30, lines=6, wc=2400, vcs="0"), "FE_LOST_MERGE",
             "G8: one FE is lost; the next frame is appended to the open one")
    s = 0
    for k in range(7):
        t0 = k * period(30)
        if k == 3:
            lg.frame(t0, 0, 300 + k, 6, 2400, fe_unk=True, done_seq=None)
        elif k == 4:
            lg.frame(t0, 0, 300 + k, 6, 2400, done_seq=s, done_flags="ERR")
            s += 1
        else:
            lg.frame(t0, 0, 300 + k, 6, 2400, done_seq=s)
            s += 1
    return lg


def g10a_sensor_15fps():
    # sensor itself runs at 15 fps (exposure > frame time stretches VTS)
    lg = Log("G10a_sensor_15fps.log", dict(fps=30, lines=6, wc=2400, vcs="0"), "SENSOR_RATE",
             "G10a: app sees 15 fps; FS every 66.7 ms at the RX, frame numbers contiguous")
    for k in range(6):
        lg.frame(k * period(15), 0, 700 + k, 6, 2400, done_seq=k)
    return lg


def g10b_buffer_starved():
    # RX sees 30 fps; DMA only has a buffer for every other frame
    lg = Log("G10b_buffer_starved.log", dict(fps=30, lines=6, wc=2400, vcs="0"),
             "DOWNSTREAM_DROP",
             "G10b: app sees 15 fps; FS every 33.3 ms at the RX, but only every other frame DONE")
    s = 0
    for k in range(10):
        if k % 2 == 0:
            lg.frame(k * period(30), 0, 800 + k, 6, 2400, done_seq=s)
            s += 1
        else:
            lg.frame(k * period(30), 0, 800 + k, 6, 2400, done_seq=None)
    return lg


def main():
    os.makedirs(OUT, exist_ok=True)
    for maker in (normal, g3_temperature, g4_wc_mismatch, g6_four_cam_drop,
                  g7_vc_collision, g8_fe_lost, g10a_sensor_15fps, g10b_buffer_starved):
        lg = maker()
        p = lg.write()
        print(f"wrote {os.path.relpath(p, HERE)} ({len(lg.rows)} events)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
