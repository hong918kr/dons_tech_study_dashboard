"""Python 스레드 실측: 진짜 OS 스레드인가 · 락 없는 += 손실 · CPU-bound 스레드 vs 프로세스 · I/O-bound.

  python3 experiments/gil/py_threads_gil.py
"""
import sys, threading, time, os
from concurrent.futures import ProcessPoolExecutor
def work(n):
    s = 0
    for i in range(n): s += i * i
    return s

def main():
    print("python", sys.version.split()[0], "| switch interval", sys.getswitchinterval(), "s | cores", os.cpu_count())
    # 1) 실제 OS 스레드인가?
    ids = set()
    def who(): ids.add(threading.get_native_id())
    ts = [threading.Thread(target=who) for _ in range(4)]
    [t.start() for t in ts]; [t.join() for t in ts]
    print("1) native thread ids:", len(ids), "distinct (main", threading.get_native_id(), ")")

    # 2) 락 없이 += → 업데이트 손실?
    class C: n = 0
    c = C()
    def inc(k):
        for _ in range(k):
            c.n += 1
    lost = []
    for trial in range(5):
        c.n = 0
        ts = [threading.Thread(target=inc, args=(200_000,)) for _ in range(4)]
        [t.start() for t in ts]; [t.join() for t in ts]
        lost.append(800_000 - c.n)
    print("2) no-lock lost updates per trial (of 800000):", lost)

    lock = threading.Lock()
    def inc_locked(k):
        for _ in range(k):
            with lock:
                c.n += 1
    c.n = 0
    ts = [threading.Thread(target=inc_locked, args=(200_000,)) for _ in range(4)]
    [t.start() for t in ts]; [t.join() for t in ts]
    print("   with Lock:", 800_000 - c.n, "lost")

    # 3) CPU-bound: 스레드 vs 프로세스
    N = 3_000_000
    t0 = time.perf_counter(); [work(N) for _ in range(4)]; seq = time.perf_counter() - t0
    t0 = time.perf_counter()
    ts = [threading.Thread(target=work, args=(N,)) for _ in range(4)]
    [t.start() for t in ts]; [t.join() for t in ts]; thr = time.perf_counter() - t0
    t0 = time.perf_counter()
    with ProcessPoolExecutor(4) as ex: list(ex.map(work, [N] * 4))
    proc = time.perf_counter() - t0
    print(f"3) CPU-bound x4: sequential {seq:.2f}s | 4 threads {thr:.2f}s | 4 processes {proc:.2f}s")

    # 4) I/O-bound (sleep 은 GIL 을 놓는다)
    t0 = time.perf_counter()
    ts = [threading.Thread(target=time.sleep, args=(0.5,)) for _ in range(4)]
    [t.start() for t in ts]; [t.join() for t in ts]
    print(f"4) 4 threads x sleep(0.5): {time.perf_counter() - t0:.2f}s (순차면 2.0s)")

if __name__ == '__main__':
    main()
