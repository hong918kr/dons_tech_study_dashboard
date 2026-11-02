#!/usr/bin/env python3
"""N06E - numbers you want ready when you write pass criteria for
Ethernet (E2), CSI-2 camera (E3) and time sync (E4).

  1. RFC 2544 line-rate frame rates for 100BASE-T1 / 1000BASE-T1
  2. how long a zero-error BER run must be (rule of three on bits)
  3. CSI-2 link + DDR budget for example camera configs [example configs]
  4. PTP offset math: path asymmetry, oscillator drift, rolling-shutter skew

Python stdlib only.  Run:  python3 -I e_numbers.py
"""
import math

RFC2544_SIZES = [64, 128, 256, 512, 1024, 1280, 1518]   # RFC 2544 s9.1 (Ethernet)
WIRE_OVERHEAD = 20                                      # preamble 7 + SFD 1 + IFG 12


def line_fps(bps, frame):
    return bps // ((frame + WIRE_OVERHEAD) * 8)


def section1():
    print("== 1. RFC 2544 line rate (frames/s), frame size incl. FCS ==")
    print(f"{'size B':>7} {'100M fps':>10} {'1G fps':>10} {'1G ns/frame':>12} {'1G payload Mb/s':>16}")
    for f in RFC2544_SIZES + [9018]:
        f100, f1g = line_fps(100_000_000, f), line_fps(1_000_000_000, f)
        ns = (f + WIRE_OVERHEAD) * 8          # at 1 Gb/s one bit = 1 ns
        payload = f1g * (f - 18) * 8 / 1e6    # minus 14 B header + 4 B FCS (untagged)
        tag = "  (jumbo, not in RFC 2544)" if f == 9018 else ""
        print(f"{f:7d} {f100:10d} {f1g:10d} {ns:12d} {payload:16.1f}{tag}")
    assert line_fps(1_000_000_000, 64) == 1_488_095
    assert line_fps(100_000_000, 1518) == 8_127
    print("PASS 1G@64B = 1,488,095 fps ; 100M@1518B = 8,127 fps")


def section2():
    print("\n== 2. zero-error run length to claim BER < target at 95% (N = -ln(0.05)/BER bits) ==")
    rates = [("100BASE-T1", 100e6), ("1000BASE-T1", 1e9), ("CSI-2 1 lane 2.5G", 2.5e9),
             ("CSI-2 4 lanes x 2.5G", 10e9)]
    for ber in (1e-10, 1e-12):
        n = -math.log(0.05) / ber
        for name, r in rates:
            t = n / r
            human = f"{t:8.0f} s" if t < 3600 else f"{t/3600:8.2f} h"
            print(f"BER {ber:.0e}  {name:22s} bits {n:.3e}  time {human}")
    assert abs(-math.log(0.05) / 1e-12 / 1e9 - 2995.7) < 1
    print("PASS 1e-12 @ 1 Gb/s ~= 2996 s (50 min)")


def section3():
    print("\n== 3. CSI-2 link and DDR budget [example configs; 20% blanking+protocol overhead is an assumption] ==")
    cams = [
        ("1x 8MP 3840x2160 RAW12 30fps", 1, 3840, 2160, 12, 30),
        ("4x 3MP 1920x1536 RAW12 30fps (VC0-3 on one port)", 4, 1920, 1536, 12, 30),
        ("4x 2MP 1920x1080 RAW10 60fps (VC0-3)", 4, 1920, 1080, 10, 60),
    ]
    OVH = 1.20
    for name, n, w, h, bpp, fps in cams:
        payload = n * w * h * bpp * fps
        need = payload * OVH
        ddr = n * w * h * 2 * fps            # unpacked to 16 bit/pixel in memory
        print(f"{name}")
        print(f"   payload {payload/1e9:.2f} Gb/s, with overhead {need/1e9:.2f} Gb/s, DDR write (16b/px) {ddr/1e9:.2f} GB/s")
        for lane_gbps in (1.5, 2.5, 4.5):
            cap = 4 * lane_gbps * 1e9
            util = need / cap
            flag = "OK" if util < 0.8 else ("TIGHT" if util < 1 else "OVER")
            print(f"   4 lanes x {lane_gbps} Gb/s = {cap/1e9:4.1f} Gb/s  util {util*100:5.1f}%  {flag}")
    # Linux formula: pixel_rate = link_freq * 2 * lanes / bpp  (D-PHY DDR clock)
    pr = 750e6 * 2 * 4 / 12
    assert pr == 500e6
    print("PASS Linux pixel_rate(750 MHz, 4 lanes, 12 bpp) = 500 Mpix/s")
    # RAW10 / RAW12 word count per line
    for dt, bpp, w in (("RAW10", 10, 1920), ("RAW12", 12, 1920), ("RAW12", 12, 3840)):
        print(f"   WC {dt} width {w} = {w*bpp//8} B")


def section4():
    print("\n== 4. PTP / gPTP timing math ==")
    # two-step exchange: t1 master tx, t2 slave rx, t3 slave tx, t4 master rx
    true_off, d_ms, d_sm = 1000.0, 600.0, 400.0     # ns; slave clock ahead by 1000 ns
    t1 = 0.0
    t2 = t1 + d_ms + true_off
    t3 = t2 + 5000.0
    t4 = t3 - true_off + d_sm
    off = ((t2 - t1) - (t4 - t3)) / 2
    mpd = ((t2 - t1) + (t4 - t3)) / 2
    err = off - true_off
    print(f"asymmetry {d_ms-d_sm:.0f} ns -> measured offset {off:.0f} ns, mean path delay {mpd:.0f} ns, error {err:.0f} ns (= asym/2)")
    assert err == (d_ms - d_sm) / 2
    for ppm, interval in ((100, 0.125), (100, 1.0), (0.1, 0.125)):
        print(f"freq error {ppm:>5} ppm over sync interval {interval*1000:6.0f} ms -> drift {ppm*1e-6*interval*1e9:9.1f} ns between corrections")
    print("timestamp clock 125 MHz -> 8 ns resolution; 250 MHz -> 4 ns")
    # rolling shutter: when does the 'frame timestamp' happen for the bottom row?
    fps, rows, vblank = 30, 2160, 90
    line_t = 1 / fps / (rows + vblank)
    skew = line_t * (rows - 1)
    print(f"rolling shutter {rows} rows @ {fps} fps (+{vblank} blank lines): line time {line_t*1e6:.2f} us, top-to-bottom {skew*1e3:.1f} ms")
    for v in (15, 30):
        print(f"   at {v} m/s the car moves {v*skew:.2f} m between first and last row")
    print("PASS asymmetry error = asym/2")


if __name__ == "__main__":
    section1()
    section2()
    section3()
    section4()
