# TOOL-M07 practice.py (= ringbuf_cmd.py 확장판) — solution.py + 추가 연습 답:
#   ① rbcheck 불변식 검사  ② 합성 자식(synthetic children)  ③ 파이썬 워치포인트 콜백  ④ 파이썬 BP 콜백
#   (lldb) command script import ringbuf_cmd.py      ← check.sh 는 <variant>.py 를 이 이름으로 복사해서 쓴다
#   (lldb) rbdump dev.tx        rbdump rb        rbdump &dev.tx
#   (lldb) p dev.tx   /  frame variable rb  /  bt  ← 요약이 자동으로 붙는다
# lldb 내장 Python 3.9 — 3.10+ 문법(match, X | Y 타입) 쓰지 말 것.
import lldb


class RB(object):
    """SBValue 에서 꺼낸 링 버퍼 스냅샷. 여기부터는 파이썬 int 만 다룬다."""

    def __init__(self, addr, head, tail, count, cap, data):
        self.addr, self.head, self.tail = addr, head, tail
        self.count, self.cap, self.data = count, cap, data


def rb_address(valobj):
    # TODO(4): 구조체의 주소. valobj 는 `struct ringbuf` 일 수도 `struct ringbuf *` 일 수도 있다.
    if valobj.GetType().IsPointerType():
        return valobj.GetValueAsUnsigned()
    return valobj.GetLoadAddress()


def read_bytes(buf):
    # TODO(3): uint8_t[N] 배열 SBValue → 파이썬 int 리스트 (길이 N)
    return [buf.GetChildAtIndex(i).GetValueAsUnsigned() for i in range(buf.GetNumChildren())]


def snapshot(valobj):
    """struct 든 포인터든 GetChildMemberWithName 은 멤버를 돌려준다 (포인터면 자동으로 따라간다)."""
    valobj = valobj.GetNonSyntheticValue()   # ② 합성 자식이 등록돼 있으면 원래 멤버(head/tail/buf)가 가려진다
    buf = valobj.GetChildMemberWithName("buf")
    def u(name):
        return valobj.GetChildMemberWithName(name).GetValueAsUnsigned()
    return RB(rb_address(valobj), u("head"), u("tail"), u("count"),
              buf.GetNumChildren(), read_bytes(buf))


def logical_slots(rb):
    # TODO(1): 논리 순서(가장 오래된 바이트부터)로 물리 인덱스 목록. count 가 깨져 cap 보다 커도 cap 개까지만.
    return [(rb.tail + i) % rb.cap for i in range(min(rb.count, rb.cap))]


def rb_state(rb):
    # TODO(2): "empty" / "full" / "wrapped"(끝을 넘어 0번으로 이어짐) / "linear"
    if rb.count == 0:
        return "empty"
    if rb.count >= rb.cap:
        return "full"
    return "wrapped" if rb.tail + rb.count > rb.cap else "linear"


def rb_summary(valobj, internal_dict):
    """type summary add -F 용: 한 줄 요약. 예외를 던지면 lldb 가 요약을 그냥 비워 버린다."""
    rb = snapshot(valobj)
    s = "count=%d/%d tail=%d head=%d" % (rb.count, rb.cap, rb.tail, rb.head)
    if rb_state(rb) == "empty":
        return s + " empty"
    return s + " [" + " ".join("%02x" % rb.data[i] for i in logical_slots(rb)) + "]"


def find_value(frame, expr):
    v = frame.GetValueForVariablePath(expr)          # 변수 경로(dev.tx, rb, prb): 코드 실행 없이 빠르다
    if not v.IsValid() or v.GetError().Fail():
        v = frame.EvaluateExpression(expr)           # &dev.tx, (struct ringbuf *)0x... 같은 식
    return v


def rbdump(debugger, command, exe_ctx, result, internal_dict):
    """rbdump <expr> — struct ringbuf (또는 그 포인터) 를 논리 순서로 덤프한다"""
    expr = command.strip()
    frame = exe_ctx.GetFrame()
    if not expr:
        result.SetError("usage: rbdump <expr>")
        return
    if not frame.IsValid():
        result.SetError("rbdump: no frame (process not stopped?)")
        return
    v = find_value(frame, expr)
    if not v.IsValid() or v.GetError().Fail():
        result.SetError("rbdump: cannot evaluate '%s'" % expr)
        return
    if not v.GetNonSyntheticValue().GetChildMemberWithName("buf").IsValid():
        result.SetError("rbdump: '%s' is not a struct ringbuf" % expr)
        return
    rb = snapshot(v)
    result.AppendMessage("ringbuf @ 0x%x cap=%d count=%d tail=%d head=%d %s"
                         % (rb.addr, rb.cap, rb.count, rb.tail, rb.head, rb_state(rb)))
    for n, slot in enumerate(logical_slots(rb)):
        result.AppendMessage("  [%d] buf[%d] = 0x%02x" % (n, slot, rb.data[slot]))


# ---------------------------------------------------------------- ① rbcheck
def rb_problems(rb):
    """링 버퍼 자체의 불변식. 빈 리스트 = 정상."""
    p = []
    if rb.count > rb.cap:
        p.append("count %d > cap %d" % (rb.count, rb.cap))
    if rb.head >= rb.cap or rb.tail >= rb.cap:
        p.append("index out of range (head %d, tail %d)" % (rb.head, rb.tail))
    if rb.head != (rb.tail + rb.count) % rb.cap:
        p.append("head %d != (tail + count) %% cap = %d" % (rb.head, (rb.tail + rb.count) % rb.cap))
    return p


def rbcheck(debugger, command, exe_ctx, result, internal_dict):
    """rbcheck <expr> — 불변식 검사: OK 또는 CORRUPT: 이유; 이유; ..."""
    expr = command.strip()
    frame = exe_ctx.GetFrame()
    v = find_value(frame, expr) if expr and frame.IsValid() else lldb.SBValue()
    if not v.IsValid() or v.GetError().Fail():
        result.SetError("rbcheck: cannot evaluate '%s'" % expr)
        return
    p = rb_problems(snapshot(v))
    result.AppendMessage("rbcheck %s: %s" % (expr, "OK" if not p else "CORRUPT: " + "; ".join(p)))


# ---------------------------------------------------------------- ② 합성 자식: frame variable 이 논리 순서 [0]..[count-1] 로 보여 준다
class RingbufSynth(object):
    def __init__(self, valobj, internal_dict):
        self.valobj = valobj
        self.update()

    def update(self):
        rb = snapshot(self.valobj)
        self.buf = self.valobj.GetNonSyntheticValue().GetChildMemberWithName("buf")
        self.slots = logical_slots(rb)
        return False            # 캐시 안 함: 멈출 때마다 다시 읽는다

    def num_children(self):
        return len(self.slots)

    def get_child_index(self, name):
        try:
            return int(name.lstrip("[").rstrip("]"))
        except ValueError:
            return -1

    def get_child_at_index(self, i):
        if i < 0 or i >= len(self.slots):
            return None
        return self.buf.GetChildAtIndex(self.slots[i]).Clone("[%d]" % i)

    def has_children(self):
        return True


# ---------------------------------------------------------------- ③ 워치포인트 콜백 = 파이썬으로 쓴 조건
WP_HITS = [0]


def wp_bad_link_state(frame, wp, internal_dict):
    """watchpoint command add -F: True = 멈춤, False = 조용히 계속. 감시 주소를 직접 읽는다."""
    WP_HITS[0] += 1
    err = lldb.SBError()
    val = frame.GetThread().GetProcess().ReadUnsignedFromMemory(wp.GetWatchAddress(), wp.GetWatchSize(), err)
    if err.Fail() or val <= 3:
        return False
    print("[wp] link_state=0x%x after %d hits" % (val, WP_HITS[0]))
    return True


# ---------------------------------------------------------------- ④ BP 콜백: 마지막 칸(cap-1)에서 2바이트를 쓰려는 순간만 멈춤
BP_HITS = [0]


def bp_edge(frame, bp_loc, extra_args, internal_dict):
    BP_HITS[0] += 1
    rb = snapshot(frame.FindVariable("rb"))
    if rb.head != rb.cap - 1:
        return False
    print("[bp] rb_put_escaped at the last slot (head=%d) after %d calls" % (rb.head, BP_HITS[0]))
    return True


def __lldb_init_module(debugger, internal_dict):
    # __name__ = 모듈 이름 (파일 이름에서 .py 뺀 것). 하드코딩하면 파일 이름을 바꿀 때 깨진다.
    debugger.HandleCommand("command script add -o -f %s.rbdump rbdump" % __name__)
    debugger.HandleCommand("command script add -o -f %s.rbcheck rbcheck" % __name__)
    debugger.HandleCommand("type summary add -F %s.rb_summary ringbuf" % __name__)
    debugger.HandleCommand("type synthetic add -l %s.RingbufSynth ringbuf" % __name__)
