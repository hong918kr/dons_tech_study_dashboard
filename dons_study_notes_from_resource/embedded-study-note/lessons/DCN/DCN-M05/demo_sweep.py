#!/usr/bin/env python3
"""DCN-M05 demo — 같은 시뮬레이터로 4가지 스윕 (결과는 결정적, 실행 시간만 기계에 따라 다르다)

  1. 타임라인: 50 사이클 창마다 배달 수 — 혼잡(incast)과 데드락(bounce)의 차이 (소스는 600 에서 정지)
  2. 부하 스윕: bounce 경로에 호스트 2대씩, 호스트당 rate r → 데드락 문턱
  3. 버퍼 스윕: XOFF 를 키우면 데드락이 사라지나?
  4. watchdog T 스윕: bounce(진짜 데드락) vs incast(그냥 혼잡)에서의 drop

  python3 -I demo_sweep.py
"""
import os
import platform
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)            # -I 는 스크립트 디렉터리를 sys.path 에서 빼므로 직접 넣는다
sys.dont_write_bytecode = True      # 레슨 폴더에 __pycache__ 를 남기지 않는다
import solution as S                # noqa: E402  (같은 레슨의 본문 라이브러리)


def sh(cmd):
    try:
        return subprocess.run(cmd, capture_output=True, text=True, check=False).stdout.strip()
    except OSError:
        return "?"


def main():
    t0 = time.perf_counter()
    print("machine: %s | python %s | %s" % (
        " / ".join(sh(["sysctl", "-n", "machdep.cpu.brand_string", "hw.perflevel0.physicalcpu",
                       "hw.perflevel1.physicalcpu", "hw.perflevel2.physicalcpu"]).splitlines()),
        platform.python_version(), (sh(["cc", "--version"]).splitlines() or ["?"])[0]))
    print("load:   %s" % sh(["uptime"]))
    print("")

    print("== 1. delivered per 50-cycle window (sources ON for t < 600) ==")
    inc = S.experiment(S.incast_flows())
    dl = S.experiment(S.bounce_flows())
    for name, r in (("incast 3->1 (congestion)", inc), ("bounce CBD (deadlock)", dl)):
        log = r["sim"].deliv_log
        w = [sum(log[i:i + 50]) for i in range(0, 1200, 50)]
        print("%-26s %s" % (name, " ".join("%2d" % x for x in w)))
    print("")

    print("== 2. load sweep: 2 hosts per bounce path, rate r each (demand on a shared link = 4r) ==")
    print("%6s %6s %10s %9s %8s" % ("r", "4r", "deadlock@", "delivered", "residual"))
    for i in range(20, 33, 2):
        r = S.experiment(S.bounce_flows(2, i / 100.0), on=2000, drain=600)
        print("%6.2f %6.2f %10s %9d %8d" % (i / 100.0, 4 * i / 100.0, r["deadlock_at"], r["delivered"],
                                            r["residual"]))
    r = S.experiment(S.bounce_flows(1, 1.0), on=2000, drain=600)
    print("1 host per path, r=1.00 (demand 2.0): deadlock@ %s delivered %d pause_events %d"
          % (r["deadlock_at"], r["delivered"], r["pause_events"]))
    print("")

    print("== 3. buffer sweep on the bounce scenario (XON = XOFF/2, CAP = XOFF+4) ==")
    print("%6s %10s %9s %8s %8s" % ("XOFF", "deadlock@", "delivered", "trapped", "max_occ"))
    for xoff in (8, 16, 32, 64, 128):
        r = S.experiment(S.bounce_flows(), xoff=xoff, xon=xoff // 2, cap=xoff + 4)
        print("%6d %10s %9d %8d %8d" % (xoff, r["deadlock_at"], r["delivered"], r["residual"], r["max_occ"]))
    print("")

    print("== 4. watchdog T sweep ==")
    print("max continuous PAUSE-blocked age in incast without watchdog: %d cycles" % inc["max_age"])
    print("%6s | %-34s | %-30s" % ("T", "bounce: fires drops delivered empty@", "incast: fires drops delivered"))
    for T in (10, 20, 40, 50, 100, 200, 400):
        b = S.experiment(S.bounce_flows(), watchdog=T)
        c = S.experiment(S.incast_flows(), watchdog=T)
        print("%6d | %5d %5d %9d %6s        | %5d %5d %9d" % (
            T, b["wd_fires"], b["drop_wd"], b["delivered"], b["empty_at"], c["wd_fires"], c["drop_wd"],
            c["delivered"]))
    print("")
    print("wall time: %.2f s" % (time.perf_counter() - t0))


if __name__ == "__main__":
    main()
