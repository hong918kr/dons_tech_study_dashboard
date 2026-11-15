#!/usr/bin/env python3
"""SIV-A07 버스 인터페이스 실패 디버그 (LA 캡처 분석)

로직 애널라이저 CSV(time_ns, scl, sda[, aux...]) → I2C 디코더 → 위반 판정 → 원인 리포트.

  Part A  CSV 파서, 프로토콜 디코더(START/rSTART/STOP, 주소·R/W, ACK/NACK, 바이트),
          clock stretching·timeout, setup/hold·tLOW/tHIGH 위반, arbitration lost(구동 vs 관측)
  Part B  3값 판정(±1샘플), 샘플레이트 마진, 동시 에지 규칙, 글리치 필터,
          rise time을 '지연'으로만 볼 수 있다는 한계 + SDA 비대칭으로 자가 교정
  Part C  합성 캡처 3종(약한 풀업, 타이밍 위반, 슬레이브 hang) → 원인 리포트

  python3 -I solution.py                       # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --diagnose cap_x.csv  # 캡처 1개 원인 리포트

Python 3.9 표준 라이브러리만 쓴다. 시간은 전부 ns. 전압은 VDD로 정규화(0..1).
"""
import bisect
import hashlib
import json
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
    raise NotImplementedError("TODO(Part A): CSV 파서")


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
        raise NotImplementedError("TODO(Part A)")

    def _scl_fall(self, i, t):
        """SCL↓: tHIGH, START 뒤 tHD;STA, 9비트가 차면 _finish_byte."""
        raise NotImplementedError("TODO(Part A)")

    def _sda_change(self, i, t, new):
        """SDA 변화. SCL HIGH 구간이면 START/STOP 또는 hold 위반, LOW 구간이면 launch 기록.
        hold 위반 판정: 프레임 안 + 이번 HIGH에 비트를 표본함 + 다음 SCL↓까지 < 0.5·tHD;STA."""
        raise NotImplementedError("TODO(Part A)")

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
        raise NotImplementedError("TODO(Part A): START/rSTART")

    def _stop(self, t):
        raise NotImplementedError("TODO(Part A): STOP")

    def _finish_byte(self, t):
        """9비트 완성: 값 조립, 첫 바이트는 주소/RW, NACK 분류."""
        raise NotImplementedError("TODO(Part A)")

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
    raise NotImplementedError("TODO(Part A)")


# ---- Part B: 판정·필터·추정 ----
def judge(measured, limit, dt):
    """하한 limit 판정. LA 에지는 실제보다 [0,dt) 늦게 잡히므로 두 에지의 차 오차는 (-dt,+dt).
    measured+dt <= limit → VIOLATION, measured-dt >= limit → OK, 그 외 MARGINAL."""
    raise NotImplementedError("TODO(Part B)")


def deglitch(cap, min_ns):
    """SCL/SDA 각 라인에서 폭 min_ns 미만 펄스를 제거한 새 Capture.
    변화 시각을 스택으로 훑으며 직전 변화와 간격 < min_ns 이면 두 변화를 지운다.
    행은 남은 변화 시각의 합집합 + 첫 행 + 끝 행."""
    raise NotImplementedError("TODO(Part B)")


def estimate_rise_delay(dec, owner="master", min_count=4):
    """같은 드라이버의 launch 오프셋(SCL↓→SDA 변화)에서 median(상승) − median(하강).
    표본이 min_count 미만이면 None."""
    raise NotImplementedError("TODO(Part B)")


# ---- Part C: 진단 ----
def find_bus_hang(cap, dec, stuck_ns=200_000):
    """상태가 stuck_ns 이상 유지된 첫 구간을 찾는다.
    SCL=1,SDA=0 → SDA_STUCK / SCL=0 → SCL_STUCK. 없으면 None.
    SDA_STUCK 뒤로 SCL 하강 수를 세며 SDA가 올라오는 시점까지 본다:
    dict(kind,t,dur,open,clocks,released,t_release,recovered,post_ok,frame_ctx)."""
    raise NotImplementedError("TODO(Part C)")


def diagnose(cap, mode="fast", dec=None):
    """규칙 기반 원인 판정. 우선순위: hang → 약한 풀업 → 타이밍 위반 → NACK → CLEAN.
    반환 dict(cause, evidence[], next[], metrics{})."""
    raise NotImplementedError("TODO(Part C)")


def format_report(rep, name=""):
    lines = ["=== %s  cause=%s ===" % (name, rep["cause"])]
    for e in rep["evidence"]:
        lines.append("  근거: " + e)
    for n in rep["next"]:
        lines.append("  다음: " + n)
    return "\n".join(lines)


# ---- 테스트 하네스 ----
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


def simple(dec):
    return [(f["addr"], f["rw"], [b["value"] for b in f["bytes"] if b["kind"] == "data"],
             [b["ack"] for b in f["bytes"]]) for f in dec.frames]


def test_model_sanity():
    sim = BusSim()
    sim.write(0x50, [0x01])
    csv = render_csv(sim, dt=20)
    CHECK("model.csv_has_header", csv.splitlines()[2].startswith("time_ns,scl,sda"), csv[:40])
    CHECK("model.rle_small", len(csv) < 5000, str(len(csv)))
    CHECK("model.wired_and", steps([(0, "x", "a", 1), (5, "x", "b", 0), (9, "x", "b", 1)], "x") == [(5, 0), (9, 1)])
    cr = analog_cross([(0, 0), (1000, 1)], 288.5, 0.0, 0.5, 3000)
    CHECK("model.rc_delay", abs(cr[-1][0] - 1000 - 288.5 * math.log(2)) < 1e-6, str(cr))


def test_a_parse():
    c = parse_la_csv("# sample_ns=20\ntime_ns,scl,sda\n0,1,1\n40,0,1\n60,0,0\n")
    CHECK("A.parse.rows", c.t == [0, 40, 60] and c.scl == [1, 0, 0] and c.sda == [1, 1, 0], str(c.t))
    CHECK("A.parse.sample_meta", c.sample_ns == 20)
    c2 = parse_la_csv("TIME_NS,SDA,SCL\n0,1,0\n30,0,0\n90,1,1\n")
    CHECK("A.parse.column_order", c2.sda == [1, 0, 1] and c2.scl == [0, 0, 1], str(c2.sda))
    CHECK("A.parse.sample_gcd", c2.sample_ns == 30, str(c2.sample_ns))
    for label, bad, frag in [("nonmono", "time_ns,scl,sda\n10,1,1\n10,0,1\n", "line 3"),
                             ("badval", "time_ns,scl,sda\n0,1,1\n5,2,1\n", "line 3"),
                             ("nocol", "time_ns,scl\n0,1\n", "line 1"),
                             ("empty", "# nothing\n", "line 0")]:
        try:
            parse_la_csv(bad)
            CHECK("A.parse.reject_" + label, False, "no error")
        except ValueError as e:
            CHECK("A.parse.reject_" + label, frag in str(e), str(e))


def test_a_decode():
    sim = BusSim()
    sim.write(0x50, [0xA5, 0x3C])
    d = decode_i2c(cap_of(sim))
    CHECK("A.write.frame", simple(d) == sim.truth, str(simple(d)))
    CHECK("A.write.no_findings", d.findings == [], str(d.findings[:2]))
    CHECK("A.write.stop", d.frames[0]["end"] == "STOP", str(d.frames[0]["end"]))

    sim = BusSim()
    sim.write_then_read(0x48, 0x01, [0x12, 0x34])
    d = decode_i2c(cap_of(sim))
    CHECK("A.rstart.frames", simple(d) == sim.truth, str(simple(d)))
    CHECK("A.rstart.first_end", d.frames[0]["end"] == "RSTART" and d.frames[1]["rstart"], str(d.frames[0]["end"]))
    CHECK("A.read.master_nack_not_finding", d.findings == [], str(d.findings[:2]))

    sim = BusSim()
    sim.write(0x60, [0x11], ack_addr=False)
    d = decode_i2c(cap_of(sim))
    CHECK("A.nack_addr.flag", d.count("NACK_ADDR") == 1, str(d.findings))
    CHECK("A.nack_addr.frame", simple(d) == [(0x60, 0, [], [False])], str(simple(d)))
    sim = BusSim()
    sim.write(0x50, [1, 2, 3], data_acks=[True, False, True])
    d = decode_i2c(cap_of(sim))
    CHECK("A.nack_data.flag", d.count("NACK_DATA") == 1 and simple(d)[0][3] == [True, True, False], str(simple(d)))


def test_a_stretch():
    sim = BusSim()
    sim.write(0x50, [0xAA], stretch=20_000)
    d = decode_i2c(cap_of(sim))
    st = [f for f in d.findings if f["kind"] == "STRETCH"]
    CHECK("A.stretch.flag", len(st) == 1, str(d.findings))
    # 샘플 격자 오차 ±dt 두 번 → 허용 2·dt
    CHECK("A.stretch.duration", st and abs(st[0]["measured"] - (1600 + 20_000)) <= 2 * 20, str(st))
    CHECK("A.stretch.no_tlow_false", d.count("TLOW") == 0, str(d.findings))
    CHECK("A.stretch.decode_ok", simple(d) == sim.truth)

    sim = BusSim()
    sim.write(0x50, [0xAA], stretch=30_000_000)
    d = decode_i2c(cap_of(sim))
    CHECK("A.stretch_timeout.closed", d.count("STRETCH_TIMEOUT") == 1, str(d.findings))

    sim = BusSim()
    sim.start()
    sim.byte(0x50 << 1)
    sim.ack("s")
    sim.scl_hold_forever()
    d = decode_i2c(cap_of(sim, t_end=sim.t + 40_000_000))
    CHECK("A.stretch_timeout.open_ended", d.count("STRETCH_TIMEOUT") == 1 and d.frames[0]["end"] == "EOF",
          str(d.findings))


def test_a_timing():
    sim = BusSim(t_low=1100, t_high=500, hd_dat=200, hd_sta=700, su_sta=700, su_sto=700)
    sim.write(0x50, [0x5A])
    d = decode_i2c(cap_of(sim))
    CHECK("A.tlow.flag", d.count("TLOW", "VIOLATION") == 18 + 1, str(d.count("TLOW")))
    CHECK("A.thigh.flag", d.count("THIGH", "VIOLATION") == 18, str(d.count("THIGH")))
    CHECK("A.tlow.decode_still_ok", simple(d) == sim.truth)

    sim = BusSim()
    sim.start()
    sim.byte(0x50 << 1)
    sim.ack("s")
    sim.byte(0xAA, "m", launch=1600 - 60)
    sim.ack("s")
    sim.stop()
    d = decode_i2c(cap_of(sim, dt=10))
    su = [f for f in d.findings if f["kind"] == "SETUP"]
    CHECK("A.setup.count", len(su) == 8, str(len(su)))
    CHECK("A.setup.measured", all(abs(f["measured"] - 60) <= 10 for f in su), str(su[:1]))
    CHECK("A.setup.owner_master", all(f["owner"] == "master" for f in su))
    CHECK("A.setup.both_polarity", {f["pol"] for f in su} == {0, 1}, str({f["pol"] for f in su}))

    sim = BusSim()
    sim.start()
    sim.byte(0x50 << 1)
    sim.ack("s")
    sim.bit(1, "m")
    sim.bit(0, "m", launch=-50)             # SCL↓ 50 ns 전에 SDA가 떨어짐
    for _ in range(6):
        sim.bit(0, "m")
    sim.ack("s")
    sim.stop()
    d = decode_i2c(cap_of(sim, dt=10))
    hd = [f for f in d.findings if f["kind"] == "HOLD"]
    CHECK("A.hold.flag", len(hd) == 1 and hd[0]["verdict"] == "VIOLATION", str(d.findings))
    CHECK("A.hold.measured", hd and abs(hd[0]["measured"] + 50) <= 10, str(hd))
    CHECK("A.hold.no_false_start", len(d.frames) == 1 and d.frames[0]["bytes"][1]["value"] == 0x80,
          str(simple(d)))


def test_a_arbitration():
    def scen(release=True):
        sim = BusSim()
        sim.start()
        sim.set(sim.t - sim.hd_sta, "sda", "m2", 0)
        sim.owner = "m"
        lost = sim.arb_byte(0x50 << 1, 0x48 << 1, loser_release=release)
        sim.set(sim.t + sim.hd_dat, "sda", "m2", 1)      # 주소 뒤: ACK 슬롯은 슬레이브
        sim.set(sim.t + sim.hd_dat, "sda", "m", 1)
        sim.owner = None
        sim.ack("s")
        sim.byte(0x0F, "m2")
        sim.ack("s")
        sim.owner = "m2"
        sim.stop()
        return sim, lost
    sim, lost = scen()
    cap = cap_of(sim, aux={"m_sda": "m", "m2_sda": "m2"})
    d = decode_i2c(cap)
    res = check_arbitration(cap, d, ["m_sda", "m2_sda"])
    CHECK("A.arb.bus_decodes_winner", d.frames[0]["addr"] == 0x48, str(simple(d)))
    CHECK("A.arb.one_loser", len(res) == 1 and res[0]["dev"] == "m_sda", str(res))
    CHECK("A.arb.position", res and (res[0]["byte"], res[0]["bit"]) == (0, 2), str(res))
    CHECK("A.arb.model_truth", lost == "m", str(lost))
    CHECK("A.arb.ack_not_false", all(r["bit"] < 8 for r in res), str(res))

    sim2, _ = scen(release=False)
    cap2 = cap_of(sim2, aux={"m_sda": "m", "m2_sda": "m2"})
    res2 = check_arbitration(cap2, decode_i2c(cap2), ["m_sda", "m2_sda"])
    CHECK("A.arb.buggy_loser_contention", any(r.get("kind") == "CONTENTION" for r in res2), str(res2))

    sim3 = BusSim()
    sim3.read(0x48, [0x00, 0x00])
    cap3 = cap_of(sim3, aux={"m_sda": "m", "m2_sda": "m2"})
    res3 = check_arbitration(cap3, decode_i2c(cap3), ["m_sda", "m2_sda"])
    CHECK("A.arb.read_data_no_false", res3 == [], str(res3))


def test_a_fuzz():
    rng = random.Random(0xA07)
    bad = 0
    n = 0
    for seed in range(40):
        r = random.Random(seed * 7 + 1)
        sim = BusSim(t_low=r.randint(1400, 1800), t_high=r.randint(700, 1100),
                     hd_dat=r.randint(150, 900))
        for _ in range(r.randint(1, 6)):
            addr = r.randint(0x08, 0x77)
            data = [r.randint(0, 255) for _ in range(r.randint(1, 4))]
            kind = r.randint(0, 2)
            if kind == 0:
                acks = [True] * len(data)
                if r.random() < 0.2:
                    acks[r.randint(0, len(data) - 1)] = False
                sim.write(addr, data, ack_addr=r.random() > 0.15, data_acks=acks)
            elif kind == 1:
                sim.read(addr, data)
            else:
                sim.write_then_read(addr, r.randint(0, 255), data)
        cap = cap_of(sim, dt=rng.choice([5, 10, 20]))
        d = decode_i2c(cap)
        n += 1
        if simple(d) != sim.truth or any(f["kind"] in TIMING_KINDS for f in d.findings):
            bad += 1
    CHECK("A.fuzz.40_captures_exact", bad == 0, "bad=%d/%d" % (bad, n))

    sim = BusSim()
    sim.write_then_read(0x48, 0x00, [0xC3])
    dense = parse_la_csv(render_csv(sim, dt=50, dense=True))
    rle = parse_la_csv(render_csv(sim, dt=50, dense=False))
    a, b = decode_i2c(dense), decode_i2c(rle)
    CHECK("A.dense_vs_rle.same_frames", simple(a) == simple(b) == sim.truth, str(simple(a)))
    CHECK("A.dense_vs_rle.same_findings", a.findings == b.findings)


def margin_case(dt, phase, setup_bad=60, setup_edge=130):
    """데이터 비트마다 setup을 달리한 Fast-mode 캡처.
    바이트 0xAA: bit7 = 위반 주입(setup_bad), bit3 = 경계(setup_edge), 나머지는 정상(1300 ns)."""
    sim = BusSim()
    t_low = sim.t_low
    sim.start()
    sim.byte(0x50 << 1)
    sim.ack("s")
    bytes_out = []
    for _ in range(8):
        for i in range(7, -1, -1):
            v = (0xAA >> i) & 1
            if i == 7:
                sim.bit(v, "m", launch=t_low - setup_bad)
            elif i == 3:
                sim.bit(v, "m", launch=t_low - setup_edge)
            else:
                sim.bit(v, "m")
        sim.ack("s")
        bytes_out.append(0xAA)
    sim.stop()
    sim.truth.append((0x50, 0, bytes_out, [True] * 9))
    return sim, cap_of(sim, dt=dt, phase=phase)


def sweep_margin(dts):
    """dt별 phase 전수 → dict(dt → stats). 측정표는 md §5, 건전성 판정은 테스트."""
    out = {}
    for dt in dts:
        st = dict(n=0, decode_ok=0, bad_v=0, bad_m=0, bad_ok=0, edge_v=0, edge_m=0, edge_ok=0,
                  naive_false=0, good_v=0, good_total=0)
        for phase in range(0, dt, max(1, dt // 8)):
            sim, cap = margin_case(dt, phase)
            d = decode_i2c(cap)
            st["n"] += 1
            if simple(d) == sim.truth:
                st["decode_ok"] += 1
            seen = {}
            for f in d.findings:
                if f["kind"] == "SETUP":
                    seen[(f["byte"], f["bit"])] = f
            for b in range(1, 9):
                for bit, name in ((0, "bad"), (4, "edge")):
                    f = seen.get((b, bit))
                    if f is None:
                        st[name + "_ok"] += 1
                    elif f["verdict"] == "VIOLATION":
                        st[name + "_v"] += 1
                    else:
                        st[name + "_m"] += 1
                    if name == "edge" and f is not None and f["measured"] < 100:
                        st["naive_false"] += 1
                for bit in (1, 2, 3, 5, 6, 7):
                    st["good_total"] += 1
                    if (b, bit) in seen and seen[(b, bit)]["verdict"] == "VIOLATION":
                        st["good_v"] += 1
        out[dt] = st
    return out


def test_b_judge():
    for m, lim, dt, want in [(60, 100, 20, "VIOLATION"), (80, 100, 20, "VIOLATION"), (90, 100, 20, "MARGINAL"),
                             (100, 100, 20, "MARGINAL"), (119, 100, 20, "MARGINAL"), (120, 100, 20, "OK"),
                             (200, 100, 20, "OK"), (0, 100, 80, "VIOLATION"), (0, 100, 150, "MARGINAL")]:
        CHECK("B.judge.%d_%d_%d" % (m, lim, dt), judge(m, lim, dt) == want, judge(m, lim, dt))


def test_b_margin():
    sw = sweep_margin([5, 10, 20, 40, 80])
    for dt in (5, 10, 20, 40, 80):
        s = sw[dt]
        CHECK("B.margin.dt%d.decode_all" % dt, s["decode_ok"] == s["n"], str(s))
        CHECK("B.margin.dt%d.bad_never_ok" % dt, s["bad_ok"] == 0, str(s))
        CHECK("B.margin.dt%d.good_never_violation" % dt, s["good_v"] == 0, str(s))
        CHECK("B.margin.dt%d.edge_never_violation" % dt, s["edge_v"] == 0, str(s))
    CHECK("B.margin.dt20_bad_definite", sw[20]["bad_v"] == 8 * sw[20]["n"], str(sw[20]))
    CHECK("B.margin.dt40_bad_some_marginal", sw[40]["bad_m"] > 0, str(sw[40]))


def test_b_coincident():
    # dt=400: SDA launch 300 ns 는 SCL↓와 같은 샘플에 자주 떨어진다.
    seen = 0
    wrong = 0
    for phase in range(0, 400, 25):
        sim = BusSim()
        sim.write(0x50, [0x5A, 0xC3])
        cap = cap_of(sim, dt=400, phase=phase)
        d = decode_i2c(cap)
        seen += d.coincident
        if simple(d) != sim.truth:
            wrong += 1
    CHECK("B.coincident.occurs", seen > 0, str(seen))
    CHECK("B.coincident.spec_rule_decodes", wrong == 0, "wrong=%d" % wrong)
    bad = 0
    for phase in range(0, 400, 25):
        sim = BusSim()
        sim.write(0x50, [0x5A, 0xC3])
        if simple(decode_i2c(cap_of(sim, dt=400, phase=phase), coincident_rule="naive")) != sim.truth:
            bad += 1
    CHECK("B.coincident.naive_breaks", bad > 0, "bad=%d" % bad)
    # 하한: dt가 tHIGH(900)·tHD;STA(700)를 넘기면 펄스 자체가 사라진다.
    miss = 0
    for phase in range(0, 1000, 50):
        sim = BusSim()
        sim.write(0x50, [0x5A])
        if simple(decode_i2c(cap_of(sim, dt=1000, phase=phase))) != sim.truth:
            miss += 1
    CHECK("B.floor.dt1000_breaks_some_phase", miss > 0, str(miss))


def test_b_rise():
    for tau in (120.0, 300.0, 600.0):
        sim = BusSim()
        for v in (0x5A, 0xA5):
            sim.write(0x50, [v, (~v) & 0xFF])
        d = decode_i2c(cap_of(sim, tau_scl=tau, tau_sda=tau, tau_f=8.0, dt=10))
        truth = tau * math.log(2) - 8.0 * math.log(2)
        est = estimate_rise_delay(d)
        # 두 median 의 양자화 오차 < dt 각각 → 허용 2·dt
        CHECK("B.rise.tau%d.delay" % tau, est is not None and abs(est - truth) <= 2 * 10,
              "est=%s truth=%.1f" % (est, truth))
        CHECK("B.rise.tau%d.tr" % tau, abs(tr_from_delay(est) - tau * math.log(7 / 3)) <= 2 * 10 * math.log(7 / 3) / math.log(2) + 12,
              "tr=%.1f" % tr_from_delay(est))


def test_b_limits():
    # 한계 1: SCL만 보면 '약한 풀업'과 'duty 설정'을 구분 못 한다. 파형이 샘플까지 같다.
    tau = 288.5                        # 지연 ≈ tau·ln2 = 200 ns (τf=0)
    sim_w = BusSim(t_low=1400, t_high=1100)
    sim_w.write(0x50, [0xA5, 0x5A])
    weak = cap_of(sim_w, tau_scl=tau, tau_sda=0.0, tau_f=0.0, dt=20)
    delay = int(round(tau * math.log(2)))
    sim_d = BusSim(t_low=1400 + delay, t_high=1100 - delay)
    sim_d.write(0x50, [0xA5, 0x5A])
    duty = cap_of(sim_d, dt=20)
    scl_of = lambda c: [(c.t[i], c.scl[i]) for i in range(1, len(c.t)) if c.scl[i] != c.scl[i - 1]]
    CHECK("B.limit.duty_vs_pullup_scl_same", scl_of(weak)[:30] == scl_of(duty)[:30],
          str(scl_of(weak)[:4]) + " vs " + str(scl_of(duty)[:4]))
    # 그러나 SDA launch 비대칭이 있으면 구분된다 (같은 드라이버의 상승 vs 하강).
    w2 = decode_i2c(cap_of(sim_w, tau_scl=tau, tau_sda=tau, tau_f=0.0, dt=20))
    d2 = decode_i2c(duty)
    CHECK("B.limit.sda_asym_separates", estimate_rise_delay(w2) > 150 and abs(estimate_rise_delay(d2)) <= 40,
          "weak=%s duty=%s" % (estimate_rise_delay(w2), estimate_rise_delay(d2)))

    # 한계 2: LA 임계(0.5)는 넘었지만 디바이스 VIH(0.7)는 못 넘는 SCL 펄스 → LA 디코드는 깨끗.
    sim = BusSim(t_low=1400, t_high=900)
    sim.write(0x50, [0xA5])
    d = decode_i2c(cap_of(sim, tau_scl=900.0, tau_sda=120.0, tau_f=5.0, dt=20))
    peak = 1.0 - math.exp(-900.0 / 900.0)
    CHECK("B.limit.la_clean_but_vih_missed", simple(d) == sim.truth and peak < 0.7, "peak=%.3f" % peak)

    # 한계 3: 슬레이브 stretch vs 마스터 pause — 선 위 파형이 완전히 같다.
    s1 = BusSim()
    s1.write(0x50, [0xAA], stretch=5000)
    s2 = BusSim()
    s2.start()
    s2.byte(0x50 << 1)
    s2.ack("s")
    for i in range(7, -1, -1):
        s2.bit((0xAA >> i) & 1, "m", extra_low=5000 if i == 7 else 0)
    s2.ack("s")
    s2.stop()
    CHECK("B.limit.stretch_owner_unobservable", render_csv(s1, dt=20) == render_csv(s2, dt=20), "wire differs")

    # 한계 4: 샘플 주기보다 짧은 글리치는 샘플 phase에 따라 보이거나 안 보인다.
    seen = 0
    total = 0
    for phase in range(0, 100, 10):
        s = BusSim()
        s.write(0x50, [0xA5])
        t_g = s.t + 2000                    # STOP 뒤 유휴(SDA=1) 구간
        s.set(t_g, "sda", "g", 0)
        s.set(t_g + 40, "sda", "g", 1)
        c = cap_of(s, dt=100, phase=phase, t_end=t_g + 5000)
        total += 1
        if any(c.sda[i] == 0 and c.t[i] >= t_g - 100 for i in range(len(c.t))):
            seen += 1
    CHECK("B.limit.short_glitch_phase_dependent", 0 < seen < total, "seen=%d/%d" % (seen, total))


def test_b_deglitch():
    # SCL HIGH 구간에 40 ns SDA 글리치 → 필터 없으면 가짜 START/STOP, 필터 있으면 깨끗.
    sim = BusSim()
    sim.write(0x50, [0xFF])
    t_high_mid = None
    for t, ln, dev, lvl in sorted(sim.ev, key=lambda e: e[0]):
        if ln == "scl" and dev == "m" and lvl == 1 and t > 8000:
            t_high_mid = t + 400
            break
    sim.set(t_high_mid, "sda", "g", 0)
    sim.set(t_high_mid + 40, "sda", "g", 1)
    cap = cap_of(sim, dt=20)
    raw = decode_i2c(cap)
    CHECK("B.deglitch.raw_false_frames", simple(raw) != sim.truth, str(simple(raw)))
    clean = decode_i2c(deglitch(cap, 50))
    CHECK("B.deglitch.clean_decode", simple(clean) == sim.truth, str(simple(clean)))
    CHECK("B.deglitch.no_findings", clean.findings == [], str(clean.findings[:2]))


def load_truth():
    with open(os.path.join(HERE, "captures_truth.json"), "r", encoding="utf-8") as f:
        return json.load(f)


def load_cap(name):
    with open(os.path.join(HERE, name), "r", encoding="utf-8") as f:
        return parse_la_csv(f.read())


def test_c_reports():
    truth = load_truth()
    expect = {"cap_good.csv": "CLEAN", "cap_weak_pullup.csv": "WEAK_PULLUP",
              "cap_timing_violation.csv": "TIMING_VIOLATION", "cap_slave_hang.csv": "SLAVE_HANG_SDA_LOW"}
    for name, want in expect.items():
        rep = diagnose(load_cap(name))
        CHECK("C.%s.cause" % name, rep["cause"] == want, rep["cause"] + " " + str(rep["evidence"]))

    r = diagnose(load_cap("cap_weak_pullup.csv"))
    t = truth["cap_weak_pullup.csv"]
    CHECK("C.weak.nack_truth", r["metrics"]["nack_addr"] == t["nack_addr"], "%d vs %d" % (r["metrics"]["nack_addr"], t["nack_addr"]))
    # τ를 프레임마다 ±10 % 흔든 표본의 median이므로 허용 = 2·dt + 진짜의 8 %
    CHECK("C.weak.delay_vs_truth", abs(r["metrics"]["rise_delay_ns"] - t["delay_ns"]) <= 2 * 20 + 0.08 * t["delay_ns"],
          "%s vs %s" % (r["metrics"]["rise_delay_ns"], t["delay_ns"]))
    CHECK("C.weak.tr_exceeds_limit", r["metrics"]["tr_est_ns"] > 300, str(r["metrics"]["tr_est_ns"]))
    d = decode_i2c(load_cap("cap_weak_pullup.csv"))
    CHECK("C.weak.thigh_violation_seen", d.count("THIGH", "VIOLATION") > 0, str(d.count("THIGH", "VIOLATION")))
    CHECK("C.weak.la_decoded_right_address", all(f["addr"] == 0x48 for f in d.frames),
          str(sorted({f["addr"] for f in d.frames})))

    r = diagnose(load_cap("cap_timing_violation.csv"))
    d = decode_i2c(load_cap("cap_timing_violation.csv"))
    t = truth["cap_timing_violation.csv"]
    tl = [f for f in d.findings if f["kind"] == "TLOW"]
    CHECK("C.timing.tlow_all_clocks", d.count("TLOW", "VIOLATION") >= 0.9 * len(tl) > 0, str(len(tl)))
    su = [f for f in d.findings if f["kind"] == "SETUP" and f["verdict"] == "VIOLATION"]
    CHECK("C.timing.setup_slave_only", su and all(f["owner"] == "slave" for f in su), str(su[:1]))
    CHECK("C.timing.setup_both_polarity", {f["pol"] for f in su} == {0, 1}, str({f["pol"] for f in su}))
    # 자기 기준 비대칭이 작다 = 풀업 문제 아님 (임계 지연 차 = τ·ln2 - τf·ln2, 허용 2·dt+)
    CHECK("C.timing.tr_not_weak", r["metrics"]["tr_est_ns"] < 300, str(r["metrics"]["tr_est_ns"]))
    CHECK("C.timing.tlow_value", all(abs(f["measured"] - t["tlow_la_ns"]) <= 2 * 20 for f in tl),
          "%s %s" % (tl[0]["measured"], t["tlow_la_ns"]))

    r = diagnose(load_cap("cap_slave_hang.csv"))
    h = r["metrics"]["hang"]
    t = truth["cap_slave_hang.csv"]
    CHECK("C.hang.kind", h["kind"] == "SDA_STUCK", str(h))
    CHECK("C.hang.duration", abs(h["dur"] - t["stuck_ns"]) <= 2 * 20, "%d vs %d" % (h["dur"], t["stuck_ns"]))
    CHECK("C.hang.clocks", h["clocks"] == t["recovery_falls"], "%d vs %d" % (h["clocks"], t["recovery_falls"]))
    CHECK("C.hang.recovered", h["recovered"] and h["post_ok"], str(h))
    CHECK("C.hang.suspect", h["frame_ctx"]["addr"] == 0x48 and h["frame_ctx"]["rw"] == 1, str(h["frame_ctx"]["addr"]))
    CHECK("C.hang.position", (h["slot"]["byte"], h["slot"]["bit"]) == (t["slot_byte"], t["slot_bit"]), str(h["slot"]))

    good = decode_i2c(load_cap("cap_good.csv"))
    CHECK("C.good.no_findings", good.findings == [], str(good.findings[:2]))

    sim = BusSim()
    sim.start()
    sim.byte((0x48 << 1) | 1)
    sim.ack("s")
    sim.bit(0, "s")
    sim.slave_launch(0)
    sim.master_reset()
    sim.idle(1_000_000)
    sim.recovery_clocks([0] * 9, release=False)
    sim.idle(500_000)
    rr = diagnose(cap_of(sim, t_end=sim.t + 500_000))
    CHECK("C.hang_unrecoverable.cause", rr["cause"] == "SLAVE_HANG_SDA_LOW", rr["cause"])
    CHECK("C.hang_unrecoverable.not_recovered", not rr["metrics"]["hang"]["recovered"], str(rr["metrics"]["hang"]))
    CHECK("C.hang_unrecoverable.advice", any("전원" in n for n in rr["next"]), str(rr["next"]))

    sim = BusSim()
    sim.start()
    sim.byte(0x50 << 1)
    sim.ack("s")
    sim.scl_hold_forever()
    rs = diagnose(cap_of(sim, t_end=sim.t + 40_000_000))
    CHECK("C.scl_held.cause", rs["cause"] == "SCL_HELD_LOW", rs["cause"])
    CHECK("C.scl_held.no_9clock_advice", any("무효" in n for n in rs["next"]), str(rs["next"]))


def main():
    if len(sys.argv) == 3 and sys.argv[1] == "--diagnose":
        with open(sys.argv[2], "r", encoding="utf-8") as f:
            cap = parse_la_csv(f.read())
        print(format_report(diagnose(cap), os.path.basename(sys.argv[2])))
        return 0
    tests = [v for k, v in sorted(globals().items()) if k.startswith("test_")]
    order = ["test_model_sanity", "test_a_parse", "test_a_decode", "test_a_stretch", "test_a_timing",
             "test_a_arbitration", "test_a_fuzz", "test_b_judge", "test_b_margin", "test_b_coincident",
             "test_b_rise", "test_b_limits", "test_b_deglitch", "test_c_reports"]
    tests = [globals()[n] for n in order]
    for t in tests:
        try:
            t()
        except Exception as e:     # 스텁(NotImplementedError)·테스트 예외를 FAIL로
            CHECK(t.__name__ + ".raised", False, type(e).__name__ + ": " + str(e)[:120])
    print("%d/%d passed" % (PASSED, PASSED + FAILED))
    return 1 if FAILED else 0


if __name__ == "__main__":
    sys.exit(main())
