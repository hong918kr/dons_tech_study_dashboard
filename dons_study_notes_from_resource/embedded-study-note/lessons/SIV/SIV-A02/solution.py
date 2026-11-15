#!/usr/bin/env python3
"""SIV-A02 바운더리 스캔(EXTEST)으로 보드 연결 테스트

BSDL(핀↔셀)과 넷리스트를 읽어 EXTEST 패턴을 만들고, 결함을 심은 가상 보드의 응답으로 결함 넷을 진단한다.
  Part A  BSDL 부분집합 파서 · 넷 역할 분류 · walking-1 / 수정 카운팅 코드 · EXTEST 흐름 · 진단
  Part B  드라이버 충돌 방지(넷당 드라이버 1개, 제어 셀 공유, PRELOAD, hold 넷) · aliasing/confounding · 한계 측정
  Part C  VAL-R2 보드 캠페인: 2단계(카운팅 → 의심 넷만 walking) 진단과 디버그 리포트

  python3 -I solution.py              # 테스트 ("N/M passed", 실패 시 exit 1)
  python3 -I solution.py --report     # 테스트 + aliasing 측정 표 + VAL-R2 리포트 출력

Python 3.9 표준 라이브러리만 쓴다. TAP 상태 기계는 TOOL-A01 의 몫이고, 여기서는 IR/DR 스캔을 레지스터 수준으로 모델링한다.
BSDL/넷리스트는 gen_board.py 로 결정적으로 다시 만들 수 있다.
"""
import itertools
import os
import random
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))


class BsdlError(Exception):
    pass


class PlanError(Exception):
    pass


# ---------------------------------------------------------------- Part A: BSDL 부분집합
class Cell:
    """BOUNDARY_REGISTER 한 항목: num (ctype, port, func, safe [, ccell, disval, rslt])"""
    __slots__ = ("num", "ctype", "port", "func", "safe", "ccell", "disval", "rslt")

    def __init__(self, num, ctype, port, func, safe, ccell=None, disval=None, rslt=None):
        self.num, self.ctype, self.port, self.func, self.safe = num, ctype, port, func, safe
        self.ccell, self.disval, self.rslt = ccell, disval, rslt


class Port:
    """핀 하나가 쓰는 셀들. in_cell = 핀을 캡처하는 셀, out_cell = 핀을 구동하는 셀, ctrl = 그 출력의 제어 셀"""
    __slots__ = ("in_cell", "out_cell", "ctrl", "func", "rslt")

    def __init__(self):
        self.in_cell = self.out_cell = self.ctrl = self.func = self.rslt = None


class Device:
    def __init__(self, name):
        self.name, self.ref = name, None
        self.ir_len, self.length = 0, 0
        self.opcodes, self.pin_map, self.cells = {}, {}, []
        self._ports, self._disval = None, None

    def ports(self):
        """포트 이름 → Port (control / internal 셀은 포트가 아니므로 빠진다)"""
        if self._ports is None:
            ports = {}
            for c in self.cells:
                if c.port == "*":
                    continue
                p = ports.setdefault(c.port, Port())
                if c.func in ("input", "clock", "observe_only"):
                    p.in_cell = c.num
                elif c.func in ("output2", "output3", "bidir"):
                    p.out_cell, p.func, p.ctrl, p.rslt = c.num, c.func, c.ccell, c.rslt
                    if c.func == "bidir":  # 셀 하나가 구동과 캡처를 겸한다 (예: BC_7)
                        p.in_cell = c.num
            self._ports = ports
        return self._ports

    def disval(self):
        """제어 셀 번호 → 그 값이면 출력이 꺼지는 disable value"""
        if self._disval is None:
            self._disval = {c.ccell: c.disval for c in self.cells if c.ccell is not None}
        return self._disval


def strip_comments(text):
    return "\n".join(line.split("--", 1)[0] for line in text.splitlines())


def attr_value(clean, name):
    """attribute NAME of X : entity is <값>;  문자열 조각("..." & "...")은 이어 붙이고, 아니면 그대로"""
    m = re.search(r"attribute\s+%s\s+of\s+\w+\s*:\s*entity\s+is\s+(.*?);" % name, clean, re.I | re.S)
    if not m:
        return None
    strs = re.findall(r'"([^"]*)"', m.group(1))
    return "".join(strs) if strs else m.group(1).strip()


def parse_pin_map(clean):
    """PHYSICAL_PIN_MAP 기본값이 고른 PIN_MAP_STRING 을 읽어 물리 핀 → 포트. bit_vector 는 선언 방향(to/downto)대로 번호를 붙인다"""
    vec = {}
    for m in re.finditer(r"(\w+)\s*:\s*\w+\s+bit_vector\s*\(\s*(\d+)\s+(to|downto)\s+(\d+)\s*\)", clean, re.I):
        a, b = int(m.group(2)), int(m.group(4))
        vec[m.group(1)] = list(range(a, b + 1)) if m.group(3).lower() == "to" else list(range(a, b - 1, -1))
    m = re.search(r'PHYSICAL_PIN_MAP\s*:\s*string\s*:=\s*"(\w+)"', clean, re.I)
    if not m:
        return {}
    mc = re.search(r"constant\s+%s\s*:\s*PIN_MAP_STRING\s*:=\s*(.*?);" % m.group(1), clean, re.I | re.S)
    if not mc:
        return {}
    text = "".join(re.findall(r'"([^"]*)"', mc.group(1)))
    out = {}
    for it in re.finditer(r"(\w+)\s*:\s*(?:\(([^)]*)\)|(\w+))", text):
        if it.group(3) is not None:
            out[it.group(3)] = it.group(1)
        else:
            pins = [x.strip() for x in it.group(2).split(",")]
            for idx, pin in zip(vec.get(it.group(1), range(len(pins))), pins):
                out[pin] = "%s(%d)" % (it.group(1), idx)
    return out


CELL_RE = re.compile(r"(\d+)\s*\(\s*(\w+)\s*,\s*(\*|\w+(?:\(\d+\))?)\s*,\s*(\w+)\s*,\s*([01XxZz])\s*"
                     r"(?:,\s*(\d+)\s*,\s*([01])\s*,\s*(\w+)\s*)?\)")


def parse_cells(text):
    """BOUNDARY_REGISTER 문자열 → [Cell]. func 는 소문자, safe 는 대문자 문자 그대로, ccell/disval 은 int (없으면 None)"""
    # TODO: CELL_RE.finditer 로 항목을 돌며 Cell 을 만든다 (단독 항목은 앞 4개 필드만)
    cells = []
    for m in CELL_RE.finditer(text):
        ccell = int(m.group(6)) if m.group(6) is not None else None
        disval = int(m.group(7)) if m.group(7) is not None else None
        cells.append(Cell(int(m.group(1)), m.group(2), m.group(3), m.group(4).lower(), m.group(5).upper(),
                          ccell, disval, m.group(8)))
    return cells


def parse_opcodes(text):
    """INSTRUCTION_OPCODE 문자열 → {'EXTEST': ['0000', ...]}. 한 명령에 코드가 여럿일 수 있다"""
    # TODO: "NAME (bits[, bits...])" 를 정규식으로 잘라 이름은 대문자로
    out = {}
    for m in re.finditer(r"(\w+)\s*\(\s*([01]+(?:\s*,\s*[01]+)*)\s*\)", text):
        out[m.group(1).upper()] = [x.strip() for x in m.group(2).split(",")]
    return out


def parse_bsdl(text):
    clean = strip_comments(text)
    m = re.search(r"entity\s+(\w+)\s+is", clean, re.I)
    if not m:
        raise BsdlError("no entity")
    dev = Device(m.group(1))
    dev.pin_map = parse_pin_map(clean)
    v = attr_value(clean, "INSTRUCTION_LENGTH")
    dev.ir_len = int(v) if v else 0
    v = attr_value(clean, "BOUNDARY_LENGTH")
    dev.length = int(v) if v else 0
    dev.opcodes = parse_opcodes(attr_value(clean, "INSTRUCTION_OPCODE") or "")
    dev.cells = sorted(parse_cells(attr_value(clean, "BOUNDARY_REGISTER") or ""), key=lambda c: c.num)
    return dev


def lint_device(dev):
    """BSDL 일관성 검사. 문제마다 문자열 하나. 'E_' 로 시작하면 사용 불가, 'W_' 는 경고"""
    # TODO: 아래 항목을 차례로 검사한다
    # TODO: E_LENGTH  : 셀 개수 != BOUNDARY_LENGTH, 셀 번호가 0..L-1 연속이 아님
    # TODO: E_CCELL   : output3/bidir 인데 ccell 없음, 또는 ccell 이 control/controlr 셀이 아님
    # TODO: E_OPCODE  : EXTEST 없음, 코드 길이가 INSTRUCTION_LENGTH 와 다름
    # TODO: W_SAFE    : 제어 셀의 safe 비트가 disable value 와 다름 (X 는 허용)
    out = []
    nums = [c.num for c in dev.cells]
    if len(nums) != dev.length:
        out.append("E_LENGTH cells=%d BOUNDARY_LENGTH=%d" % (len(nums), dev.length))
    if nums != list(range(len(nums))):
        out.append("E_LENGTH cell numbers not contiguous")
    by = {c.num: c for c in dev.cells}
    for c in dev.cells:
        if c.func in ("output3", "bidir"):
            if c.ccell is None:
                out.append("E_CCELL cell %d (%s) has no control cell" % (c.num, c.port))
            elif c.ccell not in by or by[c.ccell].func not in ("control", "controlr"):
                out.append("E_CCELL cell %d (%s) control cell %s invalid" % (c.num, c.port, c.ccell))
    if "EXTEST" not in dev.opcodes:
        out.append("E_OPCODE no EXTEST")
    for name, codes in dev.opcodes.items():
        if any(len(x) != dev.ir_len for x in codes):
            out.append("E_OPCODE %s length != %d" % (name, dev.ir_len))
    disval = dev.disval()
    for c in dev.cells:
        if c.func in ("control", "controlr") and c.safe != "X" and c.num in disval and int(c.safe) != disval[c.num]:
            out.append("W_SAFE cell %d safe %s != disable value %d" % (c.num, c.safe, disval[c.num]))
    return out


# ---------------------------------------------------------------- 넷리스트와 체인 (고정 코드)
class Net:
    __slots__ = ("name", "pins", "others", "pull")

    def __init__(self, name):
        self.name, self.pins, self.others, self.pull = name, [], [], None


def parse_netlist(text, devs):
    """NET ref.pin ...  [pull=0|1]  ·  !hold NET 값. ref 가 BSDL 부품이면 (ref, 포트) 로, 아니면 others 로 넣는다"""
    nets, hold = {}, {}
    for line in text.splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        tok = line.split()
        if tok[0] == "!hold":
            hold[tok[1]] = int(tok[2])
            continue
        net = Net(tok[0])
        for t in tok[1:]:
            if t.startswith("pull="):
                net.pull = int(t[5:])
                continue
            ref, pin = t.split(".", 1)
            if ref in devs:
                port = devs[ref].pin_map.get(pin, pin)  # 물리 핀 번호 또는 포트 이름
                if port not in devs[ref].ports():
                    raise BsdlError("net %s: %s.%s has no boundary cell" % (net.name, ref, pin))
                net.pins.append((ref, port))
            else:
                net.others.append(t)
        nets[net.name] = net
    return nets, hold


def to_stream(bits_by_ref, chain):
    """장치별 비트 → 시프트 순서. cell 0 이 TDO 에 가장 가까우므로 TDO 쪽 장치의 cell 0 이 먼저 들어가 끝까지 간다"""
    out = []
    for d in reversed(chain):
        out.extend(bits_by_ref[d.ref])
    return out


def from_stream(stream, chain):
    """to_stream 의 역. TDO 로 나온 비트열도 같은 순서다 (TDO 쪽 장치 cell 0 이 맨 먼저 나온다)"""
    out, i = {}, 0
    for d in reversed(chain):
        out[d.ref] = list(stream[i:i + d.length])
        i += d.length
    return out


def ir_stream(chain, name):
    """IR 도 같은 체인 순서. BSDL 코드는 오른쪽 비트가 TDO 쪽이라 먼저 시프트한다"""
    out = []
    for d in reversed(chain):
        out.extend(int(c) for c in reversed(d.opcodes[name][0]))
    return out


# ---------------------------------------------------------------- 가상 보드 (고정 코드 — 정답 도구가 검사하는 대상)
class VirtualBoard:
    """레지스터 수준 모델: load_ir = IR 스캔 + Update-IR, shift_dr = Capture-DR → 시프트 → Update-DR.

    결함: open(핀이 넷에서 떨어짐) · dead(핀의 출력단 고장) · stuck(넷이 전원/GND 에 붙음) · short(넷끼리 and/or/dom:<넷>).
    update 래치는 전원 투입 시 쓰레기 값이다. PRELOAD 없이 EXTEST 에 들어가면 그 값으로 구동한다."""

    def __init__(self, chain, nets, float_default=1, seed=1234):
        self.chain, self.nets, self.float_default = chain, nets, float_default
        self.devs = {d.ref: d for d in chain}
        rng = random.Random(seed)
        self.update = {d.ref: [rng.randrange(2) for _ in range(d.length)] for d in chain}
        self.instr = {d.ref: "BYPASS" for d in chain}
        self.opens, self.dead, self.stuck, self.shorts = set(), set(), {}, []
        self.pin_value, self.last = {}, {}
        self.contention = []  # (scan, 넷들, 충돌한 드라이버, 결함으로 인한 것인가)
        self.n_scan, self.n_bits = 0, 0
        self.toggles = {}  # 넷 → (마지막 값, 값이 바뀐 횟수)  hold 검증용
        self.undriven = {}  # 넷 → EXTEST 상태에서 드라이버가 없었던 이벤트 수
        self._settle()

    def add_open(self, net, ref, port):
        self.opens.add((net, ref, port))

    def add_dead(self, net, ref, port):
        self.dead.add((net, ref, port))

    def add_stuck(self, net, value):
        self.stuck[net] = value

    def add_short(self, nets, kind):
        self.shorts.append((set(nets), kind))

    def load_ir(self, stream):
        expect = sum(d.ir_len for d in self.chain)
        if len(stream) != expect:
            raise ValueError("IR length %d != chain %d (실제 보드라면 조용히 어긋난다)" % (len(stream), expect))
        i = 0
        for d in reversed(self.chain):
            op = "".join(str(b) for b in reversed(stream[i:i + d.ir_len]))
            i += d.ir_len
            self.instr[d.ref] = "BYPASS"
            for name, codes in d.opcodes.items():
                if op in codes and name in ("EXTEST", "SAMPLE", "PRELOAD", "BYPASS"):
                    self.instr[d.ref] = name
        self._settle()

    def _dr_len(self, d):
        return d.length if self.instr[d.ref] != "BYPASS" else 1

    def shift_dr(self, stream):
        """Capture-DR(핀 → 캡처) → Shift → Update-DR(입력 → 래치, EXTEST 면 핀으로). 돌아오는 건 *이번 Update 이전* 핀 값이다"""
        self.n_scan += 1
        self.n_bits += len(stream)
        cap = {}
        for d in self.chain:
            if self.instr[d.ref] == "BYPASS":
                cap[d.ref] = [0]
                continue
            bits = [0] * d.length  # 출력/제어 셀의 캡처는 코어 쪽 값이다. 핀 정보가 아니므로 여기서는 0
            for port, p in d.ports().items():
                if p.in_cell is not None:
                    bits[p.in_cell] = self._read(d.ref, port)
            cap[d.ref] = bits
        total = sum(self._dr_len(d) for d in self.chain)
        if len(stream) != total:
            raise ValueError("DR length %d != chain %d (실제 보드라면 조용히 어긋난다)" % (len(stream), total))
        out, i = [], 0
        for d in reversed(self.chain):
            n = self._dr_len(d)
            out.extend(cap[d.ref])
            if self.instr[d.ref] != "BYPASS":
                self.update[d.ref] = list(stream[i:i + n])
            i += n
        self._settle()
        return out

    def _drive(self, ref, port):
        d = self.devs[ref]
        if self.instr[ref] != "EXTEST":
            return None
        p = d.ports()[port]
        if p.out_cell is None:
            return None
        if p.ctrl is not None and self.update[ref][p.ctrl] == d.disval()[p.ctrl]:
            return None
        return self.update[ref][p.out_cell]

    def _pin_float(self, ref, port):
        """넷과 끊긴 핀(또는 구동 없는 넷)이 읽는 값: 핀 자신의 disable result(pull) → 보드 기본값('K' = 키퍼: 직전 값)"""
        r = (self.devs[ref].ports()[port].rslt or "").lower()
        if r in ("pull1", "weak1"):
            return 1
        if r in ("pull0", "weak0"):
            return 0
        if r == "keeper" or self.float_default == "K":
            return self.last.get((ref, port), 0)
        return self.float_default

    def _net_float(self, name):
        net = self.nets[name]
        if net.pull is not None:
            return net.pull
        if net.pins:
            return self._pin_float(*net.pins[0])
        return 0 if self.float_default == "K" else self.float_default

    def _groups(self):
        parent = {n: n for n in self.nets}

        def find(x):
            while parent[x] != x:
                parent[x] = parent[parent[x]]
                x = parent[x]
            return x
        kinds = {}
        for s, kind in self.shorts:
            s = sorted(s)
            for a, b in zip(s, s[1:]):
                parent[find(b)] = find(a)
        out = {}
        for n in self.nets:
            out.setdefault(find(n), []).append(n)
        for s, kind in self.shorts:
            kinds[find(sorted(s)[0])] = kind
        return [(g, kinds.get(root)) for root, g in out.items()]

    def _settle(self):
        for group, kind in self._groups():
            drive = []
            for name in group:
                for pin in self.nets[name].pins:
                    ref, port = pin
                    if (name, ref, port) in self.opens or (name, ref, port) in self.dead:
                        continue
                    v = self._drive(ref, port)
                    if v is not None:
                        drive.append((name, pin, v))
            vals = {v for _, _, v in drive}
            if len(vals) > 1:
                self.contention.append((self.n_scan, tuple(sorted(group)), tuple(drive), len(group) > 1))
            stuck = [self.stuck[n] for n in group if n in self.stuck]
            if stuck:
                value = stuck[0]
            elif not drive:
                value = None
            elif kind == "or":
                value = int(any(v for _, _, v in drive))
            elif kind and kind.startswith("dom:"):
                dom = [v for n, _, v in drive if n == kind[4:]]
                value = dom[0] if dom else drive[0][2]
            else:  # and, 또는 단일 넷 위 드라이버 충돌의 모델값
                value = int(all(v for _, _, v in drive))
            for name in group:
                float_value = self._net_float(name)
                for pin in self.nets[name].pins:
                    ref, port = pin
                    if (name, ref, port) in self.opens:
                        own = self._drive(ref, port)  # 끊긴 핀: 자기 출력이 살아 있으면 패드는 자기 값을 본다
                        v = own if own is not None and (name, ref, port) not in self.dead else self._pin_float(ref, port)
                    else:
                        v = value if value is not None else float_value
                    self.pin_value[pin] = v
                    self.last[pin] = v
                if value is None and any(v == "EXTEST" for v in self.instr.values()):
                    self.undriven[name] = self.undriven.get(name, 0) + 1
                # 토글 카운트(패드가 아니라 넷 관점)
                v_net = value if value is not None else float_value
                prev = self.toggles.get(name)
                if prev is None:
                    self.toggles[name] = (v_net, 0)
                else:
                    self.toggles[name] = (v_net, prev[1] + (1 if prev[0] != v_net else 0))

    def _read(self, ref, port):
        return self.pin_value.get((ref, port), self._pin_float(ref, port))


# ---------------------------------------------------------------- Part A: 넷 역할, 코드, EXTEST
def net_roles(nets, devs):
    """넷 → {'pins': [...], 'drivers': [...], 'receivers': [...], 'kind': ...}
    kind: testable(드라이버 + 다른 핀의 캡처) · drive_only · observe_only(드라이버 없음) · unscannable(셀 없음)"""
    # TODO: pins=넷의 BSDL 핀, drivers=out_cell 있는 핀, receivers=in_cell 있는 핀 (양방향은 둘 다).
    # TODO: 드라이버 d 와 다른 핀 r 이 있어 r 이 캡처할 수 있으면 testable.
    out = {}
    for name, net in nets.items():
        drivers = [(r, p) for r, p in net.pins if devs[r].ports()[p].out_cell is not None]
        receivers = [(r, p) for r, p in net.pins if devs[r].ports()[p].in_cell is not None]
        if not net.pins:
            kind = "unscannable"
        elif not drivers:
            kind = "observe_only"
        elif any(rx != d for rx in receivers for d in drivers):
            kind = "testable"
        else:
            kind = "drive_only"
        out[name] = {"pins": list(net.pins), "drivers": drivers, "receivers": receivers, "kind": kind}
    return out


def ceil_log2(n):
    k = 0
    while (1 << k) < n:
        k += 1
    return k


def gen_walking_1(n):
    """(코드 리스트, 벡터 수). 코드 i 의 비트 k = 벡터 k 에서 넷 i 가 구동할 값"""
    # TODO: 넷 i 는 벡터 i 에서만 1
    return [1 << i for i in range(n)], n


def gen_walking_0(n):
    """walking-1 의 보수"""
    # TODO: 넷 i 는 벡터 i 에서만 0
    ones = (1 << n) - 1
    return [ones ^ (1 << i) for i in range(n)], n


def gen_counting_plain(n):
    """Kautz 식 단순 카운팅: 코드 0..n-1, 벡터 ceil(log2 n) — 전부 0 인 코드가 있다"""
    k = max(1, ceil_log2(n))
    return list(range(n)), k


def gen_counting_modified(n):
    """수정 카운팅: 코드 1..n, 벡터 ceil(log2(n+2)) — 전부 0 / 전부 1 코드가 없다"""
    # TODO: k = ceil_log2(n+2), 코드는 1..n
    k = ceil_log2(n + 2)
    return list(range(1, n + 1)), k


def gen_true_complement(n):
    """카운팅과 그 보수를 이어 붙인다: 코드 (c, ~c), 벡터 2*ceil(log2 n)"""
    # TODO: 코드 i 의 아래 k 비트 = i, 위 k 비트 = ~i
    k = max(1, ceil_log2(n))
    mask = (1 << k) - 1
    return [i | ((mask ^ i) << k) for i in range(n)], 2 * k


class Plan:
    """driver: 패턴을 구동할 넷 → 그 핀 하나 · test: 진단 대상 넷 · hold: 고정값으로 묶을 넷 · unsafe: (넷, 이유)"""

    def __init__(self):
        self.driver, self.test, self.hold, self.unsafe = {}, [], {}, []


def assign_codes(plan, gen):
    names = sorted(plan.test)
    codes, k = gen(len(names))
    return dict(zip(names, codes)), k


def safe_image(chain, plan=None):
    """모든 셀을 안전값으로: 제어 셀 = disable value, 나머지 = safe 비트 (X 면 0).
    plan 을 주면 hold 넷의 드라이버는 켜고 hold 값을 싣는다 — EXTEST 진입 순간부터 그 넷이 떠 있으면 안 되기 때문"""
    # TODO: 장치별 비트 리스트. 어떤 출력에도 안 묶인 제어 셀은 safe 비트를 따른다. plan.hold 의 드라이버는 켠 채로 값을 싣는다
    out = {}
    for d in chain:
        dis = d.disval()
        bits = []
        for c in d.cells:
            if c.func in ("control", "controlr") and c.num in dis:
                bits.append(dis[c.num])
            else:
                bits.append(1 if c.safe == "1" else 0)
        out[d.ref] = bits
    if plan is not None:
        devs = {d.ref: d for d in chain}
        for net, value in plan.hold.items():
            ref, port = plan.driver[net]
            p = devs[ref].ports()[port]
            out[ref][p.out_cell] = value
            if p.ctrl is not None:
                out[ref][p.ctrl] = 1 - devs[ref].disval()[p.ctrl]
    return out


def vector_bits(plan, chain, codes, k):
    """벡터 k 의 장치별 비트: 안전값 위에, 계획된 드라이버만 켜고 코드 비트를 싣는다 (hold 넷은 고정값)"""
    # TODO: safe_image(chain, plan) 에서 시작 → plan.driver 를 돌며 out_cell = 값, ctrl = 1 - disable value
    devs = {d.ref: d for d in chain}
    out = safe_image(chain, plan)
    for net, (ref, port) in plan.driver.items():
        d = devs[ref]
        p = d.ports()[port]
        out[ref][p.out_cell] = plan.hold[net] if net in plan.hold else (codes[net] >> k) & 1
        if p.ctrl is not None:
            out[ref][p.ctrl] = 1 - d.disval()[p.ctrl]
    return out


def run_extest(board, plan, roles, codes, n_vec):
    """PRELOAD(안전값) → EXTEST → 벡터 n_vec 개 → 안전값 플러시. 반환: 넷 → {핀: 응답 STV(int)}
    응답은 한 스캔 늦게 나온다: 벡터 k 의 응답은 스캔 k+1 의 Capture 에서 잡힌다."""
    # TODO: 위 순서대로 load_ir / shift_dr. 캡처는 in_cell 이 있는 핀만 읽는다 (출력 전용 셀의 캡처는 핀 값이 아니다)
    chain = board.chain
    devs = {d.ref: d for d in chain}
    board.load_ir(ir_stream(chain, "PRELOAD"))
    board.shift_dr(to_stream(safe_image(chain, plan), chain))
    board.load_ir(ir_stream(chain, "EXTEST"))
    resp = {net: {pin: 0 for pin in roles[net]["receivers"]} for net in plan.test}
    for k in range(n_vec + 1):
        bits = vector_bits(plan, chain, codes, k) if k < n_vec else safe_image(chain, plan)
        out = from_stream(board.shift_dr(to_stream(bits, chain)), chain)
        if k > 0:
            for net in plan.test:
                for pin in resp[net]:
                    ref, port = pin
                    resp[net][pin] |= out[ref][devs[ref].ports()[port].in_cell] << (k - 1)
    return resp


def stv(v, n):
    """STV 를 시간 순서(벡터 0 이 왼쪽) 문자열로"""
    return "".join(str((v >> k) & 1) for k in range(n))


def classify_const(net, drv, rb, code, roles):
    """모든 rx 가 같은 상수를 읽을 때: 드라이버 read-back 으로 stuck 과 open 을 가른다"""
    rx = [p for p in roles[net]["receivers"] if p != drv]
    if rb is None:
        return "STUCK_OR_OPEN"
    if rb == code:
        return "OPEN_DRV" if len(rx) >= 2 else "OPEN_NET"
    return "STUCK"


def diagnose(plan, roles, codes, n_vec, resp):
    """응답 → finding 리스트. finding: {'kind','nets','pin','exp','got','maybe','short_kind','rb'}
      OPEN_RX        일부 수신 핀만 틀림 → 그 핀(들)
      OPEN_NET/OPEN_DRV/STUCK/STUCK_OR_OPEN   모든 수신이 같은 상수 → read-back 으로 구분
      SHORT          같은 틀린 응답을 공유하는 넷들 (maybe = 그 응답과 코드가 같은 멀쩡해 보이는 넷: aliasing)
      SHORT_OR_STUCK 2개 이상이 같은 상수 응답 → 단락인지 각각 stuck 인지 이 패턴만으로 모른다
      UNRESOLVED     모델로 설명 안 됨"""
    # TODO: 1) 넷마다 drv(계획된 드라이버), rx 응답, read-back 을 나눈다
    # TODO: 2) 일부 rx 만 틀리면 OPEN_RX(핀별 finding)
    # TODO: 3) 모든 rx 가 틀리고 값이 같으면 bad[net] = 그 값 — 응답값으로 묶어 SHORT / SHORT_OR_STUCK / 단일 판정
    ones = (1 << n_vec) - 1
    findings, bad, rb_of = [], {}, {}
    for net in sorted(plan.test):
        code, drv = codes[net], plan.driver[net]
        rx = {p: v for p, v in resp[net].items() if p != drv}
        rb_of[net] = resp[net].get(drv)
        wrong = [p for p, v in rx.items() if v != code]
        if not wrong:
            continue
        if len(wrong) < len(rx):
            for p in sorted(wrong):
                findings.append({"kind": "OPEN_RX", "nets": [net], "pin": p, "exp": code, "got": rx[p]})
            continue
        vals = {rx[p] for p in wrong}
        if len(vals) > 1:
            findings.append({"kind": "UNRESOLVED", "nets": [net], "pin": None, "exp": code, "got": rx[sorted(wrong)[0]]})
            continue
        bad[net] = vals.pop()
    code_to_net = {c: n for n, c in codes.items()}
    groups = {}
    for net, r in bad.items():
        groups.setdefault(r, []).append(net)
    for r, g in sorted(groups.items(), key=lambda kv: sorted(kv[1])[0]):
        g = sorted(g)
        constant = r in (0, ones)
        alias = code_to_net.get(r)
        maybe = [alias] if alias is not None and alias not in g else []
        if len(g) >= 2:
            f = {"kind": "SHORT_OR_STUCK" if constant else "SHORT", "nets": g, "pin": None, "got": r, "maybe": maybe,
                 "exp": None}
            if not constant:
                and_r, or_r = ones, 0
                for n in g + maybe:
                    and_r &= codes[n]
                    or_r |= codes[n]
                f["short_kind"] = "and" if r == and_r else ("or" if r == or_r else "?")
            findings.append(f)
        elif constant:
            findings.append({"kind": classify_const(g[0], plan.driver[g[0]], rb_of[g[0]], codes[g[0]], roles), "nets": g, "pin": None,
                             "got": r, "exp": codes[g[0]], "rb": rb_of[g[0]]})
        elif maybe:
            findings.append({"kind": "SHORT", "nets": g, "pin": None, "got": r, "maybe": maybe, "exp": codes[g[0]],
                             "short_kind": "alias"})
        else:
            findings.append({"kind": "UNRESOLVED", "nets": g, "pin": None, "got": r, "exp": codes[g[0]]})
    return findings


# ---------------------------------------------------------------- Part B: 드라이버 선택 (충돌 안전)
def plan_drivers(roles, devs, prefer=None, hold=None):
    """테스트/hold 넷마다 드라이버를 정확히 하나 고른다.
    - 제어 셀을 공유하는 핀은 같이 켜진다: 켜진 제어 셀의 형제 핀은 전부 자기 넷의 드라이버여야 한다
    - output2 는 끌 수 없다: 그 핀이 드라이버가 되어야 하고, 한 넷에 둘이면 unsafe 로 빼고 시험하지 않는다
    - hold 넷은 hold 값으로 묶고 진단하지 않는다. prefer = 드라이버 우선 ref 순서"""
    # TODO: 후보 드라이버를 prefer 순서로 정렬해 백트래킹. 한 넷의 선택이 제어 셀 형제 핀을 통해 다른 넷을 강제한다.
    # TODO: 드라이버가 없는 hold 넷은 건너뛴다. 끝까지 못 고르면 PlanError.
    hold = hold or {}
    prefer = list(prefer or [])
    plan = Plan()
    plan.hold = {n: v for n, v in hold.items() if roles[n]["drivers"]}
    nets = sorted(n for n, r in roles.items() if n in plan.hold or r["kind"] == "testable")

    def port(pin):
        return devs[pin[0]].ports()[pin[1]]

    def always_on(pin):
        return port(pin).ctrl is None

    usable = []
    for n in nets:
        if sum(always_on(p) for p in roles[n]["drivers"]) >= 2:
            plan.unsafe.append((n, "two always-on drivers"))
        else:
            usable.append(n)
    netset = set(usable)
    pin2net = {pin: n for n, r in roles.items() for pin in r["pins"]}
    groups = {}
    for ref, d in devs.items():
        for pname, p in d.ports().items():
            if p.ctrl is not None:
                groups.setdefault((ref, p.ctrl), []).append((ref, pname))

    def ctrl_of(pin):
        c = port(pin).ctrl
        return None if c is None else (pin[0], c)

    def place(net, pin, assign, on):
        stack = [(net, pin)]
        while stack:
            n, p = stack.pop()
            if assign.get(n, p) != p:
                return False
            assign[n] = p
            c = ctrl_of(p)
            if c is None or c in on:
                continue
            on.add(c)
            for s in groups[c]:
                m = pin2net.get(s)
                if m is None:
                    continue  # 무접속 핀은 켜져도 아무것도 안 건드린다
                if m not in netset:
                    return False
                stack.append((m, s))
        for n, p in assign.items():
            for d2 in roles[n]["drivers"]:
                if d2 != p and (always_on(d2) or ctrl_of(d2) in on):
                    return False
        return True

    def options(net):
        ds = roles[net]["drivers"]
        pinned = [d for d in ds if always_on(d)]
        if pinned:
            return pinned
        rank = {r: i for i, r in enumerate(prefer)}
        return sorted(ds, key=lambda d: (rank.get(d[0], len(prefer)), d))

    def dfs(i, assign, on):
        while i < len(usable) and usable[i] in assign:
            i += 1
        if i == len(usable):
            return assign
        for pin in options(usable[i]):
            a2, o2 = dict(assign), set(on)
            if place(usable[i], pin, a2, o2):
                res = dfs(i + 1, a2, o2)
                if res is not None:
                    return res
        return None

    res = dfs(0, {}, set())
    if res is None:
        raise PlanError("no safe driver assignment")
    plan.driver = res
    plan.test = [n for n in usable if n not in plan.hold and any(r != plan.driver[n] for r in roles[n]["receivers"])]
    return plan


# ---------------------------------------------------------------- Part B: aliasing / confounding (코드 수준)
def short_read(codes, groups, kind):
    """단락 그룹이 같은 값을 읽는 코드 수준 모델. kind = 'and' | 'or'"""
    read = list(codes)
    for g in groups:
        v = None
        for i in g:
            v = codes[i] if v is None else (v & codes[i] if kind == "and" else v | codes[i])
        for i in g:
            read[i] = v
    return read


def diagnose_codes(codes, n_vec, read):
    """코드 수준 진단: (추정 그룹 집합, 모호 여부). 같은 틀린 응답을 공유하는 넷 + 그 응답과 코드가 같은 넷(alias)을 한 그룹으로 본다.
    응답이 전부 0 / 전부 1 이면 stuck 과 구별할 수 없어 모호(ambiguous)로 친다."""
    # TODO: 틀린 넷을 응답값으로 묶고, code→넷 표로 alias 넷을 더한다. 상수 응답이면 ambiguous=True
    ones = (1 << n_vec) - 1
    failing = {}
    for i, c in enumerate(codes):
        if read[i] != c:
            failing.setdefault(read[i], set()).add(i)
    code_to_net = {c: i for i, c in enumerate(codes)}
    groups, ambiguous = set(), False
    for r, g in failing.items():
        if r == 0 or r == ones:
            ambiguous = True
        alias = code_to_net.get(r)
        if alias is not None:
            g = g | {alias}
        groups.add(frozenset(g))
    return groups, ambiguous


def connect_groups(v1, v0, n):
    """walk-1 응답 v1[j], walk-0 응답 v0[j] (j = 0..n-1, n+1 비트: 비트 n = 배경 벡터) 로부터 (단락 그룹들, 고정 넷들).
    고정 넷 = 두 walk 모두 같은 상수"""
    # TODO: j 가 i 를 따라가면(벡터 i 에서 v1 의 비트가 1, 또는 v0 의 비트가 0) i, j 를 union. 고정 넷은 union 에서 뺀다
    ones = (1 << (n + 1)) - 1  # 벡터 n 은 배경 벡터 (walk-1 은 전부 0, walk-0 은 전부 1)
    stuck = {j for j in range(n) if v1[j] == v0[j] and v1[j] in (0, ones)}
    parent = list(range(n))

    def find(x):
        while parent[x] != x:
            parent[x] = parent[parent[x]]
            x = parent[x]
        return x
    for i in range(n):
        for j in range(n):
            if i == j or i in stuck or j in stuck:
                continue
            if (v1[j] >> i) & 1 or not (v0[j] >> i) & 1:
                parent[find(j)] = find(i)
    out = {}
    for j in range(n):
        if j not in stuck:
            out.setdefault(find(j), set()).add(j)
    return [frozenset(s) for s in out.values() if len(s) >= 2], stuck


def adaptive_codes(codes, n_vec, groups, kind):
    """코드 수준 2단계 진단: 1단계 응답으로 의심 넷 S 를 고르고 S 만 walking → (그룹 집합, 총 벡터 수). 2단계 = 2(|S|+1) 벡터"""
    read = short_read(codes, groups, kind)
    found, _ = diagnose_codes(codes, n_vec, read)
    suspects = sorted(set().union(*found)) if found else []
    if not suspects:
        return set(), n_vec
    pos = {net: i for i, net in enumerate(suspects)}
    n = len(suspects)
    ones = (1 << (n + 1)) - 1
    sub = [[pos[i] for i in g if i in pos] for g in groups]  # 단락은 S 안에서만 보인다
    v1 = short_read([1 << i for i in range(n)], sub, kind)
    v0 = short_read([ones ^ (1 << i) for i in range(n)], sub, kind)
    grp, _ = connect_groups(v1, v0, n)
    return {frozenset(suspects[i] for i in g) for g in grp}, n_vec + 2 * (n + 1)


def study(n=14, seed=20261108, samples=400):
    """넷 n 개에서 결함 종류·코드 방식별 정확 진단률과 벡터 수를 잰다. exact = 추정 그룹 == 심은 그룹 이고 모호하지 않음"""
    rng = random.Random(seed)
    pairs = list(itertools.combinations(range(n), 2))
    double = []
    while len(double) < samples:
        a, b = rng.sample(pairs, 2)
        if not set(a) & set(b):
            double.append((a, b))
    triples = list(itertools.combinations(range(n), 3))
    scenarios = [("pair-AND", [[p] for p in pairs], "and"), ("pair-OR", [[p] for p in pairs], "or"),
                 ("2pairs-AND", [list(d) for d in double], "and"), ("2pairs-OR", [list(d) for d in double], "or"),
                 ("triple-AND", [[t] for t in triples], "and")]
    schemes = [("counting-mod", gen_counting_modified, False), ("walking-1", gen_walking_1, False),
               ("walking-0", gen_walking_0, False), ("true-compl", gen_true_complement, False),
               ("2-step", gen_counting_modified, True)]
    table = {}
    for sname, gen, adaptive in schemes:
        codes, k = gen(n)
        row = {}
        total_vec, total_n = 0, 0
        for fname, cases, kind in scenarios:
            exact = detected = 0
            for groups in cases:
                truth = {frozenset(g) for g in groups}
                read = short_read(codes, groups, kind)
                detected += any(r != c for r, c in zip(read, codes))
                if adaptive:
                    got, vec = adaptive_codes(codes, k, groups, kind)
                    exact += got == truth
                else:
                    got, amb = diagnose_codes(codes, k, read)
                    vec = k
                    exact += (got == truth and not amb)
                total_vec += vec
                total_n += 1
            row[fname] = (exact, detected, len(cases))
        row["avg_vectors"] = total_vec / total_n
        table[sname] = row
    return table


# ---------------------------------------------------------------- Part C: 2단계 진단과 리포트
def resolve_shorts(plan, roles, suspects, r1, r0, skip_pins):
    """2단계 walking 응답(walk-1, walk-0)으로 suspects 의 단락 그룹과 종류를 확정. 반환: (findings, cleared 넷들)"""
    # TODO: 넷마다 신뢰할 rx 하나(skip_pins 제외) 값을 v1/v0 로 뽑아 connect_groups 호출 → 그룹별 and/or/dom 판정.
    # TODO: 고정 넷은 STUCK(또는 read-back 없으면 STUCK_OR_OPEN), 어디에도 안 묶인 넷은 cleared.
    names = list(suspects)
    n = len(names)
    ones = (1 << (n + 1)) - 1  # 비트 n = 배경 벡터

    def read(resp, net):
        drv = plan.driver[net]
        for pin in sorted(resp[net]):
            if pin != drv and pin not in skip_pins:
                return resp[net][pin]
        return resp[net].get(drv, 0)
    v1 = [read(r1, x) for x in names]
    v0 = [read(r0, x) for x in names]
    groups, stuck = connect_groups(v1, v0, n)
    findings, seen = [], set()
    for g in sorted(groups, key=lambda s: min(s)):
        mask = 0
        for i in g:
            mask |= 1 << i
        if all(v1[j] == 0 and v0[j] == ones ^ mask for j in g):
            kind = "and"
        elif all(v1[j] == mask and v0[j] == ones for j in g):
            kind = "or"
        else:
            dom = [names[d] for d in g if v1[d] == 1 << d and v0[d] == ones ^ (1 << d)]
            kind = "dom:%s" % dom[0] if dom else "?"
        findings.append({"kind": "SHORT", "nets": sorted(names[i] for i in g), "pin": None, "short_kind": kind, "maybe": [],
                         "exp": None, "got": None})
        seen.update(names[i] for i in g)
    for j in sorted(stuck):
        findings.append({"kind": "STUCK", "nets": [names[j]], "pin": None, "exp": None, "got": v1[j]})
        seen.add(names[j])
    return findings, [x for x in names if x not in seen]


def adaptive_test(board, plan, roles, codes, k):
    """2단계 진단. 반환: (최종 findings, {'vectors': 총 벡터, 'step1': k, 'suspects': [...]})"""
    # TODO: 1단계: run_extest + diagnose. SHORT / SHORT_OR_STUCK / alias 가 있으면 그 넷들(+maybe)을 S 로 모아
    # TODO: 2단계: S 만 walking-1(배경 0, +전부 0 벡터), walking-0(배경 1, +전부 1 벡터) 을 run_extest 로 돌려 resolve_shorts.
    # TODO: 단락 외 finding(OPEN_*, STUCK 등)은 그대로 둔다
    resp = run_extest(board, plan, roles, codes, k)
    first = diagnose(plan, roles, codes, k, resp)
    keep = [f for f in first if f["kind"] not in ("SHORT", "SHORT_OR_STUCK")]
    suspect = sorted({n for f in first if f["kind"] in ("SHORT", "SHORT_OR_STUCK") for n in f["nets"] + f.get("maybe", [])})
    info = {"step1": k, "suspects": suspect, "vectors": k}
    if not suspect:
        return first, info
    n = len(suspect)
    ones = (1 << (n + 1)) - 1  # 벡터 n = 배경(전부 0 / 전부 1). 이게 없으면 |S|=2 의 wired-AND 가 stuck-at-0 처럼 보인다
    w1 = {x: 0 for x in plan.test}
    w0 = {x: ones for x in plan.test}
    for i, x in enumerate(suspect):
        w1[x] = 1 << i
        w0[x] = ones ^ (1 << i)
    r1 = run_extest(board, plan, roles, w1, n + 1)
    r0 = run_extest(board, plan, roles, w0, n + 1)
    skip = {f["pin"] for f in first if f["kind"] == "OPEN_RX"}
    resolved, cleared = resolve_shorts(plan, roles, suspect, r1, r0, skip)
    for f in resolved:
        if f["kind"] == "STUCK":
            # 1단계 read-back 을 살린다
            net = f["nets"][0]
            drv = plan.driver[net]
            rb = resp[net].get(drv)
            f["kind"] = classify_const(net, drv, rb, codes[net], roles)
            f["exp"] = codes[net]
            f["got"] = resp[net][sorted(p for p in resp[net] if p != drv)[0]]
    for x in cleared:
        if any(f["kind"] in ("SHORT", "SHORT_OR_STUCK") and x in f["nets"] for f in first):
            keep.append({"kind": "UNRESOLVED", "nets": [x], "pin": None, "exp": codes[x], "got": None})
    info["vectors"] = k + 2 * (n + 1)
    info["cleared"] = cleared
    return keep + resolved, info


def format_report(title, chain, plan, roles, codes, k, findings, info, board=None):
    """디버그 엔지니어용 텍스트 리포트 (문자열 리스트). 결함 1건 = FAIL 1줄: net / class / pin / exp / got"""
    # TODO: 헤더(체인, 넷 분류 수, 벡터 수), FAIL 줄들, 제외된 넷(hold, observe_only, unscannable), 판정 줄
    lines = ["=== %s ===" % title]
    lines.append("chain TDI -> " + " -> ".join("%s(%s,%db)" % (d.ref, d.name, d.length) for d in chain) +
                 " -> TDO  length=%d" % sum(d.length for d in chain))
    kinds = {}
    for r in roles.values():
        kinds[r["kind"]] = kinds.get(r["kind"], 0) + 1
    lines.append("nets total=%d " % len(roles) + " ".join("%s=%d" % kv for kv in sorted(kinds.items())) +
                 " | tested=%d held=%d" % (len(plan.test), len(plan.hold)))
    lines.append("vectors step1=%d step2=%d suspects=%s cleared=%s" % (k, info["vectors"] - k, ",".join(info["suspects"]) or "-",
                                                                       ",".join(info.get("cleared", [])) or "-"))
    for f in findings:
        pin = "%s.%s" % f["pin"] if f.get("pin") else "-"
        s = "FAIL net=%s class=%s pin=%s" % ("+".join(f["nets"]), f["kind"], pin)
        if f.get("short_kind"):
            s += " type=%s" % f["short_kind"]
        if f.get("exp") is not None:
            s += " exp=%s" % stv(f["exp"], k)
        if f.get("got") is not None:
            s += " got=%s" % stv(f["got"], k)
        if f.get("rb") is not None:
            s += " readback=%s" % stv(f["rb"], k)
        if f.get("maybe"):
            s += " maybe=%s" % ",".join(f["maybe"])
        lines.append(s)
    excl = sorted(n for n, r in roles.items() if r["kind"] in ("observe_only", "unscannable", "drive_only") and n not in plan.hold)
    lines.append("not tested: %s  held: %s" % (",".join(excl) or "-", ",".join("%s=%d" % kv for kv in sorted(plan.hold.items())) or "-"))
    if board is not None:
        fights = len([c for c in board.contention if c[3]])
        lines.append("driver fights during test (because of shorts): %d events" % fights)
    lines.append("VERDICT %s (%d findings)" % ("FAIL" if findings else "PASS", len(findings)))
    return lines


# ---------------------------------------------------------------- 테스트 하네스
PASSED = [0]
TOTAL = [0]


def CHECK(name, cond, detail=""):
    TOTAL[0] += 1
    if cond:
        PASSED[0] += 1
        print("PASS %s" % name)
    else:
        print("FAIL %s%s" % (name, (": " + str(detail)) if detail != "" else ""))
    return bool(cond)


def load_text(name):
    with open(os.path.join(HERE, name), encoding="utf-8") as f:
        return f.read()


def load_board():
    devs = {}
    for ref, fn in (("U1", "u1_sns_soc.bsd"), ("U2", "u2_io_fpga.bsd"), ("U3", "u3_lvl_buf.bsd")):
        d = parse_bsdl(load_text(fn))
        d.ref = ref
        devs[ref] = d
    nets, hold = parse_netlist(load_text("board.net"), devs)
    chain = [devs["U1"], devs["U2"], devs["U3"]]
    return devs, chain, nets, hold


TINY = '''
-- 주석은 지워야 한다  "0 (BC_1, FAKE, input, X)"
entity TINY is
  generic (PHYSICAL_PIN_MAP : string := "QFN8");
  port (TDI : in bit; TDO : out bit; TMS : in bit; TCK : in bit;
        PA0 : inout bit; PB : inout bit_vector(1 downto 0));
  use STD_1149_1_2001.all;
  attribute PIN_MAP of TINY : entity is PHYSICAL_PIN_MAP;
  constant QFN8 : PIN_MAP_STRING := "TDI:1, TDO:2, TMS:3, TCK:4, PA0:5, PB:(6,7)";
  ATTRIBUTE instruction_length OF TINY : ENTITY IS 2;
  attribute INSTRUCTION_OPCODE of TINY : entity is
    "BYPASS (11)," & "EXTEST (00, 10), SAMPLE (01), PRELOAD (01)";
  attribute BOUNDARY_LENGTH of TINY : entity is 5;
  attribute BOUNDARY_REGISTER of TINY : entity is
    "0 (BC_1, PA0, output3, X, 1, 0, Z)," &
    "1 (BC_1, *, control, 0)," &
    "2 (BC_1, PA0, input, X)," &
    "3 (BC_1, PB(0), output2, 1)," &
    "4 (BC_7, PB(1), bidir, X, 1, 0, Z)";
end TINY;
'''


def tiny(sub=None):
    text = TINY
    for a, b in (sub or []):
        text = text.replace(a, b)
    d = parse_bsdl(text)
    d.ref = "T"
    return d


def t_parse():
    d = tiny()
    CHECK("A1 BSDL lengths", d.length == 5 and d.ir_len == 2, (d.length, d.ir_len))
    CHECK("A1 five cells parsed (comment ignored)", len(d.cells) == 5, len(d.cells))
    c = d.cells[0]
    CHECK("A1 cell 0 fields", (c.ctype, c.port, c.func, c.safe, c.ccell, c.disval, c.rslt) ==
          ("BC_1", "PA0", "output3", "X", 1, 0, "Z"), (c.ctype, c.port, c.func, c.safe, c.ccell, c.disval, c.rslt))
    CHECK("A1 cell 3 output2 no control", d.cells[3].ccell is None and d.cells[3].safe == "1" and d.cells[3].func == "output2")
    CHECK("A1 opcodes (multi-code, case)", d.opcodes.get("EXTEST") == ["00", "10"] and d.opcodes.get("SAMPLE") == ["01"], d.opcodes)
    p = d.ports()
    CHECK("A1 PA0 in/out/ctrl", (p["PA0"].in_cell, p["PA0"].out_cell, p["PA0"].ctrl) == (2, 0, 1))
    CHECK("A1 bidir BC_7 is both in and out", p["PB(1)"].in_cell == 4 and p["PB(1)"].out_cell == 4)
    CHECK("A1 pin map downto order", d.pin_map.get("6") == "PB(1)" and d.pin_map.get("7") == "PB(0)" and d.pin_map.get("5") == "PA0",
          d.pin_map)
    CHECK("A1 lint clean", lint_device(d) == [], lint_device(d))


LINT_CASES = [
    ("length mismatch", [("entity is 5;", "entity is 6;")], "E_LENGTH"),
    ("cell gap", [('"2 (BC_1, PA0, input, X)," &', '"7 (BC_1, PA0, input, X)," &')], "E_LENGTH"),
    ("missing control cell", [("PB(1), bidir, X, 1, 0, Z)", "PB(1), bidir, X)")], "E_CCELL"),
    ("ccell not control", [("output3, X, 1, 0, Z", "output3, X, 2, 0, Z")], "E_CCELL"),
    ("opcode length", [("EXTEST (00, 10)", "EXTEST (000)")], "E_OPCODE"),
    ("no EXTEST", [("EXTEST (00, 10), ", "")], "E_OPCODE"),
    ("safe != disable", [('"1 (BC_1, *, control, 0)," &', '"1 (BC_1, *, control, 1)," &')], "W_SAFE"),
]


def t_lint():
    for name, sub, want in LINT_CASES:
        out = lint_device(tiny(sub))
        CHECK("A2 lint: %s -> %s" % (name, want), any(x.startswith(want) for x in out), out)
    devs, chain, nets, hold = load_board()
    for d in chain:
        CHECK("A2 data file lint clean %s" % d.ref, lint_device(d) == [], lint_device(d))
    CHECK("A2 U2 EXTEST opcode read from BSDL", devs["U2"].opcodes["EXTEST"] == ["100110"])
    CHECK("A2 chain length", sum(d.length for d in chain) == 52 + 63 + 17, [d.length for d in chain])


def t_chain():
    devs, chain, nets, hold = load_board()
    a = {d.ref: [0] * d.length for d in chain}
    a["U3"][0] = 1  # TDO 쪽 장치의 cell 0 은 TDO 에 가장 가깝다
    s = to_stream(a, chain)
    CHECK("A3 stream: cell 0 of TDO-side device shifts first", s[0] == 1 and sum(s) == 1)
    a["U3"][0] = 0
    a["U1"][-1] = 1  # TDI 쪽 장치의 마지막 셀은 맨 끝에 들어간다
    s = to_stream(a, chain)
    CHECK("A3 stream: last cell of TDI-side device shifts last", s[-1] == 1 and sum(s) == 1)
    b = VirtualBoard(chain, nets)
    b.load_ir(ir_stream(chain, "EXTEST"))
    CHECK("A3 IR decode per device", all(b.instr[d.ref] == "EXTEST" for d in chain), b.instr)
    b.load_ir(ir_stream(chain, "PRELOAD"))
    CHECK("A3 PRELOAD does not drive", all(b._drive(*p) is None for n in nets.values() for p in n.pins))


def t_roles():
    devs, chain, nets, hold = load_board()
    r = net_roles(nets, devs)
    CHECK("A4 DATA0 two drivers", len(r["DATA0"]["drivers"]) == 2 and r["DATA0"]["kind"] == "testable", r["DATA0"])
    CHECK("A4 ADDR0 one driver, two rx", len(r["ADDR0"]["drivers"]) == 1 and len(r["ADDR0"]["receivers"]) == 2, r["ADDR0"])
    CHECK("A4 CLK_25M observe_only", r["CLK_25M"]["kind"] == "observe_only", r["CLK_25M"]["kind"])
    CHECK("A4 TP_VSENSE unscannable", r["TP_VSENSE"]["kind"] == "unscannable", r["TP_VSENSE"]["kind"])
    CHECK("A4 PMIC_EN drive_only", r["PMIC_EN"]["kind"] == "drive_only", r["PMIC_EN"]["kind"])
    CHECK("A4 ADDR3 pin mapping through PIN_MAP", ("U3", "A(3)") in r["ADDR3"]["receivers"], r["ADDR3"]["receivers"])


CODE_CASES = [(2, 2), (3, 3), (6, 3), (14, 4), (15, 5), (36, 6), (62, 6), (63, 7)]


def t_codes():
    for n, k in CODE_CASES:
        codes, got = gen_counting_modified(n)
        ones = (1 << got) - 1
        CHECK("A5 modified counting n=%d k=%d" % (n, k), got == k and len(set(codes)) == n and all(0 < c < ones for c in codes),
              (got, codes[:4]))
    codes, k = gen_walking_1(5)
    CHECK("A5 walking-1 one-hot", k == 5 and codes == [1, 2, 4, 8, 16], (codes, k))
    codes, k = gen_walking_0(3)
    CHECK("A5 walking-0 dual", k == 3 and codes == [6, 5, 3], (codes, k))
    codes, k = gen_true_complement(4)
    CHECK("A5 true/complement", k == 4 and all(((c >> 2) ^ (c & 3)) == 3 for c in codes), (codes, k))


def board_setup(float_default=1, prefer=("U1", "U2", "U3")):
    devs, chain, nets, hold = load_board()
    roles = net_roles(nets, devs)
    plan = plan_drivers(roles, devs, prefer=list(prefer), hold=hold)
    return devs, chain, nets, hold, roles, plan


def t_extest():
    devs, chain, nets, hold, roles, plan = board_setup()
    codes, k = assign_codes(plan, gen_counting_modified)
    b = VirtualBoard(chain, nets)
    bits = vector_bits(plan, chain, codes, 0)
    ctrl_u1 = devs["U1"].ports()["D(0)"].ctrl
    ctrl_u2 = devs["U2"].ports()["D(0)"].ctrl
    CHECK("A6 enable polarity: U1 disval 0 -> driver on writes 1", bits["U1"][ctrl_u1] == 1, bits["U1"][ctrl_u1])
    CHECK("A6 enable polarity: U2 not driver -> disval 1 stays", bits["U2"][ctrl_u2] == 1, bits["U2"][ctrl_u2])
    resp = run_extest(b, plan, roles, codes, k)
    ok = all(v == codes[n] for n, m in resp.items() for v in m.values())
    CHECK("A6 good board: every receiver reads its code", ok,
          [(n, stv(v, k), stv(codes[n], k)) for n, m in resp.items() for v in m.values() if v != codes[n]][:3])
    CHECK("A6 good board: scans = 1 preload + k + 1 flush", b.n_scan == k + 2, b.n_scan)
    CHECK("A6 good board: no driver contention", b.contention == [], b.contention[:2])
    CHECK("A6 diagnose good board: no findings", diagnose(plan, roles, codes, k, resp) == [])


def inject(b, faults):
    for f in faults:
        getattr(b, "add_" + f[0])(*f[1:])
    return b


DIAG_CASES = [
    ("open rx on 2-rx net", [("open", "ADDR3", "U2", "A(3)")], "OPEN_RX", ["ADDR3"], ("U2", "A(3)")),
    ("open rx U3 side", [("open", "ADDR5", "U3", "A(5)")], "OPEN_RX", ["ADDR5"], ("U3", "A(5)")),
    ("open at far end of 2-pin net (read-back ok)", [("open", "DATA4", "U2", "D(4)")], "OPEN_NET", ["DATA4"], None),
    ("open at driver of 2-pin net (read-back ok)", [("open", "DATA4", "U1", "D(4)")], "OPEN_NET", ["DATA4"], None),
    ("stuck-at-0 with read-back", [("stuck", "DATA7", 0)], "STUCK", ["DATA7"], None),
    ("stuck-at-1 with read-back", [("stuck", "DATA8", 1)], "STUCK", ["DATA8"], None),
    ("stuck, no read-back on driver", [("stuck", "ADDR1", 0)], "STUCK_OR_OPEN", ["ADDR1"], None),
    ("open driver, no read-back", [("open", "ADDR2", "U1", "A(2)")], "STUCK_OR_OPEN", ["ADDR2"], None),
]


def t_diag():
    devs, chain, nets, hold, roles, plan = board_setup()
    codes, k = assign_codes(plan, gen_counting_modified)
    for name, faults, kind, fnets, pin in DIAG_CASES:
        b = inject(VirtualBoard(chain, nets), faults)
        f = diagnose(plan, roles, codes, k, run_extest(b, plan, roles, codes, k))
        good = len(f) == 1 and f[0]["kind"] == kind and f[0]["nets"] == fnets and (pin is None or f[0]["pin"] == pin)
        CHECK("A7 diag: %s -> %s" % (name, kind), good, f)
    # 단락: 코드 수준 진단이 아니라 실제 보드 응답으로
    a, b_ = "DATA5", "DATA6"
    b = inject(VirtualBoard(chain, nets), [("short", (a, b_), "or")])
    f = diagnose(plan, roles, codes, k, run_extest(b, plan, roles, codes, k))
    shorts = [x for x in f if x["kind"] == "SHORT"]
    CHECK("A7 diag: wired-OR short is one group", len(shorts) == 1 and {a, b_} <= set(shorts[0]["nets"] + shorts[0].get("maybe", [])), f)
    keeper = VirtualBoard(chain, nets, float_default="K")
    inject(keeper, [("open", "ADDR3", "U2", "A(3)")])
    f = diagnose(plan, roles, codes, k, run_extest(keeper, plan, roles, codes, k))
    CHECK("A7 keeper open still detected (non-constant codes)", len(f) == 1 and f[0]["kind"] == "OPEN_RX", f)


def t_safety():
    devs, chain, nets, hold, roles, plan = board_setup()
    CHECK("B1 one driver per net", all(p in roles[n]["drivers"] for n, p in plan.driver.items()) and len(plan.test) == 36,
          len(plan.test))
    codes, k = assign_codes(plan, gen_walking_1)
    b = VirtualBoard(chain, nets)
    run_extest(b, plan, roles, codes, k)
    CHECK("B1 walking-1 over all nets: zero contention", b.contention == [], b.contention[:2])
    # PRELOAD 를 건너뛴 순진한 흐름
    naive = VirtualBoard(chain, nets, seed=7)
    naive.load_ir(ir_stream(chain, "EXTEST"))
    CHECK("B2 skip PRELOAD: power-on garbage causes contention at Update-IR", len(naive.contention) > 0,
          len(naive.contention))
    # hold 넷은 절대 토글되면 안 된다
    b2 = VirtualBoard(chain, nets)
    codes, k = assign_codes(plan, gen_counting_modified)
    run_extest(b2, plan, roles, codes, k)
    CHECK("B3 held net RST_N never toggles", b2.toggles["RST_N"][1] == 0 and b2.toggles["RST_N"][0] == 1, b2.toggles["RST_N"])
    CHECK("B3 held nets actively driven from EXTEST entry (no float window)",
          b2.undriven.get("PMIC_EN", 0) == 0 and b2.undriven.get("RST_N", 0) == 0,
          (b2.undriven.get("PMIC_EN", 0), b2.undriven.get("RST_N", 0)))
    CHECK("B3 held net not in diagnosis set", "RST_N" not in plan.test and "PMIC_EN" not in plan.test)
    # 극성
    p2 = plan_drivers(roles, devs, prefer=["U2", "U1", "U3"], hold=hold)
    CHECK("B4 rotation: prefer U2 picks U2 on DATA nets", all(p2.driver["DATA%d" % i][0] == "U2" for i in range(16)))
    b3 = VirtualBoard(chain, nets)
    codes2, k2 = assign_codes(p2, gen_counting_modified)
    run_extest(b3, p2, roles, codes2, k2)
    CHECK("B4 rotated plan also has zero contention", b3.contention == [], b3.contention[:2])


def t_shared_control():
    # A 와 B 두 장치가 넷 N0/N1 을 공유. A 의 두 핀은 제어 셀 하나를 공유하고, N1 만 B 쪽에서 구동할 수 없으면 A 가 둘 다 구동해야 한다.
    def mk(ref, cell_lines, ir="0"):
        text = 'entity %s is\n attribute INSTRUCTION_LENGTH of %s : entity is 1;\n attribute INSTRUCTION_OPCODE of %s : entity is "EXTEST (%s)";\n attribute BOUNDARY_LENGTH of %s : entity is %d;\n attribute BOUNDARY_REGISTER of %s : entity is %s;\nend %s;' % (
            ref, ref, ref, ir, ref, len(cell_lines), ref, "&".join('"%s%s"' % (c, "," if i + 1 < len(cell_lines) else "") for i, c in enumerate(cell_lines)), ref)
        d = parse_bsdl(text)
        d.ref = ref
        return d
    A = mk("A", ["0 (BC_1, *, control, 0)", "1 (BC_1, P0, output3, X, 0, 0, Z)", "2 (BC_1, P1, output3, X, 0, 0, Z)",
                 "3 (BC_1, Q0, input, X)", "4 (BC_1, Q1, input, X)"])
    B = mk("B", ["0 (BC_1, *, control, 0)", "1 (BC_1, Q0, bidir, X, 0, 0, Z)", "2 (BC_1, Q1, input, X)"])
    devs = {"A": A, "B": B}
    net_text = "N0 A.P0 A.Q0 B.Q0\nN1 A.P1 A.Q1 B.Q1\n"
    nets, _ = parse_netlist(net_text, devs)
    roles = net_roles(nets, devs)
    plan = plan_drivers(roles, devs, prefer=["B", "A"])
    CHECK("B5 shared OE: N1 has only A as driver, so A must drive N0 too", plan.driver == {"N0": ("A", "P0"), "N1": ("A", "P1")},
          plan.driver)
    # output2 둘
    C = mk("C", ["0 (BC_1, X0, output2, 1)"])
    D = mk("D", ["0 (BC_1, X0, output2, 1)", "1 (BC_1, X1, input, X)"])
    devs2 = {"C": C, "D": D}
    nets2, _ = parse_netlist("BAD C.X0 D.X0 D.X1\n", devs2)
    r2 = net_roles(nets2, devs2)
    p = plan_drivers(r2, devs2)
    CHECK("B6 two output2 on a net -> unsafe, not tested", p.unsafe and p.unsafe[0][0] == "BAD" and "BAD" not in p.test, p.unsafe)
    # 무접속 핀은 제어를 공유해도 괜찮다 / 넷에 있으면 충돌
    E = mk("E", ["0 (BC_1, *, control, 0)", "1 (BC_1, P0, output3, X, 0, 0, Z)", "2 (BC_1, P1, output3, X, 0, 0, Z)"])
    F = mk("F", ["0 (BC_1, *, control, 0)", "1 (BC_1, P1, bidir, X, 0, 0, Z)", "2 (BC_1, P2, input, X)"])
    devs3 = {"E": E, "F": F}
    nets3, _ = parse_netlist("M0 E.P0 F.P2\nM1 E.P1 F.P1\n", devs3)
    r3 = net_roles(nets3, devs3)
    p3 = plan_drivers(r3, devs3, prefer=["F", "E"])
    CHECK("B7 shared OE forces E to drive M1 too", p3.driver["M0"] == ("E", "P0") and p3.driver["M1"] == ("E", "P1"), p3.driver)
    # 켜진 제어 셀의 형제 핀이 계획 밖 넷(패턴을 안 넣는 넷)에 있으면 그 넷을 구동해 버린다 → 계획 자체를 거부
    G = mk("G", ["0 (BC_1, *, control, 0)", "1 (BC_1, P0, output3, X, 0, 0, Z)", "2 (BC_1, P1, output3, X, 0, 0, Z)"])
    H = mk("H", ["0 (BC_1, R0, input, X)"])
    devs4 = {"G": G, "H": H}
    nets4, _ = parse_netlist("T0 G.P0 H.R0\nEXT G.P1\n", devs4)
    r4 = net_roles(nets4, devs4)
    try:
        plan_drivers(r4, devs4)
        refused = False
    except PlanError:
        refused = True
    CHECK("B7 sibling on a net outside the plan -> PlanError", refused)


def t_rotation():
    devs, chain, nets, hold, roles, plan = board_setup()
    codes, k = assign_codes(plan, gen_counting_modified)
    faults = [("dead", "DATA11", "U2", "D(11)")]
    b = inject(VirtualBoard(chain, nets), faults)
    f = diagnose(plan, roles, codes, k, run_extest(b, plan, roles, codes, k))
    CHECK("B8 dead driver on non-driving side: invisible without rotation", f == [], f)
    p2 = plan_drivers(roles, devs, prefer=["U2", "U1", "U3"], hold=hold)
    codes2, k2 = assign_codes(p2, gen_counting_modified)
    b2 = inject(VirtualBoard(chain, nets), faults)
    f2 = diagnose(p2, roles, codes2, k2, run_extest(b2, p2, roles, codes2, k2))
    CHECK("B8 rotation exposes the dead driver", len(f2) == 1 and f2[0]["nets"] == ["DATA11"], f2)


def t_aliasing():
    # 넷 4개: 코드 1..4, k=3
    codes, k = gen_counting_modified(4)
    CHECK("B9 modified counting n=4", codes == [1, 2, 3, 4] and k == 3, (codes, k))
    # 넷0(001) & 넷2(011) = 001 → 넷2 는 넷0 의 코드를 읽는다. 넷0 은 멀쩡해 보인다 (부분집합 alias)
    read = short_read(codes, [(0, 2)], "and")
    got, amb = diagnose_codes(codes, k, read)
    CHECK("B9 AND short with subset code: partner found via alias", got == {frozenset([0, 2])} and not amb, (got, amb))
    # 코드 5(101) & 6(110) = 4(100): 제3의 넷 코드와 같다 → 넷 3개가 의심된다 (진짜 aliasing)
    codes6, k6 = gen_counting_modified(6)
    read = short_read(codes6, [(4, 5)], "and")
    got, amb = diagnose_codes(codes6, k6, read)
    CHECK("B9 aliasing: AND of 5 and 6 equals net 3's code", got == {frozenset([3, 4, 5])} and not amb, got)
    # confounding: 두 쌍이 같은 응답 → 하나로 뭉친다. 코드 3,5 (AND 1) 와 코드 9,10... 여기서는 walking-1 + wired-AND
    wcodes, wk = gen_walking_1(4)
    read = short_read(wcodes, [(0, 1), (2, 3)], "and")
    got, amb = diagnose_codes(wcodes, wk, read)
    CHECK("B9 confounding: walking-1 + two AND pairs merge into one group", got == {frozenset([0, 1, 2, 3])} and amb, (got, amb))
    read = short_read(wcodes, [(0, 1), (2, 3)], "or")
    got, amb = diagnose_codes(wcodes, wk, read)
    CHECK("B9 walking-1 + wired-OR separates the two pairs", got == {frozenset([0, 1]), frozenset([2, 3])} and not amb, got)
    # 수치표에서 확인되는 사실
    t = study()
    CHECK("B10 walking-1 exact on OR pairs 100%", t["walking-1"]["pair-OR"][0] == t["walking-1"]["pair-OR"][2], t["walking-1"]["pair-OR"])
    CHECK("B10 walking-1 exact on AND pairs 0%", t["walking-1"]["pair-AND"][0] == 0, t["walking-1"]["pair-AND"])
    CHECK("B10 walking-0 exact on AND pairs 100%", t["walking-0"]["pair-AND"][0] == t["walking-0"]["pair-AND"][2], t["walking-0"]["pair-AND"])
    mc = t["counting-mod"]["pair-AND"]
    CHECK("B10 counting detects every pair but diagnoses few exactly", mc[1] == mc[2] and mc[0] < mc[2] / 2, mc)
    tc = t["true-compl"]
    CHECK("B10 true/complement beats counting on AND pairs", tc["pair-AND"][0] > mc[0], (tc["pair-AND"], mc))
    tw = t["2-step"]
    CHECK("B10 2-step exact on all pair/2-pair/triple scenarios",
          all(tw[s][0] == tw[s][2] for s in ("pair-AND", "pair-OR", "2pairs-AND", "2pairs-OR", "triple-AND")),
          {s: tw[s] for s in ("pair-AND", "pair-OR", "2pairs-AND", "2pairs-OR", "triple-AND")})
    CHECK("B10 2-step uses far fewer vectors than walking both ways", tw["avg_vectors"] < 2 * 14, tw["avg_vectors"])


def t_limits():
    # 한계 1: 단순 카운팅의 all-0 코드 넷은 SA0 을 못 잡는다
    codes, k = gen_counting_plain(8)
    read = list(codes)
    read[0] = 0  # 넷 0 은 코드 0: stuck-at-0 이어도 응답이 같다
    CHECK("L1 plain counting misses SA0 on all-zero code", read == codes, read)
    read = list(codes)
    read[7] = (1 << k) - 1  # N=2^k 이면 마지막 코드가 all-1
    CHECK("L1 plain counting misses SA1 on all-one code (n=2^k)", read == codes)
    mc, mk = gen_counting_modified(8)
    CHECK("L1 modified counting has no constant code (n=8 needs 4 vectors)", mk == 4 and all(0 < c < 15 for c in mc), (mc, mk))
    # 한계 2: 다중 결함 — 유일한 수신 핀의 open 이 단락 짝을 가린다
    devs, chain, nets, hold, roles, plan = board_setup()
    codes, k = assign_codes(plan, gen_counting_modified)
    b = inject(VirtualBoard(chain, nets), [("open", "nCS", "U2", "nCS"), ("short", ("nCS", "nWE"), "and")])
    fi = diagnose(plan, roles, codes, k, run_extest(b, plan, roles, codes, k))
    CHECK("L2 multi-fault masking: open hides the short partner (limit shown)",
          any(f["kind"] == "STUCK_OR_OPEN" and f["nets"] == ["nCS"] for f in fi) and
          not any(f["kind"] == "SHORT" and set(f["nets"]) == {"nCS", "nWE"} for f in fi), fi)
    # 한계 3: 스캔 없는 넷과의 단락은 보이지 않는다
    b = inject(VirtualBoard(chain, nets), [("short", ("ADDR0", "TP_VSENSE"), "and")])
    fi = diagnose(plan, roles, codes, k, run_extest(b, plan, roles, codes, k))
    CHECK("L3 short to a non-scan net is invisible", fi == [], fi)
    # 한계 4: 강한 드라이버 단락(dom)은 카운팅만으로 종류를 모른다, 2단계가 가른다
    b = inject(VirtualBoard(chain, nets), [("short", ("DATA1", "DATA2"), "dom:DATA1")])
    final, info = adaptive_test(b, plan, roles, codes, k)
    CHECK("L4 dominant short resolved with direction by 2-step",
          any(f["kind"] == "SHORT" and f["nets"] == ["DATA1", "DATA2"] and f["short_kind"] == "dom:DATA1" for f in final), final)


PLANTED = [
    ("open", "ADDR3", "U2", "A(3)"),
    ("short", ("DATA5", "DATA6"), "and"),
    ("stuck", "nCS", 0),
    ("short", ("BUF2", "BUF3"), "or"),
]
EXPECT_C = [
    ("OPEN_RX", ["ADDR3"]),
    ("SHORT", ["DATA5", "DATA6"]),
    ("STUCK_OR_OPEN", ["nCS"]),
    ("SHORT", ["BUF2", "BUF3"]),
]


def expected_for(fault, plan, roles):
    """임의 단일 결함 → (kind 집합, 넷 집합) 정답. 2핀 넷은 어느 쪽 끝이 open 인지 원리적으로 알 수 없다."""
    if fault[0] == "stuck":
        rb = plan.driver[fault[1]] in roles[fault[1]]["receivers"]
        return {"STUCK"} if rb else {"STUCK_OR_OPEN"}, [fault[1]]
    if fault[0] == "dead":
        return {"STUCK", "STUCK_OR_OPEN"}, [fault[1]]
    if fault[0] == "short":
        return {"SHORT"}, sorted(fault[1])
    net = fault[1]
    pin = (fault[2], fault[3])
    drv = plan.driver[net]
    has_rb = drv in roles[net]["receivers"]
    rx = [p for p in roles[net]["receivers"] if p != drv]
    if pin != drv and len(rx) >= 2:
        return {"OPEN_RX"}, [net]
    if not has_rb:
        return {"STUCK_OR_OPEN"}, [net]
    return ({"OPEN_DRV"} if len(rx) >= 2 else {"OPEN_NET"}), [net]


def t_campaign():
    devs, chain, nets, hold, roles, plan = board_setup()
    codes, k = assign_codes(plan, gen_counting_modified)
    CHECK("C1 VAL-R2: 36 nets tested in 6 counting vectors", k == 6 and len(plan.test) == 36, (k, len(plan.test)))
    good = VirtualBoard(chain, nets)
    final, info = adaptive_test(good, plan, roles, codes, k)
    CHECK("C1 good board PASS, no step 2", final == [] and info["vectors"] == k, (final, info))
    b = inject(VirtualBoard(chain, nets), PLANTED)
    final, info = adaptive_test(b, plan, roles, codes, k)
    got = sorted((f["kind"], sorted(f["nets"])) for f in final)
    want = sorted((a, sorted(n)) for a, n in EXPECT_C)
    CHECK("C2 four planted defects diagnosed exactly", got == want, got)
    opn = [f for f in final if f["kind"] == "OPEN_RX"]
    CHECK("C2 open reported at the receiver pin", opn and opn[0]["pin"] == ("U2", "A(3)"), opn)
    kinds = {tuple(f["nets"]): f.get("short_kind") for f in final if f["kind"] == "SHORT"}
    CHECK("C2 short types recovered", kinds.get(("DATA5", "DATA6")) == "and" and kinds.get(("BUF2", "BUF3")) == "or", kinds)
    CHECK("C2 only suspects got walking vectors", info["vectors"] == k + 2 * (len(info["suspects"]) + 1) and
          info["vectors"] < 2 * len(plan.test), info)
    rep = format_report("VAL-R2 board S/N 0042", chain, plan, roles, codes, k, final, info, b)
    text = "\n".join(rep)
    CHECK("C3 report: chain header", "U1(SNS_SOC" in rep[1] and "U3(LVL_BUF" in rep[1], rep[1])
    CHECK("C3 report: open line names the pin", "FAIL net=ADDR3 class=OPEN_RX pin=U2.A(3)" in text, text)
    CHECK("C3 report: AND short with type", "net=DATA5+DATA6 class=SHORT" in text and "type=and" in text, text)
    CHECK("C3 report: excluded nets listed", "CLK_25M" in text and "TP_VSENSE" in text and "RST_N=1" in text, text)
    CHECK("C3 report: verdict", text.strip().endswith("VERDICT FAIL (4 findings)"), rep[-1])
    # 무작위 단일 결함 캠페인 (결정적 seed)
    rng = random.Random(20261108)
    testable = sorted(plan.test)
    n_ok, n_total, misses = 0, 0, []
    for trial in range(120):
        roll = rng.randrange(4)
        if roll == 0:
            net = rng.choice(testable)
            ref, port = rng.choice(roles[net]["pins"])
            fault = ("open", net, ref, port)
        elif roll == 1:
            fault = ("stuck", rng.choice(testable), rng.randrange(2))
        elif roll == 2:
            a, b2 = sorted(rng.sample(testable, 2))
            fault = ("short", (a, b2), rng.choice(("and", "or")))
        else:
            net = rng.choice(testable)
            fault = ("dead", net) + tuple(plan.driver[net])
        board = inject(VirtualBoard(chain, nets, seed=trial), [fault])
        final, info = adaptive_test(board, plan, roles, codes, k)
        kinds, names = expected_for(fault, plan, roles)
        ok = len(final) == 1 and final[0]["kind"] in kinds and sorted(final[0]["nets"]) == names
        n_ok += ok
        n_total += 1
        if not ok:
            misses.append((fault, final))
    CHECK("C4 random single-fault campaign: %d boards localized" % n_total, n_ok == n_total, misses[:3])


TESTS = [t_parse, t_lint, t_chain, t_roles, t_codes, t_extest, t_diag, t_safety, t_shared_control, t_rotation, t_aliasing,
         t_limits, t_campaign]


def run_tests():
    for t in TESTS:
        try:
            t()
        except NotImplementedError:
            CHECK(t.__name__ + " (TODO not implemented)", False)
        except Exception as e:  # 스텁이 None 을 돌려줘도 하네스는 끝까지 돈다
            CHECK(t.__name__ + " (exception)", False, "%s: %s" % (type(e).__name__, e))
    print("%d/%d passed" % (PASSED[0], TOTAL[0]))
    return PASSED[0] == TOTAL[0]


def report():
    cols = ("pair-AND", "pair-OR", "2pairs-AND", "2pairs-OR", "triple-AND")
    for n in (14, 36):
        print("\n--- aliasing/confounding study: n=%d nets, exact diagnosis %%" % n)
        print("%-13s %9s %9s %11s %10s %11s %8s" % (("scheme",) + cols + ("avg_vec",)))
        for name, row in study(n).items():
            print("%-13s" % name + " ".join("%9.1f" % (100.0 * row[c][0] / row[c][2]) for c in cols) + " %8.1f" % row["avg_vectors"])
    devs, chain, nets, hold, roles, plan = board_setup()
    codes, k = assign_codes(plan, gen_counting_modified)
    b = inject(VirtualBoard(chain, nets), PLANTED)
    final, info = adaptive_test(b, plan, roles, codes, k)
    print()
    print("\n".join(format_report("VAL-R2 board S/N 0042", chain, plan, roles, codes, k, final, info, b)))
    print("scan bits: %d in %d DR scans" % (b.n_bits, b.n_scan))


def main(argv):
    ok = run_tests()
    if "--report" in argv[1:]:
        report()
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
