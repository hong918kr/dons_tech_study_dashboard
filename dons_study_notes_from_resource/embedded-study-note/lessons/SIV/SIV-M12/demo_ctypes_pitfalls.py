#!/usr/bin/env python3
"""SIV-M12 데모 — ctypes 러너와 바이너리 러너의 차이를 실제로 본다 (CHECK 없음, 관찰용)

  1) restype 을 안 정하면 char* 가 int 로 온다 / 구조체 레이아웃이 어긋나면 크기 검사가 잡는다
  2) ctypes 로 부른 C 코드가 SIGSEGV → 파이썬 프로세스(=시퀀서)가 통째로 죽는다 vs 바이너리 러너는 CRASH 한 줄
  3) ctypes 로 부른 C 코드가 무한 루프 → 스레드 join(timeout) 은 '감지'만, 죽일 방법이 없다
  4) 호출 1회 비용: ctypes vs 프로세스 생성 (실측)

  python3 -I demo_ctypes_pitfalls.py
"""
import ctypes
import importlib.util
import os
import subprocess
import sys
import tempfile
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("siv_m12_solution", os.path.join(HERE, "solution.py"))
S = importlib.util.module_from_spec(spec)
spec.loader.exec_module(S)

CHILD_CRASH = """
import ctypes, sys
L = ctypes.CDLL(sys.argv[1])
class R(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int32), ("checks", ctypes.c_uint32), ("addr", ctypes.c_uint32),
                ("expected", ctypes.c_uint32), ("actual", ctypes.c_uint32),
                ("reg", ctypes.c_char * 16), ("msg", ctypes.c_char * 96)]
L.siv_set_defects(8)
print("child: calling dma_desc(desc_count=24) via ctypes ...", flush=True)
L.siv_run(b"dma_desc", b"desc_count=24", 0, 0, ctypes.byref(R()))
print("child: never printed", flush=True)
"""

CHILD_HANG = """
import ctypes, sys, threading, time, os
L = ctypes.CDLL(sys.argv[1])
class R(ctypes.Structure):
    _fields_ = [("status", ctypes.c_int32), ("checks", ctypes.c_uint32), ("addr", ctypes.c_uint32),
                ("expected", ctypes.c_uint32), ("actual", ctypes.c_uint32),
                ("reg", ctypes.c_char * 16), ("msg", ctypes.c_char * 96)]
L.siv_set_env(800, -40)
L.siv_set_defects(4)
t = threading.Thread(target=lambda: L.siv_run(b"pll_lock", b"max_polls=0", 0, 0, ctypes.byref(R())), daemon=True)
t0 = time.monotonic()
t.start()
t.join(0.3)
print("child: join(0.3) returned after %.2f s, thread alive = %s" % (time.monotonic() - t0, t.is_alive()), flush=True)
print("child: main thread still runs (ctypes released the GIL), but nothing can stop the C loop;", flush=True)
print("child: only process exit ends it (daemon thread) -> os._exit(7)", flush=True)
os._exit(7)
"""


def main():
    with tempfile.TemporaryDirectory() as td:
        lib, exe = S.build_c(td)
        print("== 1) restype / struct layout ==")
        L = ctypes.CDLL(lib)
        raw = L.siv_test_name(0)
        print("siv_test_name(0) without restype -> %r (%s)" % (raw, type(raw).__name__))
        L.siv_test_name.restype = ctypes.c_char_p
        print("siv_test_name(0) with c_char_p   -> %r" % L.siv_test_name(0))

        class Short(ctypes.Structure):  # msg 를 64 바이트로 잘못 옮긴 버전
            _fields_ = S.SivResult._fields_[:-1] + [("msg", ctypes.c_char * 64)]
        L.siv_result_size.restype = ctypes.c_size_t
        print("siv_result_size() = %d, sizeof(SivResult) = %d, sizeof(Short) = %d -> Short 는 C 가 32 바이트 넘쳐 씀"
              % (L.siv_result_size(), ctypes.sizeof(S.SivResult), ctypes.sizeof(Short)))

        print("\n== 2) crash: ctypes vs binary ==")
        p = subprocess.run([sys.executable, "-I", "-c", CHILD_CRASH, lib], capture_output=True, text=True, timeout=20)
        print(p.stdout.rstrip())
        print("child (sequencer) returncode = %d  -> the whole sequencer process died" % p.returncode)
        o = S.BinaryRunner(exe).run(S.TestSpec("dma.ring", "dma_desc", params={"desc_count": 24}),
                                    {"vdd_mv": 800, "temp_c": 25, "defects": 8}, 0, 0)
        print("binary runner -> Outcome(status=%r, msg=%r)  -> recorded, sequencer continues" % (o.status, o.msg))

        print("\n== 3) hang: ctypes call cannot be killed ==")
        p = subprocess.run([sys.executable, "-I", "-c", CHILD_HANG, lib], capture_output=True, text=True, timeout=20)
        print(p.stdout.rstrip())
        print("child returncode = %d" % p.returncode)

        print("\n== 4) cost per call (id_check) ==")
        ct, br = S.CTypesRunner(lib), S.BinaryRunner(exe)
        sp, env = S.TestSpec("reg.id", "id_check"), {"vdd_mv": 800, "temp_c": 25, "defects": 0}
        for name, r, n in (("ctypes", ct, 2000), ("binary", br, 100)):
            t0 = time.perf_counter()
            for _ in range(n):
                assert r.run(sp, env, 0, 0).status == "PASS"
            dt = (time.perf_counter() - t0) / n
            print("%-7s %5d calls, %9.1f us/call" % (name, n, dt * 1e6))


if __name__ == "__main__":
    main()
