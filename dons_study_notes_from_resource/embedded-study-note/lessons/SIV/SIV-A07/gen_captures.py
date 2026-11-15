#!/usr/bin/env python3
"""SIV-A07 합성 LA 캡처 생성기 — 결정론적. solution.py 의 BusSim/render_csv 와 같은 코드.

  python3 -I gen_captures.py   # cap_*.csv + captures_truth.json 재생성 (lesson dir)

-I 는 스크립트 디렉터리를 sys.path 에서 빼므로 공통 코드를 import 할 수 없어 복사해 넣었다.
"""
import bisect
import json
import math
import os
import random
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


# ---- 버스 모델 (하네스 제공) ----
class BusSim:
    """오픈드레인 버스의 '의도' 타임라인. 각 디바이스는 선을 끌어내리거나(0) 놓는다(1).

    self.t = 커서. 슬롯 함수는 커서가 'SCL 하강 순간'에 있다고 가정하고 다음 하강으로 옮긴다.
    마스터 타이밍은 개방 루프다: SCL 해제 시각은 스케줄대로이고 선을 되읽어 기다리지 않는다
    (stretch()만 예외). 약한 풀업에서 tHIGH가 깎이는 이유이며 md §5에서 다룬다.
    """

    def __init__(self, t_low=1600, t_high=900, hd_dat=300, hd_sta=700, su_sta=700,
                 su_sto=700, t_buf=1500, t0=2000):
        self.t_low, self.t_high, self.hd_dat = t_low, t_high, hd_dat
        self.hd_sta, self.su_sta, self.su_sto, self.t_buf = hd_sta, su_sta, su_sto, t_buf
        self.t = t0
        self.ev = []            # (t, line, dev, level)
        self.owner = None       # 지금 SDA를 끌어내리는 디바이스
        self.truth = []         # 기대 프레임 [(addr, rw, [data], [ack...])]

    def set(self, t, line, dev, level):
        self.ev.append((t, line, dev, level))

    def _sda(self, t, dev, level):
        if self.owner is not None and self.owner != dev:
            self.set(t, "sda", self.owner, 1)
        self.set(t, "sda", dev, level)
        self.owner = dev if level == 0 else None

    def start(self):
        self._sda(self.t, "m", 0)
        self.t += self.hd_sta
        self.set(self.t, "scl", "m", 0)

    def rstart(self):
        self._sda(self.t + self.hd_dat, "m", 1)
        rise = self.t + self.t_low
        self.set(rise, "scl", "m", 1)
        self._sda(rise + self.su_sta, "m", 0)
        self.t = rise + self.su_sta + self.hd_sta
        self.set(self.t, "scl", "m", 0)

    def stop(self):
        self._sda(self.t + self.hd_dat, "m", 0)
        rise = self.t + self.t_low
        self.set(rise, "scl", "m", 1)
        self._sda(rise + self.su_sto, "m", 1)
        self.t = rise + self.su_sto + self.t_buf

    def bit(self, v, dev="m", launch=None, extra_low=0, stretch=0):
        """launch: SCL 하강 뒤 SDA를 바꾸는 시각. 음수면 하강 '전'(hold 위반 주입)."""
        launch = self.hd_dat if launch is None else launch
        self._sda(self.t + launch, dev, v)
        rise = self.t + self.t_low + extra_low
        self.set(rise, "scl", "m", 1)
        if stretch:
            self.set(self.t, "scl", "s", 0)
            self.set(rise + stretch, "scl", "s", 1)
            rise += stretch
        fall = rise + self.t_high
        self.set(fall, "scl", "m", 0)
        self.t = fall

    def byte(self, value, dev="m", launch=None, stretch=0):
        for i in range(7, -1, -1):
            self.bit((value >> i) & 1, dev, launch, stretch=stretch if i == 7 else 0)

    def ack(self, dev, ok=True):
        self.bit(0 if ok else 1, dev if ok else "m")

    def write(self, addr, data, ack_addr=True, data_acks=None, launch=None, stretch=0):
        data_acks = [True] * len(data) if data_acks is None else data_acks
        self.start()
        self.byte(addr << 1)
        self.ack("s", ack_addr)
        out, acks = [], [ack_addr]
        if ack_addr:
            for i, d in enumerate(data):
                self.byte(d, "m", launch, stretch=stretch if i == 0 else 0)
                self.ack("s", data_acks[i])
                out.append(d)
                acks.append(data_acks[i])
                if not data_acks[i]:
                    break
        self.stop()
        self.truth.append((addr, 0, out, acks))

    def read(self, addr, data, launch=None, rep=False, ack_addr=True):
        if not rep:
            self.start()
        self.byte((addr << 1) | 1)
        self.ack("s", ack_addr)
        acks = [ack_addr]
        out = []
        if ack_addr:
            for i, d in enumerate(data):
                self.byte(d, "s", launch)
                last = i == len(data) - 1
                self.ack("m", not last)
                acks.append(not last)
                out.append(d)
        self.stop()
        self.truth.append((addr, 1, out, acks))

    def write_then_read(self, addr, reg, data, launch=None):
        self.start()
        self.byte(addr << 1)
        self.ack("s")
        self.byte(reg)
        self.ack("s")
        self.truth.append((addr, 0, [reg], [True, True]))
        self.rstart()
        self.read(addr, data, launch, rep=True)

    def arb_byte(self, v1, v2, loser_release=True):
        """m 과 m2 가 같은 슬롯에 서로 다른 바이트를 내보낸다(SCL은 공유). 반환: 진 쪽 또는 None."""
        lost = None
        for i in range(7, -1, -1):
            b1 = 1 if lost == "m" and loser_release else (v1 >> i) & 1
            b2 = 1 if lost == "m2" and loser_release else (v2 >> i) & 1
            t = self.t + self.hd_dat
            self.set(t, "sda", "m", b1)
            self.set(t, "sda", "m2", b2)
            rise = self.t + self.t_low
            self.set(rise, "scl", "m", 1)
            fall = rise + self.t_high
            self.set(fall, "scl", "m", 0)
            self.t = fall
            if lost is None and b1 != b2:
                lost = "m" if b1 == 1 else "m2"
        self.owner = None
        return lost

    def master_reset(self, delay=1000):
        """마스터가 돌연 리셋: 자기 드라이버를 놓는다 (SCL 상승). 슬레이브 SDA는 그대로."""
        t = self.t + delay
        self.set(t, "scl", "m", 1)
        self.set(t, "sda", "m", 1)
        self.t = t

    def slave_launch(self, v):
        self._sda(self.t + self.hd_dat, "s", v)

    def recovery_clocks(self, slave_bits, release=True, t_low=2000, t_high=2000):
        """9-클럭 복구: slave_bits[i] 는 i번째 SCL 하강 뒤 슬레이브가 내는 비트.
        release=True 이면 다 낸 뒤 다음 하강에서 SDA를 놓는다 (ACK 슬롯)."""
        self.set(self.t, "scl", "m", 0)
        n = len(slave_bits) + (1 if release else 0)
        for k in range(n):
            if k > 0:
                self.set(self.t, "scl", "m", 0)
            if k < len(slave_bits):
                self._sda(self.t + self.hd_dat, "s", slave_bits[k])
            else:
                self._sda(self.t + self.hd_dat, "s", 1)
            self.set(self.t + t_low, "scl", "m", 1)
            self.t += t_low + t_high
        self.set(self.t, "scl", "m", 0)

    def idle(self, ns):
        self.t += ns

    def scl_hold_forever(self):
        """슬레이브가 SCL을 놓지 않는다. 커서는 SCL 하강 순간이어야 한다."""
        self.set(self.t, "scl", "s", 0)
        self.set(self.t + self.t_low, "scl", "m", 1)


def steps(ev, line):
    """디바이스별 의도 → wired-AND 후 라인 의도 [(t, level)]."""
    cur = {}
    out = []
    now = 1
    for t, ln, dev, lvl in sorted((e for e in ev if e[1] == line), key=lambda e: e[0]):
        cur[dev] = lvl
        new = min(cur.values())
        if new != now:
            out.append((t, new))
            now = new
    return out


def analog_cross(st, tau_r, tau_f, vth, t_end):
    """1차 RC 모델로 의도 → 임계 통과 시각 [(t, logic)].
    상승: v→1, 시정수 tau_r(풀업 RC). 하강: v→0, 시정수 tau_f(드라이버 Ron·C).
    tau_r 은 숫자 또는 t→tau 함수. 임계에 못 닿는 짧은 펄스는 사라진다."""
    out = []
    v, logic = 1.0, 1
    for i, (t, lvl) in enumerate(st):
        t2 = st[i + 1][0] if i + 1 < len(st) else t_end
        dur = max(0.0, t2 - t)
        tau = (tau_r(t) if callable(tau_r) else tau_r) if lvl else tau_f
        if tau <= 0:
            if logic != lvl:
                out.append((float(t), lvl))
                logic = lvl
            v = float(lvl)
            continue
        if lvl == 1 and logic == 0:
            tc = t + tau * math.log((1.0 - v) / (1.0 - vth))
            if tc < t2:
                out.append((tc, 1))
                logic = 1
        elif lvl == 0 and logic == 1:
            tc = t + tau * math.log(v / vth)
            if tc < t2:
                out.append((tc, 0))
                logic = 0
        v = lvl + (v - lvl) * math.exp(-dur / tau)
    return out


def render_csv(sim, tau_scl=0.0, tau_sda=0.0, tau_f=0.0, vth=0.5, dt=20, phase=0,
               aux=None, dense=False, t_end=None, meta=None):
    """BusSim → LA CSV 문자열. 샘플 k의 시각은 k·dt+phase, 값은 그 시각의 논리값.
    기본은 변화 지점만 쓰는 transition export, dense=True 면 모든 샘플."""
    aux = aux or {}
    t_end = t_end if t_end is not None else max(e[0] for e in sim.ev) + 5000
    n_end = int(math.ceil((t_end - phase) / dt))
    per_idx = {}

    def mark(edges, name):
        for tc, val in edges:
            k = max(0, int(math.ceil((tc - phase) / dt)))
            per_idx.setdefault(k, {})[name] = val

    mark(analog_cross(steps(sim.ev, "scl"), tau_scl, tau_f, vth, t_end), "scl")
    mark(analog_cross(steps(sim.ev, "sda"), tau_sda, tau_f, vth, t_end), "sda")
    for name, dev in aux.items():
        cur = 1
        edges = []
        for t, ln, d, lvl in sorted((e for e in sim.ev if e[1] == "sda" and e[2] == dev),
                                    key=lambda e: e[0]):
            if lvl != cur:
                edges.append((t, lvl))
                cur = lvl
        mark(edges, name)
    cols = ["scl", "sda"] + list(aux)
    state = {c: 1 for c in cols}
    lines = ["# sample_ns=%d" % dt, "# vth=%.2f" % vth]
    for k, v in (meta or {}).items():
        lines.append("# %s=%s" % (k, v))
    lines.append("time_ns," + ",".join(cols))

    def row(k):
        return "%d,%s" % (k * dt + phase, ",".join(str(state[c]) for c in cols))

    lines.append(row(0))
    last_row = 0
    if dense:
        for k in range(1, n_end):
            state.update(per_idx.get(k, {}))
            lines.append(row(k))
            last_row = k
    else:
        for k in sorted(per_idx):
            if k == 0 or k >= n_end:
                continue
            state.update(per_idx[k])
            lines.append(row(k))
            last_row = k
        if last_row != n_end:
            lines.append(row(n_end))
    return "\n".join(lines) + "\n"




def write_csv(name, text):
    path = os.path.join(HERE, name)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    return len(text.encode())


def gen_good():
    sim = BusSim(t_low=1600, t_high=900, hd_dat=300)
    for reg in (0x00, 0x01, 0x02):
        sim.write_then_read(0x48, reg, [0x1A, 0x60])
        sim.write(0x48, [reg, 0x80, 0x00])
    return sim, render_csv(sim, tau_scl=70.0, tau_sda=70.0, tau_f=6.0, dt=20,
                           meta={"source": "synthetic good bus (BMS board rev B, Rp 2.2k)"})


def gen_weak():
    """약한 풀업: Rp 10 kΩ 오장착. SCL τ=500 ns, SDA τ=800 ns ±10 %(프레임별).
    슬레이브는 SCL이 VIH(0.7)를 지나는 순간 SDA를 표본한다. SDA 0→1 전압이 VIH 미만이면
    주소 비트를 0으로 읽어 NACK (데이터 바이트 오독은 모델링하지 않는다)."""
    rng = random.Random(7)
    t_low, t_high, hd_dat = 1600, 900, 1200      # setup 설정값 400 ns
    sim = BusSim(t_low=t_low, t_high=t_high, hd_dat=hd_dat, t_buf=3000)
    tau_scl = 500.0
    starts, taus = [], []
    nack_addr = 0
    for i in range(24):
        tau_sda = 800.0 * (1.0 + rng.uniform(-0.10, 0.10))
        starts.append(sim.t)
        taus.append(tau_sda)
        elapsed = (t_low - hd_dat) + tau_scl * math.log(1.0 / 0.3)
        v = 1.0 - math.exp(-elapsed / tau_sda)
        ok = v >= 0.7                                    # 0→1 비트마다 같은 elapsed
        if not ok:
            nack_addr += 1
        sim.write(0x48, [0x01, i & 0xFF], ack_addr=ok)

    def tau_fn(t):
        idx = bisect.bisect_right(starts, t) - 1
        return taus[max(0, idx)]
    text = render_csv(sim, tau_scl=tau_scl, tau_sda=tau_fn, tau_f=10.0, dt=20,
                      meta={"source": "synthetic weak pull-up: Rp 10k, Cb SCL 50 pF / SDA about 80 pF"})
    tau_med = statistics.median(taus)
    delay = tau_med * math.log(2) - 10.0 * math.log(2)
    return text, dict(nack_addr=nack_addr, delay_ns=delay, tau_scl=tau_scl, tau_sda_med=tau_med,
                      frames=24)


def gen_timing():
    """타이밍 위반: 강한 풀업. 400 kHz 근처 master tLOW 1.2 µs, 슬레이브가 데이터를 SCL↑ 30 ns 전에 launch."""
    t_low, t_high = 1200, 1100
    sim = BusSim(t_low=t_low, t_high=t_high, hd_dat=300)
    late = t_low - 30
    for i in range(10):
        sim.write(0x48, [0x05, i])
        sim.read(0x48, [0xA6, 0x5C], launch=late)
    text = render_csv(sim, tau_scl=60.0, tau_sda=60.0, tau_f=5.0, dt=20,
                      meta={"source": "synthetic timing violation: master tLOW + late slave data"})
    tlow_la = t_low + 60.0 * math.log(2) - 5.0 * math.log(2)
    return text, dict(tlow_la_ns=round(tlow_la), slave_setup_cfg_ns=30)


def gen_hang():
    """슬레이브 hang: 읽기 도중 마스터가 리셋, 슬레이브가 SDA low를 쥠. 2 ms 뒤 9-클럭 복구."""
    sim = BusSim(t_low=1600, t_high=900, hd_dat=300)
    for i in range(3):
        sim.write_then_read(0x48, 0x00, [0x12, 0x34 + i])
    sim.idle(20_000)
    sim.start()
    sim.byte((0x48 << 1) | 1)
    sim.ack("s")
    for v in (1, 0, 1):                       # 0xA0 = 1010_0000, 비트 3개까지 전송
        sim.bit(v, "s")
    sim.slave_launch(0)                       # 비트 4 = 0 을 슬레이브가 구동
    sim.master_reset()
    stuck = 2_000_000
    sim.idle(stuck)
    # 복구: 슬레이브는 남은 비트 3..0(모두 0)을 낸 뒤 ACK 슬롯에서 SDA를 놓는다
    sim.recovery_clocks([0, 0, 0, 0], release=True)
    sim.stop()
    sim.idle(5000)
    sim.write_then_read(0x48, 0x00, [0x55, 0x66])
    text = render_csv(sim, tau_scl=70.0, tau_sda=70.0, tau_f=6.0, dt=20,
                      meta={"source": "synthetic slave hang: 0x48 holds SDA low after master reset"})
    return text, dict(stuck_ns=stuck, recovery_falls=5, slave_addr=0x48, slot_byte=1, slot_bit=3)


def main():
    truth = {}
    _, text = gen_good()
    sizes = {"cap_good.csv": write_csv("cap_good.csv", text)}
    text, t = gen_weak()
    sizes["cap_weak_pullup.csv"] = write_csv("cap_weak_pullup.csv", text)
    truth["cap_weak_pullup.csv"] = t
    text, t = gen_timing()
    sizes["cap_timing_violation.csv"] = write_csv("cap_timing_violation.csv", text)
    truth["cap_timing_violation.csv"] = t
    text, t = gen_hang()
    sizes["cap_slave_hang.csv"] = write_csv("cap_slave_hang.csv", text)
    truth["cap_slave_hang.csv"] = t
    with open(os.path.join(HERE, "captures_truth.json"), "w", encoding="utf-8", newline="\n") as f:
        json.dump(truth, f, indent=1, sort_keys=True)
        f.write("\n")
    for k in sorted(sizes):
        print("%-28s %7d bytes" % (k, sizes[k]))


if __name__ == "__main__":
    sys.exit(main())
