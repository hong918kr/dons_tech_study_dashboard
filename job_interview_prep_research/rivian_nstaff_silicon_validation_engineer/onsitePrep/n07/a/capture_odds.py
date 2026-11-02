#!/usr/bin/env python3
"""N07A capture-odds calculator (stdlib only, self-checking).

Answers four instrument-setup questions that come up in "debug this interface" interviews:
  1. Scope dead time:   %DT = 100 * (1 - U*W)          (Keysight 5989-7885)
  2. Luck vs trigger:   P(see a rare event in T s) = 1 - (1 - 1/S)^(U*T),  S = event period / W
  3. Segmented memory:  how much memory you need for N bursts vs one continuous record
  4. Bandwidth / LA rate per interface:  BW_needed = k * 0.35 / tr ;  LA rate >= 4 x signal rate
Every case has an expected verdict and prints PASS/FAIL against it.
"""
import math

results = []


def check(name, got, expected, tol=0.02):
    ok = abs(got - expected) <= tol * max(1.0, abs(expected))
    results.append(ok)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: got {got:.4g}, expected {expected:.4g}")


def check_verdict(name, got, expected):
    ok = got == expected
    results.append(ok)
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}: verdict {got} (expected {expected})")


def dead_time_pct(update_rate_wfm_s, timebase_s_per_div, divs=10):
    w = timebase_s_per_div * divs
    return 100.0 * (1.0 - update_rate_wfm_s * w), w


def p_capture(event_period_s, window_s, update_rate_wfm_s, observe_s):
    s = event_period_s / window_s
    if s <= 1.0:
        return 1.0
    n = update_rate_wfm_s * observe_s
    # 1 - (1-1/S)^N computed stably
    return -math.expm1(n * math.log1p(-1.0 / s))


print("== 1. Scope dead time (persistence / free-running acquisition) ==")
for label, u, tb, exp in [
    ("fast scope 1,000,000 wfm/s @ 20 ns/div", 1_000_000, 20e-9, 80.0),
    ("decode-on scope 1,000 wfm/s @ 20 ns/div", 1_000, 20e-9, 99.98),
    ("I2C view 50 wfm/s @ 100 us/div", 50, 100e-6, 95.0),
]:
    dt, w = dead_time_pct(u, tb)
    print(f"  {label}: window {w*1e6:.3g} us, dead time {dt:.5f}%")
    check(label + " dead-time %", dt, exp, tol=0.001)

print("\n== 2. Rare event: wait for luck (persistence) vs trigger on it ==")
cases = [
    # label, event period, window, update rate, observe time, expected P
    ("Keysight example: glitch every 10 ms, 200 ns window, 1e6 wfm/s, 1 s", 10e-3, 200e-9, 1e6, 1.0, 1.0),
    ("same glitch, 1e3 wfm/s (decode on), 1 s", 10e-3, 200e-9, 1e3, 1.0, 0.0198),
    ("I2C glitch every 10 s, 1 ms window, 50 wfm/s, 60 s", 10.0, 1e-3, 50, 60.0, 0.259),
    ("I2C glitch every 10 s, 1 ms window, 50 wfm/s, 10 min", 10.0, 1e-3, 50, 600.0, 0.950),
]
for label, per, w, u, t, exp in cases:
    p = p_capture(per, w, u, t)
    print(f"  {label}: P = {p*100:.2f}%")
    check(label, p, exp, tol=0.01)
# With a pulse-width / runt / NACK trigger the scope only acquires when the event happens.
# It can miss an event only if one arrives during re-arm; P(miss) ~ rearm / period.
rearm = 2.5e-6  # intersegment/re-arm time order quoted for a Keysight 90000 (5989-4932)
p_miss = rearm / 10.0
print(f"  pulse-width trigger on the same I2C glitch: P(miss one event) ~ {p_miss:.1e} -> capture ~certain")
check("trigger-on-event miss probability", p_miss, 2.5e-7, tol=0.01)

print("\n== 3. Segmented memory vs one continuous record ==")
# 1,000 I2C transactions at 400 kHz, one every 10 ms; each ~3 bytes*9 clocks = 27 clocks = 67.5 us
sr = 100e6  # 100 MS/s is plenty for 400 kHz analog edges (tr up to 300 ns)
txn = 27 / 400e3
seg_len = 100e-6  # segment a bit longer than one transaction
seg_pts = sr * seg_len
segmented_pts = 1000 * seg_pts
continuous_pts = sr * (1000 * 10e-3)
print(f"  transaction length {txn*1e6:.1f} us, segment {seg_len*1e6:.0f} us = {seg_pts:.0f} pts")
print(f"  segmented: 1000 x {seg_pts:.0f} = {segmented_pts/1e6:.1f} Mpts ; continuous 10 s: {continuous_pts/1e6:.0f} Mpts")
print(f"  memory ratio continuous/segmented = {continuous_pts/segmented_pts:.0f}x")
check("segmented total Mpts", segmented_pts / 1e6, 10.0)
check("continuous/segmented ratio", continuous_pts / segmented_pts, 100.0)

print("\n== 4. Per-interface: scope bandwidth for 5% rise-time error, LA sample rate ==")
# tr values: I2C Fm max rise is a spec number (UM10204: 300 ns); others are typical [estimate].
# k = 3 -> instrument tr is 1/3 of signal tr -> ~5% error (Tek ABCs of Probes).
iface = [
    # name, signal tr (s), toggle/clock rate (Hz), LA rate available (S/s), expected LA verdict
    ("I2C Fm 400 kHz (tr<=300 ns spec)", 300e-9, 400e3, 25e6, "OK"),
    ("UART 3 Mbaud (tr~10 ns est.)", 10e-9, 3e6, 25e6, "OK"),
    ("SPI 50 MHz (tr~2 ns est.)", 2e-9, 50e6, 100e6, "TOO SLOW"),
    ("SPI 50 MHz on 500 MS/s LA", 2e-9, 50e6, 500e6, "OK"),
    ("RGMII data 250 Mb/s DDR on 125 MHz clk (tr~0.75 ns est.)", 0.75e-9, 250e6, 500e6, "TOO SLOW"),
]
for name, tr, f, la, exp in iface:
    bw = 3 * 0.35 / tr
    ratio = la / f
    verdict = "OK" if ratio >= 4 else "TOO SLOW"
    print(f"  {name}: scope BW >= {bw/1e6:,.0f} MHz ; LA {la/1e6:.0f} MS/s = {ratio:.1f}x signal rate -> {verdict}"
          f" (timing resolution {1e9/la:.0f} ns)")
    check_verdict(name + " LA rate", verdict, exp)
check("I2C Fm needed BW (MHz)", 3 * 0.35 / 300e-9 / 1e6, 3.5)
check("RGMII needed BW (MHz)", 3 * 0.35 / 0.75e-9 / 1e6, 1400.0)
# Timing measurement with an LA: +-1 sample uncertainty on each edge.
setup = 100e-9  # I2C Fm tSU;DAT min 100 ns (UM10204)
for la in (25e6, 500e6):
    err = (1 / la) / setup * 100
    print(f"  measuring a 100 ns setup time with {la/1e6:.0f} MS/s: +-{1e9/la:.0f} ns = +-{err:.0f}% of the value")
check("25 MS/s error on 100 ns setup (%)", (1 / 25e6) / setup * 100, 40.0)

print(f"\nSUMMARY: {sum(results)}/{len(results)} PASS")
raise SystemExit(0 if all(results) else 1)
