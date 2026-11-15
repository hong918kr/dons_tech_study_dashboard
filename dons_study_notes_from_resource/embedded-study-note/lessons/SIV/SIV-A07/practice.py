#!/usr/bin/env python3
"""SIV-A07 추가 연습 — 한 번에 검증. 디코더는 solution.py 와 같은 코드를 넣었다.
  python3 -I practice.py
"""
import bisect
import math
import os
import random
import statistics
import sys

HERE = os.path.dirname(os.path.abspath(__file__))

# UM10204 F/S-mode 표의 최솟값·최댓값 (ns). 값 출처와 측정 기준점은 md §5 표 참고.
LIMITS = {
    "standard": dict(tLOW=4700, tHIGH=4000, tSU_DAT=250, tHD_STA=4000, tSU_STA=4700,
                     tSU_STO=4000, tBUF=4700, tr_max=1000, tVD_DAT=3450),
    "fast":     dict(tLOW=1300, tHIGH=600, tSU_DAT=100, tHD_STA=600, tSU_STA=600,
                     tSU_STO=600, tBUF=1300, tr_max=300, tVD_DAT=900),
}
TIMING_KINDS = ("SETUP", "HOLD", "TLOW", "THIGH", "THD_STA", "TSU_STA", "TSU_STO", "TBUF")
SMBUS_TIMEOUT_NS = 25_000_000  # SMBus tTIMEOUT(min) 25 ms. I2C 자체에는 stretching 상한이 없다 → 정책값.


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


class Capture:
    """파싱된 LA 캡처. t, scl, sda 는 같은 길이의 리스트, aux 는 추가 채널 dict."""

    def __init__(self, t, scl, sda, aux, sample_ns, meta):
        self.t, self.scl, self.sda, self.aux, self.sample_ns, self.meta = t, scl, sda, aux, sample_ns, meta

    def at(self, name, t):
        """시각 t 에서 채널 값 (t 이하 마지막 행의 값)."""
        col = {"scl": self.scl, "sda": self.sda}.get(name) or self.aux[name]
        i = bisect.bisect_right(self.t, t) - 1
        return col[max(0, i)]


class Decoded:
    def __init__(self):
        self.frames = []        # dict(t_start,t_end,end,rstart,addr,rw,bytes[...],t_first_fall)
        self.findings = []      # dict(kind,t,measured,limit,verdict,owner,pol,frame,byte,bit)
        self.slots = []         # 슬롯별 표본 dict(t,frame,byte,bit,owner,val)
        self.launches = []      # (owner, pol, SCL 하강→SDA 변화 ns)
        self.highs = []
        self.lows = []
        self.coincident = 0

    def count(self, kind=None, verdict=None):
        return sum(1 for f in self.findings
                   if (kind is None or f["kind"] == kind) and (verdict is None or f["verdict"] == verdict))


def tr_from_delay(delay_ns, vth=0.5):
    """임계 vth 에서 본 상승 지연 → 1차 RC 가정의 UM10204 tr(0.3→0.7 VDD)."""
    rc = delay_ns / math.log(1.0 / (1.0 - vth))
    return rc * math.log(0.7 / 0.3)


# ---- Part A: 파서 + 디코더 ----
def parse_la_csv(text):
    """LA CSV → Capture.

    형식: '# key=value' 메타 줄(선택), 헤더 'time_ns,...'(scl, sda 필수, 순서 무관, 대소문자 무시),
    그다음 데이터 행. time_ns 는 정수 ns, 강한 단조 증가. 채널 값은 0/1 만.
    오류는 ValueError("line N: ...") 로 던진다.
    sample_ns: 메타가 있으면 그 값, 없으면 인접 시각 차의 gcd.
    """
    meta = {}
    header = None
    rows = []
    for n, raw in enumerate(text.splitlines(), 1):
        line = raw.strip()
        if not line:
            continue
        if line.startswith("#"):
            kv = line[1:].strip()
            if "=" in kv:
                k, v = kv.split("=", 1)
                meta[k.strip()] = v.strip()
            continue
        if header is None:
            cols = [c.strip().lower() for c in line.split(",")]
            if cols[0] != "time_ns" or "scl" not in cols or "sda" not in cols:
                raise ValueError("line %d: header must be time_ns,...,scl,...,sda" % n)
            header = cols
            continue
        parts = [p.strip() for p in line.split(",")]
        if len(parts) != len(header):
            raise ValueError("line %d: expected %d columns" % (n, len(header)))
        try:
            vals = [int(p) for p in parts]
        except ValueError:
            raise ValueError("line %d: non-integer field" % n)
        if any(v not in (0, 1) for v in vals[1:]):
            raise ValueError("line %d: channel value must be 0 or 1" % n)
        if rows and vals[0] <= rows[-1][0]:
            raise ValueError("line %d: time must increase" % n)
        rows.append(vals)
    if header is None or not rows:
        raise ValueError("line 0: empty capture")
    t = [r[0] for r in rows]
    cols = {name: [r[i] for r in rows] for i, name in enumerate(header) if i > 0}
    aux = {k: v for k, v in cols.items() if k not in ("scl", "sda")}
    if "sample_ns" in meta:
        dt = int(meta["sample_ns"])
    else:
        dt = 0
        for a, b in zip(t, t[1:]):
            dt = math.gcd(dt, b - a)
    return Capture(t, cols["scl"], cols["sda"], aux, dt, meta)


class I2CDecoder:
    def __init__(self, cap, mode="fast", stretch_factor=3.0,
                 stretch_timeout_ns=SMBUS_TIMEOUT_NS, coincident_rule="spec"):
        self.cap, self.mode, self.lim, self.dt = cap, mode, LIMITS[mode], cap.sample_ns
        self.stretch_timeout = stretch_timeout_ns
        self.rule = coincident_rule
        self.scl, self.sda = cap.scl[0], cap.sda[0]
        self.last_scl_fall = self.last_scl_rise = self.last_sda_t = self.last_sda_pol = None
        self.frame = None
        self.bits = []
        self.high_bit = False
        self.pending_start = None
        self.last_stop = None
        self.dec = Decoded()
        lows = self._all_lows()
        self.low_nom = statistics.median(lows) if lows else None
        self.stretch_limit = stretch_factor * self.low_nom if self.low_nom else None

    def _all_lows(self):
        c = self.cap
        lows, fall = [], None
        for i in range(1, len(c.t)):
            if c.scl[i] != c.scl[i - 1]:
                if c.scl[i] == 0:
                    fall = c.t[i]
                elif fall is not None:
                    lows.append(c.t[i] - fall)
        return lows

    def _next_scl_change(self, i):
        c = self.cap
        for j in range(i + 1, len(c.t)):
            if c.scl[j] != c.scl[i]:
                return c.t[j]
        return None

    def _flag(self, kind, t, measured=None, limit=None, verdict="INFO", owner=None, pol=None,
              byte=None, bit=None):
        fi = len(self.dec.frames) - 1
        self.dec.findings.append(dict(kind=kind, t=t, measured=measured, limit=limit, verdict=verdict,
                                      owner=owner, pol=pol, frame=fi, byte=byte, bit=bit))

    def _owner(self, idx):
        """슬롯 idx(0..8)를 누가 구동하는가. idx 8 = ACK."""
        f = self.frame
        master_tx = not f["bytes"] or f["rw"] == 0
        if idx < 8:
            return "master" if master_tx else "slave"
        return "slave" if master_tx else "master"

    def run(self):
        c = self.cap
        ps, pd = c.scl[0], c.sda[0]
        for i in range(1, len(c.t)):
            t, s, d = c.t[i], c.scl[i], c.sda[i]
            ds, dd = s != ps, d != pd
            if ds and dd:
                self.dec.coincident += 1
                if self.rule == "naive":       # 데모용: 항상 SDA 먼저 → 가짜 START/STOP
                    self._sda_change(i, t, d)
                    self._scl_rise(i, t) if s else self._scl_fall(i, t)
                elif s == 1:                   # SCL↑: SDA가 먼저 안정됐다고 본다
                    self._sda_change(i, t, d)
                    self._scl_rise(i, t)
                else:                          # SCL↓: SDA는 하강 뒤에 바뀐다고 본다
                    self._scl_fall(i, t)
                    self._sda_change(i, t, d)
            elif ds:
                self._scl_rise(i, t) if s else self._scl_fall(i, t)
            elif dd:
                self._sda_change(i, t, d)
            ps, pd = s, d
        self._eof()
        return self.dec

    def _scl_rise(self, i, t):
        """SCL↑: tLOW/stretch 판정, setup 판정, 슬롯 값 표본(bits, dec.slots).
        tLOW는 프레임 안에서만, low_nom의 stretch_factor배 이상이면 STRETCH(INFO)로 분류하고
        tLOW 판정은 건너뛴다. stretch_timeout 이상이면 STRETCH_TIMEOUT."""
        L = self.lim
        if self.frame is not None and self.last_scl_fall is not None:
            low = t - self.last_scl_fall
            if low >= self.stretch_timeout:
                self._flag("STRETCH_TIMEOUT", t, low, self.stretch_timeout)
            elif self.stretch_limit is not None and low >= self.stretch_limit:
                self._flag("STRETCH", t, low, self.stretch_limit)
            else:
                self.dec.lows.append(low)
                v = judge(low, L["tLOW"], self.dt)
                if v != "OK":
                    self._flag("TLOW", t, low, L["tLOW"], v, bit=len(self.bits))
        if self.frame is not None and len(self.bits) < 9:
            idx = len(self.bits)
            if self.last_sda_t is not None and self.last_scl_fall is not None \
                    and self.last_sda_t >= self.last_scl_fall:
                su = t - self.last_sda_t
                v = judge(su, L["tSU_DAT"], self.dt)
                if v != "OK":
                    self._flag("SETUP", t, su, L["tSU_DAT"], v, owner=self._owner(idx),
                               pol=self.last_sda_pol, byte=len(self.frame["bytes"]), bit=idx)
            self.bits.append(self.sda)
            self.dec.slots.append(dict(t=t, frame=len(self.dec.frames) - 1,
                                       byte=len(self.frame["bytes"]), bit=idx,
                                       owner=self._owner(idx), val=self.sda))
            self.high_bit = True
        self.last_scl_rise = t
        self.scl = 1

    def _scl_fall(self, i, t):
        """SCL↓: tHIGH, START 뒤 tHD;STA, 9비트가 차면 _finish_byte."""
        L = self.lim
        if self.frame is not None and self.last_scl_rise is not None:
            high = t - self.last_scl_rise
            self.dec.highs.append(high)
            v = judge(high, L["tHIGH"], self.dt)
            if v != "OK":
                self._flag("THIGH", t, high, L["tHIGH"], v, bit=len(self.bits))
        if self.pending_start is not None:
            hd = t - self.pending_start
            v = judge(hd, L["tHD_STA"], self.dt)
            if v != "OK":
                self._flag("THD_STA", t, hd, L["tHD_STA"], v)
            if self.frame is not None:
                self.frame["t_first_fall"] = t
            self.pending_start = None
        self.last_scl_fall = t
        self.scl = 0
        self.high_bit = False
        if self.frame is not None and len(self.bits) == 9:
            self._finish_byte(t)

    def _sda_change(self, i, t, new):
        """SDA 변화. SCL HIGH 구간이면 START/STOP 또는 hold 위반, LOW 구간이면 launch 기록.
        hold 위반 판정: 프레임 안 + 이번 HIGH에 비트를 표본함 + 다음 SCL↓까지 < 0.5·tHD;STA."""
        L = self.lim
        if self.scl == 1:
            nf = self._next_scl_change(i)
            if self.frame is not None and self.high_bit and nf is not None \
                    and nf - t < 0.5 * L["tHD_STA"]:
                idx = len(self.bits) - 1
                self._flag("HOLD", t, -(nf - t), 0, judge(-(nf - t), 0, self.dt),
                           owner=self._owner(idx), pol=new, byte=len(self.frame["bytes"]), bit=idx)
                self.sda, self.last_sda_t, self.last_sda_pol = new, t, new
                return
            if new == 0:
                self._start(t)
            else:
                self._stop(t)
        elif self.frame is not None and self.last_scl_fall is not None:
            idx = min(len(self.bits), 8)
            self.dec.launches.append((self._owner(idx), new, t - self.last_scl_fall))
        self.sda, self.last_sda_t, self.last_sda_pol = new, t, new

    def _discard_tentative(self, t):
        """STOP/rSTART 직전의 SCL↑은 다음 바이트의 비트가 아니다. 2비트 이상이면 잘린 프레임."""
        if len(self.bits) > 1:
            self._flag("ABORTED_FRAME", t, bit=len(self.bits),
                       byte=len(self.frame["bytes"]))
        elif len(self.bits) == 1:
            self.dec.slots.pop()
        self.bits = []
        self.high_bit = False

    def _start(self, t):
        L = self.lim
        rep = self.frame is not None
        if rep:
            self._discard_tentative(t)
            if self.last_scl_rise is not None:
                v = judge(t - self.last_scl_rise, L["tSU_STA"], self.dt)
                if v != "OK":
                    self._flag("TSU_STA", t, t - self.last_scl_rise, L["tSU_STA"], v)
            self.frame["end"] = "RSTART"
            self.frame["t_end"] = t
        elif self.last_stop is not None:
            v = judge(t - self.last_stop, L["tBUF"], self.dt)
            if v != "OK":
                self._flag("TBUF", t, t - self.last_stop, L["tBUF"], v)
        self.frame = dict(t_start=t, t_end=None, end=None, rstart=rep, addr=None, rw=None,
                          bytes=[], t_first_fall=None)
        self.dec.frames.append(self.frame)
        self.bits = []
        self.high_bit = False
        self.pending_start = t

    def _stop(self, t):
        L = self.lim
        if self.frame is None:
            return                       # 프레임 밖 SDA↑ (복구 뒤 해제 등)
        self._discard_tentative(t)
        if self.last_scl_rise is not None:
            v = judge(t - self.last_scl_rise, L["tSU_STO"], self.dt)
            if v != "OK":
                self._flag("TSU_STO", t, t - self.last_scl_rise, L["tSU_STO"], v)
        self.frame["end"] = "STOP"
        self.frame["t_end"] = t
        self.frame = None
        self.pending_start = None        # START 직후 STOP(글리치) 뒤에 남지 않게
        self.last_stop = t

    def _finish_byte(self, t):
        """9비트 완성: 값 조립, 첫 바이트는 주소/RW, NACK 분류."""
        f = self.frame
        val = 0
        for b in self.bits[:8]:
            val = (val << 1) | b
        ack = self.bits[8] == 0
        first = not f["bytes"]
        if first:
            f["addr"], f["rw"] = val >> 1, val & 1
        b_idx = len(f["bytes"])
        f["bytes"].append(dict(t=t, value=val, ack=ack, kind="addr" if first else "data"))
        if not first and f.get("read_nacked"):
            self._flag("READ_AFTER_NACK", t, byte=b_idx)
        if not ack:
            if first:
                self._flag("NACK_ADDR", t, owner="slave", byte=b_idx)
            elif f["rw"] == 0:
                self._flag("NACK_DATA", t, owner="slave", byte=b_idx)
            else:
                f["read_nacked"] = True       # 읽기 끝의 master NACK은 정상
        self.bits = []

    def _eof(self):
        t = self.cap.t[-1]
        if self.frame is not None:
            if len(self.bits) > 1:
                self._flag("ABORTED_FRAME", t, bit=len(self.bits), byte=len(self.frame["bytes"]))
            elif len(self.bits) == 1:
                self.dec.slots.pop()
            if self.scl == 0 and self.last_scl_fall is not None:
                low = t - self.last_scl_fall
                if low >= self.stretch_timeout:
                    self._flag("STRETCH_TIMEOUT", t, low, self.stretch_timeout)
                elif self.stretch_limit is not None and low >= self.stretch_limit:
                    self._flag("STRETCH", t, low, self.stretch_limit)
            self.frame["end"] = "EOF"
            self.frame["t_end"] = t
            self.frame = None


def decode_i2c(cap, mode="fast", **kw):
    return I2CDecoder(cap, mode, **kw).run()


def check_arbitration(cap, dec, devices):
    """구동(devices: aux 채널 이름들) vs 관측(SDA). 프레임마다 START에 참가한 마스터 중
    '자기는 1을 놓았는데 버스는 0'인 첫 master 슬롯을 진 시점으로 본다.
    ACK/읽기 데이터 슬롯(owner != master)은 비교에서 뺀다.
    반환 [dict(dev, frame, byte, bit, t)] ; 참가자 2명 이상이 모두 지면 dict(dev="*", ...CONTENTION) 추가."""
    out = []
    for fi, f in enumerate(dec.frames):
        if f["t_first_fall"] is None:
            continue
        part = [d for d in devices if cap.at(d, f["t_first_fall"]) == 0]
        lost = {}
        for s in dec.slots:
            if s["frame"] != fi or s["owner"] != "master":
                continue
            for d in part:
                if d not in lost and s["val"] == 0 and cap.at(d, s["t"]) == 1:
                    lost[d] = dict(dev=d, frame=fi, byte=s["byte"], bit=s["bit"], t=s["t"])
        out.extend(sorted(lost.values(), key=lambda e: e["t"]))
        if len(part) >= 2 and len(lost) == len(part):
            out.append(dict(dev="*", frame=fi, kind="CONTENTION", t=max(v["t"] for v in lost.values())))
    return out


# ---- Part B: 판정·필터·추정 ----
def judge(measured, limit, dt):
    """하한 limit 판정. LA 에지는 실제보다 [0,dt) 늦게 잡히므로 두 에지의 차 오차는 (-dt,+dt).
    measured+dt <= limit → VIOLATION, measured-dt >= limit → OK, 그 외 MARGINAL."""
    if measured + dt <= limit:
        return "VIOLATION"
    if measured - dt >= limit:
        return "OK"
    return "MARGINAL"


def deglitch(cap, min_ns):
    """SCL/SDA 각 라인에서 폭 min_ns 미만 펄스를 제거한 새 Capture.
    변화 시각을 스택으로 훑으며 직전 변화와 간격 < min_ns 이면 두 변화를 지운다.
    행은 남은 변화 시각의 합집합 + 첫 행 + 끝 행."""
    def line_changes(col):
        kept = []
        for i in range(1, len(cap.t)):
            if col[i] != col[i - 1]:
                if kept and cap.t[i] - kept[-1] < min_ns:
                    kept.pop()
                else:
                    kept.append(cap.t[i])
        return kept
    scl_ch, sda_ch = line_changes(cap.scl), line_changes(cap.sda)
    times = sorted(set([cap.t[0], cap.t[-1]] + scl_ch + sda_ch))

    def values(init, changes):
        out, v, j = [], init, 0
        for t in times:
            while j < len(changes) and changes[j] <= t:
                v ^= 1
                j += 1
            out.append(v)
        return out
    aux = {k: [cap.at(k, t) for t in times] for k in cap.aux}
    return Capture(times, values(cap.scl[0], scl_ch), values(cap.sda[0], sda_ch), aux,
                   cap.sample_ns, dict(cap.meta))


def estimate_rise_delay(dec, owner="master", min_count=4):
    """같은 드라이버의 launch 오프셋(SCL↓→SDA 변화)에서 median(상승) − median(하강).
    표본이 min_count 미만이면 None."""
    up = [o for who, pol, o in dec.launches if who == owner and pol == 1]
    dn = [o for who, pol, o in dec.launches if who == owner and pol == 0]
    if len(up) < min_count or len(dn) < min_count:
        return None
    return statistics.median(up) - statistics.median(dn)


# ---- Part C: 진단 ----
def find_bus_hang(cap, dec, stuck_ns=200_000):
    """상태가 stuck_ns 이상 유지된 첫 구간을 찾는다.
    SCL=1,SDA=0 → SDA_STUCK / SCL=0 → SCL_STUCK. 없으면 None.
    SDA_STUCK 뒤로 SCL 하강 수를 세며 SDA가 올라오는 시점까지 본다:
    dict(kind,t,dur,open,clocks,released,t_release,recovered,post_ok,frame_ctx)."""
    ch = [(cap.t[0], cap.scl[0], cap.sda[0])]
    for i in range(1, len(cap.t)):
        if (cap.scl[i], cap.sda[i]) != (ch[-1][1], ch[-1][2]):
            ch.append((cap.t[i], cap.scl[i], cap.sda[i]))
    ch.append((cap.t[-1], None, None))
    for i in range(len(ch) - 1):
        t, s, d = ch[i]
        dur = ch[i + 1][0] - t
        if dur < stuck_ns or (s == 1 and d == 1):
            continue
        hang = dict(kind="SDA_STUCK" if s == 1 else "SCL_STUCK", t=t, dur=dur,
                    open=ch[i + 1][1] is None, clocks=0, released=False,
                    t_release=None, recovered=False, post_ok=False)
        before = [f for f in dec.frames if f["t_start"] < t]
        hang["frame_ctx"] = before[-1] if before else None
        sl = [x for x in dec.slots if x["t"] <= t]
        hang["slot"] = sl[-1] if sl else None
        if hang["kind"] == "SDA_STUCK":
            prev_s = s
            for tt, ss, dd in ch[i + 1:]:
                if ss is None:
                    break
                if prev_s == 1 and ss == 0:
                    hang["clocks"] += 1
                prev_s = ss
                if dd == 1:
                    hang["released"], hang["t_release"] = True, tt
                    break
            hang["recovered"] = hang["released"] and hang["clocks"] <= 9
            if hang["released"]:
                hang["post_ok"] = any(f["t_start"] > hang["t_release"] and f["bytes"]
                                      and f["bytes"][0]["ack"] for f in dec.frames)
        return hang
    return None


def diagnose(cap, mode="fast", dec=None):
    """규칙 기반 원인 판정. 우선순위: hang → 약한 풀업 → 타이밍 위반 → NACK → CLEAN.
    반환 dict(cause, evidence[], next[], metrics{})."""
    dec = dec if dec is not None else decode_i2c(cap, mode)
    L, dt = LIMITS[mode], cap.sample_ns
    hang = find_bus_hang(cap, dec)
    delay = estimate_rise_delay(dec, "master")
    tr = tr_from_delay(delay, float(cap.meta.get("vth", 0.5))) if delay is not None else None
    n_nack = dec.count("NACK_ADDR") + dec.count("NACK_DATA")
    m = dict(frames=len(dec.frames), nack=n_nack, nack_addr=dec.count("NACK_ADDR"),
             rise_delay_ns=delay, tr_est_ns=tr,
             high_med=statistics.median(dec.highs) if dec.highs else None,
             low_med=statistics.median(dec.lows) if dec.lows else None,
             hang=hang)
    ev, nxt = [], []
    if hang is not None and hang["kind"] == "SDA_STUCK":
        f = hang["frame_ctx"]
        who = "0x%02X" % f["addr"] if f is not None and f["addr"] is not None else "?"
        ev.append("SCL=1, SDA=0 가 %.1f ms 지속 (t=%.3f ms부터)" % (hang["dur"] / 1e6, hang["t"] / 1e6))
        sl = hang["slot"]
        ev.append("직전 프레임: addr=%s rw=%s, %s 표본 직후 정지" %
                  (who, f["rw"] if f else "?", "바이트 #%d 비트 #%d" % (sl["byte"], sl["bit"]) if sl else "슬롯 없음"))
        ev.append("복구: SCL 하강 %d회 뒤 SDA 해제=%s (9회 이내=%s), 복구 뒤 정상 ACK=%s" %
                  (hang["clocks"], hang["released"], hang["recovered"], hang["post_ok"]))
        nxt.append("SDA 소유자는 LA로 구분 불가 → 직렬저항 양단 전압 측정 또는 %s 분리로 확인" % who)
        nxt.append("슬레이브 FSM의 중단 경로 확인(마스터 리셋·전력 이벤트 중 전송)")
        if not hang["recovered"]:
            nxt.append("9클럭으로 안 풀림 → 슬레이브 리셋 핀 또는 전원 사이클 필요")
        return dict(cause="SLAVE_HANG_SDA_LOW", evidence=ev, next=nxt, metrics=m)
    if hang is not None:
        ev.append("SCL=0 이 %.1f ms 지속 (타임아웃 정책 %.0f ms) — 프레임 안" %
                  (hang["dur"] / 1e6, SMBUS_TIMEOUT_NS / 1e6))
        nxt.append("SCL을 쥔 쪽 확인(LA로 구분 불가): 슬레이브 리셋 또는 분리")
        nxt.append("9클럭 복구는 SCL을 쥔 슬레이브에는 무효")
        return dict(cause="SCL_HELD_LOW", evidence=ev, next=nxt, metrics=m)
    if tr is not None and tr > L["tr_max"]:
        ev.append("SDA 상승 launch가 하강보다 %.0f ns 늦음 (master 드라이버 자기 기준, 표본 %d)" %
                  (delay, sum(1 for o, p, _ in dec.launches if o == "master" and p == 1)))
        ev.append("1차 RC 가정 추정 tr(0.3→0.7) ≈ %.0f ns > %d ns" % (tr, L["tr_max"]))
        ev.append("SCL HIGH 중앙값 %.0f ns, LOW 중앙값 %.0f ns (HIGH<->LOW 비대칭)" % (m["high_med"], m["low_med"]))
        if n_nack:
            ev.append("주소 NACK %d/%d 프레임 — 간헐" % (dec.count("NACK_ADDR"), m["frames"]))
        nxt.append("스코프로 SDA/SCL 0.3→0.7 VDD tr 직접 측정 (LA 값은 추정치)")
        nxt.append("Rp 값과 Cb 확인 (HWLAB-B03 윈도우), 약하면 Rp 낮춤")
        return dict(cause="WEAK_PULLUP", evidence=ev, next=nxt, metrics=m)
    viol = [f for f in dec.findings if f["kind"] in TIMING_KINDS and f["verdict"] == "VIOLATION"]
    if viol:
        for kind in TIMING_KINDS:
            sel = [f for f in viol if f["kind"] == kind]
            if sel:
                owners = sorted(set(str(f["owner"]) for f in sel))
                ev.append("%s %d건, 최악 측정 %d ns (limit %d ns, owner=%s)" %
                          (kind, len(sel), min(f["measured"] for f in sel), sel[0]["limit"], ",".join(owners)))
        sl = [o for who, _, o in dec.launches if who == "slave"]
        if sl:
            m["slave_launch_med"] = statistics.median(sl)
            ev.append("slave launch 중앙값 %.0f ns (tVD;DAT max %d ns)" % (m["slave_launch_med"], L["tVD_DAT"]))
        nxt.append("SCL 분주/duty 재설정 (Fast-mode tLOW 1.3 µs 때문에 400 kHz 50% duty는 불가)")
        nxt.append("슬레이브 데이터 launch 지연을 데이터시트 tVD;DAT와 비교")
        return dict(cause="TIMING_VIOLATION", evidence=ev, next=nxt, metrics=m)
    if n_nack:
        ev.append("NACK %d건, 타이밍 위반 없음" % n_nack)
        nxt.append("주소/전원/리셋 상태 확인")
        return dict(cause="NACK_ONLY", evidence=ev, next=nxt, metrics=m)
    ev.append("위반 없음")
    return dict(cause="CLEAN", evidence=ev, next=nxt, metrics=m)


def format_report(rep, name=""):
    lines = ["=== %s  cause=%s ===" % (name, rep["cause"])]
    for e in rep["evidence"]:
        lines.append("  근거: " + e)
    for n in rep["next"]:
        lines.append("  다음: " + n)
    return "\n".join(lines)


# ---- 추가 연습 ----
PASSED = 0
FAILED = 0


def CHECK(name, cond, detail=""):
    global PASSED, FAILED
    if cond:
        PASSED += 1
        print("PASS", name)
    else:
        FAILED += 1
        print("FAIL %s: %s" % (name, detail))


def cap_of(sim, **kw):
    return parse_la_csv(render_csv(sim, **kw))


# 연습 1: 프레임 → 레지스터 수준 트레이스
def reg_trace(dec):
    """연속 프레임을 펌웨어가 읽는 형태로 바꾼다.
    주소 NACK → 'NACK 0x60'
    쓰기 1바이트 후 같은 주소 rSTART 읽기 → 'RD 0x48[0x01] -> 12 34'
    쓰기 1바이트(읽기 안 따라옴) → 'PTR 0x48 <- 0x01'
    쓰기 2바이트 이상 → 'WR 0x50[0x01] <- A5 3C'
    단독 읽기 → 'RD 0x48 -> 12 34'"""
    out = []
    fr = dec.frames
    i = 0
    while i < len(fr):
        f = fr[i]
        data = [b["value"] for b in f["bytes"] if b["kind"] == "data"]
        if f["bytes"] and not f["bytes"][0]["ack"]:
            out.append("NACK 0x%02X" % f["addr"])
            i += 1
            continue
        hex_data = " ".join("%02X" % v for v in data)
        if f["rw"] == 0 and len(data) == 1 and i + 1 < len(fr) and fr[i + 1]["rstart"] \
                and fr[i + 1]["rw"] == 1 and fr[i + 1]["addr"] == f["addr"]:
            rd = " ".join("%02X" % b["value"] for b in fr[i + 1]["bytes"] if b["kind"] == "data")
            out.append("RD 0x%02X[0x%02X] -> %s" % (f["addr"], data[0], rd))
            i += 2
            continue
        if f["rw"] == 0 and len(data) == 1:
            out.append("PTR 0x%02X <- 0x%02X" % (f["addr"], data[0]))
        elif f["rw"] == 0 and data:
            out.append("WR 0x%02X[0x%02X] <- %s" % (f["addr"], data[0], " ".join("%02X" % v for v in data[1:])))
        elif f["rw"] == 1:
            out.append("RD 0x%02X -> %s" % (f["addr"], hex_data))
        i += 1
    return out


def test_p1():
    sim = BusSim()
    sim.write_then_read(0x48, 0x01, [0x12, 0x34])
    sim.write(0x50, [0x01, 0xA5, 0x3C])
    sim.write(0x50, [0x07])
    sim.write(0x60, [0x11], ack_addr=False)
    sim.read(0x48, [0xFF])
    got = reg_trace(decode_i2c(cap_of(sim)))
    want = ["RD 0x48[0x01] -> 12 34", "WR 0x50[0x01] <- A5 3C", "PTR 0x50 <- 0x07", "NACK 0x60", "RD 0x48 -> FF"]
    CHECK("P1.trace", got == want, str(got))


# 연습 2: SPI — 모드 추론과 오모드 디코드
def spi_wave(mode, payload, t_bit=500, dt=10, hold=30):
    """scl 열 = SCK, sda 열 = MOSI, aux cs. CPHA=0은 선행 에지 샘플·후행 에지 변화."""
    cpol, cpha = (mode >> 1) & 1, mode & 1
    t, ev = 1000, []
    ev.append((t, "cs", 0))
    sck = cpol
    for byte in payload:
        for i in range(7, -1, -1):
            bit = (byte >> i) & 1
            if cpha == 0:
                ev.append((t + hold, "mosi", bit))
                t += t_bit
                sck ^= 1
                ev.append((t, "sck", sck))            # 선행 = 샘플
                t += t_bit
                sck ^= 1
                ev.append((t, "sck", sck))            # 후행 = 변화
            else:
                t += t_bit
                sck ^= 1
                ev.append((t, "sck", sck))            # 선행 = 변화
                ev.append((t + hold, "mosi", bit))
                t += t_bit
                sck ^= 1
                ev.append((t, "sck", sck))            # 후행 = 샘플
    ev.append((t + 500, "cs", 1))
    ev.sort(key=lambda e: e[0])
    st = dict(sck=cpol, mosi=1, cs=1)
    rows = ["# sample_ns=%d" % dt, "time_ns,scl,sda,cs", "0,%d,1,1" % cpol]
    for tt, name, v in ev:
        st[name] = v
        rows.append("%d,%d,%d,%d" % (int(math.ceil(tt / dt)) * dt, st["sck"], st["mosi"], st["cs"]))
    rows.append("%d,%d,%d,%d" % (t + 2000, st["sck"], st["mosi"], st["cs"]))
    # 같은 시각 행 합치기
    out, last = [], None
    for line in rows:
        if line.startswith("#") or line.startswith("time"):
            out.append(line)
            continue
        tv = int(line.split(",")[0])
        if last == tv:
            out[-1] = line
        else:
            out.append(line)
        last = tv
    return "\n".join(out) + "\n"


def spi_edges(cap):
    """CS 구간의 SCK 에지 [(t, 선행?)] 과 MOSI 변화 시각."""
    i0 = next(i for i in range(len(cap.t)) if cap.aux["cs"][i] == 0)
    i1 = next(i for i in range(i0, len(cap.t)) if cap.aux["cs"][i] == 1)
    idle = cap.scl[i0]
    sck, mosi = [], []
    for i in range(i0 + 1, i1 + 1):
        if cap.scl[i] != cap.scl[i - 1]:
            sck.append((cap.t[i], cap.scl[i] != idle))
        if cap.sda[i] != cap.sda[i - 1]:
            mosi.append(cap.t[i])
    return idle, sck, mosi


def spi_infer_mode(cap, window=120):
    """CPOL = CS 낙하 시 SCK 유휴 레벨. MOSI가 '선행 에지 직후'에 바뀌면 CPHA=1, 후행 직후면 CPHA=0."""
    idle, sck, mosi = spi_edges(cap)
    lead = trail = 0
    for t in mosi:
        for ts, is_lead in sck:
            if 0 <= t - ts <= window:
                if is_lead:
                    lead += 1
                else:
                    trail += 1
                break
    cpha = 1 if lead > trail else 0
    return (idle << 1) | cpha


def spi_decode(cap, mode):
    """주어진 모드의 샘플 에지에서 MOSI를 표본해 바이트로."""
    idle, sck, _ = spi_edges(cap)
    cpha = mode & 1
    bits = []
    for t, is_lead in sck:
        sample_edge = is_lead if cpha == 0 else (not is_lead)
        if sample_edge:
            bits.append(cap.at("sda", t))
    out = []
    for i in range(0, len(bits) - 7, 8):
        v = 0
        for b in bits[i:i + 8]:
            v = (v << 1) | b
        out.append(v)
    return out


def test_p2():
    r = random.Random(5)
    for mode in range(4):
        data = [r.randint(0, 255) for _ in range(6)]
        cap = parse_la_csv(spi_wave(mode, data))
        CHECK("P2.mode%d.infer" % mode, spi_infer_mode(cap) == mode, str(spi_infer_mode(cap)))
        CHECK("P2.mode%d.decode" % mode, spi_decode(cap, mode) == data, str(spi_decode(cap, mode)))
        wrong = mode ^ 1
        got = spi_decode(cap, wrong)
        if mode & 1 == 0:      # CPHA=0을 CPHA=1로 오판 → 후행 에지 표본, 값은 맞지만 마진은 hold뿐
            CHECK("P2.mode%d.wrong_cpha_right_by_luck" % mode, got == data, str(got))
        else:                  # CPHA=1을 CPHA=0으로 오판 → 변화 직전 비트를 읽어 한 비트 밀림
            CHECK("P2.mode%d.wrong_cpha_garbled" % mode, got != data, str(got))


# 연습 3: 개방 루프 vs 피드백 마스터
def master_style(dec, cfg_low, cfg_high, tol):
    """설정값과 비교: HIGH만 줄고 LOW가 늘면 open_loop, HIGH는 유지되고 LOW만 늘면 closed_loop,
    둘 다 설정값이면 no_delay. 반환 (style, 추정 지연 ns)."""
    hi = statistics.median(dec.highs) - cfg_high
    lo = statistics.median(dec.lows) - cfg_low
    if abs(hi) <= tol and abs(lo) <= tol:
        return "no_delay", 0.0
    if hi < -tol and lo > tol:
        return "open_loop", lo
    if abs(hi) <= tol and lo > tol:
        return "closed_loop", lo
    return "unknown", lo


def test_p3():
    tau = 288.5
    delay = tau * math.log(2)
    sim = BusSim(t_low=1400, t_high=1100)
    sim.write(0x50, [0xA5, 0x5A])
    d = decode_i2c(cap_of(sim, tau_scl=tau, dt=20))
    st, est = master_style(d, 1400, 1100, 40)
    CHECK("P3.open_loop", st == "open_loop" and abs(est - delay) <= 2 * 20, "%s %.1f" % (st, est))
    sim = BusSim(t_low=1400 + 200, t_high=1100)      # 카운터가 SCL HIGH 감지 후 시작
    sim.write(0x50, [0xA5, 0x5A])
    d = decode_i2c(cap_of(sim, dt=20))
    st, est = master_style(d, 1400, 1100, 40)
    CHECK("P3.closed_loop", st == "closed_loop" and abs(est - 200) <= 2 * 20, "%s %.1f" % (st, est))
    sim = BusSim(t_low=1400, t_high=1100)
    sim.write(0x50, [0xA5])
    st, _ = master_style(decode_i2c(cap_of(sim, dt=20)), 1400, 1100, 40)
    CHECK("P3.no_delay", st == "no_delay", st)


# 연습 4: 간헐 실패 통계
def nack_stats(dec):
    """주소별 (프레임 수, 주소 NACK 수)."""
    s = {}
    for f in dec.frames:
        if not f["bytes"]:
            continue
        a = f["addr"]
        c = s.setdefault(a, [0, 0])
        c[0] += 1
        if not f["bytes"][0]["ack"]:
            c[1] += 1
    return s


def captures_needed(p, conf=0.95):
    """실패 확률 p 인 간헐 고장을 conf 확률로 한 번은 보려면 몇 번 돌려야 하나.
    (1-p)^N <= 1-conf → N >= ln(1-conf)/ln(1-p)."""
    return int(math.ceil(math.log(1.0 - conf) / math.log(1.0 - p)))


def test_p4():
    sim = BusSim()
    sim.write(0x48, [1], ack_addr=False)
    sim.write(0x48, [1])
    sim.write(0x50, [1], ack_addr=False)
    CHECK("P4.nack_stats", nack_stats(decode_i2c(cap_of(sim))) == {0x48: [2, 1], 0x50: [1, 1]},
          str(nack_stats(decode_i2c(cap_of(sim)))))
    CHECK("P4.needed_table", [captures_needed(0.5), captures_needed(0.1), captures_needed(0.01)] == [5, 29, 299],
          str([captures_needed(0.5), captures_needed(0.1), captures_needed(0.01)]))
    r = random.Random(11)
    p = 0.05
    N = captures_needed(p)
    hit = 0
    T = 4000
    for _ in range(T):
        if any(r.random() < p for _ in range(N)):
            hit += 1
    # Bernoulli 경험비, 4000회 표준오차 ≈ 0.0035 → 3σ 허용
    CHECK("P4.needed_empirical", abs(hit / T - 0.95) <= 0.012, "%.3f" % (hit / T))


def main():
    for t in (test_p1, test_p2, test_p3, test_p4):
        try:
            t()
        except Exception as e:
            CHECK(t.__name__ + ".raised", False, type(e).__name__ + ": " + str(e)[:120])
    print("%d/%d passed" % (PASSED, PASSED + FAILED))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
